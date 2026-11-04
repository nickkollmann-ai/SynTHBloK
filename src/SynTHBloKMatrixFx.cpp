// ============================================================================
// SynTHBloK MATRIX-FX  (8 inputs x 8 effect slots, played from the MATRIX 8x8 grid and the JAM)
//  Parallel routing: row = input, column = effect slot. A lit pad sends that input into that slot
//  (plain a little lower, accent = full level); every slot takes its own mix and the returns add up.
//  A slot can be linked into the next one (a chain); delays and reverbs fed by a link pass it on and
//  add their repeats. The 8 patterns are 8 routing scenes; the strips set the slots' settings by bank.
//  dry + returns -> DJ filter -> tape loop -> MASTER (LOOP BEFORE FILTER: the loop before the filter).
//  The tape loop is MATRIX-ECHO's looper (on the beat while running, record length, count-in, REC
//  replaces); its audio is kept with the patch.
//  The SETUP OLED maps the slots: tabs of effects and chains, drag one onto a slot cell, a slot name
//  under the grid, or a pad (that pad's input is sent there too).
//  JAM (Controller Editor "Template 4", MIDI channel 1), as on MATRIX FORGE. GRID + pad = accent,
//  MUTE / SOLO + pad = mute / solo that input. CLEAR clears this scene (hold: every scene).
//  No clock patched: the clocked effects run at the BPM knob.
// ============================================================================
#include "plugin.hpp"
#include "MatrixFx_dsp.hpp"
#include "Echo_dsp.hpp"
#include "EchoLoopUi.hpp"
#include "Matrix8_ui.hpp"
#include "OledUi.hpp"
#include "JackLed.hpp"
#include "KnobTicks.hpp"
#include "JamOwner.hpp"
#include "SongLink.hpp"
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

namespace {

// The library as the SETUP tabs list it: effect types (tabs 0..6) or chain indices (CHAIN).
static std::vector<int> tabItems(int tab) {
    std::vector<int> v;
    if (tab == mfx::TAB_CHAIN) { for (int i = 0; i < mfx::kChainCount; i++) v.push_back(i); }
    else for (int t = 0; t < mfx::T_COUNT; t++) if (mfx::kFx[t].tab == tab) v.push_back(t);
    return v;
}

struct LoopLenQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int v = (int)getValue();
        return v <= 0 ? "Free (record until REC / PLAY)" : string::f("Record %d beat%s", v, v == 1 ? "" : "s");
    }
};

struct SynTHBloKMatrixFx : Module, songlink::Member {   // MATRIX-SONG picks its scene, plays its loop, mutes it
    enum ParamIds { PAT_PARAM, LEN_PARAM, PAGE_PARAM, CLEAR_PARAM, LEARN_PARAM, BANK_PARAM, RUN_PARAM, RESET_PARAM,
                    RAND_PARAM, MIDI_ON_PARAM, DUP_PARAM,
                    KNOB_PARAM, DRY_PARAM, WET_PARAM, LOOP_LVL_PARAM, BPM_PARAM, FILT_PARAM, RES_PARAM, ROOT_PARAM, SCALE_PARAM,
                    REC_PARAM, PLAY_PARAM, ONCE_PARAM, UNDO_PARAM, LCLEAR_PARAM, SNAP_PARAM, RANDFX_PARAM,
                    LOOPLEN_PARAM, COUNTIN_PARAM, LOOPPRE_PARAM, NUM_PARAMS };   // (the tape loop as on MATRIX-ECHO)
    enum InputIds { IN_INPUT, CLOCK_INPUT = IN_INPUT + 8, RESET_INPUT, RUN_INPUT, SC_INPUT, NUM_INPUTS };
    enum OutputIds { OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };

    midi::InputQueue midiIn;
    midi::Output midiOut;
    bool slowSend = false;
    jamown::Link jam;   // the JAM follows the last clicked module (JamOwner.hpp)

    // routing scenes [scene][input row][slot column]; strip values [bank][slot]
    bool route[8][8][8], acc[8][8][8];
    float val[8][8];
    float stripSm[8];
    int slotType[8];
    bool link[8] = {};            // slot s feeds slot s + 1
    int selSlot = 0, tab = 0;     // SETUP OLED state (saved)

    // the running effects: built on the UI thread, swapped in here with a short fade, the old one handed back
    mfx::Fx* fx[8] = {};
    std::atomic<mfx::Fx*> pending[8], retired[8];
    float swapGain[8], idleT[8], sendG[8][8] = {};
    mfx::Post post[8];
    float fxSr = 44100.f;
    gforge::DjFilter dj;
    EchoLooper looper;

    // clock: tick = 16th in the bar; step = the playing scene when the scene loop runs (LOOP column)
    int tick = -1, step = -1;
    bool pendingReset = false, running = true;
    float sinceClock = 10.f, clockPeriod = 0.f, internalTimer = 0.f, stepSecNow = 0.125f;
    dsp::SchmittTrigger clkTrig, rstTrig, runTrig, rstBtnTrig, randTrig, clrTrig, dupTrig, loopTrig[5];
    bool loopPrev[5] = {};
    float resetFlash = 0.f, recFlash = 0.f, playFlash = 0.f, onceFlash = 0.f, blink = 0.f;

    // input mute / solo (hold MUTE or SOLO on the JAM and press a pad)
    bool mute[8] = {}, solo[8] = {};
    bool muteHeld = false, soloHeld = false, gridHeld = false, jamReset = false;
    bool clearHeld = false, clearAllDone = false;
    float clearHeldT = 0.f;
    int knobCC = kKnobFirstCC;
    float masterVol = 0.8f, drySm = 0.8f, wetSm = 1.f, loopSm = 0.8f, filtSm = 0.5f;
    float inMeter[8] = {}, fxMeter[8] = {};

    int lastLed[kNumLed];
    int ledScan = 0, lastBank = -1, lastPage = -1;
    int stripPage = 0, lastStripPage = -1;
    float tokens = 4.f, resync = 0.f;

    int knobParam() {
        switch (knobCC) {
            case kKnobFirstCC + 1: return DRY_PARAM;
            case kKnobFirstCC + 2: return WET_PARAM;
            default: return KNOB_PARAM;
        }
    }

    SynTHBloKMatrixFx() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
        songlink::join(this, this, "FX", 8, true);
        configParam(PAT_PARAM, 1.f, 8.f, 1.f, "Scene");
        configParam(LEN_PARAM, 1.f, 8.f, 1.f, "Scene loop (1 = hold the scene; 2..8 = step through scenes 1..N, one per bar)");
        configParam(PAGE_PARAM, 1.f, 6.f, 1.f, "JAM matrix page (must match the JAM)");
        configButton(CLEAR_PARAM, "Clear this scene's sends");
        configSwitch(LEARN_PARAM, 0.f, 1.f, 0.f, "Learn page: then press the TOP-LEFT pad on the JAM", {"Off", "Armed"});
        configParam(BANK_PARAM, 1.f, 8.f, 1.f, "Strip bank (A level, B..E the effect's settings, F tone, G pan, H width)");
        configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run (clocked effects and the scene loop; a trigger at the RUN jack toggles it)", {"Stopped", "Running"});
        configButton(RESET_PARAM, "Reset to the start of the bar (a trigger at the RESET jack also resets)");
        configButton(RAND_PARAM, "Random routing scenes on all 8 patterns (a theme and variations) and safe settings for every slot, in a new key");
        configSwitch(MIDI_ON_PARAM, 0.f, 1.f, 1.f, "JAM MIDI (off = ignore the JAM, its lights dark)", {"Off", "On"});
        configButton(DUP_PARAM, "Duplicate this scene into the next one");
        configParam(KNOB_PARAM, 0.f, 1.f, 0.8f, "Master volume (JAM knob on MST or CUE)", "%", 0.f, 100.f);
        configParam(DRY_PARAM, 0.f, 1.f, 0.8f, "Dry: every input straight through (JAM knob on GRP)", "%", 0.f, 100.f);
        configParam(WET_PARAM, 0.f, 1.f, 1.f, "Wet: all effect returns (JAM knob on IN1)", "%", 0.f, 100.f);
        configParam(LOOP_LVL_PARAM, 0.f, 1.f, 0.8f, "Tape loop playback level", "%", 0.f, 100.f);
        configParam(BPM_PARAM, 60.f, 180.f, 120.f, "Internal tempo (when CLOCK is unpatched)", " BPM");
        configParam(FILT_PARAM, 0.f, 1.f, 0.5f, "DJ filter (left = low-pass, centre = off, right = high-pass)");
        configParam(RES_PARAM, 0.f, 1.f, 0.25f, "DJ filter resonance", "%", 0.f, 100.f);
        configSwitch(ROOT_PARAM, 0.f, 11.f, 9.f, "Key root (COMB, RING, ROBOT)", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"});
        configSwitch(SCALE_PARAM, 0.f, 3.f, 0.f, "Key scale", {"Minor", "Dorian", "Phrygian", "Major"});
        configButton(REC_PARAM, "Loop: record / replace (from the top of the loop)");
        configButton(PLAY_PARAM, "Loop: play / stop");
        configButton(ONCE_PARAM, "Loop: play once");
        configButton(UNDO_PARAM, "Loop: undo / redo the last replace");
        configButton(LCLEAR_PARAM, "Loop: clear");
        configSwitch(SNAP_PARAM, 0.f, 1.f, 1.f, "(not used: the loop always works to the beat)", {"Off", "On"});
        configButton(RANDFX_PARAM, "RAND FX: new effects on all 8 slots, linked into musical chains (always an echo and a reverb), safe settings; scenes kept");
        configParam<LoopLenQuantity>(LOOPLEN_PARAM, 0.f, 16.f, 0.f, "Loop record length (running: free = until REC again)");
        configSwitch(COUNTIN_PARAM, 0.f, 4.f, 0.f, "Loop count-in before a new take (running)",
                     {"Off (starts on the next beat)", "1 beat", "2 beats", "3 beats", "4 beats"});
        configSwitch(LOOPPRE_PARAM, 0.f, 1.f, 0.f, "Loop position", {"After the DJ filter", "Before the DJ filter (the filter sweeps the loop too)"});
        for (int id : {PAT_PARAM, LEN_PARAM, PAGE_PARAM, BANK_PARAM, BPM_PARAM, LOOPLEN_PARAM}) paramQuantities[id]->snapEnabled = true;
        for (int id : {SNAP_PARAM, LOOPLEN_PARAM, COUNTIN_PARAM, LOOPPRE_PARAM}) paramQuantities[id]->randomizeEnabled = false;
        for (int r = 0; r < 8; r++) configInput(IN_INPUT + r, "Input " + std::to_string(r + 1) + " (mono, or stereo on a 2-channel cable)");
        configInput(CLOCK_INPUT, "Clock (16th notes; internal BPM when unpatched)");
        configInput(RESET_INPUT, "Reset (trigger)");
        configInput(RUN_INPUT, "Run (trigger: toggles RUN)");
        configInput(SC_INPUT, "Sidechain (the SIDECHAIN effect ducks to it instead of the beat)");
        configOutput(OUT_L_OUTPUT, "Left");
        configOutput(OUT_R_OUTPUT, "Right");
        for (int s = 0; s < 8; s++) {
            pending[s] = NULL; retired[s] = NULL;
            swapGain[s] = 1.f; idleT[s] = 0.f; stripSm[s] = 0.f;
        }
        for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
        fxSr = APP->engine->getSampleRate();
        looper.init(fxSr, 60.f);
        looper.replace = true;   // REC on a playing loop writes over it (as MATRIX-ECHO)
        defaults();
        rebuildAll();
    }
    ~SynTHBloKMatrixFx() {
        songlink::leave(this);
        if (jam.active) { slowSend = true; clearLeds(); }
        jamown::release(id);
        for (int s = 0; s < 8; s++) { delete fx[s]; delete pending[s].exchange(NULL); delete retired[s].exchange(NULL); }
    }

    void defaults() {
        for (int p = 0; p < 8; p++) clearScene(p);
        for (int s = 0; s < 8; s++) {
            slotType[s] = mfx::kDefaultSlots[s];
            link[s] = false;
            for (int b = 0; b < 8; b++) val[b][s] = mfx::kBankDef[b];
            for (int k = 0; k < 4; k++) val[1 + k][s] = mfx::kFx[slotType[s]].def[k];
            mute[s] = solo[s] = false;
        }
        selSlot = 0; tab = 0;
    }
    // Only with the engine stopped or locked (constructor, sample rate change, reset, patch load).
    void rebuildAll() {
        for (int s = 0; s < 8; s++) {
            delete pending[s].exchange(NULL);
            delete fx[s];
            fx[s] = mfx::makeFx(slotType[s], fxSr);
            swapGain[s] = 1.f; idleT[s] = 0.f;
        }
    }
    int songPatternNow() override { return curPat(); }
    void songSetPattern(int p) override { params[PAT_PARAM].setValue((float)(clamp(p, 0, 7) + 1)); }
    void songDoLoop(int a) override { echoloop::songAction(looper, a); }
    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        fxSr = e.sampleRate;
        rebuildAll();
        if (e.sampleRate != looper.sr) { echoloop::reinit(looper, e.sampleRate, 60.f); savedKey[3] = -1; }
    }
    void onReset() override {
        defaults();
        rebuildAll();
        looper.clear();
        tick = step = -1;
    }

    // ---- the loop's audio, kept with the patch: loop.sblp in its storage folder, written as the patch is
    // saved (Rack's autosave too), read back as it loads; a loop playing when saved plays again ----
    enum { kKeyN = 6 };
    int savedKey[kKeyN] = {0, 0, 0, -1, 0, 0};   // the loop the file holds (len -1: none written yet)
    int loadPlay = -1;                           // from the JSON: 1 = it was playing (-1: not given)
    bool audioLoaded = false, playRestored = false;
    int takeGen = 0;                             // counts the takes (a new one is a new file)
    EchoLooper::State prevSt = EchoLooper::EMPTY;
    void loopKey(int* k) {
        const int v[kKeyN] = {takeGen, looper.layers, looper.undone ? 1 : 0, looper.len, looper.beats, looper.playLen};
        std::copy(v, v + kKeyN, k);
    }
    std::string loopPath(const std::string& dir) { return system::join(dir, "loop.sblp"); }
    void applyLoadPlay() {
        if (!audioLoaded || loadPlay < 0 || playRestored) return;
        if (loadPlay == 1 && looper.st == EchoLooper::STOP) looper.st = EchoLooper::PLAY;
        loadPlay = -1;
        playRestored = true;
    }
    void onAdd(const AddEvent& e) override {
        savedKey[3] = -1;
        std::string dir;
        try { dir = getPatchStorageDirectory(); } catch (Exception&) { return; }
        if (echoloop::readFile(loopPath(dir), looper) && looper.sr == fxSr) loopKey(savedKey);
        audioLoaded = true;
        applyLoadPlay();
    }
    void onSave(const SaveEvent& e) override {
        if (looper.st == EchoLooper::REC || looper.st == EchoLooper::DUB) return;   // a take in progress keeps the last file
        int key[kKeyN];
        loopKey(key);
        if (std::equal(key, key + kKeyN, savedKey)) return;
        try {
            echoloop::writeFile(loopPath(looper.len > 0 ? createPatchStorageDirectory() : getPatchStorageDirectory()), looper);
            std::copy(key, key + kKeyN, savedKey);
        } catch (Exception&) {}
    }

    // UI thread: load an effect into a slot (its settings to the effect's defaults when `fresh`).
    void setSlotType(int s, int t, bool fresh) {
        s = clamp(s, 0, 7);
        t = clamp(t, 0, mfx::T_COUNT - 1);
        slotType[s] = t;
        if (fresh) for (int k = 0; k < 4; k++) val[1 + k][s] = mfx::kFx[t].def[k];
        delete pending[s].exchange(mfx::makeFx(t, fxSr));
    }
    void swapSlots(int a, int b) {
        if (a == b) return;
        int ta = slotType[a], tb = slotType[b];
        for (int k = 0; k < 8; k++) std::swap(val[k][a], val[k][b]);
        setSlotType(a, tb, false);
        setSlotType(b, ta, false);
    }
    // A chain (kChain) from slot `start` on. row < 0: the whole scene (every input into the chain, the rest
    // of the scene cleared, DRY as the chain says); else only that input is sent to the chain.
    void applyChain(int ci, int start, int row) {
        const mfx::ChainDef& ch = mfx::kChain[clamp(ci, 0, mfx::kChainCount - 1)];
        if (ch.dry < 0.f) {   // DUB SET: the 8 default slots
            for (int s = 0; s < 8; s++) {
                setSlotType(s, ch.type[s], true);
                link[s] = false;
                for (int b : {mfx::B_LEVEL, mfx::B_TONE, mfx::B_PAN, mfx::B_WIDTH}) val[b][s] = mfx::kBankDef[b];
            }
            return;
        }
        start = clamp(start, 0, 8 - ch.n);
        for (int i = 0; i < ch.n; i++) {
            int s = start + i;
            setSlotType(s, ch.type[i], false);
            for (int k = 0; k < 4; k++) val[1 + k][s] = ch.p[i][k];
            val[mfx::B_LEVEL][s] = ch.level[i];
            val[mfx::B_TONE][s] = val[mfx::B_PAN][s] = val[mfx::B_WIDTH][s] = 0.5f;
            link[s] = i < ch.n - 1;
        }
        if (start > 0) link[start - 1] = false;   // nothing runs into the head
        int p = curPat();
        if (row < 0) {
            clearScene(p);
            for (int r = 0; r < 8; r++) route[p][r][start] = true;
            params[DRY_PARAM].setValue(ch.dry);
        } else route[p][clamp(row, 0, 7)][start] = true;
        selSlot = start;
    }
    // A SETUP item (tab, item) dropped on pad (r, c): the effect goes into slot c and input r is sent there.
    void dropOnPad(int tb, int item, int r, int c) {
        std::vector<int> it = tabItems(tb);
        if (item < 0 || item >= (int)it.size()) return;
        if (tb == mfx::TAB_CHAIN) { applyChain(it[item], c, r); return; }
        setSlotType(c, it[item], true);
        route[curPat()][r][c] = true;
        selSlot = c;
    }

    bool anySolo() { for (int r = 0; r < 8; r++) if (solo[r]) return true; return false; }
    bool anyMute() { for (int r = 0; r < 8; r++) if (mute[r]) return true; return false; }
    bool trackOn(int r) { return !mute[r] && (!anySolo() || solo[r]); }
    void clearScene(int p) { for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) route[p][r][c] = acc[p][r][c] = false; }
    void stepParam(int id, int delta, int lo, int hi, bool wrap) {
        int v = (int)params[id].getValue() + delta;
        if (wrap) v = lo + ((v - lo) % (hi - lo + 1) + (hi - lo + 1)) % (hi - lo + 1);
        params[id].setValue((float)clamp(v, lo, hi));
    }
    void duplicateScene() {
        int from = curPat(), to = (from + 1) % 8;
        for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) { route[to][r][c] = route[from][r][c]; acc[to][r][c] = acc[from][r][c]; }
        params[PAT_PARAM].setValue((float)(to + 1));
    }

    int curPat() { return clamp((int)params[PAT_PARAM].getValue(), 1, 8) - 1; }
    int curPage() { return clamp((int)params[PAGE_PARAM].getValue(), 1, 6); }
    int bank() { return clamp((int)params[BANK_PARAM].getValue(), 1, 8) - 1; }
    int root() { return clamp((int)std::round(params[ROOT_PARAM].getValue()), 0, 11); }
    int scale() { return clamp((int)std::round(params[SCALE_PARAM].getValue()), 0, mfx::kScaleCount - 1); }
    int padNote(int r, int c) { return std::min(127, 12 * (curPage() - 1) + 56 + c - 8 * r); }
    bool padOn(int r, int c) { return route[curPat()][r][c]; }
    bool padAcc(int r, int c) { return acc[curPat()][r][c]; }
    void togglePad(int r, int c) { bool& b = route[curPat()][r][c]; b = !b; if (!b) acc[curPat()][r][c] = false; }
    // right-click: send <-> accent; an empty pad becomes an accent
    void toggleAccent(int r, int c) {
        int p = curPat();
        if (!route[p][r][c]) { route[p][r][c] = acc[p][r][c] = true; return; }
        acc[p][r][c] = !acc[p][r][c];
    }
    float stripGet(int i) { return val[bank()][i]; }
    void stripSet(int i, float x) { val[bank()][i] = clamp(x, 0.f, 1.f); }
    bool fedByLink(int s) { return s > 0 && link[s - 1]; }

    const char* bankName(int b, int s) {
        if (b >= mfx::B_P1 && b <= mfx::B_P4) {
            const char* n = mfx::kFx[slotType[s]].pn[b - 1];
            return n[0] ? n : "-";
        }
        return mfx::kBankFx[clamp(b, 0, 7)];
    }
    void bankValue(int b, int s, char* buf, int n) {
        float v = val[b][s];
        switch (b) {
            case mfx::B_LEVEL: snprintf(buf, n, "%d%%", (int)std::round(v * 100.f)); break;
            case mfx::B_TONE:
                if (std::fabs(v - 0.5f) < 0.02f) snprintf(buf, n, "FLAT");
                else snprintf(buf, n, "%s %d", v < 0.5f ? "DARK" : "BRIGHT", (int)std::round(std::fabs(v - 0.5f) * 200.f));
                break;
            case mfx::B_PAN:
                if (std::fabs(v - 0.5f) < 0.02f) snprintf(buf, n, "C");
                else snprintf(buf, n, "%c%d", v < 0.5f ? 'L' : 'R', (int)std::round(std::fabs(v - 0.5f) * 200.f));
                break;
            case mfx::B_WIDTH: snprintf(buf, n, "%d%%", (int)std::round(v * 200.f)); break;
            default: mfx::fmtParam(slotType[s], b - 1, v, root(), scale(), buf, n); break;
        }
    }

    // Settings for slot s inside its effect's safe range (synced times from 1/8 to a bar, pitched effects on
    // chord tones), and level / tone / pan / width that keep the mix balanced.
    void randomSettings(int s) {
        auto rnd = []() { return random::uniform(); };
        auto pick = [&](int n) { return std::min((int)(rnd() * n), n - 1); };
        int t = slotType[s];
        const mfx::FxDef& d = mfx::kFx[t];
        for (int k = 0; k < 4; k++) {
            if (d.fmt[k] == mfx::F_NONE) continue;
            float v = d.lo[k] + (d.hi[k] - d.lo[k]) * rnd();
            if (d.fmt[k] == mfx::F_DIV) {
                static const int kDiv[4] = {6, 8, 9, 10};   // 1/8, 1/8D, 1/4, 1/4D
                int i = t == mfx::T_THROW ? (rnd() < 0.6f ? 12 : 10) : t == mfx::T_PING ? (k == 0 ? 6 : 8 + pick(2))
                      : t == mfx::T_TIGHT ? (rnd() < 0.6f ? 3 : 6) : kDiv[pick(4)];
                v = i / 12.f;
            } else if (d.fmt[k] == mfx::F_NOTE) {
                static const int kTone[4] = {0, 2, 4, 7};   // root, 3rd, 5th, octave
                v = kTone[pick(4)] / 14.f;
            } else if (d.fmt[k] == mfx::F_WARP) {
                static const int kW[4] = {0, 4, 5, 6};      // -12, +5, +7, +12
                v = kW[pick(4)] / 6.f;
            }
            val[1 + k][s] = v;
        }
        bool send = mfx::isSend(t);
        val[mfx::B_LEVEL][s] = send ? 0.45f + 0.25f * rnd() : 0.6f + 0.2f * rnd();
        val[mfx::B_TONE][s] = 0.4f + 0.2f * rnd();
        val[mfx::B_PAN][s] = 0.3f + 0.4f * rnd();
        val[mfx::B_WIDTH][s] = 0.5f + 0.3f * rnd();
    }

    // RAND FX (UI thread: it builds effects): new effects on all 8 slots, as musical chains. The slots split
    // into groups of 1..4; each group is linked in signal-flow order (drive, filter, mod, glitch, delay,
    // reverb, then sometimes a comp or limiter), no effect twice, and the set always holds at least one delay
    // and one reverb. Settings stay in each effect's safe range; the routing scenes are left alone.
    void randomizeSlots() {
        auto rnd = []() { return random::uniform(); };
        auto pick = [&](int n) { return std::min((int)(rnd() * n), n - 1); };
        std::vector<int> pool[mfx::TAB_CHAIN];
        for (int t = 0; t < mfx::T_COUNT; t++) pool[mfx::kFx[t].tab].push_back(t);
        static const int kEnd[3] = {mfx::T_GLUE, mfx::T_LIMIT, mfx::T_TAPECOMP};   // fine after anything
        bool used[mfx::T_COUNT] = {};
        auto take = [&](int tab) {
            for (int i = 0; i < 16; i++) { int t = pool[tab][pick((int)pool[tab].size())]; if (!used[t]) { used[t] = true; return t; } }
            for (int t : pool[tab]) if (!used[t]) { used[t] = true; return t; }
            return pool[tab][0];
        };
        int type[8], groupEnd[8], ng = 0;
        bool lk[8] = {};
        for (int s = 0; s < 8;) {
            int len = std::min(8 - s, s == 0 ? 2 + pick(3) : 1 + pick(4));   // the first group is always a chain
            // pick len stages from drive .. reverb, in order; a chain of 2+ ends on an echo or reverb most times
            std::vector<int> tabs;
            bool comp = len >= 3 && rnd() < 0.25f;
            int n = len - (comp ? 1 : 0);
            std::vector<int> order = {mfx::TAB_DRIVE, mfx::TAB_FILTER, mfx::TAB_MOD, mfx::TAB_GLITCH, mfx::TAB_DELAY, mfx::TAB_VERB};
            if (n >= 2 && rnd() < 0.75f) { tabs.push_back(rnd() < 0.5f ? mfx::TAB_DELAY : mfx::TAB_VERB); order.resize(4); }
            while ((int)tabs.size() < n && !order.empty()) {
                int i = pick((int)order.size());
                tabs.push_back(order[i]);
                order.erase(order.begin() + i);
            }
            std::sort(tabs.begin(), tabs.end());
            for (int i = 0; i < (int)tabs.size(); i++) type[s + i] = take(tabs[i]);
            if (comp) { int t = kEnd[pick(3)]; if (used[t]) t = mfx::T_LIMIT; used[t] = true; type[s + len - 1] = t; }
            for (int i = 0; i < len - 1; i++) lk[s + i] = true;
            groupEnd[ng++] = s + len - 1;
            s += len;
        }
        // the dub space: make sure an echo and a reverb are there (on a group's end, so the order holds)
        for (int tab : {mfx::TAB_DELAY, mfx::TAB_VERB}) {
            bool have = false;
            for (int s = 0; s < 8; s++) if (mfx::kFx[type[s]].tab == tab) have = true;
            if (have) continue;
            for (int g = ng - 1; g >= 0; g--) {
                int s = groupEnd[g], ct = mfx::kFx[type[s]].tab;
                if (ct == mfx::TAB_DELAY || ct == mfx::TAB_VERB) continue;
                used[type[s]] = false;
                type[s] = take(tab);
                break;
            }
        }
        for (int s = 0; s < 8; s++) {
            link[s] = lk[s];
            setSlotType(s, type[s], true);
            randomSettings(s);
        }
    }

    // RAND: musical and fail-safe. A new key (minor-leaning). Every slot keeps its effect and gets settings
    // inside the effect's safe range (synced times from 1/8 to a bar, pitched effects on chord tones).
    // Routing: a theme (each input to one main slot, favouring the echoes and reverbs, sometimes a second
    // send; no slot takes more than four inputs, chain members only through their head), accents on the main
    // sends in the beat columns (1, 3, 5, 7). Scenes 2..8 vary it a little; 4 and 8 throw every input into
    // a delay or reverb.
    void randomizeAll() {
        auto rnd = []() { return random::uniform(); };
        auto pick = [&](int n) { return std::min((int)(rnd() * n), n - 1); };
        float u = rnd();
        params[ROOT_PARAM].setValue((float)pick(12));
        params[SCALE_PARAM].setValue(u < 0.5f ? 0.f : u < 0.75f ? 1.f : u < 0.9f ? 2.f : 3.f);
        for (int s = 0; s < 8; s++) randomSettings(s);
        std::vector<int> heads, sends;
        for (int s = 0; s < 8; s++) if (!fedByLink(s)) { heads.push_back(s); if (mfx::isSend(slotType[s])) sends.push_back(s); }
        if (heads.empty()) return;
        int load[8] = {};
        auto pickHead = [&](int avoid) {
            float tot = 0.f;
            for (int s : heads) if (s != avoid && load[s] < 4) tot += mfx::isSend(slotType[s]) ? 2.f : 1.f;
            if (tot <= 0.f) return -1;
            float x = rnd() * tot;
            for (int s : heads) {
                if (s == avoid || load[s] >= 4) continue;
                x -= mfx::isSend(slotType[s]) ? 2.f : 1.f;
                if (x <= 0.f) return s;
            }
            return -1;
        };
        int main[8], second[8];
        for (int r = 0; r < 8; r++) {
            main[r] = pickHead(-1);
            if (main[r] >= 0) load[main[r]]++;
            second[r] = rnd() < 0.5f ? pickHead(main[r]) : -1;
            if (second[r] >= 0) load[second[r]]++;
        }
        for (int p = 0; p < 8; p++) {
            clearScene(p);
            int changes = p == 0 ? 0 : 1 + pick(2);
            for (int r = 0; r < 8; r++) {
                if (main[r] >= 0) { route[p][r][main[r]] = true; acc[p][r][main[r]] = main[r] % 2 == 0; }
                if (second[r] >= 0) route[p][r][second[r]] = true;
            }
            for (int i = 0; i < changes; i++) {   // a variation: drop a second send, or add one
                int r = pick(8), c = heads[pick((int)heads.size())];
                if (second[r] >= 0 && rnd() < 0.5f) route[p][r][second[r]] = false;
                else if (c != main[r]) route[p][r][c] = true;
            }
            if ((p == 3 || p == 7) && !sends.empty())   // the fill: a throw into an echo or reverb on every input
                for (int r = 0; r < 8; r++) { int c = sends[pick((int)sends.size())]; route[p][r][c] = acc[p][r][c] = true; }
        }
    }

    void sendNote(int n, int vel) { midi::Message m; m.bytes = {0x90, (uint8_t)n, (uint8_t)clamp(vel, 0, 127)}; if (slowSend) std::this_thread::sleep_for(std::chrono::microseconds(400)); midiOut.sendMessage(m); }
    void sendCC(int cc, int v) { midi::Message m; m.bytes = {0xB0, (uint8_t)cc, (uint8_t)clamp(v, 0, 127)}; if (slowSend) std::this_thread::sleep_for(std::chrono::microseconds(400)); midiOut.sendMessage(m); }

    void clearLeds() {
        for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) sendNote(padNote(r, c), 0);
        for (int i = 0; i < 8; i++) { sendCC(i, 0); sendCC(8 + i, 0); sendCC(48 + 8 * stripPage + i, 0); }
        for (int i = 0; i < kNumBtn; i++) sendCC(kBtnCC[i], 0);
        for (auto& n : kBtnNote) if (n[0] > padNote(0, 7)) sendNote(n[0], 0);
        for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
    }
    void onRemove(const RemoveEvent& e) override { if (jam.active) clearLeds(); jamown::release(id); }
    void onBypass(const BypassEvent& e) override { if (jam.active) clearLeds(); jamown::release(id); jam.active = false; }
    void onUnBypass(const UnBypassEvent& e) override { for (int i = 0; i < kNumLed; i++) lastLed[i] = -1; resync = 0.f; }

    void jamButton(int cc, int b) {
        bool down = b > 0;
        if (cc == CC_SOLO) { soloHeld = down; return; }
        if (cc == CC_MUTE) { muteHeld = down; return; }
        if (cc == CC_GRID) { gridHeld = down; return; }
        if (cc == CC_CLEAR && !down) { clearHeld = false; return; }
        if (!down) return;
        switch (cc) {
            case CC_CLEAR: clearScene(curPat()); clearHeld = true; clearHeldT = 0.f; clearAllDone = false; break;
            case CC_DUPLICATE: duplicateScene(); break;
            case CC_UP: stepParam(LEN_PARAM, 1, 1, 8, false); break;
            case CC_DOWN: stepParam(LEN_PARAM, -1, 1, 8, false); break;
            case CC_LEFT: stepParam(BANK_PARAM, -1, 1, 8, false); break;
            case CC_RIGHT: stepParam(BANK_PARAM, 1, 1, 8, false); break;
            case CC_PLAY: params[RUN_PARAM].setValue(params[RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f); break;
            case CC_REC: params[LEARN_PARAM].setValue(params[LEARN_PARAM].getValue() > 0.5f ? 0.f : 1.f); break;
            case CC_PREV: stepParam(PAT_PARAM, -1, 1, 8, true); break;
            case CC_NEXT: stepParam(PAT_PARAM, 1, 1, 8, true); break;
            case CC_TEMPO: jamReset = true; break;
        }
    }

    int desiredLed(int idx) {
        if (idx == kKnobLed) return (int)std::round(params[knobParam()].getValue() * 127.f);
        if (idx >= 88) {
            switch (kBtnCC[idx - 88]) {
                case CC_SONG: return 0;
                case CC_PLAY: return running ? 127 : 0;
                case CC_REC: return params[LEARN_PARAM].getValue() > 0.5f ? 127 : 0;
                case CC_TEMPO: return resetFlash > 0.f ? 127 : 0;
                case CC_GRID: return gridHeld ? 127 : 0;
                case CC_SOLO: return soloHeld || anySolo() ? 127 : 0;
                case CC_MUTE: return muteHeld || anyMute() ? 127 : 0;
                default: return 127;
            }
        }
        if (idx < 64) {   // send = the scene's bright colour, accent = white; empty = dark except the top and bottom rows
            int r = idx / 8, c = idx % 8, p = curPat();
            bool a = padOn(r, c), edge = r == 0 || r == 7;
            if (!trackOn(r)) return a ? kJamDim[p] : 0;
            if (a) return padAcc(r, c) ? kJamWhite : kJamBright[p];
            return edge ? kJamDim[p] : 0;
        }
        if (idx < 72) return idx - 64 == curPat() ? kJamBright[idx - 64] : kJamDim[idx - 64];
        if (idx < 80) return (int)std::round(stripGet(idx - 72) * 127.f);
        return idx - 80 == bank() ? kJamBright[idx - 80] : kJamDim[idx - 80];
    }
    void sendLed(int idx, int d) {
        if (!jam.active) return;
        if (idx < 64) sendNote(padNote(idx / 8, idx % 8), d);
        else if (idx < 72) sendCC(idx - 64, d);
        else if (idx < 80) sendCC(48 + 8 * stripPage + (idx - 72), d);
        else if (idx < 88) sendCC(8 + (idx - 80), d);
        else if (idx == kKnobLed) sendCC(knobCC, d);
        else {
            int cc = kBtnCC[idx - 88];
            sendCC(cc, d);
            int n = btnNoteFor(cc);
            if (n >= 0 && n > padNote(0, 7)) sendNote(n, d);
        }
    }

    bool press(int i, int param) {   // a momentary cell's rising edge
        bool b = params[param].getValue() > 0.5f, e = b && !loopPrev[i];
        loopPrev[i] = b;
        return e;
    }

    void process(const ProcessArgs& args) override {
        const float sr = args.sampleRate, dt = args.sampleTime;
        const float songG = songPoll(dt);   // MATRIX-SONG: scene / loop requests, and its mute on OUT L / R
        int own = jam.update(id, params[MIDI_ON_PARAM].getValue() > 0.5f);   // losing the JAM goes dark, gaining it resends all
        if (own > 0) for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
        else if (own < 0) clearLeds();
        bool midiOn = jam.active;
        midi::Message msg;
        while (midiIn.tryPop(&msg, args.frame)) {
            if (msg.bytes.size() < 3 || !midiOn) continue;
            int st = msg.bytes[0] & 0xf0, a = msg.bytes[1], b = msg.bytes[2];
            if (st == 0x90 && b > 0) {
                if (params[LEARN_PARAM].getValue() > 0.5f) {
                    int d = a - 56;
                    if (d >= 0 && d % 12 == 0 && d / 12 <= 5) {
                        params[PAGE_PARAM].setValue((float)(d / 12 + 1));
                        params[LEARN_PARAM].setValue(0.f);
                    }
                    continue;
                }
                bool pad = false;
                for (int r = 0; r < 8 && !pad; r++) for (int c = 0; c < 8; c++)
                    if (padNote(r, c) == a) {
                        if (muteHeld) mute[r] = !mute[r];
                        else if (soloHeld) solo[r] = !solo[r];
                        else if (gridHeld) toggleAccent(r, c);
                        else togglePad(r, c);
                        pad = true; break;
                    }
                if (!pad)
                    for (auto& n : kBtnNote) if (n[0] == a) jamButton(n[1], b);
            } else if (st == 0xB0) {
                bool isBtn = false;
                for (int i = 0; i < kNumBtn; i++) if (kBtnCC[i] == a) isBtn = true;
                if (isBtn) jamButton(a, b);
                else if (a >= kKnobFirstCC && a <= kKnobLastCC) {
                    knobCC = a;
                    params[knobParam()].setValue(b / 127.f);
                    lastLed[kKnobLed] = b;
                }
                else if (a == kKnobPushCC && b > 0) paramQuantities[knobParam()]->reset();
                else if (a >= 0 && a < 8 && b > 0) params[PAT_PARAM].setValue((float)(a + 1));
                else if (a >= 8 && a < 16 && b > 0) params[BANK_PARAM].setValue((float)(a - 8 + 1));
                else if (a >= 48 && a < 112) {
                    stripPage = (a - 48) / 8;
                    stripSet((a - 48) % 8, b / 127.f);
                }
            }
        }

        // ---- clock: the CLOCK jack (16ths) or the BPM knob ----
        if (runTrig.process(inputs[RUN_INPUT].getVoltage()))
            params[RUN_PARAM].setValue(params[RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f);
        running = params[RUN_PARAM].getValue() > 0.5f;
        bool edge = false;
        sinceClock += dt;
        bool ext = inputs[CLOCK_INPUT].isConnected();
        float intStep = 15.f / clamp(params[BPM_PARAM].getValue(), 30.f, 300.f);
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
        stepSecNow = (ext && clockPeriod > 0.f && sinceClock < 2.f) ? clockPeriod : ext ? 0.125f : intStep;
        bool rst = rstTrig.process(inputs[RESET_INPUT].getVoltage()) | rstBtnTrig.process(params[RESET_PARAM].getValue()) | jamReset;
        jamReset = false;
        if (rst) { tick = -1; pendingReset = true; resetFlash = 0.15f; internalTimer = 0.f; }
        resetFlash = std::max(0.f, resetFlash - dt);
        edge = edge && running;
        int len = clamp((int)params[LEN_PARAM].getValue(), 1, 8);
        bool restart = false;
        if (edge) {
            restart = pendingReset || tick < 0;   // (re)started: a playing loop goes back to its top
            tick = restart ? 0 : (tick + 1) % 16;
            if (tick == 0 && len > 1) {   // the scene loop: one scene per bar through 1..LEN
                if (!pendingReset) params[PAT_PARAM].setValue((float)((curPat() + 1) % len + 1));
                else if (curPat() >= len) params[PAT_PARAM].setValue(1.f);
            }
            pendingReset = false;
        }
        step = len > 1 ? curPat() : -1;

        if (clrTrig.process(params[CLEAR_PARAM].getValue())) clearScene(curPat());
        if (dupTrig.process(params[DUP_PARAM].getValue())) duplicateScene();
        if (randTrig.process(params[RAND_PARAM].getValue())) randomizeAll();
        if (clearHeld && !clearAllDone && (clearHeldT += dt) > 1.f) {
            for (int p = 0; p < 8; p++) clearScene(p);
            clearAllDone = true;
        }

        // ---- tape loop: locked to the beat (4 clock steps) while running, as MATRIX-ECHO's looper ----
        // (stopped, or a CLOCK that has gone quiet: no tempo, the transport acts at once and records free)
        bool tempoOn = running && (!ext || (clockPeriod > 0.f && sinceClock < std::max(2.f, 3.f * clockPeriod)));
        float beatSmp = tempoOn ? 4.f * stepSecNow * sr : 0.f;
        bool beatEdge = edge && tempoOn && tick % 4 == 0;
        looper.tempoBeat = beatSmp;
        looper.countIn = clamp((int)std::round(params[COUNTIN_PARAM].getValue()), 0, 4);
        looper.gridBeats = 1;
        looper.sync(beatEdge, beatEdge, beatSmp, restart && beatEdge);
        if (press(0, REC_PARAM)) looper.press(EchoLooper::B_REC);
        if (press(1, PLAY_PARAM)) looper.press(EchoLooper::B_PLAY);
        if (press(2, ONCE_PARAM)) looper.press(EchoLooper::B_ONCE);
        if (press(3, UNDO_PARAM)) looper.undo();
        if (press(4, LCLEAR_PARAM)) looper.clear();
        // LOOPLEN (the FREE | 1-4 BT | 1-4 BAR cells) is the record length: a new take records that many beats
        // from its first beat, then plays; FREE records until REC / PLAY / ONCE. Loops play in full.
        looper.setLoopBeats(0);
        looper.recBeats = std::max(0, (int)std::round(params[LOOPLEN_PARAM].getValue()));

        // ---- inputs (+-1 = +-5 V); a 2-channel cable is stereo ----
        float inL[8], inR[8], dryL = 0.f, dryR = 0.f;
        float mDecay = std::exp(-dt / 0.3f);
        for (int r = 0; r < 8; r++) {
            Input& in = inputs[IN_INPUT + r];
            inL[r] = in.getVoltage(0) / 5.f;
            inR[r] = in.getChannels() > 1 ? in.getVoltage(1) / 5.f : inL[r];
            if (!trackOn(r)) inL[r] = inR[r] = 0.f;   // a muted input leaves the dry mix and every send
            dryL += inL[r]; dryR += inR[r];
            inMeter[r] = std::max(inMeter[r] * mDecay, std::max(std::fabs(inL[r]), std::fabs(inR[r])));
        }

        mfx::Ctx ctx;
        ctx.sr = sr; ctx.stepSec = stepSecNow; ctx.edge = edge; ctx.stepIdx = std::max(tick, 0);
        ctx.root = root(); ctx.scale = scale();
        ctx.sc = inputs[SC_INPUT].isConnected() ? std::fabs(inputs[SC_INPUT].getVoltage()) / 5.f : -1.f;

        // ---- the slots, in order, so a link feeds the next slot in the same sample ----
        int p = curPat();
        float kSend = 1.f - std::exp(-dt / 0.006f);
        float busL = 0.f, busR = 0.f, linkL = 0.f, linkR = 0.f;
        bool linkIn = false;
        for (int s = 0; s < 8; s++) {
            float xl = 0.f, xr = 0.f;
            for (int r = 0; r < 8; r++) {
                float tg = route[p][r][s] ? (acc[p][r][s] ? 1.f : 0.7f) : 0.f;   // plain sends ~3 dB under an accent
                sendG[r][s] += (tg - sendG[r][s]) * kSend;
                xl += inL[r] * sendG[r][s]; xr += inR[r] * sendG[r][s];
            }
            float fl = linkIn ? linkL : 0.f, fr = linkIn ? linkR : 0.f;
            xl += fl; xr += fr;
            // swap in a newly loaded effect: fade out, swap, fade in
            if (pending[s].load()) {
                swapGain[s] -= dt / 0.01f;
                if (swapGain[s] <= 0.f) {
                    swapGain[s] = 0.f;
                    if (!retired[s].load())
                        if (mfx::Fx* n = pending[s].exchange(NULL)) { retired[s].store(fx[s]); fx[s] = n; idleT[s] = 0.f; }
                }
            } else swapGain[s] = std::min(1.f, swapGain[s] + dt / 0.01f);
            // a slot that has heard nothing and rung out for 3 s sleeps
            float ol = 0.f, orr = 0.f;
            bool quietIn = std::fabs(xl) + std::fabs(xr) < 1e-6f;
            if (fx[s] && !(quietIn && idleT[s] > 3.f)) {
                float pv[4] = {val[1][s], val[2][s], val[3][s], val[4][s]};
                fx[s]->process(xl, xr, pv, ctx, ol, orr);
                if (!std::isfinite(ol) || !std::isfinite(orr)) { ol = orr = 0.f; }
                float bv[8];
                for (int b = 0; b < 8; b++) bv[b] = val[b][s];
                post[s].process(ol, orr, bv, sr);
                ol *= swapGain[s]; orr *= swapGain[s];
                idleT[s] = quietIn && std::fabs(ol) + std::fabs(orr) < 1e-6f ? idleT[s] + dt : 0.f;
            }
            if (linkIn && mfx::isSend(slotType[s])) { ol += fl; orr += fr; }   // a delay / reverb in a chain passes it on
            fxMeter[s] = std::max(fxMeter[s] * mDecay, std::max(std::fabs(ol), std::fabs(orr)));
            linkIn = s < 7 && link[s];
            if (linkIn) { linkL = ol; linkR = orr; }
            else { busL += ol; busR += orr; }
        }

        // ---- dry + returns -> DJ filter -> tape loop -> master ----
        float k = 1.f - std::exp(-dt * 1000.f);
        drySm += (params[DRY_PARAM].getValue() - drySm) * k;
        wetSm += (params[WET_PARAM].getValue() - wetSm) * k;
        loopSm += (params[LOOP_LVL_PARAM].getValue() - loopSm) * k;
        filtSm += (params[FILT_PARAM].getValue() - filtSm) * (1.f - std::exp(-dt * 60.f));
        masterVol += (params[KNOB_PARAM].getValue() - masterVol) * k;
        float l = dryL * drySm + busL * wetSm, r = dryR * drySm + busR * wetSm;
        bool hpMode = filtSm > 0.5f, pre = params[LOOPPRE_PARAM].getValue() > 0.5f;
        float lL, lR;
        if (pre) { looper.process(l, r, lL, lR); l += lL * loopSm; r += lR * loopSm; }   // the filter sweeps the loop too
        dj.process(l, r, hpMode ? (filtSm - 0.5f) * 2.f : filtSm * 2.f, params[RES_PARAM].getValue(), sr, 0.f, hpMode ? 2.f : 0.f);
        if (!pre) { looper.process(l, r, lL, lR); l += lL * loopSm; r += lR * loopSm; }
        outputs[OUT_L_OUTPUT].setVoltage(clamp(5.f * mfx::safe(l * masterVol), -11.f, 11.f) * songG);
        outputs[OUT_R_OUTPUT].setVoltage(clamp(5.f * mfx::safe(r * masterVol), -11.f, 11.f) * songG);
        if (prevSt == EchoLooper::REC && looper.st != EchoLooper::REC) takeGen++;
        prevSt = looper.st;
        blink += dt;
        if (blink >= 0.25f) blink -= 0.25f;
        echoloop::flashes(looper, blink, recFlash, playFlash, onceFlash);

        // ---- JAM lights ----
        if (!midiOn) return;
        resync += dt;
        if (curPage() != lastPage || bank() != lastBank || stripPage != lastStripPage || resync > 3.f) {
            for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
            lastPage = curPage(); lastBank = bank(); lastStripPage = stripPage; resync = 0.f;
        }
        tokens = std::min(16.f, tokens + dt * 2000.f);
        for (int n = 0; n < 3; n++) {
            int d = desiredLed(ledScan);
            if (d != lastLed[ledScan]) {
                if (tokens < 1.f) break;
                tokens -= 1.f;
                sendLed(ledScan, d);
                lastLed[ledScan] = d;
            }
            ledScan = (ledScan + 1) % kNumLed;
        }
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        auto grid = [&](bool (*g)[8][8]) {
            json_t* arr = json_array();
            for (int p = 0; p < 8; p++) {
                std::string s;
                for (int i = 0; i < 64; i++) s += g[p][i / 8][i % 8] ? '1' : '0';
                json_array_append_new(arr, json_string(s.c_str()));
            }
            return arr;
        };
        json_object_set_new(root, "routes", grid(route));   // 64 chars per scene, row (input) by row
        json_object_set_new(root, "accents", grid(acc));
        json_t* va = json_array();
        for (int b = 0; b < 8; b++) for (int i = 0; i < 8; i++) json_array_append_new(va, json_real(val[b][i]));
        json_object_set_new(root, "values", va);
        json_t* sl = json_array();
        for (int s = 0; s < 8; s++) json_array_append_new(sl, json_integer(slotType[s]));
        json_object_set_new(root, "slots", sl);
        std::string ls, ms, ss;
        for (int i = 0; i < 8; i++) { ls += link[i] ? '1' : '0'; ms += mute[i] ? '1' : '0'; ss += solo[i] ? '1' : '0'; }
        json_object_set_new(root, "links", json_string(ls.c_str()));
        json_object_set_new(root, "mute", json_string(ms.c_str()));
        json_object_set_new(root, "solo", json_string(ss.c_str()));
        json_object_set_new(root, "sel", json_integer(selSlot));
        json_object_set_new(root, "tab", json_integer(tab));
        json_object_set_new(root, "loopPlay", json_boolean(looper.playing()));   // its audio: onSave
        json_object_set_new(root, "in", midiIn.toJson());
        json_object_set_new(root, "out", midiOut.toJson());
        return root;
    }
    void dataFromJson(json_t* r) override {
        auto grid = [&](const char* key, bool (*g)[8][8]) {
            json_t* arr = json_object_get(r, key);
            for (int p = 0; p < 8; p++) {
                const char* s = arr ? json_string_value(json_array_get(arr, p)) : NULL;
                for (int i = 0; i < 64; i++) g[p][i / 8][i % 8] = s && strlen(s) >= 64 && s[i] == '1';
            }
        };
        grid("routes", route);
        grid("accents", acc);
        if (json_t* va = json_object_get(r, "values"))
            for (int i = 0; i < 64; i++)
                if (json_t* j = json_array_get(va, i)) val[i / 8][i % 8] = clamp((float)json_number_value(j), 0.f, 1.f);
        if (json_t* sl = json_object_get(r, "slots"))
            for (int s = 0; s < 8; s++)
                if (json_t* j = json_array_get(sl, s)) slotType[s] = clamp((int)json_integer_value(j), 0, mfx::T_COUNT - 1);
        auto bits = [&](const char* key, bool* out) {
            const char* s = json_string_value(json_object_get(r, key));
            for (int i = 0; i < 8; i++) out[i] = s && strlen(s) >= 8 && s[i] == '1';
        };
        bits("links", link);
        bits("mute", mute);
        bits("solo", solo);
        if (json_t* j = json_object_get(r, "sel")) selSlot = clamp((int)json_integer_value(j), 0, 7);
        if (json_t* j = json_object_get(r, "tab")) tab = clamp((int)json_integer_value(j), 0, mfx::TAB_COUNT - 1);
        if (json_t* j = json_object_get(r, "in")) midiIn.fromJson(j);
        if (json_t* j = json_object_get(r, "out")) midiOut.fromJson(j);
        if (json_t* j = json_object_get(r, "loopPlay")) { loadPlay = json_is_true(j) ? 1 : 0; applyLoadPlay(); }
        rebuildAll();   // the engine is locked while a patch or preset loads
    }
};

typedef SynTHBloKMatrixFx MX;

// ---------------------------------------------------------------- widgets ----------------

static std::shared_ptr<window::Font> uiFont() { return APP->window->uiFont; }

// The SETUP OLED: header (scene, key), effect tabs, the tab's effects (or chains), the 8 slot cells with
// their links, then the selected slot's 8 strip banks. Click a tab; click an effect to load it into the
// selected slot, or drag it onto a slot cell, a slot name under the grid, or a pad. Drag a slot onto
// another to swap them. Click between two slots to link them. Right-click a slot for its menu.
struct FxSetupDisplay : OpaqueWidget {
    MX* module = NULL;
    enum Kind { NONE, TAB, ITEM, SLOT, LINK, BANK };
    struct Hit { int kind = NONE, idx = -1; };
    int dragKind = NONE, dragIdx = -1, dragTab = 0;
    bool dragging = false, hoverIn = false;
    Vec hoverPos, releasePos;

    // layout, mm from the top-left
    static constexpr float kTabY = 5.f, kItemY = 10.3f, kItemH = 4.4f, kSlotY = 27.1f, kSlotH = 8.f, kDetY = 36.f, kBankY = 40.4f;
    enum { kItemCols = 4, kItemMax = 12 };   // a tab's effects: 4 x 3 name cells
    static constexpr float kRandFxW = 11.f;   // the RAND FX cell at the right end of the detail line
    float pad() const { return mm2px(2.f); }
    float innerW() const { return box.size.x - 2.f * pad(); }
    Rect tabRect(int i) const {
        float g = mm2px(0.8f), w = (innerW() - g * (mfx::TAB_COUNT - 1)) / mfx::TAB_COUNT;
        return Rect(Vec(pad() + i * (w + g), mm2px(kTabY)), Vec(w, mm2px(oled::kCellH)));
    }
    Rect itemRect(int i) const {
        float g = mm2px(1.f), w = (innerW() - (kItemCols - 1) * g) / kItemCols;
        return Rect(Vec(pad() + (i % kItemCols) * (w + g), mm2px(kItemY + (i / kItemCols) * (kItemH + 0.9f))), Vec(w, mm2px(kItemH)));
    }
    Rect slotRect(int s) const {
        float g = mm2px(1.6f), w = (innerW() - 7.f * g) / 8.f;
        return Rect(Vec(pad() + s * (w + g), mm2px(kSlotY)), Vec(w, mm2px(kSlotH)));
    }
    Rect linkRect(int s) const {   // the gap after slot s
        Rect a = slotRect(s);
        return Rect(Vec(a.pos.x + a.size.x, a.pos.y), Vec(mm2px(1.6f), a.size.y));
    }
    Rect bankRect(int b) const {
        float g = mm2px(2.f), w = (innerW() - g) / 2.f;
        return Rect(Vec(pad() + (b / 4) * (w + g), mm2px(kBankY + (b % 4) * oled::kRow)), Vec(w, mm2px(oled::kRow)));
    }
    float barX0(const Rect& r) const { return r.pos.x + r.size.x * 0.46f; }
    int curTab() const { return module ? module->tab : 0; }
    int sel() const { return module ? module->selSlot : 2; }
    int typeOf(int s) const { return module ? module->slotType[s] : mfx::kDefaultSlots[s]; }

    Hit hitTest(Vec p) const {
        Hit h;
        for (int i = 0; i < mfx::TAB_COUNT; i++) if (tabRect(i).contains(p)) { h.kind = TAB; h.idx = i; return h; }
        int n = std::min((int)tabItems(curTab()).size(), (int)kItemMax);
        for (int i = 0; i < n; i++) if (itemRect(i).contains(p)) { h.kind = ITEM; h.idx = i; return h; }
        for (int s = 0; s < 8; s++) if (slotRect(s).contains(p)) { h.kind = SLOT; h.idx = s; return h; }
        for (int s = 0; s < 7; s++) if (linkRect(s).contains(p)) { h.kind = LINK; h.idx = s; return h; }
        for (int b = 0; b < 8; b++) if (bankRect(b).contains(p)) { h.kind = BANK; h.idx = b; return h; }
        return h;
    }
    // what a dragged item is called (for the ghost and the pads)
    std::string dragName() const {
        if (dragKind == SLOT) return mfx::kFx[typeOf(dragIdx)].name;
        if (dragKind != ITEM) return "";
        std::vector<int> it = tabItems(dragTab);
        if (dragIdx < 0 || dragIdx >= (int)it.size()) return "";
        return dragTab == mfx::TAB_CHAIN ? mfx::kChain[it[dragIdx]].name : mfx::kFx[it[dragIdx]].name;
    }

    void text(const DrawArgs& args, float x, float y, float size, NVGcolor c, int align, const char* s) {
        nvgFontSize(args.vg, size);
        nvgFillColor(args.vg, c);
        nvgTextAlign(args.vg, align);
        nvgText(args.vg, x, y, s, NULL);
    }
    void cell(const DrawArgs& args, const Rect& r, bool lit, bool outlineHi = false) {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, r.pos.x, r.pos.y, r.size.x, r.size.y, 1.2f);
        nvgFillColor(args.vg, lit ? oled::litFill() : oled::offFill());
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, outlineHi ? nvgRGBA(255, 255, 255, 230) : oled::outline(lit ? 255 : 110));
        nvgStrokeWidth(args.vg, outlineHi ? 1.1f : 0.7f);
        nvgStroke(args.vg);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        std::shared_ptr<window::Font> font = uiFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W, H);
        const int L = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, C = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, R = NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE;
        char buf[64];
        // header
        float y0 = mm2px(oled::kRow * 0.5f);
        text(args, 4.f, y0, 6.f, oled::dim(), L, "SETUP");
        snprintf(buf, sizeof buf, "SCENE %d", module ? module->curPat() + 1 : 1);
        text(args, mm2px(11.f), y0, 7.5f, oled::amber(), L, buf);
        int rt = module ? module->root() : 9, sc = module ? module->scale() : 0;
        snprintf(buf, sizeof buf, "KEY %s %s", mfx::kNoteName[rt], mfx::kScaleName[sc]);
        text(args, W - 4.f, y0, 7.5f, oled::amber(), R, buf);
        snprintf(buf, sizeof buf, "BANK %c", 'A' + (module ? module->bank() : 0));
        text(args, W * 0.5f, y0, 6.f, oled::dim(), C, buf);
        // tabs
        for (int i = 0; i < mfx::TAB_COUNT; i++) {
            Rect r = tabRect(i);
            bool on = i == curTab();
            cell(args, r, on);
            text(args, r.pos.x + r.size.x * 0.5f, r.pos.y + r.size.y * 0.5f, 6.f, on ? oled::ink() : oled::amber(), C, mfx::kTabName[i]);
        }
        // the tab's effects / chains
        std::vector<int> it = tabItems(curTab());
        bool chainTab = curTab() == mfx::TAB_CHAIN;
        for (int i = 0; i < (int)it.size() && i < kItemMax; i++) {   // names only; the detail line below has the description
            Rect r = itemRect(i);
            bool on = !chainTab && it[i] == typeOf(sel());
            bool held = dragging && dragKind == ITEM && dragIdx == i && dragTab == curTab();
            cell(args, r, on, held);
            const char* nm = chainTab ? mfx::kChain[it[i]].name : mfx::kFx[it[i]].name;
            nvgSave(args.vg);
            nvgIntersectScissor(args.vg, r.pos.x + 1.f, r.pos.y, r.size.x - 2.f, r.size.y);
            text(args, r.pos.x + r.size.x * 0.5f, r.pos.y + r.size.y * 0.5f, 5.8f, on ? oled::ink() : oled::amber(), C, nm);
            nvgRestore(args.vg);
        }
        nvgBeginPath(args.vg);
        oled::divider(args, 4.f, mm2px(kSlotY - 0.9f), W - 4.f, mm2px(kSlotY - 0.9f));
        oled::divider(args, 4.f, mm2px(kDetY - 0.45f), W - 4.f, mm2px(kDetY - 0.45f));
        oled::dividerStroke(args);
        // slot cells and links
        Hit hv = hoverIn && dragging ? hitTest(hoverPos) : Hit();
        for (int s = 0; s < 8; s++) {
            Rect r = slotRect(s);
            bool on = s == sel();
            cell(args, r, on, hv.kind == SLOT && hv.idx == s);
            snprintf(buf, sizeof buf, "%d", s + 1);
            text(args, r.pos.x + 2.5f, r.pos.y + 4.f, 5.f, on ? oled::ink() : oled::dim(), L, buf);
            text(args, r.pos.x + r.size.x * 0.5f, r.pos.y + r.size.y * 0.55f, 6.f, on ? oled::ink() : oled::amber(), C, mfx::kFx[typeOf(s)].shortName);
            float m = module ? std::min(1.f, module->fxMeter[s]) : 0.f;   // the slot's return level
            if (m > 0.01f) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, r.pos.x + 2.f, r.pos.y + r.size.y - 2.4f, (r.size.x - 4.f) * m, 1.2f);
                nvgFillColor(args.vg, on ? oled::ink() : oled::green());
                nvgFill(args.vg);
            }
            if (s < 7) {
                Rect g = linkRect(s);
                bool lk = module ? module->link[s] : (s == 1);
                float cx = g.pos.x + g.size.x * 0.5f, cy = g.pos.y + g.size.y * 0.5f;
                nvgBeginPath(args.vg);
                if (lk) { nvgMoveTo(args.vg, cx - 1.6f, cy - 2.4f); nvgLineTo(args.vg, cx + 1.8f, cy); nvgLineTo(args.vg, cx - 1.6f, cy + 2.4f); nvgClosePath(args.vg); }
                else nvgCircle(args.vg, cx, cy, 0.6f);
                nvgFillColor(args.vg, lk ? oled::litFill() : oled::outline(90));
                nvgFill(args.vg);
            }
        }
        // the selected slot
        int s = sel(), t = typeOf(s);
        float yd = mm2px(kDetY + oled::kRow * 0.5f);
        snprintf(buf, sizeof buf, "%d %s", s + 1, mfx::kFx[t].name);
        text(args, 4.f, yd, 7.5f, oled::amber(), L, buf);
        float bx[4];
        nvgTextBounds(args.vg, 4.f, yd, buf, NULL, bx);
        bool fed = module && module->fedByLink(s), lk = module && s < 7 && module->link[s];
        snprintf(buf, sizeof buf, "%s%s%s", mfx::kFx[t].desc, fed ? "  < LINKED IN" : "", lk ? "  > LINKED OUT" : "");
        nvgSave(args.vg);
        nvgIntersectScissor(args.vg, 0.f, 0.f, W - mm2px(kRandFxW + 3.f), H);   // stops short of RAND FX
        text(args, bx[2] + 5.f, yd, 5.5f, oled::dim(), L, buf);
        nvgRestore(args.vg);
        int bk = module ? module->bank() : 0;
        for (int b = 0; b < 8; b++) {
            Rect r = bankRect(b);
            float cy = r.pos.y + r.size.y * 0.42f;
            bool cur = b == bk;
            if (cur) {
                nvgBeginPath(args.vg);
                nvgRoundedRect(args.vg, r.pos.x - 1.f, r.pos.y + 0.5f, r.size.x + 2.f, r.size.y - 1.f, 1.f);
                nvgStrokeColor(args.vg, kPatOnColor[b]);
                nvgStrokeWidth(args.vg, 0.7f);
                nvgStroke(args.vg);
            }
            snprintf(buf, sizeof buf, "%c", 'A' + b);
            text(args, r.pos.x + 1.5f, cy, 6.f, kPatOnColor[b], L, buf);
            const char* nm = module ? module->bankName(b, s) : (b == 0 ? "LEVEL" : b < 5 ? mfx::kFx[t].pn[b - 1] : b == 5 ? "TONE" : b == 6 ? "PAN" : "WIDTH");
            text(args, r.pos.x + 7.f, cy, 6.f, cur ? oled::amber() : oled::dim(), L, nm);
            bool used = b < mfx::B_P1 || b > mfx::B_P4 || mfx::kFx[t].fmt[b - 1] != mfx::F_NONE;
            if (!used) continue;
            float v = module ? module->val[b][s] : (b >= 1 && b <= 4 ? mfx::kFx[t].def[b - 1] : mfx::kBankDef[b]);
            if (module) module->bankValue(b, s, buf, sizeof buf); else snprintf(buf, sizeof buf, "%d%%", (int)(v * 100.f));
            float x0 = barX0(r), x1 = r.pos.x + r.size.x - 1.f;
            text(args, x1, cy, 6.f, oled::amber(), R, buf);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, x0, r.pos.y + r.size.y - 2.6f, x1 - x0, 1.1f);
            nvgFillColor(args.vg, oled::offFill());
            nvgFill(args.vg);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, x0, r.pos.y + r.size.y - 2.6f, (x1 - x0) * clamp(v, 0.f, 1.f), 1.1f);
            nvgFillColor(args.vg, cur ? kPatOnColor[b] : oled::amber(170));
            nvgFill(args.vg);
        }
        // the drag ghost
        if (dragging && hoverIn && (dragKind == ITEM || dragKind == SLOT)) {
            std::string nm = dragName();
            nvgFontSize(args.vg, 7.f);
            float tb[4];
            nvgTextBounds(args.vg, 0.f, 0.f, nm.c_str(), NULL, tb);
            float w = tb[2] - tb[0] + 8.f, x = hoverPos.x + 6.f, y = hoverPos.y - 5.f;
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, x, y, w, 10.f, 1.5f);
            nvgFillColor(args.vg, oled::litFill());
            nvgFill(args.vg);
            text(args, x + 4.f, y + 5.f, 7.f, oled::ink(), L, nm.c_str());
        }
        nvgResetScissor(args.vg);
    }

    void setBank(int b, Vec p) {
        if (!module) return;
        module->params[MX::BANK_PARAM].setValue((float)(b + 1));
        Rect r = bankRect(b);
        float x0 = barX0(r), x1 = r.pos.x + r.size.x - 1.f;
        if (p.x >= x0 - 2.f) module->val[b][module->selSlot] = clamp((p.x - x0) / (x1 - x0), 0.f, 1.f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_RELEASE) releasePos = e.pos;
        if (e.action == GLFW_PRESS && module) {
            Hit h = hitTest(e.pos);
            if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
                dragKind = NONE;
                switch (h.kind) {
                    case TAB: module->tab = h.idx; break;
                    case ITEM: dragKind = ITEM; dragIdx = h.idx; dragTab = module->tab; break;
                    case SLOT: module->selSlot = h.idx; dragKind = SLOT; dragIdx = h.idx; break;
                    case LINK: module->link[h.idx] = !module->link[h.idx]; break;
                    case BANK: setBank(h.idx, e.pos); dragKind = BANK; dragIdx = h.idx; break;
                    default: break;
                }
                e.consume(this);
                return;
            }
            if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
                if (h.kind == SLOT) { slotMenu(h.idx); e.consume(this); return; }
                if (h.kind == BANK) {   // back to the default
                    int b = h.idx, s = module->selSlot;
                    module->val[b][s] = b >= mfx::B_P1 && b <= mfx::B_P4 ? mfx::kFx[module->slotType[s]].def[b - 1] : mfx::kBankDef[b];
                    e.consume(this);
                    return;
                }
            }
        }
        OpaqueWidget::onButton(e);   // consumes the release too, so drops land here
    }
    void onDragStart(const DragStartEvent& e) override { dragging = e.button == GLFW_MOUSE_BUTTON_LEFT && dragKind != NONE; }
    void onDragEnd(const DragEndEvent& e) override { dragging = false; dragKind = NONE; }
    void onDragMove(const DragMoveEvent& e) override {
        if (!module || dragKind != BANK) return;
        Rect r = bankRect(dragIdx);
        float z = APP->scene->rackScroll->getZoom();
        float x0 = barX0(r), x1 = r.pos.x + r.size.x - 1.f;
        float& v = module->val[dragIdx][module->selSlot];
        v = clamp(v + e.mouseDelta.x / z / (x1 - x0), 0.f, 1.f);
    }
    void onDragHover(const DragHoverEvent& e) override {
        hoverPos = e.pos;
        hoverIn = true;
        OpaqueWidget::onDragHover(e);
    }
    void onDragLeave(const DragLeaveEvent& e) override { hoverIn = false; }
    void onDragDrop(const DragDropEvent& e) override {
        if (e.origin != this || !module) return;
        Hit h = hitTest(releasePos);
        if (dragKind == ITEM) {
            std::vector<int> it = tabItems(dragTab);
            if (dragIdx < 0 || dragIdx >= (int)it.size()) return;
            int target = h.kind == SLOT ? h.idx : (h.kind == ITEM && h.idx == dragIdx) ? module->selSlot : -1;   // a click loads the selected slot
            if (target < 0) return;
            if (dragTab == mfx::TAB_CHAIN) module->applyChain(it[dragIdx], target, -1);
            else { module->setSlotType(target, it[dragIdx], true); module->selSlot = target; }
        } else if (dragKind == SLOT && h.kind == SLOT && h.idx != dragIdx) {
            module->swapSlots(dragIdx, h.idx);
            module->selSlot = h.idx;
        }
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        Hit h = hitTest(e.pos);
        if (module && h.kind == BANK) {
            float& v = module->val[h.idx][module->selSlot];
            v = clamp(v + (e.scrollDelta.y > 0.f ? 0.01f : -0.01f), 0.f, 1.f);
            e.consume(this);
            return;
        }
        OpaqueWidget::onHoverScroll(e);
    }

    void slotMenu(int s) {
        MX* m = module;
        ui::Menu* menu = createMenu();
        menu->addChild(createMenuLabel(string::f("Slot %d: %s", s + 1, mfx::kFx[m->slotType[s]].name)));
        for (int tb = 0; tb < mfx::TAB_CHAIN; tb++)
            menu->addChild(createSubmenuItem(mfx::kTabName[tb], "", [=](ui::Menu* sub) {
                for (int t : tabItems(tb))
                    sub->addChild(createCheckMenuItem(mfx::kFx[t].name, "", [=]() { return m->slotType[s] == t; },
                                                      [=]() { m->setSlotType(s, t, true); m->selSlot = s; }));
            }));
        menu->addChild(createSubmenuItem("Start a chain here", "", [=](ui::Menu* sub) {
            for (int c = 0; c < mfx::kChainCount; c++)
                sub->addChild(createMenuItem(mfx::kChain[c].name, mfx::kChain[c].desc, [=]() { m->applyChain(c, s, -1); }));
        }));
        menu->addChild(new MenuSeparator);
        if (s < 7) menu->addChild(createCheckMenuItem("Link into the next slot", "", [=]() { return m->link[s]; }, [=]() { m->link[s] = !m->link[s]; }));
        menu->addChild(createMenuItem("Reset its settings", "", [=]() {
            for (int b = 0; b < 8; b++) m->val[b][s] = b >= mfx::B_P1 && b <= mfx::B_P4 ? mfx::kFx[m->slotType[s]].def[b - 1] : mfx::kBankDef[b];
        }));
        menu->addChild(createMenuItem("Clear its sends in this scene", "", [=]() {
            int p = m->curPat();
            for (int r = 0; r < 8; r++) m->route[p][r][s] = m->acc[p][r][s] = false;
        }));
    }
};

struct FxPad : OpaqueWidget {
    MX* module = NULL;
    int r = 0, c = 0;
    bool dropHover = false;
    void draw(const DrawArgs& args) override {
        bool on = module ? module->padOn(r, c) : (r == c || (r == 3 && c == 4) || (r == 6 && c == 2));
        int p = module ? module->curPat() : 0;
        NVGcolor col = kPatColor[p], onCol = kPatOnColor[p];
        if (module && !module->trackOn(r)) {
            col = nvgLerpRGBA(kIdle, col, 0.35f);
            onCol = nvgLerpRGBA(kIdle, onCol, 0.35f);
        }
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        nvgFillColor(args.vg, on ? onCol : kIdle);
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, dropHover ? nvgRGB(0xff, 0xff, 0xff) : col);
        nvgStrokeWidth(args.vg, dropHover ? 1.4f : 0.8f);
        nvgStroke(args.vg);
        if (on && module && module->padAcc(r, c)) {   // accent: a bright core, as on MATRIX-DRUM
            float m = box.size.x * 0.3f;
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, m, m, box.size.x - 2.f * m, box.size.y - 2.f * m, 0.8f);
            nvgFillColor(args.vg, nvgLerpRGBA(onCol, nvgRGB(0xff, 0xf4, 0xd0), 0.75f));
            nvgFill(args.vg);
        }
    }
    // click = send on / off, right-click = accent
    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && module) {
            if (e.button == GLFW_MOUSE_BUTTON_LEFT) { module->togglePad(r, c); e.consume(this); return; }
            if (e.button == GLFW_MOUSE_BUTTON_RIGHT) { module->toggleAccent(r, c); e.consume(this); return; }
        }
        OpaqueWidget::onButton(e);
    }
    void onDragEnter(const DragEnterEvent& e) override { dropHover = dynamic_cast<FxSetupDisplay*>(e.origin) != NULL; }
    void onDragLeave(const DragLeaveEvent& e) override { dropHover = false; }
    void onDragDrop(const DragDropEvent& e) override {
        dropHover = false;
        FxSetupDisplay* d = dynamic_cast<FxSetupDisplay*>(e.origin);
        if (!d || !module) return;
        if (d->dragKind == FxSetupDisplay::ITEM) module->dropOnPad(d->dragTab, d->dragIdx, r, c);
        else if (d->dragKind == FxSetupDisplay::SLOT) { module->swapSlots(d->dragIdx, c); module->selSlot = c; }
    }
};

// The slot names under the grid (one per column): click to select the slot, drop an effect on it.
struct FxSlotName : OpaqueWidget {
    MX* module = NULL;
    int s = 0;
    bool dropHover = false;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        std::shared_ptr<window::Font> font = uiFont();
        if (!font) return;
        bool on = module ? module->selSlot == s : s == 0;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.f);
        nvgFillColor(args.vg, on ? oled::litFill() : nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, dropHover ? nvgRGBA(255, 255, 255, 230) : oled::outline(on ? 255 : 90));
        nvgStrokeWidth(args.vg, dropHover ? 1.1f : 0.6f);
        nvgStroke(args.vg);
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 5.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? oled::ink() : oled::amber());
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, mfx::kFx[module ? module->slotType[s] : mfx::kDefaultSlots[s]].shortName, NULL);
        if (module && s < 7 && module->link[s]) {   // a link: a small arrow into the next column
            float x = box.size.x + mm2px(0.4f), y = box.size.y * 0.5f;
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, x, y - 1.6f); nvgLineTo(args.vg, x + 1.8f, y); nvgLineTo(args.vg, x, y + 1.6f); nvgClosePath(args.vg);
            nvgFillColor(args.vg, oled::litFill());
            nvgFill(args.vg);
        }
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && module) { module->selSlot = s; e.consume(this); return; }
        OpaqueWidget::onButton(e);
    }
    void onDragEnter(const DragEnterEvent& e) override { dropHover = dynamic_cast<FxSetupDisplay*>(e.origin) != NULL; }
    void onDragLeave(const DragLeaveEvent& e) override { dropHover = false; }
    void onDragDrop(const DragDropEvent& e) override {
        dropHover = false;
        FxSetupDisplay* d = dynamic_cast<FxSetupDisplay*>(e.origin);
        if (!d || !module) return;
        if (d->dragKind == FxSetupDisplay::ITEM) {
            std::vector<int> it = tabItems(d->dragTab);
            if (d->dragIdx < 0 || d->dragIdx >= (int)it.size()) return;
            if (d->dragTab == mfx::TAB_CHAIN) module->applyChain(it[d->dragIdx], s, -1);
            else { module->setSlotType(s, it[d->dragIdx], true); module->selSlot = s; }
        } else if (d->dragKind == FxSetupDisplay::SLOT) { module->swapSlots(d->dragIdx, s); module->selSlot = s; }
    }
};

struct FxPatBtn : OpaqueWidget {
    MX* module = NULL;
    int p = 0;
    void draw(const DrawArgs& args) override {
        drawColorButton(args, box.size, p, module ? module->curPat() == p : p == 0, (char)('1' + p), 11.f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[MX::PAT_PARAM].setValue((float)(p + 1));
            e.consume(this);
        }
    }
};

// Touch strip: the selected bank's value for this slot.
struct FxStrip : OpaqueWidget {
    MX* module = NULL;
    int idx = 0;
    float get() { return module ? module->stripGet(idx) : 0.25f + 0.08f * idx; }
    void set(float v) { if (module) module->stripSet(idx, v); }
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        nvgFillColor(args.vg, nvgRGB(0x14, 0x14, 0x14));
        nvgFill(args.vg);
        NVGcolor col = kPatOnColor[module ? module->bank() : 0];
        float v = get(), h = (box.size.y - 3.f) * v;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 1.5f, box.size.y - 1.5f - h, box.size.x - 3.f, h, 1.f);
        nvgFillColor(args.vg, col);
        nvgFill(args.vg);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        nvgStrokeColor(args.vg, nvgLerpRGBA(kIdle, col, 0.45f));
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            set(1.f - clamp((e.pos.y - 1.5f) / (box.size.y - 3.f), 0.f, 1.f));
            if (module) module->selSlot = idx;
            e.consume(this);
        }
    }
    void onDragMove(const DragMoveEvent& e) override {
        float z = APP->scene->rackScroll->getZoom();
        set(get() - e.mouseDelta.y / z / (box.size.y - 3.f));
    }
};

struct FxBankSq : OpaqueWidget {
    MX* module = NULL;
    int b = 0;
    void draw(const DrawArgs& args) override {
        drawColorButton(args, box.size, b, module ? module->bank() == b : b == 0, (char)('A' + b), 9.f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[MX::BANK_PARAM].setValue((float)(b + 1));
            e.consume(this);
        }
    }
};

struct FxTitle : TransparentWidget {
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font = uiFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
        nvgFontSize(args.vg, 11.f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgText(args.vg, 0.f, mm2px(3.5f), "SynTHBloK", NULL);
        nvgFontSize(args.vg, 10.f);
        nvgTextLetterSpacing(args.vg, 0.8f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0x7a, 0x00));
        nvgText(args.vg, 0.f, mm2px(6.4f), "MATRIX-FX", NULL);
    }
};

// The master OLED, as on MATRIX FORGE: MIDI IN, MIDI OUT (click = port menu), BPM / run / scene / bank,
// then the buttons (oled::Cell) laid over it.
static const float kMxRow = oled::kRow, kMxBtn = 3.f * kMxRow + 1.f, kMxH = kMxBtn + 2.f * oled::kCellH + 0.9f + 1.f;
struct FxMasterDisplay : TransparentWidget {
    MX* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        nvgBeginPath(args.vg);
        for (float y : {kMxRow, 2.f * kMxRow, 3.f * kMxRow}) oled::divider(args, 4.f, mm2px(y), W - 4.f, mm2px(y));
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = uiFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W, H);
        oled::midiLine(args, module ? &module->midiIn : NULL, "IN", 0.f);
        oled::midiLine(args, module ? &module->midiOut : NULL, "OUT", mm2px(kMxRow));
        char bpm[24] = "120 BPM", info[16] = "SC 1", bank[24] = "A LEVEL";
        bool run = module ? module->running : true;
        if (module) {
            if (!module->inputs[MX::CLOCK_INPUT].isConnected()) snprintf(bpm, sizeof bpm, "%d BPM", (int)module->params[MX::BPM_PARAM].getValue());
            else if (module->clockPeriod > 0.f && module->sinceClock < 2.f) snprintf(bpm, sizeof bpm, "%.1f BPM", 60.f / (module->clockPeriod * 4.f));
            else snprintf(bpm, sizeof bpm, "--- BPM");
            int len = clamp((int)module->params[MX::LEN_PARAM].getValue(), 1, 8);
            if (len > 1) snprintf(info, sizeof info, "SC %d/%d", module->curPat() + 1, len);
            else snprintf(info, sizeof info, "SC %d", module->curPat() + 1);
            snprintf(bank, sizeof bank, "%c %s", 'A' + module->bank(), module->bankName(module->bank(), module->selSlot));
        }
        const float yi = mm2px(2.5f * kMxRow);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 8.f);
        nvgFillColor(args.vg, oled::amber());
        nvgText(args.vg, 4.f, yi, bpm, NULL);
        nvgFontSize(args.vg, 6.5f);
        nvgFillColor(args.vg, run ? oled::green() : oled::dim());
        nvgText(args.vg, W * 0.36f, yi, run ? "RUN" : "STOP", NULL);
        nvgFillColor(args.vg, oled::dim());
        nvgText(args.vg, W * 0.52f, yi, info, NULL);
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, W - 4.f, yi, bank, NULL);
        nvgResetScissor(args.vg);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT || e.pos.y >= mm2px(2.f * kMxRow)) return;
        if (module) {
            if (e.pos.y < mm2px(kMxRow)) oled::midiMenu(&module->midiIn, "MIDI IN port");
            else oled::midiMenu(&module->midiOut, "MIDI OUT port");
        }
        e.consume(this);
    }
};

// IN / FX meters: each input's level and each slot's return, in the column order of the grid.
struct FxMeterDisplay : TransparentWidget {
    MX* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        std::shared_ptr<window::Font> font = uiFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        float x0 = mm2px(7.f), x1 = W - 4.f, cw = (x1 - x0) / 8.f, rh = (H - 4.f) / 2.f;
        for (int row = 0; row < 2; row++) {
            float y = 2.f + row * rh;
            nvgFontSize(args.vg, 6.f);
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, oled::dim());
            nvgText(args.vg, 4.f, y + rh * 0.5f, row ? "FX" : "IN", NULL);
            for (int i = 0; i < 8; i++) {
                float v = module ? (row ? module->fxMeter[i] : module->inMeter[i]) : 0.2f + 0.08f * i;
                bool patched = row || !module || module->inputs[MX::IN_INPUT + i].isConnected();
                float db = v > 1e-4f ? clamp((20.f * std::log10(v) + 48.f) / 54.f, 0.f, 1.f) : 0.f;   // -48 .. +6 dB
                float bx = x0 + i * cw + 1.f, bw = cw - 2.f, by = y + 1.f, bh = rh - 2.f;
                nvgBeginPath(args.vg);
                nvgRoundedRect(args.vg, bx, by, bw, bh, 0.8f);
                nvgFillColor(args.vg, oled::offFill());
                nvgFill(args.vg);
                if (db > 0.f) {
                    nvgBeginPath(args.vg);
                    nvgRoundedRect(args.vg, bx, by + bh * (1.f - db), bw, bh * db, 0.8f);
                    nvgFillColor(args.vg, v > 1.f ? oled::litFill() : row ? oled::green() : oled::amber(200));
                    nvgFill(args.vg);
                }
                if (!patched) continue;
                char b[4]; snprintf(b, sizeof b, "%d", i + 1);
                nvgFontSize(args.vg, 5.f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
                nvgFillColor(args.vg, oled::dim());
                nvgText(args.vg, bx + bw * 0.5f, by + 1.f, b, NULL);
            }
        }
    }
};

// The tape loop, as MATRIX-ECHO's looper (EchoLoopUi.hpp), mm from the display's top: the LOOP line, the
// waveform (scroll zooms, right-click shows all), then COUNT | LOOP BEFORE FILTER over REC PLAY ONCE UNDO
// CLEAR (tall cells) at the left, the record-length block (3 x 3) at the right beside both.
namespace fl {
const float kGap = 0.9f, kPad = 2.f, kCG = 1.f;
const float yWave = oled::kRow;
const float kTransH = 2.f * oled::kCellH + kGap;               // as tall as two rows: near square
const float kH = 33.f;
const float yTrans = kH - 0.7f - kTransH;
const float yGrid = yTrans - kGap - oled::kCellH;
const float kWaveH = yGrid - 0.5f - yWave;
const float kLenW = 30.f;
}
struct FxLoopDisplay : TransparentWidget {
    MX* module = NULL;
    echoloop::View view;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        const float yW = mm2px(fl::yWave), yWB = mm2px(fl::yWave + fl::kWaveH);
        nvgBeginPath(args.vg);
        oled::divider(args, 4.f, yW, W - 4.f, yW);
        oled::divider(args, 4.f, yWB, W - 4.f, yWB);
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = uiFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W, H);
        echoloop::draw(args, W, 0.f, yW, yWB, module ? &module->looper : NULL, module ? module->blink : 0.f, view);
        nvgResetScissor(args.vg);
    }
    bool onWave(Vec p) { return p.y >= mm2px(fl::yWave) && p.y < mm2px(fl::yWave + fl::kWaveH); }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_RIGHT || !onWave(e.pos)) return;
        view.reset();
        e.consume(this);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (e.scrollDelta.y == 0.f || !onWave(e.pos)) return;
        view.zoomAt(e.pos.x, box.size.x, e.scrollDelta.y > 0.f ? 1 : -1);
        e.consume(this);
    }
};

struct SynTHBloKMatrixFxWidget : ModuleWidget {
    oled::Cell* undoCell = NULL;
    bool randFxPrev = false;
    void onButton(const ButtonEvent& e) override {   // clicking the module hands it the JAM
        MX* m = getModule<MX>();
        if (m && e.action == GLFW_PRESS && m->jam.wasEnabled) jamown::claim(m->id);
        ModuleWidget::onButton(e);
    }
    void step() override {
        MX* m = getModule<MX>();
        if (m) {
            for (int s = 0; s < 8; s++) delete m->retired[s].exchange(NULL);   // effects the engine has swapped out
            if (undoCell) undoCell->label = m->looper.undone ? "REDO" : "UNDO";
            bool rf = m->params[MX::RANDFX_PARAM].getValue() > 0.5f;   // here, not in process(): it builds effects
            if (rf && !randFxPrev) m->randomizeSlots();
            randFxPrev = rf;
        }
        ModuleWidget::step();
    }
    SynTHBloKMatrixFxWidget(MX* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKMatrixFx.svg")));
        using namespace rack::componentlibrary;
        const float PW = 223.52f;   // 44 HP
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PW - 3.5f, 3.5f))));
        FxTitle* title = createWidget<FxTitle>(mm2px(Vec(8.5f, 0.f)));
        title->box.size = mm2px(Vec(60.f, 7.f));
        addChild(title);

        auto label = [&](float cx, float cy, const char* text) {
            JLabel* l = createWidget<JLabel>(mm2px(Vec(cx - 12.f, cy - 2.f)));
            l->box.size = mm2px(Vec(24.f, 4.f)); l->text = text;
            addChild(l);
        };
        auto cell = [&](float x, float y, float w, float h, int param, const char* lab) -> oled::Cell* {
            oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(x, y)), module, param);
            c->box.size = mm2px(Vec(w, h)); c->module = module; c->label = lab;
            addParam(c);
            return c;
        };

        // ---------------- left: the grid, laid out like the JAM (rows = inputs, columns = slots) ----------------
        auto colX = [](int c) { return 4.9f + 7.4f * c; };
        auto pageLed = [=](JackLed* l) { l->color = [=]() { return kPatOnColor[module ? module->curPat() : 0]; }; return l; };
        for (int p = 0; p < 8; p++) {
            FxPatBtn* pb = createWidget<FxPatBtn>(mm2px(Vec(colX(p), 10.5f)));
            pb->box.size = mm2px(Vec(6.6f, 6.6f)); pb->module = module; pb->p = p;
            addChild(pb);
        }
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                FxPad* p = createWidget<FxPad>(mm2px(Vec(colX(c), 19.5f + 7.4f * r)));
                p->box.size = mm2px(Vec(6.6f, 6.6f)); p->module = module; p->r = r; p->c = c;
                addChild(p);
            }
        addParam(JRandBtn::create(69.6f, 10.5f, module, MX::RAND_PARAM));   // after pattern 8, over the IN jacks
        for (int r = 0; r < 8; r++) {
            addInput(createInputCentered<SmallJack>(mm2px(Vec(69.6f, 22.8f + 7.4f * r)), module, MX::IN_INPUT + r));
            addChild(pageLed(JackLed::create(Vec(73.5f, 22.8f + 7.4f * r), module, MX::IN_INPUT + r, false)));
        }
        for (int i = 0; i < 8; i++) {
            FxSlotName* sn = createWidget<FxSlotName>(mm2px(Vec(colX(i) - 0.2f, 79.5f)));
            sn->box.size = mm2px(Vec(7.f, 4.2f)); sn->module = module; sn->s = i;
            addChild(sn);
            FxBankSq* q = createWidget<FxBankSq>(mm2px(Vec(colX(i), 85.5f)));
            q->box.size = mm2px(Vec(6.6f, 6.6f)); q->module = module; q->b = i;
            addChild(q);
            FxStrip* s = createWidget<FxStrip>(mm2px(Vec(colX(i) + 0.9f, 93.5f)));
            s->box.size = mm2px(Vec(4.8f, 30.f)); s->module = module; s->idx = i;
            addChild(s);
        }
        label(69.6f, 81.6f, "SCENES");
        JLoopArrow<MX>* up = createWidget<JLoopArrow<MX>>(mm2px(Vec(66.1f, 85.5f)));
        up->box.size = mm2px(Vec(7.f, 6.6f)); up->module = module; up->dir = 1;
        addChild(up);
        JLoopDisplay<MX>* ld = createWidget<JLoopDisplay<MX>>(mm2px(Vec(66.1f, 93.5f)));
        ld->box.size = mm2px(Vec(7.f, 22.5f)); ld->module = module;
        addChild(ld);
        JLoopArrow<MX>* dn = createWidget<JLoopArrow<MX>>(mm2px(Vec(66.1f, 117.5f)));
        dn->box.size = mm2px(Vec(7.f, 6.f)); dn->module = module; dn->dir = -1;
        addChild(dn);

        // ---------------- middle: SETUP, meters, tape loop ----------------
        const float MX0 = 77.5f, MW = 89.7f;
        FxSetupDisplay* sd = createWidget<FxSetupDisplay>(mm2px(Vec(MX0, 10.5f)));
        sd->box.size = mm2px(Vec(MW, 59.1f)); sd->module = module;
        addChild(sd);
        oled::Cell* rfx = cell(MX0 + MW - 2.f - FxSetupDisplay::kRandFxW, 10.5f + FxSetupDisplay::kDetY + 0.6f,
                               FxSetupDisplay::kRandFxW, oled::kRow - 1.2f, MX::RANDFX_PARAM, "RAND FX");   // right end of the detail line
        rfx->green = true; rfx->fontSize = 5.f;
        FxMeterDisplay* mt = createWidget<FxMeterDisplay>(mm2px(Vec(MX0, 71.6f)));
        mt->box.size = mm2px(Vec(MW, 17.4f)); mt->module = module;
        addChild(mt);
        const float LY = 91.f;
        FxLoopDisplay* lpd = createWidget<FxLoopDisplay>(mm2px(Vec(MX0, LY)));
        lpd->box.size = mm2px(Vec(MW, fl::kH)); lpd->module = module;
        addChild(lpd);
        {
            const float CH = oled::kCellH, inner = MW - 2.f * fl::kPad;
            const float leftW = inner - fl::kLenW - 1.5f, lenX = MX0 + fl::kPad + leftW + 1.5f;
            const float tw = (leftW - 4.f * fl::kCG) / 5.f, halfW = (leftW - fl::kCG) / 2.f;
            const float x0 = MX0 + fl::kPad, yG = LY + fl::yGrid, yT = LY + fl::yTrans;
            // COUNT-IN | LOOP BEFORE FILTER
            echoloop::CountCell* cc = createParam<echoloop::CountCell>(mm2px(Vec(x0, yG)), module, MX::COUNTIN_PARAM);
            cc->box.size = mm2px(Vec(halfW, CH)); cc->module = module;
            addParam(cc);
            cell(x0 + halfW + fl::kCG, yG, halfW, CH, MX::LOOPPRE_PARAM, "LOOP BEFORE FILTER")->latch = true;
            // REC | PLAY | ONCE | UNDO | CLEAR: tall cells, REC red and PLAY green (dim until engaged)
            struct LC { const char* label; int param; };
            const LC lc[5] = {{"REC", MX::REC_PARAM}, {"PLAY", MX::PLAY_PARAM}, {"ONCE", MX::ONCE_PARAM},
                              {"UNDO", MX::UNDO_PARAM}, {"CLEAR", MX::LCLEAR_PARAM}};
            for (int i = 0; i < 5; i++) {
                float x = x0 + i * (tw + fl::kCG);
                oled::Cell* c;
                if (i < 2) {
                    echoloop::TintCell* t = createParam<echoloop::TintCell>(mm2px(Vec(x, yT)), module, lc[i].param);
                    t->module = module; t->label = lc[i].label;
                    t->tint = i == 0 ? nvgRGB(0xff, 0x32, 0x28) : oled::green();
                    addParam(t);
                    c = t;
                } else c = cell(x, yT, tw, CH, lc[i].param, lc[i].label);
                c->box.size = mm2px(Vec(tw, fl::kTransH));
                c->fontSize = 7.f;
                if (module) {
                    if (i == 0) c->flash = &module->recFlash;
                    if (i == 1) c->flash = &module->playFlash;
                    if (i == 2) { c->flash = &module->onceFlash; c->green = true; }
                }
                if (i == 3) undoCell = c;
            }
            // record length, 3 x 3
            const float lw = (fl::kLenW - 2.f * fl::kCG) / 3.f;
            for (int i = 0; i < 9; i++) {
                oled::Cell* c = cell(lenX + (i % 3) * (lw + fl::kCG), yG + (i / 3) * (CH + fl::kGap), lw, CH, MX::LOOPLEN_PARAM, echoloop::kLenName[i]);
                c->setTo = echoloop::kLenBeats[i]; c->fontSize = 5.5f;
            }
        }

        // ---------------- right: master OLED, knobs, MASTER, jacks ----------------
        const float RX = 172.72f, RW = 45.6f, RC = RX + RW * 0.5f;
        FxMasterDisplay* md = createWidget<FxMasterDisplay>(mm2px(Vec(RX, 10.5f)));
        md->box.size = mm2px(Vec(RW, kMxH)); md->module = module;
        addChild(md);
        oled::Cell* mc = cell(RX + RW - 10.5f, 11.2f, 9.f, 2.f * kMxRow - 1.4f, MX::MIDI_ON_PARAM, "MIDI");
        mc->latch = true; mc->green = true;
        struct CellDef { const char* label; int param; bool latch, green; int jack; };
        const CellDef cells[5] = {
            {"RUN", MX::RUN_PARAM, true, true, MX::RUN_INPUT},
            {"RESET", MX::RESET_PARAM, false, false, MX::RESET_INPUT},
            {"CLEAR", MX::CLEAR_PARAM, false, false, -1},
            {"LEARN", MX::LEARN_PARAM, true, false, -1},
            {"DUP", MX::DUP_PARAM, false, false, -1}};
        const float CX = RX + 2.f, CW = RW - 4.f, CG = 1.f;
        for (int i = 0; i < 5; i++) {
            int row = i < 3 ? 0 : 1, n = row ? 2 : 3, k = row ? i - 3 : i;
            float w = (CW - CG * (n - 1)) / n;
            oled::Cell* c = cell(CX + k * (w + CG), 10.5f + kMxBtn + row * (oled::kCellH + 0.9f), w, oled::kCellH, cells[i].param, cells[i].label);
            c->latch = cells[i].latch; c->green = cells[i].green; c->jack = cells[i].jack;
            if (cells[i].param == MX::RESET_PARAM && module) c->flash = &module->resetFlash;
        }
        auto col4 = [&](int i) { return RX + RW * (i + 0.5f) / 4.f; };
        // snap = a stepped knob (BPM, ROOT, SCALE): no value ring, snapping
        auto smallKnob = [&](float x, float y, int param, const char* name, bool snap) {
            addChild(KnobTicks::create(Vec(x, y)));
            if (!snap) {
                JKnobRing<MX>* fr = createWidget<JKnobRing<MX>>(mm2px(Vec(x - 5.5f, y - 5.5f)));
                fr->box.size = mm2px(Vec(11.f, 11.f));
                fr->module = module; fr->param = param;
                addChild(fr);
            }
            ParamWidget* kn = createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y)), module, param);
            if (snap) if (Knob* k = dynamic_cast<Knob*>(kn)) k->snap = true;
            addParam(kn);
            label(x, y + 7.f, name);
        };
        smallKnob(col4(0), 45.f, MX::DRY_PARAM, "DRY", false);
        smallKnob(col4(1), 45.f, MX::WET_PARAM, "WET", false);
        smallKnob(col4(2), 45.f, MX::LOOP_LVL_PARAM, "LOOP", false);
        smallKnob(col4(3), 45.f, MX::BPM_PARAM, "BPM", true);
        smallKnob(col4(0), 60.f, MX::FILT_PARAM, "FILTER", false);
        smallKnob(col4(1), 60.f, MX::RES_PARAM, "RES", false);
        smallKnob(col4(2), 60.f, MX::ROOT_PARAM, "ROOT", true);
        smallKnob(col4(3), 60.f, MX::SCALE_PARAM, "SCALE", true);
        const float KY = 80.f;
        addChild(KnobTicks::create(Vec(RC, KY), 19.f / 11.f));
        JKnobRing<MX>* ring = createWidgetCentered<JKnobRing<MX>>(mm2px(Vec(RC, KY)));
        ring->box.size = mm2px(Vec(19.f, 19.f));
        ring->module = module;
        addChild(ring);
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(RC, KY)), module, MX::KNOB_PARAM));
        label(RC, KY + 12.f, "MASTER");
        struct JackDef { const char* name; int id; bool out; };
        const JackDef jk[2][4] = {
            {{"CLK", MX::CLOCK_INPUT, false}, {"RST", MX::RESET_INPUT, false}, {"RUN", MX::RUN_INPUT, false}, {"SC", MX::SC_INPUT, false}},
            {{NULL, -1, false}, {NULL, -1, false}, {"OUT L", MX::OUT_L_OUTPUT, true}, {"OUT R", MX::OUT_R_OUTPUT, true}}};
        for (int r = 0; r < 2; r++)
            for (int i = 0; i < 4; i++) {
                const JackDef& d = jk[r][i];
                if (!d.name) continue;
                float x = col4(i), y = 104.f + 13.f * r;
                label(x, y - 5.5f, d.name);
                if (d.out) addOutput(createOutputCentered<SmallJack>(mm2px(Vec(x, y)), module, d.id));
                else addInput(createInputCentered<SmallJack>(mm2px(Vec(x, y)), module, d.id));
            }
    }
    void appendContextMenu(Menu* menu) override {
        MX* m = getModule<MX>();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        auto indexItem = [&](const std::string& text, int paramId, std::vector<std::string> labels) {
            menu->addChild(createIndexSubmenuItem(text, labels,
                [=]() { return (size_t)(m->params[paramId].getValue() - 1.f); },
                [=](size_t i) { m->params[paramId].setValue((float)i + 1.f); }));
        };
        indexItem("Scene loop", MX::LEN_PARAM, {"Hold", "2 scenes", "3 scenes", "4 scenes", "5 scenes", "6 scenes", "7 scenes", "8 scenes"});
        indexItem("JAM matrix page", MX::PAGE_PARAM, {"1", "2", "3", "4", "5", "6"});
        indexItem("Strip bank", MX::BANK_PARAM, {"A level", "B", "C", "D", "E", "F tone", "G pan", "H width"});
        menu->addChild(createMenuItem("Clear every scene", "", [=]() { for (int p = 0; p < 8; p++) m->clearScene(p); }));
        menu->addChild(createMenuItem("Unlink every slot", "", [=]() { for (int s = 0; s < 8; s++) m->link[s] = false; }));
        menu->addChild(createMenuItem("Clear mutes and solos", "", [=]() { for (int r = 0; r < 8; r++) m->mute[r] = m->solo[r] = false; }));
    }
};

} // namespace

Model* modelSynTHBloKMatrixFx = createModel<SynTHBloKMatrixFx, SynTHBloKMatrixFxWidget>("SynTHBloKMatrixFx");
