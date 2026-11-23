// Drive: pick a car and a course, then a race on the course's road: the first car driven with
// the pad, the others by a simple driver, all on the simulation behind race/simulation.h.
//
// GT4R_RACE="course=tsukuba,car=<label>,cars=6,view=chase,drive=auto,skip=600,paint=1,hud=1,sim=rail"
// sets it up without the lists, for pictures: `course` a course file's name, `car` a car's label
// (or its model's name), `cars` how many on the grid, `view` in | chase | roof | back, `drive`
// auto (the simple driver takes the first car too) or pad, `skip` simulation steps run before the
// first picture (the start's hold is left out then), `paint` the first car's paint, `laps` the
// race's length, `start` the start: 10 or 8 (a hold of so many seconds with the opening shots),
// <seconds> (a plain hold), none, rolling; `view=tv` the course's replay cameras; `replay=N` drives
// N steps unseen and plays them back through the replay cameras.

#include "gt4r/app/app.h"
#include "gt4r/app/race_hud.h"
#include "gt4r/app/race_world.h"
#include "gt4r/audio/race_sound.h"

#include "base/log.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/race/drive_camera.h"
#include "gt4r/race/race_flow.h"
#include "gt4r/race/rail_simulation.h"
#include "gt4r/race/replay_script.h"
#include "gt4r/race/start_camera.h"
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
    std::vector<std::string> opponents; // the other cars by label (empty: from the car list)
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
        else if (k == "start") {
            if (v == "rolling") s.start = race::StartType::Rolling;
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
        // The grid: the player at the back, the others ahead of him in entry order.
        const std::vector<int> places = race::grid_places(size_t(setup.cars), 0);
        for (int i = 0; i < setup.cars; ++i) {
            WorldEntry e;
            e.label = i == 0 || setup.opponents.empty() ? cars[(first + size_t(i) * 37) % cars.size()].label
                                                         : setup.opponents[size_t(i - 1) % setup.opponents.size()];
            e.paint = i == 0 ? setup.paint : u32(i);
            e.grid_place = places[size_t(i)];
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

    // The cars on the grid and the start's hold from the beginning.
    void begin_run() {
        if (ran_) world_->restart();
        ran_ = true;
        race::FlowSetup fs;
        fs.start = setup_.skip > 0 ? race::StartType::None : setup_.start;
        fs.start_seconds = setup_.start_seconds;
        fs.laps = setup_.laps;
        fs.player = 0;
        fs.lap_length = world_->line().length;
        flow_.begin(fs, world_->cars());
        camera_.reset(world_->cars()[0]);
        history_.clear();
        replaying_ = false, over_ = false;
        event_steps_ = -1;
        begin_start_camera(false);
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
        for (size_t c = 0; c < inputs_.size(); ++c)
            inputs_[c] = hold ? race::CarInput{} : race::follow_line(world_->line(), cars[c], c & 1 ? 2.0f : -2.0f, 1.0f - 0.05f * float(c));
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
        } else if (const int s = flow_.steps_since_start(); s >= 0 && flow_.setup().start != race::StartType::None) {
            // START at the start signal: 1.0 s and a fade of 0.15 s. (No count is written: the count is heard.)
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
        bool posed = start_camera_.active() && start_camera_.pose(pose);
        // The replay cameras' shot for the car; the chase view where the script has none.
        if (!posed && view == kTvView) posed = replay_camera_.pose(replay_script_, me, body, pose);
        if (!posed) pose = camera_.pose(me, body, world_->visual(0).mounts(), view == kTvView ? race::DriveCamera::kChase : view, look_back || setup_.look_back);
        return pose;
    }

    void quiet() { world_->quiet(); }
    audio::RaceSound* sound() { return world_->sound(); }

    const race::CarState& player() const { return shown()[0]; }
    const std::string& car_name(size_t i) const { return world_->car_name(i); }
    size_t car_count() const { return world_->car_count(); }
    const race::CourseScene& course() const { return world_->course(); }
    int mount() const { return mount_; }
    bool opening() const { return start_camera_.active(); }

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
    std::vector<std::vector<race::CarState>> history_;
    bool replaying_ = false, replay_done_ = false, over_ = false, ran_ = false;
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
        if (setup_.scripted) start(app);
    }
    ~RaceScreen() override { report(); }

    void update(App& app, double dt) override {
        const MenuInput& m = app.menu();
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
            // The results page: Cross leaves it and the run is over.
            if (m.pressed(PadButton::Cross)) {
                race_->end_run();
                if (!race_->replaying()) return finish_run(app);
            }
        } else if (m.pressed(PadButton::Start)) {
            paused_ = !paused_;
            race_->pause_sound(paused_);
        }
        if (paused_) {
            if (m.pressed(PadButton::Triangle)) {
                report();
                hud_.reset();
                race_.reset();
                paused_ = false;
                if (setup_.scripted) app.pop();
            }
            return;
        }
        const gt3r::PadState& pad = app.input().race_pad(0);
        if (m.pressed(PadButton::R1) || m.pressed(PadButton::Select)) race_->next_view();
        look_back_ = gt3r::race::look_back_held(pad, false);
        // Whole simulation steps for the time that has passed (at most four a frame).
        const Uint64 t0 = SDL_GetTicksNS();
        clock_ += dt;
        int n = 0;
        for (; clock_ >= kStep && n < 4; ++n) clock_ -= kStep, race_->step(pad, setup_.auto_drive);
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
        if (setup_.hud) {
            if (hud_ && hud_->ready()) {
                race_->fill(hud_state_, *hud_);
                hud_->draw(ui, hud_state_);
            } else {
                draw_overlay(ui);
            }
        }
        if (results_shown()) draw_results(ui);
        if (paused_) {
            ui.fill(0, 0, Ui::kWidth, Ui::kHeight, Color{0, 0, 0, 120});
            ui.text_center(Ui::kHeight / 2 - 10, "PAUSED", colors::kText);
            ui.text_center(Ui::kHeight / 2 + 6, Ui::expand_prompts("{Start}: go on   {Triangle}: leave the race"), colors::kDim);
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
    // The results page goes up once the player's FINISH has been shown.
    bool results_shown() const {
        return race_ && !race_->replaying() && !race_->over() && race_->flow().player_finished() && race_->flow().steps_since_player_finish() >= 4 * 60;
    }

    // After the run and its replay: back to the menus that asked for the race, else the same race again.
    void finish_run(App& app) {
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
            const std::string t = race::format_lap_time(r.finished ? r.total_ms : -1);
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
            work_ms_.clear(), interval_ms_.clear();
            last_frame_ns_ = 0, clock_ = 0;
        } catch (const std::exception& e) {
            error_ = e.what();
            LOG_WARN("Drive: %s", e.what());
        }
    }

    // The run's frame times in the log: the mean, the 99th percentile and the worst.
    void report() {
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
    double clock_ = 0;
    Uint64 step_ns_ = 0, last_frame_ns_ = 0;
    std::vector<float> work_ms_, interval_ms_;
};

} // namespace

std::unique_ptr<Screen> make_race_screen(App& app) { return std::make_unique<RaceScreen>(app); }
std::unique_ptr<Screen> make_race_screen(App& app, const RaceStart& start) { return std::make_unique<RaceScreen>(app, &start); }

} // namespace gt4r
