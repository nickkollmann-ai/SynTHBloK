#pragma once
// MATRIX-ECHO's looper face, shared by every module that plays an EchoLooper with it (MATRIX-ECHO, MATRIX-FX):
// the LOOP line (state | armed action or count-in | layers and length), the waveform (playing part bright,
// beat ticks, playhead; scroll zooms at the pointer, right-click shows all), the cell states, the REC / PLAY
// tint cells, the COUNT-IN cycler, the record-length cells, and the loop's audio as a file (kept with a patch).
#include "plugin.hpp"
#include "OledUi.hpp"
#include "Echo_dsp.hpp"
#include <cstring>
#include <string>
#include <vector>

namespace {
namespace echoloop {

const char* const kStateName[6] = {"EMPTY", "REC", "PLAY", "REPLACE", "STOP", "ONCE"};
const char* const kArmName[8] = {"", "> REC", "> PLAY", "> ONCE", "> PLAY", "> ONCE", "> STOP", "> DUB"};
// record length, 3 x 3: FREE (until REC again) | 1-4 beats | 1-4 bars. 4 BT and 1 BAR are the same length,
// so both light.
const char* const kLenName[9] = {"FREE", "1 BT", "2 BT", "3 BT", "4 BT", "1 BAR", "2 BAR", "3 BAR", "4 BAR"};
const int kLenBeats[9] = {0, 1, 2, 3, 4, 4, 8, 12, 16};

// the cells' lights: lit with the state; an armed action blinks its cell (blink: 0 .. 0.25 s, lit under
// 0.125) until the grid edge; the count-in lights REC on each beat
inline void flashes(const EchoLooper& lp, float blink, float& rec, float& play, float& once) {
    float bl = blink < 0.125f ? 1.f : 0.f;
    EchoLooper::State st = lp.st;
    int a = lp.arm;
    rec = st == EchoLooper::REC || st == EchoLooper::DUB ? 1.f : 0.f;
    play = st == EchoLooper::PLAY || st == EchoLooper::DUB ? 1.f : 0.f;
    once = st == EchoLooper::ONCE ? 1.f : 0.f;
    if (a == EchoLooper::A_REC || a == EchoLooper::A_DUB) rec = bl;
    if (lp.counting()) rec = lp.sinceBeat < 0.25f * lp.tempoBeat ? 1.f : 0.f;
    if (a == EchoLooper::A_CLOSE || a == EchoLooper::A_PLAY || a == EchoLooper::A_STOP) play = bl;
    if (a == EchoLooper::A_CLOSE_ONCE || a == EchoLooper::A_ONCE) once = bl;
}

// the waveform's view: zoom factor and its start (fraction of the loop)
struct View {
    float zoom = 1.f, view0 = 0.f;
    // zoom by one step, keeping the sample under the pointer (px across a display W wide) where it is
    void zoomAt(float px, float W, int d) {
        float fx = clamp((px - 4.f) / std::max(1.f, W - 8.f), 0.f, 1.f);
        float at = view0 + fx / zoom;
        zoom = clamp(zoom * (d > 0 ? 1.5f : 1.f / 1.5f), 1.f, 512.f);
        if (zoom < 1.05f) zoom = 1.f;
        view0 = clamp(at - fx / zoom, 0.f, 1.f - 1.f / zoom);
    }
    void reset() { zoom = 1.f; view0 = 0.f; }
};

// LOOP state | armed action | layers and length on the line at yL; the waveform with the playing part,
// beats and playhead between yW and yWB (fonts must be set). lp NULL: the module browser's preview.
inline void draw(const widget::Widget::DrawArgs& args, float W, float yL, float yW, float yWB, EchoLooper* lp,
                 float blink, View& v) {
    int st = lp ? (int)lp->st : 0;
    char info[40] = "60 s";
    if (lp && lp->st == EchoLooper::REC) {
        if (lp->locked) snprintf(info, sizeof info, "%d BT  %.2f s", (int)(lp->pos / lp->curBeat) + 1, lp->pos / lp->sr);
        else snprintf(info, sizeof info, "%.2f s", lp->pos / lp->sr);
    } else if (lp && lp->len > 0) {
        int L = lp->playLen > 0 ? lp->playLen : lp->len;
        snprintf(info, sizeof info, "x%d  %.2f s", lp->layers, L / lp->sr);
    }
    NVGcolor stc = st == EchoLooper::REC || st == EchoLooper::DUB ? oled::litFill()
                 : st == EchoLooper::PLAY || st == EchoLooper::ONCE ? oled::green() : oled::dim();
    float y0 = yL + mm2px(oled::kRow) * 0.5f;
    nvgFontSize(args.vg, 6.f);
    nvgFillColor(args.vg, oled::dim());
    nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(args.vg, 4.f, y0, "LOOP", NULL);
    nvgFontSize(args.vg, 7.5f);
    nvgFillColor(args.vg, stc);
    nvgText(args.vg, mm2px(7.5f), y0, kStateName[st], NULL);
    if (lp && lp->counting()) {   // the count-in: beats left until the take starts
        char cnt[16];
        snprintf(cnt, sizeof cnt, "COUNT %d", lp->counting());
        nvgFillColor(args.vg, oled::litFill());
        nvgText(args.vg, mm2px(22.f), y0, cnt, NULL);
    } else if (lp && lp->arm != EchoLooper::A_NONE && blink < 0.125f) {
        nvgFillColor(args.vg, oled::litFill());
        nvgText(args.vg, mm2px(22.f), y0, kArmName[clamp(lp->arm, 0, 7)], NULL);
    }
    nvgFillColor(args.vg, oled::amber());
    nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgText(args.vg, W - 4.f, y0, info, NULL);

    // waveform: one bar per peak bucket, mirrored round the centre line
    float x0 = 4.f, x1 = W - 4.f, yc = (yW + yWB) * 0.5f, hh = (yWB - yW) * 0.5f - 2.f;
    if (lp && lp->st == EchoLooper::REC) {
        float f = clamp((float)lp->pos / std::max(1, lp->cap), 0.f, 1.f);
        nvgBeginPath(args.vg);
        nvgRect(args.vg, x0, yc - 1.f, (x1 - x0) * f, 2.f);
        nvgFillColor(args.vg, oled::litFill());
        nvgFill(args.vg);
    } else if (lp && lp->len > 0) {
        // the view: [v0, v0 + span) samples; zoomed in, the bars are read from the recording itself
        if (v.zoom < 1.f) v.zoom = 1.f;
        // zoomed in while it plays: the view follows the playhead, keeping it in the middle
        if (v.zoom > 1.f && lp->playing()) v.view0 = (float)lp->pos / lp->len - 0.5f / v.zoom;
        v.view0 = clamp(v.view0, 0.f, 1.f - 1.f / v.zoom);
        const double len = lp->len, v0 = v.view0 * len, span = len / v.zoom;
        auto X = [&](double s) { return x0 + (x1 - x0) * (float)((s - v0) / span); };
        const int N = v.zoom <= 1.f ? EchoLooper::kPeaks : 160;
        float pk[160];
        for (int c = 0; c < N; c++) {
            if (v.zoom <= 1.f) { pk[c] = lp->peak[c]; continue; }
            int a = (int)(v0 + c * span / N), b = std::min(lp->len, std::max(a + 1, (int)(v0 + (c + 1) * span / N)));
            int step = std::max(1, (b - a) / 128);   // at most 128 reads a bar
            float m = 0.f;
            for (int i = a; i < b; i += step) m = std::max(m, std::max(std::fabs(lp->l[i]), std::fabs(lp->r[i])));
            pk[c] = m;
        }
        float bw = (x1 - x0) / N;
        int L = lp->playLen > 0 ? std::min(lp->playLen, lp->len) : lp->len;
        for (int pass = 0; pass < 2; pass++) {   // the playing part bright, the rest dim
            nvgBeginPath(args.vg);
            for (int c = 0; c < N; c++) {
                bool in = v0 + (c + 0.5) * span / N <= L;
                if (in != (pass == 0)) continue;
                float h = std::max(0.5f, std::min(1.f, pk[c]) * hh);
                nvgRect(args.vg, x0 + c * bw, yc - h, std::max(0.6f, bw - 0.4f), 2.f * h);
            }
            nvgFillColor(args.vg, oled::amber(pass == 0 ? 170 : 45));
            nvgFill(args.vg);
        }
        if (lp->beats > 1 && lp->beatLen > 0.0 && span / lp->beatLen <= 64.0) {   // beat ticks (bars longer)
            nvgBeginPath(args.vg);
            for (int k = 1; k < lp->beats; k++) {
                double s = k * lp->beatLen;
                if (s < v0 || s > v0 + span) continue;
                float x = X(s), t = k % 4 == 0 ? 2.5f : 1.3f;
                nvgMoveTo(args.vg, x, yW + 0.5f); nvgLineTo(args.vg, x, yW + 0.5f + t);
                nvgMoveTo(args.vg, x, yWB - 0.5f); nvgLineTo(args.vg, x, yWB - 0.5f - t);
            }
            nvgStrokeColor(args.vg, oled::outline(150));
            nvgStrokeWidth(args.vg, 0.6f);
            nvgStroke(args.vg);
        }
        if (lp->pos >= v0 && lp->pos <= v0 + span) {   // the playhead, when in view
            float px = X(lp->pos);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, px, yW + 1.5f);
            nvgLineTo(args.vg, px, yWB - 1.5f);
            nvgStrokeColor(args.vg, lp->playing() ? oled::green() : oled::dim());
            nvgStrokeWidth(args.vg, 1.f);
            nvgStroke(args.vg);
        }
        if (v.zoom > 1.f) {   // how far in, and where: a bar along the bottom edge
            char zt[16];
            snprintf(zt, sizeof zt, "x%.0f", v.zoom);
            nvgFontSize(args.vg, 6.f);
            nvgFillColor(args.vg, oled::dim());
            nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
            nvgText(args.vg, x1, yW + 1.f, zt, NULL);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, x0 + (x1 - x0) * v.view0, yWB - 1.2f, std::max(1.5f, (x1 - x0) / v.zoom), 1.f);
            nvgFillColor(args.vg, oled::amber(120));
            nvgFill(args.vg);
        }
    } else {
        v.reset();
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, x0, yc);
        nvgLineTo(args.vg, x1, yc);
        nvgStrokeColor(args.vg, oled::outline(60));
        nvgStrokeWidth(args.vg, 0.6f);
        nvgStroke(args.vg);
    }
}

// REC / PLAY: a cell in its own colour, dim (outline and label) until engaged, filled while it is
struct TintCell : oled::Cell {
    NVGcolor tint = nvgRGB(0xff, 0x32, 0x28);
    void drawLayer(const DrawArgs& args, int layer) override {
        ParamWidget::drawLayer(args, layer);
        if (layer != 1) return;
        bool on = lit();
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.2f);
        nvgFillColor(args.vg, on ? tint : oled::offFill());
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgTransRGBA(tint, on ? 255 : 120));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        if (module && jack >= 0 && module->inputs[jack].isConnected()) {
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, W - 2.4f, 2.4f, 1.1f);
            nvgFillColor(args.vg, on ? oled::ink() : tint);
            nvgFill(args.vg);
        }
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? oled::ink() : nvgTransRGBA(tint, 190));
        nvgText(args.vg, W * 0.5f, H * 0.5f, label, NULL);
    }
};

// COUNT-IN: one cell that steps OFF > 1 > 2 > 3 > 4 beats (click; scroll both ways; right-click lists them)
struct CountCell : oled::Cell {
    char text[12] = "COUNT OFF";
    CountCell() { label = text; latch = true; }
    void set(int v) { if (ParamQuantity* pq = getParamQuantity()) pq->setValue((float)((v % 5 + 5) % 5)); }
    int get() { ParamQuantity* pq = getParamQuantity(); return pq ? (int)std::round(pq->getValue()) : 0; }
    void step() override {
        int v = get();
        if (v > 0) snprintf(text, sizeof text, "COUNT %d", v);
        else snprintf(text, sizeof text, "COUNT OFF");
        oled::Cell::step();
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) { set(get() + 1); e.consume(this); return; }
        oled::Cell::onButton(e);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (e.scrollDelta.y == 0.f) return;
        set(clamp(get() + (e.scrollDelta.y > 0.f ? 1 : -1), 0, 4));
        e.consume(this);
    }
};

// ---- the loop's audio as a file (MATRIX-LOOP's track format): "SBLP", version 1, sample rate, samples,
// stereo, beats, loop beats, play length, layers, beat length (double), then the left and (stereo) right ----

// lp's loop from n samples at srcSr (resampled to lp's rate), stopped
inline void restore(EchoLooper& lp, const float* L, const float* R, int n, float srcSr, int beats, int loopBeats,
                    int pl, double beatLen, int layers) {
    lp.clear();
    const double k = (double)lp.sr / srcSr;
    const int m = std::min(lp.cap, (int)std::lround(n * k));
    if (n < 1 || m < 1) return;
    for (int i = 0; i < m; i++) {
        const double x = std::min((double)i / k, (double)(n - 1));
        const int a = (int)x, b = std::min(a + 1, n - 1);
        const float f = (float)(x - a);
        lp.l[i] = L[a] + (L[b] - L[a]) * f;
        lp.r[i] = R[a] + (R[b] - R[a]) * f;
    }
    lp.len = m; lp.pos = 0; lp.layers = std::max(1, layers);
    lp.beats = beats; lp.loopBeats = loopBeats; lp.beatLen = beatLen * k;
    lp.playLen = pl > 0 ? std::min(m, (int)std::lround(pl * k)) : 0;
    lp.st = EchoLooper::STOP;
    lp.rebuildPeaks();
}
// keeps a recorded loop through EchoLooper::init() at a new sample rate (stopped, or playing on as it was)
inline void reinit(EchoLooper& lp, float sampleRate, float sec) {
    const bool keep = lp.len > 0 && lp.st != EchoLooper::REC, wasPlaying = lp.playing();
    const int n = lp.len, beats = lp.beats, loopBeats = lp.loopBeats, pl = lp.playLen, layers = lp.layers;
    const double beatLen = lp.beatLen;
    const float oldSr = lp.sr;
    std::vector<float> L, R;
    L.swap(lp.l); R.swap(lp.r);
    lp.init(sampleRate, sec);
    if (!keep) return;
    restore(lp, L.data(), R.data(), n, oldSr, beats, loopBeats, pl, beatLen, layers);
    if (wasPlaying) lp.st = EchoLooper::PLAY;
}
// read a loop file into lp (stopped); false if there is none or it is not one
inline bool readFile(const std::string& path, EchoLooper& lp) {
    if (!system::exists(path)) return false;
    std::vector<uint8_t> d;
    try { d = system::readFile(path); } catch (Exception&) { return false; }
    const size_t kHead = 4 + 8 * 4 + 8;
    if (d.size() < kHead || std::memcmp(d.data(), "SBLP", 4) != 0) return false;
    int32_t h[8];
    float fsr;
    double beatLen;
    std::memcpy(h, d.data() + 4, sizeof h);
    std::memcpy(&fsr, &h[1], 4);
    std::memcpy(&beatLen, d.data() + 4 + sizeof h, 8);
    const int n = h[2];
    const bool st2 = h[3] != 0;
    if (h[0] != 1 || n < 1 || !(fsr > 1000.f) || d.size() < kHead + (size_t)n * 4 * (st2 ? 2 : 1)) return false;
    std::vector<float> L((size_t)n), R;
    std::memcpy(L.data(), d.data() + kHead, (size_t)n * 4);
    if (st2) { R.resize((size_t)n); std::memcpy(R.data(), d.data() + kHead + (size_t)n * 4, (size_t)n * 4); }
    restore(lp, L.data(), st2 ? R.data() : L.data(), n, fsr, h[4], h[5], h[6], beatLen, h[7]);
    return true;
}
// write lp's loop (stereo) to path; an empty loop removes the file
inline void writeFile(const std::string& path, const EchoLooper& lp) {
    if (lp.len <= 0) { if (system::exists(path)) system::remove(path); return; }
    const size_t n = (size_t)lp.len;
    float fsr = lp.sr;
    int32_t h[8] = {1, 0, lp.len, 1, lp.beats, lp.loopBeats, lp.playLen, lp.layers};
    std::memcpy(&h[1], &fsr, 4);
    std::vector<uint8_t> d(4 + sizeof h + 8 + n * 8);
    std::memcpy(d.data(), "SBLP", 4);
    std::memcpy(d.data() + 4, h, sizeof h);
    std::memcpy(d.data() + 4 + sizeof h, &lp.beatLen, 8);
    std::memcpy(d.data() + 4 + sizeof h + 8, lp.l.data(), n * 4);
    std::memcpy(d.data() + 4 + sizeof h + 8 + n * 4, lp.r.data(), n * 4);
    system::writeFile(path, d);
}

} // namespace echoloop
} // namespace
