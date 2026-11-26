// Drive: pick a car and a course, then a race on the course's road: the first car driven with
// the pad, the others by a simple driver, all on the simulation behind race/simulation.h.
//
// GT4R_RACE="course=tsukuba,car=<label>,cars=6,view=chase,drive=auto,skip=600,paint=1,wing=p001,hud=1,sim=rail"
// sets it up without the lists, for pictures: `course` a course file's name, `car` a car's label
// (or its model's name), `cars` how many on the grid, `view` in | chase | roof | back, `drive`
// auto (the simple driver takes the first car too) or pad, `skip` simulation steps run before the
// first picture (the start's hold is left out then), `paint` the first car's paint, `laps` the
// race's length, `start` the start: 10 or 8 (a hold of so many seconds with the opening shots),
// <seconds> (a plain hold), none, rolling, course (the course's own: race/start_rule.h); `view=tv` the course's replay cameras; `driver=0` no drivers in the cars; (GT4R_DEMO_LOG=<n> logs a demonstration's car every n steps); `replay=N` drives
// N steps unseen and plays them back through the replay cameras; `bspec=1` a B-spec run, `pit=1` its car asked into the pits from the start (GT4R_HITCH=<ms>
// logs every frame longer than that, with the race's work and the present before it); `menus=single_race` the race
// sequence's own menus (quick-arcade) before and after the race (any sequence: solo_event,
// championship, license, mission, time_trial, freerun, practice, machinetest), `race=<label>` its event.

#include "gt4r/app/app.h"
#include "gt4r/app/page.h"
#include "gt4r/app/presence.h"
#include "gt4r/app/menu_painter.h"
#include "gt4r/app/replay_picture.h"
#include "gt4r/app/race_common.h"
#include "gt4r/app/race_hud.h"
#include "gt4r/app/race_hud_prologue.h"
#include "gt4r/app/race_online_ui.h"
#include "gt4r/app/race_world.h"
#include "gt4r/audio/app_sound.h"
#include "gt4r/audio/race_playlist.h"
#include "gt4r/audio/race_sound.h"
#include "gt4r/audio/sound_library.h"

#include "base/log.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/input/player_controls.h"
#include "gt4r/input/race_feedback.h"
#include "gt4r/race/drive_camera.h"
#include "gt4r/race/race_flow.h"
#include "gt4r/race/rail_simulation.h"
#include "gt4r/race/replay_script.h"
#include "gt4r/race/replay_view.h"
#include "gt4r/race/race_recording.h"
#include "gt4r/race/recorded_race.h"
#include "gt4r/save/game_data.h"
#include "gt4r/race/replay_file.h"
#include "gt4r/formats/replay/demonstration_prologue.h"
#include "gt4r/race/license_prologue.h"
#include "gt4r/race/license_rules.h"
#include "gt4r/race/license_trophy.h"
#include "gt4r/formats/model/mdls.h"
#include "render/gpu_model.h"
#include "gt4r/race/sections.h"
#include "gt4r/sim/program_prologue.h"
#include "gt4r/sim/quick_tune.h"
#include "gt4r/sim/race_rules.h"
#include "gt4r/sim/race_tires.h"
#include "gt4r/race/start_camera.h"
#include "gt4r/race/start_rule.h"
#include "gt4r/race/picture_settings.h"
#include "gt4r/sim/ai/course_lines.h"
#include "gt4r/sim/ai/demo_driver.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/ai/entrant.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/sim/rules_start.h"
#include "gt4r/menu/game.h"
#include "gt4r/menu/game_settings.h"
#include "gt4r/menu/game_settings.h"
#include "gt4r/menu/race_natives.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/net/net_race.h"
#include "gt4r/net/online_race.h"
#include "race/controls.h"
#include "race/interp.h"
#include "gt4r/race/car_pose.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <ctime>
#include <optional>
#include <map>
#include <variant>

namespace gt4r {

namespace {

namespace render = gt3r::render;

// A license test's sequence: Gran Turismo 4's "license", Prologue's "license_race".
bool is_license(const std::string& sequence) { return sequence == "license" || sequence == "license_race"; }
using RVec = render::Vec3;

constexpr float kStep = 1.0f / 60;
constexpr int kTvView = -1; // the course's replay cameras, after the car's three views
constexpr size_t kHistory = 60 * 60 * 10; // steps of a run kept for its replay
constexpr int kSlotLogo = 49;              // the course's logo (Tex1)
constexpr int kSlotScenic = 48;            // the course's scenic shots (REP4): behind the race's menus

struct Setup {
    std::string course = "tsukuba";
    std::string car;
    int cars = 6;
    int view = race::DriveCamera::kChase;
    bool look_back = false;
    bool auto_drive = false;
    bool pit = false;     // `pit=1`: its car asked into the pits from the start
    RaceWorld::PlayerChoice player; // the gearbox, the aids, the wheel's (`mt=1`, `asm=0`, `tcs=0`; a race of the menus: its choices)
    bool bspec = false;   // `bspec=1`: a B-spec run (the computer drives the player's car; its display and commands)
    int skip = 0;
    u32 paint = 0;
    bool hud = true;
    int laps = 3;
    int replay = 0;
    bool smoke = true;
    bool drivers = true;
    bool reflection = true;
    bool rail = false;     // the stand-in simulation instead of the game's
    bool scripted = false; // from GT4R_RACE: no lists
    race::StartType start = race::StartType::Ten;
    int start_seconds = 0;
    float start_speed = 0; // a rolling start's (metres per second)
    std::vector<std::array<float, 2>> test_starts; // Prologue's test's rolling start, by car: km/h, metres from the line (sim::RaceRules)
    float start_back = 400, start_spacing = 20; // a rolling start's last place behind the line and the gap between places (metres)
    int start_kind = 0;        // a rolling start's kind (3 a free run's; 0 by its distance)
    bool course_start = false; // the start the course's start code gives (race/start_rule.h)
    int course_kind = 0;       // the course's COURSE row +0x10 (race/start_rule.h CourseStart::kind)
    bool start_given = false;
    std::vector<std::string> opponents; // the other cars by label (empty: from the car list)
    // Each entry's start place (0 the front), by the entry's number (0 the player); empty: the player
    // at the back, the others ahead in entry order. A championship's races from the second on
    // (0x3EE9F8 / 0x3EE7C0: the entries keep their order, only their places change).
    std::vector<int> grid_slots;
    std::string sequence; // `menus=single_race`: the race's own menus (the sequence's quick project) round the race
    std::optional<std::array<u8, 0x40>> bspec_info; // a recorded race's (WorldSetup)
    std::array<u16, 3 * 64> bspec_book{}; // the B-spec driver's book and the course's marks (the request's)
    std::array<u8, 8> bspec_course_marks{};
    bool bspec_course_known = false;
    std::string wing;        // `wing=<label>`: the player's car with that fitted wing (a WING row: "p001")
    std::vector<u32> paints; // every car's paint (a saved replay's); empty: the player's `paint`, the others by number
    std::string replay_file; // `replay_file=<path>`: a saved replay played, or a recorded race of the disc's (replay/<name>/<name>)
    std::shared_ptr<const sim::ai::DemoRun> demo_run; // that recorded race
    std::string entries_file; // `entries=<path>`: the entries' equipment records and grid places from a file (a captured race's)
    int demo_skip = 0;       // its steps run unseen first (`skip`)
    bool solo = false;       // a run alone (a recorded free run's)
    int save_replay = 0; // `save=1`: the run `replay=N` drove saved as a replay file; `save=N`: the run at its step N
    std::string race;     // `race=<label>`: the event (the car database's RACE row) the sequence runs
    sim::ai::Pace pace;   // the computer drivers' (the event's)
    std::vector<u8> event_row; // the event's RACE row (its tire wear's curve); empty: none
    // A license test, pace car test or mission: its conductor, its rules (its row's) and its target times.
    race::Conductor conductor = race::Conductor::Other;
    race::LicenseRules rules;
    s64 targets[3] = {-1, -1, -1};
    std::map<std::string, std::variant<s32, float, std::string>> options; // the game's (the sound's volumes); empty: a new game's
    std::optional<sim::Equipment> player_equipment;  // a garage car's parts and settings
    std::optional<std::array<u8, 0x20>> player_condition; // and its condition block
    std::vector<std::optional<sim::Equipment>> opponent_equipment; // per opponent (as `opponents`): its pool's parts and settings
    std::shared_ptr<audio::RacePlaylist> playlist;   // the races' music (the game's play list)
    std::shared_ptr<const net::OnlineRace> online;   // an online race (its cars in grid order); online_order: our entries' places in it
    std::vector<size_t> online_order;                // (entry 0 is ours, the others follow in grid order)
    Bytes playlist_bytes;                            // as the options' block keeps it (a recording's header has it)
    int penalty_option = 0, tire_damage = 0;         // an arcade race's options (race::arcade_rules)
};

// An online race's set-up: our car entry 0, the others in grid order, every car as its owner sent it on its place of the grid.
void online_set_up(Setup& setup, const std::shared_ptr<const net::OnlineRace>& online) {
    setup.online = online;
    const net::OnlineRace& o = *online;
    setup.online_order = {size_t(o.ours)};
    for (size_t i = 0; i < o.cars.size(); ++i)
        if (int(i) != o.ours) setup.online_order.push_back(i);
    setup.cars = int(o.cars.size());
    setup.laps = std::clamp(o.rules.laps, 1, 99);
    setup.opponents.clear(), setup.opponent_equipment.clear(), setup.paints.clear(), setup.grid_slots.clear();
    for (size_t e = 0; e < setup.online_order.size(); ++e) {
        const net::RaceCar& c = o.cars[setup.online_order[e]];
        if (e == 0) {
            setup.car = c.label, setup.paint = c.paint, setup.wing = c.wing;
            setup.player_equipment = c.equipment, setup.player_condition = c.condition;
            setup.player.automatic = c.automatic, setup.player.asm_on = c.asm_on, setup.player.tcs_on = c.tcs_on, setup.player.wheel_aid = c.wheel_aid, setup.player.wheel_lock = c.driver;
        } else {
            setup.opponents.push_back(c.label);
            setup.opponent_equipment.push_back(c.equipment);
        }
        setup.paints.push_back(c.paint);
        setup.grid_slots.push_back(int(setup.online_order[e]));
    }
}

Setup setup_from_env() {
    Setup s;
    const char* env = SDL_getenv("GT4R_RACE");
    if (!env) return s;
    s.scripted = true;
    for (std::string_view kv : gt3r::split(env, ',')) {
        const size_t eq = kv.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string k(gt3r::trim(kv.substr(0, eq))), v(gt3r::trim(kv.substr(eq + 1)));
        if (k == "course") s.course = v;
        else if (k == "car") s.car = v;
        else if (k == "cars") s.cars = std::clamp(std::atoi(v.c_str()), 1, 16);
        else if (k == "view") {
            s.view = v == "in" ? race::DriveCamera::kInCar : v == "roof" ? race::DriveCamera::kRoof : race::DriveCamera::kChase;
            if (v == "back") s.look_back = true;
            if (v == "tv") s.view = kTvView;
            if (!v.empty() && std::isdigit(static_cast<unsigned char>(v[0]))) s.view = std::atoi(v.c_str());
        } else if (k == "drive") s.auto_drive = v == "auto";
        else if (k == "bspec") s.bspec = v != "0";
        else if (k == "pit") s.pit = v != "0";
        else if (k == "mt") s.player.automatic = v == "0";
        else if (k == "asm") s.player.asm_on = v != "0";
        else if (k == "tcs") s.player.tcs_on = v != "0";
        else if (k == "skip") s.skip = std::max(0, std::atoi(v.c_str()));
        else if (k == "paint") s.paint = u32(std::max(0, std::atoi(v.c_str())));
        else if (k == "wing") s.wing = v;
        else if (k == "hud") s.hud = v != "0";
        else if (k == "replay") s.replay = std::max(0, std::atoi(v.c_str()));
        else if (k == "smoke") s.smoke = v != "0";
        else if (k == "driver") s.drivers = v != "0";
        else if (k == "reflection") s.reflection = v != "0";
        else if (k == "sim") s.rail = v == "rail";
        else if (k == "laps") s.laps = std::clamp(std::atoi(v.c_str()), 1, 99);
        else if (k == "menus") s.sequence = v;
        else if (k == "race") s.race = v;
        else if (k == "opponents")
            for (std::string_view o : gt3r::split(v, '/')) s.opponents.emplace_back(gt3r::trim(o));
        else if (k == "replay_file") s.replay_file = v;
        else if (k == "entries") s.entries_file = v;
        else if (k == "save") s.save_replay = std::atoi(v.c_str());
        else if (k == "start") {
            s.start_given = true;
            if (v == "course") s.course_start = true;
            else if (v == "rolling") s.start = race::StartType::Rolling, s.start_speed = 80 / 3.6f;
            else if (v == "none") s.start = race::StartType::None;
            else if (v == "8") s.start = race::StartType::Eight;
            else if (v == "10") s.start = race::StartType::Ten;
            else s.start = race::StartType::Given, s.start_seconds = std::clamp(std::atoi(v.c_str()), 0, 60);
        }
    }
    // (A run driven unseen for its replay: the computer drivers drive the player's car too, not the
    // simple driver.)
    if (s.replay > 0) s.auto_drive = true;
    return s;
}

// The sequences the free run's conductor runs (DynamicsConductorFreeRun: the arcade's time trial, the free run, Gran Turismo
// mode's practice): the player's car alone, no number of laps (its virtual 37 answers 0).
bool free_run(const std::string& sequence) { return sequence == "time_trial" || sequence == "freerun" || sequence == "practice"; }

// The course's start (race/start_rule.h): its kind always; with `start` its start code's start, a
// rolling start at the distance, speed and spacing of its drivers' file.
void apply_course_start(App& app, Setup& setup, bool start) {
    db::SpecDb* sdb = app.db();
    if (!sdb) return;
    race::CourseStart cs = race::course_start(*sdb, setup.course);
    if (const std::optional<Bytes> ad = app.vfs()->read("crs/" + setup.course + ".ad")) race::apply_drivers_file(cs, *ad);
    setup.course_kind = cs.kind;
    if (!start) return;
    // (A standing start's hold: the event's row's, set in RaceScreen::start (race::row_start). A test's start: load_test.)
    setup.start = cs.rolling ? race::StartType::Rolling : race::StartType::Ten;
    setup.start_speed = cs.speed(), setup.start_back = cs.distance_m, setup.start_spacing = cs.spacing_m;
    // A free run (DynamicsConductorFreeRun::vf30 0x3F5670): the course's free run-up, or the grid.
    if (setup.solo || free_run(setup.sequence)) {
        if (const bool prologue = sdb->prologue(); !prologue) {
            setup.start = cs.free_speed_kmh > 0 ? race::StartType::Rolling : race::StartType::Ten;
            setup.start_speed = cs.free_speed_kmh / 3.6f, setup.start_back = cs.free_distance_m, setup.start_kind = 3;
            if (free_run(setup.sequence)) setup.laps = 0;
        }
    }
}

// An event by its label, or by the scripts' code of a test or mission under the region's prefix (0x430D20: "us", then "jp").
std::optional<db::RaceEvent> find_event(const db::RaceEvents& events, const std::string& label) {
    if (label.empty()) return std::nullopt;
    for (const std::string& l : {label, "us" + label, "jp" + label})
        if (std::optional<db::RaceEvent> e = events.event(l)) return e;
    return std::nullopt;
}

// A license test's, pace car test's or mission's event: its row, rules, target times and start. The conductors' starts
// (0x3F6278) never read the course's start code: a place of the row's own (moving when it has a speed), else the grid; a
// standing start holds 8 s (start type 1: the demonstrations' recordings run a 480-step hold before the car moves).
void load_test(App& app, Setup& setup, const std::string& code) {
    setup.conductor = race::Conductor::Other, setup.rules = {};
    for (s64& t : setup.targets) t = -1;
    const bool test = is_license(setup.sequence) || setup.sequence == "mission";
    if (!test && (!setup.sequence.empty() || code.empty())) return;
    db::SpecDb* sdb = app.db();
    if (!sdb) return;
    try {
        gt3r::Vfs* vfs = app.vfs();
        const db::RaceEvents events(*sdb, [vfs](const std::string& path) { return vfs->read(path); });
        if (const std::optional<db::RaceEvent> e = find_event(events, code)) {
            setup.event_row = e->bytes;
            if (!test) { // (an event by its label without the menus: its row and its drivers' pace)
                setup.pace = sim::ai::pace_for(*e);
                return;
            }
            for (size_t k = 0; k < 3; ++k) setup.targets[k] = e->target_ms[k] > 0 ? s64(e->target_ms[k]) : -1;
            setup.rules = race::LicenseRules::from_row(e->bytes);
            if (app.disc() && app.disc()->info().serial.rfind("SCPS", 0) != 0) setup.conductor = race::conductor_for(setup.sequence, code);
        }
    } catch (const std::exception& e) {
        LOG_WARN("Drive: the test's event: %s", e.what());
    }
    if (!setup.start_given && setup.conductor != race::Conductor::Other)
        setup.start = race::RowStart::from_row(setup.event_row).moving(setup.conductor) ? race::StartType::Moving : race::StartType::Eight;
    else if (!setup.start_given && setup.test_starts.empty()) { // (a Prologue test on the move: prologue_test_start's)
        setup.start = race::StartType::Eight;
        if (sdb->prologue()) setup.start = race::StartType::Given, setup.start_seconds = 4; // (its start type 1: 4 s, 0x37A7B8)
    }
}

// The event's grid (the opponents drawn from its pool, the player last), each opponent with its pool's
// parts, settings and paint (docs/specs/ai.md "Who races"), and the drivers' pace.
void draw_grid(App& app, Setup& setup, const menu::RaceRequest& q) {
    db::SpecDb* sdb = app.db();
    if (!sdb) return;
    try {
        gt3r::Vfs* vfs = app.vfs();
        const db::RaceEvents events(*sdb, [vfs](const std::string& path) { return vfs->read(path); });
        // The scripts' entry table: entry 0 the player's car, the opponents 1 .. overwrite_enemy_num
        // ("" none), their paints in entry_paints.
        menu::RaceRequest asked = q;
        std::vector<int> paints;
        asked.entries.clear();
        const size_t last = q.overwrite_enemy_num > 0 ? std::min(q.entries.size(), size_t(q.overwrite_enemy_num) + 1) : q.entries.size();
        for (size_t i = 1; i < last; ++i)
            if (!q.entries[i].empty() && q.entries[i] != "nil") {
                asked.entries.push_back(q.entries[i]);
                paints.push_back(i < q.entry_paints.size() ? q.entry_paints[i] : -1);
            }
        // (grid_for reads the table as the scripts leave it: entry 0 the player's, the opponents from 1)
        if (!asked.entries.empty()) {
            asked.entries.insert(asked.entries.begin(), q.car);
            asked.overwrite_enemy_num = s32(paints.size());
        }
        sim::ai::RaceRandom& random = sim::ai::race_random(); // (the program's one generator, kept while the app runs)
        const std::optional<db::RaceEvent> event = find_event(events, q.race);
        const std::string& sequence = q.sequence.empty() ? setup.sequence : q.sequence;
        const bool prologue_test = event && sdb->prologue() && event->label.rfind("l0p", 0) == 0;
        const bool training = event && !sdb->prologue() && (sequence == "mission" || is_license(sequence)); // (no draw: the pool in its order)
        std::vector<sim::ai::GridEntry> grid;
        if (!training) grid = sim::ai::grid_for(events, asked, random);
        if (!asked.entries.empty())
            for (size_t i = 0; i < grid.size() && i < paints.size(); ++i)
                if (!grid[i].player && grid[i].paint < 0) grid[i].paint = paints[i];
        // A test or mission with other cars: its pool in order (RaceTrainingInformation 0x3ECB68: car i entry min(i, the pool's last),
        // without a paint of its own paint 0), entry 0 the player's car, on the grid the others in that order and the player last
        // (0x3ECC70; Prologue's 0x3E1C20).
        if (prologue_test || training) {
            grid.clear();
            const std::vector<db::Opponent> pool = events.pool(*event);
            for (size_t i = 1; !pool.empty() && int(i) < event->grid && (training || i < pool.size()); ++i) {
                const db::Opponent& o = pool[std::min(i, pool.size() - 1)];
                sim::ai::GridEntry g;
                g.car = o.car, g.paint = training ? std::max(o.paint, 0) : o.paint, g.parts = o.parts, g.param = o.param, g.enemy = o.enemy;
                grid.push_back(g);
            }
            sim::ai::GridEntry me;
            me.car = q.car, me.paint = q.paint, me.player = true;
            grid.push_back(me);
        }
        std::optional<sim::Program> program;
        if (Disc* disc = app.disc()) try {
                program = sim::Program::load(*disc);
            } catch (const std::exception& e) {
                LOG_WARN("Drive: the program: %s", e.what());
            }
        setup.opponents.clear(), setup.opponent_equipment.clear();
        // Every car's equipment, the paints the race chooses drawn among the grid's (sim::ai::grid_equipment).
        const std::vector<sim::Equipment> equipment = program ? sim::ai::grid_equipment(*sdb, *program, events, grid, random) : std::vector<sim::Equipment>{};
        for (size_t i = 0; i < grid.size(); ++i) {
            const sim::ai::GridEntry& g = grid[i];
            if (g.player) continue;
            setup.opponents.push_back(g.car);
            if (i < equipment.size() && equipment[i].part(sim::Equipment::Variation) != db::kNoPart) {
                const sim::Equipment& eq = equipment[i];
                setup.opponent_equipment.emplace_back(eq);
                LOG_INFO("Drive: opponent %s%s%s (output %d per mille, paint %d)", g.car.c_str(), g.enemy.empty() ? "" : " as ", g.enemy.c_str(),
                         int(eq.h(sim::Equipment::OutputFactor)), int(eq.b(0x18)));
            } else {
                setup.opponent_equipment.emplace_back();
            }
        }
        if (!setup.opponents.empty()) setup.cars = int(setup.opponents.size()) + 1;
        // (An arcade race of a car without an event's row: sample_entry_6, the original's, whatever its number of cars; it counts fuel.)
        const std::string race = q.race.empty() && (q.sequence == "single_race" || setup.sequence == "single_race") ? std::string("sample_entry_6") : q.race;
        if (const std::optional<db::RaceEvent> e = find_event(events, race)) setup.pace = sim::ai::pace_for(*e), setup.event_row = e->bytes;
        LOG_INFO("Drive: the event's row %s (%zu bytes)", race.c_str(), setup.event_row.size());
    } catch (const std::exception& e) {
        LOG_WARN("Drive: the event's grid: %s", e.what());
    }
}

// A test's or mission's own car: entry 0 of its event's pool with that entry's parts and settings (both games: the license
// demonstrations' files hold the same for their car), when the request's car is that one and no garage car's.
void test_car(App& app, Setup& setup, const menu::RaceRequest& q) {
    db::SpecDb* sdb = app.db();
    const std::string& sequence = q.sequence.empty() ? setup.sequence : q.sequence; // (the menus' own request names none)
    if (!sdb || !app.disc() || q.garage_slot >= 0 || q.race.empty() || (!is_license(sequence) && sequence != "mission")) return;
    try {
        gt3r::Vfs* vfs = app.vfs();
        const db::RaceEvents events(*sdb, [vfs](const std::string& path) { return vfs->read(path); });
        const std::optional<db::RaceEvent> e = find_event(events, q.race);
        if (!e) return;
        const std::vector<db::Opponent> pool = events.pool(*e);
        if (pool.empty() || pool[0].car != (q.car.empty() ? setup.car : q.car)) return; // (Prologue's lessons name none: the event's)
        sim::ai::GridEntry g;
        g.car = pool[0].car, g.paint = (sdb->prologue() || q.paint < 0 || sequence == "mission") && pool[0].paint >= 0 ? pool[0].paint : q.paint, g.parts = pool[0].parts, g.param = pool[0].param, g.enemy = pool[0].enemy;
        sim::Equipment eq;
        if (!sim::ai::entry_equipment(*sdb, sim::Program::load(*app.disc()), events, g, eq)) return;
        setup.player_equipment = eq;
        if (g.paint >= 0) setup.paint = u32(g.paint);
        LOG_INFO("Drive: the test's car %s as %s (%s, %s)", g.car.c_str(), g.enemy.c_str(), g.parts.c_str(), g.param.c_str());
    } catch (const std::exception& e) {
        LOG_WARN("Drive: the test's car: %s", e.what());
    }
}

// The tires Arcade's races put on every entry (RaceArcade's virtual 92 and RaceFreeRun's, sim/race_tires.h): the kind chosen on
// the driving options (0x111B50, 0x112FE8 give it to all), dirt's or snow's on those courses; a free run's car its own.
// (The race menus' Quick Tune changes the player's kinds as a run starts: apply_quick_tune.)
void arcade_tires(App& app, Setup& setup, const menu::RaceRequest& q) {
    const bool arcade = setup.sequence == "single_race" || setup.sequence == "time_trial";
    db::SpecDb* sdb = app.db();
    if ((!arcade && setup.sequence != "freerun") || !sdb || sdb->prologue() || !app.disc()) return;
    try {
        const sim::Program program = sim::Program::load(*app.disc());
        if (!program.known()) return;
        const std::optional<sim::CourseRow> row = sim::course_row(*sdb, setup.course);
        gt3r::Vfs* vfs = app.vfs();
        const sim::FileReader read = [vfs](const std::string& path) { return vfs->read(path); };
        auto fit = [&](const std::string& label, std::optional<sim::Equipment>& eq) {
            sim::Equipment e;
            if (eq) e = *eq;
            else if (!sim::stock_car(*sdb, program, label, e)) return;
            else e.set_b(0x18, 0xFF); // (its paint the race's choice, as without equipment)
            if (sim::fit_race_tires(*sdb, read, e, row ? row->kind : 0, arcade ? std::clamp(q.tire, 0, 12) : -1)) eq = e;
        };
        fit(setup.car, setup.player_equipment);
        setup.opponent_equipment.resize(setup.opponents.size());
        for (size_t i = 0; i < setup.opponents.size(); ++i) fit(setup.opponents[i], setup.opponent_equipment[i]);
        if (setup.player_equipment)
            LOG_INFO("Drive: the race's tires: kinds %d and %d on the player's car (%d chosen)", int(sim::tire_kind(*sdb, *setup.player_equipment, 0)),
                     int(sim::tire_kind(*sdb, *setup.player_equipment, 1)), arcade ? q.tire : -1);
    } catch (const std::exception& e) {
        LOG_WARN("Drive: the race's tires: %s", e.what());
    }
}

// A recorded race of the disc's (the Replay Theater's Demo tab: docs/formats/replay.md): its course, laps, cars with their
// parts, settings and places, its event's row; false when the disc has none by that path.
bool load_recorded_race(App& app, Setup& setup, const std::optional<Bytes>& file);
// The entries of a file: u32 the cars, then each its equipment record (0x178 bytes) and its grid place (u32).
void load_entries(App& app, Setup& setup) {
    std::FILE* fp = std::fopen(setup.entries_file.c_str(), "rb");
    if (!fp || !app.db()) return;
    u32 n = 0;
    if (std::fread(&n, 4, 1, fp) != 1) n = 0;
    setup.cars = int(std::min<u32>(n, 16));
    setup.opponents.clear(), setup.opponent_equipment.clear(), setup.paints.clear(), setup.grid_slots.clear();
    const db::Labels* labels = app.db()->labels("GENERIC_CAR");
    for (u32 i = 0; i < u32(setup.cars); ++i) {
        sim::Equipment eq;
        u32 place = 0;
        if (std::fread(eq.bytes.data(), eq.bytes.size(), 1, fp) != 1 || std::fread(&place, 4, 1, fp) != 1) break;
        u32 code = 0;
        std::memcpy(&code, eq.bytes.data(), 4);
        const std::string* car = labels ? labels->label(code) : nullptr;
        if (i == 0) setup.car = car ? *car : std::string(), setup.player_equipment = eq;
        else setup.opponents.push_back(car ? *car : std::string()), setup.opponent_equipment.emplace_back(eq);
        setup.paints.push_back(eq.b(0x18)), setup.grid_slots.push_back(int(place));
    }
    std::fclose(fp);
}
bool load_recorded_race(App& app, Setup& setup, const std::optional<Bytes>& file) {
    db::SpecDb* sdb = app.db();
    Disc* disc = app.disc();
    if (!file || !sdb || !disc) return false;
    const sim::Program program = sim::Program::load(*disc);
    std::string error;
    std::optional<sim::ai::DemoRun> run = sim::ai::DemoRun::load_replay(*file, program, setup.replay_file.substr(setup.replay_file.rfind('/') + 1), &error);
    if (!run || run->demo.entries.empty()) throw FormatError(setup.replay_file + ": " + (error.empty() ? "no entries" : error));
    auto label = [&](const char* table, u64 code) {
        const db::Labels* l = sdb->labels(table);
        const std::string* s = l ? l->label(u32(code)) : nullptr;
        return s ? *s : std::string();
    };
    const replay::Demonstration& d = run->demo;
    setup.course = label("COURSE", d.code_b8), setup.race = label("RACE", d.code_b0);
    u32 laps = 0;
    std::memcpy(&laps, d.information.data(), 4);
    setup.laps = int(laps);
    setup.cars = int(d.entries.size());
    setup.bspec_info.emplace();
    std::memcpy(setup.bspec_info->data(), d.information.data() + 0x20, 0x40);
    setup.solo = d.race_class == "RaceFreeRun" || d.race_class == "RaceSolitaire";
    setup.bspec = false;
    setup.opponents.clear(), setup.opponent_equipment.clear(), setup.paints.clear(), setup.grid_slots.clear();
    for (size_t i = 0; i < d.entries.size(); ++i) {
        const replay::DemoEntry& e = d.entries[i];
        u64 code = 0;
        std::memcpy(&code, e.equipment.bytes.data(), 8);
        const std::string car = label("GENERIC_CAR", code);
        if (car.empty()) LOG_WARN("Drive: the recorded race: no car %llx", (unsigned long long)code);
        if (i == 0) setup.car = car, setup.player_equipment = e.equipment, setup.player_condition = e.condition;
        else setup.opponents.push_back(car), setup.opponent_equipment.emplace_back(e.equipment);
        setup.paints.push_back(e.equipment.b(0x18));
        setup.grid_slots.push_back(e.grid_place);
    }
    gt3r::Vfs* vfs = app.vfs();
    if (const std::optional<db::RaceEvent> e = db::RaceEvents(*sdb, [vfs](const std::string& path) { return vfs->read(path); }).event(setup.race))
        setup.pace = sim::ai::pace_for(*e), setup.event_row = e->bytes;
    if (d.race_class == "RaceArcadeSingle") { // (the options as the recording's race had them: the race information's +4, +8)
        u32 rules[2];
        std::memcpy(rules, d.information.data() + 4, 8);
        race::arcade_rules(setup.event_row, int(rules[0]), int(rules[1]));
    }
    setup.demo_skip = std::exchange(setup.skip, 0), setup.replay = 0, setup.sequence.clear();
    if (d.race_class == "RaceLicense" || d.race_class == "RaceTraining") setup.sequence = "license"; // (its start, rules and conductor: load_test)
    else apply_course_start(app, setup, true);
    LOG_INFO("Drive: the recorded race %s: %s on %s, %zu cars, %d laps, %zu steps (%d ms, best lap %d ms)", setup.replay_file.c_str(), d.race_class.c_str(), setup.course.c_str(), d.entries.size(),
             setup.laps, run->steps(), d.time_ms, d.time2_ms);
    setup.demo_run = std::make_shared<const sim::ai::DemoRun>(std::move(*run));
    return true;
}

} // namespace

// The screen settings of the racing views and of the replays and B-spec races, kept while the app
// runs. ❓ The options' Picture Quality page keeps them in MOption (race_color_correction,
// replay_color_correction: four numbers each); the menus here do not hold them as numbers yet.
race::PictureSettings& picture_settings(bool replay) {
    static race::PictureSettings race_views, replays{race::PictureSettings::kReplayDefault};
    return replay ? replays : race_views;
}

// An arcade single race by the game's options, whatever its row holds.
void apply_arcade_options(Setup& setup) {
    const menu::GameSettings o = menu::settings_from(setup.options);
    setup.penalty_option = o.penalty == "SPEED_LIMITATION" ? 1 : o.penalty == "PASSING_PITLANE" ? 2 : 0, setup.tire_damage = o.tire_damage;
    race::arcade_rules(setup.event_row, setup.penalty_option, setup.tire_damage);
}

// The screen settings from the game's options (the Picture Quality page's race_color_correction,
// replay_color_correction) at a race's start, and what the pause changed back into them after it.
void pictures_from_options(const std::map<std::string, menu::OptionValue>& options) {
    const menu::GameSettings s = menu::settings_from(options);
    picture_settings(false).v = s.race_picture, picture_settings(true).v = s.replay_picture;
}
void pictures_to_options(std::map<std::string, menu::OptionValue>& options) {
    options["race_color_correction"] = menu::picture_text(picture_settings(false).v);
    options["replay_color_correction"] = menu::picture_text(picture_settings(true).v);
}

// The game's play list of the races' music as the race's sound takes it (null: none filled), and
// what a race leaves of it (its position, the pieces played) back in the game.
std::shared_ptr<audio::RacePlaylist> playlist_from(const menu::GameState::PlayList& list) {
    if (list.rows == 0) return nullptr;
    auto p = std::make_shared<audio::RacePlaylist>(std::min<size_t>(list.rows, list.row.size()));
    for (size_t i = 0; i < p->rows().size(); ++i) {
        audio::RacePlaylist::Row& r = p->rows()[i];
        r.enabled = list.row[i].enabled != 0, r.played = list.row[i].played != 0, r.order = list.row[i].order;
    }
    p->set_position(list.position);
    p->set_shuffle(list.shuffle != 0, list.seed);
    return p;
}
void playlist_to(const audio::RacePlaylist& p, menu::GameState::PlayList& list) {
    list.position = u16(std::max(0, p.position()));
    for (size_t i = 0; i < p.rows().size() && i < list.row.size(); ++i) list.row[i].played = p.rows()[i].played ? 1 : 0;
}

// The play list of the races' music a new game has: the configuration script's (init_raceplaylist),
// from menus booted for it (null when they cannot be).
std::shared_ptr<audio::RacePlaylist> new_game_playlist(App& app) {
    if (!app.vfs()) return nullptr;
    try {
        menu::Runtime::Config config;
        gt3r::Vfs* vfs = app.vfs();
        config.list = [vfs](const std::string& folder) { return vfs->list_dir(folder); };
        if (Disc* disc = app.disc()) config.program_image = [disc] { return disc->program_image(); };
        config.data_dir = app.save_dir();
        menu::Runtime rt([vfs](const std::string& path) { return vfs->read(path); }, config);
        rt.boot();
        return playlist_from(rt.game().bgm_playlist);
    } catch (const std::exception& e) {
        LOG_WARN("Drive: the races' play list: %s", e.what());
        return nullptr;
    }
}

namespace {

// The race itself: the world (race_world.h), its flow (race_flow.h) and the cameras.
class Race {
public:
    Race(App& app, const Setup& setup, const std::vector<db::CarEntry>& cars) : app_(&app), setup_(setup), disc_(app.disc()), db_(app.db()) {
        // The first car is the one asked for; the others follow it in the car list.
        size_t first = 0;
        for (size_t i = 0; i < cars.size(); ++i)
            if (cars[i].label == setup.car || cars[i].model == setup.car) first = i;
        WorldSetup ws;
        ws.course = setup.course;
        ws.smoke = setup.smoke, ws.reflection = setup.reflection, ws.drivers = setup.drivers, ws.rail = setup.rail;
        ws.computer_player = setup.auto_drive;
        ws.conductor = setup.conductor, ws.section = setup.rules.section;
        ws.own_hold = setup.skip == 0 && race::start_hold_steps(setup.start, setup.start_seconds) > 0;
        ws.pace = setup.pace;
        if (setup.online) ws.lan_wear = setup.online->rules.tire_wear ? 1 : 0, ws.lan_boost = setup.online->rules.boost;
        ws.bspec_book = setup.bspec_book, ws.bspec_course_marks = setup.bspec_course_marks, ws.bspec_course_known = setup.bspec_course_known;
        ws.bspec_info = setup.bspec_info;
        ws.event_row = setup.event_row;
        // The race's conductor: a run alone (its virtual 6 answers 1: the time trial and free run, the license tests but the
        // three trainings, the machine test), and whose class grips the event's row gives.
        const int license_test = setup.race.size() >= 4 ? std::atoi(setup.race.c_str() + setup.race.size() - 4) : 0;
        // (Prologue's 0x3B48B8: a test that starts on the move is not a run alone)
        const bool training = app.db() && app.db()->prologue() ? !setup.test_starts.empty() : license_test == 5 || license_test == 10 || license_test == 15;
        ws.pace.solo = setup.solo || setup.sequence == "time_trial" || setup.sequence == "freerun" || setup.sequence == "machinetest" || (is_license(setup.sequence) && !training);
        ws.pace.random_spread = setup.conductor == race::Conductor::Other;
        ws.event_grips = is_license(setup.sequence) || setup.sequence == "mission" || setup.sequence == "practice";
        ws.laps = setup.laps;
        ws.options = setup.options;
        ws.playlist = setup.playlist;
        // The grid: the player at the back, the others ahead of him in entry order.
        std::vector<int> places = race::grid_places(size_t(setup.cars), 0);
        if (setup.grid_slots.size() == places.size()) places = setup.grid_slots;
        for (int i = 0; i < setup.cars; ++i) {
            WorldEntry e;
            e.label = i == 0 || setup.opponents.empty() ? cars[(first + size_t(i) * 37) % cars.size()].label
                                                         : setup.opponents[size_t(i - 1) % setup.opponents.size()];
            e.paint = size_t(i) < setup.paints.size() ? setup.paints[size_t(i)] : i == 0 ? setup.paint : u32(i);
            e.grid_place = places[size_t(i)];
            if (setup.start == race::StartType::Rolling) {
                // In single file behind the line: the last place the course's distance back, its spacing
                // between places (docs/formats/race_flow.md "Which start a course has").
                e.start_speed = setup.start_speed, e.start_kind = setup.start_kind;
                e.start_distance = float(sim::rules::rolling_start_distance(-int(std::lround(setup.start_back)), int(std::lround(setup.start_spacing)), setup.cars, e.grid_place));
            }
            if (size_t(i) < setup.test_starts.size()) e.start_speed = setup.test_starts[size_t(i)][0] / 3.6f, e.start_distance = setup.test_starts[size_t(i)][1];
            if (i == 0) { // (what the player chose: RaceWorld::PlayerChoice)
                e.automatic = setup.player.automatic, e.asm_on = setup.player.asm_on, e.tcs_on = setup.player.tcs_on;
                e.wheel_aid = setup.player.wheel_aid, e.driver = setup.player.wheel_lock;
            }
            else e.wheel_aid = app.db() && !app.db()->prologue(); // (a computer's entry has it: every recorded race's, replay.md)
            if (i == 0) e.equipment = setup.player_equipment, e.condition = setup.player_condition, e.wing = setup.wing;
            else if (size_t(i - 1) < setup.opponent_equipment.size() && setup.opponent_equipment[size_t(i - 1)]) {
                // An opponent entered as its pool enters it: its parts, settings and the paint the pool
                // gives (0xFF: the race's choice, here by its number).
                e.equipment = setup.opponent_equipment[size_t(i - 1)];
                if (const u8 p = e.equipment->b(0x18); p != 0xFF && size_t(i) >= setup.paints.size()) e.paint = p;
            }
            if (setup.online && i > 0) { // (its owner's gearbox, aids and condition)
                const net::RaceCar& c = setup.online->cars[setup.online_order[size_t(i)]];
                e.automatic = c.automatic, e.asm_on = c.asm_on, e.tcs_on = c.tcs_on, e.wheel_aid = c.wheel_aid, e.driver = c.driver, e.condition = c.condition, e.wing = c.wing;
                e.remote = true;
            }
            ws.entries.push_back(e);
        }
        world_ = std::make_unique<RaceWorld>(app, ws);
        world_->set_player(setup.player);
        inputs_.resize(size_t(setup.cars));
        if (const ByteSpan rep = world_->course().section(course::kSlotReplay); race::ReplayScript::is_script(rep)) {
            try {
                replay_script_ = race::ReplayScript::parse(rep);
            } catch (const std::exception& e) {
                LOG_WARN("Replay cameras: %s", e.what());
            }
        }
        if (const ByteSpan scenic = world_->course().section(kSlotScenic); race::ReplayScript::is_script(scenic)) {
            try {
                scenic_script_ = race::ReplayScript::parse(scenic);
            } catch (const std::exception& e) {
                LOG_WARN("Scenic shots: %s", e.what());
            }
        }
        for (int k = 0; k < 2; ++k)
            if (const ByteSpan mot = world_->course().section(k ? race::kSlotReplayStartMotion : race::kSlotStartMotion); race::CameraMotion::is_motion(mot)) {
                try {
                    (k ? replay_motion_ : start_motion_) = race::CameraMotion::parse(mot);
                } catch (const std::exception& e) {
                    LOG_WARN("Start shots: %s", e.what());
                }
            }
        if (start_motion_.clips.empty())
            for (int k = 0; k < 2; ++k) // (Prologue's start cameras, race/start_camera_prologue.h)
                if (const ByteSpan rep = world_->course().section(k ? race::kSlotPrologueReplayStart : race::kSlotPrologueStart); race::ReplayScript::is_script(rep)) {
                    try {
                        start_paths_[k] = race::ReplayScript::parse(rep);
                    } catch (const std::exception& e) {
                        LOG_WARN("Start shots: %s", e.what());
                    }
                }
        for (const course::Runway::Triangle& t : world_->course().road().triangles) stop_area_ = stop_area_ || (t.kind & 4) != 0;
        {
            // (Gran Turismo 3's road, Prologue's: a goal distance inside the lap ends a license test there)
            const course::Runway& r = world_->course().road();
            goal_v_ = r.gtrw && r.goal_v > 0 && r.goal_v < r.length - 1 ? r.goal_v : -1;
            gt3_road_ = r.gtrw != nullptr;
            if (gt3_road_ && app.disc()) try { // (Prologue's camera on a finished license test)
                    const sim::Program program = sim::Program::load(*app.disc());
                    const auto real = [&](u32 at) { return std::bit_cast<float>(program.w(at)); };
                    if (is_license(setup.sequence)) // (the judge's pit zone, 0x42A670)
                        if (const std::optional<course::AiData> data = sim::ai::Field::read(*app.disc(), setup.course)) {
                            const sim::RoadGround ground(r);
                            pit_zone_ = race::prologue::pit_zone(sim::ai::CourseLines(*data, ground, sim::ai::Tables::load(program)));
                        }
                    if (sim::PrologueProgram::locate(program).recognized()) finish_camera_.fov = real(0x504674), finish_camera_.pitch = real(0x504678), finish_camera_.distance = real(0x50467C);
                } catch (const std::exception&) {
                }
        }
        ByteSpan road_section = world_->course().section(course::kSlotRunway);
        if (road_section.empty()) road_section = world_->course().section(course::kSlotRoadGtrw); // (Prologue's)
        splits_ = race::road_list(road_section, 0);
        if (splits_.size() > 3 && gt3_road_) splits_.resize(3); // (0x3B5FF8: four sectors at most)
        judge_ = race::LicenseJudge(setup.rules);
        if (setup.rules.section >= 0)
            LOG_INFO("Drive: the test's section %d: from %.1f m to the goal at %.1f m", setup.rules.section, double(world_->start_race().section_start),
                     double(world_->start_race().goal));
        mount_ = setup.view;
        begin_run(setup.bspec);
        built_ = true;
        // A scripted run that skips ahead: those steps unseen.
        if (setup.pit) world_->pit_command(0);
        for (int i = 0; i < setup.skip; ++i) step({}, true);
        if (setup.replay > 0) {
            // A run driven unseen, then shown through the replay cameras.
            for (int i = 0; i < setup.replay; ++i) step({}, true);
            start_replay();
        }
    }

    // The cars on the grid and the start's hold from the beginning; `watched`: the computer drives the
    // player's car too (B-spec).
    void set_bspec_book(const std::array<u16, 3 * 64>& book, const std::array<u8, 8>& marks) { world_->set_bspec_book(book, marks); }
    const WorldSetup& world_setup() const { return world_->setup(); }
    // What the player chose for his car (the next run's).
    void set_player(const RaceWorld::PlayerChoice& c) { stale_ = world_->set_player(c) || stale_; }
    // The player's car's parts and settings from the next run on (Arcade's Quick Tune).
    void set_player_equipment(const sim::Equipment& eq) {
        setup_.player_equipment = eq;
        world_->set_equipment(0, eq);
        stale_ = true;
    }
    // The race's flow of a run.
    race::FlowSetup flow_setup() const {
        race::FlowSetup fs;
        fs.start = setup_.skip > 0 ? race::StartType::None : setup_.start;
        fs.start_seconds = setup_.start_seconds;
        fs.laps = setup_.laps;
        fs.player = 0;
        fs.lap_length = world_->lap_length();
        fs.player_clock = !setup_.test_starts.empty();
        for (const std::array<float, 2>& s : setup_.test_starts) fs.past_line.push_back(s[1] > 0);
        // Where the cars' crossings start, the goal, a test's section (GT4's roads).
        const course::Runway& road = world_->course().road();
        // (Prologue, 0x3C9790, counts by the split points alone: a road without any 0.)
        fs.counted = !road.gtrw || !splits_.empty();
        fs.goal = road.gtrw ? -1 : world_->start_race().goal;
        if (!road.gtrw && world_->simulated()) {
            fs.sectors = world_->start_race().sectors;
            fs.section_start = world_->start_race().section_start;
            for (size_t i = 0; i < world_->car_count(); ++i) {
                fs.crossings.push_back(world_->start_crossings(i));
                fs.marks.push_back(world_->start_of(i).start.mark);
            }
            if (setup_.conductor != race::Conductor::Other) fs.test_kind = setup_.rules.kind, fs.mission = setup_.conductor == race::Conductor::Mission;
            fs.no_lap = world_->start_race().way == 1;
        } else fs.sectors = splits_;
        return fs;
    }
    void begin_run(bool watched = false, bool demo = false) {
        if (!demo && demo_run_) end_demo();
        // (A demonstration drives the player's car by its recording.)
        world_->set_computer_player(demo ? demo_bspec_ : watched || setup_.auto_drive);
        // (the lesson page's recorded run drove with its own aids: the player's again; GT4R_LESSON_DRIVE=1: the recording drives
        // the lesson itself, for checks)
        const bool recorded = !demo && preview_pads_;
        lesson_pads_ = nullptr, lesson_at_ = 0;
        if (recorded) {
            std::vector<std::optional<WorldEntry>> over;
            if (SDL_getenv("GT4R_LESSON_DRIVE")) lesson_pads_ = preview_pads_, over = recorded_entry();
            preview_pads_ = nullptr;
            world_->set_entry_overrides(std::move(over)); // (restarts)
        } else if (!demo && (ran_ || watched != watched_ || stale_)) world_->restart();
        if (!demo) stale_ = false;
        watched_ = watched;
        ran_ = true;
        // B-spec: the pace "Steady", the course's cameras (the camera manager's mode 1), the help strip shown.
        bspec_pace_ = 3, overtake_steps_ = 0, bspec_help_ = true, focus_ = 0;
        mail_ = {}, records_.clear();
        recording_ = !demo && recordable();
        bspec_start_ = watched ? world_->bspec_record(0) : std::nullopt;
        snapshot_ = false, dissolve_total_ = dissolve_left_ = 0; // (a cut of the menus' scene behind the page ends with it)
        if (before_tv_ && mount_ == kTvView) mount_ = *before_tv_; // (the results page's cameras, back to the player's view)
        before_tv_.reset();
        if (own_view_ && !watched) mount_ = *own_view_, own_view_.reset();
        if (watched && !own_view_) own_view_ = mount_;
        if (watched) mount_ = kTvView;
        menu_scene_ = false;
        prev_cars_.clear();
        flow_.begin(flow_setup(), world_->cars());
        for (size_t i = 0; i < world_->car_count(); ++i) world_->set_crossings(i, flow_.car(i).counter), world_->set_rank(i, flow_.place(i));
        // A recorded free run starts at its lap's line (the recorder's state: the clock two steps on, its times from the line, 0x359D40).
        if (demo && demo_run_ && setup_.solo && !demo_run_->demo.streams.empty())
            if (const std::optional<replay::RecorderState> st = replay::recorder_state(demo_run_->demo.streams[0]); st && world_->resume(*st)) {
                flow_.resume(0, 2, (100 - s64(st->units) + s64(st->remainder) - 1) / 3, 100, world_->cars()[0].course_v);
                world_->set_crossings(0, flow_.car(0).counter);
            }
        license_failed_ = false, cones_met_ = 0, graded_place_ = 0, run_ended_ = run_driven_ = false;
        wrong_way_ = false, wrong_prev_ = -1;
        judge_ = race::LicenseJudge(judge_.rules());
        finish_camera_.angle = 0;
        goal_prev_ = -1;
        board_.begin(world_->car_count(), 0, lap_words_[0], lap_words_[1]);
        camera_.reset(world_->cars()[0]);
        history_.clear(), run_dist_.clear(), saved_recording_.clear(), behind_.reset();
        if (audio::RaceSound* sound = world_->sound()) {
            sound->forget_recording();
            // (built for its menus, the run is not begun yet: its page comes first)
            if (built_ || setup_.sequence.empty()) sound->begin_run(audio::race_music(setup_.sequence, setup_.race, last_race_), int(flow_.setup().start));
            license_sounded_ = false;
        }
        split_prev_.clear(), split_first_.clear(), split_steps_ = -1;
        distance_ = 0, bspec_distance_ = 0, shown_steps_ = 0;
        replaying_ = false, over_ = false;
        event_steps_ = -1;
        release_past_line();
        begin_start_camera(false);
        if (net_) attach_online(net_); // (the world's session may be a new one)
    }

    // The scene behind the race's menus: the cars standing on the grid, the course's scenic shots
    // (the last run is kept for its replay).
    void stand(bool guide = false) {
        // Prologue's page (Gran Turismo 3's road) runs the race behind it as a replay shows one: the computer drives the
        // player's car too, the start's hold under the replay's start shots, then the course's replay cameras, the cars
        // not heard (the original's Tsukuba page, reference/14-prologue/round5). A lesson's page plays the lesson's recorded run
        // (✔ the original's lesson 1, its car on our run's line: reference/37-prologue-medal).
        preview_ = gt3_road_ && setup_.skip == 0;
        preview_pads_ = preview_ ? &recorded_run() : nullptr;
        if (preview_pads_ && preview_pads_->pads.empty()) preview_pads_ = nullptr;
        preview_at_ = 0;
        // The lesson's guide film: the course's scenic shots once, then the run under the replay's monitors (✔ lesson 1's).
        guide_ = !guide || !preview_ ? 0 : scenic_script_.records.empty() ? 2 : 1;
        guide_done_ = false, guide_steps_ = 0;
        if (preview_) world_->set_computer_player(!preview_pads_);
        if (preview_pads_) {
            world_->set_entry_overrides(recorded_entry()); // (restarts)
        } else if (ran_ || preview_) world_->restart();
        ran_ = true;
        menu_scene_ = true, replaying_ = false, over_ = false;
        prev_cars_.clear();
        world_->quiet();
        world_->clear_effects();
        scenic_.reset(&scenic_script_);
        start_camera_.begin(nullptr, false, 0, {}, world_->cars()[0]);
        if (preview_) {
            preview_hold_ = preview_start();
            if (preview_pads_) { // (its crossings of the line: the car let go by the driver that brings it there, the places)
                preview_flow_.begin(flow_setup(), world_->cars());
                for (size_t i = 0; i < world_->car_count(); ++i) world_->set_crossings(i, preview_flow_.car(i).counter), world_->set_rank(i, preview_flow_.place(i));
                release_past_line();
            }
            camera_.reset(world_->cars()[0]);
            replay_camera_.reset();
            tv_valid_ = false;
            begin_start_camera(true, preview_hold_);
        }
    }
    // The guide film: whether the run is on (not the shots before it), its seconds from its start's end, and its end.
    bool guide_run() const { return menu_scene_ && guide_ == 2; }
    bool guide_done() const { return guide_ != 0 && guide_done_; }
    float guide_time() const { return std::max(0.0f, float(guide_steps_ - preview_start()) / 60); }
    // The start's hold of the page's run: a lesson's 4 s (none on the move), else 10 s.
    int preview_start() const { return !preview_pads_ ? 10 * 60 : setup_.test_starts.empty() ? 4 * 60 : 0; }
    // The cars that start past the line.
    void release_past_line() {
        for (size_t i = 0; i < setup_.test_starts.size() && i < world_->car_count(); ++i)
            if (setup_.test_starts[i][1] > 0) world_->release_start(i);
    }
    bool has_run() const { return !history_.empty(); }
    size_t run_steps() const { return history_.size(); }

    // The last run as a saved replay, and a saved replay played back.
    race::ReplayFile replay_file(const std::string& sequence, const std::string& race_label) const {
        race::ReplayFile f;
        f.course = setup_.course, f.sequence = sequence, f.race = race_label, f.laps = setup_.laps;
        for (size_t i = 0; i < world_->car_count(); ++i) f.cars.push_back({world_->setup().entries[i].label, world_->setup().entries[i].paint});
        f.steps = history_;
        f.other.emplace_back(race::kReplayDetails, race::replay_details(history_));
        if (recording_ && !records_.empty()) try {
                const Bytes b = recording();
                if (!b.empty()) f.other.emplace_back(race::ReplayFile::kRecording, b);
            } catch (const std::exception& e) {
                LOG_WARN("Drive: the run's recording: %s", e.what());
            }
        return f;
    }
    // Whether a run of this race is kept as the program keeps it too (race/race_recording.h): an arcade single race or a license test
    // from its start.
    bool recordable() const {
        if (!disc_ || !db_ || db_->prologue() || setup_.event_row.empty() || setup_.skip != 0 || setup_.auto_drive || !world_->simulated()) return false;
        if (is_license(setup_.sequence)) return setup_.conductor == race::Conductor::License || setup_.conductor == race::Conductor::Training;
        return setup_.sequence == "single_race" && world_->computer_drivers();
    }
    Bytes recording() const {
        race::RecordingSetup rs;
        const db::Labels *races = db_->labels("RACE"), *courses = db_->labels("COURSE");
        const std::string name = setup_.race.empty() ? "sample_entry_6" : setup_.race;
        std::optional<u32> event;
        for (const std::string& l : {name, "us" + name, "jp" + name}) // (a test's code by its region: find_event)
            if (!event && races) event = races->id(l);
        const std::optional<u32> course = courses ? courses->id(setup_.course) : std::nullopt;
        if (!event || !course) return {};
        rs.event = u64(races->table_id) << 32 | *event, rs.course = u64(courses->table_id) << 32 | *course;
        if (is_license(setup_.sequence)) rs.race_class = setup_.conductor == race::Conductor::Training ? "RaceTraining" : "RaceLicense";
        rs.bspec = watched_, rs.laps = setup_.laps;
        rs.penalty = setup_.penalty_option, rs.tire_damage = setup_.tire_damage;
        if (bspec_start_) rs.driver = *bspec_start_;
        const auto& me = flow_.car(0);
        rs.time_ms = flow_.player_finished() ? flow_.run_ms(0) : std::max<s64>(0, me.last_lap_ms);
        rs.best_ms = me.laps_done > 0 ? me.best_lap_ms : -1;
        if (is_license(setup_.sequence)) { // (a test's time is its best too; a failed one has none)
            if (license_failed_ || !flow_.player_finished()) rs.time_ms = 0;
            rs.best_ms = rs.time_ms > 0 ? rs.time_ms : -1;
        }
        rs.playlist = setup_.playlist_bytes;
        std::vector<sim::Entry> entries = world_->session_entries();
        for (size_t i = 0; i < entries.size(); ++i) entries[i].equipment.bytes[0x18] = u8(world_->setup().entries[i].paint);
        return race::recording_file(race::make_recording(rs, entries, records_), sim::Program::load(*disc_), u32(std::time(nullptr)));
    }
    void play_saved(race::ReplayFile&& f) {
        history_ = std::move(f.steps);
        for (const auto& [tag, b] : f.other)
            if (tag == race::kReplayDetails) race::apply_details(history_, b);
        saved_recording_ = f.recording() ? *f.recording() : Bytes{};
        if (audio::RaceSound* sound = world_->sound()) sound->forget_recording(); // (a saved run: heard from its states)
        for (std::vector<race::CarState>& s : history_) s.resize(world_->car_count());
        ran_ = true;
        // (The replay's music: the play list's next piece, as a race's first.)
        if (audio::RaceSound* sound = world_->sound()) sound->rolling();
        start_replay();
    }

    void begin_start_camera(bool replay, int hold_steps = -1) {
        const race::CameraMotion& m = replay ? replay_motion_ : start_motion_;
        const course::Runway& road = world_->course().road();
        course::Runway::Place grid;
        if (!road.grid.empty()) grid = road.grid[0];
        // (The long clip's anchor: the place, its height from the road under it (0x3FCA08); a grid place's
        // own height is rough on some courses: Apricot Hill's are 4 m under the road, playtest B25.)
        if (float y; !road.grid.empty() && road.height_at(grid.pos.x, grid.pos.z, grid.pos.y + 50.0f, y)) grid.pos.y = y;
        const int hold = hold_steps >= 0 ? hold_steps : replay ? flow_.hold_total() : flow_.hold_left();
        if (m.clips.empty() && !start_paths_[replay ? 1 : 0].records.empty()) {
            start_camera_.begin_prologue(&start_paths_[replay ? 1 : 0], replay, hold);
            return;
        }
        start_camera_.begin(m.clips.empty() ? nullptr : &m, replay, hold, grid, replay && hold_steps < 0 && !history_.empty() ? history_[0][0] : world_->cars()[0]);
    }

    // One simulation step: the player's car from the pad (or the simple driver), the others by the
    // simple driver, each on its own side of the line.
    // `player`: the controller's answer (input/player_controls.h).
    void step(const input::RaceInput& player, bool auto_drive) {
        if (menu_scene_ && preview_ && guide_ == 1) {
            const size_t shot = scenic_.shot();
            scenic_.step();
            if (scenic_.shot() != shot && scenic_.dissolve() > 0) begin_dissolve(int(std::lround(scenic_.dissolve_seconds() * 60)));
            step_dissolve();
            if (scenic_.shot() < shot || scenic_script_.records.size() == 1) guide_ = scenic_.shot() < shot ? 2 : guide_;
            return;
        }
        if (menu_scene_ && preview_) {
            keep_previous();
            if (guide_ == 2) {
                ++guide_steps_;
                const size_t length = std::max<size_t>(recorded_run().pads.size(), 600);
                if (size_t(guide_steps_) > length + 60) guide_done_ = true;
            }
            const bool hold = preview_hold_ > 1; // (as RaceFlow::holding)
            if (preview_hold_ > 0) --preview_hold_;
            for (race::CarInput& in : inputs_) in = {};
            if (preview_pads_) {
                if (preview_at_ < preview_pads_->pads.size()) {
                    inputs_[0] = sim::ai::pad_input(replay::PadRecord{preview_pads_->pads[preview_at_].bytes});
                    inputs_[0].pad = &preview_pads_->pads[preview_at_];
                }
                if (const char* every = SDL_getenv("GT4R_DEMO_LOG"); every && std::atoi(every) > 0 && preview_at_ % size_t(std::atoi(every)) == 0) {
                    const race::CarState& c = world_->cars()[0];
                    LOG_INFO("Drive: the page's run step %zu: %.2f km/h at (%.2f, %.2f)", preview_at_, double(c.speed) * 3.6, double(c.position.x), double(c.position.z));
                }
                ++preview_at_;
                world_->step(inputs_, hold, false, u32(std::max(0, preview_hold_)));
                const race::RaceFlow::Events ev = preview_flow_.step(world_->cars());
                for (const size_t i : ev.started) world_->release_start(i);
                for (size_t i = 0; i < preview_flow_.car_count(); ++i) world_->set_crossings(i, preview_flow_.car(i).counter), world_->set_rank(i, preview_flow_.place(i));
            } else world_->step(inputs_, hold, true);
            if (preview_pads_ && preview_at_ > preview_pads_->pads.size() && guide_ == 0) { // (the recording has run out: again)
                stand();
                return;
            }
            start_camera_.step();
            camera_.step(world_->cars()[0], kStep);
            step_tv(world_->cars()[0], 0);
            return;
        }
        if (menu_scene_) {
            const size_t shot = scenic_.shot();
            scenic_.step();
            // The scenic shots' cuts dissolve (mode 5: the ended record's seconds).
            if (scenic_.shot() != shot && scenic_.dissolve() > 0) begin_dissolve(int(std::lround(scenic_.dissolve_seconds() * 60)));
            step_dissolve();
            return;
        }
        keep_previous();
        if (replaying_) {
            // The recorded run again; it ends at its end.
            if (replay_at_ + 1 >= history_.size()) {
                replay_done_ = true;
                return;
            }
            ++replay_at_;
            step_behind();
            start_camera_.step();
            step_view(history_[replay_at_]);
            const size_t car = focus();
            camera_.step(history_[replay_at_][car], kStep);
            step_gear(history_[replay_at_][car]);
            if (diving()) step_dive(history_[replay_at_]);
            else step_tv(history_[replay_at_][car], car);
            // The music ducked under the course's replay-script shots (the designer's cameras).
            if (audio::RaceSound* sound = world_->sound()) sound->set_replay_shot(tv_valid_ && !start_camera_.active());
            hear(history_[replay_at_], false);
            return;
        }
        const std::span<const race::CarState> cars = world_->cars();
        ++shown_steps_;
        const bool hold = flow_.holding();
        // A rolling start: the player's car is driven for him until it takes the line.
        auto_drive = auto_drive || flow_.player_finished() || handing_over();
        // The computer drivers (race_world.h) drive their cars; without them the simple driver follows
        // the road's line.
        const bool drivers = world_->computer_drivers();
        for (size_t c = 0; c < inputs_.size(); ++c)
            inputs_[c] = hold || (drivers && (c > 0 || watched_ || setup_.auto_drive)) || (c == 0 && !auto_drive) ? race::CarInput{}
                                                                                       : race::follow_line(world_->line(), cars[c], c & 1 ? 2.0f : -2.0f, 1.0f - 0.05f * float(c));
        // (What the pad asks during the hand-over: "GET READY TO DRIVE" once the throttle is held.)
        // (The request's part of the input phase: B-spec's commands and the pit menu's choice in the frame, taken as a recording's are.)
        sim::rules::PadFrame pad = watched_ ? sim::rules::PadFrame{} : player.frame;
        sim::rules::post_mail(pad, mail_, watched_ ? 1 : 0, true);
        const auto pedal = [](bool analog, u16 v) { return analog ? float(v) / 4096.0f : v ? 1.0f : 0.0f; };
        if (handing_over()) throttle_held_ = pedal(pad.flags & 2, pad.throttle) > 0.5f;
        if (!auto_drive) {
            // The program's own frame, read by the input phase as it is; the fields beside it are the stand-in simulation's.
            pad.to_bytes(player_frame_.bytes.data());
            race::CarInput& me = inputs_[0];
            me = {};
            me.pad = &player_frame_;
            me.steer = pad.flags & 1 ? -float(pad.steer) / 4096.0f : pad.steer > 0 ? -1.0f : pad.steer < 0 ? 1.0f : 0.0f;
            me.reverse = pad.back != 0 && pedal(pad.flags & 8, pad.back) >= pedal(pad.flags & 2, pad.throttle);
            me.throttle = std::max(pedal(pad.flags & 2, pad.throttle), pedal(pad.flags & 8, pad.back)), me.brake = pedal(pad.flags & 4, pad.brake);
            me.handbrake = pad.third != 0, me.shift_up = pad.shift > 0, me.shift_down = pad.shift < 0;
        }
        if (!demo_run_) {
            if (recording_) {
                replay::PadRecord& r = records_.emplace_back();
                pad.to_bytes(r.bytes.data());
            }
            demo_commands({pad.buttons[0], pad.buttons[1], pad.buttons[2], pad.buttons[3], pad.buttons[4], pad.buttons[5]});
        }
        if (lesson_pads_ && !flow_.player_finished()) {
            auto_drive = false;
            inputs_[0] = {};
            if (lesson_at_ < lesson_pads_->pads.size()) {
                inputs_[0] = sim::ai::pad_input(replay::PadRecord{lesson_pads_->pads[lesson_at_].bytes});
                inputs_[0].pad = &lesson_pads_->pads[lesson_at_];
            }
            ++lesson_at_;
        }
        // Prologue's license test ended: its driver drives on or stops the car, the other cars' drive on (license_prologue.h).
        if (gt3_road_ && is_license(setup_.sequence) && flow_.player_finished() && !run_ended_) {
            run_ended_ = true;
            const bool others = world_->car_count() > 1;
            const race::LicenseRules& r = judge_.rules();
            run_driven_ = world_->finish_car(0, license_failed_ ? race::prologue::fail_mode(r, others, judge_.fail()) : race::prologue::end_mode(r, others), true);
            for (size_t i = 1; i < world_->car_count(); ++i) world_->finish_car(i, 3, false);
        }
        if (finish_view() && !run_driven_) inputs_[0] = {}, inputs_[0].brake = 1, auto_drive = false;
        // A demonstration: car 0 by its recording, step by step from the race's first.
        const bool demo = demo_run_.has_value();
        if (demo) {
            auto_drive = false;
            demo_commands(demo_run_->mail(demo_step_));
            if (!demo_bspec_) inputs_[0] = demo_run_->input(demo_step_);
            ++demo_step_;
        }
        const bool opening = opening_tv();
        if (net_) net_->before_step(net_clock());
        world_->step(inputs_, hold, demo ? demo_bspec_ : (auto_drive || watched_ || setup_.auto_drive), u32(std::max(0, flow_.hold_left() - 1)));
        if (recording_ && !demo && world_->pit_took_default(0) && records_.back().bytes[0x10] == 0)
            for (size_t k = 0; k < 4; ++k) records_.back().bytes[0x10 + k] = k ? 0xFE : 1; // (0x35BF08 posts it in the step's mail)
        // GT4R_DEMO_LOG=<n>: a demonstration's car every n steps (the record's x, y: the course's x, -z), to compare with a
        // run of the program's own step (gt4r_diff demo).
        if (demo)
            if (const char* every = SDL_getenv("GT4R_DEMO_LOG"); every && std::atoi(every) > 0 && (demo_step_ - 1) % size_t(std::atoi(every)) == 0) {
                const race::CarState& c = world_->cars()[0];
                LOG_INFO("Drive: demo step %zu: %.2f km/h at (%.3f, %.3f) v %.2f%s", demo_step_ - 1, double(c.speed) * 3.6, double(c.position.x), double(-c.position.z),
                         double(c.course_v), (std::string(hold ? " held" : "") + " fuel " + std::to_string(world_->fuel(0)) + " car1 v " + std::to_string(world_->car_count() > 1 ? world_->cars()[1].course_v : 0.0f)).c_str());
            }
        // GT4R_RUN_LOG=<n>: every car's distance, speed and place every n steps of a run (to compare with the original's states).
        if (const char* every = SDL_getenv("GT4R_RUN_LOG"); every && std::atoi(every) > 0 && ++run_steps_ % size_t(std::atoi(every)) == 0)
            for (size_t i = 0; i < world_->car_count(); ++i) {
                const race::CarState& c = world_->cars()[i];
                LOG_INFO("Drive: run step %zu car %zu: d %.3f at %.4f km/h (%.3f, %.3f) gear %d lap %d", run_steps_, i, double(c.course_v), double(c.speed) * 3.6, double(c.position.x), double(-c.position.z), c.gear,
                         flow_.car(i).counter);
            }
        // (A rolling start's clock stands until the first car takes the line: RaceFlow.)
        // A free run's hand-over, 3 s from the set-up (DynamicsConductorFreeRun::vf14).
        if (!hold && world_->count_rolling(0)) world_->release_start(0), flow_.signal_start();
        const race::RaceFlow::Events ev = flow_.step(world_->cars());
        if (net_) step_online(ev);
        // A car held in its rolling start let go as its start is counted (vf20 0x347928, the license's 0x3F64D0).
        for (const size_t i : ev.started) world_->release_start(i);
        for (size_t i = 0; i < flow_.car_count(); ++i) world_->set_crossings(i, flow_.car(i).counter), world_->set_rank(i, flow_.place(i));
        if (!ev.finished.empty()) board_.finished(flow_, ev.finished);
        for (const size_t i : ev.finished) world_->finish_car(i, i == 0 && is_license(setup_.sequence));
        // Each car's distance from the line (crossings x lap + along) each step from the start: the gaps.
        if (flow_.steps_since_start() >= 0) {
            const std::span<const race::CarState> now = world_->cars();
            run_dist_.resize(now.size());
            for (size_t i = 0; i < now.size(); ++i) run_dist_[i].push_back(float(flow_.car(i).counter) * world_->lap_length() + now[i].course_v);
        }
        if (const float run = std::fabs(world_->cars()[0].speed) * kStep; !hold) distance_ += run, bspec_distance_ += watched_ ? run : 0;
        {
            // The player's sideways acceleration (the G meter), smoothed over a few steps.
            const race::CarState& c = world_->cars()[0];
            const float ax = (c.velocity.x - last_v_.x) / kStep, ay = (c.velocity.y - last_v_.y) / kStep, az = (c.velocity.z - last_v_.z) / kStep;
            const float g = (ax * c.right.x + ay * c.right.y + az * c.right.z) / 9.81f;
            lateral_g_ += (std::clamp(g, -4.0f, 4.0f) - lateral_g_) * 0.2f;
            last_v_ = c.velocity;
        }
        if (audio::RaceSound* sound = world_->sound()) {
            if (hold) sound->count(flow_.hold_left());
            // A rolling start: the racing music with the run's beginning (begin_run), the start's sound when the player's
            // car takes the line; a moving one its music and the sound then.
            const bool runs_up = flow_.setup().start == race::StartType::Rolling || flow_.setup().start == race::StartType::Moving;
            if (ev.start && runs_up && flow_.setup().start == race::StartType::Rolling) sound->start_signal();
            else if (ev.start) sound->start(); // (a standing start: the last held step is let go, so not counted above)
            if (ev.player_finished && !is_license(setup_.sequence)) sound->finish();
        }
        if (ev.clock_started) LOG_INFO("Drive: the rolling start's clock starts (the first car takes the line); the player %.1f m from it", double(to_line_raw()));
        if (opening_was_ && !opening_tv()) LOG_INFO("Drive: the opening shots end %.1f m from the line, %.2f s on the clock", double(to_line_raw()), double(flow_.time_ms()) / 1000);
        opening_was_ = opening_tv();
        if (ev.start && flow_.setup().start == race::StartType::Rolling)
            LOG_INFO("Drive: the player's car takes the line at %.1f km/h, %.2f s on the clock", double(world_->cars()[0].speed * 3.6f), double(flow_.time_ms()) / 1000);
        if (is_license(setup_.sequence) || setup_.sequence == "mission") judge_license();
        if (is_license(setup_.sequence)) judge_goal();
        if (!flow_.player_finished()) graded_place_ = flow_.place(0);
        step_wrong_way(auto_drive);
        if (demo_run_ && flow_.player_finished() && !demo_logged_) {
            demo_logged_ = true;
            LOG_INFO("Drive: the demonstration %s ended at step %zu: %s %lld ms (recorded %d ms)", demo_run_->label.c_str(), demo_step_ - 1, license_failed_ ? "failed" : "timed",
                     (long long)flow_.run_ms(0), demo_run_->recorded_ms());
        }
        if (demo_run_ && ev.lap_done == 0) LOG_INFO("Drive: the demonstration %s: lap %d in %lld ms, at step %zu", demo_run_->label.c_str(), flow_.car(0).laps_done, (long long)flow_.car(0).last_lap_ms, demo_step_ - 1);
        if (ev.lap_done == 0) event_steps_ = 0, event_lap_ = flow_.car(0).laps_done, split_steps_ = -1;
        step_splits();
        if (finish_view()) finish_camera_.angle += 0.5f;
        beeps_ += ev.beep;
        start_camera_.step();
        if (const float d = start_camera_.dissolve(); d > 0) begin_dissolve(int(std::lround(d * 60)));
        if (mount_ != kTvView && !opening && !demo) step_dissolve(); // (else step_tv's)
        if (demo) step_view(world_->cars());
        const size_t f = focus();
        camera_.step(world_->cars()[f], kStep);
        step_gear(world_->cars()[f]);
        if (demo && diving()) step_dive(world_->cars());
        else if (mount_ == kTvView || opening || demo) step_tv(world_->cars()[f], f);
        if (event_steps_ >= 0) ++event_steps_;
        if (split_steps_ >= 0) ++split_steps_;
        if (history_.size() < kHistory) history_.emplace_back(world_->cars().begin(), world_->cars().end());
        hear(world_->cars(), true);
    }

    // An online race: the race's clock, its state after a step to the others, their cars' laps here.
    void attach_online(net::NetRace* net) {
        net_ = net;
        if (net_ && world_->session()) net_->attach(*world_->session());
    }
    s64 net_clock() const { return net::NetRace::race_clock(flow_.hold_left(), flow_.clock()); }
    void step_online(const race::RaceFlow::Events& ev) {
        std::vector<double> along(flow_.car_count());
        const std::span<const race::CarState> cars = world_->cars();
        for (size_t i = 0; i < along.size() && i < cars.size(); ++i) {
            const bool behind = flow_.car(i).counter == 0 && cars[i].course_v > world_->lap_length() * 0.5f; // (short of the line on the grid)
            along[i] = double(flow_.car(i).counter) * world_->lap_length() + cars[i].course_v - (behind ? world_->lap_length() : 0.0f);
        }
        net_->set_progress(along, world_->lap_length());
        if (const net::NetField* f = net_->field())
            for (size_t i = 1; i < f->size(); ++i) world_->set_car_alpha(i, f->ghost(i) ? 0.5f : 1.0f);
        net_->after_step(net_clock(), inputs_[0], flow_.car(0).laps_done);
        for (const size_t i : ev.finished) {
            if (i == 0) net_->finished(flow_.car(0).finish_units, flow_.car(0).best_lap_ms >= 0 ? flow_.car(0).best_lap_ms * 3 : -1);
            else net_->observed_finish(i, flow_.car(i).seen_units);
        }
        for (const net::NetRace::Finish& f : net_->take_finishes()) flow_.claim_finish(f.car, f.units, f.best >= 0 ? f.best / 3 : -1);
    }

    // The gear's flash (0x3A6D60): lit as a new gear is taken with the clutch locked, off in 0.15 s.
    void step_gear(const race::CarState& c) {
        if (c.clutch_closed && c.gear != gear_shown_) gear_shown_ = c.gear, gear_flash_ = 1;
        else gear_flash_ = std::max(0.0f, gear_flash_ - kStep / 0.15f);
    }

    // The section times: each car through the course's split points (the road's first checkpoint list,
    // race/sections.h) on its laps from the start; the player's time and his gap to the first car there
    // (❓ the program's event 15 and its gap's partner, the car ahead or the leader, not read).
    void step_splits() {
        if (splits_.empty() || flow_.steps_since_start() < 0) return;
        if (const int kind = flow_.setup().test_kind; kind != 0 && kind != 6 && world_->car_count() > 1) return; // (0x3F63A0: no section times in a test with other cars)
        const std::span<const race::CarState> cars = world_->cars();
        split_prev_.resize(cars.size(), -1);
        for (size_t i = 0; i < cars.size(); ++i) {
            const float v = cars[i].course_v, before = split_prev_[i];
            split_prev_[i] = v;
            const int counter = flow_.car(i).counter;
            if (before < 0 || counter < 1 || !(v > before) || flow_.car(i).finished) continue;
            for (size_t k = 0; k < splits_.size(); ++k) {
                if (!(before < splits_[k] && splits_[k] <= v)) continue;
                const s64 t = flow_.time_ms();
                auto [it, first] = split_first_.try_emplace({counter, int(k)}, t);
                if (i == 0) {
                    split_index_ = int(k), split_ms_ = flow_.lap_ms(0), split_steps_ = 0;
                    split_gap_ms_ = first ? 0 : t - it->second;
                }
            }
        }
    }

    // The step's smoke and sound, heard from the camera.
    void hear(std::span<const race::CarState> cars, bool live) {
        const race::ViewPose pose = current_pose(cars, false);
        const auto& m = pose.view.m;
        audio::SoundListener l;
        l.position = {pose.eye.x, pose.eye.y, pose.eye.z};
        l.right = {m[0], m[4], m[8]}, l.up = {m[1], m[5], m[9]}, l.forward = {-m[2], -m[6], -m[10]};
        const size_t heard = viewing() ? focus() : 0;
        l.velocity = cars[heard].velocity;
        const bool own = !start_camera_.active() && !replaying_ && mount_ != kTvView;
        l.kind = pose.body_hidden ? audio::SoundListener::Kind::InCar : own && race::DriveCamera::chase(mount_) ? audio::SoundListener::Kind::Chase
                                                                                                                 : audio::SoundListener::Kind::Other;
        world_->present(cars, &l, int(heard), live, replaying_ && behind_ ? &behind_->session() : nullptr);
    }
    void pause_sound(bool paused) {
        if (audio::RaceSound* sound = world_->sound()) sound->pause(paused);
    }

    // The run is over (the results page left): the replay plays at once when the player drove a
    // lap or more.
    // `replay`: the sequence plays it (the single race and Gran Turismo mode's events, after a driven run).
    void end_run(bool replay = true) {
        over_ = true;
        if (replay && !watched_ && flow_.car(0).counter >= 2) start_replay();
    }

    void start_replay() {
        if (history_.empty() || replaying_) return;
        replaying_ = true, replay_done_ = false;
        begin_view();
        menu_scene_ = false; // (asked for from the race's menus: the scene behind them ends)
        prev_cars_.clear();
        if (audio::RaceSound* sound = world_->sound()) sound->replay();
        replay_at_ = 0;
        begin_behind();
        camera_.reset(history_[0][0]);
        replay_camera_.reset();
        world_->clear_effects();
        begin_start_camera(true);
    }
    // A saved replay's race run again behind it from its recording, for what its states do not hold (the sound's inputs: hits, wheels,
    // the engine's torques); left when the states are not its.
    void begin_behind() {
        behind_.reset();
        if (saved_recording_.empty() || !disc_ || !db_) return;
        try {
            std::string error;
            std::optional<sim::ai::DemoRun> run = sim::ai::DemoRun::load_replay(saved_recording_, sim::Program::load(*disc_), "saved", &error);
            if (!run) throw FormatError(error);
            behind_ = std::make_unique<race::RecordedRace>(*disc_, *db_, std::move(*run));
            step_behind();
        } catch (const std::exception& e) {
            LOG_WARN("Drive: the saved replay's recording: %s", e.what());
            behind_.reset();
        }
    }
    void step_behind() {
        if (!behind_) return;
        behind_->step();
        const std::span<const race::CarState> cars = behind_->session().cars();
        const std::vector<race::CarState>& shown = history_[replay_at_];
        for (size_t i = 0; i < cars.size() && i < shown.size(); ++i)
            if (std::memcmp(&cars[i].position, &shown[i].position, sizeof cars[i].position) != 0) {
                LOG_WARN("Drive: the saved replay's recording leaves its states at step %zu (car %zu): heard from the states", replay_at_ + 1, i);
                return behind_.reset();
            }
        if (replay_at_ + 1 == history_.size()) LOG_INFO("Drive: the saved replay's recording ran its %zu steps beside it", history_.size());
    }
    // A run watched with a replay's controls (the program's play modes 2 and 3).
    bool viewing() const { return replaying_ || demo_run_.has_value(); }
    void set_replay_style(race::ReplayStyle s) { replay_style_ = s; }
    const race::ReplayView& view() const { return view_; }
    void begin_view() {
        view_.begin(replay_style_);
        if (replay_style_ == race::ReplayStyle::Dive) begin_dive();
        replay_focus_ = 0, view_pad_ = {}, mode_time_ = -1, music_off_ = false, view_time_ = 0;
        replay_camera_.set_zoom(1);
    }
    // The pad's keys for the next step (the ones that go down are kept until it).
    void view_input(const race::ReplayPad& pad) {
        race::ReplayPad& q = view_pad_;
        q.up |= pad.up, q.down |= pad.down, q.cross |= pad.cross, q.square |= pad.square, q.circle |= pad.circle;
        q.l1 |= pad.l1, q.r1 |= pad.r1, q.l2 |= pad.l2, q.r2 |= pad.r2, q.l3 |= pad.l3;
        q.left_held = pad.left_held, q.right_held = pad.right_held, q.triangle_held = pad.triangle_held;
    }
    void step_view(std::span<const race::CarState> cars) {
        view_time_ += kStep / float(view_.speed());
        if (mode_time_ >= 0) mode_time_ += kStep / float(view_.speed());
        const race::ReplayView::Events e = view_.step(view_pad_);
        view_pad_ = {false, false, false, false, false, false, false, false, false, false, view_pad_.left_held, view_pad_.right_held, view_pad_.triangle_held};
        auto say = [&](const char* word) { mode_word_ = word, mode_time_ = 0; };
        if (e.style) say(view_.style() == race::ReplayStyle::Display ? "ReplayModeNormalDisplay" : view_.style() == race::ReplayStyle::Dive ? "ReplayModeDive" : "ReplayModeNormal");
        if (e.speed) say(view_.fast() ? "ReplayModeAccelerate" : "ReplayModeNormalSpeed");
        if (e.music) {
            music_off_ = !music_off_;
            if (audio::RaceSound* sound = world_->sound()) sound->set_music_on(!music_off_);
            say(music_off_ ? "ReplayModeBGMOff" : "ReplayModeBGMOn");
        }
        if (e.select_car != 0) select_replay_car(cars, e.select_car);
        replay_camera_.set_zoom(view_.zoom());
        if (e.view || e.style) view_cut_ = true;
        if (e.style || (e.view && !view_.ride_on())) replay_camera_.reset();
        step_music();
        dive_asked_ = e.director || (e.style && view_.style() == race::ReplayStyle::Dive);
        if (e.style && view_.style() == race::ReplayStyle::Dive) begin_dive();
    }
    bool diving() const { return viewing() && view_.style() == race::ReplayStyle::Dive && dive_loaded_ && !start_camera_.active(); }
    void begin_dive() {
        if (!dive_tried_) dive_tried_ = true, dive_loaded_ = world_->load_dive(dive_);
        dive_.reset(u32(SDL_GetTicksNS() >> 10) | 1, focus());
        dive_clock_.reset();
        dive_asked_ = true;
        if (music_off_) { // (Dive's cuts need the music: 0x33C610)
            music_off_ = false;
            if (audio::RaceSound* sound = world_->sound()) sound->set_music_on(true);
        }
    }
    // Where the piece of music is, by its own steps since it began (its markers: Dive's cuts).
    void step_music() {
        const audio::RaceSound* sound = world_->sound();
        const std::string playing = sound ? sound->report().music : std::string();
        if (playing != markers_for_) markers_for_ = playing, markers_ = world_->music_markers(playing), music_time_ = music_before_ = 0;
        music_before_ = music_time_;
        if (!music_off_) music_time_ += kStep / float(view_.speed());
        if (markers_.length > 0 && music_time_ >= markers_.length) music_time_ -= markers_.length;
    }
    void step_dive(std::span<const race::CarState> cars) {
        const bool beat = dive_clock_.step(markers_.next(music_time_), markers_.passed(music_before_, music_time_));
        race::ViewPose p;
        race::DiveDirector::World w = world_->dive_world(cars, replay_script_);
        if (replaying_)
            w.ahead = [this](size_t car, float seconds) -> std::optional<RVec> {
                const size_t at = replay_at_ + size_t(std::max(seconds, 0.0f) * 60);
                if (at >= history_.size() || car >= history_[at].size()) return std::nullopt;
                return RVec{history_[at][car].position.x, history_[at][car].position.y, history_[at][car].position.z};
            };
        tv_valid_ = dive_.step(w, beat || dive_asked_, dive_clock_.length(), p);
        dive_asked_ = false;
        if (tv_valid_) {
            // (kept as the program's angle, as the replay cameras' shots are)
            p.fov = 2 * std::atan(std::tan(p.fov * 0.5f) * (293.1175f / 426.6667f));
            tv_pose_ = p;
        }
        if (dive_.is_cut()) {
            view_cut_ = true;
            if (dive_.car() != replay_focus_) replay_focus_ = dive_.car(), camera_.reset(cars[replay_focus_]), camera_prev_ = camera_;
        }
        step_dissolve();
    }
    // Up / Down: the car ahead or behind in the race's order (0x3FD5A8), the shot chosen anew.
    void select_replay_car(std::span<const race::CarState> cars, int dir) {
        if (cars.size() < 2) return;
        const float lap = world_->lap_length();
        std::vector<size_t> order(cars.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return float(cars[a].lap) * lap + cars[a].course_v > float(cars[b].lap) * lap + cars[b].course_v; });
        size_t at = 0;
        for (size_t k = 0; k < order.size(); ++k)
            if (order[k] == focus()) at = k;
        replay_focus_ = order[(at + order.size() + size_t(dir > 0 ? 1 : order.size() - 1)) % order.size()];
        camera_.reset(cars[replay_focus_]);
        camera_prev_ = camera_;
        replay_camera_.reset();
        view_cut_ = true;
    }
    bool replaying() const { return replaying_; }
    bool replay_done() const { return replay_done_; }
    void stop_replay() { replaying_ = false, replay_done_ = true; }

    // A demonstration ("View a demo run", sim/ai/demo_driver.h): the race set up with the file's cars
    // (their equipment, places, gearboxes and aids) and car 0 given the recorded pad record at every
    // step from the race's first (the start's hold included) until the recording runs out, then the
    // demonstration ends; shown as a replay is (the course's cameras, the opening card and the logo,
    // no race display). False without a recording for `label`.
    bool begin_demo(App& app, const std::string& label) {
        demo_run_.reset();
        Disc* disc = app.disc();
        if (!disc) return false;
        try {
            const sim::Program program = sim::Program::load(*disc);
            std::string error;
            for (const std::string& l : {label, "us" + label, "jp" + label}) {
                demo_run_ = sim::ai::DemoRun::load(*disc, program, l, &error);
                if (demo_run_) break;
            }
            if (!demo_run_) {
                LOG_WARN("Demonstration %s: %s", label.c_str(), error.c_str());
                return false;
            }
        } catch (const std::exception& e) {
            LOG_WARN("Demonstration %s: %s", label.c_str(), e.what());
            return false;
        }
        enter_demo(false);
        return true;
    }
    // A recorded race of the disc's played: its entries, car 0 by its records, or by its driver and the recorded commands (B-spec).
    void play_demo(const sim::ai::DemoRun& run, int unseen) {
        demo_run_ = run;
        enter_demo(run.demo.race_mode == 1);
        for (int i = 0; i < unseen && !demo_over(); ++i) step({}, false);
    }
    void enter_demo(bool bspec) {
        const std::vector<sim::Entry> entries = demo_run_->entries();
        std::vector<std::optional<WorldEntry>> over(world_->car_count());
        for (size_t i = 0; i < entries.size() && i < over.size(); ++i) {
            WorldEntry e = world_->setup().entries[i];
            e.equipment = entries[i].equipment, e.grid_place = entries[i].grid_place;
            e.automatic = entries[i].automatic, e.asm_on = entries[i].asm_on, e.tcs_on = entries[i].tcs_on;
            e.wheel_aid = entries[i].wheel_aid, e.condition = entries[i].condition, e.driver = entries[i].driver;
            over[i] = e;
        }
        world_->set_computer_player(bspec);
        world_->set_entry_overrides(std::move(over));
        demo_step_ = 0, demo_logged_ = false, demo_bspec_ = bspec;
        begin_run(false, true);
        begin_view();
        world_->pits_by_record();
        LOG_INFO("Drive: the demonstration %s: %zu steps recorded (%d ms); wear %d, fuel %.1f of %.1f l, race %s", demo_run_->label.c_str(), demo_run_->steps(), demo_run_->recorded_ms(),
                 int(world_->wears_tires()), double(world_->fuel(0)), double(world_->tank(0)), setup_.race.c_str());
    }
    bool demonstrating() const { return demo_run_.has_value(); }
    bool demo_over() const { return demo_run_ && demo_run_->over(demo_step_); }
    // A recorded B-spec race's commands (DemoRun::mail).
    void demo_commands(const std::array<u8, 6>& m) {
        if (m[4] == 4) bspec_pace_ = std::clamp(int(m[5]) + 1, 1, 5), world_->command(0, bspec_pace_, overtake_steps_ > 0);
        else if (m[4] == 5) overtake_steps_ = m[5] ? 1 : 0, world_->command(0, bspec_pace_, overtake_steps_ > 0);
        else if (m[4] == 6) world_->pit_command(0, m[5] != 0);
        if (m[0] == 1) world_->pit_choose(0, m[1], m[2], m[3] != 0);
        else if (m[0] == 2) world_->stop_refuel(0);
    }
    void end_demo() {
        if (!demo_run_) return;
        demo_run_.reset();
        world_->set_entry_overrides({});
    }
    bool over() const { return over_; }
    const race::RaceFlow& flow() const { return flow_; }
    int mileage() const { return world_->mileage(0); } // (a hybrid's)
    int penalty_seconds() const { return world_->penalty_seconds(0); }
    void rev_marks(float& limit, float& shift) const { world_->rev_marks(0, limit, shift); }
    void aid_status(u8& asm_status, u8& tcs_status) const { world_->aid_status(0, asm_status, tcs_status); }
    race::ResultBoard& board() { return board_; }
    float player_distance() const { return distance_; } // metres the player's car ran this run
    float bspec_distance() const { return bspec_distance_; } // those of them the B-spec driver drove
    float lap_length() const { return world_->lap_length(); }
    // The race's words for one lap and several (the results' "+1Lap").
    void set_card(int tire, std::string race_word) { tire_index_ = tire, race_word_ = std::move(race_word); }
    void set_lap_words(std::string one, std::string several) { lap_words_[0] = std::move(one), lap_words_[1] = std::move(several); }
    int beeps() const { return beeps_; }
    std::span<const race::CarState> shown() const { return replaying_ ? std::span<const race::CarState>(history_[replay_at_]) : world_->cars(); }

    // What the race display shows now.
    void fill(HudState& h, const RaceHud& hud) const {
        const std::span<const race::CarState> cars = shown();
        const bool demo = demo_run_.has_value();
        h.replay = replaying_ || demo;
        // B-spec: the meters are the followed car's (Select Car), as the race display shows its target car.
        const size_t shown_car = (watched_ && !replaying_) || viewing() ? std::min(focused(), cars.size() - 1) : 0;
        const race::CarState& me = cars[shown_car];
        const race::CarSpec& spec = world_->visual(shown_car).spec();
        h.speed = me.speed, h.rpm = me.engine_rpm, h.gear = me.gear;
        h.clutch_closed = me.clutch_closed, h.gear_flash = gear_flash_;
        h.stepless = world_->stepless(shown_car);
        h.signs = replaying_ ? std::array<int, 3>{} : world_->signs(shown_car);
        h.throttle = me.throttle, h.brake = me.brake;
        h.fuel_on = world_->counts_fuel() && !world_->course().road().gtrw;
        h.fuel = replaying_ ? 1.0f : world_->fuel_share(shown_car);
        // The tire diagram: only in a race that wears its tires (the display's flag), each wheel in the color of its
        // wear level; B-spec's pit command waiting.
        h.tires_on = !replaying_ && !demo && world_->wears_tires();
        for (int w = 0; w < 4; ++w) h.tire_rgb[w] = h.tires_on ? race::tire_color(world_->tire_level(shown_car, w)) : 0;
        h.pit_command = watched_ && !replaying_ && world_->pit_requested(shown_car);
        h.suggested_gear = replaying_ || watched_ || demo ? -1 : world_->suggested_gear(0);
        h.trip_m = replaying_ ? 0 : distance_;
        h.lateral_g = replaying_ ? 0 : lateral_g_;
        h.red_line = spec.red_line, h.rev_limit = spec.rev_limit;
        // The speedometer's face by the car's top speed as the program estimates it (P +0xFD8).
        if (const float top = world_->car_top_speed(shown_car); top > 0) h.top_speed = top;
        h.driver_view = !viewing() && !race::DriveCamera::chase(mount_) && mount_ != kTvView; // (a replay's meters are the small panel)
        h.mode_text.clear(), h.mode_opacity = 0;
        if (viewing() && mode_time_ >= 0) {
            // The playback mode's plate (0x39F018): in over 0.25 s, 3 s, out over 0.25 s.
            const float t = mode_time_;
            h.mode_opacity = std::clamp(std::min(t / 0.25f, (3.5f - t) / 0.25f), 0.0f, 1.0f);
            h.mode_text = hud.word(mode_word_);
        }
        h.cars.clear();
        for (const race::CarState& c : cars) h.cars.push_back({c.position.x, c.position.z});
        h.letters.clear();
        if (net_) // (each car's entry: its place on the grid)
            for (size_t i = 0; i < cars.size() && i < net_->seats().size(); ++i) h.letters.push_back(int(net_->seats()[i]));
        h.place = flow_.place(0);
        h.lap = flow_.lap_shown(0), h.laps = setup_.laps;
        // (no lap count on a road section and on a course without a lap, 0x34C470 and COURSE +0x12, nor in a test that counts cones)
        {
            const float start = setup_.conductor != race::Conductor::Other && world_->simulated() ? world_->start_race().section_start : 0;
            h.no_lap = (start >= 1 && start <= world_->lap_length() - 1) || flow_.setup().no_lap || (setup_.conductor != race::Conductor::Other && setup_.rules.pylons > 0);
        }
        h.lap_ms = replaying_ ? s64(replay_at_) * 1000 / 60 : flow_.lap_ms(0);
        h.best_ms = flow_.car(0).best_lap_ms, h.last_ms = flow_.car(0).last_lap_ms;
        h.time = double(flow_.time_ms()) / 1000;
        // B-spec: the commands' bar and the race's time (display mode 1).
        h.bspec = watched_ && !replaying_ && !menu_scene_;
        h.pace = bspec_pace_, h.overtake = overtake_steps_ > 0;
        h.total_ms = flow_.time_ms();
        h.help = bspec_help_;
        // A window of a line or message: full for `hold` seconds, then fading out.
        auto window = [](int steps, float hold, float fade) {
            const float t = float(steps) / 60;
            return t < 0 ? 0.0f : t < hold ? 1.0f : std::max(0.0f, 1 - (t - hold) / fade);
        };
        h.message.clear(), h.message_opacity = 0;
        // A replay's opening card (RaceDisplay::vf30 0x39BEC8): 0.1 s in, over 1 s each way, gone at 6.1 s; its display after it
        // over 0.5 s. The picture under it: black at first (✔ the original's replays: until about 2.1 s, there by 2.9; its
        // fader was not read).
        h.card_opacity = h.replay_cover = 0, h.replay_shown = h.mini_logo = 1;
        if ((replaying_ || demo) && view_time_ < 20) {
            const float t = view_time_;
            h.card_opacity = std::clamp(std::min(t - 0.1f, 6.1f - t), 0.0f, 1.0f);
            h.replay_cover = std::clamp((2.9f - t) / 0.8f, 0.0f, 1.0f);
            h.replay_shown = std::clamp((t - 6.1f) / 0.5f, 0.0f, 1.0f);
            h.mini_logo = replaying_ ? std::clamp(t - 18.0f, 0.0f, 1.0f) : h.replay_shown;
            h.card_title = world_->car_name(0);
            char power[32] = "--";
            const float ps = world_->player_power_ps();
            if (ps > 0) std::snprintf(power, sizeof power, "%d%s", int(ps * (hud.mph() ? 0.98632f : 1.0f)), hud.mph() ? "HP" : "PS");
            char tire[32];
            // (the tires on the car, front and rear)
            auto tire_word = [&](int axle) {
                const int kind = world_->fitted_tire(0, axle);
                std::snprintf(tire, sizeof tire, "TireShort_%02d", std::clamp(kind >= 0 ? kind : tire_index_, 0, 12));
                return hud.word(tire);
            };
            const std::string t1 = tire_word(0), t2 = tire_word(1);
            std::string line = hud.word("RaceInfoFormat");
            // (The race's name: a race word, or "=<text>" written as it is: a license test's title.)
            const std::string name = race_word_.rfind('=', 0) == 0 ? race_word_.substr(1) : hud.word(race_word_);
            const std::string parts[3] = {power, t1 + "/" + t2, name};
            for (const std::string& part : parts)
                if (const size_t at = line.find("%s"); at != std::string::npos) line.replace(at, 2, part);
            h.card_line = line;
        }
        // The course's logo with the card; the music's caption after it (❓ from 4.5 s to 14 s, judged
        // from the original's replay; the program's times are not read).
        h.logo_opacity = replaying_ || demo ? h.card_opacity : 0;
        h.caption_opacity = 0;
        if (replaying_) {
            // (✔ the original's replay: from 7.2 s to 17.6, the small logo after it.)
            h.caption_opacity = std::clamp(std::min(view_time_ - 7.2f, 17.6f - view_time_) / 0.25f, 0.0f, 1.0f);
        }
        if (replaying_ || demo) {
        } else if (flow_.player_finished()) {
            // FINISH: 3.7 s and a fade of 0.3 s.
            h.message = hud.word("RaceFinish");
            h.message_opacity = window(flow_.steps_since_player_finish(), 3.7f, 0.3f);
        } else if (const int left = flow_.hold_left(); flow_.holding() && left > 1) {
            // The count (0x39D390): each whole second n left of the hold is written when n x 60 + 1
            // steps are left, "%d" in the middle, shown 0.8 s with a fade of 0.15 s; 4 and more only
            // while the display shows in full (not under the opening shots).
            const int n = (left + 58) / 60, at = n * 60 + 1;
            if (n > 0 && at <= flow_.hold_total() && (n < 4 || !start_camera_.active())) {
                h.message = std::to_string(n);
                h.message_opacity = window(at - left, 0.8f, n < 4 ? 0.15f : 0.0f);
            }
        } else if (const int s = flow_.steps_since_start(); s >= 0 && flow_.setup().start != race::StartType::None) {
            // START at the start signal: 1.0 s and a fade of 0.15 s.
            h.message = hud.word("RaceStart");
            h.message_opacity = window(s, 1.0f, 0.15f);
        }
        // The A-spec points: offered as the race begins (RaceDisplay::vf43: a moving start's 0.2 s in for 2 s; a standing one's
        // 0.5 s in for the hold less 3.5 s, 2 .. 5 s), taken at the finish (vf44: 1 s in for 2.7 s).
        h.points_opacity = 0;
        if (aspec_race() && !replaying_ && !demo && !menu_scene_ && !watched_) {
            const bool finished = flow_.player_finished(), moving = flow_.hold_total() <= 0;
            const float since = finished ? float(flow_.steps_since_player_finish()) / 60 - 1 : float(shown_steps_) / 60 - (moving ? 0.2f : 0.5f);
            const float length = finished ? 2.7f : moving ? 2.0f : std::clamp(float(flow_.hold_total()) / 60 - 3.5f, 2.0f, 5.0f);
            const float in = finished || moving ? 0.1f : 0.5f, out = finished || moving ? 0.3f : 1.0f;
            if (since > 0 && since < length + out) {
                h.points_opacity = std::min(since / in, since < length ? 1.0f : 1 - (since - length) / out);
                h.points_opacity = std::clamp(h.points_opacity, 0.0f, 1.0f);
                h.points_title = hud.word(finished ? "AspecPointGet" : "AspecRaceFormat1");
                h.points_line = hud.word(finished ? "AspecPointFormat" : "AspecRaceFormat2");
                if (const size_t at = h.points_line.find("%d"); at != std::string::npos)
                    h.points_line.replace(at, 2, std::to_string(finished ? aspec_taken() : world_->aspec_points(setup_.sequence == "mission")));
            }
        }
        // The rolling start's status line (the race words AutoDriveFullThrottle, AutoDriveReady).
        h.status.clear(), h.status_opacity = 0, h.status_red = false;
        if (handing_over() && !watched_ && !setup_.auto_drive) {
            h.status = hud.word(throttle_held_ ? "AutoDriveReady" : "AutoDriveFullThrottle");
            h.status_opacity = 1;
        }
        // A penalty's seconds left (RaceDisplay::vf27).
        if (const int left = replaying_ || demo ? 0 : world_->penalty_seconds(shown_car); left > 0) {
            h.status = hud.word("Penalty"), h.status_opacity = 1, h.status_red = true;
            if (const size_t at = h.status.find("%d"); at != std::string::npos) h.status.replace(at, 2, std::to_string(left));
        }
        // The pit messages (RaceDisplay::vf28: F +0x5FA's codes 7 .. 12 of the car shown).
        if (static constexpr const char* kPit[6] = {"PitTooFast", "PitLastLap", "PitNoTireWear", "PitSpeedLimit", "PitWork", "PitGetOut"}; !replaying_ && !demo && h.status.empty())
            if (const int code = world_->pit_message(shown_car); code >= 7 && code <= 12) {
                h.status = hud.word(kPit[code - 7]);
                h.status_opacity = 1;
            }
        h.event.clear(), h.event_opacity = 0, h.event_gap.clear(), h.event_gap_opacity = 0;
        if (!replaying_ && split_steps_ >= 0 && split_steps_ < 200 && (event_steps_ < 0 || event_steps_ >= 200)) {
            // "T1   0'27.247" for three seconds; the gap to the first car through that split this lap under it.
            h.event = (gt3_road_ ? std::string() : hud.word("WordOfSectionTime") + std::to_string(split_index_ + 1) + "   ") + race::format_lap_time(split_ms_); // (Prologue: the time alone, 0x38CB50)
            h.event_opacity = std::min(1.0f, float(split_steps_) / 9.0f) * window(split_steps_, 3.0f, 0.15f);
            if (split_gap_ms_ > 0) {
                h.event_gap = "+" + race::format_lap_time(split_gap_ms_);
                h.event_gap_opacity = h.event_opacity * window(split_steps_, 2.85f, 0.15f);
            }
        }
        if (!replaying_ && event_steps_ >= 0 && event_steps_ < 200) {
            // "L1/3   1'23.456" for three seconds.
            h.event = (gt3_road_ ? std::string() : hud.word("WordOfLapTime") + std::to_string(event_lap_) + (setup_.laps > 0 ? "/" + std::to_string(setup_.laps) : std::string()) + "   ") + race::format_lap_time(flow_.car(0).last_lap_ms);
            h.event_opacity = std::min(1.0f, float(event_steps_) / 9.0f) * window(event_steps_, 3.0f, 0.15f);
        }
    }

    // B-spec's commands: the pace (-1 / +1, 1 .. 5), the overtake (on until it is asked off again: the
    // program's command handler 0x3F19E0 has no timer of its own), the display shown or hidden. They go to the computer drivers of the
    // player's car (RaceWorld::command).
    bool watched() const { return watched_; }
    bool bspec_overtaking() const { return overtake_steps_ > 0; }
    void bspec_pace(int step) { mail_.command = {4, u8(std::clamp(bspec_pace_ + step, 1, 5) - 1)}; }
    void bspec_overtake() { mail_.command = {5, u8(overtake_steps_ > 0 ? 0 : 1)}; }
    void pit_choose(u8 front, u8 rear, bool fuel) { mail_.pit = {1, front, rear, u8(fuel)}; }
    void stop_refuel() { mail_.pit = {2, 0xFF, 0xFF, 0}; }
    void bspec_display() { bspec_help_ = !bspec_help_; }

    // The next view: the car's three, or with ours.
    void next_view() {
        // B-spec's Cross: the course's cameras or the car's own, its nose (0x374618).
        if (watched_) {
            mount_ = mount_ == kTvView ? race::DriveCamera::kInCar : kTvView;
            view_cut_ = true, replay_camera_.reset();
            return;
        }
        using race::DriveCamera;
        std::vector<int> views(std::begin(DriveCamera::kViews), std::end(DriveCamera::kViews));
        if (all_views_) {
            views = {DriveCamera::kInCar, DriveCamera::kInCarNoMirror, DriveCamera::kBumper, DriveCamera::kChase, DriveCamera::kFarChase, DriveCamera::kRoof};
            if (!replay_script_.records.empty()) views.push_back(kTvView);
        }
        size_t at = views.size() - 1;
        for (size_t i = 0; i < views.size(); ++i)
            if (views[i] == mount_) at = i;
        mount_ = views[(at + 1) % views.size()];
        if (mount_ == kTvView) replay_camera_.reset();
        view_cut_ = true;
    }
    // [cameras] race_views: the game's three views, or ours as well; in_car_shake.
    void set_view_options(bool all, bool shake) { all_views_ = all, camera_.set_shake(shake); }
    // Looking aside: the controller's left and right views, and a free right stick.
    void set_look(bool left, bool right, float x, float y) { camera_.set_sideways(left, right), camera_.set_stick(x, y); }

    // Where the shown frame is between the last two steps (0 .. 1), for the cars and the camera.
    void set_blend(float t) { blend_ = std::clamp(t, 0.0f, 1.0f); }

    // The course's TV cameras (a license test's results page shows the car from the trackside ❓ which
    // of the program's cameras is not read).
    void set_effect_settings(const race::ReplayEffectSettings& s) { effect_settings_ = s; }
    void tv_view() {
        if (replay_script_.records.empty() || mount_ == kTvView) return;
        before_tv_ = mount_;
        mount_ = kTvView;
    }

    void render(Ui& ui, const SDL_FRect& rect, bool look_back) {
        std::span<const race::CarState> cars = shown();
        const bool blend = blend_ < 1 && (!menu_scene_ || preview_) && prev_cars_.size() == cars.size() && !cars.empty();
        if (blend) {
            // A frame between two steps: the cars and the camera between their two places.
            blended_.resize(cars.size());
            for (size_t i = 0; i < cars.size(); ++i) blended_[i] = race::blend_states(prev_cars_[i], cars[i], blend_);
            cars = blended_;
        }
        // The racing views' screen settings; the replays' and the B-spec race's their own.
        const bool dive = diving() && tv_valid_;
        const auto grade = picture_settings(replaying_ || watched_).grade();
        world_->set_grade(dive ? dive_.grade(grade) : grade);
        world_->set_trail(dive && effect_settings_.trail ? std::min(dive_.trail(), 0.8f) : 0.0f);
        race::ViewPose pose = current_pose(shown(), look_back);
        // (A cut is shown at once: two shots of one trackside camera stand too close together for blend_poses to tell.)
        if (blend && !view_cut_ && start_prev_.active() == start_camera_.active()) pose = race::blend_poses(previous_pose(look_back), pose, blend_);
        // A cut's dissolve: the last picture frozen at the cut, over the new one as it fades.
        world_->set_dissolve(snapshot_, dissolve_total_ > 0 ? float(dissolve_left_) / float(dissolve_total_) : 0.0f, replaying_ || menu_scene_ || mount_ == kTvView || start_camera_.active() || dissolve_total_ > 0);
        snapshot_ = false;
        world_->set_cars_hidden(menu_scene_ && guide_ == 1);
        // The passes of the views the player does not drive in (the race's play mode 1 .. 4).
        race::EffectView fx;
        fx.replay = replaying_ || watched_ || demo_run_ || menu_scene_;
        world_->set_effects(fx, effect_settings_);
        world_->render(ui, rect, cars, pose, focus(), pose.body_hidden ? int(focus()) : -1);
        if (net_) label_cars(cars, pose, rect);
        // The in-car view's rear-view mirror at the top (❓ its place, (190, 18) 259 x 63 of the
        // 640 x 480 picture, measured on the original's in-car view): not in replays, demonstrations, under the results pages, the opening
        // shots (a standing start's or a rolling start's TV cameras), B-spec or looking back.
        race::ViewPose pit;
        if (mount_ == race::DriveCamera::kInCar && !camera_.aside() && !world_->pit_camera(focus(), pit) && !finish_view() && !replaying_ && !demo_run_ && !flow_.player_finished() && !menu_scene_ && !watched_ && !start_camera_.active() && !opening_tv() && !look_back && !setup_.look_back) {
            const SDL_FRect c = RaceHud::canvas();
            const float k = c.h / 480.0f;
            world_->render_mirror(ui, {c.x + 190 * k, c.y + 18 * k, 259 * k, 63 * k}, cars, pose, 0);
        }
    }

    // Where each car is in the picture (an online race's names): a point over its roof.
    void label_cars(std::span<const race::CarState> cars, const race::ViewPose& pose, const SDL_FRect& rect) {
        labels_.clear();
        const float ty = std::tan(pose.fovy() * 0.5f), tx = ty * rect.w / std::max(rect.h, 1.0f);
        for (size_t i = 0; i < cars.size(); ++i) {
            OnlineRaceUi::Label l;
            l.car = i;
            const RVec v = pose.view.transform_point({cars[i].position.x, cars[i].position.y + 1.6f, cars[i].position.z});
            l.distance = -v.z;
            if (v.z < -1.0f && std::fabs(v.x) < -v.z * tx * 1.05f && std::fabs(v.y) < -v.z * ty) {
                l.x = rect.x + rect.w * 0.5f * (1 + v.x / (-v.z * tx)), l.y = rect.y + rect.h * 0.5f * (1 - v.y / (-v.z * ty));
                l.seen = true;
            }
            labels_.push_back(l);
        }
    }
    const std::vector<OnlineRaceUi::Label>& labels() const { return labels_; }

    // A license test's or mission's end by its rules (DynamicsConductorLicense::vf15 -> the judge 0x3F8D70,
    // docs/formats/race_flow.md "The license tests' rules"), each step from the start signal until the
    // player's car's run ends:
    // - a test of kind 1 ends when all four wheels are in the stopping box (triangles flagged bit 2, the
    //   wheel ground record's +0x33 bit 2 that 0x3F91F0 reads) and the car is slower than 0.2778 m/s (1 km/h), timed that
    //   share of a step in;
    // - it fails (no time) when every wheel on the ground is on a material off the course (but on the
    //   loose courses) or on the pit lane, on a wall hit or car hit over the test's limits, on a cone
    //   touched where the test counts none, or (kind 6) when the car leads the race;
    // - a test that counts cones ends, timed, when that many are knocked down.
    void judge_license() {
        if (flow_.hold_left() > 0 || flow_.player_finished()) return;
        if (setup_.conductor == race::Conductor::Mission) return; // (its virtuals 46 and 47 answer 0 and its hits are a race's, 0x35FCC8: nothing fails it)
        const race::CarState& c = world_->cars()[0];
        const race::LicenseRules& r = judge_.rules();
        if (gt3_road_ && flow_.steps_since_start() < 0) return;
        if (const std::optional<float> share = (r.kind == 1 || !r.any()) && stop_area_ ? race::stopped_in_box(c) : std::nullopt) {
            flow_.finish_now(0, *share);
            license_failed_ = false;
            return;
        }
        // Prologue's courses (Gran Turismo 3's roads): the judge's own rules (its DynamicsConductorLicense is Gran Turismo 4's,
        // 0x3C1318 = 0x3F8D70, with its own rows: LicenseRules from them), its own surfaces and pit zone (license_prologue.h).
        if (gt3_road_) {
            // (the judge runs within the race's step: the cones it counts and the car's place are the last step's)
            const race::LicenseStep s = race::prologue::license_step(c, pit_zone_, setup_.course_kind, world_->car_mass(0), cones_met_);
            cones_met_ = world_->gadget_contacts(0);
            if (judge_.step(s) != race::LicenseFail::None) {
                LOG_INFO("Drive: the test failed (%s) at %.3f s",
                         judge_.fail() == race::LicenseFail::OffCourse ? "off the course" : judge_.fail() == race::LicenseFail::Hit ? "hit" : "judge",
                         double(flow_.time_ms()) / 1000);
                flow_.finish_now(0);
                license_failed_ = true;
                return;
            }
            if (judge_.cones_done()) flow_.finish_now(0, 1); // (the clock + 50)
            // (0x3C1318's end: a lead held for the rules' seconds ends the run)
            judge_.count_time(graded_place_ == 1);
            if (judge_.seconds_left() == 0) flow_.finish_now(0, 1);
            return;
        }
        const race::LicenseStep s = race::license_step(c, [this](u8 m) { return world_->soft_material(m); }, setup_.course_kind, world_->car_mass(0), cones_met_);
        cones_met_ = world_->gadget_contacts(0); // (the cones move after the race's step: what they met is judged in the next)
        // A cone's contact is heard (kind 2, clash4) only while the test counts cones knocked down (the
        // rule block's +0x46 not 0: 0x350328 asks 0x35DF00).
        if (r.pylons > 0 && s.cones > 0)
            if (audio::RaceSound* sound = world_->sound()) sound->contact(0, 2, world_->gadget_impulse(0));
        if (judge_.step(s) != race::LicenseFail::None) {
            LOG_INFO("Drive: the test failed (kind %d: wall %.2f, car %.2f, cones %d, wheels off %d, pit %d) at %.3f s", int(judge_.fail()), double(s.wall_hit),
                     double(s.car_hit), s.cones, s.wheels_off, s.wheels_pit, double(flow_.time_ms()) / 1000);
            flow_.finish_now(0);
            license_failed_ = true;
            return;
        }
        // Kind 6: the car may not lead (its place byte +0x5B3 at 1 fails, 0x3F8F00).
        if (r.kind == 6 && world_->car_count() > 1 && flow_.place(0) == 1) {
            LOG_INFO("Drive: the test failed (kind 6, leading) at %.3f s", double(flow_.time_ms()) / 1000);
            flow_.finish_now(0);
            license_failed_ = true;
            return;
        }
        if (judge_.cones_done()) flow_.finish_now(0, 1); // (at the step's end: the clock + 50)
    }
    const race::LicenseJudge& license_judge() const { return judge_; }
    // A series' last race has its own jingles.
    void set_last_race(bool last) { last_race_ = last; }
    // A license test's result, once: its jingle.
    void license_sound(int grade) {
        if (std::exchange(license_sounded_, true)) return;
        if (audio::RaceSound* sound = world_->sound()) sound->license_result(grade);
    }
    bool license_failed() const { return license_failed_; }
    int graded_place() const { return graded_place_; }

    // A license test on a road whose goal distance (Gran Turismo 3's road header +0x1C, Prologue's courses) lies inside the
    // lap and that has no stopping box: the test ends where the lap's progress crosses it after the line (the line counter
    // at least 1), the time taken that share of the step (Gran Turismo 3's section test; ❓ Prologue's own judge not read:
    // its test 2 on L_fisco6, 203 m from the line, has the gold time 0'12.180).
    void judge_goal() {
        if (goal_v_ <= 0 || stop_area_ || flow_.steps_since_start() < 0 || flow_.player_finished()) return;
        const float v = world_->cars()[0].course_v;
        // (on the test's last lap: lesson 1 runs two, RACE +0x29)
        const int last = std::max(1, setup_.laps);
        if (flow_.car(0).counter >= last && goal_prev_ >= 0 && goal_prev_ < goal_v_ && goal_v_ <= v && v - goal_prev_ < 100)
        {
            flow_.finish_now(0, (goal_v_ - goal_prev_) / std::max(v - goal_prev_, 1e-4f));
            LOG_INFO("Drive: the license test's goal at %.1f m: %.3f s", double(goal_v_), double(flow_.car(0).finish_units) / 3000.0);
        }
        goal_prev_ = flow_.car(0).counter >= 1 ? v : -1;
    }

    // The wrong-way sign (0x3C0878, 0x3C0038): up while the player's car goes back along the course over 5 km/h, touching nothing;
    // down when it goes on, or under 3 km/h.
    void step_wrong_way(bool driven) {
        const race::CarState& c = world_->cars()[0];
        const float half = world_->lap_length() * 0.5f, was = wrong_prev_, now = c.course_v;
        wrong_prev_ = now;
        if (was < 0 || flow_.holding()) return;
        const bool back = was <= now ? now - was > half : was - now < half;
        const float v = std::sqrt(c.velocity.x * c.velocity.x + c.velocity.y * c.velocity.y + c.velocity.z * c.velocity.z);
        if (!back || v < 0.8333333f) wrong_way_ = false;
        else if (v > 1.3888888f && c.contact_kind == 0 && !driven && !flow_.player_finished() && !handing_over()) wrong_way_ = true;
    }
    bool wrong_way() const { return wrong_way_; }

    // Before a step: the cars and the cameras as they are, for the frames shown before the next.
    void keep_previous() {
        const std::span<const race::CarState> now = shown();
        prev_cars_.assign(now.begin(), now.end());
        camera_prev_ = camera_, start_prev_ = start_camera_;
        tv_prev_ = tv_pose_, tv_prev_valid_ = tv_valid_;
        view_cut_ = false;
    }
    // The camera at the last step but one (its cameras as they were then, on the cars as they were).
    race::ViewPose previous_pose(bool look_back) {
        std::swap(camera_, camera_prev_), std::swap(start_camera_, start_prev_), std::swap(tv_pose_, tv_prev_), std::swap(tv_valid_, tv_prev_valid_);
        before_ = true;
        const race::ViewPose p = current_pose(prev_cars_, look_back);
        before_ = false;
        std::swap(camera_, camera_prev_), std::swap(start_camera_, start_prev_), std::swap(tv_pose_, tv_prev_), std::swap(tv_valid_, tv_prev_valid_);
        return p;
    }

    // The player's car's metres to the line before its first crossing (for the log).
    float to_line_raw() const {
        const race::CarState& c = world_->cars()[0];
        const float len = world_->lap_length();
        return len > 0 && c.course_v > len * 0.5f ? len - c.course_v : 0.0f;
    }
    bool opening_was_ = false;
    // A rolling start: the player's car's metres to the line before it takes it (-1: none, or past it).
    float to_line() const {
        if (!handing_over() || setup_.skip > 0) return -1;
        const race::CarState& c = world_->cars()[0];
        const float len = world_->lap_length();
        if (!(len > 0)) return -1;
        return c.course_v > len * 0.5f ? len - c.course_v : (c.lap < 0 ? len - c.course_v : 0.0f);
    }
    // A rolling start's opening: the course's TV cameras show the field while the followed car is held to the
    // start's pace, more than 3 s at the start's speed short of the line (the camera manager's state 4 from
    // 0x3FD620, which asks the car's pace 0x3F7880); then the race's own view. ✔ The original's Tsukuba start:
    // the player's view about 1.5 s after the clock starts and 4.2 s before he takes the line.
    bool opening_tv() const {
        if (handing_over() && setup_.skip <= 0)
            if (const std::optional<bool> held = world_->held_to_pace(0)) return *held;
        return setup_.start_speed > 0 && setup_.start_kind != 3 && to_line() > 3 * setup_.start_speed;
    }
    // A race of A-spec points (0x39C4C8: the single race, the missions, Gran Turismo mode's events, with other cars), and the
    // points taken in it (0x3C1220): the race's when the player leads at its end and the B-spec driver drove none of it.
    bool aspec_race() const {
        return world_->car_count() > 1 && (setup_.sequence == "single_race" || setup_.sequence == "mission" || setup_.sequence == "solo_event" || setup_.sequence == "championship");
    }
    int aspec_taken() const {
        return aspec_race() && flow_.player_finished() && flow_.place(0) == 1 && bspec_distance_ <= 0 ? world_->aspec_points(setup_.sequence == "mission") : 0;
    }

    // A rolling start before the player's car takes the line: it is driven for him.
    bool handing_over() const {
        if (replaying_ || menu_scene_ || flow_.steps_since_start() >= 0) return false;
        if (world_->simulated() && world_->computer_drivers() && !world_->course().road().gtrw) return world_->rolling(0);
        return flow_.setup().start == race::StartType::Rolling;
    }

    // The replay cameras' shot this step (once a step: their dampers and springs move a step at a
    // time); a cut with a dissolve starts it.
    void step_tv(const race::CarState& car, size_t index = 0) {
        const render::Mat4 body = world_->visual(index).body_matrix(car);
        tv_valid_ = replay_camera_.pose(replay_script_, car, body, tv_pose_, [&](const render::Mat4& view) { return world_->nearest_depth(index, car, view); });
        view_cut_ = view_cut_ || replay_camera_.cut() || tv_valid_ != tv_prev_valid_;
        if (const float d = replay_camera_.cut_dissolve(); d > 0) begin_dissolve(int(std::lround(d * 60)));
        step_dissolve();
        // GT4R_LENS_LOG=<n>: the replay camera's place and lens every n steps.
        if (const char* every = SDL_getenv("GT4R_LENS_LOG"); every && std::atoi(every) > 0 && tv_valid_ && ++lens_steps_ % size_t(std::atoi(every)) == 0)
            LOG_INFO("Drive: lens at d %.1f: %.2f degrees, eye (%.1f, %.1f, %.1f), car (%.1f, %.1f), focus %.2f f/%.1f%s%s, record %.1f", double(car.course_v), double(tv_pose_.fov) * 57.29578, double(tv_pose_.eye.x),
                     double(tv_pose_.eye.y), double(tv_pose_.eye.z), double(car.position.x), double(car.position.z), double(tv_pose_.focus), double(tv_pose_.f_number),
                     tv_pose_.depth_of_field ? " dof" : "", tv_pose_.haze ? " haze" : "", replay_camera_.record() ? double(replay_camera_.record()->start) : -1.0);
    }
    void begin_dissolve(int steps) {
        if (steps <= 0) return;
        snapshot_ = true;
        dissolve_total_ = dissolve_left_ = steps;
    }
    void step_dissolve() {
        if (dissolve_left_ > 0 && !snapshot_) --dissolve_left_;
        if (dissolve_left_ == 0) dissolve_total_ = 0;
    }

    // Prologue's license test ended: the camera circles the car (race/start_camera_prologue.h).
    bool finish_view() const { return gt3_road_ && finish_camera_.distance > 0 && is_license(setup_.sequence) && flow_.player_finished() && !replaying_ && !menu_scene_; }

    // The camera now: the opening shots, the replay cameras, or the car's view.
    race::ViewPose current_pose(std::span<const race::CarState> cars, bool look_back) {
        const size_t f = focus() < cars.size() ? focus() : 0;
        const race::CarState& me = cars[f];
        const render::Mat4 body = world_->visual(f).body_matrix(me);
        race::ViewPose pose;
        const int view = viewing() && view_.ride_on() ? view_.mount()
                         : replaying_ || demo_run_ || (menu_scene_ && preview_ && guide_ != 1) || (!menu_scene_ && opening_tv()) ? kTvView
                                                                                                                                : mount_;
        bool posed = menu_scene_ && (!preview_ || guide_ == 1) && scenic_.pose(pose);
        // (A scenic shot too is focused on the screen's car, behind it or not: 0x37C800.)
        if (posed && pose.depth_of_field) pose.focus = std::max(std::clamp(world_->nearest_depth(f, me, pose.view) + 0.7f, 0.001f, 100000.0f), race::least_focus(pose.fov * 57.29578f));
        // (Cross leaves a replay's start shots for the car's own cameras: 0x374630.)
        posed = posed || (start_camera_.active() && !(viewing() && view_.ride_on()) && start_camera_.pose(pose, &body));
        // The replay cameras' shot for the car; the chase view where the script has none.
        if (!posed && finish_view()) posed = finish_camera_.pose(RVec{me.position.x, me.position.y, me.position.z}, pose);
        if (!posed && view == kTvView && tv_valid_) pose = tv_pose_, posed = true;
        // The pit's own camera from the car's nearing its box until it has left it.
        if (!posed && !replaying_ && !menu_scene_) posed = world_->pit_camera(f, pose, before_);
        // Prologue's shots: a lens of A degrees shows 10 / 7 of the width ours does (✔ against the original's start, grid page and
        // trackside shots at Tsukuba: 1.43; 4/3 x 480/448, the angle up its 448 lines).
        if (posed && gt3_road_) pose.fov = 2 * std::atan(std::tan(pose.fov * 0.5f) * 10 / 7);
        // Gran Turismo 4's lenses are the program's angles.
        if (posed && !gt3_road_) pose.fov = race::shown_angle(pose.fov);
        if (!posed) {
            int mount = view == kTvView ? race::DriveCamera::kChase : view;
            const std::vector<race::CameraMount>& mounts = world_->visual(f).mounts();
            const bool own = viewing() && view_.ride_on();
            if (own && mount != race::DriveCamera::kChase && (size_t(mount) >= mounts.size() || mounts[size_t(mount)].flags == 0)) mount = race::DriveCamera::kInCar; // (a place the car does not have)
            pose = camera_.pose(me, body, mounts, mount, own ? view_.look_back() : look_back || setup_.look_back);
            if (!race::DriveCamera::chase(mount) && !race::DriveCamera::in_car(mount) && !gt3_road_) pose.fov = race::shown_angle(pose.fov);
        }
        return pose;
    }

    // A car's gap behind the leader now (ms): the time since the leader was where it is (❓ the program's
    // gap is not read); 0 for the leader, -1 before the start.
    s64 gap_ms(size_t car) const {
        const std::vector<size_t>& order = flow_.order();
        if (order.empty() || car >= run_dist_.size() || run_dist_[car].empty()) return -1;
        const std::vector<float>& lead = run_dist_[order[0]];
        const float d = run_dist_[car].back();
        const auto it = std::lower_bound(lead.begin(), lead.end(), d);
        const s64 steps = s64(lead.size()) - s64(it - lead.begin());
        return std::max<s64>(0, steps) * 1000 / 60;
    }
    int bspec_pace() const { return bspec_pace_; }
    int grid_place(size_t car) const { return car < world_->car_count() ? world_->setup().entries[car].grid_place : 0; }
    size_t focused() const { return focus(); }

    // B-spec's pit command (Circle: 0x3F19E0's command 6) for the player's car.
    void bspec_pit() {
        // Not before the car's first crossing (F +0x4AC) nor on its last lap (0x343C20).
        if (!watched_ || flow_.car(0).counter <= 0 || (setup_.laps > 0 && flow_.car(0).counter >= setup_.laps)) return;
        mail_.command = {6, u8(!world_->pit_requested(0))};
        LOG_INFO("Drive: B-spec pit command at %.1f s", double(flow_.time_ms()) / 1000);
    }
    int pit_message() const { return world_->pit_message(0); }
    // The PIT STOP menu's driving mode: the player's car from here on by the pad or by the B-spec driver. The display's
    // mode follows; the pace stays as it was (0x3F19E0's command 10).
    void set_driving_mode(bool bspec) {
        if (bspec == watched_ || setup_.auto_drive) return;
        watched_ = bspec;
        if (bspec) own_view_ = mount_, mount_ = kTvView;
        else if (own_view_) mount_ = *own_view_, own_view_.reset();
        view_cut_ = true, replay_camera_.reset();
        follow(0);
        recording_ = false; // (❓ the recording of a run whose driver changed, the record's command 10, is not made)
        world_->set_driving_mode(bspec);
        LOG_INFO("Drive: the driving mode at %.1f s: %s", double(flow_.time_ms()) / 1000, bspec ? "B-spec" : "A-spec");
    }
    RaceWorld& world() { return *world_; }
    std::array<sim::ai::PitStops::StopRecord, 4> pit_record(size_t car) const { return world_->pit_record(car); }
    std::optional<std::array<u8, 0x40>> bspec_record() const { return world_->bspec_record(0); }
    int bspec_class() const { return world_->bspec_class(0); }

    // B-spec's Select Car (Up / Down): the car the views follow, by race order (the player's car first:
    // 0). ❓ The program's choice by the order is not read: here the next or last car in race order.
    size_t focus() const { return viewing() ? (replay_focus_ < world_->car_count() ? replay_focus_ : 0) : watched_ && !replaying_ && !menu_scene_ && focus_ < world_->car_count() ? focus_ : 0; }
    void select_car(int dir) {
        const std::vector<size_t>& order = flow_.order();
        if (order.empty()) return;
        size_t at = 0;
        for (size_t k = 0; k < order.size(); ++k)
            if (order[k] == focus_) at = k;
        at = (at + order.size() + size_t(dir > 0 ? 1 : order.size() - 1)) % order.size();
        follow(order[at]);
    }
    // The views follow another car: they start on it, with nothing of the last car's motion.
    void follow(size_t car) {
        if (car == focus_) return;
        focus_ = car;
        camera_.reset(world_->cars()[car]);
        replay_camera_.reset();
        prev_cars_.clear(); // (no frame between the two cars' views)
    }

    void quiet() { world_->quiet(); }
    void draw_icons(Ui& ui, const SDL_FRect& canvas, std::span<const RaceWorld::Icon> icons) { world_->draw_icons(ui, canvas, icons); }
    void draw_model(Ui& ui, const SDL_FRect& rect, const render::GpuModel& m, const render::Mat4& place, const render::Mat4& proj, float alpha, bool own_reflection = false) {
        world_->draw_model(ui, rect, m, place, proj, alpha, own_reflection);
    }
    audio::RaceSound* sound() { return world_->sound(); }

    const race::CarState& player() const { return shown()[0]; }
    const std::string& car_name(size_t i) const { return world_->car_name(i); }
    // (the results' rows: an online race's the players')
    const std::string& car_short_name(size_t i) const { return i < player_names_.size() ? player_names_[i] : world_->car_short_name(i); }
    void set_player_names(std::vector<std::string> names) { player_names_ = std::move(names); }
    const WorldEntry& entry(size_t i) const { return world_->setup().entries[i]; }
    size_t car_count() const { return world_->car_count(); }
    const race::CourseScene& course() const { return world_->course(); }
    int mount() const { return mount_; }
    bool opening() const { return start_camera_.active(); }
    bool menu_scene() const { return menu_scene_; }


private:
    App* app_ = nullptr;
    Setup setup_;
    std::unique_ptr<RaceWorld> world_;
    std::vector<race::CarInput> inputs_;
    Disc* disc_ = nullptr;
    db::SpecDb* db_ = nullptr;
    Bytes saved_recording_; // a saved replay's own (race/race_recording.h)
    std::unique_ptr<race::RecordedRace> behind_;
    sim::rules::PadMail mail_; // what the player asks for the next step (B-spec's commands, the pit menu's choice)
    std::vector<replay::PadRecord> records_; // the run's records, one a step
    bool recording_ = false;
    std::optional<std::array<u8, 0x40>> bspec_start_; // the B-spec driver's record at the run's start
    race::PadRecord player_frame_; // the player's frame of this step (inputs_[0].pad)
    race::DriveCamera camera_;
    race::ReplayScript replay_script_;
    race::ReplayCamera replay_camera_;
    race::ViewPose tv_pose_;
    race::ReplayEffectSettings effect_settings_;
    race::ReplayView view_; // watching a replay
    race::ReplayStyle replay_style_ = race::ReplayStyle::Plain;
    race::ReplayPad view_pad_;
    race::DiveDirector dive_;
    race::DiveClock dive_clock_;
    race::MusicMarkers markers_; // of the piece `markers_for_`, played for music_time_ seconds
    std::string markers_for_;
    float music_time_ = 0, music_before_ = 0;
    bool dive_tried_ = false, dive_loaded_ = false, dive_asked_ = false;
    size_t replay_focus_ = 0;
    std::string mode_word_; // the playback mode's plate, and its seconds shown (-1 none); the seconds watched
    float mode_time_ = -1, view_time_ = 0;
    bool music_off_ = false;
    bool tv_valid_ = false, snapshot_ = false;
    // The last step but one, for frames shown between steps.
    std::vector<race::CarState> prev_cars_, blended_;
    race::DriveCamera camera_prev_;
    race::StartCamera start_prev_;
    race::ViewPose tv_prev_;
    bool tv_prev_valid_ = false;
    bool view_cut_ = false; // the last step cut the replay cameras' view
    bool before_ = false; // the pose asked for is the last step but one's
    float blend_ = 1;
    int dissolve_total_ = 0, dissolve_left_ = 0;
    race::CameraMotion start_motion_, replay_motion_;
    std::array<race::ReplayScript, 2> start_paths_; // Prologue's: the race's and the replay's
    race::PrologueFinishCamera finish_camera_;
    race::StartCamera start_camera_;
    race::RaceFlow flow_;
    net::NetRace* net_ = nullptr; // an online race's other machines
    std::vector<std::string> player_names_;
    std::vector<OnlineRaceUi::Label> labels_; // and where its cars are in the picture
    race::ResultBoard board_;
    float distance_ = 0, bspec_distance_ = 0;
    int shown_steps_ = 0; // since the run began, its hold included
    bool built_ = false, last_race_ = false, license_sounded_ = false; // (the race's music: a series' last race, a test's result heard)
    int tire_index_ = 4;   // the race's tires (the scripts' index: 4 sports medium)
    std::string race_word_ = "ARCADE_SINGLE_RACE"; // the race's name in the race's words
    bool throttle_held_ = false;
    bool stale_ = false; // the cars were set up before the player's last choice
    int gear_shown_ = 1;
    float gear_flash_ = 0;
    bool stop_area_ = false, license_failed_ = false; // a license test's stopping box
    bool wrong_way_ = false;
    float wrong_prev_ = -1;
    bool run_ended_ = false, run_driven_ = false;     // Prologue's test is over (the end given to the car's driver)
    float goal_v_ = -1, goal_prev_ = -1;              // a license test's goal distance (judge_goal)
    race::prologue::PitZone pit_zone_;                // (Prologue's license tests: where the pit lane counts)
    bool gt3_road_ = false;                           // the road is Gran Turismo 3's (Prologue's courses)
    race::LicenseJudge judge_;
    std::vector<std::vector<float>> run_dist_; // per car, per step since the start: its distance from the line
    size_t focus_ = 0; // B-spec: the car the views follow
    std::optional<sim::ai::DemoRun> demo_run_; // a demonstration's recording
    size_t run_steps_ = 0, lens_steps_ = 0;
    bool demo_bspec_ = false;                  // of a B-spec run: its car by its driver
    int cones_met_ = 0;
    int graded_place_ = 0; // (the player's place after the last step of his run: what Prologue's judge and grade read)
    size_t demo_step_ = 0;
    bool demo_logged_ = false;
    int bspec_pace_ = 3, overtake_steps_ = 0;
    bool bspec_help_ = true;
    float lateral_g_ = 0;
    course::Vec3 last_v_;
    std::string lap_words_[2] = {"Lap", "Laps"};
    race::ReplayScript scenic_script_;
    race::ScenicCamera scenic_;
    std::vector<std::vector<race::CarState>> history_;
    bool replaying_ = false, replay_done_ = false, over_ = false, ran_ = false, menu_scene_ = false, watched_ = false;
    bool preview_ = false; // the scene behind the menus is Prologue's run (stand)
    // Prologue's lesson's recorded run (read once), and the one the page plays at its step.
    struct RecordedRun {
        replay::prologue::Entry entry;
        std::vector<race::PadRecord> pads;
    };
    const RecordedRun& recorded_run() {
        if (recorded_) return *recorded_;
        recorded_.emplace();
        if (!is_license(setup_.sequence) || setup_.race.empty() || !app_->vfs() || !app_->disc()) return *recorded_;
        try {
            const std::optional<Bytes> file = app_->vfs()->read(replay::prologue::run_path(setup_.race));
            if (!file) return *recorded_;
            const std::optional<replay::prologue::Run> run = replay::prologue::Run::parse(*file, replay::prologue::delta_bases(sim::Program::load(*app_->disc())));
            if (!run || run->entries.empty() || run->streams.empty()) return *recorded_;
            recorded_->entry = run->entries[0];
            for (const replay::PadRecord& f : run->streams[0].frames) recorded_->pads.push_back({f.bytes});
        } catch (const std::exception& e) {
            LOG_WARN("Drive: the recorded run of %s: %s", setup_.race.c_str(), e.what());
        }
        return *recorded_;
    }
    // The player's car as the recording has it (its gearbox and aids).
    std::vector<std::optional<WorldEntry>> recorded_entry() {
        std::vector<std::optional<WorldEntry>> over(world_->car_count());
        const replay::prologue::Entry& r = recorded_run().entry;
        WorldEntry e = world_->setup().entries[0];
        e.automatic = r.pedal_style != 1, e.pedal_style = r.pedal_style;
        e.asm_on = r.asm_on != 0, e.tcs_on = r.tcs_on != 0;
        over[0] = e;
        return over;
    }
    std::optional<RecordedRun> recorded_;
    const RecordedRun* lesson_pads_ = nullptr; // (GT4R_LESSON_DRIVE)
    size_t lesson_at_ = 0;
    const RecordedRun* preview_pads_ = nullptr;
    race::RaceFlow preview_flow_;
    size_t preview_at_ = 0;
    int guide_ = 0, guide_steps_ = 0; // the guide film: 1 its shots, 2 its run
    bool guide_done_ = false;
    int preview_hold_ = 0;
    size_t replay_at_ = 0;
    int event_steps_ = -1, event_lap_ = 0, beeps_ = 0;
    std::vector<float> splits_, split_prev_;          // the split points (m along the lap); each car's last place
    std::map<std::pair<int, int>, s64> split_first_;  // (crossings, split) -> the race clock when the first car passed
    int split_steps_ = -1, split_index_ = 0;
    s64 split_ms_ = 0, split_gap_ms_ = 0;
    int mount_ = race::DriveCamera::kChase;
    bool all_views_ = false;
    std::optional<int> own_view_; // the player's view before a B-spec run
    std::optional<int> before_tv_; // the view before tv_view
};

class RaceScreen : public Screen {
public:
    explicit RaceScreen(App& app, const RaceStart* chosen = nullptr) : setup_(chosen ? Setup{} : setup_from_env()), app_(app) {
        wheel_ = input::WheelSettings::load(app.config());
        if (chosen) {
            setup_.course = chosen->course, setup_.car = chosen->car;
            setup_.paint = u32(std::max(0, chosen->paint));
            setup_.cars = std::clamp(chosen->cars, 1, 16), setup_.laps = std::clamp(chosen->laps, 1, 99);
            setup_.scripted = true;
            from_menus_ = true;
            // GT4R_AUTODRIVE=1: the computer drives the player's car in every race the menus start (scripted
            // play through Gran Turismo mode, license tests included).
            if (const char* e = SDL_getenv("GT4R_AUTODRIVE"); e && *e == '1') setup_.auto_drive = true;
            menus_ = chosen->menus, sequence_ = chosen->sequence;
            setup_.sequence = chosen->sequence;
            if (chosen->online) {
                const Setup env = setup_from_env(); // (a scripted run: `drive`, `view`, `skip`, `hud`)
                setup_.auto_drive = setup_.auto_drive || env.auto_drive, setup_.view = env.view, setup_.hud = env.hud;
                online_set_up(setup_, chosen->online);
                setup_.options = chosen->options;
                if (!setup_.options.empty()) pictures_from_options(setup_.options);
            }
            if (menus_) setup_.race = menus_->game().race.race;
            if (menus_) {
                const menu::RaceRequest& q = menus_->game().race;
                setup_.options = menus_->game().options;
                pictures_from_options(setup_.options);
                // The garage car's own parts and settings (the sheet in use).
                if (q.garage_slot >= 0) {
                    setup_.player_equipment = menu::race_equipment(menus_->game(), q.garage_slot);
                    setup_.player_condition = menu::race_condition(menus_->game(), q.garage_slot);
                }
                // Prologue's arcade: the tire grip chosen.
                if (sequence_ == "single_race" && app.db()) {
                    const sim::Program program = sim::Program::load(*app.disc());
                    sim::Equipment eq;
                    if (setup_.player_equipment) eq = *setup_.player_equipment;
                    else sim::stock_equipment(*app.db(), q.car, eq);
                    if (!program.known() && sim::prologue::fit_grip_tires(*app.db(), program, q.tire_type, eq)) setup_.player_equipment = eq;
                }
                setup_.bspec_book = q.bspec_book, setup_.bspec_course_marks = q.bspec_course_marks, setup_.bspec_course_known = q.bspec_course_known;
                setup_.playlist = playlist_from(menus_->game().bgm_playlist);
                setup_.playlist_bytes = save::playlist_block(menus_->game().bgm_playlist);
                // The Replay Theater's Play: the saved replay, then the menus again.
                if (sequence_ == "replay") setup_.replay_file = q.replay_file.empty() ? menus_->game().replay_file : q.replay_file;
                // An event's Preview (the sequence "race_view"): the event's field raced by the computer, every car
                // its, seen by the course's cameras; no race menus, the event's page again when the player presses
                // a button (❓ the program's race_view sequence is not read: judged from the event pages' Preview).
                if (sequence_ == "race_view") setup_.auto_drive = true;
                setup_.opponents.clear();
                test_car(app, setup_, q);
                draw_grid(app, setup_, q);
                arcade_tires(app, setup_, q);
                if (sequence_ == "single_race") apply_arcade_options(setup_);
                // A request without laps: the event's (its RACE row), else the arcade's option
                // (race_laps, a new game's 2).
                if (q.laps <= 0) {
                    int laps = 0;
                    if (db::SpecDb* sdb = app.db(); sdb && !q.race.empty()) {
                        gt3r::Vfs* vfs = app.vfs();
                        try {
                            const db::RaceEvents events(*sdb, [vfs](const std::string& path) { return vfs->read(path); });
                            if (const std::optional<db::RaceEvent> e = find_event(events, q.race)) laps = e->laps;
                        } catch (const std::exception& e) {
                            LOG_WARN("Drive: the event's laps: %s", e.what());
                        }
                    }
                    if (laps <= 0 && sequence_ == "single_race")
                        if (const auto it = setup_.options.find("race_laps"); it != setup_.options.end())
                            if (const s32* v = std::get_if<s32>(&it->second)) laps = *v;
                    if (laps <= 0 && (is_license(sequence_) || sequence_ == "mission")) laps = 1; // (a test: one run)
                    if (laps > 0) setup_.laps = std::clamp(laps, 1, 99);
                }
            }
            setup_.course_start = !setup_.online;
            if (setup_.online) {
                // The LAN race: a standing start from the grid with the 10 s hold whatever the course's start code (RACE row
                // sample_entry_1, its start type +0x40 = 2: docs/specs/net.md "Race"), that row its event.
                setup_.start = race::StartType::Ten, setup_.start_given = true;
                if (db::SpecDb* sdb = app.db()) {
                    gt3r::Vfs* vfs = app.vfs();
                    try {
                        if (const std::optional<db::RaceEvent> e = db::RaceEvents(*sdb, [vfs](const std::string& path) { return vfs->read(path); }).event("sample_entry_1")) setup_.event_row = e->bytes;
                    } catch (const std::exception& e) {
                        LOG_WARN("Drive: the LAN race's row: %s", e.what());
                    }
                }
            }
            // A reversed layout is its own course file (crs/r_<name>); one the disc does not have is
            // driven forward.
            if (!app.vfs()->exists("crs/" + setup_.course) && setup_.course.rfind("r_", 0) == 0) {
                LOG_WARN("Drive: the disc has no reversed layout crs/%s: the course is driven forward", setup_.course.c_str());
                setup_.course = setup_.course.substr(2);
            }
        }
        // The sequences that run the player's car alone (RaceFreeRun, the tests); the free run's race
        // code is "freerun" (0x116E20).
        bool prologue_test = false; // (a test of Prologue's with other cars: 26 .. 35)
        if (db::SpecDb* sdb = app.db(); sdb && sdb->prologue() && is_license(setup_.sequence)) {
            gt3r::Vfs* vfs = app.vfs();
            const std::optional<db::RaceEvent> e = db::RaceEvents(*sdb, [vfs](const std::string& path) { return vfs->read(path); }).event(setup_.race);
            if (e && e->grid > 1) prologue_test = true, setup_.cars = e->grid;
        }
        // (a license's pace car tests run with the event's pace car)
        if (setup_.sequence == "time_trial" || setup_.sequence == "freerun" ||
            (is_license(setup_.sequence) && !prologue_test && race::conductor_for(setup_.sequence, setup_.race) != race::Conductor::Training) || setup_.sequence == "machinetest" ||
            setup_.sequence == "photo_drive")
            setup_.cars = 1, setup_.opponents.clear();
        if (setup_.sequence == "freerun" && setup_.race.empty()) setup_.race = "freerun";
        // A race of the menus (or `start=course`): the start the course's start code gives, rolling
        // at the program's speed, else a standing start whose hold the event's row gives (start).
        if (!setup_.start_given && !setup_.sequence.empty()) setup_.course_start = true;
        apply_course_start(app, setup_, setup_.course_start);
        if (db::SpecDb* sdb = app.db()) {
            cars_ = sdb->cars();
            for (db::CourseEntry& c : sdb->courses())
                if (app.vfs()->exists("crs/" + c.label)) courses_.push_back(std::move(c));
        }
        if (cars_.empty() || courses_.empty()) throw FormatError("This disc's cars and courses are not read yet.");
        for (size_t i = 0; i < courses_.size(); ++i)
            if (courses_[i].label == setup_.course) course_sel_ = int(i);
        for (size_t i = 0; i < cars_.size(); ++i)
            if (!setup_.car.empty() && (cars_[i].label == setup_.car || cars_[i].model == setup_.car)) car_sel_ = int(i);
        // A scripted run with the race's menus: menus of its own, booted as the game's.
        if (!chosen && !setup_.sequence.empty() && !menu::quick_project(setup_.sequence).empty()) {
            menu::Runtime::Config config;
            gt3r::Vfs* vfs = app.vfs();
            config.list = [vfs](const std::string& folder) { return vfs->list_dir(folder); };
            if (Disc* disc = app.disc()) config.program_image = [disc] { return disc->program_image(); };
            if (Disc* disc = app.disc(); disc && disc->info().serial.rfind("SCPS", 0) == 0) config.region = "jp", config.language = "JP"; // (Prologue: its text is Japan's)
            config.data_dir = app.save_dir();
            own_menus_ = std::make_unique<menu::Runtime>([vfs](const std::string& path) { return vfs->read(path); }, config);
            own_menus_->boot();
            menus_ = own_menus_.get(), sequence_ = setup_.sequence;
            setup_.options = menus_->game().options;
            setup_.playlist = playlist_from(menus_->game().bgm_playlist);
            setup_.playlist_bytes = save::playlist_block(menus_->game().bgm_playlist);
            menu::RaceRequest& q = menus_->game().race;
            q.course = setup_.course, q.car = setup_.car, q.paint = int(setup_.paint), q.laps = setup_.laps, q.max_entry = setup_.cars;
            q.race = setup_.race, q.sequence = setup_.sequence;
            q.assist_tcs = 1, q.assist_asm = 1, q.tire = 4;
            test_car(app, setup_, q);
            // An event's grid from its pool (`race=<label>`, no `opponents`).
            if (!setup_.race.empty() && setup_.opponents.empty() && setup_.cars > 1) draw_grid(app, setup_, q);
            if (sequence_ == "single_race" && !setup_.event_row.empty()) apply_arcade_options(setup_);
            if (!setup_.race.empty() && setup_.event_row.empty()) // (a race of one car: its event's rules)
                if (db::SpecDb* sdb = app.db())
                    if (const std::optional<db::RaceEvent> e = db::RaceEvents(*sdb, [vfs](const std::string& path) { return vfs->read(path); }).event(setup_.race))
                        setup_.event_row = e->bytes;
        }
        // A saved replay: its course and cars, then its steps played.
        std::optional<race::ReplayFile> saved;
        if (!setup_.entries_file.empty()) load_entries(app, setup_);
        if (setup_.replay_file.rfind("replay/", 0) == 0) {
            try {
                saved_replay_ = load_recorded_race(app, setup_, app.vfs()->read(setup_.replay_file));
            } catch (const std::exception& e) {
                LOG_WARN("Drive: the recorded race: %s", e.what());
            }
        } else if (!setup_.replay_file.empty()) {
            try {
                std::FILE* fp = std::fopen(setup_.replay_file.c_str(), "rb");
                if (!fp) throw FormatError("cannot open " + setup_.replay_file);
                Bytes b;
                u8 buf[65536];
                for (size_t n; (n = std::fread(buf, 1, sizeof buf, fp)) > 0;) b.insert(b.end(), buf, buf + n);
                std::fclose(fp);
                // (a replay of the original's, from its memory card: run again from its records)
                if (!race::ReplayFile::is_replay(b)) saved_replay_ = load_recorded_race(app, setup_, b);
                else {
                    saved = race::ReplayFile::decode(b);
                    setup_.course = saved->course, setup_.laps = std::max(1, saved->laps);
                    setup_.cars = int(saved->cars.size());
                    setup_.opponents.clear(), setup_.paints.clear();
                    for (size_t i = 0; i < saved->cars.size(); ++i) {
                        if (i == 0) setup_.car = saved->cars[i].label;
                        else setup_.opponents.push_back(saved->cars[i].label);
                        setup_.paints.push_back(saved->cars[i].paint);
                    }
                    setup_.skip = 0, setup_.replay = 0, setup_.start = race::StartType::None, setup_.sequence.clear();
                    saved_replay_ = true;
                }
            } catch (const std::exception& e) {
                LOG_WARN("Drive: the saved replay: %s", e.what());
            }
        }
        settings_ = menu::settings_from(setup_.options);
        controllers_ = settings_.controllers;
        if (menus_) setup_.player = player_choice(true);
        if (setup_.online) { // (the wheel's forces are this machine's)
            const input::DeviceConfig& wheel = controllers_.port[0].of(input::Device::Cheetah);
            setup_.player.wheel_strength = gt3r::s16(std::clamp(input::get_feedback(wheel, input::Device::Cheetah, "POWER"), 0, 2));
            setup_.player.wheel_assist = gt3r::s16(input::get_feedback(wheel, input::Device::Cheetah, "POWER_ASSIST"));
        }
        // The first view of a race of the menus: the options' (view_type: SUBJECTIVE the nose,
        // BONNET the higher one, OVERLOOK the chase view).
        if (from_menus_ && (!setup_.online || !setup_.options.empty())) setup_.view = settings_.view == "OVERLOOK" ? race::DriveCamera::kChase : settings_.view == "BONNET" ? race::DriveCamera::kRoof : race::DriveCamera::kInCar;
        if (sequence_ == "race_view") setup_.view = kTvView;
        // A race without the menus: the races' music by the play list a new game has (the
        // configuration script's, run by menus booted for it).
        if (!setup_.playlist) setup_.playlist = new_game_playlist(app);
        if (setup_.scripted) start(app);
        if (saved && race_) race_->play_saved(std::move(*saved));
        if (setup_.demo_run && race_) race_->play_demo(*setup_.demo_run, setup_.demo_skip), demo_ = true;
        if (setup_.save_replay && race_ && race_->has_run()) save_replay(app);
        if (menus_ && sequence_ == "replay") {
            // A saved replay: no race menus; the menus again at its end (or at once without it).
            if (!saved_replay_ || !race_) leave_to_menus(app);
        } else if (menus_ && sequence_ == "race_view") {
            if (!race_) leave_to_menus(app);
        } else if (menus_ && race_) {
            painter_ = make_menu_painter(app);
            enter_quick(0, true, prologue_hud_ && is_license(sequence_)); // (Prologue's lesson: its guide film first)
        } else if (menus_) {
            leave_to_menus(app);
        }
    }
    ~RaceScreen() override {
        feedback_.stop(app_.input(), 0);
        report();
    }

    bool follows_aspect() const override { return race_ != nullptr; } // (Drive's lists are the app's own page)
    // The Discord status (app/presence.h): the kind of run and the course, the car, the lap and the place.
    bool presence(App& app, net::PresenceText& t) override {
        if (!race_) return false;
        net::RacePresence r;
        r.sequence = sequence_.empty() ? setup_.sequence : sequence_;
        r.online = online_ != nullptr, r.replay = race_->replaying() || saved_replay_, r.demonstration = demo_ || race_->demonstrating();
        r.course = course_title(app, setup_.course);
        r.car = race_->car_name(0);
        const race::RaceFlow& f = race_->flow();
        r.cars = int(f.car_count()), r.place = r.cars > 0 ? f.place(0) : 0, r.lap = r.cars > 0 ? f.lap_shown(0) : 0;
        r.laps = setup_.laps, r.finished = f.player_finished();
        if (r.online) r.players = r.cars;
        t = net::race_presence(r);
        return true;
    }

    void update(App& app, double dt) override {
        // (The time from the last frame's drawing to this update: its present and the events.)
        if (render_end_ns_) outside_ns_ = SDL_GetTicksNS() - render_end_ns_;
        const MenuInput& m = app.menu();
        if (race_) {
            const auto word = [&](const char* key, const char* otherwise) { return gt3r::to_lower(app.config().get("cameras", key).value_or(otherwise)); };
            race_->set_view_options(word("race_views", "original") == "all", word("in_car_shake", "on") != "off");
        }
        if (race_) race_->set_effect_settings(race::ReplayEffectSettings::load(app.config())), race_->set_replay_style(settings_.replay_mode == "DIVE" ? race::ReplayStyle::Dive : settings_.replay_display ? race::ReplayStyle::Display : race::ReplayStyle::Plain);
        if (!playing()) feedback_.stop(app.input(), 0);
        if (online_) {
            online_->frame(dt);
            online_ui_.frame(*online_, dt);
            if (online_->over()) return app.pop(); // (the host is back in its lobby)
            if (online_left_) { // (the race left, its others still on: until the host goes back)
                player_input_ = {}, run_steps(app, dt);
                if (leave_online_asked(m)) app.pop();
                return;
            }
        }
        if (quick_ && race_) return update_quick(app, m, dt);
        if (menus_ && sequence_ == "race_view" && race_ &&
            (m.pressed(PadButton::Cross) || m.pressed(PadButton::Circle) || m.pressed(PadButton::Triangle) || m.pressed(PadButton::Start) || race_->flow().everyone_finished()))
            return leave_to_menus(app);
        if (!race_) {
            if (m.pressed(PadButton::Triangle)) {
                app.pop();
                return;
            }
            if (m.horizontal()) column_ = std::clamp(column_ + m.horizontal(), 0, 1);
            int& sel = column_ == 0 ? course_sel_ : car_sel_;
            const int n = column_ == 0 ? int(courses_.size()) : int(cars_.size());
            sel = std::clamp(sel + m.vertical(), 0, n - 1);
            if (m.pressed(PadButton::L1)) sel = std::max(0, sel - 12);
            if (m.pressed(PadButton::R1)) sel = std::min(n - 1, sel + 12);
            // (the mouse: a click selects, a click on a list's selected row drives)
            bool clicked = false;
            for (const RowPointer::Hit& h : mouse_.read(app))
                if (h.click) column_ = h.list;
            for (int c = 0; c < 2; ++c) clicked = mouse_.pick(app, c, c == 0 ? course_sel_ : car_sel_, int(c == 0 ? courses_.size() : cars_.size())) || clicked;
            if (m.pressed(PadButton::Cross) || clicked) {
                setup_.course = courses_[size_t(course_sel_)].label;
                setup_.car = cars_[size_t(car_sel_)].label;
                start(app);
            }
            return;
        }
        // A replay's and a demonstration's keys; Select holds it still (SimplePause: Select again goes on).
        if (race_->viewing() && !paused_ && !results_shown()) {
            if (m.pressed(PadButton::Select) && !race_->replay_done()) {
                replay_paused_ = !replay_paused_;
                race_->pause_sound(replay_paused_);
            }
            if (replay_paused_) return;
            race::ReplayPad pad;
            const int v = m.pressed(PadButton::Up) ? -1 : m.pressed(PadButton::Down) ? 1 : 0;
            pad.up = v < 0, pad.down = v > 0;
            pad.cross = m.pressed(PadButton::Cross), pad.square = m.pressed(PadButton::Square), pad.circle = m.pressed(PadButton::Circle);
            pad.l1 = m.pressed(PadButton::L1), pad.r1 = m.pressed(PadButton::R1), pad.l2 = m.pressed(PadButton::L2), pad.r2 = m.pressed(PadButton::R2);
            pad.l3 = m.pressed(PadButton::L3);
            pad.left_held = m.held(PadButton::Left), pad.right_held = m.held(PadButton::Right), pad.triangle_held = m.held(PadButton::Triangle);
            race_->view_input(pad);
        } else {
            replay_paused_ = false;
        }
        // A demonstration ends when its recording runs out, or on Start.
        if (race_->demonstrating() && (race_->demo_over() || m.pressed(PadButton::Start))) {
            race_->end_demo();
            race_->end_run(false);
            return finish_run(app);
        }
        // A replay: Start ends it.
        if (race_->replaying()) {
            if (m.pressed(PadButton::Start)) race_->stop_replay();
            if (race_->replay_done()) return finish_run(app);
        } else if (results_shown()) {
            if (is_license(sequence_)) race_->license_sound(prize_level());
            // The results page (the game's results movie, else a plain page): leaving it ends the run.
            if (!result_ && !result_tried_) open_results(app);
            bool leave = false;
            if (result_) {
                race_->board().follow(race_->flow());
                if (m.vertical()) result_->key(m.vertical() < 0 ? 0x26 : 0x28);
                if (prologue_license_ && license_page()) { // (its RETRY and EXIT side by side; a letter of the name taken back)
                    if (m.horizontal()) result_->key(m.horizontal() < 0 ? 0x25 : 0x27);
                    if (m.pressed(PadButton::Triangle)) result_->key(0x1B);
                }
                if (m.pressed(PadButton::Cross) && !online_) result_->key(0x20);
                result_->advance(float(dt));
                // (an online race's page stays until the players leave it: the movie's own end starts it again)
                if (online_ && result_exit_) result_exit_ = false, result_->restart(), race_->board().touch();
                leave = result_exit_;
            } else {
                leave = m.pressed(PadButton::Cross) && !online_;
            }
            if (online_ && leave_online_asked(m)) {
                result_.reset();
                return app.pop();
            }
            if (leave && is_license(sequence_) && license_retry_) {
                // RETRY: the result given, the test again at once (end code 4).
                license_retry_ = false;
                result_.reset(), result_tried_ = false;
                report_result();
                race_->set_player(player_choice());
                race_->begin_run(false);
                reported_ = false;
                clock_ = 0;
                return;
            }
            if (leave) {
                result_.reset();
                report_result();
                race_->end_run(!demo_ && (sequence_.empty() || sequence_ == "single_race" || sequence_ == "solo_event" || sequence_ == "championship"));
                if (!race_->replaying()) return finish_run(app);
            }
        } else if (m.pressed(PadButton::Start) && !paused_) {
            open_pause(app);
            return;
        }
        if (paused_) {
            read_controls(app, false); // (what is held as the pause ends is not pressed anew)
            if (online_) player_input_ = {}, run_steps(app, dt); // (an online race waits for nobody)
            return update_pause(app, m, dt);
        }
        if (setup_.save_replay > 1 && race_->run_steps() >= size_t(setup_.save_replay)) setup_.save_replay = 0, save_replay(app);
        const bool pit_menu = update_pit_menu(app, m, dt);
        if (race_->watched() && pit_menu) {
            if (monitor_) monitor_->advance(float(dt)), fill_monitor();
        } else if (race_->watched()) {
            // B-spec's keys (the help strip's): Left / Right the pace, Triangle overtake, Cross the view,
            // R2 the display, Up / Down the car the views follow, Circle the pit (the player's car in at the
            // next pit entry), R1 the Race Monitor.
            if (m.horizontal()) race_->bspec_pace(m.horizontal());
            if (m.pressed(PadButton::Triangle)) race_->bspec_overtake();
            if (m.pressed(PadButton::Cross)) race_->next_view();
            if (m.pressed(PadButton::R2)) race_->bspec_display();
            if (m.vertical()) race_->select_car(m.vertical());
            if (m.pressed(PadButton::Circle)) race_->bspec_pit();
            if (m.pressed(PadButton::R1)) toggle_monitor(app);
            if (monitor_) monitor_->advance(float(dt)), fill_monitor();
        }
        // The player's controls: the views and the pause by the controller's own buttons, as they go down.
        const auto [views, menu_buttons] = read_controls(app, pit_menu);
        const bool mine = !race_->watched() && !race_->replaying();
        if (mine ? (views & input::RaceInput::kPlayView) != 0 && !race_->viewing() : !race_->watched() && !race_->viewing() && (m.pressed(PadButton::R1) || m.pressed(PadButton::Select))) race_->next_view();
        if (playing()) {
            // (the player's pad or wheel unplugged: the race waits)
            const std::string device = app.input().kind(0) == gt3r::DeviceKind::Keyboard ? std::string() : app.input().device_name(0);
            const bool lost = !device_.empty() && device != device_ && !online_;
            device_ = device;
            if (lost) LOG_INFO("Drive: the player's controller is gone: paused");
            if (lost || (menu_buttons & input::RaceInput::kPause)) {
                open_pause(app);
                return;
            }
        }
        // The refuelling stopped (0x3BAC80: the bar's accept, the help strip's "%Be").
        if (!pit_menu && race_->world().refuelling(0) && m.pressed(PadButton::Circle) && !race_->replaying()) race_->stop_refuel();
        update_panel(app, dt);
        if (monitor_ && race_->world().pit_stage(0) != 0) monitor_.reset(); // (into the pits: the live view again)
        if (mine && !race_->viewing()) {
            const bool free_stick = !app.input().wheel_steering(0) && !input::console_layout(app.input(), 0, controllers_.port[0]);
            race_->set_look((player_input_.views & input::RaceInput::kLeftView) != 0, (player_input_.views & input::RaceInput::kRightView) != 0, free_stick ? player_input_.stick[2] : 0,
                            free_stick ? player_input_.stick[3] : 0);
        } else {
            race_->set_look(false, false, 0, 0);
        }
        look_back_ = mine ? (player_input_.views & (input::RaceInput::kBackView | input::RaceInput::kRearView)) != 0 : gt3r::race::look_back_held(app.input().race_pad(0), false);
        // (Settings > Controls: the songs' own controls)
        for (int p = 0; p < gt3r::InputManager::kMaxPlayers; ++p)
            if (const int song = int(app.input().action_pressed(p, gt3r::PadAction::NextSong)) - int(app.input().action_pressed(p, gt3r::PadAction::PreviousSong)))
                if (audio::RaceSound* sound = race_->world().sound()) sound->skip_song(song);
        run_steps(app, dt);
    }

    void run_steps(App& app, double dt) {
        // Whole simulation steps for the time that has passed (at most four a frame).
        const Uint64 t0 = SDL_GetTicksNS();
        // Whole simulation steps for the time the shown frames advance (paced by the display's
        // refresh: the shared engine's pacer), the frame drawn between the last two.
        // (An online race steps by the clock its machines share.)
        const double paced = pacer_.step(dt);
        clock_ += paced;
        int due = online_ ? online_->steps(paced) : std::min(4, int(clock_ / kStep));
        if (due > 0 && !online_) clock_ = due == 4 ? 0 : clock_ - due * double(kStep);
        for (int n = 0; n < due; ++n) {
            for (int k = race_->viewing() ? race_->view().speed() : 1; k-- > 0;) race_->step(player_input_, setup_.auto_drive || spectator_);
            if (const sim::Session* session = race_->world().session(); session && playing()) feedback_.step(input::car_feel(*session, 0), float(kStep));
        }
        if (playing()) feedback_.frame(app.input(), 0, input::FeedbackOptions::from(controllers_.port[0], wheel_.wheel), dt);
        else feedback_.stop(app.input(), 0);
        race_->set_blend(online_ ? online_->alpha() : float(clock_ / kStep));
        step_ns_ += SDL_GetTicksNS() - t0;
    }

    // Another window in front: the player's input stops coming, so the race he drives waits (with the motors and the wheel
    // still), unless his controllers are read in the background.
    void on_event(App& app, const SDL_Event& e) override {
        if (e.type != SDL_EVENT_WINDOW_FOCUS_LOST || !playing() || online_) return;
        SDL_Window* window = SDL_GetWindowFromID(e.window.windowID);
        if (window && (SDL_GetWindowFlags(window) & SDL_WINDOW_NOT_FOCUSABLE)) return; // (a scripted run's)
        if (gt3r::race::focus_loss(true, app.input().reads_background(), false) != gt3r::race::FocusLoss::Pause) return;
        LOG_INFO("Drive: paused, the window lost the focus");
        feedback_.stop(app.input(), 0);
        open_pause(app);
    }

    // The player drives now: not a menu, a pause, a replay or demonstration, B-spec or a scripted run, nor a run that is over.
    bool playing() const {
        return race_ && !quick_ && !paused_ && !demo_ && !setup_.auto_drive && !race_->watched() && !race_->replaying() && !race_->demonstrating() && !race_->over() &&
               !race_->flow().player_finished();
    }

    // The player's controller this frame (player_input_), and its views and menu buttons that went down.
    std::pair<u32, u32> read_controls(App& app, bool directions_taken) {
        const bool mine = !race_->watched() && !race_->replaying();
        player_input_ = input::read_player(app.input(), 0, controllers_.port[0], wheel_, !mine ? input::Mode::Watch : directions_taken ? input::Mode::DriveNoDirections : input::Mode::Drive);
        const std::pair<u32, u32> down{player_input_.views & ~last_views_, player_input_.menu & ~last_menu_};
        last_views_ = player_input_.views, last_menu_ = player_input_.menu;
        return down;
    }

    void render(App& app, Ui& ui) override {
        if (!race_) {
            draw_lists(ui);
            return;
        }
        const Uint64 t0 = SDL_GetTicksNS();
        if (license_page() && !prologue_hud_) {
            // The test's results page: the race in its live inset (the movie's picture, 300 x 225 at
            // its scale; it starts over the whole picture and shrinks into place) on black.
            // A passed test's page has no inset (no "screen" picture in the movie's PASSED part): the
            // race fills the picture, the trophy turns in front (0x3E0E38).
            ui.fill(0, 0, ui.width(), Ui::kHeight, Color{0, 0, 0, 255});
            const SDL_FRect c = RaceHud::canvas();
            if (inset_shown_) {
                const float k = c.h / 480.0f, w = 300 * inset_.scale * k, h = 225 * inset_.scale * k;
                race_->render(ui, {c.x + inset_.x * k - w / 2, c.y + inset_.y * k - h / 2, w, h}, false);
            } else {
                race_->render(ui, {0, 0, ui.width(), float(Ui::kHeight)}, false);
            }
            // The race display's three target times stay over a passed test's page.
            if (hud_ && hud_->ready() && prize_level() < 3) {
                HudState t;
                t.license = true;
                for (int q = 0; q < 3; ++q) t.targets[q] = test_time(q);
                hud_->draw_license_targets(ui, t);
            }
        } else if (quick_ && is_license(sequence_) && !race_->has_run() && !prologue_hud_) {
            // (A license test's page before its first run has no scene behind it; Prologue's shows the recorded run.)
            ui.fill(0, 0, ui.width(), Ui::kHeight, Color{0, 0, 0, 255});
        } else {
            race_->render(ui, {0, 0, ui.width(), float(Ui::kHeight)}, look_back_);
            // (As the menus come up after a run, before they are drawn: the picture a saved replay shows.)
            if (menus_ && replay_picture_.empty() && quick_ && race_->has_run() && !race_->replaying())
                replay_picture_ = replay_picture(app.renderer());
        }
        if (quick_) {
            if (guide_page_ && race_->guide_run() && prologue_hud_ && hud_) { // (the recorded run's monitors)
                race_->fill(hud_state_, *hud_);
                hud_state_.lap_ms = s64(race_->guide_time() * 1000);
                PrologueHud::Extra e = prologue_extra();
                e.live = false, e.guide = true, e.onboard = false, e.over_t = -1, e.penalty = 0, e.wrong_way = false;
                hud_state_.replay_display = true;
                prologue_hud_->draw(ui, hud_state_, e);
            }
            // The race's menus over the scene.
            const float scale = float(Ui::kHeight) / 480.0f, x = (ui.width() - 640 * scale) / 2;
            if (painter_) painter_->draw(*menus_, x, scale);
            if (const float cover = menus_->fade_cover(); cover > 0) ui.fill(0, 0, ui.width(), Ui::kHeight, Color{0, 0, 0, u8(std::clamp(cover, 0.0f, 1.0f) * 255.0f)});
            last_frame_ns_ = 0; // (the race's frame times count the race's frames only)
            return;
        }
        // The results page covers the race display (the program hides it under the page).
        // (nor is it drawn under the Race Monitor)
        const bool monitor_up = monitor_ && race_->watched() && !race_->replaying();
        if (setup_.hud && !monitor_up && (!results_shown() || (prologue_hud_ && license_page()))) {
            if (hud_ && hud_->ready()) {
                race_->fill(hud_state_, *hud_);
                // The display's parts the options switch off; a replay's display as they say.
                const menu::GameSettings::Display& d = settings_.display;
                hud_state_.show_map = d.map, hud_state_.show_time = d.time, hud_state_.show_lap = d.lap;
                hud_state_.show_position = d.position, hud_state_.show_meter = d.meter;
                hud_state_.replay_display = race_->viewing() && race_->view().style() == race::ReplayStyle::Display;
                hud_state_.replay_paused = replay_paused_;
                // A license test: the targets (the page's gold, silver and bronze times).
                hud_state_.license = is_license(sequence_);
                for (int k = 0; k < 3; ++k) hud_state_.targets[k] = test_time(k);
                hud_state_.refuel = !race_->replaying() && race_->world().refuelling(0);
                hud_state_.refuel_fuel = race_->world().fuel(0), hud_state_.refuel_tank = race_->world().tank(0);
                hud_state_.tooltip = race_->replaying() ? std::string() : pit_open_ ? hud_->word("TOOLTIP|PitMenu") : hud_state_.refuel ? hud_->word("TOOLTIP|Refuel") : race_->watched() && race_->world().pit_stage(0) != 0 ? " " : std::string();
                if (race_->replaying()) {
                    if (caption_for_.empty()) music_caption(app);
                    hud_state_.caption_title = caption_title_, hud_state_.caption_artist = caption_artist_;
                } else {
                    caption_for_.clear();
                }
                hud_->draw(ui, hud_state_);
            } else if (prologue_hud_ && hud_) {
                race_->fill(hud_state_, *hud_);
                prologue_hud_->draw(ui, hud_state_, prologue_extra()); // (Prologue's own display)
            } else {
                draw_overlay(ui);
            }
        }
        if (results_shown()) {
            if (result_ && hud_) {
                // The rows' car icons (_EXT_CARICON<row + 1>) over the page.
                std::vector<RaceHud::MovieExternal> ext;
                hud_->draw_movie(license_page() && license_movie_ ? *license_movie_ : *result_movie_, *result_, &ext);
                std::vector<RaceWorld::Icon> icons;
                const auto& rows = race_->board().rows();
                if (license_page()) inset_shown_ = false;
                for (const RaceHud::MovieExternal& e : ext) {
                    if (e.name == "screen" || e.name == "_EXT") inset_ = e, inset_shown_ = true; // the license page's live inset
                    if (e.name == "TropyPanel" && license_page()) draw_trophy(app, ui, e);
                    if (e.name == "BRONZ" && license_page() && prologue_license_) draw_trophy(app, ui, e);
                    if (e.name.rfind("_EXT_CARICON", 0) == 0) {
                        int row = std::atoi(e.name.c_str() + 12) - 1;
                        // (a points page's row: the car whose name it shows)
                        if (points_phase_ >= 2 && row >= 0 && size_t(row) < ranking_shown_.size()) row = int(ranking_shown_[size_t(row)]);
                        if (row >= 0 && size_t(row) < rows.size() && rows[size_t(row)].car >= 0) icons.push_back({size_t(rows[size_t(row)].car), e.x, e.y, e.opacity});
                    }
                }
                race_->draw_icons(ui, RaceHud::canvas(), icons);
            }
            else draw_results(ui);
        }
        if (panel_ && hud_ && !results_shown()) hud_->draw_movie(*monitor_movie_, *panel_);
        if (monitor_ && hud_ && race_ && race_->watched() && !race_->replaying()) {
            ui.fill(0, 0, ui.width(), Ui::kHeight, Color{0, 0, 0, 255}); // (the monitor is the whole picture)
            // The followed car's picture at the movie's CURRENTCAR_ICON, the course map at MAP_PANEL (❓ the rows'
            // two externals each are not drawn).
            std::vector<RaceHud::MovieExternal> ext;
            hud_->draw_movie(*monitor_movie_, *monitor_, &ext);
            std::vector<RaceWorld::Icon> icons;
            for (const RaceHud::MovieExternal& e : ext) {
                if (e.name == "CURRENTCAR_ICON") icons.push_back({race_->focused(), e.x, e.y, e.opacity, true});
                // The gap strip's cars, the rows' paint chips.
                if (e.name.rfind("CARPOS_", 0) == 0) {
                    const size_t at = size_t(e.name.back() - 'A');
                    if (at < race_->flow().order().size()) icons.push_back({race_->flow().order()[at], e.x, e.y, e.opacity});
                }
                if (e.name == "chip") {
                    const size_t row = size_t(std::clamp(int(std::lround((e.y - 189) / 32)), 0, 5));
                    if (row < race_->flow().order().size()) hud_->draw_chip(e.x - 5, e.y - 16, 10, 32, race_->world().paint_chip(race_->flow().order()[row]));
                }
                if (e.name == "MAP_PANEL") {
                    // The course map in the panel with each car's grid letter, the followed car's lit.
                    std::vector<RaceHud::Letter> letters;
                    const std::span<const race::CarState> cars = race_->shown();
                    for (size_t i = 0; i < cars.size(); ++i)
                        letters.push_back({cars[i].position.x, cars[i].position.z, char('A' + std::clamp(race_->grid_place(i), 0, 25))});
                    hud_->draw_map_box(e.x, e.y, 170, letters, race_->focused());
                }
            }
            if (!icons.empty()) race_->draw_icons(ui, RaceHud::canvas(), icons);
            hud_->draw_strip(hud_->word("RaceMonitorToolTip1"));
        }
        if (pit_open_ && pit_ && hud_ && !results_shown() && !race_->replaying()) hud_->draw_movie(*pit_movie_, *pit_);
        if (online_) {
            const bool host = gt3r::net::online().is_host();
            if (online_left_) online_ui_.draw_left(ui, *online_, host);
            else if (results_shown()) online_ui_.draw_results_hint(ui, *online_, host);
            else if (setup_.hud) {
                std::vector<int> places;
                for (size_t i = 0; i < race_->world().car_count(); ++i) places.push_back(race_->flow().place(i));
                online_ui_.draw_race(ui, *online_, race_->labels(), places);
                online_ui_.draw_waiting(ui, *online_);
            }
        }
        if (paused_) {
            if (online_) {
                online_ui_.draw_pause(ui, gt3r::net::online().is_host(), pause_leaving_);
            } else if (pause_ && hud_) {
                hud_->draw_movie(*pause_movie_, *pause_);
            } else {
                ui.fill(0, 0, ui.width(), Ui::kHeight, Color{0, 0, 0, 120});
                ui.text_center(Ui::kHeight / 2 - 10, "PAUSED", colors::kText);
                ui.text_center(Ui::kHeight / 2 + 6, Ui::expand_prompts("{Start}: go on   {Triangle}: leave the race"), colors::kDim);
            }
        }
        // Frame times: the work of this frame, and the time from the last frame to this one.
        const Uint64 now = SDL_GetTicksNS();
        const double work = double(now - t0 + step_ns_) / 1e6;
        step_ns_ = 0;
        if (last_frame_ns_ && !paused_) {
            const double interval = double(now - last_frame_ns_) / 1e6;
            work_ms_.push_back(float(work));
            interval_ms_.push_back(float(interval));
            // GT4R_HITCH=<ms>: every frame longer than that in the log, with this screen's work in it.
            static const double hitch = [] { const char* e = SDL_getenv("GT4R_HITCH"); return e ? std::atof(e) : 0.0; }();
            if (hitch > 0 && interval > hitch)
                LOG_INFO("Drive: frame %zu took %.1f ms (the race's work %.1f ms, the last present and events %.1f ms)", interval_ms_.size(), interval, work,
                         double(outside_ns_) / 1e6);
        }
        last_frame_ns_ = now;
        render_end_ns_ = SDL_GetTicksNS();
        (void)app;
    }

private:
    // ---- B-spec's Race Monitor (menu/common/racemonitor.strb, R1): the program fills the movie's named
    // instances (0x3D8DB8 each frame: 0x3DAF60 the current car, the lap and the total; 0x3DBF90 the rows and
    // the map's letters; 0x3DC5D8). Here: the followed car's name and pace, the total time, the lap, a row
    // per car in race order (its name fitted to its field with "...", grid letter, best lap, gap to the leader; the
    // followed car's row lit); ❓ the map's letters, the gap strip, the pit panel and the replay speed are not filled
    // (hidden).

    void toggle_monitor(App& app) {
        if (monitor_) {
            monitor_.reset();
            return;
        }
        if (!load_monitor_movie(app)) return;
        monitor_ = std::make_unique<race::StrobePlayer>(monitor_movie_, [](const std::string&, const std::vector<race::StrobePlayer::Value>&) {
            return race::StrobePlayer::Value{};
        });
        monitor_->restart();
        // The rows' car names are fitted to their field (0x3DA668): the field the tree names BARn.NAME.
        if (hud_)
            for (const std::string& line : monitor_->tree())
                if (line.rfind("BAR", 0) == 0 && line.find(".NAME ") != std::string::npos)
                    if (const size_t at = line.find("text "); at != std::string::npos) {
                        const size_t field = size_t(std::atoi(line.c_str() + at + 5));
                        if (field < monitor_movie_->texts.size()) hud_->fit_field(&monitor_movie_->texts[field]);
                    }
        fill_monitor();
    }

    // The B-spec commands' panel (the movie's PACEPANEL, 0x3DB520): the pace's, the overtake's and the pit's keys lit, the word of
    // what is asked.
    void fill_pace(race::StrobePlayer& m) {
        using V = race::StrobePlayer::Value;
        const int pace = std::clamp(race_->bspec_pace(), 1, 5);
        const bool overtake = race_->bspec_overtaking(), pit = race_->world().pit_requested(0);
        m.set("PACEPANEL.PACETEXT", "text", V::string(hud_->word(pit ? "key_pitin" : overtake ? "key_overtake" : "key_pace" + std::to_string(pace))));
        for (int k = 1; k <= 7; ++k) m.call("PACEPANEL.PACE" + std::to_string(k), "gotoAndStop", {V::number((k <= 5 ? k == pace : k == 6 ? overtake : pit) ? 2 : 1)});
    }
    // The panel over the live race: the monitor's movie at its second frame (0x3D92B0), down while the car is in the pits
    // (0x3D9998).
    void update_panel(App& app, double dt) {
        const bool up = race_ && hud_ && race_->watched() && !race_->replaying() && !monitor_ && !quick_ && race_->world().pit_stage(0) == 0;
        if (!up) return panel_.reset();
        if (!panel_ && load_monitor_movie(app)) {
            panel_ = std::make_unique<race::StrobePlayer>(monitor_movie_, [](const std::string&, const std::vector<race::StrobePlayer::Value>&) {
                return race::StrobePlayer::Value{};
            });
            panel_->go_to(1);
        }
        if (panel_) panel_->advance(float(dt)), fill_pace(*panel_);
    }
    bool load_monitor_movie(App& app) {
        if (!monitor_movie_) {
            try {
                if (const std::optional<Bytes> file = app.vfs()->read("menu/common/racemonitor.strb"); file && race::StrobeMovie::is_movie(*file))
                    monitor_movie_ = std::make_shared<const race::StrobeMovie>(race::StrobeMovie::parse(*file));
            } catch (const std::exception& e) {
                LOG_WARN("Race monitor: %s", e.what());
            }
        }
        return monitor_movie_ != nullptr;
    }
    void fill_monitor() {
        if (!monitor_ || !race_ || !hud_) return;
        using V = race::StrobePlayer::Value;
        race::StrobePlayer& m = *monitor_;
        const race::RaceFlow& f = race_->flow();
        const size_t me = race_->focused();
        auto text = [&](const std::string& path, const std::string& t) { m.set(path, "text", V::string(t)); };
        auto show = [&](const std::string& path, bool on) { m.set(path, "_visible", V::boolean(on)); };
        // A car's letter: its place on the grid (A the front; GRID sprites' frames 1 .. 6).
        auto letter = [&](size_t car) { return std::clamp(race_->grid_place(car), 0, 5) + 1; };
        text("CURRENTCAR", race_->car_short_name(me));
        m.call("CURRENTCARGRID", "gotoAndStop", {V::number(letter(me))});
        {
            const s64 t = std::max<s64>(0, f.time_ms()) / 1000;
            char b[32];
            std::snprintf(b, sizeof b, "%d:%02d'%02d", int(t / 3600), int(t / 60 % 60), int(t % 60));
            text("TOTAL", b);
        }
        {
            const int lap = f.lap_shown(me), laps = setup_.laps;
            std::string line = hud_->word(laps > 0 ? "RaceMonitorLapFormat1" : "RaceMonitorLapFormat2");
            char b[64];
            if (line.empty()) std::snprintf(b, sizeof b, "Lap %d/%d", lap, laps);
            else std::snprintf(b, sizeof b, line.c_str(), lap, laps);
            text("LAP", b);
        }
        text("SPEED", "x 1");
        fill_pace(m);
        const std::vector<size_t>& order = f.order();
        for (int r = 1; r <= 6; ++r) {
            const std::string bar = "BAR" + std::to_string(r);
            const bool has = size_t(r - 1) < order.size();
            show(bar, true); // (a row without a car stays, blank: the original's one-car Practice)
            if (!has) {
                for (const char* part : {".NAME", ".TIME", ".DIFF"}) text(bar + part, "");
                for (const char* part : {".GRID", ".ICON", ".LAP", ".PIT", ".BEST", ".BESTMARK"}) show(bar + part, false);
                m.call(bar, "gotoAndStop", {V::number(1)});
                continue;
            }
            show(bar + ".GRID", true);
            const size_t car = order[size_t(r - 1)];
            text(bar + ".NAME", race_->car_short_name(car));
            const s64 best = f.car(car).best_lap_ms;
            text(bar + ".TIME", best > 0 ? race::format_lap_time(best) : "--'--.---");
            const s64 gap = race_->gap_ms(car);
            text(bar + ".DIFF", r == 1 || gap < 0 ? "" : race::format_lap_time(gap));
            m.call(bar + ".GRID", "gotoAndStop", {V::number(letter(car))});
            m.call(bar, "gotoAndStop", {V::number(car == me ? 2 : 1)}); // (the followed car's row lit: 0x3DAF60, the monitor's +0x64)
            for (const char* part : {".LAP", ".PIT", ".BEST", ".BESTMARK"}) show(bar + part, false);
        }
        fill_gap_strip(order, me);
        // The pit panel (0x3DB7B8): the followed car's stops, up to three, the oldest first: the lap ("-" none) and the
        // tires fitted front and rear (TireShort_nn, "-" none). ❓ The tire wear diagram (TIRE_PANEL) it alternates with
        // is not shown.
        {
            const std::array<sim::ai::PitStops::StopRecord, 4> rec = race_->pit_record(me);
            int count = 0;
            for (int k = 0; k < 3; ++k) count += rec[size_t(k)].lap > 0;
            auto tire = [&](int code) {
                if (code < 0 || code > 12) return std::string("-");
                char b[32];
                std::snprintf(b, sizeof b, "TireShort_%02d", code);
                const std::string w = hud_->word(b);
                return w.empty() ? std::string("-") : w;
            };
            for (int row = 1; row <= 3; ++row) {
                const std::string p = "PITPANEL.PIT" + std::to_string(row);
                const sim::ai::PitStops::StopRecord* r = row - 1 < count ? &rec[size_t(count - row)] : nullptr;
                text(p + "LAP", r && r->lap > 0 ? std::to_string(r->lap) : "-");
                text(p + "F", r ? tire(r->front) : "-");
                text(p + "R", r ? tire(r->rear) : "-");
            }
            show("PITPANEL", true);
            show("PITPANEL.TIRE_PANEL", false);
        }
    }

    // The Race Monitor's gap strip (0x3DBF90): the cars' letters in race order along the bottom, each further right by
    // the gap to the car ahead (0x3DBE98: 20 canvas units + 38 x clamp((d - 20 m) x 0.0125, 0, 1)), the row centred at
    // (640 - the sum) / 2 + 20; between two letters the gap (0x3DBDF0: laps x the lap + the distance along, ahead less
    // behind) in the options' unit (0x3DBEE8: feet x 3.28084, else metres; a lap or more: "N LAP"), the followed car's
    // letter lit (GRID frame 2 g + 2, else 2 g + 1). ❓ The pace-up marks (0x3FA508) are not worked out (hidden); the
    // cars' pictures the program draws under the letters are not drawn.
    void fill_gap_strip(const std::vector<size_t>& order, size_t me) {
        using V = race::StrobePlayer::Value;
        race::StrobePlayer& m = *monitor_;
        const race::RaceFlow& f = race_->flow();
        const float len = race_->lap_length();
        const bool feet = hud_->mph();
        const std::span<const race::CarState> cars = race_->shown();
        auto progress = [&](size_t car) { return float(f.car(car).counter) * len + (car < cars.size() ? cars[car].course_v : 0.0f); };
        auto spacing = [](float d) { return std::clamp((d - 20.0f) * 0.0125f, 0.0f, 1.0f) * 38.0f + 20.0f; };
        for (char c = 'A'; c <= 'F'; ++c) m.set(std::string("CARPOS_") + c, "_visible", V::boolean(false));
        for (int g = 1; g <= 5; ++g) m.set("GAP" + std::to_string(g), "_visible", V::boolean(false));
        float sum = 0;
        for (size_t k = 1; k < order.size() && k < 6; ++k) sum += spacing(progress(order[k - 1]) - progress(order[k]));
        float x = (640.0f - sum) * 0.5f + 20.0f, prev_x = x;
        for (size_t k = 0; k < order.size() && k < 6; ++k) {
            const size_t car = order[k];
            float d = 0;
            if (k > 0) d = progress(order[k - 1]) - progress(car), x += spacing(d);
            const int g = std::clamp(race_->grid_place(car), 0, 5);
            const std::string pos = std::string("CARPOS_") + char('A' + k); // (the strip's k-th place; its letter the car's)
            m.set(pos, "_x", V::number(x));
            m.set(pos, "_visible", V::boolean(true));
            m.call(pos + ".GRID", "gotoAndStop", {V::number(car == me ? 2 * g + 2 : 2 * g + 1)});
            m.set(pos + ".PACEUP_MARK", "_visible", V::boolean(false));
            if (k > 0) {
                const std::string gap = "GAP" + std::to_string(k);
                std::string t;
                if (d > 0) {
                    char b[32];
                    if (len > 0 && d >= len) std::snprintf(b, sizeof b, "%d LAP", int(d / len));
                    else std::snprintf(b, sizeof b, "%d", int(d * (feet ? 3.28084f : 1.0f) + 0.5f));
                    t = b;
                }
                m.set(gap + ".TEXT", "text", V::string(t));
                m.set(gap, "_x", V::number((x + prev_x) * 0.5f));
                m.set(gap, "_visible", V::boolean(true));
            }
            prev_x = x;
        }
        std::string unit = hud_->word("DistanceFormat");
        char b[64];
        if (unit.find("%s") != std::string::npos) std::snprintf(b, sizeof b, unit.c_str(), feet ? "ft" : "m");
        else std::snprintf(b, sizeof b, "(%s)", feet ? "ft" : "m");
        m.set("DISTUNIT", "text", V::string(b));
    }

    // ---- The PIT STOP menu (menu/common/pitmenu.strb, 0x3DCC50 .. 0x3DD688): up from the pit lane's start until the choice is
    // made or the car is out again, the race going on. True while it is up.

    static constexpr int kFrontTiresOwned = 0x40, kRearTiresOwned = 0x4D; // (0x43F270: the first owned bit of types 0x19 / 0x1A)
    struct PitChoice {
        bool tires = false, fuel = false;      // what may be chosen (+0xE8, +0xEC)
        bool mode = true;                      // the driving mode may be (+0x100: the race's +0x970, 0x110498: an event only without its license, which its entry asks for)
        std::array<std::vector<int>, 2> kinds; // the front and rear kinds offered; one past them: no change
        std::array<int, 2> tire{};             // +0xF0 / +0xF4
        int feed = 1, bspec = 0;               // +0xF8, +0xFC
    };

    bool update_pit_menu(App& app, const MenuInput& m, double dt) {
        if (!race_ || race_->replaying() || quick_ || results_shown()) return pit_open_ = false;
        RaceWorld& world = race_->world();
        if (const std::optional<RaceWorld::PitOffer> offer = world.pit_offer(0)) open_pit_menu(app, *offer);
        if (!pit_open_) return false;
        if (world.pit_offer_over(0)) {
            pit_open_ = false;
            return false;
        }
        if (!pit_) return true;
        pit_->advance(float(dt));
        bool touched = false;
        auto key = [&](int code) { pit_->key(code), touched = true; };
        if (m.vertical()) key(m.vertical() < 0 ? 0x26 : 0x28);
        if (m.horizontal()) key(m.horizontal() < 0 ? 0x25 : 0x27);
        if (m.pressed(PadButton::Cross)) key(0x20);
        if (touched && !pit_touched_) world.pit_wait(0), pit_touched_ = true;
        return pit_open_;
    }

    void open_pit_menu(App& app, const RaceWorld::PitOffer& offer) {
        if (!pit_movie_) {
            try {
                if (const std::optional<Bytes> file = app.vfs()->read("menu/common/pitmenu.strb"); file && race::StrobeMovie::is_movie(*file))
                    pit_movie_ = std::make_shared<const race::StrobeMovie>(race::StrobeMovie::parse(*file));
            } catch (const std::exception& e) {
                LOG_WARN("Pit stop menu: %s", e.what());
            }
        }
        RaceWorld& world = race_->world();
        pit_choice_ = PitChoice{};
        pit_choice_.tires = offer.tires, pit_choice_.fuel = offer.fuel;
        for (int a = 0; a < 2; ++a) {
            std::vector<int>& kinds = pit_choice_.kinds[size_t(a)];
            kinds = world.pit_tire_kinds(0, a);
            const int own = world.fitted_tire(0, a);
            // A Gran Turismo mode race offers the kinds the garage's car has (0x4078D0), and the fitted one.
            if (menus_ && sequence_ != "single_race" && !sequence_.empty())
                if (const int at = menus_->game().race.garage_slot; at >= 0 && size_t(at) < menus_->game().garage.size() && menus_->game().garage[size_t(at)].record) {
                    const menu::CarRecord& car = *menus_->game().garage[size_t(at)].record;
                    std::erase_if(kinds, [&](int k) { return k != own && !car.owned((a == 0 ? kFrontTiresOwned : kRearTiresOwned) + k); });
                }
            const auto at = std::find(kinds.begin(), kinds.end(), own);
            pit_choice_.tire[size_t(a)] = offer.tires && at != kinds.end() ? int(at - kinds.begin()) : int(kinds.size());
        }
        pit_choice_.feed = offer.fuel ? 1 : 0;
        pit_choice_.bspec = offer.bspec ? 1 : 0;
        pit_open_ = true, pit_touched_ = false;
        LOG_INFO("Drive: the pit stop menu at %.1f s (tires %d, fuel %d)", double(race_->flow().time_ms()) / 1000, int(offer.tires), int(offer.fuel));
        if (!pit_movie_) return;
        auto kind_of = [](const std::string& n) { return n == "FTIRE" ? 0 : n == "RTIRE" ? 1 : n == "FEED" ? 2 : 3; };
        auto host = [this, &app, kind_of](const std::string& name, const std::vector<race::StrobePlayer::Value>& args) {
            using V = race::StrobePlayer::Value;
            std::string n = name;
            for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
            auto text = [&](size_t i) { return i < args.size() ? args[i].to_string() : std::string(); };
            auto number = [&](size_t i) { return i < args.size() ? int(args[i].to_number()) : 0; };
            PitChoice& c = pit_choice_;
            const int kind = args.empty() ? 3 : kind_of(text(0));
            const bool allowed = kind < 2 ? c.tires : kind == 2 ? c.fuel : c.mode;
            if (n == "PLAY_SOUND") {
                if (audio::AppSound* sound = app.sound()) sound->menu().play(text(0));
            } else if (n == "NITEM") {
                return V::number(!allowed ? 1 : kind < 2 ? int(c.kinds[size_t(kind)].size()) + 1 : 2);
            } else if (n == "DEFAULTVAL") {
                return V::number(kind < 2 ? c.tire[size_t(kind)] : kind == 2 ? (c.feed ? 1 : 0) : c.mode && c.bspec ? 1 : 0);
            } else if (n == "ITEM") {
                const int at = number(1);
                if (!allowed) return V::string("---");
                if (kind < 2) {
                    const std::vector<int>& kinds = c.kinds[size_t(kind)];
                    if (at < 0 || size_t(at) >= kinds.size()) return V::string(hud_->word("Tire_NoChange"));
                    char b[16];
                    std::snprintf(b, sizeof b, "Tire_%02d", kinds[size_t(at)]);
                    return V::string(hud_->word(b));
                }
                return V::string(hud_->word(kind == 2 ? (at == 1 ? "DoFeed" : "DontFeed") : at == 1 ? "BSpecMode" : "ASpecMode"));
            } else if (n == "RETURNVAL" && args.size() >= 2) {
                const int v = number(1);
                if (kind < 2) c.tire[size_t(kind)] = v;
                else if (kind == 2) c.feed = v != 0;
                else c.bspec = v != 0;
            } else if (n == "EXIT") {
                auto chosen = [&](size_t a) { return u8(size_t(c.tire[a]) < c.kinds[a].size() && c.tires ? c.kinds[a][size_t(c.tire[a])] : 0xFF); };
                LOG_INFO("Drive: the pit stop's choice at %.1f s: tires %d / %d, fuel %d", double(race_->flow().time_ms()) / 1000, chosen(0), chosen(1), c.feed);
                race_->pit_choose(chosen(0), chosen(1), c.feed != 0);
                if (c.mode && !setup_.auto_drive) spectator_ = c.bspec != 0, race_->set_driving_mode(spectator_);
                pit_open_ = false;
            }
            return V{};
        };
        pit_ = std::make_unique<race::StrobePlayer>(pit_movie_, host);
        pit_->navigate(true, false); // (0x3DCC50: the movie's +0x18 set, +0x14 clear)
    }

    // ---- The pause: the game's pause movie (menu/common/pause.strb) and its entries.

    void open_pause(App& app) {
        paused_ = true;
        pause_result_.clear();
        if (online_) { // (a window over the race, which goes on and is heard)
            pause_leaving_ = false;
            return;
        }
        race_->pause_sound(true);
        if (!pause_movie_) {
            try {
                for (const char* path : {"menu/common/pause.strb", "menu/pause.strb"}) // (Prologue's: menu/pause.strb)
                    if (const std::optional<Bytes> file = app.vfs()->read(path); file && race::StrobeMovie::is_movie(*file)) {
                        pause_movie_ = std::make_shared<const race::StrobeMovie>(race::StrobeMovie::parse(*file));
                        break;
                    }
            } catch (const std::exception& e) {
                LOG_WARN("Pause: %s", e.what());
            }
        }
        if (!pause_movie_) return;
        // The entries by the race's class (docs/formats/strobe.md "Entries per mode"): a retry in the
        // license tests, missions and machine tests; Exit asks first in Gran Turismo mode's races.
        const bool retry = is_license(sequence_) || sequence_ == "mission" || sequence_ == "machinetest";
        const bool confirm = sequence_ == "solo_event" || sequence_ == "championship";
        auto host = [this, &app, retry, confirm](const std::string& name, const std::vector<race::StrobePlayer::Value>& args) {
            using V = race::StrobePlayer::Value;
            std::string n = name;
            for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
            auto text = [&](size_t i) { return i < args.size() ? args[i].to_string() : std::string(); };
            if (n == "RETRYENABLE") return V::number(retry ? 1 : 0);
            if (n == "SCREENSETTINGENABLE") return V::number(1);
            if (n == "CONFIRMEXIT") return V::number(confirm ? 1 : 0);
            if (n == "EXIT_MENU") pause_result_ = text(0);
            else if (n == "PLAY_SOUND") {
                if (audio::AppSound* sound = app.sound()) sound->menu().play(text(0));
            } else if (n == "NAVIGATEVERTICAL" && pause_) pause_->navigate(true, false);
            else if (n == "NAVIGATEALL" && pause_) pause_->navigate(true, true);
            else if (n == "DEFAULTVAL") return V::number(picture_settings(spectator_).v[text(0) == "BRIGHTNESS" ? 0 : 1]);
            else if (n == "SETVAL" && args.size() >= 2) picture_settings(spectator_).v[text(0) == "BRIGHTNESS" ? 0 : 1] = std::clamp(int(args[1].to_number()), -5, 5);
            return V{};
        };
        pause_ = std::make_unique<race::StrobePlayer>(pause_movie_, host);
    }

    // An online race over for us: whether the player leaves its results now. The host takes everyone back to the lobby
    // (asking for it while others still race ends the race for them); a guest goes to the menus, where the others' race is
    // waited for.
    bool leave_online_asked(const MenuInput& m) const {
        const bool host = gt3r::net::online().is_host();
        if (online_->lost() || !host || online_->all_done()) return m.pressed(PadButton::Cross);
        return m.pressed(PadButton::Square);
    }

    void update_pause(App& app, const MenuInput& m, double dt) {
        if (online_) { // (the race goes on under the window)
            if (pause_leaving_) {
                if (m.pressed(PadButton::Cross)) {
                    close_pause("continue");
                    pause_leaving_ = false;
                    online_->retire();
                    online_left_ = gt3r::net::online().is_host() && !online_->lost();
                    if (!online_left_) app.pop();
                } else if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) {
                    pause_leaving_ = false;
                }
            } else if (m.pressed(PadButton::Start) || m.pressed(PadButton::Triangle)) {
                close_pause("continue");
            } else if (m.pressed(PadButton::Square)) {
                pause_leaving_ = true;
            }
            return;
        }
        if (!pause_) {
            // No movie: Start goes on, Triangle leaves.
            if (m.pressed(PadButton::Start)) close_pause("continue");
            else if (m.pressed(PadButton::Triangle)) close_pause("exit");
        } else {
            // Cancel goes on at once, whatever page is shown.
            if (m.pressed(PadButton::Triangle)) pause_result_ = "continue";
            else {
                if (m.vertical()) pause_->key(m.vertical() < 0 ? 0x26 : 0x28);
                if (m.horizontal()) pause_->key(m.horizontal() < 0 ? 0x25 : 0x27);
                if (m.pressed(PadButton::Cross)) pause_->key(0x20);
                pause_->advance(float(dt));
            }
            if (!pause_result_.empty()) close_pause(pause_result_);
        }
        if (!paused_ && pause_retry_) {
            // Retry (end code 4): the run again at once, without the menus.
            pause_retry_ = false;
            if (menus_ && !sequence_.empty()) race_->set_bspec_book(menus_->game().race.bspec_book, menus_->game().race.bspec_course_marks);
            race_->set_player(player_choice());
            race_->begin_run(spectator_);
            result_tried_ = false, result_.reset(), reported_ = false;
            clock_ = 0;
            return;
        }
        if (!paused_ && pause_exit_) {
            pause_exit_ = false;
            // Exit: the run ends (end code 2); the sequence's menus again.
            if (menus_) return run_over();
            report();
            prologue_hud_.reset();
            hud_.reset();
            race_.reset();
            if (setup_.scripted) app.pop();
        }
    }
    void close_pause(std::string result) { // (by value: the result given is often pause_result_ itself)
        paused_ = false;
        pause_.reset();
        pause_result_.clear();
        race_->pause_sound(false);
        pause_exit_ = result == "exit";
        pause_retry_ = result == "retry";
    }

    // ---- The race's menus (the sequence's quick-* project) over the standing race.

    // Gran Turismo mode's races whose menus show the run's result (IsSessionFinished).
    bool session_kind() const { return sequence_ == "solo_event" || sequence_ == "championship" || sequence_ == "mission"; }

    // The menus before a run (can_replay 0) or after one (1 driven, 2 watched). `refill`: the
    // page's rows from the grid (else they stay as the run's result left them).
    void enter_quick(int can_replay, bool refill = true, bool guide = false) {
        race_->stand(guide);
        guide_page_ = guide, guide_told_ = false;
        quick_ = true, option_ = false, spectator_ = false, paused_ = false;
        if (refill) fill_quick_work(can_replay);
        else menu::quick_work(menus_->game()).set("canReplay", can_replay);
        menu::quick_work(menus_->game()).set("dispGuidePage", guide ? 1 : 0); // (Prologue's lesson: its guide film's page)
        if (prologue_hud_ && is_license(sequence_)) menu::quick_work(menus_->game()).set("canSaveGhost", can_replay ? 1 : 0); // (✔ lit after a run)
        last_can_replay_ = can_replay;
        menus_->game().during_quick_menu = true;
        try {
            menus_->start_project(menu::quick_project(sequence_));
        } catch (const std::exception& e) {
            menus_->note(std::string("stopped: ") + e.what());
            menus_->finish(); // (the page's command stays COMMAND_NOTHING: the sequence ends)
        }
    }

    // MQuickWork as the sequence fills it before its menu: the single race's (0x111A40, 0x111B50),
    // the others' (the event's 0x114250: rows by grid place, the race's label, its laps and length,
    // no prize, no result yet).
    void fill_quick_work(int can_replay) {
        menu::QuickWork& w = menu::quick_work(menus_->game());
        const menu::RaceRequest& q = menus_->game().race;
        const size_t n = race_->car_count();
        std::vector<int> places(n);
        for (size_t i = 0; i < n; ++i) places[i] = std::clamp(race_->entry(i).grid_place, 0, int(n) - 1);
        w.rows.assign(n, menu::QuickRow{});
        for (size_t i = 0; i < n; ++i) {
            menu::QuickRow& row = w.rows[size_t(places[i])];
            row.car = race_->entry(i).label, row.paint = int(race_->entry(i).paint);
            row.tire = q.tire;
        }
        w.set("numberOfEntries", s32(n));
        w.set("playerGridNumber", places[0]);
        w.set("canReplay", can_replay);
        w.course_label = setup_.course;
        w.race_label = q.race;
        w.player_car = q.car;
        w.set("Laps", setup_.laps);
        menu::fill_player_car(*menus_, w, race_->entry(0).label);
        if (sequence_ == "championship") {
            // The series' session (0x119918): the races run, whether this is its last.
            const menu::GameState& g = menus_->game();
            w.set("SessionNumber", g.series.races_run);
            w.set("IsFinalSession", g.series.race_count > 0 && g.series.races_run + 1 >= g.series.race_count ? 1 : 0);
            // The grid's Pts column (the page reads getPower: the work's +0x364, which holds each entrant's points so
            // far in a championship, 0x11A168; none before the first race).
            for (size_t i = 0; i < n; ++i) {
                const size_t entrant = i; // (the car's number is its entry's, as report_result numbers them)
                w.rows[size_t(places[i])].power = entrant < g.series.entrants.size() ? g.series.entrants[entrant].points : 0;
            }
            w.race_label = championship_race();
        }
        if ((sequence_ == "single_race" || sequence_ == "time_trial") && !quick_tune_filled_) fill_quick_tune(w);
        if (sequence_ != "single_race") {
            w.set("prize", 0);
            w.set("IsSessionFinished", 0);
            w.set("CourseLength", s32(race_->lap_length()));
            return;
        }
        // (IsBSpec and IsDryCourse: the sequence's start sets them, menu::start_quick_work)
    }

    // The Quick Tune's values as Arcade's race and time trial fill them once, before their first menus (0x111B50, 0x112FE8; the
    // sequence's work made anew, menu::start_quick_work, the same): no change of power or weight, the race's tire kind (12 on
    // snow) front and rear, the transmission and the aids as asked, the gearbox's slider of the player's car with its range,
    // whether the course is dry (the page's tire lists offered).
    // They are not filled again after a run: the page shows the choice made (its script reads them back).
    void fill_quick_tune(menu::QuickWork& w) {
        quick_tune_filled_ = true;
        const menu::RaceRequest& q = menus_->game().race;
        const int tire = setup_.course_kind == 3 ? 12 : q.tire;
        w.set("IsDryCourse", setup_.course_kind == 2 || setup_.course_kind == 3 ? 0 : 1); // (0x111B50's +0x48C: the page's tire lists)
        w.set("QuickTunePowerLevel", 0), w.set("QuickTuneWeightLevel", 0);
        w.set("QuickTuneFrontTireType", tire), w.set("QuickTuneRearTireType", tire);
        w.set("QuickTuneTransmission", q.manual ? 1 : 0);
        w.set("QuickTuneDrivingAssist", q.assist_asm ? 2 : q.assist_tcs ? 1 : 0);
        w.set("IsQuickTuned", 0);
        sim::GearSliderRange gears;
        if (db::SpecDb* sdb = app_.db(); sdb && !sdb->prologue() && app_.disc()) {
            try {
                const sim::Program program = sim::Program::load(*app_.disc());
                sim::Equipment eq;
                if (setup_.player_equipment) eq = *setup_.player_equipment;
                else if (!sim::stock_car(*sdb, program, setup_.car, eq)) eq = {};
                gears = sim::quick_tune_gears(*sdb, eq);
            } catch (const std::exception& e) {
                LOG_WARN("Drive: the Quick Tune's gears: %s", e.what());
            }
        }
        w.set("QuickTuneGearMaxSpeed", gears.value), w.set("QuickTuneGearMaxSpeedMin", gears.min), w.set("QuickTuneGearMaxSpeedMax", gears.max);
    }

    // Arcade's race and time trial: the Quick Tune chosen on their menus, onto the player's car as the run starts (0x111DC0,
    // 0x113248, after the aids: player_choice), when the page's OK set IsQuickTuned; then cleared. The car keeps it for the
    // sequence's next runs; the other cars are not touched.
    void apply_quick_tune() {
        if (!menus_ || (sequence_ != "single_race" && sequence_ != "time_trial")) return;
        db::SpecDb* sdb = app_.db();
        menu::QuickWork& w = menu::quick_work(menus_->game());
        if (!sdb || sdb->prologue() || !app_.disc() || w.get("IsQuickTuned") == 0) return;
        w.set("IsQuickTuned", 0);
        try {
            const sim::Program program = sim::Program::load(*app_.disc());
            sim::Equipment eq;
            if (setup_.player_equipment) eq = *setup_.player_equipment;
            else if (!sim::stock_car(*sdb, program, setup_.car, eq)) return;
            sim::QuickTune t;
            t.power_level = w.get("QuickTunePowerLevel"), t.weight_level = w.get("QuickTuneWeightLevel");
            t.front_tire = w.get("QuickTuneFrontTireType"), t.rear_tire = w.get("QuickTuneRearTireType");
            t.gear_top_speed = w.get("QuickTuneGearMaxSpeed");
            gt3r::Vfs* vfs = app_.vfs();
            sim::apply_quick_tune(*sdb, program, [vfs](const std::string& path) { return vfs->read(path); }, eq, t, setup_.course_kind);
            setup_.player_equipment = eq;
            race_->set_player_equipment(eq);
            LOG_INFO("Drive: the Quick Tune: power %d%%, weight %d%%, tire kinds %d / %d, gearbox slider %d", int(eq.b(sim::Equipment::PowerPct)),
                     int(eq.b(sim::Equipment::WeightPct)), int(sim::tire_kind(*sdb, eq, 0)), int(sim::tire_kind(*sdb, eq, 1)), int(eq.b(sim::Equipment::GearSlider)));
        } catch (const std::exception& e) {
            LOG_WARN("Drive: the Quick Tune: %s", e.what());
        }
    }

    // A run of the race: driven (mode 0), watched (1, B-spec) or a demonstration (watched, no result).
    void start_run(bool watched, bool demo = false) {
        quick_ = false;
        spectator_ = watched;
        demo_ = demo;
        reported_ = false;
        replay_picture_.clear();
        if (menus_ && !sequence_.empty()) race_->set_bspec_book(menus_->game().race.bspec_book, menus_->game().race.bspec_course_marks);
        if (menus_ && sequence_ == "championship") {
            const auto& series = menus_->game().series;
            race_->set_last_race(series.race_count > 0 && series.races_run + 1 >= series.race_count);
        }
        race_->set_player(player_choice());
        race_->begin_run(watched);
        clock_ = 0;
    }

    // What the player chose for his car, as the race's sequence takes it (0x10F770 for every sequence; 0x111DC0 / 0x113248 the
    // arcade's race and time trial from their menus; 0x10F848, 0x10F880 the others from the options).
    // `asked`: before the race's menus (their work not made yet): as the request has them.
    RaceWorld::PlayerChoice player_choice(bool asked = false) {
        if (!menus_) return setup_.player;
        RaceWorld::PlayerChoice c;
        const menu::GameSettings s = menu::settings_of(menus_->game());
        controllers_ = s.controllers; // (the Key Configuration, the vibration, the wheel's forces: the options' now)
        if (sequence_ == "single_race" && app_.db() && app_.db()->prologue()) {
            // Prologue's arcade, its settings popup's (0x10A360 with 0x402348): the stability control by the driving assist, the
            // traction control by the option (on; ❓ which of the option page's sets it).
            const menu::RaceRequest& q = menus_->game().race;
            c.automatic = q.manual == 0, c.asm_on = q.assist_type != 0, c.tcs_on = true;
        } else if (asked && (sequence_ == "single_race" || sequence_ == "time_trial")) {
            const menu::RaceRequest& q = menus_->game().race;
            c.automatic = q.manual == 0, c.asm_on = q.assist_asm != 0, c.tcs_on = q.assist_asm != 0 || q.assist_tcs != 0;
        } else if (sequence_ == "single_race" || sequence_ == "time_trial") {
            const menu::QuickWork& w = menu::quick_work(menus_->game());
            c.automatic = w.get("QuickTuneTransmission") == 0;
            const int assist = w.get("QuickTuneDrivingAssist"); // (0x110B90)
            c.asm_on = assist == 2, c.tcs_on = assist == 1 || assist == 2;
        } else {
            c.automatic = app_.db() && app_.db()->prologue() && is_license(sequence_) ? !s.license_manual : s.automatic;
        }
        if (setup_.scripted) c.automatic = c.automatic && setup_.player.automatic, c.asm_on = c.asm_on && setup_.player.asm_on, c.tcs_on = c.tcs_on && setup_.player.tcs_on;
        c.wheel_lock = s.steering_assist_kind(), c.wheel_aid = s.active_steering;
        const input::DeviceConfig& wheel = s.controllers.port[0].of(input::Device::Cheetah);
        c.wheel_strength = gt3r::s16(std::clamp(input::get_feedback(wheel, input::Device::Cheetah, "POWER"), 0, 2));
        c.wheel_assist = gt3r::s16(input::get_feedback(wheel, input::Device::Cheetah, "POWER_ASSIST"));
        return c;
    }

    // The run is over (its results page left and its replay played, or the pause's Exit): its
    // result to Gran Turismo mode, then the sequence's menus.
    void run_over() {
        const int can = spectator_ ? 2 : 1;
        report_result();
        if (session_kind() && !demo_) return enter_quick(can, false);
        enter_quick(can);
    }

    // The run's result as the sequence's result routine gives it (Runtime::race_finished; the
    // event's 0x114498, the license's 0x1105A8, the mission's 0x11C328, a course run's 0x115A90),
    // and the page's rows after the run (0x114390: in finishing order, the times, the player's
    // place).
    void report_result() {
        if (reported_ || demo_ || !menus_ || !race_->has_run()) return;
        reported_ = true;
        const race::RaceFlow& flow = race_->flow();
        const menu::RaceRequest& q = menus_->game().race;
        menu::QuickWork& w = menu::quick_work(menus_->game());
        menu::RaceResult r;
        r.race = q.race;
        r.watched = spectator_;
        r.course = q.course.empty() ? setup_.course : q.course;
        r.garage_slot = q.garage_slot;
        r.finished = flow.player_finished();
        r.place = std::max(0, flow.place(0) - 1);
        r.time_ms = r.finished ? u32(flow.run_ms(0)) : 0;
        r.distance_m = race_->player_distance();
        r.bspec_distance_m = race_->bspec_distance();
        if (race_->aspec_race() && !r.watched) r.score = race_->aspec_taken();
        if (is_license(sequence_)) {
            r.kind = menu::RaceResult::Kind::License;
            // ❓ The test's grading: the time against the page's gold, silver and bronze times.
            const int level = prize_level();
            r.result = level == 0 ? "gold" : level == 1 ? "silver" : level == 2 ? "bronze" : "failed";
            r.name = entered_name_;
        } else if (sequence_ == "mission") {
            r.kind = menu::RaceResult::Kind::Mission;
        } else if (sequence_ == "championship") {
            // A championship's race: the series' label and the race of it run now.
            r.kind = menu::RaceResult::Kind::Championship;
            r.series = q.race, r.race = championship_race();
        } else if (sequence_ == "solo_event") {
            r.kind = menu::RaceResult::Kind::Event;
        } else {
            r.kind = menu::RaceResult::Kind::Course;
            r.course_kind = sequence_ == "freerun" ? 1 : sequence_ == "practice" ? 3 : sequence_ == "photo_drive" ? 4 : sequence_ == "machinetest" ? 6 : 7;
            if (const s64 best = flow.car(0).best_lap_ms; best > 0) r.time_ms = u32(best);
        }
        // A race the B-spec driver drove in: his record back for the book (0x468860), the car's class.
        if (const std::optional<std::array<u8, 0x40>> rec = race_->bspec_record()) {
            r.bspec_record = rec, r.bspec_class = race_->bspec_class(), r.bspec_course = setup_.bspec_course_known;
            r.bspec_start = race_->world_setup().bspec_book, r.bspec_start_marks = race_->world_setup().bspec_course_marks;
        }
        // Every car in finishing order.
        for (const race::RaceFlow::ResultRow& row : flow.results()) {
            menu::RaceResult::Finisher f;
            f.entry = int(row.car); // the car's number is its entry's (0 the player)
            f.car = race_->entry(row.car).label, f.paint = int(race_->entry(row.car).paint);
            f.finished = row.finished;
            if (row.finished) f.time_ms = u32(row.total_ms);
            r.order.push_back(std::move(f));
        }
        try {
            const s64 prize = menus_->race_finished(r); // (the single race keeps its course's best A-spec score: 0x111E98)
            if (session_kind()) w.set("prize", s32(std::clamp<s64>(prize, 0, 0x7FFFFFFF)));
        } catch (const std::exception& e) {
            LOG_WARN("Race result: %s", e.what());
        }
        if (sequence_ == "time_trial" || sequence_ == "freerun" || sequence_ == "machinetest" || is_license(sequence_)) {
            // (A license test's best is its run's time when it passed; the others' their best lap.)
            const s64 best = is_license(sequence_) ? (r.finished && r.result != "failed" ? s64(r.time_ms) : -1) : flow.car(0).best_lap_ms;
            const s32 have = w.get("BestTime");
            if (best > 0 && (have <= 0 || have == menu::QuickWork::kNoTime || best < have)) w.set("BestTime", s32(best));
        }
        if (session_kind()) {
            // The rows in finishing order with their times; a run the player did not finish: no times.
            const std::vector<race::RaceFlow::ResultRow> rows = flow.results();
            const std::vector<menu::QuickRow> kept = w.rows; // (the result routine's: a championship's points, 0x11A168)
            w.rows.assign(rows.size(), menu::QuickRow{});
            for (size_t i = 0; i < rows.size(); ++i) {
                menu::QuickRow& row = w.rows[i];
                if (i < kept.size()) row.power = kept[i].power, row.weight = kept[i].weight;
                row.car = race_->entry(rows[i].car).label, row.paint = int(race_->entry(rows[i].car).paint);
                row.time_ms = r.finished && rows[i].finished ? rows[i].total_ms : menu::QuickWork::kNoTime;
                if (rows[i].car == 0) w.set("playerGridNumber", s32(i));
            }
            w.set("IsSessionFinished", 1);
        }
    }

    // The guide page's picture of the buttons is not drawn (✔ the original's; ❓ why its face is not).
    void hide_guide_buttons() {
        const std::function<void(const std::shared_ptr<menu::LiveWidget>&)> hide = [&](const std::shared_ptr<menu::LiveWidget>& w) {
            if (!w) return;
            if (w->is("ImageFace") && w->name == "ButtonTex") w->set_property("visible", script::Value::integer(0));
            for (const std::shared_ptr<menu::LiveWidget>& c : w->children) hide(c);
        };
        for (const std::shared_ptr<menu::LiveWidget>& page : menus_->open_pages()) hide(page);
    }

    // The guide film's page told its scene is over: its script's end (COMMAND_CONTINUE).
    void end_guide() {
        const std::function<bool(const std::shared_ptr<menu::LiveWidget>&)> tell = [&](const std::shared_ptr<menu::LiveWidget>& w) {
            if (!w) return false;
            if (w->is("SceneViewFace")) {
                auto e = std::make_shared<menu::Object>("MScriptEvent");
                e->set("module", menu::widget_value(w->self()));
                e->set("function", script::Value::string("onSceneViewFinish"));
                menus_->push_event(menu::object_value(e));
                return true;
            }
            for (const std::shared_ptr<menu::LiveWidget>& c : w->children)
                if (tell(c)) return true;
            return false;
        };
        for (const std::shared_ptr<menu::LiveWidget>& page : menus_->open_pages())
            if (tell(page)) {
                guide_told_ = true;
                return;
            }
    }

    void update_quick(App& app, const MenuInput& m, double dt) {
        try {
            send_menu_keys(*menus_, m);
            menus_->update();
        } catch (const std::exception& e) {
            menus_->note(std::string("stopped: ") + e.what());
            return leave_to_menus(app);
        }
        clock_ += dt;
        for (int n = 0; clock_ >= kStep && n < 4; ++n) clock_ -= kStep, race_->step({}, true);
        clock_ = std::min(clock_, double(kStep));
        if (guide_page_ && !guide_told_) hide_guide_buttons();
        // The guide film ends with the recording or on Start (✔ "スタートボタンを押すと終了します").
        if (guide_page_ && !guide_told_ && (race_->guide_done() || m.pressed(PadButton::Start))) end_guide();
        if (!menus_->finished()) return;
        menu::QuickWork& w = menu::quick_work(menus_->game());
        if (option_) return menus_->game().pending_replay.reset(), enter_quick(w.get("canReplay"), false);
        // The command the scripts chose (the sequences' tables: docs/formats/race_flow.md).
        const std::string command = w.take_command();
        menus_->game().during_quick_menu = false;
        const bool session_over = session_kind() && w.get("IsSessionFinished") == 1;
        if ((command == "COMMAND_START" || command == "COMMAND_SPECTATOR") && session_over) {
            // A Gran Turismo mode race already run: the page as before a race, no run.
            enter_quick(0);
        } else if (command == "COMMAND_START") {
            apply_quick_tune();
            start_run(setup_.scripted && setup_.bspec);
        } else if (command == "COMMAND_SPECTATOR") {
            apply_quick_tune();
            start_run(true);
        } else if (command == "COMMAND_CONTINUE" && guide_page_) {
            enter_quick(last_can_replay_, false);
        } else if (command == "COMMAND_DEMONSTRATION" && prologue_hud_ && is_license(sequence_)) {
            enter_quick(last_can_replay_, false, true);
        } else if (command == "COMMAND_DEMONSTRATION") {
            // The test's recorded run (the event's label names its file), else the computer drives.
            const std::string code = menus_->game().race.race.empty() ? setup_.race : menus_->game().race.race;
            quick_ = false, spectator_ = false, demo_ = true, reported_ = false, clock_ = 0;
            if (!race_->begin_demo(app, code)) start_run(true, true);
        } else if (command == "COMMAND_REPLAY" && race_->has_run()) {
            quick_ = false;
            race_->start_replay();
        } else if ((command == "COMMAND_SAVE_REPLAY" || (prologue_hud_ && command == "COMMAND_SAVE_GHOST")) && race_->has_run()) {
            // Save Replay (Prologue's Save Ghost too): the program's project labomode on its save page (MODE_SAVE_FULL_REPLAY: "Create new file" or over
            // one there, a title typed), here into the data folder's replays; then the menus again.
            pend_replay(app);
            if (menus_->is_prologue()) { // (no labomode there: its memcard project's page is not run, the file is written at once)
                menus_->game().pending_replay->save("", "");
                menus_->game().pending_replay.reset();
                enter_quick(w.get("canReplay"), !session_over);
            } else {
                option_ = true;
                menus_->game().next_menu_arg = "MODE_SAVE_FULL_REPLAY";
                menus_->start_project("labomode");
            }
        } else if (command == "COMMAND_LOAD_GHOST") {
            // Load Best Lap Replay: the program's project labomode on its load page (0x11CF08, 0x10FEE8: next_menu_arg
            // MODE_LOAD_BESTLAP_REPLAY), then the menus again. ❓ No best lap replays are kept here: its list is empty, and a
            // loaded ghost is not driven against.
            option_ = true;
            menus_->game().next_menu_arg = "MODE_LOAD_BESTLAP_REPLAY";
            menus_->start_project(prologue_hud_ ? "memcard" : "labomode");
        } else if (command == "COMMAND_OPTION" || command == "COMMAND_SETTINGS") {
            // The options, or the car's settings (the project `setting`), then the menus again.
            option_ = true;
            menus_->start_project(command == "COMMAND_OPTION" ? "option" : "setting");
        } else if (command == "COMMAND_NEXT_RACE" && next_race(app)) {
            // The series' or license's next race at once: its menus (0x119428, 0x10FCD0).
        } else if (command == "COMMAND_EXIT" || command == "COMMAND_NOTHING" || command == "COMMAND_RETIRE_CHAMPIONSHIP" || command == "COMMAND_NEXT_RACE" ||
                   command == "COMMAND_RETURN_MENU") {
            if (sequence_ == "championship") {
                // The series kept to go on later (left before its last race), or closed (retired,
                // or run to its end): 0x119BD8.
                menu::GameState& g = menus_->game();
                const bool over = g.series.race_count > 0 && g.series.races_run >= g.series.race_count;
                if (command == "COMMAND_RETIRE_CHAMPIONSHIP" || over) g.suspend_series(false);
                else if (command == "COMMAND_EXIT") g.suspend_series(true);
            }
            leave_to_menus(app);
        } else {
            // (Saving and loading replays and ghosts and the logger are not done here: the menus again.)
            enter_quick(w.get("canReplay"), !session_over);
        }
    }

    // NEXT_RACE: a championship's next race (0x119428: when the series has one, the race rebuilt on
    // its course with the same entrants, the menus before it) or a license's next test (the license
    // sequence ends with NEXT_RACE and 0x10FCD0 names the next test, run at once). False when there
    // is none: the sequence ends.
    bool next_race(App& app) {
        if (!menus_) return false;
        menu::GameState& g = menus_->game();
        menu::RaceRequest q = g.race;
        if (sequence_ == "championship") {
            if (g.series.race_count <= 0 || g.series.races_run >= g.series.race_count) return false;
            // The series' next race (the series' label with the race's number): its course and laps.
            db::SpecDb* sdb = app.db();
            if (!sdb) return false;
            gt3r::Vfs* vfs = app.vfs();
            const db::RaceEvents events(*sdb, [vfs](const std::string& path) { return vfs->read(path); });
            const std::optional<db::RaceEvent> e = events.event(championship_race());
            if (!e || e->course.empty()) return false;
            q.course = e->course;
            if (e->laps > 0) q.laps = e->laps;
        } else if (sequence_ == "license") {
            const std::string next = menu::next_license_test(q.race);
            if (next.empty()) return false;
            q.race = next, q.course.clear(), q.car.clear(), q.garage_slot = -1;
            menu::complete_race_request(*menus_, q);
            if (q.course.empty() || q.car.empty()) return false;
            q.sequence = sequence_;
            menu::start_quick_work(*menus_, q);
        } else {
            return false;
        }
        g.race = q;
        // The race again on the new course (a test: with its own car), the entrants kept.
        report();
        setup_.course = q.course, setup_.race = q.race;
        if (q.laps > 0) setup_.laps = std::clamp(q.laps, 1, 99);
        if (sequence_ == "license") setup_.car = q.car, setup_.paint = u32(std::max(0, q.paint)), setup_.player_equipment.reset(), setup_.player_condition.reset(), test_car(app, setup_, q);
        if (sequence_ == "championship") { // the start places from the second race on (0x3EE9F8); the entries keep their numbers
            std::vector<std::pair<std::string, std::optional<sim::Equipment>>> entries{{setup_.car, setup_.player_equipment}};
            for (size_t i = 0; i < setup_.opponents.size(); ++i)
                entries.emplace_back(setup_.opponents[i], i < setup_.opponent_equipment.size() ? setup_.opponent_equipment[i] : std::nullopt);
            setup_.grid_slots = g.series.races_run >= 1 ? menu::championship_grid_places(*menus_, entries) : std::vector<int>{};
        }
        if (!app.vfs()->exists("crs/" + setup_.course) && setup_.course.rfind("r_", 0) == 0) setup_.course = setup_.course.substr(2);
        apply_course_start(app, setup_, true);
        result_.reset(), result_tried_ = false, reported_ = false;
        hud_.reset(), race_.reset();
        start(app);
        if (!race_) return false;
        enter_quick(0);
        return true;
    }

    // The music's caption for a replay: the playing piece's title and artist from the streamed
    // music's index (bgm/<region>/ads.inf).
    void music_caption(App& app) {
        caption_title_.clear(), caption_artist_.clear();
        audio::RaceSound* sound = race_->sound();
        audio::AppSound* app_sound = app.sound();
        caption_for_ = sound ? sound->report().music : std::string();
        if (caption_for_.empty()) caption_for_ = "-";
        if (!sound || !app_sound) return;
        const std::string& file = sound->report().music;
        const std::string base = file.substr(file.find_last_of('/') + 1);
        for (const auto& group : app_sound->library().streams().groups)
            for (const sound::MusicRecord& r : group)
                if (!base.empty() && (r.file == base || r.file + ".ads" == base || base.rfind(r.file, 0) == 0)) {
                    caption_title_ = r.title, caption_artist_ = r.artist;
                    return;
                }
    }

    // The run for the menus' save page (menu::GameState::pending_replay; the modes by the program's names, 0x6A0020; ❓ the
    // number given for the license tests and missions alone).
    void pend_replay(App& app) {
        const menu::RaceRequest& q = menus_->game().race;
        auto pending = std::make_shared<menu::GameState::PendingReplay>();
        auto file = std::make_shared<race::ReplayFile>(race_->replay_file(sequence_, q.race));
        static const std::pair<const char*, const char*> kModes[] = {
            {"single_race", "ARCADE_SINGLE_RACE"}, {"time_trial", "ARCADE_TIME_TRIAL"},    {"solo_event", "GTMODE_EVENT_RACE"}, {"championship", "GTMODE_CHAMPIONSHIP"},
            {"freerun", "GTMODE_FREE_RUN"},        {"practice", "GTMODE_FREE_PRACTICE"}, {"mission", "GTMODE_LICENSE_MISSION"},
        };
        pending->mode = "NOTHING";
        for (const auto& [sequence, mode] : kModes)
            if (sequence_ == sequence) pending->mode = mode;
        if (sequence_ == "machinetest") // ("mt400_testcourse")
            pending->mode = q.race.find("mt1000") != q.race.npos ? "GTMODE_MACHINE_TEST_0_1000" : q.race.find("mtmax") != q.race.npos ? "GTMODE_MACHINE_TEST_MAX_SPEED" : "GTMODE_MACHINE_TEST_0_400";
        const std::string label = q.race.size() > 7 ? q.race.substr(q.race.size() - 7) : q.race; // "l0b0001"
        if (is_license(sequence_)) {
            static const std::pair<const char*, const char*> kTests[] = {{"l0b", "GTMODE_LICENSE_B"},   {"l0a", "GTMODE_LICENSE_A"}, {"lib", "GTMODE_LICENSE_IB"},
                                                                        {"lia", "GTMODE_LICENSE_IA"}, {"l0s", "GTMODE_LICENSE_S"}, {"l0c", "GTMODE_LICENSE_COFFEE_BREAK"}};
            pending->mode = "GTMODE_LICENSE";
            for (const auto& [prefix, mode] : kTests)
                if (label.rfind(prefix, 0) == 0) pending->mode = mode;
        }
        if ((is_license(sequence_) || sequence_ == "mission") && label.size() == 7) pending->submode = std::atoi(label.c_str() + 3);
        pending->course = file->course, pending->car = file->cars.empty() ? std::string() : file->cars[0].label;
        if (race_->flow().player_finished()) pending->time_ms = u32(race_->flow().run_ms(0));
        pending->bspec = spectator_, pending->picture = replay_picture_;
        file->mode = pending->mode, file->submode = pending->submode, file->time_ms = pending->time_ms, file->bspec = pending->bspec, file->picture = replay_picture_;
        pending->kb = int((file->encode().size() + 1023) / 1024);
        pending->save = [this, &app, file](const std::string& title, const std::string& path) {
            file->title = title;
            return write_replay(app, *file, path);
        };
        menus_->game().pending_replay = pending;
    }

    // The last run as a file of the data folder's `replays` (the Drive screen plays one with
    // GT4R_RACE="replay_file=<path>").
    void save_replay(App& app) {
        const menu::RaceRequest* q = menus_ ? &menus_->game().race : nullptr;
        write_replay(app, race_->replay_file(sequence_, q ? q->race : std::string()), {});
    }
    bool write_replay(App& app, const race::ReplayFile& file, const std::string& over) {
        const std::string folder = app.save_dir() + "/replays";
        SDL_CreateDirectory(folder.c_str());
        char stamp[32];
        const std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
        const std::string path = over.empty() ? folder + "/" + setup_.course + "-" + stamp + ".gt4rrep" : over;
        const Bytes b = file.encode();
        if (std::FILE* fp = std::fopen(path.c_str(), "wb")) {
            std::fwrite(b.data(), 1, b.size(), fp);
            std::fclose(fp);
            last_saved_ = path;
            LOG_INFO("Drive: the replay saved to %s (%zu bytes)", path.c_str(), b.size());
            return true;
        }
        LOG_WARN("Drive: cannot write %s", path.c_str());
        return false;
    }

    // The race sequence is over: the menus go on where the scripts said (their next project), as
    // after the program's sequence routine (last_sequence = the race's, next_sequence = menu).
    void leave_to_menus(App& app) {
        quick_ = false;
        if (menus_) {
            menu::GameState& g = menus_->game();
            if (setup_.playlist) playlist_to(*setup_.playlist, g.bgm_playlist);
            pictures_to_options(g.options);
            g.during_quick_menu = false;
            g.next_sequence.clear();
            g.last_sequence = sequence_;
            if (g.next_menu_project.empty()) g.next_menu_project = g.major_menu_project;
            if (!menus_->finished()) menus_->finish();
        }
        report();
        app.pop();
    }

    // A championship's race now: the series' label ("..._0500") with the race's number for its last
    // two digits (the races run + 1).
    std::string championship_race() const {
        const menu::GameState& g = menus_->game();
        const std::string& label = g.race.race;
        if (label.size() < 2) return label;
        char n[8];
        std::snprintf(n, sizeof n, "%02d", std::clamp(g.series.races_run + 1, 1, 99));
        return label.substr(0, label.size() - 2) + n;
    }

    // The test's title: "License B-1" from its race code (usl0b0001 or l0b0001: a region's two letters, 'l', '0' or
    // 'i' for the international class, the class letter, the number; ❓ the program's own title routine is not read).
    std::string license_title() const {
        const std::string code = menus_ ? menus_->game().race.race : std::string();
        const size_t l = code.size() > 2 && code[2] == 'l' ? 2 : 0;
        if (code.size() < l + 7 || code[l] != 'l') return "License";
        std::string cls;
        if (code[l + 1] == 'i') cls += 'I';
        cls += char(std::toupper(static_cast<unsigned char>(code[l + 2])));
        return "License " + cls + "-" + std::to_string(std::atoi(code.c_str() + code.size() - 4));
    }

    // Prologue's license record (prologue_natives.cpp) through the menus' script: the test's unit's method with a number.
    int license_call(const char* method, s32 arg) {
        if (!menus_) return -1;
        script::Vm& vm = menus_->vm();
        const script::Value lic = vm.attribute(vm.lookup("game"), "license");
        if (!lic.p) return -1;
        const script::Value code[1] = {script::Value::string(menus_->game().race.race)};
        const script::Value get = vm.attribute(lic, "getUnit");
        const script::Value unit = get.kind == script::Kind::Function ? vm.call(get, code) : script::Value{};
        const script::Value m = unit.p ? vm.attribute(unit, method) : script::Value{};
        if (m.kind != script::Kind::Function) return -1;
        const script::Value a[1] = {script::Value::integer(arg)};
        const script::Value r = vm.call(m, a);
        return r.kind == script::Kind::Int ? int(r.i) : -1;
    }
    std::string license_name() {
        if (!menus_) return {};
        script::Vm& vm = menus_->vm();
        const script::Value lic = vm.attribute(vm.lookup("game"), "license");
        const script::Value m = lic.p ? vm.attribute(lic, "getDefaultName") : script::Value{};
        if (m.kind != script::Kind::Function) return {};
        const script::Value r = vm.call(m, {});
        return r.kind == script::Kind::String ? r.as_string() : std::string();
    }
    void set_license_name(const std::string& name) {
        if (!menus_) return;
        script::Vm& vm = menus_->vm();
        const script::Value lic = vm.attribute(vm.lookup("game"), "license");
        const script::Value m = lic.p ? vm.attribute(lic, "setDefaultName") : script::Value{};
        const script::Value a[1] = {script::Value::string(name)};
        if (m.kind == script::Kind::Function) vm.call(m, a);
    }

    // The license test's results page is up (its live inset of the race).
    bool license_page() const { return is_license(sequence_) && results_shown() && result_; }

    // The test's grade (the movie's PrizeLevel: 0 gold, 1 silver, 2 bronze, 3 failed) by the time
    // against the page's three times; ❓ the test's own rules (a cone touched, the course left, the
    // car not stopped) are not read: the time alone.
    int prize_level() const {
        const menu::QuickWork& w = menu::quick_work(menus_->game());
        const race::RaceFlow& f = race_->flow();
        if (!f.player_finished() || race_->license_failed()) return 3;
        const s64 t = f.run_ms(0);
        (void)w;
        const s64 g = test_time(0), sv = test_time(1), b = test_time(2);
        auto known = [](s64 v) { return v > 0; };
        // (Prologue's tests may have no bronze time: any finish is bronze then, as the original's lesson 1; a test of no time
        // at all has no grade)
        if (prologue_hud_) // (0x3C1050)
            return race::prologue::grade({g, sv, b}, t, race_->license_judge().rules(), race_->graded_place(), race_->mileage(), mileage_wanted_);
        if (!known(b) && !known(g) && !known(sv)) return 3;
        return known(g) && t <= g ? 0 : known(sv) && t <= sv ? 1 : (prologue_hud_ && !known(b)) || (known(b) && t <= b) ? 2 : 3;
    }

    // A test's target time k (0 gold, 1 silver, 2 bronze), ms: the page's (MQuickWork goldTime ..),
    // else the event's row; -1 none.
    s64 test_time(int k) const {
        if (menus_) {
            const char* names[3] = {"goldTime", "silverTime", "bronzeTime"};
            const s32 v = menu::quick_work(menus_->game()).get(names[k]);
            if (v > 0 && v != menu::QuickWork::kNoTime) return v;
        }
        return targets_[k];
    }

    // The results page goes up at the player's finish (ResultArcade::vf7).
    bool results_shown() const { return race_ && !race_->replaying() && !race_->demonstrating() && !race_->over() && race_->flow().player_finished(); }

    // The results movie (menu/common/result.strb) with the race's rows: its callbacks answer from the
    // race's results (docs/formats/strobe.md "The results movie").
    void open_results(App& app) {
        result_tried_ = true, result_exit_ = false, points_phase_ = 0, ranking_shown_.clear();
        trophy_kind_ = -1, inset_shown_ = true;
        entered_name_.clear();
        if (!result_movie_) {
            try {
                for (const char* path : {"menu/common/result.strb", "menu/result.strb"}) // (Prologue's: menu/result.strb)
                    if (const std::optional<Bytes> file = app.vfs()->read(path); file && race::StrobeMovie::is_movie(*file)) {
                        result_movie_ = std::make_shared<const race::StrobeMovie>(race::StrobeMovie::parse(*file));
                        break;
                    }
            } catch (const std::exception& e) {
                LOG_WARN("Results: %s", e.what());
            }
        }
        if (is_license(sequence_) && !license_movie_) {
            for (const char* path : {"menu/US/result_license.strb", "menu/result_license.strb"})
                try {
                    if (const std::optional<Bytes> file = app.vfs()->read(path); file && race::StrobeMovie::is_movie(*file)) {
                        license_movie_ = std::make_shared<const race::StrobeMovie>(race::StrobeMovie::parse(*file));
                        prologue_license_ = std::string(path) == "menu/result_license.strb"; // (Prologue's)
                        break;
                    }
                } catch (const std::exception& e) {
                    LOG_WARN("License results: %s", e.what());
                }
        }
        if (!result_movie_ && !license_movie_) return;
        auto host = [this, &app](const std::string& name, const std::vector<race::StrobePlayer::Value>& args) {
            using V = race::StrobePlayer::Value;
            std::string n = name;
            for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
            race::ResultBoard& board = race_->board();
            const int i = args.empty() ? -1 : int(args[0].to_number());
            const race::ResultBoard::Row* row = i >= 0 && size_t(i) < board.rows().size() ? &board.rows()[size_t(i)] : nullptr;
            // A championship's points pages (result.strb from frame 31; docs/formats/strobe.md): Drivers Point, each row's
            // points for its place (getPoint("R1") .. "R6": "+10 pt"); past the first race (isStage1) the Point Ranking,
            // the rows in order of the points so far with this race's added ("20 pt" in the time's place).
            if (sequence_ == "championship" && menus_) {
                const menu::GameState& g = menus_->game();
                auto entrant_of = [](int car) { return car <= 0 ? size_t(0) : size_t(car); };
                auto before_of = [&](size_t r) {
                    const size_t e = entrant_of(board.rows()[r].car);
                    return e < g.series.entrants.size() ? g.series.entrants[e].points : 0;
                };
                auto total_of = [&](size_t r) { return before_of(r) + menus_->points_for_place(int(r)); };
                // A points text by the race's words ("%c%d pt": a sign or a space, the number).
                auto point_text = [&](char sign, int n) {
                    std::string f = hud_->word("ChampionShipPointFormat");
                    if (f.find("%d") == std::string::npos) f = "%c%d pt";
                    if (const size_t at = f.find("%c"); at != std::string::npos) f.replace(at, 2, 1, sign);
                    f.replace(f.find("%d"), 2, std::to_string(n));
                    return f;
                };
                // The rows in order of points (0x40C538): the most first, a tie in this race's order; before this race, or
                // with its points.
                auto ranking = [&](bool with_race) {
                    std::vector<size_t> order(board.rows().size());
                    for (size_t r = 0; r < order.size(); ++r) order[r] = r;
                    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                        return with_race ? total_of(a) > total_of(b) : before_of(a) > before_of(b);
                    });
                    return order;
                };
                if (n == "ISCHAMPIONSHIP") return V::boolean(true);
                if (n == "ISSTAGE1") return V::boolean(g.series.races_run == 0);
                if (n == "TOPOINTDISP") points_phase_ = 1;
                if (n == "TOPOINTADDPHASE") points_phase_ = 2, board.touch(); // (the rows read again for this part)
                if (n == "TOPOINTRANKINGPHASE") points_phase_ = 3, board.touch();
                if (n == "GETPOINT") {
                    // (a row's point text: "+10 pt" by its place; once the points are added the totals, in the order before
                    // this race)
                    const std::string at = args.empty() ? std::string() : args[0].to_string();
                    const int r = at.size() > 1 && (at[0] == 'R' || at[0] == 'r') ? std::atoi(at.c_str() + 1) - 1 : -1;
                    if (r < 0 || size_t(r) >= board.rows().size()) return V::string({});
                    if (points_phase_ >= 2) return V::string(point_text(' ', total_of(ranking(false)[size_t(r)])));
                    return V::string(point_text('+', menus_->points_for_place(r)));
                }
                if (points_phase_ >= 2 && i >= 0 && size_t(i) < board.rows().size()) {
                    const size_t r = ranking(points_phase_ == 3)[size_t(i)];
                    if (n == "CB_GETNAME") {
                        for (size_t k = ranking_shown_.size(); k < board.rows().size(); ++k) ranking_shown_.push_back(k);
                        ranking_shown_[size_t(i)] = r;
                        const int car = board.rows()[r].car;
                        return V::string(car >= 0 ? race_->car_short_name(size_t(car)) : std::string());
                    }
                    if (n == "CB_GETTIME")
                        return V::string(points_phase_ == 3 ? point_text(' ', total_of(r))
                                                            : std::to_string(before_of(r)) + " +" + point_text(' ', menus_->points_for_place(int(r))));
                }
                if (points_phase_ >= 2 && n == "CB_PLAYERRANK") {
                    const std::vector<size_t> order = ranking(points_phase_ == 3);
                    for (size_t k = 0; k < order.size(); ++k)
                        if (int(order[k]) == board.player_row()) return V::number(double(k));
                }
            }
            // (docs/formats/race_flow.md "The results": the program's callbacks over its rows.)
            if (n == "CB_ISVISIBLE") return V::boolean(row != nullptr);
            if (n == "CB_STABLE") return V::boolean(row && board.take_changed(size_t(i)));
            if (n == "CB_PLAYERRANK") return V::number(board.player_row());
            if (n == "CB_GETNAME") return V::string(row && row->car >= 0 ? race_->car_short_name(size_t(row->car)) : std::string());
            if (n == "CB_GETTIME") return V::string(row ? row->time : std::string());
            if (n == "CB_ENABLEEXIT") return V::number(1);
            if (n == "CB_RACEEXIT") result_exit_ = true;
            if (n == "PLAY_SOUND")
                if (audio::AppSound* sound = app.sound()) sound->menu().play(args.empty() ? std::string() : args[0].to_string());
            if (n == "NAVIGATEALL" && result_) result_->navigate(true, true);
            return V::number(0); // isChampionship, isStage1, ...: no
        };
        if (is_license(sequence_) && license_movie_) {
            race_->tv_view();
            // ResultLicense (0x3E1BF8: result_license.strb): the grade, the time and its gap, the
            // test's title, RETRY / EXIT.
            auto lhost = [this, &app](const std::string& name, const std::vector<race::StrobePlayer::Value>& args) {
                using V = race::StrobePlayer::Value;
                std::string n = name;
                for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
                const race::RaceFlow& f = race_->flow();
                const s64 t = f.player_finished() ? f.run_ms(0) : f.time_ms();
                auto seconds = [](s64 ms, bool sign) {
                    char b[32];
                    std::snprintf(b, sizeof b, "%s%lld.%03lld", sign ? (ms < 0 ? "-" : "+") : "", (long long)(std::llabs(ms) / 1000), (long long)(std::llabs(ms) % 1000));
                    return std::string(b);
                };
                if (n == "PRIZELEVEL") return V::number(prize_level());
                if (prologue_license_) {
                    // Prologue's ResultLicense (0x3D95D8): the time as the program writes it (0x409D88: M'SS.mmm), the
                    // run's place in the test's ranking (0x3FE3B0, -1 none: no name entry), the name offered (the
                    // profile's, 0x402428) and the name entered (NameEntry: into the run's record and the profile's).
                    if (n == "GETTIME") return V::string(race::format_lap_time(race_->license_failed() || !f.player_finished() ? -1 : t)); // (the run's dashes when it failed)
                    if (n == "GETRANK") return V::number(f.player_finished() && prize_level() < 3 ? license_call("getRankOf", s32(t)) : -1);
                    if (n == "GETDEFAULTNAME") return V::string(license_name()); // (none before any entry: one letter at a time from A)
                    if (n == "NAMEENTRY" && !args.empty()) {
                        entered_name_ = args[0].to_string();
                        set_license_name(entered_name_);
                        return V::number(1);
                    }
                }
                // (A test not driven to its end keeps the page's first words: no time, no gap; ResultLicense::vf2 0x3E08D0.)
                if (n == "GETTIME") return V::string(f.player_finished() ? seconds(t, false) : race::format_lap_time(-1));
                if (n == "GETTIMEDIF" && !f.player_finished()) return V::string("");
                if (n == "GETTIMEDIF") {
                    // The gap to the next grade up (a failed run: to bronze).
                    const int level = prize_level();
                    const s64 ref = test_time(level <= 1 ? 0 : level == 2 ? 1 : 2);
                    return V::string(ref > 0 ? seconds(t - ref, true) : std::string());
                }
                if (n == "GETTITLE") return V::string(license_title());
                if (n == "EXIT_MENU") {
                    result_exit_ = true;
                    license_retry_ = !args.empty() && args[0].to_string() == "retry";
                }
                if (n == "PLAY_SOUND")
                    if (audio::AppSound* sound = app.sound()) sound->menu().play(args.empty() ? std::string() : args[0].to_string());
                if (n == "NAVIGATEALL" && result_) result_->navigate(true, true);
                return V::number(0);
            };
            result_ = std::make_unique<race::StrobePlayer>(license_movie_, lhost);
            result_->navigate(true, true);
            return;
        }
        result_ = std::make_unique<race::StrobePlayer>(result_movie_, host);
        result_->play(); // the page up: the movie plays on from its first frame
    }

    // After the run and its replay: the race's menus, back to the menus that asked for the race, or the
    // same race again.
    void finish_run(App& app) {
        result_tried_ = false, result_.reset();
        if (menus_ && saved_replay_) return leave_to_menus(app);
        if (menus_) return run_over();
        if (from_menus_ || saved_replay_) {
            report();
            app.pop();
            return;
        }
        race_->begin_run();
    }

    // The results: each car's place, name and time, the player's row marked. (A plain page in place of
    // the game's results movie.)
    void draw_results(Ui& ui) {
        const std::vector<race::RaceFlow::ResultRow> rows = race_->flow().results();
        const float w = 360, row = 16, h = 40 + row * float(rows.size()), x = (ui.width() - w) / 2, y = (Ui::kHeight - h) / 2;
        ui.fill(x, y, w, h, Color{0, 0, 0, 170});
        ui.text_center(y + 8, hud_ ? hud_->word("RaceResult") : std::string("RESULT"), colors::kText);
        for (size_t i = 0; i < rows.size(); ++i) {
            const race::RaceFlow::ResultRow& r = rows[i];
            const float ry = y + 30 + row * float(i);
            const Color c = r.car == 0 ? colors::kAccent : colors::kText;
            ui.text(x + 12, ry, std::to_string(i + 1), c);
            ui.text(x + 40, ry, Ui::clip(race_->car_name(r.car), 200), c);
            const std::string t = r.finished ? race::format_lap_time(r.total_ms) : std::string("--'--.---");
            ui.text_right(x + w - 12, ry, t, c);
        }
        ui.text_center(y + h + 4, Ui::expand_prompts("{Cross}: OK"), colors::kDim);
    }

    // Prologue's license tests that start on the move: the player's car from its row, the others' from the test's case.
    void prologue_test_start(App& app) {
        setup_.test_starts.clear();
        mileage_wanted_ = 0;
        if (!app.db() || !app.db()->prologue() || !app.disc() || !is_license(sequence_.empty() ? setup_.sequence : sequence_)) return;
        try {
            const sim::RaceRules r = sim::race_rules(setup_.event_row, sim::Program::load(*app.disc()));
            mileage_wanted_ = r.mileage;
            if (r.start_speed[0] == 0 || r.start_distance[0] >= 0) return;
            for (size_t k = 0; k < r.start_speed.size() && int(k) < setup_.cars; ++k) setup_.test_starts.push_back({float(r.start_speed[k]), float(r.start_distance[k])});
            setup_.start = race::StartType::Rolling;
            setup_.start_speed = float(r.start_speed[0]) / 3.6f;
            LOG_INFO("Drive: the test starts on the move: %d km/h, %d m before the line", r.start_speed[0], -r.start_distance[0]);
        } catch (const std::exception& e) {
            LOG_WARN("Drive: the test's start: %s", e.what());
        }
    }

    void start(App& app) {
        error_.clear();
        if (!app.gpu()) {
            error_ = "3D drawing is not available on this system.";
            return;
        }
        prologue_test_start(app);
        // The B-spec driver's book as it is now (race_finished keeps the request's up to date between a sequence's races).
        if (menus_ && !sequence_.empty()) setup_.bspec_book = menus_->game().race.bspec_book, setup_.bspec_course_marks = menus_->game().race.bspec_course_marks;
        // A standing start's hold as the event's row gives it (the machine tests' 7 s; every other race's row but the tests'
        // says 10); a rolling start stays one (RaceBase::vf38), a test's start is its conductor's (load_test).
        if (!setup_.start_given && setup_.start == race::StartType::Ten && app.db() && !app.db()->prologue())
            race::row_start(setup_.event_row, setup_.start, setup_.start_seconds);
        try {
            load_test(app, setup_, menus_ && !setup_.demo_run ? menus_->game().race.race : setup_.race);
            race_ = std::make_unique<Race>(app, setup_, cars_);
            if (setup_.online) {
                online_ = std::make_unique<net::NetRace>(gt3r::net::online(), *setup_.online);
                race_->attach_online(online_.get());
                std::vector<std::string> names;
                for (const net::NetRace::Standing& c : online_->standings()) names.push_back(c.player);
                race_->set_player_names(std::move(names));
            }
            if (setup_.bspec) spectator_ = true;
            hud_ = std::make_unique<RaceHud>(app, race_->course().section(course::kSlotMiniMap));
            prologue_hud_.reset();
            if (!hud_->ready()) { // (Prologue's display)
                auto p = std::make_unique<PrologueHud>(app, race_->course().section(course::kSlotMiniMap));
                if (p->ready()) prologue_hud_ = std::move(p);
                if (prologue_hud_)
                    if (const ByteSpan logo = race_->course().section(kSlotLogo); gt3r::has_magic(logo, "Tex1")) prologue_hud_->set_course_logo(logo);
            }
            race_->set_lap_words(hud_->word("WordOfLap"), hud_->word("WordOfLaps"));
            // A test's gold, silver and bronze times.
            for (int k = 0; k < 3; ++k) targets_[k] = setup_.targets[k];
            // (Prologue's license race writes them into the menus' work from the row as it sets up: 0x409B70 .. 0x409BF0)
            if (menus_ && is_license(sequence_) && targets_[0] > 0 && menu::quick_work(menus_->game()).get("goldTime") == menu::QuickWork::kNoTime) {
                const char* names[3] = {"goldTime", "silverTime", "bronzeTime"};
                for (int k = 0; k < 3; ++k) menu::quick_work(menus_->game()).set(names[k], targets_[k] > 0 ? s32(targets_[k]) : menu::QuickWork::kNoTime);
            }
            // The options' speed unit (unit_velocity), when the race came with the game's options.
            if (!setup_.options.empty()) hud_->set_mph(std::string(settings_.velocity.name).find("mph") != std::string::npos);
            if (const std::optional<Bytes> logo = app.vfs()->read("piece/crs_logo/" + setup_.course + ".img")) hud_->set_course_logo(*logo);
            // The replay card's tires and race name.
            const std::string& seq = setup_.sequence;
            race_->set_card(menus_ ? menus_->game().race.tire : 4,
                            is_license(seq)      ? "=" + license_title()
                            : seq == "time_trial" ? "ARCADE_TIME_TRIAL"
                            : seq == "solo_event" || seq == "championship" ? "GTMODE_EVENT_RACE"
                                                                           : "ARCADE_SINGLE_RACE");
            work_ms_.clear(), interval_ms_.clear();
            last_frame_ns_ = 0, clock_ = 0;
        } catch (const std::exception& e) {
            error_ = e.what();
            LOG_WARN("Drive: %s", e.what());
        }
    }

    // The run's frame times in the log: the mean, the 99th percentile and the worst.
    void report() {
        // What the race's menus' scripts stopped on.
        if (menus_)
            for (const std::string& line : menus_->log())
                if (line.find("error") != std::string::npos || line.find("stopped") != std::string::npos) LOG_WARN("Race menus: %s", line.c_str());
        if (race_)
            if (audio::RaceSound* sound = race_->sound()) {
                const audio::RaceSound::Report& r = sound->report();
                LOG_INFO("Drive: the race's sound: %d count beeps, %d starts, music \"%s\", master %.2f", r.beeps, r.starts, r.music.c_str(), double(sound->master()));
            }
        if (interval_ms_.size() < 10) return;
        auto stats = [](std::vector<float> v, const char* what) {
            std::sort(v.begin(), v.end());
            double sum = 0;
            for (float x : v) sum += x;
            LOG_INFO("Drive: %s over %zu frames: mean %.2f ms, median %.2f, 99%% %.2f, worst %.2f", what, v.size(), sum / double(v.size()),
                     double(v[v.size() / 2]), double(v[v.size() * 99 / 100]), double(v.back()));
        };
        // (The first frames load textures: left out.)
        const size_t skip = std::min<size_t>(30, interval_ms_.size() / 2);
        stats({work_ms_.begin() + ptrdiff_t(skip), work_ms_.end()}, "work per frame");
        stats({interval_ms_.begin() + ptrdiff_t(skip), interval_ms_.end()}, "frame interval");
        interval_ms_.clear(), work_ms_.clear();
    }

    void draw_lists(Ui& ui) {
        ui.header("DRIVE", "");
        const float top = 34, row = 12;
        const int rows = int((Ui::kHeight - 30 - top) / row);
        mouse_.begin();
        auto list = [&](float x, float w, int n, int sel, bool active, const std::function<std::string(int)>& label, const char* title) {
            ui.text(x, top - 12, title, active ? colors::kAccent : colors::kDim);
            const int first = std::clamp(sel - rows / 2, 0, std::max(0, n - rows));
            mouse_.rows(x < 200 ? 0 : 1, first, std::min(rows, n - first), x - 4, top + 2, w, row);
            for (int i = 0; i < rows && first + i < n; ++i) {
                const int idx = first + i;
                const float y = top + 4 + float(i) * row;
                if (idx == sel) ui.fill(x - 4, y - 2, w, row, active ? colors::kHighlight : colors::kPanel);
                ui.text(x, y, Ui::clip(label(idx), w - 8), idx == sel ? colors::kText : colors::kDim);
            }
        };
        list(24, 250, int(courses_.size()), course_sel_, column_ == 0, [&](int i) {
            const db::CourseEntry& c = courses_[size_t(i)];
            return c.name.empty() || c.name == c.label ? c.label : c.name + "  (" + c.label + ")";
        }, "Course");
        list(300, 320, int(cars_.size()), car_sel_, column_ == 1, [&](int i) {
            const db::CarEntry& c = cars_[size_t(i)];
            return c.name.empty() ? c.label : c.name;
        }, "Car");
        if (!error_.empty()) ui.text(24, Ui::kHeight - 36, Ui::clip(error_, Ui::kWidth - 48), colors::kBad);
        ui.footer("{Left/Right}: course or car  {Up/Down}: select  {L1}/{R1}: a page  {Cross}: drive  {Triangle}: back");
    }

    // What Prologue's display shows besides the race's state: the count from the message the race
    // writes ("3", "2", "1", then START), the finish, the run's kind.
    PrologueHud::Extra prologue_extra() const {
        PrologueHud::Extra e;
        const HudState& h = hud_state_;
        if (is_license(setup_.sequence) || is_license(sequence_)) e.run = gt3r::race::HudRun::License;
        else if (setup_.sequence == "time_trial") e.run = gt3r::race::HudRun::TimeTrial;
        else if (setup_.sequence == "freerun") e.run = gt3r::race::HudRun::FreeRun;
        e.live = !h.replay;
        if (race_ && results_shown() && is_license(sequence_)) e.over_t = float(race_->flow().steps_since_player_finish()) / 60, e.passed = prize_level() < 3;
        const race::RaceFlow& flow = race_->flow();
        if (flow.holding()) e.start_left = double(flow.hold_left()) / 60, e.start_total = double(flow.hold_total()) / 60;
        else e.since_start = double(flow.steps_since_start()) / 60;
        if (flow.player_finished()) e.finish_t = double(flow.steps_since_player_finish()) / 60.0;
        for (int k = 0; k < 3; ++k) e.targets[size_t(k)] = test_time(k);
        e.penalty = h.replay ? 0 : race_->penalty_seconds();
        race_->aid_status(e.asm_status, e.tcs_status);
        if (float limit = 0, shift = 0; race_->rev_marks(limit, shift), limit > 0 && shift > 0) e.rev_limit = limit, e.shift_rpm = shift;
        e.onboard = h.driver_view && !race_->opening() && !h.replay;
        const race::CarState& me = race_->player();
        e.accelerator = me.throttle, e.brake = me.brake;
        if (!h.replay) {
            e.wrong_way = race_->wrong_way();
            e.auto_drive = race_->handing_over();
            if (is_license(sequence_)) {
                e.time_left = std::max(0, race_->license_judge().seconds_left());
                e.cones = race_->license_judge().cones(), e.cones_total = race_->license_judge().rules().pylons;
            }
        }
        race_->world().boost(0, e.turbo, e.boost);
        e.hybrid = race_->world().hybrid(0, e.hybrid_flags, e.hybrid_store, e.hybrid_fuel);
        if (e.hybrid) e.hybrid_mileage = mileage_wanted_ > 0 ? float(race_->mileage()) * 0.1f : race_->world().mileage_now(0); // (0x3CC1F8)
        return e;
    }

    // A plain read-out until the game's own race display is drawn.
    void draw_overlay(Ui& ui) {
        const race::CarState& s = race_->player();
        char line[160];
        std::snprintf(line, sizeof line, "%3.0f km/h   gear %s   %4.0f rpm   lap %d", std::fabs(s.speed) * 3.6f,
                      s.gear < 0 ? "R" : s.gear == 0 ? "N" : std::to_string(s.gear).c_str(), s.engine_rpm, s.lap + 1);
        ui.fill(0, Ui::kHeight - 16, ui.width(), 16, Color{10, 11, 14, 150});
        ui.text(8, Ui::kHeight - 13, line, colors::kText);
        ui.text_right(ui.width() - 8, Ui::kHeight - 13, race_->car_name(0), colors::kDim);
    }

    Setup setup_;
    std::vector<db::CarEntry> cars_;
    std::vector<db::CourseEntry> courses_;
    int course_sel_ = 0, car_sel_ = 0, column_ = 0;
    RowPointer mouse_; // Drive's lists'
    std::unique_ptr<Race> race_;
    OnlineRaceUi online_ui_;
    bool online_left_ = false, pause_leaving_ = false; // an online race left before its end; the pause window asks
    std::unique_ptr<net::NetRace> online_; // an online race between its machines (null: a race of this one)
    std::unique_ptr<RaceHud> hud_;
    std::unique_ptr<PrologueHud> prologue_hud_; // (Prologue's display, when the disc's is not Gran Turismo 4's)
    HudState hud_state_;
    std::string error_;
    bool paused_ = false, look_back_ = false, from_menus_ = false;
    bool replay_paused_ = false; // a replay held still (Select)
    std::shared_ptr<const race::StrobeMovie> pause_movie_, monitor_movie_, pit_movie_;
    std::unique_ptr<race::StrobePlayer> panel_; // B-spec's commands over the live race
    std::unique_ptr<race::StrobePlayer> pit_; // the PIT STOP menu
    PitChoice pit_choice_;
    bool pit_open_ = false, pit_touched_ = false;
    std::unique_ptr<race::StrobePlayer> monitor_; // B-spec's Race Monitor, while it is up
    std::unique_ptr<race::StrobePlayer> pause_;
    std::string pause_result_;
    bool pause_exit_ = false, pause_retry_ = false;
    bool saved_replay_ = false; // the screen plays a saved replay (then leaves)
    std::string last_saved_;
    menu::GameSettings settings_; // the game's options as the race reads them
    App& app_;
    // The player's controls and what is sent back to them (src/gt4r/input).
    input::Controllers controllers_;
    input::WheelSettings wheel_;
    input::RaceInput player_input_;
    u32 last_views_ = 0, last_menu_ = 0;
    input::RaceFeedback feedback_;
    std::string device_; // the player's pad or wheel as last seen while driving
    s64 targets_[3] = {-1, -1, -1}; // a test's gold, silver and bronze times from its event (ms)
    std::string caption_for_, caption_title_, caption_artist_; // the replay's music caption (for the piece caption_for_)
    bool demo_ = false, reported_ = false; // the run is a demonstration; its result was given
    int last_can_replay_ = 0;
    bool quick_tune_filled_ = false; // the Quick Tune's values filled for this sequence (fill_quick_tune)
    std::shared_ptr<const race::StrobeMovie> result_movie_;
    std::unique_ptr<race::StrobePlayer> result_;
    bool result_tried_ = false, result_exit_ = false;
    int points_phase_ = 0; // a championship's results movie: 0 the times, 1 Drivers Point, 2 the points added, 3 Point Ranking
    std::vector<size_t> ranking_shown_; // the finishing row each row of the points pages names
    std::shared_ptr<const race::StrobeMovie> license_movie_;
    bool license_retry_ = false;
    bool guide_page_ = false, guide_told_ = false; // the guide film's page is up
    int mileage_wanted_ = 0;        // Prologue's test asks for a mileage (tenths of a kilometer per liter)
    bool prologue_license_ = false; // the license results movie is Prologue's
    std::string entered_name_;      // the name entered on Prologue's results page
    RaceHud::MovieExternal inset_{"_EXT", 320, 240, 640.0f / 300.0f, 1};
    bool inset_shown_ = true;
    // A passed license test's trophy (race/trophy.mdl in the movie's TropyPanel, 0x3E0FF8).
    std::unique_ptr<render::GpuModel> trophy_;
    int trophy_kind_ = -1;
    race::LicenseTrophy trophy_motion_;
    Uint64 trophy_ticks_ = 0;
    void draw_trophy(App& app, Ui& ui, const RaceHud::MovieExternal& panel) {
        const int kind = race::LicenseTrophy::kind_for(prize_level());
        if (kind <= 0 || !app.gpu()) return;
        if (kind != trophy_kind_) {
            trophy_kind_ = kind, trophy_.reset();
            trophy_motion_.start(kind);
            trophy_ticks_ = App::frame_time_ns();
            try {
                const std::optional<Bytes> f = app.vfs()->read("race/trophy.mdl");
                if (f && gt3r::has_magic(*f, "GTM1")) { // (Prologue's: Gran Turismo 3's model, 0x572B68's sets)
                    gt3r::gtm1::Options o;
                    o.program_alpha_test = true, o.static_env_maps = true, o.gs_mip_levels = true; // (its reflection picture its own)
                    o.paint = race::LicenseTrophy::material_for(kind, 3);
                    trophy_ = std::make_unique<render::GpuModel>(*app.gpu(), gt3r::gtm1::build_model(gt3r::gtm1::Gtm1::parse(*f), o, "trophy"));
                } else if (f && mdls::is_mdls(*f)) {
                    gt3r::gtm1::Options o; // (as the race's cars: the reflection passes weighted, the course's reflection picture)
                    o.program_alpha_test = true, o.weighted_reflection = true, o.gs_mip_levels = true;
                    o.paint = race::LicenseTrophy::material_for(kind, int(mdls::read_header(*f).variations));
                    trophy_ = std::make_unique<render::GpuModel>(*app.gpu(), mdls::build_model(*f, o, mdls::Scene{}, "trophy"));
                }
            } catch (const std::exception& e) {
                LOG_WARN("Trophy: %s", e.what());
            }
        }
        if (!trophy_) return;
        // The panel's box (the movie's external record: 282 x 270 about its place) holds the whole
        // 640 x 448 picture the trophy is drawn in.
        const SDL_FRect c = RaceHud::canvas();
        const float k = c.h / 480.0f, w = 282.3f * panel.scale * k, h = 270.5f * panel.scale * k;
        SDL_FRect rect{c.x + panel.x * k - w / 2, c.y + panel.y * k - h / 2, w, h};
        // Prologue's (0x3D99C0: the same motion, 0x3DA130) is drawn over the whole picture.
        if (prologue_license_) rect = c;
        const render::Mat4 proj = render::perspective(race::LicenseTrophy::kFovDegrees * 3.14159265f / 180, race::LicenseTrophy::kAspect, race::LicenseTrophy::kNear,
                                                      race::LicenseTrophy::kFar);
        race_->draw_model(ui, rect, *trophy_, trophy_motion_.matrix(), proj, std::clamp(trophy_motion_.fade.value, 0.0f, 1.0f) * panel.opacity, prologue_license_);
        // Stepped by the time shown, 1/60 s a picture of the original's (two half steps a picture).
        const Uint64 now = App::frame_time_ns();
        float dt = std::min(float(double(now - trophy_ticks_) / 1e9), 0.1f);
        trophy_ticks_ = now;
        while (dt > 0) {
            const float d = std::min(dt, 1.0f / 120);
            trophy_motion_.step(d), dt -= d;
        }
    }
    menu::Runtime* menus_ = nullptr;    // the race's menus run on these (from the menus that asked)
    std::unique_ptr<menu::Runtime> own_menus_; // a scripted run's
    std::string sequence_;              // the race sequence ("single_race")
    std::unique_ptr<MenuPainter> painter_;
    std::vector<u32> replay_picture_; // the run's, for a saved replay (replay_picture.h)
    bool quick_ = false, option_ = false, spectator_ = false;
    double clock_ = 0;
    gt3r::race::FramePacer pacer_;
    Uint64 step_ns_ = 0, last_frame_ns_ = 0, render_end_ns_ = 0, outside_ns_ = 0;
    std::vector<float> work_ms_, interval_ms_;
};

} // namespace

std::unique_ptr<Screen> make_race_screen(App& app) {
    if (const char* e = SDL_getenv("GT4R_RACE"); e && (std::string(",") + e).find(",players=2") != std::string::npos) return make_split_screen(app, nullptr);
    return std::make_unique<RaceScreen>(app);
}
std::unique_ptr<Screen> make_race_screen(App& app, const RaceStart& start) {
    if (start.sequence == "split_battle") return make_split_screen(app, &start);
    return std::make_unique<RaceScreen>(app, &start);
}

} // namespace gt4r
