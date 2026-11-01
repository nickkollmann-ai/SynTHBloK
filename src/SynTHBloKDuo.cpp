// ============================================================================
// SynTHBloK MATRIX-DUO  (LEAD + BASS: two dual-oscillator mono synths with their own note sequencer)
//  Pulled out of MATRIX FORGE. Each synth: wave morph, OSC 2 tune, hard sync, FM, sub, 24 dB filter,
//  ADSR, drive, glide. The grid is 8 steps x 8 notes of the KEY + SCALE (bottom row = root, top = its
//  octave) for the synth LEAD | BASS picks: a pad adds that note to the step, again removes it;
//  several notes on a step play as a chord (up to 4 voices per synth).
//  8 patterns, 1..8 steps. CLOCK = 16th notes; unpatched, the internal clock runs at the BPM knob.
//  OUT L / R = both synths through FORGE FX (GRAIN-FORGE's whole FX chain, condensed into one OLED and
//  small knobs on the right) and MASTER.
//  JAM (MIDI IN / OUT, "JAM Template 4 Rack", ch 1): the pads are the grid, scenes = patterns,
//  touch strips move the knobs of strip bank A..E (group buttons or LEFT / RIGHT; A / B = LEAD, C / D = BASS,
//  E = mix), GRID = LEAD / BASS, PLAY, TEMPO = reset, CLEAR (held 1 s = every pattern), DUPLICATE,
//  UP / DOWN = length, PREV / NEXT = pattern, MUTE / SOLO = the shown synth,
//  knob MST / CUE = MASTER, GRP = LEAD level, IN1 = BASS level (push = default). Lights follow.
// ============================================================================
#include "Matrix8_ui.hpp"
#include "DualOsc_dsp.hpp"
#include "KnobTicks.hpp"
#include "OledUi.hpp"
// FORGE FX runs GRAIN-FORGE's module as an embedded engine; its own panel builder isn't used here.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "SynTHBloKGrainForge.hpp"
#pragma GCC diagnostic pop
#include <cstdio>
#include <string>

namespace {

static const char* kKeyName[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char* kBankLabel[8] = {"A: LEAD osc", "B: LEAD filter + env", "C: BASS osc", "D: BASS filter + env",
                                    "E: mix", "F: CV", "G: CV", "H: CV"};
static const char* kDuBankName[8] ={"LEAD OSC", "LEAD FILT", "BASS OSC", "BASS FILT", "MIX", "CV F", "CV G", "CV H"};
static const int kScaleCount = 8;
static const char* kScaleName[kScaleCount] = {"MINOR", "MAJOR", "DORIAN", "PHRYGIAN", "MIXOLYD", "HARM MIN", "MIN PENT", "MAJ PENT"};
static const int kScaleLen[kScaleCount] = {7, 7, 7, 7, 7, 7, 5, 5};
static const int kScale[kScaleCount][7] = {
    {0, 2, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
    {0, 2, 4, 5, 7, 9, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 3, 5, 7, 10}, {0, 2, 4, 7, 9}};

// OSC 2 tune: the knob is squared, so the middle is fine detune and the ends are +-12 semitones.
struct TuneQuantity : ParamQuantity {
    float getDisplayValue() override { float v = getValue(); return 12.f * v * std::fabs(v); }
    void setDisplayValue(float d) override { float a = std::sqrt(std::min(std::fabs(d), 12.f) / 12.f); setValue(d < 0.f ? -a : a); }
};

// Master filter, GRAIN-FORGE's DJ filter in mono: CUT 0..1 sweeps 20 Hz .. 20 kHz, MODE 0..2 = LP / BP / HP,
// STAGES 0..2 = 1..3 SVFs in series (12 / 24 / 36 dB), the first one resonant (Q 0.8 .. 14, level drops
// as it rises). LP fully open / HP fully down fade to true bypass. Every mode and stage always runs and
// MODE / STAGES arrive smoothed, so switching crossfades.
struct DUFilter {
    float ic1[3][3] = {}, ic2[3][3] = {};   // [mode][stage]
    void process(float& x, float cut, float res, float sr, float stages, float mode) {
        cut = clamp(cut, 0.f, 1.f);
        float fc = 20.f * std::pow(1000.f, cut);
        float g = std::tan(float(M_PI) * clamp(fc, 20.f, sr * 0.45f) / sr);
        float kr = 1.2f * std::pow(0.07f / 1.2f, clamp(res, 0.f, 1.f));
        float comp = std::pow(kr / 1.2f, 0.4f);
        float wetM[3] = {clamp((0.99f - cut) / 0.06f, 0.f, 1.f), 1.f, clamp((cut - 0.01f) / 0.06f, 0.f, 1.f)};
        stages = clamp(stages, 0.f, 2.f); mode = clamp(mode, 0.f, 2.f);
        int s0 = std::min((int)stages, 1), m0 = std::min((int)mode, 1);
        float sf = stages - s0, mf = mode - m0, out[3];
        for (int m = 0; m < 3; m++) {
            float y = x, tap[3];
            for (int s = 0; s < 3; s++) {
                float k = s == 0 ? kr : 1.414f;
                float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
                float v3 = y - ic2[m][s];
                float v1 = a1 * ic1[m][s] + a2 * v3;
                float v2 = ic2[m][s] + a2 * ic1[m][s] + a3 * v3;
                ic1[m][s] = 2.f * v1 - ic1[m][s]; ic2[m][s] = 2.f * v2 - ic2[m][s];
                y = m == 0 ? v2 : m == 1 ? k * v1 : y - k * v1 - v2;
                tap[s] = y;
            }
            float yf = (tap[s0] + (tap[s0 + 1] - tap[s0]) * sf) * comp;
            float ay = std::fabs(yf);   // resonant peaks above 1 round off toward 1.75
            if (ay > 1.f) { float o = ay - 1.f; yf = std::copysign(1.f + 0.75f * o / (0.75f + o), yf); }
            out[m] = x + (yf - x) * wetM[m];
        }
        x = out[m0] + (out[m0 + 1] - out[m0]) * mf;
    }
};

// Filter ADSR. Times in seconds; exponential segments, so no clicks.
struct DUAdsr {
    enum { IDLE, ATK, DEC, REL } st = IDLE;
    float v = 0.f;
    void trigger() { st = ATK; }
    void release() { if (st != IDLE) st = REL; }
    float process(float atk, float dec, float sus, float rel, float sr) {
        switch (st) {
            case ATK:
                v += (1.3f - v) * (1.f - std::exp(-1.f / (atk * sr)));
                if (v >= 1.f) { v = 1.f; st = DEC; }
                break;
            case DEC: v += (sus - v) * (1.f - std::exp(-1.f / (dec * sr))); break;
            case REL:
                v += (0.f - v) * (1.f - std::exp(-1.f / (rel * sr)));
                if (v < 1e-4f) { v = 0.f; st = IDLE; }
                break;
            default: break;
        }
        return v;
    }
};

struct SynTHBloKDuo : Module {
    enum ParamIds { PAT_PARAM, LEN_PARAM, BPM_PARAM, VIEW_PARAM, KEY_PARAM, SCALE_PARAM, RUN_PARAM, RESET_PARAM, CLEAR_PARAM,
                    ENUMS(LEAD_PARAM, DO_NUM), ENUMS(BASS_PARAM, DO_NUM), MASTER_PARAM, PAGE_PARAM, LEARN_PARAM,
                    BANK_PARAM, OFFC_PARAM, ONC_PARAM, HEADC_PARAM, HITC_PARAM, BONC_PARAM, BOFFC_PARAM, RANGE_PARAM, MIX_PARAM,
                    // master filter (as on GRAIN-FORGE), FLT_CUT .. FLT_DRYWET smoothed together
                    FLT_CUT_PARAM, FLT_RES_PARAM, FLT_ENV_PARAM, FLT_STAGES_PARAM, FLT_MODE_PARAM, FLT_ATK_PARAM, FLT_DEC_PARAM,
                    FLT_SUS_PARAM, FLT_REL_PARAM, FLT_AMP_PARAM, FLT_BYPASS_PARAM, FLT_IN_PARAM, FLT_DRYWET_PARAM, FLT_TRIG_PARAM,
                    FX_ON_PARAM,
                    NUM_PARAMS };
    enum InputIds { CLOCK_INPUT, RESET_INPUT, RUN_INPUT, NUM_INPUTS };
    enum OutputIds { OUT_L_OUTPUT, OUT_R_OUTPUT, LEAD_OUTPUT, BASS_OUTPUT, LEAD_CV_OUTPUT, LEAD_GATE_OUTPUT, BASS_CV_OUTPUT, BASS_GATE_OUTPUT,
                     ENUMS(STRIP_OUTPUT, 8), KNOB_OUTPUT, ENUMS(TRIG_OUTPUT, 8), NUM_OUTPUTS };
    enum LightIds { ENUMS(ADSR_LIGHT, 4), NUM_LIGHTS };
    static const int kFltSm = FLT_DRYWET_PARAM - FLT_CUT_PARAM + 1;

    uint8_t note[8][2][8];        // [pattern][LEAD, BASS][step]: bit d = scale degree d (a chord), 0 = rest
    bool mute[2] = {};
    static const int kVoices = 4; // a step plays its lowest 4 notes
    DualOscVoice eng[2][kVoices];
    int curNote[2] = {-1, -1};    // MIDI note each synth last played, the chord's lowest (CV out, display)
    bool tied[2] = {};
    float chordGain[2] = {1.f, 1.f}, chordGainSm[2] = {1.f, 1.f};

    int step = -1;
    bool running = true, pendingReset = false;
    float sinceClock = 10.f, clockPeriod = 0.f, internalTimer = 0.f, resetFlash = 0.f;
    float masterVol = 0.8f, mixSm = 0.5f;
    dsp::SchmittTrigger clkTrig, rstTrig, runTrig, rstBtnTrig, clrTrig;

    // Maschine JAM ("JAM Template 4 Rack", MIDI channel 1): its 8x8 pads are the note grid.
    midi::InputQueue midiIn;
    midi::Output midiOut;
    bool jamReset = false, solo[2] = {};
    bool clearHeld = false, clearAllDone = false;
    float clearHeldT = 0.f;
    int knobCC = kKnobFirstCC;    // knob page the JAM last sent from, for the value sent back
    int lastLed[kNumLed];
    bool matchColors = true;      // JAM lights use the panel colours; off = the LED colour sliders
    int ledScan = 0, lastPage = -1, lastBank = -1, lastView = -1;
    int stripPage = 0, lastStripPage = -1;   // JAM touch strip page the strips last sent from
    float tokens = 4.f, resync = 0.f;
    float stripTgt[NUM_PARAMS];   // where a touch strip is gliding a knob to (-1 = idle), so it never zips
    float cvVal[3][8] = {};       // banks F..H: free strips, only for the STRIP CV jacks
    float stripSm[8] = {}, knobSm = 0.f;
    dsp::PulseGenerator trigPulse[8];   // row jacks: a note in that row played (LEAD or BASS)
    DUFilter flt;
    DUAdsr fltEnv;
    float fltSm[kFltSm] = {}, fltGate = 0.f, envS1 = 0.f, envS2 = 0.f, stageLvl[4] = {};
    bool fltSmInit = false;
    // FORGE FX: a whole GRAIN-FORGE, outside the Rack engine. Its knobs bind to it directly; its params and
    // data are saved in this module's data ("fx"). It runs on this module's clock, after the master filter.
    SynTHBloKGrainForge fx;
    float fxOnSm = 1.f;

    SynTHBloKDuo() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(PAT_PARAM, 1.f, 8.f, 1.f, "Pattern");
        configParam(LEN_PARAM, 1.f, 8.f, 8.f, "Pattern length (steps)");
        configParam(BPM_PARAM, 40.f, 240.f, 120.f, "Internal tempo (when CLOCK is unpatched)", " BPM");
        paramQuantities[PAT_PARAM]->snapEnabled = true;
        paramQuantities[LEN_PARAM]->snapEnabled = true;
        configSwitch(VIEW_PARAM, 0.f, 1.f, 0.f, "Grid shows", {"LEAD", "BASS"});
        configSwitch(KEY_PARAM, 0.f, 11.f, 0.f, "Key", std::vector<std::string>(kKeyName, kKeyName + 12));
        configSwitch(SCALE_PARAM, 0.f, kScaleCount - 1, 0.f, "Scale", {"Minor", "Major", "Dorian", "Phrygian", "Mixolydian", "Harmonic minor", "Minor pentatonic", "Major pentatonic"});
        configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run (a trigger at the RUN jack toggles it)", {"Stopped", "Running"});
        configButton(RESET_PARAM, "Reset (a trigger at the RESET jack also resets)");
        configButton(CLEAR_PARAM, "Clear the shown synth's notes in this pattern");
        configParam(MASTER_PARAM, 0.f, 1.f, 0.8f, "Master volume (OUT L / R)", "%", 0.f, 100.f);
        configParam(PAGE_PARAM, 1.f, 6.f, 1.f, "JAM matrix page (must match the JAM)");
        paramQuantities[PAGE_PARAM]->snapEnabled = true;
        configSwitch(LEARN_PARAM, 0.f, 1.f, 0.f, "Learn JAM page: then press the TOP-LEFT pad on the JAM", {"Off", "Armed"});
        configSwitch(BANK_PARAM, 1.f, 8.f, 1.f, "Strip bank", std::vector<std::string>(kBankLabel, kBankLabel + 8));
        configSwitch(RANGE_PARAM, 0.f, 1.f, 0.f, "STRIP CV range", {"0..10 V", "-5..+5 V"});
        configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Mix LEAD <-> BASS (middle = both full)", "%", 0.f, 100.f);
        configParam(FLT_CUT_PARAM, 0.f, 1.f, 1.f, "Filter cutoff (LP fully open / HP fully down = off)", " Hz", 1000.f, 20.f);
        configParam(FLT_RES_PARAM, 0.f, 1.f, 0.f, "Filter resonance", "%", 0.f, 100.f);
        configParam(FLT_ENV_PARAM, -1.f, 1.f, 0.f, "Filter env depth (+ sweeps cutoff up, - down)", "%", 0.f, 100.f);
        configSwitch(FLT_STAGES_PARAM, 0.f, 2.f, 0.f, "Filter stages", {"1 stage (12 dB)", "2 stages (24 dB)", "3 stages (36 dB)"});
        configSwitch(FLT_MODE_PARAM, 0.f, 2.f, 0.f, "Filter type", {"Low-pass", "Band-pass", "High-pass"});
        configParam(FLT_ATK_PARAM, 0.f, 1.f, 0.1f, "Filter env attack", " ms", 667.f, 3.f);
        configParam(FLT_DEC_PARAM, 0.f, 1.f, 0.45f, "Filter env decay", " ms", 333.f, 12.f);
        configParam(FLT_SUS_PARAM, 0.f, 1.f, 0.3f, "Filter env sustain", "%", 0.f, 100.f);
        configParam(FLT_REL_PARAM, 0.f, 1.f, 0.4f, "Filter env release", " ms", 333.f, 12.f);
        configParam(FLT_AMP_PARAM, 0.f, 1.f, 0.f, "Filter env to level (pluck / gate)", "%", 0.f, 100.f);
        configSwitch(FLT_BYPASS_PARAM, 0.f, 1.f, 0.f, "Filter env bypass (the ADSR off the filter)", {"Env on", "Env bypassed"});
        configParam(FLT_IN_PARAM, 0.f, 2.f, 1.f, "Filter input gain", "%", 0.f, 100.f);
        configParam(FLT_DRYWET_PARAM, 0.f, 1.f, 1.f, "Filter dry / wet", "%", 0.f, 100.f);
        configSwitch(FLT_TRIG_PARAM, 0.f, 4.f, 0.f, "Filter env trigger", {"Any note", "LEAD notes", "BASS notes", "Every beat", "Every step"});
        configSwitch(FX_ON_PARAM, 0.f, 1.f, 1.f, "FORGE FX (off = OUT L / R dry)", {"Off", "On"});
        // FX knobs set the engine's values straight away (it smooths them itself); Rack's drag smoothing
        // would point the engine at a module it doesn't know
        for (ParamQuantity* q : fx.paramQuantities) q->smoothEnabled = false;
        configParam(OFFC_PARAM, 0.f, 127.f, 0.f, "LED colour: empty step");
        configParam(ONC_PARAM, 0.f, 127.f, 127.f, "LED colour: active step");
        configParam(HEADC_PARAM, 0.f, 127.f, 30.f, "LED colour: playhead over empty step");
        configParam(HITC_PARAM, 0.f, 127.f, 90.f, "LED colour: playhead over active step");
        configParam(BONC_PARAM, 0.f, 127.f, 127.f, "LED colour: selected bank button");
        configParam(BOFFC_PARAM, 0.f, 127.f, 20.f, "LED colour: other bank buttons");
        for (int id = OFFC_PARAM; id <= BOFFC_PARAM; id++) paramQuantities[id]->snapEnabled = true;
        for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
        for (int i = 0; i < NUM_PARAMS; i++) stripTgt[i] = -1.f;
        //                        WAVE1 WAVE2 TUNE   FM   MIX  SUB  DRIVE LEVEL CUT  RES  ENV  GLIDE ATK DEC  SUS  REL  GATE OCT SYNC
        static const float def[2][DO_NUM] = {
            {0.f, 0.f, 0.08f, 0.f, 0.5f, 0.f, 0.2f, 0.7f, 0.6f, 0.3f, 0.45f, 0.1f, 0.f, 0.45f, 0.6f, 0.4f, 0.6f, 0.f, 0.f},
            {0.5f, 0.f, 0.f, 0.f, 0.3f, 0.6f, 0.3f, 0.8f, 0.35f, 0.35f, 0.55f, 0.f, 0.f, 0.35f, 0.3f, 0.25f, 0.5f, 0.f, 0.f}};
        for (int e = 0; e < 2; e++) {
            int b = e ? BASS_PARAM : LEAD_PARAM;
            std::string n = e ? "Bass " : "Lead ";
            const float* d = def[e];
            configParam(b + DO_WAVE1, 0.f, 1.f, d[DO_WAVE1], n + "OSC 1 wave (saw -> square -> sine)", "%", 0.f, 100.f);
            configParam(b + DO_WAVE2, 0.f, 1.f, d[DO_WAVE2], n + "OSC 2 wave (saw -> square -> sine)", "%", 0.f, 100.f);
            configParam<TuneQuantity>(b + DO_TUNE, -1.f, 1.f, d[DO_TUNE], n + "OSC 2 tune", " st");
            configParam(b + DO_FM, 0.f, 1.f, d[DO_FM], n + "FM (OSC 1 -> OSC 2)", "%", 0.f, 100.f);
            configParam(b + DO_MIX, 0.f, 1.f, d[DO_MIX], n + "OSC 1 / OSC 2 mix", "%", 0.f, 100.f);
            configParam(b + DO_SUB, 0.f, 1.f, d[DO_SUB], n + "sub (square, an octave down)", "%", 0.f, 100.f);
            configParam(b + DO_DRIVE, 0.f, 1.f, d[DO_DRIVE], n + "drive", "%", 0.f, 100.f);
            configParam(b + DO_LEVEL, 0.f, 1.f, d[DO_LEVEL], n + "level", "%", 0.f, 100.f);
            configParam(b + DO_CUTOFF, 0.f, 1.f, d[DO_CUTOFF], n + "filter cutoff", " Hz", 600.f, 30.f);
            configParam(b + DO_RES, 0.f, 1.f, d[DO_RES], n + "filter resonance", "%", 0.f, 100.f);
            configParam(b + DO_ENV, 0.f, 1.f, d[DO_ENV], n + "filter envelope amount", "%", 0.f, 100.f);
            configParam(b + DO_GLIDE, 0.f, 1.f, d[DO_GLIDE], n + "glide", "%", 0.f, 100.f);
            configParam(b + DO_ATK, 0.f, 1.f, d[DO_ATK], n + "attack", " ms", 2000.f, 1.f);
            configParam(b + DO_DEC, 0.f, 1.f, d[DO_DEC], n + "decay", " ms", 400.f, 5.f);
            configParam(b + DO_SUS, 0.f, 1.f, d[DO_SUS], n + "sustain", "%", 0.f, 100.f);
            configParam(b + DO_REL, 0.f, 1.f, d[DO_REL], n + "release", " ms", 800.f, 5.f);
            configParam(b + DO_GATE, 0.05f, 1.f, d[DO_GATE], n + "gate length (full = tie into the next note, with glide)", "%", 0.f, 100.f);
            configSwitch(b + DO_OCT, -2.f, 2.f, d[DO_OCT], n + "octave", {"-2", "-1", "0", "+1", "+2"});
            configSwitch(b + DO_SYNC, 0.f, 1.f, d[DO_SYNC], n + "hard sync (OSC 2 to OSC 1)", {"Off", "On"});
        }
        configInput(CLOCK_INPUT, "Clock (16th notes; the BPM knob runs it when unpatched)");
        configInput(RESET_INPUT, "Reset (trigger: same as pressing RESET)");
        configInput(RUN_INPUT, "Run (trigger: toggles RUN, like pressing it)");
        configOutput(OUT_L_OUTPUT, "Left: LEAD + BASS through MASTER");
        configOutput(OUT_R_OUTPUT, "Right: LEAD + BASS through MASTER");
        configOutput(LEAD_OUTPUT, "Lead audio");
        configOutput(BASS_OUTPUT, "Bass audio");
        configOutput(LEAD_CV_OUTPUT, "Lead pitch (1 V/oct, 0 V = C4)");
        configOutput(LEAD_GATE_OUTPUT, "Lead gate");
        configOutput(BASS_CV_OUTPUT, "Bass pitch (1 V/oct, 0 V = C4)");
        configOutput(BASS_GATE_OUTPUT, "Bass gate");
        for (int i = 0; i < 8; i++) configOutput(STRIP_OUTPUT + i, "Strip " + std::to_string(i + 1) + " CV (the shown bank)");
        configOutput(KNOB_OUTPUT, "Knob CV (0..10 V: the JAM knob's page)");
        for (int r = 0; r < 8; r++) configOutput(TRIG_OUTPUT + r, "Row " + std::to_string(r + 1) + " trigger (LEAD or BASS plays this row's note)");
        clearAll();
        // Pattern 1 starts with a LEAD riff and a BASS line, so the synths sound before you program them.
        static const int8_t defNote[2][8] = {{4, -1, 2, 4, -1, 7, 5, 2}, {0, -1, -1, 0, -1, -1, 3, 4}};
        for (int e = 0; e < 2; e++) for (int c = 0; c < 8; c++) note[0][e][c] = defNote[e][c] < 0 ? 0 : 1 << defNote[e][c];
    }

    void clearAll() { for (int p = 0; p < 8; p++) for (int e = 0; e < 2; e++) for (int c = 0; c < 8; c++) note[p][e][c] = 0; }
    bool gateOn(int e) { for (auto& v : eng[e]) if (v.gateOn()) return true; return false; }
    int curPat() { return clamp((int)params[PAT_PARAM].getValue(), 1, 8) - 1; }
    int view() { return clamp((int)params[VIEW_PARAM].getValue(), 0, 1); }
    int engParam(int e, int p) { return (e ? BASS_PARAM : LEAD_PARAM) + p; }
    // Pad (r, c): the note 7 - r at step c of the shown synth; several per step make a chord.
    bool padOn(int r, int c) { return note[curPat()][view()][c] >> (7 - r) & 1; }
    void togglePad(int r, int c) { note[curPat()][view()][c] ^= 1 << (7 - r); }

    // MIDI note of scale degree deg (0..7) for LEAD (from C4) or BASS (from C2).
    int midiNote(int e, int deg) {
        int s = clamp((int)params[SCALE_PARAM].getValue(), 0, kScaleCount - 1), n = kScaleLen[s];
        int key = clamp((int)params[KEY_PARAM].getValue(), 0, 11);
        int oct = clamp((int)std::round(params[engParam(e, DO_OCT)].getValue()), -2, 2);
        return (e ? 36 : 60) + key + 12 * oct + 12 * (deg / n) + kScale[s][deg % n];
    }

    // LEAD (e = 0) / BASS (1) on a new step; true when it played a note.
    // A chord's notes go to voices 0, 1, .. from the lowest, so a single note always lands on voice 0 (glide, tie).
    bool playEngine(int e, float stepSec) {
        int mask = note[curPat()][e][step];
        if (!mask || !trackOn(e)) { tied[e] = false; return false; }
        float gate = params[engParam(e, DO_GATE)].getValue();
        bool tie = gate >= 0.99f;
        int k = 0;
        for (int deg = 0; deg < 8 && k < kVoices; deg++) {
            if (!(mask >> deg & 1)) continue;
            trigPulse[7 - deg].trigger(0.01f);
            int n = midiNote(e, deg);
            if (k == 0) curNote[e] = n;
            eng[e][k].noteOn(440.f * std::pow(2.f, (n - 69) / 12.f), tie ? stepSec * 1.1f : std::max(0.01f, gate * stepSec), tie && tied[e]);
            k++;
        }
        chordGain[e] = 1.f / std::sqrt((float)k);
        tied[e] = tie;
        return true;
    }

    void onReset() override {
        Module::onReset();
        step = -1;
        for (int e = 0; e < 2; e++) mute[e] = false;
        for (int i = 0; i < SynTHBloKGrainForge::NUM_PARAMS; i++) fx.params[i].setValue(fx.paramQuantities[i]->getDefaultValue());
        fx.onReset();
    }

    // ---- JAM ----
    bool trackOn(int e) { return !mute[e] && (!(solo[0] || solo[1]) || solo[e]); }
    int curPage() { return clamp((int)params[PAGE_PARAM].getValue(), 1, 6); }
    int bank() { return clamp((int)params[BANK_PARAM].getValue(), 1, 8) - 1; }
    // Touch strip bank b, strip i -> the param it moves. A..D = one synth each (and show its grid), E = mix,
    // F..H = none (-1): free strips for the STRIP CV jacks.
    int stripParam(int b, int i) {
        if (b > 4) return -1;
        static const int OSC[8] = {DO_WAVE1, DO_WAVE2, DO_TUNE, DO_FM, DO_SUB, DO_DRIVE, DO_GATE, DO_LEVEL};
        static const int FLT[8] = {DO_CUTOFF, DO_RES, DO_ENV, DO_GLIDE, DO_ATK, DO_DEC, DO_SUS, DO_REL};
        static const int MIX[8] = {DO_LEVEL, DO_LEVEL, DO_CUTOFF, DO_CUTOFF, DO_RES, DO_RES, DO_DRIVE, DO_DRIVE};
        if (b < 4) return engParam(b / 2, (b % 2 ? FLT : OSC)[i]);
        return engParam(i % 2, MIX[i]);
    }
    // Strip name for the panel: the param's short name, or "CV n".
    std::string stripName(int i) {
        static const char* N[DO_NUM] = {"WAVE1", "WAVE2", "TUNE", "FM", "MIX", "SUB", "DRIVE", "LEVEL", "CUT", "RES",
                                        "ENV", "GLIDE", "ATK", "DEC", "SUS", "REL", "GATE", "OCT", "SYNC"};
        int id = stripParam(bank(), i);
        if (id < 0) return "CV" + std::to_string(i + 1);
        int e = id >= BASS_PARAM ? 1 : 0, p = id - engParam(e, 0);
        if (bank() == 4) return std::string(e ? "B " : "L ") + N[p];
        return N[p];
    }
    float stripGet(int i) {
        int id = stripParam(bank(), i);
        if (id < 0) return cvVal[bank() - 5][i];
        ParamQuantity* q = paramQuantities[id];
        if (stripTgt[id] > -0.5f) return (stripTgt[id] - q->getMinValue()) / (q->getMaxValue() - q->getMinValue());
        return q->getScaledValue();
    }
    void stripSet(int i, float x) {
        int id = stripParam(bank(), i);
        if (id < 0) { cvVal[bank() - 5][i] = clamp(x, 0.f, 1.f); return; }
        ParamQuantity* q = paramQuantities[id];
        stripTgt[id] = q->getMinValue() + clamp(x, 0.f, 1.f) * (q->getMaxValue() - q->getMinValue());
    }
    void setBank(int b) {
        b = clamp(b, 0, 7);
        params[BANK_PARAM].setValue((float)(b + 1));
        if (b < 4) params[VIEW_PARAM].setValue((float)(b / 2));
    }
    // JAM knob pages: MST / CUE = MASTER, GRP = LEAD level, IN1 = BASS level.
    int knobParam() {
        switch (knobCC) {
            case kKnobFirstCC + 1: return engParam(0, DO_LEVEL);
            case kKnobFirstCC + 2: return engParam(1, DO_LEVEL);
            default: return MASTER_PARAM;
        }
    }
    int padNote(int r, int c) { return std::min(127, 12 * (curPage() - 1) + 56 + c - 8 * r); }
    void stepParam(int id, int delta, int lo, int hi, bool wrap) {
        int v = (int)params[id].getValue() + delta;
        if (wrap) v = lo + ((v - lo) % (hi - lo + 1) + (hi - lo + 1)) % (hi - lo + 1);
        params[id].setValue((float)clamp(v, lo, hi));
    }
    void copyToNext() {
        int from = curPat(), to = (from + 1) % 8;
        for (int e = 0; e < 2; e++) for (int c = 0; c < 8; c++) note[to][e][c] = note[from][e][c];
        params[PAT_PARAM].setValue((float)(to + 1));
    }
    // A JAM side/bottom button was pressed (b > 0) or released (b == 0).
    void jamButton(int cc, int b) {
        if (cc == CC_CLEAR && b <= 0) { clearHeld = false; return; }
        if (b <= 0) return;
        switch (cc) {
            // CLEAR: the shown synth's notes in this pattern; held for a second, every pattern.
            case CC_CLEAR:
                for (int c = 0; c < 8; c++) note[curPat()][view()][c] = 0;
                clearHeld = true; clearHeldT = 0.f; clearAllDone = false;
                break;
            case CC_DUPLICATE: copyToNext(); break;
            case CC_UP: stepParam(LEN_PARAM, 1, 1, 8, false); break;
            case CC_DOWN: stepParam(LEN_PARAM, -1, 1, 8, false); break;
            case CC_LEFT: setBank(bank() - 1); break;
            case CC_RIGHT: setBank(bank() + 1); break;
            case CC_GRID: params[RANGE_PARAM].setValue(params[RANGE_PARAM].getValue() > 0.5f ? 0.f : 1.f); break;
            case CC_SOLO: solo[view()] = !solo[view()]; break;
            case CC_PLAY: params[RUN_PARAM].setValue(params[RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f); break;
            case CC_REC: params[LEARN_PARAM].setValue(params[LEARN_PARAM].getValue() > 0.5f ? 0.f : 1.f); break;
            case CC_PREV: stepParam(PAT_PARAM, -1, 1, 8, true); break;
            case CC_NEXT: stepParam(PAT_PARAM, 1, 1, 8, true); break;
            case CC_TEMPO: jamReset = true; break;
            case CC_MUTE: mute[view()] = !mute[view()]; break;
        }
    }
    // The OLED buttons on JAM CCs: SONG / 17 = FX / BYP, 18 = next JAM page, 25..34 = CLEAR, LEARN, +-5V,
    // the five FX RNDs, THROW's RND, FREEZE, 35 / 36 = BASS / LEAD. Returns false for any other CC.
    const int kOledCC[14] = {17, 18, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36};
    int lastOledLed[14] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    void toggle(Param& p) { p.setValue(p.getValue() > 0.5f ? 0.f : 1.f); }
    bool oledButton(int cc, int b) {
        if (cc == CC_CLEAR) return false;
        if (cc == 25) { jamButton(CC_CLEAR, b); return true; }   // held for a second clears every pattern
        bool known = cc == CC_SONG;
        for (int c : kOledCC) known |= c == cc;
        if (!known) return false;
        if (b <= 0) return true;
        switch (cc) {
            case CC_SONG: toggle(params[FX_ON_PARAM]); break;
            case 17: toggle(fx.params[SynTHBloKGrainForge::FLT_BYPASS_PARAM]); break;
            case 18: stepParam(PAGE_PARAM, 1, 1, 6, true); break;
            case 26: toggle(params[LEARN_PARAM]); break;
            case 27: toggle(params[RANGE_PARAM]); break;
            case 33: {   // THROW's RND: a fresh random throw pattern, like the panel button
                bool on[SynTHBloKGrainForge::kThrowSteps];
                int count = 0;
                float density = 0.2f + 0.3f * random::uniform();
                for (int i = 0; i < SynTHBloKGrainForge::kThrowSteps; i++) { on[i] = random::uniform() < density; count += on[i]; }
                while (count < 2) {
                    int i = (int)(random::uniform() * SynTHBloKGrainForge::kThrowSteps) % SynTHBloKGrainForge::kThrowSteps;
                    if (!on[i]) { on[i] = true; count++; }
                }
                for (int i = 0; i < SynTHBloKGrainForge::kThrowSteps; i++) fx.params[SynTHBloKGrainForge::throwParam(i)].setValue(on[i] ? 1.f : 0.f);
                break;
            }
            case 34: toggle(fx.params[SynTHBloKGrainForge::RND_FREEZE_PARAM]); break;
            case 35: params[VIEW_PARAM].setValue(1.f); break;
            case 36: params[VIEW_PARAM].setValue(0.f); break;
            default: toggle(fx.params[SynTHBloKGrainForge::RND_PARAM + (cc - 28)]); break;   // 28..32
        }
        return true;
    }
    int oledLed(int cc) {
        auto lit = [](Param& p) { return p.getValue() > 0.5f ? 127 : 0; };
        switch (cc) {
            case 17: return lit(fx.params[SynTHBloKGrainForge::FLT_BYPASS_PARAM]);
            case 18: return 127;
            case 25: return clearHeld ? 127 : 0;
            case 26: return lit(params[LEARN_PARAM]);
            case 27: return lit(params[RANGE_PARAM]);
            case 33: return 40;
            case 34: return lit(fx.params[SynTHBloKGrainForge::RND_FREEZE_PARAM]);
            case 35: return view() == 1 ? 127 : 0;
            case 36: return view() == 0 ? 127 : 0;
            default: return lit(fx.params[SynTHBloKGrainForge::RND_PARAM + (cc - 28)]);
        }
    }

    void sendNote(int n, int vel) { midi::Message m; m.bytes = {0x90, (uint8_t)n, (uint8_t)clamp(vel, 0, 127)}; midiOut.sendMessage(m); }
    void sendCC(int cc, int v) { midi::Message m; m.bytes = {0xB0, (uint8_t)cc, (uint8_t)clamp(v, 0, 127)}; midiOut.sendMessage(m); }
    void clearLeds() {
        for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) sendNote(padNote(r, c), 0);
        for (int i = 0; i < 8; i++) { sendCC(i, 0); sendCC(8 + i, 0); sendCC(48 + 8 * stripPage + i, 0); }
        for (int i = 0; i < kNumBtn; i++) sendCC(kBtnCC[i], 0);
        for (int i = 0; i < 14; i++) { sendCC(kOledCC[i], 0); lastOledLed[i] = -1; }
        for (auto& n : kBtnNote) if (n[0] > padNote(0, 7)) sendNote(n[0], 0);
        sendCC(knobCC, 0);
        for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
    }
    void onRemove(const RemoveEvent& e) override { clearLeds(); }
    void onBypass(const BypassEvent& e) override { clearLeds(); }
    void onUnBypass(const UnBypassEvent& e) override { for (int i = 0; i < kNumLed; i++) lastLed[i] = -1; resync = 0.f; }
    ~SynTHBloKDuo() { clearLeds(); }

    // JAM light idx (layout in Matrix8_ui.hpp): pads = the shown synth's notes in the pattern's colour,
    // the root rows dim, the playhead column white. Scenes = patterns, strips = their knobs,
    // groups A..E = strip banks (F..H dark).
    int desiredLed(int idx) {
        int p = curPat(), v = view();
        if (idx == kKnobLed) return (int)std::round(paramQuantities[knobParam()]->getScaledValue() * 127.f);
        if (idx >= 88) {
            switch (kBtnCC[idx - 88]) {
                case CC_SONG: return params[FX_ON_PARAM].getValue() > 0.5f ? 127 : 0;   // the OLED's FX
                case CC_PLAY: return running ? 127 : 0;
                case CC_REC: return params[LEARN_PARAM].getValue() > 0.5f ? 127 : 0;
                case CC_TEMPO: return resetFlash > 0.f ? 127 : 0;
                case CC_GRID: return params[RANGE_PARAM].getValue() > 0.5f ? 127 : 0;
                case CC_SOLO: return solo[v] ? 127 : (solo[0] || solo[1]) ? 20 : 0;
                case CC_MUTE: return mute[v] ? 127 : (mute[0] || mute[1]) ? 20 : 0;
                default: return 127;
            }
        }
        if (idx >= 72 && idx < 80) return (int)std::round(stripGet(idx - 72) * 127.f);
        if (matchColors) {
            if (idx < 64) {
                int r = idx / 8, c = idx % 8;
                bool a = note[p][v][c] >> (7 - r) & 1, root = r == 0 || r == 7;
                if (!trackOn(v)) return a ? kJamDim[p] : 0;
                if (running && step >= 0 && c == step) return a ? kJamWhite : kJamWhite - 2;
                return a ? kJamBright[p] : root ? kJamDim[p] : 0;
            }
            int i = idx < 72 ? idx - 64 : idx - 80;
            bool sel = idx < 72 ? i == p : i == bank();
            return sel ? kJamBright[i] : kJamDim[i];
        }
        if (idx < 72) {
            int on = (int)params[ONC_PARAM].getValue(), off = (int)params[OFFC_PARAM].getValue();
            int head = (int)params[HEADC_PARAM].getValue(), hit = (int)params[HITC_PARAM].getValue();
            if (idx < 64) {
                int r = idx / 8, c = idx % 8;
                bool a = note[p][v][c] >> (7 - r) & 1;
                if (running && step >= 0 && c == step) return a ? hit : head;
                return a ? on : off;
            }
            return (idx - 64) == p ? on : off;
        }
        return (idx - 80) == bank() ? (int)params[BONC_PARAM].getValue() : (int)params[BOFFC_PARAM].getValue();
    }
    void sendLed(int idx, int d) {
        if (idx < 64) sendNote(padNote(idx / 8, idx % 8), d);
        else if (idx < 72) sendCC(idx - 64, d);
        else if (idx < 80) sendCC(48 + 8 * stripPage + (idx - 72), d);
        else if (idx < 88) sendCC(8 + (idx - 80), d);
        else if (idx == kKnobLed) sendCC(knobCC, d);
        else {
            int cc = kBtnCC[idx - 88];
            sendCC(cc, d);
            int n = btnNoteFor(cc);
            if (n >= 0 && n > padNote(0, 7)) sendNote(n, d);
        }
    }
    void processJamIn(const ProcessArgs& args) {
        midi::Message msg;
        while (midiIn.tryPop(&msg, args.frame)) {
            if (msg.bytes.size() < 3) continue;
            int st = msg.bytes[0] & 0xf0, a = msg.bytes[1], b = msg.bytes[2];
            if (st == 0x90 && b > 0) {
                if (params[LEARN_PARAM].getValue() > 0.5f) {
                    int d = a - 56;
                    if (d >= 0 && d % 12 == 0 && d / 12 <= 5) {
                        params[PAGE_PARAM].setValue((float)(d / 12 + 1));
                        params[LEARN_PARAM].setValue(0.f);
                    }
                    continue;
                }
                bool pad = false;
                for (int r = 0; r < 8 && !pad; r++) for (int c = 0; c < 8; c++)
                    if (padNote(r, c) == a) { togglePad(r, c); pad = true; break; }
                if (!pad)
                    for (auto& n : kBtnNote) if (n[0] == a) jamButton(n[1], b);
            } else if (st == 0xB0) {
                if (oledButton(a, b)) continue;
                bool isBtn = false;
                for (int i = 0; i < kNumBtn; i++) if (kBtnCC[i] == a) isBtn = true;
                if (isBtn) jamButton(a, b);
                else if (a >= kKnobFirstCC && a <= kKnobLastCC) {
                    knobCC = a;
                    int id = knobParam();
                    stripTgt[id] = -1.f;
                    paramQuantities[id]->setScaledValue(b / 127.f);
                    lastLed[kKnobLed] = b;   // already showing on the JAM
                }
                else if (a == kKnobPushCC && b > 0) paramQuantities[knobParam()]->reset();
                else if (a >= 0 && a < 8 && b > 0) params[PAT_PARAM].setValue((float)(a + 1));
                else if (a >= 8 && a < 16 && b > 0) setBank(a - 8);
                else if (a >= 48 && a < 112) {
                    stripPage = (a - 48) / 8;
                    stripSet((a - 48) % 8, b / 127.f);
                }
            }
        }
    }
    // Strips glide their knobs to where the finger is (~10 ms), and CLEAR held 1 s clears every pattern.
    void processJamCtl(const ProcessArgs& args) {
        float k = 1.f - std::exp(-args.sampleTime * 100.f);
        for (int id = 0; id < NUM_PARAMS; id++) {
            float t = stripTgt[id];
            if (t < -0.5f) continue;
            float v = params[id].getValue();
            if (std::fabs(t - v) < 1e-4f) { params[id].setValue(t); stripTgt[id] = -1.f; }
            else params[id].setValue(v + (t - v) * k);
        }
        if (clearHeld && !clearAllDone && (clearHeldT += args.sampleTime) > 1.f) { clearAll(); clearAllDone = true; }
        // A bank A..D belongs to one synth: when the grid switches synth, the bank follows.
        int b = bank();
        if (b < 4 && b / 2 != view()) params[BANK_PARAM].setValue((float)(view() * 2 + b % 2 + 1));
        // STRIP CV = the shown bank's strips, KNOB CV = the knob's page.
        float k2 = 1.f - std::exp(-args.sampleTime * 1000.f);
        bool bip = params[RANGE_PARAM].getValue() > 0.5f;
        for (int i = 0; i < 8; i++) {
            stripSm[i] += (stripGet(i) * 10.f - (bip ? 5.f : 0.f) - stripSm[i]) * k2;
            outputs[STRIP_OUTPUT + i].setVoltage(stripSm[i]);
        }
        knobSm += (paramQuantities[knobParam()]->getScaledValue() * 10.f - knobSm) * k2;
        outputs[KNOB_OUTPUT].setVoltage(knobSm);
    }
    void processJamLeds(const ProcessArgs& args) {
        resync += args.sampleTime;
        // A new bank, view or strip page resends everything, so the JAM shows it at once.
        if (curPage() != lastPage || bank() != lastBank || view() != lastView || stripPage != lastStripPage || resync > 3.f) {
            for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
            for (int i = 0; i < 14; i++) lastOledLed[i] = -1;
            lastPage = curPage(); lastBank = bank(); lastView = view(); lastStripPage = stripPage; resync = 0.f;
        }
        tokens = std::min(16.f, tokens + args.sampleTime * 2000.f);
        for (int n = 0; n < 3; n++) {
            int d = desiredLed(ledScan);
            if (d != lastLed[ledScan]) {
                if (tokens < 1.f) break;
                tokens -= 1.f;
                sendLed(ledScan, d);
                lastLed[ledScan] = d;
            }
            ledScan = (ledScan + 1) % kNumLed;
        }
        for (int i = 0; i < 14 && tokens >= 1.f; i++) {
            int d = oledLed(kOledCC[i]);
            if (d == lastOledLed[i]) continue;
            tokens -= 1.f;
            sendCC(kOledCC[i], d);
            lastOledLed[i] = d;
        }
    }

    void process(const ProcessArgs& args) override {
        processJamIn(args);
        processJamCtl(args);
        // ---- clock: the CLOCK jack, or the BPM knob when unpatched ----
        bool edge = false;
        bool ext = inputs[CLOCK_INPUT].isConnected();
        float intStep = 15.f / params[BPM_PARAM].getValue();
        sinceClock += args.sampleTime;
        if (ext) {
            if (clkTrig.process(inputs[CLOCK_INPUT].getVoltage())) {
                if (sinceClock < 4.f) clockPeriod = sinceClock;
                sinceClock = 0.f;
                edge = true;
            }
        } else {
            internalTimer += args.sampleTime;
            if (internalTimer >= intStep) { internalTimer = std::min(internalTimer - intStep, intStep); edge = true; }
        }
        float stepSec = (ext && clockPeriod > 0.f && sinceClock < 2.f) ? clockPeriod : intStep;

        if (runTrig.process(inputs[RUN_INPUT].getVoltage()))
            params[RUN_PARAM].setValue(params[RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f);
        running = params[RUN_PARAM].getValue() > 0.5f;
        bool rst = rstTrig.process(inputs[RESET_INPUT].getVoltage()) | rstBtnTrig.process(params[RESET_PARAM].getValue()) | jamReset;
        jamReset = false;
        if (rst) { step = -1; pendingReset = true; internalTimer = 0.f; resetFlash = 0.15f; }
        resetFlash = std::max(0.f, resetFlash - args.sampleTime);
        if (clrTrig.process(params[CLEAR_PARAM].getValue()))
            for (int c = 0; c < 8; c++) note[curPat()][view()][c] = 0;

        if (edge && running) {
            int len = clamp((int)params[LEN_PARAM].getValue(), 1, 8);
            step = (pendingReset || step < 0) ? 0 : (step + 1) % len;
            pendingReset = false;
            bool played[2];
            for (int e = 0; e < 2; e++) played[e] = playEngine(e, stepSec);
            // filter ADSR: TRIG picks what fires it; it holds open for half a step (a beat: two steps)
            int trig = clamp((int)params[FLT_TRIG_PARAM].getValue(), 0, 4);
            bool fire = trig == 0 ? played[0] || played[1] : trig == 1 ? played[0] : trig == 2 ? played[1]
                      : trig == 3 ? step % 4 == 0 : true;
            if (fire) { fltEnv.trigger(); fltGate = (trig == 3 ? 2.f : 0.5f) * stepSec * args.sampleRate; }
        }
        for (int r = 0; r < 8; r++) outputs[TRIG_OUTPUT + r].setVoltage(trigPulse[r].process(args.sampleTime) ? 10.f : 0.f);

        // ---- the two synths ----
        float mix = 0.f;
        // MIX: LEAD <-> BASS for OUT L / R; the middle keeps both at full.
        float km = 1.f - std::exp(-args.sampleTime * 1000.f);
        mixSm += (params[MIX_PARAM].getValue() - mixSm) * km;
        const float mixGain[2] = {std::min(1.f, 2.f * (1.f - mixSm)), std::min(1.f, 2.f * mixSm)};
        for (int e = 0; e < 2; e++) {
            float p[DO_NUM];
            for (int i = 0; i < DO_NUM; i++) p[i] = params[engParam(e, i)].getValue();
            float tv = p[DO_TUNE];
            p[DO_TUNE] = 12.f * tv * std::fabs(tv);
            // idle voices are skipped; the chord level drops as 1 / sqrt(notes)
            chordGainSm[e] += (chordGain[e] - chordGainSm[e]) * km;
            float s = 0.f;
            for (int k = 0; k < kVoices; k++)
                if (k == 0 || eng[e][k].stage != 0 || std::fabs(eng[e][k].adsr) > 1e-5f) s += eng[e][k].process(args.sampleRate, p);
            s *= chordGainSm[e];
            mix += s * mixGain[e];
            outputs[e ? BASS_OUTPUT : LEAD_OUTPUT].setVoltage(5.f * s);
            outputs[e ? BASS_CV_OUTPUT : LEAD_CV_OUTPUT].setVoltage(curNote[e] < 0 ? 0.f : (curNote[e] - 60) / 12.f);
            outputs[e ? BASS_GATE_OUTPUT : LEAD_GATE_OUTPUT].setVoltage(gateOn(e) ? 10.f : 0.f);
        }
        // ---- master filter (GRAIN-FORGE's): cutoff + ADSR sweep, resonance, stages, LP / BP / HP ----
        float sr = args.sampleRate;
        for (int i = 0; i < kFltSm; i++) {
            float t = params[FLT_CUT_PARAM + i].getValue();
            fltSm[i] = fltSmInit ? fltSm[i] + (t - fltSm[i]) * km : t;
        }
        fltSmInit = true;
        auto fs = [&](int id) { return fltSm[id - FLT_CUT_PARAM]; };
        if (fltGate > 0.f && (fltGate -= 1.f) <= 0.f) fltEnv.release();
        float envRaw = fltEnv.process(0.003f * std::pow(667.f, fs(FLT_ATK_PARAM)), 0.012f * std::pow(333.f, fs(FLT_DEC_PARAM)),
                                      fs(FLT_SUS_PARAM), 0.012f * std::pow(333.f, fs(FLT_REL_PARAM)), sr);
        float ec = 1.f - std::exp(-1.f / (0.0015f * sr));   // round the envelope's corners: no pops
        envS1 += (envRaw - envS1) * ec; envS2 += (envS1 - envS2) * ec;
        {   // stage lights: the active stage flashes and fades over 150 ms
            int st = fltEnv.st == DUAdsr::ATK ? 0 : fltEnv.st == DUAdsr::REL ? 3
                   : fltEnv.st == DUAdsr::DEC ? (std::fabs(fltEnv.v - fs(FLT_SUS_PARAM)) < 0.02f ? 2 : 1) : -1;
            for (int k = 0; k < 4; k++) {
                stageLvl[k] = std::max(k == st ? 1.f : 0.f, stageLvl[k] - args.sampleTime / 0.15f);
                lights[ADSR_LIGHT + k].setBrightness(stageLvl[k]);
            }
        }
        float env = envS2 * (1.f - fs(FLT_BYPASS_PARAM));
        float fenv = fs(FLT_ENV_PARAM) * std::fabs(fs(FLT_ENV_PARAM));   // squared: fine sweeps around the middle
        float fcut = clamp(fs(FLT_CUT_PARAM) + env * fenv, 0.f, 1.f);
        float fres = fs(FLT_RES_PARAM) + 0.2f * env * std::fabs(fenv);
        float wet = mix * fs(FLT_IN_PARAM);
        flt.process(wet, fcut, fres, sr, fs(FLT_STAGES_PARAM), fs(FLT_MODE_PARAM));
        float ea = std::min(env, 1.f) * std::fabs(fenv);   // big sweeps run hot: tame the peak a little
        wet *= 1.f - ea * (0.25f + 0.2f * fs(FLT_RES_PARAM));
        wet *= 1.f - fs(FLT_AMP_PARAM) * (1.f - fs(FLT_BYPASS_PARAM)) * (1.f - std::min(envS2, 1.f));
        mix += (wet - mix) * fs(FLT_DRYWET_PARAM);
        // ---- FORGE FX (stereo from here on; it always runs, so ON crossfades) and master ----
        float dry = 5.f * std::tanh(mix), fxL, fxR;
        fx.processFx(args, dry, dry, edge && running, stepSec, rst, fxL, fxR);
        fxOnSm += (params[FX_ON_PARAM].getValue() - fxOnSm) * km;
        masterVol += (params[MASTER_PARAM].getValue() - masterVol) * (1.f - std::exp(-args.sampleTime * 1000.f));
        outputs[OUT_L_OUTPUT].setVoltage((dry + (5.f * fxL - dry) * fxOnSm) * masterVol);
        outputs[OUT_R_OUTPUT].setVoltage((dry + (5.f * fxR - dry) * fxOnSm) * masterVol);
        processJamLeds(args);
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_t* na = json_array();
        json_t* ca = json_array();
        for (int p = 0; p < 8; p++) {
            std::string n, c;
            for (int i = 0; i < 16; i++) {   // the lowest note, for older versions; "chords" has them all
                int m = note[p][i / 8][i % 8], d = 0;
                while (m && !(m >> d & 1)) d++;
                n += m ? (char)('0' + d) : '-';
                char h[3]; std::snprintf(h, sizeof(h), "%02x", m); c += h;
            }
            json_array_append_new(na, json_string(n.c_str()));
            json_array_append_new(ca, json_string(c.c_str()));
        }
        json_object_set_new(root, "notes", na);
        json_object_set_new(root, "chords", ca);   // per step: two hex digits, bit d = scale degree d
        std::string ms;
        for (int e = 0; e < 2; e++) ms += mute[e] ? '1' : '0';
        json_object_set_new(root, "mute", json_string(ms.c_str()));
        std::string ss;
        for (int e = 0; e < 2; e++) ss += solo[e] ? '1' : '0';
        json_object_set_new(root, "solo", json_string(ss.c_str()));
        json_object_set_new(root, "matchColors", json_boolean(matchColors));
        json_t* va = json_array();
        for (int b = 0; b < 3; b++) for (int i = 0; i < 8; i++) json_array_append_new(va, json_real(cvVal[b][i]));
        json_object_set_new(root, "cv", va);
        json_object_set_new(root, "in", midiIn.toJson());
        json_object_set_new(root, "out", midiOut.toJson());
        json_t* fj = json_object();
        json_object_set_new(fj, "params", fx.paramsToJson());
        json_object_set_new(fj, "data", fx.dataToJson());
        json_object_set_new(root, "fx", fj);
        return root;
    }
    // FORGE FX settings: the engine's params, then what GRAIN-FORGE's fromJson restores from its data
    void fxFromJson(json_t* fj) {
        typedef SynTHBloKGrainForge G;
        if (json_t* ps = json_object_get(fj, "params")) fx.paramsFromJson(ps);
        json_t* d = json_object_get(fj, "data");
        if (json_t* hr = d ? json_object_get(d, "rbHomeRoot") : NULL) {
            fx.homeRoot = clamp((int)json_integer_value(hr), 0, 24);
            fx.homeChord = clamp((int)json_integer_value(json_object_get(d, "rbHomeChord")), 0, 7);
            fx.rndWasOn[G::R_RB] = fx.on(G::RND_PARAM + G::R_RB);
        }
        json_t* sq = d ? json_object_get(d, "seq") : NULL;
        if (sq && json_array_size(sq) == (size_t)G::kSeqMax) {
            for (int i = 0; i < G::kSeqMax; i++) {
                float v = (float)json_number_value(json_array_get(sq, i));
                fx.seqOn[i] = v >= 0.f; fx.seqLvl[i] = clamp(v >= 0.f ? v : -1.f - v, 0.f, 1.f);
            }
            fx.seqLen = clamp((int)json_integer_value(json_object_get(d, "seqLen")), 1, G::kSeqMax);
        }
        fx.lastPreset = fx.slot(G::PRESET_PARAM);   // loading must not re-apply the preset over its own settings
    }
    void dataFromJson(json_t* r) override {
        if (json_t* na = json_object_get(r, "notes"))
            for (int p = 0; p < 8; p++) {
                const char* n = json_string_value(json_array_get(na, p));
                if (n && strlen(n) >= 16)
                    for (int i = 0; i < 16; i++) note[p][i / 8][i % 8] = n[i] >= '0' && n[i] <= '7' ? 1 << (n[i] - '0') : 0;
            }
        if (json_t* ca = json_object_get(r, "chords"))
            for (int p = 0; p < 8; p++) {
                const char* c = json_string_value(json_array_get(ca, p));
                if (c && strlen(c) >= 32)
                    for (int i = 0; i < 16; i++) note[p][i / 8][i % 8] = (uint8_t)std::strtol(std::string(c + 2 * i, 2).c_str(), NULL, 16);
            }
        const char* ms = json_string_value(json_object_get(r, "mute"));
        for (int e = 0; e < 2; e++) mute[e] = ms && strlen(ms) >= 2 && ms[e] == '1';
        const char* ss = json_string_value(json_object_get(r, "solo"));
        for (int e = 0; e < 2; e++) solo[e] = ss && strlen(ss) >= 2 && ss[e] == '1';
        if (json_t* j = json_object_get(r, "matchColors")) matchColors = json_boolean_value(j);
        if (json_t* va = json_object_get(r, "cv"))
            for (int i = 0; i < 24; i++)
                if (json_t* j = json_array_get(va, i)) cvVal[i / 8][i % 8] = clamp((float)json_number_value(j), 0.f, 1.f);
        if (json_t* j = json_object_get(r, "in")) midiIn.fromJson(j);
        if (json_t* j = json_object_get(r, "out")) midiOut.fromJson(j);
        if (json_t* j = json_object_get(r, "fx")) fxFromJson(j);
    }
};

typedef SynTHBloKDuo DU;

// ---------------------------------------------------------------- widgets ----------------

static std::string noteName(int n) {
    if (n < 0) return "-";
    return std::string(kKeyName[n % 12]) + std::to_string(n / 12 - 1);
}

// Note pad: lit = this step's note, the root rows (top and bottom) outlined brighter.
struct DUPad : OpaqueWidget {
    DU* module = NULL;
    int r = 0, c = 0;
    void draw(const DrawArgs& args) override {
        static const int preview[8] = {4, -1, 2, 4, -1, 7, 5, 2};
        bool on = module ? module->padOn(r, c) : 7 - r == preview[c];
        bool playing = module && module->step >= 0 && module->step == c;
        int p = module ? module->curPat() : 0;
        NVGcolor col = kPatColor[p], onCol = kPatOnColor[p];
        if (r != 0 && r != 7) col = nvgLerpRGBA(kIdle, col, 0.5f);
        if (module && module->mute[module->view()]) {
            col = nvgLerpRGBA(kIdle, col, 0.35f);
            onCol = nvgLerpRGBA(kIdle, onCol, 0.35f);
        }
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        NVGcolor fill = on ? onCol : kIdle;
        if (playing) fill = on ? nvgLerpRGBA(onCol, nvgRGB(0xff, 0xff, 0xff), 0.55f) : nvgLerpRGBA(kIdle, col, 0.45f);
        nvgFillColor(args.vg, fill);
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, col);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->togglePad(r, c);
            e.consume(this);
        }
    }
};

struct DUPatBtn : OpaqueWidget {
    DU* module = NULL;
    int p = 0;
    void draw(const DrawArgs& args) override {
        drawColorButton(args, box.size, p, module ? module->curPat() == p : p == 0, (char)('1' + p), 11.f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[DU::PAT_PARAM].setValue((float)(p + 1));
            e.consume(this);
        }
    }
};

// Amber value arc around a knob (0..1 of the knob's range).
struct DURing : TransparentWidget {
    Module* module = NULL;
    int param = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float v = module ? module->paramQuantities[param]->getScaledValue() : 0.5f;
        float cx = box.size.x * 0.5f, cy = box.size.y * 0.5f, rr = box.size.x * 0.5f - 1.5f;
        float a0 = M_PI * 0.75f, a1 = M_PI * 2.25f;
        nvgBeginPath(args.vg);
        nvgArc(args.vg, cx, cy, rr, a0, a1, NVG_CW);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 40));
        nvgStrokeWidth(args.vg, 1.8f);
        nvgStroke(args.vg);
        if (v > 0.001f) {
            nvgBeginPath(args.vg);
            nvgArc(args.vg, cx, cy, rr, a0, a0 + (a1 - a0) * v, NVG_CW);
            nvgStrokeColor(args.vg, nvgRGBA(255, 112, 15, 235));
            nvgStrokeWidth(args.vg, 1.8f);
            nvgStroke(args.vg);
        }
    }
};

// Amber on/off button (SYNC).
struct DUToggle : app::ParamWidget {
    const char* label = "";
    void draw(const DrawArgs& args) override {
        ParamQuantity* pq = getParamQuantity();
        bool lit = pq && pq->getValue() > 0.5f;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.2f);
        nvgFillColor(args.vg, lit ? nvgRGBA(255, 112, 15, 235) : nvgRGBA(40, 25, 8, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, lit ? 255 : 120));
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 6.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, lit ? nvgRGBA(20, 10, 2, 255) : nvgRGBA(255, 190, 90, 255));
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, label, NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (ParamQuantity* pq = getParamQuantity()) pq->setValue(pq->getValue() > 0.5f ? 0.f : 1.f);
            e.consume(this);
            return;
        }
        ParamWidget::onButton(e);
    }
};

// Filter ADSR stage light, amber like the rings.
struct DUAmberLight : GrayModuleLightWidget {
    DUAmberLight() { addBaseColor(nvgRGB(255, 140, 40)); }
};

struct DUTitle : TransparentWidget {
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
        nvgFontSize(args.vg, 11.f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgText(args.vg, 0.f, mm2px(3.5f), "SynTHBloK", NULL);
        nvgFontSize(args.vg, 10.f);
        nvgTextLetterSpacing(args.vg, 0.8f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0x7a, 0x00));
        nvgText(args.vg, 0.f, mm2px(6.4f), "MATRIX-DUO", NULL);
    }
};

// LEAD / BASS header: name, ON / MUTE, the last note, sync. Click toggles mute;
// brighter outline while the grid shows this synth.
struct DUHeader : TransparentWidget {
    DU* module = NULL;
    int e = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        bool on = !module || !module->mute[e];
        bool shown = module ? module->view() == e : e == 0;
        oled::box(args, W, H, shown ? nvgRGBA(255, 112, 15, 255) : nvgRGBA(255, 180, 90, on ? 160 : 60), shown ? 1.2f : 0.8f);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, nvgRGBA(255, 190, 90, on ? 220 : 90));
        nvgText(args.vg, 5.f, H * 0.3f, e ? "BASS" : "LEAD", NULL);
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? nvgRGBA(120, 230, 90, 255) : nvgRGBA(255, 190, 90, 90));
        nvgText(args.vg, W - 5.f, H * 0.3f, on ? "ON" : "MUTE", NULL);
        std::string s = noteName(module ? module->curNote[e] : (e ? 36 : 60));
        bool gate = module && module->gateOn(e);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 9.5f);
        nvgFillColor(args.vg, nvgRGBA(255, 190, 90, gate ? 255 : 130));
        nvgText(args.vg, 5.f, H * 0.7f, s.c_str(), NULL);
        // SYNC: its own box in the lower right, lit when on.
        bool sync = module && module->params[module->engParam(e, DO_SYNC)].getValue() > 0.5f;
        float sx = W * 0.62f, sy = H * 0.52f, sw = W * 0.38f - 4.f, sh = H * 0.48f - 3.f;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, sx, sy, sw, sh, 1.2f);
        nvgFillColor(args.vg, sync ? nvgRGBA(255, 112, 15, 235) : nvgRGBA(40, 25, 8, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, sync ? 255 : 110));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 7.f);
        nvgFillColor(args.vg, sync ? nvgRGBA(20, 10, 2, 255) : nvgRGBA(255, 190, 90, 200));
        nvgText(args.vg, sx + sw * 0.5f, sy + sh * 0.5f, "SYNC", NULL);
    }
    // Click the SYNC box to toggle hard sync, anywhere else to mute / unmute.
    void onButton(const ButtonEvent& ev) override {
        if (ev.button == GLFW_MOUSE_BUTTON_LEFT && ev.action == GLFW_PRESS) {
            if (module) {
                if (ev.pos.x > box.size.x * 0.6f && ev.pos.y > box.size.y * 0.5f) {
                    Param& p = module->params[module->engParam(e, DO_SYNC)];
                    p.setValue(p.getValue() > 0.5f ? 0.f : 1.f);
                }
                else module->mute[e] = !module->mute[e];
            }
            ev.consume(this);
        }
    }
};

// The master OLED (OledUi.hpp look), one line each: MIDI IN, MIDI OUT (click = port menu); BPM, run,
// step, strip bank; KEY | SCALE | filter env TRIG (click or scroll steps it forward, right-click back).
// The button cells (oled::Cell) sit on its lower part. Rows in mm from the top:
static const float kMoRow = oled::kRow, kMoInfo = 2.f * kMoRow, kMoKey = 3.f * kMoRow, kMoBtn = 4.f * kMoRow + 1.f,
                   kMoBtnH = oled::kCellH, kMoBtnGap = 0.9f;
static const float kMoCol[2] = {0.27f, 0.62f};   // KEY | SCALE | TRIG column edges, of the width
static const char* kTrigShort[5] = {"NOTES", "LEAD", "BASS", "BEAT", "STEP"};
struct DUMasterDisplay : TransparentWidget {
    DU* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        nvgBeginPath(args.vg);
        for (float y : {kMoRow, kMoInfo, kMoKey, kMoKey + kMoRow}) oled::divider(args, 4.f, mm2px(y), W - 4.f, mm2px(y));
        for (float c : kMoCol) oled::divider(args, W * c, mm2px(kMoKey) + 2.f, W * c, mm2px(kMoKey + kMoRow) - 2.f);
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W, H);
        oled::midiLine(args, module ? &module->midiIn : NULL, "IN", 0.f);
        oled::midiLine(args, module ? &module->midiOut : NULL, "OUT", mm2px(kMoRow));
        NVGcolor amb = oled::amber(), dim = oled::dim();
        char bpm[24] = "120 BPM", info[32] = "STEP 1/8";
        bool run = module ? module->running : true;
        int b = module ? module->bank() : 0;
        if (module) {
            if (!module->inputs[DU::CLOCK_INPUT].isConnected()) snprintf(bpm, sizeof bpm, "%.0f BPM", module->params[DU::BPM_PARAM].getValue());
            else if (module->clockPeriod > 0.f && module->sinceClock < 2.f) snprintf(bpm, sizeof bpm, "%.1f BPM", 60.f / (module->clockPeriod * 4.f));
            else snprintf(bpm, sizeof bpm, "--- BPM");
            int len = clamp((int)module->params[DU::LEN_PARAM].getValue(), 1, 8);
            snprintf(info, sizeof info, "%d/%d", module->step + 1, len);
        }
        char bank[24];
        snprintf(bank, sizeof bank, "%c %s", 'A' + b, kDuBankName[b]);
        // tempo, run, step, strip bank
        const float yi = mm2px(kMoInfo + kMoRow * 0.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 8.f);
        nvgFillColor(args.vg, amb);
        nvgText(args.vg, 4.f, yi, bpm, NULL);
        nvgFontSize(args.vg, 6.5f);
        nvgFillColor(args.vg, run ? nvgRGBA(120, 230, 90, 255) : dim);
        nvgText(args.vg, W * 0.36f, yi, run ? "RUN" : "STOP", NULL);
        nvgFillColor(args.vg, dim);
        nvgText(args.vg, W * 0.5f, yi, info, NULL);
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, W - 4.f, yi, bank, NULL);
        // KEY | SCALE | TRIG: a dim name, the value in amber
        int key = module ? clamp((int)module->params[DU::KEY_PARAM].getValue(), 0, 11) : 0;
        int sc = module ? clamp((int)module->params[DU::SCALE_PARAM].getValue(), 0, kScaleCount - 1) : 0;
        int tr = module ? clamp((int)module->params[DU::FLT_TRIG_PARAM].getValue(), 0, 4) : 0;
        const char* top[3] = {"KEY", "", "ENV"};
        const char* val[3] = {kKeyName[key], kScaleName[sc], kTrigShort[tr]};
        const float x0[3] = {0.f, W * kMoCol[0], W * kMoCol[1]}, x1[3] = {W * kMoCol[0], W * kMoCol[1], W};
        const float yk = mm2px(kMoKey + kMoRow * 0.5f);
        for (int i = 0; i < 3; i++) {
            nvgFontSize(args.vg, 6.f);
            nvgFillColor(args.vg, dim);
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, x0[i] + 4.f, yk, top[i], NULL);
            nvgFontSize(args.vg, 7.5f);
            nvgFillColor(args.vg, amb);
            nvgTextAlign(args.vg, (top[i][0] ? NVG_ALIGN_RIGHT : NVG_ALIGN_CENTER) | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, top[i][0] ? x1[i] - 4.f : (x0[i] + x1[i]) * 0.5f, yk, val[i], NULL);
        }
        nvgResetScissor(args.vg);
    }
    bool inKey(float y) { return y >= mm2px(kMoKey) && y < mm2px(kMoKey + kMoRow); }
    void stepParam(float x, int d) {
        if (!module) return;
        float W = box.size.x;
        int id = x < W * kMoCol[0] ? DU::KEY_PARAM : x < W * kMoCol[1] ? DU::SCALE_PARAM : DU::FLT_TRIG_PARAM;
        int n = id == DU::KEY_PARAM ? 12 : id == DU::SCALE_PARAM ? kScaleCount : 5;
        module->params[id].setValue((float)(((int)module->params[id].getValue() + d + n) % n));
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || (e.button != GLFW_MOUSE_BUTTON_LEFT && e.button != GLFW_MOUSE_BUTTON_RIGHT)) return;
        if (e.pos.y < mm2px(kMoInfo)) {
            if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
            if (module) {
                if (e.pos.y < mm2px(kMoRow)) oled::midiMenu(&module->midiIn, "MIDI IN port");
                else oled::midiMenu(&module->midiOut, "MIDI OUT port");
            }
            e.consume(this);
            return;
        }
        if (!inKey(e.pos.y)) return;
        stepParam(e.pos.x, e.button == GLFW_MOUSE_BUTTON_LEFT ? 1 : -1);
        e.consume(this);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (e.scrollDelta.y == 0.f || !inKey(e.pos.y)) return;
        stepParam(e.pos.x, e.scrollDelta.y > 0.f ? 1 : -1);
        e.consume(this);
    }
};

// Touch strip (as on MATRIX FORGE): the shown bank's value for strip idx; click or drag it.
struct DUStrip : OpaqueWidget {
    DU* module = NULL;
    int idx = 0;
    float get() { return module ? module->stripGet(idx) : 0.25f + 0.08f * idx; }
    void set(float v) { if (module) module->stripSet(idx, v); }
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        nvgFillColor(args.vg, nvgRGB(0x14, 0x14, 0x14));
        nvgFill(args.vg);
        NVGcolor col = kPatOnColor[module ? module->bank() : 0];
        float v = get(), h = (box.size.y - 3.f) * v;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 1.5f, box.size.y - 1.5f - h, box.size.x - 3.f, h, 1.f);
        nvgFillColor(args.vg, col);
        nvgFill(args.vg);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        nvgStrokeColor(args.vg, nvgLerpRGBA(kIdle, col, 0.45f));
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            set(1.f - clamp((e.pos.y - 1.5f) / (box.size.y - 3.f), 0.f, 1.f));
            e.consume(this);
        }
    }
    void onDragMove(const DragMoveEvent& e) override {
        float z = APP->scene->rackScroll->getZoom();
        set(get() - e.mouseDelta.y / z / (box.size.y - 3.f));
    }
};

struct DUBankSq : OpaqueWidget {
    DU* module = NULL;
    int b = 0;
    void draw(const DrawArgs& args) override {
        drawColorButton(args, box.size, b, module ? module->bank() == b : b == 0, (char)('A' + b), 9.f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->setBank(b);
            e.consume(this);
        }
    }
};

// ---------------------------------------------------------------- FORGE FX ----------------
// GRAIN-FORGE's FX chain in one OLED (OledUi.hpp look). Rows (mm from the OLED's top): one per stage
// (ON cell with its name, live status, RND; PULSE also LP | BP | HP), THROW steps + their RND,
// PRESET | CYCLE | GLIDE | FREEZE, the filter env TRIG + SEQ (two rows tall) with BYP, and FILTER:
// STG | TYPE, the env stages, the output meter and FX ON. Click / scroll a value to step it,
// right-click steps back (PRESET: the list).
typedef SynTHBloKGrainForge FXG;
static const float kFxW = 99.68f, kFxPad = 2.f, kFxCol = 19.f, kFxRndW = 8.f, kFxRndX = kFxW - kFxPad - kFxRndW,
                   kFxPitch = oled::kCellH + 0.9f, kFxTop = 1.2f, kFxSeqY = kFxTop + 7.f * kFxPitch,
                   kFxSeqH = 2.f * oled::kCellH, kFxFltY = kFxSeqY + kFxSeqH + 0.9f, kFxH = kFxFltY + oled::kCellH + kFxTop;
static const float kFxModeX = kFxRndX - 1.f - 3.f * 6.f - 2.f * 0.5f;   // PULSE's LP | BP | HP
static const float kFxPreSeg[2] = {44.5f, 66.75f};                     // PRESET | CYCLE | GLIDE
static const float kFxStgX = kFxCol, kFxTypeX = 36.5f, kFxEnvX = 57.f, kFxMeterX = 71.5f;
static float fxRowY(int i) { return kFxTop + i * kFxPitch; }
struct FxStage { const char* name; int on, sel, count; };
static const FxStage kFxStage[5] = {
    {"SHUFFLE", FXG::SH_ON_PARAM, FXG::SH_PATTERN_PARAM, kShufCount}, {"GLITCH", FXG::GL_ON_PARAM, FXG::GL_ROLL_PARAM, kRollCount},
    {"ROBOT", FXG::RB_ON_PARAM, FXG::RB_CHORD_PARAM, 8}, {"PULSE", FXG::PU_ON_PARAM, FXG::PU_PATTERN_PARAM, kPulseCount},
    {"WARP DLY", FXG::DL_ON_PARAM, FXG::DL_TIME_PARAM, kDelayCount}};

// A stage's live status, as on GRAIN-FORGE's stage headers (PULSE's mode is on its cells).
static std::string fxStatus(FXG* m, int s) {
    char b[32];
    switch (s) {
        case 0: {
            int p = m ? clamp(m->slot(FXG::SH_PATTERN_PARAM), 0, kShufCount - 1) : 6, src = m ? m->shuffle.src : -1;
            if (src < 0) snprintf(b, sizeof b, "%s  LIVE", kShufName[p]); else snprintf(b, sizeof b, "%s  >%02d", kShufName[p], src + 1);
            return b;
        }
        case 1: return m ? m->glitchEvent : "ROLL 1/16";
        case 2: {
            int root = m ? m->slot(FXG::RB_ROOT_PARAM) : 12, ch = m ? clamp(m->slot(FXG::RB_CHORD_PARAM), 0, 7) : 3;
            return std::string(kNote[root % 12]) + std::to_string(1 + (root + 9) / 12) + " " + kChordName[ch];
        }
        case 3:
            if (m && m->inputs[FXG::OPEN_INPUT].getVoltage() > 1.f) return "OPEN";
            return kPulseName[m ? clamp(m->slot(FXG::PU_PATTERN_PARAM), 0, kPulseCount - 1) : 3];
        default: {
            int t = m ? clamp(m->slot(FXG::DL_TIME_PARAM), 0, kDelayCount - 1) : 8;
            snprintf(b, sizeof b, "%s  %+.0f st", kDelayName[t], m ? m->params[FXG::DL_WARP_PARAM].getValue() : 0.f);
            return b;
        }
    }
}

struct FXDisplay : TransparentWidget {
    FXG* module = NULL;
    float peak[2] = {};
    void text(const DrawArgs& args, float x, float yMm, const char* s, NVGcolor c, float size, int align = NVG_ALIGN_LEFT) {
        nvgFontSize(args.vg, size);
        nvgFillColor(args.vg, c);
        nvgTextAlign(args.vg, align | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, x, mm2px(yMm + oled::kCellH * 0.5f), s, NULL);
    }
    // thin dBFS bar, -48..+6 dB in 16 segments (amber, orange above -9, red above -1), with a falling peak
    void meter(const DrawArgs& args, float x, float y, float w, float h, float lvl, float& pk) {
        const int N = 16;
        auto pos = [](float v) { return clamp((GFStatus::toDb(v) + 48.f) / 54.f, 0.f, 1.f); };
        pk = std::max(lvl, pk * 0.9975f);
        float p = pos(lvl), pp = pos(pk), sw = w / N;
        for (int i = 0; i < N; i++) {
            float sp = (i + 1.f) / N, db = -48.f + 54.f * sp;
            bool lit = sp <= p + 1e-4f, pkSeg = (int)std::ceil(pp * N) == i + 1 && pp > 0.02f;
            NVGcolor c = db > -1.f ? nvgRGBA(255, 40, 30, 255) : db > -9.f ? nvgRGBA(255, 112, 15, 255) : nvgRGBA(255, 176, 60, 255);
            if (!lit && !pkSeg) c = nvgRGBA(46, 29, 10, 255);
            else if (!lit) c.a = 0.5f;
            nvgBeginPath(args.vg);
            nvgRect(args.vg, x + i * sw + 0.3f, y, sw - 0.6f, h);
            nvgFillColor(args.vg, c);
            nvgFill(args.vg);
        }
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        FXG* m = module;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        nvgBeginPath(args.vg);
        for (float y : {fxRowY(1), fxRowY(2), fxRowY(3), fxRowY(4), fxRowY(5), fxRowY(6), kFxSeqY, kFxFltY})
            oled::divider(args, 4.f, mm2px(y - 0.45f), W - 4.f, mm2px(y - 0.45f));
        for (float x : kFxPreSeg) oled::divider(args, mm2px(x), mm2px(fxRowY(6)) + 2.f, mm2px(x), mm2px(fxRowY(6) + oled::kCellH) - 2.f);
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W, H);
        NVGcolor amb = oled::amber(), dim = oled::dim();
        // stage status, dim while the stage is off
        for (int s = 0; s < 5; s++) {
            bool on = !m || m->on(kFxStage[s].on);
            text(args, mm2px(kFxCol + 0.5f), fxRowY(s), fxStatus(m, s).c_str(), oled::amber(on ? 255 : 90), 7.5f);
        }
        text(args, 4.f, fxRowY(5), "THROW", dim, 6.f);
        // PRESET (the name in its preset-bar colour) | CYCLE | GLIDE
        int pr = m ? clamp(m->slot(FXG::PRESET_PARAM), 0, kPresetCount) : 0;
        text(args, 4.f, fxRowY(6), "PRESET", dim, 6.f);
        text(args, mm2px(kFxCol + 0.5f), fxRowY(6), GFPresetBar::nameOf(pr), GFPresetBar::cellColor(pr, 255), 7.5f);
        int dv = m ? clamp(m->slot(FXG::RND_DIV_PARAM), 0, kRndDivCount - 1) : 2;
        int gl = m ? clamp(m->slot(FXG::RND_GLIDE_PARAM), 0, kRndGlideCount - 1) : 2;
        const char* top[2] = {"CYCLE", "GLIDE"};
        const char* val[2] = {kRndDivName[dv], kRndGlideName[gl]};
        const float x0[2] = {kFxPreSeg[0], kFxPreSeg[1]}, x1[2] = {kFxPreSeg[1], kFxRndX - 1.f};
        for (int i = 0; i < 2; i++) {
            text(args, mm2px(x0[i]) + 4.f, fxRowY(6), top[i], dim, 6.f);
            text(args, mm2px(x1[i]) - 4.f, fxRowY(6), val[i], amb, 7.5f, NVG_ALIGN_RIGHT);
        }
        // FILTER: env stages lit as they run, then the output meter (L over R)
        text(args, 4.f, kFxFltY, "FILTER", dim, 6.f);
        static const char* an[4] = {"A", "D", "S", "R"};
        const float ew = (kFxMeterX - 1.5f - kFxEnvX - 3.f * 0.4f) / 4.f;
        for (int k = 0; k < 4; k++) {
            float lv = m ? clamp(m->stageLvl[k], 0.f, 1.f) : 0.f, ex = mm2px(kFxEnvX + k * (ew + 0.4f));
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, ex, mm2px(kFxFltY), mm2px(ew), mm2px(oled::kCellH), 1.f);
            nvgFillColor(args.vg, nvgLerpRGBA(oled::offFill(), oled::litFill(), lv));
            nvgFill(args.vg);
            text(args, ex + mm2px(ew) * 0.5f, kFxFltY, an[k], lv > 0.5f ? oled::ink() : dim, 6.f, NVG_ALIGN_CENTER);
        }
        float mx = mm2px(kFxMeterX), mw = mm2px(kFxRndX - 1.f - kFxMeterX), mh = mm2px(1.5f);
        meter(args, mx, mm2px(kFxFltY + 0.5f), mw, mh, m ? m->meterL : 0.5f, peak[0]);
        meter(args, mx, mm2px(kFxFltY + oled::kCellH - 0.5f) - mh, mw, mh, m ? m->meterR : 0.4f, peak[1]);
        nvgResetScissor(args.vg);
    }
    // what a click / scroll at p (mm) steps by d; false = nothing there
    bool stepAt(Vec p, int d) {
        FXG* m = module;
        for (int s = 0; s < 5; s++) {
            float y = fxRowY(s), xe = s == 3 ? kFxModeX : kFxRndX;
            if (p.y < y || p.y >= y + oled::kCellH || p.x < kFxCol || p.x >= xe) continue;
            const FxStage& st = kFxStage[s];
            m->params[st.sel].setValue((float)((m->slot(st.sel) + d + st.count) % st.count));
            return true;
        }
        float y = fxRowY(6);
        if (p.y < y || p.y >= y + oled::kCellH || p.x < kFxCol || p.x >= kFxRndX) return false;
        int id = p.x < kFxPreSeg[0] ? FXG::PRESET_PARAM : p.x < kFxPreSeg[1] ? FXG::RND_DIV_PARAM : FXG::RND_GLIDE_PARAM;
        int n = id == FXG::PRESET_PARAM ? kPresetCount + 1 : id == FXG::RND_DIV_PARAM ? kRndDivCount : kRndGlideCount;
        m->params[id].setValue((float)((m->slot(id) + d + n) % n));
        return true;
    }
    void presetMenu() {
        FXG* m = module;
        ui::Menu* menu = createMenu();
        menu->addChild(createMenuLabel("Preset"));
        for (int i = 0; i <= kPresetCount; i++)
            menu->addChild(createCheckMenuItem(GFPresetBar::nameOf(i), "", [=]() { return m->slot(FXG::PRESET_PARAM) == i; }, [=]() {
                if (m->slot(FXG::PRESET_PARAM) == i) m->presetRequest.store(i);   // same one again: reload / re-roll
                else m->params[FXG::PRESET_PARAM].setValue((float)i);
            }));
    }
    void onButton(const ButtonEvent& e) override {
        if (!module || e.action != GLFW_PRESS || (e.button != GLFW_MOUSE_BUTTON_LEFT && e.button != GLFW_MOUSE_BUTTON_RIGHT)) return;
        Vec p = e.pos.div(mm2px(1.f));
        bool right = e.button == GLFW_MOUSE_BUTTON_RIGHT;
        if (right && p.y >= fxRowY(6) && p.y < fxRowY(6) + oled::kCellH && p.x >= kFxCol && p.x < kFxPreSeg[0]) {
            presetMenu();
            e.consume(this);
            return;
        }
        if (stepAt(p, right ? -1 : 1)) e.consume(this);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (!module || e.scrollDelta.y == 0.f) return;
        if (stepAt(e.pos.div(mm2px(1.f)), e.scrollDelta.y > 0.f ? 1 : -1)) e.consume(this);
    }
};

// Throw step cell: white outline while its step plays.
struct FXThrowCell : oled::Cell {
    FXG* gf = NULL;
    int idx = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        oled::Cell::drawLayer(args, layer);
        if (layer != 1 || !gf || gf->step != idx) return;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.2f);
        nvgStrokeColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgStrokeWidth(args.vg, 1.1f);
        nvgStroke(args.vg);
    }
};

// THROW's RND: GRAIN-FORGE's random-pattern button, drawn in the lit layer.
struct FXThrowRoll : GFThrowRoll {
    void draw(const DrawArgs& args) override {}
    void drawLayer(const DrawArgs& args, int layer) override { if (layer == 1) GFThrowRoll::draw(args); }
};

// GRAIN-FORGE's filter SEQ screen (same clicks and painting) without its own box, the TRIG name and value
// on the left at the OLED's label column (labelX, px from this widget's left).
struct FXSeq : GFSeqScreen {
    float labelX = 4.f;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        FXG* m = module;
        float W = box.size.x, H = box.size.y, x0 = barX0();
        int trig = m ? clamp(m->slot(FXG::FLT_TRIG_PARAM), 0, kTrigCount - 1) : TRIG_SEQ;
        bool seqMode = trig == TRIG_SEQ;
        float ev = m ? clamp(m->fltEnvOut, 0.f, 1.f) : 0.f;   // envelope level as a glow strip under TRIG
        nvgBeginPath(args.vg);
        nvgRect(args.vg, labelX, H - 3.f, (x0 - 3.f - labelX) * ev, 1.5f);
        nvgFillColor(args.vg, oled::litFill());
        nvgFill(args.vg);
        if (std::shared_ptr<window::Font> font = APP->window->uiFont) {
            nvgFontFaceId(args.vg, font->handle);
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFontSize(args.vg, 6.f);
            nvgFillColor(args.vg, oled::dim());
            nvgText(args.vg, labelX, H * 0.25f, "TRIG", NULL);
            nvgFontSize(args.vg, 7.5f);
            nvgFillColor(args.vg, oled::amber());
            nvgText(args.vg, labelX, H * 0.6f, kTrigName[trig], NULL);
        }
        float bw = (W - x0 - 2.f) / FXG::kSeqMax;
        int len = m ? m->seqLen : 16, pos = m ? m->seqPos : -1;
        for (int i = 0; i < FXG::kSeqMax; i++) {
            bool on = m ? m->seqOn[i] : i % 2 == 0;
            float lv = m ? m->seqLvl[i] : 0.5f;
            float bx = x0 + i * bw + 0.6f, bh = 1.5f + lv * (H - 4.5f);
            int a = i >= len ? 40 : seqMode ? 255 : 110;   // dim when SEQ isn't the trigger, dimmer past LENGTH
            nvgBeginPath(args.vg);
            nvgRect(args.vg, bx, H - 1.5f - bh, bw - 1.2f, bh);
            if (on) nvgFillColor(args.vg, i == pos && seqMode ? nvgRGBA(255, 240, 200, a) : nvgRGBA(255, 112, 15, a * 9 / 10));
            else nvgFillColor(args.vg, nvgRGBA(255, 180, 90, a / 5));
            nvgFill(args.vg);
            if (i % 4 == 0) {   // beat ticks
                nvgBeginPath(args.vg);
                nvgRect(args.vg, bx, H - 1.2f, bw - 1.2f, 0.6f);
                nvgFillColor(args.vg, nvgRGBA(255, 180, 90, 90));
                nvgFill(args.vg);
            }
        }
    }
};

struct SynTHBloKDuoWidget : ModuleWidget {
    SynTHBloKDuoWidget(DU* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKDuo.svg")));
        using namespace rack::componentlibrary;
        const float PW = 335.28f;   // 66 HP
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PW - 3.5f, 3.5f))));
        DUTitle* title = createWidget<DUTitle>(mm2px(Vec(8.5f, 0.f)));
        title->box.size = mm2px(Vec(60.f, 7.f));
        addChild(title);

        auto label = [&](float cx, float cy, const char* text) {
            JLabel* l = createWidget<JLabel>(mm2px(Vec(cx - 12.f, cy - 2.f)));
            l->box.size = mm2px(Vec(24.f, 4.f)); l->text = text;
            addChild(l);
        };
        // compact: trimpots with a small ring and ticks
        auto knob = [&](float x, float y, int param, const char* name) {
            addChild(KnobTicks::create(Vec(x, y), 0.72f));
            DURing* r = createWidget<DURing>(mm2px(Vec(x - 4.f, y - 4.f)));
            r->box.size = mm2px(Vec(8.f, 8.f)); r->module = module; r->param = param;
            addChild(r);
            addParam(createParamCentered<Trimpot>(mm2px(Vec(x, y)), module, param));
            if (name) label(x, y + 5.6f, name);
        };

        // ---------------- left: the JAM matrix, laid out like MATRIX FORGE ----------------
        auto colX = [](int c) { return 4.9f + 7.4f * c; };
        for (int p = 0; p < 8; p++) {
            DUPatBtn* pb = createWidget<DUPatBtn>(mm2px(Vec(colX(p), 10.5f)));
            pb->box.size = mm2px(Vec(6.6f, 6.6f)); pb->module = module; pb->p = p;
            addChild(pb);
        }
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                DUPad* p = createWidget<DUPad>(mm2px(Vec(colX(c), 19.5f + 7.4f * r)));
                p->box.size = mm2px(Vec(6.6f, 6.6f)); p->module = module; p->r = r; p->c = c;
                addChild(p);
            }
        // row triggers
        label(69.6f, 13.8f, "OUT");
        for (int r = 0; r < 8; r++)
            addOutput(createOutputCentered<SmallJack>(mm2px(Vec(69.6f, 22.8f + 7.4f * r)), module, DU::TRIG_OUTPUT + r));
        // strip CV jacks, banks A..H, touch strips
        for (int i = 0; i < 8; i++) {
            addOutput(createOutputCentered<SmallJack>(mm2px(Vec(colX(i) + 3.3f, 81.6f)), module, DU::STRIP_OUTPUT + i));
            DUBankSq* q = createWidget<DUBankSq>(mm2px(Vec(colX(i), 85.5f)));
            q->box.size = mm2px(Vec(6.6f, 6.6f)); q->module = module; q->b = i;
            addChild(q);
            DUStrip* s = createWidget<DUStrip>(mm2px(Vec(colX(i) + 0.9f, 93.5f)));
            s->box.size = mm2px(Vec(4.8f, 30.f)); s->module = module; s->idx = i;
            addChild(s);
        }
        label(69.6f, 81.6f, "LOOP");
        JLoopArrow<DU>* up = createWidget<JLoopArrow<DU>>(mm2px(Vec(66.1f, 85.5f)));
        up->box.size = mm2px(Vec(7.f, 6.6f)); up->module = module; up->dir = 1;
        addChild(up);
        JLoopDisplay<DU>* ld = createWidget<JLoopDisplay<DU>>(mm2px(Vec(66.1f, 93.5f)));
        ld->box.size = mm2px(Vec(7.f, 22.5f)); ld->module = module;
        addChild(ld);
        JLoopArrow<DU>* dn = createWidget<JLoopArrow<DU>>(mm2px(Vec(66.1f, 117.5f)));
        dn->box.size = mm2px(Vec(7.f, 6.f)); dn->module = module; dn->dir = -1;
        addChild(dn);

        // ---------------- middle: LEAD and BASS ----------------
        for (int e = 0; e < 2; e++) {
            float x0 = 77.6f + 51.f * e, w = 50.f;
            auto cx = [&](int i) { return x0 + w * (i + 0.5f) / 4.f; };
            DUHeader* h = createWidget<DUHeader>(mm2px(Vec(x0 + 1.5f, 10.5f)));
            h->box.size = mm2px(Vec(w - 3.f, 12.f)); h->module = module; h->e = e;
            addChild(h);
            int b = e ? DU::BASS_PARAM : DU::LEAD_PARAM;
            // sound knobs: 4 rows of 4, the last row GLIDE / GATE / OCT spread over the width
            const int P[15] = {DO_WAVE1, DO_WAVE2, DO_FM, DO_SUB, DO_CUTOFF, DO_RES, DO_ENV, DO_DRIVE,
                               DO_ATK, DO_DEC, DO_SUS, DO_REL, DO_GLIDE, DO_GATE, DO_OCT};
            const char* N[15] = {"WAVE 1", "WAVE 2", "FM", "SUB", "CUTOFF", "RES", "ENV", "DRIVE",
                                 "ATK", "DEC", "SUS", "REL", "GLIDE", "GATE", "OCT"};
            for (int i = 0; i < 12; i++) knob(cx(i % 4), 31.f + 17.f * (i / 4), b + P[i], N[i]);
            for (int i = 0; i < 3; i++) knob(x0 + w * (i + 0.5f) / 3.f, 82.f, b + P[12 + i], N[12 + i]);
            // bottom row: TUNE, LEVEL, then this synth's audio, pitch CV and gate
            auto bx = [&](int i) { return x0 + w * (i + 0.5f) / 5.f; };
            knob(bx(0), 116.f, b + DO_TUNE, NULL); label(bx(0), 109.f, "TUNE");
            knob(bx(1), 116.f, b + DO_LEVEL, NULL); label(bx(1), 109.f, "LEVEL");
            const char* JN[3] = {"OUT", "CV", "GATE"};
            const int JO[3] = {e ? DU::BASS_OUTPUT : DU::LEAD_OUTPUT, e ? DU::BASS_CV_OUTPUT : DU::LEAD_CV_OUTPUT,
                               e ? DU::BASS_GATE_OUTPUT : DU::LEAD_GATE_OUTPUT};
            for (int i = 0; i < 3; i++) {
                label(bx(2 + i), 109.f, JN[i]);
                addOutput(createOutputCentered<SmallJack>(mm2px(Vec(bx(2 + i), 116.f)), module, JO[i]));
            }
        }

        // ---------------- far right: master section ----------------
        // same width as LEAD / BASS: one OLED (MIDI, info, KEY | SCALE, buttons), one knob row, one jack row
        const float RX = 283.28f, RW = 50.f, OX = RX + 1.5f, OY = 10.5f, OW = RW - 3.f;
        DUMasterDisplay* md = createWidget<DUMasterDisplay>(mm2px(Vec(OX, OY)));
        md->box.size = mm2px(Vec(OW, kMoBtn + 2.f * kMoBtnH + kMoBtnGap + 1.f)); md->module = module;
        addChild(md);
        struct CellDef { const char* label; int param, setTo; bool latch, green; int jack; };
        const CellDef cells[7] = {
            {"LEAD", DU::VIEW_PARAM, 0, false, false, -1},
            {"BASS", DU::VIEW_PARAM, 1, false, false, -1},
            {"RUN", DU::RUN_PARAM, -1, true, true, DU::RUN_INPUT},
            {"RESET", DU::RESET_PARAM, -1, false, false, DU::RESET_INPUT},
            {"CLEAR", DU::CLEAR_PARAM, -1, false, false, -1},
            {"LEARN", DU::LEARN_PARAM, -1, true, false, -1},
            {"\xC2\xB1" "5V", DU::RANGE_PARAM, -1, true, false, -1}};
        // row 1: LEAD | BASS | RUN | RESET, row 2: CLEAR | LEARN | +-5V
        const float CX = OX + 2.f, CW = OW - 4.f, CG = 1.f;
        for (int i = 0; i < 7; i++) {
            int row = i < 4 ? 0 : 1, n = row ? 3 : 4, k = row ? i - 4 : i;
            float w = (CW - CG * (n - 1)) / n;
            const CellDef& d = cells[i];
            oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(CX + k * (w + CG), OY + kMoBtn + row * (kMoBtnH + kMoBtnGap))), module, d.param);
            c->box.size = mm2px(Vec(w, kMoBtnH));
            c->module = module; c->label = d.label; c->setTo = d.setTo;
            c->latch = d.latch; c->green = d.green; c->jack = d.jack;
            if (module && d.param == DU::RESET_PARAM) c->flash = &module->resetFlash;   // lit on any reset
            addParam(c);
        }
        // master filter, laid out like GRAIN-FORGE's: FILTER, RES, ENV, STG, TYPE; the ADSR + V (env -> level)
        // with stage lights; INPUT, DRY / WET and BYP (env off the filter)
        auto ringKnob = [&](float x, float y, int param) {
            addChild(KnobTicks::create(Vec(x, y)));
            DURing* r = createWidget<DURing>(mm2px(Vec(x - 5.5f, y - 5.5f)));
            r->box.size = mm2px(Vec(11.f, 11.f)); r->module = module; r->param = param;
            addChild(r);
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y)), module, param));
        };
        const float FY = 50.f;
        ringKnob(RX + 7.f, FY, DU::FLT_CUT_PARAM); label(RX + 7.f, FY + 8.f, "FILTER");
        knob(RX + 17.f, FY, DU::FLT_RES_PARAM, NULL); label(RX + 17.f, FY + 8.f, "RES");
        knob(RX + 25.5f, FY, DU::FLT_ENV_PARAM, NULL); label(RX + 25.5f, FY + 8.f, "ENV");
        // STG (1 / 2 / 3) | TYPE (LP / BP / HP) | BYP in one OLED, as tall as FILTER's ring
        const float BX = RX + 30.5f, BW = 18.f, BH = 11.f, BY = FY - BH * 0.5f, PAD = 0.8f, GAP = 0.6f;
        oled::Box* fb = createWidget<oled::Box>(mm2px(Vec(BX, BY)));
        fb->box.size = mm2px(Vec(BW, BH));
        addChild(fb);
        const float colW[3] = {4.6f, 5.8f, BW - 2.f * PAD - 2.f * GAP - 4.6f - 5.8f};
        const float rowH = (BH - 2.f * PAD - 2.f * 0.5f) / 3.f;
        static const char* const fNames[2][3] = {{"1", "2", "3"}, {"LP", "BP", "HP"}};
        float cx = BX + PAD;
        for (int col = 0; col < 3; col++) {
            for (int row = 0; row < (col < 2 ? 3 : 1); row++) {
                oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(cx, BY + PAD + row * (rowH + 0.5f))), module,
                                                col == 0 ? DU::FLT_STAGES_PARAM : col == 1 ? DU::FLT_MODE_PARAM : DU::FLT_BYPASS_PARAM);
                c->box.size = mm2px(Vec(colW[col], col < 2 ? rowH : BH - 2.f * PAD));
                c->module = module; c->fontSize = 6.f;
                if (col < 2) { c->label = fNames[col][row]; c->setTo = row; }
                else { c->label = "BYP"; c->latch = true; }
                addParam(c);
            }
            if (col < 2) label(cx + colW[col] * 0.5f, FY + 8.f, col ? "TYPE" : "STG");
            cx += colW[col] + GAP;
        }
        const int adsr[5] = {DU::FLT_ATK_PARAM, DU::FLT_DEC_PARAM, DU::FLT_SUS_PARAM, DU::FLT_REL_PARAM, DU::FLT_AMP_PARAM};
        const char* an[5] = {"A", "D", "S", "R", "V"};
        const float AY = 66.f;
        for (int i = 0; i < 5; i++) {
            float x = RX + 7.f + i * 9.f;
            addParam(createParamCentered<Trimpot>(mm2px(Vec(x, AY)), module, adsr[i]));
            label(x - 4.6f, AY, an[i]);
            if (i < 4) addChild(createLightCentered<TinyLight<DUAmberLight>>(mm2px(Vec(x, AY + 4.6f)), module, DU::ADSR_LIGHT + i));
        }
        const float IY = 79.f;
        knob(RX + 15.f, IY, DU::FLT_IN_PARAM, NULL); label(RX + 15.f, IY + 7.f, "INPUT");
        ringKnob(RX + 35.f, IY, DU::FLT_DRYWET_PARAM); label(RX + 35.f, IY + 7.f, "DRY / WET");
        // knobs: BPM, LEN, MIX (LEAD <-> BASS), MASTER (the JAM knob on MST / CUE)
        const float KY = 95.f;
        const char* KN[4] = {"BPM", "LEN", "MIX", "MASTER"};
        const int KP[4] = {DU::BPM_PARAM, DU::LEN_PARAM, DU::MIX_PARAM, DU::MASTER_PARAM};
        for (int i = 0; i < 4; i++) {
            float x = RX + RW * (i + 0.5f) / 4.f;
            ringKnob(x, KY, KP[i]);
            label(x, KY + 7.5f, KN[i]);
        }
        // jacks, like the LEAD / BASS bottom rows: CLK / RST / RUN in, KNOB CV, OUT L / R
        const char* JN[6] = {"CLK", "RST", "RUN", "KNOB", "OUT L", "OUT R"};
        for (int i = 0; i < 6; i++) {
            float x = RX + RW * (i + 0.5f) / 6.f;
            label(x, 109.f, JN[i]);
            if (i < 3) addInput(createInputCentered<SmallJack>(mm2px(Vec(x, 116.f)), module,
                                                               i == 0 ? DU::CLOCK_INPUT : i == 1 ? DU::RESET_INPUT : DU::RUN_INPUT));
            else addOutput(createOutputCentered<SmallJack>(mm2px(Vec(x, 116.f)), module,
                                                           i == 3 ? DU::KNOB_OUTPUT : i == 4 ? DU::OUT_L_OUTPUT : DU::OUT_R_OUTPUT));
        }

        // ---------------- right of BASS: FORGE FX ----------------
        // the OLED, then the stage knobs (a row per stage, in the OLED's order, MIX last) and, right of
        // them, the master filter, its ADSR and the levels
        FXG* fm = module ? &module->fx : NULL;
        const float FX0 = 179.6f, FOX = FX0 + 1.5f, FOY = 10.5f;
        FXDisplay* fd = createWidget<FXDisplay>(mm2px(Vec(FOX, FOY)));
        fd->box.size = mm2px(Vec(kFxW, kFxH)); fd->module = fm;
        addChild(fd);
        auto cell = [&](float x, float y, float w, float h, Module* mod, int param, const char* lab) -> oled::Cell* {
            oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(FOX + x, FOY + y)), mod, param);
            c->box.size = mm2px(Vec(w, h)); c->module = mod; c->label = lab;
            addParam(c);
            return c;
        };
        const float CH = oled::kCellH;
        for (int s = 0; s < 5; s++) {
            cell(kFxPad, fxRowY(s), kFxCol - kFxPad - 1.f, CH, fm, kFxStage[s].on, kFxStage[s].name)->latch = true;
            cell(kFxRndX, fxRowY(s), kFxRndW, CH, fm, FXG::RND_PARAM + s, "RND")->latch = true;
        }
        for (int k = 0; k < 3; k++) cell(kFxModeX + k * 6.5f, fxRowY(3), 6.f, CH, fm, FXG::PU_MODE_PARAM, kModeName[k])->setTo = k;
        // throw steps in beats of four
        const float tw = (kFxRndX - 1.f - kFxCol - 15.f * 0.4f - 3.f * 0.8f) / 16.f;
        for (int i = 0; i < FXG::kThrowSteps; i++) {
            FXThrowCell* c = createParam<FXThrowCell>(mm2px(Vec(FOX + kFxCol + i * (tw + 0.4f) + (i / 4) * 0.8f, FOY + fxRowY(5))),
                                                      fm, FXG::throwParam(i));
            c->box.size = mm2px(Vec(tw, CH)); c->module = fm; c->latch = true; c->gf = fm; c->idx = i;
            addParam(c);
        }
        FXThrowRoll* roll = createWidget<FXThrowRoll>(mm2px(Vec(FOX + kFxRndX, FOY + fxRowY(5))));
        roll->box.size = mm2px(Vec(kFxRndW, CH)); roll->module = fm;
        addChild(roll);
        oled::Cell* frz = cell(kFxRndX, fxRowY(6), kFxRndW, CH, fm, FXG::RND_FREEZE_PARAM, "FREEZE");
        frz->latch = true; frz->fontSize = 5.5f;
        // SEQ bars start at the value column, like the throw steps
        const float sw = (kFxRndX - 1.f - kFxCol) / (1.f - GFSeqScreen::kTrigW), sx = kFxRndX - 1.f - sw;
        FXSeq* sq = createWidget<FXSeq>(mm2px(Vec(FOX + sx, FOY + kFxSeqY)));
        sq->box.size = mm2px(Vec(sw, kFxSeqH)); sq->module = fm; sq->labelX = 4.f - mm2px(sx);
        addChild(sq);
        cell(kFxRndX, kFxSeqY, kFxRndW, kFxSeqH, fm, FXG::FLT_BYPASS_PARAM, "BYP")->latch = true;
        static const char* const stgName[3] = {"1", "2", "3"};
        for (int k = 0; k < 3; k++) {
            cell(kFxStgX + k * 5.5f, kFxFltY, 5.f, CH, fm, FXG::FLT_STAGES_PARAM, stgName[k])->setTo = k;
            cell(kFxTypeX + k * 6.5f, kFxFltY, 6.f, CH, fm, FXG::FLT_MODE_PARAM, kModeName[k])->setTo = k;
        }
        oled::Cell* fxOn = cell(kFxRndX, kFxFltY, kFxRndW, CH, module, DU::FX_ON_PARAM, "FX");
        fxOn->latch = true; fxOn->green = true;

        auto fxLabel = [&](float cx, float cy, const char* text) {
            GFLabel* l = createWidget<GFLabel>(mm2px(Vec(cx - 6.f, cy - 2.f)));
            l->box.size = mm2px(Vec(12.f, 4.f)); l->text = text; l->size = 5.5f;
            addChild(l);
        };
        auto fxKnob = [&](float x, float y, int param, const char* name) {
            addChild(KnobTicks::create(Vec(x, y), 0.72f));
            DURing* r = createWidget<DURing>(mm2px(Vec(x - 4.f, y - 4.f)));
            r->box.size = mm2px(Vec(8.f, 8.f)); r->module = fm; r->param = param;
            addChild(r);
            addParam(createParamCentered<Trimpot>(mm2px(Vec(x, y)), fm, param));
            fxLabel(x, y + 5.4f, name);
        };
        auto fxBigKnob = [&](float x, float y, int param, const char* name) {
            addChild(KnobTicks::create(Vec(x, y)));
            DURing* r = createWidget<DURing>(mm2px(Vec(x - 5.5f, y - 5.5f)));
            r->box.size = mm2px(Vec(11.f, 11.f)); r->module = fm; r->param = param;
            addChild(r);
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y)), fm, param));
            fxLabel(x, y + 7.4f, name);
        };
        static const int SP[5][6] = {
            {FXG::SH_PATTERN_PARAM, FXG::SH_AMOUNT_PARAM, FXG::SH_REV_PARAM, FXG::SH_PITCH_PARAM, FXG::SH_LEN_PARAM, FXG::SH_MIX_PARAM},
            {FXG::GL_CHANCE_PARAM, FXG::GL_ROLL_PARAM, FXG::GL_REV_PARAM, FXG::GL_STOP_PARAM, FXG::GL_CRUSH_PARAM, FXG::GL_MIX_PARAM},
            {FXG::RB_ROOT_PARAM, FXG::RB_CHORD_PARAM, FXG::RB_VOWEL_PARAM, FXG::RB_RING_PARAM, FXG::RB_Q_PARAM, FXG::RB_MIX_PARAM},
            {FXG::PU_CUTOFF_PARAM, FXG::PU_RES_PARAM, FXG::PU_DEPTH_PARAM, FXG::PU_PATTERN_PARAM, FXG::PU_DECAY_PARAM, FXG::PU_MIX_PARAM},
            {FXG::DL_TIME_PARAM, FXG::DL_FDBK_PARAM, FXG::DL_WARP_PARAM, FXG::DL_ROUTE_PARAM, FXG::DL_TONE_PARAM, FXG::DL_MIX_PARAM}};
        static const char* const SN[5][6] = {
            {"PATTERN", "AMOUNT", "REVERSE", "OCTAVE", "LENGTH", "MIX"}, {"CHANCE", "ROLL", "REVERSE", "STOP", "CRUSH", "MIX"},
            {"ROOT", "CHORD", "VOWEL", "RING", "RES", "MIX"}, {"CUTOFF", "RES", "DEPTH", "PATTERN", "DECAY", "MIX"},
            {"TIME", "FDBK", "WARP", "ROUTE", "TONE", "ECHO"}};
        for (int s = 0; s < 5; s++)
            for (int k = 0; k < 6; k++) fxKnob(FX0 + 1.2f + 9.6f * (k + 0.5f), 70.8f + 11.7f * s, SP[s][k], SN[s][k]);
        const float M0 = FX0 + 59.6f;
        fxBigKnob(M0 + 7.f, 71.5f, FXG::FILTER_PARAM, "FILTER");
        fxKnob(M0 + 18.5f, 71.5f, FXG::FLT_RES_PARAM, "RES");
        fxKnob(M0 + 27.5f, 71.5f, FXG::FLT_ENV_PARAM, "ENV");
        fxKnob(M0 + 36.5f, 71.5f, FXG::FLT_AMP_PARAM, "AMP");
        const int fxAdsr[4] = {FXG::FLT_ATK_PARAM, FXG::FLT_DEC_PARAM, FXG::FLT_SUS_PARAM, FXG::FLT_REL_PARAM};
        const char* fxAn[4] = {"ATK", "DEC", "SUS", "REL"};
        for (int i = 0; i < 4; i++) fxKnob(M0 + 6.f + 10.f * i, 94.2f, fxAdsr[i], fxAn[i]);
        fxKnob(M0 + 7.f, 116.f, FXG::IN_GAIN_PARAM, "INPUT");
        fxBigKnob(M0 + 21.f, 116.f, FXG::DRYWET_PARAM, "DRY / WET");
        fxKnob(M0 + 35.f, 116.f, FXG::OUT_GAIN_PARAM, "OUTPUT");
    }

    void appendContextMenu(Menu* menu) override {
        DU* m = getModule<DU>();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Copy pattern to the next one", "", [=]() { m->copyToNext(); }));
        menu->addChild(createMenuItem("Clear every pattern", "", [=]() { m->clearAll(); }));
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Maschine JAM (Template 4 Rack, ch 1)"));
        menu->addChild(createIndexSubmenuItem("JAM matrix page", {"1", "2", "3", "4", "5", "6"},
            [=]() { return m->curPage() - 1; },
            [=](size_t i) { m->params[DU::PAGE_PARAM].setValue((float)(i + 1)); }));
        menu->addChild(createBoolMenuItem("Learn page (then press the JAM's top-left pad)", "",
            [=]() { return m->params[DU::LEARN_PARAM].getValue() > 0.5f; },
            [=](bool on) { m->params[DU::LEARN_PARAM].setValue(on ? 1.f : 0.f); }));
        menu->addChild(createIndexSubmenuItem("Strip bank", std::vector<std::string>(kBankLabel, kBankLabel + 8),
            [=]() { return (size_t)m->bank(); },
            [=](size_t i) { m->setBank((int)i); }));
        menu->addChild(createBoolPtrMenuItem("JAM lights match panel colours", "", &m->matchColors));
        menu->addChild(createMenuLabel("Custom JAM LED colours (when not matching)"));
        for (int id : {DU::OFFC_PARAM, DU::ONC_PARAM, DU::HEADC_PARAM, DU::HITC_PARAM, DU::BONC_PARAM, DU::BOFFC_PARAM}) {
            ui::Slider* s = new ui::Slider;
            s->quantity = m->paramQuantities[id];
            s->box.size.x = 220.f;
            menu->addChild(s);
        }
    }
};

} // namespace

Model* modelSynTHBloKDuo = createModel<SynTHBloKDuo, SynTHBloKDuoWidget>("SynTHBloKDuo");
