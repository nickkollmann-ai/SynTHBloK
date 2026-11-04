#include "plugin.hpp"

// SynTHBloK Morph Filter (slug kept as NoizeeFlagshipFilter so old patches load).
// Stereo drive -> TPT state-variable filter per side -> LP/BP/HP morph.
struct NoizeeFlagshipFilter : Module {
    enum ParamIds {
        CUTOFF_PARAM,
        RES_PARAM,
        MORPH_PARAM,
        DRIVE_PARAM,
        FM_PARAM,
        KEY_PARAM,
        CHAOS_PARAM,
        WIDTH_PARAM,
        MIX_PARAM,
        NUM_PARAMS
    };

    enum InputIds {
        CUTOFF_IN,
        RES_IN,
        MORPH_IN,
        DRIVE_IN,
        FM_IN,
        KEY_IN,
        CHAOS_IN,
        WIDTH_IN,
        MODE_IN,
        PDECAY_IN,
        CRATE_IN,
        PTONE_IN,
        OFFSET_IN,
        RBOOST_IN,
        DTONE_IN,
        NWIDTH_IN,
        IN_L,
        IN_R,
        NUM_INPUTS
    };

    enum OutputIds {
        AUDIO_L_OUT,
        AUDIO_R_OUT,
        NUM_OUTPUTS
    };

    enum LightIds {
        CUTOFF_LED_RED, CUTOFF_LED_GREEN, CUTOFF_LED_BLUE,
        RES_LED_RED, RES_LED_GREEN, RES_LED_BLUE,
        MORPH_LED_RED, MORPH_LED_GREEN, MORPH_LED_BLUE,
        DRIVE_LED_RED, DRIVE_LED_GREEN, DRIVE_LED_BLUE,
        FM_LED_RED, FM_LED_GREEN, FM_LED_BLUE,
        KEY_LED_RED, KEY_LED_GREEN, KEY_LED_BLUE,
        CHAOS_LED_RED, CHAOS_LED_GREEN, CHAOS_LED_BLUE,
        WIDTH_LED_RED, WIDTH_LED_GREEN, WIDTH_LED_BLUE,
        MODE_LED_RED, MODE_LED_GREEN, MODE_LED_BLUE,
        PDECAY_LED_RED, PDECAY_LED_GREEN, PDECAY_LED_BLUE,
        CRATE_LED_RED, CRATE_LED_GREEN, CRATE_LED_BLUE,
        PTONE_LED_RED, PTONE_LED_GREEN, PTONE_LED_BLUE,
        OFFSET_LED_RED, OFFSET_LED_GREEN, OFFSET_LED_BLUE,
        RBOOST_LED_RED, RBOOST_LED_GREEN, RBOOST_LED_BLUE,
        DTONE_LED_RED, DTONE_LED_GREEN, DTONE_LED_BLUE,
        NWIDTH_LED_RED, NWIDTH_LED_GREEN, NWIDTH_LED_BLUE,
        IN_L_LED_RED, IN_L_LED_GREEN, IN_L_LED_BLUE,
        IN_R_LED_RED, IN_R_LED_GREEN, IN_R_LED_BLUE,
        OUT_L_LED_RED, OUT_L_LED_GREEN, OUT_L_LED_BLUE,
        OUT_R_LED_RED, OUT_R_LED_GREEN, OUT_R_LED_BLUE,
        NUM_LIGHTS
    };

    // Zavalishin TPT state-variable filter: stable at any cutoff, tuned to the cutoff in Hz
    struct Svf {
        float s1 = 0.f, s2 = 0.f;
        float lp = 0.f, bp = 0.f, hp = 0.f;

        void process(float x, float g, float k) {
            hp = (x - (k + g) * s1 - s2) / (1.f + g * (k + g));
            float v1 = g * hp;
            bp = v1 + s1;
            s1 = bp + v1;
            float v2 = g * bp;
            lp = v2 + s2;
            s2 = lp + v2;
            // a NaN or inf input would otherwise stick in the state for good
            if (!std::isfinite(s1) || !std::isfinite(s2))
                reset();
        }
        void reset() { s1 = s2 = lp = bp = hp = 0.f; }
    };

    Svf svf[2];
    float fmLP = 0.f;                       // DC tracker for the FM input
    float chaosPhase = 0.f, chaosTarget = 0.f, chaosVal = 0.f;
    dsp::ClockDivider lightDivider;

    NoizeeFlagshipFilter() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        lightDivider.setDivision(32);

        // cutoff knob is 20 Hz * 2^(10 x)
        configParam(CUTOFF_PARAM, 0.f, 1.f, 0.5f, "Cutoff", " Hz", 1024.f, 20.f);
        configParam(RES_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
        configParam(MORPH_PARAM, 0.f, 1.f, 0.5f, "Morph (LP - BP - HP)", "%", 0.f, 100.f);
        configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);

        configParam(FM_PARAM, 0.f, 1.f, 0.f, "FM amount", "%", 0.f, 100.f);
        configParam(KEY_PARAM, 0.f, 1.f, 0.f, "Keytrack (100% = 1V/oct)", "%", 0.f, 100.f);
        configParam(CHAOS_PARAM, 0.f, 1.f, 0.f, "Chaos (random cutoff drift)", "%", 0.f, 100.f);
        configParam(WIDTH_PARAM, 0.f, 1.f, 0.5f, "Stereo width (0% mono, 50% as input, 100% cutoff spread)", "%", 0.f, 100.f);

        configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Dry/wet mix", "%", 0.f, 100.f);

        configInput(CUTOFF_IN,  "Cutoff CV (1V/oct)");
        configInput(RES_IN,     "Resonance CV");
        configInput(MORPH_IN,   "Morph CV");
        configInput(DRIVE_IN,   "Drive CV");

        configInput(FM_IN,      "Audio-rate FM");
        configInput(KEY_IN,     "Keytrack (pitch CV)");
        configInput(CHAOS_IN,   "Chaos (external cutoff jitter source)");
        configInput(WIDTH_IN,   "Width CV");

        configInput(MODE_IN,    "Mode (morph offset) CV");
        configInput(PDECAY_IN,  "Damping CV");
        configInput(CRATE_IN,   "Chaos rate CV");
        configInput(PTONE_IN,   "LP/HP tilt CV");

        configInput(OFFSET_IN,  "Drive asymmetry (DC offset) CV");
        configInput(RBOOST_IN,  "Resonance boost CV");
        configInput(DTONE_IN,   "Drive gain CV");
        configInput(NWIDTH_IN,  "Width offset CV");

        configInput(IN_L, "Left in (normalled to right)");
        configInput(IN_R, "Right in");

        configOutput(AUDIO_L_OUT, "Left out");
        configOutput(AUDIO_R_OUT, "Right out");
    }

    // deep yellow -> yellow-orange -> orange -> orange-red -> red -> deep red -> deep dark red -> darker reds -> ember
    static void ledRamp(float w, float& r, float& g, float& b) {
        static const float C[10][3] = {
            {1.00f, 0.72f, 0.00f}, {1.00f, 0.55f, 0.00f}, {1.00f, 0.38f, 0.00f},
            {1.00f, 0.20f, 0.00f}, {1.00f, 0.04f, 0.00f}, {0.70f, 0.00f, 0.00f},
            {0.28f, 0.00f, 0.00f}, {0.16f, 0.00f, 0.00f}, {0.08f, 0.00f, 0.00f}, {0.03f, 0.00f, 0.00f}};
        float p = clamp(w, 0.f, 1.f) * 9.f;
        int i = (int)p; if (i > 8) i = 8;
        float f = p - (float)i;
        r = C[i][0] + (C[i + 1][0] - C[i][0]) * f;
        g = C[i][1] + (C[i + 1][1] - C[i][1]) * f;
        b = C[i][2] + (C[i + 1][2] - C[i][2]) * f;
    }

    inline void setWidthStyleLED(int baseId, float x, float dt) {
        float rr, gg, bb; ledRamp(clamp(x, 0.f, 1.f), rr, gg, bb);
        lights[baseId + 0].setSmoothBrightness(rr, dt);
        lights[baseId + 1].setSmoothBrightness(gg, dt);
        lights[baseId + 2].setSmoothBrightness(bb, dt);
    }

    inline void setBipolarLED(int baseId, float v, float dt) {
        float n = clamp(v / 5.f, -1.f, 1.f);
        if (n < 0.f) {
            lights[baseId + 2].setSmoothBrightness(-n, dt);
            lights[baseId + 0].setSmoothBrightness(0.f, dt);
            lights[baseId + 1].setSmoothBrightness(0.f, dt);
        } else {
            lights[baseId + 0].setSmoothBrightness(n, dt);
            lights[baseId + 1].setSmoothBrightness(n * 0.6f, dt);
            lights[baseId + 2].setSmoothBrightness(0.f, dt);
        }
    }

    void onReset() override {
        svf[0].reset();
        svf[1].reset();
        fmLP = chaosPhase = chaosTarget = chaosVal = 0.f;
    }

    float cv(int id, float scale, float lo, float hi) {
        return inputs[id].isConnected() ? clamp(inputs[id].getVoltage() * scale, lo, hi) : 0.f;
    }

    void process(const ProcessArgs& args) override {

        // a mono source on IN L feeds both sides
        float inL = inputs[IN_L].getVoltage();
        float inR = inputs[IN_R].isConnected() ? inputs[IN_R].getVoltage() : inL;

        // === WIDTH: 0 .. 0.5 narrows the input to mono, 0.5 .. 1 spreads the two cutoffs apart ===
        float width = params[WIDTH_PARAM].getValue() + cv(WIDTH_IN, 0.1f, -1.f, 1.f);
        width = clamp(width, 0.f, 1.f);
        float wEff = clamp(width + cv(NWIDTH_IN, 0.025f, -0.25f, 0.25f), 0.f, 1.f);
        float sideGain = std::min(wEff * 2.f, 1.f);
        float spread = std::max(wEff - 0.5f, 0.f);            // 0 .. 0.5 -> up to +-0.5 octave
        float mid = 0.5f * (inL + inR), side = 0.5f * (inL - inR);
        float x[2] = {(mid + side * sideGain) * 0.2f, (mid - side * sideGain) * 0.2f};   // +-5 V -> +-1

        // === DRIVE (asymmetry from OFFSET, without passing its DC on) ===
        float drive = clamp(params[DRIVE_PARAM].getValue() + cv(DRIVE_IN, 0.1f, -1.f, 1.f), 0.f, 1.f);
        float driveGain = (1.f + drive * 4.f) * (1.f + cv(DTONE_IN, 0.04f, -0.4f, 0.4f));
        float offset = cv(OFFSET_IN, 0.1f, -1.f, 1.f);
        float dcOff = std::tanh(offset * driveGain);

        // === CUTOFF, in octaves above 20 Hz ===
        float oct = params[CUTOFF_PARAM].getValue() * 10.f;
        oct += cv(CUTOFF_IN, 1.f, -10.f, 10.f);
        float keyAmt = params[KEY_PARAM].getValue();
        oct += cv(KEY_IN, 1.f, -10.f, 10.f) * keyAmt;

        float chaosAmt = params[CHAOS_PARAM].getValue();
        oct += cv(CHAOS_IN, 0.2f, -2.f, 2.f) * chaosAmt;

        // internal chaos: smoothed random steps, 2 Hz by default, C RATE 0.6 oct/V (0.25 .. 16 Hz)
        float rate = 2.f * std::pow(2.f, cv(CRATE_IN, 0.6f, -3.f, 3.f));
        chaosPhase += rate * args.sampleTime;
        if (chaosPhase >= 1.f) {
            chaosPhase -= 1.f;
            chaosTarget = random::uniform() * 2.f - 1.f;
        }
        chaosVal += (chaosTarget - chaosVal) * std::min(2.f * float(M_PI) * rate * args.sampleTime, 1.f);
        oct += chaosVal * chaosAmt;                              // up to +-1 octave

        // audio-rate FM, DC blocked at ~5 Hz, up to 0.6 oct/V
        float fmIn = inputs[FM_IN].getVoltage();
        fmLP += (fmIn - fmLP) * (2.f * float(M_PI) * 5.f * args.sampleTime);
        float fmAmt = params[FM_PARAM].getValue();
        oct += (fmIn - fmLP) * fmAmt * fmAmt * 0.6f;

        float fMax = std::min(20000.f, 0.45f * args.sampleRate);

        // === RESONANCE: k is the SVF damping (1 = Q 0.5 .. 0.05 = Q 20) ===
        float res = clamp(params[RES_PARAM].getValue() + cv(RES_IN, 0.1f, -1.f, 1.f), 0.f, 1.f);
        float resBoosted = clamp(res + cv(RBOOST_IN, 0.1f, 0.f, 0.5f), 0.f, 1.f);
        float k = clamp(1.f - resBoosted * 0.95f + cv(PDECAY_IN, 0.1f, -0.5f, 0.5f), 0.05f, 1.5f);

        // === MORPH + MODE BIAS ===
        float morph = params[MORPH_PARAM].getValue() + cv(MORPH_IN, 0.1f, -1.f, 1.f) + cv(MODE_IN, 0.12f, -1.f, 1.f);
        morph = clamp(morph, 0.f, 1.f);

        // tilt between the LP and HP outputs (P TONE)
        float tilt = std::tanh(cv(PTONE_IN, 0.1f, -0.5f, 0.5f) * 1.2f) * 0.5f;
        float tiltNorm = 1.f / (1.f + std::fabs(tilt) * 0.5f);

        float mix = params[MIX_PARAM].getValue();
        float dry[2] = {inL, inR};
        float outV[2];
        float cutoffHz = 0.f;
        for (int c = 0; c < 2; c++) {
            float o = oct + (c == 0 ? -spread : spread);
            float fc = clamp(20.f * std::pow(2.f, o), 10.f, fMax);
            if (c == 0) cutoffHz = 20.f * std::pow(2.f, oct);
            float g = std::tan(float(M_PI) * fc * args.sampleTime);

            float driven = std::tanh((x[c] + offset) * driveGain) - dcOff;
            Svf& f = svf[c];
            f.process(driven, g, k);

            float lp = f.lp * (1.f - tilt) * tiltNorm;
            float hp = f.hp * (1.f + tilt) * tiltNorm;
            float out = (morph < 0.5f)
                ? lp * (1.f - morph * 2.f) + f.bp * (morph * 2.f)
                : f.bp * (1.f - (morph - 0.5f) * 2.f) + hp * ((morph - 0.5f) * 2.f);

            float wet = 5.f * std::tanh(out);                    // back to +-5 V, soft-limited
            outV[c] = clamp(dry[c] * (1.f - mix) + wet * mix, -12.f, 12.f);
        }

        outputs[AUDIO_L_OUT].setVoltage(outV[0]);
        outputs[AUDIO_R_OUT].setVoltage(outV[1]);

        // 60 lights: update them every 32 samples, not every sample
        if (!lightDivider.process())
            return;
        float dt = args.sampleTime * lightDivider.getDivision();

        // log position of the cutoff (20 Hz .. 20 kHz)
        setWidthStyleLED(CUTOFF_LED_RED, std::log2(clamp(cutoffHz, 20.f, 20000.f) / 20.f) / 9.966f, dt);
        setWidthStyleLED(RES_LED_RED, res, dt);
        setWidthStyleLED(MORPH_LED_RED, morph, dt);
        setWidthStyleLED(DRIVE_LED_RED, drive, dt);

        setBipolarLED(FM_LED_RED, fmIn, dt);
        setWidthStyleLED(KEY_LED_RED, keyAmt, dt);
        setBipolarLED(CHAOS_LED_RED, inputs[CHAOS_IN].isConnected() ? inputs[CHAOS_IN].getVoltage() : chaosVal * chaosAmt * 5.f, dt);
        setWidthStyleLED(WIDTH_LED_RED, width, dt);

        setBipolarLED(MODE_LED_RED, inputs[MODE_IN].getVoltage(), dt);
        setBipolarLED(PDECAY_LED_RED, inputs[PDECAY_IN].getVoltage(), dt);
        setBipolarLED(CRATE_LED_RED, inputs[CRATE_IN].getVoltage(), dt);
        setBipolarLED(PTONE_LED_RED, inputs[PTONE_IN].getVoltage(), dt);

        setBipolarLED(OFFSET_LED_RED, inputs[OFFSET_IN].getVoltage(), dt);
        setBipolarLED(RBOOST_LED_RED, inputs[RBOOST_IN].getVoltage(), dt);
        setBipolarLED(DTONE_LED_RED, inputs[DTONE_IN].getVoltage(), dt);
        setBipolarLED(NWIDTH_LED_RED, inputs[NWIDTH_IN].getVoltage(), dt);

        setBipolarLED(IN_L_LED_RED, inL, dt);
        setBipolarLED(IN_R_LED_RED, inR, dt);
        setBipolarLED(OUT_L_LED_RED, outV[0], dt);
        setBipolarLED(OUT_R_LED_RED, outV[1], dt);
    }
};

struct NoizeeFlagshipFilterWidget : ModuleWidget {
    NoizeeFlagshipFilterWidget(NoizeeFlagshipFilter* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/NoizeeFlagshipFilter.svg")));

        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.25, 24)), module, NoizeeFlagshipFilter::CUTOFF_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(26.25, 24)), module, NoizeeFlagshipFilter::RES_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(41.25, 24)), module, NoizeeFlagshipFilter::MORPH_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(56.25, 24)), module, NoizeeFlagshipFilter::DRIVE_PARAM));

        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.25, 54)), module, NoizeeFlagshipFilter::FM_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(26.25, 54)), module, NoizeeFlagshipFilter::KEY_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(41.25, 54)), module, NoizeeFlagshipFilter::CHAOS_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(56.25, 54)), module, NoizeeFlagshipFilter::WIDTH_PARAM));

        addParam(createParamCentered<Trimpot>(mm2px(Vec(33.75, 120)), module, NoizeeFlagshipFilter::MIX_PARAM));

        addInput(createInputCentered<BlackJack>(mm2px(Vec(11.25, 38)), module, NoizeeFlagshipFilter::CUTOFF_IN));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(26.25, 38)), module, NoizeeFlagshipFilter::RES_IN));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(41.25, 38)), module, NoizeeFlagshipFilter::MORPH_IN));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(56.25, 38)), module, NoizeeFlagshipFilter::DRIVE_IN));

        addInput(createInputCentered<BlackJack>(mm2px(Vec(11.25, 68)), module, NoizeeFlagshipFilter::FM_IN));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(26.25, 68)), module, NoizeeFlagshipFilter::KEY_IN));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(41.25, 68)), module, NoizeeFlagshipFilter::CHAOS_IN));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(56.25, 68)), module, NoizeeFlagshipFilter::WIDTH_IN));

        addInput(createInputCentered<BlackJack>(mm2px(Vec(11.25, 84.59)), module, NoizeeFlagshipFilter::MODE_IN));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(26.25, 84.59)), module, NoizeeFlagshipFilter::PDECAY_IN));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(41.25, 84.59)), module, NoizeeFlagshipFilter::CRATE_IN));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(56.25, 84.59)), module, NoizeeFlagshipFilter::PTONE_IN));

        addInput(createInputCentered<BlackJack>(mm2px(Vec(11.25, 98.59)), module, NoizeeFlagshipFilter::OFFSET_IN));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(26.25, 98.59)), module, NoizeeFlagshipFilter::RBOOST_IN));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(41.25, 98.59)), module, NoizeeFlagshipFilter::DTONE_IN));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(56.25, 98.59)), module, NoizeeFlagshipFilter::NWIDTH_IN));

        // Audio I/O row
        addInput(createInputCentered<BlackJack>(mm2px(Vec(11.25, 112.95)), module, NoizeeFlagshipFilter::IN_L));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(26.25, 112.95)), module, NoizeeFlagshipFilter::IN_R));
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(41.25, 112.95)), module, NoizeeFlagshipFilter::AUDIO_L_OUT));
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(56.25, 112.95)), module, NoizeeFlagshipFilter::AUDIO_R_OUT));

        // === LEDS ===

        // Top LEDs (Small, WIDTH-style)
        addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(11.25, 31)), module, NoizeeFlagshipFilter::CUTOFF_LED_RED));
        addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(26.25, 31)), module, NoizeeFlagshipFilter::RES_LED_RED));
        addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(41.25, 31)), module, NoizeeFlagshipFilter::MORPH_LED_RED));
        addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(56.25, 31)), module, NoizeeFlagshipFilter::DRIVE_LED_RED));

        addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(11.25, 61)), module, NoizeeFlagshipFilter::FM_LED_RED));
        addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(26.25, 61)), module, NoizeeFlagshipFilter::KEY_LED_RED));
        addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(41.25, 61)), module, NoizeeFlagshipFilter::CHAOS_LED_RED));
        addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(56.25, 61)), module, NoizeeFlagshipFilter::WIDTH_LED_RED));

        // Middle CV LEDs (Tiny, 2 mm below jacks)
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(11.25, 90)), module, NoizeeFlagshipFilter::MODE_LED_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(26.25, 90)), module, NoizeeFlagshipFilter::PDECAY_LED_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(41.25, 90)), module, NoizeeFlagshipFilter::CRATE_LED_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(56.25, 90)), module, NoizeeFlagshipFilter::PTONE_LED_RED));

        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(11.25, 104)), module, NoizeeFlagshipFilter::OFFSET_LED_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(26.25, 104)), module, NoizeeFlagshipFilter::RBOOST_LED_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(41.25, 104)), module, NoizeeFlagshipFilter::DTONE_LED_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(56.25, 104)), module, NoizeeFlagshipFilter::NWIDTH_LED_RED));

        // Bottom audio LEDs (Tiny, under audio jacks)
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(11.25, 119)), module, NoizeeFlagshipFilter::IN_L_LED_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(26.25, 119)), module, NoizeeFlagshipFilter::IN_R_LED_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(41.25, 119)), module, NoizeeFlagshipFilter::OUT_L_LED_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(56.25, 119)), module, NoizeeFlagshipFilter::OUT_R_LED_RED));
    }
};

Model* modelNoizeeFlagshipFilter =
    createModel<NoizeeFlagshipFilter, NoizeeFlagshipFilterWidget>("NoizeeFlagshipFilter");
