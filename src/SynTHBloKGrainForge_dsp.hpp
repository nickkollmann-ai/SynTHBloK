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

// Pitch steps shared by STRETCH (and labels): semitones, the last slot is reverse.
static const float kPitchSemis[8] = {-12.f, -7.f, -5.f, 0.f, 5.f, 7.f, 12.f, 0.f};
static const int kPitchReverse = 7;

// ---------------------------------------------------------------------------------------------
// GLITCH: on each step (or a GLITCH trigger), with CHANCE, one of:
//   ROLL     the start of this step repeated at 1/1, 1/2, 1/4 or 1/8 of a step (or random per event)
//   REVERSE  the last step played backwards
//   STOP     a tape-stop: playback slows to a halt over the step
// then CRUSH (bit depth + sample-rate reduction) on everything.
// ROLL slice lengths in clock steps (16ths); T = triplet. The last slot picks one at random.
static const int kRollCount = 11;
static const float kRollSteps[kRollCount] = {2.f, 4.f / 3.f, 1.f, 2.f / 3.f, 0.5f, 1.f / 3.f, 0.25f, 1.f / 6.f, 0.125f, 0.0625f, 0.f};
static const char* kRollName[kRollCount] = {"1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64", "1/64T", "1/128", "1/256", "RANDOM"};
static const int kRollOldIndex[5] = {2, 4, 6, 8, 10};   // the old 5-zone ROLL knob -> this list

struct Glitch {
    enum Ev { NONE, ROLL, REVERSE, STOP };
    StereoBuf buf;
    Rng rng{7};
    Ev ev = NONE;
    double start = 0.0, pos = 0.0;
    float speed = 1.f;
    int sliceLen = 1, evLen = 1, age = 0, rollDiv = 1;
    float holdL = 0.f, holdR = 0.f, holdCount = 0.f;

    // stepLen in samples; roll = index into kRollName; knobs 0..1. force = GLITCH trigger (always fires).
    void trigger(float stepLen, float chance, int roll, float rev, float stop, bool force) {
        if (buf.n == 0) return;
        if (!force && ev != NONE && evLen > stepLen * 1.01f) return;   // let a long (1/8) roll finish
        if (!force && rng.next() >= chance) { ev = NONE; return; }
        float wRoll = 1.f, wRev = rev * 2.f, wStop = stop * 2.f;
        float pick = rng.next() * (wRoll + wRev + wStop);
        ev = pick < wRoll ? ROLL : pick < wRoll + wRev ? REVERSE : STOP;
        age = 0;
        rollDiv = clampi(roll, 0, kRollCount - 1);
        if (rollDiv == kRollCount - 1) rollDiv = 1 + (int)(rng.next() * 8.f) % 8;   // RANDOM: 1/8T .. 1/128
        sliceLen = std::max(64, (int)(stepLen * kRollSteps[rollDiv]));
        // a roll longer than a step repeats twice; everything else lasts one step
        evLen = std::max(64, ev == ROLL ? std::max((int)stepLen, 2 * sliceLen) : (int)stepLen);
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
