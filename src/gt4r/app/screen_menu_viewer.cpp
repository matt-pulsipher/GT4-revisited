// The menu pages as the game's files describe them: a project's layout drawn with its own
// pictures, text and fonts; and the menus running (menu::Runtime): the game's scripts on those
// pages, from the boot project on. How the properties are drawn is in docs/formats/menu.md.

#include "gt4r/app/app.h"

#include "base/log.h"
#include "formats/text/charset.h"
#include "formats/text/text_layout.h"
#include "gt4r/menu/font.h"
#include "gt4r/menu/project.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/menu/runtime_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <map>

namespace gt4r {

namespace {

using menu::Rect;
using menu::Rgba;
using menu::Widget;

using menu::kActive;
using menu::kFocus;
using menu::kPrelight;
using menu::kInsensitive;
using menu::LiveWidget;

constexpr int kFocusBits = kFocus | kPrelight; // a focused widget has both

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
        widget(page, {0, 0, 640, 480}, 1.0f, whole, state);
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
        if (s.empty() && w.has("key")) s = project_->translate(page_, w.text("key"));
        if (s.empty()) return;
        // The marks of the pad's buttons ("%BG") and of a colour ("%Cfff") are left out.
        for (size_t mark; (mark = s.find('%')) != std::string::npos && mark + 1 < s.size() && (s[mark + 1] == 'B' || s[mark + 1] == 'C');) {
            const size_t len = s[mark + 1] == 'B' ? 3 : 5;
            s.erase(mark, std::min(len, s.size() - mark));
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
        }

        SDL_Rect inner = clip;
        if (w.number("clip") != 0) {
            const SDL_Rect own = to_clip(at);
            if (!SDL_GetRectIntersection(&clip, &own, &inner)) return;
            SDL_SetRenderClipRect(r_, &inner);
        }
        for (const auto& c : live.children) widget(*c, at, opacity, inner, state);
        if (w.number("clip") != 0) SDL_SetRenderClipRect(r_, &clip);
    }

    menu::Project* project_ = nullptr;
    SDL_Renderer* r_;
    std::vector<gt3r::text::FontFile> files_;
    std::map<std::string, const gt3r::text::LatinFont*> fonts_;
    std::map<const gt3r::Tex1::Image*, SDL_Texture*> textures_;
    std::map<std::pair<const gt3r::text::LatinFont*, int>, SDL_Texture*> atlases_;
    float ox_ = 0, oy_ = 0, scale_ = 1;
    std::string page_;
};

class MenuViewer : public Screen {
public:
    explicit MenuViewer(App& app) {
        const auto list = [&app](const std::string& folder) { return app.vfs()->list_dir(folder); };
        for (const std::string& name : menu::list_projects(list)) {
            const std::vector<std::string> languages = menu::project_languages(list, name);
            projects_.push_back({name, languages.empty() ? std::string("US") : languages[0]});
        }
        // GT4R_MENU="project=boot,page=LanguageRoot,state=2,full=1,run=1": what a scripted run opens.
        // "boot=1": the menus run from the game's first project, as from power on; "card=none"
        // (no memory card), "card=saved" (one with a saved game) for that run.
        if (const char* env = std::getenv("GT4R_MENU")) {
            std::string spec = env;
            size_t at = 0;
            while (at < spec.size()) {
                const size_t end = std::min(spec.find(',', at), spec.size());
                const std::string item = spec.substr(at, end - at);
                at = end + 1;
                const size_t eq = item.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = item.substr(0, eq), value = item.substr(eq + 1);
                if (key == "project") start_project_ = value;
                else if (key == "page") start_page_ = value;
                else if (key == "state") state_ = std::atoi(value.c_str());
                else if (key == "full") full_ = value != "0";
                else if (key == "run") start_run_ = value != "0";
                else if (key == "boot") start_boot_ = value != "0";
                else if (key == "card") card_ = value;
            }
        }
    }

    void update(App& app, double) override {
        const MenuInput& m = app.menu();
        if (start_boot_) {
            start_boot_ = false, start_project_.clear();
            run(app, true);
        }
        if (!start_project_.empty()) {
            for (size_t i = 0; i < projects_.size(); ++i)
                if (projects_[i].name == start_project_) project_sel_ = int(i);
            start_project_.clear();
            open(app);
            for (size_t i = 0; i < pages_.size(); ++i)
                if (pages_[i]->name == start_page_) page_sel_ = int(i);
            if (start_run_ && project_) run(app, false);
        }
        if (runtime_) return update_running(app, m);
        if (m.pressed(PadButton::Triangle)) {
            if (full_) full_ = false;
            else if (project_) close();
            else app.pop();
            return;
        }
        if (m.pressed(PadButton::Square)) state_ = state_ == 0 ? kFocusBits : state_ == kFocusBits ? kActive : state_ == kActive ? kInsensitive : 0;
        if (!project_) {
            if (projects_.empty()) return;
            const int page = m.pressed(PadButton::L1) ? -kRows : m.pressed(PadButton::R1) ? kRows : 0;
            project_sel_ = std::clamp(project_sel_ + m.vertical() + page, 0, int(projects_.size()) - 1);
            if (m.pressed(PadButton::Cross)) open(app);
            if (m.pressed(PadButton::Start)) run(app, true);
            return;
        }
        const int pages = int(pages_.size());
        if (pages) page_sel_ = std::clamp(page_sel_ + m.vertical() + (m.pressed(PadButton::L1) ? -kRows : 0), 0, pages - 1);
        if (m.pressed(PadButton::Cross)) full_ = !full_;
        if (m.pressed(PadButton::R1)) run(app, false);
    }

    void render(App&, Ui& ui) override {
        if (runtime_) return render_running(ui);
        const char* states = state_ == kFocusBits ? "focused" : state_ == kActive ? "active" : state_ == kInsensitive ? "unavailable" : "at rest";
        if (project_ && full_ && page_sel_ < int(pages_.size())) {
            // The page over the whole height (4:3).
            const float scale = float(Ui::kHeight) / 480.0f;
            ui.fill(0, 0, Ui::kWidth, Ui::kHeight, {0, 0, 0});
            drawer_->draw(*project_, *pages_[size_t(page_sel_)], (Ui::kWidth - 640 * scale) / 2, 0, scale, state_);
            return;
        }
        ui.header("MENU PAGES", project_ ? project_->name() + "  (" + project_->module_name() + ", " + project_->language() + ")" : std::string());
        const int n = project_ ? int(pages_.size()) : int(projects_.size());
        const int sel = project_ ? page_sel_ : project_sel_;
        const int first = ui.list_window(n, sel, kRows);
        for (int i = 0; i < kRows && first + i < n; ++i) {
            const int idx = first + i;
            const float y = 26 + float(i) * 12;
            if (idx == sel) ui.fill(4, y - 2, 200, 12, colors::kHighlight);
            ui.text(8, y, Ui::clip(project_ ? pages_[size_t(idx)]->name : projects_[size_t(idx)].name, 190), colors::kText);
        }
        if (n > kRows) ui.text_right(200, Ui::kHeight - 30, std::to_string(sel + 1) + "/" + std::to_string(n), colors::kDim);
        const float px = 212, py = 26, pw = Ui::kWidth - px - 6, ph = Ui::kHeight - 50 - py;
        ui.fill(px, py, pw, ph, colors::kPanel);
        if (!error_.empty()) ui.text(px + 6, py + 6, Ui::clip(error_, pw - 12), colors::kBad);
        if (project_ && page_sel_ < n) {
            const LiveWidget& page = *pages_[size_t(page_sel_)];
            const float scale = std::min(pw / 640.0f, (ph - 14) / 480.0f);
            const float x = px + (pw - 640 * scale) / 2, y = py;
            ui.fill(x, y, 640 * scale, 480 * scale, {0, 0, 0});
            drawer_->draw(*project_, page, x, y, scale, state_);
            ui.text(px + 6, py + ph - 12, std::to_string(drawer_->widgets_drawn) + " widgets drawn " + states, colors::kDim);
        } else if (!project_) {
            ui.text(px + 6, py + 6, "The game's menus, a project each: its pages drawn from the", colors::kDim);
            ui.text(px + 6, py + 18, "disc's layout, pictures, text and fonts.", colors::kDim);
        }
        ui.footer(project_ ? "{Up/Down}: page  {Cross}: whole screen  {Square}: state  {R1}: run its script  {Triangle}: back"
                           : "{Up/Down}: select  {Cross}: open  {Start}: run the menus from boot  {Triangle}: back");
    }

private:
    static constexpr int kRows = 25;
    struct Entry {
        std::string name, language;
    };

    void open(App& app) {
        if (projects_.empty()) return;
        const Entry& e = projects_[size_t(project_sel_)];
        try {
            project_ = std::make_unique<menu::Project>([&app](const std::string& path) { return app.vfs()->read(path); }, e.name, e.language);
            drawer_ = std::make_unique<PageDrawer>(app);
            pages_.clear();
            for (const Widget& page : project_->pages()) pages_.push_back(LiveWidget::build(page, nullptr));
            page_sel_ = 0;
            error_.clear();
        } catch (const std::exception& ex) {
            close();
            error_ = ex.what();
            LOG_WARN("Menu viewer: %s", ex.what());
        }
    }
    void close() {
        for (auto& p : pages_) p->clear();
        pages_.clear(), drawer_.reset(), project_.reset(), error_.clear();
    }

    // The menus running: the game's scripts on its pages. `from_boot`: from the project the
    // game starts with; else the project selected, in the world the main script sets up.
    void run(App& app, bool from_boot) {
        try {
            menu::Runtime::Config config;
            gt3r::Vfs* vfs = app.vfs();
            config.list = [vfs](const std::string& folder) { return vfs->list_dir(folder); };
            Disc* disc = app.disc();
            if (disc) config.program = [disc] { return disc->read_program(); };
            config.data_dir = app.data_dir();
            while (!config.data_dir.empty() && (config.data_dir.back() == '/' || config.data_dir.back() == '\\')) config.data_dir.pop_back();
            if (!projects_.empty()) config.language = projects_[size_t(std::clamp(project_sel_, 0, int(projects_.size()) - 1))].language;
            runtime_ = std::make_unique<menu::Runtime>([vfs](const std::string& path) { return vfs->read(path); }, config);
            if (card_ == "none") runtime_->game().card.connected = false;
            else if (card_ == "saved") runtime_->game().card.has_game_data = true;
            live_drawer_ = std::make_unique<PageDrawer>(app);
            PageDrawer* drawer = live_drawer_.get();
            menu::text_measure().measure = [drawer](const LiveWidget& face, float& w, float& h) { drawer->measure(face, w, h); };
            runtime_->boot();
            race_.reset();
            runtime_->start_project(from_boot || projects_.empty() ? runtime_->first_project() : projects_[size_t(project_sel_)].name);
        } catch (const std::exception& ex) {
            stop();
            error_ = ex.what();
            LOG_WARN("Menu viewer: %s", ex.what());
        }
    }
    void stop() {
        menu::text_measure().measure = nullptr;
        live_drawer_.reset(), runtime_.reset(), race_.reset();
    }

    void update_running(App&, const MenuInput& m) {
        // Select and Start together leave the running menus.
        if (m.held(PadButton::Select) && m.pressed(PadButton::Start)) return stop();
        try {
            static const std::pair<PadButton, menu::Key> keys[] = {
                {PadButton::Cross, menu::Key::Accept}, {PadButton::Triangle, menu::Key::Cancel}, {PadButton::Start, menu::Key::Start},
                {PadButton::Select, menu::Key::Select}, {PadButton::L1, menu::Key::L1},         {PadButton::R1, menu::Key::R1},
                {PadButton::L2, menu::Key::L2},         {PadButton::R2, menu::Key::R2},         {PadButton::Square, menu::Key::Square},
            };
            if (m.vertical()) runtime_->key(m.vertical() < 0 ? menu::Key::Up : menu::Key::Down);
            if (m.horizontal()) runtime_->key(m.horizontal() < 0 ? menu::Key::Left : menu::Key::Right);
            for (const auto& [button, key] : keys)
                if (m.pressed(button)) runtime_->key(key);
            runtime_->update();
            // A script that ended its project names the next one, or asks for a race.
            if (runtime_->finished() && !race_) {
                race_ = runtime_->race_request();
                if (!race_ && !runtime_->advance()) runtime_->note("the menus ended and asked for nothing");
            }
        } catch (const std::exception& ex) {
            runtime_->note(std::string("stopped: ") + ex.what());
        }
    }

    void render_running(Ui& ui) {
        const float scale = float(Ui::kHeight) / 480.0f, x = (Ui::kWidth - 640 * scale) / 2;
        ui.fill(0, 0, Ui::kWidth, Ui::kHeight, {0, 0, 0});
        if (live_drawer_) {
            for (const auto& page : runtime_->open_pages())
                if (menu::Project* project = runtime_->project_of(*page)) live_drawer_->draw(*project, *page, x, 0, scale, 0);
            draw_cursor(x, scale);
            // The transition's colour over the pages.
            if (const float cover = runtime_->fade_cover(); cover > 0) ui.fill(x, 0, 640 * scale, 480 * scale, Color{0, 0, 0, u8(std::clamp(cover, 0.0f, 1.0f) * 255.0f)});
        }
        // What the scripts did, beside the page.
        const std::deque<std::string>& log = runtime_->log();
        const size_t lines = std::min<size_t>(log.size(), 26);
        for (size_t i = 0; i < lines; ++i) ui.text(2, 14 + float(i) * 12, Ui::clip(log[log.size() - lines + i], x - 4), colors::kDim);
        ui.text(2, 2, runtime_->project() ? runtime_->project()->name() : std::string("-"), colors::kAccent);
        ui.text(x + 640 * scale + 2, 2, "Select+Start:", colors::kDim);
        ui.text(x + 640 * scale + 2, 14, "stop", colors::kDim);
        if (race_) {
            // The menus' work is done: this is what a race would be started from.
            const float px = x + 60 * scale, py = 150 * scale, pw = 520 * scale;
            ui.fill(px, py, pw, 84, colors::kPanel);
            ui.text(px + 8, py + 6, "The menus ask for a race: " + race_->sequence, colors::kAccent);
            ui.text(px + 8, py + 22, "Course " + race_->course + (race_->race.empty() ? std::string() : "   event " + race_->race), colors::kText);
            ui.text(px + 8, py + 36, "Car " + race_->car + "   paint " + std::to_string(race_->paint) + "   " + std::to_string(race_->max_entry) + " cars", colors::kText);
            ui.text(px + 8, py + 50, std::string(race_->manual ? "Manual" : "Automatic") + " gears   difficulty " + race_->difficulty, colors::kText);
            ui.text(px + 8, py + 66, "Races are not part of this build yet.", colors::kDim);
        }
    }

    // The menus' pointer: the common project's cursor picture, its tip on the focused widget.
    // ❓ Where on the widget the program puts it (the layouts' cursor_align) is by eye: the
    // widget's right half, at mid height.
    void draw_cursor(float x, float scale) {
        if (!runtime_->cursor_visible() || !runtime_->common_project()) return;
        LiveWidget* focus = runtime_->focused();
        const std::shared_ptr<LiveWidget> root = runtime_->common_page("CursorRoot");
        const std::shared_ptr<LiveWidget> cursor = root ? root->child("Cursor") : nullptr;
        if (!focus || !cursor || !focus->shown()) return;
        const Rect at = focus->screen_rect();
        const std::optional<Rect> size = cursor->data.rect("geometry");
        const float w = size ? size->w : 32, h = size ? size->h : 32;
        LiveWidget pointer;
        pointer.data = cursor->data;
        pointer.set_property("x", script::Value::number(at.x + at.w * 0.75f));
        pointer.set_property("y", script::Value::number(at.y + at.h * 0.5f));
        pointer.set_property("w", script::Value::number(w));
        pointer.set_property("h", script::Value::number(h));
        pointer.name = root->name;
        LiveWidget page;
        page.name = root->name, page.data.type = "RootWindow";
        live_drawer_->draw_one(*runtime_->common_project(), root->name, pointer, x, 0, scale);
    }

    std::vector<Entry> projects_;
    std::unique_ptr<menu::Project> project_;
    std::unique_ptr<PageDrawer> drawer_;
    std::vector<std::shared_ptr<LiveWidget>> pages_;
    std::unique_ptr<menu::Runtime> runtime_;
    std::unique_ptr<PageDrawer> live_drawer_;
    int project_sel_ = 0, page_sel_ = 0, state_ = 0;
    bool full_ = false, start_run_ = false, start_boot_ = false;
    std::string error_, start_project_, start_page_, card_;
    std::optional<menu::RaceRequest> race_; // what the running menus asked for at their end
};

} // namespace

std::unique_ptr<Screen> make_menu_viewer(App& app) { return std::make_unique<MenuViewer>(app); }

} // namespace gt4r
