// ============================================================================
// SynTHBloK GRAIN-FORGE  (shuffle / glitch / robot voice / pulse / warp delay FX chain)
//  IN -> SHUFFLE -> GLITCH -> ROBOT -> PULSE -> WARP DELAY -> OUT, each stage with ON and its own mix.
//  SHUFFLE rearranges the clean beat first, GLITCH breaks it up, ROBOT voices it, PULSE gates and
//  filters it in time, and WARP DELAY echoes it. The delay's feedback can loop back through ROBOT and
//  PULSE (ROUTE), so repeats get "sung" and filtered as they cycle. Clock-locked (120 BPM internal).
// ============================================================================
#include "SynTHBloKGrainForge.hpp"

namespace {

struct SynTHBloKGrainForgeWidget : ModuleWidget {
    SynTHBloKGrainForgeWidget(SynTHBloKGrainForge* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKGrainForge.svg")));
        using namespace rack::componentlibrary;
        const float PW = 203.2f;   // 40 HP
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(PW - 3.5f, 3.5f))));
        GFTitle* title = createWidget<GFTitle>(mm2px(Vec(8.5f, 0.f)));
        title->box.size = mm2px(Vec(60.f, 7.f));
        addChild(title);

        gfAddPanel(this, module, 0.f, true);
    }
};

} // namespace

Model* modelSynTHBloKGrainForge = createModel<SynTHBloKGrainForge, SynTHBloKGrainForgeWidget>("GrainForge");
