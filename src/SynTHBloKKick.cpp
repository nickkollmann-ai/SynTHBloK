// ============================================================================
// SynTHBloK-K  KICK MODULE
//  Pitch-swept sine kick with click, drive and tone, plus a separate schranz
//  "rumble" tail that is ducked by the kick. Envelope output for sidechaining.
// ============================================================================
#include "plugin.hpp"
#include "SynTHBloKKick_layout.hpp"
#include <cmath>
#include <atomic>

struct SynTHBloKKick : Module {
    enum ParamIds {
        TUNE_PARAM, SWEEP_PARAM, PDEC_PARAM,
        DECAY_PARAM, PUNCH_PARAM, TONE_PARAM,
        DRIVE_PARAM, RUMBLE_PARAM, RDEC_PARAM,
        LEVEL_PARAM, ACCENT_PARAM, DUCK_PARAM,
        NUM_PARAMS
    };
    enum InputIds { TRIG_INPUT, ACC_INPUT, VOCT_INPUT, NUM_INPUTS };
    enum OutputIds { KICK_OUTPUT, RUMBLE_OUTPUT, ENV_OUTPUT, NUM_OUTPUTS };
    enum LightIds { TRIG_LIGHT, ENUMS(KNOB_LIGHT, 12 * 3), ENUMS(JACK_LIGHT, 6 * 3), NUM_LIGHTS };

    dsp::SchmittTrigger trig;
    std::atomic<bool> manual{false};

    float phase = 0.25f, rPhase = 0.25f;
    float pEnv = 0.f, ampE = 0.f, atk = 1.f, clickE = 0.f, clickLp = 0.f;
    float rAtk = 0.f, rDec = 0.f, vel = 1.f;
    float tone1 = 0.f, tone2 = 0.f, rl1 = 0.f, rl2 = 0.f;

    SynTHBloKKick() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(TUNE_PARAM, 30.f, 120.f, 50.f, "Tune (end pitch)", " Hz");
        configParam(SWEEP_PARAM, 0.f, 1.f, 0.5f, "Pitch sweep depth", " oct", 0.f, 5.f);
        configParam(PDEC_PARAM, 0.f, 1.f, 0.4f, "Pitch decay (4 to 250 ms)");
        configParam(DECAY_PARAM, 0.f, 1.f, 0.5f, "Body decay (60 ms to 1.2 s)");
        configParam(PUNCH_PARAM, 0.f, 1.f, 0.3f, "Click / punch", "%", 0.f, 100.f);
        configParam(TONE_PARAM, 0.f, 1.f, 1.f, "Tone (low-pass, 300 Hz to 20 kHz)");
        configParam(DRIVE_PARAM, 0.f, 1.f, 0.25f, "Drive (0 to 30 dB)", "%", 0.f, 100.f);
        configParam(RUMBLE_PARAM, 0.f, 1.f, 0.f, "Rumble amount", "%", 0.f, 100.f);
        configParam(RDEC_PARAM, 0.f, 1.f, 0.5f, "Rumble decay (150 ms to 1.5 s)");
        configParam(LEVEL_PARAM, 0.f, 1.f, 0.8f, "Level", "%", 0.f, 100.f);
        configParam(ACCENT_PARAM, 0.f, 1.f, 0.6f, "Accent depth (needs ACC input)", "%", 0.f, 100.f);
        configParam(DUCK_PARAM, 0.f, 1.f, 0.7f, "Rumble ducking by the kick", "%", 0.f, 100.f);
        configInput(TRIG_INPUT, "Trigger");
        configInput(ACC_INPUT, "Accent (0 to 10 V)");
        configInput(VOCT_INPUT, "V/OCT tune");
        configOutput(KICK_OUTPUT, "Kick + rumble");
        configOutput(RUMBLE_OUTPUT, "Rumble only");
        configOutput(ENV_OUTPUT, "Envelope (0 to 10 V) for ducking");
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
        float w = std::min(1.f, std::max(0.f, x));
        float blue = std::min(1.f, std::max(0.f, 1.f - (w * 2.f)));
        float red = std::min(1.f, std::max(0.f, (w - 0.5f) * 2.f));
        float yellow = std::min(1.f, std::max(0.f, 1.f - std::fabs(w - 0.5f) * 2.f));
        float rr, gg, bb; ledRamp(w, rr, gg, bb);
        lights[baseId + 0].setSmoothBrightness(rr, 1);
        lights[baseId + 1].setSmoothBrightness(gg, 1);
        lights[baseId + 2].setSmoothBrightness(bb, 1);
    }
    inline void setSignalLED(int baseId, float v) {
        float n = std::min(1.f, std::max(-1.f, v / 5.f));
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
        ampE = 0.f; pEnv = 0.f; clickE = 0.f; rAtk = 0.f; rDec = 0.f;
        tone1 = tone2 = rl1 = rl2 = 0.f;
    }

    void process(const ProcessArgs& args) override {
        const float dt = args.sampleTime;
        const float TWO_PI = 6.28318530718f;

        // ---------------- trigger ----------------
        bool go = trig.process(inputs[TRIG_INPUT].getVoltage());
        if (manual.exchange(false)) go = true;

        float tuneHz = params[TUNE_PARAM].getValue() * std::exp2(inputs[VOCT_INPUT].getVoltage());
        tuneHz = std::min(std::max(tuneHz, 10.f), 400.f);

        if (go) {
            float acc = 1.f;
            if (inputs[ACC_INPUT].isConnected()) {
                float v = std::min(std::max(inputs[ACC_INPUT].getVoltage() / 10.f, 0.f), 1.f);
                acc = 1.f - params[ACCENT_PARAM].getValue() * (1.f - v);
            }
            vel = acc;
            ampE = 1.f; pEnv = 1.f; clickE = 1.f; atk = 0.f;
            phase = 0.25f; rPhase = 0.25f; rAtk = 0.f; rDec = 1.f;
        }

        // ---------------- kick body ----------------
        float pDecS = 0.004f * std::pow(62.5f, params[PDEC_PARAM].getValue());
        float decS = 0.06f * std::pow(20.f, params[DECAY_PARAM].getValue());
        float octs = params[SWEEP_PARAM].getValue() * 5.f;

        pEnv *= std::exp(-dt / (pDecS * 0.5f));
        ampE *= std::exp(-6.9078f * dt / decS);
        atk = std::min(1.f, atk + dt / 0.0005f);

        float f = std::min(tuneHz * std::exp2(octs * pEnv), 0.45f * args.sampleRate);
        phase += f * dt; phase -= std::floor(phase);
        float body = std::sin(TWO_PI * phase) * ampE * atk;

        // click: short high-passed noise burst
        clickE *= std::exp(-dt / 0.0012f);
        float n = random::uniform() * 2.f - 1.f;
        clickLp += (n - clickLp) * 0.25f;
        float click = (n - clickLp) * clickE * params[PUNCH_PARAM].getValue() * 0.9f;

        float x = (body + click) * vel;

        // drive
        float g = std::pow(10.f, params[DRIVE_PARAM].getValue() * 30.f / 20.f);
        x = std::tanh(g * x) / std::tanh(g);

        // tone: two cascaded one-pole low-passes
        float fc = 300.f * std::pow(66.f, params[TONE_PARAM].getValue());
        fc = std::min(fc, 0.45f * args.sampleRate);
        float a = 1.f - std::exp(-TWO_PI * fc * dt);
        tone1 += (x - tone1) * a;
        tone2 += (tone1 - tone2) * a;
        float kick = tone2 * 5.f;

        // ---------------- rumble tail ----------------
        float rdecS = 0.15f * std::pow(10.f, params[RDEC_PARAM].getValue());
        rAtk += (1.f - rAtk) * (1.f - std::exp(-dt / 0.03f));
        rDec *= std::exp(-6.9078f * dt / rdecS);
        rPhase += tuneHz * dt; rPhase -= std::floor(rPhase);
        float r = std::tanh(6.f * std::sin(TWO_PI * rPhase)) * rAtk * rDec;
        float rfc = std::min(std::max(tuneHz * 3.f, 60.f), 400.f);
        float ra = 1.f - std::exp(-TWO_PI * rfc * dt);
        rl1 += (r - rl1) * ra;
        rl2 += (rl1 - rl2) * ra;
        float duck = 1.f - params[DUCK_PARAM].getValue() * std::min(1.f, ampE * 1.5f);
        float rumble = rl2 * params[RUMBLE_PARAM].getValue() * duck * vel * 5.f;

        float lvl = params[LEVEL_PARAM].getValue();
        auto lim = [](float v) { return std::min(10.f, std::max(-10.f, v)); };
        outputs[KICK_OUTPUT].setVoltage(lim((kick + rumble) * lvl));
        outputs[RUMBLE_OUTPUT].setVoltage(lim(rumble * lvl));
        outputs[ENV_OUTPUT].setVoltage(10.f * ampE * vel);
        lights[TRIG_LIGHT].setSmoothBrightness(ampE, dt);

        // ---- LEDs: identical behaviour and colours to the FX module ----
        // knobs: blue (low) -> yellow (mid) -> red/orange (high)
        for (int i = 0; i < 12; i++) setParamLED(KNOB_LIGHT + i * 3, paramQuantities[i]->getScaledValue());
        // jacks: orange for positive signal, blue for negative
        setSignalLED(JACK_LIGHT + 0 * 3, inputs[TRIG_INPUT].getVoltage());
        setSignalLED(JACK_LIGHT + 1 * 3, inputs[ACC_INPUT].isConnected() ? inputs[ACC_INPUT].getVoltage() * 0.5f : 0.f);
        setSignalLED(JACK_LIGHT + 2 * 3, inputs[VOCT_INPUT].isConnected() ? inputs[VOCT_INPUT].getVoltage() : 0.f);
        setSignalLED(JACK_LIGHT + 3 * 3, outputs[KICK_OUTPUT].getVoltage());
        setSignalLED(JACK_LIGHT + 4 * 3, outputs[RUMBLE_OUTPUT].getVoltage());
        setSignalLED(JACK_LIGHT + 5 * 3, outputs[ENV_OUTPUT].getVoltage() * 0.5f);
    }
};

// extra-small LED (about 60% of a TinyLight)
template <typename TBase>
struct MicroLight : TinyLight<TBase> {
    MicroLight() { this->box.size = mm2px(Vec(0.9f, 0.9f)); }
};

struct SynTHBloKKickWidget : ModuleWidget {
    SynTHBloKKickWidget(SynTHBloKKick* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKKick.svg")));
        using namespace rack::componentlibrary;

        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(KKL::PANEL_W - 3.5f, 3.5f))));
        addChild(createLightCentered<MicroLight<RedLight>>(mm2px(Vec(KKL::PANEL_W - 9.f, 4.6f)), module, SynTHBloKKick::TRIG_LIGHT));

        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 3; c++)
                addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm2px(Vec(KKL::KX[c], KKL::KY[r] - 4.8f)), module, SynTHBloKKick::KNOB_LIGHT + (r * 3 + c) * 3));
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 3; c++)
                addParam(createParamCentered<Trimpot>(mm2px(Vec(KKL::KX[c], KKL::KY[r])), module, r * 3 + c));

        for (int j = 0; j < 6; j++)
            addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm2px(Vec(KKL::KX[j % 3], (j < 3 ? KKL::IN_Y : KKL::OUT_Y) + 5.2f)), module, SynTHBloKKick::JACK_LIGHT + j * 3));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(KKL::KX[0], KKL::IN_Y)), module, SynTHBloKKick::TRIG_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(KKL::KX[1], KKL::IN_Y)), module, SynTHBloKKick::ACC_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(KKL::KX[2], KKL::IN_Y)), module, SynTHBloKKick::VOCT_INPUT));
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(KKL::KX[0], KKL::OUT_Y)), module, SynTHBloKKick::KICK_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(KKL::KX[1], KKL::OUT_Y)), module, SynTHBloKKick::RUMBLE_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(KKL::KX[2], KKL::OUT_Y)), module, SynTHBloKKick::ENV_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<SynTHBloKKick*>(module);
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Trigger kick (audition)", "", [=]() { m->manual = true; }));
    }
};

Model* modelSynTHBloKKick = createModel<SynTHBloKKick, SynTHBloKKickWidget>("Kick");
