// SynTHBloK MATRIX-ECHO: the MATRIX FINAL-stage DELAY / SPACE as a standalone stereo delay and looper.
// 12 delay engines (CLASSIC = the MATRIX ping-pong), tap tempo / clock with 11 subdivisions (3 dual),
// freeze, kill dry, bypass with trails, a SPACE reverb on the repeats, 11 user + 48 factory presets, and a 60 s stereo
// looper (REC / REPLACE, PLAY / STOP, PLAY ONCE, UNDO / REDO, CLEAR) locked to the tempo: the transport
// lands on the beat or bar grid, playback re-syncs to every beat, and the loop plays 1 .. all its beats.
// One screen holds the delay, preset and looper controls.
#include "plugin.hpp"
#include "OledUi.hpp"
#include "KnobTicks.hpp"
#include "JackLed.hpp"
#include "Echo_dsp.hpp"

namespace {

const char* const kEchoTypeName[ET_NUM] = {"CLASSIC", "2290", "2290 MOD", "ANALOG", "TAPE", "LO-FI",
                                           "DYNAMIC", "REVERSE", "PING-PONG", "SLAP", "CRYSTAL", "SPACE"};

// Subdivisions of a beat (quarter note); b > 0 adds a second (dual) tap.
struct EchoSub { const char* name; float a, b; };
const EchoSub kEchoSub[11] = {{"1/2", 2.f, 0.f}, {"1/4.", 1.5f, 0.f}, {"1/4", 1.f, 0.f}, {"1/4T", 2.f / 3.f, 0.f},
                              {"1/8.", 0.75f, 0.f}, {"1/8", 0.5f, 0.f}, {"1/8T", 1.f / 3.f, 0.f}, {"1/16", 0.25f, 0.f},
                              {"1/4+1/8.", 1.f, 0.75f}, {"1/4+1/8T", 1.f, 1.f / 3.f}, {"1/8.+1/16", 0.75f, 0.25f}};

// Preset banks: USER (stored, 11 slots), then the factory banks of 8: DRUMS, SYNTH, CINEMATIC, DUB, LO-FI,
// RHYTHM. A preset = TYPE, TIME, SUBDIV, FEEDBACK, LEVEL, TONE, MOD, SPACE (TIME is the free-running time,
// near the subdivision at 120 BPM). Feedback stays at 0.65 or under, so nothing runs away. DRUMS also voices
// the repeats for beats (see process()), so the presets sit back: short, darker, modest feedback, the
// repeats ducked under the hits.
const int kBanks = 7, kGenreN = 8;
const char* const kBankName[kBanks] = {"USER", "DRUMS", "SYNTH", "CINEMATIC", "DUB", "LO-FI", "RHYTHM"};
struct EchoGenre { const char* name; const char* cell; float v[8]; };
const EchoGenre kGenre[kBanks - 1][kGenreN] = {
    {{"HOUSE", "HOUSE", {1, 0.5f, 4, 0.30f, 0.30f, 0.55f, 0.00f, 0.10f}},      // 2290, 1/8. offbeat bounce
     {"TECHNO", "TECHNO", {3, 0.5f, 7, 0.35f, 0.25f, 0.45f, 0.10f, 0.15f}},    // ANALOG, 1/16 rolling texture
     {"HIP-HOP", "HIPHOP", {4, 0.5f, 6, 0.25f, 0.30f, 0.35f, 0.25f, 0.05f}},   // TAPE, 1/8T lazy swing
     {"DRUM&BASS", "DNB", {8, 0.5f, 5, 0.30f, 0.25f, 0.60f, 0.00f, 0.10f}},  // PING-PONG, 1/8 width
     {"DUB", "DUB", {11, 0.5f, 1, 0.50f, 0.35f, 0.40f, 0.20f, 0.30f}},         // SPACE heads, 1/4. throws
     {"TRAP", "TRAP", {6, 0.5f, 6, 0.30f, 0.30f, 0.65f, 0.00f, 0.10f}},        // DYNAMIC, 1/8T hat rolls
     {"BREAKBEAT", "BREAKS", {9, 0.45f, 7, 0.30f, 0.30f, 0.50f, 0.05f, 0.10f}},  // SLAP ~68 ms, a fat snare double
     {"GARAGE", "GARAGE", {2, 0.36f, 6, 0.30f, 0.28f, 0.55f, 0.20f, 0.10f}}},   // 2290 MOD, 1/8T shuffle
    {{"AMBIENT", "AMBNT", {10, 0.5f, 1, 0.60f, 0.45f, 0.60f, 0.30f, 0.50f}},   // CRYSTAL, 1/4. shimmer
     {"SYNTHWAVE", "WAVE", {2, 0.5f, 4, 0.45f, 0.40f, 0.55f, 0.35f, 0.30f}},   // 2290 MOD, 1/8. chorus
     {"TRANCE", "TRANCE", {8, 0.5f, 4, 0.50f, 0.40f, 0.60f, 0.10f, 0.25f}},    // PING-PONG, 1/8. gate echoes
     {"LEAD", "LEAD", {3, 0.5f, 2, 0.40f, 0.35f, 0.45f, 0.20f, 0.15f}},        // ANALOG, 1/4 warm repeats
     {"PAD", "PAD", {11, 0.5f, 0, 0.55f, 0.45f, 0.40f, 0.25f, 0.60f}},         // SPACE, 1/2 wash
     {"ARP", "ARP", {0, 0.5f, 10, 0.40f, 0.35f, 0.55f, 0.10f, 0.20f}},         // CLASSIC, 1/8.+1/16 cascade
     {"PLUCK", "PLUCK", {8, 0.43f, 5, 0.35f, 0.35f, 0.65f, 0.05f, 0.15f}},     // PING-PONG, 1/8 bright bounce
     {"BASS", "BASS", {9, 0.35f, 5, 0.15f, 0.20f, 0.35f, 0.00f, 0.00f}}},      // SLAP ~52 ms, dark thickener
    {{"SHIMMER", "SHIMMR", {10, 0.62f, 1, 0.60f, 0.45f, 0.65f, 0.25f, 0.60f}}, // CRYSTAL, 1/4. octave bloom
     {"CATHEDRAL", "CATHDR", {11, 0.67f, 0, 0.55f, 0.40f, 0.45f, 0.20f, 0.75f}}, // SPACE, 1/2 huge wash
     {"GLACIER", "GLACIR", {2, 0.67f, 0, 0.60f, 0.40f, 0.70f, 0.45f, 0.50f}},  // 2290 MOD, 1/2 slow icy chorus
     {"DRONE", "DRONE", {4, 0.62f, 8, 0.62f, 0.45f, 0.35f, 0.40f, 0.55f}},     // TAPE, 1/4+1/8. dark smear
     {"SWELL", "SWELL", {7, 0.55f, 2, 0.45f, 0.50f, 0.50f, 0.15f, 0.45f}},     // REVERSE, 1/4 rising swells
     {"STARFIELD", "STARS", {8, 0.55f, 3, 0.55f, 0.35f, 0.75f, 0.20f, 0.40f}}, // PING-PONG, 1/4T sparkle
     {"HORIZON", "HRZN", {3, 0.62f, 1, 0.55f, 0.40f, 0.30f, 0.30f, 0.45f}},    // ANALOG, 1/4. warm distance
     {"TRAILER", "TRAILR", {6, 0.67f, 0, 0.55f, 0.50f, 0.40f, 0.10f, 0.65f}}}, // DYNAMIC, 1/2 blooms between hits
    {{"SKANK", "SKANK", {4, 0.50f, 4, 0.50f, 0.40f, 0.40f, 0.25f, 0.15f}},     // TAPE, 1/8. offbeat chops
     {"STEPPERS", "STEPPR", {11, 0.55f, 2, 0.55f, 0.40f, 0.35f, 0.20f, 0.25f}}, // SPACE, 1/4 heads
     {"ROOTS", "ROOTS", {3, 0.62f, 1, 0.55f, 0.40f, 0.30f, 0.20f, 0.20f}},     // ANALOG, 1/4. dark throws
     {"SIREN", "SIREN", {4, 0.43f, 5, 0.62f, 0.45f, 0.50f, 0.60f, 0.20f}},     // TAPE, 1/8 heavy wobble
     {"ROCKERS", "ROCKRS", {1, 0.48f, 3, 0.50f, 0.35f, 0.45f, 0.05f, 0.15f}},  // 2290, 1/4T tight repeats
     {"SPRING", "SPRING", {9, 0.55f, 2, 0.45f, 0.35f, 0.55f, 0.10f, 0.50f}},   // SLAP ~89 ms into the SPACE: a spring
     {"DUBSTEP", "DSTEP", {8, 0.36f, 6, 0.45f, 0.35f, 0.40f, 0.10f, 0.20f}},   // PING-PONG, 1/8T half-time
     {"ECHO CHAMBER", "CHAMBR", {11, 0.62f, 9, 0.50f, 0.40f, 0.40f, 0.25f, 0.40f}}}, // SPACE, 1/4+1/8T
    {{"CASSETTE", "CASSET", {4, 0.50f, 4, 0.40f, 0.40f, 0.30f, 0.50f, 0.10f}}, // TAPE, 1/8. warbly
     {"VHS", "VHS", {2, 0.43f, 5, 0.40f, 0.35f, 0.30f, 0.65f, 0.15f}},         // 2290 MOD, 1/8 seasick chorus
     {"8-BIT", "8-BIT", {5, 0.43f, 5, 0.40f, 0.35f, 0.60f, 0.00f, 0.00f}},     // LO-FI, 1/8 crunchy
     {"VINYL", "VINYL", {5, 0.55f, 2, 0.35f, 0.35f, 0.25f, 0.20f, 0.20f}},     // LO-FI, 1/4 dusty
     {"BUCKET BRIGADE", "BBD", {3, 0.48f, 3, 0.45f, 0.40f, 0.35f, 0.25f, 0.05f}}, // ANALOG, 1/4T murky
     {"AM RADIO", "RADIO", {5, 0.31f, 7, 0.30f, 0.30f, 0.45f, 0.10f, 0.05f}},  // LO-FI, 1/16 thin
     {"ROCKABILLY", "ROCKBL", {9, 0.62f, 2, 0.20f, 0.40f, 0.55f, 0.05f, 0.00f}}, // SLAP ~108 ms, one bright slap
     {"WORN TAPE", "WORN", {4, 0.62f, 1, 0.55f, 0.40f, 0.20f, 0.70f, 0.20f}}}, // TAPE, 1/4. dark, wobbling
    {{"DOTTED 8TH", "DOTTED", {1, 0.50f, 4, 0.45f, 0.40f, 0.60f, 0.00f, 0.05f}}, // 2290, 1/8. clean
     {"TRIPLET", "TRIPLT", {8, 0.36f, 6, 0.45f, 0.35f, 0.55f, 0.00f, 0.05f}},  // PING-PONG, 1/8T
     {"CASCADE", "CASCAD", {0, 0.50f, 10, 0.45f, 0.40f, 0.55f, 0.05f, 0.10f}}, // CLASSIC, 1/8.+1/16
     {"GALLOP", "GALLOP", {1, 0.55f, 8, 0.40f, 0.40f, 0.55f, 0.00f, 0.05f}},   // 2290, 1/4+1/8.
     {"SHUFFLE", "SHUFFL", {3, 0.55f, 9, 0.40f, 0.35f, 0.45f, 0.10f, 0.05f}},  // ANALOG, 1/4+1/8T
     {"16TH ROLL", "ROLL", {6, 0.31f, 7, 0.50f, 0.40f, 0.55f, 0.00f, 0.05f}},  // DYNAMIC, 1/16 ducked roll
     {"WIDE PING", "WIDE", {8, 0.55f, 2, 0.50f, 0.40f, 0.60f, 0.10f, 0.15f}},  // PING-PONG, 1/4
     {"QUARTER TRIPLET", "QTR-T", {0, 0.48f, 3, 0.45f, 0.40f, 0.50f, 0.05f, 0.10f}}}}; // CLASSIC, 1/4T

const char* const kLoopStateName[6] = {"EMPTY", "REC", "PLAY", "REPLACE", "STOP", "ONCE"};
const char* const kArmName[8] = {"", "> REC", "> PLAY", "> ONCE", "> PLAY", "> ONCE", "> STOP", "> DUB"};
const char* const kClockDivName[3] = {"1/16 (MATRIX steps)", "1/8", "1/4 (beats)"};

// LEN: beats on a tempo-locked loop; 1 / LEN of a free loop (defined after the module)
struct EchoLenQuantity : ParamQuantity {
    std::string getDisplayValueString() override;
};
// TEMPO: 40 .. 240 BPM, with FREE (the TIME knob sets the delay) at the bottom
const float kTempoFree = 39.f;
struct EchoTempoQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        float v = getValue();
        return v < kTempoFree + 0.5f ? "Free (the TIME knob)" : string::f("%.0f BPM", v);
    }
};
} // namespace

struct SynTHBloKEcho : Module {
    enum ParamId { TYPE_PARAM, TIME_PARAM, SUBDIV_PARAM, FEEDBACK_PARAM, LEVEL_PARAM, TONE_PARAM, MOD_PARAM, SPACE_PARAM,
                   LOOP_LEVEL_PARAM, TAP_PARAM, FREEZE_PARAM, KILLDRY_PARAM, ON_PARAM,
                   REC_PARAM, PLAY_PARAM, ONCE_PARAM, UNDO_PARAM, CLEAR_PARAM, QUANT_PARAM, PREFX_PARAM,
                   STORE_PARAM, ENUMS(SLOT_PARAM, 6), LEN_PARAM, LENDN_PARAM, LENUP_PARAM, BANK_PARAM, MASTER_PARAM, LENKNOB_PARAM, COUNTIN_PARAM, PAGE_PARAM,
                   ENUMS(SLOT2_PARAM, 5),   // USER slots 7-11 (added after the rest: old patches keep their ids)
                   TEMPO_PARAM, PARAMS_LEN };   // the tempo with no clock (bottom = FREE: the TIME knob)
    enum InputId { IN_L_INPUT, IN_R_INPUT, CLOCK_INPUT, TAP_INPUT, FREEZE_INPUT, BYPASS_INPUT,
                   REC_INPUT, PLAY_INPUT, ONCE_INPUT, UNDO_INPUT, CLEAR_INPUT, INPUTS_LEN };
    enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN };
    enum LightId { LIGHTS_LEN };
    static const int kPresetN = 8;   // a preset = the first 8 params (TYPE .. SPACE)
    static const int kSlots = 11;   // USER holds 11, the factory banks kGenreN
    static int slotParam(int s) { return s < 6 ? SLOT_PARAM + s : SLOT2_PARAM + s - 6; }

    EchoEngine echo;
    EchoLooper looper;
    M8Reverb space;
    dsp::SchmittTrigger clockTrig, trig[6];   // TAP, REC, PLAY, ONCE, UNDO, CLEAR inputs
    bool btnPrev[8] = {}, slotPrev[kSlots] = {};
    float slots[kSlots][kPresetN] = {};
    bool slotUsed[kSlots] = {};
    bool slotMine[kSlots] = {};   // stored into by STORE (not a factory default): drawn darker orange
    int curSlot = -1, curBank = 0;   // the preset last loaded or stored, and its bank
    float slotLit[kSlots] = {};
    // DRUMS voicing of the repeats
    float drumSm = 0.f, duckEnv = 0.f, hpL = 0.f, hpR = 0.f;

    int curType = ET_CLASSIC;
    float typeGain = 1.f;
    int clockDiv = 0, clockCount = 0;          // clock pulse = 1/16, 1/8 or 1/4
    float sinceClock = 10.f, clockPeriod = 0.f, sinceBeatClk = 10.f, clkBeat = 0.f;
    float sinceTap = 10.f, tapBeat = 0.f, tapKnob = 0.f, tapIv[3] = {};
    int tapN = 0;
    bool tapActive = false, trails = true;
    float beatPhase = 0.f, tapFlash = 0.f, recFlash = 0.f, playFlash = 0.f, onceFlash = 0.f, blink = 0.f;
    float masterSm = 1.26f;
    float onSm = 1.f, drySm = 1.f, freezeSm = 0.f, levelSm = 0.5f, spaceSm = 0.f, loopSm = 1.f;
    int barPos = 0;   // the beat of the bar that comes next
    // for the display
    float timeNow = 0.375f, beatNow = 0.f;
    int source = 0;   // 0 free (TIME knob), 1 tap, 2 clock, 3 the TEMPO knob
    bool onNow = true, freezeNow = false;

    SynTHBloKEcho() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configSwitch(TYPE_PARAM, 0.f, ET_NUM - 1, ET_CLASSIC, "Delay type",
                     {"CLASSIC (MATRIX ping-pong)", "2290 (clean digital)", "2290 MOD (chorused)", "ANALOG (BBD)", "TAPE (wow, flutter)",
                      "LO-FI (crushed)", "DYNAMIC (ducking)", "REVERSE", "PING-PONG", "SLAP (20-300 ms)", "CRYSTAL (octave shimmer)",
                      "SPACE (multi-head echo + reverb)"});
        configParam(TIME_PARAM, 0.f, 1.f, 0.5f, "Delay time (no clock and TEMPO at FREE; SLAP: 20-300 ms)", " ms", 350.f, 20.f);
        std::vector<std::string> subs;
        for (const EchoSub& s : kEchoSub) subs.push_back(s.name);
        configSwitch(SUBDIV_PARAM, 0.f, 10.f, 4.f, "Subdivision of the clock / tapped beat", subs);
        configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.5f, "Feedback", "%", 0.f, 100.f);
        configParam(LEVEL_PARAM, 0.f, 1.f, 0.5f, "Delay mix (wet level; dry stays at unity)", "%", 0.f, 100.f);
        configParam(TONE_PARAM, 0.f, 1.f, 0.5f, "Tone (dark - bright)", "%", 0.f, 100.f);
        configParam(MOD_PARAM, 0.f, 1.f, 0.f, "Modulation", "%", 0.f, 100.f);
        configParam(SPACE_PARAM, 0.f, 1.f, 0.f, "Space (reverb on the repeats)", "%", 0.f, 100.f);
        configParam(LOOP_LEVEL_PARAM, 0.f, 1.f, 1.f, "Loop level", "%", 0.f, 100.f);
        configButton(TAP_PARAM, "Tap tempo");
        configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze (hold the repeats)", {"Off", "On"});
        configSwitch(KILLDRY_PARAM, 0.f, 1.f, 0.f, "Kill dry", {"Off", "On"});
        configSwitch(ON_PARAM, 0.f, 1.f, 1.f, "Delay", {"Bypass", "On"});
        configButton(REC_PARAM, "Loop: record / replace (from the top of the loop)");
        configButton(PLAY_PARAM, "Loop: play / stop");
        configButton(ONCE_PARAM, "Loop: play once");
        configButton(UNDO_PARAM, "Loop: undo / redo the last replace");
        configButton(CLEAR_PARAM, "Loop: clear");
        configParam(QUANT_PARAM, 0.f, 2.f, 1.f, "(not used: the looper always works to the beat)");
        configSwitch(PREFX_PARAM, 0.f, 1.f, 0.f, "Looper position", {"After the delay", "Before the delay (loops get echoed)"});
        configSwitch(STORE_PARAM, 0.f, 1.f, 0.f, "Store (then click a preset slot)", {"Off", "On"});
        for (int i = 0; i < kSlots; i++) configButton(slotParam(i), string::f("Preset %d (click: load; STORE lit: save)", i + 1));
        configParam<EchoLenQuantity>(LEN_PARAM, 0.f, 256.f, 0.f, "Record length (with a clock; free: until REC again)");
        getParamQuantity(LEN_PARAM)->snapEnabled = true;
        configButton(LENDN_PARAM, "(not used)");   // the old /2 and x2 cells and LENGTH knob: ids kept for old patches
        configButton(LENUP_PARAM, "(not used)");
        // the final stage: a little boost by default (+2 dB), up to +6 dB, softly held under 10 V
        configParam(MASTER_PARAM, 0.f, 2.f, 1.26f, "Master output (OUT L / R)", " dB", -10.f, 20.f);
        configParam(LENKNOB_PARAM, 0.f, 1.f, 1.f, "(not used)");
        configSwitch(BANK_PARAM, 0.f, kBanks - 1, 0.f, "Preset bank",
                     {"User (STORE saves here)", "Drums (genre presets; repeats tucked under the beat)", "Synth (genre presets)",
                      "Cinematic (ambient)", "Dub (reggae)", "Lo-fi (vintage)", "Rhythm (ping, dotted, triplets)"});
        configParam<EchoTempoQuantity>(TEMPO_PARAM, kTempoFree, 240.f, 120.f, "Tempo (with no clock)");
        getParamQuantity(TEMPO_PARAM)->snapEnabled = true;
        configSwitch(COUNTIN_PARAM, 0.f, 4.f, 0.f, "Count-in before a new take (with a clock)",
                     {"Off (starts on the next beat)", "1 beat", "2 beats", "3 beats", "4 beats"});
        // MODE: which page the screen's readout shows: the delay types, or a bank's presets (that bank is the one used)
        configSwitch(PAGE_PARAM, 0.f, kBanks, 0.f, "Mode (the readout beside it)",
                     {"Classics (delay types)", "User presets", "Drums presets", "Synth presets", "Cinematic presets", "Dub presets",
                      "Lo-fi presets", "Rhythm presets"});
        for (int id : {FREEZE_PARAM, KILLDRY_PARAM, ON_PARAM, QUANT_PARAM, PREFX_PARAM, STORE_PARAM, TYPE_PARAM, LEN_PARAM, BANK_PARAM, LENKNOB_PARAM, COUNTIN_PARAM, PAGE_PARAM, TEMPO_PARAM})
            getParamQuantity(id)->randomizeEnabled = false;
        configInput(IN_L_INPUT, "Left / mono");
        configInput(IN_R_INPUT, "Right (normalled to left)");
        configInput(CLOCK_INPUT, "Clock (sets the time with SUBDIV)");
        configInput(TAP_INPUT, "Tap tempo trigger");
        configInput(FREEZE_INPUT, "Freeze gate");
        configInput(BYPASS_INPUT, "Bypass gate");
        configInput(REC_INPUT, "Loop record / replace trigger");
        configInput(PLAY_INPUT, "Loop play / stop trigger");
        configInput(ONCE_INPUT, "Loop play once trigger");
        configInput(UNDO_INPUT, "Loop undo / redo trigger");
        configInput(CLEAR_INPUT, "Loop clear trigger");
        configOutput(OUT_L_OUTPUT, "Left");
        configOutput(OUT_R_OUTPUT, "Right");
        configBypass(IN_L_INPUT, OUT_L_OUTPUT);
        configBypass(IN_R_INPUT, OUT_R_OUTPUT);

        // factory presets: TYPE, TIME, SUBDIV, FEEDBACK, LEVEL, TONE, MOD, SPACE
        static const float kFactory[][kPresetN] = {
            {ET_2290, 0.5f, 2, 0.4f, 0.5f, 0.55f, 0.f, 0.f},     {ET_TAPE, 0.5f, 4, 0.45f, 0.55f, 0.45f, 0.3f, 0.1f},
            {ET_ANALOG, 0.5f, 5, 0.55f, 0.5f, 0.4f, 0.2f, 0.f},  {ET_REVERSE, 0.5f, 2, 0.35f, 0.6f, 0.5f, 0.f, 0.2f},
            {ET_CRYSTAL, 0.5f, 1, 0.6f, 0.45f, 0.6f, 0.2f, 0.4f}, {ET_SPACE, 0.5f, 4, 0.5f, 0.5f, 0.45f, 0.3f, 0.3f}};
        static const int kFactoryN = sizeof(kFactory) / sizeof(kFactory[0]);   // the rest start empty
        for (int s = 0; s < kFactoryN; s++) {
            for (int i = 0; i < kPresetN; i++) slots[s][i] = kFactory[s][i];
            slotUsed[s] = true;
        }
        looper.init(APP->engine->getSampleRate(), 60.f);
        looper.replace = true;   // REC on a playing loop writes over it
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        if (e.sampleRate != looper.sr) looper.init(e.sampleRate, 60.f);
    }

    void onReset() override {
        looper.clear();
        curSlot = -1;
        tapActive = false;
    }

    int bank() { return clamp((int)params[BANK_PARAM].getValue(), 0, kBanks - 1); }
    static int bankSlots(int b) { return b > 0 ? kGenreN : kSlots; }
    // the preset values of a slot in a bank (NULL: an empty user slot, or past the end of a factory bank)
    const float* preset(int b, int s) {
        if (s < 0 || s >= bankSlots(b)) return NULL;
        if (b > 0) return kGenre[b - 1][s].v;
        return slotUsed[s] ? slots[s] : NULL;
    }
    void loadSlot(int s) {
        const float* v = preset(bank(), s);
        if (!v) return;
        for (int i = 0; i < kPresetN; i++) params[i].setValue(v[i]);
        if (tapActive) tapKnob = params[TIME_PARAM].getValue();   // global tempo: a preset keeps the tapped tempo
        curSlot = s; curBank = bank();
    }
    void storeSlot(int s) {   // always into USER
        for (int i = 0; i < kPresetN; i++) slots[s][i] = params[i].getValue();
        slotUsed[s] = slotMine[s] = true;
        curSlot = s; curBank = 0;
    }

    bool press(int btn, int param, int input) {   // a cell's press or a trigger on its jack
        bool b = params[param].getValue() > 0.5f;
        bool edge = b && !btnPrev[btn];
        btnPrev[btn] = b;
        if (input >= 0 && trig[btn].process(inputs[input].getVoltage(), 0.1f, 1.f)) edge = true;
        return edge;
    }

    void process(const ProcessArgs& args) override {
        const float sr = args.sampleRate, dt = args.sampleTime;
        auto smooth = [&](float& v, float target, float tau) { v += (target - v) * (1.f - std::exp(-dt / tau)); };

        // ---- tempo: clock wins, then tap (until the TIME knob moves), else the TIME knob ----
        static const float kPpq[3] = {4.f, 2.f, 1.f};
        const int ppq = (int)kPpq[clockDiv];
        sinceClock += dt;
        bool clocked = inputs[CLOCK_INPUT].isConnected();
        bool clockEdge = false, pulseBeat = false, restart = false;
        sinceBeatClk += dt;
        if (clocked && clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
            restart = sinceClock >= std::max(2.f, 3.f * clockPeriod);
            if (restart) clockCount = 0;   // (re)start: a downbeat
            if (sinceClock < 4.f) clockPeriod = sinceClock;
            sinceClock = 0.f;
            clockEdge = true;
            pulseBeat = clockCount % ppq == 0;
            if (pulseBeat) {
                // the beat from beat pulse to beat pulse: steady under a swung 1/16 or 1/8 clock
                if (!restart && sinceBeatClk < 8.f) clkBeat = sinceBeatClk;
                sinceBeatClk = 0.f;
                barPos = clockCount / ppq % 4;   // the bar counts from the downbeat, even before the tempo is known
            }
            clockCount++;
        }
        bool clockOn = clocked && clockPeriod > 0.f && sinceClock < std::max(2.f, 3.f * clockPeriod);
        bool tapped = false;
        if (press(0, TAP_PARAM, TAP_INPUT)) {
            if (sinceTap > 0.05f && sinceTap < 2.5f) {
                tapIv[tapN % 3] = sinceTap;
                tapN++;
                int n = std::min(tapN, 3);
                float sum = 0.f;
                for (int i = 0; i < n; i++) sum += tapIv[i];
                tapBeat = sum / n;
                tapActive = true;
                tapKnob = params[TIME_PARAM].getValue();
                beatPhase = 0.f;
                tapped = true;
            } else tapN = 0;
            sinceTap = 0.f;
        }
        sinceTap += dt;
        if (tapActive && std::fabs(params[TIME_PARAM].getValue() - tapKnob) > 0.01f) tapActive = false;
        // no clock: the TEMPO knob, unless it is down at FREE
        float tempo = params[TEMPO_PARAM].getValue();
        bool internal = !clockOn && !tapActive && tempo >= kTempoFree + 0.5f;
        float beat = clockOn ? (clkBeat > 0.f ? clkBeat : clockPeriod * kPpq[clockDiv]) : tapActive ? tapBeat
                   : internal ? 60.f / tempo : 0.f;
        source = clockOn ? 2 : tapActive ? 1 : internal ? 3 : 0;
        beatNow = beat;

        // ---- type change: fade the repeats out, switch, fade in ----
        int want = clamp((int)params[TYPE_PARAM].getValue(), 0, ET_NUM - 1);
        if (want != curType) { typeGain -= dt / 0.012f; if (typeGain <= 0.f) { typeGain = 0.f; curType = want; } }
        else typeGain = std::min(1.f, typeGain + dt / 0.012f);

        // ---- delay time ----
        int sub = clamp((int)params[SUBDIV_PARAM].getValue(), 0, 10);
        float kv = params[TIME_PARAM].getValue();
        float T, T2 = 0.f;
        if (curType == ET_SLAP) T = 0.02f * std::pow(15.f, kv);
        else if (beat > 0.f) {
            T = beat * kEchoSub[sub].a; T2 = beat * kEchoSub[sub].b;
            while (T > 7.f) { T *= 0.5f; T2 *= 0.5f; }
        } else {
            T = 0.02f * std::pow(350.f, kv);
            if (kEchoSub[sub].b > 0.f) T2 = T * kEchoSub[sub].b / kEchoSub[sub].a;
        }
        timeNow = T;

        // ---- the beat: TAP lights on each beat (each repeat when free running) ----
        float period = beat > 0.f ? beat : T;
        bool beatEdge = false;
        if (clockOn) beatEdge = clockEdge && pulseBeat;
        else if (tapped) { beatEdge = true; barPos = 0; }   // the last tap is a downbeat
        else {
            beatPhase += dt;
            if (beatPhase >= period) { beatPhase -= period; beatEdge = true; }
        }
        if (beatEdge) tapFlash = 0.06f;
        tapFlash -= dt;
        beatEdge = beatEdge && beat > 0.f;

        // ---- looper grid: every beat (QUANT_PARAM is no longer on the screen; its id stays for old patches) ----
        const int grid = 1;
        bool gridEdge = false;
        if (beatEdge) { gridEdge = grid == 1 || barPos == 0; barPos = (barPos + 1) % 4; }
        looper.tempoBeat = beat > 0.f ? beat * sr : 0.f;   // FREE grid: takes start on a beat, lengths in whole beats
        looper.countIn = clamp((int)std::round(params[COUNTIN_PARAM].getValue()), 0, 4);
        looper.gridBeats = grid == 2 ? 4 : 1;
        looper.sync(beatEdge, gridEdge, grid > 0 && beat > 0.f ? beat * sr : 0.f, restart && beatEdge);

        // ---- switches ----
        freezeNow = params[FREEZE_PARAM].getValue() > 0.5f || inputs[FREEZE_INPUT].getVoltage() > 1.f;
        onNow = params[ON_PARAM].getValue() > 0.5f && !(inputs[BYPASS_INPUT].getVoltage() > 1.f);
        smooth(freezeSm, freezeNow ? 1.f : 0.f, 0.01f);
        smooth(onSm, onNow ? 1.f : 0.f, 0.01f);
        smooth(drySm, params[KILLDRY_PARAM].getValue() > 0.5f ? 0.f : 1.f, 0.01f);
        smooth(levelSm, params[LEVEL_PARAM].getValue(), 0.01f);
        smooth(spaceSm, params[SPACE_PARAM].getValue(), 0.01f);
        smooth(loopSm, params[LOOP_LEVEL_PARAM].getValue(), 0.01f);

        // ---- presets ----
        // MODE on a preset page uses that bank; STORE saves to USER (the page follows)
        int page = clamp((int)std::round(params[PAGE_PARAM].getValue()), 0, kBanks);
        if (params[STORE_PARAM].getValue() > 0.5f && page != 1) { params[PAGE_PARAM].setValue(1.f); page = 1; }
        if (page > 0 && bank() != page - 1) params[BANK_PARAM].setValue((float)(page - 1));
        for (int s = 0; s < kSlots; s++) {
            bool b = params[slotParam(s)].getValue() > 0.5f;
            if (b && !slotPrev[s]) {
                if (params[STORE_PARAM].getValue() > 0.5f) { storeSlot(s); params[STORE_PARAM].setValue(0.f); }
                else loadSlot(s);
            }
            slotPrev[s] = b;
            slotLit[s] = s == curSlot && curBank == bank() ? 1.f : 0.f;
        }

        // ---- looper transport (on the grid when there is a tempo) and length ----
        if (press(1, REC_PARAM, REC_INPUT)) looper.press(EchoLooper::B_REC);
        if (press(2, PLAY_PARAM, PLAY_INPUT)) looper.press(EchoLooper::B_PLAY);
        if (press(3, ONCE_PARAM, ONCE_INPUT)) looper.press(EchoLooper::B_ONCE);
        if (press(4, UNDO_PARAM, UNDO_INPUT)) looper.undo();
        if (press(5, CLEAR_PARAM, CLEAR_INPUT)) looper.clear();
        // LEN (the FREE | 1-4 BT | 1-4 BAR cells) is the record length: every new take (with a clock) records
        // that many beats from its first beat, then plays; FREE records until REC / PLAY / ONCE. Loops play in full.
        looper.setLoopBeats(0);
        looper.recBeats = std::max(0, (int)std::round(params[LEN_PARAM].getValue()));
        // ---- audio (internal units: +-1 = +-5 V) ----
        float inL = inputs[IN_L_INPUT].getVoltage() / 5.f;
        float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() / 5.f : inL;
        bool pre = params[PREFX_PARAM].getValue() > 0.5f;
        float lL = 0.f, lR = 0.f;
        if (pre) looper.process(inL, inR, lL, lR);
        lL *= loopSm; lR *= loopSm;

        EchoParams p;
        p.type = curType; p.time = T; p.time2 = T2;
        smooth(drumSm, bank() == 1 ? 1.f : 0.f, 0.02f);
        p.fb = std::min(params[FEEDBACK_PARAM].getValue(), 1.f - 0.35f * drumSm);   // DRUMS: repeats never build up
        p.tone = params[TONE_PARAM].getValue();
        p.mod = params[MOD_PARAM].getValue();
        p.freeze = freezeSm;
        float wL, wR, rl, rr;
        echo.process((inL + lL) * onSm, (inR + lR) * onSm, p, sr, wL, wR);   // bypass shuts the input: the tails ring on
        wL *= typeGain; wR *= typeGain;
        space.process(wL, wR, sr, rl, rr);
        wL += rl * spaceSm; wR += rr * spaceSm;
        float wg = levelSm * (trails ? 1.f : onSm);
        wL *= wg; wR *= wg;
        // DRUMS: the repeats sit under the beat: low cut at 160 Hz (the kick stays dry and tight), ducked
        // half way down by the hits (2 ms attack, 150 ms release), and a soft ceiling from 4 V so they never overload
        {
            float lp = 1.f - std::exp(-2.f * (float)M_PI * 160.f * dt);
            hpL += (wL - hpL) * lp; hpR += (wR - hpR) * lp;
            float x = std::max(std::fabs(inL + lL), std::fabs(inR + lR));
            duckEnv += (x - duckEnv) * (1.f - std::exp(-dt / (x > duckEnv ? 0.002f : 0.15f)));
            if (drumSm > 1e-3f) {
                float duck = 1.f - 0.5f * std::min(1.f, 1.5f * duckEnv);
                auto softCeil = [](float v) {
                    float a = std::fabs(v);
                    return a <= 0.8f ? v : std::copysign(0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f), v);
                };
                float vL = softCeil((wL - hpL) * duck), vR = softCeil((wR - hpR) * duck);
                wL += (vL - wL) * drumSm; wR += (vR - wR) * drumSm;
            }
        }
        float oL = inL * drySm + lL + wL, oR = inR * drySm + lR + wR;
        if (!pre) {
            looper.process(oL, oR, lL, lR);
            oL += lL * loopSm; oR += lR * loopSm;
        }
        // master: gain, then a soft ceiling from 8 V (never past 10 V) so the boost can't clip hard
        smooth(masterSm, params[MASTER_PARAM].getValue(), 0.01f);
        auto outCeil = [](float v) {
            float a = std::fabs(v);
            return a <= 8.f ? v : std::copysign(8.f + 2.f * std::tanh((a - 8.f) / 2.f), v);
        };
        outputs[OUT_L_OUTPUT].setVoltage(outCeil(5.f * oL * masterSm));
        outputs[OUT_R_OUTPUT].setVoltage(outCeil(5.f * oR * masterSm));

        // cells: lit with the state; an armed action blinks its cell until the grid edge
        blink += dt;
        if (blink >= 0.25f) blink -= 0.25f;
        float bl = blink < 0.125f ? 1.f : 0.f;
        EchoLooper::State st = looper.st;
        int a = looper.arm;
        recFlash = st == EchoLooper::REC || st == EchoLooper::DUB ? 1.f : 0.f;
        playFlash = st == EchoLooper::PLAY || st == EchoLooper::DUB ? 1.f : 0.f;
        onceFlash = st == EchoLooper::ONCE ? 1.f : 0.f;
        if (a == EchoLooper::A_REC || a == EchoLooper::A_DUB) recFlash = bl;
        if (looper.counting()) recFlash = looper.sinceBeat < 0.25f * looper.tempoBeat ? 1.f : 0.f;   // the count: lit on each beat
        if (a == EchoLooper::A_CLOSE || a == EchoLooper::A_PLAY || a == EchoLooper::A_STOP) playFlash = bl;
        if (a == EchoLooper::A_CLOSE_ONCE || a == EchoLooper::A_ONCE) onceFlash = bl;
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_t* arr = json_array();
        for (int s = 0; s < kSlots; s++) {
            if (!slotUsed[s]) { json_array_append_new(arr, json_null()); continue; }
            json_t* v = json_array();
            for (int i = 0; i < kPresetN; i++) json_array_append_new(v, json_real(slots[s][i]));
            json_array_append_new(arr, v);
        }
        json_object_set_new(root, "presets", arr);
        json_t* mine = json_array();
        for (int s = 0; s < kSlots; s++) json_array_append_new(mine, json_boolean(slotMine[s]));
        json_object_set_new(root, "stored", mine);
        json_object_set_new(root, "curSlot", json_integer(curSlot));
        json_object_set_new(root, "curBank", json_integer(curBank));
        json_object_set_new(root, "clockDiv", json_integer(clockDiv));
        json_object_set_new(root, "trails", json_boolean(trails));
        json_object_set_new(root, "tempoKnob", json_true());
        return root;
    }
    void dataFromJson(json_t* root) override {
        if (json_t* arr = json_object_get(root, "presets")) {
            for (int s = 0; s < kSlots && s < (int)json_array_size(arr); s++) {
                json_t* v = json_array_get(arr, s);
                slotUsed[s] = json_is_array(v) && json_array_size(v) == (size_t)kPresetN;
                if (slotUsed[s])
                    for (int i = 0; i < kPresetN; i++) slots[s][i] = (float)json_number_value(json_array_get(v, i));
            }
        }
        if (json_t* mine = json_object_get(root, "stored"))
            for (int s = 0; s < kSlots && s < (int)json_array_size(mine); s++)
                slotMine[s] = slotUsed[s] && json_is_true(json_array_get(mine, s));
        if (json_t* j = json_object_get(root, "curSlot")) curSlot = clamp((int)json_integer_value(j), -1, kSlots - 1);
        if (json_t* j = json_object_get(root, "curBank")) curBank = clamp((int)json_integer_value(j), 0, kBanks - 1);
        if (json_t* j = json_object_get(root, "clockDiv")) clockDiv = clamp((int)json_integer_value(j), 0, 2);
        if (json_t* j = json_object_get(root, "trails")) trails = json_boolean_value(j);
        // a patch from before the TEMPO knob: keep it free running, as it was
        if (!json_object_get(root, "tempoKnob")) params[TEMPO_PARAM].setValue(kTempoFree);
    }
};

typedef SynTHBloKEcho EC;

namespace {
std::string EchoLenQuantity::getDisplayValueString() {
    int v = (int)getValue();
    return v <= 0 ? "Free (record until REC / PLAY)" : string::f("Record %d beat%s", v, v == 1 ? "" : "s");
}
} // namespace

// ------------------------------------------------------------ widgets ------------------------------------------------------------

namespace {

// Value arc around a knob (as on MATRIX-DRUM): amber track, orange arc.
struct EchoRing : TransparentWidget {
    Module* module = NULL;
    int param = 0;
    float width = 1.8f;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float v = module ? module->paramQuantities[param]->getScaledValue() : 0.5f;
        float cx = box.size.x * 0.5f, cy = box.size.y * 0.5f, rr = box.size.x * 0.5f - width * 0.8f;
        float a0 = M_PI * 0.75f, a1 = M_PI * 2.25f;
        nvgBeginPath(args.vg);
        nvgArc(args.vg, cx, cy, rr, a0, a1, NVG_CW);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 40));
        nvgStrokeWidth(args.vg, width);
        nvgStroke(args.vg);
        if (v > 0.001f) {
            nvgBeginPath(args.vg);
            nvgArc(args.vg, cx, cy, rr, a0, a0 + (a1 - a0) * v, NVG_CW);
            nvgStrokeColor(args.vg, nvgRGBA(255, 112, 15, 235));
            nvgStrokeWidth(args.vg, width);
            nvgStroke(args.vg);
        }
    }
};

struct EchoLabel : TransparentWidget {
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

struct EchoTitle : TransparentWidget {
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
        nvgText(args.vg, 0.f, mm2px(6.4f), "MATRIX-ECHO", NULL);
    }
};

// Dim name at the left of a line, amber value at its right (fonts must be set).
void echoLine(const widget::Widget::DrawArgs& args, float x0, float x1, float yc, const char* name, const char* val,
              NVGcolor col = oled::amber(), float valSize = 7.5f) {
    nvgFontSize(args.vg, 6.f);
    nvgFillColor(args.vg, oled::dim());
    nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(args.vg, x0 + 4.f, yc, name, NULL);
    nvgFontSize(args.vg, valSize);
    nvgFillColor(args.vg, col);
    nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgText(args.vg, x1 - 4.f, yc, val, NULL);
}

// The screen, top to bottom (mm from its top edge). Info rows are drawn by EchoScreen; the cell rows are
// oled::Cells laid over it by the widget, on a 6-column grid where it fits.
namespace es {
const float kGap = 0.9f;                                   // cell row gap
// the looper on top
const float yLoop = 0.f;                                   // LOOP line
const float kWaveH = 14.4f;
const float yWave = yLoop + oled::kRow;                    // the loop's waveform
// under the waveform: two rows at the left, the record-length block (3 x 3) at the right beside both
const float yGrid = yWave + kWaveH + 0.5f;                 // COUNT | LOOP BEFORE DELAY
const float yTrans = yGrid + oled::kCellH + kGap;          // REC | PLAY | ONCE | UNDO | CLEAR, tall cells
const float kTransH = 2.f * oled::kCellH + kGap;           // as tall as two rows: near square
const float kLenW = 30.f;                                  // the record-length block's width
// the delay below
const float yInfo = yTrans + kTransH + 0.8f;               // TIME | SUBDIV | tempo
const float yPage = yInfo + oled::kRow;                    // the MODE page and what is chosen on it
// left: FREEZE | KILL DRY over ON | MODE (as big as REC / PLAY); right: the page's cells, 3 rows
const float yBlk = yPage + oled::kRow + 0.3f;
const float kBlkH = 3.f * oled::kCellH + 2.f * kGap;
const float kH = yBlk + kBlkH + 0.5f;
const float kPad = 2.f, kCG = 1.f;
}

struct EchoScreen : TransparentWidget {
    EC* module = NULL;
    float zoom = 1.f, view0 = 0.f;   // waveform view: zoom factor and its start (fraction of the loop)
    float col6(float W, int i) const { return mm2px(es::kPad) + i * (cw6(W) + mm2px(es::kCG)); }
    float cw6(float W) const { return (W - mm2px(2.f * es::kPad) - 5.f * mm2px(es::kCG)) / 6.f; }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        const float R = mm2px(oled::kRow);
        const float yL = mm2px(es::yLoop), yW = mm2px(es::yWave), yWB = mm2px(es::yWave + es::kWaveH);
        const float yI = mm2px(es::yInfo);
        nvgBeginPath(args.vg);
        oled::divider(args, 4.f, yI - mm2px(0.4f), W - 4.f, yI - mm2px(0.4f));   // looper above, delay below
        oled::divider(args, 4.f, yI + R, W - 4.f, yI + R);
        oled::divider(args, 4.f, mm2px(es::yBlk - 0.15f), W - 4.f, mm2px(es::yBlk - 0.15f));   // under the MODE page line
        oled::divider(args, W / 3.f, yI + 2.f, W / 3.f, yI + R - 2.f);
        oled::divider(args, 2.f * W / 3.f, yI + 2.f, 2.f * W / 3.f, yI + R - 2.f);
        oled::divider(args, 4.f, yW, W - 4.f, yW);
        oled::divider(args, 4.f, yWB, W - 4.f, yWB);
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W, H);
        drawDelay(args, W, yI + R * 0.5f);
        drawPage(args, W, mm2px(es::yPage + oled::kRow * 0.5f));
        drawLoop(args, W, yL, yW, yWB);
        nvgResetScissor(args.vg);
    }

    // the MODE page: TYPE and the delay type, or the bank and its loaded preset (or the STORE prompt)
    // (the title a little bigger and brighter than the other lines, in pale gold, so the page stands out)
    void pageLine(const DrawArgs& args, float W, float y, const char* title, const char* val, NVGcolor col) {
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0xe4, 0x9a));
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, 4.f, y, title, NULL);
        nvgFontSize(args.vg, 8.f);
        nvgFillColor(args.vg, col);
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, W - 4.f, y, val, NULL);
    }
    void drawPage(const DrawArgs& args, float W, float y) {
        if (!module) { pageLine(args, W, y, "CLASSICS", "2290", oled::amber()); return; }
        int page = clamp((int)std::round(module->params[EC::PAGE_PARAM].getValue()), 0, kBanks);
        if (module->params[EC::STORE_PARAM].getValue() > 0.5f) {
            pageLine(args, W, y, "STORE", "CLICK A PRESET TO SAVE INTO", oled::litFill());
            return;
        }
        if (page == 0) {
            pageLine(args, W, y, "CLASSICS", kEchoTypeName[clamp((int)module->params[EC::TYPE_PARAM].getValue(), 0, ET_NUM - 1)], oled::amber());
            return;
        }
        char val[48] = "--";
        int b = page - 1;
        if (module->curBank == b && module->curSlot >= 0 && module->preset(b, module->curSlot)) {
            const float* s = module->preset(b, module->curSlot);
            const char* type = kEchoTypeName[clamp((int)s[0], 0, ET_NUM - 1)];
            const char* sub = kEchoSub[clamp((int)s[2], 0, 10)].name;
            if (b > 0) snprintf(val, sizeof val, "%s  %s  %s", kGenre[b - 1][module->curSlot].name, type, sub);
            else snprintf(val, sizeof val, "%d  %s  %s", module->curSlot + 1, type, sub);
        }
        pageLine(args, W, y, kBankName[b], val, oled::amber());
    }

    // TIME | SUBDIV (click / scroll steps) | the tempo, with the beat of the bar
    void drawDelay(const DrawArgs& args, float W, float y) {
        float T = module ? module->timeNow : 0.375f;
        char tm[24], src[32] = "--- BPM", sub[16] = "1/8.";
        if (T < 1.f) snprintf(tm, sizeof tm, "%.0f ms", T * 1000.f);
        else snprintf(tm, sizeof tm, "%.2f s", T);
        NVGcolor srcCol = oled::dim();
        bool tempo = false;
        if (module) {
            if (module->source > 0) {   // the clock's tempo
                snprintf(src, sizeof src, "%.1f BPM", 60.f / module->beatNow);
                srcCol = oled::amber();
                tempo = true;
            }
            snprintf(sub, sizeof sub, "%s", kEchoSub[clamp((int)module->params[EC::SUBDIV_PARAM].getValue(), 0, 10)].name);
        }
        const char* tname = "TIME";
        NVGcolor tc = oled::amber();
        if (module && module->freezeNow) { tname = "FREEZE"; tc = oled::litFill(); }
        else if (module && !module->onNow) { tname = "BYPASS"; tc = oled::dim(); }
        echoLine(args, 0.f, W / 3.f, y, tname, tm, tc);
        echoLine(args, W / 3.f, 2.f * W / 3.f, y, "SUBDIV", sub);
        // four beat squares, the current beat lit
        float x = 2.f * W / 3.f + 4.f;
        int cur = module ? (module->barPos + 3) % 4 : 0;
        for (int k = 0; k < 4; k++) {
            nvgBeginPath(args.vg);
            nvgRect(args.vg, x + k * 4.5f, y - 1.5f, 3.f, 3.f);
            nvgFillColor(args.vg, tempo && k == cur ? oled::litFill() : oled::outline(tempo ? 90 : 40));
            nvgFill(args.vg);
        }
        nvgFontSize(args.vg, 8.f);   // the headline value (as BPM on the other OLEDs)
        nvgFillColor(args.vg, srcCol);
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, W - 4.f, y, src, NULL);
    }

    // LOOP state | armed action | layers and length; the waveform with the playing part, beats and playhead; LEN
    void drawLoop(const DrawArgs& args, float W, float yL, float yW, float yWB) {
        EchoLooper* lp = module ? &module->looper : NULL;
        int st = lp ? (int)lp->st : 0;
        char info[40] = "60 s";
        if (lp && lp->st == EchoLooper::REC) {
            if (lp->locked) snprintf(info, sizeof info, "%d BT  %.2f s", (int)(lp->pos / lp->curBeat) + 1, lp->pos / lp->sr);
            else snprintf(info, sizeof info, "%.2f s", lp->pos / lp->sr);
        } else if (lp && lp->len > 0) {
            int L = lp->playLen > 0 ? lp->playLen : lp->len;
            snprintf(info, sizeof info, "x%d  %.2f s", lp->layers, L / lp->sr);
        }
        NVGcolor stc = st == EchoLooper::REC || st == EchoLooper::DUB ? oled::litFill()
                     : st == EchoLooper::PLAY || st == EchoLooper::ONCE ? oled::green() : oled::dim();
        float y0 = yL + mm2px(oled::kRow) * 0.5f;
        nvgFontSize(args.vg, 6.f);
        nvgFillColor(args.vg, oled::dim());
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, 4.f, y0, "LOOP", NULL);
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, stc);
        nvgText(args.vg, mm2px(7.5f), y0, kLoopStateName[st], NULL);
        if (lp && lp->counting()) {   // the count-in: beats left until the take starts
            char cnt[16];
            snprintf(cnt, sizeof cnt, "COUNT %d", lp->counting());
            nvgFillColor(args.vg, oled::litFill());
            nvgText(args.vg, mm2px(22.f), y0, cnt, NULL);
        } else if (lp && lp->arm != EchoLooper::A_NONE && module->blink < 0.125f) {
            nvgFillColor(args.vg, oled::litFill());
            nvgText(args.vg, mm2px(22.f), y0, kArmName[clamp(lp->arm, 0, 7)], NULL);
        }
        nvgFillColor(args.vg, oled::amber());
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, W - 4.f, y0, info, NULL);

        // waveform: one bar per peak bucket, mirrored round the centre line
        float x0 = 4.f, x1 = W - 4.f, yc = (yW + yWB) * 0.5f, hh = (yWB - yW) * 0.5f - 2.f;
        if (lp && lp->st == EchoLooper::REC) {
            float f = clamp((float)lp->pos / std::max(1, lp->cap), 0.f, 1.f);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, x0, yc - 1.f, (x1 - x0) * f, 2.f);
            nvgFillColor(args.vg, oled::litFill());
            nvgFill(args.vg);
        } else if (lp && lp->len > 0) {
            // the view: [v0, v0 + span) samples; zoomed in, the bars are read from the recording itself
            if (zoom < 1.f) zoom = 1.f;
            // zoomed in while it plays: the view follows the playhead, keeping it in the middle
            if (zoom > 1.f && lp->playing()) view0 = (float)lp->pos / lp->len - 0.5f / zoom;
            view0 = clamp(view0, 0.f, 1.f - 1.f / zoom);
            const double len = lp->len, v0 = view0 * len, span = len / zoom;
            auto X = [&](double s) { return x0 + (x1 - x0) * (float)((s - v0) / span); };
            const int N = zoom <= 1.f ? EchoLooper::kPeaks : 160;
            float pk[160];
            for (int c = 0; c < N; c++) {
                if (zoom <= 1.f) { pk[c] = lp->peak[c]; continue; }
                int a = (int)(v0 + c * span / N), b = std::min(lp->len, std::max(a + 1, (int)(v0 + (c + 1) * span / N)));
                int step = std::max(1, (b - a) / 128);   // at most 128 reads a bar
                float m = 0.f;
                for (int i = a; i < b; i += step) m = std::max(m, std::max(std::fabs(lp->l[i]), std::fabs(lp->r[i])));
                pk[c] = m;
            }
            float bw = (x1 - x0) / N;
            int L = lp->playLen > 0 ? std::min(lp->playLen, lp->len) : lp->len;
            for (int pass = 0; pass < 2; pass++) {   // the playing part bright, the rest dim
                nvgBeginPath(args.vg);
                for (int c = 0; c < N; c++) {
                    bool in = v0 + (c + 0.5) * span / N <= L;
                    if (in != (pass == 0)) continue;
                    float h = std::max(0.5f, std::min(1.f, pk[c]) * hh);
                    nvgRect(args.vg, x0 + c * bw, yc - h, std::max(0.6f, bw - 0.4f), 2.f * h);
                }
                nvgFillColor(args.vg, oled::amber(pass == 0 ? 170 : 45));
                nvgFill(args.vg);
            }
            if (lp->beats > 1 && lp->beatLen > 0.0 && span / lp->beatLen <= 64.0) {   // beat ticks (bars longer)
                nvgBeginPath(args.vg);
                for (int k = 1; k < lp->beats; k++) {
                    double s = k * lp->beatLen;
                    if (s < v0 || s > v0 + span) continue;
                    float x = X(s), t = k % 4 == 0 ? 2.5f : 1.3f;
                    nvgMoveTo(args.vg, x, yW + 0.5f); nvgLineTo(args.vg, x, yW + 0.5f + t);
                    nvgMoveTo(args.vg, x, yWB - 0.5f); nvgLineTo(args.vg, x, yWB - 0.5f - t);
                }
                nvgStrokeColor(args.vg, oled::outline(150));
                nvgStrokeWidth(args.vg, 0.6f);
                nvgStroke(args.vg);
            }
            if (lp->pos >= v0 && lp->pos <= v0 + span) {   // the playhead, when in view
                float px = X(lp->pos);
                nvgBeginPath(args.vg);
                nvgMoveTo(args.vg, px, yW + 1.5f);
                nvgLineTo(args.vg, px, yWB - 1.5f);
                nvgStrokeColor(args.vg, lp->playing() ? oled::green() : oled::dim());
                nvgStrokeWidth(args.vg, 1.f);
                nvgStroke(args.vg);
            }
            if (zoom > 1.f) {   // how far in, and where: a bar along the bottom edge
                char zt[16];
                snprintf(zt, sizeof zt, "x%.0f", zoom);
                nvgFontSize(args.vg, 6.f);
                nvgFillColor(args.vg, oled::dim());
                nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
                nvgText(args.vg, x1, yW + 1.f, zt, NULL);
                nvgBeginPath(args.vg);
                nvgRect(args.vg, x0 + (x1 - x0) * view0, yWB - 1.2f, std::max(1.5f, (x1 - x0) / zoom), 1.f);
                nvgFillColor(args.vg, oled::amber(120));
                nvgFill(args.vg);
            }
        } else {
            zoom = 1.f; view0 = 0.f;
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, x0, yc);
            nvgLineTo(args.vg, x1, yc);
            nvgStrokeColor(args.vg, oled::outline(60));
            nvgStrokeWidth(args.vg, 0.6f);
            nvgStroke(args.vg);
        }
    }

    // clickable value: SUBDIV; the waveform zooms with the scroll wheel (right-click: all of it)
    int zone(Vec p) {
        float W = box.size.x;
        if (p.y >= mm2px(es::yInfo) && p.y < mm2px(es::yInfo + oled::kRow) && p.x >= W / 3.f && p.x < 2.f * W / 3.f) return 1;
        if (p.y >= mm2px(es::yWave) && p.y < mm2px(es::yWave + es::kWaveH)) return 3;
        return 0;
    }
    // zoom by one step, keeping the sample under the pointer where it is
    void zoomAt(float px, int d) {
        float fx = clamp((px - 4.f) / std::max(1.f, box.size.x - 8.f), 0.f, 1.f);
        float at = view0 + fx / zoom;
        zoom = clamp(zoom * (d > 0 ? 1.5f : 1.f / 1.5f), 1.f, 512.f);
        if (zoom < 1.05f) zoom = 1.f;
        view0 = clamp(at - fx / zoom, 0.f, 1.f - 1.f / zoom);
    }
    void stepValue(int z, int d) {
        if (!module) return;
        if (z == 1) {
            int s = (int)module->params[EC::SUBDIV_PARAM].getValue();
            module->params[EC::SUBDIV_PARAM].setValue((float)((s + d + 11) % 11));
        }
    }
    void onButton(const ButtonEvent& e) override {
        int z = zone(e.pos);
        if (e.action != GLFW_PRESS || !z) return;
        if (e.button != GLFW_MOUSE_BUTTON_LEFT && e.button != GLFW_MOUSE_BUTTON_RIGHT) return;
        if (z == 3) {   // the waveform: right-click shows all of it again
            if (e.button != GLFW_MOUSE_BUTTON_RIGHT) return;
            zoom = 1.f; view0 = 0.f;
        } else stepValue(z, e.button == GLFW_MOUSE_BUTTON_LEFT ? 1 : -1);
        e.consume(this);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        int z = zone(e.pos);
        if (e.scrollDelta.y == 0.f || !z) return;
        if (z == 3) zoomAt(e.pos.x, e.scrollDelta.y > 0.f ? 1 : -1);
        else stepValue(z, e.scrollDelta.y > 0.f ? 1 : -1);
        e.consume(this);
    }
};

// REC / PLAY: a cell in its own colour, dim (outline and label) until engaged, filled while it is
struct EchoTintCell : oled::Cell {
    NVGcolor tint = nvgRGB(0xff, 0x32, 0x28);
    void drawLayer(const DrawArgs& args, int layer) override {
        ParamWidget::drawLayer(args, layer);
        if (layer != 1) return;
        bool on = lit();
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.2f);
        nvgFillColor(args.vg, on ? tint : oled::offFill());
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgTransRGBA(tint, on ? 255 : 120));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        if (module && jack >= 0 && module->inputs[jack].isConnected()) {
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, W - 2.4f, 2.4f, 1.1f);
            nvgFillColor(args.vg, on ? oled::ink() : tint);
            nvgFill(args.vg);
        }
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? oled::ink() : nvgTransRGBA(tint, 190));
        nvgText(args.vg, W * 0.5f, H * 0.5f, label, NULL);
    }
};

// A preset slot: the amber cell; once you have stored a preset into it (USER), filled a darker orange
// while not loaded (loaded, it lights as any cell)
struct EchoSlotCell : oled::Cell {
    const bool* stored = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (!(stored && *stored) || lit() || layer != 1) { oled::Cell::drawLayer(args, layer); return; }
        ParamWidget::drawLayer(args, layer);
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.2f);
        nvgFillColor(args.vg, nvgRGB(0x8a, 0x3c, 0x06));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(0xc8, 0x5a, 0x0a));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, oled::amber());
        nvgText(args.vg, W * 0.5f, H * 0.5f, label, NULL);
    }
};

// MODE: steps the page CLASSICS > USER > DRUMS > SYNTH > CINEMATIC > DUB > LO-FI > RHYTHM (click; scroll both
// ways; right-click lists them). Lit
// while pressed; the page shows in the line above the cells.
struct EchoModeCell : oled::Cell {
    bool held = false;
    int flashFrames = 0;   // lit a moment even for a quick click
    EchoModeCell() { latch = true; }
    void step() override {
        if (flashFrames > 0) flashFrames--;
        oled::Cell::step();
    }
    void onDragEnd(const DragEndEvent& e) override { held = false; }
    void step_(int d, bool wrap) {
        ParamQuantity* pq = getParamQuantity();
        if (!pq) return;
        int v = (int)std::round(pq->getValue()) + d;
        const int n = kBanks + 1;   // CLASSICS and the banks
        pq->setValue((float)(wrap ? (v % n + n) % n : clamp(v, 0, n - 1)));
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        ParamWidget::drawLayer(args, layer);
        if (layer != 1) return;
        // lit while pressed (the page it is on shows in the line above the cells)
        bool on = held || flashFrames > 0;
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.2f);
        nvgFillColor(args.vg, on ? oled::litFill() : oled::offFill());
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, oled::outline(on ? 255 : 110));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, fontSize);
        nvgFillColor(args.vg, on ? oled::ink() : oled::amber());
        nvgText(args.vg, W * 0.5f, H * 0.5f, label, NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            step_(1, true);
            held = true; flashFrames = 8;
            e.consume(this);
            return;
        }
        oled::Cell::onButton(e);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (e.scrollDelta.y == 0.f) return;
        step_(e.scrollDelta.y > 0.f ? 1 : -1, false);
        e.consume(this);
    }
};

// COUNT-IN: one cell that steps OFF > 1 > 2 > 3 > 4 beats (click; scroll both ways; right-click lists them)
struct EchoCountCell : oled::Cell {
    char text[12] = "COUNT OFF";
    EchoCountCell() { label = text; latch = true; }
    void set(int v) { if (ParamQuantity* pq = getParamQuantity()) pq->setValue((float)((v % 5 + 5) % 5)); }
    int get() { ParamQuantity* pq = getParamQuantity(); return pq ? (int)std::round(pq->getValue()) : 0; }
    void step() override {
        int v = get();
        if (v > 0) snprintf(text, sizeof text, "COUNT %d", v);
        else snprintf(text, sizeof text, "COUNT OFF");
        oled::Cell::step();
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) { set(get() + 1); e.consume(this); return; }
        oled::Cell::onButton(e);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (e.scrollDelta.y == 0.f) return;
        set(clamp(get() + (e.scrollDelta.y > 0.f ? 1 : -1), 0, 4));
        e.consume(this);
    }
};

} // namespace

struct SynTHBloKEchoWidget : ModuleWidget {
    oled::Cell* undoCell = NULL;
    EchoSlotCell* slotCell[EC::kSlots] = {};
    oled::Cell* typeCell[ET_NUM] = {};
    oled::Cell* storeCell = NULL;

    SynTHBloKEchoWidget(EC* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKEcho.svg")));
        using namespace rack::componentlibrary;
        const float PW = 101.6f;   // 20 HP
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PW - 3.5f, 3.5f))));
        EchoTitle* title = createWidget<EchoTitle>(mm2px(Vec(8.5f, 0.f)));
        title->box.size = mm2px(Vec(60.f, 7.f));
        addChild(title);

        auto label = [&](float cx, float cy, const char* text) {
            EchoLabel* l = createWidget<EchoLabel>(mm2px(Vec(cx - 12.f, cy - 2.f)));
            l->box.size = mm2px(Vec(24.f, 4.f)); l->text = text;
            addChild(l);
        };
        // a trimpot with the knob dress scaled down to it: ticks and ring, the name under it
        auto smallKnob = [&](float x, float y, int param, const char* name) {
            const float k = 0.78f, rs = 11.f * k;
            addChild(KnobTicks::create(Vec(x, y), k));
            EchoRing* r = createWidget<EchoRing>(mm2px(Vec(x - 0.5f * rs, y - 0.5f * rs)));
            r->box.size = mm2px(Vec(rs, rs)); r->module = module; r->param = param; r->width = 1.5f;
            addChild(r);
            addParam(createParamCentered<Trimpot>(mm2px(Vec(x, y)), module, param));
            label(x, y + 6.2f, name);
        };

        // ---------------- the screen ----------------
        const float OX = 3.5f, OY = 9.5f, OW = PW - 7.f;
        const float CG = es::kCG, CH = oled::kCellH;
        EchoScreen* scr = createWidget<EchoScreen>(mm2px(Vec(OX, OY)));
        scr->box.size = mm2px(Vec(OW, es::kH)); scr->module = module;
        addChild(scr);
        auto cell = [&](float x, float y, float w, int param, const char* lab) -> oled::Cell* {
            oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(OX + x, OY + y)), module, param);
            c->box.size = mm2px(Vec(w, CH)); c->module = module; c->label = lab;
            addParam(c);
            return c;
        };
        const float inner = OW - 2.f * es::kPad;   // the cells' width across the screen

        // the looper's two left rows and the delay's left column share one width: REC / PLAY and ON / MODE match
        const float leftW = inner - es::kLenW - 1.5f;   // the two rows' width
        const float lenX = es::kPad + leftW + 1.5f;
        const float tw = (leftW - 4.f * CG) / 5.f;      // a transport cell's width

        // ---- delay: left, ON | MODE over FREEZE | KILL DRY; right, the page MODE shows ----
        // (no TAP cell or jack: the tempo comes from CLOCK, else the TIME knob)
        // FREEZE and KILL DRY in ice blue (dim until on), apart from the amber cells
        const float yOM = es::yBlk, yFK = es::yBlk + es::kTransH + es::kGap;
        static const int fkParam[2] = {EC::FREEZE_PARAM, EC::KILLDRY_PARAM};
        static const char* const fkName[2] = {"FREEZE", "KILL DRY"};
        for (int i = 0; i < 2; i++) {
            EchoTintCell* t = createParam<EchoTintCell>(mm2px(Vec(OX + es::kPad + i * (tw + CG), OY + yFK)), module, fkParam[i]);
            t->box.size = mm2px(Vec(tw, CH)); t->module = module; t->label = fkName[i];
            t->latch = true; t->fontSize = 5.5f; t->tint = nvgRGB(0x5c, 0xc8, 0xf2);
            if (i == 0) t->jack = EC::FREEZE_INPUT;
            addParam(t);
        }
        oled::Cell* on = cell(es::kPad, yOM, tw, EC::ON_PARAM, "ON");
        on->box.size = mm2px(Vec(tw, es::kTransH));
        on->latch = true; on->green = true; on->jack = EC::BYPASS_INPUT; on->fontSize = 7.f;
        EchoModeCell* mode = createParam<EchoModeCell>(mm2px(Vec(OX + es::kPad + tw + CG, OY + yOM)), module, EC::PAGE_PARAM);
        mode->box.size = mm2px(Vec(tw, es::kTransH)); mode->module = module; mode->label = "MODE"; mode->fontSize = 7.f;
        addParam(mode);

        const float px0 = es::kPad + 2.f * tw + CG + 1.5f, pW = es::kPad + inner - px0;   // the page's area
        // CLASSICS: the 12 delay types, 4 x 3
        const float tcw = (pW - 3.f * CG) / 4.f;
        for (int k = 0; k < ET_NUM; k++) {
            oled::Cell* tc = cell(px0 + (k % 4) * (tcw + CG), es::yBlk + (k / 4) * (CH + es::kGap), tcw, EC::TYPE_PARAM, kEchoTypeName[k]);
            tc->setTo = k; tc->fontSize = 6.f;
            typeCell[k] = tc;
        }
        // a bank: its presets on the CLASSICS grid (4 x 3: USER 11, a factory bank 8), STORE in the last cell
        static const char* const num[EC::kSlots] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"};
        for (int s = 0; s < EC::kSlots; s++) {
            EchoSlotCell* c = createParam<EchoSlotCell>(mm2px(Vec(OX + px0 + (s % 4) * (tcw + CG), OY + es::yBlk + (s / 4) * (CH + es::kGap))),
                                                        module, EC::slotParam(s));
            c->box.size = mm2px(Vec(tcw, CH)); c->module = module; c->label = num[s];
            addParam(c);
            if (module) c->flash = &module->slotLit[s];
            slotCell[s] = c;
        }
        // STORE in ice blue as FREEZE / KILL DRY (dim until armed), apart from the amber presets
        EchoTintCell* st = createParam<EchoTintCell>(mm2px(Vec(OX + px0 + 3.f * (tcw + CG), OY + es::yBlk + 2.f * (CH + es::kGap))),
                                                     module, EC::STORE_PARAM);
        st->box.size = mm2px(Vec(tcw, CH)); st->module = module; st->label = "STORE";
        st->latch = true; st->fontSize = 6.f; st->tint = nvgRGB(0x5c, 0xc8, 0xf2);
        addParam(st);
        storeCell = st;

        // record length, 3 x 3: FREE (until REC again) | 1-4 beats | 1-4 bars. 4 BT and 1 BAR are the same
        // length, so both light.
        static const char* const lenName[9] = {"FREE", "1 BT", "2 BT", "3 BT", "4 BT", "1 BAR", "2 BAR", "3 BAR", "4 BAR"};
        static const int lenBeats[9] = {0, 1, 2, 3, 4, 4, 8, 12, 16};
        const float lw = (es::kLenW - 2.f * CG) / 3.f;
        for (int i = 0; i < 9; i++) {
            oled::Cell* c = cell(lenX + (i % 3) * (lw + CG), es::yGrid + (i / 3) * (CH + es::kGap), lw, EC::LEN_PARAM, lenName[i]);
            c->setTo = lenBeats[i]; c->fontSize = 5.5f;
        }

        // REC | PLAY | ONCE | UNDO | CLEAR: tall, near-square cells along the bottom. REC red and PLAY green
        // (dim until engaged, bright while they are)
        struct LC { const char* label; int param, jack; };
        const LC lc[5] = {{"REC", EC::REC_PARAM, EC::REC_INPUT}, {"PLAY", EC::PLAY_PARAM, EC::PLAY_INPUT},
                          {"ONCE", EC::ONCE_PARAM, EC::ONCE_INPUT}, {"UNDO", EC::UNDO_PARAM, EC::UNDO_INPUT},
                          {"CLEAR", EC::CLEAR_PARAM, EC::CLEAR_INPUT}};
        for (int i = 0; i < 5; i++) {
            float x = es::kPad + i * (tw + CG);
            oled::Cell* c;
            if (i < 2) {
                EchoTintCell* t = createParam<EchoTintCell>(mm2px(Vec(OX + x, OY + es::yTrans)), module, lc[i].param);
                t->module = module; t->label = lc[i].label;
                t->tint = i == 0 ? nvgRGB(0xff, 0x32, 0x28) : oled::green();
                addParam(t);
                c = t;
            } else c = cell(x, es::yTrans, tw, lc[i].param, lc[i].label);
            c->box.size = mm2px(Vec(tw, es::kTransH));
            c->jack = lc[i].jack; c->fontSize = 7.f;
            if (module) {
                if (i == 0) c->flash = &module->recFlash;
                if (i == 1) c->flash = &module->playFlash;
                if (i == 2) { c->flash = &module->onceFlash; c->green = true; }
            }
            if (i == 3) undoCell = c;
        }

        // COUNT-IN | LOOP BEFORE DELAY (the looper works to the beat whenever there is a clock: no grid cells)
        const float halfW = (leftW - CG) / 2.f;
        EchoCountCell* cc = createParam<EchoCountCell>(mm2px(Vec(OX + es::kPad, OY + es::yGrid)), module, EC::COUNTIN_PARAM);
        cc->box.size = mm2px(Vec(halfW, CH)); cc->module = module;
        addParam(cc);
        cell(es::kPad + halfW + CG, es::yGrid, halfW, EC::PREFX_PARAM, "LOOP BEFORE DELAY")->latch = true;

        // ---------------- knobs ----------------
        auto col = [&](int n, int i) { return 2.f + (PW - 4.f) * (i + 0.5f) / n; };
        // one row of knobs: TEMPO (used with no clock) and the six delay knobs
        const float KA = 77.f;
        static const int knobParam[7] = {EC::TEMPO_PARAM, EC::TIME_PARAM, EC::SUBDIV_PARAM, EC::FEEDBACK_PARAM, EC::TONE_PARAM, EC::MOD_PARAM, EC::SPACE_PARAM};
        static const char* const knobName[7] = {"TEMPO", "TIME", "SUBDIV", "FEEDBACK", "TONE", "MOD", "SPACE"};
        for (int i = 0; i < 7; i++) smallKnob(col(7, i), KA, knobParam[i], knobName[i]);
        // ---------------- jacks: the input plate | the looper plate | the output plate (plates drawn in the SVG) ----------------
        // (TAP, FREEZE and BYPASS have no jacks: their input ids stay so old patches keep their cables in place)
        const float P = 10.f, Y1 = 108.5f, Y2 = 120.5f;   // P: the jack pitch on the input and output plates
        const float xI = 8.25f;              // IN L (plate x 2.4 .. 24.1, as wide as the output plate): MIX over IN L / R
        const float xL = 31.75f, PL = 9.5f;  // the 5 looper jacks in one row along the bottom of the plate (x 26.1 .. 75.4)
        const float xC = xL + 4.f * PL;      // CLOCK, top right of the looper plate (over CLEAR), mirroring the LOOP knob
        const float xK = 34.f;               // the LOOP knob, at the left of the plate, in the MASTER row
        const float xO = 83.25f;             // OUT L (plate x 77.4 .. 99.1)
        auto jack = [&](float x, float y, int id, bool out, const char* name, bool labelBelow = false) {
            label(x, labelBelow ? y + 6.2f : y - 4.6f, name);
            if (out) {
                addOutput(createOutputCentered<SmallJack>(mm2px(Vec(x, y)), module, id));
                addChild(JackLed::create(Vec(x + 3.9f, y), module, id, true));
            } else addInput(createInputCentered<SmallJack>(mm2px(Vec(x, y)), module, id));
        };
        auto plateKnob = [&](float x, float y, int param) {   // dressed as the knobs above: ticks and ring
            addChild(KnobTicks::create(Vec(x, y)));
            EchoRing* r = createWidget<EchoRing>(mm2px(Vec(x - 5.5f, y - 5.5f)));
            r->box.size = mm2px(Vec(11.f, 11.f)); r->module = module; r->param = param;
            addChild(r);
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y)), module, param));
        };
        // the input plate: MIX (no label, as MASTER) over IN L / R; the signal runs IN -> looper -> OUT along the bottom
        plateKnob(xI + 0.5f * P, Y1, EC::LEVEL_PARAM);
        jack(xI, Y2, EC::IN_L_INPUT, false, "IN L");
        jack(xI + P, Y2, EC::IN_R_INPUT, false, "IN R");
        jack(xC, Y1 + 1.f, EC::CLOCK_INPUT, false, "CLOCK");   // the label above it, as on the jacks below
        jack(xL, Y2, EC::REC_INPUT, false, "REC");
        jack(xL + PL, Y2, EC::PLAY_INPUT, false, "PLAY");
        jack(xL + 2.f * PL, Y2, EC::ONCE_INPUT, false, "ONCE");
        jack(xL + 3.f * PL, Y2, EC::UNDO_INPUT, false, "UNDO");
        jack(xL + 4.f * PL, Y2, EC::CLEAR_INPUT, false, "CLEAR");
        plateKnob(xK, Y1, EC::LOOP_LEVEL_PARAM);
        label(xK + 11.f, Y1, "LOOP");   // beside the knob: the jack labels take the line below it
        plateKnob(xO + 0.5f * P, Y1, EC::MASTER_PARAM);   // MASTER: no label, it heads the outputs
        jack(xO, Y2, EC::OUT_L_OUTPUT, true, "OUT L");
        jack(xO + P, Y2, EC::OUT_R_OUTPUT, true, "OUT R");
    }

    void step() override {
        EC* m = getModule<EC>();
        if (undoCell) undoCell->label = m && m->looper.undone ? "REDO" : "UNDO";
        static const char* const num[EC::kSlots] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"};
        int b = m ? m->bank() : 0;
        for (int s = 0; s < EC::kSlots; s++) {   // the slots show the genres in the factory banks
            if (!slotCell[s]) continue;
            slotCell[s]->label = b > 0 && s < kGenreN ? kGenre[b - 1][s].cell : num[s];
            slotCell[s]->fontSize = b > 0 ? 5.5f : 6.5f;
            slotCell[s]->stored = m && b == 0 ? &m->slotMine[s] : NULL;   // USER: darker orange once stored into
        }
        // MODE's page: the delay types, or a bank's presets + STORE (the browser shows the types)
        int page = m ? clamp((int)std::round(m->params[EC::PAGE_PARAM].getValue()), 0, kBanks) : 0;
        for (int k = 0; k < ET_NUM; k++) if (typeCell[k]) typeCell[k]->visible = page == 0;
        for (int s = 0; s < EC::kSlots; s++) if (slotCell[s]) slotCell[s]->visible = page > 0 && s < EC::bankSlots(page - 1);
        if (storeCell) storeCell->visible = page > 0;
        ModuleWidget::step();
    }

    void appendContextMenu(Menu* menu) override {
        EC* m = getModule<EC>();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createIndexSubmenuItem("Clock pulse length", {kClockDivName[0], kClockDivName[1], kClockDivName[2]},
                                              [=]() { return m->clockDiv; }, [=](int i) { m->clockDiv = i; }));
        menu->addChild(createBoolPtrMenuItem("Trails on bypass", "", &m->trails));
        menu->addChild(createMenuLabel("The loop audio is not saved with the patch"));
    }
};

Model* modelSynTHBloKEcho = createModel<SynTHBloKEcho, SynTHBloKEchoWidget>("SynTHBloKEcho");
