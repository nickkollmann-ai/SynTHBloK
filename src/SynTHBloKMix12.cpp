// SynTHBloK MIX-12 Fire Edition: the MIX-24 channel strip x 12, master bus, onboard aux FX + external FX loop.
// 55 HP. Labels and the title are drawn in code; the panel SVG holds only the background and section plates.
#include "plugin.hpp"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"   // MIX-24's FX header packs statements onto one line
#include "SynTHBloKMix24_fx.hpp"
#pragma GCC diagnostic pop
#include "OledUi.hpp"
#include "JackLed.hpp"
#include "KnobTicks.hpp"
#include "ModulePictures.hpp"

using namespace rack;

static const int M12_CH = 12;
static const int M12_CP = 8;   // params per channel: LVL, PAN|WID, TILT, DRIVE, AIR, GLUE, MUTE, SOLO

// Panel layout (mm)
namespace M12L {
static const float PANEL_W = 279.4f;
static float CX(int c) { return 9.5f + 14.5f * c; }
static const float KY[7] = {20.f, 31.f, 42.f, 53.f, 64.f, 75.f, 86.f};
static const float MB_Y = 98.f, MB_DX = 3.7f;
static const float LJ_Y = 111.4f, RJ_Y = 120.2f;
// the FX column sits next to the strips, MASTER at the right edge
static const float MCOL = 220.5f, MCOL_W = 56.4f;
static const float MKX[5] = {MCOL + 6.8f, MCOL + 17.4f, MCOL + 28.0f, MCOL + 38.6f, MCOL + 49.2f};
static const float MKY = 21.f;
static const float D1[4] = {MCOL + 1.5f, 31.9f, 53.4f, 56.f};       // levels OLED, centred in its plate (29.2 .. 90.6)
static const float JX[4] = {MCOL + 11.1f, MCOL + 22.5f, MCOL + 33.9f, MCOL + 45.3f};
static const float JY[2] = {104.4f, 115.6f};
static const float FCOL = 180.f, FCOL_W = 38.1f, FC = FCOL + FCOL_W * 0.5f;
static const float D2[4] = {FCOL + 1.5f, 16.2f, FCOL_W - 3.f, 30.f};   // FX OLED
// FX knobs as on MATRIX-DUO's FINAL stage: small knobs with an 11 mm ring + ticks, the big RETURN with a 19 mm one
static const float PX[3] = {FC - 12.f, FC, FC + 12.f};
static const float PY = 54.5f;
static const float RET_Y = 76.f;
static const float SEL_Y = 100.f;
static const float FXJ_X[4] = {FC - 13.2f, FC - 4.4f, FC + 4.4f, FC + 13.2f};
static const float FXJ_Y = 118.2f;   // jacks along the foot of the column
}

// -------------------------------------------------------------
// MODULE
// -------------------------------------------------------------
struct SynTHBloKMix12 : Module {
    enum ParamIds {
        CH_PARAM,                                   // + c * 8 + k
        SEND_PARAM = CH_PARAM + M12_CH * M12_CP,    // + c
        MASTER_LVL_PARAM = SEND_PARAM + M12_CH,
        MASTER_WIDTH_PARAM,
        MASTER_TILT_PARAM,
        MASTER_GLUE_PARAM,
        MASTER_CLIP_PARAM,
        FX_SEL_PARAM, FX_P1_PARAM, FX_P2_PARAM, FX_P3_PARAM, FX_RET_PARAM,
        PARAMS_LEN
    };
    enum InputIds {
        GLOBAL_CV_INPUT,                  // + row (0 LVL .. 5 GLUE)
        IN_INPUT = GLOBAL_CV_INPUT + 6,   // + 2c = L, + 2c + 1 = R
        FX_RTN_L_INPUT = IN_INPUT + 2 * M12_CH,
        FX_RTN_R_INPUT,
        INPUTS_LEN
    };
    enum OutputIds { MASTER_L_OUTPUT, MASTER_R_OUTPUT, FX_SEND_L_OUTPUT, FX_SEND_R_OUTPUT, OUTPUTS_LEN };
    enum LightIds { LIGHTS_LEN };
    enum { K_LVL, K_PAN, K_TILT, K_DRIVE, K_AIR, K_GLUE, K_MUTE, K_SOLO };

    static int chP(int c, int k) { return CH_PARAM + c * M12_CP + k; }

    bool widthMode[M12_CH] = {};   // false: the knob pans, true: it sets stereo width

    // DSP state (index M12_CH = the master bus)
    float tiltState[M12_CH + 1][2] = {};
    float lpL[M12_CH] = {}, lpR[M12_CH] = {};
    float glueEnvL[M12_CH] = {}, glueEnvR[M12_CH] = {};
    float busEnv = 0.f;
    MixFx fx;

    // metering (read by the displays and knob halos)
    float chEnv[M12_CH] = {};
    float chEnvLR[M12_CH][2] = {};
    float masterLLevel = 0.f, masterRLevel = 0.f;
    float fxWetEnv = 0.f;

    float lastDt = -1.f, cTilt = 0.f, cAir = 0.f, fEnv = 0.f, fWet = 0.f, fMaster = 0.f;

    SynTHBloKMix12() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        for (int c = 0; c < M12_CH; c++) {
            int n = c + 1;
            configParam(chP(c, K_LVL), 0.f, 2.f, 1.f, string::f("CH %d level", n));
            configParam(chP(c, K_PAN), -1.f, 1.f, 0.f, string::f("CH %d pan", n));
            configParam(chP(c, K_TILT), -1.f, 1.f, 0.f, string::f("CH %d tilt", n));
            configParam(chP(c, K_DRIVE), 0.f, 1.f, 0.f, string::f("CH %d drive", n));
            configParam(chP(c, K_AIR), 0.f, 1.f, 0.f, string::f("CH %d air", n));
            configParam(chP(c, K_GLUE), 0.f, 1.f, 0.f, string::f("CH %d glue", n));
            configSwitch(chP(c, K_MUTE), 0.f, 1.f, 0.f, string::f("CH %d mute", n), {"Off", "On"});
            configSwitch(chP(c, K_SOLO), 0.f, 1.f, 0.f, string::f("CH %d solo", n), {"Off", "On"});
            configParam(SEND_PARAM + c, 0.f, 1.f, 0.f, string::f("CH %d FX send", n));
            configInput(IN_INPUT + 2 * c, string::f("CH %d L", n));
            configInput(IN_INPUT + 2 * c + 1, string::f("CH %d R", n));
        }
        configParam(MASTER_LVL_PARAM, 0.f, 2.f, 1.f, "Master level");
        configParam(MASTER_WIDTH_PARAM, -1.f, 1.f, 0.f, "Master width");
        configParam(MASTER_TILT_PARAM, -1.f, 1.f, 0.f, "Master tilt");
        configParam(MASTER_GLUE_PARAM, 0.f, 1.f, 0.f, "Master glue");
        configParam(MASTER_CLIP_PARAM, 0.f, 1.f, 0.f, "Master clip trim");
        configSwitch(FX_SEL_PARAM, 0.f, 7.f, 2.f, "FX program",
                     {"Off", "Room", "Hall", "Plate", "Echo", "Ping-pong", "Chorus", "Flanger"});
        configParam(FX_P1_PARAM, 0.f, 1.f, 0.5f, "FX param 1");
        configParam(FX_P2_PARAM, 0.f, 1.f, 0.5f, "FX param 2");
        configParam(FX_P3_PARAM, 0.f, 1.f, 0.4f, "FX param 3");
        configParam(FX_RET_PARAM, 0.f, 1.f, 0.5f, "FX return");
        static const char* const rows[6] = {"Level", "Pan/Width", "Tilt", "Drive", "Air", "Glue"};
        for (int r = 0; r < 6; r++) configInput(GLOBAL_CV_INPUT + r, string::f("Global %d (%s row)", r + 1, rows[r]));
        configInput(FX_RTN_L_INPUT, "FX return left (mono if R unpatched)");
        configInput(FX_RTN_R_INPUT, "FX return right");
        configOutput(MASTER_L_OUTPUT, "Master left");
        configOutput(MASTER_R_OUTPUT, "Master right");
        configOutput(FX_SEND_L_OUTPUT, "FX send left");
        configOutput(FX_SEND_R_OUTPUT, "FX send right");
    }

    void setWidthMode(int c, bool w) {
        widthMode[c] = w;
        paramQuantities[chP(c, K_PAN)]->name = string::f("CH %d %s", c + 1, w ? "width" : "pan");
    }
    void onReset() override {
        for (int c = 0; c < M12_CH; c++) setWidthMode(c, false);
    }
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_t* a = json_array();
        for (int c = 0; c < M12_CH; c++) json_array_append_new(a, json_boolean(widthMode[c]));
        json_object_set_new(root, "widthMode", a);
        return root;
    }
    void dataFromJson(json_t* root) override {
        json_t* a = json_object_get(root, "widthMode");
        if (!a) return;
        for (int c = 0; c < M12_CH; c++) {
            json_t* v = json_array_get(a, c);
            setWidthMode(c, v && json_is_true(v));
        }
    }

    static float dbToLin(float db) { return std::pow(10.f, db / 20.f); }

    void tilt(float& L, float& R, float t, int idx) {
        tiltState[idx][0] = tiltState[idx][0] * cTilt + L * (1.f - cTilt);
        tiltState[idx][1] = tiltState[idx][1] * cTilt + R * (1.f - cTilt);
        float lowL = tiltState[idx][0], lowR = tiltState[idx][1];
        float lowGain = 1.f - 0.5f * std::max(0.f, t);
        float highGain = 1.f - 0.5f * std::max(0.f, -t);
        L = lowL * lowGain + (L - lowL) * highGain;
        R = lowR * lowGain + (R - lowR) * highGain;
    }
    static void width(float& L, float& R, float w) {
        float mid = 0.5f * (L + R), side = 0.5f * (L - R) * (1.f + w);
        L = mid + side;
        R = mid - side;
    }

    void process(const ProcessArgs& args) override {
        float dt = args.sampleTime;
        if (dt != lastDt) {
            lastDt = dt;
            cTilt = std::exp(-2.f * float(M_PI) * 800.f * dt);
            cAir = std::exp(-2.f * float(M_PI) * 8000.f * dt);
            fEnv = std::exp(-dt * 25.f);
            fWet = std::exp(-dt * 12.f);
            fMaster = std::exp(-dt * 40.f);
        }

        bool gc[6];
        float gv[6];
        for (int r = 0; r < 6; r++) {
            gc[r] = inputs[GLOBAL_CV_INPUT + r].isConnected();
            gv[r] = gc[r] ? inputs[GLOBAL_CV_INPUT + r].getVoltage() : 0.f;
        }
        bool anySolo = false;
        for (int c = 0; c < M12_CH; c++) anySolo = anySolo || params[chP(c, K_SOLO)].getValue() > 0.5f;

        float sumL = 0.f, sumR = 0.f, sendL = 0.f, sendR = 0.f;
        for (int c = 0; c < M12_CH; c++) {
            bool muted = anySolo ? params[chP(c, K_SOLO)].getValue() <= 0.5f   // a solo silences the rest and beats the channel's own mute
                                 : params[chP(c, K_MUTE)].getValue() > 0.5f;
            Input& il = inputs[IN_INPUT + 2 * c];
            Input& ir = inputs[IN_INPUT + 2 * c + 1];
            bool lc = il.isConnected(), rc = ir.isConnected();
            if (muted || (!lc && !rc)) {
                chEnvLR[c][0] *= fEnv; chEnvLR[c][1] *= fEnv; chEnv[c] *= fEnv;
                continue;
            }
            float L = lc ? il.getVoltage() : 0.f, R = rc ? ir.getVoltage() : 0.f;
            if (lc && !rc) R = L;
            if (!lc && rc) L = R;

            float lvl = params[chP(c, K_LVL)].getValue();
            float pan = params[chP(c, K_PAN)].getValue();
            float tl = params[chP(c, K_TILT)].getValue();
            float drive = params[chP(c, K_DRIVE)].getValue();
            float air = params[chP(c, K_AIR)].getValue();
            float glue = params[chP(c, K_GLUE)].getValue();
            if (gc[0]) lvl *= dbToLin((gv[0] - 5.f) * 0.6f);
            if (gc[1]) pan = clamp(pan + gv[1] / 5.f, -1.f, 1.f);
            if (gc[2]) tl = clamp(tl + gv[2] / 5.f, -1.f, 1.f);
            if (gc[3]) drive = clamp(drive + gv[3] / 10.f, 0.f, 1.f);
            if (gc[4]) air = clamp(air + gv[4] / 10.f, 0.f, 1.f);
            if (gc[5]) glue = clamp(glue + gv[5] / 10.f, 0.f, 1.f);

            L *= lvl;
            R *= lvl;
            if (widthMode[c]) width(L, R, pan);
            else {
                float p = (pan + 1.f) * 0.5f, mid = 0.5f * (L + R);
                L = mid * std::cos(p * float(M_PI) * 0.5f);
                R = mid * std::sin(p * float(M_PI) * 0.5f);
            }
            tilt(L, R, tl, c);
            float g = 1.f + drive * 3.f;
            L = std::tanh(L * g);
            R = std::tanh(R * g);
            lpL[c] = lpL[c] * cAir + L * (1.f - cAir);
            lpR[c] = lpR[c] * cAir + R * (1.f - cAir);
            L += (L - lpL[c]) * air * 1.5f;
            R += (R - lpR[c]) * air * 1.5f;
            if (glue > 0.f) {
                float xL = std::fabs(L), xR = std::fabs(R);
                const float atk = 0.002f, rel = 0.08f, th = 0.6f;
                glueEnvL[c] += (xL - glueEnvL[c]) * (xL > glueEnvL[c] ? atk : rel);
                glueEnvR[c] += (xR - glueEnvR[c]) * (xR > glueEnvR[c] ? atk : rel);
                float grL = glueEnvL[c] > th ? th / (glueEnvL[c] + 1e-6f) : 1.f;
                float grR = glueEnvR[c] > th ? th / (glueEnvR[c] + 1e-6f) : 1.f;
                float mix = glue * 0.6f;
                L = L * (1.f - mix) + L * grL * mix;
                R = R * (1.f - mix) + R * grR * mix;
            }

            chEnvLR[c][0] = std::max(std::fabs(L), chEnvLR[c][0] * fEnv);
            chEnvLR[c][1] = std::max(std::fabs(R), chEnvLR[c][1] * fEnv);
            chEnv[c] = std::max(std::max(std::fabs(L), std::fabs(R)), chEnv[c] * fEnv);
            sumL += L;
            sumR += R;
            float snd = params[SEND_PARAM + c].getValue();
            sendL += L * snd;
            sendR += R * snd;
        }

        // master bus: width, tilt (own state), aux FX return, glue, clip, level
        float L = sumL, R = sumR;
        float masterTilt = params[MASTER_TILT_PARAM].getValue();
        float masterGlue = params[MASTER_GLUE_PARAM].getValue();
        width(L, R, params[MASTER_WIDTH_PARAM].getValue());
        tilt(L, R, masterTilt, M12_CH);
        {
            float wl, wr;
            fx.process(sendL, sendR, args.sampleRate, (int)std::round(params[FX_SEL_PARAM].getValue()),
                       params[FX_P1_PARAM].getValue(), params[FX_P2_PARAM].getValue(), params[FX_P3_PARAM].getValue(), wl, wr);
            outputs[FX_SEND_L_OUTPUT].setVoltage(sendL * 5.f);
            outputs[FX_SEND_R_OUTPUT].setVoltage(sendR * 5.f);
            bool rl = inputs[FX_RTN_L_INPUT].isConnected(), rr = inputs[FX_RTN_R_INPUT].isConnected();
            float xl = rl ? inputs[FX_RTN_L_INPUT].getVoltage() * 0.2f : 0.f;
            float xr = rr ? inputs[FX_RTN_R_INPUT].getVoltage() * 0.2f : 0.f;
            if (rl && !rr) xr = xl;
            if (!rl && rr) xl = xr;
            wl += xl;
            wr += xr;
            float ret = params[FX_RET_PARAM].getValue();
            fxWetEnv = std::max(std::max(std::fabs(wl), std::fabs(wr)) * ret, fxWetEnv * fWet);
            L += wl * ret;
            R += wr * ret;
        }
        {
            float x = std::fabs(0.5f * (L + R));
            busEnv += (x - busEnv) * (x > busEnv ? 0.0015f : 0.12f);
            const float th = 0.7f;
            float gr = busEnv > th ? th / (busEnv + 1e-6f) : 1.f;
            float mix = masterGlue * 0.8f;
            L = L * (1.f - mix) + L * gr * mix;
            R = R * (1.f - mix) + R * gr * mix;
        }
        float clipDrive = 0.4f + params[MASTER_CLIP_PARAM].getValue() * 1.6f;
        float lvl = params[MASTER_LVL_PARAM].getValue();
        L = std::tanh(L * clipDrive) * lvl;
        R = std::tanh(R * clipDrive) * lvl;

        masterLLevel = std::max(std::fabs(L), masterLLevel * fMaster);
        masterRLevel = std::max(std::fabs(R), masterRLevel * fMaster);
        outputs[MASTER_L_OUTPUT].setVoltage(L * 5.f);
        outputs[MASTER_R_OUTPUT].setVoltage(R * 5.f);
    }
};

using M12 = SynTHBloKMix12;

// -------------------------------------------------------------
// UI (the MIX-24 fire look: value arcs on trimpots, MUTE / SOLO OLED switches)
// -------------------------------------------------------------
// fire halo: pale yellow -> yellow-orange -> orange -> orange-red -> deep red
static NVGcolor m12Fire(float t) {
    t = clamp(t, 0.f, 1.f);
    static const float st[5][3] = {{255, 236, 130}, {255, 186, 40}, {255, 112, 10}, {240, 52, 10}, {140, 8, 8}};
    float p = t * 4.f;
    int i = std::min((int)p, 3);
    float k = p - i;
    return nvgRGBA((int)(st[i][0] + (st[i + 1][0] - st[i][0]) * k), (int)(st[i][1] + (st[i + 1][1] - st[i][1]) * k),
                   (int)(st[i][2] + (st[i + 1][2] - st[i][2]) * k), 255);
}

template <class K>
struct M12ArcKnob : K {
    float arcR = 4.4f;   // mm
    std::function<float()> eff;   // effective 0..1 value (knob + CV / live signal) colouring the arc
    float sm = 0.f;
    bool init = false;
    void drawLayer(const typename K::DrawArgs& a, int layer) override {
        K::drawLayer(a, layer);
        if (layer != 1) return;
        float t = 0.5f;
        if (auto* pq = this->getParamQuantity()) t = pq->getScaledValue();
        Vec c = this->box.size.div(2.f);
        float r = mm2px(arcR);
        float e = eff ? clamp(eff(), 0.f, 1.f) : t;
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
        nvgBeginPath(a.vg);
        nvgArc(a.vg, c.x, c.y, r, a0, std::max(av, a0 + 0.02f), NVG_CW);
        nvgStrokeWidth(a.vg, mm2px(0.55f));
        nvgStrokeColor(a.vg, m12Fire(sm));
        nvgStroke(a.vg);
    }
};

// FX knob: the strips' fire arc on a bigger knob, inside MATRIX-DUO style dash marks. d = the mark ring's size
// class (11 mm small, 19 mm big); the travel matches the marks' 270 deg. Returns the knob (e.g. to set snap).
template <class K>
static M12ArcKnob<K>* m12FxKnob(ModuleWidget* w, Module* m, float x, float y, float d, int param) {
    w->addChild(KnobTicks::create(Vec(x, y), d / 11.f));
    auto* k = createParamCentered<M12ArcKnob<K>>(mm2px(Vec(x, y)), m, param);
    k->arcR = d * 0.5f - 0.5f;
    k->minAngle = -0.75f * float(M_PI);
    k->maxAngle = 0.75f * float(M_PI);
    w->addParam(k);
    return k;
}

// effective 0..1 value of channel knob k (0..5) / master knob i, for the arcs
static float m12EffCh(M12* m, int c, int k) {
    if (!m) return 0.f;
    if (k == 0) return std::sqrt(clamp(m->chEnv[c] / 1.3f, 0.f, 1.f));   // LVL: what comes out
    float p = m->params[M12::chP(c, k)].getValue();
    bool cv = m->inputs[M12::GLOBAL_CV_INPUT + k].isConnected();
    float g = cv ? m->inputs[M12::GLOBAL_CV_INPUT + k].getVoltage() : 0.f;
    if (k <= 2) return (clamp(p + g / 5.f, -1.f, 1.f) + 1.f) * 0.5f;
    return clamp(p + g / 10.f, 0.f, 1.f);
}
static float m12EffMaster(M12* m, int i) {
    if (!m) return 0.f;
    float p = m->params[M12::MASTER_LVL_PARAM + i].getValue();
    if (i == 0) return std::sqrt(clamp(std::max(m->masterLLevel, m->masterRLevel) / 1.3f, 0.f, 1.f));
    if (i <= 2) return (p + 1.f) * 0.5f;
    return clamp(p, 0.f, 1.f);
}

static NVGcolor m12WithA(NVGcolor c, int al) { return nvgRGBA((int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255), al); }

// MUTE / SOLO: small rectangular OLED; switching one on releases its partner on the same channel
struct M12Switch : app::ParamWidget {
    NVGcolor onCol = nvgRGBA(255, 60, 40, 255);
    const char* label = "MUTE";
    int partner = 1;   // param offset of the other switch: mute -> +1 (solo), solo -> -1 (mute)
    M12Switch() { box.size = mm2px(Vec(6.4f, 3.6f)); }
    bool isOn() {
        auto* pq = getParamQuantity();
        return pq && pq->getValue() > 0.5f;
    }
    void drawLayer(const DrawArgs& a, int layer) override {
        ParamWidget::drawLayer(a, layer);
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        bool on = isOn();
        if (on) {   // gentle glow
            NVGpaint g = nvgBoxGradient(a.vg, 0.f, 0.f, W, H, 2.f, 4.f, m12WithA(onCol, 55), nvgRGBA(0, 0, 0, 0));
            nvgBeginPath(a.vg);
            nvgRect(a.vg, -4.f, -4.f, W + 8.f, H + 8.f);
            nvgFillPaint(a.vg, g);
            nvgFill(a.vg);
        }
        nvgBeginPath(a.vg);
        nvgRoundedRect(a.vg, 0.5f, 0.5f, W - 1.f, H - 1.f, 1.1f);
        nvgFillColor(a.vg, on ? m12WithA(onCol, 55) : nvgRGBA(10, 7, 3, 255));
        nvgFill(a.vg);
        nvgStrokeWidth(a.vg, 0.7f);
        nvgStrokeColor(a.vg, on ? m12WithA(onCol, 190) : nvgRGBA(255, 122, 0, 90));
        nvgStroke(a.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(a.vg, font->handle);
        nvgFontSize(a.vg, 6.f);
        nvgTextAlign(a.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(a.vg, on ? m12WithA(onCol, 255) : nvgRGBA(255, 190, 90, 105));
        nvgText(a.vg, W * 0.5f, H * 0.5f + 0.3f, label, NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == 0) {
            if (auto* pq = getParamQuantity()) {
                bool turnOn = pq->getValue() <= 0.5f;
                pq->setValue(turnOn ? 1.f : 0.f);
                if (turnOn && pq->module) pq->module->params[pq->paramId + partner].setValue(0.f);
            }
            e.consume(this);
            return;
        }
        ParamWidget::onButton(e);
    }
};

static float m12ToDb(float v) { return v > 1e-5f ? 20.f * std::log10(v) : -120.f; }

// Levels OLED: 12 channel meters (L / R pairs) + master L / R as vertical bars, dBFS scale, peak hold,
// master peak and clip lamp. Channel numbers: red = muted, yellow = solo, dim = unpatched or silenced by a solo.
// Click = reset the peaks and clip.
struct M12LevelsDisplay : TransparentWidget {
    M12* module = NULL;
    float peak[2 * M12_CH + 2] = {};
    float peakMaster = 0.f, clipHold = 0.f;
    static constexpr float DB_LO = -48.f, DB_HI = 6.f;
    static constexpr int N = 24;

    static float pos(float v) { return clamp((m12ToDb(v) - DB_LO) / (DB_HI - DB_LO), 0.f, 1.f); }
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
            bool pkSeg = (int)std::ceil(pp * N) == i + 1 && pp > 0.02f;
            NVGcolor c;
            if (segDb > -1.f) c = nvgRGBA(255, 40, 30, 255);
            else if (segDb > -9.f) c = nvgRGBA(255, 112, 15, 255);
            else c = nvgRGBA(255, 176, 60, 255);
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

        float envL[M12_CH], envR[M12_CH], mL, mR;
        bool conn[M12_CH], stereo[M12_CH], mute[M12_CH], solo[M12_CH], anySolo = false;
        for (int c = 0; c < M12_CH; c++) {
            if (module) {
                envL[c] = module->chEnvLR[c][0];
                envR[c] = module->chEnvLR[c][1];
                bool lc = module->inputs[M12::IN_INPUT + 2 * c].isConnected(), rc = module->inputs[M12::IN_INPUT + 2 * c + 1].isConnected();
                conn[c] = lc || rc;
                stereo[c] = lc && rc;
                mute[c] = module->params[M12::chP(c, M12::K_MUTE)].getValue() > 0.5f;
                solo[c] = module->params[M12::chP(c, M12::K_SOLO)].getValue() > 0.5f;
                anySolo = anySolo || solo[c];
            } else {   // browser preview
                static const float demo[M12_CH] = {0.55f, 0.30f, 0.42f, 0.12f, 0.f, 0.20f, 0.65f, 0.38f, 0.08f, 0.f, 0.27f, 0.47f};
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
        const float pc = (inner - gap - 2.f * pm) / M12_CH;

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
            snprintf(s, sizeof s, "%d", (int)db);
            nvgText(a.vg, W - 4.f, y, s, NULL);
        }

        // channel bars + numbers
        const float numY = y1 + mm2px(1.9f);
        char buf[32];
        for (int c = 0; c < M12_CH; c++) {
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
            float x = x0 + M12_CH * pc + gap + k * pm;
            bar(a, x + mm2px(0.3f), y0, y1, pm - mm2px(0.6f), mxs[k], peak[2 * M12_CH + k]);
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
        float odb = m12ToDb(mx), pdb = m12ToDb(peakMaster);
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
        nvgFillColor(a.vg, clipHold > 0.f ? nvgRGBA(255, 50, 40, (int)(120 + 135 * clipHold)) : oled::amber(60));
        nvgText(a.vg, W - 4.f, yf, "CLIP", NULL);
        if (clipHold > 0.f) {
            nvgBeginPath(a.vg);
            nvgCircle(a.vg, W - 4.f - mm2px(5.4f), yf, mm2px(0.5f));
            nvgFillColor(a.vg, nvgRGBA(255, 50, 40, (int)(255 * clipHold)));
            nvgFill(a.vg);
        }
        nvgResetScissor(a.vg);
    }
};

// FX OLED: program number + name with a wet meter, what P1..P3 do for it with values, return, the 12 sends.
struct M12FxDisplay : TransparentWidget {
    M12* module = NULL;
    void bar(const DrawArgs& a, float x, float y, float w, float h, float v, NVGcolor c) {
        nvgBeginPath(a.vg);
        nvgRect(a.vg, x, y, w, h);
        nvgFillColor(a.vg, nvgRGBA(46, 29, 10, 255));
        nvgFill(a.vg);
        nvgBeginPath(a.vg);
        nvgRect(a.vg, x, y, w * clamp(v, 0.f, 1.f), h);
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
        float snd[M12_CH] = {0.6f, 0.3f, 0.f, 0.8f, 0.f, 0.15f, 0.4f, 0.f, 0.2f, 0.f, 0.5f, 0.1f};
        if (module) {
            sel = (int)std::round(module->params[M12::FX_SEL_PARAM].getValue());
            for (int i = 0; i < 3; i++) p[i] = module->params[M12::FX_P1_PARAM + i].getValue();
            ret = module->params[M12::FX_RET_PARAM].getValue();
            for (int c = 0; c < M12_CH; c++) snd[c] = module->params[M12::SEND_PARAM + c].getValue();
            wet = clamp(module->fxWetEnv / 1.3f, 0.f, 1.f);
        }
        sel = clamp(sel, 0, 7);
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
            if (i == 0 && (sel == 4 || sel == 5)) snprintf(buf, sizeof buf, "%dms", (int)(20.f + 1180.f * v * v));
            else if (i == 0 && (sel == 6 || sel == 7)) snprintf(buf, sizeof buf, "%.1fHz", 0.05f + 5.f * v * v);
            else snprintf(buf, sizeof buf, "%d%%", (int)std::round(v * 100.f));
            nvgTextAlign(a.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgFontSize(a.vg, 6.5f);
            nvgFillColor(a.vg, c);
            nvgText(a.vg, W - 4.f, y, buf, NULL);
        }

        // channel sends 1..12
        const float sy1 = H - mm2px(1.4f), sh = mm2px(4.6f), sx0 = 9.f * u, sw = (W - 4.f - sx0) / M12_CH;
        nvgTextAlign(a.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(a.vg, 6.f);
        nvgFillColor(a.vg, oled::dim());
        nvgText(a.vg, 4.f, sy1 - sh * 0.5f, "SEND", NULL);
        for (int c = 0; c < M12_CH; c++) {
            float x = sx0 + c * sw + 0.2f * u, w = sw - 0.4f * u;
            nvgBeginPath(a.vg);
            nvgRect(a.vg, x, sy1 - sh, w, sh);
            nvgFillColor(a.vg, nvgRGBA(46, 29, 10, 255));
            nvgFill(a.vg);
            float h = sh * clamp(snd[c], 0.f, 1.f);
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

// Title + every static label on the panel (the SVG carries no text).
struct M12PanelText : TransparentWidget {
    M12* module = NULL;
    void text(const DrawArgs& a, float xMm, float yMm, const char* s, float size, NVGcolor c) {
        nvgFontSize(a.vg, size);
        nvgFillColor(a.vg, c);
        nvgText(a.vg, mm2px(xMm), mm2px(yMm), s, NULL);
    }
    void draw(const DrawArgs& a) override {
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(a.vg, font->handle);
        // title, as on MATRIX-DRUM
        nvgTextAlign(a.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
        text(a, 8.5f, 3.5f, "SynTHBloK", 11.f, nvgRGB(0xff, 0xff, 0xff));
        nvgTextLetterSpacing(a.vg, 0.8f);
        text(a, 8.5f, 6.4f, "MIX-12  FIRE EDITION", 10.f, nvgRGB(0xff, 0x7a, 0x00));
        nvgTextLetterSpacing(a.vg, 0.6f);

        const NVGcolor LBL = nvgRGB(0xbb, 0xbb, 0xbb), SUB = nvgRGB(0x88, 0x88, 0x88);
        nvgTextAlign(a.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        char num[4];
        for (int c = 0; c < M12_CH; c++) {
            float x = M12L::CX(c);
            snprintf(num, sizeof num, "%d", c + 1);
            text(a, x, 12.2f, num, 8.f, nvgRGB(0xff, 0x7a, 0x00));
            const char* kn[7] = {"LVL", (module && module->widthMode[c]) ? "WID" : "PAN", "TILT", "DRIVE", "AIR", "GLUE", "SEND"};
            for (int k = 0; k < 7; k++) text(a, x, M12L::KY[k] + 5.8f, kn[k], 5.5f, SUB);
            text(a, x - 5.2f, M12L::LJ_Y, "L", 6.f, LBL);
            text(a, x - 5.2f, M12L::RJ_Y, "R", 6.f, LBL);
        }
        // master
        const float mc = M12L::MCOL + M12L::MCOL_W * 0.5f;
        text(a, mc, 12.2f, "MASTER", 7.f, LBL);
        static const char* const mk[5] = {"LVL", "WIDTH", "TILT", "GLUE", "CLIP"};
        for (int i = 0; i < 5; i++) text(a, M12L::MKX[i], M12L::MKY + 5.8f, mk[i], 5.5f, SUB);
        text(a, mc, 94.4f, "ROW CV  /  MAIN OUT", 6.f, SUB);
        static const char* const jl[2][4] = {{"LVL", "PAN", "TILT", "DRIVE"}, {"AIR", "GLUE", "OUT L", "OUT R"}};
        for (int r = 0; r < 2; r++)
            for (int i = 0; i < 4; i++) text(a, M12L::JX[i], M12L::JY[r] - 4.8f, jl[r][i], 6.f, LBL);
        // FX
        text(a, M12L::FC, 12.2f, "AUX FX", 7.f, LBL);
        static const char* const pl[3] = {"P1", "P2", "P3"};
        for (int i = 0; i < 3; i++) text(a, M12L::PX[i], M12L::PY + 7.f, pl[i], 6.f, LBL);
        text(a, M12L::FC, M12L::RET_Y + 12.f, "RETURN", 6.f, LBL);
        text(a, M12L::FC, M12L::SEL_Y + 7.f, "PROGRAM", 6.f, LBL);
        static const char* const fj[4] = {"SND L", "SND R", "RTN L", "RTN R"};
        for (int i = 0; i < 4; i++) text(a, M12L::FXJ_X[i], M12L::FXJ_Y - 4.8f, fj[i], 5.5f, LBL);
    }
};

// -------------------------------------------------------------
// WIDGET
// -------------------------------------------------------------
struct SynTHBloKMix12Widget : ModuleWidget {
    SynTHBloKMix12Widget(M12* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKMix12.svg")));
        box.size = Vec(55 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);   // 55 HP

        using namespace rack::componentlibrary;
        auto mm = [](float x, float y) { return mm2px(Vec(x, y)); };

        addChild(createWidgetCentered<SocketScrew>(mm(3.5f, 3.5f)));
        addChild(createWidgetCentered<SocketScrew>(mm(M12L::PANEL_W - 3.5f, 3.5f)));
        addChild(createWidgetCentered<SocketScrew>(mm(3.5f, 125.0f)));
        addChild(createWidgetCentered<SocketScrew>(mm(M12L::PANEL_W - 3.5f, 125.0f)));

        M12PanelText* t = createWidget<M12PanelText>(Vec(0.f, 0.f));
        t->box.size = mm(M12L::PANEL_W, 128.5f);
        t->module = module;
        addChild(t);

        // channel strips
        for (int c = 0; c < M12_CH; c++) {
            float x = M12L::CX(c);
            for (int k = 0; k < 6; k++) {
                auto* kn = createParamCentered<M12ArcKnob<Trimpot>>(mm(x, M12L::KY[k]), module, M12::chP(c, k));
                if (module) kn->eff = [module, c, k]() { return m12EffCh(module, c, k); };
                addParam(kn);
            }
            addParam(createParamCentered<M12ArcKnob<Trimpot>>(mm(x, M12L::KY[6]), module, M12::SEND_PARAM + c));
            auto* mu = createParamCentered<M12Switch>(mm(x - M12L::MB_DX, M12L::MB_Y), module, M12::chP(c, M12::K_MUTE));
            mu->onCol = nvgRGBA(255, 50, 40, 255); mu->label = "MUTE"; mu->partner = 1;
            addParam(mu);
            auto* so = createParamCentered<M12Switch>(mm(x + M12L::MB_DX, M12L::MB_Y), module, M12::chP(c, M12::K_SOLO));
            so->onCol = nvgRGBA(255, 205, 50, 255); so->label = "SOLO"; so->partner = -1;
            addParam(so);
            addInput(createInputCentered<SmallJack>(mm(x, M12L::LJ_Y), module, M12::IN_INPUT + 2 * c));
            addInput(createInputCentered<SmallJack>(mm(x, M12L::RJ_Y), module, M12::IN_INPUT + 2 * c + 1));
        }

        // master
        for (int i = 0; i < 5; i++) {
            auto* k = createParamCentered<M12ArcKnob<Trimpot>>(mm(M12L::MKX[i], M12L::MKY), module, M12::MASTER_LVL_PARAM + i);
            if (module) k->eff = [module, i]() { return m12EffMaster(module, i); };
            addParam(k);
        }
        M12LevelsDisplay* d = createWidget<M12LevelsDisplay>(mm(M12L::D1[0], M12L::D1[1]));
        d->box.size = mm(M12L::D1[2], M12L::D1[3]);
        d->module = module;
        addChild(d);
        for (int i = 0; i < 4; i++)
            addInput(createInputCentered<SmallJack>(mm(M12L::JX[i], M12L::JY[0]), module, M12::GLOBAL_CV_INPUT + i));
        addInput(createInputCentered<SmallJack>(mm(M12L::JX[0], M12L::JY[1]), module, M12::GLOBAL_CV_INPUT + 4));
        addInput(createInputCentered<SmallJack>(mm(M12L::JX[1], M12L::JY[1]), module, M12::GLOBAL_CV_INPUT + 5));
        addOutput(createOutputCentered<SmallJack>(mm(M12L::JX[2], M12L::JY[1]), module, M12::MASTER_L_OUTPUT));
        addOutput(createOutputCentered<SmallJack>(mm(M12L::JX[3], M12L::JY[1]), module, M12::MASTER_R_OUTPUT));
        addChild(JackLed::create(Vec(M12L::JX[2] + 3.6f, M12L::JY[1]), module, M12::MASTER_L_OUTPUT, true));
        addChild(JackLed::create(Vec(M12L::JX[3] + 3.6f, M12L::JY[1]), module, M12::MASTER_R_OUTPUT, true));

        // onboard FX + external loop
        M12FxDisplay* fd = createWidget<M12FxDisplay>(mm(M12L::D2[0], M12L::D2[1]));
        fd->box.size = mm(M12L::D2[2], M12L::D2[3]);
        fd->module = module;
        addChild(fd);
        for (int i = 0; i < 3; i++)
            m12FxKnob<RoundSmallBlackKnob>(this, module, M12L::PX[i], M12L::PY, 11.f, M12::FX_P1_PARAM + i);
        m12FxKnob<RoundLargeBlackKnob>(this, module, M12L::FC, M12L::RET_Y, 19.f, M12::FX_RET_PARAM);
        addOutput(createOutputCentered<SmallJack>(mm(M12L::FXJ_X[0], M12L::FXJ_Y), module, M12::FX_SEND_L_OUTPUT));
        addOutput(createOutputCentered<SmallJack>(mm(M12L::FXJ_X[1], M12L::FXJ_Y), module, M12::FX_SEND_R_OUTPUT));
        addInput(createInputCentered<SmallJack>(mm(M12L::FXJ_X[2], M12L::FXJ_Y), module, M12::FX_RTN_L_INPUT));
        addInput(createInputCentered<SmallJack>(mm(M12L::FXJ_X[3], M12L::FXJ_Y), module, M12::FX_RTN_R_INPUT));
        addChild(JackLed::create(Vec(M12L::FXJ_X[0] + 3.6f, M12L::FXJ_Y), module, M12::FX_SEND_L_OUTPUT, true));
        addChild(JackLed::create(Vec(M12L::FXJ_X[1] + 3.6f, M12L::FXJ_Y), module, M12::FX_SEND_R_OUTPUT, true));
        m12FxKnob<RoundSmallBlackKnob>(this, module, M12L::FC, M12L::SEL_Y, 11.f, M12::FX_SEL_PARAM)->snap = true;
    }

    void appendContextMenu(Menu* menu) override {
        M12* m = getModule<M12>();
        if (!m) return;
        ModulePictures::appendMenu(menu);
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("PAN / WIDTH knob"));
        menu->addChild(createMenuItem("All channels: pan", "", [=]() { for (int c = 0; c < M12_CH; c++) m->setWidthMode(c, false); }));
        menu->addChild(createMenuItem("All channels: stereo width", "", [=]() { for (int c = 0; c < M12_CH; c++) m->setWidthMode(c, true); }));
        menu->addChild(createSubmenuItem("Per channel", "", [=](Menu* sub) {
            for (int c = 0; c < M12_CH; c++)
                sub->addChild(createCheckMenuItem(string::f("CH %d: stereo width", c + 1), "",
                                                  [=]() { return m->widthMode[c]; }, [=]() { m->setWidthMode(c, !m->widthMode[c]); }));
        }));
    }
};

Model* modelSynTHBloKMix12 = createModel<SynTHBloKMix12, SynTHBloKMix12Widget>("Mix12");
