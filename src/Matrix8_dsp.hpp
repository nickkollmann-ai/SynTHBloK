#pragma once
// Shared DSP for SynTHBloK MATRIX-8 and MATRIX FORGE: the 8 row voices (delay and reverb: Echo_dsp.hpp).
#include "plugin.hpp"
#include "Echo_dsp.hpp"   // M8Delay / M8Reverb
#include <cmath>
#include <vector>

namespace {

// ---- synth voice ------------------------------------------------------------
enum VoiceParam { VP_PITCH, VP_CUTOFF, VP_RES, VP_DECAY, VP_WAVE, VP_LEVEL, VP_PAN, VP_DRIVE };
static const char* kBankName[8] = {"PITCH", "CUTOFF", "RESO", "DECAY", "WAVE", "LEVEL", "PAN", "DRIVE"};
// Starting strip values for a new module, per bank (row 1 = top voice).
// Pitch: a minor pentatonic run from C2 (bottom row) up; 36 semitones across a full strip.
static const float kDefaultVal[8][8] = {
    {17 / 36.f, 15 / 36.f, 12 / 36.f, 10 / 36.f, 7 / 36.f, 5 / 36.f, 3 / 36.f, 0.f},
    {0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f},
    {0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f},
    {0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {0.75f, 0.75f, 0.75f, 0.75f, 0.75f, 0.75f, 0.75f, 0.75f},
    {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f},
    {0.15f, 0.15f, 0.15f, 0.15f, 0.15f, 0.15f, 0.15f, 0.15f}};

static float polyBlep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}

// Morphing wave at `phase` (0..1): saw -> square -> sine as w goes 0 -> 0.5 -> 1 (band-limited edges).
static float morphWave(float phase, float dt, float w) {
    float saw = 2.f * phase - 1.f - polyBlep(phase, dt);
    float t2 = phase + 0.5f; if (t2 >= 1.f) t2 -= 1.f;
    float sq = (phase < 0.5f ? 1.f : -1.f) + polyBlep(phase, dt) - polyBlep(t2, dt);
    float sine = std::sin(2.f * float(M_PI) * phase);
    return w < 0.5f ? saw + (sq - saw) * (w * 2.f) : sq + (sine - sq) * ((w - 0.5f) * 2.f);
}

// Morphing oscillator.
struct M8Osc {
    float phase = 0.f;
    float process(float dt, float w) {
        phase += dt;
        if (phase >= 1.f) phase -= 1.f;
        return morphWave(phase, dt, w);
    }
};

// TPT state-variable filter: low-, band- and high-pass from one pass.
struct M8Svf {
    float ic1 = 0.f, ic2 = 0.f;
    void process(float x, float fc, float res, float sr, float& lp, float& bp, float& hp) {
        float g = std::tan(float(M_PI) * std::min(fc, sr * 0.45f) / sr);
        float k = 2.f - 1.9f * res;
        float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        float v3 = x - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

// Shared "filter 2 + envelope" section (SynTHBloK style), the same for all 8 voices.
struct M8Global {
    float cutoff = 1.f, res = 0.f, envAmt = 0.f, mode = 0.f;   // 0..1 each
    float a = 0.f, d = 0.5f, s = 1.f, r = 0.6f;                // 0..1 each
    float gate = 0.125f;                                       // note length in seconds (one clock step)
};

// One voice: two detuned morphing oscillators plus a sine sub an octave down, a slow random
// "analog" drift on pitch and cutoff, resonant low-pass swept by its decay (the strips), then
// the shared filter 2 (LP -> BP -> HP morph) swept by the shared ADSR, drive, and the ADSR on
// the level as well.
struct M8Voice {
    M8Osc oscA, oscB;
    M8Svf f1, f2;
    float subPhase = 0.f, env = 0.f, cut = 0.5f;
    float adsr = 0.f, gateLeft = 0.f;
    int stage = 0;   // 0 idle, 1 attack, 2 decay/sustain, 3 release
    float drift = 0.f, driftTarget = 0.f, driftTimer = 0.f;
    uint32_t rng = 1;
    explicit M8Voice(uint32_t seed = 1) : rng(seed * 2654435761u + 1u) { oscB.phase = 0.37f; }
    float rand01() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) * (1.f / 16777216.f); }
    void trigger(float gate) { env = 1.f; stage = 1; gateLeft = gate; }
    float process(float sr, const float* p, const M8Global& g) {
        // ADSR: attack 1 ms..2 s, decay 5 ms..2 s, release 5 ms..4 s (time constants)
        auto coef = [&](float t) { return 1.f - std::exp(-1.f / (t * sr)); };
        gateLeft -= 1.f / sr;
        if (gateLeft <= 0.f && (stage == 1 || stage == 2)) stage = 3;
        if (stage == 1) {
            adsr += (1.2f - adsr) * coef(0.001f * std::pow(2000.f, g.a));
            if (adsr >= 1.f) { adsr = 1.f; stage = 2; }
        } else if (stage == 2) {
            adsr += (g.s - adsr) * coef(0.005f * std::pow(400.f, g.d));
        } else if (stage == 3) {
            adsr += (0.f - adsr) * coef(0.005f * std::pow(800.f, g.r));
            if (adsr < 1e-4f) { adsr = 0.f; stage = 0; }
        }

        // drift: a new random target every ~0.3-1 s, glided to; +-1 = +-6 cents / +-0.03 cutoff
        driftTimer -= 1.f / sr;
        if (driftTimer <= 0.f) { driftTarget = rand01() * 2.f - 1.f; driftTimer = 0.3f + 0.7f * rand01(); }
        drift += (driftTarget - drift) * (2.f / sr);

        float semis = std::round(p[VP_PITCH] * 36.f) + drift * 0.06f;
        float freq = 65.406f * std::pow(2.f, semis / 12.f);
        const float detune = 1.0041f;   // +-7 cents
        float w = p[VP_WAVE];
        float osc = 0.5f * (oscA.process(std::min(freq * detune / sr, 0.45f), w)
                          + oscB.process(std::min(freq / detune / sr, 0.45f), w));
        subPhase += 0.5f * freq / sr;
        if (subPhase >= 1.f) subPhase -= 1.f;
        osc += 0.35f * std::sin(2.f * float(M_PI) * subPhase);

        // decay time constant 15 ms .. 1.2 s
        float decay = 0.015f * std::pow(80.f, p[VP_DECAY]);
        env *= std::exp(-1.f / (decay * sr));

        // cutoff 40 Hz .. 16 kHz, +2.5 octaves of envelope sweep; smoothed against zipper noise
        cut += (p[VP_CUTOFF] + drift * 0.03f - cut) * 0.002f;
        float lp, bp, hp;
        f1.process(osc, 40.f * std::pow(400.f, cut) * std::pow(2.f, 2.5f * env), p[VP_RES], sr, lp, bp, hp);

        // filter 2: cutoff 20 Hz..20 kHz, envelope adds up to +6 octaves
        float fc2 = 20.f * std::pow(1000.f, g.cutoff) * std::pow(2.f, 6.f * g.envAmt * adsr);
        float lp2, bp2, hp2;
        f2.process(lp, fc2, g.res, sr, lp2, bp2, hp2);
        float y = g.mode < 0.5f ? lp2 + (bp2 - lp2) * (g.mode * 2.f) : bp2 + (hp2 - bp2) * ((g.mode - 0.5f) * 2.f);

        float drive = 1.f + 6.f * p[VP_DRIVE];
        return std::tanh(y * drive) / std::tanh(drive) * env * adsr * p[VP_LEVEL];
    }
};

// The ping-pong delay (M8Delay) and reverb (M8Reverb) live in Echo_dsp.hpp, shared with MATRIX-ECHO.

} // namespace
