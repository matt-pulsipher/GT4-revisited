// The home screen with a disc open, and its Developer tools: the cars and courses by name, the menu
// pages, the asset browser.

#include "gt4r/app/app.h"
#include "gt4r/app/screen_saves.h"
#include "gt4r/app/page.h"

#include "base/log.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/menu/project.h"
#include "gt4r/net/identity.h"
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

// The home screen, GT4 Revisited's own menu (developer_ false): the game, Quick race, Online battle, its saves, the settings, the controls, the mods, the
// save editor, the disc library, Quit; Developer tools (developer_ true): the disc's cars, courses and
// menu pages, the asset browser and a race set up by hand.
class HubScreen : public Screen {
public:
    explicit HubScreen(bool developer) : developer_(developer) {}

    bool menu_input_any_device() const override { return true; } // (a way to Settings whatever the controllers are set to)
    bool update_prompts() const override { return true; }      // (the update dialog at startup: app/updates.h)

    void update(App& app, double) override {
        const MenuInput& m = app.menu();
        if (items_.empty() || disc_ != app.disc()) build(app);
        sel_ = std::clamp(sel_ + m.vertical(), 0, int(items_.size()) - 1);
        bool press = m.pressed(PadButton::Cross);
        for (const RowPointer::Hit& h : mouse_.read(app)) sel_ = h.row, press = press || h.click;
        if (developer_ && m.pressed(PadButton::Triangle)) return app.pop();
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
                case 26: app.push(make_quick_race_screen(app)); break;
                case 28: app.push(make_online_game_screen(app)); break;
                case 8: app.push(make_controls_screen(app)); break;
                case 9: app.push(make_saves_screen(app)); break;
                case 22: app.push(make_save_editor(app)); break;
                case 27: app.push(make_database_screen(app)); break;
                case 20: app.push(make_settings_screen(app)); break;
                case 21: app.push(make_settings_screen(app, kModsPage)); break;
                case 23: app.push(make_hub_screen(true)); break;
                case 25: app.push(make_settings_screen(app, kDiscPage)); break;
                case 24: app.pop(); return;
                default: app.quit(); break;
                }
                error_.clear();
                items_.clear(); // (the notes as they are when this page is back)
            } catch (const std::exception& e) {
                error_ = e.what();
                LOG_WARN("Home screen: %s", e.what());
            }
        }
    }

    void render(App& app, Ui& ui) override {
        if (developer_) page_header(app, ui, "DEVELOPER TOOLS", GT4R_VERSION);
        else ui.header("GT4 REVISITED", GT4R_VERSION); // (the bottom of the app: nothing to go back to)
        const DiscInfo& d = app.disc()->info();
        ui.text(24, 34, d.description, colors::kAccent);
        ui.text(24, 46, (d.serial.empty() ? std::string() : d.key + "  v" + d.version) + (d.verified ? "" : d.modified ? "  (a modified copy)" : "  (not a tested release: some things may not work)"), colors::kDim);
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
        float y = 82;
        if (!items_.empty())
            for (const std::string& line : Ui::wrap(items_[size_t(std::clamp(sel_, 0, int(items_.size()) - 1))].help, 280)) ui.text(340, y, line, colors::kText), y += 12;
        y += 10;
        if (!developer_)
            for (const std::string& line : Ui::wrap(app.disc()->info().game == Game::Prologue
                                                        ? (app.touch_ui() ? "In the game, tap GT4R menu beside the title to come back here."
                                                                          : "In the game, press {Select} on the title, or click GT4R menu beside it, to come back here.")
                                                        : (app.touch_ui() ? "In the game, tap GT4R menu beside the title or Gran Turismo mode's map to come back here."
                                                                          : "In the game, press {Select} on the title, or click GT4R menu beside it or beside Gran Turismo mode's map, to come back here."),
                                                    280))
                ui.text(340, y, line, colors::kDim), y += 12;
        y += 8;
        if (!d.problem.empty())
            for (const std::string& line : Ui::wrap(d.problem, 280)) ui.text(340, y, line, colors::kBad), y += 12;
        const std::string error = error_.empty() ? app.last_error() : error_; // (or the disc image that could not be opened)
        for (const std::string& line : Ui::wrap(error, 280)) ui.text(340, y + 8, line, colors::kBad), y += 12;
        ui.footer(developer_ ? "{Up/Down}: select  {Cross}: open  {Triangle}: back" : "{Up/Down}: select  {Cross}: open");
    }

private:
    struct Item {
        std::string label, note, help;
        int action;
    };
    void build(App& app) {
        items_.clear();
        disc_ = app.disc();
        if (!developer_) {
            const bool prologue = app.disc()->info().game == Game::Prologue;
            // GT3 Revisited's main menu's order and words for what this app has; Saved games and Controls are this app's own.
            items_.push_back({"Play", "", prologue ? "Gran Turismo 4 Prologue from the start: the opening movie, then the title." : "Gran Turismo 4 from the start: the opening movie, then the title.", 7});
            if (prologue) items_.push_back({"Quick race", "", "Arcade's single race on any of its courses with any of its cars. Nothing is saved to your game.", 26});
            else items_.push_back({"Quick race", "", "Any event of the game with any car: Gran Turismo mode's races, the license tests, the missions and Arcade's single race. Nothing is unlocked and nothing is saved to your game.", 26});
            // Arcade's Multi-LAN Race at once (Prologue has none).
            if (net::online_disc(app.disc()->info()))
                items_.push_back({"Online battle", "", "Race other players over the local network or the internet: open a lobby, join one by its code, or find one on the local network.", 28});
            items_.push_back({"Saved games", "", "Your games: load one, start a new one beside the others, delete one, or move one to or from a PlayStation 2 memory card image.", 9});
            items_.push_back({"Settings", "", "Display, sound, both players' controls, views and replays, the disc and the save folder.", 20});
            items_.push_back({"Controls", app.touch_ui() && app.input().device_name(0) == "Keyboard" ? std::string("Touch screen") : app.input().device_name(0), "Which controller drives, the race controls and your own bindings, steering, rumble, triggers and a wheel's force feedback.", 8});
            items_.push_back({"Mods", app.vfs()->mods().empty() ? std::string() : std::to_string(app.vfs()->mods().size()), "Install mods and texture packs, and choose which are used.", 21});
            items_.push_back({"Car database", "", "Every car of the disc: its specifications, power and torque, the tune shops' parts and the events it may enter.", 27});
            items_.push_back({"Save editor", "", "Change a saved game: credits, the garage, licenses, events and more. The save as it was is kept.", 22});
            items_.push_back({"Discs...", "", "Your disc library: switch to another disc, or add one.", 25});
            items_.push_back({"Developer tools", "", "Viewers for the disc's cars, courses, menu pages and files, and a race set up by hand.", 23});
#if !defined(SDL_PLATFORM_IOS) // (iOS apps do not quit themselves: the app switcher closes them)
            items_.push_back({"Quit", "", "Leave GT4 Revisited.", 4});
#endif
            return;
        }
        size_t cars = 0, courses = 0;
        try {
            if (db::SpecDb* sdb = app.db()) {
                cars = sdb->cars().size();
                for (const db::CourseEntry& c : sdb->courses()) courses += app.vfs()->exists("crs/" + c.label);
            }
        } catch (const std::exception& e) {
            error_ = e.what();
        }
        if (cars) items_.push_back({"Cars", std::to_string(cars), "Every car of the disc in 3D, with its paints.", 0});
        if (courses) items_.push_back({"Courses", std::to_string(courses), "Every course of the disc in 3D.", 1});
        if (cars && courses && app.gpu()) items_.push_back({"Drive", "", "A race set up by hand: any course, any car, the computer's cars, the view.", 6});
        if (const size_t menus = menu::list_projects([&app](const std::string& f) { return app.vfs()->list_dir(f); }).size())
            items_.push_back({"Menu pages", std::to_string(menus) + " projects", "The game's menu projects, one at a time, with what their scripts do.", 5});
        items_.push_back({"Asset browser", std::to_string(app.disc()->files().size()) + " files", "The disc's files: pictures, models, sounds and tables.", 2});
        items_.push_back({"Back", "", "Back to the home screen.", 24});
    }
    bool developer_ = false;
    std::vector<Item> items_;
    const Disc* disc_ = nullptr;
    RowPointer mouse_;
    int sel_ = 0;
    std::string error_;
};

} // namespace

std::unique_ptr<Screen> make_hub_screen(bool developer) { return std::make_unique<HubScreen>(developer); }

} // namespace gt4r
