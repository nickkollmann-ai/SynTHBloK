#pragma once
// Dual-oscillator mono synth voice for MATRIX FORGE's LEAD and BASS tracks.
// OSC 1 and OSC 2 morph saw -> square -> sine (the MATRIX-8 wave), OSC 2 is tuned against OSC 1, can be
// hard-synced to it and phase-modulated by it (FM). A square sub an octave under OSC 1, a 24 dB
// resonant low-pass swept by the ADSR, drive, and the same ADSR on the level. Notes glide (GLIDE).
#include "Matrix8_dsp.hpp"

namespace {

enum DualOscParam { DO_WAVE1, DO_WAVE2, DO_TUNE, DO_FM, DO_MIX, DO_SUB, DO_DRIVE, DO_LEVEL,
                    DO_CUTOFF, DO_RES, DO_ENV, DO_GLIDE, DO_ATK, DO_DEC, DO_SUS, DO_REL,
                    DO_GATE, DO_OCT, DO_SYNC, DO_NUM };

struct DualOscVoice {
    float p1 = 0.f, p2 = 0.f, ps = 0.f;
    float freq = 0.f, target = 110.f;     // Hz: now (gliding) and the note's
    float adsr = 0.f, gateLeft = 0.f, cut = 0.5f;
    int stage = 0;                        // 0 idle, 1 attack, 2 decay/sustain, 3 release
    M8Svf f1, f2;

    // A step's note. legato = tie from the last note: glide there without retriggering.
    void noteOn(float hz, float gate, bool legato) {
        target = hz;
        if (freq <= 0.f) freq = hz;
        gateLeft = gate;
        if (!legato || stage == 0 || stage == 3) stage = 1;
    }
    bool gateOn() const { return stage == 1 || stage == 2; }

    // p = the DO_* values: knobs 0..1, except TUNE (semitones) and OCT / SYNC (whole numbers).
    float process(float sr, const float* p) {
        auto coef = [&](float t) { return 1.f - std::exp(-1.f / (t * sr)); };
        // ADSR: attack 1 ms..2 s, decay 5 ms..2 s, release 5 ms..4 s (same as MATRIX-8's)
        gateLeft -= 1.f / sr;
        if (gateLeft <= 0.f && gateOn()) stage = 3;
        if (stage == 1) {
            adsr += (1.2f - adsr) * coef(0.001f * std::pow(2000.f, p[DO_ATK]));
            if (adsr >= 1.f) { adsr = 1.f; stage = 2; }
        } else if (stage == 2) {
            adsr += (p[DO_SUS] - adsr) * coef(0.005f * std::pow(400.f, p[DO_DEC]));
        } else if (stage == 3) {
            adsr += (0.f - adsr) * coef(0.005f * std::pow(800.f, p[DO_REL]));
            if (adsr < 1e-4f) { adsr = 0.f; stage = 0; }
        }
        if (stage == 0 && adsr == 0.f) return 0.f;

        // glide: 0..0.5 s (squared knob), in pitch
        float g = p[DO_GLIDE] * p[DO_GLIDE] * 0.5f;
        freq = g < 0.001f ? target : freq * std::pow(target / freq, coef(g));

        float dt1 = std::min(freq / sr, 0.45f);
        float dt2 = std::min(dt1 * std::pow(2.f, p[DO_TUNE] / 12.f), 0.45f);
        p1 += dt1;
        bool wrap = p1 >= 1.f;
        if (wrap) p1 -= 1.f;
        p2 += dt2;
        if (p[DO_SYNC] > 0.5f && wrap) p2 = p1 * dt2 / dt1;   // hard sync: OSC 2 restarts with OSC 1
        p2 -= std::floor(p2);
        float o1 = morphWave(p1, dt1, p[DO_WAVE1]);
        float q = p2 + p[DO_FM] * p[DO_FM] * 0.6f * o1;       // FM: OSC 1 bends OSC 2's phase
        q -= std::floor(q);
        float o2 = morphWave(q, dt2, p[DO_WAVE2]);
        ps += 0.5f * dt1;
        if (ps >= 1.f) ps -= 1.f;
        float sub = (ps < 0.5f ? 1.f : -1.f) + polyBlep(ps, 0.5f * dt1) - polyBlep(ps + (ps < 0.5f ? 0.5f : -0.5f), 0.5f * dt1);
        float osc = o1 + (o2 - o1) * p[DO_MIX] + 0.6f * p[DO_SUB] * sub;

        // filter: cutoff 30 Hz .. 18 kHz, ADSR adds up to +6 octaves; two stages = 24 dB
        cut += (p[DO_CUTOFF] - cut) * 0.002f;
        float fc = 30.f * std::pow(600.f, cut) * std::pow(2.f, 6.f * p[DO_ENV] * adsr);
        float lp, bp, hp, lp2;
        f1.process(osc, fc, p[DO_RES], sr, lp, bp, hp);
        f2.process(lp, fc, 0.f, sr, lp2, bp, hp);
        float drive = 1.f + 5.f * p[DO_DRIVE];
        return 0.8f * std::tanh(lp2 * drive) / std::tanh(drive) * adsr * p[DO_LEVEL];
    }
};

} // namespace
