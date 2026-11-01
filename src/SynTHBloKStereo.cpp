#include "plugin.hpp"
#include "SynTHBloK.hpp"
#include "SynTHBloKStereo.hpp"
#include "SynTHBloKMix24.hpp"



// -----------------------------------------------------------------------------
// Stereo Module
// -----------------------------------------------------------------------------
struct SynTHBloKStereo : Module {
 SynTHBloKInternalBus localBusStereo;
    // --- FFT STATE ---
    static constexpr int SPEC_SIZE = 256;
    static constexpr int SPEC_BINS = 16;

    float specBuffer[SPEC_SIZE] = {};
    int   specIndex = 0;
    float specBins[SPEC_BINS] = {};

    // --- Persistent DSP state ---
    float modPhase = 0.f;

    float sLow = 0.f;
    float envM = 0.f;
    float envS = 0.f;

    float sHF = 0.f;

    float sBufSpread[256] = {};
    int   sIdxSpread = 0;
    int   sDelaySamples = 32;

    float lpL = 0.f;
    float lpR = 0.f;

    float deqL = 0.f;
    float deqR = 0.f;

    float airL = 0.f;
    float airR = 0.f;

    float compEnv = 0.f;

    float smoothL = 0.f;
    float smoothR = 0.f;

    float scEnv = 0.f;

    float lowMono = 0.f;

    // --- AP filters + micro delay ---
    float ap1 = 0.f;
    float ap2 = 0.f;
    float dBuf[64] = {};
    int dIdx = 0;

    enum ParamIds {
        WIDTH_PARAM,
        TILT_PARAM,
        AIR_PARAM,
        GLUE_PARAM,
        PUNCH_PARAM,
        CLIP_PARAM,
        OUT_LEVEL_PARAM,
        MIX_PARAM,
        MIX_MODE_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        MAIN_IN_INPUT,
        SIDECHAIN_INPUT,
        WIDTH_CV_INPUT,
        TILT_CV_INPUT,
        AIR_CV_INPUT,
        GLUE_CV_INPUT,
        PUNCH_CV_INPUT,
        CLIP_CV_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        L_OUT_OUTPUT,
        R_OUT_OUTPUT,
        SIDE_OUT_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        MAIN_IN_INPUT_LIGHT,
        WIDTH_LIGHT,
        TILT_LIGHT,
        AIR_LIGHT,
        GLUE_LIGHT,
        PUNCH_LIGHT,
        CLIP_LIGHT,
        OUT_LIGHT,
        STATUS_LIGHT,
        ENUMS(KNOB_LED, 7 * 3),   // tiny RGB LED per knob: width, tilt, air, glue, punch, clip, level
        ENUMS(IO_LED, 5 * 3),     // tiny RGB LED per jack: main in, side in, L, R, side out
        NUM_LIGHTS
    };

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

    inline void setParamLED(int baseId, float x) {   // blue -> yellow -> red with knob + CV position
        float w = clamp(x, 0.f, 1.f);
        float blue   = clamp(1.f - (w * 2.f), 0.f, 1.f);
        float red    = clamp((w - 0.5f) * 2.f, 0.f, 1.f);
        float yellow = clamp(1.f - fabsf(w - 0.5f) * 2.f, 0.f, 1.f);
        float rr, gg, bb; ledRamp(w, rr, gg, bb);
        lights[baseId + 0].setSmoothBrightness(rr, 1);
        lights[baseId + 1].setSmoothBrightness(gg, 1);
        lights[baseId + 2].setSmoothBrightness(bb, 1);
    }
    inline void setSignalLED(int baseId, float v) {   // signal polarity: orange = positive, blue = negative
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

    SynTHBloKStereo() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configInput(MAIN_IN_INPUT, "Main");
        configInput(SIDECHAIN_INPUT, "Side");
        configInput(WIDTH_CV_INPUT, "Width CV");
        configInput(TILT_CV_INPUT, "Tilt CV");
        configInput(AIR_CV_INPUT, "Air CV");
        configInput(GLUE_CV_INPUT, "Glue CV");
        configInput(PUNCH_CV_INPUT, "Punch CV");
        configInput(CLIP_CV_INPUT, "Clip CV");
        configOutput(L_OUT_OUTPUT, "Left");
        configOutput(R_OUT_OUTPUT, "Right");
        configOutput(SIDE_OUT_OUTPUT, "Side");
        configParam(WIDTH_PARAM, 0.f, 2.f, 1.f, "Width");
        configParam(TILT_PARAM, -1.f, 1.f, 0.f, "Tilt");
        configParam(AIR_PARAM, 0.f, 1.f, 0.f, "Air");
        configParam(GLUE_PARAM, 0.f, 1.f, 0.f, "Glue");
        configParam(PUNCH_PARAM, 0.f, 1.f, 0.f, "Punch");
        configParam(CLIP_PARAM, 0.f, 1.f, 0.f, "Clip");
        configParam(OUT_LEVEL_PARAM, 0.f, 2.f, 1.f, "Output level");
        configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Mix");
        configSwitch(MIX_MODE_PARAM, 0.f, 1.f, 0.f, "Mix mode", {"Pre", "Post"});
    }
    void process(const ProcessArgs& args) override {

        float dt = args.sampleTime;

        // ---------------------------------------------------------------------
        // INPUT + MIX SYSTEM (PRE)
        // ---------------------------------------------------------------------
        float in = inputs[MAIN_IN_INPUT].isConnected() ? inputs[MAIN_IN_INPUT].getVoltage() : 0.f;
        float sidechainIn = inputs[SIDECHAIN_INPUT].isConnected() ? inputs[SIDECHAIN_INPUT].getVoltage() : 0.f;

        float dryL = in;
        float dryR = in;

        float mixRaw = params[MIX_PARAM].getValue();
        float mix = powf(mixRaw, 3.f);

        int mixMode = (int)params[MIX_MODE_PARAM].getValue();

        if (mixMode == 0) {
            in = dryL * (1.f - mix) + in * mix;
        }

        // ---------------------------------------------------------------------
        // PARAMS + CV
        // ---------------------------------------------------------------------
        float width = params[WIDTH_PARAM].getValue();
        if (inputs[WIDTH_CV_INPUT].isConnected())
            width += inputs[WIDTH_CV_INPUT].getVoltage() * 0.2f;
        width = clamp(width, 0.f, 2.f);

        float tilt = params[TILT_PARAM].getValue();
        if (inputs[TILT_CV_INPUT].isConnected())
            tilt += inputs[TILT_CV_INPUT].getVoltage() * 0.2f;
        tilt = clamp(tilt, -1.f, 1.f);

        float air = params[AIR_PARAM].getValue();
        if (inputs[AIR_CV_INPUT].isConnected())
            air += inputs[AIR_CV_INPUT].getVoltage() * 0.2f;
        air = clamp(air, 0.f, 1.f);

        float glue = params[GLUE_PARAM].getValue();
        if (inputs[GLUE_CV_INPUT].isConnected())
            glue += inputs[GLUE_CV_INPUT].getVoltage() * 0.2f;
        glue = clamp(glue, 0.f, 1.f);

        float punch = params[PUNCH_PARAM].getValue();
        if (inputs[PUNCH_CV_INPUT].isConnected())
            punch += inputs[PUNCH_CV_INPUT].getVoltage() * 0.2f;
        punch = clamp(punch, 0.f, 1.f);

        float clip = params[CLIP_PARAM].getValue();
        if (inputs[CLIP_CV_INPUT].isConnected())
            clip += inputs[CLIP_CV_INPUT].getVoltage() * 0.2f;
        clip = clamp(clip, 0.f, 1.f);

        float outLevel = params[OUT_LEVEL_PARAM].getValue();
        outLevel = clamp(outLevel, 0.f, 2.f);

        // ---------------------------------------------------------------------
        // STEREO DSP CORE (DE‑METALLICIZED)
        // ---------------------------------------------------------------------
        modPhase += 0.3f * dt;
        if (modPhase > 1.f) modPhase -= 1.f;
        float mod = sinf(2.f * M_PI * modPhase);

        float x = in;

        // Softer all‑pass feedback to reduce metallic ringing
        float ap1Out = x + 0.45f * ap1;
        ap1 = x - 0.45f * ap1Out;

        float ap2Out = ap1Out + 0.35f * ap2;
        ap2 = ap1Out - 0.35f * ap2Out;

        // Shorter, gentler micro‑delay to avoid comb‑metallic tone
        float delay = 0.0003f + 0.0002f * mod;
        dBuf[dIdx] = ap2Out;
        int tap = dIdx - (int)(delay / dt);
        tap &= 63;
        float dOut = dBuf[tap];
        dIdx = (dIdx + 1) & 63;

        float S = dOut * 0.6f;
        float M = in;

        // WIDTH
        float fcLow = 200.f;
        float cLowW = expf(-2.f * M_PI * fcLow * dt);
        sLow = (1.f - cLowW) * S + cLowW * sLow;
        float sHigh = S - sLow;

        float normW = clamp(width * 0.5f, 0.f, 1.f);
        float widthLow  = 0.1f + 0.3f * (1.f - normW);
        float widthHigh = 0.6f + 1.4f * normW;

        S = sLow * widthLow + sHigh * widthHigh;

        // TRANSIENT SHAPING
        float atkMs = 2.0f, relMs = 80.0f;
        float atk = expf(-1.f / (0.001f * atkMs / dt));
        float rel = expf(-1.f / (0.001f * relMs / dt));

        float absM = fabsf(M);
        envM = (absM > envM) ? atk * envM + (1.f - atk) * absM
                             : rel * envM + (1.f - rel) * absM;

        float absS2 = fabsf(S);
        envS = (absS2 > envS) ? atk * envS + (1.f - atk) * absS2
                              : rel * envS + (1.f - rel) * absS2;

        float transM = M - envM * ((M >= 0.f) ? 1.f : -1.f);
        float transS = S - envS * ((S >= 0.f) ? 1.f : -1.f);

        float midTransGain  = 0.5f + punch * 1.0f;
        float sideTransGain = 0.5f + punch * 1.5f;

        M += transM * (midTransGain  - 1.f);
        S += transS * (sideTransGain - 1.f);

        // HARMONICS (tamed to avoid metallic fizz)
        float fcHF = 4000.f;
        float cHF = expf(-2.f * M_PI * fcHF * dt);
        sHF = (1.f - cHF) * S + cHF * sHF;
        float sHFband = S - sHF;

        float harmAmt = air * 0.15f;          // was 0.25f
        float sSat = tanhf(sHFband * 2.2f);   // was * 4.f
        S += sSat * harmAmt;

        // MICRO DELAY SPREAD (shorter to reduce combing)
        float spreadMs = 0.8f + normW * 1.0f;   // was 1.5f + normW * 1.5f
        int desiredSamples = (int)(spreadMs * 0.001f / dt + 0.5f);
        desiredSamples = clamp(desiredSamples, 1, 255);
        sDelaySamples = desiredSamples;

        int readIdxS = (sIdxSpread - sDelaySamples) & 255;
        float sDelayed = sBufSpread[readIdxS];
        sBufSpread[sIdxSpread] = S;
        sIdxSpread = (sIdxSpread + 1) & 255;

        float spreadMix = 0.15f + 0.35f * normW;
        S = S * (1.f - spreadMix) + sDelayed * spreadMix;

        // BACK TO L/R
        float L = M + S;
        float R = M - S;

        // TILT
        float fcTilt = 1600.f;
        float cTilt = expf(-2.f * M_PI * fcTilt * dt);

        lpL = (1.f - cTilt) * L + cTilt * lpL;
        lpR = (1.f - cTilt) * R + cTilt * lpR;

        float hpL = L - lpL;
        float hpR = R - lpR;

        float tiltGain = tilt * 0.8f;
        L = lpL * (1.f - tiltGain) + hpL * (1.f + tiltGain);
        R = lpR * (1.f - tiltGain) + hpR * (1.f + tiltGain);

        // DE-HARSH
        float fcDeq = 4000.f;
        float cDeq = expf(-2.f * M_PI * fcDeq * dt);

        deqL = (1.f - cDeq) * L + cDeq * deqL;
        deqR = (1.f - cDeq) * R + cDeq * deqR;

        float harshL = L - deqL;
        float harshR = R - deqR;

        float deqAmt = 0.08f;
        L -= harshL * deqAmt;
        R -= harshR * deqAmt;

        // AIR
        float airFreq = 12000.f;
        float cAir = expf(-2.f * M_PI * airFreq * dt);

        airL = (1.f - cAir) * L + cAir * airL;
        airR = (1.f - cAir) * R + cAir * airR;

        float airAmt = air * 0.5f;
        float excite = air * 0.15f;

        L += (L - airL) * airAmt + excite * (L - airL);
        R += (R - airR) * airAmt + excite * (R - airR);

        // GLUE COMP
        float detector = fabsf(0.5f * (L + R));
        float atkComp = 0.001f;
        float relComp = 0.050f;

        if (detector > compEnv)
            compEnv += atkComp * (detector - compEnv);
        else
            compEnv += relComp * (detector - compEnv);

        float threshold = 0.5f;
        float ratio = 2.f + glue * 6.f;
        float gain = 1.f;

        if (compEnv > threshold) {
            float over = compEnv - threshold;
            float reduced = threshold + over / ratio;
            float gainReduction = (compEnv > 1e-6f) ? (reduced / compEnv) : 1.f;
            gain = gainReduction;
        }

        float glueGain = 1.f + glue * (gain - 1.f);
        L *= glueGain;
        R *= glueGain;

        float makeup = 1.f + glue * 0.15f;
        L *= makeup;
        R *= makeup;

        // PUNCH
        float atkPunch = 0.0005f;
        float relPunch = 0.02f;

        if (L > smoothL)
            smoothL += atkPunch * (L - smoothL);
        else
            smoothL += relPunch * (L - smoothL);

        if (R > smoothR)
            smoothR += atkPunch * (R - smoothR);
        else
            smoothR += relPunch * (R - smoothR);

        float transientL = L - smoothL;
        float transientR = R - smoothR;

        L += transientL * punch;
        R += transientR * punch;

        // SIDECHAIN DUCK
        float scLevel = fabsf(sidechainIn);
        float atkSC = 0.001f;
        float relSC = 0.050f;

        if (scLevel > scEnv)
            scEnv += atkSC * (scLevel - scEnv);
        else
            scEnv += relSC * (scEnv - scLevel);

        float duckAmt = glue * 0.5f;
        float duckGain = 1.f - duckAmt * scEnv;
        duckGain = clamp(duckGain, 0.2f, 1.f);

        L *= duckGain;
        R *= duckGain;

        // CLIP
        float drive = 1.f + clip * 3.f;
        float asym = 0.02f * clip;

        float satL = tanhf((L + asym) * drive);
        float satR = tanhf((R - asym) * drive);

        L = satL;
        R = satR;

        // LOW MONO + HF WIDEN
        float lowCut = 120.f;
        float cLow = expf(-2.f * M_PI * lowCut * dt);

        lowMono = (1.f - cLow) * (0.5f * (L + R)) + cLow * lowMono;

        float highLL = L - lowMono;
        float highRR = R - lowMono;

        L = lowMono + highLL;
        R = lowMono + highRR;

        float widenHF = 1.f + width * 0.05f;
        L += highLL * (widenHF - 1.f);
        R += highRR * (widenHF - 1.f);

        // M/S LIMITER
        float mLim = 0.5f * (L + R);
        float sLim = 0.5f * (L - R);
        float peakMS = fmaxf(fabsf(mLim), fabsf(sLim));
        float ceiling = 0.95f;

        if (peakMS > ceiling && peakMS > 0.f) {
            float g = ceiling / peakMS;
            mLim *= g;
            sLim *= g;
        }

        // --- WET/DRY MIX ---
        if (mixMode == 0) {
            L = dryL * (1.f - mix) + L * mix;
            R = dryR * (1.f - mix) + R * mix;
        }
        else {
            L = L * mix + dryL * (1.f - mix);
            R = R * mix + dryR * (1.f - mix);
        }

        // ---------------------------------------------------------------------
        // MIX SYSTEM (POST)
        // ---------------------------------------------------------------------
        if (mixMode == 1) {
            L = dryL * (1.f - mix) + L * mix;
            R = dryR * (1.f - mix) + R * mix;
        }

        // ---------------------------------------------------------------------
        // OUTPUT
        // ---------------------------------------------------------------------
        L *= outLevel;
        R *= outLevel;

        outputs[L_OUT_OUTPUT].setVoltage(L);
        outputs[R_OUT_OUTPUT].setVoltage(R);
        outputs[SIDE_OUT_OUTPUT].setVoltage(S);

        // ---------------------------------------------------------------------
        // FFT ACCUMULATION (isolated)
        // ---------------------------------------------------------------------
        bool hasAudioSource =
            inputs[MAIN_IN_INPUT].isConnected() &&
            fabsf(in) > 1e-6f;

        if (!hasAudioSource) {
            for (int i = 0; i < SPEC_BINS; ++i)
                specBins[i] = 0.f;
            specIndex = 0;
        }
        else {
            float mono = 0.5f * (localBusStereo.L[0] + localBusStereo.R[0]);
            specBuffer[specIndex++] = mono;

            if (specIndex >= SPEC_SIZE) {
                specIndex = 0;

                int binSize = SPEC_SIZE / SPEC_BINS;

                for (int b = 0; b < SPEC_BINS; b++) {
                    float sum = 0.f;
                    int start = b * binSize;
                    int end   = start + binSize;

                    for (int i = start; i < end; i++)
                        sum += fabsf(specBuffer[i]);

                    specBins[b] = sum / binSize;
                }
            }
        }

        // ---------------------------------------------------------------------
        // LED / HALO MOD LOGIC
        // ---------------------------------------------------------------------
        float widthKnob  = params[WIDTH_PARAM].getValue();
        float tiltKnob   = params[TILT_PARAM].getValue();
        float airKnob    = params[AIR_PARAM].getValue();
        float glueKnob   = params[GLUE_PARAM].getValue();
        float punchKnob  = params[PUNCH_PARAM].getValue();
        float clipKnob   = params[CLIP_PARAM].getValue();
        float outKnob    = params[OUT_LEVEL_PARAM].getValue();

        float widthCV  = inputs[WIDTH_CV_INPUT].isConnected()  ? fabsf(inputs[WIDTH_CV_INPUT].getVoltage())  * 0.1f : 0.f;
        float tiltCV   = inputs[TILT_CV_INPUT].isConnected()   ? fabsf(inputs[TILT_CV_INPUT].getVoltage())   * 0.1f : 0.f;
        float airCV    = inputs[AIR_CV_INPUT].isConnected()    ? fabsf(inputs[AIR_CV_INPUT].getVoltage())    * 0.1f : 0.f;
        float glueCV   = inputs[GLUE_CV_INPUT].isConnected()   ? fabsf(inputs[GLUE_CV_INPUT].getVoltage())   * 0.1f : 0.f;
        float punchCV  = inputs[PUNCH_CV_INPUT].isConnected()  ? fabsf(inputs[PUNCH_CV_INPUT].getVoltage())  * 0.1f : 0.f;
        float clipCV   = inputs[CLIP_CV_INPUT].isConnected()   ? fabsf(inputs[CLIP_CV_INPUT].getVoltage())   * 0.1f : 0.f;

        float bw = clamp(widthKnob  * 0.5f + widthCV, 0.f, 1.f);
        float bt = clamp((tiltKnob + 1.f) * 0.5f + tiltCV, 0.f, 1.f);
        float ba = clamp(airKnob    + airCV, 0.f, 1.f);
        float bg = clamp(glueKnob   + glueCV, 0.f, 1.f);
        float bp = clamp(punchKnob  + punchCV, 0.f, 1.f);
        float bc = clamp(clipKnob   + clipCV, 0.f, 1.f);
        float bo = clamp(outKnob * 0.5f, 0.f, 1.f);

        lights[WIDTH_LIGHT].setBrightness(bw);
        lights[TILT_LIGHT].setBrightness(bt);
        lights[AIR_LIGHT].setBrightness(ba);
        lights[GLUE_LIGHT].setBrightness(bg);
        lights[PUNCH_LIGHT].setBrightness(bp);
        lights[CLIP_LIGHT].setBrightness(bc);
        lights[OUT_LIGHT].setBrightness(bo);
        {   // tiny LEDs: knob (+CV) position and jack signal
            const float kv[7] = {bw, bt, ba, bg, bp, bc, bo};
            for (int i = 0; i < 7; i++) setParamLED(KNOB_LED + i * 3, kv[i]);
            setSignalLED(IO_LED + 0 * 3, in);
            setSignalLED(IO_LED + 1 * 3, sidechainIn);
            setSignalLED(IO_LED + 2 * 3, L);
            setSignalLED(IO_LED + 3 * 3, R);
            setSignalLED(IO_LED + 4 * 3, S);
        }

        // ---------------------------------------------------------------------
        // INTERNAL BUS ISOLATION (Stereo)
        // ---------------------------------------------------------------------
        localBusStereo.L[0] = L;
        localBusStereo.R[0] = R;
    }

};

// -----------------------------------------------------------------------------
// HYBRID STEREO ANALYZER — A1 WARM ANALOG (FINAL CLEAN VERSION)
// -----------------------------------------------------------------------------
struct HybridStereoAnalyzer : TransparentWidget {
    SynTHBloKStereo* module = nullptr;

    static const int SCOPE_SIZE = 256;
    float scopeL[SCOPE_SIZE] = {};
    float scopeR[SCOPE_SIZE] = {};
    int scopeIndex = 0;

    HybridStereoAnalyzer(SynTHBloKStereo* m) {
        module = m;
    }

    inline void stepScope(float L, float R) {
        scopeL[scopeIndex] = L;
        scopeR[scopeIndex] = R;
        scopeIndex = (scopeIndex + 1) & (SCOPE_SIZE - 1);
    }

    inline NVGcolor bg() const {
        return nvgRGBA(0, 0, 0, 140);
    }

    // -------------------------------------------------------------------------
    // TOP (50%): BALANCE + WIDTH
    // -------------------------------------------------------------------------
    void drawVectorscope(const DrawArgs& args) {
        NVGcontext* vg = args.vg;

        float w = box.size.x;
        float h = box.size.y * 0.50f;
        float pad = 3.f;
        float innerW = w - pad * 2.f;

        // Background (rounded)
        nvgBeginPath(vg);
        nvgRoundedRect(vg, pad, 0, innerW, h, 3.f);
        nvgFillColor(vg, bg());
        nvgFill(vg);

        float L = module->localBusStereo.L[0];
        float R = module->localBusStereo.R[0];

        float maxLR = std::max(fabsf(L), fabsf(R));
        if (maxLR < 1e-6f) maxLR = 1.f;
        L /= maxLR;
        R /= maxLR;

        float S = 0.5f * (L - R);

        // BALANCE
        float balY = h * 0.08f;
        float balH = h * 0.45f;

        // Center line
        nvgBeginPath(vg);
        nvgMoveTo(vg, pad + innerW * 0.5f, balY);
        nvgLineTo(vg, pad + innerW * 0.5f, balY + balH);
        nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 50));
        nvgStrokeWidth(vg, 1.2f);
        nvgStroke(vg);

        // Balance bar
        float bal = clamp((L - R) * 0.5f, -1.f, 1.f);
        float balX = pad + (bal * 0.5f + 0.5f) * innerW;

        nvgBeginPath(vg);
        nvgRect(vg, balX - 4, balY, 8, balH);
        nvgFillColor(vg, nvgRGBA(255, 180, 80, 230));
        nvgFill(vg);

        // WIDTH
        float widY = h * 0.60f;
        float widH = h * 0.30f;

        float width = clamp(fabsf(S), 0.f, 1.f);
        float widW = width * innerW;

        NVGcolor widColor =
            (width < 0.3f) ? nvgRGBA(120, 180, 255, 200) :
            (width < 0.7f) ? nvgRGBA(255, 200, 120, 200) :
                             nvgRGBA(255, 120, 120, 200);

        nvgBeginPath(vg);
        nvgRect(vg, pad, widY, widW, widH);
        nvgFillColor(vg, widColor);
        nvgFill(vg);
    }

    // -------------------------------------------------------------------------
    // CORRELATION (15%)
    // -------------------------------------------------------------------------
    void drawCorrelation(const DrawArgs& args, float corr) {
        NVGcontext* vg = args.vg;

        float w = box.size.x;
        float pad = 3.f;
        float innerW = w - pad * 2.f;

        float y0 = box.size.y * 0.50f;
        float h  = box.size.y * 0.15f;

        // Background (rounded)
        nvgBeginPath(vg);
        nvgRoundedRect(vg, pad, y0, innerW, h, 3.f);
        nvgFillColor(vg, bg());
        nvgFill(vg);

        float norm = (corr + 1.f) * 0.5f;
        float barW = norm * innerW;

        NVGcolor c =
            (corr > 0.3f)  ? nvgRGBA(80, 255, 120, 200) :
            (corr > -0.3f) ? nvgRGBA(255, 200, 120, 200) :
                             nvgRGBA(255, 120, 120, 200);

        nvgBeginPath(vg);
        nvgRect(vg, pad, y0, barW, h);
        nvgFillColor(vg, c);
        nvgFill(vg);
    }

    // -------------------------------------------------------------------------
    // MID / SIDE (20%)
    // -------------------------------------------------------------------------
    void drawMidSide(const DrawArgs& args, float M, float S) {
        NVGcontext* vg = args.vg;

        float w = box.size.x;
        float pad = 3.f;
        float innerW = w - pad * 2.f;

        float y0 = box.size.y * 0.65f;
        float h  = box.size.y * 0.20f;

        float half = innerW * 0.5f;

        float mH = clamp(fabsf(M), 0.f, 1.f) * h;
        float sH = clamp(fabsf(S), 0.f, 1.f) * h;

        // Mid background (rounded)
        nvgBeginPath(vg);
        nvgRoundedRect(vg, pad, y0, half - 2, h, 3.f);
        nvgFillColor(vg, bg());
        nvgFill(vg);

        // Side background (rounded)
        nvgBeginPath(vg);
        nvgRoundedRect(vg, pad + half + 2, y0, half - 2, h, 3.f);
        nvgFillColor(vg, bg());
        nvgFill(vg);

        // Mid bar
        nvgBeginPath(vg);
        nvgRect(vg, pad, y0 + (h - mH), half - 2, mH);
        nvgFillColor(vg, nvgRGBA(120, 180, 255, 200));
        nvgFill(vg);

        // Side bar
        nvgBeginPath(vg);
        nvgRect(vg, pad + half + 2, y0 + (h - sH), half - 2, sH);
        nvgFillColor(vg, nvgRGBA(255, 150, 80, 200));
        nvgFill(vg);
    }
// -------------------------------------------------------------------------
// TILT (15%)
// -------------------------------------------------------------------------
void drawTilt(const DrawArgs& args, float tilt) {
    NVGcontext* vg = args.vg;

    float w = box.size.x;
    float pad = 3.f;
    float innerW = w - pad * 2.f;

    float y0 = box.size.y * 0.80f;
    float h  = box.size.y * 0.10f;

    // Background (rounded)
    nvgBeginPath(vg);
    nvgRoundedRect(vg, pad, y0, innerW, h, 3.f);
    nvgFillColor(vg, bg());
    nvgFill(vg);

    // ---------------------------------------------------------
    // WIDTH-STYLE TILT BAR (centered, expands left/right)
    // ---------------------------------------------------------
// clamp tilt (-1 to +1)
float t = clamp(tilt, -1.f, 1.f);

// center of the tilt section (LOWERED)
float cx = pad + innerW * 0.5f;
float cy = y0 + h * 0.78f;   // ← LOWER BAR POSITION (0.70–0.85 works great)

// max bar half-length
float maxLen = innerW * 0.45f;

// bar length (always positive)
float len = maxLen * fabsf(t);

// compute endpoints based on direction
float x1, x2;

if (t >= 0.f) {
    // tilt RIGHT → bar grows right from center
    x1 = cx;
    x2 = cx + len;
} else {
    // tilt LEFT → bar grows left from center
    x1 = cx - len;
    x2 = cx;
}
    // background reference line (full width, subtle)
    nvgBeginPath(vg);
    nvgMoveTo(vg, pad, cy);
    nvgLineTo(vg, pad + innerW, cy);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 30));
    nvgStrokeWidth(vg, 1.0f);
    nvgLineCap(vg, NVG_ROUND);
    nvgStroke(vg);

    // active bar (tilt amount)
    nvgBeginPath(vg);
    nvgMoveTo(vg, x1, cy);
    nvgLineTo(vg, x2, cy);
    nvgStrokeColor(vg, nvgRGBA(255, 200, 120, 220));
    nvgStrokeWidth(vg, 3.0f);
    nvgLineCap(vg, NVG_ROUND);
    nvgStroke(vg);
}

    // -------------------------------------------------------------------------
    // MASTER DRAW
    // -------------------------------------------------------------------------
   void draw(const DrawArgs& args) override {
    if (!module)
        return;

    NVGcontext* vg = args.vg;
    float w = box.size.x;
    float h = box.size.y;

    // ---------------------------------------------------------
    // BEZEL (rounded corners + warm border)
    // ---------------------------------------------------------
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0.f, 0.f, w, h, 6.f);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 140));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0.5f, 0.5f, w - 1.f, h - 1.f, 6.f);
    nvgStrokeColor(vg, nvgRGBA(255, 180, 90, 140));
    nvgStrokeWidth(vg, 1.2f);
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, 2.f, 2.f, w - 4.f, h - 4.f, 5.f);
    nvgStrokeColor(vg, nvgRGBA(255, 200, 120, 60));
    nvgStrokeWidth(vg, 0.8f);
    nvgStroke(vg);

    // ---------------------------------------------------------
    // CLIP INTERNAL DRAWING (Rack-safe)
    // ---------------------------------------------------------
    nvgSave(vg); // SCISSOR SAVE
    nvgIntersectScissor(vg, 2.f, 2.f, w - 4.f, h - 4.f);

    // ---------------------------------------------------------
    // SCALE INTERNAL CONTENT (makes whole analyzer smaller)
    // ---------------------------------------------------------
    float scaleX = 1.0f;
    float scaleY = 0.80f;

    float offsetX = (1.f - scaleX) * 0.5f * w;
    float offsetY = (1.f - scaleY) * 0.5f * h;

    nvgSave(vg);  // SCALE SAVE
    nvgTranslate(vg, offsetX, offsetY);
    nvgScale(vg, scaleX, scaleY);

    // ---------------------------------------------------------
    // INTERNAL DRAW
    // ---------------------------------------------------------
    float L = module->localBusStereo.L[0];
    float R = module->localBusStereo.R[0];

    stepScope(L, R);

    float M = 0.5f * (L + R);
    float S = 0.5f * (L - R);

    float denom = (fabsf(L) + fabsf(R)) * 0.5f + 1e-6f;
    float corr = clamp((L * R) / (denom * denom), -1.f, 1.f);

    drawVectorscope(args);
    drawCorrelation(args, corr);
    drawMidSide(args, M, S);
    drawTilt(args, module->params[SynTHBloKStereo::TILT_PARAM].getValue());

    nvgRestore(vg);   // SCALE RESTORE
    nvgRestore(vg);   // SCISSOR RESTORE
}
};

// -----------------------------------------------------------------------------
// Halo Light (unchanged)
// -----------------------------------------------------------------------------
struct HaloLight : app::ModuleLightWidget {
    HaloLight() {
        box.size = Vec(28.f, 28.f);
    }
    void draw(const DrawArgs& args) override {
        if (!module)
            return;

        float b = 0.f;
        if (firstLightId >= 0)
            b = module->lights[firstLightId].value;

        float t = clamp(powf(b, 0.40f), 0.f, 1.f);
        t = t * 0.90f;

        NVGcolor g[24] = {
            nvgRGBf(1.00f, 0.03f, 0.03f),
            nvgRGBf(1.00f, 0.05f, 0.03f),
            nvgRGBf(1.00f, 0.08f, 0.03f),
            nvgRGBf(1.00f, 0.12f, 0.03f),
            nvgRGBf(1.00f, 0.16f, 0.03f),
            nvgRGBf(1.00f, 0.20f, 0.03f),
            nvgRGBf(1.00f, 0.24f, 0.03f),
            nvgRGBf(1.00f, 0.28f, 0.03f),

            nvgRGBf(1.00f, 0.34f, 0.03f),
            nvgRGBf(1.00f, 0.40f, 0.03f),
            nvgRGBf(1.00f, 0.46f, 0.03f),
            nvgRGBf(1.00f, 0.52f, 0.03f),
            nvgRGBf(1.00f, 0.58f, 0.04f),
            nvgRGBf(1.00f, 0.62f, 0.05f),
            nvgRGBf(1.00f, 0.66f, 0.06f),
            nvgRGBf(1.00f, 0.70f, 0.07f),

            nvgRGBf(1.00f, 0.74f, 0.10f),
            nvgRGBf(1.00f, 0.78f, 0.13f),
            nvgRGBf(1.00f, 0.82f, 0.16f),
            nvgRGBf(1.00f, 0.86f, 0.20f),
            nvgRGBf(1.00f, 0.89f, 0.24f),
            nvgRGBf(1.00f, 0.92f, 0.28f),

            nvgRGBf(1.00f, 0.94f, 0.33f),
            nvgRGBf(1.00f, 0.97f, 0.38f)
        };

        float seg = t * 23.0f;
        int i = (int)seg;
        float k = seg - i;

        NVGcolor c;
        if (i >= 23)
            c = g[23];
        else
            c = nvgRGBf(
                g[i].r + (g[i+1].r - g[i].r) * k,
                g[i].g + (g[i+1].g - g[i].g) * k,
                g[i].b + (g[i+1].b - g[i].b) * k
            );

        float fire =
            0.90f +
            0.12f * sinf(80.f * t + 25.f * b) +
            0.06f * sinf(14.f * t + 3.f * b) +
            0.04f * sinf(200.f * b);

        float bloom = powf(b, 1.3f) * 2.0f;
        float alpha = clamp(0.55f + bloom * 0.45f, 0.f, 1.f) * fire;

        float shimmer = 0.02f * sinf(150.f * b + t * 10.f);
        float r = 13.f + shimmer + bloom * 0.30f;

        nvgSave(args.vg);

        float cx = box.size.x * 0.5f;
        float cy = box.size.y * 0.5f;

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, r);
        nvgStrokeWidth(args.vg, 2.0f);
        nvgStrokeColor(args.vg, c);
        nvgGlobalAlpha(args.vg, alpha);
        nvgStroke(args.vg);

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, r * 0.65f);
        nvgStrokeWidth(args.vg, 1.5f);
        nvgStrokeColor(args.vg, nvgRGBf(1.0f, 0.88f, 0.30f));
        nvgGlobalAlpha(args.vg, alpha * 0.45f);
        nvgStroke(args.vg);

        nvgRestore(args.vg);
    }
};

// -----------------------------------------------------------------------------
// Widget
// -----------------------------------------------------------------------------
struct SynTHBloKStereoWidget : ModuleWidget {
    SynTHBloKStereoWidget(SynTHBloKStereo* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKStereo.svg")));

        using namespace rack::componentlibrary;

        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(57.46f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 125.0f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(57.46f, 125.0f))));

addParam(createParamCentered<Trimpot>(mm2px(Vec(47.f, 99.0f)), module, SynTHBloKStereo::MIX_PARAM));
addInput(createInputCentered<BlackJack>(mm2px(Vec(9.004f, 18.770f)), module, SynTHBloKStereo::MAIN_IN_INPUT));
addInput(createInputCentered<BlackJack>(mm2px(Vec(50.897f, 18.770f)), module, SynTHBloKStereo::SIDECHAIN_INPUT));

addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(30.48f, 38.949f)), module, SynTHBloKStereo::WIDTH_PARAM));
addInput(createInputCentered<BlackJack>(mm2px(Vec(30.48f, 53.0f)), module, SynTHBloKStereo::WIDTH_CV_INPUT));

addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(9.004f, 35.666f)), module, SynTHBloKStereo::TILT_PARAM));
addInput(createInputCentered<BlackJack>(mm2px(Vec(9.004f, 51.8f)), module, SynTHBloKStereo::TILT_CV_INPUT));

addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(51.426f, 35.666f)), module, SynTHBloKStereo::AIR_PARAM));
addInput(createInputCentered<BlackJack>(mm2px(Vec(51.426f, 51.8f)), module, SynTHBloKStereo::AIR_CV_INPUT));

addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(14.f, 67.5f)), module, SynTHBloKStereo::GLUE_PARAM));
addInput(createInputCentered<BlackJack>(mm2px(Vec(14.f, 81.7f)), module, SynTHBloKStereo::GLUE_CV_INPUT));

addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(30.48f, 67.5f)), module, SynTHBloKStereo::PUNCH_PARAM));
addInput(createInputCentered<BlackJack>(mm2px(Vec(30.48f, 81.7f)), module, SynTHBloKStereo::PUNCH_CV_INPUT));

addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(47.f, 67.5f)), module, SynTHBloKStereo::CLIP_PARAM));
addInput(createInputCentered<BlackJack>(mm2px(Vec(47.f, 81.7f)), module, SynTHBloKStereo::CLIP_CV_INPUT));

addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(30.48f, 99.0f)), module, SynTHBloKStereo::OUT_LEVEL_PARAM));

addOutput(createOutputCentered<BlackJack>(mm2px(Vec(14.f, 115.4f)), module, SynTHBloKStereo::L_OUT_OUTPUT));
addOutput(createOutputCentered<BlackJack>(mm2px(Vec(30.48f, 115.4f)), module, SynTHBloKStereo::R_OUT_OUTPUT));
addOutput(createOutputCentered<BlackJack>(mm2px(Vec(47.f, 115.4f)), module, SynTHBloKStereo::SIDE_OUT_OUTPUT));


// tiny knob LEDs (position + CV) and jack signal LEDs
addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(9.004f, 46.7f)), module, SynTHBloKStereo::KNOB_LED + 1 * 3));
addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(30.48f, 47.3f)), module, SynTHBloKStereo::KNOB_LED + 0 * 3));
addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(51.426f, 46.7f)), module, SynTHBloKStereo::KNOB_LED + 2 * 3));
addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(14.f, 77.4f)), module, SynTHBloKStereo::KNOB_LED + 3 * 3));
addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(30.48f, 77.4f)), module, SynTHBloKStereo::KNOB_LED + 4 * 3));
addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(47.f, 77.4f)), module, SynTHBloKStereo::KNOB_LED + 5 * 3));
addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(30.48f, 110.0f)), module, SynTHBloKStereo::KNOB_LED + 6 * 3));
addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(9.004f, 26.6f)), module, SynTHBloKStereo::IO_LED + 0 * 3));
addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(50.897f, 26.6f)), module, SynTHBloKStereo::IO_LED + 1 * 3));
addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(14.f, 123.4f)), module, SynTHBloKStereo::IO_LED + 2 * 3));
addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(30.48f, 123.4f)), module, SynTHBloKStereo::IO_LED + 3 * 3));
addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(47.f, 123.4f)), module, SynTHBloKStereo::IO_LED + 4 * 3));

if (module) {
auto* analyzer = new HybridStereoAnalyzer(module);
analyzer->box.pos  = mm2px(Vec(18.f, 15.f));
analyzer->box.size = mm2px(Vec(24.f, 12.f));
addChild(analyzer);
}

    }
};

Model* modelSynTHBloKStereo =
    createModel<SynTHBloKStereo, SynTHBloKStereoWidget>("SynTHBloKStereo");
