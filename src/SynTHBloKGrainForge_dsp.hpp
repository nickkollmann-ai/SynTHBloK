#pragma once
// DSP for SynTHBloK GRAIN-FORGE (header-only, no Rack deps so it can be tested offline).
// Chain: SHUFFLE -> GLITCH -> ROBOT -> PULSE -> WARP DELAY, with the delay's feedback able to loop
// back through ROBOT and PULSE so repeats get "sung" and gated / filtered in time.
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace gforge {

static inline float clampf(float x, float a, float b) { return std::min(std::max(x, a), b); }
static inline int clampi(int x, int a, int b) { return std::min(std::max(x, a), b); }

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1) : s(seed * 2654435761u + 1u) {}
    float next() { s = s * 1664525u + 1013904223u; return (s >> 8) * (1.f / 16777216.f); }
};

// Stereo ring buffer with linear-interpolated reads at fractional positions.
struct StereoBuf {
    std::vector<float> l, r;
    int n = 0, w = 0;
    void init(int size) { n = std::max(size, 16); l.assign(n, 0.f); r.assign(n, 0.f); w = 0; }
    void write(float a, float b) { l[w] = a; r[w] = b; w = (w + 1) % n; }
    float rd(const std::vector<float>& b, double p) const {
        p = std::fmod(p, (double)n); if (p < 0) p += n;
        int i = (int)p; float f = (float)(p - i);
        return b[i] + (b[(i + 1) % n] - b[i]) * f;
    }
};

// ---------------------------------------------------------------------------------------------
// SHUFFLE: a beat slicer. It records all the time and, on each clock step, picks which of the last
// LENGTH steps (4 / 8 / 16) to play: PATTERN says which slice goes where, AMOUNT is the chance a
// step is rearranged at all, REVERSE the chance a moved slice plays backwards, PITCH the chance it
// plays an octave up or down. A 3 ms crossfade hides every jump.
static const int kShufCount = 8;
static const char* kShufName[kShufCount] = {"SWAP", "BACKWARD", "DOUBLE", "HALF", "LOOP", "THIRDS", "SCATTER", "RANDOM"};
static const int kShufPat[kShufCount - 1][16] = {
    {1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14},
    {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
    {0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, 12, 14, 14},
    {0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7},
    {0, 1, 2, 3, 0, 1, 2, 3, 8, 9, 10, 11, 8, 9, 0, 1},
    {0, 3, 6, 9, 12, 15, 2, 5, 8, 11, 14, 1, 4, 7, 10, 13},
    {0, 7, 2, 13, 4, 11, 6, 1, 8, 15, 10, 3, 12, 5, 14, 9}};
static const int kShufLen[3] = {4, 8, 16};
static const char* kShufLenName[3] = {"1 BEAT", "2 BEATS", "1 BAR"};

struct Shuffle {
    StereoBuf buf;
    Rng rng{23};
    struct Rd { double pos = 0.0; float inc = 1.f; bool live = true; };
    Rd cur, old;
    float xf = 1.f;     // 0 -> 1: crossfade from the old slice to the new one
    int src = -1;       // slice now playing (-1 = live), for the display

    // stepLen in samples; edge = a clock step starts now; stepIdx = 0..15 position in the bar.
    void process(float inL, float inR, float sr, float stepLen, bool edge, int stepIdx, int pattern, float amount,
                 float rev, float pitch, int lenSlot, float& oL, float& oR) {
        if (buf.n == 0) buf.init((int)(sr * 10.f));
        buf.write(inL, inR);
        double now = buf.w - 1;   // the sample just written
        if (edge && stepIdx >= 0) {
            int L = kShufLen[clampi(lenSlot, 0, 2)];
            int s = stepIdx % L, j = s;
            if (rng.next() < amount) {
                int p = clampi(pattern, 0, kShufCount - 1);
                j = p == kShufCount - 1 ? (int)(rng.next() * L) % L : kShufPat[p][stepIdx & 15] % L;
            }
            int d = (s - j + L) % L;                                    // steps back to that slice
            if ((d + 2) * stepLen > buf.n) d = 0;                       // very slow clock: stay live
            bool back = d > 0 && rng.next() < rev;
            float sp = (d > 0 && rng.next() < pitch) ? (rng.next() < 0.5f ? 2.f : 0.5f) : 1.f;
            bool live = d == 0;
            if (!(live && cur.live)) {                                  // live -> live needs no jump
                old = cur; xf = 0.f;
                double start = now - d * (double)stepLen;
                cur.inc = back ? -sp : sp;
                cur.pos = back ? start + stepLen * sp : start;
                cur.live = live;
            }
            src = live ? -1 : j;
        }
        float aL, aR;
        if (cur.live) { aL = inL; aR = inR; }
        else { aL = buf.rd(buf.l, cur.pos); aR = buf.rd(buf.r, cur.pos); }
        if (xf < 1.f) {
            float bL = old.live ? inL : buf.rd(buf.l, old.pos), bR = old.live ? inR : buf.rd(buf.r, old.pos);
            float x = xf * xf * (3.f - 2.f * xf);
            aL = bL + (aL - bL) * x; aR = bR + (aR - bR) * x;
            xf = std::min(1.f, xf + 1.f / (0.003f * sr));
            old.pos += old.inc;
        }
        cur.pos += cur.inc;
        oL = aL; oR = aR;
    }
};

// ---------------------------------------------------------------------------------------------
// GLITCH: on each step (or a GLITCH trigger), with CHANCE, one of:
//   ROLL     the start of this step repeated at 1/1, 1/2, 1/4 or 1/8 of a step (or random per event)
//   REVERSE  the last step played backwards
//   STOP     a tape-stop: playback slows to a halt over the step
// then CRUSH (bit depth + sample-rate reduction) on everything.
// ROLL slice lengths in clock steps (16ths); T = triplet. Rolls start on the beat and last a beat.
static const int kRollCount = 7;
static const float kRollSteps[kRollCount] = {4.f, 2.f, 4.f / 3.f, 1.f, 2.f / 3.f, 0.5f, 0.25f};
static const char* kRollName[kRollCount] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64"};
static const int kRollV1Index[5] = {3, 5, 6, 6, 3};                    // the first 5-zone ROLL knob -> this list
static const int kRollV2Index[11] = {1, 2, 3, 4, 5, 5, 6, 6, 6, 6, 3}; // the 11-slot ROLL switch -> this list

struct Glitch {
    enum Ev { NONE, ROLL, REVERSE, STOP };
    StereoBuf buf;
    Rng rng{7};
    Ev ev = NONE;
    double start = 0.0, pos = 0.0;
    float speed = 1.f;
    int sliceLen = 1, evLen = 1, age = 0, rollDiv = 1;
    float holdL = 0.f, holdR = 0.f, holdCount = 0.f;

    // stepLen in samples; roll = index into kRollName; knobs 0..1. onBeat = this step starts a beat
    // (rolls only start there, so they always sit on the grid). force = GLITCH trigger (always fires).
    void trigger(float stepLen, float chance, int roll, float rev, float stop, bool onBeat, bool force) {
        if (buf.n == 0) return;
        if (!force && ev != NONE && evLen > stepLen * 1.01f) return;   // let a roll finish its beat
        if (!force && rng.next() >= chance) { ev = NONE; return; }
        float wRoll = onBeat ? 1.f : 0.f, wRev = rev * 2.f, wStop = stop * 2.f;
        if (wRoll + wRev + wStop <= 0.f) { ev = NONE; return; }
        float pick = rng.next() * (wRoll + wRev + wStop);
        ev = pick < wRoll ? ROLL : pick < wRoll + wRev ? REVERSE : STOP;
        age = 0;
        rollDiv = clampi(roll, 0, kRollCount - 1);
        sliceLen = std::max(64, (int)std::round(stepLen * kRollSteps[rollDiv]));
        // a roll lasts one beat (two repeats for 1/4); REVERSE and STOP last one step
        evLen = std::max(64, ev == ROLL ? std::max((int)std::round(stepLen * 4.f), 2 * sliceLen) : (int)stepLen);
        start = buf.w - 1;       // ROLL captures live from here; REVERSE / STOP read back from here
        pos = start;
        speed = 1.f;
    }
    void process(float inL, float inR, float sr, float crush, float& oL, float& oR) {
        if (buf.n == 0) buf.init((int)(sr * 4.f));
        buf.write(inL, inR);
        float eL = inL, eR = inR;
        if (ev != NONE) {
            float fade = 0.006f * sr;                                        // 6 ms in and out of an event
            float blend = std::min(1.f, std::min(age / fade, (evLen - age) / fade));
            blend = blend * blend * (3.f - 2.f * blend);                     // S-curve: no corners
            float pL, pR;
            if (ev == ROLL) {
                float ph = (float)(age % sliceLen);
                float sliceFade = std::min(0.004f * sr, sliceLen * 0.25f);   // soft edges on each repeat
                float edge = std::min(1.f, std::min(ph, sliceLen - ph) / sliceFade);
                pL = buf.rd(buf.l, start + ph) * edge;
                pR = buf.rd(buf.r, start + ph) * edge;
            } else if (ev == REVERSE) {
                pL = buf.rd(buf.l, start - age);
                pR = buf.rd(buf.r, start - age);
            } else {   // STOP
                pL = buf.rd(buf.l, pos);
                pR = buf.rd(buf.r, pos);
                speed = std::max(0.f, 1.f - (float)age / evLen);
                pos += speed;
            }
            eL = inL + (pL - inL) * blend;
            eR = inR + (pR - inR) * blend;
            if (++age >= evLen) ev = NONE;
        }
        if (crush > 0.001f) {
            float factor = 1.f + crush * crush * 31.f;          // hold each sample up to 32x
            float levels = std::pow(2.f, 16.f - crush * 13.f);  // 16 bits down to 3
            holdCount += 1.f;
            if (holdCount >= factor) {
                holdCount -= factor;
                holdL = std::round(eL * levels) / levels;
                holdR = std::round(eR * levels) / levels;
            }
            eL = holdL; eR = holdR;
        }
        oL = eL; oR = eR;
    }
};

// ---------------------------------------------------------------------------------------------
// ROBOT: a vocoder whose carrier is a saw chord (root + chord intervals), then a three-formant
// vowel filter (A-E-I-O-U morph) shapes it into a voice, plus ring-mod at the root.
struct Svf {
    float ic1 = 0.f, ic2 = 0.f, a1 = 0.f, a2 = 0.f, a3 = 0.f, k = 1.f;
    void set(float fc, float sr, float kk) {
        float g = std::tan(float(M_PI) * clampf(fc, 20.f, sr * 0.45f) / sr);
        k = kk; a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    float bp(float x) {
        float v3 = x - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1; ic2 = 2.f * v2 - ic2;
        return v1;
    }
};

// Master filter: CUT 0..1 sweeps 20 Hz .. 20 kHz, MODE 0..2 = LP / BP / HP. RES 0..1 goes from a
// gentle bump (Q 0.8) to a capped peak (Q 5); the level drops a little as it rises, so it sings but
// never screams. STAGES 0..2: 1, 2 or 3 SVFs in series = 12 / 24 / 36 dB per octave; the first stage
// carries the resonance, the extra ones are plain. LP fully open and HP fully down fade to true
// bypass. Every mode and stage always runs and MODE / STAGES arrive smoothed, so switching crossfades.
struct DjFilter {
    float ic1[3][3][2] = {}, ic2[3][3][2] = {};   // [mode][stage][channel]
    void process(float& l, float& r, float cut, float res, float sr, float stages = 0.f, float mode = 0.f) {
        cut = clampf(cut, 0.f, 1.f);
        float fc = 20.f * std::pow(1000.f, cut);
        float g = std::tan(float(M_PI) * clampf(fc, 20.f, sr * 0.45f) / sr);
        float kr = 1.2f * std::pow(0.2f / 1.2f, clampf(res, 0.f, 1.f));
        float comp = std::pow(kr / 1.2f, 0.3f);
        float wetM[3] = {clampf((0.99f - cut) / 0.06f, 0.f, 1.f), 1.f, clampf((cut - 0.01f) / 0.06f, 0.f, 1.f)};
        stages = clampf(stages, 0.f, 2.f); mode = clampf(mode, 0.f, 2.f);
        int s0 = std::min((int)stages, 1), m0 = std::min((int)mode, 1);
        float sf = stages - s0, mf = mode - m0;
        float* io[2] = {&l, &r};
        for (int c = 0; c < 2; c++) {
            float x = *io[c], out[3];
            for (int m = 0; m < 3; m++) {
                float y = x, tap[3];
                for (int s = 0; s < 3; s++) {
                    float k = s == 0 ? kr : 1.414f;
                    float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
                    float v3 = y - ic2[m][s][c];
                    float v1 = a1 * ic1[m][s][c] + a2 * v3;
                    float v2 = ic2[m][s][c] + a2 * ic1[m][s][c] + a3 * v3;
                    ic1[m][s][c] = 2.f * v1 - ic1[m][s][c]; ic2[m][s][c] = 2.f * v2 - ic2[m][s][c];
                    y = m == 0 ? v2 : m == 1 ? k * v1 : y - k * v1 - v2;   // BP scaled to unity peak
                    tap[s] = y;
                }
                float yf = (tap[s0] + (tap[s0 + 1] - tap[s0]) * sf) * comp;
                out[m] = x + (yf - x) * wetM[m];
            }
            *io[c] = out[m0] + (out[m0 + 1] - out[m0]) * mf;
        }
    }
};

// Filter ADSR, retriggered in time. Times in seconds; exponential segments, so no clicks.
struct Adsr {
    enum { IDLE, ATK, DEC, REL } st = IDLE;
    float v = 0.f;
    void trigger() { st = ATK; }
    void release() { if (st != IDLE) st = REL; }
    float process(float atk, float dec, float sus, float rel, float sr) {
        switch (st) {
            case ATK:   // aims past 1 so it really arrives
                v += (1.3f - v) * (1.f - std::exp(-1.f / (atk * sr)));
                if (v >= 1.f) { v = 1.f; st = DEC; }
                break;
            case DEC: v += (sus - v) * (1.f - std::exp(-1.f / (dec * sr))); break;
            case REL:
                v += (0.f - v) * (1.f - std::exp(-1.f / (rel * sr)));
                if (v < 1e-4f) { v = 0.f; st = IDLE; }
                break;
            default: break;
        }
        return v;
    }
};

static const char* kChordName[8] = {"HARM", "FIFTH", "MAJ", "MIN", "SUS4", "MAJ7", "MIN7", "CLUSTER"};
static const float kChord[8][4] = {
    {1.f, 2.f, 3.f, 4.f}, {1.f, 1.5f, 2.f, 3.f}, {1.f, 1.26f, 1.5f, 2.f}, {1.f, 1.189f, 1.5f, 2.f},
    {1.f, 1.335f, 1.5f, 2.f}, {1.f, 1.26f, 1.5f, 1.888f}, {1.f, 1.189f, 1.5f, 1.782f}, {1.f, 1.059f, 1.122f, 1.189f}};
// Vowel formants F1..F3 in Hz: A E I O U.
static const float kVowel[5][3] = {{800, 1150, 2900}, {400, 1600, 2700}, {350, 2300, 3000}, {450, 800, 2830}, {325, 700, 2530}};

// A 14-band vocoder: the input's band levels shape a buzzing saw chord (ROOT + CHORD), so the
// input "speaks" through the chord. Its hiss is mixed back in for consonants, and the vowel
// formants move a little with the input level, so the voice "talks".
static inline float blep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}

struct Robot {
    static const int NB = 14;
    struct Ch {
        Svf ana[NB], syn[NB], form[3];
        float env[NB] = {}, ph[4] = {}, hp1 = 0.f, hp2 = 0.f;
    } ch[2];
    float bandK[NB] = {}, bandGain[NB] = {};
    float lastSr = 0.f, atk = 0.f, rel = 0.f, talkEnv = 0.f, talkC = 0.f, hpC = 0.f, ringPh = 0.f;
    int count = 0;

    void setup(float sr) {
        lastSr = sr;
        for (int b = 0; b < NB; b++) {
            float fc = 120.f * std::pow(7000.f / 120.f, b / float(NB - 1));
            bandK[b] = 0.28f;
            bandGain[b] = 1.f + 0.6f * b / float(NB - 1);           // a little presence lift up top
            for (Ch& c : ch) { c.ana[b].set(fc, sr, bandK[b]); c.syn[b].set(fc, sr, bandK[b]); }
        }
        atk = 1.f - std::exp(-1.f / (0.002f * sr));
        rel = 1.f - std::exp(-1.f / (0.03f * sr));
        talkC = 1.f - std::exp(-1.f / (0.06f * sr));
        hpC = 1.f - std::exp(-2.f * float(M_PI) * 4000.f / sr);
    }

    // rootSemis 0..24 above A1 (55 Hz); chord 0..7; vowel/ring/q 0..1 (q = formant sharpness).
    void process(float inL, float inR, float sr, float rootSemis, int chord, float vowel, float ring, float q,
                 float& oL, float& oR) {
        if (sr != lastSr) setup(sr);
        float root = 55.f * std::pow(2.f, rootSemis / 12.f);
        // syllables: louder input opens the vowel a little
        float lvl = 0.5f * (std::fabs(inL) + std::fabs(inR));
        talkEnv += (lvl - talkEnv) * talkC;
        if ((count++ & 31) == 0) {                                 // retune formants every 32 samples
            float v = clampf(vowel + 0.22f * std::tanh(talkEnv * 3.f) - 0.06f, 0.f, 1.f) * 4.f;
            int a = std::min(3, (int)v); float f = v - a;
            float fk = 0.7f - 0.55f * q;                           // RES sharpens the vowel
            for (int i = 0; i < 3; i++) {
                float fc = kVowel[a][i] + (kVowel[a + 1][i] - kVowel[a][i]) * f;
                ch[0].form[i].set(fc, sr, fk); ch[1].form[i].set(fc * 1.01f, sr, fk);
            }
        }
        static const float fg[3] = {1.f, 0.8f, 0.55f};
        float in[2] = {inL, inR}, out[2];
        for (int c = 0; c < 2; c++) {
            Ch& h = ch[c];
            // carrier: four saws on the chord notes, right side detuned slightly for width
            float car = 0.f;
            for (int i = 0; i < 4; i++) {
                float dt = root * kChord[chord][i] * (c ? 1.004f : 1.f) / sr;
                h.ph[i] += dt; if (h.ph[i] >= 1.f) h.ph[i] -= 1.f;
                car += (2.f * h.ph[i] - 1.f - blep(h.ph[i], dt)) * (i ? 0.7f : 1.f);
            }
            car *= 0.35f;
            float voc = 0.f;
            for (int b = 0; b < NB; b++) {
                float a = std::fabs(h.ana[b].bp(in[c]) * bandK[b]);
                h.env[b] += (a - h.env[b]) * (a > h.env[b] ? atk : rel);
                voc += h.syn[b].bp(car) * bandK[b] * h.env[b] * bandGain[b];
            }
            voc *= 3.5f;
            float fo = 0.f;
            for (int i = 0; i < 3; i++) fo += h.form[i].bp(voc) * h.form[i].k * fg[i];
            float y = voc * 0.45f + fo * (1.2f + 1.5f * q);
            // consonants: the input's hiss above ~4 kHz goes straight through
            h.hp1 += (in[c] - h.hp1) * hpC; h.hp2 += (h.hp1 - h.hp2) * hpC;
            y += (in[c] - h.hp2) * 0.25f;
            out[c] = y;
        }
        ringPh += root / sr; if (ringPh >= 1.f) ringPh -= 1.f;
        float rm = 1.f - ring + ring * 1.6f * std::sin(2.f * float(M_PI) * ringPh);
        oL = out[0] * rm; oR = out[1] * rm;
    }
};

// ---------------------------------------------------------------------------------------------
// PULSE: a clock-locked gate + resonant filter. Each step of the PATTERN that is on fires an
// envelope (2 ms attack, DECAY from a click to a few steps long) that opens the gate and sweeps
// the filter up, both by DEPTH. MODE picks low-, band- or high-pass. OPEN (gate input) opens it fully.
static const int kPulseCount = 8;
static const char* kPulseName[kPulseCount] = {"16THS", "8THS", "OFFBEAT", "TRANCE", "DOTTED", "SYNCO", "BUILD", "RANDOM"};
static const char* kPulsePat[kPulseCount - 1] = {   // x = the step fires
    "xxxxxxxxxxxxxxxx", "x.x.x.x.x.x.x.x.", "..x...x...x...x.", "x.xx.xx.x.xx.xx.",
    "x..x..x..x..x.x.", "x..x..x.x..x.x..", "x...x...x.x.xxxx"};
static const char* kModeName[3] = {"LP", "BP", "HP"};

struct Pulse {
    float ic1[2] = {}, ic2[2] = {};
    float env = 0.f, open = 0.f;
    bool attacking = false, fired = false;   // fired: a pattern step hit this sample
    Rng rng{31};

    void process(float inL, float inR, float sr, float stepLen, bool edge, int stepIdx, int pattern,
                 float cutoff, float res, float depth, float decay, int mode, bool openGate, float& oL, float& oR) {
        fired = false;
        if (edge && stepIdx >= 0) {
            int p = clampi(pattern, 0, kPulseCount - 1);
            if (p == kPulseCount - 1 ? rng.next() < 0.6f : kPulsePat[p][stepIdx & 15] == 'x') attacking = fired = true;
        }
        if (attacking) {
            env += (1.05f - env) * (1.f - std::exp(-1.f / (0.002f * sr)));
            if (env >= 1.f) { env = 1.f; attacking = false; }
        } else {
            env *= std::exp(-1.f / (stepLen * (0.12f + 3.9f * decay * decay)));   // ~1/8 step .. 4 steps
        }
        open += ((openGate ? 1.f : 0.f) - open) * (1.f - std::exp(-1.f / (0.01f * sr)));

        float fc = 30.f * std::pow(600.f, cutoff) * std::pow(2.f, depth * 4.f * env);   // 30 Hz .. 18 kHz, +4 oct
        float g = std::tan(float(M_PI) * clampf(fc, 20.f, sr * 0.45f) / sr);
        float k = 2.f - 1.85f * res;
        float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        float amp = 1.f - depth * (1.f - env);                      // DEPTH 1: silent between hits
        float in[2] = {inL, inR}, out[2];
        for (int c = 0; c < 2; c++) {
            float x = in[c];
            float v3 = x - ic2[c];
            float v1 = a1 * ic1[c] + a2 * v3;
            float v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
            ic1[c] = 2.f * v1 - ic1[c]; ic2[c] = 2.f * v2 - ic2[c];
            float y = mode == 0 ? v2 : mode == 1 ? v1 * std::sqrt(k) * 1.6f : x - k * v1 - v2;
            y *= amp * (mode == 1 ? 1.f : 1.f - 0.35f * res);         // resonant LP / HP peaks run hot: trim
            y = y / std::sqrt(1.f + 0.3f * y * y);                  // soft saturation tames resonant peaks
            out[c] = y + (x - y) * open;
        }
        oL = out[0]; oR = out[1];
    }
};

// ---------------------------------------------------------------------------------------------
// WARP DELAY: clock-locked stereo delay. Its feedback is pitch-shifted by WARP (semitones per
// repeat), darkened by TONE, soft-clipped, and either goes straight back in or (ROUTE) loops
// through ROBOT and PULSE first. Only steps whose throw buttons are lit are fed in.
struct PitchShift {   // two crossfaded taps sweeping a 60 ms window
    std::vector<float> b; int w = 0, n = 0; float ph = 0.f;
    float process(float x, float ratio, float sr) {
        if (n == 0) { n = (int)(sr * 0.12f); b.assign(n, 0.f); }
        b[w] = x;
        float win = sr * 0.06f;
        ph += (1.f - ratio) / win; ph -= std::floor(ph);
        float out = 0.f;
        for (int t = 0; t < 2; t++) {
            float p = std::fmod(ph + t * 0.5f, 1.f);
            float d = p * win + 4.f;
            float pos = w - d; while (pos < 0) pos += n;
            int i = (int)pos; float f = pos - i;
            float s = b[i] + (b[(i + 1) % n] - b[i]) * f;
            out += s * std::sin(float(M_PI) * p);                   // equal-sum crossfade
        }
        w = (w + 1) % n;
        return out;
    }
};

// in clock steps (16ths); T = triplet, D = dotted
static const int kDelayCount = 13;
static const float kDelaySteps[kDelayCount] = {0.25f, 0.5f, 2.f / 3.f, 1.f, 4.f / 3.f, 1.5f, 2.f, 8.f / 3.f, 3.f, 4.f, 6.f, 8.f, 16.f};
static const char* kDelayName[kDelayCount] = {"1/64", "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/4D", "1/2", "1 BAR"};
static const int kDelayOldIndex[7] = {3, 6, 8, 9, 10, 11, 12};   // the old 7-slot TIME switch -> this list

struct WarpDelay {
    StereoBuf buf;
    PitchShift psL, psR;
    float time = 0.375f, gate = 0.f, lpL = 0.f, lpR = 0.f, hpL = 0.f, hpR = 0.f;
    float fbEnv = 0.f;              // level of the feedback signal, for the loop limiter
    float wetL = 0.f, wetR = 0.f;   // last read, for this sample
    // Read the delay (call first each sample), and build the feedback signal. The loop can't run
    // away: feedback tops out at 85%, a low cut stops bass building up, and a level limiter rides
    // the feedback down whenever the repeats get loud (e.g. when looped through ROBOT/PULSE).
    void read(float sr, float targetTime, float fdbk, float warpSemis, float tone, float& fbL, float& fbR) {
        if (buf.n == 0) buf.init((int)(sr * 4.1f));
        time += (clampf(targetTime, 0.01f, 4.f) - time) * 0.0002f;   // slow glide: no pitchy zips on time changes
        double p = buf.w - (double)time * sr;
        wetL = buf.rd(buf.l, p); wetR = buf.rd(buf.r, p);
        float ratio = std::pow(2.f, warpSemis / 12.f);
        float sl = std::fabs(warpSemis) > 0.05f ? psL.process(wetL, ratio, sr) : wetL;
        float sr2 = std::fabs(warpSemis) > 0.05f ? psR.process(wetR, ratio, sr) : wetR;
        float k = 1.f - std::exp(-2.f * float(M_PI) * 300.f * std::pow(30.f, tone) / sr);   // 300 Hz .. 9 kHz
        lpL += (sl - lpL) * k; lpR += (sr2 - lpR) * k;
        float kh = 1.f - std::exp(-2.f * float(M_PI) * 80.f / sr);                        // low cut at 80 Hz
        hpL += (lpL - hpL) * kh; hpR += (lpR - hpR) * kh;
        float xL = lpL - hpL, xR = lpR - hpR;
        float lvl = std::max(std::fabs(xL), std::fabs(xR));
        fbEnv += (lvl - fbEnv) * (lvl > fbEnv ? 0.01f : 0.0005f);
        float ride = fbEnv > 0.3f ? 0.3f / fbEnv : 1.f;
        float fb = fdbk * 0.85f * ride;
        fbL = fb * (0.75f * xL + 0.25f * xR);   // a little cross-feed for width
        fbR = fb * (0.75f * xR + 0.25f * xL);
    }
    // Write this sample's input (already gated / routed by the caller).
    void write(float inL, float inR, float sr, bool throwOn) {
        gate += ((throwOn ? 1.f : 0.f) - gate) * (1.f - std::exp(-1.f / (0.003f * sr)));
        buf.write(inL * gate, inR * gate);
    }
    void writeRaw(float l, float r) { buf.l[(buf.w + buf.n - 1) % buf.n] += l; buf.r[(buf.w + buf.n - 1) % buf.n] += r; }
};

} // namespace gforge
