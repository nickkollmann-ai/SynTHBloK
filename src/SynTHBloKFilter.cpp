#include "plugin.hpp"

struct SynTHBloKFILTERModule : Module {

    enum ParamIds {
        CUTOFF_PARAM,
        RES_PARAM,
        DRIVE_PARAM,
        DECAY_PARAM,
        MODE_PARAM,
        MIX_PARAM,
        STRIKE_PARAM,
        NUM_PARAMS
    };

    enum InputIds {
        CUTOFF_KNOB_CV_INPUT,
        RES_KNOB_CV_INPUT,
        DRIVE_KNOB_CV_INPUT,
        MIX_KNOB_CV_INPUT,

        IN_L_INPUT,
        IN_R_INPUT,
        CUTOFF_CV_INPUT,
        RES_CV_INPUT,
        VOCT_INPUT,
        DRIVE_CV_INPUT,
        DECAY_CV_INPUT,
        STRIKE_CV_INPUT,
        NUM_INPUTS
    };

    enum OutputIds {

        OUT_L_OUTPUT,
        OUT_R_OUTPUT,
        ENV_OUTPUT,
        HP_OUTPUT,
        NUM_OUTPUTS
    };

    enum LightIds {

        // KNOB LEDs (RGB groups)
        CUTOFF_LIGHT_R, CUTOFF_LIGHT_G, CUTOFF_LIGHT_B,
        RES_LIGHT_R,    RES_LIGHT_G,    RES_LIGHT_B,
        DRIVE_LIGHT_R,  DRIVE_LIGHT_G,  DRIVE_LIGHT_B,
        MIX_LIGHT_R,    MIX_LIGHT_G,    MIX_LIGHT_B,

        // JACK LEDs (RGB groups)
        IN_L_LED_R, IN_L_LED_G, IN_L_LED_B,
        IN_R_LED_R, IN_R_LED_G, IN_R_LED_B,
        CUTOFF_CV_LED_R, CUTOFF_CV_LED_G, CUTOFF_CV_LED_B,
        RES_CV_LED_R,    RES_CV_LED_G,    RES_CV_LED_B,

        VOCT_LED_R, VOCT_LED_G, VOCT_LED_B,
        DRIVE_CV_LED_R, DRIVE_CV_LED_G, DRIVE_CV_LED_B,
        DECAY_CV_LED_R, DECAY_CV_LED_G, DECAY_CV_LED_B,
        STRIKE_CV_LED_R, STRIKE_CV_LED_G, STRIKE_CV_LED_B,

        OUT_L_LED_R, OUT_L_LED_G, OUT_L_LED_B,
        OUT_R_LED_R, OUT_R_LED_G, OUT_R_LED_B,
        ENV_LED_R,   ENV_LED_G,   ENV_LED_B,
        HP_OUT_LED_R, HP_OUT_LED_G, HP_OUT_LED_B,

        // BUTTON LEDs (RGB groups)
        DRIVE_MODE_LIGHT_R, DRIVE_MODE_LIGHT_G, DRIVE_MODE_LIGHT_B,
        BODY_MODE_LIGHT_R,  BODY_MODE_LIGHT_G,  BODY_MODE_LIGHT_B,

        NUM_LIGHTS
    };

    // --- State ---
    bool modeButtonPrev = false;
    int bodyType = 0;

    bool strikeLast = false;
    dsp::SchmittTrigger strikeGateTrig;
    float exciteEnv = 0.f;

    // Coefficients are recomputed only when their inputs move
    float cBase = -1.f, cRes = -1.f, cDecay = -1.f, cSr = -1.f;
    int cBody = -1;
    float exciteLED = 0.f;

    // LED smoothing
    float cutoffLED = 0.f;
    float resLED    = 0.f;
    float driveLED  = 0.f;
    float mixLED    = 0.f;

    float inL_LED = 0.f;
    float inR_LED = 0.f;
    float cutoffCV_LED = 0.f;
    float resCV_LED = 0.f;

    float voctLED      = 0.f;
    float driveCV_LED  = 0.f;
    float decayCV_LED  = 0.f;
    float strikeCV_LED = 0.f;

    float outL_LED  = 0.f;
    float outR_LED  = 0.f;
    float envOutLED = 0.f;
    float hpOutLED  = 0.f;

    // Modal resonator
    static constexpr int NUM_MODES = 4;
    float modalYL[NUM_MODES][2] = {};
    float modalYR[NUM_MODES][2] = {};

    // Membrane LPF states
    float memLP_L = 0.f;
    float memLP_R = 0.f;

    // ------------------------------------------------------------
    // LED helper functions (Noizee-style)
    // ------------------------------------------------------------
    inline void setRampMonoLED(int baseId, float v) {   // same brightness behaviour as setMonoLED, ramp colour
        float b = clamp(v, 0.f, 1.f);
        float rr, gg, bb; ledRamp(b, rr, gg, bb);
        lights[baseId + 0].setSmoothBrightness(rr * b, 1.f);
        lights[baseId + 1].setSmoothBrightness(gg * b, 1.f);
        lights[baseId + 2].setSmoothBrightness(bb * b, 1.f);
    }

    inline void setMonoLED(int baseId, float v) {
        float b = clamp(v, 0.f, 1.f);
        lights[baseId + 0].setSmoothBrightness(b, 1.f);
        lights[baseId + 1].setSmoothBrightness(b * 0.6f, 1.f);
        lights[baseId + 2].setSmoothBrightness(0.f, 1.f);
    }

    inline void setBipolarLED(int baseId, float v) {
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

    inline void setWidthStyleLED(int baseId, float x) {
        float w = clamp(x, 0.f, 1.f);
        float blue   = clamp(1.f - (w * 2.f), 0.f, 1.f);
        float red    = clamp((w - 0.5f) * 2.f, 0.f, 1.f);
        float yellow = clamp(1.f - fabsf(w - 0.5f) * 2.f, 0.f, 1.f);
        float rr, gg, bb; ledRamp(w, rr, gg, bb);
        lights[baseId + 0].setSmoothBrightness(rr, 1);
        lights[baseId + 1].setSmoothBrightness(gg, 1);
        lights[baseId + 2].setSmoothBrightness(bb, 1);
    }

    SynTHBloKFILTERModule() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configParam(CUTOFF_PARAM, 0.f, 1.f, 0.5f, "Body tuning");
        configParam(RES_PARAM,    0.f, 1.f, 0.5f, "Resonance");
        configParam(DRIVE_PARAM,  0.f, 1.f, 0.4f, "Exciter strength");
        configParam(DECAY_PARAM,  0.f, 1.f, 0.4f, "Material damping");
        configParam(MODE_PARAM,   0.f, 1.f, 0.f, "Body type");
        configParam(MIX_PARAM,    0.f, 1.f, 0.5f, "Dry/wet");
        configButton(STRIKE_PARAM, "Strike");
configInput(CUTOFF_KNOB_CV_INPUT, "Cutoff knob CV");
configInput(RES_KNOB_CV_INPUT,    "Resonance knob CV");
configInput(DRIVE_KNOB_CV_INPUT,  "Exciter knob CV");
configInput(MIX_KNOB_CV_INPUT,    "Mix knob CV");
        configInput(IN_L_INPUT, "In L");
        configInput(IN_R_INPUT, "In R");
        configInput(CUTOFF_CV_INPUT, "Tuning CV");
        configInput(RES_CV_INPUT, "Resonance CV");
        configInput(VOCT_INPUT, "V/Oct");
        configInput(DRIVE_CV_INPUT, "Exciter CV");
        configInput(DECAY_CV_INPUT, "Damping CV");
        configInput(STRIKE_CV_INPUT, "Strike CV");

        configOutput(OUT_L_OUTPUT, "Out L");
        configOutput(OUT_R_OUTPUT, "Out R");
        configOutput(ENV_OUTPUT, "Excitation env");
        configOutput(HP_OUTPUT, "Body tap");
        
    }

    // ------------------------------------------------------------
    // Modal resonator helpers
    // ------------------------------------------------------------
    struct ModalCoeffs {
        float a1[NUM_MODES];
        float a2[NUM_MODES];
        float g[NUM_MODES];
    };
    ModalCoeffs coeffs{};

    ModalCoeffs computeModalCoeffs(float baseFreq, float resAmount, float decayParam, float sr) {
        ModalCoeffs c{};

        if (baseFreq < 20.f) baseFreq = 20.f;
        if (baseFreq > 8000.f) baseFreq = 8000.f;

        float ratios[NUM_MODES];

        switch (bodyType) {
            case 0: ratios[0]=1.f; ratios[1]=2.71f; ratios[2]=3.98f; ratios[3]=5.62f; break;
            case 1: ratios[0]=1.f; ratios[1]=2.f;   ratios[2]=3.01f; ratios[3]=4.05f; break;
            case 2: ratios[0]=1.f; ratios[1]=1.59f; ratios[2]=2.14f; ratios[3]=2.30f; break;
            default: ratios[0]=1.f; ratios[1]=3.f; ratios[2]=5.f; ratios[3]=7.f; break;
        }

        float modeGain[NUM_MODES];

        switch (bodyType) {
            case 0: modeGain[0]=1.0f; modeGain[1]=1.8f; modeGain[2]=2.4f; modeGain[3]=3.0f; break;
            case 1: modeGain[0]=2.0f; modeGain[1]=1.2f; modeGain[2]=0.7f; modeGain[3]=0.3f; break;
            case 2: modeGain[0]=2.2f; modeGain[1]=1.8f; modeGain[2]=0.6f; modeGain[3]=0.4f; break;
            default: modeGain[0]=2.0f; modeGain[1]=0.3f; modeGain[2]=1.6f; modeGain[3]=0.2f; break;
        }

        float minBw = 20.f;
        float maxBw = 2000.f;
        float bwBase = minBw + (1.f - decayParam) * (maxBw - minBw);

        for (int i = 0; i < NUM_MODES; i++) {
            float f = baseFreq * ratios[i];
            if (f > sr * 0.45f) f = sr * 0.45f;

            float w0 = 2.f * M_PI * f / sr;
            float bw = bwBase * (1.f + 0.2f * i);

            float dampingScale = 1.f;

            if (bodyType == 2) dampingScale = 1.8f + i * 0.8f;
            else if (bodyType == 0) dampingScale = 0.4f + i * 0.1f;
            else if (bodyType == 1) dampingScale = 0.8f + i * 0.2f;
            else dampingScale = 0.5f + i * 0.05f;

            float r = expf(-M_PI * bw * dampingScale / sr);

            c.a1[i] = 2.f * r * cosf(w0);
            c.a2[i] = -r * r;

            c.g[i] = modeGain[i] * (0.3f + resAmount * 1.7f);
        }

        return c;
    }

    float processModalBank(float in, float (&y)[NUM_MODES][2], const ModalCoeffs& c) {
        float out = 0.f;

        for (int i = 0; i < NUM_MODES; i++) {
            float y0 = c.g[i] * in + c.a1[i] * y[i][0] + c.a2[i] * y[i][1];
            y0 = tanhf(y0 * 0.8f);
            y[i][1] = y[i][0];
            y[i][0] = y0;
            out += y0;
        }

        return out;
    }

    // ------------------------------------------------------------
    // Exciter
    // ------------------------------------------------------------
    float randomNoise() {
        return random::uniform() * 2.f - 1.f;
    }

    float makeExciter(float in, bool strike, float drive, float sr) {
        float base = in * (0.3f + drive * 0.7f);

        if (strike) exciteEnv += 1.f;

        float envDecay = 0.0025f + (1.f - drive) * 0.01f;
        float envCoeff = expf(-1.f / (sr * envDecay));
        exciteEnv *= envCoeff;
        exciteEnv = clamp(exciteEnv, 0.f, 1.5f);

        float e = exciteEnv;
        float exc = 0.f;

        switch (bodyType) {
            case 0: exc = randomNoise() * (0.6f + drive * 1.4f) * e * 1.6f; break;
            case 1: exc = (in + randomNoise()*0.3f) * (0.5f + drive) * e * 1.3f; break;
            case 2: exc = (in*0.8f + randomNoise()*0.5f) * (0.6f + drive*1.2f) * e * 1.8f; break;
            default: exc = (randomNoise()*0.5f + in*0.3f) * (0.4f + drive*1.4f) * e * 1.4f; break;
        }

        return base + exc;
    }

    // ------------------------------------------------------------
    // PROCESS
    // ------------------------------------------------------------
    // Save / load the body type (Metal/Wood/Membrane/Tube) with the patch
    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "bodyType", json_integer(bodyType));
        return rootJ;
    }
    void dataFromJson(json_t* rootJ) override {
        json_t* j = json_object_get(rootJ, "bodyType");
        if (j) bodyType = clamp((int)json_integer_value(j), 0, 3);
    }

    void process(const ProcessArgs& args) override {
        float sr = args.sampleRate;

        float cutoff = params[CUTOFF_PARAM].getValue();
        float res    = params[RES_PARAM].getValue();
        float drive  = params[DRIVE_PARAM].getValue();
        float decay  = params[DECAY_PARAM].getValue();
        float mix    = params[MIX_PARAM].getValue();

        if (inputs[CUTOFF_CV_INPUT].isConnected()) cutoff += inputs[CUTOFF_CV_INPUT].getVoltage() * 0.1f;
        if (inputs[RES_CV_INPUT].isConnected())    res    += inputs[RES_CV_INPUT].getVoltage() * 0.1f;
        if (inputs[DRIVE_CV_INPUT].isConnected())  drive  += inputs[DRIVE_CV_INPUT].getVoltage() * 0.1f;
        if (inputs[DECAY_CV_INPUT].isConnected())  decay  += inputs[DECAY_CV_INPUT].getVoltage() * 0.1f;
// New dedicated knob CV row (placed after main CVs, before clamping)
if (inputs[CUTOFF_KNOB_CV_INPUT].isConnected())
    cutoff += inputs[CUTOFF_KNOB_CV_INPUT].getVoltage() * 0.1f;

if (inputs[RES_KNOB_CV_INPUT].isConnected())
    res += inputs[RES_KNOB_CV_INPUT].getVoltage() * 0.1f;

if (inputs[DRIVE_KNOB_CV_INPUT].isConnected())
    drive += inputs[DRIVE_KNOB_CV_INPUT].getVoltage() * 0.1f;

if (inputs[MIX_KNOB_CV_INPUT].isConnected())
    mix += inputs[MIX_KNOB_CV_INPUT].getVoltage() * 0.1f;

        cutoff = clamp(cutoff, 0.f, 1.f);
        res    = clamp(res,    0.f, 1.f);
        drive  = clamp(drive,  0.f, 1.f);
        decay  = clamp(decay,  0.f, 1.f);
        mix    = clamp(mix,    0.f, 1.f);

        bool pressed = params[MODE_PARAM].getValue() > 0.5f;
        if (pressed && !modeButtonPrev) bodyType = (bodyType + 1) % 4;
        modeButtonPrev = pressed;

        bool strikeButton = params[STRIKE_PARAM].getValue() > 0.5f;
        // Rising edge only, so a held gate strikes once
        bool strikeGate   = strikeGateTrig.process(inputs[STRIKE_CV_INPUT].getVoltage(), 0.1f, 1.f);
        bool strike = (strikeButton && !strikeLast) || strikeGate;
        strikeLast = strikeButton;

        float inL = inputs[IN_L_INPUT].isConnected() ? inputs[IN_L_INPUT].getVoltage() : 0.f;
        float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : inL;

        float baseFreq = 20.f * powf(400.f, cutoff);
        // V/Oct: 1 V doubles the body pitch
        if (inputs[VOCT_INPUT].isConnected())
            baseFreq *= dsp::exp2_taylor5(clamp(inputs[VOCT_INPUT].getVoltage(), -5.f, 5.f));

        if (baseFreq != cBase || res != cRes || decay != cDecay || sr != cSr || bodyType != cBody) {
            coeffs = computeModalCoeffs(baseFreq, res, decay, sr);
            cBase = baseFreq; cRes = res; cDecay = decay; cSr = sr; cBody = bodyType;
        }

        float excL = makeExciter(inL, strike, drive, sr);
        float excR = makeExciter(inR, strike, drive, sr);

        if (bodyType == 2) {
            memLP_L += 0.2f * (excL - memLP_L);
            memLP_R += 0.2f * (excR - memLP_R);
            excL = memLP_L;
            excR = memLP_R;
        }

        float bodyL = processModalBank(excL, modalYL, coeffs);
        float bodyR = processModalBank(excR, modalYR, coeffs);

        switch (bodyType) {
            case 0: bodyL*=0.90f; bodyR*=1.10f; break;
            case 1: bodyL*=0.98f; bodyR*=1.02f; break;
            case 2: bodyL*=0.94f; bodyR*=1.06f; break;
            default: bodyL*=0.85f; bodyR*=1.15f; break;
        }

        float limit = 12.f;
        bodyL = clamp(bodyL, -limit, limit);
        bodyR = clamp(bodyR, -limit, limit);

        float wet = mix * mix;
        float dry = 1.f - wet;

        float outL = dry * inL + wet * bodyL;
        float outR = dry * inR + wet * bodyR;

        outL = tanhf(outL * 0.5f) * 2.f;
        outR = tanhf(outR * 0.5f) * 2.f;

        outputs[OUT_L_OUTPUT].setVoltage(outL);
        outputs[OUT_R_OUTPUT].setVoltage(outR);
        outputs[ENV_OUTPUT].setVoltage(clamp(exciteEnv, 0.f, 1.f) * 5.f);
        outputs[HP_OUTPUT].setVoltage(bodyL);

        // ------------------------------------------------------------
        // LED LOGIC (using helpers)
        // ------------------------------------------------------------

        // KNOB LEDs
        cutoffLED = 0.15f * cutoffLED + 0.85f * cutoff;
        resLED    = 0.15f * resLED    + 0.85f * res;
        driveLED  = 0.15f * driveLED  + 0.85f * drive;
        mixLED    = 0.15f * mixLED    + 0.85f * mix;

        setRampMonoLED(CUTOFF_LIGHT_R, cutoffLED);
        setRampMonoLED(RES_LIGHT_R,    resLED);
        setRampMonoLED(DRIVE_LIGHT_R,  driveLED);
        setWidthStyleLED(MIX_LIGHT_R, mixLED);

        // JACK LEDs — inputs
        inL_LED = 0.2f * inL_LED + 0.8f * clamp(fabs(inL) * 0.1f, 0.f, 1.f);
        inR_LED = 0.2f * inR_LED + 0.8f * clamp(fabs(inR) * 0.1f, 0.f, 1.f);
        setBipolarLED(IN_L_LED_R, inL);
        setBipolarLED(IN_R_LED_R, inR);

        // CV presence LEDs
        cutoffCV_LED = 0.2f * cutoffCV_LED + 0.8f * (inputs[CUTOFF_CV_INPUT].isConnected() ? 1.f : 0.f);
        resCV_LED    = 0.2f * resCV_LED    + 0.8f * (inputs[RES_CV_INPUT].isConnected() ? 1.f : 0.f);
        voctLED      = 0.2f * voctLED      + 0.8f * (inputs[VOCT_INPUT].isConnected() ? 1.f : 0.f);
        driveCV_LED  = 0.2f * driveCV_LED  + 0.8f * (inputs[DRIVE_CV_INPUT].isConnected() ? 1.f : 0.f);
        decayCV_LED  = 0.2f * decayCV_LED  + 0.8f * (inputs[DECAY_CV_INPUT].isConnected() ? 1.f : 0.f);
        strikeCV_LED = 0.2f * strikeCV_LED + 0.8f * (inputs[STRIKE_CV_INPUT].isConnected() ? 1.f : 0.f);

        setMonoLED(CUTOFF_CV_LED_R, cutoffCV_LED);
        setMonoLED(RES_CV_LED_R,    resCV_LED);
        setMonoLED(VOCT_LED_R,      voctLED);
        setMonoLED(DRIVE_CV_LED_R,  driveCV_LED);
        setMonoLED(DECAY_CV_LED_R,  decayCV_LED);
        setMonoLED(STRIKE_CV_LED_R, strikeCV_LED);

        // Outputs / env / HP
        outL_LED  = 0.2f * outL_LED  + 0.8f * clamp(fabs(outL) * 0.1f, 0.f, 1.f);
        outR_LED  = 0.2f * outR_LED  + 0.8f * clamp(fabs(outR) * 0.1f, 0.f, 1.f);
        envOutLED = 0.2f * envOutLED + 0.8f * clamp(exciteEnv, 0.f, 1.f);
        hpOutLED  = 0.2f * hpOutLED  + 0.8f * clamp(fabs(bodyL) * 0.1f, 0.f, 1.f);

        setBipolarLED(OUT_L_LED_R, outL);
        setBipolarLED(OUT_R_LED_R, outR);
        setMonoLED(ENV_LED_R,      envOutLED);
        setBipolarLED(HP_OUT_LED_R, bodyL);

        // MODE BUTTON LEDs

        // DRIVE_MODE_LIGHT: excitation level (mono)
        exciteLED = 0.2f * exciteLED + 0.8f * clamp(exciteEnv, 0.f, 1.f);
        setMonoLED(DRIVE_MODE_LIGHT_R, exciteLED);

        // BODY_MODE_LIGHT: color per mode
        float r = 0.f, g = 0.f, b = 0.f;
        switch (bodyType) {
            case 0: // METAL → Blue
                r = 0.f; g = 0.f; b = 1.f;
                break;
            case 1: // WOOD → Green
                r = 0.f; g = 1.f; b = 0.f;
                break;
            case 2: // MEMBRANE → Yellow
                r = 1.f; g = 1.f; b = 0.f;
                break;
            case 3: // TUBE → Red
                r = 1.f; g = 0.f; b = 0.f;
                break;
        }

        // Always shows the body colour; full while MODE is held
        float on = pressed ? 1.f : 0.35f;

        lights[BODY_MODE_LIGHT_R].setSmoothBrightness(r * on, 1.f);
        lights[BODY_MODE_LIGHT_G].setSmoothBrightness(g * on, 1.f);
        lights[BODY_MODE_LIGHT_B].setSmoothBrightness(b * on, 1.f);
    }
};
// ------------------------------------------------------------
// OLED BODY DISPLAY
// ------------------------------------------------------------
struct FilterModeOLED : TransparentWidget {
    SynTHBloKFILTERModule* module;
    std::shared_ptr<Font> font;

    FilterModeOLED(SynTHBloKFILTERModule* m) {
        module = m;
        font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        box.size = Vec(36.6, 14);
    }

    void draw(const DrawArgs& args) override {
        NVGcontext* vg = args.vg;

        NVGpaint bg = nvgLinearGradient(
            vg, 0, 0, 0, box.size.y,
            nvgRGBA(10, 10, 10, 255),
            nvgRGBA(0, 0, 0, 255)
        );

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgFillPaint(vg, bg);
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgStrokeColor(vg, nvgRGBA(255, 180, 90, 80));
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);

        static const char* bodyNames[4] = {
            "METAL",
            "WOOD",
            "MEMBRANE",
            "TUBE"
        };

        // Module browser preview has no module: show the default body
        int bt = module ? clamp(module->bodyType, 0, 3) : 0;
        const char* text = bodyNames[bt];

        nvgFontSize(vg, 9.f);
        nvgFontFaceId(vg, font->handle);
        nvgFillColor(vg, nvgRGBA(255, 240, 180, 255));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, box.size.x * 0.5f, box.size.y * 0.5f, text, NULL);

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


// ------------------------------------------------------------
// WIDGET
// ------------------------------------------------------------
struct SynTHBloKFILTERWidget : ModuleWidget {
    SynTHBloKFILTERWidget(SynTHBloKFILTERModule* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKFilter.svg")));

        using namespace rack::componentlibrary;

        // Screws
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(62.54f, 3.5f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(3.5f, 125.f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(62.54f, 125.f))));

        const float x0 = 11.25f;
        const float x1 = 26.25f;
        const float x2 = 41.25f;
        const float x3 = 56.25f;

        // ROW 1 — KNOBS (y = 22)
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x0, 22)), module, SynTHBloKFILTERModule::CUTOFF_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x1, 22)), module, SynTHBloKFILTERModule::RES_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x2, 22)), module, SynTHBloKFILTERModule::DRIVE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x3, 22)), module, SynTHBloKFILTERModule::MIX_PARAM));

        // ROW 2 — DECAY / MODE / STRIKE (y = 37)
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x0, 39)), module, SynTHBloKFILTERModule::DECAY_PARAM));
        
        // MODE BUTTON LEDs + BUTTON
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(
            mm2px(Vec(x2, 39)), module, SynTHBloKFILTERModule::BODY_MODE_LIGHT_R));
        addParam(createParamCentered<TL1105>(mm2px(Vec(x2, 39)), module, SynTHBloKFILTERModule::MODE_PARAM));

        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(
            mm2px(Vec(x3, 39)), module, SynTHBloKFILTERModule::DRIVE_MODE_LIGHT_R));
        addParam(createParamCentered<TL1105>(mm2px(Vec(x3, 39)), module, SynTHBloKFILTERModule::STRIKE_PARAM));

        // OLED
        auto* modeOLED = new FilterModeOLED(module);
        modeOLED->box.pos = mm2px(Vec(23.8, 35.3));
        addChild(modeOLED);

// NEW ROW — KNOB CV INPUTS (2 mm below DECAY row)
addInput(createInputCentered<BlackJack>(mm2px(Vec(x0, 52.5)), module, SynTHBloKFILTERModule::CUTOFF_KNOB_CV_INPUT));
addInput(createInputCentered<BlackJack>(mm2px(Vec(x1, 52.5)), module, SynTHBloKFILTERModule::RES_KNOB_CV_INPUT));
addInput(createInputCentered<BlackJack>(mm2px(Vec(x2, 52.5)), module, SynTHBloKFILTERModule::DRIVE_KNOB_CV_INPUT));

addInput(createInputCentered<BlackJack>(mm2px(Vec(x3, 52.5)), module, SynTHBloKFILTERModule::MIX_KNOB_CV_INPUT));

        // ROW 3 — INPUTS (y = 66)
addInput(createInputCentered<BlackJack>(mm2px(Vec(x0, 70.5)), module, SynTHBloKFILTERModule::IN_L_INPUT));
addInput(createInputCentered<BlackJack>(mm2px(Vec(x1, 70.5)), module, SynTHBloKFILTERModule::IN_R_INPUT));
addInput(createInputCentered<BlackJack>(mm2px(Vec(x2, 70.5)), module, SynTHBloKFILTERModule::CUTOFF_CV_INPUT));
addInput(createInputCentered<BlackJack>(mm2px(Vec(x3, 70.5)), module, SynTHBloKFILTERModule::RES_CV_INPUT));

        // ROW 4 — MORE INPUTS (y = 81)
addInput(createInputCentered<BlackJack>(mm2px(Vec(x0, 89)), module, SynTHBloKFILTERModule::VOCT_INPUT));
addInput(createInputCentered<BlackJack>(mm2px(Vec(x1, 89)), module, SynTHBloKFILTERModule::DRIVE_CV_INPUT));
addInput(createInputCentered<BlackJack>(mm2px(Vec(x2, 89)), module, SynTHBloKFILTERModule::DECAY_CV_INPUT));
addInput(createInputCentered<BlackJack>(mm2px(Vec(x3, 89)), module, SynTHBloKFILTERModule::STRIKE_CV_INPUT));

        // ROW 5 — OUTPUTS (y = 108)
addOutput(createOutputCentered<BlackJack>(mm2px(Vec(x0, 113)), module, SynTHBloKFILTERModule::OUT_L_OUTPUT));
addOutput(createOutputCentered<BlackJack>(mm2px(Vec(x1, 113)), module, SynTHBloKFILTERModule::OUT_R_OUTPUT));
addOutput(createOutputCentered<BlackJack>(mm2px(Vec(x2, 113)), module, SynTHBloKFILTERModule::ENV_OUTPUT));

        // HP OUT (y = 118)
        addOutput(createOutputCentered<BlackJack>(mm2px(Vec(x3, 113)), module, SynTHBloKFILTERModule::HP_OUTPUT));

        // ------------------------------------------------------------
        // KNOB LEDs (tiny RGB) — above knobs
        // ------------------------------------------------------------
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x0, 15)), module, SynTHBloKFILTERModule::CUTOFF_LIGHT_R));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x1, 15)), module, SynTHBloKFILTERModule::RES_LIGHT_R));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x2, 15)), module, SynTHBloKFILTERModule::DRIVE_LIGHT_R));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x3, 15)), module, SynTHBloKFILTERModule::MIX_LIGHT_R));

        // ------------------------------------------------------------
        // JACK LEDs — ROW 3 (y = 69)
        // ------------------------------------------------------------
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x0, 76.5)), module, SynTHBloKFILTERModule::IN_L_LED_R));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x1, 76.5)), module, SynTHBloKFILTERModule::IN_R_LED_R));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x2, 76.5)), module, SynTHBloKFILTERModule::CUTOFF_CV_LED_R));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x3, 76.5)), module, SynTHBloKFILTERModule::RES_CV_LED_R));

        // ------------------------------------------------------------
        // JACK LEDs — ROW 4 (y = 83)
        // ------------------------------------------------------------
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x0, 95)), module, SynTHBloKFILTERModule::VOCT_LED_R));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x1, 95)), module, SynTHBloKFILTERModule::DRIVE_CV_LED_R));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x2, 95)), module, SynTHBloKFILTERModule::DECAY_CV_LED_R));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x3, 95)), module, SynTHBloKFILTERModule::STRIKE_CV_LED_R));

        // ------------------------------------------------------------
        // JACK LEDs — ROW 5 (y = 110)
        // ------------------------------------------------------------
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x0, 118)), module, SynTHBloKFILTERModule::OUT_L_LED_R));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x1, 118)), module, SynTHBloKFILTERModule::OUT_R_LED_R));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x2, 118)), module, SynTHBloKFILTERModule::ENV_LED_R));
        addChild(createLightCentered<TinyLight<RedGreenBlueLight>>(mm2px(Vec(x3, 118)), module, SynTHBloKFILTERModule::HP_OUT_LED_R));
    }
};

Model* modelSynTHBloKFilter =
    createModel<SynTHBloKFILTERModule, SynTHBloKFILTERWidget>("Resonator");
