#include "gt4r/app/race_hud_prologue.h"
#include "gt4r/app/game_area.h"

#include "base/log.h"
#include "formats/gs_memory.h"
#include "formats/menu/imgs.h"
#include "formats/text/text_layout.h"
#include "gt4r/disc/disc.h"
#include "gt4r/menu/font.h"
#include "gt4r/menu/text_table.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/program_prologue.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>

namespace gt4r {

namespace hr = gt3r::race;
using race::Rgba8;

namespace {

constexpr float kPi = 3.14159265f;
// The display's frame: 640 x 448, on a wide picture -106.667 .. 746.667 across (the program's 0x3956A0: alignment bit 4 is the
// wide picture's edge). The in-car panel's parts are in a frame of 480 lines (0x38BEEC: 32 down, 448 / 480 high).
// (Other shapes of the picture: game_area.h.)
float wide_side() { return game_area::side(); }
constexpr float kScaleX = 360.0f / 480.0f, kScaleY = float(Ui::kHeight) / 448.0f;

// The display's colors (its program's constants; the GS order, alpha 0x80 opaque).
constexpr u32 kOrange = 0x805EA8F2, kGray = 0x80B3B3B3, kRed = 0x800000C8, kGreen = 0x809CB246, kEvent = 0x802BCC49;

struct Box {
    float x = 0, y = 0, w = 0, h = 0;
};
SDL_FRect to_ui(const Box& b) { return {(b.x + wide_side()) * kScaleX, b.y * kScaleY, b.w * kScaleX, b.h * kScaleY}; }
// 0x3956A0 / 0x395638: a box's left edge by the alignment: 0 left, 1 right, 2 centered, + 4 to the wide picture's edge.
float left_of(float x, float w, int align) {
    const float wide = (align & 4) ? wide_side() : 0;
    switch (align & 3) {
    case 0: return x - wide;
    case 1: return x + wide - w;
    default: return x - w / 2;
    }
}
Rgba8 gs(u32 c, float opacity, float rgb = 1) {
    Rgba8 o;
    const auto part = [&](u32 v) { return u8(std::clamp(float(v & 0xFF) * rgb, 0.0f, 255.0f)); };
    o.r = part(c), o.g = part(c >> 8), o.b = part(c >> 16);
    o.a = u8(std::clamp(float(std::min(255u, (c >> 24) * 255u / 128u)) * opacity, 0.0f, 255.0f));
    return o;
}
// M'SS.mmm (0x3956F8): the program's dashes ("-'--.---" in the font's own dash) for none.
std::string time_text(s64 ms) {
    if (ms < 0) return "\x7F'\x7F\x7F.\x7F\x7F\x7F";
    char b[32];
    std::snprintf(b, sizeof b, "%lld'%02lld.%03lld", (long long)(ms / 60000), (long long)(ms / 1000 % 60), (long long)(ms % 1000));
    return b;
}

} // namespace

struct PrologueHud::Impl {
    struct Picture {
        SDL_Texture* texture = nullptr;
        float w = 0, h = 0;
    };
    struct FontSpec {
        const gt3r::text::LatinFont* font = nullptr;
        float sx = 1, sy = 1;
        int variant = 0;
    };
    // A dial face: for values up to `limit` the face `name` up to `max` (the tachometer's 0x5711F8, the speedometer's 0x571358
    // in miles and 0x5713D0 in kilometers an hour).
    struct Face {
        float limit = 0, max = 0;
        std::string name;
    };
    // A needle as the program leaves it (its last angle, for the trail).
    struct Needle {
        float angle = 0;
        bool set = false;
    };

    SDL_Renderer* r = nullptr;
    std::map<std::string, Picture> pictures;
    Picture logo;
    Picture flows;                        // the hybrid's monitor: its palette's indices, its colors, the waves' phase
    std::vector<u8> flow_index;
    std::array<u32, 256> flow_palette{};
    float flow_phase = 0;
    gt3r::text::FontFile fonts, more_fonts;
    std::map<std::string, const gt3r::text::LatinFont*> font_by_name;
    std::map<std::pair<const gt3r::text::LatinFont*, int>, SDL_Texture*> atlases;
    std::map<std::string, std::string> words;
    std::vector<Face> rev_faces, speed_faces;
    race::CourseMap map;
    SDL_Texture* map_texture = nullptr;
    Needle needles[3];
    std::array<float, 2> pedals{}; // the replay monitor's last brake and throttle
    float sign = 0;                // the wrong-way sign's fade
    float rank_shown = 0; // the rank's rolling digit (+0x2C)
    double clock = 0, step = 0;
    Uint64 last_ns = 0;

    SDL_Texture* make_texture(const gt3r::Tex1::Image& img) {
        if (!img.width || !img.height) return nullptr;
        SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(img.width), int(img.height));
        if (!t) return nullptr;
        SDL_UpdateTexture(t, nullptr, img.rgba.data(), int(img.width * 4));
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        return t;
    }
    const Picture& picture(const std::string& name) const {
        static const Picture kNone;
        const auto it = pictures.find(name);
        return it == pictures.end() ? kNone : it->second;
    }
    // A picture over a box (the GS's modulate: 0x80 the picture's own color).
    void draw_picture(const Picture& p, const Box& at, u32 tint, float opacity, float rgb = 1) {
        if (!p.texture || opacity <= 0) return;
        const auto part = [&](u32 v) { return u8(std::clamp(float(v & 0xFF) * rgb * 255 / 128, 0.0f, 255.0f)); };
        SDL_SetTextureColorMod(p.texture, part(tint), part(tint >> 8), part(tint >> 16));
        SDL_SetTextureAlphaMod(p.texture, u8(std::clamp(float(std::min(255u, (tint >> 24) * 255u / 128u)) * opacity, 0.0f, 255.0f)));
        const SDL_FRect dst = to_ui(at);
        SDL_RenderTexture(r, p.texture, nullptr, &dst);
    }
    // 0x395050: a picture at its own size by its alignments.
    Box place(const Picture& p, float x, float y, int ha, int va) const { return {left_of(x, p.w, ha), left_of(y, p.h, va & 3), p.w, p.h}; }

    FontSpec font(const char* name, float percent = 100, int variant = 0) const {
        FontSpec f;
        const auto it = font_by_name.find(name);
        f.font = it == font_by_name.end() ? nullptr : it->second;
        f.sx = f.sy = percent / 100;
        f.variant = variant;
        return f;
    }
    float width(const FontSpec& f, const std::string& s) const {
        if (!f.font) return 0;
        gt3r::text::LayoutOptions opt;
        opt.scale_x = f.sx, opt.scale_y = f.sy;
        std::u32string codes;
        for (char32_t c : gt3r::text::utf8_decode(s)) codes += c;
        return gt3r::text::layout_text(gt3r::text::map_unicode(codes, *f.font, nullptr, nullptr), *f.font, nullptr, 0, 0, opt).width;
    }
    float line_height(const FontSpec& f) const { return f.font ? float(f.font->line_height) : 0; }
    // Text with the pen on its baseline. `clip`: only what falls in the frame's lines clip[0] .. clip[1].
    void text(const FontSpec& f, float x, float y, u32 color, float opacity, const std::string& s, bool right = false, const float* clip = nullptr) {
        if (!f.font || s.empty() || opacity <= 0) return;
        gt3r::text::LayoutOptions opt;
        opt.scale_x = f.sx, opt.scale_y = f.sy;
        opt.align = right ? gt3r::text::HAlign::Right : gt3r::text::HAlign::Left;
        opt.valign = gt3r::text::VAlign::Baseline;
        std::u32string codes;
        for (char32_t c : gt3r::text::utf8_decode(s)) codes += c;
        const gt3r::text::TextLayout lay = gt3r::text::layout_text(gt3r::text::map_unicode(codes, *f.font, nullptr, nullptr), *f.font, nullptr, x, y, opt);
        SDL_Texture*& atlas = atlases[{f.font, f.variant}];
        if (!atlas) atlas = make_texture(f.font->atlas[size_t(std::clamp(f.variant, 0, 1))]);
        if (!atlas) return;
        const Rgba8 c = gs(color, opacity);
        SDL_SetTextureColorMod(atlas, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(atlas, c.a);
        for (const gt3r::text::GlyphQuad& q : lay.quads) {
            if (q.kanji || !q.sw || !q.sh) continue;
            SDL_FRect src{float(q.sx), float(q.sy), float(q.sw), float(q.sh)};
            Box b{q.x, q.y, q.w, q.h};
            if (clip) {
                const float top = std::max(b.y, clip[0]), bottom = std::min(b.y + b.h, clip[1]);
                if (bottom <= top) continue;
                src.y += src.h * (top - b.y) / b.h, src.h *= (bottom - top) / b.h;
                b.y = top, b.h = bottom - top;
            }
            const SDL_FRect dst = to_ui(b);
            SDL_RenderTexture(r, atlas, &src, &dst);
        }
    }
    void triangles(const std::vector<SDL_Vertex>& v) { SDL_RenderGeometry(r, nullptr, v.data(), int(v.size()), nullptr, 0); }
    static SDL_Vertex vertex(float x, float y, const Rgba8& c) {
        SDL_Vertex v{};
        v.position = {(x + wide_side()) * kScaleX, y * kScaleY};
        v.color = {float(c.r) / 255, float(c.g) / 255, float(c.b) / 255, float(c.a) / 255};
        return v;
    }
    // 0x395510: a box with its corners cut by `cut`.
    void cut_box(const Box& b, float cut, u32 color, float opacity) {
        const Rgba8 c = gs(color, opacity);
        const float x0 = b.x, x1 = b.x + b.w, y0 = b.y, y1 = b.y + b.h;
        const float p[8][2] = {{x0 + cut, y0}, {x1 - cut, y0}, {x1, y0 + cut}, {x1, y1 - cut}, {x1 - cut, y1}, {x0 + cut, y1}, {x0, y1 - cut}, {x0, y0 + cut}};
        std::vector<SDL_Vertex> v;
        for (int k = 1; k + 1 < 8; ++k) {
            v.push_back(vertex(p[0][0], p[0][1], c));
            v.push_back(vertex(p[k][0], p[k][1], c));
            v.push_back(vertex(p[k + 1][0], p[k + 1][1], c));
        }
        triangles(v);
    }
    void fill(const Box& b, u32 color, float opacity) { cut_box(b, 0, color, opacity); }

    // The panel's lines (a frame of 480) in the display's.
    static float panel_y(float y) { return y * 448 / 480 + 32; }
    static Box panel(const Box& b) { return {b.x, panel_y(b.y), b.w, b.h * 448 / 480}; }

    void draw_rank(const hr::HudRow& row, int place_now, float opacity);
    void draw_lap(const hr::HudRow& row, int lap, int laps, float opacity);
    void draw_time(float x, float y, int ha, s64 ms, const std::string& label, const char* icon, float ix, float iy, float opacity);
    void draw_map(const hr::HudRow& row, const HudState& s, float opacity);
    struct DialShape { // a dial's base picture, its middle below the box's, its radius and its needle from its tail to its tip (of it)
        const char* base = "GT4_meter_texture_sprv_base";
        float down = 16.5f, radius = 77.5f, tail = -0.30970f, tip = 0.96130f;
    };
    void draw_dial(const Box& b, const std::string& face, float value, float min, float max, float a0, float a1, float red_from, Needle& n, float opacity, const DialShape& shape);
    void draw_dial(const Box& b, const std::string& face, float value, float min, float max, float a0, float a1, float red_from, Needle& n, float opacity) {
        draw_dial(b, face, value, min, max, a0, a1, red_from, n, opacity, DialShape{});
    }
    void draw_boost(const Extra& e, float opacity);
    void draw_hybrid(const HudState& s, const Extra& e, float opacity);
    void draw_flows(const Extra& e, float opacity);
    void load_monitor(ByteSpan set);
    void draw_pedals(const hr::HudRow& row, const Extra& e, float opacity);
    void draw_cones(const hr::HudRow& row, const Extra& e, float opacity);
    void draw_onboard(const HudState& s, const Extra& e, float opacity);
    void draw_simple(const HudState& s, const Extra& e, float opacity);
    void draw_gear(const FontSpec& f, float x, float y, int gear, float opacity);
    void draw_message(float x, float y, const FontSpec& f, u32 color, float opacity, const std::string& s);
};

PrologueHud::PrologueHud(App& app, ByteSpan gtcm) : impl_(std::make_unique<Impl>()) {
    gt3r::Vfs* vfs = app.vfs();
    if (!vfs || !app.disc()) return;
    Impl& m = *impl_;
    m.r = app.renderer();
    try {
        const std::optional<Bytes> modes = vfs->read("database/racemode_e3.db"), rows = vfs->read("database/racedetail_e3.db");
        std::optional<Bytes> file;
        for (const char* region : {"JP", "US", "BR"})
            if (!file) file = vfs->read(std::string("race/") + region + "/display/display.imgs");
        if (!modes || !rows || !file || !gt3r::menu::Imgs::is_imgs(*file)) return;
        const gt3r::menu::Imgs imgs = gt3r::menu::Imgs::parse(*file);
        for (size_t i = 0; i < imgs.entries.size(); ++i) {
            const gt3r::Tex1::Image img = imgs.decode(*file, i);
            m.pictures[imgs.entries[i].name] = {m.make_texture(img), float(img.width), float(img.height)};
        }
        if (const int at = imgs.find("prius_monitor"); at >= 0) m.load_monitor(imgs.set(*file, size_t(at)));
        if (const std::optional<Bytes> f = vfs->read("font/race.fnt")) m.fonts = menu::read_fonts(*f);
        if (const std::optional<Bytes> f = vfs->read("font/system.fnt")) m.more_fonts = menu::read_fonts(*f); // (sw_*)
        for (const gt3r::text::FontFile* file : {&m.fonts, &m.more_fonts})
            for (const gt3r::text::LatinFont& f : file->fonts) m.font_by_name.emplace(f.name, &f);
        if (const std::optional<Bytes> t = vfs->read("rtext/raceJP.rt2"); t && menu::TextTable::is_table(*t)) {
            const menu::TextTable table = menu::TextTable::parse(*t);
            if (const auto* page = table.page("RaceDisplay"))
                for (const auto& [key, word] : *page) m.words[key] = word;
        }
        // The dials' faces are tables of the program.
        const sim::Program program = sim::Program::load(*app.disc());
        if (sim::PrologueProgram::locate(program).recognized()) {
            const auto string_at = [&](u32 at) {
                std::string s;
                for (u8 c; s.size() < 64 && (c = program.b(at)) != 0; ++at) s += char(c);
                return s;
            };
            const auto real = [&](u32 at) {
                const u32 w = program.w(at);
                float f;
                std::memcpy(&f, &w, 4);
                return f;
            };
            const std::string rev = string_at(0x571298);
            for (u32 k = 0; k < 8; ++k) {
                const u32 at = 0x5711F8 + 0x10 * k;
                m.rev_faces.push_back({program.w(at) == 0x7FFFFFFF ? 1e9f : real(at), real(at + 4), rev + string_at(program.w(at + 0xC))});
            }
            for (u32 k = 0; k < 3; ++k) {
                const u32 at = 0x5713D0 + 0xC * k;
                m.speed_faces.push_back({program.w(at) == 0x7FFFFFFF ? 1e9f : real(at), real(at + 4), string_at(program.w(at + 8))});
            }
        }
        if (m.rev_faces.empty() || m.fonts.fonts.empty()) return;
        if (!hr::set_hud_tables(*modes, *rows)) {
            LOG_WARN("Prologue's race display: its layout tables do not read");
            return;
        }
        m.map = race::CourseMap::parse(gtcm);
        if (m.map.valid()) m.map_texture = m.make_texture(m.map.image);
        ready_ = true;
    } catch (const std::exception& e) {
        LOG_WARN("Prologue's race display: %s", e.what());
    }
}

PrologueHud::~PrologueHud() {
    if (ready_) hr::clear_hud_tables();
    for (auto& [name, p] : impl_->pictures)
        if (p.texture) SDL_DestroyTexture(p.texture);
    for (auto& [key, t] : impl_->atlases)
        if (t) SDL_DestroyTexture(t);
    if (impl_->logo.texture) SDL_DestroyTexture(impl_->logo.texture);
    if (impl_->flows.texture) SDL_DestroyTexture(impl_->flows.texture);
    if (impl_->map_texture) SDL_DestroyTexture(impl_->map_texture);
}

void PrologueHud::set_course_logo(ByteSpan set) {
    Impl& m = *impl_;
    if (m.logo.texture) SDL_DestroyTexture(m.logo.texture), m.logo = {};
    try {
        const gt3r::Tex1 tex = gt3r::Tex1::parse(set);
        if (tex.textures.empty()) return;
        gt3r::GsMemory gs;
        tex.upload(set, gs);
        const gt3r::Tex1::Image img = tex.decode(gs, 0);
        m.logo = {m.make_texture(img), float(img.width), float(img.height)};
    } catch (const std::exception&) {
    }
}

// 0x38ECB8: the rank's digit in FC_48, rolling up or down to a new place (a fifth of the way a step, at least a hundredth) between
// the lines just over its baseline; its "st" in gray, 0.9 of a digit's width on (1.2 on a wide picture).
void PrologueHud::Impl::draw_rank(const hr::HudRow& row, int place_now, float opacity) {
    const float size = float(row.w) / 100;
    FontSpec f = font("FC_48", 100 * size);
    const float x = left_of(row.x, 0, row.align), y = row.y;
    const float digit = line_height(f) * size;
    if (rank_shown <= 0) rank_shown = float(place_now);
    const float want = float(place_now);
    float d = (want - rank_shown) * 0.2f;
    if (d > 0) rank_shown = std::min(rank_shown + std::max(d, 0.01f), want);
    else if (d < 0) rank_shown = std::max(rank_shown + std::min(d, -0.01f), want);
    const int whole = int(rank_shown);
    const float part = rank_shown - float(whole);
    const float clip[2] = {y - digit + 1, y - digit + 1 + digit + 6};
    const float up = part * digit;
    text(f, x, y - up, kOrange, opacity, std::to_string(whole), false, clip);
    if (part > 0) text(f, x, y - up + digit, kOrange, opacity, std::to_string(whole + 1), false, clip);
    static const char* const kEnds[] = {"", "s", "n", "r", "t", "t", "t", "t", "t", "t"};
    if (place_now >= 1 && place_now <= 9) text(f, x + float(int(width(f, "0") * 0.9f)), y, kGray, opacity, kEnds[place_now]);
}

// 0x394448: "Lap" in DC_12, the lap in FC_26, the slash and the laps at 0.77.
void PrologueHud::Impl::draw_lap(const hr::HudRow& row, int lap, int laps, float opacity) {
    float x = left_of(row.x, 0, row.align);
    const float y = row.y;
    const FontSpec label = font("DC_12");
    const std::string word = "Lap";
    text(label, x, y, kGray, opacity, word);
    x += width(label, word) + 10;
    FontSpec big = font("FC_26");
    const std::string now = std::to_string(lap);
    const float down = (line_height(big) - line_height(label)) * 0.3f;
    text(big, x, y + down, kOrange, opacity, now);
    if (laps <= 0) return;
    x += float(int(width(big, now)));
    FontSpec small = big;
    small.sx = small.sy = 0.77f;
    text(small, x, y + down, kGray, opacity, "/");
    x += 15;
    text(small, x, y + (line_height(big) * 0.77f - line_height(label)) * 0.3f, kOrange, opacity, std::to_string(laps));
}

// 0x38FC40: a time in FC_16 at 0.9 ending at the pen, its icon (right and bottom at an offset from the time's left) or its label in
// DC_12 ending 10 left of at least 70.
void PrologueHud::Impl::draw_time(float x, float y, int ha, s64 ms, const std::string& label, const char* icon, float ix, float iy, float opacity) {
    const float pen = x + (ha == 5 ? wide_side() : ha == 4 ? -wide_side() : 0);
    const FontSpec f = font("FC_16", 90);
    const std::string t = time_text(ms);
    text(f, pen, y, kOrange, opacity, t, true);
    float back = std::max(width(f, t), 70.0f) + 10;
    if (icon) {
        const Picture& p = picture(icon);
        draw_picture(p, {pen - back + ix - p.w, y + iy - p.h, p.w, p.h}, 0x80FFFFFF, opacity, 0.502f);
        back += p.w;
    }
    if (!label.empty()) text(font("DC_12"), pen - back, y, kGray, opacity, label, true);
}

// 0x398958: the course's map, the other cars' marks at 0.66, the player's pulsing.
void PrologueHud::Impl::draw_map(const hr::HudRow& row, const HudState& s, float opacity) {
    if (!map_texture) return;
    const float kx = row.w ? float(row.w) / 100 : 1, ky = row.h ? float(row.h) / 100 : 1;
    const float w = float(map.image.width), h = float(map.image.height);
    const Box at{left_of(row.x, 0, row.align), float(row.y), w * kx, h * ky};
    SDL_SetTextureColorMod(map_texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(map_texture, u8(255 * std::clamp(opacity, 0.0f, 1.0f)));
    const SDL_FRect dst = to_ui(at);
    SDL_RenderTexture(r, map_texture, nullptr, &dst);
    const Picture& point = picture("GT4_meter_texture_point");
    const auto mark = [&](const HudState::Dot& d, float size, u32 tint, float a) {
        float mx, my;
        map.map(d.x, d.z, mx, my);
        draw_picture(point, {at.x + mx * kx - point.w * size / 2, at.y + my * ky - point.h * size / 2, point.w * size, point.h * size}, tint, a);
    };
    for (size_t i = 1; i < s.cars.size(); ++i) mark(s.cars[i], 0.66f, kOrange, opacity);
    if (!s.cars.empty()) {
        // (a half second's pulse: up in a tenth, down in three)
        const float t = float(std::fmod(clock, 0.5));
        const float a = t < 0.25f ? std::min(1.0f, t / 0.1f) : std::max(0.0f, 1.0f - (t - 0.25f) / 0.3f);
        mark(s.cars[0], 1, kGreen, opacity * a);
    }
}

// 0x397268 / 0x397308 / 0x397968 / 0x397518: a dial: its black base, the red zone's ring (twice, at a third), the face, and the needle
// (eight copies at a sixth, led by its motion).
void PrologueHud::Impl::draw_dial(const Box& b, const std::string& face, float value, float min, float max, float a0, float a1, float red_from, Needle& n, float opacity, const DialShape& shape) {
    const Picture& base = picture(shape.base);
    const Picture& p = picture(face);
    draw_picture(base, panel(b), 0x80000000, opacity);
    const float kx = p.w > 0 ? b.w / p.w : 1, ky = p.h > 0 ? b.h / p.h : 1;
    const float cx = b.x + float(int(b.w) / 2), cy = b.y + float(int(b.h) / 2) + shape.down * ky;
    const float rx = shape.radius * kx, ry = shape.radius * ky;
    const float rad = kPi / 180;
    if (red_from > 0) {
        const float inner = 0.73548f, outer = inner + 0.26450f;
        const float from = a0 + (a1 - a0) * (red_from - min) / (max - min);
        const int parts = std::max(2, int((a1 - from) * 36 / 360));
        if (a1 > from) {
            const Rgba8 c = gs(0x80000099, opacity / 3);
            for (int pass = 0; pass < 2; ++pass) {
                const float jx = pass ? 0.5f : 0.0f, jy = pass ? 0.5f : 0.0f;
                std::vector<SDL_Vertex> v;
                for (int k = 0; k < parts; ++k) {
                    const float q0 = (from + (a1 - from) * float(k) / float(parts)) * rad, q1 = (from + (a1 - from) * float(k + 1) / float(parts)) * rad;
                    const auto at = [&](float q, float s) { return vertex(cx + jx + std::cos(q) * s * rx, panel_y(cy + jy + std::sin(q) * s * ry), c); };
                    v.push_back(at(q0, inner)), v.push_back(at(q0, outer)), v.push_back(at(q1, inner));
                    v.push_back(at(q1, inner)), v.push_back(at(q0, outer)), v.push_back(at(q1, outer));
                }
                triangles(v);
            }
        }
    }
    draw_picture(p, panel(b), 0x80909090, opacity);
    const float angle = std::clamp(a0 + (a1 - a0) * (value - min) / (max - min), a0, a1 + 2);
    const float lead = n.set ? (angle - n.angle) * 0.125f : 0;
    n.angle = angle, n.set = true;
    static const float kJitter[8][2] = {{0.5f, 0}, {-0.5f, 0.5f}, {0.5f, 0}, {-0.5f, -0.25f}, {0.5f, 0}, {-0.5f, 0.5f}, {0.5f, 0}, {-0.5f, -0.75f}};
    const float back = shape.tail, length = shape.tip - back;
    float jx = 0, jy = 0;
    const Rgba8 c = gs(kRed, opacity / 6);
    for (int k = 0; k < 8; ++k) {
        const float q = (angle + lead * float(k + 1)) * rad, co = std::cos(q), si = std::sin(q);
        const auto at = [&](float u, float w) {
            const float x = back + u * length, y = w * length;
            return vertex(cx + jx + (co * x - si * y) * rx, panel_y(cy + jy + (si * x + co * y) * ry), c);
        };
        const SDL_Vertex q0 = at(1, 0.01288f), q1 = at(0, 0.02256f), q2 = at(1, -0.01288f), q3 = at(0, -0.02256f);
        triangles({q0, q1, q2, q2, q1, q3});
        jx += kJitter[k][0], jy += kJitter[k][1];
    }
}

// 0x398520 / 0x398660: the boost gauge of a car with a turbocharger, 0 .. 2 over half a turn, at (508, 418).
void PrologueHud::Impl::draw_boost(const Extra& e, float opacity) {
    if (!e.turbo) return;
    DialShape s;
    s.base = "GT4_meter_texture_trb_base", s.down = 0, s.radius = 32, s.tail = -0.53125f, s.tip = 0.90625f;
    draw_dial({508, 418 - 64, 64, 64}, "GT4_meter_texture_trb_scale", std::min(e.boost, 2.0f), 0, 2, 90, 270, 0, needles[2], opacity, s);
}

// A hybrid's panel in place of the dials (0x3930D8, 0x393300, 0x3933C8): the picture at (617, 428) of 406 x 42, the speed, the mileage
// and the fuel (thousandths of a liter) over it, the stability lamp at its right end and the brake's at its left.
void PrologueHud::Impl::draw_hybrid(const HudState& s, const Extra& e, float opacity) {
    const Picture& base = picture("meter_prius_base");
    const float x = 617 + wide_side(), y = 428, w = 406, h = 42;
    draw_picture(base, {x - base.w, y - base.h, base.w, base.h}, 0x80FFFFFF, opacity);
    char b[32];
    std::snprintf(b, sizeof b, "%3d", int(std::fabs(s.speed) * 3.6f + 0.5f));
    text(font("DC_24_5"), x - 300, y - 12, 0x809CB246, opacity, b, true);
    const FontSpec f = font("FC_16", 90);
    std::snprintf(b, sizeof b, "%2.1f", double(std::floor((e.hybrid_mileage + 0.05f) * 10) / 10));
    text(f, x - 138, y - 12, 0x80B58F43, opacity, b, true);
    std::snprintf(b, sizeof b, "%4d", std::min(int(e.hybrid_fuel * 1000 + 0.0005f), 9999));
    text(f, x - 42, y - 12, 0x800363FD, opacity, b, true);
    const float blink = std::fmod(clock, 0.2) < 0.1 ? 1.0f : 0.0f;
    const int aid = std::max(e.asm_status, e.tcs_status);
    const Picture& vsc = picture("meter_prius_vsc");
    draw_picture(vsc, {x - vsc.w, y - vsc.h, vsc.w, vsc.h}, 0x800099FF, opacity * (aid >= 2 ? blink : aid == 1 ? 0.5f : 0));
    const Picture& brake = picture("meter_prius_brake");
    draw_picture(brake, {x - w, y - h, brake.w, brake.h}, 0x800000C8, opacity * (e.parking_brake ? 1.0f : 0.0f));
}

// The hybrid's monitor's picture: its palette's indices kept, its colors changed each picture.
void PrologueHud::Impl::load_monitor(ByteSpan set) {
    Bytes copy(set.begin(), set.end());
    const gt3r::Tex1 tex = gt3r::Tex1::parse(copy);
    const gt3r::Tex1::Buffer* clut = nullptr;
    for (const auto& buffer : tex.buffers)
        if (buffer.width == 16 && buffer.height == 16) clut = &buffer;
    if (tex.textures.empty() || !clut || size_t(clut->offset) + 1024 > copy.size()) return;
    const auto decode = [&] {
        gt3r::GsMemory gs;
        tex.upload(copy, gs);
        return tex.decode(gs, 0);
    };
    const gt3r::Tex1::Image image = decode();
    for (u32 k = 0; k < 256; ++k) {
        u32 x = 0, y = 0;
        gt3r::csm1_position(k, x, y);
        u8* at = &copy[clut->offset + 4 * (y * 16 + x)];
        flow_palette[k] = u32(at[0]) | u32(at[1]) << 8 | u32(at[2]) << 16 | u32(std::min(255, at[3] * 2)) << 24;
        at[0] = u8(k), at[1] = at[2] = 0, at[3] = 0x80;
    }
    const gt3r::Tex1::Image indices = decode();
    if (indices.width != image.width || indices.rgba.size() != image.rgba.size()) return;
    flow_index.resize(size_t(image.width) * image.height);
    for (size_t k = 0; k < flow_index.size(); ++k) flow_index[k] = u8(indices.rgba[k]);
    flows = {make_texture(image), float(image.width), float(image.height)};
}

// 0x393718 .. 0x393E50: the energy's flows, lit along their eight colors of the picture's palette in waves (three a second), and the
// store's eighths, at (26, 425).
void PrologueHud::Impl::draw_flows(const Extra& e, float opacity) {
    if (!flows.texture) return;
    flow_phase = float(std::fmod(double(flow_phase) + 3 * step, 1.0));
    std::array<u32, 256> colors = flow_palette;
    struct Flow {
        u32 row, start, count;
        bool up;
        std::array<float, 3> color;
    };
    const std::array<float, 3> orange{0.99f, 0.39f, 0.01f}, green{0.2f, 0.93f, 0.11f}, yellow{0.98f, 0.89f, 0}, none{};
    const int motor = (e.hybrid_flags >> 1) & 3, store = (e.hybrid_flags >> 4) & 3;
    const Flow list[] = {{0xD, 0, 8, false, e.hybrid_flags & 1 ? orange : none},
                         {0xC, 0, 8, motor == 1, motor == 1 ? green : motor == 2 ? yellow : none},
                         {0xB, 0, 8, false, e.hybrid_flags & 8 ? orange : none},
                         {0xA, 1, 1, false, store == 1 ? green : store == 3 ? yellow : none},
                         {0xA, 0, 1, false, store == 2 ? yellow : none}};
    for (const Flow& f : list)
        for (u32 k = 0; k < f.count; ++k) {
            const float x = float(k) / float(f.count) + (f.up ? flow_phase : -flow_phase);
            const float v = (std::sin(x * 2 * kPi) * 0.5f + 0.5f) * 255;
            colors[f.row * 16 + f.start + k] = u32(v * f.color[0]) | u32(v * f.color[1]) << 8 | u32(v * f.color[2]) << 16 | 0xFF000000u;
        }
    for (u32 k = 0; k < 8; ++k) colors[0x90 + k] = float(k + 1) / 8 <= e.hybrid_store ? 0xFF00E3FAu : 0xFF202020u;
    std::vector<u32> pixels(flow_index.size());
    for (size_t k = 0; k < pixels.size(); ++k) pixels[k] = colors[flow_index[k]];
    SDL_UpdateTexture(flows.texture, nullptr, pixels.data(), int(flows.w) * 4);
    draw_picture(flows, {left_of(26, flows.w, 4), 425 - flows.h, flows.w, flows.h}, 0x80FFFFFF, opacity);
}

// 0x391110: the pedals of a replay's monitor, the brake left and the throttle right: a dark box each, a bar up it to the lesser of the
// pedal's last two values, fading out to the greater, and the sign under it.
void PrologueHud::Impl::draw_pedals(const hr::HudRow& row, const Extra& e, float opacity) {
    const float w = row.w, h = row.h, x = left_of(row.x, 2 * w + 4, row.align), y = row.valign == 1 ? row.y - h : float(row.y);
    const float sign = float(int(w - 4) * 16 / 22), bar = h - sign - 8;
    const std::array<float, 2> now{std::clamp(e.brake, 0.0f, 1.0f), std::clamp(e.accelerator, 0.0f, 1.0f)};
    for (int k = 0; k < 2; ++k) {
        const float left = x + float(k) * (w + 4);
        fill({left, y, w, h}, 0x80000000, opacity * 0.5f);
        fill({left + 2, y + 2, w - 4, bar}, 0x80202020, opacity);
        const Picture& p = picture(k ? "replay_sign_accel" : "replay_sign_brake");
        draw_picture(p, {left + 2, y + h - sign - 4, w - 4, sign}, 0x80FFFFFF, opacity);
        const float lo = std::min(now[size_t(k)], pedals[size_t(k)]), hi = std::max(now[size_t(k)], pedals[size_t(k)]);
        const u32 color = k ? 0x802BCC49 : 0x800000F0;
        const float bottom = y + 2 + bar, at_lo = bottom - bar * lo, at_hi = bottom - bar * hi;
        fill({left + 2, at_lo, w - 4, bottom - at_lo}, color, opacity);
        const Rgba8 c = gs(color, opacity), none = gs(color, 0);
        const SDL_Vertex a = vertex(left + 2, at_hi, none), b = vertex(left + w - 2, at_hi, none), d = vertex(left + 2, at_lo, c), f = vertex(left + w - 2, at_lo, c);
        triangles({a, b, d, d, b, f});
    }
    pedals = now;
}

// 0x394028: the cones knocked down and the test's count as a time is written, with the cone's picture.
void PrologueHud::Impl::draw_cones(const hr::HudRow& row, const Extra& e, float opacity) {
    if (e.cones_total <= 0) return;
    const FontSpec f = font("FC_16", 90);
    const std::string t = std::to_string(e.cones) + "/" + std::to_string(e.cones_total);
    const Picture& p = picture("pylon");
    const float x = left_of(row.x, 0, row.align); // (❓ the pen's end in the program; its place on the row's left here)
    draw_picture(p, {x, row.y - p.h + 4, p.w, p.h}, 0x80FFFFFF, opacity, 0.502f);
    text(f, x + p.w + 10, row.y, kOrange, opacity, t);
}

// 0x38F5F8: the gear over the font's blank segments ('J'): 'A' + gear - 1, 'I' the reverse in red.
void PrologueHud::Impl::draw_gear(const FontSpec& f, float x, float y, int gear, float opacity) {
    text(f, x, y, 0x40000000, opacity, "J");
    text(f, x, y, gear > 0 ? kOrange : kRed, opacity, std::string(1, gear > 0 ? char('@' + std::min(gear, 8)) : 'I'));
}

// The in-car view's panel (0x38D730's first list): the tachometer at (345, 431), the speedometer ending at 295, the gear's box and the
// lamps between them.
void PrologueHud::Impl::draw_onboard(const HudState& s, const Extra& e, float opacity) {
    const float kmh = std::fabs(s.speed) * 3.6f;
    // (0x3CDFF8: the tachometer's range the thousand under the limiter's revolutions + 1,500; the speedometer's 80 km/h steps over
    // 1.1 x the top speed, 160 .. 320)
    const float range = float(int((e.rev_limit + 1500) / 1000) * 1000);
    const Face* rev = &rev_faces.back();
    for (const Face& f : rev_faces)
        if (range <= f.limit) {
            rev = &f;
            break;
        }
    const float top = float(std::clamp(int(s.top_speed * 1.1f / 22.2222f) + 1, 2, 4) * 80);
    const Face* speed = &speed_faces.back();
    for (const Face& f : speed_faces)
        if (top <= f.limit) {
            speed = &f;
            break;
        }
    const Box tach{345, 431 - 122, 155, 122}, dial{295 - 155, 431 - 122, 155, 122};
    draw_boost(e, opacity);
    draw_dial(tach, rev->name, s.rpm, 1000, rev->max, 174, 394, e.shift_rpm, needles[1], opacity);
    draw_dial(dial, speed->name, kmh, 0, speed->max, 146, 366, 0, needles[0], opacity);
    // The lamps (0x390D60): the fuel, the stability control's triangle (half when on, blinking while it acts), the oil, the brake.
    {
        const float x = 422 - 44, y = panel_y(377 - 38);
        const auto lamp = [&](const char* name, u32 color, float rgb) {
            const Picture& p = picture(name);
            draw_picture(p, {x, y, p.w, p.h * 448 / 480}, color, opacity, rgb);
        };
        const float blink = std::fmod(clock, 0.2) < 0.1 ? 1.0f : 0.0f;
        const int aid = std::max(e.asm_status, e.tcs_status);
        lamp("GT4_meter_texture_wrng_gas", kRed, 0);
        lamp("GT4_meter_texture_asmtcs", kGreen, aid >= 2 ? blink : aid == 1 ? 0.5f : 0);
        lamp("GT4_meter_texture_wrng_oil", kRed, 0);
        lamp("GT4_meter_texture_wrng_brk", kRed, e.parking_brake ? 1.0f : 0.0f);
    }
    // The speed (0x393F10): ends half of "000" right of the dial's middle, two lines over it.
    {
        const FontSpec f = font("DC_24_5");
        const float w = float(int(width(f, "000")));
        char b[16];
        std::snprintf(b, sizeof b, "%3d", int(kmh + 0.5f));
        text(f, float(int(dial.x) + 77) - float(int(w) / 2) + w, panel_y(dial.y + 61 - 2), kOrange, opacity, b, true);
    }
    // The gear's box (0x38F220).
    {
        const Box b{320 - 36, 387 - 65, 72, 65};
        draw_picture(picture("GT4_meter_texture_sgtg_base"), panel(b), 0x60000000, opacity);
        FontSpec f = font("DC_24_5", 90, 1);
        const float x = b.x + 36, y = b.y + line_height(f);
        draw_gear(f, x - 12, panel_y(y + 3), s.gear < 0 ? 0 : s.gear, opacity);
        FontSpec small = f;
        small.sx = small.sy = 0.81f;
        text(small, x - 9, panel_y(y + 32), 0x40000000, opacity, "J");
        if (s.suggested_gear > 0 && s.suggested_gear != s.gear)
            text(small, x - 9, panel_y(y + 32), kRed, opacity * (std::fmod(clock, 0.5) < 0.25 ? 1.0f : 0.0f), std::string(1, char('@' + std::min(s.suggested_gear, 8))));
    }
}

// The other views' panel (0x3928A0 / 0x3929D8): the speed, the gear and a small tachometer on a dark box at (615, 425).
void PrologueHud::Impl::draw_simple(const HudState& s, const Extra& e, float opacity) {
    const float x = 615 + wide_side(), y = 425, bar = 64; // (a wide picture's: 48 on 4:3)
    cut_box({x - 121, y - 29, 126, 35}, 3, 0x50000000, opacity);
    const float at = x - bar;
    // The gear and the suggested one.
    {
        FontSpec f = font("DC_24_5", 90, 1);
        draw_gear(f, at - 52, y, s.gear < 0 ? 0 : s.gear, opacity);
        FontSpec small = f;
        small.sx = small.sy = 0.81f;
        text(small, at - 52 + 26, y, 0x40000000, opacity, "J");
        if (s.suggested_gear > 0 && s.suggested_gear != s.gear)
            text(small, at - 52 + 26, y, kRed, opacity * (std::fmod(clock, 0.5) < 0.25 ? 1.0f : 0.0f), std::string(1, char('@' + std::min(s.suggested_gear, 8))));
    }
    // The speed and its unit.
    {
        char b[16];
        std::snprintf(b, sizeof b, "%3d", int(std::fabs(s.speed) * 3.6f + 0.5f));
        text(font("DC_24_5"), at - 94, y - 1, kOrange, opacity, b, true);
        text(font("sw_12", 75), at - 94, y - 2, kGray, opacity, "km/h");
    }
    // 0x397C80: the bar, the picture's alpha its scale: black whole, red to the revolutions' share of the red line x 56 (at most 64),
    // orange to at most the red line's. ❓ Drawn here as the share of the picture's width, its alpha ramp being across.
    {
        const Picture& p = picture("meter_mini");
        const Box b{x - bar, y - 24, bar, 24};
        draw_picture(p, b, 0x80000000, opacity * 0.5f);
        const float share = e.shift_rpm > 0 ? s.rpm / e.shift_rpm : 0;
        const auto part = [&](float f, u32 c) {
            if (!p.texture || f <= 0) return;
            const Rgba8 k = gs(c, opacity);
            SDL_SetTextureColorMod(p.texture, k.r, k.g, k.b);
            SDL_SetTextureAlphaMod(p.texture, k.a);
            const SDL_FRect src{0, 0, p.w * f, p.h}, dst = to_ui({b.x, b.y, b.w * f, b.h});
            SDL_RenderTexture(r, p.texture, &src, &dst);
        };
        part(std::clamp(share * 56, 4.0f, 64.0f) / 64, kRed);
        part(std::clamp(std::min(share, 1.0f) * 56, 4.0f, 64.0f) / 64, 0x801972FF);
    }
}

// 0x390480: a message's text centered, fitted to the row's width.
void PrologueHud::Impl::draw_message(float x, float y, const FontSpec& f, u32 color, float opacity, const std::string& s) {
    text(f, x - width(f, s) / 2, y, color, opacity, s);
}

void PrologueHud::draw(Ui& ui, const HudState& s, const Extra& e) {
    if (!ready_) return;
    Impl& m = *impl_;
    m.r = ui.renderer();
    const Uint64 now = SDL_GetTicksNS();
    m.step = m.last_ns ? std::min(0.1, double(now - m.last_ns) / 1e9) : 0;
    m.clock += m.step;
    m.last_ns = now;
    if (!e.live && !e.guide) { // (a replay: the display hidden by default; the mark of the game fades in and out over a minute)
        const double t = std::fmod(m.clock + 54, 60.0);
        const float a = t < 30 ? float(std::clamp(t / 3, 0.0, 1.0)) : float(std::clamp((33 - t) / 3, 0.0, 1.0));
        const Impl::Picture& p = m.picture("gt_logo_mini");
        m.draw_picture(p, {left_of(604, p.w, 5), 423 - p.h, p.w, p.h}, 0x80808080, 0.5f * a);
        if (!s.replay_display) return;
    }
    hr::HudModeSelect select;
    select.run = e.run, select.event_laps = s.laps, select.play = e.live ? hr::HudPlay::Live : hr::HudPlay::Replay;
    const hr::HudMode mode = hr::hud_mode(select);
    // The opening (0x389D30): the course's logo for 5 s from half a second (fading in over half a second and out over one), the display
    // fading in over half a second from 4.5 s before the start.
    float opacity = std::clamp(s.opacity, 0.0f, 1.0f);
    if (e.live && e.start_left >= 0 && e.start_total > 0) {
        const float t = float(e.start_total - e.start_left);
        opacity *= std::clamp((t - float(std::max(0.0, e.start_total - 4.5))) / 0.5f, 0.0f, 1.0f);
        const float length = float(std::clamp(e.start_total - 5, 0.0, 5.0)), u = t - 0.5f;
        if (u > 0 && u < length && m.logo.texture) {
            const float a = std::min({1.0f, u / 0.5f, length - u});
            m.draw_picture(m.logo, m.place(m.logo, 604, 412, 5, 1), 0x80808080, a);
        }
    }
    // A test's run over: the display gone in a quarter of a second, a passed one's target times kept (✔ the original's; ❓ the
    // routine not read).
    const float target_opacity = opacity;
    if (e.over_t >= 0) opacity *= std::clamp(1 - e.over_t / 0.25f, 0.0f, 1.0f);
    const int view = e.onboard ? 0 : 1;
    for (const hr::HudRow& row : hr::hud_layout(mode)) {
        if (row.view != hr::kAnyView && int(row.view) != view) continue;
        // (a lesson's guide film: the pedals, the clock and the simple panel; ✔ the original's lesson 1)
        if (e.guide && row.part != hr::HudPart::AccelBrake && row.part != hr::HudPart::TimeLine && row.part != hr::HudPart::Meters) continue;
        switch (row.part) {
        case hr::HudPart::Rank:
            if (s.show_position && mode != hr::HudMode::License) m.draw_rank(row, s.place, opacity);
            break;
        case hr::HudPart::LapCount:
            if (s.show_lap && s.lap > 0) m.draw_lap(row, s.lap, s.laps, opacity);
            break;
        case hr::HudPart::Meters:
            if (!s.show_meter) break;
            if (e.hybrid) {
                if (int(row.sub) == view) m.draw_hybrid(s, e, opacity), m.draw_flows(e, opacity);
            } else if (row.sub == 0 && view == 0) m.draw_onboard(s, e, opacity);
            else if (row.sub == 1 && view == 1) m.draw_simple(s, e, opacity);
            break;
        case hr::HudPart::Map:
            if (s.show_map) m.draw_map(row, s, opacity);
            break;
        case hr::HudPart::TimeLine: {
            if (!s.show_time) break;
            switch (hr::TimeLine(row.sub)) {
            case hr::TimeLine::Best:
            case hr::TimeLine::BestTime:
            case hr::TimeLine::Fastest: m.draw_time(row.x, row.y, row.align, s.best_ms, "Best", nullptr, 0, 0, opacity); break;
            case hr::TimeLine::Last: m.draw_time(row.x, row.y, row.align, s.last_ms, "Last", nullptr, 0, 0, opacity); break;
            case hr::TimeLine::Total: m.draw_time(row.x, row.y, row.align, s.total_ms, "Total", nullptr, 0, 0, opacity); break;
            case hr::TimeLine::Lap:
            case hr::TimeLine::Time: m.draw_time(row.x, row.y, row.align, std::max<s64>(0, s.lap_ms), "", "GT4_meter_texture_laptime", -2, 4, opacity); break;
            default:
                if (row.sub == 15) m.draw_cones(row, e, opacity); // (RaceCountDisplay)
                break;
            }
            break;
        }
        case hr::HudPart::Target: {
            static const char* const kMarks[] = {"mark_1st", "mark_2nd", "mark_3rd"};
            if (row.sub < 3) m.draw_time(row.x, row.y, row.align, e.targets[row.sub] > 0 ? e.targets[row.sub] : -1, "", kMarks[row.sub], 2, 8, e.passed ? target_opacity : opacity);
            break;
        }
        case hr::HudPart::Messages:
            // 0x386740: the seconds left of a test's lead; 0x38A890: the wrong-way sign, its blink of 0.2 s lit for 0.06 (in 0.05 s, out
            // 0.1), not in a test of cones; AUTO DRIVE while the car's driver brings it to the line.
            if (e.time_left > 0) {
                std::string t = m.words.count("KeepLeader") ? m.words["KeepLeader"] : "%d";
                if (const size_t at = t.find("%d"); at != std::string::npos) t.replace(at, 2, std::to_string(e.time_left));
                m.draw_message(row.x, row.y, m.font("sw_16"), kOrange, opacity, t);
            }
            else if (e.penalty <= 0 && e.wrong_way && e.cones_total <= 0) {
                m.sign += (std::fmod(m.clock, 0.2) < 0.06 ? 1 / 0.05f : -1 / 0.1f) * float(m.step);
                m.sign = std::clamp(m.sign, 0.0f, 1.0f);
                const Impl::Picture& p = m.picture("GT4_meter_texture_wrongway");
                m.draw_picture(p, {row.x - p.w / 2, row.y - p.h / 2, p.w, p.h}, 0x80FFFFFF, opacity * m.sign);
            } else if (e.penalty <= 0 && e.auto_drive) {
                const auto it = m.words.find("AutoDrive");
                m.draw_message(row.x, row.y, m.font("sw_16"), kOrange, opacity, it != m.words.end() ? it->second : std::string("AUTO DRIVE"));
            }
            // 0x38A7B8: the penalty's seconds.
            if (e.penalty > 0) {
                std::string t = m.words.count("Penalty") ? m.words["Penalty"] : "%d";
                char b[128];
                if (const size_t at = t.find("%d"); at != std::string::npos) t.replace(at, 2, (std::snprintf(b, sizeof b, "%d", e.penalty), std::string(b)));
                m.draw_message(row.x, row.y, m.font("sw_16"), kRed, opacity, t);
            }
            break;
        case hr::HudPart::Events:
            if (!s.event.empty()) m.draw_message(row.x, row.y, m.font("FC_16", 90), kEvent, opacity * s.event_opacity, s.event);
            if (!s.event_gap.empty()) m.draw_message(row.x, row.y + 20, m.font("FC_16", 90), kEvent, opacity * s.event_gap_opacity, s.event_gap);
            break;
        case hr::HudPart::AccelBrake:
            if (row.sub == 0 && !e.live) m.draw_pedals(row, e, opacity);
            break;
        case hr::HudPart::Title:
            // The count (0x38A558: 0.8 s a second, fading over the last 0.15), START (0x38CDC8: one second), FINISH (0x38CE68: three,
            // fading over the last).
            if (row.sub == 0 && e.live) {
                const auto out = [](double t, double length, double fade) {
                    return t < 0 || t > length ? 0.0f : t > length - fade ? float((length - t) / fade) : 1.0f;
                };
                std::string t;
                float a = 0;
                if (e.finish_t >= 0) t = "FINISH", a = out(e.finish_t, 3, 1);
                else if (e.start_left >= 0 && e.start_left <= 3) {
                    const int n = int(std::ceil(e.start_left));
                    t = std::to_string(n), a = n > 0 ? out(double(n) - e.start_left, 0.8, 0.15) : 0;
                } else if (e.start_left < 0) t = "START", a = out(e.since_start, 1, 0.15);
                m.draw_message(row.x, row.y, m.font("FC_48"), kOrange, a, t);
            }
            break;
        default: break;
        }
    }
}

} // namespace gt4r
