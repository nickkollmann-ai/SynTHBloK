// ============================================================================
// SynTHBloK MATRIX-SONG  (the conductor: sections A-H across every MATRIX module in the patch)
//  The grid: a row per MATRIX module (found by itself through SongLink.hpp, no cables), a column per song
//  section. A pad holds what that module does in that section: its pattern (or keeps it), a mute, a loop
//  action (PLAY / STOP / REC / ONCE) for the ECHO / FX / LOOP loopers, and its level.
//  A section header launches that section on the next bar (or beat).
//  The strips (as MATRIX-DRUM's): a level, a MUTE and an output meter per row's module. Each section keeps
//  its own levels and mutes; a launch moves the strips to them, and moving a strip edits the playing section.
//  The song cycle: with SECTIONS above 1, RUN plays A, B, .. up to that many, each for BARS bars, then from A
//  again (a launch jumps there, and the cycle goes on from it). SECTIONS 1: a section plays until launched.
//  Master clock: CLOCK (16ths), RUN, RESET, BAR and SECT (0-7 V) out; CLOCK in (16ths) or the BPM knob.
//  JAM ("JAM Template 1 Live", as the other MATRIX modules; click SONG to hand it the JAM): the pads are
//  the grid (a press selects the cell; MUTE + pad mutes it, CLEAR + pad clears it), scenes launch A-H,
//  A-H mute the rows, the touch strips are the levels. PLAY = RUN, REC = the selected cell's looper REC,
//  TEMPO = RESET, GRID = bar / beat, PREV / NEXT launch the section before / after, the cross pad moves
//  the selected cell, DUPLICATE copies the section to the next, the knob is the BPM.
// ============================================================================
#include "plugin.hpp"
#include "SongLink.hpp"
#include "OledUi.hpp"
#include "EchoLoopUi.hpp"
#include "Matrix8_ui.hpp"
#include "JackLed.hpp"
#include "KnobTicks.hpp"
#include "JamOwner.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

static const int kRows = 8, kSecs = 8;
static const char* const kSecName[kSecs] = {"A", "B", "C", "D", "E", "F", "G", "H"};
// rows (and their strips) in the fire colours the MATRIX modules all use, as the section columns are
static const NVGcolor* const kRowCol = kPatOnColor;
static const char* const kLoopGlyph[songlink::L_COUNT] = {"", "PLAY", "STOP", "REC", "ONCE"};

struct SynTHBloKMatrixSong : Module {
    // REC_ / PLAY_ / LEN_PARAM: unused (the take is gone), kept so saved patches keep their param ids
    enum ParamIds { RUN_PARAM, RESET_PARAM, REC_PARAM, PLAY_PARAM, QUANT_PARAM, BPM_PARAM, MIDI_ON_PARAM, LEN_PARAM, SECS_PARAM, BARS_PARAM, NUM_PARAMS };
    // RUN_ / RESET_INPUT: no jacks on the panel (the RUN / RESET outputs and buttons cover them), kept for ids
    enum InputIds { CLOCK_INPUT, RUN_INPUT, RESET_INPUT, NUM_INPUTS };
    enum OutputIds { CLOCK_OUTPUT, RUN_OUTPUT, RESET_OUTPUT, BAR_OUTPUT, SECT_OUTPUT, NUM_OUTPUTS };

    struct Cell { int pat = -1; bool mute = false; int loop = songlink::L_NONE; float level = 1.f; };
    Cell cells[kSecs][kRows];
    int64_t rowId[kRows];
    // the strips now (a launch loads them from the section; a strip move edits the playing section)
    float lev[kRows];
    bool mut[kRows];

    // the song now
    int curSec = 0, barInSec = 0;
    std::atomic<int> queued{-1};      // a launch waiting for its bar (or beat)
    std::atomic<int> applyNow{-1};    // stopped: a launch applies at once
    bool wasRunning = false;
    int selSec = 0, selRow = 0;       // the pad the editor shows (UI)
    int pushCount = 0;

    // clock
    int tick = -1;
    bool running = true;
    float sinceClock = 10.f, clockPeriod = 0.f, internalTimer = 0.f;
    dsp::SchmittTrigger clkTrig, runTrig, rstTrig, rstBtnTrig;
    dsp::PulseGenerator clockPulse, resetPulse, barPulse;
    float resetFlash = 0.f, blink = 0.f;

    // Maschine JAM, as on MATRIX-DRUM: the last clicked module owns it (JamOwner.hpp)
    midi::InputQueue midiIn;
    midi::Output midiOut;
    jamown::Link jam;
    bool slowSend = false, muteHeld = false, clearHeld = false, jamReset = false;
    int jamPage = 1, stripPage = 0, lastStripPage = -1, lastPage = -1, knobCC = kKnobFirstCC, ledScan = 0;
    int lastLed[kNumLed];
    float tokens = 4.f, resync = 0.f;

    SynTHBloKMatrixSong() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
        configSwitch(RUN_PARAM, 0.f, 1.f, 0.f, "Run (starts the song from the top; a trigger at RUN toggles it)", {"Stopped", "Running"});
        configButton(RESET_PARAM, "Reset: the song from the top (a trigger at RESET does too)");
        configSwitch(QUANT_PARAM, 0.f, 1.f, 0.f, "Launches land on the next", {"Bar", "Beat"});
        configParam(BPM_PARAM, 40.f, 240.f, 120.f, "Tempo (when CLOCK is unpatched)", " BPM");
        paramQuantities[BPM_PARAM]->snapEnabled = true;
        configSwitch(MIDI_ON_PARAM, 0.f, 1.f, 1.f, "JAM MIDI (off = ignore the JAM, its lights dark)", {"Off", "On"});
        configSwitch(SECS_PARAM, 0.f, 7.f, 0.f, "Song cycle: RUN steps through this many sections (1: stays on one)", {"1 (no cycle)", "A-B", "A-C", "A-D", "A-E", "A-F", "A-G", "A-H"});
        configSwitch(BARS_PARAM, 0.f, 4.f, 2.f, "Bars each section plays in the song cycle", {"1 bar", "2 bars", "4 bars", "8 bars", "16 bars"});
        for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
        for (int id : {RUN_PARAM, QUANT_PARAM}) paramQuantities[id]->randomizeEnabled = false;
        configInput(CLOCK_INPUT, "Clock (16th notes; the BPM knob when unpatched)");
        configInput(RUN_INPUT, "Run (trigger: toggles RUN)");
        configInput(RESET_INPUT, "Reset (trigger)");
        configOutput(CLOCK_OUTPUT, "Clock (16th notes, while running)");
        configOutput(RUN_OUTPUT, "Run (gate)");
        configOutput(RESET_OUTPUT, "Reset (trigger as the song starts or resets)");
        configOutput(BAR_OUTPUT, "Bar (trigger on each downbeat)");
        configOutput(SECT_OUTPUT, "Section (A = 0 V .. H = 7 V)");
        for (int r = 0; r < kRows; r++) rowId[r] = -1;
        defaults();
    }

    void defaults() {
        for (int s = 0; s < kSecs; s++) for (int r = 0; r < kRows; r++) cells[s][r] = Cell();
        for (int s = 0; s < kSecs; s++) for (int r = 0; r < kRows; r++) cells[s][r].pat = s;   // A..H: patterns 1..8
        curSec = 0; selSec = selRow = 0;
        loadLive(0);
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
    // the song cycle: sections in it (1 = none) and bars per section
    int cycleSecs() { return clamp((int)params[SECS_PARAM].getValue(), 0, 7) + 1; }
    int cycleBars() { static const int b[5] = {1, 2, 4, 8, 16}; return b[clamp((int)params[BARS_PARAM].getValue(), 0, 4)]; }
    void loadLive(int s) { for (int r = 0; r < kRows; r++) { lev[r] = cells[s][r].level; mut[r] = cells[s][r].mute; } }

    // UI: a strip / MUTE moves the live state, and writes into the playing section
    void setLevel(int r, float v) { lev[r] = clamp(v, 0.f, 1.f); cells[curSec][r].level = lev[r]; }
    void toggleMute(int r) { mut[r] = !mut[r]; cells[curSec][r].mute = mut[r]; }
    // UI: the editor changed section s; if it is the one playing, the strips follow and the modules switch
    // to its patterns now (its loop actions wait for its next launch)
    void edited(int s) {
        if (s != curSec) return;
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
    // Rack closing / the module deleted: lights off, paced so the JAM takes every message
    ~SynTHBloKMatrixSong() {
        for (int r = 0; r < kRows; r++) release(r);
        if (jam.active) { slowSend = true; clearLeds(); }
        jamown::release(id);
    }

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

    // ---- JAM ----
    int padNote(int r, int c) { return std::min(127, 12 * (jamPage - 1) + 56 + c - 8 * r); }
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

    void toggle(Param& p) { p.setValue(p.getValue() > 0.5f ? 0.f : 1.f); }
    void jamPad(int r, int s) {
        selSec = s; selRow = r;
        if (muteHeld) { cells[s][r].mute = !cells[s][r].mute; edited(s); }
        else if (clearHeld) { cells[s][r] = Cell(); edited(s); }
    }
    void jamButton(int cc, int b) {
        bool down = b > 0;
        if (cc == CC_MUTE) { muteHeld = down; return; }
        if (cc == CC_CLEAR) { clearHeld = down; return; }
        if (!down) return;
        switch (cc) {
            case CC_PLAY: toggle(params[RUN_PARAM]); break;
            case CC_REC: { Cell& c = cells[selSec][selRow]; c.loop = c.loop == songlink::L_REC ? songlink::L_NONE : songlink::L_REC; } break;
            case CC_TEMPO: jamReset = true; break;
            case CC_GRID: toggle(params[QUANT_PARAM]); break;
            case CC_PREV: launch((curSec + kSecs - 1) % kSecs); break;
            case CC_NEXT: launch((curSec + 1) % kSecs); break;
            case CC_UP: selRow = (selRow + kRows - 1) % kRows; break;
            case CC_DOWN: selRow = (selRow + 1) % kRows; break;
            case CC_LEFT: selSec = (selSec + kSecs - 1) % kSecs; break;
            case CC_RIGHT: selSec = (selSec + 1) % kSecs; break;
            case CC_DUPLICATE: {
                int to = (selSec + 1) % kSecs;
                for (int r = 0; r < kRows; r++) cells[to][r] = cells[selSec][r];
                selSec = to;
                edited(curSec);
            } break;
        }
    }
    void processJamIn(const ProcessArgs& args) {
        midi::Message msg;
        while (midiIn.tryPop(&msg, args.frame)) {
            if (msg.bytes.size() < 3) continue;
            int st = msg.bytes[0] & 0xf0, a = msg.bytes[1], b = msg.bytes[2];
            if (st == 0x90 && b > 0) {
                bool pad = false;
                for (int r = 0; r < kRows && !pad; r++) for (int s = 0; s < kSecs; s++)
                    if (padNote(r, s) == a) { jamPad(r, s); pad = true; break; }
                if (!pad) for (auto& n : kBtnNote) if (n[0] == a) jamButton(n[1], b);
            } else if (st == 0xB0) {
                bool isBtn = false;
                for (int i = 0; i < kNumBtn; i++) if (kBtnCC[i] == a) isBtn = true;
                if (isBtn) jamButton(a, b);
                else if (a >= kKnobFirstCC && a <= kKnobLastCC) {
                    knobCC = a;
                    params[BPM_PARAM].setValue(std::round(40.f + 200.f * b / 127.f));
                    lastLed[kKnobLed] = b;
                }
                else if (a == kKnobPushCC && b > 0) paramQuantities[BPM_PARAM]->reset();
                else if (a >= 0 && a < 8 && b > 0) launch(a);
                else if (a >= 8 && a < 16 && b > 0) { if (rowId[a - 8] >= 0) toggleMute(a - 8); }
                else if (a >= 48 && a < 112) {
                    stripPage = (a - 48) / 8;
                    if (rowId[(a - 48) % 8] >= 0) setLevel((a - 48) % 8, b / 127.f);
                }
            }
        }
    }
    // pads: a row's colour, bright in the playing section (dim while muted), dim elsewhere, dark when empty;
    // the selected cell white. Scenes: the playing section bright, a queued one blinking. A-H: lit = muted.
    int desiredLed(int idx) {
        bool blinkOn = blink < 0.25f;
        if (idx == kKnobLed) return (int)std::round((params[BPM_PARAM].getValue() - 40.f) / 200.f * 127.f);
        if (idx >= 88) {
            switch (kBtnCC[idx - 88]) {
                case CC_PLAY: return running ? 127 : 0;
                case CC_REC: return cells[selSec][selRow].loop == songlink::L_REC ? 127 : 0;
                case CC_SONG: return 0;
                case CC_TEMPO: return resetFlash > 0.f ? 127 : 0;
                case CC_GRID: return params[QUANT_PARAM].getValue() > 0.5f ? 127 : 0;
                case CC_MUTE: return muteHeld ? 127 : 0;
                case CC_CLEAR: return clearHeld ? 127 : 0;
                case CC_SOLO: return 0;
                default: return 127;
            }
        }
        if (idx < 64) {
            int r = idx / 8, s = idx % 8;
            if (rowId[r] < 0) return 0;
            if (s == selSec && r == selRow) return kJamWhite;
            const Cell& c = cells[s][r];
            if (c.pat < 0 && !c.mute && c.loop == songlink::L_NONE) return 0;
            if (s == curSec) return c.mute || mut[r] ? kJamDim[r] : kJamBright[r];
            return kJamDim[r];
        }
        if (idx < 72) {
            int s = idx - 64;
            if (queued.load() == s) return blinkOn ? kJamWhite : kJamDim[s];
            return s == curSec ? kJamBright[s] : kJamDim[s];
        }
        if (idx < 80) return rowId[idx - 72] >= 0 ? (int)std::round(lev[idx - 72] * 127.f) : 0;
        int r = idx - 80;
        if (rowId[r] < 0) return 0;
        return mut[r] ? kJamBright[r] : kJamDim[r];
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
    void processJamLeds(const ProcessArgs& args) {
        resync += args.sampleTime;
        if (jamPage != lastPage || stripPage != lastStripPage || resync > 3.f) {
            for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
            lastPage = jamPage; lastStripPage = stripPage; resync = 0.f;
        }
        tokens = std::min(16.f, tokens + args.sampleTime * 2000.f);
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

    void process(const ProcessArgs& args) override {
        const float dt = args.sampleTime;
        // the JAM follows the last clicked module: losing it goes dark, gaining it resends all
        int own = jam.update(id, params[MIDI_ON_PARAM].getValue() > 0.5f);
        if (own > 0) for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
        else if (own < 0) clearLeds();
        if (jam.active) processJamIn(args);
        else { midi::Message m; while (midiIn.tryPop(&m, args.frame)) {} }
        if (runTrig.process(inputs[RUN_INPUT].getVoltage()))
            params[RUN_PARAM].setValue(params[RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f);
        running = params[RUN_PARAM].getValue() > 0.5f;
        bool rst = rstTrig.process(inputs[RESET_INPUT].getVoltage()) | rstBtnTrig.process(params[RESET_PARAM].getValue()) | jamReset;
        jamReset = false;
        if (running && !wasRunning) rst = true;   // RUN starts the song from the top
        wasRunning = running;

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
            // the song cycle: its bars played, the next section launches on this bar (after the last, A)
            int cs = cycleSecs();
            if (bar && !start && cs > 1 && queued.load() < 0 && barInSec >= cycleBars()) queued = curSec + 1 >= cs ? 0 : curSec + 1;

            int to = start ? curSec : -1;
            int q = queued.load();
            if (q >= 0 && (bar || (params[QUANT_PARAM].getValue() > 0.5f && beat))) { queued = -1; to = q; }
            if (to >= 0) apply(to);
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
        if (jam.active) processJamLeds(args);
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
        json_object_set_new(root, "in", midiIn.toJson());
        json_object_set_new(root, "out", midiOut.toJson());
        json_object_set_new(root, "jamPage", json_integer(jamPage));
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
        if (json_t* j = json_object_get(root, "in")) midiIn.fromJson(j);
        if (json_t* j = json_object_get(root, "out")) midiOut.fromJson(j);
        if (json_t* j = json_object_get(root, "jamPage")) jamPage = clamp((int)json_integer_value(j), 1, 6);
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
        nvgText(args.vg, 0.f, mm2px(6.4f), "MTX COMPOSER", NULL);
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
        static const char* const demo[kRows] = {"DRUM", "DUO", "MTX-8", "FORGE", "FX", "ECHO", "LOOP", "-"};
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

// The CELL editor's info line: the selected pad (module and section), and its level, mute and loop.
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
        snprintf(val, sizeof val, "LEVEL %d%%%s%s%s", (int)std::round(c.level * 100.f), c.mute ? "  MUTED" : "", c.loop ? "  LOOP " : "", c.loop ? songlink::kLoopName[c.loop] : "");
        float y = R * 0.5f;
        nvgFontSize(args.vg, 6.f);
        nvgFillColor(args.vg, oled::dim());
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, 4.f, y, "CELL", NULL);
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, kRowCol[r]);
        std::string who = string::f("%s · %s", name, kSecName[s]);
        nvgText(args.vg, mm2px(8.f), y, who.c_str(), NULL);
        nvgFontSize(args.vg, 6.f);
        nvgFillColor(args.vg, oled::amber());
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, W - 4.f, y, val, NULL);
    }
};

// The SONG screen: state | bar | tempo; now playing, and what waits.
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
            if (module->cycleSecs() > 1) snprintf(bar, sizeof bar, "BAR %d/%d", module->barInSec + 1, module->cycleBars());
            else snprintf(bar, sizeof bar, "BAR %d", module->barInSec + 1);
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
        // NOW: the section, and the launch waiting for its bar
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
        // SECTIONS / BARS, before their choices on the cycle rows
        nvgFontSize(args.vg, 5.5f);
        nvgFillColor(args.vg, oled::dim());
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        for (int i = 0; i < 2; i++)
            nvgText(args.vg, mm2px(2.f), mm2px(2.f * oled::kRow + 0.4f + (i + 1) * (oled::kCellH + 0.9f) + oled::kCellH * 0.5f), i ? "BARS" : "SECTIONS", NULL);
        // the bar's progress while running
        if (run && module) {
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 4.f, 2.f * R - 1.2f, (W - 8.f) * (module->tick < 0 ? 0.f : (module->tick + 1) / 16.f), 0.8f);
            nvgFillColor(args.vg, oled::green());
            nvgFill(args.vg);
        }
    }
};

// A pattern in the CELL editor: a square in that pattern's fire colour (as the MATRIX modules' pattern
// buttons), filled when the cell plays it.
struct SongPatSq : OpaqueWidget {
    MS* module = NULL;
    int p = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        bool sel = module ? module->cells[module->selSec][module->selRow].pat == p : p == 0;
        drawColorButton(args, box.size, p, sel, (char)('1' + p), 9.f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS) return;
        if (module) { module->cells[module->selSec][module->selRow].pat = p; module->edited(module->selSec); }
        e.consume(this);
    }
};

// KEEP beside the patterns: the cell leaves its module on whatever pattern it plays.
struct SongKeepBtn : OpaqueWidget {
    MS* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        bool on = module ? module->cells[module->selSec][module->selRow].pat < 0 : false;
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.4f);
        nvgFillColor(args.vg, on ? oled::litFill() : oled::offFill());
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, on ? oled::litFill() : oled::outline(110));
        nvgStrokeWidth(args.vg, on ? 1.2f : 0.8f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? oled::ink() : oled::amber());
        nvgFontSize(args.vg, 9.f);
        nvgText(args.vg, W * 0.5f, H * 0.4f, "KEEP", NULL);
        nvgFontSize(args.vg, 5.5f);
        nvgFillColor(args.vg, on ? oled::ink() : oled::dim());
        nvgText(args.vg, W * 0.5f, H * 0.68f, "NO CHANGE", NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS) return;
        if (module) { module->cells[module->selSec][module->selRow].pat = -1; module->edited(module->selSec); }
        e.consume(this);
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

// The JAM corner (bottom left), laid out as MATRIX-LOOP's: the square green MIDI cell (on / off, laid over it
// by the widget) at the left; right of it MIDI IN over MIDI OUT, each a dim title over its device (click =
// the port menu). In mm:
namespace sm {
const float kPort = 5.5f;                   // a port: title + device
const float kH = 2.f * kPort + 1.f;
const float kSq = oled::kCellH + 2.f;       // the MIDI cell's side, centred top to bottom
const float xCell = 1.5f, yCell = (kH - kSq) * 0.5f;
const float xText = xCell + kSq + 1.5f;     // the ports
}
struct SongJamDisplay : TransparentWidget {
    MS* module = NULL;
    void port(const DrawArgs& args, midi::Port* p, const char* title, float y0) {
        std::string dev = "(no device)";
        if (p) { if (p->getDeviceId() >= 0) dev = p->getDeviceName(p->getDeviceId()); }
        else dev = "Maschine Jam - 1";
        const float x = mm2px(sm::xText);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 5.f);
        nvgFillColor(args.vg, oled::dim());
        nvgText(args.vg, x, mm2px(y0 + 1.9f), title, NULL);
        nvgFontSize(args.vg, 5.5f);
        nvgFillColor(args.vg, oled::amber());
        nvgText(args.vg, x, mm2px(y0 + 4.2f), dev.c_str(), NULL);
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        nvgBeginPath(args.vg);
        oled::divider(args, mm2px(sm::xText), mm2px(0.5f + sm::kPort), W - 3.f, mm2px(0.5f + sm::kPort));
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W - 2.f, H);   // a long device name stops at the edge
        port(args, module ? &module->midiIn : NULL, "MIDI IN", 0.5f);
        port(args, module ? &module->midiOut : NULL, "MIDI OUT", 0.5f + sm::kPort);
        nvgResetScissor(args.vg);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS || !module || e.pos.x < mm2px(sm::xText)) return;
        if (e.pos.y < mm2px(0.5f + sm::kPort)) oled::midiMenu(&module->midiIn, "MIDI IN port (the JAM)");
        else oled::midiMenu(&module->midiOut, "MIDI OUT port (the JAM's lights)");
        e.consume(this);
    }
};
struct SynTHBloKMatrixSongWidget : ModuleWidget {
    Found found;
    void onButton(const ButtonEvent& e) override {   // clicking the module hands it the JAM
        MS* m = getModule<MS>();
        if (m && e.action == GLFW_PRESS && m->params[MS::MIDI_ON_PARAM].getValue() > 0.5f) jamown::claim(m->id);
        ModuleWidget::onButton(e);
    }    void step() override {
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
        const float PW = 147.32f;   // 29 HP
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
        const float GX = 25.2f, GP = 7.4f, GS = 6.6f, GY = 19.5f;
        for (int s = 0; s < kSecs; s++) {
            SecHead* h = createWidget<SecHead>(mm2px(Vec(GX + GP * s, 10.5f)));
            h->box.size = mm2px(Vec(GS, 6.6f)); h->module = module; h->s = s;
            addChild(h);
        }
        for (int r = 0; r < kRows; r++) {
            const float LX = 3.6f, LW = GX - 1.2f - LX;
            RowLabel* l = createWidget<RowLabel>(mm2px(Vec(LX, GY + GP * r)));
            l->box.size = mm2px(Vec(LW, GS)); l->module = module; l->found = &found; l->r = r;
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

        // ---------------- bottom left: the JAM (the square MIDI on / off, then MIDI IN / OUT) ----------------
        {
            const float JX = 3.6f, JW = GX - 1.2f - JX, JY = LY + LH - sm::kH;   // as wide as the row labels
            SongJamDisplay* jd = createWidget<SongJamDisplay>(mm2px(Vec(JX, JY)));
            jd->box.size = mm2px(Vec(JW, sm::kH)); jd->module = module;
            addChild(jd);
            oled::Cell* jc = createParam<oled::Cell>(mm2px(Vec(JX + sm::xCell, JY + sm::yCell)), module, MS::MIDI_ON_PARAM);
            jc->box.size = mm2px(Vec(sm::kSq, sm::kSq)); jc->module = module; jc->label = "MIDI"; jc->latch = true; jc->green = true;
            addParam(jc);
        }

        // ---------------- right: the SONG screen and transport ----------------
        const float SX = PW - 60.96f, SW = PW - SX - 4.2f, SY = 10.4f;
        const float x0 = SX + 2.f, inner = SW - 4.f;
        auto pcell = [&](float x, float y, float w, float h, int param, const char* lab) {
            oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(x, y)), module, param);
            c->box.size = mm2px(Vec(w, h)); c->module = module; c->label = lab;
            addParam(c);
            return c;
        };
        auto tint = [&](float x, float y, float w, int param, const char* lab, NVGcolor col, int jack) {
            echoloop::TintCell* c = createParam<echoloop::TintCell>(mm2px(Vec(x, y)), module, param);
            c->box.size = mm2px(Vec(w, CH)); c->module = module; c->label = lab; c->latch = true;
            c->tint = col; c->jack = jack;
            addParam(c);
        };
        auto btn = [&](float x, float y, float w, float h, const char* lab) {
            SongBtn* b = createWidget<SongBtn>(mm2px(Vec(x, y)));
            b->box.size = mm2px(Vec(w, h)); b->label = lab; b->fontSize = 5.2f;
            addChild(b);
            return b;
        };
        // SONG: state | bar | tempo, NOW; then RUN | RESET | BAR | BEAT
        const float SH = 2.f * oled::kRow + 0.4f + 3.f * CH + 2.f * GAP + 1.2f, ctY = SY + 2.f * oled::kRow + 0.4f;
        SongDisplay* sd = createWidget<SongDisplay>(mm2px(Vec(SX, SY)));
        sd->box.size = mm2px(Vec(SW, SH)); sd->module = module;
        addChild(sd);
        {
            const float w = (inner - 3.f * CG) / 4.f;
            tint(x0, ctY, w, MS::RUN_PARAM, "RUN", oled::green(), -1);
            oled::Cell* rs = pcell(x0 + (w + CG), ctY, w, CH, MS::RESET_PARAM, "RESET");
            if (module) rs->flash = &module->resetFlash;
            pcell(x0 + 2.f * (w + CG), ctY, w, CH, MS::QUANT_PARAM, "BAR")->setTo = 0;
            pcell(x0 + 3.f * (w + CG), ctY, w, CH, MS::QUANT_PARAM, "BEAT")->setTo = 1;
            // the song cycle: SECTIONS 1-8, then BARS 1 2 4 8 16
            const float lx = x0 + 11.f;
            static const char* const sn[8] = {"1", "2", "3", "4", "5", "6", "7", "8"};
            static const char* const bn[5] = {"1", "2", "4", "8", "16"};
            const float sw = (x0 + inner - lx - 7.f * CG) / 8.f, bw = (x0 + inner - lx - 4.f * CG) / 5.f;
            for (int i = 0; i < 8; i++) pcell(lx + i * (sw + CG), ctY + CH + GAP, sw, CH, MS::SECS_PARAM, sn[i])->setTo = i;
            for (int i = 0; i < 5; i++) pcell(lx + i * (bw + CG), ctY + 2.f * (CH + GAP), bw, CH, MS::BARS_PARAM, bn[i])->setTo = i;
        }

        auto selCell = [=]() -> MS::Cell* { return module ? &module->cells[module->selSec][module->selRow] : NULL; };

        // ---------------- CELL: the selected pad (pattern, KEEP, loop action, edits) ----------------
        const float EY = SY + SH + 2.f, PS = 6.6f, PP = 7.4f, PH = PS + 0.8f + PS;
        const float EH = oled::kRow + 0.6f + PH + GAP + CH + GAP + CH + 0.8f;
        EdDisplay* ed = createWidget<EdDisplay>(mm2px(Vec(SX, EY)));
        ed->box.size = mm2px(Vec(SW, EH)); ed->module = module; ed->found = &found;
        addChild(ed);
        {
            float y = EY + oled::kRow + 0.6f;
            // KEEP, then patterns 1-8 as two rows of four squares
            const float px = x0 + inner - (4.f * PP - 0.8f);
            SongKeepBtn* kb = createWidget<SongKeepBtn>(mm2px(Vec(x0, y)));
            kb->box.size = mm2px(Vec(px - 1.2f - x0, PH)); kb->module = module;
            addChild(kb);
            for (int p = 0; p < 8; p++) {
                SongPatSq* q = createWidget<SongPatSq>(mm2px(Vec(px + PP * (p % 4), y + (PS + 0.8f) * (p / 4))));
                q->box.size = mm2px(Vec(PS, PS)); q->module = module; q->p = p;
                addChild(q);
            }
            // the looper (ECHO / FX / LOOP) as the section launches: NONE PLAY STOP REC ONCE
            y += PH + GAP;
            static const char* const ln[5] = {"NO LOOP", "PLAY", "STOP", "REC", "ONCE"};
            static const int la[5] = {songlink::L_NONE, songlink::L_PLAY, songlink::L_STOP, songlink::L_REC, songlink::L_ONCE};
            const float lw = (inner - 4.f * CG) / 5.f;
            for (int i = 0; i < 5; i++) {
                SongBtn* b = btn(x0 + i * (lw + CG), y, lw, CH, ln[i]);
                int a = la[i];
                if (i == 0) b->fontSize = 4.6f;
                if (a == songlink::L_REC) b->tint = nvgRGB(0xff, 0x32, 0x28);
                if (a == songlink::L_PLAY || a == songlink::L_ONCE) b->tint = oled::green();
                b->lit = [=]() { MS::Cell* c = selCell(); return c ? c->loop == a : a == songlink::L_NONE; };
                b->press = [=](int) { if (MS::Cell* c = selCell()) c->loop = a; };
            }
            // the cell, the cell across its row, the section to the next
            y += CH + GAP;
            static const char* const tn[3] = {"CLEAR CELL", "CELL TO ROW", "COPY SECTION >"};
            const float tw = (inner - 2.f * CG) / 3.f;
            for (int i = 0; i < 3; i++) {
                SongBtn* b = btn(x0 + i * (tw + CG), y, tw, CH, tn[i]);
                b->lit = []() { return false; };
                b->press = [=](int) {
                    if (!module) return;
                    MS* m = module;
                    int s = m->selSec, r = m->selRow;
                    if (i == 0) m->cells[s][r] = MS::Cell();
                    if (i == 1) for (int k = 0; k < kSecs; k++) m->cells[k][r] = m->cells[s][r];
                    if (i == 2) { int to = (s + 1) % kSecs; for (int k = 0; k < kRows; k++) m->cells[to][k] = m->cells[s][k]; m->selSec = to; }
                    m->edited(m->curSec);
                };
            }
        }
        // BPM on the left of the input row, CLK IN beside it
        const float KY = 98.f, KX = SX + 14.f;
        addChild(KnobTicks::create(Vec(KX, KY)));
        ParamWidget* kn = createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(KX, KY)), module, MS::BPM_PARAM);
        if (Knob* k = dynamic_cast<Knob*>(kn)) k->snap = true;
        addParam(kn);
        label(KX, KY - 7.f, "BPM");
        // jacks: inputs, then outputs
        auto jack = [&](float x, float y, int id, bool out, const char* name) {
            label(x, y - 5.5f, name);
            if (out) {
                addOutput(createOutputCentered<SmallJack>(mm2px(Vec(x, y)), module, id));
                addChild(JackLed::create(Vec(x + 3.9f, y), module, id, true));
            } else addInput(createInputCentered<SmallJack>(mm2px(Vec(x, y)), module, id));
        };
        const float JY = 118.f;
        jack(SX + SW - 14.f, KY, MS::CLOCK_INPUT, false, "CLK IN");
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
        menu->addChild(createMenuItem("Clear the selected row (every section)", "", [=]() {
            for (int s = 0; s < kSecs; s++) m->cells[s][m->selRow] = MS::Cell();
            m->edited(m->curSec);
        }));
        menu->addChild(createMenuItem("Clear the selected section", "", [=]() {
            for (int r = 0; r < kRows; r++) m->cells[m->selSec][r] = MS::Cell();
            m->edited(m->curSec);
        }));
        menu->addChild(new MenuSeparator);
        menu->addChild(createSubmenuItem("JAM controls", "", [](Menu* sub) {
            static const char* const key[] = {
                "Pad: select the cell", "MUTE + pad: mute the cell", "CLEAR + pad: clear the cell",
                "Scene 1-8: launch A-H", "PREV / NEXT: launch the section before / after",
                "A-H: mute the row", "Touch strips: row levels",
                "PLAY: RUN", "REC: the selected cell's looper REC", "TEMPO: RESET", "GRID: launch on bar / beat",
                "Arrows: move the selected cell", "DUPLICATE: copy the section to the next", "Knob: BPM"};
            for (const char* k : key) sub->addChild(createMenuLabel(k));
        }));
        menu->addChild(createSubmenuItem("JAM matrix page (must match the JAM)", string::f("%d", m->jamPage), [=](Menu* sub) {
            for (int p = 1; p <= 6; p++)
                sub->addChild(createCheckMenuItem(string::f("%d", p), "", [=]() { return m->jamPage == p; }, [=]() { m->jamPage = p; }));
        }));
    }
};

} // namespace

Model* modelSynTHBloKMatrixSong = createModel<SynTHBloKMatrixSong, SynTHBloKMatrixSongWidget>("SynTHBloKMatrixSong");
