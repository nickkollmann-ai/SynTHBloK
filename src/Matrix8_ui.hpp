#pragma once
// Shared JAM constants and panel widgets for SynTHBloK MATRIX-8 and MATRIX FORGE.
#include "plugin.hpp"
#include <cmath>
#include <string>

namespace {

// JAM Indexed-palette values matching the panel's yellow -> deep dark red colours
// (each JAM hue is 4 values dim -> bright: red 4-7, orange 8-11, light orange 12-15,
// warm yellow 16-19, yellow 20-23). Index = pattern 1..8 / strip bank A..H.
static const int kJamBright[8] = {23, 19, 15, 11, 7, 7, 6, 7};   // 8 uses full red so it can be seen
static const int kJamDim[8] = {21, 17, 13, 9, 5, 4, 4, 4};
static const int kJamWhite = 71;

// JAM side and bottom buttons ("JAM Template 4 Rack" numbers). Each one's light is driven
// by sending its CC back: 127 = on, 0 = off.
enum JamButtonCC {
    CC_SONG = 16, CC_CLEAR = 19, CC_DUPLICATE = 20,
    CC_UP = 21, CC_LEFT = 22, CC_RIGHT = 23, CC_DOWN = 24,
    CC_PLAY = 112, CC_REC = 113, CC_PREV = 114, CC_NEXT = 115,
    CC_TEMPO = 116, CC_GRID = 117, CC_SOLO = 118, CC_MUTE = 119
};
static const int kBtnCC[] = {CC_SONG, CC_CLEAR, CC_DUPLICATE, CC_UP, CC_LEFT, CC_RIGHT, CC_DOWN,
                             CC_PLAY, CC_REC, CC_PREV, CC_NEXT, CC_TEMPO, CC_GRID, CC_SOLO, CC_MUTE};
static const int kNumBtn = sizeof(kBtnCC) / sizeof(kBtnCC[0]);
// "JAM Template 1 Live" (the Ableton template) sends the bottom row as Mackie notes instead:
// {note, the CC it stands for}. Its grid sits on page 1 (notes 0-63), so these never clash there.
static const int kBtnNote[][2] = {{91, CC_PREV}, {92, CC_NEXT}, {93, CC_TEMPO}, {94, CC_PLAY}, {95, CC_REC}};
static int btnNoteFor(int cc) {
    for (auto& n : kBtnNote) if (n[1] == cc) return n[0];
    return -1;
}
// JAM knob: turning sends CC 42..45 (one per MST/GRP/IN1/CUE page), push sends CC 46.
// The JAM's level meter lights follow the same CC, so sending the value back shows it there.
static const int kKnobFirstCC = 42, kKnobLastCC = 45, kKnobPushCC = 46;
static const int kKnobLed = 88 + kNumBtn;
static const int kNumLed = kKnobLed + 1;   // 0..63 pads, 64..71 scenes, 72..79 strips, 80..87 groups, buttons, knob

// Yellow -> deep dark red, one colour per pattern 1..8 and per strip bank A..H
// (same order as the JAM scene row).
static const NVGcolor kPatColor[8] = {
    nvgRGB(0xff, 0xd7, 0x14), nvgRGB(0xff, 0xb0, 0x00), nvgRGB(0xff, 0x8c, 0x00), nvgRGB(0xff, 0x6a, 0x00),
    nvgRGB(0xf0, 0x40, 0x00), nvgRGB(0xcc, 0x1a, 0x00), nvgRGB(0x99, 0x00, 0x00), nvgRGB(0x5c, 0x00, 0x00)};
// On state (selected button, pad hit, strip fill): same as kPatColor, except 7 and 8 use
// shades of page 6's red instead of the deep reds, bright enough to read when lit.
static const NVGcolor kPatOnColor[8] = {
    nvgRGB(0xff, 0xd7, 0x14), nvgRGB(0xff, 0xb0, 0x00), nvgRGB(0xff, 0x8c, 0x00), nvgRGB(0xff, 0x6a, 0x00),
    nvgRGB(0xf0, 0x40, 0x00), nvgRGB(0xcc, 0x1a, 0x00), nvgRGB(0xb3, 0x17, 0x00), nvgRGB(0xc2, 0x1a, 0x00)};
static const NVGcolor kIdle = nvgRGB(0x1c, 0x1c, 0x1c);

// Selected = filled in its on colour with a dark label; others = outline and label in its colour.
static void drawColorButton(const widget::Widget::DrawArgs& args, Vec size, int i, bool sel, char ch, float fontSize) {
    NVGcolor col = sel ? kPatOnColor[i] : kPatColor[i];
    nvgBeginPath(args.vg);
    nvgRoundedRect(args.vg, 0.f, 0.f, size.x, size.y, 1.4f);
    nvgFillColor(args.vg, sel ? col : kIdle);
    nvgFill(args.vg);
    nvgStrokeColor(args.vg, col);
    nvgStrokeWidth(args.vg, sel ? 1.2f : 0.8f);
    nvgStroke(args.vg);
    std::shared_ptr<window::Font> font = APP->window->uiFont;
    if (!ch || !font) return;
    char label[2] = {ch, 0};
    nvgFontFaceId(args.vg, font->handle);
    nvgFontSize(args.vg, fontSize);
    nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(args.vg, sel ? nvgRGB(0x10, 0x10, 0x10) : col);
    nvgText(args.vg, size.x * 0.5f, size.y * 0.5f, label, NULL);
}

// RAND, after pattern 8 over the jack column: a momentary button shaped like the pattern buttons,
// outlined in the FX button's green, filled green while held (1 while held, so a SchmittTrigger sees it).
struct JRandBtn : app::ParamWidget {
    void drawLayer(const DrawArgs& args, int layer) override {
        ParamWidget::drawLayer(args, layer);
        if (layer != 1) return;
        ParamQuantity* pq = getParamQuantity();
        bool held = pq && pq->getValue() > 0.5f;
        NVGcolor g = nvgRGB(120, 230, 90);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.4f);
        nvgFillColor(args.vg, held ? g : kIdle);
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, g);
        nvgStrokeWidth(args.vg, held ? 1.2f : 0.8f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 6.f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, held ? nvgRGB(0x10, 0x10, 0x10) : g);
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, "RAND", NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (ParamQuantity* pq = getParamQuantity()) pq->setValue(1.f);
            e.consume(this);
            return;
        }
        ParamWidget::onButton(e);
    }
    void onDragStart(const DragStartEvent& e) override {}
    void onDragEnd(const DragEndEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT)
            if (ParamQuantity* pq = getParamQuantity()) pq->setValue(0.f);
    }
    // centred on the jack column x (mm), level with the pattern buttons at y (mm)
    static JRandBtn* create(float x, float y, Module* module, int param) {
        JRandBtn* b = createParam<JRandBtn>(mm2px(Vec(x - 3.3f, y)), module, param);
        b->box.size = mm2px(Vec(6.6f, 6.6f));
        return b;
    }
};

// MIDI IN and OUT in one display, in the SynTHBloK MIDI + amber style.
// Click the top half to pick the input port, the bottom half for the output port.
struct JMidiDisplay : TransparentWidget {
    midi::Port* in = NULL;
    midi::Port* out = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.f);
        nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, 5.f, H * 0.5f);
        nvgLineTo(args.vg, W - 5.f, H * 0.5f);
        nvgStrokeWidth(args.vg, 0.6f);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 40));
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (font) {
            nvgScissor(args.vg, 0, 0, W, H);
            nvgFontFaceId(args.vg, font->handle);
            drawPort(args, in, "MIDI IN", 0.f, W, H * 0.5f);
            drawPort(args, out, "MIDI OUT", H * 0.5f, W, H * 0.5f);
            nvgResetScissor(args.vg);
        }
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.5f, 0.5f, W - 1.f, H - 1.f, 2.f);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 80));
        nvgStroke(args.vg);
    }
    void drawPort(const DrawArgs& args, midi::Port* port, const char* title, float y, float W, float h) {
        std::string drv = "", dev = "(no device)";
        if (port) {
            if (midi::Driver* d = port->getDriver()) drv = d->getName();
            if (port->getDeviceId() >= 0) dev = port->getDeviceName(port->getDeviceId());
        } else dev = "Maschine Jam - 1";
        NVGcolor dim = nvgRGBA(255, 190, 90, 130);
        nvgFontSize(args.vg, 6.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, dim);
        nvgText(args.vg, 5.f, y + h * 0.3f, title, NULL);
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, W - 5.f, y + h * 0.3f, drv.c_str(), NULL);
        nvgFontSize(args.vg, 8.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGBA(255, 190, 90, 255));
        nvgText(args.vg, 5.f, y + h * 0.68f, dev.c_str(), NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            midi::Port* port = e.pos.y < box.size.y * 0.5f ? in : out;
            if (port) {
                ui::Menu* menu = createMenu();
                menu->addChild(createMenuLabel(port == in ? "MIDI IN port" : "MIDI OUT port"));
                appendMidiMenu(menu, port);
            }
            e.consume(this);
        }
    }
};

// Panel caption drawn by the module (so labels follow the layout), centred on its box.
struct JLabel : TransparentWidget {
    std::string text;
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 7.f);
        nvgTextLetterSpacing(args.vg, 0.6f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(0xbb, 0xbb, 0xbb));
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, text.c_str(), NULL);
    }
};

// LEARN / CLEAR / RUN / RESET: shaped like the page and bank buttons, coloured like the
// SynTHBloK MIDI + display (amber on dark brown, lit = orange, RUN lit = green).
// A trigger at `jack` presses the button (RUN toggles, RESET fires); a dot in the corner
// shows the jack is patched. M = the module (needs RESET_INPUT and resetFlash).
template <class M>
struct JAmberBtn : app::ParamWidget {
    M* jam = NULL;
    const char* label = "";
    bool latch = false, green = false;
    int jack = -1;
    float fontSize = 8.f;
    JAmberBtn() { box.size = mm2px(Vec(10.25f, 6.f)); }
    bool jackPatched() { return jam && jack >= 0 && jam->inputs[jack].isConnected(); }
    bool lit() {
        if (!jam) return green;
        if (jack == M::RESET_INPUT && jam->resetFlash > 0.f) return true;
        ParamQuantity* pq = getParamQuantity();
        return pq && pq->getValue() > 0.5f;
    }
    void draw(const DrawArgs& args) override {
        bool on = lit();
        NVGcolor onFill = green ? nvgRGBA(120, 230, 90, 235) : nvgRGBA(255, 112, 15, 235);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.4f);
        nvgFillColor(args.vg, on ? onFill : nvgRGBA(40, 25, 8, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, on ? 255 : 120));
        nvgStrokeWidth(args.vg, on ? 1.2f : 0.8f);
        nvgStroke(args.vg);
        if (jackPatched()) {
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, box.size.x - 2.6f, 2.6f, 1.3f);
            nvgFillColor(args.vg, on ? nvgRGBA(20, 10, 2, 255) : nvgRGBA(255, 190, 90, 255));
            nvgFill(args.vg);
        }
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? nvgRGBA(20, 10, 2, 255) : nvgRGBA(255, 190, 90, 255));
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, label, NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (ParamQuantity* pq = getParamQuantity()) pq->setValue(latch ? (pq->getValue() > 0.5f ? 0.f : 1.f) : 1.f);
            e.consume(this);
            return;
        }
        ParamWidget::onButton(e);
    }
    void onDragStart(const DragStartEvent& e) override {}
    void onDragEnd(const DragEndEvent& e) override {
        if (!latch && e.button == GLFW_MOUSE_BUTTON_LEFT)
            if (ParamQuantity* pq = getParamQuantity()) pq->setValue(0.f);
    }
};

// Amber value arc around the knob (MIDI + colours), so its position reads like the JAM's meter.
template <class M>
struct JKnobRing : TransparentWidget {
    M* module = NULL;
    int param = M::KNOB_PARAM;
    JKnobRing() { box.size = mm2px(Vec(19.f, 19.f)); }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float v = module ? module->params[param].getValue() : 0.6f;
        float cx = box.size.x * 0.5f, cy = box.size.y * 0.5f, rr = box.size.x * 0.5f - 2.f;
        float a0 = M_PI * 0.75f, a1 = M_PI * 2.25f;
        nvgBeginPath(args.vg);
        nvgArc(args.vg, cx, cy, rr, a0, a1, NVG_CW);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 40));
        nvgStrokeWidth(args.vg, 2.f);
        nvgStroke(args.vg);
        if (v > 0.001f) {
            nvgBeginPath(args.vg);
            nvgArc(args.vg, cx, cy, rr, a0, a0 + (a1 - a0) * v, NVG_CW);
            nvgStrokeColor(args.vg, nvgRGBA(255, 112, 15, 235));
            nvgStrokeWidth(args.vg, 2.f);
            nvgStroke(args.vg);
        }
    }
};

// LOOP column: the JAM cross pad's UP / DOWN (pattern length) on the panel.
// M = the module (needs LEN_PARAM, step and running).
// Arrow buttons step the length; the display shows the looped steps and the playhead.
template <class M>
struct JLoopArrow : OpaqueWidget {
    M* module = NULL;
    int dir = 1;                  // +1 = UP (longer), -1 = DOWN (shorter)
    float flash = 0.f;
    void draw(const DrawArgs& args) override {
        float W = box.size.x, H = box.size.y;
        bool on = flash > 0.f;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.4f);
        nvgFillColor(args.vg, on ? nvgRGBA(255, 112, 15, 235) : nvgRGBA(40, 25, 8, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, on ? 255 : 120));
        nvgStrokeWidth(args.vg, on ? 1.2f : 0.8f);
        nvgStroke(args.vg);
        float cx = W * 0.5f, cy = H * 0.5f, s = std::min(W, H) * 0.22f;
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, cx, cy - dir * s);
        nvgLineTo(args.vg, cx + s * 1.2f, cy + dir * s);
        nvgLineTo(args.vg, cx - s * 1.2f, cy + dir * s);
        nvgClosePath(args.vg);
        nvgFillColor(args.vg, on ? nvgRGBA(20, 10, 2, 255) : nvgRGBA(255, 190, 90, 255));
        nvgFill(args.vg);
        flash = std::max(0.f, flash - 0.1f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) {
                ParamQuantity* pq = module->paramQuantities[M::LEN_PARAM];
                pq->setValue((float)clamp((int)pq->getValue() + dir, 1, 8));
            }
            flash = 1.f;
            e.consume(this);
        }
    }
};

template <class M>
struct JLoopDisplay : OpaqueWidget {
    M* module = NULL;
    int len() { return module ? clamp((int)module->params[M::LEN_PARAM].getValue(), 1, 8) : 6; }
    void draw(const DrawArgs& args) override {
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.f);
        nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        int n = len();
        int step = module ? module->step : 2;
        bool run = module ? module->running : true;
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        float top = 2.f;
        if (font) {
            char b[4]; snprintf(b, sizeof b, "%d", n);
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, 11.f);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            nvgFillColor(args.vg, nvgRGBA(255, 190, 90, 255));
            nvgText(args.vg, W * 0.5f, top, b, NULL);
            top += 12.f;
        }
        // steps 1..8 top to bottom: lit = inside the loop, white = playhead
        float ch = (H - top - 2.f) / 8.f;
        for (int i = 0; i < 8; i++) {
            bool in = i < n, head = run && step == i;
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, 3.f, top + i * ch + 0.6f, W - 6.f, ch - 1.2f, 0.8f);
            nvgFillColor(args.vg, head ? nvgRGBA(255, 255, 255, 235) : in ? nvgRGBA(255, 112, 15, 220) : nvgRGBA(40, 25, 8, 255));
            nvgFill(args.vg);
        }
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.5f, 0.5f, W - 1.f, H - 1.f, 2.f);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 80));
        nvgStroke(args.vg);
    }
    // click a step cell to loop up to it
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            float top = 14.f, ch = (box.size.y - top - 2.f) / 8.f;
            if (module && e.pos.y >= top) {
                int i = clamp((int)((e.pos.y - top) / ch), 0, 7);
                module->paramQuantities[M::LEN_PARAM]->setValue((float)(i + 1));
            }
            e.consume(this);
        }
    }
};

} // namespace
