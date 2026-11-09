// ============================================================================
// SynTHBloK MATRIX-DRUM  (drum-synth groovebox on the MATRIX sequencer)
//  The MATRIX 8x8 grid plays 8 drum voices, one per row. Every row is the same drum-synth voice with a
//  MODEL (KICK, SNARE, CLAP, TOM, HAT, METAL, FM, NOISE) and 8 macros, laid out across the row and played
//  by the strip banks A..H: TUNE, DECAY, TONE, SNAP, MOD, DRIVE, LEVEL, PAN.
//  A step is off, a hit or an accent (right-click a pad, or ACC on; on the JAM hold GRID and press the pad).
//  Rows can share a choke group (row menu): a hit cuts the others in its group off.
//  8 patterns, 1..8 steps. CLOCK = 16th notes; unpatched, the internal clock runs at the BPM.
//  OUT L / R = the voices (panned) through the FINAL stage (filter + ADSR, DELAY, SPACE) and FORGE FX, then MASTER.
//  JAM ("JAM Template 4 Rack", MIDI channel 1), as on MATRIX FORGE: pads = the grid, scenes = patterns,
//  strips = the bank's macro for each row, GRID held + pad = accent (a GRID tap = +-5V), SONG = FX,
//  knob MST / CUE = MASTER, GRP = DELAY, IN1 = SPACE. CLEAR held 1 s clears every pattern.
//  The voice OLED (laid out like the player's info screen): the selected voice, its last hit as WAVE, LINE,
//  ENV, SPEC or SGRAM with a zoom, the KIT rows (9 factory kits; 8 user kits + STORE), and 8 pads that play a row
//  and flash on its hits.
// ============================================================================
#include "Drum_dsp.hpp"
#include "FinalStage_dsp.hpp"
#include "Matrix8_dsp.hpp"   // M8Delay / M8Reverb for the FINAL stage
#include "Matrix8_ui.hpp"
#include "KnobTicks.hpp"
#include "OledUi.hpp"
#include "JamOwner.hpp"
#include "SongLink.hpp"
#include "ForgeFxUi.hpp"
#include "JackLed.hpp"
#include "SynTHBloKGrainForge.hpp"
#include <atomic>
#include <cstdio>
#include <complex>
#include <string>
#include <thread>
#include <chrono>

namespace {

static const char* kEnvTrigName[6] = {"ANY", "ROW 1", "ACCENT", "BEAT", "STEP", "GATE"};
// The default kit, rows 1..8: model and TUNE, DECAY, TONE, SNAP, MOD, DRIVE, LEVEL, PAN.
static const int kKitModel[8] = {DM_KICK, DM_SNARE, DM_CLAP, DM_HAT, DM_HAT, DM_TOM, DM_METAL, DM_FM};
static const float kKitVal[8][DK_NUM] = {
    {0.25f, 0.45f, 0.6f, 0.3f, 0.5f, 0.25f, 0.85f, 0.5f},
    {0.3f, 0.4f, 0.5f, 0.6f, 0.4f, 0.2f, 0.7f, 0.5f},
    {0.5f, 0.4f, 0.5f, 0.6f, 0.4f, 0.1f, 0.65f, 0.55f},
    {0.5f, 0.15f, 0.5f, 0.3f, 0.5f, 0.f, 0.5f, 0.6f},
    {0.5f, 0.6f, 0.5f, 0.3f, 0.5f, 0.f, 0.45f, 0.4f},
    {0.35f, 0.4f, 0.6f, 0.3f, 0.3f, 0.15f, 0.6f, 0.35f},
    {0.5f, 0.35f, 0.4f, 0.2f, 0.f, 0.1f, 0.45f, 0.65f},
    {0.5f, 0.25f, 0.4f, 0.4f, 0.4f, 0.1f, 0.5f, 0.5f}};

// The KIT row's factory kits: a model, the 8 macros and a choke group for each of the 8 rows / pads.
struct DrumKitDef { const char* name; int model[8]; float val[8][DK_NUM]; int choke[8]; };
static const int kNumFactoryKit = 9, kNumUserKit = 8;
static const DrumKitDef kFactoryKit[kNumFactoryKit] = {
    {"808", {DM_KICK, DM_SNARE, DM_CLAP, DM_HAT, DM_HAT, DM_TOM, DM_TOM, DM_METAL},
     {{0.2f, 0.7f, 0.4f, 0.2f, 0.5f, 0.1f, 0.9f, 0.5f}, {0.35f, 0.35f, 0.45f, 0.5f, 0.4f, 0.05f, 0.7f, 0.5f},
      {0.5f, 0.45f, 0.5f, 0.6f, 0.4f, 0.f, 0.65f, 0.55f}, {0.55f, 0.12f, 0.6f, 0.3f, 0.5f, 0.f, 0.5f, 0.6f},
      {0.55f, 0.55f, 0.6f, 0.3f, 0.5f, 0.f, 0.45f, 0.4f}, {0.3f, 0.5f, 0.5f, 0.3f, 0.4f, 0.05f, 0.6f, 0.35f},
      {0.5f, 0.45f, 0.5f, 0.3f, 0.4f, 0.05f, 0.55f, 0.65f}, {0.6f, 0.3f, 0.5f, 0.2f, 0.2f, 0.05f, 0.45f, 0.6f}},
     {0, 0, 0, 1, 1, 0, 0, 0}},
    {"909", {DM_KICK, DM_SNARE, DM_CLAP, DM_HAT, DM_HAT, DM_TOM, DM_METAL, DM_NOISE},
     {{0.3f, 0.4f, 0.6f, 0.55f, 0.6f, 0.25f, 0.85f, 0.5f}, {0.4f, 0.4f, 0.6f, 0.7f, 0.5f, 0.15f, 0.7f, 0.5f},
      {0.5f, 0.4f, 0.55f, 0.65f, 0.4f, 0.1f, 0.65f, 0.5f}, {0.6f, 0.15f, 0.7f, 0.4f, 0.5f, 0.f, 0.5f, 0.6f},
      {0.6f, 0.6f, 0.7f, 0.4f, 0.5f, 0.f, 0.45f, 0.4f}, {0.4f, 0.4f, 0.6f, 0.4f, 0.3f, 0.15f, 0.6f, 0.35f},
      {0.65f, 0.6f, 0.6f, 0.3f, 0.3f, 0.05f, 0.4f, 0.65f}, {0.5f, 0.35f, 0.5f, 0.4f, 0.5f, 0.f, 0.4f, 0.5f}},
     {0, 0, 0, 1, 1, 0, 0, 0}},
    {"TECH", {DM_KICK, DM_KICK, DM_CLAP, DM_HAT, DM_HAT, DM_FM, DM_METAL, DM_NOISE},
     {{0.25f, 0.5f, 0.5f, 0.45f, 0.5f, 0.55f, 0.9f, 0.5f}, {0.2f, 0.75f, 0.3f, 0.1f, 0.6f, 0.4f, 0.5f, 0.5f},
      {0.45f, 0.5f, 0.45f, 0.6f, 0.5f, 0.3f, 0.6f, 0.5f}, {0.6f, 0.1f, 0.65f, 0.4f, 0.5f, 0.1f, 0.5f, 0.65f},
      {0.55f, 0.5f, 0.65f, 0.4f, 0.5f, 0.1f, 0.4f, 0.35f}, {0.4f, 0.25f, 0.5f, 0.5f, 0.6f, 0.3f, 0.5f, 0.4f},
      {0.55f, 0.35f, 0.45f, 0.2f, 0.3f, 0.2f, 0.45f, 0.6f}, {0.5f, 0.55f, 0.6f, 0.3f, 0.6f, 0.2f, 0.4f, 0.5f}},
     {0, 0, 0, 1, 1, 0, 0, 0}},
    {"ELEC", {DM_KICK, DM_SNARE, DM_CLAP, DM_HAT, DM_FM, DM_FM, DM_TOM, DM_METAL},
     {{0.18f, 0.8f, 0.35f, 0.35f, 0.6f, 0.2f, 0.9f, 0.5f}, {0.45f, 0.3f, 0.6f, 0.7f, 0.5f, 0.1f, 0.7f, 0.5f},
      {0.55f, 0.35f, 0.6f, 0.7f, 0.3f, 0.05f, 0.65f, 0.5f}, {0.65f, 0.1f, 0.7f, 0.5f, 0.5f, 0.f, 0.5f, 0.6f},
      {0.45f, 0.3f, 0.5f, 0.4f, 0.7f, 0.1f, 0.5f, 0.35f}, {0.6f, 0.2f, 0.5f, 0.5f, 0.8f, 0.1f, 0.45f, 0.65f},
      {0.35f, 0.45f, 0.55f, 0.4f, 0.5f, 0.1f, 0.6f, 0.4f}, {0.7f, 0.25f, 0.55f, 0.2f, 0.3f, 0.05f, 0.4f, 0.6f}},
     {0, 0, 0, 0, 0, 0, 0, 0}},
    {"LOFI", {DM_KICK, DM_SNARE, DM_CLAP, DM_HAT, DM_HAT, DM_TOM, DM_NOISE, DM_FM},
     {{0.28f, 0.35f, 0.3f, 0.25f, 0.4f, 0.6f, 0.85f, 0.5f}, {0.3f, 0.3f, 0.3f, 0.4f, 0.4f, 0.5f, 0.7f, 0.5f},
      {0.45f, 0.35f, 0.35f, 0.4f, 0.4f, 0.4f, 0.6f, 0.55f}, {0.45f, 0.12f, 0.35f, 0.25f, 0.5f, 0.3f, 0.45f, 0.6f},
      {0.45f, 0.45f, 0.35f, 0.25f, 0.5f, 0.3f, 0.4f, 0.4f}, {0.3f, 0.35f, 0.35f, 0.3f, 0.3f, 0.4f, 0.6f, 0.35f},
      {0.4f, 0.3f, 0.3f, 0.3f, 0.4f, 0.3f, 0.35f, 0.5f}, {0.45f, 0.25f, 0.3f, 0.3f, 0.4f, 0.4f, 0.45f, 0.65f}},
     {0, 0, 0, 1, 1, 0, 0, 0}},
    {"606", {DM_KICK, DM_SNARE, DM_CLAP, DM_HAT, DM_HAT, DM_TOM, DM_TOM, DM_METAL},
     {{0.35f, 0.3f, 0.55f, 0.4f, 0.4f, 0.05f, 0.85f, 0.5f}, {0.45f, 0.25f, 0.65f, 0.6f, 0.4f, 0.f, 0.7f, 0.5f},
      {0.5f, 0.35f, 0.6f, 0.6f, 0.4f, 0.f, 0.6f, 0.5f}, {0.65f, 0.08f, 0.7f, 0.3f, 0.5f, 0.f, 0.5f, 0.6f},
      {0.65f, 0.4f, 0.7f, 0.3f, 0.5f, 0.f, 0.45f, 0.4f}, {0.3f, 0.35f, 0.5f, 0.3f, 0.3f, 0.f, 0.6f, 0.35f},
      {0.55f, 0.3f, 0.5f, 0.3f, 0.3f, 0.f, 0.55f, 0.65f}, {0.7f, 0.7f, 0.7f, 0.2f, 0.4f, 0.f, 0.4f, 0.55f}},
     {0, 0, 0, 1, 1, 0, 0, 0}},
    {"HOUSE", {DM_KICK, DM_CLAP, DM_SNARE, DM_HAT, DM_HAT, DM_METAL, DM_TOM, DM_NOISE},
     {{0.27f, 0.5f, 0.55f, 0.4f, 0.5f, 0.2f, 0.9f, 0.5f}, {0.5f, 0.5f, 0.5f, 0.6f, 0.5f, 0.1f, 0.7f, 0.5f},
      {0.4f, 0.3f, 0.6f, 0.6f, 0.4f, 0.1f, 0.55f, 0.55f}, {0.6f, 0.12f, 0.65f, 0.35f, 0.5f, 0.f, 0.5f, 0.6f},
      {0.6f, 0.5f, 0.65f, 0.35f, 0.5f, 0.f, 0.45f, 0.4f}, {0.6f, 0.65f, 0.6f, 0.2f, 0.3f, 0.f, 0.4f, 0.6f},
      {0.6f, 0.25f, 0.5f, 0.4f, 0.3f, 0.f, 0.5f, 0.35f}, {0.7f, 0.1f, 0.7f, 0.5f, 0.5f, 0.f, 0.4f, 0.65f}},
     {0, 0, 0, 1, 1, 0, 0, 0}},
    {"DUB", {DM_KICK, DM_SNARE, DM_HAT, DM_HAT, DM_TOM, DM_FM, DM_NOISE, DM_METAL},
     {{0.15f, 0.75f, 0.3f, 0.15f, 0.4f, 0.15f, 0.9f, 0.5f}, {0.35f, 0.5f, 0.35f, 0.4f, 0.5f, 0.1f, 0.65f, 0.5f},
      {0.5f, 0.2f, 0.45f, 0.3f, 0.5f, 0.f, 0.45f, 0.6f}, {0.5f, 0.6f, 0.45f, 0.3f, 0.5f, 0.f, 0.4f, 0.4f},
      {0.25f, 0.6f, 0.4f, 0.3f, 0.4f, 0.1f, 0.6f, 0.35f}, {0.4f, 0.5f, 0.4f, 0.4f, 0.5f, 0.1f, 0.5f, 0.65f},
      {0.4f, 0.8f, 0.4f, 0.3f, 0.6f, 0.1f, 0.4f, 0.5f}, {0.5f, 0.5f, 0.4f, 0.2f, 0.3f, 0.05f, 0.4f, 0.55f}},
     {0, 0, 1, 1, 0, 0, 0, 0}},
    {"INDUS", {DM_KICK, DM_SNARE, DM_METAL, DM_NOISE, DM_HAT, DM_FM, DM_TOM, DM_CLAP},
     {{0.3f, 0.45f, 0.6f, 0.6f, 0.6f, 0.85f, 0.9f, 0.5f}, {0.35f, 0.45f, 0.6f, 0.7f, 0.6f, 0.7f, 0.7f, 0.5f},
      {0.45f, 0.4f, 0.6f, 0.4f, 0.5f, 0.6f, 0.5f, 0.4f}, {0.5f, 0.3f, 0.7f, 0.6f, 0.5f, 0.6f, 0.5f, 0.6f},
      {0.6f, 0.12f, 0.75f, 0.5f, 0.6f, 0.5f, 0.45f, 0.65f}, {0.35f, 0.35f, 0.6f, 0.5f, 0.8f, 0.6f, 0.5f, 0.35f},
      {0.3f, 0.4f, 0.6f, 0.5f, 0.5f, 0.6f, 0.55f, 0.45f}, {0.45f, 0.4f, 0.6f, 0.7f, 0.5f, 0.6f, 0.6f, 0.55f}},
     {0, 0, 0, 0, 0, 0, 0, 0}}};

struct SynTHBloKDrum : Module, songlink::Member {   // MATRIX-SONG picks its pattern and mutes it
    enum ParamIds { PAT_PARAM, LEN_PARAM, BPM_PARAM, RUN_PARAM, RESET_PARAM, CLEAR_PARAM, PAGE_PARAM, LEARN_PARAM,
                    BANK_PARAM, RANGE_PARAM, OFFC_PARAM, ONC_PARAM, HEADC_PARAM, HITC_PARAM, BONC_PARAM, BOFFC_PARAM,
                    MIDI_ON_PARAM, FX_ON_PARAM, ACC_EDIT_PARAM, AUDITION_PARAM, SEL_PARAM, ACCENT_PARAM,
                    ENUMS(MODEL_PARAM, 8), ENUMS(VOICE_PARAM, 8 * DK_NUM),
                    // FINAL stage filter (as on MATRIX-DUO), FLT_CUT .. FLT_DRYWET smoothed together
                    FLT_CUT_PARAM, FLT_RES_PARAM, FLT_ENV_PARAM, FLT_STAGES_PARAM, FLT_MODE_PARAM, FLT_ATK_PARAM, FLT_DEC_PARAM,
                    FLT_SUS_PARAM, FLT_REL_PARAM, FLT_AMP_PARAM, FLT_BYPASS_PARAM, FLT_IN_PARAM, FLT_DRYWET_PARAM, FLT_TRIG_PARAM,
                    MASTER_PARAM, DELAY_PARAM, SPACE_PARAM, RAND_PARAM,
                    // voice OLED: the 8 pads, the hit view and its zoom
                    ENUMS(PAD_PARAM, 8), VIEW_PARAM, ZOOM_PARAM,
                    PRE_LVL_PARAM,
                    NUM_PARAMS };
    enum InputIds { CLOCK_INPUT, RESET_INPUT, RUN_INPUT, IN_L_INPUT, IN_R_INPUT, OPEN_INPUT, GLITCH_INPUT, GATE_INPUT, NUM_INPUTS };
    enum OutputIds { OUT_L_OUTPUT, OUT_R_OUTPUT, ENUMS(TRIG_OUTPUT, 8), ENUMS(VOICE_OUTPUT, 8), ENUMS(STRIP_OUTPUT, 8),
                     KNOB_OUTPUT, CLOCK_OUTPUT, PRE_L_OUTPUT, PRE_R_OUTPUT, NUM_OUTPUTS };
    enum LightIds { NUM_LIGHTS };
    static const int kFltSm = FLT_DRYWET_PARAM - FLT_CUT_PARAM + 1;

    uint8_t pat[8][8][8];         // [pattern][row][step]: 0 off, 1 hit, 2 accent
    float kit[8][8][DK_NUM];      // [pattern][row][macro]: each pattern's voice knobs
    int kitPat = 0;               // the pattern whose kit is on the knobs
    DrumVoice voice[8] = {DrumVoice(1), DrumVoice(2), DrumVoice(3), DrumVoice(4), DrumVoice(5), DrumVoice(6), DrumVoice(7), DrumVoice(8)};
    int choke[8] = {0, 0, 0, 1, 1, 0, 0, 0};   // choke group per row, 0 = none
    bool leaveMix = false;        // a patched voice out takes that row out of OUT L / R
    float lvlSm[8], panSm[8], voicePk[8] = {};   // voicePk: each voice's out (V), held peak for its level strip
    float trigV[8] = {};
    dsp::PulseGenerator trigPulse[8], clockPulse;
    // each voice's last hit for the voice OLED: the audio from the hit on, at about 48 kHz (higher rates are
    // averaged down), kRawN samples. The display works out every view from it.
    static const int kRawN = 16384;
    float raw[8][kRawN] = {};
    int rawPos[8] = {kRawN, kRawN, kRawN, kRawN, kRawN, kRawN, kRawN, kRawN}, rawCnt[8] = {};
    float rawAcc[8] = {}, rawRate = 48000.f;
    unsigned hitId[8] = {};
    float padFlash[8] = {};       // seconds left of a pad's flash
    dsp::SchmittTrigger padTrig[8];
    // KIT rows: kits 0..8 factory, 9..16 the user's (STORE, saved in the patch). A kit sets every row's model,
    // macros (in all 8 patterns' kits) and choke group; the UI asks for it, process() loads it.
    struct UserKit { bool used = false; int model[8] = {}; float val[8][DK_NUM] = {}; int choke[8] = {}; };
    UserKit userKit[kNumUserKit];
    int kitLoaded = -1;
    bool storeArm = false;
    bool showGraph = false;       // voice OLED: the hit graph and its view / zoom cells (module menu)
    std::atomic<int> kitReq{-1};
    uint8_t rdSilent = 0;   // rows RANDROYD plays on outside voices: their own level stays at 0 (triggers still fire)
    float rdShape[8][DK_NUM] = {};   // RANDROYD's genre shaping, added to the next kit it loads (then cleared)
    bool rdShapeOn = false;
    void applySilent() { for (int r = 0; r < 8; r++) if (rdSilent >> r & 1) params[VOICE_PARAM + r * DK_NUM + DK_LEVEL].setValue(0.f); }

    int step = -1;
    bool running = true, pendingReset = false;
    float sinceClock = 10.f, clockPeriod = 0.f, internalTimer = 0.f, resetFlash = 0.f;
    float masterVol = 0.8f, preSm = 1.f;
    M8Delay m8delay;
    M8Reverb reverb;
    float delayMix = 0.f, spaceMix = 0.f;
    dsp::SchmittTrigger clkTrig, rstTrig, runTrig, rstBtnTrig, clrTrig, audTrig, gateTrig, randTrig;

    // Maschine JAM, as on MATRIX FORGE
    midi::InputQueue midiIn;
    midi::Output midiOut;
    bool slowSend = false;
    jamown::Link jam;
    bool mute[8] = {}, solo[8] = {};
    bool muteHeld = false, soloHeld = false, gridHeld = false, gridUsed = false, jamReset = false;
    bool clearHeld = false, clearAllDone = false;
    float clearHeldT = 0.f;
    int knobCC = kKnobFirstCC;
    int lastLed[kNumLed];
    bool matchColors = true;
    int ledScan = 0, lastBank = -1, lastPage = -1;
    int stripPage = 0, lastStripPage = -1;
    float tokens = 4.f, resync = 0.f;
    float stripSm[8] = {}, knobSm = 0.f;

    FSFilter flt[2];
    FSAdsr fltEnv;
    float fltSm[kFltSm] = {}, fltGate = 0.f, envS1 = 0.f, envS2 = 0.f;
    bool fltSmInit = false;
    // FORGE FX: a whole GRAIN-FORGE outside the Rack engine (as on MATRIX-DUO); saved in this module's data ("fx")
    SynTHBloKGrainForge fx;
    float fxOnSm = 1.f;

    SynTHBloKDrum() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        songlink::join(this, this, "DRUM", 8, false);
        configParam(PAT_PARAM, 1.f, 8.f, 1.f, "Pattern");
        configParam(LEN_PARAM, 1.f, 8.f, 8.f, "Pattern length (steps)");
        configParam(BPM_PARAM, 40.f, 240.f, 120.f, "Internal tempo (when CLOCK is unpatched)", " BPM");
        paramQuantities[PAT_PARAM]->snapEnabled = true;
        paramQuantities[LEN_PARAM]->snapEnabled = true;
        configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run (a trigger at the RUN jack toggles it)", {"Stopped", "Running"});
        configButton(RESET_PARAM, "Reset (a trigger at the RESET jack also resets)");
        configButton(CLEAR_PARAM, "Clear this pattern");
        configParam(PAGE_PARAM, 1.f, 6.f, 1.f, "JAM matrix page (must match the JAM)");
        paramQuantities[PAGE_PARAM]->snapEnabled = true;
        configSwitch(LEARN_PARAM, 0.f, 1.f, 0.f, "Learn JAM page: then press the TOP-LEFT pad on the JAM", {"Off", "Armed"});
        std::vector<std::string> banks;
        for (int b = 0; b < 8; b++) banks.push_back(std::string(1, (char)('A' + b)) + ": " + kDrBankName[b]);
        configSwitch(BANK_PARAM, 1.f, 8.f, 1.f, "Strip bank", banks);
        configSwitch(RANGE_PARAM, 0.f, 1.f, 0.f, "STRIP CV range", {"0..10 V", "-5..+5 V"});
        configParam(OFFC_PARAM, 0.f, 127.f, 0.f, "LED colour: empty step");
        configParam(ONC_PARAM, 0.f, 127.f, 127.f, "LED colour: active step");
        configParam(HEADC_PARAM, 0.f, 127.f, 30.f, "LED colour: playhead over empty step");
        configParam(HITC_PARAM, 0.f, 127.f, 90.f, "LED colour: playhead over active step");
        configParam(BONC_PARAM, 0.f, 127.f, 127.f, "LED colour: A-H, muted voice");
        configParam(BOFFC_PARAM, 0.f, 127.f, 20.f, "LED colour: A-H, playing voice");
        for (int id = OFFC_PARAM; id <= BOFFC_PARAM; id++) paramQuantities[id]->snapEnabled = true;
        configSwitch(MIDI_ON_PARAM, 0.f, 1.f, 1.f, "JAM MIDI (off = ignore the JAM, its lights dark)", {"Off", "On"});
        configSwitch(FX_ON_PARAM, 0.f, 1.f, 1.f, "FORGE FX (off = OUT L / R dry)", {"Off", "On"});
        configSwitch(ACC_EDIT_PARAM, 0.f, 1.f, 0.f, "Accent edit (a pad click places an accent)", {"Off", "On"});
        configButton(AUDITION_PARAM, "Audition the selected voice");
        configSwitch(SEL_PARAM, 0.f, 7.f, 0.f, "Selected voice (the voice OLED)", {"1", "2", "3", "4", "5", "6", "7", "8"});
        configParam(ACCENT_PARAM, 0.f, 1.f, 0.5f, "Accent depth (plain hits play quieter)", "%", 0.f, 100.f);
        std::vector<std::string> models(kDrumModelName, kDrumModelName + DM_NUM);
        for (int v = 0; v < 8; v++) {
            std::string n = "Row " + std::to_string(v + 1) + " ";
            configSwitch(MODEL_PARAM + v, 0.f, DM_NUM - 1, kKitModel[v], n + "model", models);
            for (int b = 0; b < DK_NUM; b++)
                configParam(VOICE_PARAM + v * DK_NUM + b, 0.f, 1.f, kKitVal[v][b], n + kDrBankName[b], "%", 0.f, 100.f);
            lvlSm[v] = kKitVal[v][DK_LEVEL]; panSm[v] = kKitVal[v][DK_PAN];
        }
        configParam(FLT_CUT_PARAM, 0.f, 1.f, 1.f, "Filter cutoff (LP fully open / HP fully down = off)", " Hz", 1000.f, 20.f);
        configParam(FLT_RES_PARAM, 0.f, 1.f, 0.f, "Filter resonance", "%", 0.f, 100.f);
        configParam(FLT_ENV_PARAM, 0.f, 1.f, 0.f, "Filter envelope amount", "%", 0.f, 100.f);
        configSwitch(FLT_STAGES_PARAM, 0.f, 2.f, 0.f, "Filter stages", {"1 stage (12 dB)", "2 stages (24 dB)", "3 stages (36 dB)"});
        configParam(FLT_MODE_PARAM, 0.f, 1.f, 0.f, "Filter mode (low-pass -> band-pass -> high-pass)", "%", 0.f, 100.f);
        configParam(FLT_ATK_PARAM, 0.f, 1.f, 0.1f, "Filter envelope attack", " ms", 667.f, 3.f);
        configParam(FLT_DEC_PARAM, 0.f, 1.f, 0.45f, "Filter envelope decay", " ms", 333.f, 12.f);
        configParam(FLT_SUS_PARAM, 0.f, 1.f, 0.3f, "Filter envelope sustain", "%", 0.f, 100.f);
        configParam(FLT_REL_PARAM, 0.f, 1.f, 0.4f, "Filter envelope release", " ms", 333.f, 12.f);
        configParam(FLT_AMP_PARAM, 0.f, 1.f, 0.f, "Filter env to level", "%", 0.f, 100.f);
        configSwitch(FLT_BYPASS_PARAM, 0.f, 1.f, 0.f, "Filter env bypass (the ADSR off the filter)", {"Env on", "Env bypassed"});
        configParam(FLT_IN_PARAM, 0.f, 2.f, 1.f, "Filter input gain", "%", 0.f, 100.f);
        configParam(FLT_DRYWET_PARAM, 0.f, 1.f, 1.f, "Filter dry / wet", "%", 0.f, 100.f);
        configSwitch(FLT_TRIG_PARAM, 0.f, 5.f, 0.f, "Filter env trigger",
                     {"Any hit", "Row 1 hits", "Accents", "Every beat", "Every step", "GATE jack"});
        configParam(MASTER_PARAM, 0.f, 1.f, 0.8f, "Master volume (JAM knob on MST or CUE)", "%", 0.f, 100.f);
        configParam(DELAY_PARAM, 0.f, 1.f, 0.f, "Delay, ping-pong dotted 1/8 (JAM knob on GRP)", "%", 0.f, 100.f);
        configParam(SPACE_PARAM, 0.f, 1.f, 0.f, "Space / reverb (JAM knob on IN1)", "%", 0.f, 100.f);
        configButton(RAND_PARAM, "Random grooves on all 8 patterns (built from each row's model)");
        for (int r = 0; r < 8; r++) configButton(PAD_PARAM + r, "Pad " + std::to_string(r + 1) + ": play row " + std::to_string(r + 1) + " and select it");
        configSwitch(VIEW_PARAM, 0.f, 4.f, 0.f, "Voice OLED view", {"Waveform", "Outline", "Envelope (peak + RMS, dB)", "Spectrum", "Spectrogram"});
        configSwitch(ZOOM_PARAM, 0.f, 3.f, 0.f, "Voice OLED zoom (from the hit on)", {"Whole hit", "First half", "First quarter", "First eighth"});
        configParam(PRE_LVL_PARAM, 0.f, 1.f, 0.5f, "PRE out level (only the PRE L / R jacks)", "%", 0.f, 200.f);
        // FX knobs set the engine's values straight away (it smooths them itself), as on MATRIX-DUO
        for (ParamQuantity* q : fx.paramQuantities) q->smoothEnabled = false;
        configInput(CLOCK_INPUT, "Clock (16th notes; the BPM runs it when unpatched)");
        configInput(RESET_INPUT, "Reset (trigger: same as pressing RESET)");
        configInput(RUN_INPUT, "Run (trigger: toggles RUN, like pressing it)");
        configInput(IN_L_INPUT, "Left (added to the drums before FORGE FX)");
        configInput(IN_R_INPUT, "Right (left is copied here when unpatched)");
        configInput(OPEN_INPUT, "Open (gate): FORGE FX PULSE lets everything through while high");
        configInput(GLITCH_INPUT, "FORGE FX glitch now (trigger)");
        configInput(GATE_INPUT, "Gate: FORGE FX filter env, and the FINAL filter env when its trigger is GATE");
        configOutput(OUT_L_OUTPUT, "Left: the drums through MASTER");
        configOutput(OUT_R_OUTPUT, "Right: the drums through MASTER");
        for (int r = 0; r < 8; r++) {
            configOutput(TRIG_OUTPUT + r, "Row " + std::to_string(r + 1) + " trigger (hit 6 V, accent 10 V)");
            configOutput(VOICE_OUTPUT + r, "Row " + std::to_string(r + 1) + " audio");
        }
        for (int i = 0; i < 8; i++) configOutput(STRIP_OUTPUT + i, "Strip " + std::to_string(i + 1) + " CV (voice " + std::to_string(i + 1) + " level)");
        configOutput(KNOB_OUTPUT, "Knob CV (0..10 V: the JAM knob's page)");
        configOutput(CLOCK_OUTPUT, "Clock: a 10 V, 5 ms pulse per 1/16 step while running");
        configOutput(PRE_L_OUTPUT, "PRE left: the voice mix before the FINAL filter, DELAY, SPACE, FORGE FX and MASTER");
        configOutput(PRE_R_OUTPUT, "PRE right: the voice mix before the FINAL filter, DELAY, SPACE, FORGE FX and MASTER");
        for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
        clearAll();
        loadDefaultBeat();
        fillKits();
    }

    void clearPattern(int p) { for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) pat[p][r][c] = 0; }
    void clearAll() { for (int p = 0; p < 8; p++) clearPattern(p); }
    // Pattern 1 starts with a beat, so the kit sounds before you program it.
    void loadDefaultBeat() {
        static const char* beat[8] = {"20000100", "00001000", "00000000", "10101000", "00000010", "00000000", "00000000", "00000000"};
        for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) pat[0][r][c] = (uint8_t)(beat[r][c] - '0');
    }

    // RAND: 8 new patterns, each its own groove. Steps are 16ths, so a pattern is two beats (beats on steps
    // 1 and 5). The four styles (house/techno, breakbeat, electro, broken) are shuffled over the 8 patterns,
    // two each, and each row's part comes from its model; the first HAT row plays closed hats, a second one
    // open hats. A pattern that repeats an earlier one is rolled again.
    void randomizeAll() {
        auto rnd = []() { return random::uniform(); };
        auto pick = [&](const std::vector<int>& v) { return v[std::min((int)(rnd() * v.size()), (int)v.size() - 1)]; };
        auto euclid = [&](int k, int rot) {   // k hits spread over 8 steps, rotated
            int m = 0;
            for (int i = 0; i < 8; i++) if ((i * k) % 8 < k) m |= 1 << ((i + rot) % 8);
            return m;
        };
        auto sprinkle = [&](int allowed, float p) {   // each allowed step with chance p
            int m = 0;
            for (int i = 0; i < 8; i++) if ((allowed >> i & 1) && rnd() < p) m |= 1 << i;
            return m;
        };
        enum { FOUR, BREAK, ELECTRO, BROKEN };
        static const std::vector<int> kKicks[4] = {{0x11, 0x11, 0x91, 0x19, 0x31}, {0x41, 0x09, 0x85, 0x21, 0x49},
                                                   {0x41, 0x09, 0x45, 0x81, 0x49}, {0x21, 0xC1, 0x29, 0x61}};
        static const std::vector<int> kHats = {0xFF, 0x55, 0xDD, 0x6D, 0xEE, 0xF5};
        int styles[8] = {FOUR, BREAK, ELECTRO, BROKEN, FOUR, BREAK, ELECTRO, BROKEN};
        for (int i = 7; i > 0; i--) std::swap(styles[i], styles[std::min((int)(rnd() * (i + 1)), i)]);
        bool hasClap = false;
        int role[8], seen[DM_NUM] = {};
        for (int r = 0; r < 8; r++) {
            int m = model(r);
            hasClap |= m == DM_CLAP;
            role[r] = m * 4 + std::min(seen[m]++, 3);
        }

        int sig[8][8];   // each pattern's hit masks, to spot repeats
        for (int p = 0; p < 8; p++) {
            int style = styles[p];
            int h[8], a[8];
            for (int tries = 0; tries < 20; tries++) {
                int kick = pick(kKicks[style]);
                int hatMode = style == FOUR ? pick({0xFF, 0x55, 0xEE, 0xDD}) : pick(kHats);
                for (int r = 0; r < 8; r++) {
                    int m = role[r] / 4, n = role[r] % 4;
                    h[r] = a[r] = 0;
                    switch (m) {
                        case DM_KICK:
                            if (n == 0) { h[r] = kick; a[r] = style == FOUR ? 0x11 : 0x01; }
                            else h[r] = sprinkle(0xEE & ~kick, 0.25f);
                            break;
                        case DM_SNARE:
                            if (n == 0) {
                                bool back = !(hasClap && style == FOUR) || rnd() < 0.5f;
                                h[r] = (back ? 0x10 : 0) | sprinkle(0xCA, style == FOUR ? 0.15f : 0.3f);   // backbeat + ghosts
                                a[r] = 0x10;
                            } else h[r] = sprinkle(0xEA, 0.2f);
                            break;
                        case DM_CLAP:
                            h[r] = ((style == FOUR || rnd() < 0.7f) ? 0x10 : 0) | sprinkle(0x80, 0.2f);
                            a[r] = 0x10;
                            break;
                        case DM_HAT:
                            if (n == 0) { h[r] = hatMode; a[r] = style == FOUR ? 0x44 : sprinkle(0x55, 0.5f); }
                            else { h[r] = style == FOUR ? 0x44 : pick({0x40, 0x04, 0x44, 0x80}); a[r] = h[r]; }
                            break;
                        case DM_TOM: h[r] = rnd() < 0.35f ? 0 : sprinkle(0xEE, 0.18f); break;
                        case DM_METAL:
                            h[r] = rnd() < 0.25f ? 0 : style == FOUR ? pick({0x44, 0x55, euclid(3, 2)}) : euclid(pick({2, 3}), (int)(rnd() * 8));
                            a[r] = sprinkle(h[r], 0.3f);
                            break;
                        case DM_FM:
                            h[r] = rnd() < 0.2f ? 0 : euclid(pick({2, 3, 3, 5}), (int)(rnd() * 8)) & ~0x01;
                            a[r] = sprinkle(h[r], 0.3f);
                            break;
                        default:   // NOISE: a sweep on the backbeat or a pickup
                            h[r] = rnd() < 0.3f ? 0 : pick({0x10, 0x80, 0x90, 0x44});
                            break;
                    }
                }
                // a second HAT row (open hats) cuts the closed hats under it, so they don't choke each other
                int openMask = 0;
                for (int r = 0; r < 8; r++) if (role[r] / 4 == DM_HAT && role[r] % 4 > 0) openMask |= h[r];
                for (int r = 0; r < 8; r++) if (role[r] == DM_HAT * 4) h[r] &= ~openMask;
                bool repeat = false;
                for (int q = 0; q < p && !repeat; q++) {
                    bool same = true;
                    for (int r = 0; r < 8; r++) same &= sig[q][r] == h[r];
                    repeat = same;
                }
                if (!repeat) break;
            }
            for (int r = 0; r < 8; r++) {
                sig[p][r] = h[r];
                for (int s = 0; s < 8; s++) pat[p][r][s] = (h[r] >> s & 1) ? ((a[r] >> s & 1) ? 2 : 1) : 0;
            }
        }
        // a kit for each pattern: each row's macros scattered round its model's kit sound, flavoured by that
        // pattern's style (electro: long boomy kick, more FM; breakbeat: snappier snare; house/techno: more
        // drive). The kick, first snare and claps stay centred; the rest spread across the stereo field.
        static const float kNoiseVal[DK_NUM] = {0.5f, 0.35f, 0.5f, 0.4f, 0.5f, 0.f, 0.4f, 0.5f};
        static const float kSpread[DK_NUM] = {0.18f, 0.18f, 0.25f, 0.25f, 0.35f, 0.f, 0.06f, 0.f};
        for (int p = 0; p < 8; p++)
        for (int r = 0; r < 8; r++) {
            int style = styles[p];
            int m = role[r] / 4, n = role[r] % 4;
            const float* c = m == DM_KICK ? kKitVal[0] : m == DM_SNARE ? kKitVal[1] : m == DM_CLAP ? kKitVal[2]
                           : m == DM_HAT ? kKitVal[n == 0 ? 3 : 4] : m == DM_TOM ? kKitVal[5] : m == DM_METAL ? kKitVal[6]
                           : m == DM_FM ? kKitVal[7] : kNoiseVal;
            float v[DK_NUM];
            for (int b = 0; b < DK_NUM; b++) v[b] = c[b] + (rnd() * 2.f - 1.f) * kSpread[b];
            v[DK_DRIVE] = c[DK_DRIVE] + rnd() * (style == FOUR || style == ELECTRO ? 0.35f : 0.2f);
            bool centre = (m == DM_KICK || m == DM_SNARE) ? n == 0 : m == DM_CLAP;
            v[DK_PAN] = centre ? 0.5f : 0.5f + (rnd() * 2.f - 1.f) * 0.3f;
            if (m == DM_KICK && n == 0) {
                if (style == ELECTRO) { v[DK_DECAY] += 0.2f; v[DK_TUNE] -= 0.05f; }
                if (style == FOUR) v[DK_DECAY] += 0.05f;
            }
            if (m == DM_SNARE && style == BREAK) v[DK_SNAP] += 0.15f;
            if (m == DM_FM && style == ELECTRO) v[DK_MOD] += 0.2f;
            if (m == DM_HAT) v[DK_DECAY] = n == 0 ? std::min(v[DK_DECAY], 0.3f) : std::max(v[DK_DECAY], 0.45f);   // closed stays closed
            for (int b = 0; b < DK_NUM; b++) kit[p][r][b] = clamp(v[b], 0.f, 1.f);
        }
        kitPat = curPat();
        applyKit(kitPat);
    }

    // Each pattern has its own voice-knob positions (a kit). Changing pattern keeps the knobs as they are in
    // the pattern being left and loads the new pattern's kit. A new or old patch starts with the knobs in
    // all 8 kits.
    void captureKit(int p) { for (int r = 0; r < 8; r++) for (int b = 0; b < DK_NUM; b++) kit[p][r][b] = params[vp(r, b)].getValue(); }
    void applyKit(int p) { for (int r = 0; r < 8; r++) for (int b = 0; b < DK_NUM; b++) params[vp(r, b)].setValue(kit[p][r][b]); applySilent(); }
    void fillKits() { for (int p = 0; p < 8; p++) captureKit(p); kitPat = curPat(); }
    // KIT row: load kit k (audio thread, from kitReq), or store the rows as user kit u (UI thread)
    void loadKit(int k) {
        const int* mdl; const float (*val)[DK_NUM]; const int* ck;
        if (k < kNumFactoryKit) { mdl = kFactoryKit[k].model; val = kFactoryKit[k].val; ck = kFactoryKit[k].choke; }
        else {
            const UserKit& u = userKit[k - kNumFactoryKit];
            if (!u.used) return;
            mdl = u.model; val = u.val; ck = u.choke;
        }
        for (int r = 0; r < 8; r++) {
            params[MODEL_PARAM + r].setValue((float)mdl[r]);
            choke[r] = ck[r];
            for (int p = 0; p < 8; p++) for (int b = 0; b < DK_NUM; b++)
                kit[p][r][b] = rdShapeOn ? clamp(val[r][b] + rdShape[r][b], 0.f, 1.f) : val[r][b];
        }
        rdShapeOn = false;
        applyKit(kitPat);
        kitLoaded = k;
        applySilent();
    }
    void storeKit(int u) {
        UserKit& s = userKit[u];
        for (int r = 0; r < 8; r++) {
            s.model[r] = model(r); s.choke[r] = choke[r];
            for (int b = 0; b < DK_NUM; b++) s.val[r][b] = params[vp(r, b)].getValue();
        }
        s.used = true;
        kitLoaded = kNumFactoryKit + u;
    }

    void onReset() override {
        Module::onReset();
        step = -1;
        for (int r = 0; r < 8; r++) { mute[r] = solo[r] = false; choke[r] = (r == 3 || r == 4) ? 1 : 0; }
        clearAll();
        loadDefaultBeat();
        fillKits();
        kitLoaded = -1; storeArm = false;
        for (int i = 0; i < SynTHBloKGrainForge::NUM_PARAMS; i++) fx.params[i].setValue(fx.paramQuantities[i]->getDefaultValue());
        fx.onReset();
    }

    int curPat() { return clamp((int)params[PAT_PARAM].getValue(), 1, 8) - 1; }
    int curPage() { return clamp((int)params[PAGE_PARAM].getValue(), 1, 6); }
    int bank() { return clamp((int)params[BANK_PARAM].getValue(), 1, 8) - 1; }
    int sel() { return clamp((int)params[SEL_PARAM].getValue(), 0, 7); }
    int model(int v) { return clamp((int)params[MODEL_PARAM + v].getValue(), 0, DM_NUM - 1); }
    int vp(int v, int b) { return VOICE_PARAM + v * DK_NUM + b; }
    int padNote(int r, int c) { return std::min(127, 12 * (curPage() - 1) + 56 + c - 8 * r); }
    int padState(int r, int c) { return pat[curPat()][r][c]; }
    bool padOn(int r, int c) { return pat[curPat()][r][c] != 0; }
    // Click: off <-> hit (ACC on: off <-> accent); accent: hit <-> accent, an empty step becomes an accent.
    void togglePad(int r, int c) {
        uint8_t& s = pat[curPat()][r][c];
        if (params[ACC_EDIT_PARAM].getValue() > 0.5f) s = s == 2 ? 0 : 2;
        else s = s ? 0 : 1;
    }
    void toggleAccent(int r, int c) { uint8_t& s = pat[curPat()][r][c]; s = s == 2 ? 1 : 2; }
    // the strips (panel, JAM, STRIP CV) are the voices' levels, as on MATRIX-LOOP
    float stripGet(int i) { return params[vp(i, DK_LEVEL)].getValue(); }
    void stripSet(int i, float x) { params[vp(i, DK_LEVEL)].setValue(clamp(x, 0.f, 1.f)); }
    bool anySolo() { for (int r = 0; r < 8; r++) if (solo[r]) return true; return false; }
    bool anyMute() { for (int r = 0; r < 8; r++) if (mute[r]) return true; return false; }
    bool trackOn(int r) { return !mute[r] && (!anySolo() || solo[r]); }
    void stepParam(int id, int delta, int lo, int hi, bool wrap) {
        int v = (int)params[id].getValue() + delta;
        if (wrap) v = lo + ((v - lo) % (hi - lo + 1) + (hi - lo + 1)) % (hi - lo + 1);
        params[id].setValue((float)clamp(v, lo, hi));
    }
    int knobParam() {
        switch (knobCC) {
            case kKnobFirstCC + 1: return DELAY_PARAM;
            case kKnobFirstCC + 2: return SPACE_PARAM;
            default: return MASTER_PARAM;
        }
    }

    // Row r plays: its choke group stops, then the voice, its trigger jack and (maybe) the filter env.
    void hit(int r, bool accent) {
        if (choke[r])
            for (int o = 0; o < 8; o++) if (o != r && choke[o] == choke[r]) voice[o].choke();
        float vel = accent ? 1.f : 1.f - 0.5f * params[ACCENT_PARAM].getValue();
        voice[r].trigger(vel, model(r));
        trigPulse[r].trigger(0.01f);
        trigV[r] = accent ? 10.f : 6.f;
        rawPos[r] = 0; rawCnt[r] = 0; rawAcc[r] = 0.f; hitId[r]++;
        padFlash[r] = 0.15f;
    }

    // ---- JAM ----
    void sendNote(int n, int vel) { midi::Message m; m.bytes = {0x90, (uint8_t)n, (uint8_t)clamp(vel, 0, 127)}; if (slowSend) std::this_thread::sleep_for(std::chrono::microseconds(400)); midiOut.sendMessage(m); }
    void sendCC(int cc, int v) { midi::Message m; m.bytes = {0xB0, (uint8_t)cc, (uint8_t)clamp(v, 0, 127)}; if (slowSend) std::this_thread::sleep_for(std::chrono::microseconds(400)); midiOut.sendMessage(m); }
    void clearLeds() {
        for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) sendNote(padNote(r, c), 0);
        for (int i = 0; i < 8; i++) { sendCC(i, 0); sendCC(8 + i, 0); sendCC(48 + 8 * stripPage + i, 0); }
        for (int i = 0; i < kNumBtn; i++) sendCC(kBtnCC[i], 0);
        for (auto& n : kBtnNote) if (n[0] > padNote(0, 7)) sendNote(n[0], 0);
        for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
    }
    void onRemove(const RemoveEvent& e) override { if (jam.active) clearLeds(); jamown::release(id); }
    void onBypass(const BypassEvent& e) override { if (jam.active) clearLeds(); jamown::release(id); jam.active = false; }
    void onUnBypass(const UnBypassEvent& e) override { for (int i = 0; i < kNumLed; i++) lastLed[i] = -1; resync = 0.f; }
    // Rack closing / the module deleted: lights off, paced so the JAM takes every message
    ~SynTHBloKDrum() { songlink::leave(this); if (jam.active) { slowSend = true; clearLeds(); } jamown::release(id); }
    int songPatternNow() override { return curPat(); }
    void songSetPattern(int p) override { params[PAT_PARAM].setValue((float)(clamp(p, 0, 7) + 1)); }

    void toggle(Param& p) { p.setValue(p.getValue() > 0.5f ? 0.f : 1.f); }
    void jamButton(int cc, int b) {
        bool down = b > 0;
        if (cc == CC_SOLO) { soloHeld = down; return; }
        if (cc == CC_MUTE) { muteHeld = down; return; }
        if (cc == CC_GRID) {   // held: pads place accents; a tap on its own: +-5V
            if (down) { gridHeld = true; gridUsed = false; }
            else { if (gridHeld && !gridUsed) toggle(params[RANGE_PARAM]); gridHeld = false; }
            return;
        }
        if (cc == CC_CLEAR && !down) { clearHeld = false; return; }
        if (!down) return;
        switch (cc) {
            case CC_CLEAR: clearPattern(curPat()); clearHeld = true; clearHeldT = 0.f; clearAllDone = false; break;
            case CC_DUPLICATE: {
                int from = curPat(), to = (from + 1) % 8;
                for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) pat[to][r][c] = pat[from][r][c];
                params[PAT_PARAM].setValue((float)(to + 1));
            } break;
            case CC_SONG: toggle(params[FX_ON_PARAM]); break;
            case CC_UP: stepParam(LEN_PARAM, 1, 1, 8, false); break;
            case CC_DOWN: stepParam(LEN_PARAM, -1, 1, 8, false); break;
            case CC_PLAY: toggle(params[RUN_PARAM]); break;
            case CC_REC: toggle(params[LEARN_PARAM]); break;
            case CC_PREV: stepParam(PAT_PARAM, -1, 1, 8, true); break;
            case CC_NEXT: stepParam(PAT_PARAM, 1, 1, 8, true); break;
            case CC_TEMPO: jamReset = true; break;
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
                    if (padNote(r, c) == a) {
                        if (muteHeld) mute[r] = !mute[r];
                        else if (soloHeld) solo[r] = !solo[r];
                        else if (gridHeld) { toggleAccent(r, c); gridUsed = true; }
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
                    lastLed[kKnobLed] = b;
                }
                else if (a == kKnobPushCC && b > 0) paramQuantities[knobParam()]->reset();
                else if (a >= 0 && a < 8 && b > 0) params[PAT_PARAM].setValue((float)(a + 1));
                else if (a >= 8 && a < 16 && b > 0) mute[a - 8] = !mute[a - 8];
                else if (a >= 48 && a < 112) {
                    stripPage = (a - 48) / 8;
                    stripSet((a - 48) % 8, b / 127.f);
                }
            }
        }
    }

    int desiredLed(int idx) {
        if (idx == kKnobLed) return (int)std::round(params[knobParam()].getValue() * 127.f);
        if (idx >= 88) {
            switch (kBtnCC[idx - 88]) {
                case CC_SONG: return params[FX_ON_PARAM].getValue() > 0.5f ? 127 : 0;
                case CC_PLAY: return running ? 127 : 0;
                case CC_REC: return params[LEARN_PARAM].getValue() > 0.5f ? 127 : 0;
                case CC_TEMPO: return resetFlash > 0.f ? 127 : 0;
                case CC_GRID: return gridHeld || params[RANGE_PARAM].getValue() > 0.5f ? 127 : 0;
                case CC_SOLO: return soloHeld || anySolo() ? 127 : 0;
                case CC_MUTE: return muteHeld || anyMute() ? 127 : 0;
                default: return 127;
            }
        }
        if (matchColors && (idx < 72 || idx >= 80)) {
            if (idx < 64) {
                // accent = the page's bright colour, hit one shade down, empty dark. Muted: hits dim.
                int r = idx / 8, c = idx % 8, p = curPat(), s = padState(r, c);
                if (!trackOn(r)) return s ? kJamDim[p] : 0;
                if (running && step >= 0 && c == step) return s ? kJamWhite : kJamWhite - 2;
                return s == 2 ? kJamBright[p] : s ? kJamBright[p] - 1 : 0;
            }
            int i = idx < 72 ? idx - 64 : idx - 80;
            bool on = idx < 72 ? i == curPat() : mute[i];   // A-H: lit while its voice is muted
            return on ? kJamBright[i] : kJamDim[i];
        }
        if (idx < 72) {
            int on = (int)params[ONC_PARAM].getValue(), off = (int)params[OFFC_PARAM].getValue();
            int head = (int)params[HEADC_PARAM].getValue(), hitc = (int)params[HITC_PARAM].getValue();
            if (idx < 64) {
                int r = idx / 8, c = idx % 8;
                bool a = padOn(r, c);
                if (running && step >= 0 && c == step) return a ? hitc : head;
                return a ? on : off;
            }
            return (idx - 64) == curPat() ? on : off;
        }
        if (idx < 80) return (int)std::round(stripGet(idx - 72) * 127.f);
        return mute[idx - 80] ? (int)params[BONC_PARAM].getValue() : (int)params[BOFFC_PARAM].getValue();
    }
    void sendLed(int idx, int d) {
        if (!jam.active) return;
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
    void processJamLeds(const ProcessArgs& args) {
        resync += args.sampleTime;
        if (curPage() != lastPage || bank() != lastBank || stripPage != lastStripPage || resync > 3.f) {
            for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
            lastPage = curPage(); lastBank = bank(); lastStripPage = stripPage; resync = 0.f;
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
    }

    void process(const ProcessArgs& args) override {
        const float songG = songPoll(args.sampleTime);   // MATRIX-SONG: its pattern requests, and its mute on OUT L / R
        // the JAM follows the last clicked module (JamOwner.hpp): losing it goes dark, gaining it resends all
        int own = jam.update(id, params[MIDI_ON_PARAM].getValue() > 0.5f);
        if (own > 0) for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
        else if (own < 0) clearLeds();
        bool midiOn = jam.active;
        if (midiOn) processJamIn(args);
        else { midi::Message m; while (midiIn.tryPop(&m, args.frame)) {} }

        // ---- clock: the CLOCK jack, or the BPM when unpatched ----
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

        if (runTrig.process(inputs[RUN_INPUT].getVoltage())) toggle(params[RUN_PARAM]);
        running = params[RUN_PARAM].getValue() > 0.5f;
        bool rst = rstTrig.process(inputs[RESET_INPUT].getVoltage()) | rstBtnTrig.process(params[RESET_PARAM].getValue()) | jamReset;
        jamReset = false;
        if (rst) { step = -1; pendingReset = true; internalTimer = 0.f; resetFlash = 0.15f; }
        resetFlash = std::max(0.f, resetFlash - args.sampleTime);
        if (clrTrig.process(params[CLEAR_PARAM].getValue())) clearPattern(curPat());
        if (randTrig.process(params[RAND_PARAM].getValue())) randomizeAll();
        if (curPat() != kitPat) { captureKit(kitPat); kitPat = curPat(); applyKit(kitPat); }
        if (clearHeld && !clearAllDone && (clearHeldT += args.sampleTime) > 1.f) { clearAll(); clearAllDone = true; }

        int envTrig = clamp((int)params[FLT_TRIG_PARAM].getValue(), 0, 5);
        if (edge && running) {
            clockPulse.trigger(0.005f);
            int len = clamp((int)params[LEN_PARAM].getValue(), 1, 8);
            step = (pendingReset || step < 0) ? 0 : (step + 1) % len;
            pendingReset = false;
            bool any = false, row1 = false, acc = false;
            for (int r = 0; r < 8; r++) {
                int s = pat[curPat()][r][step];
                if (!s || !trackOn(r)) continue;
                hit(r, s == 2);
                any = true; row1 |= r == 0; acc |= s == 2;
            }
            // filter ADSR: its trigger picks what fires it; it holds open for half a step (a beat: two steps)
            bool fire = envTrig == 0 ? any : envTrig == 1 ? row1 : envTrig == 2 ? acc : envTrig == 3 ? step % 4 == 0 : envTrig == 4;
            if (fire) { fltEnv.trigger(); fltGate = (envTrig == 3 ? 2.f : 0.5f) * stepSec * args.sampleRate; }
        }
        if (envTrig == 5) {
            if (gateTrig.process(inputs[GATE_INPUT].getVoltage(), 0.1f, 1.f)) { fltEnv.trigger(); fltGate = 0.f; }
            if (inputs[GATE_INPUT].getVoltage() < 0.1f) fltEnv.release();
        }
        if (audTrig.process(params[AUDITION_PARAM].getValue())) hit(sel(), false);
        if (kitReq.load(std::memory_order_relaxed) >= 0) { int k = kitReq.exchange(-1); if (k >= 0) loadKit(k); }
        // an OLED pad plays its row straight away (muted or not) and selects it (its MODEL cell lights)
        for (int r = 0; r < 8; r++) {
            if (padTrig[r].process(params[PAD_PARAM + r].getValue())) { hit(r, false); params[SEL_PARAM].setValue((float)r); }
            padFlash[r] = std::max(0.f, padFlash[r] - args.sampleTime);
        }
        for (int r = 0; r < 8; r++) outputs[TRIG_OUTPUT + r].setVoltage(trigPulse[r].process(args.sampleTime) ? trigV[r] : 0.f);
        outputs[CLOCK_OUTPUT].setVoltage(clockPulse.process(args.sampleTime) ? 10.f : 0.f);

        // ---- the 8 voices: their own outs, and panned into the mix ----
        float sr = args.sampleRate;
        float km = 1.f - std::exp(-args.sampleTime * 1000.f);
        float l = 0.f, r = 0.f;
        int dec = std::max(1, (int)std::round(sr / 48000.f));
        rawRate = sr / dec;
        const float pkFall = std::exp(-args.sampleTime / 0.3f);
        for (int v = 0; v < 8; v++) {
            float p[DK_NUM];
            for (int b = 0; b < DK_NUM; b++) p[b] = params[vp(v, b)].getValue();
            lvlSm[v] += (p[DK_LEVEL] - lvlSm[v]) * km;
            panSm[v] += (p[DK_PAN] - panSm[v]) * km;
            float s = voice[v].process(sr, model(v), p) * lvlSm[v];
            outputs[VOICE_OUTPUT + v].setVoltage(clamp(5.f * s, -10.f, 10.f));
            voicePk[v] = std::max(std::fabs(5.f * s), voicePk[v] * pkFall);
            if (rawPos[v] < kRawN) {
                rawAcc[v] += s;
                if (++rawCnt[v] >= dec) { raw[v][rawPos[v]++] = rawAcc[v] / dec; rawCnt[v] = 0; rawAcc[v] = 0.f; }
            }
            if (leaveMix && outputs[VOICE_OUTPUT + v].isConnected()) continue;
            float pan = panSm[v] * float(M_PI) * 0.5f;
            l += s * std::cos(pan) * 1.414f;
            r += s * std::sin(pan) * 1.414f;
        }
        // ---- PRE L / R: the voice mix before any effect, at OUT's dry level, through PRE LVL only ----
        preSm += (2.f * params[PRE_LVL_PARAM].getValue() - preSm) * km;
        outputs[PRE_L_OUTPUT].setVoltage(6.f * std::tanh(0.6f * l) * preSm);
        outputs[PRE_R_OUTPUT].setVoltage(6.f * std::tanh(0.6f * r) * preSm);

        // ---- FINAL stage filter (as on MATRIX-DUO, in stereo): cutoff + ADSR sweep, resonance, stages, LP / BP / HP ----
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
        float env = envS2 * (1.f - fs(FLT_BYPASS_PARAM));
        float fenv = fs(FLT_ENV_PARAM) * std::fabs(fs(FLT_ENV_PARAM));   // squared: fine sweeps at the low end
        float fcut = clamp(fs(FLT_CUT_PARAM) + env * fenv, 0.f, 1.f);
        float fres = fs(FLT_RES_PARAM) + 0.2f * env * std::fabs(fenv);
        float ea = std::min(env, 1.f) * std::fabs(fenv);
        float ch[2] = {l, r};
        for (int c = 0; c < 2; c++) {
            float wet = ch[c] * fs(FLT_IN_PARAM);
            flt[c].process(wet, fcut, fres, sr, fs(FLT_STAGES_PARAM), 2.f * fs(FLT_MODE_PARAM));
            wet *= 1.f - ea * (0.25f + 0.2f * fs(FLT_RES_PARAM));
            wet *= 1.f - fs(FLT_AMP_PARAM) * (1.f - fs(FLT_BYPASS_PARAM)) * (1.f - std::min(envS2, 1.f));
            ch[c] += (wet - ch[c]) * fs(FLT_DRYWET_PARAM);
        }
        l = ch[0]; r = ch[1];
        // ---- delay (dotted 1/8 = 3 steps) and space, then FORGE FX (it always runs, so ON crossfades), then MASTER ----
        float dl, dr, rl, rr;
        m8delay.process(l, r, sr, stepSec * 3.f, dl, dr);
        delayMix += (params[DELAY_PARAM].getValue() - delayMix) * km;
        l += dl * delayMix * 0.8f;
        r += dr * delayMix * 0.8f;
        reverb.process(l, r, sr, rl, rr);
        spaceMix += (params[SPACE_PARAM].getValue() - spaceMix) * km;
        l += rl * spaceMix;
        r += rr * spaceMix;
        float inL = inputs[IN_L_INPUT].getVoltage();
        float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : inL;
        float dryL = 6.f * std::tanh(0.6f * l) + inL, dryR = 6.f * std::tanh(0.6f * r) + inR, fxL, fxR;
        typedef SynTHBloKGrainForge G;
        const int fwd[3][2] = {{OPEN_INPUT, G::OPEN_INPUT}, {GLITCH_INPUT, G::GLITCH_INPUT}, {GATE_INPUT, G::GATE_INPUT}};
        for (auto& f : fwd) {
            fx.inputs[f[1]].setChannels(inputs[f[0]].getChannels());
            fx.inputs[f[1]].setVoltage(inputs[f[0]].getVoltage());
        }
        fx.processFx(args, dryL, dryR, edge && running, stepSec, rst, fxL, fxR);
        fxOnSm += (params[FX_ON_PARAM].getValue() - fxOnSm) * km;
        masterVol += (params[MASTER_PARAM].getValue() - masterVol) * km;
        outputs[OUT_L_OUTPUT].setVoltage((dryL + (5.f * fxL - dryL) * fxOnSm) * masterVol * songG);
        outputs[OUT_R_OUTPUT].setVoltage((dryR + (5.f * fxR - dryR) * fxOnSm) * masterVol * songG);
        songMeter(outputs[OUT_L_OUTPUT].getVoltage(), outputs[OUT_R_OUTPUT].getVoltage());

        // ---- STRIP CV = the strips (the voices' levels), KNOB CV = the knob's page ----
        bool bip = params[RANGE_PARAM].getValue() > 0.5f;
        for (int i = 0; i < 8; i++) {
            stripSm[i] += (stripGet(i) * 10.f - (bip ? 5.f : 0.f) - stripSm[i]) * km;
            outputs[STRIP_OUTPUT + i].setVoltage(stripSm[i]);
        }
        knobSm += (params[knobParam()].getValue() * 10.f - knobSm) * km;
        outputs[KNOB_OUTPUT].setVoltage(knobSm);

        if (midiOn) processJamLeds(args);
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_t* arr = json_array();
        for (int p = 0; p < 8; p++) {
            std::string s;
            for (int i = 0; i < 64; i++) s += (char)('0' + pat[p][i / 8][i % 8]);
            json_array_append_new(arr, json_string(s.c_str()));
        }
        json_object_set_new(root, "patterns", arr);   // 64 chars per pattern, row by row: 0 off, 1 hit, 2 accent
        std::string ms, ss, cs;
        for (int i = 0; i < 8; i++) { ms += mute[i] ? '1' : '0'; ss += solo[i] ? '1' : '0'; cs += (char)('0' + choke[i]); }
        json_object_set_new(root, "mute", json_string(ms.c_str()));
        json_object_set_new(root, "solo", json_string(ss.c_str()));
        json_object_set_new(root, "choke", json_string(cs.c_str()));
        json_t* kj = json_array();   // 8 x 8 x 8 voice knobs, pattern by pattern; the shown pattern's from the knobs
        for (int p = 0; p < 8; p++)
            for (int r = 0; r < 8; r++)
                for (int b = 0; b < DK_NUM; b++)
                    json_array_append_new(kj, json_real(p == kitPat ? params[vp(r, b)].getValue() : kit[p][r][b]));
        json_object_set_new(root, "kits", kj);
        json_t* uk = json_array();   // the KIT row's user kits: null when empty
        for (const UserKit& u : userKit) {
            if (!u.used) { json_array_append_new(uk, json_null()); continue; }
            json_t* o = json_object();
            json_t* md = json_array(); json_t* vl = json_array();
            std::string cs;
            for (int r = 0; r < 8; r++) {
                json_array_append_new(md, json_integer(u.model[r]));
                for (int b = 0; b < DK_NUM; b++) json_array_append_new(vl, json_real(u.val[r][b]));
                cs += (char)('0' + u.choke[r]);
            }
            json_object_set_new(o, "models", md);
            json_object_set_new(o, "values", vl);
            json_object_set_new(o, "choke", json_string(cs.c_str()));
            json_array_append_new(uk, o);
        }
        json_object_set_new(root, "userKits", uk);
        json_object_set_new(root, "kitLoaded", json_integer(kitLoaded));
        json_object_set_new(root, "leaveMix", json_boolean(leaveMix));
        json_object_set_new(root, "showGraph", json_boolean(showGraph));
        json_object_set_new(root, "matchColors", json_boolean(matchColors));
        json_object_set_new(root, "rdSilent", json_integer(rdSilent));
        json_object_set_new(root, "in", midiIn.toJson());
        json_object_set_new(root, "out", midiOut.toJson());
        json_t* fj = json_object();
        json_object_set_new(fj, "params", fx.paramsToJson());
        json_object_set_new(fj, "data", fx.dataToJson());
        json_object_set_new(root, "fx", fj);
        return root;
    }
    // FORGE FX settings: the engine's params, then what GRAIN-FORGE's fromJson restores from its data (as MATRIX-DUO)
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
    void dataFromJson(json_t* root) override {
        // per-pattern kits; a patch from before them gets its knobs in all 8 (the params are loaded already)
        json_t* kj = json_object_get(root, "kits");
        if (kj && json_array_size(kj) == (size_t)(8 * 8 * DK_NUM)) {
            for (int i = 0; i < 8 * 8 * DK_NUM; i++)
                kit[i / (8 * DK_NUM)][i / DK_NUM % 8][i % DK_NUM] = (float)json_number_value(json_array_get(kj, i));
            kitPat = curPat();
        } else fillKits();
        if (json_t* uk = json_object_get(root, "userKits"))
            for (int i = 0; i < kNumUserKit; i++) {
                json_t* o = json_array_get(uk, i);
                json_t* md = o ? json_object_get(o, "models") : NULL;
                json_t* vl = o ? json_object_get(o, "values") : NULL;
                const char* cs = o ? json_string_value(json_object_get(o, "choke")) : NULL;
                UserKit& u = userKit[i];
                u.used = md && vl && json_array_size(md) == 8 && json_array_size(vl) == (size_t)(8 * DK_NUM);
                if (!u.used) continue;
                for (int r = 0; r < 8; r++) {
                    u.model[r] = clamp((int)json_integer_value(json_array_get(md, r)), 0, DM_NUM - 1);
                    for (int b = 0; b < DK_NUM; b++) u.val[r][b] = clamp((float)json_number_value(json_array_get(vl, r * DK_NUM + b)), 0.f, 1.f);
                    u.choke[r] = cs && strlen(cs) >= 8 ? clamp(cs[r] - '0', 0, 4) : 0;
                }
            }
        if (json_t* j = json_object_get(root, "kitLoaded")) kitLoaded = clamp((int)json_integer_value(j), -1, kNumFactoryKit + kNumUserKit - 1);
        if (json_t* arr = json_object_get(root, "patterns"))
            for (int p = 0; p < 8; p++) {
                const char* s = json_string_value(json_array_get(arr, p));
                if (s && strlen(s) >= 64)
                    for (int i = 0; i < 64; i++) pat[p][i / 8][i % 8] = (s[i] == '1' || s[i] == '2') ? (uint8_t)(s[i] - '0') : 0;
            }
        const char* ms = json_string_value(json_object_get(root, "mute"));
        const char* ss = json_string_value(json_object_get(root, "solo"));
        const char* cs = json_string_value(json_object_get(root, "choke"));
        for (int i = 0; i < 8; i++) {
            mute[i] = ms && strlen(ms) >= 8 && ms[i] == '1';
            solo[i] = ss && strlen(ss) >= 8 && ss[i] == '1';
            if (cs && strlen(cs) >= 8) choke[i] = clamp(cs[i] - '0', 0, 4);
        }
        if (json_t* j = json_object_get(root, "leaveMix")) leaveMix = json_boolean_value(j);
        if (json_t* j = json_object_get(root, "showGraph")) showGraph = json_boolean_value(j);
        if (json_t* j = json_object_get(root, "matchColors")) matchColors = json_boolean_value(j);
        if (json_t* j = json_object_get(root, "rdSilent")) rdSilent = (uint8_t) json_integer_value(j);
        if (json_t* j = json_object_get(root, "in")) midiIn.fromJson(j);
        if (json_t* j = json_object_get(root, "out")) midiOut.fromJson(j);
        if (json_t* j = json_object_get(root, "fx")) fxFromJson(j);
    }
};

typedef SynTHBloKDrum DR;


// ---------------------------------------------------------------- widgets ----------------

// Pad: hit = the page's colour, accent = the same with a bright core; click = hit, right-click = accent.
struct DRPad : OpaqueWidget {
    DR* module = NULL;
    int r = 0, c = 0;
    void draw(const DrawArgs& args) override {
        int s = module ? module->padState(r, c) : ((r == 0 && c % 4 == 0) ? 2 : (r == 3 && c % 2 == 0) || (r == 1 && c == 4) ? 1 : 0);
        bool playing = module && module->step >= 0 && module->step == c;
        int p = module ? module->curPat() : 0;
        NVGcolor col = kPatColor[p], onCol = kPatOnColor[p];
        if (module && !module->trackOn(r)) {
            col = nvgLerpRGBA(kIdle, col, 0.35f);
            onCol = nvgLerpRGBA(kIdle, onCol, 0.35f);
        }
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        NVGcolor fill = s ? onCol : kIdle;
        if (playing) fill = s ? nvgLerpRGBA(onCol, nvgRGB(0xff, 0xff, 0xff), 0.55f) : nvgLerpRGBA(kIdle, col, 0.45f);
        nvgFillColor(args.vg, fill);
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, col);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
        if (s == 2) {
            float m = box.size.x * 0.3f;
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, m, m, box.size.x - 2.f * m, box.size.y - 2.f * m, 0.8f);
            nvgFillColor(args.vg, nvgLerpRGBA(onCol, nvgRGB(0xff, 0xf4, 0xd0), 0.75f));
            nvgFill(args.vg);
        }
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS) return;
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) { if (module) module->togglePad(r, c); e.consume(this); }
        else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) { if (module) module->toggleAccent(r, c); e.consume(this); }
    }
};

struct DRPatBtn : OpaqueWidget {
    DR* module = NULL;
    int p = 0;
    void draw(const DrawArgs& args) override {
        drawColorButton(args, box.size, p, module ? module->curPat() == p : p == 0, (char)('1' + p), 11.f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[DR::PAT_PARAM].setValue((float)(p + 1));
            e.consume(this);
        }
    }
};

// Level strip (as MATRIX-LOOP's): voice idx's LEVEL, in its column's colour (faded while muted).
struct DRStrip : OpaqueWidget {
    DR* module = NULL;
    int idx = 0;
    float get() { return module ? module->stripGet(idx) : kKitVal[idx][DK_LEVEL]; }
    void set(float v) { if (module) module->stripSet(idx, v); }
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        nvgFillColor(args.vg, nvgRGB(0x14, 0x14, 0x14));
        nvgFill(args.vg);
        NVGcolor col = kPatOnColor[idx];
        float v = get(), h = (box.size.y - 3.f) * v;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 1.5f, box.size.y - 1.5f - h, box.size.x - 3.f, h, 1.f);
        nvgFillColor(args.vg, nvgLerpRGBA(kIdle, col, module && module->mute[idx] ? 0.35f : 0.8f));
        nvgFill(args.vg);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.6f);
        nvgStrokeColor(args.vg, nvgLerpRGBA(kIdle, col, 0.45f));
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
    }
    // the voice's out level (as MATRIX-LOOP's strips): a translucent wash rising from the bottom, -48 .. +6 dB
    // (0 dB = 5 V), faint at the bottom, strongest at the level; red past +4 dB
    void drawLayer(const DrawArgs& args, int layer) override {
        OpaqueWidget::drawLayer(args, layer);
        if (layer != 1) return;
        float pk = module ? module->voicePk[idx] : 0.2f + 0.08f * idx;
        float n = clamp((20.f * std::log10(pk + 1e-6f) + 48.f) / 54.f, 0.f, 1.f);
        if (n <= 0.f) return;
        float x = 1.5f, y = 1.5f, w = box.size.x - 3.f, hh = box.size.y - 3.f;
        float lh = hh * n, top = y + hh - lh;
        NVGcolor c = pk > 1.6f ? nvgRGB(0xff, 0x32, 0x28) : nvgLerpRGBA(kPatOnColor[idx], nvgRGB(0xff, 0xff, 0xff), 0.3f);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, x, top, w, lh, std::min(1.f, w * 0.5f));
        nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.f, y + hh, 0.f, top, nvgTransRGBA(c, 30), nvgTransRGBA(c, 160)));
        nvgFill(args.vg);
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

// MUTE (as MATRIX-LOOP's), one over each level strip: mute voice b (lit: muted).
struct DRMuteSq : OpaqueWidget {
    DR* module = NULL;
    int b = 0;
    void draw(const DrawArgs& args) override {
        bool on = module && module->mute[b];
        drawColorButton(args, box.size, b, on, 0, 9.f);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? nvgRGB(0x10, 0x10, 0x10) : kPatColor[b]);
        nvgFontSize(args.vg, 5.5f);
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, "MUTE", NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->mute[b] = !module->mute[b];
            e.consume(this);
        }
    }
};

// Value arc around a knob (0..1 of the knob's range): amber, or its strip bank's colour when bank >= 0.
struct DRRing : TransparentWidget {
    Module* module = NULL;
    int param = 0;
    int bank = -1;
    float width = 1.8f;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float v = module ? module->paramQuantities[param]->getScaledValue() : 0.5f;
        float cx = box.size.x * 0.5f, cy = box.size.y * 0.5f, rr = box.size.x * 0.5f - width * 0.8f;
        float a0 = M_PI * 0.75f, a1 = M_PI * 2.25f;
        NVGcolor track = nvgRGBA(255, 180, 90, 40), arc = nvgRGBA(255, 112, 15, 235);
        if (bank >= 0) {
            track = nvgTransRGBA(kPatOnColor[bank], 40);
            arc = nvgTransRGBA(kPatOnColor[bank], 235);
        }
        nvgBeginPath(args.vg);
        nvgArc(args.vg, cx, cy, rr, a0, a1, NVG_CW);
        nvgStrokeColor(args.vg, track);
        nvgStrokeWidth(args.vg, width);
        nvgStroke(args.vg);
        if (v > 0.001f) {
            nvgBeginPath(args.vg);
            nvgArc(args.vg, cx, cy, rr, a0, a0 + (a1 - a0) * v, NVG_CW);
            nvgStrokeColor(args.vg, arc);
            nvgStrokeWidth(args.vg, width);
            nvgStroke(args.vg);
        }
    }
};

struct DRTitle : TransparentWidget {
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
        nvgText(args.vg, 0.f, mm2px(6.4f), "MTX-DRUM", NULL);
    }
};

// Voice section geometry (mm): the MODEL cells, 8 macro knob columns, the voice outs; rows centred on the pads.
static const float kVsX = 79.1f, kVsW = 99.f, kMdlW = 14.f, kVsOutX = 173.f;
static const float kMuteW = 4.6f, kMuteIn = 0.7f;   // the MUTE square inside the MODEL cell's right end
static float vsColX(int b) { return 98.6f + 9.4f * b; }
static float vsRowY(int r) { return 22.8f + 7.4f * r; }

// Column heads (an OLED strip over the rows): MODEL, the 8 macros (each in its knob rings' colour), OUT. Labels only.
struct DRColHead : OpaqueWidget {
    DR* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 6.f);
        nvgFillColor(args.vg, oled::dim());
        nvgText(args.vg, mm2px(kMdlW * 0.5f), H * 0.5f, "MODEL", NULL);
        nvgText(args.vg, mm2px(kVsOutX - kVsX), H * 0.5f, "OUT", NULL);
        for (int b = 0; b < DK_NUM; b++) {
            nvgFillColor(args.vg, kPatOnColor[b]);
            nvgText(args.vg, mm2px(vsColX(b) - kVsX), H * 0.5f, kDrBankName[b], NULL);
        }
    }
};

// The row's MODEL cell: the model's name, lit while it is the selected voice; its MUTE square sits on top. Click = select it,
// scroll = step its model, right-click = model, mute / solo and choke group menu.
struct DRModelCell : OpaqueWidget {
    DR* module = NULL;
    int r = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        bool on = module ? module->sel() == r : r == 0;
        bool off = module && !module->trackOn(r);
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.2f);
        nvgFillColor(args.vg, on ? oled::litFill() : nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, oled::outline(on ? 255 : 110));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        const float nameW = W - mm2px(kMuteW + kMuteIn);   // left of the MUTE square
        nvgFontSize(args.vg, 7.f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? oled::ink() : off ? oled::dim() : oled::amber());
        int m = module ? module->model(r) : kKitModel[r];
        nvgText(args.vg, nameW * 0.5f, H * 0.5f, kDrumModelName[m], NULL);
        if (module && module->solo[r]) {   // S mark in the corner (mute has its own square)
            nvgFontSize(args.vg, 5.f);
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgText(args.vg, 1.5f, 0.5f, "S", NULL);
        }
    }
    void stepModel(int d) {
        if (!module) return;
        int id = DR::MODEL_PARAM + r;
        module->params[id].setValue((float)((module->model(r) + d + DM_NUM) % DM_NUM));
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || !module) return;
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) { module->params[DR::SEL_PARAM].setValue((float)r); e.consume(this); }
        else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
            DR* m = module; int row = r;
            ui::Menu* menu = createMenu();
            menu->addChild(createMenuLabel("Row " + std::to_string(r + 1)));
            for (int k = 0; k < DM_NUM; k++)
                menu->addChild(createCheckMenuItem(kDrumModelName[k], "", [=]() { return m->model(row) == k; },
                                                   [=]() { m->params[DR::MODEL_PARAM + row].setValue((float)k); }));
            menu->addChild(new MenuSeparator);
            menu->addChild(createBoolPtrMenuItem("Mute", "", &m->mute[row]));
            menu->addChild(createBoolPtrMenuItem("Solo", "", &m->solo[row]));
            menu->addChild(createIndexSubmenuItem("Choke group", {"None", "1", "2", "3", "4"},
                [=]() { return (size_t)m->choke[row]; }, [=](size_t i) { m->choke[row] = (int)i; }));
            e.consume(this);
        }
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (e.scrollDelta.y == 0.f) return;
        stepModel(e.scrollDelta.y > 0.f ? 1 : -1);
        e.consume(this);
    }
};

// The row's MUTE square (inside its MODEL cell), lit while the row is muted. Click = toggle.
// On the selected (lit) row it is drawn darker, with an ink outline, so it stays visible.
struct DRMuteBtn : OpaqueWidget {
    DR* module = NULL;
    int r = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        bool on = module && module->mute[r];
        bool selRow = module ? module->sel() == r : r == 0;
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.f);
        nvgFillColor(args.vg, on ? oled::litFill() : selRow ? nvgRGBA(184, 80, 10, 255) : nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, selRow ? oled::ink() : oled::outline(on ? 255 : 110));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 6.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on || selRow ? oled::ink() : oled::dim());
        nvgText(args.vg, W * 0.5f, H * 0.5f, "M", NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS) return;
        if (module) module->mute[r] = !module->mute[r];
        e.consume(this);
    }
};

// Voice OLED geometry (mm from its top), laid out like the player's info screen: the voice as a big title,
// a status line, the last hit's graph, the view cells (and zoom - + FULL), the KIT row, then the 8 pads.
// The OLED spans the MODEL cells to the last macro knob column's edge. Without the graph (the default) the KIT
// rows are taller (kVoRowC, from kVoKitC) and the pads a 2 x 4 grid under them.
static const float kVoX = kVsX, kVoY = 80.f, kVoW = vsColX(DK_NUM - 1) + 3.4f - kVsX, kVoH = 44.5f;
static const float kVoKitC = 10.4f, kVoRowC = 5.4f;
static const float kVoStat = 5.4f, kVoG0 = 9.8f, kVoG1 = 19.3f, kVoView = 19.9f, kVoKit = kVoView + oled::kCellH + 0.9f,
                   kVoUser = kVoKit + oled::kCellH + 0.9f, kVoPads = kVoUser + oled::kCellH + 0.9f, kVoPadH = kVoH - 1.f - kVoPads;
static const int kNumView = 5;
static const char* const kViewName[kNumView] = {"WAVE", "LINE", "ENV", "SPEC", "SGRAM"};
enum { VW_WAVE, VW_LINE, VW_ENV, VW_SPEC, VW_SGRAM };

// In-place radix-2 FFT; a.size() is a power of 2.
static void drFft(std::vector<std::complex<float>>& a) {
    int n = (int)a.size();
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.f * float(M_PI) / len;
        std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (int k = 0; k < len / 2; k++) {
                std::complex<float> u = a[i + k], t = a[i + k + len / 2] * w;
                a[i + k] = u + t; a[i + k + len / 2] = u - t;
                w *= wl;
            }
        }
    }
}

// The level (dB, a full-scale sine = 0) of n samples from src[at] on (zeros past `filled`), Hann windowed:
// out[b] for each bin up to n / 2.
static void drSpectrum(const float* src, int filled, int at, int n, std::vector<float>& out) {
    std::vector<std::complex<float>> a(n);
    for (int i = 0; i < n; i++) {
        float w = 0.5f - 0.5f * std::cos(2.f * float(M_PI) * i / (n - 1));
        a[i] = at + i < filled ? src[at + i] * w : 0.f;
    }
    drFft(a);
    out.resize(n / 2);
    for (int b = 0; b < n / 2; b++) out[b] = 20.f * std::log10(std::abs(a[b]) * 4.f / n + 1e-9f);
}

// The highest level in [f0, f1) Hz of a spectrum of n bins over rate / 2 (the nearest bin when narrower).
static float drBand(const std::vector<float>& s, float rate, float f0, float f1) {
    int n = (int)s.size();
    float hz = rate / (2.f * n);
    int b0 = clamp((int)(f0 / hz), 1, n - 1), b1 = clamp((int)(f1 / hz), b0 + 1, n);
    float m = -200.f;
    for (int b = b0; b < b1; b++) m = std::max(m, s[b]);
    return m;
}

// The selected voice: its number and model (title), what MOD does, choke group, mute / solo, its knobs;
// then its last hit, one of 5 views: WAVE (min / max), LINE (outline), ENV (peak and RMS in dB),
// SPEC (spectrum) and SGRAM (spectrogram). PK and END mark the peak and where the hit falls 48 dB under it;
// the zoom shows the first half, quarter or eighth. Click the graph = next view (right-click back),
// scroll = zoom.
struct DRVoiceDisplay : TransparentWidget {
    DR* module = NULL;
    static const int kSgCols = 64, kSgBands = 28;
    // worked out again when the hit, how far it has filled, the view, the zoom or the width changes
    int cV = -1, cPos = -1, cView = -1, cZoom = -1, cCols = -1;
    unsigned cHit = 0;
    std::vector<float> colMin, colMax, colRms, spec;
    std::vector<uint8_t> sgram;   // level 0..7 per cell, column by column
    int dataCols = 0, peakI = 0, endI = 0;
    float peakDb = -120.f, specPeakHz = 0.f;

    float fLo() const { return 30.f; }
    float fHi(float rate) const { return std::min(20000.f, rate * 0.5f); }
    float logF(float rate, float t) const { return fLo() * std::pow(fHi(rate) / fLo(), t); }

    // the preview's hit (the module browser): a pitched-down sine with a click
    static const float* previewHit() {
        static std::vector<float> p;
        if (p.empty()) {
            p.resize(DR::kRawN);
            float ph = 0.f;
            for (int i = 0; i < DR::kRawN; i++) {
                float t = i / 48000.f;
                ph += 2.f * float(M_PI) * 55.f * (1.f + 3.f * std::exp(-t * 40.f)) / 48000.f;
                p[i] = 0.9f * std::sin(ph) * std::exp(-t * 9.f) + 0.3f * std::exp(-t * 400.f) * (random::uniform() * 2.f - 1.f);
            }
        }
        return p.data();
    }
    int view() { return module ? clamp((int)module->params[DR::VIEW_PARAM].getValue(), 0, kNumView - 1) : 0; }
    int zoom() { return module ? clamp((int)module->params[DR::ZOOM_PARAM].getValue(), 0, 3) : 0; }

    void analyse(const float* src, int filled, float rate, int vw, int z, int cols) {
        const int N = DR::kRawN, win = N >> z;
        // the peak and the END point (48 dB under it) over everything recorded
        float pk = 0.f;
        peakI = 0;
        for (int i = 0; i < filled; i++) if (std::fabs(src[i]) > pk) { pk = std::fabs(src[i]); peakI = i; }
        peakDb = 20.f * std::log10(pk + 1e-9f);
        endI = 0;
        for (int i = filled - 1; i >= 0; i--) if (std::fabs(src[i]) > pk * 0.004f) { endI = i; break; }
        // per pixel column of the window: min, max, RMS
        colMin.assign(cols, 0.f); colMax.assign(cols, 0.f); colRms.assign(cols, 0.f);
        dataCols = 0;
        for (int c = 0; c < cols; c++) {
            int i0 = (int)((int64_t)c * win / cols), i1 = std::max(i0 + 1, (int)((int64_t)(c + 1) * win / cols));
            if (i0 >= filled) break;
            i1 = std::min(i1, filled);
            float lo = 0.f, hi = 0.f, sq = 0.f;
            for (int i = i0; i < i1; i++) { lo = std::min(lo, src[i]); hi = std::max(hi, src[i]); sq += src[i] * src[i]; }
            colMin[c] = lo; colMax[c] = hi; colRms[c] = std::sqrt(sq / (i1 - i0));
            dataCols = c + 1;
        }
        if (vw == VW_SPEC) {   // the window's start, up to 4096 samples
            int n = 256;
            while (n < 4096 && n < win) n <<= 1;
            std::vector<float> s;
            drSpectrum(src, filled, 0, n, s);
            spec.assign(cols, -200.f);
            float best = -200.f;
            for (int c = 0; c < cols; c++) {
                float f0 = logF(rate, (float)c / cols), f1 = logF(rate, (float)(c + 1) / cols);
                spec[c] = drBand(s, rate, f0, f1);
                if (spec[c] > best) { best = spec[c]; specPeakHz = std::sqrt(f0 * f1); }
            }
        } else if (vw == VW_SGRAM) {
            int n = 128;
            while (n < 512 && n < win / 16) n <<= 1;
            sgram.assign(kSgCols * kSgBands, 0);
            std::vector<float> s;
            for (int k = 0; k < kSgCols; k++) {
                int at = std::max(0, (int)((k + 0.5f) * win / kSgCols) - n / 2);
                if (at >= filled) break;
                drSpectrum(src, filled, at, n, s);
                for (int b = 0; b < kSgBands; b++) {
                    float db = drBand(s, rate, logF(rate, (float)b / kSgBands), logF(rate, (float)(b + 1) / kSgBands));
                    sgram[k * kSgBands + b] = (uint8_t)clamp((int)((db + 72.f) / 72.f * 8.f), 0, 7);
                }
            }
        }
    }

    static void label(const DrawArgs& args, float x, float y, const char* s, float size, NVGcolor c, int align) {
        nvgFontSize(args.vg, size);
        nvgFillColor(args.vg, c);
        nvgTextAlign(args.vg, align);
        nvgText(args.vg, x, y, s, NULL);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        bool g = module && module->showGraph;   // the hit graph is on (module menu)
        const float gx0 = 4.f, gx1 = W - 4.f, gy0 = mm2px(kVoG0), gy1 = mm2px(kVoG1);
        const float yEnd = g ? kVoUser + oled::kCellH + 0.45f : kVoKitC + 2.f * kVoRowC + 1.35f;   // under the user kits
        nvgBeginPath(args.vg);
        oled::divider(args, 4.f, gy0 - 1.f, W - 4.f, gy0 - 1.f);
        oled::divider(args, 4.f, mm2px(yEnd), W - 4.f, mm2px(yEnd));
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W, H);

        int v = module ? module->sel() : 0, m = module ? module->model(v) : kKitModel[0];
        int vw = view(), z = zoom(), cols = std::max(8, (int)(gx1 - gx0));
        const float* src = module ? module->raw[v] : previewHit();
        int filled = module ? clamp(module->rawPos[v], 0, (int)DR::kRawN) : DR::kRawN;
        float rate = module ? module->rawRate : 48000.f;
        unsigned hid = module ? module->hitId[v] : 0;
        if (g && (v != cV || hid != cHit || filled != cPos || vw != cView || z != cZoom || cols != cCols)) {
            analyse(src, filled, rate, vw, z, cols);
            cV = v; cHit = hid; cPos = filled; cView = vw; cZoom = z; cCols = cols;
        }
        bool any = filled > 0 && (module ? hid > 0 : true);
        int win = DR::kRawN >> z;
        float winMs = 1000.f * win / rate;

        // title: the voice and its model, big; right: the kit (graph on: the hit's peak and length, SPEC: its
        // strongest band)
        char b[64];
        snprintf(b, sizeof b, "%d  %s", v + 1, kDrumModelName[m]);
        label(args, 4.f, 2.5f, b, 11.f, oled::amber(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        int kl = module ? module->kitLoaded : 0;
        if (!g) {
            if (kl < 0) snprintf(b, sizeof b, "KIT --");
            else if (kl < kNumFactoryKit) snprintf(b, sizeof b, "KIT %s", kFactoryKit[kl].name);
            else snprintf(b, sizeof b, "KIT U%d", kl - kNumFactoryKit + 1);
        }
        else if (!any) snprintf(b, sizeof b, "PRESS A PAD");
        else if (vw == VW_SPEC) snprintf(b, sizeof b, specPeakHz < 1000.f ? "PK %.0f HZ   %.1f DB" : "PK %.1f KHZ   %.1f DB",
                                         specPeakHz < 1000.f ? specPeakHz : specPeakHz / 1000.f, peakDb);
        else snprintf(b, sizeof b, "PEAK %.1f DB   LEN %s%.0f MS", peakDb, endI >= DR::kRawN - 64 ? ">" : "", 1000.f * endI / rate);
        label(args, W - 4.f, 3.f, b, 8.f, g ? oled::dim() : oled::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
        // status line: MOD's job, choke group, mute / solo; right: the knobs
        int ck = module ? module->choke[v] : 0;
        snprintf(b, sizeof b, "MOD %s   CHOKE %s%s", kDrumModName[m], ck ? std::to_string(ck).c_str() : "-",
                 module && module->solo[v] ? "   SOLO" : module && module->mute[v] ? "   MUTE" : "");
        const float ys = mm2px(kVoStat + (kVoG0 - kVoStat) * 0.5f) - 0.5f;
        label(args, 4.f, ys, b, 6.5f, oled::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        if (module) {
            auto kv = [&](int k) { return (int)(module->params[module->vp(v, k)].getValue() * 99.f); };
            snprintf(b, sizeof b, "T%02d D%02d C%02d S%02d M%02d", kv(DK_TUNE), kv(DK_DECAY), kv(DK_TONE), kv(DK_SNAP), kv(DK_MOD));
            label(args, W - 4.f, ys, b, 6.5f, oled::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
        if (!g) { nvgResetScissor(args.vg); return; }

        // the graph
        nvgSave(args.vg);
        nvgIntersectScissor(args.vg, gx0, gy0, gx1 - gx0, gy1 - gy0);
        float gw = gx1 - gx0, gh = gy1 - gy0, yc = (gy0 + gy1) * 0.5f, half = gh * 0.5f - 1.f;
        float cw = gw / cols;
        auto dbY = [&](float db, float floorDb) { return gy1 - clamp((db - floorDb) / -floorDb, 0.f, 1.f) * (gh - 1.f); };
        auto gridLine = [&](float x0, float y0, float x1, float y1) { nvgMoveTo(args.vg, x0, y0); nvgLineTo(args.vg, x1, y1); };
        if (vw == VW_WAVE || vw == VW_LINE) {
            nvgBeginPath(args.vg); gridLine(gx0, yc, gx1, yc); oled::dividerStroke(args);
            if (vw == VW_WAVE) {
                nvgBeginPath(args.vg);
                for (int c = 0; c < dataCols; c++) {
                    float hi = clamp(colMax[c], 0.f, 1.f), lo = clamp(colMin[c], -1.f, 0.f);
                    nvgRect(args.vg, gx0 + c * cw, yc - hi * half, std::max(0.6f, cw), (hi - lo) * half + 0.3f);
                }
                nvgFillColor(args.vg, oled::amber(200));
                nvgFill(args.vg);
            } else if (dataCols > 1) {
                for (int side = 0; side < 2; side++) {
                    nvgBeginPath(args.vg);
                    for (int c = 0; c < dataCols; c++) {
                        float y = yc - clamp(side ? colMin[c] : colMax[c], -1.f, 1.f) * half;
                        if (c == 0) nvgMoveTo(args.vg, gx0, y); else nvgLineTo(args.vg, gx0 + (c + 0.5f) * cw, y);
                    }
                    nvgStrokeColor(args.vg, oled::amber(side ? 140 : 230));
                    nvgStrokeWidth(args.vg, 0.9f);
                    nvgStroke(args.vg);
                }
            }
        } else if (vw == VW_ENV) {   // peak (filled) and RMS (the line), 0 .. -60 dB
            nvgBeginPath(args.vg);
            for (int d = 12; d < 60; d += 12) gridLine(gx0, dbY(-d, -60.f), gx1, dbY(-d, -60.f));
            oled::dividerStroke(args);
            if (dataCols > 1) {
                nvgBeginPath(args.vg);
                nvgMoveTo(args.vg, gx0, gy1);
                for (int c = 0; c < dataCols; c++)
                    nvgLineTo(args.vg, gx0 + (c + 0.5f) * cw, dbY(20.f * std::log10(std::max(-colMin[c], colMax[c]) + 1e-9f), -60.f));
                nvgLineTo(args.vg, gx0 + dataCols * cw, gy1);
                nvgClosePath(args.vg);
                nvgFillColor(args.vg, oled::amber(90));
                nvgFill(args.vg);
                nvgBeginPath(args.vg);
                for (int c = 0; c < dataCols; c++) {
                    float y = dbY(20.f * std::log10(colRms[c] + 1e-9f), -60.f);
                    if (c == 0) nvgMoveTo(args.vg, gx0, y); else nvgLineTo(args.vg, gx0 + (c + 0.5f) * cw, y);
                }
                nvgStrokeColor(args.vg, oled::amber(240));
                nvgStrokeWidth(args.vg, 1.f);
                nvgStroke(args.vg);
            }
            label(args, gx0 + 1.f, dbY(-24.f, -60.f) - 0.5f, "-24", 5.5f, oled::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
            label(args, gx0 + 1.f, dbY(-48.f, -60.f) - 0.5f, "-48", 5.5f, oled::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
            label(args, gx1 - 1.f, gy1 - 1.f, "PEAK / RMS  DB", 5.5f, oled::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
        } else {   // SPEC and SGRAM share the log frequency axis: SPEC across, SGRAM up
            bool sp = vw == VW_SPEC;
            const float marks[3] = {100.f, 1000.f, 10000.f};
            const char* markName[3] = {"100", "1K", "10K"};
            auto fPos = [&](float f) { return std::log(f / fLo()) / std::log(fHi(rate) / fLo()); };
            nvgBeginPath(args.vg);
            for (float f : marks) {
                if (sp) gridLine(gx0 + fPos(f) * gw, gy0, gx0 + fPos(f) * gw, gy1);
                else gridLine(gx0, gy1 - fPos(f) * gh, gx1, gy1 - fPos(f) * gh);
            }
            oled::dividerStroke(args);
            if (sp && any) {
                nvgBeginPath(args.vg);
                nvgMoveTo(args.vg, gx0, gy1);
                for (int c = 0; c < cols; c++) nvgLineTo(args.vg, gx0 + (c + 0.5f) * cw, dbY(spec[c], -84.f));
                nvgLineTo(args.vg, gx1, gy1);
                nvgClosePath(args.vg);
                nvgFillColor(args.vg, oled::amber(110));
                nvgFill(args.vg);
                nvgBeginPath(args.vg);
                for (int c = 0; c < cols; c++) {
                    float y = dbY(spec[c], -84.f);
                    if (c == 0) nvgMoveTo(args.vg, gx0, y); else nvgLineTo(args.vg, gx0 + (c + 0.5f) * cw, y);
                }
                nvgStrokeColor(args.vg, oled::amber(240));
                nvgStrokeWidth(args.vg, 0.9f);
                nvgStroke(args.vg);
            } else if (!sp && any) {   // 7 brightness steps, each drawn as one path
                float sw = gw / kSgCols, sh = gh / kSgBands;
                for (int lv = 1; lv < 8; lv++) {
                    nvgBeginPath(args.vg);
                    for (int k = 0; k < kSgCols; k++)
                        for (int bd = 0; bd < kSgBands; bd++)
                            if (sgram[k * kSgBands + bd] == lv) nvgRect(args.vg, gx0 + k * sw, gy1 - (bd + 1) * sh, sw + 0.3f, sh + 0.3f);
                    nvgFillColor(args.vg, lv == 7 ? oled::litFill() : oled::amber(25 + 30 * lv));
                    nvgFill(args.vg);
                }
            }
            for (int i = 0; i < 3; i++) {
                if (sp) label(args, gx0 + fPos(marks[i]) * gw + 1.f, gy1 - 1.f, markName[i], 5.5f, oled::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
                else label(args, gx0 + 1.f, gy1 - fPos(marks[i]) * gh - 0.5f, markName[i], 5.5f, oled::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
            }
        }
        // PK and END markers on the time views; past END is dimmed (as outside a loop on the player)
        if (vw != VW_SPEC && any) {
            float xp = gx0 + gw * peakI / win, xe = gx0 + gw * endI / win;
            if (xe < gx1) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, xe, gy0, gx1 - xe, gh);
                nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 120));
                nvgFill(args.vg);
            }
            nvgStrokeWidth(args.vg, 1.2f);
            nvgStrokeColor(args.vg, nvgRGBA(255, 120, 20, 230));
            for (float x : {xp, xe}) {
                if (x < gx0 || x > gx1) continue;
                nvgBeginPath(args.vg); nvgMoveTo(args.vg, x, gy0); nvgLineTo(args.vg, x, gy1); nvgStroke(args.vg);
            }
            if (xp <= gx1 - 10.f) label(args, xp + 1.5f, gy0 + 1.f, "PK", 5.5f, nvgRGBA(255, 170, 70, 230), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            if (xe <= gx1 - 14.f && xe - xp > 10.f) label(args, xe + 1.5f, gy0 + 1.f, "END", 5.5f, nvgRGBA(255, 170, 70, 230), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        }
        // the window, top right (as the player's zoom readout)
        if (vw == VW_SPEC) snprintf(b, sizeof b, "FIRST %.0f MS", 1000.f * std::min(4096, win) / rate);
        else snprintf(b, sizeof b, "%.0f MS", winMs);
        label(args, gx1 - 1.f, gy0 + 1.f, b, 6.f, oled::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
        nvgRestore(args.vg);
        nvgResetScissor(args.vg);
    }

    bool inGraph(Vec p) { return module && module->showGraph && p.y >= mm2px(kVoG0) && p.y < mm2px(kVoG1); }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || !inGraph(e.pos) || !module) return;
        if (e.button != GLFW_MOUSE_BUTTON_LEFT && e.button != GLFW_MOUSE_BUTTON_RIGHT) return;
        int d = e.button == GLFW_MOUSE_BUTTON_LEFT ? 1 : -1;
        module->params[DR::VIEW_PARAM].setValue((float)((view() + d + kNumView) % kNumView));
        e.consume(this);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (e.scrollDelta.y == 0.f || !inGraph(e.pos) || !module) return;
        module->params[DR::ZOOM_PARAM].setValue((float)clamp(zoom() + (e.scrollDelta.y > 0.f ? 1 : -1), 0, 3));
        e.consume(this);
    }
};

// Zoom - / + on the voice OLED (FULL is a plain oled::Cell).
struct DRZoomStep : OpaqueWidget {
    DR* module = NULL;
    int dir = 1;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        int z = module ? clamp((int)module->params[DR::ZOOM_PARAM].getValue(), 0, 3) : 0;
        bool can = dir > 0 ? z < 3 : z > 0;
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.2f);
        nvgFillColor(args.vg, oled::offFill());
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, oled::outline(can ? 150 : 60));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 8.f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, can ? oled::amber() : oled::dim());
        nvgText(args.vg, W * 0.5f, H * 0.5f, dir > 0 ? "+" : "-", NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS) return;
        if (module) {
            Param& p = module->params[DR::ZOOM_PARAM];
            p.setValue((float)clamp((int)p.getValue() + dir, 0, 3));
        }
        e.consume(this);
    }
};

// A pad on the voice OLED: press = play its row now (PAD_PARAM, momentary, so it can be MIDI-mapped);
// it flashes whenever its voice plays, from anywhere. Number, model name, M / S when muted / soloed;
// the selected voice has the bright outline.
struct DRPadCell : oled::Cell {
    int r = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        app::ParamWidget::drawLayer(args, layer);
        if (layer != 1) return;
        DR* m = dynamic_cast<DR*>(module);
        float fl = m ? clamp(m->padFlash[r] / 0.15f, 0.f, 1.f) : (r == 0 ? 1.f : 0.f);
        bool sel = m ? m->sel() == r : r == 0;
        bool off = m && !m->trackOn(r);
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.5f);
        nvgFillColor(args.vg, nvgLerpRGBA(off ? nvgRGBA(40, 12, 6, 255) : oled::offFill(), oled::litFill(), fl));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, off ? nvgRGBA(235, 50, 30, sel ? 255 : 150) : oled::outline(sel ? 255 : 100));
        nvgStrokeWidth(args.vg, sel ? 1.1f : 0.7f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        NVGcolor ink = fl > 0.5f ? oled::ink() : off ? nvgRGBA(255, 90, 70, 255) : oled::amber();
        char num[4]; snprintf(num, sizeof num, "%02d", r + 1);
        bool big = H > mm2px(9.f);
        nvgFontSize(args.vg, big ? 10.f : 9.f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFillColor(args.vg, ink);
        nvgText(args.vg, 3.f, 3.f, num, NULL);
        if (m && (m->mute[r] || m->solo[r])) {
            nvgFontSize(args.vg, 7.f);
            nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
            nvgText(args.vg, W - 3.f, 3.5f, m->solo[r] ? "S" : "M", NULL);
        }
        nvgFontSize(args.vg, big ? 7.5f : 6.f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
        nvgFillColor(args.vg, fl > 0.5f ? oled::ink() : nvgRGBA(255, 190, 90, off ? 90 : 150));
        nvgText(args.vg, 3.f, H - 2.5f, kDrumModelName[m ? m->model(r) : kKitModel[r]], NULL);
    }
};

// A KIT row cell on the voice OLED. k < kNumFactoryKit: a factory kit; then the user kits U1..U8
// (dim while empty); k == -1: STORE (blinks while armed: the next user kit pressed stores the rows there).
// The last kit loaded or stored is lit. Right-click a user kit = clear it.
struct DRKitCell : OpaqueWidget {
    DR* module = NULL;
    int k = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        bool store = k < 0, user = k >= kNumFactoryKit;
        bool armed = module && module->storeArm;
        bool blink = armed && std::fmod(glfwGetTime(), 0.5) < 0.25;
        bool empty = user && (!module || !module->userKit[k - kNumFactoryKit].used);
        bool on = store ? blink : module ? module->kitLoaded == k : k == 0;
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.2f);
        nvgFillColor(args.vg, on ? oled::litFill() : oled::offFill());
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, oled::outline(on || (armed && user) ? 255 : empty ? 50 : 110));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        char lab[8];
        if (store) snprintf(lab, sizeof lab, "STORE");
        else if (user) snprintf(lab, sizeof lab, "U%d", k - kNumFactoryKit + 1);
        else snprintf(lab, sizeof lab, "%s", kFactoryKit[k].name);
        nvgFontSize(args.vg, H > mm2px(5.f) ? 7.f : 6.f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? oled::ink() : empty && !armed ? oled::dim() : oled::amber());
        nvgText(args.vg, W * 0.5f, H * 0.5f, lab, NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || !module) return;
        bool user = k >= kNumFactoryKit;
        if (e.button == GLFW_MOUSE_BUTTON_RIGHT && user) {
            DR* m = module; int u = k - kNumFactoryKit;
            ui::Menu* menu = createMenu();
            menu->addChild(createMenuLabel("User kit U" + std::to_string(u + 1)));
            menu->addChild(createMenuItem("Store the rows here", "", [=]() { m->storeKit(u); m->storeArm = false; }));
            menu->addChild(createMenuItem("Clear", "", [=]() {
                m->userKit[u].used = false;
                if (m->kitLoaded == kNumFactoryKit + u) m->kitLoaded = -1;
            }));
            e.consume(this);
            return;
        }
        if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
        if (k < 0) module->storeArm = !module->storeArm;
        else if (user && module->storeArm) { module->storeKit(k - kNumFactoryKit); module->storeArm = false; }
        else { module->storeArm = false; module->kitReq.store(k); }
        e.consume(this);
    }
};

// The master OLED, as on MATRIX-DUO: MIDI IN, MIDI OUT (click = port menu); BPM (click / scroll steps the
// internal tempo), run, step, strip bank; VOICE | ENV trigger (click or scroll steps forward, right-click back).
// The button cells (oled::Cell) sit on its lower part. Rows in mm from the top:
static const float kMoRow = oled::kRow, kMoInfo = 2.f * kMoRow, kMoSel = 3.f * kMoRow, kMoBtn = 4.f * kMoRow + 1.f,
                   kMoBtnH = oled::kCellH, kMoBtnGap = 0.9f;
struct DRMasterDisplay : TransparentWidget {
    DR* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        nvgBeginPath(args.vg);
        for (float y : {kMoRow, kMoInfo, kMoSel, kMoSel + kMoRow}) oled::divider(args, 4.f, mm2px(y), W - 4.f, mm2px(y));
        oled::divider(args, W * 0.5f, mm2px(kMoSel) + 2.f, W * 0.5f, mm2px(kMoSel + kMoRow) - 2.f);
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgScissor(args.vg, 0, 0, W, H);
        oled::midiLine(args, module ? &module->midiIn : NULL, "IN", 0.f);
        oled::midiLine(args, module ? &module->midiOut : NULL, "OUT", mm2px(kMoRow));
        char bpm[24] = "120 BPM", info[16] = "1/8";
        bool run = module ? module->running : true;
        if (module) {
            if (!module->inputs[DR::CLOCK_INPUT].isConnected()) snprintf(bpm, sizeof bpm, "%.0f BPM", module->params[DR::BPM_PARAM].getValue());
            else if (module->clockPeriod > 0.f && module->sinceClock < 2.f) snprintf(bpm, sizeof bpm, "%.1f BPM", 60.f / (module->clockPeriod * 4.f));
            else snprintf(bpm, sizeof bpm, "--- BPM");
            snprintf(info, sizeof info, "%d/%d", module->step + 1, clamp((int)module->params[DR::LEN_PARAM].getValue(), 1, 8));
        }
        const float yi = mm2px(kMoInfo + kMoRow * 0.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 8.f);
        nvgFillColor(args.vg, oled::amber());
        nvgText(args.vg, 4.f, yi, bpm, NULL);
        nvgFontSize(args.vg, 6.5f);
        nvgFillColor(args.vg, run ? oled::green() : oled::dim());
        nvgText(args.vg, W * 0.36f, yi, run ? "RUN" : "STOP", NULL);
        nvgFillColor(args.vg, oled::dim());
        nvgText(args.vg, W * 0.52f, yi, info, NULL);
        // VOICE | ENV: a dim name, the value in amber
        int v = module ? module->sel() : 0;
        int tr = module ? clamp((int)module->params[DR::FLT_TRIG_PARAM].getValue(), 0, 5) : 0;
        char vs[16]; snprintf(vs, sizeof vs, "%d %s", v + 1, kDrumModelName[module ? module->model(v) : kKitModel[0]]);
        const char* top[2] = {"VOICE", "ENV"};
        const char* val[2] = {vs, kEnvTrigName[tr]};
        const float yk = mm2px(kMoSel + kMoRow * 0.5f);
        for (int i = 0; i < 2; i++) {
            float xa = W * 0.5f * i, xb = W * 0.5f * (i + 1);
            nvgFontSize(args.vg, 6.f);
            nvgFillColor(args.vg, oled::dim());
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, xa + 4.f, yk, top[i], NULL);
            nvgFontSize(args.vg, 7.5f);
            nvgFillColor(args.vg, oled::amber());
            nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, xb - 4.f, yk, val[i], NULL);
        }
        nvgResetScissor(args.vg);
    }
    bool inSel(float y) { return y >= mm2px(kMoSel) && y < mm2px(kMoSel + kMoRow); }
    bool inBpm(float x, float y) { return y >= mm2px(kMoInfo) && y < mm2px(kMoSel) && x < box.size.x * 0.34f; }
    void stepAt(float x, float y, int d) {
        if (!module) return;
        if (inBpm(x, y)) {
            if (!module->inputs[DR::CLOCK_INPUT].isConnected())
                module->params[DR::BPM_PARAM].setValue(clamp(std::round(module->params[DR::BPM_PARAM].getValue()) + d, 40.f, 240.f));
            return;
        }
        if (x < box.size.x * 0.5f) module->params[DR::SEL_PARAM].setValue((float)((module->sel() + d + 8) % 8));
        else module->params[DR::FLT_TRIG_PARAM].setValue((float)(((int)module->params[DR::FLT_TRIG_PARAM].getValue() + d + 6) % 6));
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
        if (!inSel(e.pos.y) && !inBpm(e.pos.x, e.pos.y)) return;
        stepAt(e.pos.x, e.pos.y, e.button == GLFW_MOUSE_BUTTON_LEFT ? 1 : -1);
        e.consume(this);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (e.scrollDelta.y == 0.f || !(inSel(e.pos.y) || inBpm(e.pos.x, e.pos.y))) return;
        stepAt(e.pos.x, e.pos.y, e.scrollDelta.y > 0.f ? 1 : -1);
        e.consume(this);
    }
};

struct SynTHBloKDrumWidget : ModuleWidget {
    // the voice OLED's cells: view + zoom (graph on only), KIT rows (factory, user, STORE), pads
    std::vector<widget::Widget*> voView, voKit;
    widget::Widget* voPad[8] = {};
    int voPlaced = -1;

    void onButton(const ButtonEvent& e) override {   // clicking the module hands it the JAM
        DR* m = getModule<DR>();
        if (m && e.action == GLFW_PRESS && m->params[DR::MIDI_ON_PARAM].getValue() > 0.5f) jamown::claim(m->id);
        ModuleWidget::onButton(e);
    }
    void step() override {
        DR* m = getModule<DR>();
        int g = m && m->showGraph;
        if (g != voPlaced) placeVoiceOled(g);
        ModuleWidget::step();
    }
    // Graph on: view cells + zoom, then the KIT rows and one row of 8 pads, all 4.4 mm cells. Graph off:
    // taller KIT rows from under the status line, and the pads a 2 x 4 grid filling the rest.
    void placeVoiceOled(bool g) {
        voPlaced = g;
        const float CX = kVoX + 2.f, CW = kVoW - 4.f, CG = 1.f;
        auto put = [&](widget::Widget* w, float x, float y, float wd, float h, bool show) {
            w->box.pos = mm2px(Vec(x, y)); w->box.size = mm2px(Vec(wd, h)); w->visible = show;
        };
        const float zw = 6.f, fw = 10.f, vwid = (CW - 7.f * CG - 2.f * zw - fw) / kNumView;
        for (int i = 0; i < kNumView; i++) put(voView[i], CX + i * (vwid + CG), kVoY + kVoView, vwid, oled::kCellH, g);
        float zx = CX + kNumView * (vwid + CG);
        for (int d = 0; d < 3; d++) put(voView[kNumView + d], zx + d * (zw + CG), kVoY + kVoView, d < 2 ? zw : fw, oled::kCellH, g);
        const float rh = g ? oled::kCellH : kVoRowC, yk = kVoY + (g ? kVoKit : kVoKitC), yu = yk + rh + 0.9f;
        const float fkw = (CW - CG * (kNumFactoryKit - 1)) / kNumFactoryKit;
        for (int k = 0; k < kNumFactoryKit; k++) put(voKit[k], CX + k * (fkw + CG), yk, fkw, rh, true);
        const float sw = 9.5f, ukw = (CW - CG * kNumUserKit - sw) / kNumUserKit;
        for (int u = 0; u <= kNumUserKit; u++)
            put(voKit[kNumFactoryKit + u], CX + u * (ukw + CG), yu, u < kNumUserKit ? ukw : sw, rh, true);
        float yp = yu + rh + 0.9f;
        if (g) {
            float pw = (CW - CG * 7.f) / 8.f;
            for (int r = 0; r < 8; r++) put(voPad[r], CX + r * (pw + CG), yp, pw, kVoPadH, true);
        } else {
            float pw = (CW - CG * 3.f) / 4.f, ph = (kVoY + kVoH - 1.f - yp - CG) / 2.f;
            for (int r = 0; r < 8; r++) put(voPad[r], CX + (r % 4) * (pw + CG), yp + (r / 4) * (ph + CG), pw, ph, true);
        }
    }
    SynTHBloKDrumWidget(DR* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKDrum.svg")));
        addChild(songlink::RowBadge::create(box.size, module));   // the Composer row conducting it
        using namespace rack::componentlibrary;
        const float PW = 335.28f;   // 66 HP
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(PW - 3.5f, 3.5f))));
        // bottom screws (Nov 2026: every module has four, like the hardware)
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(3.5f, 125.f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(PW - 3.5f, 125.f))));
        DRTitle* title = createWidget<DRTitle>(mm2px(Vec(8.5f, 0.f)));
        title->box.size = mm2px(Vec(60.f, 7.f));
        addChild(title);

        auto label = [&](float cx, float cy, const char* text) {
            JLabel* l = createWidget<JLabel>(mm2px(Vec(cx - 12.f, cy - 2.f)));
            l->box.size = mm2px(Vec(24.f, 4.f)); l->text = text;
            addChild(l);
        };
        auto smallKnob = [&](float x, float y, int param, const char* name) {
            addChild(KnobTicks::create(Vec(x, y)));
            DRRing* r = createWidget<DRRing>(mm2px(Vec(x - 5.5f, y - 5.5f)));
            r->box.size = mm2px(Vec(11.f, 11.f)); r->module = module; r->param = param;
            addChild(r);
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y)), module, param));
            label(x, y + 7.f, name);
        };
        auto cell = [&](float x, float y, float w, float h, int param, const char* lab) -> oled::Cell* {
            oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(x, y)), module, param);
            c->box.size = mm2px(Vec(w, h)); c->module = module; c->label = lab;
            addParam(c);
            return c;
        };

        // ---------------- left: the JAM matrix, laid out like MATRIX FORGE ----------------
        auto colX = [](int c) { return 4.9f + 7.4f * c; };
        auto pageLed = [=](JackLed* l) { l->color = [=]() { return kPatOnColor[module ? module->curPat() : 0]; }; return l; };
        auto stripLed = [=](JackLed* l, int i) { l->color = [=]() { return kPatOnColor[i]; }; return l; };
        for (int p = 0; p < 8; p++) {
            DRPatBtn* pb = createWidget<DRPatBtn>(mm2px(Vec(colX(p), 10.5f)));
            pb->box.size = mm2px(Vec(6.6f, 6.6f)); pb->module = module; pb->p = p;
            addChild(pb);
        }
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                DRPad* p = createWidget<DRPad>(mm2px(Vec(colX(c), 19.5f + 7.4f * r)));
                p->box.size = mm2px(Vec(6.6f, 6.6f)); p->module = module; p->r = r; p->c = c;
                addChild(p);
            }
        addParam(JRandBtn::create(69.6f, 10.5f, module, DR::RAND_PARAM));   // after pattern 8, over the TRIG jacks
        for (int r = 0; r < 8; r++) {
            addOutput(createOutputCentered<SmallJack>(mm2px(Vec(69.6f, vsRowY(r))), module, DR::TRIG_OUTPUT + r));
            addChild(pageLed(JackLed::create(Vec(73.5f, vsRowY(r)), module, DR::TRIG_OUTPUT + r, true)));
        }
        for (int i = 0; i < 8; i++) {
            addOutput(createOutputCentered<SmallJack>(mm2px(Vec(colX(i) + 3.3f, 81.6f)), module, DR::STRIP_OUTPUT + i));
            addChild(stripLed(JackLed::create(Vec(colX(i) + 7.f, 81.6f), module, DR::STRIP_OUTPUT + i, true), i));
            DRMuteSq* q = createWidget<DRMuteSq>(mm2px(Vec(colX(i), 85.5f)));
            q->box.size = mm2px(Vec(6.6f, 6.6f)); q->module = module; q->b = i;
            addChild(q);
            DRStrip* s = createWidget<DRStrip>(mm2px(Vec(colX(i) + 0.9f, 93.5f)));
            s->box.size = mm2px(Vec(4.8f, 30.f)); s->module = module; s->idx = i;
            addChild(s);
        }
        label(69.6f, 81.6f, "LOOP");
        JLoopArrow<DR>* up = createWidget<JLoopArrow<DR>>(mm2px(Vec(66.1f, 85.5f)));
        up->box.size = mm2px(Vec(7.f, 6.6f)); up->module = module; up->dir = 1;
        addChild(up);
        JLoopDisplay<DR>* ld = createWidget<JLoopDisplay<DR>>(mm2px(Vec(66.1f, 93.5f)));
        ld->box.size = mm2px(Vec(7.f, 22.5f)); ld->module = module;
        addChild(ld);
        JLoopArrow<DR>* dn = createWidget<JLoopArrow<DR>>(mm2px(Vec(66.1f, 117.5f)));
        dn->box.size = mm2px(Vec(7.f, 6.f)); dn->module = module; dn->dir = -1;
        addChild(dn);

        // ---------------- middle: the 8 drum voices, one per pad row ----------------
        DRColHead* head = createWidget<DRColHead>(mm2px(Vec(kVsX, 10.5f)));
        head->box.size = mm2px(Vec(kVsW, 6.6f)); head->module = module;
        addChild(head);
        for (int r = 0; r < 8; r++) {
            float y = vsRowY(r);
            DRModelCell* mc = createWidget<DRModelCell>(mm2px(Vec(kVsX, y - 3.f)));
            mc->box.size = mm2px(Vec(kMdlW, 6.f)); mc->module = module; mc->r = r;
            addChild(mc);
            DRMuteBtn* mb = createWidget<DRMuteBtn>(mm2px(Vec(kVsX + kMdlW - kMuteIn - kMuteW, y - kMuteW * 0.5f)));
            mb->box.size = mm2px(Vec(kMuteW, kMuteW)); mb->module = module; mb->r = r;
            addChild(mb);
            // macro knobs: trimpots with a value arc (no ticks: the rows sit 7.4 mm apart)
            for (int b = 0; b < DK_NUM; b++) {
                float x = vsColX(b);
                DRRing* ring = createWidget<DRRing>(mm2px(Vec(x - 3.4f, y - 3.4f)));
                ring->box.size = mm2px(Vec(6.8f, 6.8f)); ring->module = module; ring->param = DR::VOICE_PARAM + r * DK_NUM + b;
                ring->width = 1.3f; ring->bank = b;
                addChild(ring);
                addParam(createParamCentered<Trimpot>(mm2px(Vec(x, y)), module, DR::VOICE_PARAM + r * DK_NUM + b));
            }
            addOutput(createOutputCentered<SmallJack>(mm2px(Vec(kVsOutX, y)), module, DR::VOICE_OUTPUT + r));
            addChild(JackLed::create(Vec(kVsOutX + 3.9f, y), module, DR::VOICE_OUTPUT + r, true));
        }
        // the voice OLED: the selected voice, the KIT rows, the 8 pads; with the hit graph on (module menu) also
        // its view cells and zoom - + FULL. placeVoiceOled() lays the cells out for either.
        DRVoiceDisplay* vd = createWidget<DRVoiceDisplay>(mm2px(Vec(kVoX, kVoY)));
        vd->box.size = mm2px(Vec(kVoW, kVoH)); vd->module = module;
        addChild(vd);
        for (int i = 0; i < kNumView; i++) {
            oled::Cell* c = cell(0.f, 0.f, 1.f, 1.f, DR::VIEW_PARAM, kViewName[i]);
            c->setTo = i;
            voView.push_back(c);
        }
        for (int d = 0; d < 2; d++) {
            DRZoomStep* zs = createWidget<DRZoomStep>(Vec(0.f, 0.f));
            zs->module = module; zs->dir = d ? 1 : -1;
            addChild(zs);
            voView.push_back(zs);
        }
        oled::Cell* full = cell(0.f, 0.f, 1.f, 1.f, DR::ZOOM_PARAM, "FULL");
        full->setTo = 0;
        voView.push_back(full);
        for (int k = 0; k <= kNumFactoryKit + kNumUserKit; k++) {   // the last one is STORE
            DRKitCell* s = createWidget<DRKitCell>(Vec(0.f, 0.f));
            s->module = module; s->k = k < kNumFactoryKit + kNumUserKit ? k : -1;
            addChild(s);
            voKit.push_back(s);
        }
        for (int r = 0; r < 8; r++) {
            DRPadCell* pc = createParam<DRPadCell>(Vec(0.f, 0.f), module, DR::PAD_PARAM + r);
            pc->module = module; pc->r = r;
            addParam(pc);
            voPad[r] = pc;
        }
        placeVoiceOled(module && module->showGraph);
        // ACCENT: a trimpot at the foot of the voice OUT column, in line with FORGE FX's bottom knob row beside it
        // (70.8 + 4 x 11.7 mm), its name under it as there
        {
            const float x = kVsOutX, y = 70.8f + 4.f * 11.7f;
            DRRing* ring = createWidget<DRRing>(mm2px(Vec(x - 3.4f, y - 3.4f)));
            ring->box.size = mm2px(Vec(6.8f, 6.8f)); ring->module = module; ring->param = DR::ACCENT_PARAM; ring->width = 1.3f;
            addChild(ring);
            addParam(createParamCentered<Trimpot>(mm2px(Vec(x, y)), module, DR::ACCENT_PARAM));
            label(x, y + 5.4f, "ACC");   // short: the OLED's edge is close
        }
        // PRE L / R down the voice OUT column, just under the voice OLED's top, at the voice jacks' pitch (names to
        // their right), then the PRE level trimpot on FORGE FX's 4th knob row, over ACC
        {
            const float x = kVsOutX, yl = 70.8f + 11.7f + 3.33f, yr = yl + 7.4f;
            addOutput(createOutputCentered<SmallJack>(mm2px(Vec(x, yl)), module, DR::PRE_L_OUTPUT));
            label(x + 4.f, yl, "L");
            addOutput(createOutputCentered<SmallJack>(mm2px(Vec(x, yr)), module, DR::PRE_R_OUTPUT));
            label(x + 4.f, yr, "R");
            const float y = 70.8f + 3.f * 11.7f;
            DRRing* ring = createWidget<DRRing>(mm2px(Vec(x - 3.4f, y - 3.4f)));
            ring->box.size = mm2px(Vec(6.8f, 6.8f)); ring->module = module; ring->param = DR::PRE_LVL_PARAM; ring->width = 1.3f;
            addChild(ring);
            addParam(createParamCentered<Trimpot>(mm2px(Vec(x, y)), module, DR::PRE_LVL_PARAM));
            label(x, y + 5.4f, "PRE");
        }

        // ---------------- far right: the FINAL stage, as on MATRIX-DUO ----------------
        const float RX = 283.28f, RW = 50.f, OX = RX + 1.5f, OY = 10.5f, OW = RW - 3.f;
        DRMasterDisplay* md = createWidget<DRMasterDisplay>(mm2px(Vec(OX, OY)));
        md->box.size = mm2px(Vec(OW, kMoBtn + 2.f * kMoBtnH + kMoBtnGap + 1.f)); md->module = module;
        addChild(md);
        oled::Cell* mc = cell(OX + OW - 10.5f, OY + 0.7f, 9.f, 2.f * kMoRow - 1.4f, DR::MIDI_ON_PARAM, "MIDI");
        mc->latch = true; mc->green = true;
        // row 1: RUN | RESET | CLEAR, row 2: LEARN | +-5V | ACC
        struct CellDef { const char* label; int param; bool latch, green; int jack; };
        const CellDef cells[6] = {
            {"RUN", DR::RUN_PARAM, true, true, DR::RUN_INPUT},
            {"RESET", DR::RESET_PARAM, false, false, DR::RESET_INPUT},
            {"CLEAR", DR::CLEAR_PARAM, false, false, -1},
            {"LEARN", DR::LEARN_PARAM, true, false, -1},
            {"\xC2\xB1" "5V", DR::RANGE_PARAM, true, false, -1},
            {"ACC", DR::ACC_EDIT_PARAM, true, false, -1}};
        const float CX = OX + 2.f, CW = OW - 4.f, CG = 1.f;
        for (int i = 0; i < 6; i++) {
            int row = i / 3, k = i % 3;
            float w = (CW - CG * 2.f) / 3.f;
            const CellDef& d = cells[i];
            oled::Cell* c = cell(CX + k * (w + CG), OY + kMoBtn + row * (kMoBtnH + kMoBtnGap), w, kMoBtnH, d.param, d.label);
            c->latch = d.latch; c->green = d.green; c->jack = d.jack;
            if (module && d.param == DR::RESET_PARAM) c->flash = &module->resetFlash;   // lit on any reset
        }
        auto col4 = [&](int i) { return RX + RW * (i + 0.5f) / 4.f; };
        const int F2[4] = {DR::FLT_CUT_PARAM, DR::FLT_RES_PARAM, DR::FLT_ENV_PARAM, DR::FLT_MODE_PARAM};
        const char* F2N[4] = {"CUTOFF", "RES", "ENV", "MODE"};
        const int EV[4] = {DR::FLT_ATK_PARAM, DR::FLT_DEC_PARAM, DR::FLT_SUS_PARAM, DR::FLT_REL_PARAM};
        const char* EVN[4] = {"ATK", "DEC", "SUS", "REL"};
        for (int i = 0; i < 4; i++) {
            smallKnob(col4(i), 46.5f, F2[i], F2N[i]);
            smallKnob(col4(i), 61.5f, EV[i], EVN[i]);
        }
        const float KY = 79.f, RC = RX + RW * 0.5f;
        smallKnob(col4(0), KY, DR::DELAY_PARAM, "DELAY");
        smallKnob(col4(3), KY, DR::SPACE_PARAM, "SPACE");
        addChild(KnobTicks::create(Vec(RC, KY), 19.f / 11.f));
        DRRing* mr = createWidget<DRRing>(mm2px(Vec(RC - 9.5f, KY - 9.5f)));
        mr->box.size = mm2px(Vec(19.f, 19.f)); mr->module = module; mr->param = DR::MASTER_PARAM;
        addChild(mr);
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(RC, KY)), module, DR::MASTER_PARAM));
        label(RC, KY + 12.f, "MASTER");
        // jacks: CLK IN / OUT; IN L / R, OPEN, GLITCH, GATE, KNOB; RST, RUN, OUT L / R
        struct JackDef { const char* name; int id; bool out; };
        const JackDef jk[3][6] = {
            {{"CLK I", DR::CLOCK_INPUT, false}, {"CLK O", DR::CLOCK_OUTPUT, true}, {NULL, -1, false},
             {NULL, -1, false}, {NULL, -1, false}, {NULL, -1, false}},
            {{"IN L", DR::IN_L_INPUT, false}, {"IN R", DR::IN_R_INPUT, false}, {"OPEN", DR::OPEN_INPUT, false},
             {"GLITCH", DR::GLITCH_INPUT, false}, {"GATE", DR::GATE_INPUT, false}, {"KNOB", DR::KNOB_OUTPUT, true}},
            {{"RST", DR::RESET_INPUT, false}, {"RUN", DR::RUN_INPUT, false}, {NULL, -1, false},
             {NULL, -1, false}, {"OUT L", DR::OUT_L_OUTPUT, true}, {"OUT R", DR::OUT_R_OUTPUT, true}}};
        for (int r = 0; r < 3; r++)
            for (int i = 0; i < 6; i++) {
                const JackDef& d = jk[r][i];
                if (!d.name) continue;
                float x = RX + RW * (i + 0.5f) / 6.f, y = 95.f + 12.f * r;
                label(x, y - 5.f, d.name);
                if (d.out) addOutput(createOutputCentered<SmallJack>(mm2px(Vec(x, y)), module, d.id));
                else addInput(createInputCentered<SmallJack>(mm2px(Vec(x, y)), module, d.id));
            }

        // ---------------- FORGE FX, between the voices and the FINAL stage (as on MATRIX-DUO) ----------------
        forgeFxAddPanel(this, module ? &module->fx : NULL, module, DR::FX_ON_PARAM, 179.6f);
    }

    void appendContextMenu(Menu* menu) override {
        DR* m = getModule<DR>();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        auto indexItem = [&](const std::string& text, int paramId, std::vector<std::string> labels) {
            menu->addChild(createIndexSubmenuItem(text, labels,
                [=]() { return (size_t)(m->params[paramId].getValue() - 1.f); },
                [=](size_t i) { m->params[paramId].setValue((float)i + 1.f); }));
        };
        indexItem("Pattern length", DR::LEN_PARAM, {"1 step", "2 steps", "3 steps", "4 steps", "5 steps", "6 steps", "7 steps", "8 steps"});
        indexItem("JAM matrix page", DR::PAGE_PARAM, {"1", "2", "3", "4", "5", "6"});
        ui::Slider* bpm = new ui::Slider;
        bpm->quantity = m->paramQuantities[DR::BPM_PARAM];
        bpm->box.size.x = 220.f;
        menu->addChild(bpm);
        menu->addChild(createMenuItem("Clear every pattern", "", [=]() { m->clearAll(); }));
        menu->addChild(createBoolPtrMenuItem("Patched voice outs leave OUT L / R", "", &m->leaveMix));
        menu->addChild(createBoolPtrMenuItem("Voice OLED: show the hit graph (smaller kit cells and pads)", "", &m->showGraph));        menu->addChild(createSubmenuItem("Choke groups", "", [=](Menu* sub) {
            for (int r = 0; r < 8; r++)
                sub->addChild(createIndexSubmenuItem("Row " + std::to_string(r + 1), {"None", "1", "2", "3", "4"},
                    [=]() { return (size_t)m->choke[r]; }, [=](size_t i) { m->choke[r] = (int)i; }));
        }));
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolPtrMenuItem("JAM lights match panel colours", "", &m->matchColors));
        menu->addChild(createMenuLabel("Custom JAM LED colours (when not matching)"));
        for (int id : {DR::OFFC_PARAM, DR::ONC_PARAM, DR::HEADC_PARAM, DR::HITC_PARAM, DR::BONC_PARAM, DR::BOFFC_PARAM}) {
            ui::Slider* s = new ui::Slider;
            s->quantity = m->paramQuantities[id];
            s->box.size.x = 220.f;
            menu->addChild(s);
        }
    }
};

} // namespace

// RANDROYD builds drum rows and picks a factory kit for the genre (0 808, 1 909, 2 TECH, 3 ELEC, 4 LOFI,
// 5 606, 6 HOUSE, 7 DUB, 8 INDUS); the audio thread loads it, like a press on the KIT row.
// Outside the namespace above, so other files can call it.
void randroydDrumKit(Module* m, int k) { if (DR* d = dynamic_cast<DR*>(m)) d->kitReq.store(k); }
// a factory kit's row models (DM_*), so RANDROYD can put each groove row on the right instrument
void randroydKitModels(int k, int out[8]) {
    for (int r = 0; r < 8; r++) out[r] = (k >= 0 && k < kNumFactoryKit) ? kFactoryKit[k].model[r] : kKitModel[r];
}
// a kit with RANDROYD's genre shaping: add[row][macro] (TUNE DECAY TONE SNAP MOD DRIVE LEVEL PAN) on top of kit k
void randroydDrumKitShaped(Module* m, int k, const float add[8][8]) {
    DR* d = dynamic_cast<DR*>(m);
    if (!d) return;
    for (int r = 0; r < 8; r++) for (int b = 0; b < DK_NUM && b < 8; b++) d->rdShape[r][b] = add[r][b];
    d->rdShapeOn = true;
    d->kitReq.store(k);
}
void randroydDrumSilence(Module* m, int mask) { if (DR* d = dynamic_cast<DR*>(m)) { d->rdSilent = (uint8_t) mask; d->applySilent(); } }

Model* modelSynTHBloKDrum = createModel<SynTHBloKDrum, SynTHBloKDrumWidget>("Drum");
