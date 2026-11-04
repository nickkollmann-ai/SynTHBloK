// ============================================================================
// SynTHBloK MATRIX-SONG  (the conductor: sections A-H across every MATRIX module in the patch)
//  The grid: a row per MATRIX module (found by itself through SongLink.hpp, no cables), a column per song
//  section. A pad holds what that module does in that section: its pattern (or keeps it), a mute, a loop
//  action (PLAY / STOP / REC / ONCE) for the ECHO / FX / LOOP loopers, and its level.
//  A section header launches that section on the next bar (or beat).
//  The strips (as MATRIX-DRUM's): a level, a MUTE and an output meter per row's module. Each section keeps
//  its own levels and mutes; a launch moves the strips to them, and moving a strip edits the playing section.
//  REC records a performance: from the next bar, the launches, mutes and strip moves (on the 16th grid) until
//  the bar after REC goes off. PLAY replays the take from its first bar, round and round.
//  Master clock: CLOCK (16ths), RUN, RESET, BAR and SECT (0-7 V) out; CLOCK in (16ths) or the BPM knob.
// ============================================================================
#include "plugin.hpp"
#include "SongLink.hpp"
#include "OledUi.hpp"
#include "EchoLoopUi.hpp"
#include "Matrix8_ui.hpp"
#include "JackLed.hpp"
#include "KnobTicks.hpp"
#include <atomic>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace {

static const int kRows = 8, kSecs = 8, kEvMax = 20000;
static const char* const kSecName[kSecs] = {"A", "B", "C", "D", "E", "F", "G", "H"};
// rows (and their strips) in the fire colours the MATRIX modules all use, as the section columns are
static const NVGcolor* const kRowCol = kPatOnColor;
static const char* const kLoopGlyph[songlink::L_COUNT] = {"", "PLAY", "STOP", "REC", "ONCE"};

struct SynTHBloKMatrixSong : Module {
    enum ParamIds { RUN_PARAM, RESET_PARAM, REC_PARAM, PLAY_PARAM, QUANT_PARAM, BPM_PARAM, NUM_PARAMS };
    enum InputIds { CLOCK_INPUT, RUN_INPUT, RESET_INPUT, NUM_INPUTS };
    enum OutputIds { CLOCK_OUTPUT, RUN_OUTPUT, RESET_OUTPUT, BAR_OUTPUT, SECT_OUTPUT, NUM_OUTPUTS };

    struct Cell { int pat = -1; bool mute = false; int loop = songlink::L_NONE; float level = 1.f; };
    Cell cells[kSecs][kRows];
    int64_t rowId[kRows];
    // the strips now (a launch loads them from the section; a strip move edits the playing section)
    float lev[kRows];
    bool mut[kRows];

    // the take: what happened, tick by tick (16 a bar) from its first bar, and the state it started from
    enum { EV_SEC, EV_LEV, EV_MUTE };
    struct Ev { int t, type, row; float v; };
    std::vector<Ev> take, rec;
    int takeLen = 0, takeSec = 0;            // takeLen: ticks, 0 = no take
    float takeLev[kRows];
    bool takeMut[kRows];
    std::mutex takeM;                        // the take, against saving (rec is the engine's alone)
    enum { R_OFF, R_ARM, R_ON, R_END };      // REC: waiting for the bar, recording, ending on the next bar
    enum { P_OFF, P_ARM, P_ON };             // PLAY: waiting for the bar, playing
    int recState = R_OFF, playState = P_OFF, recT = 0, playT = 0;
    size_t playIx = 0;
    int recSec = 0;
    float recLev[kRows], recLastLev[kRows];
    bool recMut[kRows], recLastMut[kRows];

    // the song now
    int curSec = 0, barInSec = 0;
    std::atomic<int> queued{-1};      // a launch waiting for its bar (or beat)
    std::atomic<int> applyNow{-1};    // stopped: a launch applies at once
    bool wasRunning = false, recPrev = false, playPrev = false;
    int selSec = 0, selRow = 0;       // the pad the editor shows (UI)
    int pushCount = 0;
    std::atomic<bool> clearTakeReq{false};   // UI: clear the take (done in process, where it is played)

    // clock
    int tick = -1;
    bool running = true;
    float sinceClock = 10.f, clockPeriod = 0.f, internalTimer = 0.f;
    dsp::SchmittTrigger clkTrig, runTrig, rstTrig, rstBtnTrig;
    dsp::PulseGenerator clockPulse, resetPulse, barPulse;
    float resetFlash = 0.f, blink = 0.f;

    SynTHBloKMatrixSong() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
        configSwitch(RUN_PARAM, 0.f, 1.f, 0.f, "Run (starts the song from the top; a trigger at RUN toggles it)", {"Stopped", "Running"});
        configButton(RESET_PARAM, "Reset: the song from the top (a trigger at RESET does too)");
        configSwitch(REC_PARAM, 0.f, 1.f, 0.f, "Record a take: launches, mutes and strip moves, from the next bar until the bar after REC goes off", {"Off", "Recording"});
        configSwitch(PLAY_PARAM, 0.f, 1.f, 0.f, "Play the take (from the next bar, round and round)", {"Off", "Playing"});
        configSwitch(QUANT_PARAM, 0.f, 1.f, 0.f, "Launches land on the next", {"Bar", "Beat"});
        configParam(BPM_PARAM, 40.f, 240.f, 120.f, "Tempo (when CLOCK is unpatched)", " BPM");
        paramQuantities[BPM_PARAM]->snapEnabled = true;
        for (int id : {RUN_PARAM, REC_PARAM, PLAY_PARAM, QUANT_PARAM}) paramQuantities[id]->randomizeEnabled = false;
        configInput(CLOCK_INPUT, "Clock (16th notes; the BPM knob when unpatched)");
        configInput(RUN_INPUT, "Run (trigger: toggles RUN)");
        configInput(RESET_INPUT, "Reset (trigger)");
        configOutput(CLOCK_OUTPUT, "Clock (16th notes, while running)");
        configOutput(RUN_OUTPUT, "Run (gate)");
        configOutput(RESET_OUTPUT, "Reset (trigger as the song starts or resets)");
        configOutput(BAR_OUTPUT, "Bar (trigger on each downbeat)");
        configOutput(SECT_OUTPUT, "Section (A = 0 V .. H = 7 V)");
        for (int r = 0; r < kRows; r++) { rowId[r] = -1; takeLev[r] = 1.f; takeMut[r] = false; }
        take.reserve(kEvMax);
        rec.reserve(kEvMax);
        defaults();
    }

    void defaults() {
        for (int s = 0; s < kSecs; s++) for (int r = 0; r < kRows; r++) cells[s][r] = Cell();
        for (int s = 0; s < kSecs; s++) for (int r = 0; r < kRows; r++) cells[s][r].pat = s;   // A..H: patterns 1..8
        curSec = 0; selSec = selRow = 0;
        loadLive(0);
        std::lock_guard<std::mutex> lk(takeM);
        take.clear(); takeLen = 0;
        recState = R_OFF; playState = P_OFF;
    }
    void onReset() override {
        defaults();
        for (int r = 0; r < kRows; r++) rowId[r] = -1;
        tick = -1;
    }

    float bpmNow() {
        if (inputs[CLOCK_INPUT].isConnected() && clockPeriod > 0.f) return 15.f / clockPeriod;
        return params[BPM_PARAM].getValue();
    }
    bool takePlaying() { return playState == P_ON; }
    void loadLive(int s) { for (int r = 0; r < kRows; r++) { lev[r] = cells[s][r].level; mut[r] = cells[s][r].mute; } }

    // UI: a strip / MUTE moves the live state, and writes into the playing section (not while a take plays,
    // so replaying never rewrites the sections)
    void setLevel(int r, float v) { lev[r] = clamp(v, 0.f, 1.f); if (!takePlaying()) cells[curSec][r].level = lev[r]; }
    void toggleMute(int r) { mut[r] = !mut[r]; if (!takePlaying()) cells[curSec][r].mute = mut[r]; }
    // UI: the editor changed section s; if it is the one playing, the strips follow and the modules switch
    // to its patterns now (its loop actions wait for its next launch)
    void edited(int s) {
        if (s != curSec || takePlaying()) return;
        loadLive(s);
        for (int r = 0; r < kRows; r++) {
            int p = cells[s][r].pat;
            if (rowId[r] < 0 || p < 0) continue;
            songlink::with(rowId[r], [&](songlink::Member& m) { if (m.songPatterns > 0) m.reqPattern = std::min(p, m.songPatterns - 1); });
        }
    }
    // a row let go (emptied, or SONG removed): its module back to full level, unmuted
    void release(int r) {
        if (rowId[r] < 0) return;
        songlink::with(rowId[r], [](songlink::Member& m) { m.songLevel = 1.f; m.songMuted = false; m.songLinked = false; });
    }
    ~SynTHBloKMatrixSong() { for (int r = 0; r < kRows; r++) release(r); }

    // switch every row's module to section s
    void apply(int s) {
        s = clamp(s, 0, kSecs - 1);
        curSec = s; barInSec = 0;
        loadLive(s);
        for (int r = 0; r < kRows; r++) {
            if (rowId[r] < 0) continue;
            const Cell c = cells[s][r];
            songlink::with(rowId[r], [&](songlink::Member& m) {
                if (c.pat >= 0 && m.songPatterns > 0) m.reqPattern = std::min(c.pat, m.songPatterns - 1);
                if (c.loop != songlink::L_NONE && m.songHasLoop) m.reqLoop = c.loop;
            });
        }
        pushCount = 0;   // the levels and mutes go out at once
    }
    void launch(int s) {   // UI: running, on the next bar / beat; stopped, at once
        s = clamp(s, 0, kSecs - 1);
        if (running) queued = queued.load() == s ? -1 : s;   // a second press calls it off
        else applyNow = s;
    }

    // ---- the take ----
    void recPush(int type, int row, float v) { if ((int)rec.size() < kEvMax) rec.push_back({recT, type, row, v}); }
    void beginRec() {
        recState = R_ON; recT = 0;
        rec.clear();
        recSec = curSec;
        for (int r = 0; r < kRows; r++) { recLev[r] = recLastLev[r] = lev[r]; recMut[r] = recLastMut[r] = mut[r]; }
        playState = P_OFF; params[PLAY_PARAM].setValue(0.f); playPrev = false;
    }
    // each tick while recording: a launch (with every strip, so a replay is exact), then what moved
    void recTick(int launched) {
        if (launched >= 0) {
            recPush(EV_SEC, 0, (float)launched);
            for (int r = 0; r < kRows; r++) { recPush(EV_LEV, r, lev[r]); recPush(EV_MUTE, r, mut[r] ? 1.f : 0.f); }
            for (int r = 0; r < kRows; r++) { recLastLev[r] = lev[r]; recLastMut[r] = mut[r]; }
            return;
        }
        for (int r = 0; r < kRows; r++) {
            if (std::fabs(lev[r] - recLastLev[r]) > 0.002f) { recPush(EV_LEV, r, lev[r]); recLastLev[r] = lev[r]; }
            if (mut[r] != recLastMut[r]) { recPush(EV_MUTE, r, mut[r] ? 1.f : 0.f); recLastMut[r] = mut[r]; }
        }
    }
    void endRec(int len) {
        {
            std::lock_guard<std::mutex> lk(takeM);
            take.swap(rec);
            takeLen = std::max(16, len);
            takeSec = recSec;
            for (int r = 0; r < kRows; r++) { takeLev[r] = recLev[r]; takeMut[r] = recMut[r]; }
        }
        rec.clear();
        recState = R_OFF;
        params[REC_PARAM].setValue(0.f); recPrev = false;
    }
    void takeStart() {   // the take's first tick: its section and strips as they were
        playT = 0; playIx = 0;
        apply(takeSec);
        for (int r = 0; r < kRows; r++) { lev[r] = takeLev[r]; mut[r] = takeMut[r]; }
    }
    void takeTick() {
        while (playIx < take.size() && take[playIx].t < playT) playIx++;
        while (playIx < take.size() && take[playIx].t == playT) {
            const Ev& e = take[playIx++];
            if (e.type == EV_SEC) apply((int)e.v);
            else if (e.type == EV_LEV) lev[clamp(e.row, 0, kRows - 1)] = e.v;
            else mut[clamp(e.row, 0, kRows - 1)] = e.v > 0.5f;
        }
    }

    void process(const ProcessArgs& args) override {
        const float dt = args.sampleTime;
        if (runTrig.process(inputs[RUN_INPUT].getVoltage()))
            params[RUN_PARAM].setValue(params[RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f);
        running = params[RUN_PARAM].getValue() > 0.5f;
        bool rst = rstTrig.process(inputs[RESET_INPUT].getVoltage()) | rstBtnTrig.process(params[RESET_PARAM].getValue());
        if (running && !wasRunning) rst = true;   // RUN starts the song from the top
        if (!running && wasRunning) {             // stopped: a take being recorded ends here; a playing one waits
            if (recState == R_ON || recState == R_END) endRec((recT / 16 + 1) * 16);
            if (playState == P_ON) playState = P_ARM;
        }
        wasRunning = running;

        // REC on: arms for the next bar (and stops PLAY); off: recording ends on the next bar
        bool recOn = params[REC_PARAM].getValue() > 0.5f;
        if (recOn && !recPrev) recState = R_ARM;
        if (!recOn && recPrev) { if (recState == R_ARM) recState = R_OFF; else if (recState == R_ON) recState = R_END; }
        recPrev = recOn;
        // PLAY on: the take from its top on the next bar (nothing to play, or recording: it stays off)
        bool playOn = params[PLAY_PARAM].getValue() > 0.5f;
        if (playOn && !playPrev) {
            if (takeLen > 0 && recState == R_OFF) playState = P_ARM;
            else { params[PLAY_PARAM].setValue(0.f); playOn = false; }
        }
        if (!playOn && playPrev) playState = P_OFF;
        playPrev = playOn;

        if (clearTakeReq.exchange(false)) {
            std::lock_guard<std::mutex> lk(takeM);
            take.clear(); takeLen = 0;
            playState = P_OFF; params[PLAY_PARAM].setValue(0.f); playPrev = false;
        }
        int an = applyNow.exchange(-1);
        if (an >= 0) apply(an);

        // ---- clock: CLOCK (16ths) or the BPM knob ----
        bool edge = false, ext = inputs[CLOCK_INPUT].isConnected();
        sinceClock += dt;
        float intStep = 15.f / clamp(params[BPM_PARAM].getValue(), 40.f, 240.f);
        if (ext) {
            if (clkTrig.process(inputs[CLOCK_INPUT].getVoltage())) {
                if (sinceClock < 4.f) clockPeriod = sinceClock;
                sinceClock = 0.f;
                edge = true;
            }
        } else {
            internalTimer += dt;
            if (internalTimer >= intStep) { internalTimer -= intStep; edge = true; }
        }
        if (rst) {
            tick = -1; internalTimer = 0.f;
            resetPulse.trigger(1e-3f); resetFlash = 0.15f;
            if (playState == P_ON) playState = P_ARM;   // the take starts over with the song
            if (!ext && running) edge = true;   // the internal clock starts on the downbeat at once
        }
        resetFlash = std::max(0.f, resetFlash - dt);
        edge = edge && running;

        if (edge) {
            bool start = tick < 0;
            tick = start ? 0 : (tick + 1) % 16;
            clockPulse.trigger(1e-3f);
            bool bar = tick == 0, beat = tick % 4 == 0;
            if (bar) barPulse.trigger(1e-3f);
            if (bar && !start) barInSec++;
            if (!start && (recState == R_ON || recState == R_END)) recT++;
            bool takeTop = false;
            if (!start && playState == P_ON && ++playT >= takeLen) takeTop = true;   // round again
            if (bar && recState == R_END) endRec(recT);
            if (bar && playState == P_ARM && takeLen > 0) { playState = P_ON; takeTop = true; }

            int to = start ? curSec : -1, launched = -1;
            int q = queued.load();
            if (q >= 0 && (bar || (params[QUANT_PARAM].getValue() > 0.5f && beat))) { queued = -1; to = launched = q; }
            if (to >= 0) apply(to);
            if (takeTop) takeStart();
            if (playState == P_ON) takeTick();
            if (bar && recState == R_ARM) beginRec();
            else if (recState == R_ON || recState == R_END) recTick(launched);
        }

        // the strips go out to the modules (every 32 samples; at once after a launch)
        if (--pushCount <= 0) {
            pushCount = 32;
            for (int r = 0; r < kRows; r++) {
                if (rowId[r] < 0) continue;
                float l = lev[r];
                bool m = mut[r];
                songlink::with(rowId[r], [&](songlink::Member& mb) { mb.songLevel = l; mb.songMuted = m; mb.songLinked = true; });
            }
        }

        outputs[CLOCK_OUTPUT].setVoltage(clockPulse.process(dt) ? 10.f : 0.f);
        outputs[RUN_OUTPUT].setVoltage(running ? 10.f : 0.f);
        outputs[RESET_OUTPUT].setVoltage(resetPulse.process(dt) ? 10.f : 0.f);
        outputs[BAR_OUTPUT].setVoltage(barPulse.process(dt) ? 10.f : 0.f);
        outputs[SECT_OUTPUT].setVoltage((float)curSec);
        blink += dt;
        if (blink >= 0.5f) blink -= 0.5f;
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_t* rows = json_array();
        for (int r = 0; r < kRows; r++) json_array_append_new(rows, json_integer(rowId[r]));
        json_object_set_new(root, "rows", rows);
        json_t* cs = json_array();   // [section][row] = [pattern, mute, loop, level]
        for (int s = 0; s < kSecs; s++) {
            json_t* a = json_array();
            for (int r = 0; r < kRows; r++) {
                const Cell& c = cells[s][r];
                json_t* v = json_array();
                json_array_append_new(v, json_integer(c.pat));
                json_array_append_new(v, json_boolean(c.mute));
                json_array_append_new(v, json_integer(c.loop));
                json_array_append_new(v, json_real(c.level));
                json_array_append_new(a, v);
            }
            json_array_append_new(cs, a);
        }
        json_object_set_new(root, "cells", cs);
        json_object_set_new(root, "section", json_integer(curSec));
        std::lock_guard<std::mutex> lk(takeM);
        if (takeLen > 0) {
            json_t* tk = json_object();
            json_object_set_new(tk, "len", json_integer(takeLen));
            json_object_set_new(tk, "section", json_integer(takeSec));
            json_t* tl = json_array(), *tm = json_array(), *ev = json_array();
            for (int r = 0; r < kRows; r++) { json_array_append_new(tl, json_real(takeLev[r])); json_array_append_new(tm, json_boolean(takeMut[r])); }
            for (const Ev& e : take) {
                json_t* v = json_array();
                json_array_append_new(v, json_integer(e.t));
                json_array_append_new(v, json_integer(e.type));
                json_array_append_new(v, json_integer(e.row));
                json_array_append_new(v, json_real(e.v));
                json_array_append_new(ev, v);
            }
            json_object_set_new(tk, "levels", tl);
            json_object_set_new(tk, "mutes", tm);
            json_object_set_new(tk, "events", ev);
            json_object_set_new(root, "take", tk);
        }
        return root;
    }
    void dataFromJson(json_t* root) override {
        if (json_t* rows = json_object_get(root, "rows"))
            for (int r = 0; r < kRows; r++) { json_t* j = json_array_get(rows, r); rowId[r] = j ? json_integer_value(j) : -1; }
        if (json_t* cs = json_object_get(root, "cells"))
            for (int s = 0; s < kSecs; s++)
                for (int r = 0; r < kRows; r++) {
                    json_t* v = json_array_get(json_array_get(cs, s), r);
                    Cell c;
                    if (v) {
                        c.pat = clamp((int)json_integer_value(json_array_get(v, 0)), -1, 7);
                        c.mute = json_is_true(json_array_get(v, 1));
                        c.loop = clamp((int)json_integer_value(json_array_get(v, 2)), 0, songlink::L_COUNT - 1);
                        if (json_t* l = json_array_get(v, 3)) c.level = clamp((float)json_number_value(l), 0.f, 1.f);
                    }
                    cells[s][r] = c;
                }
        if (json_t* j = json_object_get(root, "section")) curSec = clamp((int)json_integer_value(j), 0, kSecs - 1);
        loadLive(curSec);
        std::lock_guard<std::mutex> lk(takeM);
        take.clear(); takeLen = 0;
        if (json_t* tk = json_object_get(root, "take")) {
            takeLen = clamp((int)json_integer_value(json_object_get(tk, "len")), 0, 16 * 4096);
            takeSec = clamp((int)json_integer_value(json_object_get(tk, "section")), 0, kSecs - 1);
            for (int r = 0; r < kRows; r++) {
                json_t* l = json_array_get(json_object_get(tk, "levels"), r);
                takeLev[r] = l ? clamp((float)json_number_value(l), 0.f, 1.f) : 1.f;
                takeMut[r] = json_is_true(json_array_get(json_object_get(tk, "mutes"), r));
            }
            json_t* ev = json_object_get(tk, "events");
            for (size_t k = 0; k < json_array_size(ev) && (int)take.size() < kEvMax; k++) {
                json_t* v = json_array_get(ev, k);
                take.push_back({(int)json_integer_value(json_array_get(v, 0)), clamp((int)json_integer_value(json_array_get(v, 1)), 0, 2),
                                clamp((int)json_integer_value(json_array_get(v, 2)), 0, kRows - 1), (float)json_number_value(json_array_get(v, 3))});
            }
        }
        recState = R_OFF; playState = P_OFF;
    }
};

typedef SynTHBloKMatrixSong MS;

// ---------------------------------------------------------------- widgets ----------------

// the members found now (UI thread; refreshed each frame by the widget)
struct Found {
    std::vector<songlink::Info> list;
    float pk[kRows] = {};   // each row's output peak (V), for its strip
    const songlink::Info* get(int64_t id) const {
        for (const songlink::Info& i : list) if (i.id == id) return &i;
        return NULL;
    }
};

inline std::shared_ptr<window::Font> songFont() { return APP->window->uiFont; }

// A cell in the OLED look (oled::Cell's drawing) that runs code instead of holding a param: lit() and
// the label from functions, a click / right-click / scroll handler.
struct SongBtn : OpaqueWidget {
    std::function<bool()> lit;
    std::function<std::string()> text;
    std::function<void(int)> press;    // 0 left, 1 right
    std::function<void(int)> scroll;   // +1 / -1
    std::string label;
    NVGcolor tint = oled::litFill();
    bool dimOff = false;               // off: drawn fainter (an empty choice)
    float fontSize = 6.f;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        bool on = lit && lit();
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.2f);
        nvgFillColor(args.vg, on ? tint : oled::offFill());
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, on ? tint : oled::outline(dimOff ? 50 : 110));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        std::string s = text ? text() : label;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? oled::ink() : dimOff ? oled::outline(110) : oled::amber());
        nvgText(args.vg, W * 0.5f, H * 0.5f, s.c_str(), NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS) return;
        if (e.button == GLFW_MOUSE_BUTTON_LEFT || e.button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (press) press(e.button == GLFW_MOUSE_BUTTON_LEFT ? 0 : 1);
            e.consume(this);
        }
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (!scroll || e.scrollDelta.y == 0.f) return;
        scroll(e.scrollDelta.y > 0.f ? 1 : -1);
        e.consume(this);
    }
};

struct SongTitle : TransparentWidget {
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
        nvgFontSize(args.vg, 11.f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgText(args.vg, 0.f, mm2px(3.5f), "SynTHBloK", NULL);
        nvgFontSize(args.vg, 10.f);
        nvgTextLetterSpacing(args.vg, 0.8f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0x7a, 0x00));
        nvgText(args.vg, 0.f, mm2px(6.4f), "MATRIX-SONG", NULL);
    }
};

// A section header: click launches it (and selects it for COPY / CLEAR); playing = filled, waiting = blinks.
struct SecHead : OpaqueWidget {
    MS* module = NULL;
    int s = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        bool playing = module ? module->curSec == s : s == 0;
        bool waiting = module && module->queued.load() == s;
        bool sel = module && module->selSec == s;
        bool on = playing || (waiting && module->blink < 0.25f);
        NVGcolor col = kPatOnColor[s];
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.4f);
        nvgFillColor(args.vg, on ? col : kIdle);
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, sel ? nvgRGB(0xff, 0xff, 0xff) : col);
        nvgStrokeWidth(args.vg, sel ? 1.2f : 0.8f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 9.f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? nvgRGB(0x10, 0x10, 0x10) : col);
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, kSecName[s], NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || !module) return;
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) { module->launch(s); module->selSec = s; e.consume(this); }
        else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) { module->selSec = s; e.consume(this); }
    }
};

// A row's module: its name (and its pattern now) in the row's colour; click to choose the module.
struct RowLabel : OpaqueWidget {
    MS* module = NULL;
    Found* found = NULL;
    int r = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        const char* name = "-";
        char pat[8] = "";
        bool gone = false;
        static const char* const demo[kRows] = {"DRUM", "DUO", "MATRIX-8", "FORGE", "FX", "ECHO", "LOOP", "-"};
        if (!module) name = demo[r];
        else if (module->rowId[r] >= 0) {
            const songlink::Info* i = found ? found->get(module->rowId[r]) : NULL;
            if (i) { name = i->name; if (i->patNow >= 0) snprintf(pat, sizeof pat, "%d", i->patNow + 1); }
            else { name = "GONE"; gone = true; }
        }
        bool sel = module && module->selRow == r;
        NVGcolor col = gone ? oled::dim() : kRowCol[r];
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.2f);
        nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgTransRGBA(col, sel ? 220 : 70));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 6.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, col);
        nvgText(args.vg, 3.f, box.size.y * 0.5f, name, NULL);
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, oled::amber());
        nvgText(args.vg, box.size.x - 3.f, box.size.y * 0.5f, pat, NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || !module || !found) return;
        if (e.button != GLFW_MOUSE_BUTTON_LEFT && e.button != GLFW_MOUSE_BUTTON_RIGHT) return;
        MS* m = module;
        int row = r;
        m->selRow = row;
        ui::Menu* menu = createMenu();
        menu->addChild(createMenuLabel(string::f("Row %d: the module it conducts", row + 1)));
        for (const songlink::Info& i : found->list) {
            int64_t id = i.id;
            menu->addChild(createCheckMenuItem(string::f("%s  (module %lld)", i.name, (long long)id), "",
                [=]() { return m->rowId[row] == id; },
                [=]() { for (int k = 0; k < kRows; k++) if (m->rowId[k] == id) m->rowId[k] = -1; m->release(row); m->rowId[row] = id; }));
        }
        menu->addChild(createMenuItem("Empty this row", "", [=]() { m->release(row); m->rowId[row] = -1; }));
        e.consume(this);
    }
};

// A pad: what row r's module does in section s. Click selects it for the editor below; scroll steps its
// pattern; right-click mutes it.
struct SongPad : OpaqueWidget {
    MS* module = NULL;
    Found* found = NULL;
    int r = 0, s = 0;
    int patterns() {
        if (!module || module->rowId[r] < 0) return 8;
        const songlink::Info* i = found ? found->get(module->rowId[r]) : NULL;
        return i ? i->patterns : 8;
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        MS::Cell c;
        if (module) c = module->cells[s][r];
        else if (s < 4 && r < 7) { c.pat = (s + r) % 4; c.mute = r == 2 && s == 0; }
        bool used = !module || module->rowId[r] >= 0;
        bool playing = module ? module->curSec == s : s == 0;
        bool sel = module && module->selSec == s && module->selRow == r;
        NVGcolor col = kRowCol[r];
        float W = box.size.x, H = box.size.y;
        bool content = c.pat >= 0 || c.mute || c.loop != songlink::L_NONE;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.4f);
        nvgFillColor(args.vg, playing && content && used ? (c.mute ? nvgTransRGBA(col, 70) : col) : kIdle);
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, sel ? nvgRGB(0xff, 0xff, 0xff) : nvgTransRGBA(col, !used ? 25 : content ? 200 : 60));
        nvgStrokeWidth(args.vg, sel ? 1.2f : 0.7f);
        nvgStroke(args.vg);
        if (!used) return;
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        NVGcolor ink = playing && content && !c.mute ? nvgRGB(0x10, 0x10, 0x10) : col;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        if (c.mute) {
            nvgFontSize(args.vg, 8.f);
            nvgFillColor(args.vg, nvgTransRGBA(col, 160));
            nvgText(args.vg, W * 0.5f, H * 0.45f, "M", NULL);
        } else if (c.pat >= 0) {
            char t[4];
            snprintf(t, sizeof t, "%d", c.pat + 1);
            nvgFontSize(args.vg, 9.f);
            nvgFillColor(args.vg, ink);
            nvgText(args.vg, W * 0.5f, c.loop ? H * 0.4f : H * 0.5f, t, NULL);
        }
        if (c.loop != songlink::L_NONE) {
            nvgFontSize(args.vg, 4.6f);
            nvgFillColor(args.vg, ink);
            nvgText(args.vg, W * 0.5f, H * 0.8f, kLoopGlyph[c.loop], NULL);
        }
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || !module) return;
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) { module->selSec = s; module->selRow = r; e.consume(this); }
        else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
            module->selSec = s; module->selRow = r;
            module->cells[s][r].mute = !module->cells[s][r].mute;
            module->edited(s);
            e.consume(this);
        }
    }
    // Delete / Backspace over a pad clears it (keeps its pattern, no mute, no loop action, full level)
    void onHoverKey(const HoverKeyEvent& e) override {
        if (!module || e.action != GLFW_PRESS || (e.key != GLFW_KEY_DELETE && e.key != GLFW_KEY_BACKSPACE)) return;
        module->cells[s][r] = MS::Cell();
        module->selSec = s; module->selRow = r;
        module->edited(s);
        e.consume(this);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (!module || e.scrollDelta.y == 0.f) return;
        int n = patterns();
        if (n > 0) {
            int& p = module->cells[s][r].pat;
            p = clamp(p + (e.scrollDelta.y > 0.f ? 1 : -1), -1, n - 1);
            module->edited(s);
        }
        e.consume(this);
    }
};

// The CELL editor's info lines: the selected pad (module and section), then what it holds.
struct EdDisplay : TransparentWidget {
    MS* module = NULL;
    Found* found = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        const float R = mm2px(oled::kRow);
        nvgBeginPath(args.vg);
        oled::divider(args, 4.f, R, W - 4.f, R);
        oled::divider(args, 4.f, 2.f * R, W - 4.f, 2.f * R);
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        const char* name = "DRUM";
        int s = 0, r = 0;
        MS::Cell c;
        if (module) {
            s = module->selSec; r = module->selRow; c = module->cells[s][r];
            const songlink::Info* i = module->rowId[r] >= 0 && found ? found->get(module->rowId[r]) : NULL;
            name = i ? i->name : module->rowId[r] >= 0 ? "GONE" : "EMPTY ROW";
        } else c.pat = 0;
        char val[64];
        snprintf(val, sizeof val, "%s  LEVEL %d%%%s%s%s", c.pat >= 0 ? string::f("PATTERN %d", c.pat + 1).c_str() : "KEEPS ITS PATTERN",
                 (int)std::round(c.level * 100.f), c.mute ? "  MUTED" : "", c.loop ? "  LOOP " : "", c.loop ? songlink::kLoopName[c.loop] : "");
        float y = R * 0.5f;
        nvgFontSize(args.vg, 6.f);
        nvgFillColor(args.vg, oled::dim());
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, 4.f, y, "CELL", NULL);
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, kRowCol[r]);
        std::string who = string::f("%s · %s", name, kSecName[s]);
        nvgText(args.vg, mm2px(8.f), y, who.c_str(), NULL);
        nvgFontSize(args.vg, 6.5f);
        nvgFillColor(args.vg, oled::amber());
        nvgText(args.vg, 4.f, R * 1.5f, val, NULL);
    }
};

// The SONG screen: state | bar | tempo; now playing, what waits, and the take.
struct SongDisplay : TransparentWidget {
    MS* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        const float R = mm2px(oled::kRow);
        nvgBeginPath(args.vg);
        oled::divider(args, 4.f, R, W - 4.f, R);
        oled::divider(args, 4.f, 2.f * R, W - 4.f, 2.f * R);
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        bool run = module ? module->running : false;
        char bar[32] = "BAR 1", bpm[16] = "120.0 BPM";
        if (module) {
            snprintf(bar, sizeof bar, "BAR %d", module->barInSec + 1);
            snprintf(bpm, sizeof bpm, "%.1f BPM", module->bpmNow());
        }
        float y0 = R * 0.5f, y1 = R * 1.5f;
        nvgFontSize(args.vg, 6.f);
        nvgFillColor(args.vg, oled::dim());
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, 4.f, y0, "SONG", NULL);
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, run ? oled::green() : oled::dim());
        nvgText(args.vg, mm2px(9.f), y0, run ? "RUN" : "STOP", NULL);
        nvgFillColor(args.vg, oled::amber());
        nvgText(args.vg, mm2px(20.f), y0, bar, NULL);
        nvgFontSize(args.vg, 8.f);
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, W - 4.f, y0, bpm, NULL);
        // NOW: the section, the launch waiting for its bar; on the right, the take
        int cur = module ? module->curSec : 0, q = module ? module->queued.load() : -1;
        nvgFontSize(args.vg, 6.f);
        nvgFillColor(args.vg, oled::dim());
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, 4.f, y1, "NOW", NULL);
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, kPatOnColor[cur]);
        nvgText(args.vg, mm2px(9.f), y1, kSecName[cur], NULL);
        if (q >= 0) {
            nvgFillColor(args.vg, oled::litFill());
            std::string next = string::f("> %s", kSecName[q]);
            nvgText(args.vg, mm2px(13.f), y1, next.c_str(), NULL);
        }
        std::string tk = "TAKE 8 BARS";
        NVGcolor tc = oled::amber();
        const NVGcolor red = nvgRGB(0xff, 0x32, 0x28), cyan = nvgRGB(0x5c, 0xc8, 0xf2);
        float prog = -1.f;
        if (module) {
            tk = "";
            if (module->recState == MS::R_ARM) { tk = "REC ON THE BAR"; tc = red; }
            else if (module->recState == MS::R_ON) { tk = string::f("REC  BAR %d", module->recT / 16 + 1); tc = red; }
            else if (module->recState == MS::R_END) { tk = string::f("REC  ENDS BAR %d", module->recT / 16 + 1); tc = red; }
            else if (module->playState == MS::P_ARM) { tk = "TAKE ON THE BAR"; tc = cyan; }
            else if (module->playState == MS::P_ON) {
                tk = string::f("TAKE  %d / %d", module->playT / 16 + 1, module->takeLen / 16); tc = cyan;
                prog = (module->playT + 1) / (float)module->takeLen;
            }
            else if (module->takeLen > 0) tk = string::f("TAKE  %d BARS", module->takeLen / 16);
            else { tk = "NO TAKE"; tc = oled::dim(); }
            if (prog < 0.f && run) prog = module->tick < 0 ? 0.f : (module->tick + 1) / 16.f;
        }
        nvgFillColor(args.vg, tc);
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, W - 4.f, y1, tk.c_str(), NULL);
        // the take's progress while it plays, else the bar's
        if (prog >= 0.f) {
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 4.f, 2.f * R - 1.2f, (W - 8.f) * clamp(prog, 0.f, 1.f), 0.8f);
            nvgFillColor(args.vg, module && module->playState == MS::P_ON ? cyan : oled::green());
            nvgFill(args.vg);
        }
    }
};

// A row's level strip (as MATRIX-DRUM's): the module's level in the row's colour (faded while muted), with
// its output meter as a wash rising from the bottom. Click sets it, drag moves it.
struct SongStrip : OpaqueWidget {
    MS* module = NULL;
    Found* found = NULL;
    int r = 0;
    float get() { return module ? module->lev[r] : 0.55f + 0.05f * r; }
    void draw(const DrawArgs& args) override {
        bool used = !module || module->rowId[r] >= 0;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        nvgFillColor(args.vg, nvgRGB(0x14, 0x14, 0x14));
        nvgFill(args.vg);
        NVGcolor col = kRowCol[r];
        if (used) {
            float h = (box.size.y - 3.f) * get();
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, 1.5f, box.size.y - 1.5f - h, box.size.x - 3.f, h, 1.f);
            nvgFillColor(args.vg, nvgLerpRGBA(kIdle, col, module && module->mut[r] ? 0.35f : 0.8f));
            nvgFill(args.vg);
        }
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        nvgStrokeColor(args.vg, nvgLerpRGBA(kIdle, col, used ? 0.45f : 0.12f));
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
    }
    // the module's output, -48 .. +6 dB (0 dB = 5 V): faint at the bottom, strongest at the level; red past +4 dB
    void drawLayer(const DrawArgs& args, int layer) override {
        OpaqueWidget::drawLayer(args, layer);
        if (layer != 1) return;
        float pk = module ? (found ? found->pk[r] : 0.f) : 1.2f + 0.3f * (r % 3);
        float n = clamp((20.f * std::log10(pk / 5.f + 1e-6f) + 48.f) / 54.f, 0.f, 1.f);
        if (n <= 0.f) return;
        float x = 1.5f, y = 1.5f, w = box.size.x - 3.f, hh = box.size.y - 3.f;
        float lh = hh * n, top = y + hh - lh;
        NVGcolor c = pk > 8.f ? nvgRGB(0xff, 0x32, 0x28) : nvgLerpRGBA(kRowCol[r], nvgRGB(0xff, 0xff, 0xff), 0.3f);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, x, top, w, lh, std::min(1.f, w * 0.5f));
        nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.f, y + hh, 0.f, top, nvgTransRGBA(c, 30), nvgTransRGBA(c, 160)));
        nvgFill(args.vg);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->setLevel(r, 1.f - clamp((e.pos.y - 1.5f) / (box.size.y - 3.f), 0.f, 1.f));
            e.consume(this);
        }
    }
    void onDragMove(const DragMoveEvent& e) override {
        if (!module) return;
        float z = APP->scene->rackScroll->getZoom();
        module->setLevel(r, get() - e.mouseDelta.y / z / (box.size.y - 3.f));
    }
    void onDoubleClick(const DoubleClickEvent& e) override { if (module) module->setLevel(r, 1.f); e.consume(this); }
};

// MUTE (as MATRIX-DRUM's), one over each strip: mutes the row's module (lit: muted).
struct SongMuteSq : OpaqueWidget {
    MS* module = NULL;
    Found* found = NULL;
    int r = 0;
    void draw(const DrawArgs& args) override {
        bool used = !module || module->rowId[r] >= 0;
        bool on = module ? module->mut[r] : r == 2;
        NVGcolor col = used ? kRowCol[r] : nvgRGB(0x3a, 0x3a, 0x3a);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.4f);
        nvgFillColor(args.vg, on && used ? col : kIdle);
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, col);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on && used ? nvgRGB(0x10, 0x10, 0x10) : col);
        nvgFontSize(args.vg, 5.5f);
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, "MUTE", NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module && module->rowId[r] >= 0) module->toggleMute(r);
            e.consume(this);
        }
    }
};

struct SynTHBloKMatrixSongWidget : ModuleWidget {
    Found found;
    void step() override {
        MS* m = getModule<MS>();
        if (m) {
            found.list = songlink::list();
            // a module that is not on a row yet takes the first empty row
            for (const songlink::Info& i : found.list) {
                bool placed = false;
                for (int r = 0; r < kRows; r++) if (m->rowId[r] == i.id) placed = true;
                if (placed) continue;
                for (int r = 0; r < kRows; r++) if (m->rowId[r] < 0) { m->rowId[r] = i.id; break; }
            }
            // each row's output peak, for its strip
            for (int r = 0; r < kRows; r++) {
                float pk = 0.f;
                if (m->rowId[r] >= 0) songlink::with(m->rowId[r], [&](songlink::Member& mb) { pk = mb.songPk.load(std::memory_order_relaxed); });
                found.pk[r] = pk;
            }
        }
        ModuleWidget::step();
    }
    SynTHBloKMatrixSongWidget(MS* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKMatrixSong.svg")));
        using namespace rack::componentlibrary;
        const float PW = 162.56f;   // 32 HP
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PW - 3.5f, 3.5f))));
        SongTitle* title = createWidget<SongTitle>(mm2px(Vec(8.5f, 0.f)));
        title->box.size = mm2px(Vec(60.f, 7.f));
        addChild(title);
        auto label = [&](float cx, float cy, const char* text) {
            JLabel* l = createWidget<JLabel>(mm2px(Vec(cx - 12.f, cy - 2.f)));
            l->box.size = mm2px(Vec(24.f, 4.f)); l->text = text;
            addChild(l);
        };
        const float CH = oled::kCellH, CG = 1.f, GAP = 0.9f;

        // ---------------- left: the grid (rows = modules, columns = sections) ----------------
        // sized as MATRIX-DRUM's: 6.6 mm pads on a 7.4 mm pitch, the block centred on the left plate
        const float GX = 31.4f, GP = 7.4f, GS = 6.6f, GY = 19.5f;
        for (int s = 0; s < kSecs; s++) {
            SecHead* h = createWidget<SecHead>(mm2px(Vec(GX + GP * s, 10.5f)));
            h->box.size = mm2px(Vec(GS, 6.6f)); h->module = module; h->s = s;
            addChild(h);
        }
        for (int r = 0; r < kRows; r++) {
            RowLabel* l = createWidget<RowLabel>(mm2px(Vec(12.6f, GY + GP * r)));
            l->box.size = mm2px(Vec(17.6f, GS)); l->module = module; l->found = &found; l->r = r;
            addChild(l);
            for (int s = 0; s < kSecs; s++) {
                SongPad* p = createWidget<SongPad>(mm2px(Vec(GX + GP * s, GY + GP * r)));
                p->box.size = mm2px(Vec(GS, GS)); p->module = module; p->found = &found; p->r = r; p->s = s;
                addChild(p);
            }
        }

        // ---------------- left, under the grid: the strips (a MUTE and a level per row's module) ----------------
        const float MY = 85.5f, LY = 93.5f, LH = 30.f;
        for (int r = 0; r < kRows; r++) {
            float x = GX + GP * r;
            SongMuteSq* q = createWidget<SongMuteSq>(mm2px(Vec(x, MY)));
            q->box.size = mm2px(Vec(GS, GS)); q->module = module; q->found = &found; q->r = r;
            addChild(q);
            SongStrip* s = createWidget<SongStrip>(mm2px(Vec(x + 0.9f, LY)));
            s->box.size = mm2px(Vec(4.8f, LH)); s->module = module; s->found = &found; s->r = r;
            addChild(s);
        }

        // ---------------- right: the SONG screen and transport ----------------
        const float SX = 101.6f, SW = PW - SX - 4.2f, SY = 10.4f;
        const float ctY = SY + 2.f * oled::kRow + 0.4f, TH = 2.f * CH + GAP;
        const float SH = ctY - SY + TH + 1.2f;
        SongDisplay* sd = createWidget<SongDisplay>(mm2px(Vec(SX, SY)));
        sd->box.size = mm2px(Vec(SW, SH)); sd->module = module;
        addChild(sd);
        const float x0 = SX + 2.f, inner = SW - 4.f;
        // REC | PLAY | RUN | RESET (tall), then BAR over BEAT
        auto pcell = [&](float x, float y, float w, float h, int param, const char* lab) {
            oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(x, y)), module, param);
            c->box.size = mm2px(Vec(w, h)); c->module = module; c->label = lab;
            addParam(c);
            return c;
        };
        auto tint = [&](float x, int param, const char* lab, NVGcolor col, int jack) {
            echoloop::TintCell* c = createParam<echoloop::TintCell>(mm2px(Vec(x, ctY)), module, param);
            c->box.size = mm2px(Vec((inner - 4.f * CG) / 5.f, TH)); c->module = module; c->label = lab; c->latch = true;
            c->fontSize = 7.f; c->tint = col; c->jack = jack;
            addParam(c);
        };
        const float tw = (inner - 4.f * CG) / 5.f;
        tint(x0, MS::REC_PARAM, "REC", nvgRGB(0xff, 0x32, 0x28), -1);
        tint(x0 + (tw + CG), MS::PLAY_PARAM, "PLAY", nvgRGB(0x5c, 0xc8, 0xf2), -1);
        tint(x0 + 2.f * (tw + CG), MS::RUN_PARAM, "RUN", oled::green(), MS::RUN_INPUT);
        oled::Cell* rs = pcell(x0 + 3.f * (tw + CG), ctY, tw, TH, MS::RESET_PARAM, "RESET");
        rs->fontSize = 7.f; rs->jack = MS::RESET_INPUT;
        if (module) rs->flash = &module->resetFlash;
        oled::Cell* qb = pcell(x0 + 4.f * (tw + CG), ctY, tw, CH, MS::QUANT_PARAM, "BAR");
        qb->setTo = 0;
        oled::Cell* qt = pcell(x0 + 4.f * (tw + CG), ctY + CH + GAP, tw, CH, MS::QUANT_PARAM, "BEAT");
        qt->setTo = 1;

        // ---------------- right, under the SONG screen: the CELL editor (the selected pad) ----------------
        const float EX = SX, EY = SY + SH + 2.f, EW = SW, EH = 2.f * oled::kRow + 0.5f + 4.f * CH + 3.f * GAP + 0.8f;
        EdDisplay* ed = createWidget<EdDisplay>(mm2px(Vec(EX, EY)));
        ed->box.size = mm2px(Vec(EW, EH)); ed->module = module; ed->found = &found;
        addChild(ed);
        auto btn = [&](float x, float y, float w, float h, const char* lab) {
            SongBtn* b = createWidget<SongBtn>(mm2px(Vec(x, y)));
            b->box.size = mm2px(Vec(w, h)); b->label = lab; b->fontSize = 5.2f;
            addChild(b);
            return b;
        };
        auto selCell = [=]() -> MS::Cell* { return module ? &module->cells[module->selSec][module->selRow] : NULL; };
        auto edited = [=]() { if (module) module->edited(module->selSec); };
        {
            float y = EY + 2.f * oled::kRow + 0.5f;
            // pattern: KEEP | 1 .. 8
            const float pw = (inner - 8.f * CG) / 9.f;
            static const char* const pn[9] = {"KEEP", "1", "2", "3", "4", "5", "6", "7", "8"};
            for (int i = 0; i < 9; i++) {
                SongBtn* b = btn(x0 + i * (pw + CG), y, pw, CH, pn[i]);
                if (i == 0) b->fontSize = 4.6f;
                b->lit = [=]() { MS::Cell* c = selCell(); return c ? c->pat == i - 1 : i == 1; };
                b->press = [=](int) { if (MS::Cell* c = selCell()) { c->pat = i - 1; edited(); } };
            }
            // MUTE | LOOP: NONE PLAY STOP REC ONCE
            y += CH + GAP;
            const float lw = (inner - 5.f * CG) / 6.f;
            SongBtn* mu = btn(x0, y, lw, CH, "MUTE");
            mu->tint = nvgRGB(0x5c, 0xc8, 0xf2);
            mu->lit = [=]() { MS::Cell* c = selCell(); return c && c->mute; };
            mu->press = [=](int) { if (MS::Cell* c = selCell()) { c->mute = !c->mute; edited(); } };
            static const char* const ln[5] = {"NO LOOP", "PLAY", "STOP", "REC", "ONCE"};
            for (int a = 0; a < 5; a++) {
                SongBtn* b = btn(x0 + (a + 1) * (lw + CG), y, lw, CH, ln[a]);
                if (a == 0) b->fontSize = 4.6f;
                if (a == songlink::L_REC) b->tint = nvgRGB(0xff, 0x32, 0x28);
                if (a == songlink::L_PLAY || a == songlink::L_ONCE) b->tint = oled::green();
                b->lit = [=]() { MS::Cell* c = selCell(); return c ? c->loop == a : a == 0; };
                b->press = [=](int) { if (MS::Cell* c = selCell()) c->loop = a; };
            }
            // the cell and its row (across every section); then the section: COPY to the next, CLEAR it
            static const char* const tn[5] = {"CLEAR CELL", "CELL TO ROW", "CLEAR ROW", "COPY SECTION >", "CLEAR SECTION"};
            for (int i = 0; i < 5; i++) {
                int per = i < 3 ? 3 : 2, k = i < 3 ? i : i - 3;
                float w = (inner - (per - 1) * CG) / per;
                SongBtn* b = btn(x0 + k * (w + CG), y + (i < 3 ? 1.f : 2.f) * (CH + GAP), w, CH, tn[i]);
                b->lit = []() { return false; };
                b->press = [=](int) {
                    if (!module) return;
                    MS* m = module;
                    int s = m->selSec, r = m->selRow;
                    if (i == 0) m->cells[s][r] = MS::Cell();
                    if (i == 1) for (int k = 0; k < kSecs; k++) m->cells[k][r] = m->cells[s][r];
                    if (i == 2) for (int k = 0; k < kSecs; k++) m->cells[k][r] = MS::Cell();
                    if (i == 3) { int to = (s + 1) % kSecs; for (int k = 0; k < kRows; k++) m->cells[to][k] = m->cells[s][k]; m->selSec = to; }
                    if (i == 4) for (int k = 0; k < kRows; k++) m->cells[s][k] = MS::Cell();
                    m->edited(m->curSec);
                };
            }
        }
        // BPM
        const float KY = EY + EH + 9.f, KX = SX + SW * 0.5f;
        addChild(KnobTicks::create(Vec(KX, KY)));
        ParamWidget* kn = createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(KX, KY)), module, MS::BPM_PARAM);
        if (Knob* k = dynamic_cast<Knob*>(kn)) k->snap = true;
        addParam(kn);
        label(KX + 9.f, KY, "BPM");

        // jacks: inputs, then outputs
        auto jack = [&](float x, float y, int id, bool out, const char* name) {
            label(x, y - 5.5f, name);
            if (out) {
                addOutput(createOutputCentered<SmallJack>(mm2px(Vec(x, y)), module, id));
                addChild(JackLed::create(Vec(x + 3.9f, y), module, id, true));
            } else addInput(createInputCentered<SmallJack>(mm2px(Vec(x, y)), module, id));
        };
        const float JY = 118.f;
        jack(SX + 10.f, 98.f, MS::CLOCK_INPUT, false, "CLK IN");
        jack(SX + SW * 0.5f, 98.f, MS::RUN_INPUT, false, "RUN");
        jack(SX + SW - 10.f, 98.f, MS::RESET_INPUT, false, "RESET");
        static const char* const on[5] = {"CLOCK", "RUN", "RESET", "BAR", "SECT"};
        for (int i = 0; i < 5; i++) jack(SX + 5.5f + i * (SW - 11.f) / 4.f, JY, MS::CLOCK_OUTPUT + i, true, on[i]);
    }
    void appendContextMenu(Menu* menu) override {
        MS* m = getModule<MS>();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Empty every row (modules are placed again)", "", [=]() { for (int r = 0; r < kRows; r++) { m->release(r); m->rowId[r] = -1; } }));
        menu->addChild(createMenuItem("Clear every section", "", [=]() {
            for (int s = 0; s < kSecs; s++) for (int r = 0; r < kRows; r++) m->cells[s][r] = MS::Cell();
            m->edited(m->curSec);
        }));
        menu->addChild(createMenuItem("Clear the take", "", [=]() { m->clearTakeReq = true; }));
    }
};

} // namespace

Model* modelSynTHBloKMatrixSong = createModel<SynTHBloKMatrixSong, SynTHBloKMatrixSongWidget>("SynTHBloKMatrixSong");
