#pragma once
// Onboard aux FX bus for MIX24 (header-only, no Rack deps).
// Programs: 0 OFF, 1 ROOM, 2 HALL, 3 PLATE, 4 ECHO, 5 PING-PONG, 6 CHORUS, 7 FLANGER.
// process() takes the summed channel sends and returns the WET signal only.
#include <vector>
#include <cmath>
#include <algorithm>

struct MixFx {
    float sr = 0.f;
    int lastProg = -1;
    std::vector<float> dL, dR;
    int dW = 0;
    float dTime = 0.35f * 48000.f, lpL = 0.f, lpR = 0.f;
    float lfo = 0.f;
    struct Comb { std::vector<float> b; int i = 0; float f = 0.f; };
    struct AP { std::vector<float> b; int i = 0; };
    Comb cL[4], cR[4];
    AP aL[2], aR[2];
    std::vector<float> pre;
    int pW = 0;
    float preSm = 0.f;

    void clearAll() {
        std::fill(dL.begin(), dL.end(), 0.f); std::fill(dR.begin(), dR.end(), 0.f);
        std::fill(pre.begin(), pre.end(), 0.f);
        for (int j = 0; j < 4; j++) {
            std::fill(cL[j].b.begin(), cL[j].b.end(), 0.f); cL[j].f = 0.f;
            std::fill(cR[j].b.begin(), cR[j].b.end(), 0.f); cR[j].f = 0.f;
        }
        for (int j = 0; j < 2; j++) {
            std::fill(aL[j].b.begin(), aL[j].b.end(), 0.f);
            std::fill(aR[j].b.begin(), aR[j].b.end(), 0.f);
        }
        lpL = lpR = 0.f;
    }

    void setup(float sampleRate) {
        sr = sampleRate;
        int n = (int) (sr * 2.0f) + 4;
        dL.assign(n, 0.f); dR.assign(n, 0.f); dW = 0;
        static const int cl[4] = {1116, 1188, 1277, 1356};
        static const int al[2] = {556, 441};
        float k = sr / 44100.f;
        for (int j = 0; j < 4; j++) {
            cL[j].b.assign((int) (cl[j] * k) + 1, 0.f); cL[j].i = 0; cL[j].f = 0.f;
            cR[j].b.assign((int) ((cl[j] + 23) * k) + 1, 0.f); cR[j].i = 0; cR[j].f = 0.f;
        }
        for (int j = 0; j < 2; j++) {
            aL[j].b.assign((int) (al[j] * k) + 1, 0.f); aL[j].i = 0;
            aR[j].b.assign((int) ((al[j] + 23) * k) + 1, 0.f); aR[j].i = 0;
        }
        pre.assign((int) (sr * 0.12f) + 4, 0.f); pW = 0;
        lpL = lpR = 0.f;
        lastProg = -1;
    }

    static float rd(const std::vector<float>& b, int w, float delay) {
        int n = (int) b.size();
        float p = (float) w - delay;
        while (p < 0.f) p += n;
        int i0 = (int) p; float fr = p - i0;
        if (i0 >= n) i0 -= n;
        int i1 = i0 + 1; if (i1 >= n) i1 -= n;
        return b[i0] + (b[i1] - b[i0]) * fr;
    }

    void reverb(float in, float size, float damp, float preAmt, float& wl, float& wr) {
        float fb = 0.70f + 0.28f * size;
        float dp = 0.05f + 0.80f * damp;
        float preSamp = preAmt * 0.1f * sr;
        preSm += (preSamp - preSm) * 0.001f;
        pre[pW] = in;
        float inp = rd(pre, pW, preSm + 1.f) * 0.06f;
        if (++pW >= (int) pre.size()) pW = 0;
        wl = wr = 0.f;
        for (int j = 0; j < 4; j++) {
            Comb& c = cL[j]; float o = c.b[c.i];
            c.f = o * (1.f - dp) + c.f * dp; c.b[c.i] = inp + c.f * fb;
            if (++c.i >= (int) c.b.size()) c.i = 0; wl += o;
            Comb& d = cR[j]; o = d.b[d.i];
            d.f = o * (1.f - dp) + d.f * dp; d.b[d.i] = inp + d.f * fb;
            if (++d.i >= (int) d.b.size()) d.i = 0; wr += o;
        }
        for (int j = 0; j < 2; j++) {
            AP& p = aL[j]; float bo = p.b[p.i]; float o = -wl + bo; p.b[p.i] = wl + bo * 0.5f; wl = o;
            if (++p.i >= (int) p.b.size()) p.i = 0;
            AP& q = aR[j]; bo = q.b[q.i]; o = -wr + bo; q.b[q.i] = wr + bo * 0.5f; wr = o;
            if (++q.i >= (int) q.b.size()) q.i = 0;
        }
        wl *= 2.4f; wr *= 2.4f;
    }

    void process(float inL, float inR, float sampleRate, int prog, float p1, float p2, float p3,
                 float& wl, float& wr) {
        wl = wr = 0.f;
        if (sampleRate != sr || dL.empty()) setup(sampleRate);
        if (prog != lastProg) { lastProg = prog; clearAll(); }
        if (prog <= 0) return;
        float mono = 0.5f * (inL + inR);
        if (prog <= 3) {
            float size, damp, pr;
            if (prog == 1)      { size = 0.05f + 0.30f * p1; damp = p2;        pr = 0.5f * p3; }   // ROOM
            else if (prog == 2) { size = 0.55f + 0.45f * p1; damp = p2;        pr = p3; }          // HALL
            else                { size = 0.30f + 0.60f * p1; damp = 1.f - p2;  pr = 0.25f * p3; }  // PLATE (p2 = tone)
            reverb(mono, size, damp, pr, wl, wr);
            return;
        }
        if (prog <= 5) {                                   // ECHO / PING-PONG: p1 time, p2 feedback, p3 tone
            float target = (0.02f + 1.18f * p1 * p1) * sr;
            dTime += (target - dTime) * 0.0008f;
            float tL = rd(dL, dW, dTime), tR = rd(dR, dW, dTime);
            float a = 0.05f + 0.9f * p3 * p3;
            lpL += (tL - lpL) * a; lpR += (tR - lpR) * a;
            float fb = 0.95f * p2;
            if (prog == 5) { dL[dW] = mono + lpR * fb; dR[dW] = lpL * fb; }   // ping-pong
            else           { dL[dW] = inL + lpL * fb;  dR[dW] = inR + lpR * fb; }
            if (++dW >= (int) dL.size()) dW = 0;
            wl = tL; wr = tR;
            return;
        }
        // CHORUS (p1 rate, p2 depth, p3 spread) / FLANGER (p1 rate, p2 depth, p3 feedback)
        float rate = 0.05f + 5.f * p1 * p1;
        lfo += rate / sr; if (lfo >= 1.f) lfo -= 1.f;
        const float TP = 6.2831853f;
        if (prog == 6) {
            float d1 = (0.012f + 0.006f * p2 * std::sin(TP * lfo)) * sr;
            float d2 = (0.012f + 0.006f * p2 * std::sin(TP * (lfo + 0.25f + 0.25f * p3))) * sr;
            wl = rd(dL, dW, d1); wr = rd(dR, dW, d2);
            dL[dW] = inL; dR[dW] = inR;
        } else {
            float s1 = 0.5f + 0.5f * std::sin(TP * lfo), s2 = 0.5f + 0.5f * std::sin(TP * (lfo + 0.08f));
            float d1 = (0.0006f + 0.0044f * (p2 * s1 + (1.f - p2) * 0.3f)) * sr;
            float d2 = (0.0006f + 0.0044f * (p2 * s2 + (1.f - p2) * 0.3f)) * sr;
            wl = rd(dL, dW, d1); wr = rd(dR, dW, d2);
            float fb = 0.85f * p3;
            dL[dW] = inL + wl * fb; dR[dW] = inR + wr * fb;
        }
        if (++dW >= (int) dL.size()) dW = 0;
    }
};
