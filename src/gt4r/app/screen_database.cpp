// The home screen's Car database: every car of the disc in a list (sorted by maker, name, power, price or year), and the chosen
// car's spec sheet, engine curves, tune shop parts and Gran Turismo mode's events it may enter (menu/car_database.h). Pad,
// keyboard, mouse and touch: up and down choose a car, left and right (L1 / R1 with the details in front) turn the tab, Cross
// moves into the details to scroll them, Square changes the order, Triangle goes back. A click or a tap chooses a car or a tab,
// the wheel or a finger's drag scrolls the list or the details.

#include "gt4r/app/app.h"
#include "gt4r/app/mod_car_pictures.h"
#include "gt4r/formats/db/car_export.h"
#include "gt4r/formats/db/mod_cars.h"
#include "gt4r/app/page.h"
#include "gt4r/menu/car_database.h"
#include "gt4r/menu/project.h"

#include "base/log.h"
#include "formats/tex1.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <future>
#include <map>
#include <memory>

namespace gt4r {

namespace {

using gt3r::HitList;
using gt3r::PointerInput;
using gt3r::UiRect;
using menu::CarDatabase;

// The car's picture of the disc (piece/car_pict/<label>.img; a mod car's own or its body car's), the last one drawn kept.
class CarPicture {
public:
    ~CarPicture() {
        if (texture_) SDL_DestroyTexture(texture_);
    }
    // Drawn into `r` (4:3); false when the disc has none.
    bool draw(App& app, const std::string& label, const UiRect& r) {
        if (label != label_) load(app, label);
        if (!texture_) return false;
        const SDL_FRect at{r.x, r.y, r.w, r.h};
        SDL_RenderTexture(app.renderer(), texture_, nullptr, &at);
        return true;
    }

private:
    std::string label_;
    SDL_Texture* texture_ = nullptr;
    void load(App& app, const std::string& label) {
        label_ = label;
        if (texture_) SDL_DestroyTexture(texture_);
        texture_ = nullptr;
        auto decode = [&app](const std::string& path) -> std::optional<gt3r::Tex1::Image> {
            try {
                const std::optional<Bytes> data = app.vfs()->read(path);
                if (!data || !gt3r::Tex1::is_tex1(*data)) return std::nullopt;
                const gt3r::Tex1 set = gt3r::Tex1::parse(*data);
                if (set.textures.empty()) return std::nullopt;
                gt3r::GsMemory gs;
                set.upload(*data, gs);
                return set.decode(gs, 0);
            } catch (const std::exception&) {
                return std::nullopt; // (no picture)
            }
        };
        const std::string path = "piece/car_pict/" + label + ".img";
        std::optional<gt3r::Tex1::Image> img = decode(path);
        if (!img) img = mod_car_picture(app, path, decode); // a mod car's: the mod's picture, else its body car's
        if (!img || !img->width || !img->height) return;
        texture_ = SDL_CreateTexture(app.renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(img->width), int(img->height));
        if (!texture_) return;
        SDL_UpdateTexture(texture_, nullptr, img->rgba.data(), int(img->width) * 4);
        SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
    }
};

enum Hit { kCarRow = 1, kTab, kSort, kPanel, kList, kExport };

class DatabaseScreen : public Screen {
public:
    explicit DatabaseScreen(App& app) {
        // The database is read on a worker (Gran Turismo mode's scripts start behind it): the page says so meanwhile.
        menu::RuntimeConfig config;
        gt3r::Vfs* vfs = app.vfs();
        config.list = [vfs](const std::string& folder) { return vfs->list_dir(folder); };
        if (Disc* disc = app.disc()) {
            config.program_image = [disc] { return disc->program_image(); };
            config.region = disc->info().region, config.product = disc->info().product;
            prologue_ = disc->info().game == Game::Prologue;
        }
        if (const std::vector<std::string> languages = menu::project_languages(config.list, prologue_ ? "prologue" : "boot"); !languages.empty()) {
            config.language = languages[0];
            if (Disc* disc = app.disc(); disc && std::find(languages.begin(), languages.end(), disc->info().language) != languages.end())
                config.language = disc->info().language;
        }
        auto read = [vfs](const std::string& path) { return vfs->read(path); };
        loading_ = std::async(std::launch::async, [read, config]() -> std::unique_ptr<CarDatabase> { return std::make_unique<CarDatabase>(read, config); });
    }

    void on_event(App&, const SDL_Event&) override {}

    void update(App& app, double) override {
        if (!cdb_ && loading_.valid()) {
            if (loading_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                for (const PointerInput::Action& a : app.pointer())
                    if (a.kind == PointerInput::Action::Kind::Back) return app.pop();
                if (app.menu().pressed(PadButton::Triangle)) return app.pop();
                return;
            }
            try {
                cdb_ = loading_.get();
                if (cdb_->prologue()) sort_ = Sort::Name;
                if (!cdb_->error().empty()) error_ = cdb_->error();
                sort();
            } catch (const std::exception& e) {
                error_ = std::string("The car database could not be read: ") + e.what();
                LOG_WARN("Car database: %s", e.what());
            }
        }
        const MenuInput& m = app.menu();
        if (pointer(app)) return;
        if (!cdb_ || order_.empty()) {
            if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) app.pop();
            return;
        }
        if (in_panel_) {
            if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle) || m.pressed(PadButton::Cross)) in_panel_ = false;
            if (const int v = m.vertical()) scroll_panel(v * 3);
            if (m.pressed(PadButton::R1) || m.horizontal() > 0) turn_tab(1);
            if (m.pressed(PadButton::L1) || m.horizontal() < 0) turn_tab(-1);
            return;
        }
        if (m.pressed(PadButton::Triangle)) return app.pop();
        if (m.pressed(PadButton::Cross)) in_panel_ = true;
        if (m.pressed(PadButton::Square)) next_sort();
        if (m.pressed(PadButton::Start) && can_export(app)) export_car(app);
        const int n = int(order_.size());
        int step = m.vertical();
        if (m.pressed(PadButton::R1)) step = 10;
        if (m.pressed(PadButton::L1)) step = -10;
        if (step) choose(std::clamp(sel_ + step, 0, n - 1), true);
        if (const int h = m.horizontal()) turn_tab(h);
    }

    void render(App& app, Ui& ui) override {
        hits_.clear();
        const std::string hint = cdb_ ? std::to_string(order_.size()) + " cars" + (cdb_->power_unit().empty() ? std::string() : ", " + cdb_->power_unit() + " and " + cdb_->torque_unit()) : std::string();
        page_header(app, ui, "CAR DATABASE", status_.empty() ? hint : status_); // (an export's outcome, until another car is chosen)
        if (!cdb_) {
            ui.text_center(150, error_.empty() ? "Reading the car database..." : error_, error_.empty() ? colors::kDim : colors::kBad);
            ui.footer("{Triangle}: back");
            return;
        }
        if (order_.empty()) {
            ui.text_center(150, error_.empty() ? "This disc has no cars." : error_, colors::kBad);
            ui.footer("{Triangle}: back");
            return;
        }
        export_shown_ = can_export(app);
        render_list(ui);
        render_panel(app, ui);
        ui.footer(in_panel_ ? "{Up/Down}: scroll  {Left/Right}: tab  {Triangle}: back to the list"
                            : std::string("{Up/Down}: car  {L1}/{R1}: 10 cars  {Left/Right}: tab  {Cross}: scroll the details  {Square}: order") +
                                  (can_export(app) ? "  {Start}: export" : "") + "  {Triangle}: back");
    }

private:
    bool prologue_ = false;
    std::future<std::unique_ptr<CarDatabase>> loading_;
    std::unique_ptr<CarDatabase> cdb_;
    std::string error_;
    std::vector<size_t> order_;
    int sel_ = 0, first_ = 0;
    enum class Sort { Maker, Name, Power, Price, Year, Count };
    Sort sort_ = Sort::Maker;
    enum Tab { kSpecs, kCurve, kParts, kEvents, kTabs };
    int tab_ = kSpecs;
    bool in_panel_ = false;
    float panel_top_ = 0, panel_height_ = 0; // the details' scroll and how tall they are
    float drag_from_ = -1, drag_list_first_ = 0;
    int drag_area_ = 0;
    HitList hits_;
    CarPicture picture_;
    // What a car shows, worked out when first shown.
    struct Details {
        std::vector<CarDatabase::Row> specs;
        std::optional<CarDatabase::Curve> curve;
        bool curve_read = false;
        std::vector<CarDatabase::PartKind> parts;
        bool parts_read = false;
        std::vector<CarDatabase::Event> events;
        bool events_read = false;
    };
    std::map<std::string, Details> details_;

    static constexpr float kListX = 6, kListY = 40, kListW = 232, kRowH = 15;
    static constexpr float kPanelX = 246, kPanelY = 24;
    static constexpr float kBodyY = 108;  // the details' top
    static constexpr float kBottom = 340; // above the footer

    int shown_rows() const { return int((kBottom - kListY) / kRowH); }
    const CarDatabase::Car& car_at(int i) const { return cdb_->cars()[order_[size_t(i)]]; }

    static const char* sort_name(Sort s) {
        static const char* const kNames[] = {"maker", "name", "power", "price", "year"};
        return kNames[int(s)];
    }
    void next_sort() {
        // (Prologue's database has no makers, prices or years: its cars by name or by power.)
        if (prologue_) sort_ = sort_ == Sort::Name ? Sort::Power : Sort::Name;
        else sort_ = Sort((int(sort_) + 1) % int(Sort::Count));
        sort();
    }
    void sort() {
        if (!cdb_) return;
        const std::string keep = order_.empty() ? std::string() : car_at(sel_).label;
        const auto& cars = cdb_->cars();
        if (order_.size() != cars.size()) {
            order_.clear();
            for (size_t i = 0; i < cars.size(); ++i) order_.push_back(i);
        }
        std::stable_sort(order_.begin(), order_.end(), [&](size_t a, size_t b) {
            const CarDatabase::Car& x = cars[a];
            const CarDatabase::Car& y = cars[b];
            switch (sort_) {
            case Sort::Power: if (x.power != y.power) return x.power > y.power; break;
            case Sort::Price: if (x.price != y.price) return x.price > y.price; break;
            case Sort::Year: if (x.year != y.year) return x.year < y.year; break;
            case Sort::Maker: if (x.maker != y.maker) return x.maker < y.maker; break;
            default: break;
            }
            return x.name < y.name;
        });
        int at = 0;
        for (size_t i = 0; i < order_.size(); ++i)
            if (car_at(int(i)).label == keep) at = int(i);
        choose(at, true);
    }
    void choose(int i, bool keep_in_view) {
        if (i != sel_) panel_top_ = 0, status_.clear();
        sel_ = std::clamp(i, 0, std::max(0, int(order_.size()) - 1));
        if (keep_in_view) {
            const int shown = shown_rows();
            if (sel_ < first_) first_ = sel_;
            if (sel_ >= first_ + shown) first_ = sel_ - shown + 1;
        }
        first_ = std::clamp(first_, 0, std::max(0, int(order_.size()) - shown_rows()));
    }
    void turn_tab(int d) {
        const int tabs = prologue_ ? kParts : kTabs; // (Prologue: no tune shops, no events)
        tab_ = ((tab_ + d) % tabs + tabs) % tabs;
        panel_top_ = 0;
    }
    void scroll_panel(float lines) {
        const float room = kBottom - kBodyY;
        panel_top_ = std::clamp(panel_top_ + lines * 12, 0.0f, std::max(0.0f, panel_height_ - room));
    }

    // The mouse and fingers. True when the page was left.
    bool pointer(App& app) {
        using K = PointerInput::Action::Kind;
        for (const PointerInput::Action& a : app.pointer()) {
            if (a.kind == K::Back) {
                if (in_panel_) in_panel_ = false;
                else return app.pop(), true;
                continue;
            }
            if (!cdb_ || order_.empty()) continue;
            const gt3r::HitArea* h = hits_.at(a.kind == K::Drag || a.kind == K::DragEnd ? a.sx : a.x, a.kind == K::Drag || a.kind == K::DragEnd ? a.sy : a.y);
            if (a.kind == K::Wheel) {
                if (h && h->id == kPanel) scroll_panel(-a.wheel * 3);
                else {
                    first_ = std::clamp(first_ - int(std::lround(a.wheel * 3)), 0, std::max(0, int(order_.size()) - shown_rows()));
                }
                continue;
            }
            if (a.kind == K::Drag || a.kind == K::DragEnd) {
                // A finger's (or a held button's) drag scrolls what it started on.
                if (!h) continue;
                if (drag_from_ < 0) drag_from_ = a.sy, drag_area_ = h->id, drag_list_first_ = float(first_) + (h->id == kPanel ? 0 : 0), panel_drag_top_ = panel_top_;
                const float dy = a.y - drag_from_;
                if (drag_area_ == kPanel) {
                    const float room = kBottom - kBodyY;
                    panel_top_ = std::clamp(panel_drag_top_ - dy, 0.0f, std::max(0.0f, panel_height_ - room));
                } else if (drag_area_ == kCarRow || drag_area_ == kList) {
                    first_ = std::clamp(int(std::lround(drag_list_first_ - dy / kRowH)), 0, std::max(0, int(order_.size()) - shown_rows()));
                }
                if (a.kind == K::DragEnd) drag_from_ = -1;
                continue;
            }
            if (a.kind != K::Click || !h) continue;
            switch (h->id) {
            case kCarRow: choose(h->arg, false), in_panel_ = false; break;
            case kTab: tab_ = h->arg, panel_top_ = 0; break;
            case kSort: next_sort(); break;
            case kExport:
                if (can_export(app)) export_car(app);
                break;
            default: break;
            }
        }
        return false;
    }
    float panel_drag_top_ = 0;

    // Whether the car shown can be exported as a mod (a disc car of a Gran Turismo 4 disc).
    bool can_export(App& app) {
        if (prologue_ || !cdb_ || order_.empty() || !app.disc()) return false;
        db::SpecDb* sdb = app.db();
        return sdb && !sdb->prologue() && !sdb->mod_cars().def(car_at(sel_).label);
    }
    // The car shown written whole as a mod (formats/db/car_export.h), mods/cars_<disc>/, for the player's other discs: off on
    // this one (which has the car).
    void export_car(App& app) {
        db::SpecDb* sdb = app.db();
        const Disc* disc = app.disc();
        if (!sdb || !disc) return;
        const std::string key = disc->info().key, mod = db::export_mod_id(key);
        const std::string source = !disc->info().description.empty() ? disc->info().description : disc->info().serial;
        gt3r::Vfs* vfs = app.vfs();
        std::string why;
        const std::optional<db::CarExport> ex = db::export_car(*sdb, [vfs](const std::string& path) { return vfs->read(path); }, car_at(sel_).label,
                                                               app.mods_dir() + "/" + mod, key, source, &why);
        status_ = ex ? "Exported to mods/" + mod + " (for your other discs)" : "Not exported: " + why;
        if (!ex) return;
        LOG_INFO("Car database: %s exported to %s (%zu files)", car_at(sel_).label.c_str(), mod.c_str(), ex->files.size());
        app.rescan_mods();
        app.set_mod_enabled(mod, false);
    }
    std::string status_;
    bool export_shown_ = false;

    void render_list(Ui& ui) {
        // The order, as a button.
        const std::string label = std::string("Order: ") + sort_name(sort_);
        const UiRect b{kListX, 23, std::max(96.0f, ui.tab_width(label)), 15};
        float mx = 0, my = 0;
        ui.button(b, label, false, false);
        hits_.add(b, kSort);
        if (export_shown_) { // (a disc car: export it as a mod)
            const UiRect e{b.x + b.w + 6, 23, std::max(60.0f, ui.tab_width("Export")), 15};
            ui.button(e, "Export", false, false);
            hits_.add(e, kExport);
        }
        (void)mx, (void)my;
        const int n = int(order_.size()), shown = shown_rows();
        hits_.add({kListX, kListY, kListW, float(shown) * kRowH}, kList);
        for (int i = 0; i < shown && first_ + i < n; ++i) {
            const int idx = first_ + i;
            const CarDatabase::Car& c = car_at(idx);
            const float y = kListY + float(i) * kRowH;
            const bool on = idx == sel_;
            if (on) ui.fill_round(kListX, y, kListW, kRowH - 1, 3, in_panel_ ? colors::kPanel : colors::kHighlight);
            std::string right;
            switch (sort_) {
            case Sort::Power: right = c.power ? std::to_string(c.power) + cdb_->power_unit() : "--"; break;
            case Sort::Price: right = c.for_sale ? CarDatabase::credits(c.price) : "not sold"; break;
            case Sort::Year: right = c.year > 0 ? std::to_string(c.year) : "--"; break;
            default: right = !c.maker.empty() ? c.maker : c.power ? std::to_string(c.power) + cdb_->power_unit() : std::string(); break;
            }
            const float rw = std::min(80.0f, Ui::text_width(right));
            ui.text_right(kListX + kListW - 4, y + 3, Ui::clip(right, 80), colors::kDim);
            ui.text(kListX + 4, y + 3, Ui::clip(c.name, kListW - rw - 14), on ? colors::kAccent : colors::kText);
            hits_.add({kListX, y, kListW, kRowH}, kCarRow, idx);
        }
        if (n > shown) ui.scrollbar({kListX, kListY, kListW + 4, float(shown) * kRowH}, float(n) * kRowH, float(first_) * kRowH);
    }

    Details& details(const std::string& label) {
        Details& d = details_[label];
        if (d.specs.empty()) d.specs = cdb_->specs(label);
        return d;
    }

    void render_panel(App& app, Ui& ui) {
        const CarDatabase::Car& c = car_at(sel_);
        Details& d = details(c.label);
        const float x = kPanelX, right = ui.width() - 8;
        ui.fill_round(x - 4, kPanelY, right - x + 8, kBottom - kPanelY, 4, colors::kPanel);
        if (in_panel_) ui.frame_round(x - 4, kPanelY, right - x + 8, kBottom - kPanelY, 4, 1, colors::kOutline);
        // The picture at the right, the name and the figures beside it.
        const UiRect pic{right - 96, kPanelY + 4, 96, 72};
        const bool has_picture = picture_.draw(app, c.label, pic);
        const float text_w = (has_picture ? pic.x - 8 : right) - x;
        float y = kPanelY + 6;
        for (const std::string& line : Ui::wrap(c.name, text_w)) {
            if (y > kPanelY + 30) break;
            ui.text(x, y, line, colors::kAccent);
            y += 12;
        }
        std::string sub = c.maker;
        if (c.year > 0) sub += "  " + std::to_string(c.year);
        if (!c.country.empty()) sub += "  " + c.country;
        if (!sub.empty()) ui.text(x, y + 2, Ui::clip(sub, text_w), colors::kDim);
        std::string figures = c.power ? std::to_string(c.power) + cdb_->power_unit() : std::string("--");
        figures += "   " + (c.weight ? std::to_string(c.weight) + "kg" : std::string("--")) + "   " + c.drivetrain;
        if (!prologue_) figures += "   " + (c.for_sale ? CarDatabase::credits(c.price) : std::string("Not for sale"));
        ui.text(x, y + 16, Ui::clip(figures, text_w), colors::kText);
        // The tabs.
        static const char* const kTabNames[] = {"Specifications", "Power & torque", "Tune shop parts", "Events"};
        float tx = x;
        const float th = ui.touch() ? 18 : 15;
        for (int t = 0; t < (prologue_ ? kParts : kTabs); ++t) {
            const float w = ui.tab(tx, kBodyY - th - 6, th, kTabNames[t], t == tab_, false);
            hits_.add({tx, kBodyY - th - 6, w, th}, kTab, t);
            tx += w + 4;
        }
        // The details, clipped to the panel and scrolled.
        const SDL_Rect clip{0, 0, 0, 0};
        (void)clip;
        hits_.add({x - 4, kBodyY, right - x + 8, kBottom - kBodyY}, kPanel);
        SDL_Rect r{int(x - 4), int(kBodyY), int(right - x + 8), int(kBottom - kBodyY - 2)};
        set_clip(app, &r);
        const float top = kBodyY + 2 - panel_top_;
        float h = 0;
        switch (tab_) {
        case kSpecs: h = render_specs(ui, d, x, top, right); break;
        case kCurve: h = render_curve(app, ui, c, d, x, top, right); break;
        case kParts: h = render_parts(ui, c, d, x, top, right); break;
        case kEvents: h = render_events(ui, c, d, x, top, right); break;
        default: break;
        }
        set_clip(app, nullptr);
        panel_height_ = h;
        const float room = kBottom - kBodyY;
        if (h > room) ui.scrollbar({x - 4, kBodyY, right - x + 10, room}, h, panel_top_);
    }

    // The renderer's clip in UI units (the UI is drawn in its logical units).
    static void set_clip(App& app, const SDL_Rect* r) { SDL_SetRenderClipRect(app.renderer(), r); }

    float render_specs(Ui& ui, const Details& d, float x, float top, float right) {
        float y = top;
        for (const CarDatabase::Row& r : d.specs) {
            if (r.heading) {
                y += y > top ? 4 : 0;
                ui.text(x, y, r.label, colors::kAccent);
                y += 13;
                continue;
            }
            ui.text(x + 6, y, r.label, colors::kDim);
            for (const std::string& line : Ui::wrap(r.value, right - x - 130)) ui.text(x + 128, y, line, colors::kText), y += 12;
        }
        return y - top + 4;
    }

    float render_curve(App& app, Ui& ui, const CarDatabase::Car& c, Details& d, float x, float top, float right) {
        if (!d.curve_read) d.curve = cdb_->curve(c.label), d.curve_read = true;
        if (!d.curve || d.curve->rpm.size() < 2) {
            ui.text(x, top + 4, "This car has no engine curve.", colors::kDim);
            return 20;
        }
        const CarDatabase::Curve& cv = *d.curve;
        const float gx = x + 30, gy = top + 8, gw = right - gx - 30, gh = 150;
        ui.frame(gx, gy, gw, gh, colors::kOutline);
        const float rpm0 = float(cv.rpm.front()), rpm1 = float(std::max(cv.rev_limit, cv.rpm.back()));
        float pmax = 1, tmax = 1;
        for (float p : cv.power) pmax = std::max(pmax, p);
        for (float t : cv.torque) tmax = std::max(tmax, t);
        pmax *= 1.1f, tmax *= 1.1f;
        auto px = [&](float rpm) { return gx + gw * (rpm - rpm0) / std::max(1.0f, rpm1 - rpm0); };
        // The engine speed's grid every 1,000 rpm.
        for (int k = int(std::ceil(rpm0 / 1000)); k * 1000 <= rpm1; ++k) {
            const float lx = px(float(k * 1000));
            ui.fill(lx, gy, 1, gh, Color{50, 54, 70});
            ui.text_center_in(lx - 10, 20, gy + gh + 3, std::to_string(k), colors::kDim);
        }
        ui.text(gx + gw - 60, gy + gh + 14, "x 1,000 rpm", colors::kDim);
        const Color kPowerColor = colors::kAccent, kTorqueColor{90, 160, 240};
        auto line = [&](const std::vector<float>& v, float vmax, Color col) {
            std::vector<SDL_FPoint> pts;
            for (size_t i = 0; i < v.size() && i < cv.rpm.size(); ++i) pts.push_back({px(float(cv.rpm[i])), gy + gh - gh * v[i] / vmax});
            SDL_SetRenderDrawColor(app.renderer(), col.r, col.g, col.b, 255);
            for (int o = 0; o < 2; ++o) { // (two lines a unit apart: a thicker line)
                std::vector<SDL_FPoint> p = pts;
                for (SDL_FPoint& q : p) q.y += float(o) * 0.5f;
                SDL_RenderLines(app.renderer(), p.data(), int(p.size()));
            }
        };
        line(cv.torque, tmax, kTorqueColor);
        line(cv.power, pmax, kPowerColor);
        ui.text_right(gx - 4, gy, std::to_string(int(std::lround(pmax))), kPowerColor);
        ui.text_right(gx - 4, gy + 10, cv.power_unit, kPowerColor);
        char b[64];
        std::snprintf(b, sizeof b, "%.0f", double(tmax));
        ui.text(gx + gw + 4, gy, b, kTorqueColor);
        ui.text(gx + gw + 4, gy + 10, cv.torque_unit, kTorqueColor);
        float y = gy + gh + 28;
        auto peak = [&](const char* what, float v, const std::string& unit, int rpm, Color col, int decimals) {
            char t[96];
            if (rpm > 0) std::snprintf(t, sizeof t, "%.*f %s at %d rpm", decimals, double(v), unit.c_str(), rpm);
            else std::snprintf(t, sizeof t, "%.*f %s", decimals, double(v), unit.c_str());
            ui.fill(x + 6, y + 3, 10, 3, col);
            ui.text(x + 22, y, what, colors::kDim);
            ui.text(x + 128, y, t, colors::kText);
            y += 12;
        };
        peak("Power", cv.peak_power, cv.power_unit, cv.power_rpm, kPowerColor, 0);
        peak("Torque", cv.peak_torque, cv.torque_unit, cv.torque_rpm, kTorqueColor, 2);
        ui.text(x + 22, y, "Rev limit", colors::kDim);
        ui.text(x + 128, y, std::to_string(cv.rev_limit) + " rpm", colors::kText);
        y += 12;
        return y - top + 4;
    }

    float render_parts(Ui& ui, const CarDatabase::Car& c, Details& d, float x, float top, float right) {
        if (!d.parts_read) d.parts = cdb_->upgrades(c.label), d.parts_read = true;
        float y = top;
        if (d.parts.empty()) {
            ui.text(x, y + 4, "No tune shop sells parts for this car.", colors::kDim);
            return 20;
        }
        std::string group;
        for (const CarDatabase::PartKind& k : d.parts) {
            if (k.group != group) {
                group = k.group;
                y += y > top ? 4 : 0;
                ui.text(x, y, group, colors::kAccent);
                y += 13;
            }
            for (const CarDatabase::Part& p : k.parts) {
                ui.text(x + 6, y, Ui::clip(p.name, right - x - 110), p.stock ? colors::kDim : colors::kText);
                ui.text_right(right - 4, y, p.stock ? std::string("as sold") : p.price > 0 ? CarDatabase::credits(p.price) : std::string("free"), colors::kDim);
                y += 12;
            }
        }
        return y - top + 4;
    }

    float render_events(Ui& ui, const CarDatabase::Car& c, Details& d, float x, float top, float right) {
        if (!d.events_read) d.events = cdb_->events(c.label), d.events_read = true;
        float y = top;
        int allowed = 0;
        for (const CarDatabase::Event& e : d.events) allowed += e.allowed;
        ui.text(x, y, "As sold, this car may enter " + std::to_string(allowed) + " of " + std::to_string(d.events.size()) + " events.", colors::kText);
        y += 14;
        std::string hall;
        for (const CarDatabase::Event& e : d.events) {
            if (e.hall != hall) {
                hall = e.hall;
                y += 4;
                ui.text(x, y, hall, colors::kAccent);
                y += 13;
            }
            ui.fill_circle(x + 9, y + 4, 3, e.allowed ? colors::kGood : colors::kBad);
            ui.text(x + 18, y, Ui::clip(e.name, right - x - 90), e.allowed ? colors::kText : colors::kDim);
            if (!e.level.empty()) ui.text_right(right - 4, y, e.level, colors::kDim);
            y += 12;
            if (!e.allowed)
                for (const CarDatabase::Row& r : e.rules)
                    if (r.bad)
                        for (const std::string& line : Ui::wrap(r.label + ": " + r.value, right - x - 30)) ui.text(x + 26, y, line, colors::kBad), y += 11;
        }
        return y - top + 4;
    }
};

} // namespace

std::unique_ptr<Screen> make_database_screen(App& app) { return std::make_unique<DatabaseScreen>(app); }

} // namespace gt4r
