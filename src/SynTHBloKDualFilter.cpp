// SynTHBloK DUAL FILTER (30 HP): two stereo state-variable filters, A and B.
// Each has CUTOFF, RES, DRIVE and MODE (a continuous sweep LP -> BP -> NOTCH -> HP). ROUTING runs them in
// SERIES (A into B), PARALLEL (both on the input, mixed) or STEREO (A on the left, B on the right).
// LINK makes B's cutoff follow A's, OFFSET octaves away. One LFO and an envelope follower move both cutoffs;
// SYNC locks the LFO to a clock on the SYNC jack (the RATE knob then picks the clock division).
#include "plugin.hpp"
#include "OledUi.hpp"

// Zavalishin TPT state-variable filter, run at 2x the sample rate (see DFFilter::process)
struct DFSvf {
    float s1 = 0.f, s2 = 0.f, lp = 0.f, bp = 0.f, hp = 0.f;
    void process(float x, float g, float k) {
        hp = (x - (k + g) * s1 - s2) / (1.f + g * (k + g));
        float v1 = g * hp;
        bp = v1 + s1;
        s1 = bp + v1;
        float v2 = g * bp;
        lp = v2 + s2;
        s2 = lp + v2;
        // a resonant peak driven hard is held soft, and a NaN or inf can't stick in the state
        s1 = 20.f * std::tanh(s1 * 0.05f);
        if (!std::isfinite(s1) || !std::isfinite(s2)) s1 = s2 = 0.f;
    }
};

// One filter channel: drive, then the SVF at 2x with the output morphed LP -> BP -> NOTCH -> HP
struct DFFilter {
    DFSvf svf;
    float last = 0.f;
    float out(float m) const {
        float lp = svf.lp, bp = svf.bp, hp = svf.hp, nt = lp + hp;
        if (m < 1.f) return lp + (bp - lp) * m;
        if (m < 2.f) return bp + (nt - bp) * (m - 1.f);
        return nt + (hp - nt) * (m - 2.f);
    }
    // x: input (V), g: tan(pi f / 2sr), k: damping, drive 0..1, m: mode 0..3
    float process(float x, float g, float k, float drive, float m) {
        // drive: tanh with 5 V in -> 5 V out, so turning it up thickens the sound more than it gets louder;
        // faded in over the first quarter of the knob so 0 is the clean signal
        if (drive > 0.f) {
            float pre = 1.f + drive * 9.f;
            float sat = 5.f * std::tanh(0.2f * pre * x) / std::tanh(pre);
            x += (sat - x) * std::min(1.f, drive * 4.f);
        }
        // 2x, so high cutoffs and high resonance stay stable: the midpoint first, then the sample itself
        svf.process(0.5f * (x + last), g, k);
        svf.process(x, g, k);
        last = x;
        return out(m);
    }
};

struct SynTHBloKDualFilter : Module {
    enum ParamId {
        A_CUT_PARAM, A_RES_PARAM, A_DRIVE_PARAM, A_MODE_PARAM,
        B_CUT_PARAM, B_RES_PARAM, B_DRIVE_PARAM, B_MODE_PARAM,
        ROUTE_PARAM, LINK_PARAM, OFFSET_PARAM,
        LFO_RATE_PARAM, LFO_AMT_PARAM, ENV_AMT_PARAM, SYNC_PARAM, MIX_PARAM,
        PARAMS_LEN
    };
    enum InputId { IN_L_INPUT, IN_R_INPUT, VOCT_INPUT, CUT_A_INPUT, CUT_B_INPUT, SYNC_INPUT, INPUTS_LEN };
    enum OutputId { OUT_A_OUTPUT, OUT_B_OUTPUT, OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN };
    enum RouteId { SERIES, PARALLEL, STEREO };

    DFFilter fa[2], fb[2];                 // [channel]
    float lfoPhase = 0.f, env = 0.f;
    dsp::SchmittTrigger syncTrig;
    float sinceClock = 0.f, clockPeriod = 0.5f;
    int clockCount = 0;
    float cutAHz = 1000.f, cutBHz = 1000.f;   // for the displays

    // SYNC on: the RATE knob picks how many clock pulses one LFO cycle takes
    static constexpr int kDivs = 7;
    static constexpr float kDiv[kDivs] = {32.f, 16.f, 8.f, 4.f, 2.f, 1.f, 0.5f};

    SynTHBloKDualFilter() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, 0);
        const char* nm[2] = {"A", "B"};
        for (int f = 0; f < 2; f++) {
            int o = f * 4;
            configParam(A_CUT_PARAM + o, 0.f, 1.f, f ? 0.6f : 0.5f, std::string("Filter ") + nm[f] + " cutoff", " Hz", 1024.f, 20.f);
            configParam(A_RES_PARAM + o, 0.f, 1.f, 0.2f, std::string("Filter ") + nm[f] + " resonance", "%", 0.f, 100.f);
            configParam(A_DRIVE_PARAM + o, 0.f, 1.f, 0.f, std::string("Filter ") + nm[f] + " drive", "%", 0.f, 100.f);
            configParam(A_MODE_PARAM + o, 0.f, 3.f, 0.f, std::string("Filter ") + nm[f] + " mode (0 LP, 1 BP, 2 notch, 3 HP)");
        }
        configSwitch(ROUTE_PARAM, 0.f, 2.f, 0.f, "Routing", {"Series (A into B)", "Parallel", "Stereo (A left, B right)"});
        configSwitch(LINK_PARAM, 0.f, 1.f, 0.f, "Link B's cutoff to A", {"Off", "On"});
        configParam(OFFSET_PARAM, -4.f, 4.f, 1.f, "Link offset (B above A)", " oct");
        configParam(LFO_RATE_PARAM, 0.f, 1.f, 0.4f, "LFO rate", " Hz", 512.f, 0.04f);
        configParam(LFO_AMT_PARAM, 0.f, 1.f, 0.f, "LFO to cutoffs", " oct", 0.f, 4.f);
        configParam(ENV_AMT_PARAM, -1.f, 1.f, 0.f, "Envelope follower to cutoffs", " oct", 0.f, 5.f);
        configSwitch(SYNC_PARAM, 0.f, 1.f, 0.f, "Sync the LFO to the SYNC jack", {"Off", "On"});
        configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Dry/wet", "%", 0.f, 100.f);
        configInput(IN_L_INPUT, "Left (or mono)");
        configInput(IN_R_INPUT, "Right (normalled to left)");
        configInput(VOCT_INPUT, "Cutoff V/oct (both filters)");
        configInput(CUT_A_INPUT, "Filter A cutoff CV (1 V/oct)");
        configInput(CUT_B_INPUT, "Filter B cutoff CV (1 V/oct)");
        configInput(SYNC_INPUT, "LFO sync clock");
        configOutput(OUT_A_OUTPUT, "Filter A (mono)");
        configOutput(OUT_B_OUTPUT, "Filter B (mono)");
        configOutput(OUT_L_OUTPUT, "Left");
        configOutput(OUT_R_OUTPUT, "Right");
        configBypass(IN_L_INPUT, OUT_L_OUTPUT);
        configBypass(IN_R_INPUT, OUT_R_OUTPUT);
    }

    void onReset() override {
        for (int c = 0; c < 2; c++) fa[c] = DFFilter(), fb[c] = DFFilter();
        lfoPhase = env = 0.f;
    }

    void process(const ProcessArgs& args) override {
        float inL = inputs[IN_L_INPUT].getVoltage();
        float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : inL;

        // envelope follower: 5 ms attack, 150 ms release, 0..1 for 0..5 V
        float lvl = std::min(1.f, 0.5f * (std::fabs(inL) + std::fabs(inR)) * 0.2f);
        float ec = lvl > env ? 1.f - std::exp(-args.sampleTime / 0.005f) : 1.f - std::exp(-args.sampleTime / 0.15f);
        env += (lvl - env) * ec;

        // LFO: free (0.04..20 Hz) or locked to SYNC
        float rateK = params[LFO_RATE_PARAM].getValue(), lfoHz;
        bool synced = params[SYNC_PARAM].getValue() > 0.5f && inputs[SYNC_INPUT].isConnected();
        sinceClock += args.sampleTime;
        bool tick = syncTrig.process(inputs[SYNC_INPUT].getVoltage(), 0.1f, 1.f);
        if (tick) { if (sinceClock > 0.002f && sinceClock < 8.f) clockPeriod = sinceClock; sinceClock = 0.f; clockCount++; }
        if (synced) {
            float div = kDiv[clamp((int)(rateK * kDivs), 0, kDivs - 1)];
            lfoHz = 1.f / (clockPeriod * div);
            if (tick && div >= 1.f && clockCount % (int)div == 0) lfoPhase = 0.f;   // the cycle starts on the clock
        } else {
            lfoHz = 0.04f * std::pow(512.f, rateK);
        }
        lfoPhase += lfoHz * args.sampleTime;
        lfoPhase -= std::floor(lfoPhase);
        float lfo = std::sin(2.f * M_PI * lfoPhase);

        // cutoffs, in octaves above 20 Hz
        float common = inputs[VOCT_INPUT].getVoltage() + lfo * params[LFO_AMT_PARAM].getValue() * 4.f
                     + env * params[ENV_AMT_PARAM].getValue() * 5.f;
        float octA = params[A_CUT_PARAM].getValue() * 10.f + common + inputs[CUT_A_INPUT].getVoltage();
        float octB = (params[LINK_PARAM].getValue() > 0.5f ? params[A_CUT_PARAM].getValue() * 10.f + params[OFFSET_PARAM].getValue()
                                                         : params[B_CUT_PARAM].getValue() * 10.f)
                   + common + inputs[CUT_B_INPUT].getVoltage();
        float sr2 = args.sampleRate * 2.f;
        auto coef = [&](float oct, float& hz) {
            hz = clamp(20.f * std::pow(2.f, oct), 10.f, sr2 * 0.24f);
            return std::tan(float(M_PI) * hz / sr2);
        };
        float gA = coef(octA, cutAHz), gB = coef(octB, cutBHz);
        // RES 0 is Butterworth (k = sqrt 2, -3 dB at the cutoff); full RES is a Q of about 70
        auto damp = [&](int p) { return float(M_SQRT2) * (1.f - 0.99f * params[p].getValue()); };
        float kA = damp(A_RES_PARAM), kB = damp(B_RES_PARAM);
        float dA = params[A_DRIVE_PARAM].getValue(), dB = params[B_DRIVE_PARAM].getValue();
        float mA = params[A_MODE_PARAM].getValue(), mB = params[B_MODE_PARAM].getValue();

        int route = (int)std::round(params[ROUTE_PARAM].getValue());
        float in[2] = {inL, inR}, a[2], b[2], wet[2];
        for (int c = 0; c < 2; c++) {
            if (route == STEREO) {
                // A on the left, B on the right; the other filter of each pair stays quiet
                a[c] = c == 0 ? fa[0].process(in[0], gA, kA, dA, mA) : 0.f;
                b[c] = c == 1 ? fb[1].process(in[1], gB, kB, dB, mB) : 0.f;
                wet[c] = c == 0 ? a[0] : b[1];
            } else {
                a[c] = fa[c].process(in[c], gA, kA, dA, mA);
                b[c] = fb[c].process(route == SERIES ? a[c] : in[c], gB, kB, dB, mB);
                wet[c] = route == SERIES ? b[c] : 0.5f * (a[c] + b[c]);
            }
        }
        float mix = params[MIX_PARAM].getValue();
        outputs[OUT_L_OUTPUT].setVoltage(clamp(in[0] + (wet[0] - in[0]) * mix, -12.f, 12.f));
        outputs[OUT_R_OUTPUT].setVoltage(clamp(in[1] + (wet[1] - in[1]) * mix, -12.f, 12.f));
        outputs[OUT_A_OUTPUT].setVoltage(clamp(route == STEREO ? a[0] : 0.5f * (a[0] + a[1]), -12.f, 12.f));
        outputs[OUT_B_OUTPUT].setVoltage(clamp(route == STEREO ? b[1] : 0.5f * (b[0] + b[1]), -12.f, 12.f));
    }
};
constexpr float SynTHBloKDualFilter::kDiv[];

// ---------------- widgets ----------------
using DF = SynTHBloKDualFilter;

// LP | BP | NT | HP over a filter's MODE knob: the nearest type is lit (a sweep between two lights both,
// dimmer); a click sets the knob to that type. Under the cells: the cutoff in Hz.
struct DFModeDisplay : TransparentWidget {
    DF* module = NULL;
    int filter = 0;
    float mode() { return module ? module->params[DF::A_MODE_PARAM + filter * 4].getValue() : (filter ? 0.f : 2.f); }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        static const char* names[4] = {"LP", "BP", "NT", "HP"};
        float pad = mm2px(1.4f), cw = (W - 2 * pad - 3 * mm2px(0.8f)) / 4.f, ch = mm2px(oled::kCellH), y = mm2px(1.2f);
        float m = mode();
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        for (int i = 0; i < 4; i++) {
            float x = pad + i * (cw + mm2px(0.8f)), w = 1.f - std::min(1.f, std::fabs(m - i));
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, x, y, cw, ch, 1.2f);
            nvgFillColor(args.vg, w > 0.f ? nvgLerpRGBA(oled::offFill(), oled::litFill(), w) : oled::offFill());
            nvgFill(args.vg);
            nvgStrokeColor(args.vg, oled::outline(w > 0.5f ? 255 : 110));
            nvgStrokeWidth(args.vg, 0.7f);
            nvgStroke(args.vg);
            if (!font) continue;
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, 6.5f);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, w > 0.5f ? oled::ink() : oled::amber());
            nvgText(args.vg, x + cw / 2, y + ch / 2, names[i], NULL);
        }
        if (!font) return;
        float hz = module ? (filter ? module->cutBHz : module->cutAHz) : 1000.f;
        char buf[32];
        if (hz >= 1000.f) snprintf(buf, sizeof buf, "%.2f kHz", hz / 1000.f); else snprintf(buf, sizeof buf, "%.0f Hz", hz);
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, oled::amber());
        nvgText(args.vg, W / 2, y + ch + (H - y - ch) / 2, buf, NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && module) {
            float pad = mm2px(1.4f), cw = (box.size.x - 2 * pad - 3 * mm2px(0.8f)) / 4.f;
            int i = clamp((int)((e.pos.x - pad) / (cw + mm2px(0.8f))), 0, 3);
            module->params[DF::A_MODE_PARAM + filter * 4].setValue((float)i);
            e.consume(this);
        }
    }
};

struct SynTHBloKDualFilterWidget : ModuleWidget {
    SynTHBloKDualFilterWidget(DF* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/SynTHBloKDualFilter.svg")));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // the two filter sections (mm, from the panel layout)
        for (int f = 0; f < 2; f++) {
            float x0 = f ? 78.2f : 2.f;
            int o = f * 4;
            addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(x0 + 18.f, 32.f)), module, DF::A_CUT_PARAM + o));
            addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x0 + 44.f, 26.f)), module, DF::A_RES_PARAM + o));
            addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x0 + 61.f, 26.f)), module, DF::A_DRIVE_PARAM + o));
            addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x0 + 52.5f, 47.f)), module, DF::A_MODE_PARAM + o));
            DFModeDisplay* md = createWidget<DFModeDisplay>(mm2px(Vec(x0 + 4.f, 51.5f)));
            md->box.size = mm2px(Vec(30.f, 12.5f));
            md->module = module; md->filter = f;
            addChild(md);
        }

        // ROUTING: three selector cells on one display
        oled::Box* rb = createWidget<oled::Box>(mm2px(Vec(5.f, 75.f)));
        rb->box.size = mm2px(Vec(40.f, 19.f));
        addChild(rb);
        const char* routes[3] = {"SERIES", "PARALLEL", "STEREO"};
        for (int i = 0; i < 3; i++) {
            oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(7.f, 76.6f + i * 5.3f)), module, DF::ROUTE_PARAM);
            c->box.size = mm2px(Vec(36.f, oled::kCellH));
            c->module = module; c->label = routes[i]; c->setTo = i;
            addParam(c);
        }
        // LINK and SYNC: a latch cell each on a small display
        struct LatchDef { float x; int param; const char* label; int jack; };
        for (LatchDef d : {LatchDef{50.5f, DF::LINK_PARAM, "LINK", -1}, LatchDef{121.f, DF::SYNC_PARAM, "SYNC", DF::SYNC_INPUT}}) {
            oled::Box* b = createWidget<oled::Box>(mm2px(Vec(d.x, 75.f)));
            b->box.size = mm2px(Vec(15.f, 6.5f));
            addChild(b);
            oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(d.x + 1.2f, 76.05f)), module, d.param);
            c->box.size = mm2px(Vec(12.6f, oled::kCellH));
            c->module = module; c->label = d.label; c->latch = true; c->jack = d.jack;
            addParam(c);
        }
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(58.f, 88.f)), module, DF::OFFSET_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(78.f, 82.f)), module, DF::LFO_RATE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(94.f, 82.f)), module, DF::LFO_AMT_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(110.f, 82.f)), module, DF::ENV_AMT_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(141.f, 82.f)), module, DF::MIX_PARAM));

        // jacks
        const int ins[6] = {DF::IN_L_INPUT, DF::IN_R_INPUT, DF::VOCT_INPUT, DF::CUT_A_INPUT, DF::CUT_B_INPUT, DF::SYNC_INPUT};
        for (int i = 0; i < 6; i++) addInput(createInputCentered<BlackJack>(mm2px(Vec(10.f + i * 13.6f, 110.5f)), module, ins[i]));
        const int outs[4] = {DF::OUT_A_OUTPUT, DF::OUT_B_OUTPUT, DF::OUT_L_OUTPUT, DF::OUT_R_OUTPUT};
        for (int i = 0; i < 4; i++) addOutput(createOutputCentered<BlackJack>(mm2px(Vec(97.f + i * 14.4f, 110.5f)), module, outs[i]));
    }
};

Model* modelSynTHBloKDualFilter = createModel<SynTHBloKDualFilter, SynTHBloKDualFilterWidget>("SynTHBloKDualFilter");
