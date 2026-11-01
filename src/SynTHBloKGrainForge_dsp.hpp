#pragma once
// DSP for SynTHBloK GRAIN-FORGE (header-only, no Rack deps so it can be tested offline).
// Chain: GLITCH -> STRETCH -> ROBOT -> WARP DELAY, with the delay's feedback able to loop back
// through STRETCH and ROBOT so repeats get stretched and "sung".
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace gforge {

static inline float clampf(float x, float a, float b) { return std::min(std::max(x, a), b); }

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

// Pitch steps shared by STRETCH (and labels): semitones, the last slot is reverse.
static const float kPitchSemis[8] = {-12.f, -7.f, -5.f, 0.f, 5.f, 7.f, 12.f, 0.f};
static const int kPitchReverse = 7;

// ---------------------------------------------------------------------------------------------
// GLITCH: on each step (or a GLITCH trigger), with CHANCE, one of:
//   ROLL     the start of this step repeated at 1/1, 1/2, 1/4 or 1/8 of a step (or random per event)
//   REVERSE  the last step played backwards
//   STOP     a tape-stop: playback slows to a halt over the step
// then CRUSH (bit depth + sample-rate reduction) on everything.
struct Glitch {
    enum Ev { NONE, ROLL, REVERSE, STOP };
    StereoBuf buf;
    Rng rng{7};
    Ev ev = NONE;
    double start = 0.0, pos = 0.0;
    float speed = 1.f;
    int sliceLen = 1, evLen = 1, age = 0, rollDiv = 1;
    float holdL = 0.f, holdR = 0.f, holdCount = 0.f;

    // stepLen in samples; knobs 0..1. force = GLITCH trigger (always fires).
    void trigger(float stepLen, float chance, float roll, float rev, float stop, bool force) {
        if (buf.n == 0) return;
        if (!force && rng.next() >= chance) { ev = NONE; return; }
        float wRoll = 1.f, wRev = rev * 2.f, wStop = stop * 2.f;
        float pick = rng.next() * (wRoll + wRev + wStop);
        ev = pick < wRoll ? ROLL : pick < wRoll + wRev ? REVERSE : STOP;
        evLen = std::max(64, (int)stepLen);
        age = 0;
        int slot = std::min(4, (int)(roll * 5.f));
        static const int divs[4] = {1, 2, 4, 8};
        rollDiv = slot < 4 ? divs[slot] : divs[(int)(rng.next() * 4.f) % 4];
        sliceLen = std::max(64, (int)(stepLen / rollDiv));
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
// STRETCH: granular time-stretch. A read head creeps through the recording at 1/STRETCH speed
// while grains (normal speed, so pitch is kept) are cut from around it: 1x = in time, 16x = a
// moment smeared over 16 times as long. When it falls too far behind it jumps back to "now".
// FREEZE stops recording; the head keeps scanning the frozen 2 seconds.
struct Stretch {
    StereoBuf buf;
    struct G { bool on = false; double pos = 0.0; float inc = 1.f; int age = 0, len = 1; };
    G g[10];
    double rp = 0.0;
    float spawn = 0.f;
    Rng rng{11};
    bool wasFrozen = false;

    void process(float inL, float inR, float sr, float stretch, float size, int pitchSlot, float spray,
                 bool freeze, float& oL, float& oR) {
        if (buf.n == 0) { buf.init((int)(sr * 8.f)); rp = 0.0; }
        if (!freeze) buf.write(inL, inR);
        int len = (int)(sr * 0.03f * std::pow(13.3f, size));    // 30..400 ms grains
        bool rev = pitchSlot == kPitchReverse;
        float ratio = std::pow(2.f, kPitchSemis[pitchSlot] / 12.f);
        float minLag = len * std::max(1.f, ratio) + 64.f;
        float frozenSpan = sr * 2.f;

        rp += 1.0 / std::max(1.f, stretch);
        double lag = buf.w - rp;
        if (freeze) {
            if (!wasFrozen) rp = buf.w - frozenSpan;              // start scanning the last 2 s
            if (buf.w - rp < minLag) rp = buf.w - frozenSpan;     // loop the frozen span
        } else if (lag < minLag || lag > buf.n - frozenSpan) {
            rp = buf.w - minLag;                                   // caught up / too far behind: back to now
        }
        wasFrozen = freeze;

        spawn -= 1.f;
        if (spawn <= 0.f) {
            spawn += len / 6.f;                                    // 6 overlapping grains: smoother
            for (G& gr : g) if (!gr.on) {
                gr.on = true; gr.age = 0; gr.len = len;
                gr.inc = rev ? -ratio : ratio;
                gr.pos = rp - spray * rng.next() * sr * 0.3f;   // SPRAY: up to 300 ms earlier
                if (rev) gr.pos += len * ratio;                    // read backwards over the same span
                break;
            }
        }
        float l = 0.f, r = 0.f;
        for (G& gr : g) if (gr.on) {
            float win = 0.5f - 0.5f * std::cos(2.f * float(M_PI) * gr.age / gr.len);
            l += buf.rd(buf.l, gr.pos) * win;
            r += buf.rd(buf.r, gr.pos) * win;
            gr.pos += gr.inc;
            if (++gr.age >= gr.len) gr.on = false;
        }
        oL = l * 0.5f; oR = r * 0.5f;                              // 6 Hann windows overlap to 3; +3.5 dB make-up
    }
};

// ---------------------------------------------------------------------------------------------
// ROBOT: a chord of tuned resonators (root + chord intervals) rings on the input, then a
// three-formant vowel filter (A-E-I-O-U morph) shapes it into a voice, plus ring-mod at the root.
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

static const char* kChordName[8] = {"HARM", "FIFTH", "MAJ", "MIN", "SUS4", "MAJ7", "MIN7", "CLUSTER"};
static const float kChord[8][4] = {
    {1.f, 2.f, 3.f, 4.f}, {1.f, 1.5f, 2.f, 3.f}, {1.f, 1.26f, 1.5f, 2.f}, {1.f, 1.189f, 1.5f, 2.f},
    {1.f, 1.335f, 1.5f, 2.f}, {1.f, 1.26f, 1.5f, 1.888f}, {1.f, 1.189f, 1.5f, 1.782f}, {1.f, 1.059f, 1.122f, 1.189f}};
// Vowel formants F1..F3 in Hz: A E I O U.
static const float kVowel[5][3] = {{800, 1150, 2900}, {400, 1600, 2700}, {350, 2300, 3000}, {450, 800, 2830}, {325, 700, 2530}};

struct Robot {
    Svf resL[4], resR[4], fL[3], fR[3];
    float ph = 0.f;
    int count = 0;
    // rootSemis 0..24 above A1 (55 Hz); chord 0..7; vowel/ring/q 0..1.
    void process(float inL, float inR, float sr, float rootSemis, int chord, float vowel, float ring, float q,
                 float& oL, float& oR) {
        float root = 55.f * std::pow(2.f, rootSemis / 12.f);
        if ((count++ & 31) == 0) {                                 // retune every 32 samples (inputs are smoothed)
            float kr = 2.f - 1.85f * q;                            // gentler peak at full RES
            for (int i = 0; i < 4; i++) { resL[i].set(root * kChord[chord][i], sr, kr); resR[i].set(root * kChord[chord][i] * 1.003f, sr, kr); }
            float v = clampf(vowel, 0.f, 1.f) * 4.f;
            int a = std::min(3, (int)v); float f = v - a;
            for (int i = 0; i < 3; i++) {
                float fc = kVowel[a][i] + (kVowel[a + 1][i] - kVowel[a][i]) * f;
                fL[i].set(fc, sr, 0.5f); fR[i].set(fc * 1.01f, sr, 0.5f);   // wider, softer formants
            }
        }
        float rl = 0.f, rr = 0.f;
        for (int i = 0; i < 4; i++) { rl += resL[i].bp(inL) * resL[i].k; rr += resR[i].bp(inR) * resR[i].k; }
        rl *= 0.6f; rr *= 0.6f;
        static const float fg[3] = {1.2f, 0.85f, 0.6f};
        float vl = 0.f, vr = 0.f;
        for (int i = 0; i < 3; i++) { vl += fL[i].bp(rl) * fg[i]; vr += fR[i].bp(rr) * fg[i]; }
        ph += root / sr; if (ph >= 1.f) ph -= 1.f;
        float car = std::sin(2.f * float(M_PI) * ph);
        oL = vl * (1.f - ring) + vl * car * ring * 1.6f;
        oR = vr * (1.f - ring) + vr * car * ring * 1.6f;
    }
};

// ---------------------------------------------------------------------------------------------
// WARP DELAY: clock-locked stereo delay. Its feedback is pitch-shifted by WARP (semitones per
// repeat), darkened by TONE, soft-clipped, and either goes straight back in or (ROUTE) loops
// through STRETCH and ROBOT first. Only steps whose throw buttons are lit are fed in.
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

static const float kDelaySteps[7] = {1.f, 2.f, 3.f, 4.f, 6.f, 8.f, 16.f};   // in clock steps (16ths)
static const char* kDelayName[7] = {"1/16", "1/8", "3/16", "1/4", "3/8", "1/2", "1 BAR"};

struct WarpDelay {
    StereoBuf buf;
    PitchShift psL, psR;
    float time = 0.375f, gate = 0.f, lpL = 0.f, lpR = 0.f, hpL = 0.f, hpR = 0.f;
    float fbEnv = 0.f;              // level of the feedback signal, for the loop limiter
    float wetL = 0.f, wetR = 0.f;   // last read, for this sample
    // Read the delay (call first each sample), and build the feedback signal. The loop can't run
    // away: feedback tops out at 85%, a low cut stops bass building up, and a level limiter rides
    // the feedback down whenever the repeats get loud (e.g. when looped through STRETCH/ROBOT).
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
