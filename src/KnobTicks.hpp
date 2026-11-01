#pragma once
#include "plugin.hpp"

// Knob scale dashes, the same as the ones drawn into BloKMaster's panel SVG:
// 11 ticks over 270 deg, orange end ticks, dim brown in between, long ticks at the ends and centre.
// Sizes are for a RoundSmallBlackKnob; `scale` grows them for bigger knobs.
struct KnobTicks : TransparentWidget {
    float scale = 1.f;
    static KnobTicks* create(Vec centreMm, float scale = 1.f) {
        KnobTicks* t = new KnobTicks;
        t->scale = scale;
        t->box.size = mm2px(Vec(13.f, 13.f).mult(scale));
        t->box.pos = mm2px(centreMm).minus(t->box.size.div(2.f));
        return t;
    }
    void draw(const DrawArgs& args) override {
        float cx = box.size.x * 0.5f, cy = box.size.y * 0.5f;
        float r0 = mm2px(5.7f * scale), rLong = mm2px(6.4f * scale), rShort = mm2px(6.1f * scale);
        nvgStrokeWidth(args.vg, mm2px(0.28f));
        nvgLineCap(args.vg, NVG_ROUND);
        for (int i = 0; i <= 10; i++) {
            float a = (-135.f + 27.f * i) * (float) M_PI / 180.f;   // 0 = straight up
            float s = std::sin(a), c = -std::cos(a);
            bool end = i == 0 || i == 10;
            float r1 = (end || i == 5) ? rLong : rShort;
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, cx + s * r0, cy + c * r0);
            nvgLineTo(args.vg, cx + s * r1, cy + c * r1);
            nvgStrokeColor(args.vg, end ? nvgRGB(0xff, 0x7a, 0x00) : nvgRGB(0x6b, 0x42, 0x17));
            nvgStroke(args.vg);
        }
    }
};
