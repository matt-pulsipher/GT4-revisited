// The menu pages as the game's files describe them: a project's layout drawn with its own
// pictures, text and fonts. Still pictures: the scripts that fill in and move the pages do
// not run here yet. How the properties are drawn is in docs/formats/menu.md.

#include "gt4r/app/app.h"

#include "base/log.h"
#include "formats/text/charset.h"
#include "formats/text/text_layout.h"
#include "gt4r/menu/font.h"
#include "gt4r/menu/project.h"
#include "gt4r/menu/runtime.h"

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
    PageDrawer(App& app, menu::Project& project) : project_(project), r_(app.renderer()) {
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

    // The page's 640 x 480 into (x, y) at `scale`; `state`: the bits every widget is drawn in.
    void draw(const LiveWidget& page, float x, float y, float scale, int state) {
        ox_ = x, oy_ = y, scale_ = scale, page_ = page.name, widgets_drawn = 0;
        const SDL_Rect whole = to_clip({0, 0, 640, 480});
        SDL_SetRenderClipRect(r_, &whole);
        widget(page, {0, 0, 640, 480}, 1.0f, whole, state);
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
        const gt3r::Tex1::Image* img = path.empty() ? nullptr : project_.image(page_, path);
        SDL_Texture* t = texture(img);
        if (!t) return;
        SDL_SetTextureColorMod(t, tint.r, tint.g, tint.b);
        SDL_SetTextureAlphaMod(t, u8(std::clamp(tint.a * opacity, 0.0f, 255.0f)));
        SDL_SetTextureBlendMode(t, add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
        const SDL_FRect dst = to_screen(at);
        SDL_RenderTexture(r_, t, nullptr, &dst);
    }
    SDL_FPoint image_size(const std::string& path) {
        const gt3r::Tex1::Image* img = path.empty() ? nullptr : project_.image(page_, path);
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

    void text(const Widget& w, const Rect& at, float opacity) {
        std::string s = w.text("text");
        if (s.empty() && w.has("key")) s = project_.translate(page_, w.text("key"));
        if (s.empty()) return;
        // "hm_16", or with a palette "hm_16#1"; a name the files do not have: the menus' body font.
        std::string name = w.text("font", "hm_16");
        gt3r::text::FontVariant variant = gt3r::text::FontVariant::Plain;
        if (const size_t hash = name.find('#'); hash != std::string::npos) {
            variant = name.compare(hash, 2, "#0") == 0 ? gt3r::text::FontVariant::Outlined : gt3r::text::FontVariant::Plain;
            name.resize(hash);
        }
        auto it = fonts_.find(name);
        if (it == fonts_.end()) it = fonts_.find("hm_16");
        if (it == fonts_.end()) return;
        const gt3r::text::LatinFont& font = *it->second;

        gt3r::text::LayoutOptions opt;
        opt.scale_x = w.number("scale_x", 1), opt.scale_y = w.number("scale_y", 1);
        // Where in its rectangle: 0..8, rows from the top, columns from the left; the middle (4)
        // when not given.
        const int align = w.has("align") ? int(w.number("align")) : 4;
        const int col = align % 3, row = align / 3;
        opt.align = col == 0 ? gt3r::text::HAlign::Left : col == 1 ? gt3r::text::HAlign::Center : gt3r::text::HAlign::Right;
        opt.valign = row == 0 ? gt3r::text::VAlign::Top : row == 1 ? gt3r::text::VAlign::Middle : gt3r::text::VAlign::Bottom;
        const float x = col == 0 ? at.x : col == 1 ? at.x + at.w / 2 : at.x + at.w;
        const float y = row == 0 ? at.y : row == 1 ? at.y + at.h / 2 : at.y + at.h;
        std::u32string wide; // the text's lines end in "\r\n"
        for (char32_t c : gt3r::text::utf8_decode(s))
            if (c != U'\r') wide += c;
        const std::vector<gt3r::text::GlyphRef> glyphs = gt3r::text::map_unicode(wide, font, nullptr, nullptr);
        const gt3r::text::TextLayout lay = gt3r::text::layout_text(glyphs, font, nullptr, x, y, opt);

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
        opacity *= w.number("opacity", 1) * w.number("alpha", 1);
        ++widgets_drawn;
        const std::vector<Rgba> colors = w.colors("color");
        const Rgba tint = colors.empty() ? Rgba{} : colors[0];
        const bool add = w.number("blend") != 0;
        const std::string& t = w.type;
        if (t == "ColorFace") fill(at, colors, opacity, add);
        else if (t == "ImageFace" || t == "ProgressFace") image(w.text("image_path"), at, tint, opacity, add);
        else if (t == "FrameImageFace") frame(w, at, tint, opacity);
        else if (t == "TextFace" || t == "InputTextFace" || t == "InputNumberFace" || t == "ToolTipFace" || t == "TextBoxFace") text(w, at, opacity);
        else if (t == "ColorChipFace") image(w.text("image_path0"), at, tint, opacity, false);

        SDL_Rect inner = clip;
        if (w.number("clip") != 0) {
            const SDL_Rect own = to_clip(at);
            if (!SDL_GetRectIntersection(&clip, &own, &inner)) return;
            SDL_SetRenderClipRect(r_, &inner);
        }
        for (const auto& c : live.children) widget(*c, at, opacity, inner, state);
        if (w.number("clip") != 0) SDL_SetRenderClipRect(r_, &clip);
    }

    menu::Project& project_;
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
            }
        }
    }

    void update(App& app, double) override {
        const MenuInput& m = app.menu();
        if (!start_project_.empty()) {
            for (size_t i = 0; i < projects_.size(); ++i)
                if (projects_[i].name == start_project_) project_sel_ = int(i);
            start_project_.clear();
            open(app);
            for (size_t i = 0; i < pages_.size(); ++i)
                if (pages_[i]->name == start_page_) page_sel_ = int(i);
            if (start_run_ && project_) run(app);
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
            return;
        }
        const int pages = int(pages_.size());
        if (pages) page_sel_ = std::clamp(page_sel_ + m.vertical() + (m.pressed(PadButton::L1) ? -kRows : 0), 0, pages - 1);
        if (m.pressed(PadButton::Cross)) full_ = !full_;
        if (m.pressed(PadButton::R1)) run(app);
    }

    void render(App&, Ui& ui) override {
        if (runtime_) return render_running(ui);
        const char* states = state_ == kFocusBits ? "focused" : state_ == kActive ? "active" : state_ == kInsensitive ? "unavailable" : "at rest";
        if (project_ && full_ && page_sel_ < int(pages_.size())) {
            // The page over the whole height (4:3).
            const float scale = float(Ui::kHeight) / 480.0f;
            ui.fill(0, 0, Ui::kWidth, Ui::kHeight, {0, 0, 0});
            drawer_->draw(*pages_[size_t(page_sel_)], (Ui::kWidth - 640 * scale) / 2, 0, scale, state_);
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
            drawer_->draw(page, x, y, scale, state_);
            ui.text(px + 6, py + ph - 12, std::to_string(drawer_->widgets_drawn) + " widgets drawn " + states, colors::kDim);
        } else if (!project_) {
            ui.text(px + 6, py + 6, "The game's menus, a project each: its pages drawn from the", colors::kDim);
            ui.text(px + 6, py + 18, "disc's layout, pictures, text and fonts.", colors::kDim);
        }
        ui.footer(project_ ? "{Up/Down}: page  {Cross}: whole screen  {Square}: state  {R1}: run its script  {Triangle}: back"
                           : "{Up/Down}: select  {Cross}: open  {Triangle}: back");
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
            drawer_ = std::make_unique<PageDrawer>(app, *project_);
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

    // The project's script on its widgets: the game's own menu, as far as the program's side
    // of it is written.
    void run(App& app) {
        const Entry e = projects_[size_t(project_sel_)];
        try {
            runtime_ = std::make_unique<menu::Runtime>([&app](const std::string& path) { return app.vfs()->read(path); });
            start(app, e.name);
        } catch (const std::exception& ex) {
            runtime_.reset(), live_drawer_.reset();
            error_ = ex.what();
            LOG_WARN("Menu viewer: %s", ex.what());
        }
    }
    void start(App& app, const std::string& name) {
        std::string language = "US";
        for (const Entry& e : projects_)
            if (e.name == name) language = e.language;
        live_drawer_.reset();
        runtime_->start_project(name, language);
        live_drawer_ = std::make_unique<PageDrawer>(app, *runtime_->project());
    }

    void update_running(App& app, const MenuInput& m) {
        if (m.pressed(PadButton::L1)) {
            live_drawer_.reset(), runtime_.reset();
            return;
        }
        try {
            if (m.vertical()) runtime_->key(m.vertical() < 0 ? menu::Key::Up : menu::Key::Down);
            if (m.horizontal()) runtime_->key(m.horizontal() < 0 ? menu::Key::Left : menu::Key::Right);
            if (m.pressed(PadButton::Cross)) runtime_->key(menu::Key::Accept);
            if (m.pressed(PadButton::Triangle)) runtime_->key(menu::Key::Cancel);
            runtime_->update();
            // A script that ended its project names the next one.
            if (!runtime_->current_page()) {
                const std::string next = runtime_->wanted_project();
                const bool known = std::any_of(projects_.begin(), projects_.end(), [&](const Entry& e) { return e.name == next; });
                if (known && ++restarts_ < 8) start(app, next);
            } else restarts_ = 0;
        } catch (const std::exception& ex) {
            runtime_->note(std::string("stopped: ") + ex.what());
        }
    }

    void render_running(Ui& ui) {
        const float scale = float(Ui::kHeight) / 480.0f, x = (Ui::kWidth - 640 * scale) / 2;
        ui.fill(0, 0, Ui::kWidth, Ui::kHeight, {0, 0, 0});
        if (live_drawer_)
            for (const auto& page : runtime_->open_pages()) live_drawer_->draw(*page, x, 0, scale, 0);
        // What the scripts did, beside the page.
        const std::deque<std::string>& log = runtime_->log();
        const size_t lines = std::min<size_t>(log.size(), 26);
        for (size_t i = 0; i < lines; ++i) ui.text(2, 14 + float(i) * 12, Ui::clip(log[log.size() - lines + i], x - 4), colors::kDim);
        ui.text(2, 2, runtime_->project() ? runtime_->project()->name() : std::string("-"), colors::kAccent);
        ui.text(x + 640 * scale + 2, 2, "L1: stop", colors::kDim);
    }

    std::vector<Entry> projects_;
    std::unique_ptr<menu::Project> project_;
    std::unique_ptr<PageDrawer> drawer_;
    std::vector<std::shared_ptr<LiveWidget>> pages_;
    std::unique_ptr<menu::Runtime> runtime_;
    std::unique_ptr<PageDrawer> live_drawer_;
    int project_sel_ = 0, page_sel_ = 0, state_ = 0, restarts_ = 0;
    bool full_ = false, start_run_ = false;
    std::string error_, start_project_, start_page_;
};

} // namespace

std::unique_ptr<Screen> make_menu_viewer(App& app) { return std::make_unique<MenuViewer>(app); }

} // namespace gt4r
