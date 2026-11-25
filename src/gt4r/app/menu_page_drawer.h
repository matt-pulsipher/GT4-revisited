#pragma once

// Draws a menu page: its pictures, frames, colours and text as the layout and the scripts set
// them (docs/formats/menu.md, "Drawing"). Faces with content of their own (movies, 3D) are
// drawn by `draw_live`, called in their place in the page's order.

#include "gt4r/app/app.h"
#include "gt4r/app/menu_image_key.h"

#include "base/log.h"
#include "formats/text/charset.h"
#include "formats/text/text_layout.h"
#include "gt4r/menu/faces_flash.h"
#include "gt4r/menu/faces_frame.h"
#include "gt4r/menu/faces_graph.h"
#include "gt4r/menu/faces_tooltip.h"
#include "gt4r/menu/font.h"
#include "gt4r/menu/game.h"
#include "gt4r/menu/project.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/menu/text_layout.h"
#include "gt4r/menu/transform.h"

#include "disc/core_image.h"
#include "gt4r/disc/disc.h"
#include "formats/gs_memory.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace gt4r {

using menu::Rect;
using menu::Rgba;
using menu::Widget;

using menu::kActive;
using menu::kFocus;
using menu::kPrelight;
using menu::kInsensitive;
using menu::LiveWidget;
using menu::Affine;

inline constexpr int kFocusBits = kFocus | kPrelight; // a focused widget has both

// The shape buttons' colours in texts, from the disc's program (read once).
inline std::array<menu::TextColor, 4> program_button_colors(App& app) {
    static std::optional<std::array<menu::TextColor, 4>> found;
    static bool tried = false;
    if (!tried && app.disc()) {
        tried = true;
        try {
            const Bytes core = app.disc()->read_program();
            found = menu::find_button_colors(gt3r::CoreImage::decode(core).program);
        } catch (const std::exception& e) {
            LOG_WARN("The program's button colours: %s", e.what());
        }
    }
    return found.value_or(std::array<menu::TextColor, 4>{});
}

// Draws a page into a rectangle of the screen.
class PageDrawer {
public:
    explicit PageDrawer(App& app) : r_(app.renderer()), vfs_(app.vfs()) {
        for (const char* path : {"font/system.fnt", "font/menu.fnt", "font/race.fnt"}) { // (Prologue's lesson pages: FC_16 ..)
            const std::optional<Bytes> file = app.vfs()->read(path);
            if (!file) continue;
            try {
                files_.push_back(menu::read_fonts(*file));
                for (const auto& [name, sp] : menu::read_font_spacing(*file)) spacing_.emplace(name, sp);
            } catch (const std::exception& e) {
                LOG_WARN("%s: %s", path, e.what());
            }
        }
        for (const gt3r::text::FontFile& f : files_)
            for (const gt3r::text::LatinFont& font : f.fonts) fonts_.emplace(font.name, &font);
        button_colors_ = program_button_colors(app);
    }
    ~PageDrawer() {
        for (auto& [face, a] : kanji_atlases_)
            if (a.texture) SDL_DestroyTexture(a.texture);
        for (auto& [image, t] : textures_) SDL_DestroyTexture(t.texture);
        for (auto& [font, t] : atlases_) SDL_DestroyTexture(t);
        for (auto& [k, f] : flashes_)
            for (SDL_Texture* t : f.textures)
                if (t) SDL_DestroyTexture(t);
    }

    // How far the picture behind the pages reaches past them on each side (page units): a colour as wide as the page goes on
    // to the screen's edges (the race's menus' shades and strips over a wide picture).
    float beside = 0;
    int widgets_drawn = 0;
    u64 menu_frame = 0; // the menus' picture count (Runtime::frame()): the flash faces' clock
    // A face whose content is not the layout's pictures and text (a movie, a 3D view): drawn by
    // this in its place in the page (`at`: the page's 640 x 480; `dst`: on the screen).
    // `project`, `page`: where its pictures are.
    std::function<void(const LiveWidget& face, menu::Project* project, const std::string& page, const Rect& at, const SDL_FRect& dst, float opacity)> draw_live;

    // A text's size as its face draws it (for the scripts' getTextSize).
    void measure(const LiveWidget& face, float& w, float& h) {
        const Widget& d = face.data;
        std::string s = d.text("text");
        if (s.empty()) s = d.text("value");
        menu::TextStyle st = style_of(d);
        st.one_line = d.type == "TextFace" && d.number("multiline") == 0;
        w = h = 0;
        if (!st.font || s.empty()) return;
        // A face of several lines (and a text box) breaks its lines at its width; an equalized
        // text is measured by the renderer's own break, not the word wrapper's that draws it
        // (0x2452A8: the license pages' information box).
        // (A face without a rectangle of its own fills its parent's.)
        std::optional<Rect> own = d.rect("geometry");
        if (!own && d.number("multiline") != 0 && d.number("packing") == 0)
            if (const LiveWidget* parent = face.parent_widget()) own = parent->data.rect("geometry");
        if (const std::optional<Rect>& g = own; g && g->w > 0 && (d.number("multiline") != 0 || d.type == "TextBoxFace") && d.number("packing") == 0) s = wrapped(s, st, g->w, d, false);
        // An abbreviated text measures as it is drawn, cut and ended in dots (the header's car name
        // is placed by its width: gtmode's refreshHeaderAlignment).
        if (const std::optional<Rect> g = d.rect("geometry"); g && g->w > 0 && d.number("abbreviate") != 0 && d.number("adjust_scale") == 0 &&
                                                               s.find('\n') == std::string::npos && width_of(s, st) > g->w) {
            while (s.size() > 1 && width_of(s + "...", st) > g->w) s.pop_back();
            s += "...";
        }
        st.align = gt3r::text::HAlign::Left, st.valign = gt3r::text::VAlign::Top;
        const menu::TextLayout lay = menu::layout_menu_text(s, st, 0, 0);
        w = lay.width, h = lay.height;
        // A text that is not `aligned` measures as wide as its rectangle and as high as its pen came
        // down: its line steps, without the last line (0x491618 to 0x491638).
        if (d.type == "TextFace" && d.has("aligned") && d.number("aligned") == 0) {
            w = d.number("multiline") != 0 && own ? own->w : 640.0f;
            h = float(st.font->line_height) * st.scale_y * float(lay.lines - 1);
        }
    }

    // The page's 640 x 480 into (x, y) at `scale`; `state`: the bits every widget is drawn in.
    // `project`: where the page's pictures and text are. `base`, `opacity`: the whole page's
    // (a transition's).
    void draw(menu::Project& project, const LiveWidget& page, float x, float y, float scale, int state, const Affine& base = {}, float opacity = 1) {
        use(project);
        ox_ = x, oy_ = y, scale_ = scale, page_ = page.name, widgets_drawn = 0, m_ = base;
        if (menu_frame % 60 == 0)
            for (auto it = textures_.begin(); it != textures_.end();)
                if (menu_frame > it->second.used + 600) SDL_DestroyTexture(it->second.texture), it = textures_.erase(it);
                else ++it;
        for (auto it = tips_.begin(); it != tips_.end();) // tips not shown for ten seconds
            if (menu_frame > it->second.seen + 600) it = tips_.erase(it);
            else ++it;
        const Rect whole{0, 0, 640, 480};
        set_clip(whole);
        over_.clear();
        forced_ = state;
        widget(page, {0, 0, 640, 480}, opacity, whole, 0);
        // An option menu's open list lies over everything else of the page.
        const std::vector<Over> over = std::move(over_);
        over_.clear();
        for (const Over& o : over) {
            set_clip(whole);
            m_ = o.m;
            widget(*o.live, o.parent, o.opacity, whole, o.state);
        }
        SDL_SetRenderClipRect(r_, nullptr);
    }

    // One widget by itself (the cursor), its rectangle in the page's 640 x 480 through `m`.
    void draw_one(menu::Project& project, const std::string& page, const LiveWidget& w, float x, float y, float scale, const Affine& m = {}) {
        use(project);
        ox_ = x, oy_ = y, scale_ = scale, page_ = page, m_ = m;
        const Rect whole{0, 0, 640, 480};
        set_clip(whole);
        widget(w, whole, 1.0f, whole, 0);
        SDL_SetRenderClipRect(r_, nullptr);
    }

private:
    static bool same(const Rgba& a, const Rgba& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }
    SDL_FRect to_screen(const Rect& r) const { return {ox_ + r.x * scale_, oy_ + r.y * scale_, r.w * scale_, r.h * scale_}; }
    // A point of the page through the widgets' transforms, on the screen.
    SDL_FPoint to_screen(float x, float y) const {
        const menu::Point p = m_.apply(x, y);
        return {ox_ + p.x * scale_, oy_ + p.y * scale_};
    }
    // A rectangle through the transforms: the box around it.
    Rect bounds(const Rect& r) const {
        if (m_.identity()) return r;
        const menu::Point p[4] = {m_.apply(r.x, r.y), m_.apply(r.x + r.w, r.y), m_.apply(r.x + r.w, r.y + r.h), m_.apply(r.x, r.y + r.h)};
        float x0 = p[0].x, y0 = p[0].y, x1 = x0, y1 = y0;
        for (const menu::Point& q : p) x0 = std::min(x0, q.x), y0 = std::min(y0, q.y), x1 = std::max(x1, q.x), y1 = std::max(y1, q.y);
        return {x0, y0, x1 - x0, y1 - y0};
    }
    // A quad of the page, corners clockwise from the top left, through the transforms: a
    // picture's coordinates `uv` (0..1) and a colour at each corner.
    void quad(SDL_Texture* t, const Rect& at, const SDL_FPoint uv[4], const SDL_FColor color[4], SDL_BlendMode blend) {
        const float xs[4] = {at.x, at.x + at.w, at.x + at.w, at.x}, ys[4] = {at.y, at.y, at.y + at.h, at.y + at.h};
        SDL_Vertex v[4];
        for (int i = 0; i < 4; ++i) v[i].position = to_screen(xs[i], ys[i]), v[i].color = color[i], v[i].tex_coord = uv[i];
        const int index[6] = {0, 1, 2, 0, 2, 3};
        if (t) SDL_SetTextureBlendMode(t, blend);
        else SDL_SetRenderDrawBlendMode(r_, blend);
        SDL_RenderGeometry(r_, t, v, 4, index, 6);
    }
    // A clip of the page (the stack's: each cut by the one before, 0x231A50) on the screen: the program's
    // scissor is in whole lines (menu::scissor; ❓ Prologue's is not read and kept as it is).
    void set_clip(const Rect& r) {
        const SDL_FRect f = to_screen(fonts_.count("hn_14") ? menu::scissor(r) : r);
        const int x0 = int(std::lround(f.x)), y0 = int(std::lround(f.y));
        const SDL_Rect c{x0, y0, int(std::lround(f.x + f.w)) - x0, int(std::lround(f.y + f.h)) - y0};
        SDL_SetRenderClipRect(r_, &c);
    }

    // By what the picture holds (image_key): a picture freed with its project leaves its
    // address to the next. `path`: its name (not needed for the key).
    SDL_Texture* texture(const gt3r::Tex1::Image* image, const std::string& path = {}) {
        (void)path;
        if (!image || !image->width || !image->height) return nullptr;
        Cached& c = textures_[image_key(*image)];
        c.used = menu_frame;
        SDL_Texture*& t = c.texture;
        if (!t) {
            t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(image->width), int(image->height));
            if (t) {
                SDL_UpdateTexture(t, nullptr, image->rgba.data(), int(image->width * 4));
                SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
            }
        }
        return t;
    }

    // A picture over a rectangle in a face's colour, or in four for its corners clockwise from the top left. The
    // program gives one colour to a textured quad halved (0x4AA168: 255 leaves the picture as it is) and the colours
    // of corners whole (0x49A150: 255 makes it twice as bright, up to white).
    // `uv`: the picture's coordinates of the four corners clockwise from the top left (mImageFace's `texcoord`); none: the
    // whole picture. `held`: the picture itself; else `path`'s of the page, or with `from_disk` of the disc.
    void image(const std::string& path, const Rect& at, const std::vector<Rgba>& colors, float opacity, bool add, const SDL_FPoint* uv = nullptr,
               bool from_disk = false, const gt3r::Tex1::Image* held = nullptr) {
        const gt3r::Tex1::Image* img = held ? held : path.empty() ? nullptr : from_disk ? disk_image(path) : project_->image(page_, path);
        SDL_Texture* t = texture(img, path);
        if (!t) return;
        SDL_SetTextureColorMod(t, 255, 255, 255);
        SDL_SetTextureAlphaMod(t, 255);
        const bool corners = colors.size() >= 4;
        const Rgba tint = colors.empty() ? Rgba{} : colors[0];
        const SDL_BlendMode blend = add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND;
        if (m_.identity() && !uv && !corners && at.w >= 0 && at.h >= 0) {
            SDL_SetTextureColorMod(t, tint.r, tint.g, tint.b);
            SDL_SetTextureAlphaMod(t, u8(std::clamp(tint.a * opacity, 0.0f, 255.0f)));
            SDL_SetTextureBlendMode(t, blend);
            const SDL_FRect dst = to_screen(at);
            SDL_RenderTexture(r_, t, nullptr, &dst);
            return;
        }
        static const SDL_FPoint whole[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        SDL_FColor c[4];
        for (size_t i = 0; i < 4; ++i) {
            const std::array<float, 4> k = menu::corner_color(corners ? colors[i] : tint, opacity, corners);
            c[i] = {k[0], k[1], k[2], k[3]};
        }
        quad(t, at, uv ? uv : whole, c, blend);
    }
    // A picture face's `from_disk` picture: a Tex1 file of the disc (`piece/nation_bg/japan.img`).
    const gt3r::Tex1::Image* disk_image(const std::string& path) {
        std::unique_ptr<gt3r::Tex1::Image>& slot = disk_images_[path];
        if (slot || !vfs_ || tried_.count(path)) return slot.get();
        tried_.insert(path);
        std::string name = path;
        while (!name.empty() && name[0] == '/') name.erase(0, 1);
        try {
            const std::optional<Bytes> data = vfs_->read(name);
            if (data && gt3r::Tex1::is_tex1(*data)) {
                const gt3r::Tex1 set = gt3r::Tex1::parse(*data);
                if (!set.textures.empty()) {
                    gt3r::GsMemory gs;
                    set.upload(*data, gs);
                    slot = std::make_unique<gt3r::Tex1::Image>(set.decode(gs, 0));
                }
            }
        } catch (const std::exception& e) {
            LOG_WARN("Menu picture %s: %s", path.c_str(), e.what());
        }
        return slot.get();
    }

    // A car's paint (mColorChipFace 0x159C38): its words' colours in bands down the face (or shaded
    // from one to the next), the gloss picture added to them: the first for a metallic paint, the
    // second for a solid one. With no colour nothing is drawn.
    void chip(const Widget& w, const Rect& at, float opacity, const std::vector<Rgba>& tint) {
        u32 word[4] = {};
        int n = 0;
        for (int i = 0; i < 4; ++i) {
            word[i] = u32(s32(w.number("chip_" + std::to_string(i))));
            n += (word[i] >> 24) & 1;
        }
        if (n <= 0) return;
        const bool metallic = (word[0] >> 25) & 1, shaded = (word[0] >> 26) & 1, reversed = (word[0] >> 27) & 1;
        // The bands' ends, as parts of the height from the first color on.
        static const float ends[4][4] = {{1, 1, 1, 1}, {0.5f, 1, 1, 1}, {0.5f, 0.75f, 1, 1}, {0.5f, 0.75f, 0.875f, 1}};
        static const float shades[4][4] = {{1, 1, 1, 1}, {1, 1, 1, 1}, {0.5f, 1, 1, 1}, {0.5f, 0.75f, 1, 1}};
        auto color = [&](int i) { return Rgba{u8(word[i]), u8(word[i] >> 8), u8(word[i] >> 16), 255}; };
        const int bands = shaded && n > 1 ? n - 1 : n;
        float from = 0;
        for (int i = 0; i < bands; ++i) {
            const float to = (shaded && n > 1 ? shades : ends)[n - 1][i];
            const float y0 = reversed ? 1 - to : from, y1 = reversed ? 1 - from : to;
            Rgba top = color(i), bottom = shaded && n > 1 ? color(i + 1) : top;
            if (reversed) std::swap(top, bottom);
            fill({at.x, at.y + at.h * y0, at.w, at.h * (y1 - y0)}, {top, top, bottom, bottom}, opacity, false);
            from = to;
        }
        image(w.text(metallic ? "image_path0" : "image_path1"), at, tint, opacity, true); // (blend mode 1, ALPHA 0x48: 0x1598B8, 0x215ED0)
    }

    // One colour, or four for the corners clockwise from the top left.
    // `round`: with one colour, the corners rounded to that radius (at most half the
    // rectangle's sides), each a fan of 8 steps (mColorFace, 0x2857C0).
    void fill(const Rect& at, const std::vector<Rgba>& colors, float opacity, bool add, float round = 0) {
        if (colors.empty()) return;
        auto color = [&](size_t i) {
            const Rgba c = colors[colors.size() >= 4 ? i : 0];
            return SDL_FColor{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f * opacity};
        };
        const SDL_BlendMode blend = add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND;
        const float r = std::min({round, at.w / 2, at.h / 2});
        if (colors.size() != 1 || r <= 0) {
            static const SDL_FPoint none[4] = {};
            const SDL_FColor c[4] = {color(0), color(1), color(2), color(3)};
            quad(nullptr, at, none, c, blend);
            return;
        }
        // A fan from the middle round the outline: each corner's arc of 8 steps.
        std::vector<SDL_Vertex> v;
        const SDL_FColor c = color(0);
        v.push_back({to_screen(at.x + at.w / 2, at.y + at.h / 2), c, {0, 0}});
        const float cx[4] = {at.x + at.w - r, at.x + at.w - r, at.x + r, at.x + r}, cy[4] = {at.y + r, at.y + at.h - r, at.y + at.h - r, at.y + r};
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i <= 8; ++i) {
                const float a = (-90.0f + 90.0f * float(k) + 90.0f * float(i) / 8.0f) * 0.017453292f;
                v.push_back({to_screen(cx[k] + r * std::cos(a), cy[k] + r * std::sin(a)), c, {0, 0}});
            }
        std::vector<int> index;
        const int n = int(v.size()) - 1;
        for (int i = 0; i < n; ++i) index.insert(index.end(), {0, 1 + i, 1 + (i + 1) % n});
        SDL_SetRenderDrawBlendMode(r_, blend);
        SDL_RenderGeometry(r_, nullptr, v.data(), int(v.size()), index.data(), int(index.size()));
    }

    // A flash face (mFlashFace, 0x292D70 / 0x292E70): its movie's frame at the face's time (1/60 s
    // a picture from its start or its reset(t); round with `repeat`, else nothing past the end),
    // fitted inside the rectangle and centred.
    void flash(const LiveWidget& live, const Rect& at, float opacity) {
        const Widget& w = live.data;
        const std::string path = w.text("flash_path");
        if (path.empty()) return;
        const bool disk = w.number("from_disk") != 0;
        const std::string key = (disk ? std::string("/") : page_ + "|") + path;
        Flash& f = flashes_[key];
        if (!f.tried) {
            f.tried = true;
            std::optional<Bytes> file;
            if (disk && vfs_) {
                std::string name = path;
                while (!name.empty() && name[0] == '/') name.erase(0, 1);
                file = vfs_->read(name);
            } else file = project_->entry(page_, path);
            try {
                if (file && menu::FlashMovie::is_movie(*file)) {
                    f.movie = menu::FlashMovie::parse(*file);
                    for (const menu::FlashMovie::Image& im : f.movie.images) {
                        SDL_Texture* tex = nullptr;
                        if (size_t(im.offset) + im.size <= file->size()) {
                            const ByteSpan tex1(file->data() + im.offset, im.size);
                            if (gt3r::Tex1::is_tex1(tex1)) {
                                const gt3r::Tex1 set = gt3r::Tex1::parse(tex1);
                                gt3r::GsMemory gs;
                                set.upload(tex1, gs);
                                const gt3r::Tex1::Image img = set.decode(gs, 0);
                                tex = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(img.width), int(img.height));
                                if (tex) {
                                    SDL_UpdateTexture(tex, nullptr, img.rgba.data(), int(img.width * 4));
                                    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                                }
                            }
                        }
                        f.textures.push_back(tex);
                    }
                    f.ok = true;
                }
            } catch (const std::exception& e) {
                LOG_WARN("Flash %s: %s", path.c_str(), e.what());
            }
        }
        if (!f.ok || f.movie.width <= 0 || f.movie.height <= 0) return;
        Clock& c = clocks_[&live];
        const int restarts = int(w.number("flash_restarts"));
        if (c.path != key || c.restarts != restarts || menu_frame < c.start) c.path = key, c.restarts = restarts, c.start = menu_frame, c.from = w.number("flash_restart_at");
        float time = c.from + float(menu_frame - c.start) / 60.0f;
        const float length = f.movie.frames > 0 && f.movie.rate > 0 ? float(f.movie.frames) / f.movie.rate : 0;
        if (length > 0 && time >= length) time = (!w.has("repeat") || w.number("repeat") != 0) ? std::fmod(time, length) : length;
        const float k = std::min(at.w / f.movie.width, at.h / f.movie.height);
        const float ox = at.x + (at.w - f.movie.width * k) / 2, oy = at.y + (at.h - f.movie.height * k) / 2;
        for (const menu::FlashQuad& q : f.movie.at(time)) {
            if (q.image < 0 || size_t(q.image) >= f.textures.size() || !f.textures[size_t(q.image)]) continue;
            const SDL_FPoint uv[4] = {{q.u0, q.v0}, {q.u1, q.v0}, {q.u1, q.v1}, {q.u0, q.v1}};
            const SDL_FColor col{std::min(q.r, 1.0f), std::min(q.g, 1.0f), std::min(q.b, 1.0f), std::clamp(q.a * opacity, 0.0f, 1.0f)};
            const SDL_FColor cols[4] = {col, col, col, col};
            SDL_Texture* tex = f.textures[size_t(q.image)];
            SDL_SetTextureColorMod(tex, 255, 255, 255), SDL_SetTextureAlphaMod(tex, 255);
            quad(tex, {ox + q.x0 * k, oy + q.y0 * k, (q.x1 - q.x0) * k, (q.y1 - q.y0) * k}, uv, cols, SDL_BLENDMODE_BLEND);
        }
    }

    // A graph (mGraphFace, 0x2E88F8): its points as fractions of the rectangle, through the
    // transforms, each segment a band of the line's width across it (0x218430).
    void graph(const Widget& w, const Rect& at, float opacity) {
        const menu::GraphFaceSettings g = menu::graph_face_settings(w);
        const float half = g.line_width * 0.5f * scale_;
        const SDL_FColor c{g.color.r / 255.0f, g.color.g / 255.0f, g.color.b / 255.0f, g.color.a / 255.0f * opacity};
        std::vector<SDL_Vertex> v;
        std::vector<int> index;
        for (const auto& [p, q] : menu::graph_segments(g)) {
            const SDL_FPoint a = to_screen(at.x + p.x * at.w, at.y + p.y * at.h), b = to_screen(at.x + q.x * at.w, at.y + q.y * at.h);
            const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
            if (len <= 0) continue;
            const float ox = half * dy / len, oy = half * dx / len;
            const int base = int(v.size());
            v.push_back({{a.x + ox, a.y - oy}, c, {0, 0}});
            v.push_back({{a.x - ox, a.y + oy}, c, {0, 0}});
            v.push_back({{b.x + ox, b.y - oy}, c, {0, 0}});
            v.push_back({{b.x - ox, b.y + oy}, c, {0, 0}});
            index.insert(index.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
        }
        if (v.empty()) return;
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        SDL_RenderGeometry(r_, nullptr, v.data(), int(v.size()), index.data(), int(index.size()));
    }

    // A frame (mFrameImageFace, 0x298288): nine parts about its frame_width x frame_height (menu::frame_parts), each its
    // picture stretched over it in the face's colour; with four, each part's corners have the face's own colours there
    // (0x2979A8). A part without a picture is filled with the colour.
    void frame(const Widget& w, const Rect& at, const std::vector<Rgba>& colors, float opacity, bool add) {
        for (const menu::FramePart& part : menu::frame_parts(w, at.w, at.h)) {
            const Rect r{at.x + part.at.x, at.y + part.at.y, part.at.w, part.at.h};
            const std::vector<Rgba> c = menu::frame_part_colors(colors, part.at, at.w, at.h);
            const std::string path = w.text(part.picture);
            if (!path.empty() && project_->image(page_, path)) image(path, r, c, opacity, add);
            else fill(r, c.empty() ? std::vector<Rgba>{Rgba{}} : c, opacity, add);
        }
    }

    // "hm_16", or with a palette "hm_16#1"; none: `hn_14`, the text style's default
    // (0x246F58); ❓ a name the files do not have (`sw_20`): the menus' body font, `hm_16`.
    const gt3r::text::LatinFont* font_of(const Widget& w, gt3r::text::FontVariant* variant = nullptr, std::string* found = nullptr) const {
        // The text style's default font: `hn_14` (0x246F58); Prologue's `sw_16` (0x2F75D0).
        std::string name = w.text("font", fonts_.count("hn_14") ? "hn_14" : "sw_16");
        if (const size_t hash = name.find('#'); hash != std::string::npos) {
            if (variant) *variant = name.compare(hash, 2, "#0") == 0 ? gt3r::text::FontVariant::Outlined : gt3r::text::FontVariant::Plain;
            name.resize(hash);
        }
        auto it = fonts_.find(name);
        if (it == fonts_.end()) it = fonts_.find("hm_16");
        if (it == fonts_.end()) it = fonts_.find("sw_16");
        if (found && it != fonts_.end()) *found = it->first;
        return it == fonts_.end() ? nullptr : it->second;
    }
    // A face's text style: its font and scales; the alignment and colour are set by the caller.
    menu::TextStyle style_of(const Widget& w, gt3r::text::FontVariant* variant = nullptr) const {
        menu::TextStyle st;
        std::string name;
        st.font = font_of(w, variant, &name);
        if (const auto sp = spacing_.find(name); sp != spacing_.end()) st.spacing = sp->second;
        if (const auto g = fonts_.find("Gaiji"); g != fonts_.end()) st.gaiji = g->second;
        st.button_colors = button_colors_;
        st.scale_x = w.number("scale_x", 1), st.scale_y = w.number("scale_y", 1);
        // ❓ The double-byte face nearest the font's cell (unicode.kf has one).
        if (kanji_ && st.font) st.kanji = kanji_->face_for_height(st.font->kanji_height);
        return st;
    }
    // The project whose pages are drawn, and its language's double-byte font (read once).
    void use(menu::Project& project) {
        project_ = &project;
        auto [it, fresh] = kanji_fonts_.try_emplace(project.language());
        if (fresh && vfs_)
            for (const std::string& path : menu::kanji_font_paths(project.language())) {
                const std::optional<Bytes> file = vfs_->read(path);
                if (!file) continue;
                try {
                    it->second = menu::KanjiFont::parse(*file);
                } catch (const std::exception& e) {
                    LOG_WARN("%s: %s", path.c_str(), e.what());
                }
                break;
            }
        kanji_ = it->second ? &*it->second : nullptr;
    }
    // A double-byte cell in a palette: its place in the face's atlas, filled on first use.
    SDL_Texture* kanji_cell(const menu::KanjiFace& face, size_t palette, u32 code, SDL_FRect& src) {
        if (!kanji_) return nullptr;
        const int cell = face.cell(code);
        if (cell < 0 || face.cell_width <= 0 || face.cell_height <= 0) return nullptr;
        KanjiAtlas& a = kanji_atlases_[&face];
        constexpr int kSide = 2048;
        // Each cell with a pixel of clear border, so that its neighbours do not bleed in.
        const int pw = face.cell_width + 2, ph = face.cell_height + 2;
        const int cols = kSide / pw, rows = kSide / ph;
        if (!a.texture) {
            a.texture = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kSide, kSide);
            if (!a.texture) return nullptr;
            SDL_SetTextureBlendMode(a.texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(a.texture, SDL_SCALEMODE_LINEAR);
        }
        auto [it, fresh] = a.slots.try_emplace({palette, code}, int(a.slots.size()));
        if (fresh) {
            if (it->second >= cols * rows) { // full: start again
                a.slots.clear();
                it = a.slots.try_emplace({palette, code}, 0).first;
            }
            const std::vector<u32> px = menu::cell_rgba(*kanji_, face, cell, palette);
            std::vector<u32> padded(size_t(pw) * size_t(ph), 0);
            for (int y = 0; y < face.cell_height; ++y)
                std::copy_n(px.begin() + std::ptrdiff_t(y * face.cell_width), face.cell_width, padded.begin() + std::ptrdiff_t((y + 1) * pw + 1));
            const SDL_Rect at{(it->second % cols) * pw, (it->second / cols) * ph, pw, ph};
            SDL_UpdateTexture(a.texture, &at, padded.data(), pw * 4);
        }
        src = {float((it->second % cols) * pw + 1), float((it->second / cols) * ph + 1), float(face.cell_width), float(face.cell_height)};
        return a.texture;
    }
    float width_of(const std::string& line, menu::TextStyle st) const {
        st.align = gt3r::text::HAlign::Left, st.valign = gt3r::text::VAlign::Top;
        return menu::layout_menu_text(line, st, 0, 0).width;
    }
    // Whether the program's word wrapper spreads an `equalized` face's lines: not in Japanese,
    // Korean and Chinese (0x245568: language 0 and 9 to 11), nor in Prologue.
    bool word_wrapper() const {
        if (!fonts_.count("hn_14")) return false;
        const std::string lang = project_ ? project_->language() : std::string("US");
        return lang != "JP" && lang != "KR" && lang != "TW" && lang != "CN";
    }
    // A text of several lines, broken as its face's text style has it: `equalized` by the word
    // wrapper; else a face whose text is `aligned` (the default) only at its line ends; one whose
    // text is not, at the character that would pass its right edge.
    std::string wrapped(const std::string& s, const menu::TextStyle& st, float width, const Widget& face, bool drawn = true) const {
        const bool box = face.type == "TextBoxFace";
        if (drawn && face.number("equalized") != 0 && word_wrapper()) return wrapped_words(s, st, width);
        if (!box) return face.has("aligned") && face.number("aligned") == 0 ? broken_characters(s, st, width) : s;
        // ❓ A text box's own break (mTextBoxFace) is not read: at a space where the next word would
        // pass the width. Prologue's carry their line ends (its card dialog's first line passes
        // its face's width by 5 pixels in one line).
        if (!fonts_.count("hn_14")) return s;
        std::string out, line;
        auto flush = [&] { out += line, line.clear(); };
        size_t at = 0;
        while (at <= s.size()) {
            const size_t end = std::min(s.find_first_of(" \n", at), s.size());
            const std::string word = s.substr(at, end - at);
            const std::string tried = line.empty() ? word : line + " " + word;
            if (!line.empty() && width_of(tried, st) > width + 0.5f) flush(), out += '\v', line = word;
            else line = tried;
            if (end < s.size() && s[end] == '\n') flush(), out += '\n';
            at = end + 1;
        }
        flush();
        return out;
    }
    // The renderer's break (0x490660, 0x492748, 0x492E14): when the pen after a character reaches
    // the width less the font's double-byte step, the next character starts a line, unless it is
    // one that may not (. , ' and, after a double-byte character, the comma, the full stop, the
    // closing bracket, the small kana and the full-width dash).
    std::string broken_characters(const std::string& s, const menu::TextStyle& st, float width) const {
        const gt3r::text::FontGlyph* space_glyph = st.font ? st.font->glyph(' ') : nullptr;
        const int advance = space_glyph ? int(space_glyph->advance) : st.font ? int(st.font->size / 2) : 8;
        const float pen_extra = float(advance / 2) * st.scale_x; // (a line's width is its pen less that)
        const float limit = width - float(st.spacing.wide_advance) * st.scale_x;
        std::string out, line;
        for (size_t at = 0; at < s.size();) {
            const u8 c = u8(s[at]);
            size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
            if (c == '%' && at + 1 < s.size()) n = s[at + 1] == 'C' ? 5 : s[at + 1] == 'B' ? 3 : 2;
            n = std::min(n, s.size() - at);
            const std::string ch = s.substr(at, n);
            at += n;
            if (c == '\n') {
                out += line, out += '\n', line.clear();
                continue;
            }
            line += ch;
            if (at >= s.size() || s[at] == '\n' || width_of(line, st) + pen_extra < limit) continue;
            const std::string rest = s.substr(at, 3);
            bool stays = rest[0] == '.' || rest[0] == ',' || rest[0] == '\'';
            if (c >= 0xE0)
                for (const char* k : {"\u3001", "\u3002", "\u300C", "\u3083", "\u3085", "\u3087", "\u3063", "\uFF0D"}) stays |= rest == k;
            if (stays) continue;
            out += line, out += '\n', line.clear();
        }
        return out + line;
    }
    std::string wrapped_words(const std::string& s, const menu::TextStyle& st, float width) const {
        // The program's word wrapper (0x245C90): each word measured by itself; a word starts a new
        // line when the line so far (its words, each with a space after it) and it reach the
        // width less the font's double-byte step (❓ that margin: the renderer's own right edge,
        // 0x490660, which the four reference texts' breaks agree with; the comparison read at
        // 0x245F1C is against the bare width).
        const gt3r::text::FontGlyph* space_glyph = st.font ? st.font->glyph(' ') : nullptr;
        const int advance = space_glyph ? int(space_glyph->advance) : st.font ? int(st.font->size / 2) : 8;
        const float space = float(advance) * st.scale_x;
        // (A word's width is its pen's: width_of() gives a line's, half a space less.)
        const float pen_extra = float(advance / 2) * st.scale_x;
        const float limit = width - float(st.spacing.wide_advance) * st.scale_x;
        std::string out, line;
        float cur = 0;
        auto flush = [&] { out += line, line.clear(), cur = 0; };
        size_t at = 0;
        while (at <= s.size()) {
            const size_t end = std::min(s.find_first_of(" \n", at), s.size());
            const std::string word = s.substr(at, end - at);
            const float x = word.empty() ? 0.0f : width_of(word, st) + pen_extra;
            if (!line.empty() && cur + x >= limit) flush(), out += '\v';
            line = line.empty() ? word : line + " " + word;
            cur += x + space;
            if (end < s.size() && s[end] == '\n') flush(), out += '\n';
            at = end + 1;
        }
        flush();
        return out;
    }

    SDL_Texture* atlas_of(const gt3r::text::LatinFont& font, gt3r::text::FontVariant variant) {
        SDL_Texture*& atlas = atlases_[{&font, int(variant)}];
        if (!atlas) {
            const gt3r::Tex1::Image& img = font.atlas[size_t(variant)];
            if (!img.width || !img.height) return nullptr;
            atlas = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(img.width), int(img.height));
            if (!atlas) return nullptr;
            SDL_UpdateTexture(atlas, nullptr, img.rgba.data(), int(img.width * 4));
            SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(atlas, SDL_SCALEMODE_LINEAR);
        }
        return atlas;
    }

    // An input's caret (0x2A8170): 4 wide and as high as a line where the next letter goes, in half the text's color, while the face
    // has the focus state, 15 pictures of 30 (0x2A8480).
    void caret(const Widget& w, const std::string& s, float x, float y, float line, menu::TextStyle st, const Rgba& color, float opacity) {
        if (menu_frame % 30 >= 15) return;
        const size_t after = size_t(std::max(w.number("input_after"), 0.0f));
        size_t cut = s.size();
        for (size_t k = 0; k < after && cut > 0; ++k) cut = menu::utf8_before(s, cut);
        // Where the next letter's pen is.
        st.align = gt3r::text::HAlign::Left, st.valign = gt3r::text::VAlign::Top;
        const menu::TextLayout next = menu::layout_menu_text(s.substr(0, cut) + "M", st, 0, 0), alone = menu::layout_menu_text("M", st, 0, 0);
        const float pen = next.glyphs.empty() || alone.glyphs.empty() ? 0.0f : next.glyphs.back().x - alone.glyphs[0].x;
        fill({x + pen, y, 4, line}, {Rgba{color.r, color.g, color.b, u8(color.a / 2)}}, opacity, false);
    }

    // `id`: the live face (a tool tip's motion is kept by it).
    void text(const Widget& w, const Rect& at, float opacity, const Rect& clip, const void* id = nullptr, bool typing = false) {
        std::string s = w.text("text");
        if (s.empty()) s = w.text("value"); // a tool tip's, an input's
        if (s.empty() && w.has("key")) {
            const std::string page = w.has("key_page") ? w.text("key_page") : page_;
            // A tool tip's key names its page's "TOOLTIP|<key>" (0x249B70); ❓ the key's own text
            // when there is none, and nothing when the key has no text either (the category
            // pages' "Real": the original's strip is empty).
            if (w.type == "ToolTipFace") s = project_->translate(page, "TOOLTIP|" + w.text("key"));
            if (s.empty() || s == "TOOLTIP|" + w.text("key")) {
                s = project_->translate(page, w.text("key"));
                if (w.type == "ToolTipFace" && s == w.text("key")) s.clear();
            }
        }
        const bool empty = s.empty();
        if (empty && typing) s = " ";
        if (s.empty()) return;
        gt3r::text::FontVariant variant = gt3r::text::FontVariant::Plain;
        menu::TextStyle st = style_of(w, &variant);
        if (!st.font) return;
        st.one_line = w.type == "TextFace" && w.number("multiline") == 0;
        // adjustScale(): drawn narrower when it would pass the face's width; abbreviate(): cut
        // there and ended in dots.
        if (at.w > 0) {
            const float width = width_of(s, st); // the widest line's
            if (const int adjust = int(w.number("adjust_scale")); adjust != 0) {
                // (0x2455C0: each axis by itself, where the text passes the face.)
                menu::TextStyle plain = st;
                plain.align = gt3r::text::HAlign::Left, plain.valign = gt3r::text::VAlign::Top;
                const float height = menu::layout_menu_text(s, plain, 0, 0).height;
                if ((adjust & 1) && width > at.w) st.scale_x *= at.w / width;
                if ((adjust & 2) && height > at.h && at.h > 0) st.scale_y *= at.h / height;
            } else if (width > at.w && w.number("abbreviate") != 0 && s.find('\n') == std::string::npos) {
                while (s.size() > 1 && width_of(s + "...", st) > at.w) s.pop_back();
                s += "...";
            }
        }
        if ((w.number("multiline") != 0 || w.type == "TextBoxFace") && w.number("packing") == 0 && at.w > 0) s = wrapped(s, st, at.w, w);
        // Where in its rectangle: 0..8, rows from the top, columns from the left; the middle (4)
        // when not given (the text style's default).
        // The program's word wrapper (`equalized`, 0x245C90) draws word by word from the pen at the
        // rectangle's top left (0x490A08, 0x490660, 0x490760), and so does the renderer a text that
        // is not `aligned` (0x491490): `align` is not used there, and a text taller than its
        // rectangle runs on below it (the Driving Missions' texts).
        const bool spread = w.number("equalized") != 0 && at.w > 0 && word_wrapper();
        const bool unaligned = w.type == "TextFace" && w.has("aligned") && w.number("aligned") == 0;
        const int align = spread || unaligned ? 0 : w.has("align") ? int(w.number("align")) : 4;
        const int col = align % 3, row = align / 3;
        st.align = col == 0 ? gt3r::text::HAlign::Left : col == 1 ? gt3r::text::HAlign::Center : gt3r::text::HAlign::Right;
        st.valign = row == 0 ? gt3r::text::VAlign::Top : row == 1 ? gt3r::text::VAlign::Middle : gt3r::text::VAlign::Bottom;
        float x = col == 0 ? at.x : col == 1 ? at.x + at.w / 2 : at.x + at.w;
        float y = row == 0 ? at.y : row == 1 ? at.y + at.h / 2 : at.y + at.h;
        std::optional<Rect> tip_clip;
        if (w.type == "ToolTipFace") {
            // A tool tip is not aligned (its style's `aligned` off, 0x248DD0): a new text rises
            // into the face, in the middle across when it fits 95 % of it, else from 5 % in and
            // scrolling (menu/faces_tooltip.*); cut to the face.
            const float tw = width_of(s, st);
            st.align = gt3r::text::HAlign::Left, st.valign = gt3r::text::VAlign::Top;
            Tip& tip = tips_[id ? id : &w];
            if (tip.text != s || tip.w != at.w) {
                tip.text = s, tip.w = at.w;
                tip.scroll.start(tw, at.w, at.h);
                tip.stepped = menu_frame;
            }
            for (int n = 0; tip.stepped < menu_frame && n < 600; ++n, ++tip.stepped) tip.scroll.step();
            tip.stepped = menu_frame;
            tip.seen = menu_frame;
            x = at.x + tip.scroll.x(), y = at.y + tip.scroll.y();
            tip_clip = menu::cut(clip, bounds(at));
        }
        const Rgba base = w.color("text_color").value_or(Rgba{212, 212, 212, 255}); // the text style's default
        st.color = {base.r, base.g, base.b, base.a};
        if (spread) st.justify_width = at.w;
        const menu::TextLayout lay = menu::layout_menu_text(s, st, x, y);

        // A pass of the glyphs; `shadow`: all in one colour, offset.
        auto pass = [&](const Rgba* shadow, float dx, float dy) {
            for (const menu::PlacedGlyph& g : lay.glyphs) {
                if (!g.sw || !g.sh) continue;
                SDL_Texture* atlas = nullptr;
                SDL_FRect src{float(g.sx), float(g.sy), float(g.sw), float(g.sh)};
                if (g.font) atlas = atlas_of(*g.font, g.font == st.font ? variant : gt3r::text::FontVariant::Plain);
                else if (st.kanji) {
                    // A double-byte cell: the face's pair of palettes, the font's variant of it.
                    const size_t face_index = size_t(st.kanji - kanji_->faces.data());
                    atlas = kanji_cell(*st.kanji, face_index * 2 + size_t(variant), g.code, src);
                }
                if (!atlas) continue;
                const Rgba c = shadow ? *shadow : Rgba{g.color.r, g.color.g, g.color.b, g.color.a};
                SDL_SetTextureColorMod(atlas, c.r, c.g, c.b);
                SDL_SetTextureAlphaMod(atlas, u8(std::clamp(c.a * opacity, 0.0f, 255.0f)));
                if (m_.identity()) {
                    const SDL_FRect dst = to_screen({g.x + dx, g.y + dy, g.w, g.h});
                    SDL_RenderTexture(r_, atlas, &src, &dst);
                    continue;
                }
                float aw = 1, ah = 1;
                SDL_GetTextureSize(atlas, &aw, &ah);
                const float u0 = src.x / aw, v0 = src.y / ah, u1 = (src.x + src.w) / aw, v1 = (src.y + src.h) / ah;
                const SDL_FPoint uv[4] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
                const SDL_FColor fc{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, std::clamp(c.a * opacity / 255.0f, 0.0f, 1.0f)};
                const SDL_FColor fcs[4] = {fc, fc, fc, fc};
                SDL_SetTextureColorMod(atlas, 255, 255, 255);
                SDL_SetTextureAlphaMod(atlas, 255);
                quad(atlas, {g.x + dx, g.y + dy, g.w, g.h}, uv, fcs, SDL_BLENDMODE_BLEND);
            }
        };
        if (tip_clip) set_clip(*tip_clip);
        if (const float shadow = w.number("shadow"); shadow > 0) {
            const Rgba sc = w.color("shadow_color").value_or(Rgba{0, 0, 0, 255});
            pass(&sc, shadow, shadow);
        }
        pass(nullptr, 0, 0);
        if (typing) caret(w, empty ? std::string() : s, lay.x, lay.y, lay.height, st, base, opacity);
        if (tip_clip) set_clip(clip);
    }

    // A face is drawn in its parent's state, any other widget in its own (0x231508), shown by its condition (0x265DC8).
    void widget(const LiveWidget& live, const Rect& parent, float opacity, const Rect& clip, int parent_state) {
        const Widget& w = live.data;
        const int state = menu::drawn_state(live, parent_state) | forced_;
        if (w.has("visible") && w.number("visible") == 0) return;
        if (!menu::state_shows(state, int(w.number("visible_condition")), int(w.number("visible_compare")))) return;
        Rect at = parent;
        if (const std::optional<Rect> g = w.rect("geometry")) at = {parent.x + g->x, parent.y + g->y, g->w, g->h};
        opacity *= std::clamp(w.number("opacity", 1), 0.0f, 1.0f);
        if (opacity <= 0) return;
        // A widget made larger (a move actor's, a list's chosen row) grows about its middle.
        if (const float sx = w.number("sx", 1), sy = w.number("sy", 1); sx != 1 || sy != 1)
            at = {at.x - at.w * (sx - 1) / 2, at.y - at.h * (sy - 1) / 2, at.w * sx, at.h * sy};
        const float face = opacity * std::clamp(w.number("alpha", 1), 0.0f, 1.0f); // the face's own, not its children's
        ++widgets_drawn;
        // Its own transform carries to its children.
        const Affine outer = m_;
        if (const Affine own = widget_transform(w, at); !own.identity()) m_ = m_.after(own);
        struct Restore {
            Affine& m;
            Affine was;
            ~Restore() { m = was; }
        } restore{m_, outer};
        const std::vector<Rgba> colors = w.colors("color");
        const bool add = w.number("blend") != 0;
        const std::string& t = w.type;
        if (t == "ColorFace") {
            const bool level = colors.size() < 4 || (same(colors[0], colors[1]) && same(colors[2], colors[3]));
            if (beside > 0 && m_.identity() && level && w.number("round") == 0 && at.x <= 0.5f && at.x + at.w >= 639.5f && clip.x <= 0 && clip.x + clip.w >= 640) {
                set_clip({-beside, clip.y, 640 + 2 * beside, clip.h});
                fill({-beside, at.y, 640 + 2 * beside, at.h}, colors, face, add);
                set_clip(clip);
            } else fill(at, colors, face, add, w.number("round"));
        }
        else if (t == "ImageFace" || t == "ProgressFace") {
            // `texcoord`: the picture's corners clockwise from the top left.
            SDL_FPoint uv[4];
            const menu::Property* tc = w.find("texcoord");
            const bool has_uv = tc && tc->items.size() >= 4;
            if (has_uv)
                for (size_t i = 0; i < 4; ++i)
                    uv[i] = {tc->items[i].values.size() > 0 ? std::strtof(tc->items[i].values[0].c_str(), nullptr) : 0,
                             tc->items[i].values.size() > 1 ? std::strtof(tc->items[i].values[1].c_str(), nullptr) : 0};
            // A picture the script holds (`image_chunk`), else the one the layout names.
            const gt3r::Tex1::Image* chunk = nullptr;
            if (live.runtime && w.has("image_chunk")) {
                const auto& held = live.runtime->game().chunk_pictures;
                if (const auto it = held.find(w.text("image_chunk")); it != held.end()) chunk = &it->second;
            }
            image(w.text("image_path"), at, colors, face, add, has_uv ? uv : nullptr, w.number("from_disk") != 0, chunk);
        }
        else if (t == "FrameImageFace") frame(w, at, colors, face, add);
        else if (t == "ToolTipFace" && w.number("detached") == 0 && w.text("value").empty() && w.text("text").empty() && !w.has("key")) {
            // An attached tip: the focused widget's (or the nearest parent's with one) tool tip.
            Widget tip = w;
            auto put = [&tip](const char* name, const std::string& value) {
                for (menu::Property& p : tip.properties)
                    if (p.name == name) return void(p.values = {value});
                menu::Property p;
                p.name = name, p.type = "string", p.values = {value};
                tip.properties.push_back(p);
            };
            LiveWidget* page = const_cast<LiveWidget&>(live).page();
            std::shared_ptr<LiveWidget> focus = page ? page->focus.lock() : nullptr;
            for (const LiveWidget* f = focus.get(); f; f = f->parent_widget())
                if (f->data.number("tooltip") != 0 && !f->data.text("tooltip_key").empty()) {
                    put("key", f->data.text("tooltip_key"));
                    if (LiveWidget* fp = const_cast<LiveWidget*>(f)->page()) put("key_page", fp->name);
                    break;
                }
            text(tip, at, face, clip, &live);
        } else if (t == "TextFace" || t == "InputTextFace" || t == "InputNumberFace" || t == "ToolTipFace" || t == "TextBoxFace")
            text(w, at, face, clip, &live, t == "InputTextFace" && (state & menu::kFocus));
        else if (t == "ColorChipFace") {
            chip(w, at, face, colors);
        } else if (t == "GraphFace") graph(w, at, face);
        else if (t == "FlashFace") {
            // (Cut to the face's rectangle: 0x272C60's clip.)
            const Rect own = menu::cut(clip, bounds(at));
            if (own.w > 0 && own.h > 0) {
                set_clip(own);
                flash(live, at, face);
                set_clip(clip);
            }
        }
        else if (draw_live) draw_live(live, project_, page_, at, to_screen(bounds(at)), face);

        Rect inner = clip;
        const bool clips = w.has("clip") ? w.number("clip") != 0 : menu::clips_by_default(t);
        if (clips) {
            inner = menu::cut(clip, bounds(at));
            if (inner.w <= 0 || inner.h <= 0) return;
            set_clip(inner);
        }
        const bool open_menu = t == "OptionMenu" && w.number("is_popuped") != 0;
        for (const auto& c : live.children) {
            if (open_menu && c->name == "popup") {
                over_.push_back({c.get(), at, opacity, state, m_});
                continue;
            }
            widget(*c, at, opacity, inner, state);
        }
        if (clips) set_clip(clip);
    }

    struct Over {
        const LiveWidget* live;
        Rect parent;
        float opacity;
        int state;
        Affine m;
    };
    std::vector<Over> over_;
    int forced_ = 0; // the bits every widget is drawn in (the viewer's)
    struct Tip {
        std::string text;
        float w = 0;
        menu::ToolTipScroll scroll;
        u64 stepped = 0, seen = 0;
    };
    std::map<const void*, Tip> tips_; // by the live tool tip face
    menu::Project* project_ = nullptr;
    SDL_Renderer* r_;
    std::vector<gt3r::text::FontFile> files_;
    std::map<std::string, const gt3r::text::LatinFont*> fonts_;
    std::map<std::string, menu::FontSpacing> spacing_;
    std::array<menu::TextColor, 4> button_colors_{};
    struct Cached {
        SDL_Texture* texture = nullptr;
        u64 used = 0; // the menus' picture it was last drawn in
    };
    std::map<u64, Cached> textures_; // by image_key; one not drawn for ten seconds is let go
    std::map<std::pair<const gt3r::text::LatinFont*, int>, SDL_Texture*> atlases_;
    std::map<std::string, std::optional<menu::KanjiFont>> kanji_fonts_; // by language
    const menu::KanjiFont* kanji_ = nullptr;                           // the drawn project's
    struct KanjiAtlas {
        SDL_Texture* texture = nullptr;
        std::map<std::pair<size_t, u32>, int> slots; // (palette, code point) to a cell's place
    };
    std::map<const menu::KanjiFace*, KanjiAtlas> kanji_atlases_;
    float ox_ = 0, oy_ = 0, scale_ = 1;
    Affine m_; // the transforms of the widgets being drawn
    gt3r::Vfs* vfs_ = nullptr;
    struct Flash {
        bool tried = false, ok = false;
        menu::FlashMovie movie;
        std::vector<SDL_Texture*> textures;
    };
    std::map<std::string, Flash> flashes_;
    struct Clock {
        std::string path;
        int restarts = 0;
        u64 start = 0;
        float from = 0;
    };
    std::map<const LiveWidget*, Clock> clocks_;
    std::map<std::string, std::unique_ptr<gt3r::Tex1::Image>> disk_images_;
    std::set<std::string> tried_;
    std::string page_;
};

} // namespace gt4r
