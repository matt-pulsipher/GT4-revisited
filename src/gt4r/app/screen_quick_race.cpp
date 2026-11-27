// Quick race: any event of the game with any car, nothing to unlock, nothing written to the saved game. Gran Turismo mode's events
// (each race of its event lists, championships' races one at a time), the license tests (the test itself, or a free run on its
// course), the missions and Arcade's single race on any course, each with the player's choice of car and color (car_picker.h).
//
// Pages: the event lists, an event list's events, then the setup (car, color, gearbox; Arcade: difficulty and laps; a license test:
// the test or a free run) and Start. The race runs on menus of its own (a new game's, booted for it, with the player's options read
// from the data folder and never written back): the event's own race sequence as Gran Turismo mode starts it (the event page's
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

#include <algorithm>
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
    if (const std::vector<std::string> languages = menu::project_languages(config.list, "boot"); !languages.empty()) {
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

class QuickRaceScreen : public Screen {
public:
    explicit QuickRaceScreen(App& app) : app_(app) {
        db::SpecDb* sdb = app.db();
        if (!sdb || !app.vfs()) throw FormatError("This disc's events are not read yet.");
        if (sdb->prologue()) throw FormatError("Quick race is not ready for Gran Turismo 4 Prologue yet.");
        cars_ = CarCatalog::of(app);
        read_events(app);
        // The last choices.
        car_.label = config_get(app, "car");
        car_.paint = config_int(app, "paint", 0);
        if (!cars_->find(car_.label) && !cars_->cars().empty()) car_ = {cars_->cars().front().label, 0};
        manual_ = config_int(app, "mt", 0) != 0;
        level_ = config_int(app, "level", 0);
        laps_ = std::clamp(config_int(app, "laps", 0), 0, 99);
        free_run_ = config_get(app, "run") == "free";
        const std::string last = config_get(app, "event");
        for (size_t i = 0; i < events_.size(); ++i)
            if (events_[i].id == last) event_ = int(i);
        pages_.push_back({Page::Top});
        script_from_env();
    }

    void update(App& app, double) override {
        if (!error_.empty() && app.menu().pressed(PadButton::Cross)) error_.clear();
        if (script_start_) {
            script_start_ = false;
            start(app);
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

    void render(App& app, Ui& ui) override {
        Page& p = pages_.back();
        build(p);
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
        if (p.kind == Page::Top)
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
        enum Kind { Top, List, Setup } kind = Top;
        size_t category = 0;
        PageRows rows;
    };

    // ---- The events ----------------------------------------------------------------------------------------------------
    void read_events(App& app) {
        gt3r::Vfs* vfs = app.vfs();
        const auto read = [vfs](const std::string& path) { return vfs->read(path); };
        menu::SaveEditor editor(read, menus_config(app));
        editor.start(menu::GameState{});
        const db::RaceEvents rows(*app.db(), read);
        const std::string region = app.disc() ? app.disc()->info().region : std::string("us");
        std::map<std::string, std::string> course_names;
        for (const menu::SaveEditor::Course& c : editor.courses()) course_names[c.label] = c.name;
        for (const db::CourseEntry& c : app.db()->courses())
            if (!course_names.count(c.label) && !c.name.empty()) course_names[c.label] = c.name;
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
                if (q.course_name.empty()) q.course_name = course_name(e->course);
            }
            if (q.course.empty() || !app.vfs()->exists("crs/" + q.course)) return; // (an event without a course of the disc's)
            cat.events.push_back(events_.size());
            events_.push_back(std::move(q));
        };
        // Gran Turismo mode's event lists, each race of an event.
        for (const menu::SaveEditor::EventList& l : editor.events()) {
            Category cat{l.title, "Gran Turismo mode", {}};
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
            Category cat{l.name, "License tests", {}};
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
                q.kind = Kind::Arcade, q.id = "arcade:" + c.label, q.name = c.name, q.course = c.label, q.course_name = c.name;
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
            if (event_ >= 0) r.action("Last event", events_[size_t(event_)].name, "set it up", [this] { go_ = [this] { open_setup(); }; });
            std::string group;
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
        // The setup.
        if (event_ < 0) return;
        const QuickEvent& e = events_[size_t(event_)];
        r.info("Course", e.course_name);
        r.action("Car", cars_->name(car_.label), "choose another", [this] {
            go_ = [this] {
                CarPickerOptions o;
                o.title = "QUICK RACE: YOUR CAR", o.now = car_;
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
        if (e.kind == Kind::Arcade) {
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
                arcade_setup(*rt, e);
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
        std::string page;
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
        }
        if (event_ >= 0 && (page == "setup" || page == "cars")) pages_.push_back({Page::Setup});
        if (page == "cars") app_.push(make_car_picker(app_, cars_, {"QUICK RACE: YOUR CAR", car_}, [this](const PickedCar& c) { car_ = c; }));
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
    std::string error_;
};

} // namespace

std::unique_ptr<Screen> make_quick_race_screen(App& app) { return std::make_unique<QuickRaceScreen>(app); }

} // namespace gt4r
