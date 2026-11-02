// SynTHBloK MATRIX-ECHO: the MATRIX FINAL-stage DELAY / SPACE as a standalone stereo delay and looper.
// 12 delay engines (CLASSIC = the MATRIX ping-pong), tap tempo / clock with 11 subdivisions (3 dual),
// freeze, kill dry, bypass with trails, a SPACE reverb on the repeats, 6 presets, and a 60 s stereo
// looper (REC / OVERDUB, PLAY / STOP, PLAY ONCE, UNDO / REDO, CLEAR, length snapped to the tempo).
#include "plugin.hpp"
#include "OledUi.hpp"
#include "KnobTicks.hpp"
#include "JackLed.hpp"
#include "Echo_dsp.hpp"

namespace {

const char* const kEchoTypeName[ET_NUM] = {"CLASSIC", "2290", "2290 MOD", "ANALOG", "TAPE", "LO-FI",
                                           "DYNAMIC", "REVERSE", "PING-PONG", "SLAP", "CRYSTAL", "SPACE"};

// Subdivisions of a beat (quarter note); b > 0 adds a second (dual) tap.
struct EchoSub { const char* name; float a, b; };
const EchoSub kEchoSub[11] = {{"1/2", 2.f, 0.f}, {"1/4.", 1.5f, 0.f}, {"1/4", 1.f, 0.f}, {"1/4T", 2.f / 3.f, 0.f},
                              {"1/8.", 0.75f, 0.f}, {"1/8", 0.5f, 0.f}, {"1/8T", 1.f / 3.f, 0.f}, {"1/16", 0.25f, 0.f},
                              {"1/4+1/8.", 1.f, 0.75f}, {"1/4+1/8T", 1.f, 1.f / 3.f}, {"1/8.+1/16", 0.75f, 0.25f}};

const char* const kLoopStateName[6] = {"EMPTY", "REC", "PLAY", "OVERDUB", "STOP", "ONCE"};
const char* const kClockDivName[3] = {"1/16 (MATRIX steps)", "1/8", "1/4 (beats)"};

} // namespace

struct SynTHBloKEcho : Module {
    enum ParamId { TYPE_PARAM, TIME_PARAM, SUBDIV_PARAM, FEEDBACK_PARAM, LEVEL_PARAM, TONE_PARAM, MOD_PARAM, SPACE_PARAM,
                   LOOP_LEVEL_PARAM, TAP_PARAM, FREEZE_PARAM, KILLDRY_PARAM, ON_PARAM,
                   REC_PARAM, PLAY_PARAM, ONCE_PARAM, UNDO_PARAM, CLEAR_PARAM, QUANT_PARAM, PREFX_PARAM,
                   STORE_PARAM, ENUMS(SLOT_PARAM, 6), PARAMS_LEN };
    enum InputId { IN_L_INPUT, IN_R_INPUT, CLOCK_INPUT, TAP_INPUT, FREEZE_INPUT, BYPASS_INPUT,
                   REC_INPUT, PLAY_INPUT, ONCE_INPUT, UNDO_INPUT, CLEAR_INPUT, INPUTS_LEN };
    enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, WET_L_OUTPUT, WET_R_OUTPUT, OUTPUTS_LEN };
    enum LightId { LIGHTS_LEN };
    static const int kPresetN = 8;   // a preset = the first 8 params (TYPE .. SPACE)
    static const int kSlots = 6;

    EchoEngine echo;
    EchoLooper looper;
    M8Reverb space;
    dsp::SchmittTrigger clockTrig, trig[6];   // TAP, REC, PLAY, ONCE, UNDO, CLEAR inputs
    bool btnPrev[6] = {}, slotPrev[kSlots] = {};
    float slots[kSlots][kPresetN] = {};
    bool slotUsed[kSlots] = {};
    int curSlot = -1;
    float slotLit[kSlots] = {};

    int curType = ET_CLASSIC;
    float typeGain = 1.f;
    int clockDiv = 0, clockCount = 0;          // clock pulse = 1/16, 1/8 or 1/4
    float sinceClock = 10.f, clockPeriod = 0.f;
    float sinceTap = 10.f, tapBeat = 0.f, tapKnob = 0.f, tapIv[3] = {};
    int tapN = 0;
    bool tapActive = false, trails = true;
    float beatPhase = 0.f, tapFlash = 0.f, recFlash = 0.f, playFlash = 0.f, onceFlash = 0.f;
    float onSm = 1.f, drySm = 1.f, freezeSm = 0.f, levelSm = 0.5f, spaceSm = 0.f, loopSm = 1.f;
    // for the displays
    float timeNow = 0.375f, beatNow = 0.f;
    int source = 0;   // 0 free (TIME knob), 1 tap, 2 clock
    bool onNow = true, freezeNow = false;

    SynTHBloKEcho() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configSwitch(TYPE_PARAM, 0.f, ET_NUM - 1, ET_CLASSIC, "Delay type",
                     {"CLASSIC (MATRIX ping-pong)", "2290 (clean digital)", "2290 MOD (chorused)", "ANALOG (BBD)", "TAPE (wow, flutter)",
                      "LO-FI (crushed)", "DYNAMIC (ducking)", "REVERSE", "PING-PONG", "SLAP (20-300 ms)", "CRYSTAL (octave shimmer)",
                      "SPACE (multi-head echo + reverb)"});
        configParam(TIME_PARAM, 0.f, 1.f, 0.5f, "Delay time (no clock / tap; SLAP: 20-300 ms)", " ms", 350.f, 20.f);
        std::vector<std::string> subs;
        for (const EchoSub& s : kEchoSub) subs.push_back(s.name);
        configSwitch(SUBDIV_PARAM, 0.f, 10.f, 4.f, "Subdivision of the clock / tapped beat", subs);
        configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.5f, "Feedback", "%", 0.f, 100.f);
        configParam(LEVEL_PARAM, 0.f, 1.f, 0.5f, "Delay level (dry stays at unity)", "%", 0.f, 100.f);
        configParam(TONE_PARAM, 0.f, 1.f, 0.5f, "Tone (dark - bright)", "%", 0.f, 100.f);
        configParam(MOD_PARAM, 0.f, 1.f, 0.f, "Modulation", "%", 0.f, 100.f);
        configParam(SPACE_PARAM, 0.f, 1.f, 0.f, "Space (reverb on the repeats)", "%", 0.f, 100.f);
        configParam(LOOP_LEVEL_PARAM, 0.f, 1.f, 1.f, "Loop level", "%", 0.f, 100.f);
        configButton(TAP_PARAM, "Tap tempo");
        configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze (hold the repeats)", {"Off", "On"});
        configSwitch(KILLDRY_PARAM, 0.f, 1.f, 0.f, "Kill dry", {"Off", "On"});
        configSwitch(ON_PARAM, 0.f, 1.f, 1.f, "Delay", {"Bypass", "On"});
        configButton(REC_PARAM, "Loop: record / overdub");
        configButton(PLAY_PARAM, "Loop: play / stop");
        configButton(ONCE_PARAM, "Loop: play once");
        configButton(UNDO_PARAM, "Loop: undo / redo the last overdub");
        configButton(CLEAR_PARAM, "Loop: clear");
        configSwitch(QUANT_PARAM, 0.f, 1.f, 1.f, "Loop length snaps to the clock / tapped beat", {"Off", "On"});
        configSwitch(PREFX_PARAM, 0.f, 1.f, 0.f, "Looper position", {"After the delay", "Before the delay (loops get echoed)"});
        configSwitch(STORE_PARAM, 0.f, 1.f, 0.f, "Store (then click a preset slot)", {"Off", "On"});
        for (int i = 0; i < kSlots; i++) configButton(SLOT_PARAM + i, string::f("Preset %d (click: load; STORE lit: save)", i + 1));
        for (int id : {FREEZE_PARAM, KILLDRY_PARAM, ON_PARAM, QUANT_PARAM, PREFX_PARAM, STORE_PARAM, TYPE_PARAM})
            getParamQuantity(id)->randomizeEnabled = false;
        configInput(IN_L_INPUT, "Left / mono");
        configInput(IN_R_INPUT, "Right (normalled to left)");
        configInput(CLOCK_INPUT, "Clock (sets the time with SUBDIV)");
        configInput(TAP_INPUT, "Tap tempo trigger");
        configInput(FREEZE_INPUT, "Freeze gate");
        configInput(BYPASS_INPUT, "Bypass gate");
        configInput(REC_INPUT, "Loop record / overdub trigger");
        configInput(PLAY_INPUT, "Loop play / stop trigger");
        configInput(ONCE_INPUT, "Loop play once trigger");
        configInput(UNDO_INPUT, "Loop undo / redo trigger");
        configInput(CLEAR_INPUT, "Loop clear trigger");
        configOutput(OUT_L_OUTPUT, "Left");
        configOutput(OUT_R_OUTPUT, "Right");
        configOutput(WET_L_OUTPUT, "Delay only, left");
        configOutput(WET_R_OUTPUT, "Delay only, right");
        configBypass(IN_L_INPUT, OUT_L_OUTPUT);
        configBypass(IN_R_INPUT, OUT_R_OUTPUT);

        // factory presets: TYPE, TIME, SUBDIV, FEEDBACK, LEVEL, TONE, MOD, SPACE
        static const float kFactory[kSlots][kPresetN] = {
            {ET_2290, 0.5f, 2, 0.4f, 0.5f, 0.55f, 0.f, 0.f},     {ET_TAPE, 0.5f, 4, 0.45f, 0.55f, 0.45f, 0.3f, 0.1f},
            {ET_ANALOG, 0.5f, 5, 0.55f, 0.5f, 0.4f, 0.2f, 0.f},  {ET_REVERSE, 0.5f, 2, 0.35f, 0.6f, 0.5f, 0.f, 0.2f},
            {ET_CRYSTAL, 0.5f, 1, 0.6f, 0.45f, 0.6f, 0.2f, 0.4f}, {ET_SPACE, 0.5f, 4, 0.5f, 0.5f, 0.45f, 0.3f, 0.3f}};
        for (int s = 0; s < kSlots; s++) {
            for (int i = 0; i < kPresetN; i++) slots[s][i] = kFactory[s][i];
            slotUsed[s] = true;
        }
        looper.init(APP->engine->getSampleRate(), 60.f);
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        if (e.sampleRate != looper.sr) looper.init(e.sampleRate, 60.f);
    }

    void onReset() override {
        looper.clear();
        curSlot = -1;
        tapActive = false;
    }

    void loadSlot(int s) {
        if (!slotUsed[s]) return;
        for (int i = 0; i < kPresetN; i++) params[i].setValue(slots[s][i]);
        if (tapActive) tapKnob = params[TIME_PARAM].getValue();   // global tempo: a preset keeps the tapped tempo
        curSlot = s;
    }
    void storeSlot(int s) {
        for (int i = 0; i < kPresetN; i++) slots[s][i] = params[i].getValue();
        slotUsed[s] = true;
        curSlot = s;
    }

    bool press(int btn, int param, int input) {   // a cell's press or a trigger on its jack
        bool b = params[param].getValue() > 0.5f;
        bool edge = b && !btnPrev[btn];
        btnPrev[btn] = b;
        if (input >= 0 && trig[btn].process(inputs[input].getVoltage(), 0.1f, 1.f)) edge = true;
        return edge;
    }

    void process(const ProcessArgs& args) override {
        const float sr = args.sampleRate, dt = args.sampleTime;
        auto smooth = [&](float& v, float target, float tau) { v += (target - v) * (1.f - std::exp(-dt / tau)); };

        // ---- tempo: clock wins, then tap (until the TIME knob moves), else the TIME knob ----
        static const float kPpq[3] = {4.f, 2.f, 1.f};
        sinceClock += dt;
        bool clocked = inputs[CLOCK_INPUT].isConnected();
        bool clockEdge = false;
        if (clocked && clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
            if (sinceClock < 4.f) clockPeriod = sinceClock;
            sinceClock = 0.f;
            clockEdge = true;
        }
        bool clockOn = clocked && clockPeriod > 0.f && sinceClock < std::max(2.f, 3.f * clockPeriod);
        if (press(0, TAP_PARAM, TAP_INPUT)) {
            if (sinceTap > 0.05f && sinceTap < 2.5f) {
                tapIv[tapN % 3] = sinceTap;
                tapN++;
                int n = std::min(tapN, 3);
                float sum = 0.f;
                for (int i = 0; i < n; i++) sum += tapIv[i];
                tapBeat = sum / n;
                tapActive = true;
                tapKnob = params[TIME_PARAM].getValue();
                beatPhase = 0.f;
            } else tapN = 0;
            sinceTap = 0.f;
        }
        sinceTap += dt;
        if (tapActive && std::fabs(params[TIME_PARAM].getValue() - tapKnob) > 0.01f) tapActive = false;
        float beat = clockOn ? clockPeriod * kPpq[clockDiv] : tapActive ? tapBeat : 0.f;
        source = clockOn ? 2 : tapActive ? 1 : 0;
        beatNow = beat;

        // ---- type change: fade the repeats out, switch, fade in ----
        int want = clamp((int)params[TYPE_PARAM].getValue(), 0, ET_NUM - 1);
        if (want != curType) { typeGain -= dt / 0.012f; if (typeGain <= 0.f) { typeGain = 0.f; curType = want; } }
        else typeGain = std::min(1.f, typeGain + dt / 0.012f);

        // ---- delay time ----
        int sub = clamp((int)params[SUBDIV_PARAM].getValue(), 0, 10);
        float kv = params[TIME_PARAM].getValue();
        float T, T2 = 0.f;
        if (curType == ET_SLAP) T = 0.02f * std::pow(15.f, kv);
        else if (beat > 0.f) {
            T = beat * kEchoSub[sub].a; T2 = beat * kEchoSub[sub].b;
            while (T > 7.f) { T *= 0.5f; T2 *= 0.5f; }
        } else {
            T = 0.02f * std::pow(350.f, kv);
            if (kEchoSub[sub].b > 0.f) T2 = T * kEchoSub[sub].b / kEchoSub[sub].a;
        }
        timeNow = T;

        // TAP lights on each beat (each repeat when free running)
        float period = beat > 0.f ? beat : T;
        if (clockOn) {
            if (clockEdge && clockCount++ % (int)kPpq[clockDiv] == 0) tapFlash = 0.06f;
        } else {
            beatPhase += dt;
            if (beatPhase >= period) { beatPhase -= period; tapFlash = 0.06f; }
        }
        tapFlash -= dt;

        // ---- switches ----
        freezeNow = params[FREEZE_PARAM].getValue() > 0.5f || inputs[FREEZE_INPUT].getVoltage() > 1.f;
        onNow = params[ON_PARAM].getValue() > 0.5f && !(inputs[BYPASS_INPUT].getVoltage() > 1.f);
        smooth(freezeSm, freezeNow ? 1.f : 0.f, 0.01f);
        smooth(onSm, onNow ? 1.f : 0.f, 0.01f);
        smooth(drySm, params[KILLDRY_PARAM].getValue() > 0.5f ? 0.f : 1.f, 0.01f);
        smooth(levelSm, params[LEVEL_PARAM].getValue(), 0.01f);
        smooth(spaceSm, params[SPACE_PARAM].getValue(), 0.01f);
        smooth(loopSm, params[LOOP_LEVEL_PARAM].getValue(), 0.01f);

        // ---- presets ----
        for (int s = 0; s < kSlots; s++) {
            bool b = params[SLOT_PARAM + s].getValue() > 0.5f;
            if (b && !slotPrev[s]) {
                if (params[STORE_PARAM].getValue() > 0.5f) { storeSlot(s); params[STORE_PARAM].setValue(0.f); }
                else loadSlot(s);
            }
            slotPrev[s] = b;
            slotLit[s] = s == curSlot ? 1.f : 0.f;
        }

        // ---- looper transport ----
        float qBeat = params[QUANT_PARAM].getValue() > 0.5f && beat > 0.f ? beat * sr : 0.f;
        if (press(1, REC_PARAM, REC_INPUT)) looper.rec(qBeat);
        if (press(2, PLAY_PARAM, PLAY_INPUT)) looper.play(qBeat);
        if (press(3, ONCE_PARAM, ONCE_INPUT)) looper.once(qBeat);
        if (press(4, UNDO_PARAM, UNDO_INPUT)) looper.undo();
        if (press(5, CLEAR_PARAM, CLEAR_INPUT)) looper.clear();

        // ---- audio (internal units: +-1 = +-5 V) ----
        float inL = inputs[IN_L_INPUT].getVoltage() / 5.f;
        float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() / 5.f : inL;
        bool pre = params[PREFX_PARAM].getValue() > 0.5f;
        float lL = 0.f, lR = 0.f;
        if (pre) looper.process(inL, inR, lL, lR);
        lL *= loopSm; lR *= loopSm;

        EchoParams p;
        p.type = curType; p.time = T; p.time2 = T2;
        p.fb = params[FEEDBACK_PARAM].getValue();
        p.tone = params[TONE_PARAM].getValue();
        p.mod = params[MOD_PARAM].getValue();
        p.freeze = freezeSm;
        float wL, wR, rl, rr;
        echo.process((inL + lL) * onSm, (inR + lR) * onSm, p, sr, wL, wR);   // bypass shuts the input: the tails ring on
        wL *= typeGain; wR *= typeGain;
        space.process(wL, wR, sr, rl, rr);
        wL += rl * spaceSm; wR += rr * spaceSm;
        float wg = levelSm * (trails ? 1.f : onSm);
        wL *= wg; wR *= wg;
        float oL = inL * drySm + lL + wL, oR = inR * drySm + lR + wR;
        if (!pre) {
            looper.process(oL, oR, lL, lR);
            oL += lL * loopSm; oR += lR * loopSm;
        }
        outputs[OUT_L_OUTPUT].setVoltage(clamp(5.f * oL, -12.f, 12.f));
        outputs[OUT_R_OUTPUT].setVoltage(clamp(5.f * oR, -12.f, 12.f));
        outputs[WET_L_OUTPUT].setVoltage(clamp(5.f * wL, -12.f, 12.f));
        outputs[WET_R_OUTPUT].setVoltage(clamp(5.f * wR, -12.f, 12.f));

        EchoLooper::State st = looper.st;
        recFlash = st == EchoLooper::REC || st == EchoLooper::DUB ? 1.f : 0.f;
        playFlash = st == EchoLooper::PLAY || st == EchoLooper::DUB ? 1.f : 0.f;
        onceFlash = st == EchoLooper::ONCE ? 1.f : 0.f;
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_t* arr = json_array();
        for (int s = 0; s < kSlots; s++) {
            if (!slotUsed[s]) { json_array_append_new(arr, json_null()); continue; }
            json_t* v = json_array();
            for (int i = 0; i < kPresetN; i++) json_array_append_new(v, json_real(slots[s][i]));
            json_array_append_new(arr, v);
        }
        json_object_set_new(root, "presets", arr);
        json_object_set_new(root, "curSlot", json_integer(curSlot));
        json_object_set_new(root, "clockDiv", json_integer(clockDiv));
        json_object_set_new(root, "trails", json_boolean(trails));
        return root;
    }
    void dataFromJson(json_t* root) override {
        if (json_t* arr = json_object_get(root, "presets")) {
            for (int s = 0; s < kSlots && s < (int)json_array_size(arr); s++) {
                json_t* v = json_array_get(arr, s);
                slotUsed[s] = json_is_array(v) && json_array_size(v) == (size_t)kPresetN;
                if (slotUsed[s])
                    for (int i = 0; i < kPresetN; i++) slots[s][i] = (float)json_number_value(json_array_get(v, i));
            }
        }
        if (json_t* j = json_object_get(root, "curSlot")) curSlot = clamp((int)json_integer_value(j), -1, kSlots - 1);
        if (json_t* j = json_object_get(root, "clockDiv")) clockDiv = clamp((int)json_integer_value(j), 0, 2);
        if (json_t* j = json_object_get(root, "trails")) trails = json_boolean_value(j);
    }
};

typedef SynTHBloKEcho EC;

// ------------------------------------------------------------ widgets ------------------------------------------------------------

namespace {

// Value arc around a knob (as on MATRIX-DRUM): amber track, orange arc.
struct EchoRing : TransparentWidget {
    Module* module = NULL;
    int param = 0;
    float width = 1.8f;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float v = module ? module->paramQuantities[param]->getScaledValue() : 0.5f;
        float cx = box.size.x * 0.5f, cy = box.size.y * 0.5f, rr = box.size.x * 0.5f - width * 0.8f;
        float a0 = M_PI * 0.75f, a1 = M_PI * 2.25f;
        nvgBeginPath(args.vg);
        nvgArc(args.vg, cx, cy, rr, a0, a1, NVG_CW);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 40));
        nvgStrokeWidth(args.vg, width);
        nvgStroke(args.vg);
        if (v > 0.001f) {
            nvgBeginPath(args.vg);
            nvgArc(args.vg, cx, cy, rr, a0, a0 + (a1 - a0) * v, NVG_CW);
            nvgStrokeColor(args.vg, nvgRGBA(255, 112, 15, 235));
            nvgStrokeWidth(args.vg, width);
            nvgStroke(args.vg);
        }
    }
};

struct EchoLabel : TransparentWidget {
    std::string text;
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 7.f);
        nvgTextLetterSpacing(args.vg, 0.6f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(0xbb, 0xbb, 0xbb));
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, text.c_str(), NULL);
    }
};

struct EchoTitle : TransparentWidget {
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
        nvgFontSize(args.vg, 11.f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgText(args.vg, 0.f, mm2px(3.5f), "SynTHBloK", NULL);
        nvgFontSize(args.vg, 10.f);
        nvgTextLetterSpacing(args.vg, 0.8f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0x7a, 0x00));
        nvgText(args.vg, 0.f, mm2px(6.4f), "MATRIX-ECHO", NULL);
    }
};

// Dim name at the left of a line, amber value at its right (fonts must be set).
void echoLine(const widget::Widget::DrawArgs& args, float x0, float x1, float yc, const char* name, const char* val,
              NVGcolor col = oled::amber(), float valSize = 7.5f) {
    nvgFontSize(args.vg, 6.f);
    nvgFillColor(args.vg, oled::dim());
    nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(args.vg, x0 + 4.f, yc, name, NULL);
    nvgFontSize(args.vg, valSize);
    nvgFillColor(args.vg, col);
    nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgText(args.vg, x1 - 4.f, yc, val, NULL);
}

// DELAY display: TIME | tempo source, then SUBDIV (click / scroll steps) | state. The TYPE cells sit below.
struct EchoDelayDisplay : TransparentWidget {
    EC* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        const float R = mm2px(oled::kRow);
        nvgBeginPath(args.vg);
        oled::divider(args, 4.f, R, W - 4.f, R);
        oled::divider(args, 4.f, 2.f * R, W - 4.f, 2.f * R);
        oled::divider(args, W * 0.5f, R + 2.f, W * 0.5f, 2.f * R - 2.f);
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W, H);
        float T = module ? module->timeNow : 0.375f;
        char tm[24], src[32] = "FREE", sub[16] = "1/8.";
        if (T < 1.f) snprintf(tm, sizeof tm, "%.0f ms", T * 1000.f);
        else snprintf(tm, sizeof tm, "%.2f s", T);
        NVGcolor srcCol = oled::dim();
        if (module) {
            if (module->source > 0) {
                snprintf(src, sizeof src, "%s %.1f BPM", module->source == 2 ? "CLK" : "TAP", 60.f / module->beatNow);
                srcCol = oled::amber();
            }
            snprintf(sub, sizeof sub, "%s", kEchoSub[clamp((int)module->params[EC::SUBDIV_PARAM].getValue(), 0, 10)].name);
        }
        float y0 = R * 0.5f, y1 = R * 1.5f;
        nvgFontSize(args.vg, 6.f);
        nvgFillColor(args.vg, oled::dim());
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, 4.f, y0, "TIME", NULL);
        nvgFontSize(args.vg, 8.f);
        nvgFillColor(args.vg, oled::amber());
        nvgText(args.vg, mm2px(9.f), y0, tm, NULL);
        nvgFontSize(args.vg, 6.5f);
        nvgFillColor(args.vg, srcCol);
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, W - 4.f, y0, src, NULL);
        echoLine(args, 0.f, W * 0.5f, y1, "SUBDIV", sub);
        const char* st = "ON";
        NVGcolor stc = oled::green();
        if (module && module->freezeNow) { st = "FREEZE"; stc = oled::amber(); }
        else if (module && !module->onNow) { st = "BYPASS"; stc = oled::dim(); }
        echoLine(args, W * 0.5f, W, y1, "DELAY", st, stc);
        nvgResetScissor(args.vg);
    }
    bool inSub(Vec p) { return p.y >= mm2px(oled::kRow) && p.y < mm2px(2.f * oled::kRow) && p.x < box.size.x * 0.5f; }
    void stepSub(int d) {
        if (!module) return;
        int s = (int)module->params[EC::SUBDIV_PARAM].getValue();
        module->params[EC::SUBDIV_PARAM].setValue((float)((s + d + 11) % 11));
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || !inSub(e.pos)) return;
        if (e.button != GLFW_MOUSE_BUTTON_LEFT && e.button != GLFW_MOUSE_BUTTON_RIGHT) return;
        stepSub(e.button == GLFW_MOUSE_BUTTON_LEFT ? 1 : -1);
        e.consume(this);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (e.scrollDelta.y == 0.f || !inSub(e.pos)) return;
        stepSub(e.scrollDelta.y > 0.f ? 1 : -1);
        e.consume(this);
    }
};

// LOOPER display: state | layers and length, then the loop's waveform with the playhead. Cells below.
static const float kLoopWaveH = 8.f;
struct EchoLoopDisplay : TransparentWidget {
    EC* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        const float R = mm2px(oled::kRow), WB = mm2px(oled::kRow + kLoopWaveH);
        nvgBeginPath(args.vg);
        oled::divider(args, 4.f, R, W - 4.f, R);
        oled::divider(args, 4.f, WB, W - 4.f, WB);
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W, H);
        EchoLooper* lp = module ? &module->looper : NULL;
        int st = lp ? (int)lp->st : 0;
        char info[32] = "60 s";
        if (lp && lp->st == EchoLooper::REC) snprintf(info, sizeof info, "%.2f s", lp->pos / lp->sr);
        else if (lp && lp->len > 0) snprintf(info, sizeof info, "x%d  %.2f s", lp->layers, lp->len / lp->sr);
        NVGcolor stc = st == EchoLooper::REC || st == EchoLooper::DUB ? oled::litFill()
                     : st == EchoLooper::PLAY || st == EchoLooper::ONCE ? oled::green() : oled::dim();
        float y0 = R * 0.5f;
        nvgFontSize(args.vg, 6.f);
        nvgFillColor(args.vg, oled::dim());
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, 4.f, y0, "LOOP", NULL);
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, stc);
        nvgText(args.vg, mm2px(9.f), y0, kLoopStateName[st], NULL);
        nvgFillColor(args.vg, oled::amber());
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, W - 4.f, y0, info, NULL);
        // waveform: one bar per peak bucket, mirrored round the centre line
        float x0 = 4.f, x1 = W - 4.f, yc = (R + WB) * 0.5f, hh = (WB - R) * 0.5f - 2.f;
        if (lp && lp->st == EchoLooper::REC) {
            float f = clamp((float)lp->pos / std::max(1, lp->cap), 0.f, 1.f);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, x0, yc - 1.f, (x1 - x0) * f, 2.f);
            nvgFillColor(args.vg, oled::litFill());
            nvgFill(args.vg);
        } else if (lp && lp->len > 0) {
            float bw = (x1 - x0) / EchoLooper::kPeaks;
            nvgBeginPath(args.vg);
            for (int k = 0; k < EchoLooper::kPeaks; k++) {
                float h = std::max(0.5f, std::min(1.f, lp->peak[k]) * hh);
                nvgRect(args.vg, x0 + k * bw, yc - h, std::max(0.6f, bw - 0.4f), 2.f * h);
            }
            nvgFillColor(args.vg, oled::amber(170));
            nvgFill(args.vg);
            float px = x0 + (x1 - x0) * (float)lp->pos / lp->len;
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, px, R + 1.5f);
            nvgLineTo(args.vg, px, WB - 1.5f);
            nvgStrokeColor(args.vg, lp->playing() ? oled::green() : oled::dim());
            nvgStrokeWidth(args.vg, 1.f);
            nvgStroke(args.vg);
        } else {
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, x0, yc);
            nvgLineTo(args.vg, x1, yc);
            nvgStrokeColor(args.vg, oled::outline(60));
            nvgStrokeWidth(args.vg, 0.6f);
            nvgStroke(args.vg);
        }
        nvgResetScissor(args.vg);
    }
};

// PRESET display: the loaded slot (or the STORE hint). Slot cells below.
struct EchoPresetDisplay : TransparentWidget {
    EC* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        nvgBeginPath(args.vg);
        oled::divider(args, 4.f, mm2px(oled::kRow), W - 4.f, mm2px(oled::kRow));
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W, H);
        char val[40] = "--";
        NVGcolor col = oled::amber();
        if (module && module->params[EC::STORE_PARAM].getValue() > 0.5f) { snprintf(val, sizeof val, "STORE: CLICK A SLOT"); col = oled::litFill(); }
        else if (module && module->curSlot >= 0) {
            const float* s = module->slots[module->curSlot];
            snprintf(val, sizeof val, "%d  %s  %s", module->curSlot + 1, kEchoTypeName[clamp((int)s[0], 0, ET_NUM - 1)],
                     kEchoSub[clamp((int)s[2], 0, 10)].name);
        }
        echoLine(args, 0.f, W, mm2px(oled::kRow * 0.5f), "PRESET", val, col);
        nvgResetScissor(args.vg);
    }
};

} // namespace

struct SynTHBloKEchoWidget : ModuleWidget {
    oled::Cell* undoCell = NULL;

    SynTHBloKEchoWidget(EC* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKEcho.svg")));
        using namespace rack::componentlibrary;
        const float PW = 162.56f;   // 32 HP
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PW - 3.5f, 3.5f))));
        EchoTitle* title = createWidget<EchoTitle>(mm2px(Vec(8.5f, 0.f)));
        title->box.size = mm2px(Vec(60.f, 7.f));
        addChild(title);

        auto label = [&](float cx, float cy, const char* text) {
            EchoLabel* l = createWidget<EchoLabel>(mm2px(Vec(cx - 12.f, cy - 2.f)));
            l->box.size = mm2px(Vec(24.f, 4.f)); l->text = text;
            addChild(l);
        };
        auto smallKnob = [&](float x, float y, int param, const char* name) {
            addChild(KnobTicks::create(Vec(x, y)));
            EchoRing* r = createWidget<EchoRing>(mm2px(Vec(x - 5.5f, y - 5.5f)));
            r->box.size = mm2px(Vec(11.f, 11.f)); r->module = module; r->param = param;
            addChild(r);
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y)), module, param));
            label(x, y + 7.f, name);
        };
        auto cell = [&](float x, float y, float w, float h, int param, const char* lab) -> oled::Cell* {
            oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(x, y)), module, param);
            c->box.size = mm2px(Vec(w, h)); c->module = module; c->label = lab;
            addParam(c);
            return c;
        };
        auto strip = [&](float x, float y, float w) {   // a bare display for one row of cells
            oled::Box* b = createWidget<oled::Box>(mm2px(Vec(x, y)));
            b->box.size = mm2px(Vec(w, oled::kCellH + 2.f));
            addChild(b);
        };
        auto jackRow = [&](float x0, float w, float y, const char* const* names, const int* ids, const bool* outs) {
            for (int i = 0; i < 4; i++) {
                if (!names[i]) continue;
                float x = x0 + w * (i + 0.5f) / 4.f;
                label(x, y - 5.5f, names[i]);
                if (outs[i]) {
                    addOutput(createOutputCentered<SmallJack>(mm2px(Vec(x, y)), module, ids[i]));
                    addChild(JackLed::create(Vec(x + 3.9f, y), module, ids[i], true));
                } else addInput(createInputCentered<SmallJack>(mm2px(Vec(x, y)), module, ids[i]));
            }
        };

        const float SW = 78.28f, LX = 2.f, RX = 82.28f;
        const float CG = 1.f, CH = oled::kCellH, RG = 0.9f;

        // ---------------- left: DELAY ----------------
        {
            const float OX = LX + 1.5f, OY = 10.5f, OW = SW - 3.f;
            const float cellY = OY + 2.f * oled::kRow + 1.f;
            EchoDelayDisplay* d = createWidget<EchoDelayDisplay>(mm2px(Vec(OX, OY)));
            d->box.size = mm2px(Vec(OW, 2.f * oled::kRow + 1.f + 3.f * CH + 2.f * RG + 1.5f)); d->module = module;
            addChild(d);
            float cw = (OW - 4.f - 3.f * CG) / 4.f;
            for (int k = 0; k < ET_NUM; k++) {
                int r = k / 4, c = k % 4;
                oled::Cell* tc = cell(OX + 2.f + c * (cw + CG), cellY + r * (CH + RG), cw, CH, EC::TYPE_PARAM, kEchoTypeName[k]);
                tc->setTo = k; tc->fontSize = 6.f;
            }
            auto col4 = [&](int i) { return LX + SW * (i + 0.5f) / 4.f; };
            const float KA = 48.5f, KB = 67.f;
            smallKnob(col4(0), KA, EC::TIME_PARAM, "TIME");
            smallKnob(col4(1), KA, EC::FEEDBACK_PARAM, "FEEDBACK");
            smallKnob(col4(2), KA, EC::LEVEL_PARAM, "LEVEL");
            smallKnob(col4(3), KA, EC::TONE_PARAM, "TONE");
            smallKnob(col4(0), KB, EC::SUBDIV_PARAM, "SUBDIV");
            smallKnob(col4(1), KB, EC::MOD_PARAM, "MOD");
            smallKnob(col4(2), KB, EC::SPACE_PARAM, "SPACE");
            oled::Cell* tap = cell(col4(3) - 7.f, KB - 5.5f, 14.f, 11.f, EC::TAP_PARAM, "TAP");
            tap->jack = EC::TAP_INPUT; tap->fontSize = 7.5f;
            if (module) tap->flash = &module->tapFlash;
            // FREEZE | KILL DRY | ON
            const float SY = 80.f;
            strip(OX, SY, OW);
            float w3 = (OW - 4.f - 2.f * CG) / 3.f;
            oled::Cell* fz = cell(OX + 2.f, SY + 1.f, w3, CH, EC::FREEZE_PARAM, "FREEZE");
            fz->latch = true; fz->jack = EC::FREEZE_INPUT;
            oled::Cell* kd = cell(OX + 2.f + (w3 + CG), SY + 1.f, w3, CH, EC::KILLDRY_PARAM, "KILL DRY");
            kd->latch = true;
            oled::Cell* on = cell(OX + 2.f + 2.f * (w3 + CG), SY + 1.f, w3, CH, EC::ON_PARAM, "ON");
            on->latch = true; on->green = true; on->jack = EC::BYPASS_INPUT;

            static const char* const n1[4] = {"IN L", "IN R", "CLOCK", "TAP"};
            static const int i1[4] = {EC::IN_L_INPUT, EC::IN_R_INPUT, EC::CLOCK_INPUT, EC::TAP_INPUT};
            static const bool o1[4] = {false, false, false, false};
            static const char* const n2[4] = {"FREEZE", "BYPASS", "WET L", "WET R"};
            static const int i2[4] = {EC::FREEZE_INPUT, EC::BYPASS_INPUT, EC::WET_L_OUTPUT, EC::WET_R_OUTPUT};
            static const bool o2[4] = {false, false, true, true};
            jackRow(LX, SW, 104.f, n1, i1, o1);
            jackRow(LX, SW, 117.f, n2, i2, o2);
        }

        // ---------------- right: LOOPER and PRESETS ----------------
        {
            const float OX = RX + 1.5f, OW = SW - 3.f;
            const float LY = 10.5f, lcY = LY + oled::kRow + kLoopWaveH + 1.f;
            EchoLoopDisplay* ld = createWidget<EchoLoopDisplay>(mm2px(Vec(OX, LY)));
            ld->box.size = mm2px(Vec(OW, oled::kRow + kLoopWaveH + 1.f + CH + 1.5f)); ld->module = module;
            addChild(ld);
            struct LC { const char* label; int param, jack; };
            const LC lc[5] = {{"REC", EC::REC_PARAM, EC::REC_INPUT}, {"PLAY", EC::PLAY_PARAM, EC::PLAY_INPUT},
                              {"ONCE", EC::ONCE_PARAM, EC::ONCE_INPUT}, {"UNDO", EC::UNDO_PARAM, EC::UNDO_INPUT},
                              {"CLEAR", EC::CLEAR_PARAM, EC::CLEAR_INPUT}};
            float w5 = (OW - 4.f - 4.f * CG) / 5.f;
            for (int i = 0; i < 5; i++) {
                oled::Cell* c = cell(OX + 2.f + i * (w5 + CG), lcY, w5, CH, lc[i].param, lc[i].label);
                c->jack = lc[i].jack;
                if (module) {
                    if (i == 0) c->flash = &module->recFlash;
                    if (i == 1) { c->flash = &module->playFlash; c->green = true; }
                    if (i == 2) { c->flash = &module->onceFlash; c->green = true; }
                }
                if (i == 3) undoCell = c;
            }

            const float PY = 33.5f;
            EchoPresetDisplay* pd = createWidget<EchoPresetDisplay>(mm2px(Vec(OX, PY)));
            pd->box.size = mm2px(Vec(OW, oled::kRow + 1.f + CH + 1.5f)); pd->module = module;
            addChild(pd);
            static const char* const num[6] = {"1", "2", "3", "4", "5", "6"};
            float storeW = 12.f, w6 = (OW - 4.f - storeW - 6.f * CG) / 6.f;
            for (int s = 0; s < 6; s++) {
                oled::Cell* c = cell(OX + 2.f + s * (w6 + CG), PY + oled::kRow + 1.f, w6, CH, EC::SLOT_PARAM + s, num[s]);
                if (module) c->flash = &module->slotLit[s];
            }
            oled::Cell* store = cell(OX + 2.f + 6.f * (w6 + CG), PY + oled::kRow + 1.f, storeW, CH, EC::STORE_PARAM, "STORE");
            store->latch = true;

            // LOOP level: the big knob
            const float KY = 61.f, RC = RX + SW * 0.5f;
            addChild(KnobTicks::create(Vec(RC, KY), 19.f / 11.f));
            EchoRing* mr = createWidget<EchoRing>(mm2px(Vec(RC - 9.5f, KY - 9.5f)));
            mr->box.size = mm2px(Vec(19.f, 19.f)); mr->module = module; mr->param = EC::LOOP_LEVEL_PARAM;
            addChild(mr);
            addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(RC, KY)), module, EC::LOOP_LEVEL_PARAM));
            label(RC, KY + 12.f, "LOOP LEVEL");

            // SNAP | PRE FX
            const float SY = 80.f;
            strip(OX, SY, OW);
            float w2 = (OW - 4.f - CG) / 2.f;
            cell(OX + 2.f, SY + 1.f, w2, CH, EC::QUANT_PARAM, "SNAP TO TEMPO")->latch = true;
            cell(OX + 2.f + w2 + CG, SY + 1.f, w2, CH, EC::PREFX_PARAM, "LOOP BEFORE DELAY")->latch = true;

            static const char* const n1[4] = {"REC", "PLAY", "ONCE", "UNDO"};
            static const int i1[4] = {EC::REC_INPUT, EC::PLAY_INPUT, EC::ONCE_INPUT, EC::UNDO_INPUT};
            static const bool o1[4] = {false, false, false, false};
            static const char* const n2[4] = {"CLEAR", NULL, "OUT L", "OUT R"};
            static const int i2[4] = {EC::CLEAR_INPUT, -1, EC::OUT_L_OUTPUT, EC::OUT_R_OUTPUT};
            static const bool o2[4] = {false, false, true, true};
            jackRow(RX, SW, 104.f, n1, i1, o1);
            jackRow(RX, SW, 117.f, n2, i2, o2);
        }
    }

    void step() override {
        EC* m = getModule<EC>();
        if (undoCell) undoCell->label = m && m->looper.undone ? "REDO" : "UNDO";
        ModuleWidget::step();
    }

    void appendContextMenu(Menu* menu) override {
        EC* m = getModule<EC>();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createIndexSubmenuItem("Clock pulse length", {kClockDivName[0], kClockDivName[1], kClockDivName[2]},
                                              [=]() { return m->clockDiv; }, [=](int i) { m->clockDiv = i; }));
        menu->addChild(createBoolPtrMenuItem("Trails on bypass", "", &m->trails));
        menu->addChild(createMenuLabel("The loop audio is not saved with the patch"));
    }
};

Model* modelSynTHBloKEcho = createModel<SynTHBloKEcho, SynTHBloKEchoWidget>("SynTHBloKEcho");
