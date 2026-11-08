#include "plugin.hpp"
#include "SynTHBloKMix24.hpp"
#include "SynTHBloKMix24_layout.hpp"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"   // the FX header packs statements onto one line
#include "SynTHBloKMix24_fx.hpp"
#pragma GCC diagnostic pop
#include "OledUi.hpp"
#include "KnobTicks.hpp"
#include "JackLed.hpp"

using namespace rack;

// -------------------------------------------------------------
// MODULE DEFINITION
// -------------------------------------------------------------
struct SynTHBloKMix24 : Module {
    // Per-instance DSP state (was function-local static, shared by every MIX-24 in a patch)
    float tiltState[7][2] = {};
    float lpL[6] = {};
    float lpR[6] = {};
    float glueEnvL[6] = {};
    float glueEnvR[6] = {};
    float busEnv = 0.f;

    float masterLLevel = 0.f;
    float masterRLevel = 0.f;

    // Per‑channel meter buffers (for ChannelBarMeter)
    float chL[6] = {};
    float chR[6] = {};
    MixFx fx;   // onboard aux FX bus
    float fxWetEnv = 0.f;
    float chEnvLR[6][2] = {};   // per-side peak envelopes for the channel bars
    float chEnv[6] = {};   // per-channel peak envelope for the display

    enum ParamIds {
        MAIN_LVL_PARAM,
        MAIN_PAN_PARAM,
        MAIN_TILT_PARAM,
        MAIN_DRIVE_PARAM,
        MAIN_AIR_PARAM,
        MAIN_GLUE_PARAM,
        MAIN_MUTE_PARAM,
        MAIN_SOLO_PARAM,

        POLY_LVL_PARAM,
        POLY_WID_PARAM,
        POLY_TILT_PARAM,
        POLY_DRIVE_PARAM,
        POLY_AIR_PARAM,
        POLY_GLUE_PARAM,
        POLY_MUTE_PARAM,
        POLY_SOLO_PARAM,

        STEREO_LVL_PARAM,
        STEREO_WID_PARAM,
        STEREO_TILT_PARAM,
        STEREO_DRIVE_PARAM,
        STEREO_AIR_PARAM,
        STEREO_GLUE_PARAM,
        STEREO_MUTE_PARAM,
        STEREO_SOLO_PARAM,

        FX_LVL_PARAM,
        FX_WID_PARAM,
        FX_TILT_PARAM,
        FX_DRIVE_PARAM,
        FX_AIR_PARAM,
        FX_GLUE_PARAM,
        FX_MUTE_PARAM,
        FX_SOLO_PARAM,

        EXT1_LVL_PARAM,
        EXT1_PAN_PARAM,
        EXT1_TILT_PARAM,
        EXT1_DRIVE_PARAM,
        EXT1_AIR_PARAM,
        EXT1_GLUE_PARAM,
        EXT1_MUTE_PARAM,
        EXT1_SOLO_PARAM,

        EXT2_LVL_PARAM,
        EXT2_WID_PARAM,
        EXT2_TILT_PARAM,
        EXT2_DRIVE_PARAM,
        EXT2_AIR_PARAM,
        EXT2_GLUE_PARAM,
        EXT2_MUTE_PARAM,
        EXT2_SOLO_PARAM,

        MASTER_LVL_PARAM,
        MASTER_WIDTH_PARAM,
        MASTER_TILT_PARAM,
        MASTER_GLUE_PARAM,
        MASTER_CLIP_PARAM,

        // onboard FX (appended so existing patches keep their values)
        OB_FX_SEL_PARAM, OB_FX_P1_PARAM, OB_FX_P2_PARAM, OB_FX_P3_PARAM, OB_FX_RET_PARAM,
        OB_SEND_1_PARAM, OB_SEND_2_PARAM, OB_SEND_3_PARAM, OB_SEND_4_PARAM, OB_SEND_5_PARAM, OB_SEND_6_PARAM,

        PARAMS_LEN
    };

    enum InputIds {
        GLOBAL_1_CV_INPUT,
        GLOBAL_2_CV_INPUT,
        GLOBAL_3_CV_INPUT,
        GLOBAL_4_CV_INPUT,
        GLOBAL_5_CV_INPUT,
        GLOBAL_6_CV_INPUT,

        MAIN_L_INPUT,   MAIN_R_INPUT,
        POLY_L_INPUT,   POLY_R_INPUT,
        STEREO_L_INPUT, STEREO_R_INPUT,
        FX_L_INPUT,     FX_R_INPUT,
        EXT1_L_INPUT,   EXT1_R_INPUT,
        EXT2_L_INPUT,   EXT2_R_INPUT,

        FX_RTN_L_INPUT, FX_RTN_R_INPUT,   // external FX return (appended)

        INPUTS_LEN
    };

    enum OutputIds {
        MASTER_L_OUTPUT,
        MASTER_R_OUTPUT,
        FX_SEND_L_OUTPUT, FX_SEND_R_OUTPUT,   // FX send bus (appended)
        OUTPUTS_LEN
    };

    enum LightIds {
        MAIN_L_METER_1_LIGHT, MAIN_L_METER_2_LIGHT, MAIN_L_METER_3_LIGHT, MAIN_L_METER_4_LIGHT, MAIN_L_METER_5_LIGHT,
        MAIN_R_METER_1_LIGHT, MAIN_R_METER_2_LIGHT, MAIN_R_METER_3_LIGHT, MAIN_R_METER_4_LIGHT, MAIN_R_METER_5_LIGHT,

        POLY_L_METER_1_LIGHT, POLY_L_METER_2_LIGHT, POLY_L_METER_3_LIGHT, POLY_L_METER_4_LIGHT, POLY_L_METER_5_LIGHT,
        POLY_R_METER_1_LIGHT, POLY_R_METER_2_LIGHT, POLY_R_METER_3_LIGHT, POLY_R_METER_4_LIGHT, POLY_R_METER_5_LIGHT,

        STEREO_L_METER_1_LIGHT, STEREO_L_METER_2_LIGHT, STEREO_L_METER_3_LIGHT, STEREO_L_METER_4_LIGHT, STEREO_L_METER_5_LIGHT,
        STEREO_R_METER_1_LIGHT, STEREO_R_METER_2_LIGHT, STEREO_R_METER_3_LIGHT, STEREO_R_METER_4_LIGHT, STEREO_R_METER_5_LIGHT,

        FX_L_METER_1_LIGHT, FX_L_METER_2_LIGHT, FX_L_METER_3_LIGHT, FX_L_METER_4_LIGHT, FX_L_METER_5_LIGHT,
        FX_R_METER_1_LIGHT, FX_R_METER_2_LIGHT, FX_R_METER_3_LIGHT, FX_R_METER_4_LIGHT, FX_R_METER_5_LIGHT,

        EXT1_L_METER_1_LIGHT, EXT1_L_METER_2_LIGHT, EXT1_L_METER_3_LIGHT, EXT1_L_METER_4_LIGHT, EXT1_L_METER_5_LIGHT,
        EXT1_R_METER_1_LIGHT, EXT1_R_METER_2_LIGHT, EXT1_R_METER_3_LIGHT, EXT1_R_METER_4_LIGHT, EXT1_R_METER_5_LIGHT,

        EXT2_L_METER_1_LIGHT, EXT2_L_METER_2_LIGHT, EXT2_L_METER_3_LIGHT, EXT2_L_METER_4_LIGHT, EXT2_L_METER_5_LIGHT,
        EXT2_R_METER_1_LIGHT, EXT2_R_METER_2_LIGHT, EXT2_R_METER_3_LIGHT, EXT2_R_METER_4_LIGHT, EXT2_R_METER_5_LIGHT,

        MASTER_L_METER_1_LIGHT, MASTER_L_METER_2_LIGHT, MASTER_L_METER_3_LIGHT, MASTER_L_METER_4_LIGHT, MASTER_L_METER_5_LIGHT,
        MASTER_R_METER_1_LIGHT, MASTER_R_METER_2_LIGHT, MASTER_R_METER_3_LIGHT, MASTER_R_METER_4_LIGHT, MASTER_R_METER_5_LIGHT,

        MAIN_LVL_HALO_LIGHT, POLY_LVL_HALO_LIGHT, STEREO_LVL_HALO_LIGHT,
        FX_LVL_HALO_LIGHT, EXT1_LVL_HALO_LIGHT, EXT2_LVL_HALO_LIGHT,

        MASTER_LVL_HALO_LIGHT, MASTER_WIDTH_HALO_LIGHT, MASTER_TILT_HALO_LIGHT,
        MASTER_GLUE_HALO_LIGHT, MASTER_CLIP_HALO_LIGHT,

        MASTER_L_BAR_LIGHT, MASTER_R_BAR_LIGHT,
    // Per‑channel LVL LEDs (to the right of the LVL knob)
    CH1_LVL_LED_LIGHT,
    CH2_LVL_LED_LIGHT,
    CH3_LVL_LED_LIGHT,
    CH4_LVL_LED_LIGHT,
    CH5_LVL_LED_LIGHT,
    CH6_LVL_LED_LIGHT,


        LIGHTS_LEN
    };

    // PAN vs WIDTH per channel (right-click menu, saved with the patch). The defaults are the old fixed setup.
    const bool kWidthDefault[6] = {false, true, true, true, false, true};
    const int kPanParam[6] = {MAIN_PAN_PARAM, POLY_WID_PARAM, STEREO_WID_PARAM, FX_WID_PARAM, EXT1_PAN_PARAM, EXT2_WID_PARAM};
    bool widthMode[6] = {false, true, true, true, false, true};

    SynTHBloKMix24();
    void process(const ProcessArgs& args) override;

    void setWidthMode(int c, bool w) {
        widthMode[c] = w;
        paramQuantities[kPanParam[c]]->name = string::f("CH %d %s", c + 1, w ? "width" : "pan");
    }
    void onReset() override {
        for (int c = 0; c < 6; c++) setWidthMode(c, kWidthDefault[c]);
    }
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_t* a = json_array();
        for (int c = 0; c < 6; c++) json_array_append_new(a, json_boolean(widthMode[c]));
        json_object_set_new(root, "widthMode", a);
        return root;
    }
    void dataFromJson(json_t* root) override {
        json_t* a = json_object_get(root, "widthMode");
        if (!a) return;   // older patches keep the fixed setup
        for (int c = 0; c < 6; c++) {
            json_t* v = json_array_get(a, c);
            setWidthMode(c, v ? json_is_true(v) : kWidthDefault[c]);
        }
    }
};

// -------------------------------------------------------------
// WIDGET DEFINITION
// -------------------------------------------------------------
struct SynTHBloKMix24Widget : ModuleWidget {
    SynTHBloKMix24Widget(SynTHBloKMix24* module);
    void appendContextMenu(Menu* menu) override;
};

// -------------------------------------------------------------
// Small helpers
// -------------------------------------------------------------
static inline float dbToLin(float db) {
    return std::pow(10.f, db / 20.f);
}

static inline float softClip(float x, float drive) {
    float g = 1.f + drive * 3.f;
    return std::tanh(x * g);
}

// -------------------------------------------------------------
// MODULE IMPLEMENTATION
// -------------------------------------------------------------
SynTHBloKMix24::SynTHBloKMix24() {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

    // MAIN
    configParam(MAIN_LVL_PARAM,   0.f, 2.f, 1.f, "CH 1 level");
    configParam(MAIN_PAN_PARAM,  -1.f, 1.f, 0.f, "CH 1 pan");
    configParam(MAIN_TILT_PARAM, -1.f, 1.f, 0.f, "CH 1 tilt");
    configParam(MAIN_DRIVE_PARAM, 0.f, 1.f, 0.f, "CH 1 drive");
    configParam(MAIN_AIR_PARAM,   0.f, 1.f, 0.f, "CH 1 air");
    configParam(MAIN_GLUE_PARAM,  0.f, 1.f, 0.f, "CH 1 glue");
    configSwitch(MAIN_MUTE_PARAM, 0.f, 1.f, 0.f, "CH 1 mute", {"Off", "On"});
    configSwitch(MAIN_SOLO_PARAM, 0.f, 1.f, 0.f, "CH 1 solo", {"Off", "On"});

    // POLY
    configParam(POLY_LVL_PARAM,   0.f, 2.f, 1.f, "CH 2 level");
    configParam(POLY_WID_PARAM,  -1.f, 1.f, 0.f, "CH 2 width");
    configParam(POLY_TILT_PARAM, -1.f, 1.f, 0.f, "CH 2 tilt");
    configParam(POLY_DRIVE_PARAM, 0.f, 1.f, 0.f, "CH 2 drive");
    configParam(POLY_AIR_PARAM,   0.f, 1.f, 0.f, "CH 2 air");
    configParam(POLY_GLUE_PARAM,  0.f, 1.f, 0.f, "CH 2 glue");
    configSwitch(POLY_MUTE_PARAM, 0.f, 1.f, 0.f, "CH 2 mute", {"Off", "On"});
    configSwitch(POLY_SOLO_PARAM, 0.f, 1.f, 0.f, "CH 2 solo", {"Off", "On"});

    // STEREO
    configParam(STEREO_LVL_PARAM,   0.f, 2.f, 1.f, "CH 3 level");
    configParam(STEREO_WID_PARAM,  -1.f, 1.f, 0.f, "CH 3 width");
    configParam(STEREO_TILT_PARAM, -1.f, 1.f, 0.f, "CH 3 tilt");
    configParam(STEREO_DRIVE_PARAM, 0.f, 1.f, 0.f, "CH 3 drive");
    configParam(STEREO_AIR_PARAM,   0.f, 1.f, 0.f, "CH 3 air");
    configParam(STEREO_GLUE_PARAM,  0.f, 1.f, 0.f, "CH 3 glue");
    configSwitch(STEREO_MUTE_PARAM, 0.f, 1.f, 0.f, "CH 3 mute", {"Off", "On"});
    configSwitch(STEREO_SOLO_PARAM, 0.f, 1.f, 0.f, "CH 3 solo", {"Off", "On"});

    // FX
    configParam(FX_LVL_PARAM,   0.f, 2.f, 1.f, "CH 4 level");
    configParam(FX_WID_PARAM,  -1.f, 1.f, 0.f, "CH 4 width");
    configParam(FX_TILT_PARAM, -1.f, 1.f, 0.f, "CH 4 tilt");
    configParam(FX_DRIVE_PARAM, 0.f, 1.f, 0.f, "CH 4 drive");
    configParam(FX_AIR_PARAM,   0.f, 1.f, 0.f, "CH 4 air");
    configParam(FX_GLUE_PARAM,  0.f, 1.f, 0.f, "CH 4 glue");
    configSwitch(FX_MUTE_PARAM, 0.f, 1.f, 0.f, "CH 4 mute", {"Off", "On"});
    configSwitch(FX_SOLO_PARAM, 0.f, 1.f, 0.f, "CH 4 solo", {"Off", "On"});

    // EXT1
    configParam(EXT1_LVL_PARAM,   0.f, 2.f, 1.f, "CH 5 level");
    configParam(EXT1_PAN_PARAM,  -1.f, 1.f, 0.f, "CH 5 pan");
    configParam(EXT1_TILT_PARAM, -1.f, 1.f, 0.f, "CH 5 tilt");
    configParam(EXT1_DRIVE_PARAM, 0.f, 1.f, 0.f, "CH 5 drive");
    configParam(EXT1_AIR_PARAM,   0.f, 1.f, 0.f, "CH 5 air");
    configParam(EXT1_GLUE_PARAM,  0.f, 1.f, 0.f, "CH 5 glue");
    configSwitch(EXT1_MUTE_PARAM, 0.f, 1.f, 0.f, "CH 5 mute", {"Off", "On"});
    configSwitch(EXT1_SOLO_PARAM, 0.f, 1.f, 0.f, "CH 5 solo", {"Off", "On"});

    // EXT2
    configParam(EXT2_LVL_PARAM,   0.f, 2.f, 1.f, "CH 6 level");
    configParam(EXT2_WID_PARAM,  -1.f, 1.f, 0.f, "CH 6 width");
    configParam(EXT2_TILT_PARAM, -1.f, 1.f, 0.f, "CH 6 tilt");
    configParam(EXT2_DRIVE_PARAM, 0.f, 1.f, 0.f, "CH 6 drive");
    configParam(EXT2_AIR_PARAM,   0.f, 1.f, 0.f, "CH 6 air");
    configParam(EXT2_GLUE_PARAM,  0.f, 1.f, 0.f, "CH 6 glue");
    configSwitch(EXT2_MUTE_PARAM, 0.f, 1.f, 0.f, "CH 6 mute", {"Off", "On"});
    configSwitch(EXT2_SOLO_PARAM, 0.f, 1.f, 0.f, "CH 6 solo", {"Off", "On"});

    // MASTER
    configParam(MASTER_LVL_PARAM,    0.f, 2.f, 1.f, "Master level");
    configParam(MASTER_WIDTH_PARAM, -1.f, 1.f, 0.f, "Master width");
    configParam(MASTER_TILT_PARAM,  -1.f, 1.f, 0.f, "Master tilt");
    configParam(MASTER_GLUE_PARAM,   0.f, 1.f, 0.f, "Master glue");
    configParam(MASTER_CLIP_PARAM,   0.f, 1.f, 0.f, "Master clip trim");
    configSwitch(OB_FX_SEL_PARAM, 0.f, 7.f, 2.f, "FX program",
                 {"Off", "Room", "Hall", "Plate", "Echo", "Ping-pong", "Chorus", "Flanger"});
    configParam(OB_FX_P1_PARAM, 0.f, 1.f, 0.5f, "FX param 1");
    configParam(OB_FX_P2_PARAM, 0.f, 1.f, 0.5f, "FX param 2");
    configParam(OB_FX_P3_PARAM, 0.f, 1.f, 0.4f, "FX param 3");
    configParam(OB_FX_RET_PARAM, 0.f, 1.f, 0.5f, "FX return");
    for (int i = 0; i < 6; i++)
        configParam(OB_SEND_1_PARAM + i, 0.f, 1.f, 0.f, string::f("CH %d FX send", i + 1));

    // GLOBAL CV INPUTS
    configInput(GLOBAL_1_CV_INPUT, "Global 1 (Level row)");
    configInput(GLOBAL_2_CV_INPUT, "Global 2 (Pan/Width row)");
    configInput(GLOBAL_3_CV_INPUT, "Global 3 (Tilt row)");
    configInput(GLOBAL_4_CV_INPUT, "Global 4 (Drive row)");
    configInput(GLOBAL_5_CV_INPUT, "Global 5 (Air row)");
    configInput(GLOBAL_6_CV_INPUT, "Global 6 (Glue row)");

    // STEREO AUDIO INPUTS
    configInput(MAIN_L_INPUT,   "CH 1 L");
    configInput(MAIN_R_INPUT,   "CH 1 R");
    configInput(POLY_L_INPUT,   "CH 2 L");
    configInput(POLY_R_INPUT,   "CH 2 R");
    configInput(STEREO_L_INPUT, "CH 3 L");
    configInput(STEREO_R_INPUT, "CH 3 R");
    configInput(FX_L_INPUT,     "CH 4 L");
    configInput(FX_R_INPUT,     "CH 4 R");
    configInput(EXT1_L_INPUT,   "CH 5 L");
    configInput(EXT1_R_INPUT,   "CH 5 R");
    configInput(EXT2_L_INPUT,   "CH 6 L");
    configInput(EXT2_R_INPUT,   "CH 6 R");

    configOutput(MASTER_L_OUTPUT, "Master left");
    configOutput(MASTER_R_OUTPUT, "Master right");
    configOutput(FX_SEND_L_OUTPUT, "FX send left");
    configOutput(FX_SEND_R_OUTPUT, "FX send right");
    configInput(FX_RTN_L_INPUT, "FX return left (mono if R unpatched)");
    configInput(FX_RTN_R_INPUT, "FX return right");
}
// -------------------------------------------------------------
// DSP
// -------------------------------------------------------------
void SynTHBloKMix24::process(const ProcessArgs& args) {
    float dt = args.sampleTime;

    // --- STEREO INPUT HELPER (mono fallback) ---
    auto getStereo = [&](int Lid, int Rid) {
        float L = 0.f, R = 0.f;

        bool lConn = inputs[Lid].isConnected();
        bool rConn = inputs[Rid].isConnected();

        if (lConn) L = inputs[Lid].getVoltage();
        if (rConn) R = inputs[Rid].getVoltage();

        if (lConn && !rConn) R = L;
        if (!lConn && rConn) L = R;

        return std::pair<float,float>(L, R);
    };

    // --- RAW AUDIO INPUTS ---
    auto mainIn   = getStereo(MAIN_L_INPUT,   MAIN_R_INPUT);
    auto polyIn   = getStereo(POLY_L_INPUT,   POLY_R_INPUT);
    auto stereoIn = getStereo(STEREO_L_INPUT, STEREO_R_INPUT);
    auto fxIn     = getStereo(FX_L_INPUT,     FX_R_INPUT);
    auto ext1In   = getStereo(EXT1_L_INPUT,   EXT1_R_INPUT);
    auto ext2In   = getStereo(EXT2_L_INPUT,   EXT2_R_INPUT);

    float mainL   = mainIn.first;
    float mainR   = mainIn.second;
    float polyL   = polyIn.first;
    float polyR   = polyIn.second;
    float stereoL = stereoIn.first;
    float stereoR = stereoIn.second;
    float fxL     = fxIn.first;
    float fxR     = fxIn.second;
    float ext1L   = ext1In.first;
    float ext1R   = ext1In.second;
    float ext2L   = ext2In.first;
    float ext2R   = ext2In.second;

    // --- SOLO / MUTE ---
    bool solo[6] = {
        params[MAIN_SOLO_PARAM].getValue() > 0.5f,
        params[POLY_SOLO_PARAM].getValue() > 0.5f,
        params[STEREO_SOLO_PARAM].getValue() > 0.5f,
        params[FX_SOLO_PARAM].getValue() > 0.5f,
        params[EXT1_SOLO_PARAM].getValue() > 0.5f,
        params[EXT2_SOLO_PARAM].getValue() > 0.5f
    };
    bool anySolo = solo[0] || solo[1] || solo[2] || solo[3] || solo[4] || solo[5];

    auto isMuted = [&](int idx, int muteParam) {
        bool m = params[muteParam].getValue() > 0.5f;
        if (anySolo) return !solo[idx];   // a solo silences everything else and beats the channel's own mute
        return m;
    };

    // --- PROCESS HELPERS ---
    auto processPan = [](float L, float R, float pan) {
        float p = (pan + 1.f) * 0.5f;
        float gL = std::cos(p * float(M_PI) * 0.5f);
        float gR = std::sin(p * float(M_PI) * 0.5f);
        float mid = 0.5f * (L + R);
        return std::pair<float,float>(mid * gL, mid * gR);
    };

    auto processWidth = [](float L, float R, float width) {
        float mid  = 0.5f * (L + R);
        float side = 0.5f * (L - R);
        side *= (1.f + width);
        return std::pair<float,float>(mid + side, mid - side);
    };


    auto processTilt = [&](float L, float R, float tilt, int idx) {
        float cutoff = 800.f;
        float c = std::exp(-2.f * float(M_PI) * cutoff * dt);

        tiltState[idx][0] = tiltState[idx][0] * c + L * (1.f - c);
        tiltState[idx][1] = tiltState[idx][1] * c + R * (1.f - c);

        float lowL = tiltState[idx][0];
        float lowR = tiltState[idx][1];
        float highL = L - lowL;
        float highR = R - lowR;

        float lowGain  = 1.f - 0.5f * std::max(0.f,  tilt);
        float highGain = 1.f - 0.5f * std::max(0.f, -tilt);

        return std::pair<float,float>(
            lowL * lowGain + highL * highGain,
            lowR * lowGain + highR * highGain
        );
    };
auto processAir = [&](float L, float R, float air, int idx) {
    float cutoff = 8000.f;
    float c = std::exp(-2.f * float(M_PI) * cutoff * dt);

    lpL[idx] = lpL[idx] * c + L * (1.f - c);
    lpR[idx] = lpR[idx] * c + R * (1.f - c);

    float highL = L - lpL[idx];
    float highR = R - lpR[idx];

    float g = 1.f + air * 1.5f;

    return std::pair<float,float>(
        L + highL * (g - 1.f),
        R + highR * (g - 1.f)
    );
};



    auto applyLevelCv = [&](float base, float cv) {
        float db = (cv - 5.f) * 0.6f;
        return base * dbToLin(db);
    };

    // 5-LED meter helper
    auto update5LedMeter = [&](float v, int baseLightId) {
        float level = rack::math::clamp(std::fabs(v) * 0.5f, 0.f, 1.f);
        const float th[5] = {0.1f, 0.2f, 0.35f, 0.5f, 0.7f};
        for (int i = 0; i < 5; ++i) {
            float t = th[i];
            float seg = rack::math::clamp((level - t) / 0.2f, 0.f, 1.f);
            lights[baseLightId + i].setBrightness(seg);
        }
    };

    // --- GLOBAL CV READ ---
    float g1 = inputs[GLOBAL_1_CV_INPUT].isConnected() ? inputs[GLOBAL_1_CV_INPUT].getVoltage() : 0.f;
    float g2 = inputs[GLOBAL_2_CV_INPUT].isConnected() ? inputs[GLOBAL_2_CV_INPUT].getVoltage() : 0.f;
    float g3 = inputs[GLOBAL_3_CV_INPUT].isConnected() ? inputs[GLOBAL_3_CV_INPUT].getVoltage() : 0.f;
    float g4 = inputs[GLOBAL_4_CV_INPUT].isConnected() ? inputs[GLOBAL_4_CV_INPUT].getVoltage() : 0.f;
    float g5 = inputs[GLOBAL_5_CV_INPUT].isConnected() ? inputs[GLOBAL_5_CV_INPUT].getVoltage() : 0.f;
    float g6 = inputs[GLOBAL_6_CV_INPUT].isConnected() ? inputs[GLOBAL_6_CV_INPUT].getVoltage() : 0.f;

    // --- PER-CHANNEL OUTPUT BUFFERS ---
    float sumL = 0.f;
    float sumR = 0.f;
    float sendL = 0.f;
    float sendR = 0.f;

    // --- CHANNEL PROCESSOR ---
    auto doChannel = [&](int idx,
                         float inL, float inR,
                         int lvlP, int panP, bool usePan,
                         int tiltP, int driveP, int airP,
                         int glueP, int muteP)
    {
        if (isMuted(idx, muteP))
            return;

        float lvl   = params[lvlP].getValue();
        float pan   = params[panP].getValue();
        float tilt  = params[tiltP].getValue();
        float drive = params[driveP].getValue();
        float air   = params[airP].getValue();
        float glue  = params[glueP].getValue();

        // --- APPLY GLOBAL CVs ---
        if (inputs[GLOBAL_1_CV_INPUT].isConnected())
            lvl = applyLevelCv(lvl, g1);

        if (inputs[GLOBAL_2_CV_INPUT].isConnected())
            pan = rack::math::clamp(pan + g2 / 5.f, -1.f, 1.f);

        if (inputs[GLOBAL_3_CV_INPUT].isConnected())
            tilt = rack::math::clamp(tilt + g3 / 5.f, -1.f, 1.f);

        if (inputs[GLOBAL_4_CV_INPUT].isConnected())
            drive = rack::math::clamp(drive + g4 / 10.f, 0.f, 1.f);

        if (inputs[GLOBAL_5_CV_INPUT].isConnected())
            air = rack::math::clamp(air + g5 / 10.f, 0.f, 1.f);

        if (inputs[GLOBAL_6_CV_INPUT].isConnected())
            glue = rack::math::clamp(glue + g6 / 10.f, 0.f, 1.f);

        float L = inL * lvl;
        float R = inR * lvl;

        if (usePan) {
            auto pr = processPan(L, R, pan);
            L = pr.first;
            R = pr.second;
        } else {
            auto pr = processWidth(L, R, pan);
            L = pr.first;
            R = pr.second;
        }

        auto tr = processTilt(L, R, tilt, idx);
        L = tr.first;
        R = tr.second;

        L = softClip(L, drive);
        R = softClip(R, drive);

        auto ar = processAir(L, R, air, idx);
        L = ar.first;
        R = ar.second;

        if (glue > 0.f) {
            float xL = std::fabs(L);
            float xR = std::fabs(R);
            float atk = 0.002f, rel = 0.08f;

            glueEnvL[idx] += (xL - glueEnvL[idx]) * (xL > glueEnvL[idx] ? atk : rel);
            glueEnvR[idx] += (xR - glueEnvR[idx]) * (xR > glueEnvR[idx] ? atk : rel);

            float th = 0.6f;
            float grL = glueEnvL[idx] > th ? (th / (glueEnvL[idx] + 1e-6f)) : 1.f;
            float grR = glueEnvR[idx] > th ? (th / (glueEnvR[idx] + 1e-6f)) : 1.f;

            float mix = glue * 0.6f;
            L = L * (1.f - mix) + L * grL * mix;
            R = R * (1.f - mix) + R * grR * mix;
        }

        // WRITE INTO MODULE‑LEVEL ARRAYS (for ChannelBarMeter)
        chL[idx] = L;
        chR[idx] = R;
        {
            float f = std::exp(-dt * 25.f);
            chEnvLR[idx][0] = std::max(std::fabs(L), chEnvLR[idx][0] * f);
            chEnvLR[idx][1] = std::max(std::fabs(R), chEnvLR[idx][1] * f);
        }
        chEnv[idx] = std::max(std::max(std::fabs(L), std::fabs(R)), chEnv[idx] * std::exp(-dt * 25.f));

        sumL += L;
        sumR += R;
        {
            float snd = params[OB_SEND_1_PARAM + idx].getValue();
            sendL += L * snd;
            sendR += R * snd;
        }
    };

    // --- PROCESS ALL CHANNELS ---
    doChannel(0, mainL,   mainR,   MAIN_LVL_PARAM,   MAIN_PAN_PARAM,   !widthMode[0],
                                MAIN_TILT_PARAM, MAIN_DRIVE_PARAM, MAIN_AIR_PARAM,
                                MAIN_GLUE_PARAM, MAIN_MUTE_PARAM);

    doChannel(1, polyL,   polyR,   POLY_LVL_PARAM,   POLY_WID_PARAM,   !widthMode[1],
                                POLY_TILT_PARAM, POLY_DRIVE_PARAM, POLY_AIR_PARAM,
                                POLY_GLUE_PARAM, POLY_MUTE_PARAM);

    doChannel(2, stereoL, stereoR, STEREO_LVL_PARAM, STEREO_WID_PARAM, !widthMode[2],
                                STEREO_TILT_PARAM, STEREO_DRIVE_PARAM, STEREO_AIR_PARAM,
                                STEREO_GLUE_PARAM, STEREO_MUTE_PARAM);

    doChannel(3, fxL,     fxR,     FX_LVL_PARAM,     FX_WID_PARAM,     !widthMode[3],
                                FX_TILT_PARAM, FX_DRIVE_PARAM, FX_AIR_PARAM,
                                FX_GLUE_PARAM, FX_MUTE_PARAM);

    doChannel(4, ext1L,   ext1R,   EXT1_LVL_PARAM,   EXT1_PAN_PARAM,   !widthMode[4],
                                EXT1_TILT_PARAM, EXT1_DRIVE_PARAM, EXT1_AIR_PARAM,
                                EXT1_GLUE_PARAM, EXT1_MUTE_PARAM);

    doChannel(5, ext2L,   ext2R,   EXT2_LVL_PARAM,   EXT2_WID_PARAM,   !widthMode[5],
                                EXT2_TILT_PARAM, EXT2_DRIVE_PARAM, EXT2_AIR_PARAM,
                                EXT2_GLUE_PARAM, EXT2_MUTE_PARAM);

    // -------------------------------------------------
    // MASTER BUS: WIDTH, TILT, GLUE, CLIP
    // -------------------------------------------------
    float masterLvl   = params[MASTER_LVL_PARAM].getValue();
    float masterWidth = params[MASTER_WIDTH_PARAM].getValue();
    float masterTilt  = params[MASTER_TILT_PARAM].getValue();
    float masterGlue  = params[MASTER_GLUE_PARAM].getValue();
    float masterClip  = params[MASTER_CLIP_PARAM].getValue();

    auto mw = processWidth(sumL, sumR, masterWidth);
    float L = mw.first;
    float R = mw.second;

    auto mt = processTilt(L, R, masterTilt, 0);
    L = mt.first;
    R = mt.second;

    // onboard FX: aux bus fed by the per-channel sends, wet return added before bus glue and clip
    {
        float wl, wr;
        fx.process(sendL, sendR, args.sampleRate, (int) std::round(params[OB_FX_SEL_PARAM].getValue()),
                   params[OB_FX_P1_PARAM].getValue(), params[OB_FX_P2_PARAM].getValue(),
                   params[OB_FX_P3_PARAM].getValue(), wl, wr);
        // external FX loop: send bus out, return in (added to the internal effect)
        outputs[FX_SEND_L_OUTPUT].setVoltage(sendL * 5.f);
        outputs[FX_SEND_R_OUTPUT].setVoltage(sendR * 5.f);
        {
            bool rl = inputs[FX_RTN_L_INPUT].isConnected(), rr = inputs[FX_RTN_R_INPUT].isConnected();
            float xl = rl ? inputs[FX_RTN_L_INPUT].getVoltage() * 0.2f : 0.f;
            float xr = rr ? inputs[FX_RTN_R_INPUT].getVoltage() * 0.2f : 0.f;
            if (rl && !rr) xr = xl;
            if (!rl && rr) xl = xr;
            wl += xl;
            wr += xr;
        }
        float ret = params[OB_FX_RET_PARAM].getValue();
        fxWetEnv = std::max(std::max(std::fabs(wl), std::fabs(wr)) * ret, fxWetEnv * std::exp(-dt * 12.f));
        L += wl * ret;
        R += wr * ret;
    }

    float mono = 0.5f * (L + R);
    float x = std::fabs(mono);
    float atk = 0.0015f;
    float rel = 0.12f;
    busEnv += (x - busEnv) * (x > busEnv ? atk : rel);
    float th = 0.7f;
    float gr = busEnv > th ? (th / (busEnv + 1e-6f)) : 1.f;
    float mix = masterGlue * 0.8f;
    L = L * (1.f - mix) + L * gr * mix;
    R = R * (1.f - mix) + R * gr * mix;

    float clipDrive = 0.4f + masterClip * 1.6f;
    L = std::tanh(L * clipDrive);
    R = std::tanh(R * clipDrive);

    L *= masterLvl;
    R *= masterLvl;

    // --- MASTER METER UPDATE ---
    float fall = std::exp(-dt * 40.f);

    float magL = std::fabs(L);
    float magR = std::fabs(R);

    masterLLevel = std::max(magL, masterLLevel * fall);
    masterRLevel = std::max(magR, masterRLevel * fall);

    auto norm = [](float x) {
        return rack::math::clamp(x / 5.f, 0.f, 1.f);
    };

    lights[MASTER_L_BAR_LIGHT].setBrightness(norm(masterLLevel));
    lights[MASTER_R_BAR_LIGHT].setBrightness(norm(masterRLevel));

    // -------------------------------------------------
    // METERS (per-channel 5-LED L/R + master 5-LED L/R)
    // -------------------------------------------------
    update5LedMeter(chL[0], MAIN_L_METER_1_LIGHT);
    update5LedMeter(chR[0], MAIN_R_METER_1_LIGHT);

    update5LedMeter(chL[1], POLY_L_METER_1_LIGHT);
    update5LedMeter(chR[1], POLY_R_METER_1_LIGHT);

    update5LedMeter(chL[2], STEREO_L_METER_1_LIGHT);
    update5LedMeter(chR[2], STEREO_R_METER_1_LIGHT);

    update5LedMeter(chL[3], FX_L_METER_1_LIGHT);
    update5LedMeter(chR[3], FX_R_METER_1_LIGHT);

    update5LedMeter(chL[4], EXT1_L_METER_1_LIGHT);
    update5LedMeter(chR[4], EXT1_R_METER_1_LIGHT);

    update5LedMeter(chL[5], EXT2_L_METER_1_LIGHT);
    update5LedMeter(chR[5], EXT2_R_METER_1_LIGHT);

    update5LedMeter(L, MASTER_L_METER_1_LIGHT);
    update5LedMeter(R, MASTER_R_METER_1_LIGHT);

    // -------------------------------------------------
    // HALOS (LEVEL knobs + MASTER knobs)
    // -------------------------------------------------
    auto channelHalo = [&](int idx, int haloLightId) {
        float mag = 0.5f * (std::fabs(chL[idx]) + std::fabs(chR[idx]));
        lights[haloLightId].setBrightness(rack::math::clamp(mag * 0.5f, 0.f, 1.f));
    };
    channelHalo(0, MAIN_LVL_HALO_LIGHT);
    channelHalo(1, POLY_LVL_HALO_LIGHT);
    channelHalo(2, STEREO_LVL_HALO_LIGHT);
    channelHalo(3, FX_LVL_HALO_LIGHT);
    channelHalo(4, EXT1_LVL_HALO_LIGHT);
    channelHalo(5, EXT2_LVL_HALO_LIGHT);

    float masterMag = 0.5f * (std::fabs(L) + std::fabs(R));
    lights[MASTER_LVL_HALO_LIGHT].setBrightness(rack::math::clamp(masterMag * 0.5f, 0.f, 1.f));

    float midM = 0.5f * (L + R);
    (void)midM; // avoid unused warning

    float sideM = 0.5f * (L - R);
    float sideEnergy = rack::math::clamp(std::fabs(sideM) * 0.7f, 0.f, 1.f);
    lights[MASTER_WIDTH_HALO_LIGHT].setBrightness(sideEnergy);

    float tiltEnergy = rack::math::clamp(std::fabs(masterTilt), 0.f, 1.f);
    lights[MASTER_TILT_HALO_LIGHT].setBrightness(tiltEnergy);

    float glueEnergy = rack::math::clamp(masterGlue, 0.f, 1.f);
    lights[MASTER_GLUE_HALO_LIGHT].setBrightness(glueEnergy);

    float clipEnergy = rack::math::clamp(masterClip, 0.f, 1.f);
    lights[MASTER_CLIP_HALO_LIGHT].setBrightness(clipEnergy);

// -------------------------------------------------
// LVL LEDs (one per channel)
// Brightness = knob value, but ONLY if left jack is connected
// -------------------------------------------------
auto setLvlLed = [&](int ch, int lvlParam, int inputId, int lightId) {
    bool connected = inputs[inputId].isConnected();
    float knob = params[lvlParam].getValue();   // 0–2 range
    float norm = rack::math::clamp(knob * 0.5f, 0.f, 1.f); // normalize to 0–1
    lights[lightId].setBrightness(connected ? norm : 0.f);
};

setLvlLed(0, MAIN_LVL_PARAM,   MAIN_L_INPUT,   CH1_LVL_LED_LIGHT);
setLvlLed(1, POLY_LVL_PARAM,   POLY_L_INPUT,   CH2_LVL_LED_LIGHT);
setLvlLed(2, STEREO_LVL_PARAM, STEREO_L_INPUT, CH3_LVL_LED_LIGHT);
setLvlLed(3, FX_LVL_PARAM,     FX_L_INPUT,     CH4_LVL_LED_LIGHT);
setLvlLed(4, EXT1_LVL_PARAM,   EXT1_L_INPUT,   CH5_LVL_LED_LIGHT);
setLvlLed(5, EXT2_LVL_PARAM,   EXT2_L_INPUT,   CH6_LVL_LED_LIGHT);
    // -------------------------------------------------
    // OUTPUT
    // -------------------------------------------------
    outputs[MASTER_L_OUTPUT].setVoltage(L * 5.f);
    outputs[MASTER_R_OUTPUT].setVoltage(R * 5.f);
}

// -------------------------------------------------------------
// FIRE EDITION UI  (amber OLED palette shared with BloKMaster / MIDI + Expansion)
// -------------------------------------------------------------
// fire halo: pale yellow -> yellow-orange -> orange -> orange-red -> deep red
static NVGcolor fireHalo(float t) {
    t = rack::math::clamp(t, 0.f, 1.f);
    static const float st[5][3] = {{255, 236, 130}, {255, 186, 40}, {255, 112, 10}, {240, 52, 10}, {140, 8, 8}};
    float p = t * 4.f; int i = (int) p; if (i > 3) i = 3; float k = p - i;
    return nvgRGBA((int)(st[i][0] + (st[i+1][0] - st[i][0]) * k), (int)(st[i][1] + (st[i+1][1] - st[i][1]) * k),
                   (int)(st[i][2] + (st[i+1][2] - st[i][2]) * k), 255);
}
static NVGcolor fireRamp(float t) { return fireHalo(t); }

template <class K>
struct ArcKnob : K {
    float arcR = 4.4f;   // mm
    std::function<float()> eff;   // effective 0..1 value (knob + CV / live signal) driving the halo
    float sm = 0.f;
    bool init = false;
    void drawLayer(const typename K::DrawArgs& a, int layer) override {
        K::drawLayer(a, layer);
        if (layer != 1) return;
        float t = 0.5f;
        if (auto* pq = this->getParamQuantity()) t = pq->getScaledValue();
        Vec c = this->box.size.div(2.f);
        float r = mm2px(arcR);

        // ring colour follows the effective value (knob + CV / live signal), no glow
        float e = eff ? rack::math::clamp(eff(), 0.f, 1.f) : t;
        if (!init) { sm = e; init = true; }
        sm += (e - sm) * 0.30f;

        float a0 = this->minAngle - float(M_PI) / 2.f;
        float a1 = this->maxAngle - float(M_PI) / 2.f;
        float av = a0 + (a1 - a0) * t;
        nvgLineCap(a.vg, NVG_ROUND);
        nvgBeginPath(a.vg);
        nvgArc(a.vg, c.x, c.y, r, a0, a1, NVG_CW);
        nvgStrokeWidth(a.vg, mm2px(0.55f));
        nvgStrokeColor(a.vg, nvgRGBA(70, 45, 15, 140));
        nvgStroke(a.vg);
        NVGcolor col = fireHalo(sm);
        nvgBeginPath(a.vg);
        nvgArc(a.vg, c.x, c.y, r, a0, std::max(av, a0 + 0.02f), NVG_CW);
        nvgStrokeWidth(a.vg, mm2px(0.55f));
        nvgStrokeColor(a.vg, col);
        nvgStroke(a.vg);
    }
};

// effective (post-CV) 0..1 value per channel knob / master knob, for the halos
static float mixEffCh(SynTHBloKMix24* m, int c, int k) {
    static const int base[6] = {SynTHBloKMix24::MAIN_LVL_PARAM, SynTHBloKMix24::POLY_LVL_PARAM, SynTHBloKMix24::STEREO_LVL_PARAM,
                                SynTHBloKMix24::FX_LVL_PARAM, SynTHBloKMix24::EXT1_LVL_PARAM, SynTHBloKMix24::EXT2_LVL_PARAM};
    static const int gin[6] = {SynTHBloKMix24::GLOBAL_1_CV_INPUT, SynTHBloKMix24::GLOBAL_2_CV_INPUT, SynTHBloKMix24::GLOBAL_3_CV_INPUT,
                               SynTHBloKMix24::GLOBAL_4_CV_INPUT, SynTHBloKMix24::GLOBAL_5_CV_INPUT, SynTHBloKMix24::GLOBAL_6_CV_INPUT};
    if (!m) return 0.f;
    float p = m->params[base[c] + k].getValue();
    bool cv = m->inputs[gin[k]].isConnected();
    float g = cv ? m->inputs[gin[k]].getVoltage() : 0.f;
    switch (k) {
        case 0: {   // LVL: live signal level (knob + CV set the gain, the halo shows what comes out)
            float lv = rack::math::clamp(m->chEnv[c] / 1.3f, 0.f, 1.f);
            return std::sqrt(lv);
        }
        case 1: case 2: { float v = rack::math::clamp(p + (cv ? g / 5.f : 0.f), -1.f, 1.f); return (v + 1.f) * 0.5f; }
        default: return rack::math::clamp(p + (cv ? g / 10.f : 0.f), 0.f, 1.f);
    }
}
static float mixEffMaster(SynTHBloKMix24* m, int i) {
    if (!m) return 0.f;
    static const int mp[5] = {SynTHBloKMix24::MASTER_LVL_PARAM, SynTHBloKMix24::MASTER_WIDTH_PARAM, SynTHBloKMix24::MASTER_TILT_PARAM,
                              SynTHBloKMix24::MASTER_GLUE_PARAM, SynTHBloKMix24::MASTER_CLIP_PARAM};
    float p = m->params[mp[i]].getValue();
    if (i == 0) return std::sqrt(rack::math::clamp(std::max(m->masterLLevel, m->masterRLevel) / 1.3f, 0.f, 1.f));
    if (i == 1 || i == 2) return (p + 1.f) * 0.5f;
    return rack::math::clamp(p, 0.f, 1.f);
}

struct MixSwitch : app::ParamWidget {
    NVGcolor onCol = nvgRGBA(255, 60, 40, 255);
    const char* label = "MUTE";
    int partner = 1;   // param offset of the opposite switch: mute -> +1 (solo), solo -> -1 (mute)
    std::shared_ptr<window::Font> font;
    MixSwitch() { box.size = mm2px(Vec(6.4f, 3.6f)); }   // small rectangular OLED
    bool isOn() {
        auto* pq = getParamQuantity();
        return pq && pq->getValue() > 0.5f;
    }
    static NVGcolor withA(NVGcolor c, int al) { return nvgRGBA((int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255), al); }
    void draw(const DrawArgs& a) override {
        float W = box.size.x, H = box.size.y;
        bool on = isOn();
        nvgBeginPath(a.vg);
        nvgRoundedRect(a.vg, 0.f, 0.f, W, H, 1.2f);
        nvgFillColor(a.vg, nvgRGBA(10, 7, 3, 255));
        nvgFill(a.vg);
        if (on) {   // lit state: soft tint
            nvgBeginPath(a.vg);
            nvgRoundedRect(a.vg, 0.5f, 0.5f, W - 1.f, H - 1.f, 1.1f);
            nvgFillColor(a.vg, withA(onCol, 55));
            nvgFill(a.vg);
        }
        nvgBeginPath(a.vg);
        nvgRoundedRect(a.vg, 0.5f, 0.5f, W - 1.f, H - 1.f, 1.1f);
        nvgStrokeWidth(a.vg, 0.7f);
        nvgStrokeColor(a.vg, on ? withA(onCol, 190) : nvgRGBA(255, 122, 0, 90));
        nvgStroke(a.vg);
        if (!font) font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        if (font) {
            nvgFontFaceId(a.vg, font->handle);
            nvgFontSize(a.vg, 6.2f);
            nvgTextAlign(a.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(a.vg, on ? withA(onCol, 255) : nvgRGBA(255, 190, 90, 105));
            nvgText(a.vg, W * 0.5f, H * 0.5f + 0.5f, label, nullptr);
        }
        ParamWidget::draw(a);
    }
    void drawLayer(const DrawArgs& a, int layer) override {
        if (layer == 1 && isOn()) {   // gentle glow only
            float W = box.size.x, H = box.size.y;
            NVGpaint g = nvgBoxGradient(a.vg, 0.f, 0.f, W, H, 2.f, 4.f, withA(onCol, 55), nvgRGBA(0, 0, 0, 0));
            nvgBeginPath(a.vg);
            nvgRect(a.vg, -4.f, -4.f, W + 8.f, H + 8.f);
            nvgFillPaint(a.vg, g);
            nvgFill(a.vg);
        }
        ParamWidget::drawLayer(a, layer);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == 0) {
            if (auto* pq = getParamQuantity()) {
                bool turnOn = pq->getValue() <= 0.5f;
                pq->setValue(turnOn ? 1.f : 0.f);
                if (turnOn && pq->module) pq->module->params[pq->paramId + partner].setValue(0.f);   // solo releases the channel's mute (and vice versa)
            }
            e.consume(this);
            return;
        }
        ParamWidget::onButton(e);
    }
};

// Levels OLED (the MIX-12 design): 6 channel meters (L / R pairs) + master L / R as vertical bars, dBFS scale,
// peak hold, master peak and clip lamp. Channel numbers: red = muted, yellow = solo, dim = unpatched or silenced by
// a solo. Click = reset the peaks and clip.
struct MixLevelsDisplay : TransparentWidget {
    SynTHBloKMix24* module = nullptr;
    float peak[14] = {};
    float peakMaster = 0.f;
    float clipHold = 0.f;
    static constexpr float DB_LO = -48.f, DB_HI = 6.f;
    static constexpr int N = 24;

    static float toDb(float v) { return v > 1e-5f ? 20.f * std::log10(v) : -120.f; }
    static float pos(float v) { return rack::math::clamp((toDb(v) - DB_LO) / (DB_HI - DB_LO), 0.f, 1.f); }
    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            for (float& p : peak) p = 0.f;
            peakMaster = 0.f;
            clipHold = 0.f;
            e.consume(this);
        }
    }
    void bar(const DrawArgs& a, float x, float y0, float y1, float w, float lvl, float& pk) {
        float p = pos(lvl);
        pk = std::max(lvl, pk * 0.9975f);
        float pp = pos(pk);
        float sh = (y1 - y0) / N;
        for (int i = 0; i < N; i++) {
            float segPos = (i + 1.f) / N;
            float segDb = DB_LO + (DB_HI - DB_LO) * segPos;
            bool lit = segPos <= p + 1e-4f;
            bool pkSeg = (int) std::ceil(pp * N) == i + 1 && pp > 0.02f;
            NVGcolor c;
            if (segDb > -1.f)      c = nvgRGBA(255, 40, 30, 255);
            else if (segDb > -9.f) c = nvgRGBA(255, 112, 15, 255);
            else                   c = nvgRGBA(255, 176, 60, 255);
            if (!lit && !pkSeg) c = nvgRGBA(46, 29, 10, 255);
            else if (!lit) c.a = 0.5f;
            nvgBeginPath(a.vg);
            nvgRect(a.vg, x, y1 - (i + 1) * sh + 0.3f, w, sh - 0.6f);
            nvgFillColor(a.vg, c);
            nvgFill(a.vg);
        }
    }
    void drawLayer(const DrawArgs& a, int layer) override {
        if (layer != 1) return;
        const float W = box.size.x, H = box.size.y;
        oled::box(a, W, H);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(a.vg, font->handle);
        nvgScissor(a.vg, 0, 0, W, H);

        static const int muteP[6] = {SynTHBloKMix24::MAIN_MUTE_PARAM, SynTHBloKMix24::POLY_MUTE_PARAM, SynTHBloKMix24::STEREO_MUTE_PARAM,
                                     SynTHBloKMix24::FX_MUTE_PARAM, SynTHBloKMix24::EXT1_MUTE_PARAM, SynTHBloKMix24::EXT2_MUTE_PARAM};
        static const int soloP[6] = {SynTHBloKMix24::MAIN_SOLO_PARAM, SynTHBloKMix24::POLY_SOLO_PARAM, SynTHBloKMix24::STEREO_SOLO_PARAM,
                                     SynTHBloKMix24::FX_SOLO_PARAM, SynTHBloKMix24::EXT1_SOLO_PARAM, SynTHBloKMix24::EXT2_SOLO_PARAM};
        static const int inL[6] = {SynTHBloKMix24::MAIN_L_INPUT, SynTHBloKMix24::POLY_L_INPUT, SynTHBloKMix24::STEREO_L_INPUT,
                                   SynTHBloKMix24::FX_L_INPUT, SynTHBloKMix24::EXT1_L_INPUT, SynTHBloKMix24::EXT2_L_INPUT};

        float envL[6], envR[6], mL, mR;
        bool conn[6], stereo[6], mute[6], solo[6], anySolo = false;
        for (int c = 0; c < 6; c++) {
            if (module) {
                envL[c] = module->chEnvLR[c][0];
                envR[c] = module->chEnvLR[c][1];
                bool lc = module->inputs[inL[c]].isConnected(), rc = module->inputs[inL[c] + 1].isConnected();
                conn[c] = lc || rc;
                stereo[c] = lc && rc;
                mute[c] = module->params[muteP[c]].getValue() > 0.5f;
                solo[c] = module->params[soloP[c]].getValue() > 0.5f;
                anySolo = anySolo || solo[c];
            } else {   // browser preview
                static const float demo[6] = {0.55f, 0.30f, 0.42f, 0.12f, 0.0f, 0.20f};
                envL[c] = demo[c]; envR[c] = demo[c] * 0.9f;
                conn[c] = demo[c] > 0.f; stereo[c] = c % 3 != 1; mute[c] = false; solo[c] = false;
            }
        }
        if (module) { mL = module->masterLLevel; mR = module->masterRLevel; }
        else { mL = 0.62f; mR = 0.55f; }

        // header
        const float yh = mm2px(oled::kRow * 0.5f);
        nvgFontSize(a.vg, 6.f);
        nvgFillColor(a.vg, oled::dim());
        nvgTextAlign(a.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(a.vg, 4.f, yh, "LEVELS", NULL);
        nvgTextAlign(a.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(a.vg, W - 4.f, yh, "dBFS", NULL);

        // geometry
        const float x0 = 4.f, scaleW = mm2px(4.2f);
        const float y0 = mm2px(oled::kRow + 1.6f), y1 = H - mm2px(2.f * oled::kRow + 0.4f);
        const float inner = W - x0 - 4.f - scaleW;
        const float gap = mm2px(1.4f), pm = mm2px(4.2f);
        const float pc = (inner - gap - 2.f * pm) / 6;

        nvgBeginPath(a.vg);
        oled::divider(a, 4.f, mm2px(oled::kRow), W - 4.f, mm2px(oled::kRow));
        oled::divider(a, 4.f, H - mm2px(oled::kRow), W - 4.f, H - mm2px(oled::kRow));
        // scale lines at 0 / -12 / -24 / -36 dB
        static const float marks[4] = {0.f, -12.f, -24.f, -36.f};
        for (float db : marks) {
            float y = y1 - (y1 - y0) * (db - DB_LO) / (DB_HI - DB_LO);
            oled::divider(a, x0, y, x0 + inner, y);
        }
        oled::dividerStroke(a);
        nvgFontSize(a.vg, 5.f);
        nvgTextAlign(a.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFillColor(a.vg, oled::dim());
        for (float db : marks) {
            float y = y1 - (y1 - y0) * (db - DB_LO) / (DB_HI - DB_LO);
            char s[8];
            snprintf(s, sizeof s, "%d", (int) db);
            nvgText(a.vg, W - 4.f, y, s, NULL);
        }

        // channel bars + numbers
        const float numY = y1 + mm2px(1.9f);
        char buf[32];
        for (int c = 0; c < 6; c++) {
            float x = x0 + c * pc;
            bool off = anySolo ? !solo[c] : mute[c];
            bool dead = !conn[c] || off;
            const float lrGap = mm2px(0.4f);   // a visible split, so each pair reads as L + R
            float bw = (pc - mm2px(0.7f) - lrGap) * 0.5f;
            float bx = x + mm2px(0.35f);
            if (stereo[c]) {   // both jacks patched: L + R bars
                bar(a, bx, y0, y1, bw, dead ? 0.f : envL[c], peak[2 * c]);
                bar(a, bx + bw + lrGap, y0, y1, bw, dead ? 0.f : envR[c], peak[2 * c + 1]);
            } else {           // one jack (or none): a single mono bar
                bar(a, bx, y0, y1, 2.f * bw + lrGap, dead ? 0.f : std::max(envL[c], envR[c]), peak[2 * c]);
                peak[2 * c + 1] = 0.f;
            }
            NVGcolor nc = mute[c] ? nvgRGBA(255, 50, 40, 255) : solo[c] ? nvgRGBA(255, 220, 90, 255)
                        : dead ? oled::amber(70) : oled::amber();
            snprintf(buf, sizeof buf, "%d", c + 1);
            nvgFontSize(a.vg, 5.f);
            nvgTextAlign(a.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(a.vg, nc);
            nvgText(a.vg, x + pc * 0.5f, numY, buf, NULL);
        }
        // master L / R
        float mxs[2] = {mL, mR};
        static const char* const mn[2] = {"L", "R"};
        for (int k = 0; k < 2; k++) {
            float x = x0 + 6 * pc + gap + k * pm;
            bar(a, x + mm2px(0.3f), y0, y1, pm - mm2px(0.6f), mxs[k], peak[12 + k]);
            nvgFontSize(a.vg, 5.f);
            nvgTextAlign(a.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(a.vg, oled::amber());
            nvgText(a.vg, x + pm * 0.5f, numY, mn[k], NULL);
        }

        // footer: master level, peak hold, clip lamp
        float mx = std::max(mL, mR);
        peakMaster = std::max(peakMaster, mx);
        if (mx >= 0.97f) clipHold = 1.f;
        else clipHold = std::max(0.f, clipHold - 0.004f);
        const float yf = H - mm2px(oled::kRow * 0.5f);
        float odb = toDb(mx), pdb = toDb(peakMaster);
        nvgTextAlign(a.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(a.vg, 6.f);
        nvgFillColor(a.vg, oled::dim());
        nvgText(a.vg, 4.f, yf, "OUT", NULL);
        if (odb < -60.f) snprintf(buf, sizeof buf, "-inf");
        else snprintf(buf, sizeof buf, "%.1f", odb);
        nvgFontSize(a.vg, 7.5f);
        nvgFillColor(a.vg, odb > -1.f ? nvgRGBA(255, 50, 40, 255) : oled::amber());
        nvgText(a.vg, mm2px(6.f), yf, buf, NULL);
        if (pdb < -60.f) snprintf(buf, sizeof buf, "PK --");
        else snprintf(buf, sizeof buf, "PK %.1f", pdb);
        nvgFontSize(a.vg, 6.f);
        nvgFillColor(a.vg, oled::dim());
        nvgText(a.vg, W * 0.42f, yf, buf, NULL);
        nvgTextAlign(a.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFillColor(a.vg, clipHold > 0.f ? nvgRGBA(255, 50, 40, (int) (120 + 135 * clipHold)) : oled::amber(60));
        nvgText(a.vg, W - 4.f, yf, "CLIP", NULL);
        if (clipHold > 0.f) {
            nvgBeginPath(a.vg);
            nvgCircle(a.vg, W - 4.f - mm2px(5.4f), yf, mm2px(0.5f));
            nvgFillColor(a.vg, nvgRGBA(255, 50, 40, (int) (255 * clipHold)));
            nvgFill(a.vg);
        }
        nvgResetScissor(a.vg);
    }
};

// FX OLED (the MIX-12 design): program number + name with a wet meter, what P1..P3 do for it with values,
// return, the 6 sends.
struct MixFxDisplay : TransparentWidget {
    SynTHBloKMix24* module = nullptr;
    void bar(const DrawArgs& a, float x, float y, float w, float h, float v, NVGcolor c) {
        nvgBeginPath(a.vg);
        nvgRect(a.vg, x, y, w, h);
        nvgFillColor(a.vg, nvgRGBA(46, 29, 10, 255));
        nvgFill(a.vg);
        nvgBeginPath(a.vg);
        nvgRect(a.vg, x, y, w * rack::math::clamp(v, 0.f, 1.f), h);
        nvgFillColor(a.vg, c);
        nvgFill(a.vg);
    }
    void drawLayer(const DrawArgs& a, int layer) override {
        if (layer != 1) return;
        const float W = box.size.x, H = box.size.y;
        oled::box(a, W, H);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(a.vg, font->handle);
        nvgScissor(a.vg, 0, 0, W, H);
        static const char* const names[8] = {"OFF", "ROOM", "HALL", "PLATE", "ECHO", "PING-PONG", "CHORUS", "FLANGER"};
        static const char* const pn[8][3] = {{"P1", "P2", "P3"}, {"SIZE", "DAMP", "PRE"}, {"SIZE", "DAMP", "PRE"}, {"DECAY", "TONE", "PRE"},
                                             {"TIME", "FBK", "TONE"}, {"TIME", "FBK", "TONE"}, {"RATE", "DEPTH", "SPREAD"}, {"RATE", "DEPTH", "FBK"}};
        int sel = 2;
        float p[3] = {0.5f, 0.5f, 0.4f}, ret = 0.5f, wet = 0.35f;
        float snd[6] = {0.6f, 0.3f, 0.f, 0.8f, 0.f, 0.15f};
        if (module) {
            sel = (int) std::round(module->params[SynTHBloKMix24::OB_FX_SEL_PARAM].getValue());
            for (int i = 0; i < 3; i++) p[i] = module->params[SynTHBloKMix24::OB_FX_P1_PARAM + i].getValue();
            ret = module->params[SynTHBloKMix24::OB_FX_RET_PARAM].getValue();
            for (int c = 0; c < 6; c++) snd[c] = module->params[SynTHBloKMix24::OB_SEND_1_PARAM + c].getValue();
            wet = rack::math::clamp(module->fxWetEnv / 1.3f, 0.f, 1.f);
        }
        sel = rack::math::clamp(sel, 0, 7);
        const bool off = sel == 0;
        const NVGcolor ORG = nvgRGBA(255, 150, 30, 255);
        char buf[32];
        const float u = mm2px(1.f);

        // header: program number + name, wet meter
        const float yh = mm2px(oled::kRow * 0.5f);
        snprintf(buf, sizeof buf, "%d", sel);
        nvgTextAlign(a.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(a.vg, 6.f);
        nvgFillColor(a.vg, oled::dim());
        nvgText(a.vg, 4.f, yh, buf, NULL);
        nvgFontSize(a.vg, 8.f);
        nvgFillColor(a.vg, off ? oled::dim() : oled::amber());
        nvgText(a.vg, 4.f + 2.6f * u, yh, names[sel], NULL);
        for (int i = 0; i < 8; i++) {
            bool lit = !off && (i + 1) / 8.f <= std::sqrt(wet) + 1e-4f;
            nvgBeginPath(a.vg);
            nvgRect(a.vg, W - 4.f - (8 - i) * 1.2f * u + 0.2f * u, yh - 1.4f * u, 0.8f * u, 2.8f * u);
            nvgFillColor(a.vg, lit ? (i > 5 ? nvgRGBA(255, 60, 40, 255) : ORG) : nvgRGBA(46, 29, 10, 255));
            nvgFill(a.vg);
        }
        nvgBeginPath(a.vg);
        oled::divider(a, 4.f, mm2px(oled::kRow), W - 4.f, mm2px(oled::kRow));
        oled::divider(a, 4.f, H - mm2px(7.4f), W - 4.f, H - mm2px(7.4f));
        oled::dividerStroke(a);

        // P1..P3 + return
        for (int i = 0; i < 4; i++) {
            float y = mm2px(oled::kRow + 2.6f + 3.9f * i);
            const char* nm = i < 3 ? pn[sel][i] : "RETURN";
            float v = i < 3 ? p[i] : ret;
            NVGcolor c = off ? oled::dim() : oled::amber();
            nvgTextAlign(a.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFontSize(a.vg, 6.f);
            nvgFillColor(a.vg, oled::dim());
            nvgText(a.vg, 4.f, y, nm, NULL);
            bar(a, 12.5f * u, y - 1.f * u, W - 12.5f * u - 10.f * u, 2.f * u, v, off ? nvgRGBA(255, 150, 30, 90) : ORG);
            if (i == 0 && (sel == 4 || sel == 5)) snprintf(buf, sizeof buf, "%dms", (int) (20.f + 1180.f * v * v));
            else if (i == 0 && (sel == 6 || sel == 7)) snprintf(buf, sizeof buf, "%.1fHz", 0.05f + 5.f * v * v);
            else snprintf(buf, sizeof buf, "%d%%", (int) std::round(v * 100.f));
            nvgTextAlign(a.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgFontSize(a.vg, 6.5f);
            nvgFillColor(a.vg, c);
            nvgText(a.vg, W - 4.f, y, buf, NULL);
        }

        // channel sends 1..6
        const float sy1 = H - mm2px(1.4f), sh = mm2px(4.6f), sx0 = 9.f * u, sw = (W - 4.f - sx0) / 6;
        nvgTextAlign(a.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(a.vg, 6.f);
        nvgFillColor(a.vg, oled::dim());
        nvgText(a.vg, 4.f, sy1 - sh * 0.5f, "SEND", NULL);
        for (int c = 0; c < 6; c++) {
            float x = sx0 + c * sw + 0.2f * u, w = sw - 0.4f * u;
            nvgBeginPath(a.vg);
            nvgRect(a.vg, x, sy1 - sh, w, sh);
            nvgFillColor(a.vg, nvgRGBA(46, 29, 10, 255));
            nvgFill(a.vg);
            float h = sh * rack::math::clamp(snd[c], 0.f, 1.f);
            if (h > 0.f) {
                nvgBeginPath(a.vg);
                nvgRect(a.vg, x, sy1 - h, w, h);
                nvgFillColor(a.vg, ORG);
                nvgFill(a.vg);
            }
        }
        nvgResetScissor(a.vg);
    }
};

// FX knob: the strips' fire arc on a bigger knob, inside MATRIX-DUO style dash marks. d = the mark ring's size
// class (11 mm small, 19 mm big); the travel matches the marks' 270 deg. Returns the knob (e.g. to set snap).
template <class K>
static ArcKnob<K>* mixFxKnob(ModuleWidget* w, Module* m, float x, float y, float d, int param) {
    w->addChild(KnobTicks::create(Vec(x, y), d / 11.f));
    auto* k = createParamCentered<ArcKnob<K>>(mm2px(Vec(x, y)), m, param);
    k->arcR = d * 0.5f - 0.5f;
    k->minAngle = -0.75f * float(M_PI);
    k->maxAngle = 0.75f * float(M_PI);
    w->addParam(k);
    return k;
}

// Title + every static label on the panel, as on MIX-12 / MIX-8 (the SVG carries no text).
struct MixPanelText : TransparentWidget {
    SynTHBloKMix24* module = nullptr;
    void text(const DrawArgs& a, float xMm, float yMm, const char* s, float size, NVGcolor c) {
        nvgFontSize(a.vg, size);
        nvgFillColor(a.vg, c);
        nvgText(a.vg, mm2px(xMm), mm2px(yMm), s, NULL);
    }
    void draw(const DrawArgs& a) override {
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(a.vg, font->handle);
        // title
        nvgTextAlign(a.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
        text(a, 8.5f, 3.5f, "SynTHBloK", 11.f, nvgRGB(0xff, 0xff, 0xff));
        nvgTextLetterSpacing(a.vg, 0.8f);
        text(a, 8.5f, 6.4f, "MIX-6  FIRE EDITION", 10.f, nvgRGB(0xff, 0x7a, 0x00));
        nvgTextLetterSpacing(a.vg, 0.6f);

        const NVGcolor LBL = nvgRGB(0xbb, 0xbb, 0xbb), SUB = nvgRGB(0x88, 0x88, 0x88);
        nvgTextAlign(a.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        char num[4];
        for (int c = 0; c < 6; c++) {
            float x = MXL::CX[c];
            snprintf(num, sizeof num, "%d", c + 1);
            text(a, x, 12.2f, num, 8.f, nvgRGB(0xff, 0x7a, 0x00));
            bool w = module ? module->widthMode[c] : (c != 0 && c != 4);
            const char* kn[7] = {"LVL", w ? "WID" : "PAN", "TILT", "DRIVE", "AIR", "GLUE", "SEND"};
            for (int k = 0; k < 7; k++) text(a, x, MXL::KY[k] + 5.8f, kn[k], 5.5f, SUB);
            text(a, x - 5.2f, MXL::LJ_Y, "L", 6.f, LBL);
            text(a, x - 5.2f, MXL::RJ_Y, "R", 6.f, LBL);
        }
        // master
        const float mc = MXL::MCOL + MXL::MCOL_W * 0.5f;
        text(a, mc, 12.2f, "MASTER", 7.f, LBL);
        static const char* const mk[5] = {"LVL", "WIDTH", "TILT", "GLUE", "CLIP"};
        for (int i = 0; i < 5; i++) text(a, MXL::MKX[i], MXL::MKY + 5.8f, mk[i], 5.5f, SUB);
        text(a, mc, 94.4f, "ROW CV  /  MAIN OUT", 6.f, SUB);
        static const char* const jl[2][4] = {{"LVL", "PAN", "TILT", "DRIVE"}, {"AIR", "GLUE", "OUT L", "OUT R"}};
        for (int r = 0; r < 2; r++)
            for (int i = 0; i < 4; i++) text(a, MXL::JX[i], MXL::JY[r] - 4.8f, jl[r][i], 6.f, LBL);
        // FX
        text(a, MXL::FC, 12.2f, "AUX FX", 7.f, LBL);
        static const char* const pl[3] = {"P1", "P2", "P3"};
        for (int i = 0; i < 3; i++) text(a, MXL::PX[i], MXL::PY + 7.f, pl[i], 6.f, LBL);
        text(a, MXL::FC, MXL::RET[1] + 12.f, "RETURN", 6.f, LBL);
        text(a, MXL::FC, MXL::FXC[1] + 7.f, "PROGRAM", 6.f, LBL);
        static const char* const fj[4] = {"SND L", "SND R", "RTN L", "RTN R"};
        for (int i = 0; i < 4; i++) text(a, MXL::FXJ_X[i], MXL::FXJ_Y - 4.8f, fj[i], 5.5f, LBL);
    }
};

// -------------------------------------------------------------
// WIDGET IMPLEMENTATION
// -------------------------------------------------------------
SynTHBloKMix24Widget::SynTHBloKMix24Widget(SynTHBloKMix24* module) {
    setModule(module);
    setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKMix24.svg")));
    box.size = Vec(38 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);   // 38 HP (the FX column is as wide as on MIX-12 / MIX-8)

    using namespace rack::componentlibrary;
    auto mm = [](float x, float y) { return mm2px(Vec(x, y)); };

    addChild(createWidgetCentered<SocketScrew>(mm(3.5f, 3.5f)));
    addChild(createWidgetCentered<SocketScrew>(mm(MXL::PANEL_W - 3.5f, 3.5f)));
    addChild(createWidgetCentered<SocketScrew>(mm(3.5f, 125.0f)));
    addChild(createWidgetCentered<SocketScrew>(mm(MXL::PANEL_W - 3.5f, 125.0f)));

    MixPanelText* t = createWidget<MixPanelText>(Vec(0.f, 0.f));
    t->box.size = mm(MXL::PANEL_W, 128.5f);
    t->module = module;
    addChild(t);

    // channel strips: param ids are 8 per channel (LVL, PAN|WID, TILT, DRIVE, AIR, GLUE, MUTE, SOLO)
    static const int base[6] = {SynTHBloKMix24::MAIN_LVL_PARAM, SynTHBloKMix24::POLY_LVL_PARAM, SynTHBloKMix24::STEREO_LVL_PARAM,
                                SynTHBloKMix24::FX_LVL_PARAM, SynTHBloKMix24::EXT1_LVL_PARAM, SynTHBloKMix24::EXT2_LVL_PARAM};
    static const int inL[6] = {SynTHBloKMix24::MAIN_L_INPUT, SynTHBloKMix24::POLY_L_INPUT, SynTHBloKMix24::STEREO_L_INPUT,
                               SynTHBloKMix24::FX_L_INPUT, SynTHBloKMix24::EXT1_L_INPUT, SynTHBloKMix24::EXT2_L_INPUT};
    for (int c = 0; c < 6; c++) {
        float x = MXL::CX[c];
        for (int k = 0; k < 6; k++) {
            auto* kn = createParamCentered<ArcKnob<Trimpot>>(mm(x, MXL::KY[k]), module, base[c] + k);
            if (module) kn->eff = [module, c, k]() { return mixEffCh(module, c, k); };
            addParam(kn);
        }
        addParam(createParamCentered<ArcKnob<Trimpot>>(mm(x, MXL::KY[6]), module, SynTHBloKMix24::OB_SEND_1_PARAM + c));
        auto* mu = createParamCentered<MixSwitch>(mm(x - MXL::MB_DX, MXL::MB_Y), module, base[c] + 6);
        mu->onCol = nvgRGBA(255, 50, 40, 255); mu->label = "MUTE"; mu->partner = 1;
        addParam(mu);
        auto* so = createParamCentered<MixSwitch>(mm(x + MXL::MB_DX, MXL::MB_Y), module, base[c] + 7);
        so->onCol = nvgRGBA(255, 205, 50, 255); so->label = "SOLO"; so->partner = -1;
        addParam(so);
        addInput(createInputCentered<SmallJack>(mm(x + MXL::JDX, MXL::LJ_Y), module, inL[c]));
        addInput(createInputCentered<SmallJack>(mm(x + MXL::JDX, MXL::RJ_Y), module, inL[c] + 1));
    }

    // master
    static const int mp[5] = {SynTHBloKMix24::MASTER_LVL_PARAM, SynTHBloKMix24::MASTER_WIDTH_PARAM, SynTHBloKMix24::MASTER_TILT_PARAM,
                              SynTHBloKMix24::MASTER_GLUE_PARAM, SynTHBloKMix24::MASTER_CLIP_PARAM};
    for (int i = 0; i < 5; i++) {
        auto* k = createParamCentered<ArcKnob<Trimpot>>(mm(MXL::MKX[i], MXL::MKY), module, mp[i]);
        if (module) k->eff = [module, i]() { return mixEffMaster(module, i); };
        addParam(k);
    }

    auto* d = new MixLevelsDisplay();
    d->module = module;
    d->box.pos = mm2px(Vec(MXL::D1[0], MXL::D1[1]));
    d->box.size = mm2px(Vec(MXL::D1[2], MXL::D1[3]));
    addChild(d);

    // onboard FX, as on MIX-12 / MIX-8: P1..P3 small ring knobs, the big RETURN, program selector (detented)
    {
        mixFxKnob<RoundSmallBlackKnob>(this, module, MXL::FXC[0], MXL::FXC[1], 11.f, SynTHBloKMix24::OB_FX_SEL_PARAM)->snap = true;
        static const int pp[3] = {SynTHBloKMix24::OB_FX_P1_PARAM, SynTHBloKMix24::OB_FX_P2_PARAM, SynTHBloKMix24::OB_FX_P3_PARAM};
        for (int i = 0; i < 3; i++)
            mixFxKnob<RoundSmallBlackKnob>(this, module, MXL::PX[i], MXL::PY, 11.f, pp[i]);
        mixFxKnob<RoundLargeBlackKnob>(this, module, MXL::RET[0], MXL::RET[1], 19.f, SynTHBloKMix24::OB_FX_RET_PARAM);
        auto* fd = new MixFxDisplay();
        fd->module = module;
        fd->box.pos = mm2px(Vec(MXL::D2[0], MXL::D2[1]));
        fd->box.size = mm2px(Vec(MXL::D2[2], MXL::D2[3]));
        addChild(fd);
    }

    // external FX loop jacks
    addOutput(createOutputCentered<SmallJack>(mm(MXL::FXJ_X[0], MXL::FXJ_Y), module, SynTHBloKMix24::FX_SEND_L_OUTPUT));
    addOutput(createOutputCentered<SmallJack>(mm(MXL::FXJ_X[1], MXL::FXJ_Y), module, SynTHBloKMix24::FX_SEND_R_OUTPUT));
    addInput(createInputCentered<SmallJack>(mm(MXL::FXJ_X[2], MXL::FXJ_Y), module, SynTHBloKMix24::FX_RTN_L_INPUT));
    addInput(createInputCentered<SmallJack>(mm(MXL::FXJ_X[3], MXL::FXJ_Y), module, SynTHBloKMix24::FX_RTN_R_INPUT));
    addChild(JackLed::create(Vec(MXL::FXJ_X[0] + 3.6f, MXL::FXJ_Y), module, SynTHBloKMix24::FX_SEND_L_OUTPUT, true));
    addChild(JackLed::create(Vec(MXL::FXJ_X[1] + 3.6f, MXL::FXJ_Y), module, SynTHBloKMix24::FX_SEND_R_OUTPUT, true));

    // global CV (row-wise) + master outputs share one 4 x 2 grid under the levels display
    static const int gcv[6] = {SynTHBloKMix24::GLOBAL_1_CV_INPUT, SynTHBloKMix24::GLOBAL_2_CV_INPUT, SynTHBloKMix24::GLOBAL_3_CV_INPUT,
                               SynTHBloKMix24::GLOBAL_4_CV_INPUT, SynTHBloKMix24::GLOBAL_5_CV_INPUT, SynTHBloKMix24::GLOBAL_6_CV_INPUT};
    for (int i = 0; i < 4; i++)
        addInput(createInputCentered<SmallJack>(mm(MXL::JX[i], MXL::JY[0]), module, gcv[i]));
    addInput(createInputCentered<SmallJack>(mm(MXL::JX[0], MXL::JY[1]), module, gcv[4]));
    addInput(createInputCentered<SmallJack>(mm(MXL::JX[1], MXL::JY[1]), module, gcv[5]));
    addOutput(createOutputCentered<SmallJack>(mm(MXL::JX[2], MXL::JY[1]), module, SynTHBloKMix24::MASTER_L_OUTPUT));
    addOutput(createOutputCentered<SmallJack>(mm(MXL::JX[3], MXL::JY[1]), module, SynTHBloKMix24::MASTER_R_OUTPUT));
    addChild(JackLed::create(Vec(MXL::JX[2] + 3.6f, MXL::JY[1]), module, SynTHBloKMix24::MASTER_L_OUTPUT, true));
    addChild(JackLed::create(Vec(MXL::JX[3] + 3.6f, MXL::JY[1]), module, SynTHBloKMix24::MASTER_R_OUTPUT, true));
}

void SynTHBloKMix24Widget::appendContextMenu(Menu* menu) {
    SynTHBloKMix24* m = getModule<SynTHBloKMix24>();
    if (!m) return;
    menu->addChild(new MenuSeparator);
    menu->addChild(createMenuLabel("PAN / WIDTH knob"));
    menu->addChild(createMenuItem("All channels: pan", "", [=]() { for (int c = 0; c < 6; c++) m->setWidthMode(c, false); }));
    menu->addChild(createMenuItem("All channels: stereo width", "", [=]() { for (int c = 0; c < 6; c++) m->setWidthMode(c, true); }));
    menu->addChild(createSubmenuItem("Per channel", "", [=](Menu* sub) {
        for (int c = 0; c < 6; c++)
            sub->addChild(createCheckMenuItem(string::f("CH %d: stereo width", c + 1), "",
                                              [=]() { return m->widthMode[c]; }, [=]() { m->setWidthMode(c, !m->widthMode[c]); }));
    }));
}

// -------------------------------------------------------------
// MODEL
// -------------------------------------------------------------
Model* modelSynTHBloKMix24 = createModel<SynTHBloKMix24, SynTHBloKMix24Widget>("Mix24");
