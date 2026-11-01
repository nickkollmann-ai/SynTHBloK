// SynTHBloK Player Plus - MIDI file player with a watch folder.
// 16 poly tracks (pitch / gate / velocity), 16th-note clock, reset and end-of-cycle outputs.
#include "plugin.hpp"
#include "MidiFile.hpp"
#include "PlayerLink.hpp"
#include <osdialog.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iterator>
#include <sys/stat.h>
#include <string>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <algorithm>
#include <vector>
#include <chrono>
#include <ctime>

using synthblok::MidiSong;

namespace {
const int MAXL = 16;
// modification time (seconds) of a file, 0 if it can't be read. Works with UTF-8 paths on Windows.
double fileMTime(const std::string& path) {
#if defined ARCH_WIN
    struct _stat64 st;
    if (_wstat64(string::UTF8toUTF16(path).c_str(), &st) != 0) return 0.0;
    return (double) st.st_mtime;
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0.0;
    return (double) st.st_mtime;
#endif
}
std::string baseName(const std::string& p) {
    size_t k = p.find_last_of("/\\");
    return k == std::string::npos ? p : p.substr(k + 1);
}
}

// LED colour per panel column, all on one fire ramp: pitch = deep dark red, gate = red, velocity = deep
// yellow-orange (same in the master outs and both track blocks); the top jacks use the rest of the ramp.
static const float LED_COL_RGB[5][3] = {
    {1.00f, 0.90f, 0.40f},  // 0 light yellow
    {1.00f, 0.72f, 0.00f},  // 1 deep yellow
    {1.00f, 0.48f, 0.00f},  // 2 deep yellow-orange
    {1.00f, 0.12f, 0.02f},  // 3 red
    {0.55f, 0.00f, 0.00f},  // 4 deep dark red (pitch)
};
static inline int ledCol(int i) {
    static const int top[5] = {1, 3, 0, 2, 4};   // run, reset, clock, reset out, end of song
    if (i < 5) return top[i];            // top jacks
    if (i == 56) return 1;               // speed CV
    if (i < 8) return (i - 5 == 0) ? 4 : (i - 5 == 1 ? 3 : 2);   // pitch, gate, vel
    int c = (i - 8) % 3;                 // track grid: pitch, gate, vel
    return c == 0 ? 4 : (c == 1 ? 3 : 2);
}

// panel lamps on the same fire ramp as the jack LEDs
struct FireYellowLight : GrayModuleLightWidget { FireYellowLight() { addBaseColor(nvgRGB(255, 232, 110)); } };
struct FireRedLight   : GrayModuleLightWidget { FireRedLight()   { addBaseColor(nvgRGB(255, 40, 10)); } };
struct FireDeepLight  : GrayModuleLightWidget { FireDeepLight()  { addBaseColor(nvgRGB(255, 150, 0)); } };

struct SynTHBloKPlayer : Module, PlayerLinkProvider {
    std::shared_ptr<PlayerLink> link = std::make_shared<PlayerLink>();
    PlayerLink* playerLink() override { return link.get(); }

    enum ParamId { RUN_PARAM, RESET_PARAM, LOOP_PARAM, SPEED_PARAM, TRANSPOSE_PARAM, BPM_PARAM, PARAMS_LEN };
    enum InputId { RUN_INPUT, RESET_INPUT, SPEED_INPUT, INPUTS_LEN };
    enum OutputId { PITCH_OUTPUT, GATE_OUTPUT, VEL_OUTPUT, CLOCK_OUTPUT, RESET_OUTPUT, EOC_OUTPUT,
                    ENUMS(TRK_PITCH_OUTPUT, 16), ENUMS(TRK_GATE_OUTPUT, 16), ENUMS(TRK_VEL_OUTPUT, 16), OUTPUTS_LEN };
    enum LightId { RUN_LIGHT, RESET_LIGHT, LOOP_LIGHT, ENUMS(LED_LIGHT, 57 * 3), LIGHTS_LEN };

    // ---- shared with the UI thread (guarded by mu) ----
    std::mutex mu;
    std::shared_ptr<MidiSong> pending;
    std::atomic<bool> hasPending{false};
    struct ScriptState { std::mutex m; std::string text = "CLICK TO PASTE"; bool busy = false; std::string loadPath, err; };
    std::shared_ptr<ScriptState> script = std::make_shared<ScriptState>();
    std::string filePath, watchDir, status = "No file loaded";
    struct Part { std::shared_ptr<MidiSong> song; std::string path; int start; };
    std::vector<Part> parts;                          // every loaded file and the first track-out it sits on
    std::atomic<uint32_t> slotMask{0};                // track outs that have a file, for the panel
    std::string fileLabel;
    std::string notice; double noticeUntil = 0.0;      // script messages shown on the OLED for a few seconds
    bool autoPlay = false;
    std::vector<std::string> laneNamesUi;
    std::vector<double> clockUi;
    int lanesUi = 0;
    double lengthUi = 0.0, bpmUi = 120.0;

    // ---- audio thread only ----
    std::shared_ptr<MidiSong> song;
    size_t evIdx = 0, clkIdx = 0;
    double playhead = 0.0;
    bool running = false;
    // tempo: the BPM knob follows the loaded file; clock pulses on the clock jack take over
    int clkInPpq = 4, clkOutPpq = 4;                  // pulses per beat on the clock in / out
    dsp::SchmittTrigger clkInTrig;
    float sinceEdge = 1e9f, extPeriod = 0.f;
    long lastClkK = -1;
    float bpmRestore = -1.f; double bpmRestoreUntil = 0.0;
    double stPos = 0.0, edgeTick = -1.0;               // playhead in 16ths; tick the last clock edge should land on
    std::atomic<float> bpmNow{120.f};                 // tempo actually playing, for the readout
    std::atomic<bool> extClk{false};
    int lastLoopStart = 0, lastLoopLen = 0;           // to notice when a new loop is chosen
    dsp::BooleanTrigger resetBtn;
    dsp::SchmittTrigger runIn, resetIn;
    dsp::PulseGenerator clkPulse, rstPulse, eocPulse;
    uint8_t stackNote[MAXL][16], stackVel[MAXL][16];
    int stackN[MAXL] = {0};
    int retrig[MAXL] = {0};
    float outPitch[MAXL] = {0}, outVel[MAXL] = {0};
    float ledVal[57] = {0};
    bool hit[MAXL] = {false};
    int ledDiv = 0;
    std::atomic<float> loopPos{0.f};                 // 0..1 through the current 4-bar phrase
    std::atomic<float> elapsedUi{0.f};
    std::atomic<int> barNow{0}, barsTotal{0};
    std::atomic<float> playPos{0.f};                 // 0..1 for the display
    std::atomic<uint32_t> activeMask{0};             // lane gates, for the display

    SynTHBloKPlayer() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configSwitch(RUN_PARAM, 0.f, 1.f, 0.f, "Run", {"Stopped", "Running"});
        configButton(RESET_PARAM, "Restart from the beginning");
        configSwitch(LOOP_PARAM, 0.f, 1.f, 1.f, "Loop", {"Off", "On"});
        configParam(SPEED_PARAM, 0.25f, 2.f, 1.f, "Speed (unused: tempo is the BPM knob)", "x");
        configParam(BPM_PARAM, 30.f, 300.f, 120.f, "Tempo (follows the loaded file; an external clock overrides it)", " BPM", 0.f, 1.f, 0.f);
        configParam(TRANSPOSE_PARAM, -24.f, 24.f, 0.f, "Transpose", " semitones");
        paramQuantities[TRANSPOSE_PARAM]->snapEnabled = true;
        configInput(RUN_INPUT, "Run toggle (trigger)");
        configInput(RESET_INPUT, "Reset (trigger)");
        configInput(SPEED_INPUT, "Clock in (tempo follows the pulses and the BPM knob is bypassed; set pulses per beat in the menu)");
        configOutput(PITCH_OUTPUT, "Pitch, one channel per track (1 V/oct, C4 = 0 V)");
        configOutput(GATE_OUTPUT, "Gate, one channel per track");
        configOutput(VEL_OUTPUT, "Velocity 0-10 V, one channel per track");
        configOutput(CLOCK_OUTPUT, "Clock out (pulses per beat set in the menu, 4 = 16ths)");
        configOutput(RESET_OUTPUT, "Pulse when the song restarts");
        configOutput(EOC_OUTPUT, "Pulse at the end of the song");
        for (int l = 0; l < 16; l++) {
            configOutput(TRK_PITCH_OUTPUT + l, string::f("Track %d pitch (1 V/oct, C4 = 0 V)", l + 1));
            configOutput(TRK_GATE_OUTPUT + l, string::f("Track %d gate", l + 1));
            configOutput(TRK_VEL_OUTPUT + l, string::f("Track %d velocity (0-10 V)", l + 1));
        }
        clearStacks();
    }

    void clearStacks() {
        for (int l = 0; l < MAXL; l++) { stackN[l] = 0; retrig[l] = 0; outPitch[l] = 0.f; outVel[l] = 0.f; }
    }

    // ----- loading (UI thread) -----
    // Combine all loaded files into one song: each file's tracks sit on the track outs from its start slot.
    static std::shared_ptr<MidiSong> mergeParts(const std::vector<Part>& ps, std::string& warn) {
        auto m = std::make_shared<MidiSong>();
        int top = 0; size_t longest = 0;
        for (size_t i = 0; i < ps.size(); i++) {
            const MidiSong& s = *ps[i].song;
            for (const auto& e : s.ev) {
                int l = (int) e.lane + ps[i].start;
                if (l >= MAXL) continue;
                synthblok::MidiNoteEvt c = e; c.lane = (uint8_t) l; m->ev.push_back(c);
            }
            for (int k = 0; k < s.lanes; k++) {
                int l = ps[i].start + k;
                if (l >= MAXL) { warn = "TRUNCATED TO 16 LANES"; break; }
                if ((int) m->laneNames.size() <= l) m->laneNames.resize(l + 1);
                std::string tn = k < (int) s.laneNames.size() ? s.laneNames[k] : "";
                std::string fn = s.name;                                   // file name without extension
                for (const char* ex : {".mid", ".midi", ".MID", ".MIDI"}) {
                    size_t xl = std::strlen(ex);
                    if (fn.size() > xl && fn.compare(fn.size() - xl, xl, ex) == 0) { fn.erase(fn.size() - xl); break; }
                }
                bool generic = tn.empty() || tn.compare(0, 3, "Ch ") == 0 || tn.compare(0, 5, "Track") == 0;
                if (s.lanes == 1) m->laneNames[l] = fn;                  // one-track file: show the file name
                else m->laneNames[l] = generic ? fn + " " + tn : tn;     // several tracks: show each track name
                top = std::max(top, l + 1);
            }
            if (s.length > ps[longest].song->length) longest = i;
        }
        m->lanes = top;
        m->laneNames.resize(top);
        m->length = ps[longest].song->length;
        m->clock = ps[longest].song->clock;
        m->bpm = ps[0].song->bpm; m->ppq = ps[0].song->ppq;
        std::stable_sort(m->ev.begin(), m->ev.end(), [](const synthblok::MidiNoteEvt& a, const synthblok::MidiNoteEvt& b) {
            if (a.t != b.t) return a.t < b.t;
            return (int) a.on < (int) b.on;      // note-offs first
        });
        return m;
    }

    // call with mu held: publish the loaded files to the Player View expander
    void updateLinkLocked() {
        std::lock_guard<std::mutex> lg(link->m);
        link->songs.clear(); link->starts.clear();
        for (auto& p : parts) { link->songs.push_back(p.song); link->starts.push_back(p.start); }
        link->clock = clockUi; link->laneNames = laneNamesUi; link->lanes = lanesUi; link->length = lengthUi; link->bpm = bpmUi;
        link->label = parts.empty() ? std::string() : (parts.size() > 1 ? fileLabel : parts[0].song->name);
        link->version++;
    }

    // call with mu held: rebuild the song the audio thread plays from the loaded parts
    void rebuildLocked(const std::string& okText) {
        if (parts.empty()) {
            pending.reset(); filePath.clear(); fileLabel.clear(); laneNamesUi.clear(); clockUi.clear();
            lanesUi = 0; lengthUi = 0.0; bpmUi = 120.0; slotMask = 0; status = "No file loaded";
        } else {
            std::sort(parts.begin(), parts.end(), [](const Part& a, const Part& b) { return a.start < b.start; });
            std::string warn;
            auto merged = mergeParts(parts, warn);
            pending = merged;
            fileLabel = parts.size() > 1 ? std::to_string(parts.size()) + " FILES " + baseName(parts[0].path) : "";
            clockUi = merged->clock; laneNamesUi = merged->laneNames; lanesUi = merged->lanes; lengthUi = merged->length; bpmUi = merged->bpm;
            uint32_t mk = 0;
            for (auto& p : parts) for (int k = 0; k < p.song->lanes; k++) if (p.start + k < MAXL) mk |= 1u << (p.start + k);
            slotMask = mk;
            status = !warn.empty() ? warn : okText;
        }
        link->loopStart = 0; link->loopLen = 0;          // a new song starts with the whole song looping
        updateLinkLocked();
        hasPending = true;
    }

    // startLane = first track out the file's tracks go on; replaceAll = clear everything else first
    bool loadFile(const std::string& path, int startLane = 0, bool replaceAll = true) {
        std::vector<uint8_t> d = system::readFile(path);
        if (d.empty()) { std::lock_guard<std::mutex> g(mu); status = "Can't read " + baseName(path); return false; }
        auto s = std::make_shared<MidiSong>();
        std::string err;
        if (!synthblok::parseMidi(d.data(), d.size(), *s, err)) {
            std::lock_guard<std::mutex> g(mu); status = baseName(path) + ": " + err; return false;
        }
        s->name = baseName(path);
        startLane = clamp(startLane, 0, MAXL - 1);
        std::lock_guard<std::mutex> g(mu);
        if (replaceAll) parts.clear();
        else {      // drop files this one would overlap
            int lo = startLane, hi = startLane + std::max(1, s->lanes);
            parts.erase(std::remove_if(parts.begin(), parts.end(), [&](const Part& p) {
                return p.start < hi && p.start + std::max(1, p.song->lanes) > lo; }), parts.end());
        }
        parts.push_back({s, path, startLane});
        filePath = path;
        rebuildLocked(s->warn.empty() ? (parts.size() > 1 ? "Added" : "Loaded") : s->warn);
        return true;
    }

    // first free track out after everything that is loaded
    int nextFreeLane() {
        std::lock_guard<std::mutex> g(mu);
        int top = 0;
        for (auto& p : parts) top = std::max(top, p.start + std::max(1, p.song->lanes));
        return std::min(top, MAXL - 1);
    }

    // remove the file that sits on a track out
    void removeSlot(int lane) {
        std::lock_guard<std::mutex> g(mu);
        parts.erase(std::remove_if(parts.begin(), parts.end(), [&](const Part& p) {
            return lane >= p.start && lane < p.start + std::max(1, p.song->lanes); }), parts.end());
        if (!parts.empty()) filePath = parts.back().path;
        rebuildLocked("Removed");
    }

    // unload the current file and reset the display
    void clearFile() {
        {
            std::lock_guard<std::mutex> g(mu);
            pending.reset(); filePath.clear(); parts.clear(); slotMask = 0; fileLabel.clear(); laneNamesUi.clear(); lanesUi = 0; lengthUi = 0.0; bpmUi = 120.0;
            status = "No file loaded"; notice.clear();
        }
        { std::lock_guard<std::mutex> g(script->m); if (!script->busy) script->text = "CLICK TO PASTE"; script->err.clear(); script->loadPath.clear(); }
        hasPending = true;
    }

    // ----- playback (audio thread) -----
    void releaseAll() {
        for (int l = 0; l < MAXL; l++) stackN[l] = 0;
    }
    // loop region from the Player View (beats -> seconds via the 16th clock); false = play the whole song
    bool loopRegion(double& t0, double& t1) {
        if (!song) return false;
        int len = link->loopLen.load();
        if (len <= 0) return false;
        auto& ck = song->clock;
        int beats = (int) ((ck.size() + 3) / 4);                // the clock is 16ths, so 4 ticks per beat
        if (beats <= 0) return false;
        int s = std::max(0, std::min(beats - 1, link->loopStart.load()));
        int e = std::min(beats, s + len);
        if (e <= s || (s == 0 && e >= beats)) return false;
        t0 = (size_t) s * 4 < ck.size() ? ck[(size_t) s * 4] : 0.0;
        t1 = (size_t) e * 4 < ck.size() ? ck[(size_t) e * 4] : song->length;
        return t1 > t0 + 1e-3;
    }
    void seek(double t) {
        playhead = t; releaseAll(); lastClkK = -1;
        if (!song) { evIdx = 0; clkIdx = 0; return; }
        auto& ev = song->ev;
        evIdx = (size_t) (std::lower_bound(ev.begin(), ev.end(), t, [](const decltype(ev[0])& e, double v) { return e.t < v; }) - ev.begin());
        auto& ck = song->clock;
        clkIdx = (size_t) (std::lower_bound(ck.begin(), ck.end(), t) - ck.begin());
    }
    void restart(bool pulse) {
        double r0, r1;
        if (params[LOOP_PARAM].getValue() > 0.5f && loopRegion(r0, r1)) seek(r0);
        else { playhead = 0.0; evIdx = 0; clkIdx = 0; releaseAll(); lastClkK = -1; }
        if (pulse) rstPulse.trigger(1e-3f);
    }
    void noteOn(int l, uint8_t n, uint8_t v) {
        int& c = stackN[l];
        for (int k = 0; k < c; k++) if (stackNote[l][k] == n) {           // re-press: move to top
            for (int j = k; j + 1 < c; j++) { stackNote[l][j] = stackNote[l][j+1]; stackVel[l][j] = stackVel[l][j+1]; }
            c--; break;
        }
        if (c >= 16) { for (int j = 0; j + 1 < c; j++) { stackNote[l][j] = stackNote[l][j+1]; stackVel[l][j] = stackVel[l][j+1]; } c--; }
        if (c > 0) retrig[l] = std::max(2, (int) std::ceil(0.001f * APP->engine->getSampleRate()));
        stackNote[l][c] = n; stackVel[l][c] = v; c++;
        hit[l] = true;
    }
    void noteOff(int l, uint8_t n) {
        int& c = stackN[l];
        for (int k = 0; k < c; k++) if (stackNote[l][k] == n) {
            for (int j = k; j + 1 < c; j++) { stackNote[l][j] = stackNote[l][j+1]; stackVel[l][j] = stackVel[l][j+1]; }
            c--; break;
        }
    }

    void process(const ProcessArgs& args) override {
        if (hasPending.load() && mu.try_lock()) {
            song = pending; pending.reset(); hasPending = false;
            if (song && song->bpm > 1.0) {      // the knob jumps to the file's tempo (a patch reload keeps the saved knob)
                float want = (bpmRestore > 0.f && system::getTime() < bpmRestoreUntil) ? bpmRestore : (float) song->bpm;
                params[BPM_PARAM].setValue(clamp(want, 30.f, 300.f));
            }
            bool ap = autoPlay;
            mu.unlock();
            restart(false);
            if (!song) { running = false; params[RUN_PARAM].setValue(0.f); }
            if (ap && song) { running = true; params[RUN_PARAM].setValue(1.f); }
        }

        bool runParam = params[RUN_PARAM].getValue() > 0.5f;
        bool runTrig = false;
        if (runIn.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 2.f)) {
            runTrig = true;
            runParam = !runParam; params[RUN_PARAM].setValue(runParam ? 1.f : 0.f);
        }
        if (runParam != running) {
            running = runParam;
            if (!running) releaseAll();
        }
        bool doReset = resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f);
        if (resetBtn.process(params[RESET_PARAM].getValue() > 0.5f)) doReset = true;
        if (doReset) restart(true);

        {   // choosing a loop jumps the song to it: new start -> go to the start; same start but the playhead is outside -> go to the start
            int lsNow = link->loopStart.load(), llNow = link->loopLen.load();
            if (lsNow != lastLoopStart || llNow != lastLoopLen) {
                bool startMoved = lsNow != lastLoopStart || lastLoopLen <= 0;
                lastLoopStart = lsNow; lastLoopLen = llNow;
                if (llNow > 0) params[LOOP_PARAM].setValue(1.f);   // picking a loop in the roll switches looping on
                double r0, r1;
                if (params[LOOP_PARAM].getValue() > 0.5f && loopRegion(r0, r1) && (startMoved || playhead < r0 || playhead >= r1)) {
                    seek(r0); rstPulse.trigger(1e-3f);
                }
            }
        }

        // ---- tempo: knob, or the external clock when pulses are arriving on the clock jack ----
        float tempoNow = params[BPM_PARAM].getValue();
        bool clkEdge = false;
        {
            bool cin = inputs[SPEED_INPUT].isConnected();
            sinceEdge += args.sampleTime;
            if (cin && clkInTrig.process(inputs[SPEED_INPUT].getVoltage(), 0.1f, 2.f)) {
                clkEdge = true;
                if (edgeTick >= 0.0) edgeTick += 4.0 / (double) std::max(1, clkInPpq);
                if (sinceEdge > 1e-3f && sinceEdge < 2.f) {
                    float p = sinceEdge;
                    extPeriod = (extPeriod > 0.f && p > extPeriod * 0.5f && p < extPeriod * 2.f) ? extPeriod + (p - extPeriod) * 0.25f : p;
                }
                sinceEdge = 0.f;
            }
            if (!cin || sinceEdge > std::max(1.5f, extPeriod * 4.f)) { extPeriod = 0.f; edgeTick = -1.0; }
            bool ext = cin && extPeriod > 0.f;
            if (ext) tempoNow = clamp(60.f / (extPeriod * (float) std::max(1, clkInPpq)), 20.f, 400.f);
            extClk = ext; bpmNow = tempoNow;
        }
        int lanes = song ? std::min((int) song->lanes, MAXL) : 0;
        if (running && song) {
            float spd = tempoNow / (float) std::max(1.0, song->bpm);
            if (extClk.load() && extPeriod > 0.f) {            // phase-lock: steer the playhead onto the edge grid
                double step = 4.0 / (double) std::max(1, clkInPpq);
                if (edgeTick < 0.0) edgeTick = stPos;
                double target = edgeTick + step * std::min(1.0, (double) sinceEdge / (double) extPeriod);
                double err = target - stPos;
                if (std::fabs(err) > 6.0 * step) edgeTick = stPos;   // seek / loop jump / start: re-anchor
                else spd *= clamp((float) (1.0 + 0.5 * err / step), 0.5f, 2.f);
            } else edgeTick = -1.0;
            playhead += args.sampleTime * clamp(spd, 0.03f, 8.f);
            {   // loop region: wrap at its end and carry on from its start
                double r0, r1;
                if (params[LOOP_PARAM].getValue() > 0.5f && loopRegion(r0, r1) && playhead >= r1) {
                    double over = std::fmod(playhead - r1, r1 - r0);
                    seek(r0); playhead = r0 + over;
                    rstPulse.trigger(1e-3f); eocPulse.trigger(1e-3f);
                }
            }
            auto& ev = song->ev;
            while (evIdx < ev.size() && ev[evIdx].t <= playhead) {
                auto& e = ev[evIdx++];
                if (e.lane >= MAXL) continue;
                if (e.on) noteOn(e.lane, e.note, e.vel); else noteOff(e.lane, e.note);
            }
            auto& ck = song->clock;
            while (clkIdx < ck.size() && ck[clkIdx] <= playhead) clkIdx++;
            if (clkIdx > 0) {                                      // clock out: pulses per beat from the playhead's position in 16ths
                double st = (double) (clkIdx - 1);
                if (clkIdx < ck.size() && ck[clkIdx] > ck[clkIdx - 1]) st += std::min(1.0, std::max(0.0, (playhead - ck[clkIdx - 1]) / (ck[clkIdx] - ck[clkIdx - 1])));
                stPos = st;
                long k = (long) std::floor(st * (double) clkOutPpq / 4.0);
                if (k > lastClkK) { clkPulse.trigger(2e-3f); lastClkK = k; }
            }
            if (playhead >= song->length) {
                eocPulse.trigger(1e-3f);
                if (params[LOOP_PARAM].getValue() > 0.5f) {
                    playhead -= song->length; evIdx = 0; clkIdx = 0; releaseAll(); rstPulse.trigger(1e-3f);
                } else {
                    running = false; params[RUN_PARAM].setValue(0.f); releaseAll();
                    playhead = song->length;
                }
            }
        }

        float trans = params[TRANSPOSE_PARAM].getValue();
        int chans = std::max(1, lanes);
        outputs[PITCH_OUTPUT].setChannels(chans);
        outputs[GATE_OUTPUT].setChannels(chans);
        outputs[VEL_OUTPUT].setChannels(chans);
        uint32_t mask = 0;
        const uint32_t mm = link->muteMask.load();
        for (int l = 0; l < chans; l++) {
            bool on = (l < lanes) && stackN[l] > 0 && !((mm >> l) & 1u);
            if (on) {
                int top = stackN[l] - 1;
                outPitch[l] = ((float) stackNote[l][top] + trans - 60.f) / 12.f;
                outVel[l] = stackVel[l][top] / 127.f * 10.f;
            }
            bool gate = on;
            if (retrig[l] > 0) { retrig[l]--; gate = false; }
            if (gate) mask |= (1u << l);
            outputs[PITCH_OUTPUT].setVoltage(outPitch[l], l);
            outputs[GATE_OUTPUT].setVoltage(gate ? 10.f : 0.f, l);
            outputs[VEL_OUTPUT].setVoltage(on ? outVel[l] : 0.f, l);
        }
        for (int l = 0; l < MAXL; l++) {                       // one mono jack set per track
            bool used = l < lanes;
            bool on = used && stackN[l] > 0 && !((mm >> l) & 1u);
            bool gate = on && retrig[l] <= 0;
            outputs[TRK_PITCH_OUTPUT + l].setVoltage(used ? outPitch[l] : 0.f);
            outputs[TRK_GATE_OUTPUT + l].setVoltage(gate ? 10.f : 0.f);
            outputs[TRK_VEL_OUTPUT + l].setVoltage(on ? outVel[l] : 0.f);
            link->pitchNow[l] = outPitch[l];
        }
        activeMask = mask;
        link->activeMask = mask;
        outputs[CLOCK_OUTPUT].setVoltage(clkPulse.process(args.sampleTime) ? 10.f : 0.f);
        outputs[RESET_OUTPUT].setVoltage(rstPulse.process(args.sampleTime) ? 10.f : 0.f);
        outputs[EOC_OUTPUT].setVoltage(eocPulse.process(args.sampleTime) ? 10.f : 0.f);

        // ---- LEDs: tiny lights, one colour per column ----
        {
            float decay = std::exp(-args.sampleTime / 0.09f);
            auto up = [&](int i, float target) { ledVal[i] = std::max(ledVal[i] * decay, target); };
            up(0, runTrig ? 1.f : 0.f);
            up(1, doReset ? 1.f : 0.f);
            up(2, outputs[CLOCK_OUTPUT].getVoltage() > 1.f ? 1.f : 0.f);
            up(3, outputs[RESET_OUTPUT].getVoltage() > 1.f ? 1.f : 0.f);
            up(4, outputs[EOC_OUTPUT].getVoltage() > 1.f ? 1.f : 0.f);
            float anyHit = 0.f, anyGate = 0.f, maxVel = 0.f;
            for (int l = 0; l < MAXL; l++) {
                bool used = l < lanes;
                bool on = used && stackN[l] > 0 && !((mm >> l) & 1u);
                bool gate = on && retrig[l] <= 0;
                float v = on ? outVel[l] / 10.f : 0.f;
                if ((mm >> l) & 1u) hit[l] = false;
                up(8 + l * 3 + 0, hit[l] ? 1.f : 0.f);
                up(8 + l * 3 + 1, gate ? 1.f : 0.f);
                up(8 + l * 3 + 2, v);
                if (hit[l]) anyHit = 1.f;
                if (gate) anyGate = 1.f;
                maxVel = std::max(maxVel, v);
                hit[l] = false;
            }
            up(56, clkEdge ? 1.f : 0.f);
            up(5, anyHit); up(6, anyGate); up(7, maxVel);
            if (++ledDiv >= 32) {
                ledDiv = 0;
                for (int i = 0; i < 57; i++) {
                    float b = ledVal[i];
                    const float* c = LED_COL_RGB[ledCol(i)];
                    const float k = 0.8f;   // deep saturated hues, no pale mixes
                    lights[LED_LIGHT + i * 3 + 0].setBrightness(b * c[0] * k);
                    lights[LED_LIGHT + i * 3 + 1].setBrightness(b * c[1] * k);
                    lights[LED_LIGHT + i * 3 + 2].setBrightness(b * c[2] * k);
                }
            }
        }

        playPos = (song && song->length > 0.0) ? (float) std::min(1.0, playhead / song->length) : 0.f;
        if (song && !song->clock.empty()) {
            auto& ck = song->clock;
            double steps = 0.0;
            if (clkIdx > 0) {
                steps = (double) (clkIdx - 1);
                if (clkIdx < ck.size() && ck[clkIdx] > ck[clkIdx - 1])
                    steps += std::min(1.0, std::max(0.0, (playhead - ck[clkIdx - 1]) / (ck[clkIdx] - ck[clkIdx - 1])));
            }
            loopPos = (float) (std::fmod(steps, 64.0) / 64.0);
            barNow = (int) (steps / 16.0) + 1;
            barsTotal = (int) ((ck.size() + 15) / 16);
            elapsedUi = (float) playhead;
        } else { loopPos = 0.f; barNow = 0; barsTotal = 0; elapsedUi = 0.f; }
        link->playPos = playPos.load(); link->elapsed = elapsedUi.load(); link->barNow = barNow.load(); link->barsTotal = barsTotal.load(); link->running = running;
        lights[RUN_LIGHT].setBrightness(running ? 1.f : 0.f);
        lights[LOOP_LIGHT].setBrightness(params[LOOP_PARAM].getValue() > 0.5f ? 1.f : 0.f);
        lights[RESET_LIGHT].setSmoothBrightness(doReset ? 1.f : 0.f, args.sampleTime);
    }

    json_t* dataToJson() override {
        json_t* r = json_object();
        std::lock_guard<std::mutex> g(mu);
        json_object_set_new(r, "file", json_string(filePath.c_str()));
        json_t* fa = json_array();
        for (auto& pp : parts) {
            json_t* o = json_object();
            json_object_set_new(o, "path", json_string(pp.path.c_str()));
            json_object_set_new(o, "start", json_integer(pp.start));
            json_array_append_new(fa, o);
        }
        json_object_set_new(r, "files", fa);
        json_object_set_new(r, "watch", json_string(watchDir.c_str()));
        json_object_set_new(r, "autoPlay", json_boolean(autoPlay));
        json_object_set_new(r, "clkInPpq", json_integer(clkInPpq));
        json_object_set_new(r, "clkOutPpq", json_integer(clkOutPpq));
        json_object_set_new(r, "bpmKnob", json_real(params[BPM_PARAM].getValue()));
        json_object_set_new(r, "mute", json_integer((json_int_t) link->muteMask.load()));
        json_object_set_new(r, "loopBeatStart", json_integer(link->loopStart.load()));
        json_object_set_new(r, "loopBeatLen", json_integer(link->loopLen.load()));
        return r;
    }
    void dataFromJson(json_t* r) override {
        if (!r) return;
        json_t* a = json_object_get(r, "autoPlay");
        json_t* w = json_object_get(r, "watch");
        json_t* f = json_object_get(r, "file");
        json_t* fa = json_object_get(r, "files");
        std::vector<std::pair<std::string, int>> files;
        {
            std::lock_guard<std::mutex> g(mu);
            (void) a;   // auto-play is off by default; old saved value is ignored
            if (w && json_is_string(w)) watchDir = json_string_value(w);
            if (fa && json_is_array(fa)) {
                size_t i; json_t* v;
                json_array_foreach(fa, i, v) {
                    if (json_is_object(v)) {
                        json_t* pth = json_object_get(v, "path"); json_t* st = json_object_get(v, "start");
                        if (pth && json_is_string(pth)) files.push_back({json_string_value(pth), st ? (int) json_integer_value(st) : 0});
                    } else if (json_is_string(v)) files.push_back({json_string_value(v), (int) files.size()});
                }
            } else if (f && json_is_string(f) && json_string_length(f) > 0) files.push_back({json_string_value(f), 0});
        }
        json_t* jci = json_object_get(r, "clkInPpq"); json_t* jco = json_object_get(r, "clkOutPpq"); json_t* jbk = json_object_get(r, "bpmKnob");
        if (jci && json_is_integer(jci)) clkInPpq = (int) clamp((float) json_integer_value(jci), 1.f, 48.f);
        if (jco && json_is_integer(jco)) clkOutPpq = (int) clamp((float) json_integer_value(jco), 1.f, 48.f);
        if (jbk && json_is_number(jbk)) { bpmRestore = (float) json_number_value(jbk); bpmRestoreUntil = system::getTime() + 4.0; }
        json_t* jm = json_object_get(r, "mute");
        if (jm && json_is_integer(jm)) link->muteMask = (uint32_t) json_integer_value(jm);
        json_t* jls = json_object_get(r, "loopBeatStart"); json_t* jll = json_object_get(r, "loopBeatLen");
        if (!files.empty()) {
            bool ap; { std::lock_guard<std::mutex> g(mu); ap = autoPlay; autoPlay = false; }
            for (size_t i = 0; i < files.size(); i++) loadFile(files[i].first, files[i].second, i == 0);
            { std::lock_guard<std::mutex> g(mu); autoPlay = ap; }
        }
        if (jls && json_is_integer(jls)) link->loopStart = (int) json_integer_value(jls);
        if (jll && json_is_integer(jll)) link->loopLen = (int) json_integer_value(jll);
    }
};

// ---------- file dialogs (shared by the menu, the panel buttons and the display) ----------
static void playerLoadDialog(SynTHBloKPlayer* m, int startLane = 0, bool replaceAll = true) {
    std::string d;
    { std::lock_guard<std::mutex> g(m->mu); d = m->filePath.empty() ? "" : system::getDirectory(m->filePath); }
    osdialog_filters* fl = osdialog_filters_parse("MIDI:mid,midi");
    char* p = osdialog_file(OSDIALOG_OPEN, d.empty() ? nullptr : d.c_str(), nullptr, fl);
    osdialog_filters_free(fl);
    if (p) { m->loadFile(p, startLane, replaceAll); std::free(p); }
}
static bool playerFolderDialog(SynTHBloKPlayer* m) {
    std::string d;
    { std::lock_guard<std::mutex> g(m->mu); d = m->watchDir; }
    char* p = osdialog_file(OSDIALOG_OPEN_DIR, d.empty() ? nullptr : d.c_str(), nullptr, nullptr);
    if (!p) return false;
    { std::lock_guard<std::mutex> g(m->mu); m->watchDir = p; }
    std::free(p);
    return true;
}

// ---------- paste-a-script: clipboard -> temp .py -> python -> .mid in the watch folder ----------
static void playerRunClipboardScript(SynTHBloKPlayer* m) {
    auto st = m->script;
    auto setText = [st](const std::string& t, bool busy) { std::lock_guard<std::mutex> g(st->m); st->text = t; st->busy = busy; };
    { std::lock_guard<std::mutex> g(st->m); if (st->busy) return; }
    std::string dir;
    { std::lock_guard<std::mutex> g(m->mu); dir = m->watchDir; }
    if (dir.empty()) { setText("SET WATCH DIR", false); return; }
    const char* clip = glfwGetClipboardString(APP->window->win);
    std::string code = clip ? clip : "";
    if (code.size() < 10) { setText("CLIPBOARD EMPTY", false); return; }
    if (code.size() > 2000000) { setText("SCRIPT TOO BIG", false); return; }
    std::string py = asset::user("SynTHBloK_pasted.py");
    std::string log = asset::user("SynTHBloK_pasted.log");
    {   // drop markdown code fences (``` or ```python) if the text was copied from a chat or web page
        std::istringstream is(code); std::string line, outc;
        while (std::getline(is, line)) {
            std::string t = line; while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
            if (t.compare(0, 3, "```") == 0) continue;
            outc += line; outc += "\n";
        }
        code = outc;
    }
    { std::ofstream f(py, std::ios::binary); if (!f) { setText("CAN'T SAVE SCRIPT", false); return; } f << code; }
    setText("SCRIPT PASTED", true);
    std::thread([st, dir, py, log, setText]() {
        double t0 = (double) std::time(nullptr) - 2.0;
#ifdef ARCH_WIN
        auto sh = [](const std::string& c) { return std::system(("\"" + c + "\"").c_str()); };
        const std::string cdc = "cd /d \"" + dir + "\" && ";
        const std::string quiet = " > nul 2>&1";
        const char* cands[3] = {"python", "py -3", "python3"};
#else
        auto sh = [](const std::string& c) { return std::system(c.c_str()); };
        const std::string cdc = "cd \"" + dir + "\" && ";
        const std::string quiet = " > /dev/null 2>&1";
        const char* cands[3] = {"python3", "python", "py -3"};
#endif
        std::string exe;
        for (int i = 0; i < 3 && exe.empty(); i++)
            if (sh(std::string(cands[i]) + " --version" + quiet) == 0) exe = cands[i];
#ifdef ARCH_WIN
        if (exe.empty()) {
            const char* la2 = std::getenv("LOCALAPPDATA");
            if (la2) {
                std::string wa = std::string(la2) + "\\Microsoft\\WindowsApps\\python.exe";
                if (system::isFile(wa) && sh("\"" + wa + "\" --version" + quiet) == 0) exe = "\"" + wa + "\"";
            }
        }
        if (exe.empty()) {          // MSYS2's native Python (works from Rack without the MSYS2 shell)
            for (const char* c : {"C:\\msys64\\mingw64\\bin\\python.exe", "C:\\msys64\\ucrt64\\bin\\python.exe", "C:\\msys64\\clang64\\bin\\python.exe"}) {
                if (exe.empty() && system::isFile(c) && sh(std::string("\"") + c + "\" --version" + quiet) == 0) exe = std::string("\"") + c + "\"";
            }
        }
        if (exe.empty()) {          // Rack may not see PATH changes: look in the usual install folders
            std::vector<std::string> roots;
            const char* la = std::getenv("LOCALAPPDATA");
            if (la) roots.push_back(std::string(la) + "\\Python");             // Python 3.14 install manager
            if (la) roots.push_back(std::string(la) + "\\Programs\\Python");
            roots.push_back("C:\\Program Files\\Python");
            roots.push_back("C:\\Program Files (x86)\\Python");
            roots.push_back("C:\\");
            for (const std::string& r : roots) {
                try {
                    std::vector<std::string> es = system::getEntries(r);
                    std::sort(es.begin(), es.end());
                    for (auto it = es.rbegin(); it != es.rend() && exe.empty(); ++it) {   // newest version first
                        std::string base = system::getFilename(*it);
                        if (base.size() < 6 || string::lowercase(base).compare(0, 6, "python") != 0) continue;
                        std::string cand = "\"" + system::join(*it, "python.exe") + "\"";
                        if (system::isFile(system::join(*it, "python.exe")) && sh(cand + " --version" + quiet) == 0) exe = cand;
                    }
                } catch (...) {}
                if (!exe.empty()) break;
            }
        }
#endif
        if (exe.empty()) { setText("NO PYTHON FOUND", false); { std::lock_guard<std::mutex> g(st->m); st->err = "Python not found: install it from python.org (tick 'Add to PATH')"; } return; }

        auto readLog = [&]() { std::ifstream f(log, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); };
        auto runIt = [&]() { setText("CONVERTING...", true); return sh(cdc + exe + " \"" + py + "\" > \"" + log + "\" 2>&1"); };

        int rc = runIt();
        std::string out = readLog();
        for (int tries = 0; rc != 0 && tries < 2; tries++) {         // missing library? install it once and retry
            size_t k = out.find("No module named '");
            if (k == std::string::npos) break;
            k += 17; size_t e = out.find('\'', k);
            if (e == std::string::npos) break;
            std::string mod = out.substr(k, e - k);
            size_t dot = mod.find('.'); if (dot != std::string::npos) mod = mod.substr(0, dot);
            bool ok = !mod.empty() && mod.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos;
            if (!ok) break;
            std::string pkg = mod;
            if (mod == "pretty_midi") pkg = "pretty_midi"; else if (mod == "midiutil") pkg = "MIDIUtil";
            setText("INSTALLING " + mod, true);
            if (sh(cdc + exe + " -m pip install " + pkg + " > \"" + log + ".pip\" 2>&1") != 0) { setText("PIP FAILED: " + mod, false); std::lock_guard<std::mutex> g(st->m); st->err = "pip could not install " + pkg; return; }
            rc = runIt(); out = readLog();
        }
        // collect new .mid files from the watch folder and one level of sub-folders (scripts often write into their own folder)
        std::string newest; int bestScore = -1; double best = 0.0;
        auto consider = [&](const std::string& e) {
            std::string ext = string::lowercase(system::getExtension(e));
            if (ext != ".mid" && ext != ".midi") return;
            double tm = fileMTime(e);
            if (tm < t0) return;
            std::string up = string::uppercase(system::getFilename(e));
            int score = up.find("FULL_ARRANGEMENT") != std::string::npos ? 2 : (up.find("ARRANGEMENT") != std::string::npos ? 1 : 0);
            if (score > bestScore || (score == bestScore && tm >= best)) { newest = e; bestScore = score; best = tm; }
        };
        try {
            for (const std::string& e : system::getEntries(dir)) {
                if (system::isDirectory(e)) {
                    try { for (const std::string& e2 : system::getEntries(e)) if (system::isFile(e2)) consider(e2); } catch (...) {}
                } else if (system::isFile(e)) consider(e);
            }
        } catch (...) {}
        if (rc != 0) {
            std::string last, line;
            std::istringstream is(out);
            while (std::getline(is, line)) { while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back(); if (!line.empty()) last = line; }
            setText("SCRIPT ERROR", false);
            std::string where;
            size_t ln = out.rfind(", line ");
            if (ln != std::string::npos) { size_t e = ln + 7; while (e < out.size() && std::isdigit((unsigned char) out[e])) e++; where = "line " + out.substr(ln + 7, e - ln - 7) + ": "; }
            std::lock_guard<std::mutex> g(st->m); st->err = last.empty() ? "Script failed" : where + last;
        } else if (newest.empty()) {
            setText("NO MIDI WRITTEN", false);
            std::lock_guard<std::mutex> g(st->m); st->err = "Script ran but wrote no .mid into the watch folder";
        } else {
            setText("CONVERTED", false);
            std::lock_guard<std::mutex> g(st->m); st->err.clear(); st->loadPath = newest;
        }
    }).detach();
}

// ---------- OLED display (same look as the FX module) ----------
struct PlayerDisplay : TransparentWidget {
    SynTHBloKPlayer* module = nullptr;
    std::shared_ptr<window::Font> font;
    int hoverLane = -1;                                   // track row under the mouse (-1 = none)
    static constexpr float kRowH = 8.0f, kY0 = 47.f;

    // which of the 16 track rows is at this position (-1 if none)
    int laneAt(Vec p) const {
        float colW = (box.size.x - 8) / 2.f;
        if (p.y < kY0 || p.y >= kY0 + 8 * kRowH || p.x < 4 || p.x >= 4 + 2 * colW) return -1;
        int col = (int) ((p.x - 4) / colW), row = (int) ((p.y - kY0) / kRowH);
        return col * 8 + row;
    }

    void draw(const DrawArgs& a) override {
        nvgBeginPath(a.vg);
        nvgRoundedRect(a.vg, 0, 0, box.size.x, box.size.y, 2.5f);
        nvgFillColor(a.vg, nvgRGBA(20, 12, 4, 255));
        nvgFill(a.vg);
        nvgStrokeWidth(a.vg, 0.8f);
        nvgStrokeColor(a.vg, nvgRGBA(255, 180, 90, 70));
        nvgStroke(a.vg);
    }
    void drawLayer(const DrawArgs& a, int layer) override {
        if (layer != 1) return;
        if (!font) font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        if (!font) return;
        nvgFontFaceId(a.vg, font->handle);
        nvgTextAlign(a.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

        std::string l1 = "NO FILE", l2 = "CLICK HERE OR LOAD";
        std::vector<std::string> names;
        float pos = 0.f, lpos = 0.f, el = 0.f; int bn = 0, bt = 0; uint32_t mask = 0, smask = 0xFFFF, muteM = 0; int lanes = 0;
        if (module) {
            std::lock_guard<std::mutex> g(module->mu);
            if (!module->filePath.empty()) l1 = module->fileLabel.empty() ? baseName(module->filePath) : module->fileLabel;
            lanes = module->lanesUi; names = module->laneNamesUi;
            char buf[96];
            if (lanes > 0) { snprintf(buf, sizeof buf, "%d TRK  %.0f BPM  %.0fs", lanes, module->bpmUi, module->lengthUi); l2 = buf; }
            else l2 = module->status;
            if (!module->notice.empty() && system::getTime() < module->noticeUntil) l2 = module->notice;
            pos = module->playPos; mask = module->activeMask;
            smask = module->slotMask.load(); muteM = module->link->muteMask.load();
            lpos = module->loopPos; el = module->elapsedUi; bn = module->barNow; bt = module->barsTotal;
        } else {
            l1 = "acid_bedrock_export.mid"; l2 = "8 TRK  140 BPM  27s"; lanes = 8; pos = 0.4f; lpos = 0.62f; el = 10.8f; bn = 7; bt = 16; mask = 0x15;
            names = {"Kick", "Snare", "Hats", "Bass", "Acid", "Pad", "Lead", "FX"};
        }
        if (l1.size() > 24) l1 = l1.substr(0, 21) + "...";
        if (l2.size() > 30) l2 = l2.substr(0, 27) + "...";

        NVGcolor amber = nvgRGBA(255, 200, 100, 255), dim = nvgRGBA(255, 180, 90, 150);
        nvgFontSize(a.vg, 10.f); nvgFillColor(a.vg, amber); nvgText(a.vg, 4, 4, l1.c_str(), nullptr);
        nvgFontSize(a.vg, 8.f);  nvgFillColor(a.vg, dim);   nvgText(a.vg, 4, 17, l2.c_str(), nullptr);

        // TRACK bar (whole file) and LOOP bar (current 4-bar phrase)
        {
            float lx = 4, lw = 24.f, tw = 40.f;
            float bx = lx + lw, bw = box.size.x - 4 - tw - bx, bh = 3.5f;
            char t1[24], t2[24];
            snprintf(t1, sizeof t1, "%d:%02d", (int) el / 60, (int) el % 60);
            if (bt > 0) snprintf(t2, sizeof t2, "BAR %d/%d", bn, bt); else snprintf(t2, sizeof t2, "BAR -");
            float ys[2] = {28.5f, 36.5f};
            float vs[2] = {pos, lpos};
            const char* nm[2] = {"TRACK", "LOOP"};
            const std::string rt[2] = {t1, t2};
            nvgFontSize(a.vg, 7.f);
            for (int i = 0; i < 2; i++) {
                nvgFillColor(a.vg, dim); nvgTextAlign(a.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                nvgText(a.vg, lx, ys[i] - 1.8f, nm[i], nullptr);
                nvgBeginPath(a.vg); nvgRoundedRect(a.vg, bx, ys[i], bw, bh, 1.5f); nvgFillColor(a.vg, nvgRGBA(60, 36, 10, 255)); nvgFill(a.vg);
                nvgBeginPath(a.vg); nvgRoundedRect(a.vg, bx, ys[i], std::max(0.f, bw * clamp(vs[i], 0.f, 1.f)), bh, 1.5f);
                nvgFillColor(a.vg, i == 0 ? nvgRGBA(255, 190, 90, 255) : nvgRGBA(255, 122, 0, 255)); nvgFill(a.vg);
                if (i == 1) {   // bar dividers inside the 4-bar phrase
                    nvgStrokeWidth(a.vg, 0.8f); nvgStrokeColor(a.vg, nvgRGBA(20, 12, 4, 255));
                    for (int k = 1; k < 4; k++) { nvgBeginPath(a.vg); nvgMoveTo(a.vg, bx + bw * k / 4.f, ys[i]); nvgLineTo(a.vg, bx + bw * k / 4.f, ys[i] + bh); nvgStroke(a.vg); }
                }
                nvgFillColor(a.vg, dim); nvgTextAlign(a.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
                nvgText(a.vg, box.size.x - 4, ys[i] - 1.8f, rt[i].c_str(), nullptr);
            }
            nvgTextAlign(a.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        }

        // 16 tracks in two columns of 8: lamp + number + name
        float colW = (box.size.x - 8) / 2.f, rowH = kRowH, y0 = kY0;
        nvgFontSize(a.vg, 7.5f);
        for (int l = 0; l < 16; l++) {
            int col = l / 8, row = l % 8;
            float x = 4 + col * colW, y = y0 + row * rowH;
            bool used = l < lanes && ((smask >> l) & 1), on = (mask >> l) & 1;
            nvgBeginPath(a.vg); nvgRoundedRect(a.vg, x, y + 1.5f, 5.f, 5.f, 1.f);
            bool mu = used && ((muteM >> l) & 1u);
            nvgFillColor(a.vg, mu ? nvgRGBA(200, 40, 25, 255) : on ? nvgRGBA(255, 200, 100, 255) : used ? nvgRGBA(110, 70, 25, 255) : nvgRGBA(40, 26, 10, 255));
            nvgFill(a.vg);
            if (l == hoverLane) {      // row under the mouse: click loads a file here, right-click clears it
                nvgBeginPath(a.vg); nvgRoundedRect(a.vg, x - 2.f, y + 0.5f, colW - 1.f, rowH - 1.f, 1.5f);
                nvgStrokeWidth(a.vg, 0.8f); nvgStrokeColor(a.vg, nvgRGBA(255, 200, 100, 200)); nvgStroke(a.vg);
            }
            if (!used) {
                char em[24]; snprintf(em, sizeof em, "%02d %s", l + 1, l == hoverLane ? "CLICK TO LOAD" : "---");
                nvgFillColor(a.vg, l == hoverLane ? dim : nvgRGBA(110, 70, 25, 150));
                nvgText(a.vg, x + 8, y + 0.5f, em, nullptr);
            }
            if (used) {
                std::string nm = (l < (int) names.size()) ? names[l] : "";
                char num[8]; snprintf(num, sizeof num, "%02d ", l + 1);
                std::string t = std::string(num) + nm;
                if (t.size() > 14) t = t.substr(0, 13) + ".";
                nvgFillColor(a.vg, mu ? nvgRGBA(255, 110, 90, 140) : on ? amber : dim);
                nvgText(a.vg, x + 8, y + 0.5f, t.c_str(), nullptr);
            }
        }
    }
    void onButton(const event::Button& e) override {
        if (!module || e.action != GLFW_PRESS) return;
        int lane = laneAt(e.pos);
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
            if (lane >= 0) playerLoadDialog(module, lane, false);   // put a file on this track out
            else playerLoadDialog(module);                          // top of the display: load one file, replacing all
            e.consume(this);
        } else if (e.button == GLFW_MOUSE_BUTTON_RIGHT && lane >= 0) {
            module->removeSlot(lane);                               // right-click a row to clear it
            e.consume(this);
        }
    }
    void onHover(const event::Hover& e) override { hoverLane = laneAt(e.pos); e.consume(this); }
    void onLeave(const event::Leave& e) override { hoverLane = -1; }
};

// ---------- small OLED-style panel button ----------
struct PlayerButton : TransparentWidget {
    std::string label;
    std::function<std::string()> dyn;
    float fs = 9.5f;
    bool hover = false;
    std::function<void()> onPaste;
    std::function<void()> onClick;
    std::function<bool()> lit;
    std::shared_ptr<window::Font> font;

    void drawLayer(const DrawArgs& a, int layer) override {
        if (layer != 1) return;
        if (!font) font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        bool on = lit && lit();
        nvgBeginPath(a.vg);
        nvgRoundedRect(a.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 2.f);
        nvgFillColor(a.vg, on ? nvgRGBA(255, 150, 40, 90) : nvgRGBA(20, 12, 4, 255));
        nvgFill(a.vg);
        nvgStrokeWidth(a.vg, 1.f);
        nvgStrokeColor(a.vg, nvgRGBA(255, 122, 0, 255));
        nvgStroke(a.vg);
        if (!font) return;
        nvgFontFaceId(a.vg, font->handle);
        nvgFontSize(a.vg, fs);
        nvgFillColor(a.vg, nvgRGBA(255, 200, 100, 255));
        nvgTextAlign(a.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        std::string t = dyn ? dyn() : label;
        if (hover && onPaste && t == "CLICK TO PASTE") {          // idle + hovered: show a blinking cursor and the hint
            bool blink = ((int) (system::getTime() * 2.0)) % 2 == 0;
            t = blink ? "CLICK TO PASTE_" : "CLICK TO PASTE";
        }
        nvgText(a.vg, box.size.x / 2, box.size.y / 2 + 0.5f, t.c_str(), nullptr);
    }
    void onButton(const event::Button& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            if (onClick) onClick();
            e.consume(this);
        }
    }
    void onHover(const event::Hover& e) override { e.consume(this); }
    void onEnter(const event::Enter& e) override { hover = true; }
    void onLeave(const event::Leave& e) override { hover = false; }
    void onHoverKey(const event::HoverKey& e) override {
        if (onPaste && e.action == GLFW_PRESS && e.key == GLFW_KEY_V && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL) {
            onPaste();
            e.consume(this);
        }
    }
};

// small OLED under the tempo knob: BPM now playing, EXT while a clock is patched in
struct BpmDisplay : TransparentWidget {
    SynTHBloKPlayer* module = nullptr;
    std::shared_ptr<window::Font> font;
    void drawLayer(const DrawArgs& a, int layer) override {
        if (layer != 1) return;
        if (!font) font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(a.vg); nvgRoundedRect(a.vg, 0, 0, W, H, 2.f); nvgFillColor(a.vg, nvgRGBA(14, 9, 3, 255)); nvgFill(a.vg);
        nvgBeginPath(a.vg); nvgRoundedRect(a.vg, 0.4f, 0.4f, W - 0.8f, H - 0.8f, 2.f);
        nvgStrokeWidth(a.vg, 0.6f); nvgStrokeColor(a.vg, nvgRGBA(255, 180, 90, 90)); nvgStroke(a.vg);
        if (!font) return;
        float bpm = module ? module->bpmNow.load() : 161.f;
        bool ext = module ? module->extClk.load() : false;
        char b[16];
        if (bpm >= 100.f) snprintf(b, sizeof b, "%.1f", bpm); else snprintf(b, sizeof b, "%.2f", bpm);
        nvgFontFaceId(a.vg, font->handle);
        nvgFontSize(a.vg, 7.5f); nvgFillColor(a.vg, ext ? nvgRGBA(255, 120, 30, 255) : nvgRGBA(255, 200, 100, 255));
        nvgTextAlign(a.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(a.vg, 2.f, H * 0.5f + 0.5f, b, nullptr);
        nvgFontSize(a.vg, 4.6f); nvgFillColor(a.vg, nvgRGBA(255, 180, 90, ext ? 255 : 140));
        nvgTextAlign(a.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(a.vg, W - 1.8f, H * 0.5f + 0.5f, ext ? "EXT" : "BPM", nullptr);
    }
};
struct KnobDimOverlay : TransparentWidget {
    SynTHBloKPlayer* module = nullptr;
    void drawLayer(const DrawArgs& a, int layer) override {
        if (layer != 1 || !module || !module->extClk.load()) return;
        nvgBeginPath(a.vg); nvgCircle(a.vg, box.size.x * 0.5f, box.size.y * 0.5f, box.size.x * 0.5f);
        nvgFillColor(a.vg, nvgRGBA(10, 6, 2, 170)); nvgFill(a.vg);
    }
};

struct SynTHBloKPlayerWidget : ModuleWidget {
    int tick = 0;

    SynTHBloKPlayerWidget(SynTHBloKPlayer* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKPlayer.svg")));
        using namespace rack::componentlibrary;

        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(138.74f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 125.0f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(138.74f, 125.0f))));

        auto* disp = new PlayerDisplay();
        disp->module = module;
        disp->box.pos = mm2px(Vec(4.f, 10.f));
        disp->box.size = mm2px(Vec(52.96f, 42.f));
        addChild(disp);

        auto* bl = new PlayerButton();
        bl->label = "LOAD"; bl->fs = 8.f;
        bl->box.pos = mm2px(Vec(4.f, 55.f)); bl->box.size = mm2px(Vec(14.f, 5.2f));
        bl->onClick = [=]() { if (module) playerLoadDialog(module); };
        addChild(bl);

        auto* bc = new PlayerButton();
        bc->label = "CLEAR"; bc->fs = 8.f;
        bc->box.pos = mm2px(Vec(4.f, 60.9f)); bc->box.size = mm2px(Vec(14.f, 4.4f));
        bc->onClick = [=]() { if (module) module->clearFile(); };
        addChild(bc);

        auto* bw = new PlayerButton();
        bw->label = "WATCH"; bw->fs = 8.f;
        bw->box.pos = mm2px(Vec(19.5f, 55.f)); bw->box.size = mm2px(Vec(14.f, 5.2f));
        bw->onClick = [=]() {
            if (!module) return;
            bool watching; { std::lock_guard<std::mutex> g(module->mu); watching = !module->watchDir.empty(); }
            if (watching) { std::lock_guard<std::mutex> g(module->mu); module->watchDir.clear(); }   // click again to stop
            else if (playerFolderDialog(module)) primed = false;
        };
        bw->lit = [=]() { if (!module) return false; std::lock_guard<std::mutex> g(module->mu); return !module->watchDir.empty(); };
        addChild(bw);

        // paste box: click = take the Python script from the clipboard and convert it; shows the state
        auto* bp = new PlayerButton();
        bp->fs = 6.5f;
        bp->box.pos = mm2px(Vec(35.f, 55.f)); bp->box.size = mm2px(Vec(21.96f, 5.2f));
        bp->dyn = [=]() -> std::string {
            if (!module) return "CLICK TO PASTE";
            std::lock_guard<std::mutex> g(module->script->m); return module->script->text;
        };
        bp->lit = [=]() { if (!module) return false; std::lock_guard<std::mutex> g(module->script->m); return module->script->busy || module->script->text == "CONVERTED"; };
        bp->onClick = [=]() { if (module) playerRunClipboardScript(module); };
        bp->onPaste = bp->onClick;
        addChild(bp);

        float y1 = 74.5f;
        {   // round TL1105 buttons, same as the Poly / FX modules (light sits under the cap)
            addChild(createLightCentered<SmallLight<FireYellowLight>>(mm2px(Vec(8.f, y1)), module, SynTHBloKPlayer::RUN_LIGHT));
            auto* run = createParamCentered<TL1105>(mm2px(Vec(8.f, y1)), module, SynTHBloKPlayer::RUN_PARAM);
            run->momentary = false; addParam(run);
            addChild(createLightCentered<SmallLight<FireRedLight>>(mm2px(Vec(19.f, y1)), module, SynTHBloKPlayer::RESET_LIGHT));
            addParam(createParamCentered<TL1105>(mm2px(Vec(19.f, y1)), module, SynTHBloKPlayer::RESET_PARAM));
            addChild(createLightCentered<SmallLight<FireDeepLight>>(mm2px(Vec(30.f, y1)), module, SynTHBloKPlayer::LOOP_LIGHT));
            auto* lp = createParamCentered<TL1105>(mm2px(Vec(30.f, y1)), module, SynTHBloKPlayer::LOOP_PARAM);
            lp->momentary = false; addParam(lp);
        }
        addParam(createParamCentered<Trimpot>(mm2px(Vec(43.f, y1)), module, SynTHBloKPlayer::BPM_PARAM));
        {   // tempo readout under the knob; dims the knob while an external clock is in control
            auto* dim = new KnobDimOverlay(); dim->module = module;
            dim->box.size = mm2px(Vec(7.2f, 7.2f)); dim->box.pos = mm2px(Vec(43.f, y1)).minus(dim->box.size.div(2.f));
            addChild(dim);
            auto* bd = new BpmDisplay(); bd->module = module;
            bd->box.size = mm2px(Vec(10.4f, 4.0f)); bd->box.pos = mm2px(Vec(43.f, 80.6f)).minus(bd->box.size.div(2.f));
            addChild(bd);
        }
        addParam(createParamCentered<Trimpot>(mm2px(Vec(54.5f, y1)), module, SynTHBloKPlayer::TRANSPOSE_PARAM));

        float y2 = 99.f;
        addInput(createInputCentered<BlackJack>(mm2px(Vec(7.5f, y2)), module, SynTHBloKPlayer::RUN_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(16.5f, y2)), module, SynTHBloKPlayer::RESET_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(25.5f, y2)), module, SynTHBloKPlayer::SPEED_INPUT));
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(36.5f, y2)), module, SynTHBloKPlayer::CLOCK_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(45.5f, y2)), module, SynTHBloKPlayer::RESET_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(54.5f, y2)), module, SynTHBloKPlayer::EOC_OUTPUT));

        // (the summed poly pitch/gate/vel jacks are gone from the panel: every track has its own outputs)

        // ---- tiny amber LEDs above every jack (same TinyLight as the Resonator) ----
        auto led = [&](int idx, float x, float y) {
            addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x, y)), module, SynTHBloKPlayer::LED_LIGHT + idx * 3));
        };
        led(0, 7.5f, y2 - 6.2f); led(1, 16.5f, y2 - 6.2f); led(56, 25.5f, y2 - 6.2f);
        led(2, 36.5f, y2 - 6.2f); led(3, 45.5f, y2 - 6.2f); led(4, 54.5f, y2 - 6.2f);

        // ---- track-out grid: 16 tracks x (pitch, gate, velocity), two blocks of 8 ----
        for (int l = 0; l < 16; l++) {
            int blk = l / 8, row = l % 8;
            float x0 = 65.f + blk * 37.5f, y = 27.f + 12.6f * row;
            addOutput(createOutputCentered<BlackJack>(mm2px(Vec(x0 + 11.f, y)), module, SynTHBloKPlayer::TRK_PITCH_OUTPUT + l));
            addOutput(createOutputCentered<BlackJack>(mm2px(Vec(x0 + 21.f, y)), module, SynTHBloKPlayer::TRK_GATE_OUTPUT + l));
            addOutput(createOutputCentered<BlackJack>(mm2px(Vec(x0 + 31.f, y)), module, SynTHBloKPlayer::TRK_VEL_OUTPUT + l));
            led(8 + l * 3 + 0, x0 + 11.f, y - 6.2f);
            led(8 + l * 3 + 1, x0 + 21.f, y - 6.2f);
            led(8 + l * 3 + 2, x0 + 31.f, y - 6.2f);
        }
    }

    void step() override {
        ModuleWidget::step();
        auto* m = dynamic_cast<SynTHBloKPlayer*>(module);
        if (!m) return;
        {   // results from the paste-a-script worker
            std::string lp, er;
            { std::lock_guard<std::mutex> g(m->script->m); lp.swap(m->script->loadPath); er.swap(m->script->err); }
            if (!er.empty()) { std::lock_guard<std::mutex> g(m->mu); m->notice = er; m->noticeUntil = system::getTime() + 10.0; }
            if (!lp.empty()) { m->loadFile(lp); seenPath = lp; seenTime = fileMTime(lp); primed = true; pendPath.clear(); }
        }
        if (++tick < 90) return;          // about every 1.5 s
        tick = 0;
        std::string dir;
        { std::lock_guard<std::mutex> g(m->mu); dir = m->watchDir; }
        if (dir.empty()) return;
        try {
            std::string np; double best = 0.0;
            for (const std::string& e : system::getEntries(dir)) {
                if (!system::isFile(e)) continue;
                std::string ext = string::lowercase(system::getExtension(e));
                if (ext != ".mid" && ext != ".midi") continue;
                double t = fileMTime(e);
                if (np.empty() || t > best) { np = e; best = t; }
            }
            if (np.empty()) return;
            uint64_t sz = system::getFileSize(np);
            if (!primed) {                         // first look at this folder
                primed = true; seenPath = np; seenTime = best;
                bool empty; { std::lock_guard<std::mutex> g(m->mu); empty = m->filePath.empty(); }
                if (empty) m->loadFile(np);
                return;
            }
            if (np == seenPath && best == seenTime) return;
            // changed: wait until size and time are stable for one more check, so half-written files aren't read
            if (pendPath == np && pendTime == best && pendSize == sz) {
                m->loadFile(np);
                seenPath = np; seenTime = best;
                pendPath.clear();
            } else { pendPath = np; pendTime = best; pendSize = sz; }
        } catch (...) {}
    }
    bool primed = false;
    std::string seenPath, pendPath;
    double seenTime = 0.0, pendTime = 0.0;
    uint64_t pendSize = 0;

    void onPathDrop(const event::PathDrop& e) override {
        auto* m = dynamic_cast<SynTHBloKPlayer*>(module);
        if (!m) return;
        bool first = true;
        for (const std::string& p : e.paths) {
            std::string ext = string::lowercase(system::getExtension(p));
            if (ext != ".mid" && ext != ".midi") continue;
            if (first) m->loadFile(p, 0, true);       // first file replaces, the rest go on the next free track outs
            else m->loadFile(p, m->nextFreeLane(), false);
            first = false;
        }
        if (!first) { primed = false; e.consume(this); }
    }

    void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<SynTHBloKPlayer*>(module);
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Load MIDI file...", "", [=]() { playerLoadDialog(m); }));
        menu->addChild(createMenuItem("Choose watch folder...", "", [=]() { if (playerFolderDialog(m)) primed = false; }));
        std::string wd; { std::lock_guard<std::mutex> g(m->mu); wd = m->watchDir; }
        if (!wd.empty()) {
            menu->addChild(createMenuLabel("Watching: " + (wd.size() > 40 ? "..." + wd.substr(wd.size() - 37) : wd)));
            menu->addChild(createMenuItem("Stop watching", "", [=]() { std::lock_guard<std::mutex> g(m->mu); m->watchDir.clear(); }));
        }
        menu->addChild(createBoolMenuItem("Auto-play new files", "",
            [=]() { std::lock_guard<std::mutex> g(m->mu); return m->autoPlay; },
            [=](bool v) { std::lock_guard<std::mutex> g(m->mu); m->autoPlay = v; }));
        menu->addChild(new MenuSeparator);
        static const int PPQ[5] = {1, 2, 4, 8, 24};
        static const char* PPQN[5] = {"1 per beat (quarters)", "2 per beat (8ths)", "4 per beat (16ths)", "8 per beat (32nds)", "24 per beat (MIDI clock)"};
        menu->addChild(createSubmenuItem("Clock in pulses", "", [=](Menu* sm) {
            for (int i = 0; i < 5; i++) sm->addChild(createCheckMenuItem(PPQN[i], "", [=]() { return m->clkInPpq == PPQ[i]; }, [=]() { m->clkInPpq = PPQ[i]; }));
        }));
        menu->addChild(createSubmenuItem("Clock out pulses", "", [=](Menu* sm) {
            for (int i = 0; i < 5; i++) sm->addChild(createCheckMenuItem(PPQN[i], "", [=]() { return m->clkOutPpq == PPQ[i]; }, [=]() { m->clkOutPpq = PPQ[i]; }));
        }));
        menu->addChild(createMenuItem("Reload current file", "", [=]() {
            std::string p; { std::lock_guard<std::mutex> g(m->mu); p = m->filePath; }
            if (!p.empty()) m->loadFile(p);
        }));
    }
};

Model* modelSynTHBloKPlayer = createModel<SynTHBloKPlayer, SynTHBloKPlayerWidget>("SynTHBloKPlayer");
