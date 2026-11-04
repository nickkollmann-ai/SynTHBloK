// ============================================================================
// SynTHBloK MTX QUAD OSCILLATOR SYNTH  (the QUAD with the MATRIX note grid and per-step sliders)
//  The SynTHBloK-QUAD (QuadCore.hpp) on the right, with its essential jacks only (CLOCK IN/OUT, RESET,
//  RUN, stereo OUT; 41 HP), played by an 8 x 8 note grid on the left:
//  8 steps x 8 notes of KEY + SCALE (bottom row = root, top = its octave). A pad adds that note to the
//  step, again removes it; several notes on a step play as a chord (up to 4 voices, through the QUAD's
//  one filter and VCA). Right-click a pad = the step's accent on / off. 8 patterns, 1..8 steps (LOOP).
//  The sliders set one value per step; the buttons above them pick which:
//   ACC accent, CUT cutoff +-, GATE length (full = tie into the next step), GLIDE, PROB chance,
//   RATCH 1..4 hits, OCT -2..+2 octaves, RES resonance +-.
//  Clock: one step per QUAD clock pulse (the CLOCK knob, or the CLOCK IN jack), on the same sample as
//  CLOCK OUT; RESET IN goes back to step 1; RUN IN (trigger: start / stop, or gate) starts from step 1 or stops
//  it, and while stopped the clock holds (CLOCK OUT quiet).
//  TO SYNTH on: the grid plays the QUAD. CV / GATE / ACC out
//  always carry the grid (CV and GATE polyphonic, one channel per chord note).
//  KEY, SCALE, octave, RUN, copy and clear are in the right-click menu.
// ============================================================================
#include "plugin.hpp"
#include "QuadCore.hpp"
#include "Matrix8_ui.hpp"

namespace {

static const char* kMqKeyName[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const int kMqScaleCount = 8;
static const int kMqScaleLen[kMqScaleCount] = {7, 7, 7, 7, 7, 7, 5, 5};
static const int kMqScale[kMqScaleCount][7] = {
    {0, 2, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
    {0, 2, 4, 5, 7, 9, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 3, 5, 7, 10}, {0, 2, 4, 7, 9}};

// per-step slider banks
enum MqBank { B_ACC, B_CUT, B_GATE, B_GLIDE, B_PROB, B_RATCH, B_OCT, B_RES, B_NUM };
static const char* kMqBankName[B_NUM] = {"ACC", "CUT", "GATE", "GLIDE", "PROB", "RATCH", "OCT", "RES"};
static const float kMqBankDef[B_NUM] = {0.f, 0.5f, 0.5f, 0.f, 1.f, 0.f, 0.5f, 0.5f};
static bool mqBipolar(int b) { return b == B_CUT || b == B_OCT || b == B_RES; }

} // namespace

struct SynTHBloKMtxQuad : SynTHBloK {
    enum MqParamId { PAT_PARAM = PARAMS_LEN, LEN_PARAM, BANK_PARAM, KEY_PARAM, SCALE_PARAM, OCT_PARAM,
                     RUN_PARAM, RAND_PARAM, TOSYNTH_PARAM, MQ_PARAMS_END };
    enum MqInputId { RUN_INPUT = INPUTS_LEN, MQ_INPUTS_END };
    enum MqOutputId { CV_OUTPUT = OUTPUTS_LEN, GATE_OUTPUT, ACC_OUTPUT, MQ_OUTPUTS_END };

    uint8_t note[8][8] = {};      // [pattern][step]: bit d = scale degree d (bottom row = 0)
    float val[8][B_NUM][8];       // [pattern][bank][step], 0..1

    int step = -1;
    bool running = true;
    bool pendingReset = false;
    float resetFlash = 0.f;       // JAmberBtn wants it
    float lastRst = 0.f;
    float sinceEdge = 1.f;        // seconds since the last clock pulse
    dsp::SchmittTrigger randTrig, runTrig;
    bool runWas = false, wasRunning = false;
    float runHigh = 0.f;          // seconds RUN IN has been high

    // the step that is playing
    bool playing = false, tie = false, tiedIn = false;
    int ratch = 1, ch = 1;
    float stepSec = 0.25f, tStep = 0.f, gateFrac = 0.5f;
    float voct[4] = {};

    SynTHBloKMtxQuad() : SynTHBloK(MQ_PARAMS_END - PARAMS_LEN, MQ_INPUTS_END - INPUTS_LEN, MQ_OUTPUTS_END - OUTPUTS_LEN, 0) {
        configParam(PAT_PARAM, 1.f, 8.f, 1.f, "Pattern");
        configParam(LEN_PARAM, 1.f, 8.f, 8.f, "Pattern length (steps)");
        configParam(BANK_PARAM, 0.f, 7.f, 0.f, "Slider bank");
        paramQuantities[PAT_PARAM]->snapEnabled = true;
        paramQuantities[LEN_PARAM]->snapEnabled = true;
        paramQuantities[BANK_PARAM]->snapEnabled = true;
        configSwitch(KEY_PARAM, 0.f, 11.f, 0.f, "Key", std::vector<std::string>(kMqKeyName, kMqKeyName + 12));
        configSwitch(SCALE_PARAM, 0.f, kMqScaleCount - 1, 0.f, "Scale",
                     {"Minor", "Major", "Dorian", "Phrygian", "Mixolydian", "Harmonic minor", "Minor pentatonic", "Major pentatonic"});
        configSwitch(OCT_PARAM, -2.f, 2.f, -1.f, "Grid octave (bottom row)", {"C1", "C2", "C3", "C4", "C5"});
        configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run", {"Stopped", "Running"});
        configButton(RAND_PARAM, "Random lines on all 8 patterns (a chord progression, in key); the sliders stay");
        configSwitch(TOSYNTH_PARAM, 0.f, 1.f, 1.f, "Grid plays the synth", {"Off", "On"});
        configInput(RUN_INPUT, "Run (a trigger starts from step 1 and the next stops; or a gate: high runs)");
        configOutput(CV_OUTPUT, "Grid pitch (1V/oct, one channel per chord note)");
        configOutput(GATE_OUTPUT, "Grid gate (one channel per chord note)");
        configOutput(ACC_OUTPUT, "Grid accent (0..10 V while the gate is high)");
        clearAll();
        // pattern 1: a little line to start from
        static const int8_t defNote[8] = {0, -1, 2, 4, -1, 7, 4, 2};
        for (int c = 0; c < 8; c++) note[0][c] = defNote[c] < 0 ? 0 : 1 << defNote[c];
        val[0][B_ACC][0] = 1.f;
    }

    int curPat() { return clamp((int)params[PAT_PARAM].getValue(), 1, 8) - 1; }
    int bank() { return clamp((int)params[BANK_PARAM].getValue(), 0, 7); }
    int len() { return clamp((int)params[LEN_PARAM].getValue(), 1, 8); }

    void clearPattern(int p) {
        for (int c = 0; c < 8; c++) note[p][c] = 0;
        for (int b = 0; b < B_NUM; b++) for (int c = 0; c < 8; c++) val[p][b][c] = kMqBankDef[b];
    }
    void clearAll() { for (int p = 0; p < 8; p++) clearPattern(p); }
    void copyToNext() {
        int from = curPat(), to = (from + 1) % 8;
        std::memcpy(note[to], note[from], sizeof(note[0]));
        std::memcpy(val[to], val[from], sizeof(val[0]));
        params[PAT_PARAM].setValue((float)(to + 1));
    }

    // Pad (r, c): degree 7 - r at step c.
    bool padOn(int r, int c) { return note[curPat()][c] >> (7 - r) & 1; }
    void togglePad(int r, int c) { note[curPat()][c] ^= 1 << (7 - r); }
    bool stepAcc(int c) { return val[curPat()][B_ACC][c] >= 0.5f; }
    // right-click: the step's accent on / off; an empty pad also gets its note
    void toggleAccent(int r, int c) {
        float& a = val[curPat()][B_ACC][c];
        if (!padOn(r, c)) { togglePad(r, c); a = 1.f; return; }
        a = a >= 0.5f ? 0.f : 1.f;
    }
    float stripGet(int i) { return val[curPat()][bank()][i]; }
    void stripSet(int i, float v) { val[curPat()][bank()][i] = clamp(v, 0.f, 1.f); }

    int scale() { return clamp((int)params[SCALE_PARAM].getValue(), 0, kMqScaleCount - 1); }
    // semitones of scale degree d above the key
    int semis(int d) { int s = scale(), n = kMqScaleLen[s]; return 12 * (d / n) + kMqScale[s][d % n]; }

    // The notes of a step, in semitones above the key (lowest first, up to 4); returns how many.
    // One note plays as it is. Several always make a consonant chord, whatever pads are lit:
    //  - the root is the lit note whose triad (its 3rd and perfect 5th, in the scale) holds the most lit notes,
    //    ties going to the lowest; a root whose 5th is not in the scale only wins if no other can
    //  - every lit note moves to the nearest root / 3rd / 5th / octave in its own register; repeats drop
    //  - more than 4: the lowest root, the 3rd, the 5th and the top note
    //  - just intonation (menu, on by default): the 3rd and 5th are tuned to the root as 5:4 / 6:5 and 3:2,
    //    so the chord rings without beating; the root itself stays on the key's equal-tempered pitch
    bool justChords = true;
    static void sortInts(int* a, int n) {   // a few notes: insertion sort
        for (int i = 1; i < n; i++) for (int j = i; j > 0 && a[j - 1] > a[j]; j--) std::swap(a[j - 1], a[j]);
    }
    int chordOf(int mask, float* out) {
        int lit[8], n = 0;
        for (int d = 0; d < 8; d++) if (mask >> d & 1) lit[n++] = semis(d);
        if (n == 0) return 0;
        if (n == 1) { out[0] = (float)lit[0]; return 1; }
        auto inScale = [&](int st) {
            int s = scale(), pc = ((st % 12) + 12) % 12;
            for (int i = 0; i < kMqScaleLen[s]; i++) if (kMqScale[s][i] == pc) return true;
            return false;
        };
        int bestRoot = lit[0], bestThird = 4, bestScore = -1;
        for (int i = 0; i < n; i++) {
            int r = lit[i];
            int third = inScale(r + 4) ? 4 : inScale(r + 3) ? 3 : 4;
            bool fifthOk = inScale(r + 7);
            int score = 0;
            for (int j = 0; j < n; j++) {
                int iv = ((lit[j] - r) % 12 + 12) % 12;
                if (iv == 0 || iv == third || iv == 7) score += 2;
            }
            if (fifthOk) score += 100;   // a perfect 5th in the scale comes first
            if (score > bestScore) { bestScore = score; bestRoot = r; bestThird = third; }
        }
        // move every lit note to the nearest chord tone in its register
        int tones[5] = {0, bestThird, 7, 12, -5};   // -5: the 5th below, for a note just under the root
        int got[16], m = 0;
        for (int j = 0; j < n; j++) {
            int rel = lit[j] - bestRoot, oc = (int)std::floor(rel / 12.f) * 12, best = 0, bd = 99;
            for (int t : tones) {
                int cand = oc + t, dist = std::abs(rel - cand);
                if (dist < bd || (dist == bd && cand > best)) { bd = dist; best = cand; }
            }
            bool dup = false;
            for (int q = 0; q < m; q++) dup |= got[q] == best;
            if (!dup) got[m++] = best;
        }
        sortInts(got, m);
        int pick[4], c = 0;
        if (m <= 4) { for (int q = 0; q < m; q++) pick[c++] = got[q]; }
        else {
            pick[c++] = got[0];
            for (int want : {bestThird, 7}) {
                for (int q = 1; q < m - 1; q++)
                    if (((got[q] % 12) + 12) % 12 == want) { pick[c++] = got[q]; break; }
            }
            pick[c++] = got[m - 1];
            sortInts(pick, c);
        }
        // pure intervals over the root (cents off equal temperament)
        auto pure = [&](int rel) {
            if (!justChords) return 0.f;
            switch (((rel % 12) + 12) % 12) {
                case 3: return 15.64f;     // 6:5
                case 4: return -13.69f;    // 5:4
                case 7: return 1.96f;      // 3:2
                default: return 0.f;
            }
        };
        for (int q = 0; q < c; q++) out[q] = bestRoot + pick[q] + pure(pick[q]) / 100.f;
        return c;
    }

    // a new step: what plays and how
    void startStep(int p) {
        tStep = 0.f;
        int mask = note[p][step];
        bool wasTied = playing && tie;
        float prob = val[p][B_PROB][step];
        playing = mask && (prob >= 0.999f || random::uniform() < prob);
        if (!playing) { tie = false; return; }
        int key = clamp((int)params[KEY_PARAM].getValue(), 0, 11);
        int oct = clamp((int)std::round(params[OCT_PARAM].getValue()), -2, 2)
                + clamp((int)std::round((val[p][B_OCT][step] - 0.5f) * 4.f), -2, 2);
        float pitch[4];
        int k = chordOf(mask, pitch);   // lowest note on voice 0, so a single note always glides on the same voice
        for (int i = 0; i < k; i++) voct[i] = (key + 12 * oct + pitch[i]) / 12.f;
        for (int i = k; i < 4; i++) voct[i] = voct[k - 1];
        ch = k;
        float g = val[p][B_GATE][step];
        ratch = 1 + (int)(val[p][B_RATCH][step] * 3.999f);
        gateFrac = 0.05f + 0.94f * g;
        tiedIn = wasTied;
        tie = g >= 0.99f && ratch == 1;
        seq.acc = val[p][B_ACC][step];
        seq.cut = (val[p][B_CUT][step] - 0.5f) * 0.8f;
        seq.res = (val[p][B_RES][step] - 0.5f) * 0.8f;
        seq.glide = val[p][B_GLIDE][step];
    }

    void process(const ProcessArgs& args) override {
        float dt = args.sampleTime;
        if (randTrig.process(params[RAND_PARAM].getValue())) randomizeAll();
        int p = curPat();
        // RUN IN takes a trigger or a gate. A rising edge starts; a short pulse (< 50 ms) that arrives while
        // running stops on its fall (trigger = start / stop), and a gate stops when it falls.
        if (runTrig.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f)) {
            runWas = params[RUN_PARAM].getValue() > 0.5f;
            runHigh = 0.f;
            params[RUN_PARAM].setValue(1.f);
        }
        if (runTrig.isHigh()) runHigh += dt;
        else if (runHigh > 0.f) {
            if (runHigh >= 0.05f || runWas) params[RUN_PARAM].setValue(0.f);
            runHigh = 0.f;
        }
        running = params[RUN_PARAM].getValue() > 0.5f;
        // stopped: the clock holds (no steps, CLOCK OUT quiet). Starting (RUN IN or the menu) plays step 1
        // on the next clock pulse, at once with the CLOCK knob.
        if (running && !wasRunning) {
            step = -1;
            pendingReset = true;
            if (!externalClock()) clockPhase = 1.f;
        }
        wasRunning = running;
        clockHold = !running;

        // RESET IN before the clock: a reset with a clock pulse plays step 1 on that pulse. A reset up to
        // 1 ms after a pulse restarts that pulse's step as step 1. With the CLOCK knob, the pulse (and
        // step 1, and CLOCK OUT) comes at once.
        float r = inputs[RESET_INPUT].getVoltage();
        if (r > 1.f && lastRst <= 1.f) {
            if (running && step >= 0 && sinceEdge < 1e-3f) { step = 0; startStep(p); }
            else {
                step = -1;
                pendingReset = true;
                if (!externalClock()) clockPhase = 1.f;
            }
        }
        lastRst = r;

        // the QUAD's own clock (CLOCK IN, or the CLOCK knob), run here so the step and CLOCK OUT share a sample
        runClock(dt);
        stepSec = clockPeriod();
        sinceEdge += dt;
        if (clockTrig) {
            sinceEdge = 0.f;
            if (running) {
                step = (pendingReset || step < 0) ? 0 : (step + 1) % len();
                pendingReset = false;
                startStep(p);
            }
        }
        if (!running) playing = false;

        // gate: GATE of the step (or of each ratchet hit); a tie holds it into the next step
        bool gate = false;
        if (playing) {
            float sub = stepSec / ratch;
            int hit = (int)(tStep / sub);
            float ph = tStep - hit * sub;
            float gl = (ratch > 1 ? std::min(gateFrac, 0.85f) : gateFrac) * sub;
            gate = tie || (hit < ratch && ph < gl);
            // a tied-into step keeps the gate high from its start (legato); others restart it
            if (!tiedIn && tStep < 0.5f * dt) gate = false;
            tStep += dt;   // tStep is 0 on the step's first sample
        }

        seq.on = params[TOSYNTH_PARAM].getValue() > 0.5f;
        seq.ch = ch;
        for (int i = 0; i < 4; i++) { seq.voct[i] = voct[i]; seq.gate[i] = gate && i < ch; }
        if (!playing) seq.acc = 0.f;

        SynTHBloK::process(args);

        outputs[CV_OUTPUT].setChannels(ch);
        outputs[GATE_OUTPUT].setChannels(ch);
        for (int i = 0; i < ch; i++) {
            outputs[CV_OUTPUT].setVoltage(voct[i], i);
            outputs[GATE_OUTPUT].setVoltage(gate ? 10.f : 0.f, i);
        }
        outputs[ACC_OUTPUT].setVoltage(gate ? seq.acc * 10.f : 0.f);
    }

    void onReset() override {
        SynTHBloK::onReset();
        clearAll();
        step = -1;
        playing = false;
    }

    // RAND: 8 related patterns following a chord progression (four chords, twice), in KEY / SCALE.
    // One rhythm for all 8; on the beats the line lands on a chord tone (sometimes the whole chord),
    // between them it moves a step or two. The sliders are left as they are.
    void randomizeAll() {
        auto rnd = []() { return random::uniform(); };
        auto pick = [&](int n) { return std::min((int)(rnd() * n), n - 1); };
        int n = kMqScaleLen[scale()];
        auto chordTone = [&](int t, int root) {
            int i = ((semis(t) - semis(root)) % 12 + 12) % 12;
            return i == 0 || i == 3 || i == 4 || i == 7;
        };
        static const int kProg[6][4] = {{0, 5, 3, 4}, {0, 4, 5, 3}, {0, 3, 4, 0}, {0, 3, 0, 4}, {0, 5, 3, 0}, {0, 2, 3, 4}};
        int prog[4];
        for (int k = 0; k < 4; k++) prog[k] = kProg[pick(6)][k] % n;
        int rhythm = 0x11;
        for (int c = 1; c < 8; c++) if (c != 4 && rnd() < 0.45f) rhythm |= 1 << c;
        int d = 2;
        for (int pt = 0; pt < 8; pt++) {
            int R = prog[pt % 4];
            for (int c = 0; c < 8; c++) {
                note[pt][c] = 0;
                if (!(rhythm >> c & 1)) continue;
                if (c % 4 == 0 || rnd() < 0.5f) {   // the nearest chord tone, within a fourth
                    int best = -1;
                    for (int t = 0; t <= 7; t++)
                        if (chordTone(t, R) && std::abs(t - d) <= 4 && (c % 4 == 0 || t != d) &&
                            (best < 0 || std::abs(t - d) < std::abs(best - d)))
                            best = t;
                    d = best >= 0 ? best : R;
                } else {
                    int mv = (pick(2) + 1) * (rnd() < 0.5f ? 1 : -1);
                    if (d + mv < 0 || d + mv > 7) mv = -mv;
                    d += mv;
                }
                uint8_t m = 1 << d;
                if (c % 4 == 0 && rnd() < 0.35f)   // the chord: its tones above the root, up to 3
                    for (int t = R; t <= 7 && __builtin_popcount(m) < 3; t++) if (chordTone(t, R)) m |= 1 << t;
                note[pt][c] = m;
            }
        }
    }

    json_t* dataToJson() override {
        json_t* root = SynTHBloK::dataToJson();
        json_t* na = json_array();
        json_t* va = json_array();
        for (int p = 0; p < 8; p++) {
            char h[17];
            for (int c = 0; c < 8; c++) snprintf(h + 2 * c, 3, "%02x", note[p][c]);
            json_array_append_new(na, json_string(h));
            for (int b = 0; b < B_NUM; b++) for (int c = 0; c < 8; c++) json_array_append_new(va, json_real(val[p][b][c]));
        }
        json_object_set_new(root, "notes", na);   // per pattern: two hex digits per step, bit d = scale degree d
        json_object_set_new(root, "steps", va);   // per pattern, bank, step
        json_object_set_new(root, "justChords", json_boolean(justChords));
        return root;
    }
    void dataFromJson(json_t* root) override {
        SynTHBloK::dataFromJson(root);
        json_t* na = json_object_get(root, "notes");
        for (int p = 0; p < 8 && na && p < (int)json_array_size(na); p++) {
            const char* h = json_string_value(json_array_get(na, p));
            for (int c = 0; h && c < 8 && h[2 * c] && h[2 * c + 1]; c++) {
                char b[3] = {h[2 * c], h[2 * c + 1], 0};
                note[p][c] = (uint8_t)strtol(b, NULL, 16);
            }
        }
        if (json_t* j = json_object_get(root, "justChords")) justChords = json_boolean_value(j);
        json_t* va = json_object_get(root, "steps");
        if (va && json_array_size(va) == 8 * B_NUM * 8) {
            int i = 0;
            for (int p = 0; p < 8; p++) for (int b = 0; b < B_NUM; b++) for (int c = 0; c < 8; c++)
                val[p][b][c] = clamp((float)json_number_value(json_array_get(va, i++)), 0.f, 1.f);
        }
    }
};

using MQ = SynTHBloKMtxQuad;

namespace {

// Note pad: lit = a note of this step, the root rows (top and bottom) outlined brighter, white core = accent.
struct MQPad : OpaqueWidget {
    MQ* module = NULL;
    int r = 0, c = 0;
    void draw(const DrawArgs& args) override {
        static const int preview[8] = {0, -1, 2, 4, -1, 7, 4, 2};
        bool on = module ? module->padOn(r, c) : 7 - r == preview[c];
        bool playing = module && module->running && module->step == c;
        int p = module ? module->curPat() : 0;
        NVGcolor col = kPatColor[p], onCol = kPatOnColor[p];
        if (r != 0 && r != 7) col = nvgLerpRGBA(kIdle, col, 0.5f);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        NVGcolor fill = on ? onCol : kIdle;
        if (playing) fill = on ? nvgLerpRGBA(onCol, nvgRGB(0xff, 0xff, 0xff), 0.55f) : nvgLerpRGBA(kIdle, col, 0.45f);
        nvgFillColor(args.vg, fill);
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, col);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
        if (on && (module ? module->stepAcc(c) : c == 0)) {
            float m = box.size.x * 0.3f;
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, m, m, box.size.x - 2.f * m, box.size.y - 2.f * m, 0.8f);
            nvgFillColor(args.vg, nvgLerpRGBA(onCol, nvgRGB(0xff, 0xf4, 0xd0), 0.75f));
            nvgFill(args.vg);
        }
    }
    // click = note on / off, right-click = accent
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || !module) return;
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) { module->togglePad(r, c); e.consume(this); }
        else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) { module->toggleAccent(r, c); e.consume(this); }
    }
};

struct MQPatBtn : OpaqueWidget {
    MQ* module = NULL;
    int p = 0;
    void draw(const DrawArgs& args) override {
        drawColorButton(args, box.size, p, module ? module->curPat() == p : p == 0, (char)('1' + p), 11.f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[MQ::PAT_PARAM].setValue((float)(p + 1));
            e.consume(this);
        }
    }
};

// Slider bank button: its name in the bank's colour, filled while it is the shown bank.
struct MQBankBtn : OpaqueWidget {
    MQ* module = NULL;
    int b = 0;
    void draw(const DrawArgs& args) override {
        bool sel = module ? module->bank() == b : b == 0;
        drawColorButton(args, box.size, b, sel, 0, 0.f);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 5.2f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, sel ? nvgRGB(0x10, 0x10, 0x10) : kPatColor[b]);
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, kMqBankName[b], NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[MQ::BANK_PARAM].setValue((float)b);
            e.consume(this);
        }
    }
};

// Step slider: the shown bank's value for step idx; click or drag it. CUT / OCT / RES fill from the middle.
struct MQStrip : OpaqueWidget {
    MQ* module = NULL;
    int idx = 0;
    float get() { return module ? module->stripGet(idx) : 0.25f + 0.08f * idx; }
    void set(float v) { if (module) module->stripSet(idx, v); }
    void draw(const DrawArgs& args) override {
        int b = module ? module->bank() : 0;
        bool head = module && module->running && module->step == idx;
        NVGcolor col = kPatOnColor[b];
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        nvgFillColor(args.vg, nvgRGB(0x14, 0x14, 0x14));
        nvgFill(args.vg);
        float H = box.size.y - 3.f, v = get();
        float y0 = mqBipolar(b) ? 1.5f + H * 0.5f : box.size.y - 1.5f, y1 = box.size.y - 1.5f - H * v;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 1.5f, std::min(y0, y1), box.size.x - 3.f, std::max(std::fabs(y1 - y0), 0.8f), 1.f);
        nvgFillColor(args.vg, col);
        nvgFill(args.vg);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        nvgStrokeColor(args.vg, head ? col : nvgLerpRGBA(kIdle, col, 0.45f));
        nvgStrokeWidth(args.vg, head ? 1.2f : 0.8f);
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

} // namespace

struct SynTHBloKMtxQuadWidget : ModuleWidget {
    SynTHBloKMtxQuadWidget(MQ* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/SynTHBloKMtxQuad.svg")));
        using namespace rack::componentlibrary;
        const float PW = 208.28f, MX = 81.28f;   // 41 HP: the grid section (16 HP), then the QUAD with its essential jacks (25 HP)
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PW - 3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 125.f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PW - 3.5f, 125.f))));

        // ---------------- the grid section ----------------
        auto colX = [](int c) { return 4.9f + 7.4f * c; };
        const float RX = 69.6f;   // the column right of the grid
        for (int p = 0; p < 8; p++) {
            MQPatBtn* pb = createWidget<MQPatBtn>(mm2px(Vec(colX(p), 10.8f)));
            pb->box.size = mm2px(Vec(6.6f, 6.6f)); pb->module = module; pb->p = p;
            addChild(pb);
        }
        addParam(JRandBtn::create(RX, 10.8f, module, MQ::RAND_PARAM));
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                MQPad* pd = createWidget<MQPad>(mm2px(Vec(colX(c), 19.8f + 7.4f * r)));
                pd->box.size = mm2px(Vec(6.6f, 6.6f)); pd->module = module; pd->r = r; pd->c = c;
                addChild(pd);
            }
        // grid outputs, and TO SYNTH
        const int jo[3] = {MQ::CV_OUTPUT, MQ::GATE_OUTPUT, MQ::ACC_OUTPUT};
        for (int i = 0; i < 3; i++)
            addOutput(createOutputCentered<BlackJack>(mm2px(Vec(RX, 27.f + 11.5f * i)), module, jo[i]));
        JAmberBtn<MQ>* ts = createParam<JAmberBtn<MQ>>(mm2px(Vec(RX - 3.5f, 68.3f)), module, MQ::TOSYNTH_PARAM);
        ts->box.size = mm2px(Vec(7.f, 5.f));
        ts->jam = module; ts->label = "ON"; ts->latch = true; ts->green = true; ts->fontSize = 6.5f;
        addParam(ts);
        // slider banks and the per-step sliders
        for (int i = 0; i < 8; i++) {
            MQBankBtn* q = createWidget<MQBankBtn>(mm2px(Vec(colX(i), 81.f)));
            q->box.size = mm2px(Vec(6.6f, 5.2f)); q->module = module; q->b = i;
            addChild(q);
            MQStrip* s = createWidget<MQStrip>(mm2px(Vec(colX(i) + 0.9f, 88.f)));
            s->box.size = mm2px(Vec(4.8f, 35.5f)); s->module = module; s->idx = i;
            addChild(s);
        }
        // LOOP beside the sliders: up, the length, down
        JLoopArrow<MQ>* up = createWidget<JLoopArrow<MQ>>(mm2px(Vec(RX - 3.5f, 81.f)));
        up->box.size = mm2px(Vec(7.f, 6.f)); up->module = module; up->dir = 1;
        addChild(up);
        JLoopDisplay<MQ>* ld = createWidget<JLoopDisplay<MQ>>(mm2px(Vec(RX - 3.5f, 88.f)));
        ld->box.size = mm2px(Vec(7.f, 27.5f)); ld->module = module;
        addChild(ld);
        JLoopArrow<MQ>* dn = createWidget<JLoopArrow<MQ>>(mm2px(Vec(RX - 3.5f, 117.5f)));
        dn->box.size = mm2px(Vec(7.f, 6.f)); dn->module = module; dn->dir = -1;
        addChild(dn);

        // ---------------- the QUAD ----------------
        addQuadControls(this, module, MX, true);
        addInput(createInputCentered<BlackJack>(mm2px(Vec(MX + 117.5f, 50.6f)), module, MQ::RUN_INPUT));   // under RESET
    }

    void appendContextMenu(Menu* menu) override {
        MQ* m = getModule<MQ>();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("MTX grid"));
        menu->addChild(createBoolMenuItem("Run", "",
            [=]() { return m->params[MQ::RUN_PARAM].getValue() > 0.5f; },
            [=](bool on) { m->params[MQ::RUN_PARAM].setValue(on ? 1.f : 0.f); }));
        menu->addChild(createIndexSubmenuItem("Key", std::vector<std::string>(kMqKeyName, kMqKeyName + 12),
            [=]() { return (size_t)m->params[MQ::KEY_PARAM].getValue(); },
            [=](size_t i) { m->params[MQ::KEY_PARAM].setValue((float)i); }));
        menu->addChild(createIndexSubmenuItem("Scale", {"Minor", "Major", "Dorian", "Phrygian", "Mixolydian", "Harmonic minor", "Minor pentatonic", "Major pentatonic"},
            [=]() { return (size_t)m->params[MQ::SCALE_PARAM].getValue(); },
            [=](size_t i) { m->params[MQ::SCALE_PARAM].setValue((float)i); }));
        menu->addChild(createIndexSubmenuItem("Grid octave (bottom row)", {"C1", "C2", "C3", "C4", "C5"},
            [=]() { return (size_t)(m->params[MQ::OCT_PARAM].getValue() + 2.f); },
            [=](size_t i) { m->params[MQ::OCT_PARAM].setValue((float)i - 2.f); }));
        menu->addChild(createIndexSubmenuItem("Pattern length", {"1 step", "2 steps", "3 steps", "4 steps", "5 steps", "6 steps", "7 steps", "8 steps"},
            [=]() { return (size_t)(m->params[MQ::LEN_PARAM].getValue() - 1.f); },
            [=](size_t i) { m->params[MQ::LEN_PARAM].setValue((float)i + 1.f); }));
        menu->addChild(createMenuItem("Copy pattern to the next one", "", [=]() { m->copyToNext(); }));
        menu->addChild(createMenuItem("Clear this pattern", "", [=]() { m->clearPattern(m->curPat()); }));
        menu->addChild(createMenuItem("Clear every pattern", "", [=]() { m->clearAll(); }));
        menu->addChild(createBoolPtrMenuItem("Chords in just intonation (pure, no beating)", "", &m->justChords));
        addQuadMenu(menu, m);
    }
};

Model* modelSynTHBloKMtxQuad = createModel<SynTHBloKMtxQuad, SynTHBloKMtxQuadWidget>("SynTHBloKMtxQuad");
