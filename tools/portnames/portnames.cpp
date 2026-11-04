// Prints the jack names of every module in one installed Rack plugin, as JSON, for the workbench's PATCH MAP
// (tools/patch_feed.py runs it once per plugin and caches the result in tools/port_names.json).
//   portnames.exe <plugin slug>     ("Core" for Rack's own modules)
//   -> {"<model slug>": {"in": ["Clock", ...], "out": [...]}, ...}
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
        try {
            engine::Module* mod = m->createModule();
            json_t* e = json_object();
            json_object_set_new(e, "in", names(mod->inputInfos));
            json_object_set_new(e, "out", names(mod->outputInfos));
            json_object_set_new(out, m->slug.c_str(), e);
        } catch (...) {}
    }
    char* s = json_dumps(out, JSON_COMPACT);
    fputs(s, stdout);
    fflush(stdout);
    _exit(0);   // skip plugin destructors: some expect a full Rack around them
}
