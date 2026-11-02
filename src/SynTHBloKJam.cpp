// ============================================================================
// SynTHBloK-K  JAM  (sequencer + touch strips in one module)
//  Native Instruments Maschine JAM, Controller Editor "Template 4", MIDI channel 1.
//   SEQ   : 8 tracks x 8 steps. pad note = min(127, 12*(PAGE-1) + 56 + col(A..H=0..7) - 8*(row-1)),
//           row 1 = top. LED = same note, velocity = colour index. Scene buttons CC 0..7 pick 1 of 8 patterns.
//   STRIPS: touch strip CC = 48 + 8*bank + strip. 8 CV outs, 8 banks (A..H). Group buttons CC 8..15 = bank.
//  Panel: pattern buttons 1..8 and bank buttons A..H; length, page, bank and LED colours
//  are in the right-click menu. All JAM lights go dark when the module is removed or bypassed.
// ============================================================================
#include "plugin.hpp"
#include <cmath>
#include <cstdio>
#include <string>

#include "JamOwner.hpp"
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

struct SynTHBloKJam : Module {
    enum ParamIds { LEN_PARAM, PAT_PARAM, PAGE_PARAM, OFFC_PARAM, ONC_PARAM, HEADC_PARAM, HITC_PARAM, CLEAR_PARAM, LEARN_PARAM,
                    BANK_PARAM, RANGE_PARAM, BONC_PARAM, BOFFC_PARAM, RUN_PARAM, RESET_PARAM, KNOB_PARAM, NUM_PARAMS };
    enum InputIds { CLOCK_INPUT, RESET_INPUT, RUN_INPUT, NUM_INPUTS };
    enum OutputIds { TRIG_OUTPUT, STRIP_OUTPUT = TRIG_OUTPUT + 8, KNOB_OUTPUT = STRIP_OUTPUT + 8, NUM_OUTPUTS };
    enum LightIds { NUM_LIGHTS };

    midi::InputQueue midiIn;
    midi::Output midiOut;
    jamown::Link jam;   // the JAM follows the last clicked module (JamOwner.hpp)

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

    int lastLed[kNumLed];
    bool matchColors = true;      // JAM lights use the panel colours; off = the LED colour sliders
    int ledScan = 0, lastBank = -1, lastPage = -1;
    int stripPage = 0, lastStripPage = -1;   // JAM touch strip page the strips last sent from
    float tokens = 4.f, resync = 0.f;

    SynTHBloKJam() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
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
        configParam(KNOB_PARAM, 0.f, 1.f, 0.f, "Knob (follows the JAM knob)", "%", 0.f, 100.f);
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
        for (int p = 0; p < 8; p++) for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) pat[p][r][c] = false;
        for (int b = 0; b < 8; b++) for (int i = 0; i < 8; i++) val[b][i] = 0.f;
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
    void onRemove(const RemoveEvent& e) override { if (jam.active) clearLeds(); jamown::release(id); }
    void onBypass(const BypassEvent& e) override { if (jam.active) clearLeds(); jamown::release(id); jam.active = false; }
    void onUnBypass(const UnBypassEvent& e) override { for (int i = 0; i < kNumLed; i++) lastLed[i] = -1; resync = 0.f; }
    ~SynTHBloKJam() { if (jam.active) clearLeds(); jamown::release(id); }

    int desiredLed(int idx, bool running) {
        if (idx == kKnobLed) return (int)std::round(params[KNOB_PARAM].getValue() * 127.f);
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
        if (!jam.active) return;
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
        int own = jam.update(id, true);   // losing the JAM goes dark, gaining it resends all
        if (own > 0) for (int i = 0; i < kNumLed; i++) lastLed[i] = -1;
        else if (own < 0) clearLeds();
        midi::Message msg;
        while (midiIn.tryPop(&msg, args.frame)) {
            if (!jam.active) continue;
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
                    params[KNOB_PARAM].setValue(b / 127.f);
                    lastLed[kKnobLed] = b;   // already showing on the JAM
                }
                else if (a == kKnobPushCC && b > 0) params[KNOB_PARAM].setValue(0.f);
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
            for (int r = 0; r < 8; r++) if (pat[curPat()][r][step] && trackOn(r)) pulse[r].trigger(0.01f);
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
    SynTHBloKJam* module = NULL;
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
    SynTHBloKJam* module = NULL;
    int p = 0;
    JPatBtn() { box.size = mm2px(Vec(6.6f, 6.6f)); }
    void draw(const DrawArgs& args) override {
        bool sel = module ? module->curPat() == p : p == 0;
        drawColorButton(args, box.size, p, sel, (char)('1' + p), 11.f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[SynTHBloKJam::PAT_PARAM].setValue((float)(p + 1));
            e.consume(this);
        }
    }
};

struct JStrip : OpaqueWidget {
    SynTHBloKJam* module = NULL;
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
    SynTHBloKJam* module = NULL;
    int b = 0;
    JBankSq() { box.size = mm2px(Vec(4.8f, 6.6f)); }
    void draw(const DrawArgs& args) override {
        bool sel = module ? module->bank() == b : b == 0;
        drawColorButton(args, box.size, b, sel, (char)('A' + b), 9.f);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            if (module) module->params[SynTHBloKJam::BANK_PARAM].setValue((float)(b + 1));
            e.consume(this);
        }
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
    SynTHBloKJam* jam = NULL;
    const char* label = "";
    bool latch = false, green = false;
    int jack = -1;
    float fontSize = 8.f;
    JAmberBtn() { box.size = mm2px(Vec(10.25f, 6.f)); }
    bool jackPatched() { return jam && jack >= 0 && jam->inputs[jack].isConnected(); }
    bool lit() {
        if (!jam) return green;
        if (jack == SynTHBloKJam::RESET_INPUT && jam->resetFlash > 0.f) return true;
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
    SynTHBloKJam* module = NULL;
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
            else if (module->inputs[SynTHBloKJam::CLOCK_INPUT].isConnected()) {
                if (module->clockPeriod > 0.f && module->sinceClock < 2.f)
                    snprintf(bpm, sizeof bpm, "%.1f BPM", 60.f / (module->clockPeriod * 4.f));
                int len = clamp((int)module->params[SynTHBloKJam::LEN_PARAM].getValue(), 1, 8);
                snprintf(info, sizeof info, "STEP %d/%d", module->step + 1, len);
            }
            nvgFontFaceId(args.vg, font->handle);
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFontSize(args.vg, bigFont);
            nvgFillColor(args.vg, amb);
            nvgText(args.vg, 5.f, H * 0.33f, bpm, NULL);
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
    SynTHBloKJam* module = NULL;
    JKnobRing() { box.size = mm2px(Vec(19.f, 19.f)); }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float v = module ? module->params[SynTHBloKJam::KNOB_PARAM].getValue() : 0.6f;
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

struct SynTHBloKJamWidget : ModuleWidget {
    void onButton(const ButtonEvent& e) override {   // clicking the module hands it the JAM
        SynTHBloKJam* m = getModule<SynTHBloKJam>();
        if (m && e.action == GLFW_PRESS && m->jam.wasEnabled) jamown::claim(m->id);
        ModuleWidget::onButton(e);
    }
    SynTHBloKJamWidget(SynTHBloKJam* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKJam.svg")));
        using namespace rack::componentlibrary;
        const float PW = 127.0f;
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PW - 3.5f, 3.5f))));

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
            addOutput(createOutputCentered<SmallJack>(mm2px(Vec(69.6f, 22.8f + 7.4f * r)), module, SynTHBloKJam::TRIG_OUTPUT + r));
        for (int i = 0; i < 8; i++) {
            addOutput(createOutputCentered<SmallJack>(mm2px(Vec(colX(i) + 3.3f, 81.6f)), module, SynTHBloKJam::STRIP_OUTPUT + i));
            JBankSq* q = createWidget<JBankSq>(mm2px(Vec(colX(i), 85.5f)));
            q->box.size = mm2px(Vec(6.6f, 6.6f));
            q->module = module; q->b = i;
            addChild(q);
            JStrip* s = createWidget<JStrip>(mm2px(Vec(colX(i) + 0.9f, 93.5f)));
            s->box.size = mm2px(Vec(4.8f, 30.f));
            s->module = module; s->idx = i;
            addChild(s);
        }

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
            {"LEARN", SynTHBloKJam::LEARN_PARAM, true, false, -1},
            {"CLEAR", SynTHBloKJam::CLEAR_PARAM, false, false, -1},
            {"RUN", SynTHBloKJam::RUN_PARAM, true, true, SynTHBloKJam::RUN_INPUT},
            {"RESET", SynTHBloKJam::RESET_PARAM, false, false, SynTHBloKJam::RESET_INPUT},
            {"\xC2\xB1" "5V", SynTHBloKJam::RANGE_PARAM, true, false, -1}};
        for (int i = 0; i < 5; i++) {
            JAmberBtn* b = createParam<JAmberBtn>(mm2px(Vec(RX + 9.3f * i, 117.5f)), module, defs[i].param);   // bottom, level with the strips' end
            b->box.size = mm2px(Vec(8.4f, 6.f));
            b->fontSize = 6.5f;
            b->jam = module; b->label = defs[i].label; b->latch = defs[i].latch;
            b->green = defs[i].green; b->jack = defs[i].jack;
            addParam(b);
        }

        // Knob synced with the JAM's knob (its CV output is in the jack row).
        const float KY = 72.f;
        JKnobRing* ring = createWidgetCentered<JKnobRing>(mm2px(Vec(RC, KY)));
        ring->module = module;
        addChild(ring);
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(RC, KY)), module, SynTHBloKJam::KNOB_PARAM));
        label(RC, KY + 12.f, "KNOB");

        // All right-panel jacks in one row just above the buttons, labels on top.
        const char* JNAME[4] = {"RUN", "RESET", "CLOCK", "KNOB"};
        for (int i = 0; i < 4; i++) {
            float x = RX + RW * (i + 0.5f) / 4.f;
            label(x, 105.5f, JNAME[i]);
            if (i < 3) {
                const int JIN[3] = {SynTHBloKJam::RUN_INPUT, SynTHBloKJam::RESET_INPUT, SynTHBloKJam::CLOCK_INPUT};
                addInput(createInputCentered<SmallJack>(mm2px(Vec(x, 110.5f)), module, JIN[i]));
            } else {
                addOutput(createOutputCentered<SmallJack>(mm2px(Vec(x, 110.5f)), module, SynTHBloKJam::KNOB_OUTPUT));
            }
        }
    }

    // Settings that used to be panel knobs live in the right-click menu.
    void appendContextMenu(Menu* menu) override {
        SynTHBloKJam* m = getModule<SynTHBloKJam>();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        auto indexItem = [&](const std::string& text, int paramId, std::vector<std::string> labels) {
            menu->addChild(createIndexSubmenuItem(text, labels,
                [=]() { return (size_t)(m->params[paramId].getValue() - 1.f); },
                [=](size_t i) { m->params[paramId].setValue((float)i + 1.f); }));
        };
        indexItem("Pattern length", SynTHBloKJam::LEN_PARAM, {"1 step", "2 steps", "3 steps", "4 steps", "5 steps", "6 steps", "7 steps", "8 steps"});
        indexItem("JAM matrix page", SynTHBloKJam::PAGE_PARAM, {"1", "2", "3", "4", "5", "6"});
        indexItem("Strip bank", SynTHBloKJam::BANK_PARAM, {"A", "B", "C", "D", "E", "F", "G", "H"});
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolPtrMenuItem("JAM lights match panel colours", "", &m->matchColors));
        menu->addChild(createMenuLabel("Custom JAM LED colours (when not matching)"));
        for (int id : {SynTHBloKJam::OFFC_PARAM, SynTHBloKJam::ONC_PARAM, SynTHBloKJam::HEADC_PARAM,
                       SynTHBloKJam::HITC_PARAM, SynTHBloKJam::BONC_PARAM, SynTHBloKJam::BOFFC_PARAM}) {
            ui::Slider* s = new ui::Slider;
            s->quantity = m->paramQuantities[id];
            s->box.size.x = 220.f;
            menu->addChild(s);
        }
    }
};

} // namespace

Model* modelSynTHBloKJam = createModel<SynTHBloKJam, SynTHBloKJamWidget>("SynTHBloKJam");
