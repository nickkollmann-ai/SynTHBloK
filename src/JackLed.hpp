#pragma once
// BLOK-style micro LED (0.9 mm, as on the Kick / SnareHat / Euclid builds) showing a jack's signal: orange for
// positive, blue for negative. It lights at once and fades over a few frames, so short triggers still flash.
// Drawn by the widget, so the module needs no LightIds.
#include "plugin.hpp"

struct JackLed : widget::TransparentWidget {
    Module* module = NULL;
    int id = 0;
    bool out = true;
    float sm = 0.f;
    static JackLed* create(Vec posMm, Module* m, int id, bool out) {
        JackLed* l = new JackLed;
        l->box.size = mm2px(Vec(0.9f, 0.9f));
        l->box.pos = mm2px(posMm).minus(l->box.size.div(2.f));
        l->module = m; l->id = id; l->out = out;
        return l;
    }
    void draw(const DrawArgs& args) override {   // the dark lens
        float R = box.size.x * 0.5f;
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, R, R, R);
        nvgFillColor(args.vg, nvgRGB(0x22, 0x18, 0x10));
        nvgFill(args.vg);
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float v = 0.f;
        if (module) v = out ? module->outputs[id].getVoltage() : (module->inputs[id].isConnected() ? module->inputs[id].getVoltage() : 0.f);
        float n = clamp(v / 5.f, -1.f, 1.f);
        sm = std::fabs(n) >= std::fabs(sm) ? n : sm * 0.82f;
        float a = std::fabs(sm);
        if (a < 0.02f) return;
        float r = sm < 0.f ? 0.1f : 1.f, g = sm < 0.f ? 0.45f : 0.6f, b = sm < 0.f ? 1.f : 0.f, R = box.size.x * 0.5f;
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, R, R, R);
        nvgFillColor(args.vg, nvgRGBAf(r, g, b, a));
        nvgFill(args.vg);
        nvgBeginPath(args.vg);
        nvgRect(args.vg, -R * 3.f, -R * 3.f, R * 8.f, R * 8.f);
        nvgFillPaint(args.vg, nvgRadialGradient(args.vg, R, R, R * 0.5f, R * 4.f, nvgRGBAf(r, g, b, 0.35f * a), nvgRGBAf(r, g, b, 0.f)));
        nvgFill(args.vg);
    }
};
