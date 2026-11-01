// ============================================================================
// SynTHBloK GRAIN-FORGE  (glitch / stretch / robot voice / warp delay FX chain)
//  IN -> GLITCH -> STRETCH -> ROBOT -> WARP DELAY -> OUT, each stage with ON and its own mix.
//  WARP DELAY's feedback can loop back through STRETCH and ROBOT (ROUTE), so repeats get
//  stretched, pitch-warped and "sung" as they cycle. Clock-locked (internal 120 BPM if no clock).
// ============================================================================
#include "plugin.hpp"
#include "SynTHBloKGrainForge_dsp.hpp"
#include <functional>
#include <string>

namespace {

using namespace gforge;

static const char* kNote[12] = {"A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#"};
static const char* kPitchName[8] = {"-12", "-7", "-5", "0", "+5", "+7", "+12", "REV"};

struct SynTHBloKGrainForge : Module {
    enum ParamIds {
        GL_ON_PARAM, GL_CHANCE_PARAM, GL_ROLL_PARAM, GL_REV_PARAM, GL_STOP_PARAM, GL_CRUSH_PARAM, GL_MIX_PARAM,
        ST_ON_PARAM, ST_STRETCH_PARAM, ST_SIZE_PARAM, ST_PITCH_PARAM, ST_SPRAY_PARAM, ST_MIX_PARAM, ST_FREEZE_PARAM,
        RB_ON_PARAM, RB_ROOT_PARAM, RB_CHORD_PARAM, RB_VOWEL_PARAM, RB_RING_PARAM, RB_Q_PARAM, RB_MIX_PARAM,
        DL_ON_PARAM, DL_TIME_PARAM, DL_FDBK_PARAM, DL_WARP_PARAM, DL_ROUTE_PARAM, DL_TONE_PARAM, DL_MIX_PARAM,
        ENUMS(DL_STEP_PARAM, 8),
        IN_GAIN_PARAM, DRYWET_PARAM, OUT_GAIN_PARAM,
        ENUMS(DL_STEP2_PARAM, 8),     // throw steps 9-16 (added last so saved patches keep their ids)
        NUM_PARAMS
    };
    enum InputIds { IN_L_INPUT, IN_R_INPUT, CLOCK_INPUT, RESET_INPUT, FREEZE_INPUT, GLITCH_INPUT, NUM_INPUTS };
    enum OutputIds { OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };
    enum LightIds { NUM_LIGHTS };

    Glitch glitch;
    Stretch stretch;
    Robot robot;
    WarpDelay delay;
    dsp::SchmittTrigger clkTrig, rstTrig, glitchTrig;

    static const int kThrowSteps = 16;
    int step = -1;                    // 0..15, for the throw buttons
    float sinceClock = 1e9f, clockPeriod = 0.f, internalTimer = 0.f;
    float meterL = 0.f, meterR = 0.f;
    std::string glitchEvent = "-";    // shown on the GLITCH display
    float sm[NUM_PARAMS] = {};         // smoothed knob values
    bool smInit = false;

    // Clean up to 0.8, then a soft knee that never passes 1.0.
    static float softLimit(float x) {
        float a = std::fabs(x);
        if (a <= 0.8f) return x;
        return (x < 0.f ? -1.f : 1.f) * (0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f));
    }

    SynTHBloKGrainForge() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        const std::vector<std::string> offOn = {"Off", "On"};
        configSwitch(GL_ON_PARAM, 0.f, 1.f, 1.f, "GLITCH", offOn);
        configParam(GL_CHANCE_PARAM, 0.f, 1.f, 0.2f, "Glitch chance per step", "%", 0.f, 100.f);
        configSwitch(GL_ROLL_PARAM, 0.f, kRollCount - 1, 4.f, "Roll size",
                     std::vector<std::string>(kRollName, kRollName + kRollCount));
        configParam(GL_REV_PARAM, 0.f, 1.f, 0.2f, "Reverse amount", "%", 0.f, 100.f);
        configParam(GL_STOP_PARAM, 0.f, 1.f, 0.1f, "Tape-stop amount", "%", 0.f, 100.f);
        configParam(GL_CRUSH_PARAM, 0.f, 1.f, 0.f, "Crush (bits + sample rate)", "%", 0.f, 100.f);
        configParam(GL_MIX_PARAM, 0.f, 1.f, 1.f, "Glitch mix", "%", 0.f, 100.f);
        configSwitch(ST_ON_PARAM, 0.f, 1.f, 1.f, "STRETCH", offOn);
        configParam(ST_STRETCH_PARAM, 0.f, 1.f, 0.25f, "Stretch", "x", 16.f, 1.f);
        configParam(ST_SIZE_PARAM, 0.f, 1.f, 0.45f, "Grain size", " ms", 13.3f, 30.f);
        configSwitch(ST_PITCH_PARAM, 0.f, 7.f, 3.f, "Grain pitch", {"-12", "-7", "-5", "0", "+5", "+7", "+12", "Reverse"});
        configParam(ST_SPRAY_PARAM, 0.f, 1.f, 0.2f, "Spray", "%", 0.f, 100.f);
        configParam(ST_MIX_PARAM, 0.f, 1.f, 0.4f, "Stretch mix", "%", 0.f, 100.f);
        configSwitch(ST_FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze (the FREEZE gate also freezes)", {"Off", "Frozen"});
        configSwitch(RB_ON_PARAM, 0.f, 1.f, 1.f, "ROBOT", offOn);
        configSwitch(RB_ROOT_PARAM, 0.f, 24.f, 12.f, "Robot root",
                     {"A1", "A#1", "B1", "C2", "C#2", "D2", "D#2", "E2", "F2", "F#2", "G2", "G#2",
                      "A2", "A#2", "B2", "C3", "C#3", "D3", "D#3", "E3", "F3", "F#3", "G3", "G#3", "A3"});
        configSwitch(RB_CHORD_PARAM, 0.f, 7.f, 3.f, "Robot chord", {"Harmonics", "Fifth", "Major", "Minor", "Sus4", "Major 7", "Minor 7", "Cluster"});
        configParam(RB_VOWEL_PARAM, 0.f, 1.f, 0.5f, "Vowel (A - E - I - O - U)");
        configParam(RB_RING_PARAM, 0.f, 1.f, 0.05f, "Ring-mod", "%", 0.f, 100.f);
        configParam(RB_Q_PARAM, 0.f, 1.f, 0.55f, "Resonance (vowel sharpness)", "%", 0.f, 100.f);
        configParam(RB_MIX_PARAM, 0.f, 1.f, 0.25f, "Robot mix", "%", 0.f, 100.f);
        configSwitch(DL_ON_PARAM, 0.f, 1.f, 1.f, "WARP DELAY", offOn);
        configSwitch(DL_TIME_PARAM, 0.f, kDelayCount - 1, 8.f, "Delay time",
                     std::vector<std::string>(kDelayName, kDelayName + kDelayCount));
        configParam(DL_FDBK_PARAM, 0.f, 1.f, 0.4f, "Feedback", "%", 0.f, 100.f);
        configParam(DL_WARP_PARAM, -12.f, 12.f, 0.f, "Warp (pitch shift per repeat)", " st");
        configParam(DL_ROUTE_PARAM, 0.f, 1.f, 0.3f, "Route repeats through STRETCH + ROBOT", "%", 0.f, 100.f);
        configParam(DL_TONE_PARAM, 0.f, 1.f, 0.6f, "Repeat tone (dark -> bright)", "%", 0.f, 100.f);
        configParam(DL_MIX_PARAM, 0.f, 1.f, 0.6f, "Echo level", "%", 0.f, 100.f);
        for (int i = 0; i < kThrowSteps; i++)
            configSwitch(throwParam(i), 0.f, 1.f, 1.f, "Throw step " + std::to_string(i + 1) + " into the delay", {"Dry", "Throw"});
        configParam(IN_GAIN_PARAM, 0.f, 2.f, 1.f, "Input gain", "%", 0.f, 100.f);
        configParam(DRYWET_PARAM, 0.f, 1.f, 0.45f, "Dry / wet", "%", 0.f, 100.f);
        configParam(OUT_GAIN_PARAM, 0.f, 1.5f, 1.f, "Output level", "%", 0.f, 100.f);
        configInput(IN_L_INPUT, "Left");
        configInput(IN_R_INPUT, "Right (left is copied here when unpatched)");
        configInput(CLOCK_INPUT, "Clock (16th notes; 120 BPM internal when unpatched)");
        configInput(RESET_INPUT, "Reset");
        configInput(FREEZE_INPUT, "Freeze (gate)");
        configInput(GLITCH_INPUT, "Glitch now (trigger)");
        configOutput(OUT_L_OUTPUT, "Left");
        configOutput(OUT_R_OUTPUT, "Right");
    }

    bool on(int id) { return params[id].getValue() > 0.5f; }
    int slot(int id) { return (int)std::round(params[id].getValue()); }
    static int throwParam(int i) { return i < 8 ? DL_STEP_PARAM + i : DL_STEP2_PARAM + (i - 8); }

    void onReset() override { step = -1; }

    // Patches saved before the 13-slot TIME switch and 11-slot ROLL switch stored the old layouts:
    // TIME as an index into 7 slots, ROLL as a 0..1 knob split into 5 zones.
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "v2", json_true());
        return root;
    }
    void fromJson(json_t* rootJ) override {   // old patches have no "data" block, so dataFromJson never runs for them
        Module::fromJson(rootJ);
        json_t* d = json_object_get(rootJ, "data");
        if (d && json_object_get(d, "v2")) return;
        int old = clamp((int)std::round(params[DL_TIME_PARAM].getValue()), 0, 6);
        params[DL_TIME_PARAM].setValue((float)kDelayOldIndex[old]);
        // the old ROLL value was loaded into the new switch's range, so read it from the patch itself
        float roll = 0.4f;
        json_t* ps = json_object_get(rootJ, "params");
        for (size_t i = 0; ps && i < json_array_size(ps); i++) {
            json_t* p = json_array_get(ps, i);
            if (json_integer_value(json_object_get(p, "id")) == GL_ROLL_PARAM)
                roll = (float)json_number_value(json_object_get(p, "value"));
        }
        params[GL_ROLL_PARAM].setValue((float)kRollOldIndex[clamp((int)(roll * 5.f), 0, 4)]);
    }

    void process(const ProcessArgs& args) override {
        const float sr = args.sampleRate;

        // ---- clock: step edges drive GLITCH and the throw buttons ----
        bool edge = false;
        sinceClock += args.sampleTime;
        if (inputs[CLOCK_INPUT].isConnected()) {
            if (clkTrig.process(inputs[CLOCK_INPUT].getVoltage())) {
                if (sinceClock < 4.f) clockPeriod = sinceClock;
                sinceClock = 0.f;
                edge = true;
            }
        } else {
            internalTimer += args.sampleTime;
            if (internalTimer >= 0.125f) { internalTimer -= 0.125f; edge = true; }
        }
        float stepSec = (inputs[CLOCK_INPUT].isConnected() && clockPeriod > 0.f && sinceClock < 2.f) ? clockPeriod : 0.125f;
        if (rstTrig.process(inputs[RESET_INPUT].getVoltage())) { step = -1; internalTimer = 0.f; }
        if (edge) step = (step + 1) % kThrowSteps;
        bool force = glitchTrig.process(inputs[GLITCH_INPUT].getVoltage());

        // ---- smoothing: every knob and stage switch glides (~15 ms), so moves never click or zip ----
        float c = 1.f - std::exp(-1.f / (0.015f * sr));
        for (int i = 0; i < NUM_PARAMS; i++) {
            float t = params[i].getValue();
            sm[i] = smInit ? sm[i] + (t - sm[i]) * c : t;
        }
        smInit = true;

        // ---- input ----
        float g = sm[IN_GAIN_PARAM] * 0.2f;   // +-5 V -> +-1
        float inL = inputs[IN_L_INPUT].getVoltage() * g;
        float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() * g : inL;

        // Every stage keeps running so switching it on is seamless; ON crossfades it in.
        // ---- GLITCH ----
        if (edge || force) {
            glitch.trigger(stepSec * sr, params[GL_CHANCE_PARAM].getValue(), slot(GL_ROLL_PARAM),
                           params[GL_REV_PARAM].getValue(), params[GL_STOP_PARAM].getValue(), force);
            static const char* names[4] = {"-", "ROLL", "REVERSE", "TAPE STOP"};
            glitchEvent = glitch.ev == Glitch::ROLL ? std::string("ROLL ") + kRollName[glitch.rollDiv] : names[glitch.ev];
        }
        float pL, pR;
        glitch.process(inL, inR, sr, sm[GL_CRUSH_PARAM], pL, pR);
        float m = sm[GL_MIX_PARAM] * sm[GL_ON_PARAM];
        float aL = inL + (pL - inL) * m, aR = inR + (pR - inR) * m;

        // ---- WARP DELAY read (its feedback may loop through STRETCH and ROBOT) ----
        float fbL = 0.f, fbR = 0.f;
        float route = sm[DL_ROUTE_PARAM], dlOn = sm[DL_ON_PARAM];
        delay.read(sr, kDelaySteps[clamp(slot(DL_TIME_PARAM), 0, kDelayCount - 1)] * stepSec, sm[DL_FDBK_PARAM] * dlOn,
                   sm[DL_WARP_PARAM], sm[DL_TONE_PARAM], fbL, fbR);

        // ---- STRETCH ----
        float bInL = aL + route * fbL, bInR = aR + route * fbR;
        bool frz = on(ST_FREEZE_PARAM) || inputs[FREEZE_INPUT].getVoltage() > 1.f;
        stretch.process(bInL, bInR, sr, std::pow(16.f, sm[ST_STRETCH_PARAM]), sm[ST_SIZE_PARAM],
                        clamp(slot(ST_PITCH_PARAM), 0, 7), sm[ST_SPRAY_PARAM], frz, pL, pR);
        m = sm[ST_MIX_PARAM] * sm[ST_ON_PARAM];
        float bL = bInL + (pL - bInL) * m, bR = bInR + (pR - bInR) * m;

        // ---- ROBOT (root glides between notes) ----
        robot.process(bL, bR, sr, sm[RB_ROOT_PARAM], clamp(slot(RB_CHORD_PARAM), 0, 7),
                      sm[RB_VOWEL_PARAM], sm[RB_RING_PARAM], sm[RB_Q_PARAM], pL, pR);
        m = sm[RB_MIX_PARAM] * sm[RB_ON_PARAM];
        float cL = bL + (pL - bL) * m, cR = bR + (pR - bR) * m;

        // ---- WARP DELAY write: lit steps throw this signal in; un-routed feedback goes straight back ----
        bool throwOn = step < 0 || on(throwParam(step));
        delay.write(cL, cR, sr, throwOn);
        delay.writeRaw((1.f - route) * fbL, (1.f - route) * fbR);
        m = sm[DL_MIX_PARAM] * dlOn;
        float wetL = cL + delay.wetL * m, wetR = cR + delay.wetR * m;

        // ---- out: smooth limiter (clean up to 80%, then a soft knee to full scale) ----
        float dw = sm[DRYWET_PARAM], og = sm[OUT_GAIN_PARAM];
        float oL = softLimit((inL + (wetL - inL) * dw) * og);
        float oR = softLimit((inR + (wetR - inR) * dw) * og);
        outputs[OUT_L_OUTPUT].setVoltage(5.f * oL);
        outputs[OUT_R_OUTPUT].setVoltage(5.f * oR);
        float fall = std::exp(-args.sampleTime * 6.f);
        meterL = std::max(std::fabs(oL), meterL * fall);
        meterR = std::max(std::fabs(oR), meterR * fall);
    }
};

// ---------------------------------------------------------------- widgets ----------------

static std::shared_ptr<window::Font> uiFont() { return APP->window->uiFont; }

struct GFLabel : TransparentWidget {
    std::string text;
    float size = 7.f;
    void draw(const DrawArgs& args) override {
        auto font = uiFont(); if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, size);
        nvgTextLetterSpacing(args.vg, 0.6f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(0xbb, 0xbb, 0xbb));
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, text.c_str(), NULL);
    }
};

struct GFTitle : TransparentWidget {
    void draw(const DrawArgs& args) override {
        auto font = uiFont(); if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
        nvgFontSize(args.vg, 11.f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgText(args.vg, 0.f, mm2px(3.5f), "SynTHBloK", NULL);
        nvgFontSize(args.vg, 10.f);
        nvgTextLetterSpacing(args.vg, 0.8f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0x7a, 0x00));
        nvgText(args.vg, 0.f, mm2px(6.4f), "GRAIN-FORGE", NULL);
    }
};

// Amber value arc around a knob (0..1 of the knob's range).
struct GFRing : TransparentWidget {
    Module* module = NULL;
    int param = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float v = module ? module->paramQuantities[param]->getScaledValue() : 0.5f;
        float cx = box.size.x * 0.5f, cy = box.size.y * 0.5f, rr = box.size.x * 0.5f - 1.5f;
        float a0 = M_PI * 0.75f, a1 = M_PI * 2.25f;
        nvgBeginPath(args.vg);
        nvgArc(args.vg, cx, cy, rr, a0, a1, NVG_CW);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 40));
        nvgStrokeWidth(args.vg, 1.8f);
        nvgStroke(args.vg);
        if (v > 0.001f) {
            nvgBeginPath(args.vg);
            nvgArc(args.vg, cx, cy, rr, a0, a0 + (a1 - a0) * v, NVG_CW);
            nvgStrokeColor(args.vg, nvgRGBA(255, 112, 15, 235));
            nvgStrokeWidth(args.vg, 1.8f);
            nvgStroke(args.vg);
        }
    }
};

// Stage header (MIDI + style): stage name and live status; click toggles the stage on/off.
struct GFStageDisplay : TransparentWidget {
    SynTHBloKGrainForge* module = NULL;
    int onParam = 0;
    const char* title = "";
    std::function<std::string()> status;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        bool lit = !module || module->on(onParam);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.f);
        nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        nvgStrokeWidth(args.vg, lit ? 1.f : 0.8f);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, lit ? 160 : 60));
        nvgStroke(args.vg);
        auto font = uiFont(); if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, nvgRGBA(255, 190, 90, lit ? 220 : 90));
        nvgText(args.vg, 5.f, H * 0.3f, title, NULL);
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, lit ? nvgRGBA(120, 230, 90, 255) : nvgRGBA(255, 190, 90, 90));
        nvgText(args.vg, W - 5.f, H * 0.3f, lit ? "ON" : "OFF", NULL);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 9.5f);
        nvgFillColor(args.vg, nvgRGBA(255, 190, 90, lit ? 255 : 90));
        std::string s = status ? status() : "";
        nvgText(args.vg, 5.f, H * 0.7f, s.c_str(), NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[onParam].setValue(module->on(onParam) ? 0.f : 1.f);
            e.consume(this);
        }
    }
};

// Amber latch button (FREEZE) and throw-step buttons.
struct GFButton : app::ParamWidget {
    const char* label = "";
    int playIdx = -1;                       // throw buttons: outline when this step plays
    SynTHBloKGrainForge* gf = NULL;
    void draw(const DrawArgs& args) override {
        ParamQuantity* pq = getParamQuantity();
        bool lit = pq && pq->getValue() > 0.5f;
        bool now = gf && playIdx >= 0 && gf->step == playIdx;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.2f);
        nvgFillColor(args.vg, lit ? nvgRGBA(255, 112, 15, 235) : nvgRGBA(40, 25, 8, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, now ? nvgRGB(0xff, 0xff, 0xff) : nvgRGBA(255, 180, 90, lit ? 255 : 120));
        nvgStrokeWidth(args.vg, now ? 1.2f : 0.8f);
        nvgStroke(args.vg);
        auto font = uiFont(); if (!font || !label[0]) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 7.f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, lit ? nvgRGBA(20, 10, 2, 255) : nvgRGBA(255, 190, 90, 255));
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, label, NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (ParamQuantity* pq = getParamQuantity()) pq->setValue(pq->getValue() > 0.5f ? 0.f : 1.f);
            e.consume(this);
            return;
        }
        ParamWidget::onButton(e);
    }
};

// Tempo / step / chain status, and the stereo output meter.
struct GFStatus : TransparentWidget {
    SynTHBloKGrainForge* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.f);
        nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 80));
        nvgStroke(args.vg);
        auto font = uiFont(); if (!font) return;
        char bpm[32] = "120.0 BPM", sub[48] = "INTERNAL CLOCK";
        if (module && module->inputs[SynTHBloKGrainForge::CLOCK_INPUT].isConnected()) {
            if (module->clockPeriod > 0.f && module->sinceClock < 2.f) snprintf(bpm, sizeof bpm, "%.1f BPM", 60.f / (module->clockPeriod * 4.f));
            else snprintf(bpm, sizeof bpm, "--- BPM");
            snprintf(sub, sizeof sub, "STEP %d/%d", module->step + 1, SynTHBloKGrainForge::kThrowSteps);
        }
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 12.f);
        nvgFillColor(args.vg, nvgRGBA(255, 190, 90, 255));
        nvgText(args.vg, 6.f, H * 0.32f, bpm, NULL);
        nvgFontSize(args.vg, 8.f);
        nvgFillColor(args.vg, nvgRGBA(255, 190, 90, 130));
        nvgText(args.vg, 6.f, H * 0.72f, sub, NULL);
        // OUT L / OUT R: the same segmented dBFS bars as the MIX-24 levels display (full scale = 0 dB)
        float lv[2] = {module ? module->meterL : 0.6f, module ? module->meterR : 0.5f};
        static const char* nm[2] = {"L", "R"};
        float lx = W * 0.40f, bx = W * 0.45f, bw = W * 0.38f, vx = W - 4.f, bh = H * 0.24f;
        char buf[16];
        nvgFontSize(args.vg, 8.f);
        for (int i = 0; i < 2; i++) {
            float y = H * (i ? 0.56f : 0.2f);
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, nvgRGBA(255, 190, 90, 255));
            nvgText(args.vg, lx, y + bh * 0.5f, nm[i], NULL);
            bar(args, bx, y, bw, bh, lv[i], peak[i]);
            float db = toDb(lv[i]);
            if (db < -60.f) snprintf(buf, sizeof buf, "-inf"); else snprintf(buf, sizeof buf, "%.1f", db);
            nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, db > -1.f ? nvgRGBA(255, 50, 40, 255) : nvgRGBA(255, 190, 90, 255));
            nvgText(args.vg, vx, y + bh * 0.5f, buf, NULL);
        }
    }

    float peak[2] = {};
    static float toDb(float v) { return v > 1e-5f ? 20.f * std::log10(v) : -120.f; }
    // 24 segments, -48..+6 dB: light amber, amber above -9, red above -1; a peak-hold segment falls back slowly.
    void bar(const DrawArgs& a, float x, float y, float w, float h, float lvl, float& pk) {
        const int N = 24;
        const float DB_LO = -48.f, DB_HI = 6.f;
        float pos = clamp((toDb(lvl) - DB_LO) / (DB_HI - DB_LO), 0.f, 1.f);
        pk = std::max(lvl, pk * 0.9975f);
        float ppos = clamp((toDb(pk) - DB_LO) / (DB_HI - DB_LO), 0.f, 1.f);
        float sw = w / N;
        for (int i = 0; i < N; i++) {
            float segPos = (i + 1.f) / N;
            float segDb = DB_LO + (DB_HI - DB_LO) * segPos;
            bool lit = segPos <= pos + 1e-4f;
            bool pkSeg = (int)std::ceil(ppos * N) == i + 1;
            NVGcolor c;
            if (segDb > -1.f)      c = nvgRGBA(255, 40, 30, 255);
            else if (segDb > -9.f) c = nvgRGBA(255, 112, 15, 255);
            else                   c = nvgRGBA(255, 176, 60, 255);
            if (!lit && !(pkSeg && ppos > 0.02f)) c = nvgRGBA(46, 29, 10, 255);
            else if (!lit) c.a = 0.5f;
            nvgBeginPath(a.vg);
            nvgRect(a.vg, x + i * sw + 0.4f, y, sw - 0.8f, h);
            nvgFillColor(a.vg, c);
            nvgFill(a.vg);
        }
    }
    void onButton(const ButtonEvent& e) override {   // click resets the peak hold, like MIX-24
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) { peak[0] = peak[1] = 0.f; e.consume(this); }
    }
};

struct SynTHBloKGrainForgeWidget : ModuleWidget {
    SynTHBloKGrainForgeWidget(SynTHBloKGrainForge* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKGrainForge.svg")));
        using namespace rack::componentlibrary;
        const float PW = 162.56f;   // 32 HP
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PW - 3.5f, 3.5f))));
        GFTitle* title = createWidget<GFTitle>(mm2px(Vec(8.5f, 0.f)));
        title->box.size = mm2px(Vec(60.f, 7.f));
        addChild(title);

        auto label = [&](float cx, float cy, const std::string& text) {
            GFLabel* l = createWidget<GFLabel>(mm2px(Vec(cx - 10.f, cy - 2.f)));
            l->box.size = mm2px(Vec(20.f, 4.f)); l->text = text;
            addChild(l);
        };
        auto knob = [&](float x, float y, int param, const char* name) {
            GFRing* r = createWidget<GFRing>(mm2px(Vec(x - 5.5f, y - 5.5f)));
            r->box.size = mm2px(Vec(11.f, 11.f)); r->module = module; r->param = param;
            addChild(r);
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y)), module, param));
            label(x, y + 7.f, name);
        };
        SynTHBloKGrainForge* m = module;
        auto stage = [&](int i, int onParam, const char* name, std::function<std::string()> status) {
            float x0 = 2.f + i * 39.75f;
            GFStageDisplay* d = createWidget<GFStageDisplay>(mm2px(Vec(x0 + 1.5f, 10.5f)));
            d->box.size = mm2px(Vec(35.8f, 10.f)); d->module = module; d->onParam = onParam; d->title = name; d->status = status;
            addChild(d);
            return x0 + 19.4f;   // section centre
        };
        typedef SynTHBloKGrainForge G;

        // ---- four stages, left to right ----
        float c = stage(0, G::GL_ON_PARAM, "GLITCH", [m]() -> std::string { return m ? m->glitchEvent : "ROLL 1/4"; });
        knob(c - 9.f, 29.f, G::GL_CHANCE_PARAM, "CHANCE"); knob(c + 9.f, 29.f, G::GL_ROLL_PARAM, "ROLL");
        knob(c - 9.f, 44.f, G::GL_REV_PARAM, "REVERSE");   knob(c + 9.f, 44.f, G::GL_STOP_PARAM, "STOP");
        knob(c - 9.f, 59.f, G::GL_CRUSH_PARAM, "CRUSH");   knob(c + 9.f, 59.f, G::GL_MIX_PARAM, "MIX");

        c = stage(1, G::ST_ON_PARAM, "STRETCH", [m]() -> std::string {
            if (!m) return "x2.0";
            if (m->on(G::ST_FREEZE_PARAM) || m->inputs[G::FREEZE_INPUT].getVoltage() > 1.f) return "FROZEN";
            char b[24]; snprintf(b, sizeof b, "x%.1f  %s", std::pow(16.f, m->params[G::ST_STRETCH_PARAM].getValue()),
                                 kPitchName[clamp(m->slot(G::ST_PITCH_PARAM), 0, 7)]);
            return b;
        });
        knob(c - 9.f, 29.f, G::ST_STRETCH_PARAM, "STRETCH"); knob(c + 9.f, 29.f, G::ST_SIZE_PARAM, "SIZE");
        knob(c - 9.f, 44.f, G::ST_PITCH_PARAM, "PITCH");     knob(c + 9.f, 44.f, G::ST_SPRAY_PARAM, "SPRAY");
        knob(c - 9.f, 59.f, G::ST_MIX_PARAM, "MIX");
        GFButton* frz = createParam<GFButton>(mm2px(Vec(c + 3.f, 56.f)), module, G::ST_FREEZE_PARAM);
        frz->box.size = mm2px(Vec(12.f, 6.f)); frz->label = "FREEZE";
        addParam(frz);

        c = stage(2, G::RB_ON_PARAM, "ROBOT", [m]() -> std::string {
            int root = m ? m->slot(G::RB_ROOT_PARAM) : 12, ch = m ? clamp(m->slot(G::RB_CHORD_PARAM), 0, 7) : 3;
            return std::string(kNote[root % 12]) + std::to_string(1 + (root + 9) / 12) + " " + kChordName[ch];
        });
        knob(c - 9.f, 29.f, G::RB_ROOT_PARAM, "ROOT");   knob(c + 9.f, 29.f, G::RB_CHORD_PARAM, "CHORD");
        knob(c - 9.f, 44.f, G::RB_VOWEL_PARAM, "VOWEL"); knob(c + 9.f, 44.f, G::RB_RING_PARAM, "RING");
        knob(c - 9.f, 59.f, G::RB_Q_PARAM, "RES");       knob(c + 9.f, 59.f, G::RB_MIX_PARAM, "MIX");

        c = stage(3, G::DL_ON_PARAM, "WARP DELAY", [m]() -> std::string {
            int t = m ? clamp(m->slot(G::DL_TIME_PARAM), 0, kDelayCount - 1) : 8;
            float w = m ? m->params[G::DL_WARP_PARAM].getValue() : 0.f;
            char b[24]; snprintf(b, sizeof b, "%s  %+.0f st", kDelayName[t], w);
            return b;
        });
        knob(c - 13.f, 29.f, G::DL_TIME_PARAM, "TIME");  knob(c, 29.f, G::DL_FDBK_PARAM, "FDBK");   knob(c + 13.f, 29.f, G::DL_WARP_PARAM, "WARP");
        knob(c - 13.f, 44.f, G::DL_ROUTE_PARAM, "ROUTE"); knob(c, 44.f, G::DL_TONE_PARAM, "TONE");  knob(c + 13.f, 44.f, G::DL_MIX_PARAM, "ECHO");
        label(c, 56.f, "THROW STEPS");
        float bx0 = c - 17.4f;
        for (int i = 0; i < G::kThrowSteps; i++) {   // two rows: 1-8, 9-16
            GFButton* b = createParam<GFButton>(mm2px(Vec(bx0 + (i % 8) * 4.4f, 58.5f + (i / 8) * 6.5f)), module, G::throwParam(i));
            b->box.size = mm2px(Vec(4.f, 5.5f)); b->playIdx = i; b->gf = module;
            addParam(b);
        }

        // ---- bottom: status + meter, gains, jacks ----
        GFStatus* st = createWidget<GFStatus>(mm2px(Vec(5.f, 79.5f)));
        st->box.size = mm2px(Vec(56.f, 14.f)); st->module = module;
        addChild(st);
        knob(76.f, 86.5f, G::IN_GAIN_PARAM, "INPUT");
        GFRing* big = createWidgetCentered<GFRing>(mm2px(Vec(100.f, 86.5f)));
        big->box.size = mm2px(Vec(18.f, 18.f)); big->box.pos = mm2px(Vec(91.f, 77.5f)); big->module = module; big->param = G::DRYWET_PARAM;
        addChild(big);
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(100.f, 86.5f)), module, G::DRYWET_PARAM));
        label(100.f, 97.f, "DRY / WET");
        knob(124.f, 86.5f, G::OUT_GAIN_PARAM, "OUTPUT");

        const char* jn[8] = {"IN L", "IN R", "CLOCK", "RESET", "FREEZE", "GLITCH", "OUT L", "OUT R"};
        for (int i = 0; i < 8; i++) {
            float x = 12.f + i * 19.8f;
            label(x, 106.f, jn[i]);
            Vec p = mm2px(Vec(x, 113.f));
            if (i < 6) addInput(createInputCentered<BlackJack>(p, module, i));
            else addOutput(createOutputCentered<BlackJack>(p, module, i - 6));
        }
    }
};

} // namespace

Model* modelSynTHBloKGrainForge = createModel<SynTHBloKGrainForge, SynTHBloKGrainForgeWidget>("SynTHBloKGrainForge");
