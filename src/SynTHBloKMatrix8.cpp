// ============================================================================
// SynTHBloK Matrix-8  (JAM groovebox: the SynTHBloK-K JAM sequencer + 8 synth voices)
//  Each grid row is a voice: a step plays it. Strip bank = sound parameter, strip = voice:
//   A pitch, B cutoff, C resonance, D decay, E wave, F level, G pan, H drive.
//  Knob = master volume. Stereo out L/R; trigger/strip/knob CV outs as on the JAM module.
//  Native Instruments Maschine JAM, Controller Editor "Template 4", MIDI channel 1.
//   SEQ   : 8 tracks x 8 steps. pad note = min(127, 12*(PAGE-1) + 56 + col(A..H=0..7) - 8*(row-1)),
//           row 1 = top. LED = same note, velocity = colour index. Scene buttons CC 0..7 pick 1 of 8 patterns.
//   STRIPS: touch strip CC = 48 + 8*bank + strip. 8 CV outs, 8 banks (A..H). Group buttons CC 8..15 = bank.
//  Panel: pattern buttons 1..8 and bank buttons A..H; length, page, bank and LED colours
//  are in the right-click menu. All JAM lights go dark when the module is removed or bypassed.
// ============================================================================
#include "plugin.hpp"
#include "KnobTicks.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "SongLink.hpp"

namespace {

// JAM Indexed-palette values matching the panel's yellow -> deep dark red colours
// (each JAM hue is 4 values dim -> bright: red 4-7, orange 8-11, light orange 12-15,
// warm yellow 16-19, yellow 20-23). Index = pattern 1..8 / strip bank A..H.
static const int kJamBright[8] = {23, 19, 15, 11, 7, 7, 6, 7};   // 8 uses full red so it can be seen
static const int kJamDim[8] = {21, 17, 13, 9, 5, 4, 4, 4};
static const int kJamWhite = 71;

// JAM side and bottom buttons ("JAM Template 4 Rack" numbers). Each one's light is driven
// by sending its CC back: 127 = on, 0 = off.
enum JamButtonCC {
    CC_SONG = 16, CC_CLEAR = 19, CC_DUPLICATE = 20,
    CC_UP = 21, CC_LEFT = 22, CC_RIGHT = 23, CC_DOWN = 24,
    CC_PLAY = 112, CC_REC = 113, CC_PREV = 114, CC_NEXT = 115,
    CC_TEMPO = 116, CC_GRID = 117, CC_SOLO = 118, CC_MUTE = 119
};
static const int kBtnCC[] = {CC_SONG, CC_CLEAR, CC_DUPLICATE, CC_UP, CC_LEFT, CC_RIGHT, CC_DOWN,
                             CC_PLAY, CC_REC, CC_PREV, CC_NEXT, CC_TEMPO, CC_GRID, CC_SOLO, CC_MUTE};
static const int kNumBtn = sizeof(kBtnCC) / sizeof(kBtnCC[0]);
// "JAM Template 1 Live" (the Ableton template) sends the bottom row as Mackie notes instead:
// {note, the CC it stands for}. Its grid sits on page 1 (notes 0-63), so these never clash there.
static const int kBtnNote[][2] = {{91, CC_PREV}, {92, CC_NEXT}, {93, CC_TEMPO}, {94, CC_PLAY}, {95, CC_REC}};
static int btnNoteFor(int cc) {
    for (auto& n : kBtnNote) if (n[1] == cc) return n[0];
    return -1;
}
// JAM knob: turning sends CC 42..45 (one per MST/GRP/IN1/CUE page), push sends CC 46.
// The JAM's level meter lights follow the same CC, so sending the value back shows it there.
static const int kKnobFirstCC = 42, kKnobLastCC = 45, kKnobPushCC = 46;
static const int kKnobLed = 88 + kNumBtn;
static const int kNumLed = kKnobLed + 1;   // 0..63 pads, 64..71 scenes, 72..79 strips, 80..87 groups, buttons, knob

// ---- synth voice ------------------------------------------------------------
enum VoiceParam { VP_PITCH, VP_CUTOFF, VP_RES, VP_DECAY, VP_WAVE, VP_LEVEL, VP_PAN, VP_DRIVE };
static const char* kBankName[8] = {"PITCH", "CUTOFF", "RESO", "DECAY", "WAVE", "LEVEL", "PAN", "DRIVE"};
// Starting strip values for a new module, per bank (row 1 = top voice).
// Pitch: a minor pentatonic run from C2 (bottom row) up; 36 semitones across a full strip.
static const float kDefaultVal[8][8] = {
    {17 / 36.f, 15 / 36.f, 12 / 36.f, 10 / 36.f, 7 / 36.f, 5 / 36.f, 3 / 36.f, 0.f},
    {0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f},
    {0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f},
    {0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {0.75f, 0.75f, 0.75f, 0.75f, 0.75f, 0.75f, 0.75f, 0.75f},
    {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f},
    {0.15f, 0.15f, 0.15f, 0.15f, 0.15f, 0.15f, 0.15f, 0.15f}};

static float polyBlep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}

// Morphing oscillator: saw -> square -> sine as w goes 0 -> 0.5 -> 1 (band-limited edges).
struct M8Osc {
    float phase = 0.f;
    float process(float dt, float w) {
        phase += dt;
        if (phase >= 1.f) phase -= 1.f;
        float saw = 2.f * phase - 1.f - polyBlep(phase, dt);
        float t2 = phase + 0.5f; if (t2 >= 1.f) t2 -= 1.f;
        float sq = (phase < 0.5f ? 1.f : -1.f) + polyBlep(phase, dt) - polyBlep(t2, dt);
        float sine = std::sin(2.f * float(M_PI) * phase);
        return w < 0.5f ? saw + (sq - saw) * (w * 2.f) : sq + (sine - sq) * ((w - 0.5f) * 2.f);
    }
};

// TPT state-variable filter: low-, band- and high-pass from one pass.
struct M8Svf {
    float ic1 = 0.f, ic2 = 0.f;
    void process(float x, float fc, float res, float sr, float& lp, float& bp, float& hp) {
        float g = std::tan(float(M_PI) * std::min(fc, sr * 0.45f) / sr);
        float k = 2.f - 1.9f * res;
        float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        float v3 = x - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

// Shared "filter 2 + envelope" section (SynTHBloK style), the same for all 8 voices.
struct M8Global {
    float cutoff = 1.f, res = 0.f, envAmt = 0.f, mode = 0.f;   // 0..1 each
    float a = 0.f, d = 0.5f, s = 1.f, r = 0.6f;                // 0..1 each
    float gate = 0.125f;                                       // note length in seconds (one clock step)
};

// One voice: two detuned morphing oscillators plus a sine sub an octave down, a slow random
// "analog" drift on pitch and cutoff, resonant low-pass swept by its decay (the strips), then
// the shared filter 2 (LP -> BP -> HP morph) swept by the shared ADSR, drive, and the ADSR on
// the level as well.
struct M8Voice {
    M8Osc oscA, oscB;
    M8Svf f1, f2;
    float subPhase = 0.f, env = 0.f, cut = 0.5f;
    float adsr = 0.f, gateLeft = 0.f;
    int stage = 0;   // 0 idle, 1 attack, 2 decay/sustain, 3 release
    float drift = 0.f, driftTarget = 0.f, driftTimer = 0.f;
    uint32_t rng = 1;
    explicit M8Voice(uint32_t seed = 1) : rng(seed * 2654435761u + 1u) { oscB.phase = 0.37f; }
    float rand01() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) * (1.f / 16777216.f); }
    void trigger(float gate) { env = 1.f; stage = 1; gateLeft = gate; }
    float process(float sr, const float* p, const M8Global& g) {
        // ADSR: attack 1 ms..2 s, decay 5 ms..2 s, release 5 ms..4 s (time constants)
        auto coef = [&](float t) { return 1.f - std::exp(-1.f / (t * sr)); };
        gateLeft -= 1.f / sr;
        if (gateLeft <= 0.f && (stage == 1 || stage == 2)) stage = 3;
        if (stage == 1) {
            adsr += (1.2f - adsr) * coef(0.001f * std::pow(2000.f, g.a));
            if (adsr >= 1.f) { adsr = 1.f; stage = 2; }
        } else if (stage == 2) {
            adsr += (g.s - adsr) * coef(0.005f * std::pow(400.f, g.d));
        } else if (stage == 3) {
            adsr += (0.f - adsr) * coef(0.005f * std::pow(800.f, g.r));
            if (adsr < 1e-4f) { adsr = 0.f; stage = 0; }
        }

        // drift: a new random target every ~0.3-1 s, glided to; +-1 = +-6 cents / +-0.03 cutoff
        driftTimer -= 1.f / sr;
        if (driftTimer <= 0.f) { driftTarget = rand01() * 2.f - 1.f; driftTimer = 0.3f + 0.7f * rand01(); }
        drift += (driftTarget - drift) * (2.f / sr);

        float semis = std::round(p[VP_PITCH] * 36.f) + drift * 0.06f;
        float freq = 65.406f * std::pow(2.f, semis / 12.f);
        const float detune = 1.0041f;   // +-7 cents
        float w = p[VP_WAVE];
        float osc = 0.5f * (oscA.process(std::min(freq * detune / sr, 0.45f), w)
                          + oscB.process(std::min(freq / detune / sr, 0.45f), w));
        subPhase += 0.5f * freq / sr;
        if (subPhase >= 1.f) subPhase -= 1.f;
        osc += 0.35f * std::sin(2.f * float(M_PI) * subPhase);

        // decay time constant 15 ms .. 1.2 s
        float decay = 0.015f * std::pow(80.f, p[VP_DECAY]);
        env *= std::exp(-1.f / (decay * sr));

        // cutoff 40 Hz .. 16 kHz, +2.5 octaves of envelope sweep; smoothed against zipper noise
        cut += (p[VP_CUTOFF] + drift * 0.03f - cut) * 0.002f;
        float lp, bp, hp;
        f1.process(osc, 40.f * std::pow(400.f, cut) * std::pow(2.f, 2.5f * env), p[VP_RES], sr, lp, bp, hp);

        // filter 2: cutoff 20 Hz..20 kHz, envelope adds up to +6 octaves
        float fc2 = 20.f * std::pow(1000.f, g.cutoff) * std::pow(2.f, 6.f * g.envAmt * adsr);
        float lp2, bp2, hp2;
        f2.process(lp, fc2, g.res, sr, lp2, bp2, hp2);
        float y = g.mode < 0.5f ? lp2 + (bp2 - lp2) * (g.mode * 2.f) : bp2 + (hp2 - bp2) * ((g.mode - 0.5f) * 2.f);

        float drive = 1.f + 6.f * p[VP_DRIVE];
        return std::tanh(y * drive) / std::tanh(drive) * env * adsr * p[VP_LEVEL];
    }
};

// Stereo ping-pong delay: left feeds right and back, a low-pass in the loop darkens each repeat.
struct M8Delay {
    std::vector<float> bl, br;
    int pos = 0;
    float lpL = 0.f, lpR = 0.f, time = 0.375f;
    void init(float sr) { bl.assign((size_t)(sr * 2.1f), 0.f); br.assign(bl.size(), 0.f); pos = 0; }
    void process(float inL, float inR, float sr, float targetTime, float& outL, float& outR) {
        if (bl.empty()) init(sr);
        time += (clamp(targetTime, 0.02f, 2.f) - time) * 0.0002f;   // glide when the tempo changes
        int n = (int)bl.size();
        float d = time * sr;
        int i0 = (int)d; float f = d - i0;
        auto tap = [&](std::vector<float>& b) {
            int a = (pos - i0 + n) % n, c = (pos - i0 - 1 + n) % n;
            return b[a] + (b[c] - b[a]) * f;
        };
        float dl = tap(bl), dr = tap(br);
        lpL += (dl - lpL) * 0.35f;
        lpR += (dr - lpR) * 0.35f;
        bl[pos] = inL + lpR * 0.5f;          // ping-pong: each side feeds the other
        br[pos] = inR + lpL * 0.5f;
        pos = (pos + 1) % n;
        outL = dl; outR = dr;
    }
};

// Small stereo reverb (Freeverb-style: 4 damped combs + 2 allpasses per side).
struct M8Reverb {
    struct Comb { std::vector<float> b; int i = 0; float lp = 0.f;
        float process(float x, float fb, float damp) { float y = b[i]; lp = y + (lp - y) * damp; b[i] = x + lp * fb; i = (i + 1) % (int)b.size(); return y; } };
    struct AllPass { std::vector<float> b; int i = 0;
        float process(float x) { float y = b[i]; b[i] = x + y * 0.5f; i = (i + 1) % (int)b.size(); return y - x; } };
    Comb cl[4], cr[4];
    AllPass al[2], ar[2];
    bool ready = false;
    void init(float sr) {
        static const int combs[4] = {1116, 1188, 1277, 1356}, aps[2] = {556, 441};
        float s = sr / 44100.f;
        for (int k = 0; k < 4; k++) { cl[k].b.assign((size_t)(combs[k] * s), 0.f); cr[k].b.assign((size_t)((combs[k] + 23) * s), 0.f); }
        for (int k = 0; k < 2; k++) { al[k].b.assign((size_t)(aps[k] * s), 0.f); ar[k].b.assign((size_t)((aps[k] + 23) * s), 0.f); }
        ready = true;
    }
    void process(float inL, float inR, float sr, float& outL, float& outR) {
        if (!ready) init(sr);
        float in = (inL + inR) * 0.15f, l = 0.f, r = 0.f;
        for (int k = 0; k < 4; k++) { l += cl[k].process(in, 0.84f, 0.3f); r += cr[k].process(in, 0.84f, 0.3f); }
        for (int k = 0; k < 2; k++) { l = al[k].process(l); r = ar[k].process(r); }
        outL = l; outR = r;
    }
};

struct SynTHBloKMatrix8 : Module, songlink::Member {
    enum ParamIds { LEN_PARAM, PAT_PARAM, PAGE_PARAM, OFFC_PARAM, ONC_PARAM, HEADC_PARAM, HITC_PARAM, CLEAR_PARAM, LEARN_PARAM,
                    BANK_PARAM, RANGE_PARAM, BONC_PARAM, BOFFC_PARAM, RUN_PARAM, RESET_PARAM, KNOB_PARAM,
                    DELAY_PARAM, SPACE_PARAM,
                    F2_CUTOFF_PARAM, F2_RES_PARAM, F2_ENV_PARAM, F2_MODE_PARAM,
                    ENV_A_PARAM, ENV_D_PARAM, ENV_S_PARAM, ENV_R_PARAM, NUM_PARAMS };
    enum InputIds { CLOCK_INPUT, RESET_INPUT, RUN_INPUT, NUM_INPUTS };
    enum OutputIds { TRIG_OUTPUT, STRIP_OUTPUT = TRIG_OUTPUT + 8, KNOB_OUTPUT = STRIP_OUTPUT + 8,
                     AUDIO_L_OUTPUT, AUDIO_R_OUTPUT, NUM_OUTPUTS };
    enum LightIds { NUM_LIGHTS };

    midi::InputQueue midiIn;
    midi::Output midiOut;

    bool pat[8][8][8];
    float val[8][8];
    float sm[8];
    int step = -1;
    bool pendingReset = false;
    dsp::SchmittTrigger clkTrig, rstTrig, rstBtnTrig, runTrig, clrTrig;
    dsp::PulseGenerator pulse[8];

    // Shown on the panel: RUN/RESET light from their jacks when patched, clock tempo on the display.
    bool running = true;
    float resetFlash = 0.f;
    float sinceClock = 1e9f, clockPeriod = 0.f;

    // Track (row) mute/solo, set by holding MUTE or SOLO on the JAM and pressing a pad in that row.
    bool mute[8] = {}, solo[8] = {};
    bool muteHeld = false, soloHeld = false, jamReset = false;
    int knobCC = kKnobFirstCC;    // knob page the JAM last sent from, for the value sent back
    float knobSm = 0.f;
    M8Voice voice[8] = {M8Voice(1), M8Voice(2), M8Voice(3), M8Voice(4), M8Voice(5), M8Voice(6), M8Voice(7), M8Voice(8)};
    M8Delay delay;
    M8Reverb reverb;
    float delayMix = 0.f, spaceMix = 0.f;

    // JAM knob pages: MST = master, GRP = delay, IN1 = space, CUE = master.
    int knobParam() {
        switch (knobCC) {
            case kKnobFirstCC + 1: return DELAY_PARAM;
            case kKnobFirstCC + 2: return SPACE_PARAM;
            default: return KNOB_PARAM;
        }
    }
    float master = 0.8f;

    int lastLed[kNumLed];
    bool matchColors = true;      // JAM lights use the panel colours; off = the LED colour sliders
    int ledScan = 0, lastBank = -1, lastPage = -1;
    int stripPage = 0, lastStripPage = -1;   // JAM touch strip page the strips last sent from
    float tokens = 4.f, resync = 0.f;

    SynTHBloKMatrix8() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        songlink::join(this, this, "MTX-8", 8, false);
        configParam(LEN_PARAM, 1.f, 8.f, 8.f, "Pattern length (steps)");
        configParam(PAT_PARAM, 1.f, 8.f, 1.f, "Pattern");
        configParam(PAGE_PARAM, 1.f, 6.f, 1.f, "JAM matrix page (must match the JAM)");
        configParam(OFFC_PARAM, 0.f, 127.f, 0.f, "LED colour: empty step");
        configParam(ONC_PARAM, 0.f, 127.f, 127.f, "LED colour: active step");
        configParam(HEADC_PARAM, 0.f, 127.f, 30.f, "LED colour: playhead over empty step");
        configParam(HITC_PARAM, 0.f, 127.f, 90.f, "LED colour: playhead over active step");
        configButton(CLEAR_PARAM, "Clear current pattern");
        configSwitch(LEARN_PARAM, 0.f, 1.f, 0.f, "Learn page: then press the TOP-LEFT pad on the JAM", {"Off", "Armed"});
        configParam(BANK_PARAM, 1.f, 8.f, 1.f, "Strip bank (A..H)");
        configSwitch(RANGE_PARAM, 0.f, 1.f, 0.f, "Strip output range", {"0..10 V", "-5..+5 V"});
        configParam(BONC_PARAM, 0.f, 127.f, 127.f, "LED colour: selected bank button");
        configParam(BOFFC_PARAM, 0.f, 127.f, 20.f, "LED colour: other bank buttons");
        configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run (a trigger at the RUN jack toggles it)", {"Stopped", "Running"});
        configButton(RESET_PARAM, "Reset (a trigger at the RESET jack also resets)");
        configParam(KNOB_PARAM, 0.f, 1.f, 0.8f, "Master volume (JAM knob on MST or CUE)", "%", 0.f, 100.f);
        configParam(DELAY_PARAM, 0.f, 1.f, 0.f, "Delay, ping-pong dotted 1/8 (JAM knob on GRP)", "%", 0.f, 100.f);
        configParam(SPACE_PARAM, 0.f, 1.f, 0.f, "Space / reverb (JAM knob on IN1)", "%", 0.f, 100.f);
        configParam(F2_CUTOFF_PARAM, 0.f, 1.f, 1.f, "Filter 2 cutoff", " Hz", 1000.f, 20.f);
        configParam(F2_RES_PARAM, 0.f, 1.f, 0.f, "Filter 2 resonance", "%", 0.f, 100.f);
        configParam(F2_ENV_PARAM, 0.f, 1.f, 0.f, "Filter 2 envelope amount", "%", 0.f, 100.f);
        configParam(F2_MODE_PARAM, 0.f, 1.f, 0.f, "Filter 2 mode (low-pass -> band-pass -> high-pass)", "%", 0.f, 100.f);
        configParam(ENV_A_PARAM, 0.f, 1.f, 0.f, "Envelope attack", " ms", 2000.f, 1.f);
        configParam(ENV_D_PARAM, 0.f, 1.f, 0.5f, "Envelope decay", " ms", 400.f, 5.f);
        configParam(ENV_S_PARAM, 0.f, 1.f, 1.f, "Envelope sustain", "%", 0.f, 100.f);
        configParam(ENV_R_PARAM, 0.f, 1.f, 0.6f, "Envelope release", " ms", 800.f, 5.f);
        for (int i = 0; i < CLEAR_PARAM; i++) paramQuantities[i]->snapEnabled = true;
        paramQuantities[BANK_PARAM]->snapEnabled = true;
        paramQuantities[BONC_PARAM]->snapEnabled = true;
        paramQuantities[BOFFC_PARAM]->snapEnabled = true;
        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset (trigger: same as pressing RESET)");
        configInput(RUN_INPUT, "Run (trigger: toggles RUN, like pressing it)");
        for (int r = 0; r < 8; r++) configOutput(TRIG_OUTPUT + r, "Track " + std::to_string(r + 1) + " trigger");
        for (int i = 0; i < 8; i++) { configOutput(STRIP_OUTPUT + i, "Strip " + std::to_string(i + 1)); sm[i] = 0.f; }
        configOutput(KNOB_OUTPUT, "Knob (0..10 V)");
        configOutput(AUDIO_L_OUTPUT, "Audio left");
        configOutput(AUDIO_R_OUTPUT, "Audio right");
        for (int p = 0; p < 8; p++) for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) pat[p][r][c] = false;
        for (int b = 0; b < 8; b++) for (int i = 0; i < 8; i++) val[b][i] = kDefaultVal[b][i];
        for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
    }

    bool anySolo() { for (int r = 0; r < 8; r++) if (solo[r]) return true; return false; }
    bool anyMute() { for (int r = 0; r < 8; r++) if (mute[r]) return true; return false; }
    bool trackOn(int r) { return !mute[r] && (!anySolo() || solo[r]); }
    void clearPattern(int p) { for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) pat[p][r][c] = false; }
    void stepParam(int id, int delta, int lo, int hi, bool wrap) {
        int v = (int)params[id].getValue() + delta;
        if (wrap) v = lo + ((v - lo) % (hi - lo + 1) + (hi - lo + 1)) % (hi - lo + 1);
        params[id].setValue((float)clamp(v, lo, hi));
    }

    // A JAM side/bottom button was pressed (b > 0) or released (b == 0).
    void jamButton(int cc, int b) {
        bool down = b > 0;
        if (cc == CC_SOLO) { soloHeld = down; return; }
        if (cc == CC_MUTE) { muteHeld = down; return; }
        if (!down) return;
        switch (cc) {
            case CC_SONG: for (int p = 0; p < 8; p++) clearPattern(p); break;
            case CC_CLEAR: clearPattern(curPat()); break;
            case CC_DUPLICATE: {
                int from = curPat(), to = (from + 1) % 8;
                for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) pat[to][r][c] = pat[from][r][c];
                params[PAT_PARAM].setValue((float)(to + 1));
            } break;
            case CC_UP: stepParam(LEN_PARAM, 1, 1, 8, false); break;
            case CC_DOWN: stepParam(LEN_PARAM, -1, 1, 8, false); break;
            case CC_LEFT: stepParam(BANK_PARAM, -1, 1, 8, false); break;
            case CC_RIGHT: stepParam(BANK_PARAM, 1, 1, 8, false); break;
            case CC_PLAY: params[RUN_PARAM].setValue(params[RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f); break;
            case CC_REC: params[LEARN_PARAM].setValue(params[LEARN_PARAM].getValue() > 0.5f ? 0.f : 1.f); break;
            case CC_PREV: stepParam(PAT_PARAM, -1, 1, 8, true); break;
            case CC_NEXT: stepParam(PAT_PARAM, 1, 1, 8, true); break;
            case CC_TEMPO: jamReset = true; break;
            case CC_GRID: params[RANGE_PARAM].setValue(params[RANGE_PARAM].getValue() > 0.5f ? 0.f : 1.f); break;
        }
    }

    int curPat() { return clamp((int)params[PAT_PARAM].getValue(), 1, 8) - 1; }
    int songPatternNow() override { return curPat(); }
    void songSetPattern(int p) override { params[PAT_PARAM].setValue((float)(clamp(p, 0, 7) + 1)); }
    int curPage() { return clamp((int)params[PAGE_PARAM].getValue(), 1, 6); }
    int bank() { return clamp((int)params[BANK_PARAM].getValue(), 1, 8) - 1; }
    int padNote(int r, int c) { return std::min(127, 12 * (curPage() - 1) + 56 + c - 8 * r); }
    void togglePad(int r, int c) { bool& b = pat[curPat()][r][c]; b = !b; }

    void onReset() override { step = -1; }

    void sendNote(int note, int vel) { midi::Message m; m.bytes = {0x90, (uint8_t)note, (uint8_t)clamp(vel, 0, 127)}; midiOut.sendMessage(m); }
    void sendCC(int cc, int v) { midi::Message m; m.bytes = {0xB0, (uint8_t)cc, (uint8_t)clamp(v, 0, 127)}; midiOut.sendMessage(m); }

    void clearLeds() {
        for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) sendNote(padNote(r, c), 0);
        for (int i = 0; i < 8; i++) { sendCC(i, 0); sendCC(8 + i, 0); sendCC(48 + 8 * stripPage + i, 0); }
        for (int i = 0; i < kNumBtn; i++) sendCC(kBtnCC[i], 0);
        for (auto& n : kBtnNote) if (n[0] > padNote(0, 7)) sendNote(n[0], 0);
        for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
    }
    void onRemove(const RemoveEvent& e) override { clearLeds(); }
    void onBypass(const BypassEvent& e) override { clearLeds(); }
    void onUnBypass(const UnBypassEvent& e) override { for (int i = 0; i < kNumLed; i++) lastLed[i] = -1; resync = 0.f; }
    ~SynTHBloKMatrix8() { songlink::leave(this); clearLeds(); }

    int desiredLed(int idx, bool running) {
        if (idx == kKnobLed) return (int)std::round(params[knobParam()].getValue() * 127.f);
        if (idx >= 88) {
            switch (kBtnCC[idx - 88]) {
                case CC_PLAY: return running ? 127 : 0;
                case CC_REC: return params[LEARN_PARAM].getValue() > 0.5f ? 127 : 0;
                case CC_TEMPO: return resetFlash > 0.f ? 127 : 0;
                case CC_GRID: return params[RANGE_PARAM].getValue() > 0.5f ? 127 : 0;
                case CC_SOLO: return soloHeld || anySolo() ? 127 : 0;
                case CC_MUTE: return muteHeld || anyMute() ? 127 : 0;
                default: return 127;   // always available
            }
        }
        if (matchColors && (idx < 72 || idx >= 80)) {
            if (idx < 64) {
                // All pads take the loaded page's colour: its off-state (dim) = empty, bright = hit.
                // Muted tracks: hits dim, empty pads dark.
                int r = idx / 8, c = idx % 8, p = curPat();
                bool a = pat[p][r][c];
                if (!trackOn(r)) return a ? kJamDim[p] : 0;
                if (running && step >= 0 && c == step) return a ? kJamWhite : kJamWhite - 2;
                return a ? kJamBright[p] : kJamDim[p];
            }
            int i = idx < 72 ? idx - 64 : idx - 80;
            bool sel = idx < 72 ? i == curPat() : i == bank();
            return sel ? kJamBright[i] : kJamDim[i];
        }
        if (idx < 72) {
            int on = (int)params[ONC_PARAM].getValue(), off = (int)params[OFFC_PARAM].getValue();
            int head = (int)params[HEADC_PARAM].getValue(), hit = (int)params[HITC_PARAM].getValue();
            if (idx < 64) {
                int r = idx / 8, c = idx % 8;
                bool a = pat[curPat()][r][c];
                bool playing = running && step >= 0 && c == step;
                if (playing) return a ? hit : head;
                return a ? on : off;
            }
            return (idx - 64) == curPat() ? on : off;
        }
        if (idx < 80) return (int)std::round(val[bank()][idx - 72] * 127.f);
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
            // Same light for the Template 1 Live note, unless that note is a pad on this matrix page.
            int note = btnNoteFor(cc);
            if (note >= 0 && note > padNote(0, 7)) sendNote(note, d);
        }
    }

    void process(const ProcessArgs& args) override {
        const float songG = songPoll(args.sampleTime);   // MATRIX-SONG: its pattern requests, and its mute on the audio and triggers
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
                    if (padNote(r, c) == a) {
                        if (muteHeld) mute[r] = !mute[r];
                        else if (soloHeld) solo[r] = !solo[r];
                        else togglePad(r, c);
                        pad = true; break;
                    }
                if (!pad)
                    for (auto& n : kBtnNote) if (n[0] == a) jamButton(n[1], b);
            } else if (st == 0xB0) {
                bool isBtn = false;
                for (int i = 0; i < kNumBtn; i++) if (kBtnCC[i] == a) isBtn = true;
                if (isBtn) jamButton(a, b);
                else if (a >= kKnobFirstCC && a <= kKnobLastCC) {
                    knobCC = a;
                    params[knobParam()].setValue(b / 127.f);
                    lastLed[kKnobLed] = b;   // already showing on the JAM
                }
                else if (a == kKnobPushCC && b > 0) paramQuantities[knobParam()]->reset();   // back to its default
                else if (a >= 0 && a < 8 && b > 0) params[PAT_PARAM].setValue((float)(a + 1));
                else if (a >= 8 && a < 16 && b > 0) params[BANK_PARAM].setValue((float)(a - 8 + 1));
                else if (a >= 48 && a < 112) {
                    // Strips always write into the bank picked with A..H; whichever JAM strip page
                    // they came from only decides where the strip LEDs are sent back to.
                    stripPage = (a - 48) / 8;
                    val[bank()][(a - 48) % 8] = b / 127.f;
                }
            }
        }

        // A patched jack overrides its button.
        // RUN and RESET jacks act like pressing their buttons: a trigger toggles RUN, or fires RESET.
        if (runTrig.process(inputs[RUN_INPUT].getVoltage()))
            params[RUN_PARAM].setValue(params[RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f);
        running = params[RUN_PARAM].getValue() > 0.5f;
        bool rst = rstTrig.process(inputs[RESET_INPUT].getVoltage()) | rstBtnTrig.process(params[RESET_PARAM].getValue()) | jamReset;
        jamReset = false;
        if (rst) { step = -1; pendingReset = true; resetFlash = 0.15f; }
        resetFlash = std::max(0.f, resetFlash - args.sampleTime);
        if (clrTrig.process(params[CLEAR_PARAM].getValue())) clearPattern(curPat());
        bool clk = clkTrig.process(inputs[CLOCK_INPUT].getVoltage());
        sinceClock += args.sampleTime;
        if (clk) { if (sinceClock < 4.f) clockPeriod = sinceClock; sinceClock = 0.f; }
        if (clk && running) {
            int len = clamp((int)params[LEN_PARAM].getValue(), 1, 8);
            step = (pendingReset || step < 0) ? 0 : (step + 1) % len;
            pendingReset = false;
            for (int r = 0; r < 8; r++)
                if (pat[curPat()][r][step] && trackOn(r) && !songMuted.load()) {
                    pulse[r].trigger(0.01f);
                    voice[r].trigger(clockPeriod > 0.f && clockPeriod < 2.f ? clockPeriod : 0.125f);   // held for one step
                }
        }
        for (int r = 0; r < 8; r++) outputs[TRIG_OUTPUT + r].setVoltage(pulse[r].process(args.sampleTime) ? 10.f : 0.f);

        float k = 1.f - std::exp(-args.sampleTime * 1000.f);
        bool bip = params[RANGE_PARAM].getValue() > 0.5f;
        for (int i = 0; i < 8; i++) {
            float t = val[bank()][i] * 10.f - (bip ? 5.f : 0.f);
            sm[i] += (t - sm[i]) * k;
            outputs[STRIP_OUTPUT + i].setVoltage(sm[i]);
        }
        knobSm += (params[KNOB_PARAM].getValue() * 10.f - knobSm) * k;
        outputs[KNOB_OUTPUT].setVoltage(knobSm);

        // Voices: strip bank b, strip r = parameter b of voice r.
        if (outputs[AUDIO_L_OUTPUT].isConnected() || outputs[AUDIO_R_OUTPUT].isConnected()) {
            float l = 0.f, r = 0.f;
            M8Global gs;
            gs.cutoff = params[F2_CUTOFF_PARAM].getValue();
            gs.res = params[F2_RES_PARAM].getValue();
            gs.envAmt = params[F2_ENV_PARAM].getValue();
            gs.mode = params[F2_MODE_PARAM].getValue();
            gs.a = params[ENV_A_PARAM].getValue();
            gs.d = params[ENV_D_PARAM].getValue();
            gs.s = params[ENV_S_PARAM].getValue();
            gs.r = params[ENV_R_PARAM].getValue();
            for (int v = 0; v < 8; v++) {
                float p[8];
                for (int b = 0; b < 8; b++) p[b] = val[b][v];
                float s = voice[v].process(args.sampleRate, p, gs);
                float pan = p[VP_PAN] * float(M_PI) * 0.5f;
                l += s * std::cos(pan);
                r += s * std::sin(pan);
            }
            // Delay locks to the clock: dotted 1/8 = 3 clock pulses at a 16th-note clock.
            float delayTime = (clockPeriod > 0.f && sinceClock < 2.f) ? clockPeriod * 3.f : 0.375f;
            float dl, dr, rl, rr;
            delay.process(l, r, args.sampleRate, delayTime, dl, dr);
            delayMix += (params[DELAY_PARAM].getValue() - delayMix) * k;
            l += dl * delayMix * 0.8f;
            r += dr * delayMix * 0.8f;
            reverb.process(l, r, args.sampleRate, rl, rr);
            spaceMix += (params[SPACE_PARAM].getValue() - spaceMix) * k;
            l += rl * spaceMix;
            r += rr * spaceMix;
            master += (params[KNOB_PARAM].getValue() - master) * k;
            outputs[AUDIO_L_OUTPUT].setVoltage(5.f * std::tanh(l * 0.5f * master) * songG);
            outputs[AUDIO_R_OUTPUT].setVoltage(5.f * std::tanh(r * 0.5f * master) * songG);
        }
        songMeter(outputs[AUDIO_L_OUTPUT].getVoltage(), outputs[AUDIO_R_OUTPUT].getVoltage());

        resync += args.sampleTime;
        // A new bank (or strip page) resends everything, so the strip LEDs show that bank's stored values.
        if (curPage() != lastPage || bank() != lastBank || stripPage != lastStripPage || resync > 3.f) {
            for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
            lastPage = curPage(); lastBank = bank(); lastStripPage = stripPage; resync = 0.f;
        }
        tokens = std::min(16.f, tokens + args.sampleTime * 2000.f);
        for (int n = 0; n < 3; n++) {
            int d = desiredLed(ledScan, running);
            if (d != lastLed[ledScan]) {
                if (tokens < 1.f) break;
                tokens -= 1.f;
                sendLed(ledScan, d);
                lastLed[ledScan] = d;
            }
            ledScan = (ledScan + 1) % kNumLed;
        }
    }

    json_t* dataToJson() override {
        json_t* r = json_object();
        json_t* arr = json_array();
        for (int p = 0; p < 8; p++) {
            std::string s;
            for (int i = 0; i < 64; i++) s += pat[p][i / 8][i % 8] ? '1' : '0';
            json_array_append_new(arr, json_string(s.c_str()));
        }
        json_object_set_new(r, "patterns", arr);
        json_t* va = json_array();
        for (int b = 0; b < 8; b++) for (int i = 0; i < 8; i++) json_array_append_new(va, json_real(val[b][i]));
        json_object_set_new(r, "values", va);
        json_object_set_new(r, "matchColors", json_boolean(matchColors));
        std::string ms, ss;
        for (int i = 0; i < 8; i++) { ms += mute[i] ? '1' : '0'; ss += solo[i] ? '1' : '0'; }
        json_object_set_new(r, "mute", json_string(ms.c_str()));
        json_object_set_new(r, "solo", json_string(ss.c_str()));
        json_object_set_new(r, "in", midiIn.toJson());
        json_object_set_new(r, "out", midiOut.toJson());
        return r;
    }
    void dataFromJson(json_t* r) override {
        if (json_t* arr = json_object_get(r, "patterns"))
            for (int p = 0; p < 8; p++)
                if (json_t* j = json_array_get(arr, p)) {
                    const char* s = json_string_value(j);
                    if (s && strlen(s) >= 64) for (int i = 0; i < 64; i++) pat[p][i / 8][i % 8] = s[i] == '1';
                }
        if (json_t* va = json_object_get(r, "values"))
            for (int i = 0; i < 64; i++)
                if (json_t* j = json_array_get(va, i)) val[i / 8][i % 8] = (float)json_number_value(j);
        if (json_t* j = json_object_get(r, "matchColors")) matchColors = json_boolean_value(j);
        const char* ms = json_string_value(json_object_get(r, "mute"));
        const char* ss = json_string_value(json_object_get(r, "solo"));
        for (int i = 0; i < 8; i++) {
            mute[i] = ms && strlen(ms) >= 8 && ms[i] == '1';
            solo[i] = ss && strlen(ss) >= 8 && ss[i] == '1';
        }
        if (json_t* j = json_object_get(r, "in")) midiIn.fromJson(j);
        if (json_t* j = json_object_get(r, "out")) midiOut.fromJson(j);
    }
};

// Yellow -> deep dark red, one colour per pattern 1..8 and per strip bank A..H
// (same order as the JAM scene row).
static const NVGcolor kPatColor[8] = {
    nvgRGB(0xff, 0xd7, 0x14), nvgRGB(0xff, 0xb0, 0x00), nvgRGB(0xff, 0x8c, 0x00), nvgRGB(0xff, 0x6a, 0x00),
    nvgRGB(0xf0, 0x40, 0x00), nvgRGB(0xcc, 0x1a, 0x00), nvgRGB(0x99, 0x00, 0x00), nvgRGB(0x5c, 0x00, 0x00)};
// On state (selected button, pad hit, strip fill): same as kPatColor, except 7 and 8 use
// shades of page 6's red instead of the deep reds, bright enough to read when lit.
static const NVGcolor kPatOnColor[8] = {
    nvgRGB(0xff, 0xd7, 0x14), nvgRGB(0xff, 0xb0, 0x00), nvgRGB(0xff, 0x8c, 0x00), nvgRGB(0xff, 0x6a, 0x00),
    nvgRGB(0xf0, 0x40, 0x00), nvgRGB(0xcc, 0x1a, 0x00), nvgRGB(0xb3, 0x17, 0x00), nvgRGB(0xc2, 0x1a, 0x00)};
static const NVGcolor kIdle = nvgRGB(0x1c, 0x1c, 0x1c);

// Selected = filled in its on colour with a dark label; others = outline and label in its colour.
static void drawColorButton(const widget::Widget::DrawArgs& args, Vec size, int i, bool sel, char ch, float fontSize) {
    NVGcolor col = sel ? kPatOnColor[i] : kPatColor[i];
    nvgBeginPath(args.vg);
    nvgRoundedRect(args.vg, 0.f, 0.f, size.x, size.y, 1.4f);
    nvgFillColor(args.vg, sel ? col : kIdle);
    nvgFill(args.vg);
    nvgStrokeColor(args.vg, col);
    nvgStrokeWidth(args.vg, sel ? 1.2f : 0.8f);
    nvgStroke(args.vg);
    std::shared_ptr<window::Font> font = APP->window->uiFont;
    if (!ch || !font) return;
    char label[2] = {ch, 0};
    nvgFontFaceId(args.vg, font->handle);
    nvgFontSize(args.vg, fontSize);
    nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(args.vg, sel ? nvgRGB(0x10, 0x10, 0x10) : col);
    nvgText(args.vg, size.x * 0.5f, size.y * 0.5f, label, NULL);
}

struct JSPad : OpaqueWidget {
    SynTHBloKMatrix8* module = NULL;
    int r = 0, c = 0;
    JSPad() { box.size = mm2px(Vec(6.6f, 6.6f)); }
    void draw(const DrawArgs& args) override {
        bool on = module ? module->pat[module->curPat()][r][c] : ((r == 0 && (c % 4) == 0) || (r == 1 && c == 4) || (r == 3 && (c % 2) == 1));
        bool playing = module && module->step >= 0 && module->step == c;
        // All pads take the loaded page's colour; empty pads look like that page button's
        // off state (dark fill, coloured outline), hits are filled.
        int p = module ? module->curPat() : 0;
        NVGcolor col = kPatColor[p], onCol = kPatOnColor[p];
        if (module && !module->trackOn(r)) {   // muted track: faded
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

struct JSTinyBtn : app::ParamWidget {
    bool latch = false;
    JSTinyBtn() { box.size = mm2px(Vec(4.4f, 4.4f)); }
    void draw(const DrawArgs& args) override {
        ParamQuantity* pq = getParamQuantity();
        bool on = pq && pq->getValue() > 0.5f;
        float cx = box.size.x * 0.5f, cy = box.size.y * 0.5f;
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, mm2px(1.9f));
        nvgFillColor(args.vg, nvgRGB(0x1a, 0x1a, 0x1a));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGBA(0xff, 0x7b, 0x00, 0xe0));
        nvgStrokeWidth(args.vg, 1.f);
        nvgStroke(args.vg);
        if (on) {
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, cx, cy, mm2px(1.15f));
            nvgFillColor(args.vg, nvgRGB(0xff, 0x7a, 0x00));
            nvgFill(args.vg);
        }
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (ParamQuantity* pq = getParamQuantity()) pq->setValue(latch ? (pq->getValue() > 0.5f ? 0.f : 1.f) : 1.f);
            e.consume(this);
            return;
        }
        ParamWidget::onButton(e);
    }
    void onDragStart(const DragStartEvent& e) override {}
    void onDragEnd(const DragEndEvent& e) override {
        if (!latch && e.button == GLFW_MOUSE_BUTTON_LEFT)
            if (ParamQuantity* pq = getParamQuantity()) pq->setValue(0.f);
    }
};

// MIDI IN and OUT in one display, in the SynTHBloK MIDI + amber style.
// Click the top half to pick the input port, the bottom half for the output port.
struct JMidiDisplay : TransparentWidget {
    midi::Port* in = NULL;
    midi::Port* out = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.f);
        nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, 5.f, H * 0.5f);
        nvgLineTo(args.vg, W - 5.f, H * 0.5f);
        nvgStrokeWidth(args.vg, 0.6f);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 40));
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (font) {
            nvgScissor(args.vg, 0, 0, W, H);
            nvgFontFaceId(args.vg, font->handle);
            drawPort(args, in, "MIDI IN", 0.f, W, H * 0.5f);
            drawPort(args, out, "MIDI OUT", H * 0.5f, W, H * 0.5f);
            nvgResetScissor(args.vg);
        }
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.5f, 0.5f, W - 1.f, H - 1.f, 2.f);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 80));
        nvgStroke(args.vg);
    }
    void drawPort(const DrawArgs& args, midi::Port* port, const char* title, float y, float W, float h) {
        std::string drv = "", dev = "(no device)";
        if (port) {
            if (midi::Driver* d = port->getDriver()) drv = d->getName();
            if (port->getDeviceId() >= 0) dev = port->getDeviceName(port->getDeviceId());
        } else dev = "Maschine Jam - 1";
        NVGcolor dim = nvgRGBA(255, 190, 90, 130);
        nvgFontSize(args.vg, 6.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, dim);
        nvgText(args.vg, 5.f, y + h * 0.3f, title, NULL);
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, W - 5.f, y + h * 0.3f, drv.c_str(), NULL);
        nvgFontSize(args.vg, 8.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGBA(255, 190, 90, 255));
        nvgText(args.vg, 5.f, y + h * 0.68f, dev.c_str(), NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            midi::Port* port = e.pos.y < box.size.y * 0.5f ? in : out;
            if (port) {
                ui::Menu* menu = createMenu();
                menu->addChild(createMenuLabel(port == in ? "MIDI IN port" : "MIDI OUT port"));
                appendMidiMenu(menu, port);
            }
            e.consume(this);
        }
    }
};

struct JPatBtn : OpaqueWidget {
    SynTHBloKMatrix8* module = NULL;
    int p = 0;
    JPatBtn() { box.size = mm2px(Vec(6.6f, 6.6f)); }
    void draw(const DrawArgs& args) override {
        bool sel = module ? module->curPat() == p : p == 0;
        drawColorButton(args, box.size, p, sel, (char)('1' + p), 11.f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[SynTHBloKMatrix8::PAT_PARAM].setValue((float)(p + 1));
            e.consume(this);
        }
    }
};

struct JStrip : OpaqueWidget {
    SynTHBloKMatrix8* module = NULL;
    int idx = 0;
    JStrip() { box.size = mm2px(Vec(4.8f, 48.f)); }
    float get() { return module ? module->val[module->bank()][idx] : 0.25f + 0.08f * idx; }
    void set(float v) { if (module) module->val[module->bank()][idx] = clamp(v, 0.f, 1.f); }
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        nvgFillColor(args.vg, nvgRGB(0x14, 0x14, 0x14));
        nvgFill(args.vg);
        NVGcolor col = kPatOnColor[module ? module->bank() : 0];   // strips take the selected bank's colour
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

struct JBankSq : OpaqueWidget {
    SynTHBloKMatrix8* module = NULL;
    int b = 0;
    JBankSq() { box.size = mm2px(Vec(4.8f, 6.6f)); }
    void draw(const DrawArgs& args) override {
        bool sel = module ? module->bank() == b : b == 0;
        drawColorButton(args, box.size, b, sel, (char)('A' + b), 9.f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[SynTHBloKMatrix8::BANK_PARAM].setValue((float)(b + 1));
            e.consume(this);
        }
    }
};

// Module name in the header bar: "SynTHBloK" in white over "MATRIX-8" in orange.
struct JTitle : TransparentWidget {
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
        nvgText(args.vg, 0.f, mm2px(6.4f), "MTX-8", NULL);
    }
};

// Panel caption drawn by the module (so labels follow the layout), centred on its box.
struct JLabel : TransparentWidget {
    std::string text;
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 7.f);
        nvgTextLetterSpacing(args.vg, 0.6f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(0xbb, 0xbb, 0xbb));
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, text.c_str(), NULL);
    }
};

// LEARN / CLEAR / RUN / RESET: shaped like the page and bank buttons, coloured like the
// SynTHBloK MIDI + display (amber on dark brown, lit = orange, RUN lit = green).
// A trigger at `jack` presses the button (RUN toggles, RESET fires); a dot in the corner
// shows the jack is patched.
struct JAmberBtn : app::ParamWidget {
    SynTHBloKMatrix8* jam = NULL;
    const char* label = "";
    bool latch = false, green = false;
    int jack = -1;
    float fontSize = 8.f;
    JAmberBtn() { box.size = mm2px(Vec(10.25f, 6.f)); }
    bool jackPatched() { return jam && jack >= 0 && jam->inputs[jack].isConnected(); }
    bool lit() {
        if (!jam) return green;
        if (jack == SynTHBloKMatrix8::RESET_INPUT && jam->resetFlash > 0.f) return true;
        ParamQuantity* pq = getParamQuantity();
        return pq && pq->getValue() > 0.5f;
    }
    void draw(const DrawArgs& args) override {
        bool on = lit();
        NVGcolor onFill = green ? nvgRGBA(120, 230, 90, 235) : nvgRGBA(255, 112, 15, 235);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.4f);
        nvgFillColor(args.vg, on ? onFill : nvgRGBA(40, 25, 8, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, on ? 255 : 120));
        nvgStrokeWidth(args.vg, on ? 1.2f : 0.8f);
        nvgStroke(args.vg);
        if (jackPatched()) {
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, box.size.x - 2.6f, 2.6f, 1.3f);
            nvgFillColor(args.vg, on ? nvgRGBA(20, 10, 2, 255) : nvgRGBA(255, 190, 90, 255));
            nvgFill(args.vg);
        }
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? nvgRGBA(20, 10, 2, 255) : nvgRGBA(255, 190, 90, 255));
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, label, NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (ParamQuantity* pq = getParamQuantity()) pq->setValue(latch ? (pq->getValue() > 0.5f ? 0.f : 1.f) : 1.f);
            e.consume(this);
            return;
        }
        ParamWidget::onButton(e);
    }
    void onDragStart(const DragStartEvent& e) override {}
    void onDragEnd(const DragEndEvent& e) override {
        if (!latch && e.button == GLFW_MOUSE_BUTTON_LEFT)
            if (ParamQuantity* pq = getParamQuantity()) pq->setValue(0.f);
    }
};

// Clock display in the MIDI + style: tempo from the CLOCK jack (16th-note clock), step and run state.
struct JClockDisplay : TransparentWidget {
    SynTHBloKMatrix8* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.f);
        nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (font) {
            NVGcolor amb = nvgRGBA(255, 190, 90, 255), dim = nvgRGBA(255, 190, 90, 130);
            float bigFont = std::max(11.f, H * 0.3f), smallFont = std::max(8.f, H * 0.2f);
            char bpm[24] = "--- BPM", info[32] = "NO CLOCK";
            bool run = module ? module->running : true;
            if (!module) snprintf(bpm, sizeof bpm, "120.0 BPM");
            else if (module->inputs[SynTHBloKMatrix8::CLOCK_INPUT].isConnected()) {
                if (module->clockPeriod > 0.f && module->sinceClock < 2.f)
                    snprintf(bpm, sizeof bpm, "%.1f BPM", 60.f / (module->clockPeriod * 4.f));
                int len = clamp((int)module->params[SynTHBloKMatrix8::LEN_PARAM].getValue(), 1, 8);
                snprintf(info, sizeof info, "STEP %d/%d", module->step + 1, len);
            }
            nvgFontFaceId(args.vg, font->handle);
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFontSize(args.vg, bigFont);
            nvgFillColor(args.vg, amb);
            nvgText(args.vg, 5.f, H * 0.33f, bpm, NULL);
            // Which sound parameter the strips are on.
            char bankText[16];
            int bk = module ? module->bank() : 0;
            snprintf(bankText, sizeof bankText, "%c %s", 'A' + bk, kBankName[bk]);
            nvgFontSize(args.vg, smallFont);
            nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, amb);
            nvgText(args.vg, W - 5.f, H * 0.33f, bankText, NULL);
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFontSize(args.vg, smallFont);
            nvgFillColor(args.vg, dim);
            nvgText(args.vg, 5.f, H * 0.74f, info, NULL);
            nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, run ? nvgRGBA(120, 230, 90, 255) : dim);
            nvgText(args.vg, W - 5.f, H * 0.74f, run ? "RUN" : "STOP", NULL);
        }
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.5f, 0.5f, W - 1.f, H - 1.f, 2.f);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 80));
        nvgStroke(args.vg);
    }
};

// Amber value arc around the knob (MIDI + colours), so its position reads like the JAM's meter.
struct JKnobRing : TransparentWidget {
    SynTHBloKMatrix8* module = NULL;
    int param = SynTHBloKMatrix8::KNOB_PARAM;
    JKnobRing() { box.size = mm2px(Vec(19.f, 19.f)); }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float v = module ? module->params[param].getValue() : 0.6f;
        float cx = box.size.x * 0.5f, cy = box.size.y * 0.5f, rr = box.size.x * 0.5f - 2.f;
        float a0 = M_PI * 0.75f, a1 = M_PI * 2.25f;
        nvgBeginPath(args.vg);
        nvgArc(args.vg, cx, cy, rr, a0, a1, NVG_CW);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 40));
        nvgStrokeWidth(args.vg, 2.f);
        nvgStroke(args.vg);
        if (v > 0.001f) {
            nvgBeginPath(args.vg);
            nvgArc(args.vg, cx, cy, rr, a0, a0 + (a1 - a0) * v, NVG_CW);
            nvgStrokeColor(args.vg, nvgRGBA(255, 112, 15, 235));
            nvgStrokeWidth(args.vg, 2.f);
            nvgStroke(args.vg);
        }
    }
};

// LOOP column: the JAM cross pad's UP / DOWN (pattern length) on the panel.
// Arrow buttons step the length; the display shows the looped steps and the playhead.
struct JLoopArrow : OpaqueWidget {
    SynTHBloKMatrix8* module = NULL;
    int dir = 1;                  // +1 = UP (longer), -1 = DOWN (shorter)
    float flash = 0.f;
    void draw(const DrawArgs& args) override {
        float W = box.size.x, H = box.size.y;
        bool on = flash > 0.f;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.4f);
        nvgFillColor(args.vg, on ? nvgRGBA(255, 112, 15, 235) : nvgRGBA(40, 25, 8, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, on ? 255 : 120));
        nvgStrokeWidth(args.vg, on ? 1.2f : 0.8f);
        nvgStroke(args.vg);
        float cx = W * 0.5f, cy = H * 0.5f, s = std::min(W, H) * 0.22f;
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, cx, cy - dir * s);
        nvgLineTo(args.vg, cx + s * 1.2f, cy + dir * s);
        nvgLineTo(args.vg, cx - s * 1.2f, cy + dir * s);
        nvgClosePath(args.vg);
        nvgFillColor(args.vg, on ? nvgRGBA(20, 10, 2, 255) : nvgRGBA(255, 190, 90, 255));
        nvgFill(args.vg);
        flash = std::max(0.f, flash - 0.1f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) {
                ParamQuantity* pq = module->paramQuantities[SynTHBloKMatrix8::LEN_PARAM];
                pq->setValue((float)clamp((int)pq->getValue() + dir, 1, 8));
            }
            flash = 1.f;
            e.consume(this);
        }
    }
};

struct JLoopDisplay : OpaqueWidget {
    SynTHBloKMatrix8* module = NULL;
    int len() { return module ? clamp((int)module->params[SynTHBloKMatrix8::LEN_PARAM].getValue(), 1, 8) : 6; }
    void draw(const DrawArgs& args) override {
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.f);
        nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        int n = len();
        int step = module ? module->step : 2;
        bool run = module ? module->running : true;
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        float top = 2.f;
        if (font) {
            char b[4]; snprintf(b, sizeof b, "%d", n);
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, 11.f);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            nvgFillColor(args.vg, nvgRGBA(255, 190, 90, 255));
            nvgText(args.vg, W * 0.5f, top, b, NULL);
            top += 12.f;
        }
        // steps 1..8 top to bottom: lit = inside the loop, white = playhead
        float ch = (H - top - 2.f) / 8.f;
        for (int i = 0; i < 8; i++) {
            bool in = i < n, head = run && step == i;
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, 3.f, top + i * ch + 0.6f, W - 6.f, ch - 1.2f, 0.8f);
            nvgFillColor(args.vg, head ? nvgRGBA(255, 255, 255, 235) : in ? nvgRGBA(255, 112, 15, 220) : nvgRGBA(40, 25, 8, 255));
            nvgFill(args.vg);
        }
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.5f, 0.5f, W - 1.f, H - 1.f, 2.f);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 80));
        nvgStroke(args.vg);
    }
    // click a step cell to loop up to it
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            float top = 14.f, ch = (box.size.y - top - 2.f) / 8.f;
            if (module && e.pos.y >= top) {
                int i = clamp((int)((e.pos.y - top) / ch), 0, 7);
                module->paramQuantities[SynTHBloKMatrix8::LEN_PARAM]->setValue((float)(i + 1));
            }
            e.consume(this);
        }
    }
};

struct SynTHBloKMatrix8Widget : ModuleWidget {
    SynTHBloKMatrix8Widget(SynTHBloKMatrix8* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKMatrix8.svg")));
        using namespace rack::componentlibrary;
        const float PW = 127.0f;
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PW - 3.5f, 3.5f))));
        JTitle* title = createWidget<JTitle>(mm2px(Vec(8.5f, 0.f)));
        title->box.size = mm2px(Vec(60.f, 7.f));
        addChild(title);

        auto label = [&](float cx, float cy, const char* text) {
            JLabel* l = createWidget<JLabel>(mm2px(Vec(cx - 12.f, cy - 2.f)));
            l->box.size = mm2px(Vec(24.f, 4.f)); l->text = text;
            addChild(l);
        };

        // ---------------- left: laid out like the JAM ----------------
        // page buttons 1..8, pad grid (with its trigger outputs), strip outputs, bank buttons A..H, touch strips.
        // Every column lines up with a pad column.
        auto colX = [](int c) { return 4.9f + 7.4f * c; };
        for (int p = 0; p < 8; p++) {
            JPatBtn* pb = createWidget<JPatBtn>(mm2px(Vec(colX(p), 10.5f)));
            pb->module = module; pb->p = p;
            addChild(pb);
        }
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                JSPad* p = createWidget<JSPad>(mm2px(Vec(colX(c), 19.5f + 7.4f * r)));
                p->module = module; p->r = r; p->c = c;
                addChild(p);
            }
        label(69.6f, 13.8f, "OUT");
        for (int r = 0; r < 8; r++)
            addOutput(createOutputCentered<SmallJack>(mm2px(Vec(69.6f, 22.8f + 7.4f * r)), module, SynTHBloKMatrix8::TRIG_OUTPUT + r));
        for (int i = 0; i < 8; i++) {
            addOutput(createOutputCentered<SmallJack>(mm2px(Vec(colX(i) + 3.3f, 81.6f)), module, SynTHBloKMatrix8::STRIP_OUTPUT + i));
            JBankSq* q = createWidget<JBankSq>(mm2px(Vec(colX(i), 85.5f)));
            q->box.size = mm2px(Vec(6.6f, 6.6f));
            q->module = module; q->b = i;
            addChild(q);
            JStrip* s = createWidget<JStrip>(mm2px(Vec(colX(i) + 0.9f, 93.5f)));
            s->box.size = mm2px(Vec(4.8f, 30.f));
            s->module = module; s->idx = i;
            addChild(s);
        }

        // LOOP column beside the strips, under the OUT jacks (JAM cross pad UP / DOWN = length)
        label(69.6f, 81.6f, "LOOP");
        JLoopArrow* up = createWidget<JLoopArrow>(mm2px(Vec(66.1f, 85.5f)));      // level with the bank buttons
        up->box.size = mm2px(Vec(7.f, 6.6f)); up->module = module; up->dir = 1;
        addChild(up);
        JLoopDisplay* ld = createWidget<JLoopDisplay>(mm2px(Vec(66.1f, 93.5f)));  // level with the strips
        ld->box.size = mm2px(Vec(7.f, 22.5f)); ld->module = module;
        addChild(ld);
        JLoopArrow* dn = createWidget<JLoopArrow>(mm2px(Vec(66.1f, 117.5f)));     // level with the bottom buttons
        dn->box.size = mm2px(Vec(7.f, 6.f)); dn->module = module; dn->dir = -1;
        addChild(dn);

        // ---------------- right: everything else ----------------
        const float RX = 78.f, RW = 45.6f, RC = RX + RW * 0.5f;
        JMidiDisplay* md = createWidget<JMidiDisplay>(mm2px(Vec(RX, 10.5f)));   // top level with the page buttons
        md->box.size = mm2px(Vec(RW, 18.f));
        md->in = module ? &module->midiIn : NULL;
        md->out = module ? &module->midiOut : NULL;
        addChild(md);
        JClockDisplay* cd = createWidget<JClockDisplay>(mm2px(Vec(RX, 30.5f)));   // tempo display under MIDI
        cd->box.size = mm2px(Vec(RW, 12.f)); cd->module = module;
        addChild(cd);

        // LEARN / CLEAR / RUN / RESET / range.
        struct BtnDef { const char* label; int param; bool latch, green; int jack; };
        const BtnDef defs[5] = {
            {"LEARN", SynTHBloKMatrix8::LEARN_PARAM, true, false, -1},
            {"CLEAR", SynTHBloKMatrix8::CLEAR_PARAM, false, false, -1},
            {"RUN", SynTHBloKMatrix8::RUN_PARAM, true, true, SynTHBloKMatrix8::RUN_INPUT},
            {"RESET", SynTHBloKMatrix8::RESET_PARAM, false, false, SynTHBloKMatrix8::RESET_INPUT},
            {"\xC2\xB1" "5V", SynTHBloKMatrix8::RANGE_PARAM, true, false, -1}};
        for (int i = 0; i < 5; i++) {
            JAmberBtn* b = createParam<JAmberBtn>(mm2px(Vec(RX + 9.3f * i, 117.5f)), module, defs[i].param);   // bottom, level with the strips' end
            b->box.size = mm2px(Vec(8.4f, 6.f));
            b->fontSize = 6.5f;
            b->jam = module; b->label = defs[i].label; b->latch = defs[i].latch;
            b->green = defs[i].green; b->jack = defs[i].jack;
            addParam(b);
        }

        // Sound section under the BPM display, on a 4-column grid:
        //   FILTER 2: CUTOFF RES ENV MODE / ENVELOPE: ATK DEC SUS REL / DELAY MASTER SPACE.
        auto col4 = [&](int i) { return RX + RW * (i + 0.5f) / 4.f; };
        auto smallKnob = [&](float x, float y, int param, const char* name) {
            addChild(KnobTicks::create(Vec(x, y)));
            JKnobRing* fr = createWidget<JKnobRing>(mm2px(Vec(x - 5.5f, y - 5.5f)));
            fr->box.size = mm2px(Vec(11.f, 11.f));
            fr->module = module; fr->param = param;
            addChild(fr);
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y)), module, param));
            label(x, y + 7.f, name);
        };
        const int F2[4] = {SynTHBloKMatrix8::F2_CUTOFF_PARAM, SynTHBloKMatrix8::F2_RES_PARAM,
                           SynTHBloKMatrix8::F2_ENV_PARAM, SynTHBloKMatrix8::F2_MODE_PARAM};
        const char* F2N[4] = {"CUTOFF", "RES", "ENV", "MODE"};
        const int EV[4] = {SynTHBloKMatrix8::ENV_A_PARAM, SynTHBloKMatrix8::ENV_D_PARAM,
                           SynTHBloKMatrix8::ENV_S_PARAM, SynTHBloKMatrix8::ENV_R_PARAM};
        const char* EVN[4] = {"ATK", "DEC", "SUS", "REL"};
        for (int i = 0; i < 4; i++) {
            smallKnob(col4(i), 50.f, F2[i], F2N[i]);
            smallKnob(col4(i), 65.f, EV[i], EVN[i]);
        }
        smallKnob(col4(0), 85.f, SynTHBloKMatrix8::DELAY_PARAM, "DELAY");    // JAM knob on GRP
        smallKnob(col4(3), 85.f, SynTHBloKMatrix8::SPACE_PARAM, "SPACE");    // JAM knob on IN1

        // Master volume, synced with the JAM's knob (MST/CUE); its CV output is in the jack row.
        const float KY = 85.f;
        addChild(KnobTicks::create(Vec(RC, KY), 19.f / 11.f));   // scaled with the ring
        JKnobRing* ring = createWidgetCentered<JKnobRing>(mm2px(Vec(RC, KY)));
        ring->module = module;
        addChild(ring);
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(RC, KY)), module, SynTHBloKMatrix8::KNOB_PARAM));
        label(RC, KY + 12.f, "MASTER");

        // All right-panel jacks in one row just above the buttons, labels on top.
        const char* JNAME[6] = {"RUN", "RST", "CLK", "KNOB", "L", "R"};
        const int JIN[3] = {SynTHBloKMatrix8::RUN_INPUT, SynTHBloKMatrix8::RESET_INPUT, SynTHBloKMatrix8::CLOCK_INPUT};
        const int JOUT[3] = {SynTHBloKMatrix8::KNOB_OUTPUT, SynTHBloKMatrix8::AUDIO_L_OUTPUT, SynTHBloKMatrix8::AUDIO_R_OUTPUT};
        for (int i = 0; i < 6; i++) {
            float x = RX + RW * (i + 0.5f) / 6.f;
            label(x, 102.f, JNAME[i]);
            if (i < 3) addInput(createInputCentered<SmallJack>(mm2px(Vec(x, 107.f)), module, JIN[i]));
            else addOutput(createOutputCentered<SmallJack>(mm2px(Vec(x, 107.f)), module, JOUT[i - 3]));
        }
    }

    // Settings that used to be panel knobs live in the right-click menu.
    void appendContextMenu(Menu* menu) override {
        SynTHBloKMatrix8* m = getModule<SynTHBloKMatrix8>();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        auto indexItem = [&](const std::string& text, int paramId, std::vector<std::string> labels) {
            menu->addChild(createIndexSubmenuItem(text, labels,
                [=]() { return (size_t)(m->params[paramId].getValue() - 1.f); },
                [=](size_t i) { m->params[paramId].setValue((float)i + 1.f); }));
        };
        indexItem("Pattern length", SynTHBloKMatrix8::LEN_PARAM, {"1 step", "2 steps", "3 steps", "4 steps", "5 steps", "6 steps", "7 steps", "8 steps"});
        indexItem("JAM matrix page", SynTHBloKMatrix8::PAGE_PARAM, {"1", "2", "3", "4", "5", "6"});
        indexItem("Strip bank", SynTHBloKMatrix8::BANK_PARAM, {"A", "B", "C", "D", "E", "F", "G", "H"});
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolPtrMenuItem("JAM lights match panel colours", "", &m->matchColors));
        menu->addChild(createMenuLabel("Custom JAM LED colours (when not matching)"));
        for (int id : {SynTHBloKMatrix8::OFFC_PARAM, SynTHBloKMatrix8::ONC_PARAM, SynTHBloKMatrix8::HEADC_PARAM,
                       SynTHBloKMatrix8::HITC_PARAM, SynTHBloKMatrix8::BONC_PARAM, SynTHBloKMatrix8::BOFFC_PARAM}) {
            ui::Slider* s = new ui::Slider;
            s->quantity = m->paramQuantities[id];
            s->box.size.x = 220.f;
            menu->addChild(s);
        }
    }
};

} // namespace

Model* modelSynTHBloKMatrix8 = createModel<SynTHBloKMatrix8, SynTHBloKMatrix8Widget>("SynTHBloKMatrix8");
