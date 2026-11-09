// SynTHBloK BLANK: a NoiZee ToyZ blank panel for the empty spaces in a rack. Three looks (the BACKDROP rack ears'
// designs): BADGE (the hex Z logo as an enamel badge), BACKLIT (slots and the name lit orange from behind; the light is
// drawn on Rack's light layer, so it glows with the room lights down) and CIRCUIT (the logo's line, nodes and
// lightning Z). Drag the right edge to resize it, 1 to 32 HP (1-2 HP: centred screws, a thin card). No inputs or outputs.
// Ships in the MTX pack and as its own small plugin (singles/SynTHBloK-Blanks), so it draws everything itself.
#include "plugin.hpp"

namespace {

static const char* const kBlankStyle[] = {"BADGE", "BACKLIT", "CIRCUIT"};
static const int kMinHp = 1, kMaxHp = 32, kDefHp = 4;   // 1 and 2 HP: the skinny blanks that fill a single gap

struct SynTHBloKBlank : Module {
    int style = 0, hp = kDefHp;
    bool showName = true;
    int64_t lockedTo = -1;   // a BACKDROP's id: made by its COLLAPSE to fill the 5 HP it left; its size is fixed then
    SynTHBloKBlank() { config(0, 0, 0, 0); }
    json_t* dataToJson() override {
        json_t* r = json_object();
        json_object_set_new(r, "style", json_integer(style));
        json_object_set_new(r, "hp", json_integer(hp));
        json_object_set_new(r, "showName", json_boolean(showName));
        json_object_set_new(r, "lockedTo", json_integer(lockedTo));
        return r;
    }
    void dataFromJson(json_t* r) override {
        if (json_t* v = json_object_get(r, "style")) style = clamp((int) json_integer_value(v), 0, 2);
        if (json_t* v = json_object_get(r, "hp")) hp = clamp((int) json_integer_value(v), kMinHp, kMaxHp);
        if (json_t* v = json_object_get(r, "showName")) showName = json_boolean_value(v);
        if (json_t* v = json_object_get(r, "lockedTo")) lockedTo = json_integer_value(v);
    }
};

const NVGcolor kOrange = nvgRGB(255, 122, 0), kTeal = nvgRGB(0, 255, 170);

// the name, reading bottom to top, ending at yEnd (style 0 plain, 2 with a soft glow behind)
static void blankText(NVGcontext* vg, const char* t, float cx, float yEnd, float size, float spacing, NVGcolor c, int style) {
    std::shared_ptr<window::Font> font = APP->window->uiFont;
    if (!font) return;
    nvgSave(vg);
    nvgTranslate(vg, cx, yEnd); nvgRotate(vg, -(float) M_PI * 0.5f);
    nvgFontFaceId(vg, font->handle); nvgFontSize(vg, size); nvgTextLetterSpacing(vg, spacing);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    if (style == 2) { nvgFontBlur(vg, 6.f); nvgFillColor(vg, nvgTransRGBA(c, 160)); nvgText(vg, 0, 0, t, NULL); nvgFontBlur(vg, 0.f); }
    nvgFillColor(vg, c); nvgText(vg, 0, 0, t, NULL);
    nvgRestore(vg);
}

// RESIZE: drag the right edge (as on Rack's own blank); snaps to whole HP and never overlaps a neighbour
struct BlankResize : OpaqueWidget {
    SynTHBloKBlank* module = NULL;
    Vec dragPos; math::Rect startBox;
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) e.consume(this);
    }
    void onDragStart(const DragStartEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
        dragPos = APP->scene->rack->getMousePos();
        if (ModuleWidget* mw = getAncestorOfType<ModuleWidget>()) startBox = mw->box;
    }
    void onDragMove(const DragMoveEvent& e) override {
        ModuleWidget* mw = getAncestorOfType<ModuleWidget>();
        if (!mw || !module || module->lockedTo >= 0) return;   // locked to a BACKDROP: its size is the void's
        float dx = APP->scene->rack->getMousePos().x - dragPos.x;
        math::Rect nb = startBox, old = mw->box;
        nb.size.x = clamp(std::round((startBox.size.x + dx) / RACK_GRID_WIDTH), (float) kMinHp, (float) kMaxHp) * RACK_GRID_WIDTH;
        mw->box = nb;
        if (!APP->scene->rack->requestModulePos(mw, nb.pos)) mw->box = old;
        module->hp = (int) std::round(mw->box.size.x / RACK_GRID_WIDTH);
    }
    void draw(const DrawArgs& args) override {}
};

struct BlankWidget : ModuleWidget {
    BlankResize* handle = NULL;
    widget::Widget* screws[4] = {};
    BlankWidget(SynTHBloKBlank* module) {
        setModule(module);
        box.size = Vec(RACK_GRID_WIDTH * (module ? module->hp : kDefHp), RACK_GRID_HEIGHT);
        handle = new BlankResize;
        handle->module = module;
        handle->box.size = Vec(RACK_GRID_WIDTH * 0.75f, RACK_GRID_HEIGHT);
        addChild(handle);
        // four screws, 3.5 mm in from each corner, as on every SynTHBloK module (the right pair follows the width)
        for (int k = 0; k < 4; k++) {
            screws[k] = createWidgetCentered<SocketScrew>(mm2px(Vec(3.5f, k < 2 ? 3.5f : 125.f)));
            addChild(screws[k]);
        }
        place();
    }
    void place() {
        float w = box.size.x;
        bool skinny = w < RACK_GRID_WIDTH * 2.5f;   // 1-2 HP: one screw top and bottom, centred (as on real skinny blanks)
        handle->box.size.x = skinny ? RACK_GRID_WIDTH * 0.4f : RACK_GRID_WIDTH * 0.75f;
        handle->box.pos = Vec(w - handle->box.size.x, 0);
        for (int k = 0; k < 4; k++) {
            float cx = skinny ? w * 0.5f : (k & 1) ? w - mm2px(3.5f) : mm2px(3.5f);
            screws[k]->box.pos.x = cx - screws[k]->box.size.x * 0.5f;
            screws[k]->visible = !(skinny && (k & 1));
        }
    }
    void step() override {
        SynTHBloKBlank* m = dynamic_cast<SynTHBloKBlank*>(module);
        if (m && m->lockedTo >= 0 && !APP->engine->getModule(m->lockedTo)) m->lockedTo = -1;   // its BACKDROP is gone: free
        if (m) {
            float w = RACK_GRID_WIDTH * clamp(m->hp, kMinHp, kMaxHp);
            if (box.size.x != w) { box.size.x = w; place(); }
        }
        ModuleWidget::step();
    }
    int styleNow() { SynTHBloKBlank* m = dynamic_cast<SynTHBloKBlank*>(module); return m ? clamp(m->style, 0, 2) : 0; }
    bool nameNow() { SynTHBloKBlank* m = dynamic_cast<SynTHBloKBlank*>(module); return m ? m->showName : true; }

    // the face (everything but the backlit light)
    void draw(const DrawArgs& args) override {
        NVGcontext* vg = args.vg;
        float w = box.size.x, h = box.size.y, cx = w * 0.5f;
        int st = styleNow();
        // the same face as every SynTHBloK module (res/*.svg): #111 with a soft top light, the black header strip with
        // its orange rule, the dark card, and the grey NoiZee ToyZ footer
        nvgBeginPath(vg); nvgRect(vg, 0, 0, w, h); nvgFillColor(vg, nvgRGB(0x11, 0x11, 0x11)); nvgFill(vg);
        nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 0, h, nvgRGBA(255, 255, 255, 9), nvgRGBA(0, 0, 0, 41))); nvgFill(vg);
        nvgBeginPath(vg); nvgRect(vg, 0, 0, w, mm2px(7.f)); nvgFillColor(vg, nvgRGB(8, 8, 8)); nvgFill(vg);
        nvgBeginPath(vg); nvgRect(vg, 0, mm2px(7.f) - 0.45f, w, 0.9f); nvgFillColor(vg, nvgRGBA(255, 122, 0, 190)); nvgFill(vg);
        float ci = w < mm2px(12.f) ? mm2px(0.6f) : mm2px(2.5f);   // the card's inset (thin on the skinny blanks)
        nvgBeginPath(vg); nvgRoundedRect(vg, ci, mm2px(9.f), w - 2.f * ci, mm2px(116.5f), mm2px(w < mm2px(12.f) ? 0.6f : 1.2f));
        nvgFillColor(vg, nvgRGB(0x16, 0x16, 0x16)); nvgFill(vg);
        nvgStrokeColor(vg, nvgRGB(0x2c, 0x2c, 0x2c)); nvgStrokeWidth(vg, mm2px(0.25f)); nvgStroke(vg);
        if (std::shared_ptr<window::Font> font = APP->window->uiFont) {
            nvgFontFaceId(vg, font->handle); nvgFontSize(vg, mm2px(1.944f) * 1.4f); nvgTextLetterSpacing(vg, 0.f);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
            // the footer fits between the bottom screws: the full name from 7 HP, NZT from 4 HP, none at 3 HP
            const char* foot = w >= mm2px(35.f) ? "NoiZee ToyZ" : w >= mm2px(20.f) ? "NZT" : NULL;
            nvgFillColor(vg, nvgRGB(0x80, 0x80, 0x80)); if (foot) nvgText(vg, cx, mm2px(127.9f), foot, NULL);
        }
        float top = mm2px(12.f), bot = mm2px(121.f), span = bot - top;
        float txt = std::min(w * 0.55f, 15.f);   // the name's size follows the width, within reason
        if (st == 0) {   // BADGE
            float r = std::min(w * 0.32f, 34.f), by = top + span * 0.32f;
            auto hexPath = [&](float dx, float dy) {
                nvgBeginPath(vg);
                for (int k = 0; k < 6; k++) {
                    float a = (float) M_PI / 6.f + k * (float) M_PI / 3.f;
                    float hx = cx + dx + r * std::cos(a), hy = by + dy + r * std::sin(a);
                    if (k) nvgLineTo(vg, hx, hy); else nvgMoveTo(vg, hx, hy);
                }
                nvgClosePath(vg);
            };
            hexPath(1.5f, 2.5f); nvgFillColor(vg, nvgRGBA(0, 0, 0, 120)); nvgFill(vg);
            hexPath(0.f, 0.f); nvgFillColor(vg, nvgRGB(10, 10, 12)); nvgFill(vg);
            nvgStrokeColor(vg, kTeal); nvgStrokeWidth(vg, std::max(1.5f, r * 0.1f)); nvgStroke(vg);
            nvgBeginPath(vg); nvgCircle(vg, cx, by - r * 0.39f, r * 0.16f); nvgStrokeWidth(vg, std::max(1.2f, r * 0.07f)); nvgStroke(vg);
            nvgBeginPath(vg); nvgMoveTo(vg, cx - r * 0.28f, by - r * 0.12f); nvgLineTo(vg, cx + r * 0.28f, by - r * 0.12f);
            nvgLineTo(vg, cx - r * 0.28f, by + r * 0.2f); nvgLineTo(vg, cx + r * 0.28f, by + r * 0.2f);
            nvgStrokeColor(vg, kOrange); nvgStrokeWidth(vg, std::max(1.2f, r * 0.08f)); nvgStroke(vg);
            nvgBeginPath(vg); nvgCircle(vg, cx, by + r * 0.46f, r * 0.13f); nvgFillColor(vg, kTeal); nvgFill(vg);
            if (nameNow()) blankText(vg, "NOIZEE TOYZ", cx, bot - 8.f, std::min(txt * 0.8f, 12.f), 4.f, nvgRGB(205, 205, 210), 0);
        }
        else if (st == 1) {   // BACKLIT: the slots and letters, unlit (the light comes in drawLayer)
            float sw = w * 0.5f, end = top + span * 0.45f;
            for (float yy = top + 6.f; yy + 5.f < end; yy += 12.f) {
                nvgBeginPath(vg); nvgRoundedRect(vg, cx - sw * 0.5f, yy, sw, 5.f, 2.5f); nvgFillColor(vg, nvgRGB(70, 32, 6)); nvgFill(vg);
            }
            if (nameNow()) blankText(vg, "NoiZee ToyZ", cx, bot - 8.f, std::min(txt * 1.2f, 18.f), 1.5f, nvgRGB(70, 32, 6), 0);
        }
        else {   // CIRCUIT
            float ly0 = top + 4.f, ly1 = top + span * 0.32f;
            nvgBeginPath(vg); nvgMoveTo(vg, cx, ly0); nvgLineTo(vg, cx, ly1); nvgStrokeColor(vg, kTeal); nvgStrokeWidth(vg, 2.f); nvgStroke(vg);
            for (float ny : {ly0 + span * 0.05f, ly0 + span * 0.17f}) {
                nvgBeginPath(vg); nvgCircle(vg, cx, ny, 5.f); nvgFillColor(vg, nvgRGB(12, 14, 14)); nvgFill(vg);
                nvgStrokeColor(vg, kTeal); nvgStrokeWidth(vg, 2.f); nvgStroke(vg);
            }
            float zs = std::min(14.f, w * 0.3f);
            nvgBeginPath(vg); nvgMoveTo(vg, cx, ly1); nvgLineTo(vg, cx - zs, ly1 + zs * 0.55f); nvgLineTo(vg, cx + zs, ly1 + zs * 0.55f); nvgLineTo(vg, cx, ly1 + zs * 1.1f);
            nvgStrokeColor(vg, kOrange); nvgStrokeWidth(vg, 3.f); nvgLineJoin(vg, NVG_MITER); nvgStroke(vg);
            if (nameNow()) blankText(vg, "NOIZEE TOYZ", cx, bot - 8.f, std::min(txt, 14.f), 5.f, nvgRGB(235, 235, 235), 0);
        }
        ModuleWidget::draw(args);
    }
    // BACKLIT's light, on Rack's light layer so it glows when the room lights are down
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1 && styleNow() == 1) {
            NVGcontext* vg = args.vg;
            float w = box.size.x, cx = w * 0.5f, top = mm2px(12.f), bot = mm2px(121.f), span = bot - top;
            float txt = std::min(w * 0.55f, 15.f);
            NVGcolor lit = nvgRGB(255, 146, 36);
            float sw = w * 0.5f, end = top + span * 0.45f;
            for (float yy = top + 6.f; yy + 5.f < end; yy += 12.f) {
                nvgBeginPath(vg); nvgRoundedRect(vg, cx - sw * 0.5f - 5.f, yy - 5.f, sw + 10.f, 15.f, 7.f);
                nvgFillPaint(vg, nvgBoxGradient(vg, cx - sw * 0.5f, yy, sw, 5.f, 2.5f, 9.f, nvgRGBA(255, 110, 20, 90), nvgRGBA(0, 0, 0, 0))); nvgFill(vg);
                nvgBeginPath(vg); nvgRoundedRect(vg, cx - sw * 0.5f, yy, sw, 5.f, 2.5f); nvgFillColor(vg, lit); nvgFill(vg);
            }
            if (nameNow()) blankText(vg, "NoiZee ToyZ", cx, bot - 8.f, std::min(txt * 1.2f, 18.f), 1.5f, lit, 2);
        }
        ModuleWidget::drawLayer(args, layer);
    }
    void appendContextMenu(Menu* menu) override {
        SynTHBloKBlank* m = dynamic_cast<SynTHBloKBlank*>(module);
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createIndexPtrSubmenuItem("Look", {kBlankStyle[0], kBlankStyle[1], kBlankStyle[2]}, &m->style));
        menu->addChild(createBoolPtrMenuItem("Show the NoiZee ToyZ name", "", &m->showName));
        if (m->lockedTo >= 0) {
            menu->addChild(createMenuLabel(string::f("Width: %d HP (locked: it fills its BACKDROP's collapse)", m->hp)));
            return;
        }
        menu->addChild(createSubmenuItem("Width", string::f("%d HP", m->hp), [=](Menu* sub) {
            for (int w : {1, 2, 3, 4, 6, 8, 10, 12, 16, 20, 24, 32})
                sub->addChild(createCheckMenuItem(string::f("%d HP", w), "", [=]() { return m->hp == w; }, [=]() {
                    ModuleWidget* mw = this;
                    math::Rect old = mw->box, nb = old;
                    nb.size.x = w * RACK_GRID_WIDTH;
                    mw->box = nb;
                    if (APP->scene->rack->requestModulePos(mw, nb.pos)) m->hp = w;
                    mw->box = old;   // step() applies the new width
                }));
        }));
        menu->addChild(createMenuLabel("Tip: drag the right edge to resize"));
    }
};

}  // namespace

Model* modelSynTHBloKBlank = createModel<SynTHBloKBlank, BlankWidget>("Blank");
