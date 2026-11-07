#include "plugin.hpp"
#include "SynTHBloK.hpp"
#include "SynTHBloKPoly.hpp"
#include "SynTHBloKStereo.hpp"
#include "SynTHBloKFXModule.hpp"
#include "SynTHBloKMix24.hpp"
#include "QuadCore.hpp"


struct SynTHBloKWidget : ModuleWidget {
    SynTHBloKWidget(SynTHBloK* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/SynTHBloK.svg")));

        using namespace rack::componentlibrary;

        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(138.62f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 125.0f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(138.62f, 125.0f))));

        addQuadControls(this, module, 0.f);
    }
    void appendContextMenu(Menu* menu) override {
        if (auto* m = dynamic_cast<SynTHBloK*>(module)) addQuadMenu(menu, m);
    }
};

Model* modelSynTHBloK = createModel<SynTHBloK, SynTHBloKWidget>("Quad");
