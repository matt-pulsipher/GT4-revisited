// Drive: pick a car and a course, then a race on the course's road: the first car driven with
// the pad, the others by a simple driver, all on the simulation behind race/simulation.h.
//
// GT4R_RACE="course=tsukuba,car=<label>,cars=6,view=chase,drive=auto,skip=600,paint=1,hud=1,sim=rail"
// sets it up without the lists, for pictures: `course` a course file's name, `car` a car's label
// (or its model's name), `cars` how many on the grid, `view` in | chase | roof | back, `drive`
// auto (the simple driver takes the first car too) or pad, `skip` simulation steps run before the
// first picture (the start's countdown is left out then), `paint` the first car's paint, `laps` the
// race's length; `view=tv` the course's replay cameras; `replay=N` drives N steps unseen and plays
// them back through the replay cameras.

#include "gt4r/app/app.h"
#include "gt4r/app/race_hud.h"
#include "gt4r/app/race_world.h"

#include "base/log.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/race/drive_camera.h"
#include "gt4r/race/rail_simulation.h"
#include "gt4r/race/replay_script.h"
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
    }
    return s;
}

// The race itself: the world (race_world.h), the flow and the cameras.
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
        for (int i = 0; i < setup.cars; ++i) {
            WorldEntry e;
            e.label = cars[(first + size_t(i) * 37) % cars.size()].label;
            e.paint = i == 0 ? setup.paint : u32(i);
            ws.entries.push_back(e);
        }
        world_ = std::make_unique<RaceWorld>(app, ws);
        inputs_.resize(size_t(setup.cars));
        camera_.reset(world_->cars()[0]);
        // The count before the start: three seconds (a scripted run that skips ahead has none).
        started_with_count_ = setup.skip == 0;
        countdown_ = started_with_count_ ? 3 * 60 : 0;
        for (int i = 0; i < setup.skip; ++i) step(gt3r::PadState{}, true);
        mount_ = setup.view;
        if (const ByteSpan rep = world_->course().section(course::kSlotReplay); race::ReplayScript::is_script(rep)) {
            try {
                replay_script_ = race::ReplayScript::parse(rep);
            } catch (const std::exception& e) {
                LOG_WARN("Replay cameras: %s", e.what());
            }
        }
        if (setup.replay > 0) {
            // A run driven unseen, then shown through the replay cameras.
            countdown_ = 0;
            for (int i = 0; i < setup.replay; ++i) step(gt3r::PadState{}, true);
            start_replay();
        }
    }

    // One simulation step: the first car from the pad (or the simple driver), the others by the
    // simple driver, each on its own side of the line.
    void step(const gt3r::PadState& pad, bool auto_drive) {
        if (replaying_) {
            // The recorded run again, from its start when it ends.
            replay_at_ = (replay_at_ + 1) % history_.size();
            if (replay_at_ == 0) camera_.reset(history_[0][0]), replay_camera_.reset(), world_->clear_effects();
            camera_.step(history_[replay_at_][0], kStep);
            world_->present(history_[replay_at_], true);
            return;
        }
        const std::span<const race::CarState> cars = world_->cars();
        // The start: the cars stand through the count.
        if (countdown_ > 0) {
            --countdown_;
            mapper_.update(pad, kStep);
            std::fill(inputs_.begin(), inputs_.end(), race::CarInput{});
            for (race::CarInput& in : inputs_) in.brake = 1;
            world_->step(inputs_);
            world_->present(world_->cars(), true);
            return;
        }
        auto_drive = auto_drive || finished_;
        for (size_t c = 0; c < inputs_.size(); ++c)
            inputs_[c] = race::follow_line(world_->line(), cars[c], c & 1 ? 2.0f : -2.0f, 1.0f - 0.05f * float(c));
        if (!auto_drive) {
            const gt3r::sim::ControlInputs in = mapper_.update(pad, kStep);
            race::CarInput& me = inputs_[0];
            me = {};
            me.steer = in.steer, me.throttle = in.throttle, me.brake = in.brake, me.handbrake = in.handbrake;
            me.shift_up = in.shift_up, me.shift_down = in.shift_down;
            me.reverse = mapper_.reverse();
            if (me.reverse) me.throttle = std::max(me.throttle, gt3r::race::pressure_to_pedal(pad.press(PadButton::Triangle)));
        }
        const int lap_before = cars[0].lap;
        world_->step(inputs_);
        camera_.step(world_->cars()[0], kStep);
        ++steps_;
        // The player's laps: a lap's time runs from one crossing of the line to the next (the
        // first from the start).
        const int lap = world_->cars()[0].lap;
        if (lap > lap_before && !finished_) {
            if (lap >= 1) {
                last_ms_ = s64(steps_ - lap_start_) * 1000 / 60;
                if (best_ms_ < 0 || last_ms_ < best_ms_) best_ms_ = last_ms_;
                event_lap_ = lap, event_steps_ = 0;
                if (lap >= setup_.laps) finished_ = true, finish_steps_ = 0;
            }
            lap_start_ = steps_;
        }
        if (event_steps_ >= 0) ++event_steps_;
        if (finished_) ++finish_steps_;
        if (history_.size() < kHistory) history_.emplace_back(world_->cars().begin(), world_->cars().end());
        world_->present(world_->cars(), true);
        // Five seconds after the finish the run is shown again.
        if (finished_ && finish_steps_ > 5 * 60) start_replay();
    }

    void start_replay() {
        if (history_.empty() || replaying_) return;
        replaying_ = true;
        replay_at_ = 0;
        mount_ = kTvView;
        camera_.reset(history_[0][0]);
        replay_camera_.reset();
    }
    bool replaying() const { return replaying_; }
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
        // The place: by laps and distance along the course.
        const float length = world_->line().length;
        auto progress = [&](const race::CarState& c) { return float(c.lap) * length + c.course_v; };
        h.place = 1;
        h.cars.clear();
        for (const race::CarState& c : cars) {
            h.place += &c != &me && progress(c) > progress(me);
            h.cars.push_back({c.position.x, c.position.z});
        }
        h.lap = std::clamp(me.lap + 1, 1, setup_.laps), h.laps = setup_.laps;
        h.lap_ms = replaying_ ? s64(replay_at_) * 1000 / 60 : finished_ ? last_ms_ : countdown_ > 0 ? 0 : s64(steps_ - lap_start_) * 1000 / 60;
        h.best_ms = best_ms_, h.last_ms = last_ms_;
        h.time = double(steps_) / 60;
        // A window of a line or message: full for `hold` seconds, then fading out.
        auto window = [](int steps, float hold, float fade) {
            const float t = float(steps) / 60;
            return t < 0 ? 0.0f : t < hold ? 1.0f : std::max(0.0f, 1 - (t - hold) / fade);
        };
        h.message.clear(), h.message_opacity = 0;
        if (replaying_) {
        } else if (countdown_ > 0) {
            // The whole seconds left, each for 0.8 s and a fade of 0.15 s.
            const int left = (countdown_ + 59) / 60;
            h.message = std::to_string(left);
            h.message_opacity = window(left * 60 - countdown_, 0.8f, 0.15f);
        } else if (finished_) {
            h.message = hud.word("RaceFinish");
            h.message_opacity = window(finish_steps_, 3.7f, 0.3f);
        } else if (started_with_count_ && steps_ < 90) {
            h.message = hud.word("RaceStart");
            h.message_opacity = window(steps_, 1.0f, 0.15f);
        }
        h.event.clear(), h.event_opacity = 0;
        if (!replaying_ && event_steps_ >= 0 && event_steps_ < 200) {
            // "L1/3   1'23.456" for three seconds.
            h.event = hud.word("WordOfLapTime") + std::to_string(event_lap_) + "/" + std::to_string(setup_.laps) + "   " + race::format_lap_time(last_ms_);
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
        const race::CarState& me = cars[0];
        const render::Mat4 body = world_->visual(0).body_matrix(me);
        race::ViewPose pose;
        // The replay cameras' shot for the car; the chase view where the script has none.
        if (mount_ != kTvView || !replay_camera_.pose(replay_script_, me, body, pose))
            pose = camera_.pose(me, body, world_->visual(0).mounts(), mount_ == kTvView ? race::DriveCamera::kChase : mount_, look_back || setup_.look_back);
        world_->render(ui, rect, cars, pose, 0, pose.body_hidden ? 0 : -1);
    }

    void quiet() { world_->quiet(); }

    const race::CarState& player() const { return shown()[0]; }
    const std::string& car_name(size_t i) const { return world_->car_name(i); }
    const race::CourseScene& course() const { return world_->course(); }
    int steps() const { return steps_; }
    int mount() const { return mount_; }

private:
    Setup setup_;
    std::unique_ptr<RaceWorld> world_;
    std::vector<race::CarInput> inputs_;
    gt3r::race::ControlMapper mapper_;
    race::DriveCamera camera_;
    race::ReplayScript replay_script_;
    race::ReplayCamera replay_camera_;
    std::vector<std::vector<race::CarState>> history_;
    bool replaying_ = false;
    size_t replay_at_ = 0;
    int countdown_ = 0, lap_start_ = 0, event_steps_ = -1, event_lap_ = 0, finish_steps_ = 0;
    bool started_with_count_ = false, finished_ = false;
    s64 best_ms_ = -1, last_ms_ = -1;
    int mount_ = race::DriveCamera::kChase;
    int steps_ = 0;
};

class RaceScreen : public Screen {
public:
    explicit RaceScreen(App& app, const RaceStart* chosen = nullptr) : setup_(chosen ? Setup{} : setup_from_env()) {
        if (chosen) {
            setup_.course = chosen->course, setup_.car = chosen->car;
            setup_.paint = u32(std::max(0, chosen->paint));
            setup_.cars = std::clamp(chosen->cars, 1, 16), setup_.laps = std::clamp(chosen->laps, 1, 99);
            setup_.scripted = true;
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
        if (m.pressed(PadButton::Start)) {
            paused_ = !paused_;
            if (paused_) race_->quiet();
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
    bool paused_ = false, look_back_ = false;
    double clock_ = 0;
    Uint64 step_ns_ = 0, last_frame_ns_ = 0;
    std::vector<float> work_ms_, interval_ms_;
};

} // namespace

std::unique_ptr<Screen> make_race_screen(App& app) { return std::make_unique<RaceScreen>(app); }
std::unique_ptr<Screen> make_race_screen(App& app, const RaceStart& start) { return std::make_unique<RaceScreen>(app, &start); }

} // namespace gt4r
