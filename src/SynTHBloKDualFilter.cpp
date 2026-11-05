// SynTHBloK DUAL FILTER (44 HP): two stereo multimode filters, A and B.
// Each has CUTOFF, RES, DRIVE, MODE (a continuous sweep LP -> BP -> NOTCH -> HP) and a SLOPE of 12, 24 or 48 dB/oct.
// ROUTING runs them in SERIES (A into B), PARALLEL (both on the input, mixed) or STEREO (A on the left, B on the
// right). LINK makes B's cutoff follow A's, OFFSET octaves away. MIX blends the filtered signal with the dry one.
// LFO: TRI / SAW / SQR / RND, on A, B or both, to the cutoff (TO CUT) and the resonance (TO RES); SYNC locks it to
// a clock on the SYNC jack (RATE then picks the clock division).
// ENVELOPE: FOLLOW (follows the input level) or ADSR (played by the GATE jack or the TRIG cell); AMOUNT sends it
// to both cutoffs, up or down.
// After the mix: a stereo phaser (6 all-pass stages, its own LFO, the right side a quarter cycle on) and a
// three-band EQ (low shelf 120 Hz, peak 1 kHz, high shelf 8 kHz, +-12 dB).
#include "plugin.hpp"
#include "OledUi.hpp"

// Zavalishin TPT state-variable filter, run at 2x the sample rate (see DFFilter::process)
struct DFSvf {
    float s1 = 0.f, s2 = 0.f, lp = 0.f, bp = 0.f, hp = 0.f;
    void process(float x, float g, float k) {
        hp = (x - (k + g) * s1 - s2) / (1.f + g * (k + g));
        float v1 = g * hp;
        bp = v1 + s1;
        s1 = bp + v1;
        float v2 = g * bp;
        lp = v2 + s2;
        s2 = lp + v2;
        // a resonant peak driven hard is held soft, and a NaN or inf can't stick in the state
        s1 = 20.f * std::tanh(s1 * 0.05f);
        if (!std::isfinite(s1) || !std::isfinite(s2)) s1 = s2 = 0.f;
    }
    // the output morphed LP -> BP -> NOTCH -> HP over m = 0..3
    float out(float m) const {
        float nt = lp + hp;
        if (m < 1.f) return lp + (bp - lp) * m;
        if (m < 2.f) return bp + (nt - bp) * (m - 1.f);
        return nt + (hp - nt) * (m - 2.f);
    }
};

// SLOPE: 1, 2 or 4 SVF stages with Butterworth Qs, so 24 and 48 dB/oct keep the -3 dB point at the cutoff.
// RES lifts the Q of the last (highest-Q) stage, by the same amount at every slope.
static const int kStages[3] = {1, 2, 4};
static const float kBwQ[3][4] = {{0.7071f}, {0.5412f, 1.3066f}, {0.5098f, 0.6013f, 0.9000f, 2.5629f}};

// One filter channel: drive, then the stage cascade at 2x
struct DFFilter {
    DFSvf svf[4];
    float last = 0.f;
    // x: input (V), g: tan(pi f / 2sr), k[]: damping per stage, n: stages, drive 0..1, m: mode 0..3
    float process(float x, float g, const float* k, int n, float drive, float m) {
        // drive: tanh with 5 V in -> 5 V out, so turning it up thickens the sound more than it gets louder;
        // faded in over the first quarter of the knob so 0 is the clean signal
        if (drive > 0.f) {
            float pre = 1.f + drive * 9.f;
            float sat = 5.f * std::tanh(0.2f * pre * x) / std::tanh(pre);
            x += (sat - x) * std::min(1.f, drive * 4.f);
        }
        // 2x, so high cutoffs and high resonance stay stable: the midpoint first, then the sample itself
        float y = 0.f;
        for (int sub = 0; sub < 2; sub++) {
            y = sub ? x : 0.5f * (x + last);
            for (int i = 0; i < n; i++) { svf[i].process(y, g, k[i]); y = svf[i].out(m); }
        }
        last = x;
        return y;
    }
};

// Stereo phaser: 6 first-order all-passes per side, swept between 150 Hz and up to 4.8 kHz
struct DFPhaser {
    float z[2][6] = {}, fb[2] = {};
    float process(int c, float x, float a, float feedback, float mix) {
        float y = x + feedback * fb[c];
        for (int i = 0; i < 6; i++) {
            float o = a * y + z[c][i];
            z[c][i] = y - a * o;
            y = o;
        }
        if (!std::isfinite(y)) { y = 0.f; std::fill(z[c], z[c] + 6, 0.f); }
        fb[c] = y;
        return x + (0.5f * (x + y) - x) * mix;
    }
};

// RBJ biquad (transposed direct form II)
struct DFBiquad {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1[2] = {}, z2[2] = {};
    // type 0 low shelf, 1 peak, 2 high shelf
    void set(int type, float hz, float db, float sr) {
        float A = std::pow(10.f, db / 40.f), w = 2.f * float(M_PI) * hz / sr, cw = std::cos(w), sw = std::sin(w);
        float a0, q = type == 1 ? 0.7f : 0.7071f, al = sw / (2.f * q);
        if (type == 1) {
            b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A;
        } else {
            float sA = 2.f * std::sqrt(A) * al, s = type == 0 ? -1.f : 1.f;
            b0 = A * ((A + 1) + s * (A - 1) * cw + sA);
            b1 = -2 * s * A * ((A - 1) + s * (A + 1) * cw);
            b2 = A * ((A + 1) + s * (A - 1) * cw - sA);
            a0 = (A + 1) - s * (A - 1) * cw + sA;
            a1 = 2 * s * ((A - 1) - s * (A + 1) * cw);
            a2 = (A + 1) - s * (A - 1) * cw - sA;
        }
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }
    float process(int c, float x) {
        float y = b0 * x + z1[c];
        z1[c] = b1 * x - a1 * y + z2[c];
        z2[c] = b2 * x - a2 * y;
        return y;
    }
};

struct SynTHBloKDualFilter : Module {
    enum ParamId {
        A_CUT_PARAM, A_RES_PARAM, A_DRIVE_PARAM, A_MODE_PARAM,
        B_CUT_PARAM, B_RES_PARAM, B_DRIVE_PARAM, B_MODE_PARAM,
        ROUTE_PARAM, LINK_PARAM, OFFSET_PARAM,
        LFO_RATE_PARAM, LFO_AMT_PARAM, ENV_AMT_PARAM, SYNC_PARAM, MIX_PARAM,
        A_SLOPE_PARAM, B_SLOPE_PARAM,
        LFO_SHAPE_PARAM, LFO_DEST_PARAM, LFO_RES_PARAM,
        ENV_SRC_PARAM, ENV_A_PARAM, ENV_D_PARAM, ENV_S_PARAM, ENV_R_PARAM, ENV_TRIG_PARAM,
        PH_RATE_PARAM, PH_DEPTH_PARAM, PH_FDBK_PARAM, PH_MIX_PARAM,
        EQ_LO_PARAM, EQ_MID_PARAM, EQ_HI_PARAM,
        PARAMS_LEN
    };
    enum InputId { IN_L_INPUT, IN_R_INPUT, VOCT_INPUT, CUT_A_INPUT, CUT_B_INPUT, SYNC_INPUT, GATE_INPUT, INPUTS_LEN };
    enum OutputId { OUT_A_OUTPUT, OUT_B_OUTPUT, OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN };
    enum RouteId { SERIES, PARALLEL, STEREO };
    enum ShapeId { TRI, SAW, SQR, RND };
    enum DestId { DEST_AB, DEST_A, DEST_B };
    enum EnvSrcId { FOLLOW, ADSR };

    DFFilter fa[2], fb[2];                 // [channel]
    DFPhaser phaser;
    DFBiquad eq[3];
    float eqSet[3] = {99.f, 99.f, 99.f}, eqSr = 0.f;
    float lfoPhase = 0.f, lfoOut = 0.f, rnd = 0.f, phPhase = 0.f;
    float follow = 0.f, adsr = 0.f, env = 0.f;
    bool attacking = false, gate = false;
    float gateLit = 0.f;                   // the TRIG cell lights while the envelope is gated
    dsp::SchmittTrigger syncTrig, gateTrig;
    float sinceClock = 0.f, clockPeriod = 0.5f;
    int clockCount = 0;
    float cutAHz = 1000.f, cutBHz = 1000.f;   // for the displays

    // SYNC on: the RATE knob picks how many clock pulses one LFO cycle takes
    static constexpr int kDivs = 7;
    static constexpr float kDiv[kDivs] = {32.f, 16.f, 8.f, 4.f, 2.f, 1.f, 0.5f};

    SynTHBloKDualFilter() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, 0);
        const char* nm[2] = {"A", "B"};
        for (int f = 0; f < 2; f++) {
            int o = f * 4;
            configParam(A_CUT_PARAM + o, 0.f, 1.f, f ? 0.6f : 0.5f, std::string("Filter ") + nm[f] + " cutoff", " Hz", 1024.f, 20.f);
            configParam(A_RES_PARAM + o, 0.f, 1.f, 0.2f, std::string("Filter ") + nm[f] + " resonance", "%", 0.f, 100.f);
            configParam(A_DRIVE_PARAM + o, 0.f, 1.f, 0.f, std::string("Filter ") + nm[f] + " drive", "%", 0.f, 100.f);
            configParam(A_MODE_PARAM + o, 0.f, 3.f, 0.f, std::string("Filter ") + nm[f] + " mode (0 LP, 1 BP, 2 notch, 3 HP)");
            configSwitch(A_SLOPE_PARAM + f, 0.f, 2.f, 0.f, std::string("Filter ") + nm[f] + " slope", {"12 dB/oct", "24 dB/oct", "48 dB/oct"});
        }
        configSwitch(ROUTE_PARAM, 0.f, 2.f, 0.f, "Routing", {"Series (A into B)", "Parallel", "Stereo (A left, B right)"});
        configSwitch(LINK_PARAM, 0.f, 1.f, 0.f, "Link B's cutoff to A", {"Off", "On"});
        configParam(OFFSET_PARAM, -4.f, 4.f, 1.f, "Link offset (B above A)", " oct");
        configParam(LFO_RATE_PARAM, 0.f, 1.f, 0.4f, "LFO rate", " Hz", 512.f, 0.04f);
        configParam(LFO_AMT_PARAM, 0.f, 1.f, 0.f, "LFO to cutoff", " oct", 0.f, 4.f);
        configParam(LFO_RES_PARAM, 0.f, 1.f, 0.f, "LFO to resonance", "%", 0.f, 50.f);
        configSwitch(LFO_SHAPE_PARAM, 0.f, 3.f, 0.f, "LFO shape", {"Triangle", "Saw", "Square", "Random"});
        configSwitch(LFO_DEST_PARAM, 0.f, 2.f, 0.f, "LFO moves", {"Both filters", "Filter A", "Filter B"});
        configSwitch(SYNC_PARAM, 0.f, 1.f, 0.f, "Sync the LFO to the SYNC jack", {"Off", "On"});
        configSwitch(ENV_SRC_PARAM, 0.f, 1.f, 1.f, "Envelope", {"Follow the input level", "ADSR (GATE jack / TRIG)"});
        configParam(ENV_A_PARAM, 0.f, 1.f, 0.1f, "Attack", " ms", 5000.f, 1.f);
        configParam(ENV_D_PARAM, 0.f, 1.f, 0.5f, "Decay", " ms", 5000.f, 1.f);
        configParam(ENV_S_PARAM, 0.f, 1.f, 0.5f, "Sustain", "%", 0.f, 100.f);
        configParam(ENV_R_PARAM, 0.f, 1.f, 0.5f, "Release", " ms", 5000.f, 1.f);
        configButton(ENV_TRIG_PARAM, "Trigger the envelope (held = gate)");
        configParam(ENV_AMT_PARAM, -1.f, 1.f, 0.f, "Envelope to cutoffs", " oct", 0.f, 5.f);
        configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Dry/wet", "%", 0.f, 100.f);
        configParam(PH_RATE_PARAM, 0.f, 1.f, 0.35f, "Phaser rate", " Hz", 500.f, 0.02f);
        configParam(PH_DEPTH_PARAM, 0.f, 1.f, 0.6f, "Phaser depth", "%", 0.f, 100.f);
        configParam(PH_FDBK_PARAM, 0.f, 1.f, 0.3f, "Phaser feedback", "%", 0.f, 100.f);
        configParam(PH_MIX_PARAM, 0.f, 1.f, 0.f, "Phaser mix", "%", 0.f, 100.f);
        configParam(EQ_LO_PARAM, -1.f, 1.f, 0.f, "EQ low (120 Hz)", " dB", 0.f, 12.f);
        configParam(EQ_MID_PARAM, -1.f, 1.f, 0.f, "EQ mid (1 kHz)", " dB", 0.f, 12.f);
        configParam(EQ_HI_PARAM, -1.f, 1.f, 0.f, "EQ high (8 kHz)", " dB", 0.f, 12.f);
        configInput(IN_L_INPUT, "Left (or mono)");
        configInput(IN_R_INPUT, "Right (normalled to left)");
        configInput(VOCT_INPUT, "Cutoff V/oct (both filters)");
        configInput(CUT_A_INPUT, "Filter A cutoff CV (1 V/oct)");
        configInput(CUT_B_INPUT, "Filter B cutoff CV (1 V/oct)");
        configInput(SYNC_INPUT, "LFO sync clock");
        configInput(GATE_INPUT, "Envelope gate (ADSR)");
        configOutput(OUT_A_OUTPUT, "Filter A (mono)");
        configOutput(OUT_B_OUTPUT, "Filter B (mono)");
        configOutput(OUT_L_OUTPUT, "Left");
        configOutput(OUT_R_OUTPUT, "Right");
        configBypass(IN_L_INPUT, OUT_L_OUTPUT);
        configBypass(IN_R_INPUT, OUT_R_OUTPUT);
    }

    void onReset() override {
        for (int c = 0; c < 2; c++) fa[c] = DFFilter(), fb[c] = DFFilter();
        phaser = DFPhaser();
        for (int i = 0; i < 3; i++) eq[i] = DFBiquad(), eqSet[i] = 99.f;
        lfoPhase = lfoOut = rnd = phPhase = 0.f;
        follow = adsr = env = 0.f;
        attacking = gate = false;
    }

    // segment time (s) of an ADSR knob: 1 ms .. 5 s
    static float segTime(float k) { return 0.001f * std::pow(5000.f, k); }

    void process(const ProcessArgs& args) override {
        float inL = inputs[IN_L_INPUT].getVoltage();
        float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : inL;
        float dt = args.sampleTime;

        // envelope follower: 5 ms attack, 150 ms release, 0..1 for 0..5 V
        float lvl = std::min(1.f, 0.5f * (std::fabs(inL) + std::fabs(inR)) * 0.2f);
        follow += (lvl - follow) * (1.f - std::exp(-dt / (lvl > follow ? 0.005f : 0.15f)));

        // ADSR: gated by the GATE jack or a held TRIG; a new gate restarts the attack from where it is
        bool g = inputs[GATE_INPUT].getVoltage() >= 1.f || params[ENV_TRIG_PARAM].getValue() > 0.5f;
        if (g && !gate) attacking = true;
        gate = g;
        if (!gate) attacking = false;
        if (attacking) {
            // aims past 1 so the attack lands in its set time instead of creeping up on 1
            adsr += (1.2f - adsr) * (1.f - std::exp(-dt / (segTime(params[ENV_A_PARAM].getValue()) * 0.56f)));
            if (adsr >= 1.f) { adsr = 1.f; attacking = false; }
        } else {
            float target = gate ? params[ENV_S_PARAM].getValue() : 0.f;
            float t = segTime(params[gate ? ENV_D_PARAM : ENV_R_PARAM].getValue()) * 0.25f;
            adsr += (target - adsr) * (1.f - std::exp(-dt / t));
        }
        gateLit = gate ? 1.f : 0.f;
        env = params[ENV_SRC_PARAM].getValue() > 0.5f ? adsr : follow;

        // LFO: free (0.04..20 Hz) or locked to SYNC
        float rateK = params[LFO_RATE_PARAM].getValue(), lfoHz;
        bool synced = params[SYNC_PARAM].getValue() > 0.5f && inputs[SYNC_INPUT].isConnected();
        sinceClock += dt;
        bool tick = syncTrig.process(inputs[SYNC_INPUT].getVoltage(), 0.1f, 1.f);
        if (tick) { if (sinceClock > 0.002f && sinceClock < 8.f) clockPeriod = sinceClock; sinceClock = 0.f; clockCount++; }
        float prev = lfoPhase;
        if (synced) {
            float div = kDiv[clamp((int)(rateK * kDivs), 0, kDivs - 1)];
            lfoHz = 1.f / (clockPeriod * div);
            if (tick && div >= 1.f && clockCount % (int)div == 0) lfoPhase = 0.f;   // the cycle starts on the clock
        } else {
            lfoHz = 0.04f * std::pow(512.f, rateK);
        }
        lfoPhase += lfoHz * dt;
        lfoPhase -= std::floor(lfoPhase);
        if (lfoPhase < prev) rnd = random::uniform() * 2.f - 1.f;   // a new random step each cycle
        int shape = (int)std::round(params[LFO_SHAPE_PARAM].getValue());
        float raw;
        switch (shape) {
            case SAW: raw = 2.f * lfoPhase - 1.f; break;
            case SQR: raw = lfoPhase < 0.5f ? 1.f : -1.f; break;
            case RND: raw = rnd; break;
            default: { float p = lfoPhase + 0.25f; p -= std::floor(p); raw = 1.f - 4.f * std::fabs(p - 0.5f); }
        }
        // the jumps of SAW / SQR / RND are rounded off over about 1 ms so the cutoff doesn't click
        lfoOut += (raw - lfoOut) * (1.f - std::exp(-dt / 0.001f));
        int dest = (int)std::round(params[LFO_DEST_PARAM].getValue());
        float lfoA = dest == DEST_B ? 0.f : lfoOut, lfoB = dest == DEST_A ? 0.f : lfoOut;

        // cutoffs, in octaves above 20 Hz
        float common = inputs[VOCT_INPUT].getVoltage() + env * params[ENV_AMT_PARAM].getValue() * 5.f;
        float lfoCut = params[LFO_AMT_PARAM].getValue() * 4.f;
        float octA = params[A_CUT_PARAM].getValue() * 10.f + common + lfoA * lfoCut + inputs[CUT_A_INPUT].getVoltage();
        float octB = (params[LINK_PARAM].getValue() > 0.5f ? params[A_CUT_PARAM].getValue() * 10.f + params[OFFSET_PARAM].getValue()
                                                         : params[B_CUT_PARAM].getValue() * 10.f)
                   + common + lfoB * lfoCut + inputs[CUT_B_INPUT].getVoltage();
        float sr2 = args.sampleRate * 2.f;
        auto coef = [&](float oct, float& hz) {
            hz = clamp(20.f * std::pow(2.f, oct), 10.f, sr2 * 0.24f);
            return std::tan(float(M_PI) * hz / sr2);
        };
        float gA = coef(octA, cutAHz), gB = coef(octB, cutBHz);
        // RES 0 is Butterworth. RES sets the gain at the cutoff, the same at every slope: Q = 1/(sqrt2 (1 - 0.99 res))
        // (about 70 at full RES), reached by the last stage's Q over the product of the other stages' Qs
        float lfoRes = params[LFO_RES_PARAM].getValue() * 0.5f;
        float kA[4], kB[4];
        int nA = 0, nB = 0;
        auto damp = [&](int f, float lfo, float* k, int& n) {
            int sl = clamp((int)std::round(params[A_SLOPE_PARAM + f].getValue()), 0, 2);
            n = kStages[sl];
            float res = clamp(params[A_RES_PARAM + f * 4].getValue() + lfo * lfoRes, 0.f, 1.f);
            float q = 1.f / (float(M_SQRT2) * (1.f - 0.99f * res)), others = 1.f;
            for (int i = 0; i < n - 1; i++) { k[i] = 1.f / kBwQ[sl][i]; others *= kBwQ[sl][i]; }
            k[n - 1] = others / q;
        };
        damp(0, lfoA, kA, nA);
        damp(1, lfoB, kB, nB);
        float dA = params[A_DRIVE_PARAM].getValue(), dB = params[B_DRIVE_PARAM].getValue();
        float mA = params[A_MODE_PARAM].getValue(), mB = params[B_MODE_PARAM].getValue();

        int route = (int)std::round(params[ROUTE_PARAM].getValue());
        float in[2] = {inL, inR}, a[2], b[2], wet[2];
        for (int c = 0; c < 2; c++) {
            if (route == STEREO) {
                // A on the left, B on the right; the other filter of each pair stays quiet
                a[c] = c == 0 ? fa[0].process(in[0], gA, kA, nA, dA, mA) : 0.f;
                b[c] = c == 1 ? fb[1].process(in[1], gB, kB, nB, dB, mB) : 0.f;
                wet[c] = c == 0 ? a[0] : b[1];
            } else {
                a[c] = fa[c].process(in[c], gA, kA, nA, dA, mA);
                b[c] = fb[c].process(route == SERIES ? a[c] : in[c], gB, kB, nB, dB, mB);
                wet[c] = route == SERIES ? b[c] : 0.5f * (a[c] + b[c]);
            }
        }

        // phaser: its own sine LFO sweeps the all-passes exponentially; the right side runs a quarter cycle on
        float phMix = params[PH_MIX_PARAM].getValue();
        phPhase += 0.02f * std::pow(500.f, params[PH_RATE_PARAM].getValue()) * dt;
        phPhase -= std::floor(phPhase);
        float depth = params[PH_DEPTH_PARAM].getValue() * 5.f, fbk = params[PH_FDBK_PARAM].getValue() * 0.85f;

        // EQ coefficients follow the knobs (and the sample rate) only when they move
        if (eqSr != args.sampleRate) { eqSr = args.sampleRate; for (float& s : eqSet) s = 99.f; }
        static const float eqHz[3] = {120.f, 1000.f, 8000.f};
        bool eqOn = false;
        for (int i = 0; i < 3; i++) {
            float db = params[EQ_LO_PARAM + i].getValue() * 12.f;
            if (db != eqSet[i]) { eqSet[i] = db; eq[i].set(i, eqHz[i], db, args.sampleRate); }
            eqOn |= db != 0.f;
        }

        float mix = params[MIX_PARAM].getValue(), outv[2];
        for (int c = 0; c < 2; c++) {
            float y = in[c] + (wet[c] - in[c]) * mix;
            if (phMix > 0.f) {
                float p = phPhase + c * 0.25f;
                float hz = 150.f * std::pow(2.f, depth * (0.5f + 0.5f * std::sin(2.f * float(M_PI) * p)));
                float t = std::tan(float(M_PI) * std::min(hz, args.sampleRate * 0.45f) / args.sampleRate);
                y = phaser.process(c, y, (t - 1.f) / (t + 1.f), fbk, phMix);
            }
            if (eqOn) for (int i = 0; i < 3; i++) y = eq[i].process(c, y);
            outv[c] = y;
        }
        outputs[OUT_L_OUTPUT].setVoltage(clamp(outv[0], -12.f, 12.f));
        outputs[OUT_R_OUTPUT].setVoltage(clamp(outv[1], -12.f, 12.f));
        outputs[OUT_A_OUTPUT].setVoltage(clamp(route == STEREO ? a[0] : 0.5f * (a[0] + a[1]), -12.f, 12.f));
        outputs[OUT_B_OUTPUT].setVoltage(clamp(route == STEREO ? b[1] : 0.5f * (b[0] + b[1]), -12.f, 12.f));
    }
};
constexpr float SynTHBloKDualFilter::kDiv[];

// ---------------- widgets ----------------
using DF = SynTHBloKDualFilter;

// A filter's display: LP | BP | NT | HP (the nearest type to the MODE knob is lit; a sweep between two lights
// both, dimmer; a click sets the knob to that type), SLOPE 12 / 24 / 48 dB, then the cutoff in Hz.
struct DFFilterDisplay : TransparentWidget {
    DF* module = NULL;
    int filter = 0;
    // cell rows, mm from the top of the display
    static constexpr float kModeY = 1.5f, kSlopeY = 10.9f, kPad = 1.4f, kGap = 0.8f;
    float mode() { return module ? module->params[DF::A_MODE_PARAM + filter * 4].getValue() : (filter ? 0.f : 2.f); }
    int slope() { return module ? (int)std::round(module->params[DF::A_SLOPE_PARAM + filter].getValue()) : 1; }
    void cell(const DrawArgs& args, float x, float y, float w, const char* s, float lit, std::shared_ptr<window::Font>& font) {
        float ch = mm2px(oled::kCellH);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, x, y, w, ch, 1.2f);
        nvgFillColor(args.vg, lit > 0.f ? nvgLerpRGBA(oled::offFill(), oled::litFill(), lit) : oled::offFill());
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, oled::outline(lit > 0.5f ? 255 : 110));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        if (!font) return;
        nvgFontSize(args.vg, 6.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, lit > 0.5f ? oled::ink() : oled::amber());
        nvgText(args.vg, x + w / 2, y + ch / 2, s, NULL);
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (font) nvgFontFaceId(args.vg, font->handle);
        static const char* modes[4] = {"LP", "BP", "NT", "HP"};
        static const char* slopes[3] = {"12dB", "24dB", "48dB"};
        float pad = mm2px(kPad), gap = mm2px(kGap), inner = W - 2 * pad;
        float m = mode();
        float cw = (inner - 3 * gap) / 4.f;
        for (int i = 0; i < 4; i++)
            cell(args, pad + i * (cw + gap), mm2px(kModeY), cw, modes[i], 1.f - std::min(1.f, std::fabs(m - i)), font);
        int sl = slope();
        cw = (inner - 2 * gap) / 3.f;
        for (int i = 0; i < 3; i++) cell(args, pad + i * (cw + gap), mm2px(kSlopeY), cw, slopes[i], i == sl ? 1.f : 0.f, font);
        if (!font) return;
        nvgFontSize(args.vg, 5.f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, oled::dim());
        nvgText(args.vg, W / 2, mm2px((kModeY + oled::kCellH + kSlopeY) / 2.f), "SLOPE", NULL);
        float hz = module ? (filter ? module->cutBHz : module->cutAHz) : 1000.f;
        char buf[32];
        if (hz >= 1000.f) snprintf(buf, sizeof buf, "%.2f kHz", hz / 1000.f); else snprintf(buf, sizeof buf, "%.0f Hz", hz);
        nvgFontSize(args.vg, 9.f);
        nvgFillColor(args.vg, oled::amber());
        float top = mm2px(kSlopeY + oled::kCellH);
        nvgText(args.vg, W / 2, top + (H - top) / 2, buf, NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS || !module) return;
        float pad = mm2px(kPad), gap = mm2px(kGap), inner = box.size.x - 2 * pad, y = e.pos.y;
        if (y >= mm2px(kModeY) && y <= mm2px(kModeY + oled::kCellH)) {
            int i = clamp((int)((e.pos.x - pad) / ((inner + gap) / 4.f)), 0, 3);
            module->params[DF::A_MODE_PARAM + filter * 4].setValue((float)i);
            e.consume(this);
        } else if (y >= mm2px(kSlopeY) && y <= mm2px(kSlopeY + oled::kCellH)) {
            int i = clamp((int)((e.pos.x - pad) / ((inner + gap) / 3.f)), 0, 2);
            module->params[DF::A_SLOPE_PARAM + filter].setValue((float)i);
            e.consume(this);
        }
    }
};

// The envelope display: a bar of the envelope's level under its cells
struct DFEnvDisplay : oled::Box {
    DF* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        oled::Box::drawLayer(args, layer);
        if (layer != 1) return;
        float x = mm2px(1.4f), y = mm2px(19.0f), w = box.size.x - 2 * x, h = mm2px(2.2f);
        float v = module ? clamp(module->env, 0.f, 1.f) : 0.4f;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, x, y, w, h, 1.f);
        nvgFillColor(args.vg, oled::offFill());
        nvgFill(args.vg);
        if (v > 0.002f) {
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, x, y, w * v, h, 1.f);
            nvgFillColor(args.vg, oled::litFill());
            nvgFill(args.vg);
        }
    }
};

struct SynTHBloKDualFilterWidget : ModuleWidget {
    // one OLED cell (mm)
    oled::Cell* cell(DF* module, float x, float y, float w, int param, const char* label, int setTo) {
        oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(x, y)), module, param);
        c->box.size = mm2px(Vec(w, oled::kCellH));
        c->module = module; c->label = label; c->setTo = setTo;
        addParam(c);
        return c;
    }
    // a row of selector cells for one switch, set to first, first + 1, ...
    void row(DF* module, float x, float y, float w, int param, std::vector<const char*> labels, int first = 0) {
        float gap = 0.8f, cw = (w - gap * (labels.size() - 1)) / labels.size();
        for (size_t i = 0; i < labels.size(); i++) cell(module, x + i * (cw + gap), y, cw, param, labels[i], first + (int)i);
    }
    void display(float x, float y, float w, float h) {
        oled::Box* b = createWidget<oled::Box>(mm2px(Vec(x, y)));
        b->box.size = mm2px(Vec(w, h));
        addChild(b);
    }
    void small(DF* module, float x, float y, int param) {
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y)), module, param));
    }

    SynTHBloKDualFilterWidget(DF* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/SynTHBloKDualFilter.svg")));
        // screws in the corners, like the other SynTHBloK panels, so the header title sits right of the left one
        const float PW = 223.52f;
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PW - 3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 125.f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PW - 3.5f, 125.f))));

        // the two filter sections (mm, from the panel layout)
        for (int f = 0; f < 2; f++) {
            float x0 = f ? 112.76f : 2.f;
            int o = f * 4;
            addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(x0 + 18.f, 35.f)), module, DF::A_CUT_PARAM + o));
            addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x0 + 42.f, 28.f)), module, DF::A_RES_PARAM + o));
            addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x0 + 59.f, 28.f)), module, DF::A_DRIVE_PARAM + o));
            addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x0 + 50.5f, 47.f)), module, DF::A_MODE_PARAM + o));
            DFFilterDisplay* d = createWidget<DFFilterDisplay>(mm2px(Vec(x0 + 70.f, 19.5f)));
            d->box.size = mm2px(Vec(34.f, 27.f));
            d->module = module; d->filter = f;
            addChild(d);
        }

        // LFO
        display(5.f, 67.6f, 28.f, 24.f);
        row(module, 6.4f, 69.0f, 25.2f, DF::LFO_SHAPE_PARAM, {"TRI", "SAW"});
        row(module, 6.4f, 74.3f, 25.2f, DF::LFO_SHAPE_PARAM, {"SQR", "RND"}, 2);
        row(module, 6.4f, 79.6f, 25.2f, DF::LFO_DEST_PARAM, {"A+B", "A", "B"});
        oled::Cell* sync = cell(module, 6.4f, 84.9f, 25.2f, DF::SYNC_PARAM, "SYNC", -1);
        sync->latch = true; sync->jack = DF::SYNC_INPUT;
        small(module, 43.f, 73.5f, DF::LFO_RATE_PARAM);
        small(module, 56.5f, 73.5f, DF::LFO_AMT_PARAM);
        small(module, 43.f, 88.5f, DF::LFO_RES_PARAM);

        // ENVELOPE
        DFEnvDisplay* ed = createWidget<DFEnvDisplay>(mm2px(Vec(69.f, 67.6f)));
        ed->box.size = mm2px(Vec(22.f, 24.f));
        ed->module = module;
        addChild(ed);
        cell(module, 70.4f, 69.0f, 19.2f, DF::ENV_SRC_PARAM, "FOLLOW", 0);
        cell(module, 70.4f, 74.3f, 19.2f, DF::ENV_SRC_PARAM, "ADSR", 1);
        oled::Cell* trig = cell(module, 70.4f, 79.6f, 19.2f, DF::ENV_TRIG_PARAM, "TRIG", -1);
        trig->jack = DF::GATE_INPUT;
        if (module) trig->flash = &module->gateLit;
        for (int i = 0; i < 4; i++) small(module, 98.f + i * 13.f, 73.5f, DF::ENV_A_PARAM + i);
        small(module, 98.f, 88.5f, DF::ENV_AMT_PARAM);

        // PHASER and EQ
        small(module, 157.f, 73.5f, DF::PH_RATE_PARAM);
        small(module, 177.f, 73.5f, DF::PH_DEPTH_PARAM);
        small(module, 157.f, 88.5f, DF::PH_FDBK_PARAM);
        small(module, 177.f, 88.5f, DF::PH_MIX_PARAM);
        small(module, 199.5f, 73.5f, DF::EQ_LO_PARAM);
        small(module, 213.5f, 73.5f, DF::EQ_MID_PARAM);
        small(module, 206.5f, 88.5f, DF::EQ_HI_PARAM);

        // ROUTING, LINK + OFFSET, MIX
        display(101.f, 104.f, 30.f, 17.4f);
        cell(module, 102.4f, 105.4f, 27.2f, DF::ROUTE_PARAM, "SERIES", 0);
        cell(module, 102.4f, 110.7f, 27.2f, DF::ROUTE_PARAM, "PARALLEL", 1);
        cell(module, 102.4f, 116.0f, 27.2f, DF::ROUTE_PARAM, "STEREO", 2);
        display(135.f, 102.6f, 14.f, 7.2f);
        cell(module, 136.2f, 104.0f, 11.6f, DF::LINK_PARAM, "LINK", -1)->latch = true;
        small(module, 142.f, 116.5f, DF::OFFSET_PARAM);
        small(module, 155.5f, 110.f, DF::MIX_PARAM);

        // jacks
        const int ins[7] = {DF::IN_L_INPUT, DF::IN_R_INPUT, DF::VOCT_INPUT, DF::CUT_A_INPUT, DF::CUT_B_INPUT, DF::SYNC_INPUT, DF::GATE_INPUT};
        for (int i = 0; i < 7; i++) addInput(createInputCentered<BlackJack>(mm2px(Vec(10.f + i * 12.8f, 113.f)), module, ins[i]));
        const int outs[4] = {DF::OUT_A_OUTPUT, DF::OUT_B_OUTPUT, DF::OUT_L_OUTPUT, DF::OUT_R_OUTPUT};
        for (int i = 0; i < 4; i++) addOutput(createOutputCentered<BlackJack>(mm2px(Vec(174.f + i * 13.2f, 113.f)), module, outs[i]));
    }
};

Model* modelSynTHBloKDualFilter = createModel<SynTHBloKDualFilter, SynTHBloKDualFilterWidget>("SynTHBloKDualFilter");
