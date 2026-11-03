#pragma once
// DSP for SynTHBloK MATRIX-FX (header-only, no Rack deps so it can be tested offline): the effect library
// the 8 slots load (each built from its ACID BEDROCK description, plus the FORGE FX stages), the parameter
// names / formats / safe ranges, and the dub chains. Signals in internal units: +-1 = +-5 V.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"   // its name tables we don't show
#include "SynTHBloKGrainForge_dsp.hpp"
#pragma GCC diagnostic pop
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>

namespace mfx {

using gforge::clampf;
using gforge::clampi;
static const float kPi = 3.14159265f;

// ---------------------------------------------------------------- key ----------------
static const int kScaleCount = 4;
static const char* kScaleName[kScaleCount] = {"MINOR", "DORIAN", "PHRYG", "MAJOR"};
static const int kScale[kScaleCount][7] = {{0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 11}};
static const char* kNoteName[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
// a scale degree 0..14 (two octaves) -> semitones above the root
inline int degSemis(int deg, int scale) { deg = clampi(deg, 0, 14); return 12 * (deg / 7) + kScale[clampi(scale, 0, kScaleCount - 1)][deg % 7]; }
inline int paramDeg(float v) { return clampi((int)std::round(v * 14.f), 0, 14); }
inline float rootHz(int root) { return 65.406f * std::pow(2.f, clampi(root, 0, 11) / 12.f); }   // C2..B2

// What every effect sees each sample.
struct Ctx {
    float sr = 48000.f;
    float stepSec = 0.125f;   // one clock step (a 16th)
    bool edge = false;        // a clock step starts this sample
    int stepIdx = 0;          // 0..15 in the bar
    int root = 0, scale = 0;  // the key (COMB, RING, ROBOT)
    float sc = -1.f;          // SC jack level (-1 = unpatched)
    float stepLen() const { return stepSec * sr; }
    bool barStart() const { return edge && stepIdx == 0; }
};

// ---------------------------------------------------------------- helpers ----------------
// tanh drive whose -6 dB point stays put, so the level hardly moves with DRIVE (peak <= 1.08)
inline float drv(float x, float d) { return std::tanh(d * x) * 0.5f / std::tanh(0.5f * d); }
// transparent below 1, then a soft ceiling under 2
inline float safe(float x) { float a = std::fabs(x); return a < 1.f ? x : std::copysign(1.f + std::tanh(a - 1.f), x); }
inline float coefHz(float hz, float sr) { return 1.f - std::exp(-2.f * kPi * std::min(hz, 0.45f * sr) / sr); }
inline float coefT(float sec, float sr) { return 1.f - std::exp(-1.f / (std::max(sec, 1e-5f) * sr)); }
inline float dbToGain(float db) { return std::pow(10.f, db / 20.f); }

// Mono delay line, power of two. read(d): d samples back from the next write (d = 1: the last sample).
struct Line {
    std::vector<float> b;
    int mask = 0, pos = 0;
    void init(float sec, float sr) {
        int n = 16;
        while (n < sec * sr) n <<= 1;
        b.assign((size_t)n, 0.f); mask = n - 1; pos = 0;
    }
    float read(float d) const {
        d = clampf(d, 1.f, (float)(mask - 2));
        float fp = (float)pos - d;
        int i = (int)std::floor(fp);
        float f = fp - (float)i;
        float a = b[i & mask], c = b[(i + 1) & mask];
        return a + (c - a) * f;
    }
    void write(float x) { b[pos] = x; pos = (pos + 1) & mask; }
    float maxDelay() const { return (float)(mask - 2); }
};

struct DcBlock {
    float s = 0.f;
    float process(float x, float sr) { s += (x - s) * coefHz(8.f, sr); return x - s; }
};

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1) : s(seed * 2654435761u + 1u) {}
    float next() { s = s * 1664525u + 1013904223u; return (s >> 8) * (1.f / 16777216.f); }
};

// ---------------------------------------------------------------- parameter maps ----------------
// Shared by the effects and the OLED, so what is shown is what runs.
inline int divIdx(float v) { return clampi((int)std::round(v * 12.f), 0, gforge::kDelayCount - 1); }
inline float divSteps(float v) { return gforge::kDelaySteps[divIdx(v)]; }
inline float cutHz(float v) { return 30.f * std::pow(16000.f / 30.f, clampf(v, 0.f, 1.f)); }
inline float t60(float v) { return 0.3f * std::pow(40.f, clampf(v, 0.f, 1.f)); }
inline float dampHz(float v) { return 12000.f * std::pow(0.1f, clampf(v, 0.f, 1.f)); }
inline float preMs(float v) { return 250.f * clampf(v, 0.f, 1.f); }
inline float chorusHz(float v) { return 0.05f * std::pow(60.f, v); }
inline float phaserHz(float v) { return 0.03f * std::pow(100.f, v); }
static const float kTremSteps[6] = {4.f, 2.f, 4.f / 3.f, 1.f, 2.f / 3.f, 0.5f};
static const char* kTremName[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
inline int tremIdx(float v) { return clampi((int)std::round(v * 5.f), 0, 5); }
static const int kWarpSemis[7] = {-12, -7, -5, 0, 5, 7, 12};
inline int warpIdx(float v) { return clampi((int)std::round(v * 6.f), 0, 6); }
inline float hiCutHz(float v) { return 400.f * std::pow(15.f, v); }
inline float loCutHz(float v) { return 30.f * std::pow(10.f, v); }
inline float slapSec(float v) { return 0.04f + 0.16f * v; }
inline float throwFb(float v) { return 0.38f + 0.52f * clampf(v, 0.f, 1.f); }   // FEEDBACK RIDE 0.38 -> 0.90
inline float duckRel(float v) { return 0.02f + 0.5f * v * v; }
static const int kDuckSteps[3] = {4, 2, 8};
static const char* kDuckName[3] = {"1/4", "1/8", "1/2"};
inline int duckIdx(float v) { return clampi((int)std::round(v * 2.f), 0, 2); }
static const int kSliceSteps[3] = {1, 2, 4};
static const char* kSliceName[3] = {"1/16", "1/8", "1/4"};
inline int sliceIdx(float v) { return clampi((int)std::round(v * 2.f), 0, 2); }
inline int stopSteps(float v) { return 1 + clampi((int)std::round(v * 7.f), 0, 7); }
inline float thrDb(float v) { return -30.f + 30.f * v; }
inline float ratio(float v) { return 1.5f + 8.5f * v * v; }
inline float attMs(float v) { return 1.f + 49.f * v; }
inline float relMs(float v) { return 50.f + 750.f * v; }
inline float ceilDb(float v) { return -12.f + 12.f * v; }
inline float bits(float v) { return 16.f - 13.f * v; }
inline float holdN(float v) { return 1.f + 39.f * v * v; }
inline float eqDb(float v) { return (v - 0.5f) * 24.f; }
inline float exciteHz(float v) { return 1000.f * std::pow(8.f, v); }
inline float subLpHz(float v) { return 60.f * std::pow(400.f / 60.f, v); }
static const int kSweepSteps[4] = {8, 16, 32, 64};
static const char* kSweepName[4] = {"1/2 BAR", "1 BAR", "2 BARS", "4 BARS"};
inline int sweepIdx(float v) { return clampi((int)std::round(v * 3.f), 0, 3); }
inline float vibHz(float v) { return 2.f + 6.f * v; }
inline float vibCents(float v) { return 50.f * v; }
inline float onsetSec(float v) { return 0.5f * v; }
static const float kPanSteps[6] = {64.f, 32.f, 16.f, 8.f, 4.f, 2.f};
static const char* kPanName[6] = {"4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/8"};
inline int panIdx(float v) { return clampi((int)std::round(v * 5.f), 0, 5); }
inline float haasSec(float v) { return 0.03f * v; }
inline float monoHz(float v) { return 40.f * std::pow(7.5f, v); }
static const float kRetrigSteps[4] = {0.25f, 0.5f, 1.f, 2.f};
static const char* kRetrigName[4] = {"1/64", "1/32", "1/16", "1/8"};
inline int retrigIdx(float v) { return clampi((int)std::round(v * 3.f), 0, 3); }
inline float winSec(float v) { return 0.01f + 0.07f * v; }
inline int reps(float v) { return 2 + clampi((int)std::round(v * 4.f), 0, 4); }
inline float errRate(float v) { return 0.3f * v; }
inline int blockN(float v) { return 16 << clampi((int)std::round(v * 5.f), 0, 5); }
static const float kSrHoldSteps[3] = {0.5f, 1.f, 2.f};
static const char* kSrHoldName[3] = {"1/32", "1/16", "1/8"};
inline int srHoldIdx(float v) { return clampi((int)std::round(v * 2.f), 0, 2); }
static const char* kArpName[4] = {"0 7 12 -5", "0 12 0 -12", "CHORD", "0 5 7 12"};
inline int arpIdx(float v) { return clampi((int)std::round(v * 3.f), 0, 3); }
inline int arpRateIdx(float v) { return clampi((int)std::round(v * 2.f), 0, 2); }   // kSrHoldSteps / names
inline float jungSec(float v) { return 0.04f + 0.11f * v; }
inline float microSec(float v) { return 0.002f + 0.028f * v; }
static const char* kHeadsName[4] = {"3", "2+3", "1+3", "1+2+3"};
inline int headsIdx(float v) { return clampi((int)std::round(v * 3.f), 0, 3); }
inline float revLenSec(float v) { return 0.25f + 1.75f * v; }
inline float gateSec(float v) { return 0.05f + 0.45f * v; }
inline float db60(float v) { return -60.f + 50.f * v; }
inline float relShort(float v) { return 0.01f + 0.19f * v; }
inline float gateRel(float v) { return 0.01f + 0.49f * v; }
inline float xover(float v) { return 0.5f * std::pow(4.f, v); }

// ---------------------------------------------------------------- the library ----------------
enum FxType {
    T_TAPE, T_TUBE, T_FUZZ, T_FOLD, T_CRUSH,
    T_DUBF, T_MOOG, T_FORMANT, T_COMB,
    T_CHORUS, T_PHASER, T_RING, T_TREM, T_ROBOT,
    T_GLITCH, T_STUTTER, T_TSTOP, T_REVERSE, T_SHUFFLE,
    T_KING, T_THROW, T_PING, T_SLAP, T_WARP,
    T_SPRCOR, T_SPRING, T_CORR, T_PLATE, T_SHIMMER, T_ROOM,
    T_PUMP, T_DUCK, T_GLUE, T_LIMIT,
    // added later (ids stay stable in saved patches; each tab lists its effects in id order)
    T_DIODE, T_RECT, T_EXCITER, T_SUBOCT,
    T_STEINER, T_WAH, T_SWEEP, T_EQ3,
    T_FLANGER, T_VIBRATO, T_AUTOPAN, T_HAAS, T_IMAGER,
    T_RETRIG, T_CDSKIP, T_CORRUPT, T_SRDROP, T_FREEZE, T_ARP,
    T_TIGHT, T_JUNGLE, T_MICRO, T_SPACE,
    T_HALL, T_REVPLATE, T_GATEDVERB,
    T_SMASH, T_TAPECOMP, T_TRANS, T_GATE,
    T_COUNT
};
enum Tab { TAB_DRIVE, TAB_FILTER, TAB_MOD, TAB_GLITCH, TAB_DELAY, TAB_VERB, TAB_DYN, TAB_CHAIN, TAB_COUNT };
static const char* kTabName[TAB_COUNT] = {"DRIVE", "FILTER", "MOD", "GLITCH", "DELAY", "VERB", "DYN", "CHAIN"};

enum Fmt { F_NONE, F_PCT, F_DIV, F_CUT, F_MODE, F_VOWEL, F_NOTE, F_COMBCH, F_CHOHZ, F_VOICES, F_PHAHZ, F_STAGES,
           F_TREM, F_CHORD, F_ROLL, F_STOPLEN, F_SLICE, F_FADE, F_SHUFPAT, F_SHUFLEN, F_FB90, F_RIDE, F_HICUT, F_LOCUT,
           F_FB80, F_SLAPMS, F_FB45, F_WARP, F_T60, F_DAMP, F_PRE, F_PULSE, F_DUCKREL, F_DUCKRATE, F_THR, F_RATIO,
           F_ATT, F_REL, F_CEIL, F_DRVDB, F_BITS, F_HOLD, F_SPACE,
           F_EQDB, F_HPHZ, F_SUBLP, F_LPHP, F_SWEEP, F_MORPH3, F_CENTS, F_VIBHZ, F_ONSET, F_PANRATE, F_HAASMS,
           F_MONOHZ, F_WIDTH2, F_RETRIG, F_WINMS, F_REPS, F_ERR, F_BLOCK, F_CMODE, F_SRHOLD, F_ARPPAT, F_ARPRATE,
           F_ARPOCT, F_JUNGMS, F_MICROMS, F_HEADS, F_REVLEN, F_GATEMS, F_DB60, F_RELSH, F_RANGE, F_PM, F_XOVER };

struct FxDef {
    const char* name;
    const char* shortName;   // slot cells (5 chars)
    int tab;
    const char* desc;
    const char* pn[4];       // "" = unused
    int fmt[4];
    float def[4];
    float lo[4], hi[4];      // RAND's safe range
};

static const FxDef kFx[T_COUNT] = {
    {"TAPE SAT", "TAPE", TAB_DRIVE, "tape saturation, warmth and glue", {"DRIVE", "BIAS", "LOW", "WOW"},
     {F_PCT, F_PCT, F_PCT, F_PCT}, {0.14f, 0.2f, 0.3f, 0.2f}, {0.05f, 0.f, 0.1f, 0.f}, {0.35f, 0.4f, 0.5f, 0.4f}},
    {"TUBE WARM", "TUBE", TAB_DRIVE, "valve drive, 2nd harmonic, smooth", {"DRIVE", "EVEN", "BODY", "SAG"},
     {F_PCT, F_PCT, F_PCT, F_PCT}, {0.55f, 0.4f, 0.3f, 0.3f}, {0.2f, 0.2f, 0.1f, 0.1f}, {0.6f, 0.6f, 0.5f, 0.5f}},
    {"FUZZ", "FUZZ", TAB_DRIVE, "germanium fuzz, asymmetric grit", {"GAIN", "BIAS", "GATE", "FAT"},
     {F_PCT, F_PCT, F_PCT, F_PCT}, {0.4f, 0.3f, 0.2f, 0.3f}, {0.2f, 0.1f, 0.f, 0.1f}, {0.5f, 0.5f, 0.3f, 0.5f}},
    {"WAVEFOLD", "FOLD", TAB_DRIVE, "foldback shaping, rich odd harmonics", {"FOLD", "SYM", "STAGE", "LP"},
     {F_PCT, F_PCT, F_PCT, F_PCT}, {0.3f, 0.2f, 0.3f, 0.4f}, {0.1f, 0.f, 0.f, 0.3f}, {0.45f, 0.4f, 0.5f, 0.6f}},
    {"BITCRUSH", "CRUSH", TAB_DRIVE, "low bits and sample rate, jitter", {"BITS", "RATE", "JITTER", "DRIVE"},
     {F_BITS, F_HOLD, F_PCT, F_PCT}, {0.5f, 0.3f, 0.2f, 0.2f}, {0.3f, 0.1f, 0.f, 0.f}, {0.65f, 0.45f, 0.4f, 0.4f}},

    {"DUB FILTER", "DUBF", TAB_FILTER, "state variable 12 dB, LP BP HP notch", {"CUTOFF", "RES", "MODE", "LFO"},
     {F_CUT, F_PCT, F_MODE, F_PCT}, {0.55f, 0.4f, 0.f, 0.2f}, {0.35f, 0.2f, 0.f, 0.f}, {0.75f, 0.6f, 0.34f, 0.4f}},
    {"MOOG LADDER", "MOOG", TAB_FILTER, "4-pole ladder, the acid filter", {"CUTOFF", "RES", "DRIVE", "ENV"},
     {F_CUT, F_PCT, F_PCT, F_PCT}, {0.5f, 0.55f, 0.3f, 0.3f}, {0.35f, 0.3f, 0.1f, 0.1f}, {0.7f, 0.7f, 0.5f, 0.5f}},
    {"FORMANT", "VOWEL", TAB_FILTER, "3 formants, vowel wub", {"VOWEL", "MOVE", "Q", "SHIFT"},
     {F_VOWEL, F_PCT, F_PCT, F_PCT}, {0.75f, 0.3f, 0.5f, 0.5f}, {0.f, 0.1f, 0.3f, 0.35f}, {1.f, 0.5f, 0.7f, 0.65f}},
    {"COMB RES", "COMB", TAB_FILTER, "comb bank tuned to the key", {"NOTE", "FEEDBACK", "DAMP", "CHORD"},
     {F_NOTE, F_PCT, F_PCT, F_COMBCH}, {0.f, 0.7f, 0.4f, 0.5f}, {0.f, 0.5f, 0.2f, 0.f}, {1.f, 0.8f, 0.6f, 1.f}},

    {"CHORUS", "CHOR", TAB_MOD, "analog chorus, slow and wide", {"RATE", "DEPTH", "SPREAD", "VOICES"},
     {F_CHOHZ, F_PCT, F_PCT, F_VOICES}, {0.36f, 0.6f, 0.45f, 0.f}, {0.2f, 0.3f, 0.3f, 0.f}, {0.5f, 0.7f, 0.8f, 1.f}},
    {"PHASER", "PHASE", TAB_MOD, "4 to 8 stage phaser", {"RATE", "DEPTH", "FEEDBACK", "STAGES"},
     {F_PHAHZ, F_PCT, F_PCT, F_STAGES}, {0.35f, 0.6f, 0.5f, 0.f}, {0.2f, 0.4f, 0.2f, 0.f}, {0.5f, 0.8f, 0.6f, 1.f}},
    {"RING MOD", "RING", TAB_MOD, "ring modulator in key, metallic", {"NOTE", "SHAPE", "RING", "SPREAD"},
     {F_NOTE, F_PCT, F_PCT, F_PCT}, {0.5f, 0.f, 0.7f, 0.3f}, {0.f, 0.f, 0.4f, 0.f}, {1.f, 0.3f, 0.8f, 0.5f}},
    {"TREMOLO", "TREM", TAB_MOD, "tremolo gate locked to the clock", {"RATE", "DEPTH", "SHAPE", "STEREO"},
     {F_TREM, F_PCT, F_PCT, F_PCT}, {0.2f, 0.6f, 0.5f, 0.5f}, {0.f, 0.4f, 0.2f, 0.2f}, {0.6f, 0.8f, 0.8f, 0.8f}},
    {"ROBOT", "ROBOT", TAB_MOD, "FORGE vocoder, sings in key", {"NOTE", "CHORD", "VOWEL", "RING"},
     {F_NOTE, F_CHORD, F_PCT, F_PCT}, {0.f, 0.34f, 0.3f, 0.1f}, {0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 0.3f}},

    {"GLITCH", "GLTCH", TAB_GLITCH, "FORGE glitch: roll, reverse, stop", {"CHANCE", "ROLL", "REVERSE", "STOP"},
     {F_PCT, F_ROLL, F_PCT, F_PCT}, {0.3f, 0.5f, 0.3f, 0.2f}, {0.15f, 0.15f, 0.1f, 0.f}, {0.45f, 0.85f, 0.5f, 0.4f}},
    {"STUTTER", "STUTR", TAB_GLITCH, "buffer stutter on the beat", {"CHANCE", "ROLL", "CRUSH", ""},
     {F_PCT, F_ROLL, F_PCT, F_NONE}, {0.35f, 0.5f, 0.f, 0.f}, {0.2f, 0.15f, 0.f, 0.f}, {0.5f, 0.85f, 0.3f, 0.f}},
    {"TAPE STOP", "STOP", TAB_GLITCH, "tape slows to a halt, snaps back", {"CHANCE", "LENGTH", "CURVE", ""},
     {F_PCT, F_STOPLEN, F_PCT, F_NONE}, {0.2f, 0.3f, 0.5f, 0.f}, {0.1f, 0.15f, 0.3f, 0.f}, {0.35f, 0.6f, 0.7f, 0.f}},
    {"REVERSE", "REV", TAB_GLITCH, "reverse slices, backwards ghosts", {"CHANCE", "SLICE", "FADE", ""},
     {F_PCT, F_SLICE, F_FADE, F_NONE}, {0.4f, 0.5f, 0.3f, 0.f}, {0.2f, 0.f, 0.2f, 0.f}, {0.6f, 1.f, 0.5f, 0.f}},
    {"SHUFFLE", "SHUF", TAB_GLITCH, "FORGE beat slicer", {"AMOUNT", "PATTERN", "REVERSE", "LENGTH"},
     {F_PCT, F_SHUFPAT, F_PCT, F_SHUFLEN}, {0.4f, 0.f, 0.2f, 0.5f}, {0.2f, 0.f, 0.f, 0.f}, {0.6f, 1.f, 0.4f, 1.f}},

    {"KING TAPE", "KING", TAB_DELAY, "dotted 1/8 tape echo, darker each repeat", {"TIME", "FEEDBACK", "WOW", "AGE"},
     {F_DIV, F_FB90, F_PCT, F_PCT}, {8 / 12.f, 0.69f, 0.3f, 0.5f}, {0.f, 0.4f, 0.1f, 0.3f}, {1.f, 0.69f, 0.5f, 0.7f}},
    {"DUB THROW", "THROW", TAB_DELAY, "1-bar dub echo, feedback ride", {"TIME", "RIDE", "HI CUT", "LO CUT"},
     {F_DIV, F_RIDE, F_HICUT, F_LOCUT}, {1.f, 0.f, 0.49f, 0.3f}, {0.f, 0.f, 0.35f, 0.2f}, {1.f, 0.2f, 0.6f, 0.45f}},
    {"PING PONG", "PING", TAB_DELAY, "1/8 left, dotted 1/8 right", {"TIME L", "TIME R", "FEEDBACK", "SPREAD"},
     {F_DIV, F_DIV, F_FB80, F_PCT}, {6 / 12.f, 8 / 12.f, 0.375f, 1.f}, {0.f, 0.f, 0.25f, 0.6f}, {1.f, 1.f, 0.5f, 1.f}},
    {"SLAPBACK", "SLAP", TAB_DELAY, "short slap, a little grit", {"TIME", "FEEDBACK", "SAT", "WIDTH"},
     {F_SLAPMS, F_FB45, F_PCT, F_PCT}, {0.4f, 0.2f, 0.3f, 0.4f}, {0.2f, 0.f, 0.1f, 0.2f}, {0.6f, 0.35f, 0.5f, 0.7f}},
    {"WARP DELAY", "WARP", TAB_DELAY, "FORGE delay, repeats pitch-warped", {"TIME", "FEEDBACK", "WARP", "TONE"},
     {F_DIV, F_PCT, F_WARP, F_PCT}, {6 / 12.f, 0.45f, 0.5f, 0.5f}, {0.f, 0.3f, 0.f, 0.35f}, {1.f, 0.6f, 1.f, 0.65f}},

    {"SPRING>CORR", "S>C", TAB_VERB, "dub spring morphing to a vast corridor", {"SPACE", "DECAY", "DAMP", "PRE"},
     {F_SPACE, F_T60, F_DAMP, F_PRE}, {0.3f, 0.62f, 0.45f, 0.1f}, {0.f, 0.45f, 0.3f, 0.f}, {1.f, 0.7f, 0.6f, 0.3f}},
    {"DUB SPRING", "SPRNG", TAB_VERB, "vintage spring, drip and boing", {"DECAY", "DRIP", "DAMP", "PRE"},
     {F_T60, F_PCT, F_DAMP, F_PRE}, {0.49f, 0.6f, 0.4f, 0.f}, {0.35f, 0.4f, 0.3f, 0.f}, {0.6f, 0.8f, 0.6f, 0.1f}},
    {"CORRIDOR 800M", "CORR", TAB_VERB, "vast corridor, long pre-delay", {"DECAY", "PRE", "DAMP", "SIZE"},
     {F_T60, F_PRE, F_DAMP, F_PCT}, {0.88f, 0.34f, 0.5f, 0.5f}, {0.6f, 0.15f, 0.35f, 0.3f}, {0.85f, 0.4f, 0.65f, 0.8f}},
    {"PLATE", "PLATE", TAB_VERB, "cold metal plate", {"DECAY", "PRE", "DAMP", "DIFFUSE"},
     {F_T60, F_PRE, F_DAMP, F_PCT}, {0.64f, 0.09f, 0.35f, 0.6f}, {0.45f, 0.f, 0.25f, 0.4f}, {0.7f, 0.2f, 0.55f, 0.8f}},
    {"SHIMMER", "SHIM", TAB_VERB, "reverb, an octave up in the tail", {"DECAY", "SHIMMER", "DAMP", "PRE"},
     {F_T60, F_PCT, F_DAMP, F_PRE}, {0.7f, 0.4f, 0.45f, 0.1f}, {0.5f, 0.2f, 0.3f, 0.f}, {0.75f, 0.5f, 0.6f, 0.2f}},
    {"ROOM", "ROOM", TAB_VERB, "small chamber", {"DECAY", "SIZE", "DAMP", "PRE"},
     {F_T60, F_PCT, F_DAMP, F_PRE}, {0.33f, 0.4f, 0.4f, 0.04f}, {0.2f, 0.2f, 0.3f, 0.f}, {0.45f, 0.7f, 0.6f, 0.1f}},

    {"PUMP", "PUMP", TAB_DYN, "FORGE pulse: gate and filter in time", {"PATTERN", "DEPTH", "DECAY", "CUTOFF"},
     {F_PULSE, F_PCT, F_PCT, F_PCT}, {1 / 7.f, 0.6f, 0.35f, 0.85f}, {0.f, 0.4f, 0.2f, 0.7f}, {0.86f, 0.75f, 0.5f, 1.f}},
    {"SIDECHAIN", "DUCK", TAB_DYN, "pumping duck on the beat or the SC jack", {"DEPTH", "RELEASE", "CURVE", "RATE"},
     {F_PCT, F_DUCKREL, F_PCT, F_DUCKRATE}, {0.7f, 0.2f, 0.4f, 0.f}, {0.5f, 0.15f, 0.2f, 0.f}, {0.8f, 0.4f, 0.6f, 0.5f}},
    {"GLUE COMP", "GLUE", TAB_DYN, "bus glue, soft knee", {"THRESH", "RATIO", "ATTACK", "RELEASE"},
     {F_THR, F_RATIO, F_ATT, F_REL}, {0.6f, 0.34f, 0.22f, 0.3f}, {0.5f, 0.2f, 0.15f, 0.2f}, {0.75f, 0.45f, 0.4f, 0.5f}},
    {"LIMITER", "LIMIT", TAB_DYN, "peak limiter", {"CEILING", "RELEASE", "DRIVE", ""},
     {F_CEIL, F_REL, F_DRVDB, F_NONE}, {0.833f, 0.3f, 0.2f, 0.f}, {0.75f, 0.2f, 0.f, 0.f}, {0.9f, 0.5f, 0.35f, 0.f}},

    {"DIODE CLIP", "DIODE", TAB_DRIVE, "hard diode clipping, square-wave edge", {"GAIN", "ASYM", "TONE", "BLEND"},
     {F_PCT, F_PCT, F_PCT, F_PCT}, {0.5f, 0.2f, 0.5f, 1.f}, {0.25f, 0.f, 0.3f, 0.5f}, {0.55f, 0.4f, 0.6f, 1.f}},
    {"RECTIFIER", "RECT", TAB_DRIVE, "half to full wave, octave-up fuzz", {"DRIVE", "OCTAVE", "TONE", ""},
     {F_PCT, F_PCT, F_PCT, F_NONE}, {0.3f, 0.6f, 0.5f, 0.f}, {0.1f, 0.3f, 0.3f, 0.f}, {0.5f, 0.9f, 0.6f, 0.f}},
    {"EXCITER", "EXCIT", TAB_DRIVE, "new harmonics up top, air", {"FREQ", "DRIVE", "AMOUNT", ""},
     {F_HPHZ, F_PCT, F_PCT, F_NONE}, {0.5f, 0.4f, 0.4f, 0.f}, {0.3f, 0.2f, 0.2f, 0.f}, {0.7f, 0.6f, 0.5f, 0.f}},
    {"SUB OCTAVER", "SUB", TAB_DRIVE, "an octave down, sub stays mono", {"SUB", "DRY", "LP", ""},
     {F_PCT, F_PCT, F_SUBLP, F_NONE}, {0.4f, 1.f, 0.4f, 0.f}, {0.25f, 0.8f, 0.25f, 0.f}, {0.5f, 1.f, 0.6f, 0.f}},

    {"STEINER", "STNR", TAB_FILTER, "multimode, more vocal than the Moog", {"CUTOFF", "RES", "MODE", "DRIVE"},
     {F_CUT, F_PCT, F_MORPH3, F_PCT}, {0.55f, 0.6f, 0.f, 0.3f}, {0.35f, 0.3f, 0.f, 0.1f}, {0.75f, 0.7f, 0.5f, 0.5f}},
    {"AUTO WAH", "WAH", TAB_FILTER, "envelope filter, talks with the input", {"SENS", "RANGE", "RES", "RELEASE"},
     {F_PCT, F_PCT, F_PCT, F_PCT}, {0.5f, 0.6f, 0.5f, 0.3f}, {0.3f, 0.4f, 0.3f, 0.2f}, {0.7f, 0.8f, 0.7f, 0.5f}},
    {"FILTER SWEEP", "SWEEP", TAB_FILTER, "clocked low- or high-pass sweep", {"RATE", "DEPTH", "RES", "MODE"},
     {F_SWEEP, F_PCT, F_PCT, F_LPHP}, {0.67f, 0.6f, 0.4f, 0.f}, {0.33f, 0.4f, 0.2f, 0.f}, {1.f, 0.7f, 0.6f, 1.f}},
    {"3-BAND EQ", "EQ", TAB_FILTER, "low, mid, high, movable split", {"LOW", "MID", "HIGH", "SPLIT"},
     {F_EQDB, F_EQDB, F_EQDB, F_XOVER}, {0.5f, 0.5f, 0.5f, 0.5f}, {0.4f, 0.4f, 0.4f, 0.3f}, {0.65f, 0.6f, 0.65f, 0.7f}},

    {"FLANGER", "FLANG", TAB_MOD, "jet flanger, positive or negative", {"RATE", "DEPTH", "FEEDBACK", ""},
     {F_PHAHZ, F_PCT, F_PM, F_NONE}, {0.3f, 0.6f, 0.75f, 0.f}, {0.15f, 0.4f, 0.3f, 0.f}, {0.45f, 0.8f, 0.8f, 0.f}},
    {"FM VIBRATO", "VIB", TAB_MOD, "vibrato that fades in on held notes", {"RATE", "DEPTH", "ONSET", ""},
     {F_VIBHZ, F_CENTS, F_ONSET, F_NONE}, {0.5f, 0.16f, 0.4f, 0.f}, {0.3f, 0.1f, 0.2f, 0.f}, {0.6f, 0.3f, 0.6f, 0.f}},
    {"AUTO PAN", "APAN", TAB_MOD, "panning locked to the clock", {"RATE", "DEPTH", "SHAPE", ""},
     {F_PANRATE, F_PCT, F_PCT, F_NONE}, {0.4f, 0.7f, 0.3f, 0.f}, {0.2f, 0.4f, 0.f, 0.f}, {0.8f, 0.8f, 0.6f, 0.f}},
    {"HAAS WIDE", "HAAS", TAB_MOD, "mono to wide with a short right delay", {"TIME", "AMOUNT", "MONO <", ""},
     {F_HAASMS, F_PCT, F_MONOHZ, F_NONE}, {0.67f, 1.f, 0.34f, 0.f}, {0.4f, 0.6f, 0.3f, 0.f}, {0.8f, 1.f, 0.5f, 0.f}},
    {"STEREO IMAGER", "WIDE", TAB_MOD, "wider sides, sub kept mono", {"WIDTH", "MONO <", "", ""},
     {F_WIDTH2, F_MONOHZ, F_NONE, F_NONE}, {0.7f, 0.34f, 0.f, 0.f}, {0.55f, 0.3f, 0.f, 0.f}, {0.8f, 0.5f, 0.f, 0.f}},

    {"RETRIGGER GATE", "RTRIG", TAB_GLITCH, "fast clocked gate chops, beat by beat", {"RATE", "DEPTH", "SHAPE", "CHANCE"},
     {F_RETRIG, F_PCT, F_PCT, F_PCT}, {1 / 3.f, 0.9f, 0.2f, 0.4f}, {0.f, 0.6f, 0.1f, 0.2f}, {0.67f, 1.f, 0.5f, 0.5f}},
    {"CD SKIP", "SKIP", TAB_GLITCH, "skipping CD micro loops", {"CHANCE", "WINDOW", "REPEATS", ""},
     {F_PCT, F_WINMS, F_REPS, F_NONE}, {0.25f, 0.36f, 0.5f, 0.f}, {0.1f, 0.2f, 0.f, 0.f}, {0.35f, 0.6f, 1.f, 0.f}},
    {"DATA CORRUPT", "CORPT", TAB_GLITCH, "dropouts and repeated blocks", {"ERRORS", "BLOCK", "MODE", ""},
     {F_ERR, F_BLOCK, F_CMODE, F_NONE}, {0.4f, 0.4f, 0.5f, 0.f}, {0.15f, 0.2f, 0.f, 0.f}, {0.45f, 0.6f, 1.f, 0.f}},
    {"SR DROPOUT", "SRDRP", TAB_GLITCH, "sudden sample-rate dips, metallic", {"CHANCE", "DEPTH", "HOLD", ""},
     {F_PCT, F_PCT, F_SRHOLD, F_NONE}, {0.3f, 0.6f, 0.f, 0.f}, {0.15f, 0.3f, 0.f, 0.f}, {0.4f, 0.7f, 1.f, 0.f}},
    {"MICRO FREEZE", "FRZ", TAB_GLITCH, "tiny loops that drone on the key", {"NOTE", "FEEDBACK", "CHANCE", ""},
     {F_NOTE, F_PCT, F_PCT, F_NONE}, {0.f, 0.6f, 0.3f, 0.f}, {0.f, 0.4f, 0.1f, 0.f}, {1.f, 0.7f, 0.4f, 0.f}},
    {"PITCH ARP", "ARP", TAB_GLITCH, "steps the input through pitch shifts", {"PATTERN", "RATE", "OCTAVE", ""},
     {F_ARPPAT, F_ARPRATE, F_ARPOCT, F_NONE}, {0.f, 0.5f, 1.f, 0.f}, {0.f, 0.5f, 1.f, 0.f}, {1.f, 1.f, 1.f, 0.f}},

    {"TECHNO TIGHT", "TIGHT", TAB_DELAY, "tight 1/16 digital echo", {"TIME", "FEEDBACK", "TONE", ""},
     {F_DIV, F_FB80, F_PCT, F_NONE}, {3 / 12.f, 0.31f, 0.6f, 0.f}, {0.f, 0.2f, 0.4f, 0.f}, {1.f, 0.4f, 0.7f, 0.f}},
    {"JUNGLE SHORT", "JUNG", TAB_DELAY, "short 87 ms jungle slap", {"TIME", "FEEDBACK", "TONE", ""},
     {F_JUNGMS, F_FB80, F_PCT, F_NONE}, {0.43f, 0.28f, 0.5f, 0.f}, {0.3f, 0.15f, 0.4f, 0.f}, {0.6f, 0.4f, 0.6f, 0.f}},
    {"MICRO DELAY", "MICRO", TAB_DELAY, "12 ms thickener, a hint of phasing", {"TIME", "FEEDBACK", "SPREAD", ""},
     {F_MICROMS, F_PCT, F_PCT, F_NONE}, {0.36f, 0.f, 0.3f, 0.f}, {0.2f, 0.f, 0.1f, 0.f}, {0.5f, 0.3f, 0.6f, 0.f}},
    {"SPACE ECHO", "SPACE", TAB_DELAY, "three tape heads, swirling repeats", {"TIME", "FEEDBACK", "HEADS", "WOW"},
     {F_DIV, F_FB90, F_HEADS, F_PCT}, {8 / 12.f, 0.55f, 1.f, 0.3f}, {0.f, 0.35f, 0.f, 0.1f}, {1.f, 0.6f, 1.f, 0.5f}},

    {"GRAND HALL", "HALL", TAB_VERB, "vast hall for breakdowns", {"DECAY", "PRE", "DAMP", "SIZE"},
     {F_T60, F_PRE, F_DAMP, F_PCT}, {0.81f, 0.12f, 0.45f, 0.5f}, {0.6f, 0.05f, 0.3f, 0.3f}, {0.85f, 0.2f, 0.6f, 0.8f}},
    {"REVERSE PLATE", "RPLT", TAB_VERB, "a backwards tail swelling into the hit", {"DECAY", "LENGTH", "DAMP", ""},
     {F_T60, F_REVLEN, F_DAMP, F_NONE}, {0.49f, 0.3f, 0.4f, 0.f}, {0.35f, 0.1f, 0.3f, 0.f}, {0.6f, 0.5f, 0.6f, 0.f}},
    {"GATED VERB", "GATED", TAB_VERB, "big plate cut short by a gate", {"DECAY", "GATE", "THRESH", "DAMP"},
     {F_T60, F_GATEMS, F_DB60, F_DAMP}, {0.7f, 0.07f, 0.48f, 0.35f}, {0.55f, 0.05f, 0.4f, 0.25f}, {0.8f, 0.4f, 0.6f, 0.5f}},

    {"PARALLEL SMASH", "SMASH", TAB_DYN, "a crushed layer under the clean one", {"THRESH", "MIX", "RELEASE", ""},
     {F_THR, F_PCT, F_RELSH, F_NONE}, {0.3f, 0.3f, 0.16f, 0.f}, {0.2f, 0.2f, 0.1f, 0.f}, {0.5f, 0.4f, 0.4f, 0.f}},
    {"TAPE COMP", "TCOMP", TAB_DYN, "soft tape compression with saturation", {"AMOUNT", "SAT", "SPEED", ""},
     {F_PCT, F_PCT, F_PCT, F_NONE}, {0.5f, 0.12f, 0.5f, 0.f}, {0.3f, 0.05f, 0.3f, 0.f}, {0.7f, 0.3f, 0.7f, 0.f}},
    {"TRANSIENT", "TRANS", TAB_DYN, "sharper attack, shorter sustain", {"ATTACK", "SUSTAIN", "", ""},
     {F_PM, F_PM, F_NONE, F_NONE}, {0.8f, 0.4f, 0.f, 0.f}, {0.55f, 0.35f, 0.f, 0.f}, {0.8f, 0.6f, 0.f, 0.f}},
    {"NOISE GATE", "GATE", TAB_DYN, "closes in the gaps", {"THRESH", "RELEASE", "RANGE", ""},
     {F_DB60, F_REL, F_RANGE, F_NONE}, {0.4f, 0.04f, 0.7f, 0.f}, {0.3f, 0.02f, 0.4f, 0.f}, {0.45f, 0.2f, 0.8f, 0.f}},
};

// Delays and reverbs are "sends": fed from the slot before (a chain link), they pass that signal on and add
// their repeats / tail; every other effect replaces its input.
inline bool isSend(int t) { return t >= 0 && t < T_COUNT && (kFx[t].tab == TAB_DELAY || kFx[t].tab == TAB_VERB); }

static const char* kVowelName[5] = {"A", "E", "I", "O", "U"};
static const char* kModeName4[4] = {"LP", "BP", "HP", "NOTCH"};
static const char* kCombChord[3] = {"ROOT", "+5TH", "+5+OCT"};
static const char* kRobotChord[4] = {"FIFTH", "TRIAD", "7TH", "SUS4"};

inline int robotChord(float v, int scale) {
    bool minor = kScale[clampi(scale, 0, kScaleCount - 1)][2] == 3;
    switch (clampi((int)std::round(v * 3.f), 0, 3)) {
        case 0: return 1;
        case 1: return minor ? 3 : 2;
        case 2: return minor ? 6 : 5;
        default: return 4;
    }
}

// The value of parameter k of an effect, as the OLED shows it.
inline void fmtParam(int type, int k, float v, int root, int scale, char* b, int n) {
    if (type < 0 || type >= T_COUNT) { snprintf(b, n, "-"); return; }
    switch (kFx[type].fmt[k]) {
        case F_NONE: snprintf(b, n, "-"); break;
        case F_DIV: snprintf(b, n, "%s", gforge::kDelayName[divIdx(v)]); break;
        case F_CUT: case F_HICUT: case F_LOCUT: {
            float hz = kFx[type].fmt[k] == F_CUT ? cutHz(v) : kFx[type].fmt[k] == F_HICUT ? hiCutHz(v) : loCutHz(v);
            if (hz >= 1000.f) snprintf(b, n, "%.1fk", hz / 1000.f); else snprintf(b, n, "%d Hz", (int)hz);
        } break;
        case F_MODE: snprintf(b, n, "%s", kModeName4[clampi((int)std::round(v * 3.f), 0, 3)]); break;
        case F_VOWEL: {
            float x = clampf(v, 0.f, 1.f) * 4.f;
            snprintf(b, n, "%s", kVowelName[clampi((int)std::round(x), 0, 4)]);
        } break;
        case F_NOTE: {
            int s = degSemis(paramDeg(v), scale);
            snprintf(b, n, "%s%d", kNoteName[(root + s) % 12], (type == T_RING || type == T_FREEZE ? 4 : type == T_COMB ? 3 : 2) + (root + s) / 12);
        } break;
        case F_COMBCH: snprintf(b, n, "%s", kCombChord[clampi((int)std::round(v * 2.f), 0, 2)]); break;
        case F_CHOHZ: snprintf(b, n, "%.2f Hz", chorusHz(v)); break;
        case F_PHAHZ: snprintf(b, n, "%.2f Hz", phaserHz(v)); break;
        case F_VOICES: snprintf(b, n, "%d", v > 0.5f ? 3 : 2); break;
        case F_STAGES: snprintf(b, n, "%d", 4 + 2 * clampi((int)std::round(v * 2.f), 0, 2)); break;
        case F_TREM: snprintf(b, n, "%s", kTremName[tremIdx(v)]); break;
        case F_CHORD: snprintf(b, n, "%s", kRobotChord[clampi((int)std::round(v * 3.f), 0, 3)]); break;
        case F_ROLL: snprintf(b, n, "%s", gforge::kRollName[clampi((int)std::round(v * 6.f), 0, 6)]); break;
        case F_STOPLEN: snprintf(b, n, "%d/16", stopSteps(v)); break;
        case F_SLICE: snprintf(b, n, "%s", kSliceName[sliceIdx(v)]); break;
        case F_FADE: snprintf(b, n, "%d ms", (int)(1.f + 19.f * v)); break;
        case F_SHUFPAT: snprintf(b, n, "%s", gforge::kShufName[clampi((int)std::round(v * 7.f), 0, 7)]); break;
        case F_SHUFLEN: snprintf(b, n, "%s", gforge::kShufLenName[clampi((int)std::round(v * 2.f), 0, 2)]); break;
        case F_FB90: snprintf(b, n, "%.2f", 0.9f * v); break;
        case F_FB80: snprintf(b, n, "%.2f", 0.8f * v); break;
        case F_FB45: snprintf(b, n, "%.2f", 0.45f * v); break;
        case F_RIDE: snprintf(b, n, "fb %.2f", throwFb(v)); break;
        case F_SLAPMS: snprintf(b, n, "%d ms", (int)(slapSec(v) * 1000.f)); break;
        case F_WARP: snprintf(b, n, "%+d st", kWarpSemis[warpIdx(v)]); break;
        case F_T60: snprintf(b, n, "%.1f s", t60(v)); break;
        case F_DAMP: snprintf(b, n, "%.1fk", dampHz(v) / 1000.f); break;
        case F_PRE: snprintf(b, n, "%d ms", (int)preMs(v)); break;
        case F_PULSE: snprintf(b, n, "%s", gforge::kPulseName[clampi((int)std::round(v * 7.f), 0, 7)]); break;
        case F_DUCKREL: snprintf(b, n, "%d ms", (int)(duckRel(v) * 1000.f)); break;
        case F_DUCKRATE: snprintf(b, n, "%s", kDuckName[duckIdx(v)]); break;
        case F_THR: snprintf(b, n, "%.0f dB", thrDb(v)); break;
        case F_RATIO: snprintf(b, n, "%.1f:1", ratio(v)); break;
        case F_ATT: snprintf(b, n, "%.0f ms", attMs(v)); break;
        case F_REL: snprintf(b, n, "%.0f ms", relMs(v)); break;
        case F_CEIL: snprintf(b, n, "%.1f dB", ceilDb(v)); break;
        case F_DRVDB: snprintf(b, n, "+%.1f dB", 12.f * v); break;
        case F_BITS: snprintf(b, n, "%.1f bit", bits(v)); break;
        case F_HOLD: snprintf(b, n, "/%.0f", holdN(v)); break;
        case F_SPACE: snprintf(b, n, v < 0.15f ? "SPRING" : v > 0.85f ? "CORRIDOR" : "%d%%", (int)(v * 100.f)); break;
        case F_EQDB: snprintf(b, n, "%+.1f dB", eqDb(v)); break;
        case F_HPHZ: snprintf(b, n, "%.1fk", exciteHz(v) / 1000.f); break;
        case F_SUBLP: snprintf(b, n, "%d Hz", (int)subLpHz(v)); break;
        case F_LPHP: snprintf(b, n, v < 0.5f ? "LP" : "HP"); break;
        case F_SWEEP: snprintf(b, n, "%s", kSweepName[sweepIdx(v)]); break;
        case F_MORPH3: snprintf(b, n, v < 0.17f ? "LP" : v < 0.42f ? "LP>BP" : v < 0.58f ? "BP" : v < 0.83f ? "BP>HP" : "HP"); break;
        case F_CENTS: snprintf(b, n, "%d ct", (int)std::round(vibCents(v))); break;
        case F_VIBHZ: snprintf(b, n, "%.1f Hz", vibHz(v)); break;
        case F_ONSET: snprintf(b, n, "%d ms", (int)(onsetSec(v) * 1000.f)); break;
        case F_PANRATE: snprintf(b, n, "%s", kPanName[panIdx(v)]); break;
        case F_HAASMS: snprintf(b, n, "%.1f ms", haasSec(v) * 1000.f); break;
        case F_MONOHZ: snprintf(b, n, "%d Hz", (int)monoHz(v)); break;
        case F_WIDTH2: snprintf(b, n, "%d%%", (int)std::round(v * 200.f)); break;
        case F_RETRIG: snprintf(b, n, "%s", kRetrigName[retrigIdx(v)]); break;
        case F_WINMS: snprintf(b, n, "%d ms", (int)(winSec(v) * 1000.f)); break;
        case F_REPS: snprintf(b, n, "x%d", reps(v)); break;
        case F_ERR: snprintf(b, n, "%d%%", (int)std::round(errRate(v) * 100.f)); break;
        case F_BLOCK: snprintf(b, n, "%d smp", blockN(v)); break;
        case F_CMODE: snprintf(b, n, v < 0.2f ? "ZERO" : v > 0.8f ? "REPEAT" : "BOTH"); break;
        case F_SRHOLD: case F_ARPRATE: snprintf(b, n, "%s", kSrHoldName[srHoldIdx(v)]); break;
        case F_ARPPAT: snprintf(b, n, "%s", kArpName[arpIdx(v)]); break;
        case F_ARPOCT: snprintf(b, n, v < 0.5f ? "-12" : "0"); break;
        case F_JUNGMS: snprintf(b, n, "%d ms", (int)(jungSec(v) * 1000.f)); break;
        case F_MICROMS: snprintf(b, n, "%.1f ms", microSec(v) * 1000.f); break;
        case F_HEADS: snprintf(b, n, "%s", kHeadsName[headsIdx(v)]); break;
        case F_REVLEN: snprintf(b, n, "%.2f s", revLenSec(v)); break;
        case F_GATEMS: snprintf(b, n, "%d ms", (int)(gateSec(v) * 1000.f)); break;
        case F_DB60: snprintf(b, n, "%.0f dB", db60(v)); break;
        case F_RELSH: snprintf(b, n, "%d ms", (int)(relShort(v) * 1000.f)); break;
        case F_RANGE: snprintf(b, n, "-%.0f dB", 60.f * v); break;
        case F_PM: snprintf(b, n, "%+d%%", (int)std::round((v - 0.5f) * 200.f)); break;
        case F_XOVER: snprintf(b, n, "x%.2f", xover(v)); break;
        default: snprintf(b, n, "%d%%", (int)std::round(v * 100.f)); break;
    }
}

// ---------------------------------------------------------------- effects ----------------
struct Fx {
    virtual ~Fx() {}
    virtual void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) = 0;
};

// ---- DRIVE ----
struct TapeSat : Fx {
    Line w[2];
    float low[2] = {}, ph = 0.f, fph = 0.f;
    DcBlock dc[2];
    explicit TapeSat(float sr) { for (Line& l : w) l.init(0.03f, sr); }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr, d = 1.f + 12.f * p[0], b = 0.25f * p[1];
        ph += 0.6f / sr; if (ph >= 1.f) ph -= 1.f;
        fph += 7.f / sr; if (fph >= 1.f) fph -= 1.f;
        float wow = sr * (0.004f + p[3] * (0.0022f * std::sin(2.f * kPi * ph) + 0.0004f * std::sin(2.f * kPi * fph)));
        float kl = coefHz(110.f, sr), in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float x = in[ch];
            low[ch] += (x - low[ch]) * kl;
            x += low[ch] * p[2] * 1.2f;
            float y = drv(x + b, d) - drv(b, d);
            w[ch].write(dc[ch].process(y, sr));
            out[ch] = w[ch].read(wow);
        }
        oL = out[0]; oR = out[1];
    }
};

struct TubeWarm : Fx {
    float pre[2] = {}, env = 0.f;
    DcBlock dc[2];
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr;
        float lvl = 0.5f * (std::fabs(inL) + std::fabs(inR));
        env += (lvl - env) * (lvl > env ? coefT(0.005f, sr) : coefT(0.12f, sr));
        float d = (1.f + 10.f * p[0]) / (1.f + 3.f * p[3] * env);
        float kc = coefHz(18000.f * std::pow(0.12f, p[2]), sr);
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            pre[ch] += (in[ch] - pre[ch]) * kc;
            float x = pre[ch];
            x += p[1] * 0.5f * x * x;   // the 2nd harmonic
            out[ch] = dc[ch].process(drv(x, d), sr);
        }
        oL = out[0]; oR = out[1];
    }
};

struct Fuzz : Fx {
    float pre[2] = {}, env = 0.f, gate = 1.f;
    DcBlock dc[2];
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr;
        float lvl = 0.5f * (std::fabs(inL) + std::fabs(inR));
        env += (lvl - env) * (lvl > env ? coefT(0.002f, sr) : coefT(0.05f, sr));
        float thr = 0.002f + 0.08f * p[2] * p[2];
        float gt = env >= thr ? 1.f : (env / thr) * (env / thr);
        gate += (gt - gate) * coefT(0.004f, sr);
        float g = 1.f + 40.f * p[0] * p[0], b = 0.6f * p[1], off = std::tanh(b);
        float kc = coefHz(12000.f * std::pow(0.1f, p[3]), sr);
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            pre[ch] += (in[ch] - pre[ch]) * kc;
            float x = pre[ch] * g + b;
            float y = x > 0.f ? std::tanh(x) : std::tanh(1.8f * x) * 0.85f;   // the negative half clips harder
            out[ch] = dc[ch].process(y - off, sr) * 0.6f * gate;
        }
        oL = out[0]; oR = out[1];
    }
};

struct Wavefold : Fx {
    float post[2] = {};
    DcBlock dc[2];
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr, g = 1.f + 7.f * p[0], sym = p[1];
        float kc = coefHz(18000.f * std::pow(0.08f, p[3]), sr);
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float y = std::sin(0.5f * kPi * (g * in[ch] + sym));
            y = y * (1.f - p[2]) + std::sin(0.75f * kPi * g * y) * p[2];
            y = dc[ch].process(y, sr);
            post[ch] += (y - post[ch]) * kc;
            out[ch] = post[ch] * 0.7f;
        }
        oL = out[0]; oR = out[1];
    }
};

struct Bitcrush : Fx {
    float hold[2] = {}, cnt = 0.f, next = 1.f;
    Rng rng{11};
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        (void)c;
        float lv = std::pow(2.f, bits(p[0]) - 1.f), dr = 1.f + 3.f * p[3];
        cnt += 1.f;
        if (cnt >= next) {
            cnt = 0.f;
            next = std::max(1.f, holdN(p[1]) * (1.f + p[2] * (rng.next() - 0.5f)));
            float in[2] = {inL, inR};
            for (int ch = 0; ch < 2; ch++) hold[ch] = std::round(std::tanh(dr * in[ch]) / std::tanh(dr) * lv) / lv;
        }
        oL = hold[0]; oR = hold[1];
    }
};

// ---- FILTER ----
struct DubFilter : Fx {
    float ic1[2] = {}, ic2[2] = {}, ph = 0.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr;
        ph += 1.f / std::max(16.f * c.stepLen(), 1.f); if (ph >= 1.f) ph -= 1.f;
        if (c.barStart()) ph = 0.f;
        float fc = cutHz(p[0]) * std::pow(2.f, 2.f * p[3] * std::sin(2.f * kPi * ph));
        float g = std::tan(kPi * clampf(fc, 20.f, sr * 0.45f) / sr);
        float k = 2.f - 1.8f * p[1];
        float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        int m = clampi((int)std::round(p[2] * 3.f), 0, 3);
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float x = in[ch], v3 = x - ic2[ch];
            float v1 = a1 * ic1[ch] + a2 * v3, v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2.f * v1 - ic1[ch]; ic2[ch] = 2.f * v2 - ic2[ch];
            float y = m == 0 ? v2 : m == 1 ? k * v1 : m == 2 ? x - k * v1 - v2 : x - k * v1;
            if (m == 0 || m == 2) y *= 1.f - 0.3f * p[1];
            out[ch] = y / std::sqrt(1.f + 0.2f * y * y);
        }
        oL = out[0]; oR = out[1];
    }
};

struct MoogLadder : Fx {
    float s[2][4] = {}, env = 0.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr;
        float lvl = 0.5f * (std::fabs(inL) + std::fabs(inR));
        env += (lvl - env) * (lvl > env ? coefT(0.005f, sr) : coefT(0.15f, sr));
        float fc = std::min(cutHz(p[0]) * std::pow(2.f, 4.f * p[3] * std::min(1.f, 2.f * env)), 0.3f * sr);
        float g = 1.f - std::exp(-2.f * kPi * fc / sr);
        float k = 3.6f * p[1], dr = 1.f + 3.f * p[2];
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float* st = s[ch];
            float u = std::tanh(dr * in[ch] - k * st[3]);
            st[0] += g * (u - st[0]);
            st[1] += g * (st[0] - st[1]);
            st[2] += g * (st[1] - st[2]);
            st[3] += g * (st[2] - st[3]);
            out[ch] = st[3] * (1.f + 0.6f * k) / (0.6f + 0.4f * dr);
        }
        oL = out[0]; oR = out[1];
    }
};

struct Formant : Fx {
    gforge::Svf f[2][3];
    float ph = 0.f;
    int count = 0;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr;
        ph += 1.f / std::max(32.f * c.stepLen(), 1.f); if (ph >= 1.f) ph -= 1.f;   // two bars per sweep
        if ((count++ & 15) == 0) {
            float v = clampf(p[0] + 0.5f * p[1] * std::sin(2.f * kPi * ph), 0.f, 1.f) * 4.f;
            int a = std::min(3, (int)v); float fr = v - a;
            float kq = 0.6f - 0.5f * p[2], sh = 0.7f * std::pow(2.f, p[3]);
            for (int i = 0; i < 3; i++) {
                float fc = (gforge::kVowel[a][i] + (gforge::kVowel[a + 1][i] - gforge::kVowel[a][i]) * fr) * sh;
                f[0][i].set(fc, sr, kq); f[1][i].set(fc * 1.01f, sr, kq);
            }
        }
        static const float fg[3] = {1.f, 0.7f, 0.45f};
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float y = 0.f;
            for (int i = 0; i < 3; i++) y += f[ch][i].bp(in[ch]) * f[ch][i].k * fg[i];
            out[ch] = y * 3.5f;
        }
        oL = out[0]; oR = out[1];
    }
};

struct CombRes : Fx {
    Line ln[2][3];
    float lp[2][3] = {};
    explicit CombRes(float sr) { for (auto& a : ln) for (Line& l : a) l.init(0.05f, sr); }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        static const float kRatio[3] = {1.f, 1.4983f, 2.f};
        float sr = c.sr;
        float f = rootHz(c.root) * 2.f * std::pow(2.f, degSemis(paramDeg(p[0]), c.scale) / 12.f);
        int nv = 1 + clampi((int)std::round(p[3] * 2.f), 0, 2);
        float fb = 0.92f * p[1], damp = coefHz(9000.f * std::pow(0.15f, p[2]), sr);
        float norm = (1.f - fb) * 1.6f / std::sqrt((float)nv);
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float y = 0.f;
            for (int v = 0; v < 3; v++) {
                float d = sr / (f * kRatio[v] * (ch ? 1.002f : 1.f));
                float r = ln[ch][v].read(d);
                lp[ch][v] += (r - lp[ch][v]) * damp;
                ln[ch][v].write(safe(in[ch] + fb * lp[ch][v]));
                if (v < nv) y += r;
            }
            out[ch] = y * norm;
        }
        oL = out[0]; oR = out[1];
    }
};

// ---- MOD ----
struct Chorus : Fx {
    Line ln[2];
    float ph = 0.f;
    explicit Chorus(float sr) { for (Line& l : ln) l.init(0.06f, sr); }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr;
        ph += chorusHz(p[0]) / sr; if (ph >= 1.f) ph -= 1.f;
        int nv = p[3] > 0.5f ? 3 : 2;
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            ln[ch].write(in[ch]);
            float w = 0.f;
            for (int v = 0; v < nv; v++) {
                float q = ph + (float)v / nv + ch * 0.25f * p[2];
                float ms = 7.f + 12.f * p[1] * (0.5f + 0.5f * std::sin(2.f * kPi * q));
                w += ln[ch].read(ms * 0.001f * sr);
            }
            out[ch] = 0.55f * (in[ch] + w / nv);
        }
        oL = out[0]; oR = out[1];
    }
};

struct Phaser : Fx {
    float z[2][8] = {}, last[2] = {}, ph = 0.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr;
        ph += phaserHz(p[0]) / sr; if (ph >= 1.f) ph -= 1.f;
        int n = 4 + 2 * clampi((int)std::round(p[3] * 2.f), 0, 2);
        float fmax = 200.f * std::pow(2.f, 1.f + 5.f * p[1]);
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float s = 0.5f + 0.5f * std::sin(2.f * kPi * (ph + 0.25f * ch));
            float f = 200.f * std::pow(fmax / 200.f, s);
            float t = std::tan(kPi * std::min(f, 0.45f * sr) / sr), a = (t - 1.f) / (t + 1.f);
            float x = in[ch] + 0.7f * p[2] * last[ch], y = x;
            for (int i = 0; i < n; i++) { float o = a * y + z[ch][i]; z[ch][i] = y - a * o; y = o; }
            last[ch] = safe(y);
            out[ch] = 0.5f * (in[ch] + y);
        }
        oL = out[0]; oR = out[1];
    }
};

struct RingMod : Fx {
    float ph[2] = {};
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr;
        float f = rootHz(c.root) * 4.f * std::pow(2.f, degSemis(paramDeg(p[0]), c.scale) / 12.f);
        float sh = 1.f + 20.f * p[1], shn = std::tanh(sh);
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            ph[ch] += f * (ch ? 1.f + 0.01f * p[3] : 1.f) / sr; if (ph[ch] >= 1.f) ph[ch] -= 1.f;
            float car = std::tanh(std::sin(2.f * kPi * ph[ch]) * sh) / shn;
            float m = p[2] * car + (1.f - p[2]) * (0.5f + 0.5f * car);
            out[ch] = in[ch] * m;
        }
        oL = out[0]; oR = out[1];
    }
};

struct Tremolo : Fx {
    float ph = 0.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float period = kTremSteps[tremIdx(p[0])] * c.stepLen();
        ph += 1.f / std::max(period, 1.f); if (ph >= 1.f) ph -= 1.f;
        if (c.barStart()) ph = 0.f;
        float sh = 1.f + 30.f * p[2], shn = std::tanh(sh);
        auto gain = [&](float q) {
            float l = std::tanh(std::sin(2.f * kPi * q) * sh) / shn;
            return 1.f - p[1] * (0.5f - 0.5f * l);
        };
        float q2 = ph + 0.5f * p[3]; if (q2 >= 1.f) q2 -= 1.f;
        oL = inL * gain(ph); oR = inR * gain(q2);
    }
};

struct RobotFx : Fx {
    gforge::Robot r;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float semis = (float)(3 + c.root + degSemis(paramDeg(p[0]), c.scale));   // above A1
        while (semis > 24.f) semis -= 12.f;
        r.process(inL, inR, c.sr, semis, robotChord(p[1], c.scale), p[2], 0.6f * p[3], 0.4f, oL, oR);
    }
};

// ---- GLITCH ----
struct GlitchFx : Fx {
    gforge::Glitch g;
    bool stutterOnly;
    GlitchFx(float sr, bool stutter) : stutterOnly(stutter) { g.buf.init((int)(sr * 4.f)); }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        int roll = clampi((int)std::round(p[1] * 6.f), 0, 6);
        if (c.edge) {
            bool beat = c.stepIdx % 4 == 0;
            if (stutterOnly) { if (beat) g.trigger(c.stepLen(), p[0], roll, 0.f, 0.f, true, false); }
            else g.trigger(c.stepLen(), p[0], roll, p[2], p[3], beat, false);
        }
        g.process(inL, inR, c.sr, stutterOnly ? p[2] : 0.f, oL, oR);
    }
};

// On a beat, with CHANCE: playback slows to a halt over LENGTH (CURVE = how late it brakes), then snaps
// back to live on the next step with a short crossfade.
struct TapeStop : Fx {
    Line ln[2];
    Rng rng{17};
    bool on = false;
    float lag = 0.f, t = 0.f, len = 1.f, mix = 0.f;
    explicit TapeStop(float sr) { for (Line& l : ln) l.init(6.f, sr); }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr;
        ln[0].write(inL); ln[1].write(inR);
        if (c.edge) {
            if (on && t >= len) on = false;
            else if (!on && c.stepIdx % 4 == 0 && rng.next() < p[0]) {
                on = true; t = 0.f; lag = 1.f;
                len = stopSteps(p[1]) * c.stepLen();
            }
        }
        if (on) {
            float x = std::min(1.f, t / std::max(len, 1.f));
            float speed = 1.f - std::pow(x, 0.4f + 2.f * p[2]);
            lag = std::min(lag + 1.f - speed, ln[0].maxDelay());
            t += 1.f;
        }
        mix += ((on ? 1.f : 0.f) - mix) * coefT(0.004f, sr);
        if (mix < 1e-4f) { oL = inL; oR = inR; return; }
        float sL = ln[0].read(lag), sR = ln[1].read(lag);
        oL = inL + (sL - inL) * mix; oR = inR + (sR - inR) * mix;
    }
};

// At the start of each SLICE, with CHANCE: the slice just gone plays backwards.
struct Reverse : Fx {
    Line ln[2];
    Rng rng{29};
    bool on = false;
    int age = 0, len = 1;
    explicit Reverse(float sr) { for (Line& l : ln) l.init(4.f, sr); }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        ln[0].write(inL); ln[1].write(inR);
        int steps = kSliceSteps[sliceIdx(p[1])];
        if (c.edge && c.stepIdx % steps == 0) {
            on = rng.next() < p[0];
            age = 0;
            len = std::max(64, (int)(steps * c.stepLen()));
        }
        if (!on) { oL = inL; oR = inR; return; }
        float fade = std::max(8.f, (1.f + 19.f * p[2]) * 0.001f * c.sr);
        float w = std::min(1.f, std::min(age / fade, (len - age) / fade));
        w = w * w * (3.f - 2.f * w);
        float d = 2.f * age + 1.f;
        oL = inL + (ln[0].read(d) - inL) * w;
        oR = inR + (ln[1].read(d) - inR) * w;
        if (++age >= len) on = false;
    }
};

struct ShuffleFx : Fx {
    gforge::Shuffle s;
    explicit ShuffleFx(float sr) { s.buf.init((int)(sr * 10.f)); }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        s.process(inL, inR, c.sr, c.stepLen(), c.edge, c.stepIdx, clampi((int)std::round(p[1] * 7.f), 0, 7), p[0], p[2], 0.f,
                  clampi((int)std::round(p[3] * 2.f), 0, 2), oL, oR);
    }
};

// ---- DELAY ----
// Tape-style stereo echo: KING TAPE, DUB THROW, PING PONG and SLAPBACK. Each repeat goes through a low-pass,
// a low cut and soft saturation; a level rider keeps the loop under control even at feedback 0.90.
struct Echo : Fx {
    int mode;
    Line ln[2];
    float tL = 0.375f, tR = 0.375f, lp[2] = {}, hp[2] = {}, low[2] = {}, fbEnv = 0.f, ph = 0.f, fph = 0.f;
    Echo(float sr, int m) : mode(m) { for (Line& l : ln) l.init(8.f, sr); }
    float fitTime(float t) const { while (t > 7.5f) t *= 0.5f; return t; }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr, stepSec = c.stepSec;
        float TL, TR, fb, lpHz, hpHz, drive = 1.3f, wow = 0.f, cross = 0.f;
        switch (mode) {
            case T_KING:
                TL = TR = fitTime(divSteps(p[0]) * stepSec); fb = 0.9f * p[1];
                lpHz = 7000.f * std::pow(0.15f, p[3]); hpHz = 60.f; wow = p[2];
                break;
            case T_THROW:
                TL = TR = fitTime(divSteps(p[0]) * stepSec); fb = throwFb(p[1]);
                lpHz = hiCutHz(p[2]); hpHz = loCutHz(p[3]); wow = 0.2f;
                break;
            case T_PING:
                TL = fitTime(divSteps(p[0]) * stepSec); TR = fitTime(divSteps(p[1]) * stepSec); fb = 0.8f * p[2];
                lpHz = 9000.f; hpHz = 80.f; drive = 1.f; cross = 1.f;
                break;
            case T_TIGHT:
                TL = TR = fitTime(divSteps(p[0]) * stepSec); fb = 0.8f * p[1];
                lpHz = 2000.f * std::pow(8.f, p[2]); hpHz = 100.f; drive = 1.f;
                break;
            case T_JUNGLE:
                TL = jungSec(p[0]); TR = TL + 0.003f; fb = 0.8f * p[1];
                lpHz = 2000.f * std::pow(8.f, p[2]); hpHz = 100.f; drive = 1.2f;
                break;
            case T_MICRO:
                TL = microSec(p[0]); TR = TL + 0.01f * p[2]; fb = 0.6f * p[1];
                lpHz = 12000.f; hpHz = 40.f; drive = 1.f;
                break;
            case T_SPACE:
                TL = TR = fitTime(divSteps(p[0]) * stepSec); fb = 0.9f * p[1];
                lpHz = 5000.f; hpHz = 80.f; drive = 1.5f; wow = p[3];
                break;
            default:   // SLAP
                TL = slapSec(p[0]); TR = TL + 0.015f * p[3]; fb = 0.45f * p[1];
                lpHz = 6000.f; hpHz = 90.f; drive = 1.f + 3.f * p[2];
                break;
        }
        float gl = coefT(0.25f, sr);   // tape: time changes bend the pitch
        tL += (TL - tL) * gl; tR += (TR - tR) * gl;
        ph += 0.5f / sr; if (ph >= 1.f) ph -= 1.f;
        fph += 6.3f / sr; if (fph >= 1.f) fph -= 1.f;
        float wm = wow * sr * (0.0015f * std::sin(2.f * kPi * ph) + 0.00025f * std::sin(2.f * kPi * fph));
        float r[2] = {ln[0].read(tL * sr + wm), ln[1].read(tR * sr + wm)};
        if (mode == T_SPACE) {   // playback heads at 1/3, 2/3 and the whole time, as a multi-head tape echo
            static const bool kOn[4][3] = {{false, false, true}, {false, true, true}, {true, false, true}, {true, true, true}};
            const bool* on = kOn[headsIdx(p[2])];
            for (int ch = 0; ch < 2; ch++) {
                float d = (ch ? tR : tL) * sr + wm, s = 0.f;
                int n = 0;
                for (int h = 0; h < 3; h++) if (on[h]) { s += ln[ch].read(d * (h + 1) / 3.f); n++; }
                r[ch] = s / std::sqrt((float)n);
            }
        }
        float klp = coefHz(lpHz, sr), khp = coefHz(hpHz, sr), klow = coefHz(150.f, sr);
        float x[2];
        for (int ch = 0; ch < 2; ch++) {
            lp[ch] += (r[ch] - lp[ch]) * klp;
            hp[ch] += (lp[ch] - hp[ch]) * khp;
            float y = lp[ch] - hp[ch];
            if (mode == T_KING) { low[ch] += (y - low[ch]) * klow; y += 0.12f * low[ch]; }   // +1 dB low per repeat
            x[ch] = std::tanh(drive * y) / drive;
        }
        float lvl = std::max(std::fabs(x[0]), std::fabs(x[1]));
        fbEnv += (lvl - fbEnv) * (lvl > fbEnv ? 0.01f : 0.0005f);
        float ride = fbEnv > 0.5f ? 0.5f / fbEnv : 1.f;
        float g = fb * ride;
        if (cross > 0.f) {   // ping-pong: the input enters left, each side feeds the other
            ln[0].write(safe(0.5f * (inL + inR) + g * x[1]));
            ln[1].write(safe(g * x[0]));
            float s = p[3];
            oL = r[0] + (1.f - s) * r[1]; oR = r[1] + (1.f - s) * r[0];
            oL *= 1.f / (2.f - s); oR *= 1.f / (2.f - s);
        } else {
            ln[0].write(safe(inL + g * x[0]));
            ln[1].write(safe(inR + g * x[1]));
            oL = r[0]; oR = r[1];
        }
    }
};

struct WarpFx : Fx {
    gforge::WarpDelay w;
    explicit WarpFx(float sr) { w.buf.init((int)(sr * 4.1f)); }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float T = divSteps(p[0]) * c.stepSec;
        while (T > 4.f) T *= 0.5f;
        float fbL, fbR;
        w.read(c.sr, T, p[1], (float)kWarpSemis[warpIdx(p[2])], p[3], fbL, fbR);
        w.write(inL + fbL, inR + fbR, c.sr, true);
        oL = w.wetL; oR = w.wetR;
    }
};

// ---- REVERB ----
// 8-line feedback delay network (Hadamard mix, damped lines, slow modulation), input diffusion and
// pre-delay. SPRING adds an allpass chirp (the drip) and a band-limited tank, SHIMMER an octave-up
// shifter in the loop, SPRING>CORR morphs the spring into the corridor.
struct Verb : Fx {
    int mode;
    Line pre[2], ln[8], ap[2][4];
    float damp[8] = {}, disp[2][12] = {}, hp[2] = {}, lp[2] = {}, lfo = 0.f, shimFb = 0.f;
    gforge::PitchShift ps;
    Verb(float sr, int m) : mode(m) {
        for (Line& l : pre) l.init(0.3f, sr);
        for (Line& l : ln) l.init(0.3f, sr);
        for (auto& a : ap) for (Line& l : a) l.init(0.03f, sr);
    }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        static const float kLenMs[8] = {23.1f, 29.3f, 33.7f, 37.9f, 43.1f, 47.3f, 53.9f, 59.3f};
        static const float kApMs[4] = {4.77f, 3.59f, 12.7f, 9.3f};
        float sr = c.sr;
        float size, T, dHz, preS, diff, drip = 0.f, shim = 0.f;
        bool spring = false;
        switch (mode) {
            case T_SPRING: size = 0.45f; T = t60(p[0]); drip = p[1]; dHz = dampHz(p[2]); preS = preMs(p[3]); diff = 0.5f; spring = true; break;
            case T_CORR: size = 1.4f + 0.6f * p[3]; T = t60(p[0]); preS = preMs(p[1]); dHz = dampHz(p[2]); diff = 0.75f; break;
            case T_PLATE: size = 0.8f; T = t60(p[0]); preS = preMs(p[1]); dHz = dampHz(p[2]); diff = 0.55f + 0.3f * p[3]; break;
            case T_SHIMMER: size = 1.3f; T = t60(p[0]); shim = p[1]; dHz = dampHz(p[2]); preS = preMs(p[3]); diff = 0.7f; break;
            case T_ROOM: size = 0.35f + 0.6f * p[1]; T = t60(p[0]); dHz = dampHz(p[2]); preS = preMs(p[3]); diff = 0.6f; break;
            case T_HALL: size = 1.7f + 0.5f * p[3]; T = t60(p[0]); preS = preMs(p[1]); dHz = dampHz(p[2]); diff = 0.8f; break;
            default: {   // SPRING>CORR
                float m = clampf(p[0], 0.f, 1.f);
                size = 0.45f + 1.35f * m; T = t60(p[1]); dHz = dampHz(p[2]); preS = preMs(p[3]) + 85.f * m;
                drip = 0.6f * (1.f - m); diff = 0.5f + 0.25f * m; spring = m < 0.999f;
            } break;
        }
        preS = std::min(preS, 280.f);
        // pre-delay and input diffusion
        float x[2] = {inL * 0.5f, inR * 0.5f};
        for (int ch = 0; ch < 2; ch++) {
            pre[ch].write(x[ch]);
            float y = pre[ch].read(std::max(1.f, preS * 0.001f * sr));
            if (spring && drip > 0.f) {   // the chirp: a dozen first-order allpasses
                float a = -(0.45f + 0.3f * drip);
                for (int i = 0; i < 12; i++) { float o = a * y + disp[ch][i]; disp[ch][i] = y - a * o; y = o; }
            }
            for (int i = 0; i < 4; i++) {
                float d = ap[ch][i].read(kApMs[i] * (ch ? 1.07f : 1.f) * 0.001f * sr);
                float v = y + diff * d;
                ap[ch][i].write(v);
                y = d - diff * v;
            }
            x[ch] = y;
        }
        // the network
        lfo += 0.5f / sr; if (lfo >= 1.f) lfo -= 1.f;
        float mod = (spring ? 0.1f : 0.8f) * 0.001f * sr * std::sin(2.f * kPi * lfo);
        float kd = coefHz(dHz, sr), y[8], outL = 0.f, outR = 0.f;
        for (int i = 0; i < 8; i++) {
            float len = kLenMs[i] * size * 0.001f * sr + (i == 1 ? mod : i == 5 ? -mod : 0.f);
            float r = ln[i].read(len);
            if (i & 1) outR += r; else outL += r;
            damp[i] += (r - damp[i]) * kd;
            y[i] = damp[i] * std::pow(10.f, -3.f * (len / sr) / T);
        }
        for (int h = 1; h < 8; h <<= 1)   // fast Hadamard
            for (int i = 0; i < 8; i += h << 1)
                for (int j = i; j < i + h; j++) { float a = y[j], b = y[j + h]; y[j] = a + b; y[j + h] = a - b; }
        float sh = 0.f;
        if (shim > 0.f) sh = ps.process(shimFb, 2.f, sr) * shim * 0.35f;
        for (int i = 0; i < 8; i++) {
            float v = y[i] * 0.35355339f * (1.f - 0.25f * shim) + ((i & 1) ? x[1] : x[0]) + sh;
            ln[i].write(safe(v));
        }
        shimFb = 0.25f * (outL + outR);
        outL *= 0.35f; outR *= 0.35f;
        if (spring) {   // a spring tank's band: thin lows, soft highs
            float kh = coefHz(140.f, sr), kl = coefHz(4500.f, sr);
            hp[0] += (outL - hp[0]) * kh; hp[1] += (outR - hp[1]) * kh;
            lp[0] += (outL - hp[0] - lp[0]) * kl; lp[1] += (outR - hp[1] - lp[1]) * kl;
            float m = mode == T_SPRING ? 1.f : 1.f - clampf(p[0], 0.f, 1.f);
            outL += (lp[0] - outL) * m; outR += (lp[1] - outR) * m;
        }
        oL = outL; oR = outR;
    }
};

// ---- DYN ----
struct PumpFx : Fx {
    gforge::Pulse pl;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        pl.process(inL, inR, c.sr, c.stepLen(), c.edge, c.stepIdx, clampi((int)std::round(p[0] * 7.f), 0, 7), p[3], 0.15f,
                   p[1], p[2], 0, false, oL, oR);
    }
};

struct Duck : Fx {
    float env = 0.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        if (c.sc >= 0.f) env = std::max(env, clampf(c.sc * 2.f, 0.f, 1.f));
        else if (c.edge && c.stepIdx % kDuckSteps[duckIdx(p[3])] == 0) env = 1.f;
        float g = 1.f - p[0] * std::pow(env, 0.5f + 2.f * p[2]);
        env *= 1.f - coefT(duckRel(p[1]), c.sr);
        oL = inL * g; oR = inR * g;
    }
};

struct Glue : Fx {
    float grDb = 0.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float thr = thrDb(p[0]), r = ratio(p[1]);
        float lvl = 20.f * std::log10(std::max(std::max(std::fabs(inL), std::fabs(inR)), 1e-6f));
        float over = lvl - thr, knee = 4.f, target;
        if (over <= -knee * 0.5f) target = 0.f;
        else if (over >= knee * 0.5f) target = over * (1.f - 1.f / r);
        else { float q = over + knee * 0.5f; target = (1.f - 1.f / r) * q * q / (2.f * knee); }
        grDb += (target - grDb) * (target > grDb ? coefT(attMs(p[2]) * 0.001f, c.sr) : coefT(relMs(p[3]) * 0.001f, c.sr));
        float g = dbToGain(std::min(12.f, -thr * (1.f - 1.f / r) * 0.4f) - grDb);
        oL = inL * g; oR = inR * g;
    }
};

struct Limiter : Fx {
    float g = 1.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float ceil = dbToGain(ceilDb(p[0])), pre = dbToGain(12.f * p[2]);
        float l = inL * pre, r = inR * pre;
        float pk = std::max(std::fabs(l), std::fabs(r));
        float target = pk > ceil ? ceil / pk : 1.f;
        g = target < g ? target : g + (target - g) * coefT(relMs(p[1]) * 0.001f, c.sr);
        oL = clampf(l * g, -ceil, ceil); oR = clampf(r * g, -ceil, ceil);
    }
};

// ---- added DRIVE ----
struct DiodeClip : Fx {
    float pre[2] = {}, post[2] = {};
    DcBlock dc[2];
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr, g = 1.f + 24.f * p[0] * p[0], b = 0.3f * p[1];
        float kp = coefHz(1500.f * std::pow(12.f, p[2]), sr), kq = coefHz(12000.f, sr);
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            pre[ch] += (in[ch] - pre[ch]) * kp;
            float x = pre[ch] * g + b;
            float x4 = x * x * x * x;
            float y = x / std::sqrt(std::sqrt(1.f + x4 * x4));   // a hard clip with a hair of knee
            y = dc[ch].process(y, sr);
            post[ch] += (y - post[ch]) * kq;
            out[ch] = in[ch] + (post[ch] * 0.6f - in[ch]) * p[3];
        }
        oL = out[0]; oR = out[1];
    }
};

struct Rectifier : Fx {
    float post[2] = {};
    DcBlock dc[2];
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr, d = 1.f + 4.f * p[0], k = coefHz(1000.f * std::pow(16.f, p[2]), sr);
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float x = std::tanh(d * in[ch]);
            float y = std::max(x, 0.f) * (1.f - p[1]) + std::fabs(x) * p[1];   // half wave -> full wave (octave up)
            y = dc[ch].process(y, sr);
            post[ch] += (y - post[ch]) * k;
            out[ch] = post[ch] * 1.4f;
        }
        oL = out[0]; oR = out[1];
    }
};

struct Exciter : Fx {
    float lp[2] = {};
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float k = coefHz(exciteHz(p[0]), c.sr), d = 1.f + 9.f * p[1];
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            lp[ch] += (in[ch] - lp[ch]) * k;
            out[ch] = in[ch] + p[2] * std::tanh(d * (in[ch] - lp[ch])) * 0.5f;
        }
        oL = out[0]; oR = out[1];
    }
};

// A flip-flop divider on the low end: a square an octave down, shaped by the input's level, low-passed.
struct SubOctaver : Fx {
    float a1 = 0.f, a2 = 0.f, env = 0.f, flip = 1.f, s1 = 0.f, s2 = 0.f;
    bool armed = false;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr, k = coefHz(250.f, sr), kl = coefHz(subLpHz(p[2]), sr);
        float m = 0.5f * (inL + inR);
        a1 += (m - a1) * k; a2 += (a1 - a2) * k;
        env += (std::fabs(a2) - env) * coefT(0.01f, sr);
        if (a2 < -0.002f) armed = true;
        else if (a2 > 0.002f && armed) { armed = false; flip = -flip; }   // every rising crossing
        float s = flip * env * 1.6f;
        s1 += (s - s1) * kl; s2 += (s1 - s2) * kl;
        float sub = safe(s2 * p[0] * 1.5f);
        oL = inL * p[1] + sub; oR = inR * p[1] + sub;
    }
};

// ---- added FILTER ----
struct Steiner : Fx {
    float ic1[2] = {}, ic2[2] = {};
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr, g = std::tan(kPi * clampf(cutHz(p[0]), 20.f, sr * 0.45f) / sr);
        float k = 2.f - 1.95f * p[1], dr = 1.f + 4.f * p[3];
        float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        float m = clampf(p[2], 0.f, 1.f) * 2.f;
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float x = std::tanh(dr * in[ch]) / std::sqrt(dr), v3 = x - ic2[ch];
            float v1 = a1 * ic1[ch] + a2 * v3, v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = std::tanh(2.f * v1 - ic1[ch]);   // the saturating resonance: vocal, never runs away
            ic2[ch] = 2.f * v2 - ic2[ch];
            float lp = v2, bp = k * v1, hp = x - k * v1 - v2;
            float y = m < 1.f ? lp + (bp - lp) * m : bp + (hp - bp) * (m - 1.f);
            out[ch] = y * 1.3f;
        }
        oL = out[0]; oR = out[1];
    }
};

struct AutoWah : Fx {
    float ic1[2] = {}, ic2[2] = {}, env = 0.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr, lvl = 0.5f * (std::fabs(inL) + std::fabs(inR));
        env += (lvl - env) * (lvl > env ? coefT(0.004f, sr) : coefT(0.02f + 0.3f * p[3], sr));
        float e = std::min(1.f, env * (1.f + 30.f * p[0] * p[0]));
        float fc = 200.f * std::pow(2.f, (1.f + 5.f * p[1]) * e);
        float g = std::tan(kPi * std::min(fc, sr * 0.45f) / sr), k = 2.f - 1.85f * p[2];
        float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float v3 = in[ch] - ic2[ch];
            float v1 = a1 * ic1[ch] + a2 * v3, v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2.f * v1 - ic1[ch]; ic2[ch] = 2.f * v2 - ic2[ch];
            out[ch] = (k * v1 * 1.2f + 0.25f * v2);
        }
        oL = out[0]; oR = out[1];
    }
};

struct Sweep : Fx {
    float ic1[2] = {}, ic2[2] = {}, ph = 0.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr;
        int steps = kSweepSteps[sweepIdx(p[0])];
        ph += 1.f / std::max(steps * c.stepLen(), 1.f); if (ph >= 1.f) ph -= 1.f;
        if (c.barStart() && steps <= 16) ph = 0.f;
        float tri = 1.f - std::fabs(2.f * ph - 1.f);   // 0 -> 1 -> 0
        bool hp = p[3] >= 0.5f;
        float fc = hp ? 20.f * std::pow(2.f, 7.f * p[1] * tri) : 19000.f * std::pow(2.f, -7.f * p[1] * (1.f - tri));
        float g = std::tan(kPi * clampf(fc, 20.f, sr * 0.45f) / sr), k = 2.f - 1.8f * p[2];
        float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float x = in[ch], v3 = x - ic2[ch];
            float v1 = a1 * ic1[ch] + a2 * v3, v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2.f * v1 - ic1[ch]; ic2[ch] = 2.f * v2 - ic2[ch];
            float y = (hp ? x - k * v1 - v2 : v2) * (1.f - 0.3f * p[2]);
            out[ch] = y / std::sqrt(1.f + 0.2f * y * y);
        }
        oL = out[0]; oR = out[1];
    }
};

struct Eq3 : Fx {
    float lo[2][2] = {}, hi[2][2] = {};
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float xo = xover(p[3]), kl = coefHz(250.f * xo, c.sr), kh = coefHz(3000.f * xo, c.sr);
        float gl = dbToGain(eqDb(p[0])), gm = dbToGain(eqDb(p[1])), gh = dbToGain(eqDb(p[2]));
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float x = in[ch];
            lo[ch][0] += (x - lo[ch][0]) * kl; lo[ch][1] += (lo[ch][0] - lo[ch][1]) * kl;
            hi[ch][0] += (x - hi[ch][0]) * kh; hi[ch][1] += (hi[ch][0] - hi[ch][1]) * kh;
            float low = lo[ch][1], high = x - hi[ch][1], mid = x - low - high;   // the three bands add back to x
            out[ch] = low * gl + mid * gm + high * gh;
        }
        oL = out[0]; oR = out[1];
    }
};

// ---- added MOD ----
struct Flanger : Fx {
    Line ln[2];
    float ph = 0.f, last[2] = {};
    explicit Flanger(float sr) { for (Line& l : ln) l.init(0.03f, sr); }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr, fb = (p[2] - 0.5f) * 1.8f;
        ph += phaserHz(p[0]) / sr; if (ph >= 1.f) ph -= 1.f;
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float ms = 0.5f + 7.f * p[1] * (0.5f + 0.5f * std::sin(2.f * kPi * (ph + 0.25f * ch)));
            float d = ln[ch].read(ms * 0.001f * sr);
            ln[ch].write(safe(in[ch] + fb * d));
            out[ch] = 0.5f * (in[ch] + d);
        }
        oL = out[0]; oR = out[1];
    }
};

// Pitch vibrato from a swept delay; it fades in ONSET after each new note (a jump in level).
struct Vibrato : Fx {
    Line ln[2];
    float ph = 0.f, env = 0.f, slow = 0.f, ramp = 1.f;
    explicit Vibrato(float sr) { for (Line& l : ln) l.init(0.05f, sr); }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr, f = vibHz(p[0]);
        float lvl = 0.5f * (std::fabs(inL) + std::fabs(inR));
        env += (lvl - env) * coefT(0.003f, sr);
        slow += (lvl - slow) * coefT(0.08f, sr);
        if (env > 2.f * slow + 0.01f) ramp = 0.f;   // an onset
        ramp = std::min(1.f, ramp + 1.f / (std::max(onsetSec(p[2]), 0.001f) * sr));
        ph += f / sr; if (ph >= 1.f) ph -= 1.f;
        float a = (std::pow(2.f, vibCents(p[1]) / 1200.f) - 1.f) / (2.f * kPi * f);   // seconds of sweep for that pitch swing
        float d = (0.008f + a * ramp * std::sin(2.f * kPi * ph)) * sr;
        ln[0].write(inL); ln[1].write(inR);
        oL = ln[0].read(d); oR = ln[1].read(d);
    }
};

struct AutoPan : Fx {
    float ph = 0.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float steps = kPanSteps[panIdx(p[0])];
        ph += 1.f / std::max(steps * c.stepLen(), 1.f); if (ph >= 1.f) ph -= 1.f;
        if (c.barStart() && steps <= 16.f) ph = 0.f;
        float sh = 1.f + 30.f * p[2];
        float l = std::tanh(std::sin(2.f * kPi * ph) * sh) / std::tanh(sh);
        float pan = 0.5f + 0.5f * p[1] * l;
        oL = inL * std::min(1.f, 1.41421f * std::cos(pan * 0.5f * kPi));
        oR = inR * std::min(1.f, 1.41421f * std::sin(pan * 0.5f * kPi));
    }
};

struct Haas : Fx {
    Line ln;
    float lo[2] = {};
    explicit Haas(float sr) { ln.init(0.05f, sr); }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float k = coefHz(monoHz(p[2]), c.sr);
        lo[0] += (inL - lo[0]) * k; lo[1] += (inR - lo[1]) * k;
        float hl = inL - lo[0], hr = inR - lo[1];
        ln.write(hl);
        float d = ln.read(std::max(1.f, haasSec(p[0]) * c.sr));
        oL = inL;
        oR = lo[1] + hr * (1.f - p[1]) + d * p[1];   // the lows stay put, the highs arrive late on the right
    }
};

struct Imager : Fx {
    float lp = 0.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float m = 0.5f * (inL + inR), s = 0.5f * (inL - inR);
        lp += (s - lp) * coefHz(monoHz(p[1]), c.sr);
        s = (s - lp) * 2.f * p[0];   // no side below the split
        oL = m + s; oR = m - s;
    }
};

// ---- added GLITCH ----
// A clocked gate (RATE) that, with CHANCE, chops each beat.
struct Retrig : Fx {
    Rng rng{41};
    float t = 0.f, g = 1.f;
    bool on = false;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        if (c.edge && c.stepIdx % 4 == 0) { t = 0.f; on = rng.next() < p[3]; }
        float period = std::max(kRetrigSteps[retrigIdx(p[0])] * c.stepLen(), 8.f);
        float ph = std::fmod(t, period) / period;
        t += 1.f;
        float sq = ph < 0.5f ? 1.f : 0.f, sm = std::sin(kPi * ph); sm *= sm;
        float gate = sq + (sm - sq) * p[2];
        float tg = on ? 1.f - p[1] * (1.f - gate) : 1.f;
        g += (tg - g) * coefT(0.0008f, c.sr);
        oL = inL * g; oR = inR * g;
    }
};

// With CHANCE on a step: the last WINDOW loops REPEATS times, then playback skips back to live.
struct CdSkip : Fx {
    Line ln[2];
    Rng rng{43};
    bool on = false;
    float t = 0.f, w = 1.f, len = 1.f;
    explicit CdSkip(float sr) { for (Line& l : ln) l.init(1.f, sr); }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        ln[0].write(inL); ln[1].write(inR);
        if (c.edge && !on && rng.next() < p[0]) {
            on = true; t = 0.f;
            w = std::max(32.f, winSec(p[1]) * c.sr);
            len = w * reps(p[2]);
        }
        if (!on) { oL = inL; oR = inR; return; }
        float q = std::fmod(t, w), d = t + w - q;
        float f = std::min(1.f, std::min(q, w - q) / std::max(4.f, 0.002f * c.sr));   // soft loop edges
        oL = ln[0].read(d) * f; oR = ln[1].read(d) * f;
        if ((t += 1.f) >= len) on = false;
    }
};

// Every BLOCK samples, with ERRORS: the block drops out or repeats the last one (MODE picks which).
struct Corrupt : Fx {
    Line ln[2];
    Rng rng{47};
    int cnt = 0, act = 0, n = 64;
    float g = 1.f;
    explicit Corrupt(float sr) { for (Line& l : ln) l.init(0.05f, sr); }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        (void)c;
        ln[0].write(inL); ln[1].write(inR);
        if (cnt-- <= 0) {
            n = blockN(p[1]); cnt = n - 1;
            act = rng.next() < errRate(p[0]) ? (rng.next() < p[2] ? 2 : 1) : 0;   // 1 = drop, 2 = repeat
        }
        float tg = act == 1 ? 0.f : 1.f;
        g += (tg - g) * 0.25f;
        if (act == 2) { oL = ln[0].read((float)n + 1.f); oR = ln[1].read((float)n + 1.f); }
        else { oL = inL * g; oR = inR * g; }
    }
};

// With CHANCE on a step: the sample rate drops (DEPTH) for HOLD, folding the top into aliasing.
struct SrDrop : Fx {
    Rng rng{53};
    float left = 0.f, factor = 1.f, cnt = 0.f, hl = 0.f, hr = 0.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        if (c.edge && left <= 0.f && rng.next() < p[0]) {
            left = kSrHoldSteps[srHoldIdx(p[2])] * c.stepLen();
            factor = 2.f + p[1] * 22.f * (0.5f + 0.5f * rng.next());
        }
        if (left <= 0.f) { oL = inL; oR = inR; return; }
        left -= 1.f;
        if ((cnt += 1.f) >= factor) { cnt -= factor; hl = inL; hr = inR; }
        oL = hl; oR = hr;
    }
};

// A loop of one pitch period (NOTE, in key) that rings; with CHANCE on a beat it freezes for the beat
// and drones on its own.
struct MicroFreeze : Fx {
    Line ln[2];
    Rng rng{59};
    float lp[2] = {}, left = 0.f;
    explicit MicroFreeze(float sr) { for (Line& l : ln) l.init(0.05f, sr); }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        if (c.edge && c.stepIdx % 4 == 0 && rng.next() < p[2]) left = 4.f * c.stepLen();
        bool frozen = left > 0.f;
        left -= 1.f;
        float f = rootHz(c.root) * 4.f * std::pow(2.f, degSemis(paramDeg(p[0]), c.scale) / 12.f);
        float d = c.sr / f, k = coefHz(6000.f, c.sr);
        float in[2] = {inL, inR}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float y = ln[ch].read(d * (ch ? 1.003f : 1.f));
            lp[ch] += (y - lp[ch]) * k;
            ln[ch].write(safe(frozen ? lp[ch] * 0.995f : in[ch] + 0.9f * p[1] * lp[ch]));
            out[ch] = y * (1.f - 0.6f * p[1]);
        }
        oL = out[0]; oR = out[1];
    }
};

// The input stepped through a pitch pattern at RATE (CHORD = the key's triad).
struct PitchArp : Fx {
    gforge::PitchShift ps[2];
    int edges = 0;
    float since = 0.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        if (c.barStart()) edges = 0;
        if (c.edge) { edges++; since = 0.f; }
        float pos = (edges + since / std::max(c.stepLen(), 1.f)) / kSrHoldSteps[arpRateIdx(p[1])];
        since += 1.f;
        int third = kScale[clampi(c.scale, 0, kScaleCount - 1)][2];
        const int pat[4][4] = {{0, 7, 12, -5}, {0, 12, 0, -12}, {0, third, 7, 12}, {0, 5, 7, 12}};
        int semis = pat[arpIdx(p[0])][((int)pos) & 3] + (p[2] < 0.5f ? -12 : 0);
        float ratio = std::pow(2.f, semis / 12.f);
        oL = semis == 0 ? inL : ps[0].process(inL, ratio, c.sr);
        oR = semis == 0 ? inR : ps[1].process(inR, ratio, c.sr);
        if (semis == 0) { ps[0].process(inL, 1.f, c.sr); ps[1].process(inR, 1.f, c.sr); }   // keep the buffers fresh
    }
};

// ---- added VERB ----
// The plate's tail played backwards in windows of LENGTH: it swells into the next hit.
struct RevPlate : Fx {
    Verb v;
    Line ln[2];
    float ph = 0.f;
    explicit RevPlate(float sr) : v(sr, T_PLATE) { for (Line& l : ln) l.init(4.5f, sr); }
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float pv[4] = {p[0], 0.f, p[2], 0.6f}, wl, wr;
        v.process(inL, inR, pv, c, wl, wr);
        ln[0].write(wl); ln[1].write(wr);
        float W = revLenSec(p[1]) * c.sr;
        ph += 1.f / W; if (ph >= 1.f) ph -= 1.f;
        float out[2] = {0.f, 0.f};
        for (int h = 0; h < 2; h++) {
            float q = ph + 0.5f * h; if (q >= 1.f) q -= 1.f;
            float s = std::sin(kPi * q); s *= s;
            for (int ch = 0; ch < 2; ch++) out[ch] += s * ln[ch].read(3.f + 2.f * q * W);
        }
        oL = out[0] * 1.3f; oR = out[1] * 1.3f;
    }
};

struct GatedVerb : Fx {
    Verb v;
    float env = 0.f, hold = 0.f, g = 0.f;
    explicit GatedVerb(float sr) : v(sr, T_PLATE) {}
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float pv[4] = {p[0], 0.f, p[3], 0.8f}, wl, wr;
        v.process(inL, inR, pv, c, wl, wr);
        float lvl = std::max(std::fabs(inL), std::fabs(inR));
        env += (lvl - env) * (lvl > env ? 0.5f : coefT(0.01f, c.sr));
        if (env > dbToGain(db60(p[2]))) hold = gateSec(p[1]) * c.sr;
        hold -= 1.f;
        g += ((hold > 0.f ? 1.f : 0.f) - g) * coefT(hold > 0.f ? 0.001f : 0.006f, c.sr);
        oL = wl * g * 1.4f; oR = wr * g * 1.4f;
    }
};

// ---- added DYN ----
struct Smash : Fx {
    float grDb = 0.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float thr = thrDb(p[0]);
        float lvl = 20.f * std::log10(std::max(std::max(std::fabs(inL), std::fabs(inR)), 1e-6f));
        float target = std::max(0.f, lvl - thr) * 0.95f;   // 20:1
        grDb = target > grDb ? target : grDb + (target - grDb) * coefT(relShort(p[2]), c.sr);
        float g = dbToGain(std::min(18.f, -thr * 0.5f) - grDb);
        oL = inL + p[1] * safe(inL * g); oR = inR + p[1] * safe(inR * g);
    }
};

struct TapeComp : Fx {
    float grDb = 0.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float thr = -6.f - 18.f * p[0];
        float lvl = 20.f * std::log10(std::max(std::max(std::fabs(inL), std::fabs(inR)), 1e-6f));
        float target = std::max(0.f, lvl - thr) * (1.f - 1.f / 1.5f);
        float att = 0.03f - 0.025f * p[2], rel = 0.6f - 0.5f * p[2];
        grDb += (target - grDb) * (target > grDb ? coefT(att, c.sr) : coefT(rel, c.sr));
        float g = dbToGain(-thr * 0.33f * 0.6f - grDb), d = 1.f + 12.f * p[1];
        oL = drv(inL * g, d); oR = drv(inR * g, d);
    }
};

struct Transient : Fx {
    float fast = 0.f, slow = 0.f, g = 1.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float sr = c.sr, lvl = std::max(std::fabs(inL), std::fabs(inR));
        fast += (lvl - fast) * (lvl > fast ? coefT(0.001f, sr) : coefT(0.05f, sr));
        slow += (lvl - slow) * (lvl > slow ? coefT(0.03f, sr) : coefT(0.05f, sr));
        float tr = clampf((fast - slow) / std::max(slow, 1e-4f), 0.f, 2.f);   // how much this is a hit
        float a = (p[0] - 0.5f) * 2.f, s = (p[1] - 0.5f) * 2.f;
        float tg = clampf((1.f + a * tr * 0.4f) * (1.f + s * 0.6f * (1.f - std::min(tr, 1.f))), 0.f, 2.f);
        g += (tg - g) * coefT(0.0005f, sr);
        oL = inL * g; oR = inR * g;
    }
};

struct NoiseGate : Fx {
    float env = 0.f, g = 0.f;
    void process(float inL, float inR, const float* p, const Ctx& c, float& oL, float& oR) override {
        float lvl = std::max(std::fabs(inL), std::fabs(inR));
        env += (lvl - env) * (lvl > env ? 0.5f : coefT(0.005f, c.sr));
        bool open = env > dbToGain(db60(p[0]));
        g += ((open ? 1.f : 0.f) - g) * (open ? coefT(0.0005f, c.sr) : coefT(relMs(p[1]) * 0.001f, c.sr));
        float floorG = dbToGain(-60.f * p[2]), gg = floorG + (1.f - floorG) * g;
        oL = inL * gg; oR = inR * gg;
    }
};

// Build an effect (on the UI thread: its buffers are allocated here, never in the audio thread).
inline Fx* makeFx(int t, float sr) {
    switch (t) {
        case T_TAPE: return new TapeSat(sr);
        case T_TUBE: return new TubeWarm;
        case T_FUZZ: return new Fuzz;
        case T_FOLD: return new Wavefold;
        case T_CRUSH: return new Bitcrush;
        case T_DUBF: return new DubFilter;
        case T_MOOG: return new MoogLadder;
        case T_FORMANT: return new Formant;
        case T_COMB: return new CombRes(sr);
        case T_CHORUS: return new Chorus(sr);
        case T_PHASER: return new Phaser;
        case T_RING: return new RingMod;
        case T_TREM: return new Tremolo;
        case T_ROBOT: return new RobotFx;
        case T_GLITCH: return new GlitchFx(sr, false);
        case T_STUTTER: return new GlitchFx(sr, true);
        case T_TSTOP: return new TapeStop(sr);
        case T_REVERSE: return new Reverse(sr);
        case T_SHUFFLE: return new ShuffleFx(sr);
        case T_KING: case T_THROW: case T_PING: case T_SLAP: return new Echo(sr, t);
        case T_WARP: return new WarpFx(sr);
        case T_SPRCOR: case T_SPRING: case T_CORR: case T_PLATE: case T_SHIMMER: case T_ROOM: return new Verb(sr, t);
        case T_PUMP: return new PumpFx;
        case T_DUCK: return new Duck;
        case T_GLUE: return new Glue;
        case T_LIMIT: return new Limiter;
        case T_DIODE: return new DiodeClip;
        case T_RECT: return new Rectifier;
        case T_EXCITER: return new Exciter;
        case T_SUBOCT: return new SubOctaver;
        case T_STEINER: return new Steiner;
        case T_WAH: return new AutoWah;
        case T_SWEEP: return new Sweep;
        case T_EQ3: return new Eq3;
        case T_FLANGER: return new Flanger(sr);
        case T_VIBRATO: return new Vibrato(sr);
        case T_AUTOPAN: return new AutoPan;
        case T_HAAS: return new Haas(sr);
        case T_IMAGER: return new Imager;
        case T_RETRIG: return new Retrig;
        case T_CDSKIP: return new CdSkip(sr);
        case T_CORRUPT: return new Corrupt(sr);
        case T_SRDROP: return new SrDrop;
        case T_FREEZE: return new MicroFreeze(sr);
        case T_ARP: return new PitchArp;
        case T_TIGHT: case T_JUNGLE: case T_MICRO: case T_SPACE: return new Echo(sr, t);
        case T_HALL: return new Verb(sr, t);
        case T_REVPLATE: return new RevPlate(sr);
        case T_GATEDVERB: return new GatedVerb(sr);
        case T_SMASH: return new Smash;
        case T_TAPECOMP: return new TapeComp;
        case T_TRANS: return new Transient;
        default: return new NoiseGate;
    }
}

// ---------------------------------------------------------------- slots ----------------
// Strip banks A..H: what each strip (slot) sets. B..E are the effect's own four parameters.
enum Bank { B_LEVEL, B_P1, B_P2, B_P3, B_P4, B_TONE, B_PAN, B_WIDTH, B_COUNT };
static const char* kBankFx[B_COUNT] = {"LEVEL", "P1", "P2", "P3", "P4", "TONE", "PAN", "WIDTH"};
static const float kBankDef[B_COUNT] = {0.75f, 0.f, 0.f, 0.f, 0.f, 0.5f, 0.5f, 0.5f};
static const int kDefaultSlots[8] = {T_TAPE, T_DUBF, T_KING, T_THROW, T_SPRCOR, T_COMB, T_GLITCH, T_PUMP};

// After the effect: TONE (tilt round 1 kHz), WIDTH (mid / side), PAN (balance), LEVEL; a soft ceiling.
struct Post {
    float lo[2] = {};
    void process(float& l, float& r, const float* b, float sr) {
        float t = 2.f * b[B_TONE] - 1.f, k = coefHz(1000.f, sr);
        float gl = t > 0.f ? 1.f - 0.7f * t : 1.f, gh = t < 0.f ? 1.f + 0.85f * t : 1.f + 0.4f * t;
        float* io[2] = {&l, &r};
        for (int ch = 0; ch < 2; ch++) {
            lo[ch] += (*io[ch] - lo[ch]) * k;
            *io[ch] = lo[ch] * gl + (*io[ch] - lo[ch]) * gh;
        }
        float m = 0.5f * (l + r), s = 0.5f * (l - r) * 2.f * b[B_WIDTH];
        float pan = b[B_PAN], lev = 1.25f * b[B_LEVEL];
        l = safe((m + s) * std::min(1.f, 2.f * (1.f - pan)) * lev);
        r = safe((m - s) * std::min(1.f, 2.f * pan) * lev);
    }
};

// ---------------------------------------------------------------- chains ----------------
// ACID BEDROCK's dub chains: consecutive slots, each linked into the next. dry < 0 leaves DRY alone.
struct ChainDef {
    const char* name;
    const char* desc;
    int n;
    int type[8];
    float p[8][4];
    float level[8];
    float dry;
};
static const int kChainCount = 12;
static const ChainDef kChain[kChainCount] = {
    {"BASS DUB", "tube > SV LP > 1-bar echo > 4 s reverb", 4,
     {T_TUBE, T_DUBF, T_THROW, T_PLATE},
     {{0.45f, 0.4f, 0.35f, 0.3f}, {0.45f, 0.35f, 0.f, 0.f}, {1.f, 0.f, 0.49f, 0.3f}, {0.70f, 0.09f, 0.4f, 0.6f}},
     {0.8f, 0.8f, 0.6f, 0.4f}, 0.f},
    {"DUB BUS TAPE", "tape .14 > SV LP > king tape > corridor > limit", 5,
     {T_TAPE, T_DUBF, T_KING, T_CORR, T_LIMIT},
     {{0.14f, 0.2f, 0.3f, 0.2f}, {0.5f, 0.35f, 0.f, 0.1f}, {8 / 12.f, 0.69f, 0.3f, 0.5f}, {0.88f, 0.34f, 0.5f, 0.5f}, {0.833f, 0.3f, 0.f, 0.f}},
     {0.8f, 0.8f, 0.5f, 0.24f, 0.8f}, 0.f},
    {"DUB VOID", "dry muted, a .90 feedback throw into the corridor", 2,
     {T_THROW, T_CORR},
     {{8 / 12.f, 1.f, 0.49f, 0.3f}, {0.85f, 0.34f, 0.5f, 0.6f}},
     {0.7f, 0.6f}, 0.f},
    {"ACID STANDARD", "tube .55 > moog > chorus > ping pong > room", 5,
     {T_TUBE, T_MOOG, T_CHORUS, T_PING, T_ROOM},
     {{0.55f, 0.4f, 0.3f, 0.3f}, {0.45f, 0.6f, 0.3f, 0.4f}, {0.36f, 0.6f, 0.45f, 0.f}, {6 / 12.f, 8 / 12.f, 0.375f, 1.f}, {0.33f, 0.4f, 0.4f, 0.04f}},
     {0.8f, 0.8f, 0.8f, 0.4f, 0.18f}, 0.f},
    {"KICK IRON", "hard clip > transient > 8:1 > cut 300, lift 5k", 4,
     {T_DIODE, T_TRANS, T_GLUE, T_EQ3},
     {{0.45f, 0.1f, 0.6f, 1.f}, {0.83f, 0.4f, 0.f, 0.f}, {0.5f, 0.85f, 0.f, 0.15f}, {0.5f, 0.35f, 0.6f, 0.5f}},
     {0.8f, 0.8f, 0.8f, 0.8f}, 0.f},
    {"MASTER BUS", "EQ > glue 2.5:1 > tape .12 > limit", 4,
     {T_EQ3, T_GLUE, T_TAPE, T_LIMIT},
     {{0.52f, 0.5f, 0.54f, 0.5f}, {0.6f, 0.34f, 0.22f, 0.3f}, {0.12f, 0.2f, 0.3f, 0.f}, {0.975f, 0.3f, 0.f, 0.f}},
     {0.8f, 0.8f, 0.8f, 0.8f}, 0.f},
    {"JUNGLE ACID", "fuzz > haas 18 ms > 87 ms echo > 14 bit > room", 5,
     {T_FUZZ, T_HAAS, T_JUNGLE, T_CRUSH, T_ROOM},
     {{0.35f, 0.3f, 0.15f, 0.3f}, {0.6f, 1.f, 0.34f, 0.f}, {0.43f, 0.28f, 0.5f, 0.f}, {0.15f, 0.f, 0.f, 0.f}, {0.33f, 0.4f, 0.4f, 0.04f}},
     {0.8f, 0.8f, 0.5f, 0.8f, 0.15f}, 0.f},
    {"SNARE CRACK", "transient > plate 2.2 s gated at 80 ms > 4k lift", 3,
     {T_TRANS, T_GATEDVERB, T_EQ3},
     {{0.75f, 0.45f, 0.f, 0.f}, {0.53f, 0.07f, 0.48f, 0.3f}, {0.5f, 0.5f, 0.65f, 0.6f}},
     {0.8f, 0.6f, 0.8f}, 0.f},
    {"WUB BUS", "formant > smash > chorus .30 Hz > 140% wide > limit", 5,
     {T_FORMANT, T_SMASH, T_CHORUS, T_IMAGER, T_LIMIT},
     {{0.6f, 0.3f, 0.5f, 0.5f}, {0.3f, 0.3f, 0.16f, 0.f}, {0.42f, 0.5f, 0.45f, 0.f}, {0.7f, 0.34f, 0.f, 0.f}, {0.917f, 0.3f, 0.f, 0.f}},
     {0.8f, 0.8f, 0.8f, 0.8f, 0.8f}, 0.f},
    {"GLITCH DRUMS", "shuffle 1/16 > 8 bit > retrigger 1/32 > plate", 4,
     {T_SHUFFLE, T_CRUSH, T_RETRIG, T_PLATE},
     {{0.45f, 1.f, 0.2f, 0.5f}, {0.62f, 0.15f, 0.1f, 0.1f}, {1 / 3.f, 0.9f, 0.2f, 0.4f}, {0.45f, 0.05f, 0.35f, 0.6f}},
     {0.8f, 0.8f, 0.8f, 0.3f}, 0.f},
    {"GLITCH BASS", "pitch arp > tape stop > SR dropout > hard clip", 4,
     {T_ARP, T_TSTOP, T_SRDROP, T_DIODE},
     {{0.f, 0.5f, 1.f, 0.f}, {0.2f, 0.3f, 0.5f, 0.f}, {0.3f, 0.6f, 0.f, 0.f}, {0.4f, 0.1f, 0.5f, 1.f}},
     {0.8f, 0.8f, 0.8f, 0.8f}, 0.f},
    {"DUB SET", "the 8 dark dub slots, unlinked", 8,
     {T_TAPE, T_DUBF, T_KING, T_THROW, T_SPRCOR, T_COMB, T_GLITCH, T_PUMP}, {}, {}, -1.f},
};

} // namespace mfx
