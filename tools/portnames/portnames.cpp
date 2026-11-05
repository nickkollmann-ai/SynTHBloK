// Prints the jack names of every module in one installed Rack plugin, as JSON, for the workbench's PATCH MAP
// (tools/patch_feed.py runs it once per plugin and caches the result in tools/port_names.json).
//   portnames.exe <plugin slug>     ("Core" for Rack's own modules)
//   -> {"<model slug>": {"in": ["Clock", ...], "out": [...]}, ...}
//   portnames.exe <plugin slug> --pos <model slug>
//   -> the same for that model plus "inPos" / "outPos" ([x, y] mm per jack, null if not on the panel) and "wmm"
// A module whose constructor fails is left out; the map then shows its jacks by number.
#include <rack.hpp>
#include <cstdio>
extern "C" {
__declspec(dllimport) void* __stdcall LoadLibraryA(const char*);
__declspec(dllimport) void* __stdcall GetProcAddress(void*, const char*);
__declspec(dllimport) int __stdcall SetDllDirectoryA(const char*);
}
using namespace rack;
namespace rack { namespace core { void init(plugin::Plugin* p); } }

static json_t* names(const std::vector<engine::PortInfo*>& infos) {
    json_t* a = json_array();
    for (engine::PortInfo* pi : infos) json_array_append_new(a, json_string(pi ? pi->getName().c_str() : ""));
    return a;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: portnames <plugin slug>\n"); return 1; }
    std::string slug = argv[1];
    // portnames <plugin> --pos <model>: also the jack positions of that one model (builds its panel, so it runs
    // one model per call: a panel that needs more of Rack than this gives it only takes its own call down)
    bool pos = argc >= 4 && std::string(argv[2]) == "--pos";
    std::string only = pos ? argv[3] : "";
    asset::systemDir = "C:\\Program Files\\VCV\\Rack2Pro";
    random::init();
    contextSet(new Context);
    APP->engine = (engine::Engine*)calloc(1, sizeof(engine::Engine));
    APP->engine->internal = (engine::Engine::Internal*)calloc(1, 1 << 16);
    plugin::Plugin* p = new plugin::Plugin;
    p->slug = slug;
    if (slug == "Core") core::init(p);
    else {
        std::string dir = std::string(getenv("LOCALAPPDATA")) + "\\Rack2\\plugins-win-x64\\" + slug;
        SetDllDirectoryA(dir.c_str());
        void* h = LoadLibraryA((dir + "\\plugin.dll").c_str());
        if (!h) { fprintf(stderr, "cannot load %s\n", dir.c_str()); return 1; }
        auto init = (void (*)(plugin::Plugin*))GetProcAddress(h, "init");
        if (!init) { fprintf(stderr, "no init in %s\n", slug.c_str()); return 1; }
        p->path = dir;
        init(p);
    }
    json_t* out = json_object();
    for (plugin::Model* m : p->models) {
        if (pos && m->slug != only) continue;
        try {
            engine::Module* mod = m->createModule();
            json_t* e = json_object();
            json_object_set_new(e, "in", names(mod->inputInfos));
            json_object_set_new(e, "out", names(mod->outputInfos));
            if (pos) {
                // build the panel the way Rack does and read where each jack sits, in mm from the panel's top left
                app::ModuleWidget* mw = m->createModuleWidget(mod);
                json_t* ip = json_array(); json_t* op = json_array();
                for (int i = 0; i < (int)mod->inputs.size(); i++) json_array_append_new(ip, json_null());
                for (int i = 0; i < (int)mod->outputs.size(); i++) json_array_append_new(op, json_null());
                auto put = [&](app::PortWidget* pw, json_t* arr) {
                    math::Vec c = pw->box.getCenter();
                    json_array_set_new(arr, pw->portId, json_pack("[f,f]", c.x * 5.08 / 15.0, c.y * 5.08 / 15.0));
                };
                for (app::PortWidget* pw : mw->getInputs()) put(pw, ip);
                for (app::PortWidget* pw : mw->getOutputs()) put(pw, op);
                json_object_set_new(e, "inPos", ip);
                json_object_set_new(e, "outPos", op);
                json_object_set_new(e, "wmm", json_real(mw->box.size.x * 5.08 / 15.0));
            }
            json_object_set_new(out, m->slug.c_str(), e);
        } catch (...) {}
    }
    char* s = json_dumps(out, JSON_COMPACT);
    fputs(s, stdout);
    fflush(stdout);
    _exit(0);   // skip plugin destructors: some expect a full Rack around them
}
