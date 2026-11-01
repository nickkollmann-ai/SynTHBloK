#include "plugin.hpp"

struct GradientLogoModule : Module {
    enum ParamIds {
        NUM_PARAMS
    };
    enum InputIds {
        AUDIO_IN,
        NUM_INPUTS
    };
    enum OutputIds {
        NUM_OUTPUTS
    };
    enum LightIds {
        NUM_LIGHTS
    };

    float env = 0.f;

    GradientLogoModule() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configInput(AUDIO_IN, "Audio");
    }

    void process(const ProcessArgs& args) override {
        float in = inputs[AUDIO_IN].getVoltage();

        float rect = fabsf(in) / 5.f;
        env = 0.1f * rect + 0.9f * env;
        env = clamp(env, 0.f, 1.f);
    }
};

struct ReactiveLogo : TransparentWidget {
    GradientLogoModule* module = nullptr;

    void draw(const DrawArgs& args) override {
        if (!module)
            return;

        float e = module->env;

        NVGcolor blue   = nvgRGB(0, 120, 255);
        NVGcolor yellow = nvgRGB(255, 220, 0);
        NVGcolor orange = nvgRGB(255, 120, 0);
        NVGcolor red    = nvgRGB(255, 0, 0);

        NVGcolor col;

        if (e < 0.33f) {
            col = nvgLerpRGBA(blue, yellow, e / 0.33f);
        }
        else if (e < 0.66f) {
            col = nvgLerpRGBA(yellow, orange, (e - 0.33f) / 0.33f);
        }
        else {
            col = nvgLerpRGBA(orange, red, (e - 0.66f) / 0.34f);
        }

        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, col);
        nvgFill(args.vg);

        nvgFontSize(args.vg, 32.f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 220));
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, "LiQuiDNicK", nullptr);
    }
};

struct GradientLogoWidget : ModuleWidget {
    GradientLogoWidget(GradientLogoModule* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/GradientLogo.svg")));

        auto logo = new ReactiveLogo();
        logo->module = module;
        logo->box.pos = Vec(0.f, 0.f);
        logo->box.size = Vec(box.size.x, box.size.y);
        addChild(logo);

        addInput(createInputCentered<BlackJack>(
            Vec(box.size.x * 0.5f, box.size.y - 20.f),
            module,
            GradientLogoModule::AUDIO_IN
        ));
    }
};

Model* modelGradientLogo = createModel<GradientLogoModule, GradientLogoWidget>("GradientLogo");
