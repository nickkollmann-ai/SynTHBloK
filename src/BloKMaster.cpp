// ============================================================================
// BLOKMASTER - mastering strip (rebuilt)
//
//  IN -> trim -> low / high cut -> tone EQ (low / tilt / high / air) -> M-S gain
//     -> glue compressor (soft knee, sidechain HPF, external SC, auto makeup)
//     -> 3-band leveler (MULTI) -> saturator (2x oversampled) -> punch
//     -> width + mono bass -> look-ahead brick-wall limiter -> output gain
//     -> mono check / side solo -> OUT
//  Meters: spectrum, loudness (LUFS M / S / I), vectorscope, correlation,
//          input / output peaks, GR for comp, leveler and limiter,
//          scrolling limiter tracker (level history + GR curve).
// ============================================================================

#include "plugin.hpp"
#include "BloKMaster_layout.hpp"
#include "OledUi.hpp"
#include "ModulePictures.hpp"
#include <cmath>
#include <algorithm>
#include <atomic>

namespace {

const float FS_V = 5.f;     // 5 V = 0 dBFS

inline float db2lin(float db) { return std::pow(10.f, db / 20.f); }
inline float lin2db(float x) { return 20.f * std::log10(std::max(x, 1e-9f)); }
inline float fclamp(float x, float a, float b) { return std::min(b, std::max(a, x)); }
inline float onePole(float ms, float sr) { return 1.f - std::exp(-1.f / (std::max(ms, 0.01f) * 0.001f * sr)); }

// ---------------------------------------------------------------------------
// RBJ biquad (transposed direct form II)
// ---------------------------------------------------------------------------
struct Biquad {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;

    inline float process(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.f; }
    void copyCoefs(const Biquad& o) { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }
    void raw(float B0, float B1, float B2, float A0, float A1, float A2) {
        float i = 1.f / A0;
        b0 = B0 * i; b1 = B1 * i; b2 = B2 * i; a1 = A1 * i; a2 = A2 * i;
    }
    static float fc(float f, float sr) { return fclamp(f, 5.f, 0.45f * sr); }

    void lowpass(float f, float q, float sr) {
        float w = 2.f * (float) M_PI * fc(f, sr) / sr, cw = std::cos(w), al = std::sin(w) / (2.f * q);
        raw((1.f - cw) * 0.5f, 1.f - cw, (1.f - cw) * 0.5f, 1.f + al, -2.f * cw, 1.f - al);
    }
    void highpass(float f, float q, float sr) {
        float w = 2.f * (float) M_PI * fc(f, sr) / sr, cw = std::cos(w), al = std::sin(w) / (2.f * q);
        raw((1.f + cw) * 0.5f, -(1.f + cw), (1.f + cw) * 0.5f, 1.f + al, -2.f * cw, 1.f - al);
    }
    void bandpass(float f, float q, float sr) {
        float w = 2.f * (float) M_PI * fc(f, sr) / sr, cw = std::cos(w), al = std::sin(w) / (2.f * q);
        raw(al, 0.f, -al, 1.f + al, -2.f * cw, 1.f - al);
    }
    void lowShelf(float f, float gdb, float sr) {
        float A = std::pow(10.f, gdb / 40.f), w = 2.f * (float) M_PI * fc(f, sr) / sr, cw = std::cos(w), sw = std::sin(w);
        float al = sw * 0.5f * std::sqrt(2.f), tsa = 2.f * std::sqrt(A) * al;
        raw(A * ((A + 1.f) - (A - 1.f) * cw + tsa), 2.f * A * ((A - 1.f) - (A + 1.f) * cw), A * ((A + 1.f) - (A - 1.f) * cw - tsa),
            (A + 1.f) + (A - 1.f) * cw + tsa, -2.f * ((A - 1.f) + (A + 1.f) * cw), (A + 1.f) + (A - 1.f) * cw - tsa);
    }
    void highShelf(float f, float gdb, float sr) {
        float A = std::pow(10.f, gdb / 40.f), w = 2.f * (float) M_PI * fc(f, sr) / sr, cw = std::cos(w), sw = std::sin(w);
        float al = sw * 0.5f * std::sqrt(2.f), tsa = 2.f * std::sqrt(A) * al;
        raw(A * ((A + 1.f) + (A - 1.f) * cw + tsa), -2.f * A * ((A - 1.f) + (A + 1.f) * cw), A * ((A + 1.f) + (A - 1.f) * cw - tsa),
            (A + 1.f) - (A - 1.f) * cw + tsa, 2.f * ((A - 1.f) - (A + 1.f) * cw), (A + 1.f) - (A - 1.f) * cw - tsa);
    }
    void peak(float f, float q, float gdb, float sr) {
        float A = std::pow(10.f, gdb / 40.f), w = 2.f * (float) M_PI * fc(f, sr) / sr, cw = std::cos(w), al = std::sin(w) / (2.f * q);
        raw(1.f + al * A, -2.f * cw, 1.f - al * A, 1.f + al / A, -2.f * cw, 1.f - al / A);
    }
};

// two channels sharing one set of coefficients
struct BiquadPair {
    Biquad c[2];
    void sync() { c[1].copyCoefs(c[0]); }
    void reset() { c[0].reset(); c[1].reset(); }
};

// Linkwitz-Riley 4th order = two cascaded Butterworth stages
struct LR4 {
    Biquad s1, s2;
    inline float process(float x) { return s2.process(s1.process(x)); }
    void setLP(float f, float sr) { s1.lowpass(f, 0.70710678f, sr); s2.copyCoefs(s1); }
    void setHP(float f, float sr) { s1.highpass(f, 0.70710678f, sr); s2.copyCoefs(s1); }
    void reset() { s1.reset(); s2.reset(); }
};

const int NSPEC = 24;
const int NSCOPE = 256;
const int LA_MAX = 128;
const int NTRACK = 512;     // limiter tracker history (power of two), one frame per 10 ms
const int NVIEW = 400;      // frames on screen = 4 s

// one block of the dynamics tracker history (dB / LUFS)
struct TrackFrame {
    float inDb = -90.f, outDb = -90.f, limGr = 0.f;          // limiter in / out peak, GR
    float cInDb = -90.f, cOutDb = -90.f, compGr = 0.f;       // compressor detector / output peak, GR
    float mbLo = 0.f, mbMid = 0.f, mbHi = 0.f;               // leveler band gains (signed)
    float lufsM = -70.f, lufsS = -70.f;
};
enum TrackView { VIEW_LIM, VIEW_COMP, VIEW_MULTI, VIEW_LOUD, NUM_VIEWS };

} // namespace

// ============================================================================
// Module
// ============================================================================
struct BlokMaster : Module {

    enum ParamIds {
        TRIM_PARAM, TILT_PARAM, LOW_PARAM, HIGH_PARAM,
        COMP_THRESH_PARAM, COMP_RATIO_PARAM, COMP_ATTACK_PARAM, COMP_RELEASE_PARAM, COMP_MIX_PARAM,
        PUNCH_PARAM,
        LIMIT_THRESH_PARAM, LIMIT_RELEASE_PARAM, LIMIT_CEILING_PARAM,
        WIDTH_PARAM, MONO_BASS_PARAM, AIR_PARAM,
        OUT_GAIN_PARAM,
        // added in the rebuild (old patches keep the ids above)
        LOWCUT_PARAM, COMP_KNEE_PARAM, COMP_SCHPF_PARAM,
        DRIVE_PARAM, SATMIX_PARAM, MB_PARAM, MID_PARAM, SIDE_PARAM,
        BYPASS_PARAM, AUTO_PARAM, MONO_PARAM, SIDESOLO_PARAM,
        VIEW_LIM_PARAM, VIEW_COMP_PARAM, VIEW_MULTI_PARAM, VIEW_LOUD_PARAM,
        EQ_MID_PARAM, MIDCUT_PARAM,
        // leveler bands (the knobs under the DYNAMICS display)
        MB_LOW_PARAM, MB_MIDLO_PARAM, MB_MIDHI_PARAM, MB_HIGH_PARAM, MB_SPEED_PARAM, MB_RANGE_PARAM,
        HICUT_PARAM,
        NUM_PARAMS
    };
    enum InputIds { IN_L_INPUT, IN_R_INPUT, SIDECHAIN_INPUT, THR_CV_INPUT, DRIVE_CV_INPUT, WIDTH_CV_INPUT, NUM_INPUTS };
    enum OutputIds { OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };
    enum LightIds {
        COMP_GR_LIGHT, LIMIT_GR_LIGHT, CORRELATION_LIGHT,
        BYPASS_LIGHT, AUTO_LIGHT, MONO_LIGHT, SIDESOLO_LIGHT,
        VIEW_LIM_LIGHT, VIEW_COMP_LIGHT, VIEW_MULTI_LIGHT, VIEW_LOUD_LIGHT,
        MIDCUT_LIGHT,
        NUM_LIGHTS
    };

    // ---- values shared with the displays (written by the audio thread) ----
    struct DisplayState {
        float specEnv[NSPEC] = {};
        float inPeak = 0.f, outPeak = 0.f;           // linear volts, decaying
        float compGR = 0.f, mbGR = 0.f, limitGR = 0.f; // dB, positive = reduction
        float corr = 0.f;
        float lufsM = -70.f, lufsS = -70.f, lufsI = -70.f;
        float peakHoldDb = -90.f;
        float scopeL[NSCOPE] = {}, scopeR[NSCOPE] = {};
        int scopeIdx = 0;
    } disp;
    float specFreq[NSPEC];

    // ---- limiter tracker: lock-free ring, written by process(), read by the display ----
    TrackFrame track[NTRACK];
    std::atomic<uint32_t> trackPos{0};      // frames written so far (frame i lives at i % NTRACK)
    int trkCount = 0, trkLen = 480;
    float trkIn = 0.f, trkOut = 0.f, trkLimG = 1.f, trkCompG = 0.f, trkCIn = 0.f, trkCOut = 0.f;
    float compThrDb = -12.f;                 // effective threshold incl. CV, for the COMP view
    int view = VIEW_LIM;                     // tracker view, saved with the patch
    dsp::BooleanTrigger viewTrig[NUM_VIEWS];
    float viewOn[NUM_VIEWS] = {1.f};         // 1 = the shown view, lights its cell in the DYNAMICS display

    // ---- DSP state ----
    float sr = 48000.f;
    int ctrlCount = 0;

    float trimLin = 1.f, outLin = 1.f, byp = 0.f;
    BiquadPair lowCut, hiCut, lowSh, highSh, tiltLo, tiltHi, airSh, midPk, midCut;
    float midCutSm = 0.f;                    // smoothed MID CUT amount, no zipper on the knob

    // compressor
    BiquadPair scHp;
    float compG = 0.f;                       // smoothed gain reduction (dB, >= 0)
    float cThr = -12.f, cRatio = 2.f, cKnee = 6.f, cAtk = 0.01f, cRel = 0.001f, cMix = 1.f;

    // 3-band leveler
    // three bands with their own edges (LOW, MID LO .. MID HI, HIGH), so they can overlap or leave gaps
    LR4 bLo[2], bMidHp[2], bMidLp[2], bHi[2];
    float mbFast[3] = {}, mbSlow[3] = {}, mbGain[3] = {};
    float mbMixSm = 0.f;
    float bandF[4] = {};                     // the edges the band filters are set to (Hz)
    // MB_LOW .. MB_HIGH knob (0..1) -> Hz, 30 Hz .. 16 kHz on a log scale (the TONE display's range)
    static float edgeHz(float v) { return 30.f * std::pow(16000.f / 30.f, v); }

    // saturator (2x oversampled)
    Biquad osLp1[2], osLp2[2];
    float osPrev[2] = {}, dcX[2] = {}, dcY[2] = {};

    // punch
    float punchFast = 0.f, punchSlow = 0.f;

    // stereo
    Biquad monoHp;
    float widthSm = 1.f;

    // limiter
    int laN = 72;
    float limDl[2][LA_MAX];
    float limGreq[2 * LA_MAX];
    float limGmin[LA_MAX];
    int limW = 0, limG = 0, limM = 0;
    float limRel = 1.f;

    // meters
    Biquad specBp[NSPEC];
    float specSm[NSPEC] = {};
    BiquadPair kShelf, kHp;
    float msM = 0.f, msS = 0.f;
    double intSum = 0.0, intCount = 0.0;
    float corrLR = 0.f, corrLL = 0.f, corrRR = 0.f;
    float peakHold = 0.f; int peakHoldTimer = 0;
    int scopeDiv = 0;

    BlokMaster() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configParam(TRIM_PARAM, -12.f, 12.f, 0.f, "Input trim", " dB");
        configParam(TILT_PARAM, -1.f, 1.f, 0.f, "Tilt (dark to bright)");
        configParam(LOW_PARAM, -12.f, 12.f, 0.f, "Low shelf (120 Hz)", " dB");
        configParam(HIGH_PARAM, -12.f, 12.f, 0.f, "High shelf (6 kHz)", " dB");
        configParam(EQ_MID_PARAM, -12.f, 12.f, 0.f, "Mid bell (1 kHz)", " dB");
        // a knob now (was an on/off button: old patches load as off / full)
        configParam(MIDCUT_PARAM, 0.f, 1.f, 0.f, "Mid cut (scoop the mids, full = -24 dB at 1 kHz)", "%", 0.f, 100.f);
        configParam(HICUT_PARAM, 2000.f, 20000.f, 20000.f, "High cut (20 kHz = off)", " Hz");

        configParam(COMP_THRESH_PARAM, -40.f, 0.f, -12.f, "Compressor threshold", " dB");
        configParam(COMP_RATIO_PARAM, 1.f, 20.f, 2.f, "Compressor ratio", ":1");
        configParam(COMP_ATTACK_PARAM, 0.1f, 100.f, 10.f, "Compressor attack", " ms");
        configParam(COMP_RELEASE_PARAM, 20.f, 1000.f, 120.f, "Compressor release", " ms");
        configParam(COMP_MIX_PARAM, 0.f, 1.f, 1.f, "Compressor mix (parallel)", "%", 0.f, 100.f);

        configParam(PUNCH_PARAM, 0.f, 1.f, 0.f, "Punch (transient emphasis)", "%", 0.f, 100.f);

        configParam(LIMIT_THRESH_PARAM, -12.f, 0.f, -1.f, "Limiter threshold (push into the ceiling)", " dB");
        configParam(LIMIT_RELEASE_PARAM, 10.f, 200.f, 50.f, "Limiter release", " ms");
        configParam(LIMIT_CEILING_PARAM, -6.f, 0.f, -0.1f, "Limiter ceiling", " dB");

        configParam(WIDTH_PARAM, 0.f, 2.f, 1.f, "Stereo width", "%", 0.f, 100.f);
        configParam(MONO_BASS_PARAM, 20.f, 300.f, 20.f, "Mono bass below (20 = off)", " Hz");
        configParam(AIR_PARAM, 0.f, 1.f, 0.f, "Air (12 kHz shelf)", "%", 0.f, 100.f);

        configParam(OUT_GAIN_PARAM, -12.f, 12.f, 0.f, "Output gain", " dB");

        configParam(LOWCUT_PARAM, 20.f, 300.f, 20.f, "Low cut (20 = off)", " Hz");
        configParam(COMP_KNEE_PARAM, 0.f, 12.f, 6.f, "Compressor knee", " dB");
        configParam(COMP_SCHPF_PARAM, 20.f, 400.f, 20.f, "Compressor sidechain high-pass (20 = off)", " Hz");
        configParam(DRIVE_PARAM, 0.f, 24.f, 0.f, "Saturation drive", " dB");
        configParam(SATMIX_PARAM, 0.f, 1.f, 1.f, "Saturation mix", "%", 0.f, 100.f);
        configParam(MB_PARAM, 0.f, 1.f, 0.f, "Multiband leveler amount", "%", 0.f, 100.f);
        configParam(MID_PARAM, -6.f, 6.f, 0.f, "Mid gain", " dB");
        configParam(SIDE_PARAM, -6.f, 6.f, 0.f, "Side gain", " dB");
        // band edges, defaults where the old fixed crossovers were (180 Hz, 3.2 kHz)
        const float v180 = std::log(180.f / 30.f) / std::log(16000.f / 30.f), v3k2 = std::log(3200.f / 30.f) / std::log(16000.f / 30.f);
        configParam(MB_LOW_PARAM, 0.f, 1.f, v180, "Leveler low band: up to", " Hz", 16000.f / 30.f, 30.f);
        configParam(MB_MIDLO_PARAM, 0.f, 1.f, v180, "Leveler mid band: from", " Hz", 16000.f / 30.f, 30.f);
        configParam(MB_MIDHI_PARAM, 0.f, 1.f, v3k2, "Leveler mid band: up to", " Hz", 16000.f / 30.f, 30.f);
        configParam(MB_HIGH_PARAM, 0.f, 1.f, v3k2, "Leveler high band: from", " Hz", 16000.f / 30.f, 30.f);
        configParam(MB_SPEED_PARAM, 0.f, 1.f, 0.5f, "Leveler speed (slow -> fast)", "%", 0.f, 100.f);
        configParam(MB_RANGE_PARAM, 1.f, 18.f, 9.f, "Leveler range (most cut; boost is 5/9 of it)", " dB");

        configSwitch(BYPASS_PARAM, 0.f, 1.f, 0.f, "Bypass", {"Active", "Bypassed"});
        configSwitch(AUTO_PARAM, 0.f, 1.f, 0.f, "Compressor auto makeup gain", {"Off", "On"});
        configSwitch(MONO_PARAM, 0.f, 1.f, 0.f, "Mono check", {"Off", "On"});
        configSwitch(SIDESOLO_PARAM, 0.f, 1.f, 0.f, "Side solo (hear only the stereo difference)", {"Off", "On"});
        configButton(VIEW_LIM_PARAM, "Tracker view: limiter");
        configButton(VIEW_COMP_PARAM, "Tracker view: compressor");
        configButton(VIEW_MULTI_PARAM, "Tracker view: multiband leveler");
        configButton(VIEW_LOUD_PARAM, "Tracker view: loudness");
        configInput(IN_L_INPUT, "Left in");
        configInput(IN_R_INPUT, "Right in (normalled to left)");
        configInput(SIDECHAIN_INPUT, "External sidechain for the compressor");
        configInput(THR_CV_INPUT, "Compressor threshold CV (4 dB per volt)");
        configInput(DRIVE_CV_INPUT, "Saturation drive CV (2.4 dB per volt)");
        configInput(WIDTH_CV_INPUT, "Stereo width CV (20% per volt)");
        configOutput(OUT_L_OUTPUT, "Left out");
        configOutput(OUT_R_OUTPUT, "Right out");

        for (int i = 0; i < NSPEC; i++) specFreq[i] = 30.f * std::pow(16000.f / 30.f, (float) i / (NSPEC - 1));
        std::fill(limDl[0], limDl[0] + LA_MAX, 0.f);
        std::fill(limDl[1], limDl[1] + LA_MAX, 0.f);
        std::fill(limGreq, limGreq + 2 * LA_MAX, 1.f);
        std::fill(limGmin, limGmin + LA_MAX, 1.f);
        onSampleRateChange();
        updateControlRate();
    }

    void onReset() override {
        compG = 0.f; limRel = 1.f; mbMixSm = 0.f;
        for (int i = 0; i < 3; i++) { mbFast[i] = mbSlow[i] = 0.f; mbGain[i] = 0.f; }
    }

    // ------------------------------------------------------------------
    // fixed filters that depend only on the sample rate
    // ------------------------------------------------------------------
    void onSampleRateChange() override {
        sr = APP->engine->getSampleRate();
        for (float& f : bandF) f = 0.f;
        setBands();
        for (int ch = 0; ch < 2; ch++) {
            bLo[ch].reset(); bMidHp[ch].reset(); bMidLp[ch].reset(); bHi[ch].reset();
            osLp1[ch].lowpass(0.45f * sr, 0.70710678f, 2.f * sr); osLp2[ch].copyCoefs(osLp1[ch]);
            osLp1[ch].reset(); osLp2[ch].reset();
        }
        for (int i = 0; i < NSPEC; i++) specBp[i].bandpass(specFreq[i], 4.3f, sr);
        // K-weighting (ITU-R BS.1770): high shelf + 38 Hz high-pass
        kShelf.c[0].highShelf(1681.97f, 3.99984f, sr);
        kShelf.sync();
        kHp.c[0].highpass(38.1355f, 0.5003f, sr);
        kHp.sync();
        laN = (int) fclamp(std::floor(0.0015f * sr), 8.f, (float) LA_MAX - 1.f);
        std::fill(limDl[0], limDl[0] + LA_MAX, 0.f);
        std::fill(limDl[1], limDl[1] + LA_MAX, 0.f);
        std::fill(limGreq, limGreq + 2 * LA_MAX, 1.f);
        std::fill(limGmin, limGmin + LA_MAX, 1.f);
        limRel = 1.f;
        trkLen = std::max(1, (int) std::round(0.01f * sr));
    }

    // leveler band edges (LOW, MID LO, MID HI, HIGH knobs), recomputed only when a knob moved
    void setBands() {
        for (int i = 0; i < 4; i++) {
            float f = edgeHz(params[MB_LOW_PARAM + i].getValue());
            if (f == bandF[i]) continue;
            bandF[i] = f;
            for (int ch = 0; ch < 2; ch++) {
                if (i == 0) bLo[ch].setLP(f, sr);
                else if (i == 1) bMidHp[ch].setHP(f, sr);
                else if (i == 2) bMidLp[ch].setLP(f, sr);
                else bHi[ch].setHP(f, sr);
            }
        }
    }

    // ------------------------------------------------------------------
    // knob-dependent filters, refreshed every 32 samples
    // ------------------------------------------------------------------
    void updateControlRate() {
        setBands();
        float lc = params[LOWCUT_PARAM].getValue();
        lowCut.c[0].highpass(lc, 0.70710678f, sr); lowCut.sync();
        hiCut.c[0].lowpass(params[HICUT_PARAM].getValue(), 0.70710678f, sr); hiCut.sync();
        lowSh.c[0].lowShelf(120.f, params[LOW_PARAM].getValue(), sr); lowSh.sync();
        highSh.c[0].highShelf(6000.f, params[HIGH_PARAM].getValue(), sr); highSh.sync();
        midPk.c[0].peak(1000.f, 0.7f, params[EQ_MID_PARAM].getValue(), sr); midPk.sync();
        midCut.c[0].peak(1000.f, 0.6f, -24.f, sr); midCut.sync();
        float t = params[TILT_PARAM].getValue();
        tiltLo.c[0].lowShelf(650.f, -t * 5.f, sr); tiltLo.sync();
        tiltHi.c[0].highShelf(650.f, t * 5.f, sr); tiltHi.sync();
        airSh.c[0].highShelf(12000.f, params[AIR_PARAM].getValue() * 9.f, sr); airSh.sync();
        scHp.c[0].highpass(params[COMP_SCHPF_PARAM].getValue(), 0.70710678f, sr); scHp.sync();
        monoHp.highpass(params[MONO_BASS_PARAM].getValue(), 0.70710678f, sr);
    }

    // ------------------------------------------------------------------
    // main audio callback
    // ------------------------------------------------------------------
    void process(const ProcessArgs& args) override {
        if (++ctrlCount >= 32) {
            ctrlCount = 0; updateControlRate();
            for (int i = 0; i < NUM_VIEWS; i++)
                if (viewTrig[i].process(params[VIEW_LIM_PARAM + i].getValue() > 0.5f)) view = i;
        }

        // ---------------- inputs ----------------
        float inL = inputs[IN_L_INPUT].getVoltage();
        float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : inL;
        float l = inL, r = inR;

        disp.inPeak = std::max(disp.inPeak * 0.99995f, std::max(std::fabs(inL), std::fabs(inR)));

        // ---------------- trim + filters ----------------
        float trimT = db2lin(params[TRIM_PARAM].getValue());
        trimLin += (trimT - trimLin) * 0.002f;
        l *= trimLin; r *= trimLin;

        if (params[LOWCUT_PARAM].getValue() > 20.5f) { l = lowCut.c[0].process(l); r = lowCut.c[1].process(r); }
        if (params[HICUT_PARAM].getValue() < 19950.f) { l = hiCut.c[0].process(l); r = hiCut.c[1].process(r); }
        l = airSh.c[0].process(highSh.c[0].process(lowSh.c[0].process(tiltHi.c[0].process(tiltLo.c[0].process(l)))));
        r = airSh.c[1].process(highSh.c[1].process(lowSh.c[1].process(tiltHi.c[1].process(tiltLo.c[1].process(r)))));
        l = midPk.c[0].process(l); r = midPk.c[1].process(r);
        midCutSm += (params[MIDCUT_PARAM].getValue() - midCutSm) * 0.002f;
        { float cl = midCut.c[0].process(l), cr = midCut.c[1].process(r); l += (cl - l) * midCutSm; r += (cr - r) * midCutSm; }

        // ---------------- mid / side gain ----------------
        float midDb = params[MID_PARAM].getValue(), sideDb = params[SIDE_PARAM].getValue();
        if (std::fabs(midDb) > 0.01f || std::fabs(sideDb) > 0.01f) {
            float m = 0.5f * (l + r) * db2lin(midDb), s = 0.5f * (l - r) * db2lin(sideDb);
            l = m + s; r = m - s;
        }

        // ---------------- glue compressor ----------------
        float thrCv = inputs[THR_CV_INPUT].isConnected() ? inputs[THR_CV_INPUT].getVoltage() * 4.f : 0.f;
        float thr = fclamp(params[COMP_THRESH_PARAM].getValue() + thrCv, -60.f, 6.f);
        float ratio = params[COMP_RATIO_PARAM].getValue();
        float knee = params[COMP_KNEE_PARAM].getValue();
        float aC = onePole(params[COMP_ATTACK_PARAM].getValue(), sr);
        float rC = onePole(params[COMP_RELEASE_PARAM].getValue(), sr);
        float det;
        if (inputs[SIDECHAIN_INPUT].isConnected()) {
            float sc = inputs[SIDECHAIN_INPUT].getVoltage();
            det = std::fabs(scHp.c[0].process(sc));
            (void) scHp.c[1].process(0.f);
        } else {
            det = std::max(std::fabs(scHp.c[0].process(l)), std::fabs(scHp.c[1].process(r)));
        }
        float lvDb = lin2db(det / FS_V);
        float over = lvDb - thr, outDb;
        if (2.f * over < -knee) outDb = lvDb;
        else if (knee > 0.01f && 2.f * std::fabs(over) <= knee) outDb = lvDb + (1.f / ratio - 1.f) * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.f * knee);
        else outDb = thr + over / ratio;
        float grT = std::max(0.f, lvDb - outDb);
        compG += (grT - compG) * (grT > compG ? aC : rC);
        float makeup = params[AUTO_PARAM].getValue() > 0.5f ? -thr * (1.f - 1.f / ratio) * 0.5f : 0.f;
        float cg = db2lin(-compG + makeup);
        float mix = params[COMP_MIX_PARAM].getValue();
        l = l * (1.f - mix) + l * cg * mix;
        r = r * (1.f - mix) + r * cg * mix;
        compThrDb = thr;
        trkCIn = std::max(trkCIn, det);
        trkCOut = std::max(trkCOut, std::max(std::fabs(l), std::fabs(r)));

        // ---------------- 3-band leveler (MULTI) ----------------
        float mbAmt = params[MB_PARAM].getValue();
        float mbT = mbAmt > 0.001f ? fclamp(mbAmt * 10.f, 0.f, 1.f) : 0.f;
        mbMixSm += (mbT - mbMixSm) * 0.002f;
        float mbGrNow = 0.f;
        if (mbMixSm > 0.0005f) {
            // each band filtered out on its own (LR4 edges); its gain is applied as a change added to the
            // dry signal, so at 0 dB the leveler is transparent however the bands overlap or gap
            float bl[3], br[3];
            bl[0] = bLo[0].process(l); br[0] = bLo[1].process(r);
            bl[1] = bMidLp[0].process(bMidHp[0].process(l)); br[1] = bMidLp[1].process(bMidHp[1].process(r));
            bl[2] = bHi[0].process(l); br[2] = bHi[1].process(r);
            // SPEED: the slow average 3 s .. 300 ms (about 900 ms in the middle); RANGE: cut up to R dB, boost 5/9 R
            float slowMs = 300.f * std::pow(10.f, 1.f - params[MB_SPEED_PARAM].getValue());
            float rng = params[MB_RANGE_PARAM].getValue();
            float fastC = onePole(12.f, sr), slowC = onePole(slowMs, sr), gC = onePole(25.f, sr);
            float sumL = 0.f, sumR = 0.f;
            for (int b = 0; b < 3; b++) {
                float lv = 0.5f * (std::fabs(bl[b]) + std::fabs(br[b])) / FS_V;
                mbFast[b] += (lv - mbFast[b]) * fastC;
                mbSlow[b] += (lv - mbSlow[b]) * slowC;
                float tg = 0.f;
                if (mbSlow[b] > 1e-4f) tg = fclamp((lin2db(mbSlow[b]) - lin2db(mbFast[b])) * mbAmt * 0.75f, -rng, rng * 5.f / 9.f);
                mbGain[b] += (tg - mbGain[b]) * gC;
                float g = db2lin(mbGain[b]) - 1.f;
                sumL += bl[b] * g; sumR += br[b] * g;
                mbGrNow = std::max(mbGrNow, -mbGain[b]);
            }
            l += sumL * mbMixSm;
            r += sumR * mbMixSm;
        }

        // ---------------- saturation (2x oversampled) ----------------
        float driveCv = inputs[DRIVE_CV_INPUT].isConnected() ? inputs[DRIVE_CV_INPUT].getVoltage() * 2.4f : 0.f;
        float driveDb = fclamp(params[DRIVE_PARAM].getValue() + driveCv, 0.f, 30.f);
        if (driveDb > 0.05f) {
            float g = db2lin(driveDb), norm = 1.f / std::sqrt(g), smix = params[SATMIX_PARAM].getValue();
            float xs[2] = {l, r};
            for (int ch = 0; ch < 2; ch++) {
                float x = xs[ch], xm = 0.5f * (osPrev[ch] + x);
                float a = FS_V * std::tanh(g * (xm / FS_V) + 0.08f);
                float b = FS_V * std::tanh(g * (x / FS_V) + 0.08f);
                (void) osLp2[ch].process(osLp1[ch].process(a));
                float y = osLp2[ch].process(osLp1[ch].process(b)) * norm;
                osPrev[ch] = x;
                float dc = y - dcX[ch] + 0.995f * dcY[ch];            // remove the DC the asymmetry creates
                dcX[ch] = y; dcY[ch] = dc;
                xs[ch] = x * (1.f - smix) + dc * smix;
            }
            l = xs[0]; r = xs[1];
        }

        // ---------------- punch ----------------
        float punch = params[PUNCH_PARAM].getValue();
        {
            float lv = 0.5f * (std::fabs(l) + std::fabs(r));
            punchFast += (lv - punchFast) * onePole(1.f, sr);
            punchSlow += (lv - punchSlow) * onePole(25.f, sr);
            if (punch > 0.001f) {
                float tr = (punchFast - punchSlow) / (punchSlow + 0.02f);
                float pg = db2lin(10.f * punch * fclamp(tr, 0.f, 1.f));
                l *= pg; r *= pg;
            }
        }

        // ---------------- stereo: width + mono bass ----------------
        float widthCv = inputs[WIDTH_CV_INPUT].isConnected() ? inputs[WIDTH_CV_INPUT].getVoltage() * 0.2f : 0.f;
        float wT = fclamp(params[WIDTH_PARAM].getValue() + widthCv, 0.f, 2.f);
        widthSm += (wT - widthSm) * 0.002f;
        {
            float m = 0.5f * (l + r), s = 0.5f * (l - r) * widthSm;
            if (params[MONO_BASS_PARAM].getValue() > 20.5f) s = monoHp.process(s);
            l = m + s; r = m - s;
        }

        // ---------------- look-ahead limiter ----------------
        float ceilDb = params[LIMIT_CEILING_PARAM].getValue();
        float pushDb = std::max(0.f, ceilDb - params[LIMIT_THRESH_PARAM].getValue());
        float push = db2lin(pushDb), ceilLin = FS_V * db2lin(ceilDb);
        float limRelStep = onePole(params[LIMIT_RELEASE_PARAM].getValue(), sr);
        float limOut[2];
        float limInPk = std::max(std::fabs(l), std::fabs(r));
        {
            float pk = std::max(std::fabs(l), std::fabs(r)) * push;
            float greq = pk > ceilLin ? ceilLin / pk : 1.f;
            limGreq[limG] = greq;
            float mn = 1.f; int idx = limG;
            for (int k = 0; k < laN; k++) { mn = std::min(mn, limGreq[idx]); idx = (idx == 0) ? 2 * LA_MAX - 1 : idx - 1; }
            limGmin[limM] = mn;
            float sum = 0.f; idx = limM;
            for (int k = 0; k < laN; k++) { sum += limGmin[idx]; idx = (idx == 0) ? LA_MAX - 1 : idx - 1; }
            float gbox = sum / (float) laN;
            limG = (limG + 1 >= 2 * LA_MAX) ? 0 : limG + 1;
            limM = (limM + 1 >= LA_MAX) ? 0 : limM + 1;
            limRel = std::min(gbox, limRel + (1.f - limRel) * limRelStep);
            limDl[0][limW] = l; limDl[1][limW] = r;
            int rd = limW - (laN - 1); if (rd < 0) rd += LA_MAX;
            limOut[0] = limDl[0][rd] * push * limRel;
            limOut[1] = limDl[1][rd] * push * limRel;
            limW = (limW + 1 >= LA_MAX) ? 0 : limW + 1;
            limOut[0] = fclamp(limOut[0], -ceilLin, ceilLin);
            limOut[1] = fclamp(limOut[1], -ceilLin, ceilLin);
        }
        l = limOut[0]; r = limOut[1];

        // limiter tracker: per-block peaks and worst-case GR, published once per block
        trkIn = std::max(trkIn, limInPk);
        trkOut = std::max(trkOut, std::max(std::fabs(l), std::fabs(r)));
        trkLimG = std::min(trkLimG, limRel);
        trkCompG = std::max(trkCompG, compG);
        if (++trkCount >= trkLen) {
            uint32_t p = trackPos.load(std::memory_order_relaxed);
            TrackFrame& f = track[p % NTRACK];
            f.inDb = lin2db(trkIn / FS_V); f.outDb = lin2db(trkOut / FS_V);
            f.limGr = -lin2db(trkLimG); f.compGr = trkCompG;
            f.cInDb = lin2db(trkCIn / FS_V); f.cOutDb = lin2db(trkCOut / FS_V);
            f.mbLo = mbGain[0] * mbMixSm; f.mbMid = mbGain[1] * mbMixSm; f.mbHi = mbGain[2] * mbMixSm;
            f.lufsM = disp.lufsM; f.lufsS = disp.lufsS;
            trackPos.store(p + 1, std::memory_order_release);
            trkCount = 0; trkIn = trkOut = trkCIn = trkCOut = 0.f; trkLimG = 1.f; trkCompG = 0.f;
        }

        // ---------------- output gain + monitoring ----------------
        float oT = db2lin(params[OUT_GAIN_PARAM].getValue());
        outLin += (oT - outLin) * 0.002f;
        l *= outLin; r *= outLin;
        if (params[SIDESOLO_PARAM].getValue() > 0.5f) { float s = 0.5f * (l - r); l = s; r = s; }
        else if (params[MONO_PARAM].getValue() > 0.5f) { float m = 0.5f * (l + r); l = m; r = m; }

        // ---------------- bypass (click-free crossfade) ----------------
        float bT = params[BYPASS_PARAM].getValue() > 0.5f ? 1.f : 0.f;
        byp += (bT - byp) * 0.002f;
        float oL = l * (1.f - byp) + inL * byp, oR = r * (1.f - byp) + inR * byp;
        outputs[OUT_L_OUTPUT].setVoltage(oL);
        outputs[OUT_R_OUTPUT].setVoltage(oR);

        // ---------------- metering ----------------
        float pkOut = std::max(std::fabs(oL), std::fabs(oR));
        disp.outPeak = std::max(disp.outPeak * 0.99995f, pkOut);
        if (pkOut > peakHold) { peakHold = pkOut; peakHoldTimer = (int) (1.5f * sr); }
        else if (--peakHoldTimer < 0) peakHold *= 0.9998f;
        disp.peakHoldDb = lin2db(peakHold / FS_V);
        disp.compGR += (compG - disp.compGR) * 0.01f;
        disp.mbGR += (mbGrNow * mbMixSm - disp.mbGR) * 0.01f;
        disp.limitGR += (-lin2db(limRel) - disp.limitGR) * 0.02f;

        // correlation
        {
            const float c = 0.0004f;
            corrLR += (oL * oR - corrLR) * c; corrLL += (oL * oL - corrLL) * c; corrRR += (oR * oR - corrRR) * c;
            float den = std::sqrt(corrLL * corrRR);
            disp.corr = den > 1e-6f ? fclamp(corrLR / den, -1.f, 1.f) : 0.f;
        }
        // scope (every 3rd sample)
        if (++scopeDiv >= 3) {
            scopeDiv = 0;
            disp.scopeL[disp.scopeIdx] = oL / FS_V; disp.scopeR[disp.scopeIdx] = oR / FS_V;
            disp.scopeIdx = (disp.scopeIdx + 1) % NSCOPE;
        }
        // spectrum
        {
            float mono = 0.5f * (oL + oR) / FS_V;
            float aA = onePole(2.f, sr), aR = onePole(120.f, sr);
            for (int i = 0; i < NSPEC; i++) {
                if (specFreq[i] > 0.45f * sr) { disp.specEnv[i] = 0.f; continue; }
                float y = std::fabs(specBp[i].process(mono));
                specSm[i] += (y - specSm[i]) * (y > specSm[i] ? aA : aR);
                disp.specEnv[i] = specSm[i];
            }
        }
        // loudness (K-weighted, BS.1770 style)
        {
            float kl = kHp.c[0].process(kShelf.c[0].process(oL / FS_V));
            float kr = kHp.c[1].process(kShelf.c[1].process(oR / FS_V));
            float e = kl * kl + kr * kr;
            msM += (e - msM) * onePole(200.f, sr);
            msS += (e - msS) * onePole(1500.f, sr);
            float lm = -0.691f + 10.f * std::log10(std::max(msM, 1e-10f));
            disp.lufsM = lm; disp.lufsS = -0.691f + 10.f * std::log10(std::max(msS, 1e-10f));
            if (lm > -70.f) { intSum += msM; intCount += 1.0; }
            if (intCount > 0.5) disp.lufsI = -0.691f + 10.f * (float) std::log10(std::max(intSum / intCount, 1e-10));
        }

        // lights
        float dt = args.sampleTime;
        lights[COMP_GR_LIGHT].setSmoothBrightness(fclamp(compG / 10.f, 0.f, 1.f), dt);
        lights[LIMIT_GR_LIGHT].setSmoothBrightness(fclamp(disp.limitGR / 6.f, 0.f, 1.f), dt);
        lights[CORRELATION_LIGHT].setSmoothBrightness((disp.corr + 1.f) * 0.5f, dt);
        lights[BYPASS_LIGHT].setBrightness(params[BYPASS_PARAM].getValue());
        lights[AUTO_LIGHT].setBrightness(params[AUTO_PARAM].getValue());
        lights[MONO_LIGHT].setBrightness(params[MONO_PARAM].getValue());
        lights[SIDESOLO_LIGHT].setBrightness(params[SIDESOLO_PARAM].getValue());
        lights[MIDCUT_LIGHT].setBrightness(params[MIDCUT_PARAM].getValue());
        for (int i = 0; i < NUM_VIEWS; i++) { viewOn[i] = view == i ? 1.f : 0.f; lights[VIEW_LIM_LIGHT + i].setBrightness(viewOn[i]); }
    }

    void resetIntegrated() { intSum = 0.0; intCount = 0.0; disp.lufsI = -70.f; }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "view", json_integer(view));
        return root;
    }
    void dataFromJson(json_t* root) override {
        json_t* v = json_object_get(root, "view");
        if (v) view = clamp((int) json_integer_value(v), 0, NUM_VIEWS - 1);
    }
};

// ============================================================================
// Displays
// ============================================================================
namespace {
NVGcolor AMB(int a = 255) { return nvgRGBA(255, 190, 90, a); }
NVGcolor ORG(int a = 255) { return nvgRGBA(255, 122, 0, a); }
// leveler band colours: LOW, MID, HIGH (TONE display, LEVELER view, the band icons under the knobs)
const NVGcolor kBandCol[3] = {nvgRGBA(255, 122, 0, 255), nvgRGBA(255, 190, 90, 255), nvgRGBA(255, 240, 200, 255)};

// The band shape under the MULTIBAND knobs, as on a multiband compressor: low-pass (LOW), band-pass spanning
// MID LO .. MID HI, high-pass (HIGH). Drawn in the lit layer like the displays.
struct BandIcon : TransparentWidget {
    int band = 0;
    static BandIcon* create(float x0Mm, float x1Mm, float yMm, int band) {
        BandIcon* w = new BandIcon;
        w->band = band;
        w->box.pos = mm2px(Vec(x0Mm, yMm));
        w->box.size = mm2px(Vec(x1Mm - x0Mm, 1.8f));
        return w;
    }
    void drawLayer(const DrawArgs& a, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y, s = H;     // slope width = height
        nvgBeginPath(a.vg);
        if (band == 0) { nvgMoveTo(a.vg, 0.f, 0.f); nvgLineTo(a.vg, W - s, 0.f); nvgLineTo(a.vg, W, H); }
        else if (band == 2) { nvgMoveTo(a.vg, 0.f, H); nvgLineTo(a.vg, s, 0.f); nvgLineTo(a.vg, W, 0.f); }
        else { nvgMoveTo(a.vg, 0.f, H); nvgLineTo(a.vg, s, 0.f); nvgLineTo(a.vg, W - s, 0.f); nvgLineTo(a.vg, W, H); }
        nvgStrokeWidth(a.vg, 1.4f);
        nvgLineJoin(a.vg, NVG_ROUND);
        nvgStrokeColor(a.vg, kBandCol[band]);
        nvgStroke(a.vg);
    }
};

struct MasterDisplayBase : TransparentWidget {
    BlokMaster* module = nullptr;
    std::shared_ptr<window::Font> font;
    void bg(const DrawArgs& a) {
        nvgBeginPath(a.vg);
        nvgRoundedRect(a.vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgFillColor(a.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(a.vg);
    }
    void frame(const DrawArgs& a) {
        nvgBeginPath(a.vg);
        nvgRoundedRect(a.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 3.f);
        nvgStrokeWidth(a.vg, 0.8f);
        nvgStrokeColor(a.vg, nvgRGBA(255, 180, 90, 80));
        nvgStroke(a.vg);
    }
    bool loadFont() {
        if (!font) font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        return (bool) font;
    }
    void text(const DrawArgs& a, float x, float y, const char* s, float size, NVGcolor c, int align) {
        if (!loadFont()) return;
        nvgFontFaceId(a.vg, font->handle);
        nvgFontSize(a.vg, size);
        nvgFillColor(a.vg, c);
        nvgTextAlign(a.vg, align);
        nvgText(a.vg, x, y, s, nullptr);
    }
    void draw(const DrawArgs& a) override { bg(a); }
};

// Panel lettering drawn at run time, sized and coloured like the SVG's: white knob names,
// grey letter-spaced section headings.
struct PanelText : TransparentWidget {
    std::string s;
    bool heading = false;
    static PanelText* create(Vec centreMm, const char* s, bool heading) {
        PanelText* t = new PanelText;
        t->s = s; t->heading = heading;
        t->box.size = mm2px(Vec(30.f, 4.f));
        t->box.pos = mm2px(centreMm).minus(t->box.size.div(2.f));
        return t;
    }
    void draw(const DrawArgs& a) override {
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(a.vg, font->handle);
        nvgFontSize(a.vg, heading ? 6.8f : 6.2f);
        nvgTextLetterSpacing(a.vg, heading ? 1.6f : 0.4f);
        nvgTextAlign(a.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(a.vg, heading ? nvgRGB(0xbb, 0xbb, 0xbb) : nvgRGB(0xff, 0xff, 0xff));
        nvgText(a.vg, box.size.x * 0.5f, box.size.y * 0.5f, s.c_str(), NULL);
    }
};

// ---- ANALYZER: spectrum + loudness -----------------------------------------
struct SpectrumDisplay : MasterDisplayBase {
    float hold[NSPEC] = {};
    void drawLayer(const DrawArgs& a, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        float sx = 4.f, sy = 4.f, sw = W - 8.f, sh = H * 0.62f;

        // grid
        nvgStrokeWidth(a.vg, 0.5f);
        nvgStrokeColor(a.vg, nvgRGBA(255, 180, 90, 28));
        for (int k = 0; k <= 4; k++) { nvgBeginPath(a.vg); nvgMoveTo(a.vg, sx, sy + sh * k / 4.f); nvgLineTo(a.vg, sx + sw, sy + sh * k / 4.f); nvgStroke(a.vg); }

        float bw = sw / NSPEC;
        for (int i = 0; i < NSPEC; i++) {
            float v = 0.f;
            if (module) {
                float f = module->specFreq[i];
                float db = lin2db(module->disp.specEnv[i] * 1.6f) + 3.f * std::log2(std::max(f, 20.f) / 1000.f);   // +3 dB/oct tilt = pink reference
                v = fclamp((db + 66.f) / 66.f, 0.f, 1.f);
            } else {
                v = 0.55f + 0.3f * std::sin(i * 0.5f) - 0.015f * i;      // module browser preview
            }
            hold[i] = std::max(v, hold[i] - 0.004f);
            float h = v * sh, x = sx + i * bw + 0.5f;
            NVGpaint g = nvgLinearGradient(a.vg, 0, sy + sh - h, 0, sy + sh, nvgRGBA(255, 220, 120, 255), nvgRGBA(255, 100, 0, 200));
            nvgBeginPath(a.vg); nvgRect(a.vg, x, sy + sh - h, bw - 1.f, h); nvgFillPaint(a.vg, g); nvgFill(a.vg);
            nvgBeginPath(a.vg); nvgRect(a.vg, x, sy + sh - hold[i] * sh - 1.f, bw - 1.f, 1.2f); nvgFillColor(a.vg, nvgRGBA(255, 240, 200, 200)); nvgFill(a.vg);
        }
        // frequency ticks
        const char* fl[5] = {"50", "200", "1k", "4k", "16k"};
        float ff[5] = {50.f, 200.f, 1000.f, 4000.f, 16000.f};
        for (int k = 0; k < 5; k++) {
            float pos = std::log(ff[k] / 30.f) / std::log(16000.f / 30.f);
            text(a, sx + sw * pos, sy + sh + 8.f, fl[k], 6.5f, nvgRGBA(255, 180, 90, 120), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        }
        // loudness readout
        float lm = module ? module->disp.lufsM : -14.2f, ls = module ? module->disp.lufsS : -13.8f;
        float li = module ? module->disp.lufsI : -14.0f, pk = module ? module->disp.peakHoldDb : -0.3f;
        char b[48];
        float y0 = H - 17.f, colW = (W - 8.f) / 4.f;
        const char* nm[4] = {"MOM", "SHORT", "INTEG", "PEAK"};
        float val[4] = {lm, ls, li, pk};
        for (int k = 0; k < 4; k++) {
            float x = 4.f + colW * (k + 0.5f);
            text(a, x, y0, nm[k], 6.f, nvgRGBA(255, 180, 90, 120), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            if (val[k] < -69.f) snprintf(b, sizeof b, "--");
            else snprintf(b, sizeof b, "%.1f", val[k]);
            NVGcolor c = (k == 3 && val[k] > -0.5f) ? nvgRGBA(255, 70, 50, 255) : AMB();
            text(a, x, y0 + 6.5f, b, 8.5f, c, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        }
        frame(a);
    }
    void onButton(const event::Button& e) override {
        if (module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && e.pos.y > box.size.y * 0.7f) {
            module->resetIntegrated();          // click the readout to restart the integrated loudness
            e.consume(this);
        }
    }
};

// ---- METERS: vectorscope, correlation, level / GR bars ----------------------
struct MeterDisplay : MasterDisplayBase {
    void drawBar(const DrawArgs& a, float x, float y, float w, float h, float frac, bool fromTop, NVGcolor c, const char* label) {
        nvgBeginPath(a.vg); nvgRoundedRect(a.vg, x, y, w, h, 1.5f); nvgFillColor(a.vg, nvgRGBA(40, 26, 10, 255)); nvgFill(a.vg);
        frac = fclamp(frac, 0.f, 1.f);
        float fh = h * frac;
        nvgBeginPath(a.vg);
        if (fromTop) nvgRoundedRect(a.vg, x, y, w, fh, 1.5f); else nvgRoundedRect(a.vg, x, y + h - fh, w, fh, 1.5f);
        nvgFillColor(a.vg, c); nvgFill(a.vg);
        text(a, x + w * 0.5f, y + h + 2.f, label, 6.f, nvgRGBA(255, 180, 90, 140), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    }
    void drawLayer(const DrawArgs& a, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;

        // --- vectorscope (left) ---
        float cs = std::min(H - 22.f, W * 0.46f), cx = 4.f + cs * 0.5f, cy = 4.f + cs * 0.5f;
        nvgSave(a.vg);
        nvgBeginPath(a.vg); nvgRoundedRect(a.vg, 4.f, 4.f, cs, cs, 2.f); nvgFillColor(a.vg, nvgRGBA(26, 16, 5, 255)); nvgFill(a.vg);
        nvgStrokeWidth(a.vg, 0.5f); nvgStrokeColor(a.vg, nvgRGBA(255, 180, 90, 40));
        nvgBeginPath(a.vg); nvgMoveTo(a.vg, 4.f, cy); nvgLineTo(a.vg, 4.f + cs, cy); nvgMoveTo(a.vg, cx, 4.f); nvgLineTo(a.vg, cx, 4.f + cs); nvgStroke(a.vg);
        nvgScissor(a.vg, 4.f, 4.f, cs, cs);
        if (module) {
            for (int k = 0; k < NSCOPE; k++) {
                int i = (module->disp.scopeIdx + k) % NSCOPE;
                float l = module->disp.scopeL[i], r = module->disp.scopeR[i];
                float px = cx + (l - r) * 0.7071f * cs * 0.5f * 1.4f, py = cy - (l + r) * 0.7071f * cs * 0.5f * 1.4f;
                int al = 20 + (int) (170.f * k / NSCOPE);
                nvgBeginPath(a.vg); nvgRect(a.vg, px - 0.6f, py - 0.6f, 1.3f, 1.3f); nvgFillColor(a.vg, AMB(al)); nvgFill(a.vg);
            }
        } else {
            for (int k = 0; k < 120; k++) {
                float t = k * 0.12f;
                nvgBeginPath(a.vg); nvgRect(a.vg, cx + std::sin(t * 1.3f) * cs * 0.18f, cy + std::cos(t) * cs * 0.3f, 1.3f, 1.3f); nvgFillColor(a.vg, AMB(120)); nvgFill(a.vg);
            }
        }
        nvgResetScissor(a.vg);
        text(a, 6.f, 5.f, "L", 6.f, nvgRGBA(255, 180, 90, 110), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        text(a, 2.f + cs, 5.f, "R", 6.f, nvgRGBA(255, 180, 90, 110), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
        nvgRestore(a.vg);

        // --- correlation bar under the scope ---
        float corr = module ? module->disp.corr : 0.8f;
        float by = 4.f + cs + 4.f, bh = 5.f;
        nvgBeginPath(a.vg); nvgRoundedRect(a.vg, 4.f, by, cs, bh, 2.f); nvgFillColor(a.vg, nvgRGBA(40, 26, 10, 255)); nvgFill(a.vg);
        float mid = 4.f + cs * 0.5f, px = mid + corr * cs * 0.5f;
        nvgBeginPath(a.vg); nvgRect(a.vg, std::min(mid, px), by, std::fabs(px - mid), bh);
        nvgFillColor(a.vg, corr >= 0.f ? nvgRGBA(120, 220, 90, 230) : nvgRGBA(255, 70, 50, 230)); nvgFill(a.vg);
        text(a, 4.f, by + bh + 2.f, "-1", 6.f, nvgRGBA(255, 180, 90, 120), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        text(a, 4.f + cs * 0.5f, by + bh + 2.f, "CORR", 6.f, nvgRGBA(255, 180, 90, 120), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        text(a, 4.f + cs, by + bh + 2.f, "+1", 6.f, nvgRGBA(255, 180, 90, 120), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

        // --- level + GR bars (right) ---
        float bx0 = cs + 12.f, bwid = (W - bx0 - 4.f) / 5.f - 2.f, bTop = 4.f, bH = H - 22.f;
        float inDb = module ? lin2db(module->disp.inPeak / FS_V) : -9.f, outDb = module ? lin2db(module->disp.outPeak / FS_V) : -1.f;
        float cg = module ? module->disp.compGR : 3.f, mg = module ? module->disp.mbGR : 2.f, lg = module ? module->disp.limitGR : 2.f;
        auto lvlCol = [](float db) { return db > -0.5f ? nvgRGBA(255, 70, 50, 255) : db > -6.f ? nvgRGBA(255, 190, 60, 255) : nvgRGBA(120, 220, 90, 255); };
        drawBar(a, bx0 + 0 * (bwid + 2.f), bTop, bwid, bH, (inDb + 48.f) / 48.f, false, lvlCol(inDb), "IN");
        drawBar(a, bx0 + 1 * (bwid + 2.f), bTop, bwid, bH, cg / 12.f, true, nvgRGBA(255, 140, 30, 255), "CMP");
        drawBar(a, bx0 + 2 * (bwid + 2.f), bTop, bwid, bH, mg / 9.f, true, nvgRGBA(255, 190, 90, 255), "MB");
        drawBar(a, bx0 + 3 * (bwid + 2.f), bTop, bwid, bH, lg / 12.f, true, nvgRGBA(255, 70, 50, 255), "LIM");
        drawBar(a, bx0 + 4 * (bwid + 2.f), bTop, bwid, bH, (outDb + 48.f) / 48.f, false, lvlCol(outDb), "OUT");
        frame(a);
    }
};

// ---- DYNAMICS: scrolling history - limiter / compressor / leveler / loudness ----
// The view cells sit inside it along the top (oled::Cell, added by the widget), kCellTop mm down.
const float kCellTop = 1.5f;
struct DynamicsTracker : MasterDisplayBase {
    typedef float TrackFrame::*Field;
    TrackFrame buf[NVIEW];
    int n = 0;
    float sx = 4.f, sy = 4.f, sw = 0.f, sh = 0.f;   // plot area
    float top = 3.f, bot = -45.f;                   // value range of the plot

    float yOf(float v) { return sy + (top - fclamp(v, bot, top)) / (top - bot) * sh; }
    float xOf(int k) { return sx + sw - (n - 1 - k) * sw / (NVIEW - 1); }     // newest frame at the right edge
    float pxPer() { return sh / (top - bot); }

    // copy the newest frames out of the ring (or make up a preview for the module browser)
    void fetch() {
        if (module) {
            uint32_t pos = module->trackPos.load(std::memory_order_acquire);
            n = (int) std::min<uint32_t>(pos, NVIEW);
            for (int k = 0; k < n; k++) buf[k] = module->track[(pos - n + k) % NTRACK];
            return;
        }
        n = NVIEW;
        for (int k = 0; k < n; k++) {
            TrackFrame& f = buf[k];
            float in = -10.f + 5.f * std::sin(k * 0.05f) + 4.f * std::sin(k * 0.37f) * std::sin(k * 0.011f);
            f.limGr = std::max(0.f, in + 1.f);
            f.inDb = in; f.outDb = in - f.limGr + 0.9f;
            f.cInDb = in - 4.f; f.compGr = std::max(0.f, (f.cInDb + 12.f) * 0.5f); f.cOutDb = f.cInDb - f.compGr;
            f.mbLo = 3.f * std::sin(k * 0.03f); f.mbMid = -2.f + std::sin(k * 0.05f); f.mbHi = 2.f * std::sin(k * 0.021f + 1.f);
            f.lufsS = -14.f + 2.f * std::sin(k * 0.02f); f.lufsM = f.lufsS + 2.5f * std::sin(k * 0.13f);
        }
    }

    void hline(const DrawArgs& a, float y, NVGcolor c, bool dashed) {
        nvgStrokeWidth(a.vg, 0.8f);
        nvgStrokeColor(a.vg, c);
        nvgBeginPath(a.vg);
        if (!dashed) { nvgMoveTo(a.vg, sx, y); nvgLineTo(a.vg, sx + sw, y); }
        else for (float x = sx; x < sx + sw; x += 5.f) { nvgMoveTo(a.vg, x, y); nvgLineTo(a.vg, std::min(x + 2.5f, sx + sw), y); }
        nvgStroke(a.vg);
    }
    // filled area from the bottom edge up to a level curve
    void area(const DrawArgs& a, Field m, NVGpaint p) {
        if (n < 2) return;
        nvgBeginPath(a.vg);
        nvgMoveTo(a.vg, xOf(0), sy + sh);
        for (int k = 0; k < n; k++) nvgLineTo(a.vg, xOf(k), yOf(buf[k].*m));
        nvgLineTo(a.vg, xOf(n - 1), sy + sh);
        nvgClosePath(a.vg);
        nvgFillPaint(a.vg, p);
        nvgFill(a.vg);
    }
    NVGpaint solid(const DrawArgs& a, NVGcolor c) { return nvgLinearGradient(a.vg, 0, 0, 0, 1, c, c); }
    NVGpaint glow(const DrawArgs& a) { return nvgLinearGradient(a.vg, 0, sy, 0, sy + sh, nvgRGBA(255, 220, 120, 210), nvgRGBA(255, 100, 0, 110)); }
    // a curve; GR curves (fromTop) hang from the top edge on the plot's dB scale
    void curve(const DrawArgs& a, Field m, bool fromTop, NVGcolor c, float w) {
        if (n < 2) return;
        nvgBeginPath(a.vg);
        for (int k = 0; k < n; k++) {
            float y = fromTop ? sy + buf[k].*m * pxPer() : yOf(buf[k].*m);
            if (k == 0) nvgMoveTo(a.vg, xOf(k), y); else nvgLineTo(a.vg, xOf(k), y);
        }
        nvgStrokeWidth(a.vg, w);
        nvgStrokeColor(a.vg, c);
        nvgStroke(a.vg);
    }
    void grFill(const DrawArgs& a, Field m, NVGcolor c) {
        if (n < 2) return;
        nvgBeginPath(a.vg);
        nvgMoveTo(a.vg, xOf(0), sy);
        for (int k = 0; k < n; k++) nvgLineTo(a.vg, xOf(k), sy + buf[k].*m * pxPer());
        nvgLineTo(a.vg, xOf(n - 1), sy);
        nvgClosePath(a.vg);
        nvgFillColor(a.vg, c);
        nvgFill(a.vg);
    }
    void grid(const DrawArgs& a, float from, float step, int labelEvery, bool plus) {
        char b[16];
        nvgStrokeWidth(a.vg, 0.5f);
        nvgStrokeColor(a.vg, nvgRGBA(255, 180, 90, 28));
        int i = 0;
        for (float v = from; v >= bot; v -= step, i++) {
            nvgBeginPath(a.vg); nvgMoveTo(a.vg, sx, yOf(v)); nvgLineTo(a.vg, sx + sw, yOf(v)); nvgStroke(a.vg);
            if (i % labelEvery || v < bot + 1.f) continue;
            snprintf(b, sizeof b, plus && v > 0.f ? "+%d" : "%d", (int) v);
            text(a, sx + 1.f, yOf(v) + 1.f, b, 6.f, nvgRGBA(255, 180, 90, 110), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        }
    }
    void tag(const DrawArgs& a, float x, float y, const char* s, NVGcolor c, int align) {
        text(a, x, y, s, 6.f, c, align);
    }
    float maxOf(Field m) { float v = -90.f; for (int k = 0; k < n; k++) v = std::max(v, buf[k].*m); return v; }
    float last(Field m, float dflt) { return n ? buf[n - 1].*m : dflt; }

    // four-column readout under the plot, same look as the analyzer's loudness row
    void readout(const DrawArgs& a, const char* const nm[4], const float val[4], const bool hot[4], const char* const unit[4]) {
        float W = box.size.x, H = box.size.y, y0 = H - 17.f, colW = (W - 8.f) / 4.f;
        char b[32];
        nvgSave(a.vg);
        nvgResetScissor(a.vg);          // the readout sits below the clipped plot area
        for (int k = 0; k < 4; k++) {
            float x = 4.f + colW * (k + 0.5f), v = std::fabs(val[k]) < 0.05f ? 0.f : val[k];     // no "-0.0"
            text(a, x, y0, nm[k], 6.f, nvgRGBA(255, 180, 90, 120), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            if (v < -69.f) snprintf(b, sizeof b, "--");
            else snprintf(b, sizeof b, "%.1f%s", v, unit[k]);
            text(a, x, y0 + 6.5f, b, 8.5f, hot[k] ? nvgRGBA(255, 70, 50, 255) : AMB(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        }
        nvgRestore(a.vg);
    }
    float param(int id, float dflt) { return module ? module->params[id].getValue() : dflt; }

    void drawLimiter(const DrawArgs& a) {
        top = 3.f; bot = -45.f;
        float thrDb = param(BlokMaster::LIMIT_THRESH_PARAM, -1.f), ceilDb = param(BlokMaster::LIMIT_CEILING_PARAM, -0.1f);
        grid(a, 0.f, 6.f, 2, false);
        area(a, &TrackFrame::inDb, solid(a, AMB(45)));
        area(a, &TrackFrame::outDb, glow(a));
        hline(a, yOf(thrDb), AMB(150), true);
        hline(a, yOf(ceilDb), nvgRGBA(255, 70, 50, 200), false);
        grFill(a, &TrackFrame::limGr, nvgRGBA(255, 70, 50, 45));
        curve(a, &TrackFrame::limGr, true, nvgRGBA(255, 70, 50, 255), 1.2f);
        tag(a, sx + sw - 1.f, yOf(ceilDb) - 1.f, "CEIL", nvgRGBA(255, 70, 50, 200), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
        tag(a, sx + sw - 1.f, yOf(thrDb) + 1.f, "THR", AMB(150), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
        tag(a, sx + sw - 1.f, sy + sh - 2.f, "IN", AMB(120), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
        tag(a, sx + sw - 13.f, sy + sh - 2.f, "OUT", nvgRGBA(255, 200, 100, 230), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
        const char* nm[4] = {"LIM GR", "MAX GR", "CEIL", "OUT PK"};
        const char* un[4] = {"", "", "", ""};
        float val[4] = {-last(&TrackFrame::limGr, 0.f), -maxOf(&TrackFrame::limGr), ceilDb, maxOf(&TrackFrame::outDb)};
        bool hot[4] = {val[0] < -0.05f, val[1] < -0.05f, false, false};
        readout(a, nm, val, hot, un);
    }
    void drawComp(const DrawArgs& a) {
        top = 3.f; bot = -45.f;
        float thrDb = module ? module->compThrDb : -12.f;
        bool ext = module && module->inputs[BlokMaster::SIDECHAIN_INPUT].isConnected();
        grid(a, 0.f, 6.f, 2, false);
        area(a, &TrackFrame::cInDb, solid(a, AMB(45)));
        area(a, &TrackFrame::cOutDb, glow(a));
        hline(a, yOf(thrDb), AMB(150), true);
        grFill(a, &TrackFrame::compGr, nvgRGBA(255, 140, 30, 45));
        curve(a, &TrackFrame::compGr, true, nvgRGBA(255, 140, 30, 255), 1.2f);
        tag(a, sx + sw - 1.f, yOf(thrDb) + 1.f, "THR", AMB(150), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
        tag(a, sx + sw - 1.f, sy + sh - 2.f, ext ? "SC" : "DET", AMB(120), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
        tag(a, sx + sw - 17.f, sy + sh - 2.f, "OUT", nvgRGBA(255, 200, 100, 230), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
        const char* nm[4] = {"CMP GR", "MAX GR", "THR", "RATIO"};
        const char* un[4] = {"", "", "", ":1"};
        float val[4] = {-last(&TrackFrame::compGr, 0.f), -maxOf(&TrackFrame::compGr), thrDb, param(BlokMaster::COMP_RATIO_PARAM, 2.f)};
        bool hot[4] = {val[0] < -0.05f, val[1] < -0.05f, false, false};
        readout(a, nm, val, hot, un);
    }
    void drawMulti(const DrawArgs& a) {
        top = 10.f; bot = -10.f;
        grid(a, 10.f, 2.5f, 2, true);
        hline(a, yOf(0.f), AMB(90), false);
        NVGcolor cl = nvgRGBA(255, 122, 0, 255), cm = nvgRGBA(255, 190, 90, 255), ch = nvgRGBA(255, 240, 200, 255);
        curve(a, &TrackFrame::mbLo, false, cl, 1.2f);
        curve(a, &TrackFrame::mbMid, false, cm, 1.2f);
        curve(a, &TrackFrame::mbHi, false, ch, 1.2f);
        tag(a, sx + sw - 1.f, sy + sh - 2.f, "HI", ch, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
        tag(a, sx + sw - 11.f, sy + sh - 2.f, "MID", cm, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
        tag(a, sx + sw - 27.f, sy + sh - 2.f, "LOW", cl, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
        float amt = param(BlokMaster::MB_PARAM, 0.5f);
        if (amt < 0.001f) text(a, sx + sw * 0.5f, sy + sh * 0.3f, "LEVELER OFF", 7.f, AMB(110), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        const char* nm[4] = {"LOW", "MID", "HIGH", "AMOUNT"};
        const char* un[4] = {"", "", "", "%"};
        float val[4] = {last(&TrackFrame::mbLo, 0.f), last(&TrackFrame::mbMid, 0.f), last(&TrackFrame::mbHi, 0.f), amt * 100.f};
        bool hot[4] = {false, false, false, false};
        readout(a, nm, val, hot, un);
    }
    void drawLoud(const DrawArgs& a) {
        top = 0.f; bot = -42.f;
        float integ = module ? module->disp.lufsI : -14.f;
        grid(a, 0.f, 6.f, 2, false);
        area(a, &TrackFrame::lufsS, glow(a));
        curve(a, &TrackFrame::lufsM, false, nvgRGBA(255, 240, 200, 200), 1.f);
        hline(a, yOf(-14.f), nvgRGBA(120, 220, 90, 150), true);
        tag(a, sx + sw - 1.f, yOf(-14.f) - 1.f, "-14", nvgRGBA(120, 220, 90, 180), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
        if (integ > -69.f) {
            hline(a, yOf(integ), AMB(150), false);
            tag(a, sx + sw - 1.f, yOf(integ) + 1.f, "INT", AMB(150), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
        }
        tag(a, sx + sw - 1.f, sy + sh - 2.f, "MOM", nvgRGBA(255, 240, 200, 200), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
        tag(a, sx + sw - 17.f, sy + sh - 2.f, "SHORT", nvgRGBA(255, 200, 100, 230), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
        const char* nm[4] = {"MOM", "SHORT", "INTEG", "MAX S"};
        const char* un[4] = {"", "", "", ""};
        float val[4] = {last(&TrackFrame::lufsM, -70.f), last(&TrackFrame::lufsS, -70.f), integ, n ? maxOf(&TrackFrame::lufsS) : -70.f};
        bool hot[4] = {false, false, false, false};
        readout(a, nm, val, hot, un);
    }

    void drawLayer(const DrawArgs& a, int layer) override {
        if (layer != 1) return;
        // the plot starts under the view cells (LIM / COMP / MULTI / LOUD, laid over the top of the display)
        sy = mm2px(kCellTop + oled::kCellH) + 3.f;
        sw = box.size.x - 8.f; sh = box.size.y - 26.f - sy;
        fetch();
        int v = module ? module->view : VIEW_LIM;
        nvgSave(a.vg);
        nvgScissor(a.vg, sx, sy, sw, sh);
        if (v == VIEW_COMP) drawComp(a);
        else if (v == VIEW_MULTI) drawMulti(a);
        else if (v == VIEW_LOUD) drawLoud(a);
        else drawLimiter(a);
        nvgResetScissor(a.vg);
        nvgRestore(a.vg);
        frame(a);
    }
    void onButton(const event::Button& e) override {
        if (module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            module->view = (module->view + 1) % NUM_VIEWS;      // click the display to step through the views
            e.consume(this);
        }
    }
};

// ---- TONE: EQ response with the leveler crossovers and live band gains ------
struct ToneDisplay : MasterDisplayBase {
    // biquad magnitude at f (dB)
    static float magDb(const Biquad& q, float f, float sr) {
        float w = 2.f * (float) M_PI * f / sr, c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2.f * w), s2 = std::sin(2.f * w);
        float nr = q.b0 + q.b1 * c1 + q.b2 * c2, ni = -(q.b1 * s1 + q.b2 * s2);
        float dr = 1.f + q.a1 * c1 + q.a2 * c2, di = -(q.a1 * s1 + q.a2 * s2);
        return 10.f * std::log10(std::max(nr * nr + ni * ni, 1e-12f) / std::max(dr * dr + di * di, 1e-12f));
    }
    float param(int id, float dflt) { return module ? module->params[id].getValue() : dflt; }

    void drawLayer(const DrawArgs& a, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        float sx = 4.f, sy = 4.f, sw = W - 8.f, sh = H * 0.62f;
        const float F0 = 30.f, F1 = 16000.f, R = 12.f;
        auto fx = [&](float f) { return sx + sw * std::log(f / F0) / std::log(F1 / F0); };
        auto gy = [&](float db) { return sy + sh * 0.5f - fclamp(db, -R, R) / R * sh * 0.5f; };
        NVGcolor dim = nvgRGBA(255, 180, 90, 120);

        // the same filters process() runs, from the current knob values
        float sr = module ? module->sr : 48000.f;
        float lcF = param(BlokMaster::LOWCUT_PARAM, 40.f), lowDb = param(BlokMaster::LOW_PARAM, 3.f), tilt = param(BlokMaster::TILT_PARAM, 0.2f);
        float highDb = param(BlokMaster::HIGH_PARAM, -2.f), airAmt = param(BlokMaster::AIR_PARAM, 0.4f);
        float midDb = param(BlokMaster::EQ_MID_PARAM, 0.f);
        float cutAmt = module ? module->midCutSm : 0.f;
        float hcF = param(BlokMaster::HICUT_PARAM, 20000.f);
        Biquad lc, hc, ls, hs, tl, th, air, mp, mc;
        hc.lowpass(hcF, 0.70710678f, sr);
        mp.peak(1000.f, 0.7f, midDb, sr);
        mc.peak(1000.f, 0.6f, -24.f, sr);
        lc.highpass(lcF, 0.70710678f, sr);
        ls.lowShelf(120.f, lowDb, sr);
        hs.highShelf(6000.f, highDb, sr);
        tl.lowShelf(650.f, -tilt * 5.f, sr);
        th.highShelf(650.f, tilt * 5.f, sr);
        air.highShelf(12000.f, airAmt * 9.f, sr);

        // grid: 6 dB lines, 0 dB brighter
        nvgStrokeWidth(a.vg, 0.5f);
        for (int k = -2; k <= 2; k++) {
            nvgStrokeColor(a.vg, nvgRGBA(255, 180, 90, k == 0 ? 60 : 28));
            nvgBeginPath(a.vg); nvgMoveTo(a.vg, sx, gy(k * 6.f)); nvgLineTo(a.vg, sx + sw, gy(k * 6.f)); nvgStroke(a.vg);
        }
        text(a, sx + 1.f, gy(12.f) + 1.f, "+12", 6.f, nvgRGBA(255, 180, 90, 90), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        text(a, sx + 1.f, gy(-12.f) - 1.f, "-12", 6.f, nvgRGBA(255, 180, 90, 90), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);

        // leveler bands, drawn like a multiband compressor's: each band's response (LR4: low-pass at LOW,
        // band-pass MID LO .. MID HI, high-pass at HIGH) as a trapezoid, its flat top riding the band's live gain
        const float dflt[4] = {0.285f, 0.285f, 0.743f, 0.743f};
        float ef[4];
        for (int i = 0; i < 4; i++) ef[i] = BlokMaster::edgeHz(param(BlokMaster::MB_LOW_PARAM + i, dflt[i]));
        {
            Biquad lpLo, hpLo, lpHi, hpHi;
            lpLo.lowpass(ef[0], 0.70710678f, sr); hpLo.highpass(ef[1], 0.70710678f, sr);
            lpHi.lowpass(ef[2], 0.70710678f, sr); hpHi.highpass(ef[3], 0.70710678f, sr);
            const float top = sy + sh * 0.14f, floorDb = 36.f;      // a band's 0 dB, and the depth shown below it
            const int NB = 48;
            nvgSave(a.vg);
            nvgScissor(a.vg, sx, sy, sw, sh);
            for (int b = 0; b < 3; b++) {
                float g = module ? module->mbGain[b] * module->mbMixSm : (b == 0 ? 2.f : b == 1 ? -1.5f : 1.f);
                float lift = g / R * sh * 0.5f;
                nvgBeginPath(a.vg);
                nvgMoveTo(a.vg, fx(F0), sy + sh);
                for (int i = 0; i <= NB; i++) {
                    float f = F0 * std::pow(F1 / F0, (float) i / NB);
                    float db = b == 0 ? magDb(lpLo, f, sr) : b == 1 ? magDb(hpLo, f, sr) + magDb(lpHi, f, sr) : magDb(hpHi, f, sr);
                    db *= 2.f;                                              // LR4 = each Butterworth stage twice
                    nvgLineTo(a.vg, fx(f), top - lift + std::min(-db, floorDb) / floorDb * (sy + sh - top));
                }
                nvgLineTo(a.vg, fx(F1), sy + sh);
                nvgClosePath(a.vg);
                nvgFillColor(a.vg, nvgTransRGBA(kBandCol[b], 22)); nvgFill(a.vg);
                nvgStrokeWidth(a.vg, 1.f); nvgStrokeColor(a.vg, nvgTransRGBA(kBandCol[b], 150)); nvgStroke(a.vg);
            }
            nvgRestore(a.vg);
            // the four edges in their band's colour: LOW / HIGH on the top line, MID LO / MID HI under them
            for (int i = 0; i < 4; i++) {
                char eb[16];
                if (ef[i] >= 1000.f) snprintf(eb, sizeof eb, "%.1fk", ef[i] / 1000.f); else snprintf(eb, sizeof eb, "%d", (int) std::round(ef[i]));
                float x = fclamp(fx(ef[i]), sx + 8.f, sx + sw - 8.f);
                text(a, x, sy + (i == 1 || i == 2 ? 8.f : 1.f), eb, 6.f, nvgTransRGBA(kBandCol[i == 0 ? 0 : i == 3 ? 2 : 1], 190), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            }
        }

        // EQ curve with a soft fill to the 0 dB line
        const int NP = 64;
        float px[NP + 1], py[NP + 1];
        for (int i = 0; i <= NP; i++) {
            float f = F0 * std::pow(F1 / F0, (float) i / NP);
            float db = magDb(ls, f, sr) + magDb(hs, f, sr) + magDb(tl, f, sr) + magDb(th, f, sr) + magDb(air, f, sr);
            if (lcF > 20.5f) db += magDb(lc, f, sr);
            if (hcF < 19950.f) db += magDb(hc, f, sr);
            db += magDb(mp, f, sr);
            if (cutAmt > 0.001f) db += lin2db(1.f + (db2lin(magDb(mc, f, sr)) - 1.f) * cutAmt);
            px[i] = fx(f); py[i] = gy(db);
        }
        nvgBeginPath(a.vg);
        nvgMoveTo(a.vg, px[0], gy(0.f));
        for (int i = 0; i <= NP; i++) nvgLineTo(a.vg, px[i], py[i]);
        nvgLineTo(a.vg, px[NP], gy(0.f));
        nvgClosePath(a.vg);
        nvgFillColor(a.vg, AMB(40)); nvgFill(a.vg);
        nvgBeginPath(a.vg);
        for (int i = 0; i <= NP; i++) { if (i == 0) nvgMoveTo(a.vg, px[i], py[i]); else nvgLineTo(a.vg, px[i], py[i]); }
        nvgStrokeWidth(a.vg, 1.4f); nvgStrokeColor(a.vg, AMB()); nvgStroke(a.vg);

        // frequency ticks (same as the analyzer)
        const char* fl[5] = {"50", "200", "1k", "4k", "16k"};
        const float ff[5] = {50.f, 200.f, 1000.f, 4000.f, 16000.f};
        for (int k = 0; k < 5; k++) text(a, fx(ff[k]), sy + sh + 2.f, fl[k], 6.f, nvgRGBA(255, 180, 90, 100), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

        // readout: the tone knobs
        char b[6][16];
        if (lcF > 20.5f) snprintf(b[0], 16, "%d", (int) lcF); else snprintf(b[0], 16, "OFF");
        snprintf(b[1], 16, "%+.1f", lowDb);
        if (param(BlokMaster::MIDCUT_PARAM, 0.f) > 0.005f) snprintf(b[2], 16, "CUT%d", (int) std::round(param(BlokMaster::MIDCUT_PARAM, 0.f) * 100.f));
        else snprintf(b[2], 16, "%+.1f", midDb);
        snprintf(b[3], 16, "%+.1f", tilt * 5.f);
        snprintf(b[4], 16, "%+.1f", highDb);
        snprintf(b[5], 16, "%d%%", (int) std::round(airAmt * 100.f));
        const char* nm[6] = {"LO CUT", "LOW", "MID", "TILT", "HIGH", "AIR"};
        float y0 = H - 17.f, colW = (W - 8.f) / 6.f;
        for (int k = 0; k < 6; k++) {
            float x = 4.f + colW * (k + 0.5f);
            text(a, x, y0, nm[k], 6.f, dim, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            text(a, x, y0 + 6.5f, b[k], 8.f, AMB(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        }
        frame(a);
    }
};
} // namespace

// ============================================================================
// Widget
// ============================================================================
struct BlokMasterWidget : ModuleWidget {
    BlokMasterWidget(BlokMaster* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/BloKMaster.svg")));
        using namespace rack::componentlibrary;
        const float W = BML::PANEL_W;

        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(W - 3.5f, 3.5f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(3.5f, 125.f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(W - 3.5f, 125.f))));

        auto knob = [&](int col, int row, int id) {
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(BML::KX[col], BML::KY[row])), module, id));
        };
        // row 1: tone
        knob(0, 0, BlokMaster::TRIM_PARAM);     knob(1, 0, BlokMaster::LOWCUT_PARAM);   knob(2, 0, BlokMaster::LOW_PARAM);
        knob(3, 0, BlokMaster::EQ_MID_PARAM);   knob(4, 0, BlokMaster::MIDCUT_PARAM);   knob(5, 0, BlokMaster::HIGH_PARAM);
        knob(6, 0, BlokMaster::HICUT_PARAM);
        // row 2: glue compressor
        knob(0, 1, BlokMaster::COMP_THRESH_PARAM); knob(1, 1, BlokMaster::COMP_RATIO_PARAM); knob(2, 1, BlokMaster::COMP_ATTACK_PARAM);
        knob(3, 1, BlokMaster::COMP_RELEASE_PARAM); knob(4, 1, BlokMaster::COMP_KNEE_PARAM); knob(5, 1, BlokMaster::COMP_SCHPF_PARAM);
        knob(6, 1, BlokMaster::COMP_MIX_PARAM);
        // row 3: color / dynamics / mid-side
        knob(0, 2, BlokMaster::DRIVE_PARAM);    knob(1, 2, BlokMaster::SATMIX_PARAM);   knob(2, 2, BlokMaster::PUNCH_PARAM);
        knob(3, 2, BlokMaster::MB_PARAM);       knob(4, 2, BlokMaster::MID_PARAM);      knob(5, 2, BlokMaster::SIDE_PARAM);
        knob(6, 2, BlokMaster::AIR_PARAM);
        // row 4: stereo / limiter / out
        knob(0, 3, BlokMaster::WIDTH_PARAM);    knob(1, 3, BlokMaster::MONO_BASS_PARAM); knob(2, 3, BlokMaster::LIMIT_THRESH_PARAM);
        knob(3, 3, BlokMaster::LIMIT_CEILING_PARAM); knob(4, 3, BlokMaster::LIMIT_RELEASE_PARAM); knob(5, 3, BlokMaster::OUT_GAIN_PARAM);
        knob(6, 3, BlokMaster::TILT_PARAM);

        // monitor buttons (round TL1105 with the light under the cap, like the other modules)
        const int btnParam[4] = {BlokMaster::BYPASS_PARAM, BlokMaster::AUTO_PARAM, BlokMaster::MONO_PARAM, BlokMaster::SIDESOLO_PARAM};
        const int btnLight[4] = {BlokMaster::BYPASS_LIGHT, BlokMaster::AUTO_LIGHT, BlokMaster::MONO_LIGHT, BlokMaster::SIDESOLO_LIGHT};
        for (int i = 0; i < 4; i++) {
            Vec p = mm2px(Vec(BML::BTN_X[i], BML::BTN_Y));
            if (i == 0) addChild(createLightCentered<SmallLight<RedLight>>(p, module, btnLight[i]));
            else if (i == 1) addChild(createLightCentered<SmallLight<GreenLight>>(p, module, btnLight[i]));
            else addChild(createLightCentered<SmallLight<YellowLight>>(p, module, btnLight[i]));
            auto* b = createParamCentered<TL1105>(p, module, btnParam[i]);
            b->momentary = false;
            addParam(b);
        }

        // CV inputs
        addInput(createInputCentered<BlackJack>(mm2px(Vec(BML::CV_X[0], BML::CV_Y)), module, BlokMaster::THR_CV_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(BML::CV_X[1], BML::CV_Y)), module, BlokMaster::DRIVE_CV_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(BML::CV_X[2], BML::CV_Y)), module, BlokMaster::WIDTH_CV_INPUT));

        // audio I/O
        addInput(createInputCentered<BlackJack>(mm2px(Vec(BML::IN_X[0], BML::IO_Y)), module, BlokMaster::IN_L_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(BML::IN_X[1], BML::IO_Y)), module, BlokMaster::IN_R_INPUT));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(BML::IN_X[2], BML::IO_Y)), module, BlokMaster::SIDECHAIN_INPUT));
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(BML::OUT_X[0], BML::IO_Y)), module, BlokMaster::OUT_L_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(BML::OUT_X[1], BML::IO_Y)), module, BlokMaster::OUT_R_OUTPUT));

        // tiny status lights beside the section headings
        addChild(createLightCentered<TinyLight<RedLight>>(mm2px(Vec(93.f, 33.6f)), module, BlokMaster::COMP_GR_LIGHT));
        addChild(createLightCentered<TinyLight<RedLight>>(mm2px(Vec(93.f, 76.8f)), module, BlokMaster::LIMIT_GR_LIGHT));
        addChild(createLightCentered<TinyLight<GreenLight>>(mm2px(Vec(BML::OUT_X[1] + 0.f, BML::IO_Y - 7.2f)), module, BlokMaster::CORRELATION_LIGHT));

        // displays
        auto* sp = new SpectrumDisplay();
        sp->module = module;
        sp->box.pos = mm2px(Vec(BML::D1[0], BML::D1[1]));
        sp->box.size = mm2px(Vec(BML::D1[2], BML::D1[3]));
        addChild(sp);

        auto* md = new MeterDisplay();
        md->module = module;
        md->box.pos = mm2px(Vec(BML::D2[0], BML::D2[1]));
        md->box.size = mm2px(Vec(BML::D2[2], BML::D2[3]));
        addChild(md);

        auto* dt = new DynamicsTracker();
        dt->module = module;
        dt->box.pos = mm2px(Vec(BML::D3[0], BML::D3[1]));
        dt->box.size = mm2px(Vec(BML::D3[2], BML::D3[3]));
        addChild(dt);

        auto* td = new ToneDisplay();
        td->module = module;
        td->box.pos = mm2px(Vec(BML::D4[0], BML::D4[1]));
        td->box.size = mm2px(Vec(BML::D4[2], BML::D4[3]));
        addChild(td);

        // tracker view cells, inside the DYNAMICS display along its top (lit = the shown view)
        static const char* const viewName[NUM_VIEWS] = {"LIM", "COMP", "MULTI", "LOUD"};
        const float CG = 1.f, CW = (BML::D3[2] - 4.f - CG * (NUM_VIEWS - 1)) / NUM_VIEWS;
        for (int i = 0; i < NUM_VIEWS; i++) {
            oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(BML::D3[0] + 2.f + i * (CW + CG), BML::D3[1] + kCellTop)), module,
                                                    BlokMaster::VIEW_LIM_PARAM + i);
            c->box.size = mm2px(Vec(CW, oled::kCellH));
            c->module = module; c->label = viewName[i];
            if (module) c->flash = &module->viewOn[i];
            addParam(c);
        }

        // leveler band edges where the view buttons were: LOW, MID LO, MID HI, HIGH, with the band shapes under them
        static const char* const mbName[4] = {"LOW", "MID LO", "MID HI", "HIGH"};
        for (int i = 0; i < 4; i++) {
            addParam(createParamCentered<Trimpot>(mm2px(Vec(BML::MBK_X[i], BML::MBK_Y)), module, BlokMaster::MB_LOW_PARAM + i));
            addChild(PanelText::create(Vec(BML::MBK_X[i], BML::MBK_LABEL_Y), mbName[i], false));
        }
        const float iy = BML::MBK_LABEL_Y + 2.6f, ih = 3.5f;   // icon half width under a single knob
        addChild(BandIcon::create(BML::MBK_X[0] - ih, BML::MBK_X[0] + ih, iy, 0));
        addChild(BandIcon::create(BML::MBK_X[1] - ih, BML::MBK_X[2] + ih, iy, 1));
        addChild(BandIcon::create(BML::MBK_X[3] - ih, BML::MBK_X[3] + ih, iy, 2));
        addChild(PanelText::create(Vec(BML::MBK_HEAD[0], BML::MBK_HEAD[1]), "MULTIBAND", true));
    }

    void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<BlokMaster*>(module);
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Restart integrated loudness", "", [=]() { m->resetIntegrated(); }));
        menu->addChild(createMenuLabel("Multiband leveler"));
        for (int id : {BlokMaster::MB_SPEED_PARAM, BlokMaster::MB_RANGE_PARAM}) {
            ui::Slider* s = new ui::Slider;
            s->quantity = m->paramQuantities[id];
            s->box.size.x = 220.f;
            menu->addChild(s);
        }
        ModulePictures::appendMenu(menu);
    }
};

Model* modelBloKMaster = createModel<BlokMaster, BlokMasterWidget>("BlokMaster");
