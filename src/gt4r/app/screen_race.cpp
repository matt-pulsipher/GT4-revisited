// Drive: pick a car and a course, then a race on the course's road: the first car driven with
// the pad, the others by a simple driver, all on the simulation behind race/simulation.h.
//
// GT4R_RACE="course=tsukuba,car=<label>,cars=6,view=chase,drive=auto,skip=600,paint=1,hud=1,sim=rail"
// sets it up without the lists, for pictures: `course` a course file's name, `car` a car's label
// (or its model's name), `cars` how many on the grid, `view` in | chase | roof | back, `drive`
// auto (the simple driver takes the first car too) or pad, `skip` simulation steps run before the
// first picture (the start's hold is left out then), `paint` the first car's paint, `laps` the
// race's length, `start` the start: 10 or 8 (a hold of so many seconds with the opening shots),
// <seconds> (a plain hold), none, rolling, course (the course's own: race/start_rule.h); `view=tv` the course's replay cameras; `replay=N` drives
// N steps unseen and plays them back through the replay cameras; `bspec=1` a B-spec run (GT4R_HITCH=<ms>
// logs every frame longer than that, with the race's work and the present before it); `menus=single_race` the race
// sequence's own menus (quick-arcade) before and after the race (any sequence: solo_event,
// championship, license, mission, time_trial, freerun, practice, machinetest), `race=<label>` its event.

#include "gt4r/app/app.h"
#include "gt4r/app/menu_painter.h"
#include "gt4r/app/race_hud.h"
#include "gt4r/app/race_hud_prologue.h"
#include "gt4r/app/race_world.h"
#include "gt4r/audio/app_sound.h"
#include "gt4r/audio/race_playlist.h"
#include "gt4r/audio/race_sound.h"
#include "gt4r/audio/sound_library.h"

#include "base/log.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/race/drive_camera.h"
#include "gt4r/race/race_flow.h"
#include "gt4r/race/rail_simulation.h"
#include "gt4r/race/replay_script.h"
#include "gt4r/race/replay_file.h"
#include "gt4r/race/start_camera.h"
#include "gt4r/race/start_rule.h"
#include "gt4r/race/picture_settings.h"
#include "gt4r/sim/ai/entrant.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/sim/rules_start.h"
#include "gt4r/menu/game.h"
#include "gt4r/menu/game_settings.h"
#include "gt4r/menu/game_settings.h"
#include "gt4r/menu/race_natives.h"
#include "gt4r/menu/runtime.h"
#include "race/controls.h"
#include "race/interp.h"
#include "gt4r/race/car_pose.h"

#include <algorithm>
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
constexpr int kSlotScenic = 48;            // the course's scenic shots (REP4): behind the race's menus

struct Setup {
    std::string course = "tsukuba";
    std::string car;
    int cars = 6;
    int view = race::DriveCamera::kChase;
    bool look_back = false;
    bool auto_drive = false;
    bool bspec = false;   // `bspec=1`: a B-spec run (the computer drives the player's car; its display and commands)
    int skip = 0;
    u32 paint = 0;
    bool hud = true;
    int laps = 3;
    int replay = 0;
    bool smoke = true;
    bool reflection = true;
    bool rail = false;     // the stand-in simulation instead of the game's
    bool scripted = false; // from GT4R_RACE: no lists
    race::StartType start = race::StartType::Ten;
    int start_seconds = 0;
    float start_speed = 0; // a rolling start's (metres per second)
    bool course_start = false; // the start the course's start code gives (race/start_rule.h)
    bool start_given = false;
    std::vector<std::string> opponents; // the other cars by label (empty: from the car list)
    std::string sequence; // `menus=single_race`: the race's own menus (the sequence's quick project) round the race
    std::vector<u32> paints; // every car's paint (a saved replay's); empty: the player's `paint`, the others by number
    std::string replay_file; // `replay_file=<path>`: a saved replay played
    bool save_replay = false; // `save=1`: the run `replay=N` drove saved as a replay file
    std::string race;     // `race=<label>`: the event (the car database's RACE row) the sequence runs
    sim::ai::Pace pace;   // the computer drivers' (the event's)
    std::map<std::string, std::variant<s32, float, std::string>> options; // the game's (the sound's volumes); empty: a new game's
    std::optional<sim::Equipment> player_equipment;  // a garage car's parts and settings
    std::vector<std::optional<sim::Equipment>> opponent_equipment; // per opponent (as `opponents`): its pool's parts and settings
    std::shared_ptr<audio::RacePlaylist> playlist;   // the races' music (the game's play list)
};

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
        else if (k == "skip") s.skip = std::max(0, std::atoi(v.c_str()));
        else if (k == "paint") s.paint = u32(std::max(0, std::atoi(v.c_str())));
        else if (k == "hud") s.hud = v != "0";
        else if (k == "replay") s.replay = std::max(0, std::atoi(v.c_str()));
        else if (k == "smoke") s.smoke = v != "0";
        else if (k == "reflection") s.reflection = v != "0";
        else if (k == "sim") s.rail = v == "rail";
        else if (k == "laps") s.laps = std::clamp(std::atoi(v.c_str()), 1, 99);
        else if (k == "menus") s.sequence = v;
        else if (k == "race") s.race = v;
        else if (k == "opponents")
            for (std::string_view o : gt3r::split(v, '/')) s.opponents.emplace_back(gt3r::trim(o));
        else if (k == "replay_file") s.replay_file = v;
        else if (k == "save") s.save_replay = v != "0";
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
    return s;
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
        const u32 seed = u32(SDL_GetTicks());
        std::vector<sim::ai::GridEntry> grid = sim::ai::grid_for(events, asked, seed);
        if (!asked.entries.empty())
            for (size_t i = 0; i < grid.size() && i < paints.size(); ++i)
                if (!grid[i].player && grid[i].paint < 0) grid[i].paint = paints[i];
        std::optional<sim::Program> program;
        if (Disc* disc = app.disc()) try {
                program = sim::Program::load(*disc);
            } catch (const std::exception& e) {
                LOG_WARN("Drive: the program: %s", e.what());
            }
        setup.opponents.clear(), setup.opponent_equipment.clear();
        // Every car's equipment, the paints the race chooses drawn among the grid's (sim::ai::grid_equipment).
        const std::vector<sim::Equipment> equipment = program ? sim::ai::grid_equipment(*sdb, *program, events, grid, seed) : std::vector<sim::Equipment>{};
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
        if (const std::optional<db::RaceEvent> e = events.event(q.race)) setup.pace = sim::ai::pace_for(*e);
    } catch (const std::exception& e) {
        LOG_WARN("Drive: the event's grid: %s", e.what());
    }
}

// The screen settings of the racing views and of the replays and B-spec races, kept while the app
// runs. ❓ The options' Picture Quality page keeps them in MOption (race_color_correction,
// replay_color_correction: four numbers each); the menus here do not hold them as numbers yet.
race::PictureSettings& picture_settings(bool replay) {
    static race::PictureSettings race_views, replays{race::PictureSettings::kReplayDefault};
    return replay ? replays : race_views;
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
        if (Disc* disc = app.disc()) config.program = [disc] { return disc->read_program(); };
        config.data_dir = app.data_dir();
        menu::Runtime rt([vfs](const std::string& path) { return vfs->read(path); }, config);
        rt.boot();
        return playlist_from(rt.game().bgm_playlist);
    } catch (const std::exception& e) {
        LOG_WARN("Drive: the races' play list: %s", e.what());
        return nullptr;
    }
}

// The race itself: the world (race_world.h), its flow (race_flow.h) and the cameras.
class Race {
public:
    Race(App& app, const Setup& setup, const std::vector<db::CarEntry>& cars) : setup_(setup) {
        // The first car is the one asked for; the others follow it in the car list.
        size_t first = 0;
        for (size_t i = 0; i < cars.size(); ++i)
            if (cars[i].label == setup.car || cars[i].model == setup.car) first = i;
        WorldSetup ws;
        ws.course = setup.course;
        ws.smoke = setup.smoke, ws.reflection = setup.reflection, ws.rail = setup.rail;
        ws.computer_player = setup.auto_drive || setup.start == race::StartType::Rolling;
        ws.pace = setup.pace;
        ws.options = setup.options;
        ws.playlist = setup.playlist;
        // The grid: the player at the back, the others ahead of him in entry order.
        const std::vector<int> places = race::grid_places(size_t(setup.cars), 0);
        for (int i = 0; i < setup.cars; ++i) {
            WorldEntry e;
            e.label = i == 0 || setup.opponents.empty() ? cars[(first + size_t(i) * 37) % cars.size()].label
                                                         : setup.opponents[size_t(i - 1) % setup.opponents.size()];
            e.paint = size_t(i) < setup.paints.size() ? setup.paints[size_t(i)] : i == 0 ? setup.paint : u32(i);
            e.grid_place = places[size_t(i)];
            if (setup.start == race::StartType::Rolling) {
                // In single file behind the line: the last place 400 m back, 20 m between places
                // (the program's defaults: docs/formats/race_flow.md "Which start a course has").
                e.start_speed = setup.start_speed;
                e.start_distance = float(sim::rules::rolling_start_distance(-400, 20, setup.cars, e.grid_place));
            }
            if (i == 0) e.equipment = setup.player_equipment;
            else if (size_t(i - 1) < setup.opponent_equipment.size() && setup.opponent_equipment[size_t(i - 1)]) {
                // An opponent entered as its pool enters it: its parts, settings and the paint the pool
                // gives (0xFF: the race's choice, here by its number).
                e.equipment = setup.opponent_equipment[size_t(i - 1)];
                if (const u8 p = e.equipment->b(0x18); p != 0xFF && size_t(i) >= setup.paints.size()) e.paint = p;
            }
            ws.entries.push_back(e);
        }
        world_ = std::make_unique<RaceWorld>(app, ws);
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
        for (const course::Runway::Triangle& t : world_->course().road().triangles) stop_area_ = stop_area_ || (t.kind & 4) != 0;
        mount_ = setup.view;
        begin_run(setup.bspec);
        // A scripted run that skips ahead: those steps unseen.
        for (int i = 0; i < setup.skip; ++i) step(gt3r::PadState{}, true);
        if (setup.replay > 0) {
            // A run driven unseen, then shown through the replay cameras.
            for (int i = 0; i < setup.replay; ++i) step(gt3r::PadState{}, true);
            start_replay();
        }
    }

    // The cars on the grid and the start's hold from the beginning; `watched`: the computer drives the
    // player's car too (B-spec).
    void begin_run(bool watched = false) {
        // (A rolling start: the computer drivers drive the player's car up to the line.)
        world_->set_computer_player(watched || setup_.auto_drive || setup_.start == race::StartType::Rolling);
        if (ran_ || watched != watched_) world_->restart();
        watched_ = watched;
        ran_ = true;
        // B-spec: the pace "Steady", the low view from the car's nose (as the original's B-spec race
        // opens), the help strip shown.
        bspec_pace_ = 3, overtake_steps_ = 0, bspec_help_ = true;
        if (watched) mount_ = race::DriveCamera::kInCar;
        menu_scene_ = false;
        race::FlowSetup fs;
        prev_cars_.clear();
        fs.start = setup_.skip > 0 ? race::StartType::None : setup_.start;
        fs.start_seconds = setup_.start_seconds;
        fs.laps = setup_.laps;
        fs.player = 0;
        fs.lap_length = world_->line().length;
        flow_.begin(fs, world_->cars());
        rolled_ = false;
        in_box_ = license_failed_ = false;
        board_.begin(world_->car_count(), 0, lap_words_[0], lap_words_[1]);
        camera_.reset(world_->cars()[0]);
        history_.clear();
        distance_ = 0;
        replaying_ = false, over_ = false;
        event_steps_ = -1;
        begin_start_camera(false);
    }

    // The scene behind the race's menus: the cars standing on the grid, the course's scenic shots
    // (the last run is kept for its replay).
    void stand() {
        if (ran_) world_->restart();
        ran_ = true;
        menu_scene_ = true, replaying_ = false, over_ = false;
        prev_cars_.clear();
        world_->quiet();
        world_->clear_effects();
        scenic_.reset(&scenic_script_);
        start_camera_.begin(nullptr, false, 0, {}, world_->cars()[0]);
    }
    bool has_run() const { return !history_.empty(); }

    // The last run as a saved replay, and a saved replay played back.
    race::ReplayFile replay_file(const std::string& sequence, const std::string& race_label) const {
        race::ReplayFile f;
        f.course = setup_.course, f.sequence = sequence, f.race = race_label, f.laps = setup_.laps;
        for (size_t i = 0; i < world_->car_count(); ++i) f.cars.push_back({world_->setup().entries[i].label, world_->setup().entries[i].paint});
        f.steps = history_;
        return f;
    }
    void play_saved(race::ReplayFile&& f) {
        history_ = std::move(f.steps);
        for (std::vector<race::CarState>& s : history_) s.resize(world_->car_count());
        ran_ = true;
        // (The replay's music: the play list's next piece, as a race's first.)
        if (audio::RaceSound* sound = world_->sound()) sound->rolling();
        start_replay();
    }

    void begin_start_camera(bool replay) {
        const race::CameraMotion& m = replay ? replay_motion_ : start_motion_;
        const course::Runway& road = world_->course().road();
        course::Runway::Place grid;
        if (!road.grid.empty()) grid = road.grid[0];
        const int hold = replay ? flow_.hold_total() : flow_.hold_left();
        start_camera_.begin(m.clips.empty() ? nullptr : &m, replay, hold, grid, replay && !history_.empty() ? history_[0][0] : world_->cars()[0]);
    }

    // One simulation step: the player's car from the pad (or the simple driver), the others by the
    // simple driver, each on its own side of the line.
    void step(const gt3r::PadState& pad, bool auto_drive) {
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
            start_camera_.step();
            camera_.step(history_[replay_at_][0], kStep);
            step_tv(history_[replay_at_][0]);
            // The music ducked under the course's replay-script shots (the designer's cameras).
            if (audio::RaceSound* sound = world_->sound()) sound->set_replay_shot(tv_valid_ && !start_camera_.active());
            hear(history_[replay_at_], false);
            return;
        }
        const std::span<const race::CarState> cars = world_->cars();
        const bool hold = flow_.holding();
        // A rolling start: the player's car is driven for him until it takes the line.
        auto_drive = auto_drive || flow_.player_finished() || handing_over();
        // The computer drivers (race_world.h) drive their cars; without them the simple driver follows
        // the road's line.
        const bool drivers = world_->computer_drivers();
        for (size_t c = 0; c < inputs_.size(); ++c)
            inputs_[c] = hold || (drivers && (c > 0 || watched_ || setup_.auto_drive)) ? race::CarInput{}
                                                                                       : race::follow_line(world_->line(), cars[c], c & 1 ? 2.0f : -2.0f, 1.0f - 0.05f * float(c));
        const gt3r::sim::ControlInputs in = mapper_.update(pad, kStep);
        // (What the pad asks during the hand-over: "GET READY TO DRIVE" once the throttle is held.)
        if (handing_over()) throttle_held_ = in.throttle > 0.5f;
        if (!auto_drive) {
            race::CarInput& me = inputs_[0];
            me = {};
            me.steer = in.steer, me.throttle = in.throttle, me.brake = in.brake, me.handbrake = in.handbrake;
            me.shift_up = in.shift_up, me.shift_down = in.shift_down;
            me.reverse = mapper_.reverse();
            if (me.reverse) me.throttle = std::max(me.throttle, gt3r::race::pressure_to_pedal(pad.press(PadButton::Triangle)));
        }
        const bool opening = opening_tv(), running_up = run_up();
        // A rolling start: the field holds the start's speed until the release, then goes.
        world_->set_speed_hold(running_up && setup_.start_speed > 0 ? setup_.start_speed : 0);
        world_->step(inputs_, hold, auto_drive || watched_ || setup_.auto_drive);
        // (The race clock stands until the release.)
        const race::RaceFlow::Events ev = running_up ? race::RaceFlow::Events{} : flow_.step(world_->cars());
        if (!ev.finished.empty()) board_.finished(flow_, ev.finished);
        if (!hold) distance_ += std::fabs(world_->cars()[0].speed) * kStep;
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
            // A rolling start: the racing music as the field begins to run, the start's sound when
            // the player's car takes the line.
            if (!rolled_ && flow_.setup().start == race::StartType::Rolling) sound->rolling(), rolled_ = true;
            if (ev.start && flow_.setup().start == race::StartType::Rolling) sound->start_signal();
            if (ev.player_finished) sound->finish();
        }
        if (ev.start && flow_.setup().start == race::StartType::Rolling)
            LOG_INFO("Drive: the player's car takes the line at %.1f km/h, %.2f s on the clock", double(world_->cars()[0].speed * 3.6f), double(flow_.time_ms()) / 1000);
        if (is_license(setup_.sequence)) judge_stop();
        if (ev.lap_done == 0) event_steps_ = 0, event_lap_ = flow_.car(0).laps_done;
        beeps_ += ev.beep;
        start_camera_.step();
        camera_.step(world_->cars()[0], kStep);
        if (mount_ == kTvView || opening) step_tv(world_->cars()[0]);
        if (event_steps_ >= 0) ++event_steps_;
        if (history_.size() < kHistory) history_.emplace_back(world_->cars().begin(), world_->cars().end());
        hear(world_->cars(), true);
    }

    // The step's smoke and sound, heard from the camera.
    void hear(std::span<const race::CarState> cars, bool live) {
        const race::ViewPose pose = current_pose(cars, false);
        const auto& m = pose.view.m;
        audio::SoundListener l;
        l.position = {pose.eye.x, pose.eye.y, pose.eye.z};
        l.right = {m[0], m[4], m[8]}, l.up = {m[1], m[5], m[9]}, l.forward = {-m[2], -m[6], -m[10]};
        l.velocity = cars[0].velocity;
        const bool own = !start_camera_.active() && !replaying_ && mount_ != kTvView;
        l.kind = pose.body_hidden ? audio::SoundListener::Kind::InCar : own && mount_ == race::DriveCamera::kChase ? audio::SoundListener::Kind::Chase
                                                                                                                 : audio::SoundListener::Kind::Other;
        world_->present(cars, &l, 0, live);
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
        prev_cars_.clear();
        if (audio::RaceSound* sound = world_->sound()) sound->replay();
        replay_at_ = 0;
        camera_.reset(history_[0][0]);
        replay_camera_.reset();
        world_->clear_effects();
        begin_start_camera(true);
    }
    bool replaying() const { return replaying_; }
    bool replay_done() const { return replay_done_; }
    void stop_replay() { replaying_ = false, replay_done_ = true; }
    bool over() const { return over_; }
    const race::RaceFlow& flow() const { return flow_; }
    race::ResultBoard& board() { return board_; }
    float player_distance() const { return distance_; } // metres the player's car ran this run
    float lap_length() const { return world_->line().length; }
    // The race's words for one lap and several (the results' "+1Lap").
    void set_card(int tire, std::string race_word) { tire_index_ = tire, race_word_ = std::move(race_word); }
    void set_lap_words(std::string one, std::string several) { lap_words_[0] = std::move(one), lap_words_[1] = std::move(several); }
    int beeps() const { return beeps_; }
    std::span<const race::CarState> shown() const { return replaying_ ? std::span<const race::CarState>(history_[replay_at_]) : world_->cars(); }

    // What the race display shows now.
    void fill(HudState& h, const RaceHud& hud) const {
        const std::span<const race::CarState> cars = shown();
        h.replay = replaying_;
        const race::CarState& me = cars[0];
        const race::CarSpec& spec = world_->visual(0).spec();
        h.speed = me.speed, h.rpm = me.engine_rpm, h.gear = me.gear;
        h.throttle = me.throttle, h.brake = me.brake;
        h.trip_m = replaying_ ? 0 : distance_;
        h.lateral_g = replaying_ ? 0 : lateral_g_;
        h.red_line = spec.red_line, h.rev_limit = spec.rev_limit;
        h.driver_view = mount_ != race::DriveCamera::kChase && mount_ != kTvView;
        h.cars.clear();
        for (const race::CarState& c : cars) h.cars.push_back({c.position.x, c.position.z});
        h.place = flow_.place(0);
        h.lap = flow_.lap_shown(0), h.laps = setup_.laps;
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
        // A replay's opening card for its first seconds (❓ 3 s with fades of 0.5 s, judged from the
        // original's replay; the program's times come from its caller, not read).
        h.card_opacity = 0;
        if (replaying_ && replay_at_ < 210) {
            const float t = float(replay_at_) / 60;
            h.card_opacity = std::min({1.0f, t / 0.5f + 0.2f, (3.5f - t) / 0.5f});
            h.card_title = world_->car_name(0);
            char power[32] = "--";
            const float ps = world_->player_power_ps();
            if (ps > 0) std::snprintf(power, sizeof power, "%d%s", int(ps * (hud.mph() ? 0.98632f : 1.0f)), hud.mph() ? "HP" : "PS");
            char tire[32];
            std::snprintf(tire, sizeof tire, "TireShort_%02d", std::clamp(tire_index_, 0, 12));
            const std::string t1 = hud.word(tire);
            std::string line = hud.word("RaceInfoFormat");
            const std::string parts[3] = {power, t1 + "/" + t1, hud.word(race_word_)};
            for (const std::string& part : parts)
                if (const size_t at = line.find("%s"); at != std::string::npos) line.replace(at, 2, part);
            h.card_line = line;
        }
        // The course's logo with the card; the music's caption after it (❓ from 4.5 s to 14 s, judged
        // from the original's replay; the program's times are not read).
        h.logo_opacity = replaying_ ? h.card_opacity : 0;
        h.caption_opacity = 0;
        if (replaying_) {
            const float t = float(replay_at_) / 60;
            h.caption_opacity = std::clamp(std::min((t - 4.5f) / 0.5f, (14.0f - t) / 0.5f), 0.0f, 1.0f);
        }
        if (replaying_) {
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
        // The rolling start's status line (the race words AutoDriveFullThrottle, AutoDriveReady).
        h.status.clear(), h.status_opacity = 0;
        if (handing_over() && !watched_ && !setup_.auto_drive) {
            h.status = hud.word(throttle_held_ ? "AutoDriveReady" : "AutoDriveFullThrottle");
            h.status_opacity = 1;
        }
        h.event.clear(), h.event_opacity = 0;
        if (!replaying_ && event_steps_ >= 0 && event_steps_ < 200) {
            // "L1/3   1'23.456" for three seconds.
            h.event = hud.word("WordOfLapTime") + std::to_string(event_lap_) + "/" + std::to_string(setup_.laps) + "   " + race::format_lap_time(flow_.car(0).last_lap_ms);
            h.event_opacity = std::min(1.0f, float(event_steps_) / 9.0f) * window(event_steps_, 3.0f, 0.15f);
        }
    }

    // B-spec's commands: the pace (-1 / +1, 1 .. 5), the overtake (on until it is asked off again: the
    // program's command handler 0x3F19E0 has no timer of its own), the display shown or hidden. They go to the computer drivers of the
    // player's car (RaceWorld::command).
    bool watched() const { return watched_; }
    void bspec_pace(int step) {
        bspec_pace_ = std::clamp(bspec_pace_ + step, 1, 5);
        world_->command(0, bspec_pace_, overtake_steps_ > 0);
    }
    void bspec_overtake() {
        overtake_steps_ = overtake_steps_ > 0 ? 0 : 1;
        world_->command(0, bspec_pace_, overtake_steps_ > 0);
    }
    void bspec_display() { bspec_help_ = !bspec_help_; }

    // The car's three views, then the course's replay cameras (ours: the game shows those in replays only).
    void next_view() {
        const auto& views = race::DriveCamera::kViews;
        if (mount_ == kTvView) {
            mount_ = views[0];
            return;
        }
        size_t at = 0;
        for (size_t i = 0; i < std::size(views); ++i)
            if (views[i] == mount_) at = i;
        mount_ = at + 1 < std::size(views) ? views[at + 1] : !replay_script_.records.empty() ? kTvView : views[0];
    }

    // Where the shown frame is between the last two steps (0 .. 1), for the cars and the camera.
    void set_blend(float t) { blend_ = std::clamp(t, 0.0f, 1.0f); }

    void render(Ui& ui, const SDL_FRect& rect, bool look_back) {
        std::span<const race::CarState> cars = shown();
        const bool blend = blend_ < 1 && !menu_scene_ && prev_cars_.size() == cars.size() && !cars.empty();
        if (blend) {
            // A frame between two steps: the cars and the camera between their two places.
            blended_.resize(cars.size());
            for (size_t i = 0; i < cars.size(); ++i) blended_[i] = race::blend_states(prev_cars_[i], cars[i], blend_);
            cars = blended_;
        }
        // The racing views' screen settings; the replays' and the B-spec race's their own.
        world_->set_grade(picture_settings(replaying_ || watched_).grade());
        race::ViewPose pose = current_pose(shown(), look_back);
        if (blend) pose = race::blend_poses(previous_pose(look_back), pose, blend_);
        // A cut's dissolve: the last picture frozen at the cut, over the new one as it fades.
        world_->set_dissolve(snapshot_, dissolve_total_ > 0 ? float(dissolve_left_) / float(dissolve_total_) : 0.0f, replaying_ || menu_scene_ || mount_ == kTvView);
        snapshot_ = false;
        world_->render(ui, rect, cars, pose, 0, pose.body_hidden ? 0 : -1);
        // The in-car view's rear-view mirror at the top (❓ its place, (190, 18) 259 x 63 of the
        // 640 x 480 picture, measured on the original's in-car view): not in replays, the opening
        // shots or looking back.
        if (mount_ == race::DriveCamera::kInCar && !replaying_ && !menu_scene_ && !watched_ && !start_camera_.active() && !look_back && !setup_.look_back) {
            const SDL_FRect c = RaceHud::canvas();
            const float k = c.h / 480.0f;
            world_->render_mirror(ui, {c.x + 190 * k, c.y + 18 * k, 259 * k, 63 * k}, cars, pose, 0);
        }
    }

    // A license test on a road with a stopping box (triangles flagged bit 2, the wheel ground record's
    // +0x33 bit 2 the program's judge 0x3F91F0 reads): the test ends when all four wheels are on it
    // and the car is slower than 0.527 m/s (its squared speed under 0.2778), the time taken that
    // share of a step in (0x3F8D70); ❓ a car that has been in the box and leaves it moving fails
    // (judged from the original's B-1: running past the box ends on FAIL; that rule is not read).
    void judge_stop() {
        if (!stop_area_ || flow_.steps_since_start() < 0 || flow_.player_finished()) return;
        const race::CarState& c = world_->cars()[0];
        bool all = true, any = false;
        for (const race::WheelState& w : c.wheels) (w.surface & 4) ? any = true : all = false;
        const float v2 = c.velocity.x * c.velocity.x + c.velocity.y * c.velocity.y + c.velocity.z * c.velocity.z;
        if (all && v2 < 0.27778f) {
            flow_.finish_now(0, v2 / 0.27778f);
            license_failed_ = false;
        } else if (in_box_ && !any && v2 >= 0.27778f) {
            flow_.finish_now(0);
            license_failed_ = true;
        }
        in_box_ = in_box_ || any;
    }
    bool license_failed() const { return license_failed_; }

    // Before a step: the cars and the cameras as they are, for the frames shown before the next.
    void keep_previous() {
        const std::span<const race::CarState> now = shown();
        prev_cars_.assign(now.begin(), now.end());
        camera_prev_ = camera_, start_prev_ = start_camera_;
        tv_prev_ = tv_pose_, tv_prev_valid_ = tv_valid_;
    }
    // The camera at the last step but one (its cameras as they were then, on the cars as they were).
    race::ViewPose previous_pose(bool look_back) {
        std::swap(camera_, camera_prev_), std::swap(start_camera_, start_prev_), std::swap(tv_pose_, tv_prev_), std::swap(tv_valid_, tv_prev_valid_);
        const race::ViewPose p = current_pose(prev_cars_, look_back);
        std::swap(camera_, camera_prev_), std::swap(start_camera_, start_prev_), std::swap(tv_pose_, tv_prev_), std::swap(tv_valid_, tv_prev_valid_);
        return p;
    }

    // A rolling start: the player's car's metres to the line before it takes it (-1: none, or past it).
    float to_line() const {
        if (!handing_over() || setup_.skip > 0) return -1;
        const race::CarState& c = world_->cars()[0];
        const float len = world_->line().length;
        if (!(len > 0)) return -1;
        return c.course_v > len * 0.5f ? len - c.course_v : (c.lap < 0 ? len - c.course_v : 0.0f);
    }
    // A rolling start's run-up: the field at the start's speed in single file, the race clock standing,
    // until the release, when the player's car is kRelease metres from the line; the course's TV cameras
    // show it until kOpeningEnd metres (❓ both judged from the original's Tsukuba start: the field at
    // 50 mph through some 12 s of shots, the clock starting during the last one, the player's view
    // about 1.5 s later, the line 4.2 s after the clock's start at 50 -> 56 mph; the program's hold
    // is not read).
    static constexpr float kRelease = 80, kOpeningEnd = 50;
    bool run_up() const { return to_line() > kRelease; }
    bool opening_tv() const { return to_line() > kOpeningEnd; }

    // A rolling start before the player's car takes the line: it is driven for him.
    bool handing_over() const { return flow_.setup().start == race::StartType::Rolling && flow_.steps_since_start() < 0 && !replaying_ && !menu_scene_; }

    // The replay cameras' shot this step (once a step: their dampers and springs move a step at a
    // time); a cut with a dissolve starts it.
    void step_tv(const race::CarState& car) {
        const render::Mat4 body = world_->visual(0).body_matrix(car);
        tv_valid_ = replay_camera_.pose(replay_script_, car, body, tv_pose_);
        if (const float d = replay_camera_.cut_dissolve(); d > 0) begin_dissolve(int(std::lround(d * 60)));
        step_dissolve();
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

    // The camera now: the opening shots, the replay cameras, or the car's view.
    race::ViewPose current_pose(std::span<const race::CarState> cars, bool look_back) {
        const race::CarState& me = cars[0];
        const render::Mat4 body = world_->visual(0).body_matrix(me);
        race::ViewPose pose;
        const int view = replaying_ || (!menu_scene_ && opening_tv()) ? kTvView : mount_;
        bool posed = menu_scene_ && scenic_.pose(pose);
        posed = posed || (start_camera_.active() && start_camera_.pose(pose));
        // The replay cameras' shot for the car; the chase view where the script has none.
        if (!posed && view == kTvView && tv_valid_) pose = tv_pose_, posed = true;
        if (!posed) pose = camera_.pose(me, body, world_->visual(0).mounts(), view == kTvView ? race::DriveCamera::kChase : view, look_back || setup_.look_back);
        return pose;
    }

    void quiet() { world_->quiet(); }
    void draw_icons(Ui& ui, const SDL_FRect& canvas, std::span<const RaceWorld::Icon> icons) { world_->draw_icons(ui, canvas, icons); }
    audio::RaceSound* sound() { return world_->sound(); }

    const race::CarState& player() const { return shown()[0]; }
    const std::string& car_name(size_t i) const { return world_->car_name(i); }
    const std::string& car_short_name(size_t i) const { return world_->car_short_name(i); }
    const WorldEntry& entry(size_t i) const { return world_->setup().entries[i]; }
    size_t car_count() const { return world_->car_count(); }
    const race::CourseScene& course() const { return world_->course(); }
    int mount() const { return mount_; }
    bool opening() const { return start_camera_.active(); }
    bool menu_scene() const { return menu_scene_; }


private:
    Setup setup_;
    std::unique_ptr<RaceWorld> world_;
    std::vector<race::CarInput> inputs_;
    gt3r::race::ControlMapper mapper_;
    race::DriveCamera camera_;
    race::ReplayScript replay_script_;
    race::ReplayCamera replay_camera_;
    race::ViewPose tv_pose_;
    bool tv_valid_ = false, snapshot_ = false;
    // The last step but one, for frames shown between steps.
    std::vector<race::CarState> prev_cars_, blended_;
    race::DriveCamera camera_prev_;
    race::StartCamera start_prev_;
    race::ViewPose tv_prev_;
    bool tv_prev_valid_ = false;
    float blend_ = 1;
    int dissolve_total_ = 0, dissolve_left_ = 0;
    race::CameraMotion start_motion_, replay_motion_;
    race::StartCamera start_camera_;
    race::RaceFlow flow_;
    race::ResultBoard board_;
    float distance_ = 0;
    bool rolled_ = false; // a rolling start's music was started
    int tire_index_ = 4;   // the race's tires (the scripts' index: 4 sports medium)
    std::string race_word_ = "ARCADE_SINGLE_RACE"; // the race's name in the race's words
    bool throttle_held_ = false;
    bool stop_area_ = false, in_box_ = false, license_failed_ = false; // a license test's stopping box
    int bspec_pace_ = 3, overtake_steps_ = 0;
    bool bspec_help_ = true;
    float lateral_g_ = 0;
    course::Vec3 last_v_;
    std::string lap_words_[2] = {"Lap", "Laps"};
    race::ReplayScript scenic_script_;
    race::ScenicCamera scenic_;
    std::vector<std::vector<race::CarState>> history_;
    bool replaying_ = false, replay_done_ = false, over_ = false, ran_ = false, menu_scene_ = false, watched_ = false;
    size_t replay_at_ = 0;
    int event_steps_ = -1, event_lap_ = 0, beeps_ = 0;
    int mount_ = race::DriveCamera::kChase;
};

class RaceScreen : public Screen {
public:
    explicit RaceScreen(App& app, const RaceStart* chosen = nullptr) : setup_(chosen ? Setup{} : setup_from_env()) {
        if (chosen) {
            setup_.course = chosen->course, setup_.car = chosen->car;
            setup_.paint = u32(std::max(0, chosen->paint));
            setup_.cars = std::clamp(chosen->cars, 1, 16), setup_.laps = std::clamp(chosen->laps, 1, 99);
            setup_.scripted = true;
            from_menus_ = true;
            menus_ = chosen->menus, sequence_ = chosen->sequence;
            setup_.sequence = chosen->sequence;
            if (menus_) setup_.race = menus_->game().race.race;
            if (menus_) {
                const menu::RaceRequest& q = menus_->game().race;
                setup_.options = menus_->game().options;
                pictures_from_options(setup_.options);
                // The garage car's own parts and settings (the sheet in use).
                if (q.garage_slot >= 0) setup_.player_equipment = menu::race_equipment(menus_->game(), q.garage_slot);
                setup_.playlist = playlist_from(menus_->game().bgm_playlist);
                // The Replay Theater's Play: the saved replay, then the menus again.
                if (sequence_ == "replay") setup_.replay_file = q.replay_file.empty() ? menus_->game().replay_file : q.replay_file;
                setup_.opponents.clear();
                draw_grid(app, setup_, q);
                // A request without laps: the event's (its RACE row), else the arcade's option
                // (race_laps, a new game's 2).
                if (q.laps <= 0) {
                    int laps = 0;
                    if (db::SpecDb* sdb = app.db(); sdb && !q.race.empty()) {
                        gt3r::Vfs* vfs = app.vfs();
                        try {
                            const db::RaceEvents events(*sdb, [vfs](const std::string& path) { return vfs->read(path); });
                            if (const std::optional<db::RaceEvent> e = events.event(q.race)) laps = e->laps;
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
            setup_.course_start = true;
            // A reversed layout is its own course file (crs/r_<name>); one the disc does not have is
            // driven forward.
            if (!app.vfs()->exists("crs/" + setup_.course) && setup_.course.rfind("r_", 0) == 0) {
                LOG_WARN("Drive: the disc has no reversed layout crs/%s: the course is driven forward", setup_.course.c_str());
                setup_.course = setup_.course.substr(2);
            }
        }
        // The sequences that run the player's car alone (RaceFreeRun, the tests); the free run's race
        // code is "freerun" (0x116E20).
        if (setup_.sequence == "time_trial" || setup_.sequence == "freerun" || is_license(setup_.sequence) || setup_.sequence == "machinetest" ||
            setup_.sequence == "photo_drive")
            setup_.cars = 1, setup_.opponents.clear();
        if (setup_.sequence == "freerun" && setup_.race.empty()) setup_.race = "freerun";
        // A race of the menus (or `start=course`): the start the course's start code gives, rolling
        // at the program's speed, else ❓ a standing start of 10 s with the opening shots (the race
        // information's own type is not traced to its source).
        if (!setup_.start_given && !setup_.sequence.empty()) setup_.course_start = true;
        if (db::SpecDb* sdb = app.db(); sdb && setup_.course_start) {
            const race::CourseStart cs = race::course_start(*sdb, setup_.course);
            setup_.start = cs.rolling ? race::StartType::Rolling : race::StartType::Ten;
            setup_.start_speed = cs.speed();
        }
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
            if (Disc* disc = app.disc()) config.program = [disc] { return disc->read_program(); };
            if (Disc* disc = app.disc(); disc && disc->info().serial.rfind("SCPS", 0) == 0) config.region = "jp"; // (Prologue)
            config.data_dir = app.data_dir();
            own_menus_ = std::make_unique<menu::Runtime>([vfs](const std::string& path) { return vfs->read(path); }, config);
            own_menus_->boot();
            menus_ = own_menus_.get(), sequence_ = setup_.sequence;
            setup_.options = menus_->game().options;
            setup_.playlist = playlist_from(menus_->game().bgm_playlist);
            menu::RaceRequest& q = menus_->game().race;
            q.course = setup_.course, q.car = setup_.car, q.paint = int(setup_.paint), q.laps = setup_.laps, q.max_entry = setup_.cars;
            q.race = setup_.race, q.sequence = setup_.sequence;
            q.assist_tcs = 1, q.assist_asm = 1, q.tire = 4;
            // An event's grid from its pool (`race=<label>`, no `opponents`).
            if (!setup_.race.empty() && setup_.opponents.empty() && setup_.cars > 1) draw_grid(app, setup_, q);
        }
        // A saved replay: its course and cars, then its steps played.
        std::optional<race::ReplayFile> saved;
        if (!setup_.replay_file.empty()) {
            try {
                std::FILE* fp = std::fopen(setup_.replay_file.c_str(), "rb");
                if (!fp) throw FormatError("cannot open " + setup_.replay_file);
                Bytes b;
                u8 buf[65536];
                for (size_t n; (n = std::fread(buf, 1, sizeof buf, fp)) > 0;) b.insert(b.end(), buf, buf + n);
                std::fclose(fp);
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
            } catch (const std::exception& e) {
                LOG_WARN("Drive: the saved replay: %s", e.what());
            }
        }
        settings_ = menu::settings_from(setup_.options);
        // The first view of a race of the menus: the options' (view_type: SUBJECTIVE the nose,
        // BONNET the higher one, OVERLOOK the chase view).
        if (from_menus_) setup_.view = settings_.view == "OVERLOOK" ? race::DriveCamera::kChase : settings_.view == "BONNET" ? race::DriveCamera::kRoof : race::DriveCamera::kInCar;
        // A race without the menus: the races' music by the play list a new game has (the
        // configuration script's, run by menus booted for it).
        if (!setup_.playlist) setup_.playlist = new_game_playlist(app);
        if (setup_.scripted) start(app);
        if (saved && race_) race_->play_saved(std::move(*saved));
        if (setup_.save_replay && race_ && race_->has_run()) save_replay(app);
        if (menus_ && sequence_ == "replay") {
            // A saved replay: no race menus; the menus again at its end (or at once without it).
            if (!saved_replay_ || !race_) leave_to_menus(app);
        } else if (menus_ && race_) {
            painter_ = make_menu_painter(app);
            enter_quick(0);
        } else if (menus_) {
            leave_to_menus(app);
        }
    }
    ~RaceScreen() override { report(); }

    void update(App& app, double dt) override {
        // (The time from the last frame's drawing to this update: its present and the events.)
        if (render_end_ns_) outside_ns_ = SDL_GetTicksNS() - render_end_ns_;
        const MenuInput& m = app.menu();
        if (quick_ && race_) return update_quick(app, m, dt);
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
            if (m.pressed(PadButton::Cross)) {
                setup_.course = courses_[size_t(course_sel_)].label;
                setup_.car = cars_[size_t(car_sel_)].label;
                start(app);
            }
            return;
        }
        // A replay: Start (or Cross) ends it; it has no pause.
        if (race_->replaying()) {
            if (m.pressed(PadButton::Start) || m.pressed(PadButton::Cross)) race_->stop_replay();
            if (race_->replay_done()) return finish_run(app);
        } else if (results_shown()) {
            // The results page (the game's results movie, else a plain page): leaving it ends the run.
            if (!result_ && !result_tried_) open_results(app);
            bool leave = false;
            if (result_) {
                race_->board().follow(race_->flow());
                if (m.vertical()) result_->key(m.vertical() < 0 ? 0x26 : 0x28);
                if (m.pressed(PadButton::Cross)) result_->key(0x20);
                result_->advance(float(dt));
                leave = result_exit_;
            } else {
                leave = m.pressed(PadButton::Cross);
            }
            if (leave && is_license(sequence_) && license_retry_) {
                // RETRY: the result given, the test again at once (end code 4).
                license_retry_ = false;
                result_.reset(), result_tried_ = false;
                report_result();
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
        if (paused_) return update_pause(app, m, dt);
        const gt3r::PadState& pad = app.input().race_pad(0);
        if (race_->watched()) {
            // B-spec's keys (the help strip's): Left / Right the pace, Triangle overtake, Cross the view,
            // R2 the display. (Circle's pit, Up / Down's car and R1's race monitor are not built.)
            if (m.horizontal()) race_->bspec_pace(m.horizontal());
            if (m.pressed(PadButton::Triangle)) race_->bspec_overtake();
            if (m.pressed(PadButton::Cross)) race_->next_view();
            if (m.pressed(PadButton::R2)) race_->bspec_display();
        } else if (m.pressed(PadButton::R1) || m.pressed(PadButton::Select)) race_->next_view();
        look_back_ = gt3r::race::look_back_held(pad, false);
        // Whole simulation steps for the time that has passed (at most four a frame).
        const Uint64 t0 = SDL_GetTicksNS();
        // Whole simulation steps for the time the shown frames advance (paced by the display's
        // refresh: the shared engine's pacer), the frame drawn between the last two.
        clock_ += pacer_.step(dt);
        int n = 0;
        for (; clock_ >= kStep && n < 4; ++n) clock_ -= kStep, race_->step(pad, setup_.auto_drive || spectator_);
        if (n == 4) clock_ = 0;
        race_->set_blend(float(clock_ / kStep));
        step_ns_ += SDL_GetTicksNS() - t0;
    }

    void render(App& app, Ui& ui) override {
        if (!race_) {
            draw_lists(ui);
            return;
        }
        const Uint64 t0 = SDL_GetTicksNS();
        if (license_page()) {
            // The test's results page: the race in its live inset (the movie's picture, 300 x 225 at
            // its scale; it starts over the whole picture and shrinks into place) on black.
            ui.fill(0, 0, Ui::kWidth, Ui::kHeight, Color{0, 0, 0, 255});
            const SDL_FRect c = RaceHud::canvas();
            const float k = c.h / 480.0f, w = 300 * inset_.scale * k, h = 225 * inset_.scale * k;
            race_->render(ui, {c.x + inset_.x * k - w / 2, c.y + inset_.y * k - h / 2, w, h}, false);
        } else {
            race_->render(ui, {0, 0, float(Ui::kWidth), float(Ui::kHeight)}, look_back_);
        }
        if (quick_) {
            // The race's menus over the scene.
            const float scale = float(Ui::kHeight) / 480.0f, x = (Ui::kWidth - 640 * scale) / 2;
            if (painter_) painter_->draw(*menus_, x, scale);
            if (const float cover = menus_->fade_cover(); cover > 0) ui.fill(0, 0, Ui::kWidth, Ui::kHeight, Color{0, 0, 0, u8(std::clamp(cover, 0.0f, 1.0f) * 255.0f)});
            last_frame_ns_ = 0; // (the race's frame times count the race's frames only)
            return;
        }
        // The results page covers the race display (the program hides it under the page).
        if (setup_.hud && !results_shown()) {
            if (hud_ && hud_->ready()) {
                race_->fill(hud_state_, *hud_);
                // The display's parts the options switch off; a replay's display as they say.
                const menu::GameSettings::Display& d = settings_.display;
                hud_state_.show_map = d.map, hud_state_.show_time = d.time, hud_state_.show_lap = d.lap;
                hud_state_.show_position = d.position, hud_state_.show_meter = d.meter;
                hud_state_.replay_display = settings_.replay_display;
                // A license test: the targets (the page's gold, silver and bronze times).
                hud_state_.license = is_license(sequence_);
                for (int k = 0; k < 3; ++k) hud_state_.targets[k] = test_time(k);
                if (race_->replaying()) {
                    if (caption_for_.empty()) music_caption(app);
                    hud_state_.caption_title = caption_title_, hud_state_.caption_artist = caption_artist_;
                } else {
                    caption_for_.clear();
                }
                hud_->draw(ui, hud_state_);
            } else if (prologue_hud_ && hud_) {
                race_->fill(hud_state_, *hud_);
                prologue_hud_->draw(ui, hud_state_, prologue_extra()); // (Prologue's: Gran Turismo 3's display)
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
                for (const RaceHud::MovieExternal& e : ext) {
                    if (e.name == "_EXT") inset_ = e; // the license page's live inset
                    if (e.name.rfind("_EXT_CARICON", 0) == 0) {
                        const int row = std::atoi(e.name.c_str() + 12) - 1;
                        if (row >= 0 && size_t(row) < rows.size() && rows[size_t(row)].car >= 0) icons.push_back({size_t(rows[size_t(row)].car), e.x, e.y, e.opacity});
                    }
                }
                race_->draw_icons(ui, RaceHud::canvas(), icons);
            }
            else draw_results(ui);
        }
        if (paused_) {
            if (pause_ && hud_) {
                hud_->draw_movie(*pause_movie_, *pause_);
            } else {
                ui.fill(0, 0, Ui::kWidth, Ui::kHeight, Color{0, 0, 0, 120});
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
    // ---- The pause: the game's pause movie (menu/common/pause.strb) and its entries.

    void open_pause(App& app) {
        paused_ = true;
        pause_result_.clear();
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

    void update_pause(App& app, const MenuInput& m, double dt) {
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
    void close_pause(const std::string& result) {
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
    void enter_quick(int can_replay, bool refill = true) {
        race_->stand();
        quick_ = true, option_ = false, spectator_ = false, paused_ = false;
        if (refill) fill_quick_work(can_replay);
        else menu::quick_work(menus_->game()).set("canReplay", can_replay);
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
        const std::vector<int> places = race::grid_places(n, 0);
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
        }
        if (sequence_ != "single_race") {
            w.set("IsASpec", 1); // ❓ the license the event asks for is not checked
            w.set("prize", 0);
            w.set("IsSessionFinished", 0);
            w.set("CourseLength", s32(race_->lap_length()));
            return;
        }
        // ❓ The Spectator button's rule (the course's condition and its record's byte +0x14) and
        // the dry-course rule are not read from the course here: both answer yes.
        w.set("IsBSpec", 1);
        w.set("IsDryCourse", 1);
        w.set("QuickTunePowerLevel", 0), w.set("QuickTuneWeightLevel", 0);
        w.set("QuickTuneFrontTireType", q.tire), w.set("QuickTuneRearTireType", q.tire);
        w.set("QuickTuneTransmission", q.manual ? 1 : 0);
        w.set("QuickTuneDrivingAssist", q.assist_asm ? 2 : q.assist_tcs ? 1 : 0);
        w.set("IsQuickTuned", 0);
    }

    // A run of the race: driven (mode 0), watched (1, B-spec) or a demonstration (watched, no result).
    void start_run(bool watched, bool demo = false) {
        quick_ = false;
        spectator_ = watched;
        demo_ = demo;
        reported_ = false;
        race_->begin_run(watched);
        clock_ = 0;
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
        r.time_ms = r.finished ? u32(race::clock_ms(flow.car(0).finish_units)) : 0;
        r.distance_m = race_->player_distance();
        if (is_license(sequence_)) {
            r.kind = menu::RaceResult::Kind::License;
            // ❓ The test's grading: the time against the page's gold, silver and bronze times.
            const int level = prize_level();
            r.result = level == 0 ? "gold" : level == 1 ? "silver" : level == 2 ? "bronze" : "failed";
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
        // Every car in finishing order.
        for (const race::RaceFlow::ResultRow& row : flow.results()) {
            menu::RaceResult::Finisher f;
            f.entry = row.car == 0 ? 0 : race_->entry(row.car).grid_place + 1;
            f.car = race_->entry(row.car).label, f.paint = int(race_->entry(row.car).paint);
            f.finished = row.finished;
            if (row.finished) f.time_ms = u32(row.total_ms);
            r.order.push_back(std::move(f));
        }
        if (sequence_ == "single_race") {
            // (The single race keeps its course record only: 0x111E98.)
        } else {
            try {
                const s64 prize = menus_->race_finished(r);
                if (session_kind()) w.set("prize", s32(std::clamp<s64>(prize, 0, 0x7FFFFFFF)));
            } catch (const std::exception& e) {
                LOG_WARN("Race result: %s", e.what());
            }
        }
        if (sequence_ == "time_trial" || sequence_ == "freerun" || sequence_ == "machinetest" || is_license(sequence_)) {
            const s64 best = flow.car(0).best_lap_ms;
            const s32 have = w.get("BestTime");
            if (best > 0 && (have <= 0 || have == menu::QuickWork::kNoTime || best < have)) w.set("BestTime", s32(best));
        }
        if (session_kind()) {
            // The rows in finishing order with their times; a run the player did not finish: no times.
            const std::vector<race::RaceFlow::ResultRow> rows = flow.results();
            w.rows.assign(rows.size(), menu::QuickRow{});
            for (size_t i = 0; i < rows.size(); ++i) {
                menu::QuickRow& row = w.rows[i];
                row.car = race_->entry(rows[i].car).label, row.paint = int(race_->entry(rows[i].car).paint);
                row.time_ms = r.finished && rows[i].finished ? rows[i].total_ms : menu::QuickWork::kNoTime;
                if (rows[i].car == 0) w.set("playerGridNumber", s32(i));
            }
            w.set("IsSessionFinished", 1);
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
        if (!menus_->finished()) return;
        menu::QuickWork& w = menu::quick_work(menus_->game());
        if (option_) return enter_quick(w.get("canReplay"), false);
        // The command the scripts chose (the sequences' tables: docs/formats/race_flow.md).
        const std::string command = w.take_command();
        menus_->game().during_quick_menu = false;
        const bool session_over = session_kind() && w.get("IsSessionFinished") == 1;
        if ((command == "COMMAND_START" || command == "COMMAND_SPECTATOR") && session_over) {
            // A Gran Turismo mode race already run: the page as before a race, no run.
            enter_quick(0);
        } else if (command == "COMMAND_START") {
            start_run(false);
        } else if (command == "COMMAND_SPECTATOR") {
            start_run(true);
        } else if (command == "COMMAND_DEMONSTRATION") {
            start_run(true, true);
        } else if (command == "COMMAND_REPLAY" && race_->has_run()) {
            quick_ = false;
            race_->start_replay();
        } else if (command == "COMMAND_SAVE_REPLAY" && race_->has_run()) {
            // Save Replay (the program's labomode project with MODE_SAVE_FULL_REPLAY saves to the
            // memory card): the run into the data folder's replays, then the menus again.
            save_replay(app);
            enter_quick(w.get("canReplay"), !session_over);
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
        } else {
            return false;
        }
        g.race = q;
        // The race again on the new course (a test: with its own car), the entrants kept.
        report();
        setup_.course = q.course, setup_.race = q.race;
        if (q.laps > 0) setup_.laps = std::clamp(q.laps, 1, 99);
        if (sequence_ == "license") setup_.car = q.car, setup_.paint = u32(std::max(0, q.paint)), setup_.player_equipment.reset();
        if (!app.vfs()->exists("crs/" + setup_.course) && setup_.course.rfind("r_", 0) == 0) setup_.course = setup_.course.substr(2);
        if (db::SpecDb* sdb = app.db()) {
            const race::CourseStart cs = race::course_start(*sdb, setup_.course);
            setup_.start = cs.rolling ? race::StartType::Rolling : race::StartType::Ten;
            setup_.start_speed = cs.speed();
        }
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

    // The last run as a file of the data folder's `replays` (the Drive screen plays one with
    // GT4R_RACE="replay_file=<path>").
    void save_replay(App& app) {
        const std::string folder = app.data_dir() + "/replays";
        SDL_CreateDirectory(folder.c_str());
        char stamp[32];
        const std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
        const std::string path = folder + "/" + setup_.course + "-" + stamp + ".gt4rrep";
        const menu::RaceRequest* q = menus_ ? &menus_->game().race : nullptr;
        const Bytes b = race_->replay_file(sequence_, q ? q->race : std::string()).encode();
        if (std::FILE* fp = std::fopen(path.c_str(), "wb")) {
            std::fwrite(b.data(), 1, b.size(), fp);
            std::fclose(fp);
            last_saved_ = path;
            LOG_INFO("Drive: the replay saved to %s (%zu bytes)", path.c_str(), b.size());
        } else {
            LOG_WARN("Drive: cannot write %s", path.c_str());
        }
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

    // The test's title: "License B-1" from its race code (l0b0001: the class letters, the number;
    // ❓ the program's own title routine is not read).
    std::string license_title() const {
        const std::string code = menus_ ? menus_->game().race.race : std::string();
        if (code.size() < 7) return "License";
        std::string cls;
        for (size_t i = 2; i < code.size() && std::isalpha(static_cast<unsigned char>(code[i])); ++i) cls += char(std::toupper(static_cast<unsigned char>(code[i])));
        if (cls.size() > 1 && cls.back() != 'S') cls = cls.substr(1); // "0B" / "IB" -> B, IB
        return "License " + cls + "-" + std::to_string(std::atoi(code.c_str() + code.size() - 4));
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
        const s64 t = race::clock_ms(f.car(0).finish_units);
        (void)w;
        const s64 g = test_time(0), sv = test_time(1), b = test_time(2);
        auto known = [](s64 v) { return v > 0; };
        if (!known(b)) return 3;
        return known(g) && t <= g ? 0 : known(sv) && t <= sv ? 1 : t <= b ? 2 : 3;
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
    bool results_shown() const { return race_ && !race_->replaying() && !race_->over() && race_->flow().player_finished(); }

    // The results movie (menu/common/result.strb) with the race's rows: its callbacks answer from the
    // race's results (docs/formats/strobe.md "The results movie").
    void open_results(App& app) {
        result_tried_ = true, result_exit_ = false;
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
            // ResultLicense (0x3E1BF8: result_license.strb): the grade, the time and its gap, the
            // test's title, RETRY / EXIT.
            auto lhost = [this, &app](const std::string& name, const std::vector<race::StrobePlayer::Value>& args) {
                using V = race::StrobePlayer::Value;
                std::string n = name;
                for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
                const race::RaceFlow& f = race_->flow();
                const s64 t = f.player_finished() ? race::clock_ms(f.car(0).finish_units) : f.time_ms();
                auto seconds = [](s64 ms, bool sign) {
                    char b[32];
                    std::snprintf(b, sizeof b, "%s%lld.%03lld", sign ? (ms < 0 ? "-" : "+") : "", (long long)(std::llabs(ms) / 1000), (long long)(std::llabs(ms) % 1000));
                    return std::string(b);
                };
                if (n == "PRIZELEVEL") return V::number(prize_level());
                if (n == "GETTIME") return V::string(seconds(t, false));
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
        const float w = 360, row = 16, h = 40 + row * float(rows.size()), x = (Ui::kWidth - w) / 2, y = (Ui::kHeight - h) / 2;
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

    void start(App& app) {
        error_.clear();
        if (!app.gpu()) {
            error_ = "3D drawing is not available on this system.";
            return;
        }
        try {
            race_ = std::make_unique<Race>(app, setup_, cars_);
            if (setup_.bspec) spectator_ = true;
            hud_ = std::make_unique<RaceHud>(app, race_->course().section(course::kSlotMiniMap));
            prologue_hud_.reset();
            if (!hud_->ready()) { // (Prologue's display: Gran Turismo 3's on its own pictures and rows)
                auto p = std::make_unique<PrologueHud>(app, race_->course().section(course::kSlotMiniMap));
                if (p->ready()) prologue_hud_ = std::move(p);
            }
            race_->set_lap_words(hud_->word("WordOfLap"), hud_->word("WordOfLaps"));
            // A test's gold, silver and bronze times (its RACE row, under the region's prefix).
            targets_[0] = targets_[1] = targets_[2] = -1;
            if (is_license(sequence_) || sequence_ == "mission")
                if (db::SpecDb* sdb = app.db()) try {
                        gt3r::Vfs* vfs = app.vfs();
                        const db::RaceEvents events(*sdb, [vfs](const std::string& path) { return vfs->read(path); });
                        const std::string code = menus_ ? menus_->game().race.race : setup_.race;
                        for (const std::string& label : {code, "us" + code, "jp" + code})
                            if (const std::optional<db::RaceEvent> e = events.event(label)) {
                                for (int k = 0; k < 3; ++k) targets_[k] = e->target_ms[size_t(k)] > 0 ? s64(e->target_ms[size_t(k)]) : -1;
                                break;
                            }
                    } catch (const std::exception& e) {
                        LOG_WARN("Drive: the test's times: %s", e.what());
                    }
            // The options' speed unit (unit_velocity), when the race came with the game's options.
            if (!setup_.options.empty()) hud_->set_mph(std::string(settings_.velocity.name).find("mph") != std::string::npos);
            if (const std::optional<Bytes> logo = app.vfs()->read("piece/crs_logo/" + setup_.course + ".img")) hud_->set_course_logo(*logo);
            // The replay card's tires and race name.
            const std::string& seq = setup_.sequence;
            race_->set_card(menus_ ? menus_->game().race.tire : 4,
                            seq == "time_trial" ? "ARCADE_TIME_TRIAL" : seq == "solo_event" || seq == "championship" ? "GTMODE_EVENT_RACE" : "ARCADE_SINGLE_RACE");
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
        auto list = [&](float x, float w, int n, int sel, bool active, const std::function<std::string(int)>& label, const char* title) {
            ui.text(x, top - 12, title, active ? colors::kAccent : colors::kDim);
            const int first = std::clamp(sel - rows / 2, 0, std::max(0, n - rows));
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
        if (is_license(setup_.sequence)) e.run = gt3r::race::HudRun::License;
        else if (setup_.sequence == "time_trial") e.run = gt3r::race::HudRun::TimeTrial;
        else if (setup_.sequence == "freerun") e.run = gt3r::race::HudRun::FreeRun;
        e.live = !h.replay;
        if (!h.message.empty() && h.message_opacity > 0) {
            const bool digits = std::all_of(h.message.begin(), h.message.end(), [](char c) { return c >= '0' && c <= '9'; });
            if (digits) e.countdown = std::atoi(h.message.c_str());
            else if (!race_->flow().player_finished()) e.countdown = 0;
        }
        if (race_->flow().player_finished()) e.finish_t = double(race_->flow().steps_since_player_finish()) / 60.0;
        if (menus_ && e.run == gt3r::race::HudRun::License) {
            // The test's gold, silver and bronze times (the race's menus' work).
            const menu::QuickWork& w = menu::quick_work(menus_->game());
            const char* names[3] = {"goldTime", "silverTime", "bronzeTime"};
            for (int i = 0; i < 3; ++i)
                if (const s32 t = w.get(names[i]); t > 0 && t != menu::QuickWork::kNoTime) e.license_targets[size_t(i)] = double(t) / 1000.0;
        }
        return e;
    }

    // A plain read-out until the game's own race display is drawn.
    void draw_overlay(Ui& ui) {
        const race::CarState& s = race_->player();
        char line[160];
        std::snprintf(line, sizeof line, "%3.0f km/h   gear %s   %4.0f rpm   lap %d", std::fabs(s.speed) * 3.6f,
                      s.gear < 0 ? "R" : s.gear == 0 ? "N" : std::to_string(s.gear).c_str(), s.engine_rpm, s.lap + 1);
        ui.fill(0, Ui::kHeight - 16, Ui::kWidth, 16, Color{10, 11, 14, 150});
        ui.text(8, Ui::kHeight - 13, line, colors::kText);
        ui.text_right(Ui::kWidth - 8, Ui::kHeight - 13, race_->car_name(0), colors::kDim);
    }

    Setup setup_;
    std::vector<db::CarEntry> cars_;
    std::vector<db::CourseEntry> courses_;
    int course_sel_ = 0, car_sel_ = 0, column_ = 0;
    std::unique_ptr<Race> race_;
    std::unique_ptr<RaceHud> hud_;
    std::unique_ptr<PrologueHud> prologue_hud_; // (Prologue's display, when the disc's is not Gran Turismo 4's)
    HudState hud_state_;
    std::string error_;
    bool paused_ = false, look_back_ = false, from_menus_ = false;
    std::shared_ptr<const race::StrobeMovie> pause_movie_;
    std::unique_ptr<race::StrobePlayer> pause_;
    std::string pause_result_;
    bool pause_exit_ = false, pause_retry_ = false;
    bool saved_replay_ = false; // the screen plays a saved replay (then leaves)
    std::string last_saved_;
    menu::GameSettings settings_; // the game's options as the race reads them
    s64 targets_[3] = {-1, -1, -1}; // a test's gold, silver and bronze times from its event (ms)
    std::string caption_for_, caption_title_, caption_artist_; // the replay's music caption (for the piece caption_for_)
    bool demo_ = false, reported_ = false; // the run is a demonstration; its result was given
    int last_can_replay_ = 0;
    std::shared_ptr<const race::StrobeMovie> result_movie_;
    std::unique_ptr<race::StrobePlayer> result_;
    bool result_tried_ = false, result_exit_ = false;
    std::shared_ptr<const race::StrobeMovie> license_movie_;
    bool license_retry_ = false;
    RaceHud::MovieExternal inset_{"_EXT", 320, 240, 640.0f / 300.0f, 1};
    menu::Runtime* menus_ = nullptr;    // the race's menus run on these (from the menus that asked)
    std::unique_ptr<menu::Runtime> own_menus_; // a scripted run's
    std::string sequence_;              // the race sequence ("single_race")
    std::unique_ptr<MenuPainter> painter_;
    bool quick_ = false, option_ = false, spectator_ = false;
    double clock_ = 0;
    gt3r::race::FramePacer pacer_;
    Uint64 step_ns_ = 0, last_frame_ns_ = 0, render_end_ns_ = 0, outside_ns_ = 0;
    std::vector<float> work_ms_, interval_ms_;
};

} // namespace

std::unique_ptr<Screen> make_race_screen(App& app) { return std::make_unique<RaceScreen>(app); }
std::unique_ptr<Screen> make_race_screen(App& app, const RaceStart& start) { return std::make_unique<RaceScreen>(app, &start); }

} // namespace gt4r
