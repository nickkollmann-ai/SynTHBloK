// ============================================================================
// SynTHBloK-V  PLAYER VIEW  (expander for SynTHBloK Player Plus)
// Dock it directly to the right of the Player. Shows the loaded MIDI as a
// piano roll, one band per track, with a live playhead and per-track activity.
// ============================================================================
#include "plugin.hpp"
#include "PlayerLink.hpp"
#include "SynTHBloKPlayerView_layout.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <vector>

namespace {

struct NoteRect { float t0, t1; uint8_t note; };

struct LaneData {
    std::vector<NoteRect> notes;
    int minN = 127, maxN = 0;
    bool has = false;
};

std::string noteName(int n) {
    static const char* nm[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    n = std::min(127, std::max(0, n));
    char b[8];
    snprintf(b, sizeof b, "%s%d", nm[n % 12], n / 12 - 1);
    return b;
}

NVGcolor laneColor(int l, int a = 255) {
    // warm amber / orange family to match the rest of the BloK displays; lanes differ by hue and lightness
    float hue = 0.025f + 0.105f * (float) ((l * 5) % 16) / 15.f;
    float lig = (l % 2) ? 0.62f : 0.50f;
    return nvgHSLA(hue, 0.95f, lig, (unsigned char) a);
}

} // namespace

struct SynTHBloKPlayerView : Module {
    enum LightIds { LINK_LIGHT, NUM_LIGHTS };

    SynTHBloKPlayerView() {
        config(0, 0, 0, NUM_LIGHTS);
    }

    PlayerLink* link() {
        Module* lm = leftExpander.module;
        if (!lm) return nullptr;
        auto* prov = dynamic_cast<PlayerLinkProvider*>(lm);
        return prov ? prov->playerLink() : nullptr;
    }

    void process(const ProcessArgs& args) override {
        lights[LINK_LIGHT].setSmoothBrightness(link() ? 1.f : 0.f, args.sampleTime);
    }
};

// ---------------------------------------------------------------------------
struct PlayerViewDisplay : TransparentWidget {
    SynTHBloKPlayerView* module = nullptr;
    std::shared_ptr<window::Font> font;
    int mode = 1;                       // zoom: 0..3 = 2, 4, 8, 16 bars across
    uint32_t hidden = 0;                // lanes hidden from the roll
    uint32_t seenVersion = 0xffffffffu;
    LaneData lanes[16];
    std::vector<std::string> names;
    std::vector<double> clk;            // the song's 16th clock, so bar lines and the loop sit where the music really is
    double tickTime(double tick, double spb) const {
        if (clk.size() < 2) return tick * spb / 16.0;
        double mx = (double) (clk.size() - 1);
        if (tick <= 0.0) return clk[0] + tick * (clk[1] - clk[0]);
        if (tick >= mx) return clk.back() + (tick - mx) * (clk.back() - clk[clk.size() - 2]);
        size_t i = (size_t) tick; double f = tick - (double) i;
        return clk[i] + f * (clk[i + 1] - clk[i]);
    }
    double length = 0.0, bpm = 120.0;
    std::string label;
    int nlanes = 0;
    bool demo = false;
    int kind = 0;                       // 0 roll, 1 tracks/transport, 2 event log
    struct LogEv { float t; uint8_t lane, note, vel; };
    std::vector<LogEv> allOn;           // every note-on, sorted by time
    size_t logIdx = 0;
    double lastEl = -1.0;
    struct Line { double t; int lane, note, vel; };
    std::vector<Line> lines;
    float scrollOff = 0.f;
    float lvl[16] = {0};                // smoothed activity per track (gentle pulse instead of a flash)
    int lastNote[16] = {60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60};
    double lastT = 0.0;

    float sm[16] = {0}, imp[16] = {0};
    uint32_t prevAct = 0;
    void smooth(PlayerLink* lk) {
        double now = system::getTime();
        float dt = lastT > 0.0 ? (float) std::min(0.2, now - lastT) : 0.016f;
        lastT = now;
        uint32_t act = lk ? lk->activeMask.load() : 0;
        for (int l = 0; l < 16; l++) {
            bool on = (act >> l) & 1u;
            float target = on ? 1.f : 0.f;
            // quick follow of the gate, plus a sharper pulse on every new note or pitch change (even when legato)
            float tau = target > sm[l] ? 0.045f : 0.38f;
            sm[l] += (target - sm[l]) * (1.f - std::exp(-dt / tau));
            if (on && lk) {
                int nn = 60 + (int) std::lround(lk->pitchNow[l].load() * 12.f);
                if (!((prevAct >> l) & 1u) || nn != lastNote[l]) imp[l] = 1.f;
                lastNote[l] = nn;
            }
            imp[l] *= std::exp(-dt / 0.22f);
            lvl[l] = std::max(sm[l] * 0.75f, imp[l]);
        }
        prevAct = act;
    }

    void ensureFont() {
        if (!font) font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
    }
    void text(const DrawArgs& a, float x, float y, const std::string& s, float size, NVGcolor c, int align) {
        ensureFont();
        if (!font) return;
        nvgFontFaceId(a.vg, font->handle);
        nvgFontSize(a.vg, size);
        nvgFillColor(a.vg, c);
        nvgTextAlign(a.vg, align);
        nvgText(a.vg, x, y, s.c_str(), nullptr);
    }

    // rebuild the note lists whenever the Player's loaded files change (UI thread)
    void refresh(PlayerLink* lk) {
        uint32_t v = lk->version.load();
        if (v == seenVersion) return;
        std::vector<std::shared_ptr<synthblok::MidiSong>> songs;
        std::vector<int> starts;
        {
            std::lock_guard<std::mutex> g(lk->m);
            songs = lk->songs; starts = lk->starts; names = lk->laneNames; clk = lk->clock;
            length = lk->length; bpm = lk->bpm; nlanes = lk->lanes; label = lk->label;
        }
        for (auto& l : lanes) { l.notes.clear(); l.minN = 127; l.maxN = 0; l.has = false; }
        allOn.clear(); lines.clear(); logIdx = 0; lastEl = -1.0;
        for (size_t i = 0; i < songs.size(); i++) {
            const synthblok::MidiSong& s = *songs[i];
            static double open[16][128];
            for (int l = 0; l < 16; l++) for (int n = 0; n < 128; n++) open[l][n] = -1.0;
            for (const auto& e : s.ev) {
                int gl = (int) e.lane + starts[i];
                if (gl < 0 || gl >= 16) continue;
                if (e.on) {
                    allOn.push_back({(float) e.t, (uint8_t) gl, e.note, e.vel});
                    if (open[gl][e.note] < 0.0) open[gl][e.note] = e.t;
                } else if (open[gl][e.note] >= 0.0) {
                    double t0 = open[gl][e.note];
                    open[gl][e.note] = -1.0;
                    lanes[gl].notes.push_back({(float) t0, (float) e.t, e.note});
                }
            }
            for (int l = 0; l < 16; l++)
                for (int n = 0; n < 128; n++)
                    if (open[l][n] >= 0.0 && l - 0 >= 0) {
                        // the file's own lanes only: skip lanes that belong to another file
                        int fl = l - starts[i];
                        if (fl >= 0 && fl < s.lanes) lanes[l].notes.push_back({(float) open[l][n], (float) s.length, (uint8_t) n});
                    }
        }
        for (auto& l : lanes)
            for (auto& n : l.notes) { l.has = true; l.minN = std::min(l.minN, (int) n.note); l.maxN = std::max(l.maxN, (int) n.note); }
        std::stable_sort(allOn.begin(), allOn.end(), [](const LogEv& a, const LogEv& b) { return a.t < b.t; });
        seenVersion = v;
    }

    // ---- geometry shared by drawing and clicking ----
    static constexpr float PAD = 4.f;
    float rollH() const { return box.size.y; }
    static constexpr float LOOP_Y = 31.f, LOOP_H = 10.f, BTN_Y = 44.f, BTN_H = 12.f, PRE_Y = 59.f, PRE_H = 13.f;
    static float loopX0() { return PAD + 30.f; }
    static float loopW(float W) { return W - PAD - 2.f - loopX0(); }
    static constexpr int NBTN = 3, NPRE = 9, MAXLOOP = 128;       // loop length runs from 1 beat to 32 bars (128 beats)
    static float btnW(int i) { return i == 2 ? 32.f : 15.f; }
    static float preW(float W) { return (W - 2.f * PAD - 3.f * (NPRE - 1)) / (float) NPRE; }
    static float preX(float W, int i) { return PAD + (preW(W) + 3.f) * (float) i; }
    static int presetBeats(int i) { static const int v[NPRE] = {1, 2, 3, 4, 8, 16, 32, 64, 128}; return v[i]; }
    static float btnX(float W, int i) {                // -  +  FULL, right-aligned
        float x = W - PAD - 2.f;
        for (int k = NBTN - 1; k >= i; k--) { x -= btnW(k); if (k > i) x -= 3.f; }
        return x;
    }
    // loop strip drag state
    bool dragLoop = false, dragMoved = false; int anchorBar = 0; float dragX = 0.f;
    int beatAtX(float x, int tb) const {                     // beat under x (0-based)
        float bw = loopW(box.size.x) / (float) std::max(1, tb);
        return std::max(0, std::min(tb - 1, (int) std::floor((x - loopX0()) / bw)));
    }
    int snapUnit(int tb) const { return loopW(box.size.x) / (float) std::max(1, tb) >= 4.f ? 1 : 4; }   // beats per drag step
    static void setLoop(PlayerLink* lk, int start, int len, int bt) {
        if (len <= 0) { lk->loopStart = 0; lk->loopLen = 0; return; }
        len = std::min(len, MAXLOOP);
        if (start <= 0 && len >= bt) { lk->loopStart = 0; lk->loopLen = 0; return; }
        start = std::max(0, std::min(bt - 1, start)); len = std::max(1, std::min(len, bt - start));
        lk->loopStart = start; lk->loopLen = len;
    }
    int lastLen = 4;                                   // last loop length chosen (beats): a click on the strip drops a loop of this size
    void applyLoop(PlayerLink* lk, int start, int len, int bt) {
        setLoop(lk, start, len, bt);
        int n = lk->loopLen.load();
        if (n > 0) lastLen = n;
    }
    bool inRoll(Vec p) const { return true; }

    void drawRoll(const DrawArgs& a, PlayerLink* lk, float W, float H) {
        std::vector<int> vis;
        const uint32_t muted = lk ? lk->muteMask.load() : 0u;
        for (int l = 0; l < 16; l++) if (lanes[l].has && !(hidden & (1u << l))) vis.push_back(l);
        if (vis.empty()) {
            text(a, W * 0.5f, H * 0.5f, lk ? "NO MIDI LOADED  -  LOAD A FILE IN THE PLAYER" : "DOCK ME TO THE RIGHT OF THE PLAYER", 11.f,
                 nvgRGBA(255, 190, 90, 150), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            return;
        }
        float gx = 16.f, rx = gx + 3.f, rw = W - rx - PAD;
        float top = PAD, avail = H - 2.f * PAD;
        float bh = avail / (float) vis.size();
        // scrolling view: the playhead stays put and the music moves past it
        static const double ZB[4] = {2.0, 4.0, 8.0, 16.0};
        double spb = clk.size() > 16 ? (clk.back() - clk.front()) * 16.0 / (double) (clk.size() - 1) : 240.0 / std::max(20.0, bpm);   // average seconds per bar
        int md = mode & 3;
        double lsT = 0.0, leT = 0.0;
        const bool loopOn = lk && lk->loopLen.load() > 0;
        if (loopOn) {
            lsT = tickTime(lk->loopStart.load() * 4.0, spb); leT = tickTime((lk->loopStart.load() + lk->loopLen.load()) * 4.0, spb);
            while (md < 3 && leT - lsT > 0.84 * ZB[md] * spb) md++;      // zoom out far enough to show the whole loop
        }
        double win = ZB[md] * spb;                               // seconds across the roll
        double el = lk ? lk->elapsed.load() : 0.0;
        double t0w = el - 0.2 * win;                              // playhead sits 20% in from the left
        // a loop is set: hold the view still on it (when it fits) so the highlighted area shows where you picked it
        if (loopOn && leT - lsT <= 0.84 * win) t0w = lsT - 0.08 * win;
        double t1w = t0w + win;
        double pps = rw / win;                                    // pixels per second
        float phx = rx + (float) ((el - t0w) * pps);

        // bar grid, placed from the song's own clock
        long b0 = std::max(0L, (long) std::floor(t0w / spb) - 3);
        for (long bi = b0; bi < 4096; bi++) {
            double tb2 = tickTime((double) bi * 16.0, spb);
            if (tb2 > t1w) break;
            float x = rx + (float) ((tb2 - t0w) * pps);
            if (x < rx || x > rx + rw) continue;
            bool major = (bi % 4) == 0;
            nvgBeginPath(a.vg); nvgMoveTo(a.vg, x, top); nvgLineTo(a.vg, x, top + avail);
            nvgStrokeWidth(a.vg, 0.5f);
            nvgStrokeColor(a.vg, nvgRGBA(255, 180, 90, major ? 55 : 22));
            nvgStroke(a.vg);
        }

        for (size_t k = 0; k < vis.size(); k++) {
            int l = vis[k];
            float y0 = top + bh * (float) k;
            const float lv = 0.f;           // no trigger flashing: the roll is a steady scrolling view
            const bool mu = (muted >> l) & 1u;
            // band background
            nvgBeginPath(a.vg); nvgRoundedRect(a.vg, rx, y0 + 0.5f, rw, bh - 1.f, 2.f);
            nvgFillColor(a.vg, nvgRGBA((int) (26 + 34 * lv), (int) (17 + 19 * lv), (int) (6 + 2 * lv), 255));
            nvgFill(a.vg);
            // lane number
            char b[8]; snprintf(b, sizeof b, "%02d", l + 1);
            {   const float nl = lvl[l];   // channel number badge: the only part that pulses (swells and fades with the track's activity)
                float cy = y0 + bh * 0.5f, bwid = 14.f, bhei = std::min(12.f, bh - 2.f);
                nvgBeginPath(a.vg); nvgRoundedRect(a.vg, gx - bwid - 1.f, cy - bhei * 0.5f, bwid, bhei, 3.f);
                nvgFillColor(a.vg, mu ? nvgRGBA(150, 30, 20, 150) : laneColor(l, (int) (10 + 120 * nl))); nvgFill(a.vg);
                nvgStrokeWidth(a.vg, 0.7f); nvgStrokeColor(a.vg, laneColor(l, (int) (70 + 150 * nl))); nvgStroke(a.vg);
                int w = (int) (120 + 135 * nl);
                text(a, gx - bwid * 0.5f - 1.f, cy, b, 8.f, nvgRGBA(255, (int) (170 + 60 * nl), (int) (70 + 100 * nl), w), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            }

            // notes
            const LaneData& ld = lanes[l];
            int range = ld.maxN - ld.minN + 1;
            float nh = std::min(6.f, std::max(1.2f, (bh - 3.f) / (float) range));
            float usable = bh - 3.f - nh;
            nvgSave(a.vg);
            nvgScissor(a.vg, rx, y0, rw, bh);
            nvgBeginPath(a.vg);
            for (const auto& n : ld.notes) {
                if (n.t1 < t0w || n.t0 > t1w) continue;
                float x0 = rx + (float) ((n.t0 - t0w) * pps);
                float x1 = rx + (float) ((n.t1 - t0w) * pps);
                float w = std::max(1.f, x1 - x0);
                float yy = range > 1 ? y0 + 1.5f + usable * (float) (ld.maxN - n.note) / (float) (range - 1) : y0 + bh * 0.5f - nh * 0.5f;
                nvgRect(a.vg, x0, yy, w, nh);
            }
            nvgFillColor(a.vg, mu ? laneColor(l, 55) : laneColor(l, (int) (200 + 55 * lv)));
            nvgFill(a.vg);
            nvgRestore(a.vg);
        }

        // loop region: darken everything outside it and mark its ends
        if (lk && lk->loopLen.load() > 0) {
            double ls = lsT, le = leT;
            float xs = rx + (float) ((ls - t0w) * pps), xe = rx + (float) ((le - t0w) * pps);
            float cx0 = std::max(rx, std::min(rx + rw, xs)), cx1 = std::max(rx, std::min(rx + rw, xe));
            if (cx1 > cx0) {                                   // the loop area itself: warm tint with a bright cap top and bottom
                nvgBeginPath(a.vg); nvgRect(a.vg, cx0, top, cx1 - cx0, avail);
                nvgFillColor(a.vg, nvgRGBA(255, 140, 30, 38)); nvgFill(a.vg);
                nvgBeginPath(a.vg); nvgRect(a.vg, cx0, top, cx1 - cx0, 2.f); nvgRect(a.vg, cx0, top + avail - 2.f, cx1 - cx0, 2.f);
                nvgFillColor(a.vg, nvgRGBA(255, 130, 20, 230)); nvgFill(a.vg);
                if (cx1 - cx0 > 30.f) text(a, cx0 + 4.f, top + 4.f, "LOOP", 7.f, nvgRGBA(255, 170, 70, 230), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            }
            nvgFillColor(a.vg, nvgRGBA(0, 0, 0, 120));
            nvgBeginPath(a.vg); nvgRect(a.vg, rx, top, cx0 - rx, avail); nvgFill(a.vg);
            nvgBeginPath(a.vg); nvgRect(a.vg, cx1, top, rx + rw - cx1, avail); nvgFill(a.vg);
            nvgStrokeWidth(a.vg, 1.5f); nvgStrokeColor(a.vg, nvgRGBA(255, 120, 20, 230));
            if (xs >= rx && xs <= rx + rw) { nvgBeginPath(a.vg); nvgMoveTo(a.vg, xs, top); nvgLineTo(a.vg, xs, top + avail); nvgStroke(a.vg); }
            if (xe >= rx && xe <= rx + rw) { nvgBeginPath(a.vg); nvgMoveTo(a.vg, xe, top); nvgLineTo(a.vg, xe, top + avail); nvgStroke(a.vg); }
        }

        // fixed playhead
        nvgBeginPath(a.vg); nvgMoveTo(a.vg, phx, top); nvgLineTo(a.vg, phx, top + avail);
        nvgStrokeWidth(a.vg, 1.2f); nvgStrokeColor(a.vg, nvgRGBA(255, 226, 170, 230)); nvgStroke(a.vg);
        char zb[16]; snprintf(zb, sizeof zb, "%d BARS", (int) ZB[md]);
        text(a, W - PAD - 2.f, top + 2.f, zb, 7.f, nvgRGBA(255, 190, 90, 150), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
    }

    void drawInfo(const DrawArgs& a, PlayerLink* lk, float W, float H) {
        NVGcolor amb = nvgRGBA(255, 190, 90, 255), dim = nvgRGBA(255, 190, 90, 130);
        std::string lab = lk && !label.empty() ? label : "NO FILE";
        if (lab.size() > 44) lab = lab.substr(0, 43) + "~";
        text(a, PAD + 2.f, 3.f, lab, 11.f, amb, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

        double len = length;
        int bt = lk ? lk->barsTotal.load() : 0, bn = lk ? lk->barNow.load() : 0;
        double el = lk ? lk->elapsed.load() : 0.0;
        char b[96];
        snprintf(b, sizeof b, "%.0f BPM   %d BARS   %d:%02d", bpm, bt, (int) (len / 60.0), (int) std::fmod(len, 60.0));
        text(a, PAD + 2.f, 17.f, b, 9.f, dim, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        bool run = lk && lk->running.load();
        snprintf(b, sizeof b, "%s  BAR %d/%d   %d:%02d", run ? "PLAY" : "STOP", bn, bt, (int) (el / 60.0), (int) std::fmod(el, 60.0));
        text(a, W - PAD - 2.f, 16.f, b, 10.f, run ? nvgRGBA(120, 230, 90, 255) : dim, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

        // ---- loop strip: drag across the song to loop it (snaps to bars, or beats when zoomed enough) ----
        {
            int tb = std::max(4, bt * 4);                         // song length in beats
            int ls = lk ? lk->loopStart.load() : 0, ll = lk ? lk->loopLen.load() : 0;
            bool act = ll > 0 && !(ls == 0 && ls + ll >= tb);
            float sx = loopX0(), sw = loopW(W), sy = LOOP_Y, sh = LOOP_H;
            text(a, PAD + 2.f, sy + sh * 0.5f, "LOOP", 8.f, dim, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgBeginPath(a.vg); nvgRoundedRect(a.vg, sx, sy, sw, sh, 2.f); nvgFillColor(a.vg, nvgRGBA(40, 25, 8, 255)); nvgFill(a.vg);
            float bw = sw / (float) tb;
            if (act) {
                nvgBeginPath(a.vg); nvgRoundedRect(a.vg, sx + bw * (float) ls, sy, bw * (float) std::min(ll, tb - ls), sh, 2.f);
                nvgFillColor(a.vg, nvgRGBA(255, 112, 15, 230)); nvgFill(a.vg);
            }
            if (bt > 0) {
                nvgBeginPath(a.vg);
                for (int k = 1; k < tb; k++) {
                    bool bar = (k % 4) == 0;
                    if (bar ? (bw * 4.f < 2.f && (k / 4) % 4) : bw < 3.f) continue;
                    nvgMoveTo(a.vg, sx + bw * (float) k, bar ? sy : sy + sh * 0.45f); nvgLineTo(a.vg, sx + bw * (float) k, sy + sh);
                }
                nvgStrokeWidth(a.vg, 0.6f); nvgStrokeColor(a.vg, nvgRGBA(14, 9, 3, 200)); nvgStroke(a.vg);
                double pos = lk ? lk->elapsed.load() : 0.0;
                float px = sx + sw * (float) std::min(1.0, pos / std::max(1e-3, length));
                nvgBeginPath(a.vg); nvgMoveTo(a.vg, px, sy - 1.f); nvgLineTo(a.vg, px, sy + sh + 1.f);
                nvgStrokeWidth(a.vg, 1.4f); nvgStrokeColor(a.vg, nvgRGBA(255, 236, 170, 240)); nvgStroke(a.vg);
            }
            nvgBeginPath(a.vg); nvgRoundedRect(a.vg, sx + 0.5f, sy + 0.5f, sw - 1.f, sh - 1.f, 2.f);
            nvgStrokeWidth(a.vg, 0.7f); nvgStrokeColor(a.vg, nvgRGBA(255, 180, 90, 90)); nvgStroke(a.vg);
            // readout and the quick buttons
            char lb[64];
            if (bt == 0) snprintf(lb, sizeof lb, "DRAG OVER THE SONG TO LOOP");
            else if (act) {
                int n = std::min(ll, tb - ls);
                char ln[24];
                if (n % 4 == 0) snprintf(ln, sizeof ln, "%d BAR%s", n / 4, n == 4 ? "" : "S");
                else snprintf(ln, sizeof ln, "%d BEAT%s", n, n == 1 ? "" : "S");
                snprintf(lb, sizeof lb, "BAR %d.%d  %s", ls / 4 + 1, ls % 4 + 1, ln);
            } else snprintf(lb, sizeof lb, "WHOLE SONG  (%d BARS)", bt);
            text(a, PAD + 2.f, BTN_Y + BTN_H * 0.5f, lb, 9.f, act ? nvgRGBA(255, 130, 30, 255) : dim, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            static const char* bn2[NBTN] = {"-", "+", "FULL"};
            for (int i = 0; i < NBTN; i++) {
                float bx = btnX(W, i), bwid = btnW(i);
                bool on3 = (i == 2 && !act);
                nvgBeginPath(a.vg); nvgRoundedRect(a.vg, bx, BTN_Y, bwid, BTN_H, 2.5f);
                nvgFillColor(a.vg, on3 ? nvgRGBA(255, 176, 60, 70) : nvgRGBA(40, 25, 8, 255)); nvgFill(a.vg);
                nvgStrokeWidth(a.vg, 0.8f); nvgStrokeColor(a.vg, nvgRGBA(255, 180, 90, on3 ? 200 : 120)); nvgStroke(a.vg);
                text(a, bx + bwid * 0.5f, BTN_Y + BTN_H * 0.5f, bn2[i], 8.f, amb, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            }
            // one-tap lengths: 1, 2, 3 beats, then 1 bar and up
            static const char* pl[NPRE] = {"1 BEAT", "2 BEATS", "3 BEATS", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS", "32 BARS"};
            for (int i = 0; i < NPRE; i++) {
                float px2 = preX(W, i), pw = preW(W);
                bool cur = act && ll == presetBeats(i);
                nvgBeginPath(a.vg); nvgRoundedRect(a.vg, px2, PRE_Y, pw, PRE_H, 2.5f);
                nvgFillColor(a.vg, cur ? nvgRGBA(255, 112, 15, 235) : nvgRGBA(40, 25, 8, 255)); nvgFill(a.vg);
                nvgStrokeWidth(a.vg, 0.8f); nvgStrokeColor(a.vg, nvgRGBA(255, 180, 90, cur ? 255 : (i < 3 ? 150 : 100))); nvgStroke(a.vg);
                text(a, px2 + pw * 0.5f, PRE_Y + PRE_H * 0.5f, pl[i], 7.f, cur ? nvgRGBA(20, 10, 2, 255) : amb, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            }
        }

        // track chips: 8 x 2
        float cw = (W - 2.f * PAD) / 8.f, ch = 24.f, y0 = H - 2.f * ch - PAD - 2.f;
        for (int l = 0; l < 16; l++) {
            const float lv = 0.f;
            float x = PAD + cw * (float) (l % 8), y = y0 + ch * (float) (l / 8);
            bool used = lanes[l].has;
            bool hid = (hidden >> l) & 1u;
            bool mu = used && (((lk ? lk->muteMask.load() : 0u) >> l) & 1u);
            nvgBeginPath(a.vg); nvgRoundedRect(a.vg, x + 1.f, y + 1.f, cw - 2.f, ch - 2.f, 2.5f);
            nvgFillColor(a.vg, mu ? nvgRGBA(70, 14, 8, 255) : used ? laneColor(l, (int) (12 + 60 * lv)) : nvgRGBA(26, 17, 6, 255));
            nvgFill(a.vg);
            nvgStrokeWidth(a.vg, 0.7f + 0.7f * lv + (mu ? 0.5f : 0.f));
            nvgStrokeColor(a.vg, mu ? nvgRGBA(235, 50, 30, 230) : used ? laneColor(l, hid ? 80 : (int) (170 + 85 * lv)) : nvgRGBA(255, 190, 90, 25));
            nvgStroke(a.vg);
            if (!used) {
                snprintf(b, sizeof b, "%02d", l + 1);
                text(a, x + cw * 0.5f, y + ch * 0.5f, b, 9.f, nvgRGBA(255, 190, 90, 40), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                continue;
            }
            snprintf(b, sizeof b, "%02d", l + 1);
            text(a, x + 4.f, y + 4.f, b, 9.f, mu ? nvgRGBA(255, 90, 70, 255) : hid ? nvgRGBA(255, 190, 90, 90) : laneColor(l, 255), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            if (mu) text(a, x + cw - 4.f, y + 4.f, "M", 8.f, nvgRGBA(255, 90, 70, 255), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
            std::string nm = l < (int) names.size() ? names[l] : "";
            if (nm.size() > 8) nm = nm.substr(0, 8);
            text(a, x + 4.f, y + 15.f, nm, 7.f, mu ? nvgRGBA(255, 110, 90, 110) : nvgRGBA(255, 190, 90, hid ? 60 : 140), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        }
    }

    void drawLayer(const DrawArgs& a, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(a.vg); nvgRoundedRect(a.vg, 0, 0, W, H, 3.f); nvgFillColor(a.vg, nvgRGBA(14, 9, 3, 255)); nvgFill(a.vg);
        PlayerLink* lk = module ? module->link() : nullptr;
        smooth(lk);
        if (lk) refresh(lk);
        else if (module) { seenVersion = 0xffffffffu; for (auto& l : lanes) { l.notes.clear(); l.has = false; } label.clear(); }
        if (isRoll) drawRoll(a, lk, W, H); else drawInfo(a, lk, W, H);
        nvgBeginPath(a.vg); nvgRoundedRect(a.vg, 0.5f, 0.5f, W - 1.f, H - 1.f, 3.f);
        nvgStrokeWidth(a.vg, 0.8f); nvgStrokeColor(a.vg, nvgRGBA(255, 180, 90, 80)); nvgStroke(a.vg);
    }

    bool isRoll = true;
    PlayerViewDisplay* peer = nullptr;       // the other display, so the roll sees mode / hidden changes

    void onButton(const event::Button& e) override {
        if (e.action != GLFW_PRESS) return;
        PlayerViewDisplay* roll = isRoll ? this : peer;
        if (!roll) return;
        if (isRoll) {
            if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
            roll->mode = (roll->mode + 1) % 4;
            e.consume(this);
            return;
        }
        PlayerLink* lk = module ? module->link() : nullptr;
        float W = box.size.x, H = box.size.y;
        int bt = lk ? lk->barsTotal.load() : 0;
        // loop strip and quick buttons
        int tb = bt * 4;
        if (lk && bt > 0) {
            bool inStrip = e.pos.x >= loopX0() && e.pos.x <= loopX0() + loopW(W) && e.pos.y >= LOOP_Y - 2.f && e.pos.y <= LOOP_Y + LOOP_H + 2.f;
            if (inStrip) {
                if (e.button == GLFW_MOUSE_BUTTON_RIGHT) { applyLoop(lk, 0, 0, tb); e.consume(this); return; }
                if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
                    int u = snapUnit(tb);
                    anchorBar = beatAtX(e.pos.x, tb) / u * u; dragX = e.pos.x; dragLoop = true; dragMoved = false;
                    int len = std::max(1, std::min(std::min(lastLen, MAXLOOP), tb));
                    applyLoop(lk, std::min(beatAtX(e.pos.x, tb) / len * len, tb - len), len, tb);      // click = loop here, same length as last time, in step
                    e.consume(this); return;
                }
            }
            int ls = lk->loopStart.load(), ll = lk->loopLen.load();
            bool act = ll > 0 && !(ls == 0 && ls + ll >= tb);
            int ns = act ? ls : std::max(0, std::min(tb - 1, (lk->barNow.load() - 1) * 4));   // no loop yet: start at the playing bar
            if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.pos.y >= BTN_Y && e.pos.y <= BTN_Y + BTN_H) {
                for (int i = 0; i < NBTN; i++) {
                    if (e.pos.x < btnX(W, i) || e.pos.x > btnX(W, i) + btnW(i)) continue;
                    int cur = act ? ll : std::min(tb, 4), nl = cur;
                    if (i == 2) nl = 0;                                  // FULL
                    else if (i == 0) nl = std::max(1, cur - 1);          // one beat shorter
                    else nl = std::min(MAXLOOP, std::min(tb, cur + 1));  // one beat longer
                    if (nl > 0 && ns + nl > tb) ns = std::max(0, tb - nl);
                    applyLoop(lk, ns, nl, tb);
                    e.consume(this); return;
                }
            }
            if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.pos.y >= PRE_Y && e.pos.y <= PRE_Y + PRE_H) {
                for (int i = 0; i < NPRE; i++) {
                    if (e.pos.x < preX(W, i) || e.pos.x > preX(W, i) + preW(W)) continue;
                    int nl = std::min(presetBeats(i), tb);
                    if (act && ll == nl) nl = 0;                         // tap the lit one again to clear the loop
                    if (nl > 0 && ns + nl > tb) ns = std::max(0, tb - nl);
                    applyLoop(lk, ns, nl, tb);
                    e.consume(this); return;
                }
            }
        }
        // track chips: click = mute, shift-click = solo, right-click = hide in the roll
        float cw = (W - 2.f * PAD) / 8.f, ch = 24.f, y0 = H - 2.f * ch - PAD - 2.f;
        int col = (int) ((e.pos.x - PAD) / cw), row = (int) ((e.pos.y - y0) / ch);
        if (e.pos.x < PAD || col < 0 || col > 7 || row < 0 || row > 1) return;
        int l = row * 8 + col;
        if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
            roll->hidden ^= (1u << l);
            hidden = roll->hidden;
        } else if (e.button == GLFW_MOUSE_BUTTON_LEFT && lk && roll->lanes[l].has) {
            if (e.mods & GLFW_MOD_SHIFT) {
                uint32_t used = 0;
                for (int k = 0; k < 16; k++) if (roll->lanes[k].has) used |= 1u << k;
                uint32_t others = used & ~(1u << l);
                lk->muteMask = (lk->muteMask.load() == others) ? 0u : others;      // again = unsolo
            } else {
                lk->muteMask ^= (1u << l);
            }
        } else return;
        e.consume(this);
    }
    // wheel over the loop strip: change the length one beat at a time, shift-wheel slides the loop a beat
    void onHoverScroll(const event::HoverScroll& e) override {
        if (isRoll || !module) return;
        PlayerLink* lk = module->link();
        int bt = lk ? lk->barsTotal.load() : 0, tb = bt * 4;
        float W = box.size.x;
        if (!lk || bt <= 0 || e.pos.x < loopX0() || e.pos.x > loopX0() + loopW(W) || e.pos.y < LOOP_Y - 3.f || e.pos.y > LOOP_Y + LOOP_H + 3.f) return;
        int step = e.scrollDelta.y > 0 ? -1 : 1;                  // wheel up = longer
        int ls = lk->loopStart.load(), ll = lk->loopLen.load();
        bool act = ll > 0 && !(ls == 0 && ls + ll >= tb);
        if (!act) { int b0 = std::max(0, std::min(tb - 1, (lk->barNow.load() - 1) * 4)); applyLoop(lk, b0, std::min(4, tb - b0), tb); }
        else if (APP->window->getMods() & GLFW_MOD_SHIFT) applyLoop(lk, std::max(0, std::min(tb - ll, ls - step)), ll, tb);
        else {
            int nl = std::max(1, std::min(std::min(MAXLOOP, tb), ll - step));
            int ns = ls; if (ns + nl > tb) ns = std::max(0, tb - nl);
            applyLoop(lk, ns, nl, tb);
        }
        e.consume(this);
    }
    void onDragStart(const event::DragStart& e) override {
        if (!dragLoop) return;
    }
    void onDragMove(const event::DragMove& e) override {
        if (!dragLoop) return;
        PlayerLink* lk = module ? module->link() : nullptr;
        int bt = lk ? lk->barsTotal.load() : 0, tb = bt * 4;
        if (!lk || bt <= 0) return;
        dragX += e.mouseDelta.x / getAbsoluteZoom();
        int u = snapUnit(tb);
        int c = beatAtX(dragX, tb) / u * u;
        if (!dragMoved && c == anchorBar) return;                  // not left the first cell yet: keep the click's loop
        dragMoved = true;
        int st = std::min(anchorBar, c), len = std::abs(c - anchorBar) + u;
        if (len > MAXLOOP) { len = MAXLOOP; st = c < anchorBar ? anchorBar + u - MAXLOOP : anchorBar; }
        applyLoop(lk, st, len, tb);
    }
    void onDragEnd(const event::DragEnd& e) override { dragLoop = false; }
    void step() override {
        TransparentWidget::step();
        if (!isRoll && peer) hidden = peer->hidden;
    }
};

// out-of-class definitions (C++11 needs them when a static constexpr is bound to a reference, e.g. by std::min)
constexpr float PlayerViewDisplay::PAD;
constexpr int PlayerViewDisplay::NBTN;
constexpr int PlayerViewDisplay::NPRE;
constexpr float PlayerViewDisplay::PRE_Y;
constexpr float PlayerViewDisplay::PRE_H;
constexpr int PlayerViewDisplay::MAXLOOP;
constexpr float PlayerViewDisplay::LOOP_Y;
constexpr float PlayerViewDisplay::LOOP_H;
constexpr float PlayerViewDisplay::BTN_Y;
constexpr float PlayerViewDisplay::BTN_H;

// ---------------------------------------------------------------------------
struct SynTHBloKPlayerViewWidget : ModuleWidget {
    SynTHBloKPlayerViewWidget(SynTHBloKPlayerView* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKPlayerView.svg")));
        using namespace rack::componentlibrary;

        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PVL::PANEL_W - 3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 125.f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PVL::PANEL_W - 3.5f, 125.f))));

        auto* roll = new PlayerViewDisplay();
        roll->module = module; roll->isRoll = true;
        roll->box.pos = mm2px(Vec(PVL::D1[0], PVL::D1[1]));
        roll->box.size = mm2px(Vec(PVL::D1[2], PVL::D1[3]));
        addChild(roll);

        auto* info = new PlayerViewDisplay();
        info->module = module; info->isRoll = false; info->peer = roll;
        info->box.pos = mm2px(Vec(PVL::D2[0], PVL::D2[1]));
        info->box.size = mm2px(Vec(PVL::D2[2], PVL::D2[3]));
        addChild(info);
        roll->peer = info;
    }
};

Model* modelSynTHBloKPlayerView = createModel<SynTHBloKPlayerView, SynTHBloKPlayerViewWidget>("PlayerView");
