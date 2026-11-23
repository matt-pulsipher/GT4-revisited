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
// N steps unseen and plays them back through the replay cameras; `menus=single_race` the race
// sequence's own menus (quick-arcade) before and after the race.

#include "gt4r/app/app.h"
#include "gt4r/app/menu_painter.h"
#include "gt4r/app/race_hud.h"
#include "gt4r/app/race_world.h"
#include "gt4r/audio/app_sound.h"
#include "gt4r/audio/race_sound.h"

#include "base/log.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/race/drive_camera.h"
#include "gt4r/race/race_flow.h"
#include "gt4r/race/rail_simulation.h"
#include "gt4r/race/replay_script.h"
#include "gt4r/race/start_camera.h"
#include "gt4r/race/start_rule.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/menu/game.h"
#include "gt4r/menu/race_natives.h"
#include "gt4r/menu/runtime.h"
#include "race/controls.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace gt4r {

namespace {

namespace render = gt3r::render;
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
    sim::ai::Pace pace;   // the computer drivers' (the event's)
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
        else if (k == "skip") s.skip = std::max(0, std::atoi(v.c_str()));
        else if (k == "paint") s.paint = u32(std::max(0, std::atoi(v.c_str())));
        else if (k == "hud") s.hud = v != "0";
        else if (k == "replay") s.replay = std::max(0, std::atoi(v.c_str()));
        else if (k == "smoke") s.smoke = v != "0";
        else if (k == "reflection") s.reflection = v != "0";
        else if (k == "sim") s.rail = v == "rail";
        else if (k == "laps") s.laps = std::clamp(std::atoi(v.c_str()), 1, 99);
        else if (k == "menus") s.sequence = v;
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
        ws.computer_player = setup.auto_drive;
        ws.pace = setup.pace;
        // The grid: the player at the back, the others ahead of him in entry order.
        const std::vector<int> places = race::grid_places(size_t(setup.cars), 0);
        for (int i = 0; i < setup.cars; ++i) {
            WorldEntry e;
            e.label = i == 0 || setup.opponents.empty() ? cars[(first + size_t(i) * 37) % cars.size()].label
                                                         : setup.opponents[size_t(i - 1) % setup.opponents.size()];
            e.paint = i == 0 ? setup.paint : u32(i);
            e.grid_place = places[size_t(i)];
            if (setup.start == race::StartType::Rolling) e.start_speed = setup.start_speed;
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
        mount_ = setup.view;
        begin_run();
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
        world_->set_computer_player(watched || setup_.auto_drive);
        if (ran_ || watched != watched_) world_->restart();
        watched_ = watched;
        ran_ = true;
        menu_scene_ = false;
        race::FlowSetup fs;
        fs.start = setup_.skip > 0 ? race::StartType::None : setup_.start;
        fs.start_seconds = setup_.start_seconds;
        fs.laps = setup_.laps;
        fs.player = 0;
        fs.lap_length = world_->line().length;
        flow_.begin(fs, world_->cars());
        board_.begin(world_->car_count(), 0, lap_words_[0], lap_words_[1]);
        camera_.reset(world_->cars()[0]);
        history_.clear();
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
        world_->quiet();
        world_->clear_effects();
        scenic_.reset(&scenic_script_);
        start_camera_.begin(nullptr, false, 0, {}, world_->cars()[0]);
    }
    bool has_run() const { return !history_.empty(); }

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
            scenic_.step();
            return;
        }
        if (replaying_) {
            // The recorded run again; it ends at its end.
            if (replay_at_ + 1 >= history_.size()) {
                replay_done_ = true;
                return;
            }
            ++replay_at_;
            start_camera_.step();
            camera_.step(history_[replay_at_][0], kStep);
            hear(history_[replay_at_], false);
            return;
        }
        const std::span<const race::CarState> cars = world_->cars();
        const bool hold = flow_.holding();
        auto_drive = auto_drive || flow_.player_finished();
        // The computer drivers (race_world.h) drive their cars; without them the simple driver follows
        // the road's line.
        const bool drivers = world_->computer_drivers();
        for (size_t c = 0; c < inputs_.size(); ++c)
            inputs_[c] = hold || (drivers && (c > 0 || watched_ || setup_.auto_drive)) ? race::CarInput{}
                                                                                       : race::follow_line(world_->line(), cars[c], c & 1 ? 2.0f : -2.0f, 1.0f - 0.05f * float(c));
        const gt3r::sim::ControlInputs in = mapper_.update(pad, kStep);
        if (!auto_drive) {
            race::CarInput& me = inputs_[0];
            me = {};
            me.steer = in.steer, me.throttle = in.throttle, me.brake = in.brake, me.handbrake = in.handbrake;
            me.shift_up = in.shift_up, me.shift_down = in.shift_down;
            me.reverse = mapper_.reverse();
            if (me.reverse) me.throttle = std::max(me.throttle, gt3r::race::pressure_to_pedal(pad.press(PadButton::Triangle)));
        }
        world_->step(inputs_, hold);
        const race::RaceFlow::Events ev = flow_.step(world_->cars());
        if (!ev.finished.empty()) board_.finished(flow_, ev.finished);
        if (audio::RaceSound* sound = world_->sound()) {
            if (hold) sound->count(flow_.hold_left());
            if (ev.player_finished) sound->finish();
        }
        if (ev.lap_done == 0) event_steps_ = 0, event_lap_ = flow_.car(0).laps_done;
        beeps_ += ev.beep;
        start_camera_.step();
        camera_.step(world_->cars()[0], kStep);
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
    void end_run() {
        over_ = true;
        if (flow_.car(0).counter >= 2) start_replay();
    }

    void start_replay() {
        if (history_.empty() || replaying_) return;
        replaying_ = true, replay_done_ = false;
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
    // The race's words for one lap and several (the results' "+1Lap").
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
        h.red_line = spec.red_line, h.rev_limit = spec.rev_limit;
        h.driver_view = mount_ != race::DriveCamera::kChase && mount_ != kTvView;
        h.cars.clear();
        for (const race::CarState& c : cars) h.cars.push_back({c.position.x, c.position.z});
        h.place = flow_.place(0);
        h.lap = flow_.lap_shown(0), h.laps = setup_.laps;
        h.lap_ms = replaying_ ? s64(replay_at_) * 1000 / 60 : flow_.lap_ms(0);
        h.best_ms = flow_.car(0).best_lap_ms, h.last_ms = flow_.car(0).last_lap_ms;
        h.time = double(flow_.time_ms()) / 1000;
        // A window of a line or message: full for `hold` seconds, then fading out.
        auto window = [](int steps, float hold, float fade) {
            const float t = float(steps) / 60;
            return t < 0 ? 0.0f : t < hold ? 1.0f : std::max(0.0f, 1 - (t - hold) / fade);
        };
        h.message.clear(), h.message_opacity = 0;
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
        h.event.clear(), h.event_opacity = 0;
        if (!replaying_ && event_steps_ >= 0 && event_steps_ < 200) {
            // "L1/3   1'23.456" for three seconds.
            h.event = hud.word("WordOfLapTime") + std::to_string(event_lap_) + "/" + std::to_string(setup_.laps) + "   " + race::format_lap_time(flow_.car(0).last_lap_ms);
            h.event_opacity = std::min(1.0f, float(event_steps_) / 9.0f) * window(event_steps_, 3.0f, 0.15f);
        }
    }

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

    void render(Ui& ui, const SDL_FRect& rect, bool look_back) {
        const std::span<const race::CarState> cars = shown();
        const race::ViewPose pose = current_pose(cars, look_back);
        world_->render(ui, rect, cars, pose, 0, pose.body_hidden ? 0 : -1);
    }

    // The camera now: the opening shots, the replay cameras, or the car's view.
    race::ViewPose current_pose(std::span<const race::CarState> cars, bool look_back) {
        const race::CarState& me = cars[0];
        const render::Mat4 body = world_->visual(0).body_matrix(me);
        race::ViewPose pose;
        const int view = replaying_ ? kTvView : mount_;
        bool posed = menu_scene_ && scenic_.pose(pose);
        posed = posed || (start_camera_.active() && start_camera_.pose(pose));
        // The replay cameras' shot for the car; the chase view where the script has none.
        if (!posed && view == kTvView) posed = replay_camera_.pose(replay_script_, me, body, pose);
        if (!posed) pose = camera_.pose(me, body, world_->visual(0).mounts(), view == kTvView ? race::DriveCamera::kChase : view, look_back || setup_.look_back);
        return pose;
    }

    void quiet() { world_->quiet(); }
    void draw_icons(Ui& ui, const SDL_FRect& canvas, std::span<const RaceWorld::Icon> icons) { world_->draw_icons(ui, canvas, icons); }
    audio::RaceSound* sound() { return world_->sound(); }

    const race::CarState& player() const { return shown()[0]; }
    const std::string& car_name(size_t i) const { return world_->car_name(i); }
    const WorldEntry& entry(size_t i) const { return world_->setup().entries[i]; }
    size_t car_count() const { return world_->car_count(); }
    const race::CourseScene& course() const { return world_->course(); }
    int mount() const { return mount_; }
    bool opening() const { return start_camera_.active(); }
    bool menu_scene() const { return menu_scene_; }
    float scene_dissolve() const { return menu_scene_ ? scenic_.dissolve() : 0.0f; }

private:
    Setup setup_;
    std::unique_ptr<RaceWorld> world_;
    std::vector<race::CarInput> inputs_;
    gt3r::race::ControlMapper mapper_;
    race::DriveCamera camera_;
    race::ReplayScript replay_script_;
    race::ReplayCamera replay_camera_;
    race::CameraMotion start_motion_, replay_motion_;
    race::StartCamera start_camera_;
    race::RaceFlow flow_;
    race::ResultBoard board_;
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
            if (menus_) {
                const menu::RaceRequest& q = menus_->game().race;
                setup_.opponents = q.entries;
                // The event's grid (the opponents drawn from its pool, the player last) and its pace.
                if (db::SpecDb* sdb = app.db()) {
                    try {
                        gt3r::Vfs* vfs = app.vfs();
                        const db::RaceEvents events(*sdb, [vfs](const std::string& path) { return vfs->read(path); });
                        const std::vector<sim::ai::GridEntry> grid = sim::ai::grid_for(events, q, u32(SDL_GetTicks()));
                        setup_.opponents.clear();
                        for (const sim::ai::GridEntry& g : grid)
                            if (!g.player) setup_.opponents.push_back(g.car);
                        if (!setup_.opponents.empty()) setup_.cars = int(setup_.opponents.size()) + 1;
                        if (const std::optional<db::RaceEvent> e = events.event(q.race)) setup_.pace = sim::ai::pace_for(*e);
                    } catch (const std::exception& e) {
                        LOG_WARN("Drive: the event's grid: %s", e.what());
                    }
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
            config.data_dir = app.data_dir();
            own_menus_ = std::make_unique<menu::Runtime>([vfs](const std::string& path) { return vfs->read(path); }, config);
            own_menus_->boot();
            menus_ = own_menus_.get(), sequence_ = setup_.sequence;
            menu::RaceRequest& q = menus_->game().race;
            q.course = setup_.course, q.car = setup_.car, q.paint = int(setup_.paint), q.laps = setup_.laps, q.max_entry = setup_.cars;
            q.assist_tcs = 1, q.assist_asm = 1, q.tire = 4;
        }
        if (setup_.scripted) start(app);
        if (menus_ && race_) {
            painter_ = make_menu_painter(app);
            enter_quick(0);
        } else if (menus_) {
            leave_to_menus(app);
        }
    }
    ~RaceScreen() override { report(); }

    void update(App& app, double dt) override {
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
            if (leave) {
                result_.reset();
                race_->end_run();
                if (!race_->replaying()) return finish_run(app);
            }
        } else if (m.pressed(PadButton::Start) && !paused_) {
            open_pause(app);
            return;
        }
        if (paused_) return update_pause(app, m, dt);
        const gt3r::PadState& pad = app.input().race_pad(0);
        if (m.pressed(PadButton::R1) || m.pressed(PadButton::Select)) race_->next_view();
        look_back_ = gt3r::race::look_back_held(pad, false);
        // Whole simulation steps for the time that has passed (at most four a frame).
        const Uint64 t0 = SDL_GetTicksNS();
        clock_ += dt;
        int n = 0;
        for (; clock_ >= kStep && n < 4; ++n) clock_ -= kStep, race_->step(pad, setup_.auto_drive || spectator_);
        if (n == 4) clock_ = 0;
        step_ns_ += SDL_GetTicksNS() - t0;
    }

    void render(App& app, Ui& ui) override {
        if (!race_) {
            draw_lists(ui);
            return;
        }
        const Uint64 t0 = SDL_GetTicksNS();
        race_->render(ui, {0, 0, float(Ui::kWidth), float(Ui::kHeight)}, look_back_);
        if (quick_) {
            // The race's menus over the scene; the scenic shots' cuts dissolve through black (❓ how
            // the program blends a dissolve is not read).
            if (const float d = race_->scene_dissolve(); d > 0) ui.fill(0, 0, Ui::kWidth, Ui::kHeight, Color{0, 0, 0, u8(std::clamp(d, 0.0f, 1.0f) * 255)});
            const float scale = float(Ui::kHeight) / 480.0f, x = (Ui::kWidth - 640 * scale) / 2;
            if (painter_) painter_->draw(*menus_, x, scale);
            if (const float cover = menus_->fade_cover(); cover > 0) ui.fill(0, 0, Ui::kWidth, Ui::kHeight, Color{0, 0, 0, u8(std::clamp(cover, 0.0f, 1.0f) * 255.0f)});
            return;
        }
        // The results page covers the race display (the program hides it under the page).
        if (setup_.hud && !results_shown()) {
            if (hud_ && hud_->ready()) {
                race_->fill(hud_state_, *hud_);
                hud_->draw(ui, hud_state_);
            } else {
                draw_overlay(ui);
            }
        }
        if (results_shown()) {
            if (result_ && hud_) {
                // The rows' car icons (_EXT_CARICON<row + 1>) over the page.
                std::vector<RaceHud::MovieExternal> ext;
                hud_->draw_movie(*result_movie_, *result_, &ext);
                std::vector<RaceWorld::Icon> icons;
                const auto& rows = race_->board().rows();
                for (const RaceHud::MovieExternal& e : ext)
                    if (e.name.rfind("_EXT_CARICON", 0) == 0) {
                        const int row = std::atoi(e.name.c_str() + 12) - 1;
                        if (row >= 0 && size_t(row) < rows.size() && rows[size_t(row)].car >= 0) icons.push_back({size_t(rows[size_t(row)].car), e.x, e.y, e.opacity});
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
        }
        last_frame_ns_ = now;
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
                if (const std::optional<Bytes> file = app.vfs()->read("menu/common/pause.strb"); file && race::StrobeMovie::is_movie(*file))
                    pause_movie_ = std::make_shared<const race::StrobeMovie>(race::StrobeMovie::parse(*file));
            } catch (const std::exception& e) {
                LOG_WARN("Pause: %s", e.what());
            }
        }
        if (!pause_movie_) return;
        // The single race's pause: Continue, Exit and the screen settings; no retry, no confirmation.
        auto host = [this, &app](const std::string& name, const std::vector<race::StrobePlayer::Value>& args) {
            using V = race::StrobePlayer::Value;
            std::string n = name;
            for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
            auto text = [&](size_t i) { return i < args.size() ? args[i].to_string() : std::string(); };
            if (n == "RETRYENABLE") return V::number(0);
            if (n == "SCREENSETTINGENABLE") return V::number(1);
            if (n == "CONFIRMEXIT") return V::number(0);
            if (n == "EXIT_MENU") pause_result_ = text(0);
            else if (n == "PLAY_SOUND") {
                if (audio::AppSound* sound = app.sound()) sound->menu().play(text(0));
            } else if (n == "NAVIGATEVERTICAL" && pause_) pause_->navigate(true, false);
            else if (n == "NAVIGATEALL" && pause_) pause_->navigate(true, true);
            else if (n == "DEFAULTVAL") return V::number(text(0) == "BRIGHTNESS" ? screen_[0] : screen_[1]);
            else if (n == "SETVAL" && args.size() >= 2) screen_[text(0) == "BRIGHTNESS" ? 0 : 1] = int(args[1].to_number());
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
        if (!paused_ && pause_exit_) {
            pause_exit_ = false;
            // Exit: the run ends (end code 2); the single race goes back to its menus.
            if (menus_) return enter_quick(spectator_ ? 2 : 1);
            report();
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
    }

    // ---- The race's menus (the sequence's quick-* project) over the standing race.

    // The menus before a run (can_replay 0) or after one (1 driven, 2 watched).
    void enter_quick(int can_replay) {
        race_->stand();
        quick_ = true, option_ = false, spectator_ = false, paused_ = false;
        fill_quick_work(can_replay);
        menus_->game().during_quick_menu = true;
        menus_->start_project(menu::quick_project(sequence_));
    }

    // MQuickWork as the arcade sequence fills it before its menu (the program's 0x111A40, 0x111B50).
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
        // ❓ The Spectator button's rule (the course's condition and its record's byte +0x14) and
        // the dry-course rule are not read from the course here: both answer yes.
        w.set("IsBSpec", 1);
        w.set("IsDryCourse", 1);
        w.set("QuickTunePowerLevel", 0), w.set("QuickTuneWeightLevel", 0);
        w.set("QuickTuneFrontTireType", q.tire), w.set("QuickTuneRearTireType", q.tire);
        w.set("QuickTuneTransmission", q.manual ? 1 : 0);
        w.set("QuickTuneDrivingAssist", q.assist_asm ? 2 : q.assist_tcs ? 1 : 0);
        w.set("IsQuickTuned", 0);
        w.set("Laps", setup_.laps);
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
        if (option_) return enter_quick(menu::quick_work(menus_->game()).get("canReplay"));
        // The command the scripts chose (the arcade sequence's table).
        menu::QuickWork& w = menu::quick_work(menus_->game());
        const std::string command = w.take_command();
        menus_->game().during_quick_menu = false;
        if (command == "COMMAND_START" || command == "COMMAND_SPECTATOR") {
            quick_ = false;
            spectator_ = command == "COMMAND_SPECTATOR";
            race_->begin_run(spectator_);
            clock_ = 0;
        } else if (command == "COMMAND_REPLAY" && race_->has_run()) {
            quick_ = false;
            race_->start_replay();
        } else if (command == "COMMAND_OPTION") {
            option_ = true;
            menus_->start_project("option");
        } else if (command == "COMMAND_EXIT" || command == "COMMAND_NOTHING") {
            leave_to_menus(app);
        } else {
            // (Saving a replay is not done here: the menus again.)
            enter_quick(w.get("canReplay"));
        }
    }

    // The race sequence is over: the menus go on where the scripts said (their next project), as
    // after the program's sequence routine (last_sequence = the race's, next_sequence = menu).
    void leave_to_menus(App& app) {
        quick_ = false;
        if (menus_) {
            menu::GameState& g = menus_->game();
            g.during_quick_menu = false;
            g.next_sequence.clear();
            g.last_sequence = sequence_;
            if (g.next_menu_project.empty()) g.next_menu_project = g.major_menu_project;
            if (!menus_->finished()) menus_->finish();
        }
        report();
        app.pop();
    }

    // The results page goes up at the player's finish (ResultArcade::vf7).
    bool results_shown() const { return race_ && !race_->replaying() && !race_->over() && race_->flow().player_finished(); }

    // The results movie (menu/common/result.strb) with the race's rows: its callbacks answer from the
    // race's results (docs/formats/strobe.md "The results movie").
    void open_results(App& app) {
        result_tried_ = true, result_exit_ = false;
        if (!result_movie_) {
            try {
                if (const std::optional<Bytes> file = app.vfs()->read("menu/common/result.strb"); file && race::StrobeMovie::is_movie(*file))
                    result_movie_ = std::make_shared<const race::StrobeMovie>(race::StrobeMovie::parse(*file));
            } catch (const std::exception& e) {
                LOG_WARN("Results: %s", e.what());
            }
        }
        if (!result_movie_) return;
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
            if (n == "CB_GETNAME") return V::string(row && row->car >= 0 ? race_->car_name(size_t(row->car)) : std::string());
            if (n == "CB_GETTIME") return V::string(row ? row->time : std::string());
            if (n == "CB_ENABLEEXIT") return V::number(1);
            if (n == "CB_RACEEXIT") result_exit_ = true;
            if (n == "PLAY_SOUND")
                if (audio::AppSound* sound = app.sound()) sound->menu().play(args.empty() ? std::string() : args[0].to_string());
            if (n == "NAVIGATEALL" && result_) result_->navigate(true, true);
            return V::number(0); // isChampionship, isStage1, ...: no
        };
        result_ = std::make_unique<race::StrobePlayer>(result_movie_, host);
        result_->play(); // the page up: the movie plays on from its first frame
    }

    // After the run and its replay: the race's menus, back to the menus that asked for the race, or the
    // same race again.
    void finish_run(App& app) {
        result_tried_ = false, result_.reset();
        if (menus_) return enter_quick(spectator_ ? 2 : 1);
        if (from_menus_) {
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
            hud_ = std::make_unique<RaceHud>(app, race_->course().section(course::kSlotMiniMap));
            race_->set_lap_words(hud_->word("WordOfLap"), hud_->word("WordOfLaps"));
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
    HudState hud_state_;
    std::string error_;
    bool paused_ = false, look_back_ = false, from_menus_ = false;
    std::shared_ptr<const race::StrobeMovie> pause_movie_;
    std::unique_ptr<race::StrobePlayer> pause_;
    std::string pause_result_;
    bool pause_exit_ = false;
    std::shared_ptr<const race::StrobeMovie> result_movie_;
    std::unique_ptr<race::StrobePlayer> result_;
    bool result_tried_ = false, result_exit_ = false;
    int screen_[2] = {0, 0}; // the pause's screen settings: brightness, contrast (-5..5) ❓ not applied to the picture
    menu::Runtime* menus_ = nullptr;    // the race's menus run on these (from the menus that asked)
    std::unique_ptr<menu::Runtime> own_menus_; // a scripted run's
    std::string sequence_;              // the race sequence ("single_race")
    std::unique_ptr<MenuPainter> painter_;
    bool quick_ = false, option_ = false, spectator_ = false;
    double clock_ = 0;
    Uint64 step_ns_ = 0, last_frame_ns_ = 0;
    std::vector<float> work_ms_, interval_ms_;
};

} // namespace

std::unique_ptr<Screen> make_race_screen(App& app) { return std::make_unique<RaceScreen>(app); }
std::unique_ptr<Screen> make_race_screen(App& app, const RaceStart& start) { return std::make_unique<RaceScreen>(app, &start); }

} // namespace gt4r
