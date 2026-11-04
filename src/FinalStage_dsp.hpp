#pragma once
// The FINAL stage's master filter and its ADSR, as on MATRIX-DUO (copied from SynTHBloKDuo.cpp's DUFilter /
// DUAdsr so DUO stays untouched; DUO can include this header later and drop its own copies).
#include "plugin.hpp"
#include <cmath>

namespace {

// Master filter, GRAIN-FORGE's DJ filter in mono: CUT 0..1 sweeps 20 Hz .. 20 kHz, MODE 0..2 = LP / BP / HP,
// STAGES 0..2 = 1..3 SVFs in series (12 / 24 / 36 dB), the first one resonant (Q 0.8 .. 14, level drops
// as it rises). LP fully open / HP fully down fade to true bypass. Every mode and stage always runs and
// MODE / STAGES arrive smoothed, so switching crossfades.
struct FSFilter {
    float ic1[3][3] = {}, ic2[3][3] = {};   // [mode][stage]
    void process(float& x, float cut, float res, float sr, float stages, float mode) {
        cut = clamp(cut, 0.f, 1.f);
        float fc = 20.f * std::pow(1000.f, cut);
        float g = std::tan(float(M_PI) * clamp(fc, 20.f, sr * 0.45f) / sr);
        float kr = 1.2f * std::pow(0.07f / 1.2f, clamp(res, 0.f, 1.f));
        float comp = std::pow(kr / 1.2f, 0.4f);
        float wetM[3] = {clamp((0.99f - cut) / 0.06f, 0.f, 1.f), 1.f, clamp((cut - 0.01f) / 0.06f, 0.f, 1.f)};
        stages = clamp(stages, 0.f, 2.f); mode = clamp(mode, 0.f, 2.f);
        int s0 = std::min((int)stages, 1), m0 = std::min((int)mode, 1);
        float sf = stages - s0, mf = mode - m0, out[3];
        for (int m = 0; m < 3; m++) {
            float y = x, tap[3];
            for (int s = 0; s < 3; s++) {
                float k = s == 0 ? kr : 1.414f;
                float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
                float v3 = y - ic2[m][s];
                float v1 = a1 * ic1[m][s] + a2 * v3;
                float v2 = ic2[m][s] + a2 * ic1[m][s] + a3 * v3;
                ic1[m][s] = 2.f * v1 - ic1[m][s]; ic2[m][s] = 2.f * v2 - ic2[m][s];
                y = m == 0 ? v2 : m == 1 ? k * v1 : y - k * v1 - v2;
                tap[s] = y;
            }
            float yf = (tap[s0] + (tap[s0 + 1] - tap[s0]) * sf) * comp;
            float ay = std::fabs(yf);   // resonant peaks above 1 round off toward 1.75
            if (ay > 1.f) { float o = ay - 1.f; yf = std::copysign(1.f + 0.75f * o / (0.75f + o), yf); }
            out[m] = x + (yf - x) * wetM[m];
        }
        x = out[m0] + (out[m0 + 1] - out[m0]) * mf;
    }
};

// Filter ADSR. Times in seconds; exponential segments, so no clicks.
struct FSAdsr {
    enum { IDLE, ATK, DEC, REL } st = IDLE;
    float v = 0.f;
    void trigger() { st = ATK; }
    void release() { if (st != IDLE) st = REL; }
    float process(float atk, float dec, float sus, float rel, float sr) {
        switch (st) {
            case ATK:
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

} // namespace
