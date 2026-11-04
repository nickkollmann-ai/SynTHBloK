#pragma once
// SynTHBloK-QUAD: the module (engine) and its panel controls, shared by SynTHBloK-QUAD and MTX QUAD.
#include "plugin.hpp"

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
        DRIVE_PARAM,
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
    double lastClockTime = -1.0;   // < 0: no CLOCK IN pulse seen yet, so nothing to measure from
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
    dsp::SchmittTrigger clockIn;
    dsp::PulseGenerator clockOut;
    bool clockTrig = false;   // a clock pulse on this sample
    bool clockRan = false;    // runClock() already ran this sample (MTX QUAD runs it before the engine)
    bool clockHold = false;   // stopped (MTX QUAD's RUN off): no pulses, CLOCK OUT quiet; CLOCK IN is still measured

    float clockRate() { return 0.5f + params[CLOCK_RATE_PARAM].getValue() * 9.5f; }
    bool externalClock() { return inputs[CLOCK_IN_INPUT].isConnected(); }
    // seconds between clock pulses: measured at CLOCK IN, or set by the CLOCK knob
    float clockPeriod() {
        if (externalClock()) return extBpm > 0.f ? 60.f / extBpm : 0.5f;
        return 1.f / clockRate();
    }
    // The clock: a pulse per beat, from CLOCK IN (its rate measured for the display and LFO sync) or the
    // CLOCK knob (0.5..10 Hz). CLOCK OUT gets a 1 ms trigger on every pulse.
    void runClock(float dt) {
        clockRan = true;
        clockTrig = false;
        sampleCounter += dt;
        if (externalClock()) {
            if (clockIn.process(inputs[CLOCK_IN_INPUT].getVoltage(), 0.1f, 1.f)) {
                clockTrig = true;
                double d = sampleCounter - lastClockTime;
                if (lastClockTime >= 0.0 && d > 0.0001 && d < 10.0) extBpm = 60.0 / d;
                lastClockTime = sampleCounter;
            }
            clockPhase = 0.f;
        } else {
            lastClockTime = -1.0;   // a fresh patch measures from its own first pulse
            extBpm = 0.f;           // and shows EXT until then, not the last cable's tempo
            if (!clockHold) clockPhase += clockRate() * dt;
            if (clockPhase >= 1.f && !clockHold) {
                clockPhase -= 1.f;
                clockTrig = true;
            }
        }
        if (clockHold) clockTrig = false;
        if (clockTrig) clockOut.trigger(1e-3f);
    }

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
    float smoothBlade[8] = {};    // per voice (MAX_VOICES), so the voices of a chord stay apart
    float bladeDC[8] = {};
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

    float ladder[4] = {};          // the 4-pole filter's stages
    float ladderIn = 0.f;          // last input, for the 2x oversampling

    // MTX QUAD's sequencer: while on, it plays the synth in place of an unpatched 1V/O IN (pitch, up to 4
    // notes) and EG IN (gates), and adds its step's accent, cutoff, resonance and glide.
    struct SeqFeed {
        bool on = false;
        int ch = 1;
        float voct[4] = {};
        bool gate[4] = {};
        float acc = 0.f, cut = 0.f, res = 0.f, glide = 0.f;
    } seq;

    // xp / xi / xo / xl: room for a module built on this one (MTX QUAD) to add its own after the QUAD's
    SynTHBloK(int xp = 0, int xi = 0, int xo = 0, int xl = 0) {
        config(PARAMS_LEN + xp, INPUTS_LEN + xi, OUTPUTS_LEN + xo, LIGHTS_LEN + xl);
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
        configParam(SPREAD_PARAM, 0.f, 1.f, kSpreadDefault, "Unison spread", " cents", 0.f, 28.f);
        configParam(DRIVE_PARAM, 0.f, 1.f, 0.3f, "Filter drive", "%", 0.f, 100.f);

        for (int v = 0; v < MAX_VOICES; ++v) {
            driftSpeed1[v] = (random::uniform() - 0.5f) * 0.02f;
            driftSpeed2[v] = (random::uniform() - 0.5f) * 0.02f;
            driftSpeed3[v] = (random::uniform() - 0.5f) * 0.02f;
            driftSpeed4[v] = (random::uniform() - 0.5f) * 0.02f;
            // free-running unison: the oscillators start out of phase, as on an analog voice
            oscPhase1[v] = random::uniform();
            oscPhase2[v] = random::uniform();
            oscPhase3[v] = random::uniform();
            oscPhase4[v] = random::uniform();
        }
    }
    static constexpr float kSpreadDefault = 0.35f;

    // PolyBLEP: takes the step out of a saw / square edge (no aliasing grit); t = phase, dt = phase step
    static inline float polyBlep(float t, float dt) {
        if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
        if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
        return 0.f;
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

    // the oscillator count (the 1V..4V button) is not a param, so it is saved here
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "oscCount", json_integer(voiceSetting + 1));
        json_object_set_new(root, "spreadLive", json_true());
        return root;
    }
    void dataFromJson(json_t* root) override {
        if (json_t* j = json_object_get(root, "oscCount")) voiceSetting = clamp((int)json_integer_value(j), 1, 4) - 1;
        // before Unison spread worked, patches saved it as an unused 0: give them the default
        if (!json_object_get(root, "spreadLive")) params[SPREAD_PARAM].setValue(kSpreadDefault);
    }

    void process(const ProcessArgs& args) override {


        float dt = args.sampleTime;

        // the clock keeps running (CLOCK OUT too) while nothing is patched
        if (!clockRan) runClock(dt);
        clockRan = false;
        outputs[CLOCK_OUT_OUTPUT].setVoltage(clockOut.process(dt) ? 10.f : 0.f);

        bool isolated =
            !inputs[VOCT_INPUT].isConnected() &&
            !inputs[EG_INPUT].isConnected() &&
            !inputs[FLT_INPUT].isConnected() &&
            !inputs[VCA_IN_INPUT].isConnected() &&
            !seq.on;

        if (isolated) {
            outputs[TRI_OUTPUT].setVoltage(0.f);
            outputs[SQR_OUTPUT].setVoltage(0.f);
            outputs[SAW_OUTPUT].setVoltage(0.f);
            outputs[MIX_OUTPUT].setVoltage(0.f);
            outputs[EG_OUTPUT].setVoltage(0.f);
            outputs[FLT_OUTPUT].setVoltage(0.f);
            outputs[VCA_OUT_OUTPUT].setVoltage(0.f);
            outputs[STEREO_L_OUTPUT].setVoltage(0.f);
            outputs[STEREO_R_OUTPUT].setVoltage(0.f);
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

        float accentRaw = accentEnabled ? (accentKnob + accentCV + (seq.on ? seq.acc : 0.f)) : 0.f;
        accentRaw = clamp(accentRaw, 0.f, 1.f);
        accentSmooth = accentSmooth * 0.9f + accentRaw * 0.1f;
        float ACC = accentSmooth;

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

            lfoFreq = div / clockPeriod();   // the patched clock, or the CLOCK knob's tempo
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

        bool seqPitch = seq.on && !inputs[VOCT_INPUT].isConnected();
        bool seqGate = seq.on && !inputs[EG_INPUT].isConnected();
        int voices = seqPitch ? clamp(seq.ch, 1, 4) : std::max(1, inputs[VOCT_INPUT].getChannels());
        outputs[SINE_OUT].setChannels(voices);
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

            float voct = seqPitch ? clamp(seq.voct[std::min(v, 3)], -5.f, 5.f)
                : inputs[VOCT_INPUT].isConnected()
                ? clamp(inputs[VOCT_INPUT].getPolyVoltage(v), -5.f, 5.f)
                : 0.f;

            float coarseClamped = coarse * 0.4166f;
            coarseClamped = clamp(coarseClamped, -2.f - voct, 2.f - voct);

            float basePitch = clamp(voct + coarseClamped + fine, -5.f, 5.f);

            float fmLinear = fm + fmWobble;

            float gl = clamp(params[GLIDE_PARAM].getValue() + (seq.on ? seq.glide : 0.f), 0.f, 1.f);
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

            // OSCILLATORS: 1..4 per voice (the 1V..4V button), spread evenly over +-14 cents x Unison
            // spread, each wandering up to +-1.5 cents (analog drift); that beating is the hum.
            // Saw and square are PolyBLEP (clean edges, no aliasing grit up high).
            float spread = params[SPREAD_PARAM].getValue();
            float* oscPh[4] = {&oscPhase1[v], &oscPhase2[v], &oscPhase3[v], &oscPhase4[v]};
            float oscDrift[4] = {drift1[v], drift2[v], drift3[v], drift4[v]};
            float triMix = 0.f, sawMix = 0.f, sqrMix = 0.f, sineV = 0.f;
            for (int o = 0; o < oscCount; o++) {
                float pos = oscCount > 1 ? (2.f * o / (oscCount - 1) - 1.f) : 0.f;   // -1..+1 across the stack
                float cents = pos * spread * 14.f + oscDrift[o] * 0.3f;
                float inc = clamp(freq * std::exp2(cents * (1.f / 1200.f)) * dt, 0.f, 0.45f);
                float p = *oscPh[o] + inc;
                if (p >= 1.f) p -= 1.f;
                *oscPh[o] = p;
                triMix += 1.f - 4.f * fabsf(p - 0.5f);
                sawMix += 2.f * p - 1.f - polyBlep(p, inc);
                float q = p + 1.f - pw;
                if (q >= 1.f) q -= 1.f;
                sqrMix += (p < pw ? 1.f : -1.f) + polyBlep(p, inc) - polyBlep(q, inc);
                sineV += sinf(p * 2.f * PI);
            }
            float invOsc = 1.f / oscCount;
            triMix *= invOsc;
            sawMix *= invOsc;
            sqrMix *= invOsc;
            sineV *= invOsc;


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

            bladeDC[v] = bladeDC[v] * 0.995f + bladeRaw * 0.005f;
            bladeRaw -= bladeDC[v];
            bladeRaw = clamp(bladeRaw, -1.2f, 1.2f);

            smoothBlade[v] += (bladeRaw - smoothBlade[v]) * 0.06f;

            float bladeShaped = tanhf(smoothBlade[v] * 1.35f);
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



            float c = expf(-2.f * PI * 14000.f * dt);
            mixLP[v] = mixLP[v] * c + mix * (1.f - c);
            float mixOutVoice = mixLP[v];

            float hf = clamp(freq / 8000.f, 0.f, 1.f);
            float hfDamp = 1.f - hf * 0.65f;
            mixOutVoice *= hfDamp;
            bool gateNow = false;
            if (seqGate) gateNow = seq.gate[std::min(v, 3)];
            else if (inputs[EG_INPUT].isConnected()) {
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
            outputs[SINE_OUT].setVoltage(sineV * 5.f, v);
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
    ACC * 0.12f +    // slightly reduced accent influence
    (seq.on ? seq.cut : 0.f);

float keyAmt = params[KEYTRACK_PARAM].getValue();   // -1..1

float keyCv = inputs[KEYTRACK_INPUT].isConnected()
    ? clamp(inputs[KEYTRACK_INPUT].getVoltage() / 10.f, -1.f, 1.f)
    : 0.f;

// Bias + CV modulation
float keyMod = keyAmt * 0.25f + keyCv * keyAmt * 0.5f;

// Apply to cutoff
rawCutoff += keyMod;

rawCutoff = clamp(rawCutoff, 0.f, 1.f);

// --- FILTER: a driven 4-pole ladder, run at 2x the sample rate ---
// DRIVE pushes the mix into an asymmetric saturator (odd + a little even: the growl), accent pushes harder.
// RES: the feedback reaches self-oscillation near the top of the knob (the scream); the passband is
// compensated so the bass stays as RES goes up. Every stage saturates, so it can't run away.
float resKnob = clamp(params[RES_PARAM].getValue() + (seq.on ? seq.res : 0.f), 0.f, 1.f);
float k = 5.6f * powf(resKnob, 1.3f);

// --- HARD CLOSE ---
if (rawCutoff <= 0.0001f) {
    for (int i = 0; i < 4; i++) ladder[i] = 0.f;
    ladderIn = 0.f;
    fltOut = 0.f;
}
else {
    float drive = params[DRIVE_PARAM].getValue();
    float pre = 1.f + drive * drive * 9.f + ACC * 1.5f;
    float bias = 0.15f * drive;
    float x0 = tanhf(filterIn * pre + bias) - tanhf(bias);
    x0 += (random::uniform() - 0.5f) * 1e-4f;   // a breath of noise, so full RES can start ringing

    // cutoff in Hz: 20 Hz..20 kHz, an octave every tenth of the range
    float hdt = 0.5f * dt;
    float fc = clamp(20.f * std::exp2(rawCutoff * 10.f), 20.f, 0.4f / hdt);
    float g = 1.f - std::exp(-2.f * float(M_PI) * fc * hdt);
    float comp = 1.f + 0.6f * k;

    float sum = 0.f;
    for (int os = 0; os < 2; os++) {
        float xin = os == 0 ? 0.5f * (ladderIn + x0) : x0;
        float u = tanhf(xin * comp - k * ladder[3]);
        ladder[0] += g * (u - tanhf(ladder[0]));
        ladder[1] += g * (tanhf(ladder[0]) - tanhf(ladder[1]));
        ladder[2] += g * (tanhf(ladder[1]) - tanhf(ladder[2]));
        ladder[3] += g * (tanhf(ladder[2]) - tanhf(ladder[3]));
        sum += ladder[3];
    }
    ladderIn = x0;
    fltOut = tanhf(sum * 0.5f * 1.4f);
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

// Right-click menu sliders for the engine settings that have no knob on the panel.
struct QuadMenuSlider : ui::Slider {
    QuadMenuSlider(Quantity* q) { quantity = q; box.size.x = 220.f; }
};
inline void addQuadMenu(Menu* menu, SynTHBloK* module) {
    menu->addChild(new MenuSeparator);
    menu->addChild(createMenuLabel("Voice"));
    menu->addChild(new QuadMenuSlider(module->paramQuantities[SynTHBloK::DRIVE_PARAM]));
    menu->addChild(new QuadMenuSlider(module->paramQuantities[SynTHBloK::SPREAD_PARAM]));
}

// The QUAD's controls, ox mm to the right of the panel's left edge (screws and panel stay with the widget).
// essential: only CLOCK IN/OUT, RESET and the stereo out, in one column at x 117.5 (MTX QUAD's 41 HP panel).
inline void addQuadControls(ModuleWidget* w, SynTHBloK* module, float ox, bool essential = false) {
        using namespace rack::componentlibrary;
        auto mm = [=](float x, float y) { return mm2px(Vec(x + ox, y)); };

  w->addChild(createLightCentered<SmallLight<GreenLight>>(mm(15.00f, 32.00f),module,SynTHBloK::VOICE_BUTTON_LIGHT));;
w->addParam(createParamCentered<TL1105>(mm(15.0f, 32.0f), module, SynTHBloK::VOICE_SELECT_PARAM));
auto* vdisp = new MainVoiceCountDisplay(module);
vdisp->box.pos = mm(8.00f, 30.00f);
vdisp->box.size = Vec(10.f, 10.f);
w->addChild(vdisp);
        w->addParam(createParamCentered<RoundBlackKnob>(mm(26.f, 32.f), module, SynTHBloK::FREQ_PARAM));
        w->addParam(createParamCentered<RoundBlackKnob>(mm(46.f, 32.f), module, SynTHBloK::FINE_PARAM));
        w->addParam(createParamCentered<RoundBlackKnob>(mm(66.f, 32.f), module, SynTHBloK::FM_CV_PARAM));
        w->addParam(createParamCentered<RoundBlackKnob>(mm(86.f, 32.f), module, SynTHBloK::PWM_CV_PARAM));
        w->addParam(createParamCentered<CKSS>(mm(102.f, 94.f), module, SynTHBloK::LFO_ENABLE_PARAM));
        w->addParam(createParamCentered<Trimpot>(mm(16.f, 52.f), module, SynTHBloK::ENV_AMT_PARAM));
        w->addParam(createParamCentered<RoundBlackKnob>(mm(26.f, 52.f), module, SynTHBloK::CUT_CV_PARAM));
        w->addParam(createParamCentered<RoundBlackKnob>(mm(46.f, 52.f), module, SynTHBloK::CUTOFF_PARAM));
        w->addParam(createParamCentered<CKSS>(mm(102.f, 31.f), module, SynTHBloK::ACCENT_ENABLE_PARAM));
        w->addParam(createParamCentered<RoundBlackKnob>(mm(66.f, 52.f), module, SynTHBloK::RES_PARAM));
        w->addParam(createParamCentered<RoundBlackKnob>(mm(86.f, 52.f), module, SynTHBloK::LFO_RATE_PARAM));
        w->addParam(createParamCentered<Trimpot>(mm(76.f, 57.f), module, SynTHBloK::ACCENT_PARAM));
        if (!essential) w->addInput(createInputCentered<BlackJack>(mm(76.f, 50.f), module, SynTHBloK::ACCENT_INPUT));
        w->addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm(26.f, 60.2f), module, SynTHBloK::FLT_LED + 0 * 3));
        w->addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm(46.f, 60.2f), module, SynTHBloK::FLT_LED + 1 * 3));
        w->addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm(66.f, 60.2f), module, SynTHBloK::FLT_LED + 2 * 3));
        w->addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm(86.f, 60.2f), module, SynTHBloK::FLT_LED + 3 * 3));
        w->addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm(26, 80), module, SynTHBloK::ADSR_LED + 0 * 3));
        w->addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm(46, 80), module, SynTHBloK::ADSR_LED + 1 * 3));
        w->addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm(66, 80), module, SynTHBloK::ADSR_LED + 3 * 3));
        w->addChild(createLightCentered<MicroLight<RedGreenBlueLight>>(mm(102, 80), module, SynTHBloK::ADSR_LED + 2 * 3));
        w->addParam(createParamCentered<RoundBlackKnob>(mm(26.f, 72.f), module, SynTHBloK::A_PARAM));
        w->addParam(createParamCentered<RoundBlackKnob>(mm(46.f, 72.f), module, SynTHBloK::D_PARAM));
        w->addParam(createParamCentered<Trimpot>(mm(56.f, 77.f), module, SynTHBloK::SUSTAIN_PARAM));
        w->addParam(createParamCentered<RoundBlackKnob>(mm(66.f, 72.f), module, SynTHBloK::R_PARAM));
        w->addParam(createParamCentered<RoundBlackKnob>(mm(86.f, 72.f), module, SynTHBloK::GLIDE_PARAM));
        if (!essential) w->addInput(createInputCentered<BlackJack>(mm(56.f, 70.f), module, SynTHBloK::SUSTAIN_CV_INPUT));
        w->addParam(createParamCentered<RoundBlackKnob>(mm(26.f, 94.f), module, SynTHBloK::SINE_PARAM));
        w->addParam(createParamCentered<RoundBlackKnob>(mm(46.f, 94.f), module, SynTHBloK::SAW_PARAM));
        w->addParam(createParamCentered<RoundBlackKnob>(mm(66.f, 94.f), module, SynTHBloK::SQUARE_PARAM));
        w->addParam(createParamCentered<RoundBlackKnob>(mm(86.f, 94.f), module, SynTHBloK::VCA_OUT_LEVEL_PARAM));
        if (!essential) w->addOutput(createOutputCentered<BlackJack>(mm(15.f, 94.f), module, SynTHBloK::SINE_OUT));
        w->addParam(createParamCentered<CKSS>(mm(102.f, 52.f), module, SynTHBloK::LFO_RANGE_PARAM));
        w->addParam(createParamCentered<CKSS>(mm(102.f, 72.f), module, SynTHBloK::SUSTAIN_MODE_PARAM));
        if (!essential) {
        w->addInput(createInputCentered<BlackJack>(mm(118.f, 26.f), module, SynTHBloK::VOCT_INPUT));
        w->addInput(createInputCentered<BlackJack>(mm(132.f, 26.f), module, SynTHBloK::PWM_CV_INPUT));
        w->addInput(createInputCentered<BlackJack>(mm(118.f, 36.f), module, SynTHBloK::RESET_INPUT));
        w->addInput(createInputCentered<BlackJack>(mm(132.f, 36.f), module, SynTHBloK::FM_CV_INPUT));
        w->addOutput(createOutputCentered<BlackJack>(mm(118.f, 46.f), module, SynTHBloK::TRI_OUTPUT));
        w->addOutput(createOutputCentered<BlackJack>(mm(132.f, 46.f), module, SynTHBloK::SQR_OUTPUT));
        w->addOutput(createOutputCentered<BlackJack>(mm(118.f, 56.f), module, SynTHBloK::SAW_OUTPUT));
        w->addOutput(createOutputCentered<BlackJack>(mm(132.f, 56.f), module, SynTHBloK::MIX_OUTPUT));
        w->addInput(createInputCentered<BlackJack>(mm(118.f, 66.f), module, SynTHBloK::CUT_CV_INPUT));
        w->addInput(createInputCentered<BlackJack>(mm(132.f, 66.f), module, SynTHBloK::EG_INPUT));
        w->addInput(createInputCentered<BlackJack>(mm(118.f, 76.f), module, SynTHBloK::FLT_INPUT));
        w->addOutput(createOutputCentered<BlackJack>(mm(132.f, 76.f), module, SynTHBloK::EG_OUTPUT));
        w->addOutput(createOutputCentered<BlackJack>(mm(118.f, 88.f), module, SynTHBloK::FLT_OUTPUT));
        w->addInput(createInputCentered<BlackJack>(mm(132.f, 88.f), module, SynTHBloK::VCA_CV_INPUT));
        w->addInput(createInputCentered<BlackJack>(mm(118.f, 100.f), module, SynTHBloK::VCA_IN_INPUT));
        w->addOutput(createOutputCentered<BlackJack>(mm(132.f, 100.f), module, SynTHBloK::VCA_OUT_OUTPUT));
        }
        w->addParam(createParamCentered<CKSS>(mm(26.f, 105.f), module, SynTHBloK::SAW_BLADE_PARAM));
        w->addParam(createParamCentered<RoundSmallBlackKnob>(mm(46.f, 105.f), module, SynTHBloK::MOD_CV_PARAM));
        w->addParam(createParamCentered<RoundSmallBlackKnob>(mm(66.f, 105.f), module, SynTHBloK::SUB_LEVEL_PARAM));
        if (!essential) {
        w->addInput(createInputCentered<BlackJack>(mm(46.f, 117.f), module, SynTHBloK::MOD_CV_INPUT));
        w->addInput(createInputCentered<BlackJack>(mm(15.f, 105.f), module, SynTHBloK::BLADE_IN_INPUT));
        }
        w->addParam(createParamCentered<RoundSmallBlackKnob>(mm(66.f, 117.f), module, SynTHBloK::MIX_OUT_LEVEL_PARAM));
        if (!essential) {
        w->addOutput(createOutputCentered<BlackJack>(mm(118.f, 113.f), module, SynTHBloK::LFO_TRI_OUTPUT));
        w->addOutput(createOutputCentered<BlackJack>(mm(132.f, 113.f), module, SynTHBloK::LFO_SQR_OUTPUT));
        }
        w->addParam(createParamCentered<Trimpot>(mm(102.f, 108.f), module, SynTHBloK::LFO_FADE_PARAM));
        w->addParam(createParamCentered<CKSS>(mm(90.f, 108.f), module, SynTHBloK::LFO_RETRIG_PARAM));
        w->addParam(createParamCentered<CKSS>(mm(90.f, 118.f), module, SynTHBloK::LFO_SYNC_PARAM));
        w->addParam(createParamCentered<Trimpot>(mm(102.f, 118.f), module, SynTHBloK::LFO_MORPH_PARAM));
        if (!essential) {
        w->addInput(createInputCentered<BlackJack>(mm(118.f, 12.8f), module, SynTHBloK::CLOCK_IN_INPUT));
        w->addOutput(createOutputCentered<BlackJack>(mm(132.f, 12.8f), module, SynTHBloK::CLOCK_OUT_OUTPUT));
        }
        w->addParam(createParamCentered<Trimpot>(mm(104.5f, 12.8f), module, SynTHBloK::CLOCK_RATE_PARAM));
        {
            auto* dim = new ClockKnobDim(module);
            dim->box.size = mm2px(Vec(7.2f, 7.2f));
            dim->box.pos = mm(104.5f - 3.6f, 12.8f - 3.6f);
            w->addChild(dim);
            auto* cd = new ClockDisplay(module);
            cd->box.size = mm2px(Vec(11.f, 4.6f));
            cd->box.pos = mm(88.8f, 12.8f - 2.3f);
            w->addChild(cd);
        }
        if (!essential) {
        w->addOutput(createOutput<BlackJack>(mm(10.f, 112.f), module, SynTHBloK::STEREO_L_OUTPUT));
        w->addOutput(createOutput<BlackJack>(mm(21.f, 112.f), module, SynTHBloK::STEREO_R_OUTPUT));
        }
w->addParam(createParamCentered<Trimpot>(mm(56.f, 57.f), module, SynTHBloK::KEYTRACK_PARAM));
        if (!essential) w->addInput(createInputCentered<BlackJack>(mm(56.f, 50.f), module, SynTHBloK::KEYTRACK_INPUT));
        else {
            const float cx = 117.5f;
            w->addInput(createInputCentered<BlackJack>(mm(cx, 12.8f), module, SynTHBloK::CLOCK_IN_INPUT));
            w->addOutput(createOutputCentered<BlackJack>(mm(cx, 25.4f), module, SynTHBloK::CLOCK_OUT_OUTPUT));
            w->addInput(createInputCentered<BlackJack>(mm(cx, 38.f), module, SynTHBloK::RESET_INPUT));
            w->addOutput(createOutputCentered<BlackJack>(mm(cx, 105.5f), module, SynTHBloK::STEREO_L_OUTPUT));
            w->addOutput(createOutputCentered<BlackJack>(mm(cx, 116.3f), module, SynTHBloK::STEREO_R_OUTPUT));
        }

}
