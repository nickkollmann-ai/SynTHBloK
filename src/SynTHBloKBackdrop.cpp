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
#include <map>

namespace {

// ---------------------------------------------------------------------------------------------------------
// THE CASE LIBRARY. A material decides how the frame is drawn; the colours make it this case.
// ---------------------------------------------------------------------------------------------------------
enum Material { M_NONE, M_WOOD, M_ANODIZED, M_BRUSHED, M_POWDER, M_TOLEX, M_CARBON, M_EARS, M_ACRYLIC, M_PRINTED, M_NUM };
// the end shapes: square, slanted cheeks (a case on legs or a tiered cabinet), rounded corners, plastic end caps
enum Ends { E_SQUARE, E_SLANT, E_ROUND, E_CAPS };
// extras on the top lip
enum { X_NONE = 0, X_1U = 1, X_HANDLE = 2, X_LATCHES = 4, X_LEGS = 8 };
struct CaseStyle {
    std::string name;
    int material;
    NVGcolor base, accent, rail;
    float side;    // frame thickness at the ends, px
    int ends;      // Ends
    int extras;    // X_ flags
};
static NVGcolor rgb(int r, int g, int b) { return nvgRGB(r, g, b); }
static std::vector<CaseStyle> builtInCases() {
    return {
        {"NO CASE", M_NONE, rgb(0, 0, 0), rgb(0, 0, 0), rgb(0, 0, 0), 0.f, E_SQUARE, X_NONE},
        {"WALNUT SKIFF", M_WOOD, rgb(92, 58, 34), rgb(60, 36, 20), rgb(46, 46, 48), 16.f, E_SQUARE, X_NONE},
        {"OAK SKIFF", M_WOOD, rgb(168, 124, 78), rgb(128, 90, 52), rgb(52, 52, 54), 16.f, E_SQUARE, X_NONE},
        {"EBONY SKIFF", M_WOOD, rgb(44, 30, 24), rgb(26, 18, 14), rgb(40, 40, 42), 16.f, E_SQUARE, X_NONE},
        {"BLACK ANODIZED", M_ANODIZED, rgb(22, 22, 24), rgb(40, 40, 44), rgb(34, 34, 36), 12.f, E_SQUARE, X_NONE},
        {"BRUSHED ALUMINUM", M_BRUSHED, rgb(160, 165, 170), rgb(120, 125, 130), rgb(110, 112, 116), 12.f, E_SQUARE, X_NONE},
        {"POWDER COAT AMBER", M_POWDER, rgb(176, 88, 16), rgb(120, 56, 8), rgb(36, 30, 26), 12.f, E_SQUARE, X_NONE},
        {"POWDER COAT RED", M_POWDER, rgb(150, 28, 20), rgb(100, 16, 12), rgb(36, 30, 30), 12.f, E_SQUARE, X_NONE},
        {"POWDER COAT TEAL", M_POWDER, rgb(24, 92, 108), rgb(14, 60, 72), rgb(30, 36, 38), 12.f, E_SQUARE, X_NONE},
        {"POWDER COAT WHITE", M_POWDER, rgb(214, 210, 200), rgb(170, 166, 158), rgb(70, 70, 72), 12.f, E_SQUARE, X_NONE},
        {"ROAD CASE", M_TOLEX, rgb(26, 26, 28), rgb(150, 152, 156), rgb(36, 36, 38), 18.f, E_SQUARE, X_NONE},
        {"CARBON FIBER", M_CARBON, rgb(24, 24, 26), rgb(44, 44, 48), rgb(30, 30, 32), 12.f, E_SQUARE, X_NONE},
        {"RACK EARS", M_EARS, rgb(150, 154, 158), rgb(100, 104, 108), rgb(50, 50, 52), 22.f, E_SQUARE, X_NONE},
        // our own takes on the classic case shapes (our names; no maker's look copied badge for badge)
        {"STALKER 6U", M_POWDER, rgb(24, 24, 26), rgb(14, 14, 15), rgb(40, 40, 42), 18.f, E_SLANT, X_LEGS},
        {"STALKER 6U SILVER", M_BRUSHED, rgb(170, 172, 176), rgb(120, 122, 126), rgb(60, 60, 62), 18.f, E_SLANT, X_LEGS},
        {"PERFORMER 7U", M_ANODIZED, rgb(20, 20, 22), rgb(44, 44, 48), rgb(36, 36, 38), 14.f, E_ROUND, X_1U | X_HANDLE},
        {"PERFORMER 7U SILVER", M_BRUSHED, rgb(176, 180, 184), rgb(130, 134, 138), rgb(70, 70, 72), 14.f, E_ROUND, X_1U | X_HANDLE},
        {"BUS STEEL 7U", M_POWDER, rgb(30, 30, 32), rgb(18, 18, 20), rgb(44, 44, 46), 12.f, E_SQUARE, X_1U},
        {"FLATBOAT", M_POWDER, rgb(34, 34, 36), rgb(22, 22, 24), rgb(44, 44, 46), 8.f, E_ROUND, X_NONE},
        {"FLATBOAT WHITE", M_POWDER, rgb(226, 224, 218), rgb(180, 178, 172), rgb(80, 80, 82), 8.f, E_ROUND, X_NONE},
        {"LAB EXTRUSION", M_BRUSHED, rgb(186, 188, 190), rgb(24, 24, 26), rgb(120, 122, 124), 16.f, E_CAPS, X_NONE},
        {"LAB EXTRUSION GREY", M_BRUSHED, rgb(120, 124, 128), rgb(20, 20, 22), rgb(90, 92, 96), 16.f, E_CAPS, X_NONE},
        {"SUITCASE 9U", M_TOLEX, rgb(40, 40, 42), rgb(160, 162, 166), rgb(40, 40, 42), 18.f, E_SQUARE, X_HANDLE | X_LATCHES},
        {"SUITCASE 9U BLUE", M_TOLEX, rgb(22, 34, 60), rgb(160, 162, 166), rgb(36, 40, 50), 18.f, E_SQUARE, X_HANDLE | X_LATCHES},
        {"TIERED WALNUT", M_WOOD, rgb(98, 62, 36), rgb(62, 38, 22), rgb(30, 30, 32), 20.f, E_SLANT, X_NONE},
        {"TIERED ASH", M_WOOD, rgb(196, 170, 130), rgb(150, 124, 88), rgb(40, 40, 42), 20.f, E_SLANT, X_NONE},
        {"WEST COAST BOAT", M_WOOD, rgb(150, 96, 52), rgb(104, 62, 30), rgb(26, 26, 28), 22.f, E_ROUND, X_HANDLE},
        {"SCAFFOLD", M_ANODIZED, rgb(18, 18, 20), rgb(36, 36, 40), rgb(30, 30, 32), 12.f, E_CAPS, X_NONE},
        {"POD RED", M_ANODIZED, rgb(150, 26, 26), rgb(100, 14, 14), rgb(40, 30, 30), 10.f, E_ROUND, X_NONE},
        {"POD BLUE", M_ANODIZED, rgb(28, 70, 150), rgb(16, 44, 104), rgb(30, 34, 44), 10.f, E_ROUND, X_NONE},
        {"POD GOLD", M_ANODIZED, rgb(176, 140, 60), rgb(130, 100, 36), rgb(50, 44, 34), 10.f, E_ROUND, X_NONE},
        {"POD PURPLE", M_ANODIZED, rgb(96, 40, 140), rgb(64, 24, 100), rgb(36, 30, 44), 10.f, E_ROUND, X_NONE},
        {"BRUTE DOCK", M_ANODIZED, rgb(22, 22, 24), rgb(150, 154, 160), rgb(34, 34, 36), 20.f, E_CAPS, X_LEGS},
        {"CLEAR ACRYLIC", M_ACRYLIC, rgb(200, 220, 230), rgb(255, 255, 255), rgb(70, 72, 76), 12.f, E_ROUND, X_NONE},
        {"SMOKED ACRYLIC", M_ACRYLIC, rgb(40, 40, 46), rgb(200, 200, 210), rgb(50, 50, 54), 12.f, E_ROUND, X_NONE},
        {"PRINTED PLA ORANGE", M_PRINTED, rgb(230, 110, 20), rgb(170, 70, 8), rgb(36, 32, 30), 14.f, E_ROUND, X_NONE},
        {"PRINTED PLA BLACK", M_PRINTED, rgb(30, 30, 32), rgb(16, 16, 18), rgb(40, 40, 42), 14.f, E_ROUND, X_NONE},
        {"PRINTED PETG TEAL", M_PRINTED, rgb(20, 120, 130), rgb(10, 80, 90), rgb(30, 38, 40), 14.f, E_ROUND, X_NONE},
    };
}
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
    static const char* const mats[M_NUM] = {"none", "wood", "anodized", "brushed", "powder", "tolex", "carbon", "ears", "acrylic", "printed"};
    static const char* const ends[4] = {"square", "slant", "round", "caps"};
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
            for (int k = 0; k < 4; k++) if (m == ends[k]) c.ends = k;
        }
        if (json_t* v = json_object_get(e, "extras")) {
            size_t xi; json_t* xe;
            json_array_foreach(v, xi, xe) {
                std::string x = json_is_string(xe) ? json_string_value(xe) : "";
                if (x == "1u") c.extras |= X_1U;
                if (x == "handle") c.extras |= X_HANDLE;
                if (x == "latches") c.extras |= X_LATCHES;
                if (x == "legs") c.extras |= X_LEGS;
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
static const int kLookN = 18;
static const char* const kLookName[kLookN] = {"Plain colour", "Amber grid", "OLED glow", "Diamond plate", "Deep space", "Amber rails (Rack's look, warmer)",
    "Carbon weave", "Circuit traces", "CRT scanlines", "Hex mesh", "Blueprint", "Pegboard", "Acoustic foam", "Brushed steel",
    "Sunset fade", "Halftone dots", "Brick wall", "Wood slats"};
static const char* const kFitName[4] = {"Tiled across the rack", "Fill the screen (snaps to fit)", "Fit the screen (whole image shows)", "Centred on the screen"};
static const int kColorN = 24;
static const char* const kColorName[kColorN] = {"Black", "Charcoal", "Deep amber", "Ember", "Midnight teal", "Plum", "Forest", "Steel",
    "Navy", "Indigo", "Oxblood", "Wine", "Rust", "Mocha", "Olive", "Moss", "Slate blue", "Deep cyan", "Deep magenta", "Gunmetal",
    "Warm grey", "Sand", "SynTHBloK orange", "Cream"};
static const NVGcolor kColor[kColorN] = {rgb(6, 6, 6), rgb(22, 22, 24), rgb(40, 22, 6), rgb(46, 12, 6), rgb(6, 30, 36), rgb(34, 14, 38), rgb(10, 30, 16), rgb(36, 40, 46),
    rgb(8, 14, 40), rgb(22, 12, 52), rgb(52, 8, 12), rgb(44, 10, 26), rgb(70, 28, 10), rgb(44, 30, 22), rgb(42, 44, 14), rgb(26, 44, 22), rgb(34, 44, 70), rgb(0, 54, 64), rgb(64, 8, 54), rgb(30, 34, 38),
    rgb(70, 66, 62), rgb(120, 100, 70), rgb(200, 96, 10), rgb(214, 204, 182)};

} // namespace

struct SynTHBloKBackdrop : Module {
    // settings (saved with the patch)
    bool showBg = true;
    int look = 1;
    int colorIdx = 0;
    std::string imagePath;
    int fit = 1;   // fill the screen: the image always snaps to cover the whole background
    float imgScale = 1.f, imgDim = 0.f;
    float imgW = 1.f, imgH = 1.f, imgX = 0.f, imgY = 0.f;   // fine-tuning: width / height stretch, X / Y offset (of the image's size)
    int caseIdx = 1;
    float depthMm = 65.f;   // the case depth, mm, as real builds come
    bool shadow = true, rails = true;
    bool showTitle = true;
    std::string title;   // empty: the patch's file name

    SynTHBloKBackdrop() { config(0, 0, 0, 0); }

    json_t* dataToJson() override {
        json_t* r = json_object();
        json_object_set_new(r, "showBg", json_boolean(showBg));
        json_object_set_new(r, "look", json_integer(look));
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
        json_object_set_new(r, "shadow", json_boolean(shadow));
        json_object_set_new(r, "rails", json_boolean(rails));
        json_object_set_new(r, "showTitle", json_boolean(showTitle));
        json_object_set_new(r, "title", json_string(title.c_str()));
        return r;
    }
    void dataFromJson(json_t* r) override {
        if (json_t* v = json_object_get(r, "showBg")) showBg = json_boolean_value(v);
        if (json_t* v = json_object_get(r, "look")) look = clamp((int) json_integer_value(v), 0, kLookN - 1);
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
    ~BackdropLayer() { freeImage(); }

    void step() override {
        if (parent) box = math::Rect(Vec(0, 0), parent->box.size);
        Widget::step();
    }

    // ---- the background ----
    void drawLook(NVGcontext* vg, math::Rect v, int look, NVGcolor base) {
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
    void drawFrameFill(NVGcontext* vg, const CaseStyle& cs, float x, float y, float w, float h) {
        float rad = cs.ends == E_ROUND ? 9.f : 0.f;
        switch (cs.material) {
            case M_ACRYLIC: {   // see-through: a tint, a bright cut edge and a diagonal glint
                nvgBeginPath(vg); nvgRoundedRect(vg, x, y, w, h, rad); nvgFillColor(vg, nvgTransRGBA(cs.base, 70)); nvgFill(vg);
                nvgStrokeColor(vg, nvgTransRGBA(cs.accent, 120)); nvgStrokeWidth(vg, 1.5f); nvgStroke(vg);
                nvgSave(vg);
                nvgScissor(vg, x, y, w, h);
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
                nvgScissor(vg, x, y, w, h);
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
                nvgBeginPath(vg); nvgRect(vg, x, y, w, h); nvgFillPaint(vg, p); nvgFill(vg);
                nvgSave(vg);
                nvgScissor(vg, x, y, w, h);
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
                NVGpaint p = nvgLinearGradient(vg, x, y, x, y + h, nvgLerpRGBA(cs.base, nvgRGB(255, 255, 255), 0.25f), lo);
                nvgBeginPath(vg); nvgRoundedRect(vg, x, y, w, h, rad); nvgFillPaint(vg, p); nvgFill(vg);
                nvgSave(vg);
                nvgScissor(vg, x, y, w, h);
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
                nvgScissor(vg, x, y, w, h);
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
    void drawCase(NVGcontext* vg, const CaseStyle& cs, math::Rect in, float depthMm, bool doShadow, bool doRails, const std::vector<float>& rowTops) {
        float s = cs.side, t = 6.f, tt = topLip(cs);   // ends, the bottom lip and the top lip (taller with a 1U row)
        float x = in.pos.x - s, y = in.pos.y - tt, w = in.size.x + 2.f * s, h = in.size.y + t + tt;
        float d = mm2px(depthMm) * 0.18f;   // the visible side, below and to the right: the real depth, seen at a slant
        if (doShadow) {
            NVGpaint sh = nvgBoxGradient(vg, x + d * 0.6f, y + d * 0.9f, w, h, 8.f, 22.f, nvgRGBA(0, 0, 0, 150), nvgRGBA(0, 0, 0, 0));
            nvgBeginPath(vg);
            nvgRect(vg, x - 30.f, y - 30.f, w + d + 60.f, h + d + 60.f);
            nvgFillPaint(vg, sh);
            nvgFill(vg);
        }
        if (d > 0.5f) {   // the 3D sides: right and bottom faces, darker
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + w, y); nvgLineTo(vg, x + w + d, y + d); nvgLineTo(vg, x + w + d, y + h + d); nvgLineTo(vg, x + w, y + h);
            nvgClosePath(vg);
            nvgFillColor(vg, nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.55f));
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgMoveTo(vg, x, y + h); nvgLineTo(vg, x + w, y + h); nvgLineTo(vg, x + w + d, y + h + d); nvgLineTo(vg, x + d, y + h + d);
            nvgClosePath(vg);
            nvgFillColor(vg, nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.4f));
            nvgFill(vg);
        }
        // the frame: the ends and lips, then the dark well the modules sit in
        drawFrameFill(vg, cs, x, y, w, h);
        nvgBeginPath(vg);
        nvgRect(vg, in.pos.x, in.pos.y, in.size.x, in.size.y);
        nvgFillColor(vg, nvgRGB(10, 10, 11));
        nvgFill(vg);
        if (doRails)
            for (float rt : rowTops) {
                drawRail(vg, in.pos.x, in.pos.x + in.size.x, rt, cs.rail);
                drawRail(vg, in.pos.x, in.pos.x + in.size.x, rt + RACK_GRID_HEIGHT - 15.f, cs.rail);
            }
        // finishing touches
        if (cs.material == M_TOLEX) {   // metal corners
            for (int k = 0; k < 4; k++) {
                float cx = k & 1 ? x + w - 22.f : x, cy = k & 2 ? y + h - 22.f : y;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, cx, cy, 22.f, 22.f, 3.f);
                nvgFillColor(vg, nvgLerpRGBA(cs.accent, nvgRGB(255, 255, 255), 0.15f));
                nvgFill(vg);
                nvgBeginPath(vg);
                nvgCircle(vg, cx + 11.f, cy + 11.f, 2.2f);
                nvgFillColor(vg, nvgRGBA(0, 0, 0, 120));
                nvgFill(vg);
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
        if (cs.material == M_WOOD) {   // the end cheeks' screws
            for (int side = 0; side < 2; side++)
                for (float rt : rowTops) {
                    nvgBeginPath(vg);
                    nvgCircle(vg, side ? x + w - s * 0.5f : x + s * 0.5f, rt + RACK_GRID_HEIGHT * 0.5f, 2.f);
                    nvgFillColor(vg, nvgRGBA(20, 14, 10, 200));
                    nvgFill(vg);
                }
        }
        if (cs.ends == E_SLANT) {   // cheeks that flare out toward the bottom: a case on legs, a tiered cabinet
            float fl = s * 0.9f;
            for (int side = 0; side < 2; side++) {
                float ex = side ? x + w : x, dir = side ? 1.f : -1.f;
                nvgBeginPath(vg);
                nvgMoveTo(vg, ex, y); nvgLineTo(vg, ex, y + h); nvgLineTo(vg, ex + dir * fl, y + h); nvgLineTo(vg, ex + dir * fl * 0.15f, y);
                nvgClosePath(vg);
                nvgFillColor(vg, nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.3f));
                nvgFill(vg);
                nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 140)); nvgStrokeWidth(vg, 1.f); nvgStroke(vg);
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
        if (cs.extras & X_1U) {   // the 1U utility row on the top lip: jacks and a couple of LEDs
            float ry = y + 3.f, rh = tt - 6.f;
            nvgBeginPath(vg);
            nvgRect(vg, in.pos.x, ry, in.size.x, rh);
            nvgFillColor(vg, nvgRGB(14, 14, 15));
            nvgFill(vg);
            nvgBeginPath(vg);
            for (float jx = in.pos.x + 14.f; jx < in.pos.x + std::min(in.size.x, 260.f); jx += 18.f) nvgCircle(vg, jx, ry + rh * 0.5f, 3.6f);
            nvgFillColor(vg, nvgRGB(150, 152, 156));
            nvgFill(vg);
            nvgBeginPath(vg);
            for (float jx = in.pos.x + 14.f; jx < in.pos.x + std::min(in.size.x, 260.f); jx += 18.f) nvgCircle(vg, jx, ry + rh * 0.5f, 1.7f);
            nvgFillColor(vg, nvgRGB(10, 10, 10));
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgCircle(vg, in.pos.x + std::min(in.size.x, 260.f) + 8.f, ry + rh * 0.5f, 2.f);
            nvgFillColor(vg, nvgRGB(255, 140, 30));
            nvgFill(vg);
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
        if (cs.extras & X_LEGS) {   // folding legs under the bottom corners
            for (int side = 0; side < 2; side++) {
                float lx = side ? x + w - s * 0.8f - 6.f : x + s * 0.8f;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, lx, y + h, 6.f, 14.f, 2.f);
                nvgFillColor(vg, nvgLerpRGBA(cs.base, nvgRGB(0, 0, 0), 0.4f));
                nvgFill(vg);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, lx - 3.f, y + h + 12.f, 12.f, 4.f, 2.f);
                nvgFillColor(vg, nvgRGB(12, 12, 12));
                nvgFill(vg);
            }
        }
        nvgBeginPath(vg);   // a fine edge so the case reads against any background
        nvgRoundedRect(vg, x + 0.5f, y + 0.5f, w - 1.f, h - 1.f, cs.ends == E_ROUND ? 9.f : 0.f);
        nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 160));
        nvgStrokeWidth(vg, 1.f);
        nvgStroke(vg);
    }

    // the blocks of modules: rows that hold modules, joined when they touch and overlap
    struct Block { math::Rect r; std::vector<float> rows; };
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
            }
            else out.push_back({math::Rect(Vec(a, top), Vec(b - a, RACK_GRID_HEIGHT)), {top}});
            last = kv.first;
        }
        return out;
    }

    void draw(const DrawArgs& args) override {
        if (!module) return;
        SynTHBloKBackdrop* m = module;
        math::Rect v = args.clipBox;
        if (m->showBg) {
            drawLook(args.vg, v, m->look, kColor[clamp(m->colorIdx, 0, kColorN - 1)]);
            drawImage(args, v);
        }
        const std::vector<CaseStyle>& lib = caseLibrary();
        const CaseStyle& cs = lib[clamp(m->caseIdx, 0, (int) lib.size() - 1)];
        std::vector<Block> bl = blocks();
        if (cs.material != M_NONE)
            for (const Block& b : bl) drawCase(args.vg, cs, b.r, m->depthMm, m->shadow, m->rails, b.rows);
        else if (m->showBg && m->rails)   // no case: the rails still show under the modules
            for (const Block& b : bl)
                for (float rt : b.rows) {
                    drawRail(args.vg, b.r.pos.x, b.r.pos.x + b.r.size.x, rt, nvgRGB(46, 46, 48));
                    drawRail(args.vg, b.r.pos.x, b.r.pos.x + b.r.size.x, rt + RACK_GRID_HEIGHT - 15.f, nvgRGB(46, 46, 48));
                }
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
                nvgFontSize(args.vg, 22.f);
                nvgTextLetterSpacing(args.vg, 2.f);
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
        float cx = box.size.x * 0.5f, cy = mm2px(112.f);
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
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.f);
        nvgFillColor(args.vg, nvgRGB(10, 7, 3));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, editing() ? oled::amber(200) : oled::outline(70));
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        float pad = 4.f, w = box.size.x - 2.f * pad;
        nvgFontSize(args.vg, 7.f);
        nvgTextLetterSpacing(args.vg, 1.f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFillColor(args.vg, oled::dim());
        nvgText(args.vg, pad, pad - 1.f, editing() ? "TYPE A NAME  \xe2\x8f\x8e" : "PATCH NAME", NULL);
        nvgTextLetterSpacing(args.vg, 0.f);
        std::string t = getText();
        if (editing()) {   // one line, shrinks to fit, the caret where you type
            float fs = 13.f, bw = 0.f;
            for (; fs > 7.f; fs -= 0.5f) { nvgFontSize(args.vg, fs); bw = nvgTextBounds(args.vg, 0, 0, t.c_str(), NULL, NULL); if (bw <= w) break; }
            int c = clamp(cursor, 0, (int) t.size());
            float cx = nvgTextBounds(args.vg, 0, 0, t.c_str(), t.c_str() + c, NULL);
            float scroll = std::max(0.f, cx - w);
            float ty = box.size.y * 0.58f;
            nvgSave(args.vg);
            nvgScissor(args.vg, pad, 0, w + 2.f, box.size.y);
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, oled::amber());
            nvgText(args.vg, pad - scroll, ty, t.c_str(), NULL);
            if (std::fmod(system::getTime(), 1.0) < 0.6) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, pad - scroll + cx + 0.5f, ty - fs * 0.45f, 1.2f, fs * 0.9f);
                nvgFill(args.vg);
            }
            nvgRestore(args.vg);
        }
        else {   // up to three lines; the patch's file name, dimmed, until you type one
            bool own = !t.empty();
            if (!own) { std::string pp = APP->patch->path; t = pp.empty() ? "UNTITLED" : system::getStem(pp); }
            t = string::uppercase(t);
            float fs = 12.f;
            nvgFontSize(args.vg, fs);
            float b[4];
            nvgTextBoxBounds(args.vg, 0, 0, w, t.c_str(), NULL, b);
            if (b[3] - b[1] > fs * 3.3f) { fs = 9.f; nvgFontSize(args.vg, fs); }
            nvgTextLineHeight(args.vg, 1.05f);
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgFillColor(args.vg, own ? oled::amber() : oled::dim());
            nvgSave(args.vg);
            nvgScissor(args.vg, pad, 0, w, box.size.y - 2.f);
            nvgTextBox(args.vg, pad, pad + 8.f, w, t.c_str(), NULL);
            nvgRestore(args.vg);
        }
    }
};
// NAME: a slim button that starts the typing (selection moves on release, so the click doesn't take it back)
struct NameButton : widget::OpaqueWidget {
    NameOled* field = NULL;
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) e.consume(this);
    }
    void onDragEnd(const DragEndEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && field) APP->event->setSelectedWidget(field);
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        bool on = field && field->editing();
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, box.size.y * 0.5f);
        nvgFillColor(args.vg, on ? oled::litFill() : nvgRGB(24, 18, 12));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, oled::outline(on ? 200 : 110));
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStroke(args.vg);
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 7.5f);
        nvgTextLetterSpacing(args.vg, 1.5f);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, on ? oled::ink() : oled::amber());
        nvgText(args.vg, box.size.x * 0.5f + 0.75f, box.size.y * 0.5f + 0.5f, "NAME", NULL);
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
        NameOled* nf = new NameOled;   // the name OLED and its button, at the top of the well
        nf->module = module;
        nf->box.pos = mm2px(Vec(5.8f, 14.f));
        nf->box.size = mm2px(Vec(18.88f, 20.f));
        addChild(nf);
        NameButton* nb = new NameButton;
        nb->field = nf;
        nb->box.size = mm2px(Vec(11.f, 4.2f));
        nb->box.pos = Vec(box.size.x * 0.5f - nb->box.size.x * 0.5f, mm2px(37.f));
        addChild(nb);
        addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH * 0.5f, 0)));
        addChild(createWidget<ThemedScrew>(Vec(box.size.x - RACK_GRID_WIDTH * 1.5f, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
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
        menu->addChild(createBoolPtrMenuItem("Rails", "", &m->rails));
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
