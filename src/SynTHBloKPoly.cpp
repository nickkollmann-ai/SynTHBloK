#include "plugin.hpp"
#include "SynTHBloK.hpp"
#include "SynTHBloKMix24.hpp"

// ============================================================================
// SynTHBloKPoly — Hybrid Polyphonic Engine (Rack 2.6, C++11 compliant)
// ============================================================================

struct SynTHBloKPoly : Module {
    float smoothChorus = 0.f;   // per-instance (was a function-local static)


    // ------------------------------------------------------------
    // Chorus toggle state
    // ------------------------------------------------------------
    int  chorusMode       = 0;
    bool chorusButtonPrev = false;

    float globalPitchSlew = 0.f;

    // ------------------------------------------------------------
    // PARAMS / INPUTS / OUTPUTS / LIGHTS
    // ------------------------------------------------------------
    enum ParamId {
        MODE_PARAM,
        UNISON_PARAM,

        ATTACK_PARAM,
        DECAY_PARAM,
        SUSTAIN_PARAM,
        RELEASE_PARAM,

        TONE_PARAM,

        CHORUS_AMOUNT_PARAM,
        POLY_VOL_PARAM,

        PARAMS_LEN
    };

    enum InputId {
        POLY_VOCT_INPUT,
        POLY_GATE_INPUT,
        RESET_INPUT,
        SPREAD_CV_INPUT,
        POLY_ENV_IN_INPUT,
        POLY_FLT_IN_INPUT,
        POLY_VCA_CV_INPUT,
        INPUTS_LEN
    };

    enum OutputId {
        POLY_AUDIO_L_OUTPUT,
        POLY_AUDIO_R_OUTPUT,

        ENV_POLY_OUTPUT,
        VCA_POLY_OUTPUT,
        FLT_POLY_OUTPUT,
        PITCH_POLY_OUTPUT,
        
        OUTPUTS_LEN
    };

    enum LightId {
        STATUS_LIGHT,
        DETUNE_LIGHT,
        CLAMP_LIGHT,
        SAFEMODE_LIGHT,

        VOCT_IN_LIGHT,
        GATE_IN_LIGHT,
        RESET_IN_LIGHT,
        SPREAD_IN_LIGHT,

        ENV_LIGHT,
        VCA_LIGHT = ENV_LIGHT + 3,
        FLT_LIGHT = VCA_LIGHT + 3,
        PITCH_LIGHT = FLT_LIGHT + 3,

        ATTACK_LIGHT,
        DECAY_LIGHT,
        SUSTAIN_LIGHT,
        RELEASE_LIGHT,
        MODE_LIGHT,
        KNOB_CHORUS_LIGHT,
        ENUMS(KNOB_LED, 6 * 3),
        ENUMS(JACK_LED, 13 * 3),
        LIGHTS_LEN
    };

    // ------------------------------------------------------------
    // ADSR stage machine
    // ------------------------------------------------------------
    enum EnvStage {
        STAGE_IDLE,
        STAGE_ATTACK,
        STAGE_DECAY,
        STAGE_SUSTAIN,
        STAGE_RELEASE
    };

    // ------------------------------------------------------------
    // Polyphony + harmonic stacks
    // ------------------------------------------------------------
    static constexpr int MAX_VOICES   = 8;
    static constexpr int NUM_STACKS   = 11;
    static constexpr int MAX_STACK_SZ = 5;

static const int stackSizes[NUM_STACKS];
static const int stackIntervals[NUM_STACKS][MAX_STACK_SZ];

    // ------------------------------------------------------------
    // Internal bus
    // ------------------------------------------------------------
    SynTHBloKInternalBus localBusPoly;

    // ------------------------------------------------------------
    // Per‑voice state
    // ------------------------------------------------------------
    int   activeVoices          = 4;
    float lastPitch[MAX_VOICES] = {};
    float lastEnv[MAX_VOICES]   = {};
    float lastVca[MAX_VOICES]   = {};
    float lastFlt[MAX_VOICES]   = {};

    float smoothUnison = 0.f;
    float smoothSpread = 0.f;

    float smoothEnv[MAX_VOICES] = {};
    float smoothVca[MAX_VOICES] = {};
    float smoothFlt[MAX_VOICES] = {};

    float drift[MAX_VOICES]      = {};
    float driftSpeed[MAX_VOICES] = {};

    float slewPitch[MAX_VOICES] = {};
    float envState[MAX_VOICES]  = {};
    float vcaState[MAX_VOICES]  = {};
    float fltState[MAX_VOICES]  = {};

    bool  gatePrev[MAX_VOICES]  = {};
    float envJitter[MAX_VOICES] = {};

    float oscPhase[MAX_VOICES] = {};

    EnvStage envStage[MAX_VOICES] = {};
    float    envValue[MAX_VOICES] = {};

    float smoothPitchInternal[MAX_VOICES] = {};
    float vcaSmooth[MAX_VOICES]           = {};
    float lp1[MAX_VOICES]                 = {};
    float lp2[MAX_VOICES]                 = {};

    float voiceCardBias[MAX_VOICES] = {};

    float phaseOffset[MAX_VOICES]   = {};
    float shapeBias[MAX_VOICES]     = {};
    float satBias[MAX_VOICES]       = {};
    float filtBias[MAX_VOICES]      = {};
    float shimmerBias[MAX_VOICES]   = {};
    float toneBiasV[MAX_VOICES]     = {};
    float vcaCurveBias[MAX_VOICES]  = {};
    float panBias[MAX_VOICES]       = {};

    // ------------------------------------------------------------
    // Post‑mix chorus buffers
    // ------------------------------------------------------------
    static constexpr int CHORUS_TAPS = 4;
    static constexpr int CHORUS_BUF  = 4096;

    float chorusBufL[CHORUS_BUF] = {};
    float chorusBufR[CHORUS_BUF] = {};
    int   chorusWrite            = 0;

    float chorusPhase[CHORUS_TAPS]       = {};
    float chorusPhaseOffset[CHORUS_TAPS] = {};
    // ------------------------------------------------------------
    // Constructor
    // ------------------------------------------------------------
    SynTHBloKPoly() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // -----------------------------
        // PARAMS
        // -----------------------------
        configParam(MODE_PARAM, 0.f, 3.f, 0.f, "Chorus Mode");
        configParam(UNISON_PARAM, 0.f, float(NUM_STACKS - 1), 0.f, "Unison Stack");

        configParam(ATTACK_PARAM,  0.f, 0.5f, 0.01f, "Attack",  "s");
        configParam(DECAY_PARAM,   0.f, 1.5f, 0.15f, "Decay",   "s");
        configParam(SUSTAIN_PARAM, 0.f, 1.0f, 0.70f, "Sustain");
        configParam(RELEASE_PARAM, 0.f, 1.5f, 0.20f, "Release", "s");

        configParam(TONE_PARAM, -1.f, 1.f, 0.f, "Tone");
        configParam(CHORUS_AMOUNT_PARAM, 0.f, 1.f, 0.5f, "Chorus Amount");
        configParam(POLY_VOL_PARAM, 0.f, 1.f, 1.f, "Poly Volume Trim");

        // -----------------------------
        // INPUTS
        // -----------------------------
        configInput(POLY_VOCT_INPUT,   "Poly V/Oct");
        configInput(POLY_GATE_INPUT,   "Poly Gate");
        configInput(RESET_INPUT,       "Reset");
        configInput(SPREAD_CV_INPUT,   "Spread CV");
        configInput(POLY_ENV_IN_INPUT, "Poly Env In");
        configInput(POLY_FLT_IN_INPUT, "Poly Filter In");
        configInput(POLY_VCA_CV_INPUT, "Poly VCA CV");

        // -----------------------------
        // OUTPUTS
        // -----------------------------
        configOutput(POLY_AUDIO_L_OUTPUT, "Poly Audio L");
        configOutput(POLY_AUDIO_R_OUTPUT, "Poly Audio R");
        configOutput(ENV_POLY_OUTPUT,     "Env Poly");
        configOutput(VCA_POLY_OUTPUT,     "VCA Poly");
        configOutput(FLT_POLY_OUTPUT,     "Filter Poly");
        configOutput(PITCH_POLY_OUTPUT,   "Pitch Poly");

        // -----------------------------
        // LIGHTS
        // -----------------------------
        configLight(STATUS_LIGHT, "Status");
        configLight(DETUNE_LIGHT, "Detune Activity");
        configLight(CLAMP_LIGHT,  "Pitch Clamp");
        
        configLight(SAFEMODE_LIGHT, "Safe Mode");
        configLight(MODE_LIGHT, "Mode Button LED");

        configLight(VOCT_IN_LIGHT,  "V/Oct Input");
        configLight(GATE_IN_LIGHT,  "Gate Input");
        configLight(RESET_IN_LIGHT, "Reset Input");
        configLight(SPREAD_IN_LIGHT,"Spread CV Input");

        configLight(ENV_LIGHT,   "Env Output");
        configLight(VCA_LIGHT,   "VCA Output");
        configLight(FLT_LIGHT,   "Filter Output");
        configLight(PITCH_LIGHT, "Pitch Output");

        configLight(ATTACK_LIGHT,  "Attack Stage");
        configLight(DECAY_LIGHT,   "Decay Stage");
        configLight(SUSTAIN_LIGHT, "Sustain Stage");
        configLight(RELEASE_LIGHT, "Release Stage");

        configLight(KNOB_CHORUS_LIGHT, "Chorus Knob Halo");

        // -----------------------------
        // Per‑voice randomization
        // -----------------------------
        for (int v = 0; v < MAX_VOICES; ++v) {
            driftSpeed[v] = 0.f;
            drift[v]      = 0.f;

            envJitter[v]  = 1.f;
            oscPhase[v]   = random::uniform();

            envStage[v] = STAGE_IDLE;
            envValue[v] = 0.f;

            voiceCardBias[v] = (random::uniform() - 0.5f) * 0.001f;

            phaseOffset[v]  = random::uniform() * 0.25f;
            shapeBias[v]    = 0.9f  + (random::uniform() - 0.5f) * 0.10f;
            satBias[v]      = 1.6f  + (random::uniform() - 0.5f) * 0.20f;
            filtBias[v]     = 1.0f  + (random::uniform() - 0.5f) * 0.08f;
            shimmerBias[v]  = 0.8f  + (random::uniform() - 0.5f) * 0.40f;
            toneBiasV[v]    =        (random::uniform() - 0.5f) * 0.15f;
            vcaCurveBias[v] = 1.0f  + (random::uniform() - 0.5f) * 0.20f;
            panBias[v]      =        (random::uniform() - 0.5f) * 0.15f;
        }

        // -----------------------------
        // Chorus LFO initialization
        // -----------------------------
        for (int i = 0; i < CHORUS_TAPS; ++i) {
            chorusPhase[i]       = random::uniform();
            chorusPhaseOffset[i] = random::uniform();
        }
    }
    
    
    // ------------------------------------------------------------
    // PROCESS
    // ------------------------------------------------------------
    // Save / load the chorus mode with the patch (it is not a param, so it was lost on reload)
    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "chorusMode", json_integer(chorusMode));
        return rootJ;
    }
    void dataFromJson(json_t* rootJ) override {
        json_t* j = json_object_get(rootJ, "chorusMode");
        if (j) chorusMode = clamp((int)json_integer_value(j), 0, 3);
    }


    // ---- tiny RGB LEDs (same colours/behaviour as FX / Filter / Stereo) ----
    struct LedK { int id; float x, y; };
    struct LedJ { bool in; int id; float x, y; };
    static const LedK* kLeds(int& n) {
        static const LedK a[] = {
            {ATTACK_PARAM, 8.f, 73.f},
            {DECAY_PARAM, 18.f, 73.f},
            {SUSTAIN_PARAM, 28.f, 73.f},
            {RELEASE_PARAM, 38.f, 73.f},
            {TONE_PARAM, 20.f, 88.4f},
            {CHORUS_AMOUNT_PARAM, 38.f, 88.f}
        };
        n = (int)(sizeof(a) / sizeof(a[0])); return a;
    }
    static const LedJ* jLeds(int& n) {
        static const LedJ a[] = {
            {true, POLY_VOCT_INPUT, 8.f, 16.3f},
            {true, POLY_GATE_INPUT, 18.f, 16.3f},
            {true, SPREAD_CV_INPUT, 28.f, 16.3f},
            {true, RESET_INPUT, 38.f, 16.3f},
            {true, POLY_ENV_IN_INPUT, 8.f, 31.9f},
            {true, POLY_VCA_CV_INPUT, 18.f, 31.9f},
            {true, POLY_FLT_IN_INPUT, 28.f, 31.9f},
            {false, ENV_POLY_OUTPUT, 8.f, 50.9f},
            {false, VCA_POLY_OUTPUT, 18.f, 50.9f},
            {false, FLT_POLY_OUTPUT, 28.f, 50.9f},
            {false, PITCH_POLY_OUTPUT, 38.f, 50.9f},
            {false, POLY_AUDIO_L_OUTPUT, 28.f, 109.9f},
            {false, POLY_AUDIO_R_OUTPUT, 38.f, 109.9f}
        };
        n = (int)(sizeof(a) / sizeof(a[0])); return a;
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
        float w = clamp(x, 0.f, 1.f);
        float blue   = clamp(1.f - (w * 2.f), 0.f, 1.f);
        float red    = clamp((w - 0.5f) * 2.f, 0.f, 1.f);
        float yellow = clamp(1.f - fabsf(w - 0.5f) * 2.f, 0.f, 1.f);
        float rr, gg, bb; ledRamp(w, rr, gg, bb);
        lights[baseId + 0].setSmoothBrightness(rr, 1);
        lights[baseId + 1].setSmoothBrightness(gg, 1);
        lights[baseId + 2].setSmoothBrightness(bb, 1);
    }
    inline void setSignalLED(int baseId, float v) {
        float n = clamp(v / 5.f, -1.f, 1.f);
        if (n < 0.f) {
            lights[baseId + 2].setSmoothBrightness(-n, 1);
            lights[baseId + 0].setSmoothBrightness(0.f, 1);
            lights[baseId + 1].setSmoothBrightness(0.f, 1);
        } else {
            lights[baseId + 0].setSmoothBrightness(n, 1);
            lights[baseId + 1].setSmoothBrightness(n * 0.6f, 1);
            lights[baseId + 2].setSmoothBrightness(0.f, 1);
        }
    }
    inline void updateTinyLEDs() {
        int n; const LedK* k = kLeds(n);
        for (int i = 0; i < n; i++) setParamLED(KNOB_LED + i * 3, paramQuantities[k[i].id]->getScaledValue());
        const LedJ* j = jLeds(n);
        for (int i = 0; i < n; i++)
            setSignalLED(JACK_LED + i * 3, j[i].in ? inputs[j[i].id].getVoltage() : outputs[j[i].id].getVoltage());
    }

    void process(const ProcessArgs& args) override {
        float dt         = args.sampleTime;
        float sampleRate = args.sampleRate;

        // --------------------------------------------------------
        // CHORUS MODE TOGGLE
        // --------------------------------------------------------
        bool pressed = params[MODE_PARAM].getValue() > 0.5f;
        if (pressed && !chorusButtonPrev) {
            chorusMode = (chorusMode + 1) % 4;
        }
        chorusButtonPrev = pressed;

        // --------------------------------------------------------
        // Clear internal bus
        // --------------------------------------------------------
        localBusPoly.L[0] = 0.f;
        localBusPoly.R[0] = 0.f;

        // --------------------------------------------------------
        // UNISON (stepped → normalized)
        // --------------------------------------------------------
        float unisonIndex = params[UNISON_PARAM].getValue();
        int preset = clamp((int)std::round(unisonIndex), 0, NUM_STACKS - 1);

        float unisonNorm = (NUM_STACKS > 1)
            ? float(preset) / float(NUM_STACKS - 1)
            : 0.f;

        smoothUnison += (unisonNorm - smoothUnison) * 0.15f;
float chorusAmt = params[CHORUS_AMOUNT_PARAM].getValue();
smoothChorus += (chorusAmt - smoothChorus) * 0.15f;
lights[KNOB_CHORUS_LIGHT].setBrightness(smoothChorus);

        // --------------------------------------------------------
        // Spread CV smoothing
        // --------------------------------------------------------
        float spreadCv = inputs[SPREAD_CV_INPUT].getVoltage();
        spreadCv = clamp(spreadCv, -5.f, 5.f);
        float rawSpread = spreadCv * 0.2f;
        smoothSpread += (rawSpread - smoothSpread) * 0.15f;

        // --------------------------------------------------------
        // Voice allocation
        // --------------------------------------------------------
        int inVoices = inputs[POLY_VOCT_INPUT].getChannels();
        if (inVoices <= 0)
            inVoices = 1;

        int voices = std::min(inVoices, MAX_VOICES);
        activeVoices = voices;

        outputs[ENV_POLY_OUTPUT].setChannels(voices);
        outputs[VCA_POLY_OUTPUT].setChannels(voices);
        outputs[FLT_POLY_OUTPUT].setChannels(voices);
        outputs[PITCH_POLY_OUTPUT].setChannels(voices);

        outputs[POLY_AUDIO_L_OUTPUT].setChannels(1);
        outputs[POLY_AUDIO_R_OUTPUT].setChannels(1);

        // --------------------------------------------------------
        // Safe mode (pitch slew protection)
        // --------------------------------------------------------
        bool safeMode = false;
        if (inputs[POLY_VOCT_INPUT].isConnected() &&
            outputs[PITCH_POLY_OUTPUT].isConnected()) {
            safeMode = true;
        }

        // --------------------------------------------------------
        // Global VCA CV
        // --------------------------------------------------------
        float vcaGlobalMod = 1.f;
        if (inputs[POLY_VCA_CV_INPUT].isConnected()) {
            float cv = inputs[POLY_VCA_CV_INPUT].getVoltage();
            vcaGlobalMod = clamp(cv / 10.f, 0.f, 1.f);
        }

        bool anyClamp = false;
        float detuneActivity = 0.f;

        // --------------------------------------------------------
        // ADSR parameters
        // --------------------------------------------------------
        float A = params[ATTACK_PARAM].getValue();
        float D = params[DECAY_PARAM].getValue();
        float S = params[SUSTAIN_PARAM].getValue();
        float R = params[RELEASE_PARAM].getValue();

        const float A_min = 0.000f;
        const float D_min = 0.000f;
        const float S_min = 0.000f;
        const float R_min = 0.010f;

        A = std::max(A, A_min);
        D = std::max(D, D_min);
        S = std::max(S, S_min);
        R = std::max(R, R_min);

        int stackCount = stackSizes[preset];

        // --------------------------------------------------------
        // Stereo accumulators
        // --------------------------------------------------------
        float mixL = 0.f;
        float mixR = 0.f;

        // --------------------------------------------------------
        // PER‑VOICE LOOP
        // --------------------------------------------------------
        for (int v = 0; v < voices; ++v) {

            int ch = (v < inVoices) ? v : (inVoices - 1);

            // ----------------------------------------------------
            // BASE PITCH
            // ----------------------------------------------------
            float basePitch = inputs[POLY_VOCT_INPUT].getPolyVoltage(ch);
            basePitch = clamp(basePitch, -6.f, 6.f);

            float slewAmt = safeMode ? 0.05f : 0.15f;
            globalPitchSlew += (basePitch - globalPitchSlew) * slewAmt;
            float p = globalPitchSlew;

            // ----------------------------------------------------
            // UNISON DETUNE
            // ----------------------------------------------------
            float detuneVolts = 0.f;
            if (!safeMode) {
                float x = (voices > 1)
                    ? (v - (voices - 1) * 0.5f) / ((voices - 1) * 0.5f)
                    : 0.f;

                float detuneCents = x * (smoothSpread * 14.f + smoothUnison * 10.f);
                detuneVolts = detuneCents / 1200.f;
                detuneActivity += std::fabs(detuneCents);
            }

            p += detuneVolts;
            p += voiceCardBias[v];
            p = clamp(p, -3.f, 3.f);
            lastPitch[v] = p;

            // ----------------------------------------------------
            // GATE + ENV
            // ----------------------------------------------------
            float gate = inputs[POLY_GATE_INPUT].getVoltage() > 1.f ? 1.f : 0.f;
            bool gateNow  = gate > 0.f;
            bool gateRise = gateNow && !gatePrev[v];
            gatePrev[v]   = gateNow;

            if (gateRise) {
                envJitter[v] = 1.f + (random::uniform() - 0.5f) * 0.02f;
            }

            bool useExternalEnv = inputs[POLY_ENV_IN_INPUT].isConnected();
            if (useExternalEnv) {
                float eg = inputs[POLY_ENV_IN_INPUT].getPolyVoltage(ch);
                eg = clamp(eg / 10.f, 0.f, 1.f);
                envState[v] = eg;
            }
            else {
                if (gateNow) {
                    if (envState[v] < 1.f) {
                        float coeff = (A > 0.f) ? std::exp(-dt / A) : 0.f;
                        envState[v] = envState[v] * coeff + (1.f - coeff);
                    }
                    else {
                        float coeff = (D > 0.f) ? std::exp(-dt / D) : 0.f;
                        envState[v] = envState[v] * coeff + S * (1.f - coeff);
                    }
                }
                else {
                    float coeff = (R > 0.f) ? std::exp(-dt / R) : 0.f;
                    envState[v] = envState[v] * coeff;
                }
            }

            envState[v] = clamp(envState[v], 0.f, 1.f);
            vcaState[v] = envState[v];

            // ----------------------------------------------------
            // ADSR STAGE MACHINE
            // ----------------------------------------------------
            {
                EnvStage s = envStage[v];
                float env  = envState[v];

                if (gateNow) {
                    if (s == STAGE_IDLE || s == STAGE_RELEASE) {
                        s = STAGE_ATTACK;
                    }
                    else if (s == STAGE_ATTACK && env >= 0.999f) {
                        s = STAGE_DECAY;
                    }
                    else if (s == STAGE_DECAY && env <= S) {
                        env = S;
                        envState[v] = S;
                        s = STAGE_SUSTAIN;
                    }
                }
                else {
                    if (s == STAGE_ATTACK || s == STAGE_DECAY || s == STAGE_SUSTAIN) {
                        env = envState[v];
                        envState[v] = env;
                        s = STAGE_RELEASE;
                    }
                    else if (s == STAGE_RELEASE && env <= 0.001f) {
                        env = 0.f;
                        envState[v] = 0.f;
                        s = STAGE_IDLE;

                        lp1[v] = 0.f;
                        lp2[v] = 0.f;
                    }
                }

                envStage[v] = s;
                envValue[v] = env;
            }

            // ----------------------------------------------------
            // VCA smoothing + curve
            // ----------------------------------------------------
            float vcaCoeff = 0.001f;
            vcaSmooth[v] = vcaSmooth[v] * (1.f - vcaCoeff) + vcaState[v] * vcaCoeff;

            float vcaCurve = 1.5f * vcaCurveBias[v];
            float vca = powf(vcaSmooth[v], vcaCurve) * envJitter[v] * vcaGlobalMod;

            // ----------------------------------------------------
            // OSCILLATOR
            // ----------------------------------------------------
            float freq = dsp::FREQ_C4 * std::exp2(p);
            freq = clamp(freq, 10.f, 18000.f);

            oscPhase[v] += freq * dt;
            oscPhase[v] -= std::floor(oscPhase[v]);

            float basePhase = oscPhase[v] + phaseOffset[v];
            basePhase -= std::floor(basePhase);

            // ----------------------------------------------------
            // HARMONIC STACK ENGINE
            // ----------------------------------------------------
            float oscSum = 0.f;
            for (int i = 0; i < stackCount; ++i) {
                int semi = stackIntervals[preset][i];
                float ratio = std::exp2(semi / 12.f);

                float ph = basePhase * ratio;
                ph -= std::floor(ph);

                float o = ph * 2.f - 1.f;
                o *= shapeBias[v];

                float s = tanhf(o * satBias[v]);
                o = 0.72f * o + 0.28f * s;

                oscSum += o;
            }
            float osc = oscSum / std::max(1, stackCount);

            // ----------------------------------------------------
            // FILTER INPUT MIX
            // ----------------------------------------------------
            float voiceIn = osc;
            if (inputs[POLY_FLT_IN_INPUT].isConnected()) {
                float ext = inputs[POLY_FLT_IN_INPUT].getPolyVoltage(ch);
                voiceIn += ext * 0.4f;
            }

            // ----------------------------------------------------
            // SIMPLE LP FILTER
            // ----------------------------------------------------
            float cutoffMod = 0.f;
            float cutoffHz = (450.f + cutoffMod * 9000.f) * filtBias[v];
            cutoffHz = clamp(cutoffHz, 80.f, 16000.f);
            float c = expf(-2.f * M_PI * cutoffHz * dt);

            lp1[v] = lp1[v] * c + voiceIn * (1.f - c);
            lp2[v] = lp2[v] * c + lp1[v] * (1.f - c);
            float voiceOut = lp2[v];

            // ----------------------------------------------------
            // PAD ENGINE
            // ----------------------------------------------------
            float sineCore = sinf(basePhase * 2.f * M_PI);
            float sineBody = sinf((basePhase * 0.5f) * 2.f * M_PI);
            float tri      = 4.f * fabsf(basePhase - 0.5f) - 1.f;
            float fifth    = sinf((basePhase * 1.5f) * 2.f * M_PI) * shimmerBias[v];

            float padMix =
                voiceOut * 0.55f +
                sineCore * 0.25f +
                sineBody * 0.10f +
                tri      * 0.04f +
                fifth    * 0.03f;

            voiceOut = tanhf(padMix * 1.10f);

            // ----------------------------------------------------
            // TONE MACRO
            // ----------------------------------------------------
            float tone = params[TONE_PARAM].getValue() + toneBiasV[v];
            tone = clamp(tone, -1.f, 1.f);

            float warmTone   = tanhf(voiceOut * 0.8f);
            float brightTone = tanhf(voiceOut * 1.35f);

            if (tone < 0.f) {
                voiceOut = rack::math::crossfade(voiceOut, warmTone, -tone);
            }
            else if (tone > 0.f) {
                voiceOut = rack::math::crossfade(voiceOut, brightTone, tone);
            }

            // ----------------------------------------------------
            // PER‑VOICE DRIVE
            // ----------------------------------------------------
            float tilt = (1.f - 0.012f * v) * (0.82f + 0.18f * cutoffMod);
            voiceOut *= tilt;

            float drive = 1.08f + 0.025f * v;
            float satVoice = tanhf(voiceOut * drive);
            voiceOut = 0.86f * voiceOut + 0.14f * satVoice;

            // Apply VCA
            voiceOut *= vca;

            lastEnv[v] = envState[v];
            lastVca[v] = vca;
            lastFlt[v] = voiceOut;

            // ----------------------------------------------------
            // PRE‑MIX WARMTH
            // ----------------------------------------------------
            {
                voiceOut *= 1.18f;
                float warm = tanhf(voiceOut * 0.9f);
                voiceOut = 0.84f * voiceOut + 0.16f * warm;
            }

            // ----------------------------------------------------
            // STEREO PAN
            // ----------------------------------------------------
            float panNorm = 0.f;
            if (voices > 1) {
                panNorm = (v - (voices - 1) * 0.5f) / ((voices - 1) * 0.5f);
            }

            float width = clamp(smoothUnison, 0.f, 1.f);
            float pan = (panNorm + panBias[v]) * 0.6f * width;
            pan = clamp(pan, -0.9f, 0.9f);

            float lGain = 0.5f - pan * 0.5f;
            float rGain = 0.5f + pan * 0.5f;

            float Lvoice = voiceOut * lGain;
            float Rvoice = voiceOut * rGain;

            mixL += Lvoice;
            mixR += Rvoice;

            // ----------------------------------------------------
            // POLY TAPS
            // ----------------------------------------------------
            outputs[PITCH_POLY_OUTPUT].setVoltage(p, v);
            outputs[ENV_POLY_OUTPUT].setVoltage(lastEnv[v] * 10.f, v);
            outputs[VCA_POLY_OUTPUT].setVoltage(lastVca[v] * 10.f, v);
            outputs[FLT_POLY_OUTPUT].setVoltage(lastFlt[v] * 5.f,  v);
        }

        // --------------------------------------------------------
        // MONO‑CORE RECOMBINATION
        // --------------------------------------------------------
        float monoRaw = 0.5f * (mixL + mixR);

        int active = activeVoices > 0 ? activeVoices : 1;
        float norm = 1.f / std::sqrt((float)active);

        float mono = monoRaw * norm;

        {
            float warm = tanhf(mono * 0.9f);
            const float blend = 0.22f;
            mono = mono * (1.f - blend) + warm * blend;
        }

        mono *= 1.30f;

        mixL *= norm;
        mixR *= norm;

        {
            float warmL = tanhf(mixL * 0.9f);
            float warmR = tanhf(mixR * 0.9f);
            const float sBlend = 0.18f;
            mixL = mixL * (1.f - sBlend) + warmL * sBlend;
            mixR = mixR * (1.f - sBlend) + warmR * sBlend;
        }

        // --------------------------------------------------------
        // MONO‑CORE TONE + SIDE REBUILD
        // --------------------------------------------------------
        float sideL = mixL - monoRaw;
        float sideR = mixR - monoRaw;

        float coreL = mono;
        float coreR = mono;

        mixL = coreL + sideL;
        mixR = coreR + sideR;

        // --------------------------------------------------------
        // POST‑MIX CHORUS (B1/C1)
        // --------------------------------------------------------
        {
            int mode = chorusMode;
            float amount = clamp(params[CHORUS_AMOUNT_PARAM].getValue(), 0.f, 1.f);

            if (mode > 0 && amount > 0.001f && sampleRate > 0.f) {

                const float rates[CHORUS_TAPS]     = {0.20f, 0.40f, 0.60f, 0.80f};
                const float baseDelayMs[CHORUS_TAPS] = {8.f, 12.f, 16.f, 20.f};

                int tapsUsed = 4;
                float depthScale = 1.f;

                if (mode == 1) {
                    tapsUsed   = 2;
                    depthScale = 0.4f;
                }
                else if (mode == 2) {
                    tapsUsed   = 4;
                    depthScale = 0.6f;
                }
                else {
                    tapsUsed   = 4;
                    depthScale = 1.0f;
                }

                float depthMsBase = 3.0f;
                float depthMs     = depthMsBase * depthScale;

                chorusBufL[chorusWrite] = mixL;
                chorusBufR[chorusWrite] = mixR;

                float wetL = 0.f;
                float wetR = 0.f;

                for (int i = 0; i < tapsUsed; ++i) {

                    chorusPhase[i] += rates[i] * dt;
                    chorusPhase[i] = fmod(chorusPhase[i], 1.f);
                                        float ph = chorusPhase[i] + chorusPhaseOffset[i];
                    ph -= std::floor(ph);

                    float mod = sinf(2.f * M_PI * ph);

                    float delayMs = baseDelayMs[i] + mod * depthMs;
                    delayMs = clamp(delayMs, 3.f, 30.f);

                    float delaySamples = delayMs * (sampleRate / 1000.f);

                    float readPos = (float)chorusWrite - delaySamples;
                    while (readPos < 0.f) readPos += (float)CHORUS_BUF;
                    while (readPos >= (float)CHORUS_BUF) readPos -= (float)CHORUS_BUF;

                    int i0 = (int)readPos;
                    int i1 = (i0 + 1) % CHORUS_BUF;
                    float frac = readPos - (float)i0;

                    float l0 = chorusBufL[i0];
                    float l1 = chorusBufL[i1];
                    float r0 = chorusBufR[i0];
                    float r1 = chorusBufR[i1];

                    float lTap = l0 + (l1 - l0) * frac;
                    float rTap = r0 + (r1 - r0) * frac;

                    wetL += lTap;
                    wetR += rTap;
                }

                if (tapsUsed > 0) {
                    wetL /= (float)tapsUsed;
                    wetR /= (float)tapsUsed;
                }

                float mixDepth = amount * 0.6f;

                mixL = mixL * (1.f - mixDepth) + wetL * mixDepth;
                mixR = mixR * (1.f - mixDepth) + wetR * mixDepth;

                chorusWrite++;
                if (chorusWrite >= CHORUS_BUF)
                    chorusWrite = 0;
            }
        }

        // --------------------------------------------------------
        updateTinyLEDs();
        // STATUS LIGHTS
        // --------------------------------------------------------
        bool gateHigh = inputs[POLY_GATE_INPUT].getVoltage() > 1.f;
        lights[STATUS_LIGHT].setBrightness(gateHigh ? 1.f : 0.f);

        float detuneLevel = (voices > 0) ? detuneActivity / (voices * 6.f) : 0.f;
        lights[DETUNE_LIGHT].setBrightness(detuneLevel);

        lights[CLAMP_LIGHT].setBrightness(anyClamp ? 1.f : 0.f);
        lights[SAFEMODE_LIGHT].setBrightness(safeMode ? 1.f : 0.f);

        // Input activity lights
        auto dim = [](float x) { return clamp(x * 0.6f, 0.f, 0.6f); };

        lights[VOCT_IN_LIGHT].setBrightness(dim(fabsf(inputs[POLY_VOCT_INPUT].getVoltage()) / 5.f));
        lights[GATE_IN_LIGHT].setBrightness(dim(inputs[POLY_GATE_INPUT].getVoltage() > 1.f ? 1.f : 0.f));
        lights[RESET_IN_LIGHT].setBrightness(dim(inputs[RESET_INPUT].getVoltage() > 1.f ? 1.f : 0.f));
        lights[SPREAD_IN_LIGHT].setBrightness(dim(fabsf(inputs[SPREAD_CV_INPUT].getVoltage()) / 5.f));

        // Poly tap RGB lights
        auto setRGB = [&](int base, float v) {
            float n = clamp(v / 5.f, -1.f, 1.f);

            float r = 0.f, g = 0.f, b = 0.f;

            if (n < -0.02f) {
                r = -n;
            }
            else if (n > 0.02f) {
                g = n;
            }
            else {
                r = 0.25f;
                g = 0.25f;
            }

            lights[base + 0].setBrightness(r);
            lights[base + 1].setBrightness(g);
            lights[base + 2].setBrightness(b);
        };

        if (activeVoices > 0) {
            setRGB(ENV_LIGHT,   outputs[ENV_POLY_OUTPUT].getVoltage(0));
            setRGB(VCA_LIGHT,   outputs[VCA_POLY_OUTPUT].getVoltage(0));
            setRGB(FLT_LIGHT,   outputs[FLT_POLY_OUTPUT].getVoltage(0));
            setRGB(PITCH_LIGHT, outputs[PITCH_POLY_OUTPUT].getVoltage(0));
        }

        // --------------------------------------------------------
        // ADSR STAGE LIGHTS
        // --------------------------------------------------------
        int   activeVoice = 0;
        float maxEnv      = 0.f;
        for (int v = 0; v < voices; ++v) {
            if (envValue[v] > maxEnv) {
                maxEnv      = envValue[v];
                activeVoice = v;
            }
        }

        lights[ATTACK_LIGHT].setSmoothBrightness(
            envStage[activeVoice] == STAGE_ATTACK ? 1.f : 0.f, args.sampleTime);

        lights[DECAY_LIGHT].setSmoothBrightness(
            envStage[activeVoice] == STAGE_DECAY ? 1.f : 0.f, args.sampleTime);

        lights[SUSTAIN_LIGHT].setSmoothBrightness(
            envStage[activeVoice] == STAGE_SUSTAIN ? 1.f : 0.f, args.sampleTime);

        lights[RELEASE_LIGHT].setSmoothBrightness(
            envStage[activeVoice] == STAGE_RELEASE ? 1.f : 0.f, args.sampleTime);

        // --------------------------------------------------------
        // OUTPUT STAGE
        // --------------------------------------------------------
        float outL = tanhf(mixL * 4.f) * 5.f;
        float outR = tanhf(mixR * 4.f) * 5.f;

        outL = tanhf(outL);
        outR = tanhf(outR);

        float volTrim = params[POLY_VOL_PARAM].getValue();
        outL *= volTrim;
        outR *= volTrim;

        outputs[POLY_AUDIO_L_OUTPUT].setVoltage(outL);
        outputs[POLY_AUDIO_R_OUTPUT].setVoltage(outR);

        // --------------------------------------------------------
        // LOCAL BUS WRITE
        // --------------------------------------------------------
        localBusPoly.L[0] = outL;
        localBusPoly.R[0] = outR;

        bool modePressed = params[MODE_PARAM].getValue() > 0.5f;
        lights[MODE_LIGHT].setBrightness(modePressed ? 1.f : 0.f);

    }
}; 

// ============================================================================
// C++11 constexpr DEFINITIONS (REQUIRED)
// ============================================================================

const int SynTHBloKPoly::stackSizes[SynTHBloKPoly::NUM_STACKS] = {
    1, 2, 2, 3, 3, 3, 3, 3, 4, 4, 5
};

const int SynTHBloKPoly::stackIntervals[SynTHBloKPoly::NUM_STACKS][SynTHBloKPoly::MAX_STACK_SZ] = {
    { 0,  0,  0,  0,  0},
    { 0,  7,  0,  0,  0},
    { 0, 12,  0,  0,  0},
    { 0,  7, 12,  0,  0},
    { 0, 12, 24,  0,  0},
    { 0,  3,  7,  0,  0},
    { 0,  4,  7,  0,  0},
    { 0, 12, 19,  0,  0},
    { 0,  7, 14, 21,  0},
    { 0, 12, 24, 36,  0},
    { 0,  3,  7, 12, 24}
};



// end of struct SynTHBloKPoly
// ============================================================================
// UNISON DISPLAY
// ============================================================================
struct UnisonDisplay : TransparentWidget {
    SynTHBloKPoly* module;

    // C++11: declaration only
    static const char* labels[SynTHBloKPoly::NUM_STACKS];

    UnisonDisplay(SynTHBloKPoly* m) {
        module = m;
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        NVGcontext* vg = args.vg;

        int preset = clamp(
            (int)std::round(module->params[SynTHBloKPoly::UNISON_PARAM].getValue()),
            0,
            SynTHBloKPoly::NUM_STACKS - 1
        );

        const char* label = labels[preset];

        NVGcolor bgTop    = nvgRGBA(10, 10, 10, 255);
        NVGcolor bgBottom = nvgRGBA(0, 0, 0, 255);
        NVGpaint bg = nvgLinearGradient(vg, 0, 0, 0, box.size.y, bgTop, bgBottom);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgFillPaint(vg, bg);
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgStrokeColor(vg, nvgRGBA(255, 180, 90, 80));
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);

        nvgFontSize(vg, 8.f);
        nvgFontFaceId(vg, 0);
        nvgFillColor(vg, nvgRGBA(255, 240, 180, 255));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        nvgText(vg, box.size.x * 0.5f, box.size.y * 0.5f, label, NULL);
    }
};

// OUT‑OF‑CLASS DEFINITION (C++11 requires this)
const char* UnisonDisplay::labels[SynTHBloKPoly::NUM_STACKS] = {
    "SING", "5TH", "OCT", "5+8", "OCT2",
    "MIN", "MAJ", "O+5", "STK5", "STK8", "FULL"
};

// ============================================================================
// CHORUS MODE DISPLAY
// ============================================================================
struct ModeDisplay : TransparentWidget {
    SynTHBloKPoly* module;

    ModeDisplay(SynTHBloKPoly* m) {
        module = m;
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        NVGcontext* vg = args.vg;

        int mode = module->chorusMode;
        const char* label = "OFF";
        if (mode == 1) label = "LITE";
        else if (mode == 2) label = "WIDE";
        else if (mode == 3) label = "ENS";

        NVGcolor bgTop    = nvgRGBA(10, 10, 10, 255);
        NVGcolor bgBottom = nvgRGBA(0, 0, 0, 255);
        NVGpaint bg = nvgLinearGradient(vg, 0, 0, 0, box.size.y, bgTop, bgBottom);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgFillPaint(vg, bg);
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgStrokeColor(vg, nvgRGBA(255, 180, 90, 80));
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);

        nvgFontSize(vg, 8.f);
        nvgFontFaceId(vg, 0);
        nvgFillColor(vg, nvgRGBA(255, 240, 180, 255));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        nvgText(vg, box.size.x * 0.5f, box.size.y * 0.5f, label, NULL);
    }
};

// ============================================================================
// CHORUS AMOUNT DISPLAY
// ============================================================================
struct ChorusAmountDisplay : TransparentWidget {
    SynTHBloKPoly* module;

    ChorusAmountDisplay(SynTHBloKPoly* m) {
        module = m;
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        NVGcontext* vg = args.vg;

        float amt = module->params[SynTHBloKPoly::CHORUS_AMOUNT_PARAM].getValue();

        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f", amt);

        NVGcolor bgTop    = nvgRGBA(10, 10, 10, 255);
        NVGcolor bgBottom = nvgRGBA(0, 0, 0, 255);
        NVGpaint bg = nvgLinearGradient(vg, 0, 0, 0, box.size.y, bgTop, bgBottom);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgFillPaint(vg, bg);
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgStrokeColor(vg, nvgRGBA(255, 180, 90, 80));
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);

        nvgFontSize(vg, 8.f);
        nvgFontFaceId(vg, 0);
        nvgFillColor(vg, nvgRGBA(255, 240, 180, 255));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        nvgText(vg, box.size.x * 0.5f, box.size.y * 0.5f, buf, NULL);
    }
};

// ============================================================================
// POLY VOICE COUNT DISPLAY
// ============================================================================
struct PolyVoiceCountDisplay : TransparentWidget {
    SynTHBloKPoly* module;

    PolyVoiceCountDisplay(SynTHBloKPoly* m) {
        module = m;
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;

        NVGcontext* vg = args.vg;
        int voices = module->activeVoices;

        NVGcolor bgTop    = nvgRGBA(10, 10, 10, 255);
        NVGcolor bgBottom = nvgRGBA(0, 0, 0, 255);
        NVGpaint bg = nvgLinearGradient(vg, 0, 0, 0, box.size.y, bgTop, bgBottom);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgFillPaint(vg, bg);
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgStrokeColor(vg, nvgRGBA(255, 180, 90, 80));
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);

        nvgFontSize(vg, 8.f);
        nvgFontFaceId(vg, 0);
        nvgFillColor(vg, nvgRGBA(255, 240, 180, 255));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        char buf[16];
        snprintf(buf, sizeof(buf), "%dV", voices);

        nvgText(vg, box.size.x * 0.5f, box.size.y * 0.5f, buf, NULL);
    }
};

// ============================================================================
// WIDGET
// ============================================================================
struct SynTHBloKPolyWidget : ModuleWidget {
    SynTHBloKPolyWidget(SynTHBloKPoly* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/SynTHBloKPoly.svg")));

        using namespace rack::componentlibrary;

        auto mm = [](float x, float y) { return mm2px(Vec(x, y)); };

        // Screws
        addChild(createWidgetCentered<ScrewSilver>(mm(3.5f, 3.5f)));
        addChild(createWidgetCentered<ScrewSilver>(mm(42.22f, 3.5f)));
        addChild(createWidgetCentered<ScrewSilver>(mm(3.5f, 125.0f)));
        addChild(createWidgetCentered<ScrewSilver>(mm(42.22f, 125.0f)));

        // Inputs

        {   // tiny RGB LEDs
            int nn; const SynTHBloKPoly::LedK* kk = SynTHBloKPoly::kLeds(nn);
            for (int i = 0; i < nn; i++)
                addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(kk[i].x, kk[i].y)), module, SynTHBloKPoly::KNOB_LED + i * 3));
            const SynTHBloKPoly::LedJ* jj = SynTHBloKPoly::jLeds(nn);
            for (int i = 0; i < nn; i++)
                addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(jj[i].x, jj[i].y)), module, SynTHBloKPoly::JACK_LED + i * 3));
        }
        addInput(createInputCentered<BlackJack>(mm(8.f, 21.4f), module, SynTHBloKPoly::POLY_VOCT_INPUT));
        addChild(createLightCentered<TinyLight<RedLight>>(mm(4.6f, 16.3f),   module, SynTHBloKPoly::CLAMP_LIGHT));
        addChild(createLightCentered<TinyLight<YellowLight>>(mm(11.4f, 16.3f), module, SynTHBloKPoly::SAFEMODE_LIGHT));
        addInput(createInputCentered<BlackJack>(mm(18.f, 21.4f), module, SynTHBloKPoly::POLY_GATE_INPUT));

        addInput(createInputCentered<BlackJack>(mm(28.f, 21.4f), module, SynTHBloKPoly::SPREAD_CV_INPUT));

        addInput(createInputCentered<BlackJack>(mm(38.f, 21.4f), module, SynTHBloKPoly::RESET_INPUT));

        // Chorus mode button
        addChild(createLightCentered<SmallLight<GreenLight>>(mm(28.f, 95.f), module, SynTHBloKPoly::MODE_LIGHT));
        
        addParam(createParamCentered<TL1105>(mm(28.f, 95.f), module, SynTHBloKPoly::MODE_PARAM));

        // Unison + Tone
        addParam(createParamCentered<RoundBlackSnapKnob>(mm(8.f, 95.f), module, SynTHBloKPoly::UNISON_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm(20.f, 95.f), module, SynTHBloKPoly::TONE_PARAM));

        // Chorus amount
        addParam(createParamCentered<RoundBlackKnob>(mm(38.f, 95.f), module, SynTHBloKPoly::CHORUS_AMOUNT_PARAM));

        // Displays
        auto* ud = new UnisonDisplay(module);
        ud->box.pos  = mm2px(Vec(4.f, 103.f));
        ud->box.size = Vec(22.f, 10.f);
        addChild(ud);

        auto* md = new ModeDisplay(module);
        md->box.pos  = mm2px(Vec(25.f, 103.f));
        md->box.size = Vec(21.f, 10.f);
        addChild(md);

        auto* cad = new ChorusAmountDisplay(module);
        cad->box.pos  = mm2px(Vec(35.f, 103.f));
        cad->box.size = Vec(25.f, 10.f);
        addChild(cad);

        auto* vcd = new PolyVoiceCountDisplay(module);
        vcd->box.pos  = mm2px(Vec(34.f, 112.f));
        vcd->box.size = Vec(20.f, 10.f);
        addChild(vcd);

        // ADSR
        addParam(createParamCentered<Trimpot>(mm(8.f, 78.f),  module, SynTHBloKPoly::ATTACK_PARAM));
        addParam(createParamCentered<Trimpot>(mm(18.f, 78.f), module, SynTHBloKPoly::DECAY_PARAM));
        addParam(createParamCentered<Trimpot>(mm(28.f, 78.f), module, SynTHBloKPoly::SUSTAIN_PARAM));
        addParam(createParamCentered<Trimpot>(mm(38.f, 78.f), module, SynTHBloKPoly::RELEASE_PARAM));


        // Poly taps
        addOutput(createOutputCentered<BlackJack>(mm(8.f, 56.f), module, SynTHBloKPoly::ENV_POLY_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm(18.f, 56.f), module, SynTHBloKPoly::VCA_POLY_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm(28.f, 56.f), module, SynTHBloKPoly::FLT_POLY_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm(38.f, 56.f), module, SynTHBloKPoly::PITCH_POLY_OUTPUT));


        // ENV / VCA / FILTER inputs
        addInput(createInputCentered<BlackJack>(mm(8.f, 37.f), module, SynTHBloKPoly::POLY_ENV_IN_INPUT));
        addInput(createInputCentered<BlackJack>(mm(18.f, 37.f), module, SynTHBloKPoly::POLY_VCA_CV_INPUT));
        addInput(createInputCentered<BlackJack>(mm(28.f, 37.f), module, SynTHBloKPoly::POLY_FLT_IN_INPUT));

        // Status lights
        addChild(createLightCentered<SmallLight<GreenLight>>(mm(28.f, 88.f), module, SynTHBloKPoly::STATUS_LIGHT));
        

        // Volume + audio outs
        addParam(createParamCentered<RoundSmallBlackKnob>(mm(8.f, 115.f), module, SynTHBloKPoly::POLY_VOL_PARAM));

        addOutput(createOutputCentered<BlackJack>(mm(28.f, 115.f), module, SynTHBloKPoly::POLY_AUDIO_L_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm(38.f, 115.f), module, SynTHBloKPoly::POLY_AUDIO_R_OUTPUT));
    }
};



Model* modelSynTHBloKPoly = createModel<SynTHBloKPoly, SynTHBloKPolyWidget>("SynTHBloKPoly");
