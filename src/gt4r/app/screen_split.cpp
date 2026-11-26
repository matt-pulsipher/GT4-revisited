// The two-player battle (Arcade's 2 Player Battle: the program's sequence 0x10C0E8 and its race RaceSplitBattle): two players,
// each with a half of the screen. The run, its replay (both halves again when the options say so), then the menus; the pause's
// Retry runs it again. docs/formats/race_flow.md "2 Player Battle", race_display.md "The two-player halves".
//
// Scripted: GT4R_RACE="players=2,course=tsukuba,car=<label>,car2=<label>,laps=2,handicap=20,boost=1,tires=1,wide=1,
// view=in,view2=chase,drive=auto,drive2=auto,skip=600,replay=<steps>,paint=1,paint2=2,hud=1" (drive / drive2 auto: the simple driver takes
// that player's car; skip: steps run before the first picture, without the start's hold).

#include "gt4r/app/game_area.h"
#include "gt4r/app/race_common.h"
#include "gt4r/app/race_hud.h"
#include "gt4r/app/race_world.h"
#include "gt4r/audio/app_sound.h"
#include "gt4r/audio/race_sound.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/db/race_events.h"
#include "gt4r/input/player_controls.h"
#include "gt4r/input/race_feedback.h"
#include "gt4r/menu/game.h"
#include "gt4r/menu/game_settings.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/race/car_pose.h"
#include "gt4r/race/drive_camera.h"
#include "gt4r/race/race_flow.h"
#include "gt4r/race/replay_script.h"
#include "gt4r/race/sections.h"
#include "gt4r/race/split_battle.h"
#include "gt4r/race/start_camera.h"
#include "gt4r/race/start_rule.h"
#include "gt4r/race/strobe.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/pit_tires.h"
#include "gt4r/sim/program.h"
#include "race/controls.h"
#include "race/interp.h"

#include "base/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>

namespace gt4r {

namespace {

namespace render = gt3r::render;
using RVec = render::Vec3;

constexpr float kStep = 1.0f / 60;
constexpr size_t kPlayers = 2;
constexpr size_t kHistory = 60 * 60 * 10; // steps of a run kept for its replay

struct Setup {
    std::string course = "tsukuba", race = "sample_entry_1";
    std::array<std::string, kPlayers> car;
    std::array<u32, kPlayers> paint{};
    std::array<int, kPlayers> tire{-1, -1}; // the tires chosen (-1 the car's own)
    std::array<RaceWorld::PlayerChoice, kPlayers> choice;
    std::array<int, kPlayers> view{race::DriveCamera::kInCar, race::DriveCamera::kInCar};
    std::array<bool, kPlayers> auto_drive{};
    race::SplitRules rules;
    bool side_by_side = false, replay_split = true, hud = true, scripted = false;
    int skip = 0, replay = 0;
    std::map<std::string, menu::OptionValue> options;
    std::shared_ptr<audio::RacePlaylist> playlist;
};

int view_of(const std::string& option) { // (0x388D40)
    return option == "OVERLOOK" ? race::DriveCamera::kChase : option == "BONNET" ? race::DriveCamera::kRoof : race::DriveCamera::kInCar;
}
std::string option_of(int view) { // (0x388D80)
    return view == race::DriveCamera::kChase ? "OVERLOOK" : view == race::DriveCamera::kRoof ? "BONNET" : "SUBJECTIVE";
}

Setup setup_from_env() {
    Setup s;
    s.scripted = true;
    const char* env = SDL_getenv("GT4R_RACE");
    for (std::string_view kv : gt3r::split(env ? env : "", ',')) {
        const size_t eq = kv.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string k(gt3r::trim(kv.substr(0, eq))), v(gt3r::trim(kv.substr(eq + 1)));
        const auto view = [&] { return v == "in" ? race::DriveCamera::kInCar : v == "roof" ? race::DriveCamera::kRoof : race::DriveCamera::kChase; };
        if (k == "course") s.course = v;
        else if (k == "car") s.car[0] = v;
        else if (k == "car2") s.car[1] = v;
        else if (k == "paint") s.paint[0] = u32(std::max(0, std::atoi(v.c_str())));
        else if (k == "paint2") s.paint[1] = u32(std::max(0, std::atoi(v.c_str())));
        else if (k == "laps") s.rules.laps = std::clamp(std::atoi(v.c_str()), 1, 99);
        else if (k == "handicap") s.rules.handicap = std::atoi(v.c_str());
        else if (k == "boost") s.rules.boost = std::atoi(v.c_str());
        else if (k == "tires") s.rules.tire_damage = std::atoi(v.c_str());
        else if (k == "wide") s.side_by_side = v != "0";
        else if (k == "view") s.view[0] = view();
        else if (k == "view2") s.view[1] = view();
        else if (k == "drive") s.auto_drive[0] = v == "auto";
        else if (k == "drive2") s.auto_drive[1] = v == "auto";
        else if (k == "mt") s.choice[0].automatic = v == "0";
        else if (k == "mt2") s.choice[1].automatic = v == "0";
        else if (k == "skip") s.skip = std::max(0, std::atoi(v.c_str()));
        else if (k == "replay") s.replay = std::max(0, std::atoi(v.c_str()));
        else if (k == "split") s.replay_split = v != "0"; // (the replay's halves)
        else if (k == "hud") s.hud = v != "0";
    }
    return s;
}

// The tires of a kind on an equipment, both axles (0x330650: the car's fitting parts, 0x44A048, set by 0x4468F8; kinds without a part leave
// the axle as it is).
void fit_tires(App& app, sim::Equipment& eq, int kind) {
    if (kind < 0) return;
    gt3r::Vfs* vfs = app.vfs();
    const std::vector<sim::FittingPart> parts = sim::fitting_parts([vfs](const std::string& path) { return vfs->read(path); }, *app.db(), eq.part(sim::Equipment::Car));
    const db::PartId front = sim::fitting_part(parts, u32(db::PartTable::FrontTire), u8(kind)), rear = sim::fitting_part(parts, u32(db::PartTable::RearTire), u8(kind));
    if (front != db::kNoPart) eq.set_part(sim::Equipment::FrontTire, front);
    if (rear != db::kNoPart) eq.set_part(sim::Equipment::RearTire, rear);
}

// The battle's race: the world, its flow, each player's cameras and what his display is told.
class SplitRace {
public:
    SplitRace(App& app, const Setup& setup) : setup_(setup) {
        WorldSetup ws;
        ws.course = setup.course;
        ws.players = int(kPlayers);
        ws.own_hold = setup.skip == 0;
        ws.laps = setup.rules.laps;
        ws.options = setup.options;
        ws.playlist = setup.playlist;
        db::SpecDb& sdb = *app.db();
        gt3r::Vfs* vfs = app.vfs();
        const race::CourseStart course = race::course_start(sdb, setup.course);
        start_type_ = race::StartType::Ten;
        // The race's event (sample_entry_1: its start type, its hits' limits ...), with the battle's rules in it.
        if (const std::optional<db::RaceEvent> e = db::RaceEvents(sdb, [vfs](const std::string& path) { return vfs->read(path); }).event(setup.race)) {
            ws.event_row = e->bytes;
            ws.pace = sim::ai::pace_for(*e);
            // (its start: the row's +0x40 and +0x41, race_flow.md "The start")
            if (ws.event_row.size() > 0x41) {
                start_type_ = race::StartType(std::clamp(int(ws.event_row[0x40]), 0, 3));
                start_seconds_ = ws.event_row[0x41];
            }
        }
        // (No wear on a loose course, nor on one whose row's +0x14 is 0: 0x34CCD4.)
        race::put_tire_damage(ws.event_row, course.fuel && course.kind != 2 && course.kind != 3 ? setup.rules.tire_damage : 0);
        race::put_boost(ws.event_row, setup.rules.boost);
        if (ws.event_row.size() > 0x6A) {
            ws.pace.band_mode = ws.event_row[0x64];
            ws.pace.fast_pct = ws.event_row[0x68], ws.pace.fast_start_m = ws.event_row[0x69], ws.pace.fast_full_10m = ws.event_row[0x6A];
        }
        std::optional<sim::Program> program;
        if (app.disc()) try {
                program = sim::Program::load(*app.disc());
            } catch (const std::exception& e) {
                LOG_WARN("Split: the program: %s", e.what());
            }
        for (size_t i = 0; i < kPlayers; ++i) {
            WorldEntry e;
            e.label = setup.car[i], e.paint = setup.paint[i], e.grid_place = int(i);
            const RaceWorld::PlayerChoice& c = setup.choice[i];
            e.automatic = c.automatic, e.asm_on = c.asm_on, e.tcs_on = c.tcs_on, e.wheel_aid = c.wheel_aid, e.driver = c.wheel_lock;
            if (const int tire = race::split_tire(course.kind, setup.tire[i]); tire >= 0 && program) {
                sim::Equipment eq;
                if (sim::stock_car(sdb, *program, e.label, eq)) {
                    fit_tires(app, eq, tire);
                    e.equipment = eq;
                }
            }
            if (const std::optional<race::CarStart> s = race::handicap_start(setup.rules.handicap, i))
                e.start_kind = s->kind, e.start_distance = float(s->distance);
            ws.entries.push_back(e);
        }
        world_ = std::make_unique<RaceWorld>(app, ws);
        for (size_t i = 0; i < kPlayers; ++i) world_->set_player(setup.choice[i], i);
        const auto script = [this](int slot, race::ReplayScript& out) {
            if (const ByteSpan rep = world_->course().section(slot); race::ReplayScript::is_script(rep)) try {
                    out = race::ReplayScript::parse(rep);
                } catch (const std::exception& e) {
                    LOG_WARN("Split: the cameras: %s", e.what());
                }
        };
        script(course::kSlotReplay, replay_script_);
        for (int k = 0; k < 2; ++k)
            if (const ByteSpan mot = world_->course().section(k ? race::kSlotReplayStartMotion : race::kSlotStartMotion); race::CameraMotion::is_motion(mot)) try {
                    (k ? replay_motion_ : start_motion_) = race::CameraMotion::parse(mot);
                } catch (const std::exception& e) {
                    LOG_WARN("Split: the start's shots: %s", e.what());
                }
        splits_ = race::road_list(world_->course().section(course::kSlotRunway), 0);
        inputs_.resize(kPlayers);
        for (size_t i = 0; i < kPlayers; ++i) half_[i].mount = setup.view[i];
        begin_run();
        for (int i = 0; i < setup.skip; ++i) step({}, true);
        if (setup.replay > 0) {
            for (int i = 0; i < setup.replay; ++i) step({}, true);
            start_replay();
        }
    }

    void begin_run() {
        if (ran_) world_->restart();
        ran_ = true;
        race::FlowSetup fs;
        fs.start = setup_.skip > 0 ? race::StartType::None : start_type_;
        fs.start_seconds = start_seconds_;
        fs.laps = setup_.rules.laps;
        fs.lap_length = world_->lap_length();
        fs.counted = true;
        fs.goal = world_->start_race().goal;
        fs.no_lap = world_->start_race().way == 1;
        if (world_->simulated()) {
            fs.sectors = world_->start_race().sectors;
            for (size_t i = 0; i < kPlayers; ++i) fs.crossings.push_back(world_->start_crossings(i));
        } else fs.sectors = splits_;
        flow_.begin(fs, world_->cars());
        for (size_t i = 0; i < kPlayers; ++i) world_->set_crossings(i, flow_.car(i).counter), world_->set_rank(i, flow_.place(i));
        board_.begin(kPlayers, 0, lap_words_[0], lap_words_[1]);
        arrivals_.reset(splits_.size() + 1);
        history_.clear(), prev_cars_.clear();
        replaying_ = false, replay_done_ = false, steps_ = 0;
        if (audio::RaceSound* sound = world_->sound()) {
            sound->forget_recording();
            sound->begin_run(audio::race_music("split_battle", setup_.race, false), int(fs.start));
        }
        for (size_t i = 0; i < kPlayers; ++i) {
            Half& h = half_[i];
            const int mount = h.mount;
            h = Half{};
            h.mount = mount;
            h.camera.reset(world_->cars()[i]);
            begin_start_camera(i, false);
        }
        world_->clear_effects();
    }

    // One step: each player's car by his controller's frame (`unseen`: a run's steps before its first picture, both by the simple driver).
    void step(const std::array<input::RaceInput, kPlayers>& player, bool unseen) {
        prev_cars_.assign(shown().begin(), shown().end());
        for (Half& h : half_) h.camera_prev = h.camera, h.start_prev = h.start_camera, h.tv_prev = h.tv, h.tv_prev_valid = h.tv_valid, h.cut = false;
        if (replaying_) {
            if (replay_at_ + 1 >= history_.size()) {
                replay_done_ = true;
                return;
            }
            ++replay_at_;
            for (size_t i = 0; i < kPlayers; ++i) step_half(i, history_[replay_at_][i]);
            if (audio::RaceSound* sound = world_->sound()) sound->set_replay_shot(half_[0].tv_valid && !half_[0].start_camera.active());
            hear(history_[replay_at_], false);
            return;
        }
        const std::span<const race::CarState> cars = world_->cars();
        const bool hold = flow_.holding();
        for (size_t i = 0; i < kPlayers; ++i) {
            inputs_[i] = {};
            if (flow_.car(i).finished) continue; // (his car's driver cruises on: finish_car)
            if (unseen || setup_.auto_drive[i]) {
                if (!hold) inputs_[i] = race::follow_line(world_->line(), cars[i], i & 1 ? 2.0f : -2.0f, 1.0f - 0.05f * float(i));
                continue;
            }
            const sim::rules::PadFrame& pad = player[i].frame;
            pad.to_bytes(frames_[i].bytes.data());
            race::CarInput& in = inputs_[i];
            in.pad = &frames_[i];
            // (the fields beside the frame are the stand-in simulation's)
            const auto pedal = [](bool analog, u16 v) { return analog ? float(v) / 4096.0f : v ? 1.0f : 0.0f; };
            in.steer = pad.flags & 1 ? -float(pad.steer) / 4096.0f : pad.steer > 0 ? -1.0f : pad.steer < 0 ? 1.0f : 0.0f;
            in.reverse = pad.back != 0 && pedal(pad.flags & 8, pad.back) >= pedal(pad.flags & 2, pad.throttle);
            in.throttle = std::max(pedal(pad.flags & 2, pad.throttle), pedal(pad.flags & 8, pad.back)), in.brake = pedal(pad.flags & 4, pad.brake);
            in.handbrake = pad.third != 0, in.shift_up = pad.shift > 0, in.shift_down = pad.shift < 0;
        }
        world_->step(inputs_, hold, false, u32(std::max(0, flow_.hold_left() - 1)));
        ++steps_;
        const race::RaceFlow::Events ev = flow_.step(world_->cars());
        for (size_t i = 0; i < kPlayers; ++i) world_->set_crossings(i, flow_.car(i).counter), world_->set_rank(i, flow_.place(i));
        const bool first_finish = !ev.finished.empty() && !flow_.everyone_finished() ? true : ev.finished.size() == kPlayers;
        for (const size_t i : ev.finished) {
            world_->finish_car(i, false); // (the battle's conductor 0x347230: 0x343D78 with 3)
            half_[i].finish_steps = 0;
            LOG_INFO("Split: player %zu finishes, place %d, %.3f s", i + 1, flow_.place(i), double(flow_.run_ms(i)) / 1000);
        }
        if (!ev.finished.empty()) board_.finished(flow_, ev.finished);
        step_arrivals();
        if (audio::RaceSound* sound = world_->sound()) {
            if (hold) sound->count(flow_.hold_left());
            if (ev.start) sound->start();
            if (first_finish) sound->finish();
        }
        for (size_t i = 0; i < kPlayers; ++i) {
            Half& h = half_[i];
            const race::CarState& c = world_->cars()[i];
            h.distance += hold ? 0.0f : std::fabs(c.speed) * kStep;
            const float ax = (c.velocity.x - h.last_v.x) / kStep, ay = (c.velocity.y - h.last_v.y) / kStep, az = (c.velocity.z - h.last_v.z) / kStep;
            h.lateral_g += (std::clamp((ax * c.right.x + ay * c.right.y + az * c.right.z) / 9.81f, -4.0f, 4.0f) - h.lateral_g) * 0.2f;
            h.last_v = c.velocity;
            step_half(i, c);
            for (int* t : {&h.event_steps, &h.gap_steps, &h.finish_steps})
                if (*t >= 0) ++*t;
        }
        // GT4R_RUN_LOG=<n>: both cars' distance, speed and place every n steps.
        if (const char* every = SDL_getenv("GT4R_RUN_LOG"); every && std::atoi(every) > 0 && steps_ % std::atoi(every) == 0)
            for (size_t i = 0; i < kPlayers; ++i) {
                const race::CarState& c = world_->cars()[i];
                LOG_INFO("Split: step %d car %zu: d %.3f at %.4f km/h gear %d lap %d place %d", steps_, i, double(c.course_v), double(c.speed) * 3.6, c.gear, flow_.car(i).counter, flow_.place(i));
            }
        if (history_.size() < kHistory) history_.emplace_back(world_->cars().begin(), world_->cars().end());
        hear(world_->cars(), true);
    }

    void start_replay() {
        if (history_.empty() || replaying_) return;
        replaying_ = true, replay_done_ = false, replay_at_ = 0;
        prev_cars_.clear();
        if (audio::RaceSound* sound = world_->sound()) sound->replay();
        world_->clear_effects();
        for (size_t i = 0; i < kPlayers; ++i) {
            half_[i].camera.reset(history_[0][i]);
            half_[i].replay_camera.reset(), half_[i].tv_valid = false;
            begin_start_camera(i, true);
        }
    }
    bool has_run() const { return !history_.empty(); }
    bool replaying() const { return replaying_; }
    bool replay_done() const { return replay_done_; }
    bool any_finished() const { return flow_.car(0).finished || flow_.car(1).finished; }
    bool finished(size_t i) const { return flow_.car(i).finished; }
    // The screen shows both halves (a replay only with the options' replay_2p_split).
    bool halves() const { return !replaying_ || setup_.replay_split; }

    void next_view(size_t i) {
        size_t at = 0;
        for (size_t k = 0; k < std::size(race::DriveCamera::kViews); ++k)
            if (race::DriveCamera::kViews[k] == half_[i].mount) at = k;
        half_[i].mount = race::DriveCamera::kViews[(at + 1) % std::size(race::DriveCamera::kViews)];
    }
    int mount(size_t i) const { return half_[i].mount; }
    void set_look_back(size_t i, bool on) { half_[i].look_back = on; }
    void set_blend(float t) { blend_ = std::clamp(t, 0.0f, 1.0f); }

    // (A picture narrower than 16:9 has no room for two displays beside each other.)
    bool side_by_side() const { return setup_.side_by_side && game_area::side() > 106.0f; }
    // A half's part of the picture (0x32FCC8), and the line between the two.
    SDL_FRect half_rect(const SDL_FRect& whole, size_t i) const {
        if (side_by_side()) return {whole.x + whole.w / 2 * float(i), whole.y, whole.w / 2, whole.h};
        return {whole.x, whole.y + whole.h / 2 * float(i), whole.w, whole.h / 2};
    }
    void render(Ui& ui, const SDL_FRect& whole) {
        std::span<const race::CarState> cars = shown();
        const bool blend = blend_ < 1 && prev_cars_.size() == cars.size() && !cars.empty();
        if (blend) {
            blended_.resize(cars.size());
            for (size_t i = 0; i < cars.size(); ++i) blended_[i] = race::blend_states(prev_cars_[i], cars[i], blend_);
            cars = blended_;
        }
        world_->set_grade(picture_settings(replaying_).grade());
        if (!halves()) {
            const race::ViewPose pose = half_pose(0, blend);
            world_->render(ui, whole, cars, pose, 0, pose.body_hidden ? 0 : -1);
            return;
        }
        for (size_t i = 0; i < kPlayers; ++i) {
            const SDL_FRect rect = half_rect(whole, i);
            const race::SplitView view = race::split_view(side_by_side(), int(i));
            const race::ViewPose pose = half_pose(i, blend);
            race::ViewPose seen = pose;
            seen.fov = 2 * std::atan(std::tan(pose.fov * 0.5f) / view.lens);
            world_->render(ui, rect, cars, seen, i, pose.body_hidden ? int(i) : -1, int(i));
            // The rear-view mirror of the in-car view, while the race is driven.
            if (!replaying_ && half_[i].mount == race::DriveCamera::kInCar && pose.body_hidden && !half_[i].look_back && !half_[i].start_camera.active()) {
                const float kx = side_by_side() ? rect.w / view.picture[0] : whole.h * 4 / 3 / view.picture[0], ky = rect.h / view.picture[1];
                const float x = side_by_side() ? rect.x + view.mirror[0] * kx : rect.x + rect.w / 2 + (view.mirror[0] - view.picture[0] / 2) * kx;
                world_->render_mirror(ui, {x, rect.y + view.mirror[1] * ky, view.mirror[2] * kx, view.mirror[3] * ky}, cars, pose, int(i), int(i), view.mirror_zoom);
            }
        }
        const float line = whole.h * 2 / 448;
        if (side_by_side()) ui.fill(whole.x + whole.w / 2 - line / 2, whole.y, line, whole.h, Color{0, 0, 0, 255});
        else ui.fill(whole.x, whole.y + whole.h / 2 - line / 2, whole.w, line, Color{0, 0, 0, 255});
    }

    // What a player's display shows.
    void fill(HudState& h, const RaceHud& hud, size_t i) const {
        const std::span<const race::CarState> cars = shown();
        const Half& me = half_[i];
        const race::CarState& c = cars[i];
        const race::CarSpec& spec = world_->visual(i).spec();
        h.split = side_by_side() ? 2 : 1, h.half = int(i);
        h.replay = replaying_;
        h.speed = c.speed, h.rpm = c.engine_rpm, h.gear = c.gear, h.clutch_closed = c.clutch_closed, h.gear_flash = me.gear_flash;
        h.stepless = world_->stepless(i);
        h.signs = replaying_ ? std::array<int, 3>{} : world_->signs(i);
        h.throttle = c.throttle, h.brake = c.brake;
        h.fuel_on = world_->counts_fuel(), h.fuel = replaying_ ? 1.0f : world_->fuel_share(i);
        h.tires_on = !replaying_ && world_->wears_tires();
        for (int w = 0; w < 4; ++w) h.tire_rgb[w] = h.tires_on ? race::tire_color(world_->tire_level(i, w)) : 0;
        h.suggested_gear = replaying_ || finished(i) ? -1 : world_->suggested_gear(i);
        h.lateral_g = replaying_ ? 0 : me.lateral_g;
        h.red_line = spec.red_line, h.rev_limit = spec.rev_limit;
        h.cars.clear();
        h.cars.push_back({c.position.x, c.position.z});
        for (size_t k = 0; k < cars.size(); ++k)
            if (k != i) h.cars.push_back({cars[k].position.x, cars[k].position.z});
        h.place = flow_.place(i);
        h.lap = flow_.lap_shown(i), h.laps = setup_.rules.laps;
        h.lap_ms = replaying_ ? s64(replay_at_) * 1000 / 60 : flow_.lap_ms(i);
        h.best_ms = flow_.car(i).best_lap_ms, h.last_ms = flow_.car(i).last_lap_ms;
        h.time = double(flow_.time_ms()) / 1000;
        const auto window = [](int steps, float hold, float fade) {
            const float t = float(steps) / 60;
            return t < 0 ? 0.0f : t < hold ? 1.0f : std::max(0.0f, 1 - (t - hold) / fade);
        };
        // The display under the opening shots: out 6.1 s, then in over 0.5 (0x39BEC8); from the finish on full 1 s, out in 0.25 (0x3A1568).
        h.opacity = 1;
        if (!replaying_ && flow_.hold_total() > 0) h.opacity = std::clamp((float(steps_) / 60 - 6.1f) / 0.5f, 0.0f, 1.0f);
        if (!replaying_ && me.finish_steps >= 0) h.opacity = window(me.finish_steps, 1, 0.25f);
        h.message.clear(), h.message_opacity = 0;
        if (!replaying_ && me.finish_steps >= 0) {
            h.message = hud.word("RaceFinish"), h.message_opacity = window(me.finish_steps, 3.7f, 0.3f);
        } else if (const int left = flow_.hold_left(); !replaying_ && flow_.holding() && left > 1) {
            const int n = (left + 58) / 60, at = n * 60 + 1;
            if (n > 0 && at <= flow_.hold_total() && (n < 4 || h.opacity >= 1)) h.message = std::to_string(n), h.message_opacity = window(at - left, 0.8f, n < 4 ? 0.15f : 0.0f);
        } else if (const int s = flow_.steps_since_start(); !replaying_ && s >= 0 && flow_.setup().start != race::StartType::None) {
            h.message = hud.word("RaceStart"), h.message_opacity = window(s, 1.0f, 0.15f);
        }
        h.status.clear(), h.status_opacity = 0, h.status_red = false;
        if (const int left = replaying_ ? 0 : world_->penalty_seconds(i); left > 0) {
            h.status = hud.word("Penalty"), h.status_opacity = 1, h.status_red = true;
            if (const size_t at = h.status.find("%d"); at != std::string::npos) h.status.replace(at, 2, std::to_string(left));
        }
        h.event.clear(), h.event_opacity = 0, h.event_gap.clear(), h.event_gap_opacity = 0, h.event_gap_lead = false;
        if (replaying_) return;
        if (me.event_steps >= 0 && me.event_steps < 200) {
            // "L1/2   1'23.456" (a half's always with the laps: 0x3A0AD8), "T1   0'27.247", three seconds.
            h.event = hud.word(me.event_lap > 0 ? "WordOfLapTime" : "WordOfSectionTime");
            if (me.event_lap > 0) h.event += std::to_string(me.event_lap) + (setup_.rules.laps > 0 ? "/" + std::to_string(setup_.rules.laps) : std::string());
            else h.event += std::to_string(me.event_split);
            h.event += "   " + race::format_lap_time(me.event_ms);
            h.event_opacity = std::min(1.0f, float(me.event_steps) / 9.0f) * window(me.event_steps, 3.0f, 0.15f);
        }
        if (me.gap_steps >= 0 && me.gap_steps < 200) {
            if (me.gap_laps > 0) h.event_gap = "+" + std::to_string(me.gap_laps) + lap_words_[me.gap_laps > 1 ? 1 : 0];
            else h.event_gap = (me.gap_ms < 0 ? "-" : "+") + race::format_lap_time(std::abs(me.gap_ms));
            h.event_gap_lead = me.gap_laps == 0 && me.gap_ms < 0;
            h.event_gap_opacity = window(me.gap_steps, 3.0f, 0.15f);
        }
    }

    void set_lap_words(std::string one, std::string several) { lap_words_[0] = std::move(one), lap_words_[1] = std::move(several); }
    void set_choice(size_t i, const RaceWorld::PlayerChoice& c) { world_->set_player(c, i); }
    RaceWorld& world() { return *world_; }
    const race::RaceFlow& flow() const { return flow_; }
    race::ResultBoard& board() { return board_; }
    const std::string& car_short_name(size_t i) const { return world_->car_short_name(i); }

private:
    // What a player's half keeps.
    struct Half {
        int mount = race::DriveCamera::kInCar;
        bool look_back = false;
        race::DriveCamera camera, camera_prev;
        race::StartCamera start_camera, start_prev;
        race::ReplayCamera replay_camera;
        race::ViewPose tv, tv_prev;
        bool tv_valid = false, tv_prev_valid = false, cut = false;
        int gear_shown = 1;
        float gear_flash = 0, lateral_g = 0, distance = 0;
        course::Vec3 last_v;
        // The event line: a lap's time (event_lap) or a section's (event_split); a gap to the other car under it.
        int event_steps = -1, event_lap = 0, event_split = 0, gap_steps = -1, gap_laps = 0, finish_steps = -1;
        s64 event_ms = 0, gap_ms = 0;
        float prev_v = -1;
        int prev_laps = 0;
    };

    std::span<const race::CarState> shown() const { return replaying_ ? std::span<const race::CarState>(history_[replay_at_]) : world_->cars(); }

    void begin_start_camera(size_t i, bool replay) {
        const race::CameraMotion& m = replay ? replay_motion_ : start_motion_;
        const course::Runway& road = world_->course().road();
        course::Runway::Place grid;
        if (!road.grid.empty()) grid = road.grid[0];
        if (float y; !road.grid.empty() && road.height_at(grid.pos.x, grid.pos.z, grid.pos.y + 50.0f, y)) grid.pos.y = y;
        const int hold = replay ? flow_.hold_total() : flow_.hold_left();
        half_[i].start_camera.begin(m.clips.empty() ? nullptr : &m, replay, hold, grid, replay ? history_[0][i] : world_->cars()[i]);
        half_[i].start_prev = half_[i].start_camera;
    }

    // A half's step: its cameras, the gear's flash.
    void step_half(size_t i, const race::CarState& c) {
        Half& h = half_[i];
        h.start_camera.step();
        h.camera.step(c, kStep);
        if (c.clutch_closed && c.gear != h.gear_shown) h.gear_shown = c.gear, h.gear_flash = 1;
        else h.gear_flash = std::max(0.0f, h.gear_flash - kStep / 0.15f);
        if (replaying_) {
            h.tv_valid = h.replay_camera.pose(replay_script_, c, world_->visual(i).body_matrix(c), h.tv);
            h.cut = h.replay_camera.cut() || h.tv_valid != h.tv_prev_valid;
        }
    }

    // The laps' and sections' times, and the gaps told (0x3578B0, 0x359238).
    void step_arrivals() {
        const std::span<const race::CarState> cars = world_->cars();
        for (size_t i = 0; i < kPlayers; ++i) {
            Half& h = half_[i];
            const race::RaceFlow::CarRecord& r = flow_.car(i);
            const float v = cars[i].course_v, before = std::exchange(h.prev_v, v);
            if (r.laps_done > h.prev_laps) {
                h.prev_laps = r.laps_done;
                h.event_steps = 0, h.event_lap = r.laps_done, h.event_ms = r.last_lap_ms;
                arrivals_.arrive(i, r.laps_done, splits_.size(), r.finished ? race::clock_ms(r.finish_units) : flow_.time_ms());
            }
            if (before < 0 || r.counter < 1 || !(v > before) || r.finished || flow_.steps_since_start() < 0) continue;
            for (size_t k = 0; k < splits_.size(); ++k) {
                if (!(before < splits_[k] && splits_[k] <= v)) continue;
                h.event_steps = 0, h.event_lap = 0, h.event_split = int(k) + 1, h.event_ms = flow_.lap_ms(i);
                arrivals_.arrive(i, r.counter, k, flow_.time_ms());
            }
        }
        const race::ArrivalBoard::Told told = arrivals_.tell([this](size_t car) { return flow_.place(car); });
        for (const auto& g : told.gaps) half_[g.car].gap_steps = 0, half_[g.car].gap_ms = g.ms, half_[g.car].gap_laps = 0;
        for (const auto& d : told.laps_down) half_[d.car].gap_steps = 0, half_[d.car].gap_laps = d.laps;
    }

    race::ViewPose pose_of(size_t i, std::span<const race::CarState> cars) {
        Half& h = half_[i];
        const race::CarState& c = cars[i];
        const render::Mat4 body = world_->visual(i).body_matrix(c);
        race::ViewPose pose;
        bool posed = h.start_camera.active() && h.start_camera.pose(pose, &body);
        if (!posed && replaying_ && h.tv_valid) pose = h.tv, posed = true;
        if (posed) {
            pose.fov = race::shown_angle(pose.fov);
            return pose;
        }
        const int mount = replaying_ ? race::DriveCamera::kChase : h.mount;
        pose = h.camera.pose(c, body, world_->visual(i).mounts(), mount, h.look_back);
        if (mount == race::DriveCamera::kRoof) pose.fov = race::shown_angle(pose.fov);
        return pose;
    }
    race::ViewPose half_pose(size_t i, bool blend) {
        Half& h = half_[i];
        const race::ViewPose now = pose_of(i, shown());
        if (!blend || h.cut || h.start_prev.active() != h.start_camera.active()) return now;
        std::swap(h.camera, h.camera_prev), std::swap(h.start_camera, h.start_prev), std::swap(h.tv, h.tv_prev), std::swap(h.tv_valid, h.tv_prev_valid);
        const race::ViewPose was = pose_of(i, prev_cars_);
        std::swap(h.camera, h.camera_prev), std::swap(h.start_camera, h.start_prev), std::swap(h.tv, h.tv_prev), std::swap(h.tv_valid, h.tv_prev_valid);
        return race::blend_poses(was, now, blend_);
    }

    // The step heard from both halves' cameras (0x398B70, 0x3B5E80).
    void hear(std::span<const race::CarState> cars, bool live) {
        std::vector<audio::SoundListener> at;
        for (size_t i = 0; i < (halves() ? kPlayers : 1); ++i) {
            const race::ViewPose pose = pose_of(i, cars);
            const auto& m = pose.view.m;
            audio::SoundListener l;
            l.position = {pose.eye.x, pose.eye.y, pose.eye.z};
            l.right = {m[0], m[4], m[8]}, l.up = {m[1], m[5], m[9]}, l.forward = {-m[2], -m[6], -m[10]};
            l.velocity = cars[i].velocity;
            const bool own = !half_[i].start_camera.active() && !replaying_;
            l.kind = pose.body_hidden ? audio::SoundListener::Kind::InCar : own && half_[i].mount == race::DriveCamera::kChase ? audio::SoundListener::Kind::Chase : audio::SoundListener::Kind::Other;
            at.push_back(l);
        }
        world_->present(cars, at, live);
    }

    Setup setup_;
    std::unique_ptr<RaceWorld> world_;
    race::RaceFlow flow_;
    race::ResultBoard board_;
    race::ArrivalBoard arrivals_;
    race::ReplayScript replay_script_;
    race::CameraMotion start_motion_, replay_motion_;
    std::array<Half, kPlayers> half_;
    std::vector<race::CarInput> inputs_;
    std::array<race::PadRecord, kPlayers> frames_;
    std::vector<race::CarState> prev_cars_, blended_;
    std::vector<std::vector<race::CarState>> history_;
    std::vector<float> splits_;
    std::string lap_words_[2] = {"Lap", "Laps"};
    race::StartType start_type_ = race::StartType::Ten;
    int start_seconds_ = 0, steps_ = 0;
    float blend_ = 1;
    bool ran_ = false, replaying_ = false, replay_done_ = false;
    size_t replay_at_ = 0;
};

class SplitScreen : public Screen {
public:
    SplitScreen(App& app, const RaceStart* chosen) : app_(app), setup_(chosen ? Setup{} : setup_from_env()) {
        wheel_ = input::WheelSettings::load(app.config());
        // Without a controller of his own the second player drives with his keys of the keyboard, for the battle's time.
        if (app.input().device_filter(1) == "auto" && app.input().device_name(1) == "No device") app.input().set_keyboard(1, keys_lent_ = true);
        if (chosen && chosen->menus) {
            menus_ = chosen->menus;
            menu::GameState& g = menus_->game();
            const menu::RaceRequest& q = g.race;
            setup_.course = chosen->course;
            if (!q.race.empty()) setup_.race = q.race;
            setup_.car = {q.car, q.car_2p};
            setup_.paint = {u32(std::max(0, q.paint)), u32(std::max(0, q.paint_2p))};
            setup_.tire = {q.tire, q.tire_2p};
            setup_.options = g.options;
            setup_.playlist = playlist_from(g.bgm_playlist);
            pictures_from_options(setup_.options);
            setup_.rules = race::SplitRules::from_options(setup_.options);
            const auto text = [&](const char* name) {
                const auto it = setup_.options.find(name);
                const std::string* v = it == setup_.options.end() ? nullptr : std::get_if<std::string>(&it->second);
                return v ? *v : std::string();
            };
            setup_.view = {view_of(text("view_type")), view_of(text("view_type_2p"))};
            const auto flag = [&](const char* name, bool otherwise) {
                const auto it = setup_.options.find(name);
                const s32* v = it == setup_.options.end() ? nullptr : std::get_if<s32>(&it->second);
                return v ? *v != 0 : otherwise;
            };
            setup_.replay_split = flag("replay_2p_split", true);
            if (const char* e = SDL_getenv("GT4R_AUTODRIVE"); e && *e == '1') setup_.auto_drive = {true, true};
        } else {
            if (setup_.car[0].empty()) setup_.car[0] = "ka_01";
            if (setup_.car[1].empty()) setup_.car[1] = "focus_st170_jp_03";
        }
        settings_ = menu::settings_from(setup_.options);
        if (menus_) {
            setup_.side_by_side = settings_.wide;
            for (size_t i = 0; i < kPlayers; ++i) setup_.choice[i] = player_choice(i);
        }
        if (!app.vfs()->exists("crs/" + setup_.course) && setup_.course.rfind("r_", 0) == 0) {
            LOG_WARN("Split: the disc has no reversed layout crs/%s: the course is driven forward", setup_.course.c_str());
            setup_.course = setup_.course.substr(2);
        }
        if (!setup_.playlist) setup_.playlist = new_game_playlist(app);
        if (!app.gpu() || !app.db()) error_ = "3D drawing is not available on this system.";
        else try {
                race_ = std::make_unique<SplitRace>(app, setup_);
                hud_ = std::make_unique<RaceHud>(app, race_->world().course().section(course::kSlotMiniMap));
                race_->set_lap_words(hud_->word("WordOfLap"), hud_->word("WordOfLaps"));
                race_->board().begin(kPlayers, 0, hud_->word("WordOfLap"), hud_->word("WordOfLaps"));
                if (!setup_.options.empty()) hud_->set_mph(std::string(settings_.velocity.name).find("mph") != std::string::npos);
            } catch (const std::exception& e) {
                error_ = e.what();
                LOG_WARN("Split: %s", e.what());
                race_.reset();
            }
        if (!race_ && menus_) leave(app);
    }
    ~SplitScreen() override {
        stop_feedback();
        if (keys_lent_) app_.input().set_keyboard(1, false);
    }

    bool follows_aspect() const override { return race_ != nullptr; }

    void update(App& app, double dt) override {
        const MenuInput& m = app.menu();
        if (!race_) {
            if (menus_ ? left_ : m.pressed(PadButton::Triangle)) {
                if (!left_) app.pop();
            }
            return;
        }
        if (!playing_any()) stop_feedback();
        if (paused_) {
            read_controls(app);
            return update_pause(app, m, dt);
        }
        // The first player's Start opens the pause (the race's virtual 70, 0x32F910: his controller; 0x38B0E0), in the replay too (its
        // virtual 23 answers 0): there Retry races again and Exit ends it.
        if (race_->replaying()) {
            if (m.pressed(PadButton::Start)) return open_pause(app);
            if (race_->replay_done()) return leave(app);
        } else if (race_->any_finished()) {
            // The results page goes up at the first finish of either (ResultArcade's +0x64 is 1), and may be left at once.
            if (!result_ && !result_tried_) open_results(app);
            bool done = false;
            if (result_) {
                race_->board().follow(race_->flow());
                if (m.vertical()) result_->key(m.vertical() < 0 ? 0x26 : 0x28);
                if (m.pressed(PadButton::Cross)) result_->key(0x20);
                result_->advance(float(dt));
                done = result_exit_;
            } else done = m.pressed(PadButton::Cross);
            if (done) return run_over(app);
        }
        const std::array<std::pair<u32, u32>, kPlayers> down = read_controls(app);
        for (size_t i = 0; i < kPlayers; ++i) {
            if (down[i].first & input::RaceInput::kPlayView) race_->next_view(i);
            race_->set_look_back(i, (inputs_[i].views & (input::RaceInput::kBackView | input::RaceInput::kRearView)) != 0);
        }
        if (!race_->replaying() && !race_->any_finished()) {
            if (down[0].second & input::RaceInput::kPause) return open_pause(app);
            if (lost_device(app)) {
                LOG_INFO("Split: a player's controller is gone: paused");
                return open_pause(app);
            }
        }
        // (Settings > Controls: the songs' own controls)
        for (int p = 0; p < gt3r::InputManager::kMaxPlayers; ++p)
            if (const int song = int(app.input().action_pressed(p, gt3r::PadAction::NextSong)) - int(app.input().action_pressed(p, gt3r::PadAction::PreviousSong)))
                if (audio::RaceSound* sound = race_->world().sound()) sound->skip_song(song);
        clock_ += pacer_.step(dt);
        int n = 0;
        for (; clock_ >= kStep && n < 4; ++n) {
            clock_ -= kStep;
            race_->step(inputs_, false);
            if (const sim::Session* session = race_->world().session())
                for (size_t i = 0; i < kPlayers; ++i)
                    if (playing(i)) feedback_[i].step(input::car_feel(*session, i), kStep);
        }
        if (n == 4) clock_ = 0;
        for (size_t i = 0; i < kPlayers; ++i)
            if (playing(i)) feedback_[i].frame(app.input(), int(i), input::FeedbackOptions::from(controllers_.port[i], wheel_.wheel), dt);
            else feedback_[i].stop(app.input(), int(i));
        race_->set_blend(float(clock_ / kStep));
    }

    void on_event(App& app, const SDL_Event& e) override {
        if (e.type != SDL_EVENT_WINDOW_FOCUS_LOST || !playing_any()) return;
        SDL_Window* window = SDL_GetWindowFromID(e.window.windowID);
        if (window && (SDL_GetWindowFlags(window) & SDL_WINDOW_NOT_FOCUSABLE)) return; // (a scripted run's)
        if (gt3r::race::focus_loss(true, app.input().reads_background(), false) != gt3r::race::FocusLoss::Pause) return;
        stop_feedback();
        open_pause(app);
    }

    void render(App&, Ui& ui) override {
        const SDL_FRect whole{0, 0, ui.width(), float(Ui::kHeight)};
        if (!race_) {
            ui.fill(0, 0, whole.w, whole.h, Color{0, 0, 0, 255});
            if (!error_.empty()) ui.text_center(Ui::kHeight / 2, error_, colors::kBad);
            return;
        }
        race_->render(ui, whole);
        const bool results = result_ && race_->any_finished() && !race_->replaying();
        if (setup_.hud && hud_ && race_->halves()) {
            for (size_t i = 0; i < kPlayers; ++i) {
                race_->fill(hud_state_[i], *hud_, i);
                const menu::GameSettings::Display& d = settings_.display;
                HudState& h = hud_state_[i];
                h.show_map = d.map, h.show_time = d.time, h.show_lap = d.lap, h.show_position = d.position, h.show_meter = d.meter;
                h.replay_display = settings_.replay_display;
                // (Under the results page the two displays are out but for their messages.)
                hud_->draw(ui, h);
            }
        }
        if (results && hud_) {
            std::vector<RaceHud::MovieExternal> ext;
            hud_->draw_movie(*result_movie_, *result_, &ext);
            std::vector<RaceWorld::Icon> icons;
            const auto& rows = race_->board().rows();
            for (const RaceHud::MovieExternal& e : ext)
                if (e.name.rfind("_EXT_CARICON", 0) == 0)
                    if (const int row = std::atoi(e.name.c_str() + 12) - 1; row >= 0 && size_t(row) < rows.size() && rows[size_t(row)].car >= 0)
                        icons.push_back({size_t(rows[size_t(row)].car), e.x, e.y, e.opacity});
            race_->world().draw_icons(ui, RaceHud::canvas(), icons);
        }
        if (paused_) {
            if (pause_ && hud_) hud_->draw_movie(*pause_movie_, *pause_);
            else {
                ui.fill(0, 0, whole.w, whole.h, Color{0, 0, 0, 120});
                ui.text_center(Ui::kHeight / 2 - 10, "PAUSED", colors::kText);
                ui.text_center(Ui::kHeight / 2 + 6, Ui::expand_prompts("{Start}: go on   {Triangle}: leave the race"), colors::kDim);
            }
        }
    }

private:
    // What a player chose, as the battle's sequence takes it (0x10C0E8, 0x3D6350): the car page's driving options, the options'
    // steering assistance and active steering of his controller.
    RaceWorld::PlayerChoice player_choice(size_t i) {
        const menu::RaceRequest& q = menus_->game().race;
        const std::map<std::string, menu::OptionValue>& o = menus_->game().options;
        const auto text = [&](const char* name) {
            const auto it = o.find(name);
            const std::string* v = it == o.end() ? nullptr : std::get_if<std::string>(&it->second);
            return v ? *v : std::string();
        };
        const auto flag = [&](const char* name) {
            const auto it = o.find(name);
            const s32* v = it == o.end() ? nullptr : std::get_if<s32>(&it->second);
            return v && *v != 0;
        };
        RaceWorld::PlayerChoice c;
        c.automatic = (i ? q.manual_2p : q.manual) == 0;
        c.asm_on = (i ? q.assist_asm_2p : q.assist_asm) != 0, c.tcs_on = (i ? q.assist_tcs_2p : q.assist_tcs) != 0;
        const std::string assist = text(i ? "steering_assist_2p" : "steering_assist_1p");
        c.wheel_lock = assist == "PROFESSIONAL" ? 1 : assist == "SIMULATION" ? 2 : 0;
        c.wheel_aid = flag(i ? "active_steering_2p" : "active_steering_1p");
        controllers_ = menu::settings_from(o).controllers;
        const input::DeviceConfig& wheel = controllers_.port[i].of(input::Device::Cheetah);
        c.wheel_strength = gt3r::s16(std::clamp(input::get_feedback(wheel, input::Device::Cheetah, "POWER"), 0, 2));
        c.wheel_assist = gt3r::s16(input::get_feedback(wheel, input::Device::Cheetah, "POWER_ASSIST"));
        return c;
    }

    bool playing(size_t i) const { return race_ && !paused_ && !setup_.auto_drive[i] && !race_->replaying() && !race_->finished(i); }
    bool playing_any() const { return playing(0) || playing(1); }
    void stop_feedback() {
        for (size_t i = 0; i < kPlayers; ++i) feedback_[i].stop(app_.input(), int(i));
    }

    // Each player's controller this frame (inputs_), and its views and menu buttons that went down.
    std::array<std::pair<u32, u32>, kPlayers> read_controls(App& app) {
        std::array<std::pair<u32, u32>, kPlayers> down{};
        for (size_t i = 0; i < kPlayers; ++i) {
            inputs_[i] = input::read_player(app.input(), int(i), controllers_.port[i], wheel_, race_->replaying() ? input::Mode::Watch : input::Mode::Drive);
            down[i] = {inputs_[i].views & ~last_views_[i], inputs_[i].menu & ~last_menu_[i]};
            last_views_[i] = inputs_[i].views, last_menu_[i] = inputs_[i].menu;
        }
        return down;
    }
    // A player's pad or wheel unplugged: the race waits.
    bool lost_device(App& app) {
        bool lost = false;
        for (size_t i = 0; i < kPlayers; ++i) {
            if (!playing(i)) continue;
            const std::string device = app.input().kind(int(i)) == gt3r::DeviceKind::Keyboard ? std::string() : app.input().device_name(int(i));
            lost = lost || (!device_[i].empty() && device != device_[i]);
            device_[i] = device;
        }
        return lost;
    }

    // ---- the pause (RacePause with its Retry on and its screen settings off: 0x32F1D8)

    void open_pause(App& app) {
        paused_ = true;
        pause_result_.clear();
        if (audio::RaceSound* sound = race_->world().sound()) sound->pause(true);
        if (!pause_movie_) pause_movie_ = load_movie(app, {"menu/common/pause.strb"});
        if (!pause_movie_) return;
        auto host = [this, &app](const std::string& name, const std::vector<race::StrobePlayer::Value>& args) {
            using V = race::StrobePlayer::Value;
            const std::string n = upper(name);
            const auto text = [&](size_t i) { return i < args.size() ? args[i].to_string() : std::string(); };
            if (n == "RETRYENABLE") return V::number(1);
            if (n == "SCREENSETTINGENABLE" || n == "CONFIRMEXIT") return V::number(0);
            if (n == "EXIT_MENU") pause_result_ = text(0);
            else if (n == "PLAY_SOUND") {
                if (audio::AppSound* sound = app.sound()) sound->menu().play(text(0));
            } else if (n == "NAVIGATEVERTICAL" && pause_) pause_->navigate(true, false);
            else if (n == "NAVIGATEALL" && pause_) pause_->navigate(true, true);
            return V{};
        };
        pause_ = std::make_unique<race::StrobePlayer>(pause_movie_, host);
    }

    void update_pause(App& app, const MenuInput& m, double dt) {
        if (!pause_) {
            if (m.pressed(PadButton::Start)) pause_result_ = "continue";
            else if (m.pressed(PadButton::Triangle)) pause_result_ = "exit";
        } else if (m.pressed(PadButton::Triangle)) {
            pause_result_ = "continue";
        } else {
            if (m.vertical()) pause_->key(m.vertical() < 0 ? 0x26 : 0x28);
            if (m.horizontal()) pause_->key(m.horizontal() < 0 ? 0x25 : 0x27);
            if (m.pressed(PadButton::Cross)) pause_->key(0x20);
            pause_->advance(float(dt));
        }
        if (pause_result_.empty()) return;
        const std::string result = std::exchange(pause_result_, {});
        paused_ = false;
        pause_.reset();
        if (audio::RaceSound* sound = race_->world().sound()) sound->pause(false);
        if (result == "retry") return again();
        if (result == "exit") return race_->replaying() ? leave(app) : run_over(app);
    }

    // The run again (end code 4).
    void again() {
        result_.reset(), result_tried_ = result_exit_ = false;
        if (menus_)
            for (size_t i = 0; i < kPlayers; ++i) race_->set_choice(i, player_choice(i));
        race_->begin_run();
        clock_ = 0;
    }

    // The run is over (the results page left, or the pause's Exit): its replay (0x10C2BC: whatever the run came to), then the menus.
    void run_over(App& app) {
        result_.reset(), result_tried_ = result_exit_ = false;
        stop_feedback();
        if (!race_->has_run()) return leave(app);
        race_->start_replay();
        clock_ = 0;
    }

    void leave(App& app) {
        if (left_) return;
        left_ = true;
        stop_feedback();
        if (race_) race_->world().quiet();
        if (menus_) {
            menu::GameState& g = menus_->game();
            if (setup_.playlist) playlist_to(*setup_.playlist, g.bgm_playlist);
            pictures_to_options(g.options);
            if (race_) g.options["view_type"] = option_of(race_->mount(0)), g.options["view_type_2p"] = option_of(race_->mount(1)); // (0x32F838)
            g.during_quick_menu = false;
            g.next_sequence.clear();
            g.last_sequence = "split_battle";
            if (g.next_menu_project.empty()) g.next_menu_project = g.major_menu_project;
            if (!menus_->finished()) menus_->finish();
        }
        app.pop();
    }

    // ---- the results (menu/common/result.strb: a row a player as he finishes)

    void open_results(App& app) {
        result_tried_ = true, result_exit_ = false;
        if (!result_movie_) result_movie_ = load_movie(app, {"menu/common/result.strb"});
        if (!result_movie_) return;
        auto host = [this, &app](const std::string& name, const std::vector<race::StrobePlayer::Value>& args) {
            using V = race::StrobePlayer::Value;
            const std::string n = upper(name);
            race::ResultBoard& board = race_->board();
            const int i = args.empty() ? -1 : int(args[0].to_number());
            const race::ResultBoard::Row* row = i >= 0 && size_t(i) < board.rows().size() ? &board.rows()[size_t(i)] : nullptr;
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
            return V::number(0);
        };
        result_ = std::make_unique<race::StrobePlayer>(result_movie_, host); // (at its frame 0: the movie plays itself on)
    }

    static std::string upper(std::string s) {
        for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }
    std::shared_ptr<const race::StrobeMovie> load_movie(App& app, std::initializer_list<const char*> paths) {
        for (const char* path : paths) try {
                if (const std::optional<Bytes> file = app.vfs()->read(path); file && race::StrobeMovie::is_movie(*file))
                    return std::make_shared<const race::StrobeMovie>(race::StrobeMovie::parse(*file));
            } catch (const std::exception& e) {
                LOG_WARN("Split: %s: %s", path, e.what());
            }
        return nullptr;
    }

    App& app_;
    Setup setup_;
    menu::Runtime* menus_ = nullptr;
    menu::GameSettings settings_;
    std::unique_ptr<SplitRace> race_;
    std::unique_ptr<RaceHud> hud_;
    std::array<HudState, kPlayers> hud_state_;
    std::string error_;
    input::Controllers controllers_;
    input::WheelSettings wheel_;
    std::array<input::RaceInput, kPlayers> inputs_;
    std::array<u32, kPlayers> last_views_{}, last_menu_{};
    std::array<input::RaceFeedback, kPlayers> feedback_;
    bool keys_lent_ = false;
    std::array<std::string, kPlayers> device_;
    std::shared_ptr<const race::StrobeMovie> pause_movie_, result_movie_;
    std::unique_ptr<race::StrobePlayer> pause_, result_;
    std::string pause_result_;
    bool paused_ = false, result_tried_ = false, result_exit_ = false, left_ = false;
    double clock_ = 0;
    gt3r::race::FramePacer pacer_;
};

} // namespace

std::unique_ptr<Screen> make_split_screen(App& app, const RaceStart* chosen) { return std::make_unique<SplitScreen>(app, chosen); }

} // namespace gt4r
