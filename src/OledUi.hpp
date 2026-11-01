#pragma once
// SynTHBloK OLED look (first used on MATRIX-DUO): amber text on dark brown, thin amber outline,
// faint divider lines, lit cells orange (RUN: green) with dark ink. Reuse these so every module's
// displays and display buttons match.
//
// Design rules:
//  - a display is oled::box() (2 px corners, 0.8 px outline at amber alpha 80), inset 1.5 mm from a
//    section's edge; it draws in the lit layer (drawLayer 1), so it glows in a dark room
//  - info OLED: one line per item, rows kRow (4.4 mm) tall, divided by oled::divider(); dim 6 px names,
//    amber 7.5 px values (8 px for the headline, e.g. BPM); text inset 4 px; clickable values step
//    forward on click / scroll up, back on right-click / scroll down
//  - MIDI rows: oled::midiLine() ("IN" / "OUT" + device), click = oled::midiMenu()
//  - buttons inside a display: oled::Cell ParamWidgets laid over the box (so tooltips and param menus
//    still work), 4.4 mm tall, 0.9 mm row gap, 1 mm column gap, 2 mm side padding, 6.5 px labels
//    (6 px in tight stacks). A button-only panel is oled::Box with Cells on it; size it to the
//    controls beside it (e.g. as tall as a knob's ring)
#include "plugin.hpp"
#include <string>

namespace oled {

static const float kRow = 4.4f;      // info line height, mm
static const float kCellH = 4.4f;    // button cell height, mm

inline NVGcolor amber(int a = 255) { return nvgRGBA(255, 190, 90, a); }
inline NVGcolor dim() { return nvgRGBA(255, 190, 90, 130); }
inline NVGcolor green() { return nvgRGBA(120, 230, 90, 255); }
inline NVGcolor outline(int a = 80) { return nvgRGBA(255, 180, 90, a); }
inline NVGcolor litFill() { return nvgRGBA(255, 112, 15, 235); }
inline NVGcolor offFill() { return nvgRGBA(40, 25, 8, 255); }
inline NVGcolor ink() { return nvgRGBA(20, 10, 2, 255); }

// The display body.
inline void box(const widget::Widget::DrawArgs& args, float W, float H, NVGcolor stroke = outline(), float strokeW = 0.8f) {
    nvgBeginPath(args.vg);
    nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.f);
    nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
    nvgFill(args.vg);
    nvgStrokeWidth(args.vg, strokeW);
    nvgStrokeColor(args.vg, stroke);
    nvgStroke(args.vg);
}

// Faint divider line (add to the current path, stroke with dividerStroke()).
inline void divider(const widget::Widget::DrawArgs& args, float x0, float y0, float x1, float y1) {
    nvgMoveTo(args.vg, x0, y0);
    nvgLineTo(args.vg, x1, y1);
}
inline void dividerStroke(const widget::Widget::DrawArgs& args) {
    nvgStrokeColor(args.vg, outline(40));
    nvgStrokeWidth(args.vg, 0.6f);
    nvgStroke(args.vg);
}

// One MIDI line: dim title, then the device (the preview name in the module browser). Font must be set.
inline void midiLine(const widget::Widget::DrawArgs& args, midi::Port* port, const char* title, float y,
                     const char* preview = "Maschine Jam - 1") {
    std::string dev = "(no device)";
    if (port) { if (port->getDeviceId() >= 0) dev = port->getDeviceName(port->getDeviceId()); }
    else dev = preview;
    float yc = y + mm2px(kRow * 0.5f);
    nvgFontSize(args.vg, 6.f);
    nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(args.vg, dim());
    nvgText(args.vg, 4.f, yc, title, NULL);
    nvgFontSize(args.vg, 7.5f);
    nvgFillColor(args.vg, amber());
    nvgText(args.vg, mm2px(6.5f), yc, dev.c_str(), NULL);
}

// The port menu a MIDI line opens.
inline void midiMenu(midi::Port* port, const char* title) {
    if (!port) return;
    ui::Menu* menu = createMenu();
    menu->addChild(createMenuLabel(title));
    appendMidiMenu(menu, port);
}

// A bare display for Cells to sit on.
struct Box : TransparentWidget {
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1) oled::box(args, box.size.x, box.size.y);
        Widget::drawLayer(args, layer);
    }
};

// A button cell drawn in the lit layer. setTo >= 0: a press sets that value (lit while it holds it:
// view / mode selectors); else latch toggles, or momentary (1 while held, so SchmittTriggers see a
// press). green = lit green (RUN). jack: a patched input shows a dot; flash: lit while *flash > 0.
struct Cell : app::ParamWidget {
    Module* module = NULL;
    const char* label = "";
    int setTo = -1;
    bool latch = false, green = false;
    int jack = -1;
    const float* flash = NULL;
    float fontSize = 6.5f;
    bool lit() {
        ParamQuantity* pq = getParamQuantity();
        if (!module || !pq) return setTo == 0 || green;
        if (flash && *flash > 0.f) return true;
        if (setTo >= 0) return (int)std::round(pq->getValue()) == setTo;
        return pq->getValue() > 0.5f;
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        ParamWidget::drawLayer(args, layer);
        if (layer != 1) return;
        bool on = lit();
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.2f);
        nvgFillColor(args.vg, on ? (green ? oled::green() : litFill()) : offFill());
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, outline(on ? 255 : 110));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        if (module && jack >= 0 && module->inputs[jack].isConnected()) {
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, W - 2.4f, 2.4f, 1.1f);
            nvgFillColor(args.vg, on ? ink() : amber());
            nvgFill(args.vg);
        }
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? ink() : amber());
        nvgText(args.vg, W * 0.5f, H * 0.5f, label, NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (ParamQuantity* pq = getParamQuantity()) {
                if (setTo >= 0) pq->setValue((float)setTo);
                else pq->setValue(latch ? (pq->getValue() > 0.5f ? 0.f : 1.f) : 1.f);
            }
            e.consume(this);
            return;
        }
        ParamWidget::onButton(e);
    }
    void onDragStart(const DragStartEvent& e) override {}
    void onDragEnd(const DragEndEvent& e) override {
        if (setTo < 0 && !latch && e.button == GLFW_MOUSE_BUTTON_LEFT)
            if (ParamQuantity* pq = getParamQuantity()) pq->setValue(0.f);
    }
};

} // namespace oled
