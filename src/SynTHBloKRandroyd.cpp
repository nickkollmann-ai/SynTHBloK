// ============================================================================
// SynTHBloK-R  RANDROYD RANDOMYZER  (40 HP track instrument)
//
//  BANKS    8 banks x 16 save spots, a fixed numbered list. Spots fill only when you press SAVE and pick one.
//  SCENES   1-8 across the top: which tracks play. Edited in the SCENES tab.
//  SETS     A-H along the bottom: 8 scenes each, a whole song.
//  OLED     TRACKS / SCENES / BANKS / PATCH tabs.
//  Track colours follow MTX-LOOP's ramp: yellow (1) through orange and red to deep red (8).
//
//  BUILD STEP 1 (this file): panel, controls, matrix / scene / set state and saving, genre list,
//  MIDI ports, live CPU meter, smart ROLL / MUTATE / UNDO. NEW TRACK (generator), STORE / PARK
//  (tracks in the matrix), scene launching and CPU sizing arrive in the next steps.
//
//  All cable work happens in the widget's step() (UI thread), as Rack requires.
// ============================================================================
#include "plugin.hpp"
#include "SynTHBloKRandroyd_layout.hpp"
#include "OledUi.hpp"
#include "JackLed.hpp"
#include <tag.hpp>
#include <atomic>
#include <random>
#include <algorithm>
#include <map>
#include <set>
#include <ctime>

// MTX-DRUM: load a factory kit (SynTHBloKDrum.cpp)
void randroydDrumKit(Module* m, int k);
int randroydSongSection(Module* m);
void randroydDrumSilence(Module* m, int mask);

namespace rdz {
// MTX-LOOP's track ramp
static NVGcolor ramp(int i, int a = 255) {
    static const unsigned char C[8][3] = {{255, 210, 26}, {255, 178, 26}, {255, 143, 26}, {255, 106, 18},
                                          {255, 74, 18}, {232, 48, 15}, {184, 32, 12}, {138, 22, 10}};
    i = clamp(i, 0, 7);
    return nvgRGBA(C[i][0], C[i][1], C[i][2], a);
}
static NVGcolor green(int a = 255) { return nvgRGBA(120, 230, 90, a); }
static NVGcolor red(int a = 255) { return nvgRGBA(255, 90, 74, a); }

// the 16 styles of tools/patch_genres.py (name, tempo range, first key, first scale)
struct Genre { const char* name; int bpmLo, bpmHi, key, scale; };
static const Genre GENRES[16] = {
    {"HOUSE", 122, 126, 9, 0}, {"DEEP HOUSE", 118, 122, 2, 2}, {"TECHNO", 128, 134, 5, 3}, {"ACID", 124, 130, 9, 3},
    {"TRANCE", 136, 140, 9, 0}, {"ELECTRO", 124, 130, 2, 3}, {"HIP-HOP", 86, 94, 1, 6}, {"TRAP", 68, 72, 6, 5},
    {"LO-FI", 74, 84, 0, 1}, {"SYNTHWAVE", 100, 110, 9, 0}, {"DRUM & BASS", 85, 88, 5, 0}, {"DUB", 70, 78, 9, 0},
    {"AMBIENT", 66, 76, 2, 7}, {"FUNK", 108, 116, 4, 4}, {"AFROBEAT", 100, 108, 7, 4}, {"TRIP-HOP", 80, 88, 0, 5}};
static const char* KEYS[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char* SCALES[8] = {"MIN", "MAJ", "DORIAN", "PHRYG", "MIXO", "HARM MIN", "MIN PENT", "MAJ PENT"};
// each genre's groove (tools/patch_genres.py): MTX-DRUM rows KICK SNARE CLAP CLOSED-HAT OPEN-HAT TOM METAL FM,
// 8 sixteenths, '2' = accent; the fill adds to rows on pattern 4; reverb / delay / drive amounts; glitch styles
struct GenreDrums { const char* rows[8]; const char* fill[8]; float rev, dly, drive; bool glitch; };
static const GenreDrums GDRUMS[16] = {
    {{"20002000", "00000000", "00002000", "10101010", "00100010", "00000000", "00000000", "00000100"}, {"00000000", "00000001", "00000000", "00000000", "00000000", "00000110", "00000000", "00000000"}, 0.35f, 0.25f, 0.15f, false},  // House
    {{"20002000", "00000000", "00001000", "10111010", "00100010", "00000000", "00010010", "00000000"}, {"00000000", "00000000", "00001011", "00000000", "00000000", "00000000", "00000000", "00000000"}, 0.45f, 0.30f, 0.10f, false},  // Deep House
    {{"20002000", "00001000", "00000000", "01110111", "00100010", "00000000", "10001000", "00010001"}, {"00000000", "00001011", "00000000", "00000000", "00000000", "00000100", "00000000", "00000000"}, 0.30f, 0.30f, 0.30f, true},  // Techno
    {{"20002000", "00000000", "00002000", "01010101", "00100010", "00000000", "00000000", "00000000"}, {"00000000", "00000000", "00002022", "00000000", "00000000", "00000000", "00000000", "00000000"}, 0.25f, 0.35f, 0.30f, false},  // Acid
    {{"20002000", "00000000", "00002000", "10101010", "00100010", "00000000", "00000000", "00000000"}, {"00000000", "00011111", "00000000", "00000000", "00000000", "00000000", "00000000", "00000000"}, 0.50f, 0.40f, 0.15f, false},  // Trance
    {{"20000010", "00002000", "00000000", "10111011", "00000000", "00000000", "00000000", "01000100"}, {"00000000", "00000000", "00000000", "00000000", "00000000", "00000111", "00000000", "00000000"}, 0.25f, 0.30f, 0.30f, true},  // Electro
    {{"20000110", "00002000", "00000000", "10101010", "00000000", "00000000", "00000000", "00000000"}, {"00000000", "00002010", "00000000", "00000000", "00000000", "00000000", "00000000", "00000000"}, 0.25f, 0.20f, 0.25f, false},  // Hip-Hop
    {{"20000010", "00002000", "00000000", "11111111", "00000000", "00000000", "00000000", "00000000"}, {"00000000", "00000000", "00000000", "11112222", "00000000", "00000000", "00000000", "00000000"}, 0.35f, 0.25f, 0.35f, false},  // Trap
    {{"20000100", "00002000", "00000000", "10101010", "00000000", "00000000", "00000000", "00000000"}, {"00000000", "00002001", "00000000", "00000000", "00000000", "00000000", "00000000", "00000000"}, 0.40f, 0.20f, 0.20f, false},  // Lo-Fi
    {{"20000000", "00002000", "00000000", "10101010", "00000000", "00000000", "00000000", "00000000"}, {"00000000", "00000000", "00000000", "00000000", "00000000", "00001111", "00000000", "00000000"}, 0.50f, 0.35f, 0.15f, false},  // Synthwave
    {{"20000100", "00200020", "00000000", "11111111", "00000000", "00000000", "00000000", "00000000"}, {"00000000", "00200222", "00000000", "00000000", "00000000", "00000000", "00000000", "00000000"}, 0.35f, 0.30f, 0.25f, true},  // Drum & Bass
    {{"00002000", "00002000", "00000000", "10101010", "00000000", "00000000", "00100010", "00000000"}, {"00000000", "00000000", "00000000", "00000000", "00000000", "00000011", "00000000", "00000000"}, 0.50f, 0.60f, 0.15f, false},  // Dub
    {{"20000000", "00000000", "00000000", "00100000", "00000000", "00000000", "00000010", "00010000"}, {"00000000", "00000000", "00000000", "00000000", "00000000", "00000000", "00100010", "00000000"}, 0.80f, 0.45f, 0.00f, false},  // Ambient
    {{"20100010", "00002000", "00000000", "11111111", "00100000", "00000000", "00000000", "00000000"}, {"00000000", "00002121", "00000000", "00000000", "00000000", "00000000", "00000000", "00000000"}, 0.25f, 0.15f, 0.20f, false},  // Funk
    {{"20000100", "00001001", "00000000", "10110110", "00000000", "00100100", "10010010", "00000000"}, {"00000000", "00000000", "00000000", "00000000", "00000000", "00000111", "00000000", "00000000"}, 0.30f, 0.20f, 0.15f, false},  // Afrobeat
    {{"20000010", "00002000", "00000000", "10101010", "00000001", "00000000", "00000000", "00000000"}, {"00000000", "00002010", "00000000", "00000000", "00000000", "00000000", "00000000", "00000000"}, 0.50f, 0.35f, 0.30f, true},  // Trip-Hop
};
// each genre's harmony and rhythms: scales, keys (MTX order), two 4-chord progressions (scale degrees),
// the lead and bass rhythms (8 sixteenths), whether it is an acid style, and how bright its tones are
struct GenreHarm { int scales[3]; int keys[3]; int prog[2][4]; const char* lead; const char* bass; bool acid; float bright; };
static const GenreHarm GHARM[16] = {
    {{0, 2, 0}, {9, 5, 7}, {{0, 5, 2, 6}, {0, 3, 6, 5}}, "10010010", "00100010", false, 0.55f},  // House
    {{2, 0, 2}, {2, 0, 5}, {{0, 3, 0, 4}, {0, 5, 3, 4}}, "10001000", "10100010", false, 0.40f},  // Deep House
    {{3, 0, 3}, {5, 4, 7}, {{0, 0, 1, 0}, {0, 6, 0, 5}}, "10100100", "01110111", false, 0.45f},  // Techno
    {{3, 0, 3}, {9, 4, 2}, {{0, 0, 6, 0}, {0, 1, 0, 6}}, "11011011", "11011011", true, 0.50f},  // Acid
    {{0, 5, 0}, {9, 11, 7}, {{0, 5, 2, 6}, {0, 5, 3, 4}}, "11111111", "01110111", false, 0.60f},  // Trance
    {{3, 0, 3}, {2, 4, 5}, {{0, 6, 5, 6}, {0, 3, 6, 0}}, "10010100", "10010010", false, 0.55f},  // Electro
    {{6, 0, 6}, {1, 3, 0}, {{0, 5, 3, 4}, {0, 3, 0, 5}}, "10000100", "10000110", false, 0.40f},  // Hip-Hop
    {{5, 0, 5}, {6, 8, 5}, {{0, 5, 3, 4}, {0, 0, 5, 4}}, "10010010", "20000010", false, 0.45f},  // Trap
    {{1, 2, 7}, {0, 5, 10}, {{0, 5, 3, 4}, {1, 4, 0, 5}}, "10001000", "10000100", false, 0.30f},  // Lo-Fi
    {{0, 0, 0}, {9, 4, 2}, {{0, 5, 2, 6}, {0, 3, 5, 4}}, "10101010", "11111111", false, 0.55f},  // Synthwave
    {{0, 2, 0}, {5, 7, 4}, {{0, 5, 6, 4}, {0, 3, 5, 6}}, "10000010", "10000100", false, 0.50f},  // Drum & Bass
    {{0, 2, 0}, {9, 4, 7}, {{0, 3, 0, 4}, {0, 6, 5, 6}}, "00100010", "10010100", false, 0.35f},  // Dub
    {{7, 1, 7}, {2, 7, 0}, {{0, 3, 4, 3}, {0, 2, 3, 1}}, "10000000", "10000000", false, 0.35f},  // Ambient
    {{4, 2, 4}, {4, 9, 7}, {{0, 3, 0, 6}, {0, 0, 3, 4}}, "10110100", "10110010", false, 0.55f},  // Funk
    {{4, 7, 4}, {7, 0, 5}, {{0, 3, 0, 4}, {0, 6, 3, 0}}, "10101001", "10010100", false, 0.50f},  // Afrobeat
    {{5, 0, 5}, {0, 2, 7}, {{0, 5, 3, 4}, {0, 3, 5, 4}}, "10000100", "10000010", false, 0.35f},  // Trip-Hop
};
// the SynTHBloK scale list: 0 minor 1 major 2 dorian 3 phrygian 4 mixolydian 5 harmonic minor 6 min pent 7 maj pent
static const std::vector<int> SCALE_STEPS[8] = {{0, 2, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
                                               {0, 2, 4, 5, 7, 9, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 3, 5, 7, 10}, {0, 2, 4, 7, 9}};
// factory kits that suit each genre (0 808, 1 909, 2 TECH, 3 ELEC, 4 LOFI, 5 606, 6 HOUSE, 7 DUB, 8 INDUS)
static const std::vector<int> KITS[16] = {{6, 1}, {6, 0}, {2, 1, 8}, {0, 1}, {1}, {3, 0}, {4, 0}, {0}, {4}, {5, 0},
                                          {1, 2}, {7}, {4, 5}, {0, 4}, {4, 5}, {4, 7}};
static const char* KIT_NAMES[9] = {"808", "909", "TECH", "ELEC", "LOFI", "606", "HOUSE", "DUB", "INDUS"};
// what each matrix column builds
static const char* ROLES[8] = {"DRUMS", "BASS", "LEAD", "CHORDS", "PERC", "TEXTURE", "FX SEND", "MOD"};
static const char* SIZES[4] = {"AUTO", "LIGHT", "MED", "FULL"};
}

struct SynTHBloKRandroyd : Module {
    enum ParamIds {
        NEWTRACK_PARAM, NEWRACK_PARAM, SHIFT_PARAM, GENRE_PARAM,
        ROLL_PARAM, MUTATEBTN_PARAM, STORE_PARAM, PARK_PARAM, UNDO_PARAM,
        TAB_PARAM, SIZE_PARAM, MUTATE_PARAM, DENSITY_PARAM, FX_PARAM, NUM_PARAMS
    };
    enum InputIds { CLOCK_INPUT, SCENE_INPUT, ROLL_INPUT, MUTATE_INPUT, NUM_INPUTS };
    enum OutputIds { TRIG_OUTPUT, SCENE_OUTPUT, CPU_OUTPUT, NUM_OUTPUTS };
    enum LightIds { NUM_LIGHTS };

    // ---- audio thread -> UI thread requests ----
    dsp::SchmittTrigger btn[9], rollIn, sceneIn;
    std::atomic<bool> rollReq{false}, mutateReq{false}, storeReq{false}, parkReq{false}, undoReq{false},
        newTrackReq{false}, newRackReq{false}, sceneNextReq{false}, rolled{false};
    std::atomic<float> cpuLoad{0.f};
    std::atomic<int> sceneOut{-1};
    dsp::PulseGenerator trigPulse;
    midi::InputQueue midiIn;
    midi::Output midiOut;

    // ---- state (UI thread; saved with the patch) ----
    struct Slot { bool used = false; std::string name; bool named = false; int genre = 0; int bpm = 0; int key = -1; int scale = 0; int modules = 0; std::string snapshot; std::string date; };
    struct Scene { bool used = false; std::string name; uint8_t mask = 0; int bars = 8; };
    struct Set { bool used = false; Scene scenes[8]; };
    static const int BANKS = 8, SPOTS = 16;   // a pad number is bank * SPOTS + spot
    Slot slots[BANKS][SPOTS];   // [bank][spot]
    Set sets[8];
    int bank = 0, set = 0, scene = -1, selTrack = -1;
    int curPad = -1;       // the spot whose track is in the rack (bank * SPOTS + spot), -1 none
    int mode = 0;          // 0 BUILD (recipes), 1 RACK (one rack, new songs, GROW)
    struct Grow { int64_t mod = -1; int track = 0; int ch = 0; int kind = 0; };
    std::vector<Grow> grown;   // modules GROW added, oldest first
    uint8_t arr[8] = {255, 255, 255, 255, 255, 255, 255, 255};   // arrangement: bit t = layer t plays in section A-H
    int arrKind = -1;
    std::vector<int64_t> myCables;
    // mixer channels locked to a track: the cable that feeds the channel, and the track that owns it
    struct Lock {
        int64_t cable = -1; int64_t mixer = -1; int input = 0; int track = 0; int channel = 0;
        static Lock make(int64_t c, int64_t mx, int in, int t, int ch) { Lock l; l.cable = c; l.mixer = mx; l.input = in; l.track = t; l.channel = ch; return l; }
    };
    std::vector<Lock> locks;
    uint32_t seed = 0;
    // modules RANDROYD built: the MASTER row (COMPOSER, mixer, audio) and each track's row
    int64_t composerId = -1, mixerId = -1, audioId = -1, masterId = -1;   // masterId: BloKMaster on the master bus
    std::vector<int64_t> masterMods;
    std::vector<int64_t> trackMods[8];
    int trackGenre[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int trackKit[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    // the rack's harmony, set by its first track: every instrument plays in this key, scale and progression
    int hKey = -1, hScale = 0;
    int hProg[4] = {0, 0, 0, 0};
    int trackRole[8] = {-1, -1, -1, -1, -1, -1, -1, -1};   // QUAD role: 0 bass 1 acid 2 lead 3 chords
    // each part's recipe (which modules build it): 0 the standard MTX build, 1 TECHNO RIG, 2 BROKEN BEAT,
    // 3 METAL GARDEN, 4 QUAD + MORPH FILTER, 5 QUAD + DELAY, 6 QUAD + DUAL FILTER, 7 MTX-DUO (bass part, plays the
    // lead too), 8 the lead inside that DUO, 9 808 BOOM (Euclid + Kick in key), 10 TUNED BELLS (Euclid strikes a Resonator)
    int trackRecipe[8] = {0, 0, 0, 0, 0, 0, 0, 0};   // 11 HYBRID KIT (MTX-DRUM's kick row plays a Kick)
    int trackFlags[8] = {0, 0, 0, 0, 0, 0, 0, 0};    // 1 a Resonator on the kick, 2 the DUO's bass notes play a Kick, 4 the DUO's lead notes strike a Resonator
    bool chaos = false, safe = true;
    std::string notice;    // short message in the OLED footer
    float noticeTime = 0.f;

    SynTHBloKRandroyd() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configButton(NEWTRACK_PARAM, "New track: every layer in the chosen genre (plays unsaved: SAVE, then pick a spot, keeps it)");
        configButton(NEWRACK_PARAM, "Clear: empty the rack of RANDROYD's rows (your pads are kept)");
        configSwitch(SHIFT_PARAM, 0.f, 1.f, 0.f, "Shift (or hold the Shift key)", {"Off", "On"});
        std::vector<std::string> gn;
        for (const rdz::Genre& g : rdz::GENRES) gn.push_back(g.name);
        configSwitch(GENRE_PARAM, 0.f, 15.f, 3.f, "Genre", gn);
        configButton(ROLL_PARAM, "Roll: new sounds for the playing track (no track: smart random cables)");
        configButton(MUTATEBTN_PARAM, "Mutate: variations on every layer (no track: re-roll part of the cables)");
        configButton(STORE_PARAM, "Save your tweaks into the playing track's pad");
        configButton(PARK_PARAM, "Park the live track (bypass: no CPU)");
        configButton(UNDO_PARAM, "Undo");
        configSwitch(TAB_PARAM, 0.f, 3.f, 0.f, "OLED view", {"Tracks", "Scenes", "Banks", "Patch"});
        configSwitch(SIZE_PARAM, 0.f, 3.f, 0.f, "Track size", {"Auto (by CPU)", "Light", "Medium", "Full"});
        configParam(MUTATE_PARAM, 0.f, 1.f, 0.35f, "Mutate (share a re-roll changes)", "%", 0.f, 100.f);
        configParam(DENSITY_PARAM, 0.f, 1.f, 0.5f, "Density (cables per roll; layers per track)", "%", 0.f, 100.f);
        configParam(FX_PARAM, 0.f, 1.f, 0.5f, "FX (how wet new tracks are)", "%", 0.f, 100.f);
        configInput(CLOCK_INPUT, "Clock (scene changes land on the bar)");
        configInput(SCENE_INPUT, "Next scene trigger");
        configInput(ROLL_INPUT, "Roll trigger");
        configInput(MUTATE_INPUT, "Mutate CV (0 to 10 V, added to the knob)");
        configOutput(TRIG_OUTPUT, "Trigger after each roll / scene change");
        configOutput(SCENE_OUTPUT, "Scene (0 V = scene 1 ... 7 V = scene 8)");
        configOutput(CPU_OUTPUT, "CPU load (10 V = 100 %)");
        for (int i = 0; i < 8; i++) sets[i].scenes[i].bars = 8;
    }

    float mutateAmount() {
        return clamp(params[MUTATE_PARAM].getValue() + inputs[MUTATE_INPUT].getVoltage() / 10.f, 0.f, 1.f);
    }
    int cablesWanted() { return 4 + (int) std::round(params[DENSITY_PARAM].getValue() * 28.f); }
    const rdz::Genre& genre() { return rdz::GENRES[clamp((int) std::round(params[GENRE_PARAM].getValue()), 0, 15)]; }
    void say(const std::string& s) { notice = s; noticeTime = 3.5f; }

    void process(const ProcessArgs& args) override {
        std::atomic<bool>* req[9] = {&newTrackReq, &newRackReq, NULL, NULL, &rollReq, &mutateReq, &storeReq, &parkReq, &undoReq};
        int ids[9] = {NEWTRACK_PARAM, NEWRACK_PARAM, -1, -1, ROLL_PARAM, MUTATEBTN_PARAM, STORE_PARAM, PARK_PARAM, UNDO_PARAM};
        for (int i = 0; i < 9; i++)
            if (req[i] && btn[i].process(params[ids[i]].getValue())) *req[i] = true;
        if (rollIn.process(inputs[ROLL_INPUT].getVoltage(), 0.1f, 1.f)) rollReq = true;
        if (sceneIn.process(inputs[SCENE_INPUT].getVoltage(), 0.1f, 1.f)) sceneNextReq = true;

        midi::Message msg;
        while (midiIn.tryPop(&msg, args.frame)) {}   // MIDI mapping arrives in a later step

        if (rolled.exchange(false)) trigPulse.trigger(1e-3f);
        outputs[TRIG_OUTPUT].setVoltage(trigPulse.process(args.sampleTime) ? 10.f : 0.f);
        int sc = sceneOut.load();
        outputs[SCENE_OUTPUT].setVoltage(sc < 0 ? 0.f : (float) sc);
        outputs[CPU_OUTPUT].setVoltage(10.f * cpuLoad.load());
    }

    void onReset() override {
        for (auto& b : slots) for (auto& s : b) s = Slot();
        for (auto& s : sets) s = Set();
        bank = 0; set = 0; scene = -1; selTrack = -1;
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_t* arr = json_array();
        for (int64_t id : myCables) json_array_append_new(arr, json_integer(id));
        json_object_set_new(root, "cables", arr);
        json_object_set_new(root, "seed", json_integer(seed));
        json_t* lk = json_array();
        for (const Lock& l : locks) {
            json_t* j = json_array();
            json_array_append_new(j, json_integer(l.cable));
            json_array_append_new(j, json_integer(l.mixer));
            json_array_append_new(j, json_integer(l.input));
            json_array_append_new(j, json_integer(l.track));
            json_array_append_new(j, json_integer(l.channel));
            json_array_append_new(lk, j);
        }
        json_object_set_new(root, "locks", lk);
        json_object_set_new(root, "chaos", json_boolean(chaos));
        json_object_set_new(root, "safe", json_boolean(safe));
        json_object_set_new(root, "composerId", json_integer(composerId));
        json_object_set_new(root, "mixerId", json_integer(mixerId));
        json_object_set_new(root, "audioId", json_integer(audioId));
        json_object_set_new(root, "masterId", json_integer(masterId));
        auto ids = [](const std::vector<int64_t>& v) { json_t* a = json_array(); for (int64_t i : v) json_array_append_new(a, json_integer(i)); return a; };
        json_object_set_new(root, "masterMods", ids(masterMods));
        json_t* tm = json_array();
        for (int t = 0; t < 8; t++) {
            json_t* tj = json_object();
            json_object_set_new(tj, "mods", ids(trackMods[t]));
            json_object_set_new(tj, "genre", json_integer(trackGenre[t]));
            json_object_set_new(tj, "kit", json_integer(trackKit[t]));
            json_array_append_new(tm, tj);
        }
        json_object_set_new(root, "tracks", tm);
        json_object_set_new(root, "hKey", json_integer(hKey));
        json_object_set_new(root, "hScale", json_integer(hScale));
        json_t* hp = json_array();
        for (int i = 0; i < 4; i++) json_array_append_new(hp, json_integer(hProg[i]));
        json_object_set_new(root, "hProg", hp);
        json_t* rl = json_array();
        for (int i = 0; i < 8; i++) json_array_append_new(rl, json_integer(trackRole[i]));
        json_object_set_new(root, "roles", rl);
        json_t* rc = json_array();
        for (int i = 0; i < 8; i++) json_array_append_new(rc, json_integer(trackRecipe[i]));
        json_object_set_new(root, "recipes", rc);
        json_t* fl = json_array();
        for (int i = 0; i < 8; i++) json_array_append_new(fl, json_integer(trackFlags[i]));
        json_object_set_new(root, "flags", fl);
        json_object_set_new(root, "bank", json_integer(bank));
        json_object_set_new(root, "set", json_integer(set));
        json_object_set_new(root, "scene", json_integer(scene));
        json_object_set_new(root, "selTrack", json_integer(selTrack));
        json_object_set_new(root, "curPad", json_integer(curPad));
        json_object_set_new(root, "spots", json_integer(SPOTS));
        json_t* ar = json_array();
        for (int i = 0; i < 8; i++) json_array_append_new(ar, json_integer(this->arr[i]));
        json_object_set_new(root, "arr", ar);
        json_object_set_new(root, "arrKind", json_integer(arrKind));
        json_object_set_new(root, "mode", json_integer(mode));
        json_t* gw = json_array();
        for (const Grow& g : grown) {
            json_t* e = json_array();
            json_array_append_new(e, json_integer(g.mod)); json_array_append_new(e, json_integer(g.track));
            json_array_append_new(e, json_integer(g.ch)); json_array_append_new(e, json_integer(g.kind));
            json_array_append_new(gw, e);
        }
        json_object_set_new(root, "grown", gw);
        json_t* sl = json_array();
        for (int b = 0; b < BANKS; b++) for (int t = 0; t < SPOTS; t++) {
            const Slot& s = slots[b][t];
            if (!s.used) continue;
            json_t* j = json_object();
            json_object_set_new(j, "bank", json_integer(b));
            json_object_set_new(j, "track", json_integer(t));
            json_object_set_new(j, "name", json_string(s.name.c_str()));
            json_object_set_new(j, "genre", json_integer(s.genre));
            json_object_set_new(j, "bpm", json_integer(s.bpm));
            json_object_set_new(j, "key", json_integer(s.key));
            json_object_set_new(j, "scale", json_integer(s.scale));
            json_object_set_new(j, "named", json_boolean(s.named));
            json_object_set_new(j, "modules", json_integer(s.modules));
            json_object_set_new(j, "snap", json_string(s.snapshot.c_str()));
            json_object_set_new(j, "date", json_string(s.date.c_str()));
            json_array_append_new(sl, j);
        }
        json_object_set_new(root, "slots", sl);
        json_t* st = json_array();
        for (int i = 0; i < 8; i++) {
            json_t* sj = json_object();
            json_object_set_new(sj, "used", json_boolean(sets[i].used));
            json_t* scs = json_array();
            for (int k = 0; k < 8; k++) {
                const Scene& c = sets[i].scenes[k];
                json_t* cj = json_object();
                json_object_set_new(cj, "used", json_boolean(c.used));
                json_object_set_new(cj, "name", json_string(c.name.c_str()));
                json_object_set_new(cj, "mask", json_integer(c.mask));
                json_object_set_new(cj, "bars", json_integer(c.bars));
                json_array_append_new(scs, cj);
            }
            json_object_set_new(sj, "scenes", scs);
            json_array_append_new(st, sj);
        }
        json_object_set_new(root, "sets", st);
        json_object_set_new(root, "midiIn", midiIn.toJson());
        json_object_set_new(root, "midiOut", midiOut.toJson());
        return root;
    }

    void dataFromJson(json_t* root) override {
        auto geti = [&](const char* k, int d) { json_t* v = json_object_get(root, k); return v ? (int) json_integer_value(v) : d; };
        myCables.clear();
        if (json_t* arr = json_object_get(root, "cables")) {
            size_t i; json_t* v;
            json_array_foreach(arr, i, v) myCables.push_back(json_integer_value(v));
        }
        seed = (uint32_t) geti("seed", 0);
        locks.clear();
        if (json_t* lk = json_object_get(root, "locks")) {
            size_t i; json_t* j;
            json_array_foreach(lk, i, j) {
                if (json_array_size(j) < 5) continue;
                Lock l;
                l.cable = json_integer_value(json_array_get(j, 0));
                l.mixer = json_integer_value(json_array_get(j, 1));
                l.input = (int) json_integer_value(json_array_get(j, 2));
                l.track = (int) json_integer_value(json_array_get(j, 3));
                l.channel = (int) json_integer_value(json_array_get(j, 4));
                locks.push_back(l);
            }
        }
        if (json_t* v = json_object_get(root, "chaos")) chaos = json_boolean_value(v);
        if (json_t* v = json_object_get(root, "safe")) safe = json_boolean_value(v);
        composerId = json_integer_value(json_object_get(root, "composerId")) ?: -1;
        mixerId = json_integer_value(json_object_get(root, "mixerId")) ?: -1;
        audioId = json_integer_value(json_object_get(root, "audioId")) ?: -1;
        masterId = json_object_get(root, "masterId") ? json_integer_value(json_object_get(root, "masterId")) : -1;
        auto rdIds = [](json_t* a, std::vector<int64_t>& v) { v.clear(); size_t i; json_t* x; if (a) json_array_foreach(a, i, x) v.push_back(json_integer_value(x)); };
        rdIds(json_object_get(root, "masterMods"), masterMods);
        if (json_t* tm = json_object_get(root, "tracks"))
            for (int t = 0; t < 8 && t < (int) json_array_size(tm); t++) {
                json_t* tj = json_array_get(tm, t);
                rdIds(json_object_get(tj, "mods"), trackMods[t]);
                trackGenre[t] = (int) json_integer_value(json_object_get(tj, "genre"));
                trackKit[t] = (int) json_integer_value(json_object_get(tj, "kit"));
                if (!json_object_get(tj, "genre")) trackGenre[t] = -1;
            }
        if (json_t* v = json_object_get(root, "hKey")) hKey = (int) json_integer_value(v);
        if (json_t* v = json_object_get(root, "hScale")) hScale = (int) json_integer_value(v);
        if (json_t* hp = json_object_get(root, "hProg")) for (int i = 0; i < 4; i++) hProg[i] = (int) json_integer_value(json_array_get(hp, i));
        if (json_t* rl = json_object_get(root, "roles")) for (int i = 0; i < 8; i++) trackRole[i] = rl && json_array_get(rl, i) ? (int) json_integer_value(json_array_get(rl, i)) : -1;
        if (json_t* rc = json_object_get(root, "recipes")) for (int i = 0; i < 8; i++) trackRecipe[i] = (int) json_integer_value(json_array_get(rc, i));
        if (json_t* fl = json_object_get(root, "flags")) for (int i = 0; i < 8; i++) trackFlags[i] = (int) json_integer_value(json_array_get(fl, i));
        bank = clamp(geti("bank", 0), 0, 7);
        set = clamp(geti("set", 0), 0, 7);
        scene = clamp(geti("scene", -1), -1, 7);
        selTrack = clamp(geti("selTrack", -1), -1, 7);
        curPad = geti("curPad", -1);
        if (curPad >= 0 && geti("spots", 8) == 8) curPad = (curPad / 8) * SPOTS + curPad % 8;   // saved before 16 spots per bank
        curPad = clamp(curPad, -1, BANKS * SPOTS - 1);
        if (json_t* ar = json_object_get(root, "arr")) for (int i = 0; i < 8; i++) arr[i] = (uint8_t) json_integer_value(json_array_get(ar, i));
        arrKind = geti("arrKind", -1);
        mode = geti("mode", 0);
        grown.clear();
        if (json_t* gw = json_object_get(root, "grown")) {
            size_t i; json_t* e;
            json_array_foreach(gw, i, e) {
                Grow g;
                g.mod = json_integer_value(json_array_get(e, 0)); g.track = (int) json_integer_value(json_array_get(e, 1));
                g.ch = (int) json_integer_value(json_array_get(e, 2)); g.kind = (int) json_integer_value(json_array_get(e, 3));
                grown.push_back(g);
            }
        }
        for (auto& b : slots) for (auto& s : b) s = Slot();
        if (json_t* sl = json_object_get(root, "slots")) {
            size_t i; json_t* j;
            json_array_foreach(sl, i, j) {
                int b = (int) json_integer_value(json_object_get(j, "bank"));
                int t = (int) json_integer_value(json_object_get(j, "track"));
                if (b < 0 || b >= BANKS || t < 0 || t >= SPOTS) continue;
                Slot& s = slots[b][t];
                s.used = true;
                if (json_t* v = json_object_get(j, "name")) s.name = json_string_value(v);
                s.genre = (int) json_integer_value(json_object_get(j, "genre"));
                s.bpm = (int) json_integer_value(json_object_get(j, "bpm"));
                s.key = json_object_get(j, "key") ? (int) json_integer_value(json_object_get(j, "key")) : -1;
                s.scale = (int) json_integer_value(json_object_get(j, "scale"));
                s.named = json_is_true(json_object_get(j, "named"));
                s.modules = (int) json_integer_value(json_object_get(j, "modules"));
                if (json_t* v = json_object_get(j, "snap")) s.snapshot = json_string_value(v);
                if (json_t* v = json_object_get(j, "date")) s.date = json_string_value(v);
            }
        }
        if (json_t* st = json_object_get(root, "sets")) {
            for (int i = 0; i < 8 && i < (int) json_array_size(st); i++) {
                json_t* sj = json_array_get(st, i);
                sets[i].used = json_boolean_value(json_object_get(sj, "used"));
                json_t* scs = json_object_get(sj, "scenes");
                for (int k = 0; k < 8 && scs && k < (int) json_array_size(scs); k++) {
                    json_t* cj = json_array_get(scs, k);
                    Scene& c = sets[i].scenes[k];
                    c.used = json_boolean_value(json_object_get(cj, "used"));
                    if (json_t* v = json_object_get(cj, "name")) c.name = json_string_value(v);
                    c.mask = (uint8_t) json_integer_value(json_object_get(cj, "mask"));
                    c.bars = std::max(1, (int) json_integer_value(json_object_get(cj, "bars")));
                }
            }
        }
        if (json_t* v = json_object_get(root, "midiIn")) midiIn.fromJson(v);
        if (json_t* v = json_object_get(root, "midiOut")) midiOut.fromJson(v);
        sceneOut = scene;
    }
};

// ---------------------------------------------------------------------------
// What the two OLEDs show. Rebuilt by the widget a few times a second.
// ---------------------------------------------------------------------------
struct RdInfo {
    struct Mod { std::string name; int ins = 0, outs = 0; };
    struct Cab { std::string from, to; bool mine = false; int lockTrack = -1; };
    std::vector<Mod> mods;
    std::vector<Cab> cabs;
    int freeIns = 0, mine = 0;
};

static const int RD_SPOTS = SynTHBloKRandroyd::SPOTS;
static int rdBankOf(int pad) { return pad / RD_SPOTS; }
static int rdSpotOf(int pad) { return pad % RD_SPOTS; }
static std::string rdSpotName(int pad) { return string::f("BANK %d SPOT %02d", rdBankOf(pad) + 1, rdSpotOf(pad) + 1); }
static std::string rdToday() {   // the save date, MM/DD/YY
    std::time_t t = std::time(nullptr);
    char buf[16] = "";
    if (std::tm* lt = std::localtime(&t)) std::strftime(buf, sizeof buf, "%m/%d/%y", lt);
    return buf;
}
static std::string upperTrim(std::string s, size_t n) {
    for (char& c : s) c = (char) std::toupper((unsigned char) c);
    if (s.size() > n) s = s.substr(0, n);
    return s;
}

// short module name for the cable list: "SynTHBloK-K Kick" -> "K KICK", "LFO" -> "LFO"
static std::string shortModule(app::ModuleWidget* mw) {
    std::string n = mw->model ? mw->model->name : "?";
    std::string p = (mw->model && mw->model->plugin) ? mw->model->plugin->name : "";
    if (!p.empty() && n.size() > p.size() + 1 && n.compare(0, p.size(), p) == 0) n = n.substr(p.size() + 1);
    return upperTrim(n, 7);
}

static std::string portName(app::PortWidget* pw) {
    engine::PortInfo* info = pw->getPortInfo();
    std::string n = info ? info->name : string::f("%d", pw->portId + 1);
    return upperTrim(n, 7);
}


// ---------------------------------------------------------------------------
// Smart rolls: what kind of signal a jack carries, read from its name and its module's tags.
//  AUDIO: sound.  GATE: clocks, triggers, gates.  PITCH: V/OCT, tune, FM.  CV: slow control.
// A roll only joins kinds that make musical sense, so audio never lands in a clock, speed
// or pitch input (that is what makes a random patch scream or race), and it makes no loops.
// ---------------------------------------------------------------------------
enum RdSig { SIG_AUDIO, SIG_GATE, SIG_PITCH, SIG_CV };

static std::vector<std::string> rdTokens(const std::string& name) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : name) {
        char c = (char) std::toupper((unsigned char) ch);
        if (std::isalnum((unsigned char) c) || c == '/' || c == '&') cur += c;
        else { if (!cur.empty()) out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
static bool rdHas(const std::vector<std::string>& t, std::initializer_list<const char*> words) {
    for (const std::string& s : t)
        for (const char* w : words) if (s == w) return true;
    return false;
}
static const std::initializer_list<const char*> kGate = {"GATE", "GATES", "TRIG", "TRIGGER", "CLOCK", "CLK", "RESET", "RST", "RUN",
    "SYNC", "BEAT", "BAR", "STEP", "EOC", "EOR", "END", "PULSE", "ACC", "ACCENT", "STRIKE", "HIT", "START", "STOP", "SECT"};
static const std::initializer_list<const char*> kPitch = {"V/OCT", "VOCT", "1V/OCT", "PITCH", "NOTE", "TUNE", "TUNING", "FREQ", "FM", "TRANSPOSE"};
static const std::initializer_list<const char*> kTime = {"TIME", "RATE", "SPEED", "BPM", "TEMPO", "LENGTH", "LEN", "DIV", "MULT", "SIZE"};
static const std::initializer_list<const char*> kCvWords = {"CV", "MOD", "KNOB", "ENV", "ENVELOPE", "LFO", "RANDOM", "S&H", "COUNT", "LEVEL", "PHASE", "DEPTH", "AMOUNT", "GLOBAL", "OFFSET", "ATTEN"};

static bool rdModuleIsControl(app::ModuleWidget* mw) {
    if (!mw->model) return false;
    for (int id : mw->model->tagIds) {
        std::string t = tag::getTag(id);
        if (t == "Low-frequency oscillator" || t == "Envelope generator" || t == "Sequencer" || t == "Random"
            || t == "Clock generator" || t == "Clock modulator" || t == "Sample and hold" || t == "Controller"
            || t == "Logic" || t == "Arpeggiator" || t == "Quantizer")
            return true;
    }
    return false;
}

static std::string rdPortFullName(app::PortWidget* pw) {
    engine::PortInfo* info = pw->getPortInfo();
    return info ? info->name : "";
}

static RdSig rdOutputSig(app::ModuleWidget* mw, app::PortWidget* pw) {
    std::vector<std::string> t = rdTokens(rdPortFullName(pw));
    if (rdHas(t, kGate)) return SIG_GATE;
    if (rdHas(t, kPitch)) return SIG_PITCH;
    if (rdHas(t, kCvWords)) return SIG_CV;
    return rdModuleIsControl(mw) ? SIG_CV : SIG_AUDIO;
}

// inputs: also says whether it is a timing input (time, rate, speed...), which only takes CV
static RdSig rdInputSig(app::PortWidget* pw, bool* timing) {
    std::vector<std::string> t = rdTokens(rdPortFullName(pw));
    *timing = rdHas(t, kTime);
    if (rdHas(t, kGate)) return SIG_GATE;
    if (rdHas(t, kPitch)) return SIG_PITCH;
    if (*timing || rdHas(t, kCvWords)) return SIG_CV;
    return SIG_AUDIO;   // "In L", "Ch 3", "Return", "Audio"...
}

static bool rdCompatible(RdSig out, RdSig in, bool timing) {
    switch (out) {
        case SIG_AUDIO: return in == SIG_AUDIO;
        case SIG_GATE: return in == SIG_GATE || (in == SIG_CV && !timing);
        case SIG_PITCH: return in == SIG_PITCH || (in == SIG_CV && !timing);
        case SIG_CV: return in == SIG_CV || in == SIG_PITCH;
    }
    return false;
}




// ---------------------------------------------------------------------------
// Mixer routing: sounds that end a chain go to free mixer channels, locked to the track.
// ---------------------------------------------------------------------------
static bool rdIsMixer(app::ModuleWidget* mw) {
    if (!mw->model) return false;
    for (int id : mw->model->tagIds) if (tag::getTag(id) == "Mixer") return true;
    std::vector<std::string> t = rdTokens(mw->model->name);
    for (const std::string& w : t) if (w.compare(0, 3, "MIX") == 0) return true;
    return false;
}
// a mixer input that takes a channel's sound (not an FX return, not a CV)
static bool rdIsChannelInput(app::PortWidget* pw) {
    bool timing;
    if (rdInputSig(pw, &timing) != SIG_AUDIO) return false;
    std::vector<std::string> t = rdTokens(rdPortFullName(pw));
    return !rdHas(t, {"RETURN", "RTN", "RET", "SIDECHAIN", "SC", "AUX", "EXT"});
}
// channel number in a name ("CH 3 L" -> 3), or 0
static int rdChannelNum(app::PortWidget* pw) {
    for (const std::string& w : rdTokens(rdPortFullName(pw))) {
        if (!w.empty() && std::isdigit((unsigned char) w[0])) return std::atoi(w.c_str());
        if (w.size() > 2 && w.compare(0, 2, "CH") == 0 && std::isdigit((unsigned char) w[2])) return std::atoi(w.c_str() + 2);
    }
    return 0;
}
// -1 left, 1 right, 0 mono / unknown
static int rdSide(app::PortWidget* pw) {
    std::vector<std::string> t = rdTokens(rdPortFullName(pw));
    if (rdHas(t, {"L", "LEFT"})) return -1;
    if (rdHas(t, {"R", "RIGHT"})) return 1;
    return 0;
}

static bool rdShift(SynTHBloKRandroyd* m) {
    if (m && m->params[SynTHBloKRandroyd::SHIFT_PARAM].getValue() > 0.5f) return true;
    return (APP->window->getMods() & RACK_MOD_MASK) == GLFW_MOD_SHIFT;
}
static void rdShiftDone(SynTHBloKRandroyd* m) {
    if (m) m->params[SynTHBloKRandroyd::SHIFT_PARAM].setValue(0.f);
}
static void rdFont(NVGcontext* vg) {
    std::shared_ptr<window::Font> font = APP->window->uiFont;
    if (font) nvgFontFaceId(vg, font->handle);
}
static void rdText(NVGcontext* vg, float x, float y, const std::string& s, float size, NVGcolor c, int align) {
    nvgFontSize(vg, size);
    nvgFillColor(vg, c);
    nvgTextAlign(vg, align | NVG_ALIGN_MIDDLE);
    nvgText(vg, x, y, s.c_str(), NULL);
}
static void rdBox(NVGcontext* vg, float x, float y, float w, float h, NVGcolor fill, NVGcolor stroke, float sw = 0.8f, float r = 1.6f) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, r);
    nvgFillColor(vg, fill);
    nvgFill(vg);
    if (sw > 0.f) {
        nvgStrokeColor(vg, stroke);
        nvgStrokeWidth(vg, sw);
        nvgStroke(vg);
    }
}

// ---------------------------------------------------------------------------
// A panel button drawn in the lit layer. Styles: ORANGE (lit action), DARK (amber outline),
// RED (NEW RACK), GREEN (MIDI). setTo >= 0: a selector cell (lit while the param holds that value).
// ---------------------------------------------------------------------------
struct RdButton : app::ParamWidget {
    enum Style { ORANGE, DARK, RED };
    Module* module = NULL;
    std::string label;
    Style style = DARK;
    int setTo = -1;
    bool latch = false;
    float fontSize = 7.f;
    bool held = false;
    std::function<void()> onPress;   // set: runs on the click itself (UI event, not inside step())
    bool lit() {
        ParamQuantity* pq = getParamQuantity();
        if (!module || !pq) return style == ORANGE || setTo == 0;
        if (setTo >= 0) return (int) std::round(pq->getValue()) == setTo;
        if (latch) return pq->getValue() > 0.5f;
        return style == ORANGE || held;
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        ParamWidget::drawLayer(args, layer);
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        bool on = lit();
        NVGcolor fill, stroke, ink;
        if (style == RED) { fill = on && held ? rdz::red(200) : oled::offFill(); stroke = rdz::red(200); ink = on && held ? oled::ink() : rdz::red(); }
        else if (on) { fill = oled::litFill(); stroke = oled::amber(); ink = oled::ink(); }
        else { fill = oled::offFill(); stroke = oled::outline(120); ink = oled::amber(); }
        if (held && style != RED) fill = nvgRGBA(255, 160, 60, 255);
        rdBox(args.vg, 0.f, 0.f, W, H, fill, stroke, 0.9f, 1.8f);
        rdFont(args.vg);
        rdText(args.vg, W * 0.5f, H * 0.5f, label, fontSize, ink, NVG_ALIGN_CENTER);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (ParamQuantity* pq = getParamQuantity()) {
                if (setTo >= 0) pq->setValue((float) setTo);
                else if (latch) pq->setValue(pq->getValue() > 0.5f ? 0.f : 1.f);
                else { pq->setValue(1.f); held = true; }
            }
            if (onPress && module) onPress();
            e.consume(this);
            return;
        }
        ParamWidget::onButton(e);
    }
    void onDragStart(const DragStartEvent& e) override {}
    void onDragEnd(const DragEndEvent& e) override {
        if (setTo < 0 && !latch && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            if (ParamQuantity* pq = getParamQuantity()) pq->setValue(0.f);
            held = false;
        }
    }
};

// ---------------------------------------------------------------------------
// GENRE OLED: click the left half = previous, right half = next, wheel steps; right-click lists all.
// ---------------------------------------------------------------------------
struct RdGenre : app::ParamWidget {
    Module* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        ParamWidget::drawLayer(args, layer);
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        int g = 3;
        if (ParamQuantity* pq = getParamQuantity()) g = clamp((int) std::round(pq->getValue()), 0, 15);
        const rdz::Genre& G = rdz::GENRES[g];
        rdFont(args.vg);
        rdText(args.vg, 6.f, mm2px(2.8f), "GENRE", 6.f, oled::dim(), NVG_ALIGN_LEFT);
        rdText(args.vg, W - 6.f, mm2px(2.8f), string::f("< %d / 16 >", g + 1), 6.f, oled::dim(), NVG_ALIGN_RIGHT);
        rdText(args.vg, 6.f, mm2px(8.2f), G.name, 11.f, oled::amber(), NVG_ALIGN_LEFT);
        rdText(args.vg, W - 6.f, mm2px(8.2f),
               string::f("%d BPM  %s %s", (G.bpmLo + G.bpmHi) / 2, rdz::KEYS[G.key], rdz::SCALES[G.scale]), 6.5f, oled::amber(200), NVG_ALIGN_RIGHT);
    }
    void step(int d) {
        if (ParamQuantity* pq = getParamQuantity()) pq->setValue((float) (((int) std::round(pq->getValue()) + d + 16) % 16));
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            step(e.pos.x < box.size.x * 0.5f ? -1 : 1);
            e.consume(this);
            return;
        }
        ParamWidget::onButton(e);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        step(e.scrollDelta.y > 0.f ? -1 : 1);
        e.consume(this);
    }
    void onDragStart(const DragStartEvent& e) override {}
};

// ---------------------------------------------------------------------------
// MIDI panel: green MIDI cell, IN and OUT device lines. Click the top half = input menu, bottom = output.
// ---------------------------------------------------------------------------
struct RdMidi : OpaqueWidget {
    SynTHBloKRandroyd* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1) {
            float W = box.size.x, H = box.size.y;
            rdBox(args.vg, 0.f, 0.f, W, H, nvgRGBA(18, 13, 6, 255), nvgRGBA(107, 66, 23, 255), 0.7f, 1.8f);
            bool conn = module && (module->midiIn.getDeviceId() >= 0 || module->midiOut.getDeviceId() >= 0);
            float bw = mm2px(7.4f);
            rdBox(args.vg, mm2px(1.2f), mm2px(1.3f), bw, H - mm2px(2.6f), conn || !module ? rdz::green() : rdz::green(60),
                  rdz::green(), 0.8f, 1.6f);
            rdFont(args.vg);
            rdText(args.vg, mm2px(1.2f) + bw * 0.5f, H * 0.5f, "MIDI", 6.f, nvgRGBA(13, 42, 6, 255), NVG_ALIGN_CENTER);
            std::string in = "Maschine Jam", out = "Maschine Jam";
            if (module) {
                in = module->midiIn.getDeviceId() >= 0 ? module->midiIn.getDeviceName(module->midiIn.getDeviceId()) : "(no device)";
                out = module->midiOut.getDeviceId() >= 0 ? module->midiOut.getDeviceName(module->midiOut.getDeviceId()) : "(no device)";
            }
            float tx = mm2px(10.f);
            rdText(args.vg, tx, mm2px(2.4f), "IN", 4.6f, oled::dim(), NVG_ALIGN_LEFT);
            rdText(args.vg, tx, mm2px(4.4f), upperTrim(in, 14), 5.6f, oled::amber(), NVG_ALIGN_LEFT);
            rdText(args.vg, tx, mm2px(6.6f), "OUT", 4.6f, oled::dim(), NVG_ALIGN_LEFT);
            rdText(args.vg, tx, mm2px(8.6f), upperTrim(out, 14), 5.6f, oled::amber(), NVG_ALIGN_LEFT);
        }
        Widget::drawLayer(args, layer);
    }
    void onButton(const ButtonEvent& e) override {
        if (module && e.action == GLFW_PRESS && (e.button == GLFW_MOUSE_BUTTON_LEFT || e.button == GLFW_MOUSE_BUTTON_RIGHT)) {
            if (e.pos.y < box.size.y * 0.5f) oled::midiMenu(&module->midiIn, "MIDI IN (controller)");
            else oled::midiMenu(&module->midiOut, "MIDI OUT (pad lights)");
            e.consume(this);
            return;
        }
        OpaqueWidget::onButton(e);
    }
};

// ---------------------------------------------------------------------------
// CPU meter + headroom line
// ---------------------------------------------------------------------------
struct RdCpu : TransparentWidget {
    SynTHBloKRandroyd* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1) {
            float load = module ? module->cpuLoad.load() : 0.42f;
            int n = 9;
            float x0 = mm2px(6.4f), sw = mm2px(1.8f), sp = mm2px(2.25f), y = 0.f, h = mm2px(3.2f);
            for (int i = 0; i < n; i++) {
                bool on = load * n > i;   // 9 segments, 0-100 %
                NVGcolor c = i < 5 ? rdz::green(on ? 240 : 45) : (i < 7 ? nvgRGBA(255, 178, 26, on ? 240 : 45) : rdz::red(on ? 240 : 45));
                rdBox(args.vg, x0 + i * sp, y, sw, h, c, c, 0.f, 0.8f);
            }
            rdFont(args.vg);
            rdText(args.vg, box.size.x, h * 0.5f, string::f("%d%%", (int) std::round(load * 100.f)), 7.f, oled::amber(), NVG_ALIGN_RIGHT);
            int room = std::max(0, (int) std::floor((0.6f - load) / 0.08f));
            std::string line = load > 0.85f ? "CPU LIMIT" : (room > 0 ? string::f("ROOM FOR ~%d MORE", room) : "NEAR THE LIMIT");
            rdText(args.vg, 0.f, mm2px(5.4f), line, 5.2f, load > 0.85f ? rdz::red() : oled::amber(170), NVG_ALIGN_LEFT);
        }
        Widget::drawLayer(args, layer);
    }
};

// forward
struct SynTHBloKRandroydWidget;

// ---------------------------------------------------------------------------
// TRACKS OLED: the bank's 16 save spots, a fixed numbered list (EMPTY, or name + date).
// Click = play, right-click = rename / delete. SAVE and CLEAR are soft buttons along its bottom edge.
// ---------------------------------------------------------------------------
struct RdList : OpaqueWidget {
    SynTHBloKRandroyd* module = NULL;
    SynTHBloKRandroydWidget* owner = NULL;
    float scroll = 0.f;
    int flash = -1; float flashT = 0.f;   // a soft button lights while pressed
    // SAVE mode: every spot of the bank flashes (the playing track's own spot faster); click one to save there
    bool saveArm = false;
    float saveIdle = 0.f, blinkT = 0.f;
    // CLEAR mode: the bank's saved spots flash red; click one, then again, to delete it. The lit BANK button
    // (clicked twice) clears the whole bank. Deleting can't be undone, hence the second click.
    bool clearArm = false;
    int clearPad = -1, clearBank = -1;   // waiting for the second click
    std::vector<int> pads() {   // always the bank's 16 spots, in order: the list never moves
        std::vector<int> v;
        int b = module ? clamp(module->bank, 0, 7) : 0;
        for (int i = 0; i < SynTHBloKRandroyd::SPOTS; i++) v.push_back(b * SynTHBloKRandroyd::SPOTS + i);
        return v;
    }
    float rowH() { return (box.size.y - top() - mm2px(9.6f)) / (float) SynTHBloKRandroyd::SPOTS; }   // all 16 fit
    float top() { return mm2px(13.6f); }
    Rect bankBtn(int b) {   // BANK 1-8, a row under the title
        float x0 = mm2px(2.f), gap = mm2px(0.8f), w = (box.size.x - mm2px(4.f) - 7.f * gap) / 8.f;
        return Rect(Vec(x0 + b * (w + gap), mm2px(7.2f)), Vec(w, mm2px(4.6f)));
    }
    int visible() { return SynTHBloKRandroyd::SPOTS; }
    Rect softBtn(int k) {   // 0 SAVE, 1 GROW, 2 MODE, 3 CLEAR
        float y = box.size.y - mm2px(5.4f), h = mm2px(4.4f), w = (box.size.x - mm2px(7.f)) / 4.f;
        return Rect(Vec(mm2px(2.f) + k * (w + mm2px(1.f)), y), Vec(w, h));
    }
    void drawLayer(const DrawArgs& args, int layer) override;
    void onButton(const ButtonEvent& e) override;
};

// ---------------------------------------------------------------------------
// Main OLED: TRACKS / SCENES / BANKS / PATCH
// ---------------------------------------------------------------------------
struct RdInfoHolder { RdInfo info; };
struct RdOled : OpaqueWidget {
    SynTHBloKRandroyd* module = NULL;
    SynTHBloKRandroydWidget* owner = NULL;
    const RdInfo* info = NULL;
    float scroll = 0.f;
    int tab() { int t = module ? clamp((int) std::round(module->params[SynTHBloKRandroyd::TAB_PARAM].getValue()), 0, 3) : 0; return t == 3 ? 3 : 0; }   // TRACK / PATCH
    void drawLayer(const DrawArgs& args, int layer) override;
    void onButton(const ButtonEvent& e) override;
    void onHoverScroll(const HoverScrollEvent& e) override {
        scroll = std::max(0.f, scroll - e.scrollDelta.y / 20.f);
        e.consume(this);
    }
};

// ---------------------------------------------------------------------------
// The widget: owns roll / clear / undo and the state edits (UI thread)
// ---------------------------------------------------------------------------
struct SynTHBloKRandroydWidget : ModuleWidget {
    RdInfo info;
    float infoTimer = 0.f;

    SynTHBloKRandroyd* rd() { return dynamic_cast<SynTHBloKRandroyd*>(module); }

    template <class T>
    T* place(T* w, float x, float y, float wmm, float hmm) {
        w->box.pos = mm2px(Vec(x, y));
        w->box.size = mm2px(Vec(wmm, hmm));
        return w;
    }
    RdButton* button(SynTHBloKRandroyd* m, int param, const char* label, float x, float y, float w, float h,
                     RdButton::Style style, float font = 7.f, int setTo = -1, bool latch = false) {
        RdButton* b = createParam<RdButton>(mm2px(Vec(x, y)), m, param);
        b->box.size = mm2px(Vec(w, h));
        b->module = m; b->label = label; b->style = style; b->fontSize = font; b->setTo = setTo; b->latch = latch;
        addParam(b);
        return b;
    }

    SynTHBloKRandroydWidget(SynTHBloKRandroyd* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKRandroyd.svg")));
        using namespace rack::componentlibrary;
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(RDL::PANEL_W - 3.5f, 3.5f))));

        // left: the saved tracks (SAVE / CLEAR inside), GENRE, MIDI
        RdList* li = place(new RdList, RDL::LIST_X, RDL::LIST_Y, RDL::LIST_W, RDL::LIST_H);
        li->module = module; li->owner = this;
        addChild(li);
        RdGenre* g = createParam<RdGenre>(mm2px(Vec(RDL::GENRE_X, RDL::GENRE_Y)), module, SynTHBloKRandroyd::GENRE_PARAM);
        g->box.size = mm2px(Vec(RDL::GENRE_W, RDL::GENRE_H));
        g->module = module;
        addParam(g);
        RdMidi* mi = place(new RdMidi, RDL::MIDI_X, RDL::MIDI_Y, RDL::MIDI_W, RDL::MIDI_H);
        mi->module = module;
        addChild(mi);

        // main OLED + its two screens' tabs
        RdOled* ol = place(new RdOled, RDL::OLED_X, RDL::OLED_Y, RDL::OLED_W, RDL::OLED_H);
        ol->module = module; ol->owner = this; ol->info = &info;
        addChild(ol);
        button(module, SynTHBloKRandroyd::TAB_PARAM, "TRACK", RDL::OLED_X + RDL::OLED_W - 31.f, RDL::OLED_Y + 2.2f, 13.8f, 4.8f, RdButton::DARK, 6.f, 0);
        button(module, SynTHBloKRandroyd::TAB_PARAM, "PATCH", RDL::OLED_X + RDL::OLED_W - 16.4f, RDL::OLED_Y + 2.2f, 13.8f, 4.8f, RdButton::DARK, 6.f, 3);

        // action row: NEW TRACK next to ROLL
        const char* acts[5] = {"NEW TRACK", "ROLL", "MUTATE", "PARK", "UNDO"};
        int actIds[5] = {SynTHBloKRandroyd::NEWTRACK_PARAM, SynTHBloKRandroyd::ROLL_PARAM, SynTHBloKRandroyd::MUTATEBTN_PARAM,
                         SynTHBloKRandroyd::PARK_PARAM, SynTHBloKRandroyd::UNDO_PARAM};
        for (int i = 0; i < 5; i++) {
            RdButton* b = button(module, actIds[i], acts[i], RDL::ACT_X0 + i * RDL::ACT_PITCH, RDL::ACT_Y, RDL::ACT_W, RDL::ACT_H,
                                 i < 2 ? RdButton::ORANGE : RdButton::DARK, 8.f);
            if (i == 0) b->onPress = [=]() { newTrack(); };
            if (i == 3) b->onPress = [=]() { parkPressed(); };
        }

        // knobs, jacks
        int knobs[3] = {SynTHBloKRandroyd::MUTATE_PARAM, SynTHBloKRandroyd::DENSITY_PARAM, SynTHBloKRandroyd::FX_PARAM};
        for (int i = 0; i < 3; i++)
            addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(RDL::KNOB_X[i], RDL::KNOB_Y)), module, knobs[i]));
        for (int i = 0; i < 4; i++) {
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(RDL::IN_X[i], RDL::IN_Y)), module, i));
            addChild(JackLed::create(Vec(RDL::IN_X[i] + 3.6f, RDL::IN_Y - 3.6f), module, i, false));
        }
        for (int i = 0; i < 3; i++) {
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(RDL::OUT_X[i], RDL::OUT_Y)), module, i));
            addChild(JackLed::create(Vec(RDL::OUT_X[i] + 3.6f, RDL::OUT_Y - 3.6f), module, i, true));
        }

        // CPU + SIZE
        RdCpu* cpu = place(new RdCpu, RDL::CPU_X, RDL::CPU_Y - 2.4f, 33.f, 7.f);
        cpu->module = module;
        addChild(cpu);
        for (int i = 0; i < 4; i++)
            button(module, SynTHBloKRandroyd::SIZE_PARAM, rdz::SIZES[i], RDL::SIZE_X + (i % 2) * RDL::SIZE_DX, RDL::SIZE_Y + (i / 2) * RDL::SIZE_DY,
                   RDL::SIZE_W, RDL::SIZE_H, RdButton::DARK, 6.5f, i);
    }

    // ---- modules RANDROYD may patch ----
    std::vector<app::ModuleWidget*> targets(bool safe) {
        std::vector<app::ModuleWidget*> out;
        for (app::ModuleWidget* mw : APP->scene->rack->getModules()) {
            if (mw == this || !mw->module || !mw->model) continue;
            if (mw->model == modelSynTHBloKRandroyd) continue;
            if (safe && mw->model->plugin && mw->model->plugin->slug == "Core") continue;
            out.push_back(mw);
        }
        std::sort(out.begin(), out.end(), [](app::ModuleWidget* a, app::ModuleWidget* b) {
            if (a->box.pos.y != b->box.pos.y) return a->box.pos.y < b->box.pos.y;
            return a->box.pos.x < b->box.pos.x;
        });
        return out;
    }

    void removeMine(int64_t id, history::ComplexAction* h) {
        app::CableWidget* cw = APP->scene->rack->getCable(id);
        if (!cw || !cw->isComplete()) return;
        history::CableRemove* hr = new history::CableRemove;
        hr->setCable(cw);
        h->push(hr);
        APP->scene->rack->removeCable(cw);
        delete cw;
    }

    void doClear(bool alsoLocks = true) {
        SynTHBloKRandroyd* m = rd();
        if (!m || (m->myCables.empty() && (!alsoLocks || m->locks.empty()))) return;
        history::ComplexAction* h = new history::ComplexAction;
        h->name = "RANDROYD clear";
        for (int64_t id : m->myCables) removeMine(id, h);
        m->myCables.clear();
        if (alsoLocks) {
            for (const SynTHBloKRandroyd::Lock& l : m->locks) removeMine(l.cable, h);
            m->locks.clear();
        }
        if (h->isEmpty()) delete h; else APP->history->push(h);
        refreshInfo();
    }

    // smart roll: mutate = share of the last roll replaced (1 = all new)
    void doRoll(float mutate) {
        SynTHBloKRandroyd* m = rd();
        if (!m) return;
        app::RackWidget* rack = APP->scene->rack;
        history::ComplexAction* h = new history::ComplexAction;
        h->name = "RANDROYD roll";
        m->seed = random::u32();
        std::mt19937 rng(m->seed);
        auto chance = [&](float p) { return std::uniform_real_distribution<float>(0.f, 1.f)(rng) < p; };

        std::vector<int64_t> kept;
        for (int64_t id : m->myCables) {
            app::CableWidget* cw = rack->getCable(id);
            if (!cw || !cw->isComplete()) continue;
            if (mutate < 1.f && !chance(mutate)) kept.push_back(id);
            else removeMine(id, h);
        }
        m->myCables = kept;

        int wanted = m->cablesWanted();
        const int fan = 2;
        struct OutJ { app::PortWidget* pw; RdSig sig; };
        struct InJ { app::PortWidget* pw; RdSig sig; bool timing; };
        std::vector<InJ> freeIns;
        std::vector<OutJ> outs;
        for (app::ModuleWidget* mw : targets(m->safe)) {
            if (rdIsMixer(mw)) continue;   // mixers are fed only by the routing step below
            for (app::PortWidget* pw : mw->getInputs()) {
                if (rack->getTopCable(pw)) continue;
                InJ j; j.pw = pw; j.sig = rdInputSig(pw, &j.timing);
                freeIns.push_back(j);
            }
            for (app::PortWidget* pw : mw->getOutputs()) outs.push_back({pw, rdOutputSig(mw, pw)});
        }
        std::shuffle(freeIns.begin(), freeIns.end(), rng);
        std::shuffle(outs.begin(), outs.end(), rng);

        std::map<int64_t, std::set<int64_t>> graph;
        for (app::CableWidget* cw : rack->getCompleteCables())
            if (engine::Cable* c = cw->getCable()) graph[c->outputModule->id].insert(c->inputModule->id);
        auto reaches = [&](int64_t from, int64_t to) {
            std::vector<int64_t> stack{from};
            std::set<int64_t> seen;
            while (!stack.empty()) {
                int64_t n = stack.back(); stack.pop_back();
                if (n == to) return true;
                if (!seen.insert(n).second) continue;
                for (int64_t k : graph[n]) stack.push_back(k);
            }
            return false;
        };

        NVGcolor col = rdz::ramp(m->selTrack >= 0 ? m->selTrack : 0);
        int need = wanted - (int) m->myCables.size();
        for (const InJ& in : freeIns) {
            if (need <= 0) break;
            std::vector<app::PortWidget*> ok;
            for (const OutJ& o : outs) {
                if (o.pw->module == in.pw->module) continue;
                if ((int) rack->getCablesOnPort(o.pw).size() >= fan) continue;
                if (!m->chaos) {
                    if (!rdCompatible(o.sig, in.sig, in.timing)) continue;
                    if (reaches(in.pw->module->id, o.pw->module->id)) continue;
                }
                ok.push_back(o.pw);
            }
            if (ok.empty()) continue;
            app::PortWidget* out = ok[std::uniform_int_distribution<int>(0, (int) ok.size() - 1)(rng)];
            engine::Cable* cable = new engine::Cable;
            cable->inputModule = in.pw->module;
            cable->inputId = in.pw->portId;
            cable->outputModule = out->module;
            cable->outputId = out->portId;
            APP->engine->addCable(cable);
            app::CableWidget* cw = new app::CableWidget;
            cw->setCable(cable);
            cw->color = col;   // the selected track's colour
            rack->addCable(cw);
            graph[out->module->id].insert(in.pw->module->id);
            history::CableAdd* ha = new history::CableAdd;
            ha->setCable(cw);
            h->push(ha);
            m->myCables.push_back(cable->id);
            need--;
        }
        routeToMixer(h, rng);
        m->rolled = true;
        if (h->isEmpty()) delete h; else APP->history->push(h);
        refreshInfo();
    }

    // add one cable (with undo) and return its id
    int64_t addCable(app::PortWidget* out, app::PortWidget* in, NVGcolor col, history::ComplexAction* h) {
        app::RackWidget* rack = APP->scene->rack;
        engine::Cable* cable = new engine::Cable;
        cable->inputModule = in->module;
        cable->inputId = in->portId;
        cable->outputModule = out->module;
        cable->outputId = out->portId;
        APP->engine->addCable(cable);
        app::CableWidget* cw = new app::CableWidget;
        cw->setCable(cable);
        cw->color = col;
        rack->addCable(cw);
        history::CableAdd* ha = new history::CableAdd;
        ha->setCable(cw);
        h->push(ha);
        return cable->id;
    }

    // every sound that ends a chain (an audio out with nothing plugged in) goes to a free mixer
    // channel, stereo where it can; the channel is locked to the selected track
    void routeToMixer(history::ComplexAction* h, std::mt19937& rng) {
        SynTHBloKRandroyd* m = rd();
        app::RackWidget* rack = APP->scene->rack;
        int track = m->selTrack >= 0 ? m->selTrack : 0;
        NVGcolor col = rdz::ramp(track);

        // free channels: number -> {left/mono input, right input}
        struct Ch { app::PortWidget* l = NULL; app::PortWidget* r = NULL; int num = 0; app::ModuleWidget* mixer = NULL; };
        std::vector<Ch> chans;
        std::vector<app::ModuleWidget*> mods = targets(m->safe);
        for (app::ModuleWidget* mw : mods) {
            if (!rdIsMixer(mw)) continue;
            std::map<int, Ch> byNum;
            int solo = 1000;
            for (app::PortWidget* pw : mw->getInputs()) {
                if (!rdIsChannelInput(pw)) continue;
                int n = rdChannelNum(pw);
                if (n == 0) n = solo++;
                Ch& c = byNum[n];
                c.num = n < 1000 ? n : 0; c.mixer = mw;
                if (rdSide(pw) == 1) c.r = pw; else if (!c.l) c.l = pw; else c.r = pw;
            }
            for (auto& kv : byNum) {
                Ch& c = kv.second;
                bool busy = (c.l && rack->getTopCable(c.l)) || (c.r && rack->getTopCable(c.r));
                if (c.l && !busy) chans.push_back(c);
            }
        }
        if (chans.empty()) return;

        // chain ends: audio outs of sound modules with nothing plugged in
        int routed = 0;
        for (app::ModuleWidget* mw : mods) {
            if (rdIsMixer(mw) || rdModuleIsControl(mw)) continue;
            app::PortWidget *L = NULL, *R = NULL, *mono = NULL;
            bool feeds = false;
            for (app::PortWidget* pw : mw->getOutputs()) {
                if (rdOutputSig(mw, pw) != SIG_AUDIO) continue;
                if (!rack->getCablesOnPort(pw).empty()) { feeds = true; continue; }
                int sd = rdSide(pw);
                if (sd < 0 && !L) L = pw; else if (sd > 0 && !R) R = pw; else if (sd == 0 && !mono) mono = pw;
            }
            if (feeds && !L && !mono) continue;   // its sound already goes somewhere
            if (feeds) continue;                   // part of a chain: the chain's end is routed instead
            app::PortWidget* first = L ? L : mono;
            if (!first || chans.empty()) continue;
            Ch c = chans.front();
            chans.erase(chans.begin());
            int64_t id = addCable(first, c.l, col, h);
            m->locks.push_back(SynTHBloKRandroyd::Lock::make(id, c.mixer->module->id, c.l->portId, track, c.num));
            if (c.r) {
                app::PortWidget* second = R ? R : first;   // mono sources feed both sides
                int64_t id2 = addCable(second, c.r, col, h);
                m->locks.push_back(SynTHBloKRandroyd::Lock::make(id2, c.mixer->module->id, c.r->portId, track, c.num));
            }
            routed++;
        }
        if (routed) m->say(string::f("ROUTED %d SOUND%s TO THE MIXER, LOCKED TO TRACK %d", routed, routed > 1 ? "S" : "", track + 1));
    }

    void refreshInfo() {
        SynTHBloKRandroyd* m = rd();
        if (!m) return;
        app::RackWidget* rack = APP->scene->rack;
        info = RdInfo();
        m->myCables.erase(std::remove_if(m->myCables.begin(), m->myCables.end(), [&](int64_t id) {
            app::CableWidget* cw = rack->getCable(id);
            return !cw || !cw->isComplete();
        }), m->myCables.end());
        m->locks.erase(std::remove_if(m->locks.begin(), m->locks.end(), [&](const SynTHBloKRandroyd::Lock& l) {
            app::CableWidget* cw = rack->getCable(l.cable);
            return !cw || !cw->isComplete();   // unplugged by hand: the channel is free again
        }), m->locks.end());
        for (app::ModuleWidget* mw : targets(m->safe)) {
            RdInfo::Mod md;
            md.name = mw->model->name;
            std::vector<app::PortWidget*> ins = mw->getInputs();
            md.ins = (int) ins.size();
            md.outs = (int) mw->getOutputs().size();
            for (app::PortWidget* pw : ins) if (!rack->getTopCable(pw)) info.freeIns++;
            info.mods.push_back(md);
        }
        std::vector<RdInfo::Cab> mine, yours;
        for (app::CableWidget* cw : rack->getCompleteCables()) {
            engine::Cable* c = cw->getCable();
            if (!c || !cw->inputPort || !cw->outputPort) continue;
            app::ModuleWidget* omw = rack->getModule(c->outputModule->id);
            app::ModuleWidget* imw = rack->getModule(c->inputModule->id);
            if (!omw || !imw || omw == this || imw == this) continue;
            RdInfo::Cab cb;
            cb.from = shortModule(omw) + " " + portName(cw->outputPort);
            cb.to = shortModule(imw) + " " + portName(cw->inputPort);
            cb.mine = std::find(m->myCables.begin(), m->myCables.end(), c->id) != m->myCables.end();
            for (const SynTHBloKRandroyd::Lock& l : m->locks) if (l.cable == c->id) { cb.mine = true; cb.lockTrack = l.track; }
            (cb.mine ? mine : yours).push_back(cb);
        }
        info.mine = (int) mine.size();
        info.cabs = mine;
        info.cabs.insert(info.cabs.end(), yours.begin(), yours.end());
    }


    // =======================================================================
    // GENERATOR: builds instrument rows. Row 1 under RANDROYD = MASTER (COMPOSER clock, mixer, audio);
    // the rows under it = tracks T1..T8 (T1 DRUMS first; the other instruments follow).
    // Everything runs from click events (not inside step()), as adding / removing modules needs.
    // =======================================================================
    static float rowH() { return RACK_GRID_HEIGHT; }

    app::ModuleWidget* findMw(int64_t id) { return id >= 0 ? APP->scene->rack->getModule(id) : NULL; }

    // set the first param whose name starts with `name` (case-insensitive)
    static bool setP(engine::Module* mod, const char* name, float v) {
        std::string want = string::lowercase(name);
        for (size_t i = 0; i < mod->paramQuantities.size(); i++) {   // exact name first ("Key" is not "Keytrack amount")
            engine::ParamQuantity* pq = mod->paramQuantities[i];
            if (pq && string::lowercase(pq->name) == want) { pq->setValue(v); return true; }
        }
        for (size_t i = 0; i < mod->paramQuantities.size(); i++) {
            engine::ParamQuantity* pq = mod->paramQuantities[i];
            if (pq && string::lowercase(pq->name).compare(0, want.size(), want) == 0) { pq->setValue(v); return true; }
        }
        return false;
    }
    // a port by name prefix (on-panel jacks only)
    static app::PortWidget* port(app::ModuleWidget* mw, bool out, const char* name) {
        std::string want = string::lowercase(name);
        for (app::PortWidget* pw : out ? mw->getOutputs() : mw->getInputs()) {
            engine::PortInfo* info = pw->getPortInfo();
            if (info && string::lowercase(info->name).compare(0, want.size(), want) == 0) return pw;
        }
        return NULL;
    }

    // ---- WHERE MODULES LIVE. SynTHBloK is sold as the MTX pack (this plugin, slug "SynTHBloK") plus singles, each
    // its own plugin (see tools/lineup.json). Code asks for ("SynTHBloK", model) and locate() finds the real plugin.
    // It also translates tracks saved before the split, and before the VCV Library rename, when every module was in
    // "SynTHBloK" and the slugs had a "SynTHBloK" prefix (SynTHBloKDrum -> Drum, SynTHBloK -> Quad).
    static const std::map<std::string, std::string>& singles() {   // model -> single plugin
        static const std::map<std::string, std::string> S = {
            {"Quad", "SynTHBloK-Quad"}, {"Poly", "SynTHBloK-Poly"}, {"Stereo", "SynTHBloK-StereoPhaser"},
            {"DualFilter", "SynTHBloK-DualFilter"}, {"NoizeeFlagshipFilter", "SynTHBloK-MorphFilter"},
            {"Resonator", "SynTHBloK-Resonator"}, {"FX", "SynTHBloK-CharacterDelay"}, {"GrainForge", "SynTHBloK-GrainForge"},
            {"BlokMaster", "SynTHBloK-BloKMaster"}, {"Kick", "SynTHBloK-Kick"}, {"SnareHat", "SynTHBloK-SnareHat"},
            {"Euclid", "SynTHBloK-Euclid"}, {"JamProbe", "SynTHBloK-JamProbe"},
            {"Mix24", "SynTHBloK-MixFire"}, {"Mix12", "SynTHBloK-MixFire"}, {"Mix8", "SynTHBloK-MixFire"},
            {"Player", "SynTHBloK-PlayerPlus"}, {"PlayerView", "SynTHBloK-PlayerPlus"},
        };
        return S;
    }
    static void locate(std::string& plugin, std::string& model) {
        static const std::map<std::string, std::string> OLD = {
            {"SynTHBloK", "Quad"}, {"SynTHBloKDrum", "Drum"}, {"SynTHBloKDualFilter", "DualFilter"}, {"SynTHBloKDuo", "Duo"},
            {"SynTHBloKEcho", "Echo"}, {"SynTHBloKEuclid", "Euclid"}, {"SynTHBloKFX", "FX"}, {"SynTHBloKGrainForge", "GrainForge"},
            {"SynTHBloKJam", "Jam"}, {"SynTHBloKJamProbe", "JamProbe"}, {"SynTHBloKKick", "Kick"}, {"SynTHBloKLoop", "Loop"},
            {"SynTHBloKMatrix8", "Matrix8"}, {"SynTHBloKMatrixForge", "MatrixForge"}, {"SynTHBloKMatrixFx", "MatrixFx"},
            {"SynTHBloKMatrixSong", "MatrixSong"}, {"SynTHBloKMix12", "Mix12"}, {"SynTHBloKMix24", "Mix24"}, {"SynTHBloKMix8", "Mix8"},
            {"SynTHBloKMtxQuad", "MtxQuad"}, {"SynTHBloKPlayer", "Player"}, {"SynTHBloKPlayerView", "PlayerView"},
            {"SynTHBloKPoly", "Poly"}, {"SynTHBloKRandroyd", "Randroyd"}, {"SynTHBloKResonator", "Resonator"},
            {"SynTHBloKSnareHat", "SnareHat"}, {"SynTHBloKStereo", "Stereo"},
        };
        if (plugin != "SynTHBloK") return;
        auto o = OLD.find(model);
        if (o != OLD.end()) model = o->second;
        auto s = singles().find(model);
        if (s != singles().end()) plugin = s->second;
    }
    static plugin::Model* findModel(const char* plugin, const char* model) {
        if (!plugin || !model) return NULL;
        std::string p = plugin, md = model;
        locate(p, md);
        return plugin::getModel(p, md);
    }
    static std::string newSlug(const char* plugin, const char* model) {   // the module slug a saved track means
        std::string p = plugin ? plugin : "SynTHBloK", md = model ? model : "";
        locate(p, md);
        return md;
    }
    // is this SynTHBloK module installed? (singles are bought separately)
    static bool have(const char* model) { return findModel("SynTHBloK", model) != NULL; }
    // the product to buy for a missing module, for messages
    static std::string productFor(const char* model) {
        static const std::map<std::string, std::string> N = {
            {"Quad", "QUAD"}, {"Poly", "POLY"}, {"Stereo", "STEREO PHASER"}, {"DualFilter", "DUAL FILTER"},
            {"NoizeeFlagshipFilter", "MORPH FILTER"}, {"Resonator", "RESONATOR"}, {"FX", "CHARACTER DELAY"},
            {"GrainForge", "GRAIN-FORGE"}, {"BlokMaster", "BLOKMASTER"}, {"Kick", "KICK"}, {"SnareHat", "SNARE/HAT"},
            {"Euclid", "EUCLID"}, {"Mix8", "MIX FIRE EDITION"}, {"Mix12", "MIX FIRE EDITION"}, {"Mix24", "MIX FIRE EDITION"},
        };
        auto it = N.find(model);
        return it != N.end() ? it->second : std::string(model);
    }
    // the SynTHBloK products missing for these modules ("" when all are installed), e.g. "KICK, EUCLID"
    static std::string missing(std::initializer_list<const char*> models) {
        std::vector<std::string> out;
        for (const char* md : models)
            if (!have(md)) {
                std::string n = productFor(md);
                if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
            }
        std::string s;
        for (size_t i = 0; i < out.size(); i++) s += (i ? ", " : "") + out[i];
        return s;
    }

    // create a module at (x, y) px, with params and data set before it starts running; undoable
    app::ModuleWidget* spawn(const char* plugin, const char* model, float& x, float y, history::ComplexAction* h,
                             std::function<void(engine::Module*)> setup = nullptr) {
        plugin::Model* mdl = findModel(plugin, model);
        if (!mdl) return NULL;   // a single the user doesn't own: callers build without it
        engine::Module* mod = mdl->createModule();
        if (setup) setup(mod);
        APP->engine->addModule(mod);
        app::ModuleWidget* mw = mdl->createModuleWidget(mod);
        APP->scene->rack->addModule(mw);
        APP->scene->rack->setModulePosNearest(mw, Vec(x, y));
        x = mw->box.pos.x + mw->box.size.x;
        history::ModuleAdd* ha = new history::ModuleAdd;
        ha->name = "RANDROYD add module";
        ha->setModule(mw);
        h->push(ha);
        return mw;
    }

    void wire(app::PortWidget* out, app::PortWidget* in, NVGcolor col, history::ComplexAction* h) {
        if (!out || !in || APP->scene->rack->getTopCable(in)) return;
        addCable(out, in, col, h);
    }

    // the MASTER row: MTX COMPOSER (tempo, song cycle A-D = the 4 patterns), MIX-8 (channel n = track n),
    // and an Audio module (yours if the patch has one with free inputs)
    bool ensureMaster(history::ComplexAction* h, int genre, int bpm) {
        SynTHBloKRandroyd* m = rd();
        app::RackWidget* rack = APP->scene->rack;
        if (findMw(m->composerId) && findMw(m->mixerId)) { ensureMastering(h); return true; }
        float x = box.pos.x, y = box.pos.y + rowH();
        m->masterMods.clear();
        app::ModuleWidget* song = spawn("SynTHBloK", "MatrixSong", x, y, h, [&](engine::Module* mod) {
            setP(mod, "Tempo", (float) bpm);
            setP(mod, "Song cycle", 3.f);       // A-D
            setP(mod, "Bars each section", 1.f);  // 2 bars
            setP(mod, "Run", 1.f);
            json_t* d = json_object();
            json_t* cs = json_array();
            for (int sc = 0; sc < 8; sc++) {
                json_t* a = json_array();
                for (int r = 0; r < 8; r++) {
                    json_t* v = json_array();
                    json_array_append_new(v, json_integer(sc % 4));
                    json_array_append_new(v, json_false());
                    json_array_append_new(v, json_integer(0));
                    json_array_append_new(v, json_real(1.0));
                    json_array_append_new(a, v);
                }
                json_array_append_new(cs, a);
            }
            json_object_set_new(d, "cells", cs);
            json_object_set_new(d, "section", json_integer(0));
            mod->dataFromJson(d);
            json_decref(d);
        });
        app::ModuleWidget* mix = spawn("SynTHBloK", "Mix8", x, y, h, [&](engine::Module* mod) {
            setP(mod, "Master level", 0.9f);
        });
        if (!song || !mix) {
            m->say(!mix && !have("Mix8") ? "RANDROYD NEEDS MIX FIRE EDITION (ITS MIXER): INSTALL IT FROM THE VCV LIBRARY"
                                         : "COULD NOT BUILD THE MASTER ROW");
            return false;
        }
        m->composerId = song->module->id;
        m->mixerId = mix->module->id;
        m->masterMods = {song->module->id, mix->module->id};

        // audio: yours (free inputs), or a new one
        app::ModuleWidget* audio = NULL;
        for (app::ModuleWidget* mw : rack->getModules())
            if (mw->model && mw->model->plugin && mw->model->plugin->slug == "Core" && mw->model->slug.compare(0, 14, "AudioInterface") == 0) {
                std::vector<app::PortWidget*> ins = mw->getInputs();
                if (ins.size() >= 2 && !rack->getTopCable(ins[0]) && !rack->getTopCable(ins[1])) { audio = mw; break; }
            }
        if (!audio) {
            audio = spawn("Core", "AudioInterface2", x, y, h);
            if (audio) { m->masterMods.push_back(audio->module->id); m->say("PICK YOUR AUDIO DEVICE ON THE NEW AUDIO MODULE"); }
        }
        if (audio) m->audioId = audio->module->id;
        ensureMastering(h);
        return true;
    }

    // BloKMaster on the master bus, always: MIX-8 master -> BloKMaster -> audio. A rack built before it
    // was added gets it slotted in between (the mixer's cables to the audio module move to BloKMaster).
    void ensureMastering(history::ComplexAction* h) {
        SynTHBloKRandroyd* m = rd();
        app::RackWidget* rack = APP->scene->rack;
        app::ModuleWidget* mix = findMw(m->mixerId);
        app::ModuleWidget* audio = findMw(m->audioId);
        if (!mix || findMw(m->masterId)) return;
        app::PortWidget* ml = port(mix, true, "Master left");
        app::PortWidget* mr = port(mix, true, "Master right");
        // the mixer's own cables straight to the audio module go (BloKMaster sits between them now)
        for (app::PortWidget* pw : {ml, mr}) {
            if (!pw) continue;
            for (app::CableWidget* cw : rack->getCablesOnPort(pw)) {
                engine::Cable* cb = cw->getCable();
                if (!cb || !audio || cb->inputModule != audio->module) continue;
                history::CableRemove* hr = new history::CableRemove;
                hr->setCable(cw);
                h->push(hr);
                rack->removeCable(cw);
                delete cw;
            }
        }
        float mx = mix->box.pos.x + mix->box.size.x;   // right of the mixer, before the audio module
        app::ModuleWidget* mst = spawn("SynTHBloK", "BlokMaster", mx, mix->box.pos.y, h, [&](engine::Module* mod) {
            // a gentle, safe master: glue, mono low end, a limiter just under 0 dB
            setP(mod, "Compressor threshold", -14.f);
            setP(mod, "Compressor ratio", 2.f);
            setP(mod, "Compressor attack", 20.f);
            setP(mod, "Compressor release", 150.f);
            setP(mod, "Compressor auto makeup gain", 1.f);
            setP(mod, "Punch", 0.15f);
            setP(mod, "Mono bass below", 110.f);
            setP(mod, "Limiter threshold", -2.f);
            setP(mod, "Limiter ceiling", -0.3f);
        });
        if (!mst) { m->say("BLOKMASTER NOT FOUND: MIXER GOES STRAIGHT TO AUDIO"); }
        else {
            m->masterId = mst->module->id;
            m->masterMods.push_back(mst->module->id);
            wire(ml, port(mst, false, "Left in"), nvgRGB(0x00, 0xb5, 0x6e), h);
            wire(mr, port(mst, false, "Right in"), nvgRGB(0x36, 0x95, 0xef), h);
        }
        if (audio) {
            std::vector<app::PortWidget*> ins = audio->getInputs();
            if (ins.size() >= 2) {
                wire(mst ? port(mst, true, "Left out") : ml, ins[0], nvgRGB(0x00, 0xb5, 0x6e), h);
                wire(mst ? port(mst, true, "Right out") : mr, ins[1], nvgRGB(0x36, 0x95, 0xef), h);
            }
        }
    }

    // COMPOSER's clock and reset into a module's own jacks (not RUN: MTX modules toggle on a RUN trigger)
    void clockFrom(app::ModuleWidget* mw, history::ComplexAction* h) {
        app::ModuleWidget* song = findMw(rd()->composerId);
        if (!song || !mw) return;
        wire(port(song, true, "Clock"), port(mw, false, "Clock"), nvgRGB(0x94, 0x37, 0xff), h);
        wire(port(song, true, "Reset"), port(mw, false, "Reset"), nvgRGB(0x36, 0x95, 0xef), h);
    }

    // a track's sound into its own mixer channel (channel = track number), locked to the track
    void toMixer(int track, app::PortWidget* l, app::PortWidget* r, float level, float pan, float send, history::ComplexAction* h, int chan = -1) {
        SynTHBloKRandroyd* m = rd();
        app::ModuleWidget* mix = findMw(m->mixerId);
        if (!mix || !l) return;
        int ch = chan > 0 ? chan : track + 1;
        app::PortWidget* il = port(mix, false, string::f("CH %d L", ch).c_str());
        app::PortWidget* ir = port(mix, false, string::f("CH %d R", ch).c_str());
        NVGcolor col = rdz::ramp(track);
        if (il && !APP->scene->rack->getTopCable(il)) {
            int64_t id = addCable(l, il, col, h);
            m->locks.push_back(SynTHBloKRandroyd::Lock::make(id, mix->module->id, il->portId, track, ch));
        }
        if (ir && !APP->scene->rack->getTopCable(ir)) {
            int64_t id = addCable(r ? r : l, ir, col, h);
            m->locks.push_back(SynTHBloKRandroyd::Lock::make(id, mix->module->id, ir->portId, track, ch));
        }
        setP(mix->module, string::f("CH %d level", ch).c_str(), level);
        setP(mix->module, string::f("CH %d pan", ch).c_str(), pan);
        setP(mix->module, string::f("CH %d FX send", ch).c_str(), send);
    }

    // ---- drums: the genre's groove on patterns 1-4 ----
    // 1 the groove, 2 + ghost hats, 3 a variation (moved claps / open hats, accents), 4 the fill.
    // variety 0..1: how far patterns 2-3 stray from the groove
    static std::vector<std::string> drumPatterns(int g, std::mt19937& rng, float variety) {
        const rdz::GenreDrums& G = rdz::GDRUMS[g];
        auto rnd = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
        auto chance = [&](float p) { return std::uniform_real_distribution<float>(0.f, 1.f)(rng) < p; };
        std::vector<std::string> pats;
        for (int p = 0; p < 4; p++) {
            std::string rows[8];
            for (int r = 0; r < 8; r++) rows[r] = G.rows[r];
            if (p == 1) {   // ghost hats
                for (int k = 0; k < 1 + (int) (variety * 2); k++) { int i = rnd(8); if (rows[3][i] == '0') rows[3][i] = '1'; }
            }
            if (p == 2) {   // variation: a perc hit moves, an open hat or tom joins, accents shift (the kick stays)
                for (int k = 0; k < 1 + (int) (variety * 3); k++) {
                    int r = 2 + rnd(6), i = rnd(8);
                    if (chance(0.5f)) rows[r][i] = rows[r][i] == '0' ? '1' : '0';
                    else if (rows[r][i] != '0') rows[r][i] = rows[r][i] == '2' ? '1' : '2';
                }
            }
            if (p == 3)   // the fill
                for (int r = 0; r < 8; r++)
                    for (int i = 0; i < 8; i++) rows[r][i] = std::max(rows[r][i], G.fill[r][i]);
            std::string s;
            for (int r = 0; r < 8; r++) s += rows[r];
            pats.push_back(s);
        }
        for (int p = 4; p < 8; p++) pats.push_back(pats[p % 4]);   // 5-8 copy 1-4, ready to edit
        return pats;
    }
    static std::vector<std::string> percPatterns(int g, std::mt19937& rng, float variety) {
        const rdz::GenreDrums& G = rdz::GDRUMS[g];
        auto rnd = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
        auto chance = [&](float p) { return std::uniform_real_distribution<float>(0.f, 1.f)(rng) < p; };
        auto euclid = [&](int k, int rot) {
            std::string r(8, '0');
            for (int i = 0; i < 8; i++) if (((i * k) % 8) < k) r[(i + rot) % 8] = chance(0.25f) ? '2' : '1';
            return r;
        };
        float d = 0.3f + 0.5f * variety;
        std::string base[8];
        base[0] = base[1] = base[4] = "00000000";                       // kick, snare, open hat: the main kit has them
        base[2] = chance(0.35f) ? "00000001" : "00000000";               // a clap pickup now and then
        base[3] = "00000000";                                             // shaker: the main hats' gaps
        for (int i = 0; i < 8; i++) if (G.rows[3][i] == '0' && chance(0.45f + 0.4f * d)) base[3][i] = '1';
        for (int r = 5; r < 8; r++) base[r] = chance(0.35f + 0.5f * d) ? euclid(1 + rnd(r == 7 ? 4 : 3), rnd(8)) : std::string("00000000");
        std::vector<std::string> pats;
        for (int p = 0; p < 4; p++) {
            std::string rows[8];
            for (int r = 0; r < 8; r++) rows[r] = base[r];
            if (p == 1 || p == 2)   // variations: a hit moves on one perc row
                for (int k = 0; k < 1 + (int) (variety * 2); k++) { int r = 5 + rnd(3), i = rnd(8); rows[r][i] = rows[r][i] == '0' ? '1' : '0'; }
            if (p == 3)   // fill: toms roll in
                for (int i = 4; i < 8; i++) if (chance(0.6f)) rows[5][i] = '1';
            std::string out;
            for (int r = 0; r < 8; r++) out += rows[r];
            pats.push_back(out);
        }
        for (int p = 4; p < 8; p++) pats.push_back(pats[p % 4]);
        return pats;
    }
    static json_t* patternsJson(const std::vector<std::string>& pats) {
        json_t* d = json_object();
        json_t* arr = json_array();
        for (const std::string& p : pats) json_array_append_new(arr, json_string(p.c_str()));
        json_object_set_new(d, "patterns", arr);
        return d;
    }

    bool buildDrums(int track, int g, int bpm, std::mt19937& rng, history::ComplexAction* h, int& kit, bool perc = false, bool hybrid = false, bool resKick = false) {
        SynTHBloKRandroyd* m = rd();
        const rdz::GenreDrums& G = rdz::GDRUMS[g];
        const std::vector<int>& kits = rdz::KITS[g];
        kit = kits[std::uniform_int_distribution<int>(0, (int) kits.size() - 1)(rng)];
        if (perc) kit = percKit(g, rng);
        float x = box.pos.x, y = box.pos.y + (2 + track) * rowH();
        float variety = m->params[SynTHBloKRandroyd::DENSITY_PARAM].getValue();
        std::vector<std::string> pats = perc ? percPatterns(g, rng, variety) : drumPatterns(g, rng, variety);
        app::ModuleWidget* d = spawn("SynTHBloK", "Drum", x, y, h, [&](engine::Module* mod) {
            setP(mod, "Internal tempo", (float) bpm);
            setP(mod, "FORGE FX", G.glitch ? 1.f : 0.f);
            setP(mod, "Accent depth", 0.5f);
            setP(mod, "Master volume", perc ? 0.55f : 0.75f);
            float fx = m->params[SynTHBloKRandroyd::FX_PARAM].getValue() * 2.f;   // FX knob: 0 dry .. 1 double the genre's
            setP(mod, "Delay", clamp(0.25f * G.dly * fx, 0.f, 1.f));
            setP(mod, "Space", clamp((perc ? 0.55f : 0.35f) * G.rev * fx + (perc ? 0.1f : 0.f), 0.f, 1.f));
            json_t* dj = patternsJson(pats);
            mod->dataFromJson(dj);
            json_decref(dj);
        });
        if (!d) return false;
        randroydDrumKit(d->module, kit);
        m->trackMods[track] = {d->module->id};
        clockFrom(d, h);
        toMixer(track, port(d, true, "Left"), port(d, true, "Right"), perc ? 0.7f : 0.95f, perc ? 0.15f : 0.f, (perc ? 0.3f : 0.1f) * G.rev, h);
        if (hybrid && !perc) {   // the kick row's trigger plays an outside Kick; MTX-DRUM's own kick stays silent
            app::ModuleWidget* kick = spawn("SynTHBloK", "Kick", x, y, h, [&](engine::Module* mod) { rigTone(mod, NULL, g, rng); });
            if (kick) {
                wire(port(d, true, "Row 1 trigger"), port(kick, false, "Trigger"), rdz::ramp(track), h);
                randroydDrumSilence(d->module, 1);
                app::ModuleWidget* rs = resKick ? kickRes(kick, x, y, track, rng, h) : NULL;
                if (rs) toMixer(track, port(rs, true, "Out L"), port(rs, true, "Out R"), 0.95f, 0.f, 0.f, h, 7);
                else toMixer(track, port(kick, true, "Kick"), NULL, 0.95f, 0.f, 0.f, h, 7);
                m->trackMods[track].push_back(kick->module->id);
                if (rs) m->trackMods[track].push_back(rs->module->id);
                m->trackRecipe[track] = 11;
                m->trackFlags[track] = rs ? 1 : 0;
            }
        }
        return true;
    }
    // PERC plays a different kit from the main drums
    int percKit(int g, std::mt19937& rng) {
        const std::vector<int>& kits = rdz::KITS[g];
        int main = rd()->trackKit[0];
        std::vector<int> other;
        for (int k : kits) if (k != main) other.push_back(k);
        if (other.empty()) for (int k = 0; k < 9; k++) if (k != main) other.push_back(k);
        return other[std::uniform_int_distribution<int>(0, (int) other.size() - 1)(rng)];
    }


    // =======================================================================
    // HARMONY + MELODIC TRACKS (MTX QUAD): BASS / ACID, LEAD, CHORDS
    // The rack has one key, scale and 4-chord progression. Patterns 1-4 of every instrument follow the
    // chords, and COMPOSER cycles A-D, so all rows change chord together. Grid rows = scale degrees 0-7.
    // =======================================================================
    void ensureHarmony(int g, std::mt19937& rng) {
        SynTHBloKRandroyd* m = rd();
        if (m->hKey >= 0) return;
        const rdz::GenreHarm& H = rdz::GHARM[g];
        auto pick3 = [&](const int* a) { return a[std::uniform_int_distribution<int>(0, 2)(rng)]; };
        m->hScale = pick3(H.scales);
        m->hKey = pick3(H.keys);
        int pr = std::uniform_int_distribution<int>(0, 1)(rng);
        for (int i = 0; i < 4; i++) m->hProg[i] = H.prog[pr][i];
    }
    int scaleLen() { return (int) rdz::SCALE_STEPS[clamp(rd()->hScale, 0, 7)].size(); }
    // the chord on degree root as grid degrees 0..7: root, 3rd, 5th (pentatonic: root, +2, +3)
    std::vector<int> triad(int root) {
        int n = scaleLen();
        int st[3] = {0, 2, n == 7 ? 4 : 3};
        std::vector<int> out;
        for (int k = 0; k < 3; k++) { int d = root + st[k]; while (d > 7) d -= n; out.push_back(d); }
        return out;
    }
    // 8 steps of degree masks: chord tones on the beats, steps between (a melody, not random notes)
    std::vector<int> leadLine(std::mt19937& rng, int root, const char* rhythm) {
        std::vector<int> tones = triad(root), out;
        auto rnd = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
        int cur = tones[rnd(3)];
        static const int moves[5] = {-1, 1, -2, 2, 0};
        for (int c = 0; c < 8; c++) {
            if (rhythm[c] == '0') { out.push_back(0); continue; }
            if (c % 4 == 0) {   // on the beat: the nearest chord tone
                int best = tones[0]; float bd = 99.f;
                for (int t : tones) { float d = std::abs(t - cur) + std::uniform_real_distribution<float>(0.f, 1.5f)(rng); if (d < bd) { bd = d; best = t; } }
                cur = best;
            }
            else cur = clamp(cur + moves[rnd(5)], 0, 7);
            out.push_back(1 << cur);
        }
        return out;
    }
    // roots and fifths, an octave up now and then; step 1 always the root
    std::vector<int> bassLine(std::mt19937& rng, int root, const char* rhythm) {
        std::vector<int> tones = triad(root), out;
        int n = scaleLen();
        int lo = tones[0] < n ? tones[0] : tones[0] - n;
        for (int c = 0; c < 8; c++) {
            if (rhythm[c] == '0') { out.push_back(0); continue; }
            int d = lo;
            if (c > 0) {
                int opts[4] = {lo, lo, tones[2] >= lo ? tones[2] : lo, std::min(7, lo + n)};
                d = opts[std::uniform_int_distribution<int>(0, 3)(rng)];
            }
            out.push_back(1 << d);
        }
        return out;
    }
    // whole chords on the lead rhythm, thinned to 4 hits at most
    std::vector<int> stabLine(int root, const char* rhythm) {
        std::vector<int> tones = triad(root), out;
        int mask = 0;
        for (int t : tones) mask |= 1 << t;
        int hits = 0;
        for (int c = 0; c < 8; c++) hits += rhythm[c] == '1';
        const char* r = hits <= 4 ? rhythm : "10010010";
        for (int c = 0; c < 8; c++) out.push_back(r[c] == '1' ? mask : 0);
        return out;
    }
    // a 303-style line: root-heavy, accents, slides
    std::vector<int> acidLine(std::mt19937& rng, int root, const char* rhythm) {
        std::vector<int> tones = triad(root), out;
        static const int pickIdx[4] = {0, 0, 1, 2};
        for (int c = 0; c < 8; c++)
            out.push_back(rhythm[c] == '1' ? 1 << tones[pickIdx[std::uniform_int_distribution<int>(0, 3)(rng)]] : 0);
        out[0] = 1 << tones[0];
        return out;
    }

    // MTX QUAD notes + step sliders (ACC CUT GATE GLIDE PROB RATCH OCT RES) for patterns 1-4; 5-8 copy them.
    // keepFirst: pattern 1 stays as the module has it (MUTATE)
    std::vector<std::vector<int>> roleLines(int role, int g, std::mt19937& rng) {
        const rdz::GenreHarm& H = rdz::GHARM[g];
        SynTHBloKRandroyd* m = rd();
        std::vector<std::vector<int>> lines;
        for (int p = 0; p < 4; p++) {
            int root = m->hProg[p];
            if (role == 2) lines.push_back(leadLine(rng, root, H.lead));
            else if (role == 1) lines.push_back(acidLine(rng, root, H.lead));
            else lines.push_back(bassLine(rng, root, H.bass));
        }
        if (role == 2 || role == 1) {   // A B A' B': pattern 3 repeats pattern 1 on its own chord
            int shift = m->hProg[2] - m->hProg[0];
            std::vector<int> rep;
            for (int v : lines[0]) {
                int out = 0;
                for (int b = 0; b < 8; b++) if (v >> b & 1) out |= 1 << clamp(b + shift, 0, 7);
                rep.push_back(out);
            }
            lines[2] = rep;
        }
        return lines;
    }
    json_t* quadData(int role, int g, std::mt19937& rng, json_t* keepFirst = NULL) {
        const rdz::GenreHarm& H = rdz::GHARM[g];
        SynTHBloKRandroyd* m = rd();
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        json_t* d = json_object();
        json_t* notes = json_array();
        json_t* steps = json_array();
        std::vector<std::vector<int>> lines;
        for (int p = 0; p < 4; p++) {
            int root = m->hProg[p];
            if (role == 4) {   // pad: the chord held from step 1, nothing else
                std::vector<int> st = stabLine(root, H.lead), pad(8, 0);
                for (int v : st) if (v) { pad[0] = v; break; }
                lines.push_back(pad);
            }
            else if (role == 3) lines.push_back(stabLine(root, H.lead));
            else if (role == 2) lines.push_back(leadLine(rng, root, H.lead));
            else if (role == 1) lines.push_back(acidLine(rng, root, H.lead));
            else lines.push_back(bassLine(rng, root, H.bass));
        }
        // repetition makes it music: pattern 3 repeats pattern 1's line moved to its own chord
        // (A B A' B'), so the phrase is heard twice
        if (role == 2 || role == 1) {
            int shift = m->hProg[2] - m->hProg[0];
            std::vector<int> rep;
            for (int v : lines[0]) {
                int out = 0;
                for (int b = 0; b < 8; b++) if (v >> b & 1) out |= 1 << clamp(b + shift, 0, 7);
                rep.push_back(out);
            }
            lines[2] = rep;
        }
        const float gate = role == 4 ? 1.f : role == 3 ? 0.35f : (role == 2 ? 0.55f : (role == 1 ? 0.5f : 0.45f));
        for (int p = 0; p < 8; p++) {
            const std::vector<int>& line = lines[p % 4];
            std::string hx;
            if (p == 0 && keepFirst) {
                const char* old = json_string_value(json_array_get(json_object_get(keepFirst, "notes"), 0));
                hx = old ? old : "";
            }
            if (hx.empty()) for (int v : line) hx += string::f("%02x", v);
            json_array_append_new(notes, json_string(hx.c_str()));
            for (int b = 0; b < 8; b++)
                for (int c = 0; c < 8; c++) {
                    bool on = line[c] != 0;
                    float v;
                    if (b == 0) v = on && (c % 4 == 0 || (role == 1 && uni(0, 1) < 0.3f)) ? 1.f : 0.f;   // accent
                    else if (b == 1) v = 0.5f + uni(-0.08f, 0.08f);                                     // cutoff
                    else if (b == 2) v = gate + uni(-0.1f, 0.1f);                                        // gate length
                    else if (b == 3) v = (role == 1 && uni(0, 1) < 0.3f) ? 0.55f : 0.f;                  // glide
                    else if (b == 4) v = 1.f;                                                            // probability
                    else if (b == 5) v = 0.f;                                                            // ratchet
                    else if (b == 6) v = (role == 1 && uni(0, 1) < 0.15f) ? 0.75f : 0.5f;                 // octave
                    else v = 0.5f;                                                                       // resonance
                    json_array_append_new(steps, json_real(v));
                }
        }
        json_object_set_new(d, "notes", notes);
        json_object_set_new(d, "steps", steps);
        json_object_set_new(d, "justChords", json_true());
        return d;
    }

    static const char* roleName(int role) { static const char* n[5] = {"BASS", "ACID", "LEAD", "CHORDS", "PAD"}; return n[clamp(role, 0, 4)]; }

    // a synth layer's tone. vary = ROLL: a new sound around the role's home (still in the genre's character)
    void quadTone(engine::Module* mod, int role, int g, std::mt19937& rng, bool vary) {
        const rdz::GenreHarm& H = rdz::GHARM[g];
        const rdz::GenreDrums& GD = rdz::GDRUMS[g];
        float br = H.bright, drv = GD.drive;
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        auto v = [&](float base, float spread, float lo = 0.f, float hi = 1.f) { return clamp(vary ? base + uni(-spread, spread) : base, lo, hi); };
        bool slow = rdz::GENRES[g].bpmHi < 100;
        if (role == 4) {   // pad: slow swell, long tail, soft and wide
            setP(mod, "Filter cutoff", v(0.28f + 0.1f * br, 0.08f, 0.1f, 0.5f));
            setP(mod, "Resonance", v(0.1f, 0.08f));
            setP(mod, "Envelope amount", v(0.12f, 0.08f));
            setP(mod, "Attack", v(0.55f, 0.15f));
            setP(mod, "Decay", v(0.7f, 0.15f));
            setP(mod, "Sustain", v(0.85f, 0.1f));
            setP(mod, "Release", v(0.8f, 0.12f));
            setP(mod, "Glide", 0.f);
            setP(mod, "Triangle level", v(0.5f, 0.2f));
            setP(mod, "Saw/Blade level", v(0.45f, 0.2f));
            setP(mod, "Square level", vary ? uni(0.f, 0.2f) : 0.f);
            setP(mod, "VCA out level", 0.7f);
            setP(mod, "Sub level", 0.f);
            setP(mod, "Mix out level", 0.9f);
            setP(mod, "Accent amount", 0.f);
            setP(mod, "Unison spread", v(0.55f, 0.2f));
            setP(mod, "Filter drive", v(0.1f, 0.08f));
            return;
        }
        setP(mod, "Filter cutoff", v((role == 0 ? 0.25f : role == 1 ? 0.22f : role == 2 ? 0.4f : 0.38f) + 0.15f * br, 0.13f, 0.08f, 0.85f));
        setP(mod, "Resonance", role == 1 ? v(0.5f, 0.2f, 0.f, 0.8f) : (vary ? uni(0.05f, 0.4f) : uni(0.1f, 0.3f)));
        setP(mod, "Envelope amount", v(role == 1 ? 0.55f : role == 3 ? 0.25f : 0.35f, 0.18f));
        setP(mod, "Attack", role == 3 && slow ? v(0.25f, 0.15f) : v(0.02f, 0.02f));
        setP(mod, "Decay", v(role == 3 ? 0.5f : role == 1 ? 0.25f : 0.35f, 0.15f, 0.08f, 0.9f));
        setP(mod, "Sustain", v(role >= 2 ? 0.6f : 0.4f, 0.2f));
        setP(mod, "Release", v(role == 3 ? 0.6f : 0.25f, 0.15f, 0.05f, 0.9f));
        setP(mod, "Glide", role == 1 ? v(0.35f, 0.15f) : role == 2 ? v(0.1f, 0.1f) : 0.f);
        // oscillator mix: ROLL deals a new blend, kept at the same loudness
        float tri = 0.3f, saw = role == 3 ? 0.45f : 0.6f, sq = role == 1 ? 0.3f : 0.2f;
        if (vary) {
            tri = uni(0.f, 0.7f); saw = uni(0.15f, 0.8f); sq = uni(0.f, 0.6f);
            float sum = tri + saw + sq, want = role == 3 ? 0.95f : 1.1f;
            tri *= want / sum; saw *= want / sum; sq *= want / sum;
        }
        setP(mod, "Triangle level", tri);
        setP(mod, "Saw/Blade level", saw);
        setP(mod, "Square level", sq);
        setP(mod, "VCA out level", 0.7f);
        setP(mod, "Sub level", role == 0 ? v(0.35f, 0.15f) : 0.f);
        setP(mod, "Mix out level", 0.9f);
        setP(mod, "Accent amount", v(role == 1 ? 0.4f : 0.2f, 0.15f));
        setP(mod, "Unison spread", vary ? uni(0.f, role == 3 ? 0.5f : 0.35f) : (role == 3 ? 0.3f : 0.2f));
        setP(mod, "Filter drive", v(0.2f + 0.4f * drv, 0.15f));
    }

    bool buildQuad(int track, int g, std::mt19937& rng, history::ComplexAction* h, int role, int recipe = 0) {
        SynTHBloKRandroyd* m = rd();
        const rdz::GenreDrums& GD = rdz::GDRUMS[g];
        float x = box.pos.x, y = box.pos.y + (2 + track) * rowH();
        json_t* data = quadData(role, g, rng);
        app::ModuleWidget* q = spawn("SynTHBloK", "MtxQuad", x, y, h, [&](engine::Module* mod) {
            setP(mod, "Key", (float) m->hKey);
            setP(mod, "Scale", (float) m->hScale);
            setP(mod, "Grid octave (bottom row)", role <= 1 ? -1.f : 0.f);
            setP(mod, "Pattern length (steps)", 8.f);
            setP(mod, "Run", 1.f);
            setP(mod, "Grid plays the synth", 1.f);
            quadTone(mod, role, g, rng, false);
            mod->dataFromJson(data);
        });
        json_decref(data);
        if (!q) return false;
        m->trackMods[track] = {q->module->id};
        m->trackRole[track] = role;
        m->trackRecipe[track] = 0;
        clockFrom(q, h);
        // the effect after it (recipes 4-6): the QUAD's stereo out runs through it into the mixer
        app::PortWidget* srcL = port(q, true, "Stereo left");
        app::PortWidget* srcR = port(q, true, "Stereo right");
        if (recipe >= 4 && recipe <= 6) {
            static const char* fxModel[3] = {"NoizeeFlagshipFilter", "FX", "DualFilter"};
            app::ModuleWidget* fxm = spawn("SynTHBloK", fxModel[recipe - 4], x, y, h, [&](engine::Module* mod) { insertTone(mod, recipe, g, rng, false); });
            if (fxm) {
                const char* inL = recipe == 4 ? "Left in" : recipe == 5 ? "In L" : "Left";
                const char* inR = recipe == 4 ? "Right in" : recipe == 5 ? "In R" : "Right";
                wire(srcL, port(fxm, false, inL), rdz::ramp(track), h);
                wire(srcR, port(fxm, false, inR), rdz::ramp(track), h);
                srcL = port(fxm, true, recipe == 4 ? "Left out" : recipe == 5 ? "Out L" : "Left");
                srcR = port(fxm, true, recipe == 4 ? "Right out" : recipe == 5 ? "Out R" : "Right");
                m->trackMods[track].push_back(fxm->module->id);
                m->trackRecipe[track] = recipe;
            }
        }
        float fx = m->params[SynTHBloKRandroyd::FX_PARAM].getValue() * 2.f;
        float level = role <= 1 ? 0.85f : role == 2 ? 0.7f : 0.6f;
        float pan = role == 2 ? -0.2f : role == 3 ? 0.25f : 0.f;
        float send = clamp((role <= 1 ? 0.05f : role == 2 ? 0.1f + 0.3f * GD.rev : 0.15f + 0.45f * GD.rev) * fx, 0.f, 1.f);
        toMixer(track, srcL, srcR, level, pan, send, h);
        return true;
    }

    // the effects after a QUAD. vary = ROLL: a new setting around the recipe's home
    void insertTone(engine::Module* mod, int recipe, int g, std::mt19937& rng, bool vary) {
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        bool echoGenre = g == 11 || g == 12 || g == 15;   // dub, ambient, trip-hop
        if (recipe == 4) {   // MORPH FILTER on the bass: low-pass with drive and a slow chaos drift
            setP(mod, "Cutoff", uni(0.5f, 0.72f));
            setP(mod, "Resonance", uni(0.15f, 0.42f));
            setP(mod, "Morph", uni(0.02f, 0.18f));
            setP(mod, "Drive", uni(0.15f, 0.5f));
            setP(mod, "Chaos", uni(0.12f, 0.35f));
            setP(mod, "Stereo width", uni(0.5f, 0.7f));
            setP(mod, "Dry/wet mix", 1.f);
        }
        else if (recipe == 5) {   // CHARACTER DELAY on the lead
            static const int modes[5] = {1, 2, 4, 6, 3};   // tape, analog, ping-pong, space, modern
            setP(mod, "Delay mode", (float) modes[std::uniform_int_distribution<int>(0, 4)(rng)]);
            setP(mod, "Time", uni(0.25f, 0.45f));
            setP(mod, "FBK", echoGenre ? uni(0.45f, 0.62f) : uni(0.28f, 0.48f));
            setP(mod, "Mix", echoGenre ? uni(0.32f, 0.45f) : uni(0.2f, 0.32f));
            setP(mod, "Tone", uni(0.35f, 0.6f));
            setP(mod, "Depth", uni(0.05f, 0.3f));
            setP(mod, "Duck", uni(0.25f, 0.55f));
            setP(mod, "Width", uni(0.65f, 0.95f));
        }
        else if (recipe == 6) {   // DUAL FILTER on the chords: slow LFO sweep and a phaser
            setP(mod, "Filter A cutoff", uni(0.5f, 0.7f));
            setP(mod, "Filter A resonance", uni(0.2f, 0.45f));
            setP(mod, "Filter B cutoff", 0.92f);
            setP(mod, "LFO rate", uni(0.15f, 0.35f));
            setP(mod, "LFO to cutoff", uni(0.2f, 0.45f));
            setP(mod, "LFO shape", vary && uni(0, 1) < 0.3f ? 3.f : 0.f);
            setP(mod, "Phaser mix", uni(0.25f, 0.6f));
            setP(mod, "Phaser rate", uni(0.15f, 0.4f));
            setP(mod, "Phaser depth", uni(0.5f, 0.8f));
            setP(mod, "Envelope to cutoffs", 0.f);
            setP(mod, "Dry/wet", 1.f);
        }
    }

    // ---- LIGHT DRUMS: Euclid triggers the Kick and the Snare/Hat (no MTX sequencer, a fraction of the CPU).
    // TECHNO RIG: the Euclid channels copy the genre's groove. BROKEN BEAT: the channels run different lengths
    // and drift against each other. The kick goes to the part's own mixer channel, snare + hats to channel 7.
    static int grooveHits(const char* row) { int n = 0; for (int i = 0; i < 8; i++) if (row[i] != '0') n++; return n; }
    static int grooveFirst(const char* row) { for (int i = 0; i < 8; i++) if (row[i] != '0') return i; return 0; }
    void rigSettings(engine::Module* eu, int g, std::mt19937& rng, bool broken, float variety) {
        const rdz::GenreDrums& G = rdz::GDRUMS[g];
        auto rnd = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
        auto set = [&](int c, int steps, int hits, int first) {
            setP(eu, string::f("Ch %d steps", c + 1).c_str(), (float) steps);
            setP(eu, string::f("Ch %d hits", c + 1).c_str(), (float) clamp(hits, 0, steps));
            setP(eu, string::f("Ch %d rotate", c + 1).c_str(), (float) ((steps - first % steps) % steps));   // a hit lands on step `first`
        };
        if (broken) {
            set(0, 16, rnd(3, 5), rnd(0, 2));
            set(1, rnd(0, 1) ? 10 : 14, rnd(2, 3), rnd(3, 5));
            set(2, rnd(0, 1) ? 12 : 9, rnd(5, 8), rnd(0, 2));
            set(3, 7, rnd(1, 2), rnd(2, 5));
            return;
        }
        // the groove's rows, twice over a 16-step bar
        int snareRow = grooveHits(G.rows[1]) >= grooveHits(G.rows[2]) ? 1 : 2;
        set(0, 16, std::max(2, 2 * grooveHits(G.rows[0])), grooveFirst(G.rows[0]));
        set(1, 16, std::max(2, 2 * grooveHits(G.rows[snareRow])), grooveHits(G.rows[snareRow]) ? grooveFirst(G.rows[snareRow]) : 4);
        int hats = 2 * grooveHits(G.rows[3]) + (variety > 0.5f ? rnd(0, 2) * 2 : 0);
        set(2, 16, std::max(4, std::min(16, hats)), grooveFirst(G.rows[3]));
        int oh = 2 * grooveHits(G.rows[4]);
        set(3, 16, oh ? oh : (rnd(0, 1) ? 4 : 0), oh ? grooveFirst(G.rows[4]) : 2);
    }
    void rigTone(engine::Module* kick, engine::Module* sh, int g, std::mt19937& rng) {
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        const rdz::GenreDrums& G = rdz::GDRUMS[g];
        bool hard = g == 2 || g == 3 || g == 5;   // techno, acid, electro: rumble
        bool slow = rdz::GENRES[g].bpmHi < 100;
        if (kick) {
            setP(kick, "Tune", uni(44.f, 58.f));
            setP(kick, "Pitch sweep depth", uni(0.35f, 0.65f));
            setP(kick, "Pitch decay", uni(0.3f, 0.55f));
            setP(kick, "Body decay", slow ? uni(0.55f, 0.8f) : uni(0.35f, 0.6f));
            setP(kick, "Click / punch", uni(0.2f, 0.5f));
            setP(kick, "Drive", clamp(0.1f + G.drive * 1.2f + uni(0.f, 0.2f), 0.f, 0.8f));
            setP(kick, "Rumble amount", hard ? uni(0.25f, 0.6f) : uni(0.f, 0.1f));
            setP(kick, "Level", 0.85f);
        }
        if (sh) {
            setP(sh, "Snare tune", uni(170.f, 240.f));
            setP(sh, "Snare snap", uni(0.45f, 0.75f));
            setP(sh, "Snare decay", uni(0.25f, 0.5f));
            setP(sh, "Hat tune", uni(0.35f, 0.7f));
            setP(sh, "Closed hat decay", uni(0.15f, 0.4f));
            setP(sh, "Open hat decay", uni(0.3f, 0.6f));
            setP(sh, "Snare body vs noise tone", uni(0.35f, 0.65f));
            setP(sh, "Snare drive", clamp(G.drive + uni(0.f, 0.25f), 0.f, 0.7f));
            setP(sh, "Hat tone", uni(0.4f, 0.7f));
            setP(sh, "Snare level", 0.75f);
            setP(sh, "Hat level", uni(0.5f, 0.65f));
        }
    }
    bool buildRig(int track, int g, std::mt19937& rng, history::ComplexAction* h, bool broken, bool resKick = false) {
        SynTHBloKRandroyd* m = rd();
        float x = box.pos.x, y = box.pos.y + (2 + track) * rowH();
        float variety = m->params[SynTHBloKRandroyd::DENSITY_PARAM].getValue();
        app::ModuleWidget* eu = spawn("SynTHBloK", "Euclid", x, y, h, [&](engine::Module* mod) { rigSettings(mod, g, rng, broken, variety); });
        app::ModuleWidget* kick = spawn("SynTHBloK", "Kick", x, y, h, [&](engine::Module* mod) { rigTone(mod, NULL, g, rng); });
        app::ModuleWidget* sh = spawn("SynTHBloK", "SnareHat", x, y, h, [&](engine::Module* mod) { rigTone(NULL, mod, g, rng); });
        if (!eu || !kick || !sh) return false;
        NVGcolor col = rdz::ramp(track);
        wire(port(eu, true, "Ch 1"), port(kick, false, "Trigger"), col, h);
        wire(port(eu, true, "Ch 2"), port(sh, false, "Snare"), col, h);
        wire(port(eu, true, "Ch 3"), port(sh, false, "Closed"), col, h);
        wire(port(eu, true, "Ch 4"), port(sh, false, "Open"), col, h);
        clockFrom(eu, h);
        float fx = m->params[SynTHBloKRandroyd::FX_PARAM].getValue() * 2.f;
        float send = clamp(0.1f * rdz::GDRUMS[g].rev * fx, 0.f, 1.f);
        app::ModuleWidget* rs = resKick ? kickRes(kick, x, y, track, rng, h) : NULL;
        if (rs) toMixer(track, port(rs, true, "Out L"), port(rs, true, "Out R"), 0.95f, 0.f, send * 0.5f, h);
        else toMixer(track, port(kick, true, "Kick"), NULL, 0.95f, 0.f, send * 0.5f, h);
        toMixer(track, port(sh, true, "Mix"), NULL, 0.8f, 0.f, send, h, 7);
        m->trackMods[track] = {eu->module->id, kick->module->id, sh->module->id};
        if (rs) m->trackMods[track].push_back(rs->module->id);
        m->trackFlags[track] = rs ? 1 : 0;
        m->trackRecipe[track] = broken ? 2 : 1;
        m->trackKit[track] = -1;
        return true;
    }

    // ---- METAL GARDEN: Euclid plays a high snare and hats, the Resonator rings each hit as a body
    // (metal, wood, membrane, tube) tuned to the track's key
    void metalSettings(engine::Module* eu, std::mt19937& rng) {
        auto rnd = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
        auto set = [&](int c, int steps, int hits, int rot) {
            setP(eu, string::f("Ch %d steps", c + 1).c_str(), (float) steps);
            setP(eu, string::f("Ch %d hits", c + 1).c_str(), (float) hits);
            setP(eu, string::f("Ch %d rotate", c + 1).c_str(), (float) rot);
        };
        set(0, 16, rnd(3, 6), rnd(0, 15));
        set(1, rnd(0, 2) ? 16 : 12, rnd(5, 11), rnd(0, 11));
        set(2, 16, rnd(1, 2), rnd(0, 15));
        set(3, 16, 0, 0);
    }
    void metalTone(engine::Module* sh, engine::Module* rs, std::mt19937& rng) {
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        if (sh) {
            setP(sh, "Snare tune", uni(260.f, 400.f));
            setP(sh, "Snare snap", uni(0.65f, 0.9f));
            setP(sh, "Snare decay", uni(0.12f, 0.3f));
            setP(sh, "Hat tune", uni(0.3f, 0.8f));
            setP(sh, "Closed hat decay", uni(0.08f, 0.25f));
            setP(sh, "Open hat decay", uni(0.25f, 0.45f));
            setP(sh, "Snare level", uni(0.6f, 0.8f));
            setP(sh, "Hat level", uni(0.45f, 0.65f));
        }
        if (rs) {
            resBody(rs, std::uniform_int_distribution<int>(0, 3)(rng));
            setP(rs, "Body tuning", resTune(std::max(0, rd()->hKey) + (uni(0, 1) < 0.5f ? 0 : 7), 4));
            setP(rs, "Resonance", uni(0.5f, 0.82f));
            setP(rs, "Material damping", uni(0.25f, 0.6f));
            setP(rs, "Exciter strength", uni(0.3f, 0.55f));
            setP(rs, "Dry/wet", uni(0.4f, 0.7f));
        }
    }
    // Resonator helpers: its body is set in its data (the panel's BODY is a cycle button); its pitch is
    // 20 Hz * 400^knob, so a note maps exactly to a knob position
    static void resBody(engine::Module* rs, int body) {
        json_t* d = json_object();
        json_object_set_new(d, "bodyType", json_integer(clamp(body, 0, 3)));
        rs->dataFromJson(d);
        json_decref(d);
        setP(rs, "Body type", 0.f);
    }
    static float resTune(int semi, int octave) {   // semi 0 = C; octave 4 = middle C
        float f = 261.63f * std::pow(2.f, (semi + 12 * (octave - 4)) / 12.f);
        return clamp(std::log(f / 20.f) / std::log(400.f), 0.f, 1.f);
    }
    bool buildMetal(int track, int g, std::mt19937& rng, history::ComplexAction* h) {
        SynTHBloKRandroyd* m = rd();
        float x = box.pos.x, y = box.pos.y + (2 + track) * rowH();
        app::ModuleWidget* eu = spawn("SynTHBloK", "Euclid", x, y, h, [&](engine::Module* mod) { metalSettings(mod, rng); });
        app::ModuleWidget* sh = spawn("SynTHBloK", "SnareHat", x, y, h, [&](engine::Module* mod) { metalTone(mod, NULL, rng); });
        app::ModuleWidget* rs = spawn("SynTHBloK", "Resonator", x, y, h, [&](engine::Module* mod) { metalTone(NULL, mod, rng); });
        if (!eu || !sh || !rs) return false;
        NVGcolor col = rdz::ramp(track);
        wire(port(eu, true, "Ch 1"), port(sh, false, "Snare"), col, h);
        wire(port(eu, true, "Ch 2"), port(sh, false, "Closed"), col, h);
        wire(port(eu, true, "Ch 3"), port(sh, false, "Open"), col, h);
        wire(port(sh, true, "Mix"), port(rs, false, "In L"), col, h);
        clockFrom(eu, h);
        float fx = m->params[SynTHBloKRandroyd::FX_PARAM].getValue() * 2.f;
        toMixer(track, port(rs, true, "Out L"), port(rs, true, "Out R"), 0.7f, 0.15f, clamp(0.3f * rdz::GDRUMS[g].rev * fx, 0.f, 1.f), h);
        m->trackMods[track] = {eu->module->id, sh->module->id, rs->module->id};
        m->trackRecipe[track] = 3;
        m->trackKit[track] = -1;
        return true;
    }
    // ---- MTX-DUO: one module plays the bass (part 2) and the lead (part 3), each to its own mixer channel.
    // Its FORGE FX stay off (lighter). COMPOSER switches its patterns (the chords); RANDROYD mutes its
    // two channels by the arrangement.
    json_t* duoData(int bassRole, int g, std::mt19937& rng, json_t* keepFirst) {
        std::vector<std::vector<int>> lead = roleLines(2, g, rng), bass = roleLines(bassRole, g, rng);
        json_t* d = json_object();
        json_t* ca = json_array();
        for (int p = 0; p < 8; p++) {
            std::string hx;
            if (p == 0 && keepFirst) { const char* old = json_string_value(json_array_get(json_object_get(keepFirst, "chords"), 0)); if (old) hx = old; }
            if (hx.size() < 32) {
                hx.clear();
                for (int v : lead[p % 4]) hx += string::f("%02x", v);
                for (int v : bass[p % 4]) hx += string::f("%02x", v);
            }
            json_array_append_new(ca, json_string(hx.c_str()));
        }
        json_object_set_new(d, "chords", ca);
        return d;
    }
    void duoTone(engine::Module* mod, int bassRole, int g, std::mt19937& rng, bool vary) {
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        float br = rdz::GHARM[g].bright, drv = rdz::GDRUMS[g].drive;
        setP(mod, "Lead OSC 1 wave", uni(0.f, 0.6f));
        setP(mod, "Lead OSC 2 wave", uni(0.f, 0.8f));
        setP(mod, "Lead OSC 2 tune", vary ? uni(-0.12f, 0.12f) : 0.08f);
        setP(mod, "Lead filter cutoff", clamp(0.45f + 0.2f * br + uni(-0.1f, 0.1f), 0.2f, 0.9f));
        setP(mod, "Lead filter resonance", uni(0.15f, 0.4f));
        setP(mod, "Lead filter envelope amount", uni(0.3f, 0.6f));
        setP(mod, "Lead glide", uni(0.f, 0.2f));
        setP(mod, "Lead drive", uni(0.1f, 0.3f) + drv * 0.3f);
        setP(mod, "Lead level", 0.65f);
        setP(mod, "Bass OSC 1 wave", bassRole == 1 ? uni(0.f, 0.4f) : uni(0.3f, 0.7f));
        setP(mod, "Bass sub", bassRole == 1 ? uni(0.f, 0.2f) : uni(0.4f, 0.7f));
        setP(mod, "Bass filter cutoff", clamp((bassRole == 1 ? 0.3f : 0.32f) + 0.1f * br + uni(-0.06f, 0.08f), 0.15f, 0.7f));
        setP(mod, "Bass filter resonance", bassRole == 1 ? uni(0.5f, 0.75f) : uni(0.15f, 0.4f));
        setP(mod, "Bass filter envelope amount", bassRole == 1 ? uni(0.55f, 0.8f) : uni(0.3f, 0.55f));
        setP(mod, "Bass glide", bassRole == 1 ? uni(0.2f, 0.45f) : 0.f);
        setP(mod, "Bass drive", uni(0.15f, 0.35f) + drv * 0.4f);
        setP(mod, "Bass level", 0.8f);
    }
    bool buildDuo(int g, std::mt19937& rng, history::ComplexAction* h, int bassRole, bool followKick = false, bool bellLead = false, bool resKick = false) {
        SynTHBloKRandroyd* m = rd();
        float x = box.pos.x, y = box.pos.y + (2 + 1) * rowH();
        json_t* data = duoData(bassRole, g, rng, NULL);
        app::ModuleWidget* duo = spawn("SynTHBloK", "Duo", x, y, h, [&](engine::Module* mod) {
            setP(mod, "Key", (float) m->hKey);
            setP(mod, "Scale", (float) m->hScale);
            setP(mod, "Pattern length", 8.f);
            setP(mod, "Run", 1.f);
            setP(mod, "FORGE FX", 0.f);
            setP(mod, "Master volume", 0.8f);
            setP(mod, "Lead octave", 0.f);
            setP(mod, "Bass octave", followKick ? 0.f : -1.f);   // following: the Kick plays it 3 octaves down
            duoTone(mod, bassRole, g, rng, false);
            if (followKick) setP(mod, "Bass level", 0.f);
            if (bellLead) setP(mod, "Lead level", 0.f);
            mod->dataFromJson(data);
        });
        json_decref(data);
        if (!duo) return false;
        clockFrom(duo, h);
        float fx = m->params[SynTHBloKRandroyd::FX_PARAM].getValue() * 2.f;
        const rdz::GenreDrums& GD = rdz::GDRUMS[g];
        std::vector<int64_t> extra;
        int flags = 0;
        if (followKick) {   // 808 FOLLOW: bass pitch + gate play a Kick tuned so 0 V (C4) sounds C1
            app::ModuleWidget* kick = spawn("SynTHBloK", "Kick", x, y, h, [&](engine::Module* mod) { boomTone(mod, rng); setP(mod, "Tune", 32.70f); });
            if (kick) {
                wire(port(duo, true, "Bass pitch"), port(kick, false, "V/OCT"), rdz::ramp(1), h);
                wire(port(duo, true, "Bass gate"), port(kick, false, "Trigger"), rdz::ramp(1), h);
                app::ModuleWidget* rs = resKick ? kickRes(kick, x, y, 1, rng, h) : NULL;
                if (rs) toMixer(1, port(rs, true, "Out L"), port(rs, true, "Out R"), 0.85f, 0.f, 0.f, h);
                else toMixer(1, port(kick, true, "Kick"), NULL, 0.85f, 0.f, 0.f, h);
                extra.push_back(kick->module->id);
                if (rs) { extra.push_back(rs->module->id); flags |= 1; }
                flags |= 2;
            }
        }
        if (!(flags & 2)) toMixer(1, port(duo, true, "Bass audio"), NULL, 0.85f, 0.f, clamp(0.05f * fx, 0.f, 1.f), h);
        if (bellLead) {     // BELL LEAD: lead pitch + gate strike a Resonator an octave up
            app::ModuleWidget* rs = spawn("SynTHBloK", "Resonator", x, y, h, [&](engine::Module* mod) { bellLeadTone(mod, rng); });
            if (rs) {
                wire(port(duo, true, "Lead pitch"), port(rs, false, "V/Oct"), rdz::ramp(2), h);
                wire(port(duo, true, "Lead gate"), port(rs, false, "Strike"), rdz::ramp(2), h);
                toMixer(2, port(rs, true, "Out L"), port(rs, true, "Out R"), 0.6f, -0.15f, clamp((0.2f + 0.4f * GD.rev) * fx, 0.f, 1.f), h);
                extra.push_back(rs->module->id);
                flags |= 4;
            }
        }
        if (!(flags & 4)) toMixer(2, port(duo, true, "Lead audio"), NULL, 0.7f, -0.2f, clamp((0.1f + 0.3f * GD.rev) * fx, 0.f, 1.f), h);
        m->trackMods[1] = {duo->module->id};
        for (int64_t id : extra) m->trackMods[1].push_back(id);
        m->trackFlags[1] = flags;
        m->trackRecipe[1] = 7; m->trackRecipe[2] = 8;
        m->trackRole[1] = bassRole; m->trackRole[2] = 2;
        m->trackGenre[1] = m->trackGenre[2] = g;
        return true;
    }
    bool rollDuo(bool mutate) {
        SynTHBloKRandroyd* m = rd();
        app::ModuleWidget* duo = m->trackMods[1].empty() ? NULL : findMw(m->trackMods[1][0]);
        if (!duo) return false;
        std::mt19937 rng(random::u32());
        int g = clamp(m->trackGenre[1], 0, 15);
        history::ModuleChange* hc = new history::ModuleChange;
        hc->name = mutate ? "RANDROYD mutate duo" : "RANDROYD roll duo";
        hc->moduleId = duo->module->id;
        hc->oldModuleJ = duo->module->toJson();
        json_t* cur = mutate ? duo->module->dataToJson() : NULL;
        json_t* d = duoData(std::max(0, m->trackRole[1]), g, rng, cur);
        duo->module->dataFromJson(d);
        json_decref(d);
        if (cur) json_decref(cur);
        if (!mutate) {
            duoTone(duo->module, std::max(0, m->trackRole[1]), g, rng, true);
            if (m->trackFlags[1] & 2) setP(duo->module, "Bass level", 0.f);
            if (m->trackFlags[1] & 4) setP(duo->module, "Lead level", 0.f);
            retoneExtras(1, rng);
        }
        hc->newModuleJ = duo->module->toJson();
        pushUndo(hc);
        m->rolled = true;
        return true;
    }

    // ---- 808 BOOM: Euclid plays a syncopated pattern on a Kick tuned to the key, with a long body
    void boomSettings(engine::Module* eu, std::mt19937& rng) {
        auto rnd = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
        setP(eu, "Ch 1 steps", 16.f); setP(eu, "Ch 1 hits", (float) rnd(3, 5)); setP(eu, "Ch 1 rotate", (float) rnd(0, 6));
        for (int c = 2; c <= 4; c++) setP(eu, string::f("Ch %d hits", c).c_str(), 0.f);
    }
    void boomTone(engine::Module* kick, std::mt19937& rng) {
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        float f = 32.70f * std::pow(2.f, std::max(0, rd()->hKey) / 12.f);   // the key's root, octave 1
        setP(kick, "Tune", clamp(f, 30.f, 120.f));
        setP(kick, "Pitch sweep depth", uni(0.12f, 0.3f));
        setP(kick, "Pitch decay", uni(0.25f, 0.4f));
        setP(kick, "Body decay", uni(0.8f, 0.97f));
        setP(kick, "Click / punch", uni(0.08f, 0.25f));
        setP(kick, "Tone", uni(0.45f, 0.8f));
        setP(kick, "Drive", uni(0.2f, 0.5f));
        setP(kick, "Rumble amount", 0.f);
        setP(kick, "Level", 0.75f);
    }
    bool buildBoom(int track, int g, std::mt19937& rng, history::ComplexAction* h, bool resKick = false) {
        SynTHBloKRandroyd* m = rd();
        float x = box.pos.x, y = box.pos.y + (2 + track) * rowH();
        app::ModuleWidget* eu = spawn("SynTHBloK", "Euclid", x, y, h, [&](engine::Module* mod) { boomSettings(mod, rng); });
        app::ModuleWidget* kick = spawn("SynTHBloK", "Kick", x, y, h, [&](engine::Module* mod) { boomTone(mod, rng); });
        if (!eu || !kick) return false;
        wire(port(eu, true, "Ch 1"), port(kick, false, "Trigger"), rdz::ramp(track), h);
        clockFrom(eu, h);
        app::ModuleWidget* rs = resKick ? kickRes(kick, x, y, track, rng, h) : NULL;
        if (rs) toMixer(track, port(rs, true, "Out L"), port(rs, true, "Out R"), 0.85f, 0.f, 0.f, h);
        else toMixer(track, port(kick, true, "Kick"), NULL, 0.85f, 0.f, 0.f, h);
        m->trackMods[track] = {eu->module->id, kick->module->id};
        if (rs) m->trackMods[track].push_back(rs->module->id);
        m->trackFlags[track] = rs ? 1 : 0;
        m->trackRecipe[track] = 9;
        m->trackRole[track] = 0;
        return true;
    }

    // ---- TUNED BELLS: Euclid strikes a Resonator (no audio in) tuned to the key's root or fifth: a light,
    // ringing texture in place of the pad
    void bellSettings(engine::Module* eu, std::mt19937& rng) {
        auto rnd = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
        int steps = rnd(0, 1) ? 16 : 12;
        setP(eu, "Ch 1 steps", (float) steps); setP(eu, "Ch 1 hits", (float) rnd(2, 5)); setP(eu, "Ch 1 rotate", (float) rnd(0, steps - 1));
        for (int c = 2; c <= 4; c++) setP(eu, string::f("Ch %d hits", c).c_str(), 0.f);
    }
    void bellTone(engine::Module* rs, std::mt19937& rng) {
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        resBody(rs, std::uniform_int_distribution<int>(0, 3)(rng));
        int semi = std::max(0, rd()->hKey) + (uni(0, 1) < 0.35f ? 7 : 0);
        setP(rs, "Body tuning", resTune(semi, uni(0, 1) < 0.5f ? 4 : 5));
        setP(rs, "Resonance", uni(0.7f, 0.92f));
        setP(rs, "Material damping", uni(0.45f, 0.8f));
        setP(rs, "Exciter strength", uni(0.35f, 0.6f));
        setP(rs, "Dry/wet", 1.f);
    }
    bool buildBells(int track, int g, std::mt19937& rng, history::ComplexAction* h) {
        SynTHBloKRandroyd* m = rd();
        float x = box.pos.x, y = box.pos.y + (2 + track) * rowH();
        app::ModuleWidget* eu = spawn("SynTHBloK", "Euclid", x, y, h, [&](engine::Module* mod) { bellSettings(mod, rng); });
        app::ModuleWidget* rs = spawn("SynTHBloK", "Resonator", x, y, h, [&](engine::Module* mod) { bellTone(mod, rng); });
        if (!eu || !rs) return false;
        wire(port(eu, true, "Ch 1"), port(rs, false, "Strike"), rdz::ramp(track), h);
        clockFrom(eu, h);
        float fx = m->params[SynTHBloKRandroyd::FX_PARAM].getValue() * 2.f;
        toMixer(track, port(rs, true, "Out L"), port(rs, true, "Out R"), 0.55f, 0.f, clamp((0.25f + 0.45f * rdz::GDRUMS[g].rev) * fx, 0.f, 1.f), h);
        m->trackMods[track] = {eu->module->id, rs->module->id};
        m->trackRecipe[track] = 10;
        return true;
    }

    // ---- RES KICK: the Kick through a Resonator (membrane or tube body, tuned to the key, low): a ringing,
    // pitched boom. Returns the Resonator, or NULL (then the Kick goes to the mixer as it is).
    void kickResTone(engine::Module* rs, std::mt19937& rng) {
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        static const int bodies[4] = {2, 3, 2, 1};   // membrane, tube, membrane, wood
        resBody(rs, bodies[std::uniform_int_distribution<int>(0, 3)(rng)]);
        setP(rs, "Body tuning", resTune(std::max(0, rd()->hKey), uni(0, 1) < 0.5f ? 2 : 3));
        setP(rs, "Resonance", uni(0.45f, 0.75f));
        setP(rs, "Material damping", uni(0.4f, 0.7f));
        setP(rs, "Exciter strength", uni(0.3f, 0.5f));
        setP(rs, "Dry/wet", uni(0.28f, 0.55f));
    }
    app::ModuleWidget* kickRes(app::ModuleWidget* kick, float& x, float y, int track, std::mt19937& rng, history::ComplexAction* h) {
        app::ModuleWidget* rs = spawn("SynTHBloK", "Resonator", x, y, h, [&](engine::Module* mod) { kickResTone(mod, rng); });
        if (!rs) return NULL;
        wire(port(kick, true, "Kick"), port(rs, false, "In L"), rdz::ramp(track), h);
        return rs;
    }
    // a lead line struck on a Resonator: the DUO's lead pitch + gate, an octave up, ringing
    void bellLeadTone(engine::Module* rs, std::mt19937& rng) {
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        resBody(rs, std::uniform_int_distribution<int>(0, 3)(rng));
        setP(rs, "Body tuning", resTune(0, 5));   // 0 V (C4) rings at C5
        setP(rs, "Resonance", uni(0.72f, 0.9f));
        setP(rs, "Material damping", uni(0.35f, 0.7f));
        setP(rs, "Exciter strength", uni(0.4f, 0.6f));
        setP(rs, "Dry/wet", 1.f);
    }
    // ROLL: new tones on a part's extra voices (the kick on a DUO / MTX-DRUM, the resonators)
    void retoneExtras(int t, std::mt19937& rng) {
        SynTHBloKRandroyd* m = rd();
        int g = clamp(m->trackGenre[t], 0, 15);
        for (size_t k = 1; k < m->trackMods[t].size(); k++) {
            app::ModuleWidget* mw = findMw(m->trackMods[t][k]);
            if (!mw) continue;
            std::string slug = mw->model->slug;
            if (slug == "Kick") {
                if (m->trackFlags[t] & 2) { boomTone(mw->module, rng); setP(mw->module, "Tune", 32.70f); }
                else rigTone(mw->module, NULL, g, rng);
            }
            else if (slug == "Resonator") {
                bool bell = false;
                for (app::PortWidget* pw : mw->getInputs())
                    if (engine::PortInfo* pi = pw->getPortInfo()) if (pi->name == "V/Oct" && APP->scene->rack->getTopCable(pw)) bell = true;
                if (bell) bellLeadTone(mw->module, rng); else kickResTone(mw->module, rng);
            }
        }
    }

    // a part built from light modules: COMPOSER can't mute it, RANDROYD mutes its mixer channels by the section
    bool isLight(int t) { int r = rd()->trackRecipe[t]; return (r >= 1 && r <= 3) || r == 9 || r == 10 || r == 13 || r == 14; }
    // parts whose mixer channels follow the arrangement (light parts, and both halves of an MTX-DUO)
    bool mixerMuted(int t) { int r = rd()->trackRecipe[t]; return isLight(t) || r == 7 || r == 8 || r == 11 || r == 12; }

    // ROLL / MUTATE on the whole track gathers every part's change into one undo step, so UNDO takes back
    // the whole press, not just the last part. Outside rollAll, each part is its own undo step.
    history::ComplexAction* undoGroup = NULL;
    void pushUndo(history::Action* a) {
        if (undoGroup) undoGroup->push(a);
        else APP->history->push(a);
    }

    // ---- MUTATE helpers: variations made from what is playing now, sized by the MUTATE knob ----
    // a drum-style pattern (8 rows x 8 steps, '0' off '1' hit '2' accent): flip hits on the given rows,
    // move an accent now and then
    static void flipHits(std::string& pat, const std::vector<int>& rows, int flips, std::mt19937& rng) {
        if (pat.size() < 64 || rows.empty()) return;
        auto rnd = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
        for (int k = 0; k < flips; k++) {
            int r = rows[rnd(0, (int) rows.size() - 1)], i = rnd(0, 7);
            char& c = pat[r * 8 + i];
            if (c != '0' && rnd(0, 3) == 0) c = c == '2' ? '1' : '2';   // an accent moves
            else c = c == '0' ? '1' : '0';
        }
    }
    // a chord line (8 steps, two hex digits each = the step's notes): stabs move to new steps, an extra
    // stab joins, or one drops out; the chord tones stay, so the harmony doesn't change
    static std::string mutateStabs(const std::string& hx, int ops, std::mt19937& rng) {
        if (hx.size() < 16) return hx;
        int st[8];
        for (int c = 0; c < 8; c++) st[c] = (int) std::strtol(hx.substr(2 * c, 2).c_str(), NULL, 16);
        auto rnd = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
        int chord = 0;
        for (int c = 0; c < 8; c++) if (st[c]) { chord = st[c]; break; }
        if (!chord) return hx;
        for (int k = 0; k < ops; k++) {
            std::vector<int> on, off;
            for (int c = 0; c < 8; c++) (st[c] ? on : off).push_back(c);
            int op = rnd(0, 2);
            if (op == 0 && !on.empty() && !off.empty()) {          // move a stab
                int a = on[rnd(0, (int) on.size() - 1)], b = off[rnd(0, (int) off.size() - 1)];
                st[b] = st[a]; st[a] = 0;
            }
            else if (op == 1 && !off.empty() && on.size() < 5) {   // an extra stab
                st[off[rnd(0, (int) off.size() - 1)]] = chord;
            }
            else if (on.size() > 2) {                              // one drops out
                st[on[rnd(0, (int) on.size() - 1)]] = 0;
            }
        }
        std::string out;
        for (int c = 0; c < 8; c++) out += string::f("%02x", st[c]);
        return out;
    }
    static float getPq(engine::Module* mod, const char* name, float dflt) {
        for (engine::ParamQuantity* pq : mod->paramQuantities) if (pq && pq->name == name) return pq->getValue();
        return dflt;
    }

    // MUTATE on the JAM perc part: patterns 1 and 5 (the main groove) stay; the others get a few steps
    // flipped on the rows that play, more with the MUTATE knob. The JAM's mutes and solos stay as they are.
    bool mutateJam(engine::Module* jam, float amount, std::mt19937& rng) {
        json_t* cur = jam->dataToJson();
        json_t* arr = json_object_get(cur, "patterns");
        size_t n = json_array_size(arr);
        if (n < 2) { json_decref(cur); return false; }
        auto rnd = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
        std::string main;
        if (const char* s0 = json_string_value(json_array_get(arr, 0))) main = s0;
        int flips = 1 + (int) std::round(clamp(amount, 0.f, 1.f) * 5.f);
        json_t* out = json_array();
        for (size_t p = 0; p < n; p++) {
            const char* s = json_string_value(json_array_get(arr, p));
            std::string pat = s ? s : "";
            if (p % 4 != 0 && pat.size() >= 64) {
                // the rows that play in the main groove or in this pattern (the first four if none do)
                std::vector<int> rows;
                for (int r = 0; r < 8; r++) {
                    bool on = false;
                    for (int i = 0; i < 8; i++)
                        if (pat[r * 8 + i] == '1' || (main.size() >= 64 && main[r * 8 + i] == '1')) on = true;
                    if (on) rows.push_back(r);
                }
                if (rows.empty()) rows = {0, 1, 2, 3};
                for (int k = 0; k < flips; k++) {
                    int r = rows[rnd(0, (int) rows.size() - 1)], i = rnd(0, 7);
                    char& c = pat[r * 8 + i];
                    c = c == '1' ? '0' : '1';
                }
            }
            json_array_append_new(out, json_string(pat.c_str()));
        }
        json_t* d = json_object();
        json_object_set_new(d, "patterns", out);
        for (const char* key : {"mute", "solo"})
            if (json_t* j = json_object_get(cur, key)) json_object_set(d, key, j);
        jam->dataFromJson(d);
        json_decref(d);
        json_decref(cur);
        return true;
    }

    // ROLL / MUTATE a light part: new Euclid settings; ROLL also new tones. One undo step.
    // RACK mode's perc part is a JAM: new patterns (ROLL) or variations (MUTATE). A part with nothing to
    // change (the texture looper on MUTATE) returns false and leaves no empty undo step.
    bool rollLight(int t, bool mutate) {
        SynTHBloKRandroyd* m = rd();
        std::mt19937 rng(random::u32());
        int g = clamp(m->trackGenre[t], 0, 15);
        int r = m->trackRecipe[t];
        std::vector<app::ModuleWidget*> mws;
        for (int64_t id : m->trackMods[t]) mws.push_back(findMw(id));
        if (mws.empty() || !mws[0]) return false;
        history::ComplexAction* h = new history::ComplexAction;
        h->name = mutate ? "RANDROYD mutate rhythm" : "RANDROYD roll rhythm";
        for (app::ModuleWidget* mw : mws) {
            if (!mw) continue;
            json_t* oldJ = mw->module->toJson();
            std::string slug = mw->model->slug;
            bool did = true;
            if (slug == "Euclid") {
                if (r == 9) boomSettings(mw->module, rng);
                else if (r == 10) bellSettings(mw->module, rng);
                else if (r == 3) metalSettings(mw->module, rng);
                else if (mutate) {   // the hats and the open hat move (more with the knob); the kick stays
                    auto rnd = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
                    float amt = m->mutateAmount();
                    int st3 = std::max(1, (int) getPq(mw->module, "Ch 3 steps", 16.f));
                    int st4 = std::max(1, (int) getPq(mw->module, "Ch 4 steps", 16.f));
                    int h3 = (int) getPq(mw->module, "Ch 3 hits", 4.f) + rnd(-1 - (int) (amt * 2), 1 + (int) (amt * 2));
                    setP(mw->module, "Ch 3 hits", (float) clamp(h3, 2, st3));
                    setP(mw->module, "Ch 3 rotate", (float) rnd(0, st3 - 1));
                    setP(mw->module, "Ch 4 hits", (float) clamp(rnd(0, 2 + (int) (amt * 3)), 0, st4));
                    setP(mw->module, "Ch 4 rotate", (float) rnd(0, st4 - 1));
                    if (amt > 0.6f) {   // a lot of MUTATE: the snare shifts by a step too
                        int st2 = std::max(1, (int) getPq(mw->module, "Ch 2 steps", 16.f));
                        float r2 = getPq(mw->module, "Ch 2 rotate", 0.f) + (rnd(0, 1) ? 1.f : -1.f);
                        setP(mw->module, "Ch 2 rotate", (float) (((int) r2 % st2 + st2) % st2));
                    }
                }
                else rigSettings(mw->module, g, rng, r == 2, m->params[SynTHBloKRandroyd::DENSITY_PARAM].getValue());
            }
            else if (slug == "Jam") {
                if (mutate) did = mutateJam(mw->module, m->mutateAmount(), rng);
                else { json_t* jd = jamData(g, rng); mw->module->dataFromJson(jd); json_decref(jd); }
            }
            else if (!mutate) {
                if (slug == "Kick") { if (r == 9) boomTone(mw->module, rng); else rigTone(mw->module, NULL, g, rng); }
                else if (slug == "SnareHat") { if (r == 3) metalTone(mw->module, NULL, rng); else rigTone(NULL, mw->module, g, rng); }
                else if (slug == "Resonator") { if (r == 10) bellTone(mw->module, rng); else if (r == 3) metalTone(NULL, mw->module, rng); else kickResTone(mw->module, rng); }
                else did = false;
            }
            else did = false;
            if (!did) { json_decref(oldJ); continue; }
            history::ModuleChange* hc = new history::ModuleChange;
            hc->moduleId = mw->module->id;
            hc->oldModuleJ = oldJ;
            hc->newModuleJ = mw->module->toJson();
            h->push(hc);
        }
        if (h->isEmpty()) { delete h; return false; }
        pushUndo(h);
        m->rolled = true;
        return true;
    }

    // the arrangement for light parts: their mixer channels are muted in the sections they sit out
    int lastSec = -2; uint64_t lastArrSig = 0;
    int texPhase = 0;   // the texture looper: 1 waits for section A to record, 2 records until section C, then plays
    struct Press { int64_t mod; std::string param; int frames; };
    std::vector<Press> presses;   // momentary buttons pressed on other modules, released a few frames later
    void press(app::ModuleWidget* mw, const char* name) { if (!mw) return; setP(mw->module, name, 1.f); presses.push_back({mw->module->id, name, 3}); }
    void releasePresses() {
        for (auto it = presses.begin(); it != presses.end();) {
            if (--it->frames > 0) { ++it; continue; }
            if (app::ModuleWidget* mw = findMw(it->mod)) setP(mw->module, it->param.c_str(), 0.f);
            it = presses.erase(it);
        }
    }
    void followSong() {
        SynTHBloKRandroyd* m = rd();
        app::ModuleWidget* song = findMw(m->composerId);
        app::ModuleWidget* mix = findMw(m->mixerId);
        if (!mix) return;
        int sec = song ? randroydSongSection(song->module) : -1;
        // the texture looper records the chords from the top of the song (section A) until section C, then plays
        if (texPhase > 0 && sec != lastSec) {
            app::ModuleWidget* tex = m->trackMods[5].empty() ? NULL : findMw(m->trackMods[5][0]);
            if (!tex || tex->model->slug != "Echo") texPhase = 0;
            else if (texPhase == 1 && sec == 0) { press(tex, "Loop: clear"); press(tex, "Loop: record"); texPhase = 2; }
            else if (texPhase == 2 && sec == 2) { press(tex, "Loop: record"); press(tex, "Loop: play"); texPhase = 0; }
        }
        uint64_t sig = 0;
        for (int i = 0; i < 8; i++) sig = sig * 131 + m->arr[i] + 7 * m->trackRecipe[i];
        sig = sig * 131 + m->locks.size();
        if (sec == lastSec && sig == lastArrSig) return;
        // No mixer mutes any more (they stopped parts dead): the DUO's halves sit out on blank patterns, and
        // light parts (no patterns COMPOSER can pick) play on. Lift the mutes older versions set, once per
        // new arrangement, so a channel you mute yourself stays muted through the sections.
        bool newArr = sig != lastArrSig;
        lastSec = sec; lastArrSig = sig;
        if (!newArr) return;
        for (int t = 0; t < 8; t++) {
            if (!mixerMuted(t)) continue;
            std::set<int> chs;
            for (const SynTHBloKRandroyd::Lock& l : m->locks) if (l.track == t) chs.insert(l.channel);
            for (int ch : chs) setP(mix->module, string::f("CH %d mute", ch).c_str(), 0.f);
        }
    }

    // remove a track's modules (each is its own undo step, as when you delete a module)
    void removeTrack(int track) {
        SynTHBloKRandroyd* m = rd();
        for (int64_t id : m->trackMods[track])
            if (app::ModuleWidget* mw = findMw(id)) mw->removeAction();
        m->trackMods[track].clear();
        m->trackGenre[track] = -1;
        m->trackKit[track] = -1;
    }
    bool trackLive(int track) {
        for (int64_t id : rd()->trackMods[track]) if (findMw(id)) return true;
        return false;
    }

    // NEW TRACK: a whole new track (every layer, one key and tempo), saved into the next empty pad.
    // The track that was playing is saved into its own pad first, with any tweaks you made.
    void newTrack() {
        SynTHBloKRandroyd* m = rd();
        if (!m) return;
        if (m->mode == 1) { rackTrack(); return; }
        int g = clamp((int) std::round(m->params[SynTHBloKRandroyd::GENRE_PARAM].getValue()), 0, 15);
        const rdz::Genre& G = rdz::GENRES[g];
        uint32_t sd = random::u32();
        std::mt19937 rng(sd);
        int bpm = std::uniform_int_distribution<int>(G.bpmLo, G.bpmHi)(rng);
        // SIZE: how many layers. LIGHT = drums + bass, MED = + lead + chords, FULL = + perc + pad.
        // AUTO reads the CPU meter: what the rack uses without the track being replaced, ~8 % per layer.
        int size = clamp((int) std::round(m->params[SynTHBloKRandroyd::SIZE_PARAM].getValue()), 0, 3);
        int layers = size == 1 ? 2 : size == 2 ? 4 : 6;
        if (size == 0) {
            int live = 0;
            for (int t = 0; t < 8; t++) if (trackLive(t)) live++;
            float load = m->cpuLoad.load();
            float others = std::max(0.f, load - 0.08f * live);
            layers = clamp((int) std::floor((0.6f - others) / 0.08f), 2, 6);
        }
        static const char* SIZE_NAMES[7] = {"", "", "LIGHT", "3 PARTS", "MED", "5 PARTS", "FULL"};
        clearLayers();
        m->hKey = -1;   // a new track picks a new key and progression
        ensureHarmony(g, rng);
        history::ComplexAction* h = new history::ComplexAction;
        h->name = string::f("RANDROYD new %s track", G.name);
        int built = 0, kit = -1;
        if (ensureMaster(h, g, bpm)) {
            setTempo(bpm);
            // casting: a recipe per part, leaning on the genre; a small track takes the light drums
            auto chance = [&](float p) { return std::uniform_real_distribution<float>(0.f, 1.f)(rng) < p; };
            bool technoish = g == 2 || g == 3 || g == 5;
            bool brokenish = g == 6 || g == 7 || g == 10 || g == 13 || g == 14 || g == 15;
            bool echoish = g == 11 || g == 12 || g == 15;
            bool boomish = g == 6 || g == 7 || g == 8 || g == 10 || g == 11 || g == 15;
            bool big = layers >= 5;   // a big track leans on the small units, so it fits the CPU
            int drumR = 0;
            if (layers <= 3) drumR = brokenish ? 2 : 1;
            else {
                float pr = (technoish ? 0.55f : 0.35f) + (big ? 0.15f : 0.f), pb = brokenish ? 0.45f : 0.15f;
                float u = std::uniform_real_distribution<float>(0.f, 1.f)(rng);
                drumR = u < pb ? 2 : u < pb + pr ? 1 : 0;
            }
            // recipes built from singles (sold separately) are only picked when those are installed
            bool rigOK = have("Euclid") && have("Kick") && have("SnareHat");
            bool boomOK = have("Euclid") && have("Kick");
            bool metalOK = have("Euclid") && have("SnareHat") && have("Resonator");
            bool bellsOK = have("Euclid") && have("Resonator");
            if (!rigOK) drumR = 0;
            for (int t = 0; t < layers; t++) {
                bool ok;
                if (t == 0) {
                    bool res = chance(0.5f);   // RES on the kick
                    ok = drumR ? buildRig(t, g, rng, h, drumR == 2, res)
                               : buildDrums(t, g, bpm, rng, h, kit, false, chance(big ? 0.5f : 0.35f), res);
                }
                else if (t == 2 && m->trackRecipe[2] == 8) ok = true;   // the lead plays inside the MTX-DUO
                else if (t == 1 && boomOK && boomish && chance(big ? 0.5f : 0.35f)) ok = buildBoom(t, g, rng, h, chance(0.5f));
                else if (t == 1 && layers >= 3 && chance(big ? 0.5f : 0.3f)) {
                    int role = (rdz::GHARM[g].acid && std::uniform_int_distribution<int>(0, 9)(rng) < 6) ? 1 : 0;
                    ok = buildDuo(g, rng, h, role, chance(boomish ? 0.6f : 0.35f), chance(echoish ? 0.55f : 0.35f), chance(0.4f));
                }
                else if (t == 5 && bellsOK && chance((big ? 0.5f : 0.35f) + (echoish ? 0.15f : 0.f))) ok = buildBells(t, g, rng, h);
                else if (t == 4) {
                    bool metal = metalOK && chance((echoish ? 0.7f : 0.5f) + (big ? 0.15f : 0.f));
                    int pk = -1;
                    ok = metal ? buildMetal(t, g, rng, h) : buildDrums(t, g, bpm, rng, h, pk, true);
                    if (ok && !metal) m->trackKit[4] = pk;
                }
                else if (t == 5) ok = buildQuad(t, g, rng, h, 4);
                else {
                    int role = t == 1 ? ((rdz::GHARM[g].acid && std::uniform_int_distribution<int>(0, 9)(rng) < 6) ? 1 : 0) : (t == 2 ? 2 : 3);
                    int recipe = 0;
                    if (t == 1 && layers >= 4 && chance(0.35f)) recipe = 4;              // bass + morph filter
                    if (t == 2 && chance(echoish ? 0.75f : 0.45f)) recipe = 5;           // lead + delay
                    if (t == 3 && chance(0.3f)) recipe = 6;                              // chords + dual filter
                    ok = buildQuad(t, g, rng, h, role, recipe);
                }
                if (!ok) continue;
                m->trackGenre[t] = g;
                if (t == 0) m->trackKit[0] = drumR ? -1 : kit;
                else if (t != 4) m->trackKit[t] = -1;
                built++;
            }
        }
        if (h->isEmpty()) delete h; else APP->history->push(h);
        if (!built) return;
        m->seed = sd;
        dealArrangement(rng);
        writeArrangement();
        // a new track plays unsaved: the banks only change when you press SAVE and pick a spot
        m->curPad = -1;
        m->selTrack = -1;
        m->params[SynTHBloKRandroyd::TAB_PARAM].setValue(0.f);
        std::string keyName = string::f("%s %s", rdz::KEYS[clamp(m->hKey, 0, 11)], rdz::SCALES[clamp(m->hScale, 0, 7)]);
        m->say(string::f("NEW TRACK: %s  %d BPM  %s  %s%s  ·  %s  ·  SAVE TO KEEP IT", G.name, bpm, keyName.c_str(),
                         size == 0 ? "AUTO " : "", SIZE_NAMES[layers], arrName(m->arrKind)));
        refreshInfo();
    }

    // ---- ARRANGEMENT: COMPOSER plays sections A-H (2 bars each, chords 1-4 twice), and each section has
    // its own set of layers playing. Layers: 0 drums 1 bass 2 lead 3 chords 4 perc 5 pad.
    static const char* arrName(int k) { static const char* n[4] = {"BUILD + DROP", "GROOVE FIRST", "BREAK IN THE MIDDLE", "SLOW BURN"}; return n[clamp(k, 0, 3)]; }
    void dealArrangement(std::mt19937& rng) {
        SynTHBloKRandroyd* m = rd();
        auto chance = [&](float p) { return std::uniform_real_distribution<float>(0.f, 1.f)(rng) < p; };
        enum { D = 1, B = 2, L = 4, C = 8, P = 16, X = 32, ALL = 63 };
        static const uint8_t T[4][8] = {
            {D | X, D | B | X, D | B | P | X, D | B | P | C | X, ALL, ALL, ALL, L | C | X},              // build, drop, break
            {D | P, D | P | B, D | P | B | C, D | P | B | C | L, ALL, ALL, ALL & ~L, C | X | B},           // the groove comes first
            {ALL & ~L, ALL, ALL, L | X, L | X | C, D | L | X | C, ALL, ALL},                              // breakdown in the middle
            {X, X | C, X | C | P, X | C | P | B, X | C | P | B | D, ALL, ALL, ALL & ~D},                  // slow burn
        };
        // the parts this rack has (the lead may be the DUO's other half), and the light ones (no patterns)
        uint8_t present = 0, alwaysOn = 0;
        for (int t = 0; t < 6; t++) {
            bool has = trackLive(t) || (t == 2 && m->trackRecipe[1] == 7 && trackLive(1));
            if (has) present |= (uint8_t) (1 << t);
            if (has && isLight(t)) alwaysOn |= (uint8_t) (1 << t);
        }
        int present_n = 0;
        for (int b = 0; b < 6; b++) present_n += present >> b & 1;
        int k = std::uniform_int_distribution<int>(0, 3)(rng);
        if (k == m->arrKind) k = (k + 1 + std::uniform_int_distribution<int>(0, 2)(rng)) % 4;   // a ROLL always changes it
        m->arrKind = k;
        for (int i = 0; i < 8; i++) {
            uint8_t v = T[k][i];
            if (v == ALL && chance(0.15f)) v &= (uint8_t) ~(chance(0.5f) ? L : P);   // a drop section breathes now and then
            v |= alwaysOn;   // light parts have no blank pattern: they play every section (and show lit)
            // never a thin section: at least three of the parts the rack has play (drums, bass, chords join first)
            for (int add : {0, 1, 3, 2, 4, 5}) {
                int n = 0;
                for (int b = 0; b < 6; b++) n += (v & present) >> b & 1;
                if (n >= std::min(3, present_n)) break;
                if (present >> add & 1) v |= (uint8_t) (1 << add);
            }
            m->arr[i] = v;
        }
    }
    // ---- SILENCE WITHOUT MUTES: a part that sits a section out plays a blank pattern there. COMPOSER never
    // mutes it, so held notes and FX tails ring out instead of a hard stop.
    // Patterns 1-4 hold the four chords' lines and are never touched here. 8 is the blank pattern.
    // On an MTX-DUO, 5-7 hold a chord with one half blank (only the lead or only the bass sits out);
    // if a song needs more than three of those, that half plays on. Unused 5-7 copy 1-3, as before.
    int secPat[8][8] = {};   // [section][track]: the pattern COMPOSER picks
    static std::string blankHalf(std::string hx, bool lead) {   // DUO: 16 hex lead, then 16 hex bass
        if (hx.size() < 32) return hx;
        for (int i = 0; i < 16; i++) hx[(lead ? 0 : 16) + i] = '0';
        return hx;
    }
    void applySectionPatterns() {
        SynTHBloKRandroyd* m = rd();
        for (int sec = 0; sec < 8; sec++) for (int t = 0; t < 8; t++) secPat[sec][t] = sec % 4;
        auto on = [&](int sec, int bit) { return (m->arr[sec] >> bit & 1) != 0; };
        for (int t = 0; t < 6; t++) {
            if (isLight(t) || m->trackMods[t].empty()) continue;
            app::ModuleWidget* mw = findMw(m->trackMods[t][0]);
            if (!mw || !mw->module) continue;
            std::string slug = mw->model->slug;
            bool isDrum = slug == "Drum", isQuad = slug == "MtxQuad", isDuo = slug == "Duo";
            if (!isDrum && !isQuad && !isDuo) continue;   // no blank pattern known: it plays on
            const char* key = isDrum ? "patterns" : isQuad ? "notes" : "chords";
            json_t* cur = mw->module->dataToJson();
            json_t* arr = json_object_get(cur, key);
            if (json_array_size(arr) < 8) { json_decref(cur); continue; }
            std::string src[8];
            for (int p = 0; p < 8; p++) { const char* v = json_string_value(json_array_get(arr, p)); src[p] = v ? v : ""; }
            std::string pat[8];
            int from[8];   // the pattern each slot is built from (DUO accents follow it)
            for (int p = 0; p < 4; p++) { pat[p] = src[p]; from[p] = p; }
            for (int p = 4; p < 8; p++) { pat[p] = src[p % 4]; from[p] = p % 4; }
            pat[7] = std::string(isDrum ? 64 : isQuad ? 16 : 32, '0');
            bool duoHalves = isDuo && t == 1 && m->trackRecipe[1] == 7;   // bass = layer 1, lead = layer 2
            int nextVar = 4;
            std::map<int, int> varSlot;   // chord * 2 + (lead ? 1 : 0) -> slot 4-6
            for (int sec = 0; sec < 8; sec++) {
                int c = sec % 4;
                if (!duoHalves) { if (!on(sec, t)) secPat[sec][t] = 7; continue; }
                bool bass = on(sec, 1), lead = on(sec, 2);
                if (bass && lead) continue;
                if (!bass && !lead) { secPat[sec][t] = 7; continue; }
                int k = c * 2 + (lead ? 0 : 1);   // which half is blank
                auto it = varSlot.find(k);
                if (it == varSlot.end()) {
                    if (nextVar > 6) { m->arr[sec] |= (uint8_t) (lead ? 2 : 4); continue; }   // out of slots: this half plays on (and shows lit)
                    pat[nextVar] = blankHalf(src[c], !lead);
                    from[nextVar] = c;
                    it = varSlot.emplace(k, nextVar++).first;
                }
                secPat[sec][t] = it->second;
            }
            json_t* out = json_array();
            for (int p = 0; p < 8; p++) json_array_append_new(out, json_string(pat[p].c_str()));
            if (isQuad) {   // QUAD reads every key it saves (none are MIDI ports): send it all back
                json_object_set_new(cur, key, out);
                mw->module->dataFromJson(cur);
            }
            else {          // DRUM / DUO: the patterns plus what their loader would reset if missing
                json_t* d = json_object();
                json_object_set_new(d, key, out);
                for (const char* k2 : {"mute", "solo", "choke"})
                    if (json_t* j = json_object_get(cur, k2)) json_object_set(d, k2, j);
                if (json_t* aa = json_object_get(cur, "accents")) {
                    json_t* acc = json_array();
                    for (int p = 0; p < 8; p++) {
                        json_t* a = json_array_get(aa, p == 7 ? 7 : from[p]);
                        if (a) json_array_append(acc, a);
                        else json_array_append_new(acc, json_string("0000"));
                    }
                    json_object_set_new(d, "accents", acc);
                }
                mw->module->dataFromJson(d);
                json_decref(d);
            }
            json_decref(cur);
        }
    }

    // COMPOSER: row t = layer t's module, sections A-H = chord (A-D, then again E-H); a part sitting a
    // section out plays its blank pattern there (applySectionPatterns), never a COMPOSER mute
    void writeArrangement() {
        SynTHBloKRandroyd* m = rd();
        app::ModuleWidget* song = findMw(m->composerId);
        if (!song) return;
        applySectionPatterns();
        json_t* d = song->module->dataToJson();
        json_t* rows = json_array();
        for (int t = 0; t < 8; t++) {
            int64_t id = -1;
            if (t < 6 && !isLight(t) && !m->trackMods[t].empty() && findMw(m->trackMods[t][0])) id = m->trackMods[t][0];
            json_array_append_new(rows, json_integer(id));
        }
        json_object_set_new(d, "rows", rows);
        json_t* cs = json_array();
        for (int sec = 0; sec < 8; sec++) {
            json_t* a = json_array();
            for (int t = 0; t < 8; t++) {
                json_t* v = json_array();
                json_array_append_new(v, json_integer(secPat[sec][t]));
                json_array_append_new(v, json_false());   // never a mute: silence is a blank pattern
                json_array_append_new(v, json_integer(0));
                json_array_append_new(v, json_real(1.0));
                json_array_append_new(a, v);
            }
            json_array_append_new(cs, a);
        }
        json_object_set_new(d, "cells", cs);
        json_object_set_new(d, "dropped", json_array());
        json_object_del(d, "in");    // its MIDI ports stay connected as they are
        json_object_del(d, "out");
        song->module->dataFromJson(d);
        json_decref(d);
        setP(song->module, "Song cycle", 7.f);         // A-H
        setP(song->module, "Bars each section", 1.f);  // 2 bars: one chord each
    }

    // =======================================================================
    // RACK MODE: one rack, built once; NEW TRACK writes a new song into it (patterns, notes, key, tempo,
    // kits, tones, FX settings, arrangement); GROW adds a module now and then (the oldest goes at SIZE's cap).
    // MTX modules are heavy, so the rack has three: MTX-DRUM, MTX-DUO, and MTX QUAD as the chord sequencer
    // (its own synth off). The rest are small SynTHBloK units, wired into each other.
    //   1 DRUMS   MTX-DRUM (its kick row's audio pumps the chords' phaser)
    //   2 BASS    MTX-DUO bass > MORPH FILTER (keytracked by the bass pitch)
    //   3 LEAD    MTX-DUO lead > DUAL FILTER (its envelope fired by the lead gate) > DELAY
    //   4 CHORDS  MTX QUAD (sequencer) poly CV + gate > POLY > PHASER (side = the kick) > RESONATOR or DUAL
    //   5 PERC    K JAM: 1-3 SNARE/HAT, 4 a KICK as a tom > RESONATOR; 6 glitches the drums, 7 jolts the bass filter
    //   6 TEXTURE (FULL) MTX-ECHO looper: records the chords at the top of the song, plays them back reversed
    //   FX bus    MIX-8's own FX, or (FULL) GRAIN-FORGE clocked by COMPOSER
    // =======================================================================
    bool rackReady() {
        SynTHBloKRandroyd* m = rd();
        if (!findMw(m->composerId) || !findMw(m->mixerId)) return false;
        for (int t : {0, 1, 3, 4}) if (m->trackMods[t].empty() || !findMw(m->trackMods[t][0])) return false;
        return true;
    }
    app::ModuleWidget* modOf(int t, const char* slug) {
        for (int64_t id : rd()->trackMods[t]) if (app::ModuleWidget* mw = findMw(id)) if (mw->model->slug == slug) return mw;
        return NULL;
    }
    app::ModuleWidget* masterOf(const char* slug) {
        for (int64_t id : rd()->masterMods) if (app::ModuleWidget* mw = findMw(id)) if (mw->model->slug == slug) return mw;
        return NULL;
    }
    // tones for the rack's small units
    void leadEnv(engine::Module* dual, std::mt19937& rng) {
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        setP(dual, "Envelope", 1.f);   // ADSR, fired by the GATE jack
        setP(dual, "Envelope to cutoffs", uni(0.25f, 0.55f));
        setP(dual, "Attack", uni(0.02f, 0.12f));
        setP(dual, "Decay", uni(0.25f, 0.5f));
        setP(dual, "Sustain", uni(0.15f, 0.45f));
        setP(dual, "Release", uni(0.2f, 0.45f));
        setP(dual, "LFO to cutoff", uni(0.f, 0.15f));
    }
    void polyTone(engine::Module* p, std::mt19937& rng) {
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        setP(p, "Unison stack", (float) std::uniform_int_distribution<int>(0, 2)(rng));
        setP(p, "Attack", uni(0.005f, 0.12f));
        setP(p, "Decay", uni(0.25f, 0.8f));
        setP(p, "Sustain", uni(0.35f, 0.7f));
        setP(p, "Release", uni(0.25f, 0.9f));
        setP(p, "Tone", uni(-0.35f, 0.35f));
        setP(p, "Chorus amount", uni(0.3f, 0.75f));
        setP(p, "Gain", 0.7f);
    }
    void phaserTone(engine::Module* p, std::mt19937& rng) {
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        setP(p, "Width", uni(1.2f, 1.8f));
        setP(p, "Tilt", uni(-0.2f, 0.2f));
        setP(p, "Air", uni(0.1f, 0.4f));
        setP(p, "Glue", uni(0.3f, 0.65f));
        setP(p, "Punch", uni(0.1f, 0.3f));
        setP(p, "Clip", uni(0.f, 0.1f));
        setP(p, "Output level", 1.f);
        setP(p, "Mix", 1.f);
    }
    void chordResTone(engine::Module* rs, std::mt19937& rng) {
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        resBody(rs, std::uniform_int_distribution<int>(0, 3)(rng));
        setP(rs, "Body tuning", resTune(std::max(0, rd()->hKey), 3));
        setP(rs, "Resonance", uni(0.3f, 0.55f));
        setP(rs, "Material damping", uni(0.5f, 0.8f));
        setP(rs, "Exciter strength", uni(0.3f, 0.5f));
        setP(rs, "Dry/wet", uni(0.25f, 0.45f));
    }
    void tomTone(engine::Module* k, std::mt19937& rng) {
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        setP(k, "Tune", clamp(65.41f * std::pow(2.f, std::max(0, rd()->hKey) / 12.f), 30.f, 120.f));
        setP(k, "Pitch sweep depth", uni(0.2f, 0.35f));
        setP(k, "Pitch decay", uni(0.15f, 0.3f));
        setP(k, "Body decay", uni(0.15f, 0.35f));
        setP(k, "Click / punch", uni(0.2f, 0.45f));
        setP(k, "Tone", uni(0.55f, 0.85f));
        setP(k, "Drive", uni(0.05f, 0.3f));
        setP(k, "Rumble amount", 0.f);
        setP(k, "Level", 0.7f);
    }
    void percResTone(engine::Module* rs, std::mt19937& rng) {
        metalTone(NULL, rs, rng);
        setP(rs, "Dry/wet", std::uniform_real_distribution<float>(0.3f, 0.55f)(rng));
    }
    void texTone(engine::Module* e, std::mt19937& rng) {
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        static const int types[4] = {7, 10, 11, 4};   // reverse, crystal, space, tape
        setP(e, "Delay type", (float) types[std::uniform_int_distribution<int>(0, 3)(rng)]);
        setP(e, "Subdivision", 4.f);
        setP(e, "Feedback", uni(0.45f, 0.65f));
        setP(e, "Delay mix", uni(0.45f, 0.65f));
        setP(e, "Tone", uni(0.3f, 0.55f));
        setP(e, "Modulation", uni(0.1f, 0.35f));
        setP(e, "Space", uni(0.4f, 0.75f));
        setP(e, "Kill dry", 1.f);
        setP(e, "Looper position", 1.f);   // loops get echoed
        setP(e, "Record length", 8.f);
    }
    void forgeTone(engine::Module* f, int g, std::mt19937& rng) {
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        bool glitchy = rdz::GDRUMS[g].glitch;
        setP(f, "Glitch chance per step", glitchy ? uni(0.12f, 0.3f) : uni(0.02f, 0.1f));
        setP(f, "Shuffle amount", uni(0.1f, 0.35f));
        setP(f, "Pulse mix", uni(0.f, 0.35f));
        setP(f, "Robot mix", uni(0.f, 0.15f));
        setP(f, "Echo level", uni(0.35f, 0.65f));
        setP(f, "Feedback", uni(0.3f, 0.55f));
    }
    // K JAM's 8 tracks x 8 steps: 1 rim / snare, 2 shaker, 3 open hat, 4 tom, 6 drum glitch, 7 bass-filter jolts
    json_t* jamData(int g, std::mt19937& rng) {
        auto rnd = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
        auto chance = [&](float p) { return std::uniform_real_distribution<float>(0.f, 1.f)(rng) < p; };
        const rdz::GenreDrums& G = rdz::GDRUMS[g];
        auto euclid = [&](int k, int rot) { std::string r(8, '0'); for (int i = 0; i < 8; i++) if (((i * k) % 8) < k) r[(i + rot) % 8] = '1'; return r; };
        std::string base[8];
        base[0] = euclid(rnd(1, 3), rnd(1, 7));
        base[1] = "00000000";
        for (int i = 0; i < 8; i++) if (G.rows[3][i] == '0' && chance(0.6f)) base[1][i] = '1';   // shaker in the hats' gaps
        base[2] = chance(0.6f) ? euclid(1, rnd(2, 7)) : std::string("00000000");
        base[3] = chance(0.7f) ? euclid(rnd(1, 2), rnd(0, 7)) : std::string("00000000");
        base[4] = "00000000";
        base[5] = "00000000";
        base[6] = chance(0.5f) ? euclid(rnd(1, 2), rnd(0, 7)) : std::string("00000000");
        base[7] = "00000000";
        json_t* d = json_object();
        json_t* arr = json_array();
        for (int p = 0; p < 8; p++) {
            std::string rows[8];
            for (int r = 0; r < 8; r++) rows[r] = base[r];
            if (p % 4 == 1 || p % 4 == 2) { int r = rnd(0, 3), i = rnd(0, 7); rows[r][i] = rows[r][i] == '0' ? '1' : '0'; }
            if (p % 4 == 3) { rows[3][rnd(4, 7)] = '1'; rows[5][7] = chance(0.5f) ? '1' : '0'; }   // a fill, maybe a drum glitch
            std::string s;
            for (int r = 0; r < 8; r++) s += rows[r];
            json_array_append_new(arr, json_string(s.c_str()));
        }
        json_object_set_new(d, "patterns", arr);
        return d;
    }

    bool buildRack(int g, int bpm, std::mt19937& rng, history::ComplexAction* h, bool full) {
        SynTHBloKRandroyd* m = rd();
        auto chance = [&](float p) { return std::uniform_real_distribution<float>(0.f, 1.f)(rng) < p; };
        if (!ensureMaster(h, g, bpm)) return false;
        setTempo(bpm);
        app::ModuleWidget* mix = findMw(m->mixerId);
        float fx = m->params[SynTHBloKRandroyd::FX_PARAM].getValue() * 2.f;
        const rdz::GenreDrums& GD = rdz::GDRUMS[g];
        // 1 DRUMS
        int kit = -1;
        if (!buildDrums(0, g, bpm, rng, h, kit)) return false;
        m->trackKit[0] = kit; m->trackGenre[0] = g;
        app::ModuleWidget* drum = findMw(m->trackMods[0][0]);
        // 2 + 3 MTX-DUO: bass > MORPH (keytracked); lead > DUAL (gated envelope) > DELAY
        int bassRole = (rdz::GHARM[g].acid && chance(0.6f)) ? 1 : 0;
        float x = box.pos.x, y = box.pos.y + 3 * rowH();
        json_t* data = duoData(bassRole, g, rng, NULL);
        app::ModuleWidget* duo = spawn("SynTHBloK", "Duo", x, y, h, [&](engine::Module* mod) {
            setP(mod, "Key", (float) m->hKey); setP(mod, "Scale", (float) m->hScale);
            setP(mod, "Pattern length", 8.f); setP(mod, "Run", 1.f); setP(mod, "FORGE FX", 0.f); setP(mod, "Master volume", 0.8f);
            setP(mod, "Lead octave", 0.f); setP(mod, "Bass octave", -1.f);
            duoTone(mod, bassRole, g, rng, false);
            mod->dataFromJson(data);
        });
        json_decref(data);
        app::ModuleWidget* morph = spawn("SynTHBloK", "NoizeeFlagshipFilter", x, y, h, [&](engine::Module* mod) { insertTone(mod, 4, g, rng, false); setP(mod, "Keytrack", 0.5f); });
        app::ModuleWidget* dual = spawn("SynTHBloK", "DualFilter", x, y, h, [&](engine::Module* mod) { insertTone(mod, 6, g, rng, false); leadEnv(mod, rng); });
        app::ModuleWidget* dly = spawn("SynTHBloK", "FX", x, y, h, [&](engine::Module* mod) { insertTone(mod, 5, g, rng, false); });
        if (!duo || !morph || !dual || !dly) return false;
        clockFrom(duo, h);
        NVGcolor c1 = rdz::ramp(1), c2 = rdz::ramp(2), c3 = rdz::ramp(3), c4 = rdz::ramp(4);
        wire(port(duo, true, "Bass audio"), port(morph, false, "Left in"), c1, h);
        wire(port(duo, true, "Bass pitch"), port(morph, false, "Keytrack"), c1, h);
        toMixer(1, port(morph, true, "Left out"), port(morph, true, "Right out"), 0.85f, 0.f, clamp(0.05f * fx, 0.f, 1.f), h);
        wire(port(duo, true, "Lead audio"), port(dual, false, "Left"), c2, h);
        wire(port(duo, true, "Lead gate"), port(dual, false, "Envelope gate"), c2, h);
        wire(port(dual, true, "Left"), port(dly, false, "In L"), c2, h);
        wire(port(dual, true, "Right"), port(dly, false, "In R"), c2, h);
        toMixer(2, port(dly, true, "Out L"), port(dly, true, "Out R"), 0.7f, -0.2f, clamp((0.1f + 0.3f * GD.rev) * fx, 0.f, 1.f), h);
        m->trackMods[1] = {duo->module->id, morph->module->id, dual->module->id, dly->module->id};
        m->trackRecipe[1] = 7; m->trackRecipe[2] = 8; m->trackFlags[1] = 8;
        m->trackRole[1] = bassRole; m->trackRole[2] = 2;
        m->trackGenre[1] = m->trackGenre[2] = g;
        // 4 CHORDS: MTX QUAD sequences > POLY > PHASER (side = the kick) > RESONATOR or DUAL
        x = box.pos.x; y = box.pos.y + 5 * rowH();
        json_t* qd = quadData(3, g, rng);
        app::ModuleWidget* q = spawn("SynTHBloK", "MtxQuad", x, y, h, [&](engine::Module* mod) {
            setP(mod, "Key", (float) m->hKey); setP(mod, "Scale", (float) m->hScale);
            setP(mod, "Grid octave (bottom row)", 0.f); setP(mod, "Pattern length (steps)", 8.f); setP(mod, "Run", 1.f);
            setP(mod, "Grid plays the synth", 0.f);
            mod->dataFromJson(qd);
        });
        json_decref(qd);
        app::ModuleWidget* poly = spawn("SynTHBloK", "Poly", x, y, h, [&](engine::Module* mod) { polyTone(mod, rng); });
        app::ModuleWidget* phs = spawn("SynTHBloK", "Stereo", x, y, h, [&](engine::Module* mod) { phaserTone(mod, rng); });
        bool cres = chance(0.5f);
        app::ModuleWidget* cfx = spawn("SynTHBloK", cres ? "Resonator" : "DualFilter", x, y, h, [&](engine::Module* mod) {
            if (cres) chordResTone(mod, rng); else insertTone(mod, 6, g, rng, false);
        });
        if (!q || !poly || !phs || !cfx) return false;
        clockFrom(q, h);
        wire(port(q, true, "Grid pitch"), port(poly, false, "Poly V/Oct"), c3, h);
        wire(port(q, true, "Grid gate"), port(poly, false, "Poly Gate"), c3, h);
        wire(port(poly, true, "Poly Audio L"), port(phs, false, "Main"), c3, h);
        if (drum) wire(port(drum, true, "Row 1 audio"), port(phs, false, "Side"), rdz::ramp(0), h);
        wire(port(phs, true, "Left"), port(cfx, false, cres ? "In L" : "Left"), c3, h);
        wire(port(phs, true, "Right"), port(cfx, false, cres ? "In R" : "Right"), c3, h);
        toMixer(3, port(cfx, true, cres ? "Out L" : "Left"), port(cfx, true, cres ? "Out R" : "Right"), 0.6f, 0.25f,
                clamp((0.15f + 0.45f * GD.rev) * fx, 0.f, 1.f), h);
        m->trackMods[3] = {q->module->id, poly->module->id, phs->module->id, cfx->module->id};
        m->trackRecipe[3] = 12; m->trackRole[3] = 3; m->trackGenre[3] = g;
        // 5 PERC: K JAM > SNARE/HAT + a tom KICK > RESONATOR; JAM 6 glitches the drums, 7 jolts the bass filter
        x = box.pos.x; y = box.pos.y + 6 * rowH();
        json_t* jd = jamData(g, rng);
        app::ModuleWidget* jam = spawn("SynTHBloK", "Jam", x, y, h, [&](engine::Module* mod) {
            setP(mod, "Pattern length (steps)", 8.f); setP(mod, "Pattern", 1.f);
            mod->dataFromJson(jd);
        });
        json_decref(jd);
        app::ModuleWidget* sh = spawn("SynTHBloK", "SnareHat", x, y, h, [&](engine::Module* mod) { metalTone(mod, NULL, rng); });
        app::ModuleWidget* tom = spawn("SynTHBloK", "Kick", x, y, h, [&](engine::Module* mod) { tomTone(mod, rng); });
        app::ModuleWidget* prs = spawn("SynTHBloK", "Resonator", x, y, h, [&](engine::Module* mod) { percResTone(mod, rng); });
        if (!jam || !sh || !tom || !prs) return false;
        clockFrom(jam, h);
        wire(port(jam, true, "Track 1"), port(sh, false, "Snare"), c4, h);
        wire(port(jam, true, "Track 2"), port(sh, false, "Closed"), c4, h);
        wire(port(jam, true, "Track 3"), port(sh, false, "Open"), c4, h);
        wire(port(jam, true, "Track 4"), port(tom, false, "Trigger"), c4, h);
        if (drum) wire(port(jam, true, "Track 6"), port(drum, false, "FORGE FX glitch"), c4, h);
        wire(port(jam, true, "Track 7"), port(morph, false, "Chaos"), c4, h);
        wire(port(sh, true, "Mix"), port(prs, false, "In L"), c4, h);
        wire(port(tom, true, "Kick"), port(prs, false, "In R"), c4, h);
        toMixer(4, port(prs, true, "Out L"), port(prs, true, "Out R"), 0.7f, 0.15f, clamp(0.3f * GD.rev * fx, 0.f, 1.f), h);
        m->trackMods[4] = {jam->module->id, sh->module->id, tom->module->id, prs->module->id};
        m->trackRecipe[4] = 13; m->trackGenre[4] = g;
        // FULL: the texture (MTX-ECHO looper fed by the chords) and GRAIN-FORGE on the FX bus
        if (full) {
            x = box.pos.x; y = box.pos.y + 7 * rowH();
            app::ModuleWidget* tex = spawn("SynTHBloK", "Echo", x, y, h, [&](engine::Module* mod) { texTone(mod, rng); });
            if (tex) {
                wire(port(cfx, true, cres ? "Out L" : "Left"), port(tex, false, "Left"), rdz::ramp(5), h);
                wire(port(cfx, true, cres ? "Out R" : "Right"), port(tex, false, "Right"), rdz::ramp(5), h);
                clockFrom(tex, h);
                toMixer(5, port(tex, true, "Left"), port(tex, true, "Right"), 0.55f, 0.f, 0.2f, h);
                m->trackMods[5] = {tex->module->id};
                m->trackRecipe[5] = 14; m->trackGenre[5] = g;
                texPhase = 1;
            }
            float mx = mix->box.pos.x;
            app::ModuleWidget* gf = spawn("SynTHBloK", "GrainForge", mx, mix->box.pos.y, h, [&](engine::Module* mod) { forgeTone(mod, g, rng); });
            if (gf) {
                wire(port(mix, true, "FX send left"), port(gf, false, "Left"), nvgRGB(0x94, 0x37, 0xff), h);
                wire(port(mix, true, "FX send right"), port(gf, false, "Right"), nvgRGB(0x94, 0x37, 0xff), h);
                wire(port(gf, true, "Left"), port(mix, false, "FX return left"), nvgRGB(0x94, 0x37, 0xff), h);
                wire(port(gf, true, "Right"), port(mix, false, "FX return right"), nvgRGB(0x94, 0x37, 0xff), h);
                clockFrom(gf, h);
                m->masterMods.push_back(gf->module->id);
            }
        }
        setP(mix->module, "FX program", (float) std::uniform_int_distribution<int>(0, 7)(rng));
        setP(mix->module, "FX return", 0.5f);
        return true;
    }

    // a new song into the rack (keep = the same key, tempo and chords: ROLL). One undo step.
    void writeSong(int g, std::mt19937& rng, bool keep) {
        SynTHBloKRandroyd* m = rd();
        std::vector<app::ModuleWidget*> all;
        for (int64_t id : m->masterMods) if (app::ModuleWidget* mw = findMw(id)) all.push_back(mw);
        for (int t = 0; t < 8; t++) for (int64_t id : m->trackMods[t]) if (app::ModuleWidget* mw = findMw(id)) all.push_back(mw);
        std::vector<json_t*> before;
        for (app::ModuleWidget* mw : all) before.push_back(mw->module->toJson());
        if (!keep) {
            const rdz::Genre& G = rdz::GENRES[g];
            setTempo(std::uniform_int_distribution<int>(G.bpmLo, G.bpmHi)(rng));
            m->hKey = -1;
            ensureHarmony(g, rng);
        }
        float variety = m->params[SynTHBloKRandroyd::DENSITY_PARAM].getValue();
        const rdz::GenreDrums& GD = rdz::GDRUMS[g];
        float fx = m->params[SynTHBloKRandroyd::FX_PARAM].getValue() * 2.f;
        int bassRole = (rdz::GHARM[g].acid && std::uniform_int_distribution<int>(0, 9)(rng) < 6) ? 1 : 0;
        if (app::ModuleWidget* d = modOf(0, "Drum")) {
            json_t* dj = patternsJson(drumPatterns(g, rng, variety));
            d->module->dataFromJson(dj);
            json_decref(dj);
            const std::vector<int>& kits = rdz::KITS[g];
            int kit = kits[std::uniform_int_distribution<int>(0, (int) kits.size() - 1)(rng)];
            randroydDrumKit(d->module, kit);
            m->trackKit[0] = kit;
            setP(d->module, "FORGE FX", GD.glitch ? 1.f : 0.f);
            setP(d->module, "Delay", clamp(0.25f * GD.dly * fx, 0.f, 1.f));
            setP(d->module, "Space", clamp(0.35f * GD.rev * fx, 0.f, 1.f));
        }
        if (app::ModuleWidget* duo = modOf(1, "Duo")) {
            setP(duo->module, "Key", (float) m->hKey); setP(duo->module, "Scale", (float) m->hScale);
            json_t* dd = duoData(bassRole, g, rng, NULL);
            duo->module->dataFromJson(dd);
            json_decref(dd);
            duoTone(duo->module, bassRole, g, rng, true);
            m->trackRole[1] = bassRole;
        }
        if (app::ModuleWidget* mf = modOf(1, "NoizeeFlagshipFilter")) { insertTone(mf->module, 4, g, rng, true); setP(mf->module, "Keytrack", 0.5f); }
        if (app::ModuleWidget* df = modOf(1, "DualFilter")) { insertTone(df->module, 6, g, rng, true); leadEnv(df->module, rng); }
        if (app::ModuleWidget* dl = modOf(1, "FX")) insertTone(dl->module, 5, g, rng, true);
        if (app::ModuleWidget* q = modOf(3, "MtxQuad")) {
            setP(q->module, "Key", (float) m->hKey); setP(q->module, "Scale", (float) m->hScale);
            json_t* qd = quadData(3, g, rng);
            q->module->dataFromJson(qd);
            json_decref(qd);
        }
        if (app::ModuleWidget* p = modOf(3, "Poly")) polyTone(p->module, rng);
        if (app::ModuleWidget* p = modOf(3, "Stereo")) phaserTone(p->module, rng);
        if (app::ModuleWidget* r = modOf(3, "Resonator")) chordResTone(r->module, rng);
        if (app::ModuleWidget* r = modOf(3, "DualFilter")) insertTone(r->module, 6, g, rng, true);
        if (app::ModuleWidget* j = modOf(4, "Jam")) { json_t* jd = jamData(g, rng); j->module->dataFromJson(jd); json_decref(jd); }
        if (app::ModuleWidget* s = modOf(4, "SnareHat")) metalTone(s->module, NULL, rng);
        if (app::ModuleWidget* k = modOf(4, "Kick")) tomTone(k->module, rng);
        if (app::ModuleWidget* r = modOf(4, "Resonator")) percResTone(r->module, rng);
        if (app::ModuleWidget* e = modOf(5, "Echo")) { texTone(e->module, rng); texPhase = 1; }
        if (app::ModuleWidget* f = masterOf("GrainForge")) forgeTone(f->module, g, rng);
        for (const SynTHBloKRandroyd::Grow& gr : m->grown) if (app::ModuleWidget* mw = findMw(gr.mod)) growTone(mw->module, gr.kind, g, rng);
        if (app::ModuleWidget* mix = findMw(m->mixerId)) setP(mix->module, "FX program", (float) std::uniform_int_distribution<int>(0, 7)(rng));
        for (int t = 0; t < 8; t++) if (!m->trackMods[t].empty() || m->trackRecipe[t] == 8) m->trackGenre[t] = g;
        dealArrangement(rng);
        writeArrangement();
        history::ComplexAction* h = new history::ComplexAction;
        h->name = keep ? "RANDROYD new sounds" : "RANDROYD new song";
        for (size_t i = 0; i < all.size(); i++) {
            history::ModuleChange* hc = new history::ModuleChange;
            hc->moduleId = all[i]->module->id;
            hc->oldModuleJ = before[i];
            hc->newModuleJ = all[i]->module->toJson();
            h->push(hc);
        }
        APP->history->push(h);
        m->rolled = true;
    }

    // NEW TRACK in RACK mode: the rack the first time, then a new song in it, saved into the next empty track
    void rackTrack() {
        SynTHBloKRandroyd* m = rd();
        int g = clamp((int) std::round(m->params[SynTHBloKRandroyd::GENRE_PARAM].getValue()), 0, 15);
        uint32_t sd = random::u32();
        std::mt19937 rng(sd);
        bool built = false;
        if (!rackReady()) {
            // the rack uses some singles (sold separately): say which are missing rather than half-build it
            std::string need = missing({"Mix8", "NoizeeFlagshipFilter", "DualFilter", "FX", "Poly", "Stereo", "Resonator", "SnareHat", "Kick"});
            if (!need.empty()) { m->say("RACK MODE ALSO NEEDS: " + need + "  ·  BUILD MODE WORKS WITH WHAT YOU HAVE"); return; }
            int size = clamp((int) std::round(m->params[SynTHBloKRandroyd::SIZE_PARAM].getValue()), 0, 3);
            bool full = size == 3 || (size == 0 && m->cpuLoad.load() < 0.3f);
            clearLayers();
            m->hKey = -1;
            ensureHarmony(g, rng);
            const rdz::Genre& G = rdz::GENRES[g];
            history::ComplexAction* h = new history::ComplexAction;
            h->name = "RANDROYD build the rack";
            bool ok = buildRack(g, std::uniform_int_distribution<int>(G.bpmLo, G.bpmHi)(rng), rng, h, full);
            if (h->isEmpty()) delete h; else APP->history->push(h);
            if (!ok) { m->say("COULD NOT BUILD THE RACK (A SYNTHBLOK MODULE IS MISSING?)"); return; }
            built = true;
        }
        writeSong(g, rng, false);
        m->seed = sd;
        m->curPad = -1;   // a new song plays unsaved: SAVE puts it in a bank
        m->params[SynTHBloKRandroyd::TAB_PARAM].setValue(0.f);
        std::string keyName = string::f("%s %s", rdz::KEYS[clamp(m->hKey, 0, 11)], rdz::SCALES[clamp(m->hScale, 0, 7)]);
        m->say(string::f("%sNEW TRACK: %s  %d BPM  %s  ·  %s  ·  SAVE TO KEEP IT", built ? "RACK BUILT  ·  " : "", rdz::GENRES[g].name,
                         curTempo(0), keyName.c_str(), arrName(m->arrKind)));
        refreshInfo();
    }
    // ROLL in RACK mode: new patterns, sounds and FX settings on the same key, tempo and chords
    void rackRoll() {
        SynTHBloKRandroyd* m = rd();
        if (!rackReady()) { m->say("RACK MODE: PRESS NEW TRACK TO BUILD THE RACK"); return; }
        std::mt19937 rng(random::u32());
        int g = clamp(m->trackGenre[0] >= 0 ? m->trackGenre[0] : (int) std::round(m->params[SynTHBloKRandroyd::GENRE_PARAM].getValue()), 0, 15);
        writeSong(g, rng, true);
        m->say(string::f("NEW SOUNDS + PATTERNS, SAME KEY AND CHORDS  ·  %s  ·  SAVE TO KEEP IT", arrName(m->arrKind)));
    }

    // ---- GROW: one module into the rack, on a part's way to the mixer. At SIZE's cap the oldest goes first.
    // Kinds: 0 RESONATOR, 1 MORPH FILTER, 2 DUAL FILTER, 3 DELAY
    static const char* growName(int k) { static const char* n[4] = {"RESONATOR", "MORPH FILTER", "DUAL FILTER", "DELAY"}; return n[clamp(k, 0, 3)]; }
    static const char* growModel(int k) { static const char* n[4] = {"Resonator", "NoizeeFlagshipFilter", "DualFilter", "FX"}; return n[clamp(k, 0, 3)]; }
    static void growPorts(int k, const char*& inL, const char*& inR, const char*& outL, const char*& outR) {
        static const char* p[4][4] = {{"In L", "In R", "Out L", "Out R"}, {"Left in", "Right in", "Left out", "Right out"},
                                      {"Left", "Right", "Left", "Right"}, {"In L", "In R", "Out L", "Out R"}};
        int i = clamp(k, 0, 3);
        inL = p[i][0]; inR = p[i][1]; outL = p[i][2]; outR = p[i][3];
    }
    void growTone(engine::Module* mod, int kind, int g, std::mt19937& rng) {
        if (kind == 0) chordResTone(mod, rng);
        else insertTone(mod, kind == 1 ? 4 : kind == 2 ? 6 : 5, g, rng, true);
    }
    void chanVals(int ch, float& lvl, float& pan, float& send) {
        lvl = 0.7f; pan = 0.f; send = 0.f;
        app::ModuleWidget* mix = findMw(rd()->mixerId);
        if (!mix) return;
        for (engine::ParamQuantity* pq : mix->module->paramQuantities) {
            if (!pq) continue;
            if (pq->name == string::f("CH %d level", ch)) lvl = pq->getValue();
            else if (pq->name == string::f("CH %d pan", ch)) pan = pq->getValue();
            else if (pq->name == string::f("CH %d FX send", ch)) send = pq->getValue();
        }
    }
    int growCap() {
        SynTHBloKRandroyd* m = rd();
        int size = clamp((int) std::round(m->params[SynTHBloKRandroyd::SIZE_PARAM].getValue()), 0, 3);
        if (size == 1) return 2;
        if (size == 2) return 4;
        if (size == 3) return 6;
        // AUTO: keep adding while the CPU has room (under 85 %), up to 6; past that, the oldest makes way
        float load = m->cpuLoad.load();
        int n = (int) m->grown.size();
        return load < 0.85f ? std::min(6, n + 1) : std::max(1, n);
    }
    void ungrow(size_t i) {
        SynTHBloKRandroyd* m = rd();
        if (i >= m->grown.size()) return;
        SynTHBloKRandroyd::Grow gr = m->grown[i];
        m->grown.erase(m->grown.begin() + i);
        app::ModuleWidget* fxm = findMw(gr.mod);
        app::ModuleWidget* mix = findMw(m->mixerId);
        std::vector<int64_t>& tm = m->trackMods[gr.track];
        tm.erase(std::remove(tm.begin(), tm.end(), gr.mod), tm.end());
        if (!fxm) return;
        const char *inL, *inR, *outL, *outR;
        growPorts(gr.kind, inL, inR, outL, outR);
        app::RackWidget* rack = APP->scene->rack;
        app::PortWidget* srcL = NULL; app::PortWidget* srcR = NULL;
        if (app::CableWidget* cw = rack->getTopCable(port(fxm, false, inL))) srcL = cw->outputPort;
        if (app::CableWidget* cw = rack->getTopCable(port(fxm, false, inR))) srcR = cw->outputPort;
        float lvl, pan, send;
        chanVals(gr.ch, lvl, pan, send);
        m->locks.erase(std::remove_if(m->locks.begin(), m->locks.end(), [&](const SynTHBloKRandroyd::Lock& l) { return l.channel == gr.ch; }), m->locks.end());
        fxm->removeAction();
        if (mix && srcL) {
            history::ComplexAction* h = new history::ComplexAction;
            h->name = "RANDROYD ungrow";
            toMixer(gr.track, srcL, srcR, lvl, pan, send, h, gr.ch);
            if (h->isEmpty()) delete h; else APP->history->push(h);
        }
    }
    void growPressed() {
        SynTHBloKRandroyd* m = rd();
        if (!m) return;
        // GROW works in both modes: it needs a playing track and RANDROYD's mixer (RACK mode: the built rack)
        if (m->mode == 1 ? !rackReady() : (!findMw(m->mixerId) || !anyLive())) {
            m->say(m->mode == 1 ? "RACK MODE: PRESS NEW TRACK TO BUILD THE RACK, THEN GROW" : "PRESS NEW TRACK FIRST: GROW ADDS A MODULE TO THE PLAYING TRACK");
            return;
        }
        // grown modules whose track was rebuilt or deleted are gone: forget them
        m->grown.erase(std::remove_if(m->grown.begin(), m->grown.end(), [&](const SynTHBloKRandroyd::Grow& gr) { return !findMw(gr.mod); }), m->grown.end());
        int swapped = 0;
        int cap = growCap();   // once: in AUTO it depends on how many have grown
        while ((int) m->grown.size() >= cap && !m->grown.empty()) { ungrow(0); swapped++; }
        std::mt19937 rng(random::u32());
        // a channel RANDROYD routes, and an effect it doesn't have on it yet
        std::vector<int> chans;
        for (const SynTHBloKRandroyd::Lock& l : m->locks) if (l.channel >= 1 && l.channel <= 6 && std::find(chans.begin(), chans.end(), l.channel) == chans.end()) chans.push_back(l.channel);
        if (chans.empty()) { m->say("NOTHING TO GROW ON"); return; }
        int ch = chans[std::uniform_int_distribution<int>(0, (int) chans.size() - 1)(rng)];
        std::vector<int> kinds;   // GROW picks among the effect singles that are installed
        for (int k = 0; k < 4; k++) if (have(growModel(k))) kinds.push_back(k);
        if (kinds.empty()) { m->say("GROW NEEDS ONE OF: RESONATOR, MORPH FILTER, DUAL FILTER, CHARACTER DELAY"); return; }
        int kind = kinds[std::uniform_int_distribution<int>(0, (int) kinds.size() - 1)(rng)];
        float lvl, pan, send;
        chanVals(ch, lvl, pan, send);
        int track = 0;
        app::PortWidget* srcL = NULL; app::PortWidget* srcR = NULL;
        app::RackWidget* rack = APP->scene->rack;
        history::ComplexAction* h = new history::ComplexAction;
        h->name = "RANDROYD grow";
        for (auto it = m->locks.begin(); it != m->locks.end();) {
            if (it->channel != ch) { ++it; continue; }
            app::CableWidget* cw = rack->getCable(it->cable);
            if (cw && cw->isComplete()) {
                std::string nm = cw->inputPort->getPortInfo() ? cw->inputPort->getPortInfo()->name : "";
                if (nm.size() && nm.back() == 'R') srcR = cw->outputPort; else srcL = cw->outputPort;
                track = it->track;
                history::CableRemove* hr = new history::CableRemove;
                hr->setCable(cw);
                h->push(hr);
                rack->removeCable(cw);
                delete cw;
            }
            it = m->locks.erase(it);
        }
        if (!srcL) srcL = srcR;
        float x = box.pos.x, y = box.pos.y + (2 + track) * rowH();
        int g = clamp(m->trackGenre[track] >= 0 ? m->trackGenre[track] : 0, 0, 15);
        app::ModuleWidget* fxm = spawn("SynTHBloK", growModel(kind), x, y, h, [&](engine::Module* mod) { growTone(mod, kind, g, rng); });
        const char *inL, *inR, *outL, *outR;
        growPorts(kind, inL, inR, outL, outR);
        if (fxm && srcL) {
            wire(srcL, port(fxm, false, inL), rdz::ramp(track), h);
            if (srcR && srcR != srcL) wire(srcR, port(fxm, false, inR), rdz::ramp(track), h);
            toMixer(track, port(fxm, true, outL), port(fxm, true, outR), lvl, pan, send, h, ch);
            m->trackMods[track].push_back(fxm->module->id);
            SynTHBloKRandroyd::Grow gr; gr.mod = fxm->module->id; gr.track = track; gr.ch = ch; gr.kind = kind;
            m->grown.push_back(gr);
            m->say(string::f("GREW: %s ON MIX CH %d  ·  %d GROWN%s", growName(kind), ch, (int) m->grown.size(),
                             swapped ? "  (CPU / SIZE CAP: THE OLDEST WENT)" : ""));
        }
        else if (srcL) toMixer(track, srcL, srcR, lvl, pan, send, h, ch);
        if (h->isEmpty()) delete h; else APP->history->push(h);
        lastSec = -2;
    }
    void modePressed() {
        SynTHBloKRandroyd* m = rd();
        if (!m) return;
        m->mode = m->mode ? 0 : 1;
        m->say(m->mode ? "RACK MODE: ONE RACK, NEW TRACK = A NEW SONG IN IT, GROW = ADD A MODULE"
                       : "BUILD MODE: NEW TRACK BUILDS EVERY PART FROM RECIPES");
    }
    // a saved track whose modules match the rack loads in place (no rebuilding)
    bool rackMatches(json_t* layers) {
        SynTHBloKRandroyd* m = rd();
        for (int c = 0; c < 8; c++) {
            json_t* lj = json_array_get(layers, c);
            json_t* mods = json_is_object(lj) ? json_object_get(lj, "modules") : NULL;
            size_t n = mods ? json_array_size(mods) : 0;
            if (n != m->trackMods[c].size()) return false;
            for (size_t i = 0; i < n; i++) {
                app::ModuleWidget* mw = findMw(m->trackMods[c][i]);
                json_t* mj = json_array_get(mods, i);
                const char* md = json_string_value(json_object_get(mj, "model"));
                if (!mw || !md || mw->model->slug != newSlug(json_string_value(json_object_get(mj, "plugin")), md)) return false;
            }
        }
        return true;
    }

    // ---- whole tracks: one pad = every layer, its tempo, key and progression ----
    bool anyLive() { for (int t = 0; t < 8; t++) if (trackLive(t)) return true; return false; }
    // SAVE BANKS: 8 banks x 16 spots. Nothing is saved until you press SAVE and pick a spot.
    int nextFreePad() {
        SynTHBloKRandroyd* m = rd();
        int b = clamp(m->bank, 0, 7);
        for (int i = 0; i < RD_SPOTS; i++) if (!m->slots[b][i].used) return b * RD_SPOTS + i;
        return -1;
    }
    bool bankFull() {
        if (nextFreePad() >= 0) return false;
        rd()->say(string::f("BANK %d IS FULL (16 SPOTS): PICK ANOTHER BANK, OR CLEAR A SPOT", clamp(rd()->bank, 0, 7) + 1));
        return true;
    }
    void clearLayers() {
        SynTHBloKRandroyd* m = rd();
        m->locks.erase(std::remove_if(m->locks.begin(), m->locks.end(), [&](const SynTHBloKRandroyd::Lock& l) { return l.track >= 0; }), m->locks.end());
        for (int t = 0; t < 8; t++) { removeTrack(t); m->trackRole[t] = -1; m->trackRecipe[t] = 0; m->trackFlags[t] = 0; }
        if (app::ModuleWidget* mix = findMw(m->mixerId))
            for (int ch = 1; ch <= 8; ch++) setP(mix->module, string::f("CH %d mute", ch).c_str(), 0.f);
        lastSec = -2;
    }
    void setTempo(int bpm) {
        if (app::ModuleWidget* song = findMw(rd()->composerId))
            for (engine::ParamQuantity* pq : song->module->paramQuantities)
                if (pq && pq->name.compare(0, 5, "Tempo") == 0) pq->setValue((float) bpm);
    }
    int curTempo(int fallback) {
        if (app::ModuleWidget* song = findMw(rd()->composerId))
            for (engine::ParamQuantity* pq : song->module->paramQuantities)
                if (pq && pq->name.compare(0, 5, "Tempo") == 0) return (int) std::round(pq->getValue());
        return fallback;
    }

    // one layer: its modules (settings, patterns), the cables between them, genre / kit / role
    json_t* layerJson(int c) {
        SynTHBloKRandroyd* m = rd();
        app::RackWidget* rack = APP->scene->rack;
        json_t* root = json_object();
        json_t* mods = json_array();
        std::map<int64_t, int> index;
        for (int64_t id : m->trackMods[c]) {
            app::ModuleWidget* mw = findMw(id);
            if (!mw) continue;
            json_t* mj = mw->module->toJson();
            json_object_del(mj, "id");
            json_object_del(mj, "leftModuleId");
            json_object_del(mj, "rightModuleId");
            json_object_del(mj, "bypass");
            index[id] = (int) json_array_size(mods);
            json_array_append_new(mods, mj);
        }
        json_t* cabs = json_array();
        for (app::CableWidget* cw : rack->getCompleteCables()) {
            engine::Cable* cb = cw->getCable();
            if (!cb) continue;
            auto oi = index.find(cb->outputModule->id), ii = index.find(cb->inputModule->id);
            if (oi == index.end() || ii == index.end()) continue;   // only the layer's own cables
            json_t* cj = json_array();
            json_array_append_new(cj, json_integer(oi->second));
            json_array_append_new(cj, json_integer(cb->outputId));
            json_array_append_new(cj, json_integer(ii->second));
            json_array_append_new(cj, json_integer(cb->inputId));
            json_array_append_new(cabs, cj);
        }
        json_object_set_new(root, "modules", mods);
        json_object_set_new(root, "cables", cabs);
        json_object_set_new(root, "genre", json_integer(m->trackGenre[c]));
        json_object_set_new(root, "kit", json_integer(m->trackKit[c]));
        json_object_set_new(root, "role", json_integer(m->trackRole[c]));
        json_object_set_new(root, "recipe", json_integer(m->trackRecipe[c]));
        json_object_set_new(root, "flags", json_integer(m->trackFlags[c]));
        // the part's own cables into the mixer, and those channels' level / pan / send
        if (app::ModuleWidget* mix = findMw(m->mixerId)) {
            json_t* mx = json_array();
            json_t* chans = json_array();
            std::set<int> seen;
            for (app::CableWidget* cw : rack->getCompleteCables()) {
                engine::Cable* cb = cw->getCable();
                if (!cb || cb->inputModule != mix->module) continue;
                auto oi = index.find(cb->outputModule->id);
                if (oi == index.end()) continue;
                json_t* cj = json_array();
                json_array_append_new(cj, json_integer(oi->second));
                json_array_append_new(cj, json_integer(cb->outputId));
                json_array_append_new(cj, json_integer(cb->inputId));
                int lt = c;
                for (const SynTHBloKRandroyd::Lock& l : m->locks) if (l.cable == cb->id) lt = l.track;
                json_array_append_new(cj, json_integer(lt));
                json_array_append_new(mx, cj);
                for (const SynTHBloKRandroyd::Lock& l : m->locks)
                    if (l.cable == cb->id && seen.insert(l.channel).second) {
                        auto pv = [&](const char* what) {
                            std::string want = string::lowercase(string::f("CH %d %s", l.channel, what));
                            for (engine::ParamQuantity* pq : mix->module->paramQuantities) if (pq && string::lowercase(pq->name) == want) return pq->getValue();
                            return 0.f;
                        };
                        json_t* ch = json_array();
                        json_array_append_new(ch, json_integer(l.channel));
                        json_array_append_new(ch, json_real(pv("level")));
                        json_array_append_new(ch, json_real(pv("pan")));
                        json_array_append_new(ch, json_real(pv("FX send")));
                        json_array_append_new(chans, ch);
                    }
            }
            json_object_set_new(root, "mix", mx);
            json_object_set_new(root, "chans", chans);
        }
        return root;
    }

    // SAVE: the whole playing track into spot pad (bank * SPOTS + spot)
    bool saveTrack(int pad) {
        SynTHBloKRandroyd* m = rd();
        if (pad < 0 || pad >= SynTHBloKRandroyd::BANKS * RD_SPOTS || !anyLive()) return false;
        json_t* root = json_object();
        json_t* layers = json_array();
        int mods = 0, g = -1;
        for (int t = 0; t < 8; t++) {
            if (!trackLive(t)) { json_array_append_new(layers, json_null()); continue; }
            json_t* lj = layerJson(t);
            mods += (int) json_array_size(json_object_get(lj, "modules"));
            if (g < 0) g = m->trackGenre[t];
            json_array_append_new(layers, lj);
        }
        g = clamp(g, 0, 15);
        int bpm = curTempo((rdz::GENRES[g].bpmLo + rdz::GENRES[g].bpmHi) / 2);
        json_object_set_new(root, "layers", layers);
        json_object_set_new(root, "genre", json_integer(g));
        json_object_set_new(root, "bpm", json_integer(bpm));
        json_object_set_new(root, "key", json_integer(m->hKey));
        json_object_set_new(root, "scale", json_integer(m->hScale));
        json_t* hp = json_array();
        for (int i = 0; i < 4; i++) json_array_append_new(hp, json_integer(m->hProg[i]));
        json_object_set_new(root, "prog", hp);
        json_t* ar = json_array();
        for (int i = 0; i < 8; i++) json_array_append_new(ar, json_integer(m->arr[i]));
        json_object_set_new(root, "arr", ar);
        json_object_set_new(root, "arrKind", json_integer(m->arrKind));
        char* txt = json_dumps(root, JSON_COMPACT);
        json_decref(root);
        SynTHBloKRandroyd::Slot& sl = m->slots[rdBankOf(pad)][rdSpotOf(pad)];
        sl.used = true;
        sl.date = rdToday();
        sl.snapshot = txt ? txt : "";
        free(txt);
        sl.genre = g;
        sl.bpm = bpm;
        sl.modules = mods;
        if (!sl.named) sl.name = rdz::GENRES[g].name;
        sl.key = m->hKey;
        sl.scale = m->hScale;
        return true;
    }

    // one layer back into the rack as row c, rewired to the clock and its mixer channel. Returns modules missing.
    int loadLayer(json_t* root, int c, history::ComplexAction* h) {
        SynTHBloKRandroyd* m = rd();
        int g = clamp((int) json_integer_value(json_object_get(root, "genre")), 0, 15);
        float x = box.pos.x, y = box.pos.y + (2 + c) * rowH();
        std::vector<app::ModuleWidget*> made;
        json_t* mods = json_object_get(root, "modules");
        size_t i; json_t* mj;
        int missing = 0;
        json_array_foreach(mods, i, mj) {
            const char* pl = json_string_value(json_object_get(mj, "plugin"));
            const char* md = json_string_value(json_object_get(mj, "model"));
            app::ModuleWidget* mw = (pl && md) ? spawn(pl, md, x, y, h, [&](engine::Module* mod) { mod->fromJson(mj); }) : NULL;
            if (!mw) missing++;
            made.push_back(mw);
        }
        json_t* cabs = json_object_get(root, "cables");
        json_t* cj;
        json_array_foreach(cabs, i, cj) {
            int oi = (int) json_integer_value(json_array_get(cj, 0)), op = (int) json_integer_value(json_array_get(cj, 1));
            int ii = (int) json_integer_value(json_array_get(cj, 2)), ip = (int) json_integer_value(json_array_get(cj, 3));
            if (oi < 0 || ii < 0 || oi >= (int) made.size() || ii >= (int) made.size() || !made[oi] || !made[ii]) continue;
            wire(made[oi]->getOutput(op), made[ii]->getInput(ip), rdz::ramp(c), h);
        }
        m->trackMods[c].clear();
        for (app::ModuleWidget* mw : made) if (mw) m->trackMods[c].push_back(mw->module->id);
        m->trackGenre[c] = g;
        m->trackKit[c] = (int) json_integer_value(json_object_get(root, "kit"));
        m->trackRole[c] = json_object_get(root, "role") ? (int) json_integer_value(json_object_get(root, "role")) : -1;
        for (app::ModuleWidget* mw : made) if (mw) clockFrom(mw, h);
        m->trackRecipe[c] = (int) json_integer_value(json_object_get(root, "recipe"));
        m->trackFlags[c] = (int) json_integer_value(json_object_get(root, "flags"));
        if (m->trackRecipe[c] == 7) { m->trackRecipe[2] = 8; m->trackRole[2] = 2; m->trackGenre[2] = g; }
        json_t* mxs = json_object_get(root, "mix");
        app::ModuleWidget* mixw = findMw(m->mixerId);
        if (mixw && json_array_size(mxs) > 0) {
            json_t* mj2;
            json_array_foreach(mxs, i, mj2) {
                int oi = (int) json_integer_value(json_array_get(mj2, 0)), op = (int) json_integer_value(json_array_get(mj2, 1));
                int ip = (int) json_integer_value(json_array_get(mj2, 2));
                if (oi < 0 || oi >= (int) made.size() || !made[oi]) continue;
                app::PortWidget* in = NULL;
                for (app::PortWidget* pw : mixw->getInputs()) if (pw->portId == ip) in = pw;
                if (!in || APP->scene->rack->getTopCable(in)) continue;
                int ch = 0;
                if (engine::PortInfo* pi = in->getPortInfo()) sscanf(pi->name.c_str(), "CH %d", &ch);
                json_t* tj = json_array_get(mj2, 3);
                int lt = tj ? (int) json_integer_value(tj) : c;
                int64_t id = addCable(made[oi]->getOutput(op), in, rdz::ramp(lt), h);
                m->locks.push_back(SynTHBloKRandroyd::Lock::make(id, mixw->module->id, ip, lt, ch));
            }
            json_t* cv;
            json_array_foreach(json_object_get(root, "chans"), i, cv) {
                int ch = (int) json_integer_value(json_array_get(cv, 0));
                setP(mixw->module, string::f("CH %d level", ch).c_str(), (float) json_number_value(json_array_get(cv, 1)));
                setP(mixw->module, string::f("CH %d pan", ch).c_str(), (float) json_number_value(json_array_get(cv, 2)));
                setP(mixw->module, string::f("CH %d FX send", ch).c_str(), (float) json_number_value(json_array_get(cv, 3)));
            }
            return missing;
        }
        if (!made.empty() && made[0]) {
            app::PortWidget* l = port(made[0], true, "Left");
            if (!l) l = port(made[0], true, "Stereo left");
            if (!l) l = port(made[0], true, "Out L");
            app::PortWidget* rr = port(made[0], true, "Right");
            if (!rr) rr = port(made[0], true, "Stereo right");
            if (!rr) rr = port(made[0], true, "Out R");
            int role = m->trackRole[c];
            float level = c == 0 ? 0.95f : role <= 1 ? 0.85f : role == 2 ? 0.7f : 0.6f;
            float pan = role == 2 ? -0.2f : role == 3 ? 0.25f : 0.f;
            toMixer(c, l, rr, level, c == 0 ? 0.f : pan, 0.1f * rdz::GDRUMS[g].rev, h);
        }
        return missing;
    }

    // PAD: bring back the whole track saved there (the playing one is saved first)
    void loadTrack(int pad) {
        SynTHBloKRandroyd* m = rd();
        SynTHBloKRandroyd::Slot sl = m->slots[rdBankOf(pad)][rdSpotOf(pad)];
        if (!sl.used || sl.snapshot.empty()) { m->say(rdSpotName(pad) + " IS EMPTY"); return; }
        if (pad == m->curPad && anyLive()) { m->say(rdSpotName(pad) + " IS PLAYING   ROLL = NEW SOUNDS   MUTATE = VARIATIONS"); return; }
        json_error_t err;
        json_t* root = json_loads(sl.snapshot.c_str(), 0, &err);
        if (!root) { m->say("THAT PAD'S DATA IS DAMAGED"); return; }
        json_t* layers = json_object_get(root, "layers");
        if (!json_is_array(layers)) { json_decref(root); m->say("AN OLD-STYLE TRACK (ONE LAYER): RIGHT-CLICK IT TO DELETE"); return; }
        if (m->mode == 1 && rackReady() && rackMatches(layers)) {   // the same rack: load the song in place
            int bpm = (int) json_integer_value(json_object_get(root, "bpm"));
            m->hKey = (int) json_integer_value(json_object_get(root, "key"));
            m->hScale = (int) json_integer_value(json_object_get(root, "scale"));
            json_t* hp = json_object_get(root, "prog");
            for (int i = 0; i < 4; i++) m->hProg[i] = (int) json_integer_value(json_array_get(hp, i));
            json_t* ar = json_object_get(root, "arr");
            for (int i = 0; i < 8; i++) m->arr[i] = ar ? (uint8_t) json_integer_value(json_array_get(ar, i)) : 255;
            m->arrKind = ar ? (int) json_integer_value(json_object_get(root, "arrKind")) : -1;
            history::ComplexAction* h = new history::ComplexAction;
            h->name = "RANDROYD load " + rdSpotName(pad);
            for (int c = 0; c < 8; c++) {
                json_t* lj = json_array_get(layers, c);
                if (!json_is_object(lj)) continue;
                json_t* mods = json_object_get(lj, "modules");
                for (size_t i = 0; i < json_array_size(mods); i++) {
                    app::ModuleWidget* mw = findMw(m->trackMods[c][i]);
                    if (!mw) continue;
                    history::ModuleChange* hc = new history::ModuleChange;
                    hc->moduleId = mw->module->id;
                    hc->oldModuleJ = mw->module->toJson();
                    json_t* mj = json_deep_copy(json_array_get(mods, i));
                    json_object_set_new(mj, "id", json_integer(mw->module->id));
                    mw->module->fromJson(mj);
                    json_decref(mj);
                    hc->newModuleJ = mw->module->toJson();
                    h->push(hc);
                }
                m->trackGenre[c] = (int) json_integer_value(json_object_get(lj, "genre"));
                m->trackKit[c] = (int) json_integer_value(json_object_get(lj, "kit"));
                m->trackRole[c] = (int) json_integer_value(json_object_get(lj, "role"));
                if (app::ModuleWidget* mixw = findMw(m->mixerId)) {
                    size_t k; json_t* cv;
                    json_array_foreach(json_object_get(lj, "chans"), k, cv) {
                        int ch = (int) json_integer_value(json_array_get(cv, 0));
                        setP(mixw->module, string::f("CH %d level", ch).c_str(), (float) json_number_value(json_array_get(cv, 1)));
                        setP(mixw->module, string::f("CH %d pan", ch).c_str(), (float) json_number_value(json_array_get(cv, 2)));
                        setP(mixw->module, string::f("CH %d FX send", ch).c_str(), (float) json_number_value(json_array_get(cv, 3)));
                    }
                }
            }
            json_decref(root);
            if (bpm > 0) setTempo(bpm);
            writeArrangement();
            if (h->isEmpty()) delete h; else APP->history->push(h);
            if (modOf(5, "Echo")) texPhase = 1;
            m->curPad = pad;
            m->bank = rdBankOf(pad);
            m->say(string::f("%s: %s  %d BPM", rdSpotName(pad).c_str(), upperTrim(sl.name, 18).c_str(), bpm));
            refreshInfo();
            return;
        }
        clearLayers();
        int g = clamp((int) json_integer_value(json_object_get(root, "genre")), 0, 15);
        int bpm = (int) json_integer_value(json_object_get(root, "bpm"));
        if (bpm <= 0) bpm = (rdz::GENRES[g].bpmLo + rdz::GENRES[g].bpmHi) / 2;
        m->hKey = (int) json_integer_value(json_object_get(root, "key"));
        m->hScale = (int) json_integer_value(json_object_get(root, "scale"));
        json_t* hp = json_object_get(root, "prog");
        for (int i = 0; i < 4; i++) m->hProg[i] = (int) json_integer_value(json_array_get(hp, i));
        json_t* ar = json_object_get(root, "arr");
        for (int i = 0; i < 8; i++) m->arr[i] = ar ? (uint8_t) json_integer_value(json_array_get(ar, i)) : 255;
        m->arrKind = ar ? (int) json_integer_value(json_object_get(root, "arrKind")) : -1;
        history::ComplexAction* h = new history::ComplexAction;
        h->name = "RANDROYD load " + rdSpotName(pad);
        int missing = 0;
        if (ensureMaster(h, g, bpm)) {
            setTempo(bpm);
            for (int c = 0; c < 8 && c < (int) json_array_size(layers); c++) {
                json_t* lj = json_array_get(layers, c);
                if (json_is_object(lj)) missing += loadLayer(lj, c, h);
            }
        }
        json_decref(root);
        if (h->isEmpty()) delete h; else APP->history->push(h);
        writeArrangement();
        m->curPad = pad;
        m->bank = rdBankOf(pad);
        m->selTrack = -1;
        m->say(missing ? string::f("%s LOADED (%d MODULE%s NOT INSTALLED)", rdSpotName(pad).c_str(), missing, missing > 1 ? "S" : "")
                       : string::f("%s: %s  %d BPM", rdSpotName(pad).c_str(), upperTrim(sl.name, 18).c_str(), bpm));
        refreshInfo();
    }

    // ROLL / MUTATE the whole playing track: every layer, then the pad is updated
    bool rollAll(bool mutate) {
        SynTHBloKRandroyd* m = rd();
        int keep = m->selTrack;
        bool any = false;
        undoGroup = new history::ComplexAction;   // every part's change: one undo step
        undoGroup->name = mutate ? "RANDROYD mutate track" : "RANDROYD roll track";
        for (int t = 0; t < 8; t++)
            if (trackLive(t) && m->trackGenre[t] >= 0) { m->selTrack = t; any = rollTrack(mutate) || any; }
        m->selTrack = keep;
        history::ComplexAction* group = undoGroup;
        undoGroup = NULL;
        if (group->isEmpty()) delete group; else APP->history->push(group);
        if (!any) return false;
        if (!mutate) { std::mt19937 rng(random::u32()); dealArrangement(rng); writeArrangement(); }
        else writeArrangement();   // same arrangement: the new patterns get their blank sections back
        m->say(mutate ? std::string("MUTATED: VARIATIONS ON EVERY PART, THE MAIN GROOVE STAYS  ·  SAVE TO KEEP IT")
                      : string::f("NEW SOUNDS + ARRANGEMENT: %s  ·  SAVE TO KEEP IT", arrName(m->arrKind)));
        return true;
    }

    // CLEAR: empty the rack of everything RANDROYD built (your own modules stay). The playing track is
    // saved into its pad first, so CLEAR never loses it; the pads stay until you erase them.
    void newRack() {
        SynTHBloKRandroyd* m = rd();
        if (!m) return;
        doClear(true);
        clearLayers();
        for (int64_t id : m->masterMods)
            if (app::ModuleWidget* mw = findMw(id)) mw->removeAction();
        m->masterMods.clear();
        m->composerId = m->mixerId = m->audioId = m->masterId = -1;
        m->hKey = -1;
        m->locks.clear();
        m->curPad = -1;
        m->grown.clear();
        texPhase = 0;
        m->say("CLEARED: THE RACK IS EMPTY, YOUR TRACKS ARE KEPT (UNDO BRINGS IT BACK)");
        refreshInfo();
    }

    // ROLL / MUTATE on a live drum track: new grooves in the same genre (ROLL: new patterns and kit;
    // MUTATE: patterns 2-4 only, the main groove stays). Undo: Ctrl+Z restores the module's state.
    bool rollTrack(bool mutate) {
        SynTHBloKRandroyd* m = rd();
        int t = m->selTrack;
        if (t < 0 || !trackLive(t) || m->trackGenre[t] < 0) return false;
        if (m->trackRecipe[t] == 7) return rollDuo(mutate);
        if (isLight(t)) return rollLight(t, mutate);
        if (t != 0 && t != 4) return m->trackRole[t] >= 0 ? rollQuad(t, mutate) : false;
        bool perc = t == 4;
        app::ModuleWidget* d = findMw(m->trackMods[t][0]);
        if (!d) return false;
        std::mt19937 rng(random::u32());
        int g = m->trackGenre[t];
        float variety = mutate ? m->mutateAmount() : std::max(0.4f, m->params[SynTHBloKRandroyd::DENSITY_PARAM].getValue());
        std::vector<std::string> pats = perc ? percPatterns(g, rng, variety) : drumPatterns(g, rng, variety);
        // undo: the module's state before
        history::ModuleChange* hc = new history::ModuleChange;
        hc->name = mutate ? "RANDROYD mutate drums" : "RANDROYD roll drums";
        hc->moduleId = d->module->id;
        hc->oldModuleJ = d->module->toJson();
        json_t* keep = NULL;   // MUTATE: the drum module's own row mutes / solos / chokes stay as they are
        if (mutate) {   // pattern 1 (the main groove) stays; 2-4 are the playing patterns with hits flipped
            json_t* cur = d->module->dataToJson();
            json_t* arr = json_object_get(cur, "patterns");
            int flips = 2 + (int) std::round(variety * 8.f);
            // rows that may change: everything but the kick (row 1); the snare only with a lot of MUTATE
            std::vector<int> rows = variety > 0.6f ? std::vector<int>{1, 2, 3, 4, 5, 6, 7} : std::vector<int>{2, 3, 4, 5, 6, 7};
            for (int p = 0; p < 8; p++) {
                const char* v = json_string_value(json_array_get(arr, p));
                if (v && strlen(v) >= 64) pats[p] = v;
                if (p % 4 != 0) flipHits(pats[p], rows, flips, rng);
            }
            keep = json_object();
            for (const char* k2 : {"mute", "solo", "choke"})
                if (json_t* j = json_object_get(cur, k2)) json_object_set(keep, k2, j);
            json_decref(cur);
        }
        json_t* dj = patternsJson(pats);
        if (keep) { json_object_update(dj, keep); json_decref(keep); }
        d->module->dataFromJson(dj);
        json_decref(dj);
        if (!mutate) {
            const std::vector<int>& kits = rdz::KITS[g];
            int kit = perc ? percKit(g, rng) : kits[std::uniform_int_distribution<int>(0, (int) kits.size() - 1)(rng)];
            randroydDrumKit(d->module, kit);
            m->trackKit[t] = kit;
            m->say(string::f("NEW %s GROOVES, KIT %s", rdz::GENRES[g].name, rdz::KIT_NAMES[kit]));
        }
        else m->say(string::f("MUTATED PATTERNS 2-4 (%d%%), THE MAIN GROOVE STAYS", (int) std::round(variety * 100.f)));
        hc->newModuleJ = d->module->toJson();
        pushUndo(hc);
        if (!mutate && m->trackRecipe[t] == 11) retoneExtras(t, rng);
        m->rolled = true;
        return true;
    }


    // =======================================================================
    // PARK / STORE / LOAD
    // =======================================================================
    int laneState(int t) { return (!rd() || !trackLive(t)) ? 0 : (trackParked(t) ? 2 : 1); }   // no module (browser preview): 0
    bool trackParked(int t) {
        for (int64_t id : rd()->trackMods[t])
            if (app::ModuleWidget* mw = findMw(id)) if (mw->module && mw->module->isBypassed()) return true;
        return false;
    }

    // PARK: bypass the track's row (no CPU, Rack dims it); again = wake. One undo step.
    void parkTrack(int t, bool park) {
        SynTHBloKRandroyd* m = rd();
        history::ComplexAction* h = new history::ComplexAction;
        h->name = park ? "RANDROYD park track" : "RANDROYD wake track";
        for (int64_t id : m->trackMods[t]) {
            app::ModuleWidget* mw = findMw(id);
            if (!mw || mw->module->isBypassed() == park) continue;
            APP->engine->bypassModule(mw->module, park);
            history::ModuleBypass* hb = new history::ModuleBypass;
            hb->moduleId = id;
            hb->bypassed = park;
            h->push(hb);
        }
        if (h->isEmpty()) delete h; else APP->history->push(h);
        m->say(string::f("T%d %s %s", t + 1, rdz::ROLES[t], park ? "PARKED: BYPASSED, NO CPU" : "AWAKE"));
    }
    void parkPressed() {
        SynTHBloKRandroyd* m = rd();
        int t = m->selTrack;
        if (t < 0 || !trackLive(t)) { m->say("SELECT A LIVE TRACK TO PARK (A PAD IN ITS COLUMN)"); return; }
        parkTrack(t, !trackParked(t));
    }

    // ROLL: new lines for all 4 chords; MUTATE: patterns 2-4 (pattern 1 stays)
    bool rollQuad(int t, bool mutate) {
        SynTHBloKRandroyd* m = rd();
        app::ModuleWidget* q = m->trackMods[t].empty() ? NULL : findMw(m->trackMods[t][0]);
        if (!q || m->trackRole[t] < 0) return false;
        std::mt19937 rng(random::u32());
        history::ModuleChange* hc = new history::ModuleChange;
        hc->name = mutate ? "RANDROYD mutate line" : "RANDROYD roll line";
        hc->moduleId = q->module->id;
        hc->oldModuleJ = q->module->toJson();
        json_t* cur = mutate ? q->module->dataToJson() : NULL;
        int role = m->trackRole[t];
        if (mutate && (role == 3 || role == 4) && json_array_size(json_object_get(cur, "notes")) >= 8) {
            // chords / pad lines have no randomness, so a re-deal would come out the same: instead the
            // playing stabs move, join or drop on chords 2-4 (more with the knob); chord 1 stays
            json_t* na = json_object_get(cur, "notes");
            int ops = 1 + (int) std::round(m->mutateAmount() * 4.f);
            json_t* out = json_array();
            for (int p = 0; p < 8; p++) {
                const char* v = json_string_value(json_array_get(na, p));
                std::string hx = v ? v : "";
                if (p % 4 != 0) hx = mutateStabs(hx, ops, rng);
                json_array_append_new(out, json_string(hx.c_str()));
            }
            json_object_set_new(cur, "notes", out);
            q->module->dataFromJson(cur);   // QUAD reads back every key it saves
        }
        else {
            json_t* d = quadData(role, m->trackGenre[t], rng, cur);
            q->module->dataFromJson(d);
            json_decref(d);
        }
        if (cur) json_decref(cur);
        if (!mutate) quadTone(q->module, m->trackRole[t], m->trackGenre[t], rng, true);
        if (!mutate && m->trackMods[t].size() > 1 && m->trackRecipe[t] >= 4)
            if (app::ModuleWidget* fxm = findMw(m->trackMods[t][1])) insertTone(fxm->module, m->trackRecipe[t], clamp(m->trackGenre[t], 0, 15), rng, true);
        hc->newModuleJ = q->module->toJson();
        pushUndo(hc);
        m->rolled = true;
        m->say(mutate ? string::f("T%d %s: NEW LINES ON CHORDS 2-4, THE FIRST STAYS", t + 1, roleName(m->trackRole[t]))
                      : string::f("T%d %s: A NEW LINE ON THE SAME CHORDS", t + 1, roleName(m->trackRole[t])));
        return true;
    }

    // SAVE: the playing track (with your tweaks) back into its own spot (a new track needs SAVE on the list)
    void savePressed() {
        SynTHBloKRandroyd* m = rd();
        if (!m) return;
        if (!anyLive()) { m->say("NOTHING PLAYING TO SAVE: PRESS NEW TRACK"); return; }
        if (m->curPad < 0) { m->say("PRESS SAVE ON THE TRACK LIST AND PICK A SPOT"); return; }   // the user always picks the spot
        saveTrack(m->curPad);
        m->say("SAVED: " + rdSpotName(m->curPad));
    }

    // SAVE mode: the playing track into the spot you clicked (any bank). A used spot is replaced.
    void saveTo(int pad) {
        SynTHBloKRandroyd* m = rd();
        if (!m || pad < 0 || pad >= SynTHBloKRandroyd::BANKS * RD_SPOTS) return;
        if (!anyLive()) { m->say("NOTHING PLAYING TO SAVE: PRESS NEW TRACK"); return; }
        bool replaced = m->slots[rdBankOf(pad)][rdSpotOf(pad)].used && pad != m->curPad;
        saveTrack(pad);
        m->curPad = pad;
        m->bank = rdBankOf(pad);
        m->say("SAVED: " + rdSpotName(pad) + (replaced ? "  ·  REPLACED WHAT WAS THERE" : ""));
    }

    // ---- matrix / scene / set actions ----
    void padClicked(int r, int c) {
        SynTHBloKRandroyd* m = rd();
        if (!m) return;
        int pad = r * RD_SPOTS + c;
        if (rdShift(m)) {   // SHIFT + pad: erase it
            rdShiftDone(m);
            if (!m->slots[r][c].used) { m->say(rdSpotName(pad) + " IS ALREADY EMPTY"); return; }
            m->slots[r][c] = SynTHBloKRandroyd::Slot();
            if (m->curPad == pad) m->curPad = -1;
            m->say(rdSpotName(pad) + " ERASED");
            return;
        }
        loadTrack(pad);
    }
    void sceneClicked(int c) {
        SynTHBloKRandroyd* m = rd();
        if (!m) return;
        SynTHBloKRandroyd::Scene& sc = m->sets[m->set].scenes[c];
        if (rdShift(m)) {
            rdShiftDone(m);
            sc.used = true;
            if (sc.name.empty()) sc.name = string::f("SCENE %d", c + 1);
            m->sets[m->set].used = true;
            m->say(string::f("SAVED SCENE %d IN SET %c", c + 1, 'A' + m->set));
        }
        m->scene = c;
        m->sceneOut = c;
        m->rolled = true;
        m->params[SynTHBloKRandroyd::TAB_PARAM].setValue(1.f);
    }
    void setClicked(int c) {
        SynTHBloKRandroyd* m = rd();
        if (!m) return;
        if (rdShift(m)) {
            rdShiftDone(m);
            m->sets[c].used = true;
            m->say(string::f("SAVED SET %c", 'A' + c));
        }
        m->set = c;
        m->params[SynTHBloKRandroyd::TAB_PARAM].setValue(1.f);
    }

    void step() override {
        ModuleWidget::step();
        SynTHBloKRandroyd* m = rd();
        if (!m) return;
        float dt = APP->window->getLastFrameDuration();
        releasePresses();
        if (m->rollReq.exchange(false)) { if (m->mode == 1) rackRoll(); else if (!rollAll(false)) doRoll(1.f); }
        if (m->mutateReq.exchange(false)) { if (!rollAll(true)) doRoll(m->mutateAmount()); }
        if (m->undoReq.exchange(false)) APP->history->undo();
        m->newTrackReq = false;   // NEW TRACK / NEW RACK run on the click (onPress)
        m->newRackReq = false;
        m->storeReq = false;   // STORE / PARK run on the click (onPress)
        m->parkReq = false;
        if (m->sceneNextReq.exchange(false)) sceneClicked(((m->scene < 0 ? -1 : m->scene) + 1) % 8);
        if (m->noticeTime > 0.f) m->noticeTime -= dt;
        m->cpuLoad = clamp((float) APP->engine->getMeterAverage(), 0.f, 1.f);
        infoTimer -= dt;
        if (infoTimer <= 0.f) { refreshInfo(); infoTimer = 0.25f; }
        followSong();
    }

    void appendContextMenu(Menu* menu) override {
        SynTHBloKRandroyd* m = rd();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolPtrMenuItem("SAFE: leave VCV Core (Audio / MIDI) modules alone", "", &m->safe));
        menu->addChild(createBoolPtrMenuItem("CHAOS: any jack to any jack, loops allowed (wild, can get loud)", "", &m->chaos));
        menu->addChild(createMenuItem("Clear RANDROYD's cables (rolled + mixer routing)", "", [=]() { doClear(true); }));
        menu->addChild(createMenuItem("Clear rolled cables only (keep mixer routing)", "", [=]() { doClear(false); }));
        menu->addChild(createMenuItem("Unlock mixer channels (cables stay, become yours)", "", [=]() { m->locks.clear(); }));
        menu->addChild(createMenuItem("Make rolled cables mine (stop tracking them)", "", [=]() { m->myCables.clear(); }));
        menu->addChild(createMenuItem("Clear all scenes and sets", "", [=]() { for (auto& s : m->sets) s = SynTHBloKRandroyd::Set(); m->scene = -1; }));
        menu->addChild(createMenuItem("Delete all saved tracks", "", [=]() { for (auto& b : m->slots) for (auto& s : b) s = SynTHBloKRandroyd::Slot(); m->curPad = -1; }));
    }
};

// ---------------------------------------------------------------------------
// RdList drawing + clicks
// ---------------------------------------------------------------------------
static std::string rdKeyName(int key, int scale) {
    if (key < 0) return "";
    return string::f("%s %s", rdz::KEYS[clamp(key, 0, 11)], rdz::SCALES[clamp(scale, 0, 7)]);
}
void RdList::drawLayer(const DrawArgs& args, int layer) {
    if (layer != 1) { Widget::drawLayer(args, layer); return; }
    NVGcontext* vg = args.vg;
    float W = box.size.x, H = box.size.y;
    const int N = SynTHBloKRandroyd::SPOTS;
    oled::box(args, W, H);
    rdFont(vg);
    struct Row { int pad; bool used; std::string name, date; };
    std::vector<Row> rows;
    int cur = module ? module->curPad : 2;
    if (module) for (int p : pads()) {   // the bank's 16 spots, always in place
        const SynTHBloKRandroyd::Slot& s = module->slots[rdBankOf(p)][rdSpotOf(p)];
        rows.push_back({p, s.used, s.used ? s.name : std::string(), s.used ? s.date : std::string()});
    }
    else {   // module browser preview
        static const char* demo[4] = {"ACID TECHNO", "DUB TECHNO", "HOUSE", "TRANCE"};
        for (int i = 0; i < N; i++) rows.push_back({i, i < 4, i < 4 ? demo[i] : "", i < 4 ? "11/07/26" : ""});
    }
    float dt = APP->window->getLastFrameDuration();
    blinkT += dt;
    if (saveArm || clearArm) {   // no pick for 15 s: SAVE / CLEAR mode ends
        saveIdle += dt;
        if (saveIdle > 15.f) { saveArm = clearArm = false; clearPad = clearBank = -1; }
    }
    int used = 0;
    for (const Row& r : rows) used += r.used;
    bool titleBlink = std::fmod(blinkT * 2.f, 1.f) < 0.5f;
    rdText(vg, mm2px(2.4f), mm2px(4.2f), saveArm ? "SAVE TO..." : clearArm ? "CLEAR..." : "TRACKS", 11.f,
           saveArm && titleBlink ? rdz::green() : clearArm && titleBlink ? rdz::red() : oled::amber(), NVG_ALIGN_LEFT);
    int curBank = module ? clamp(module->bank, 0, 7) : 0;
    rdText(vg, W - mm2px(2.4f), mm2px(4.2f), string::f("BANK %d  ·  %d / %d", curBank + 1, used, N), 6.f, oled::amber(140), NVG_ALIGN_RIGHT);
    for (int b = 0; b < 8; b++) {   // lit = the bank you're on; brighter outline = it holds tracks
        Rect r = bankBtn(b);
        int n = 0;
        if (module) for (int i = 0; i < N; i++) n += module->slots[b][i].used;
        bool on = b == curBank;
        NVGcolor c = n ? oled::amber() : oled::amber(90);
        if (clearArm && b == clearBank) {   // waiting for the second click: the whole bank goes
            rdBox(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, rdz::red(), rdz::red(), 0.9f, 1.8f);
            rdText(vg, r.pos.x + r.size.x * 0.5f, r.pos.y + r.size.y * 0.5f, "X", 6.5f, oled::ink(), NVG_ALIGN_CENTER);
            continue;
        }
        rdBox(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, on ? oled::amber() : nvgRGBA(20, 14, 6, 255), c, 0.9f, on ? 1.8f : 1.f);
        rdText(vg, r.pos.x + r.size.x * 0.5f, r.pos.y + r.size.y * 0.5f, string::f("%d", b + 1), 6.5f, on ? oled::ink() : c, NVG_ALIGN_CENTER);
    }
    float rh = rowH(), fs = std::min(7.f, rh * 0.62f);
    for (int k = 0; k < (int) rows.size(); k++) {
        const Row& r = rows[k];
        float y = top() + k * rh, yc = y + rh * 0.5f;
        bool play = r.pad == cur;
        if (saveArm) {   // every spot flashes; the playing track's own spot flashes fast
            bool lit = std::fmod(blinkT * (play ? 6.f : 1.6f), 1.f) < 0.5f;
            if (lit) rdBox(vg, mm2px(1.f), y + 0.5f, W - mm2px(2.f), rh - 1.f, play ? nvgRGBA(70, 40, 8, 255) : nvgRGBA(40, 24, 6, 255),
                           play ? nvgRGB(255, 255, 255) : oled::amber(), 1.f, play ? 2.f : 1.2f);
        }
        else if (clearArm) {   // saved spots flash red (the playing one fast); the one waiting to go is solid red
            if (r.pad == clearPad) rdBox(vg, mm2px(1.f), y + 0.5f, W - mm2px(2.f), rh - 1.f, rdz::red(), rdz::red(), 1.f, 2.f);
            else if (r.used && std::fmod(blinkT * (play ? 6.f : 1.6f), 1.f) < 0.5f)
                rdBox(vg, mm2px(1.f), y + 0.5f, W - mm2px(2.f), rh - 1.f, nvgRGBA(60, 10, 6, 255), rdz::red(), 1.f, play ? 2.f : 1.2f);
        }
        else if (play) rdBox(vg, mm2px(1.f), y + 0.5f, W - mm2px(2.f), rh - 1.f, nvgRGBA(58, 32, 8, 255), nvgRGB(255, 255, 255), 1.f, 2.f);
        if (clearArm && r.pad == clearPad) {
            rdText(vg, mm2px(2.6f), yc, "CLICK AGAIN = DELETE", fs, oled::ink(), NVG_ALIGN_LEFT);
            continue;
        }
        int spot = rdSpotOf(r.pad);
        rdText(vg, mm2px(2.6f), yc, string::f("%02d", spot + 1), fs, r.used ? rdz::ramp(spot / 2) : oled::amber(90), NVG_ALIGN_LEFT);
        if (r.used) {
            rdText(vg, mm2px(8.4f), yc, upperTrim(r.name, 14), fs, play ? nvgRGB(255, 255, 255) : oled::amber(), NVG_ALIGN_LEFT);
            if (play) {   // a green play triangle
                float tx = mm2px(36.2f), th = std::min(3.f, rh * 0.28f);
                nvgBeginPath(vg); nvgMoveTo(vg, tx, yc - th); nvgLineTo(vg, tx + th * 1.6f, yc); nvgLineTo(vg, tx, yc + th); nvgClosePath(vg);
                nvgFillColor(vg, rdz::green()); nvgFill(vg);
            }
            rdText(vg, W - mm2px(2.4f), yc, r.date.empty() ? std::string("--/--/--") : r.date, fs * 0.8f, oled::amber(150), NVG_ALIGN_RIGHT);
        }
        else rdText(vg, mm2px(8.4f), yc, "- EMPTY -", fs, oled::amber(saveArm ? 170 : 70), NVG_ALIGN_LEFT);
    }
    rdText(vg, W * 0.5f, H - mm2px(7.3f), saveArm ? "CLICK A SPOT = SAVE THERE   BANK 1-8 = OTHER BANK   SAVE = CANCEL"
                                        : clearArm ? "SPOT x2 = DELETE   LIT BANK x2 = CLEAR BANK   CLEAR = DONE"
                                                   : "CLICK = PLAY   RIGHT-CLICK = RENAME / DELETE   SAVE = KEEP THE TRACK", 4.4f,
           saveArm ? oled::amber() : clearArm ? rdz::red() : oled::dim(), NVG_ALIGN_CENTER);
    if (flashT > 0.f) flashT -= APP->window->getLastFrameDuration(); else flash = -1;
    bool rackMode = module && module->mode == 1;
    for (int k = 0; k < 4; k++) {
        Rect b = softBtn(k);
        bool lit = flash == k || (k == 2 && rackMode) || (k == 0 && saveArm) || (k == 3 && clearArm);
        NVGcolor c = k == 3 ? rdz::red() : (k == 1 || k == 2) ? rdz::green() : oled::amber();
        static const char* lab[4] = {"SAVE", "GROW", "BUILD", "CLEAR"};
        const char* l = k == 2 ? (rackMode ? "RACK" : "BUILD") : lab[k];
        rdBox(vg, b.pos.x, b.pos.y, b.size.x, b.size.y, lit ? c : nvgRGBA(20, 14, 6, 255), c, 0.9f, 1.8f);
        rdText(vg, b.pos.x + b.size.x * 0.5f, b.pos.y + b.size.y * 0.5f, l, 6.5f, lit ? oled::ink() : c, NVG_ALIGN_CENTER);
    }
    Widget::drawLayer(args, layer);
}
void RdList::onButton(const ButtonEvent& e) {
    if (!module || !owner || e.action != GLFW_PRESS) { OpaqueWidget::onButton(e); return; }
    for (int b = 0; b < 8; b++)
        if (bankBtn(b).contains(e.pos) && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            if (clearArm) {
                saveIdle = 0.f;
                clearPad = -1;
                if (b != module->bank) {   // another bank: show it, still in CLEAR mode
                    module->bank = b; scroll = 0.f; clearBank = -1;
                    module->say(string::f("CLEAR: BANK %d  ·  CLICK A SPOT TWICE, OR THIS BANK TWICE TO CLEAR IT ALL", b + 1));
                }
                else if (clearBank != b) {   // the lit bank, once: arm the whole-bank clear
                    clearBank = b;
                    module->say(string::f("CLICK BANK %d AGAIN TO DELETE ALL 16 SPOTS (NO UNDO)", b + 1));
                }
                else {   // twice: every track in the bank goes
                    int n = 0;
                    for (int i = 0; i < SynTHBloKRandroyd::SPOTS; i++) {
                        if (!module->slots[b][i].used) continue;
                        module->slots[b][i] = SynTHBloKRandroyd::Slot();
                        if (module->curPad == b * SynTHBloKRandroyd::SPOTS + i) module->curPad = -1;
                        n++;
                    }
                    clearBank = -1;
                    module->say(n ? string::f("BANK %d CLEARED: %d TRACK%s DELETED (THE RACK KEEPS PLAYING)", b + 1, n, n > 1 ? "S" : "")
                                  : string::f("BANK %d WAS ALREADY EMPTY", b + 1));
                }
                e.consume(this);
                return;
            }
            module->bank = b;
            scroll = 0.f;
            if (saveArm) { saveIdle = 0.f; module->say(string::f("SAVE: PICK A SPOT IN BANK %d", b + 1)); e.consume(this); return; }
            int n = 0;
            for (int i = 0; i < SynTHBloKRandroyd::SPOTS; i++) n += module->slots[b][i].used;
            module->say(n ? string::f("BANK %d: %d OF 16 SPOTS SAVED", b + 1, n)
                          : string::f("BANK %d IS EMPTY  ·  SAVE, THEN PICK A SPOT, TO KEEP A TRACK", b + 1));
            e.consume(this);
            return;
        }
    if (softBtn(3).contains(e.pos) && e.button == GLFW_MOUSE_BUTTON_RIGHT) {   // right-click CLEAR: empty the rack
        saveArm = clearArm = false; clearPad = clearBank = -1;
        flash = 3; flashT = 0.25f;
        owner->newRack();
        e.consume(this);
        return;
    }
    for (int k = 0; k < 4; k++)
        if (softBtn(k).contains(e.pos) && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            flash = k; flashT = 0.25f;
            if (k != 3 && clearArm) { clearArm = false; clearPad = clearBank = -1; }
            if (k != 0 && saveArm) saveArm = false;
            if (k == 0) {   // SAVE: into save mode (pick a spot), or out of it
                if (saveArm) { saveArm = false; module->say("SAVE CANCELLED"); }
                else if (!owner->anyLive()) module->say("NOTHING PLAYING TO SAVE: PRESS NEW TRACK");
                else {
                    saveArm = true; saveIdle = 0.f; blinkT = 0.f; scroll = 0.f;
                    if (module->curPad >= 0) module->bank = rdBankOf(module->curPad);   // start on the playing track's bank
                    module->say("SAVE: CLICK A SPOT (THE FAST ONE IS THIS TRACK'S OWN)  ·  BANK 1-8 = OTHER BANK");
                }
            }
            else if (k == 1) owner->growPressed();
            else if (k == 2) owner->modePressed();
            else {   // CLEAR: into clear mode (delete saved tracks or a whole bank), or out of it
                if (clearArm) { clearArm = false; clearPad = clearBank = -1; module->say("CLEAR DONE"); }
                else {
                    clearArm = true; saveIdle = 0.f; blinkT = 0.f; scroll = 0.f; clearPad = clearBank = -1;
                    module->say("CLEAR: CLICK A SPOT TWICE TO DELETE IT  ·  THE LIT BANK TWICE = THE WHOLE BANK  ·  RIGHT-CLICK CLEAR = EMPTY THE RACK");
                }
            }
            e.consume(this);
            return;
        }
    std::vector<int> ps = pads();
    int k = (int) std::floor((e.pos.y - top()) / rowH());
    if (e.pos.y >= top() && k >= 0 && k < (int) ps.size()) {
        int pad = ps[k];
        if (saveArm) {   // SAVE mode: any click on a spot saves there
            saveArm = false;
            owner->saveTo(pad);
            e.consume(this);
            return;
        }
        if (clearArm) {   // CLEAR mode: a track, clicked twice, is deleted
            saveIdle = 0.f;
            clearBank = -1;
            SynTHBloKRandroyd::Slot& sl = module->slots[rdBankOf(pad)][rdSpotOf(pad)];
            if (!sl.used) { clearPad = -1; module->say(rdSpotName(pad) + " IS ALREADY EMPTY"); }
            else if (clearPad != pad) { clearPad = pad; module->say("CLICK " + rdSpotName(pad) + " AGAIN TO DELETE IT (NO UNDO)"); }
            else {
                sl = SynTHBloKRandroyd::Slot();
                if (module->curPad == pad) module->curPad = -1;
                clearPad = -1;
                module->say(rdSpotName(pad) + " DELETED");
            }
            e.consume(this);
            return;
        }
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) owner->loadTrack(pad);
        else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
            SynTHBloKRandroyd* m = module;
            SynTHBloKRandroyd::Slot* sl = &m->slots[rdBankOf(pad)][rdSpotOf(pad)];
            ui::Menu* menu = createMenu();
            menu->addChild(createMenuLabel(rdSpotName(pad)));
            struct NameField : ui::TextField {
                SynTHBloKRandroyd::Slot* sl = NULL;
                void onChange(const ChangeEvent& ev) override { if (sl) { sl->name = getText(); sl->named = !sl->name.empty(); } ui::TextField::onChange(ev); }
            };
            NameField* f = new NameField;
            f->sl = sl;
            f->box.size.x = 170.f;
            f->setText(sl->name);
            f->selectAll();
            menu->addChild(f);
            menu->addChild(createMenuItem("Play it", "", [=]() { owner->loadTrack(pad); }));
            menu->addChild(createMenuItem("Delete this track", "", [=]() {
                *sl = SynTHBloKRandroyd::Slot();
                if (m->curPad == pad) m->curPad = -1;
                m->say(rdSpotName(pad) + " DELETED");
            }));
        }
        e.consume(this);
        return;
    }
    OpaqueWidget::onButton(e);
}

// ---------------------------------------------------------------------------
// RdOled drawing + clicks
// ---------------------------------------------------------------------------
static const float OLED_TOP = 11.f;   // mm, first row

void RdOled::drawLayer(const DrawArgs& args, int layer) {
    if (layer != 1) { Widget::drawLayer(args, layer); return; }
    NVGcontext* vg = args.vg;
    float W = box.size.x, H = box.size.y;
    oled::box(args, W, H);
    rdFont(vg);
    SynTHBloKRandroyd* m = module;
    int t = tab();
    int curSet = m ? m->set : 0, curScene = m ? m->scene : 1, sel = m ? m->selTrack : 3;
    float hy = mm2px(4.6f);

    // header (left of the tabs)
    int cur = m ? m->curPad : 9;
    if (t == 1) {
        rdText(vg, mm2px(2.5f), hy, string::f("SET %c", 'A' + curSet), 13.f, oled::amber(), NVG_ALIGN_LEFT);
        if (curScene >= 0) {
            std::string nm = m ? m->sets[curSet].scenes[curScene].name : std::string("THE DROP");
            rdText(vg, mm2px(19.f), hy, string::f("SCENE %d", curScene + 1), 13.f, rdz::ramp(curScene), NVG_ALIGN_LEFT);
            if (!nm.empty()) rdText(vg, mm2px(42.f), hy, "\"" + upperTrim(nm, 14) + "\"", 8.f, oled::amber(220), NVG_ALIGN_LEFT);
        }
    }
    else {
        rdText(vg, mm2px(2.5f), hy, cur >= 0 ? string::f("%02d", rdSpotOf(cur) + 1) : std::string("--"), 14.f,
               cur >= 0 ? rdz::ramp(rdSpotOf(cur) / 2) : oled::amber(140), NVG_ALIGN_LEFT);
        std::string name = "PRESS NEW TRACK", sub;
        if (!m) { name = "ACID TECHNO"; sub = "135 BPM  ·  E MIN  ·  BUILD + DROP"; }
        else if (cur >= 0 && m->slots[rdBankOf(cur)][rdSpotOf(cur)].used) {
            const SynTHBloKRandroyd::Slot& sl = m->slots[rdBankOf(cur)][rdSpotOf(cur)];
            name = sl.name;
            sub = string::f("%d BPM  ·  %s", sl.bpm, rdKeyName(m->hKey, m->hScale).c_str());
            if (m->arrKind >= 0) sub += std::string("  ·  ") + SynTHBloKRandroydWidget::arrName(m->arrKind);
        }
        rdText(vg, mm2px(13.f), hy, upperTrim(name, 18), 11.f, oled::amber(), NVG_ALIGN_LEFT);
        if (!sub.empty()) rdText(vg, mm2px(13.f), hy + mm2px(3.8f), sub, 6.f, oled::dim(), NVG_ALIGN_LEFT);
    }

    if (t == 0) {   // ---- TRACK: the playing track's 8 layers ----
        for (int k = 0; k < 8; k++)
            rdText(vg, mm2px(44.f + k * 7.6f + 3.3f), mm2px(OLED_TOP - 1.3f), std::string(1, (char) ('A' + k)), 5.f, oled::dim(), NVG_ALIGN_CENTER);
        for (int i = 0; i < 8; i++) {
            float y = mm2px(OLED_TOP + i * 7.2f) + mm2px(2.6f);
            bool used = m ? (!m->trackMods[i].empty() || m->trackRecipe[i] == 8) : i < 4;
            if (i == sel) rdBox(vg, 3.f, y - mm2px(3.2f), W - 6.f, mm2px(6.4f), rdz::ramp(i, 30), nvgRGBA(0, 0, 0, 0), 0.f, 2.f);
            rdText(vg, mm2px(3.f), y, string::f("%d", i + 1), 10.f, rdz::ramp(i, used ? 255 : 110), NVG_ALIGN_LEFT);
            std::string chs;
            if (m) {
                std::set<int> seen;
                for (const SynTHBloKRandroyd::Lock& l : m->locks)
                    if (l.track == i && seen.insert(l.channel).second) chs += (chs.empty() ? "MIX CH " : ",") + string::f("%d", l.channel);
            }
            if (!chs.empty()) rdText(vg, W - mm2px(15.f), y, chs, 5.2f, rdz::ramp(i, 200), NVG_ALIGN_RIGHT);
            if (!used) {
                rdText(vg, mm2px(9.f), y, std::string(rdz::ROLES[i]) + (i >= 6 ? "  (COMING)" : ""), 6.f, rdz::ramp(i, 120), NVG_ALIGN_LEFT);
                nvgBeginPath(vg);
                float xEnd = chs.empty() ? W - mm2px(3.f) : W - mm2px(18.f) - mm2px(2.f) - nvgTextBounds(vg, 0, 0, chs.c_str(), NULL, NULL);
                for (float x = mm2px(40.f); x < xEnd; x += mm2px(1.6f)) { nvgMoveTo(vg, x, y); nvgLineTo(vg, x + mm2px(0.8f), y); }
                nvgStrokeColor(vg, rdz::ramp(i, 64));
                nvgStrokeWidth(vg, 0.7f);
                nvgStroke(vg);
                continue;
            }
            std::string nm;
            if (!m) nm = i == 0 ? "DRUMS  KIT 909" : SynTHBloKRandroydWidget::roleName(i == 1 ? 1 : i);
            else if (m->trackRecipe[i] == 1) nm = "TECHNO RIG  KICK + SNARE/HAT";
            else if (m->trackRecipe[i] == 2) nm = "BROKEN BEAT  EUCLID KIT";
            else if (m->trackRecipe[i] == 3) nm = "METAL GARDEN  RESONATOR";
            else if (m->trackRecipe[i] == 7 && (m->trackFlags[i] & 8)) nm = std::string(SynTHBloKRandroydWidget::roleName(std::max(0, m->trackRole[i]))) + "  DUO > MORPH (KEYTRACKED)";
            else if (m->trackRecipe[i] == 8 && (m->trackFlags[1] & 8)) nm = "LEAD  DUO > DUAL (GATED) > DELAY";
            else if (m->trackRecipe[i] == 12) nm = "CHORDS  QUAD > POLY > PHASER > FX";
            else if (m->trackRecipe[i] == 13) nm = "PERC  K JAM > SNARE/HAT + TOM > RES";
            else if (m->trackRecipe[i] == 14) nm = "TEXTURE  ECHO LOOPER ON THE CHORDS";
            else if (m->trackRecipe[i] == 7) nm = (m->trackFlags[i] & 2) ? std::string("808 FOLLOW  DUO NOTES ON A KICK") : std::string(SynTHBloKRandroydWidget::roleName(std::max(0, m->trackRole[i]))) + "  (MTX-DUO)";
            else if (m->trackRecipe[i] == 8) nm = (m->trackFlags[1] & 4) ? "BELL LEAD  DUO NOTES ON A RESONATOR" : "LEAD  (MTX-DUO, WITH THE BASS)";
            else if (m->trackRecipe[i] == 11) nm = m->trackKit[0] >= 0 ? string::f("DRUMS  KIT %s + KICK", rdz::KIT_NAMES[clamp(m->trackKit[0], 0, 8)]) : std::string("DRUMS + KICK");
            else if (m->trackRecipe[i] == 9) nm = "808 BOOM  EUCLID + KICK";
            else if (m->trackRecipe[i] == 10) nm = "TUNED BELLS  RESONATOR";
            else if (i == 4) nm = m->trackKit[4] >= 0 ? string::f("PERC  KIT %s", rdz::KIT_NAMES[clamp(m->trackKit[4], 0, 8)]) : std::string("PERC");
            else if (i == 0) nm = m->trackKit[0] >= 0 ? string::f("DRUMS  KIT %s", rdz::KIT_NAMES[clamp(m->trackKit[0], 0, 8)]) : std::string("DRUMS");
            else nm = m->trackRole[i] >= 0 ? SynTHBloKRandroydWidget::roleName(m->trackRole[i]) : rdz::ROLES[i];
            if (m && (m->trackFlags[i] & 1)) nm += "  + RES";
            if (m) { int n = 0; for (const SynTHBloKRandroyd::Grow& gr : m->grown) if (gr.track == i) n++; if (n) nm += string::f("  +%d GROWN", n); }
            if (m && (m->trackRecipe[i] >= 4 && m->trackRecipe[i] <= 6)) {
                static const char* fx[3] = {"  + MORPH FILTER", "  + DELAY", "  + DUAL FILTER"};
                nm += fx[m->trackRecipe[i] - 4];
            }
            rdText(vg, mm2px(9.f), y, nm, 7.5f, rdz::ramp(i), NVG_ALIGN_LEFT);
            // the arrangement: sections A-H, lit where this part plays
            for (int k = 0; k < 8; k++) {
                bool on = m ? (m->arr[k] >> i & 1) : !(i == 2 && k < 4) && !(i == 0 && k == 7);
                rdBox(vg, mm2px(44.f + k * 7.6f), y - mm2px(1.8f), mm2px(6.6f), mm2px(3.6f), on ? rdz::ramp(i, 220) : rdz::ramp(i, 28), rdz::ramp(i, on ? 255 : 70), 0.6f, 1.2f);
            }
            int st = (m && owner) ? owner->laneState(i) : 1;   // 1 live, 2 parked
            rdText(vg, W - mm2px(4.f), y, st == 2 ? "PARKED" : "LIVE", 6.f, st == 2 ? rdz::ramp(i, 120) : rdz::green(), NVG_ALIGN_RIGHT);
        }
    }
    else if (t == 1) {   // ---- SCENES: rows = scenes of the set, columns = tracks ----
        float colX0 = mm2px(44.f), colW = mm2px(10.5f);
        rdText(vg, mm2px(3.f), mm2px(OLED_TOP - 0.2f), "SCENE", 5.5f, oled::dim(), NVG_ALIGN_LEFT);
        for (int c = 0; c < 8; c++) rdText(vg, colX0 + c * colW + mm2px(4.5f), mm2px(OLED_TOP - 0.2f), string::f("T%d", c + 1), 5.5f, rdz::ramp(c), NVG_ALIGN_CENTER);
        rdText(vg, W - mm2px(4.f), mm2px(OLED_TOP - 0.2f), "BARS", 5.5f, oled::dim(), NVG_ALIGN_RIGHT);
        for (int i = 0; i < 8; i++) {
            float y = mm2px(OLED_TOP + 3.f + i * 6.6f) + mm2px(2.5f);
            const SynTHBloKRandroyd::Scene* sc = m ? &m->sets[curSet].scenes[i] : NULL;
            bool used = sc ? sc->used : i < 5;
            if (i == curScene) rdBox(vg, 3.f, y - mm2px(3.f), W - 6.f, mm2px(6.f), rdz::ramp(i, 36), nvgRGBA(0, 0, 0, 0), 0.f, 2.f);
            rdText(vg, mm2px(3.f), y, string::f("%d", i + 1), 9.f, rdz::ramp(i, used ? 255 : 110), NVG_ALIGN_LEFT);
            rdText(vg, mm2px(9.f), y, used ? upperTrim(sc ? (sc->name.empty() ? string::f("SCENE %d", i + 1) : sc->name) : std::string("INTRO"), 14) : "- CLICK A BOX -",
                   used ? 7.f : 5.5f, rdz::ramp(i, used ? 255 : 90), NVG_ALIGN_LEFT);
            uint8_t mask = sc ? sc->mask : (uint8_t) (1 << i);
            for (int c = 0; c < 8; c++) {
                bool on = mask >> c & 1;
                rdBox(vg, colX0 + c * colW, y - mm2px(2.2f), mm2px(9.f), mm2px(4.4f), on ? rdz::ramp(c) : nvgRGBA(26, 18, 8, 255),
                      rdz::ramp(c, on ? 255 : 80), 0.7f, 1.2f);
            }
            rdText(vg, W - mm2px(4.f), y, string::f("%d", sc ? sc->bars : 8), 7.f, oled::amber(used ? 220 : 90), NVG_ALIGN_RIGHT);
        }
    }
    else if (t == 2) {   // ---- BANKS: U1-U8 and what is in them ----
        for (int b = 0; b < 8; b++) {
            float y = mm2px(OLED_TOP + b * 7.2f) + mm2px(2.6f);
            int n = 0;
            std::string names;
            for (int c = 0; c < RD_SPOTS; c++) if (m && m->slots[b][c].used) {
                n++;
                if (names.size() < 60) names += (names.empty() ? "" : " / ") + upperTrim(m->slots[b][c].name, 10);
            }
            int curRow = cur >= 0 ? rdBankOf(cur) : -1;
            if (b == curRow) rdBox(vg, 3.f, y - mm2px(3.2f), W - 6.f, mm2px(6.4f), nvgRGBA(255, 255, 255, 16), nvgRGBA(0, 0, 0, 0), 0.f, 2.f);
            rdText(vg, mm2px(3.f), y, string::f("BANK %d", b + 1), 7.f, b == curRow ? nvgRGB(255, 255, 255) : oled::amber(), NVG_ALIGN_LEFT);
            for (int c = 0; c < RD_SPOTS; c++) {
                bool used = m && m->slots[b][c].used;
                rdBox(vg, mm2px(15.f + c * 1.7f), y - mm2px(1.4f), mm2px(1.4f), mm2px(2.8f), used ? rdz::ramp(c / 2) : nvgRGBA(26, 18, 8, 255), rdz::ramp(c / 2, used ? 255 : 70), 0.3f, 0.6f);
            }
            rdText(vg, mm2px(43.f), y, n ? names : "EMPTY", 6.5f, oled::amber(n ? 230 : 90), NVG_ALIGN_LEFT);
            rdText(vg, W - mm2px(4.f), y, string::f("%d / 16", n), 6.5f, oled::amber(n ? 220 : 90), NVG_ALIGN_RIGHT);
        }
    }
    else {   // ---- PATCH: modules and cables RANDROYD sees ----
        const RdInfo* in = info;
        RdInfo demo;
        if (!m || !in) in = &demo;
        float y = mm2px(OLED_TOP);
        rdText(vg, mm2px(3.f), y, string::f("%d MODULES   %d FREE INPUTS   %d ROLLED   %d YOURS", (int) in->mods.size(), in->freeIns,
                                            in->mine, (int) in->cabs.size() - in->mine), 6.f, oled::dim(), NVG_ALIGN_LEFT);
        const float line = mm2px(3.4f);
        float top = y + mm2px(3.f), bot = H - mm2px(6.f);
        int fit = std::max(1, (int) ((bot - top) / line)) * 2;   // two columns
        int maxScroll = std::max(0, ((int) in->cabs.size() - fit + 1) / 2);
        if (scroll > maxScroll) scroll = (float) maxScroll;
        int first = (int) scroll * 2;
        nvgSave(vg);
        nvgScissor(vg, 0.f, top - line * 0.5f, W, bot - top + line * 0.5f);
        for (int i = first; i < (int) in->cabs.size() && i < first + fit; i++) {
            const RdInfo::Cab& c = in->cabs[i];
            int k = i - first;
            float cx = (k % 2) ? W * 0.5f + mm2px(1.f) : mm2px(3.f);
            float cy = top + (k / 2) * line + line * 0.5f;
            NVGcolor col = c.lockTrack >= 0 ? rdz::ramp(c.lockTrack) : (c.mine ? oled::amber() : rdz::green());
            nvgBeginPath(vg);
            if (c.lockTrack >= 0) nvgRect(vg, cx + mm2px(0.2f), cy - mm2px(0.6f), mm2px(1.2f), mm2px(1.2f));
            else nvgCircle(vg, cx + mm2px(0.8f), cy, mm2px(0.6f));
            nvgFillColor(vg, col); nvgFill(vg);
            rdText(vg, cx + mm2px(2.2f), cy, c.from + "  >  " + c.to + (c.lockTrack >= 0 ? string::f("  [T%d]", c.lockTrack + 1) : ""), 6.f, col, NVG_ALIGN_LEFT);
        }
        nvgRestore(vg);
        if (in->cabs.empty()) rdText(vg, W * 0.5f, (top + bot) * 0.5f, "NO CABLES YET: PRESS ROLL", 8.f, oled::dim(), NVG_ALIGN_CENTER);
    }

    // footer: notice or help
    std::string foot;
    if (m && m->noticeTime > 0.f) foot = m->notice;
    else if (t == 0) foot = "A-H = THE ARRANGEMENT   ROLL = NEW SOUNDS + ARRANGEMENT   CLICK A PART, THEN PARK = MUTE IT   UNDO = STEP BACK";
    else if (t == 1) foot = "CLICK BOXES = WHICH TRACKS PLAY   CLICK BARS = LENGTH   SHIFT + SCENE BUTTON = SAVE";
    else if (t == 2) foot = "EVERY SAVED TRACK, 8 PADS PER ROW   CLICK A PAD ON THE GRID TO PLAY IT";
    else foot = "SQUARE = MIXER CHANNEL LOCKED TO A TRACK   AMBER = ROLLED   GREEN = YOURS   WHEEL = SCROLL";
    if (m && m->noticeTime > 0.f) {   // a message: a lit banner, easy to see
        float bh = mm2px(6.f), by = H - bh - mm2px(1.f);
        float a = std::min(1.f, m->noticeTime / 0.4f);
        rdBox(vg, mm2px(2.f), by, W - mm2px(4.f), bh, nvgRGBA(20, 60, 12, (int) (235 * a)), rdz::green((int) (255 * a)), 0.9f, 2.f);
        rdText(vg, W * 0.5f, by + bh * 0.5f, foot, 7.5f, rdz::green((int) (255 * a)), NVG_ALIGN_CENTER);
    }
    else rdText(vg, W * 0.5f, H - mm2px(2.4f), foot, 5.6f, oled::dim(), NVG_ALIGN_CENTER);
    Widget::drawLayer(args, layer);
}

void RdOled::onButton(const ButtonEvent& e) {
    if (!module || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT) { OpaqueWidget::onButton(e); return; }
    SynTHBloKRandroyd* m = module;
    int t = tab();
    float W = box.size.x;
    if (t == 0) {
        int i = (int) std::floor((e.pos.y - mm2px(OLED_TOP - 1.f)) / mm2px(7.2f));
        if (i >= 0 && i < 8) {
            m->selTrack = i;
            if (owner && owner->laneState(i) == 2) owner->parkTrack(i, false);   // a parked lane: wake it
            e.consume(this);
            return;
        }
    }
    else if (t == 1) {
        int i = (int) std::floor((e.pos.y - mm2px(OLED_TOP + 3.f - 0.8f)) / mm2px(6.6f));
        if (i >= 0 && i < 8) {
            SynTHBloKRandroyd::Scene& sc = m->sets[m->set].scenes[i];
            float colX0 = mm2px(44.f), colW = mm2px(10.5f);
            int c = (int) std::floor((e.pos.x - colX0) / colW);
            if (e.pos.x >= colX0 && c >= 0 && c < 8) {
                sc.mask ^= (uint8_t) (1 << c);
                sc.used = true;
                if (sc.name.empty()) sc.name = string::f("SCENE %d", i + 1);
                m->sets[m->set].used = true;
            }
            else if (e.pos.x > W - mm2px(12.f)) {
                static const int bars[6] = {1, 2, 4, 8, 16, 32};
                int k = 0;
                while (k < 6 && bars[k] != sc.bars) k++;
                sc.bars = bars[(k + 1) % 6];
            }
            else if (sc.used) {
                // rename
                ui::Menu* menu = createMenu();
                menu->addChild(createMenuLabel(string::f("SCENE %d NAME", i + 1)));
                struct NameField : ui::TextField {
                    SynTHBloKRandroyd::Scene* sc = NULL;
                    void onChange(const ChangeEvent& ev) override { if (sc) sc->name = getText(); ui::TextField::onChange(ev); }
                };
                NameField* f = new NameField;
                f->sc = &sc;
                f->box.size.x = 160.f;
                f->setText(sc.name);
                f->selectAll();
                menu->addChild(f);
                menu->addChild(createMenuItem("Clear this scene", "", [=, &sc]() { sc = SynTHBloKRandroyd::Scene(); }));
            }
            e.consume(this);
            return;
        }
    }
    else if (t == 2) {
        int b = (int) std::floor((e.pos.y - mm2px(OLED_TOP - 1.f)) / mm2px(7.2f));
        if (b >= 0 && b < 8) { m->bank = b; m->say(string::f("BANK U%d LOADED", b + 1)); e.consume(this); return; }
    }
    OpaqueWidget::onButton(e);
}

Model* modelSynTHBloKRandroyd = createModel<SynTHBloKRandroyd, SynTHBloKRandroydWidget>("Randroyd");
