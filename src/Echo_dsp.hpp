#pragma once
// Shared delay / space DSP for SynTHBloK: the FINAL-stage DELAY and SPACE of MATRIX-8, MATRIX FORGE,
// MATRIX-DUO and MATRIX-DRUM (M8Delay, M8Reverb), and MATRIX-ECHO's 12 delay engines and looper.
// Signals are in "internal" units: +-1 = +-5 V.
#include "plugin.hpp"
#include <cmath>
#include <vector>
#include <algorithm>

namespace {

// Stereo ping-pong delay: left feeds right and back, a low-pass in the loop darkens each repeat.
struct M8Delay {
    std::vector<float> bl, br;
    int pos = 0;
    float lpL = 0.f, lpR = 0.f, time = 0.375f;
    void init(float sr) { bl.assign((size_t)(sr * 2.1f), 0.f); br.assign(bl.size(), 0.f); pos = 0; }
    void process(float inL, float inR, float sr, float targetTime, float& outL, float& outR) {
        if (bl.empty()) init(sr);
        time += (clamp(targetTime, 0.02f, 2.f) - time) * 0.0002f;   // glide when the tempo changes
        int n = (int)bl.size();
        float d = time * sr;
        int i0 = (int)d; float f = d - i0;
        auto tap = [&](std::vector<float>& b) {
            int a = (pos - i0 + n) % n, c = (pos - i0 - 1 + n) % n;
            return b[a] + (b[c] - b[a]) * f;
        };
        float dl = tap(bl), dr = tap(br);
        lpL += (dl - lpL) * 0.35f;
        lpR += (dr - lpR) * 0.35f;
        bl[pos] = inL + lpR * 0.5f;          // ping-pong: each side feeds the other
        br[pos] = inR + lpL * 0.5f;
        pos = (pos + 1) % n;
        outL = dl; outR = dr;
    }
};

// Small stereo reverb (Freeverb-style: 4 damped combs + 2 allpasses per side).
struct M8Reverb {
    struct Comb { std::vector<float> b; int i = 0; float lp = 0.f;
        float process(float x, float fb, float damp) { float y = b[i]; lp = y + (lp - y) * damp; b[i] = x + lp * fb; i = (i + 1) % (int)b.size(); return y; } };
    struct AllPass { std::vector<float> b; int i = 0;
        float process(float x) { float y = b[i]; b[i] = x + y * 0.5f; i = (i + 1) % (int)b.size(); return y - x; } };
    Comb cl[4], cr[4];
    AllPass al[2], ar[2];
    bool ready = false;
    void init(float sr) {
        static const int combs[4] = {1116, 1188, 1277, 1356}, aps[2] = {556, 441};
        float s = sr / 44100.f;
        for (int k = 0; k < 4; k++) { cl[k].b.assign((size_t)(combs[k] * s), 0.f); cr[k].b.assign((size_t)((combs[k] + 23) * s), 0.f); }
        for (int k = 0; k < 2; k++) { al[k].b.assign((size_t)(aps[k] * s), 0.f); ar[k].b.assign((size_t)((aps[k] + 23) * s), 0.f); }
        ready = true;
    }
    void process(float inL, float inR, float sr, float& outL, float& outR) {
        if (!ready) init(sr);
        float in = (inL + inR) * 0.15f, l = 0.f, r = 0.f;
        for (int k = 0; k < 4; k++) { l += cl[k].process(in, 0.84f, 0.3f); r += cr[k].process(in, 0.84f, 0.3f); }
        for (int k = 0; k < 2; k++) { l = al[k].process(l); r = ar[k].process(r); }
        outL = l; outR = r;
    }
};

// ============================== MATRIX-ECHO ==============================

enum EchoType { ET_CLASSIC, ET_2290, ET_2290MOD, ET_ANALOG, ET_TAPE, ET_LOFI, ET_DYNAMIC, ET_REVERSE,
                ET_PINGPONG, ET_SLAP, ET_CRYSTAL, ET_SPACE, ET_NUM };

// Power-of-two ring buffer. read(d) = d samples before the slot write() fills next (Hermite).
struct EchoBuf {
    std::vector<float> b;
    int mask = 0, pos = 0;
    void init(float sec, float sr) {
        int n = 1;
        while (n < sec * sr) n <<= 1;
        b.assign((size_t)n, 0.f); mask = n - 1; pos = 0;
    }
    float read(float d) const {
        d = clamp(d, 3.f, (float)(mask - 4));
        float fp = (float)pos - d;
        int i = (int)std::floor(fp);
        float f = fp - (float)i;
        float xm = b[(i - 1) & mask], x0 = b[i & mask], x1 = b[(i + 1) & mask], x2 = b[(i + 2) & mask];
        float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    void write(float x) { b[pos] = x; pos = (pos + 1) & mask; }
};

struct EchoParams {
    int type = ET_CLASSIC;
    float time = 0.375f;   // s
    float time2 = 0.f;     // s, a second (dual) tap; 0 = none
    float fb = 0.5f, tone = 0.5f, mod = 0.f;
    float freeze = 0.f;    // 0..1 (smoothed by the caller)
};

// The 12 engines on one pair of buffers. Each type is a read (taps, modulation), a "colour" applied
// to every repeat (tone filter, saturation, lo-fi), and a write (feedback routing).
struct EchoEngine {
    EchoBuf buf[2], oct[2];
    float lp[2] = {}, hp[2] = {}, cls[2] = {}, hold[2] = {};
    float sr = 0.f, time = 0.375f, time2 = 0.f;
    float lfo = 0.f, flut = 0.f, holdPh = 0.f, octPh = 0.f, revPh = 0.f;
    float drift = 0.f, driftT = 0.f, driftTimer = 0.f, env = 0.f;
    uint32_t rng = 0x9e3779b9u;
    M8Reverb rev;

    void init(float sampleRate) {
        sr = sampleRate;
        for (int c = 0; c < 2; c++) {
            buf[c].init(15.f, sr);   // 7 s, doubled for REVERSE's grains
            oct[c].init(0.1f, sr);
            lp[c] = hp[c] = cls[c] = hold[c] = 0.f;
        }
        rev.ready = false;
    }
    float rand01() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) * (1.f / 16777216.f); }
    static float safe(float x) {   // transparent below 1, then a soft ceiling under 2: bounds self-oscillation
        float a = std::fabs(x);
        return a < 1.f ? x : std::copysign(1.f + std::tanh(a - 1.f), x);
    }
    float coef(float hz) const { return 1.f - std::exp(-2.f * float(M_PI) * std::min(hz, 0.45f * sr) / sr); }

    void process(float inL, float inR, const EchoParams& p, float sampleRate, float& oL, float& oR) {
        if (sampleRate != sr) init(sampleRate);
        const int t = p.type;
        const float dt = 1.f / sr;
        bool tapeLike = t == ET_TAPE || t == ET_SPACE;

        // ---- delay time: glide (CLASSIC as M8Delay; tape slow, so time changes bend the pitch) ----
        float gl = t == ET_CLASSIC ? 0.0002f : 1.f - std::exp(-dt / (tapeLike ? 0.35f : 0.06f));
        time += (clamp(p.time, 0.02f, 7.f) - time) * gl;
        if (p.time2 > 0.f) time2 = time2 > 0.f ? time2 + (clamp(p.time2, 0.02f, 7.f) - time2) * gl : p.time2;
        else time2 = 0.f;

        // ---- modulation (seconds added to the read time) ----
        float depth = p.mod * 0.004f, rate = 0.7f;
        if (t == ET_2290MOD) { depth = (0.3f + 0.7f * p.mod) * 0.005f; rate = 0.8f + 2.f * p.mod; }
        else if (t == ET_ANALOG) { depth = 0.0002f + p.mod * 0.002f; rate = 0.35f + p.mod; }
        else if (tapeLike) { depth = (0.15f + p.mod) * 0.003f; rate = 0.55f; }
        else if (t == ET_REVERSE) depth = 0.f;
        lfo += rate * dt; if (lfo >= 1.f) lfo -= 1.f;
        flut += 6.3f * dt; if (flut >= 1.f) flut -= 1.f;
        driftTimer -= dt;
        if (driftTimer <= 0.f) { driftT = rand01() * 2.f - 1.f; driftTimer = 0.2f + 0.6f * rand01(); }
        drift += (driftT - drift) * (3.f * dt);
        float mod[2];
        for (int c = 0; c < 2; c++) {
            float ph = 2.f * float(M_PI) * (lfo + 0.25f * c);
            mod[c] = depth * (0.5f + 0.5f * std::sin(ph));
            if (tapeLike) mod[c] += 0.00012f * (1.f + std::sin(2.f * float(M_PI) * flut)) + 0.0006f * (0.5f + p.mod) * (1.f + drift);
            if (t == ET_ANALOG) mod[c] += 0.0002f * (1.f + drift);
        }

        // ---- read ----
        float wet[2], raw[2];
        float T = time * sr;
        revPh += 1.f / std::max(T, 64.f);
        if (revPh >= 1.f) revPh -= 1.f;
        for (int c = 0; c < 2; c++) {
            float d = T + mod[c] * sr;
            if (t == ET_SLAP && c == 1) d += 0.007f * sr;   // a hair later on the right: width
            if (t == ET_REVERSE) {
                // two backwards-running grains, half a grain apart, sin^2 windows (they sum to 1)
                float w = 0.f;
                for (int h = 0; h < 2; h++) {
                    float ph = revPh + 0.5f * h; if (ph >= 1.f) ph -= 1.f;
                    float s = std::sin(float(M_PI) * ph);
                    w += s * s * buf[c].read(3.f + 2.f * ph * T);
                }
                wet[c] = w;
                raw[c] = buf[c].read(d);   // forward tap: what FREEZE loops
            } else if (t == ET_SPACE) {
                // three playback heads at 1/3, 2/3 and 1 of the time, like a multi-head tape echo
                wet[c] = 0.3f * buf[c].read(d / 3.f) + 0.3f * buf[c].read(d * 2.f / 3.f) + 0.4f * buf[c].read(d);
                raw[c] = buf[c].read(d);
            } else {
                wet[c] = raw[c] = buf[c].read(d);
            }
        }

        // ---- colour: per-type tone filter, saturation, lo-fi (it runs on every repeat) ----
        static const float kLp[ET_NUM] = {0.f, 14000.f, 14000.f, 0.f, 5500.f, 3200.f, 12000.f, 10000.f, 12000.f, 5000.f, 11000.f, 4500.f};
        static const float kHp[ET_NUM] = {0.f, 40.f, 40.f, 60.f, 90.f, 350.f, 40.f, 40.f, 40.f, 80.f, 120.f, 100.f};
        static const float kDrive[ET_NUM] = {0.f, 0.f, 0.f, 1.4f, 1.8f, 2.2f, 0.f, 0.f, 0.f, 1.3f, 0.f, 1.5f};
        float tf = 2.f * p.tone - 1.f;
        float lpHz = t == ET_ANALOG ? 1200.f + 4500.f / (1.f + 2.f * time) : kLp[t];   // a BBD darkens as it slows
        float aLp = coef(clamp(lpHz * std::pow(4.f, tf), 150.f, 18000.f));
        float aHp = coef(kHp[t] * (tf > 0.f ? std::pow(8.f, tf) : 1.f));
        float drive = kDrive[t];
        bool sh = false;
        if (t == ET_LOFI) {
            holdPh += 6000.f * std::pow(2.f, tf) * dt;
            if (holdPh >= 1.f) { holdPh -= 1.f; sh = true; }
        }
        float y[2], fbs[2];
        for (int c = 0; c < 2; c++) {
            if (t == ET_CLASSIC) {
                cls[c] += (wet[c] - cls[c]) * clamp(0.35f * std::pow(2.5f, tf), 0.02f, 1.f);
                y[c] = wet[c]; fbs[c] = cls[c];
                continue;
            }
            float x = wet[c];
            lp[c] += (x - lp[c]) * aLp; x = lp[c];
            hp[c] += (x - hp[c]) * aHp; x -= hp[c];
            if (drive > 0.f) x = std::tanh(drive * x) / drive;
            if (t == ET_LOFI) { if (sh) hold[c] = std::round(x * 32.f) / 32.f; x = hold[c]; }
            y[c] = x; fbs[c] = x;
            if (t == ET_CRYSTAL) {
                // octave up: two read heads sweeping a short buffer at double speed, sin^2 crossfaded
                oct[c].write(x);
                float W = 0.05f * sr, s = 0.f;
                for (int h = 0; h < 2; h++) {
                    float ph = octPh + 0.5f * h; if (ph >= 1.f) ph -= 1.f;
                    float w = std::sin(float(M_PI) * ph);
                    s += w * w * oct[c].read(3.f + W * (1.f - ph));
                }
                fbs[c] = 0.5f * x + 0.6f * s;
                y[c] = x + 0.4f * s;
            }
        }
        if (t == ET_CRYSTAL) { octPh += 1.f / (0.05f * sr); if (octPh >= 1.f) octPh -= 1.f; }

        // ---- write: feedback routing; FREEZE loops the raw repeats with the input shut ----
        float g = p.fb * (drive > 0.f ? 1.08f : 1.f) * (t == ET_SLAP ? 0.7f : 1.f);
        float fz = p.freeze, ig = 1.f - fz;
        float in[2] = {inL * ig, inR * ig};
        float fbv[2];
        for (int c = 0; c < 2; c++) fbv[c] = (1.f - fz) * g * fbs[c] + fz * raw[c];
        if (t == ET_CLASSIC) {
            buf[0].write(in[0] + (1.f - fz) * g * fbs[1] + fz * raw[1]);   // ping-pong: each side feeds the other
            buf[1].write(in[1] + (1.f - fz) * g * fbs[0] + fz * raw[0]);
        } else if (t == ET_PINGPONG) {
            buf[0].write(safe(0.5f * (in[0] + in[1]) + (1.f - fz) * g * fbs[1] + fz * raw[1]));
            buf[1].write(safe((1.f - fz) * g * fbs[0] + fz * raw[0]));
        } else {
            for (int c = 0; c < 2; c++) buf[c].write(safe(in[c] + fbv[c]));
        }

        // ---- output ----
        if (time2 > 0.f && t != ET_REVERSE && t != ET_SPACE)
            for (int c = 0; c < 2; c++) y[c] += 0.7f * buf[c].read((time2 + mod[c]) * sr + 1.f);   // +1: the write just moved
        if (t == ET_DYNAMIC) {
            // ducking: the repeats drop while the input plays and bloom in the gaps
            float lvl = std::max(std::fabs(inL), std::fabs(inR));
            env += (lvl - env) * (1.f - std::exp(-dt / (lvl > env ? 0.005f : 0.3f)));
            float duck = 1.f / (1.f + 12.f * env);
            y[0] *= duck; y[1] *= duck;
        }
        if (t == ET_SPACE) {
            float rl, rr;
            rev.process(y[0], y[1], sr, rl, rr);
            y[0] += rl; y[1] += rr;
        }
        oL = y[0]; oR = y[1];
    }
};

// Stereo looper: REC -> PLAY -> OVERDUB, STOP, PLAY ONCE, one level of UNDO / REDO (the buffer before
// the last overdub pass is kept and swapped), CLEAR. Optional length snap to a beat (rec / play / once).
// Tempo lock (MATRIX-ECHO): call sync() every sample with the beat and grid (beat or bar) edges and use
// press() for the buttons. REC start / stop, PLAY, ONCE and STOP then land on the next grid edge (or
// at once, counted from the edge, when pressed just after one), playback re-syncs to every beat, and
// setLoopBeats() plays only the first n beats of the recording. Off the grid the transport acts at once;
// with tempoBeat set, a free take's length still snaps to whole beats of the tempo. recBeats > 0 ends a tempo-locked
// recording on its own after that many beats; REC on a stopped loop records a new take.
struct EchoLooper {
    enum State { EMPTY, REC, PLAY, DUB, STOP, ONCE };
    enum Arm { A_NONE, A_REC, A_CLOSE, A_CLOSE_ONCE, A_PLAY, A_ONCE, A_STOP, A_DUB };
    enum Button { B_REC, B_PLAY, B_ONCE };
    static const int kPeaks = 96;
    static const int kXf = 96;   // samples of crossfade when a re-sync moves the playhead
    std::vector<float> l, r, ul, ur;
    int cap = 0, len = 0, pos = 0, recTarget = 0, layers = 0;
    State st = EMPTY;
    bool undoOk = false, undone = false;
    float gain = 0.f, sr = 44100.f;
    float peak[kPeaks] = {};
    // tempo lock
    bool locked = false;     // a tempo grid drives the transport (set by sync())
    int arm = A_NONE;        // the action waiting for the next grid edge
    int beats = 0, loopBeats = 0, playLen = 0, beatIdx = 0, sinceGrid = 0, takes = 0;
    int recBeats = 0, recCount = 0;   // beats to record before the take closes itself (0: until REC / PLAY / ONCE)
    int loopSub = 0;                  // playing 1 / 2^loopSub of a beat (0: whole beats)
    bool replace = false;             // REC on a playing loop replaces the audio instead of layering (MATRIX-ECHO)
    float dubMix = 0.f;
    float tempoBeat = 0.f;            // samples per beat of the tempo even off the grid (0: none): free loops snap to it
    int seamF = 0, seamAt = 0;        // head samples to crossfade with the live run-on after a close
    bool closeOnce = false;           // the recording that runs on to its length then plays once
    int closeBeatsN = 0;              // a free take snapped to the tempo: its beats, for closeAt()
    // a new take with a tempo starts on a beat: countIn beats after REC, then on the next grid edge
    int countIn = 0, gridBeats = 1;   // set by the module (gridBeats: 4 on the bar grid, else 1)
    int recWait = 0;                  // beat edges until the waiting take starts (0: none waiting)
    int sinceBeat = 1 << 30, gIdx = 0;   // samples since the last beat edge; that beat's place in the grid
    std::vector<float> pl, pr;        // the input just before now: a take started late gets its head from here
    int preCap = 1, preW = 0;
    double beatLen = 0.0;    // samples per beat of the recording (0: recorded free)
    float curBeat = 0.f;     // samples per beat now (0: no tempo)
    int xf = 0, xfPos = 0;

    void init(float sampleRate, float sec) {
        sr = sampleRate;
        cap = (int)(sec * sr);
        l.assign((size_t)cap, 0.f); r.assign(l.size(), 0.f); ul.assign(l.size(), 0.f); ur.assign(l.size(), 0.f);
        preCap = std::max(1, (int)(0.1f * sr));
        pl.assign((size_t)preCap, 0.f); pr.assign(pl.size(), 0.f); preW = 0;
        clear();
    }
    void clear() {
        st = EMPTY; len = pos = recTarget = layers = 0; undoOk = undone = false; std::fill(peak, peak + kPeaks, 0.f);
        arm = A_NONE; beats = loopBeats = playLen = beatIdx = xf = 0; beatLen = 0.0; dubMix = 0.f;
        seamF = seamAt = 0; closeOnce = false; loopSub = 0; recWait = 0; closeBeatsN = 0;
    }
    // beats left of the count-in (0: not counting)
    int counting() const { return arm == A_REC && recWait > 0 ? recWait : 0; }
    bool playing() const { return st == PLAY || st == DUB || st == ONCE; }
    void rebuildPeaks() {
        std::fill(peak, peak + kPeaks, 0.f);
        for (int i = 0; i < len; i++) {
            int k = (int)((int64_t)i * kPeaks / len);
            peak[k] = std::max(peak[k], std::max(std::fabs(l[i]), std::fabs(r[i])));
        }
    }
    // end the recording at n samples. The seam has no fade to silence: the loop's first 3 ms are crossfaded
    // into what was played right after its end (already recorded past n, else the live input as it plays),
    // so the end runs on into the start as it did when it was played.
    void closeAt(int n) {
        int run = pos - n;   // samples recorded past the end
        len = n; pos = 0; recTarget = 0;
        seamF = std::min(len / 4, (int)(0.003f * sr));
        seamAt = clamp(run, 0, seamF);
        for (int i = 0; i < seamAt; i++) {
            float g = (float)i / seamF;
            l[i] = l[i] * g + l[n + i] * (1.f - g); r[i] = r[i] * g + r[n + i] * (1.f - g);
        }
        rebuildPeaks();
        st = closeOnce ? ONCE : PLAY; closeOnce = false;
        playLen = len; beats = loopBeats = beatIdx = xf = loopSub = 0; beatLen = 0.0;
        if (closeBeatsN > 0) { beats = loopBeats = closeBeatsN; beatLen = (double)n / closeBeatsN; }   // free, in whole beats
        closeBeatsN = 0;
    }
    // end a tempo-locked recording of n samples (a whole number of beats)
    void closeBeats(int n) {
        int b = std::max(1, (int)std::lround(n / curBeat));
        closeAt(n);
        beats = loopBeats = b; beatLen = (double)n / b; takes++;
    }
    // end the first recording; beat > 0 snaps the length to whole beats (recording on to reach it)
    void close(float beat, State then) {
        if (pos < (int)(0.05f * sr)) { clear(); return; }
        int n = pos;
        if (beat > 0.f) {
            int b = std::max(1, (int)std::round(pos / beat));
            while (b > 1 && b * beat > cap) b--;
            n = std::min(cap, (int)std::round(b * beat));
            if (tempoBeat > 0.f) closeBeatsN = b;   // MATRIX-ECHO: the loop keeps its beats (LEN steps in beats)
            if (n > pos) { recTarget = n; closeOnce = then == ONCE; return; }
        }
        closeOnce = then == ONCE;
        closeAt(n);
    }
    void startDub() {
        std::copy(l.begin(), l.begin() + len, ul.begin());
        std::copy(r.begin(), r.begin() + len, ur.begin());
        undoOk = true; undone = false; layers++;
        st = DUB;
    }
    void rec(float beat) {
        switch (st) {
            case EMPTY: st = REC; pos = 0; len = 0; recTarget = 0; layers = 1; undoOk = undone = false;
                        std::fill(peak, peak + kPeaks, 0.f); break;
            case REC: if (!recTarget) close(beat, PLAY); break;
            case PLAY: case ONCE: startDub(); break;
            case DUB: st = PLAY; break;
            case STOP: pos = 0; startDub(); break;
        }
    }
    void play(float beat) {
        if (st == REC) { if (!recTarget) close(beat, PLAY); }
        else if (st == PLAY || st == DUB) st = STOP;
        else if (st == STOP || st == ONCE) { pos = 0; st = PLAY; }
    }
    void once(float beat) {
        if (st == REC) { if (!recTarget) close(beat, ONCE); }
        else if (st != EMPTY) { pos = 0; st = ONCE; }
    }
    void undo() {
        if (st == REC) { clear(); return; }
        if (!undoOk) return;
        if (st == DUB) st = PLAY;
        std::swap(l, ul); std::swap(r, ur);
        undone = !undone;
        layers += undone ? -1 : 1;
        rebuildPeaks();
    }
    // ---- tempo lock ----
    void jump(int to) { xfPos = pos; xf = kXf; pos = to; }
    // every sample, before process(): beatSamples = samples per beat (0: no tempo, the transport acts at once);
    // restart = this beat is the downbeat of a (re)started clock: a playing loop starts again from its top
    void sync(bool beatEdge, bool gridEdge, float beatSamples, bool restart = false) {
        locked = beatSamples > 0.f;
        curBeat = beatSamples;
        if (beatEdge) { sinceBeat = 0; gIdx = gridEdge || gridBeats <= 1 ? 0 : gIdx + 1; }
        else if (sinceBeat < (1 << 30)) sinceBeat++;
        if (counting()) {   // a new take waits for its beat
            if (locked) { if (gridEdge) sinceGrid = 0; else if (sinceGrid < (1 << 30)) sinceGrid++; }
            if (tempoBeat <= 0.f || (beatEdge && --recWait == 0)) fire(0);   // (the tempo went away: start now)
            return;
        }
        bool dubWait = arm == A_DUB && playing();   // an overdub waits for the top of the loop, not the grid
        if (!locked) { if (arm != A_NONE && !dubWait) fire(0); return; }
        if (gridEdge) sinceGrid = 0;
        else if (sinceGrid < (1 << 30)) sinceGrid++;
        if (gridEdge && arm != A_NONE && !dubWait) { fire(0); return; }
        if (beatEdge && st == REC && recBeats > 0 && ++recCount >= recBeats) {   // the take reached its length
            arm = arm == A_CLOSE_ONCE ? A_CLOSE_ONCE : A_CLOSE;
            fire(0);
            return;
        }
        if (beatEdge && beats > 0 && playing()) {
            if (restart || ++beatIdx >= loopBeats) {   // the top of the loop
                beatIdx = 0;
                if (arm == A_DUB) { arm = A_NONE; startDub(); }
                else if (st == ONCE) { st = STOP; return; }
            }
            int want = (int)std::llround(beatIdx * beatLen);
            if (want != pos) jump(want);
        }
    }
    // REC / PLAY / ONCE: arm the action for the next grid edge; a second press disarms it
    void press(int b) {
        int a = A_NONE;
        switch (st) {
            case EMPTY: if (b == B_REC) a = A_REC; break;
            case REC: a = b == B_ONCE ? A_CLOSE_ONCE : A_CLOSE; break;
            case PLAY: case ONCE:
                if (b == B_REC) {   // overdub from the top of the loop (at once when just past it)
                    if (arm == A_DUB) { arm = A_NONE; return; }
                    int late = (int)(locked ? std::min(0.08f * sr, 0.125f * curBeat) : 0.08f * sr);
                    if (pos <= late) { arm = A_NONE; startDub(); }
                    else arm = A_DUB;
                    return;
                }
                if (b == B_PLAY && st == ONCE) { st = PLAY; return; }   // keep looping
                a = b == B_PLAY ? A_STOP : A_ONCE; break;
            case DUB:
                if (b == B_REC) { st = PLAY; return; }
                a = b == B_PLAY ? A_STOP : A_ONCE; break;
            case STOP: a = b == B_REC ? A_REC : b == B_PLAY ? A_PLAY : A_ONCE; break;   // REC: a new take, the old loop stays silent
        }
        if (a == A_NONE) return;
        if (a == arm) { arm = A_NONE; recWait = 0; return; }
        arm = a;
        if (a == A_REC && tempoBeat > 0.f) {   // a new take starts on a beat: after the count-in, on the grid
            int late = (int)std::min(0.08f * sr, 0.125f * tempoBeat);   // just after a beat: that beat counts
            int k = sinceBeat <= late ? countIn : countIn + 1;          // beat edges on from the last one
            int g = locked ? std::max(1, gridBeats) : 1;
            while ((gIdx + k) % g) k++;
            if (k == 0) fire(sinceBeat); else recWait = k;
            return;
        }
        if (!locked) { fire(0); return; }
        int late = (int)std::min(0.08f * sr, 0.125f * curBeat);   // just after an edge: act now, counted from it
        if (sinceGrid <= late) fire(sinceGrid);
    }
    // off = samples since the grid edge the action belongs to
    void fire(int off) {
        int a = arm;
        arm = A_NONE; recWait = 0;
        switch (a) {
            case A_REC: {
                st = REC; len = recTarget = 0; layers = 1; undoOk = undone = false;
                std::fill(peak, peak + kPeaks, 0.f);
                pos = std::min(off, cap);
                // started late: the head is what came in since the beat (zeros past the pre-roll)
                int got = std::min(pos, preCap);
                for (int i = 0; i < pos; i++) {
                    int back = pos - i;   // samples before now
                    if (back > got) { l[i] = r[i] = 0.f; continue; }
                    int j = ((preW - back) % preCap + preCap) % preCap;
                    l[i] = pl[j]; r[i] = pr[j];
                }
                beats = loopBeats = playLen = recCount = 0; beatLen = 0.0; closeOnce = false;
                // off the grid with a tempo: the take starts now and closes itself after exactly recBeats beats
                closeBeatsN = 0;
                if (!locked && recBeats > 0 && tempoBeat > 0.f) {
                    recTarget = std::min(cap, (int)std::lround(recBeats * tempoBeat));
                    closeBeatsN = recBeats;
                }
                break;
            }
            case A_CLOSE: case A_CLOSE_ONCE: {
                if (st != REC) break;
                // off the grid: stops now, or (with a tempo) at the nearest whole beat from the start
                if (!locked) { close(tempoBeat, a == A_CLOSE_ONCE ? ONCE : PLAY); break; }
                int n = pos - off;
                if (n < 0.5f * curBeat) { arm = a; break; }   // under half a beat: wait for the next edge
                closeBeats(n);
                pos = off;
                if (a == A_CLOSE_ONCE) st = ONCE;
                break;
            }
            case A_PLAY: case A_ONCE: case A_DUB:
                if (len <= 0) break;
                pos = off; beatIdx = 0; xf = 0;
                if (a == A_DUB) startDub();
                else st = a == A_PLAY ? PLAY : ONCE;
                break;
            case A_STOP: if (playing()) st = STOP; break;
        }
    }
    // play the first n beats of a tempo-locked recording (n <= 0: all of it); sub > 0: the first 1 / 2^sub of
    // a beat, repeated 2^sub times a beat and re-synced on every beat
    void setLoopBeats(int n, int sub = 0) {
        if (beats <= 0 || st == REC) return;
        int b = sub > 0 ? 1 : n <= 0 ? beats : std::min(n, beats);
        if (b == loopBeats && sub == loopSub) return;
        loopBeats = b; loopSub = sub;
        playLen = std::max(1, std::min(len, (int)std::llround(b * beatLen / (1 << sub))));
        if (beatIdx >= b) {   // same place in the beat, in the shorter loop
            int nb = beatIdx % b;
            int np = pos - (int)std::llround((beatIdx - nb) * beatLen);
            beatIdx = nb;
            jump(((np % playLen) + playLen) % playLen);
        } else if (pos >= playLen) jump(pos % playLen);
    }

    void process(float inL, float inR, float& oL, float& oR) {
        oL = oR = 0.f;
        if (st != REC) { pl[preW] = inL; pr[preW] = inR; if (++preW >= preCap) preW = 0; }
        if (st == REC) {
            if (pos < cap) { l[pos] = inL; r[pos] = inR; pos++; }
            if (recTarget && pos >= recTarget) closeAt(recTarget);
            else if (pos >= cap) {
                int n = locked ? (int)(std::floor(cap / curBeat) * curBeat) : 0;
                arm = A_NONE;
                if (n > 0) closeBeats(n); else closeAt(cap);
            }
            return;
        }
        gain += ((playing() ? 1.f : 0.f) - gain) * (1.f - std::exp(-1.f / (0.004f * sr)));   // click-free start / stop
        int L = playLen > 0 ? std::min(playLen, len) : len;
        if (L <= 0 || (!playing() && gain < 1e-4f)) return;
        if (pos >= L) pos = 0;
        if (seamAt < seamF) {   // the seam: blend the head with the live run-on as it plays
            if (pos == seamAt && playing()) {
                float g = (float)pos / seamF;
                l[pos] = l[pos] * g + inL * (1.f - g); r[pos] = r[pos] * g + inR * (1.f - g);
                seamAt++;
            } else seamAt = seamF;   // the playhead moved away: leave the rest
        }
        float a = l[pos], b = r[pos];
        if (xf > 0) {   // the old head runs on (into the recording past a shortened loop) and fades out
            if (xfPos >= len) xfPos = 0;
            float g = (float)xf / kXf;
            a += (l[xfPos] - a) * g; b += (r[xfPos] - b) * g;
            xfPos++; xf--;
        }
        if (replace) {   // REC writes over the loop: the old audio fades out as the new fades in (3 ms)
            dubMix = st == DUB ? std::min(1.f, dubMix + 1.f / (0.003f * sr)) : std::max(0.f, dubMix - 1.f / (0.003f * sr));
            a *= 1.f - dubMix; b *= 1.f - dubMix;
        }
        oL = a * gain; oR = b * gain;
        if (replace && dubMix > 0.f) {
            l[pos] += (inL - l[pos]) * dubMix; r[pos] += (inR - r[pos]) * dubMix;
            int k = (int)((int64_t)pos * kPeaks / len);
            // a bucket being replaced starts fresh, so the waveform shows the new audio (silence too)
            if (st == DUB && (pos == 0 || (int)((int64_t)(pos - 1) * kPeaks / len) != k)) peak[k] = 0.f;
            peak[k] = std::max(peak[k], std::max(std::fabs(l[pos]), std::fabs(r[pos])));
        } else if (st == DUB) {
            l[pos] += inL; r[pos] += inR;
            int k = (int)((int64_t)pos * kPeaks / len);
            peak[k] = std::max(peak[k], std::max(std::fabs(l[pos]), std::fabs(r[pos])));
        }
        if (++pos >= L) {   // the top of the loop (a beat-synced loop's top is the beat edge in sync())
            if (L < len && xf == 0) jump(0);   // a shortened loop: crossfade its end into its start (2 ms)
            else pos = 0;
            if (arm == A_DUB && (st == ONCE || !(locked && beats > 0))) { arm = A_NONE; startDub(); }
            else if (st == ONCE) st = STOP;
        }
        if (!locked && beatLen > 0.0) beatIdx = (int)(pos / beatLen);
    }
};

} // namespace
