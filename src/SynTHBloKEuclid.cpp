// ============================================================================
// SynTHBloK-E  EUCLID MODULE
//  Four independent Euclidean rhythm channels from one clock.
//  Each channel: STEPS (1..32), HITS (0..STEPS), ROTATE (0..STEPS-1).
//  Different STEPS per channel gives polyrhythms. Outputs are 10 V trigger pulses
//  (10 ms) that drive Kick, Snare/Hat, Quad gates, Player run, etc.
//  RESET restarts all channels on the next clock. RUN (if patched) must be high.
// ============================================================================
#include "plugin.hpp"
#include "SynTHBloKEuclid_layout.hpp"
#include <cmath>

struct SynTHBloKEuclid : Module {
    enum ParamIds { PARAMS_START, NUM_PARAMS = 12 };   // ch*3 + {steps, hits, rotate}
    enum InputIds { CLOCK_INPUT, RESET_INPUT, RUN_INPUT, NUM_INPUTS };
    enum OutputIds { CH1_OUTPUT, CH2_OUTPUT, CH3_OUTPUT, CH4_OUTPUT, NUM_OUTPUTS };
    enum LightIds { ENUMS(KNOB_LIGHT, 12 * 3), ENUMS(JACK_LIGHT, 7 * 3), NUM_LIGHTS };

    dsp::SchmittTrigger clkTrig, rstTrig;
    dsp::PulseGenerator pulse[4];
    int step[4] = {0, 0, 0, 0};
    bool pendingReset = false;
    float flash[4] = {0.f, 0.f, 0.f, 0.f};

    SynTHBloKEuclid() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        for (int c = 0; c < 4; c++) {
            std::string ch = "Ch " + std::to_string(c + 1) + " ";
            configParam(c * 3 + 0, 1.f, 32.f, 16.f, ch + "steps");
            configParam(c * 3 + 1, 0.f, 32.f, c == 0 ? 4.f : (c == 1 ? 5.f : (c == 2 ? 7.f : 3.f)), ch + "hits");
            configParam(c * 3 + 2, 0.f, 31.f, 0.f, ch + "rotate");
            for (int k = 0; k < 3; k++) paramQuantities[c * 3 + k]->snapEnabled = true;
            configOutput(CH1_OUTPUT + c, ch + "trigger");
        }
        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configInput(RUN_INPUT, "Run (gate; unpatched = always running)");
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
    inline void setParamLED(int baseId, float x) {
        float rr, gg, bb; ledRamp(x, rr, gg, bb);
        lights[baseId + 0].setSmoothBrightness(rr, 1);
        lights[baseId + 1].setSmoothBrightness(gg, 1);
        lights[baseId + 2].setSmoothBrightness(bb, 1);
    }
    inline void setSignalLED(int baseId, float v) {
        float n = clamp(v / 5.f, -1.f, 1.f);
        if (n < 0.f) {
            lights[baseId + 2].setSmoothBrightness(-n, 1);
            lights[baseId + 0].setSmoothBrightness(0.f, 1);
            lights[baseId + 1].setSmoothBrightness(0.f, 1);
        } else {
            lights[baseId + 0].setSmoothBrightness(n, 1);
            lights[baseId + 1].setSmoothBrightness(n * 0.6f, 1);
            lights[baseId + 2].setSmoothBrightness(0.f, 1);
        }
    }

    void onReset() override {
        for (int c = 0; c < 4; c++) { step[c] = 0; pulse[c].reset(); }
        pendingReset = false;
    }

    // Euclidean test: is there a hit on step i? (evenly spread, then rotated)
    static bool hitAt(int i, int steps, int hits, int rot) {
        if (hits <= 0) return false;
        if (hits >= steps) return true;
        int k = (i + rot) % steps;
        return ((k * hits) % steps) < hits;
    }

    void process(const ProcessArgs& args) override {
        bool running = !inputs[RUN_INPUT].isConnected() || inputs[RUN_INPUT].getVoltage() >= 1.f;
        if (rstTrig.process(inputs[RESET_INPUT].getVoltage())) pendingReset = true;

        if (clkTrig.process(inputs[CLOCK_INPUT].getVoltage()) && running) {
            if (pendingReset) { for (int c = 0; c < 4; c++) step[c] = 0; pendingReset = false; }
            for (int c = 0; c < 4; c++) {
                int steps = clamp((int)std::round(params[c * 3 + 0].getValue()), 1, 32);
                int hits = clamp((int)std::round(params[c * 3 + 1].getValue()), 0, steps);
                int rot = clamp((int)std::round(params[c * 3 + 2].getValue()), 0, steps - 1);
                if (step[c] >= steps) step[c] = 0;
                if (hitAt(step[c], steps, hits, rot)) { pulse[c].trigger(0.010f); flash[c] = 1.f; }
                step[c] = (step[c] + 1) % steps;
            }
        }

        for (int c = 0; c < 4; c++) {
            float v = pulse[c].process(args.sampleTime) ? 10.f : 0.f;
            outputs[CH1_OUTPUT + c].setVoltage(v);
            flash[c] = std::max(0.f, flash[c] - args.sampleTime / 0.12f);
        }

        // LEDs: knobs by position, jacks by signal (outputs hold a short flash so hits stay visible)
        static const float maxv[3] = {32.f, 32.f, 31.f};
        for (int i = 0; i < 12; i++) {
            float lo = paramQuantities[i]->getMinValue(), hi = paramQuantities[i]->getMaxValue();
            setParamLED(KNOB_LIGHT + i * 3, (params[i].getValue() - lo) / std::max(1e-6f, hi - lo));
        }
        (void)maxv;
        setSignalLED(JACK_LIGHT + 0 * 3, inputs[CLOCK_INPUT].getVoltage());
        setSignalLED(JACK_LIGHT + 1 * 3, inputs[RESET_INPUT].getVoltage());
        setSignalLED(JACK_LIGHT + 2 * 3, inputs[RUN_INPUT].isConnected() ? inputs[RUN_INPUT].getVoltage() : 0.f);
        for (int c = 0; c < 4; c++) setSignalLED(JACK_LIGHT + (3 + c) * 3, flash[c] * 5.f);
    }
};

template <typename TBase>
struct MicroLightEU : TinyLight<TBase> {
    MicroLightEU() { this->box.size = mm2px(Vec(0.9f, 0.9f)); }
};

struct SynTHBloKEuclidWidget : ModuleWidget {
    SynTHBloKEuclidWidget(SynTHBloKEuclid* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKEuclid.svg")));
        using namespace rack::componentlibrary;

        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(EUL::PANEL_W - 3.5f, 3.5f))));

        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 3; c++) {
                addChild(createLightCentered<MicroLightEU<RedGreenBlueLight>>(mm2px(Vec(EUL::KX[c], EUL::KY[r] - 4.8f)), module, SynTHBloKEuclid::KNOB_LIGHT + (r * 3 + c) * 3));
                addParam(createParamCentered<Trimpot>(mm2px(Vec(EUL::KX[c], EUL::KY[r])), module, r * 3 + c));
            }
        for (int j = 0; j < 3; j++) {
            addChild(createLightCentered<MicroLightEU<RedGreenBlueLight>>(mm2px(Vec(EUL::KX[j], EUL::IN_Y + 5.2f)), module, SynTHBloKEuclid::JACK_LIGHT + j * 3));
            addInput(createInputCentered<BlackJack>(mm2px(Vec(EUL::KX[j], EUL::IN_Y)), module, SynTHBloKEuclid::CLOCK_INPUT + j));
        }
        for (int j = 0; j < 4; j++) {
            addChild(createLightCentered<MicroLightEU<RedGreenBlueLight>>(mm2px(Vec(EUL::OX[j], EUL::OUT_Y + 5.2f)), module, SynTHBloKEuclid::JACK_LIGHT + (3 + j) * 3));
            addOutput(createOutputCentered<BlackJack>(mm2px(Vec(EUL::OX[j], EUL::OUT_Y)), module, SynTHBloKEuclid::CH1_OUTPUT + j));
        }
    }
};

Model* modelSynTHBloKEuclid = createModel<SynTHBloKEuclid, SynTHBloKEuclidWidget>("Euclid");
