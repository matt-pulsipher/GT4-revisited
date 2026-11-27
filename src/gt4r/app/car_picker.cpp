// The app's car picker (car_picker.h).

#include "gt4r/app/car_picker.h"
#include "gt4r/app/app.h"
#include "gt4r/app/page.h"

#include "base/log.h"
#include "gt4r/menu/project.h"
#include "gt4r/menu/save_editor.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace gt4r {

bool latin_text(std::string_view s) {
    for (size_t i = 0; i < s.size(); ++i) {
        const u8 c = u8(s[i]);
        if (c < 0x80) continue;
        // (two-byte UTF-8 runs up to U+07FF: Latin, Greek, Cyrillic; three and four bytes: the rest, Japanese among them)
        if (c >= 0xE0) return false;
    }
    return true;
}

struct CarCatalog::Impl {
    std::unique_ptr<menu::SaveEditor> editor; // (a new game's: its natives name the makers and the colors)
};

CarCatalog::~CarCatalog() = default;

namespace {

menu::RuntimeConfig menus_config(App& app, const std::string& project) {
    menu::RuntimeConfig config;
    gt3r::Vfs* vfs = app.vfs();
    config.list = [vfs](const std::string& folder) { return vfs->list_dir(folder); };
    if (Disc* disc = app.disc()) {
        config.program_image = [disc] { return disc->program_image(); };
        config.region = disc->info().region, config.product = disc->info().product;
    }
    if (const std::vector<std::string> languages = menu::project_languages(config.list, project); !languages.empty()) {
        config.language = languages[0];
        if (Disc* disc = app.disc(); disc && std::find(languages.begin(), languages.end(), disc->info().language) != languages.end())
            config.language = disc->info().language;
    }
    return config;
}

} // namespace

std::shared_ptr<CarCatalog> CarCatalog::of(App& app) {
    static const Disc* disc = nullptr;
    static std::shared_ptr<CarCatalog> kept;
    if (kept && disc == app.disc()) return kept;
    kept.reset(), disc = app.disc();
    db::SpecDb* sdb = app.db();
    if (!sdb || !app.vfs()) throw FormatError("This disc's cars are not read yet.");
    std::shared_ptr<CarCatalog> c(new CarCatalog);
    c->impl_ = std::make_unique<Impl>();
    if (!sdb->prologue()) {
        gt3r::Vfs* vfs = app.vfs();
        c->impl_->editor = std::make_unique<menu::SaveEditor>([vfs](const std::string& path) { return vfs->read(path); }, menus_config(app, "boot"));
        c->impl_->editor->start(menu::GameState{});
        // (the database's American names where the menus' are not Latin: the Japanese disc's)
        std::map<std::string, std::string> american;
        for (const db::CarEntry& e : sdb->cars()) american[e.label] = e.name;
        for (const menu::SaveEditor::CarKind& k : c->impl_->editor->cars()) {
            Car car{k.label, k.name, k.maker, k.maker_key, k.year, k.price};
            if (!latin_text(car.name)) {
                car.name = american[k.label].empty() ? k.label : american[k.label];
                for (char& ch : car.name)
                    if (ch == '`') ch = '\'';
            }
            if (!latin_text(car.maker)) {
                car.maker = k.maker_key;
                if (!car.maker.empty()) car.maker[0] = char(std::toupper(static_cast<unsigned char>(car.maker[0])));
            }
            c->cars_.push_back(std::move(car));
        }
    } else {
        // Prologue's: the database's names, no makers' (its menus list the cars themselves).
        for (const db::CarEntry& e : sdb->cars()) {
            Car k;
            k.label = e.label, k.name = e.name.empty() ? e.label : e.name;
            for (char& ch : k.name)
                if (ch == '`') ch = '\'';
            c->cars_.push_back(std::move(k));
        }
    }
    c->paints_.resize(c->cars_.size()), c->paints_read_.assign(c->cars_.size(), false);
    kept = c;
    return kept;
}

const CarCatalog::Car* CarCatalog::find(const std::string& label) const {
    for (const Car& c : cars_)
        if (c.label == label) return &c;
    return nullptr;
}

std::string CarCatalog::name(const std::string& label) const {
    const Car* c = find(label);
    return c ? c->name : label;
}

const std::vector<CarCatalog::Paint>& CarCatalog::paints(const std::string& label) {
    static const std::vector<Paint> one{{"Color 1", {}}};
    const Car* c = find(label);
    if (!c) return one;
    const size_t i = size_t(c - cars_.data());
    if (!paints_read_[i]) {
        paints_read_[i] = true;
        try {
            if (impl_->editor)
                for (const menu::SaveEditor::Paint& p : impl_->editor->paints(label)) paints_[i].push_back({p.name, {p.rgb[0], p.rgb[1], p.rgb[2]}});
        } catch (const std::exception& e) {
            LOG_WARN("Car picker: the colors of %s: %s", label.c_str(), e.what());
        }
        if (paints_[i].empty()) paints_[i] = one;
        for (size_t k = 0; k < paints_[i].size(); ++k)
            if (paints_[i][k].name.empty() || !latin_text(paints_[i][k].name)) paints_[i][k].name = "Color " + std::to_string(k + 1);
    }
    return paints_[i];
}

namespace {

class CarPickerScreen : public Screen {
public:
    CarPickerScreen(std::shared_ptr<CarCatalog> cars, CarPickerOptions o, std::function<void(const PickedCar&)> chosen)
        : cars_(std::move(cars)), o_(std::move(o)), chosen_(std::move(chosen)) {
        for (const CarCatalog::Car& c : cars_->cars())
            if (!o_.allowed || o_.allowed(c)) makers_[c.maker.empty() ? std::string("Other") : c.maker].push_back(&c);
        // The car chosen now: its maker's list, the car selected.
        if (const CarCatalog::Car* now = cars_->find(o_.now.label)) {
            open(now->maker.empty() ? std::string("Other") : now->maker);
            for (size_t i = 0; i < list_.size(); ++i)
                if (list_[i] == now) want_ = int(i);
        }
    }

    void update(App& app, double) override {
        if (o_.keep_open && !o_.keep_open(app)) return app.pop();
        build();
        rows_.update(app);
        if (go_) {
            const std::function<void()> go = std::exchange(go_, {});
            go();
            if (done_) return app.pop();
            build();
        }
        if (app.menu().pressed(PadButton::Triangle)) {
            if (car_) car_ = nullptr, rows_.selected = car_row_;
            else if (!maker_.empty()) maker_.clear(), rows_.selected = maker_row_, rows_.scroll.first = 0;
            else return app.pop();
        }
    }

    void render(App& app, Ui& ui) override {
        build();
        page_header(app, ui, o_.title, car_ ? car_->name : maker_.empty() ? std::string("Makers") : maker_);
        rows_.render(ui, {20, 28, 410, 17 * 18 + 1});
        const CarCatalog::Car* shown = car_;
        if (!shown && !maker_.empty() && rows_.selected >= 0 && rows_.selected < int(list_.size())) shown = list_[size_t(rows_.selected)];
        if (shown) {
            float y = 34;
            for (const std::string& line : Ui::wrap(shown->name, 180)) ui.text(446, y, line, colors::kText), y += 12;
            if (!shown->maker.empty()) ui.text(446, y + 2, shown->maker, colors::kDim), y += 14;
            if (shown->year > 0) ui.text(446, y + 2, std::to_string(shown->year), colors::kDim), y += 14;
            if (car_ && rows_.selected >= 0) {
                const std::vector<CarCatalog::Paint>& p = cars_->paints(car_->label);
                if (rows_.selected < int(p.size())) {
                    const auto& rgb = p[size_t(rows_.selected)].rgb;
                    ui.fill_round(446, y + 10, 120, 40, 4, Color{rgb[0], rgb[1], rgb[2]});
                }
            }
        }
        const std::string hint = rows_.hint();
        ui.footer(hint + (hint.empty() ? "" : "  ") + "{Triangle}: back");
    }

private:
    void open(const std::string& maker) {
        maker_ = maker, list_.clear();
        if (maker == kEvery) {
            for (const auto& [m, cars] : makers_) list_.insert(list_.end(), cars.begin(), cars.end());
            std::sort(list_.begin(), list_.end(), [](const CarCatalog::Car* a, const CarCatalog::Car* b) { return a->name < b->name; });
        } else if (const auto it = makers_.find(maker); it != makers_.end()) {
            list_ = it->second;
            std::stable_sort(list_.begin(), list_.end(), [](const CarCatalog::Car* a, const CarCatalog::Car* b) { return a->name < b->name; });
        }
    }
    void choose(const CarCatalog::Car* c) {
        if (!o_.colors) {
            chosen_({c->label, c->label == o_.now.label ? o_.now.paint : 0});
            done_ = true;
            return;
        }
        car_row_ = rows_.selected;
        car_ = c;
        const int n = int(cars_->paints(c->label).size());
        rows_.selected = c->label == o_.now.label ? std::clamp(o_.now.paint, 0, n - 1) : 0;
        rows_.scroll.first = 0;
    }
    void build() {
        rows_.rows.clear();
        if (car_) {
            const std::vector<CarCatalog::Paint>& paints = cars_->paints(car_->label);
            for (size_t i = 0; i < paints.size(); ++i)
                rows_.action(paints[i].name, "", "race in this color", [this, i] {
                    go_ = [this, i] { chosen_({car_->label, int(i)}), done_ = true; };
                });
            return;
        }
        if (maker_.empty()) {
            int k = 0;
            rows_.action(kEvery, std::to_string(cars_->cars().size()), "list them", [this, k] { go_ = [this, k] { maker_row_ = k, open(kEvery), rows_.selected = 0; }; });
            for (const auto& [m, cars] : makers_) {
                ++k;
                rows_.action(m, std::to_string(cars.size()), "list its cars", [this, k, name = m] { go_ = [this, k, name] { maker_row_ = k, open(name), rows_.selected = 0; }; });
            }
            return;
        }
        for (size_t i = 0; i < list_.size(); ++i) {
            const CarCatalog::Car* c = list_[i];
            // (two cars of one name, as the US disc's two Ford GT '05: their labels tell them apart)
            const bool twin = (i > 0 && list_[i - 1]->name == c->name) || (i + 1 < list_.size() && list_[i + 1]->name == c->name);
            rows_.action(c->name, twin ? c->label : std::string(), o_.colors ? "choose its color" : "choose it", [this, c] { go_ = [this, c] { choose(c); }; });
        }
        if (want_ >= 0) rows_.selected = std::exchange(want_, -1), rows_.scroll.first = std::max(0, rows_.selected - 8);
    }

    static constexpr const char* kEvery = "Every car (A to Z)";
    std::shared_ptr<CarCatalog> cars_;
    CarPickerOptions o_;
    std::function<void(const PickedCar&)> chosen_;
    std::map<std::string, std::vector<const CarCatalog::Car*>> makers_;
    std::string maker_;
    std::vector<const CarCatalog::Car*> list_;
    const CarCatalog::Car* car_ = nullptr;
    int car_row_ = 0, maker_row_ = 0, want_ = -1;
    PageRows rows_;
    std::function<void()> go_;
    bool done_ = false;
};

} // namespace

std::unique_ptr<Screen> make_car_picker(App&, std::shared_ptr<CarCatalog> cars, CarPickerOptions options, std::function<void(const PickedCar&)> chosen) {
    return std::make_unique<CarPickerScreen>(std::move(cars), std::move(options), std::move(chosen));
}

} // namespace gt4r
