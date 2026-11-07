#include "plugin.hpp"
#include "SynTHBloK.hpp"
#include "SynTHBloKMix24.hpp"
#include "OledUi.hpp"

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

    // (ids kept in this order so saved patches and the light layout stay the same)
    enum LightId {
        STATUS_LIGHT,
        DETUNE_LIGHT,
        CLAMP_LIGHT,
        SAFEMODE_LIGHT,     // the yellow light by V/OCT: unison detune active

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

    float slewPitch[MAX_VOICES] = {};
    float envState[MAX_VOICES]  = {};

    bool  gatePrev[MAX_VOICES]  = {};
    float envJitter[MAX_VOICES] = {};

    float oscPhase[MAX_VOICES] = {};

    EnvStage envStage[MAX_VOICES] = {};

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

    dsp::SchmittTrigger resetTrig;
    dsp::ClockDivider lightDivider;

    // ------------------------------------------------------------
    // Post‑mix chorus buffers
    // ------------------------------------------------------------
    static constexpr int CHORUS_TAPS = 4;
    static constexpr int CHORUS_BUF  = 8192;   // 30 ms at 192 kHz is 5760 samples

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
        lightDivider.setDivision(32);

        // -----------------------------
        // PARAMS
        // -----------------------------
        configButton(MODE_PARAM, "Chorus mode (OFF / LITE / WIDE / ENS)");
        configSwitch(UNISON_PARAM, 0.f, float(NUM_STACKS - 1), 0.f, "Unison stack",
            {"Single", "5th", "Octave", "5th + octave", "Two octaves", "Minor", "Major",
             "Octave + 5th", "Stacked 5ths", "Stacked octaves", "Full"});

        configParam(ATTACK_PARAM,  0.f, 0.5f, 0.01f, "Attack",  " s");
        configParam(DECAY_PARAM,   0.f, 1.5f, 0.15f, "Decay",   " s");
        configParam(SUSTAIN_PARAM, 0.f, 1.0f, 0.70f, "Sustain", "%", 0.f, 100.f);
        configParam(RELEASE_PARAM, 0.f, 1.5f, 0.20f, "Release", " s");

        // cutoff of the voice filter: 450 Hz * 2^(3 x)
        configParam(TONE_PARAM, -1.f, 1.f, 0.f, "Tone (voice filter cutoff)", " Hz", 8.f, 450.f);
        configParam(CHORUS_AMOUNT_PARAM, 0.f, 1.f, 0.5f, "Chorus amount", "%", 0.f, 100.f);
        configParam(POLY_VOL_PARAM, 0.f, 1.f, 1.f, "Gain", "%", 0.f, 100.f);

        // -----------------------------
        // INPUTS
        // -----------------------------
        configInput(POLY_VOCT_INPUT,   "Poly V/Oct");
        configInput(POLY_GATE_INPUT,   "Poly Gate");
        configInput(RESET_INPUT,       "Reset (restarts every oscillator)");
        configInput(SPREAD_CV_INPUT,   "Spread CV");
        configInput(POLY_ENV_IN_INPUT, "Poly Env In (replaces the ADSR, 0-10 V)");
        configInput(POLY_FLT_IN_INPUT, "Poly Filter In (audio into each voice filter)");
        configInput(POLY_VCA_CV_INPUT, "VCA CV (0-10 V)");

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
        configLight(STATUS_LIGHT, "Gate");
        configLight(CLAMP_LIGHT,  "Pitch clamp (V/OCT past +-3 V)");
        configLight(SAFEMODE_LIGHT, "Unison detune");
        configLight(MODE_LIGHT, "Chorus mode");

        configLight(ATTACK_LIGHT,  "Attack Stage");
        configLight(DECAY_LIGHT,   "Decay Stage");
        configLight(SUSTAIN_LIGHT, "Sustain Stage");
        configLight(RELEASE_LIGHT, "Release Stage");

        // -----------------------------
        // Per‑voice randomization
        // -----------------------------
        for (int v = 0; v < MAX_VOICES; ++v) {
            envJitter[v]  = 1.f;
            oscPhase[v]   = random::uniform();

            envStage[v] = STAGE_IDLE;

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
            // under the L / R jacks (above them they sat on the labels)
            {false, POLY_AUDIO_L_OUTPUT, 28.f, 120.4f},
            {false, POLY_AUDIO_R_OUTPUT, 38.f, 120.4f}
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

    inline void setParamLED(int baseId, float x, float dt) {
        float rr, gg, bb; ledRamp(clamp(x, 0.f, 1.f), rr, gg, bb);
        lights[baseId + 0].setSmoothBrightness(rr, dt);
        lights[baseId + 1].setSmoothBrightness(gg, dt);
        lights[baseId + 2].setSmoothBrightness(bb, dt);
    }
    inline void setSignalLED(int baseId, float v, float dt) {
        float n = clamp(v / 5.f, -1.f, 1.f);
        if (n < 0.f) {
            lights[baseId + 2].setSmoothBrightness(-n, dt);
            lights[baseId + 0].setSmoothBrightness(0.f, dt);
            lights[baseId + 1].setSmoothBrightness(0.f, dt);
        } else {
            lights[baseId + 0].setSmoothBrightness(n, dt);
            lights[baseId + 1].setSmoothBrightness(n * 0.6f, dt);
            lights[baseId + 2].setSmoothBrightness(0.f, dt);
        }
    }
    inline void updateTinyLEDs(float dt) {
        int n; const LedK* k = kLeds(n);
        for (int i = 0; i < n; i++) setParamLED(KNOB_LED + i * 3, paramQuantities[k[i].id]->getScaledValue(), dt);
        const LedJ* j = jLeds(n);
        for (int i = 0; i < n; i++)
            setSignalLED(JACK_LED + i * 3, j[i].in ? inputs[j[i].id].getVoltage() : outputs[j[i].id].getVoltage(), dt);
    }

    void onReset() override {
        chorusMode = 0;
        for (int v = 0; v < MAX_VOICES; ++v) {
            envState[v] = vcaSmooth[v] = lp1[v] = lp2[v] = 0.f;
            envStage[v] = STAGE_IDLE;
            gatePrev[v] = false;
        }
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
        // RESET: restart every oscillator together
        // --------------------------------------------------------
        if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
            for (int v = 0; v < MAX_VOICES; ++v)
                oscPhase[v] = 0.f;
        }

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

        // --------------------------------------------------------
        // Spread CV smoothing
        // --------------------------------------------------------
        float spreadCv = inputs[SPREAD_CV_INPUT].getVoltage();
        spreadCv = clamp(spreadCv, -5.f, 5.f);
        float rawSpread = spreadCv * 0.2f;
        smoothSpread += (rawSpread - smoothSpread) * 0.15f;

        // --------------------------------------------------------
        // Voice allocation: as many voices as the wider of V/OCT and GATE
        // --------------------------------------------------------
        int inVoices = std::max(inputs[POLY_VOCT_INPUT].getChannels(), inputs[POLY_GATE_INPUT].getChannels());
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
        // ADSR: each stage takes about its knob time
        // --------------------------------------------------------
        float A = params[ATTACK_PARAM].getValue();
        float D = params[DECAY_PARAM].getValue();
        float S = params[SUSTAIN_PARAM].getValue();
        float R = std::max(params[RELEASE_PARAM].getValue(), 0.01f);

        // attack heads for 1.2 so it reaches 1 in A seconds; decay / release fall ~95% in D / R
        float aCoeff = (A > 0.0005f) ? 1.f - std::exp(-dt * 1.792f / A) : 1.f;
        float dCoeff = (D > 0.0005f) ? std::exp(-dt * 3.f / D) : 0.f;
        float rCoeff = std::exp(-dt * 3.f / R);

        int stackCount = stackSizes[preset];

        // voice filter cutoff from TONE: 450 Hz * 2^(3 tone), 56 Hz .. 3.6 kHz
        float toneKnob = params[TONE_PARAM].getValue();

        // --------------------------------------------------------
        // Stereo accumulators
        // --------------------------------------------------------
        float mixL = 0.f;
        float mixR = 0.f;

        bool useExternalEnv = inputs[POLY_ENV_IN_INPUT].isConnected();

        // --------------------------------------------------------
        // PER‑VOICE LOOP
        // --------------------------------------------------------
        for (int v = 0; v < voices; ++v) {

            int ch = v;

            // ----------------------------------------------------
            // BASE PITCH
            // ----------------------------------------------------
            float basePitch = inputs[POLY_VOCT_INPUT].getPolyVoltage(ch);
            if (basePitch < -3.f || basePitch > 3.f) anyClamp = true;
            basePitch = clamp(basePitch, -6.f, 6.f);

            // each voice slews on its own (one shared slew dragged every voice toward the previous one)
            slewPitch[v] += (basePitch - slewPitch[v]) * 0.15f;
            float p = slewPitch[v];

            // ----------------------------------------------------
            // UNISON DETUNE (spread across the voices)
            // ----------------------------------------------------
            float x = (voices > 1)
                ? (v - (voices - 1) * 0.5f) / ((voices - 1) * 0.5f)
                : 0.f;

            float detuneCents = x * (smoothSpread * 14.f + smoothUnison * 10.f);
            detuneActivity += std::fabs(detuneCents);

            p += detuneCents / 1200.f;
            p += voiceCardBias[v];
            p = clamp(p, -3.f, 3.f);
            lastPitch[v] = p;

            // ----------------------------------------------------
            // GATE + ENV
            // ----------------------------------------------------
            bool gateNow  = inputs[POLY_GATE_INPUT].getPolyVoltage(v) > 1.f;
            bool gateRise = gateNow && !gatePrev[v];
            gatePrev[v]   = gateNow;

            if (gateRise) {
                envJitter[v] = 1.f + (random::uniform() - 0.5f) * 0.02f;
            }

            float& env = envState[v];
            EnvStage& st = envStage[v];
            if (useExternalEnv) {
                env = clamp(inputs[POLY_ENV_IN_INPUT].getPolyVoltage(ch) / 10.f, 0.f, 1.f);
                st = gateNow ? STAGE_SUSTAIN : STAGE_IDLE;
            }
            else {
                if (gateRise)
                    st = STAGE_ATTACK;
                else if (!gateNow && st != STAGE_IDLE)
                    st = STAGE_RELEASE;

                switch (st) {
                    case STAGE_ATTACK:
                        env += (1.2f - env) * aCoeff;
                        if (env >= 1.f) { env = 1.f; st = STAGE_DECAY; }
                        break;
                    case STAGE_DECAY:
                        env = S + (env - S) * dCoeff;
                        if (std::fabs(env - S) < 0.001f) { env = S; st = STAGE_SUSTAIN; }
                        break;
                    case STAGE_SUSTAIN:
                        env = S;
                        break;
                    case STAGE_RELEASE:
                        env *= rCoeff;
                        if (env < 0.0005f) {
                            env = 0.f; st = STAGE_IDLE;
                            lp1[v] = 0.f;
                            lp2[v] = 0.f;
                        }
                        break;
                    default:
                        env = 0.f;
                        break;
                }
            }
            env = clamp(env, 0.f, 1.f);

            // ----------------------------------------------------
            // VCA smoothing (~1 ms, against clicks) + curve
            // ----------------------------------------------------
            float vcaCoeff = std::min(dt * 1000.f, 1.f);
            vcaSmooth[v] += (env - vcaSmooth[v]) * vcaCoeff;

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
                voiceIn += ext * 0.08f;          // +-5 V audio -> about the oscillator's level
            }

            // ----------------------------------------------------
            // VOICE LP FILTER (TONE sets the cutoff)
            // ----------------------------------------------------
            float tone = clamp(toneKnob + toneBiasV[v], -1.f, 1.f);
            float cutoffHz = 450.f * std::exp2(tone * 3.f) * filtBias[v];
            cutoffHz = clamp(cutoffHz, 40.f, std::min(16000.f, 0.45f * sampleRate));
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
            // TONE colour: warmer below centre, brighter above
            // ----------------------------------------------------
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
            voiceOut *= (1.f - 0.012f * v) * 0.82f;

            float drive = 1.08f + 0.025f * v;
            float satVoice = tanhf(voiceOut * drive);
            voiceOut = 0.86f * voiceOut + 0.14f * satVoice;

            // Apply VCA
            voiceOut *= vca;

            lastEnv[v] = env;
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

            mixL += voiceOut * (0.5f - pan * 0.5f);
            mixR += voiceOut * (0.5f + pan * 0.5f);

            // ----------------------------------------------------
            // POLY TAPS
            // ----------------------------------------------------
            outputs[PITCH_POLY_OUTPUT].setVoltage(p, v);
            outputs[ENV_POLY_OUTPUT].setVoltage(lastEnv[v] * 10.f, v);
            outputs[VCA_POLY_OUTPUT].setVoltage(lastVca[v] * 10.f, v);
            outputs[FLT_POLY_OUTPUT].setVoltage(lastFlt[v] * 5.f,  v);
        }

        // --------------------------------------------------------
        // MONO‑CORE: warm the mid, keep the sides (both at the same, voice-normalized level)
        // --------------------------------------------------------
        float norm = 1.f / std::sqrt((float)std::max(voices, 1));
        mixL *= norm;
        mixR *= norm;

        float mid = 0.5f * (mixL + mixR);
        float side = 0.5f * (mixL - mixR);
        mid = mid * 0.78f + tanhf(mid * 0.9f) * 0.22f;

        // L/R were each half the voice level (pan gains sum to 1): bring them back up
        mixL = (mid + side) * 2.f;
        mixR = (mid - side) * 2.f;

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

                float depthMs = 3.0f * depthScale;

                chorusBufL[chorusWrite] = mixL;
                chorusBufR[chorusWrite] = mixR;

                float wetL = 0.f;
                float wetR = 0.f;

                for (int i = 0; i < tapsUsed; ++i) {

                    chorusPhase[i] += rates[i] * dt;
                    chorusPhase[i] -= std::floor(chorusPhase[i]);
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

                    wetL += chorusBufL[i0] + (chorusBufL[i1] - chorusBufL[i0]) * frac;
                    wetR += chorusBufR[i0] + (chorusBufR[i1] - chorusBufR[i0]) * frac;
                }

                wetL /= (float)tapsUsed;
                wetR /= (float)tapsUsed;

                float mixDepth = amount * 0.6f;

                mixL = mixL * (1.f - mixDepth) + wetL * mixDepth;
                mixR = mixR * (1.f - mixDepth) + wetR * mixDepth;

                chorusWrite++;
                if (chorusWrite >= CHORUS_BUF)
                    chorusWrite = 0;
            }
        }

        // --------------------------------------------------------
        // OUTPUT STAGE: soft limit to +-5 V (a second tanh used to squash it to +-1 V)
        // --------------------------------------------------------
        float volTrim = params[POLY_VOL_PARAM].getValue();
        float outL = 5.f * tanhf(mixL * 1.2f) * volTrim;
        float outR = 5.f * tanhf(mixR * 1.2f) * volTrim;

        outputs[POLY_AUDIO_L_OUTPUT].setVoltage(outL);
        outputs[POLY_AUDIO_R_OUTPUT].setVoltage(outR);

        // --------------------------------------------------------
        // LOCAL BUS WRITE
        // --------------------------------------------------------
        localBusPoly.L[0] = outL;
        localBusPoly.R[0] = outR;

        // --------------------------------------------------------
        // LIGHTS (every 32 samples)
        // --------------------------------------------------------
        float detuneLevel = clamp(detuneActivity / (std::max(voices, 1) * 6.f), 0.f, 1.f);
        if (!lightDivider.process())
            return;
        float ldt = dt * lightDivider.getDivision();

        updateTinyLEDs(ldt);
        lights[KNOB_CHORUS_LIGHT].setBrightness(smoothChorus);

        bool gateHigh = false;
        for (int v = 0; v < voices; ++v)
            gateHigh = gateHigh || gatePrev[v];
        lights[STATUS_LIGHT].setBrightness(gateHigh ? 1.f : 0.f);

        lights[CLAMP_LIGHT].setBrightness(anyClamp ? 1.f : 0.f);
        lights[SAFEMODE_LIGHT].setBrightness(detuneLevel);
        lights[MODE_LIGHT].setBrightness(chorusMode == 0 ? 0.f : 0.35f + 0.65f * chorusMode / 3.f);

        // ADSR stage lights follow the loudest voice
        int   activeVoice = 0;
        float maxEnv      = -1.f;
        for (int v = 0; v < voices; ++v) {
            if (envState[v] > maxEnv) {
                maxEnv      = envState[v];
                activeVoice = v;
            }
        }
        EnvStage st = envStage[activeVoice];
        lights[ATTACK_LIGHT].setSmoothBrightness(st == STAGE_ATTACK ? 1.f : 0.f, ldt);
        lights[DECAY_LIGHT].setSmoothBrightness(st == STAGE_DECAY ? 1.f : 0.f, ldt);
        lights[SUSTAIN_LIGHT].setSmoothBrightness(st == STAGE_SUSTAIN ? 1.f : 0.f, ldt);
        lights[RELEASE_LIGHT].setSmoothBrightness(st == STAGE_RELEASE ? 1.f : 0.f, ldt);
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

// ============================================================================
// INFO OLED: unison stack | chorus mode | chorus amount | voices (OledUi standard)
// ============================================================================
struct PolyOled : TransparentWidget {
    SynTHBloKPoly* module = NULL;

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1) drawOled(args);
        Widget::drawLayer(args, layer);
    }

    void drawOled(const DrawArgs& args) {
        static const char* stacks[SynTHBloKPoly::NUM_STACKS] = {
            "SING", "5TH", "OCT", "5+8", "OCT2", "MIN", "MAJ", "O+5", "STK5", "STK8", "FULL"};
        static const char* modes[4] = {"OFF", "LITE", "WIDE", "ENS"};

        // the module browser preview shows the defaults
        int preset = 0, mode = 0, voices = 1;
        float amt = 0.5f;
        if (module) {
            preset = clamp((int)std::round(module->params[SynTHBloKPoly::UNISON_PARAM].getValue()), 0, SynTHBloKPoly::NUM_STACKS - 1);
            mode = module->chorusMode;
            amt = module->params[SynTHBloKPoly::CHORUS_AMOUNT_PARAM].getValue();
            voices = module->activeVoices;
        }
        char amtS[8], vS[8];
        snprintf(amtS, sizeof amtS, "%d%%", (int)std::round(amt * 100.f));
        snprintf(vS, sizeof vS, "%d", voices);
        const char* names[4] = {"STK", "CHO", "AMT", "V"};
        const char* vals[4] = {stacks[preset], modes[clamp(mode, 0, 3)], amtS, vS};
        const float cols[5] = {0.f, 0.30f, 0.58f, 0.82f, 1.f};    // fractions of the width

        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        nvgBeginPath(args.vg);
        for (int i = 1; i < 4; i++)
            oled::divider(args, W * cols[i], 2.f, W * cols[i], H - 2.f);
        oled::dividerStroke(args);

        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        for (int i = 0; i < 4; i++) {
            float x = W * cols[i] + 3.f;
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFontSize(args.vg, 6.f);
            nvgFillColor(args.vg, oled::dim());
            nvgText(args.vg, x, H * 0.5f, names[i], NULL);
            float nameW = nvgTextBounds(args.vg, 0.f, 0.f, names[i], NULL, NULL);
            nvgFontSize(args.vg, 7.5f);
            nvgFillColor(args.vg, oled::amber());
            nvgText(args.vg, x + nameW + 2.f, H * 0.5f, vals[i], NULL);
        }
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

        {   // tiny RGB LEDs
            int nn; const SynTHBloKPoly::LedK* kk = SynTHBloKPoly::kLeds(nn);
            for (int i = 0; i < nn; i++)
                addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(kk[i].x, kk[i].y)), module, SynTHBloKPoly::KNOB_LED + i * 3));
            const SynTHBloKPoly::LedJ* jj = SynTHBloKPoly::jLeds(nn);
            for (int i = 0; i < nn; i++)
                addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(jj[i].x, jj[i].y)), module, SynTHBloKPoly::JACK_LED + i * 3));
        }

        // Inputs
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

        // Info OLED between the knob row and GAIN / L / R
        PolyOled* od = new PolyOled;
        od->module = module;
        od->box.pos  = mm(2.5f, 101.8f);
        od->box.size = mm(40.72f, oled::kRow);
        addChild(od);

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

        // Gate light
        addChild(createLightCentered<SmallLight<GreenLight>>(mm(28.f, 88.f), module, SynTHBloKPoly::STATUS_LIGHT));

        // Volume + audio outs
        addParam(createParamCentered<RoundSmallBlackKnob>(mm(8.f, 115.f), module, SynTHBloKPoly::POLY_VOL_PARAM));

        addOutput(createOutputCentered<BlackJack>(mm(28.f, 115.f), module, SynTHBloKPoly::POLY_AUDIO_L_OUTPUT));
        addOutput(createOutputCentered<BlackJack>(mm(38.f, 115.f), module, SynTHBloKPoly::POLY_AUDIO_R_OUTPUT));
    }
};



Model* modelSynTHBloKPoly = createModel<SynTHBloKPoly, SynTHBloKPolyWidget>("Poly");
