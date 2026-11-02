#pragma once
// FORGE FX: GRAIN-FORGE's whole FX chain condensed into one OLED and small knobs (first on MATRIX-DUO).
// forgeFxAddPanel() builds it at x = FX0 (mm), 102.68 mm wide, the full panel height; fm = the
// GRAIN-FORGE engine (a module of its own, or embedded), onMod / onParam = an FX on / off switch (-1: none).
#include "OledUi.hpp"
#include "KnobTicks.hpp"
#include "SynTHBloKGrainForge.hpp"

namespace {

// Amber value arc around a knob (0..1 of the knob's range).
struct FXRing : TransparentWidget {
    Module* module = NULL;
    int param = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float v = module ? module->paramQuantities[param]->getScaledValue() : 0.5f;
        float cx = box.size.x * 0.5f, cy = box.size.y * 0.5f, rr = box.size.x * 0.5f - 1.5f;
        float a0 = M_PI * 0.75f, a1 = M_PI * 2.25f;
        nvgBeginPath(args.vg);
        nvgArc(args.vg, cx, cy, rr, a0, a1, NVG_CW);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 40));
        nvgStrokeWidth(args.vg, 1.8f);
        nvgStroke(args.vg);
        if (v > 0.001f) {
            nvgBeginPath(args.vg);
            nvgArc(args.vg, cx, cy, rr, a0, a0 + (a1 - a0) * v, NVG_CW);
            nvgStrokeColor(args.vg, nvgRGBA(255, 112, 15, 235));
            nvgStrokeWidth(args.vg, 1.8f);
            nvgStroke(args.vg);
        }
    }
};

// ---------------------------------------------------------------- FORGE FX ----------------
// GRAIN-FORGE's FX chain in one OLED (OledUi.hpp look). Rows (mm from the OLED's top): one per stage
// (ON cell with its name, live status, RND; PULSE also LP | BP | HP), THROW steps + their RND,
// PRESET | CYCLE | GLIDE | FREEZE, the filter env TRIG + SEQ (two rows tall) with BYP, and FILTER:
// STG | TYPE, the env stages, the output meter and FX ON. Click / scroll a value to step it,
// right-click steps back (PRESET: the list).
typedef SynTHBloKGrainForge FXG;
static const float kFxW = 99.68f, kFxPad = 2.f, kFxCol = 19.f, kFxRndW = 8.f, kFxRndX = kFxW - kFxPad - kFxRndW,
                   kFxPitch = oled::kCellH + 0.9f, kFxTop = 1.2f, kFxSeqY = kFxTop + 7.f * kFxPitch,
                   kFxSeqH = 2.f * oled::kCellH, kFxFltY = kFxSeqY + kFxSeqH + 0.9f, kFxH = kFxFltY + oled::kCellH + kFxTop;
static const float kFxModeX = kFxRndX - 1.f - 3.f * 6.f - 2.f * 0.5f;   // PULSE's LP | BP | HP
static const float kFxPreSeg[2] = {44.5f, 66.75f};                     // PRESET | CYCLE | GLIDE
static const float kFxStgX = kFxCol, kFxTypeX = 36.5f, kFxEnvX = 57.f, kFxMeterX = 71.5f;
static float fxRowY(int i) { return kFxTop + i * kFxPitch; }
struct FxStage { const char* name; int on, sel, count; };
static const FxStage kFxStage[5] = {
    {"SHUFFLE", FXG::SH_ON_PARAM, FXG::SH_PATTERN_PARAM, kShufCount}, {"GLITCH", FXG::GL_ON_PARAM, FXG::GL_ROLL_PARAM, kRollCount},
    {"ROBOT", FXG::RB_ON_PARAM, FXG::RB_CHORD_PARAM, 8}, {"PULSE", FXG::PU_ON_PARAM, FXG::PU_PATTERN_PARAM, kPulseCount},
    {"WARP DLY", FXG::DL_ON_PARAM, FXG::DL_TIME_PARAM, kDelayCount}};

// A stage's live status, as on GRAIN-FORGE's stage headers (PULSE's mode is on its cells).
static std::string fxStatus(FXG* m, int s) {
    char b[32];
    switch (s) {
        case 0: {
            int p = m ? clamp(m->slot(FXG::SH_PATTERN_PARAM), 0, kShufCount - 1) : 6, src = m ? m->shuffle.src : -1;
            if (src < 0) snprintf(b, sizeof b, "%s  LIVE", kShufName[p]); else snprintf(b, sizeof b, "%s  >%02d", kShufName[p], src + 1);
            return b;
        }
        case 1: return m ? m->glitchEvent : "ROLL 1/16";
        case 2: {
            int root = m ? m->slot(FXG::RB_ROOT_PARAM) : 12, ch = m ? clamp(m->slot(FXG::RB_CHORD_PARAM), 0, 7) : 3;
            return std::string(kNote[root % 12]) + std::to_string(1 + (root + 9) / 12) + " " + kChordName[ch];
        }
        case 3:
            if (m && m->inputs[FXG::OPEN_INPUT].getVoltage() > 1.f) return "OPEN";
            return kPulseName[m ? clamp(m->slot(FXG::PU_PATTERN_PARAM), 0, kPulseCount - 1) : 3];
        default: {
            int t = m ? clamp(m->slot(FXG::DL_TIME_PARAM), 0, kDelayCount - 1) : 8;
            snprintf(b, sizeof b, "%s  %+.0f st", kDelayName[t], m ? m->params[FXG::DL_WARP_PARAM].getValue() : 0.f);
            return b;
        }
    }
}

struct FXDisplay : TransparentWidget {
    FXG* module = NULL;
    float peak[2] = {};
    void text(const DrawArgs& args, float x, float yMm, const char* s, NVGcolor c, float size, int align = NVG_ALIGN_LEFT) {
        nvgFontSize(args.vg, size);
        nvgFillColor(args.vg, c);
        nvgTextAlign(args.vg, align | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, x, mm2px(yMm + oled::kCellH * 0.5f), s, NULL);
    }
    // thin dBFS bar, -48..+6 dB in 16 segments (amber, orange above -9, red above -1), with a falling peak
    void meter(const DrawArgs& args, float x, float y, float w, float h, float lvl, float& pk) {
        const int N = 16;
        auto pos = [](float v) { return clamp((GFStatus::toDb(v) + 48.f) / 54.f, 0.f, 1.f); };
        pk = std::max(lvl, pk * 0.9975f);
        float p = pos(lvl), pp = pos(pk), sw = w / N;
        for (int i = 0; i < N; i++) {
            float sp = (i + 1.f) / N, db = -48.f + 54.f * sp;
            bool lit = sp <= p + 1e-4f, pkSeg = (int)std::ceil(pp * N) == i + 1 && pp > 0.02f;
            NVGcolor c = db > -1.f ? nvgRGBA(255, 40, 30, 255) : db > -9.f ? nvgRGBA(255, 112, 15, 255) : nvgRGBA(255, 176, 60, 255);
            if (!lit && !pkSeg) c = nvgRGBA(46, 29, 10, 255);
            else if (!lit) c.a = 0.5f;
            nvgBeginPath(args.vg);
            nvgRect(args.vg, x + i * sw + 0.3f, y, sw - 0.6f, h);
            nvgFillColor(args.vg, c);
            nvgFill(args.vg);
        }
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        FXG* m = module;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        nvgBeginPath(args.vg);
        for (float y : {fxRowY(1), fxRowY(2), fxRowY(3), fxRowY(4), fxRowY(5), fxRowY(6), kFxSeqY, kFxFltY})
            oled::divider(args, 4.f, mm2px(y - 0.45f), W - 4.f, mm2px(y - 0.45f));
        for (float x : kFxPreSeg) oled::divider(args, mm2px(x), mm2px(fxRowY(6)) + 2.f, mm2px(x), mm2px(fxRowY(6) + oled::kCellH) - 2.f);
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W, H);
        NVGcolor amb = oled::amber(), dim = oled::dim();
        // stage status, dim while the stage is off
        for (int s = 0; s < 5; s++) {
            bool on = !m || m->on(kFxStage[s].on);
            text(args, mm2px(kFxCol + 0.5f), fxRowY(s), fxStatus(m, s).c_str(), oled::amber(on ? 255 : 90), 7.5f);
        }
        text(args, 4.f, fxRowY(5), "THROW", dim, 6.f);
        // PRESET (the name in its preset-bar colour) | CYCLE | GLIDE
        int pr = m ? clamp(m->slot(FXG::PRESET_PARAM), 0, kPresetCount) : 0;
        text(args, 4.f, fxRowY(6), "PRESET", dim, 6.f);
        text(args, mm2px(kFxCol + 0.5f), fxRowY(6), GFPresetBar::nameOf(pr), GFPresetBar::cellColor(pr, 255), 7.5f);
        int dv = m ? clamp(m->slot(FXG::RND_DIV_PARAM), 0, kRndDivCount - 1) : 2;
        int gl = m ? clamp(m->slot(FXG::RND_GLIDE_PARAM), 0, kRndGlideCount - 1) : 2;
        const char* top[2] = {"CYCLE", "GLIDE"};
        const char* val[2] = {kRndDivName[dv], kRndGlideName[gl]};
        const float x0[2] = {kFxPreSeg[0], kFxPreSeg[1]}, x1[2] = {kFxPreSeg[1], kFxRndX - 1.f};
        for (int i = 0; i < 2; i++) {
            text(args, mm2px(x0[i]) + 4.f, fxRowY(6), top[i], dim, 6.f);
            text(args, mm2px(x1[i]) - 4.f, fxRowY(6), val[i], amb, 7.5f, NVG_ALIGN_RIGHT);
        }
        // FILTER: env stages lit as they run, then the output meter (L over R)
        text(args, 4.f, kFxFltY, "FILTER", dim, 6.f);
        static const char* an[4] = {"A", "D", "S", "R"};
        const float ew = (kFxMeterX - 1.5f - kFxEnvX - 3.f * 0.4f) / 4.f;
        for (int k = 0; k < 4; k++) {
            float lv = m ? clamp(m->stageLvl[k], 0.f, 1.f) : 0.f, ex = mm2px(kFxEnvX + k * (ew + 0.4f));
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, ex, mm2px(kFxFltY), mm2px(ew), mm2px(oled::kCellH), 1.f);
            nvgFillColor(args.vg, nvgLerpRGBA(oled::offFill(), oled::litFill(), lv));
            nvgFill(args.vg);
            text(args, ex + mm2px(ew) * 0.5f, kFxFltY, an[k], lv > 0.5f ? oled::ink() : dim, 6.f, NVG_ALIGN_CENTER);
        }
        float mx = mm2px(kFxMeterX), mw = mm2px(kFxRndX - 1.f - kFxMeterX), mh = mm2px(1.5f);
        meter(args, mx, mm2px(kFxFltY + 0.5f), mw, mh, m ? m->meterL : 0.5f, peak[0]);
        meter(args, mx, mm2px(kFxFltY + oled::kCellH - 0.5f) - mh, mw, mh, m ? m->meterR : 0.4f, peak[1]);
        nvgResetScissor(args.vg);
    }
    // what a click / scroll at p (mm) steps by d; false = nothing there
    bool stepAt(Vec p, int d) {
        FXG* m = module;
        for (int s = 0; s < 5; s++) {
            float y = fxRowY(s), xe = s == 3 ? kFxModeX : kFxRndX;
            if (p.y < y || p.y >= y + oled::kCellH || p.x < kFxCol || p.x >= xe) continue;
            const FxStage& st = kFxStage[s];
            m->params[st.sel].setValue((float)((m->slot(st.sel) + d + st.count) % st.count));
            return true;
        }
        float y = fxRowY(6);
        if (p.y < y || p.y >= y + oled::kCellH || p.x < kFxCol || p.x >= kFxRndX) return false;
        int id = p.x < kFxPreSeg[0] ? FXG::PRESET_PARAM : p.x < kFxPreSeg[1] ? FXG::RND_DIV_PARAM : FXG::RND_GLIDE_PARAM;
        int n = id == FXG::PRESET_PARAM ? kPresetCount + 1 : id == FXG::RND_DIV_PARAM ? kRndDivCount : kRndGlideCount;
        m->params[id].setValue((float)((m->slot(id) + d + n) % n));
        return true;
    }
    void presetMenu() {
        FXG* m = module;
        ui::Menu* menu = createMenu();
        menu->addChild(createMenuLabel("Preset"));
        for (int i = 0; i <= kPresetCount; i++)
            menu->addChild(createCheckMenuItem(GFPresetBar::nameOf(i), "", [=]() { return m->slot(FXG::PRESET_PARAM) == i; }, [=]() {
                if (m->slot(FXG::PRESET_PARAM) == i) m->presetRequest.store(i);   // same one again: reload / re-roll
                else m->params[FXG::PRESET_PARAM].setValue((float)i);
            }));
    }
    void onButton(const ButtonEvent& e) override {
        if (!module || e.action != GLFW_PRESS || (e.button != GLFW_MOUSE_BUTTON_LEFT && e.button != GLFW_MOUSE_BUTTON_RIGHT)) return;
        Vec p = e.pos.div(mm2px(1.f));
        bool right = e.button == GLFW_MOUSE_BUTTON_RIGHT;
        if (right && p.y >= fxRowY(6) && p.y < fxRowY(6) + oled::kCellH && p.x >= kFxCol && p.x < kFxPreSeg[0]) {
            presetMenu();
            e.consume(this);
            return;
        }
        if (stepAt(p, right ? -1 : 1)) e.consume(this);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (!module || e.scrollDelta.y == 0.f) return;
        if (stepAt(e.pos.div(mm2px(1.f)), e.scrollDelta.y > 0.f ? 1 : -1)) e.consume(this);
    }
};

// Throw step cell: white outline while its step plays.
struct FXThrowCell : oled::Cell {
    FXG* gf = NULL;
    int idx = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        oled::Cell::drawLayer(args, layer);
        if (layer != 1 || !gf || gf->step != idx) return;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.2f);
        nvgStrokeColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgStrokeWidth(args.vg, 1.1f);
        nvgStroke(args.vg);
    }
};

// THROW's RND: GRAIN-FORGE's random-pattern button, drawn in the lit layer.
struct FXThrowRoll : GFThrowRoll {
    void draw(const DrawArgs& args) override {}
    void drawLayer(const DrawArgs& args, int layer) override { if (layer == 1) GFThrowRoll::draw(args); }
};

// GRAIN-FORGE's filter SEQ screen (same clicks and painting) without its own box, the TRIG name and value
// on the left at the OLED's label column (labelX, px from this widget's left).
struct FXSeq : GFSeqScreen {
    float labelX = 4.f;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        FXG* m = module;
        float W = box.size.x, H = box.size.y, x0 = barX0();
        int trig = m ? clamp(m->slot(FXG::FLT_TRIG_PARAM), 0, kTrigCount - 1) : TRIG_SEQ;
        bool seqMode = trig == TRIG_SEQ;
        float ev = m ? clamp(m->fltEnvOut, 0.f, 1.f) : 0.f;   // envelope level as a glow strip under TRIG
        nvgBeginPath(args.vg);
        nvgRect(args.vg, labelX, H - 3.f, (x0 - 3.f - labelX) * ev, 1.5f);
        nvgFillColor(args.vg, oled::litFill());
        nvgFill(args.vg);
        if (std::shared_ptr<window::Font> font = APP->window->uiFont) {
            nvgFontFaceId(args.vg, font->handle);
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFontSize(args.vg, 6.f);
            nvgFillColor(args.vg, oled::dim());
            nvgText(args.vg, labelX, H * 0.25f, "TRIG", NULL);
            nvgFontSize(args.vg, 7.5f);
            nvgFillColor(args.vg, oled::amber());
            nvgText(args.vg, labelX, H * 0.6f, kTrigName[trig], NULL);
        }
        float bw = (W - x0 - 2.f) / FXG::kSeqMax;
        int len = m ? m->seqLen : 16, pos = m ? m->seqPos : -1;
        for (int i = 0; i < FXG::kSeqMax; i++) {
            bool on = m ? m->seqOn[i] : i % 2 == 0;
            float lv = m ? m->seqLvl[i] : 0.5f;
            float bx = x0 + i * bw + 0.6f, bh = 1.5f + lv * (H - 4.5f);
            int a = i >= len ? 40 : seqMode ? 255 : 110;   // dim when SEQ isn't the trigger, dimmer past LENGTH
            nvgBeginPath(args.vg);
            nvgRect(args.vg, bx, H - 1.5f - bh, bw - 1.2f, bh);
            if (on) nvgFillColor(args.vg, i == pos && seqMode ? nvgRGBA(255, 240, 200, a) : nvgRGBA(255, 112, 15, a * 9 / 10));
            else nvgFillColor(args.vg, nvgRGBA(255, 180, 90, a / 5));
            nvgFill(args.vg);
            if (i % 4 == 0) {   // beat ticks
                nvgBeginPath(args.vg);
                nvgRect(args.vg, bx, H - 1.2f, bw - 1.2f, 0.6f);
                nvgFillColor(args.vg, nvgRGBA(255, 180, 90, 90));
                nvgFill(args.vg);
            }
        }
    }
};


static void forgeFxAddPanel(ModuleWidget* mw, SynTHBloKGrainForge* fm, Module* onMod, int onParam, float FX0) {
        // ---------------- right of BASS: FORGE FX ----------------
        // the OLED, then the stage knobs (a row per stage, in the OLED's order, MIX last) and, right of
        // them, the master filter, its ADSR and the levels
        const float FOX = FX0 + 1.5f, FOY = 10.5f;
        FXDisplay* fd = createWidget<FXDisplay>(mm2px(Vec(FOX, FOY)));
        fd->box.size = mm2px(Vec(kFxW, kFxH)); fd->module = fm;
        mw->addChild(fd);
        auto cell = [&](float x, float y, float w, float h, Module* mod, int param, const char* lab) -> oled::Cell* {
            oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(FOX + x, FOY + y)), mod, param);
            c->box.size = mm2px(Vec(w, h)); c->module = mod; c->label = lab;
            mw->addParam(c);
            return c;
        };
        const float CH = oled::kCellH;
        for (int s = 0; s < 5; s++) {
            cell(kFxPad, fxRowY(s), kFxCol - kFxPad - 1.f, CH, fm, kFxStage[s].on, kFxStage[s].name)->latch = true;
            cell(kFxRndX, fxRowY(s), kFxRndW, CH, fm, FXG::RND_PARAM + s, "RND")->latch = true;
        }
        for (int k = 0; k < 3; k++) cell(kFxModeX + k * 6.5f, fxRowY(3), 6.f, CH, fm, FXG::PU_MODE_PARAM, kModeName[k])->setTo = k;
        // throw steps in beats of four
        const float tw = (kFxRndX - 1.f - kFxCol - 15.f * 0.4f - 3.f * 0.8f) / 16.f;
        for (int i = 0; i < FXG::kThrowSteps; i++) {
            FXThrowCell* c = createParam<FXThrowCell>(mm2px(Vec(FOX + kFxCol + i * (tw + 0.4f) + (i / 4) * 0.8f, FOY + fxRowY(5))),
                                                      fm, FXG::throwParam(i));
            c->box.size = mm2px(Vec(tw, CH)); c->module = fm; c->latch = true; c->gf = fm; c->idx = i;
            mw->addParam(c);
        }
        FXThrowRoll* roll = createWidget<FXThrowRoll>(mm2px(Vec(FOX + kFxRndX, FOY + fxRowY(5))));
        roll->box.size = mm2px(Vec(kFxRndW, CH)); roll->module = fm;
        mw->addChild(roll);
        oled::Cell* frz = cell(kFxRndX, fxRowY(6), kFxRndW, CH, fm, FXG::RND_FREEZE_PARAM, "FREEZE");
        frz->latch = true; frz->fontSize = 5.5f;
        // SEQ bars start at the value column, like the throw steps
        const float sw = (kFxRndX - 1.f - kFxCol) / (1.f - GFSeqScreen::kTrigW), sx = kFxRndX - 1.f - sw;
        FXSeq* sq = createWidget<FXSeq>(mm2px(Vec(FOX + sx, FOY + kFxSeqY)));
        sq->box.size = mm2px(Vec(sw, kFxSeqH)); sq->module = fm; sq->labelX = 4.f - mm2px(sx);
        mw->addChild(sq);
        cell(kFxRndX, kFxSeqY, kFxRndW, kFxSeqH, fm, FXG::FLT_BYPASS_PARAM, "BYP")->latch = true;
        static const char* const stgName[3] = {"1", "2", "3"};
        for (int k = 0; k < 3; k++) {
            cell(kFxStgX + k * 5.5f, kFxFltY, 5.f, CH, fm, FXG::FLT_STAGES_PARAM, stgName[k])->setTo = k;
            cell(kFxTypeX + k * 6.5f, kFxFltY, 6.f, CH, fm, FXG::FLT_MODE_PARAM, kModeName[k])->setTo = k;
        }
        if (onParam >= 0) {
            oled::Cell* fxOn = cell(kFxRndX, kFxFltY, kFxRndW, CH, onMod, onParam, "FX");
            fxOn->latch = true; fxOn->green = true;
        }

        auto fxLabel = [&](float cx, float cy, const char* text) {
            GFLabel* l = createWidget<GFLabel>(mm2px(Vec(cx - 6.f, cy - 2.f)));
            l->box.size = mm2px(Vec(12.f, 4.f)); l->text = text; l->size = 5.5f;
            mw->addChild(l);
        };
        auto fxKnob = [&](float x, float y, int param, const char* name) {
            mw->addChild(KnobTicks::create(Vec(x, y), 0.72f));
            FXRing* r = createWidget<FXRing>(mm2px(Vec(x - 4.f, y - 4.f)));
            r->box.size = mm2px(Vec(8.f, 8.f)); r->module = fm; r->param = param;
            mw->addChild(r);
            mw->addParam(createParamCentered<Trimpot>(mm2px(Vec(x, y)), fm, param));
            fxLabel(x, y + 5.4f, name);
        };
        auto fxBigKnob = [&](float x, float y, int param, const char* name) {
            mw->addChild(KnobTicks::create(Vec(x, y)));
            FXRing* r = createWidget<FXRing>(mm2px(Vec(x - 5.5f, y - 5.5f)));
            r->box.size = mm2px(Vec(11.f, 11.f)); r->module = fm; r->param = param;
            mw->addChild(r);
            mw->addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y)), fm, param));
            fxLabel(x, y + 7.4f, name);
        };
        static const int SP[5][6] = {
            {FXG::SH_PATTERN_PARAM, FXG::SH_AMOUNT_PARAM, FXG::SH_REV_PARAM, FXG::SH_PITCH_PARAM, FXG::SH_LEN_PARAM, FXG::SH_MIX_PARAM},
            {FXG::GL_CHANCE_PARAM, FXG::GL_ROLL_PARAM, FXG::GL_REV_PARAM, FXG::GL_STOP_PARAM, FXG::GL_CRUSH_PARAM, FXG::GL_MIX_PARAM},
            {FXG::RB_ROOT_PARAM, FXG::RB_CHORD_PARAM, FXG::RB_VOWEL_PARAM, FXG::RB_RING_PARAM, FXG::RB_Q_PARAM, FXG::RB_MIX_PARAM},
            {FXG::PU_CUTOFF_PARAM, FXG::PU_RES_PARAM, FXG::PU_DEPTH_PARAM, FXG::PU_PATTERN_PARAM, FXG::PU_DECAY_PARAM, FXG::PU_MIX_PARAM},
            {FXG::DL_TIME_PARAM, FXG::DL_FDBK_PARAM, FXG::DL_WARP_PARAM, FXG::DL_ROUTE_PARAM, FXG::DL_TONE_PARAM, FXG::DL_MIX_PARAM}};
        static const char* const SN[5][6] = {
            {"PATTERN", "AMOUNT", "REVERSE", "OCTAVE", "LENGTH", "MIX"}, {"CHANCE", "ROLL", "REVERSE", "STOP", "CRUSH", "MIX"},
            {"ROOT", "CHORD", "VOWEL", "RING", "RES", "MIX"}, {"CUTOFF", "RES", "DEPTH", "PATTERN", "DECAY", "MIX"},
            {"TIME", "FDBK", "WARP", "ROUTE", "TONE", "ECHO"}};
        for (int s = 0; s < 5; s++)
            for (int k = 0; k < 6; k++) fxKnob(FX0 + 1.2f + 9.6f * (k + 0.5f), 70.8f + 11.7f * s, SP[s][k], SN[s][k]);
        const float M0 = FX0 + 59.6f;
        fxBigKnob(M0 + 7.f, 71.5f, FXG::FILTER_PARAM, "FILTER");
        fxKnob(M0 + 18.5f, 71.5f, FXG::FLT_RES_PARAM, "RES");
        fxKnob(M0 + 27.5f, 71.5f, FXG::FLT_ENV_PARAM, "ENV");
        fxKnob(M0 + 36.5f, 71.5f, FXG::FLT_AMP_PARAM, "AMP");
        const int fxAdsr[4] = {FXG::FLT_ATK_PARAM, FXG::FLT_DEC_PARAM, FXG::FLT_SUS_PARAM, FXG::FLT_REL_PARAM};
        const char* fxAn[4] = {"ATK", "DEC", "SUS", "REL"};
        for (int i = 0; i < 4; i++) {   // each with its stage LED under the name, as on GRAIN-FORGE
            fxKnob(M0 + 6.f + 10.f * i, 94.2f, fxAdsr[i], fxAn[i]);
            mw->addChild(createLightCentered<GFMicroLight<RedGreenBlueLight>>(mm2px(Vec(M0 + 6.f + 10.f * i, 102.4f)), fm, FXG::ADSR_LED + i * 3));
        }
        fxKnob(M0 + 7.f, 116.f, FXG::IN_GAIN_PARAM, "INPUT");
        fxBigKnob(M0 + 21.f, 116.f, FXG::DRYWET_PARAM, "DRY / WET");
        fxKnob(M0 + 35.f, 116.f, FXG::OUT_GAIN_PARAM, "OUTPUT");
}

}  // namespace
