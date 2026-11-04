// ============================================================================
// SynTHBloK MATRIX-SONG  (the conductor: sections A-H across every MATRIX module in the patch)
//  The grid: a row per MATRIX module (found by itself through SongLink.hpp, no cables), a column per song
//  section. A pad holds what that module does in that section: its pattern (or keeps it), a mute, a loop
//  action (PLAY / STOP / REC / ONCE) for the ECHO / FX / LOOP loopers.
//  A section header launches that section on the next bar (or beat). CHAIN plays the song: a list of
//  sections, each for a number of bars, round and round. REC records live launches into the chain.
//  Master clock: CLOCK (16ths), RUN, RESET, BAR and SECT (0-7 V) out; CLOCK in (16ths) or the BPM knob.
// ============================================================================
#include "plugin.hpp"
#include "SongLink.hpp"
#include "OledUi.hpp"
#include "EchoLoopUi.hpp"
#include "Matrix8_ui.hpp"
#include "JackLed.hpp"
#include "KnobTicks.hpp"
#include <atomic>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

static const int kRows = 8, kSecs = 8, kChainMax = 32;
static const char* const kSecName[kSecs] = {"A", "B", "C", "D", "E", "F", "G", "H"};
// a colour per row (a module keeps its row's colour)
static const NVGcolor kRowCol[kRows] = {
    nvgRGB(0xff, 0x5a, 0x3c), nvgRGB(0xff, 0xb3, 0x47), nvgRGB(0xff, 0xd3, 0x5a), nvgRGB(0xc8, 0x8c, 0xff),
    nvgRGB(0x5c, 0xc8, 0xf2), nvgRGB(0x5c, 0xe6, 0xc8), nvgRGB(0x78, 0xe6, 0x5a), nvgRGB(0xd0, 0xd0, 0xd0)};
static const char* const kLoopGlyph[songlink::L_COUNT] = {"", "PLAY", "STOP", "REC", "ONCE"};

struct SynTHBloKMatrixSong : Module {
    enum ParamIds { RUN_PARAM, RESET_PARAM, REC_PARAM, MODE_PARAM, QUANT_PARAM, BPM_PARAM, NUM_PARAMS };
    enum InputIds { CLOCK_INPUT, RUN_INPUT, RESET_INPUT, NUM_INPUTS };
    enum OutputIds { CLOCK_OUTPUT, RUN_OUTPUT, RESET_OUTPUT, BAR_OUTPUT, SECT_OUTPUT, NUM_OUTPUTS };

    struct Cell { int pat = -1; bool mute = false; int loop = songlink::L_NONE; };
    struct Step { int sec = 0, bars = 4; Step() {} Step(int s, int b) : sec(s), bars(b) {} };
    Cell cells[kSecs][kRows];
    int64_t rowId[kRows];
    Step chain[kChainMax];
    int chainN = 0;
    Step rec[kChainMax];
    int recN = 0, recSec = -1, recBars = 0;

    // the song now
    int curSec = 0, chainPos = 0, barInSec = 0;
    std::atomic<int> queued{-1};      // a launch waiting for its bar (or beat)
    std::atomic<int> applyNow{-1};    // stopped: a launch applies at once
    bool chainArm = false, wasRunning = false, recPrev = false, modePrev = false;
    int selSec = 0, selRow = 0;       // the pad the editor shows (UI)

    // clock
    int tick = -1;
    bool running = true;
    float sinceClock = 10.f, clockPeriod = 0.f, internalTimer = 0.f, stepSec = 0.125f;
    dsp::SchmittTrigger clkTrig, runTrig, rstTrig, rstBtnTrig;
    dsp::PulseGenerator clockPulse, resetPulse, barPulse;
    float resetFlash = 0.f, recFlash = 0.f, blink = 0.f;

    SynTHBloKMatrixSong() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
        configSwitch(RUN_PARAM, 0.f, 1.f, 0.f, "Run (starts the song from the top; a trigger at RUN toggles it)", {"Stopped", "Running"});
        configButton(RESET_PARAM, "Reset: the song from the top (a trigger at RESET does too)");
        configSwitch(REC_PARAM, 0.f, 1.f, 0.f, "Record: your launches become the chain (when REC goes off)", {"Off", "Recording"});
        configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Mode", {"LIVE: a section plays until you launch another", "CHAIN: the sections play in the chain's order"});
        configSwitch(QUANT_PARAM, 0.f, 1.f, 0.f, "Launches land on the next", {"Bar", "Beat"});
        configParam(BPM_PARAM, 40.f, 240.f, 120.f, "Tempo (when CLOCK is unpatched)", " BPM");
        paramQuantities[BPM_PARAM]->snapEnabled = true;
        for (int id : {RUN_PARAM, REC_PARAM, MODE_PARAM, QUANT_PARAM}) paramQuantities[id]->randomizeEnabled = false;
        configInput(CLOCK_INPUT, "Clock (16th notes; the BPM knob when unpatched)");
        configInput(RUN_INPUT, "Run (trigger: toggles RUN)");
        configInput(RESET_INPUT, "Reset (trigger)");
        configOutput(CLOCK_OUTPUT, "Clock (16th notes, while running)");
        configOutput(RUN_OUTPUT, "Run (gate)");
        configOutput(RESET_OUTPUT, "Reset (trigger as the song starts or resets)");
        configOutput(BAR_OUTPUT, "Bar (trigger on each downbeat)");
        configOutput(SECT_OUTPUT, "Section (A = 0 V .. H = 7 V)");
        for (int r = 0; r < kRows; r++) rowId[r] = -1;
        defaults();
    }

    void defaults() {
        for (int s = 0; s < kSecs; s++) for (int r = 0; r < kRows; r++) cells[s][r] = Cell();
        for (int r = 0; r < kRows; r++) cells[0][r].pat = 0;   // A: every module on its first pattern
        chainN = 0; curSec = 0; chainPos = 0; selSec = selRow = 0;
    }
    void onReset() override {
        defaults();
        for (int r = 0; r < kRows; r++) rowId[r] = -1;
        tick = -1;
    }

    bool chainOn() { return params[MODE_PARAM].getValue() > 0.5f && chainN > 0; }
    float bpmNow() {
        if (inputs[CLOCK_INPUT].isConnected() && clockPeriod > 0.f) return 15.f / clockPeriod;
        return params[BPM_PARAM].getValue();
    }
    // the bars the chain step at k plays (UI edits are clamped here too)
    int stepBars(int k) { return clamp(chain[k].bars, 1, 64); }

    // switch every row's module to section s
    void apply(int s) {
        s = clamp(s, 0, kSecs - 1);
        if (params[REC_PARAM].getValue() > 0.5f) {   // a live take: the section just left goes into the record
            if (recSec >= 0 && recN < kChainMax) rec[recN++] = {recSec, std::max(1, recBars + (tick > 0 ? 1 : 0))};
            recSec = s; recBars = 0;
        }
        curSec = s; barInSec = 0;
        for (int r = 0; r < kRows; r++) {
            if (rowId[r] < 0) continue;
            const Cell c = cells[s][r];
            songlink::with(rowId[r], [&](songlink::Member& m) {
                if (c.pat >= 0 && m.songPatterns > 0) m.reqPattern = std::min(c.pat, m.songPatterns - 1);
                m.songMuted = c.mute;
                if (c.loop != songlink::L_NONE && m.songHasLoop) m.reqLoop = c.loop;
                m.songLinked = true;
            });
        }
    }
    void launch(int s) {   // UI: running, on the next bar / beat; stopped, at once
        s = clamp(s, 0, kSecs - 1);
        if (running) queued = queued.load() == s ? -1 : s;   // a second press calls it off
        else applyNow = s;
    }
    void endRecord() {
        if (recSec >= 0 && recN < kChainMax) rec[recN++] = {recSec, std::max(1, recBars + (tick > 0 ? 1 : 0))};
        if (recN > 0) { for (int k = 0; k < recN; k++) chain[k] = rec[k]; chainN = recN; }
        recN = 0; recSec = -1; recBars = 0;
    }

    void process(const ProcessArgs& args) override {
        const float dt = args.sampleTime;
        if (runTrig.process(inputs[RUN_INPUT].getVoltage()))
            params[RUN_PARAM].setValue(params[RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f);
        running = params[RUN_PARAM].getValue() > 0.5f;
        bool rst = rstTrig.process(inputs[RESET_INPUT].getVoltage()) | rstBtnTrig.process(params[RESET_PARAM].getValue());
        if (running && !wasRunning) rst = true;   // RUN starts the song from the top
        wasRunning = running;

        // REC: on, a new record starts from the section playing now; off, the record becomes the chain
        bool recOn = params[REC_PARAM].getValue() > 0.5f;
        if (recOn && !recPrev) { recN = 0; recSec = running ? curSec : -1; recBars = 0; }
        if (!recOn && recPrev) endRecord();
        recPrev = recOn;
        // CHAIN switched on: the chain starts from its first step on the next bar
        bool modeOn = params[MODE_PARAM].getValue() > 0.5f;
        if (modeOn && !modePrev) chainArm = true;
        modePrev = modeOn;

        int an = applyNow.exchange(-1);
        if (an >= 0) apply(an);

        // ---- clock: CLOCK (16ths) or the BPM knob ----
        bool edge = false, ext = inputs[CLOCK_INPUT].isConnected();
        sinceClock += dt;
        float intStep = 15.f / clamp(params[BPM_PARAM].getValue(), 40.f, 240.f);
        if (ext) {
            if (clkTrig.process(inputs[CLOCK_INPUT].getVoltage())) {
                if (sinceClock < 4.f) clockPeriod = sinceClock;
                sinceClock = 0.f;
                edge = true;
            }
        } else {
            internalTimer += dt;
            if (internalTimer >= intStep) { internalTimer -= intStep; edge = true; }
        }
        stepSec = ext && clockPeriod > 0.f ? clockPeriod : intStep;
        if (rst) {
            tick = -1; chainPos = 0; internalTimer = 0.f; chainArm = false;
            resetPulse.trigger(1e-3f); resetFlash = 0.15f;
            if (!ext && running) edge = true;   // the internal clock starts on the downbeat at once
        }
        resetFlash = std::max(0.f, resetFlash - dt);
        edge = edge && running;

        if (edge) {
            bool start = tick < 0;
            tick = start ? 0 : (tick + 1) % 16;
            clockPulse.trigger(1e-3f);
            bool bar = tick == 0, beat = tick % 4 == 0;
            if (bar) barPulse.trigger(1e-3f);
            if (bar && !start) { barInSec++; recBars++; }
            int to = -1;
            if (start) { chainPos = 0; to = chainOn() ? chain[0].sec : curSec; }
            else if (bar && chainArm && chainOn()) { chainPos = 0; to = chain[0].sec; }
            else if (bar && chainOn() && barInSec >= stepBars(clamp(chainPos, 0, chainN - 1))) {
                chainPos = (chainPos + 1) % chainN;
                to = chain[chainPos].sec;
            }
            if (bar) chainArm = false;
            int q = queued.load();
            if (q >= 0 && (bar || (params[QUANT_PARAM].getValue() > 0.5f && beat))) {
                queued = -1;
                to = q;
                if (chainOn()) params[MODE_PARAM].setValue(0.f);   // a launch takes over from the chain (LIVE)
                modePrev = false;
            }
            if (to >= 0) apply(to);
        }

        outputs[CLOCK_OUTPUT].setVoltage(clockPulse.process(dt) ? 10.f : 0.f);
        outputs[RUN_OUTPUT].setVoltage(running ? 10.f : 0.f);
        outputs[RESET_OUTPUT].setVoltage(resetPulse.process(dt) ? 10.f : 0.f);
        outputs[BAR_OUTPUT].setVoltage(barPulse.process(dt) ? 10.f : 0.f);
        outputs[SECT_OUTPUT].setVoltage((float)curSec);
        blink += dt;
        if (blink >= 0.5f) blink -= 0.5f;
        recFlash = recOn ? 1.f : 0.f;
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_t* rows = json_array();
        for (int r = 0; r < kRows; r++) json_array_append_new(rows, json_integer(rowId[r]));
        json_object_set_new(root, "rows", rows);
        json_t* cs = json_array();   // [section][row] = [pattern, mute, loop]
        for (int s = 0; s < kSecs; s++) {
            json_t* a = json_array();
            for (int r = 0; r < kRows; r++) {
                const Cell& c = cells[s][r];
                json_t* v = json_array();
                json_array_append_new(v, json_integer(c.pat));
                json_array_append_new(v, json_boolean(c.mute));
                json_array_append_new(v, json_integer(c.loop));
                json_array_append_new(a, v);
            }
            json_array_append_new(cs, a);
        }
        json_object_set_new(root, "cells", cs);
        json_t* ch = json_array();
        for (int k = 0; k < chainN; k++) {
            json_t* v = json_array();
            json_array_append_new(v, json_integer(chain[k].sec));
            json_array_append_new(v, json_integer(chain[k].bars));
            json_array_append_new(ch, v);
        }
        json_object_set_new(root, "chain", ch);
        json_object_set_new(root, "section", json_integer(curSec));
        return root;
    }
    void dataFromJson(json_t* root) override {
        if (json_t* rows = json_object_get(root, "rows"))
            for (int r = 0; r < kRows; r++) { json_t* j = json_array_get(rows, r); rowId[r] = j ? json_integer_value(j) : -1; }
        if (json_t* cs = json_object_get(root, "cells"))
            for (int s = 0; s < kSecs; s++)
                for (int r = 0; r < kRows; r++) {
                    json_t* v = json_array_get(json_array_get(cs, s), r);
                    Cell c;
                    if (v) {
                        c.pat = clamp((int)json_integer_value(json_array_get(v, 0)), -1, 7);
                        c.mute = json_is_true(json_array_get(v, 1));
                        c.loop = clamp((int)json_integer_value(json_array_get(v, 2)), 0, songlink::L_COUNT - 1);
                    }
                    cells[s][r] = c;
                }
        chainN = 0;
        if (json_t* ch = json_object_get(root, "chain"))
            for (size_t k = 0; k < json_array_size(ch) && chainN < kChainMax; k++) {
                json_t* v = json_array_get(ch, k);
                chain[chainN++] = {clamp((int)json_integer_value(json_array_get(v, 0)), 0, kSecs - 1),
                                   clamp((int)json_integer_value(json_array_get(v, 1)), 1, 64)};
            }
        if (json_t* j = json_object_get(root, "section")) curSec = clamp((int)json_integer_value(j), 0, kSecs - 1);
    }
};

typedef SynTHBloKMatrixSong MS;

// ---------------------------------------------------------------- widgets ----------------

// the members found now (UI thread; refreshed each frame by the widget)
struct Found {
    std::vector<songlink::Info> list;
    const songlink::Info* get(int64_t id) const {
        for (const songlink::Info& i : list) if (i.id == id) return &i;
        return NULL;
    }
};

inline std::shared_ptr<window::Font> songFont() { return APP->window->uiFont; }

// A cell in the OLED look (oled::Cell's drawing) that runs code instead of holding a param: lit() and
// the label from functions, a click / right-click / scroll handler.
struct SongBtn : OpaqueWidget {
    std::function<bool()> lit;
    std::function<std::string()> text;
    std::function<void(int)> press;    // 0 left, 1 right
    std::function<void(int)> scroll;   // +1 / -1
    std::string label;
    NVGcolor tint = oled::litFill();
    bool dimOff = false;               // off: drawn fainter (an empty choice)
    float fontSize = 6.f;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        bool on = lit && lit();
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.2f);
        nvgFillColor(args.vg, on ? tint : oled::offFill());
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, on ? tint : oled::outline(dimOff ? 50 : 110));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        std::string s = text ? text() : label;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? oled::ink() : dimOff ? oled::outline(110) : oled::amber());
        nvgText(args.vg, W * 0.5f, H * 0.5f, s.c_str(), NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS) return;
        if (e.button == GLFW_MOUSE_BUTTON_LEFT || e.button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (press) press(e.button == GLFW_MOUSE_BUTTON_LEFT ? 0 : 1);
            e.consume(this);
        }
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (!scroll || e.scrollDelta.y == 0.f) return;
        scroll(e.scrollDelta.y > 0.f ? 1 : -1);
        e.consume(this);
    }
};

struct SongTitle : TransparentWidget {
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
        nvgFontSize(args.vg, 11.f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
        nvgText(args.vg, 0.f, mm2px(3.5f), "SynTHBloK", NULL);
        nvgFontSize(args.vg, 10.f);
        nvgTextLetterSpacing(args.vg, 0.8f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0x7a, 0x00));
        nvgText(args.vg, 0.f, mm2px(6.4f), "MATRIX-SONG", NULL);
    }
};

// A section header: click launches it (and selects it for COPY / CLEAR); playing = filled, waiting = blinks.
struct SecHead : OpaqueWidget {
    MS* module = NULL;
    int s = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        bool playing = module ? module->curSec == s : s == 0;
        bool waiting = module && (module->queued.load() == s || (module->chainOn() && module->chainArm && module->chain[0].sec == s));
        bool sel = module && module->selSec == s;
        bool on = playing || (waiting && module->blink < 0.25f);
        NVGcolor col = kPatOnColor[s];
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.4f);
        nvgFillColor(args.vg, on ? col : kIdle);
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, sel ? nvgRGB(0xff, 0xff, 0xff) : col);
        nvgStrokeWidth(args.vg, sel ? 1.2f : 0.8f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 9.f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? nvgRGB(0x10, 0x10, 0x10) : col);
        nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, kSecName[s], NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || !module) return;
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) { module->launch(s); module->selSec = s; e.consume(this); }
        else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) { module->selSec = s; e.consume(this); }
    }
};

// A row's module: its name (and its pattern now) in the row's colour; click to choose the module.
struct RowLabel : OpaqueWidget {
    MS* module = NULL;
    Found* found = NULL;
    int r = 0;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        const char* name = "-";
        char pat[8] = "";
        bool gone = false;
        static const char* const demo[kRows] = {"DRUM", "DUO", "MATRIX-8", "FORGE", "FX", "ECHO", "LOOP", "-"};
        if (!module) name = demo[r];
        else if (module->rowId[r] >= 0) {
            const songlink::Info* i = found ? found->get(module->rowId[r]) : NULL;
            if (i) { name = i->name; if (i->patNow >= 0) snprintf(pat, sizeof pat, "%d", i->patNow + 1); }
            else { name = "GONE"; gone = true; }
        }
        bool sel = module && module->selRow == r;
        NVGcolor col = gone ? oled::dim() : kRowCol[r];
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 1.2f);
        nvgFillColor(args.vg, nvgRGBA(14, 9, 3, 255));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgTransRGBA(col, sel ? 220 : 70));
        nvgStrokeWidth(args.vg, 0.7f);
        nvgStroke(args.vg);
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 6.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, col);
        nvgText(args.vg, 3.f, box.size.y * 0.5f, name, NULL);
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, oled::amber());
        nvgText(args.vg, box.size.x - 3.f, box.size.y * 0.5f, pat, NULL);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || !module || !found) return;
        if (e.button != GLFW_MOUSE_BUTTON_LEFT && e.button != GLFW_MOUSE_BUTTON_RIGHT) return;
        MS* m = module;
        int row = r;
        m->selRow = row;
        ui::Menu* menu = createMenu();
        menu->addChild(createMenuLabel(string::f("Row %d: the module it conducts", row + 1)));
        for (const songlink::Info& i : found->list) {
            int64_t id = i.id;
            menu->addChild(createCheckMenuItem(string::f("%s  (module %lld)", i.name, (long long)id), "",
                [=]() { return m->rowId[row] == id; },
                [=]() { for (int k = 0; k < kRows; k++) if (m->rowId[k] == id) m->rowId[k] = -1; m->rowId[row] = id; }));
        }
        menu->addChild(createMenuItem("Empty this row", "", [=]() { m->rowId[row] = -1; }));
        e.consume(this);
    }
};

// A pad: what row r's module does in section s. Click selects it for the editor below; scroll steps its
// pattern; right-click mutes it.
struct SongPad : OpaqueWidget {
    MS* module = NULL;
    Found* found = NULL;
    int r = 0, s = 0;
    int patterns() {
        if (!module || module->rowId[r] < 0) return 8;
        const songlink::Info* i = found ? found->get(module->rowId[r]) : NULL;
        return i ? i->patterns : 8;
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        MS::Cell c;
        if (module) c = module->cells[s][r];
        else if (s < 4 && r < 7) { c.pat = (s + r) % 4; c.mute = r == 2 && s == 0; }
        bool used = !module || module->rowId[r] >= 0;
        bool playing = module ? module->curSec == s : s == 0;
        bool sel = module && module->selSec == s && module->selRow == r;
        NVGcolor col = kRowCol[r];
        float W = box.size.x, H = box.size.y;
        bool content = c.pat >= 0 || c.mute || c.loop != songlink::L_NONE;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.f, 0.f, W, H, 1.4f);
        nvgFillColor(args.vg, playing && content && used ? (c.mute ? nvgTransRGBA(col, 70) : col) : kIdle);
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, sel ? nvgRGB(0xff, 0xff, 0xff) : nvgTransRGBA(col, !used ? 25 : content ? 200 : 60));
        nvgStrokeWidth(args.vg, sel ? 1.2f : 0.7f);
        nvgStroke(args.vg);
        if (!used) return;
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        NVGcolor ink = playing && content && !c.mute ? nvgRGB(0x10, 0x10, 0x10) : col;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        if (c.mute) {
            nvgFontSize(args.vg, 8.f);
            nvgFillColor(args.vg, nvgTransRGBA(col, 160));
            nvgText(args.vg, W * 0.5f, H * 0.45f, "M", NULL);
        } else if (c.pat >= 0) {
            char t[4];
            snprintf(t, sizeof t, "%d", c.pat + 1);
            nvgFontSize(args.vg, 9.f);
            nvgFillColor(args.vg, ink);
            nvgText(args.vg, W * 0.5f, c.loop ? H * 0.4f : H * 0.5f, t, NULL);
        }
        if (c.loop != songlink::L_NONE) {
            nvgFontSize(args.vg, 4.6f);
            nvgFillColor(args.vg, ink);
            nvgText(args.vg, W * 0.5f, H * 0.8f, kLoopGlyph[c.loop], NULL);
        }
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || !module) return;
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) { module->selSec = s; module->selRow = r; e.consume(this); }
        else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
            module->selSec = s; module->selRow = r;
            module->cells[s][r].mute = !module->cells[s][r].mute;
            e.consume(this);
        }
    }
    // Delete / Backspace over a pad clears it (keeps its pattern, no mute, no loop action)
    void onHoverKey(const HoverKeyEvent& e) override {
        if (!module || e.action != GLFW_PRESS || (e.key != GLFW_KEY_DELETE && e.key != GLFW_KEY_BACKSPACE)) return;
        module->cells[s][r] = MS::Cell();
        module->selSec = s; module->selRow = r;
        e.consume(this);
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        if (!module || e.scrollDelta.y == 0.f) return;
        int n = patterns();
        if (n > 0) {
            int& p = module->cells[s][r].pat;
            p = clamp(p + (e.scrollDelta.y > 0.f ? 1 : -1), -1, n - 1);
        }
        e.consume(this);
    }
};

// The CELL editor's info line: the selected pad in words.
struct EdDisplay : TransparentWidget {
    MS* module = NULL;
    Found* found = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        const float R = mm2px(oled::kRow);
        nvgBeginPath(args.vg);
        oled::divider(args, 4.f, R, W - 4.f, R);
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        const char* name = "DRUM";
        int s = 0, r = 0;
        MS::Cell c;
        if (module) {
            s = module->selSec; r = module->selRow; c = module->cells[s][r];
            const songlink::Info* i = module->rowId[r] >= 0 && found ? found->get(module->rowId[r]) : NULL;
            name = i ? i->name : module->rowId[r] >= 0 ? "GONE" : "EMPTY ROW";
        } else c.pat = 0;
        char val[64];
        snprintf(val, sizeof val, "%s  %s%s%s", c.pat >= 0 ? string::f("PATTERN %d", c.pat + 1).c_str() : "KEEPS ITS PATTERN",
                 c.mute ? "  MUTED" : "", c.loop ? "  LOOP " : "", c.loop ? songlink::kLoopName[c.loop] : "");
        float y = R * 0.5f;
        nvgFontSize(args.vg, 6.f);
        nvgFillColor(args.vg, oled::dim());
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, 4.f, y, "CELL", NULL);
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, kRowCol[r]);
        std::string who = string::f("%s · %s", name, kSecName[s]);
        nvgText(args.vg, mm2px(8.f), y, who.c_str(), NULL);
        nvgFillColor(args.vg, oled::amber());
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, W - 4.f, y, val, NULL);
    }
};

// The SONG screen: state | bar | tempo; now playing and what waits; the CHAIN title (its cells sit below).
struct SongDisplay : TransparentWidget {
    MS* module = NULL;
    static float chainY() { return 2.f * oled::kRow + 0.4f; }   // the chain's rows of cells start here
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        const float R = mm2px(oled::kRow);
        nvgBeginPath(args.vg);
        oled::divider(args, 4.f, R, W - 4.f, R);
        oled::divider(args, 4.f, 2.f * R, W - 4.f, 2.f * R);
        oled::dividerStroke(args);
        std::shared_ptr<window::Font> font = songFont();
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        bool run = module ? module->running : false;
        bool chainMode = module && module->chainOn();
        char bar[32] = "BAR 1", bpm[16] = "120.0 BPM";
        if (module) {
            int b = module->barInSec + 1;
            if (chainMode) snprintf(bar, sizeof bar, "BAR %d / %d", b, module->stepBars(clamp(module->chainPos, 0, module->chainN - 1)));
            else snprintf(bar, sizeof bar, "BAR %d", b);
            snprintf(bpm, sizeof bpm, "%.1f BPM", module->bpmNow());
        }
        float y0 = R * 0.5f, y1 = R * 1.5f;
        nvgFontSize(args.vg, 6.f);
        nvgFillColor(args.vg, oled::dim());
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, 4.f, y0, "SONG", NULL);
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, run ? oled::green() : oled::dim());
        nvgText(args.vg, mm2px(9.f), y0, run ? (chainMode ? "CHAIN" : "LIVE") : "STOP", NULL);
        nvgFillColor(args.vg, oled::amber());
        nvgText(args.vg, mm2px(22.f), y0, bar, NULL);
        nvgFontSize(args.vg, 8.f);
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, W - 4.f, y0, bpm, NULL);
        // NOW: the section and what comes next
        int cur = module ? module->curSec : 0, q = module ? module->queued.load() : -1;
        nvgFontSize(args.vg, 6.f);
        nvgFillColor(args.vg, oled::dim());
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, 4.f, y1, "NOW", NULL);
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, kPatOnColor[cur]);
        nvgText(args.vg, mm2px(9.f), y1, kSecName[cur], NULL);
        std::string next;
        if (q >= 0) next = string::f("> %s ON THE %s", kSecName[q], module->params[MS::QUANT_PARAM].getValue() > 0.5f ? "BEAT" : "BAR");
        else if (chainMode && module->chainArm) next = string::f("> CHAIN FROM %s", kSecName[module->chain[0].sec]);
        else if (chainMode) next = string::f("NEXT %s", kSecName[module->chain[(module->chainPos + 1) % module->chainN].sec]);
        else if (module && module->params[MS::REC_PARAM].getValue() > 0.5f) next = string::f("RECORDING  %d STEPS", module->recN);
        nvgFillColor(args.vg, q >= 0 || (module && module->params[MS::REC_PARAM].getValue() > 0.5f) ? oled::litFill() : oled::amber());
        nvgText(args.vg, mm2px(14.f), y1, next.c_str(), NULL);
        // the section's progress through its bars (chain) or the bar (live)
        if (module && run) {
            float f = module->tick < 0 ? 0.f : (module->tick + 1) / 16.f;
            if (chainMode) f = (module->barInSec + f) / module->stepBars(clamp(module->chainPos, 0, module->chainN - 1));
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 4.f, 2.f * R - 1.2f, (W - 8.f) * clamp(f, 0.f, 1.f), 0.8f);
            nvgFillColor(args.vg, oled::green());
            nvgFill(args.vg);
        }
    }
};

// A chain step: "A 4" = section A for 4 bars. Click: the playing step's section is selected; scroll: bars;
// right-click: section / bars / insert / delete. The cell after the last adds a step (the selected section).
struct ChainCell : SongBtn {
    MS* module = NULL;
    int k = 0;
    ChainCell() {
        fontSize = 6.f;
        text = [this]() -> std::string {
            static const int demoSec[5] = {0, 1, 0, 2, 3}, demoBars[5] = {4, 8, 4, 8, 4};
            if (!module) return k < 5 ? string::f("%s %d", kSecName[demoSec[k]], demoBars[k]) : "";
            if (k < module->chainN) return string::f("%s %d", kSecName[module->chain[k].sec], module->chain[k].bars);
            return k == module->chainN ? "+" : "";
        };
        lit = [this]() { return module && module->chainOn() && k == module->chainPos && module->running; };
        press = [this](int b) {
            if (!module) return;
            MS* m = module;
            if (k > m->chainN) return;
            if (k == m->chainN) {   // add a step: the selected section, 4 bars
                if (m->chainN < kChainMax) { m->chain[m->chainN] = {m->selSec, 4}; m->chainN++; }
                return;
            }
            if (b == 0) { m->selSec = m->chain[k].sec; return; }
            int kk = k;
            ui::Menu* menu = createMenu();
            menu->addChild(createMenuLabel(string::f("Chain step %d", kk + 1)));
            menu->addChild(createIndexSubmenuItem("Section", {"A", "B", "C", "D", "E", "F", "G", "H"},
                [=]() { return (size_t)m->chain[kk].sec; }, [=](size_t i) { m->chain[kk].sec = (int)i; }));
            menu->addChild(createIndexSubmenuItem("Bars", {"1", "2", "3", "4", "6", "8", "12", "16", "24", "32"},
                [=]() { static const int b[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32}; for (size_t i = 0; i < 10; i++) if (b[i] == m->chain[kk].bars) return i; return (size_t)3; },
                [=](size_t i) { static const int b[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32}; m->chain[kk].bars = b[i]; }));
            menu->addChild(createMenuItem("Insert a copy after", "", [=]() {
                if (m->chainN >= kChainMax) return;
                for (int j = m->chainN; j > kk + 1; j--) m->chain[j] = m->chain[j - 1];
                m->chain[kk + 1] = m->chain[kk]; m->chainN++;
            }));
            menu->addChild(createMenuItem("Delete", "", [=]() {
                for (int j = kk; j < m->chainN - 1; j++) m->chain[j] = m->chain[j + 1];
                m->chainN = std::max(0, m->chainN - 1);
                if (m->chainPos >= m->chainN) m->chainPos = 0;
            }));
            menu->addChild(createMenuItem("Clear the whole chain", "", [=]() { m->chainN = 0; m->chainPos = 0; }));
        };
        scroll = [this](int d) {
            if (!module || k >= module->chainN) return;
            module->chain[k].bars = clamp(module->chain[k].bars + d, 1, 64);
        };
    }
    // Delete / Backspace over a step takes it out of the chain (the steps after it move up)
    void onHoverKey(const HoverKeyEvent& e) override {
        if (!module || k >= module->chainN || e.action != GLFW_PRESS || (e.key != GLFW_KEY_DELETE && e.key != GLFW_KEY_BACKSPACE)) return;
        MS* m = module;
        for (int j = k; j < m->chainN - 1; j++) m->chain[j] = m->chain[j + 1];
        m->chainN--;
        if (m->chainPos >= m->chainN) m->chainPos = 0;
        e.consume(this);
    }
    void step() override {
        dimOff = !module || k >= module->chainN;
        SongBtn::step();
    }
};

struct SynTHBloKMatrixSongWidget : ModuleWidget {
    Found found;
    void step() override {
        MS* m = getModule<MS>();
        if (m) {
            found.list = songlink::list();
            // a module that is not on a row yet takes the first empty row
            for (const songlink::Info& i : found.list) {
                bool placed = false;
                for (int r = 0; r < kRows; r++) if (m->rowId[r] == i.id) placed = true;
                if (placed) continue;
                for (int r = 0; r < kRows; r++) if (m->rowId[r] < 0) { m->rowId[r] = i.id; break; }
            }
        }
        ModuleWidget::step();
    }
    SynTHBloKMatrixSongWidget(MS* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKMatrixSong.svg")));
        using namespace rack::componentlibrary;
        const float PW = 162.56f;   // 32 HP
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(PW - 3.5f, 3.5f))));
        SongTitle* title = createWidget<SongTitle>(mm2px(Vec(8.5f, 0.f)));
        title->box.size = mm2px(Vec(60.f, 7.f));
        addChild(title);
        auto label = [&](float cx, float cy, const char* text) {
            JLabel* l = createWidget<JLabel>(mm2px(Vec(cx - 12.f, cy - 2.f)));
            l->box.size = mm2px(Vec(24.f, 4.f)); l->text = text;
            addChild(l);
        };
        const float CH = oled::kCellH, CG = 1.f, GAP = 0.9f;

        // ---------------- left: the grid (rows = modules, columns = sections) ----------------
        const float GX = 23.f, GP = 9.6f, GS = 8.8f, GY = 19.f;
        for (int s = 0; s < kSecs; s++) {
            SecHead* h = createWidget<SecHead>(mm2px(Vec(GX + GP * s, 10.4f)));
            h->box.size = mm2px(Vec(GS, 6.6f)); h->module = module; h->s = s;
            addChild(h);
        }
        for (int r = 0; r < kRows; r++) {
            RowLabel* l = createWidget<RowLabel>(mm2px(Vec(4.2f, GY + GP * r)));
            l->box.size = mm2px(Vec(17.6f, GS)); l->module = module; l->found = &found; l->r = r;
            addChild(l);
            for (int s = 0; s < kSecs; s++) {
                SongPad* p = createWidget<SongPad>(mm2px(Vec(GX + GP * s, GY + GP * r)));
                p->box.size = mm2px(Vec(GS, GS)); p->module = module; p->found = &found; p->r = r; p->s = s;
                addChild(p);
            }
        }

        // ---------------- left, under the grid: the CELL editor ----------------
        const float EX = 4.2f, EY = 98.2f, EW = 95.4f, EH = oled::kRow + 0.5f + 3.f * CH + 2.f * GAP + 0.8f;
        EdDisplay* ed = createWidget<EdDisplay>(mm2px(Vec(EX, EY)));
        ed->box.size = mm2px(Vec(EW, EH)); ed->module = module; ed->found = &found;
        addChild(ed);
        auto btn = [&](float x, float y, float w, float h, const char* lab) {
            SongBtn* b = createWidget<SongBtn>(mm2px(Vec(x, y)));
            b->box.size = mm2px(Vec(w, h)); b->label = lab;
            addChild(b);
            return b;
        };
        auto selCell = [=]() -> MS::Cell* { return module ? &module->cells[module->selSec][module->selRow] : NULL; };
        {
            const float x0 = EX + 2.f, inner = EW - 4.f;
            float y = EY + oled::kRow + 0.5f;
            // pattern: KEEP | 1 .. 8
            const float pw = (inner - 8.f * CG) / 9.f;
            static const char* const pn[9] = {"KEEP", "1", "2", "3", "4", "5", "6", "7", "8"};
            for (int i = 0; i < 9; i++) {
                SongBtn* b = btn(x0 + i * (pw + CG), y, pw, CH, pn[i]);
                b->lit = [=]() { MS::Cell* c = selCell(); return c ? c->pat == i - 1 : i == 1; };
                b->press = [=](int) { if (MS::Cell* c = selCell()) c->pat = i - 1; };
            }
            // MUTE | LOOP: NONE PLAY STOP REC ONCE
            y += CH + GAP;
            const float lw = (inner - 5.f * CG) / 6.f;
            SongBtn* mu = btn(x0, y, lw, CH, "MUTE");
            mu->tint = nvgRGB(0x5c, 0xc8, 0xf2);
            mu->lit = [=]() { MS::Cell* c = selCell(); return c && c->mute; };
            mu->press = [=](int) { if (MS::Cell* c = selCell()) c->mute = !c->mute; };
            static const char* const ln[5] = {"NO LOOP", "PLAY", "STOP", "REC", "ONCE"};
            for (int a = 0; a < 5; a++) {
                SongBtn* b = btn(x0 + (a + 1) * (lw + CG), y, lw, CH, ln[a]);
                if (a == songlink::L_REC) b->tint = nvgRGB(0xff, 0x32, 0x28);
                if (a == songlink::L_PLAY || a == songlink::L_ONCE) b->tint = oled::green();
                b->lit = [=]() { MS::Cell* c = selCell(); return c ? c->loop == a : a == 0; };
                b->press = [=](int) { if (MS::Cell* c = selCell()) c->loop = a; };
            }
            // the selected section: COPY to the next, CLEAR it; the selected row across every section: FILL, CLEAR
            y += CH + GAP;
            const float tw = (inner - 4.f * CG) / 5.f;
            static const char* const tn[5] = {"CLEAR CELL", "COPY SECTION >", "CLEAR SECTION", "CELL TO ROW", "CLEAR ROW"};
            for (int i = 0; i < 5; i++) {
                SongBtn* b = btn(x0 + i * (tw + CG), y, tw, CH, tn[i]);
                b->fontSize = 5.5f;
                b->lit = []() { return false; };
                b->press = [=](int) {
                    if (!module) return;
                    MS* m = module;
                    int s = m->selSec, r = m->selRow;
                    if (i == 0) m->cells[s][r] = MS::Cell();
                    if (i == 1) { int to = (s + 1) % kSecs; for (int k = 0; k < kRows; k++) m->cells[to][k] = m->cells[s][k]; m->selSec = to; }
                    if (i == 2) for (int k = 0; k < kRows; k++) m->cells[s][k] = MS::Cell();
                    if (i == 3) for (int k = 0; k < kSecs; k++) m->cells[k][r] = m->cells[s][r];
                    if (i == 4) for (int k = 0; k < kSecs; k++) m->cells[k][r] = MS::Cell();
                };
            }
        }

        // ---------------- right: the SONG screen, transport, tempo, jacks ----------------
        const float SX = 101.6f, SW = PW - SX - 4.2f, SY = 10.4f;
        const float cy0 = SY + SongDisplay::chainY(), ctY = cy0 + 4.f * (CH + GAP) + 0.4f;
        const float SH = ctY - SY + 2.f * CH + GAP + 1.2f;
        SongDisplay* sd = createWidget<SongDisplay>(mm2px(Vec(SX, SY)));
        sd->box.size = mm2px(Vec(SW, SH)); sd->module = module;
        addChild(sd);
        const float x0 = SX + 2.f, inner = SW - 4.f;
        // the chain: 4 rows of 8 steps
        const float cw = (inner - 7.f * CG) / 8.f;
        for (int k = 0; k < kChainMax; k++) {
            ChainCell* c = createWidget<ChainCell>(mm2px(Vec(x0 + (k % 8) * (cw + CG), cy0 + (k / 8) * (CH + GAP))));
            c->box.size = mm2px(Vec(cw, CH)); c->module = module; c->k = k;
            addChild(c);
        }
        // REC | RUN | RESET (tall), then LIVE | CHAIN and BAR | BEAT
        auto pcell = [&](float x, float y, float w, float h, int param, const char* lab) {
            oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(x, y)), module, param);
            c->box.size = mm2px(Vec(w, h)); c->module = module; c->label = lab;
            addParam(c);
            return c;
        };
        const float tw = (inner - 4.f * CG) / 5.f, TH = 2.f * CH + GAP;
        echoloop::TintCell* rc = createParam<echoloop::TintCell>(mm2px(Vec(x0, ctY)), module, MS::REC_PARAM);
        rc->box.size = mm2px(Vec(tw, TH)); rc->module = module; rc->label = "REC"; rc->latch = true; rc->fontSize = 7.f;
        addParam(rc);
        echoloop::TintCell* run = createParam<echoloop::TintCell>(mm2px(Vec(x0 + tw + CG, ctY)), module, MS::RUN_PARAM);
        run->box.size = mm2px(Vec(tw, TH)); run->module = module; run->label = "RUN"; run->latch = true; run->fontSize = 7.f;
        run->tint = oled::green(); run->jack = MS::RUN_INPUT;
        addParam(run);
        oled::Cell* rs = pcell(x0 + 2.f * (tw + CG), ctY, tw, TH, MS::RESET_PARAM, "RESET");
        rs->fontSize = 7.f; rs->jack = MS::RESET_INPUT;
        if (module) rs->flash = &module->resetFlash;
        static const char* const modeN[2] = {"LIVE", "CHAIN"}, *const quantN[2] = {"BAR", "BEAT"};
        for (int i = 0; i < 2; i++) {
            oled::Cell* a = pcell(x0 + (3 + i) * (tw + CG), ctY, tw, CH, MS::MODE_PARAM, modeN[i]);
            a->setTo = i;
            oled::Cell* b = pcell(x0 + (3 + i) * (tw + CG), ctY + CH + GAP, tw, CH, MS::QUANT_PARAM, quantN[i]);
            b->setTo = i;
        }
        // BPM
        const float KY = 70.f, KX = SX + SW * 0.5f;
        addChild(KnobTicks::create(Vec(KX, KY)));
        ParamWidget* kn = createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(KX, KY)), module, MS::BPM_PARAM);
        if (Knob* k = dynamic_cast<Knob*>(kn)) k->snap = true;
        addParam(kn);
        label(KX, KY + 7.f, "BPM");

        // jacks: inputs at the bottom left, outputs at the bottom right
        auto jack = [&](float x, float y, int id, bool out, const char* name) {
            label(x, y - 5.5f, name);
            if (out) {
                addOutput(createOutputCentered<SmallJack>(mm2px(Vec(x, y)), module, id));
                addChild(JackLed::create(Vec(x + 3.9f, y), module, id, true));
            } else addInput(createInputCentered<SmallJack>(mm2px(Vec(x, y)), module, id));
        };
        const float JY = 118.f;
        jack(SX + 10.f, 98.f, MS::CLOCK_INPUT, false, "CLK IN");
        jack(SX + SW * 0.5f, 98.f, MS::RUN_INPUT, false, "RUN");
        jack(SX + SW - 10.f, 98.f, MS::RESET_INPUT, false, "RESET");
        static const char* const on[5] = {"CLOCK", "RUN", "RESET", "BAR", "SECT"};
        for (int i = 0; i < 5; i++) jack(SX + 5.5f + i * (SW - 11.f) / 4.f, JY, MS::CLOCK_OUTPUT + i, true, on[i]);
    }
    void appendContextMenu(Menu* menu) override {
        MS* m = getModule<MS>();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Empty every row (modules are placed again)", "", [=]() { for (int r = 0; r < kRows; r++) m->rowId[r] = -1; }));
        menu->addChild(createMenuItem("Clear every section", "", [=]() {
            for (int s = 0; s < kSecs; s++) for (int r = 0; r < kRows; r++) m->cells[s][r] = MS::Cell();
        }));
        menu->addChild(createMenuItem("Clear the chain", "", [=]() { m->chainN = 0; m->chainPos = 0; }));
    }
};

} // namespace

Model* modelSynTHBloKMatrixSong = createModel<SynTHBloKMatrixSong, SynTHBloKMatrixSongWidget>("SynTHBloKMatrixSong");
