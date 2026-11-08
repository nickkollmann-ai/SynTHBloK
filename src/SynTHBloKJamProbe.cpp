// ============================================================================
// SynTHBloK-J  JAM PROBE
//  MIDI monitor + test transmitter for learning a controller's note/CC map
//  (built for the Maschine JAM). Shows every incoming message, keeps per-control
//  statistics, can send test notes/CCs back (to find which values light which
//  pads) and can sweep through all 128 numbers automatically.
//  Right-click: copy map summary to clipboard / save it to a text file.
// ============================================================================
#include "plugin.hpp"
#include <deque>
#include <map>
#include <mutex>
#include <cstdio>
#include <algorithm>

struct ProbeStat { int count = 0; int last = 0; int mn = 127; int mx = 0; int ch = 0; };

struct ProbeInput : midi::Input {
    std::mutex* mtx = NULL;
    std::deque<std::string>* log = NULL;
    std::map<int, ProbeStat>* stats = NULL;
    int* total = NULL;
    std::string* last = NULL;

    void onMessage(const midi::Message& m) override {
        if (!mtx || m.bytes.empty()) return;
        uint8_t st = m.bytes[0];
        char buf[96];
        int type = st & 0xf0, ch = (st & 0x0f) + 1;
        int a = m.bytes.size() > 1 ? m.bytes[1] : 0;
        int b = m.bytes.size() > 2 ? m.bytes[2] : 0;
        int key = 0, val = 0;
        if (type == 0x90 && b == 0) type = 0x80;
        switch (type) {
            case 0x90: snprintf(buf, sizeof buf, "RX N+ ch%-2d #%-3d v%d", ch, a, b); key = (0x90 << 8) | a; val = b; break;
            case 0x80: snprintf(buf, sizeof buf, "RX N- ch%-2d #%-3d", ch, a); key = (0x90 << 8) | a; val = 0; break;
            case 0xB0: snprintf(buf, sizeof buf, "RX CC ch%-2d #%-3d v%d", ch, a, b); key = (0xB0 << 8) | a; val = b; break;
            case 0xA0: snprintf(buf, sizeof buf, "RX PA ch%-2d #%-3d v%d", ch, a, b); key = (0xA0 << 8) | a; val = b; break;
            case 0xD0: snprintf(buf, sizeof buf, "RX CP ch%-2d v%d", ch, a); key = (0xD0 << 8); val = a; break;
            case 0xE0: snprintf(buf, sizeof buf, "RX PB ch%-2d %d", ch, a | (b << 7)); key = (0xE0 << 8); val = b; break;
            case 0xC0: snprintf(buf, sizeof buf, "RX PC ch%-2d #%d", ch, a); key = (0xC0 << 8) | a; val = a; break;
            default:   snprintf(buf, sizeof buf, "RX %02X len%d %02X %02X", st, (int)m.bytes.size(), a, b); key = (0xF0 << 8) | (st & 0x0f); val = a; break;
        }
        std::lock_guard<std::mutex> lk(*mtx);
        ProbeStat& s = (*stats)[key];
        s.count++; s.last = val; s.mn = std::min(s.mn, val); s.mx = std::max(s.mx, val); s.ch = ch;
        (*total)++;
        if (last) *last = std::string(buf + 3);
        log->push_back(buf);
        while (log->size() > 64) log->pop_front();
    }
};

struct SynTHBloKJamProbe : Module {
    enum ParamIds { NUM_PARAM, VAL_PARAM, TYPE_PARAM, SWEEP_PARAM, SEND_PARAM, OFF_PARAM, CLEAR_PARAM, SAVE_PARAM, COPY_PARAM, AUTO_PARAM, SPEED_PARAM, CCTOO_PARAM, LOOP_PARAM, CH_PARAM, LIT_PARAM, NUM_PARAMS };
    enum InputIds { NUM_INPUTS };
    enum OutputIds { NUM_OUTPUTS };
    enum LightIds { NUM_LIGHTS };

    ProbeInput midiIn;
    midi::Output midiOut;
    std::mutex mtx;
    std::deque<std::string> log;
    std::map<int, ProbeStat> stats;
    int total = 0;
    std::string lastMsg;

    dsp::SchmittTrigger sendTrig, offTrig, clearTrig, saveTrig, copyTrig, litTrig;
    struct LitInfo { int ch = 1, val = 0; };
    std::map<int, LitInfo> lit;
    int lastTxKey = -1, lastTxCh = 1, lastTxVal = 0;
    float sweepTimer = 0.f, noteOffTimer = -1.f;
    int pendingOffNum = -1;
    bool doSave = false, copyReq = false;
    bool autoWas = false;
    int autoIdx = 0, autoPhase = 2, autoPrev = -1;   // phase 0 notes, 1 CC, 2 idle/done
    float autoClock = 0.f;
    int lastSavedTotal = -1;
    float autoTimer = 0.f;

    SynTHBloKJamProbe() {
        config(NUM_PARAMS, 0, 0, 0);
        configParam(NUM_PARAM, 0.f, 127.f, 36.f, "Note / CC number");
        configParam(VAL_PARAM, 0.f, 127.f, 127.f, "Velocity / value");
        configSwitch(TYPE_PARAM, 0.f, 1.f, 0.f, "Message type", {"Note", "CC"});
        configSwitch(SWEEP_PARAM, 0.f, 1.f, 0.f, "Sweep all 128 numbers", {"Off", "On"});
        configButton(SEND_PARAM, "Send test message");
        configButton(OFF_PARAM, "All notes off");
        configButton(CLEAR_PARAM, "Clear log and statistics");
        configButton(SAVE_PARAM, "Save map summary to text file");
        configButton(COPY_PARAM, "Copy map summary to clipboard");
        configSwitch(AUTO_PARAM, 0.f, 1.f, 0.f, "Auto probe (sweeps notes, then CCs)", {"Off", "On"});
        configParam(SPEED_PARAM, 0.04f, 0.4f, 0.12f, "Auto probe step time", " s");
        configSwitch(CCTOO_PARAM, 0.f, 1.f, 1.f, "Auto probe also sweeps CCs", {"Notes only", "Notes + CCs"});
        configParam(CH_PARAM, 1.f, 16.f, 1.f, "MIDI channel for test / auto messages");
        paramQuantities[CH_PARAM]->snapEnabled = true;
        configButton(LIT_PARAM, "Mark: the last sent number made something light up");
        configSwitch(LOOP_PARAM, 0.f, 1.f, 0.f, "Auto probe loops", {"Once", "Loop"});
        paramQuantities[NUM_PARAM]->snapEnabled = true;
        paramQuantities[VAL_PARAM]->snapEnabled = true;
        midiIn.mtx = &mtx; midiIn.log = &log; midiIn.stats = &stats; midiIn.total = &total; midiIn.last = &lastMsg;
    }

    void addLog(const std::string& s) {
        std::lock_guard<std::mutex> lk(mtx);
        log.push_back(s);
        while (log.size() > 64) log.pop_front();
    }

    void tx(int status, int a, int b) {
        int ch = (int)params[CH_PARAM].getValue() - 1;
        if (ch < 0) ch = 0; if (ch > 15) ch = 15;
        status = (status & 0xf0) | ch;
        if (((status & 0xf0) == 0x90 && b > 0) || ((status & 0xf0) == 0xB0 && b > 0)) {
            lastTxKey = ((status & 0xf0) << 8) | a; lastTxCh = ch + 1; lastTxVal = b;
        }
        midi::Message m;
        m.bytes = {(uint8_t)status, (uint8_t)a, (uint8_t)b};
        midiOut.sendMessage(m);
    }

    void sendTest(bool on) {
        int num = (int)params[NUM_PARAM].getValue();
        int val = (int)params[VAL_PARAM].getValue();
        char buf[64];
        if (params[TYPE_PARAM].getValue() < 0.5f) {
            tx(on ? 0x90 : 0x80, num, on ? val : 0);
            snprintf(buf, sizeof buf, "TX N%c  #%-3d v%d", on ? '+' : '-', num, on ? val : 0);
        } else {
            tx(0xB0, num, val);
            snprintf(buf, sizeof buf, "TX CC  #%-3d v%d", num, val);
        }
        addLog(buf);
    }

    std::string summary() {
        std::lock_guard<std::mutex> lk(mtx);
        std::string out = "JAM PROBE map summary  (messages: " + std::to_string(total) + ")\n";
        char buf[128];
        if (!lit.empty()) {
            out += "\nLIT (something lit up when this was sent):\n";
            for (auto& kv : lit) {
                int type = kv.first >> 8, num = kv.first & 0xff;
                snprintf(buf, sizeof buf, "%-5s #%-3d ch%-2d vel/val=%d\n", type == 0x90 ? "NOTE" : "CC", num, kv.second.ch, kv.second.val);
                out += buf;
            }
            out += "\nRECEIVED:\n";
        }
        for (auto& kv : stats) {
            int type = kv.first >> 8, num = kv.first & 0xff;
            const char* t = type == 0x90 ? "NOTE" : type == 0xB0 ? "CC" : type == 0xA0 ? "POLYAT" : type == 0xD0 ? "CHANAT" : type == 0xE0 ? "PITCHBEND" : type == 0xC0 ? "PROGCHG" : "OTHER";
            snprintf(buf, sizeof buf, "%-9s #%-3d ch%-2d count=%-4d last=%-3d min=%-3d max=%d\n", t, num, kv.second.ch, kv.second.count, kv.second.last, kv.second.mn, kv.second.mx);
            out += buf;
        }
        return out;
    }

    void saveFile(bool quiet) {
        std::string s = summary();
        // only into Rack's user folder: a plugin's own folder is replaced on updates and may be read-only
        const std::string path = asset::user("SynTHBloK_JamProbe.txt");
        bool ok = false;
        FILE* f = fopen(path.c_str(), "wb");
        if (f) { ok = fwrite(s.data(), 1, s.size(), f) == s.size(); fclose(f); }
        if (!quiet) addLog(ok ? "SAVED map file" : "SAVE FAILED");
        lastSavedTotal = total + (int)lit.size() * 100003;
    }

    void autoRelease() {
        if (autoPrev >= 0) {
            if (autoPhase == 0) tx(0x80, autoPrev, 0); else tx(0xB0, autoPrev, 0);
            autoPrev = -1;
        }
    }

    void autoStep() {
        int val = (int)params[VAL_PARAM].getValue();
        char buf[64];
        autoRelease();
        if (autoPhase == 0 && autoIdx >= 128) {
            if (params[CCTOO_PARAM].getValue() > 0.5f) { autoPhase = 1; autoIdx = 0; }
            else autoIdx = 999;
        }
        if (autoPhase == 1 && autoIdx >= 128) autoIdx = 999;
        if (autoIdx >= 999) {
            if (params[LOOP_PARAM].getValue() > 0.5f) { autoPhase = 0; autoIdx = 0; }
            else { autoPhase = 2; params[AUTO_PARAM].setValue(0.f); addLog("AUTO PROBE DONE"); return; }
        }
        if (autoPhase == 0) {
            tx(0x90, autoIdx, val);
            snprintf(buf, sizeof buf, "TX N+  #%-3d v%d", autoIdx, val);
        } else {
            tx(0xB0, autoIdx, val);
            snprintf(buf, sizeof buf, "TX CC  #%-3d v%d", autoIdx, val);
        }
        addLog(buf);
        autoPrev = autoIdx;
        params[NUM_PARAM].setValue((float)autoIdx);
        autoIdx++;
    }

    void clearAll() {
        std::lock_guard<std::mutex> lk(mtx);
        log.clear(); stats.clear(); total = 0; lastMsg.clear();
    }

    void process(const ProcessArgs& args) override {
        if (sendTrig.process(params[SEND_PARAM].getValue())) {
            sendTest(true);
            noteOffTimer = 0.25f;
            pendingOffNum = (int)params[NUM_PARAM].getValue();
        }
        if (noteOffTimer >= 0.f) {
            noteOffTimer -= args.sampleTime;
            if (noteOffTimer < 0.f && params[TYPE_PARAM].getValue() < 0.5f && pendingOffNum >= 0) {
                tx(0x80, pendingOffNum, 0);
                pendingOffNum = -1;
            }
        }
        if (offTrig.process(params[OFF_PARAM].getValue())) {
            for (int n = 0; n < 128; n++) tx(0x80, n, 0);
            addLog("TX ALL NOTES OFF");
        }
        if (clearTrig.process(params[CLEAR_PARAM].getValue())) { clearAll(); std::lock_guard<std::mutex> lk(mtx); lit.clear(); }
        if (litTrig.process(params[LIT_PARAM].getValue()) && lastTxKey >= 0) {
            char lb[64];
            { std::lock_guard<std::mutex> lk(mtx); LitInfo li; li.ch = lastTxCh; li.val = lastTxVal; lit[lastTxKey] = li; }
            snprintf(lb, sizeof lb, "LIT %s #%d ch%d", (lastTxKey >> 8) == 0x90 ? "N" : "CC", lastTxKey & 0xff, lastTxCh);
            addLog(lb);
        }
        if (saveTrig.process(params[SAVE_PARAM].getValue())) doSave = true;
        if (copyTrig.process(params[COPY_PARAM].getValue())) copyReq = true;
        if (doSave) { doSave = false; saveFile(false); }
        autoTimer += args.sampleTime;
        if (autoTimer >= 3.f) { autoTimer = 0.f; if (total + (int)lit.size() * 100003 != lastSavedTotal && (total > 0 || !lit.empty())) saveFile(true); }

        bool autoOn = params[AUTO_PARAM].getValue() > 0.5f;
        if (autoOn && !autoWas) { autoIdx = 0; autoPhase = 0; autoClock = 1e9f; addLog("AUTO PROBE START"); }
        if (!autoOn && autoWas) { autoRelease(); if (autoPhase != 2) { autoPhase = 2; addLog("AUTO PROBE STOP"); } }
        autoWas = autoOn;
        if (autoOn) {
            autoClock += args.sampleTime;
            if (autoClock >= params[SPEED_PARAM].getValue()) { autoClock = 0.f; autoStep(); }
        }

        if (params[SWEEP_PARAM].getValue() > 0.5f && !autoOn) {
            sweepTimer += args.sampleTime;
            if (sweepTimer >= 0.15f) {
                sweepTimer = 0.f;
                int num = (int)params[NUM_PARAM].getValue();
                if (params[TYPE_PARAM].getValue() < 0.5f && pendingOffNum >= 0) tx(0x80, pendingOffNum, 0);
                sendTest(true);
                pendingOffNum = num;
                params[NUM_PARAM].setValue((float)((num + 1) % 128));
            }
        }
    }

    json_t* dataToJson() override {
        json_t* r = json_object();
        json_object_set_new(r, "in", midiIn.toJson());
        json_object_set_new(r, "out", midiOut.toJson());
        return r;
    }
    void dataFromJson(json_t* r) override {
        if (json_t* j = json_object_get(r, "in")) midiIn.fromJson(j);
        if (json_t* j = json_object_get(r, "out")) midiOut.fromJson(j);
    }
};

struct ProbeLogDisplay : TransparentWidget {
    SynTHBloKJamProbe* module = NULL;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgFillColor(args.vg, nvgRGB(0x0a, 0x0a, 0x0a));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(0x55, 0x33, 0x00));
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        std::vector<std::string> lines;
        int total = 0, keys = 0;
        if (module) {
            std::lock_guard<std::mutex> lk(module->mtx);
            total = module->total; keys = (int)module->stats.size();
            int n = (int)module->log.size(), maxL = std::max(1, (int)((box.size.y - 18.f) / 10.5f));
            for (int i = std::max(0, n - maxL); i < n; i++) lines.push_back(module->log[i]);
        } else {
            lines = {"RX N+ ch1  #36  v127", "RX N-  ch1  #36", "RX CC ch1  #20  v64", "TX N+  #12  v127"};
        }
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 8.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        char hdr[64];
        snprintf(hdr, sizeof hdr, "msgs %d   controls %d", total, keys);
        nvgFillColor(args.vg, nvgRGB(0xff, 0x7a, 0x00));
        nvgText(args.vg, 4.f, 3.f, hdr, NULL);
        float y = 15.f;
        for (auto& s : lines) {
            bool isTx = s.compare(0, 2, "TX") == 0;
            nvgFillColor(args.vg, isTx ? nvgRGB(0xff, 0x3a, 0x00) : nvgRGB(0xe8, 0xc0, 0x40));
            nvgText(args.vg, 4.f, y, s.c_str(), NULL);
            y += 10.5f;
        }
    }
};

template <bool LATCH>
struct TinyBtn : app::ParamWidget {
    TinyBtn() { box.size = mm2px(Vec(4.4f, 4.4f)); }
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
            if (ParamQuantity* pq = getParamQuantity()) {
                if (LATCH) pq->setValue(pq->getValue() > 0.5f ? 0.f : 1.f);
                else pq->setValue(1.f);
            }
            e.consume(this);
            return;
        }
        ParamWidget::onButton(e);
    }
    void onDragStart(const DragStartEvent& e) override {}
    void onDragEnd(const DragEndEvent& e) override {
        if (!LATCH && e.button == GLFW_MOUSE_BUTTON_LEFT)
            if (ParamQuantity* pq = getParamQuantity()) pq->setValue(0.f);
    }
};

struct ProbePortDisplay : TransparentWidget {
    midi::Port* port = NULL;
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        std::string drv = "Driver", dev = "(no device)";
        if (port) {
            if (midi::Driver* d = port->getDriver()) drv = d->getName();
            if (port->getDeviceId() >= 0) dev = port->getDeviceName(port->getDeviceId());
        }
        nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 8.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(0xff, 0x7a, 0x00));
        nvgText(args.vg, 5.f, box.size.y * 0.28f, drv.c_str(), NULL);
        nvgFillColor(args.vg, nvgRGB(0xff, 0xd7, 0x14));
        nvgText(args.vg, 5.f, box.size.y * 0.74f, dev.c_str(), NULL);
        nvgResetScissor(args.vg);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && port) {
            ui::Menu* menu = createMenu();
            menu->addChild(createMenuLabel("MIDI port"));
            appendMidiMenu(menu, port);
            e.consume(this);
        }
    }
};

struct ProbeLastDisplay : TransparentWidget {
    SynTHBloKJamProbe* module = NULL;
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        std::string t = "N+ ch1  #36  v127";
        if (module) {
            std::lock_guard<std::mutex> lk(module->mtx);
            t = module->lastMsg.empty() ? "waiting for MIDI..." : module->lastMsg;
        }
        nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 12.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(0xff, 0xd7, 0x14));
        nvgText(args.vg, 5.f, box.size.y * 0.5f, t.c_str(), NULL);
        nvgResetScissor(args.vg);
    }
};

struct ProbeAutoDisplay : TransparentWidget {
    SynTHBloKJamProbe* module = NULL;
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        bool on = module && module->params[SynTHBloKJamProbe::AUTO_PARAM].getValue() > 0.5f;
        int ph = module ? module->autoPhase : 0, idx = module ? module->autoIdx : 0;
        float frac = 0.f;
        char t[64];
        if (!on) snprintf(t, sizeof t, "AUTO  OFF");
        else {
            snprintf(t, sizeof t, "%s  %03d / 128", ph == 1 ? "CC  " : "NOTE", std::min(idx, 128));
            frac = (float)std::min(idx, 128) / 128.f;
        }
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 4.f, box.size.y - 5.f, (box.size.x - 8.f) * frac, 2.f);
        nvgFillColor(args.vg, nvgRGB(0xff, 0x7a, 0x00));
        nvgFill(args.vg);
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 8.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? nvgRGB(0xff, 0x7a, 0x00) : nvgRGB(0x90, 0x90, 0x90));
        nvgText(args.vg, 5.f, box.size.y * 0.5f - 2.f, t, NULL);
    }
};

struct SynTHBloKJamProbeWidget : ModuleWidget {
    SynTHBloKJamProbeWidget(SynTHBloKJamProbe* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SynTHBloKJamProbe.svg")));
        using namespace rack::componentlibrary;

        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(96.52f - 3.5f, 3.5f))));

        ProbePortDisplay* di = createWidget<ProbePortDisplay>(mm2px(Vec(4.f, 14.f)));
        di->box.size = mm2px(Vec(43.52f, 8.f)); di->port = module ? &module->midiIn : NULL;
        addChild(di);
        ProbePortDisplay* dout = createWidget<ProbePortDisplay>(mm2px(Vec(4.f, 29.f)));
        dout->box.size = mm2px(Vec(43.52f, 8.f)); dout->port = module ? &module->midiOut : NULL;
        addChild(dout);

        ProbeAutoDisplay* ad = createWidget<ProbeAutoDisplay>(mm2px(Vec(4.f, 44.2f)));
        ad->box.size = mm2px(Vec(43.52f, 8.f));
        ad->module = module;
        addChild(ad);

        ProbeLogDisplay* ld = createWidget<ProbeLogDisplay>(mm2px(Vec(53.f, 14.f)));
        ld->box.size = mm2px(Vec(39.5f, 108.f));
        ld->module = module;
        addChild(ld);

        ProbeLastDisplay* ls = createWidget<ProbeLastDisplay>(mm2px(Vec(4.f, 108.f)));
        ls->box.size = mm2px(Vec(43.52f, 12.f));
        ls->module = module;
        addChild(ls);

        const float KX[4] = {9.5f, 20.5f, 31.5f, 42.5f};
        const float BX[5] = {6.5f, 16.f, 25.5f, 35.f, 44.5f};
        addParam(createParamCentered<TinyBtn<true>>(mm2px(Vec(KX[0], 60.5f)), module, SynTHBloKJamProbe::AUTO_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(KX[1], 60.5f)), module, SynTHBloKJamProbe::SPEED_PARAM));
        addParam(createParamCentered<TinyBtn<true>>(mm2px(Vec(KX[2], 60.5f)), module, SynTHBloKJamProbe::CCTOO_PARAM));
        addParam(createParamCentered<TinyBtn<false>>(mm2px(Vec(KX[3], 60.5f)), module, SynTHBloKJamProbe::LIT_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(KX[0], 80.f)), module, SynTHBloKJamProbe::NUM_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(KX[1], 80.f)), module, SynTHBloKJamProbe::VAL_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(KX[2], 80.f)), module, SynTHBloKJamProbe::CH_PARAM));
        addParam(createParamCentered<TinyBtn<true>>(mm2px(Vec(KX[3], 80.f)), module, SynTHBloKJamProbe::TYPE_PARAM));
        addParam(createParamCentered<TinyBtn<false>>(mm2px(Vec(BX[0], 93.5f)), module, SynTHBloKJamProbe::SEND_PARAM));
        addParam(createParamCentered<TinyBtn<false>>(mm2px(Vec(BX[1], 93.5f)), module, SynTHBloKJamProbe::OFF_PARAM));
        addParam(createParamCentered<TinyBtn<false>>(mm2px(Vec(BX[2], 93.5f)), module, SynTHBloKJamProbe::CLEAR_PARAM));
        addParam(createParamCentered<TinyBtn<false>>(mm2px(Vec(BX[3], 93.5f)), module, SynTHBloKJamProbe::SAVE_PARAM));
        addParam(createParamCentered<TinyBtn<false>>(mm2px(Vec(BX[4], 93.5f)), module, SynTHBloKJamProbe::COPY_PARAM));
    }

    void step() override {
        ModuleWidget::step();
        SynTHBloKJamProbe* m = dynamic_cast<SynTHBloKJamProbe*>(module);
        if (m && m->copyReq) { m->copyReq = false; glfwSetClipboardString(APP->window->win, m->summary().c_str()); }
    }

    void appendContextMenu(Menu* menu) override {
        SynTHBloKJamProbe* m = dynamic_cast<SynTHBloKJamProbe*>(module);
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Copy map summary to clipboard", "", [=]() {
            glfwSetClipboardString(APP->window->win, m->summary().c_str());
        }));
        menu->addChild(createMenuItem("Save map summary to Rack user folder", "", [=]() { m->doSave = true; }));
    }
};

Model* modelSynTHBloKJamProbe = createModel<SynTHBloKJamProbe, SynTHBloKJamProbeWidget>("JamProbe");
