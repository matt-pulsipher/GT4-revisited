// Quick race: any event of the game with any car, nothing to unlock, nothing written to the saved game. Gran Turismo mode's events
// (each race of its event lists, championships' races one at a time), the license tests (the test itself, or a free run on its
// course), the missions and Arcade's single race on any course, each with the player's choice of car and color (car_picker.h).
//
// Pages: the event lists, an event list's events, then the setup (car, color, gearbox; Arcade: difficulty and laps; a license test:
// the test or a free run) and Start. The race runs on menus of its own (a new game's, booted for it, holding every license, so
// that an event asking for one offers A-spec; the player's options read from the data folder and never written back): the event's own race sequence as Gran Turismo mode starts it (the event page's
// 0x... entry: race_code, no course, no opponents chosen; `solo_event`, `license`, `mission`), Arcade's single race as Arcade's
// GameRoot.doGame sets it up (its set_enemy_cars run in Arcade's own script, then every entry through MCarData.ShiftCarCode),
// with the pages the race sequence shows before and after the race. The race's results go into that new game only. Leaving the
// race comes back here. Replays saved from it go to the data folder's replays as any race's.
//
// The last choices are kept in config.ini [quickrace] (`event`, `car`, `paint`, `mt`, `level`, `laps`, `run`).
//
// Scripted runs (tests, pictures):
//   GT4R_QUICKRACE="event=am_sunday_0001,car=ka_01,paint=1,mt=1,start"
// selects the event (a race's label; `arcade:<course>` Arcade's single race there; `license:<test>` a test, `mission:<label>`)
// and the car; `level=<n>` Arcade's difficulty (the slider's value), `laps=<n>` its laps, `run=free` a license test's free run;
// `page=setup` / `page=cars` opens that page; `start` starts the race at once.
// A custom race: `page=custom` (or `race=<saved name>`) with `course=<label>`, `car`, `paint`, `mt`, `laps=<n>`, `place=<n>` (1
// pole), `grid=<car>:<paint>/...` (the opponents), `fill=<level>` (drawn as Arcade draws them), `save=<name>`, `start`.
//
// The custom race (the list's first item): any course, the laps, the player's car, color and place, up to the course's field
// (MCourseData.GetMaxCarNum) of opponents, each any car and color. It runs as Arcade's single race on its row sample_entry_6 (its
// start, tires and drivers; the game sets no skill per driver and no race has a catch-up), the entry table as doGame leaves it.
// "Fill the grid like Arcade" draws the opponents with Arcade's own set_enemy_cars at a level of -10 .. 10. Saved under a name
// in [quickrace_race<n>]; the last in [quickrace_custom].

#include "gt4r/app/app.h"
#include "gt4r/app/car_picker.h"
#include "gt4r/app/page.h"

#include "base/log.h"
#include "gt4r/formats/db/race_events.h"
#include "gt4r/formats/db/race_labels.h"
#include "gt4r/menu/game.h"
#include "gt4r/menu/project.h"
#include "gt4r/menu/race_natives.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/menu/save_editor.h"
#include "gt4r/script/vm.h"

#include "app/text_keyboard.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>

namespace gt4r {

namespace {

using script::Value;

enum class Kind { Event, License, Mission, Arcade };

struct QuickEvent {
    std::string id;       // [quickrace] event: the race's label, or "arcade:<course>"
    std::string race;     // the RACE label as the menus give it ("am_sunday_0001", "l0b0001"; Arcade: none)
    std::string name;     // as listed
    std::string course;   // its course's label (crs/<course>)
    std::string course_name;
    Kind kind = Kind::Event;
    int laps = 0, grid = 0; // the event row's (0: none)
};

struct Category {
    std::string title, group; // group: the heading it is listed under
    std::vector<size_t> events;
};

constexpr const char* kSection = "quickrace";

// A script function called with these values.
Value call(script::Vm& vm, const Value& fn, std::vector<Value> args) { return vm.call(fn, args); }
Value call(script::Vm& vm, const std::string& path, std::vector<Value> args) { return vm.call(vm.lookup(path), args); }

menu::RuntimeConfig menus_config(App& app) {
    menu::RuntimeConfig config;
    gt3r::Vfs* vfs = app.vfs();
    config.list = [vfs](const std::string& folder) { return vfs->list_dir(folder); };
    if (Disc* disc = app.disc()) {
        config.program_image = [disc] { return disc->program_image(); };
        config.region = disc->info().region, config.product = disc->info().product;
    }
    const bool prologue = app.db() && app.db()->prologue();
    if (const std::vector<std::string> languages = menu::project_languages(config.list, prologue ? "prologue" : "boot"); !languages.empty()) {
        config.language = languages[0];
        if (Disc* disc = app.disc(); disc && std::find(languages.begin(), languages.end(), disc->info().language) != languages.end())
            config.language = disc->info().language;
    }
    return config; // (no data folder: no options file, no memory card; nothing is written)
}

std::string config_get(App& app, const char* key, const std::string& fallback = {}) {
    const std::optional<std::string> v = app.config().get(kSection, key);
    return v ? *v : fallback;
}
int config_int(App& app, const char* key, int fallback) {
    const std::string v = config_get(app, key);
    return v.empty() ? fallback : std::atoi(v.c_str());
}

// A custom race (config.ini: the last one in [quickrace_custom], saved ones in [quickrace_race<n>] with `name`).
constexpr const char* kCustomSection = "quickrace_custom";
constexpr const char* kSavedSection = "quickrace_race";
constexpr int kMaxLaps = 99;

struct CustomRace {
    std::string name; // a saved race's ("" : not saved)
    std::string course;
    int laps = 2, place = -1; // the player's start place (0 the front; -1 the back)
    PickedCar car;
    std::vector<PickedCar> opponents;

    static PickedCar parse(const std::string& t) {
        PickedCar c;
        const size_t colon = t.find(':');
        c.label = t.substr(0, colon);
        if (colon != std::string::npos) c.paint = std::max(0, std::atoi(t.c_str() + colon + 1));
        return c;
    }
    void read(const gt3r::IniDocument& cfg, const std::string& section) {
        const auto get = [&](const char* k) { return cfg.get(section, k).value_or(""); };
        name = get("name"), course = get("course");
        if (const std::string v = get("laps"); !v.empty()) laps = std::clamp(std::atoi(v.c_str()), 1, kMaxLaps);
        if (const std::string v = get("place"); !v.empty()) place = std::atoi(v.c_str());
        car = parse(get("car"));
        opponents.clear();
        const int n = std::clamp(std::atoi(get("opponents").c_str()), 0, 63);
        for (int k = 1; k <= n; ++k) opponents.push_back(parse(cfg.get(section, "opponent" + std::to_string(k)).value_or("")));
    }
    void write(gt3r::IniDocument& cfg, const std::string& section) const {
        cfg.set(section, "name", name), cfg.set(section, "course", course), cfg.set(section, "laps", std::to_string(laps));
        cfg.set(section, "place", std::to_string(place)), cfg.set(section, "car", car.label + ":" + std::to_string(car.paint));
        cfg.set(section, "opponents", std::to_string(opponents.size()));
        for (size_t k = 0; k < opponents.size(); ++k) cfg.set(section, "opponent" + std::to_string(k + 1), opponents[k].label + ":" + std::to_string(opponents[k].paint));
    }
    // Each entry's start place by its number (0 the player's): the player's place, the opponents in order in the others.
    std::vector<int> grid_slots() const {
        const int n = int(opponents.size()) + 1;
        const int mine = place < 0 ? n - 1 : std::min(place, n - 1);
        std::vector<int> out{mine};
        for (int k = 0; k < n - 1; ++k) out.push_back(k < mine ? k : k + 1);
        return out;
    }
};

class QuickRaceScreen : public Screen {
public:
    explicit QuickRaceScreen(App& app) : app_(app) {
        db::SpecDb* sdb = app.db();
        if (!sdb || !app.vfs()) throw FormatError("This disc's events are not read yet.");
        prologue_ = sdb->prologue();
        cars_ = CarCatalog::of(app);
        if (prologue_) read_prologue(app);
        else read_events(app);
        // The last choices.
        car_.label = config_get(app, "car");
        car_.paint = config_int(app, "paint", 0);
        if (!cars_->find(car_.label) && !cars_->cars().empty()) car_ = {cars_->cars().front().label, 0};
        if (prologue_ && !prologue_races_.count(car_.label) && !prologue_races_.empty()) car_ = {prologue_races_.begin()->first, 0};
        manual_ = config_int(app, "mt", 0) != 0;
        level_ = config_int(app, "level", 0);
        laps_ = std::clamp(config_int(app, "laps", 0), 0, 99);
        free_run_ = config_get(app, "run") == "free";
        const std::string last = config_get(app, "event");
        for (size_t i = 0; i < events_.size(); ++i)
            if (events_[i].id == last) event_ = int(i);
        custom_.read(app.config(), kCustomSection);
        if (!cars_->find(custom_.car.label)) custom_.car = car_;
        read_saved();
        pages_.push_back({Page::Top});
        script_from_env();
    }

    void update(App& app, double) override {
        if (typing_) return edit(app);
        if (!error_.empty() && app.menu().pressed(PadButton::Cross)) error_.clear();
        if (script_start_) {
            script_start_ = false;
            start(app);
            return;
        }
        if (std::exchange(script_cars_, false)) {
            app.push(make_car_picker(app, cars_, {"QUICK RACE: YOUR CAR", car_}, [this](const PickedCar& c) { car_ = c, keep(); }));
            return;
        }
        if (script_custom_start_) {
            script_custom_start_ = false;
            start_custom(app);
            return;
        }
        Page& p = pages_.back();
        build(p);
        p.rows.update(app);
        if (go_) {
            const std::function<void()> go = std::exchange(go_, {});
            go();
            return;
        }
        if (app.menu().pressed(PadButton::Triangle)) {
            if (pages_.size() > 1) pages_.pop_back();
            else app.pop();
        }
    }

    void on_event(App&, const SDL_Event& e) override {
        if (!typing_) return;
        if (e.type == SDL_EVENT_TEXT_INPUT) keys_.close(); // (a real keyboard types: ours is not needed)
        const gt3r::TextKeyboard::Result r = gt3r::text_field_event(e, typed_text_, !keys_.is_open());
        if (r != gt3r::TextKeyboard::Result::None) typed_ = r;
    }

    void render(App& app, Ui& ui) override {
        Page& p = pages_.back();
        build(p);
        if (typing_ && keys_.is_open()) {
            page_header(app, ui, "QUICK RACE", "Save the custom race");
            return keys_.draw(ui, typed_text_);
        }
        page_header(app, ui, "QUICK RACE", title(p));
        p.rows.render(ui, {20, 28, 410, 17 * 18 + 1});
        float y = 34;
        const auto line = [&](const std::string& t, Color c = colors::kText) {
            for (const std::string& l : Ui::wrap(t, 180)) ui.text(446, y, l, c), y += 12;
        };
        const QuickEvent* e = shown_event(p);
        if (e) {
            line(e->name);
            line(e->course_name, colors::kDim);
            if (e->kind == Kind::Event && e->laps > 0) line(std::to_string(e->laps) + (e->laps == 1 ? " lap" : " laps"), colors::kDim);
            if (e->kind == Kind::Event && e->grid > 1) line(std::to_string(e->grid - 1) + " opponents", colors::kDim);
            y += 8;
        }
        if (p.kind == Page::Custom) {
            line("Custom race", colors::kText);
            line(course_name(custom_.course), colors::kDim);
            const int field = field_size(custom_.course);
            line(field <= 1 ? std::string("This course races one car.") : "Up to " + std::to_string(field) + " cars on this course.", colors::kDim);
            y += 8;
            line("The opponents drive as Arcade's single race has them: the game sets no skill per driver (Arcade's difficulty chooses which cars race). Fill the grid like Arcade draws them at a level, or choose each car.", colors::kDim);
            if (typing_) y += 8, line("Type a name, then Enter.", colors::kAccent), line(typed_text_ + "_", colors::kText);
        } else if (p.kind == Page::Top)
            line("Any event of the game with any car. Nothing is unlocked and nothing is saved to your game.", colors::kDim);
        if (p.kind == Page::Setup) {
            line("The event's entry rules are not asked for. The results are not kept in your game.", colors::kDim);
        }
        if (!error_.empty()) y += 8, line(error_, colors::kBad);
        const std::string hint = p.rows.hint();
        ui.footer(hint + (hint.empty() ? "" : "  ") + "{Triangle}: back");
    }

private:
    struct Page {
        enum Kind { Top, List, Setup, Custom, Courses } kind = Top;
        size_t category = 0;
        PageRows rows;
    };

    // ---- The events ----------------------------------------------------------------------------------------------------
    // Gran Turismo 4 Prologue: its arcade's courses and cars (the configuration script's main::prologue ArcadeCourseList and
    // ArcadeCarList), the race as its arcade's setupRace sets it up.
    void read_prologue(App& app) {
        probe_ = new_menus(app);
        script::Vm& vm = probe_->vm();
        std::map<std::string, std::string> names;
        for (const db::CourseEntry& c : app.db()->courses())
            if (!c.name.empty() && latin_text(c.name)) names[c.label] = c.name;
        Category cat{"Single Race", "Arcade", {}};
        if (const std::vector<Value>* crs = vm.attribute(vm.lookup("main::prologue::ArcadeCourseList"), "crs_list").items())
            for (const Value& c : *crs) {
                QuickEvent q;
                q.kind = Kind::Arcade, q.course = vm.attribute(c, "code").to_string();
                if (q.course.empty() || !app.vfs()->exists("crs/" + q.course)) continue;
                std::string name = names.count(q.course) ? names[q.course] : q.course; // (the label, where the disc's name is not Latin)
                if (!name.empty()) name[0] = char(std::toupper(static_cast<unsigned char>(name[0])));
                q.id = "arcade:" + q.course, q.name = q.course_name = name;
                q.grid = std::max(1, int(vm.attribute(c, "max_entry").as_float()));
                prologue_dirt_[q.course] = vm.attribute(c, "is_dirt").truthy();
                cat.events.push_back(events_.size());
                events_.push_back(std::move(q));
            }
        if (const std::vector<Value>* cars = vm.attribute(vm.lookup("main::prologue::ArcadeCarList"), "car_list").items())
            for (const Value& c : *cars) prologue_races_[vm.attribute(c, "car_code").to_string()] = vm.attribute(c, "race_code").to_string();
        if (!cat.events.empty()) categories_.push_back(std::move(cat));
        LOG_INFO("Quick race: Prologue's arcade, %zu courses, %zu cars", events_.size(), prologue_races_.size());
    }

    void read_events(App& app) {
        gt3r::Vfs* vfs = app.vfs();
        const auto read = [vfs](const std::string& path) { return vfs->read(path); };
        menu::SaveEditor editor(read, menus_config(app));
        editor.start(menu::GameState{});
        const db::RaceEvents rows(*app.db(), read);
        const std::string region = app.disc() ? app.disc()->info().region : std::string("us");
        std::map<std::string, std::string> course_names;
        // (names the app's pages cannot show, the Japanese disc's, are made of the database's American names or the labels)
        for (const menu::SaveEditor::Course& c : editor.courses())
            if (latin_text(c.name)) course_names[c.label] = c.name;
        for (const db::CourseEntry& c : app.db()->courses())
            if (!course_names.count(c.label) && !c.name.empty() && latin_text(c.name)) course_names[c.label] = c.name;
        const auto course_name = [&](const std::string& label) {
            const auto it = course_names.find(label);
            return it != course_names.end() ? it->second : label;
        };
        const auto row = [&](const std::string& label) -> std::optional<db::RaceEvent> {
            for (const std::string& l : db::race_labels(label, region))
                if (std::optional<db::RaceEvent> e = rows.event(l)) return e;
            return std::nullopt;
        };
        const auto add = [&](Category& cat, QuickEvent q) {
            if (const std::optional<db::RaceEvent> e = row(q.race)) {
                q.course = e->course, q.laps = e->laps, q.grid = e->grid;
                if (q.course_name.empty() || !latin_text(q.course_name)) q.course_name = course_name(e->course);
            }
            if (!latin_text(q.name)) q.name = cat.title + " " + std::to_string(cat.events.size() + 1) + ": " + q.course_name;
            if (q.course.empty() || !app.vfs()->exists("crs/" + q.course)) return; // (an event without a course of the disc's)
            cat.events.push_back(events_.size());
            events_.push_back(std::move(q));
        };
        // Gran Turismo mode's event lists, each race of an event.
        for (const menu::SaveEditor::EventList& l : editor.events()) {
            Category cat{latin_text(l.title) ? l.title : "Events " + std::to_string(categories_.size() + 1), "Gran Turismo mode", {}};
            for (const menu::SaveEditor::Event& e : l.events)
                for (size_t r = 0; r < e.races.size(); ++r) {
                    QuickEvent q;
                    q.kind = Kind::Event, q.race = q.id = e.races[r].label, q.course_name = e.races[r].name;
                    q.name = e.races.size() > 1 ? e.name + " " + std::to_string(r + 1) + ": " + e.races[r].name : e.name;
                    add(cat, std::move(q));
                }
            if (!cat.events.empty()) categories_.push_back(std::move(cat));
        }
        // The license tests.
        for (const menu::SaveEditor::License& l : editor.licenses()) {
            std::string license = l.name;
            if (!latin_text(license)) { // ("l0b" .. : the license's letters)
                license = l.category.size() > 2 ? l.category.substr(2) : l.category;
                for (char& ch : license) ch = char(std::toupper(static_cast<unsigned char>(ch)));
                license += " License";
            }
            Category cat{license, "License tests", {}};
            for (const menu::SaveEditor::LicenseTest& t : l.tests) {
                QuickEvent q;
                q.kind = Kind::License, q.race = t.label, q.id = "license:" + t.label, q.name = t.name;
                add(cat, std::move(q));
            }
            if (!cat.events.empty()) categories_.push_back(std::move(cat));
        }
        // The missions.
        {
            Category cat{"Missions", "Missions", {}};
            for (const menu::SaveEditor::Mission& m : editor.missions()) {
                QuickEvent q;
                q.kind = Kind::Mission, q.race = m.label, q.id = "mission:" + m.label, q.name = m.name;
                add(cat, std::move(q));
            }
            if (!cat.events.empty()) categories_.push_back(std::move(cat));
        }
        // Arcade's single race: every course of the disc's lists that has a course file.
        {
            Category cat{"Single Race", "Arcade", {}};
            std::set<std::string> seen;
            for (const menu::SaveEditor::Course& c : editor.courses()) {
                if (!seen.insert(c.label).second || !app.vfs()->exists("crs/" + c.label)) continue;
                QuickEvent q;
                q.kind = Kind::Arcade, q.id = "arcade:" + c.label, q.course = c.label, q.name = q.course_name = course_name(c.label);
                cat.events.push_back(events_.size());
                events_.push_back(std::move(q));
            }
            if (!cat.events.empty()) categories_.push_back(std::move(cat));
        }
        LOG_INFO("Quick race: %zu events in %zu lists", events_.size(), categories_.size());
    }

    // ---- Pages ---------------------------------------------------------------------------------------------------------
    std::string title(const Page& p) const {
        if (p.kind == Page::List) return categories_[p.category].group + ": " + categories_[p.category].title;
        if (p.kind == Page::Setup && event_ >= 0) return events_[size_t(event_)].name;
        if (p.kind == Page::Custom) return custom_.name.empty() ? std::string("Custom race") : "Custom race: " + custom_.name;
        if (p.kind == Page::Courses) return "Custom race: the course";
        return "Choose an event";
    }
    const QuickEvent* shown_event(const Page& p) const {
        if (p.kind == Page::Setup) return event_ >= 0 ? &events_[size_t(event_)] : nullptr;
        if (p.kind == Page::List) {
            const std::vector<size_t>& list = categories_[p.category].events;
            if (p.rows.selected >= 0 && p.rows.selected < int(list.size())) return &events_[list[size_t(p.rows.selected)]];
        }
        return nullptr;
    }

    void build(Page& p) {
        PageRows& r = p.rows;
        r.rows.clear();
        if (p.kind == Page::Top) {
            if (!prologue_) r.heading("Custom race");
            if (!prologue_) r.action("Custom race", custom_.name.empty() ? course_name(custom_.course) : custom_.name, "set it up", [this] {
                go_ = [this] { open_custom(); };
            });
            for (size_t i = 0; i < saved_.size() && !prologue_; ++i) {
                PageRow& row = r.action(saved_[i].name, course_name(saved_[i].course), "set it up", [this, i] {
                    go_ = [this, i] { custom_ = saved_[i], open_custom(); };
                });
                row.other = [this, i] { go_ = [this, i] { remove_saved(i); }; }, row.other_hint = "delete it";
            }
            if (event_ >= 0) r.action("Last event", events_[size_t(event_)].name, "set it up", [this] { go_ = [this] { open_setup(); }; });
            std::string group = prologue_ ? std::string() : std::string("Custom race");
            for (size_t i = 0; i < categories_.size(); ++i) {
                if (categories_[i].group != group) r.heading(group = categories_[i].group);
                r.action(categories_[i].title, std::to_string(categories_[i].events.size()), "list its events", [this, i] {
                    go_ = [this, i] {
                        Page n{Page::List, i, {}};
                        const std::vector<size_t>& list = categories_[i].events;
                        for (size_t k = 0; k < list.size(); ++k)
                            if (int(list[k]) == event_) n.rows.selected = int(k), n.rows.scroll.first = std::max(0, int(k) - 8);
                        pages_.push_back(std::move(n));
                    };
                });
            }
            return;
        }
        if (p.kind == Page::List) {
            for (size_t k : categories_[p.category].events) {
                const QuickEvent& e = events_[k];
                r.action(e.name, e.kind == Kind::Arcade || e.name.find(e.course_name) != std::string::npos ? std::string() : e.course_name, "set it up", [this, k] {
                    go_ = [this, k] { event_ = int(k), open_setup(); };
                });
            }
            return;
        }
        if (p.kind == Page::Courses) {
            for (const std::string& c : course_list_)
                r.action(course_name(c), "", "race here", [this, c] {
                    go_ = [this, c] {
                        custom_.course = c;
                        fit_field(), keep_custom(), pages_.pop_back();
                    };
                });
            return;
        }
        if (p.kind == Page::Custom) {
            build_custom(r);
            return;
        }
        // The setup.
        if (event_ < 0) return;
        const QuickEvent& e = events_[size_t(event_)];
        r.info("Course", e.course_name);
        r.action("Car", cars_->name(car_.label), "choose another", [this] {
            go_ = [this] {
                CarPickerOptions o;
                o.title = "QUICK RACE: YOUR CAR", o.now = car_;
                if (prologue_) o.allowed = [this](const CarCatalog::Car& c) { return prologue_races_.count(c.label) > 0; }; // (its arcade's cars)
                app_.push(make_car_picker(app_, cars_, o, [this](const PickedCar& c) { car_ = c, keep(); }));
            };
        });
        {
            const std::vector<CarCatalog::Paint>& paints = cars_->paints(car_.label);
            std::vector<std::string> names;
            for (const CarCatalog::Paint& c : paints) names.push_back(c.name);
            r.choice("Color", names, size_t(std::clamp(car_.paint, 0, int(names.size()) - 1)), [this](size_t i) { car_.paint = int(i), keep(); });
        }
        r.choice("Transmission", {"Automatic", "Manual"}, manual_ ? 1 : 0, [this](size_t i) { manual_ = i == 1, keep(); });
        if (e.kind == Kind::Arcade && prologue_) {
            r.choice("Difficulty", {"1", "2", "3"}, size_t(std::clamp(level_, 0, 2)), [this](size_t i) { level_ = int(i), keep(); });
            std::vector<std::string> laps{"The race's"};
            for (int n = 1; n <= 99; ++n) laps.push_back(std::to_string(n));
            r.choice("Laps", laps, size_t(laps_), [this](size_t i) { laps_ = int(i), keep(); });
        } else if (e.kind == Kind::Arcade) {
            // The difficulty as Arcade's slider has it: -1 .. 1 for a car with races of its own on this kind of course, else -10 .. 10.
            const int lo = arcade_has_races() ? -1 : -10, hi = -lo;
            std::vector<std::string> names;
            for (int v = lo; v <= hi; ++v) names.push_back(v > 0 ? "+" + std::to_string(v) : std::to_string(v));
            r.choice("Difficulty", names, size_t(std::clamp(level_, lo, hi) - lo), [this, lo](size_t i) { level_ = int(i) + lo, keep(); });
            std::vector<std::string> laps{"Your option's"};
            for (int n = 1; n <= 99; ++n) laps.push_back(std::to_string(n));
            r.choice("Laps", laps, size_t(laps_), [this](size_t i) { laps_ = int(i), keep(); });
        } else if (e.kind == Kind::License) {
            r.choice("Run", {"The test", "A free run on its course"}, free_run_ ? 1 : 0, [this](size_t i) { free_run_ = i == 1, keep(); });
        } else {
            if (e.laps > 0) r.info("Laps", std::to_string(e.laps));
            if (e.grid > 1) r.info("Opponents", std::to_string(e.grid - 1));
        }
        r.action("Start", "", "start the race", [this] { go_ = [this] { start(app_); }; });
    }

    // ---- The custom race ------------------------------------------------------------------------------------------------
    void open_custom() {
        if (course_list_.empty())
            for (const QuickEvent& e : events_)
                if (e.kind == Kind::Arcade) course_list_.push_back(e.course);
        if (custom_.course.empty() && !course_list_.empty()) custom_.course = course_list_.front();
        if (!cars_->find(custom_.car.label)) custom_.car = car_;
        fit_field();
        if (custom_.opponents.empty() && field_size(custom_.course) > 1) fill_like_arcade(std::min(5, field_size(custom_.course) - 1));
        pages_.push_back({Page::Custom});
        keep_custom();
    }
    std::string course_name(const std::string& label) const {
        for (const QuickEvent& e : events_)
            if (e.kind == Kind::Arcade && e.course == label) return e.course_name;
        return label;
    }
    // The course's most cars (MCourseData.GetMaxCarNum: COURSE +0x11), the player's included.
    int field_size(const std::string& course) {
        if (course.empty()) return 1;
        if (const auto it = fields_.find(course); it != fields_.end()) return it->second;
        int n = 1;
        try {
            if (!probe_) probe_ = new_menus(app_);
            script::Vm& vm = probe_->vm();
            n = std::max(1, int(call(vm, "main::menu::MCourseData::GetMaxCarNum", {Value::string(course)}).as_float()));
        } catch (const std::exception& e) {
            LOG_WARN("Quick race: the course's field: %s", e.what());
        }
        return fields_[course] = n;
    }
    void fit_field() {
        const int most = field_size(custom_.course) - 1;
        if (int(custom_.opponents.size()) > most) custom_.opponents.resize(size_t(std::max(0, most)));
        if (custom_.place > int(custom_.opponents.size())) custom_.place = -1;
    }
    // `count` opponents as Arcade draws them for the player's car on the course at Arcade's level (GameRoot.set_enemy_cars in
    // Arcade's own script: MCarData.PickUpEnemyCars around the car's evaluation); places past what it drew keep their cars.
    void fill_like_arcade(int count) {
        try {
            if (!probe_) probe_ = new_menus(app_);
            menu::Runtime& rt = *probe_;
            if (!probe_arcade_) rt.start_project("arcade"), probe_arcade_ = true;
            script::Vm& vm = rt.vm();
            const auto set = [&](const char* module, const char* name, const Value& v) {
                if (std::shared_ptr<script::Reference> m = vm.member(vm.lookup(module), name)) m->set(vm, v);
            };
            menu::RaceRequest& q = rt.game().race;
            set("ArcadeProject::CourseRoot", "current_crs", Value::string(custom_.course));
            set("ArcadeProject::CarRoot", "current_car", Value::string(custom_.car.label));
            set("ArcadeProject::CarRoot", "current_col", Value::integer(custom_.car.paint));
            set("ArcadeProject::GameRoot", "current_game", Value::string("SingleRace"));
            q.course = custom_.course, q.car = custom_.car.label, q.paint = custom_.car.paint, q.enemy_level = std::clamp(fill_level_, -10, 10);
            q.difficulty = "NORMAL", q.overwrite_enemy_num = 0, q.max_entry = count + 1, q.entries.clear(), q.entry_paints.clear();
            call(vm, "ArcadeProject::GameRoot::set_enemy_cars", {Value::string(custom_.course)});
            std::vector<PickedCar> drawn;
            for (int i = 1; i <= q.overwrite_enemy_num && i < int(q.entries.size()); ++i)
                if (!q.entries[size_t(i)].empty()) drawn.push_back({q.entries[size_t(i)], i < int(q.entry_paints.size()) ? q.entry_paints[size_t(i)] : 0});
            if (drawn.empty()) {
                error_ = "Arcade draws no opponents for this car: choose them one by one.";
                return;
            }
            std::vector<PickedCar> out = drawn;
            for (size_t k = out.size(); k < size_t(count) && k < custom_.opponents.size(); ++k) out.push_back(custom_.opponents[k]);
            while (int(out.size()) < count) out.push_back(drawn[out.size() % drawn.size()]);
            custom_.opponents = out;
        } catch (const std::exception& e) {
            error_ = std::string("Arcade's draw failed: ") + e.what();
            LOG_WARN("Quick race: %s", error_.c_str());
        }
    }
    void build_custom(PageRows& r) {
        r.action("Course", course_name(custom_.course), "choose another", [this] {
            go_ = [this] {
                Page n{Page::Courses, 0, {}};
                for (size_t i = 0; i < course_list_.size(); ++i)
                    if (course_list_[i] == custom_.course) n.rows.selected = int(i), n.rows.scroll.first = std::max(0, int(i) - 8);
                pages_.push_back(std::move(n));
            };
        });
        {
            std::vector<std::string> laps;
            for (int n = 1; n <= kMaxLaps; ++n) laps.push_back(std::to_string(n));
            r.choice("Laps", laps, size_t(std::clamp(custom_.laps, 1, kMaxLaps) - 1), [this](size_t i) { custom_.laps = int(i) + 1, keep_custom(); });
        }
        r.action("Your car", cars_->name(custom_.car.label) + ", " + paint_name(custom_.car), "choose another", [this] {
            go_ = [this] { pick(-1); };
        });
        r.choice("Transmission", {"Automatic", "Manual"}, manual_ ? 1 : 0, [this](size_t i) { manual_ = i == 1, keep(); });
        const int most = field_size(custom_.course) - 1;
        if (most <= 0) {
            r.info("Opponents", "none on this course");
        } else {
            std::vector<std::string> counts;
            for (int n = 1; n <= most; ++n) counts.push_back(std::to_string(n));
            const int now = std::clamp(int(custom_.opponents.size()), 1, most);
            r.choice("Opponents", counts, size_t(now - 1), [this](size_t i) {
                const int n = int(i) + 1;
                if (n < int(custom_.opponents.size())) custom_.opponents.resize(size_t(n));
                else if (n > int(custom_.opponents.size())) fill_like_arcade(n);
                fit_field(), keep_custom();
            });
            std::vector<std::string> places;
            const int cars = int(custom_.opponents.size()) + 1;
            for (int k = 1; k <= cars; ++k) places.push_back(k == 1 ? std::string("1st (pole)") : k == 2 ? "2nd" : k == 3 ? "3rd" : std::to_string(k) + "th");
            const int mine = custom_.place < 0 ? cars - 1 : std::min(custom_.place, cars - 1);
            r.choice("Your place", places, size_t(mine), [this, cars](size_t i) { custom_.place = int(i) == cars - 1 ? -1 : int(i), keep_custom(); });
            std::vector<std::string> levels;
            for (int v = -10; v <= 10; ++v) levels.push_back(v > 0 ? "+" + std::to_string(v) : std::to_string(v));
            PageRow& fill = r.choice("Fill the grid like Arcade", levels, size_t(std::clamp(fill_level_, -10, 10) + 10), [this](size_t i) { fill_level_ = int(i) - 10, keep(); });
            fill.press = [this] { go_ = [this] { fill_like_arcade(int(custom_.opponents.size())), keep_custom(); }; };
            fill.press_hint = "draw the opponents at this level";
            for (size_t k = 0; k < custom_.opponents.size(); ++k) {
                const PickedCar& o = custom_.opponents[k];
                r.action("Opponent " + std::to_string(k + 1), cars_->name(o.label) + ", " + paint_name(o), "choose another", [this, k] { go_ = [this, k] { pick(int(k)); }; });
            }
        }
        r.action("Save as...", custom_.name, "keep it under a name", [this] { go_ = [this] { ask_name(); }; });
        r.action("Start", "", "start the race", [this] { go_ = [this] { start_custom(app_); }; });
    }
    std::string paint_name(const PickedCar& c) {
        const std::vector<CarCatalog::Paint>& p = cars_->paints(c.label);
        return p[size_t(std::clamp(c.paint, 0, int(p.size()) - 1))].name;
    }
    void pick(int who) { // -1 the player's car
        CarPickerOptions o;
        o.title = who < 0 ? std::string("CUSTOM RACE: YOUR CAR") : "CUSTOM RACE: OPPONENT " + std::to_string(who + 1);
        o.now = who < 0 ? custom_.car : custom_.opponents[size_t(who)];
        app_.push(make_car_picker(app_, cars_, o, [this, who](const PickedCar& c) {
            if (who < 0) custom_.car = c;
            else if (who < int(custom_.opponents.size())) custom_.opponents[size_t(who)] = c;
            keep_custom();
        }));
    }
    void keep_custom() {
        custom_.write(app_.config(), kCustomSection);
        keep();
    }
    void read_saved() {
        saved_.clear();
        for (int n = 1; n < 1000; ++n) {
            const std::string section = kSavedSection + std::to_string(n);
            if (!app_.config().find(section)) break;
            CustomRace c;
            c.read(app_.config(), section);
            if (!c.name.empty()) saved_.push_back(std::move(c));
        }
    }
    void write_saved() {
        std::vector<gt3r::IniDocument::Section>& all = app_.config().sections();
        all.erase(std::remove_if(all.begin(), all.end(), [](const gt3r::IniDocument::Section& s) { return s.name.rfind(kSavedSection, 0) == 0; }), all.end());
        for (size_t i = 0; i < saved_.size(); ++i) saved_[i].write(app_.config(), kSavedSection + std::to_string(i + 1));
        app_.save_config();
    }
    void save_as(const std::string& name) {
        custom_.name = name;
        bool replaced = false;
        for (CustomRace& c : saved_)
            if (c.name == name) c = custom_, replaced = true; // (the same name replaces it)
        if (!replaced) saved_.push_back(custom_);
        write_saved(), keep_custom();
    }
    void remove_saved(size_t i) {
        if (i >= saved_.size()) return;
        if (custom_.name == saved_[i].name) custom_.name.clear(), keep_custom();
        saved_.erase(saved_.begin() + long(i));
        write_saved();
    }
    void ask_name() {
        typed_text_ = custom_.name, typed_ = gt3r::TextKeyboard::Result::None;
        typing_ = true, typing_started_ = false;
        if (!Ui::prompts_keyboard()) keys_.open("Name of this race", "Save", true);
    }
    void edit(App& app) {
        if (!typing_started_) {
            typing_started_ = true;
            window_ = SDL_GetRenderWindow(app.renderer());
            if (window_) SDL_StartTextInput(window_);
            return; // (the press that opened it is not its first key)
        }
        gt3r::TextKeyboard::Result r = std::exchange(typed_, gt3r::TextKeyboard::Result::None);
        if (keys_.is_open())
            if (const gt3r::TextKeyboard::Result pressed = keys_.update(app.menu(), {}, typed_text_); pressed != gt3r::TextKeyboard::Result::None) r = pressed;
        if (r == gt3r::TextKeyboard::Result::None) return;
        typing_ = false;
        keys_.close();
        if (window_) SDL_StopTextInput(window_), window_ = nullptr;
        const std::string t(gt3r::trim(typed_text_));
        if (r == gt3r::TextKeyboard::Result::Done && !t.empty()) save_as(t);
    }

    // Arcade's single race (its race row sample_entry_6: its start, tires and drivers) with the custom race's cars and places.
    void start_custom(App& app) {
        if (custom_.course.empty() || !cars_->find(custom_.car.label)) return;
        try {
            std::unique_ptr<menu::Runtime> rt = new_menus(app);
            rt->start_project("arcade");
            menu::GameState& g = rt->game();
            menu::RaceRequest& q = g.race;
            q = menu::RaceRequest{};
            q.sequence = "single_race", q.race = "sample_entry_6", q.course = custom_.course, q.laps = std::clamp(custom_.laps, 1, kMaxLaps);
            q.car = custom_.car.label, q.paint = custom_.car.paint, q.manual = manual_ ? 1 : 0, q.garage_slot = -1;
            q.assist_asm = g.option_int("assist_asm", 0), q.assist_tcs = g.option_int("assist_tcs", 0);
            const int n = std::min(int(custom_.opponents.size()), field_size(custom_.course) - 1);
            q.max_entry = n + 1, q.overwrite_enemy_num = 0;
            script::Vm& vm = rt->vm();
            const Value game = vm.lookup("main::game");
            const Value shift = vm.lookup("main::menu::MCarData::ShiftCarCode");
            // The entry table as Arcade's doGame leaves it: every entry through ShiftCarCode (the dirt and snow versions).
            for (int i = 0; i <= n; ++i) {
                const PickedCar& c = i == 0 ? custom_.car : custom_.opponents[size_t(i - 1)];
                const Value car = call(vm, shift, {Value::string(c.label), Value::string(custom_.course)});
                call(vm, vm.attribute(game, "setEntryCarCode"), {Value::integer(i), car, Value::integer(c.paint)});
            }
            q.overwrite_enemy_num = n;
            const Value type = call(vm, "main::menu::MCourseData::GetType", {Value::string(custom_.course)});
            const bool loose = type.as_string() == "dirt" || type.as_string() == "snow";
            const bool normal = call(vm, "main::menu::MCarData::IsTireNormal", {Value::string(custom_.car.label)}).truthy();
            q.tire = loose ? 11 : normal ? 4 : 1 + 6;
            g.major_menu_project = "arcade", g.next_sequence = "single_race", g.next_menu_project = "arcade";
            g.options["automatic_gear"] = s32(manual_ ? 0 : 1);
            menu::start_quick_work(*rt, q);
            RaceStart s;
            s.course = q.course, s.car = q.car, s.paint = q.paint, s.cars = n + 1, s.laps = q.laps;
            s.menus = rt.get(), s.sequence = "single_race";
            custom_.place = std::min(custom_.place, n);
            s.grid_slots = custom_.grid_slots();
            LOG_INFO("Quick race: custom race at %s, %d laps, %s and %d opponents, the player %d", q.course.c_str(), q.laps, q.car.c_str(), n, s.grid_slots[0] + 1);
            std::unique_ptr<Screen> race = make_race_screen(app, s);
            race_menus_ = std::move(rt);
            error_.clear();
            app.push(std::move(race));
        } catch (const std::exception& ex) {
            error_ = std::string("The race could not be started: ") + ex.what();
            LOG_WARN("Quick race: %s", error_.c_str());
        }
    }

    void open_setup() {
        Page n{Page::Setup, 0, {}};
        pages_.push_back(std::move(n));
        keep();
    }

    void keep() {
        gt3r::IniDocument& c = app_.config();
        if (event_ >= 0) c.set(kSection, "event", events_[size_t(event_)].id);
        c.set(kSection, "car", car_.label), c.set(kSection, "paint", std::to_string(car_.paint));
        c.set(kSection, "mt", manual_ ? "1" : "0"), c.set(kSection, "level", std::to_string(level_)), c.set(kSection, "laps", std::to_string(laps_));
        c.set(kSection, "run", free_run_ ? "free" : "test");
        app_.save_config();
    }

    // ---- The race ------------------------------------------------------------------------------------------------------
    std::unique_ptr<menu::Runtime> new_menus(App& app) {
        gt3r::Vfs* vfs = app.vfs();
        auto rt = std::make_unique<menu::Runtime>([vfs](const std::string& path) { return vfs->read(path); }, menus_config(app));
        rt->boot();
        menu::hold_every_license(*rt); // (nothing to unlock: an event that asks for a license offers A-spec)
        // The player's options (the view, the units, the sound's volumes) as the data folder keeps them, read only.
        if (std::FILE* f = std::fopen((app.save_dir() + "/gt4r-options.txt").c_str(), "rb")) {
            std::string t;
            char buf[4096];
            for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) t.append(buf, n);
            std::fclose(f);
            rt->game().options_from_text(t);
        }
        return rt;
    }

    // Arcade's car has races of its own on the course's kind (MCarData.GetRaceCode, ARCADEINFO_NORMAL).
    bool arcade_has_races() {
        if (event_ < 0 || events_[size_t(event_)].kind != Kind::Arcade) return false;
        const std::string key = car_.label + "@" + events_[size_t(event_)].course;
        if (const auto it = has_races_.find(key); it != has_races_.end()) return it->second;
        bool has = false;
        try {
            if (!probe_) probe_ = new_menus(app_);
            script::Vm& vm = probe_->vm();
            const Value type = call(vm, "main::menu::MCourseData::GetType", {Value::string(events_[size_t(event_)].course)});
            const int kind = type.as_string() == "wet" ? 1 : type.as_string() == "dirt" ? 2 : type.as_string() == "snow" ? 3 : 0;
            has = !call(vm, "main::menu::MCarData::GetRaceCode", {Value::string(car_.label), Value::integer(kind), Value::string("NORMAL")}).is_nil();
        } catch (const std::exception& e) {
            LOG_WARN("Quick race: the car's Arcade races: %s", e.what());
        }
        return has_races_[key] = has;
    }

    // Arcade's single race as GameRoot.doGame sets it up for "SingleRace", in Arcade's own script.
    void arcade_setup(menu::Runtime& rt, const QuickEvent& e) {
        menu::GameState& g = rt.game();
        menu::RaceRequest& q = g.race;
        rt.start_project("arcade");
        script::Vm& vm = rt.vm();
        const auto set = [&](const char* module, const char* name, const Value& v) {
            if (std::shared_ptr<script::Reference> m = vm.member(vm.lookup(module), name)) m->set(vm, v);
        };
        set("ArcadeProject::CourseRoot", "current_crs", Value::string(e.course));
        set("ArcadeProject::CarRoot", "current_car", Value::string(car_.label));
        set("ArcadeProject::CarRoot", "current_col", Value::integer(car_.paint));
        set("ArcadeProject::GameRoot", "current_game", Value::string("SingleRace"));
        q.course = e.course, q.car = car_.label, q.paint = car_.paint;
        if (arcade_has_races()) q.difficulty = level_ < 0 ? "EASY" : level_ > 0 ? "HARD" : "NORMAL";
        else q.enemy_level = std::clamp(level_, -10, 10);
        q.overwrite_enemy_num = 0;
        g.major_menu_project = "arcade", g.next_sequence = "single_race", g.next_menu_project = "arcade";
        // The field: the course's most cars, two for an open car of model type 2 (the open-top option's page is not asked).
        int max_entry = int(call(vm, "main::menu::MCourseData::GetMaxCarNum", {Value::string(e.course)}).as_float());
        if (call(vm, "main::menu::MCarData::GetOpenModelType", {Value::string(car_.label)}).as_float() == 2 && max_entry > 2) max_entry = 2;
        q.max_entry = std::max(1, max_entry);
        call(vm, "ArcadeProject::GameRoot::set_enemy_cars", {Value::string(e.course)});
        // Every entry through ShiftCarCode (the dirt and snow versions).
        const Value game = vm.lookup("main::game");
        const Value shift = vm.lookup("main::menu::MCarData::ShiftCarCode");
        for (int i = 0; i < q.overwrite_enemy_num + 1; ++i) {
            const Value got = call(vm, vm.attribute(game, "getEntryCarCode"), {Value::integer(i)});
            const std::vector<Value>* pair = got.items();
            if (!pair || pair->size() < 2) continue;
            const Value car = call(vm, shift, {(*pair)[0], Value::string(e.course)});
            call(vm, vm.attribute(game, "setEntryCarCode"), {Value::integer(i), car, (*pair)[1]});
        }
        if (laps_ > 0) q.laps = laps_;
        // The setting page's tire: on dirt 11, else its default (the fifth of the normal tires, the second of the racing ones).
        const Value type = call(vm, "main::menu::MCourseData::GetType", {Value::string(e.course)});
        const bool loose = type.as_string() == "dirt" || type.as_string() == "snow";
        const bool normal = call(vm, "main::menu::MCarData::IsTireNormal", {Value::string(car_.label)}).truthy();
        q.tire = loose ? 11 : normal ? 4 : 1 + 6;
    }

    // Prologue's arcade's setupRace: its dirt race on a dirt course (the car's dirt version), else the car's own race, "_h" or "_p"
    // after it at the second and third difficulty; the course's field.
    void prologue_setup(menu::Runtime& rt, const QuickEvent& e) {
        script::Vm& vm = rt.vm();
        menu::RaceRequest& q = rt.game().race;
        const bool dirt = prologue_dirt_[e.course];
        std::string race = dirt ? vm.lookup("main::prologue::DIRT_RACECODE").to_string() : prologue_races_[car_.label];
        const int level = std::clamp(level_, 0, 2);
        if (level == 1) race += "_h";
        if (level == 2) race += "_p";
        q.race = race, q.course = e.course, q.max_entry = e.grid, q.paint = car_.paint;
        q.car = dirt ? vm.lookup("main::prologue::DIRT_PREFIX").to_string() + car_.label : car_.label;
        rt.game().options["difficulty"] = s32(level);
        if (laps_ > 0) q.laps = laps_;
    }

    void start(App& app) {
        if (event_ < 0) return;
        const QuickEvent& e = events_[size_t(event_)];
        try {
            std::unique_ptr<menu::Runtime> rt = new_menus(app);
            menu::GameState& g = rt->game();
            menu::RaceRequest& q = g.race;
            q = menu::RaceRequest{};
            q.car = car_.label, q.paint = car_.paint, q.manual = manual_ ? 1 : 0, q.garage_slot = -1;
            q.assist_asm = g.option_int("assist_asm", 0), q.assist_tcs = g.option_int("assist_tcs", 0);
            std::string sequence;
            switch (e.kind) {
            case Kind::Event:
                // The event page's entry: the race's label, no course, no opponents of the scripts'.
                sequence = "solo_event", q.race = e.race, q.course = e.course, q.max_entry = e.grid > 0 ? e.grid : 6;
                break;
            case Kind::License:
                if (free_run_) sequence = "freerun", q.course = e.course, q.max_entry = 1;
                else sequence = "license", q.race = e.race, q.course = e.course, q.max_entry = std::max(1, e.grid);
                break;
            case Kind::Mission: sequence = "mission", q.race = e.race, q.course = e.course, q.max_entry = std::max(1, e.grid); break;
            case Kind::Arcade:
                sequence = "single_race";
                if (prologue_) prologue_setup(*rt, e);
                else arcade_setup(*rt, e);
                break;
            }
            q.sequence = sequence;
            g.next_sequence = sequence;
            g.options["automatic_gear"] = s32(manual_ ? 0 : 1); // (the gearbox a race of the menus reads)
            menu::start_quick_work(*rt, q);
            RaceStart s;
            s.course = q.course, s.car = q.car, s.paint = q.paint, s.cars = std::max(1, q.max_entry), s.laps = q.laps > 0 ? q.laps : 3;
            s.menus = rt.get(), s.sequence = sequence;
            LOG_INFO("Quick race: %s (%s) at %s with %s, paint %d, %d cars", e.name.c_str(), sequence.c_str(), q.course.c_str(), q.car.c_str(), q.paint, s.cars);
            std::unique_ptr<Screen> race = make_race_screen(app, s);
            race_menus_ = std::move(rt); // (they outlive the race screen: it runs its menus on them)
            error_.clear();
            app.push(std::move(race));
        } catch (const std::exception& ex) {
            error_ = std::string("The race could not be started: ") + ex.what();
            LOG_WARN("Quick race: %s", error_.c_str());
        }
    }

    void script_from_env() {
        const char* env = SDL_getenv("GT4R_QUICKRACE");
        if (!env) return;
        std::string page, save_name;
        bool fill = false;
        for (std::string_view kv : gt3r::split(env, ',')) {
            const size_t eq = kv.find('=');
            const std::string k(gt3r::trim(kv.substr(0, eq))), v(eq == std::string_view::npos ? std::string() : std::string(gt3r::trim(kv.substr(eq + 1))));
            if (k == "event") {
                event_ = -1;
                for (size_t i = 0; i < events_.size(); ++i)
                    if (events_[i].id == v || events_[i].race == v) event_ = int(i);
                if (event_ < 0) {
                    std::string near;
                    for (const QuickEvent& e : events_)
                        if (e.id.find(v) != std::string::npos && near.size() < 400) near += " " + e.id;
                    LOG_WARN("Quick race: no event %s (%s)", v.c_str(), near.c_str());
                }
            } else if (k == "car") car_.label = v;
            else if (k == "paint") car_.paint = std::atoi(v.c_str());
            else if (k == "mt") manual_ = v != "0";
            else if (k == "level") level_ = std::atoi(v.c_str());
            else if (k == "laps") laps_ = std::clamp(std::atoi(v.c_str()), 0, 99);
            else if (k == "run") free_run_ = v == "free";
            else if (k == "page") page = v;
            else if (k == "start") script_start_ = true;
            else if (k == "course") custom_.course = v;
            else if (k == "place") custom_.place = std::atoi(v.c_str()) - 1;
            else if (k == "race") {
                for (const CustomRace& c : saved_)
                    if (c.name == v) custom_ = c, page = "custom";
            } else if (k == "grid") {
                custom_.opponents.clear();
                for (std::string_view o : gt3r::split(v, '/')) custom_.opponents.push_back(CustomRace::parse(std::string(gt3r::trim(o))));
            } else if (k == "save") save_name = v;
            else if (k == "fill") fill_level_ = std::atoi(v.c_str()), fill = true;
        }
        if (page == "custom") {
            const int laps = laps_;
            if (laps > 0) custom_.laps = laps;
            if (cars_->find(car_.label) && SDL_getenv("GT4R_QUICKRACE") && std::string(SDL_getenv("GT4R_QUICKRACE")).find("car=") != std::string::npos) custom_.car = car_;
            open_custom();
            if (fill) fill_like_arcade(int(custom_.opponents.size()));
            if (!save_name.empty()) save_as(save_name);
            if (script_start_) script_start_ = false, script_custom_start_ = true;
            return;
        }
        if (event_ >= 0 && (page == "setup" || page == "cars")) pages_.push_back({Page::Setup});
        script_cars_ = page == "cars";
        if (page == "list" && event_ >= 0)
            for (size_t c = 0; c < categories_.size(); ++c)
                for (size_t k = 0; k < categories_[c].events.size(); ++k)
                    if (int(categories_[c].events[k]) == event_) {
                        Page n{Page::List, c, {}};
                        n.rows.selected = int(k);
                        pages_.push_back(std::move(n));
                    }
    }

    App& app_;
    std::shared_ptr<CarCatalog> cars_;
    std::vector<QuickEvent> events_;
    std::vector<Category> categories_;
    std::vector<Page> pages_;
    std::function<void()> go_;
    int event_ = -1;
    PickedCar car_;
    bool manual_ = false, free_run_ = false, script_start_ = false;
    int level_ = 0, laps_ = 0;
    std::map<std::string, bool> has_races_;
    std::unique_ptr<menu::Runtime> probe_;      // menus for the natives' answers on the setup page
    std::unique_ptr<menu::Runtime> race_menus_; // the last race's
    // The custom race.
    CustomRace custom_;
    std::vector<CustomRace> saved_;
    std::vector<std::string> course_list_;
    std::map<std::string, int> fields_;
    int fill_level_ = 0;
    // Prologue's arcade: the cars' own races, the dirt courses.
    bool prologue_ = false;
    std::map<std::string, std::string> prologue_races_;
    std::map<std::string, bool> prologue_dirt_;
    bool probe_arcade_ = false, script_custom_start_ = false, script_cars_ = false;
    // A name typed.
    gt3r::TextKeyboard keys_;
    gt3r::TextKeyboard::Result typed_ = gt3r::TextKeyboard::Result::None;
    std::string typed_text_;
    bool typing_ = false, typing_started_ = false;
    SDL_Window* window_ = nullptr;
    std::string error_;
};

} // namespace

std::unique_ptr<Screen> make_quick_race_screen(App& app) { return std::make_unique<QuickRaceScreen>(app); }

} // namespace gt4r
