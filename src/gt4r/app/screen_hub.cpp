// The home screen with a disc open: the cars and courses by name, the menu pages, and the asset
// browser.

#include "gt4r/app/app.h"
#include "gt4r/app/page.h"

#include "base/log.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/menu/project.h"
#include "gt4r_version.h"

#include <algorithm>

namespace gt4r {

namespace {

// The 3D viewer over the database's cars: each car's showroom model (car/menu/<model>), else its
// race model (car/lod/<model>).
std::unique_ptr<Screen> make_cars_screen(App& app) {
    std::vector<db::CarEntry> cars = app.db()->cars();
    std::vector<std::string> items;
    for (const db::CarEntry& c : cars) items.push_back(c.name.empty() ? c.label : c.name);
    return make_model_viewer("Cars", std::move(items), [cars](App& a, size_t i) {
        const db::CarEntry& c = cars[i];
        // The showroom model (its paints inside), else the race model with its paint file.
        for (const char* folder : {"car/menu/", "car/lod/"}) {
            const std::optional<Bytes> data = a.vfs()->read(folder + c.model);
            if (!data || !car::CarFile::is_car(*data)) continue;
            const std::optional<Bytes> pat = a.vfs()->read(folder + c.model + ".pat");
            db::SpecDb::WheelLayout l;
            car::Wheels w;
            if (a.db()->car_wheels(c.label, l)) {
                w.wheelbase = l.wheelbase;
                for (int k = 0; k < 2; ++k) w.outer_width[k] = l.outer_width[k], w.tire_diameter[k] = l.tire_diameter[k], w.tire_width[k] = l.tire_width[k];
            }
            return car::build_car(*data, pat ? ByteSpan(*pat) : ByteSpan{}, &w, car::Options{}, c.label);
        }
        throw FormatError("This car has no model on the disc (" + c.model + ").");
    });
}

// The same over the courses that have a file (crs/<label>).
std::unique_ptr<Screen> make_courses_screen(App& app) {
    std::vector<db::CourseEntry> courses;
    for (db::CourseEntry& c : app.db()->courses())
        if (app.vfs()->exists("crs/" + c.label)) courses.push_back(std::move(c));
    std::vector<std::string> items;
    for (const db::CourseEntry& c : courses) items.push_back(c.name.empty() || c.name == c.label ? c.label : c.name + "  (" + c.label + ")");
    return make_model_viewer("Courses", std::move(items), [courses](App& a, size_t i) {
        const std::optional<Bytes> data = a.vfs()->read("crs/" + courses[i].label);
        if (!data) throw FormatError("Could not read crs/" + courses[i].label);
        course::ModelOptions o;
        o.sky = true;
        return course::build_course_model(*data, o, courses[i].label);
    });
}

class HubScreen : public Screen {
public:
    bool menu_input_any_device() const override { return true; } // (a way to Settings whatever the controllers are set to)

    void update(App& app, double) override {
        const MenuInput& m = app.menu();
        if (items_.empty() || disc_ != app.disc()) build(app);
        sel_ = std::clamp(sel_ + m.vertical(), 0, int(items_.size()) - 1);
        bool press = m.pressed(PadButton::Cross);
        for (const RowPointer::Hit& h : mouse_.read(app)) sel_ = h.row, press = press || h.click;
        if (press) {
            try {
                switch (items_[size_t(sel_)].action) {
                case 0: app.push(make_cars_screen(app)); break;
                case 1: app.push(make_courses_screen(app)); break;
                case 2: app.push(make_browser_screen()); break;
                case 3: app.show_open_dialog(); break;
                case 5: app.push(make_menu_viewer(app)); break;
                case 6: app.push(make_race_screen(app)); break;
                case 7: app.push(make_game_screen(app)); break;
                case 8: app.push(make_controls_screen(app)); break;
                case 9: app.push(make_save_editor(app)); break;
                case 20: app.push(make_settings_screen(app)); break;
                case 21: app.push(make_settings_screen(app, kModsPage)); break;
                default: app.quit(); break;
                }
                error_.clear();
                items_.clear(); // (the notes as they are when this page is back)
            } catch (const std::exception& e) {
                error_ = e.what();
                LOG_WARN("Hub: %s", e.what());
            }
        }
    }

    void render(App& app, Ui& ui) override {
        ui.header("GT4 REVISITED", GT4R_VERSION);
        const DiscInfo& d = app.disc()->info();
        ui.text(24, 34, d.description, colors::kAccent);
        ui.text(24, 46, d.serial + "  version " + d.version + (d.verified ? "" : d.modified ? "  (a modified copy)" : "  (not a tested release)"), colors::kDim);
        const float row = items_.size() > 11 ? 20.0f : 22.0f;
        mouse_.begin();
        for (size_t i = 0; i < items_.size(); ++i) {
            const float y = 78 + float(i) * row;
            const bool on = int(i) == sel_;
            ui.fill_round(20, y - 4, 300, row - 3, 4, on ? colors::kHighlight : colors::kPanel);
            ui.text(30, y + 1, items_[i].label, on ? colors::kText : colors::kDim);
            ui.text_right(312, y + 1, Ui::elide(items_[i].note, 190), colors::kDim);
        }
        mouse_.rows(0, 0, int(items_.size()), 20, 74, 300, row);
        ui.text(340, 82, "Play: the game from power on (Select and Start together", colors::kDim);
        ui.text(340, 94, "come back here). The rest: the disc's cars, courses, menu", colors::kDim);
        ui.text(340, 106, "pages and files, and a race set up by hand.", colors::kDim);
        ui.text(340, 124, "An early build: see docs/PLAYING.md for what works.", colors::kDim);
        float y = 148;
        if (!d.problem.empty())
            for (const std::string& line : Ui::wrap(d.problem, 280)) ui.text(340, y, line, colors::kBad), y += 12;
        const std::string error = error_.empty() ? app.last_error() : error_; // (or the disc image that could not be opened)
        for (const std::string& line : Ui::wrap(error, 280)) ui.text(340, y + 8, line, colors::kBad), y += 12;
        ui.footer("{Up/Down}: select  {Cross}: open");
    }

private:
    struct Item {
        std::string label, note;
        int action;
    };
    void build(App& app) {
        items_.clear();
        disc_ = app.disc();
        size_t cars = 0, courses = 0;
        try {
            if (db::SpecDb* sdb = app.db()) {
                cars = sdb->cars().size();
                for (const db::CourseEntry& c : sdb->courses()) courses += app.vfs()->exists("crs/" + c.label);
            }
        } catch (const std::exception& e) {
            error_ = e.what();
        }
        items_.push_back({"Play", app.disc()->info().description, 7});
        if (cars) items_.push_back({"Cars", std::to_string(cars), 0});
        if (courses) items_.push_back({"Courses", std::to_string(courses), 1});
        if (cars && courses && app.gpu()) items_.push_back({"Drive", "", 6});
        if (const size_t menus = menu::list_projects([&app](const std::string& f) { return app.vfs()->list_dir(f); }).size())
            items_.push_back({"Menu pages", std::to_string(menus) + " projects", 5});
        items_.push_back({"Asset browser", std::to_string(app.disc()->files().size()) + " files", 2});
        items_.push_back({"Controls", app.input().device_name(0), 8});
        items_.push_back({"Save editor", "", 9});
        items_.push_back({"Settings", "", 20});
        items_.push_back({"Mods", app.vfs()->mods().empty() ? std::string() : std::to_string(app.vfs()->mods().size()), 21});
        items_.push_back({"Another disc", "", 3});
        items_.push_back({"Quit", "", 4});
    }
    std::vector<Item> items_;
    const Disc* disc_ = nullptr;
    RowPointer mouse_;
    int sel_ = 0;
    std::string error_;
};

} // namespace

std::unique_ptr<Screen> make_hub_screen() { return std::make_unique<HubScreen>(); }

} // namespace gt4r
