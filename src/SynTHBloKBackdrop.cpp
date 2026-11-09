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
    const char* bodyTex;   // CASE MATERIAL: the whole frame in a species wood or a man-made material (NULL: its own)
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
// SIDE PANELS (attachments): separate solid panels screwed to the OUTSIDE of the case ends (the screws are on their
// inner side, out of sight), in a species wood or a man-made material. Also what CASE MATERIAL builds a frame in.
// New materials go at the END (patches save them by number). 0 = none.
struct PanelMat { const char* name; const char* tex; int r, g, b; };
static const PanelMat kPanelMat[] = {
    {"NONE", NULL, 0, 0, 0},
    {"BLACK WALNUT", "wood_black_walnut", 81, 54, 36}, {"CLARO WALNUT", "wood_claro_walnut", 90, 62, 35}, {"QUARTERSAWN OAK", "wood_white_oak_qs", 189, 152, 100},
    {"AMERICAN CHERRY", "wood_cherry", 158, 77, 40}, {"HARD MAPLE", "wood_hard_maple", 231, 208, 166}, {"BALTIC BIRCH PLY", "wood_birch_ply", 225, 199, 155},
    {"CURLY MAPLE", "wood_curly_maple", 221, 205, 165}, {"QUILTED MAPLE", "wood_quilted_maple", 219, 204, 166}, {"BIRDSEYE MAPLE", "wood_birdseye_maple", 231, 209, 166},
    {"SPALTED MAPLE", "wood_spalted_maple", 179, 161, 128}, {"WATERFALL BUBINGA", "wood_waterfall_bubinga", 140, 65, 43}, {"RIBBON SAPELE", "wood_ribbon_sapele", 124, 61, 32},
    {"ZEBRAWOOD", "wood_zebrawood", 179, 156, 115}, {"PADAUK", "wood_padauk", 200, 57, 23}, {"PURPLEHEART", "wood_purpleheart", 112, 39, 101},
    {"COCOBOLO", "wood_cocobolo", 155, 71, 24}, {"WENGE", "wood_wenge", 55, 40, 29}, {"MACASSAR EBONY", "wood_macassar_ebony", 41, 28, 16},
    {"ZIRICOTE", "wood_ziricote", 56, 43, 30}, {"BOCOTE", "wood_bocote", 137, 103, 52}, {"KOA", "wood_koa", 160, 106, 48},
    {"LEOPARDWOOD", "wood_leopardwood", 163, 96, 55}, {"AMBOYNA BURL", "wood_amboyna_burl", 176, 97, 40}, {"SHOU SUGI BAN", "wood_shou_sugi_ban", 37, 32, 27},
    // man-made
    {"DIAMOND PLATE", "mat_diamond_plate", 152, 153, 158}, {"CARBON FIBER", "mat_carbon_fiber", 36, 37, 39}, {"3D PRINTED PLA", "mat_printed_pla", 214, 94, 23},
    {"BRUSHED ALUMINUM", "mat_brushed_alu", 171, 173, 178}, {"NEON ACRYLIC", "mat_neon_acrylic", 216, 36, 146}, {"TOLEX", "mat_tolex", 36, 36, 38},
    // NoiZee ToyZ side plates (Nov 2026): the BLANK panels' looks (and the matching ears'), on the modules' dark face
    {"NOIZEE BADGE", NULL, 26, 26, 29}, {"NOIZEE BACKLIT", NULL, 16, 16, 17}, {"NOIZEE CIRCUIT", NULL, 12, 14, 14},
};
static const int kPanelMatN = (int) (sizeof(kPanelMat) / sizeof(kPanelMat[0]));
static const int kPanelManMade = 25;   // the first man-made entry (menus put a heading there)
static const int kPanelNoizee = 31;     // the first NoiZee ToyZ plate; each draws the art of an ear (kEarNoizee + 1, 3, 4)
static inline int noizeePanelArt(int mat) { return mat == kPanelNoizee ? 11 : mat == kPanelNoizee + 1 ? 13 : mat == kPanelNoizee + 2 ? 14 : 0; }
static int woodToPanel(int sw) { return sw >= SW_BLACK_WALNUT && sw <= SW_SHOU_SUGI_BAN ? sw - SW_BLACK_WALNUT + 1 : 0; }
static const char* const kPanelShape[] = {"SEMI-ROUNDED", "ROUNDED", "SQUARE"};   // shown SQUARE / SEMI / ROUND
static const float kPanelRadius[] = {6.f, 16.f, 2.f};
static const int kPanelShapeOrder[] = {2, 0, 1};
static const char* const kPanelWidth[] = {"SLIM", "NORMAL", "WIDE"};
static const float kPanelWidthPx[] = {36.f, 52.f, 70.f};
// RACK EARS: L-shaped metal brackets, a 1/4" lip down each case end, then a flat plate bent outward with two oval
// mounting slots (how a case is screwed into a rack). Plain by default; the NOIZEE TOYZ ear matches the rig (black
// anodized, the orange rule of the MTX panels) and can carry the name, which is optional. New ones go at the END.
struct EarFin { const char* name; int r, g, b; float brushed; bool darkText; };
static const EarFin kEarFin[] = {
    {"NONE", 0, 0, 0, 0.f, false},
    {"RAW ALUMINUM", 178, 182, 188, 0.9f, true}, {"BLACK ANODIZED", 34, 34, 38, 0.45f, false}, {"GOLD", 206, 164, 72, 0.9f, true},
    {"COPPER", 184, 100, 62, 0.9f, false}, {"BRONZE", 140, 104, 58, 0.9f, false}, {"GREEN ANODIZED", 36, 120, 72, 0.45f, false},
    {"PRIMARY RED", 196, 28, 30, 0.45f, false}, {"PRIMARY BLUE", 24, 72, 180, 0.45f, false}, {"PRIMARY YELLOW", 232, 190, 24, 0.45f, true},
    {"NOIZEE TOYZ", 34, 34, 38, 0.45f, false},
    // more NoiZee ToyZ ears (Nov 2026): the logo badge, an engraved plate, backlit slots, the logo's circuit line
    {"NOIZEE BADGE", 26, 26, 29, 0.45f, false}, {"NOIZEE ENGRAVED", 160, 163, 168, 0.9f, true},
    {"NOIZEE BACKLIT", 16, 16, 17, 0.3f, false}, {"NOIZEE CIRCUIT", 12, 14, 14, 0.3f, false},
};
static const int kEarFinN = (int) (sizeof(kEarFin) / sizeof(kEarFin[0]));
static const int kEarNoizee = 10;   // the first NoiZee ToyZ ear; every ear from here on is one (isNoizee)
static inline bool isNoizee(int k) { return k >= kEarNoizee; }
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
// DISPLAY ORDER (Nov 2026): every list shows darkest to lightest, NoiZee ToyZ builds first (tools/make_backdrop_order.py
// writes the table). The stored numbers never change, so patches keep their looks; only what the OLED and menus show moves.
#include "BackdropOrder.hpp"
static_assert(sizeof(kOrderLook) / sizeof(int) == kLookN && sizeof(kOrderColor) / sizeof(int) == kColorN, "re-run make_backdrop_order.py");
// the order to show a list of n in: the table where it covers it, the rest (cases from cases.json) after, in file order
static std::vector<int> showOrder(const int* o, int on, int n) {
    std::vector<int> v; std::vector<bool> seen(n, false);
    for (int i = 0; i < on; i++) if (o[i] < n && !seen[o[i]]) { v.push_back(o[i]); seen[o[i]] = true; }
    for (int i = 0; i < n; i++) if (!seen[i]) v.push_back(i);
    return v;
}
#define SHOW_ORDER(tab, n) showOrder(tab, (int) (sizeof(tab) / sizeof(int)), (n))

} // namespace

struct SynTHBloKBackdrop : Module {
    // settings (saved with the patch)
    bool showBg = true;
    bool lookOn = true;       // Nov 2026: the BG look shows; off (BG > OFF), a loaded picture shows in its place
    int look = LOOK_GRADIENT;   // loads as the SynTHBloK black gradient (theme 0), no grid
    int colorIdx = 0;
    std::string imagePath;
    int bankImg = 0;          // the IMAGE BANK picture (1..kBankN, res/bank), used when imagePath is empty; 0: none
    bool collapsed = false;   // COLLAPSE: the module folds to 7 HP, showing a strip of the background
    int blankFill = 1;        // the 5 HP left by collapsing: 0 left empty, 1 a BLANK on the right, 2 on the left
    int64_t blankId = -1;     // that BLANK (locked to this BACKDROP while collapsed)
    int fit = 1;   // fill the screen: the image always snaps to cover the whole background
    float imgScale = 1.f, imgDim = 0.f;
    float imgW = 1.f, imgH = 1.f, imgX = 0.f, imgY = 0.f;   // fine-tuning: width / height stretch, X / Y offset (of the image's size)
    int caseIdx = 4;   // BLACK ANODIZED
    int themeIdx = 0;  // the theme last picked (-1: changed by hand since)
    float depthMm = 65.f;   // the case depth, mm, as real builds come
    int sideShape = 0, sideMat = 0, sideWidth = 0, openMode = 0;   // side panels (0: the case's own)
    int sideFinish = 0;   // species woods: OILED / SATIN / GLOSS
    int panelMat = 0, panelShape = 0, panelWidth = 1, panelFinish = 2;   // SIDE PANELS (attachments); 0: none
    int caseMat = 0;   // CASE MATERIAL (kPanelMat index; 0: the case's own)
    // RACK EARS: kEarFin index (0: none); the NOIZEE TOYZ ear's name on/off; shape 0 STRAIGHT, 1 CONTOURED outer edge
    int earKind = 0; bool earName = true; int earShape = 0;
    bool noCase = false;   // side panels only, no case between them: the rails held by the panels (an open rack)
    bool shadow = true, rails = true;
    bool showTitle = false;   // the name above the case: optional, off unless turned on (NAME page or menu)
    std::string title;   // empty: the patch's file name
    std::string presetPath;   // the preset last loaded or saved (shown on the THEME page)

    // the info strip's level meter: patch your mix into L / R
    float peak[2] = {0.f, 0.f}, hold[2] = {0.f, 0.f}, level[2] = {0.f, 0.f};
    int infoMode = 0;   // the info strip on the case's top: 0 auto (cases with a 1U row), 1 on, 2 off
    SynTHBloKBackdrop() {
        // no audio in (Nov 2026: the L/R jacks and the level meter came off the panel); peak/hold stay at 0, so the
        // BACKLIT ears and plates glow steadily and the info strip's meter rests
        config(0, 0, 0, 0);
    }

    json_t* dataToJson() override {
        json_t* r = json_object();
        json_object_set_new(r, "showBg", json_boolean(showBg));
        json_object_set_new(r, "lookOn", json_boolean(lookOn));
        json_object_set_new(r, "look", json_integer(look));
        json_object_set_new(r, "theme", json_integer(themeIdx));
        json_object_set_new(r, "color", json_integer(colorIdx));
        json_object_set_new(r, "image", json_string(imagePath.c_str()));
        json_object_set_new(r, "bankImg", json_integer(bankImg));
        json_object_set_new(r, "collapsed", json_boolean(collapsed));
        json_object_set_new(r, "blankFill", json_integer(blankFill));
        json_object_set_new(r, "blankId", json_integer(blankId));
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
        json_object_set_new(r, "panelMat", json_integer(panelMat));
        json_object_set_new(r, "panelShape", json_integer(panelShape));
        json_object_set_new(r, "panelWidth", json_integer(panelWidth));
        json_object_set_new(r, "panelFinish", json_integer(panelFinish));
        json_object_set_new(r, "caseMat", json_integer(caseMat));
        json_object_set_new(r, "earKind", json_integer(earKind));
        json_object_set_new(r, "earName", json_boolean(earName));
        json_object_set_new(r, "earShape", json_integer(earShape));
        json_object_set_new(r, "noCase", json_boolean(noCase));
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
        if (json_t* v = json_object_get(r, "bankImg")) bankImg = (int) json_integer_value(v);
        if (json_t* v = json_object_get(r, "collapsed")) collapsed = json_boolean_value(v);
        if (json_t* v = json_object_get(r, "blankFill")) blankFill = clamp((int) json_integer_value(v), 0, 2);
        if (json_t* v = json_object_get(r, "blankId")) blankId = json_integer_value(v);
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
        if (json_t* v = json_object_get(r, "panelMat")) panelMat = clamp((int) json_integer_value(v), 0, kPanelMatN - 1);
        if (json_t* v = json_object_get(r, "panelShape")) panelShape = clamp((int) json_integer_value(v), 0, 2);
        if (json_t* v = json_object_get(r, "panelWidth")) panelWidth = clamp((int) json_integer_value(v), 0, 2);
        if (json_t* v = json_object_get(r, "panelFinish")) panelFinish = clamp((int) json_integer_value(v), 0, 2);
        if (json_t* v = json_object_get(r, "caseMat")) caseMat = clamp((int) json_integer_value(v), 0, kPanelMatN - 1);
        if (json_t* v = json_object_get(r, "earKind")) earKind = clamp((int) json_integer_value(v), 0, kEarFinN - 1);
        if (json_t* v = json_object_get(r, "earName")) earName = json_boolean_value(v);
        if (json_t* v = json_object_get(r, "earShape")) earShape = clamp((int) json_integer_value(v), 0, 1);
        if (json_t* v = json_object_get(r, "noCase")) noCase = json_boolean_value(v);
        if (json_t* v = json_object_get(r, "openMode")) openMode = clamp((int) json_integer_value(v), 0, 2);
        if (json_t* v = json_object_get(r, "shadow")) shadow = json_boolean_value(v);
        if (json_t* v = json_object_get(r, "rails")) rails = json_boolean_value(v);
        if (json_t* v = json_object_get(r, "showTitle")) showTitle = json_boolean_value(v);
        if (json_t* v = json_object_get(r, "title")) title = json_string_value(v);
        // before lookOn, a loaded picture covered the look: keep showing it
        if (json_t* v = json_object_get(r, "lookOn")) lookOn = json_boolean_value(v);
        else lookOn = imagePath.empty() && bankImg <= 0;
    }
};

namespace {

// ---------------------------------------------------------------------------------------------------------
// THE LAYER: one widget in the rack, just above Rack's rails and below the modules and cables.
// ---------------------------------------------------------------------------------------------------------
// IMAGE BANK (Nov 2026): 50 pictures shipped with the plugin (res/bank/bank_NN.jpg + thumb_NN.jpg), from Nick's own
// abstract artwork; picked on the OLED's IMAGE page. Saved by number (bankImg), so patches move between machines.
static const int kBankN = 69;
static std::string bankPath(int n) { return asset::plugin(pluginInstance, string::f("res/bank/bank_%02d.jpg", clamp(n, 1, kBankN))); }
static std::string bankThumbPath(int n) { return asset::plugin(pluginInstance, string::f("res/bank/thumb_%02d.jpg", clamp(n, 1, kBankN))); }
static std::string bgImagePath(SynTHBloKBackdrop* m) { return !m->imagePath.empty() ? m->imagePath : m->bankImg > 0 ? bankPath(m->bankImg) : std::string(); }

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
        std::string ip = bgImagePath(m);   // the person's picture, else the bank's
        if (ip.empty()) { if (image >= 0) freeImage(); return; }
        if (image < 0 || imageLoaded != ip || imageVg != args.vg) {
            freeImage();
            image = nvgCreateImage(args.vg, ip.c_str(), NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY);
            imageVg = args.vg;
            imageLoaded = ip;
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
        if (cs.bodyTex) {   // CASE MATERIAL: the frame in a species wood / man-made material, lit from above
            base(x, y, w, h, rad);
            texRect(vg, x, y, w, h, rad, cs.bodyTex, 1.f, false, 260.f);   // along the lips
            for (int side = 0; side < 2; side++) {   // and up the ends
                float cx = side ? x + w - s : x;
                nvgSave(vg); nvgIntersectScissor(vg, cx, y, s, h);
                texRect(vg, x, y, w, h, rad, cs.bodyTex, 1.f, true, 260.f);
                nvgRestore(vg);
            }
            nvgBeginPath(vg); nvgRoundedRect(vg, x, y, w, h, rad);
            nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x, y + h, nvgRGBA(255, 255, 255, 22), nvgRGBA(0, 0, 0, 70)));
            nvgFill(vg);
            return;
        }
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
            if (cs.bodyTex) {   // CASE MATERIAL on an open frame: the panels in that material, lit from above
                int bi = tex(vg, cs.bodyTex);
                if (bi >= 0) {
                    nvgFillPaint(vg, nvgImagePattern(vg, xin, in.pos.y, 260.f, 260.f, (float) M_PI * 0.5f, bi, 1.f)); nvgFill(vg);
                    nvgFillPaint(vg, nvgLinearGradient(vg, 0, in.pos.y, 0, in.pos.y + in.size.y, nvgRGBA(255, 255, 255, 18), nvgRGBA(0, 0, 0, 60))); nvgFill(vg);
                }
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
    // ---- SIDE PANELS (attachments): a solid slab on each end of the case, flush with its top and its full height.
    // Its face has a round-over on every edge (the light from the top left rolls onto the sides), its depth shows
    // in the same material, darker, and it throws its own shadow. No screws on the face:
    // real panels are screwed on from the inside.
    // DEPTH: the panel is as deep as the case. It is swept back toward the lower right exactly as drawCase sweeps the case
    // (by d = depth * 0.18), so its side and foot follow the case's own side and foot. The order makes the hidden parts
    // hide: pass 0 the shadows, pass 1 the LEFT panel's depth (then the case, which covers what of it lies behind the
    // case), pass 2 the RIGHT panel's depth (over the case's right side, which it stands in front of), pass 3 the faces.
    void drawPanels(NVGcontext* vg, const CaseStyle& cs, math::Rect in, int mat, int shape, int finish, bool doShadow, float depthMm, int pass, bool name = true, float level = 0.f) {
        const PanelMat& pm = kPanelMat[clamp(mat, 1, kPanelMatN - 1)];
        int img = pm.tex ? tex(vg, pm.tex) : -1;
        NVGcolor avg = nvgRGB(pm.r, pm.g, pm.b);
        float s = cs.side, tt = topLip(cs);
        float x = in.pos.x - s, y = in.pos.y - tt, w = in.size.x + 2.f * s, h = in.size.y + 6.f + tt;
        float pw = kPanelWidthPx[0], rad = kPanelRadius[clamp(shape, 0, 2)];
        // flush with the top of the case and running its full height, so it follows the case's size
        float py = y, ph = h, e = std::min(7.f, pw * 0.2f);
        float d = std::max(4.f, mm2px(depthMm) * 0.18f);   // as deep as the case (a little depth even on a flat case)
        for (int side = 0; side < 2; side++) {
            float px = side ? x + w : x - pw;
            if (pass == 0) {   // the shadow
                if (doShadow) {
                    nvgBeginPath(vg); nvgRect(vg, px - 40.f, py - 30.f, pw + d + 90.f, ph + d + 90.f);
                    nvgFillPaint(vg, nvgBoxGradient(vg, px + d * 0.8f + 4.f, py + d * 0.9f + 8.f, pw, ph, rad + 6.f, 16.f, nvgRGBA(0, 0, 0, 150), nvgRGBA(0, 0, 0, 0)));
                    nvgFill(vg);
                }
                continue;
            }
            if (pass == 1 || pass == 2) {   // the depth (pass 1: the left panel's, pass 2: the right one's)
            if ((pass == 1) != (side == 0)) continue;
            // the outline swept back to the lower right, darker toward the back
            int nd = std::max(2, std::min(24, (int) (d / 1.5f)));
            {
                nvgSave(vg);
                // every face carries the material: each layer is the texture (one continuous pattern), then a shade
                // that deepens toward the back, so the side and foot read as the same piece turned away from the light
                for (int k = nd; k >= 1; k--) {
                    float o = d * k / nd;
                    nvgBeginPath(vg); nvgRoundedRect(vg, px + o, py + o, pw, ph, rad);
                    nvgFillColor(vg, avg); nvgFill(vg);
                    if (img >= 0) { nvgFillPaint(vg, nvgImagePattern(vg, px, py, 260.f, 260.f, (float) M_PI * 0.5f, img, 1.f)); nvgFill(vg); }
                    nvgFillColor(vg, nvgRGBA(0, 0, 0, (int) (110 + 60.f * k / nd))); nvgFill(vg);
                }
                nvgRestore(vg);
            }
            continue;
            }
            // pass 3: the face
            nvgBeginPath(vg); nvgRoundedRect(vg, px, py, pw, ph, rad);
            nvgFillColor(vg, avg); nvgFill(vg);
            if (img >= 0) { nvgFillPaint(vg, nvgImagePattern(vg, px, py, 260.f, 260.f, (float) M_PI * 0.5f, img, 1.f)); nvgFill(vg); }
            nvgFillPaint(vg, nvgLinearGradient(vg, 0, py, 0, py + ph, nvgRGBA(255, 255, 255, 16), nvgRGBA(0, 0, 0, 60))); nvgFill(vg);
            // the round-over: rings inset from the rim, lit at the top left and shaded at the lower right, strongest at
            // the rim and fading in (the profile of a quarter circle)
            int n = std::max(3, (int) std::round(e));
            for (int i = 0; i < n; i++) {
                float k = 1.f - std::sin((i + 0.5f) / n * (float) M_PI * 0.5f), ins = i + 0.5f;
                nvgBeginPath(vg); nvgRoundedRect(vg, px + ins, py + ins, pw - 2.f * ins, ph - 2.f * ins, std::max(0.f, rad - ins));
                nvgStrokePaint(vg, nvgLinearGradient(vg, px, py, px + pw, py + pw * 1.4f, nvgRGBA(255, 255, 255, (int) (95 * k)), nvgRGBA(0, 0, 0, (int) (120 * k))));
                nvgStrokeWidth(vg, 1.05f); nvgStroke(vg);
            }
            if (int art = noizeePanelArt(mat))   // NoiZee ToyZ plates: the matching ear's art (badge, backlit, circuit)
                drawNoizeeEar(vg, art, name, px, pw, side, py, ph, py + 4.f, py + ph - 4.f, level);
            if (finish >= 1) {   // satin / gloss: a soft vertical highlight and a sheen across the top
                float kk = finish == 2 ? 1.f : 0.45f, cx = px + pw * 0.36f, bw = std::max(3.f, pw * 0.16f);
                nvgBeginPath(vg); nvgRoundedRect(vg, px + e * 0.5f, py + e * 0.5f, pw - e, ph - e, std::max(0.f, rad - e * 0.5f));
                nvgFillPaint(vg, nvgLinearGradient(vg, cx - bw, 0, cx, 0, nvgRGBA(255, 255, 255, 0), nvgRGBA(255, 255, 255, (int) (52 * kk)))); nvgFill(vg);
                nvgFillPaint(vg, nvgLinearGradient(vg, cx, 0, cx + bw, 0, nvgRGBA(255, 255, 255, (int) (52 * kk)), nvgRGBA(255, 255, 255, 0))); nvgFill(vg);
                nvgFillPaint(vg, nvgLinearGradient(vg, 0, py, 0, py + ph * 0.12f, nvgRGBA(255, 255, 255, (int) (34 * kk)), nvgRGBA(255, 255, 255, 0))); nvgFill(vg);
            }
            // a hairline where the panel meets the case
            nvgBeginPath(vg); nvgMoveTo(vg, side ? px + 0.5f : px + pw - 0.5f, y + 2.f); nvgLineTo(vg, side ? px + 0.5f : px + pw - 0.5f, y + h - 2.f);
            nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 90)); nvgStrokeWidth(vg, 1.f); nvgStroke(vg);
        }
    }
    // ---- the NoiZee ToyZ ears' artwork, between the top and bottom screws (sy0, sy1). The name reads bottom to top and
    // sits low, ending just above the bottom screw. name = false leaves the name off (the art stays).
    void earText(NVGcontext* vg, const char* t, float cx, float yEnd, float size, float spacing, NVGcolor c, int style) {
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgSave(vg);
        nvgTranslate(vg, cx, yEnd); nvgRotate(vg, -(float) M_PI * 0.5f);
        nvgFontFaceId(vg, font->handle); nvgFontSize(vg, size); nvgTextLetterSpacing(vg, spacing);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        if (style == 1) {   // engraved: a dark cut with a lit lower lip
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 70)); nvgText(vg, 0, 0.8f, t, NULL);
            nvgFillColor(vg, nvgRGBA(0, 0, 0, 120)); nvgText(vg, 0, -0.6f, t, NULL);
        }
        if (style == 2) {   // lit: a soft glow behind
            nvgFontBlur(vg, 6.f); nvgFillColor(vg, nvgTransRGBA(c, 160)); nvgText(vg, 0, 0, t, NULL); nvgFontBlur(vg, 0.f);
        }
        nvgFillColor(vg, c); nvgText(vg, 0, 0, t, NULL);
        nvgRestore(vg);
    }
    void drawNoizeeEar(NVGcontext* vg, int kind, bool name, float px, float pw, int side, float y, float h, float sy0, float sy1, float level) {
        const NVGcolor orange = nvgRGB(255, 122, 0), teal = nvgRGB(0, 255, 170);
        float cx = px + pw * 0.5f, top = sy0 + 22.f, bot = sy1 - 22.f, span = bot - top;
        if (span < 40.f) return;
        if (kind == kEarNoizee) {   // BIG WORDMARK: the name large and low, the orange rule down the inner edge
            float rx = side ? px + 8.f : px + pw - 10.f;
            nvgBeginPath(vg); nvgRect(vg, rx, top, 2.f, span); nvgFillColor(vg, orange); nvgFill(vg);
            nvgBeginPath(vg); nvgRect(vg, rx - 2.f, top, 6.f, span); nvgFillColor(vg, nvgRGBA(255, 122, 0, 40)); nvgFill(vg);
            if (name) earText(vg, "NoiZee ToyZ", cx - (side ? -3.f : 3.f), bot, std::min(22.f, span * 0.09f), 1.5f, orange, 0);
        }
        else if (kind == kEarNoizee + 1) {   // LOGO BADGE: the hex Z logo as an enamel badge, the name above it
            float r = std::min(pw * 0.3f, span * 0.12f), by = bot - r;
            NVGcolor dark = nvgRGB(10, 10, 12);
            auto hexPath = [&](float dx, float dy) {
                nvgBeginPath(vg);
                for (int k = 0; k < 6; k++) { float a = (float) M_PI / 6.f + k * (float) M_PI / 3.f; float hx = cx + dx + r * std::cos(a), hy = by + dy + r * std::sin(a); if (k) nvgLineTo(vg, hx, hy); else nvgMoveTo(vg, hx, hy); }
                nvgClosePath(vg);
            };
            hexPath(1.5f, 2.5f); nvgFillColor(vg, nvgRGBA(0, 0, 0, 120)); nvgFill(vg);   // its shadow
            hexPath(0.f, 0.f); nvgFillColor(vg, dark); nvgFill(vg);
            nvgStrokeColor(vg, teal); nvgStrokeWidth(vg, std::max(2.f, r * 0.1f)); nvgStroke(vg);
            nvgBeginPath(vg); nvgCircle(vg, cx, by - r * 0.39f, r * 0.16f); nvgStrokeWidth(vg, std::max(1.5f, r * 0.07f)); nvgStroke(vg);
            nvgBeginPath(vg); nvgMoveTo(vg, cx - r * 0.28f, by - r * 0.12f); nvgLineTo(vg, cx + r * 0.28f, by - r * 0.12f);
            nvgLineTo(vg, cx - r * 0.28f, by + r * 0.2f); nvgLineTo(vg, cx + r * 0.28f, by + r * 0.2f);
            nvgStrokeColor(vg, orange); nvgStrokeWidth(vg, std::max(1.5f, r * 0.08f)); nvgStroke(vg);
            nvgBeginPath(vg); nvgCircle(vg, cx, by + r * 0.46f, r * 0.13f); nvgFillColor(vg, teal); nvgFill(vg);
            nvgBeginPath(vg); nvgRect(vg, cx - r * 0.9f, by - r * 0.95f, r * 0.5f, r * 0.12f); nvgFillColor(vg, nvgRGBA(255, 255, 255, 30)); nvgFill(vg);   // enamel shine
            if (name) earText(vg, "NOIZEE TOYZ", cx, by - r - 14.f, std::min(13.f, span * 0.06f), 4.f, nvgRGB(205, 205, 210), 0);
        }
        else if (kind == kEarNoizee + 2) {   // ENGRAVED: laser-etched into raw aluminium, with a small data line
            if (name) {
                earText(vg, "NOIZEE TOYZ", cx - 6.f, bot, std::min(20.f, span * 0.085f), 3.f, nvgRGB(72, 74, 78), 1);
                earText(vg, "SYNTHBLOK  \xc2\xb7  EURORACK 3U", cx + 18.f, bot, 7.f, 1.f, nvgRGB(84, 86, 90), 1);
            }
        }
        else if (kind == kEarNoizee + 3) {   // BACKLIT: slots and the name cut through, lit orange from behind; the glow
            // breathes with the audio coming into BACKDROP
            float lv = clamp(0.55f + 0.45f * std::sqrt(clamp(level, 0.f, 1.f)), 0.f, 1.f);
            NVGcolor lit = nvgLerpRGBA(nvgRGB(120, 50, 0), nvgRGB(255, 150, 40), lv);
            float slotsEnd = top + span * 0.42f, sw = pw * 0.46f;
            for (float yy = top + 4.f; yy + 5.f < slotsEnd; yy += 12.f) {
                nvgBeginPath(vg); nvgRoundedRect(vg, cx - sw * 0.5f - 4.f, yy - 4.f, sw + 8.f, 13.f, 6.f);
                nvgFillPaint(vg, nvgBoxGradient(vg, cx - sw * 0.5f, yy, sw, 5.f, 2.5f, 8.f, nvgTransRGBA(lit, (unsigned char) (110 * lv)), nvgRGBA(0, 0, 0, 0))); nvgFill(vg);
                nvgBeginPath(vg); nvgRoundedRect(vg, cx - sw * 0.5f, yy, sw, 5.f, 2.5f); nvgFillColor(vg, lit); nvgFill(vg);
            }
            if (name) earText(vg, "NoiZee ToyZ", cx, bot, std::min(19.f, span * 0.08f), 1.5f, lit, 2);
        }
        else {   // CIRCUIT: the logo's line with two nodes and the lightning Z, the name below
            float ly0 = top, ly1 = top + span * 0.3f;
            nvgBeginPath(vg); nvgMoveTo(vg, cx, ly0); nvgLineTo(vg, cx, ly1); nvgStrokeColor(vg, teal); nvgStrokeWidth(vg, 2.f); nvgStroke(vg);
            for (float ny : {ly0 + span * 0.04f, ly0 + span * 0.16f}) { nvgBeginPath(vg); nvgCircle(vg, cx, ny, 5.f); nvgFillColor(vg, nvgRGB(12, 14, 14)); nvgFill(vg); nvgStrokeWidth(vg, 2.f); nvgStroke(vg); }
            float zy = ly1, zs = 14.f;
            nvgBeginPath(vg); nvgMoveTo(vg, cx, zy); nvgLineTo(vg, cx - zs, zy + zs * 0.55f); nvgLineTo(vg, cx + zs, zy + zs * 0.55f); nvgLineTo(vg, cx, zy + zs * 1.1f);
            nvgStrokeColor(vg, orange); nvgStrokeWidth(vg, 3.f); nvgLineJoin(vg, NVG_MITER); nvgStroke(vg);
            if (name) earText(vg, "NOIZEE TOYZ", cx, bot, std::min(15.f, span * 0.065f), 5.f, nvgRGB(235, 235, 235), 0);
        }
    }
    // ---- RACK EARS: on each end of the case (outside the side panels, if there are any)
    // With ears there is no case: an open rack, just Rack's rails, whose ends screw straight onto the ears. So the ears
    // sit right at the ends of the module rows, and their slots line up with each row's rails.
    void drawEars(NVGcontext* vg, math::Rect in, int kind, bool name, int shape, bool doShadow, const std::vector<float>& rowTops, float level, float depthMm) {
        const EarFin& ef = kEarFin[clamp(kind, 1, kEarFinN - 1)];
        NVGcolor c = nvgRGB(ef.r, ef.g, ef.b);
        float x = in.pos.x, y = in.pos.y, w = in.size.x, h = in.size.y;
        const float lip = 7.f, plate = 92.f;   // twice the first width (Nov 2026)
        // CONTOURED: the top and bottom outer corners are rounded off (a generous radius); STRAIGHT: square corners
        auto platePath = [&](float px, int sd) {
            float in0 = sd ? px : px + plate, out = sd ? px + plate : px, o = sd ? 1.f : -1.f;
            float rc = shape ? std::min(18.f, h * 0.2f) : 0.f;
            nvgBeginPath(vg);
            nvgMoveTo(vg, in0, y);
            nvgLineTo(vg, out - o * rc, y);
            if (rc > 0.f) nvgArcTo(vg, out, y, out, y + rc, rc);
            nvgLineTo(vg, out, y + h - rc);
            if (rc > 0.f) nvgArcTo(vg, out, y + h, out - o * rc, y + h, rc);
            nvgLineTo(vg, in0, y + h);
            nvgClosePath(vg);
        };
        int brushed = tex(vg, "brushed");
        for (int side = 0; side < 2; side++) {
            int dir = side ? 1 : -1;
            float lx = side ? x + w : x - lip;                          // the lip, seen edge-on along the case end
            float px = side ? x + w + lip : x - lip - plate;            // the plate, bent outward
            float ox = std::min(lx, px), ow = lip + plate;
            if (doShadow) {
                nvgBeginPath(vg); nvgRect(vg, ox - 30.f, y - 30.f, ow + 80.f, h + 80.f);
                nvgFillPaint(vg, nvgBoxGradient(vg, ox + 6.f, y + 8.f, ow, h, 2.f, 12.f, nvgRGBA(0, 0, 0, 140), nvgRGBA(0, 0, 0, 0)));
                nvgFill(vg);
            }
            // the WING: the side of the L running back along the modules, as deep as the case setting (swept to the lower
            // right like the case). The left wing goes in behind the modules, so it shows below the bottom row; the
            // right one runs behind its plate and shows below it.
            {
                float d = std::max(6.f, mm2px(depthMm) * 0.18f);
                int nw = std::max(2, std::min(24, (int) (d / 1.5f)));
                for (int k = nw; k >= 1; k--) {
                    float o = d * k / nw;
                    nvgBeginPath(vg); nvgRect(vg, lx + o, y + o, lip, h);
                    nvgFillColor(vg, nvgLerpRGBA(c, nvgRGB(0, 0, 0), 0.45f + 0.25f * k / nw)); nvgFill(vg);
                    if (brushed >= 0 && ef.brushed > 0.f) { nvgFillPaint(vg, nvgImagePattern(vg, lx, y, 300.f, 300.f, (float) M_PI * 0.5f, brushed, ef.brushed * 0.35f)); nvgFill(vg); }
                }
                // the plate's own thickness (sheet metal), a dark edge below and to the right
                nvgSave(vg); nvgTranslate(vg, 1.8f, 1.8f); platePath(px, side); nvgRestore(vg);
                nvgFillColor(vg, nvgLerpRGBA(c, nvgRGB(0, 0, 0), 0.65f)); nvgFill(vg);
            }
            // plate: the colour, brushed grain, then a subtle shade from the bend (inside, darker) to the outer edge,
            // and a faint fall-off from top to bottom
            platePath(px, side);
            nvgFillColor(vg, c); nvgFill(vg);
            if (brushed >= 0 && ef.brushed > 0.f) { nvgFillPaint(vg, nvgImagePattern(vg, px, y, 300.f, 300.f, (float) M_PI * 0.5f, brushed, ef.brushed)); nvgFill(vg); }
            float inX = side ? px : px + plate, outX = side ? px + plate : px;
            nvgFillPaint(vg, nvgLinearGradient(vg, inX, 0, outX, 0, nvgRGBA(0, 0, 0, 38), nvgRGBA(255, 255, 255, 10))); nvgFill(vg);
            nvgFillPaint(vg, nvgLinearGradient(vg, 0, y, 0, y + h, nvgRGBA(255, 255, 255, 8), nvgRGBA(0, 0, 0, 16))); nvgFill(vg);
            nvgBeginPath(vg); nvgRect(vg, px + (shape && !side ? 18.f : 0.f), y, plate - (shape ? 18.f : 0.f), 1.f); nvgFillColor(vg, nvgRGBA(255, 255, 255, 60)); nvgFill(vg);   // lit top edge
            platePath(px, side);   // the outer rim: a dark line round the plate (the bend side is covered by the lip)
            nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 110)); nvgStrokeWidth(vg, 1.f); nvgStroke(vg);
            // lip and bend: a rounded highlight across the fold
            for (int half = 0; half < 2; half++) {
                float hx = lx + half * lip * 0.5f;
                nvgBeginPath(vg); nvgRect(vg, hx, y, lip * 0.5f, h);
                NVGcolor a = nvgLerpRGBA(c, nvgRGB(0, 0, 0), 0.4f), b = nvgLerpRGBA(c, nvgRGB(255, 255, 255), 0.25f);
                nvgFillPaint(vg, half ? nvgLinearGradient(vg, hx, 0, hx + lip * 0.5f, 0, b, a) : nvgLinearGradient(vg, hx, 0, hx + lip * 0.5f, 0, a, b));
                nvgFill(vg);
            }
            float cx = px + plate * 0.5f;
            // only at the top of the first row and the bottom of the last (none where two rows meet)
            std::vector<float> slots;
            if (!rowTops.empty()) { float ins = 7.5f + mm2px(5.f); slots.push_back(rowTops.front() + ins); slots.push_back(rowTops.back() + RACK_GRID_HEIGHT - ins); }   // 5 mm in from the rails, so the heads sit on the ear
            else { slots.push_back(y + 22.f); slots.push_back(y + h - 22.f); }
            // round holes with our black socket-head screw in each, bigger than the modules' M3 screws: a 10-32 rack
            // screw's socket cap is about 7.9 mm across (23 px; the modules' screw is 15 px)
            static std::shared_ptr<window::Svg> screw;
            if (!screw) screw = window::Svg::load(asset::plugin(pluginInstance, "res/SocketScrewBlack.svg"));
            const float sd = 23.f;
            for (float sy : slots) {
                nvgBeginPath(vg); nvgCircle(vg, cx, sy, sd * 0.5f + 1.2f);   // a faint countersink ring round the head
                nvgFillPaint(vg, nvgRadialGradient(vg, cx, sy, sd * 0.5f - 1.f, sd * 0.5f + 1.2f, nvgRGBA(0, 0, 0, 120), nvgRGBA(0, 0, 0, 0))); nvgFill(vg);
                if (screw && screw->handle) {
                    nvgSave(vg); nvgTranslate(vg, cx - sd * 0.5f, sy - sd * 0.5f); nvgScale(vg, sd / 15.f, sd / 15.f);
                    window::svgDraw(vg, screw->handle);
                    nvgRestore(vg);
                } else {
                    nvgBeginPath(vg); nvgCircle(vg, cx, sy, 4.f); nvgFillColor(vg, nvgRGB(8, 8, 10)); nvgFill(vg);   // the bare round hole
                }
            }
            if (isNoizee(kind)) drawNoizeeEar(vg, kind, name, px, plate, side, y, h, slots.front(), slots.back(), level);
            (void) dir;
        }
    }
    // the case's shadow (drawCase draws it; with side panels it is drawn first, apart, so the left panel's depth can go
    // between the shadow and the case)
    void caseShadow(NVGcontext* vg, const CaseStyle& cs, math::Rect in, float depthMm) {
        float s = cs.side, t = 6.f, tt = topLip(cs);
        float x = in.pos.x - s, y = in.pos.y - tt, w = in.size.x + 2.f * s, h = in.size.y + t + tt;
        float d = mm2px(depthMm) * 0.18f;
        {
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
    }
    void drawCase(NVGcontext* vg, const CaseStyle& cs, math::Rect in, float depthMm, bool doShadow, bool doRails, const std::vector<float>& rowTops) {
        if (cs.extras & X_OPEN) { drawOpenFrame(vg, cs, in, depthMm, doShadow, doRails, rowTops); return; }
        float s = cs.side, t = 6.f, tt = topLip(cs);   // ends, the bottom lip and the top lip (taller with a 1U row)
        float x = in.pos.x - s, y = in.pos.y - tt, w = in.size.x + 2.f * s, h = in.size.y + t + tt;
        float d = mm2px(depthMm) * 0.18f;   // the visible side, below and to the right: the real depth, seen at a slant
        if (doShadow) caseShadow(vg, cs, in, depthMm);
        float rr = caseRadius(cs);
        if (d > 0.5f && rr > 0.f) {   // a rounded case: its rounded outline swept back for the depth
            int n = std::max(2, std::min(24, (int) (d / 1.5f)));
            int bimg = cs.bodyTex ? tex(vg, cs.bodyTex) : -1;   // CASE MATERIAL: its texture on the sides too, shaded
            for (int k = n; k >= 1; k--) {
                float o = d * k / n;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, x + o, y + o, w, h, rr);
                if (bimg >= 0) {
                    nvgFillColor(vg, cs.base); nvgFill(vg);
                    nvgFillPaint(vg, nvgImagePattern(vg, x, y, 260.f, 260.f, 0.f, bimg, 1.f)); nvgFill(vg);
                    nvgFillColor(vg, nvgRGBA(0, 0, 0, (int) (115 + 40.f * k / n))); nvgFill(vg);
                }
                else { nvgFillColor(vg, nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.45f + 0.15f * k / n)); nvgFill(vg); }
            }
        }
        else if (d > 0.5f) {   // the 3D sides: right and bottom faces, darker
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + w, y); nvgLineTo(vg, x + w + d, y + d); nvgLineTo(vg, x + w + d, y + h + d); nvgLineTo(vg, x + w, y + h);
            nvgClosePath(vg);
            int bimg = cs.bodyTex ? tex(vg, cs.bodyTex) : -1;   // CASE MATERIAL: its texture on the sides too, shaded
            if (bimg >= 0) {
                nvgFillColor(vg, cs.base); nvgFill(vg);
                nvgFillPaint(vg, nvgImagePattern(vg, x, y, 260.f, 260.f, (float) M_PI * 0.5f, bimg, 1.f)); nvgFill(vg);
                nvgFillPaint(vg, nvgLinearGradient(vg, x + w, 0, x + w + d, 0, nvgRGBA(0, 0, 0, 150), nvgRGBA(0, 0, 0, 200))); nvgFill(vg);
            }
            else {
                nvgFillPaint(vg, nvgLinearGradient(vg, x + w, 0, x + w + d, 0, nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.62f), nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.82f)));
                nvgFill(vg);
            }
            nvgBeginPath(vg);
            nvgMoveTo(vg, x, y + h); nvgLineTo(vg, x + w, y + h); nvgLineTo(vg, x + w + d, y + h + d); nvgLineTo(vg, x + d, y + h + d);
            nvgClosePath(vg);
            if (bimg >= 0) {
                nvgFillColor(vg, cs.base); nvgFill(vg);
                nvgFillPaint(vg, nvgImagePattern(vg, x, y, 260.f, 260.f, 0.f, bimg, 1.f)); nvgFill(vg);
                nvgFillPaint(vg, nvgLinearGradient(vg, 0, y + h, 0, y + h + d, nvgRGBA(0, 0, 0, 130), nvgRGBA(0, 0, 0, 190))); nvgFill(vg);
            }
            else {
                nvgFillPaint(vg, nvgLinearGradient(vg, 0, y + h, 0, y + h + d, nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.55f), nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.8f)));
                nvgFill(vg);
            }
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
        // every row of a case runs edge to edge, like real Eurorack rails: a shorter row's empty end stays open too,
        // so Rack's own rails show across the whole case between the side panels (BACKDROP never draws rails)
        for (Block& bl : out)
            for (math::Rect& rr : bl.rowRects) { rr.pos.x = bl.r.pos.x; rr.size.x = bl.r.size.x; }
        return out;
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;
        SynTHBloKBackdrop* m = module;
        math::Rect v = args.clipBox;
        const std::vector<CaseStyle>& lib = caseLibrary();
        CaseStyle cs = lib[clamp(m->caseIdx, 0, (int) lib.size() - 1)];
        if (cs.material != M_NONE) {   // the side panel options
            // END SHAPE / END WIDTH: removed (Nov 2026); every case keeps the ends it was drawn with
            if (m->openMode == 1) cs.extras |= X_OPEN;
            if (m->openMode == 2) cs.extras &= ~X_OPEN;
            cs.sideIdx = 0;   // CASE ENDS material: removed (Nov 2026); old patches' choice is ignored
            if (m->caseMat > 0 && kPanelMat[clamp(m->caseMat, 0, kPanelMatN - 1)].tex) {   // CASE MATERIAL
                const PanelMat& pm = kPanelMat[clamp(m->caseMat, 0, kPanelMatN - 1)];
                cs.bodyTex = pm.tex; cs.base = nvgRGB(pm.r, pm.g, pm.b);
            }
            if (m->infoMode == 1) cs.extras |= X_1U;
            if (m->infoMode == 2) cs.extras &= ~X_1U;
        }
        NVGcolor col = kColor[clamp(m->colorIdx, 0, kColorN - 1)];
        std::vector<Block> bl = blocks();
        std::vector<math::Rect> holes;   // the module rows: left open, so Rack's own rails show (BACKDROP never redraws them)
        for (const Block& b : bl) holes.insert(holes.end(), b.rowRects.begin(), b.rowRects.end());
        if (m->showBg)
            outside(args.vg, v, holes, [&]() {
                if (m->lookOn) drawLook(args.vg, v, m->look, col, cs.material == M_NONE ? col : cs.base);
                else drawImage(args, v);   // BG OFF: the loaded picture shows in the look's place
            });
        if (cs.material != M_NONE || m->earKind > 0)
            outside(args.vg, v, holes, [&]() {
                for (const Block& b : bl) {
                    // either side panels on a case, or rack ears on an open rack (no case: the rails screw onto the
                    // ears); never both (ears win in an old patch with both)
                    if (m->earKind > 0) { drawEars(args.vg, b.r, m->earKind, m->earName, m->earShape, m->shadow, b.rows, std::max(m->peak[0], m->peak[1]), m->depthMm); continue; }
                    if (m->panelMat > 0) {   // side panels replace the case's own ends (they come back when the panels go)
                        CaseStyle pc = cs; pc.side = 2.f; pc.ends = E_SQUARE;
                        bool box = !m->noCase && !(pc.extras & X_OPEN);
                        if (box && m->shadow) caseShadow(args.vg, pc, b.r, m->depthMm);
                        for (int pass = 0; pass < 4; pass++) {
                            if (pass == 2 && !m->noCase) drawCase(args.vg, pc, b.r, m->depthMm, m->shadow && !box, false, b.rows);   // CASE OFF: just the panels
                            drawPanels(args.vg, pc, b.r, m->panelMat, m->panelShape, m->panelFinish, m->shadow, m->depthMm, pass, m->earName, std::max(m->peak[0], m->peak[1]));
                        }
                    }
                    else drawCase(args.vg, cs, b.r, m->depthMm, m->shadow, false, b.rows);
                }
            });
        // the patch title, above the first block
        if (false && m->showTitle && !bl.empty()) {   // the name came off (Nov 2026): no title on the rack
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
// THE PANEL: res/SynTHBloKBackdrop.svg (tools/make_backdrop_panel.py), 12 HP, the SynTHBloK look. This widget
// draws nothing extra now (the status light lives in the OLED's NAME tab; the meter and jacks are gone).
// ---------------------------------------------------------------------------------------------------------
struct BackdropPanel : widget::Widget {
    SynTHBloKBackdrop* module = NULL;
    void* widget = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        // (nothing now: the level meter came off the panel with the jacks)
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
// sideMat/sideFinish: the side panels' wood (0: none); ear/earShape: rack ears (kEarFin index, 0: none; 1 = contoured).
// Remade Nov 2026: most themes are just a case; a few wear side panels, a few rack ears (an open rack, no case).
struct Theme { const char* name; int look; int color; const char* caseName; float depthMm; int sideMat; int sideFinish; int ear; int earShape; };
static const Theme kThemes[] = {
    {"SYNTHBLOK BLACK", LOOK_GRADIENT, 0, "BLACK ANODIZED", 65.f, 0, 0, 10, 0},
    {"WALNUT STUDIO", LOOK_CASE_GRADIENT, 0, "WALNUT SKIFF", 84.f},
    {"OAK DAYLIGHT", LOOK_CASE_GRADIENT, 0, "OAK SKIFF", 65.f},
    {"EBONY NIGHT", LOOK_GRADIENT, 1, "EBONY SKIFF", 84.f},
    {"WEST COAST", 17, 13, "WEST COAST BOAT", 84.f},
    {"TIERED SUNSET", 14, 5, "TIERED WALNUT", 100.f},
    {"AMBER LAB", 1, 2, "LAB EXTRUSION", 84.f},
    {"OLED GLOW", 2, 0, "BLACK ANODIZED", 50.f},
    {"DEEP SPACE", 4, 0, "SMOKED ACRYLIC", 50.f, 0, 0, 13, 1},
    {"BLUEPRINT", 10, 8, "BRUSHED ALUMINUM", 65.f, 0, 0, 12, 0},
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
    {"CARBON", 6, 0, "CARBON FIBER", 65.f, 0, 0, 11, 1},
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
    {"GOLD MARBLE", LOOK_ART + 7, 0, "EBONY SKIFF", 84.f, 0, 0, 3, 1},
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
    // DREAM combos (round 6): the Midjourney-inspired art with matching cases (a few wear species-wood side panels)
    {"MOTION WALNUT", LOOK_ART + 19, 0, "WALNUT CONSOLE", 120.f, SW_BLACK_WALNUT, F_OILED, 0, 0},
    {"MIRROR NEON", LOOK_ART + 20, 0, "STEPPED NEON", 84.f},
    {"IRIS WATCH", LOOK_ART + 21, 0, "BLACK ANODIZED", 65.f, SW_MACASSAR, F_GLOSS, 0, 0},
    {"TUNNEL VISION", LOOK_ART + 22, 0, "SMOKED ACRYLIC", 50.f},
    {"PRISM CASE", LOOK_ART + 23, 0, "CLEAR ACRYLIC", 50.f},
    {"BRONZE TEMPLE", LOOK_ART + 24, 0, "EBONY GOLD CONSOLE", 120.f},
    {"FLOWER OF LIFE", LOOK_ART + 25, 0, "CURVED VIOLET", 100.f},
    {"GOLD FIBRE", LOOK_ART + 26, 0, "BLACK PLY STUDIO", 100.f},
    {"NEURAL VIOLET", LOOK_ART + 27, 0, "SMOKED VIOLET ACRYLIC", 50.f},
    {"COSMIC KOA", LOOK_ART + 28, 0, "CURVED CONSOLE", 100.f},
    {"CLOUD NINE", LOOK_ART + 29, 0, "POD PURPLE", 65.f},
    {"INK AND MAPLE", LOOK_ART + 30, 0, "BIRCH PLY STUDIO", 84.f},
    {"GLASS WORLD", LOOK_ART + 31, 0, "NEON ACRYLIC CYAN", 50.f},
    {"COPPER STAIRS", LOOK_ART + 32, 0, "STEPPED WALNUT", 120.f},
    {"TESSELLATE", LOOK_ART + 33, 0, "STEPPED OAK", 100.f},
    {"VIOLET RIDGE", LOOK_ART + 34, 0, "BLACKLIGHT POD", 65.f},
    {"SNOW GLOBE", LOOK_ART + 35, 0, "POD BLUE", 65.f},
    {"MANDELBROT", LOOK_ART + 36, 0, "CARBON FIBER", 65.f},
    {"JULIA", LOOK_ART + 37, 0, "NEON ACRYLIC PINK", 50.f},
    {"REEF", LOOK_ART + 38, 0, "MYCELIUM LATTICE", 84.f},
    {"LOTUS", LOOK_ART + 39, 0, "CHERRY STUDIO", 100.f, SW_CHERRY, F_GLOSS, 0, 0},
    {"NEON MARBLE", LOOK_ART + 40, 0, "GRAFFITI ROAD CASE", 84.f},
    {"MELTDOWN", LOOK_ART + 41, 0, "FORGE STEEL", 100.f},
    {"THIRD EYE", LOOK_ART + 42, 0, "IRIDESCENT ANODIZED", 65.f},
    {"RAINBOW SURF", LOOK_ART + 43, 0, "WEST COAST BOAT", 84.f},
    // DREAM II combos (round 6b)
    {"TESSERACT", LOOK_ART + 44, 0, "CARBON FIBER", 65.f},
    {"LAVA LOUNGE", LOOK_ART + 45, 0, "WALNUT CONSOLE", 120.f},
    {"OP POP", LOOK_ART + 46, 0, "POWDER COAT WHITE", 65.f},
    {"RING NEBULA", LOOK_ART + 47, 0, "BLACK ANODIZED", 84.f, SW_WENGE, F_SATIN, 0, 0},
    {"AURORA HALO", LOOK_ART + 48, 0, "SMOKED ACRYLIC", 50.f},
    {"CRYSTAL GROTTO", LOOK_ART + 49, 0, "CLEAR ACRYLIC", 50.f},
    {"FERN GULLY", LOOK_ART + 50, 0, "OAK OPEN FRAME", 100.f},
    {"STORM CELL", LOOK_ART + 51, 0, "BRUSHED ALUMINUM", 65.f},
    {"DRAGON FOLD", LOOK_ART + 52, 0, "STEPPED NEON", 84.f},
    {"CATHEDRAL", LOOK_ART + 53, 0, "EBONY GOLD CONSOLE", 120.f},
    {"CHROME DRIP", LOOK_ART + 54, 0, "STALKER 6U SILVER", 84.f},
    {"METATRON", LOOK_ART + 55, 0, "POD GOLD", 65.f},
    {"PENROSE", LOOK_ART + 56, 0, "CURVED VIOLET", 100.f},
    {"GLOW FOREST", LOOK_ART + 57, 0, "MYCELIUM LATTICE", 84.f},
    {"BUBBLE BATH", LOOK_ART + 58, 0, "POD BLUE", 65.f},
    {"SHARD SIX", LOOK_ART + 59, 0, "IRIDESCENT ANODIZED", 65.f},
    {"RIPPLE POOL", LOOK_ART + 60, 0, "POWDER COAT TEAL", 65.f},
    {"ATTRACTOR", LOOK_ART + 61, 0, "BLACK PLY STUDIO", 100.f, SW_COCOBOLO, F_SATIN, 0, 0},
    {"SPIRO", LOOK_ART + 62, 0, "PRINTED PLA BLACK", 50.f},
    {"PLASMA TV", LOOK_ART + 63, 0, "ROAD CASE", 84.f, SW_LEOPARD, F_OILED, 0, 0},
    {"WHIRLPOOL", LOOK_ART + 64, 0, "SMOKED VIOLET ACRYLIC", 50.f},
    {"DEEP SEA", LOOK_ART + 65, 0, "BLACKLIGHT POD", 65.f},
    {"NEON GRID CITY", LOOK_ART + 66, 0, "LAB EXTRUSION", 84.f, 0, 0, 14, 0},
    {"SEVENTIES", LOOK_ART + 67, 0, "TIERED WALNUT", 120.f},
    {"GOLDEN SPIRAL", LOOK_ART + 68, 0, "CHERRY STUDIO", 100.f},
};
static const int kThemeN = (int) (sizeof(kThemes) / sizeof(kThemes[0]));
static void applyTheme(SynTHBloKBackdrop* m, int i) {
    i = ((i % kThemeN) + kThemeN) % kThemeN;
    const Theme& t = kThemes[i];
    m->look = t.look; m->colorIdx = t.color; m->depthMm = t.depthMm; m->showBg = true; m->lookOn = true;   // the theme's look shows (a loaded picture waits under BG > OFF)
    m->sideShape = m->sideMat = m->sideWidth = m->openMode = 0;
    m->panelMat = woodToPanel(t.sideMat); m->panelFinish = clamp(t.sideFinish, 0, 2);   // a theme's wood goes on the side panels
    m->earKind = m->panelMat > 0 ? 0 : clamp(t.ear, 0, kEarFinN - 1); m->earShape = clamp(t.earShape, 0, 1);
    m->sideMat = 0; m->caseMat = 0; m->noCase = false;
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
    m->imagePath = path; m->bankImg = 0; m->showBg = true; m->lookOn = false;   // a picture shows in the look's place
    m->fit = 1; m->imgScale = 1.f; m->imgW = m->imgH = 1.f; m->imgX = m->imgY = 0.f;   // snap it in, every time
}
static void useBank(SynTHBloKBackdrop* m, int n) {
    m->imagePath.clear(); m->bankImg = clamp(n, 1, kBankN); m->showBg = true; m->lookOn = false;
    m->fit = 1; m->imgScale = 1.f; m->imgW = m->imgH = 1.f; m->imgX = m->imgY = 0.f;
}
static void loadPreset(SynTHBloKBackdrop* m, const std::string& path) {
    json_error_t err;
    json_t* j = json_load_file(path.c_str(), 0, &err);
    if (!j) return;
    std::string keepTitle = m->title; bool keepShow = m->showTitle, keepFold = m->collapsed;   // the patch's own name (and fold) stay
    m->dataFromJson(j);
    m->title = keepTitle; m->showTitle = keepShow; m->collapsed = keepFold; m->presetPath = path;
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
    void* owner = NULL;   // the module widget: for the status light in the NAME tab
    // OLED v3 (Nov 2026): tabs, the live preview, a row of CATEGORY keys, and the chosen category's choices below as a
    // grid of square keys, in numbered pages (no stepping or scrolling through lists)
    static constexpr float kPad = 3.f, kPrevH = 34.f, kCatH = 13.f, kPageH = 12.f, kGap = 1.f;
    static const int kPages = 3;   // LOOK, CASE, SIDES (the NAME tab came off, Nov 2026)
    static const int kNamePage = 99;   // gone: never the page
    int page = 0, cat[kPages] = {0, 0, 0}, pg = 0, hoverKey = -1, hoverCat = -1, hoverPg = -1, hoverTab = -1;
    bool pgPicked = false;   // set when the person turns pages; until then the page follows the current choice
    struct Item { std::string label; NVGcolor sw; bool hasSw; std::string thumb; bool on; std::function<void()> act; };
    struct Grid { std::vector<Item> items; int cols; float keyH; };
    std::vector<std::string> cats() {
        SynTHBloKBackdrop* m = module;
        if (page == 0) return {"BG", "COLOUR", "IMAGE", "THEME", "PRESET"};
        if (page == 1) return {"CASE", "MATERIAL", "DEPTH", "SHADOW"};
        if (page == 2) {
            if (m && m->earKind > 0) return {"OFF", "PANELS", "EARS", "FINISH", "SHAPE", "MORE"};
            if (m && m->panelMat > 0) return {"OFF", "PANELS", "EARS", "MATERIAL", "SHAPE", "FINISH", "MORE"};
            return {"OFF", "PANELS", "EARS"};
        }
        return {};
    }
    // SIDES: the first three keys are the mode; they act at once (and open the mode's first list)
    int lastPanel = 0, lastEar = 0;
    bool catIsAction(int c) { return page == 2 && c < 3; }
    void catAction(int c) {
        SynTHBloKBackdrop* m = module; if (!m) return;
        if (c == 0) { m->panelMat = m->earKind = 0; }
        if (c == 1) { if (m->panelMat == 0) m->panelMat = lastPanel > 0 ? lastPanel : 1; m->earKind = 0; cat[2] = 3; }
        if (c == 2) { if (m->earKind == 0) m->earKind = lastEar > 0 ? lastEar : 1; m->panelMat = 0; cat[2] = 3; }
        m->themeIdx = -1; pgPicked = false;
    }
    int modeCat() { SynTHBloKBackdrop* m = module; return !m ? 0 : m->earKind > 0 ? 2 : m->panelMat > 0 ? 1 : 0; }
    Grid grid() {
        Grid g; g.cols = 2; g.keyH = 12.f;
        SynTHBloKBackdrop* m = module;
        if (!m) return g;
        auto hand = [=]() { m->themeIdx = -1; };
        auto add = [&](const std::string& l, bool on, std::function<void()> f) { Item it; it.label = l; it.hasSw = false; it.on = on; it.act = f; g.items.push_back(it); };
        auto addSw = [&](NVGcolor c, bool on, std::function<void()> f) { Item it; it.sw = c; it.hasSw = true; it.on = on; it.act = f; g.items.push_back(it); };
        auto addThumb = [&](const std::string& t, bool on, std::function<void()> f) { Item it; it.thumb = t; it.hasSw = false; it.on = on; it.act = f; g.items.push_back(it); };
        int c = cat[page];
        if (page == 0) { static const int kLookCat[5] = {1, 2, 3, 0, 4}; c = kLookCat[clamp(c, 0, 4)]; }
        if (page == 0) {
            if (c == 0) { for (int i : SHOW_ORDER(kOrderTheme, kThemeN)) add(kThemes[i].name, m->themeIdx == i, [=]() { applyTheme(m, i); }); }
            else if (c == 1) {
                // OFF leads every page: the look goes, and a loaded picture (IMAGE) shows in its place
                std::vector<int> ord = SHOW_ORDER(kOrderLook, kLookN);
                int pp = std::max(2, perPage(g));
                for (size_t q = 0; q < ord.size(); q++) {
                    if ((int) (g.items.size() % pp) == 0) add("OFF", !m->lookOn || !m->showBg, [=]() { m->lookOn = false; m->showBg = true; hand(); });
                    int i = ord[q];
                    add(kLookShort[i], m->showBg && m->lookOn && m->look == i, [=]() { m->look = i; m->showBg = true; m->lookOn = true; hand(); });
                }
            }
            else if (c == 2) { g.cols = 4; g.keyH = 20.f; for (int i : SHOW_ORDER(kOrderColor, kColorN)) addSw(kColor[i], m->colorIdx == i, [=]() { m->colorIdx = i; hand(); }); }
            else if (c == 3) {   // IMAGE: the bank of built-in pictures, then the person's own (MY IMAGES), and loading one
                g.cols = 3; g.keyH = 22.f;
                bool none = m->imagePath.empty() && m->bankImg <= 0;
                add("NONE", none, [=]() { m->imagePath.clear(); m->bankImg = 0; m->lookOn = true; });
                add("LOAD FILE", false, [=]() {
                    osdialog_filters* f = osdialog_filters_parse("Images:png,jpg,jpeg,bmp,gif");
                    char* path = osdialog_file(OSDIALOG_OPEN, myImagesDir().c_str(), NULL, f);
                    osdialog_filters_free(f);
                    if (path) { useImage(m, path); std::free(path); }
                });
                add("MY FOLDER", false, [=]() { system::openDirectory(myImagesDir()); });
                for (int i : SHOW_ORDER(kOrderBank, kBankN + 1)) if (i > 0) addThumb(bankThumbPath(i), m->bankImg == i && m->imagePath.empty(), [=]() { useBank(m, i); });
                for (const std::string& p : myImages()) add(string::uppercase(system::getStem(p)), m->imagePath == p, [=]() { useImage(m, p); });
            }
            else {   // PRESET
                add("SAVE AS...", false, [=]() { savePresetAs(m); });
                add("OPEN FOLDER", false, [=]() { system::openDirectory(presetsDir()); });
                for (const std::string& p : myPresets()) add(string::uppercase(system::getStem(p)), m->presetPath == p, [=]() { loadPreset(m, p); });
            }
        }
        else if (page == 1) {
            const std::vector<CaseStyle>& lib = caseLibrary();
            if (c == 0) { for (int i : SHOW_ORDER(kOrderCase, (int) lib.size())) add(lib[i].name, m->caseIdx == i && m->earKind == 0, [=]() { m->caseIdx = i; m->earKind = 0; hand(); }); }
            else if (c == 1) {
                add("ITS OWN", m->caseMat == 0, [=]() { m->caseMat = 0; hand(); });
                for (int i : SHOW_ORDER(kOrderPanel, kPanelNoizee)) if (i > 0) add(kPanelMat[i].name, m->caseMat == i, [=]() { m->caseMat = i; hand(); });
            }
            else if (c == 2) { g.cols = 3; g.keyH = 16.f; for (int k = 0; k < 9; k++) { float v = kDepthMm[k]; add(k ? string::f("%d MM", (int) v) : std::string("FLAT"), std::fabs(m->depthMm - v) < 0.5f, [=]() { m->depthMm = v; hand(); }); } }
            else { g.keyH = 16.f; add("SHADOW ON", m->shadow, [=]() { m->shadow = true; }); add("SHADOW OFF", !m->shadow, [=]() { m->shadow = false; }); }
        }
        else if (page == 2) {
            bool ears = m->earKind > 0;
            int k = c - 3;   // the categories after the three mode keys
            if (ears) {
                if (k == 0) { for (int i : SHOW_ORDER(kOrderEar, kEarFinN)) if (i > 0) add(kEarFin[i].name, m->earKind == i, [=]() { m->earKind = i; lastEar = i; hand(); }); }
                else if (k == 1) { g.keyH = 16.f; add("STRAIGHT", m->earShape == 0, [=]() { m->earShape = 0; }); add("CONTOURED", m->earShape == 1, [=]() { m->earShape = 1; }); }
                else if (k == 2) {
                    g.cols = 1; g.keyH = 14.f;
                    add(m->showBg ? "BACKGROUND: ON" : "BACKGROUND: OFF (BARE RAILS)", m->showBg, [=]() { m->showBg = !m->showBg; hand(); });
                    if (isNoizee(m->earKind)) add(m->earName ? "NAME ON EAR: ON" : "NAME ON EAR: OFF", m->earName, [=]() { m->earName = !m->earName; });
                }
            }
            else if (m->panelMat > 0) {
                if (k == 0) { for (int i : SHOW_ORDER(kOrderPanel, kPanelMatN)) if (i > 0) add(kPanelMat[i].name, m->panelMat == i, [=]() { m->panelMat = i; lastPanel = i; hand(); }); }
                else if (k == 1) { g.cols = 3; g.keyH = 16.f; static const char* const sh[3] = {"SQUARE", "SEMI", "ROUND"};
                    for (int i = 0; i < 3; i++) { int v = kPanelShapeOrder[i]; add(sh[i], m->panelShape == v, [=]() { m->panelShape = v; }); } }
                else if (k == 2) { g.cols = 3; g.keyH = 16.f; for (int i = 0; i < 3; i++) add(kSideFinish[i], m->panelFinish == i, [=]() { m->panelFinish = i; }); }
                else if (k == 3) {
                    g.cols = 1; g.keyH = 14.f;
                    add(m->noCase ? "CASE: OFF (RAILS ONLY)" : "CASE: ON", !m->noCase, [=]() { m->noCase = !m->noCase; hand(); });
                    add(m->showBg ? "BACKGROUND: ON" : "BACKGROUND: OFF", m->showBg, [=]() { m->showBg = !m->showBg; hand(); });
                    if (noizeePanelArt(m->panelMat)) add(m->earName ? "NAME ON PANEL: ON" : "NAME ON PANEL: OFF", m->earName, [=]() { m->earName = !m->earName; });
                }
            }
        }
        else {   // NAME: the field rides at the top of the grid area
            g.keyH = 16.f;
            add("SHOW ON RACK", m->showTitle, [=]() { m->showTitle = true; });
            add("HIDE", !m->showTitle, [=]() { m->showTitle = false; });
            add("USE FILE NAME", false, [=]() { m->title.clear(); });
        }
        return g;
    }
    std::function<void()> onCollapse;
    // geometry
    // the four tabs (Nov 2026): square keys with contoured (rounded) corners, in a row across the top
    static constexpr float kTabGap = 2.f, kTabR = 4.f;
    float tabSide() { return (box.size.x - 2.f * kPad - 3.f * kTabGap) / 4.f; }   // the size of four across, so they stay small
    math::Rect tabRect(int k) {   // the three, centred
        float s = tabSide(), gap = (box.size.x - 2.f * kPad - kPages * s) / (kPages - 1);
        return math::Rect(Vec(kPad + k * (s + gap), 1.f), Vec(s, s));
    }
    float tabH() { return tabSide() + 1.f; }
    float catY() { return tabH() + 3.f + kPrevH + 3.f; }
    float gridY() { return page == kNamePage ? catY() + 16.f : catY() + kCatH + 9.f; }
    float pagerY() { return box.size.y - 8.f - 2.f - kPageH - 1.f; }   // above the COLLAPSE / FILL row
    int perPage(const Grid& g) { int rows = std::max(1, (int) ((pagerY() - 3.f - gridY() + kGap) / (g.keyH + kGap))); return rows * g.cols; }
    int pageCount(const Grid& g) { int pp = perPage(g); return std::max(1, ((int) g.items.size() + pp - 1) / pp); }
    int curPage(const Grid& g) {
        int n = pageCount(g);
        if (!pgPicked) {   // follow the current choice
            for (int i = 0; i < (int) g.items.size(); i++) if (g.items[i].on) { pg = i / perPage(g); break; }
        }
        return clamp(pg, 0, n - 1);
    }
    math::Rect keyRect(const Grid& g, int idxOnPage) {
        float iw = box.size.x - 2.f * kPad, kw = iw / g.cols;
        int r = idxOnPage / g.cols, c = idxOnPage % g.cols;
        return math::Rect(Vec(kPad + c * kw + kGap * 0.5f, gridY() + r * (g.keyH + kGap)), Vec(kw - kGap, g.keyH));
    }
    std::vector<math::Rect> catRects() {
        std::vector<std::string> cs = cats(); std::vector<math::Rect> r;
        float iw = box.size.x - 2.f * kPad, kw = cs.empty() ? 0.f : iw / cs.size();
        for (size_t i = 0; i < cs.size(); i++) r.push_back(math::Rect(Vec(kPad + i * kw + kGap * 0.5f, catY()), Vec(kw - kGap, kCatH)));
        return r;
    }
    std::vector<math::Rect> pagerRects(int n) {   // ‹ 1 2 ... n ›
        std::vector<math::Rect> r; int k = n + 2;
        float iw = box.size.x - 2.f * kPad, kw = iw / k;
        for (int i = 0; i < k; i++) r.push_back(math::Rect(Vec(kPad + i * kw + kGap * 0.5f, pagerY()), Vec(kw - kGap, kPageH)));
        return r;
    }
    float tabW() { return box.size.x / kPages; }
    void step() override {
        if (name) {   // the name field lives at the top of the NAME page's grid area
            name->visible = page == kNamePage && visible;
            name->box.pos = box.pos.plus(Vec(kPad, catY()));
            name->box.size = Vec(box.size.x - 2.f * kPad, 12.f);
        }
        OpaqueWidget::step();
    }
    void hit(Vec p, int& tab, int& c, int& key, int& pgk) {
        tab = c = key = pgk = -1;
        if (p.y < tabH()) { for (int k = 0; k < kPages; k++) if (tabRect(k).contains(p)) tab = k; return; }
        std::vector<math::Rect> cr = catRects();
        for (size_t i = 0; i < cr.size(); i++) if (cr[i].contains(p)) { c = (int) i; return; }
        Grid g = grid(); int n = pageCount(g), cp = curPage(g), pp = perPage(g);
        if (n > 1) { std::vector<math::Rect> pr = pagerRects(n); for (size_t i = 0; i < pr.size(); i++) if (pr[i].contains(p)) { pgk = (int) i; return; } }
        for (int i = 0; i < pp && cp * pp + i < (int) g.items.size(); i++) if (keyRect(g, i).contains(p)) { key = cp * pp + i; return; }
    }
    // NAME page, very bottom: the small COLLAPSE FILL row (label, then BLANK RIGHT / BLANK LEFT / EMPTY)
    static constexpr float kFillH = 8.f;
    float fillY() { return box.size.y - kFillH - 2.f; }
    math::Rect fillRect(int i) {   // i: 0 the COLLAPSE key, 1..3 the fill keys
        float x0 = kPad, iw = box.size.x - 2.f * kPad, lw = iw * 0.28f, kw = (iw - lw) / 3.f;
        if (i == 0) return math::Rect(Vec(x0, fillY()), Vec(lw, kFillH));
        return math::Rect(Vec(x0 + lw + (i - 1) * kw + kGap * 0.5f, fillY()), Vec(kw - kGap, kFillH));
    }
    int hoverFill = -1;
    int fillHit(Vec p) { for (int i = 0; i <= 3; i++) if (fillRect(i).contains(p)) return i; return -1; }
    void onButton(const ButtonEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS) { OpaqueWidget::onButton(e); return; }
        e.consume(this);
        int f = fillHit(e.pos);
        if (f == 0) { pendingAct = [=]() { if (onCollapse) onCollapse(); }; return; }   // on release: it rebuilds the module's face
        if (f > 0) { if (module) { static const int v[4] = {0, 1, 2, 0}; module->blankFill = v[f]; } return; }
        int tab, c, key, pgk; hit(e.pos, tab, c, key, pgk);
        if (tab >= 0) {
            page = tab; pgPicked = false;
            if (page == kNamePage && name) { step(); name->visible = true; nameWanted = true; }   // NAME: start typing
            return;
        }
        if (c >= 0) { if (catIsAction(c)) catAction(c); else { cat[page] = c; pgPicked = false; } return; }
        Grid g = grid();
        if (pgk >= 0) {
            int n = pageCount(g), cp = curPage(g);
            pg = pgk == 0 ? (cp + n - 1) % n : pgk == n + 1 ? (cp + 1) % n : pgk - 1; pgPicked = true;
            return;
        }
        if (key >= 0 && key < (int) g.items.size() && g.items[key].act) { pendingAct = g.items[key].act; }   // run on release (dialogs)
    }
    std::function<void()> pendingAct;
    bool nameWanted = false;   // select the name field once the click is over, so the click doesn't take it back
    void onDragEnd(const DragEndEvent& e) override {
        if (pendingAct) { std::function<void()> f = pendingAct; pendingAct = nullptr; f(); }
        if (nameWanted && name) { nameWanted = false; APP->event->setSelectedWidget(name); }
        OpaqueWidget::onDragEnd(e);
    }
    void onHover(const HoverEvent& e) override { hit(e.pos, hoverTab, hoverCat, hoverKey, hoverPg); hoverFill = fillHit(e.pos); OpaqueWidget::onHover(e); }
    void onLeave(const LeaveEvent& e) override { hoverTab = hoverCat = hoverKey = hoverPg = hoverFill = -1; }
    void onHoverScroll(const HoverScrollEvent& e) override {   // the wheel turns pages (never steps through a list)
        Grid g = grid(); int n = pageCount(g);
        if (n > 1) { int cp = curPage(g); pg = clamp(cp + (e.scrollDelta.y > 0 ? -1 : 1), 0, n - 1); pgPicked = true; }
        e.consume(this);
    }
    // images for the keys (bank thumbnails) and the preview, cached per path
    std::map<std::string, int> thumbs; NVGcontext* thumbVg = NULL;
    int thumbImg(NVGcontext* vg, const std::string& p) {
        if (thumbVg != vg) { thumbs.clear(); thumbVg = vg; }
        auto it = thumbs.find(p);
        if (it != thumbs.end()) return it->second;
        int h = nvgCreateImage(vg, p.c_str(), 0);
        thumbs[p] = h;
        return h;
    }
    // LIVE PREVIEW: a small picture of the look as set: the background (the art itself, or its colour), the case in its
    // colour / material, two rows of modules, the side panels and the rack ears
    int prevImg = -1, prevLook = -1; NVGcontext* prevVg = NULL;
    void drawPreview(NVGcontext* vg, float x, float y, float w, float h) {
        SynTHBloKBackdrop* m = module;
        nvgSave(vg); nvgIntersectScissor(vg, x, y, w, h);
        nvgBeginPath(vg); nvgRect(vg, x, y, w, h);
        NVGcolor bg = m ? kColor[clamp(m->colorIdx, 0, kColorN - 1)] : nvgRGB(10, 10, 12);
        nvgFillColor(vg, nvgLerpRGBA(bg, nvgRGB(0, 0, 0), 0.2f)); nvgFill(vg);
        if (m && m->showBg && m->lookOn && m->look >= LOOK_ART) {
            int k = clamp(m->look - LOOK_ART, 0, kArtN - 1);
            if (prevVg != vg || prevLook != k) {
                if (prevImg >= 0 && prevVg == vg) nvgDeleteImage(vg, prevImg);
                prevImg = nvgCreateImage(vg, asset::plugin(pluginInstance, string::f("res/backdrops/%s.jpg", kArtFile[k])).c_str(), 0);
                prevLook = k; prevVg = vg;
            }
            if (prevImg >= 0) { nvgFillPaint(vg, nvgImagePattern(vg, x, y - (w * 0.625f - h) * 0.5f, w, w * 0.625f, 0.f, prevImg, 1.f)); nvgFill(vg); }
        }
        if (m && m->showBg && !m->lookOn) {   // BG OFF: the picture (the bank's or the person's own), as in the rack
            std::string ip = m->imagePath.empty() && m->bankImg > 0 ? bankThumbPath(m->bankImg) : m->imagePath;
            int img = ip.empty() ? -1 : thumbImg(vg, ip);
            if (img >= 0) {
                int iw = 1, ih = 1; nvgImageSize(vg, img, &iw, &ih);
                float k = std::max(w / iw, h / ih), dw = iw * k, dh = ih * k;
                nvgBeginPath(vg); nvgRect(vg, x, y, w, h);
                nvgFillPaint(vg, nvgImagePattern(vg, x + (w - dw) * 0.5f, y + (h - dh) * 0.5f, dw, dh, 0.f, img, 1.f - m->imgDim)); nvgFill(vg);
            }
        }
        if (m) {
            const std::vector<CaseStyle>& lib = caseLibrary();
            const CaseStyle& cs = lib[clamp(m->caseIdx, 0, (int) lib.size() - 1)];
            NVGcolor cc = m->caseMat > 0 ? nvgRGB(kPanelMat[m->caseMat].r, kPanelMat[m->caseMat].g, kPanelMat[m->caseMat].b) : cs.base;
            float ew = m->earKind > 0 ? 10.f : 0.f, pw = m->panelMat > 0 && m->earKind == 0 ? 4.f : 0.f;
            float cw = w * 0.56f, ch = h * 0.62f, cx = x + (w - cw) * 0.5f, cy = y + (h - ch) * 0.5f;
            if (cs.material != M_NONE && ew == 0.f && !(pw > 0.f && m->noCase)) {   // with ears (or panels, CASE OFF): no case
                nvgBeginPath(vg); nvgRect(vg, cx - 3.f, cy - 2.f, cw + 6.f, ch + 4.f); nvgFillColor(vg, cc); nvgFill(vg);
            }
            nvgBeginPath(vg); nvgRect(vg, cx, cy, cw, ch); nvgFillColor(vg, nvgRGB(14, 14, 15)); nvgFill(vg);
            for (int r = 0; r < 2; r++) {   // two rows of modules on Rack's grey rails
                float ry = cy + r * ch * 0.5f;
                nvgBeginPath(vg); nvgRect(vg, cx, ry, cw, 1.4f); nvgRect(vg, cx, ry + ch * 0.5f - 1.4f, cw, 1.4f); nvgFillColor(vg, nvgRGB(70, 70, 74)); nvgFill(vg);
                float mx = cx + 1.f;
                for (float f : {0.24f, 0.32f, 0.2f}) {
                    nvgBeginPath(vg); nvgRect(vg, mx, ry + 1.f, cw * f - 1.f, ch * 0.5f - 2.f); nvgFillColor(vg, nvgRGB(30, 30, 32)); nvgFill(vg);
                    nvgBeginPath(vg); nvgRect(vg, mx, ry + 2.6f, cw * f - 1.f, 0.6f); nvgFillColor(vg, nvgRGB(255, 122, 0)); nvgFill(vg);
                    mx += cw * f;
                }
            }
            for (int sd = 0; sd < 2; sd++) {
                float ex = ew > 0.f ? (sd ? cx + cw : cx) : (sd ? cx + cw + 3.f : cx - 3.f);
                if (pw > 0.f) {
                    const PanelMat& pm = kPanelMat[m->panelMat];
                    nvgBeginPath(vg); nvgRoundedRect(vg, sd ? ex : ex - pw, cy - 2.f, pw, ch + 4.f, m->panelShape ? 1.5f : 0.8f);
                    nvgFillColor(vg, nvgRGB(pm.r, pm.g, pm.b)); nvgFill(vg);
                    ex += sd ? pw : -pw;
                }
                if (ew > 0.f) {
                    const EarFin& ef = kEarFin[m->earKind];
                    nvgBeginPath(vg); nvgRect(vg, sd ? ex : ex - ew, cy, ew, ch); nvgFillColor(vg, nvgRGB(ef.r, ef.g, ef.b)); nvgFill(vg);
                    if (isNoizee(m->earKind)) { nvgBeginPath(vg); nvgRect(vg, sd ? ex + 0.8f : ex - 1.6f, cy - 1.f, 0.8f, ch + 2.f); nvgFillColor(vg, nvgRGB(255, 122, 0)); nvgFill(vg); }
                }
            }
        }
        nvgRestore(vg);
        nvgBeginPath(vg); nvgRect(vg, x + 0.3f, y + 0.3f, w - 0.6f, h - 0.6f);
        nvgStrokeColor(vg, oled::outline(80)); nvgStrokeWidth(vg, 0.6f); nvgStroke(vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (font) {
            nvgFontFaceId(vg, font->handle); nvgFontSize(vg, 4.6f); nvgTextLetterSpacing(vg, 0.4f);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP); nvgFillColor(vg, oled::amber(200));
            nvgText(vg, x + 2.f, y + 1.5f, "LIVE PREVIEW", NULL);
        }
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
    // a square key: softly backlit, brighter when hovered, brightest (with a lit rim) when on
    void keyBox(NVGcontext* vg, math::Rect r, bool on, bool hot, float radius = 0.f) {
        nvgBeginPath(vg);
        if (radius > 0.f) nvgRoundedRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, radius); else nvgRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
        nvgFillColor(vg, oled::amber(on ? 150 : hot ? 52 : 30)); nvgFill(vg);
        nvgStrokeColor(vg, oled::amber(on ? 235 : hot ? 150 : 90)); nvgStrokeWidth(vg, 0.6f); nvgStroke(vg);
    }
    void keyText(NVGcontext* vg, math::Rect r, const std::string& t, bool on, float size) {
        nvgFontSize(vg, size); nvgTextLetterSpacing(vg, 0.15f);
        float bw = nvgTextBounds(vg, 0, 0, t.c_str(), NULL, NULL);
        if (bw > r.size.x - 2.f) { nvgFontSize(vg, size * 0.82f); nvgTextLetterSpacing(vg, 0.f); }
        nvgSave(vg); nvgIntersectScissor(vg, r.pos.x + 0.5f, r.pos.y, r.size.x - 1.f, r.size.y);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, on ? nvgRGB(255, 246, 225) : oled::amber());
        nvgText(vg, r.pos.x + r.size.x * 0.5f, r.pos.y + r.size.y * 0.5f + 0.3f, t.c_str(), NULL);
        nvgRestore(vg);
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        NVGcontext* vg = args.vg;
        nvgFontFaceId(vg, font->handle);
        float W = box.size.x;
        // tabs
        static const char* const tabs[kPages] = {"LOOK", "CASE", "SIDES"};
        for (int k = 0; k < kPages; k++) {
            math::Rect r = tabRect(k);
            keyBox(vg, r, k == page, k == hoverTab, kTabR);
            if (k == kNamePage) {   // the status light: amber while this BACKDROP dresses the rack, red if another one does
                bool mine = !module || gOwner == owner;
                float cx = r.pos.x + r.size.x * 0.5f, cy = r.pos.y + r.size.y * 0.3f;
                NVGcolor lc = mine ? nvgRGB(255, 190, 90) : nvgRGB(220, 50, 35);
                nvgBeginPath(vg); nvgCircle(vg, cx, cy, 4.f);
                nvgFillPaint(vg, nvgRadialGradient(vg, cx, cy, 0.8f, 4.f, nvgTransRGBA(lc, 150), nvgRGBA(0, 0, 0, 0))); nvgFill(vg);
                nvgBeginPath(vg); nvgCircle(vg, cx, cy, 1.5f); nvgFillColor(vg, lc); nvgFill(vg);
                r.pos.y += r.size.y * 0.25f;
            }
            keyText(vg, r, tabs[k], k == page, 6.6f);
        }
        drawPreview(vg, kPad, tabH() + 3.f, W - 2.f * kPad, kPrevH);
        // categories
        std::vector<std::string> cs = cats(); std::vector<math::Rect> cr = catRects();
        int mc = modeCat();
        for (size_t i = 0; i < cs.size(); i++) {
            bool on = catIsAction((int) i) ? (int) i == mc : (int) i == cat[page];
            if (page == 2 && !catIsAction((int) i) && (int) i == cat[page]) on = true;
            keyBox(vg, cr[i], on, (int) i == hoverCat);
            keyText(vg, cr[i], cs[i], on, 5.6f);
        }
        if (page == 2 && cat[2] >= (int) cs.size()) cat[2] = (int) cs.size() > 3 ? 3 : 0;
        // the chosen category's name and the current pick, over the grid
        Grid g = grid();
        if (!cs.empty() && page != kNamePage) {
            std::string what = catIsAction(cat[page]) && page == 2 ? "SIDES" : cs[clamp(cat[page], 0, (int) cs.size() - 1)];
            std::string cur;
            for (const Item& it : g.items) if (it.on && !it.label.empty()) { cur = it.label; break; }
            nvgFontSize(vg, 5.2f); nvgTextLetterSpacing(vg, 0.4f); nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, oled::dim());
            nvgText(vg, kPad + 1.f, catY() + kCatH + 4.5f, (std::string("\xe2\x96\xbe ") + what + (cur.empty() ? "" : "  \xc2\xb7  " + cur)).c_str(), NULL);
        }
        if (page == 2 && g.items.empty()) {   // SIDES with nothing open: say what the keys do
            nvgFontSize(vg, 5.6f); nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE); nvgFillColor(vg, oled::outline(150));
            nvgText(vg, W * 0.5f, gridY() + 20.f, mc == 0 ? "PICK PANELS OR EARS" : "PICK A KEY ABOVE", NULL);
        }
        // the grid (this page of it)
        int n = pageCount(g), cp = curPage(g), pp = perPage(g);
        for (int i = 0; i < pp && cp * pp + i < (int) g.items.size(); i++) {
            const Item& it = g.items[cp * pp + i];
            math::Rect r = keyRect(g, i);
            bool hot = cp * pp + i == hoverKey;
            if (it.hasSw) {   // a colour swatch
                nvgBeginPath(vg); nvgRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y); nvgFillColor(vg, it.sw); nvgFill(vg);
                nvgStrokeColor(vg, it.on ? nvgRGB(255, 240, 210) : oled::amber(hot ? 180 : 70)); nvgStrokeWidth(vg, it.on ? 1.6f : 0.6f); nvgStroke(vg);
                continue;
            }
            if (!it.thumb.empty()) {   // a picture from the bank
                int img = thumbImg(vg, it.thumb);
                nvgBeginPath(vg); nvgRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
                if (img >= 0) {
                    int iw = 1, ih = 1; nvgImageSize(vg, img, &iw, &ih);
                    float k = std::max(r.size.x / iw, r.size.y / ih), dw = iw * k, dh = ih * k;
                    nvgSave(vg); nvgIntersectScissor(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
                    nvgFillPaint(vg, nvgImagePattern(vg, r.pos.x + (r.size.x - dw) * 0.5f, r.pos.y + (r.size.y - dh) * 0.5f, dw, dh, 0.f, img, 1.f)); nvgFill(vg);
                    nvgRestore(vg);
                }
                nvgBeginPath(vg); nvgRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
                nvgStrokeColor(vg, it.on ? nvgRGB(255, 240, 210) : oled::amber(hot ? 200 : 60)); nvgStrokeWidth(vg, it.on ? 1.6f : 0.6f); nvgStroke(vg);
                continue;
            }
            keyBox(vg, r, it.on, hot);
            keyText(vg, r, it.label, it.on, 5.4f);
        }
        // the pager: ‹ 1 2 ... ›, the page shown lit
        if (n > 1) {
            std::vector<math::Rect> pr = pagerRects(n);
            for (int i = 0; i < (int) pr.size(); i++) {
                bool on = i >= 1 && i <= n && i - 1 == cp;
                keyBox(vg, pr[i], on, i == hoverPg);
                keyText(vg, pr[i], i == 0 ? "\xe2\x80\xb9" : i == n + 1 ? "\xe2\x80\xba" : string::f("%d", i), on, 6.f);
            }
        }
        else if (module && gOwner && gOwner != owner) {
            nvgFontSize(vg, 5.f); nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE); nvgFillColor(vg, nvgRGB(220, 70, 50));
            nvgText(vg, W * 0.5f, pagerY() + kPageH * 0.5f, "ANOTHER BACKDROP IS DRESSING THE RACK", NULL);
        }
        {   // the small bottom row on every page: COLLAPSE, then what fills the gap it leaves
            static const char* const fl[4] = {"\xe2\x80\xba COLLAPSE \xe2\x80\xb9", "FILL: BLANK R", "FILL: BLANK L", "LEAVE EMPTY"};
            static const int fv[4] = {-1, 1, 2, 0};
            keyBox(vg, fillRect(0), false, hoverFill == 0, 1.5f);
            keyText(vg, fillRect(0), fl[0], false, 4.4f);
            for (int i = 1; i <= 3; i++) {
                bool on = module && module->blankFill == fv[i];
                math::Rect r = fillRect(i);
                keyBox(vg, r, on, i == hoverFill);
                keyText(vg, r, fl[i], on, 4.2f);
            }
        }
        nvgTextLetterSpacing(vg, 0.f);
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

// COLLAPSED (Nov 2026): BACKDROP folds to 7 HP (the width of its title): the modules' face, and down the middle a tall
// strip showing the rack's background (the picture, the art, or the colour), with an EXPAND key at the foot.
struct BackdropFace : widget::TransparentWidget {
    SynTHBloKBackdrop* module = NULL;
    int img = -1; std::string imgPath; NVGcontext* imgVg = NULL;
    void draw(const DrawArgs& args) override {
        NVGcontext* vg = args.vg;
        float w = box.size.x, h = box.size.y;
        nvgBeginPath(vg); nvgRect(vg, 0, 0, w, h); nvgFillColor(vg, nvgRGB(0x11, 0x11, 0x11)); nvgFill(vg);
        nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 0, h, nvgRGBA(255, 255, 255, 9), nvgRGBA(0, 0, 0, 41))); nvgFill(vg);
        nvgBeginPath(vg); nvgRect(vg, 0, 0, w, mm2px(7.f)); nvgFillColor(vg, nvgRGB(8, 8, 8)); nvgFill(vg);
        nvgBeginPath(vg); nvgRect(vg, 0, mm2px(7.f) - 0.45f, w, 0.9f); nvgFillColor(vg, nvgRGBA(255, 122, 0, 190)); nvgFill(vg);
        nvgBeginPath(vg); nvgRoundedRect(vg, mm2px(2.5f), mm2px(9.f), w - mm2px(5.f), mm2px(116.5f), mm2px(1.2f));
        nvgFillColor(vg, nvgRGB(0x16, 0x16, 0x16)); nvgFill(vg);
        nvgStrokeColor(vg, nvgRGB(0x2c, 0x2c, 0x2c)); nvgStrokeWidth(vg, mm2px(0.25f)); nvgStroke(vg);
        // the strip: what is on the background now
        SynTHBloKBackdrop* m = module;
        float sx = mm2px(4.2f), sy = mm2px(21.5f), sw = w - 2.f * sx, sh = mm2px(102.f);   // under the EXPAND key
        NVGcolor col = m ? kColor[clamp(m->colorIdx, 0, kColorN - 1)] : nvgRGB(40, 20, 60);
        nvgBeginPath(vg); nvgRect(vg, sx, sy, sw, sh);
        nvgFillPaint(vg, nvgLinearGradient(vg, 0, sy, 0, sy + sh, nvgLerpRGBA(col, nvgRGB(255, 255, 255), 0.1f), nvgLerpRGBA(col, nvgRGB(0, 0, 0), 0.5f))); nvgFill(vg);
        std::string p;
        if (m && m->showBg) {
            if (!m->lookOn) p = bgImagePath(m);
            else if (m->look >= LOOK_ART) p = asset::plugin(pluginInstance, string::f("res/backdrops/%s.jpg", kArtFile[clamp(m->look - LOOK_ART, 0, kArtN - 1)]));
        }
        if (!p.empty()) {
            if (imgVg != vg || imgPath != p) { if (img >= 0 && imgVg == vg) nvgDeleteImage(vg, img); img = nvgCreateImage(vg, p.c_str(), 0); imgPath = p; imgVg = vg; }
            if (img >= 0) {
                int iw = 1, ih = 1; nvgImageSize(vg, img, &iw, &ih);
                float k = std::max(sw / iw, sh / ih), dw = iw * k, dh = ih * k;
                nvgSave(vg); nvgIntersectScissor(vg, sx, sy, sw, sh);
                nvgBeginPath(vg); nvgRect(vg, sx, sy, sw, sh);
                nvgFillPaint(vg, nvgImagePattern(vg, sx + (sw - dw) * 0.5f, sy + (sh - dh) * 0.5f, dw, dh, 0.f, img, 1.f)); nvgFill(vg);
                nvgRestore(vg);
            }
        }
        nvgBeginPath(vg); nvgRect(vg, sx, sy, sw, sh); nvgStrokeColor(vg, nvgRGBA(255, 190, 90, 90)); nvgStrokeWidth(vg, 0.8f); nvgStroke(vg);
        if (std::shared_ptr<window::Font> font = APP->window->uiFont) {
            nvgFontFaceId(vg, font->handle); nvgFontSize(vg, mm2px(1.944f) * 1.4f); nvgTextLetterSpacing(vg, 0.f);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
            nvgFillColor(vg, nvgRGB(0x80, 0x80, 0x80)); nvgText(vg, w * 0.5f, mm2px(127.9f), "NoiZee ToyZ", NULL);
        }
        Widget::draw(args);
    }
};
// the EXPAND key on the collapsed face (and nothing else there takes clicks, so the module still drags)
// COLLAPSE / EXPAND (Nov 2026): one small key in the gap between the OLED well and the card's bottom edge, in the same
// place on the full module and on the folded face; it says what pressing it does
struct BackdropExpand : widget::OpaqueWidget {
    std::function<void()> act;
    std::function<bool()> folded;
    bool hot = false;
    void onButton(const ButtonEvent& e) override { if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) e.consume(this); }
    void onDragEnd(const DragEndEvent& e) override { if (e.button == GLFW_MOUSE_BUTTON_LEFT && act) act(); }
    void onEnter(const EnterEvent& e) override { hot = true; }
    void onLeave(const LeaveEvent& e) override { hot = false; }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        NVGcontext* vg = args.vg;
        // folded only (the full module's COLLAPSE key lives in the OLED's bottom row): a square key, arrows over EXPAND
        float W = box.size.x, H = box.size.y;
        nvgBeginPath(vg); nvgRoundedRect(vg, 0.3f, 0.3f, W - 0.6f, H - 0.6f, 4.f);
        nvgFillColor(vg, oled::amber(hot ? 90 : 40)); nvgFill(vg);
        nvgStrokeColor(vg, oled::amber(hot ? 235 : 130)); nvgStrokeWidth(vg, 0.7f); nvgStroke(vg);
        if (std::shared_ptr<window::Font> font = APP->window->uiFont) {
            nvgFontFaceId(vg, font->handle); nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE); nvgFillColor(vg, oled::amber());
            nvgFontSize(vg, 12.f); nvgTextLetterSpacing(vg, 2.f); nvgText(vg, W * 0.5f, H * 0.4f, "\xe2\x80\xb9 \xe2\x80\xba", NULL);
            nvgFontSize(vg, 5.f); nvgTextLetterSpacing(vg, 0.4f); nvgText(vg, W * 0.5f, H * 0.74f, "EXPAND", NULL);
        }
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
        face = new BackdropFace;   // shown only when collapsed
        face->module = module;
        face->box.size = box.size;
        face->visible = false;
        addChild(face);
        expand = new BackdropExpand;
        expand->box.size = mm2px(Vec(10.f, 10.f));   // folded only: a square key at the top, the picture strip below it
        expand->box.pos = Vec(0.f, mm2px(10.f));
        expand->act = [=]() { SynTHBloKBackdrop* m = dynamic_cast<SynTHBloKBackdrop*>(this->module); if (m) setCollapsed(!m->collapsed); };
        expand->folded = [=]() { SynTHBloKBackdrop* m = dynamic_cast<SynTHBloKBackdrop*>(this->module); return m && m->collapsed; };
        addChild(expand);
        addChild(createWidget<BackdropTitle>(mm2px(Vec(8.5f, 0.f))));
        BackdropOled* om = new BackdropOled;   // the OLED menu fills the well; the name field rides in its NAME page
        om->module = module;
        om->box.pos = mm2px(Vec(5.6f, 12.6f));
        om->box.size = mm2px(Vec(49.76f, 107.8f));   // the well runs down to just above the bottom screws
        om->owner = this;
        om->onCollapse = [=]() { setCollapsed(true); };
        addChild(om);
        oled = om;
        // four screws, 3.5 mm in from each corner, as on every module
        for (int k = 0; k < 4; k++) {
            screws[k] = createWidgetCentered<SocketScrew>(mm2px(Vec((k & 1) ? 60.96f - 3.5f : 3.5f, k < 2 ? 3.5f : 125.f)));
            addChild(screws[k]);
        }
        fold(false);
    }
    // COLLAPSE / EXPAND: 12 HP <-> 7 HP. Expanding pushes the neighbours aside if they are in the way.
    BackdropFace* face = NULL; BackdropExpand* expand = NULL; BackdropOled* oled = NULL; NameOled* nameField = NULL;
    widget::Widget* screws[4] = {};
    // Collapsing leaves a 5 HP void; with FILL on, a BLANK is made there (right of BACKDROP, or left: then BACKDROP
    // moves right so the blank takes its old left edge). That blank is locked to this BACKDROP and to the void's size;
    // expanding removes it (and, if it was on the left, BACKDROP moves back to its old place).
    void setCollapsed(bool c) {
        SynTHBloKBackdrop* m = dynamic_cast<SynTHBloKBackdrop*>(module);
        if (!m || m->collapsed == c || !APP->scene || !APP->scene->rack) return;
        float gap = RACK_GRID_WIDTH * 5.f;
        if (c) {
            Vec p = box.pos;
            m->collapsed = true;
            foldHp = 7; growTick = 0;
            if (m->blankFill == 2) box.pos.x += gap;   // the blank goes on the left: BACKDROP shifts right into place
            fold(false);
            m->blankId = -1;
            if (m->blankFill > 0 && modelSynTHBloKBlank) {
                engine::Module* bm = modelSynTHBloKBlank->createModule();
                json_t* j = json_object();   // 5 HP, locked to this BACKDROP
                json_object_set_new(j, "hp", json_integer(5));
                json_object_set_new(j, "lockedTo", json_integer(m->id));
                bm->dataFromJson(j);
                json_decref(j);
                APP->engine->addModule(bm);
                app::ModuleWidget* bw = modelSynTHBloKBlank->createModuleWidget(bm);
                bw->box.pos = m->blankFill == 2 ? p : Vec(p.x + RACK_GRID_WIDTH * 7.f, p.y);
                APP->scene->rack->addModule(bw);
                m->blankId = bm->id;
            }
        }
        else {
            app::ModuleWidget* bw = m->blankId >= 0 ? APP->scene->rack->getModule(m->blankId) : NULL;
            if (bw && m->blankFill == 2 && bw->box.pos.x < box.pos.x) box.pos.x = bw->box.pos.x;   // back to its old left edge
            if (bw) bw->removeAction();
            m->blankId = -1;
            m->collapsed = false;
            fold(true);
        }
    }
    // COLLAPSED, IT FILLS THE GAP (Nov 2026): the folded face stretches by itself over the free space to its right, from
    // 7 HP up to its full 12 HP; while any module is being dragged (or added from the browser) it drops back to 7 HP so
    // there is room to put one there, then fills whatever is left
    bool folded = false;
    int foldHp = 7, growTick = 0;
    void growStep() {
        if (!APP->scene || !APP->scene->rack || ++growTick < 6) return;
        growTick = 0;
        int want = 7;
        bool dragging = APP->event && dynamic_cast<app::ModuleWidget*>(APP->event->draggedWidget) != NULL;
        if (!dragging) {
            float nx = box.pos.x + RACK_GRID_WIDTH * 12.f;   // never wider than the full module
            for (app::ModuleWidget* mw : APP->scene->rack->getModules()) {
                if (mw == this || std::fabs(mw->box.pos.y - box.pos.y) > 1.f) continue;
                if (mw->box.pos.x > box.pos.x + 1.f) nx = std::min(nx, mw->box.pos.x);
            }
            want = clamp((int) std::floor((nx - box.pos.x) / RACK_GRID_WIDTH + 0.01f), 7, 12);
        }
        if (want != foldHp) { foldHp = want; fold(false); }
    }
    void fold(bool push) {
        SynTHBloKBackdrop* m = dynamic_cast<SynTHBloKBackdrop*>(module);
        bool c = m && m->collapsed;
        folded = c;
        float w = RACK_GRID_WIDTH * (c ? (float) foldHp : 12.f);
        if (box.size.x != w) {
            box.size.x = w;
            if (push && APP->scene && APP->scene->rack) APP->scene->rack->setModulePosForce(this, box.pos);
        }
        if (getPanel()) getPanel()->visible = !c;
        if (oled) oled->visible = !c;
        if (nameField && c) nameField->visible = false;
        if (face) { face->visible = c; face->box.size = box.size; }
        if (expand) { expand->visible = c; expand->box.pos.x = (w - expand->box.size.x) * 0.5f; }
        for (int k = 0; k < 4; k++) if (screws[k]) screws[k]->box.pos.x = ((k & 1) ? w - mm2px(3.5f) : mm2px(3.5f)) - screws[k]->box.size.x * 0.5f;
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
        if (folded != m->collapsed) fold(false);   // a patch or preset changed it
        if (m->collapsed) growStep();
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
        menu->addChild(createMenuItem(m->collapsed ? "Expand BACKDROP" : "Collapse BACKDROP (7 HP)", "", [=]() { setCollapsed(!m->collapsed); }));
        menu->addChild(createIndexSubmenuItem("When collapsed, fill the 5 HP with", {"Nothing", "A BLANK on the right", "A BLANK on the left"},
            [=]() { return (size_t) m->blankFill; }, [=](size_t i) { m->blankFill = (int) i; }));
        menu->addChild(createSubmenuItem("Image bank", m->bankImg > 0 && m->imagePath.empty() ? string::f("#%d", m->bankImg) : "", [=](Menu* sub) {
            sub->addChild(createCheckMenuItem("None", "", [=]() { return m->bankImg <= 0 || !m->imagePath.empty(); }, [=]() { m->bankImg = 0; }));
            for (int i : SHOW_ORDER(kOrderBank, kBankN + 1)) if (i > 0)
                sub->addChild(createCheckMenuItem(string::f("Picture %d", i), "", [=]() { return m->bankImg == i && m->imagePath.empty(); }, [=]() { useBank(m, i); m->themeIdx = -1; }));
        }));
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("BACKGROUND"));
        menu->addChild(createBoolPtrMenuItem("Draw the background", "", &m->showBg));
        menu->addChild(createSubmenuItem("Look", kLookName[clamp(m->look, 0, kLookN - 1)], [=](Menu* sub) {   // darkest first
            sub->addChild(createCheckMenuItem("Off (a loaded picture shows instead)", "", [=]() { return !m->lookOn; }, [=]() { m->lookOn = false; m->themeIdx = -1; }));
            for (int i : SHOW_ORDER(kOrderLook, kLookN))
                sub->addChild(createCheckMenuItem(kLookName[i], "", [=]() { return m->lookOn && m->look == i; }, [=]() { m->look = i; m->lookOn = true; m->themeIdx = -1; }));
        }));
        menu->addChild(createSubmenuItem("Colour", kColorName[clamp(m->colorIdx, 0, kColorN - 1)], [=](Menu* sub) {
            for (int i : SHOW_ORDER(kOrderColor, kColorN))
                sub->addChild(createCheckMenuItem(kColorName[i], "", [=]() { return m->colorIdx == i; }, [=]() { m->colorIdx = i; m->themeIdx = -1; }));
        }));
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
                m->imagePath = path; std::free(path); m->bankImg = 0; m->lookOn = false;
                m->fit = 1; m->imgScale = 1.f; m->imgW = m->imgH = 1.f; m->imgX = m->imgY = 0.f;   // snap it in, every time
            }
        }));
        if (!bgImagePath(m).empty()) {
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
            menu->addChild(createMenuItem("Clear the image", "", [=]() { m->imagePath.clear(); m->bankImg = 0; m->lookOn = true; }));
        }
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("CASE"));
        menu->addChild(createSubmenuItem("Case", caseLibrary()[clamp(m->caseIdx, 0, (int) caseLibrary().size() - 1)].name, [=](Menu* sub) {
            const std::vector<CaseStyle>& lib = caseLibrary();
            for (int i : SHOW_ORDER(kOrderCase, (int) lib.size()))
                sub->addChild(createCheckMenuItem(lib[i].name, "", [=]() { return m->caseIdx == i; }, [=]() { m->caseIdx = i; }));
        }));
        auto matMenu = [=](Menu* sub, int* v, const char* none) {
            bool first = true;
            for (int i : SHOW_ORDER(kOrderPanel, v == &m->caseMat ? kPanelNoizee : kPanelMatN)) {   // the NoiZee plates are panel-only
                if (i > 0 && first) { sub->addChild(new MenuSeparator); sub->addChild(createMenuLabel("Darkest to lightest")); first = false; }
                sub->addChild(createCheckMenuItem(i ? kPanelMat[i].name : none, "", [=]() { return *v == i; }, [=]() { *v = i; m->themeIdx = -1; if (v == &m->panelMat && i > 0) m->earKind = 0; }));
            }
        };
        menu->addChild(createSubmenuItem("Side panels (attachments)", kPanelMat[clamp(m->panelMat, 0, kPanelMatN - 1)].name, [=](Menu* sub) {
            sub->addChild(createSubmenuItem("Shape", kPanelShape[clamp(m->panelShape, 0, 2)], [=](Menu* s2) {
                for (int k = 0; k < 3; k++) { int v = kPanelShapeOrder[k]; s2->addChild(createCheckMenuItem(kPanelShape[v], "", [=]() { return m->panelShape == v; }, [=]() { m->panelShape = v; })); }
            }));
            sub->addChild(createIndexSubmenuItem("Finish", std::vector<std::string>(kSideFinish, kSideFinish + 3), [=]() { return (size_t) m->panelFinish; }, [=](size_t i) { m->panelFinish = (int) i; }));
            sub->addChild(new MenuSeparator);
            sub->addChild(createMenuLabel("Material"));
            matMenu(sub, &m->panelMat, "NONE");
            sub->addChild(new MenuSeparator);
            sub->addChild(createBoolPtrMenuItem("No case between the panels (rails only)", "", &m->noCase));
            sub->addChild(createMenuLabel("Turns the rack ears off (a case has one or the other)"));
        }));
        menu->addChild(createSubmenuItem("Rack ears", kEarFin[clamp(m->earKind, 0, kEarFinN - 1)].name, [=](Menu* sub) {
            for (int i : SHOW_ORDER(kOrderEar, kEarFinN)) {   // NONE, the NoiZee ToyZ ears, then dark to light
                if (i == kOrderEar[1]) sub->addChild(new MenuSeparator);
                sub->addChild(createCheckMenuItem(kEarFin[i].name, "", [=]() { return m->earKind == i; }, [=]() { m->earKind = i; m->themeIdx = -1; if (i > 0) m->panelMat = 0; }));
            }
            sub->addChild(new MenuSeparator);
            sub->addChild(createIndexSubmenuItem("Shape", {"STRAIGHT", "CONTOURED"}, [=]() { return (size_t) m->earShape; }, [=](size_t i) { m->earShape = (int) i; }));
            sub->addChild(createBoolPtrMenuItem("Name on the NOIZEE TOYZ ears", "", &m->earName));
            sub->addChild(createMenuLabel("Ears mean no case: an open rack (turns the side panels off)"));
        }));
        menu->addChild(createSubmenuItem("Case material", m->caseMat > 0 ? kPanelMat[clamp(m->caseMat, 0, kPanelMatN - 1)].name : "its own", [=](Menu* sub) { matMenu(sub, &m->caseMat, "ITS OWN"); }));
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
        menu->addChild(createMenuItem("Reset the case options", "panels, ears, material, depth, shadow", [=]() { m->sideShape = m->sideMat = m->sideWidth = m->openMode = 0; m->panelMat = m->caseMat = m->earKind = 0; m->noCase = false; m->depthMm = 65.f; m->shadow = true; }));
        menu->addChild(createMenuItem("Open the case library folder", "add your own cases in cases.json", [=]() {
            std::string dir = asset::user("SynTHBloK");
            system::createDirectories(dir);
            system::openDirectory(dir);
        }));
    }
};

Model* modelSynTHBloKBackdrop = createModel<SynTHBloKBackdrop, SynTHBloKBackdropWidget>("Backdrop");
