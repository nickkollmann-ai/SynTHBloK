// SynTHBloK MATRIX-LOOP: an 8-track loop sampler. Every track is MATRIX-ECHO's looper (EchoLooper, Echo_dsp.hpp):
// record / replace, play / stop, play once, undo / redo, clear, all on one beat grid (CLOCK, else the TEMPO knob;
// at FREE the loops run free). A track records mono, or stereo from a 2-channel (poly) cable; its output follows.
// Left: the JAM layout (track buttons 1-8 and RAND, the 8 x 8 grid with each track's loop in eighths along its row:
// a pad plays from that eighth; the track outputs beside the rows; inputs, input monitor buttons and input gain
// strips under the columns, switched to track levels by IN / LVL beside A-H (with MUTE: every input); the selected track's
// value with its meter; beside them CHOP, arrows and display: every loop repeats 1-8 eighths from where it plays).
// Right: the MATRIX-DRUM-style screen: the selected track's header, all 8 loops running at once, the record
// length and scene rows, the track tiles and the transport for the selected track.
#include "plugin.hpp"
#include "OledUi.hpp"
#include "KnobTicks.hpp"
#include "JackLed.hpp"
#include "Echo_dsp.hpp"
#include "Matrix8_ui.hpp"
#include "JamOwner.hpp"
#include "SongLink.hpp"
#include <atomic>
#include <thread>
#include <chrono>

// The Maschine JAM ("JAM Template 4 Rack", as MATRIX-DRUM; the module owns the JAM when last clicked):
//   pads           = the grid: play that track from that eighth (JUMP), or gate that eighth on / off (GATE)
//                    (MUTE held: mute the row's track; SOLO held: play / stop it)
//   scene 1-8      = select the track          group A-H = mute track 1-8 (lit: muted)
//   touch strips   = input gain A-H, or (strips on LVL) track level 1-8        knob = MASTER (push: back to 0 dB)
//   PLAY / REC     = PLAY / REC                CLEAR     = CLEAR      SONG = UNDO / REDO    DUPLICATE = ONCE
//   PREV / NEXT    = the track before / after  UP / DOWN = the track's level +-1 dB
//   LEFT / RIGHT   = record length shorter / longer       TEMPO = count-in    GRID = RAND
// The lights follow: loops dim in their track colour, the playing eighth white, recording red.

namespace {

const int kTracks = 8;
const float kLoopSec = 20.f;     // per track (8 stereo tracks with their undo copies: ~120 MB at 48 kHz)
const float kTempoFree = 39.f;
const char* const kStateName[6] = {"EMPTY", "REC", "PLAY", "REPLACE", "STOP", "ONCE"};
const char* const kClockDivName[3] = {"1/16 (MTX steps)", "1/8", "1/4 (beats)"};
const int kLenN = 7;
const char* const kLenName[kLenN] = {"FREE", "1 BT", "2 BT", "1 BAR", "2 BAR", "4 BAR", "8 BAR"};
const int kLenBeats[kLenN] = {0, 1, 2, 4, 8, 16, 32};
// MON selected (the inputs heard): its button, LED and the screen's amber readouts turn cyan
inline NVGcolor monCyan(int a = 255) { return nvgRGBA(0x3a, 0xd7, 0xff, (unsigned char)a); }
// a track while it monitors: cyan, light to dark across the tracks as their own colours run yellow to red
const NVGcolor kMonRamp[8] = {
    nvgRGB(0xc4, 0xf7, 0xff), nvgRGB(0x96, 0xee, 0xff), nvgRGB(0x6b, 0xe3, 0xff), nvgRGB(0x3a, 0xd7, 0xff),
    nvgRGB(0x1f, 0xbd, 0xf2), nvgRGB(0x12, 0xa0, 0xdd), nvgRGB(0x0c, 0x86, 0xc8), nvgRGB(0x0f, 0x8f, 0xd0)};

// TEMPO: 40 .. 240 BPM, with FREE (no grid: the loops run free) at the bottom
struct LoopTempoQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        float v = getValue();
        return v < kTempoFree + 0.5f ? "Free (no grid)" : string::f("%.0f BPM", v);
    }
};
// LEN: beats to record (with a tempo); 0 = until REC / PLAY again
struct LoopLenQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int v = (int)getValue();
        if (v <= 0) return "Free (record until REC / PLAY)";
        if (v % 4 == 0) return string::f("%d bar%s", v / 4, v == 4 ? "" : "s");
        return string::f("%d beat%s", v, v == 1 ? "" : "s");
    }
};

struct SynTHBloKLoop : Module, songlink::Member {   // MATRIX-SONG picks its scene, plays its loops, mutes it
    enum ParamId { SEL_PARAM, REC_PARAM, PLAY_PARAM, ONCE_PARAM, UNDO_PARAM, CLEAR_PARAM, COUNTIN_PARAM, TEMPO_PARAM,
                   MASTER_PARAM, ENUMS(LEN_PARAM, kTracks), ENUMS(GAIN_PARAM, kTracks), ENUMS(MON_PARAM, kTracks),
                   ENUMS(LEVEL_PARAM, kTracks), MIDI_ON_PARAM, PAGE_PARAM, ENUMS(MUTE_PARAM, kTracks), RUN_PARAM, RESET_PARAM,
                   STRIP_PARAM, IN_MUTE_PARAM, REC_ALL_PARAM, CHOP_PARAM, CLEAR_ALL_PARAM, PLAY_ALL_PARAM, PAD_MODE_PARAM,
                   PARAMS_LEN };
    // REC .. CLEAR jacks: mono = the selected track; poly = channel n plays track n
    enum InputId { ENUMS(IN_INPUT, kTracks), CLOCK_INPUT, REC_INPUT, PLAY_INPUT, ONCE_INPUT, UNDO_INPUT, CLEAR_INPUT,
                   RUN_INPUT, RESET_INPUT, INPUTS_LEN };
    enum OutputId { ENUMS(OUT_OUTPUT, kTracks), MIX_L_OUTPUT, MIX_R_OUTPUT, OUTPUTS_LEN };
    enum LightId { LIGHTS_LEN };

    EchoLooper loop[kTracks];
    bool stereo[kTracks] = {};   // the track holds a stereo recording
    int prevSt[kTracks] = {};
    float gainSm[kTracks], levelSm[kTracks], muteSm[kTracks], inPk[kTracks] = {}, outPk[kTracks] = {}, playPk[kTracks] = {};
    // RUN stops every playing loop (and starts them again, on the beat); RESET plays them all from the top
    bool running = true, wasPlaying[kTracks] = {}, restartReq = false;
    float resetFlash = 0.f;
    dsp::SchmittTrigger runTrig, rstTrig, rstBtnTrig;
    bool btnPrev[8] = {};
    dsp::SchmittTrigger clockTrig, trig[5][kTracks];
    // pads: a press jumps the track to that eighth (tempo-locked loops: on the next beat); RAND: at each eighth
    // the track may play another eighth instead (as many whole beats away, so it stays on the grid)
    std::atomic<int> padReq{-1};   // track * 8 + eighth, from the UI
    int pend[kTracks], lastSlice[kTracks];
    // GATE (the pads' mode): each track's eighths gated off (bit c: silent there) and accented (louder), as the
    // playhead's column; a clear sets them back (all on, none accented)
    int gateOff[kTracks] = {}, accent[kTracks] = {};
    float gateSm[kTracks];
    bool randOn[kTracks] = {};   // the lane has a RAND effect (its mode); it plays while randAll is on
    bool randAll = false;        // RAND (the button, the JAM's GRID): every lane's effect on / off
    // RAND's mode per track (what a random step does) and its amount (LOW / MED / HIGH: how often)
    enum RandMode { RM_8, RM_16, RM_32, RM_64, RM_STUTTER, RM_BEAT, RM_BAR, RM_ORDER, RM_LEN };
    int randMode[kTracks] = {}, randAmt[kTracks];
    int rReps[kTracks] = {}, rBack[kTracks] = {};   // a roll's repeats to go; slices to jump back by, to end it
    // each scene's own RAND set (the lanes' effects), loaded with the scene; [8]: the set with no scene selected
    bool sRandUsed[9] = {}, sRandOn[9][kTracks] = {};
    int sRandMode[9][kTracks] = {}, sRandAmt[9][kTracks] = {};
    int clockDiv = 0, clockCount = 0;   // clock pulse = 1/16, 1/8 or 1/4
    float sinceClock = 10.f, clockPeriod = 0.f, sinceBeatClk = 10.f, clkBeat = 0.f, beatPhase = 0.f;
    float masterSm = 1.f;
    // for the display
    float beatNow = 0.f;
    int source = 0;   // 0 free, 1 the TEMPO knob, 2 clock
    float blink = 0.f, recFlash = 0.f, playFlash = 0.f, onceFlash = 0.f, allRecFlash = 0.f, allPlayFlash = 0.f;
    // scenes U1-U8: which tracks played as each was stored (bit t; kept, no longer recalled); a click asks the
    // audio thread to switch
    int sceneMask[8] = {};
    bool sceneUsed[8] = {};
    int curScene = -1;
    // CHOP: a chopped track repeats rollLen samples from rollStart; ghost is where it would be unchopped (it
    // goes on in time, and the loop carries on from there when the chop ends)
    int ghost[kTracks] = {}, rollStart[kTracks] = {}, rollLen[kTracks] = {};
    bool chopped[kTracks] = {};
    int dubN[kTracks] = {};   // samples written by the replace now running
    bool takeClosed = false;  // a take closed: back to LVL once none is recording or waiting to
    bool replacing[kTracks] = {};   // a playing loop waits for its new take (a cancel lets it play on)
    int waveZoom = 1;   // the screen's lanes: x1 (whole loops) .. x32 (following the playhead); UI only
    // the lanes' VIEW (UI only): 0 smooth wave, 1 static spectrum (the whole loop's, measured as a take closes),
    // 2 live spectrum, 3 live scope; LVL and MON each keep their own ([1]: MON)
    int laneViews[2] = {};
    int& laneView() { return laneViews[hearIn() ? 1 : 0]; }
    // what leaves each track (mono), the last kScope samples, for the live views
    enum { kScope = 2048 };
    float scope[kTracks][kScope] = {};
    std::atomic<int> scopeW{0};
    int takeGen[kTracks] = {};   // counts each new loop (a take closes, a clear): the static spectrum is measured again
    bool doneMon[kTracks] = {}, wasHear = false;   // a take closed while MON was selected (cleared as MON is selected)
    bool autoMon = true;   // a closed take turns its track's input monitor off; REC turns it back on
    float listenSm[kTracks] = {};   // 1 = the inputs heard in place of the loops (MON selected)
    bool storeArm = false;
    std::atomic<int> sceneReq{-1};
    // the JAM
    midi::InputQueue midiIn;
    midi::Output midiOut;
    jamown::Link jam;
    bool slowSend = false, muteHeld = false, soloHeld = false;
    int lastLed[kNumLed];
    int ledScan = 0, lastPage = -1, stripPage = 0, lastStripPage = -1, knobCC = kKnobFirstCC;
    float tokens = 4.f, resync = 0.f;

    SynTHBloKLoop() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        songlink::join(this, this, "LOOP", 8, true);
        configParam(SEL_PARAM, 0.f, kTracks - 1, 0.f, "Selected track", "", 0.f, 1.f, 1.f);
        getParamQuantity(SEL_PARAM)->snapEnabled = true;
        configButton(REC_PARAM, "Record / replace (selected track)");
        configButton(PLAY_PARAM, "Play / stop (selected track)");
        configButton(ONCE_PARAM, "Play once (selected track)");
        configButton(UNDO_PARAM, "Undo / redo the last replace (selected track)");
        configButton(CLEAR_PARAM, "Clear (selected track)");
        configSwitch(COUNTIN_PARAM, 0.f, 4.f, 0.f, "Count-in before a new take (with a tempo)",
                     {"Off (starts on the next beat)", "1 beat", "2 beats", "3 beats", "4 beats"});
        configParam<LoopTempoQuantity>(TEMPO_PARAM, kTempoFree, 240.f, 120.f, "Tempo (with no clock)");
        getParamQuantity(TEMPO_PARAM)->snapEnabled = true;
        configParam(MASTER_PARAM, 0.f, 2.f, 1.f, "Master output (MIX L / R)", " dB", -10.f, 20.f);
        for (int t = 0; t < kTracks; t++) {
            configParam<LoopLenQuantity>(LEN_PARAM + t, 0.f, 32.f, 0.f, string::f("Track %d record length", t + 1));
            getParamQuantity(LEN_PARAM + t)->snapEnabled = true;
            configParam(GAIN_PARAM + t, 0.f, 2.f, 1.f, string::f("Input %c gain", 'A' + t), " dB", -10.f, 20.f);
            configSwitch(MON_PARAM + t, 0.f, 1.f, 1.f, string::f("Input %c monitor (heard at OUT %d and MIX)", 'A' + t, t + 1), {"Off", "On"});
            configInput(IN_INPUT + t, string::f("Track %d (input %c; 2 channels: stereo)", t + 1, 'A' + t));
            configOutput(OUT_OUTPUT + t, string::f("Track %d (2 channels when stereo)", t + 1));
            configParam(LEVEL_PARAM + t, 0.f, 2.f, 1.f, string::f("Track %d level", t + 1), " dB", -10.f, 20.f);
            configSwitch(MUTE_PARAM + t, 0.f, 1.f, 0.f, string::f("Track %d mute (button %c)", t + 1, 'A' + t), {"Off", "Muted"});
            getParamQuantity(MUTE_PARAM + t)->randomizeEnabled = false;
            gainSm[t] = levelSm[t] = muteSm[t] = gateSm[t] = 1.f;
            pend[t] = lastSlice[t] = -1;
            randAmt[t] = 1;
        }
        static const char* const kTrigName[5] = {"Record / replace", "Play / stop", "Play once", "Undo / redo", "Clear"};
        for (int i = 0; i < 5; i++)
            configInput(REC_INPUT + i, string::f("%s trigger (mono: the selected track; poly: channel n = track n)", kTrigName[i]));
        for (int id : {SEL_PARAM, COUNTIN_PARAM, TEMPO_PARAM}) getParamQuantity(id)->randomizeEnabled = false;
        for (int t = 0; t < kTracks; t++) {
            getParamQuantity(LEN_PARAM + t)->randomizeEnabled = false;
            getParamQuantity(MON_PARAM + t)->randomizeEnabled = false;
        }
        configSwitch(MIDI_ON_PARAM, 0.f, 1.f, 1.f, "JAM MIDI (off = ignore the JAM, its lights dark)", {"Off", "On"});
        configParam(PAGE_PARAM, 1.f, 6.f, 1.f, "JAM matrix page (must match the JAM)");
        getParamQuantity(PAGE_PARAM)->snapEnabled = true;
        getParamQuantity(MIDI_ON_PARAM)->randomizeEnabled = false;
        getParamQuantity(PAGE_PARAM)->randomizeEnabled = false;
        for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
        configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run: stop / start every loop (a trigger at RUN toggles it)", {"Stopped", "Running"});
        configButton(RESET_PARAM, "Reset: every loop from the top (a trigger at RESET does too)");
        getParamQuantity(RUN_PARAM)->randomizeEnabled = false;
        configSwitch(STRIP_PARAM, 0.f, 1.f, 1.f, "Strips and audio", {"MON: input gain, hear the inputs", "LVL: track level, hear the loops"});
        getParamQuantity(STRIP_PARAM)->randomizeEnabled = false;
        configButton(REC_ALL_PARAM, "Record / replace on all 8 tracks at once");
        configButton(CLEAR_ALL_PARAM, "Clear all 8 tracks at once");
        configButton(PLAY_ALL_PARAM, "Play every stopped loop (all playing: stop them all)");
        configParam(CHOP_PARAM, 1.f, 8.f, 8.f, "Chop: every loop repeats this many eighths from where it plays (8: whole)", "/8");
        getParamQuantity(CHOP_PARAM)->snapEnabled = true;
        getParamQuantity(CHOP_PARAM)->randomizeEnabled = false;
        configSwitch(PAD_MODE_PARAM, 0.f, 1.f, 0.f, "Pads",
                     {"JUMP: a pad plays from that eighth", "GATE: a pad gates that eighth on / off (right-click: accent)"});
        getParamQuantity(PAD_MODE_PARAM)->randomizeEnabled = false;
        configSwitch(IN_MUTE_PARAM, 0.f, 1.f, 0.f, "(unused: was the input mute)", {"Off", "On"});
        getParamQuantity(IN_MUTE_PARAM)->randomizeEnabled = false;
        configInput(RUN_INPUT, "Run (trigger: toggles RUN)");
        configInput(RESET_INPUT, "Reset (trigger: every loop from the top)");
        configInput(CLOCK_INPUT, "Clock");
        configOutput(MIX_L_OUTPUT, "Mix left");
        configOutput(MIX_R_OUTPUT, "Mix right");
        for (int t = 0; t < kTracks; t++) {
            loop[t].init(APP->engine->getSampleRate(), kLoopSec);
            loop[t].replace = true;   // REC on a playing loop writes over it (as MATRIX-ECHO)
        }
    }

    // a new sample rate: each loop is kept, resampled to it (a take still recording is lost)
    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        for (int t = 0; t < kTracks; t++) {
            EchoLooper& lp = loop[t];
            if (e.sampleRate == lp.sr) continue;
            const bool keep = lp.len > 0 && lp.st != EchoLooper::REC, wasPlaying = lp.playing();
            const int n = lp.len, beats = lp.beats, loopBeats = lp.loopBeats, pl = lp.playLen, layers = lp.layers;
            const double beatLen = lp.beatLen;
            const float oldSr = lp.sr;
            std::vector<float> L, R;
            L.swap(lp.l); R.swap(lp.r);
            lp.init(e.sampleRate, kLoopSec);
            stereo[t] = keep && stereo[t];
            if (keep) {
                restoreLoop(t, L.data(), R.data(), n, oldSr, stereo[t], beats, loopBeats, pl, beatLen, layers);
                if (wasPlaying) lp.st = EchoLooper::PLAY;
            }
        }
    }

    // ---- the loops' audio, kept with the patch: a file per track in its storage folder, written as the patch
    // is saved (Rack's autosave too, so as Rack closes), read back as it loads ----
    enum { kKeyN = 7 };
    int savedKey[kTracks][kKeyN];   // the loop each file holds (len -1: none written yet)
    int loadPlay = -1;              // the tracks playing as the patch was saved (from its JSON; -1: none given)
    bool audioLoaded = false;
    void loopKey(int t, int* k) {
        const EchoLooper& lp = loop[t];
        const int v[kKeyN] = {takeGen[t], lp.layers, lp.undone ? 1 : 0, lp.len, lp.beats, lp.loopBeats, lp.playLen};
        std::copy(v, v + kKeyN, k);
    }
    std::string loopPath(const std::string& dir, int t) { return system::join(dir, string::f("track%d.loop", t + 1)); }
    // track t's loop from n samples at srcSr (resampled to the engine's), stopped
    void restoreLoop(int t, const float* L, const float* R, int n, float srcSr, bool st2, int beats, int loopBeats,
                     int pl, double beatLen, int layers) {
        EchoLooper& lp = loop[t];
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
        stereo[t] = st2; prevSt[t] = EchoLooper::STOP; takeGen[t]++;
    }
    // the loops playing as the patch was saved play again (once both the audio and the JSON are in)
    // (only as the patch loads: a later preset or undo leaves the transport alone)
    bool playRestored = false;
    void applyLoadPlay() {
        if (!audioLoaded || loadPlay < 0 || playRestored) return;
        for (int t = 0; t < kTracks; t++)
            if ((loadPlay >> t & 1) && loop[t].st == EchoLooper::STOP) loop[t].st = EchoLooper::PLAY;
        loadPlay = -1;
        playRestored = true;
    }
    // a file: "SBLP", version, sample rate, samples, stereo, beats, loop beats, play length, layers, beat length
    // (double), then the left samples and (stereo) the right
    void onAdd(const AddEvent& e) override {
        for (int t = 0; t < kTracks; t++) savedKey[t][3] = -1;
        std::string dir;
        try { dir = getPatchStorageDirectory(); } catch (Exception&) { return; }
        for (int t = 0; t < kTracks; t++) {
            const std::string path = loopPath(dir, t);
            if (!system::exists(path)) continue;
            std::vector<uint8_t> d;
            try { d = system::readFile(path); } catch (Exception&) { continue; }
            const size_t kHead = 4 + 8 * 4 + 8;
            if (d.size() < kHead || std::memcmp(d.data(), "SBLP", 4) != 0) continue;
            int32_t h[8];
            float fsr;
            double beatLen;
            std::memcpy(h, d.data() + 4, sizeof h);
            std::memcpy(&fsr, &h[1], 4);
            std::memcpy(&beatLen, d.data() + 4 + sizeof h, 8);
            const int n = h[2];
            const bool st2 = h[3] != 0;
            if (h[0] != 1 || n < 1 || !(fsr > 1000.f) || d.size() < kHead + (size_t)n * 4 * (st2 ? 2 : 1)) continue;
            std::vector<float> L((size_t)n), R;
            std::memcpy(L.data(), d.data() + kHead, (size_t)n * 4);
            if (st2) { R.resize((size_t)n); std::memcpy(R.data(), d.data() + kHead + (size_t)n * 4, (size_t)n * 4); }
            restoreLoop(t, L.data(), st2 ? R.data() : L.data(), n, fsr, st2, h[4], h[5], h[6], beatLen, h[7]);
            if (fsr == loop[t].sr) loopKey(t, savedKey[t]);   // as in the file: no need to write it again
        }
        audioLoaded = true;
        applyLoadPlay();
    }
    // write each loop that changed since its file was written (a take still recording keeps the last one)
    void onSave(const SaveEvent& e) override {
        for (int t = 0; t < kTracks; t++) {
            EchoLooper& lp = loop[t];
            if (lp.st == EchoLooper::REC || lp.st == EchoLooper::DUB) continue;
            int key[kKeyN];
            loopKey(t, key);
            if (std::equal(key, key + kKeyN, savedKey[t])) continue;
            try {
                const std::string path = loopPath(lp.len > 0 ? createPatchStorageDirectory() : getPatchStorageDirectory(), t);
                if (lp.len <= 0) { if (system::exists(path)) system::remove(path); }
                else {
                    const bool st2 = stereo[t];
                    const size_t n = (size_t)lp.len;
                    float fsr = lp.sr;
                    int32_t h[8] = {1, 0, lp.len, st2 ? 1 : 0, lp.beats, lp.loopBeats, lp.playLen, lp.layers};
                    std::memcpy(&h[1], &fsr, 4);
                    std::vector<uint8_t> d(4 + sizeof h + 8 + n * 4 * (st2 ? 2 : 1));
                    std::memcpy(d.data(), "SBLP", 4);
                    std::memcpy(d.data() + 4, h, sizeof h);
                    std::memcpy(d.data() + 4 + sizeof h, &lp.beatLen, 8);
                    std::memcpy(d.data() + 4 + sizeof h + 8, lp.l.data(), n * 4);
                    if (st2) std::memcpy(d.data() + 4 + sizeof h + 8 + n * 4, lp.r.data(), n * 4);
                    system::writeFile(path, d);
                }
                std::copy(key, key + kKeyN, savedKey[t]);
            } catch (Exception&) {}
        }
    }
    void onReset() override {
        for (int t = 0; t < kTracks; t++) {
            loop[t].clear(); stereo[t] = false; randOn[t] = false; pend[t] = -1; randMode[t] = RM_8; randAmt[t] = 1;
            gateOff[t] = accent[t] = 0;
        }
        randAll = false;
        for (int k = 0; k < 8; k++) { sceneMask[k] = 0; sceneUsed[k] = false; }
        curScene = -1; storeArm = false;
        for (int k = 0; k < 9; k++) sRandUsed[k] = false;
    }

    int sel() { return clamp((int)std::round(params[SEL_PARAM].getValue()), 0, kTracks - 1); }
    // the playing length of a track's loop (0: none)
    int playLen(int t) const {
        const EchoLooper& lp = loop[t];
        return lp.playLen > 0 ? std::min(lp.playLen, lp.len) : lp.len;
    }
    bool edge(int b, int param) {
        bool v = params[param].getValue() > 0.5f, e = v && !btnPrev[b];
        btnPrev[b] = v;
        return e;
    }
    // GATE (the pads' mode): a pad gates its eighth on / off, or (acc: right-click) accents it (an accented
    // eighth plays)
    bool gateMode() { return params[PAD_MODE_PARAM].getValue() > 0.5f; }
    void gatePad(int r, int c, bool acc) {
        const int bit = 1 << c;
        if (!acc) {
            gateOff[r] ^= bit;
            if (gateOff[r] & bit) accent[r] &= ~bit;
        } else {
            accent[r] ^= bit;
            if (accent[r] & bit) gateOff[r] &= ~bit;
        }
    }
    // store to scene k: it is used (and selected) from now, with the tracks playing (or armed to start) noted
    void storeScene(int k) {
        int m = 0;
        for (int t = 0; t < kTracks; t++) {
            const EchoLooper& lp = loop[t];
            bool on = (lp.playing() && lp.arm != EchoLooper::A_STOP) || lp.arm == EchoLooper::A_PLAY;
            if (on) m |= 1 << t;
        }
        sceneMask[k] = m; sceneUsed[k] = true; curScene = k;
        loadRand(k);   // its own RAND set: blank until RAND on the screen deals it one
    }
    // a scene's RAND set (k < 0: the one with no scene): the lanes' effects now stored in it / loaded from it
    // (none stored: every lane blank). RAND off keeps them, so RAND on again plays the same.
    void saveRand(int k) {
        if (k < 0 || k > 7) k = 8;
        sRandUsed[k] = true;
        for (int t = 0; t < kTracks; t++) { sRandOn[k][t] = randOn[t]; sRandMode[k][t] = randMode[t]; sRandAmt[k][t] = randAmt[t]; }
    }
    void loadRand(int k) {
        if (k < 0 || k > 7) k = 8;
        bool has = sRandUsed[k];
        for (int t = 0; t < kTracks; t++) {
            randOn[t] = has && sRandOn[k][t];
            if (has) { randMode[t] = sRandMode[k][t]; randAmt[t] = sRandAmt[k][t]; }
            rReps[t] = rBack[t] = 0;
            lastSlice[t] = -1;
        }
    }
    // a lane's effect changed (its box, its menu, a deal): the selected scene keeps it
    void randChanged() { saveRand(curScene); }
    // select a scene (stored or not): its own RAND set (blank until one is dealt to it). It never starts or
    // stops a track: what plays is left as it is.
    void recallScene(int k) {
        curScene = k;
        loadRand(k);
    }
    // REC / PLAY / ONCE / UNDO / CLEAR on track t
    void act(int t, int b) {
        EchoLooper& lp = loop[t];
        switch (b) {
            case 0:
                // closing a take on the beat grid: within half a beat after an edge, close at that edge now (and
                // play on from it), rather than wait for the next one
                if (lp.st == EchoLooper::REC && lp.locked && lp.arm == EchoLooper::A_NONE && lp.curBeat > 0.f
                    && lp.sinceBeat <= 0.5f * lp.curBeat && lp.pos - lp.sinceBeat >= 0.5f * lp.curBeat) {
                    lp.arm = EchoLooper::A_CLOSE;
                    lp.fire(lp.sinceBeat);
                    break;
                }
            {
                bool starting = lp.st != EchoLooper::REC && lp.st != EchoLooper::DUB;
                bool cancel = lp.arm == EchoLooper::A_REC || lp.arm == EchoLooper::A_DUB;
                // a replace: REC on a loop records a new take of LEN on the grid, the same as a first take
                // (all of them start together under ALL 8, each closes at its own LEN)
                if (starting && !cancel && lp.len > 0 && lp.st != EchoLooper::EMPTY) {
                    replacing[t] = lp.st != EchoLooper::STOP;
                    lp.st = EchoLooper::STOP;
                }
                // auto monitor: a take about to start opens its monitor and selects MON (the inputs heard)
                if (autoMon && starting && !cancel) {
                    params[MON_PARAM + t].setValue(1.f);
                    params[STRIP_PARAM].setValue(0.f);
                }
                lp.press(EchoLooper::B_REC);
                if (cancel) {   // a second press called it off: a replaced loop plays on, and back to LVL when idle
                    if (replacing[t] && lp.st == EchoLooper::STOP) lp.press(EchoLooper::B_PLAY);
                    replacing[t] = false;
                    if (autoMon) takeClosed = true;
                }
                break;
            }
            case 1: lp.press(EchoLooper::B_PLAY); break;
            case 2: lp.press(EchoLooper::B_ONCE); break;
            case 3: lp.undo(); break;
            case 4: lp.clear(); stereo[t] = false; pend[t] = -1; break;
        }
    }
    // the pads and RAND move a loop that holds audio and is not being recorded
    bool movable(int t) const {
        const EchoLooper& lp = loop[t];
        return lp.len > 0 && (lp.st == EchoLooper::PLAY || lp.st == EchoLooper::ONCE || lp.st == EchoLooper::STOP);
    }
    // play track t from eighth c now (a stopped loop starts)
    void jumpTo(int t, int c) {
        EchoLooper& lp = loop[t];
        int L = playLen(t);
        if (L <= 0) return;
        if (lp.locked && lp.beats > 0 && lp.beatLen > 0.0) {   // on the beat grid: the eighth's first beat
            int b = c * std::max(1, lp.loopBeats) / 8;
            lp.beatIdx = b;
            lp.jump(std::min(L - 1, (int)std::llround(b * lp.beatLen)));
        } else lp.jump((int)((int64_t)c * L / 8));
        if (lp.st == EchoLooper::STOP) { lp.st = EchoLooper::PLAY; lp.arm = EchoLooper::A_NONE; }
        lastSlice[t] = randN(t) == 8 ? c : -1;
        chopAnchor(t);
    }
    // CHOP: track t now plays from where it is: the slice it repeats is the one around it (pads, RAND)
    void chopAnchor(int t) {
        ghost[t] = loop[t].pos;
        if (chopped[t]) chopped[t] = false;   // re-anchored by chopStep()
    }
    // CHOP, before process(): keep a chopped track inside its slice; at the chop's end go on from the ghost
    void chopStep(int t, int n) {
        EchoLooper& lp = loop[t];
        const int L = playLen(t);
        bool want = n < 8 && L > 0 && (lp.st == EchoLooper::PLAY || lp.st == EchoLooper::ONCE);
        if (!want) {
            if (chopped[t] && L > 0 && lp.playing() && lp.pos != ghost[t] % L) lp.jump(ghost[t] % L);
            chopped[t] = false;
            return;
        }
        int rl = std::max(1, (int)((int64_t)L * n / 8));
        if (!chopped[t] || rl != rollLen[t]) {   // (re)start: the slice of this length the track is in now
            ghost[t] %= L;
            rollLen[t] = rl;
            rollStart[t] = std::min(ghost[t] / rl * rl, L - rl);
            chopped[t] = true;
        }
        int d = ((ghost[t] - rollStart[t]) % rl + rl) % rl;
        int to = rollStart[t] + d;
        if (lp.pos != to) lp.jump(to);
    }
    // RAND: the playhead just entered eighth sl; maybe play another one (whole beats away when on the grid)
    // the slices RAND steps on (n of the loop): eighths .. sixty-fourths, beats or bars
    int randN(int t) const {
        const EchoLooper& lp = loop[t];
        int nb = lp.beats > 0 ? std::max(1, lp.loopBeats) : 4;
        switch (randMode[t]) {
            case RM_16: case RM_STUTTER: return 16;
            case RM_32: return 32;
            case RM_64: return 64;
            case RM_BEAT: return std::max(2, std::min(64, nb));
            case RM_BAR: return std::max(2, nb / 4);
            default: return 8;
        }
    }
    // at the start of slice sl (of n), as the track plays on into it. Every mode but ORDER puts something in for
    // a moment and then goes back to where the loop would be, so the phrase stays where it was:
    //   1/8, 1/16: maybe another eighth / sixteenth in the same place of the beat (on-beat for on-beat, off for off),
    //   for one slice; 1/32, 1/64: maybe, from an eighth, a roll of its first 32nd / 64th to the next eighth;
    //   STUTTER: maybe the sixteenth just played again; BEAT: maybe the beat two away (1 <-> 3, 2 <-> 4) for a beat;
    //   BAR: maybe another bar for a bar; ORDER: always the eighth before the last (the loop backwards).
    // On the beat grid a jump of whole beats moves the beat count with it; a shorter one lasts at most to the next
    // beat, which puts the loop back in place (and ends the variation), so every mode stays in time.
    void randStep(int t, int sl, int n) {
        static const float kAmt[3] = {0.18f, 0.35f, 0.6f};
        EchoLooper& lp = loop[t];
        const int m = randMode[t];
        int d = 0;
        if (rReps[t] > 0) { d = -1; rBack[t]++; rReps[t]--; }      // a roll: the same slice again
        else if (rBack[t] != 0) { d = rBack[t]; rBack[t] = 0; }    // back to where the loop would be
        else {
            if (!randAll || !randOn[t]) return;
            if (m != RM_ORDER && random::uniform() >= kAmt[clamp(randAmt[t], 0, 2)]) return;
            auto pick = [&](int step) {   // another slice, a whole number of steps away
                int k = n / step;
                if (k < 2) return 0;
                int r = 1 + std::min(k - 2, (int)(random::uniform() * (k - 1)));
                return r * step;
            };
            switch (m) {
                case RM_8: case RM_16: d = pick(2); break;
                case RM_BAR: d = pick(1); break;
                case RM_32: case RM_64:
                    if (sl % (n / 8) == 0) rReps[t] = n / 8 - 1;   // roll from here to the next eighth
                    return;
                case RM_STUTTER: d = -1; break;
                case RM_BEAT: d = (n >= 4 ? 2 : 1) * (random::uniform() < 0.5f ? 1 : -1); break;
                case RM_ORDER: d = -2; break;
            }
            if (d == 0) return;
            if (m != RM_ORDER) rBack[t] = -d;
        }
        bool grid = lp.locked && lp.beats > 0 && lp.beatLen > 0.0;
        int nb = std::max(1, lp.loopBeats);
        if (d == 0) return;
        int L = playLen(t);
        if (L <= 0) return;
        int to = lp.pos + (int)((int64_t)d * L / n);
        to = ((to % L) + L) % L;
        if (grid && ((int64_t)d * nb) % n == 0) lp.beatIdx = ((lp.beatIdx + (int)((int64_t)d * nb / n)) % nb + nb) % nb;
        lp.jump(to);
        lastSlice[t] = clamp((int)((int64_t)to * n / L), 0, n - 1);
        chopAnchor(t);
    }

    // deal every lane a RAND effect that sits with the others: one of a few sets that go together (a groove,
    // fills, the phrase, glitch, backwards); the busy ones (rolls, STUTTER) on three lanes at most and gentle,
    // 1/64 and ORDER on one lane at most, one other lane strong, the rest in the middle
    void dealRand() {
        static const int kSets[5][4] = {
            {RM_8, RM_16, RM_BEAT, RM_8},        // a groove
            {RM_8, RM_STUTTER, RM_32, RM_16},    // fills
            {RM_BAR, RM_BEAT, RM_8, RM_BAR},     // the phrase
            {RM_16, RM_32, RM_64, RM_STUTTER},   // glitch
            {RM_ORDER, RM_BAR, RM_8, RM_BEAT}};  // backwards
        const int set = std::min(4, (int)(random::uniform() * 5));
        int busy = 0, n64 = 0, nOrder = 0, strong = std::min(kTracks - 1, (int)(random::uniform() * kTracks));
        for (int t = 0; t < kTracks; t++) {
            int m = RM_8;
            for (int k = 0; k < 12; k++) {
                int c = kSets[set][std::min(3, (int)(random::uniform() * 4))];
                bool isBusy = c == RM_32 || c == RM_64 || c == RM_STUTTER;
                if ((isBusy && busy >= 3) || (c == RM_64 && n64 >= 1) || (c == RM_ORDER && nOrder >= 1)) continue;
                m = c;
                break;
            }
            bool isBusy = m == RM_32 || m == RM_64 || m == RM_STUTTER;
            busy += isBusy; n64 += m == RM_64; nOrder += m == RM_ORDER;
            randOn[t] = true;
            randMode[t] = m;
            randAmt[t] = isBusy ? 0 : t == strong ? 2 : 1;
            rReps[t] = rBack[t] = 0;
            lastSlice[t] = -1;
        }
        randChanged();
    }
    // one RAND effect (mode m) on every lane, each keeping its amount: a pad in RAND on the screen
    void setAllRand(int m) {
        for (int t = 0; t < kTracks; t++) {
            randOn[t] = true;
            randMode[t] = clamp(m, 0, RM_LEN - 1);
            rReps[t] = rBack[t] = 0;
            lastSlice[t] = -1;
        }
        randChanged();
    }
    // the effect every lane shares (-1: they differ, or one has none)
    int sharedRand() const {
        for (int t = 0; t < kTracks; t++)
            if (!randOn[t] || randMode[t] != randMode[0]) return -1;
        return randMode[0];
    }
    // RAND on / off (the button, the JAM); switched on, the lanes load the selected scene's RAND set (blank until
    // RAND on the screen deals one)
    void toggleRand() {
        randAll = !randAll;
        if (randAll) loadRand(curScene);
    }

    // the strips' state: IN (input gain, input meters) or LVL (track level, meters of the loop playing alone,
    // without the input monitor): the two never show each other's audio
    bool stripLvl() { return params[STRIP_PARAM].getValue() > 0.5f; }
    // every loop that holds audio is playing (and there is at least one)
    bool allPlaying() const {
        int n = 0;
        for (int t = 0; t < kTracks; t++) {
            const EchoLooper& lp = loop[t];
            if (lp.len <= 0 || lp.st == EchoLooper::REC || lp.st == EchoLooper::DUB) continue;
            if (lp.st != EchoLooper::PLAY) return false;
            n++;
        }
        return n > 0;
    }
    // the inputs are heard in place of the loops: MON selected (LVL not)
    bool hearIn() { return !stripLvl(); }
    // track t shows as monitoring (cyan): MON selected and its input monitor on (a closed take turns it off)
    bool trackMon(int t) { return hearIn() && params[MON_PARAM + t].getValue() > 0.5f; }
    // track t's lane shows MON (cyan): MON selected and no take of its closed since (REC starts it over)
    bool laneMon(int t) { return hearIn() && !doneMon[t]; }
    int stripParam(int t) { return (stripLvl() ? LEVEL_PARAM : GAIN_PARAM) + t; }
    float stripPk(int t) { return stripLvl() ? playPk[t] : inPk[t]; }

    // track t's strip value (input gain or level), -40 .. +6 dB, 1 dB a step (below -40 dB: off)
    void nudgeLevel(int t, int d) {
        ParamQuantity* pq = paramQuantities[stripParam(t)];
        float v = pq->getValue();
        float db = std::round(v <= 0.f ? -100.f : 20.f * std::log10(v)) + d;
        if (db < -40.f) { pq->setValue(d > 0 ? std::pow(10.f, -40.f / 20.f) : 0.f); return; }
        pq->setValue(std::pow(10.f, std::min(db, 6.f) / 20.f));
    }
    // the selected track's record length, one cell of FREE .. 8 BAR along
    void nudgeLen(int d) {
        Param& p = params[LEN_PARAM + sel()];
        int v = (int)std::round(p.getValue()), i = 0;
        for (int k = 0; k < kLenN; k++) if (kLenBeats[k] <= v) i = k;
        p.setValue((float)kLenBeats[clamp(i + d, 0, kLenN - 1)]);
    }
    void toggle(Param& p) { p.setValue(p.getValue() > 0.5f ? 0.f : 1.f); }

    // ---- the JAM ----
    int curPage() { return clamp((int)params[PAGE_PARAM].getValue(), 1, 6); }
    int padNote(int r, int c) { return std::min(127, 12 * (curPage() - 1) + 56 + c - 8 * r); }
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
    ~SynTHBloKLoop() { songlink::leave(this); if (jam.active) { slowSend = true; clearLeds(); } jamown::release(id); }
    // MATRIX-SONG: its pattern is a scene (U1-U8); PLAY / STOP / ONCE act on every track that holds a loop,
    // REC records the selected track
    int songPatternNow() override { return curScene; }
    // a section picks a scene: selected as on the panel, and (a stored scene) its tracks start and stop to
    // match what it noted, so each section brings LOOP's tracks in and out
    void songSetPattern(int p) override {
        if (p < 0 || p > 7) return;
        recallScene(p);
        if (!sceneUsed[p]) return;
        for (int t = 0; t < kTracks; t++) {
            const EchoLooper& lp = loop[t];
            if (lp.len <= 0 || lp.st == EchoLooper::REC || lp.st == EchoLooper::DUB || lp.arm != EchoLooper::A_NONE) continue;
            bool want = (sceneMask[p] >> t) & 1;
            if (want != lp.playing()) act(t, 1);
        }
    }
    void songDoLoop(int a) override {
        if (a == songlink::L_REC) {
            const EchoLooper& lp = loop[sel()];
            if (lp.st != EchoLooper::REC && lp.st != EchoLooper::DUB && lp.arm != EchoLooper::A_REC) act(sel(), 0);
            return;
        }
        for (int t = 0; t < kTracks; t++) {
            const EchoLooper& lp = loop[t];
            if (lp.len <= 0 || lp.st == EchoLooper::REC || lp.st == EchoLooper::DUB || lp.arm != EchoLooper::A_NONE) continue;
            if (a == songlink::L_PLAY && !lp.playing()) act(t, 1);
            else if (a == songlink::L_STOP && lp.playing()) act(t, 1);
            else if (a == songlink::L_ONCE && lp.st != EchoLooper::ONCE) act(t, 2);
        }
    }

    void jamButton(int cc, int b) {
        bool down = b > 0;
        if (cc == CC_SOLO) { soloHeld = down; return; }
        if (cc == CC_MUTE) { muteHeld = down; return; }
        if (!down) return;
        int s = sel();
        switch (cc) {
            case CC_PLAY: act(s, 1); break;
            case CC_REC: act(s, 0); break;
            case CC_DUPLICATE: act(s, 2); break;
            case CC_SONG: act(s, 3); break;
            case CC_CLEAR: act(s, 4); break;
            case CC_PREV: params[SEL_PARAM].setValue((float)((s + kTracks - 1) % kTracks)); break;
            case CC_NEXT: params[SEL_PARAM].setValue((float)((s + 1) % kTracks)); break;
            case CC_UP: nudgeLevel(s, 1); break;
            case CC_DOWN: nudgeLevel(s, -1); break;
            case CC_LEFT: nudgeLen(-1); break;
            case CC_RIGHT: nudgeLen(1); break;
            case CC_TEMPO: {
                Param& p = params[COUNTIN_PARAM];
                p.setValue((float)(((int)std::round(p.getValue()) + 1) % 5));
            } break;
            case CC_GRID: toggleRand(); break;
        }
    }
    void processJamIn(const ProcessArgs& args) {
        midi::Message msg;
        while (midiIn.tryPop(&msg, args.frame)) {
            if (msg.bytes.size() < 3) continue;
            int st = msg.bytes[0] & 0xf0, a = msg.bytes[1], b = msg.bytes[2];
            if (st == 0x90 && b > 0) {
                bool pad = false;
                for (int r = 0; r < 8 && !pad; r++) for (int c = 0; c < 8; c++)
                    if (padNote(r, c) == a) {
                        if (muteHeld) toggle(params[MUTE_PARAM + r]);
                        else if (soloHeld) act(r, 1);
                        else {
                            params[SEL_PARAM].setValue((float)r);
                            if (gateMode()) gatePad(r, c, false);
                            else padReq.store(r * 8 + c);
                        }
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
                    params[MASTER_PARAM].setValue(2.f * b / 127.f);
                    lastLed[kKnobLed] = b;
                }
                else if (a == kKnobPushCC && b > 0) paramQuantities[MASTER_PARAM]->reset();
                else if (a >= 0 && a < 8 && b > 0) params[SEL_PARAM].setValue((float)a);
                else if (a >= 8 && a < 16 && b > 0) toggle(params[MUTE_PARAM + a - 8]);
                else if (a >= 48 && a < 112) {
                    stripPage = (a - 48) / 8;
                    params[stripParam((a - 48) % 8)].setValue(2.f * b / 127.f);
                }
            }
        }
    }
    // what a light should show: 0..63 pads, 64..71 scene, 72..79 strips, 80..87 group, buttons, knob
    int desiredLed(int idx) {
        bool bl = blink < 0.125f;
        const int s = sel();
        const EchoLooper& S = loop[s];
        if (idx == kKnobLed) return (int)std::round(params[MASTER_PARAM].getValue() * 0.5f * 127.f);
        if (idx >= 88) {
            switch (kBtnCC[idx - 88]) {
                case CC_PLAY: return S.playing() || ((S.arm == EchoLooper::A_PLAY || S.arm == EchoLooper::A_STOP) && bl) ? 127 : 0;
                case CC_REC: return S.st == EchoLooper::REC || S.st == EchoLooper::DUB
                                    || ((S.arm == EchoLooper::A_REC || S.arm == EchoLooper::A_DUB) && bl) ? 127 : 0;
                case CC_DUPLICATE: return S.st == EchoLooper::ONCE ? 127 : 0;
                case CC_SONG: return S.undoOk ? 127 : 0;
                case CC_CLEAR: return S.len > 0 || S.st == EchoLooper::REC ? 127 : 0;
                case CC_TEMPO: return params[COUNTIN_PARAM].getValue() > 0.5f ? 127 : 0;
                case CC_GRID: return randAll ? 127 : 0;
                case CC_SOLO: return soloHeld ? 127 : 0;
                case CC_MUTE: return muteHeld ? 127 : 0;
                default: return 127;
            }
        }
        if (idx < 64) {   // as the panel pads
            int r = idx / 8, c = idx % 8;
            const EchoLooper& lp = loop[r];
            if (pend[r] == c) return bl ? kJamWhite : kJamDim[r];
            if (lp.st == EchoLooper::REC) {
                bool on = lp.recTarget > 0 ? c <= (int)((int64_t)lp.pos * 8 / lp.recTarget) : bl;
                return on ? kJamBright[4] : 0;
            }
            if (lp.len <= 0) return 0;
            int L = playLen(r);
            bool head = pend[r] < 0 && lp.playing() && L > 0 && c == clamp((int)((int64_t)lp.pos * 8 / L), 0, 7);
            bool off = (gateOff[r] >> c) & 1;   // GATE: an eighth gated off is dark (the playhead dim on it)
            if (head) return lp.st == EchoLooper::DUB ? kJamBright[4] : off ? kJamDim[r] : kJamWhite;
            if (off) return 0;
            return r == s || (accent[r] >> c) & 1 ? kJamBright[r] : kJamDim[r];   // an accent bright on any row
        }
        if (idx < 72) { int i = idx - 64; return i == s ? kJamBright[i] : kJamDim[i]; }
        if (idx < 80) return (int)std::round(params[stripParam(idx - 72)].getValue() * 0.5f * 127.f);
        int i = idx - 80;
        return params[MUTE_PARAM + i].getValue() > 0.5f ? kJamBright[i] : kJamDim[i];
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
            int n = btnNoteFor(cc);   // the same light for the Template 1 Live note, unless a pad has it
            if (n >= 0 && n > padNote(0, 7)) sendNote(n, d);
        }
    }
    void processJamLeds(const ProcessArgs& args) {
        resync += args.sampleTime;
        if (curPage() != lastPage || stripPage != lastStripPage || resync > 3.f) {
            for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
            lastPage = curPage(); lastStripPage = stripPage; resync = 0.f;
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
        const float sr = args.sampleRate, dt = args.sampleTime;
        const float songG = songPoll(dt);   // MATRIX-SONG: scene / loop requests, and its mute on every output
        // the JAM follows the last clicked module (JamOwner.hpp): losing it goes dark, gaining it resends all
        int own = jam.update(id, params[MIDI_ON_PARAM].getValue() > 0.5f);
        if (own > 0) for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
        else if (own < 0) clearLeds();
        if (jam.active) processJamIn(args);
        else { midi::Message m; while (midiIn.tryPop(&m, args.frame)) {} }
        auto smooth = [&](float& v, float target, float tau) { v += (target - v) * (1.f - std::exp(-dt / tau)); };

        // ---- tempo: the clock, else the TEMPO knob (FREE: no grid) ----
        static const float kPpq[3] = {4.f, 2.f, 1.f};
        const int ppq = (int)kPpq[clockDiv];
        sinceClock += dt;
        sinceBeatClk += dt;
        bool clocked = inputs[CLOCK_INPUT].isConnected();
        bool clockEdge = false, pulseBeat = false, restart = false;
        if (clocked && clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
            restart = sinceClock >= std::max(2.f, 3.f * clockPeriod);
            if (restart) clockCount = 0;   // (re)start: a downbeat
            if (sinceClock < 4.f) clockPeriod = sinceClock;
            sinceClock = 0.f;
            clockEdge = true;
            pulseBeat = clockCount % ppq == 0;
            if (pulseBeat) {   // the beat from beat pulse to beat pulse: steady under a swung clock
                if (!restart && sinceBeatClk < 8.f) clkBeat = sinceBeatClk;
                sinceBeatClk = 0.f;
            }
            clockCount++;
        }
        bool clockOn = clocked && clockPeriod > 0.f && sinceClock < std::max(2.f, 3.f * clockPeriod);
        float tempo = params[TEMPO_PARAM].getValue();
        bool internal = !clockOn && tempo >= kTempoFree + 0.5f;
        float beat = clockOn ? (clkBeat > 0.f ? clkBeat : clockPeriod * kPpq[clockDiv]) : internal ? 60.f / tempo : 0.f;
        source = clockOn ? 2 : internal ? 1 : 0;
        beatNow = beat;
        // ---- RUN / RESET (the TEMPO knob's beat starts again from them: a downbeat at once) ----
        bool kick = false;
        if (runTrig.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f)) toggle(params[RUN_PARAM]);
        const bool run = params[RUN_PARAM].getValue() > 0.5f;
        const bool runStart = run && !running;
        if (!run && running)   // stop: every playing loop stops now; RUN starts the same ones again
            for (int t = 0; t < kTracks; t++) {
                EchoLooper& lp = loop[t];
                wasPlaying[t] = lp.playing();
                if (lp.arm == EchoLooper::A_PLAY || lp.arm == EchoLooper::A_ONCE || lp.arm == EchoLooper::A_STOP
                    || lp.arm == EchoLooper::A_DUB) lp.arm = EchoLooper::A_NONE;
                if (lp.playing()) lp.st = EchoLooper::STOP;
                pend[t] = -1;
            }
        running = run;
        if (runStart) kick = true;
        bool rst = rstTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f) | rstBtnTrig.process(params[RESET_PARAM].getValue());
        if (rst) {
            resetFlash = 0.15f;
            kick = restartReq = true;   // on the grid: from the top on this (or, clocked, the next) beat
            clockCount = 0;
            for (int t = 0; t < kTracks; t++) {
                EchoLooper& lp = loop[t];
                pend[t] = -1;
                if (!lp.locked && lp.len > 0 && lp.st != EchoLooper::REC) { lp.jump(0); lp.beatIdx = 0; }
            }
        }
        resetFlash = std::max(0.f, resetFlash - dt);

        bool beatEdge = false;
        if (clockOn) beatEdge = clockEdge && pulseBeat;
        else if (internal) {
            beatPhase += dt;
            if (kick) { beatPhase = 0.f; beatEdge = true; }
            else if (beatPhase >= beat) { beatPhase -= beat; beatEdge = true; }
        }
        beatEdge = beatEdge && beat > 0.f;
        if (runStart)   // start the loops RUN stopped, on the grid (as PLAY does)
            for (int t = 0; t < kTracks; t++) {
                EchoLooper& lp = loop[t];
                if (wasPlaying[t] && lp.len > 0 && lp.st == EchoLooper::STOP && lp.arm == EchoLooper::A_NONE) lp.press(EchoLooper::B_PLAY);
                wasPlaying[t] = false;
            }

        // ---- every track on the beat grid ----
        const float bs = beat > 0.f ? beat * sr : 0.f;
        const int countIn = clamp((int)std::round(params[COUNTIN_PARAM].getValue()), 0, 4);
        for (int t = 0; t < kTracks; t++) {
            EchoLooper& lp = loop[t];
            lp.tempoBeat = bs; lp.countIn = countIn; lp.gridBeats = 1;
            // a chopped loop: the beat grid's realignment goes to the ghost, not to the playhead
            const int p0 = lp.pos, x0 = lp.xf, xp0 = lp.xfPos;
            const bool wasChop = chopped[t];
            lp.sync(beatEdge, beatEdge, bs, (restart || restartReq) && beatEdge);
            if (wasChop && lp.pos != p0 && (lp.st == EchoLooper::PLAY || lp.st == EchoLooper::ONCE)) {
                ghost[t] = lp.pos;
                lp.pos = p0; lp.xf = x0; lp.xfPos = xp0;
            }
        }
        if (beatEdge) restartReq = false;

        // ---- the transport cells act on the selected track, the jacks on it or (poly) on every track ----
        const int s = sel();
        EchoLooper& L = loop[s];
        for (int b = 0; b < 5; b++) {
            if (edge(b, REC_PARAM + b)) act(s, b);
            Input& in = inputs[REC_INPUT + b];
            int ch = in.getChannels();
            if (ch == 1) { if (trig[b][s].process(in.getVoltage(), 0.1f, 1.f)) act(s, b); }
            else for (int t = 0; t < std::min(ch, kTracks); t++)
                if (trig[b][t].process(in.getVoltage(t), 0.1f, 1.f)) act(t, b);
        }
        // ALL (inside REC): REC on every track, as if pressed on each (each take closes at its own LEN, plays)
        if (edge(5, REC_ALL_PARAM)) for (int t = 0; t < kTracks; t++) act(t, 0);
        // ALL (inside CLEAR): CLEAR on every track
        if (edge(6, CLEAR_ALL_PARAM)) for (int t = 0; t < kTracks; t++) act(t, 4);
        // ALL (inside PLAY): every loop that is not playing starts (on the grid); all playing: they all stop
        if (edge(7, PLAY_ALL_PARAM)) {
            bool allOn = allPlaying();
            for (int t = 0; t < kTracks; t++) {
                const EchoLooper& lp = loop[t];
                if (lp.len <= 0 || lp.st == EchoLooper::REC || lp.st == EchoLooper::DUB) continue;
                if (allOn ? lp.playing() && lp.arm != EchoLooper::A_STOP
                          : lp.st != EchoLooper::PLAY && lp.arm != EchoLooper::A_PLAY) act(t, 1);
            }
        }
        int rq = sceneReq.exchange(-1);
        if (rq >= 0 && rq < 8) recallScene(rq);
        // ---- pads: jump now, or on the next beat when on the grid ----
        int pr = padReq.exchange(-1);
        if (pr >= 0) {
            int t = clamp(pr / 8, 0, kTracks - 1), c = pr % 8;
            if (movable(t)) {
                const EchoLooper& lp = loop[t];
                if (lp.locked && lp.beats > 0) pend[t] = pend[t] == c ? -1 : c;   // a second press cancels
                else jumpTo(t, c);
            }
        }
        for (int t = 0; t < kTracks; t++) {
            if (pend[t] < 0) continue;
            if (!movable(t) || !loop[t].locked) pend[t] = -1;
            else if (beatEdge) { jumpTo(t, pend[t]); pend[t] = -1; }
        }
        // LEN: every new take (with a tempo) records that many beats, then plays; FREE records until REC / PLAY
        for (int t = 0; t < kTracks; t++) {
            loop[t].setLoopBeats(0);
            loop[t].recBeats = std::max(0, (int)std::round(params[LEN_PARAM + t].getValue()));
        }

        // ---- audio (internal units: +-1 = +-5 V) ----
        float mixL = 0.f, mixR = 0.f;
        const float pkFall = std::exp(-dt / 0.3f);
        const int chopN = clamp((int)std::round(params[CHOP_PARAM].getValue()), 1, 8);
        const int sw = scopeW;
        if (hearIn() && !wasHear) for (int t = 0; t < kTracks; t++) doneMon[t] = false;
        wasHear = hearIn();
        for (int t = 0; t < kTracks; t++) {
            Input& in = inputs[IN_INPUT + t];
            const int ch = in.getChannels();
            smooth(gainSm[t], params[GAIN_PARAM + t].getValue(), 0.01f);
            float iL = ch > 0 ? in.getVoltage(0) / 5.f * gainSm[t] : 0.f;
            float iR = ch > 1 ? in.getVoltage(1) / 5.f * gainSm[t] : iL;
            inPk[t] = std::max(std::max(std::fabs(iL), std::fabs(iR)), inPk[t] * pkFall);
            EchoLooper& lp = loop[t];
            float oL, oR;
            chopStep(t, chopN);
            lp.process(iL, iR, oL, oR);
            if (chopped[t]) { int Lc = playLen(t); ghost[t] = Lc > 0 ? (ghost[t] + 1) % Lc : 0; }
            else ghost[t] = lp.pos;
            // a new take is mono or stereo as its input; replacing with a stereo input makes it stereo
            if (lp.st == EchoLooper::REC && prevSt[t] != EchoLooper::REC) { stereo[t] = ch > 1; replacing[t] = false; }
            if ((lp.st == EchoLooper::REC || lp.st == EchoLooper::DUB) && ch > 1) stereo[t] = true;
            if (lp.st == EchoLooper::EMPTY) stereo[t] = false;
            // a replace (REC on a playing loop) ends by itself once it has written over the whole loop
            if (lp.st == EchoLooper::DUB) {
                if (prevSt[t] != EchoLooper::DUB) dubN[t] = 0;
                int Ld = playLen(t);
                if (Ld > 0 && ++dubN[t] >= Ld) lp.st = EchoLooper::PLAY;
            }
            // auto monitor-off: a take (or overdub) just closed into a loop
            bool wasRec = prevSt[t] == EchoLooper::REC || prevSt[t] == EchoLooper::DUB;
            if (autoMon && wasRec && lp.st != EchoLooper::REC && lp.st != EchoLooper::DUB && lp.len > 0) {
                params[MON_PARAM + t].setValue(0.f);
                takeClosed = true;
            }
            if ((wasRec && lp.st != EchoLooper::REC && lp.st != EchoLooper::DUB)
                || (lp.st == EchoLooper::EMPTY && prevSt[t] != EchoLooper::EMPTY)) takeGen[t]++;
            if (lp.st == EchoLooper::EMPTY && prevSt[t] != EchoLooper::EMPTY) gateOff[t] = accent[t] = 0;
            if (wasRec && lp.st != EchoLooper::REC && lp.st != EchoLooper::DUB && lp.len > 0) doneMon[t] = true;
            if ((lp.st == EchoLooper::REC || lp.st == EchoLooper::DUB) && !wasRec) doneMon[t] = false;
            prevSt[t] = lp.st;
            // the eighth now playing; RAND may move on from it as it starts
            int Lt = playLen(t), rn = randN(t);
            if (lp.playing() && Lt > 0 && lp.st != EchoLooper::DUB) {
                int sl = clamp((int)((int64_t)lp.pos * rn / Lt), 0, rn - 1);
                if (sl != lastSlice[t]) {
                    int was = lastSlice[t];
                    lastSlice[t] = sl;
                    if (was >= 0 && sl == (was + 1) % rn && pend[t] < 0) randStep(t, sl, rn);
                    else rReps[t] = rBack[t] = 0;   // moved by something else (the beat, a pad): the variation is over
                }
            } else { lastSlice[t] = -1; rReps[t] = rBack[t] = 0; }
            smooth(levelSm[t], params[LEVEL_PARAM + t].getValue(), 0.01f);
            smooth(muteSm[t], params[MUTE_PARAM + t].getValue() > 0.5f ? 0.f : 1.f, 0.004f);
            // GATE: the loop silent on an eighth gated off, louder (+6 dB) on an accented one (a short fade, no click)
            float gt = 1.f;
            if (lp.playing() && lp.st != EchoLooper::DUB && Lt > 0) {
                int gc = clamp((int)((int64_t)lp.pos * 8 / Lt), 0, 7);
                gt = (gateOff[t] >> gc) & 1 ? 0.f : (accent[t] >> gc) & 1 ? 2.f : 1.f;
            }
            smooth(gateSm[t], gt, 0.001f);
            oL *= gateSm[t]; oR *= gateSm[t];
            // MON selected: the loop fades out and the input in
            smooth(listenSm[t], hearIn() ? 1.f : 0.f, 0.004f);
            oL *= 1.f - listenSm[t]; oR *= 1.f - listenSm[t];
            // the loop alone (no input monitor), as it leaves the track
            float pg = levelSm[t] * muteSm[t];
            playPk[t] = std::max(std::max(std::fabs(oL * pg), std::fabs(oR * pg)), playPk[t] * pkFall);
            bool mon = params[MON_PARAM + t].getValue() > 0.5f;
            if (mon) { oL += iL; oR += iR; }
            else if (listenSm[t] > 0.f) { oL += iL * listenSm[t]; oR += iR * listenSm[t]; mon = true; }
            oL *= levelSm[t] * muteSm[t] * songG; oR *= levelSm[t] * muteSm[t] * songG;
            outPk[t] = std::max(std::max(std::fabs(oL), std::fabs(oR)), outPk[t] * pkFall);
            scope[t][sw] = 0.5f * (oL + oR);
            Output& o = outputs[OUT_OUTPUT + t];
            if (stereo[t] || (mon && ch > 1)) {
                o.setChannels(2);
                o.setVoltage(5.f * oL, 0); o.setVoltage(5.f * oR, 1);
            } else {
                o.setChannels(1);
                o.setVoltage(2.5f * (oL + oR));
            }
            mixL += oL; mixR += oR;
        }
        scopeW = (sw + 1) & (kScope - 1);
        // auto monitor-off: once the last take (or replace) has closed, back from MON to LVL (the loops heard)
        if (takeClosed) {
            bool busy = false;
            for (int t = 0; t < kTracks; t++) {
                const EchoLooper& lp = loop[t];
                if (lp.st == EchoLooper::REC || lp.st == EchoLooper::DUB || lp.counting()
                    || lp.arm == EchoLooper::A_REC || lp.arm == EchoLooper::A_DUB) busy = true;
            }
            if (!busy) { params[STRIP_PARAM].setValue(1.f); takeClosed = false; }
        }
        // master: gain, then a soft ceiling from 8 V (never past 10 V)
        smooth(masterSm, params[MASTER_PARAM].getValue(), 0.01f);
        auto outCeil = [](float v) {
            float a = std::fabs(v);
            return a <= 8.f ? v : std::copysign(8.f + 2.f * std::tanh((a - 8.f) / 2.f), v);
        };
        outputs[MIX_L_OUTPUT].setVoltage(outCeil(5.f * mixL * masterSm));
        outputs[MIX_R_OUTPUT].setVoltage(outCeil(5.f * mixR * masterSm));
        songMeter(outputs[MIX_L_OUTPUT].getVoltage(), outputs[MIX_R_OUTPUT].getVoltage());

        // the selected track's cells: lit with its state; an armed action blinks until the grid edge
        blink += dt;
        if (blink >= 0.25f) blink -= 0.25f;
        float bl = blink < 0.125f ? 1.f : 0.f;
        EchoLooper::State st = L.st;
        int a = L.arm;
        recFlash = st == EchoLooper::REC || st == EchoLooper::DUB ? 1.f : 0.f;
        playFlash = st == EchoLooper::PLAY || st == EchoLooper::DUB ? 1.f : 0.f;
        onceFlash = st == EchoLooper::ONCE ? 1.f : 0.f;
        if (a == EchoLooper::A_REC || a == EchoLooper::A_DUB) recFlash = bl;
        if (L.counting()) recFlash = L.sinceBeat < 0.25f * L.tempoBeat ? 1.f : 0.f;   // the count: lit on each beat
        if (a == EchoLooper::A_CLOSE || a == EchoLooper::A_PLAY || a == EchoLooper::A_STOP) playFlash = bl;
        if (a == EchoLooper::A_CLOSE_ONCE || a == EchoLooper::A_ONCE) onceFlash = bl;
        // ALL: lit while every track records, blinking while every track is recording or waiting to
        int nRec = 0, nArm = 0;
        for (int t = 0; t < kTracks; t++) {
            const EchoLooper& lp = loop[t];
            if (lp.st == EchoLooper::REC || lp.st == EchoLooper::DUB) nRec++;
            else if (lp.arm == EchoLooper::A_REC || lp.arm == EchoLooper::A_DUB) nArm++;
        }
        allRecFlash = nRec == kTracks ? 1.f : nRec + nArm == kTracks ? bl : 0.f;
        // PLAY ALL: lit while every loop plays
        allPlayFlash = allPlaying() ? 1.f : 0.f;
        if (jam.active) processJamLeds(args);
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_t* sc = json_array();
        for (int k = 0; k < 8; k++) json_array_append_new(sc, sceneUsed[k] ? json_integer(sceneMask[k]) : json_null());
        json_object_set_new(root, "scenes", sc);
        json_object_set_new(root, "curScene", json_integer(curScene));
        json_object_set_new(root, "clockDiv", json_integer(clockDiv));
        json_object_set_new(root, "zoom", json_integer(waveZoom));
        json_object_set_new(root, "view", json_integer(laneViews[0]));
        json_object_set_new(root, "viewMon", json_integer(laneViews[1]));
        std::string rs;
        for (int t = 0; t < kTracks; t++) rs += randOn[t] ? '1' : '0';
        json_object_set_new(root, "rand", json_string(rs.c_str()));
        json_object_set_new(root, "randAll", json_boolean(randAll));
        std::string rm, ra;
        for (int t = 0; t < kTracks; t++) { rm += (char)('0' + randMode[t]); ra += (char)('0' + randAmt[t]); }
        json_object_set_new(root, "randMode", json_string(rm.c_str()));
        json_object_set_new(root, "randAmt", json_string(ra.c_str()));
        json_t* sr = json_array();   // each scene's RAND set (and the one with none): per lane its mode ('-': none), amount
        for (int k = 0; k < 9; k++) {
            std::string v;
            for (int t = 0; t < kTracks; t++) {
                v += sRandOn[k][t] ? (char)('0' + sRandMode[k][t]) : '-';
                v += (char)('0' + sRandAmt[k][t]);
            }
            json_array_append_new(sr, sRandUsed[k] ? json_string(v.c_str()) : json_null());
        }
        json_object_set_new(root, "sceneRand", sr);
        json_object_set_new(root, "autoMon", json_boolean(autoMon));
        int playing = 0;   // the loops playing, to play again as the patch loads (their audio: onSave)
        for (int t = 0; t < kTracks; t++) if (loop[t].st == EchoLooper::PLAY || loop[t].st == EchoLooper::DUB) playing |= 1 << t;
        json_object_set_new(root, "playing", json_integer(playing));
        json_t* go = json_array();   // each track's eighths gated off, and accented (bit c: eighth c)
        json_t* ac = json_array();
        for (int t = 0; t < kTracks; t++) { json_array_append_new(go, json_integer(gateOff[t])); json_array_append_new(ac, json_integer(accent[t])); }
        json_object_set_new(root, "gateOff", go);
        json_object_set_new(root, "accent", ac);
        json_object_set_new(root, "in", midiIn.toJson());
        json_object_set_new(root, "out", midiOut.toJson());
        return root;
    }
    void dataFromJson(json_t* root) override {
        if (json_t* sc = json_object_get(root, "scenes"))
            for (int k = 0; k < 8; k++) {
                json_t* j = json_array_get(sc, k);
                sceneUsed[k] = j && json_is_integer(j);
                sceneMask[k] = sceneUsed[k] ? (int)json_integer_value(j) & 0xff : 0;
            }
        if (json_t* j = json_object_get(root, "curScene")) curScene = clamp((int)json_integer_value(j), -1, 7);
        if (json_t* j = json_object_get(root, "clockDiv")) clockDiv = clamp((int)json_integer_value(j), 0, 2);
        if (json_t* j = json_object_get(root, "zoom")) waveZoom = clamp((int)json_integer_value(j), 1, 32);
        if (json_t* j = json_object_get(root, "view")) laneViews[0] = laneViews[1] = clamp((int)json_integer_value(j), 0, 3);
        if (json_t* j = json_object_get(root, "viewMon")) laneViews[1] = clamp((int)json_integer_value(j), 0, 3);
        const char* rs = json_string_value(json_object_get(root, "rand"));
        for (int t = 0; t < kTracks; t++) randOn[t] = rs && strlen(rs) >= (size_t)kTracks && rs[t] == '1';
        if (json_t* sr = json_object_get(root, "sceneRand"))
            for (int k = 0; k < 9; k++) {
                const char* v = json_string_value(json_array_get(sr, k));
                sRandUsed[k] = v && strlen(v) >= (size_t)(2 * kTracks);
                for (int t = 0; t < kTracks && sRandUsed[k]; t++) {
                    sRandOn[k][t] = v[2 * t] != '-';
                    sRandMode[k][t] = sRandOn[k][t] ? clamp(v[2 * t] - '0', 0, RM_LEN - 1) : 0;
                    sRandAmt[k][t] = clamp(v[2 * t + 1] - '0', 0, 2);
                }
            }
        if (json_t* j = json_object_get(root, "randAll")) randAll = json_boolean_value(j);
        else { randAll = false; for (int t = 0; t < kTracks; t++) randAll = randAll || randOn[t]; }   // older patches
        const char* rm = json_string_value(json_object_get(root, "randMode"));
        const char* ra = json_string_value(json_object_get(root, "randAmt"));
        for (int t = 0; t < kTracks; t++) {
            if (rm && strlen(rm) >= (size_t)kTracks) randMode[t] = clamp(rm[t] - '0', 0, RM_LEN - 1);
            if (ra && strlen(ra) >= (size_t)kTracks) randAmt[t] = clamp(ra[t] - '0', 0, 2);
        }
        if (json_t* j = json_object_get(root, "autoMon")) autoMon = json_boolean_value(j);
        if (json_t* j = json_object_get(root, "playing")) { loadPlay = (int)json_integer_value(j) & 0xff; applyLoadPlay(); }
        json_t* go = json_object_get(root, "gateOff");
        json_t* ac = json_object_get(root, "accent");
        for (int t = 0; t < kTracks; t++) {
            gateOff[t] = (int)json_integer_value(json_array_get(go, t)) & 0xff;   // (none saved: 0, all on)
            accent[t] = (int)json_integer_value(json_array_get(ac, t)) & 0xff;
        }
        if (json_t* j = json_object_get(root, "in")) midiIn.fromJson(j);
        if (json_t* j = json_object_get(root, "out")) midiOut.fromJson(j);
    }
};

typedef SynTHBloKLoop LP;

// ------------------------------------------------------------ widgets ------------------------------------------------------------

// (the JAM colours kPatColor / kPatOnColor / kIdle and drawColorButton are shared: Matrix8_ui.hpp)
const NVGcolor kRecRed = nvgRGB(0xff, 0x32, 0x28);

// The module browser's preview: tracks 1-5 hold loops, 1-3 playing.
bool previewHas(int t) { return t < 5; }
float previewPeak(int t, int c) {
    float ph = (float)(c % 12) / 12.f;
    return (0.25f + 0.6f * std::exp(-ph * (3.f + t))) * (0.7f + 0.3f * std::sin(0.7f * c + t));
}

// Track 1-8: picks the track the screen and transport work on.
struct LTrackBtn : OpaqueWidget {
    LP* module = NULL;
    int t = 0;
    void draw(const DrawArgs& args) override {
        drawColorButton(args, box.size, t, module ? module->sel() == t : t == 0, (char)('1' + t), 11.f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[LP::SEL_PARAM].setValue((float)t);
            e.consume(this);
        }
    }
};

// A grid pad: row = track, column = an eighth of its loop. A loop lights its row dim, the playing eighth
// bright; a take being recorded fills its row red as it goes (blinks while recording free). Click: select the
// track and play from that eighth (on the next beat when on the grid: the pad blinks until then). GATE (the pads'
// mode): click gates the eighth on / off, right-click accents it; an eighth gated off is dark, an accented one
// has a bright core (in either mode).
struct LPad : OpaqueWidget {
    LP* module = NULL;
    int r = 0, c = 0;
    void draw(const DrawArgs& args) override {
        NVGcolor col = kPatColor[r], onCol = kPatOnColor[r];
        if (module && module->params[LP::MUTE_PARAM + r].getValue() > 0.5f) {   // muted: faded
            col = nvgLerpRGBA(kIdle, col, 0.35f);
            onCol = nvgLerpRGBA(kIdle, onCol, 0.35f);
        }
        bool has = false, head = false, rec = false, sel = false;
        if (module && module->pend[r] == c && module->blink < 0.125f) head = true;
        if (!module) {
            has = previewHas(r); head = has && r < 3 && c == (2 * r + 1) % 8; sel = r == 0;
        } else {
            const EchoLooper& lp = module->loop[r];
            sel = module->sel() == r;
            if (lp.st == EchoLooper::REC) {
                if (lp.recTarget > 0) rec = c <= (int)((int64_t)lp.pos * 8 / lp.recTarget);
                else rec = module->blink < 0.125f;
            } else if (lp.len > 0) {
                has = true;
                int L = module->playLen(r);
                if (module->pend[r] < 0) head = lp.playing() && L > 0 && c == clamp((int)((int64_t)lp.pos * 8 / L), 0, 7);
                rec = head && lp.st == EchoLooper::DUB;
            }
        }
        bool off = module && (module->gateOff[r] >> c) & 1, acc = module && !off && (module->accent[r] >> c) & 1;
        NVGcolor fill = kIdle;
        if (rec) fill = kRecRed;
        else if (has && off) fill = head ? nvgRGB(0x44, 0x44, 0x44) : kIdle;
        else if (head) fill = nvgLerpRGBA(onCol, nvgRGB(0xff, 0xff, 0xff), 0.35f);
        else if (has) fill = nvgLerpRGBA(kIdle, onCol, 0.45f);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        nvgFillColor(args.vg, fill);
        nvgFill(args.vg);
        NVGcolor edge = sel ? onCol : nvgLerpRGBA(kIdle, col, 0.7f);
        if (has && off && !rec) edge = nvgLerpRGBA(kIdle, edge, 0.35f);
        nvgStrokeColor(args.vg, edge);
        nvgStrokeWidth(args.vg, sel ? 1.2f : 0.8f);
        nvgStroke(args.vg);
        if (has && acc && !rec) {   // accent: a bright core, as on MATRIX-DRUM and MATRIX-DUO
            float m = box.size.x * 0.3f;
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, m, m, box.size.x - 2.f * m, box.size.y - 2.f * m, 0.8f);
            nvgFillColor(args.vg, nvgLerpRGBA(onCol, nvgRGB(0xff, 0xf4, 0xd0), 0.75f));
            nvgFill(args.vg);
        }
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || (e.button != GLFW_MOUSE_BUTTON_LEFT && e.button != GLFW_MOUSE_BUTTON_RIGHT)) return;
        bool gate = module && module->gateMode();
        if (e.button == GLFW_MOUSE_BUTTON_RIGHT && !gate) return;
        if (module) {
            module->params[LP::SEL_PARAM].setValue((float)r);
            if (!gate) module->padReq.store(r * 8 + c);
            else module->gatePad(r, c, e.button == GLFW_MOUSE_BUTTON_RIGHT);
        }
        e.consume(this);
    }
};

const char* const kRandModeName[LP::RM_LEN] = {"1/8: another eighth for an eighth", "1/16: another sixteenth for a sixteenth",
    "1/32: a 32nd roll to the next eighth", "1/64: a 64th roll to the next eighth",
    "STUTTER: the sixteenth just played again", "BEAT: the beat two away, for a beat", "BAR: another bar, for a bar",
    "ORDER: the eighths backwards"};

// RAND's modes as the screen labels them
const char* const kRandLabel[LP::RM_LEN] = {"1/8", "1/16", "1/32", "1/64", "STU", "BEA", "BAR", "ORD"};

// RAND's menu for track s: its mode and amount
void randMenu(LP* m, int s) {
    Menu* menu = createMenu();
    menu->addChild(createMenuLabel(string::f("Track %d RAND", s + 1)));
    for (int i = 0; i < LP::RM_LEN; i++)
        menu->addChild(createCheckMenuItem(kRandModeName[i], "", [=]() { return m->randMode[s] == i; },
                                           [=]() { m->randOn[s] = true; m->randMode[s] = i; m->lastSlice[s] = -1; m->randChanged(); }));
    menu->addChild(new MenuSeparator);
    menu->addChild(createMenuLabel("Amount (ORDER: every eighth)"));
    static const char* const kAmtName[3] = {"LOW", "MED", "HIGH"};
    for (int i = 0; i < 3; i++)
        menu->addChild(createCheckMenuItem(kAmtName[i], "", [=]() { return m->randAmt[s] == i; },
                                           [=]() { m->randAmt[s] = i; m->randChanged(); }));
}

// RAND (green, as on the JAM): the selected track shuffles its loop as it plays (lit while on). Right-click: its
// mode and amount.
struct LRandBtn : OpaqueWidget {
    LP* module = NULL;
    void draw(const DrawArgs& args) override {
        bool on = module ? module->randAll : false;
        NVGcolor g = nvgRGB(0x78, 0xe6, 0x5a);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.4f);
        nvgFillColor(args.vg, on ? g : kIdle);
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, g);
        nvgStrokeWidth(args.vg, on ? 1.2f : 0.8f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 7.f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? nvgRGB(0x10, 0x10, 0x10) : g);
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, "RAND", NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->toggleRand();
            e.consume(this);
        }
        if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS) {
            if (module) randMenu(module, module->sel());
            e.consume(this);
        }
    }
};

// CHOP: set it to n eighths (1 .. 8), or step it
int chopGet(LP* m) { return m ? clamp((int)std::round(m->params[LP::CHOP_PARAM].getValue()), 1, 8) : 8; }
void chopSet(LP* m, int n) { if (m) m->params[LP::CHOP_PARAM].setValue((float)clamp(n, 1, 8)); }
void stepChop(LP* m, int d) { chopSet(m, chopGet(m) + d); }

// Up / down arrow: CHOP longer / shorter.
struct LArrowBtn : OpaqueWidget {
    LP* module = NULL;
    bool up = true;
    void draw(const DrawArgs& args) override {
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.4f);
        nvgFillColor(args.vg, oled::offFill());
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, oled::outline(120));
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
        float cx = W * 0.5f, cy = H * 0.5f, s = std::min(W, H) * 0.28f;
        nvgBeginPath(args.vg);
        if (up) { nvgMoveTo(args.vg, cx - s, cy + s * 0.7f); nvgLineTo(args.vg, cx + s, cy + s * 0.7f); nvgLineTo(args.vg, cx, cy - s * 0.8f); }
        else { nvgMoveTo(args.vg, cx - s, cy - s * 0.7f); nvgLineTo(args.vg, cx + s, cy - s * 0.7f); nvgLineTo(args.vg, cx, cy + s * 0.8f); }
        nvgClosePath(args.vg);
        nvgFillColor(args.vg, nvgRGB(0xff, 0xb0, 0x50));
        nvgFill(args.vg);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            stepChop(module, up ? 1 : -1);
            e.consume(this);
        }
    }
};

// The CHOP display: the length every loop repeats, in eighths (FULL: none), over 8 segments, 1 at the bottom,
// filled up to it (the length itself lit). Click a segment: that length; scroll: step.
struct LChopDisplay : OpaqueWidget {
    LP* module = NULL;
    const float y0 = 19.f;
    float segH() { return (box.size.y - 3.f - y0) / 8.f; }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        int n = chopGet(module);
        char txt[8];
        if (n >= 8) snprintf(txt, sizeof txt, "FULL");
        else snprintf(txt, sizeof txt, "%d/8", n);
        nvgFontSize(args.vg, 5.f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        nvgFillColor(args.vg, oled::dim());
        nvgText(args.vg, W * 0.5f, 2.f, "CHOP", NULL);
        nvgFontSize(args.vg, n >= 8 ? 7.f : 9.f);
        nvgFillColor(args.vg, n >= 8 ? oled::amber() : oled::litFill());
        nvgText(args.vg, W * 0.5f, n >= 8 ? 8.f : 7.f, txt, NULL);
        float y1 = H - 3.f, sh = segH();
        nvgFontSize(args.vg, 4.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        for (int i = 0; i < 8; i++) {
            bool used = i < n, on = i == n - 1;
            float y = y1 - (i + 1) * sh;
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, 3.f, y + 0.6f, W - 6.f, sh - 1.2f, 0.6f);
            nvgFillColor(args.vg, on ? oled::litFill() : used ? nvgRGBA(255, 120, 30, 90) : nvgRGBA(255, 120, 30, 30));
            nvgFill(args.vg);
            char n[2] = {(char)('1' + i), 0};
            nvgFillColor(args.vg, on ? oled::ink() : used ? oled::amber() : oled::dim());
            nvgText(args.vg, W * 0.5f, y + sh * 0.5f, n, NULL);
        }
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS) return;
        e.consume(this);
        if (!module || e.pos.y < y0) return;
        int k = (int)((box.size.y - 3.f - e.pos.y) / segH());
        if (k >= 0 && k < 8) chopSet(module, k + 1);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (e.scrollDelta.y == 0.f) return;
        stepChop(module, e.scrollDelta.y > 0.f ? 1 : -1);
        e.consume(this);
    }
};

// In the input plate, beside A-H and shaped like them, a pair selecting the strips and what is heard (the lit one):
// LVL = track levels, the loops heard; MON = input gains, the inputs heard in place of the loops.
struct LInBtn : OpaqueWidget {
    LP* module = NULL;
    int param = 0;
    bool mute = false;   // (the MON button)
    void draw(const DrawArgs& args) override {
        bool on = module ? (module->params[param].getValue() > 0.5f) != mute : mute;
        NVGcolor col = mute ? monCyan() : nvgRGB(0xff, 0x70, 0x0f);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.4f);
        nvgFillColor(args.vg, on ? col : kIdle);
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, col);
        nvgStrokeWidth(args.vg, on ? 1.2f : 0.8f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 6.f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? nvgRGB(0x10, 0x10, 0x10) : col);
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, mute ? "MON" : "LVL", NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[param].setValue(mute ? 0.f : 1.f);
            e.consume(this);
        }
    }
};

// Between the IN and GAIN titles: lit (amber, as MON) while MON is selected (the inputs heard), dark on LVL.
struct LMonLed : TransparentWidget {
    LP* module = NULL;
    bool lit() { return !module || module->hearIn(); }
    void dot(const DrawArgs& args, NVGcolor c) {
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, box.size.x * 0.5f);
        nvgFillColor(args.vg, c);
        nvgFill(args.vg);
    }
    void draw(const DrawArgs& args) override {
        if (!lit()) dot(args, nvgRGB(0x14, 0x30, 0x38));
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1 && lit()) {
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, box.size.x * 1.4f);
            nvgFillPaint(args.vg, nvgRadialGradient(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, 0.f, box.size.x * 1.4f,
                                                    monCyan(0x70), monCyan(0)));
            nvgFill(args.vg);
            dot(args, monCyan());
        }
        Widget::drawLayer(args, layer);
    }
};

// MUTE (the JAM's A-H), one under each column: mute track 1-8 (lit: muted; it keeps playing and recording, silent at its output and the mix).
struct LMuteBtn : OpaqueWidget {
    LP* module = NULL;
    int t = 0;
    void draw(const DrawArgs& args) override {
        bool on = module ? module->params[LP::MUTE_PARAM + t].getValue() > 0.5f : false;
        drawColorButton(args, box.size, t, on, 0, 9.f);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? nvgRGB(0x10, 0x10, 0x10) : kPatColor[t]);
        nvgFontSize(args.vg, 5.5f);
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, "MUTE", NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) {
                Param& p = module->params[LP::MUTE_PARAM + t];
                p.setValue(p.getValue() > 0.5f ? 0.f : 1.f);
            }
            e.consume(this);
        }
    }
};

// A level meter as a translucent wash in colour base (the track's), rising in the box (x, y, w, h) from its bottom:
// -48 .. +6 dB (0 dB = 5 V), faint at the bottom, strongest at the level; red past +4 dB.
void levelWash(const widget::Widget::DrawArgs& args, float x, float y, float w, float h, float pk, NVGcolor base) {
    float n = clamp((20.f * std::log10(pk + 1e-6f) + 48.f) / 54.f, 0.f, 1.f);
    if (n <= 0.f) return;
    float lh = h * n, top = y + h - lh;
    NVGcolor c = pk > 1.6f ? kRecRed : nvgLerpRGBA(base, nvgRGB(0xff, 0xff, 0xff), 0.3f);
    nvgBeginPath(args.vg);
    nvgRoundedRect(args.vg, x, top, w, lh, std::min(1.f, w * 0.5f));
    nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.f, y + h, 0.f, top, nvgTransRGBA(c, 30), nvgTransRGBA(c, 160)));
    nvgFill(args.vg);
}

// Strip (as the JAM strips), in two states: IN, the fill is the input gain; LVL, the track level (unity half
// way). It rebinds its param to the state. The wash follows: the input level (IN), or the track's output (LVL).
struct LGainStrip : app::ParamWidget {
    LP* lmodule = NULL;
    int t = 0;
    void step() override {
        if (lmodule) paramId = lmodule->stripParam(t);
        ParamWidget::step();
    }
    float get() {
        ParamQuantity* pq = getParamQuantity();
        return pq ? pq->getValue() * 0.5f : 0.5f;
    }
    void set(float v) { if (ParamQuantity* pq = getParamQuantity()) pq->setValue(clamp(v, 0.f, 1.f) * 2.f); }
    // the track's colour; its cyan while it monitors (back to its colour as its take closes)
    NVGcolor colour() { return lmodule && lmodule->trackMon(t) ? kMonRamp[t] : kPatOnColor[t]; }
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        nvgFillColor(args.vg, nvgRGB(0x14, 0x14, 0x14));
        nvgFill(args.vg);
        NVGcolor col = colour();
        float h = (box.size.y - 3.f) * get();
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 1.5f, box.size.y - 1.5f - h, box.size.x - 3.f, h, 1.f);
        nvgFillColor(args.vg, nvgLerpRGBA(kIdle, col, 0.8f));
        nvgFill(args.vg);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        nvgStrokeColor(args.vg, nvgLerpRGBA(kIdle, col, 0.45f));
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        ParamWidget::drawLayer(args, layer);
        if (layer != 1) return;
        // the input (IN) or output (LVL) level, over the fill (it brightens the fill, and shows faintly above it)
        levelWash(args, 1.5f, 1.5f, box.size.x - 3.f, box.size.y - 3.f, lmodule ? lmodule->stripPk(t) : 0.2f + 0.08f * t, colour());
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            set(1.f - clamp((e.pos.y - 1.5f) / (box.size.y - 3.f), 0.f, 1.f));
            e.consume(this);
            return;
        }
        ParamWidget::onButton(e);
    }
    void onDragMove(const DragMoveEvent& e) override {
        float z = APP->scene->rackScroll->getZoom();
        set(get() - e.mouseDelta.y / z / (box.size.y - 3.f));
    }
};

// Value arc around a knob (as on MATRIX-DRUM): amber track, orange arc.
struct LRing : TransparentWidget {
    Module* module = NULL;
    int param = 0;
    float width = 1.8f;
    int color = -1;     // a track's pad colour (0-7), else amber
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        NVGcolor on = color >= 0 ? nvgTransRGBA(kPatOnColor[color], 235) : nvgRGBA(255, 112, 15, 235);
        NVGcolor off = color >= 0 ? nvgTransRGBA(kPatOnColor[color], 50) : nvgRGBA(255, 180, 90, 40);
        float v = module ? module->paramQuantities[param]->getScaledValue() : 0.5f;
        float cx = box.size.x * 0.5f, cy = box.size.y * 0.5f, rr = box.size.x * 0.5f - width * 0.8f;
        float a0 = M_PI * 0.75f, a1 = M_PI * 2.25f;
        nvgBeginPath(args.vg);
        nvgArc(args.vg, cx, cy, rr, a0, a1, NVG_CW);
        nvgStrokeColor(args.vg, off);
        nvgStrokeWidth(args.vg, width);
        nvgStroke(args.vg);
        if (v > 0.001f) {
            nvgBeginPath(args.vg);
            nvgArc(args.vg, cx, cy, rr, a0, a0 + (a1 - a0) * v, NVG_CW);
            nvgStrokeColor(args.vg, on);
            nvgStrokeWidth(args.vg, width);
            nvgStroke(args.vg);
        }
    }
};

struct LLabel : TransparentWidget {
    std::string text;
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 7.f);
        nvgTextLetterSpacing(args.vg, 0.6f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(0xbb, 0xbb, 0xbb));
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, text.c_str(), NULL);
    }
};

struct LTitle : TransparentWidget {
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
        nvgText(args.vg, 0.f, mm2px(6.4f), "MTX-LOOP", NULL);
    }
};

// The screen, top to bottom (mm from its top edge). The header, info line and lanes are drawn by LScreen;
// the cell rows and tiles are widgets laid over it.
namespace ls {
const float kPad = 2.f, kCG = 1.f, kGap = 0.9f;
const float kHeadH = 7.f;                                   // big track number + name | state, layers, length
const float yInfo = kHeadH;                                 // VIEW | RAND | COUNT | FREE .. 8 BAR | tempo
const float yLanes = yInfo + oled::kRow + 0.4f;             // the 8 loops (taller: the row LEN had)
const float kLaneH = 5.2f + (oled::kCellH + kGap) / kTracks;
const float yScene = yLanes + kTracks * kLaneH + 0.6f;      // U1 .. U8 | STORE
// the info row's cells (mm from the screen's left): COUNT, then the record lengths
const float kInfoCellH = 3.8f, xCount = 26.5f, kCountW = 12.f, kLenW = 10.8f;   // after RAND and its pads
const float yTiles = yScene + oled::kCellH + 1.2f;          // 01 .. 08, 2 rows of 4
const float kTileH = 8.f;
const float yTrans = yTiles + 2.f * kTileH + 2.f * kGap;   // REC | PLAY | ONCE | UNDO | CLEAR, tall cells
const float kTransH = 2.f * oled::kCellH + kGap;
const float kH = yTrans + kTransH + 0.6f;
}

// a track's own colour on the screen: the colour of its pads and its 1-8 button
NVGcolor trackCol(int t, int a = 255) { return nvgTransRGBA(kPatOnColor[clamp(t, 0, kTracks - 1)], (unsigned char)a); }

NVGcolor stateColor(int st) {
    return st == EchoLooper::REC || st == EchoLooper::DUB ? kRecRed
         : st == EchoLooper::PLAY || st == EchoLooper::ONCE ? oled::green() : oled::dim();
}

struct LScreen : TransparentWidget {
    LP* module = NULL;
    // the readouts: amber on LVL, cyan on MON
    NVGcolor accent() { return module && module->hearIn() ? monCyan() : oled::amber(); }
    // a lane's readout and number: its track colour, or its cyan (kMonRamp) while MON is selected, back to its
    // colour as its take closes
    NVGcolor laneCol(int t, int a) {
        return module && module->laneMon(t) ? nvgTransRGBA(kMonRamp[t], (unsigned char)a) : trackCol(t, a);
    }

    // the spectrum views: kBands log-spaced bands, 30 Hz .. 16 kHz, each 0..1 on the screen
    enum { kFft = LP::kScope, kBands = 48 };
    dsp::RealFFT fft{kFft};
    float win[kFft];
    float statBand[kTracks][kBands] = {}, liveBand[kTracks][kBands] = {}, liveHold[kTracks][kBands] = {};
    int statKey[kTracks][4];   // takeGen, len, layers, undone the static spectrum was measured at
    float viewX0 = 0.f, viewX1 = 0.f, randX0 = 0.f, randX1 = 0.f;   // the VIEW and RAND tags, for clicks
    float padX0 = 0.f, padY0 = 0.f, padW = 1.f, padH = 1.f;          // RAND's 4 x 2 pads: the grid's corner, one step
    int hoverPad = -1;                                               // the RAND pad under the mouse (-1: none)
    float vgX0 = 0.f, vgY0 = 0.f, vgW = 1.f, vgH = 1.f;              // VIEW's 2 x 2 boxes: the grid's corner, one step
    int hoverView = -1;                                              // the VIEW box under the mouse (-1: none)
    int hoverTag = 0;                                                // the tag under the mouse: 1 VIEW, 2 RAND, 0 neither
    double viewPressT = -1.0, randPressT = -1.0;   // when each was last clicked (its glow)
    LScreen() {
        for (int i = 0; i < kFft; i++) win[i] = 0.5f - 0.5f * std::cos(2.f * (float)M_PI * i / kFft);
        for (int t = 0; t < kTracks; t++) statKey[t][0] = -1;
    }
    // a frame's band powers (a full-scale sine = 1), Hann windowed, added to pw
    void bandPower(const float* x, float sr, float* pw) {
        alignas(16) float in[kFft], sp[2 * kFft];
        for (int i = 0; i < kFft; i++) in[i] = x[i] * win[i];
        fft.rfft(in, sp);
        const float binHz = sr / kFft, r = std::pow(16000.f / 30.f, 1.f / kBands);
        auto amp2 = [&](int k) { k = clamp(k, 1, kFft / 2 - 1); return (sp[2 * k] * sp[2 * k] + sp[2 * k + 1] * sp[2 * k + 1]) * 16.f / ((float)kFft * kFft); };
        for (int b = 0; b < kBands; b++) {
            float fa = 30.f * std::pow(r, (float)b), fb = fa * r;
            int ka = (int)std::ceil(fa / binHz), kb = (int)std::floor(fb / binHz);
            float p = 0.f;
            if (kb < ka) p = amp2((int)std::round(std::sqrt(fa * fb) / binHz));   // narrower than a bin
            else for (int k = ka; k <= kb; k++) p += amp2(k);
            pw[b] += p;
        }
    }
    // band powers -> 0..1 (a +3 dB / octave tilt round 1 kHz, so music reads level; -72 .. -6 dB)
    static float bandShow(float p, int b) {
        float fc = 30.f * std::pow(16000.f / 30.f, (b + 0.5f) / kBands);
        float db = 10.f * std::log10(p + 1e-12f) + 3.f * std::log2(fc / 1000.f);
        return clamp((db + 72.f) / 66.f, 0.f, 1.f);
    }
    // the static spectrum: up to 48 frames across the whole loop, measured again when its audio changes
    void updateStatic() {
        for (int t = 0; t < kTracks; t++) {
            const EchoLooper& lp = module->loop[t];
            int key[4] = {module->takeGen[t], lp.len, lp.layers, lp.undone ? 1 : 0};
            if (std::equal(key, key + 4, statKey[t])) continue;
            std::copy(key, key + 4, statKey[t]);
            float pw[kBands] = {};
            const int len = lp.len;
            if (len > 0 && (int)lp.l.size() >= len && (int)lp.r.size() >= len) {
                int n = clamp(len / kFft, 1, 48);
                float x[kFft];
                for (int f = 0; f < n; f++) {
                    int a = n > 1 ? (int)((int64_t)(len - kFft) * f / (n - 1)) : 0;
                    for (int i = 0; i < kFft; i++) x[i] = a + i < len ? 0.5f * (lp.l[a + i] + lp.r[a + i]) : 0.f;
                    bandPower(x, lp.sr, pw);
                }
                for (int b = 0; b < kBands; b++) pw[b] /= n;
            }
            for (int b = 0; b < kBands; b++) statBand[t][b] = len > 0 ? bandShow(pw[b], b) : 0.f;
        }
    }
    // the live spectrum, from what leaves each track: quick up, slow down; a falling peak line
    void updateLive() {
        const int w = module->scopeW;
        float x[kFft];
        for (int t = 0; t < kTracks; t++) {
            for (int i = 0; i < kFft; i++) x[i] = module->scope[t][(w + i) & (kFft - 1)];
            float pw[kBands] = {};
            bandPower(x, module->loop[t].sr, pw);
            for (int b = 0; b < kBands; b++) {
                float v = bandShow(pw[b], b), &lb = liveBand[t][b];
                lb = v > lb ? lb + (v - lb) * 0.6f : std::max(v, lb - 0.03f);
                liveHold[t][b] = std::max(lb, liveHold[t][b] - 0.006f);
            }
        }
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        int view = module ? module->laneView() : 0;
        if (module && view == 1) updateStatic();
        if (module && (view == 1 || view == 2)) updateLive();
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        nvgBeginPath(args.vg);
        oled::divider(args, 4.f, mm2px(ls::yInfo), W - 4.f, mm2px(ls::yInfo));
        oled::divider(args, 4.f, mm2px(ls::yLanes - 0.2f), W - 4.f, mm2px(ls::yLanes - 0.2f));
        oled::divider(args, 4.f, mm2px(ls::yScene - 0.3f), W - 4.f, mm2px(ls::yScene - 0.3f));
        oled::divider(args, 4.f, mm2px(ls::yTiles - 0.6f), W - 4.f, mm2px(ls::yTiles - 0.6f));
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W, H);
        int s = module ? module->sel() : 0;
        drawHeader(args, W, s);
        drawInfo(args, W, s);
        for (int t = 0; t < kTracks; t++) drawLane(args, W, t, t == s);
        if (module && module->waveZoom > 1 && view == 0) {   // the zoom, a tag at the lanes' top right
            char z[12];
            snprintf(z, sizeof z, "ZOOM x%d", module->waveZoom);
            nvgFontSize(args.vg, 5.5f);
            float b[4], zw = nvgTextBounds(args.vg, 0.f, 0.f, z, NULL, b) + 3.f, zy = mm2px(ls::yLanes) + 0.6f;
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, W - 4.f - zw, zy, zw, 6.5f, 1.f);
            nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 210));
            nvgFill(args.vg);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, accent());
            nvgText(args.vg, W - 4.f - zw * 0.5f, zy + 3.25f, z, NULL);
        }
        nvgResetScissor(args.vg);
    }

    // the selected track: number, LOOP, FULL / EMPTY, mono / stereo | LEN, COUNT, IN level | armed action,
    // RAND, MON | LVL
    void drawHeader(const DrawArgs& args, float W, int s) {
        const EchoLooper* lp = module ? &module->loop[s] : NULL;
        float yc = mm2px(ls::kHeadH * 0.5f + 0.2f);
        char num[4];
        snprintf(num, sizeof num, "%d", s + 1);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 12.f);
        nvgFillColor(args.vg, trackCol(s));
        nvgText(args.vg, 4.f, yc, num, NULL);
        float b[4];
        float x = mm2px(5.5f) + nvgTextBounds(args.vg, 0.f, 0.f, "LOOP", NULL, b) + mm2px(1.5f);
        nvgText(args.vg, mm2px(5.5f), yc, "LOOP", NULL);
        bool full = lp ? lp->len > 0 : previewHas(s);
        nvgFontSize(args.vg, 7.f);
        nvgFillColor(args.vg, full ? oled::green() : oled::dim());
        nvgText(args.vg, x, yc + 0.3f, full ? "FULL" : "EMPTY", NULL);
        x += nvgTextBounds(args.vg, 0.f, 0.f, "EMPTY", NULL, b) + mm2px(2.f);
        bool st2 = module ? module->stereo[s] : true;
        nvgFontSize(args.vg, 6.f);
        nvgFillColor(args.vg, oled::dim());
        nvgText(args.vg, x, yc + 0.5f, lp && lp->len <= 0 && lp->st != EchoLooper::REC ? "" : st2 ? "STEREO" : "MONO", NULL);

        // LEN | COUNT | IN level
        auto pair = [&](float xmm, const char* n, const char* v) {
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFontSize(args.vg, 6.f);
            nvgFillColor(args.vg, oled::dim());
            nvgText(args.vg, mm2px(xmm), yc + 0.5f, n, NULL);
            float w = nvgTextBounds(args.vg, 0.f, 0.f, n, NULL, b);
            nvgFontSize(args.vg, 7.5f);
            nvgFillColor(args.vg, accent());
            nvgText(args.vg, mm2px(xmm) + w + 4.f, yc + 0.3f, v, NULL);
        };
        char len[12] = "FREE", cnt[8] = "OFF", lvl[16] = "-12 dB";
        if (module) {
            int v = (int)std::round(module->params[LP::LEN_PARAM + s].getValue());
            if (v > 0 && v % 4 == 0) snprintf(len, sizeof len, "%d BAR", v / 4);
            else if (v > 0) snprintf(len, sizeof len, "%d BT", v);
            int c = (int)std::round(module->params[LP::COUNTIN_PARAM].getValue());
            if (c > 0) snprintf(cnt, sizeof cnt, "%d", c);
            float pk = module->inPk[s];
            if (!module->inputs[LP::IN_INPUT + s].isConnected()) snprintf(lvl, sizeof lvl, "--");
            else if (pk < 0.004f) snprintf(lvl, sizeof lvl, "-48 dB");
            else snprintf(lvl, sizeof lvl, "%.0f dB", 20.f * std::log10(pk));
        }
        pair(36.f, "LEN", len);
        pair(54.f, "COUNT", cnt);
        pair(67.f, "IN", lvl);

        // MON | LVL at the right end; the count-in or an armed action (blinking) left of it
        float xr = W - 4.f - drawHearTag(args, W - 4.f, yc) - mm2px(3.f);
        char arm[16] = "";
        if (lp && lp->counting()) snprintf(arm, sizeof arm, "COUNT %d", lp->counting());
        else if (lp && lp->arm != EchoLooper::A_NONE && module->blink < 0.125f) {
            static const char* const kArm[8] = {"", "> REC", "> PLAY", "> ONCE", "> PLAY", "> ONCE", "> STOP", "> REPLACE"};
            snprintf(arm, sizeof arm, "%s", kArm[clamp(lp->arm, 0, 7)]);
        }
        if (arm[0]) {
            nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgFontSize(args.vg, 7.f);
            nvgFillColor(args.vg, oled::litFill());
            nvgText(args.vg, xr, yc + 0.3f, arm, NULL);
        }
    }

    // a tag's short glow as it is clicked (pressed at t0), over its box
    void pressGlow(const DrawArgs& args, double t0, float x, float y, float w, float h, NVGcolor col) {
        const float a = 1.f - (float)(system::getTime() - t0) / 0.18f;
        if (a <= 0.f) return;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, x, y, w, h, 1.f);
        nvgFillColor(args.vg, nvgTransRGBA(col, (unsigned char)(70.f * a)));
        nvgFill(args.vg);
    }

    // RAND (as VIEW, green), from x: lit while RAND is on; RAND and a 4 x 2 of pads, one per effect (1/8 .. ORD),
    // the one every lane shares lit. Click RAND (RAND on): deal every lane an effect, kept by the selected scene;
    // click a pad: that effect on every lane; right-click: the selected lane's menu. Over a pad, its name
    // stands in for RAND.
    void drawRandTag(const DrawArgs& args, float x, float yc) {
        const bool all = module && module->randAll;
        const int shared = module ? module->sharedRand() : -1;
        const float h = mm2px(3.6f), y = yc - h * 0.5f, pad = mm2px(0.7f), cw = 3.4f, ch = (h - 2.f * pad - 0.8f) * 0.5f;
        NVGcolor g = oled::green();
        float b[4];
        nvgFontSize(args.vg, 5.5f);
        const float tw = nvgTextBounds(args.vg, 0.f, 0.f, "RAND", NULL, b);
        const float w = pad + tw + mm2px(0.9f) + 4.f * cw + 3.f * 0.8f + pad;
        randX0 = x; randX1 = x + w;
        pressGlow(args, randPressT, x, y, w, h, g);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, x, y, w, h, 1.f);
        if (hoverTag == 2) { nvgFillColor(args.vg, nvgTransRGBA(g, 22)); nvgFill(args.vg); }   // under the mouse: a faint fill
        nvgStrokeColor(args.vg, all ? g : nvgTransRGBA(g, 90));
        nvgStrokeWidth(args.vg, 0.6f);
        nvgStroke(args.vg);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, all || hoverPad >= 0 ? g : nvgTransRGBA(g, 110));
        nvgText(args.vg, x + pad, yc + 0.2f, hoverPad >= 0 ? kRandLabel[clamp(hoverPad, 0, LP::RM_LEN - 1)] : "RAND", NULL);
        padX0 = x + w - pad - 4.f * cw - 3.f * 0.8f; padY0 = y + pad; padW = cw + 0.8f; padH = ch + 0.8f;
        for (int i = 0; i < LP::RM_LEN; i++) {
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, padX0 + (i % 4) * padW, padY0 + (i / 4) * padH, cw, ch, 0.4f);
            if (i == shared) { nvgFillColor(args.vg, all ? g : nvgTransRGBA(g, 150)); nvgFill(args.vg); }
            else {
                nvgStrokeColor(args.vg, nvgTransRGBA(g, i == hoverPad ? 230 : all ? 100 : 70));
                nvgStrokeWidth(args.vg, 0.4f);
                nvgStroke(args.vg);
            }
        }
    }
    // the RAND pad at p (-1: none)
    int padAt(Vec p) {
        int c = (int)std::floor((p.x - padX0) / padW), r = (int)std::floor((p.y - padY0) / padH);
        if (p.x < padX0 || p.y < padY0 || c > 3 || r > 1) return -1;
        return r * 4 + c;
    }
    // the VIEW box at p (-1: none)
    int viewAt(Vec p) {
        int c = (int)std::floor((p.x - vgX0) / vgW), r = (int)std::floor((p.y - vgY0) / vgH);
        if (p.x < vgX0 || p.y < vgY0 || c > 1 || r > 1) return -1;
        return r * 2 + c;
    }
    // over a pad or a VIEW box: its name in the tag; over either tag: a faint fill (consumed over the tags only, so
    // the panel under the screen still gets the rest)
    void onHover(const HoverEvent& e) override {
        bool info = module && e.pos.y >= mm2px(ls::yInfo) && e.pos.y < mm2px(ls::yInfo + oled::kRow);
        hoverPad = module ? padAt(e.pos) : -1;
        hoverView = info ? viewAt(e.pos) : -1;
        hoverTag = !info ? 0 : e.pos.x >= viewX0 && e.pos.x <= viewX1 ? 1 : e.pos.x >= randX0 && e.pos.x <= randX1 ? 2 : 0;
        if (hoverPad >= 0 || hoverTag) e.consume(this);
    }
    void onLeave(const LeaveEvent& e) override { hoverPad = -1; hoverView = -1; hoverTag = 0; }

    // MON | LVL (as the buttons), what is heard: the selected one lit, the other dim; right-aligned to xRight,
    // returns its width
    float drawHearTag(const DrawArgs& args, float xRight, float yc) {
        bool mon = module ? module->hearIn() : false;
        const float w = mm2px(7.f), h = mm2px(3.6f), y = yc - h * 0.5f + 0.3f, gap = mm2px(0.8f);
        const float x = xRight - 2.f * w - gap;
        nvgFontSize(args.vg, 5.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        for (int i = 0; i < 2; i++) {   // i = 1: MON (left), 0: LVL (right)
            bool on = (i == 1) == mon;
            float xi = x + (1 - i) * (w + gap);
            NVGcolor col = i == 1 ? monCyan() : nvgRGB(0xff, 0x70, 0x0f);
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, xi, y, w, h, 1.f);
            if (on) { nvgFillColor(args.vg, col); nvgFill(args.vg); }
            else { nvgStrokeColor(args.vg, nvgTransRGBA(col, 90)); nvgStrokeWidth(args.vg, 0.6f); nvgStroke(args.vg); }
            nvgFillColor(args.vg, on ? nvgRGB(0x10, 0x10, 0x10) : nvgTransRGBA(col, 110));
            nvgText(args.vg, xi + w * 0.5f, y + h * 0.5f, i == 1 ? "MON" : "LVL", NULL);
        }
        return 2.f * w + gap;
    }

    // the VIEW button (click: the next view): VIEW and a 2 x 2 of boxes, the view's lit (smooth, static spectrum
    // | live spectrum, live scope), from x
    void drawViewTag(const DrawArgs& args, float x, float yc) {
        const int view = module ? clamp(module->laneView(), 0, 3) : 0;
        const float h = mm2px(3.6f), y = yc - h * 0.5f, pad = mm2px(0.7f), cw = 3.4f, ch = (h - 2.f * pad - 0.8f) * 0.5f;
        nvgFontSize(args.vg, 5.5f);
        float b[4];
        float tw = nvgTextBounds(args.vg, 0.f, 0.f, "VIEW", NULL, b);
        const float w = pad + tw + mm2px(0.9f) + 2.f * cw + 0.8f + pad;
        pressGlow(args, viewPressT, x, y, w, h, accent());
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, x, y, w, h, 1.f);
        if (hoverTag == 1) { nvgFillColor(args.vg, nvgTransRGBA(accent(), 22)); nvgFill(args.vg); }   // under the mouse: a faint fill
        nvgStrokeColor(args.vg, accent());
        nvgStrokeWidth(args.vg, 0.6f);
        nvgStroke(args.vg);
        // over a box: that view's name stands in for VIEW
        static const char* const vn[4] = {"WAVE", "SPEC", "LIVE", "SCOP"};
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, accent());
        nvgText(args.vg, x + pad, yc + 0.2f, hoverView >= 0 ? vn[hoverView] : "VIEW", NULL);
        const float gx = x + w - pad - 2.f * cw - 0.8f, gy = y + pad;
        vgX0 = gx; vgY0 = gy; vgW = cw + 0.8f; vgH = ch + 0.8f;
        for (int i = 0; i < 4; i++) {
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, gx + (i % 2) * vgW, gy + (i / 2) * vgH, cw, ch, 0.4f);
            if (i == view) { nvgFillColor(args.vg, accent()); nvgFill(args.vg); }
            else { nvgStrokeColor(args.vg, nvgTransRGBA(accent(), i == hoverView ? 230 : 100)); nvgStrokeWidth(args.vg, 0.4f); nvgStroke(args.vg); }
        }
        viewX0 = x; viewX1 = x + w;
    }

    // VIEW at the left, the tempo at the right
    void drawInfo(const DrawArgs& args, float W, int s) {
        float yc = mm2px(ls::yInfo + oled::kRow * 0.5f);
        char tmp[24] = "120 BPM";
        const char* src = "TEMPO";
        if (module) {
            if (module->source == 0) { src = "FREE"; tmp[0] = 0; }
            else {
                src = module->source == 2 ? "CLOCK" : "TEMPO";
                snprintf(tmp, sizeof tmp, "%.1f BPM", 60.f / module->beatNow);
            }
        }
        drawViewTag(args, 4.f, yc);
        drawRandTag(args, viewX1 + mm2px(1.5f), yc);   // RAND beside VIEW
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 8.f);
        nvgFillColor(args.vg, accent());
        nvgText(args.vg, W - 4.f, yc, tmp, NULL);
        float b[4];
        float w = tmp[0] ? nvgTextBounds(args.vg, 0.f, 0.f, tmp, NULL, b) + 4.f : 0.f;
        nvgFontSize(args.vg, 6.f);
        nvgFillColor(args.vg, oled::dim());
        nvgText(args.vg, W - 4.f - w, yc, src, NULL);
    }

    // each lane's RAND effect (mm), between the state dot and the readout: dim and empty with none; its label,
    // green and outlined while RAND is off, black on green while it is on. Click: the next effect (none,
    // 1/8 .. ORD, none); right-click: its mode and amount.
    static constexpr float kLaneBoxX = 7.3f, kLaneBoxW = 4.2f, kLaneBoxH = 3.3f;
    void drawLaneRand(const DrawArgs& args, int t, float yc) {
        const bool on = module && module->randOn[t];
        const float x = mm2px(kLaneBoxX), w = mm2px(kLaneBoxW), h = mm2px(kLaneBoxH), y = yc - h * 0.5f;
        NVGcolor g = oled::green();
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, x, y, w, h, 0.8f);
        const bool live = on && module->randAll;
        if (!live) { nvgStrokeColor(args.vg, nvgTransRGBA(g, on ? 160 : 90)); nvgStrokeWidth(args.vg, 0.5f); nvgStroke(args.vg); }
        if (!on) return;
        if (live) { nvgFillColor(args.vg, g); nvgFill(args.vg); }
        nvgFontSize(args.vg, 5.f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, live ? nvgRGB(0x10, 0x10, 0x10) : nvgTransRGBA(g, 170));
        nvgText(args.vg, x + w * 0.5f, yc + 0.2f, kRandLabel[clamp(module->randMode[t], 0, LP::RM_LEN - 1)], NULL);
    }

    // one track's loop: number and state dot; the waveform with the playing part, beats and playhead
    void drawLane(const DrawArgs& args, float W, int t, bool sel) {
        const EchoLooper* lp = module ? &module->loop[t] : NULL;
        float yT = mm2px(ls::yLanes + t * ls::kLaneH), yB = yT + mm2px(ls::kLaneH), yc = (yT + yB) * 0.5f;
        float x0 = mm2px(kLaneBoxX + kLaneBoxW + 1.f), x1 = W - 4.f, hh = (yB - yT) * 0.5f - 1.2f;
        if (sel) {
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, 2.f, yT + 0.3f, W - 4.f, yB - yT - 0.6f, 1.2f);
            nvgFillColor(args.vg, oled::amber(16));
            nvgFill(args.vg);
        }
        char num[4];
        snprintf(num, sizeof num, "%d", t + 1);
        nvgFontSize(args.vg, 7.f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        bool muted = module && module->params[LP::MUTE_PARAM + t].getValue() > 0.5f;
        nvgFillColor(args.vg, laneCol(t, sel ? 255 : muted ? 70 : 170));
        nvgText(args.vg, 4.f, yc, num, NULL);
        int st = lp ? (int)lp->st : previewHas(t) ? (t < 3 ? (int)EchoLooper::PLAY : (int)EchoLooper::STOP) : (int)EchoLooper::EMPTY;
        bool armed = lp && lp->arm != EchoLooper::A_NONE;
        if (st != EchoLooper::EMPTY || armed) {   // the state dot (blinking while armed)
            if (!armed || module->blink < 0.125f) {
                nvgBeginPath(args.vg);
                nvgCircle(args.vg, mm2px(5.6f), yc, 1.6f);
                nvgFillColor(args.vg, armed ? oled::litFill() : stateColor(st));
                nvgFill(args.vg);
            }
        }
        drawLaneRand(args, t, yc);
        // the live views: what leaves the track now, whatever its state; a loop's playhead over it
        int view = module ? module->laneView() : 0;
        if (view == 2 || view == 3) {
            if (view == 2) drawSpectrum(args, liveBand[t], liveHold[t], nvgRGBA(255, 255, 255, muted ? 40 : 110), t, sel, muted, x0, x1, yT, yB);
            else drawScope(args, t, sel, muted, x0, x1, yc, hh);
            if (lp && lp->len > 0 && lp->st != EchoLooper::REC) drawTracker(args, lp, t, x0, x1, yT, yB);
            return;
        }

        if (lp && lp->st == EchoLooper::REC) {   // recording: a red bar grows (to its length, else over 20 s)
            int target = lp->recTarget > 0 ? lp->recTarget : lp->recBeats > 0 && lp->curBeat > 0.f
                       ? (int)(lp->recBeats * lp->curBeat) : lp->cap;
            float f = clamp((float)lp->pos / std::max(1, target), 0.f, 1.f);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, x0, yc - 1.f, (x1 - x0) * f, 2.f);
            nvgFillColor(args.vg, kRecRed);
            nvgFill(args.vg);
            return;
        }
        bool has = lp ? lp->len > 0 : previewHas(t);
        if (!has) {   // empty: a dashed centre line
            nvgBeginPath(args.vg);
            for (float x = x0; x < x1; x += 4.f) { nvgMoveTo(args.vg, x, yc); nvgLineTo(args.vg, std::min(x + 2.f, x1), yc); }
            nvgStrokeColor(args.vg, oled::outline(70));
            nvgStrokeWidth(args.vg, 0.6f);
            nvgStroke(args.vg);
            return;
        }
        if (view == 1) {   // the whole loop's spectrum, still; the live one a faint line over it
            drawSpectrum(args, statBand[t], liveBand[t], nvgRGBA(255, 255, 255, muted ? 30 : 90), t, sel, muted, x0, x1, yT, yB);
            drawTracker(args, lp, t, x0, x1, yT, yB);
            return;
        }
        int zoom = module ? module->waveZoom : 1;
        if (lp && zoom > 1 && lp->len > 0) { drawZoomed(args, lp, t, sel, muted, zoom, x0, x1, yT, yB, yc, hh); return; }
        const int N = EchoLooper::kPeaks;
        float bw = (x1 - x0) / N;
        float lenF = lp ? (float)lp->len : 1.f, L = lp ? (float)module->playLen(t) : 1.f;
        float pk[N];
        char in[N];
        for (int c = 0; c < N; c++) {
            in[c] = (c + 0.5f) * lenF / N <= L;
            pk[c] = lp ? lp->peak[c] : previewPeak(t, c);
        }
        smoothWave(args, pk, in, N, x0, bw, yc, hh, t, sel, muted);
        drawTracker(args, lp, t, x0, x1, yT, yB);
    }

    // the whole loop's beat ticks (bars longer) and its playhead, over a lane
    void drawTracker(const DrawArgs& args, const EchoLooper* lp, int t, float x0, float x1, float yT, float yB) {
        if (lp && lp->beats > 1 && lp->beatLen > 0.0 && lp->beats <= 64) {   // beat ticks (bars longer)
            nvgBeginPath(args.vg);
            for (int k = 1; k < lp->beats; k++) {
                float x = x0 + (x1 - x0) * (float)(k * lp->beatLen / lp->len), tk = k % 4 == 0 ? 1.8f : 0.9f;
                nvgMoveTo(args.vg, x, yT + 0.5f); nvgLineTo(args.vg, x, yT + 0.5f + tk);
                nvgMoveTo(args.vg, x, yB - 0.5f); nvgLineTo(args.vg, x, yB - 0.5f - tk);
            }
            nvgStrokeColor(args.vg, oled::outline(150));
            nvgStrokeWidth(args.vg, 0.6f);
            nvgStroke(args.vg);
        }
        float f = lp ? (float)lp->pos / std::max(1, lp->len) : 0.15f + 0.1f * t;
        bool playing = lp ? lp->playing() : t < 3;
        float px = x0 + (x1 - x0) * clamp(f, 0.f, 1.f);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, px, yT + 0.8f);
        nvgLineTo(args.vg, px, yB - 0.8f);
        nvgStrokeColor(args.vg, lp && lp->st == EchoLooper::DUB ? kRecRed : playing ? accent() : oled::dim());
        nvgStrokeWidth(args.vg, 1.f);
        nvgStroke(args.vg);
    }

    // zoomed in: 1 / zoom of the loop, following the playhead (held at the lane's centre; the view wraps round
    // the loop's top), each column the peak of the audio under it, read from the loop itself
    void drawZoomed(const DrawArgs& args, const EchoLooper* lp, int t, bool sel, bool muted, int zoom,
                    float x0, float x1, float yT, float yB, float yc, float hh) {
        const int len = lp->len, L = module->playLen(t);
        if ((int)lp->l.size() < len || (int)lp->r.size() < len) return;
        const double span = (double)len / zoom, start = lp->pos - span * 0.5;
        auto wrap = [&](double i) { double m = std::fmod(i, (double)len); return m < 0.0 ? m + len : m; };
        const float colW = 1.5f;
        const int cols = std::max(1, (int)((x1 - x0) / colW));
        const double per = span / cols;
        const int taps = std::max(1, std::min(48, (int)per));
        std::vector<float> pk(cols, 0.f);
        std::vector<char> in(cols);
        for (int c = 0; c < cols; c++) {
            double a = start + c * per;
            in[c] = wrap(a + per * 0.5) < L;
            for (int k = 0; k < taps; k++) {
                int i = (int)wrap(a + per * k / taps);
                pk[c] = std::max(pk[c], std::max(std::fabs(lp->l[i]), std::fabs(lp->r[i])));
            }
        }
        smoothWave(args, pk.data(), in.data(), cols, x0, colW, yc, hh, t, sel, muted);
        float w = cols * colW;
        if (lp->beats > 0 && lp->beatLen > 0.0) {   // beat ticks in view (bars longer; the loop's top longest)
            nvgBeginPath(args.vg);
            for (int k = 0; k < lp->beats; k++) {
                double u = wrap(k * lp->beatLen - start) / span;
                if (u > 1.0) continue;
                float x = x0 + w * (float)u, tk = k == 0 ? 3.f : k % 4 == 0 ? 1.8f : 0.9f;
                nvgMoveTo(args.vg, x, yT + 0.5f); nvgLineTo(args.vg, x, yT + 0.5f + tk);
                nvgMoveTo(args.vg, x, yB - 0.5f); nvgLineTo(args.vg, x, yB - 0.5f - tk);
            }
            nvgStrokeColor(args.vg, oled::outline(150));
            nvgStrokeWidth(args.vg, 0.6f);
            nvgStroke(args.vg);
        }
        float px = x0 + w * 0.5f;
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, px, yT + 0.8f);
        nvgLineTo(args.vg, px, yB - 0.8f);
        nvgStrokeColor(args.vg, lp->st == EchoLooper::DUB ? kRecRed : lp->playing() ? accent() : oled::dim());
        nvgStrokeWidth(args.vg, 1.f);
        nvgStroke(args.vg);
    }

    // the smooth view: a translucent wave through the column peaks, mirrored round yc, outlined; the playing
    // part (in[c]) bright, the rest dim
    void smoothWave(const DrawArgs& args, const float* pk, const char* in, int n, float x0, float cw, float yc, float hh,
                    int t, bool sel, bool muted) {
        if (n < 2) return;
        std::vector<float> h(n);
        for (int c = 0; c < n; c++) {   // softened a little across its neighbours
            float v = 0.5f * pk[c] + 0.25f * (pk[std::max(0, c - 1)] + pk[std::min(n - 1, c + 1)]);
            h[c] = std::max(0.4f, std::min(1.f, v) * hh);
        }
        auto edge = [&](float sgn) {   // left to right along the top (sgn -1) or right to left along the foot (+1)
            for (int k = 0; k < n; k++) {
                int c = sgn < 0.f ? k : n - 1 - k, p = sgn < 0.f ? c - 1 : c + 1;
                float x = x0 + (c + 0.5f) * cw, y = yc + sgn * h[c];
                if (k == 0) { nvgLineTo(args.vg, x, y); continue; }
                float xp = x0 + (p + 0.5f) * cw, yp = yc + sgn * h[p];
                nvgQuadTo(args.vg, xp, yp, (xp + x) * 0.5f, (yp + y) * 0.5f);
                if (k == n - 1) nvgLineTo(args.vg, x, y);
            }
        };
        const float xe = x0 + n * cw;
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, x0, yc);
        edge(-1.f);
        nvgLineTo(args.vg, xe, yc);
        edge(1.f);
        nvgClosePath(args.vg);
        nvgFillColor(args.vg, laneCol(t, muted ? 22 : 34));   // all of it dim
        nvgFill(args.vg);
        if (muted) return;
        for (int a = 0; a < n;) {   // each playing run bright
            if (!in[a]) { a++; continue; }
            int b = a;
            while (b < n && in[b]) b++;
            nvgSave(args.vg);
            nvgIntersectScissor(args.vg, x0 + a * cw, yc - hh - 2.f, (b - a) * cw, 2.f * hh + 4.f);
            nvgFillColor(args.vg, laneCol(t, sel ? 120 : 85));
            nvgFill(args.vg);
            nvgStrokeColor(args.vg, laneCol(t, sel ? 235 : 180));
            nvgStrokeWidth(args.vg, 0.6f);
            nvgStroke(args.vg);
            nvgRestore(args.vg);
            a = b;
        }
    }

    // a spectrum across the lane (low .. high): a translucent fill under the curve through the bands, outlined;
    // a second line over it (peaks, or the live spectrum)
    void drawSpectrum(const DrawArgs& args, const float* v, const float* line, NVGcolor lineCol, int t, bool sel, bool muted,
                      float x0, float x1, float yT, float yB) {
        const float yb = yB - 0.6f, hgt = yB - yT - 1.4f;
        auto curve = [&](const float* u, bool start) {
            float xp = x0, yp = yb - u[0] * hgt;
            if (start) nvgMoveTo(args.vg, xp, yp); else nvgLineTo(args.vg, xp, yp);
            for (int b = 1; b < kBands; b++) {
                float x = x0 + (x1 - x0) * b / (kBands - 1), y = yb - u[b] * hgt;
                nvgQuadTo(args.vg, xp, yp, (xp + x) * 0.5f, (yp + y) * 0.5f);
                xp = x; yp = y;
            }
            nvgLineTo(args.vg, xp, yp);
        };
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, x0, yb);
        curve(v, false);
        nvgLineTo(args.vg, x1, yb);
        nvgClosePath(args.vg);
        nvgFillColor(args.vg, laneCol(t, muted ? 25 : sel ? 105 : 75));
        nvgFill(args.vg);
        nvgBeginPath(args.vg);
        curve(v, true);
        nvgStrokeColor(args.vg, laneCol(t, muted ? 60 : sel ? 240 : 185));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        if (!line) return;
        nvgBeginPath(args.vg);
        curve(line, true);
        nvgStrokeColor(args.vg, lineCol);
        nvgStrokeWidth(args.vg, 0.5f);
        nvgStroke(args.vg);
    }

    // the live scope: what leaves the track, held still on a rising zero crossing; a translucent fill to the centre
    void drawScope(const DrawArgs& args, int t, bool sel, bool muted, float x0, float x1, float yc, float hh) {
        if (!module) return;
        const int N = LP::kScope, show = N / 2, pts = 512, w = module->scopeW;
        const float* s = module->scope[t];
        auto at = [&](int i) { return s[(w + i) & (N - 1)]; };   // i = 0: the oldest sample
        int start = N - show;
        for (int i = N - show; i > 0; i--) if (at(i - 1) <= 0.f && at(i) > 0.f) { start = i; break; }
        auto trace = [&]() {
            for (int k = 0; k < pts; k++)
                nvgLineTo(args.vg, x0 + (x1 - x0) * k / (pts - 1), yc - clamp(at(start + k * show / pts), -1.f, 1.f) * hh);
        };
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, x0, yc);
        trace();
        nvgLineTo(args.vg, x1, yc);
        nvgClosePath(args.vg);
        nvgFillColor(args.vg, laneCol(t, muted ? 20 : sel ? 70 : 50));
        nvgFill(args.vg);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, x0, yc - clamp(at(start), -1.f, 1.f) * hh);
        trace();
        nvgStrokeColor(args.vg, laneCol(t, muted ? 60 : sel ? 240 : 190));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
    }

    // scroll over the lanes: zoom in / out (x1 .. x32, every lane together; the smooth view)
    void onHoverScroll(const HoverScrollEvent& e) override {
        float y = e.pos.y - mm2px(ls::yLanes);
        if (!module || module->laneView() != 0 || e.scrollDelta.y == 0.f || y < 0.f || y >= mm2px(kTracks * ls::kLaneH)) return;
        module->waveZoom = clamp(e.scrollDelta.y > 0.f ? module->waveZoom * 2 : module->waveZoom / 2, 1, 32);
        e.consume(this);
    }

    // click a lane: select its track; VIEW: the next view; RAND: on, then the next mode (right-click: its menu)
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS) return;
        bool info = module && e.pos.y >= mm2px(ls::yInfo) && e.pos.y < mm2px(ls::yInfo + oled::kRow);
        if (info && e.pos.x >= randX0 && e.pos.x <= randX1) {
            const int s = module->sel();
            const int p = padAt(e.pos);
            if (e.button == GLFW_MOUSE_BUTTON_RIGHT) randMenu(module, s);
            else if (e.button == GLFW_MOUSE_BUTTON_LEFT && p >= 0) {   // a pad: its effect on every lane
                randPressT = system::getTime();
                module->setAllRand(p);
            }
            else if (e.button == GLFW_MOUSE_BUTTON_LEFT && module->randAll) {   // deal every lane an effect, kept by the scene
                randPressT = system::getTime();
                module->dealRand();
            }
            e.consume(this);
            return;
        }
        float ly = e.pos.y - mm2px(ls::yLanes);   // a lane's RAND box
        if (module && ly >= 0.f && ly < mm2px(kTracks * ls::kLaneH)
            && e.pos.x >= mm2px(kLaneBoxX) && e.pos.x <= mm2px(kLaneBoxX + kLaneBoxW)) {
            const int t = clamp((int)(ly / mm2px(ls::kLaneH)), 0, kTracks - 1);
            if (e.button == GLFW_MOUSE_BUTTON_RIGHT) randMenu(module, t);
            else if (e.button == GLFW_MOUSE_BUTTON_LEFT) {   // the next effect: none, 1/8 .. ORD, none
                if (!module->randOn[t]) { module->randOn[t] = true; module->randMode[t] = 0; }
                else if (module->randMode[t] < LP::RM_LEN - 1) module->randMode[t]++;
                else module->randOn[t] = false;
                module->lastSlice[t] = -1;
                module->randChanged();
            }
            e.consume(this);
            return;
        }
        if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
        if (info && e.pos.x >= viewX0 && e.pos.x <= viewX1) {   // a box: that view; elsewhere on VIEW: the next one
            const int v = viewAt(e.pos);
            module->laneView() = v >= 0 ? v : (module->laneView() + 1) % 4;
            viewPressT = system::getTime();
            e.consume(this);
            return;
        }
        float y = e.pos.y - mm2px(ls::yLanes);
        if (y < 0.f || y >= mm2px(kTracks * ls::kLaneH)) return;
        if (module) module->params[LP::SEL_PARAM].setValue((float)clamp((int)(y / mm2px(ls::kLaneH)), 0, kTracks - 1));
        e.consume(this);
    }
};

// A track tile (as the MATRIX-DRUM pads): number, state, length; red while recording, the play position along
// its foot; the selected one outlined. Click: select.
struct LTile : OpaqueWidget {
    LP* module = NULL;
    int t = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        const EchoLooper* lp = module ? &module->loop[t] : NULL;
        bool sel = module ? module->sel() == t : t == 0;
        int st = lp ? (int)lp->st : previewHas(t) ? (t < 3 ? (int)EchoLooper::PLAY : (int)EchoLooper::STOP) : (int)EchoLooper::EMPTY;
        bool rec = st == EchoLooper::REC || st == EchoLooper::DUB;
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.5f);
        bool has = lp ? lp->len > 0 : previewHas(t);   // holds a recording: a faint glow in its colour
        nvgFillColor(args.vg, rec ? nvgLerpRGBA(oled::offFill(), kRecRed, 0.55f)
                              : has ? nvgLerpRGBA(oled::offFill(), kPatOnColor[t], 0.2f) : oled::offFill());
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, trackCol(t, sel ? 255 : 110));
        nvgStrokeWidth(args.vg, sel ? 1.1f : 0.7f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        char num[4];
        snprintf(num, sizeof num, "%02d", t + 1);
        nvgFontSize(args.vg, 10.f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        // the number: its cyan while the track monitors, back to its colour as the take closes
        nvgFillColor(args.vg, module && module->trackMon(t) ? kMonRamp[t] : rec ? oled::amber() : trackCol(t));
        nvgText(args.vg, 3.f, 2.5f, num, NULL);
        nvgFontSize(args.vg, 7.f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
        nvgFillColor(args.vg, rec ? oled::amber() : stateColor(st));
        nvgText(args.vg, 3.f, H - 2.5f, kStateName[st], NULL);
        char len[16] = "";
        if (lp && lp->len > 0 && lp->st != EchoLooper::REC) {
            if (lp->beats > 0) snprintf(len, sizeof len, "%d BT", lp->loopBeats > 0 ? lp->loopBeats : lp->beats);
            else snprintf(len, sizeof len, "%.2f s", module->playLen(t) / lp->sr);
        } else if (!lp && previewHas(t)) snprintf(len, sizeof len, "%d BT", 4 << (t % 2));
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
        nvgFillColor(args.vg, trackCol(t, 170));
        nvgText(args.vg, W - 3.f, H - 2.5f, len, NULL);
        bool mon = module ? module->params[LP::MON_PARAM + t].getValue() > 0.5f || module->hearIn() : true;
        bool connected = module && module->inputs[LP::IN_INPUT + t].isConnected();
        bool muted = module && module->params[LP::MUTE_PARAM + t].getValue() > 0.5f;
        if (muted || (mon && connected)) {
            nvgFontSize(args.vg, 6.f);
            nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
            nvgFillColor(args.vg, muted ? kRecRed : oled::dim());
            nvgText(args.vg, W - 3.f, 3.f, muted ? "MUTE" : "MON", NULL);
        }
        bool playing = lp ? lp->playing() : t < 3;
        if (playing) {   // where it plays
            float L = lp ? (float)std::max(1, module->playLen(t)) : 1.f;
            float f = lp ? clamp(lp->pos / L, 0.f, 1.f) : 0.3f + 0.1f * t;
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 2.5f, H - 1.6f, (W - 5.f) * f, 0.9f);
            // amber (cyan on MON): on a red (recording) tile too
            nvgFillColor(args.vg, module && module->hearIn() ? monCyan() : oled::amber());
            nvgFill(args.vg);
        }
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[LP::SEL_PARAM].setValue((float)t);
            e.consume(this);
        }
    }
};

// A scene cell U1-U8 (grey while empty, amber when stored; the selected one lit), or STORE (k == -1, blinks while
// armed: the next scene clicked stores which tracks play now). While armed every scene blinks, the selected one
// twice as fast; a scene just stored to blinks fast for a moment. A scene never starts or stops a track.
// Right-click a scene: store or clear it.
struct LSceneCell : OpaqueWidget {
    LP* module = NULL;
    int k = 0;
    double confirmT = -1.0;   // when this scene was last stored to (the confirm blink)
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        bool store = k < 0;
        bool armed = module && module->storeArm;
        double now = glfwGetTime();
        bool slow = std::fmod(now, 0.5) < 0.25, fast = std::fmod(now, 0.25) < 0.125;
        bool empty = !store && (!module || !module->sceneUsed[k]);
        bool sel = !store && module && module->curScene == k;
        bool confirm = !store && confirmT >= 0.0 && now - confirmT < 0.6;
        bool on = store ? armed && slow : confirm ? fast : armed ? (sel ? fast : slow) : sel;
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.2f);
        nvgFillColor(args.vg, on ? oled::litFill() : oled::offFill());
        nvgFill(args.vg);
        int oa = on || (armed && !store) ? 255 : empty ? 90 : 140;
        nvgStrokeColor(args.vg, empty && !on ? nvgRGBA(170, 170, 170, oa) : oled::outline(oa));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        char lab[8];
        if (store) snprintf(lab, sizeof lab, "STORE");
        else snprintf(lab, sizeof lab, "U%d", k + 1);
        nvgFontSize(args.vg, 6.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? oled::ink() : empty ? nvgRGBA(170, 170, 170, armed ? 220 : 150) : oled::amber());
        nvgText(args.vg, W * 0.5f, H * 0.5f, lab, NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || !module) return;
        LP* m = module;
        if (e.button == GLFW_MOUSE_BUTTON_RIGHT && k >= 0) {
            int u = k;
            ui::Menu* menu = createMenu();
            menu->addChild(createMenuLabel("Scene U" + std::to_string(u + 1)));
            LSceneCell* self = this;
            menu->addChild(createMenuItem("Store the tracks playing now", "", [=]() {
                m->storeScene(u); m->storeArm = false; self->confirmT = glfwGetTime();
            }));
            menu->addChild(createMenuItem("Clear", "", [=]() {
                m->sceneUsed[u] = false; m->sceneMask[u] = 0; m->sRandUsed[u] = false;
                if (m->curScene == u) m->curScene = -1;
            }));
            e.consume(this);
            return;
        }
        if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
        if (k < 0) m->storeArm = !m->storeArm;
        else if (m->storeArm) { m->storeScene(k); m->storeArm = false; confirmT = glfwGetTime(); }
        else m->sceneReq.store(k);
        e.consume(this);
    }
};

// REC / PLAY: a cell in its own colour, dim (outline and label) until engaged, filled while it is
// (labelY: the label's height, a fraction of the cell; inset: a dark outline when lit, for a cell inside another)
struct LTintCell : oled::Cell {
    NVGcolor tint = kRecRed;
    float labelY = 0.5f;
    bool inset = false;
    void drawLayer(const DrawArgs& args, int layer) override {
        ParamWidget::drawLayer(args, layer);
        if (layer != 1) return;
        bool on = lit();
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.2f);
        nvgFillColor(args.vg, on ? tint : oled::offFill());
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, on && inset ? nvgTransRGBA(oled::ink(), 200) : nvgTransRGBA(tint, on ? 255 : 120));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? oled::ink() : nvgTransRGBA(tint, 190));
        nvgText(args.vg, W * 0.5f, H * labelY, label, NULL);
    }
};

// COUNT-IN: one cell that steps OFF > 1 > 2 > 3 > 4 beats (click; scroll both ways; right-click lists them)
struct LCountCell : oled::Cell {
    char text[12] = "COUNT OFF";
    LCountCell() { label = text; latch = true; }
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

// The JAM display, small, in the bottom-left corner: the square MIDI cell (on / off, laid over it by the widget)
// at the left; right of it MIDI IN over MIDI OUT, each a dim title over its device (click = the port menu).
// In mm:
namespace lm {
const float kPort = 5.5f;                   // a port: title + device
const float kH = 2.f * kPort + 1.f;
const float kSq = oled::kCellH + 2.f;       // the MIDI cell's side, centred top to bottom
const float xCell = 1.5f, yCell = (kH - kSq) * 0.5f;
const float xText = xCell + kSq + 1.5f;     // the ports
}
struct LMidiDisplay : TransparentWidget {
    LP* module = NULL;
    void port(const DrawArgs& args, midi::Port* p, const char* title, float y0) {
        std::string dev = "(no device)";
        if (p) { if (p->getDeviceId() >= 0) dev = p->getDeviceName(p->getDeviceId()); }
        else dev = "Maschine Jam - 1";
        const float x = mm2px(lm::xText);
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
        oled::divider(args, mm2px(lm::xText), mm2px(0.5f + lm::kPort), W - 3.f, mm2px(0.5f + lm::kPort));
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W - 2.f, H);   // a long device name stops at the edge
        port(args, module ? &module->midiIn : NULL, "MIDI IN", 0.5f);
        port(args, module ? &module->midiOut : NULL, "MIDI OUT", 0.5f + lm::kPort);
        nvgResetScissor(args.vg);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS || !module || e.pos.x < mm2px(lm::xText)) return;
        if (e.pos.y < mm2px(0.5f + lm::kPort)) oled::midiMenu(&module->midiIn, "MIDI IN port (the JAM)");
        else oled::midiMenu(&module->midiOut, "MIDI OUT port (the JAM's lights)");
        e.consume(this);
    }
};

struct SynTHBloKLoopWidget : ModuleWidget {
    oled::Cell* undoCell = NULL;
    oled::Cell* lenCell[kLenN] = {};

    SynTHBloKLoopWidget(LP* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKLoop.svg")));
        addChild(songlink::RowBadge::create(box.size, module));   // the Composer row conducting it
        using namespace rack::componentlibrary;
        const float PW = 254.f;    // 50 HP
        const float DX = 20.32f;   // everything right of the input column (the inputs and their gain knobs)
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PW - 3.5f, 3.5f))));
        LTitle* title = createWidget<LTitle>(mm2px(Vec(8.5f, 0.f)));
        title->box.size = mm2px(Vec(60.f, 7.f));
        addChild(title);
        auto label = [&](float cx, float cy, const char* text) {
            LLabel* l = createWidget<LLabel>(mm2px(Vec(cx - 12.f, cy - 2.f)));
            l->box.size = mm2px(Vec(24.f, 4.f)); l->text = text;
            addChild(l);
        };

        // ---------------- far left: each track's input and its gain knob, level with its row of pads ----------------
        // (its plate, drawn in the SVG, is as wide as the MIDI screen under it: x 2.5 .. 23.5; titles inside it)
        const float xJ = 7.5f, xK = 18.f;
        label(xJ, 14.f, "IN");
        label(xK, 14.f, "GAIN");
        LMonLed* led = createWidget<LMonLed>(mm2px(Vec(0.5f * (xJ + xK) - 0.8f, 14.f - 0.8f)));
        led->box.size = mm2px(Vec(1.6f, 1.6f)); led->module = module;
        addChild(led);
        for (int t = 0; t < kTracks; t++) {
            float y = 22.8f + 7.4f * t;
            addInput(createInputCentered<SmallJack>(mm2px(Vec(xJ, y)), module, LP::IN_INPUT + t));
            LRing* r = createWidget<LRing>(mm2px(Vec(xK - 3.6f, y - 3.6f)));
            r->box.size = mm2px(Vec(7.2f, 7.2f)); r->module = module; r->param = LP::GAIN_PARAM + t; r->width = 1.3f; r->color = t;
            addChild(r);
            addParam(createParamCentered<Trimpot>(mm2px(Vec(xK, y)), module, LP::GAIN_PARAM + t));
        }
        // LVL / MON: the strips and what is heard, on the A-H row (the plate reaches down to it)
        // (MON under the inputs, LVL under the gains)
        for (int i = 0; i < 2; i++) {
            LInBtn* b = createWidget<LInBtn>(mm2px(Vec((i == 0 ? xK : xJ) - 3.3f, 85.5f)));
            b->box.size = mm2px(Vec(6.6f, 6.6f)); b->module = module;
            b->param = LP::STRIP_PARAM; b->mute = i == 1;
            addChild(b);
        }

        // ---------------- left: laid out like the JAM ----------------
        // track buttons 1-8, the grid (a row per track, its output beside it), monitor buttons A-H and
        // gain strips under the columns: every column lines up with a pad column
        auto colX = [&](int c) { return DX + 4.9f + 7.4f * c; };
        for (int t = 0; t < kTracks; t++) {
            LTrackBtn* b = createWidget<LTrackBtn>(mm2px(Vec(colX(t), 10.5f)));
            b->box.size = mm2px(Vec(6.6f, 6.6f)); b->module = module; b->t = t;
            addChild(b);
        }
        for (int r = 0; r < kTracks; r++)
            for (int c = 0; c < 8; c++) {
                LPad* p = createWidget<LPad>(mm2px(Vec(colX(c), 19.5f + 7.4f * r)));
                p->box.size = mm2px(Vec(6.6f, 6.6f)); p->module = module; p->r = r; p->c = c;
                addChild(p);
            }
        // the pads' mode, between the grid and A-H: JUMP over A-B, GATE over C-D
        for (int i = 0; i < 2; i++) {
            oled::Cell* mc = createParam<oled::Cell>(mm2px(Vec(colX(2 * i), 80.15f)), module, LP::PAD_MODE_PARAM);
            mc->box.size = mm2px(Vec(colX(1) - colX(0) + 6.6f, oled::kCellH)); mc->module = module;
            mc->setTo = i; mc->label = i == 0 ? "JUMP" : "GATE";
            addParam(mc);
        }
        // the right column: RAND over the track outputs; then CHOP (arrows and display)
        LRandBtn* rb = createWidget<LRandBtn>(mm2px(Vec(DX + 65.8f, 10.5f)));
        rb->box.size = mm2px(Vec(7.6f, 6.6f)); rb->module = module;
        addChild(rb);
        label(DX + 69.6f, 18.5f, "OUT");
        for (int r = 0; r < kTracks; r++)
            addOutput(createOutputCentered<SmallJack>(mm2px(Vec(DX + 69.6f, 22.8f + 7.4f * r)), module, LP::OUT_OUTPUT + r));
        for (int i = 0; i < 2; i++) {
            LArrowBtn* a = createWidget<LArrowBtn>(mm2px(Vec(DX + 65.8f, i == 0 ? 85.5f : 117.f)));
            a->box.size = mm2px(Vec(7.6f, 6.6f)); a->module = module; a->up = i == 0;
            addChild(a);
        }
        LChopDisplay* ld = createWidget<LChopDisplay>(mm2px(Vec(DX + 65.8f, 93.5f)));
        ld->box.size = mm2px(Vec(7.6f, 22.f)); ld->module = module;
        addChild(ld);
        for (int t = 0; t < kTracks; t++) {
            LMuteBtn* m = createWidget<LMuteBtn>(mm2px(Vec(colX(t), 85.5f)));
            m->box.size = mm2px(Vec(6.6f, 6.6f)); m->module = module; m->t = t;
            addChild(m);
            LGainStrip* g = createParam<LGainStrip>(mm2px(Vec(colX(t) + 0.9f, 93.5f)), module, LP::GAIN_PARAM + t);
            g->box.size = mm2px(Vec(4.8f, 30.f)); g->lmodule = module; g->t = t;
            addParam(g);
        }

        // ---------------- right: the screen ----------------
        const float OX = DX + 79.5f, OY = 9.5f, OW = PW - OX - 3.5f;
        const float CG = ls::kCG, CH = oled::kCellH, inner = OW - 2.f * ls::kPad;
        LScreen* scr = createWidget<LScreen>(mm2px(Vec(OX, OY)));
        scr->box.size = mm2px(Vec(OW, ls::kH)); scr->module = module;
        addChild(scr);
        auto cell = [&](float x, float y, float w, int param, const char* lab) -> oled::Cell* {
            oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(OX + x, OY + y)), module, param);
            c->box.size = mm2px(Vec(w, CH)); c->module = module; c->label = lab;
            addParam(c);
            return c;
        };
        // on the info row, after VIEW and RAND: COUNT, then the record length (red; the selected track's: the
        // cells follow the selection)
        const float u9 = (inner - 8.f * CG) / 9.f;
        const float yIc = ls::yInfo + (oled::kRow - ls::kInfoCellH) * 0.5f;
        LCountCell* cc = createParam<LCountCell>(mm2px(Vec(OX + ls::xCount, OY + yIc)), module, LP::COUNTIN_PARAM);
        cc->box.size = mm2px(Vec(ls::kCountW, ls::kInfoCellH)); cc->module = module; cc->fontSize = 6.f;
        addParam(cc);
        for (int i = 0; i < kLenN; i++) {
            LTintCell* c = createParam<LTintCell>(mm2px(Vec(OX + ls::xCount + ls::kCountW + CG + i * (ls::kLenW + CG), OY + yIc)),
                                                  module, LP::LEN_PARAM);
            c->box.size = mm2px(Vec(ls::kLenW, ls::kInfoCellH)); c->module = module;
            c->label = kLenName[i]; c->setTo = kLenBeats[i]; c->fontSize = 6.f;
            c->tint = nvgRGB(0xf0, 0x50, 0x3c);
            addParam(c);
            lenCell[i] = c;
        }
        // scenes U1-U8 | STORE
        for (int k = 0; k <= 8; k++) {
            LSceneCell* c = createWidget<LSceneCell>(mm2px(Vec(OX + ls::kPad + k * (u9 + CG), OY + ls::yScene)));
            c->box.size = mm2px(Vec(u9, CH)); c->module = module; c->k = k < 8 ? k : -1;
            addChild(c);
        }
        // the track tiles, 2 rows of 4
        const float tw = (inner - 3.f * CG) / 4.f;
        for (int t = 0; t < kTracks; t++) {
            LTile* tl = createWidget<LTile>(mm2px(Vec(OX + ls::kPad + (t % 4) * (tw + CG), OY + ls::yTiles + (t / 4) * (ls::kTileH + ls::kGap))));
            tl->box.size = mm2px(Vec(tw, ls::kTileH)); tl->module = module; tl->t = t;
            addChild(tl);
        }
        // REC | PLAY | CLEAR | ONCE | UNDO for the selected track: tall cells, REC red, PLAY green, CLEAR yellow
        const NVGcolor kClearYellow = nvgRGB(0xff, 0xe1, 0x3a);
        struct TC { const char* label; int param; };
        const TC tc[5] = {{"REC", LP::REC_PARAM}, {"PLAY", LP::PLAY_PARAM}, {"ONCE", LP::ONCE_PARAM},
                          {"UNDO", LP::UNDO_PARAM}, {"CLEAR", LP::CLEAR_PARAM}};
        static const int kSlot[5] = {0, 1, 3, 4, 2};   // each cell's place in the row: CLEAR right of PLAY
        const float xw = (inner - 4.f * CG) / 5.f;
        for (int i = 0; i < 5; i++) {
            float x = ls::kPad + kSlot[i] * (xw + CG);
            oled::Cell* c;
            if (i < 2 || i == 4) {
                LTintCell* t = createParam<LTintCell>(mm2px(Vec(OX + x, OY + ls::yTrans)), module, tc[i].param);
                t->module = module; t->label = tc[i].label;
                t->tint = i == 0 ? kRecRed : i == 1 ? oled::green() : kClearYellow;
                t->labelY = 0.27f;   // ALL 8 sits in its lower half
                addParam(t);
                c = t;
            } else c = cell(x, ls::yTrans, xw, tc[i].param, tc[i].label);
            c->box.size = mm2px(Vec(xw, ls::kTransH));
            c->fontSize = 7.f;
            if (module) {
                if (i == 0) c->flash = &module->recFlash;
                if (i == 1) c->flash = &module->playFlash;
                if (i == 2) { c->flash = &module->onceFlash; c->green = true; }
            }
            if (i == 3) undoCell = c;
        }
        // ALL, inside REC's lower half: record on all 8 tracks at once
        {
            const float ax = OX + ls::kPad + 0.9f, ay = OY + ls::yTrans + ls::kTransH * 0.5f;
            LTintCell* a = createParam<LTintCell>(mm2px(Vec(ax, ay)), module, LP::REC_ALL_PARAM);
            a->box.size = mm2px(Vec(xw - 1.8f, ls::kTransH * 0.5f - 0.9f));
            a->module = module; a->label = "ALL 8"; a->fontSize = 5.5f; a->inset = true;
            if (module) a->flash = &module->allRecFlash;
            addParam(a);
        }
        // ALL, inside PLAY's and CLEAR's lower halves: play / clear all 8 tracks at once
        for (int k = 0; k < 2; k++) {
            const float ax = OX + ls::kPad + (k == 0 ? 1.f : 2.f) * (xw + CG) + 0.9f, ay = OY + ls::yTrans + ls::kTransH * 0.5f;
            LTintCell* a = createParam<LTintCell>(mm2px(Vec(ax, ay)), module, k == 0 ? LP::PLAY_ALL_PARAM : LP::CLEAR_ALL_PARAM);
            a->box.size = mm2px(Vec(xw - 1.8f, ls::kTransH * 0.5f - 0.9f));
            a->module = module; a->label = "ALL 8"; a->fontSize = 5.5f; a->inset = true;
            a->tint = k == 0 ? oled::green() : kClearYellow;
            if (module && k == 0) a->flash = &module->allPlayFlash;
            addParam(a);
        }

        // ---------------- right, bottom: TEMPO + CLOCK | MASTER + MIX L / R ----------------
        const float Y = 117.f, YL = 109.6f;
        auto plateKnob = [&](float x, int param, const char* name) {
            addChild(KnobTicks::create(Vec(x, Y)));
            LRing* r = createWidget<LRing>(mm2px(Vec(x - 5.5f, Y - 5.5f)));
            r->box.size = mm2px(Vec(11.f, 11.f)); r->module = module; r->param = param;
            addChild(r);
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, Y)), module, param));
            label(x, YL, name);
        };
        plateKnob(DX + 88.f, LP::TEMPO_PARAM, "TEMPO");
        label(DX + 101.f, YL, "CLOCK");
        addInput(createInputCentered<SmallJack>(mm2px(Vec(DX + 101.f, Y)), module, LP::CLOCK_INPUT));
        // RUN (lit green while running) | RESET: the button is the jack's label (as on the JAM module)
        struct RB { const char* label; int param, jack; bool latch; };
        const RB rbs[2] = {{"RUN", LP::RUN_PARAM, LP::RUN_INPUT, true}, {"RESET", LP::RESET_PARAM, LP::RESET_INPUT, false}};
        for (int i = 0; i < 2; i++) {
            float x = DX + 113.f + 12.f * i;
            JAmberBtn<LP>* b = createParam<JAmberBtn<LP>>(mm2px(Vec(x - 5.f, 106.6f)), module, rbs[i].param);
            b->box.size = mm2px(Vec(10.f, 5.f));
            b->jam = module; b->label = rbs[i].label; b->latch = rbs[i].latch;
            b->green = i == 0; b->jack = rbs[i].jack; b->fontSize = 6.5f;
            addParam(b);
            addInput(createInputCentered<SmallJack>(mm2px(Vec(x, Y)), module, rbs[i].jack));
        }
        // REC .. CLEAR triggers (mono: the selected track; poly: one channel per track)
        static const char* const kJackName[5] = {"REC", "PLAY", "ONCE", "UNDO", "CLEAR"};
        for (int i = 0; i < 5; i++) {
            float x = DX + 139.f + 9.5f * i;
            label(x, YL, kJackName[i]);
            addInput(createInputCentered<SmallJack>(mm2px(Vec(x, Y)), module, LP::REC_INPUT + i));
        }
        // the JAM, bottom-left corner under the inputs: the square MIDI cell (the JAM on / off), then MIDI IN / OUT
        // (click: the port)
        const float MX = 2.5f, MW = 21.f, MY = 124.8f - lm::kH;
        LMidiDisplay* md = createWidget<LMidiDisplay>(mm2px(Vec(MX, MY)));
        md->box.size = mm2px(Vec(MW, lm::kH)); md->module = module;
        addChild(md);
        oled::Cell* jc = createParam<oled::Cell>(mm2px(Vec(MX + lm::xCell, MY + lm::yCell)), module, LP::MIDI_ON_PARAM);
        jc->box.size = mm2px(Vec(lm::kSq, lm::kSq)); jc->module = module; jc->label = "MIDI";
        jc->latch = true; jc->green = true;
        addParam(jc);
        plateKnob(DX + 196.f, LP::MASTER_PARAM, "MASTER");
        const float xo[2] = {DX + 211.f, DX + 222.f};
        const char* const on[2] = {"MIX L", "MIX R"};
        for (int i = 0; i < 2; i++) {
            label(xo[i], YL, on[i]);
            addOutput(createOutputCentered<SmallJack>(mm2px(Vec(xo[i], Y)), module, LP::MIX_L_OUTPUT + i));
            addChild(JackLed::create(Vec(xo[i] + 3.9f, Y - 3.9f), module, LP::MIX_L_OUTPUT + i, true));
        }
    }

    void step() override {
        LP* m = getModule<LP>();
        if (m) for (int i = 0; i < kLenN; i++) if (lenCell[i]) lenCell[i]->paramId = LP::LEN_PARAM + m->sel();
        if (undoCell) undoCell->label = m && m->loop[m->sel()].undone ? "REDO" : "UNDO";
        ModuleWidget::step();
    }

    void appendContextMenu(Menu* menu) override {
        LP* m = getModule<LP>();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createIndexSubmenuItem("Clock pulse length", {kClockDivName[0], kClockDivName[1], kClockDivName[2]},
                                              [=]() { return m->clockDiv; }, [=](int i) { m->clockDiv = i; }));
        menu->addChild(createIndexSubmenuItem("JAM matrix page", {"1", "2", "3", "4", "5", "6"},
                                              [=]() { return (size_t)(m->curPage() - 1); },
                                              [=](size_t i) { m->params[LP::PAGE_PARAM].setValue((float)i + 1.f); }));
        menu->addChild(createSubmenuItem("Input monitor (hear the input at its track)", "", [=](Menu* sub) {
            for (int t = 0; t < kTracks; t++)
                sub->addChild(createCheckMenuItem(string::f("Input %d", t + 1), "",
                    [=]() { return m->params[LP::MON_PARAM + t].getValue() > 0.5f; },
                    [=]() { m->toggle(m->params[LP::MON_PARAM + t]); }));
        }));
        menu->addChild(createBoolPtrMenuItem("Auto monitor-off after recording (REC turns it back on)", "", &m->autoMon));
    }
    void onButton(const ButtonEvent& e) override {   // clicking the module hands it the JAM
        LP* m = getModule<LP>();
        if (m && e.action == GLFW_PRESS && m->params[LP::MIDI_ON_PARAM].getValue() > 0.5f) jamown::claim(m->id);
        ModuleWidget::onButton(e);
    }
};

} // namespace

Model* modelSynTHBloKLoop = createModel<SynTHBloKLoop, SynTHBloKLoopWidget>("Loop");
