// ============================================================================
// SynTHBloK MATRIX FORGE  (MATRIX-8 groovebox + the GRAIN-FORGE FX chain)
//  The MATRIX-8 JAM sequencer and its 8 row voices, and the whole mix through every GRAIN-FORGE stage,
//  clocked by the sequencer. Built on the GRAIN-FORGE module: its params, ports and DSP come first,
//  MATRIX FORGE's own follow. (The LEAD / BASS synths are now their own module, SynTHBloK DUO:
//  patch it into IN L/R to run it through the FX.)
//  JAM (Controller Editor "Template 4", MIDI channel 1), as on MATRIX-8.
//   CLEAR clears this pattern; hold it for a second to clear every pattern.
//  No clock patched: the sequencer and FX run at 120 BPM.
// ============================================================================
#include "SynTHBloKGrainForge.hpp"
#include "Matrix8_dsp.hpp"
#include "Matrix8_ui.hpp"
#include "OledUi.hpp"
#include "ForgeFxUi.hpp"
#include "JackLed.hpp"
#include <cstdio>
#include <string>

#include "JamOwner.hpp"
#include "SongLink.hpp"
#include <thread>
#include <chrono>
namespace {

struct SynTHBloKMatrixForge : SynTHBloKGrainForge, songlink::Member {
    typedef SynTHBloKGrainForge GF;
    enum MFParamIds { LEN_PARAM = GF::NUM_PARAMS, PAT_PARAM, PAGE_PARAM, OFFC_PARAM, ONC_PARAM, HEADC_PARAM, HITC_PARAM,
                      CLEAR_PARAM, LEARN_PARAM, BANK_PARAM, RANGE_PARAM, BONC_PARAM, BOFFC_PARAM, RUN_PARAM, RESET_PARAM,
                      KNOB_PARAM, M8_DELAY_PARAM, SPACE_PARAM,
                      F2_CUTOFF_PARAM, F2_RES_PARAM, F2_ENV_PARAM, F2_MODE_PARAM,
                      ENV_A_PARAM, ENV_D_PARAM, ENV_S_PARAM, ENV_R_PARAM, MIDI_ON_PARAM, RAND_PARAM, MF_NUM_PARAMS };
    // GRAIN-FORGE's inputs come first: IN L/R (added to the mix), CLOCK and RESET (sequencer + FX), OPEN, GLITCH, GATE.
    enum MFInputIds { RUN_INPUT = GF::NUM_INPUTS, MF_NUM_INPUTS };
    // GRAIN-FORGE's OUT L/R are the main outs.
    enum MFOutputIds { TRIG_OUTPUT = GF::NUM_OUTPUTS, STRIP_OUTPUT = TRIG_OUTPUT + 8, KNOB_OUTPUT = STRIP_OUTPUT + 8, MF_NUM_OUTPUTS };

    midi::InputQueue midiIn;
    midi::Output midiOut;
    bool slowSend = false;
    jamown::Link jam;   // the JAM follows the last clicked module (JamOwner.hpp)

    bool pat[8][8][8];
    bool acc[8][8][8];            // [pattern][row][step]: an accent on that hit (right-click a pad)
    float val[8][8];
    float stripSm[8];
    int step = -1;                // sequencer step (GRAIN-FORGE's own 16-step counter is GF::step)
    bool pendingReset = false;
    dsp::SchmittTrigger rstBtnTrig, runTrig, clrTrig, randTrig;
    dsp::PulseGenerator trigPulse[8];

    bool running = true;
    float resetFlash = 0.f;

    // Track mute/solo, rows 0..7. Hold MUTE or SOLO on the JAM and press a pad.
    bool mute[8] = {}, solo[8] = {};
    bool muteHeld = false, soloHeld = false, jamReset = false;
    bool clearHeld = false, clearAllDone = false;
    float clearHeldT = 0.f;
    int knobCC = kKnobFirstCC;
    float knobSm = 0.f;
    M8Voice voice[8] = {M8Voice(1), M8Voice(2), M8Voice(3), M8Voice(4), M8Voice(5), M8Voice(6), M8Voice(7), M8Voice(8)};
    M8Delay m8delay;
    M8Reverb reverb;
    float delayMix = 0.f, spaceMix = 0.f, masterVol = 0.8f;

    int knobParam() {
        switch (knobCC) {
            case kKnobFirstCC + 1: return M8_DELAY_PARAM;
            case kKnobFirstCC + 2: return SPACE_PARAM;
            default: return KNOB_PARAM;
        }
    }

    int lastLed[kNumLed];
    bool matchColors = true;
    int ledScan = 0, lastBank = -1, lastPage = -1;
    int stripPage = 0, lastStripPage = -1;
    float tokens = 4.f, resync = 0.f;

    SynTHBloKMatrixForge() : GF(MF_NUM_PARAMS - GF::NUM_PARAMS, MF_NUM_INPUTS - GF::NUM_INPUTS, MF_NUM_OUTPUTS - GF::NUM_OUTPUTS, 0) {
        songlink::join(this, this, "FORGE", 8, false);
        configParam(LEN_PARAM, 1.f, 8.f, 8.f, "Pattern length (steps)");
        configSwitch(MIDI_ON_PARAM, 0.f, 1.f, 1.f, "JAM MIDI (off = ignore the JAM, its lights dark)", {"Off", "On"});
        configParam(PAT_PARAM, 1.f, 8.f, 1.f, "Pattern");
        configParam(PAGE_PARAM, 1.f, 6.f, 1.f, "JAM matrix page (must match the JAM)");
        configParam(OFFC_PARAM, 0.f, 127.f, 0.f, "LED colour: empty step");
        configParam(ONC_PARAM, 0.f, 127.f, 127.f, "LED colour: active step");
        configParam(HEADC_PARAM, 0.f, 127.f, 30.f, "LED colour: playhead over empty step");
        configParam(HITC_PARAM, 0.f, 127.f, 90.f, "LED colour: playhead over active step");
        configButton(CLEAR_PARAM, "Clear this view of the current pattern");
        configButton(RAND_PARAM, "Random grooves on all 8 patterns (a theme and variations, in key) and new strip values on all 8 banks");
        configSwitch(LEARN_PARAM, 0.f, 1.f, 0.f, "Learn page: then press the TOP-LEFT pad on the JAM", {"Off", "Armed"});
        configParam(BANK_PARAM, 1.f, 8.f, 1.f, "Strip bank (A..H)");
        configSwitch(RANGE_PARAM, 0.f, 1.f, 0.f, "Strip output range", {"0..10 V", "-5..+5 V"});
        configParam(BONC_PARAM, 0.f, 127.f, 127.f, "LED colour: selected bank button");
        configParam(BOFFC_PARAM, 0.f, 127.f, 20.f, "LED colour: other bank buttons");
        configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run (a trigger at the RUN jack toggles it)", {"Stopped", "Running"});
        configButton(RESET_PARAM, "Reset (a trigger at the RESET jack also resets)");
        configParam(KNOB_PARAM, 0.f, 1.f, 0.8f, "Master volume (JAM knob on MST or CUE)", "%", 0.f, 100.f);
        configParam(M8_DELAY_PARAM, 0.f, 1.f, 0.f, "Delay, ping-pong dotted 1/8 (JAM knob on GRP)", "%", 0.f, 100.f);
        configParam(SPACE_PARAM, 0.f, 1.f, 0.f, "Space / reverb (JAM knob on IN1)", "%", 0.f, 100.f);
        configParam(F2_CUTOFF_PARAM, 0.f, 1.f, 1.f, "Voices filter 2 cutoff", " Hz", 1000.f, 20.f);
        configParam(F2_RES_PARAM, 0.f, 1.f, 0.f, "Voices filter 2 resonance", "%", 0.f, 100.f);
        configParam(F2_ENV_PARAM, 0.f, 1.f, 0.f, "Voices filter 2 envelope amount", "%", 0.f, 100.f);
        configParam(F2_MODE_PARAM, 0.f, 1.f, 0.f, "Voices filter 2 mode (low-pass -> band-pass -> high-pass)", "%", 0.f, 100.f);
        configParam(ENV_A_PARAM, 0.f, 1.f, 0.f, "Voices envelope attack", " ms", 2000.f, 1.f);
        configParam(ENV_D_PARAM, 0.f, 1.f, 0.5f, "Voices envelope decay", " ms", 400.f, 5.f);
        configParam(ENV_S_PARAM, 0.f, 1.f, 1.f, "Voices envelope sustain", "%", 0.f, 100.f);
        configParam(ENV_R_PARAM, 0.f, 1.f, 0.6f, "Voices envelope release", " ms", 800.f, 5.f);        for (int i = LEN_PARAM; i < CLEAR_PARAM; i++) paramQuantities[i]->snapEnabled = true;
        paramQuantities[BANK_PARAM]->snapEnabled = true;
        paramQuantities[BONC_PARAM]->snapEnabled = true;
        paramQuantities[BOFFC_PARAM]->snapEnabled = true;
        configInput(IN_L_INPUT, "Left (added to the voices before the FX; patch SynTHBloK DUO here)");
        configInput(IN_R_INPUT, "Right (left is copied here when unpatched)");
        configInput(CLOCK_INPUT, "Clock (16th notes; steps the sequencer and the FX; 120 BPM internal when unpatched)");
        configInput(RESET_INPUT, "Reset (trigger: same as pressing RESET)");
        configInput(RUN_INPUT, "Run (trigger: toggles RUN, like pressing it)");
        for (int r = 0; r < 8; r++) configOutput(TRIG_OUTPUT + r, "Track " + std::to_string(r + 1) + " trigger");
        for (int i = 0; i < 8; i++) { configOutput(STRIP_OUTPUT + i, "Strip " + std::to_string(i + 1)); stripSm[i] = 0.f; }
        configOutput(KNOB_OUTPUT, "Knob (0..10 V)");
        configOutput(OUT_L_OUTPUT, "Audio left");
        configOutput(OUT_R_OUTPUT, "Audio right");        for (int p = 0; p < 8; p++) clearPattern(p);
        for (int b = 0; b < 8; b++) for (int i = 0; i < 8; i++) val[b][i] = kDefaultVal[b][i];
        for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
    }

    bool anySolo() { for (int r = 0; r < 8; r++) if (solo[r]) return true; return false; }
    bool anyMute() { for (int r = 0; r < 8; r++) if (mute[r]) return true; return false; }
    bool trackOn(int r) { return !mute[r] && (!anySolo() || solo[r]); }
    void clearPattern(int p) { for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) pat[p][r][c] = acc[p][r][c] = false; }
    void stepParam(int id, int delta, int lo, int hi, bool wrap) {
        int v = (int)params[id].getValue() + delta;
        if (wrap) v = lo + ((v - lo) % (hi - lo + 1) + (hi - lo + 1)) % (hi - lo + 1);
        params[id].setValue((float)clamp(v, lo, hi));
    }

    void jamButton(int cc, int b) {
        bool down = b > 0;
        if (cc == CC_SOLO) { soloHeld = down; return; }
        if (cc == CC_MUTE) { muteHeld = down; return; }
        if (cc == CC_CLEAR && !down) { clearHeld = false; return; }
        if (!down) return;
        switch (cc) {
            case CC_CLEAR: clearPattern(curPat()); clearHeld = true; clearHeldT = 0.f; clearAllDone = false; break;
            case CC_DUPLICATE: {
                int from = curPat(), to = (from + 1) % 8;
                for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) { pat[to][r][c] = pat[from][r][c]; acc[to][r][c] = acc[from][r][c]; }
                params[PAT_PARAM].setValue((float)(to + 1));
            } break;
            case CC_UP: stepParam(LEN_PARAM, 1, 1, 8, false); break;
            case CC_DOWN: stepParam(LEN_PARAM, -1, 1, 8, false); break;
            case CC_LEFT: stepParam(BANK_PARAM, -1, 1, 8, false); break;
            case CC_RIGHT: stepParam(BANK_PARAM, 1, 1, 8, false); break;
            case CC_PLAY: params[RUN_PARAM].setValue(params[RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f); break;
            case CC_REC: params[LEARN_PARAM].setValue(params[LEARN_PARAM].getValue() > 0.5f ? 0.f : 1.f); break;
            case CC_PREV: stepParam(PAT_PARAM, -1, 1, 8, true); break;
            case CC_NEXT: stepParam(PAT_PARAM, 1, 1, 8, true); break;
            case CC_TEMPO: jamReset = true; break;
            case CC_GRID: params[RANGE_PARAM].setValue(params[RANGE_PARAM].getValue() > 0.5f ? 0.f : 1.f); break;
        }
    }

    int curPat() { return clamp((int)params[PAT_PARAM].getValue(), 1, 8) - 1; }
    int songPatternNow() override { return curPat(); }
    void songSetPattern(int p) override { params[PAT_PARAM].setValue((float)(clamp(p, 0, 7) + 1)); }
    int curPage() { return clamp((int)params[PAGE_PARAM].getValue(), 1, 6); }
    int bank() { return clamp((int)params[BANK_PARAM].getValue(), 1, 8) - 1; }
    int padNote(int r, int c) { return std::min(127, 12 * (curPage() - 1) + 56 + c - 8 * r); }
    bool padOn(int r, int c) { return pat[curPat()][r][c]; }
    void togglePad(int r, int c) { bool& b = pat[curPat()][r][c]; b = !b; if (!b) acc[curPat()][r][c] = false; }
    // Right-click: hit <-> accent; an empty step becomes an accent.
    bool padAcc(int r, int c) { return acc[curPat()][r][c]; }
    void toggleAccent(int r, int c) {
        int p = curPat();
        if (!pat[p][r][c]) { pat[p][r][c] = acc[p][r][c] = true; return; }
        acc[p][r][c] = !acc[p][r][c];
    }
    float stripGet(int i) { return val[bank()][i]; }
    void stripSet(int i, float x) { val[bank()][i] = clamp(x, 0.f, 1.f); }

    // RAND: 8 related patterns over a chord progression, and new strip values; always in key and in harmony.
    // A key (C minor or C major) and four chords (i-VI-iv-v and the like, each with a perfect fifth) are
    // picked, and the voices are tuned to them (PITCH bank): the bottom four rows are the chords' roots in
    // the bass, the top four the notes that cover every chord best (each chord gets at least two). Patterns
    // 1..4 play the four chords and 5..8 play them again: each pattern only uses the rows that belong to its
    // chord, so every step sounds as a chord. The rhythms are one theme (the bass on the beat, a chord stab
    // on the downbeat, an answer off the beat, a busier top line); later patterns vary it a little and
    // patterns 4 and 8 end on a fill. Accents fall on the beats. The sound banks stay in ranges that keep
    // every voice clean and the mix balanced (the bass centred and a little louder).
    void randomizeAll() {
        auto rnd = []() { return random::uniform(); };
        auto pick = [&](int n) { return std::min((int)(rnd() * n), n - 1); };
        auto spread = [&](int k, int rot) {   // k hits spread evenly over the 8 steps
            int m = 0;
            for (int i = 0; i < 8; i++) if ((i * k) % 8 < k) m |= 1 << ((i + rot) % 8);
            return m;
        };
        static const int kMinor[7] = {0, 2, 3, 5, 7, 8, 10}, kMajor[7] = {0, 2, 4, 5, 7, 9, 11};
        const int* sc = rnd() < 0.5f ? kMinor : kMajor;
        auto tone = [&](int deg, int i) { return sc[(deg + 2 * i) % 7]; };   // pitch class of a chord's root / 3rd / 5th
        static const int kProg[6][4] = {{0, 5, 3, 4}, {0, 4, 5, 3}, {0, 3, 4, 0}, {0, 3, 0, 4}, {0, 5, 3, 0}, {0, 2, 3, 4}};
        const int* prog = kProg[pick(6)];
        auto inChord = [&](int pc, int k) { for (int i = 0; i < 3; i++) if (tone(prog[k], i) == pc % 12) return true; return false; };
        // bass rows (7 = lowest .. 4): the chord roots, then the key's fifth and its octave as spares
        std::vector<int> bass;
        for (int k = 0; k < 4; k++) if (std::find(bass.begin(), bass.end(), tone(prog[k], 0)) == bass.end()) bass.push_back(tone(prog[k], 0));
        for (int x : {7, 12}) if ((int)bass.size() < 4 && std::find(bass.begin(), bass.end(), x) == bass.end()) bass.push_back(x);
        std::sort(bass.begin(), bass.end());
        // upper rows (3 .. 0 = highest): the 4 chord tones that cover the chords best (two or more each)
        std::vector<int> cand;
        for (int k = 0; k < 4; k++) for (int i = 0; i < 3; i++)
            if (std::find(cand.begin(), cand.end(), tone(prog[k], i)) == cand.end()) cand.push_back(tone(prog[k], i));
        std::vector<int> best;
        float bestScore = -1.f;
        int nc = (int)cand.size();
        for (int a = 0; a < nc; a++) for (int b = a + 1; b < nc; b++) for (int c = b + 1; c < nc; c++) for (int d = c + 1; d < nc; d++) {
            int lo = 9, sum = 0;
            for (int k = 0; k < 4; k++) {
                int n = inChord(cand[a], k) + inChord(cand[b], k) + inChord(cand[c], k) + inChord(cand[d], k);
                lo = std::min(lo, n); sum += n;
            }
            float score = lo * 100.f + sum + rnd();
            if (score > bestScore) { bestScore = score; best = {cand[a], cand[b], cand[c], cand[d]}; }
        }
        std::vector<int> upper;
        for (int pc : best) upper.push_back(pc + (pc + 12 <= 14 ? 24 : 12));   // above the bass, within 3 octaves
        std::sort(upper.begin(), upper.end());
        int pitch[8];
        for (int i = 0; i < 4; i++) { pitch[7 - i] = bass[i]; pitch[3 - i] = upper[i]; }
        for (int r = 0; r < 8; r++) val[VP_PITCH][r] = pitch[r] / 36.f;
        // the theme (bit c = step c): rows 3 and 2 hit the downbeat together, row 1 answers off the beat
        static const int kBass[4] = {0x11, 0x01, 0x91, 0x21};
        static const int kStab[3] = {0x11, 0x01, 0x05};
        static const int kOff[4] = {0x44, 0x22, 0x4C, 0xA0};
        int theme[8];
        int bassR = kBass[pick(4)];
        for (int r = 4; r < 8; r++) theme[r] = bassR;
        theme[3] = kStab[pick(3)];
        theme[2] = spread(2 + pick(2), 0);
        theme[1] = kOff[pick(4)];
        theme[0] = spread(3 + pick(3), pick(8));
        for (int p = 0; p < 8; p++) {
            int k = p % 4;
            for (int r = 0; r < 8; r++) {
                bool on = r >= 4 ? pitch[r] % 12 == tone(prog[k], 0) : inChord(pitch[r], k);
                int m = on ? theme[r] : 0;
                if (p > 0 && r < 4 && m && rnd() < 0.35f) m ^= 1 << (1 + 2 * pick(4));   // a variation off the beat
                if ((p == 3 || p == 7) && r < 2 && m) m |= 0xC0;                       // the fill: the last two steps
                for (int c = 0; c < 8; c++) {
                    pat[p][r][c] = m >> c & 1;
                    acc[p][r][c] = pat[p][r][c] && c == 0 && (r >= 4 || rnd() < 0.5f);
                }
            }
        }
        for (int r = 0; r < 8; r++) {
            bool low = r >= 4;
            val[VP_CUTOFF][r] = 0.4f + rnd() * 0.4f;
            val[VP_RES][r] = rnd() * 0.4f;
            val[VP_DECAY][r] = low ? 0.3f + rnd() * 0.35f : 0.12f + rnd() * 0.4f;
            val[VP_WAVE][r] = rnd();
            val[VP_LEVEL][r] = low ? 0.7f + rnd() * 0.15f : 0.55f + rnd() * 0.2f;
            val[VP_PAN][r] = low ? 0.5f : 0.3f + rnd() * 0.4f;
            val[VP_DRIVE][r] = rnd() * 0.3f;
        }
    }

    void onReset() override { GF::onReset(); step = -1; }

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
    // Rack closing / the module deleted: lights off, paced so the JAM takes every message
    ~SynTHBloKMatrixForge() { songlink::leave(this); if (jam.active) { slowSend = true; clearLeds(); } jamown::release(id); }

    int desiredLed(int idx) {
        if (idx == kKnobLed) return (int)std::round(params[knobParam()].getValue() * 127.f);
        if (idx >= 88) {
            switch (kBtnCC[idx - 88]) {
                case CC_SONG: return 0;
                case CC_PLAY: return running ? 127 : 0;
                case CC_REC: return params[LEARN_PARAM].getValue() > 0.5f ? 127 : 0;
                case CC_TEMPO: return resetFlash > 0.f ? 127 : 0;
                case CC_GRID: return params[RANGE_PARAM].getValue() > 0.5f ? 127 : 0;
                case CC_SOLO: return soloHeld || anySolo() ? 127 : 0;
                case CC_MUTE: return muteHeld || anyMute() ? 127 : 0;
                default: return 127;
            }
        }
        if (matchColors && (idx < 72 || idx >= 80)) {
            if (idx < 64) {
                // like MATRIX-DUO: hit = the page's bright colour, empty = dark except the top and bottom
                // rows (the page's dim colour). Muted: hits dim, the rest dark.
                int r = idx / 8, c = idx % 8, p = curPat();
                bool a = padOn(r, c), edge = r == 0 || r == 7;
                if (!trackOn(r)) return a ? kJamDim[p] : 0;
                if (running && step >= 0 && c == step) return a ? kJamWhite : kJamWhite - 2;
                return a ? kJamBright[p] : edge ? kJamDim[p] : 0;
            }
            int i = idx < 72 ? idx - 64 : idx - 80;
            bool sel = idx < 72 ? i == curPat() : i == bank();
            return sel ? kJamBright[i] : kJamDim[i];
        }
        if (idx < 72) {
            int on = (int)params[ONC_PARAM].getValue(), off = (int)params[OFFC_PARAM].getValue();
            int head = (int)params[HEADC_PARAM].getValue(), hit = (int)params[HITC_PARAM].getValue();
            if (idx < 64) {
                int r = idx / 8, c = idx % 8;
                bool a = padOn(r, c);
                if (running && step >= 0 && c == step) return a ? hit : head;
                return a ? on : off;
            }
            return (idx - 64) == curPat() ? on : off;
        }
        if (idx < 80) return (int)std::round(stripGet(idx - 72) * 127.f);
        return (idx - 80) == bank() ? (int)params[BONC_PARAM].getValue() : (int)params[BOFFC_PARAM].getValue();
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

    void process(const ProcessArgs& args) override {
        const float songG = songPoll(args.sampleTime);   // MATRIX-SONG: its pattern requests, and its mute on OUT L / R
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

        if (runTrig.process(inputs[RUN_INPUT].getVoltage()))
            params[RUN_PARAM].setValue(params[RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f);
        running = params[RUN_PARAM].getValue() > 0.5f;
        // GRAIN-FORGE's clock: the CLOCK jack, or 120 BPM when unpatched. It steps the sequencer and the FX.
        float stepSec;
        bool jackRst;
        bool edge = clockStep(args, stepSec, jackRst);
        bool rst = jackRst | rstBtnTrig.process(params[RESET_PARAM].getValue()) | jamReset;
        jamReset = false;
        if (rst) { step = -1; pendingReset = true; resetFlash = 0.15f; }
        resetFlash = std::max(0.f, resetFlash - args.sampleTime);
        if (clrTrig.process(params[CLEAR_PARAM].getValue())) clearPattern(curPat());
        if (randTrig.process(params[RAND_PARAM].getValue())) randomizeAll();
        if (clearHeld && !clearAllDone && (clearHeldT += args.sampleTime) > 1.f) {
            for (int p = 0; p < 8; p++) clearPattern(p);
            clearAllDone = true;
        }
        if (edge && running) {
            int len = clamp((int)params[LEN_PARAM].getValue(), 1, 8);
            step = (pendingReset || step < 0) ? 0 : (step + 1) % len;
            pendingReset = false;
            for (int r = 0; r < 8; r++)
                if (pat[curPat()][r][step] && trackOn(r) && !songMuted.load()) {
                    trigPulse[r].trigger(0.01f);
                    voice[r].trigger(stepSec, acc[curPat()][r][step] ? 1.4f : 1.f);   // held for one step; an accent +3 dB
                }
        }
        for (int r = 0; r < 8; r++) outputs[TRIG_OUTPUT + r].setVoltage(trigPulse[r].process(args.sampleTime) ? 10.f : 0.f);

        float k = 1.f - std::exp(-args.sampleTime * 1000.f);
        bool bip = params[RANGE_PARAM].getValue() > 0.5f;
        for (int i = 0; i < 8; i++) {
            float t = val[bank()][i] * 10.f - (bip ? 5.f : 0.f);
            stripSm[i] += (t - stripSm[i]) * k;
            outputs[STRIP_OUTPUT + i].setVoltage(stripSm[i]);
        }
        knobSm += (params[KNOB_PARAM].getValue() * 10.f - knobSm) * k;
        outputs[KNOB_OUTPUT].setVoltage(knobSm);

        // ---- voices: strip bank b, strip r = parameter b of voice r ----
        float l = 0.f, r = 0.f;
        M8Global gs;
        gs.cutoff = params[F2_CUTOFF_PARAM].getValue();
        gs.res = params[F2_RES_PARAM].getValue();
        gs.envAmt = params[F2_ENV_PARAM].getValue();
        gs.mode = params[F2_MODE_PARAM].getValue();
        gs.a = params[ENV_A_PARAM].getValue();
        gs.d = params[ENV_D_PARAM].getValue();
        gs.s = params[ENV_S_PARAM].getValue();
        gs.r = params[ENV_R_PARAM].getValue();
        for (int v = 0; v < 8; v++) {
            float p[8];
            for (int b = 0; b < 8; b++) p[b] = val[b][v];
            float s = voice[v].process(args.sampleRate, p, gs);
            float pan = p[VP_PAN] * float(M_PI) * 0.5f;
            l += s * std::cos(pan);
            r += s * std::sin(pan);
        }
        // ---- MATRIX-8 delay (dotted 1/8 = 3 steps) and space ----
        float dl, dr, rl, rr;
        m8delay.process(l, r, args.sampleRate, stepSec * 3.f, dl, dr);
        delayMix += (params[M8_DELAY_PARAM].getValue() - delayMix) * k;
        l += dl * delayMix * 0.8f;
        r += dr * delayMix * 0.8f;
        reverb.process(l, r, args.sampleRate, rl, rr);
        spaceMix += (params[SPACE_PARAM].getValue() - spaceMix) * k;
        l += rl * spaceMix;
        r += rr * spaceMix;

        // ---- the whole mix (plus IN L/R) through GRAIN-FORGE, then the master volume ----
        float vL = 5.f * std::tanh(l * 0.5f) + inputs[IN_L_INPUT].getVoltage();
        float vR = 5.f * std::tanh(r * 0.5f) + (inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : inputs[IN_L_INPUT].getVoltage());
        float oL, oR;
        processFx(args, vL, vR, edge, stepSec, rst, oL, oR);
        masterVol += (params[KNOB_PARAM].getValue() - masterVol) * k;
        outputs[OUT_L_OUTPUT].setVoltage(5.f * oL * masterVol * songG);
        outputs[OUT_R_OUTPUT].setVoltage(5.f * oR * masterVol * songG);
        songMeter(outputs[OUT_L_OUTPUT].getVoltage(), outputs[OUT_R_OUTPUT].getVoltage());

        // ---- JAM lights ----
        if (!midiOn) return;
        resync += args.sampleTime;
        if (curPage() != lastPage || bank() != lastBank || stripPage != lastStripPage || resync > 3.f) {
            for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
            lastPage = curPage(); lastBank = bank(); lastStripPage = stripPage; resync = 0.f;
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

    json_t* dataToJson() override {
        json_t* root = GF::dataToJson();
        json_t* arr = json_array();
        for (int p = 0; p < 8; p++) {
            std::string s;
            for (int i = 0; i < 64; i++) s += pat[p][i / 8][i % 8] ? '1' : '0';
            json_array_append_new(arr, json_string(s.c_str()));
        }
        json_object_set_new(root, "patterns", arr);
        json_t* aa = json_array();   // 64 chars per pattern, row by row: 1 = accent
        for (int p = 0; p < 8; p++) {
            std::string s;
            for (int i = 0; i < 64; i++) s += acc[p][i / 8][i % 8] ? '1' : '0';
            json_array_append_new(aa, json_string(s.c_str()));
        }
        json_object_set_new(root, "accents", aa);
        json_t* va = json_array();
        for (int b = 0; b < 8; b++) for (int i = 0; i < 8; i++) json_array_append_new(va, json_real(val[b][i]));
        json_object_set_new(root, "values", va);
        json_object_set_new(root, "matchColors", json_boolean(matchColors));
        std::string ms, ss;
        for (int i = 0; i < 8; i++) { ms += mute[i] ? '1' : '0'; ss += solo[i] ? '1' : '0'; }
        json_object_set_new(root, "mute", json_string(ms.c_str()));
        json_object_set_new(root, "solo", json_string(ss.c_str()));
        json_object_set_new(root, "in", midiIn.toJson());
        json_object_set_new(root, "out", midiOut.toJson());
        return root;
    }
    void dataFromJson(json_t* r) override {
        json_t* arr = json_object_get(r, "patterns");
        for (int p = 0; p < 8; p++) {
            const char* s = arr ? json_string_value(json_array_get(arr, p)) : NULL;
            if (s && strlen(s) >= 64) for (int i = 0; i < 64; i++) pat[p][i / 8][i % 8] = s[i] == '1';
        }
        json_t* aa = json_object_get(r, "accents");   // older patches: no accents
        for (int p = 0; p < 8; p++) {
            const char* s = aa ? json_string_value(json_array_get(aa, p)) : NULL;
            for (int i = 0; i < 64; i++) acc[p][i / 8][i % 8] = s && strlen(s) >= 64 && s[i] == '1' && pat[p][i / 8][i % 8];
        }
        if (json_t* va = json_object_get(r, "values"))
            for (int i = 0; i < 64; i++)
                if (json_t* j = json_array_get(va, i)) val[i / 8][i % 8] = (float)json_number_value(j);
        if (json_t* j = json_object_get(r, "matchColors")) matchColors = json_boolean_value(j);
        const char* ms = json_string_value(json_object_get(r, "mute"));
        const char* ss = json_string_value(json_object_get(r, "solo"));
        for (int i = 0; i < 8; i++) {
            mute[i] = ms && strlen(ms) >= 8 && ms[i] == '1';
            solo[i] = ss && strlen(ss) >= 8 && ss[i] == '1';
        }
        if (json_t* j = json_object_get(r, "in")) midiIn.fromJson(j);
        if (json_t* j = json_object_get(r, "out")) midiOut.fromJson(j);
    }
};

typedef SynTHBloKMatrixForge MF;

// ---------------------------------------------------------------- widgets ----------------

struct MFPad : OpaqueWidget {
    MF* module = NULL;
    int r = 0, c = 0;
    void draw(const DrawArgs& args) override {
        bool on = module ? module->padOn(r, c) : ((r == 0 && (c % 4) == 0) || (r == 1 && c == 4) || (r == 3 && (c % 2) == 1));
        bool playing = module && module->step >= 0 && module->step == c;
        int p = module ? module->curPat() : 0;
        NVGcolor col = kPatColor[p], onCol = kPatOnColor[p];
        if (module && !module->trackOn(r)) {
            col = nvgLerpRGBA(kIdle, col, 0.35f);
            onCol = nvgLerpRGBA(kIdle, onCol, 0.35f);
        }
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        NVGcolor fill = on ? onCol : kIdle;
        if (playing) fill = on ? nvgLerpRGBA(onCol, nvgRGB(0xff, 0xff, 0xff), 0.55f) : nvgLerpRGBA(kIdle, col, 0.45f);
        nvgFillColor(args.vg, fill);
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, col);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
        if (on && module && module->padAcc(r, c)) {   // accent: a bright core, as on MATRIX-DRUM
            float m = box.size.x * 0.3f;
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, m, m, box.size.x - 2.f * m, box.size.y - 2.f * m, 0.8f);
            nvgFillColor(args.vg, nvgLerpRGBA(onCol, nvgRGB(0xff, 0xf4, 0xd0), 0.75f));
            nvgFill(args.vg);
        }
    }
    // click = hit on / off, right-click = accent
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || !module) return;
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) { module->togglePad(r, c); e.consume(this); }
        else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) { module->toggleAccent(r, c); e.consume(this); }
    }
};

struct MFPatBtn : OpaqueWidget {
    MF* module = NULL;
    int p = 0;
    void draw(const DrawArgs& args) override {
        drawColorButton(args, box.size, p, module ? module->curPat() == p : p == 0, (char)('1' + p), 11.f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[MF::PAT_PARAM].setValue((float)(p + 1));
            e.consume(this);
        }
    }
};

// Touch strip: the selected bank's value for this voice.
struct MFStrip : OpaqueWidget {
    MF* module = NULL;
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
            e.consume(this);
        }
    }
    void onDragMove(const DragMoveEvent& e) override {
        float z = APP->scene->rackScroll->getZoom();
        set(get() - e.mouseDelta.y / z / (box.size.y - 3.f));
    }
};

struct MFBankSq : OpaqueWidget {
    MF* module = NULL;
    int b = 0;
    void draw(const DrawArgs& args) override {
        drawColorButton(args, box.size, b, module ? module->bank() == b : b == 0, (char)('A' + b), 9.f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[MF::BANK_PARAM].setValue((float)(b + 1));
            e.consume(this);
        }
    }
};

struct MFTitle : TransparentWidget {
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
        nvgFontSize(args.vg, 11.f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgText(args.vg, 0.f, mm2px(3.5f), "SynTHBloK", NULL);
        nvgFontSize(args.vg, 10.f);
        nvgTextLetterSpacing(args.vg, 0.8f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0x7a, 0x00));
        nvgText(args.vg, 0.f, mm2px(6.4f), "MTX FORGE", NULL);
    }
};

// Tempo, step, what the strips play, and run state.
struct MFClockDisplay : TransparentWidget {
    MF* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.f);
        nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (font) {
            NVGcolor amb = nvgRGBA(255, 190, 90, 255), dim = nvgRGBA(255, 190, 90, 130);
            float bigFont = std::max(11.f, H * 0.3f), smallFont = std::max(8.f, H * 0.2f);
            char bpm[24] = "120.0 BPM", info[32] = "INT  STEP 1/8", strips[24] = "A PITCH";
            bool run = module ? module->running : true;
            if (module) {
                bool ext = module->inputs[MF::CLOCK_INPUT].isConnected();
                if (ext) {
                    if (module->clockPeriod > 0.f && module->sinceClock < 2.f)
                        snprintf(bpm, sizeof bpm, "%.1f BPM", 60.f / (module->clockPeriod * 4.f));
                    else snprintf(bpm, sizeof bpm, "--- BPM");
                }
                int len = clamp((int)module->params[MF::LEN_PARAM].getValue(), 1, 8);
                snprintf(info, sizeof info, "%sSTEP %d/%d", ext ? "" : "INT  ", module->step + 1, len);
                snprintf(strips, sizeof strips, "%c %s", 'A' + module->bank(), kBankName[module->bank()]);
            }
            nvgFontFaceId(args.vg, font->handle);
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFontSize(args.vg, bigFont);
            nvgFillColor(args.vg, amb);
            nvgText(args.vg, 5.f, H * 0.33f, bpm, NULL);
            nvgFontSize(args.vg, smallFont);
            nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, W - 5.f, H * 0.33f, strips, NULL);
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, dim);
            nvgText(args.vg, 5.f, H * 0.74f, info, NULL);
            nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, run ? nvgRGBA(120, 230, 90, 255) : dim);
            nvgText(args.vg, W - 5.f, H * 0.74f, run ? "RUN" : "STOP", NULL);
        }
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.5f, 0.5f, W - 1.f, H - 1.f, 2.f);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 80));
        nvgStroke(args.vg);
    }
};

// The master OLED, as on MATRIX-DUO: MIDI IN, MIDI OUT (click = port menu), BPM / run / step / strip bank,
// then the buttons (oled::Cell) laid over it.
static const float kMfRow = oled::kRow, kMfBtn = 3.f * kMfRow + 1.f, kMfH = kMfBtn + 2.f * oled::kCellH + 0.9f + 1.f;
struct MFMasterDisplay : TransparentWidget {
    MF* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        nvgBeginPath(args.vg);
        for (float y : {kMfRow, 2.f * kMfRow, 3.f * kMfRow}) oled::divider(args, 4.f, mm2px(y), W - 4.f, mm2px(y));
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W, H);
        oled::midiLine(args, module ? &module->midiIn : NULL, "IN", 0.f);
        oled::midiLine(args, module ? &module->midiOut : NULL, "OUT", mm2px(kMfRow));
        char bpm[24] = "INT CLK", info[16] = "1/8", bank[24] = "A PITCH";
        bool run = module ? module->running : true;
        if (module) {
            if (!module->inputs[MF::CLOCK_INPUT].isConnected()) snprintf(bpm, sizeof bpm, "INT CLK");
            else if (module->clockPeriod > 0.f && module->sinceClock < 2.f) snprintf(bpm, sizeof bpm, "%.1f BPM", 60.f / (module->clockPeriod * 4.f));
            else snprintf(bpm, sizeof bpm, "--- BPM");
            snprintf(info, sizeof info, "%d/%d", module->step + 1, clamp((int)module->params[MF::LEN_PARAM].getValue(), 1, 8));
            snprintf(bank, sizeof bank, "%c %s", 'A' + module->bank(), kBankName[module->bank()]);
        }
        const float yi = mm2px(2.5f * kMfRow);
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
        if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT || e.pos.y >= mm2px(2.f * kMfRow)) return;
        if (module) {
            if (e.pos.y < mm2px(kMfRow)) oled::midiMenu(&module->midiIn, "MIDI IN port");
            else oled::midiMenu(&module->midiOut, "MIDI OUT port");
        }
        e.consume(this);
    }
};

struct SynTHBloKMatrixForgeWidget : ModuleWidget {
    void onButton(const ButtonEvent& e) override {   // clicking the module hands it the JAM
        MF* m = getModule<MF>();
        if (m && e.action == GLFW_PRESS && m->jam.wasEnabled) jamown::claim(m->id);
        ModuleWidget::onButton(e);
    }
    SynTHBloKMatrixForgeWidget(MF* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKMatrixForge.svg")));
        addChild(songlink::RowBadge::create(box.size, module));   // the Composer row conducting it
        using namespace rack::componentlibrary;
        const float PW = 233.68f;   // 46 HP
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(PW - 3.5f, 3.5f))));
        // bottom screws (Nov 2026: every module has four, like the hardware)
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(3.5f, 125.f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(PW - 3.5f, 125.f))));
        MFTitle* title = createWidget<MFTitle>(mm2px(Vec(8.5f, 0.f)));
        title->box.size = mm2px(Vec(60.f, 7.f));
        addChild(title);

        auto label = [&](float cx, float cy, const char* text) {
            JLabel* l = createWidget<JLabel>(mm2px(Vec(cx - 12.f, cy - 2.f)));
            l->box.size = mm2px(Vec(24.f, 4.f)); l->text = text;
            addChild(l);
        };

        // ---------------- left: MATRIX-8, laid out like the JAM ----------------
        auto colX = [](int c) { return 4.9f + 7.4f * c; };
        // sequencer jack LEDs: row triggers in the pattern page's colour, strip CVs in the strip bank's (like the strips)
        auto pageLed = [=](JackLed* l) { l->color = [=]() { return kPatOnColor[module ? module->curPat() : 0]; }; return l; };
        auto bankLed = [=](JackLed* l) { l->color = [=]() { return kPatOnColor[module ? module->bank() : 0]; }; return l; };
        for (int p = 0; p < 8; p++) {
            MFPatBtn* pb = createWidget<MFPatBtn>(mm2px(Vec(colX(p), 10.5f)));
            pb->box.size = mm2px(Vec(6.6f, 6.6f)); pb->module = module; pb->p = p;
            addChild(pb);
        }
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                MFPad* p = createWidget<MFPad>(mm2px(Vec(colX(c), 19.5f + 7.4f * r)));
                p->box.size = mm2px(Vec(6.6f, 6.6f)); p->module = module; p->r = r; p->c = c;
                addChild(p);
            }
        addParam(JRandBtn::create(69.6f, 10.5f, module, MF::RAND_PARAM));   // after pattern 8, over the OUT jacks
        for (int r = 0; r < 8; r++)
            { addOutput(createOutputCentered<SmallJack>(mm2px(Vec(69.6f, 22.8f + 7.4f * r)), module, MF::TRIG_OUTPUT + r));
              addChild(pageLed(JackLed::create(Vec(73.5f, 22.8f + 7.4f * r), module, MF::TRIG_OUTPUT + r, true))); }
        for (int i = 0; i < 8; i++) {
            addOutput(createOutputCentered<SmallJack>(mm2px(Vec(colX(i) + 3.3f, 81.6f)), module, MF::STRIP_OUTPUT + i));
            addChild(bankLed(JackLed::create(Vec(colX(i) + 7.f, 81.6f), module, MF::STRIP_OUTPUT + i, true)));   // right of the jack
            MFBankSq* q = createWidget<MFBankSq>(mm2px(Vec(colX(i), 85.5f)));
            q->box.size = mm2px(Vec(6.6f, 6.6f)); q->module = module; q->b = i;
            addChild(q);
            MFStrip* s = createWidget<MFStrip>(mm2px(Vec(colX(i) + 0.9f, 93.5f)));
            s->box.size = mm2px(Vec(4.8f, 30.f)); s->module = module; s->idx = i;
            addChild(s);
        }
        label(69.6f, 81.6f, "LOOP");
        JLoopArrow<MF>* up = createWidget<JLoopArrow<MF>>(mm2px(Vec(66.1f, 85.5f)));
        up->box.size = mm2px(Vec(7.f, 6.6f)); up->module = module; up->dir = 1;
        addChild(up);
        JLoopDisplay<MF>* ld = createWidget<JLoopDisplay<MF>>(mm2px(Vec(66.1f, 93.5f)));
        ld->box.size = mm2px(Vec(7.f, 22.5f)); ld->module = module;
        addChild(ld);
        JLoopArrow<MF>* dn = createWidget<JLoopArrow<MF>>(mm2px(Vec(66.1f, 117.5f)));
        dn->box.size = mm2px(Vec(7.f, 6.f)); dn->module = module; dn->dir = -1;
        addChild(dn);

        // ---------------- far right: master (as on MATRIX-DUO): MIDI OLED, voice sound, MASTER, every jack ----------------
        const float RX = 182.88f, RW = 45.6f, RC = RX + RW * 0.5f;
        MFMasterDisplay* md = createWidget<MFMasterDisplay>(mm2px(Vec(RX, 10.5f)));
        md->box.size = mm2px(Vec(RW, kMfH)); md->module = module;
        addChild(md);
        auto cell = [&](float x, float y, float w, float h, int param, const char* lab) -> oled::Cell* {
            oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(x, y)), module, param);
            c->box.size = mm2px(Vec(w, h)); c->module = module; c->label = lab;
            addParam(c);
            return c;
        };
        // MIDI on / off, right of the IN / OUT lines
        oled::Cell* mc = cell(RX + RW - 10.5f, 11.2f, 9.f, 2.f * kMfRow - 1.4f, MF::MIDI_ON_PARAM, "MIDI");
        mc->latch = true; mc->green = true;
        // row 1: RUN | RESET | CLEAR, row 2: LEARN | +-5V
        struct CellDef { const char* label; int param; bool latch, green; int jack; };
        const CellDef cells[5] = {
            {"RUN", MF::RUN_PARAM, true, true, MF::RUN_INPUT},
            {"RESET", MF::RESET_PARAM, false, false, MF::RESET_INPUT},
            {"CLEAR", MF::CLEAR_PARAM, false, false, -1},
            {"LEARN", MF::LEARN_PARAM, true, false, -1},
            {"\xC2\xB1" "5V", MF::RANGE_PARAM, true, false, -1}};
        const float CX = RX + 2.f, CW = RW - 4.f, CG = 1.f;
        for (int i = 0; i < 5; i++) {
            int row = i < 3 ? 0 : 1, n = row ? 2 : 3, k = row ? i - 3 : i;
            float w = (CW - CG * (n - 1)) / n;
            oled::Cell* c = cell(CX + k * (w + CG), 10.5f + kMfBtn + row * (oled::kCellH + 0.9f), w, oled::kCellH, cells[i].param, cells[i].label);
            c->latch = cells[i].latch; c->green = cells[i].green; c->jack = cells[i].jack;
        }

        // Voices sound section: FILTER 2 / ENVELOPE / DELAY MASTER SPACE, as on MATRIX-8.
        auto col4 = [&](int i) { return RX + RW * (i + 0.5f) / 4.f; };
        auto smallKnob = [&](float x, float y, int param, const char* name) {
            addChild(KnobTicks::create(Vec(x, y)));
            JKnobRing<MF>* fr = createWidget<JKnobRing<MF>>(mm2px(Vec(x - 5.5f, y - 5.5f)));
            fr->box.size = mm2px(Vec(11.f, 11.f));
            fr->module = module; fr->param = param;
            addChild(fr);
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y)), module, param));
            label(x, y + 7.f, name);
        };
        const int F2[4] = {MF::F2_CUTOFF_PARAM, MF::F2_RES_PARAM, MF::F2_ENV_PARAM, MF::F2_MODE_PARAM};
        const char* F2N[4] = {"CUTOFF", "RES", "ENV", "MODE"};
        const int EV[4] = {MF::ENV_A_PARAM, MF::ENV_D_PARAM, MF::ENV_S_PARAM, MF::ENV_R_PARAM};
        const char* EVN[4] = {"ATK", "DEC", "SUS", "REL"};
        for (int i = 0; i < 4; i++) {
            smallKnob(col4(i), 45.f, F2[i], F2N[i]);
            smallKnob(col4(i), 60.f, EV[i], EVN[i]);
        }
        smallKnob(col4(0), 79.f, MF::M8_DELAY_PARAM, "DELAY");
        smallKnob(col4(3), 79.f, MF::SPACE_PARAM, "SPACE");
        const float KY = 79.f;
        addChild(KnobTicks::create(Vec(RC, KY), 19.f / 11.f));
        JKnobRing<MF>* ring = createWidgetCentered<JKnobRing<MF>>(mm2px(Vec(RC, KY)));
        ring->box.size = mm2px(Vec(19.f, 19.f));
        ring->module = module;
        addChild(ring);
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(RC, KY)), module, MF::KNOB_PARAM));
        label(RC, KY + 12.f, "MASTER");

        // jacks, two rows of six like MATRIX-DUO's: IN L / R, OPEN, GLITCH, GATE, KNOB; CLK, RST, RUN, OUT L / R
        struct JackDef { const char* name; int id; bool out; };
        const JackDef jk[2][6] = {
            {{"IN L", MF::IN_L_INPUT, false}, {"IN R", MF::IN_R_INPUT, false}, {"OPEN", MF::OPEN_INPUT, false},
             {"GLITCH", MF::GLITCH_INPUT, false}, {"GATE", MF::GATE_INPUT, false}, {"KNOB", MF::KNOB_OUTPUT, true}},
            {{"CLK", MF::CLOCK_INPUT, false}, {"RST", MF::RESET_INPUT, false}, {"RUN", MF::RUN_INPUT, false},
             {NULL, -1, false}, {"OUT L", MF::OUT_L_OUTPUT, true}, {"OUT R", MF::OUT_R_OUTPUT, true}}};
        for (int r = 0; r < 2; r++)
            for (int i = 0; i < 6; i++) {
                const JackDef& d = jk[r][i];
                if (!d.name) continue;
                float x = RX + RW * (i + 0.5f) / 6.f, y = 104.f + 13.f * r;
                label(x, y - 5.5f, d.name);
                if (d.out) addOutput(createOutputCentered<SmallJack>(mm2px(Vec(x, y)), module, d.id));
                else addInput(createInputCentered<SmallJack>(mm2px(Vec(x, y)), module, d.id));
            }

        // ---------------- middle: GRAIN-FORGE as FORGE FX (one OLED + small knobs, as on MATRIX-DUO) ----------------
        forgeFxAddPanel(this, module, NULL, -1, 76.f);
    }
    void appendContextMenu(Menu* menu) override {
        MF* m = getModule<MF>();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        auto indexItem = [&](const std::string& text, int paramId, std::vector<std::string> labels) {
            menu->addChild(createIndexSubmenuItem(text, labels,
                [=]() { return (size_t)(m->params[paramId].getValue() - 1.f); },
                [=](size_t i) { m->params[paramId].setValue((float)i + 1.f); }));
        };
        indexItem("Pattern length", MF::LEN_PARAM, {"1 step", "2 steps", "3 steps", "4 steps", "5 steps", "6 steps", "7 steps", "8 steps"});
        indexItem("JAM matrix page", MF::PAGE_PARAM, {"1", "2", "3", "4", "5", "6"});
        indexItem("Strip bank", MF::BANK_PARAM, {"A", "B", "C", "D", "E", "F", "G", "H"});
        menu->addChild(createMenuItem("Clear every pattern", "", [=]() { for (int p = 0; p < 8; p++) m->clearPattern(p); }));
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolPtrMenuItem("JAM lights match panel colours", "", &m->matchColors));
        menu->addChild(createMenuLabel("Custom JAM LED colours (when not matching)"));
        for (int id : {MF::OFFC_PARAM, MF::ONC_PARAM, MF::HEADC_PARAM, MF::HITC_PARAM, MF::BONC_PARAM, MF::BOFFC_PARAM}) {
            ui::Slider* s = new ui::Slider;
            s->quantity = m->paramQuantities[id];
            s->box.size.x = 220.f;
            menu->addChild(s);
        }
    }
};

} // namespace

Model* modelSynTHBloKMatrixForge = createModel<SynTHBloKMatrixForge, SynTHBloKMatrixForgeWidget>("MatrixForge");
