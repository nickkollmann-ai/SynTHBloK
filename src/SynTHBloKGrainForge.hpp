#pragma once
// SynTHBloK GRAIN-FORGE: the module and its panel parts, shared with MATRIX FORGE (which builds on this module).
#include "plugin.hpp"
#include "SynTHBloKGrainForge_dsp.hpp"
#include "KnobTicks.hpp"
#include <atomic>
#include <functional>
#include <string>

namespace {

using namespace gforge;

static const char* kNote[12] = {"A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#"};
static const int kRndDivCount = 6, kRndGlideCount = 5;
static const char* kRndDivName[kRndDivCount] = {"1 BEAT", "2 BEATS", "1 BAR", "2 BARS", "4 BARS", "8 BARS"};
static const int kRndDivSteps[kRndDivCount] = {4, 8, 16, 32, 64, 128};
static const char* kRndGlideName[kRndGlideCount] = {"SNAP", "FAST", "BEAT", "BAR", "DRIFT"};
enum { TRIG_BEAT, TRIG_STEP, TRIG_PULSE, TRIG_GLITCH, TRIG_SEQ, TRIG_GATE, kTrigCount };
static const char* kTrigName[kTrigCount] = {"BEAT", "STEP", "PULSE", "GLITCH", "SEQ", "GATE"};

// Presets: every stage's settings, in param order. Switch values are slot indexes (see the name tables).
// Levels and throw steps are left alone. The last menu entry, SMART RANDOM, builds one with the randomizer.
struct GFPreset {
    const char* name;
    float sh[7];   // ON PATTERN AMOUNT REV OCTAVE LENGTH MIX
    float gl[7];   // ON CHANCE ROLL REV STOP CRUSH MIX
    float rb[7];   // ON ROOT CHORD VOWEL RING RES MIX
    float pu[8];   // ON CUTOFF RES PATTERN DEPTH MIX MODE, DECAY
    float dl[7];   // ON TIME FDBK WARP ROUTE TONE ECHO
    float dw;      // DRY / WET
};
static const GFPreset kPreset[] = {
    {"INIT",         {1, 0, .35f, .15f, 0, 2, 1}, {1, .2f, 3, .2f, .1f, 0, 1},   {1, 12, 3, .5f, .05f, .55f, .25f},
                     {1, .55f, .35f, 0, .5f, .6f, 0, .35f}, {1, 8, .4f, 0, .3f, .6f, .6f}, .45f},
    {"DUB ECHO",     {0, 0, .2f, 0, 0, 2, 1},     {1, .1f, 1, .1f, .25f, 0, .6f}, {0, 12, 1, .5f, 0, .5f, .2f},
                     {1, .45f, .3f, 2, .4f, .5f, 0, .4f},   {1, 8, .6f, 0, .2f, .35f, .75f}, .5f},
    {"DAFT VOCODER", {0, 0, .2f, 0, 0, 2, 1},     {1, .15f, 3, 0, 0, 0, .5f},     {1, 15, 5, .4f, .05f, .6f, .85f},
                     {1, .6f, .3f, 1, .35f, .4f, 0, .4f},   {1, 6, .3f, 0, .5f, .6f, .35f}, .7f},
    {"TRANCE GATE",  {0, 0, .2f, 0, 0, 2, 1},     {0, .1f, 3, 0, 0, 0, .5f},      {0, 12, 3, .5f, 0, .5f, .2f},
                     {1, .7f, .45f, 3, .75f, 1, 0, .25f},   {1, 8, .45f, 0, .3f, .6f, .4f}, .8f},
    {"STUTTER ROLL", {1, 6, .5f, .25f, 0, 0, 1},  {1, .45f, 3, .3f, .2f, 0, 1},   {0, 12, 3, .5f, 0, .5f, .2f},
                     {0, .55f, .35f, 0, .5f, .6f, 0, .35f}, {1, 3, .3f, 0, 0, .6f, .3f}, .6f},
    {"HALF TIME",    {1, 3, .6f, 0, 0, 2, 1},     {1, .1f, 0, 0, .2f, 0, .6f},    {0, 12, 3, .5f, 0, .5f, .2f},
                     {0, .55f, .35f, 0, .5f, .6f, 0, .35f}, {1, 9, .35f, 0, .2f, .5f, .4f}, .9f},
    {"LO-FI TAPE",   {1, 1, .4f, .35f, .15f, 1, 1}, {1, .3f, 1, .2f, .3f, .3f, 1}, {0, 12, 3, .5f, 0, .5f, .2f},
                     {1, .4f, .2f, 0, .3f, .6f, 0, .5f},    {1, 9, .35f, -12, .2f, .35f, .4f}, .6f},
    {"SHIMMER",      {0, 0, .2f, 0, 0, 2, 1},     {0, .1f, 3, 0, 0, 0, .5f},      {1, 12, 4, .7f, 0, .4f, .3f},
                     {0, .55f, .35f, 0, .5f, .6f, 0, .35f}, {1, 10, .6f, 12, .3f, .75f, .6f}, .5f},
    {"ROBO DUB",     {0, 0, .2f, 0, 0, 2, 1},     {0, .1f, 3, 0, 0, 0, .5f},      {1, 7, 3, .3f, .1f, .6f, .4f},
                     {1, .5f, .5f, 4, .5f, .6f, 1, .4f},    {1, 8, .55f, 7, .5f, .5f, .6f}, .6f},
};
static const int kPresetCount = sizeof(kPreset) / sizeof(kPreset[0]);   // index kPresetCount = SMART RANDOM

struct SynTHBloKGrainForge : Module {
    enum ParamIds {
        GL_ON_PARAM, GL_CHANCE_PARAM, GL_ROLL_PARAM, GL_REV_PARAM, GL_STOP_PARAM, GL_CRUSH_PARAM, GL_MIX_PARAM,
        // PULSE took over the old STRETCH slots (saved patches reset them, see fromJson)
        PU_ON_PARAM, PU_CUTOFF_PARAM, PU_RES_PARAM, PU_PATTERN_PARAM, PU_DEPTH_PARAM, PU_MIX_PARAM, PU_MODE_PARAM,
        RB_ON_PARAM, RB_ROOT_PARAM, RB_CHORD_PARAM, RB_VOWEL_PARAM, RB_RING_PARAM, RB_Q_PARAM, RB_MIX_PARAM,
        DL_ON_PARAM, DL_TIME_PARAM, DL_FDBK_PARAM, DL_WARP_PARAM, DL_ROUTE_PARAM, DL_TONE_PARAM, DL_MIX_PARAM,
        ENUMS(DL_STEP_PARAM, 8),
        IN_GAIN_PARAM, DRYWET_PARAM, OUT_GAIN_PARAM,
        ENUMS(DL_STEP2_PARAM, 8),     // throw steps 9-16 (added last so saved patches keep their ids)
        PU_DECAY_PARAM,
        SH_ON_PARAM, SH_PATTERN_PARAM, SH_AMOUNT_PARAM, SH_REV_PARAM, SH_PITCH_PARAM, SH_LEN_PARAM, SH_MIX_PARAM,
        ENUMS(RND_PARAM, 5),          // per-stage randomizer, in panel order: SHUFFLE GLITCH ROBOT PULSE DELAY
        RND_DIV_PARAM, RND_GLIDE_PARAM, RND_FREEZE_PARAM,
        PRESET_PARAM,                 // turning it (or clicking the PRESET screen) loads that preset
        FILTER_PARAM,                 // master DJ filter on the final mix
        FLT_RES_PARAM, FLT_ATK_PARAM, FLT_DEC_PARAM, FLT_SUS_PARAM, FLT_REL_PARAM,
        FLT_ENV_PARAM,                // ADSR depth on the cutoff, + up / - down; retriggers every beat
        FLT_STAGES_PARAM,             // 1 / 2 / 3 filter stages (12 / 24 / 36 dB)
        FLT_MODE_PARAM,               // LP / BP / HP
        FLT_AMP_PARAM,                // how much the filter ADSR also shapes the level (0 = none, 1 = full pluck)
        FLT_TRIG_PARAM,               // what fires the filter ADSR: BEAT STEP PULSE GLITCH SEQ GATE
        FLT_BYPASS_PARAM,             // takes the ADSR + TRIG/SEQ off the filter (crossfaded); the filter stays
        NUM_PARAMS
    };
    enum InputIds { IN_L_INPUT, IN_R_INPUT, CLOCK_INPUT, RESET_INPUT, OPEN_INPUT, GLITCH_INPUT, GATE_INPUT, NUM_INPUTS };
    enum OutputIds { OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };
    enum LightIds { ENUMS(ADSR_LED, 4 * 3), NUM_LIGHTS };

    Shuffle shuffle;
    Glitch glitch;
    Robot robot;
    Pulse pulse;
    WarpDelay delay;
    DjFilter master;
    Adsr fltEnv;
    float fltGate = 0.f;               // samples left before the envelope's release
    float fltEnvOut = 0.f;             // for the SEQ screen
    float envS1 = 0.f, envS2 = 0.f;    // envelope corner smoothing
    float stageLvl[4] = {};            // ADSR LED activity, falls off over 150 ms
    // the main synth's LED ramp: knob low = amber/yellow .. high = deep red
    static void ledRamp(float w, float& r, float& g, float& b) {
        static const float C[10][3] = {
            {1.00f, 0.72f, 0.00f}, {1.00f, 0.55f, 0.00f}, {1.00f, 0.38f, 0.00f},
            {1.00f, 0.20f, 0.00f}, {1.00f, 0.04f, 0.00f}, {0.70f, 0.00f, 0.00f},
            {0.28f, 0.00f, 0.00f}, {0.16f, 0.00f, 0.00f}, {0.08f, 0.00f, 0.00f}, {0.03f, 0.00f, 0.00f}};
        float p = clamp(w, 0.f, 1.f) * 9.f;
        int i = std::min((int)p, 8);
        float f = p - (float)i;
        r = C[i][0] + (C[i + 1][0] - C[i][0]) * f;
        g = C[i][1] + (C[i + 1][1] - C[i][1]) * f;
        b = C[i][2] + (C[i + 1][2] - C[i][2]) * f;
    }
    dsp::SchmittTrigger clkTrig, rstTrig, glitchTrig, gateTrig;

    // ---- filter SEQ: 16 steps (on + height); height = a cutoff offset snapped to ROBOT's key, so with
    // RES up the filter whistles an arpeggio. Edited on its screen (UI thread), read here; plain values.
    static const int kSeqMax = 16;
    bool seqOn[kSeqMax] = {};
    float seqLvl[kSeqMax] = {};
    int seqLen = 16;
    int seqPos = -1;
    float seqOff = 0.f, seqOffSm = 0.f;   // cutoff offset of the current step (0..~0.2), and slewed
    void seqDefault() {
        static const float lv[8] = {0.f, .5f, .3f, .7f, 0.f, .9f, .5f, .3f};
        for (int i = 0; i < kSeqMax; i++) { seqOn[i] = i % 2 == 0 || i % 8 == 3; seqLvl[i] = lv[i % 8]; }
        seqLen = 16;
    }
    void seqRandom() {   // musical: downbeat on, about 60% density, heights from a few levels
        static const float lv[6] = {0.f, .25f, .45f, .55f, .75f, 1.f};
        for (int i = 0; i < kSeqMax; i++) {
            seqOn[i] = i == 0 || random::uniform() < (i % 2 == 0 ? 0.75f : 0.4f);
            seqLvl[i] = lv[std::min((int)(random::uniform() * 6), 5)];
        }
    }
    // height 0..1 -> 0..24 semitones, snapped to the scale of ROBOT's key, in cutoff-knob units
    float seqNote(float h) {
        static const int maj[7] = {0, 2, 4, 5, 7, 9, 11}, mnr[7] = {0, 2, 3, 5, 7, 8, 10}, pent[5] = {0, 3, 5, 7, 10};
        const int* sc = pent; int n = 5;
        if (homeChord == 2 || homeChord == 5) { sc = maj; n = 7; }
        else if (homeChord == 3 || homeChord == 6) { sc = mnr; n = 7; }
        int deg = (int)std::round(clamp(h, 0.f, 1.f) * (2 * n));   // two octaves of scale degrees
        int semi = 12 * (deg / n) + sc[deg % n];
        return semi / (12.f * std::log2(1000.f));
    }

    static const int kThrowSteps = 16;
    int step = -1;                    // 0..15, for the throw buttons
    float sinceClock = 1e9f, clockPeriod = 0.f, internalTimer = 0.f;
    float meterL = 0.f, meterR = 0.f;
    std::string glitchEvent = "-";    // shown on the GLITCH display
    float sm[NUM_PARAMS] = {};         // smoothed knob values
    bool smInit = false;

    // ---- smart randomizer: each lit RND stage picks new settings on every bar line, from musical
    // choices only (straight/dotted grid, in-key chord moves, safe ranges). Mix levels stay yours.
    // Switch-type settings jump on the downbeat; knobs glide there over about a beat.
    enum { R_SH, R_GL, R_RB, R_PU, R_DL };
    struct RndKnob { int id, stage; };
    static constexpr int kRndKnobs = 18;
    const RndKnob rndKnob[kRndKnobs] = {
        {SH_AMOUNT_PARAM, R_SH}, {SH_REV_PARAM, R_SH}, {SH_PITCH_PARAM, R_SH},
        {GL_CHANCE_PARAM, R_GL}, {GL_REV_PARAM, R_GL}, {GL_STOP_PARAM, R_GL}, {GL_CRUSH_PARAM, R_GL},
        {RB_VOWEL_PARAM, R_RB}, {RB_RING_PARAM, R_RB}, {RB_Q_PARAM, R_RB},
        {PU_CUTOFF_PARAM, R_PU}, {PU_RES_PARAM, R_PU}, {PU_DEPTH_PARAM, R_PU}, {PU_DECAY_PARAM, R_PU},
        {DL_FDBK_PARAM, R_DL}, {DL_WARP_PARAM, R_DL}, {DL_ROUTE_PARAM, R_DL}, {DL_TONE_PARAM, R_DL}};
    long clk = -1;                     // steps since reset, so cycles always start on the grid
    float rndTgt[NUM_PARAMS] = {};
    bool rndGlide[NUM_PARAMS] = {};
    bool rndWasOn[5] = {};
    int bar = 0;
    int homeRoot = 12, homeChord = 3;  // ROBOT's key, taken when its RND is switched on
    Rng rng{0x5eedu};
    float rnd(float lo, float hi) { return lo + (hi - lo) * rng.next(); }
    int pick(const int* v, int n) { return v[std::min((int)(rng.next() * n), n - 1)]; }
    void aim(int id, float lo, float hi) { rndTgt[id] = rnd(lo, hi); rndGlide[id] = true; }
    void jump(int id, int v) { params[id].setValue((float)v); rndGlide[id] = false; }

    std::atomic<int> presetRequest{-1};   // UI asks to (re)load a preset, e.g. SMART RANDOM again
    int lastPreset = -1;               // PRESET knob position last seen by process()

    void applyPreset(int idx) {
        for (int i = 0; i < NUM_PARAMS; i++) rndGlide[i] = false;
        if (idx >= kPresetCount) {   // SMART RANDOM: a random key, then one randomizer pass over every stage, snapped
            static const int chords[4] = {2, 3, 5, 6};
            homeRoot = 3 + (int)(rng.next() * 13) % 13;   // C2..C3
            homeChord = pick(chords, 4);
            bar = 0;
            randomizeBar(31, true);
            for (const RndKnob& k : rndKnob) if (rndGlide[k.id]) { params[k.id].setValue(rndTgt[k.id]); rndGlide[k.id] = false; }
            return;
        }
        const GFPreset& p = kPreset[idx];
        for (int i = 0; i < 7; i++) {
            params[SH_ON_PARAM + i].setValue(p.sh[i]);
            params[GL_ON_PARAM + i].setValue(p.gl[i]);
            params[RB_ON_PARAM + i].setValue(p.rb[i]);
            params[PU_ON_PARAM + i].setValue(p.pu[i]);
            params[DL_ON_PARAM + i].setValue(p.dl[i]);
        }
        params[PU_DECAY_PARAM].setValue(p.pu[7]);
        params[DRYWET_PARAM].setValue(p.dw);
        homeRoot = (int)p.rb[1]; homeChord = (int)p.rb[2];   // RND on ROBOT stays in the preset's key
        bar = 0;
    }

    // mask: which stages to pick for (bit = R_SH..R_DL). all = SMART RANDOM preset: delay time changes too.
    void randomizeBar(int mask, bool all = false) {
        auto want = [&](int s) { return (mask >> s) & 1; };
        // one shared feel per bar, so PULSE and the delay land on the same grid
        bool dotted = rng.next() < 0.3f;
        if (want(R_SH)) {
            jump(SH_PATTERN_PARAM, (int)(rng.next() * kShufCount) % kShufCount);
            static const int len[4] = {0, 1, 2, 2};
            jump(SH_LEN_PARAM, pick(len, 4));
            aim(SH_AMOUNT_PARAM, 0.15f, 0.6f); aim(SH_REV_PARAM, 0.f, 0.35f); aim(SH_PITCH_PARAM, 0.f, 0.25f);
        }
        if (want(R_GL)) {
            static const int roll[5] = {0, 1, 1, 3, 5};   // 1/4 1/8 1/16 1/32: straight only, no triplets
            jump(GL_ROLL_PARAM, pick(roll, 5));
            aim(GL_CHANCE_PARAM, 0.1f, 0.45f); aim(GL_REV_PARAM, 0.f, 0.4f);
            aim(GL_STOP_PARAM, 0.f, 0.3f);     aim(GL_CRUSH_PARAM, 0.f, 0.3f);
        }
        if (want(R_RB)) {
            // move inside the key of the root/chord you set: I IV V vi ii (major), i iv v III VI VII (minor)
            struct Deg { int off, c1, c2; };
            static const Deg maj[6] = {{0, 2, 5}, {5, 2, 5}, {7, 2, 4}, {9, 3, 6}, {2, 3, 6}, {0, 2, 2}};
            static const Deg min[6] = {{0, 3, 6}, {5, 3, 6}, {7, 3, 4}, {3, 2, 5}, {8, 2, 5}, {10, 2, 2}};
            static const Deg neu[4] = {{0, 1, 0}, {5, 1, 4}, {7, 1, 4}, {0, 4, 1}};
            const Deg* d; int n;
            if (homeChord == 2 || homeChord == 5) { d = maj; n = 6; }
            else if (homeChord == 3 || homeChord == 6) { d = min; n = 6; }
            else { d = neu; n = 4; }
            Deg g = (bar % 4 == 0) ? d[0] : d[std::min((int)(rng.next() * n), n - 1)];   // home chord every 4 bars
            int r = homeRoot + g.off;
            if (r > 24) r -= 12;
            jump(RB_ROOT_PARAM, r);
            jump(RB_CHORD_PARAM, rng.next() < 0.5f ? g.c1 : g.c2);
            aim(RB_VOWEL_PARAM, 0.1f, 0.9f); aim(RB_RING_PARAM, 0.f, 0.15f); aim(RB_Q_PARAM, 0.3f, 0.7f);
        }
        if (want(R_PU)) {
            static const int straight[6] = {0, 1, 2, 3, 6, 7}, dot[2] = {4, 5};
            jump(PU_PATTERN_PARAM, dotted ? pick(dot, 2) : pick(straight, 6));
            static const int mode[4] = {0, 0, 1, 2};
            jump(PU_MODE_PARAM, pick(mode, 4));
            aim(PU_CUTOFF_PARAM, 0.35f, 0.8f); aim(PU_RES_PARAM, 0.15f, 0.55f);
            aim(PU_DEPTH_PARAM, 0.3f, 0.75f);  aim(PU_DECAY_PARAM, 0.15f, 0.6f);
        }
        if (want(R_DL)) {
            if (all || (bar % 2 == 0 && step == 0)) {   // time and warp: only on a bar line, every other cycle, so tails ring out
                static const int straight[4] = {3, 6, 9, 11}, dot[3] = {5, 8, 10};   // 1/16 1/8 1/4 1/2 | 1/16D 1/8D 1/4D
                jump(DL_TIME_PARAM, dotted ? pick(dot, 3) : pick(straight, 4));
                static const float warp[8] = {0.f, 0.f, 0.f, 12.f, -12.f, 7.f, -5.f, 5.f};   // octaves, fifths, fourths
                rndTgt[DL_WARP_PARAM] = warp[std::min((int)(rng.next() * 8), 7)];
                rndGlide[DL_WARP_PARAM] = true;
            }
            aim(DL_FDBK_PARAM, 0.2f, 0.6f); aim(DL_ROUTE_PARAM, 0.f, 0.5f); aim(DL_TONE_PARAM, 0.3f, 0.75f);
        }
    }

    // Clean up to 0.8, then a soft knee that never passes 1.0.
    static float softLimit(float x) {
        float a = std::fabs(x);
        if (a <= 0.8f) return x;
        return (x < 0.f ? -1.f : 1.f) * (0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f));
    }

    SynTHBloKGrainForge() : SynTHBloKGrainForge(0, 0, 0, 0) {}
    // A module built on GRAIN-FORGE (MATRIX FORGE) adds its own params / ports / lights after these.
    SynTHBloKGrainForge(int extraParams, int extraInputs, int extraOutputs, int extraLights) {
        config(NUM_PARAMS + extraParams, NUM_INPUTS + extraInputs, NUM_OUTPUTS + extraOutputs, NUM_LIGHTS + extraLights);
        seqDefault();
        const std::vector<std::string> offOn = {"Off", "On"};
        configSwitch(GL_ON_PARAM, 0.f, 1.f, 1.f, "GLITCH", offOn);
        configParam(GL_CHANCE_PARAM, 0.f, 1.f, 0.2f, "Glitch chance per step", "%", 0.f, 100.f);
        configSwitch(GL_ROLL_PARAM, 0.f, kRollCount - 1, 3.f, "Roll size (rolls start on the beat and last a beat)",
                     std::vector<std::string>(kRollName, kRollName + kRollCount));
        configParam(GL_REV_PARAM, 0.f, 1.f, 0.2f, "Reverse amount", "%", 0.f, 100.f);
        configParam(GL_STOP_PARAM, 0.f, 1.f, 0.1f, "Tape-stop amount", "%", 0.f, 100.f);
        configParam(GL_CRUSH_PARAM, 0.f, 1.f, 0.f, "Crush (bits + sample rate)", "%", 0.f, 100.f);
        configParam(GL_MIX_PARAM, 0.f, 1.f, 1.f, "Glitch mix", "%", 0.f, 100.f);
        configSwitch(SH_ON_PARAM, 0.f, 1.f, 1.f, "SHUFFLE", offOn);
        configSwitch(SH_PATTERN_PARAM, 0.f, kShufCount - 1, 0.f, "Shuffle pattern",
                     std::vector<std::string>(kShufName, kShufName + kShufCount));
        configParam(SH_AMOUNT_PARAM, 0.f, 1.f, 0.35f, "Shuffle amount (chance a step is moved)", "%", 0.f, 100.f);
        configParam(SH_REV_PARAM, 0.f, 1.f, 0.15f, "Reversed slices", "%", 0.f, 100.f);
        configParam(SH_PITCH_PARAM, 0.f, 1.f, 0.f, "Octave slices (up or down)", "%", 0.f, 100.f);
        configSwitch(SH_LEN_PARAM, 0.f, 2.f, 2.f, "Shuffle length", std::vector<std::string>(kShufLenName, kShufLenName + 3));
        configParam(SH_MIX_PARAM, 0.f, 1.f, 1.f, "Shuffle mix", "%", 0.f, 100.f);
        configSwitch(PU_ON_PARAM, 0.f, 1.f, 1.f, "PULSE", offOn);
        configParam(PU_CUTOFF_PARAM, 0.f, 1.f, 0.55f, "Cutoff", " Hz", 600.f, 30.f);
        configParam(PU_RES_PARAM, 0.f, 1.f, 0.35f, "Resonance", "%", 0.f, 100.f);
        configSwitch(PU_PATTERN_PARAM, 0.f, kPulseCount - 1, 0.f, "Pulse pattern",
                     std::vector<std::string>(kPulseName, kPulseName + kPulseCount));
        configParam(PU_DEPTH_PARAM, 0.f, 1.f, 0.5f, "Depth (gate + filter sweep per hit)", "%", 0.f, 100.f);
        configParam(PU_DECAY_PARAM, 0.f, 1.f, 0.35f, "Decay (click .. 4 steps)", "%", 0.f, 100.f);
        configParam(PU_MIX_PARAM, 0.f, 1.f, 0.6f, "Pulse mix", "%", 0.f, 100.f);
        configSwitch(PU_MODE_PARAM, 0.f, 2.f, 0.f, "Filter mode", {"Low-pass", "Band-pass", "High-pass"});
        configSwitch(RB_ON_PARAM, 0.f, 1.f, 1.f, "ROBOT", offOn);
        configSwitch(RB_ROOT_PARAM, 0.f, 24.f, 12.f, "Robot root",
                     {"A1", "A#1", "B1", "C2", "C#2", "D2", "D#2", "E2", "F2", "F#2", "G2", "G#2",
                      "A2", "A#2", "B2", "C3", "C#3", "D3", "D#3", "E3", "F3", "F#3", "G3", "G#3", "A3"});
        configSwitch(RB_CHORD_PARAM, 0.f, 7.f, 3.f, "Robot chord", {"Harmonics", "Fifth", "Major", "Minor", "Sus4", "Major 7", "Minor 7", "Cluster"});
        configParam(RB_VOWEL_PARAM, 0.f, 1.f, 0.5f, "Vowel (A - E - I - O - U)");
        configParam(RB_RING_PARAM, 0.f, 1.f, 0.05f, "Ring-mod", "%", 0.f, 100.f);
        configParam(RB_Q_PARAM, 0.f, 1.f, 0.55f, "Resonance (vowel sharpness)", "%", 0.f, 100.f);
        configParam(RB_MIX_PARAM, 0.f, 1.f, 0.25f, "Robot mix", "%", 0.f, 100.f);
        configSwitch(DL_ON_PARAM, 0.f, 1.f, 1.f, "WARP DELAY", offOn);
        configSwitch(DL_TIME_PARAM, 0.f, kDelayCount - 1, 8.f, "Delay time",
                     std::vector<std::string>(kDelayName, kDelayName + kDelayCount));
        configParam(DL_FDBK_PARAM, 0.f, 1.f, 0.4f, "Feedback", "%", 0.f, 100.f);
        configParam(DL_WARP_PARAM, -12.f, 12.f, 0.f, "Warp (pitch shift per repeat)", " st");
        configParam(DL_ROUTE_PARAM, 0.f, 1.f, 0.3f, "Route repeats through ROBOT + PULSE", "%", 0.f, 100.f);
        configParam(DL_TONE_PARAM, 0.f, 1.f, 0.6f, "Repeat tone (dark -> bright)", "%", 0.f, 100.f);
        configParam(DL_MIX_PARAM, 0.f, 1.f, 0.6f, "Echo level", "%", 0.f, 100.f);
        for (int i = 0; i < kThrowSteps; i++)
            configSwitch(throwParam(i), 0.f, 1.f, 1.f, "Throw step " + std::to_string(i + 1) + " into the delay", {"Dry", "Throw"});
        static const char* rndName[5] = {"SHUFFLE", "GLITCH", "ROBOT", "PULSE", "WARP DELAY"};
        for (int i = 0; i < 5; i++)
            configSwitch(RND_PARAM + i, 0.f, 1.f, 0.f, std::string(rndName[i]) + " smart random (new musical settings every bar)", offOn);
        configSwitch(RND_DIV_PARAM, 0.f, kRndDivCount - 1, 2.f, "Random cycle (new picks every)",
                     std::vector<std::string>(kRndDivName, kRndDivName + kRndDivCount));
        configSwitch(RND_GLIDE_PARAM, 0.f, kRndGlideCount - 1, 2.f, "Random glide (how fast knobs travel)",
                     std::vector<std::string>(kRndGlideName, kRndGlideName + kRndGlideCount));
        configSwitch(RND_FREEZE_PARAM, 0.f, 1.f, 0.f, "Random freeze (hold the current picks)", offOn);
        std::vector<std::string> presetNames;
        for (int i = 0; i < kPresetCount; i++) presetNames.push_back(kPreset[i].name);
        presetNames.push_back("SMART RANDOM");
        configSwitch(PRESET_PARAM, 0.f, (float)kPresetCount, 0.f, "Preset", presetNames);
        paramQuantities[PRESET_PARAM]->randomizeEnabled = false;
        configParam(FILTER_PARAM, 0.f, 1.f, 1.f, "Filter cutoff (LP fully open / HP fully down = off)", " Hz", 1000.f, 20.f);
        configParam(FLT_AMP_PARAM, 0.f, 1.f, 0.f, "Filter env to level (pluck / gate)", "%", 0.f, 100.f);
        configSwitch(FLT_TRIG_PARAM, 0.f, (float)kTrigCount - 1, 0.f, "Filter env trigger",
                     {"Every beat", "Every step", "PULSE hits", "GLITCH events", "SEQ steps", "GATE input"});
        configSwitch(FLT_BYPASS_PARAM, 0.f, 1.f, 0.f, "Filter env bypass (ADSR + TRIG/SEQ off the filter)", {"Env on", "Env bypassed"});
        configSwitch(FLT_MODE_PARAM, 0.f, 2.f, 0.f, "Filter type", {"Low-pass", "Band-pass", "High-pass"});
        configParam(FLT_RES_PARAM, 0.f, 1.f, 0.f, "Filter resonance", "%", 0.f, 100.f);
        configParam(FLT_ATK_PARAM, 0.f, 1.f, 0.1f, "Filter env attack", " ms", 667.f, 3.f);
        configParam(FLT_DEC_PARAM, 0.f, 1.f, 0.45f, "Filter env decay", " ms", 333.f, 12.f);
        configParam(FLT_SUS_PARAM, 0.f, 1.f, 0.3f, "Filter env sustain", "%", 0.f, 100.f);
        configParam(FLT_REL_PARAM, 0.f, 1.f, 0.4f, "Filter env release", " ms", 333.f, 12.f);
        configParam(FLT_ENV_PARAM, -1.f, 1.f, 0.f, "Filter env depth (retriggers every beat; + sweeps cutoff up, - down)", "%", 0.f, 100.f);
        configSwitch(FLT_STAGES_PARAM, 0.f, 2.f, 0.f, "Filter stages", {"1 stage (12 dB)", "2 stages (24 dB)", "3 stages (36 dB)"});
        configParam(IN_GAIN_PARAM, 0.f, 2.f, 1.f, "Input gain", "%", 0.f, 100.f);
        configParam(DRYWET_PARAM, 0.f, 1.f, 0.45f, "Dry / wet", "%", 0.f, 100.f);
        configParam(OUT_GAIN_PARAM, 0.f, 1.5f, 1.f, "Output level", "%", 0.f, 100.f);
        configInput(IN_L_INPUT, "Left");
        configInput(IN_R_INPUT, "Right (left is copied here when unpatched)");
        configInput(CLOCK_INPUT, "Clock (16th notes; 120 BPM internal when unpatched)");
        configInput(RESET_INPUT, "Reset");
        configInput(OPEN_INPUT, "Open (gate): PULSE lets everything through while high");
        configInput(GLITCH_INPUT, "Glitch now (trigger)");
        configInput(GATE_INPUT, "Filter env gate (fires the ADSR when TRIG is GATE; holds while high)");
        configOutput(OUT_L_OUTPUT, "Left");
        configOutput(OUT_R_OUTPUT, "Right");
    }

    bool on(int id) { return params[id].getValue() > 0.5f; }
    int slot(int id) { return (int)std::round(params[id].getValue()); }
    static int throwParam(int i) { return i < 8 ? DL_STEP_PARAM + i : DL_STEP2_PARAM + (i - 8); }

    void onReset() override { step = -1; clk = -1; bar = 0; seqDefault(); }

    // Older patches stored older layouts.
    //   v1 (no data block): TIME as an index into 7 slots, ROLL as a 0..1 knob split into 5 zones.
    //   v2: ROLL as an index into 11 slots.
    //   v3: the current 13 TIME slots and 7 ROLL slots, with STRETCH where PULSE is now.
    //   v4: SHUFFLE and PULSE.
    //   v5: FILTER is a 0..1 cutoff with LP / BP / HP buttons (it was bipolar: LP left, HP right).
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "v5", json_true());
        json_object_set_new(root, "rbHomeRoot", json_integer(homeRoot));
        json_object_set_new(root, "rbHomeChord", json_integer(homeChord));
        json_object_set_new(root, "seqLen", json_integer(seqLen));
        json_t* sq = json_array();
        for (int i = 0; i < kSeqMax; i++) json_array_append_new(sq, json_real(seqOn[i] ? seqLvl[i] : -1.f - seqLvl[i]));   // <0 = off
        json_object_set_new(root, "seq", sq);
        return root;
    }
    void fromJson(json_t* rootJ) override {   // old patches have no "data" block, so dataFromJson never runs for them
        Module::fromJson(rootJ);
        json_t* d = json_object_get(rootJ, "data");
        if (json_t* hr = d ? json_object_get(d, "rbHomeRoot") : NULL) {   // keep ROBOT's key across reloads
            homeRoot = clamp((int)json_integer_value(hr), 0, 24);
            homeChord = clamp((int)json_integer_value(json_object_get(d, "rbHomeChord")), 0, 7);
            rndWasOn[R_RB] = on(RND_PARAM + R_RB);
        }
        json_t* sq = d ? json_object_get(d, "seq") : NULL;
        if (sq && json_array_size(sq) == (size_t)kSeqMax) {
            for (int i = 0; i < kSeqMax; i++) {
                float v = (float)json_number_value(json_array_get(sq, i));
                seqOn[i] = v >= 0.f; seqLvl[i] = clamp(v >= 0.f ? v : -1.f - v, 0.f, 1.f);
            }
            seqLen = clamp((int)json_integer_value(json_object_get(d, "seqLen")), 1, kSeqMax);
        } else seqDefault();
        lastPreset = slot(PRESET_PARAM);   // loading a patch must not re-apply its preset over its own settings
        int ver = !d ? 1 : json_object_get(d, "v5") ? 5 : json_object_get(d, "v4") ? 4 : json_object_get(d, "v3") ? 3 : json_object_get(d, "v2") ? 2 : 1;
        if (ver >= 5) return;
        // the old bipolar FILTER was clamped into 0..1, so read it from the patch: same cutoff, LP or HP
        json_t* ps0 = json_object_get(rootJ, "params");
        for (size_t i = 0; ps0 && i < json_array_size(ps0); i++) {
            json_t* p = json_array_get(ps0, i);
            if (json_integer_value(json_object_get(p, "id")) != FILTER_PARAM) continue;
            float amt = (float)json_number_value(json_object_get(p, "value")), a = std::fabs(amt);
            float fc = amt < 0.f ? 20000.f * std::pow(80.f / 20000.f, a) : 20.f * std::pow(400.f, a);
            bool off = a < 0.02f;
            params[FILTER_PARAM].setValue(off ? 1.f : std::log(fc / 20.f) / std::log(1000.f));
            params[FLT_MODE_PARAM].setValue(amt > 0.f && !off ? 2.f : 0.f);
        }
        if (ver >= 4) return;
        // STRETCH's settings were saved in PULSE's slots: start PULSE from its defaults instead
        for (int id = PU_ON_PARAM; id <= PU_MODE_PARAM; id++) params[id].setValue(paramQuantities[id]->getDefaultValue());
        if (ver == 3) return;
        // the saved ROLL value was clamped into the new switch's range, so read it from the patch itself
        float roll = 0.4f;
        json_t* ps = json_object_get(rootJ, "params");
        for (size_t i = 0; ps && i < json_array_size(ps); i++) {
            json_t* p = json_array_get(ps, i);
            if (json_integer_value(json_object_get(p, "id")) == GL_ROLL_PARAM)
                roll = (float)json_number_value(json_object_get(p, "value"));
        }
        if (ver == 2) {
            params[GL_ROLL_PARAM].setValue((float)kRollV2Index[clamp((int)std::round(roll), 0, 10)]);
            return;
        }
        int old = clamp((int)std::round(params[DL_TIME_PARAM].getValue()), 0, 6);
        params[DL_TIME_PARAM].setValue((float)kDelayOldIndex[old]);
        params[GL_ROLL_PARAM].setValue((float)kRollV1Index[clamp((int)(roll * 5.f), 0, 4)]);
    }

    void process(const ProcessArgs& args) override {
        float stepSec;
        bool rst;
        bool edge = clockStep(args, stepSec, rst);
        float vL = inputs[IN_L_INPUT].getVoltage();
        float vR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : vL;
        float oL, oR;
        processFx(args, vL, vR, edge, stepSec, rst, oL, oR);
        outputs[OUT_L_OUTPUT].setVoltage(5.f * oL);
        outputs[OUT_R_OUTPUT].setVoltage(5.f * oR);
    }

    // ---- clock: step edges drive GLITCH and the throw buttons ----
    // Returns true on a step edge (CLOCK jack, or 120 BPM internal when unpatched); stepSec = step
    // length, rst = a RESET trigger.
    bool clockStep(const ProcessArgs& args, float& stepSec, bool& rst) {
        bool edge = false;
        sinceClock += args.sampleTime;
        if (inputs[CLOCK_INPUT].isConnected()) {
            if (clkTrig.process(inputs[CLOCK_INPUT].getVoltage())) {
                if (sinceClock < 4.f) clockPeriod = sinceClock;
                sinceClock = 0.f;
                edge = true;
            }
        } else {
            internalTimer += args.sampleTime;
            if (internalTimer >= 0.125f) { internalTimer -= 0.125f; edge = true; }
        }
        stepSec = (inputs[CLOCK_INPUT].isConnected() && clockPeriod > 0.f && sinceClock < 2.f) ? clockPeriod : 0.125f;
        rst = rstTrig.process(inputs[RESET_INPUT].getVoltage());
        return edge;
    }

    // The whole chain, one sample. vL / vR = input in volts (before INPUT gain); oL / oR = output, +-1.
    void processFx(const ProcessArgs& args, float vL, float vR, bool edge, float stepSec, bool rst, float& oL, float& oR) {
        const float sr = args.sampleRate;
        if (rst) { step = -1; clk = -1; internalTimer = 0.f; }
        if (edge) { step = (step + 1) % kThrowSteps; clk++; }
        bool force = glitchTrig.process(inputs[GLITCH_INPUT].getVoltage());

        // ---- smart randomizer: new picks on each downbeat, knobs glide there over ~a beat ----
        for (int i = 0; i < 5; i++) {
            bool o = on(RND_PARAM + i);
            if (o && !rndWasOn[i] && i == R_RB) { homeRoot = slot(RB_ROOT_PARAM); homeChord = clamp(slot(RB_CHORD_PARAM), 0, 7); }
            rndWasOn[i] = o;
        }
        // FREEZE holds everything where it is: no new picks, knobs stop mid-glide
        bool frozen = on(RND_FREEZE_PARAM);
        int div = kRndDivSteps[clamp(slot(RND_DIV_PARAM), 0, kRndDivCount - 1)];
        // presets load here, not on the UI thread, so they never race the audio
        int pk = clamp(slot(PRESET_PARAM), 0, kPresetCount);
        int presetReq = presetRequest.exchange(-1);
        if (lastPreset < 0) lastPreset = pk;   // first run: just take the knob's position
        else if (pk != lastPreset) { lastPreset = pk; presetReq = pk; }
        if (presetReq >= 0) applyPreset(presetReq);
        if (edge && !frozen && clk % div == 0) {
            int mask = 0;
            for (int i = 0; i < 5; i++) if (on(RND_PARAM + i)) mask |= 1 << i;
            if (mask) { randomizeBar(mask); bar++; }
        }
        static const float glideSteps[kRndGlideCount] = {0.f, 0.5f, 2.f, 6.f, -1.f};   // time constant in steps; -1 = most of a cycle
        int gi = clamp(slot(RND_GLIDE_PARAM), 0, kRndGlideCount - 1);
        float tau = glideSteps[gi] < 0.f ? 0.4f * div : glideSteps[gi];
        float cg = tau <= 0.f ? 1.f : 1.f - std::exp(-1.f / (tau * stepSec * sr));
        for (const RndKnob& k : rndKnob) {
            if (frozen || !rndGlide[k.id] || !on(RND_PARAM + k.stage)) continue;
            float v = params[k.id].getValue();
            params[k.id].setValue(v + (rndTgt[k.id] - v) * cg);
        }

        // ---- smoothing: every knob and stage switch glides (~15 ms), so moves never click or zip ----
        float c = 1.f - std::exp(-1.f / (0.015f * sr));
        for (int i = 0; i < NUM_PARAMS; i++) {
            float t = params[i].getValue();
            sm[i] = smInit ? sm[i] + (t - sm[i]) * c : t;
        }
        smInit = true;

        // ---- input ----
        float g = sm[IN_GAIN_PARAM] * 0.2f;   // +-5 V -> +-1
        float inL = vL * g;
        float inR = vR * g;

        // Every stage keeps running so switching it on is seamless; ON crossfades it in.
        float stepLen = stepSec * sr;
        float pL, pR;
        // ---- SHUFFLE (first, so it rearranges the clean beat) ----
        shuffle.process(inL, inR, sr, stepLen, edge, step, slot(SH_PATTERN_PARAM), params[SH_AMOUNT_PARAM].getValue(),
                        params[SH_REV_PARAM].getValue(), params[SH_PITCH_PARAM].getValue(), slot(SH_LEN_PARAM), pL, pR);
        float m = sm[SH_MIX_PARAM] * sm[SH_ON_PARAM];
        float sL = inL + (pL - inL) * m, sR = inR + (pR - inR) * m;

        // ---- GLITCH ----
        if (edge || force) {
            glitch.trigger(stepSec * sr, params[GL_CHANCE_PARAM].getValue(), slot(GL_ROLL_PARAM),
                           params[GL_REV_PARAM].getValue(), params[GL_STOP_PARAM].getValue(), step % 4 == 0 || force, force);
            static const char* names[4] = {"-", "ROLL", "REVERSE", "TAPE STOP"};
            glitchEvent = glitch.ev == Glitch::ROLL ? std::string("ROLL ") + kRollName[glitch.rollDiv] : names[glitch.ev];
        }
        glitch.process(sL, sR, sr, sm[GL_CRUSH_PARAM], pL, pR);
        m = sm[GL_MIX_PARAM] * sm[GL_ON_PARAM];
        float aL = sL + (pL - sL) * m, aR = sR + (pR - sR) * m;

        // ---- WARP DELAY read (its feedback may loop through ROBOT and PULSE) ----
        float fbL = 0.f, fbR = 0.f;
        float route = sm[DL_ROUTE_PARAM], dlOn = sm[DL_ON_PARAM];
        delay.read(sr, kDelaySteps[clamp(slot(DL_TIME_PARAM), 0, kDelayCount - 1)] * stepSec, sm[DL_FDBK_PARAM] * dlOn,
                   sm[DL_WARP_PARAM], sm[DL_TONE_PARAM], fbL, fbR);

        // ---- ROBOT (root glides between notes) ----
        float bL = aL + route * fbL, bR = aR + route * fbR;
        robot.process(bL, bR, sr, sm[RB_ROOT_PARAM], clamp(slot(RB_CHORD_PARAM), 0, 7),
                      sm[RB_VOWEL_PARAM], sm[RB_RING_PARAM], sm[RB_Q_PARAM], pL, pR);
        m = sm[RB_MIX_PARAM] * sm[RB_ON_PARAM];
        float rL = bL + (pL - bL) * m, rR = bR + (pR - bR) * m;

        // ---- PULSE (gate + filter in time; OPEN lets everything through) ----
        pulse.process(rL, rR, sr, stepLen, edge, step, slot(PU_PATTERN_PARAM), sm[PU_CUTOFF_PARAM], sm[PU_RES_PARAM],
                      sm[PU_DEPTH_PARAM], sm[PU_DECAY_PARAM], clamp(slot(PU_MODE_PARAM), 0, 2),
                      inputs[OPEN_INPUT].getVoltage() > 1.f, pL, pR);
        m = sm[PU_MIX_PARAM] * sm[PU_ON_PARAM];
        float cL = rL + (pL - rL) * m, cR = rR + (pR - rR) * m;

        // ---- WARP DELAY write: lit steps throw this signal in; un-routed feedback goes straight back ----
        bool throwOn = step < 0 || on(throwParam(step));
        delay.write(cL, cR, sr, throwOn);
        delay.writeRaw((1.f - route) * fbL, (1.f - route) * fbR);
        m = sm[DL_MIX_PARAM] * dlOn;
        float wetL = cL + delay.wetL * m, wetR = cR + delay.wetR * m;

        // ---- out: smooth limiter (clean up to 80%, then a soft knee to full scale) ----
        float dw = sm[DRYWET_PARAM], og = sm[OUT_GAIN_PARAM];
        float mL = inL + (wetL - inL) * dw, mR = inR + (wetR - inR) * dw;
        // ---- filter ADSR: TRIG picks what fires it; clocked sources hold it open for a while, GATE while high ----
        int trig = clamp(slot(FLT_TRIG_PARAM), 0, kTrigCount - 1);
        if (edge) {
            seqPos = (int)(clk % std::max(seqLen, 1));
            seqOff = seqOn[seqPos] ? seqNote(seqLvl[seqPos]) : seqOff;   // rests keep the last note
        }
        bool fire = false; float hold = 0.f;
        switch (trig) {
            case TRIG_BEAT:   fire = edge && clk % 4 == 0; hold = 2.f; break;
            case TRIG_STEP:   fire = edge; hold = 0.5f; break;
            case TRIG_PULSE:  fire = pulse.fired; hold = 0.5f; break;
            case TRIG_GLITCH: fire = (edge || force) && glitch.ev != Glitch::NONE; hold = 1.f; break;
            case TRIG_SEQ:    fire = edge && seqOn[seqPos]; hold = 0.5f; break;
            default: break;
        }
        bool gateHigh = inputs[GATE_INPUT].getVoltage() > 1.f;
        if (trig == TRIG_GATE) {
            if (gateTrig.process(inputs[GATE_INPUT].getVoltage(), 0.1f, 1.f)) fltEnv.trigger();
            else if (!gateHigh) fltEnv.release();
            fltGate = 0.f;
        } else if (fire) { fltEnv.trigger(); fltGate = hold * stepLen; }
        if (fltGate > 0.f && (fltGate -= 1.f) <= 0.f) fltEnv.release();
        float envRaw = fltEnv.process(0.003f * std::pow(667.f, sm[FLT_ATK_PARAM]), 0.012f * std::pow(333.f, sm[FLT_DEC_PARAM]),
                                      sm[FLT_SUS_PARAM], 0.012f * std::pow(333.f, sm[FLT_REL_PARAM]), sr);
        // two gentle 1.5 ms poles round off the envelope's corners: still snappy, but no pop or crunch
        float ec = 1.f - std::exp(-1.f / (0.0015f * sr));
        envS1 += (envRaw - envS1) * ec; envS2 += (envS1 - envS2) * ec;
        fltEnvOut = envS2;
        {   // stage LEDs, as on the main synth: colour follows the knob, the active stage flashes and fades over 150 ms
            int st = fltEnv.st == Adsr::ATK ? 0 : fltEnv.st == Adsr::REL ? 3
                   : fltEnv.st == Adsr::DEC ? (std::fabs(fltEnv.v - sm[FLT_SUS_PARAM]) < 0.02f ? 2 : 1) : -1;
            static const int knobs[4] = {FLT_ATK_PARAM, FLT_DEC_PARAM, FLT_SUS_PARAM, FLT_REL_PARAM};
            for (int k = 0; k < 4; k++) {
                stageLvl[k] = std::max(k == st ? 1.f : 0.f, stageLvl[k] - args.sampleTime / 0.15f);
                float r, g, b; ledRamp(params[knobs[k]].getValue(), r, g, b);
                lights[ADSR_LED + k * 3 + 0].setBrightness(r * stageLvl[k]);
                lights[ADSR_LED + k * 3 + 1].setBrightness(g * stageLvl[k]);
                lights[ADSR_LED + k * 3 + 2].setBrightness(b * stageLvl[k]);
            }
        }
        // BYP takes the envelope and SEQ off the filter (smoothed); the filter itself stays as set
        float env = envS2 * (1.f - sm[FLT_BYPASS_PARAM]);
        // SEQ: each step's note moves the cutoff (a quick 3 ms slew, so jumps never click)
        bool seqLive = trig == TRIG_SEQ && !on(FLT_BYPASS_PARAM);
        seqOffSm += ((seqLive ? seqOff : 0.f) - seqOffSm) * (1.f - std::exp(-1.f / (0.003f * sr)));
        float fenv = sm[FLT_ENV_PARAM] * std::fabs(sm[FLT_ENV_PARAM]);   // squared: fine sweeps around the middle, full range at the ends
        float fcut = clamp(sm[FILTER_PARAM] + seqOffSm + env * fenv, 0.f, 1.f);
        // hits bark: resonance climbs a little with the envelope (still capped)
        float fres = sm[FLT_RES_PARAM] + 0.2f * env * std::fabs(fenv);
        master.process(mL, mR, fcut, fres, sr, sm[FLT_STAGES_PARAM], sm[FLT_MODE_PARAM]);
        // big ENV sweeps run hot (the resonant peak rushes through the whole spectrum): pull the hit's peak
        // down by up to ~5 dB at full ENV + RES, so it stays punchy instead of loud
        float ea = std::min(env, 1.f) * std::fabs(fenv);
        float tame = 1.f - ea * (0.25f + 0.2f * sm[FLT_RES_PARAM]);
        mL *= tame; mR *= tame;
        float duck = 1.f - sm[FLT_AMP_PARAM] * (1.f - sm[FLT_BYPASS_PARAM]) * (1.f - std::min(envS2, 1.f));   // AMP: the envelope shapes the level too
        mL *= duck; mR *= duck;   // DJ filter on the whole mix, before the limiter
        oL = softLimit(mL * og);
        oR = softLimit(mR * og);
        float fall = std::exp(-args.sampleTime * 6.f);
        meterL = std::max(std::fabs(oL), meterL * fall);
        meterR = std::max(std::fabs(oR), meterR * fall);
    }
};

// ---------------------------------------------------------------- widgets ----------------

static std::shared_ptr<window::Font> uiFont() { return APP->window->uiFont; }

struct GFLabel : TransparentWidget {
    std::string text;
    float size = 7.f;
    void draw(const DrawArgs& args) override {
        auto font = uiFont(); if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, size);
        nvgTextLetterSpacing(args.vg, 0.6f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(0xbb, 0xbb, 0xbb));
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, text.c_str(), NULL);
    }
};

struct GFTitle : TransparentWidget {
    void draw(const DrawArgs& args) override {
        auto font = uiFont(); if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
        nvgFontSize(args.vg, 11.f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgText(args.vg, 0.f, mm2px(3.5f), "SynTHBloK", NULL);
        nvgFontSize(args.vg, 10.f);
        nvgTextLetterSpacing(args.vg, 0.8f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0x7a, 0x00));
        nvgText(args.vg, 0.f, mm2px(6.4f), "GRAIN-FORGE", NULL);
    }
};

// Amber value arc around a knob (0..1 of the knob's range).
struct GFRing : TransparentWidget {
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

// Stage header (MIDI + style): stage name and live status; click toggles the stage on/off.
struct GFStageDisplay : TransparentWidget {
    SynTHBloKGrainForge* module = NULL;
    int onParam = 0;
    const char* title = "";
    std::function<std::string()> status;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        bool lit = !module || module->on(onParam);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.f);
        nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        nvgStrokeWidth(args.vg, lit ? 1.f : 0.8f);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, lit ? 160 : 60));
        nvgStroke(args.vg);
        auto font = uiFont(); if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, nvgRGBA(255, 190, 90, lit ? 220 : 90));
        nvgText(args.vg, 5.f, H * 0.3f, title, NULL);
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, lit ? nvgRGBA(120, 230, 90, 255) : nvgRGBA(255, 190, 90, 90));
        nvgText(args.vg, W - 5.f, H * 0.3f, lit ? "ON" : "OFF", NULL);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 9.5f);
        nvgFillColor(args.vg, nvgRGBA(255, 190, 90, lit ? 255 : 90));
        std::string s = status ? status() : "";
        nvgText(args.vg, 5.f, H * 0.7f, s.c_str(), NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[onParam].setValue(module->on(onParam) ? 0.f : 1.f);
            e.consume(this);
        }
    }
};

// Amber throw-step buttons.
struct GFButton : app::ParamWidget {
    const char* label = "";
    int playIdx = -1;                       // throw buttons: outline when this step plays
    SynTHBloKGrainForge* gf = NULL;
    void draw(const DrawArgs& args) override {
        ParamQuantity* pq = getParamQuantity();
        bool lit = pq && pq->getValue() > 0.5f;
        bool now = gf && playIdx >= 0 && gf->step == playIdx;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.2f);
        nvgFillColor(args.vg, lit ? nvgRGBA(255, 112, 15, 235) : nvgRGBA(40, 25, 8, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, now ? nvgRGB(0xff, 0xff, 0xff) : nvgRGBA(255, 180, 90, lit ? 255 : 120));
        nvgStrokeWidth(args.vg, now ? 1.2f : 0.8f);
        nvgStroke(args.vg);
        auto font = uiFont(); if (!font || !label[0]) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 7.f);
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

// Micro button beside the THROW STEPS label: every click deals a new random throw pattern.
struct GFThrowRoll : OpaqueWidget {
    SynTHBloKGrainForge* module = NULL;
    float flash = 0.f;
    void roll() {
        if (!module) return;
        float density = 0.2f + 0.3f * random::uniform();          // 20..50% of the steps lit
        bool on[SynTHBloKGrainForge::kThrowSteps];
        int count = 0;
        for (int i = 0; i < SynTHBloKGrainForge::kThrowSteps; i++) { on[i] = random::uniform() < density; count += on[i]; }
        while (count < 2) {                                          // always throw something
            int i = (int)(random::uniform() * SynTHBloKGrainForge::kThrowSteps) % SynTHBloKGrainForge::kThrowSteps;
            if (!on[i]) { on[i] = true; count++; }
        }
        for (int i = 0; i < SynTHBloKGrainForge::kThrowSteps; i++)
            module->paramQuantities[SynTHBloKGrainForge::throwParam(i)]->setValue(on[i] ? 1.f : 0.f);
    }
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.f);
        nvgFillColor(args.vg, flash > 0.f ? nvgRGBA(255, 112, 15, (int)(235 * flash)) : nvgRGBA(40, 25, 8, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 160));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        auto font = uiFont(); if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 5.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, flash > 0.5f ? nvgRGBA(20, 10, 2, 255) : nvgRGBA(255, 190, 90, 255));
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, "RND", NULL);
        flash = std::max(0.f, flash - 0.08f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            roll();
            flash = 1.f;
            e.consume(this);
        }
    }
};

// Three-way picker, the active one lit; click a segment to pick it. PULSE mode (LP | BP | HP) side by
// side, the master filter's stages (1 / 2 / 3) stacked.
struct GFModeButton : app::ParamWidget {
    const char* const* names = kModeName;
    bool vertical = false;
    void draw(const DrawArgs& args) override {
        ParamQuantity* pq = getParamQuantity();
        int sel = pq ? clamp((int)std::round(pq->getValue()), 0, 2) : 0;
        float w = vertical ? box.size.x : box.size.x / 3.f, h = vertical ? box.size.y / 3.f : box.size.y;
        auto font = uiFont();
        for (int i = 0; i < 3; i++) {
            bool lit = i == sel;
            float x0 = vertical ? 0.f : i * w, y0 = vertical ? i * h : 0.f;
            nvgBeginPath(args.vg);
            if (vertical) nvgRoundedRect(args.vg, 0.f, y0 + 0.5f, w, h - 1.f, 1.2f);
            else nvgRoundedRect(args.vg, x0 + 0.5f, 0.f, w - 1.f, h, 1.2f);
            nvgFillColor(args.vg, lit ? nvgRGBA(255, 112, 15, 235) : nvgRGBA(40, 25, 8, 255));
            nvgFill(args.vg);
            nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, lit ? 255 : 120));
            nvgStrokeWidth(args.vg, 0.8f);
            nvgStroke(args.vg);
            if (!font) continue;
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, 7.f);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, lit ? nvgRGBA(20, 10, 2, 255) : nvgRGBA(255, 190, 90, 255));
            nvgText(args.vg, x0 + w * 0.5f, y0 + h * 0.5f, names[i], NULL);
        }
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (ParamQuantity* pq = getParamQuantity()) {
                int sel = clamp((int)(vertical ? e.pos.y / (box.size.y / 3.f) : e.pos.x / (box.size.x / 3.f)), 0, 2);
                pq->setValue((float)sel);
            }
            e.consume(this);
            return;
        }
        ParamWidget::onButton(e);
    }
};

// Tempo / step / chain status, and the stereo output meter.
struct GFStatus : TransparentWidget {
    SynTHBloKGrainForge* module = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.f);
        nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 80));
        nvgStroke(args.vg);
        auto font = uiFont(); if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        if (part == 1) { meters(args, W, H); return; }
        char bpm[32] = "120.0 BPM", sub[48] = "INTERNAL";
        if (module && module->inputs[SynTHBloKGrainForge::CLOCK_INPUT].isConnected()) {
            if (module->clockPeriod > 0.f && module->sinceClock < 2.f) snprintf(bpm, sizeof bpm, "%.1f BPM", 60.f / (module->clockPeriod * 4.f));
            else snprintf(bpm, sizeof bpm, "--- BPM");
            snprintf(sub, sizeof sub, "STEP %d/%d", module->step + 1, SynTHBloKGrainForge::kThrowSteps);
        }
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 9.f);
        nvgFillColor(args.vg, nvgRGBA(255, 190, 90, 255));
        nvgText(args.vg, W * 0.5f, H * 0.34f, bpm, NULL);
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, nvgRGBA(255, 190, 90, 130));
        nvgText(args.vg, W * 0.5f, H * 0.7f, sub, NULL);
    }

    // OUT L / OUT R: the same segmented dBFS bars as the MIX-24 levels display (full scale = 0 dB)
    void meters(const DrawArgs& args, float W, float H) {
        float lv[2] = {module ? module->meterL : 0.6f, module ? module->meterR : 0.5f};
        static const char* nm[2] = {"L", "R"};
        float lx = 4.f, bx = 12.f, bw = W - 40.f, vx = W - 3.f, bh = H * 0.24f;
        char buf[16];
        nvgFontSize(args.vg, 8.f);
        for (int i = 0; i < 2; i++) {
            float y = H * (i ? 0.56f : 0.2f);
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, nvgRGBA(255, 190, 90, 255));
            nvgText(args.vg, lx, y + bh * 0.5f, nm[i], NULL);
            bar(args, bx, y, bw, bh, lv[i], peak[i]);
            float db = toDb(lv[i]);
            if (db < -60.f) snprintf(buf, sizeof buf, "-inf"); else snprintf(buf, sizeof buf, "%.1f", db);
            nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, db > -1.f ? nvgRGBA(255, 50, 40, 255) : nvgRGBA(255, 190, 90, 255));
            nvgText(args.vg, vx, y + bh * 0.5f, buf, NULL);
        }
    }

    int part = 0;                     // 0 = BPM / step, 1 = output meters
    float peak[2] = {};
    static float toDb(float v) { return v > 1e-5f ? 20.f * std::log10(v) : -120.f; }
    // 24 segments, -48..+6 dB: light amber, amber above -9, red above -1; a peak-hold segment falls back slowly.
    void bar(const DrawArgs& a, float x, float y, float w, float h, float lvl, float& pk) {
        const int N = 24;
        const float DB_LO = -48.f, DB_HI = 6.f;
        float pos = clamp((toDb(lvl) - DB_LO) / (DB_HI - DB_LO), 0.f, 1.f);
        pk = std::max(lvl, pk * 0.9975f);
        float ppos = clamp((toDb(pk) - DB_LO) / (DB_HI - DB_LO), 0.f, 1.f);
        float sw = w / N;
        for (int i = 0; i < N; i++) {
            float segPos = (i + 1.f) / N;
            float segDb = DB_LO + (DB_HI - DB_LO) * segPos;
            bool lit = segPos <= pos + 1e-4f;
            bool pkSeg = (int)std::ceil(ppos * N) == i + 1;
            NVGcolor c;
            if (segDb > -1.f)      c = nvgRGBA(255, 40, 30, 255);
            else if (segDb > -9.f) c = nvgRGBA(255, 112, 15, 255);
            else                   c = nvgRGBA(255, 176, 60, 255);
            if (!lit && !(pkSeg && ppos > 0.02f)) c = nvgRGBA(46, 29, 10, 255);
            else if (!lit) c.a = 0.5f;
            nvgBeginPath(a.vg);
            nvgRect(a.vg, x + i * sw + 0.4f, y, sw - 0.8f, h);
            nvgFillColor(a.vg, c);
            nvgFill(a.vg);
        }
    }
    void onButton(const ButtonEvent& e) override {   // click resets the peak hold, like MIX-24
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) { peak[0] = peak[1] = 0.f; e.consume(this); }
    }
};

// RANDOM OLED: three segments, CYCLE | GLIDE | FREEZE. Click (or scroll) CYCLE/GLIDE to step through,
// right-click steps back; click FREEZE to hold the current picks.
struct GFRndPanel : TransparentWidget {
    SynTHBloKGrainForge* module = NULL;
    typedef SynTHBloKGrainForge G;
    static int segAt(float x, float W) { return clamp((int)(x / (W / 3.f)), 0, 2); }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y, w = W / 3.f;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.f);
        nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 80));
        nvgStroke(args.vg);
        int dv = module ? clamp(module->slot(G::RND_DIV_PARAM), 0, kRndDivCount - 1) : 2;
        int gl = module ? clamp(module->slot(G::RND_GLIDE_PARAM), 0, kRndGlideCount - 1) : 2;
        bool fz = module && module->on(G::RND_FREEZE_PARAM);
        // freeze segment lights up like the buttons
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 2.f * w + 2.f, 3.f, w - 4.f, H - 6.f, 1.5f);
        nvgFillColor(args.vg, fz ? nvgRGBA(255, 112, 15, 235) : nvgRGBA(40, 25, 8, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, fz ? 255 : 120));
        nvgStroke(args.vg);
        for (int i = 1; i < 3; i++) {   // dividers
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, i * w, 4.f); nvgLineTo(args.vg, i * w, H - 4.f);
            nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 50));
            nvgStroke(args.vg);
        }
        auto font = uiFont(); if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        const char* top[2] = {"CYCLE", "GLIDE"};
        const char* val[2] = {kRndDivName[dv], kRndGlideName[gl]};
        for (int i = 0; i < 2; i++) {
            nvgFontSize(args.vg, 7.f);
            nvgFillColor(args.vg, nvgRGBA(255, 190, 90, 130));
            nvgText(args.vg, i * w + w * 0.5f, H * 0.3f, top[i], NULL);
            nvgFontSize(args.vg, 9.f);
            nvgFillColor(args.vg, nvgRGBA(255, 190, 90, 255));
            nvgText(args.vg, i * w + w * 0.5f, H * 0.68f, val[i], NULL);
        }
        nvgFontSize(args.vg, 8.f);
        nvgFillColor(args.vg, fz ? nvgRGBA(20, 10, 2, 255) : nvgRGBA(255, 190, 90, 255));
        nvgText(args.vg, 2.5f * w, H * 0.5f, "FREEZE", NULL);
    }
    void stepParam(int seg, int d) {
        if (!module || seg > 1) return;
        int id = seg == 0 ? G::RND_DIV_PARAM : G::RND_GLIDE_PARAM;
        int n = seg == 0 ? kRndDivCount : kRndGlideCount;
        module->params[id].setValue((float)((module->slot(id) + d + n) % n));
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || (e.button != GLFW_MOUSE_BUTTON_LEFT && e.button != GLFW_MOUSE_BUTTON_RIGHT)) return;
        int seg = segAt(e.pos.x, box.size.x);
        if (seg == 2) {
            if (module && e.button == GLFW_MOUSE_BUTTON_LEFT)
                module->params[G::RND_FREEZE_PARAM].setValue(module->on(G::RND_FREEZE_PARAM) ? 0.f : 1.f);
        } else stepParam(seg, e.button == GLFW_MOUSE_BUTTON_LEFT ? 1 : -1);
        e.consume(this);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        int seg = segAt(e.pos.x, box.size.x);
        if (seg > 1 || e.scrollDelta.y == 0.f) return;
        stepParam(seg, e.scrollDelta.y > 0.f ? 1 : -1);
        e.consume(this);
    }
};

// PRESET screen: a tiny 2-row matrix on top, one gradient-coloured cell per preset (click one to load
// it), and the name below (click = next, right-click = list).
struct GFPresetBar : TransparentWidget {
    SynTHBloKGrainForge* module = NULL;
    typedef SynTHBloKGrainForge G;
    static constexpr int kCells = kPresetCount + 1, kCols = (kCells + 1) / 2;
    static constexpr float kMatH = 0.52f;   // share of the height for the matrix
    static const char* nameOf(int i) { return i >= kPresetCount ? "SMART RND" : kPreset[i].name; }
    int cur() { return module ? clamp(module->slot(G::PRESET_PARAM), 0, kPresetCount) : 0; }
    static NVGcolor cellColor(int i, int alpha) {   // amber -> pink -> violet across the presets
        float t = i / float(kCells - 1);
        NVGcolor c = nvgHSL(0.08f - 0.30f * t, 1.f, 0.55f);   // nanovg wraps the hue
        c.a = alpha / 255.f;
        return c;
    }
    Rect cellRect(int i) {
        float m = 1.5f, cw = (box.size.x - 2.f * m) / kCols, ch = (box.size.y * kMatH - m) / 2.f;
        return Rect(Vec(m + (i % kCols) * cw, m + (i / kCols) * ch), Vec(cw, ch));
    }
    int cellAt(Vec p) {
        if (p.y >= box.size.y * kMatH) return -1;
        for (int i = 0; i < kCells; i++) if (cellRect(i).contains(p)) return i;
        return -1;
    }
    void drawMatrix(NVGcontext* vg) {
        int sel = cur();
        for (int i = 0; i < kCells; i++) {
            Rect r = cellRect(i);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, r.pos.x + 0.5f, r.pos.y + 0.5f, r.size.x - 1.f, r.size.y - 1.f, 0.8f);
            nvgFillColor(vg, cellColor(i, i == sel ? 255 : 95));
            nvgFill(vg);
            if (i == sel) {
                nvgStrokeWidth(vg, 0.8f);
                nvgStrokeColor(vg, nvgRGBA(255, 245, 225, 255));
                nvgStroke(vg);
            }
        }
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.f);
        nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 80));
        nvgStroke(args.vg);
        drawMatrix(args.vg);
        auto font = uiFont(); if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 7.f);
        nvgFillColor(args.vg, cellColor(cur(), 255));   // the name takes its cell's colour
        nvgText(args.vg, W * 0.5f, H * (kMatH + 1.f) * 0.5f + 0.5f, nameOf(cur()), NULL);
    }
    // matrix cell = that preset (again = reload / re-roll SMART RND), name = next, right-click = list
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || !module) return;
        SynTHBloKGrainForge* m = module;
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
            e.consume(this);
            int c = cellAt(e.pos);
            if (c >= 0 && c == cur()) m->presetRequest.store(c);
            else m->params[G::PRESET_PARAM].setValue((float)(c >= 0 ? c : (cur() + 1) % kCells));
        } else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
            e.consume(this);
            ui::Menu* menu = createMenu();
            menu->addChild(createMenuLabel("Preset"));
            for (int i = 0; i <= kPresetCount; i++)
                menu->addChild(createCheckMenuItem(nameOf(i), "", [=]() { return cur() == i; }, [=]() {
                    if (cur() == i) m->presetRequest.store(i);   // same one again: reload / re-roll
                    else m->params[G::PRESET_PARAM].setValue((float)i);
                }));
        }
    }
};

// extra-small RGB LED, same as the main SynTHBloK synth's
template <typename TBase>
struct GFMicroLight : TinyLight<TBase> {
    GFMicroLight() { this->box.size = mm2px(Vec(0.9f, 0.9f)); }
};

// Filter SEQ screen: left, the TRIG source (click = next, right-click = menu); right, 16 bars.
// Click a bar to switch its step on/off; drag up/down across bars to paint heights (notes).
struct GFSeqScreen : TransparentWidget {
    SynTHBloKGrainForge* module = NULL;
    typedef SynTHBloKGrainForge G;
    static constexpr float kTrigW = 0.17f;   // share of the width for the TRIG label
    Vec drag, press; int pressStep = -1; bool moved = false;
    float barX0() { return box.size.x * kTrigW; }
    int stepAt(float x) { return clamp((int)((x - barX0()) / ((box.size.x - barX0()) / G::kSeqMax)), 0, G::kSeqMax - 1); }
    void paint(Vec p) {
        if (!module || p.x < barX0()) return;
        int i = stepAt(p.x);
        module->seqLvl[i] = clamp(1.f - (p.y - 1.5f) / (box.size.y - 3.f), 0.f, 1.f);
        module->seqOn[i] = true;
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y, x0 = barX0();
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 2.f);
        nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 80));
        nvgStroke(args.vg);
        int trig = module ? clamp(module->slot(G::FLT_TRIG_PARAM), 0, kTrigCount - 1) : TRIG_SEQ;
        bool seqMode = trig == TRIG_SEQ;
        // envelope level as a glow strip under the TRIG name
        float ev = module ? clamp(module->fltEnvOut, 0.f, 1.f) : 0.f;
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 2.f, H - 3.f, (x0 - 4.f) * ev, 1.5f);
        nvgFillColor(args.vg, nvgRGBA(255, 112, 15, 220));
        nvgFill(args.vg);
        auto font = uiFont();
        if (font) {
            nvgFontFaceId(args.vg, font->handle);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFontSize(args.vg, 6.f);
            nvgFillColor(args.vg, nvgRGBA(255, 190, 90, 130));
            nvgText(args.vg, x0 * 0.5f, H * 0.28f, "TRIG", NULL);
            nvgFontSize(args.vg, 7.5f);
            nvgFillColor(args.vg, nvgRGBA(255, 190, 90, 255));
            nvgText(args.vg, x0 * 0.5f, H * 0.62f, kTrigName[trig], NULL);
        }
        float bw = (W - x0 - 2.f) / G::kSeqMax;
        int len = module ? module->seqLen : 16, pos = module ? module->seqPos : -1;
        for (int i = 0; i < G::kSeqMax; i++) {
            bool on = module ? module->seqOn[i] : i % 2 == 0;
            float lv = module ? module->seqLvl[i] : 0.5f;
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
    void onButton(const ButtonEvent& e) override {
        if (!module || e.action != GLFW_PRESS) return;
        if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
            e.consume(this);
            SynTHBloKGrainForge* m = module;
            ui::Menu* menu = createMenu();
            menu->addChild(createMenuLabel("Filter env"));
            for (int t = 0; t < kTrigCount; t++)
                menu->addChild(createCheckMenuItem(std::string("Trigger: ") + kTrigName[t], "",
                    [=]() { return clamp(m->slot(G::FLT_TRIG_PARAM), 0, kTrigCount - 1) == t; },
                    [=]() { m->params[G::FLT_TRIG_PARAM].setValue((float)t); }));
            menu->addChild(new MenuSeparator);
            menu->addChild(createMenuLabel("SEQ"));
            for (int L : {4, 8, 12, 16})
                menu->addChild(createCheckMenuItem("Length " + std::to_string(L), "",
                    [=]() { return m->seqLen == L; }, [=]() { m->seqLen = L; }));
            menu->addChild(createMenuItem("Randomize pattern", "", [=]() { m->seqRandom(); }));
            menu->addChild(createMenuItem("Default pattern", "", [=]() { m->seqDefault(); }));
            menu->addChild(createMenuItem("Clear", "", [=]() { for (int i = 0; i < G::kSeqMax; i++) m->seqOn[i] = false; }));
            return;
        }
        if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
        e.consume(this);
        if (e.pos.x < barX0()) {   // TRIG: next source
            int t = clamp(module->slot(G::FLT_TRIG_PARAM), 0, kTrigCount - 1);
            module->params[G::FLT_TRIG_PARAM].setValue((float)((t + 1) % kTrigCount));
            pressStep = -1;
            return;
        }
        drag = press = e.pos; pressStep = stepAt(e.pos.x); moved = false;
    }
    void onDragMove(const DragMoveEvent& e) override {
        if (pressStep < 0 || e.button != GLFW_MOUSE_BUTTON_LEFT) return;
        drag = drag.plus(e.mouseDelta.div(getAbsoluteZoom()));
        if (!moved && drag.minus(press).norm() > 2.f) { moved = true; paint(press); }   // a little jitter is still a click
        if (moved) paint(drag);
    }
    void onDragEnd(const DragEndEvent& e) override {
        if (pressStep >= 0 && !moved && module) module->seqOn[pressStep] = !module->seqOn[pressStep];
        pressStep = -1;
    }
};

// Every GRAIN-FORGE control, `dx` mm to the right of where they sit on GRAIN-FORGE's own panel.
// jacks = false leaves out the bottom jack row (the host module places its own).
static void gfAddPanel(ModuleWidget* mw, SynTHBloKGrainForge* module, float dx, bool jacks) {
    using namespace rack::componentlibrary;
    auto at = [&](float x, float y) { return mm2px(Vec(x + dx, y)); };
    auto label = [&](float cx, float cy, const std::string& text) {
        GFLabel* l = createWidget<GFLabel>(at(cx - 10.f, cy - 2.f));
        l->box.size = mm2px(Vec(20.f, 4.f)); l->text = text;
        mw->addChild(l);
    };
    auto knob = [&](float x, float y, int param, const char* name) {
        mw->addChild(KnobTicks::create(Vec(x + dx, y)));
        GFRing* r = createWidget<GFRing>(at(x - 5.5f, y - 5.5f));
        r->box.size = mm2px(Vec(11.f, 11.f)); r->module = module; r->param = param;
        mw->addChild(r);
        mw->addParam(createParamCentered<RoundSmallBlackKnob>(at(x, y), module, param));
        label(x, y + 7.f, name);
    };
    SynTHBloKGrainForge* m = module;
    typedef SynTHBloKGrainForge G;
    auto stage = [&](int i, int onParam, const char* name, std::function<std::string()> status) {
        float x0 = 2.f + i * 39.75f;
        GFStageDisplay* d = createWidget<GFStageDisplay>(at(x0 + 1.5f, 10.5f));
        d->box.size = mm2px(Vec(29.3f, 10.f)); d->module = module; d->onParam = onParam; d->title = name; d->status = status;
        mw->addChild(d);
        GFButton* r = createParam<GFButton>(at(x0 + 31.4f, 10.5f), module, G::RND_PARAM + i);
        r->box.size = mm2px(Vec(5.9f, 10.f)); r->label = "RND";
        mw->addParam(r);
        return x0 + 19.4f;   // section centre
    };

    // ---- five stages, left to right in signal order ----
    float c = stage(0, G::SH_ON_PARAM, "SHUFFLE", [m]() -> std::string {
        int p = m ? clamp(m->slot(G::SH_PATTERN_PARAM), 0, kShufCount - 1) : 0;
        int s = m ? m->shuffle.src : -1;
        char b[24];
        if (s < 0) snprintf(b, sizeof b, "%s  LIVE", kShufName[p]); else snprintf(b, sizeof b, "%s  >%02d", kShufName[p], s + 1);
        return b;
    });
    knob(c - 9.f, 29.f, G::SH_PATTERN_PARAM, "PATTERN"); knob(c + 9.f, 29.f, G::SH_AMOUNT_PARAM, "AMOUNT");
    knob(c - 9.f, 44.f, G::SH_REV_PARAM, "REVERSE");     knob(c + 9.f, 44.f, G::SH_PITCH_PARAM, "OCTAVE");
    knob(c - 9.f, 59.f, G::SH_LEN_PARAM, "LENGTH");      knob(c + 9.f, 59.f, G::SH_MIX_PARAM, "MIX");

    c = stage(1, G::GL_ON_PARAM, "GLITCH", [m]() -> std::string { return m ? m->glitchEvent : "ROLL 1/16"; });
    knob(c - 9.f, 29.f, G::GL_CHANCE_PARAM, "CHANCE"); knob(c + 9.f, 29.f, G::GL_ROLL_PARAM, "ROLL");
    knob(c - 9.f, 44.f, G::GL_REV_PARAM, "REVERSE");   knob(c + 9.f, 44.f, G::GL_STOP_PARAM, "STOP");
    knob(c - 9.f, 59.f, G::GL_CRUSH_PARAM, "CRUSH");   knob(c + 9.f, 59.f, G::GL_MIX_PARAM, "MIX");

    c = stage(2, G::RB_ON_PARAM, "ROBOT", [m]() -> std::string {
        int root = m ? m->slot(G::RB_ROOT_PARAM) : 12, ch = m ? clamp(m->slot(G::RB_CHORD_PARAM), 0, 7) : 3;
        return std::string(kNote[root % 12]) + std::to_string(1 + (root + 9) / 12) + " " + kChordName[ch];
    });
    knob(c - 9.f, 29.f, G::RB_ROOT_PARAM, "ROOT");   knob(c + 9.f, 29.f, G::RB_CHORD_PARAM, "CHORD");
    knob(c - 9.f, 44.f, G::RB_VOWEL_PARAM, "VOWEL"); knob(c + 9.f, 44.f, G::RB_RING_PARAM, "RING");
    knob(c - 9.f, 59.f, G::RB_Q_PARAM, "RES");       knob(c + 9.f, 59.f, G::RB_MIX_PARAM, "MIX");

    c = stage(3, G::PU_ON_PARAM, "PULSE", [m]() -> std::string {
        if (m && m->inputs[G::OPEN_INPUT].getVoltage() > 1.f) return "OPEN";
        int p = m ? clamp(m->slot(G::PU_PATTERN_PARAM), 0, kPulseCount - 1) : 3;
        int md = m ? clamp(m->slot(G::PU_MODE_PARAM), 0, 2) : 0;
        return std::string(kPulseName[p]) + "  " + kModeName[md];
    });
    knob(c - 13.f, 29.f, G::PU_CUTOFF_PARAM, "CUTOFF");   knob(c, 29.f, G::PU_RES_PARAM, "RES");     knob(c + 13.f, 29.f, G::PU_DEPTH_PARAM, "DEPTH");
    knob(c - 13.f, 44.f, G::PU_PATTERN_PARAM, "PATTERN"); knob(c, 44.f, G::PU_DECAY_PARAM, "DECAY"); knob(c + 13.f, 44.f, G::PU_MIX_PARAM, "MIX");
    GFModeButton* mode = createParam<GFModeButton>(at(c - 8.f, 58.f), module, G::PU_MODE_PARAM);
    mode->box.size = mm2px(Vec(16.f, 6.5f));
    mw->addParam(mode);

    c = stage(4, G::DL_ON_PARAM, "WARP DELAY", [m]() -> std::string {
        int t = m ? clamp(m->slot(G::DL_TIME_PARAM), 0, kDelayCount - 1) : 8;
        float w = m ? m->params[G::DL_WARP_PARAM].getValue() : 0.f;
        char b[24]; snprintf(b, sizeof b, "%s  %+.0f st", kDelayName[t], w);
        return b;
    });
    knob(c - 13.f, 29.f, G::DL_TIME_PARAM, "TIME");  knob(c, 29.f, G::DL_FDBK_PARAM, "FDBK");   knob(c + 13.f, 29.f, G::DL_WARP_PARAM, "WARP");
    knob(c - 13.f, 44.f, G::DL_ROUTE_PARAM, "ROUTE"); knob(c, 44.f, G::DL_TONE_PARAM, "TONE");  knob(c + 13.f, 44.f, G::DL_MIX_PARAM, "ECHO");
    label(c, 56.f, "THROW STEPS");
    GFThrowRoll* roll = createWidget<GFThrowRoll>(at(c + 12.f, 54.4f));   // right edge = throw grid's
    roll->box.size = mm2px(Vec(5.8f, 3.2f)); roll->module = module;
    mw->addChild(roll);
    float bx0 = c - 17.4f;
    for (int i = 0; i < G::kThrowSteps; i++) {   // two rows: 1-8, 9-16
        GFButton* b = createParam<GFButton>(at(bx0 + (i % 8) * 4.4f, 58.5f + (i / 8) * 6.5f), module, G::throwParam(i));
        b->box.size = mm2px(Vec(4.f, 5.5f)); b->playIdx = i; b->gf = module;
        mw->addParam(b);
    }

    // ---- bottom: status + meter, gains, jacks ----
    GFStatus* st = createWidget<GFStatus>(at(5.f, 79.5f));
    st->box.size = mm2px(Vec(20.f, 14.f)); st->module = module;
    mw->addChild(st);
    GFStatus* mt = createWidget<GFStatus>(at(27.f, 79.5f));
    mt->box.size = mm2px(Vec(30.f, 14.f)); mt->module = module; mt->part = 1;
    mw->addChild(mt);
    GFRndPanel* rp = createWidget<GFRndPanel>(at(59.f, 79.5f));
    rp->box.size = mm2px(Vec(36.f, 14.f)); rp->module = module;
    mw->addChild(rp);
    GFPresetBar* pb = createWidget<GFPresetBar>(at(5.f, 95.5f));   // under BPM, same width
    pb->box.size = mm2px(Vec(20.f, 8.f)); pb->module = module;
    mw->addChild(pb);
    GFSeqScreen* sq = createWidget<GFSeqScreen>(at(27.f, 95.5f));   // filter env TRIG + SEQ, under METER .. RANDOM
    sq->box.size = mm2px(Vec(68.f, 8.f)); sq->module = module;
    GFButton* byb = createParam<GFButton>(at(97.f, 97.5f), module, G::FLT_BYPASS_PARAM);   // lit = filter bypassed
    byb->box.size = mm2px(Vec(5.f, 4.f)); byb->label = "BYP";
    mw->addParam(byb);
    mw->addChild(sq);
    knob(104.f, 86.5f, G::IN_GAIN_PARAM, "INPUT");
    // master filter: cutoff, res, env depth on top; ADSR trimpots below
    // FILTER and DRY/WET: one size up from the rest
    auto midKnob = [&](float x, float y, int param, const char* name) {
        mw->addChild(KnobTicks::create(Vec(x + dx, y), 13.5f / 11.f));   // scaled with the ring
        GFRing* r = createWidget<GFRing>(at(x - 6.75f, y - 6.75f));
        r->box.size = mm2px(Vec(13.5f, 13.5f)); r->module = module; r->param = param;
        mw->addChild(r);
        mw->addParam(createParamCentered<RoundBlackKnob>(at(x, y), module, param));
        label(x, y + 8.5f, name);
    };
    midKnob(118.f, 84.f, G::FILTER_PARAM, "FILTER");
    knob(132.f, 84.f, G::FLT_RES_PARAM, "RES");
    knob(145.f, 84.f, G::FLT_ENV_PARAM, "ENV");
    static const char* const stageNames[3] = {"1", "2", "3"};
    GFModeButton* stg = createParam<GFModeButton>(at(151.5f, 77.5f), module, G::FLT_STAGES_PARAM);
    stg->box.size = mm2px(Vec(5.5f, 13.5f)); stg->names = stageNames; stg->vertical = true;
    mw->addParam(stg);
    label(154.25f, 93.f, "STG");
    GFModeButton* typ = createParam<GFModeButton>(at(158.f, 77.5f), module, G::FLT_MODE_PARAM);
    typ->box.size = mm2px(Vec(7.f, 13.5f)); typ->vertical = true;   // LP / BP / HP
    mw->addParam(typ);
    label(161.5f, 93.f, "TYPE");
    const int adsr[4] = {G::FLT_ATK_PARAM, G::FLT_DEC_PARAM, G::FLT_SUS_PARAM, G::FLT_REL_PARAM};
    const char* an[4] = {"A", "D", "S", "R"};
    for (int i = 0; i < 4; i++) {
        float x = 120.f + i * 10.f;
        mw->addParam(createParamCentered<Trimpot>(at(x, 98.5f), module, adsr[i]));
        label(x - 4.6f, 98.5f, an[i]);
    }
    for (int i = 0; i < 4; i++)   // stage LEDs under the trimpots
        mw->addChild(createLightCentered<GFMicroLight<RedGreenBlueLight>>(at(120.f + i * 10.f, 103.2f), module, G::ADSR_LED + i * 3));
    mw->addParam(createParamCentered<Trimpot>(at(161.f, 98.5f), module, G::FLT_AMP_PARAM));   // env -> level
    label(156.2f, 98.5f, "V");
    midKnob(174.f, 86.5f, G::DRYWET_PARAM, "DRY / WET");
    knob(191.f, 86.5f, G::OUT_GAIN_PARAM, "OUTPUT");

    if (!jacks) return;
    const char* jn[9] = {"IN L", "IN R", "CLOCK", "RESET", "OPEN", "GLITCH", "GATE", "OUT L", "OUT R"};
    for (int i = 0; i < 9; i++) {
        float x = 12.f + i * 22.4f;
        label(x, 106.f, jn[i]);
        Vec p = at(x, 113.f);
        if (i < 7) mw->addInput(createInputCentered<BlackJack>(p, module, i));
        else mw->addOutput(createOutputCentered<BlackJack>(p, module, i - 7));
    }
}

} // namespace
