#include "gt4r/app/race_hud.h"

#include "base/log.h"
#include "disc/core_image.h"
#include "formats/gs_memory.h"
#include "formats/text/text_layout.h"
#include "gt4r/menu/font.h"
#include "gt4r/menu/image_pack.h"
#include "gt4r/menu/text_table.h"

#include <algorithm>
#include <cmath>
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
    gt3r::text::FontFile menu_fonts; // the movies' (font/menu.fnt)
    std::map<std::pair<const race::StrobeMovie*, size_t>, SDL_Texture*> movie_textures;
    Bytes pack_file;
    std::optional<menu::ImagePack> pack;
    std::map<std::string, Picture> pictures;
    race::DisplayLayout layout;
    std::vector<race::DialFace> rev_faces, speed_faces;
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
        if (const std::optional<Bytes> file = app.vfs()->read("font/system.fnt")) m.fonts = menu::read_fonts(*file);
        for (const gt3r::text::LatinFont& f : m.fonts.fonts) m.font_by_name.emplace(f.name, &f);
        if (const std::optional<Bytes> file = app.vfs()->read("font/menu.fnt")) m.menu_fonts = menu::read_fonts(*file);
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
        m.rev_faces = race::read_rev_faces(core.program, core.load_address);
        m.speed_faces = race::read_speed_faces(core.program, core.load_address, m.mph);
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
    for (auto& [key, t] : impl_->movie_textures)
        if (t) SDL_DestroyTexture(t);
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
            if (externals) externals->push_back({d.external, d.m[4], d.m[5], std::clamp(d.mul[3], 0.0f, 1.0f)});
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
            // The field's em (its height) against the font's own size (the program
            // scales the glyphs by the height alone: 0x479CA0).
            const float em = f.font->size > 0 ? float(f.font->size) : 16.0f;
            const float size = t.height / em, sx = std::hypot(d.m[0], d.m[1]), sy = std::hypot(d.m[2], d.m[3]);
            f.sx *= size * sx, f.sy *= size * sy;
            const float x = t.align == 2 ? (t.x0 + t.x1) / 2 : t.align == 1 ? t.x1 : t.x0;
            const float px = d.m[0] * x + d.m[2] * t.y0 + d.m[4], py = d.m[1] * x + d.m[3] * t.y0 + d.m[5];
            const SDL_FColor c = colour(t.color, d);
            const u32 rgba = u32(c.r * 255) | u32(c.g * 255) << 8 | u32(c.b * 255) << 16 | u32(c.a * 128) << 24;
            m.draw_text(f, px, py + 2 * sy + float(f.font->ascent) * f.sy, t.align == 2 ? kCentered : t.align == 1 ? kEndsAtPen : 0, rgba, 1.0f, word(d.words));
        }
    }
}

bool RaceHud::ready() const { return impl_->layout.valid() && impl_->pack && !impl_->font_by_name.empty(); }
bool RaceHud::mph() const { return impl_->mph; }

std::string RaceHud::word(const std::string& key) const {
    if (impl_->words)
        if (const std::string* s = impl_->words->find("RaceDisplay", key)) return *s;
    return key;
}

// Position, lap count and the three times.
void RaceHud::Impl::draw_top(const HudState& s) {
    const float o = s.opacity;
    if (const DisplayPlace* p = layout.find(element::kPositionWord)) draw_at("JP/position", *p, kNeutral, o);
    if (const DisplayPlace* p = layout.find(element::kPosition)) {
        const DisplayRect at = draw_at("position_base", *p, kNeutral, o);
        const int digit = std::clamp(s.place, 0, 9);
        draw_text(font("FC_26"), at.x + at.w / 2 - 1 + (digit == 1 ? 4.0f : 0.0f), at.y + at.h - 19, kCentered | kMiddleOfLine, kTextGray, o,
                  std::string(1, char('0' + digit)));
    }
    if (const DisplayPlace* p = layout.find(element::kLapWord)) draw_at("JP/lap", *p, kNeutral, o);
    if (const DisplayPlace* p = layout.find(element::kLapCount)) {
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
    if (const DisplayPlace* p = layout.find(element::kLapTime)) {
        const float left = draw_text(times, race::place_x(*p, true), float(p->y), kCentered, kTextGray, o, race::format_lap_time(s.lap_ms));
        // The stopwatch ends 8 units left of the text and 7 below its baseline.
        const Picture& watch = picture("GT4_meter_texture_laptime");
        draw_picture(watch, {left - 8 - watch.w, float(p->y) + 7 - watch.h, watch.w, watch.h}, kNeutral, o);
    }
    if (const DisplayPlace* p = layout.find(element::kBestWord)) draw_at("JP/best_time", *p, kNeutral, o);
    if (const DisplayPlace* p = layout.find(element::kBestTime))
        draw_text(times, race::place_x(*p, true), float(p->y), kCentered, kTextGray, o, race::format_lap_time(s.best_ms));
    if (const DisplayPlace* p = layout.find(element::kLastWord)) draw_at("JP/last_time", *p, kNeutral, o);
    if (const DisplayPlace* p = layout.find(element::kLastTime))
        draw_text(times, race::place_x(*p, true), float(p->y), kCentered, kTextGray, o, race::format_lap_time(s.last_ms));
    if (const DisplayPlace* p = layout.find(element::kEventLine))
        draw_text(times, race::place_x(*p, true), float(p->y), kCentered, kTextGray, o * s.event_opacity, s.event);
    if (const DisplayPlace* p = layout.find(element::kMessage))
        draw_text(font("FC_48"), race::place_x(*p, true), float(p->y), kCentered, kTextGray, o * s.message_opacity, s.message);
}

// The course map with the cars on it; it neither turns nor scrolls.
void RaceHud::Impl::draw_map(const HudState& s) {
    const DisplayPlace* p = layout.find(element::kMap);
    if (!p || !map_texture) return;
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
}

void RaceHud::draw(Ui& ui, const HudState& s) {
    if (!ready() || s.opacity <= 0) return;
    (void)ui;
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
    if (s.driver_view) impl_->draw_driver_panel(s);
    else impl_->draw_chase_panel(s);
}

} // namespace gt4r
