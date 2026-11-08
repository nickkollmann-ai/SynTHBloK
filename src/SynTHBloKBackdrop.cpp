// SynTHBloK-B BACKDROP: dresses the whole rack. A layer behind every module (above Rack's own rails) draws
//  - a background: a colour, one of the SynTHBloK looks (amber grid, OLED glow, diamond plate, deep space),
//    and / or your own image (tiled across the rack, or filling / fitting the screen)
//  - a CASE around each block of modules: wooden skiffs, anodized and brushed metal, powder coats, a road
//    case, carbon, rack ears. Each has its frame, rails, side depth and shadow, drawn (no image files), and
//    more can be added in Rack's user folder (SynTHBloK/cases.json)
//  - the patch title above the first case
// One BACKDROP dresses the rack; a second one stays quiet (its light turns red). Everything is in the menu.
#include "plugin.hpp"
#include "OledUi.hpp"
#include <osdialog.h>
#include <patch.hpp>
#include <cmath>
#include <cstring>
#include <algorithm>
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"   // the tables leave trailing fields at 0 on purpose
#include <map>
#include <ctime>
#include <algorithm>
#include <functional>

namespace {

// ---------------------------------------------------------------------------------------------------------
// THE CASE LIBRARY. A material decides how the frame is drawn; the colours make it this case.
// ---------------------------------------------------------------------------------------------------------
enum Material { M_NONE, M_WOOD, M_ANODIZED, M_BRUSHED, M_POWDER, M_TOLEX, M_CARBON, M_EARS, M_ACRYLIC, M_PRINTED, M_PLY, M_NUM };
// the end shapes: square, slanted cheeks (a case on legs or a tiered cabinet), rounded corners, plastic end caps
enum Ends { E_SQUARE, E_SLANT, E_ROUND, E_CAPS, E_STEP, E_CURVE, E_TRUSS, E_ENDS };
// extras on the top lip
enum { X_NONE = 0, X_1U = 1, X_HANDLE = 2, X_LATCHES = 4, X_LEGS = 8, X_OPEN = 16 };   // X_OPEN: just two side panels and the rails
struct CaseStyle {
    std::string name;
    int material;
    NVGcolor base, accent, rail;
    float side;    // frame thickness at the ends, px
    int ends;      // Ends
    int extras;    // X_ flags
    int sideIdx;   // side panels in another material (kSideMat index; 0: the case's own)
    int sideFinish;   // species woods: 0 OILED, 1 SATIN, 2 GLOSS (aggregate init leaves it 0)
};
static NVGcolor rgb(int r, int g, int b) { return nvgRGB(r, g, b); }
static std::vector<CaseStyle> builtInCases() {
    return {
        {"NO CASE", M_NONE, rgb(0, 0, 0), rgb(0, 0, 0), rgb(0, 0, 0), 0.f, E_SQUARE, X_NONE, 0},
        {"WALNUT SKIFF", M_WOOD, rgb(92, 58, 34), rgb(60, 36, 20), rgb(46, 46, 48), 16.f, E_SQUARE, X_NONE, 0},
        {"OAK SKIFF", M_WOOD, rgb(168, 124, 78), rgb(128, 90, 52), rgb(52, 52, 54), 16.f, E_SQUARE, X_NONE, 0},
        {"EBONY SKIFF", M_WOOD, rgb(44, 30, 24), rgb(26, 18, 14), rgb(40, 40, 42), 16.f, E_SQUARE, X_NONE, 0},
        {"BLACK ANODIZED", M_ANODIZED, rgb(22, 22, 24), rgb(40, 40, 44), rgb(34, 34, 36), 12.f, E_SQUARE, X_NONE, 0},
        {"BRUSHED ALUMINUM", M_BRUSHED, rgb(160, 165, 170), rgb(120, 125, 130), rgb(110, 112, 116), 12.f, E_SQUARE, X_NONE, 0},
        {"POWDER COAT AMBER", M_POWDER, rgb(176, 88, 16), rgb(120, 56, 8), rgb(36, 30, 26), 12.f, E_SQUARE, X_NONE, 0},
        {"POWDER COAT RED", M_POWDER, rgb(150, 28, 20), rgb(100, 16, 12), rgb(36, 30, 30), 12.f, E_SQUARE, X_NONE, 0},
        {"POWDER COAT TEAL", M_POWDER, rgb(24, 92, 108), rgb(14, 60, 72), rgb(30, 36, 38), 12.f, E_SQUARE, X_NONE, 0},
        {"POWDER COAT WHITE", M_POWDER, rgb(214, 210, 200), rgb(170, 166, 158), rgb(70, 70, 72), 12.f, E_SQUARE, X_NONE, 0},
        {"ROAD CASE", M_TOLEX, rgb(26, 26, 28), rgb(150, 152, 156), rgb(36, 36, 38), 18.f, E_SQUARE, X_NONE, 0},
        {"CARBON FIBER", M_CARBON, rgb(24, 24, 26), rgb(44, 44, 48), rgb(30, 30, 32), 12.f, E_SQUARE, X_NONE, 0},
        {"RACK EARS", M_EARS, rgb(150, 154, 158), rgb(100, 104, 108), rgb(50, 50, 52), 22.f, E_SQUARE, X_NONE, 0},
        // our own takes on the classic case shapes (our names; no maker's look copied badge for badge)
        {"STALKER 6U", M_POWDER, rgb(24, 24, 26), rgb(14, 14, 15), rgb(40, 40, 42), 18.f, E_SLANT, X_LEGS, 0},
        {"STALKER 6U SILVER", M_BRUSHED, rgb(170, 172, 176), rgb(120, 122, 126), rgb(60, 60, 62), 18.f, E_SLANT, X_LEGS, 0},
        {"PERFORMER 7U", M_ANODIZED, rgb(20, 20, 22), rgb(44, 44, 48), rgb(36, 36, 38), 14.f, E_ROUND, X_1U, 0},
        {"PERFORMER 7U SILVER", M_BRUSHED, rgb(176, 180, 184), rgb(130, 134, 138), rgb(70, 70, 72), 14.f, E_ROUND, X_1U, 0},
        {"BUS STEEL 7U", M_POWDER, rgb(30, 30, 32), rgb(18, 18, 20), rgb(44, 44, 46), 12.f, E_SQUARE, X_1U, 0},
        {"FLATBOAT", M_POWDER, rgb(34, 34, 36), rgb(22, 22, 24), rgb(44, 44, 46), 8.f, E_ROUND, X_NONE, 0},
        {"FLATBOAT WHITE", M_POWDER, rgb(226, 224, 218), rgb(180, 178, 172), rgb(80, 80, 82), 8.f, E_ROUND, X_NONE, 0},
        {"LAB EXTRUSION", M_BRUSHED, rgb(186, 188, 190), rgb(24, 24, 26), rgb(120, 122, 124), 16.f, E_CAPS, X_NONE, 0},
        {"LAB EXTRUSION GREY", M_BRUSHED, rgb(120, 124, 128), rgb(20, 20, 22), rgb(90, 92, 96), 16.f, E_CAPS, X_NONE, 0},
        {"SUITCASE 9U", M_TOLEX, rgb(40, 40, 42), rgb(160, 162, 166), rgb(40, 40, 42), 18.f, E_SQUARE, X_LATCHES, 0},
        {"SUITCASE 9U BLUE", M_TOLEX, rgb(22, 34, 60), rgb(160, 162, 166), rgb(36, 40, 50), 18.f, E_SQUARE, X_LATCHES, 0},
        {"TIERED WALNUT", M_WOOD, rgb(98, 62, 36), rgb(62, 38, 22), rgb(30, 30, 32), 20.f, E_SLANT, X_NONE, 0},
        {"TIERED ASH", M_WOOD, rgb(196, 170, 130), rgb(150, 124, 88), rgb(40, 40, 42), 20.f, E_SLANT, X_NONE, 0},
        {"WEST COAST BOAT", M_WOOD, rgb(150, 96, 52), rgb(104, 62, 30), rgb(26, 26, 28), 22.f, E_ROUND, X_NONE, 0},
        {"SCAFFOLD", M_ANODIZED, rgb(18, 18, 20), rgb(36, 36, 40), rgb(30, 30, 32), 12.f, E_CAPS, X_NONE, 0},
        {"POD RED", M_ANODIZED, rgb(150, 26, 26), rgb(100, 14, 14), rgb(40, 30, 30), 10.f, E_ROUND, X_NONE, 0},
        {"POD BLUE", M_ANODIZED, rgb(28, 70, 150), rgb(16, 44, 104), rgb(30, 34, 44), 10.f, E_ROUND, X_NONE, 0},
        {"POD GOLD", M_ANODIZED, rgb(176, 140, 60), rgb(130, 100, 36), rgb(50, 44, 34), 10.f, E_ROUND, X_NONE, 0},
        {"POD PURPLE", M_ANODIZED, rgb(96, 40, 140), rgb(64, 24, 100), rgb(36, 30, 44), 10.f, E_ROUND, X_NONE, 0},
        {"BRUTE DOCK", M_ANODIZED, rgb(22, 22, 24), rgb(150, 154, 160), rgb(34, 34, 36), 20.f, E_CAPS, X_LEGS, 0},
        {"CLEAR ACRYLIC", M_ACRYLIC, rgb(200, 220, 230), rgb(255, 255, 255), rgb(70, 72, 76), 12.f, E_ROUND, X_NONE, 0},
        {"SMOKED ACRYLIC", M_ACRYLIC, rgb(40, 40, 46), rgb(200, 200, 210), rgb(50, 50, 54), 12.f, E_ROUND, X_NONE, 0},
        {"PRINTED PLA ORANGE", M_PRINTED, rgb(230, 110, 20), rgb(170, 70, 8), rgb(36, 32, 30), 14.f, E_ROUND, X_NONE, 0},
        {"PRINTED PLA BLACK", M_PRINTED, rgb(30, 30, 32), rgb(16, 16, 18), rgb(40, 40, 42), 14.f, E_ROUND, X_NONE, 0},
        {"PRINTED PETG TEAL", M_PRINTED, rgb(20, 120, 130), rgb(10, 80, 90), rgb(30, 38, 40), 14.f, E_ROUND, X_NONE, 0},
        // studio consoles in hand-finished wood: thick cheeks, the rails edge to edge, black steel hardware
        {"BIRCH PLY STUDIO", M_PLY, rgb(196, 162, 114), rgb(150, 118, 78), rgb(26, 26, 28), 24.f, E_SLANT, X_NONE, 0},
        {"BLACK PLY STUDIO", M_PLY, rgb(38, 33, 30), rgb(22, 19, 17), rgb(24, 24, 26), 24.f, E_SLANT, X_NONE, 0},
        {"CHERRY STUDIO", M_WOOD, rgb(136, 60, 34), rgb(92, 38, 20), rgb(24, 24, 26), 24.f, E_SLANT, X_NONE, 0},
        {"WALNUT CONSOLE", M_WOOD, rgb(84, 54, 34), rgb(52, 32, 20), rgb(24, 24, 26), 28.f, E_SQUARE, X_NONE, 0},
        // open frames: two side panels, the rails bare between them, the background showing through
        {"OAK OPEN FRAME", M_WOOD, rgb(170, 126, 80), rgb(128, 90, 52), rgb(150, 154, 158), 22.f, E_SQUARE, X_OPEN, 0},
        {"STEPPED OAK", M_WOOD, rgb(176, 132, 84), rgb(130, 92, 54), rgb(150, 154, 158), 22.f, E_STEP, X_OPEN, 0},
        {"STEPPED WALNUT", M_WOOD, rgb(92, 58, 36), rgb(58, 36, 22), rgb(150, 154, 158), 22.f, E_STEP, X_OPEN, 0},
        {"CURVED CONSOLE", M_WOOD, rgb(196, 170, 130), rgb(150, 124, 88), rgb(150, 154, 158), 22.f, E_CURVE, X_OPEN, 0},
        {"LATTICE PRINT", M_PRINTED, rgb(34, 30, 34), rgb(20, 18, 20), rgb(150, 154, 158), 26.f, E_TRUSS, X_OPEN, 0},
        {"LATTICE PRINT ORANGE", M_PRINTED, rgb(230, 110, 20), rgb(170, 70, 8), rgb(150, 154, 158), 26.f, E_TRUSS, X_OPEN, 0},
        // PSYCHEDELIC (Nov 2026): neon acrylics, UV prints and blacklight finishes to go with the psychedelic ART
        {"NEON ACRYLIC PINK", M_ACRYLIC, rgb(255, 47, 168), rgb(255, 200, 236), rgb(54, 40, 52), 12.f, E_ROUND, X_NONE, 0},
        {"NEON ACRYLIC CYAN", M_ACRYLIC, rgb(25, 227, 255), rgb(210, 250, 255), rgb(36, 50, 56), 12.f, E_ROUND, X_NONE, 0},
        {"SMOKED VIOLET ACRYLIC", M_ACRYLIC, rgb(64, 30, 96), rgb(200, 170, 240), rgb(44, 36, 54), 12.f, E_ROUND, X_NONE, 0},
        {"UV PRINT LIME", M_PRINTED, rgb(182, 255, 42), rgb(120, 190, 20), rgb(36, 40, 30), 14.f, E_ROUND, X_NONE, 0},
        {"UV PRINT MAGENTA", M_PRINTED, rgb(230, 36, 150), rgb(160, 20, 100), rgb(40, 30, 38), 14.f, E_ROUND, X_NONE, 0},
        {"BLACKLIGHT POD", M_ANODIZED, rgb(70, 24, 130), rgb(40, 12, 90), rgb(34, 28, 44), 10.f, E_ROUND, X_NONE, 0},
        {"IRIDESCENT ANODIZED", M_ANODIZED, rgb(36, 96, 128), rgb(120, 40, 150), rgb(34, 34, 42), 12.f, E_SQUARE, X_NONE, 0},
        {"GRAFFITI ROAD CASE", M_TOLEX, rgb(20, 18, 26), rgb(255, 47, 168), rgb(34, 30, 38), 18.f, E_SQUARE, X_LATCHES, 0},
        {"FORGE STEEL", M_POWDER, rgb(150, 52, 14), rgb(90, 26, 8), rgb(40, 30, 26), 18.f, E_SLANT, X_LEGS, 0},
        {"EBONY GOLD CONSOLE", M_WOOD, rgb(44, 30, 24), rgb(26, 18, 14), rgb(190, 152, 64), 28.f, E_CURVE, X_NONE, 0},
        {"MYCELIUM LATTICE", M_PRINTED, rgb(18, 96, 104), rgb(10, 58, 64), rgb(150, 154, 158), 26.f, E_TRUSS, X_OPEN, 0},
        {"ACID LATTICE", M_PRINTED, rgb(182, 255, 42), rgb(110, 170, 20), rgb(150, 154, 158), 26.f, E_TRUSS, X_OPEN, 0},
        {"STEPPED NEON", M_PRINTED, rgb(255, 47, 168), rgb(170, 20, 110), rgb(150, 154, 158), 22.f, E_STEP, X_OPEN, 0},
        {"CURVED VIOLET", M_PRINTED, rgb(138, 60, 255), rgb(90, 30, 180), rgb(150, 154, 158), 22.f, E_CURVE, X_OPEN, 0},
    };
}
// SIDE PANELS: put any side panels on any case (shape, material, width, open frame); AUTO keeps the case's own
struct SideMat { const char* name; int material; NVGcolor base; const char* tex; };   // tex: a full-colour species wood (res/cases/wood_*.png)
static const SideMat kSideMat[] = {
    {"AUTO", M_NONE, {}}, {"WALNUT", M_WOOD, {}}, {"OAK", M_WOOD, {}}, {"CHERRY", M_WOOD, {}}, {"EBONY", M_WOOD, {}}, {"ASH", M_WOOD, {}},
    {"BIRCH PLY", M_PLY, {}}, {"BLACK PLY", M_PLY, {}}, {"BLACK STEEL", M_POWDER, {}}, {"BRUSHED", M_BRUSHED, {}}, {"CARBON", M_CARBON, {}},
    {"ORANGE PRINT", M_PRINTED, {}}, {"WHITE", M_POWDER, {}},
    {"NEON PINK", M_ACRYLIC, {}}, {"NEON CYAN", M_ACRYLIC, {}}, {"ACID LIME", M_PRINTED, {}}, {"UV VIOLET", M_PRINTED, {}}, {"BLACKLIGHT", M_ANODIZED, {}},
    {"FORGE RUST", M_POWDER, {}}, {"GOLD", M_BRUSHED, {}},
    // species woods (tools/make_wood_textures.py), each with its own grain and figure; new ones go at the END
    {"BLACK WALNUT", M_WOOD, {}, "wood_black_walnut"}, {"CLARO WALNUT", M_WOOD, {}, "wood_claro_walnut"},
    {"QUARTERSAWN OAK", M_WOOD, {}, "wood_white_oak_qs"}, {"AMERICAN CHERRY", M_WOOD, {}, "wood_cherry"},
    {"HARD MAPLE", M_WOOD, {}, "wood_hard_maple"}, {"BALTIC BIRCH PLY", M_PLY, {}, "wood_birch_ply"},
    {"CURLY MAPLE", M_WOOD, {}, "wood_curly_maple"}, {"QUILTED MAPLE", M_WOOD, {}, "wood_quilted_maple"},
    {"BIRDSEYE MAPLE", M_WOOD, {}, "wood_birdseye_maple"}, {"SPALTED MAPLE", M_WOOD, {}, "wood_spalted_maple"},
    {"WATERFALL BUBINGA", M_WOOD, {}, "wood_waterfall_bubinga"}, {"RIBBON SAPELE", M_WOOD, {}, "wood_ribbon_sapele"},
    {"ZEBRAWOOD", M_WOOD, {}, "wood_zebrawood"}, {"PADAUK", M_WOOD, {}, "wood_padauk"}, {"PURPLEHEART", M_WOOD, {}, "wood_purpleheart"},
    {"COCOBOLO", M_WOOD, {}, "wood_cocobolo"}, {"WENGE", M_WOOD, {}, "wood_wenge"}, {"MACASSAR EBONY", M_WOOD, {}, "wood_macassar_ebony"},
    {"ZIRICOTE", M_WOOD, {}, "wood_ziricote"}, {"BOCOTE", M_WOOD, {}, "wood_bocote"}, {"KOA", M_WOOD, {}, "wood_koa"},
    {"LEOPARDWOOD", M_WOOD, {}, "wood_leopardwood"}, {"AMBOYNA BURL", M_WOOD, {}, "wood_amboyna_burl"},
    {"SHOU SUGI BAN", M_WOOD, {}, "wood_shou_sugi_ban"},
};
static const char* const kSideFinish[] = {"OILED", "SATIN", "GLOSS"};
enum { SW_BLACK_WALNUT = 20, SW_CLARO, SW_QS_OAK, SW_CHERRY, SW_HARD_MAPLE, SW_BIRCH_PLY, SW_CURLY_MAPLE, SW_QUILTED_MAPLE, SW_BIRDSEYE,
       SW_SPALTED, SW_BUBINGA, SW_SAPELE, SW_ZEBRAWOOD, SW_PADAUK, SW_PURPLEHEART, SW_COCOBOLO, SW_WENGE, SW_MACASSAR, SW_ZIRICOTE,
       SW_BOCOTE, SW_KOA, SW_LEOPARD, SW_AMBOYNA, SW_SHOU_SUGI_BAN };
enum { F_OILED, F_SATIN, F_GLOSS };
static const int kSideMatN = (int) (sizeof(kSideMat) / sizeof(kSideMat[0]));
static NVGcolor sideColor(int i) {
    static const int c[][3] = {{0, 0, 0}, {92, 58, 34}, {168, 124, 78}, {136, 60, 34}, {44, 30, 24}, {196, 170, 130}, {196, 162, 114}, {38, 33, 30},
                               {26, 26, 28}, {170, 172, 176}, {24, 24, 26}, {230, 110, 20}, {220, 218, 210},
                               {255, 47, 168}, {25, 227, 255}, {182, 255, 42}, {138, 60, 255}, {70, 24, 130}, {150, 52, 14}, {196, 160, 70},
                               // species woods: each texture's average (the edge, the depth, the joints)
                               {81, 54, 36}, {90, 62, 35}, {189, 152, 100}, {158, 77, 40}, {231, 208, 166}, {225, 199, 155}, {221, 205, 165}, {219, 204, 166},
                               {231, 209, 166}, {179, 161, 128}, {140, 65, 43}, {124, 61, 32}, {179, 156, 115}, {200, 57, 23}, {112, 39, 101}, {155, 71, 24},
                               {55, 40, 29}, {41, 28, 16}, {56, 43, 30}, {137, 103, 52}, {160, 106, 48}, {163, 96, 55}, {176, 97, 40}, {37, 32, 27}};
    static_assert(sizeof(c) / sizeof(c[0]) == sizeof(kSideMat) / sizeof(kSideMat[0]), "one colour per side material");
    i = clamp(i, 0, kSideMatN - 1);
    return nvgRGB(c[i][0], c[i][1], c[i][2]);
}
static const char* const kSideShape[] = {"AUTO", "SQUARE", "SLANTED", "ROUNDED", "STEPPED", "CURVED", "LATTICE", "END CAPS"};
static const int kSideShapeEnds[] = {-1, E_SQUARE, E_SLANT, E_ROUND, E_STEP, E_CURVE, E_TRUSS, E_CAPS};
static const int kSideShapeN = 8;
static const char* const kSideWidth[] = {"AUTO", "SLIM", "NORMAL", "WIDE", "XL"};
static const float kSideWidthK[] = {1.f, 0.65f, 1.f, 1.45f, 1.9f};
static const char* const kOpenName[] = {"AUTO", "ON", "OFF"};

// the library: the built-in cases, then any in Rack's user folder, SynTHBloK/cases.json:
//   [{"name": "MY CASE", "material": "wood", "base": "#5c3a22", "accent": "#3c2414", "rail": "#2e2e30", "side": 16,
//     "ends": "slant", "extras": ["1u", "handle", "latches", "legs"]}]
//   materials: wood anodized brushed powder tolex carbon ears acrylic printed; ends: square slant round caps
static NVGcolor hexColor(const char* s, NVGcolor fallback) {
    if (!s || s[0] != '#' || std::strlen(s) < 7) return fallback;
    unsigned v = (unsigned) std::strtoul(s + 1, NULL, 16);
    return nvgRGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
}
static std::vector<CaseStyle>& caseLibrary() {
    static std::vector<CaseStyle> lib;
    static bool loaded = false;
    if (loaded) return lib;
    loaded = true;
    lib = builtInCases();
    json_error_t err;
    json_t* j = json_load_file(asset::user("SynTHBloK/cases.json").c_str(), 0, &err);
    if (!j) return lib;
    static const char* const mats[M_NUM] = {"none", "wood", "anodized", "brushed", "powder", "tolex", "carbon", "ears", "acrylic", "printed", "ply"};
    static const char* const ends[E_ENDS] = {"square", "slant", "round", "caps", "step", "curve", "truss"};
    size_t i; json_t* e;
    json_array_foreach(j, i, e) {
        CaseStyle c = lib[1];
        if (json_t* v = json_object_get(e, "name")) c.name = json_string_value(v);
        if (json_t* v = json_object_get(e, "material")) {
            std::string m = json_string_value(v);
            for (int k = 0; k < M_NUM; k++) if (m == mats[k]) c.material = k;
        }
        c.ends = E_SQUARE; c.extras = X_NONE;
        if (json_t* v = json_object_get(e, "ends")) {
            std::string m = json_is_string(v) ? json_string_value(v) : "";
            for (int k = 0; k < E_ENDS; k++) if (m == ends[k]) c.ends = k;
        }
        if (json_t* v = json_object_get(e, "extras")) {
            size_t xi; json_t* xe;
            json_array_foreach(v, xi, xe) {
                std::string x = json_is_string(xe) ? json_string_value(xe) : "";
                if (x == "1u") c.extras |= X_1U;
                if (x == "handle") c.extras |= X_HANDLE;
                if (x == "latches") c.extras |= X_LATCHES;
                if (x == "legs") c.extras |= X_LEGS;
                if (x == "open") c.extras |= X_OPEN;
            }
        }
        c.base = hexColor(json_string_value(json_object_get(e, "base")), c.base);
        c.accent = hexColor(json_string_value(json_object_get(e, "accent")), c.accent);
        c.rail = hexColor(json_string_value(json_object_get(e, "rail")), c.rail);
        if (json_t* v = json_object_get(e, "side")) c.side = (float) json_number_value(v);
        lib.push_back(c);
    }
    json_decref(j);
    return lib;
}

// the SynTHBloK looks for the background
static const int kLookN = 89;
static const int kArtN = 69;
enum { LOOK_GRADIENT = 18, LOOK_CASE_GRADIENT = 19, LOOK_ART = 20 };
// ART: detailed pictures made once (tools/make_backdrop_art.py) and shipped in res/backdrops/: one textured
// rectangle per frame, so they cost no more than a plain colour
static const char* const kArtFile[kArtN] = {"nebula", "aurora", "topographic", "synthwave", "flowfield", "crystal", "oscilloscope", "marble", "circuit", "spectrum",
    // the PSYCHEDELIC set (Nov 2026), drawn from Nick's own abstract artwork
    "wildstyle", "liquid", "kaleidoscope", "biolum", "forge", "escher", "biomorph", "stickerbomb", "spheres",
    // round 6 (Nov 2026): 25 pieces drawn from Nick's 50 approved Midjourney picks
    "motion", "mirror_swirl", "iris", "tunnel", "prism", "bronze_mandala", "flower_of_life", "gold_filament", "violet_neural", "cosmic_swirl",
    "dream_clouds", "ink_bloom", "glass_world", "copper_maze", "tessellation", "violet_ridges", "snow_globes", "mandelbrot", "julia",
    "coral_cells", "lotus", "neon_marble", "melt", "third_eye", "rainbow_surf",
    // round 6b: 25 more from the same picks
    "hypercube", "lava", "op_art", "nebula_eye", "aurora_mandala", "crystal_cave", "ferns", "lightning", "dragon", "stained_glass", "chrome", "metatron", "penrose", "glow_forest", "bubbles", "kaleido_shards", "ripples", "attractor", "spirograph", "plasma", "vortex_streams", "jellyfish", "neon_lattice", "sunburst", "nautilus"};
static const char* const kLookName[kLookN] = {"Plain colour", "Amber grid", "OLED glow", "Diamond plate", "Deep space", "Amber rails (Rack's look, warmer)",
    "Carbon weave", "Circuit traces", "CRT scanlines", "Hex mesh", "Blueprint", "Pegboard", "Acoustic foam", "Brushed steel",
    "Sunset fade", "Halftone dots", "Brick wall", "Wood slats", "Gradient (the colour, to black)", "Case gradient (follows the case)",
    "Art: Nebula", "Art: Aurora", "Art: Topographic", "Art: Synthwave", "Art: Flow field", "Art: Crystal", "Art: Oscilloscope", "Art: Gold marble", "Art: Circuit board", "Art: Spectrum",
    "Psychedelic: Neon wildstyle", "Psychedelic: Liquid illusion", "Psychedelic: Sacred kaleidoscope", "Psychedelic: Bioluminescent network", "Psychedelic: Machine elf forge",
    "Psychedelic: Escher blocks", "Psychedelic: Grey biomorph", "Psychedelic: Sticker bomb", "Psychedelic: Glass spheres",
    "Dream: Motion paint", "Dream: Mirror swirl", "Dream: Iris vortex", "Dream: Illusion tunnel", "Dream: Prism shards", "Dream: Bronze mandala",
    "Dream: Flower of life", "Dream: Gold filament", "Dream: Violet neural", "Dream: Cosmic swirl", "Dream: Dream clouds", "Dream: Ink bloom",
    "Dream: Glass world", "Dream: Copper maze", "Dream: Tessellation", "Dream: Violet ridges", "Dream: Snow globes", "Dream: Mandelbrot",
    "Dream: Julia bloom", "Dream: Coral cells", "Dream: Lotus kaleido", "Dream: Neon marble", "Dream: Melt", "Dream: Third eye", "Dream: Rainbow surf",
    "Dream: Hypercube", "Dream: Lava lamp", "Dream: Op art", "Dream: Nebula eye", "Dream: Aurora mandala", "Dream: Crystal cave", "Dream: Neon ferns", "Dream: Lightning", "Dream: Dragon curve", "Dream: Stained glass", "Dream: Chrome drip", "Dream: Metatron", "Dream: Penrose", "Dream: Glowing forest", "Dream: Soap bubbles", "Dream: Kaleido shards", "Dream: Ripples", "Dream: Strange attractor", "Dream: Spirograph", "Dream: Plasma", "Dream: Vortex streams", "Dream: Jellyfish", "Dream: Neon lattice", "Dream: Sunburst", "Dream: Nautilus"};
static const char* const kFitName[4] = {"Tiled across the rack", "Fill the screen (snaps to fit)", "Fit the screen (whole image shows)", "Centred on the screen"};
static const int kColorN = 28;
static const char* const kColorName[kColorN] = {"Black", "Charcoal", "Deep amber", "Ember", "Midnight teal", "Plum", "Forest", "Steel",
    "Navy", "Indigo", "Oxblood", "Wine", "Rust", "Mocha", "Olive", "Moss", "Slate blue", "Deep cyan", "Deep magenta", "Gunmetal",
    "Warm grey", "Sand", "SynTHBloK orange", "Cream", "Blacklight violet", "Deep neon pink", "Deep acid green", "Deep UV blue"};
static const NVGcolor kColor[kColorN] = {rgb(6, 6, 6), rgb(22, 22, 24), rgb(40, 22, 6), rgb(46, 12, 6), rgb(6, 30, 36), rgb(34, 14, 38), rgb(10, 30, 16), rgb(36, 40, 46),
    rgb(8, 14, 40), rgb(22, 12, 52), rgb(52, 8, 12), rgb(44, 10, 26), rgb(70, 28, 10), rgb(44, 30, 22), rgb(42, 44, 14), rgb(26, 44, 22), rgb(34, 44, 70), rgb(0, 54, 64), rgb(64, 8, 54), rgb(30, 34, 38),
    rgb(70, 66, 62), rgb(120, 100, 70), rgb(200, 96, 10), rgb(214, 204, 182), rgb(30, 8, 50), rgb(60, 6, 42), rgb(18, 42, 6), rgb(8, 12, 64)};

} // namespace

struct SynTHBloKBackdrop : Module {
    // settings (saved with the patch)
    bool showBg = true;
    int look = LOOK_GRADIENT;   // loads as the SynTHBloK black gradient (theme 0), no grid
    int colorIdx = 0;
    std::string imagePath;
    int fit = 1;   // fill the screen: the image always snaps to cover the whole background
    float imgScale = 1.f, imgDim = 0.f;
    float imgW = 1.f, imgH = 1.f, imgX = 0.f, imgY = 0.f;   // fine-tuning: width / height stretch, X / Y offset (of the image's size)
    int caseIdx = 4;   // BLACK ANODIZED
    int themeIdx = 0;  // the theme last picked (-1: changed by hand since)
    float depthMm = 65.f;   // the case depth, mm, as real builds come
    int sideShape = 0, sideMat = 0, sideWidth = 0, openMode = 0;   // side panels (0: the case's own)
    int sideFinish = 0;   // species woods: OILED / SATIN / GLOSS
    bool shadow = true, rails = true;
    bool showTitle = false;   // the name above the case: optional, off unless turned on (NAME page or menu)
    std::string title;   // empty: the patch's file name
    std::string presetPath;   // the preset last loaded or saved (shown on the THEME page)

    // the info strip's level meter: patch your mix into L / R
    enum InputIds { IN_L, IN_R, NUM_INPUTS };
    float peak[2] = {0.f, 0.f}, hold[2] = {0.f, 0.f}, level[2] = {0.f, 0.f};
    int infoMode = 0;   // the info strip on the case's top: 0 auto (cases with a 1U row), 1 on, 2 off
    SynTHBloKBackdrop() {
        config(0, NUM_INPUTS, 0, 0);
        configInput(IN_L, "Meter L (the info strip's level meter)");
        configInput(IN_R, "Meter R (normalled to L)");
    }
    void process(const ProcessArgs& args) override {
        if (!inputs[IN_L].isConnected() && !inputs[IN_R].isConnected()) { peak[0] = peak[1] = level[0] = level[1] = 0.f; return; }
        float l = inputs[IN_L].getVoltageSum() / 5.f, r = inputs[IN_R].isConnected() ? inputs[IN_R].getVoltageSum() / 5.f : l;
        float in[2] = {std::fabs(l), std::fabs(r)};
        float fall = std::exp(-args.sampleTime / 0.25f), slow = 1.f - std::exp(-args.sampleTime / 0.3f), hfall = std::exp(-args.sampleTime / 1.5f);
        for (int c = 0; c < 2; c++) {
            peak[c] = std::max(in[c], peak[c] * fall);
            level[c] += (in[c] * in[c] - level[c]) * slow;   // mean square
            hold[c] = std::max(peak[c], hold[c] * hfall);
        }
    }

    json_t* dataToJson() override {
        json_t* r = json_object();
        json_object_set_new(r, "showBg", json_boolean(showBg));
        json_object_set_new(r, "look", json_integer(look));
        json_object_set_new(r, "theme", json_integer(themeIdx));
        json_object_set_new(r, "color", json_integer(colorIdx));
        json_object_set_new(r, "image", json_string(imagePath.c_str()));
        json_object_set_new(r, "fit", json_integer(fit));
        json_object_set_new(r, "imgScale", json_real(imgScale));
        json_object_set_new(r, "imgDim", json_real(imgDim));
        json_object_set_new(r, "imgW", json_real(imgW));
        json_object_set_new(r, "imgH", json_real(imgH));
        json_object_set_new(r, "imgX", json_real(imgX));
        json_object_set_new(r, "imgY", json_real(imgY));
        json_object_set_new(r, "case", json_string(caseLibrary()[clamp(caseIdx, 0, (int) caseLibrary().size() - 1)].name.c_str()));
        json_object_set_new(r, "depthMm", json_real(depthMm));
        json_object_set_new(r, "sideShape", json_integer(sideShape));
        json_object_set_new(r, "infoMode", json_integer(infoMode));
        json_object_set_new(r, "sideMat", json_integer(sideMat));
        json_object_set_new(r, "sideWidth", json_integer(sideWidth));
        json_object_set_new(r, "sideFinish", json_integer(sideFinish));
        json_object_set_new(r, "openMode", json_integer(openMode));
        json_object_set_new(r, "shadow", json_boolean(shadow));
        json_object_set_new(r, "rails", json_boolean(rails));
        json_object_set_new(r, "showTitle", json_boolean(showTitle));
        json_object_set_new(r, "title", json_string(title.c_str()));
        json_object_set_new(r, "preset", json_string(presetPath.c_str()));
        return r;
    }
    void dataFromJson(json_t* r) override {
        if (json_t* v = json_object_get(r, "showBg")) showBg = json_boolean_value(v);
        if (json_t* v = json_object_get(r, "preset")) presetPath = json_string_value(v);
        if (json_t* v = json_object_get(r, "look")) look = clamp((int) json_integer_value(v), 0, kLookN - 1);
        themeIdx = -1;
        if (json_t* v = json_object_get(r, "theme")) themeIdx = (int) json_integer_value(v);
        if (json_t* v = json_object_get(r, "color")) colorIdx = clamp((int) json_integer_value(v), 0, kColorN - 1);
        if (json_t* v = json_object_get(r, "image")) imagePath = json_string_value(v);
        if (json_t* v = json_object_get(r, "fit")) fit = clamp((int) json_integer_value(v), 0, 3);
        if (json_t* v = json_object_get(r, "imgScale")) imgScale = clamp((float) json_number_value(v), 0.1f, 8.f);
        if (json_t* v = json_object_get(r, "imgDim")) imgDim = clamp((float) json_number_value(v), 0.f, 0.9f);
        if (json_t* v = json_object_get(r, "imgW")) imgW = clamp((float) json_number_value(v), 0.25f, 4.f);
        if (json_t* v = json_object_get(r, "imgH")) imgH = clamp((float) json_number_value(v), 0.25f, 4.f);
        if (json_t* v = json_object_get(r, "imgX")) imgX = clamp((float) json_number_value(v), -1.f, 1.f);
        if (json_t* v = json_object_get(r, "imgY")) imgY = clamp((float) json_number_value(v), -1.f, 1.f);
        if (json_t* v = json_object_get(r, "case")) {   // by name, so a library that grows keeps the choice
            std::string n = json_string_value(v);
            for (size_t i = 0; i < caseLibrary().size(); i++) if (caseLibrary()[i].name == n) caseIdx = (int) i;
        }
        if (json_t* v = json_object_get(r, "depth")) depthMm = clamp((float) json_number_value(v), 0.f, 1.f) * 120.f;   // older patches: a share
        if (json_t* v = json_object_get(r, "depthMm")) depthMm = clamp((float) json_number_value(v), 0.f, 200.f);
        if (json_t* v = json_object_get(r, "infoMode")) infoMode = clamp((int) json_integer_value(v), 0, 2);
        if (json_t* v = json_object_get(r, "sideShape")) sideShape = clamp((int) json_integer_value(v), 0, kSideShapeN - 1);
        if (json_t* v = json_object_get(r, "sideMat")) sideMat = clamp((int) json_integer_value(v), 0, kSideMatN - 1);
        if (json_t* v = json_object_get(r, "sideWidth")) sideWidth = clamp((int) json_integer_value(v), 0, 4);
        if (json_t* v = json_object_get(r, "sideFinish")) sideFinish = clamp((int) json_integer_value(v), 0, 2);
        if (json_t* v = json_object_get(r, "openMode")) openMode = clamp((int) json_integer_value(v), 0, 2);
        if (json_t* v = json_object_get(r, "shadow")) shadow = json_boolean_value(v);
        if (json_t* v = json_object_get(r, "rails")) rails = json_boolean_value(v);
        if (json_t* v = json_object_get(r, "showTitle")) showTitle = json_boolean_value(v);
        if (json_t* v = json_object_get(r, "title")) title = json_string_value(v);
    }
};

namespace {

// ---------------------------------------------------------------------------------------------------------
// THE LAYER: one widget in the rack, just above Rack's rails and below the modules and cables.
// ---------------------------------------------------------------------------------------------------------
struct BackdropLayer : widget::TransparentWidget {
    SynTHBloKBackdrop* module = NULL;
    int image = -1;
    std::string imageLoaded;
    NVGcontext* imageVg = NULL;
    int imgW = 0, imgH = 0;

    void freeImage() {
        if (image >= 0 && imageVg) nvgDeleteImage(imageVg, image);
        image = -1; imageLoaded.clear(); imageVg = NULL;
    }
    ~BackdropLayer() {
        freeImage();
        if (art >= 0 && artVg) nvgDeleteImage(artVg, art);
        if (texVg) for (auto& kv : texImg) if (kv.second >= 0) nvgDeleteImage(texVg, kv.second);
    }

    void step() override {
        if (parent) box = math::Rect(Vec(0, 0), parent->box.size);
        Widget::step();
    }

    // ---- the background ----
    // GRADIENT looks: like the SynTHBloK panels, a lift at the top fading to near black, a soft glow in the middle
    void drawGradient(NVGcontext* vg, math::Rect v, NVGcolor c) {
        float x0 = v.pos.x, y0 = v.pos.y;
        NVGcolor top = nvgRGBf(std::min(1.f, c.r * 1.25f + 0.07f), std::min(1.f, c.g * 1.25f + 0.07f), std::min(1.f, c.b * 1.25f + 0.075f));
        NVGcolor bot = nvgRGBf(c.r * 0.22f + 0.008f, c.g * 0.22f + 0.008f, c.b * 0.22f + 0.01f);
        nvgBeginPath(vg);
        nvgRect(vg, x0, y0, v.size.x, v.size.y);
        nvgFillPaint(vg, nvgLinearGradient(vg, x0, y0, x0, y0 + v.size.y, top, bot));
        nvgFill(vg);
        float cx = x0 + v.size.x * 0.5f, cy = y0 + v.size.y * 0.35f, r = std::max(v.size.x, v.size.y) * 0.6f;
        nvgBeginPath(vg);
        nvgRect(vg, x0, y0, v.size.x, v.size.y);
        nvgFillPaint(vg, nvgRadialGradient(vg, cx, cy, r * 0.1f, r, nvgRGBAf(1.f, 1.f, 1.f, 0.035f), nvgRGBAf(0.f, 0.f, 0.f, 0.f)));
        nvgFill(vg);
    }
    // ART: the picture fills the visible screen (stays put while you scroll), loaded once and kept
    int art = -1, artFor = -1; NVGcontext* artVg = NULL; int artW = 0, artH = 0;
    void drawArt(NVGcontext* vg, math::Rect v, int k) {
        if (art < 0 || artFor != k || artVg != vg) {
            if (art >= 0 && artVg) nvgDeleteImage(artVg, art);
            art = nvgCreateImage(vg, asset::plugin(pluginInstance, string::f("res/backdrops/%s.jpg", kArtFile[k])).c_str(), 0);
            artFor = k; artVg = vg; artW = artH = 0;
            if (art >= 0) nvgImageSize(vg, art, &artW, &artH);
        }
        if (art < 0 || artW <= 0 || artH <= 0) { drawGradient(vg, v, nvgRGB(6, 6, 6)); return; }
        float sc = std::max(v.size.x / artW, v.size.y / artH) * 1.004f, w = artW * sc, h = artH * sc;
        float ox = v.pos.x + (v.size.x - w) * 0.5f, oy = v.pos.y + (v.size.y - h) * 0.5f;
        nvgBeginPath(vg);
        nvgRect(vg, v.pos.x, v.pos.y, v.size.x, v.size.y);
        nvgFillPaint(vg, nvgImagePattern(vg, ox, oy, w, h, 0.f, art, 1.f));
        nvgFill(vg);
    }
    void drawLook(NVGcontext* vg, math::Rect v, int look, NVGcolor base, NVGcolor caseCol) {
        if (look >= LOOK_ART) { drawArt(vg, v, clamp(look - LOOK_ART, 0, kArtN - 1)); return; }
        if (look == LOOK_GRADIENT) { drawGradient(vg, v, base); return; }
        if (look == LOOK_CASE_GRADIENT) { drawGradient(vg, v, nvgLerpRGBA(caseCol, nvgRGB(0, 0, 0), 0.45f)); return; }
        nvgBeginPath(vg);
        nvgRect(vg, v.pos.x, v.pos.y, v.size.x, v.size.y);
        nvgFillColor(vg, base);
        nvgFill(vg);
        float x0 = v.pos.x, y0 = v.pos.y, x1 = v.pos.x + v.size.x, y1 = v.pos.y + v.size.y;
        if (look == 1) {   // AMBER GRID: a fine grid on the HP / row lines, a brighter one each 8 HP
            float gx = RACK_GRID_WIDTH, gy = RACK_GRID_HEIGHT / 8.f;
            nvgBeginPath(vg);
            for (float x = std::floor(x0 / gx) * gx; x < x1; x += gx) { nvgMoveTo(vg, x, y0); nvgLineTo(vg, x, y1); }
            for (float y = std::floor(y0 / gy) * gy; y < y1; y += gy) { nvgMoveTo(vg, x0, y); nvgLineTo(vg, x1, y); }
            nvgStrokeColor(vg, nvgRGBA(255, 150, 40, 14));
            nvgStrokeWidth(vg, 1.f);
            nvgStroke(vg);
            nvgBeginPath(vg);
            for (float x = std::floor(x0 / (gx * 8)) * gx * 8; x < x1; x += gx * 8) { nvgMoveTo(vg, x, y0); nvgLineTo(vg, x, y1); }
            for (float y = std::floor(y0 / RACK_GRID_HEIGHT) * RACK_GRID_HEIGHT; y < y1; y += RACK_GRID_HEIGHT) { nvgMoveTo(vg, x0, y); nvgLineTo(vg, x1, y); }
            nvgStrokeColor(vg, nvgRGBA(255, 150, 40, 34));
            nvgStroke(vg);
        }
        else if (look == 2) {   // OLED GLOW: warm pools of light over the dark
            float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, r = std::max(v.size.x, v.size.y) * 0.7f;
            NVGpaint p = nvgRadialGradient(vg, cx, cy, r * 0.05f, r, nvgRGBA(255, 120, 20, 46), nvgRGBA(0, 0, 0, 0));
            nvgBeginPath(vg);
            nvgRect(vg, x0, y0, v.size.x, v.size.y);
            nvgFillPaint(vg, p);
            nvgFill(vg);
            NVGpaint q = nvgLinearGradient(vg, x0, y0, x0, y1, nvgRGBA(255, 90, 10, 18), nvgRGBA(0, 0, 0, 0));
            nvgFillPaint(vg, q);
            nvgFill(vg);
        }
        else if (look == 3) {   // DIAMOND PLATE: raised diamonds in rows
            float s = 22.f;
            nvgBeginPath(vg);
            for (float y = std::floor(y0 / s) * s; y < y1 + s; y += s)
                for (float x = std::floor(x0 / s) * s; x < x1 + s; x += s) {
                    bool alt = ((int) std::floor(y / s) + (int) std::floor(x / s)) & 1;
                    float cx = x + s * 0.5f, cy = y + s * 0.5f, a = s * 0.36f, b = s * 0.09f;
                    float dx = alt ? a : b, dy = alt ? b : a;   // alternate the diamonds' direction
                    nvgMoveTo(vg, cx - dx, cy + dy); nvgLineTo(vg, cx - dx + b, cy + dy - b);
                    nvgLineTo(vg, cx + dx, cy - dy); nvgLineTo(vg, cx + dx - b, cy - dy + b);
                    nvgClosePath(vg);
                }
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 16));
            nvgFill(vg);
        }
        else if (look == 4) {   // DEEP SPACE: a dark gradient with a scatter of stars
            NVGpaint p = nvgLinearGradient(vg, x0, y0, x1, y1, nvgRGBA(20, 14, 40, 255), nvgRGBA(4, 18, 26, 255));
            nvgBeginPath(vg);
            nvgRect(vg, x0, y0, v.size.x, v.size.y);
            nvgFillPaint(vg, p);
            nvgFill(vg);
            float cell = 60.f;
            for (float y = std::floor(y0 / cell) * cell; y < y1; y += cell)
                for (float x = std::floor(x0 / cell) * cell; x < x1; x += cell) {
                    unsigned h = (unsigned) ((int) (x / cell) * 73856093) ^ (unsigned) ((int) (y / cell) * 19349663);
                    float sx = x + (h % 997) / 997.f * cell, sy = y + ((h / 997) % 991) / 991.f * cell;
                    float r = 0.5f + (h % 7) * 0.18f;
                    nvgBeginPath(vg);
                    nvgCircle(vg, sx, sy, r);
                    nvgFillColor(vg, nvgRGBA(255, 230, 200, 60 + (h % 5) * 30));
                    nvgFill(vg);
                }
        }
        else if (look == 5) {   // AMBER RAILS: Rack's rails, in the SynTHBloK colours
            for (float y = std::floor(y0 / RACK_GRID_HEIGHT) * RACK_GRID_HEIGHT; y < y1; y += RACK_GRID_HEIGHT) {
                drawRail(vg, x0, x1, y, nvgRGB(58, 40, 22));
                drawRail(vg, x0, x1, y + RACK_GRID_HEIGHT - 15.f, nvgRGB(58, 40, 22));
            }
        }
        else if (look == 6) {   // CARBON WEAVE: the woven check, tinted by the colour
            float c = 8.f;
            nvgBeginPath(vg);
            for (float y = std::floor(y0 / c) * c; y < y1; y += c)
                for (float x = std::floor(x0 / c) * c; x < x1; x += c)
                    if ((((int) std::floor(x / c)) + ((int) std::floor(y / c))) & 1) nvgRect(vg, x, y, c, c);
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 12));
            nvgFill(vg);
        }
        else if (look == 7) {   // CIRCUIT TRACES: right-angle traces with pads, from a hash per cell
            float c = 48.f;
            nvgBeginPath(vg);
            for (float y = std::floor(y0 / c) * c; y < y1; y += c)
                for (float x = std::floor(x0 / c) * c; x < x1; x += c) {
                    unsigned h = (unsigned) ((int) (x / c) * 73856093) ^ (unsigned) ((int) (y / c) * 19349663);
                    float ax = x + (h % 4) * 12.f + 6.f, ay = y + ((h >> 3) % 4) * 12.f + 6.f;
                    nvgMoveTo(vg, ax, ay);
                    if (h & 64) { nvgLineTo(vg, ax + c * 0.5f, ay); nvgLineTo(vg, ax + c * 0.5f + 10.f, ay + 10.f); nvgLineTo(vg, ax + c * 0.5f + 10.f, ay + c); }
                    else { nvgLineTo(vg, ax, ay + c * 0.5f); nvgLineTo(vg, ax + 10.f, ay + c * 0.5f + 10.f); nvgLineTo(vg, ax + c, ay + c * 0.5f + 10.f); }
                }
            nvgStrokeColor(vg, nvgRGBA(255, 150, 40, 30));
            nvgStrokeWidth(vg, 1.5f);
            nvgStroke(vg);
            nvgBeginPath(vg);
            for (float y = std::floor(y0 / c) * c; y < y1; y += c)
                for (float x = std::floor(x0 / c) * c; x < x1; x += c) {
                    unsigned h = (unsigned) ((int) (x / c) * 73856093) ^ (unsigned) ((int) (y / c) * 19349663);
                    nvgCircle(vg, x + (h % 4) * 12.f + 6.f, y + ((h >> 3) % 4) * 12.f + 6.f, 2.5f);
                }
            nvgFillColor(vg, nvgRGBA(255, 170, 60, 50));
            nvgFill(vg);
        }
        else if (look == 8) {   // CRT SCANLINES: dark lines every 3 px, a soft vignette
            nvgBeginPath(vg);
            for (float y = std::floor(y0 / 3.f) * 3.f; y < y1; y += 3.f) nvgRect(vg, x0, y, v.size.x, 1.f);
            nvgFillColor(vg, nvgRGBA(0, 0, 0, 60));
            nvgFill(vg);
            float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, r = std::max(v.size.x, v.size.y) * 0.75f;
            nvgBeginPath(vg);
            nvgRect(vg, x0, y0, v.size.x, v.size.y);
            nvgFillPaint(vg, nvgRadialGradient(vg, cx, cy, r * 0.4f, r, nvgRGBA(0, 0, 0, 0), nvgRGBA(0, 0, 0, 120)));
            nvgFill(vg);
        }
        else if (look == 9) {   // HEX MESH: a speaker-grille honeycomb
            float r = 9.f, hw = r * 1.732f, vh = r * 1.5f;
            nvgBeginPath(vg);
            for (float y = std::floor(y0 / vh) * vh - vh; y < y1 + vh; y += vh) {
                float off = ((int) std::floor(y / vh)) & 1 ? hw * 0.5f : 0.f;
                for (float x = std::floor(x0 / hw) * hw - hw + off; x < x1 + hw; x += hw)
                    for (int k = 0; k < 6; k++) {
                        float a = (float) M_PI / 3.f * k + (float) M_PI / 6.f;
                        float px = x + std::cos(a) * (r - 1.5f), py = y + std::sin(a) * (r - 1.5f);
                        if (k == 0) nvgMoveTo(vg, px, py); else nvgLineTo(vg, px, py);
                        if (k == 5) nvgClosePath(vg);
                    }
            }
            nvgFillColor(vg, nvgRGBA(0, 0, 0, 90));
            nvgFill(vg);
        }
        else if (look == 10) {   // BLUEPRINT: white drafting grid on the colour (navy or slate blue suit it best)
            float g = 20.f;
            nvgBeginPath(vg);
            for (float x = std::floor(x0 / g) * g; x < x1; x += g) { nvgMoveTo(vg, x, y0); nvgLineTo(vg, x, y1); }
            for (float y = std::floor(y0 / g) * g; y < y1; y += g) { nvgMoveTo(vg, x0, y); nvgLineTo(vg, x1, y); }
            nvgStrokeColor(vg, nvgRGBA(200, 225, 255, 18));
            nvgStrokeWidth(vg, 1.f);
            nvgStroke(vg);
            nvgBeginPath(vg);
            for (float x = std::floor(x0 / (g * 5)) * g * 5; x < x1; x += g * 5) { nvgMoveTo(vg, x, y0); nvgLineTo(vg, x, y1); }
            for (float y = std::floor(y0 / (g * 5)) * g * 5; y < y1; y += g * 5) { nvgMoveTo(vg, x0, y); nvgLineTo(vg, x1, y); }
            nvgStrokeColor(vg, nvgRGBA(200, 225, 255, 44));
            nvgStroke(vg);
        }
        else if (look == 11) {   // PEGBOARD: rows of holes
            float g = 25.f;
            nvgBeginPath(vg);
            for (float y = std::floor(y0 / g) * g; y < y1 + g; y += g)
                for (float x = std::floor(x0 / g) * g; x < x1 + g; x += g) nvgCircle(vg, x, y, 3.5f);
            nvgFillColor(vg, nvgRGBA(0, 0, 0, 150));
            nvgFill(vg);
            nvgBeginPath(vg);
            for (float y = std::floor(y0 / g) * g; y < y1 + g; y += g)
                for (float x = std::floor(x0 / g) * g; x < x1 + g; x += g) nvgCircle(vg, x + 0.8f, y + 0.8f, 3.5f);
            nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 14));
            nvgStrokeWidth(vg, 0.8f);
            nvgStroke(vg);
        }
        else if (look == 12) {   // ACOUSTIC FOAM: egg-crate wedges, light from the top left
            float c = 30.f;
            for (float y = std::floor(y0 / c) * c; y < y1; y += c)
                for (float x = std::floor(x0 / c) * c; x < x1; x += c) {
                    bool alt = (((int) std::floor(x / c)) + ((int) std::floor(y / c))) & 1;
                    nvgBeginPath(vg);
                    nvgRect(vg, x, y, c, c);
                    nvgFillPaint(vg, alt ? nvgLinearGradient(vg, x, y, x + c, y, nvgRGBA(255, 255, 255, 16), nvgRGBA(0, 0, 0, 70))
                                         : nvgLinearGradient(vg, x, y, x, y + c, nvgRGBA(255, 255, 255, 16), nvgRGBA(0, 0, 0, 70)));
                    nvgFill(vg);
                }
        }
        else if (look == 13) {   // BRUSHED STEEL: fine streaks and a sheen
            nvgBeginPath(vg);
            nvgRect(vg, x0, y0, v.size.x, v.size.y);
            nvgFillPaint(vg, nvgLinearGradient(vg, x0, y0, x0, y1, nvgRGBA(255, 255, 255, 26), nvgRGBA(0, 0, 0, 30)));
            nvgFill(vg);
            nvgBeginPath(vg);
            for (float y = std::floor(y0 / 2.f) * 2.f; y < y1; y += 2.f) {
                unsigned h = (unsigned) ((int) y * 2654435761u);
                if (h % 3) continue;
                nvgMoveTo(vg, x0, y); nvgLineTo(vg, x1, y);
            }
            nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 10));
            nvgStrokeWidth(vg, 0.7f);
            nvgStroke(vg);
        }
        else if (look == 14) {   // SUNSET FADE: the colour fading to a warm glow at the bottom
            nvgBeginPath(vg);
            nvgRect(vg, x0, y0, v.size.x, v.size.y);
            nvgFillPaint(vg, nvgLinearGradient(vg, x0, y0, x0, y1, nvgRGBA(0, 0, 0, 120), nvgRGBA(255, 90, 20, 90)));
            nvgFill(vg);
        }
        else if (look == 15) {   // HALFTONE DOTS: dots that grow toward the bottom right
            float g = 14.f, span = v.size.x + v.size.y;
            nvgBeginPath(vg);
            for (float y = std::floor(y0 / g) * g; y < y1 + g; y += g)
                for (float x = std::floor(x0 / g) * g; x < x1 + g; x += g) {
                    float k = clamp(((x - x0) + (y - y0)) / span, 0.f, 1.f);
                    nvgCircle(vg, x, y, 0.6f + k * 4.6f);
                }
            nvgFillColor(vg, nvgRGBA(255, 150, 40, 26));
            nvgFill(vg);
        }
        else if (look == 16) {   // BRICK WALL: running bond with mortar lines
            float bw = 60.f, bh = 22.f;
            nvgBeginPath(vg);
            for (float y = std::floor(y0 / bh) * bh; y < y1; y += bh) {
                nvgMoveTo(vg, x0, y); nvgLineTo(vg, x1, y);
                float off = ((int) std::floor(y / bh)) & 1 ? bw * 0.5f : 0.f;
                for (float x = std::floor(x0 / bw) * bw + off; x < x1; x += bw) { nvgMoveTo(vg, x, y); nvgLineTo(vg, x, y + bh); }
            }
            nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 110));
            nvgStrokeWidth(vg, 2.f);
            nvgStroke(vg);
        }
        else if (look == 17) {   // WOOD SLATS: vertical boards with dark gaps and a little grain
            float sw = 36.f;
            nvgBeginPath(vg);
            for (float x = std::floor(x0 / sw) * sw; x < x1; x += sw)
                for (float k = 6.f; k < sw - 4.f; k += 7.f) { nvgMoveTo(vg, x + k, y0); nvgLineTo(vg, x + k + 2.f, y1); }
            nvgStrokeColor(vg, nvgRGBA(255, 220, 180, 10));
            nvgStrokeWidth(vg, 1.f);
            nvgStroke(vg);
            nvgBeginPath(vg);
            for (float x = std::floor(x0 / sw) * sw; x < x1; x += sw) nvgRect(vg, x, y0, 3.f, v.size.y);
            nvgFillColor(vg, nvgRGBA(0, 0, 0, 140));
            nvgFill(vg);
        }
    }
    void drawImage(const DrawArgs& args, math::Rect v) {
        SynTHBloKBackdrop* m = module;
        if (m->imagePath.empty()) { if (image >= 0) freeImage(); return; }
        if (image < 0 || imageLoaded != m->imagePath || imageVg != args.vg) {
            freeImage();
            image = nvgCreateImage(args.vg, m->imagePath.c_str(), NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY);
            imageVg = args.vg;
            imageLoaded = m->imagePath;
            if (image >= 0) nvgImageSize(args.vg, image, &imgW, &imgH);
        }
        if (image < 0 || imgW <= 0 || imgH <= 0) return;
        float w = imgW * m->imgScale, h = imgH * m->imgScale, ox = 0.f, oy = 0.f;
        math::Rect area = v;
        if (m->fit == 1 || m->fit == 2) {   // fill / fit the visible screen (the image stays put as you scroll)
            float sx = v.size.x / imgW, sy = v.size.y / imgH, k = m->fit == 1 ? std::max(sx, sy) * 1.004f : std::min(sx, sy);
            w = imgW * k; h = imgH * k;
            ox = v.pos.x + (v.size.x - w) * 0.5f; oy = v.pos.y + (v.size.y - h) * 0.5f;
            if (m->fit == 2) area = math::Rect(Vec(ox, oy), Vec(w, h));
        }
        else if (m->fit == 3) {
            ox = v.pos.x + (v.size.x - w) * 0.5f; oy = v.pos.y + (v.size.y - h) * 0.5f;
            area = math::Rect(Vec(ox, oy), Vec(w, h));
        }
        // FINE-TUNING: stretch the width / height (about the image's centre) and move it by a share of its size
        {
            float cx = ox + w * 0.5f, cy = oy + h * 0.5f;
            w *= m->imgW; h *= m->imgH;
            ox = cx - w * 0.5f + m->imgX * w; oy = cy - h * 0.5f + m->imgY * h;
            if (m->fit >= 2) area = math::Rect(Vec(ox, oy), Vec(w, h));
        }
        NVGpaint p = nvgImagePattern(args.vg, ox, oy, w, h, 0.f, image, 1.f);
        nvgBeginPath(args.vg);
        nvgRect(args.vg, area.pos.x, area.pos.y, area.size.x, area.size.y);
        nvgFillPaint(args.vg, p);
        nvgFill(args.vg);
        if (m->imgDim > 0.f) {
            nvgFillColor(args.vg, nvgRGBAf(0.f, 0.f, 0.f, m->imgDim));
            nvgFill(args.vg);
        }
    }

    // ---- the case ----
    static void drawRail(NVGcontext* vg, float x0, float x1, float y, NVGcolor c) {
        float h = 15.f;
        NVGpaint p = nvgLinearGradient(vg, 0, y, 0, y + h, nvgLerpRGBA(c, nvgRGB(255, 255, 255), 0.25f), nvgLerpRGBA(c, nvgRGB(0, 0, 0), 0.35f));
        nvgBeginPath(vg);
        nvgRect(vg, x0, y, x1 - x0, h);
        nvgFillPaint(vg, p);
        nvgFill(vg);
        // the threaded strip with its holes every HP
        nvgBeginPath(vg);
        float hx0 = std::ceil(x0 / RACK_GRID_WIDTH) * RACK_GRID_WIDTH;
        for (float x = hx0; x < x1; x += RACK_GRID_WIDTH) nvgCircle(vg, x + RACK_GRID_WIDTH * 0.5f, y + h * 0.5f, 1.6f);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 150));
        nvgFill(vg);
    }
    static float topLip(const CaseStyle& cs) { return cs.extras & X_1U ? 22.f : 6.f; }
    static float caseRadius(const CaseStyle& cs) { return cs.ends == E_ROUND ? std::max(6.f, std::min(cs.side * 0.8f, 16.f)) : 0.f; }
    // ---- REAL TEXTURES (tools/make_case_textures.py -> res/cases/*.png): neutral light/dark overlays drawn over the
    // case's own colour, tiled; one textured rectangle each, so realism costs next to nothing
    std::map<std::string, int> texImg; NVGcontext* texVg = NULL;
    int tex(NVGcontext* vg, const char* name) {
        if (texVg != vg) { texImg.clear(); texVg = vg; }   // a new GL context: images belong to the old one
        auto it = texImg.find(name);
        if (it != texImg.end()) return it->second;
        int img = nvgCreateImage(vg, asset::plugin(pluginInstance, string::f("res/cases/%s.png", name)).c_str(), NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY);
        texImg[name] = img;
        return img;
    }
    void texRect(NVGcontext* vg, float x, float y, float w, float h, float rad, const char* name, float alpha, bool vertical, float size = 200.f) {
        int img = tex(vg, name);
        if (img < 0) return;
        NVGpaint p = vertical ? nvgImagePattern(vg, x, y, size, size, (float) M_PI * 0.5f, img, alpha) : nvgImagePattern(vg, x, y, size, size, 0.f, img, alpha);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, h, rad);
        nvgFillPaint(vg, p);
        nvgFill(vg);
    }
    static NVGcolor cheekBase(const CaseStyle& cs) { return cs.sideIdx > 0 ? sideColor(cs.sideIdx) : cs.base; }
    static int cheekMat(const CaseStyle& cs) { return cs.sideIdx > 0 ? kSideMat[clamp(cs.sideIdx, 0, kSideMatN - 1)].material : cs.material; }
    static const char* cheekTex(const CaseStyle& cs) { return cs.sideIdx > 0 ? kSideMat[clamp(cs.sideIdx, 0, kSideMatN - 1)].tex : NULL; }
    // over the current path: a species wood face, lit from above, then its finish (oil: none; satin: soft sheen; gloss: a
    // bright vertical highlight and a clear-coat edge)
    void woodFace(NVGcontext* vg, const CaseStyle& cs, const char* t, float x, float y, float w, float h) {
        int img = tex(vg, t);
        if (img >= 0) { nvgFillPaint(vg, nvgImagePattern(vg, x, y, 260.f, 260.f, (float) M_PI * 0.5f, img, 1.f)); nvgFill(vg); }
        nvgFillPaint(vg, nvgLinearGradient(vg, 0, y, 0, y + h, nvgRGBA(255, 255, 255, 18), nvgRGBA(0, 0, 0, 70))); nvgFill(vg);
        if (cs.sideFinish >= 1) {
            float k = cs.sideFinish == 2 ? 1.f : 0.45f, cx = x + w * 0.38f, bw = std::max(2.f, std::fabs(w) * 0.22f);
            nvgFillPaint(vg, nvgLinearGradient(vg, cx - bw, 0, cx, 0, nvgRGBA(255, 255, 255, 0), nvgRGBA(255, 255, 255, (int) (60 * k)))); nvgFill(vg);
            nvgFillPaint(vg, nvgLinearGradient(vg, cx, 0, cx + bw, 0, nvgRGBA(255, 255, 255, (int) (60 * k)), nvgRGBA(255, 255, 255, 0))); nvgFill(vg);
            nvgFillPaint(vg, nvgLinearGradient(vg, 0, y, 0, y + h * 0.18f, nvgRGBA(255, 255, 255, (int) (40 * k)), nvgRGBA(255, 255, 255, 0))); nvgFill(vg);
            if (cs.sideFinish == 2) { nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 46)); nvgStrokeWidth(vg, 0.7f); nvgStroke(vg); }
        }
    }
    static const char* matTex(int mat) {
        switch (mat) {
            case M_WOOD: case M_PLY: return "wood";
            case M_BRUSHED: case M_EARS: return "brushed";
            case M_CARBON: return "carbon";
            case M_TOLEX: return "tolex";
            case M_ACRYLIC: return NULL;
            default: return "powder";
        }
    }
    // side panels in their own material, over the case's ends (square, rounded, end-cap frames)
    void drawCheeks(NVGcontext* vg, const CaseStyle& cs, float x, float y, float w, float h) {
        if (cs.sideIdx <= 0) return;
        float s = cs.side, rad = caseRadius(cs);
        NVGcolor b = cheekBase(cs);
        int mat = cheekMat(cs);
        for (int side = 0; side < 2; side++) {
            float cx = side ? x + w - s : x;
            nvgSave(vg); nvgIntersectScissor(vg, cx, y - 2.f, s, h + 4.f);
            nvgBeginPath(vg); nvgRoundedRect(vg, x, y, w, h, rad);
            nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x, y + h, nvgLerpRGBA(b, nvgRGB(255, 255, 255), 0.08f), nvgLerpRGBA(b, nvgRGB(0, 0, 0), 0.15f)));
            nvgFill(vg);
            if (const char* wt = cheekTex(cs)) { nvgBeginPath(vg); nvgRoundedRect(vg, x, y, w, h, rad); woodFace(vg, cs, wt, cx, y, s, h); }
            else if (const char* t = matTex(mat)) texRect(vg, cx, y, s, h, 0.f, t, 0.85f, true, t[0] == 'w' ? 240.f : 180.f);
            if (mat == M_PLY) {
                float e = std::min(5.f, s * 0.25f), ex = side ? cx + s - e : cx;
                nvgBeginPath(vg); nvgRect(vg, ex, y, e, h);
                nvgFillColor(vg, nvgLerpRGBA(nvgRGB(214, 186, 140), b, 0.25f)); nvgFill(vg);
                texRect(vg, ex, y, e, h, 0.f, "ply_edge", 0.9f, true, 64.f);
            }
            nvgRestore(vg);
            nvgBeginPath(vg); nvgMoveTo(vg, side ? cx : cx + s, y + 1.f); nvgLineTo(vg, side ? cx : cx + s, y + h - 1.f);   // the joint
            nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 120)); nvgStrokeWidth(vg, 1.f); nvgStroke(vg);
        }
    }
    void drawFrameFill(NVGcontext* vg, const CaseStyle& cs, float x, float y, float w, float h) {
        float rad = caseRadius(cs), s = cs.side;
        auto base = [&](float bx, float by, float bw, float bh, float r) {   // the colour, lit from above
            nvgBeginPath(vg); nvgRoundedRect(vg, bx, by, bw, bh, r);
            nvgFillPaint(vg, nvgLinearGradient(vg, bx, by, bx, by + bh, nvgLerpRGBA(cs.base, nvgRGB(255, 255, 255), 0.07f), nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.12f)));
            nvgFill(vg);
        };
        if (cs.material == M_WOOD || cs.material == M_PLY) {
            base(x, y, w, h, rad);
            texRect(vg, x, y, w, h, rad, "wood", 0.85f, false, 240.f);   // grain along the lips
            for (int side = 0; side < 2; side++) {   // and up the cheeks
                float cx = side ? x + w - s : x;
                nvgSave(vg); nvgIntersectScissor(vg, cx, y, s, h);
                base(x, y, w, h, rad);
                texRect(vg, cx, y, s, h, 0.f, "wood", 0.85f, true, 240.f);
                if (cs.material == M_PLY) {   // the layered cut edge round the outside of the cheek
                    float e = std::min(5.f, s * 0.25f), ex = side ? cx + s - e : cx;
                    nvgBeginPath(vg); nvgRect(vg, ex, y, e, h);
                    nvgFillColor(vg, nvgLerpRGBA(nvgRGB(214, 186, 140), cs.base, 0.25f)); nvgFill(vg);
                    texRect(vg, ex, y, e, h, 0.f, "ply_edge", 0.9f, true, 64.f);
                }
                nvgRestore(vg);
            }
            if (cs.material == M_PLY) {   // ply edge along the top lip too
                float e = 3.f;
                nvgBeginPath(vg); nvgRect(vg, x + s, y, w - 2.f * s, e);
                nvgFillColor(vg, nvgLerpRGBA(nvgRGB(214, 186, 140), cs.base, 0.25f)); nvgFill(vg);
                texRect(vg, x + s, y, w - 2.f * s, e, 0.f, "ply_edge", 0.9f, false, 64.f);
            }
            return;
        }
        if (cs.material == M_BRUSHED || cs.material == M_EARS) {
            if (cs.ends == E_CAPS) base(x, y, w, h, rad); else base(x, y, w, h, rad);
            texRect(vg, x, y, w, h, rad, "brushed", 0.9f, false, 300.f);
            return;
        }
        // carbon: the original fine woven check (drawFrameOld) - the user preferred it to the big twill texture
        if (cs.material == M_TOLEX) {
            base(x, y, w, h, rad); texRect(vg, x, y, w, h, rad, "tolex", 0.9f, false, 160.f);
            nvgBeginPath(vg); nvgRect(vg, x + 1.5f, y + 1.5f, w - 3.f, h - 3.f);   // the aluminium edge trim
            nvgStrokeColor(vg, nvgTransRGBA(cs.accent, 120)); nvgStrokeWidth(vg, 2.f); nvgStroke(vg);
            return;
        }
        if (cs.material == M_POWDER || cs.material == M_ANODIZED) {
            base(x, y, w, h, rad);
            texRect(vg, x, y, w, h, rad, "powder", cs.material == M_POWDER ? 0.9f : 0.45f, false, 160.f);
            return;
        }
        drawFrameOld(vg, cs, x, y, w, h);
    }
    void drawFrameOld(NVGcontext* vg, const CaseStyle& cs, float x, float y, float w, float h) {
        float rad = caseRadius(cs);
        switch (cs.material) {
            case M_ACRYLIC: {   // see-through: a tint, a bright cut edge and a diagonal glint
                nvgBeginPath(vg); nvgRoundedRect(vg, x, y, w, h, rad); nvgFillColor(vg, nvgTransRGBA(cs.base, 70)); nvgFill(vg);
                nvgStrokeColor(vg, nvgTransRGBA(cs.accent, 120)); nvgStrokeWidth(vg, 1.5f); nvgStroke(vg);
                nvgSave(vg);
                nvgIntersectScissor(vg, x, y, w, h);
                nvgBeginPath(vg);
                for (float g = x - h; g < x + w; g += 260.f) { nvgMoveTo(vg, g, y + h); nvgLineTo(vg, g + 40.f, y + h); nvgLineTo(vg, g + 40.f + h, y); nvgLineTo(vg, g + h, y); nvgClosePath(vg); }
                nvgFillColor(vg, nvgRGBA(255, 255, 255, 14));
                nvgFill(vg);
                nvgRestore(vg);
                break;
            }
            case M_PRINTED: {   // 3D printed: fine layer lines along the length
                NVGpaint p = nvgLinearGradient(vg, x, y, x, y + h, nvgLerpRGBA(cs.base, nvgRGB(255, 255, 255), 0.1f), cs.base);
                nvgBeginPath(vg); nvgRoundedRect(vg, x, y, w, h, rad); nvgFillPaint(vg, p); nvgFill(vg);
                nvgSave(vg);
                nvgIntersectScissor(vg, x, y, w, h);
                nvgBeginPath(vg);
                for (float k = y + 1.f; k < y + h; k += 1.4f) { nvgMoveTo(vg, x, k); nvgLineTo(vg, x + w, k); }
                nvgStrokeColor(vg, nvgTransRGBA(cs.accent, 60));
                nvgStrokeWidth(vg, 0.5f);
                nvgStroke(vg);
                nvgRestore(vg);
                break;
            }
            case M_WOOD: {   // grain: long wavy lines along the board
                NVGpaint p = nvgLinearGradient(vg, x, y, x + w, y + h, nvgLerpRGBA(cs.base, nvgRGB(255, 230, 200), 0.08f), cs.base);
                nvgBeginPath(vg); nvgRoundedRect(vg, x, y, w, h, rad); nvgFillPaint(vg, p); nvgFill(vg);
                nvgSave(vg);
                nvgIntersectScissor(vg, x + rad, y, w - 2.f * rad, h);   // the grain stays off the rounded corners
                nvgBeginPath(vg);
                bool vertical = h > w;
                float len = vertical ? h : w, span = vertical ? w : h;
                for (float k = 2.f; k < span; k += 2.6f) {
                    float ph = std::sin(k * 1.7f) * 3.f;
                    for (float t = 0.f; t <= len; t += 12.f) {
                        float off = k + std::sin(t * 0.03f + ph) * 1.2f;
                        float px = vertical ? x + off : x + t, py = vertical ? y + t : y + off;
                        if (t == 0.f) nvgMoveTo(vg, px, py); else nvgLineTo(vg, px, py);
                    }
                }
                nvgStrokeColor(vg, nvgTransRGBA(cs.accent, 70));
                nvgStrokeWidth(vg, 0.7f);
                nvgStroke(vg);
                nvgRestore(vg);
                break;
            }
            case M_BRUSHED: {   // fine horizontal brushing
                NVGcolor lo = cs.ends == E_CAPS ? nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.25f) : cs.accent;   // the end caps take the accent
                NVGpaint p = nvgLinearGradient(vg, x, y, x, y + h, nvgLerpRGBA(cs.base, nvgRGB(255, 255, 255), 0.06f), nvgLerpRGBA(lo, cs.base, 0.4f));
                nvgBeginPath(vg); nvgRoundedRect(vg, x, y, w, h, rad); nvgFillPaint(vg, p); nvgFill(vg);
                nvgSave(vg);
                nvgIntersectScissor(vg, x, y, w, h);
                nvgBeginPath(vg);
                for (float k = y + 1.f; k < y + h; k += 1.7f) { nvgMoveTo(vg, x, k); nvgLineTo(vg, x + w, k); }
                nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 10));
                nvgStrokeWidth(vg, 0.6f);
                nvgStroke(vg);
                nvgRestore(vg);
                break;
            }
            case M_CARBON: {   // the woven check
                nvgBeginPath(vg); nvgRect(vg, x, y, w, h); nvgFillColor(vg, cs.base); nvgFill(vg);
                nvgSave(vg);
                nvgIntersectScissor(vg, x, y, w, h);
                nvgBeginPath(vg);
                float s = 5.f;
                for (float yy = y; yy < y + h; yy += s)
                    for (float xx = x; xx < x + w; xx += s)
                        if ((((int) ((xx - x) / s)) + ((int) ((yy - y) / s))) & 1) nvgRect(vg, xx, yy, s, s);
                nvgFillColor(vg, cs.accent);
                nvgFill(vg);
                nvgRestore(vg);
                break;
            }
            case M_TOLEX: {   // the textured black, with an aluminium edge
                nvgBeginPath(vg); nvgRect(vg, x, y, w, h); nvgFillColor(vg, cs.base); nvgFill(vg);
                nvgBeginPath(vg); nvgRect(vg, x + 1.5f, y + 1.5f, w - 3.f, h - 3.f);
                nvgStrokeColor(vg, nvgTransRGBA(cs.accent, 150)); nvgStrokeWidth(vg, 2.f); nvgStroke(vg);
                break;
            }
            default: {   // anodized, powder, ears: a soft sheen over the colour
                NVGpaint p = nvgLinearGradient(vg, x, y, x, y + h, nvgLerpRGBA(cs.base, nvgRGB(255, 255, 255), 0.12f), cs.base);
                nvgBeginPath(vg); nvgRoundedRect(vg, x, y, w, h, rad); nvgFillPaint(vg, p); nvgFill(vg);
                if (cs.material == M_POWDER) {   // the powder coat's speckle
                    nvgBeginPath(vg);
                    for (float yy = y + 2.f; yy < y + h; yy += 5.f)
                        for (float xx = x + 2.f + std::fmod(yy * 1.3f, 5.f); xx < x + w; xx += 6.f) nvgRect(vg, xx, yy, 0.8f, 0.8f);
                    nvgFillColor(vg, nvgRGBA(0, 0, 0, 30));
                    nvgFill(vg);
                }
            }
        }
    }
    // ---- OPEN FRAMES: only the two side panels (square, stepped, curved or a printed lattice) and the bare rails
    void cheekPath(NVGcontext* vg, const CaseStyle& cs, float xin, float y, float h, int dir, int rows, bool holes) {
        // xin: the inner edge (against the modules); dir -1 = left cheek (grows left), +1 = right cheek
        float s = cs.side, rh = RACK_GRID_HEIGHT, lip = 8.f;
        float top = y - lip, bot = y + h + lip;
        nvgBeginPath(vg);
        nvgMoveTo(vg, xin, top);
        if (cs.ends == E_STEP) {   // a staircase: each row sets back, so the panel steps out toward the bottom
            float step = s * 0.55f;
            for (int k = 0; k < rows; k++) {
                float ox = xin + dir * (s + k * step), ry = y + k * rh;
                nvgLineTo(vg, ox, k == 0 ? top : ry);
                nvgLineTo(vg, ox, k == rows - 1 ? bot : ry + rh);
            }
        }
        else if (cs.ends == E_CURVE) {   // one long sweep, slim at the top, wide at the foot
            float o0 = xin + dir * s * 0.7f, o1 = xin + dir * s * 1.9f;
            nvgLineTo(vg, o0, top);
            nvgBezierTo(vg, o0 + dir * s * 0.2f, top + (bot - top) * 0.45f, o1, top + (bot - top) * 0.55f, o1, bot);
        }
        else { nvgLineTo(vg, xin + dir * s, top); nvgLineTo(vg, xin + dir * s, bot); }
        nvgLineTo(vg, xin, bot);
        nvgClosePath(vg);
        if (holes && cs.ends == E_TRUSS) {   // the printed lattice: triangles cut through, alternating
            float w = s * 0.62f, cx = xin + dir * s * 0.5f, hh = w * 1.15f;
            int i = 0;
            for (float ty = top + 10.f; ty + hh < bot - 8.f; ty += hh * 0.62f, i++) {
                nvgMoveTo(vg, cx - w * 0.5f, i & 1 ? ty : ty + hh);
                nvgLineTo(vg, cx + w * 0.5f, i & 1 ? ty : ty + hh);
                nvgLineTo(vg, cx, i & 1 ? ty + hh : ty);
                nvgClosePath(vg);
                nvgPathWinding(vg, NVG_HOLE);
            }
        }
    }
    void drawOpenFrame(NVGcontext* vg, const CaseStyle& cs, math::Rect in, float depthMm, bool doShadow, bool doRails, const std::vector<float>& rowTops) {
        int rows = std::max(1, (int) rowTops.size());
        float d = mm2px(depthMm) * 0.18f;
        for (int side = 0; side < 2; side++) {
            int dir = side ? 1 : -1;
            float xin = side ? in.pos.x + in.size.x : in.pos.x;
            if (doShadow) {   // a soft shadow under each panel
                nvgSave(vg); nvgTranslate(vg, d * 0.5f + 4.f, d * 0.7f + 8.f);
                cheekPath(vg, cs, xin, in.pos.y, in.size.y, dir, rows, false);
                nvgFillColor(vg, nvgRGBA(0, 0, 0, 90)); nvgFill(vg);
                nvgRestore(vg);
            }
            if (d > 0.5f) {   // the panel's thickness, seen behind it
                int n = std::max(2, std::min(16, (int) (d / 2.f)));
                for (int k = n; k >= 1; k--) {
                    nvgSave(vg); nvgTranslate(vg, d * k / n, d * k / n);
                    cheekPath(vg, cs, xin, in.pos.y, in.size.y, dir, rows, true);
                    nvgFillColor(vg, nvgLerpRGBA(cheekBase(cs), nvgRGB(0, 0, 0), 0.5f + 0.25f * k / n)); nvgFill(vg);
                    nvgRestore(vg);
                }
            }
            // the face: the colour lit from above, then its texture
            cheekPath(vg, cs, xin, in.pos.y, in.size.y, dir, rows, true);
            nvgFillPaint(vg, nvgLinearGradient(vg, 0, in.pos.y, 0, in.pos.y + in.size.y, nvgLerpRGBA(cheekBase(cs), nvgRGB(255, 255, 255), 0.08f), nvgLerpRGBA(cheekBase(cs), nvgRGB(0, 0, 0), 0.15f)));
            nvgFill(vg);
            const char* wt = cheekTex(cs);
            const char* t = wt ? NULL : matTex(cheekMat(cs));
            int img = t ? tex(vg, t) : -1;
            if (wt) woodFace(vg, cs, wt, xin, in.pos.y, dir * cs.side, in.size.y);
            if (img >= 0) {
                nvgFillPaint(vg, nvgImagePattern(vg, xin, in.pos.y, 240.f, 240.f, (float) M_PI * 0.5f, img, 0.85f));
                nvgFill(vg);
            }
            nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 120)); nvgStrokeWidth(vg, 0.8f); nvgStroke(vg);
            for (float rt : rowTops)   // the black steel bolts holding the rails
                for (int k = 0; k < 2; k++) {
                    nvgBeginPath(vg);
                    nvgCircle(vg, xin + dir * cs.side * 0.5f, rt + (k ? RACK_GRID_HEIGHT - 7.5f : 7.5f), 2.2f);
                    nvgFillColor(vg, nvgRGB(16, 16, 18)); nvgFill(vg);
                }
        }
    }
    void drawCase(NVGcontext* vg, const CaseStyle& cs, math::Rect in, float depthMm, bool doShadow, bool doRails, const std::vector<float>& rowTops) {
        if (cs.extras & X_OPEN) { drawOpenFrame(vg, cs, in, depthMm, doShadow, doRails, rowTops); return; }
        float s = cs.side, t = 6.f, tt = topLip(cs);   // ends, the bottom lip and the top lip (taller with a 1U row)
        float x = in.pos.x - s, y = in.pos.y - tt, w = in.size.x + 2.f * s, h = in.size.y + t + tt;
        float d = mm2px(depthMm) * 0.18f;   // the visible side, below and to the right: the real depth, seen at a slant
        if (doShadow) {
            // two shadows: a wide soft ambient one, and a tight contact one where the case meets the surface
            float rr = caseRadius(cs);
            nvgBeginPath(vg);
            nvgRect(vg, x - 80.f, y - 60.f, w + d + 180.f, h + d + 180.f);
            nvgFillPaint(vg, nvgBoxGradient(vg, x + d * 0.4f + 6.f, y + d * 0.6f + 14.f, w, h, rr + 10.f, 60.f, nvgRGBA(0, 0, 0, 110), nvgRGBA(0, 0, 0, 0)));
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgRect(vg, x - 20.f, y - 20.f, w + d + 60.f, h + d + 60.f);
            nvgFillPaint(vg, nvgBoxGradient(vg, x + d * 0.8f + 1.f, y + d * 0.9f + 3.f, w, h, rr + 2.f, 10.f, nvgRGBA(0, 0, 0, 150), nvgRGBA(0, 0, 0, 0)));
            nvgFill(vg);
        }
        float rr = caseRadius(cs);
        if (d > 0.5f && rr > 0.f) {   // a rounded case: its rounded outline swept back for the depth
            int n = std::max(2, std::min(24, (int) (d / 1.5f)));
            for (int k = n; k >= 1; k--) {
                float o = d * k / n;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, x + o, y + o, w, h, rr);
                nvgFillColor(vg, nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.45f + 0.15f * k / n));
                nvgFill(vg);
            }
        }
        else if (d > 0.5f) {   // the 3D sides: right and bottom faces, darker
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + w, y); nvgLineTo(vg, x + w + d, y + d); nvgLineTo(vg, x + w + d, y + h + d); nvgLineTo(vg, x + w, y + h);
            nvgClosePath(vg);
            nvgFillPaint(vg, nvgLinearGradient(vg, x + w, 0, x + w + d, 0, nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.62f), nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.82f)));
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgMoveTo(vg, x, y + h); nvgLineTo(vg, x + w, y + h); nvgLineTo(vg, x + w + d, y + h + d); nvgLineTo(vg, x + d, y + h + d);
            nvgClosePath(vg);
            nvgFillPaint(vg, nvgLinearGradient(vg, 0, y + h, 0, y + h + d, nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.55f), nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.8f)));
            nvgFill(vg);
        }
        // the frame: the ends and lips, then the dark well the modules sit in
        drawFrameFill(vg, cs, x, y, w, h);
        drawCheeks(vg, cs, x, y, w, h);
        bool woodish = cs.material == M_WOOD || cs.material == M_PLY;
        if (rr > 0.f && cs.material != M_ACRYLIC) {   // contoured: rounded cheeks and a rounded top, lit from above
            if (!woodish) {   // (not on wood: the dark ring read as a black border round the grain)
                nvgBeginPath(vg);
                nvgRoundedRect(vg, x, y, w, h, rr);
                nvgFillPaint(vg, nvgBoxGradient(vg, x + s * 0.5f, y + tt * 0.5f, w - s, h - tt * 0.5f - t * 0.5f, rr, std::max(s, 8.f), nvgRGBA(0, 0, 0, 0), nvgRGBA(0, 0, 0, 110)));
                nvgFill(vg);
            }
            nvgBeginPath(vg);   // the highlight running round the top edge
            nvgRoundedRect(vg, x + 1.5f, y + 1.f, w - 3.f, tt + rr, rr);
            nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x, y + tt + rr * 0.5f, nvgRGBA(255, 255, 255, 46), nvgRGBA(255, 255, 255, 0)));
            nvgFill(vg);
        }
        nvgBeginPath(vg);
        nvgRect(vg, in.pos.x, in.pos.y, in.size.x, in.size.y);
        nvgFillColor(vg, nvgRGB(10, 10, 11));   // the empty part of the case (the module rows stay open: Rack's own rails)
        nvgFill(vg);
        // finishing touches
        if (cs.material == M_TOLEX) {   // corner caps: small, dark metal, a thin lit edge (not big grey blocks)
            float c = std::min(12.f, s * 0.75f);
            for (int k = 0; k < 4; k++) {
                float cx = k & 1 ? x + w - c : x, cy = k & 2 ? y + h - c : y;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, cx, cy, c, c, 2.5f);
                nvgFillPaint(vg, nvgLinearGradient(vg, cx, cy, cx + c, cy + c, nvgLerpRGBA(cs.accent, nvgRGB(0, 0, 0), 0.35f), nvgLerpRGBA(cs.accent, nvgRGB(0, 0, 0), 0.65f)));
                nvgFill(vg);
                nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 28)); nvgStrokeWidth(vg, 0.8f); nvgStroke(vg);
            }
        }
        if (cs.material == M_EARS) {   // 19" ears with their slots
            for (int side = 0; side < 2; side++) {
                float ex = side ? x + w - s : x;
                for (float rt : rowTops)
                    for (int k = 0; k < 2; k++) {
                        nvgBeginPath(vg);
                        nvgRoundedRect(vg, ex + s * 0.3f, rt + (k ? RACK_GRID_HEIGHT - 26.f : 14.f), s * 0.4f, 10.f, 4.f);
                        nvgFillColor(vg, nvgRGBA(0, 0, 0, 170));
                        nvgFill(vg);
                    }
            }
        }
        if (cs.material == M_WOOD || cs.material == M_PLY) {   // the end cheeks' screws (black steel)
            for (int side = 0; side < 2; side++)
                for (float rt : rowTops) {
                    nvgBeginPath(vg);
                    nvgCircle(vg, side ? x + w - s * 0.5f : x + s * 0.5f, rt + RACK_GRID_HEIGHT * 0.5f, 2.f);
                    nvgFillColor(vg, nvgRGBA(20, 14, 10, 200));
                    nvgFill(vg);
                }
        }
        if (cs.ends == E_SLANT) {   // cheeks that flare out toward the bottom: a case on legs, a tiered cabinet
            float fl = s * 0.45f;
            for (int side = 0; side < 2; side++) {
                float ex = side ? x + w : x, dir = side ? 1.f : -1.f;
                nvgBeginPath(vg);
                nvgMoveTo(vg, ex, y + 2.f); nvgLineTo(vg, ex, y + h); nvgLineTo(vg, ex + dir * fl, y + h); nvgLineTo(vg, ex + dir * 1.5f, y + 2.f);
                nvgClosePath(vg);
                nvgFillPaint(vg, nvgLinearGradient(vg, ex, 0, ex + dir * fl, 0, nvgLerpRGBA(cheekBase(cs), nvgRGB(0, 0, 0), 0.35f), nvgLerpRGBA(cheekBase(cs), nvgRGB(0, 0, 0), 0.7f)));
                nvgFill(vg);
                nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 90)); nvgStrokeWidth(vg, 0.8f); nvgStroke(vg);
            }
        }
        if (cs.ends == E_CAPS) {   // moulded end caps over the extrusions
            for (int side = 0; side < 2; side++) {
                float ex = side ? x + w - s : x;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, ex - (side ? 0.f : 2.f), y - 2.f, s + 2.f, h + 4.f, 4.f);
                nvgFillPaint(vg, nvgLinearGradient(vg, ex, y, ex + s, y, nvgLerpRGBA(cs.accent, nvgRGB(255, 255, 255), 0.12f), cs.accent));
                nvgFill(vg);
                for (float rt : rowTops) {
                    nvgBeginPath(vg);
                    nvgCircle(vg, ex + s * 0.5f, rt + RACK_GRID_HEIGHT * 0.5f, 2.f);
                    nvgFillColor(vg, nvgRGBA(0, 0, 0, 160));
                    nvgFill(vg);
                }
            }
        }
        if (cs.extras & X_1U) {   // the INFO STRIP on the top lip: live readouts from the rack + a stereo level meter
            float ry = y + 3.f, rh = tt - 6.f, sx = in.pos.x, sw = in.size.x;
            nvgBeginPath(vg); nvgRoundedRect(vg, sx, ry, sw, rh, 2.f);
            nvgFillColor(vg, nvgRGB(8, 6, 3)); nvgFill(vg);
            nvgStrokeColor(vg, nvgRGBA(255, 180, 90, 50)); nvgStrokeWidth(vg, 0.8f); nvgStroke(vg);
            std::shared_ptr<window::Font> font = APP->window->uiFont;
            // right end, out to in: 4 dummy jacks (IN L/R, OUT L/R), then a column of LEDs, then the meter
            float jr = clamp(rh * 0.36f, 3.f, 7.f), jp = jr * 2.f + 5.f, jw = sw > 260.f ? 4.f * jp + 4.f : 0.f;
            float lw = sw > 220.f ? 4.f * 9.f + 6.f : 0.f;
            float mid = ry + rh * 0.5f, mw = std::min(160.f, sw * 0.26f), mx = sx + sw - jw - lw - mw - 8.f;
            if (font && sw > 140.f) {
                std::string name = module && !module->title.empty() ? module->title : (APP->patch->path.empty() ? std::string("UNTITLED") : system::getStem(APP->patch->path));
                char clock[16] = ""; std::time_t now = std::time(NULL); std::strftime(clock, sizeof(clock), "%H:%M", std::localtime(&now));
                std::string info = string::f("CPU %d%%   %.1f kHz   %d MODULES   %s", (int) std::round(APP->engine->getMeterAverage() * 100.f),
                                             APP->engine->getSampleRate() / 1000.f, (int) APP->engine->getNumModules(), clock);
                nvgSave(vg);
                nvgIntersectScissor(vg, sx + 4.f, ry, mx - sx - 10.f, rh);
                nvgFontFaceId(vg, font->handle);
                nvgFontSize(vg, 9.f);
                nvgTextLetterSpacing(vg, 1.f);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                nvgBeginPath(vg); nvgCircle(vg, sx + 9.f, mid, 2.2f); nvgFillColor(vg, nvgRGB(255, 140, 30)); nvgFill(vg);
                nvgFillColor(vg, nvgRGB(255, 190, 90));
                float tx = nvgText(vg, sx + 16.f, mid + 0.5f, string::uppercase(name).c_str(), NULL);
                nvgFillColor(vg, nvgRGBA(255, 190, 90, 140));
                nvgText(vg, tx + 14.f, mid + 0.5f, info.c_str(), NULL);
                nvgTextLetterSpacing(vg, 0.f);
                nvgRestore(vg);
            }
            // the meter: two bars, green to amber to red, with a peak hold tick
            for (int c = 0; c < 2; c++) {
                float by = ry + 3.f + c * (rh - 6.f) * 0.5f + 0.5f, bh = (rh - 6.f) * 0.5f - 1.5f;
                nvgBeginPath(vg); nvgRect(vg, mx, by, mw, bh); nvgFillColor(vg, nvgRGB(20, 16, 10)); nvgFill(vg);
                float pk = module ? clamp(module->peak[c], 0.f, 1.2f) / 1.2f : 0.f, hd = module ? clamp(module->hold[c], 0.f, 1.2f) / 1.2f : 0.f;
                if (pk > 0.002f) {
                    nvgBeginPath(vg); nvgRect(vg, mx, by, mw * pk, bh);
                    NVGpaint gp = nvgLinearGradient(vg, mx, 0, mx + mw, 0, nvgRGB(90, 220, 110), nvgRGB(255, 60, 40));
                    nvgFillPaint(vg, gp); nvgFill(vg);
                }
                if (hd > 0.002f) { nvgBeginPath(vg); nvgRect(vg, mx + mw * hd - 1.f, by, 1.5f, bh); nvgFillColor(vg, nvgRGB(255, 220, 160)); nvgFill(vg); }
            }
            // LEDs: POWER (always on), L and R signal (glow with level), CLIP (lights when either side hits the top)
            if (lw > 0.f) {
                float pkL = module ? clamp(module->peak[0], 0.f, 1.f) : 0.f, pkR = module ? clamp(module->peak[1], 0.f, 1.f) : 0.f;
                bool clip = module && (module->hold[0] >= 1.f || module->hold[1] >= 1.f);
                float lv[4] = {1.f, pkL, pkR, clip ? 1.f : 0.f};
                NVGcolor lc[4] = {nvgRGB(255, 140, 30), nvgRGB(90, 230, 110), nvgRGB(90, 230, 110), nvgRGB(255, 50, 35)};
                for (int k = 0; k < 4; k++) {
                    float lx = mx + mw + 10.f + k * 9.f, a = 0.18f + 0.82f * std::sqrt(lv[k]);
                    if (lv[k] > 0.05f) {   // a soft halo when lit
                        nvgBeginPath(vg); nvgCircle(vg, lx, mid, 5.5f);
                        nvgFillPaint(vg, nvgRadialGradient(vg, lx, mid, 1.f, 5.5f, nvgTransRGBAf(lc[k], 0.45f * a), nvgTransRGBAf(lc[k], 0.f))); nvgFill(vg);
                    }
                    nvgBeginPath(vg); nvgCircle(vg, lx, mid, 2.3f);
                    nvgFillColor(vg, nvgLerpRGBA(nvgRGB(30, 24, 18), lc[k], a)); nvgFill(vg);
                    nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 160)); nvgStrokeWidth(vg, 0.6f); nvgStroke(vg);
                }
            }
            // dummy jacks: knurled nut, metal ring, dark hole (decoration only: a case can't own real ports)
            if (jw > 0.f) {
                for (int k = 0; k < 4; k++) {
                    float jx = sx + sw - jw + 2.f + jp * (k + 0.5f) + (k >= 2 ? 2.f : 0.f);
                    nvgBeginPath(vg); nvgCircle(vg, jx, mid + 0.6f, jr + 0.6f); nvgFillColor(vg, nvgRGBA(0, 0, 0, 120)); nvgFill(vg);
                    nvgBeginPath(vg); nvgCircle(vg, jx, mid, jr);
                    nvgFillPaint(vg, nvgLinearGradient(vg, jx, mid - jr, jx, mid + jr, nvgRGB(200, 202, 206), nvgRGB(90, 92, 98))); nvgFill(vg);
                    nvgBeginPath(vg); nvgCircle(vg, jx, mid, jr * 0.68f);
                    nvgFillPaint(vg, nvgLinearGradient(vg, jx, mid - jr, jx, mid + jr, nvgRGB(70, 72, 76), nvgRGB(170, 172, 176))); nvgFill(vg);
                    nvgBeginPath(vg); nvgCircle(vg, jx, mid, jr * 0.42f); nvgFillColor(vg, nvgRGB(6, 6, 7)); nvgFill(vg);
                }
                // a hairline between the IN pair and the OUT pair
                float dx = sx + sw - jw + 2.f + jp * 2.f + 1.f;
                nvgBeginPath(vg); nvgRect(vg, dx, ry + 3.f, 0.8f, rh - 6.f); nvgFillColor(vg, nvgRGBA(255, 180, 90, 50)); nvgFill(vg);
            }
        }
        if (cs.extras & X_HANDLE) {   // a carry handle above the top lip, in the middle
            float hw = std::min(120.f, w * 0.3f), hx = x + (w - hw) * 0.5f;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, hx, y - 9.f, hw, 8.f, 4.f);
            nvgFillColor(vg, nvgRGB(20, 20, 22));
            nvgFill(vg);
            nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 30)); nvgStrokeWidth(vg, 1.f); nvgStroke(vg);
            for (int k = 0; k < 2; k++) {
                nvgBeginPath(vg);
                nvgRect(vg, k ? hx + hw - 10.f : hx, y - 4.f, 10.f, 6.f);
                nvgFillColor(vg, nvgRGB(140, 142, 146));
                nvgFill(vg);
            }
        }
        if (cs.extras & X_LATCHES) {   // suitcase latches on the bottom lip
            for (int k = 0; k < 2; k++) {
                float lx = x + w * (k ? 0.75f : 0.25f) - 9.f, ly = y + h - 4.f;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, lx, ly, 18.f, 10.f, 2.f);
                nvgFillColor(vg, nvgLerpRGBA(cs.accent, nvgRGB(255, 255, 255), 0.1f));
                nvgFill(vg);
                nvgBeginPath(vg);
                nvgRect(vg, lx + 6.f, ly + 3.f, 6.f, 4.f);
                nvgFillColor(vg, nvgRGBA(0, 0, 0, 140));
                nvgFill(vg);
            }
        }
        if (cs.extras & X_LEGS) {   // round rubber feet under the bottom corners
            for (int side = 0; side < 2; side++) {
                float fw = 18.f, fh = 7.f, fx = side ? x + w - s * 0.5f - fw * 0.5f : x + s * 0.5f - fw * 0.5f, fy = y + h - 1.f;
                nvgBeginPath(vg);
                nvgEllipse(vg, fx + fw * 0.5f + 2.f, fy + fh + 2.f, fw * 0.6f, 2.5f);   // its little shadow
                nvgFillColor(vg, nvgRGBA(0, 0, 0, 90)); nvgFill(vg);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, fx, fy, fw, fh, fh * 0.5f);   // the rubber pad, seen from the front: a soft dome
                nvgFillPaint(vg, nvgLinearGradient(vg, 0, fy, 0, fy + fh, nvgRGB(46, 46, 48), nvgRGB(10, 10, 11)));
                nvgFill(vg);
                nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 18)); nvgStrokeWidth(vg, 0.8f); nvgStroke(vg);
            }
        }
        if (cs.material != M_ACRYLIC) {   // a fine lit edge along the top, like light catching a bevel
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + caseRadius(cs) + 1.f, y + 1.f); nvgLineTo(vg, x + w - caseRadius(cs) - 1.f, y + 1.f);
            nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 34)); nvgStrokeWidth(vg, 1.f); nvgStroke(vg);
        }
        if (!woodish) {   // a fine edge so the case reads (not on wood: it looked like a black border)
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x + 0.5f, y + 0.5f, w - 1.f, h - 1.f, caseRadius(cs));
            nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 160));
            nvgStrokeWidth(vg, 1.f);
            nvgStroke(vg);
        }
    }

    // the blocks of modules: rows that hold modules, joined when they touch and overlap
    struct Block { math::Rect r; std::vector<float> rows; std::vector<math::Rect> rowRects; };
    // HOLES: everything BACKDROP draws stays off the module rows, so Rack's own rails always show there.
    // The visible area minus the rows, as rectangles: slabs between the rows' top/bottom edges, and in each the gaps.
    static void outsideRects(math::Rect v, const std::vector<math::Rect>& holes, std::vector<math::Rect>& out) {
        float vx0 = v.pos.x, vx1 = v.pos.x + v.size.x, vy0 = v.pos.y, vy1 = v.pos.y + v.size.y;
        std::vector<float> ys = {vy0, vy1};
        for (const math::Rect& h : holes) {
            float t = h.pos.y, b = h.pos.y + h.size.y;
            if (b <= vy0 || t >= vy1) continue;
            ys.push_back(clamp(t, vy0, vy1)); ys.push_back(clamp(b, vy0, vy1));
        }
        std::sort(ys.begin(), ys.end());
        ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
        for (size_t i = 0; i + 1 < ys.size(); i++) {
            float y0 = ys[i], y1 = ys[i + 1];
            if (y1 - y0 < 0.01f) continue;
            std::vector<std::pair<float, float>> xs;
            for (const math::Rect& h : holes)
                if (h.pos.y <= y0 + 0.01f && h.pos.y + h.size.y >= y1 - 0.01f) xs.push_back({h.pos.x, h.pos.x + h.size.x});
            std::sort(xs.begin(), xs.end());
            float x = vx0;
            for (auto& iv : xs) {
                if (iv.first > x) out.push_back(math::Rect(Vec(x, y0), Vec(std::min(iv.first, vx1) - x, y1 - y0)));
                x = std::max(x, iv.second);
                if (x >= vx1) break;
            }
            if (x < vx1) out.push_back(math::Rect(Vec(x, y0), Vec(vx1 - x, y1 - y0)));
        }
    }
    template <class F> void outside(NVGcontext* vg, math::Rect v, const std::vector<math::Rect>& holes, F fn) {
        // snap the band edges to whole screen pixels: where two bands met at a fractional pixel, each half-painted
        // that row and the rail behind showed through as a thin line
        float t[6]; nvgCurrentTransform(vg, t);
        float pr = APP->window ? std::max(APP->window->pixelRatio, 1.f) : 1.f;
        auto sy = [&](float y) { return (t[3] != 0.f) ? (std::round((t[3] * y + t[5]) * pr) / pr - t[5]) / t[3] : y; };
        auto sx = [&](float x) { return (t[0] != 0.f) ? (std::round((t[0] * x + t[4]) * pr) / pr - t[4]) / t[0] : x; };
        std::vector<math::Rect> hs;
        for (const math::Rect& h : holes) {
            float x0 = sx(h.pos.x), x1 = sx(h.pos.x + h.size.x), y0 = sy(h.pos.y), y1 = sy(h.pos.y + h.size.y);
            hs.push_back(math::Rect(Vec(x0, y0), Vec(x1 - x0, y1 - y0)));
        }
        std::vector<math::Rect> rs;
        outsideRects(v, hs, rs);
        for (const math::Rect& r : rs) {
            if (r.size.x < 0.5f || r.size.y < 0.5f) continue;
            nvgSave(vg);
            nvgScissor(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
            fn();
            nvgRestore(vg);
        }
    }
    std::vector<Block> blocks() {
        std::map<int, std::pair<float, float>> rows;   // row index -> x range
        for (app::ModuleWidget* mw : APP->scene->rack->getModules()) {
            int ri = (int) std::round(mw->box.pos.y / RACK_GRID_HEIGHT);
            auto it = rows.find(ri);
            float a = mw->box.pos.x, b = mw->box.pos.x + mw->box.size.x;
            if (it == rows.end()) rows[ri] = {a, b};
            else { it->second.first = std::min(it->second.first, a); it->second.second = std::max(it->second.second, b); }
        }
        std::vector<Block> out;
        int last = -1000;
        for (auto& kv : rows) {
            float a = kv.second.first, b = kv.second.second, top = kv.first * RACK_GRID_HEIGHT;
            bool joins = !out.empty() && kv.first == last + 1 && a < out.back().r.pos.x + out.back().r.size.x && b > out.back().r.pos.x;
            if (joins) {
                Block& bl = out.back();
                float x0 = std::min(bl.r.pos.x, a), x1 = std::max(bl.r.pos.x + bl.r.size.x, b);
                bl.r = math::Rect(Vec(x0, bl.r.pos.y), Vec(x1 - x0, top + RACK_GRID_HEIGHT - bl.r.pos.y));
                bl.rows.push_back(top);
                bl.rowRects.push_back(math::Rect(Vec(a, top), Vec(b - a, RACK_GRID_HEIGHT)));
            }
            else out.push_back({math::Rect(Vec(a, top), Vec(b - a, RACK_GRID_HEIGHT)), {top}, {math::Rect(Vec(a, top), Vec(b - a, RACK_GRID_HEIGHT))}});
            last = kv.first;
        }
        return out;
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;
        SynTHBloKBackdrop* m = module;
        math::Rect v = args.clipBox;
        const std::vector<CaseStyle>& lib = caseLibrary();
        CaseStyle cs = lib[clamp(m->caseIdx, 0, (int) lib.size() - 1)];
        if (cs.material != M_NONE) {   // the side panel options
            if (m->sideShape > 0) cs.ends = kSideShapeEnds[m->sideShape];
            cs.side *= kSideWidthK[clamp(m->sideWidth, 0, 4)];
            if (m->openMode == 1) cs.extras |= X_OPEN;
            if (m->openMode == 2) cs.extras &= ~X_OPEN;
            cs.sideIdx = m->sideMat;
            cs.sideFinish = m->sideFinish;
            if (m->infoMode == 1) cs.extras |= X_1U;
            if (m->infoMode == 2) cs.extras &= ~X_1U;
        }
        NVGcolor col = kColor[clamp(m->colorIdx, 0, kColorN - 1)];
        std::vector<Block> bl = blocks();
        std::vector<math::Rect> holes;   // the module rows: left open, so Rack's own rails show (BACKDROP never redraws them)
        for (const Block& b : bl) holes.insert(holes.end(), b.rowRects.begin(), b.rowRects.end());
        if (m->showBg)
            outside(args.vg, v, holes, [&]() {
                drawLook(args.vg, v, m->look, col, cs.material == M_NONE ? col : cs.base);
                drawImage(args, v);
            });
        if (cs.material != M_NONE)
            outside(args.vg, v, holes, [&]() {
                for (const Block& b : bl) drawCase(args.vg, cs, b.r, m->depthMm, m->shadow, false, b.rows);
            });
        // the patch title, above the first block
        if (m->showTitle && !bl.empty()) {
            std::string t = m->title;
            if (t.empty()) {
                std::string p = APP->patch->path;
                t = p.empty() ? "UNTITLED PATCH" : system::getStem(p);
            }
            std::shared_ptr<window::Font> font = APP->window->uiFont;
            if (font) {
                nvgFontFaceId(args.vg, font->handle);
                nvgFontSize(args.vg, 13.f);   // small: a label, not a banner
                nvgTextLetterSpacing(args.vg, 1.5f);
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
                float tx = bl.front().r.pos.x - cs.side, ty = bl.front().r.pos.y - (cs.material == M_NONE ? 12.f : topLip(cs) + ((cs.extras & X_HANDLE) ? 14.f : 6.f));
                nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 160));
                nvgText(args.vg, tx + 1.5f, ty + 1.5f, string::uppercase(t).c_str(), NULL);
                nvgFillColor(args.vg, oled::amber());
                nvgText(args.vg, tx, ty, string::uppercase(t).c_str(), NULL);
                nvgTextLetterSpacing(args.vg, 0.f);
            }
        }
    }
};

// one BACKDROP dresses the rack at a time
static BackdropLayer* gLayer = NULL;
static void* gOwner = NULL;

} // namespace

// ---------------------------------------------------------------------------------------------------------
// THE PANEL: res/SynTHBloKBackdrop.svg (tools/make_backdrop_panel.py), 6 HP, the SynTHBloK look. This widget
// only adds the status light: amber while this BACKDROP dresses the rack, dim red if another one does.
// ---------------------------------------------------------------------------------------------------------
struct BackdropPanel : widget::Widget {
    SynTHBloKBackdrop* module = NULL;
    void* widget = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        // the status light: amber while this BACKDROP dresses the rack, dim red if another one does
        bool mine = module && gOwner == widget;
        float cx = box.size.x * 0.5f, cy = mm2px(108.8f);
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, 6.f);
        nvgFillPaint(args.vg, nvgRadialGradient(args.vg, cx, cy, 1.f, 6.f, mine ? nvgRGBA(255, 160, 40, 200) : nvgRGBA(200, 40, 30, 120), nvgRGBA(0, 0, 0, 0)));
        nvgFill(args.vg);
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, 2.2f);
        nvgFillColor(args.vg, mine || !module ? nvgRGB(255, 190, 90) : nvgRGB(160, 40, 30));
        nvgFill(args.vg);
        if (module && !mine) {
            std::shared_ptr<window::Font> font = APP->window->uiFont;
            if (font) {
                nvgFontFaceId(args.vg, font->handle);
                nvgFontSize(args.vg, 5.5f);
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
                nvgFillColor(args.vg, nvgRGB(200, 70, 50));
                nvgText(args.vg, cx, cy - 9.f, "ANOTHER IS ON", NULL);
            }
        }
    }
};

// a menu slider for a percentage setting (the value is a factor: 1 = 100 %)
struct PctQuantity : Quantity {
    float* v; float lo, hi, def; std::string label;
    void setValue(float x) override { *v = clamp(x, lo, hi); }
    float getValue() override { return *v; }
    float getMinValue() override { return lo; }
    float getMaxValue() override { return hi; }
    float getDefaultValue() override { return def; }
    float getDisplayValue() override { return *v * 100.f; }
    void setDisplayValue(float d) override { setValue(d / 100.f); }
    int getDisplayPrecision() override { return 3; }
    std::string getLabel() override { return label; }
    std::string getUnit() override { return "%"; }
};
struct PctSlider : ui::Slider {
    PctSlider() { box.size.x = 220.f; }
    ~PctSlider() { delete quantity; }
};
static ui::Slider* pctSlider(const std::string& label, float* v, float lo, float hi, float def) {
    PctSlider* s = new PctSlider;
    PctQuantity* q = new PctQuantity;
    q->v = v; q->lo = lo; q->hi = hi; q->def = def; q->label = label;
    s->quantity = q;
    return s;
}

// ---- the name OLED: click it (or NAME) and type; the rack's title follows each letter --------------------
// Enter: done. If the patch has no file yet, it also becomes the patch's file name in Rack's patches folder,
// so Ctrl+S saves it under that name straight away (an existing file of that name is never taken over).
static std::string gAutoPath;   // the path BACKDROP last gave an unsaved patch
static std::string safeFileName(std::string n) {
    for (char& c : n) if (std::strchr("\\/:*?\"<>|", c) || (unsigned char) c < 32) c = '-';
    while (!n.empty() && (n.back() == ' ' || n.back() == '.')) n.pop_back();
    while (!n.empty() && n.front() == ' ') n.erase(0, 1);
    return n;
}
static void nameThePatch(const std::string& title) {
    std::string n = safeFileName(title);
    if (n.empty()) return;
    std::string cur = APP->patch->path;
    bool mine = !cur.empty() && cur == gAutoPath && !system::exists(cur);   // still the name we gave, never saved
    if (!cur.empty() && !mine) return;   // a saved patch keeps its file
    std::string dir = asset::user("patches");
    system::createDirectories(dir);
    std::string path = system::join(dir, n + ".vcv");
    if (system::exists(path)) return;
    APP->patch->path = path;
    gAutoPath = path;
}
struct NameOled : ui::TextField {
    SynTHBloKBackdrop* module = NULL;
    NameOled() { multiline = false; }
    bool editing() { return APP->event->getSelectedWidget() == this; }
    void step() override {
        if (module && !editing() && getText() != module->title) setText(module->title);
        ui::TextField::step();
    }
    void onChange(const ChangeEvent& e) override { if (module) module->title = getText(); }
    void onAction(const ActionEvent& e) override {
        if (module) { module->title = getText(); nameThePatch(module->title); }
        APP->event->setSelectedWidget(NULL);
        e.consume(this);
    }
    void onSelect(const SelectEvent& e) override { cursor = selection = (int) getText().size(); ui::TextField::onSelect(e); }
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) { cursor = selection = (int) getText().size(); e.consume(this); }
    }
    void onDragHover(const DragHoverEvent& e) override {}
    void draw(const DrawArgs& args) override {}   // the OLED behind draws the glass
    // one line in the NAME row: fixed sizes (no per-frame re-measuring at many sizes), scrolls with the caret;
    // while typing only this text flashes (breathes), nothing else on the panel or the rack changes
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        bool ed = editing();
        std::string t = getText();
        bool own = !t.empty();
        if (!ed && !own) { std::string pp = APP->patch->path; t = pp.empty() ? "CLICK TO NAME" : system::getStem(pp); }
        if (!ed) t = string::uppercase(t);
        float w = box.size.x, fs = 8.f;
        nvgSave(args.vg);
        nvgScissor(args.vg, 0, 0, w, box.size.y);
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fs);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        float cx = 0.f, scroll = 0.f;
        if (ed) {
            int c = clamp(cursor, 0, (int) t.size());
            cx = nvgTextBounds(args.vg, 0, 0, t.c_str(), t.c_str() + c, NULL);
            scroll = std::max(0.f, cx - (w - 3.f));
        }
        int a = ed ? (int) (150 + 105 * (0.5f + 0.5f * std::sin((float) system::getTime() * 6.f))) : 255;
        nvgFillColor(args.vg, own || ed ? oled::amber(a) : oled::dim());
        float mid = box.size.y * 0.5f;
        nvgText(args.vg, -scroll, mid, t.c_str(), NULL);
        if (ed && std::fmod(system::getTime(), 1.0) < 0.6) {
            nvgBeginPath(args.vg);
            nvgRect(args.vg, -scroll + cx + 0.5f, mid - fs * 0.45f, 1.f, fs * 0.9f);
            nvgFill(args.vg);
        }
        nvgRestore(args.vg);
    }
};


// THEMES: a look, a colour, a case and a depth that go together. Pick one, then change any part (it shows CUSTOM).
struct Theme { const char* name; int look; int color; const char* caseName; float depthMm; int sideMat; int sideFinish; };   // sideMat/Finish: 0 = the case's own
static const Theme kThemes[] = {
    {"SYNTHBLOK BLACK", LOOK_GRADIENT, 0, "BLACK ANODIZED", 65.f},
    {"WALNUT STUDIO", LOOK_CASE_GRADIENT, 0, "WALNUT SKIFF", 84.f},
    {"OAK DAYLIGHT", LOOK_CASE_GRADIENT, 0, "OAK SKIFF", 65.f},
    {"EBONY NIGHT", LOOK_GRADIENT, 1, "EBONY SKIFF", 84.f},
    {"WEST COAST", 17, 13, "WEST COAST BOAT", 84.f},
    {"TIERED SUNSET", 14, 5, "TIERED WALNUT", 100.f},
    {"AMBER LAB", 1, 2, "LAB EXTRUSION", 84.f},
    {"OLED GLOW", 2, 0, "BLACK ANODIZED", 50.f},
    {"DEEP SPACE", 4, 0, "SMOKED ACRYLIC", 50.f},
    {"BLUEPRINT", 10, 8, "BRUSHED ALUMINUM", 65.f},
    {"ROAD WARRIOR", 3, 19, "ROAD CASE", 120.f},
    {"TOUR CASE", 6, 1, "SUITCASE 9U", 120.f},
    {"STUDIO FOAM", 12, 1, "STALKER 6U", 100.f},
    {"PERFORMER", LOOK_GRADIENT, 19, "PERFORMER 7U", 100.f},
    {"RETRO CRT", 8, 6, "BUS STEEL 7U", 84.f},
    {"CIRCUIT", 7, 0, "PRINTED PLA BLACK", 65.f},
    {"MAKER TEAL", LOOK_CASE_GRADIENT, 0, "PRINTED PETG TEAL", 50.f},
    {"MAKER ORANGE", LOOK_CASE_GRADIENT, 0, "PRINTED PLA ORANGE", 50.f},
    {"RED POD", LOOK_CASE_GRADIENT, 0, "POD RED", 50.f},
    {"BLUE POD", LOOK_CASE_GRADIENT, 0, "POD BLUE", 50.f},
    {"PURPLE POD", LOOK_CASE_GRADIENT, 0, "POD PURPLE", 50.f},
    {"NAVY STEEL", LOOK_GRADIENT, 8, "BRUSHED ALUMINUM", 84.f},
    {"OXBLOOD", LOOK_GRADIENT, 10, "EBONY SKIFF", 84.f},
    {"FOREST", LOOK_GRADIENT, 6, "OAK SKIFF", 84.f},
    {"CLEAN WHITE", LOOK_GRADIENT, 20, "FLATBOAT WHITE", 40.f},
    {"CARBON", 6, 0, "CARBON FIBER", 65.f},
    {"BIRCH STUDIO", LOOK_CASE_GRADIENT, 0, "BIRCH PLY STUDIO", 100.f},
    {"BLACK PLY", LOOK_GRADIENT, 0, "BLACK PLY STUDIO", 100.f},
    {"CHERRY STUDIO", LOOK_CASE_GRADIENT, 0, "CHERRY STUDIO", 100.f},
    {"WALNUT CONSOLE", LOOK_ART + 2, 0, "WALNUT CONSOLE", 120.f},
    {"OPEN OAK", LOOK_GRADIENT, 0, "OAK OPEN FRAME", 84.f},
    {"STAIRCASE", LOOK_CASE_GRADIENT, 0, "STEPPED WALNUT", 120.f},
    {"CURVED STUDIO", LOOK_ART + 1, 0, "CURVED CONSOLE", 100.f},
    {"LATTICE", LOOK_ART + 4, 0, "LATTICE PRINT", 84.f},
    // with the ART pictures
    {"NEBULA DREAM", LOOK_ART + 0, 0, "SMOKED ACRYLIC", 50.f},
    {"AURORA", LOOK_ART + 1, 0, "BLACK ANODIZED", 65.f},
    {"TOPO MAP", LOOK_ART + 2, 0, "WALNUT SKIFF", 84.f},
    {"SYNTHWAVE", LOOK_ART + 3, 0, "PRINTED PLA BLACK", 65.f},
    {"FLOW", LOOK_ART + 4, 0, "CARBON FIBER", 65.f},
    {"CRYSTAL CAVE", LOOK_ART + 5, 0, "BRUSHED ALUMINUM", 84.f},
    {"SCOPE", LOOK_ART + 6, 0, "BUS STEEL 7U", 84.f},
    {"GOLD MARBLE", LOOK_ART + 7, 0, "EBONY SKIFF", 84.f},
    {"PCB", LOOK_ART + 8, 0, "LAB EXTRUSION", 84.f},
    {"SPECTRUM", LOOK_ART + 9, 0, "PERFORMER 7U", 100.f},
    // PSYCHEDELIC combos (Nov 2026): Nick's abstract artwork, with neon, UV and blacklight cases
    {"NEON WILDSTYLE", LOOK_ART + 10, 0, "GRAFFITI ROAD CASE", 84.f},
    {"WILDSTYLE STAIRS", LOOK_ART + 10, 0, "STEPPED NEON", 100.f},
    {"LIQUID DREAM", LOOK_ART + 11, 0, "SMOKED VIOLET ACRYLIC", 50.f},
    {"LIQUID WALNUT", LOOK_ART + 11, 0, "WALNUT CONSOLE", 120.f},
    {"SACRED GEOMETRY", LOOK_ART + 12, 0, "BLACKLIGHT POD", 65.f},
    {"KALEIDO GOLD", LOOK_ART + 12, 0, "EBONY GOLD CONSOLE", 120.f},
    {"BIOLUMINESCENCE", LOOK_ART + 13, 0, "MYCELIUM LATTICE", 84.f},
    {"DEEP SEA GLOW", LOOK_ART + 13, 0, "SMOKED ACRYLIC", 50.f},
    {"MACHINE ELF", LOOK_ART + 14, 0, "FORGE STEEL", 100.f},
    {"FORGE STUDIO", LOOK_ART + 14, 0, "BLACK PLY STUDIO", 100.f},
    {"ESCHER", LOOK_ART + 15, 0, "TIERED WALNUT", 100.f},
    {"IMPOSSIBLE OAK", LOOK_ART + 15, 0, "STEPPED OAK", 120.f},
    {"BIOMORPH", LOOK_ART + 16, 0, "CARBON FIBER", 65.f},
    {"BIOMORPH CHROME", LOOK_ART + 16, 0, "BRUSHED ALUMINUM", 84.f},
    {"STICKER BOMB", LOOK_ART + 17, 0, "UV PRINT LIME", 65.f},
    {"STICKER SUITCASE", LOOK_ART + 17, 0, "SUITCASE 9U", 120.f},
    {"GLASS ORBS", LOOK_ART + 18, 0, "NEON ACRYLIC CYAN", 50.f},
    {"BUBBLE POD", LOOK_ART + 18, 0, "NEON ACRYLIC PINK", 50.f},
    {"ACID LATTICE", LOOK_ART + 3, 0, "ACID LATTICE", 84.f},
    {"IRIDESCENT", LOOK_ART + 0, 0, "IRIDESCENT ANODIZED", 65.f},
    {"CURVED VIOLET", LOOK_ART + 12, 0, "CURVED VIOLET", 100.f},
    {"MAGENTA PRINT", LOOK_ART + 10, 0, "UV PRINT MAGENTA", 65.f},
    {"BLACKLIGHT", LOOK_GRADIENT, 24, "BLACKLIGHT POD", 65.f},
    {"NEON PINK NIGHT", LOOK_GRADIENT, 25, "NEON ACRYLIC PINK", 50.f},
    {"ACID GREEN", LOOK_GRADIENT, 26, "UV PRINT LIME", 65.f},
    {"UV BLUE", LOOK_GRADIENT, 27, "NEON ACRYLIC CYAN", 50.f},
    // DREAM combos (round 6): the Midjourney-inspired art with matching cases and species-wood side panels
    {"MOTION WALNUT", LOOK_ART + 19, 0, "WALNUT CONSOLE", 120.f, SW_BLACK_WALNUT, F_OILED},
    {"MIRROR NEON", LOOK_ART + 20, 0, "STEPPED NEON", 84.f, 0, 0},
    {"IRIS WATCH", LOOK_ART + 21, 0, "BLACK ANODIZED", 65.f, SW_MACASSAR, F_GLOSS},
    {"TUNNEL VISION", LOOK_ART + 22, 0, "SMOKED ACRYLIC", 50.f, 0, 0},
    {"PRISM CASE", LOOK_ART + 23, 0, "CLEAR ACRYLIC", 50.f, SW_PURPLEHEART, F_GLOSS},
    {"BRONZE TEMPLE", LOOK_ART + 24, 0, "EBONY GOLD CONSOLE", 120.f, SW_BUBINGA, F_SATIN},
    {"FLOWER OF LIFE", LOOK_ART + 25, 0, "CURVED VIOLET", 100.f, 0, 0},
    {"GOLD FIBRE", LOOK_ART + 26, 0, "BLACK PLY STUDIO", 100.f, SW_WENGE, F_OILED},
    {"NEURAL VIOLET", LOOK_ART + 27, 0, "SMOKED VIOLET ACRYLIC", 50.f, SW_PURPLEHEART, F_SATIN},
    {"COSMIC KOA", LOOK_ART + 28, 0, "CURVED CONSOLE", 100.f, SW_KOA, F_GLOSS},
    {"CLOUD NINE", LOOK_ART + 29, 0, "POD PURPLE", 65.f, SW_QUILTED_MAPLE, F_GLOSS},
    {"INK AND MAPLE", LOOK_ART + 30, 0, "BIRCH PLY STUDIO", 84.f, SW_SPALTED, F_OILED},
    {"GLASS WORLD", LOOK_ART + 31, 0, "NEON ACRYLIC CYAN", 50.f, 0, 0},
    {"COPPER STAIRS", LOOK_ART + 32, 0, "STEPPED WALNUT", 120.f, SW_PADAUK, F_SATIN},
    {"TESSELLATE", LOOK_ART + 33, 0, "STEPPED OAK", 100.f, SW_QS_OAK, F_OILED},
    {"VIOLET RIDGE", LOOK_ART + 34, 0, "BLACKLIGHT POD", 65.f, SW_ZIRICOTE, F_SATIN},
    {"SNOW GLOBE", LOOK_ART + 35, 0, "POD BLUE", 65.f, SW_BIRDSEYE, F_GLOSS},
    {"MANDELBROT", LOOK_ART + 36, 0, "CARBON FIBER", 65.f, SW_SHOU_SUGI_BAN, F_OILED},
    {"JULIA", LOOK_ART + 37, 0, "NEON ACRYLIC PINK", 50.f, 0, 0},
    {"REEF", LOOK_ART + 38, 0, "MYCELIUM LATTICE", 84.f, 0, 0},
    {"LOTUS", LOOK_ART + 39, 0, "CHERRY STUDIO", 100.f, SW_CHERRY, F_GLOSS},
    {"NEON MARBLE", LOOK_ART + 40, 0, "GRAFFITI ROAD CASE", 84.f, SW_ZEBRAWOOD, F_GLOSS},
    {"MELTDOWN", LOOK_ART + 41, 0, "FORGE STEEL", 100.f, SW_COCOBOLO, F_OILED},
    {"THIRD EYE", LOOK_ART + 42, 0, "IRIDESCENT ANODIZED", 65.f, SW_AMBOYNA, F_GLOSS},
    {"RAINBOW SURF", LOOK_ART + 43, 0, "WEST COAST BOAT", 84.f, SW_CURLY_MAPLE, F_GLOSS},
    // DREAM II combos (round 6b)
    {"TESSERACT", LOOK_ART + 44, 0, "CARBON FIBER", 65.f, SW_MACASSAR, F_GLOSS},
    {"LAVA LOUNGE", LOOK_ART + 45, 0, "WALNUT CONSOLE", 120.f, SW_BLACK_WALNUT, F_GLOSS},
    {"OP POP", LOOK_ART + 46, 0, "POWDER COAT WHITE", 65.f, SW_HARD_MAPLE, F_SATIN},
    {"RING NEBULA", LOOK_ART + 47, 0, "BLACK ANODIZED", 84.f, SW_WENGE, F_OILED},
    {"AURORA HALO", LOOK_ART + 48, 0, "SMOKED ACRYLIC", 50.f, 0, 0},
    {"CRYSTAL GROTTO", LOOK_ART + 49, 0, "CLEAR ACRYLIC", 50.f, SW_BIRDSEYE, F_GLOSS},
    {"FERN GULLY", LOOK_ART + 50, 0, "OAK OPEN FRAME", 100.f, SW_SPALTED, F_OILED},
    {"STORM CELL", LOOK_ART + 51, 0, "BRUSHED ALUMINUM", 65.f, SW_SHOU_SUGI_BAN, F_OILED},
    {"DRAGON FOLD", LOOK_ART + 52, 0, "STEPPED NEON", 84.f, 0, 0},
    {"CATHEDRAL", LOOK_ART + 53, 0, "EBONY GOLD CONSOLE", 120.f, SW_PADAUK, F_GLOSS},
    {"CHROME DRIP", LOOK_ART + 54, 0, "STALKER 6U SILVER", 84.f, SW_ZEBRAWOOD, F_GLOSS},
    {"METATRON", LOOK_ART + 55, 0, "POD GOLD", 65.f, SW_BUBINGA, F_GLOSS},
    {"PENROSE", LOOK_ART + 56, 0, "CURVED VIOLET", 100.f, 0, 0},
    {"GLOW FOREST", LOOK_ART + 57, 0, "MYCELIUM LATTICE", 84.f, 0, 0},
    {"BUBBLE BATH", LOOK_ART + 58, 0, "POD BLUE", 65.f, SW_QUILTED_MAPLE, F_GLOSS},
    {"SHARD SIX", LOOK_ART + 59, 0, "IRIDESCENT ANODIZED", 65.f, SW_AMBOYNA, F_SATIN},
    {"RIPPLE POOL", LOOK_ART + 60, 0, "POWDER COAT TEAL", 65.f, SW_CLARO, F_OILED},
    {"ATTRACTOR", LOOK_ART + 61, 0, "BLACK PLY STUDIO", 100.f, SW_COCOBOLO, F_SATIN},
    {"SPIRO", LOOK_ART + 62, 0, "PRINTED PLA BLACK", 50.f, 0, 0},
    {"PLASMA TV", LOOK_ART + 63, 0, "ROAD CASE", 84.f, SW_LEOPARD, F_OILED},
    {"WHIRLPOOL", LOOK_ART + 64, 0, "SMOKED VIOLET ACRYLIC", 50.f, SW_PURPLEHEART, F_GLOSS},
    {"DEEP SEA", LOOK_ART + 65, 0, "BLACKLIGHT POD", 65.f, SW_ZIRICOTE, F_SATIN},
    {"NEON GRID CITY", LOOK_ART + 66, 0, "LAB EXTRUSION", 84.f, 0, 0},
    {"SEVENTIES", LOOK_ART + 67, 0, "TIERED WALNUT", 120.f, SW_KOA, F_GLOSS},
    {"GOLDEN SPIRAL", LOOK_ART + 68, 0, "CHERRY STUDIO", 100.f, SW_CURLY_MAPLE, F_GLOSS},
};
static const int kThemeN = (int) (sizeof(kThemes) / sizeof(kThemes[0]));
static void applyTheme(SynTHBloKBackdrop* m, int i) {
    i = ((i % kThemeN) + kThemeN) % kThemeN;
    const Theme& t = kThemes[i];
    m->look = t.look; m->colorIdx = t.color; m->depthMm = t.depthMm; m->showBg = true;
    m->sideShape = m->sideMat = m->sideWidth = m->openMode = 0;
    m->sideMat = clamp(t.sideMat, 0, kSideMatN - 1); m->sideFinish = clamp(t.sideFinish, 0, 2);
    const std::vector<CaseStyle>& lib = caseLibrary();
    for (size_t k = 0; k < lib.size(); k++) if (lib[k].name == t.caseName) m->caseIdx = (int) k;
    m->themeIdx = i;
}
// ---- MY IMAGES and PRESETS: two plain folders in Rack's user folder (Documents\Rack2\SynTHBloK\...), so images and
// looks can be dropped in from Explorer and stepped through on the panel. A preset is BACKDROP's settings as JSON
// (look, colour, image, case, sides, wood, finish), without the patch title.
static std::string myImagesDir() { std::string d = asset::user("SynTHBloK/backdrop-images"); system::createDirectories(d); return d; }
static std::string presetsDir() { std::string d = asset::user("SynTHBloK/backdrop-presets"); system::createDirectories(d); return d; }
static std::vector<std::string> listFiles(const std::string& dir, std::vector<std::string> exts) {
    std::vector<std::string> out;
    for (const std::string& p : system::getEntries(dir)) {
        std::string e = string::lowercase(system::getExtension(p));
        if (!e.empty() && e[0] == '.') e = e.substr(1);
        if (std::find(exts.begin(), exts.end(), e) != exts.end()) out.push_back(p);
    }
    std::sort(out.begin(), out.end());
    return out;
}
// the OLED redraws often: the folders are read at most once a second
static const std::vector<std::string>& cachedList(int which) {
    static std::vector<std::string> L[2]; static double at[2] = {-10.0, -10.0};
    double now = system::getTime();
    if (now - at[which] > 1.0) {
        at[which] = now;
        L[which] = which == 0 ? listFiles(myImagesDir(), {"png", "jpg", "jpeg", "bmp", "gif"}) : listFiles(presetsDir(), {"json"});
    }
    return L[which];
}
static std::vector<std::string> myImages() { return cachedList(0); }
static std::vector<std::string> myPresets() { return cachedList(1); }
static void useImage(SynTHBloKBackdrop* m, const std::string& path) {
    m->imagePath = path; m->showBg = true;
    m->fit = 1; m->imgScale = 1.f; m->imgW = m->imgH = 1.f; m->imgX = m->imgY = 0.f;   // snap it in, every time
}
static void stepIn(const std::vector<std::string>& L, const std::string& cur, int d, std::function<void(const std::string&)> use) {
    if (L.empty()) return;
    int i = -1;
    for (size_t k = 0; k < L.size(); k++) if (L[k] == cur) i = (int) k;
    int n = (int) L.size();
    i = i < 0 ? (d < 0 ? n - 1 : 0) : ((i + d) % n + n) % n;
    use(L[i]);
}
static void loadPreset(SynTHBloKBackdrop* m, const std::string& path) {
    json_error_t err;
    json_t* j = json_load_file(path.c_str(), 0, &err);
    if (!j) return;
    std::string keepTitle = m->title; bool keepShow = m->showTitle;   // the patch's own name stays
    m->dataFromJson(j);
    m->title = keepTitle; m->showTitle = keepShow; m->presetPath = path;
    json_decref(j);
}
static void savePresetAs(SynTHBloKBackdrop* m) {
    osdialog_filters* f = osdialog_filters_parse("BACKDROP preset:json");
    std::string def = m->presetPath.empty() ? "my look.json" : system::getFilename(m->presetPath);
    char* path = osdialog_file(OSDIALOG_SAVE, presetsDir().c_str(), def.c_str(), f);
    osdialog_filters_free(f);
    if (!path) return;
    std::string p = path; std::free(path);
    if (string::lowercase(system::getExtension(p)).find("json") == std::string::npos) p += ".json";
    json_t* j = m->dataToJson();
    json_object_del(j, "title"); json_object_del(j, "showTitle"); json_object_del(j, "preset");
    json_object_set_new(j, "backdropPreset", json_integer(1));
    if (json_dump_file(j, p.c_str(), JSON_INDENT(2)) == 0) m->presetPath = p;
    json_decref(j);
}
// ---- the OLED menu: the settings on the panel, so nobody has to dig in the right-click menu ----------------
// Tabs BG / CASE / NAME along the top; each row is a small label over its value. Click the left half of a row
// for the previous value, the right half for the next (on/off rows just flip), or scroll over it. Long values
// slide sideways. On NAME, click the name and type (Enter: done).
static const char* const kLookShort[kLookN] = {"PLAIN", "AMBER GRID", "OLED GLOW", "DIAMOND PLATE", "DEEP SPACE", "AMBER RAILS",
    "CARBON WEAVE", "CIRCUIT", "SCANLINES", "HEX MESH", "BLUEPRINT", "PEGBOARD", "FOAM", "BRUSHED STEEL", "SUNSET FADE", "HALFTONE", "BRICK", "WOOD SLATS", "GRADIENT", "CASE GRADIENT",
    "NEBULA", "AURORA", "TOPOGRAPHIC", "SYNTHWAVE", "FLOW FIELD", "CRYSTAL", "OSCILLOSCOPE", "GOLD MARBLE", "CIRCUIT BOARD", "SPECTRUM",
    "WILDSTYLE", "LIQUID", "KALEIDOSCOPE", "BIOLUMINESCENT", "MACHINE ELF", "ESCHER", "BIOMORPH", "STICKER BOMB", "GLASS SPHERES",
    "MOTION PAINT", "MIRROR SWIRL", "IRIS VORTEX", "TUNNEL", "PRISM SHARDS", "BRONZE MANDALA", "FLOWER OF LIFE", "GOLD FILAMENT",
    "VIOLET NEURAL", "COSMIC SWIRL", "DREAM CLOUDS", "INK BLOOM", "GLASS WORLD", "COPPER MAZE", "TESSELLATION", "VIOLET RIDGES",
    "SNOW GLOBES", "MANDELBROT", "JULIA BLOOM", "CORAL CELLS", "LOTUS", "NEON MARBLE", "MELT", "THIRD EYE", "RAINBOW SURF",
    "HYPERCUBE", "LAVA LAMP", "OP ART", "NEBULA EYE", "AURORA MANDALA", "CRYSTAL CAVE", "NEON FERNS", "LIGHTNING", "DRAGON CURVE", "STAINED GLASS", "CHROME DRIP", "METATRON", "PENROSE", "GLOW FOREST", "SOAP BUBBLES", "KALEIDO SHARDS", "RIPPLES", "ATTRACTOR", "SPIROGRAPH", "PLASMA", "VORTEX STREAMS", "JELLYFISH", "NEON LATTICE", "SUNBURST", "NAUTILUS"};
static const char* const kFitShort[4] = {"TILED", "FILL", "FIT", "CENTRED"};
static const float kDepthMm[9] = {0.f, 25.f, 40.f, 50.f, 65.f, 84.f, 100.f, 120.f, 150.f};
struct BackdropOled : widget::OpaqueWidget {
    SynTHBloKBackdrop* module = NULL;
    NameOled* name = NULL;
    int page = 0, hover = -1;
    static constexpr float kTabH = 10.f, kRowH = 16.f, kPad = 3.f;
    static const int kPages = 4;   // THEME, BG, CASE, NAME
    struct Row { std::string label, value; std::function<void(int)> step; bool flip; };
    std::vector<Row> rows() {
        std::vector<Row> r;
        SynTHBloKBackdrop* m = module;
        if (!m) return r;
        auto wrap = [](int v, int d, int n) { return ((v + d) % n + n) % n; };
        auto hand = [=]() { m->themeIdx = -1; };   // changed by hand: the theme shows CUSTOM
        const std::vector<CaseStyle>& clib = caseLibrary();
        int cn = (int) clib.size();
        int di = 0; for (int k = 0; k < 9; k++) if (std::fabs(kDepthMm[k] - m->depthMm) < std::fabs(kDepthMm[di] - m->depthMm)) di = k;
        if (page == 0) {   // THEME: pick one, then combine its parts as you like
            r.push_back({"THEME", m->themeIdx >= 0 && m->themeIdx < kThemeN ? kThemes[m->themeIdx].name : "CUSTOM",
                [=](int d) { applyTheme(m, (m->themeIdx >= 0 ? m->themeIdx : -1 + (d < 0 ? 1 : 0)) + d); }, false});
            {   // MY PRESETS: step through the presets folder (each step loads one); SAVE writes the current look
                std::vector<std::string> P = myPresets();
                std::string pv = !m->presetPath.empty() && system::exists(m->presetPath) ? string::uppercase(system::getStem(m->presetPath)) : P.empty() ? "NONE SAVED" : string::f("%d SAVED", (int) P.size());
                r.push_back({"MY PRESET", pv, [=](int d) { stepIn(myPresets(), m->presetPath, d, [=](const std::string& p) { loadPreset(m, p); }); }, false});
                r.push_back({"SAVE PRESET", "SAVE AS...", [=](int) { savePresetAs(m); }, true});
            }
            r.push_back({"LOOK", kLookShort[clamp(m->look, 0, kLookN - 1)], [=](int d) { m->look = wrap(m->look, d, kLookN); hand(); }, false});
            r.push_back({"COLOUR", string::uppercase(kColorName[clamp(m->colorIdx, 0, kColorN - 1)]), [=](int d) { m->colorIdx = wrap(m->colorIdx, d, kColorN); hand(); }, false});
            r.push_back({"CASE", clib[clamp(m->caseIdx, 0, cn - 1)].name, [=](int d) { m->caseIdx = wrap(m->caseIdx, d, cn); hand(); }, false});
            r.push_back({"DEPTH", m->depthMm < 1.f ? "FLAT" : string::f("%d MM", (int) std::round(m->depthMm)), [=](int d) { m->depthMm = kDepthMm[clamp(di + d, 0, 8)]; hand(); }, false});
        }
        else if (page == 1) {
            r.push_back({"BACKGROUND", m->showBg ? "ON" : "OFF", [=](int) { m->showBg = !m->showBg; }, true});
            r.push_back({"LOOK", kLookShort[clamp(m->look, 0, kLookN - 1)], [=](int d) { m->look = wrap(m->look, d, kLookN); hand(); }, false});
            r.push_back({"COLOUR", string::uppercase(kColorName[clamp(m->colorIdx, 0, kColorN - 1)]), [=](int d) { m->colorIdx = wrap(m->colorIdx, d, kColorN); hand(); }, false});
            r.push_back({"IMAGE", m->imagePath.empty() ? "LOAD..." : string::uppercase(system::getStem(m->imagePath)), [=](int d) {
                if (d < 0 && !m->imagePath.empty()) { m->imagePath.clear(); return; }   // left half: clear
                osdialog_filters* f = osdialog_filters_parse("Images:png,jpg,jpeg,bmp,gif");
                char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, f);
                osdialog_filters_free(f);
                if (path) { m->imagePath = path; std::free(path); m->fit = 1; m->imgScale = 1.f; m->imgW = m->imgH = 1.f; m->imgX = m->imgY = 0.f; }
            }, false});
            {   // MY IMAGES: step through the images folder; empty, a click opens the folder to drop some in
                std::vector<std::string> L = myImages();
                bool mine = !m->imagePath.empty() && system::getDirectory(m->imagePath) == system::getDirectory(myImagesDir() + "/x");
                std::string iv = L.empty() ? "OPEN FOLDER" : mine ? string::uppercase(system::getStem(m->imagePath)) : string::f("%d IMAGES", (int) L.size());
                r.push_back({"MY IMAGES", iv, [=](int d) {
                    std::vector<std::string> L2 = myImages();
                    if (L2.empty()) { system::openDirectory(myImagesDir()); return; }
                    stepIn(L2, m->imagePath, d, [=](const std::string& p) { useImage(m, p); });
                }, false});
            }
            if (!m->imagePath.empty()) {
                r.push_back({"IMAGE FIT", kFitShort[clamp(m->fit, 0, 3)], [=](int d) { m->fit = wrap(m->fit, d, 4); }, false});
                r.push_back({"DARKEN", string::f("%d%%", (int) std::round(m->imgDim * 100.f)), [=](int d) { m->imgDim = clamp(m->imgDim + d * 0.05f, 0.f, 0.9f); }, false});
            }
        }
        else if (page == 2) {   // CASE
            r.push_back({"CASE", clib[clamp(m->caseIdx, 0, cn - 1)].name, [=](int d) { m->caseIdx = wrap(m->caseIdx, d, cn); hand(); }, false});
            r.push_back({"DEPTH", m->depthMm < 1.f ? "FLAT" : string::f("%d MM", (int) std::round(m->depthMm)), [=](int d) { m->depthMm = kDepthMm[clamp(di + d, 0, 8)]; hand(); }, false});
            r.push_back({"SIDES", kSideShape[clamp(m->sideShape, 0, kSideShapeN - 1)], [=](int d) { m->sideShape = wrap(m->sideShape, d, kSideShapeN); hand(); }, false});
            r.push_back({"SIDE MATERIAL", kSideMat[clamp(m->sideMat, 0, kSideMatN - 1)].name, [=](int d) { m->sideMat = wrap(m->sideMat, d, kSideMatN); hand(); }, false});
            r.push_back({"SIDE WIDTH", kSideWidth[clamp(m->sideWidth, 0, 4)], [=](int d) { m->sideWidth = wrap(m->sideWidth, d, 5); hand(); }, false});
            r.push_back({"WOOD FINISH", kSideFinish[clamp(m->sideFinish, 0, 2)], [=](int d) { m->sideFinish = wrap(m->sideFinish, d, 3); }, false});
            r.push_back({"OPEN FRAME", kOpenName[clamp(m->openMode, 0, 2)], [=](int d) { m->openMode = wrap(m->openMode, d, 3); hand(); }, false});
            r.push_back({"INFO STRIP", kOpenName[clamp(m->infoMode, 0, 2)], [=](int d) { m->infoMode = wrap(m->infoMode, d, 3); }, false});
            r.push_back({"BG MATCHES CASE", m->look == LOOK_CASE_GRADIENT ? "ON" : "OFF", [=](int) { m->look = m->look == LOOK_CASE_GRADIENT ? LOOK_GRADIENT : LOOK_CASE_GRADIENT; hand(); }, true});
            r.push_back({"SHADOW", m->shadow ? "ON" : "OFF", [=](int) { m->shadow = !m->shadow; }, true});
            r.push_back({"RESET CASE", "TO ITS OWN", [=](int) { m->sideShape = m->sideMat = m->sideWidth = m->openMode = m->infoMode = 0; m->depthMm = 65.f; m->shadow = true; }, true});
        }
        else {
            r.push_back({"PATCH NAME", "", nullptr, false});   // the name field sits in this row
            r.push_back({"SHOW ON RACK", m->showTitle ? "ON" : "OFF", [=](int) { m->showTitle = !m->showTitle; }, true});
            r.push_back({"USE FILE NAME", "CLEAR", [=](int) { m->title.clear(); }, true});
        }
        return r;
    }
    float rowY(int i) { return kTabH + 3.f + i * kRowH; }
    float tabW(int k) { static const float wt[kPages] = {5.2f, 3.2f, 4.4f, 4.6f}; return box.size.x * wt[k] / 17.4f; }
    int rowAt(Vec p) { if (p.y < kTabH + 3.f) return -1; int i = (int) ((p.y - kTabH - 3.f) / kRowH); return i; }
    void step() override {
        if (name) {   // the name field lives in the first NAME row
            name->visible = page == 3;
            name->box.pos = box.pos.plus(Vec(kPad, rowY(0) + 6.f));
            name->box.size = Vec(box.size.x - 2.f * kPad, kRowH - 7.f);
        }
        OpaqueWidget::step();
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS) { OpaqueWidget::onButton(e); return; }
        e.consume(this);
        if (e.pos.y < kTabH) { float x = 0.f; for (int k = 0; k < kPages; k++) { x += tabW(k); if (e.pos.x < x) { page = k; break; } } return; }
        std::vector<Row> r = rows();
        int i = rowAt(e.pos);
        if (i < 0 || i >= (int) r.size() || !r[i].step) return;
        r[i].step(e.pos.x < box.size.x * 0.5f && !r[i].flip ? -1 : 1);
    }
    void onHover(const HoverEvent& e) override { hover = rowAt(e.pos); OpaqueWidget::onHover(e); }
    void onLeave(const LeaveEvent& e) override { hover = -1; }
    void onHoverScroll(const HoverScrollEvent& e) override {
        std::vector<Row> r = rows();
        int i = rowAt(e.pos);
        if (i < 0 || i >= (int) r.size() || !r[i].step) return;
        if (r[i].label == "IMAGE") return;   // no file dialogs from a scroll wheel
        r[i].step(e.scrollDelta.y > 0 ? -1 : 1);
        e.consume(this);
    }
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 1.5f);
        nvgFillColor(args.vg, nvgRGB(10, 7, 3));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, oled::outline(70));
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        NVGcontext* vg = args.vg;
        nvgFontFaceId(vg, font->handle);
        float W = box.size.x;
        // tabs
        static const char* const tabs[kPages] = {"THEME", "BG", "CASE", "NAME"};
        nvgFontSize(vg, 5.f);
        nvgTextLetterSpacing(vg, 0.2f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        float tx0 = 0.f;
        for (int k = 0; k < kPages; k++) {
            float tw = tabW(k);
            if (k == page) {
                nvgBeginPath(vg); nvgRect(vg, tx0 + 0.6f, 1.f, tw - 1.2f, kTabH - 2.f);
                nvgFillColor(vg, oled::amber()); nvgFill(vg);
            }
            nvgFillColor(vg, k == page ? oled::ink() : oled::dim());
            nvgText(vg, tx0 + tw * 0.5f + 0.1f, kTabH * 0.5f + 0.5f, tabs[k], NULL);
            tx0 += tw;
        }
        nvgBeginPath(vg); nvgMoveTo(vg, 1.f, kTabH + 0.5f); nvgLineTo(vg, W - 1.f, kTabH + 0.5f);
        nvgStrokeColor(vg, oled::outline(90)); nvgStrokeWidth(vg, 0.6f); nvgStroke(vg);
        // rows
        std::vector<Row> r = rows();
        float iw = W - 2.f * kPad;
        for (int i = 0; i < (int) r.size(); i++) {
            float y = rowY(i);
            if (y + kRowH > box.size.y) break;
            bool hot = i == hover && r[i].step;
            if (hot) {
                nvgBeginPath(vg); nvgRect(vg, 1.f, y, W - 2.f, kRowH - 1.f);
                nvgFillColor(vg, oled::amber(22)); nvgFill(vg);
            }
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgFontSize(vg, 4.8f);
            nvgTextLetterSpacing(vg, 0.4f);
            nvgFillColor(vg, oled::dim());
            nvgText(vg, kPad, y + 1.2f, r[i].label.c_str(), NULL);
            if (page == 3 && i == 0) continue;   // the name field draws itself
            // the value, with arrows when it steps; slides sideways when it's too long and hovered
            bool arrows = r[i].step && !r[i].flip;
            float vx = kPad + (arrows ? 6.f : 0.f), vw = iw - (arrows ? 12.f : 0.f);
            float bw = 0.f, off = 0.f;
            for (float fsz : {6.8f, 5.8f, 5.f}) {   // three fixed sizes only (nothing re-rasterised per frame)
                nvgFontSize(vg, fsz);
                nvgTextLetterSpacing(vg, fsz > 6.f ? 0.2f : 0.f);
                bw = nvgTextBounds(vg, 0, 0, r[i].value.c_str(), NULL, NULL);
                if (bw <= vw) break;
            }
            if (bw > vw && hot) { float span = bw - vw + 8.f; off = std::fmod((float) system::getTime() * 18.f, span * 2.f); if (off > span) off = 2.f * span - off; }
            nvgSave(vg);
            nvgScissor(vg, vx, y, vw, kRowH);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, oled::amber());
            float tx = bw <= vw ? vx + (vw - bw) * 0.5f : vx - off;
            nvgText(vg, tx, y + 10.6f, r[i].value.c_str(), NULL);
            nvgRestore(vg);
            if (arrows) {
                nvgFillColor(vg, hot ? oled::amber() : oled::outline(140));
                for (int sd = 0; sd < 2; sd++) {
                    float ax = sd ? W - kPad - 1.f : kPad + 1.f, ay = y + 10.6f, d = sd ? -1.f : 1.f;
                    nvgBeginPath(vg); nvgMoveTo(vg, ax, ay); nvgLineTo(vg, ax + d * 3.f, ay - 2.2f); nvgLineTo(vg, ax + d * 3.f, ay + 2.2f); nvgClosePath(vg); nvgFill(vg);
                }
            }
        }
        nvgTextLetterSpacing(vg, 0.f);
    }
};

// NAME: a square key: opens the NAME page and starts the typing (on release, so the click doesn't take it back)
struct NameButton : widget::OpaqueWidget {
    NameOled* field = NULL;
    BackdropOled* menu = NULL;
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) e.consume(this);
    }
    void onDragEnd(const DragEndEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT || !field) return;
        if (menu) { menu->page = 3; menu->step(); }
        field->visible = true;
        APP->event->setSelectedWidget(field);
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 2.2f);   // square, softly rounded corners
        nvgFillColor(args.vg, nvgRGB(24, 18, 12));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, oled::outline(110));
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 6.5f);
        nvgTextLetterSpacing(args.vg, 0.8f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, oled::amber());
        nvgText(args.vg, box.size.x * 0.5f + 0.4f, box.size.y * 0.5f + 0.5f, "NAME", NULL);
        nvgTextLetterSpacing(args.vg, 0.f);
    }
};

// ---- a picture of the Rack window, for the case snapshots (Rack keeps its own screenshot call internal) --------
// Reads what is on screen with OpenGL (looked up in opengl32.dll, which Rack has loaded) and writes a 24-bit BMP.
#ifdef ARCH_WIN
extern "C" __declspec(dllimport) void* __stdcall GetModuleHandleA(const char* name);
extern "C" __declspec(dllimport) void* __stdcall GetProcAddress(void* module, const char* name);
static bool snapWindow(const std::string& path) {
    typedef void (__stdcall *ReadPixels)(int, int, int, int, unsigned, unsigned, void*);
    typedef void (__stdcall *ReadBuffer)(unsigned);
    typedef void (__stdcall *GetIntegerv)(unsigned, int*);
    void* gl = GetModuleHandleA("opengl32.dll");
    if (!gl) return false;
    ReadPixels readPixels = (ReadPixels) GetProcAddress(gl, "glReadPixels");
    ReadBuffer readBuffer = (ReadBuffer) GetProcAddress(gl, "glReadBuffer");
    GetIntegerv getIntegerv = (GetIntegerv) GetProcAddress(gl, "glGetIntegerv");
    if (!readPixels || !readBuffer || !getIntegerv) return false;
    int vp[4] = {0, 0, 0, 0};
    getIntegerv(0x0BA2, vp);   // GL_VIEWPORT: Rack's last frame covers the whole window
    int w = vp[2], h = vp[3];
    if (w <= 0 || h <= 0) return false;
    std::vector<unsigned char> px((size_t) w * h * 3);
    readBuffer(0x0404);   // GL_FRONT: what is on screen
    readPixels(0, 0, w, h, 0x80E0, 0x1401, px.data());   // GL_BGR, GL_UNSIGNED_BYTE (rows bottom-up, as a BMP keeps them)
    readBuffer(0x0405);   // back to GL_BACK
    int row = (w * 3 + 3) & ~3;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    unsigned size = 54 + (unsigned) row * h;
    unsigned char hd[54] = {'B', 'M'};
    auto put = [&](int at, unsigned v) { for (int k = 0; k < 4; k++) hd[at + k] = (v >> (8 * k)) & 255; };
    put(2, size); put(10, 54); put(14, 40); put(18, (unsigned) w); put(22, (unsigned) h);
    hd[26] = 1; hd[28] = 24; put(34, (unsigned) row * h);
    std::fwrite(hd, 1, 54, f);
    std::vector<unsigned char> line(row, 0);
    for (int y = 0; y < h; y++) {
        std::memcpy(line.data(), px.data() + (size_t) y * w * 3, (size_t) w * 3);
        std::fwrite(line.data(), 1, row, f);
    }
    std::fclose(f);
    return true;
}
#else
static bool snapWindow(const std::string& path) { return false; }
#endif

// the header, as on every MTX module (EchoTitle): SynTHBloK in white, the module's name in orange
struct BackdropTitle : TransparentWidget {
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
        nvgText(args.vg, 0.f, mm2px(6.4f), "BACKDROP", NULL);
        nvgTextLetterSpacing(args.vg, 0.f);
    }
};

struct SynTHBloKBackdropWidget : ModuleWidget {
    SynTHBloKBackdropWidget(SynTHBloKBackdrop* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/SynTHBloKBackdrop.svg")));
        BackdropPanel* p = new BackdropPanel;
        p->box.size = box.size;
        p->module = module;
        p->widget = this;
        addChild(p);
        addChild(createWidget<BackdropTitle>(mm2px(Vec(8.5f, 0.f))));
        addInput(createInputCentered<BlackJack>(mm2px(Vec(10.2f, 117.6f)), module, SynTHBloKBackdrop::IN_L));   // the meter
        addInput(createInputCentered<BlackJack>(mm2px(Vec(20.28f, 117.6f)), module, SynTHBloKBackdrop::IN_R));
        BackdropOled* om = new BackdropOled;   // the OLED menu fills the well; the name field rides in its NAME page
        om->module = module;
        om->box.pos = mm2px(Vec(5.6f, 13.6f));
        om->box.size = mm2px(Vec(19.28f, 81.f));
        addChild(om);
        NameOled* nf = new NameOled;
        nf->module = module;
        nf->visible = false;
        om->name = nf;
        addChild(nf);   // after the menu, so it gets the clicks in its row
        NameButton* nb = new NameButton;
        nb->field = nf;
        nb->menu = om;
        nb->box.size = mm2px(Vec(7.6f, 7.0f));   // nearly square
        nb->box.pos = Vec(box.size.x * 0.5f - nb->box.size.x * 0.5f, mm2px(97.3f));
        addChild(nb);
        addChild(createWidget<SocketScrew>(Vec(RACK_GRID_WIDTH * 0.5f, 0)));
        addChild(createWidget<SocketScrew>(Vec(box.size.x - RACK_GRID_WIDTH * 1.5f, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    }
    // CASE SNAPSHOTS (a dev aid): if Rack's user folder holds SynTHBloK/snap_cases.txt when this BACKDROP opens, it
    // steps through every case and saves a picture of the Rack window for each to SynTHBloK/case_shots/
    // (NN_NAME.bmp), then puts the case back and writes done.txt. The trigger file is removed first, so it runs once.
    int shotI = -1, shotWait = 0, shotKeep = 0, nowWait = 0;
    bool shotChecked = false;
    std::string shotDir;
    void shotStep(SynTHBloKBackdrop* m) {
        // runs only when the trigger file is there (it never ships; only the dev workflow writes it)
        if (!shotChecked) {
            shotChecked = true;
            if (system::exists(asset::user("SynTHBloK/snap_now.txt"))) { system::remove(asset::user("SynTHBloK/snap_now.txt")); nowWait = 1; APP->scene->rackScroll->zoomToModules(); }
            std::string trig = asset::user("SynTHBloK/snap_cases.txt");
            INFO("BACKDROP snapshots: looking for %s: %s", trig.c_str(), system::exists(trig) ? "found" : "none");
            if (system::exists(trig)) {
                system::remove(trig);
                shotDir = asset::user("SynTHBloK/case_shots");
                system::createDirectories(shotDir);
                shotKeep = m->caseIdx;
                shotI = 0; shotWait = -240;   // ~4 s for the patch to settle first
                APP->scene->rackScroll->zoomToModules();
            }
        }
        if (nowWait == 170) APP->scene->rackScroll->zoomToModules();
        if (nowWait > 0 && ++nowWait > 180) { snapWindow(asset::user("SynTHBloK/rack_now.bmp")); nowWait = -1; }
        if (shotI < 0) return;
        const std::vector<CaseStyle>& lib = caseLibrary();
        if (shotWait == 0) { m->caseIdx = shotI; APP->scene->rackScroll->zoomToModules(); }   // frame the modules for every shot
        if (++shotWait < 14) return;   // a few frames on screen before the picture
        std::string n = lib[shotI].name;
        for (char& c : n) if (c == ' ') c = '_';
        if (!snapWindow(system::join(shotDir, string::f("%02d_%s.bmp", shotI, n.c_str())))) WARN("BACKDROP snapshot %d failed", shotI);
        shotWait = 0;
        if (++shotI >= (int) lib.size()) {
            shotI = -1;
            m->caseIdx = shotKeep;
            if (FILE* f = std::fopen(system::join(shotDir, "done.txt").c_str(), "w")) { std::fprintf(f, "%d\n", (int) lib.size()); std::fclose(f); }
        }
    }
    ~SynTHBloKBackdropWidget() {
        if (gOwner == this && gLayer) {
            if (gLayer->parent) gLayer->parent->removeChild(gLayer);
            delete gLayer;
            gLayer = NULL;
            gOwner = NULL;
        }
    }
    void step() override {
        ModuleWidget::step();
        SynTHBloKBackdrop* m = getModule<SynTHBloKBackdrop>();
        if (!m) return;
        // take the rack if no BACKDROP has it: the layer goes just above Rack's rails (its first child)
        if (!gOwner && APP->scene && APP->scene->rack && !APP->scene->rack->children.empty()) {
            gLayer = new BackdropLayer;
            gLayer->module = m;
            APP->scene->rack->addChildAbove(gLayer, APP->scene->rack->children.front());
            gOwner = this;
        }
        if (gOwner == this) shotStep(m);
    }

    void appendContextMenu(Menu* menu) override {
        SynTHBloKBackdrop* m = getModule<SynTHBloKBackdrop>();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("BACKGROUND"));
        menu->addChild(createBoolPtrMenuItem("Draw the background", "", &m->showBg));
        menu->addChild(createIndexSubmenuItem("Look", std::vector<std::string>(kLookName, kLookName + kLookN),
            [=]() { return (size_t) m->look; }, [=](size_t i) { m->look = (int) i; }));
        menu->addChild(createIndexSubmenuItem("Colour", std::vector<std::string>(kColorName, kColorName + kColorN),
            [=]() { return (size_t) m->colorIdx; }, [=](size_t i) { m->colorIdx = (int) i; }));
        menu->addChild(createSubmenuItem("My images", string::f("%d", (int) myImages().size()), [=](Menu* sub) {
            std::vector<std::string> L = myImages();
            if (L.empty()) sub->addChild(createMenuLabel("(none yet: drop images in the folder)"));
            for (const std::string& p : L)
                sub->addChild(createCheckMenuItem(system::getStem(p), "", [=]() { return m->imagePath == p; }, [=]() { useImage(m, p); m->themeIdx = -1; }));
            sub->addChild(new MenuSeparator);
            sub->addChild(createMenuItem("Open my images folder", "", [=]() { system::openDirectory(myImagesDir()); }));
        }));
        menu->addChild(createSubmenuItem("My presets", m->presetPath.empty() ? "" : system::getStem(m->presetPath), [=](Menu* sub) {
            sub->addChild(createMenuItem("Save the current look as a preset...", "", [=]() { savePresetAs(m); }));
            std::vector<std::string> P = myPresets();
            if (!P.empty()) sub->addChild(new MenuSeparator);
            for (const std::string& p : P)
                sub->addChild(createCheckMenuItem(system::getStem(p), "", [=]() { return m->presetPath == p; }, [=]() { loadPreset(m, p); }));
            sub->addChild(new MenuSeparator);
            sub->addChild(createMenuItem("Load a preset from a file...", "", [=]() {
                osdialog_filters* f = osdialog_filters_parse("BACKDROP preset:json");
                char* path = osdialog_file(OSDIALOG_OPEN, presetsDir().c_str(), NULL, f);
                osdialog_filters_free(f);
                if (path) { loadPreset(m, path); std::free(path); }
            }));
            sub->addChild(createMenuItem("Open my presets folder", "", [=]() { system::openDirectory(presetsDir()); }));
        }));
        menu->addChild(createMenuItem("Load an image...", m->imagePath.empty() ? "" : system::getFilename(m->imagePath), [=]() {
            osdialog_filters* f = osdialog_filters_parse("Images:png,jpg,jpeg,bmp,gif");
            char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, f);
            osdialog_filters_free(f);
            if (path) {
                m->imagePath = path; std::free(path);
                m->fit = 1; m->imgScale = 1.f; m->imgW = m->imgH = 1.f; m->imgX = m->imgY = 0.f;   // snap it in, every time
            }
        }));
        if (!m->imagePath.empty()) {
            menu->addChild(createIndexSubmenuItem("Image placement", std::vector<std::string>(kFitName, kFitName + 4),
                [=]() { return (size_t) m->fit; }, [=](size_t i) { m->fit = (int) i; }));
            menu->addChild(pctSlider("Darken", &m->imgDim, 0.f, 0.9f, 0.f));
            menu->addChild(createMenuItem("Snap to fit", "", [=]() { m->fit = 1; m->imgScale = 1.f; m->imgW = m->imgH = 1.f; m->imgX = m->imgY = 0.f; }));
            // optional fine-tuning, out of the way: live sliders (double-click one for its default)
            menu->addChild(createSubmenuItem("Fine-tune (optional)", "", [=](Menu* sub) {
                sub->addChild(pctSlider("Scale (tiled / centred)", &m->imgScale, 0.1f, 8.f, 1.f));
                sub->addChild(pctSlider("Width", &m->imgW, 0.25f, 4.f, 1.f));
                sub->addChild(pctSlider("Height", &m->imgH, 0.25f, 4.f, 1.f));
                sub->addChild(pctSlider("X offset", &m->imgX, -1.f, 1.f, 0.f));
                sub->addChild(pctSlider("Y offset", &m->imgY, -1.f, 1.f, 0.f));
            }));
            menu->addChild(createMenuItem("Clear the image", "", [=]() { m->imagePath.clear(); }));
        }
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("CASE"));
        menu->addChild(createSubmenuItem("Case", caseLibrary()[clamp(m->caseIdx, 0, (int) caseLibrary().size() - 1)].name, [=](Menu* sub) {
            const std::vector<CaseStyle>& lib = caseLibrary();
            for (size_t i = 0; i < lib.size(); i++)
                sub->addChild(createCheckMenuItem(lib[i].name, "", [=]() { return m->caseIdx == (int) i; }, [=]() { m->caseIdx = (int) i; }));
        }));
        menu->addChild(createIndexSubmenuItem("Side panels", std::vector<std::string>(kSideShape, kSideShape + kSideShapeN),
            [=]() { return (size_t) m->sideShape; }, [=](size_t i) { m->sideShape = (int) i; m->themeIdx = -1; }));
        menu->addChild(createSubmenuItem("Side material", kSideMat[clamp(m->sideMat, 0, kSideMatN - 1)].name, [=](Menu* sub) {
            for (int i = 0; i < kSideMatN; i++) {
                if (i && kSideMat[i].tex && !kSideMat[i - 1].tex) { sub->addChild(new MenuSeparator); sub->addChild(createMenuLabel("Species woods")); }
                sub->addChild(createCheckMenuItem(kSideMat[i].name, "", [=]() { return m->sideMat == i; }, [=]() { m->sideMat = i; m->themeIdx = -1; }));
            }
        }));
        menu->addChild(createIndexSubmenuItem("Wood finish (species woods)", std::vector<std::string>(kSideFinish, kSideFinish + 3),
            [=]() { return (size_t) m->sideFinish; }, [=](size_t i) { m->sideFinish = (int) i; }));
        menu->addChild(createIndexSubmenuItem("Side width", std::vector<std::string>(kSideWidth, kSideWidth + 5),
            [=]() { return (size_t) m->sideWidth; }, [=](size_t i) { m->sideWidth = (int) i; }));
        menu->addChild(createIndexSubmenuItem("Info strip (readouts + level meter)", std::vector<std::string>(kOpenName, kOpenName + 3),
            [=]() { return (size_t) m->infoMode; }, [=](size_t i) { m->infoMode = (int) i; }));
        menu->addChild(createIndexSubmenuItem("Open frame", std::vector<std::string>(kOpenName, kOpenName + 3),
            [=]() { return (size_t) m->openMode; }, [=](size_t i) { m->openMode = (int) i; }));
        menu->addChild(createSubmenuItem("Case depth", m->depthMm < 1.f ? "flat" : string::f("%d mm", (int) std::round(m->depthMm)), [=](Menu* sub) {
            // the depths real cases come in (the room behind the rails for the modules)
            static const float mm[9] = {0.f, 25.f, 40.f, 50.f, 65.f, 84.f, 100.f, 120.f, 150.f};
            static const char* const what[9] = {"flat (no side)", "slim skiff", "skiff", "shallow pod", "desktop case", "standard case", "deep case", "studio deep", "monster cabinet"};
            for (int k = 0; k < 9; k++) {
                float v = mm[k];
                sub->addChild(createCheckMenuItem(k ? string::f("%d mm  %s", (int) v, what[k]) : std::string(what[k]), "",
                    [=]() { return std::fabs(m->depthMm - v) < 0.5f; }, [=]() { m->depthMm = v; }));
            }
        }));
        menu->addChild(createBoolPtrMenuItem("Drop shadow", "", &m->shadow));
        menu->addChild(createMenuItem("Reset the case options", "sides, depth, shadow", [=]() { m->sideShape = m->sideMat = m->sideWidth = m->openMode = 0; m->depthMm = 65.f; m->shadow = true; }));
        menu->addChild(createMenuItem("Open the case library folder", "add your own cases in cases.json", [=]() {
            std::string dir = asset::user("SynTHBloK");
            system::createDirectories(dir);
            system::openDirectory(dir);
        }));
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("TITLE"));
        menu->addChild(createBoolPtrMenuItem("Show the patch title", "", &m->showTitle));
        menu->addChild(createSubmenuItem("Title text", m->title.empty() ? "the patch name" : m->title, [=](Menu* sub) {
            ui::TextField* tf = new ui::TextField;
            tf->box.size.x = 220.f;
            tf->placeholder = "the patch name";
            tf->setText(m->title);
            struct Apply : ui::MenuItem {
                SynTHBloKBackdrop* m; ui::TextField* tf;
                void onAction(const ActionEvent& e) override { m->title = tf->getText(); nameThePatch(m->title); }
            };
            sub->addChild(tf);
            Apply* a = new Apply; a->m = m; a->tf = tf; a->text = "Set";
            sub->addChild(a);
            sub->addChild(createMenuItem("Use the patch name", "", [=]() { m->title.clear(); }));
        }));
    }
};

Model* modelSynTHBloKBackdrop = createModel<SynTHBloKBackdrop, SynTHBloKBackdropWidget>("Backdrop");
