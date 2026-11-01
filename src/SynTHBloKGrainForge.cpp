// ============================================================================
// SynTHBloK GRAIN-FORGE  (shuffle / glitch / robot voice / pulse / warp delay FX chain)
//  IN -> SHUFFLE -> GLITCH -> ROBOT -> PULSE -> WARP DELAY -> OUT, each stage with ON and its own mix.
//  SHUFFLE rearranges the clean beat first, GLITCH breaks it up, ROBOT voices it, PULSE gates and
//  filters it in time, and WARP DELAY echoes it. The delay's feedback can loop back through ROBOT and
//  PULSE (ROUTE), so repeats get "sung" and filtered as they cycle. Clock-locked (120 BPM internal).
// ============================================================================
#include "plugin.hpp"
#include "SynTHBloKGrainForge_dsp.hpp"
#include <functional>
#include <string>

namespace {

using namespace gforge;

static const char* kNote[12] = {"A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#"};

struct SynTHBloKGrainForge : Module {
    enum ParamIds {
        GL_ON_PARAM, GL_CHANCE_PARAM, GL_ROLL_PARAM, GL_REV_PARAM, GL_STOP_PARAM, GL_CRUSH_PARAM, GL_MIX_PARAM,
        // PULSE took over the old STRETCH slots (saved patches reset them, see fromJson)
        PU_ON_PARAM, PU_CUTOFF_PARAM, PU_RES_PARAM, PU_PATTERN_PARAM, PU_DEPTH_PARAM, PU_MIX_PARAM, PU_MODE_PARAM,
        RB_ON_PARAM, RB_ROOT_PARAM, RB_CHORD_PARAM, RB_VOWEL_PARAM, RB_RING_PARAM, RB_Q_PARAM, RB_MIX_PARAM,
        DL_ON_PARAM, DL_TIME_PARAM, DL_FDBK_PARAM, DL_WARP_PARAM, DL_ROUTE_PARAM, DL_TONE_PARAM, DL_MIX_PARAM,
        ENUMS(DL_STEP_PARAM, 8),
        IN_GAIN_PARAM, DRYWET_PARAM, OUT_GAIN_PARAM,
        ENUMS(DL_STEP2_PARAM, 8),     // throw steps 9-16 (added last so saved patches keep their ids)
        PU_DECAY_PARAM,
        SH_ON_PARAM, SH_PATTERN_PARAM, SH_AMOUNT_PARAM, SH_REV_PARAM, SH_PITCH_PARAM, SH_LEN_PARAM, SH_MIX_PARAM,
        NUM_PARAMS
    };
    enum InputIds { IN_L_INPUT, IN_R_INPUT, CLOCK_INPUT, RESET_INPUT, OPEN_INPUT, GLITCH_INPUT, NUM_INPUTS };
    enum OutputIds { OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };
    enum LightIds { NUM_LIGHTS };

    Shuffle shuffle;
    Glitch glitch;
    Robot robot;
    Pulse pulse;
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
        configSwitch(GL_ROLL_PARAM, 0.f, kRollCount - 1, 3.f, "Roll size (rolls start on the beat and last a beat)",
                     std::vector<std::string>(kRollName, kRollName + kRollCount));
        configParam(GL_REV_PARAM, 0.f, 1.f, 0.2f, "Reverse amount", "%", 0.f, 100.f);
        configParam(GL_STOP_PARAM, 0.f, 1.f, 0.1f, "Tape-stop amount", "%", 0.f, 100.f);
        configParam(GL_CRUSH_PARAM, 0.f, 1.f, 0.f, "Crush (bits + sample rate)", "%", 0.f, 100.f);
        configParam(GL_MIX_PARAM, 0.f, 1.f, 1.f, "Glitch mix", "%", 0.f, 100.f);
        configSwitch(SH_ON_PARAM, 0.f, 1.f, 1.f, "SHUFFLE", offOn);
        configSwitch(SH_PATTERN_PARAM, 0.f, kShufCount - 1, 0.f, "Shuffle pattern",
                     std::vector<std::string>(kShufName, kShufName + kShufCount));
        configParam(SH_AMOUNT_PARAM, 0.f, 1.f, 0.35f, "Shuffle amount (chance a step is moved)", "%", 0.f, 100.f);
        configParam(SH_REV_PARAM, 0.f, 1.f, 0.15f, "Reversed slices", "%", 0.f, 100.f);
        configParam(SH_PITCH_PARAM, 0.f, 1.f, 0.f, "Octave slices (up or down)", "%", 0.f, 100.f);
        configSwitch(SH_LEN_PARAM, 0.f, 2.f, 2.f, "Shuffle length", std::vector<std::string>(kShufLenName, kShufLenName + 3));
        configParam(SH_MIX_PARAM, 0.f, 1.f, 1.f, "Shuffle mix", "%", 0.f, 100.f);
        configSwitch(PU_ON_PARAM, 0.f, 1.f, 1.f, "PULSE", offOn);
        configParam(PU_CUTOFF_PARAM, 0.f, 1.f, 0.55f, "Cutoff", " Hz", 600.f, 30.f);
        configParam(PU_RES_PARAM, 0.f, 1.f, 0.35f, "Resonance", "%", 0.f, 100.f);
        configSwitch(PU_PATTERN_PARAM, 0.f, kPulseCount - 1, 0.f, "Pulse pattern",
                     std::vector<std::string>(kPulseName, kPulseName + kPulseCount));
        configParam(PU_DEPTH_PARAM, 0.f, 1.f, 0.5f, "Depth (gate + filter sweep per hit)", "%", 0.f, 100.f);
        configParam(PU_DECAY_PARAM, 0.f, 1.f, 0.35f, "Decay (click .. 4 steps)", "%", 0.f, 100.f);
        configParam(PU_MIX_PARAM, 0.f, 1.f, 0.6f, "Pulse mix", "%", 0.f, 100.f);
        configSwitch(PU_MODE_PARAM, 0.f, 2.f, 0.f, "Filter mode", {"Low-pass", "Band-pass", "High-pass"});
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
        configParam(DL_ROUTE_PARAM, 0.f, 1.f, 0.3f, "Route repeats through ROBOT + PULSE", "%", 0.f, 100.f);
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
        configInput(OPEN_INPUT, "Open (gate): PULSE lets everything through while high");
        configInput(GLITCH_INPUT, "Glitch now (trigger)");
        configOutput(OUT_L_OUTPUT, "Left");
        configOutput(OUT_R_OUTPUT, "Right");
    }

    bool on(int id) { return params[id].getValue() > 0.5f; }
    int slot(int id) { return (int)std::round(params[id].getValue()); }
    static int throwParam(int i) { return i < 8 ? DL_STEP_PARAM + i : DL_STEP2_PARAM + (i - 8); }

    void onReset() override { step = -1; }

    // Older patches stored older layouts.
    //   v1 (no data block): TIME as an index into 7 slots, ROLL as a 0..1 knob split into 5 zones.
    //   v2: ROLL as an index into 11 slots.
    //   v3: the current 13 TIME slots and 7 ROLL slots, with STRETCH where PULSE is now.
    //   v4: SHUFFLE and PULSE.
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "v4", json_true());
        return root;
    }
    void fromJson(json_t* rootJ) override {   // old patches have no "data" block, so dataFromJson never runs for them
        Module::fromJson(rootJ);
        json_t* d = json_object_get(rootJ, "data");
        int ver = !d ? 1 : json_object_get(d, "v4") ? 4 : json_object_get(d, "v3") ? 3 : json_object_get(d, "v2") ? 2 : 1;
        if (ver >= 4) return;
        // STRETCH's settings were saved in PULSE's slots: start PULSE from its defaults instead
        for (int id = PU_ON_PARAM; id <= PU_MODE_PARAM; id++) params[id].setValue(paramQuantities[id]->getDefaultValue());
        if (ver == 3) return;
        // the saved ROLL value was clamped into the new switch's range, so read it from the patch itself
        float roll = 0.4f;
        json_t* ps = json_object_get(rootJ, "params");
        for (size_t i = 0; ps && i < json_array_size(ps); i++) {
            json_t* p = json_array_get(ps, i);
            if (json_integer_value(json_object_get(p, "id")) == GL_ROLL_PARAM)
                roll = (float)json_number_value(json_object_get(p, "value"));
        }
        if (ver == 2) {
            params[GL_ROLL_PARAM].setValue((float)kRollV2Index[clamp((int)std::round(roll), 0, 10)]);
            return;
        }
        int old = clamp((int)std::round(params[DL_TIME_PARAM].getValue()), 0, 6);
        params[DL_TIME_PARAM].setValue((float)kDelayOldIndex[old]);
        params[GL_ROLL_PARAM].setValue((float)kRollV1Index[clamp((int)(roll * 5.f), 0, 4)]);
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
        float stepLen = stepSec * sr;
        float pL, pR;
        // ---- SHUFFLE (first, so it rearranges the clean beat) ----
        shuffle.process(inL, inR, sr, stepLen, edge, step, slot(SH_PATTERN_PARAM), params[SH_AMOUNT_PARAM].getValue(),
                        params[SH_REV_PARAM].getValue(), params[SH_PITCH_PARAM].getValue(), slot(SH_LEN_PARAM), pL, pR);
        float m = sm[SH_MIX_PARAM] * sm[SH_ON_PARAM];
        float sL = inL + (pL - inL) * m, sR = inR + (pR - inR) * m;

        // ---- GLITCH ----
        if (edge || force) {
            glitch.trigger(stepSec * sr, params[GL_CHANCE_PARAM].getValue(), slot(GL_ROLL_PARAM),
                           params[GL_REV_PARAM].getValue(), params[GL_STOP_PARAM].getValue(), step % 4 == 0 || force, force);
            static const char* names[4] = {"-", "ROLL", "REVERSE", "TAPE STOP"};
            glitchEvent = glitch.ev == Glitch::ROLL ? std::string("ROLL ") + kRollName[glitch.rollDiv] : names[glitch.ev];
        }
        glitch.process(sL, sR, sr, sm[GL_CRUSH_PARAM], pL, pR);
        m = sm[GL_MIX_PARAM] * sm[GL_ON_PARAM];
        float aL = sL + (pL - sL) * m, aR = sR + (pR - sR) * m;

        // ---- WARP DELAY read (its feedback may loop through ROBOT and PULSE) ----
        float fbL = 0.f, fbR = 0.f;
        float route = sm[DL_ROUTE_PARAM], dlOn = sm[DL_ON_PARAM];
        delay.read(sr, kDelaySteps[clamp(slot(DL_TIME_PARAM), 0, kDelayCount - 1)] * stepSec, sm[DL_FDBK_PARAM] * dlOn,
                   sm[DL_WARP_PARAM], sm[DL_TONE_PARAM], fbL, fbR);

        // ---- ROBOT (root glides between notes) ----
        float bL = aL + route * fbL, bR = aR + route * fbR;
        robot.process(bL, bR, sr, sm[RB_ROOT_PARAM], clamp(slot(RB_CHORD_PARAM), 0, 7),
                      sm[RB_VOWEL_PARAM], sm[RB_RING_PARAM], sm[RB_Q_PARAM], pL, pR);
        m = sm[RB_MIX_PARAM] * sm[RB_ON_PARAM];
        float rL = bL + (pL - bL) * m, rR = bR + (pR - bR) * m;

        // ---- PULSE (gate + filter in time; OPEN lets everything through) ----
        pulse.process(rL, rR, sr, stepLen, edge, step, slot(PU_PATTERN_PARAM), sm[PU_CUTOFF_PARAM], sm[PU_RES_PARAM],
                      sm[PU_DEPTH_PARAM], sm[PU_DECAY_PARAM], clamp(slot(PU_MODE_PARAM), 0, 2),
                      inputs[OPEN_INPUT].getVoltage() > 1.f, pL, pR);
        m = sm[PU_MIX_PARAM] * sm[PU_ON_PARAM];
        float cL = rL + (pL - rL) * m, cR = rR + (pR - rR) * m;

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

// Amber throw-step buttons.
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

// PULSE filter mode: LP | BP | HP, the active one lit. Click steps to the next.
struct GFModeButton : app::ParamWidget {
    void draw(const DrawArgs& args) override {
        ParamQuantity* pq = getParamQuantity();
        int sel = pq ? clamp((int)std::round(pq->getValue()), 0, 2) : 0;
        float w = box.size.x / 3.f, h = box.size.y;
        auto font = uiFont();
        for (int i = 0; i < 3; i++) {
            bool lit = i == sel;
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, i * w + 0.5f, 0.f, w - 1.f, h, 1.2f);
            nvgFillColor(args.vg, lit ? nvgRGBA(255, 112, 15, 235) : nvgRGBA(40, 25, 8, 255));
            nvgFill(args.vg);
            nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, lit ? 255 : 120));
            nvgStrokeWidth(args.vg, 0.8f);
            nvgStroke(args.vg);
            if (!font) continue;
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, 7.f);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, lit ? nvgRGBA(20, 10, 2, 255) : nvgRGBA(255, 190, 90, 255));
            nvgText(args.vg, i * w + w * 0.5f, h * 0.5f, kModeName[i], NULL);
        }
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (ParamQuantity* pq = getParamQuantity()) {
                int sel = clamp((int)(e.pos.x / (box.size.x / 3.f)), 0, 2);   // click a segment to pick it
                pq->setValue((float)sel);
            }
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
        const float PW = 203.2f;   // 40 HP
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

        // ---- five stages, left to right in signal order ----
        float c = stage(0, G::SH_ON_PARAM, "SHUFFLE", [m]() -> std::string {
            int p = m ? clamp(m->slot(G::SH_PATTERN_PARAM), 0, kShufCount - 1) : 0;
            int s = m ? m->shuffle.src : -1;
            char b[24];
            if (s < 0) snprintf(b, sizeof b, "%s  LIVE", kShufName[p]); else snprintf(b, sizeof b, "%s  >%02d", kShufName[p], s + 1);
            return b;
        });
        knob(c - 9.f, 29.f, G::SH_PATTERN_PARAM, "PATTERN"); knob(c + 9.f, 29.f, G::SH_AMOUNT_PARAM, "AMOUNT");
        knob(c - 9.f, 44.f, G::SH_REV_PARAM, "REVERSE");     knob(c + 9.f, 44.f, G::SH_PITCH_PARAM, "OCTAVE");
        knob(c - 9.f, 59.f, G::SH_LEN_PARAM, "LENGTH");      knob(c + 9.f, 59.f, G::SH_MIX_PARAM, "MIX");

        c = stage(1, G::GL_ON_PARAM, "GLITCH", [m]() -> std::string { return m ? m->glitchEvent : "ROLL 1/16"; });
        knob(c - 9.f, 29.f, G::GL_CHANCE_PARAM, "CHANCE"); knob(c + 9.f, 29.f, G::GL_ROLL_PARAM, "ROLL");
        knob(c - 9.f, 44.f, G::GL_REV_PARAM, "REVERSE");   knob(c + 9.f, 44.f, G::GL_STOP_PARAM, "STOP");
        knob(c - 9.f, 59.f, G::GL_CRUSH_PARAM, "CRUSH");   knob(c + 9.f, 59.f, G::GL_MIX_PARAM, "MIX");

        c = stage(2, G::RB_ON_PARAM, "ROBOT", [m]() -> std::string {
            int root = m ? m->slot(G::RB_ROOT_PARAM) : 12, ch = m ? clamp(m->slot(G::RB_CHORD_PARAM), 0, 7) : 3;
            return std::string(kNote[root % 12]) + std::to_string(1 + (root + 9) / 12) + " " + kChordName[ch];
        });
        knob(c - 9.f, 29.f, G::RB_ROOT_PARAM, "ROOT");   knob(c + 9.f, 29.f, G::RB_CHORD_PARAM, "CHORD");
        knob(c - 9.f, 44.f, G::RB_VOWEL_PARAM, "VOWEL"); knob(c + 9.f, 44.f, G::RB_RING_PARAM, "RING");
        knob(c - 9.f, 59.f, G::RB_Q_PARAM, "RES");       knob(c + 9.f, 59.f, G::RB_MIX_PARAM, "MIX");

        c = stage(3, G::PU_ON_PARAM, "PULSE", [m]() -> std::string {
            if (m && m->inputs[G::OPEN_INPUT].getVoltage() > 1.f) return "OPEN";
            int p = m ? clamp(m->slot(G::PU_PATTERN_PARAM), 0, kPulseCount - 1) : 3;
            int md = m ? clamp(m->slot(G::PU_MODE_PARAM), 0, 2) : 0;
            return std::string(kPulseName[p]) + "  " + kModeName[md];
        });
        knob(c - 13.f, 29.f, G::PU_CUTOFF_PARAM, "CUTOFF");   knob(c, 29.f, G::PU_RES_PARAM, "RES");     knob(c + 13.f, 29.f, G::PU_DEPTH_PARAM, "DEPTH");
        knob(c - 13.f, 44.f, G::PU_PATTERN_PARAM, "PATTERN"); knob(c, 44.f, G::PU_DECAY_PARAM, "DECAY"); knob(c + 13.f, 44.f, G::PU_MIX_PARAM, "MIX");
        GFModeButton* mode = createParam<GFModeButton>(mm2px(Vec(c - 8.f, 58.f)), module, G::PU_MODE_PARAM);
        mode->box.size = mm2px(Vec(16.f, 6.5f));
        addParam(mode);

        c = stage(4, G::DL_ON_PARAM, "WARP DELAY", [m]() -> std::string {
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
        st->box.size = mm2px(Vec(72.f, 14.f)); st->module = module;
        addChild(st);
        knob(105.f, 86.5f, G::IN_GAIN_PARAM, "INPUT");
        GFRing* big = createWidgetCentered<GFRing>(mm2px(Vec(140.f, 86.5f)));
        big->box.size = mm2px(Vec(18.f, 18.f)); big->box.pos = mm2px(Vec(131.f, 77.5f)); big->module = module; big->param = G::DRYWET_PARAM;
        addChild(big);
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(140.f, 86.5f)), module, G::DRYWET_PARAM));
        label(140.f, 97.f, "DRY / WET");
        knob(175.f, 86.5f, G::OUT_GAIN_PARAM, "OUTPUT");

        const char* jn[8] = {"IN L", "IN R", "CLOCK", "RESET", "OPEN", "GLITCH", "OUT L", "OUT R"};
        for (int i = 0; i < 8; i++) {
            float x = 14.f + i * 25.f;
            label(x, 106.f, jn[i]);
            Vec p = mm2px(Vec(x, 113.f));
            if (i < 6) addInput(createInputCentered<BlackJack>(p, module, i));
            else addOutput(createOutputCentered<BlackJack>(p, module, i - 6));
        }
    }
};

} // namespace

Model* modelSynTHBloKGrainForge = createModel<SynTHBloKGrainForge, SynTHBloKGrainForgeWidget>("SynTHBloKGrainForge");
