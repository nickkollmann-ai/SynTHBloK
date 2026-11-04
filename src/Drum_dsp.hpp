#pragma once
// SynTHBloK MATRIX-DRUM's drum-synth voice: one voice, 8 MODEL engines, steered by 8 macros (the strip
// banks A..H). KICK / TOM follow SynTHBloK-K, SNARE / HAT follow SynTHBloK-D (SnareHat); CLAP, METAL,
// FM and NOISE are new. Output is about +-1; the module applies LEVEL and PAN (smoothed).
#include "plugin.hpp"
#include "Matrix8_dsp.hpp"   // M8Svf
#include <cmath>

namespace {

enum DrumModel { DM_KICK, DM_SNARE, DM_CLAP, DM_TOM, DM_HAT, DM_METAL, DM_FM, DM_NOISE, DM_NUM };
static const char* kDrumModelName[DM_NUM] = {"KICK", "SNARE", "CLAP", "TOM", "HAT", "METAL", "FM", "NOISE"};
enum DrumMacro { DK_TUNE, DK_DECAY, DK_TONE, DK_SNAP, DK_MOD, DK_DRIVE, DK_LEVEL, DK_PAN, DK_NUM };
static const char* kDrBankName[DK_NUM] = {"TUNE", "DECAY", "TONE", "SNAP", "MOD", "DRIVE", "LEVEL", "PAN"};
// What MOD does per engine (tooltips, the voice OLED).
static const char* kDrumModName[DM_NUM] = {"sweep", "pitch drop", "spread", "sweep", "ring", "partials", "FM index", "sweep"};

struct DrumVoice {
    bool active = false, choked = false;
    float vel = 1.f, t = 0.f;           // velocity, seconds since the hit
    float ampE = 0.f, pEnv = 0.f, clickE = 0.f, atk = 1.f, modE = 0.f, tailE = 0.f, chokeG = 1.f;
    float ph[6] = {}, ph2 = 0.f;
    float lp1 = 0.f, lp2 = 0.f, nLp = 0.f, nHp = 0.f, hp1 = 0.f, hp2 = 0.f;
    int burst = 0;
    M8Svf svf;
    uint32_t rng;
    explicit DrumVoice(uint32_t seed = 1) : rng(seed * 2654435761u + 7u) {}
    float noise() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng >> 8) * (2.f / 16777216.f) - 1.f; }

    void trigger(float v, int model) {
        vel = v; active = true; choked = false; chokeG = 1.f;
        t = 0.f; ampE = pEnv = clickE = modE = 1.f; tailE = 0.f; atk = 0.f; burst = 0;
        if (model == DM_KICK || model == DM_TOM) ph[0] = 0.25f;   // as SynTHBloK-K: starts on the peak, 0.5 ms ramp
        else if (model == DM_SNARE || model == DM_FM) { ph[0] = ph[1] = 0.f; ph2 = 0.f; }
    }
    // Choke group: another voice in the group fades this one out over a few ms.
    void choke() { if (active) choked = true; }

    float process(float sr, int model, const float* p) {
        if (!active) return 0.f;
        const float dt = 1.f / sr, TWO_PI = 6.28318530718f, nyq = 0.45f * sr;
        auto onePole = [&](float fc) { return 1.f - std::exp(-TWO_PI * std::min(fc, nyq) * dt); };
        auto decay = [&](float& e, float sec) { e *= std::exp(-6.9078f * dt / sec); };   // -60 dB in sec
        t += dt;
        float x = 0.f, env = 0.f;
        switch (model) {
            case DM_KICK:
            case DM_TOM: {
                // sine with a pitch sweep and a click (SynTHBloK-K); MOD = sweep depth, SNAP = click
                bool tom = model == DM_TOM;
                float tune = tom ? 60.f * std::pow(6.67f, p[DK_TUNE]) : 30.f + 90.f * p[DK_TUNE];
                float decS = tom ? 0.05f * std::pow(16.f, p[DK_DECAY]) : 0.06f * std::pow(20.f, p[DK_DECAY]);
                pEnv *= std::exp(-dt / (tom ? 0.03f : 0.012f));
                decay(ampE, decS);
                atk = std::min(1.f, atk + dt / 0.0005f);
                float f = std::min(tune * std::exp2(p[DK_MOD] * (tom ? 2.f : 5.f) * pEnv), nyq);
                ph[0] += f * dt; ph[0] -= std::floor(ph[0]);
                clickE *= std::exp(-dt / (tom ? 0.003f : 0.0012f));
                float n = noise();
                nLp += (n - nLp) * 0.25f;
                x = std::sin(TWO_PI * ph[0]) * ampE * atk + (n - nLp) * clickE * p[DK_SNAP] * 0.9f;
                env = ampE;
            } break;
            case DM_SNARE: {
                // two sines with a pitch drop and a band of noise (SynTHBloK-D); TONE = noise colour
                float decS = 0.06f * std::pow(8.33f, p[DK_DECAY]);
                float tune = 120.f + 280.f * p[DK_TUNE], drop = 2.f * p[DK_MOD];
                pEnv *= std::exp(-dt / 0.012f);
                float f1 = std::min(tune * (1.f + 0.8f * drop * pEnv), nyq);
                float f2 = std::min(tune * 1.62f * (1.f + 0.5f * drop * pEnv), nyq);
                ph[0] += f1 * dt; ph[0] -= std::floor(ph[0]);
                ph[1] += f2 * dt; ph[1] -= std::floor(ph[1]);
                decay(ampE, decS * 0.55f);
                decay(modE, decS);
                float body = (std::sin(TWO_PI * ph[0]) * 0.7f + std::sin(TWO_PI * ph[1]) * 0.3f) * ampE;
                float k = p[DK_TONE], n = noise();
                nLp += (n - nLp) * onePole(400.f + 2200.f * k);
                nHp += ((n - nLp) - nHp) * onePole(3500.f + 9000.f * k);
                x = body * (1.f - 0.35f * p[DK_SNAP]) + nHp * modE * p[DK_SNAP] * 1.6f;
                env = std::max(ampE, modE);
            } break;
            case DM_CLAP: {
                // four noise bursts MOD apart, then a tail; all band-passed at TONE. SNAP = bursts vs tail
                float sp = 0.006f + 0.012f * p[DK_MOD];
                if (burst < 3 && t >= (burst + 1) * sp) { burst++; clickE = 1.f; if (burst == 3) tailE = 1.f; }
                clickE *= std::exp(-dt / 0.0035f);
                decay(tailE, 0.08f * std::pow(8.f, p[DK_DECAY]));
                env = clickE * (0.5f + 0.5f * p[DK_SNAP]) + tailE * 0.8f;
                float lp, bp, hp;
                svf.process(noise(), 700.f * std::pow(5.7f, p[DK_TONE]), 0.45f, sr, lp, bp, hp);
                x = bp * 2.2f * env;
                if (burst < 3) env = 1.f;   // not done until the tail has started
            } break;
            case DM_HAT: {
                // six squares at 808 ratios, high-passed twice (SynTHBloK-D); DECAY = closed -> open,
                // SNAP = noise in the metal, MOD = ring (spreads the partials)
                static const float R[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
                float ht = 0.6f + 1.6f * p[DK_TUNE], s = (p[DK_MOD] - 0.5f) * 0.3f, sq = 0.f;
                for (int i = 0; i < 6; i++) {
                    float fi = std::min(R[i] * ht * (1.f + s * (i - 2.5f) / 2.5f), nyq);
                    ph[i] += fi * dt; ph[i] -= std::floor(ph[i]);
                    sq += ph[i] < 0.5f ? 1.f : -1.f;
                }
                sq = sq * (1.f / 6.f) * (1.f - 0.5f * p[DK_SNAP]) + noise() * 0.6f * p[DK_SNAP];
                decay(ampE, 0.01f * std::pow(120.f, p[DK_DECAY]));
                float a = onePole(3000.f + 7000.f * p[DK_TONE]);
                hp1 += (sq - hp1) * a;
                float h1 = sq - hp1;
                hp2 += (h1 - hp2) * a;
                x = (h1 - hp2) * ampE * 1.6f;
                env = ampE;
            } break;
            case DM_METAL: {
                // 2 (cowbell, 540 / 800 Hz) .. 6 (cymbal) squares as MOD rises, band-passed at TONE,
                // a fast drop into a longer ring; SNAP = stick click
                static const float R[6] = {1.f, 1.4815f, 0.9680f, 0.6844f, 0.5637f, 0.3802f};
                float base = 540.f * std::pow(2.f, (p[DK_TUNE] - 0.5f) * 2.f);
                int n = 2 + (int)std::round(p[DK_MOD] * 4.f);
                float sq = 0.f;
                for (int i = 0; i < n; i++) {
                    ph[i] += std::min(base * R[i], nyq) * dt; ph[i] -= std::floor(ph[i]);
                    sq += ph[i] < 0.5f ? 1.f : -1.f;
                }
                sq /= (float)n;
                float decS = 0.03f * std::pow(40.f, p[DK_DECAY]);
                decay(ampE, decS * 0.25f);
                decay(modE, decS);
                clickE *= std::exp(-dt / 0.001f);
                env = 0.55f * ampE + 0.45f * modE;
                float lp, bp, hp;
                svf.process(sq, base * (1.2f + 3.f * p[DK_TONE]), 0.5f, sr, lp, bp, hp);
                x = bp * env * 2.f + noise() * clickE * p[DK_SNAP] * 0.5f;
            } break;
            case DM_FM: {
                // two-operator FM: TONE = ratio, MOD = index (it decays twice as fast), SNAP = pitch blip
                float fc = 50.f * std::pow(40.f, p[DK_TUNE]), ratio = 0.5f + 3.5f * p[DK_TONE];
                pEnv *= std::exp(-dt / 0.015f);
                float f = std::min(fc * (1.f + 3.f * p[DK_SNAP] * pEnv), nyq);
                float decS = 0.02f * std::pow(50.f, p[DK_DECAY]);
                decay(ampE, decS);
                decay(modE, decS * 0.5f);
                ph2 += std::min(f * ratio, nyq) * dt; ph2 -= std::floor(ph2);
                ph[0] += f * dt; ph[0] -= std::floor(ph[0]);
                float m = std::sin(TWO_PI * ph2) * p[DK_MOD] * 8.f * modE;
                x = std::sin(TWO_PI * ph[0] + m) * ampE;
                env = ampE;
            } break;
            default: {   // DM_NOISE
                // noise through a resonant filter at TUNE, swept up by MOD; TONE = LP -> BP -> HP, SNAP = resonance
                float decS = 0.01f * std::pow(150.f, p[DK_DECAY]);
                decay(ampE, decS);
                decay(pEnv, decS * 0.5f);
                float fc = std::min(100.f * std::pow(120.f, p[DK_TUNE]) * std::exp2(p[DK_MOD] * 6.f * pEnv), nyq);
                float lp, bp, hp, k = p[DK_TONE];
                svf.process(noise(), fc, p[DK_SNAP] * 0.95f, sr, lp, bp, hp);
                float y = k < 0.5f ? lp + (bp - lp) * (k * 2.f) : bp + (hp - bp) * ((k - 0.5f) * 2.f);
                x = y * ampE;
                env = ampE;
            } break;
        }
        // velocity, then drive (0..24 dB, as SynTHBloK-D)
        float g = std::pow(10.f, p[DK_DRIVE] * 1.2f);
        x = std::tanh(g * x * vel) / std::tanh(g);
        if (model == DM_KICK || model == DM_TOM) {   // tone: two one-pole low-passes, 300 Hz .. 20 kHz
            float a = onePole(300.f * std::pow(66.f, p[DK_TONE]));
            lp1 += (x - lp1) * a;
            lp2 += (lp1 - lp2) * a;
            x = lp2;
        }
        if (choked) {
            chokeG *= std::exp(-dt / 0.004f);
            if (chokeG < 1e-3f) active = false;
            x *= chokeG;
        }
        if (t > 0.02f && env < 1e-4f) active = false;
        return x;
    }
};

} // namespace
