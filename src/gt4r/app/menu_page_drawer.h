#pragma once

// Draws a menu page: its pictures, frames, colours and text as the layout and the scripts set
// them (docs/formats/menu.md, "Drawing"). Faces with content of their own (movies, 3D) are
// drawn by `draw_live`, called in their place in the page's order.

#include "gt4r/app/app.h"

#include "base/log.h"
#include "formats/text/charset.h"
#include "formats/text/text_layout.h"
#include "gt4r/menu/font.h"
#include "gt4r/menu/project.h"
#include "gt4r/menu/runtime.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace gt4r {

using menu::Rect;
using menu::Rgba;
using menu::Widget;

using menu::kActive;
using menu::kFocus;
using menu::kPrelight;
using menu::kInsensitive;
using menu::LiveWidget;

inline constexpr int kFocusBits = kFocus | kPrelight; // a focused widget has both

// Draws a page into a rectangle of the screen.
class PageDrawer {
public:
    explicit PageDrawer(App& app) : r_(app.renderer()) {
        for (const char* path : {"font/system.fnt", "font/menu.fnt"}) {
            const std::optional<Bytes> file = app.vfs()->read(path);
            if (!file) continue;
            try {
                files_.push_back(menu::read_fonts(*file));
            } catch (const std::exception& e) {
                LOG_WARN("%s: %s", path, e.what());
            }
        }
        for (const gt3r::text::FontFile& f : files_)
            for (const gt3r::text::LatinFont& font : f.fonts) fonts_.emplace(font.name, &font);
    }
    ~PageDrawer() {
        for (auto& [image, t] : textures_) SDL_DestroyTexture(t);
        for (auto& [font, t] : atlases_) SDL_DestroyTexture(t);
    }

    int widgets_drawn = 0;
    // A face whose content is not the layout's pictures and text (a movie, a 3D view): drawn by
    // this in its place in the page (`at`: the page's 640 x 480; `dst`: on the screen).
    // `project`, `page`: where its pictures are.
    std::function<void(const LiveWidget& face, menu::Project* project, const std::string& page, const Rect& at, const SDL_FRect& dst, float opacity)> draw_live;

    // A text's size as its face draws it (for the scripts' getTextSize).
    void measure(const LiveWidget& face, float& w, float& h) {
        const Widget& d = face.data;
        std::string s = d.text("text");
        if (s.empty()) s = d.text("value");
        const gt3r::text::LatinFont* font = font_of(d);
        w = h = 0;
        if (!font || s.empty()) return;
        gt3r::text::LayoutOptions opt;
        opt.scale_x = d.number("scale_x", 1), opt.scale_y = d.number("scale_y", 1);
        // A packed face takes the size of its text; any other breaks its lines at its width.
        if (const std::optional<Rect> g = d.rect("geometry"); g && g->w > 0 && d.number("multiline") != 0 && d.number("packing") == 0) s = wrapped(s, *font, opt, g->w);
        const gt3r::text::TextLayout lay = gt3r::text::layout_text(glyphs_of(s, *font), *font, nullptr, 0, 0, opt);
        w = lay.width, h = lay.height;
    }

    // The page's 640 x 480 into (x, y) at `scale`; `state`: the bits every widget is drawn in.
    // `project`: where the page's pictures and text are.
    void draw(menu::Project& project, const LiveWidget& page, float x, float y, float scale, int state) {
        project_ = &project;
        ox_ = x, oy_ = y, scale_ = scale, page_ = page.name, widgets_drawn = 0;
        const SDL_Rect whole = to_clip({0, 0, 640, 480});
        SDL_SetRenderClipRect(r_, &whole);
        over_.clear();
        widget(page, {0, 0, 640, 480}, 1.0f, whole, state);
        // An option menu's open list lies over everything else of the page.
        const std::vector<Over> over = std::move(over_);
        over_.clear();
        for (const Over& o : over) {
            SDL_SetRenderClipRect(r_, &whole);
            widget(*o.live, o.parent, o.opacity, whole, o.state);
        }
        SDL_SetRenderClipRect(r_, nullptr);
    }

    // One widget by itself (the cursor), its rectangle in the page's 640 x 480.
    void draw_one(menu::Project& project, const std::string& page, const LiveWidget& w, float x, float y, float scale) {
        project_ = &project;
        ox_ = x, oy_ = y, scale_ = scale, page_ = page;
        const SDL_Rect whole = to_clip({0, 0, 640, 480});
        SDL_SetRenderClipRect(r_, &whole);
        widget(w, {0, 0, 640, 480}, 1.0f, whole, 0);
        SDL_SetRenderClipRect(r_, nullptr);
    }

private:
    SDL_FRect to_screen(const Rect& r) const { return {ox_ + r.x * scale_, oy_ + r.y * scale_, r.w * scale_, r.h * scale_}; }
    SDL_Rect to_clip(const Rect& r) const {
        const SDL_FRect f = to_screen(r);
        return {int(std::floor(f.x)), int(std::floor(f.y)), int(std::ceil(f.x + f.w) - std::floor(f.x)), int(std::ceil(f.y + f.h) - std::floor(f.y))};
    }

    SDL_Texture* texture(const gt3r::Tex1::Image* image) {
        if (!image || !image->width || !image->height) return nullptr;
        SDL_Texture*& t = textures_[image];
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

    void image(const std::string& path, const Rect& at, Rgba tint, float opacity, bool add) {
        const gt3r::Tex1::Image* img = path.empty() ? nullptr : project_->image(page_, path);
        SDL_Texture* t = texture(img);
        if (!t) return;
        SDL_SetTextureColorMod(t, tint.r, tint.g, tint.b);
        SDL_SetTextureAlphaMod(t, u8(std::clamp(tint.a * opacity, 0.0f, 255.0f)));
        SDL_SetTextureBlendMode(t, add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
        const SDL_FRect dst = to_screen(at);
        SDL_RenderTexture(r_, t, nullptr, &dst);
    }
    SDL_FPoint image_size(const std::string& path) {
        const gt3r::Tex1::Image* img = path.empty() ? nullptr : project_->image(page_, path);
        return img ? SDL_FPoint{float(img->width), float(img->height)} : SDL_FPoint{0, 0};
    }

    // One colour, or four for the corners clockwise from the top left.
    void fill(const Rect& at, const std::vector<Rgba>& colors, float opacity, bool add) {
        if (colors.empty()) return;
        const SDL_FRect d = to_screen(at);
        const float xs[4] = {d.x, d.x + d.w, d.x + d.w, d.x}, ys[4] = {d.y, d.y, d.y + d.h, d.y + d.h};
        SDL_Vertex v[4];
        for (int i = 0; i < 4; ++i) {
            const Rgba c = colors[colors.size() >= 4 ? size_t(i) : 0];
            v[i].position = {xs[i], ys[i]};
            v[i].color = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f * opacity};
            v[i].tex_coord = {0, 0};
        }
        const int index[6] = {0, 1, 2, 0, 2, 3};
        SDL_SetRenderDrawBlendMode(r_, add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
        SDL_RenderGeometry(r_, nullptr, v, 4, index, 6);
    }

    // A frame of nine pictures: the corners at their own size, the sides stretched between
    // them, the middle over what is left.
    void frame(const Widget& w, const Rect& at, Rgba tint, float opacity) {
        const std::string tl = w.text("tl_image_path"), tr = w.text("tr_image_path"), bl = w.text("bl_image_path"), br = w.text("br_image_path");
        const SDL_FPoint stl = image_size(tl), str = image_size(tr), sbl = image_size(bl), sbr = image_size(br);
        const float left = std::max(stl.x, sbl.x), right = std::max(str.x, sbr.x), top = std::max(stl.y, str.y), bottom = std::max(sbl.y, sbr.y);
        const float midw = std::max(at.w - left - right, 0.0f), midh = std::max(at.h - top - bottom, 0.0f);
        image(w.text("image_path"), {at.x + left, at.y + top, midw, midh}, tint, opacity, false);
        image(w.text("t_image_path"), {at.x + left, at.y, midw, top}, tint, opacity, false);
        image(w.text("b_image_path"), {at.x + left, at.y + at.h - bottom, midw, bottom}, tint, opacity, false);
        image(w.text("l_image_path"), {at.x, at.y + top, left, midh}, tint, opacity, false);
        image(w.text("r_image_path"), {at.x + at.w - right, at.y + top, right, midh}, tint, opacity, false);
        image(tl, {at.x, at.y, stl.x, stl.y}, tint, opacity, false);
        image(tr, {at.x + at.w - str.x, at.y, str.x, str.y}, tint, opacity, false);
        image(bl, {at.x, at.y + at.h - sbl.y, sbl.x, sbl.y}, tint, opacity, false);
        image(br, {at.x + at.w - sbr.x, at.y + at.h - sbr.y, sbr.x, sbr.y}, tint, opacity, false);
    }

    // "hm_16", or with a palette "hm_16#1"; a name the files do not have: the menus' body font.
    const gt3r::text::LatinFont* font_of(const Widget& w, gt3r::text::FontVariant* variant = nullptr) const {
        std::string name = w.text("font", "hm_16");
        if (const size_t hash = name.find('#'); hash != std::string::npos) {
            if (variant) *variant = name.compare(hash, 2, "#0") == 0 ? gt3r::text::FontVariant::Outlined : gt3r::text::FontVariant::Plain;
            name.resize(hash);
        }
        auto it = fonts_.find(name);
        if (it == fonts_.end()) it = fonts_.find("hm_16");
        return it == fonts_.end() ? nullptr : it->second;
    }
    static std::vector<gt3r::text::GlyphRef> glyphs_of(const std::string& utf8, const gt3r::text::LatinFont& font) {
        std::u32string wide; // the text's lines end in "\r\n"
        for (char32_t c : gt3r::text::utf8_decode(utf8))
            if (c != U'\r') wide += c;
        return gt3r::text::map_unicode(wide, font, nullptr, nullptr);
    }
    float width_of(const std::string& line, const gt3r::text::LatinFont& font, const gt3r::text::LayoutOptions& opt) const {
        gt3r::text::LayoutOptions plain = opt;
        plain.align = gt3r::text::HAlign::Left, plain.valign = gt3r::text::VAlign::Top;
        return gt3r::text::layout_text(glyphs_of(line, font), font, nullptr, 0, 0, plain).width;
    }
    // A text of several lines is broken at spaces where a line would pass the face's width.
    std::string wrapped(const std::string& s, const gt3r::text::LatinFont& font, const gt3r::text::LayoutOptions& opt, float width) const {
        std::string out, line;
        auto flush = [&] { out += line, line.clear(); };
        size_t at = 0;
        while (at <= s.size()) {
            const size_t end = std::min(s.find_first_of(" \n", at), s.size());
            const std::string word = s.substr(at, end - at);
            const std::string tried = line.empty() ? word : line + " " + word;
            if (!line.empty() && width_of(tried, font, opt) > width) flush(), out += '\n', line = word;
            else line = tried;
            if (end < s.size() && s[end] == '\n') flush(), out += '\n';
            at = end + 1;
        }
        flush();
        return out;
    }

    void text(const Widget& w, const Rect& at, float opacity) {
        std::string s = w.text("text");
        if (s.empty()) s = w.text("value"); // a tool tip's, an input's
        if (s.empty() && w.has("key")) s = project_->translate(w.has("key_page") ? w.text("key_page") : page_, w.text("key"));
        if (s.empty()) return;
        // A text's marks: of the pad's buttons ("%BG") and of a colour ("%Cfff"), left out;
        // "%%" is a percent sign (❓ judged from the scripts, which end a percentage with it).
        for (size_t mark = s.find('%'); mark != std::string::npos && mark + 1 < s.size(); mark = s.find('%', mark)) {
            const char c = s[mark + 1];
            if (c == 'B') s.erase(mark, std::min<size_t>(3, s.size() - mark));
            else if (c == 'C') s.erase(mark, std::min<size_t>(5, s.size() - mark));
            else if (c == '%') s.erase(mark, 1), ++mark;
            else ++mark;
        }
        gt3r::text::FontVariant variant = gt3r::text::FontVariant::Plain;
        const gt3r::text::LatinFont* found = font_of(w, &variant);
        if (!found) return;
        const gt3r::text::LatinFont& font = *found;

        gt3r::text::LayoutOptions opt;
        opt.scale_x = w.number("scale_x", 1), opt.scale_y = w.number("scale_y", 1);
        // adjustScale(): drawn narrower when it would pass the face's width; abbreviate(): cut
        // there and ended in dots.
        if (at.w > 0 && s.find('\n') == std::string::npos) {
            const float width = width_of(s, font, opt);
            if (width > at.w && w.number("adjust_scale") != 0) opt.scale_x *= at.w / width;
            else if (width > at.w && w.number("abbreviate") != 0) {
                while (s.size() > 1 && width_of(s + "...", font, opt) > at.w) s.pop_back();
                s += "...";
            }
        }
        if (w.number("multiline") != 0 && w.number("packing") == 0 && at.w > 0) s = wrapped(s, font, opt, at.w);
        // Where in its rectangle: 0..8, rows from the top, columns from the left; the middle (4)
        // when not given.
        const int align = w.has("align") ? int(w.number("align")) : 4;
        const int col = align % 3, row = align / 3;
        opt.align = col == 0 ? gt3r::text::HAlign::Left : col == 1 ? gt3r::text::HAlign::Center : gt3r::text::HAlign::Right;
        opt.valign = row == 0 ? gt3r::text::VAlign::Top : row == 1 ? gt3r::text::VAlign::Middle : gt3r::text::VAlign::Bottom;
        const float x = col == 0 ? at.x : col == 1 ? at.x + at.w / 2 : at.x + at.w;
        const float y = row == 0 ? at.y : row == 1 ? at.y + at.h / 2 : at.y + at.h;
        const gt3r::text::TextLayout lay = gt3r::text::layout_text(glyphs_of(s, font), font, nullptr, x, y, opt);

        SDL_Texture*& atlas = atlases_[{&font, int(variant)}];
        if (!atlas) {
            const gt3r::Tex1::Image& img = font.atlas[size_t(variant)];
            if (!img.width || !img.height) return;
            atlas = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(img.width), int(img.height));
            if (!atlas) return;
            SDL_UpdateTexture(atlas, nullptr, img.rgba.data(), int(img.width * 4));
            SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(atlas, SDL_SCALEMODE_LINEAR);
        }
        auto pass = [&](Rgba c, float dx, float dy) {
            SDL_SetTextureColorMod(atlas, c.r, c.g, c.b);
            SDL_SetTextureAlphaMod(atlas, u8(std::clamp(c.a * opacity, 0.0f, 255.0f)));
            for (const gt3r::text::GlyphQuad& q : lay.quads) {
                if (q.kanji || !q.sw || !q.sh) continue;
                const SDL_FRect src{float(q.sx), float(q.sy), float(q.sw), float(q.sh)};
                const SDL_FRect dst = to_screen({q.x + dx, q.y + dy, q.w, q.h});
                SDL_RenderTexture(r_, atlas, &src, &dst);
            }
        };
        if (const float shadow = w.number("shadow"); shadow > 0) pass(w.color("shadow_color").value_or(Rgba{0, 0, 0, 255}), shadow, shadow);
        pass(w.color("text_color").value_or(Rgba{255, 255, 255, 255}), 0, 0);
    }

    // A widget is drawn in its own state and its parents' (a focused button's faces light up).
    void widget(const LiveWidget& live, const Rect& parent, float opacity, const SDL_Rect& clip, int state) {
        const Widget& w = live.data;
        state |= live.state;
        if (w.has("visible") && w.number("visible") == 0) return;
        if (w.has("visible_condition") || w.has("visible_compare")) {
            const int want = int(w.number("visible_condition")), mask = int(w.number("visible_compare", float(want)));
            if ((state & mask) != want) return;
        }
        Rect at = parent;
        if (const std::optional<Rect> g = w.rect("geometry")) at = {parent.x + g->x, parent.y + g->y, g->w, g->h};
        opacity *= std::clamp(w.number("opacity", 1), 0.0f, 1.0f);
        if (opacity <= 0) return;
        // A widget made larger (a move actor's, a list's chosen row) grows about its middle.
        if (const float sx = w.number("sx", 1), sy = w.number("sy", 1); sx != 1 || sy != 1)
            at = {at.x - at.w * (sx - 1) / 2, at.y - at.h * (sy - 1) / 2, at.w * sx, at.h * sy};
        const float face = opacity * std::clamp(w.number("alpha", 1), 0.0f, 1.0f); // the face's own, not its children's
        ++widgets_drawn;
        const std::vector<Rgba> colors = w.colors("color");
        const Rgba tint = colors.empty() ? Rgba{} : colors[0];
        const bool add = w.number("blend") != 0;
        const std::string& t = w.type;
        if (t == "ColorFace") fill(at, colors, face, add);
        else if (t == "ImageFace" || t == "ProgressFace") image(w.text("image_path"), at, tint, face, add);
        else if (t == "FrameImageFace") frame(w, at, tint, face);
        else if (t == "TextFace" || t == "InputTextFace" || t == "InputNumberFace" || t == "ToolTipFace" || t == "TextBoxFace") text(w, at, face);
        else if (t == "ColorChipFace") {
            // A car's paint: its colour under the chip's picture.
            if (w.has("chip_r")) fill(at, {Rgba{u8(w.number("chip_r")), u8(w.number("chip_g")), u8(w.number("chip_b")), 255}}, face, false);
            image(w.text("image_path0"), at, tint, face, false);
        } else if (draw_live) draw_live(live, project_, page_, at, to_screen(at), face);

        SDL_Rect inner = clip;
        if (w.number("clip") != 0) {
            const SDL_Rect own = to_clip(at);
            if (!SDL_GetRectIntersection(&clip, &own, &inner)) return;
            SDL_SetRenderClipRect(r_, &inner);
        }
        const bool open_menu = t == "OptionMenu" && w.number("is_popuped") != 0;
        for (const auto& c : live.children) {
            if (open_menu && c->name == "popup") {
                over_.push_back({c.get(), at, opacity, state & ~kFocusBits});
                continue;
            }
            widget(*c, at, opacity, inner, state);
        }
        if (w.number("clip") != 0) SDL_SetRenderClipRect(r_, &clip);
    }

    struct Over {
        const LiveWidget* live;
        Rect parent;
        float opacity;
        int state;
    };
    std::vector<Over> over_;
    menu::Project* project_ = nullptr;
    SDL_Renderer* r_;
    std::vector<gt3r::text::FontFile> files_;
    std::map<std::string, const gt3r::text::LatinFont*> fonts_;
    std::map<const gt3r::Tex1::Image*, SDL_Texture*> textures_;
    std::map<std::pair<const gt3r::text::LatinFont*, int>, SDL_Texture*> atlases_;
    float ox_ = 0, oy_ = 0, scale_ = 1;
    std::string page_;
};

} // namespace gt4r
