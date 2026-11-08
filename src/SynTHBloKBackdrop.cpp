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
#include <map>

namespace {

// ---------------------------------------------------------------------------------------------------------
// THE CASE LIBRARY. A material decides how the frame is drawn; the colours make it this case.
// ---------------------------------------------------------------------------------------------------------
enum Material { M_NONE, M_WOOD, M_ANODIZED, M_BRUSHED, M_POWDER, M_TOLEX, M_CARBON, M_EARS };
struct CaseStyle {
    std::string name;
    int material;
    NVGcolor base, accent, rail;
    float side;    // frame thickness at the ends, px
};
static NVGcolor rgb(int r, int g, int b) { return nvgRGB(r, g, b); }
static std::vector<CaseStyle> builtInCases() {
    return {
        {"NO CASE", M_NONE, rgb(0, 0, 0), rgb(0, 0, 0), rgb(0, 0, 0), 0.f},
        {"WALNUT SKIFF", M_WOOD, rgb(92, 58, 34), rgb(60, 36, 20), rgb(46, 46, 48), 16.f},
        {"OAK SKIFF", M_WOOD, rgb(168, 124, 78), rgb(128, 90, 52), rgb(52, 52, 54), 16.f},
        {"EBONY SKIFF", M_WOOD, rgb(44, 30, 24), rgb(26, 18, 14), rgb(40, 40, 42), 16.f},
        {"BLACK ANODIZED", M_ANODIZED, rgb(22, 22, 24), rgb(40, 40, 44), rgb(34, 34, 36), 12.f},
        {"BRUSHED ALUMINUM", M_BRUSHED, rgb(160, 165, 170), rgb(120, 125, 130), rgb(110, 112, 116), 12.f},
        {"POWDER COAT AMBER", M_POWDER, rgb(176, 88, 16), rgb(120, 56, 8), rgb(36, 30, 26), 12.f},
        {"POWDER COAT RED", M_POWDER, rgb(150, 28, 20), rgb(100, 16, 12), rgb(36, 30, 30), 12.f},
        {"POWDER COAT TEAL", M_POWDER, rgb(24, 92, 108), rgb(14, 60, 72), rgb(30, 36, 38), 12.f},
        {"POWDER COAT WHITE", M_POWDER, rgb(214, 210, 200), rgb(170, 166, 158), rgb(70, 70, 72), 12.f},
        {"ROAD CASE", M_TOLEX, rgb(26, 26, 28), rgb(150, 152, 156), rgb(36, 36, 38), 18.f},
        {"CARBON FIBER", M_CARBON, rgb(24, 24, 26), rgb(44, 44, 48), rgb(30, 30, 32), 12.f},
        {"RACK EARS", M_EARS, rgb(150, 154, 158), rgb(100, 104, 108), rgb(50, 50, 52), 22.f},
    };
}
// the library: the built-in cases, then any in Rack's user folder, SynTHBloK/cases.json:
//   [{"name": "MY CASE", "material": "wood", "base": "#5c3a22", "accent": "#3c2414", "rail": "#2e2e30", "side": 16}]
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
    static const char* const mats[8] = {"none", "wood", "anodized", "brushed", "powder", "tolex", "carbon", "ears"};
    size_t i; json_t* e;
    json_array_foreach(j, i, e) {
        CaseStyle c = lib[1];
        if (json_t* v = json_object_get(e, "name")) c.name = json_string_value(v);
        if (json_t* v = json_object_get(e, "material")) {
            std::string m = json_string_value(v);
            for (int k = 0; k < 8; k++) if (m == mats[k]) c.material = k;
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
static const char* const kLookName[6] = {"Plain colour", "Amber grid", "OLED glow", "Diamond plate", "Deep space", "Amber rails (Rack's look, warmer)"};
static const char* const kFitName[4] = {"Tiled across the rack", "Fill the screen", "Fit the screen", "Centred on the screen"};
static const char* const kColorName[8] = {"Black", "Charcoal", "Deep amber", "Ember", "Midnight teal", "Plum", "Forest", "Steel"};
static const NVGcolor kColor[8] = {rgb(6, 6, 6), rgb(22, 22, 24), rgb(40, 22, 6), rgb(46, 12, 6), rgb(6, 30, 36), rgb(34, 14, 38), rgb(10, 30, 16), rgb(36, 40, 46)};

} // namespace

struct SynTHBloKBackdrop : Module {
    // settings (saved with the patch)
    bool showBg = true;
    int look = 1;
    int colorIdx = 0;
    std::string imagePath;
    int fit = 0;
    float imgScale = 1.f, imgDim = 0.f;
    int caseIdx = 1;
    float depth = 0.5f;
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
        json_object_set_new(r, "case", json_string(caseLibrary()[clamp(caseIdx, 0, (int) caseLibrary().size() - 1)].name.c_str()));
        json_object_set_new(r, "depth", json_real(depth));
        json_object_set_new(r, "shadow", json_boolean(shadow));
        json_object_set_new(r, "rails", json_boolean(rails));
        json_object_set_new(r, "showTitle", json_boolean(showTitle));
        json_object_set_new(r, "title", json_string(title.c_str()));
        return r;
    }
    void dataFromJson(json_t* r) override {
        if (json_t* v = json_object_get(r, "showBg")) showBg = json_boolean_value(v);
        if (json_t* v = json_object_get(r, "look")) look = clamp((int) json_integer_value(v), 0, 5);
        if (json_t* v = json_object_get(r, "color")) colorIdx = clamp((int) json_integer_value(v), 0, 7);
        if (json_t* v = json_object_get(r, "image")) imagePath = json_string_value(v);
        if (json_t* v = json_object_get(r, "fit")) fit = clamp((int) json_integer_value(v), 0, 3);
        if (json_t* v = json_object_get(r, "imgScale")) imgScale = clamp((float) json_number_value(v), 0.1f, 8.f);
        if (json_t* v = json_object_get(r, "imgDim")) imgDim = clamp((float) json_number_value(v), 0.f, 0.9f);
        if (json_t* v = json_object_get(r, "case")) {   // by name, so a library that grows keeps the choice
            std::string n = json_string_value(v);
            for (size_t i = 0; i < caseLibrary().size(); i++) if (caseLibrary()[i].name == n) caseIdx = (int) i;
        }
        if (json_t* v = json_object_get(r, "depth")) depth = clamp((float) json_number_value(v), 0.f, 1.f);
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
            float sx = v.size.x / imgW, sy = v.size.y / imgH, k = m->fit == 1 ? std::max(sx, sy) : std::min(sx, sy);
            w = imgW * k; h = imgH * k;
            ox = v.pos.x + (v.size.x - w) * 0.5f; oy = v.pos.y + (v.size.y - h) * 0.5f;
            if (m->fit == 2) area = math::Rect(Vec(ox, oy), Vec(w, h));
        }
        else if (m->fit == 3) {
            ox = v.pos.x + (v.size.x - w) * 0.5f; oy = v.pos.y + (v.size.y - h) * 0.5f;
            area = math::Rect(Vec(ox, oy), Vec(w, h));
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
    void drawFrameFill(NVGcontext* vg, const CaseStyle& cs, float x, float y, float w, float h) {
        switch (cs.material) {
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
                NVGpaint p = nvgLinearGradient(vg, x, y, x, y + h, nvgLerpRGBA(cs.base, nvgRGB(255, 255, 255), 0.25f), cs.accent);
                nvgBeginPath(vg); nvgRect(vg, x, y, w, h); nvgFillPaint(vg, p); nvgFill(vg);
                nvgBeginPath(vg);
                for (float k = y + 1.f; k < y + h; k += 1.7f) { nvgMoveTo(vg, x, k); nvgLineTo(vg, x + w, k); }
                nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 10));
                nvgStrokeWidth(vg, 0.6f);
                nvgStroke(vg);
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
                nvgBeginPath(vg); nvgRect(vg, x, y, w, h); nvgFillPaint(vg, p); nvgFill(vg);
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
    void drawCase(NVGcontext* vg, const CaseStyle& cs, math::Rect in, float depthK, bool doShadow, bool doRails, const std::vector<float>& rowTops) {
        float s = cs.side, t = 6.f;   // ends and the top / bottom lip
        float x = in.pos.x - s, y = in.pos.y - t, w = in.size.x + 2.f * s, h = in.size.y + 2.f * t;
        float d = 26.f * depthK;      // the skiff's visible side, below and to the right
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
        nvgBeginPath(vg);   // a fine edge so the case reads against any background
        nvgRect(vg, x + 0.5f, y + 0.5f, w - 1.f, h - 1.f);
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
            drawLook(args.vg, v, m->look, kColor[clamp(m->colorIdx, 0, 7)]);
            drawImage(args, v);
        }
        const std::vector<CaseStyle>& lib = caseLibrary();
        const CaseStyle& cs = lib[clamp(m->caseIdx, 0, (int) lib.size() - 1)];
        std::vector<Block> bl = blocks();
        if (cs.material != M_NONE)
            for (const Block& b : bl) drawCase(args.vg, cs, b.r, m->depth, m->shadow, m->rails, b.rows);
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
                float tx = bl.front().r.pos.x - cs.side, ty = bl.front().r.pos.y - 12.f;
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

struct SynTHBloKBackdropWidget : ModuleWidget {
    SynTHBloKBackdropWidget(SynTHBloKBackdrop* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/SynTHBloKBackdrop.svg")));
        BackdropPanel* p = new BackdropPanel;
        p->box.size = box.size;
        p->module = module;
        p->widget = this;
        addChild(p);
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
        menu->addChild(createIndexSubmenuItem("Look", std::vector<std::string>(kLookName, kLookName + 6),
            [=]() { return (size_t) m->look; }, [=](size_t i) { m->look = (int) i; }));
        menu->addChild(createIndexSubmenuItem("Colour", std::vector<std::string>(kColorName, kColorName + 8),
            [=]() { return (size_t) m->colorIdx; }, [=](size_t i) { m->colorIdx = (int) i; }));
        menu->addChild(createMenuItem("Load an image...", m->imagePath.empty() ? "" : system::getFilename(m->imagePath), [=]() {
            osdialog_filters* f = osdialog_filters_parse("Images:png,jpg,jpeg,bmp,gif");
            char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, f);
            osdialog_filters_free(f);
            if (path) { m->imagePath = path; std::free(path); }
        }));
        if (!m->imagePath.empty()) {
            menu->addChild(createIndexSubmenuItem("Image placement", std::vector<std::string>(kFitName, kFitName + 4),
                [=]() { return (size_t) m->fit; }, [=](size_t i) { m->fit = (int) i; }));
            menu->addChild(createSubmenuItem("Image scale (tiled / centred)", string::f("%d%%", (int) std::round(m->imgScale * 100.f)), [=](Menu* sub) {
                for (float k : {0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.f, 3.f, 4.f})
                    sub->addChild(createCheckMenuItem(string::f("%d%%", (int) (k * 100.f)), "", [=]() { return std::fabs(m->imgScale - k) < 0.01f; }, [=]() { m->imgScale = k; }));
            }));
            menu->addChild(createSubmenuItem("Darken the image", string::f("%d%%", (int) std::round(m->imgDim * 100.f)), [=](Menu* sub) {
                for (float k : {0.f, 0.2f, 0.4f, 0.6f, 0.8f})
                    sub->addChild(createCheckMenuItem(string::f("%d%%", (int) (k * 100.f)), "", [=]() { return std::fabs(m->imgDim - k) < 0.01f; }, [=]() { m->imgDim = k; }));
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
        menu->addChild(createSubmenuItem("Case depth (3D side)", string::f("%d%%", (int) std::round(m->depth * 100.f)), [=](Menu* sub) {
            for (float k : {0.f, 0.25f, 0.5f, 0.75f, 1.f})
                sub->addChild(createCheckMenuItem(string::f("%d%%", (int) (k * 100.f)), "", [=]() { return std::fabs(m->depth - k) < 0.01f; }, [=]() { m->depth = k; }));
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
                void onAction(const ActionEvent& e) override { m->title = tf->getText(); }
            };
            sub->addChild(tf);
            Apply* a = new Apply; a->m = m; a->tf = tf; a->text = "Set";
            sub->addChild(a);
            sub->addChild(createMenuItem("Use the patch name", "", [=]() { m->title.clear(); }));
        }));
    }
};

Model* modelSynTHBloKBackdrop = createModel<SynTHBloKBackdrop, SynTHBloKBackdropWidget>("Backdrop");
