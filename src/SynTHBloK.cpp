#include "plugin.hpp"
#include "SynTHBloK.hpp"
#include "SynTHBloKPoly.hpp"
#include "SynTHBloKStereo.hpp"
#include "SynTHBloKFXModule.hpp"
#include "SynTHBloKMix24.hpp"


struct SynTHBloK : Module {

    enum ParamId {
        VOICE_SELECT_PARAM,
        FREQ_PARAM,
        FINE_PARAM,
        FM_CV_PARAM,
        PWM_CV_PARAM,
        LFO_ENABLE_PARAM,

        CUT_CV_PARAM,
        KEYTRACK_PARAM,
        ENV_AMT_PARAM,
        CUTOFF_PARAM,
        RES_PARAM,
        LFO_RATE_PARAM,

        A_PARAM,
        D_PARAM,
        SUSTAIN_PARAM,
        R_PARAM,
        
        GLIDE_PARAM,

        SINE_PARAM,
        SAW_PARAM,
        SQUARE_PARAM,
        VCA_OUT_LEVEL_PARAM,

        LFO_RANGE_PARAM,
        SUSTAIN_MODE_PARAM,

        SAW_BLADE_PARAM,
        MOD_CV_PARAM,
        SUB_LEVEL_PARAM,
        MIX_OUT_LEVEL_PARAM,

        ACCENT_PARAM,
        ACCENT_ENABLE_PARAM,

        LFO_FADE_PARAM,
        LFO_RETRIG_PARAM,
        LFO_SYNC_PARAM,
        LFO_MORPH_PARAM,

        CLOCK_RATE_PARAM,
        SPREAD_PARAM,
        PARAMS_LEN
    };

    enum InputId {
        VOCT_INPUT,
        PWM_CV_INPUT,
        RESET_INPUT,
        FM_CV_INPUT,
        CLOCK_IN_INPUT,
        CUT_CV_INPUT,
        KEYTRACK_INPUT, 
        EG_INPUT,
        SUSTAIN_CV_INPUT,
        FLT_INPUT,

        VCA_CV_INPUT,
        VCA_IN_INPUT,

        MOD_CV_INPUT,
        BLADE_IN_INPUT,

        ACCENT_INPUT,

        INPUTS_LEN
    };

    enum OutputId {
        TRI_OUTPUT,
        SQR_OUTPUT,
        SAW_OUTPUT,
        MIX_OUTPUT,
        EG_OUTPUT,
        FLT_OUTPUT,
        VCA_OUT_OUTPUT,
STEREO_L_OUTPUT,
STEREO_R_OUTPUT,

        SINE_OUT,
        LFO_TRI_OUTPUT,
        LFO_SQR_OUTPUT,

        CLOCK_OUT_OUTPUT,

        OUTPUTS_LEN
    };

    enum LightId {
        VOICE1_LIGHT,
        VOICE2_LIGHT,
        VOICE3_LIGHT,
        VOICE4_LIGHT,
VOICE_BUTTON_LIGHT,

        FREQ_LIGHT,
        FINE_LIGHT,
        FM_CV_LIGHT,
        PWM_CV_LIGHT,

        CUT_CV_LIGHT,
        CUTOFF_LIGHT,
        RES_LIGHT,
        LFO_RATE_LIGHT,

        A_LIGHT,
        D_LIGHT,
        SUSTAIN_LIGHT,
        R_LIGHT,
        GLIDE_LIGHT,

        SINE_LIGHT,
        SAW_LIGHT,
        SQUARE_LIGHT,
        VCA_OUT_LEVEL_LIGHT,
        VCA_OUT_OUTPUT_LIGHT,
        FLT_OUTPUT_LIGHT,
        ENUMS(ADSR_LED, 4 * 3),
        ENUMS(FLT_LED, 4 * 3),
        LIGHTS_LEN
    };

    SynTHBloKInternalBus localBusMain;

    float extBpm = 0.f;
    double lastClockTime = 0.0;
    double sampleCounter = 0.0;

    float lfoPhase = 0.f;
    float lfo2Phase = 0.f;
    float lastReset = 0.f;
    float lastResetTime = 0.f;
    float resetPeriod = 0.5f;
    float lfoFade = 0.f;
    float lfoDC = 0.f;
    float lfoSmooth = 0.f;

    float clockPhase = 0.f;
    float lastClock = 0.f;

    static constexpr float PI = 3.14159265f;
    static constexpr int MAX_VOICES = 8;

    enum EnvStage {
        ENV_IDLE,
        ENV_ATTACK,
        ENV_DECAY,
        ENV_SUSTAIN,
        ENV_RELEASE
    };

    float smoothPWM = 0.f;
    float smoothBlade = 0.f;
    float bladeDC = 0.f;
    float pwState = 0.5f;

    float oscPhase1[MAX_VOICES] = {};
    float oscPhase2[MAX_VOICES] = {};
    float oscPhase3[MAX_VOICES] = {};
    float oscPhase4[MAX_VOICES] = {};
    int voiceSetting = 3;        // 0–3 → 1–4 oscillators
    float prevVoiceButton = 0.f; // edge detection
    float subPhaseV[MAX_VOICES] = {};
    float glidePitchV[MAX_VOICES] = {};
    float envV[MAX_VOICES] = {};
    EnvStage envStage[MAX_VOICES] = {};
    bool gatePrev[MAX_VOICES] = {};

    float drift1[MAX_VOICES] = {};
    float drift2[MAX_VOICES] = {};
    float drift3[MAX_VOICES] = {};
    float drift4[MAX_VOICES] = {};

    float driftSpeed1[MAX_VOICES] = {};
    float driftSpeed2[MAX_VOICES] = {};
    float driftSpeed3[MAX_VOICES] = {};
    float driftSpeed4[MAX_VOICES] = {};

    float mixLP[MAX_VOICES] = {};

    float accentSmooth = 0.f;

    float s1 = 0.f;
    float s2 = 0.f;
    float s3 = 0.f;

    SynTHBloK() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
       configButton(VOICE_SELECT_PARAM, "Oscillator count");
       configLight(VOICE_BUTTON_LIGHT);

 
        configParam(FREQ_PARAM, -0.2f, 0.2f, 0.f, "Frequency");
        configParam(FINE_PARAM, -0.1f, 0.1f, 0.f, "Fine tune");

        configParam(FM_CV_PARAM, 0.f, 1.f, 0.f, "FM CV amount");
        configParam(PWM_CV_PARAM, 0.f, 1.2f, 0.f, "PWM CV amount");
        configSwitch(LFO_ENABLE_PARAM, 0.f, 1.f, 1.f, "LFO enable", {"Off", "On"});

configParam(CUT_CV_PARAM, 0.f, 1.f, 0.f, "Cutoff CV amount");
configParam(KEYTRACK_PARAM, -1.f, 1.f, 0.f, "Keytrack amount");   // <‑‑ correct spot
configInput(KEYTRACK_INPUT, "Keytrack CV");
        // Jack names (tooltips in Rack and in the VCV Library)
        configInput(VOCT_INPUT, "1V/oct");
        configInput(PWM_CV_INPUT, "PWM CV");
        configInput(RESET_INPUT, "Reset");
        configInput(FM_CV_INPUT, "FM CV");
        configInput(CLOCK_IN_INPUT, "Clock");
        configInput(CUT_CV_INPUT, "Cutoff CV");
        configInput(EG_INPUT, "Envelope generator");
        configInput(SUSTAIN_CV_INPUT, "Sustain CV");
        configInput(FLT_INPUT, "Filter");
        configInput(VCA_CV_INPUT, "VCA CV");
        configInput(VCA_IN_INPUT, "VCA");
        configInput(MOD_CV_INPUT, "Waveform mod CV");
        configInput(BLADE_IN_INPUT, "Blade CV");
        configInput(ACCENT_INPUT, "Accent");
        configOutput(TRI_OUTPUT, "Triangle");
        configOutput(SQR_OUTPUT, "Square");
        configOutput(SAW_OUTPUT, "Saw");
        configOutput(MIX_OUTPUT, "Mix");
        configOutput(EG_OUTPUT, "Envelope generator");
        configOutput(FLT_OUTPUT, "Filter");
        configOutput(VCA_OUT_OUTPUT, "VCA");
        configOutput(STEREO_L_OUTPUT, "Stereo left");
        configOutput(STEREO_R_OUTPUT, "Stereo right");
        configOutput(SINE_OUT, "Sine");
        configOutput(LFO_TRI_OUTPUT, "LFO triangle");
        configOutput(LFO_SQR_OUTPUT, "LFO square");
        configOutput(CLOCK_OUT_OUTPUT, "Clock");
configParam(ENV_AMT_PARAM, 0.f, 1.f, 0.33f, "Envelope amount");

configParam(CUTOFF_PARAM, 0.f, 1.f, 0.35f, "Filter cutoff");
configParam(RES_PARAM, 0.f, 1.f, 0.15f, "Resonance");
configParam(LFO_RATE_PARAM, 0.f, 1.f, 0.5f, "LFO rate");

        configParam(A_PARAM, 0.f, 2.f, 0.1f, "Attack");
        configParam(D_PARAM, 0.f, 2.f, 0.3f, "Decay");
        configParam(SUSTAIN_PARAM, 0.f, 1.f, 0.7f, "Sustain");
        configParam(R_PARAM, 0.f, 2.f, 0.3f, "Release");
        configParam(GLIDE_PARAM, 0.f, 1.f, 0.f, "Glide");

        configParam(SINE_PARAM, 0.f, 1.f, 0.5f, "Triangle level");
        configParam(SAW_PARAM, 0.f, 1.f, 0.5f, "Saw/Blade level");
        configParam(SQUARE_PARAM, 0.f, 1.f, 0.5f, "Square level");
        configParam(VCA_OUT_LEVEL_PARAM, 0.f, 1.f, 0.8f, "VCA out level");

        configSwitch(LFO_RANGE_PARAM, 0.f, 1.f, 0.f, "LFO range", {"Slow", "Fast"});
        configSwitch(SUSTAIN_MODE_PARAM, 0.f, 1.f, 0.f, "Sustain mode", {"ADR", "ADSR"});

        configSwitch(SAW_BLADE_PARAM, 0.f, 1.f, 0.f, "Saw / Blade", {"Saw", "Blade"});
        configParam(MOD_CV_PARAM, 0.f, 1.f, 0.f, "Waveform MOD CV amount");
        configParam(SUB_LEVEL_PARAM, 0.f, 1.f, 0.f, "Sub level");
        configParam(MIX_OUT_LEVEL_PARAM, 0.f, 1.f, 1.f, "Mix out level");

        configParam(ACCENT_PARAM, 0.f, 1.f, 0.f, "Accent amount");
        configSwitch(ACCENT_ENABLE_PARAM, 0.f, 1.f, 1.f, "Accent enable", {"Off", "On"});

        configParam(LFO_FADE_PARAM, 0.f, 1.f, 0.f, "LFO Fade-in");
        configSwitch(LFO_RETRIG_PARAM, 0.f, 1.f, 0.f, "LFO Retrigger", {"Off", "On"});
        configSwitch(LFO_SYNC_PARAM, 0.f, 1.f, 0.f, "LFO Sync", {"Off", "On"});
        configParam(LFO_MORPH_PARAM, 0.f, 1.f, 0.f, "LFO Morph");

        configParam(CLOCK_RATE_PARAM, 0.f, 1.f, 0.5f, "Clock rate");

        for (int v = 0; v < MAX_VOICES; ++v) {
            driftSpeed1[v] = (random::uniform() - 0.5f) * 0.02f;
            driftSpeed2[v] = (random::uniform() - 0.5f) * 0.02f;
            driftSpeed3[v] = (random::uniform() - 0.5f) * 0.02f;
            driftSpeed4[v] = (random::uniform() - 0.5f) * 0.02f;
        }
    }
    inline float warmth(float x) {
    float soft = tanhf(x * 1.15f);
    float odd  = x * x * x * 0.18f;
    float even = (x * x - 0.5f) * 0.04f;
    return soft * 0.82f + odd * 0.12f + even * 0.06f;
}

    // deep yellow -> yellow-orange -> orange -> orange-red -> red -> deep red -> deep dark red -> darker reds -> ember
    static void ledRamp(float w, float& r, float& g, float& b) {
        static const float C[10][3] = {
            {1.00f, 0.72f, 0.00f}, {1.00f, 0.55f, 0.00f}, {1.00f, 0.38f, 0.00f},
            {1.00f, 0.20f, 0.00f}, {1.00f, 0.04f, 0.00f}, {0.70f, 0.00f, 0.00f},
            {0.28f, 0.00f, 0.00f}, {0.16f, 0.00f, 0.00f}, {0.08f, 0.00f, 0.00f}, {0.03f, 0.00f, 0.00f}};
        float p = clamp(w, 0.f, 1.f) * 9.f;
        int i = (int)p; if (i > 8) i = 8;
        float f = p - (float)i;
        r = C[i][0] + (C[i + 1][0] - C[i][0]) * f;
        g = C[i][1] + (C[i + 1][1] - C[i][1]) * f;
        b = C[i][2] + (C[i + 1][2] - C[i][2]) * f;
    }
    inline void setParamLED(int baseId, float x) {
        float r, g, b; ledRamp(x, r, g, b);
        lights[baseId + 0].setSmoothBrightness(r, 1);
        lights[baseId + 1].setSmoothBrightness(g, 1);
        lights[baseId + 2].setSmoothBrightness(b, 1);
    }

    float stageLvl[4] = {0.f, 0.f, 0.f, 0.f};
    inline void setActParamLED(int baseId, float x, float act) {
        act = clamp(act, 0.f, 1.f);
        float r, g, b; ledRamp(x, r, g, b);
        lights[baseId + 0].setSmoothBrightness(r * act, 1);
        lights[baseId + 1].setSmoothBrightness(g * act, 1);
        lights[baseId + 2].setSmoothBrightness(b * act, 1);
    }

    inline void setActLED(int baseId, float n) {
        n = clamp(n, 0.f, 1.f);
        lights[baseId + 0].setSmoothBrightness(n, 1);
        lights[baseId + 1].setSmoothBrightness(n * 0.6f, 1);
        lights[baseId + 2].setSmoothBrightness(0.f, 1);
    }

    void process(const ProcessArgs& args) override {


        float dt = args.sampleTime;

        bool isolated =
            !inputs[VOCT_INPUT].isConnected() &&
            !inputs[EG_INPUT].isConnected() &&
            !inputs[FLT_INPUT].isConnected() &&
            !inputs[VCA_IN_INPUT].isConnected();

        if (isolated) {
            outputs[TRI_OUTPUT].setVoltage(0.f);
            outputs[SQR_OUTPUT].setVoltage(0.f);
            outputs[SAW_OUTPUT].setVoltage(0.f);
            outputs[MIX_OUTPUT].setVoltage(0.f);
            outputs[EG_OUTPUT].setVoltage(0.f);
            outputs[FLT_OUTPUT].setVoltage(0.f);
            outputs[VCA_OUT_OUTPUT].setVoltage(0.f);
            localBusMain.L[0] = 0.f;
            localBusMain.R[0] = 0.f;
            return;
        }
 

// --- Voice Count Button Logic ---
float btn = params[VOICE_SELECT_PARAM].getValue();
if (btn > 0.5f && prevVoiceButton <= 0.5f) {
    voiceSetting++;
    if (voiceSetting > 3)
        voiceSetting = 0;
}
prevVoiceButton = btn;
// LED 
lights[VOICE_BUTTON_LIGHT].setBrightness(btn > 0.5f ? 1.f : 0.f);
prevVoiceButton = btn;
int oscCount = voiceSetting + 1;   // 1–4 oscillators

float accentKnob = params[ACCENT_PARAM].getValue();

        bool accentEnabled = params[ACCENT_ENABLE_PARAM].getValue() > 0.5f;

        float accentCV = inputs[ACCENT_INPUT].isConnected()
            ? clamp(inputs[ACCENT_INPUT].getVoltage() / 10.f, 0.f, 1.f)
            : 0.f;

        float accentRaw = accentEnabled ? (accentKnob + accentCV) : 0.f;
        accentRaw = clamp(accentRaw, 0.f, 1.f);
        accentSmooth = accentSmooth * 0.9f + accentRaw * 0.1f;
        float ACC = accentSmooth;

        bool externalClock = inputs[CLOCK_IN_INPUT].isConnected();
        float clockRate = externalClock ? 0.f : (0.5f + params[CLOCK_RATE_PARAM].getValue() * 9.5f);
        bool clockTrig = false;

        sampleCounter += dt;

        if (externalClock) {
            float clk = inputs[CLOCK_IN_INPUT].getVoltage();
            clockTrig = (clk > 0.5f && lastClock <= 0.5f);
            lastClock = clk;

            if (clockTrig) {
                double now = sampleCounter;
                double dtClock = now - lastClockTime;
                lastClockTime = now;
                if (dtClock > 0.0001 && dtClock < 2.0)
                    extBpm = 60.0 / dtClock;
            }
            clockPhase = 0.f;
        }
        else {
            clockPhase += clockRate * dt;
            if (clockPhase >= 1.f) {
                clockPhase -= 1.f;
                clockTrig = true;
            }
        }

        float lfoRateKnob = params[LFO_RATE_PARAM].getValue();
        bool lfoFast = params[LFO_RANGE_PARAM].getValue() > 0.5f;

        float lfoMin = lfoFast ? 1.f : 0.02f;
        float lfoMax = lfoFast ? 50.f : 5.f;
        float lfoFreq = lfoMin * std::pow(lfoMax / lfoMin, lfoRateKnob);

        float resetNow = inputs[RESET_INPUT].getVoltage();
        bool resetTrig = (resetNow > 1.f && lastReset <= 1.f);
        lastReset = resetNow;

        if (resetTrig) {
            resetPeriod = lastResetTime;
            lastResetTime = 0.f;
        }
        else {
            lastResetTime += dt;
        }

        if (params[LFO_SYNC_PARAM].getValue() > 0.5f && resetPeriod > 0.f)
            lfoFreq = 1.f / resetPeriod;

        if (params[LFO_SYNC_PARAM].getValue() > 0.5f) {
            float k = params[LFO_RATE_PARAM].getValue();
            float div = 1.f;

            if      (k < 0.20f) div = 1.f;
            else if (k < 0.40f) div = 0.5f;
            else if (k < 0.60f) div = 0.25f;
            else if (k < 0.70f) div = 0.125f;
            else if (k < 0.80f) div = 0.125f * (2.f/3.f);
            else if (k < 0.90f) div = 0.0625f;
            else                div = 0.03125f;

            lfoFreq = extBpm * div / 60.f;
        }

        lfoPhase += lfoFreq * dt;
        if (lfoPhase >= 1.f)
            lfoPhase -= 1.f;

        float sine = sinf(lfoPhase * 2.f * PI);
        float tri  = 4.f * fabsf(lfoPhase - 0.5f) - 1.f;
        float saw  = lfoPhase * 2.f - 1.f;
        float sqr  = tanhf((lfoPhase < 0.5f ? 1.f : -1.f) * 3.f);
        float scurve = sinf((lfoPhase - 0.5f) * PI) * 1.2f;

        float lfoTriRaw = tri * 5.f;
        float lfoSqrRaw = sqr * 5.f;

        float morphKnob = params[LFO_MORPH_PARAM].getValue();
        float morph = morphKnob * morphKnob * (3.f - 2.f * morphKnob);

        float m = morph * 4.f;
        int idx = (int)m;
        float t = m - idx;
        t = t * t * (3.f - 2.f * t);

        float w0 = 0.f, w1 = 0.f;
        switch (idx) {
            case 0: w0 = sine;  w1 = tri;    break;
            case 1: w0 = tri;   w1 = saw;    break;
            case 2: w0 = saw;   w1 = sqr;    break;
            case 3: w0 = sqr;   w1 = scurve; break;
            default: w0 = scurve; w1 = scurve; break;
        }

        float wave = w0 * (1.f - t) + w1 * t;
        float warp = morph * morph;

        float skew = lfoPhase + (warp * 0.25f * (lfoPhase < 0.5f ? -1.f : 1.f));
        skew = clamp(skew, 0.f, 1.f);

        float folded = sinf(skew * 2.f * PI * (1.f + warp * 1.5f));
        float shaped = tanhf(wave * (1.5f + warp * 2.f));

        float edmWave = shaped * 0.6f + folded * 0.4f;

        float fadeTime = 0.05f + params[LFO_FADE_PARAM].getValue() * 0.95f;
        if (params[LFO_ENABLE_PARAM].getValue() > 0.5f)
            lfoFade += dt / fadeTime;
        else
            lfoFade -= dt / 0.05f;

        lfoFade = clamp(lfoFade, 0.f, 1.f);

        float lfoModRaw = edmWave * lfoFade;

        float dcCoeff = 0.9995f;
        lfoDC = lfoDC * dcCoeff + lfoModRaw * (1.f - dcCoeff);
        float lfoNoDC = lfoModRaw - lfoDC;

        float smoothCoeff = 0.9975f;
        lfoSmooth = lfoSmooth * smoothCoeff + lfoNoDC * (1.f - smoothCoeff);

        float lfoModUltra = lfoSmooth;

        float lfo2Freq = lfoFreq * (1.5f + morph * 2.f);
        lfo2Phase += lfo2Freq * dt;
        if (lfo2Phase >= 1.f)
            lfo2Phase -= 1.f;

        float lfo2 = sinf(lfo2Phase * 2.f * PI);
        lfoModUltra *= (1.f + lfo2 * 0.25f);

        if (params[LFO_SYNC_PARAM].getValue() > 0.5f) {
            float jitterAmt = morph * 0.015f;
            lfoModUltra += (random::uniform() - 0.5f) * jitterAmt;
        }

        float fmWobble = lfoModUltra * params[FM_CV_PARAM].getValue() * 0.25f;

        float pwmRaw = inputs[PWM_CV_INPUT].isConnected()
            ? inputs[PWM_CV_INPUT].getVoltage()
            : lfoModUltra * 2.f;

        smoothPWM = smoothPWM * 0.85f + pwmRaw * 0.15f;
        float pwmSource = tanhf(smoothPWM * 0.5f);

        float pwTarget =
            0.5f +
            params[PWM_CV_PARAM].getValue() * 0.4f +
            pwmSource * 0.15f;


        pwTarget = clamp(pwTarget, 0.15f, 0.85f);
        pwState = pwState * 0.995f + pwTarget * 0.005f;
        float pw = pwState;

        int voices = std::max(1, inputs[VOCT_INPUT].getChannels());
        outputs[TRI_OUTPUT].setChannels(voices);
        outputs[SQR_OUTPUT].setChannels(voices);
        outputs[SAW_OUTPUT].setChannels(voices);
        outputs[MIX_OUTPUT].setChannels(voices);
        outputs[EG_OUTPUT].setChannels(voices);

        float envFilter = 0.f;
        float triSum = 0.f;
        float sqrSum = 0.f;
        float sawSum = 0.f;
        float mixSum = 0.f;

        float coarse = params[FREQ_PARAM].getValue();
        float fine   = params[FINE_PARAM].getValue();
        float fm     = inputs[FM_CV_INPUT].isConnected()
            ? clamp(inputs[FM_CV_INPUT].getVoltage() / 10.f, -1.f, 1.f) * params[FM_CV_PARAM].getValue()
            : 0.f;
        for (int v = 0; v < voices; ++v) {

            float voct = inputs[VOCT_INPUT].isConnected()
                ? clamp(inputs[VOCT_INPUT].getPolyVoltage(v), -5.f, 5.f)
                : 0.f;

            float coarseClamped = coarse * 0.4166f;
            coarseClamped = clamp(coarseClamped, -2.f - voct, 2.f - voct);

            float basePitch = clamp(voct + coarseClamped + fine, -5.f, 5.f);

            float fmLinear = fm + fmWobble;

            float gl = params[GLIDE_PARAM].getValue();
            float glShape = gl * gl;
            float glBase = 0.0008f + glShape * 0.35f;
            float glSag = 0.25f + 0.75f * fabsf(basePitch - glidePitchV[v]);
            float glTime = glBase * glSag;
            float glCoeff = std::exp(-dt / glTime);

            glidePitchV[v] = glidePitchV[v] * glCoeff + basePitch * (1.f - glCoeff);

            float freq = dsp::FREQ_C4 * std::exp2(glidePitchV[v]);
            freq += fmLinear * 50.f;
            freq = clamp(freq, 0.1f, 15000.f);

            float ampComp = 1.f / (1.f + powf(freq / 3000.f, 1.3f));

            auto updateDrift = [&](float& d, float& s) {
                d += s * dt;
                if (d > 5.f)  s = -fabsf(s);
                if (d < -5.f) s =  fabsf(s);
            };

            updateDrift(drift1[v], driftSpeed1[v]);
            updateDrift(drift2[v], driftSpeed2[v]);
            updateDrift(drift3[v], driftSpeed3[v]);
            updateDrift(drift4[v], driftSpeed4[v]);

            float d1 = drift1[v] * (1.f / 1200.f);
            float d2 = drift2[v] * (1.f / 1200.f);
            float d3 = drift3[v] * (1.f / 1200.f);
            float d4 = drift4[v] * (1.f / 1200.f);

            float spread = params[SPREAD_PARAM].getValue();
            float det1 = -0.012f * spread;
            float det2 = -0.004f * spread;
            float det3 = +0.004f * spread;
            float det4 = +0.012f * spread;

            float inc1 = (freq * std::exp2(det1)) * dt + d1 * dt;
            float inc2 = (freq * std::exp2(det2)) * dt + d2 * dt;
            float inc3 = (freq * std::exp2(det3)) * dt + d3 * dt;
            float inc4 = (freq * std::exp2(det4)) * dt + d4 * dt;

            oscPhase1[v] = fmodf(oscPhase1[v] + inc1, 1.f);
            oscPhase2[v] = fmodf(oscPhase2[v] + inc2, 1.f);
            oscPhase3[v] = fmodf(oscPhase3[v] + inc3, 1.f);
            oscPhase4[v] = fmodf(oscPhase4[v] + inc4, 1.f);


            auto triFn = [](float p) {
                float t = fabsf(p * 2.f - 1.f);
                return (1.f - t) * 2.f - 1.f;
            };

            auto sawFn = [](float p) {
                return p * 2.f - 1.f;
            };
auto sqrFn = [&](float p) {
    return (p < pw ? 1.f : -1.f);
};

// ------------------------------------------------------------
// OSCILLATOR COUNT (1–4) — START
// ------------------------------------------------------------

// TRIANGLE MIX
float triMix = 0.f;
if (oscCount >= 1) triMix += triFn(oscPhase1[v]);
if (oscCount >= 2) triMix += triFn(oscPhase2[v]);
if (oscCount >= 3) triMix += triFn(oscPhase3[v]);
if (oscCount >= 4) triMix += triFn(oscPhase4[v]);
triMix /= oscCount;

// SAW MIX
float sawMix = 0.f;
if (oscCount >= 1) sawMix += sawFn(oscPhase1[v]);
if (oscCount >= 2) sawMix += sawFn(oscPhase2[v]);
if (oscCount >= 3) sawMix += sawFn(oscPhase3[v]);
if (oscCount >= 4) sawMix += sawFn(oscPhase4[v]);
sawMix /= oscCount;

// SQUARE MIX
float sqrMix = 0.f;
if (oscCount >= 1) sqrMix += sqrFn(oscPhase1[v]);
if (oscCount >= 2) sqrMix += sqrFn(oscPhase2[v]);
if (oscCount >= 3) sqrMix += sqrFn(oscPhase3[v]);
if (oscCount >= 4) sqrMix += sqrFn(oscPhase4[v]);
sqrMix /= oscCount;

// SINE MIX
float sineV = 0.f;
if (oscCount >= 1) sineV += sinf(oscPhase1[v] * 2.f * PI);
if (oscCount >= 2) sineV += sinf(oscPhase2[v] * 2.f * PI);
if (oscCount >= 3) sineV += sinf(oscPhase3[v] * 2.f * PI);
if (oscCount >= 4) sineV += sinf(oscPhase4[v] * 2.f * PI);
sineV /= oscCount;
// ------------------------------------------------------------
// OSCILLATOR COUNT (1–4) — END
// ------------------------------------------------------------


            float subFreq = freq * 0.5f;
            subPhaseV[v] = fmodf(subPhaseV[v] + subFreq * dt, 1.f);

            float triSub = 4.f * fabsf(subPhaseV[v] - 0.5f) - 1.f;
            float sqSub = tanhf(triSub * 1.4f);
            float sineSub = sinf(subPhaseV[v] * 2.f * PI) * 0.30f;

            float prophetSub =
                triSub * 0.55f +
                sqSub * 0.30f +
                sineSub * 0.15f;

            prophetSub = tanhf(prophetSub * 1.2f);
            float sub = prophetSub * params[SUB_LEVEL_PARAM].getValue() * 0.6f;

            float modSource = inputs[MOD_CV_INPUT].isConnected()
                ? inputs[MOD_CV_INPUT].getVoltage()
                : lfoModUltra * 5.f;

            float modNorm = modSource * (0.8f * params[MOD_CV_PARAM].getValue());

            float bladeIn = inputs[BLADE_IN_INPUT].isConnected()
                ? inputs[BLADE_IN_INPUT].getVoltage() * 0.3f
                : 0.f;

            float modSafe = modNorm * 0.28f;
            float bladeRaw = (sawMix * 0.80f) + bladeIn + modSafe;

            bladeDC = bladeDC * 0.995f + bladeRaw * 0.005f;
            bladeRaw -= bladeDC;
            bladeRaw = clamp(bladeRaw, -1.2f, 1.2f);

            smoothBlade += (bladeRaw - smoothBlade) * 0.06f;

            float bladeShaped = tanhf(smoothBlade * 1.35f);
            bladeShaped *= 0.70f;
            bladeShaped *= ampComp;

            float blade = bladeShaped;
            bool useBlade = params[SAW_BLADE_PARAM].getValue() > 0.5f;
            float mainSaw = useBlade ? blade : sawMix;

            float triGain = params[SINE_PARAM].getValue() * 0.8f;
            float sawGain = params[SAW_PARAM].getValue() * 0.8f;
            float sqrGain = params[SQUARE_PARAM].getValue() * 0.8f;
            float subGain = params[SUB_LEVEL_PARAM].getValue() * 0.6f;

            triMix  = tanhf(triMix  * 1.1f);
            mainSaw = tanhf(mainSaw * 1.1f);
            sqrMix  = tanhf(sqrMix  * 1.1f);
            sub     = tanhf(sub     * 1.1f);

            float mix =
                triMix * triGain +
                mainSaw * sawGain +
                sqrMix * sqrGain +
                sub    * subGain;



            float c = expf(-2.f * PI * 8000.f * dt);
            mixLP[v] = mixLP[v] * c + mix * (1.f - c);
            float mixOutVoice = mixLP[v];

            float hf = clamp(freq / 8000.f, 0.f, 1.f);
            float hfDamp = 1.f - hf * 0.65f;
            mixOutVoice *= hfDamp;
            bool gateNow = false;
            if (inputs[EG_INPUT].isConnected()) {
                int egCh = std::min(v, inputs[EG_INPUT].getChannels() - 1);
                gateNow = inputs[EG_INPUT].getPolyVoltage(egCh) >= 1.f;
            }

            bool gateRise = gateNow && !gatePrev[v];


            bool gateFall = !gateNow && gatePrev[v];
            gatePrev[v] = gateNow;

            if (inputs[EG_INPUT].isConnected() && inputs[EG_INPUT].getChannels() > 1) {
                float eg = clamp(inputs[EG_INPUT].getPolyVoltage(v) / 10.f, 0.f, 1.f);
                envV[v] = eg;
                envStage[v] = gateNow ? ENV_SUSTAIN : ENV_RELEASE;
            }
            else {
                float A = params[A_PARAM].getValue();
                float D = params[D_PARAM].getValue();
                float S = params[SUSTAIN_PARAM].getValue();
                float R = params[R_PARAM].getValue();

                A = (A * 0.7f + A * A * 0.5f) * 2.6f + 0.003f;
                D = (D * 0.5f + D * D * 0.5f) * 2.9f + 0.015f;

                if (inputs[SUSTAIN_CV_INPUT].isConnected())
                    S = clamp(S + inputs[SUSTAIN_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);

                if (gateRise) envStage[v] = ENV_ATTACK;
                if (gateFall) envStage[v] = ENV_RELEASE;

                switch (envStage[v]) {
                    case ENV_IDLE:
                        if (gateNow) envStage[v] = ENV_ATTACK;
                        break;

                    case ENV_ATTACK: {
                        float coeff = (A > 0.f) ? std::exp(-dt / A) : 0.f;
                        envV[v] = envV[v] * coeff + (1.f - coeff);
                        if (envV[v] >= 0.999f) {
                            envV[v] = 1.f;
                            envStage[v] = ENV_DECAY;
                        }
                    } break;

                  case ENV_DECAY: {

    // ADR = 0, ADSR = 1
    bool adsrMode = params[SUSTAIN_MODE_PARAM].getValue() > 0.5f;

    // Sustain level (only active in ADSR mode)
    float S = adsrMode
        ? params[SUSTAIN_PARAM].getValue()
        : 0.f;

    // Sustain CV (only meaningful in ADSR)
    if (adsrMode && inputs[SUSTAIN_CV_INPUT].isConnected())
        S += clamp(inputs[SUSTAIN_CV_INPUT].getVoltage() / 10.f, -1.f, 1.f);

    S = clamp(S, 0.f, 1.f);

    // Decay coefficient
    float coeff = (D > 0.f) ? std::exp(-dt / D) : 0.f;

    // Move envelope toward sustain level (or zero in ADR)
    envV[v] = envV[v] * coeff + S * (1.f - coeff);

    if (adsrMode) {
        // ADSR → enter sustain when reaching sustain level
        if (envV[v] <= S + 0.01f)
            envStage[v] = ENV_SUSTAIN;
    }
    else {
        // ADR → no sustain stage, decay straight to zero
        if (envV[v] <= 0.01f)
            envStage[v] = gateNow ? ENV_ATTACK : ENV_IDLE;
    }

} break;

case ENV_SUSTAIN:
    if (!(params[SUSTAIN_MODE_PARAM].getValue() > 0.5f)) {
        // ADR mode should never be in sustain
        envStage[v] = gateNow ? ENV_ATTACK : ENV_IDLE;
    }
    else if (!gateNow) {
        envStage[v] = ENV_RELEASE;
    }
    break;

case ENV_RELEASE: {
    float coeff = (R > 0.f) ? std::exp(-dt / R) : 0.f;
    envV[v] = envV[v] * coeff;

    // When envelope is basically silent, exit RELEASE
    if (envV[v] <= 0.0005f) {
        envV[v] = 0.f;

        if (gateNow) {
            // New gate → restart attack
            envStage[v] = ENV_ATTACK;
        } else {
            // No gate → fully idle
            envStage[v] = ENV_IDLE;
        }
    }
} break;

                }
            }

            envV[v] = clamp(envV[v], 0.f, 1.f);
            envFilter += envV[v];

            triSum += triMix;
            sqrSum += sqrMix;
            sawSum += mainSaw;
            mixSum += mixOutVoice;

            outputs[EG_OUTPUT].setVoltage(v, envV[v] * 10.f);
            outputs[SINE_OUT].setVoltage(sineV * 5.f);
        }

        float invVoices = 1.f / float(voices);

        envFilter *= invVoices;

        float triOut = triSum * invVoices;
        float sqrOut = sqrSum * invVoices;
        float sawOut = sawSum * invVoices;
        float mixOut = mixSum * invVoices;
float filterIn = 0.f;
if (inputs[VCA_IN_INPUT].isConnected())
    filterIn = inputs[VCA_IN_INPUT].getVoltage();
else if (inputs[FLT_INPUT].isConnected())
    filterIn = inputs[FLT_INPUT].getVoltage();
else
    filterIn = mixOut;

// final filter output (always defined)
float fltOut = 0.f;

// --- BASE CUTOFF ---
float cutoffKnob = clamp(params[CUTOFF_PARAM].getValue(), 0.f, 1.f);
float envAmt     = params[ENV_AMT_PARAM].getValue();
float cutCvAmt   = params[CUT_CV_PARAM].getValue();

float envMod = powf(envFilter, 0.6f) * envAmt;

float cutCvIn = inputs[CUT_CV_INPUT].isConnected()
    ? clamp(inputs[CUT_CV_INPUT].getVoltage() / 10.f, -1.f, 1.f)
    : 0.f;

float cutCvMod = cutCvIn * cutCvAmt;

float rawCutoff =
    cutoffKnob +
    envMod +
    cutCvMod +
    ACC * 0.12f;     // slightly reduced accent influence

float keyAmt = params[KEYTRACK_PARAM].getValue();   // -1..1

float keyCv = inputs[KEYTRACK_INPUT].isConnected()
    ? clamp(inputs[KEYTRACK_INPUT].getVoltage() / 10.f, -1.f, 1.f)
    : 0.f;

// Bias + CV modulation
float keyMod = keyAmt * 0.25f + keyCv * keyAmt * 0.5f;

// Apply to cutoff
rawCutoff += keyMod;

rawCutoff = clamp(rawCutoff, 0.f, 1.f);

// --- RESONANCE (SAFE) ---
float resKnob = params[RES_PARAM].getValue();
float resNorm = powf(resKnob, 1.6f);     // softer curve
resNorm = clamp(resNorm, 0.f, 0.85f);    // HARD LIMIT: cannot explode

// --- HARD CLOSE ---
if (rawCutoff <= 0.0001f) {
    s1 = s2 = s3 = 0.f;
    fltOut = 0.f;
}
else {
    // smooth musical cutoff curve
    float cutoffMod = rawCutoff * rawCutoff * rawCutoff;

    // gentle tilt (internal)
    float tilt = (cutoffMod > 0.02f) ? (1.f - cutoffMod) * 0.12f : 0.f;
    filterIn *= (1.f + tilt);

    // soft bump (internal)
    if (cutoffMod > 0.02f) {
        float bump = tanhf(filterIn * 0.7f);
        filterIn = filterIn * 0.92f + bump * 0.08f;
    }

    // pre-saturation (internal)
    float preSat = tanhf(filterIn * (1.2f + resNorm * 0.4f));
    filterIn = preSat * (0.85f + resNorm * 0.1f);

    // cutoff in Hz
    float fc = 60.f + cutoffMod * cutoffMod * 18000.f;
    fc = clamp(fc, 20.f, 18000.f);

    // stable one-pole coefficient
    float g = 1.f - std::exp(-2.f * float(M_PI) * fc * dt);

    // SAFE resonance feedback
    float k = resNorm * 1.35f;   // reduced from 1.6f

    float y3 = s3;
    float fb = k * y3;
    float x = filterIn - fb;

    float y1 = s1 + g * (x  - s1);
    float y2 = s2 + g * (y1 - s2);
    y3       = s3 + g * (y2 - s3);

    s1 = y1;
    s2 = y2;
    s3 = y3;

    // gentle post-tilt
    float postTilt = cutoffMod * 0.10f;
    float lowMix   = y3 * (1.f + postTilt);
    float highMix  = (filterIn - y3) * cutoffMod * 0.35f;

    fltOut = lowMix + highMix;

    // output soft clip
    fltOut = tanhf(fltOut * (1.15f + resNorm * 0.25f));

    // SAFE resonance brightener
    fltOut += (fltOut * resNorm * 0.08f);
}

        float vcaEnv = envFilter;
        float vcaLevel = params[VCA_OUT_LEVEL_PARAM].getValue();

        float vcaCV = inputs[VCA_CV_INPUT].isConnected()
            ? clamp(inputs[VCA_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f)
            : 1.f;

        float accentGain = 1.f + ACC * 0.6f;
        float vcaGain = vcaLevel * vcaCV * accentGain;

        float vcaOut = fltOut * vcaEnv * vcaGain;
        vcaOut = warmth(vcaOut);

        outputs[TRI_OUTPUT].setVoltage(triOut * 5.f);
        outputs[SQR_OUTPUT].setVoltage(sqrOut * 5.f);
        outputs[SAW_OUTPUT].setVoltage(sawOut * 5.f);
        outputs[MIX_OUTPUT].setVoltage(mixOut * params[MIX_OUT_LEVEL_PARAM].getValue() * 5.f);
        outputs[FLT_OUTPUT].setVoltage(fltOut * 5.f);
        outputs[VCA_OUT_OUTPUT].setVoltage(vcaOut * 5.f);
outputs[STEREO_L_OUTPUT].setVoltage(vcaOut * 5.f);
outputs[STEREO_R_OUTPUT].setVoltage(vcaOut * 5.f);

        outputs[LFO_TRI_OUTPUT].setVoltage(lfoTriRaw);
        outputs[LFO_SQR_OUTPUT].setVoltage(lfoSqrRaw);
        outputs[CLOCK_OUT_OUTPUT].setVoltage(clockTrig ? 10.f : 0.f);

        lights[FREQ_LIGHT].setBrightness(fabsf(coarse) * 0.8f);
        lights[FINE_LIGHT].setBrightness(fabsf(fine) * 0.8f);
        lights[FM_CV_LIGHT].setBrightness(params[FM_CV_PARAM].getValue());
        lights[PWM_CV_LIGHT].setBrightness(params[PWM_CV_PARAM].getValue());

        lights[CUT_CV_LIGHT].setBrightness(params[CUT_CV_PARAM].getValue());
        lights[CUTOFF_LIGHT].setBrightness(rawCutoff);
        lights[RES_LIGHT].setBrightness(resKnob);
        lights[LFO_RATE_LIGHT].setBrightness(lfoRateKnob);

        bool anyA = false, anyD = false, anyS = false, anyR = false;
        for (int v = 0; v < voices; ++v) {
            switch (envStage[v]) {
                case ENV_ATTACK:  anyA = true; break;
                case ENV_DECAY:   anyD = true; break;
                case ENV_SUSTAIN: anyS = true; break;
                case ENV_RELEASE: anyR = true; break;
                default: break;
            }
        }

        lights[A_LIGHT].setBrightness(anyA ? 1.f : 0.f);
        lights[D_LIGHT].setBrightness(anyD ? 1.f : 0.f);
        lights[SUSTAIN_LIGHT].setBrightness(anyS ? 1.f : 0.f);
        lights[R_LIGHT].setBrightness(anyR ? 1.f : 0.f);

        lights[GLIDE_LIGHT].setBrightness(params[GLIDE_PARAM].getValue());

        lights[SINE_LIGHT].setBrightness(params[SINE_PARAM].getValue());
        lights[SAW_LIGHT].setBrightness(params[SAW_PARAM].getValue());
        lights[SQUARE_LIGHT].setBrightness(params[SQUARE_PARAM].getValue());

        lights[VCA_OUT_LEVEL_LIGHT].setBrightness(params[VCA_OUT_LEVEL_PARAM].getValue());
        lights[VCA_OUT_OUTPUT_LIGHT].setBrightness(clamp(fabsf(vcaOut) / 5.f, 0.f, 1.f));
        lights[FLT_OUTPUT_LIGHT].setBrightness(clamp(fabsf(fltOut) / 5.f, 0.f, 1.f));

        int voiceSel = (int)std::round(params[VOICE_SELECT_PARAM].getValue());
        voiceSel = clamp(voiceSel, 0, 3);
        lights[VOICE1_LIGHT].setBrightness(voiceSel == 0 ? 1.f : 0.f);
        lights[VOICE2_LIGHT].setBrightness(voiceSel == 1 ? 1.f : 0.f);
        lights[VOICE3_LIGHT].setBrightness(voiceSel == 2 ? 1.f : 0.f);
        lights[VOICE4_LIGHT].setBrightness(voiceSel == 3 ? 1.f : 0.f);

        // stage activity (original behaviour) coloured by the knob value; short stages (e.g. decay) hold ~0.15 s so they stay visible
        {
            float sv = paramQuantities[SUSTAIN_PARAM]->getScaledValue();
            if (inputs[SUSTAIN_CV_INPUT].isConnected())
                sv = clamp(sv + inputs[SUSTAIN_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
            float tgt[4] = { lights[A_LIGHT].getBrightness(), lights[D_LIGHT].getBrightness(),
                             lights[SUSTAIN_LIGHT].getBrightness(), lights[R_LIGHT].getBrightness() };
            for (int k = 0; k < 4; k++)
                stageLvl[k] = std::max(tgt[k], stageLvl[k] - args.sampleTime / 0.15f);
            setActParamLED(ADSR_LED + 0 * 3, paramQuantities[A_PARAM]->getScaledValue(), stageLvl[0]);
            setActParamLED(ADSR_LED + 1 * 3, paramQuantities[D_PARAM]->getScaledValue(), stageLvl[1]);
            setActParamLED(ADSR_LED + 2 * 3, sv, stageLvl[2]);
            setActParamLED(ADSR_LED + 3 * 3, paramQuantities[R_PARAM]->getScaledValue(), stageLvl[3]);
        }
        {   // filter knob value LEDs (cutoff includes env, CV voltage and accent)
            setParamLED(FLT_LED + 0 * 3, paramQuantities[CUT_CV_PARAM]->getScaledValue());
            setParamLED(FLT_LED + 1 * 3, rawCutoff);
            setParamLED(FLT_LED + 2 * 3, paramQuantities[RES_PARAM]->getScaledValue());
            setParamLED(FLT_LED + 3 * 3, paramQuantities[LFO_RATE_PARAM]->getScaledValue());
        }
        localBusMain.L[0] = vcaOut;
        localBusMain.R[0] = vcaOut;
    }
};
// extra-small LED
template <typename TBase>
struct MicroLight : TinyLight<TBase> {
    MicroLight() { this->box.size = mm2px(Vec(0.9f, 0.9f)); }
};

struct ClockDisplay : TransparentWidget {
    SynTHBloK* module;

    ClockDisplay(SynTHBloK* m) {
        module = m;
    }

    void draw(const DrawArgs& args) override {
        NVGcontext* vg = args.vg;
        float w = box.size.x, h = box.size.y;

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0.f, 0.f, w, h, 2.f);
        nvgFillColor(vg, nvgRGBA(8, 6, 4, 255));
        nvgFill(vg);
        nvgStrokeWidth(vg, 0.8f);
        nvgStrokeColor(vg, nvgRGBA(150, 95, 25, 200));
        nvgStroke(vg);

        bool ext = module && module->inputs[SynTHBloK::CLOCK_IN_INPUT].isConnected();
        float bpm = 100.f;
        if (module) {
            if (ext) bpm = module->extBpm;
            else bpm = 60.f * (0.5f + module->params[SynTHBloK::CLOCK_RATE_PARAM].getValue() * 9.5f);
        }

        char text[16];
        if (ext && bpm < 1.f) snprintf(text, sizeof(text), "EXT");
        else snprintf(text, sizeof(text), "%.1f", bpm);

        nvgFontSize(vg, 7.f);
        nvgFillColor(vg, ext ? nvgRGBA(120, 220, 255, 255) : nvgRGBA(255, 170, 40, 255));
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, 3.f, h * 0.5f, text, NULL);

        nvgFontSize(vg, 4.f);
        nvgFillColor(vg, nvgRGBA(150, 100, 40, 255));
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(vg, w - 2.5f, h * 0.5f + 0.5f, ext ? "EXT" : "BPM", NULL);
    }
};

// dims the rate knob while an external clock is patched
struct ClockKnobDim : TransparentWidget {
    SynTHBloK* module;
    ClockKnobDim(SynTHBloK* m) { module = m; }
    void draw(const DrawArgs& args) override {
        if (!module || !module->inputs[SynTHBloK::CLOCK_IN_INPUT].isConnected()) return;
        NVGcontext* vg = args.vg;
        nvgBeginPath(vg);
        nvgCircle(vg, box.size.x * 0.5f, box.size.y * 0.5f, box.size.x * 0.5f);
        nvgFillColor(vg, nvgRGBA(10, 10, 10, 170));
        nvgFill(vg);
    }
};
// --- Main Voice Count OLED Display ---
struct MainVoiceCountDisplay : TransparentWidget {
    SynTHBloK* module;

    MainVoiceCountDisplay(SynTHBloK* m) {
        module = m;
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        int voices = module->voiceSetting + 1;   // 1–4
        NVGcontext* vg = args.vg;

        // Background gradient
        NVGcolor bgTop    = nvgRGBA(10, 10, 10, 255);
        NVGcolor bgBottom = nvgRGBA(0, 0, 0, 255);
        NVGpaint bg = nvgLinearGradient(vg, 0, 0, 0, box.size.y, bgTop, bgBottom);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgFillPaint(vg, bg);
        nvgFill(vg);

        // Border
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgStrokeColor(vg, nvgRGBA(255, 180, 90, 80));
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);

        // Text
        nvgFontSize(vg, 8.f);
        nvgFontFaceId(vg, 0);
        nvgFillColor(vg, nvgRGBA(255, 240, 180, 255));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        char buf[16];
        snprintf(buf, sizeof(buf), "%dV", voices);

        nvgText(vg, box.size.x * 0.5f, box.size.y * 0.5f, buf, NULL);

        // Scanlines
        for (int y = 1; y < box.size.y; y += 2) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, 1, y);
            nvgLineTo(vg, box.size.x - 1, y);
            nvgStrokeColor(vg, nvgRGBA(255, 200, 120, 18));
            nvgStrokeWidth(vg, 0.4f);
            nvgStroke(vg);
        }
    }
};


struct SynTHBloKWidget : ModuleWidget {
    SynTHBloKWidget(SynTHBloK* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/SynTHBloK.svg")));

        using namespace rack::componentlibrary;

        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(138.62f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 125.0f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(138.62f, 125.0f))));

        auto mm = [](float x, float y) { return mm2px(Vec(x, y)); };

  addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(15.00f, 32.00f)),module,SynTHBloK::VOICE_BUTTON_LIGHT));;
addParam(createParamCentered<TL1105>(mm2px(Vec(15.0f, 32.0f)), module, SynTHBloK::VOICE_SELECT_PARAM));
auto* vdisp = new MainVoiceCountDisplay(module);
vdisp->box.pos = mm2px(Vec(8.00f, 30.00f));
vdisp->box.size = Vec(10.f, 10.f);
addChild(vdisp);

        addParam(createParamCentered<RoundBlackKnob>(mm(26.f, 32.f), module, SynTHBloK::FREQ_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm(46.f, 32.f), module, SynTHBloK::FINE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm(66.f, 32.f), module, SynTHBloK::FM_CV_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm(86.f, 32.f), module, SynTHBloK::PWM_CV_PARAM));
        addParam(createParamCentered<CKSS>(mm(102.f, 94.f), module, SynTHBloK::LFO_ENABLE_PARAM));

        addParam(createParamCentered<Trimpot>(mm(16.f, 52.f), module, SynTHBloK::ENV_AMT_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm(26.f, 52.f), module, SynTHBloK::CUT_CV_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm(46.f, 52.f), module, SynTHBloK::CUTOFF_PARAM));

        addParam(createParamCentered<CKSS>(mm(102.f, 31.f), module, SynTHBloK::ACCENT_ENABLE_PARAM));

        addParam(createParamCentered<RoundBlackKnob>(mm(66.f, 52.f), module, SynTHBloK::RES_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm(86.f, 52.f), module, SynTHBloK::LFO_RATE_PARAM));

        addParam(createParamCentered<Trimpot>(mm(76.f, 57.f), module, SynTHBloK::ACCENT_PARAM));
        addInput(createInputCentered<BlackJack>(mm(76.f, 50.f), module, SynTHBloK::ACCENT_INPUT));

        addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm2px(Vec(26.f, 60.2f)), module, SynTHBloK::FLT_LED + 0 * 3));
        addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm2px(Vec(46.f, 60.2f)), module, SynTHBloK::FLT_LED + 1 * 3));
        addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm2px(Vec(66.f, 60.2f)), module, SynTHBloK::FLT_LED + 2 * 3));
        addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm2px(Vec(86.f, 60.2f)), module, SynTHBloK::FLT_LED + 3 * 3));
        addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm2px(Vec(26, 80)), module, SynTHBloK::ADSR_LED + 0 * 3));
        addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm2px(Vec(46, 80)), module, SynTHBloK::ADSR_LED + 1 * 3));
        addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm2px(Vec(66, 80)), module, SynTHBloK::ADSR_LED + 3 * 3));
        addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm2px(Vec(102, 80)), module, SynTHBloK::ADSR_LED + 2 * 3));

        addParam(createParamCentered<RoundBlackKnob>(mm(26.f, 72.f), module, SynTHBloK::A_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm(46.f, 72.f), module, SynTHBloK::D_PARAM));
        addParam(createParamCentered<Trimpot>(mm(56.f, 77.f), module, SynTHBloK::SUSTAIN_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm(66.f, 72.f), module, SynTHBloK::R_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm(86.f, 72.f), module, SynTHBloK::GLIDE_PARAM));
        addInput(createInputCentered<BlackJack>(mm(56.f, 70.f), module, SynTHBloK::SUSTAIN_CV_INPUT));

        addParam(createParamCentered<RoundBlackKnob>(mm(26.f, 94.f), module, SynTHBloK::SINE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm(46.f, 94.f), module, SynTHBloK::SAW_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm(66.f, 94.f), module, SynTHBloK::SQUARE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm(86.f, 94.f), module, SynTHBloK::VCA_OUT_LEVEL_PARAM));

        addOutput(createOutputCentered<BlackJack>(mm(15.f, 94.f), module, SynTHBloK::SINE_OUT));

        addParam(createParamCentered<CKSS>(mm(102.f, 52.f), module, SynTHBloK::LFO_RANGE_PARAM));
        addParam(createParamCentered<CKSS>(mm(102.f, 72.f), module, SynTHBloK::SUSTAIN_MODE_PARAM));

        addInput(createInputCentered<BlackJack>(mm(118.f, 26.f), module, SynTHBloK::VOCT_INPUT));
        addInput(createInputCentered<BlackJack>(mm(132.f, 26.f), module, SynTHBloK::PWM_CV_INPUT));

        addInput(createInputCentered<BlackJack>(mm(118.f, 36.f), module, SynTHBloK::RESET_INPUT));
        addInput(createInputCentered<BlackJack>(mm(132.f, 36.f), module, SynTHBloK::FM_CV_INPUT));

        addOutput(createOutputCentered<BlackJack>(mm(118.f, 46.f), module, SynTHBloK::TRI_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm(132.f, 46.f), module, SynTHBloK::SQR_OUTPUT));

        addOutput(createOutputCentered<BlackJack>(mm(118.f, 56.f), module, SynTHBloK::SAW_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm(132.f, 56.f), module, SynTHBloK::MIX_OUTPUT));

        addInput(createInputCentered<BlackJack>(mm(118.f, 66.f), module, SynTHBloK::CUT_CV_INPUT));
        addInput(createInputCentered<BlackJack>(mm(132.f, 66.f), module, SynTHBloK::EG_INPUT));

        addInput(createInputCentered<BlackJack>(mm(118.f, 76.f), module, SynTHBloK::FLT_INPUT));
        addOutput(createOutputCentered<BlackJack>(mm(132.f, 76.f), module, SynTHBloK::EG_OUTPUT));

        addOutput(createOutputCentered<BlackJack>(mm(118.f, 88.f), module, SynTHBloK::FLT_OUTPUT));
        addInput(createInputCentered<BlackJack>(mm(132.f, 88.f), module, SynTHBloK::VCA_CV_INPUT));

        addInput(createInputCentered<BlackJack>(mm(118.f, 100.f), module, SynTHBloK::VCA_IN_INPUT));
        addOutput(createOutputCentered<BlackJack>(mm(132.f, 100.f), module, SynTHBloK::VCA_OUT_OUTPUT));
            
        addParam(createParamCentered<CKSS>(mm(26.f, 105.f), module, SynTHBloK::SAW_BLADE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm(46.f, 105.f), module, SynTHBloK::MOD_CV_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm(66.f, 105.f), module, SynTHBloK::SUB_LEVEL_PARAM));

        addInput(createInputCentered<BlackJack>(mm(46.f, 117.f), module, SynTHBloK::MOD_CV_INPUT));
        addInput(createInputCentered<BlackJack>(mm(15.f, 105.f), module, SynTHBloK::BLADE_IN_INPUT));

        addParam(createParamCentered<RoundSmallBlackKnob>(mm(66.f, 117.f), module, SynTHBloK::MIX_OUT_LEVEL_PARAM));


        addOutput(createOutputCentered<BlackJack>(mm(118.f, 113.f), module, SynTHBloK::LFO_TRI_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm(132.f, 113.f), module, SynTHBloK::LFO_SQR_OUTPUT));

        addParam(createParamCentered<Trimpot>(mm(102.f, 108.f), module, SynTHBloK::LFO_FADE_PARAM));
        addParam(createParamCentered<CKSS>(mm(90.f, 108.f), module, SynTHBloK::LFO_RETRIG_PARAM));
        addParam(createParamCentered<CKSS>(mm(90.f, 118.f), module, SynTHBloK::LFO_SYNC_PARAM));
        addParam(createParamCentered<Trimpot>(mm(102.f, 118.f), module, SynTHBloK::LFO_MORPH_PARAM));

        addInput(createInputCentered<BlackJack>(mm(118.f, 12.8f), module, SynTHBloK::CLOCK_IN_INPUT));
        addOutput(createOutputCentered<BlackJack>(mm(132.f, 12.8f), module, SynTHBloK::CLOCK_OUT_OUTPUT));
        addParam(createParamCentered<Trimpot>(mm(104.5f, 12.8f), module, SynTHBloK::CLOCK_RATE_PARAM));
        {
            auto* dim = new ClockKnobDim(module);
            dim->box.size = mm(7.2f, 7.2f);
            dim->box.pos = mm(104.5f - 3.6f, 12.8f - 3.6f);
            addChild(dim);
            auto* cd = new ClockDisplay(module);
            cd->box.size = mm(11.f, 4.6f);
            cd->box.pos = mm(88.8f, 12.8f - 2.3f);
            addChild(cd);
        }
        addOutput(createOutput<BlackJack>(mm2px(Vec(10.f, 112.f)), module, SynTHBloK::STEREO_L_OUTPUT));
        addOutput(createOutput<BlackJack>(mm2px(Vec(21.f, 112.f)), module, SynTHBloK::STEREO_R_OUTPUT));
addParam(createParamCentered<Trimpot>(mm(56.f, 57.f), module, SynTHBloK::KEYTRACK_PARAM));
addInput(createInputCentered<BlackJack>(mm(56.f, 50.f), module, SynTHBloK::KEYTRACK_INPUT));


    }
};

Model* modelSynTHBloK = createModel<SynTHBloK, SynTHBloKWidget>("SynTHBloK");
