#include "plugin.hpp"

struct SynTHBloKFXModule : Module {

    enum ParamIds {
        TIME_PARAM,
        FEEDBACK_PARAM,
        MIX_PARAM,
        COLOR_PARAM,
        MOD_DEPTH_PARAM,
        MOD_RATE_PARAM,
        DUCKING_PARAM,
        WIDTH_PARAM,
        DELAY_TYPE_BUTTON_PARAM,
        DELAY_TYPE_PARAM,
        SUBDIV_PARAM,
        SYNC_PARAM,
        RESET_PARAM,
        PREPOST_MIX_PARAM,
        NUM_PARAMS
    };

    enum InputIds {
        TIME_CV_INPUT,
        FEEDBACK_CV_INPUT,
        MIX_CV_INPUT,
        COLOR_CV_INPUT,
        MOD_DEPTH_CV_INPUT,
        MOD_RATE_CV_INPUT,
        DUCKING_CV_INPUT,
        WIDTH_CV_INPUT,
        CLOCK_INPUT,
        IN_L_INPUT,
        IN_R_INPUT,
        NUM_INPUTS
    };

    enum OutputIds {
        LEFT_OUTPUT,
        RIGHT_OUTPUT,
        SIDE_OUT_OUTPUT,
        NUM_OUTPUTS
    };

    enum LightIds {
        TIME_LIGHT,
        FEEDBACK_LIGHT,
        MIX_LIGHT,
        COLOR_LIGHT,
        MOD_DEPTH_LIGHT,
        MOD_RATE_LIGHT,
        DUCKING_LIGHT,
        WIDTH_LIGHT,
        DELAY_TYPE_LIGHT,
        SYNC_LIGHT,
        RESET_LIGHT,

        TIME_CV_LED_RED, TIME_CV_LED_GREEN, TIME_CV_LED_BLUE,
        FEEDBACK_CV_LED_RED, FEEDBACK_CV_LED_GREEN, FEEDBACK_CV_LED_BLUE,
        MIX_CV_LED_RED, MIX_CV_LED_GREEN, MIX_CV_LED_BLUE,

        COLOR_CV_TINY_RED, COLOR_CV_TINY_GREEN, COLOR_CV_TINY_BLUE,
        MOD_DEPTH_CV_TINY_RED, MOD_DEPTH_CV_TINY_GREEN, MOD_DEPTH_CV_TINY_BLUE,
        MOD_RATE_CV_TINY_RED, MOD_RATE_CV_TINY_GREEN, MOD_RATE_CV_TINY_BLUE,

        DUCKING_CV_TINY_RED, DUCKING_CV_TINY_GREEN, DUCKING_CV_TINY_BLUE,
        WIDTH_CV_TINY_RED, WIDTH_CV_TINY_GREEN, WIDTH_CV_TINY_BLUE,
        CLOCK_TINY_RED, CLOCK_TINY_GREEN, CLOCK_TINY_BLUE,

        IN_L_TINY_RED, IN_L_TINY_GREEN, IN_L_TINY_BLUE,
        IN_R_TINY_RED, IN_R_TINY_GREEN, IN_R_TINY_BLUE,
        OUT_L_TINY_RED, OUT_L_TINY_GREEN, OUT_L_TINY_BLUE,
        OUT_R_TINY_RED, OUT_R_TINY_GREEN, OUT_R_TINY_BLUE,
        SIDE_TINY_RED, SIDE_TINY_GREEN, SIDE_TINY_BLUE,

        NUM_LIGHTS
    };

    std::vector<float> delayBufferL;
    std::vector<float> delayBufferR;

    bool delayTypeButtonLast = false;
    bool lastReset = false;
    bool resetState = false;

    int writePos = 0;

    float smearZL = 0.f, smearZR = 0.f;
    float toneZL  = 0.f, toneZR  = 0.f;

    float d1L = 0.f, d2L = 0.f, d3L = 0.f;
    float d1R = 0.f, d2R = 0.f, d3R = 0.f;

    bool lastClockHigh = false;
    float clockTimer = 0.f;
    float clockPeriod = 0.5f;

    float envFollower = 0.f;
    float fbZL = 0.f, fbZR = 0.f;   // per-instance feedback lowpass state (was static)
    float wowPhase = 0.f;
    float flutterPhase = 0.f;

    // REVERSE: two read heads play the last chunk backwards, half a chunk apart, crossfaded
    float revPhase = 0.f;
    int revAnchorA = 0, revAnchorB = 0;

    SynTHBloKFXModule() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        // === PARAMS (Short Names) ===
        configParam(TIME_PARAM,      0.f, 1.f, 0.25f, "Time");
        configParam(FEEDBACK_PARAM,  0.f, 1.f, 0.30f, "FBK");
        configParam(MIX_PARAM,       0.f, 1.f, 0.50f, "Mix");
        configParam(COLOR_PARAM,     0.f, 1.f, 0.50f, "Tone");
        configParam(MOD_DEPTH_PARAM, 0.f, 1.f, 0.00f, "Depth");
        configParam(MOD_RATE_PARAM,  0.f, 1.f, 0.20f, "Rate");
        configParam(DUCKING_PARAM,   0.f, 1.f, 0.00f, "Duck");
        configParam(WIDTH_PARAM,     0.f, 1.f, 0.50f, "Width");

        configButton(DELAY_TYPE_BUTTON_PARAM, "Mode");
        configSwitch(DELAY_TYPE_PARAM, 0.f, 7.f, 0.f, "Delay mode",
            {"Clean", "Tape", "Analog", "Modern", "Ping-pong", "Dynamic", "Space", "Reverse"});

        configSwitch(SUBDIV_PARAM, 0.f, 2.f, 0.f, "Div",
            {"1/1", "3/4", "2/3"});

        configButton(SYNC_PARAM, "Sync");
        configButton(RESET_PARAM, "Rst");

        // === INPUTS (Short Names) ===
        configInput(TIME_CV_INPUT,      "Time CV");
        configInput(FEEDBACK_CV_INPUT,  "FBK CV");
        configInput(MIX_CV_INPUT,       "Mix CV");
        configInput(COLOR_CV_INPUT,     "Tone CV");
        configInput(MOD_DEPTH_CV_INPUT, "Depth CV");
        configInput(MOD_RATE_CV_INPUT,  "Rate CV");
        configInput(DUCKING_CV_INPUT,   "Duck CV");
        configInput(WIDTH_CV_INPUT,     "Width CV");
        configParam(PREPOST_MIX_PARAM, 0.f, 1.f, 0.5f, "Pre/Post (unused)");   // kept so old patches load
        configInput(CLOCK_INPUT, "Clk");
        configInput(IN_L_INPUT,  "In L");
        configInput(IN_R_INPUT,  "In R");

        // === OUTPUTS (Short Names) ===
        configOutput(LEFT_OUTPUT,  "Out L");
        configOutput(RIGHT_OUTPUT, "Out R");
        configOutput(SIDE_OUT_OUTPUT, "Side");

        float sr = APP->engine->getSampleRate();
        int maxDelaySamples = (int)(sr * 2.0f);

        delayBufferL.resize(maxDelaySamples, 0.f);
        delayBufferR.resize(maxDelaySamples, 0.f);
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
        float w = clamp(x, 0.f, 1.f);

        float blue   = clamp(1.f - (w * 2.f), 0.f, 1.f);
        float red    = clamp((w - 0.5f) * 2.f, 0.f, 1.f);
        float yellow = clamp(1.f - fabsf(w - 0.5f) * 2.f, 0.f, 1.f);

        float rr, gg, bb; ledRamp(w, rr, gg, bb);
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

    float saturateSoft(float x, float drive);
    float toneFilter(float x, float& z, float normCut, float sr);
    float smearDiffusion(float x, float& z, float amt);
    float reverseCrossfade(float a, float b, float t);
    void  pingPongMix(float& dl, float& dr, float width);

    // Keep the 2-second delay buffer correct if the engine sample rate changes
    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        int maxDelaySamples = (int)(e.sampleRate * 2.0f);
        delayBufferL.assign(maxDelaySamples, 0.f);
        delayBufferR.assign(maxDelaySamples, 0.f);
        writePos = 0;
    }

    void process(const ProcessArgs& args) override;
};
struct DelayTypeOLED : TransparentWidget {
    SynTHBloKFXModule* module;
    std::shared_ptr<Font> font;

    DelayTypeOLED(SynTHBloKFXModule* m) {
        module = m;
        font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        box.size = Vec(33, 12);
    }

    void draw(const DrawArgs& args) override {
        NVGcontext* vg = args.vg;

        NVGpaint bg = nvgLinearGradient(vg, 0, 0, 0, box.size.y,
            nvgRGBA(10, 10, 10, 255),
            nvgRGBA(0, 0, 0, 255));

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgFillPaint(vg, bg);
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgStrokeColor(vg, nvgRGBA(255, 180, 90, 80));
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);

        static const char* names[8] = {
            "CLEAN", "TAPE", "ANALOG", "MODERN",
            "PING", "DYNAMIC", "SPACE", "REVERSE"
        };

        // the module browser preview has no module: show the default mode
        int mode = module ? (int)module->params[SynTHBloKFXModule::DELAY_TYPE_PARAM].getValue() : 0;
        mode = clamp(mode, 0, 7);

        nvgFontSize(vg, 9.f);
        nvgFontFaceId(vg, font->handle);
        nvgFillColor(vg, nvgRGBA(255, 240, 180, 255));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, box.size.x * 0.5f, box.size.y * 0.5f, names[mode], NULL);

        for (int y = 1; y < box.size.y; y += 2) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, 1, y);
            nvgLineTo(vg, box.size.x - 1, y);
            nvgStrokeColor(vg, nvgRGBA(255, 200, 120, 18));
            nvgStrokeWidth(vg, 0.4f);
            nvgStroke(vg);
        }
    }
};

struct DivOLED : TransparentWidget {
    SynTHBloKFXModule* module = nullptr;

    float animPhase = 0.f;
    float idlePhase = 0.f;
    float flash     = 0.f;

    std::shared_ptr<Font> font;

    DivOLED(SynTHBloKFXModule* m) {
        module = m;
        font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        box.size = Vec(30.f, 14.f);   // same footprint style as DelayTypeOLED
    }

    void step() override {
        const float dt = 1.f / 60.f;

        // --- CLOCK-SYNCED ANIMATION SPEED ---
        float speed = 0.5f;   // default free-run speed

        if (module && module->inputs[SynTHBloKFXModule::CLOCK_INPUT].isConnected()) {
            float period = module->clockPeriod;

            if (period < 0.05f) period = 0.05f;
            if (period > 2.0f)  period = 2.0f;

            float bpmSpeed = 1.f / period;
            speed = bpmSpeed * 0.25f;
        }

        animPhase += dt * speed;
        if (animPhase > 1.f)
            animPhase -= 1.f;

        idlePhase += dt * 0.2f;
        if (idlePhase > 2.f * M_PI)
            idlePhase -= 2.f * M_PI;

        if (module && module->inputs[SynTHBloKFXModule::CLOCK_INPUT].isConnected()) {
            float clk = module->inputs[SynTHBloKFXModule::CLOCK_INPUT].getVoltage();
            if (clk > 1.f)
                flash = 1.f;
        }

        flash *= 0.9f;

        TransparentWidget::step();
    }

    void draw(const DrawArgs& args) override {
        NVGcontext* vg = args.vg;
        nvgSave(vg);

        NVGpaint bg = nvgLinearGradient(
            vg,
            0, 0, 0, box.size.y,
            nvgRGBA(10, 10, 10, 255),
            nvgRGBA(0, 0, 0, 255)
        );

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgFillPaint(vg, bg);
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgStrokeColor(vg, nvgRGBA(255, 180, 90, 80));
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);

        int divMode = 0;
        if (module) {
            divMode = (int)std::round(module->params[SynTHBloKFXModule::SUBDIV_PARAM].getValue());
            divMode = clamp(divMode, 0, 2);
        }

        const char* divText = "1/1";
        if (divMode == 1) divText = "3/4";
        else if (divMode == 2) divText = "2/3";

        float baseBright = 1.f;
        if (!module)
            baseBright = 0.6f + 0.3f * std::sin(idlePhase);

        float flashBright = 0.25f * flash;
        float textBright  = clamp(baseBright + flashBright, 0.f, 1.f);

        NVGcolor textColor = nvgRGBA(
            255,
            240,
            180,
            (unsigned char)(255 * textBright)
        );

        nvgFontSize(vg, 9.f);
        nvgFontFaceId(vg, font->handle);
        nvgFillColor(vg, textColor);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgText(vg, 3.f, 3.f, divText, nullptr);

        float barX = 3.f;
        float barY = 12.f;
        float barW = box.size.x - 6.f;
        float barH = box.size.y - barY - 3.f;

        nvgBeginPath(vg);
        nvgRoundedRect(vg, barX, barY, barW, barH, 1.5f);
        nvgFillColor(vg, nvgRGBA(20, 12, 4, 255));
        nvgFill(vg);

        nvgStrokeWidth(vg, 0.8f);
        nvgStrokeColor(vg, nvgRGBA(255, 180, 90, 40));
        nvgStroke(vg);

        float barBright = 0.9f;
        if (!module)
            barBright = 0.5f + 0.3f * std::sin(idlePhase * 0.8f);

        barBright = clamp(barBright + flashBright, 0.f, 1.f);

        NVGcolor barColor = nvgRGBA(
            255,
            200,
            120,
            (unsigned char)(255 * barBright)
        );

        nvgFillColor(vg, barColor);

        if (divMode == 0) {
            float t = animPhase;
            float e = t * t * (3.f - 2.f * t);
            float fillW = barW * e;

            nvgBeginPath(vg);
            nvgRoundedRect(vg, barX, barY, fillW, barH, 1.2f);
            nvgFill(vg);
        }
        else if (divMode == 1) {
            int   clusters     = 3;
            float clusterWidth = barW / clusters;
            float localT       = animPhase * clusters;

            for (int i = 0; i < clusters; i++) {
                float phase = localT - i;
                if (phase < 0.f) phase += clusters;
                if (phase > 1.f) continue;

                float e  = phase * phase * (3.f - 2.f * phase);
                float cw = clusterWidth * 0.7f;
                float cx = barX + clusterWidth * i + (clusterWidth - cw) * 0.5f;
                float fillW = cw * e;

                nvgBeginPath(vg);
                nvgRoundedRect(vg, cx, barY, fillW, barH, 1.2f);
                nvgFill(vg);
            }
        }
        else {
            int   segments = 4;
            float segWidth = barW / segments;
            float localT   = animPhase * segments;

            for (int i = 0; i < segments; i++) {
                float phase = localT - i;
                if (phase < 0.f) phase += segments;
                if (phase > 1.f) continue;

                float e  = phase * phase * (3.f - 2.f * phase);
                float sw = segWidth * 0.9f;
                float sx = barX + segWidth * i + (segWidth - sw) * 0.5f;
                float fillW = sw * e;

                nvgBeginPath(vg);
                nvgRoundedRect(vg, sx, barY, fillW, barH, 1.0f);
                nvgFill(vg);
            }
        }

        for (int y = 1; y < box.size.y; y += 2) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, 1, y);
            nvgLineTo(vg, box.size.x - 1, y);
            nvgStrokeColor(vg, nvgRGBA(255, 200, 120, 18));
            nvgStrokeWidth(vg, 0.4f);
            nvgStroke(vg);
        }

        nvgRestore(vg);
    }
};
struct SynTHBloKFXWidget : ModuleWidget {
    SynTHBloKFXWidget(SynTHBloKFXModule* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKFX.svg")));

        using namespace rack::componentlibrary;

        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(57.46f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 125.0f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(57.46f, 125.0f))));

        const float x0 = 10.f;
        const float x1 = 30.f;
        const float x2 = 50.f;

        addChild(createLightCentered<SmallLight<GreenLight>>(
            mm2px(Vec(41.0f, 87.0f)),
            module,
            SynTHBloKFXModule::DELAY_TYPE_LIGHT));

        addParam(createParamCentered<TL1105>(
            mm2px(Vec(41.0f, 87.0f)),
            module,
            SynTHBloKFXModule::DELAY_TYPE_BUTTON_PARAM));

        auto* typeOLED = new DelayTypeOLED(module);
        typeOLED->box.pos = mm2px(Vec(45.0f, 83.5f));
        addChild(typeOLED);

        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x0, 20)), module, SynTHBloKFXModule::TIME_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x1, 20)), module, SynTHBloKFXModule::FEEDBACK_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x2, 20)), module, SynTHBloKFXModule::MIX_PARAM));

        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x0, 50)), module, SynTHBloKFXModule::COLOR_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x1, 50)), module, SynTHBloKFXModule::MOD_DEPTH_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x2, 50)), module, SynTHBloKFXModule::MOD_RATE_PARAM));

        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x0, 85)), module, SynTHBloKFXModule::DUCKING_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x1, 85)), module, SynTHBloKFXModule::WIDTH_PARAM));
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(x2, 75)), module, SynTHBloKFXModule::SUBDIV_PARAM));

        auto* divOLED = new DivOLED(module);
        divOLED->box.pos  = mm2px(Vec(37.0f, 71.0f));
        divOLED->box.size = mm2px(Vec(8.0f, 8.0f));
        addChild(divOLED);

        // bottom row: L OUT, R OUT, SIDE, L IN, R IN
        const float bx[5] = {8.0f, 19.25f, 30.5f, 41.75f, 53.0f};
        addInput(createInputCentered<BlackJack>(mm2px(Vec(bx[3], 115.0f)), module, SynTHBloKFXModule::IN_L_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(bx[4], 115.0f)), module, SynTHBloKFXModule::IN_R_INPUT));

        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(bx[0], 115.0f)), module, SynTHBloKFXModule::LEFT_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(bx[1], 115.0f)), module, SynTHBloKFXModule::RIGHT_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(bx[2], 115.0f)), module, SynTHBloKFXModule::SIDE_OUT_OUTPUT));

        addInput(createInputCentered<BlackJack>(mm2px(Vec(x0, 33)), module, SynTHBloKFXModule::TIME_CV_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(x1, 33)), module, SynTHBloKFXModule::FEEDBACK_CV_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(x2, 33)), module, SynTHBloKFXModule::MIX_CV_INPUT));

        addInput(createInputCentered<BlackJack>(mm2px(Vec(x0, 62)), module, SynTHBloKFXModule::COLOR_CV_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(x1, 62)), module, SynTHBloKFXModule::MOD_DEPTH_CV_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(x2, 62)), module, SynTHBloKFXModule::MOD_RATE_CV_INPUT));

        addInput(createInputCentered<BlackJack>(mm2px(Vec(x0, 98)), module, SynTHBloKFXModule::DUCKING_CV_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(x1, 98)), module, SynTHBloKFXModule::WIDTH_CV_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(x2, 98)), module, SynTHBloKFXModule::CLOCK_INPUT));

        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x0, 27)), module, SynTHBloKFXModule::TIME_CV_LED_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x1, 27)), module, SynTHBloKFXModule::FEEDBACK_CV_LED_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x2, 27)), module, SynTHBloKFXModule::MIX_CV_LED_RED));

        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x0, 57)), module, SynTHBloKFXModule::COLOR_CV_TINY_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x1, 57)), module, SynTHBloKFXModule::MOD_DEPTH_CV_TINY_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x2, 57)), module, SynTHBloKFXModule::MOD_RATE_CV_TINY_RED));

        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x0, 92)), module, SynTHBloKFXModule::DUCKING_CV_TINY_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x1, 92)), module, SynTHBloKFXModule::WIDTH_CV_TINY_RED));
        // the CLK label sits above the clock jack, so its light goes beside it
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(56.0f, 98)), module, SynTHBloKFXModule::CLOCK_TINY_RED));

        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(bx[3], 120.0f)), module, SynTHBloKFXModule::IN_L_TINY_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(bx[4], 120.0f)), module, SynTHBloKFXModule::IN_R_TINY_RED));

        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(bx[0], 120.0f)), module, SynTHBloKFXModule::OUT_L_TINY_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(bx[1], 120.0f)), module, SynTHBloKFXModule::OUT_R_TINY_RED));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(bx[2], 120.0f)), module, SynTHBloKFXModule::SIDE_TINY_RED));
    }
};
// -----------------------------------------------------------------------------
// HELPER DSP FUNCTIONS (UPGRADED ENGINE)
// -----------------------------------------------------------------------------
inline float hermite4(float xm1, float x0, float x1, float x2, float t) {
    float c0 = x0;
    float c1 = 0.5f * (x1 - xm1);
    float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
    float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

inline float fbLowpass(float x, float& z, float cutoff) {
    float a = clamp(cutoff, 0.01f, 0.45f);
    z += a * (x - z);
    return z;
}

inline float diffuse(float x, float& z, float amt) {
    float a = amt * 0.35f;
    float y = x + a * (x - z);
    z = y;
    return y;
}

inline float softSat(float x) {
    return tanhf(x * 0.7f);
}

// -----------------------------------------------------------------------------
// PROCESS
// -----------------------------------------------------------------------------
void SynTHBloKFXModule::process(const ProcessArgs& args) {
    float sr = args.sampleRate;

    bool pressed = params[DELAY_TYPE_BUTTON_PARAM].getValue() > 0.5f;
    if (pressed && !delayTypeButtonLast) {
        int v = (int)params[DELAY_TYPE_PARAM].getValue();
        v = (v + 1) % 8;
        params[DELAY_TYPE_PARAM].setValue(v);
    }
    delayTypeButtonLast = pressed;

    bool resetPressed = params[RESET_PARAM].getValue() > 0.5f;
    bool resetEdge    = resetPressed && !lastReset;
    lastReset         = resetPressed;

    if (resetEdge)
        resetState = true;

    float inL = inputs[IN_L_INPUT].isConnected() ? inputs[IN_L_INPUT].getVoltage() : 0.f;
    // a mono patch into IN L feeds both sides
    float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : inL;

    float time     = params[TIME_PARAM].getValue();
    float fb       = params[FEEDBACK_PARAM].getValue();
    float mix      = params[MIX_PARAM].getValue();
    float color    = params[COLOR_PARAM].getValue();
    float modDepth = params[MOD_DEPTH_PARAM].getValue();
    float modRate  = params[MOD_RATE_PARAM].getValue();
    float duckAmt  = params[DUCKING_PARAM].getValue();
    float width    = params[WIDTH_PARAM].getValue();

    int delayType  = (int)params[DELAY_TYPE_PARAM].getValue();
    int subdiv     = (int)params[SUBDIV_PARAM].getValue();

    if (resetState) {
        writePos = 0;
        smearZL = smearZR = 0.f;
        toneZL  = toneZR  = 0.f;
        d1L = d2L = d3L = 0.f;
        d1R = d2R = d3R = 0.f;
        std::fill(delayBufferL.begin(), delayBufferL.end(), 0.f);
        std::fill(delayBufferR.begin(), delayBufferR.end(), 0.f);
        lastClockHigh = false;
        clockTimer = 0.f;
        resetState = false;
    }

    float clk = inputs[CLOCK_INPUT].getVoltage();
    bool clockHigh = clk > 1.0f;

    if (clockHigh && !lastClockHigh) {
        clockPeriod = clockTimer;
        clockTimer = 0.f;
    }
    lastClockHigh = clockHigh;
    clockTimer += args.sampleTime;

    if (inputs[CLOCK_INPUT].isConnected()) {
        float bpm = 60.f / std::max(clockPeriod, 1e-3f);

        float subdivMul = 1.f;
        if (subdiv == 1) subdivMul = 0.75f;
        if (subdiv == 2) subdivMul = 0.666f;

        float beatTime = subdivMul * (60.f / bpm);
        float maxTime  = 2.0f;
        time = clamp(beatTime / maxTime, 0.f, 1.f);
    }

    auto applyCV = [&](float base, int cvInput) {
        if (inputs[cvInput].isConnected())
            base += inputs[cvInput].getVoltage() * 0.1f;
        return clamp(base, 0.f, 1.f);
    };

    time     = applyCV(time,     TIME_CV_INPUT);
    fb       = applyCV(fb,       FEEDBACK_CV_INPUT);
    mix      = applyCV(mix,      MIX_CV_INPUT);
    color    = applyCV(color,    COLOR_CV_INPUT);
    modDepth = applyCV(modDepth, MOD_DEPTH_CV_INPUT);
    modRate  = applyCV(modRate,  MOD_RATE_CV_INPUT);
    duckAmt  = applyCV(duckAmt,  DUCKING_CV_INPUT);
    width    = applyCV(width,    WIDTH_CV_INPUT);

    fb = clamp(fb, 0.f, 0.95f);

    // TONE shapes every mode (CLEAN included)
    bool colorModeOn = true;

    bool colorActive = colorModeOn &&
        ((params[COLOR_PARAM].getValue() > 0.f) || inputs[COLOR_CV_INPUT].isConnected());

    // DEPTH / RATE move the read head in every mode but REVERSE
    bool modModeOn = delayType != 7;

    bool modDepthActive = modModeOn &&
        ((params[MOD_DEPTH_PARAM].getValue() > 0.f) || inputs[MOD_DEPTH_CV_INPUT].isConnected());
    bool modRateActive = modModeOn &&
        ((params[MOD_RATE_PARAM].getValue() > 0.f) || inputs[MOD_RATE_CV_INPUT].isConnected());

    // DUCK works in every mode (the wet signal dips under the input)
    bool duckModeOn = true;
    bool duckingActive = duckModeOn &&
        ((params[DUCKING_PARAM].getValue() > 0.f) || inputs[DUCKING_CV_INPUT].isConnected());

    bool clockActive = inputs[CLOCK_INPUT].isConnected();
    bool inLActive   = inputs[IN_L_INPUT].isConnected();
    bool inRActive   = inputs[IN_R_INPUT].isConnected();
    bool outActive   = true;

    float maxDelay     = (float)delayBufferL.size();
    int   maxDelayI    = (int)maxDelay;
    float minDelaySec  = 0.02f;
    float maxDelaySec  = 2.0f;
    float delaySec     = minDelaySec + time * (maxDelaySec - minDelaySec);
    float delaySamples = clamp(delaySec * sr, 1.f, maxDelay - 3.f);

    writePos++;
    if (writePos >= maxDelayI)
        writePos = 0;

    float modOffset = 0.f;

    if (delayType != 1 && delayType != 7) {   // a plain sine wobble; TAPE has its own wow + flutter
        float rate = 0.1f + modRate * 3.f;
        wowPhase += 2.f * M_PI * rate * args.sampleTime;
        modOffset = sinf(wowPhase) * (modDepth * 0.003f * sr);
    }

    if (delayType == 1) {
        wowPhase     += 2.f * M_PI * (0.2f + modRate * 0.5f) * args.sampleTime;
        flutterPhase += 2.f * M_PI * (3.f + modRate * 5.f)   * args.sampleTime;
        float wow     = sinf(wowPhase)     * (0.002f * sr * modDepth);
        float flutter = sinf(flutterPhase) * (0.001f * sr * modDepth);
        modOffset = wow + flutter;
    }

    float readPosF = (float)writePos - (delaySamples + modOffset);
    while (readPosF < 0.f)
        readPosF += maxDelay;

    int readPos0 = (int)readPosF;
    int readPos1 = readPos0 + 1;
    if (readPos1 >= maxDelayI)
        readPos1 -= maxDelayI;

    float frac = readPosF - (float)readPos0;

    int rm1 = readPos0 - 1;
    if (rm1 < 0) rm1 += maxDelayI;
    int r2 = readPos1 + 1;
    if (r2 >= maxDelayI) r2 -= maxDelayI;

    float dl = hermite4(
        delayBufferL[rm1],
        delayBufferL[readPos0],
        delayBufferL[readPos1],
        delayBufferL[r2],
        frac
    );

    float dr = hermite4(
        delayBufferR[rm1],
        delayBufferR[readPos0],
        delayBufferR[readPos1],
        delayBufferR[r2],
        frac
    );

    float smearAmt = 0.25f;
    dl = diffuse(dl, smearZL, smearAmt);
    dr = diffuse(dr, smearZR, smearAmt);

    {
        float decor = 0.003f;
        float mid   = 0.5f * (dl + dr);
        float side  = 0.5f * (dl - dr);
        side *= (1.f + decor);
        dl = mid + side;
        dr = mid - side;
    }
    switch (delayType) {
        case 0: { // CLEAN (studio-grade)
            // HF damping set by TONE (the old fixed 0.45 at noon)
            dl = toneFilter(dl, toneZL, 0.20f + color * 0.50f, sr);
            dr = toneFilter(dr, toneZR, 0.20f + color * 0.50f, sr);

            // Micro‑decorrelation (adds width without coloration)
            float widen = 0.002f;
            float mid   = 0.5f * (dl + dr);
            float side  = 0.5f * (dl - dr) * (1.f + widen);
            dl = mid + side;
            dr = mid - side;
        }
        break;

        case 1: { // TAPE (pre-emphasis → saturation → de-emphasis)
            // Pre‑emphasis (tape head EQ)
            dl = toneFilter(dl, toneZL, 0.35f + color * 0.25f, sr);
            dr = toneFilter(dr, toneZR, 0.35f + color * 0.25f, sr);

            // Saturation
            dl = tanhf(dl * (0.7f + color * 0.4f));
            dr = tanhf(dr * (0.7f + color * 0.4f));

            // De‑emphasis (playback EQ)
            dl = toneFilter(dl, toneZL, 0.18f + color * 0.15f, sr);
            dr = toneFilter(dr, toneZR, 0.18f + color * 0.15f, sr);
        }
        break;

        case 2: { // ANALOG (BBD-style)
            // BBD HF rolloff
            dl = toneFilter(dl, toneZL, 0.12f + color * 0.18f, sr);
            dr = toneFilter(dr, toneZR, 0.12f + color * 0.18f, sr);

            // Envelope‑dependent saturation (compander feel)
            float env = clamp(envFollower * 0.3f, 0.f, 1.f);
            float drive = 0.5f + env * 0.4f;

            dl = tanhf(dl * drive);
            dr = tanhf(dr * drive);

            // Tiny BBD clock bleed (very subtle)
            dl += sinf(wowPhase * 37.f) * 0.0004f;
            dr += sinf(wowPhase * 41.f) * 0.0004f;
        }
        break;

        case 3: { // MODERN (ultra-clean)
            // Clean digital tone shaping
            dl = toneFilter(dl, toneZL, 0.28f + color * 0.25f, sr);
            dr = toneFilter(dr, toneZR, 0.28f + color * 0.25f, sr);

            // Transient softening (removes harsh spikes)
            dl = dl - 0.02f * (dl - d1L);
            dr = dr - 0.02f * (dr - d1R);

            d1L = dl;
            d1R = dr;

            // Subtle stereo widening
            float widen = 0.003f;
            float mid   = 0.5f * (dl + dr);
            float side  = 0.5f * (dl - dr) * (1.f + widen);
            dl = mid + side;
            dr = mid - side;
        }
        break;

        case 4: { // PING-PONG (enhanced)
            // Swap channels
            std::swap(dl, dr);

            // Width enhancement
            float widen = 0.15f + width * 0.3f;
            float mid   = 0.5f * (dl + dr);
            float side  = 0.5f * (dl - dr) * widen;
            dl = mid + side;
            dr = mid - side;

            // Alternating tone (L brighter, R darker)
            dl = toneFilter(dl, toneZL, 0.30f + color * 0.2f, sr);
            dr = toneFilter(dr, toneZR, 0.15f + color * 0.1f, sr);
        }
        break;

        case 5: { // DYNAMIC (finally implemented)
            float env = clamp(envFollower * 0.25f, 0.f, 1.f);

            // Dynamic feedback reduction (fixed amount: DUCK is applied to every mode below)
            float dyn = 1.f - env * 0.5f;
            dl *= dyn;
            dr *= dyn;

            // Dynamic tone brightening
            float dynCut = 0.20f + color * 0.25f + env * 0.15f;
            dl = toneFilter(dl, toneZL, dynCut, sr);
            dr = toneFilter(dr, toneZR, dynCut, sr);

            // Dynamic saturation
            float drive = 0.6f + env * 0.5f;
            dl = tanhf(dl * drive);
            dr = tanhf(dr * drive);
        }
        break;
case 6: { // SPACE (lush, evolving, cinematic)
    // --- Dual diffusion for reverb-like smear ---
    float smear1 = 0.45f + color * 0.25f;   // early reflections
    float smear2 = 0.30f + color * 0.20f;   // late tail
    dl = diffuse(dl, smearZL, smear1);
    dr = diffuse(dr, smearZR, smear1);
    dl = diffuse(dl, smearZL, smear2);
    dr = diffuse(dr, smearZR, smear2);

    // --- Slow modulation (cloud-like movement) ---
    float slowMod = sinf(wowPhase * 0.25f) * 0.015f;   // ±1.5%
    float fastMod = sinf(flutterPhase * 0.15f) * 0.005f; // ±0.5%
    float mod = slowMod + fastMod;

    dl += dl * mod;
    dr -= dr * mod;   // opposite phase for stereo motion

    // --- Soft bloom saturation (keeps it warm, not harsh) ---
    dl = tanhf(dl * 0.55f);
    dr = tanhf(dr * 0.55f);

    // --- Depth tone shaping (darker = deeper space) ---
    float cut = 0.10f + color * 0.20f;
    dl = toneFilter(dl, toneZL, cut, sr);
    dr = toneFilter(dr, toneZR, cut, sr);

    // --- Stereo warp (big ambient field) ---
    float widen = 0.20f + width * 0.35f;  // 20–55%
    float mid   = 0.5f * (dl + dr);
    float side  = 0.5f * (dl - dr) * (1.f + widen);
    dl = mid + side;
    dr = mid - side;

    // --- Air shimmer (no pitch shift, just psychoacoustic lift) ---
    float air = 0.002f + color * 0.0035f;
    dl += dl * air;
    dr += dr * air;
}
break;

case 7: { // TRUE REVERSE (actual backwards playback)
    // Each head takes the chunk recorded just before it started (chunk = TIME) and plays it backwards:
    // it reads anchor - phase while the writer moves on, so it runs back through the buffer.
    // Two heads half a chunk apart, with sin^2 windows that sum to 1, hide the jump at each chunk start.
    // A head reaches up to 2 chunks behind the writer, so a chunk is at most half the buffer.
    float chunk = clamp(delaySamples, 64.f, maxDelay * 0.5f - 4.f);
    revPhase += 1.f;
    if (revPhase >= chunk) revPhase = fmodf(revPhase, chunk);
    float phA = revPhase, phB = revPhase + chunk * 0.5f;
    if (phB >= chunk) phB -= chunk;
    if (phA < 1.f) revAnchorA = writePos;
    if (phB < 1.f) revAnchorB = writePos;

    auto at = [&](const std::vector<float>& buf, int anchor, float ph) {
        int i = anchor - (int)ph;
        while (i < 0) i += maxDelayI;
        return buf[i % maxDelayI];
    };
    float wA = sinf((float)M_PI * phA / chunk); wA *= wA;
    float wB = 1.f - wA;
    dl = at(delayBufferL, revAnchorA, phA) * wA + at(delayBufferL, revAnchorB, phB) * wB;
    dr = at(delayBufferR, revAnchorA, phA) * wA + at(delayBufferR, revAnchorB, phB) * wB;

    // Optional: tiny modulation for life
    float revMod = (random::uniform() - 0.5f) * 0.002f;
    dl += dl * revMod;
    dr -= dr * revMod;

    // Tone shaping (reverse tends to be dull)
    float tilt = 0.15f + color * 0.20f;
    dl = toneFilter(dl, toneZL, tilt, sr);
    dr = toneFilter(dr, toneZR, tilt, sr);

    // Soft saturation
    dl = tanhf(dl * 0.7f);
    dr = tanhf(dr * 0.7f);

    // Stereo widening
    float widen = 0.005f;
    float mid   = 0.5f * (dl + dr);
    float side  = 0.5f * (dl - dr) * (1.f + widen);
    dl = mid + side;
    dr = mid - side;
}
break;


    }

    float inMono = 0.5f * (fabsf(inL) + fabsf(inR));
    float envAttack  = 0.01f;
    float envRelease = 0.2f;
    float envCoeff   = (inMono > envFollower) ? envAttack : envRelease;
    envFollower += (inMono - envFollower) * envCoeff;

    float duckGain = 1.f - duckAmt * clamp(envFollower, 0.f, 1.f);
    dl *= duckGain;
    dr *= duckGain;

    bool freeze = (fb > 0.98f && resetPressed);
    float fbSafe = freeze ? 0.999f : fb;

    float fbL = dl * fbSafe;
    float fbR = dr * fbSafe;

    fbL = fbLowpass(fbL, fbZL, 0.15f);
    fbR = fbLowpass(fbR, fbZR, 0.15f);

    fbL = softSat(fbL);
    fbR = softSat(fbR);

    float writeL = freeze ? dl : (inL + fbL);
    float writeR = freeze ? dr : (inR + fbR);

    writeL = clamp(writeL, -6.f, 6.f);
    writeR = clamp(writeR, -6.f, 6.f);

    delayBufferL[writePos] = writeL;
    delayBufferR[writePos] = writeR;

    float mid  = (dl + dr) * 0.5f;
    float side = (dl - dr) * 0.5f * width;

    float wetL = mid + side;
    float wetR = mid - side;

    float outL = wetL * mix + inL * (1.f - mix);
    float outR = wetR * mix + inR * (1.f - mix);

    outL = clamp(outL, -5.f, 5.f);
    outR = clamp(outR, -5.f, 5.f);

    // MIX is the only dry/wet: the old Pre/Post trim (a second dry/wet after it) is gone from the panel
    outputs[LEFT_OUTPUT].setVoltage(outL);
    outputs[RIGHT_OUTPUT].setVoltage(outR);
    outputs[SIDE_OUT_OUTPUT].setVoltage(side);

    lights[SYNC_LIGHT].setBrightness(params[SYNC_PARAM].getValue());
    lights[RESET_LIGHT].setBrightness(resetPressed);

    lights[TIME_LIGHT].setBrightness(time);
    lights[FEEDBACK_LIGHT].setBrightness(fb);
    lights[MIX_LIGHT].setBrightness(mix);
    lights[COLOR_LIGHT].setBrightness(colorModeOn ? color : 0.f);
    lights[MOD_DEPTH_LIGHT].setBrightness(modModeOn ? modDepth : 0.f);
    lights[MOD_RATE_LIGHT].setBrightness(modModeOn ? modRate : 0.f);
    lights[DUCKING_LIGHT].setBrightness(duckModeOn ? duckAmt : 0.f);
    lights[WIDTH_LIGHT].setBrightness(width);

    lights[DELAY_TYPE_LIGHT].setBrightness(
        params[DELAY_TYPE_BUTTON_PARAM].getValue() > 0.5f ? 1.f : 0.f
    );

    setParamLED(TIME_CV_LED_RED,     time);
    setParamLED(FEEDBACK_CV_LED_RED, fb);
    setParamLED(MIX_CV_LED_RED,      mix);

    setParamLED(COLOR_CV_TINY_RED,     colorActive ? color : 0.f);
    setParamLED(MOD_DEPTH_CV_TINY_RED, modDepthActive ? modDepth : 0.f);
    setParamLED(MOD_RATE_CV_TINY_RED,  modRateActive ? modRate : 0.f);

    setParamLED(DUCKING_CV_TINY_RED, duckingActive ? duckAmt : 0.f);
    setParamLED(WIDTH_CV_TINY_RED,   width);

    setSignalLED(CLOCK_TINY_RED, clockActive ? clk : 0.f);

    setSignalLED(IN_L_TINY_RED,  inLActive ? inL : 0.f);
    setSignalLED(IN_R_TINY_RED,  inRActive ? inR : 0.f);
    setSignalLED(OUT_L_TINY_RED, outActive ? outL : 0.f);
    setSignalLED(OUT_R_TINY_RED, outActive ? outR : 0.f);
    setSignalLED(SIDE_TINY_RED,  outActive ? side : 0.f);
}
// -----------------------------------------------------------------------------
// ORIGINAL HELPERS (KEPT FOR MODES / COMPAT)
// -----------------------------------------------------------------------------
float SynTHBloKFXModule::saturateSoft(float x, float drive) {
    float k = 1.f + drive * 3.f;
    float y = tanhf(x * k);
    return y * 0.9f;
}

float SynTHBloKFXModule::toneFilter(float x, float& z, float normCut, float sr) {
    float a = clamp(normCut * 0.45f, 0.001f, 0.49f);
    float out = z + a * (x - z);
    z = out;
    return out;
}

float SynTHBloKFXModule::smearDiffusion(float x, float& z, float amt) {
    float a = amt * 0.35f;
    float y = x + a * (x - z);
    z = y;
    return y;
}

float SynTHBloKFXModule::reverseCrossfade(float a, float b, float t) {
    float w = 0.5f - 0.5f * cosf(t * M_PI);
    return a * (1.f - w) + b * w;
}

void SynTHBloKFXModule::pingPongMix(float& dl, float& dr, float width) {
    float mid  = (dl + dr) * 0.5f;
    float side = (dl - dr) * 0.5f * width;
    dl = mid + side;
    dr = mid - side;
}

// -----------------------------------------------------------------------------
// MODEL
// -----------------------------------------------------------------------------
Model* modelSynTHBloKFX =
    createModel<SynTHBloKFXModule, SynTHBloKFXWidget>("SynTHBloKFX");
