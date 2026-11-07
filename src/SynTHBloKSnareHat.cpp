// ============================================================================
// SynTHBloK-D  SNARE / HAT MODULE
//  Snare: two-oscillator body with pitch drop + filtered noise snap.
//  Hats : six detuned square oscillators (808 ratios), band/high-passed, with a
//         closed and an open trigger. The closed hat chokes the open hat.
//  Trigger amplitude (0..10 V) sets velocity when ACCENT is turned up.
// ============================================================================
#include "plugin.hpp"
#include "SynTHBloKSnareHat_layout.hpp"
#include <cmath>
#include <atomic>

struct SynTHBloKSnareHat : Module {
    enum ParamIds {
        TUNE_PARAM, SNAP_PARAM, SDEC_PARAM,
        HTUNE_PARAM, HDEC_PARAM, OPEN_PARAM,
        BODY_PARAM, DRIVE_PARAM, HTONE_PARAM,
        SLEVEL_PARAM, HLEVEL_PARAM, ACCENT_PARAM,
        NUM_PARAMS
    };
    enum InputIds { SNARE_INPUT, CHAT_INPUT, OHAT_INPUT, NUM_INPUTS };
    enum OutputIds { SNARE_OUTPUT, HAT_OUTPUT, MIX_OUTPUT, NUM_OUTPUTS };
    enum LightIds { TRIG_LIGHT, ENUMS(KNOB_LIGHT, 12 * 3), ENUMS(JACK_LIGHT, 6 * 3), NUM_LIGHTS };

    dsp::SchmittTrigger trigS, trigC, trigO;
    std::atomic<int> manual{0};   // 1 snare, 2 closed hat, 3 open hat

    // snare
    float sPh1 = 0.f, sPh2 = 0.f, sPitch = 0.f, sBodyE = 0.f, sNoiseE = 0.f, sVel = 1.f;
    float nHp = 0.f, nLp = 0.f, nBp1 = 0.f, nBp2 = 0.f;
    // hats
    float hPh[6] = {0, 0, 0, 0, 0, 0};
    float hE = 0.f, hOpen = 0.f, hVel = 1.f, hHp1 = 0.f, hHp2 = 0.f, hLp = 0.f;
    bool hatIsOpen = false;

    SynTHBloKSnareHat() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(TUNE_PARAM, 120.f, 400.f, 190.f, "Snare tune", " Hz");
        configParam(SNAP_PARAM, 0.f, 1.f, 0.6f, "Snare snap (noise amount)", "%", 0.f, 100.f);
        configParam(SDEC_PARAM, 0.f, 1.f, 0.4f, "Snare decay (60 to 500 ms)");
        configParam(HTUNE_PARAM, 0.f, 1.f, 0.5f, "Hat tune (metal pitch)");
        configParam(HDEC_PARAM, 0.f, 1.f, 0.3f, "Closed hat decay (10 to 150 ms)");
        configParam(OPEN_PARAM, 0.f, 1.f, 0.5f, "Open hat decay (150 ms to 1.2 s)");
        configParam(BODY_PARAM, 0.f, 1.f, 0.5f, "Snare body vs noise tone");
        configParam(DRIVE_PARAM, 0.f, 1.f, 0.2f, "Snare drive", "%", 0.f, 100.f);
        configParam(HTONE_PARAM, 0.f, 1.f, 0.5f, "Hat tone (high-pass, 3 to 10 kHz)");
        configParam(SLEVEL_PARAM, 0.f, 1.f, 0.8f, "Snare level", "%", 0.f, 100.f);
        configParam(HLEVEL_PARAM, 0.f, 1.f, 0.7f, "Hat level", "%", 0.f, 100.f);
        configParam(ACCENT_PARAM, 0.f, 1.f, 0.5f, "Accent depth (trigger voltage sets velocity)", "%", 0.f, 100.f);
        configInput(SNARE_INPUT, "Snare trigger");
        configInput(CHAT_INPUT, "Closed hat trigger");
        configInput(OHAT_INPUT, "Open hat trigger");
        configOutput(SNARE_OUTPUT, "Snare");
        configOutput(HAT_OUTPUT, "Hat");
        configOutput(MIX_OUTPUT, "Mix");
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
        sBodyE = sNoiseE = sPitch = hE = 0.f;
        nHp = nLp = nBp1 = nBp2 = hHp1 = hHp2 = hLp = 0.f;
    }

    float velFrom(float trigV) {
        float v = clamp(trigV / 10.f, 0.f, 1.f);
        return 1.f - params[ACCENT_PARAM].getValue() * (1.f - v);
    }

    void process(const ProcessArgs& args) override {
        const float dt = args.sampleTime;
        const float TWO_PI = 6.28318530718f;
        const float sr = args.sampleRate;

        int man = manual.exchange(0);
        bool goS = trigS.process(inputs[SNARE_INPUT].getVoltage()) || man == 1;
        bool goC = trigC.process(inputs[CHAT_INPUT].getVoltage()) || man == 2;
        bool goO = trigO.process(inputs[OHAT_INPUT].getVoltage()) || man == 3;

        if (goS) { sVel = velFrom(inputs[SNARE_INPUT].getVoltage()); sBodyE = 1.f; sNoiseE = 1.f; sPitch = 1.f; }
        if (goC) { hVel = velFrom(inputs[CHAT_INPUT].getVoltage()); hE = 1.f; hatIsOpen = false; }
        if (goO) { hVel = velFrom(inputs[OHAT_INPUT].getVoltage()); hE = 1.f; hatIsOpen = true; }

        // ---------------- snare ----------------
        float decS = 0.06f * std::pow(8.33f, params[SDEC_PARAM].getValue());          // 60..500 ms
        float tune = params[TUNE_PARAM].getValue();
        sPitch *= std::exp(-dt / 0.012f);
        float f1 = std::min(tune * (1.f + 0.8f * sPitch), 0.45f * sr);
        float f2 = std::min(tune * 1.62f * (1.f + 0.5f * sPitch), 0.45f * sr);
        sPh1 += f1 * dt; sPh1 -= std::floor(sPh1);
        sPh2 += f2 * dt; sPh2 -= std::floor(sPh2);
        sBodyE *= std::exp(-6.9078f * dt / (decS * 0.55f));
        sNoiseE *= std::exp(-6.9078f * dt / decS);
        float body = (std::sin(TWO_PI * sPh1) * 0.7f + std::sin(TWO_PI * sPh2) * 0.3f) * sBodyE;

        float n = random::uniform() * 2.f - 1.f;
        // tone: low = darker noise band, high = brighter noise
        float bodyK = params[BODY_PARAM].getValue();
        float hpFc = 400.f + 2200.f * bodyK;
        float hpA = 1.f - std::exp(-TWO_PI * std::min(hpFc, 0.45f * sr) * dt);
        nLp += (n - nLp) * hpA;
        float nHi = n - nLp;                                    // high-passed noise
        float lpFc = 3500.f + 9000.f * bodyK;
        float lpA = 1.f - std::exp(-TWO_PI * std::min(lpFc, 0.45f * sr) * dt);
        nHp += (nHi - nHp) * lpA;
        float noise = nHp * sNoiseE * params[SNAP_PARAM].getValue() * 1.6f;

        float sx = (body * (1.f - 0.35f * params[SNAP_PARAM].getValue()) + noise) * sVel;
        float g = std::pow(10.f, params[DRIVE_PARAM].getValue() * 24.f / 20.f);
        sx = std::tanh(g * sx) / std::tanh(g);
        float snare = sx * 5.f * params[SLEVEL_PARAM].getValue();

        // ---------------- hats ----------------
        static const float R[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
        float ht = 0.6f + params[HTUNE_PARAM].getValue() * 1.6f;                        // 0.6..2.2
        float sq = 0.f;
        for (int i = 0; i < 6; i++) {
            float fi = std::min(R[i] * ht, 0.45f * sr);
            hPh[i] += fi * dt; hPh[i] -= std::floor(hPh[i]);
            sq += (hPh[i] < 0.5f) ? 1.f : -1.f;
        }
        sq *= (1.f / 6.f);
        float cDec = 0.010f * std::pow(15.f, params[HDEC_PARAM].getValue());           // 10..150 ms
        float oDec = 0.150f * std::pow(8.f, params[OPEN_PARAM].getValue());            // 150 ms..1.2 s
        float hd = hatIsOpen ? oDec : cDec;
        hE *= std::exp(-6.9078f * dt / hd);
        float hpF = 3000.f + 7000.f * params[HTONE_PARAM].getValue();
        float ha = 1.f - std::exp(-TWO_PI * std::min(hpF, 0.45f * sr) * dt);
        hHp1 += (sq - hHp1) * ha;
        float h1 = sq - hHp1;
        hHp2 += (h1 - hHp2) * ha;
        float h2 = h1 - hHp2;
        float hx = h2 * hE * hVel;
        float hat = hx * 5.f * params[HLEVEL_PARAM].getValue();

        auto lim = [](float v) { return std::min(10.f, std::max(-10.f, v)); };
        outputs[SNARE_OUTPUT].setVoltage(lim(snare));
        outputs[HAT_OUTPUT].setVoltage(lim(hat));
        outputs[MIX_OUTPUT].setVoltage(lim(snare + hat));
        lights[TRIG_LIGHT].setSmoothBrightness(std::max(sNoiseE, hE), dt);

        for (int i = 0; i < 12; i++) setParamLED(KNOB_LIGHT + i * 3, paramQuantities[i]->getScaledValue());
        setSignalLED(JACK_LIGHT + 0 * 3, inputs[SNARE_INPUT].getVoltage());
        setSignalLED(JACK_LIGHT + 1 * 3, inputs[CHAT_INPUT].getVoltage());
        setSignalLED(JACK_LIGHT + 2 * 3, inputs[OHAT_INPUT].getVoltage());
        setSignalLED(JACK_LIGHT + 3 * 3, outputs[SNARE_OUTPUT].getVoltage());
        setSignalLED(JACK_LIGHT + 4 * 3, outputs[HAT_OUTPUT].getVoltage());
        setSignalLED(JACK_LIGHT + 5 * 3, outputs[MIX_OUTPUT].getVoltage());
    }
};

// extra-small LED
template <typename TBase>
struct MicroLightSH : TinyLight<TBase> {
    MicroLightSH() { this->box.size = mm2px(Vec(0.9f, 0.9f)); }
};

struct SynTHBloKSnareHatWidget : ModuleWidget {
    SynTHBloKSnareHatWidget(SynTHBloKSnareHat* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKSnareHat.svg")));
        using namespace rack::componentlibrary;

        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(SHL::PANEL_W - 3.5f, 3.5f))));
        addChild(createLightCentered<MicroLightSH<RedLight>>(mm2px(Vec(SHL::PANEL_W - 9.f, 4.6f)), module, SynTHBloKSnareHat::TRIG_LIGHT));

        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 3; c++) {
                addChild(createLightCentered<MicroLightSH<RedGreenBlueLight>>(mm2px(Vec(SHL::KX[c], SHL::KY[r] - 4.8f)), module, SynTHBloKSnareHat::KNOB_LIGHT + (r * 3 + c) * 3));
                addParam(createParamCentered<Trimpot>(mm2px(Vec(SHL::KX[c], SHL::KY[r])), module, r * 3 + c));
            }
        for (int j = 0; j < 6; j++)
            addChild(createLightCentered<MicroLightSH<RedGreenBlueLight>>(mm2px(Vec(SHL::KX[j % 3], (j < 3 ? SHL::IN_Y : SHL::OUT_Y) + 5.2f)), module, SynTHBloKSnareHat::JACK_LIGHT + j * 3));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(SHL::KX[0], SHL::IN_Y)), module, SynTHBloKSnareHat::SNARE_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(SHL::KX[1], SHL::IN_Y)), module, SynTHBloKSnareHat::CHAT_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(SHL::KX[2], SHL::IN_Y)), module, SynTHBloKSnareHat::OHAT_INPUT));
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(SHL::KX[0], SHL::OUT_Y)), module, SynTHBloKSnareHat::SNARE_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(SHL::KX[1], SHL::OUT_Y)), module, SynTHBloKSnareHat::HAT_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(SHL::KX[2], SHL::OUT_Y)), module, SynTHBloKSnareHat::MIX_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<SynTHBloKSnareHat*>(module);
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Trigger snare (audition)", "", [=]() { m->manual = 1; }));
        menu->addChild(createMenuItem("Trigger closed hat (audition)", "", [=]() { m->manual = 2; }));
        menu->addChild(createMenuItem("Trigger open hat (audition)", "", [=]() { m->manual = 3; }));
    }
};

Model* modelSynTHBloKSnareHat = createModel<SynTHBloKSnareHat, SynTHBloKSnareHatWidget>("SnareHat");
