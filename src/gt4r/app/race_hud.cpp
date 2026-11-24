#include "gt4r/app/race_hud.h"
#include "gt4r/menu/text_layout.h"

#include "base/log.h"
#include "disc/core_image.h"
#include "formats/gs_memory.h"
#include "formats/text/text_layout.h"
#include "gt4r/menu/font.h"
#include "gt4r/menu/image_pack.h"
#include "gt4r/menu/text_table.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <map>

namespace gt4r {

namespace {

using race::DisplayPlace;
using race::DisplayRect;
using race::Rgba8;
namespace element = race::element;

constexpr float kPi = 3.14159265f;
// The display's canvas is 640 x 480 (y down); on a wide picture it runs from x = -106.667 to
// 746.667, and the whole is drawn at one scale so that everything keeps its shape.
constexpr float kCanvasH = 480.0f, kWideLeft = -106.667f;
constexpr float kScale = float(Ui::kHeight) / kCanvasH;

constexpr u32 kTextGray = 0x80C8C8C8u, kMeterBlue = 0x80E3B896u, kNeutral = 0x80808080u;

SDL_FRect to_ui(const DisplayRect& r) { return {(r.x - kWideLeft) * kScale, r.y * kScale, r.w * kScale, r.h * kScale}; }
SDL_FPoint to_ui(float x, float y) { return {(x - kWideLeft) * kScale, y * kScale}; }

// A text color: the GS color's RGB as it is, its alpha with 0x80 as opaque.
Rgba8 text_color(u32 c, float opacity) {
    Rgba8 o;
    o.r = u8(c), o.g = u8(c >> 8), o.b = u8(c >> 16);
    o.a = u8(std::clamp(float(std::min(255u, (c >> 24) * 255u / 128u)) * opacity, 0.0f, 255.0f));
    return o;
}

// Pen flags of the game's text draw.
constexpr int kEndsAtPen = 2, kCentered = 3, kMiddleOfLine = 12;

} // namespace

struct RaceHud::Impl {
    struct Picture {
        SDL_Texture* texture = nullptr;
        float w = 0, h = 0;
    };
    SDL_Renderer* r = nullptr;
    gt3r::text::FontFile fonts;
    std::map<std::string, const gt3r::text::LatinFont*> font_by_name;
    std::map<std::pair<const gt3r::text::LatinFont*, int>, SDL_Texture*> atlases;
    std::map<std::string, menu::FontSpacing> spacing; // the fonts' GT4 spacing (FT01 +0x14, +0x15) by name
    gt3r::text::FontFile menu_fonts; // the movies' (font/menu.fnt)
    std::map<std::pair<const race::StrobeMovie*, size_t>, SDL_Texture*> movie_textures;
    Bytes pack_file;
    std::optional<menu::ImagePack> pack;
    std::map<std::string, Picture> pictures;
    Picture course_logo;
    race::DisplayLayout layout, mode1;
    const race::DisplayLayout* cur = &layout; // the layout drawn now
    std::array<menu::TextColor, 4> button_colors{};
    std::vector<race::DialFace> rev_faces, speed_faces, speed_faces_by[2]; // the speedometer's: now, km/h, mph
    race::CourseMap map;
    SDL_Texture* map_texture = nullptr;
    std::optional<menu::TextTable> words;
    bool mph = false;
    float needle_from[2] = {0, 0}; // the dials' needles last frame (speed, revs), for their trail
    bool needle_set = false;

    SDL_Texture* make_texture(const gt3r::Tex1::Image& img) {
        if (!img.width || !img.height) return nullptr;
        SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(img.width), int(img.height));
        if (!t) return nullptr;
        SDL_UpdateTexture(t, nullptr, img.rgba.data(), int(img.width * 4));
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        return t;
    }

    // A picture of the pack by its short name ("JP/lap"): the pack's entry image/<name>.png.
    const Picture& picture(const std::string& name) {
        auto it = pictures.find(name);
        if (it != pictures.end()) return it->second;
        Picture p;
        if (pack) {
            if (const menu::ImagePack::Entry* e = pack->find("image/" + name + ".png")) {
                try {
                    const Bytes set = pack->read(*e);
                    const gt3r::Tex1 tex = gt3r::Tex1::parse(set);
                    if (!tex.textures.empty()) {
                        gt3r::GsMemory gs;
                        tex.upload(set, gs);
                        const gt3r::Tex1::Image img = tex.decode(gs, 0);
                        p.texture = make_texture(img);
                        p.w = float(img.width), p.h = float(img.height);
                    }
                } catch (const std::exception& e2) {
                    LOG_WARN("Race display: picture %s: %s", name.c_str(), e2.what());
                }
            }
        }
        return pictures.emplace(name, p).first->second;
    }

    // A picture over a rectangle of the canvas, tinted (0x80 neutral); `part`: the left part of it.
    void draw_picture(const Picture& p, const DisplayRect& at, u32 tint, float opacity, float part = 1) {
        if (!p.texture || opacity <= 0 || part <= 0) return;
        const Rgba8 c = race::gs_color(tint, opacity);
        SDL_SetTextureColorMod(p.texture, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(p.texture, c.a);
        const SDL_FRect src{0, 0, p.w * part, p.h};
        const SDL_FRect dst = to_ui({at.x, at.y, at.w * part, at.h});
        SDL_RenderTexture(r, p.texture, &src, &dst);
    }
    // A picture at a place of the layout, at the place's size or its own.
    DisplayRect draw_at(const std::string& name, const DisplayPlace& place, u32 tint, float opacity, float part = 1) {
        const Picture& p = picture(name);
        const DisplayRect at = race::place_rect(place, p.w, p.h, true);
        draw_picture(p, at, tint, opacity, part);
        return at;
    }

    // "FC_26*80,80#1": a font, its scale in percent, its palette (1: the plain glyphs).
    struct FontSpec {
        const gt3r::text::LatinFont* font = nullptr;
        float sx = 1, sy = 1;
        int variant = 0;
    };
    FontSpec font(const char* name, float percent = 100, int variant = 0) {
        FontSpec f;
        const auto it = font_by_name.find(name);
        f.font = it == font_by_name.end() ? nullptr : it->second;
        f.sx = f.sy = percent / 100.0f;
        f.variant = variant;
        return f;
    }
    float text_width(const FontSpec& f, const std::string& s) {
        if (!f.font) return 0;
        gt3r::text::LayoutOptions opt;
        opt.scale_x = f.sx, opt.scale_y = f.sy;
        std::u32string codes;
        for (char32_t c : gt3r::text::utf8_decode(s)) codes += c;
        return gt3r::text::layout_text(gt3r::text::map_unicode(codes, *f.font, nullptr, nullptr), *f.font, nullptr, 0, 0, opt).width;
    }
    // Text at a pen of the canvas. `flags`: bits 0-1: 0 it starts at the pen, 2 it ends there, 3 it
    // is centered on it; bits 2-3: 0 the pen is on the baseline, 12 at the middle of the line.
    // `squeeze`: an x factor on top of the font's. Returns the text's left edge.
    float draw_text(const FontSpec& f, float x, float y, int flags, u32 color, float opacity, const std::string& s, float squeeze = 1) {
        if (!f.font || s.empty() || opacity <= 0) return x;
        gt3r::text::LayoutOptions opt;
        opt.scale_x = f.sx * squeeze, opt.scale_y = f.sy;
        opt.align = (flags & 3) == kEndsAtPen ? gt3r::text::HAlign::Right : (flags & 3) == kCentered ? gt3r::text::HAlign::Center : gt3r::text::HAlign::Left;
        opt.valign = gt3r::text::VAlign::Baseline;
        if ((flags & 12) == kMiddleOfLine) {
            // The line's top is half a line height above the pen.
            y = y - float(f.font->line_height) * f.sy * 0.5f + float(f.font->ascent) * f.sy;
        }
        std::u32string codes; // (the game's strings are the font's own codes: 0x7F is its dash)
        for (char32_t c : gt3r::text::utf8_decode(s)) codes += c;
        const gt3r::text::TextLayout lay = gt3r::text::layout_text(gt3r::text::map_unicode(codes, *f.font, nullptr, nullptr), *f.font, nullptr, x, y, opt);
        SDL_Texture*& atlas = atlases[{f.font, f.variant}];
        if (!atlas) atlas = make_texture(f.font->atlas[size_t(std::clamp(f.variant, 0, 1))]);
        if (!atlas) return x;
        const Rgba8 c = text_color(color, opacity);
        SDL_SetTextureColorMod(atlas, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(atlas, c.a);
        for (const gt3r::text::GlyphQuad& q : lay.quads) {
            if (q.kanji || !q.sw || !q.sh) continue;
            const SDL_FRect src{float(q.sx), float(q.sy), float(q.sw), float(q.sh)};
            const SDL_FRect dst = to_ui({q.x, q.y, q.w, q.h});
            SDL_RenderTexture(r, atlas, &src, &dst);
        }
        return lay.x;
    }

    // Untextured triangles on the canvas (the needles, the red zone): colors as they are.
    void triangles(const std::vector<SDL_Vertex>& v) {
        if (v.size() >= 3) SDL_RenderGeometry(r, nullptr, v.data(), int(v.size() / 3 * 3), nullptr, 0);
    }
    static SDL_Vertex vertex(float x, float y, u8 red, u8 green, u8 blue, float alpha) {
        SDL_Vertex v{};
        v.position = to_ui(x, y);
        v.color = {float(red) / 255, float(green) / 255, float(blue) / 255, std::clamp(alpha, 0.0f, 1.0f)};
        return v;
    }

    void draw_top(const HudState& s);
    void draw_map(const HudState& s);
    void draw_chase_panel(const HudState& s);
    void draw_driver_panel(const HudState& s);
    void draw_dial(const DisplayRect& at, const std::string& face, float ox, float value, float min, float max, float a0, float a1,
                   float red_from, float& last_angle, float opacity);
    void draw_gear(const DisplayRect& box, const HudState& s);
    void draw_small_parts(const HudState& s, bool driver);
    void draw_bspec(const HudState& s, const std::string& pace_word, const std::string& help);
    // Text in GT4's menu layout with the pad's buttons ("%Bc"), from (x, baseline y).
    void draw_menu_text(const char* font_name, int variant, float x, float y, gt3r::text::HAlign align, u32 color, float opacity, const std::string& s,
                        float max_width = 0);
    void fill(const DisplayRect& at, u8 red, u8 green, u8 blue, float alpha) {
        triangles({vertex(at.x, at.y, red, green, blue, alpha), vertex(at.x + at.w, at.y, red, green, blue, alpha), vertex(at.x + at.w, at.y + at.h, red, green, blue, alpha),
                   vertex(at.x, at.y, red, green, blue, alpha), vertex(at.x + at.w, at.y + at.h, red, green, blue, alpha), vertex(at.x, at.y + at.h, red, green, blue, alpha)});
    }
    std::string speed_text(const HudState& s) const {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%3d", int(std::lround(std::fabs(s.speed) * (mph ? 2.2369363f : 3.6f))));
        return buf;
    }
};

RaceHud::RaceHud(App& app, ByteSpan gtcm) : impl_(std::make_unique<Impl>()) {
    Impl& m = *impl_;
    m.r = app.renderer();
    try {
        if (const std::optional<Bytes> file = app.vfs()->read("font/system.fnt")) {
            m.fonts = menu::read_fonts(*file);
            m.spacing = menu::read_font_spacing(*file);
        }
        for (const gt3r::text::LatinFont& f : m.fonts.fonts) m.font_by_name.emplace(f.name, &f);
        if (const std::optional<Bytes> file = app.vfs()->read("font/menu.fnt")) {
            m.menu_fonts = menu::read_fonts(*file);
            for (const auto& [name, sp] : menu::read_font_spacing(*file)) m.spacing.emplace(name, sp);
        }
        for (const gt3r::text::LatinFont& f : m.menu_fonts.fonts) m.font_by_name.emplace(f.name, &f);
        // The region's pack: the disc's own region first.
        const std::string serial = app.disc()->info().serial;
        m.mph = serial.rfind("SCUS", 0) == 0;
        std::vector<std::string> regions{m.mph ? "US" : serial.rfind("SCES", 0) == 0 ? "GB" : "JP", "US", "GB", "JP"};
        for (const std::string& region : regions) {
            if (std::optional<Bytes> file = app.vfs()->read("race_display/gt4/" + region + "/display.gpb"); file && menu::ImagePack::is_pack(*file)) {
                m.pack_file = std::move(*file);
                m.pack.emplace(menu::ImagePack::parse(m.pack_file));
                break;
            }
        }
        for (const char* path : {"rtext/raceUS.rt2", "rtext/raceGB.rt2", "rtext/raceJP.rt2"})
            if (const std::optional<Bytes> file = app.vfs()->read(path); file && menu::TextTable::is_table(*file)) {
                m.words.emplace(menu::TextTable::parse(*file));
                break;
            }
        // The elements' places and the dials' faces are tables of the game's program.
        const gt3r::CoreImage core = gt3r::CoreImage::decode(app.disc()->read_program());
        m.layout = race::read_single_race_layout(core.program, core.load_address);
        m.mode1 = race::read_mode1_layout(core.program, core.load_address);
        if (const auto colors = menu::find_button_colors(core.program)) m.button_colors = *colors;
        m.rev_faces = race::read_rev_faces(core.program, core.load_address);
        for (int k = 0; k < 2; ++k) m.speed_faces_by[k] = race::read_speed_faces(core.program, core.load_address, k == 1);
        m.speed_faces = m.speed_faces_by[m.mph ? 1 : 0];
        m.map = race::CourseMap::parse(gtcm);
        if (m.map.valid()) m.map_texture = m.make_texture(m.map.image);
    } catch (const std::exception& e) {
        LOG_WARN("Race display: %s", e.what());
        m.layout = {};
    }
    if (!ready()) LOG_WARN("Race display: the disc's program or files are not the ones its places were read from; a plain read-out is shown");
}

RaceHud::~RaceHud() {
    for (auto& [name, p] : impl_->pictures)
        if (p.texture) SDL_DestroyTexture(p.texture);
    for (auto& [key, t] : impl_->atlases)
        if (t) SDL_DestroyTexture(t);
    if (impl_->map_texture) SDL_DestroyTexture(impl_->map_texture);
    if (impl_->course_logo.texture) SDL_DestroyTexture(impl_->course_logo.texture);
    for (auto& [key, t] : impl_->movie_textures)
        if (t) SDL_DestroyTexture(t);
}

void RaceHud::set_course_logo(ByteSpan set) {
    Impl& m = *impl_;
    if (m.course_logo.texture) SDL_DestroyTexture(m.course_logo.texture), m.course_logo = {};
    try {
        if (!gt3r::Tex1::is_tex1(set)) return;
        const gt3r::Tex1 tex = gt3r::Tex1::parse(set);
        if (tex.textures.empty()) return;
        gt3r::GsMemory gs;
        tex.upload(set, gs);
        const gt3r::Tex1::Image img = tex.decode(gs, 0);
        m.course_logo.texture = m.make_texture(img);
        m.course_logo.w = float(img.width), m.course_logo.h = float(img.height);
    } catch (const std::exception& e) {
        LOG_WARN("Race display: the course's logo: %s", e.what());
    }
}

SDL_FRect RaceHud::canvas() { return to_ui(DisplayRect{0, 0, 640, 480}); }

void RaceHud::draw_movie(const race::StrobeMovie& movie, const race::StrobePlayer& player, std::vector<MovieExternal>* externals) {
    Impl& m = *impl_;
    // A colour of the movie (0xAABBGGRR, 0x80 a full channel) through a draw's colour change.
    auto colour = [](u32 c, const race::StrobePlayer::Draw& d) {
        SDL_FColor o;
        float ch[4];
        for (int j = 0; j < 4; ++j) ch[j] = std::clamp(float((c >> (8 * j)) & 0xFF) / 128.0f * d.mul[j] + d.add[j], 0.0f, 1.0f);
        o.r = ch[0], o.g = ch[1], o.b = ch[2], o.a = ch[3];
        return o;
    };
    auto texture = [&](size_t i) -> SDL_Texture* {
        SDL_Texture*& t = m.movie_textures[{&movie, i}];
        if (t) return t;
        try {
            const ByteSpan set = movie.texture_set(i);
            if (!gt3r::Tex1::is_tex1(set)) return nullptr;
            const gt3r::Tex1 tex = gt3r::Tex1::parse(set);
            if (tex.textures.empty()) return nullptr;
            gt3r::GsMemory gs;
            tex.upload(set, gs);
            gt3r::Tex1::Image img = tex.decode(gs, 0);
            // The fill matrices map the movie's size of the picture (a power of two: 128 x 64); the set's
            // texture is the picture's own size (99 x 34) at its top left.
            if (i < movie.textures.size()) {
                const u32 w = movie.textures[i].width, h = movie.textures[i].height;
                if (w > 0 && h > 0 && w <= 4096 && h <= 4096 && (w != img.width || h != img.height)) {
                    gt3r::Tex1::Image out;
                    out.width = w, out.height = h;
                    out.rgba.assign(size_t(w) * h, 0);
                    for (u32 y = 0; y < std::min(h, img.height); ++y)
                        std::copy_n(img.rgba.begin() + ptrdiff_t(size_t(y) * img.width), std::min(w, img.width), out.rgba.begin() + ptrdiff_t(size_t(y) * w));
                    img = std::move(out);
                }
            }
            t = m.make_texture(img);
        } catch (const std::exception& e) {
            LOG_WARN("Race movie: picture %zu: %s", i, e.what());
        }
        return t;
    };
    for (const race::StrobePlayer::Draw& d : player.draw_list()) {
        if (!d.external.empty()) {
            if (externals) externals->push_back({d.external, d.m[4], d.m[5], std::hypot(d.m[0], d.m[1]), std::clamp(d.mul[3], 0.0f, 1.0f)});
            continue;
        }
        if (d.group && !d.group->triangles.empty() && d.group->fill < movie.fills.size()) {
            const race::StrobeMovie::Fill& fill = movie.fills[d.group->fill];
            const std::array<float, 16>* mat = fill.value < movie.matrices.size() ? &movie.matrices[fill.value] : nullptr;
            SDL_Texture* tex = fill.kind == 4 ? texture(fill.index) : nullptr;
            const race::StrobeMovie::Gradient* grad = fill.kind == 1 && fill.index < movie.gradients.size() ? &movie.gradients[fill.index] : nullptr;
            std::vector<SDL_Vertex> v;
            v.reserve(d.group->triangles.size());
            for (const race::StrobeMovie::Point& p : d.group->triangles) {
                SDL_Vertex x{};
                x.position = to_ui(d.m[0] * p.x + d.m[2] * p.y + d.m[4], d.m[1] * p.x + d.m[3] * p.y + d.m[5]);
                const float u = mat ? p.x * (*mat)[0] + p.y * (*mat)[4] + (*mat)[12] : 0, w = mat ? p.x * (*mat)[1] + p.y * (*mat)[5] + (*mat)[13] : 0;
                x.tex_coord = {u, w};
                if (fill.kind == 0) x.color = colour(fill.value, d);
                else if (grad && !grad->stops.empty()) {
                    // A gradient's colour at the point's place along it.
                    const auto& st = grad->stops;
                    u32 c = st.front().second;
                    for (size_t k = 0; k + 1 < st.size(); ++k)
                        if (u >= st[k].first && u <= st[k + 1].first) {
                            const float t = st[k + 1].first > st[k].first ? (u - st[k].first) / (st[k + 1].first - st[k].first) : 0;
                            c = 0;
                            for (int j = 0; j < 4; ++j) {
                                const float a = float((st[k].second >> (8 * j)) & 0xFF), b = float((st[k + 1].second >> (8 * j)) & 0xFF);
                                c |= u32(std::lround(a + (b - a) * t)) << (8 * j);
                            }
                        }
                    if (u > st.back().first) c = st.back().second;
                    x.color = colour(c, d);
                } else {
                    x.color = colour(0x80808080u, d);
                }
                v.push_back(x);
            }
            SDL_RenderGeometry(m.r, tex, v.data(), int(v.size()), nullptr, 0);
        } else if (d.text && !d.words.empty()) {
            const race::StrobeMovie::TextField& t = *d.text;
            // The font string: "hm_16#1*80,80": a name, its palette (1 the plain glyphs, the movies' default:
            // their colour is the field's), its scale in percent across and down.
            const std::string spec = size_t(t.font) < movie.fonts.size() ? movie.fonts[size_t(t.font)] : std::string("hm_16");
            const size_t star = spec.find('*'), hash = spec.find('#');
            const int variant = hash != std::string::npos && hash < star ? std::atoi(spec.c_str() + hash + 1) : 1;
            Impl::FontSpec f = m.font(spec.substr(0, std::min(star, hash)).c_str(), 100, variant);
            if (star != std::string::npos) {
                const std::string sizes = spec.substr(star + 1);
                const size_t comma = sizes.find(',');
                f.sx = float(std::atof(sizes.c_str())) / 100.0f;
                f.sy = comma == std::string::npos ? f.sx : float(std::atof(sizes.c_str() + comma + 1)) / 100.0f;
            }
            if (!f.font) continue;
            // GT4's own text layout (menu/text_layout.h) at the glyphs' own pixel size: only the font
            // string's percentages and the instance's scale (the field's height does not scale the
            // glyphs). The field's box less its 2-unit gutter; Flash's alignment 0 left, 1 right,
            // 2 center, the first line at the box's top.
            const float sx = std::hypot(d.m[0], d.m[1]), sy = std::hypot(d.m[2], d.m[3]);
            menu::TextStyle st;
            st.font = f.font;
            if (const auto it = m.spacing.find(f.font->name); it != m.spacing.end()) st.spacing = it->second;
            st.scale_x = f.sx * sx, st.scale_y = f.sy * sy;
            const float bx = d.m[0] * (t.x0 + 2) + d.m[2] * (t.y0 + 2) + d.m[4], by = d.m[1] * (t.x0 + 2) + d.m[3] * (t.y0 + 2) + d.m[5];
            const float bw = std::max(0.0f, t.x1 - t.x0 - 4) * sx, bh = std::max(0.0f, t.y1 - t.y0 - 4) * sy;
            const int align = t.align == 2 ? 1 : t.align == 1 ? 2 : 0;
            const menu::TextLayout lay = menu::layout_menu_text_in(word(d.words), st, bx, by, bw, bh, align);
            SDL_Texture*& atlas = m.atlases[{f.font, f.variant}];
            if (!atlas) atlas = m.make_texture(f.font->atlas[size_t(std::clamp(f.variant, 0, 1))]);
            if (!atlas) continue;
            // A text field's colour is the font's (texels of 0x80) modulated by it: its RGB as it is
            // (0xFF white; the pause title's 0x79 a mid gray, as the original shows it), its alpha
            // with 0x80 opaque.
            SDL_FColor c = colour(t.color, d);
            float* rgb[3] = {&c.r, &c.g, &c.b};
            for (int j = 0; j < 3; ++j) *rgb[j] = std::clamp(float((t.color >> (8 * j)) & 0xFF) / 255.0f * d.mul[j] + d.add[j], 0.0f, 1.0f);
            SDL_SetTextureColorMod(atlas, u8(c.r * 255), u8(c.g * 255), u8(c.b * 255));
            SDL_SetTextureAlphaMod(atlas, u8(std::clamp(c.a, 0.0f, 1.0f) * 255));
            for (const menu::PlacedGlyph& g : lay.glyphs) {
                if (g.font != f.font || !g.sw || !g.sh) continue; // (other fonts' glyphs: the pad's buttons; none in these movies)
                const SDL_FRect src{float(g.sx), float(g.sy), float(g.sw), float(g.sh)};
                const SDL_FRect dst = to_ui({g.x, g.y, g.w, g.h});
                SDL_RenderTexture(m.r, atlas, &src, &dst);
            }
        }
    }
}

bool RaceHud::ready() const { return impl_->layout.valid() && impl_->pack && !impl_->font_by_name.empty(); }
bool RaceHud::mph() const { return impl_->mph; }
void RaceHud::set_mph(bool on) {
    if (impl_->mph == on) return;
    impl_->mph = on;
    impl_->speed_faces = impl_->speed_faces_by[on ? 1 : 0]; // the speedometer's faces are by the unit
}

std::string RaceHud::word(const std::string& key) const {
    if (impl_->words) {
        if (const std::string* s = impl_->words->find("RaceDisplay", key)) return *s;
        for (const std::string& page : impl_->words->pages())
            if (const std::string* s = impl_->words->find(page, key)) return *s;
    }
    return key;
}

// Position, lap count and the three times.
void RaceHud::Impl::draw_top(const HudState& s) {
    const float o = s.opacity;
    if (const DisplayPlace* p = cur->find(element::kPositionWord); p && s.show_position) draw_at("JP/position", *p, kNeutral, o);
    if (const DisplayPlace* p = cur->find(element::kPosition); p && s.show_position) {
        const DisplayRect at = draw_at("position_base", *p, kNeutral, o);
        const int digit = std::clamp(s.place, 0, 9);
        draw_text(font("FC_26"), at.x + at.w / 2 - 1 + (digit == 1 ? 4.0f : 0.0f), at.y + at.h - 19, kCentered | kMiddleOfLine, kTextGray, o,
                  std::string(1, char('0' + digit)));
    }
    if (const DisplayPlace* p = cur->find(element::kLapWord); p && s.show_lap) draw_at("JP/lap", *p, kNeutral, o);
    if (const DisplayPlace* p = cur->find(element::kLapCount); p && s.show_lap) {
        char buf[32];
        if (s.laps > 0) std::snprintf(buf, sizeof buf, "%d/%d", std::clamp(s.lap, 0, 999), s.laps);
        else std::snprintf(buf, sizeof buf, "%d", std::clamp(s.lap, 0, 999));
        // By its width from the anchor, squeezed into the element's width; the baseline at y.
        const FontSpec f = font("FC_26", 80);
        float width = text_width(f, buf), squeeze = 1;
        if (p->w > 0 && width > float(p->w)) squeeze = float(p->w) / width, width = float(p->w);
        DisplayPlace q = *p;
        q.h = 0;
        const DisplayRect at = race::place_rect(q, width, 0, true);
        draw_text(f, at.x, float(p->y), 0, kTextGray, o, buf, squeeze);
    }
    const FontSpec times = font("hn_20R");
    if (const DisplayPlace* p = cur->find(element::kLapTime); p && s.show_time) {
        const float left = draw_text(times, race::place_x(*p, true), float(p->y), kCentered, kTextGray, o, race::format_lap_time(s.lap_ms));
        // The stopwatch ends 8 units left of the text and 7 below its baseline.
        const Picture& watch = picture("GT4_meter_texture_laptime");
        draw_picture(watch, {left - 8 - watch.w, float(p->y) + 7 - watch.h, watch.w, watch.h}, kNeutral, o);
    }
    if (const DisplayPlace* p = cur->find(element::kBestWord); p && s.show_time) draw_at(s.bspec ? "JP/total_time" : "JP/best_time", *p, kNeutral, o);
    if (const DisplayPlace* p = cur->find(element::kTotalTime); p && s.show_time)
        draw_text(times, race::place_x(*p, true), float(p->y), kCentered, kTextGray, o, race::format_total_time(s.total_ms));
    if (const DisplayPlace* p = cur->find(element::kBestTime); p && s.show_time)
        draw_text(times, race::place_x(*p, true), float(p->y), kCentered, kTextGray, o, race::format_lap_time(s.best_ms));
    if (const DisplayPlace* p = cur->find(element::kLastWord); p && s.show_time) draw_at("JP/last_time", *p, kNeutral, o);
    if (const DisplayPlace* p = cur->find(element::kLastTime); p && s.show_time)
        draw_text(times, race::place_x(*p, true), float(p->y), kCentered, kTextGray, o, race::format_lap_time(s.last_ms));
    if (const DisplayPlace* p = cur->find(element::kEventLine))
        draw_text(times, race::place_x(*p, true), float(p->y), kCentered, kTextGray, o * s.event_opacity, s.event);
    if (const DisplayPlace* p = cur->find(element::kMessage))
        draw_text(font("FC_48"), race::place_x(*p, true), float(p->y), kCentered, kTextGray, o * s.message_opacity, s.message);
    // The status line (member 0x228C): over the driver's panel at (320, 348), over the chase views'
    // at (320, 430); `hn_20R`.
    if (!s.status.empty() && s.status_opacity > 0)
        draw_text(font("hn_20R"), 320, s.driver_view ? 348.0f : 430.0f, kCentered, kTextGray, o * s.status_opacity, s.status);
}

// The course map with the cars on it; it neither turns nor scrolls.
void RaceHud::Impl::draw_map(const HudState& s) {
    const DisplayPlace* p = cur->find(element::kMap);
    if (!p || !map_texture || !s.show_map) return;
    const DisplayRect at = race::place_rect(*p, float(map.image.width), float(map.image.height), true);
    auto marker = [&](const Picture& pic, const HudState::Dot& d, u32 tint, float opacity) {
        float mx, my;
        map.map(d.x, d.z, mx, my);
        draw_picture(pic, {at.x + mx - pic.w / 2, at.y + my - pic.h / 2, pic.w, pic.h}, tint, opacity);
    };
    const Picture& target = picture("point_target");
    const Picture& rival = picture("point_rival");
    if (!s.cars.empty()) marker(target, s.cars[0], 0x80000000u, s.opacity); // the player's, black, under the map
    SDL_SetTextureColorMod(map_texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(map_texture, u8(255 * std::clamp(s.opacity, 0.0f, 1.0f)));
    const SDL_FRect dst = to_ui(at);
    SDL_RenderTexture(r, map_texture, nullptr, &dst);
    for (size_t i = 1; i < s.cars.size(); ++i) marker(rival, s.cars[i], kNeutral, s.opacity);
    if (!s.cars.empty()) {
        // The player's marker pulses twice a second: up in a tenth of a second, down in three.
        const float t = float(std::fmod(s.time, 0.5));
        const float b = t < 0.25f ? std::min(1.0f, t / 0.1f) : std::max(0.0f, 1.0f - (t - 0.25f) / 0.3f);
        marker(target, s.cars[0], kNeutral, s.opacity * (0.3f + 0.7f * b));
    }
}

// The gear in its pale box: black, in the meters' font ("R" reverse; its "K" draws N).
void RaceHud::Impl::draw_gear(const DisplayRect& box, const HudState& s) {
    draw_picture(picture("gear_base"), box, 0x80FFFFFFu, s.opacity);
    const char c = s.gear < 0 ? 'R' : s.gear == 0 ? 'K' : char('0' + std::clamp(s.gear, 1, 9));
    draw_text(font("GT4meter", 100, 1), box.x + box.w / 2, box.y + box.h - 12, kCentered | kMiddleOfLine, 0x80000000u, s.opacity, std::string(1, c));
}

// The chase view's meters at the bottom right: the speed on its plate, the gear, the rev bar.
void RaceHud::Impl::draw_chase_panel(const HudState& s) {
    const int R = 640, B = 464;
    const float o = s.opacity;
    draw_at(mph ? "remote_speed_mph" : "remote_speed_kmph", {0, R - 26, B - 44, 82, 26, 13, 0}, kNeutral, o);
    // The digits end where the widest number would.
    const FontSpec meter = font("GT4meter", 100, 1);
    const DisplayPlace speed{0, R - 59, B - 26, 0, 0, 13, 1};
    draw_text(meter, race::place_x(speed, true), float(speed.y), kEndsAtPen, kMeterBlue, o, speed_text(s));
    draw_gear(race::place_rect({0, R - 114, B - 44, 24, 26, 13, 0}, 24, 26, true), s);
    // The rev bar: the unlit bar in black at half strength, the lit part over it, and a red line
    // over the part past the red line.
    const DisplayPlace bar{0, R - 26, B, 160, 10, 13, 1};
    const Picture& pic = picture("rev_bar");
    const DisplayRect at = race::place_rect(bar, pic.w, pic.h, true);
    draw_picture(pic, at, 0x80000000u, o * 0.5f);
    draw_picture(pic, at, 0x80FFFFFFu, o, race::rev_bar_fraction(s.rpm, s.red_line));
    const float x0 = at.x + 0.70625f * at.w, x1 = at.x + at.w - 1, y0 = at.y - 3, y1 = at.y - 1, a = o * float(0x70) / 128.0f;
    triangles({vertex(x0, y0, 0x99, 0, 0, a), vertex(x1, y0, 0x99, 0, 0, a), vertex(x1, y1, 0x99, 0, 0, a), vertex(x0, y0, 0x99, 0, 0, a),
               vertex(x1, y1, 0x99, 0, 0, a), vertex(x0, y1, 0x99, 0, 0, a)});
    draw_small_parts(s, false);
}

// The panels' smaller parts at the places the panels give them (docs/formats/race_display.md
// "Driver's views", "Chase views"): the warning signs, the shift lamp, the sideways G meter, and in
// the driver's views the trip meter, in the chase views the pedals, the tire wear and the fuel.
// ❓ How each part's value moves its picture is judged (their drawing routines are not read): the
// G meter's point by the sideways g against two either way, a sign unlit at a third (the aids'
// work is not given by the simulation), the shift lamp lit past 97 % of the red line, the bars
// filled by their share.
void RaceHud::Impl::draw_small_parts(const HudState& s, bool driver) {
    const float o = s.opacity;
    const int C = 320, B = driver ? 452 : 464, L = 0;
    // The signs: the stability aid's in its own color, the brake's and the oil's red.
    if (driver) {
        draw_at("sign_asm", {0, C - 199, B, 24, 24, 1, 2}, 0x80E3B896u, o / 3);
        draw_at("sign_brake", {0, C - 166, B, 24, 24, 1, 2}, 0x800000C8u, o / 3);
        draw_at("sign_oil", {0, C + 166, B, 24, 24, 0, 2}, 0x800000C8u, o / 3);
    } else {
        draw_at("sign_asm", {0, L + 119, B, 24, 24, 6, 1}, 0x80E3B896u, o / 3);
        draw_at("sign_brake", {0, L + 147, B, 24, 24, 6, 1}, 0x800000C8u, o / 3);
        draw_at("sign_oil", {0, L + 175, B, 24, 24, 6, 1}, 0x800000C8u, o / 3);
    }
    // The shift lamp.
    const bool shift = s.red_line > 0 && s.rpm >= 0.97f * s.red_line;
    if (driver) draw_at("timing_lamp", {0, C + 103, B - 62, 8, 8, 2, 2}, shift ? kNeutral : 0x80404040u, o);
    else draw_at("timing_lamp", {0, 640 - 142, B - 28, 8, 8, 13, 0}, shift ? kNeutral : 0x80404040u, o);
    // The sideways G meter: its base, and its point moved along it.
    {
        const DisplayRect base = draw_at("g_meter_base", {0, C, B, 214, 24, 2, u8(driver ? 2 : 1)}, kNeutral, o);
        const Picture& pt = picture("g_meter_point");
        if (pt.texture && base.w > 0) {
            const float k = std::clamp(s.lateral_g / 2.0f, -1.0f, 1.0f);
            const float cx = base.x + base.w / 2 + k * (base.w / 2 - pt.w), cy = base.y + base.h / 2;
            draw_picture(pt, {cx - pt.w / 2, cy - pt.h / 2, pt.w, pt.h}, kNeutral, o);
        }
    }
    if (driver) {
        // The trip meter: whole miles or kilometres of this race, six digits.
        char buf[16];
        std::snprintf(buf, sizeof buf, "%06d", std::clamp(int(s.trip_m / (mph ? 1609.344f : 1000.0f)), 0, 999999));
        draw_text(font("GT4meter", 60, 1), float(C - 103), float(B - 45), kCentered, kMeterBlue, o, buf);
        return;
    }
    // The pedals: the throttle's bar and the brake's, from the bottom.
    {
        const Picture& p = picture("pedals");
        const DisplayRect at = race::place_rect({0, L + 26, B, 22, 66, 4, 1}, p.w > 0 ? p.w : 22, p.h > 0 ? p.h : 66, true);
        draw_picture(p, at, 0x80202020u, o * 0.6f);
        const float bw = at.w / 2 - 1;
        auto bar = [&](float x, float v, u8 r, u8 g, u8 b) {
            const float h = at.h * std::clamp(v, 0.0f, 1.0f);
            if (h <= 0) return;
            triangles({vertex(x, at.y + at.h - h, r, g, b, o), vertex(x + bw, at.y + at.h - h, r, g, b, o), vertex(x + bw, at.y + at.h, r, g, b, o),
                       vertex(x, at.y + at.h - h, r, g, b, o), vertex(x + bw, at.y + at.h, r, g, b, o), vertex(x, at.y + at.h, r, g, b, o)});
        };
        bar(at.x, s.throttle, 0x40, 0xC0, 0x40);
        bar(at.x + bw + 2, s.brake, 0xC0, 0x30, 0x30);
    }
    draw_at("tire_wear", {0, L + 58, B, 40, 68, 4, 1}, kNeutral, o * (0.5f + 0.5f * std::clamp(s.tire_wear, 0.0f, 1.0f)));
    // The fuel gauge: the empty bar, the full one over it by what is left.
    draw_at("fuel_b0", {0, L + 107, B - 30, 86, 11, 4, 1}, kNeutral, o);
    draw_at("fuel_b1", {0, L + 107, B - 30, 86, 11, 4, 1}, kNeutral, o, std::clamp(s.fuel, 0.0f, 1.0f));
}

// A round meter: the red zone behind the face (from `red_from` in the value's units; past the
// range: none), the face, the needle with the trail of its move since the last frame.
void RaceHud::Impl::draw_dial(const DisplayRect& at, const std::string& face, float ox, float value, float min, float max, float a0, float a1,
                              float red_from, float& last_angle, float opacity) {
    const float cx = at.x + at.w / 2 + ox, cy = at.y + at.h / 2 + 15, radius = 60;
    auto dir = [](float degrees) { return SDL_FPoint{std::cos(degrees * kPi / 180), std::sin(degrees * kPi / 180)}; };
    if (red_from < max) {
        const float from = race::needle_angle(std::max(red_from, min), min, max, a0, a1), to = a1 + 90;
        const float r0 = 0.6833f * radius, r1 = r0 + 0.3109f * radius;
        const int steps = std::max(2, int((to - from) / 10.0f + 0.5f));
        std::vector<SDL_Vertex> v;
        for (int k = 0; k < steps; ++k) {
            const SDL_FPoint d0 = dir(from + (to - from) * float(k) / float(steps)), d1 = dir(from + (to - from) * float(k + 1) / float(steps));
            const float a = opacity * 2.0f / 3.0f;
            const SDL_Vertex q[4] = {vertex(cx + d0.x * r0, cy + d0.y * r0, 0x99, 0, 0, a), vertex(cx + d0.x * r1, cy + d0.y * r1, 0x99, 0, 0, a),
                                     vertex(cx + d1.x * r1, cy + d1.y * r1, 0x99, 0, 0, a), vertex(cx + d1.x * r0, cy + d1.y * r0, 0x99, 0, 0, a)};
            v.insert(v.end(), {q[0], q[1], q[2], q[0], q[2], q[3]});
        }
        triangles(v);
    }
    draw_picture(picture(face), at, 0x80999999u, opacity); // (the face is drawn a fifth brighter than its picture)
    // The needle: a strip from behind the pivot to near the rim, bright along its middle, drawn
    // several times from last frame's angle to this one's.
    const float angle = race::needle_angle(value, min, max, a0, a1);
    if (!needle_set) last_angle = angle;
    const int copies = 5;
    std::vector<SDL_Vertex> v;
    for (int k = 0; k < copies; ++k) {
        const float a = last_angle + (angle - last_angle) * float(k + 1) / float(copies);
        const SDL_FPoint d = dir(a), n{-d.y, d.x};
        const float n0 = -0.3097f * radius, n1 = 0.9613f * radius, w0 = 0.02256f * radius, w1 = 0.01288f * radius, alpha = opacity * 0.3f; // (five copies: together about five sixths)
        const SDL_Vertex base_l = vertex(cx + d.x * n0 + n.x * w0, cy + d.y * n0 + n.y * w0, 0x40, 0, 0, alpha), base_m = vertex(cx + d.x * n0, cy + d.y * n0, 0xC8, 0, 0, alpha),
                         base_r = vertex(cx + d.x * n0 - n.x * w0, cy + d.y * n0 - n.y * w0, 0x40, 0, 0, alpha);
        const SDL_Vertex tip_l = vertex(cx + d.x * n1 + n.x * w1, cy + d.y * n1 + n.y * w1, 0x40, 0, 0, alpha), tip_m = vertex(cx + d.x * n1, cy + d.y * n1, 0xC8, 0, 0, alpha),
                         tip_r = vertex(cx + d.x * n1 - n.x * w1, cy + d.y * n1 - n.y * w1, 0x40, 0, 0, alpha);
        v.insert(v.end(), {base_l, base_m, tip_m, base_l, tip_m, tip_l, base_m, base_r, tip_r, base_m, tip_r, tip_m});
    }
    triangles(v);
    last_angle = angle;
}

// The driver's views' panel at the bottom middle: the speedometer and the rev counter either
// side of the gear, the speed in digits under it.
void RaceHud::Impl::draw_driver_panel(const HudState& s) {
    const int C = 320, B = 452;
    const float o = s.opacity;
    draw_at("panel_base", {0, C, B, 336, 102, 2, 1}, kNeutral, o);
    const float unit = mph ? 2.2369363f : 3.6f;
    // The speedometer's face: by the car's top speed, in steps of 80 km/h or 50 mph, two to four.
    const int steps = std::clamp(int(s.top_speed * 1.1f / 22.2222f) + 1, 2, 4);
    if (const race::DialFace* f = race::pick_face(speed_faces, float(steps) * (mph ? 50.0f : 80.0f)))
        draw_dial(race::place_rect({0, C - 168, B - 100, 130, 100, 0, 0}, 130, 100, true), f->picture, 1, std::fabs(s.speed) * unit, f->min, f->max, 56, 276, 1e30f,
                  needle_from[0], o);
    if (const race::DialFace* f = race::pick_face(rev_faces, race::rev_dial_maximum(s.rev_limit)))
        draw_dial(race::place_rect({0, C + 168, B - 100, 130, 100, 1, 0}, 130, 100, true), f->picture, -1, s.rpm, f->min, f->max, 84, 304, s.red_line, needle_from[1], o);
    needle_set = true;
    draw_gear(race::place_rect({0, C, B - 64, 24, 26, 2, 1}, 24, 26, true), s);
    const FontSpec meter = font("GT4meter", 100, 1);
    const float width = text_width(meter, "000");
    draw_text(meter, float(C - 2) + width / 2, float(B - 41), kEndsAtPen, kMeterBlue, o, speed_text(s));
    draw_at(mph ? "mph" : "kmph", {0, C, B - 24, 28, 9, 2, 1}, kNeutral, o);
    draw_small_parts(s, true);
}

void RaceHud::draw(Ui& ui, const HudState& s) {
    if (!ready() || s.opacity <= 0) return;
    impl_->cur = s.bspec && !s.replay && impl_->mode1.valid() ? &impl_->mode1 : &impl_->layout;
    (void)ui;
    if (s.replay && s.card_opacity > 0) {
        // A replay's opening card (RaceDisplay::vf45, 0x39C838): the car's name in hb_24 at (320, 220),
        // the line "power  tires  race" in hm_16 at (320, 260), color 0x80C8C8C8, over the picture
        // darkened (❓ the darkening's strength is judged from the original's replay).
        const float a = std::clamp(s.card_opacity, 0.0f, 1.0f);
        const SDL_FRect c = to_ui(DisplayRect{-200, 0, 1040, 480});
        SDL_SetRenderDrawBlendMode(impl_->r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(impl_->r, 0, 0, 0, u8(190 * a));
        SDL_RenderFillRect(impl_->r, &c);
        impl_->draw_text(impl_->font("hb_24", 100, 1), 320, 220, kCentered, kTextGray, s.opacity * a, s.card_title);
        impl_->draw_text(impl_->font("hm_16", 100, 1), 320, 260, kCentered, kTextGray, s.opacity * a, s.card_line);
    }
    if (s.replay && s.logo_opacity > 0 && impl_->course_logo.texture) {
        // The course's logo under the opening card, its right bottom at (604, 441) (❓ the place and
        // size judged from the original's replay: the logo at its own size there).
        const Impl::Picture& logo = impl_->course_logo;
        impl_->draw_picture(logo, race::place_rect({0, 604, 441, 0, 0, 1, 1}, logo.w, logo.h, true), kNeutral, s.opacity * std::clamp(s.logo_opacity, 0.0f, 1.0f));
    }
    if (s.replay && s.caption_opacity > 0 && !s.caption_title.empty()) {
        // The music's caption at the bottom left: the piece's title, its artist smaller under
        // it (❓ the fonts, places and colour judged from the original's replay).
        const float a = s.opacity * std::clamp(s.caption_opacity, 0.0f, 1.0f);
        // (The original's note before the title is not in these fonts: not drawn.)
        impl_->draw_text(impl_->font("hm_16", 90, 1), 42, 432, 0, kTextGray, a, s.caption_title);
        if (!s.caption_artist.empty()) impl_->draw_text(impl_->font("hm_16", 70, 1), 42, 449, 0, kTextGray, a, s.caption_artist);
    }
    if (s.replay && !s.replay_display) {
        // A replay with its display off (the Replay options' Screen Display: Off, the original's
        // default): the small Gran Turismo 4 logo, its right bottom at (604, 441) (member 0x1170),
        // ❓ at about 60 % (judged from the original's replay; its draw colour is not read).
        const Impl::Picture& logo = impl_->picture("JP/gt_logo_mini");
        if (logo.w > 0 && s.card_opacity <= 0 && s.logo_opacity <= 0 && s.caption_opacity <= 0) impl_->draw_picture(logo, race::place_rect({0, 604, 441, 0, 0, 1, 1}, logo.w, logo.h, true), kNeutral, s.opacity * 0.6f);
        return;
    }
    if (s.replay) {
        // The replay's sign at its place (the place's rectangle is the sign's room: its own size, top left).
        if (const DisplayPlace* p = impl_->layout.find(element::kReplaySign)) {
            const Impl::Picture& sign = impl_->picture("JP/replay");
            DisplayPlace at = *p;
            at.w = at.h = 0;
            impl_->draw_picture(sign, race::place_rect(at, sign.w, sign.h, true), kNeutral, s.opacity);
        }
    } else {
        impl_->draw_map(s);
        impl_->draw_top(s);
    }
    if (s.bspec && !s.replay) {
        char key[16];
        std::snprintf(key, sizeof key, "key_pace%d", std::clamp(s.pace, 1, 5));
        impl_->draw_bspec(s, word(s.overtake ? "key_overtake" : key), word("Tool Tip1"));
        return;
    }
    if (!s.show_meter) return;
    if (s.driver_view) impl_->draw_driver_panel(s);
    else impl_->draw_chase_panel(s);
}

void RaceHud::Impl::draw_menu_text(const char* font_name, int variant, float x, float y, gt3r::text::HAlign align, u32 color, float opacity, const std::string& text,
                                   float max_width) {
    const FontSpec f = font(font_name, 100, variant);
    if (!f.font || text.empty() || opacity <= 0) return;
    menu::TextStyle st;
    st.font = f.font;
    if (const auto it = spacing.find(f.font->name); it != spacing.end()) st.spacing = it->second;
    if (const auto g = font_by_name.find("Gaiji"); g != font_by_name.end()) st.gaiji = g->second;
    st.button_colors = button_colors;
    st.align = align, st.valign = gt3r::text::VAlign::Baseline;
    if (max_width > 0) {
        // Squeezed to fit (the font's size kept up to it).
        const float w = menu::layout_menu_text(text, st, 0, 0).width;
        if (w > max_width) st.scale_x = st.scale_y = max_width / w;
    }
    const Rgba8 c = text_color(color, opacity);
    st.color = {c.r, c.g, c.b, c.a};
    const menu::TextLayout lay = menu::layout_menu_text(text, st, x, y);
    for (const menu::PlacedGlyph& g : lay.glyphs) {
        if (!g.font || !g.sw || !g.sh) continue;
        SDL_Texture*& atlas = atlases[{g.font, g.font == f.font ? f.variant : 1}];
        if (!atlas) atlas = make_texture(g.font->atlas[size_t(g.font == f.font ? std::clamp(f.variant, 0, 1) : 1)]);
        if (!atlas) continue;
        SDL_SetTextureColorMod(atlas, g.color.r, g.color.g, g.color.b);
        SDL_SetTextureAlphaMod(atlas, u8(float(g.color.a) * std::clamp(opacity, 0.0f, 1.0f)));
        const SDL_FRect src{float(g.sx), float(g.sy), float(g.sw), float(g.sh)};
        const SDL_FRect dst = to_ui({g.x, g.y, g.w, g.h});
        SDL_RenderTexture(r, atlas, &src, &dst);
    }
}

// The B-spec display (display mode 1, panel layout 2: docs/formats/race_display.md "B-spec"): the
// place, laps, map, the lap's time and the race's time at mode 1's places; the commands' bar at the
// top (the pace 1 .. 5 with the chosen one lit, the overtake and pit keys, the pace's word under
// them); the speed's small plate; the help strip. ❓ The bar's, the plate's and the strip's shapes
// and places are judged from the original's B-spec race (their drawing is not read).
void RaceHud::Impl::draw_bspec(const HudState& s, const std::string& pace_word, const std::string& help) {
    const float o = s.opacity;
    // The commands' bar.
    fill({190, 18, 260, 58}, 0x10, 0x10, 0x14, o * 0.85f);
    for (int k = 0; k < 7; ++k) {
        const DisplayRect key{float(198 + k * 30 + (k >= 5 ? 6 : 0)), 24, 26, 26};
        fill(key, 0xC8, 0xC8, 0xC8, o * (k == 5 ? 0.0f : 0.9f));
        const float cx = key.x + key.w / 2, base = key.y + 21;
        if (k < 5) {
            const bool lit = k + 1 == s.pace && !s.overtake;
            draw_text(font("hb_24", 100, 1), cx, base, kCentered, lit ? 0x800050F0u : 0x80202020u, o, std::string(1, char('1' + k)));
        } else if (k == 5) {
            // The overtake key: the triangle button, lit while the command is on.
            fill(key, s.overtake ? 0xF0 : 0x58, s.overtake ? 0x80 : 0x58, s.overtake ? 0x20 : 0x60, o * 0.9f);
            draw_menu_text("hm_16", 1, cx, base - 3, gt3r::text::HAlign::Center, kNeutral, o, "%Bc");
        } else {
            draw_text(font("hb_24", 100, 1), cx, base, kCentered, 0x80202020u, o, "P");
        }
    }
    draw_text(font("hn_20R", 80, 1), 320, 71, kCentered, kTextGray, o, pace_word);
    // The speed's small plate over the help strip.
    const int R = 640, B = 431;
    draw_at(mph ? "remote_speed_mph" : "remote_speed_kmph", {0, R - 26, B, 82, 28, 13, 1}, kNeutral, o);
    draw_text(font("GT4meter", 100, 1), race::place_x({0, R - 59, B - 8, 0, 0, 13, 1}, true), float(B - 8), kEndsAtPen, kMeterBlue, o, speed_text(s));
    // The help strip (member 0x2DA0: 0, 480, 640 x 42, its bottom at the bottom).
    if (s.help) {
        fill({kWideLeft, 438, 640 - 2 * kWideLeft, 42}, 0, 0, 0, o * 0.75f);
        draw_menu_text("hm_16", 1, 18, 463, gt3r::text::HAlign::Left, kTextGray, o, help, 604);
    }
}

} // namespace gt4r
