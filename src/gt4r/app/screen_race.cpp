// Drive: pick a car and a course, then a race on the course's road: the first car driven with
// the pad, the others by a simple driver, all on the simulation behind race/simulation.h.
//
// GT4R_RACE="course=tsukuba,car=<label>,cars=6,view=chase,drive=auto,skip=600,paint=1,hud=1"
// sets it up without the lists, for pictures: `course` a course file's name, `car` a car's label
// (or its model's name), `cars` how many on the grid, `view` in | chase | roof | back, `drive`
// auto (the simple driver takes the first car too) or pad, `skip` simulation steps run before the
// first picture (the start's countdown is left out then), `paint` the first car's paint, `laps` the
// race's length; `view=tv` the course's replay cameras; `replay=N` drives N steps unseen and plays
// them back through the replay cameras.

#include "gt4r/app/app.h"
#include "gt4r/app/race_hud.h"

#include "base/log.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/race/car_spec.h"
#include "gt4r/race/car_visual.h"
#include "gt4r/race/course_scene.h"
#include "gt4r/race/drive_camera.h"
#include "gt4r/race/rail_simulation.h"
#include "gt4r/race/replay_script.h"
#include "race/billboards.h"
#include "race/controls.h"
#include "race/lamp_glows.h"
#include "race/sun_disc.h"
#include "race/sun_glare.h"
#include "race/tyre_smoke.h"
#include "race/wet_road.h"
#include "render/view3d.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace gt4r {

namespace {

namespace render = gt3r::render;
using RVec = render::Vec3;

constexpr float kStep = 1.0f / 60;
constexpr float kPi = 3.14159265f;
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
        else if (k == "laps") s.laps = std::clamp(std::atoi(v.c_str()), 1, 99);
    }
    return s;
}

// The race itself: the course, the cars, the simulation and the view.
class Race {
public:
    Race(App& app, const Setup& setup, const std::vector<db::CarEntry>& cars) : setup_(setup) {
        render::GpuContext& gpu = *app.gpu();
        const std::optional<Bytes> course = app.vfs()->read("crs/" + setup.course);
        if (!course || !course::CourseFile::looks_like_course(*course)) throw FormatError("There is no course file crs/" + setup.course + ".");
        course_ = std::make_unique<race::CourseScene>(gpu, *course, setup.course);
        if (course_->road().checkpoints.size() < 3) throw FormatError("This place has no lap to drive.");
        const u32 flags = course_->section(course::kSlotEnv).size() >= 0x14 ? rd32(course_->section(course::kSlotEnv), 0x10) : 0;
        night_ = (flags & 0x8000) != 0;

        // The first car is the one asked for; the others follow it in the car list.
        size_t first = 0;
        for (size_t i = 0; i < cars.size(); ++i)
            if (cars[i].label == setup.car || cars[i].model == setup.car) first = i;
        std::vector<race::RailCar> rails;
        for (int i = 0; i < setup.cars; ++i) {
            const db::CarEntry& entry = cars[(first + size_t(i) * 37) % cars.size()];
            const race::CarSpec spec = race::read_car_spec(*app.db(), entry.label);
            std::unique_ptr<race::CarVisual>& visual = visuals_by_model_[entry.model + "#" + std::to_string(i == 0 ? setup.paint : u32(i))];
            if (!visual) {
                const std::optional<Bytes> file = app.vfs()->read("car/lod/" + entry.model);
                if (!file || !car::CarFile::is_car(*file)) throw FormatError("This car has no race model on the disc (" + entry.model + ").");
                const std::optional<Bytes> pat = app.vfs()->read("car/lod/" + entry.model + ".pat");
                const std::vector<u32> paints = race::car_paints(*app.db(), entry.label);
                race::CarVisual::Options o;
                const u32 pick = i == 0 ? setup.paint : u32(i);
                o.paint = paints.empty() ? pick : paints[pick % paints.size()];
                o.lamp_level = night_ ? 1 : 0;
                visual = std::make_unique<race::CarVisual>(gpu, *file, pat ? ByteSpan(*pat) : ByteSpan{}, spec, o, entry.label);
                // The course's strengths of the three lamp lists (Env0 +0x1794: lights, brake, tail).
                if (const ByteSpan e0 = course_->section(course::kSlotEnv); e0.size() >= 0x17A0) {
                    float v[3];
                    std::memcpy(v, e0.data() + 0x1794, 12);
                    visual->set_lamp_strengths(v[0], v[1], v[2]);
                }
                const gt3r::race::SunParams& env = course_->env();
                if (env.has_shadow) {
                    race::CarVisual::Shadow sh;
                    sh.on = true;
                    for (int k = 0; k < 3; ++k) sh.rgb[k] = env.shadow_rgb[size_t(k)];
                    sh.alpha = env.shadow_alpha;
                    // (The factor of a car in full light is the picture's scale on every course looked
                    // at, 2.0: taken as a strength in the half-scale picture's terms.)
                    sh.light_factor = env.shadow_f_lit / std::max(course_->color_scale(), 1.0f);
                    // Where the light throws a point a metre up: along the shadow's direction
                    // (elevation and azimuth in degrees), its length per metre of height.
                    const float el = -env.shadow_elev * kPi / 180, az = -env.shadow_az * kPi / 180;
                    sh.shear = RVec{std::sin(az), 0, std::cos(az)} * (env.shadow_len * std::cos(el));
                    sh.headlamps = night_ ? env.headlamp_pool : 0.0f;
                    visual->set_shadow(sh);
                }
            }
            car_visuals_.push_back(visual.get());
            names_.push_back(entry.name.empty() ? entry.label : entry.name);
            rails.push_back(race::rail_car(spec));
        }
        rail_ = std::make_unique<race::RailSimulation>(course_->road(), std::move(rails));
        sim_ = rail_.get();
        inputs_.resize(size_t(setup.cars));
        camera_.reset(sim_->cars()[0]);
        // The count before the start: three seconds (a scripted run that skips ahead has none).
        started_with_count_ = setup.skip == 0;
        countdown_ = started_with_count_ ? 3 * 60 : 0;
        for (int i = 0; i < setup.skip; ++i) step(gt3r::PadState{}, true);
        view_ = std::make_unique<render::View3D>(gpu);
        mount_ = setup.view;
        if (const ByteSpan rep = course_->section(course::kSlotReplay); race::ReplayScript::is_script(rep)) {
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
        // The sun, the lamps' glows (their meshes are the course's own, slot 37) and the billboards:
        // the shared engine's layouts, read by its own code.
        const ByteSpan env0 = course_->section(course::kSlotEnv);
        sun_.load(*app.vfs(), app.renderer(), &gpu, env0);
        sun_disc_.load(gpu, sun_.params());
        lamps_.load(app.renderer(), &gpu, course_->section(course::kSlotEffects), env0, course_->section(course::kSlotWorldVis),
                    course_->section(course::kSlotFlareShape));
        billboards_.load(gpu, course_->section(course::kSlotBillboards), setup.course);
        car_lamps_.load(app.renderer(), &gpu, env0, course_->section(course::kSlotFlareShape));
        if (setup.reflection && !course_->reflection_course().empty()) wet_.load(gpu, *app.vfs(), course_->reflection_course(), env0);
        if (setup.smoke) smoke_.load(gpu, env0, course_->section(course::kSlotSmoke));
        LOG_INFO("Drive: %zu lamps, %zu billboards, sun %s", lamps_.count(), billboards_.count(), sun_.params().enabled ? "on" : "off");
    }

    // One simulation step: the first car from the pad (or the simple driver), the others by the
    // simple driver, each on its own side of the line.
    void step(const gt3r::PadState& pad, bool auto_drive) {
        if (replaying_) {
            // The recorded run again, from its start when it ends.
            replay_at_ = (replay_at_ + 1) % history_.size();
            if (replay_at_ == 0) camera_.reset(history_[0][0]), replay_camera_.reset(), smoke_.clear();
            camera_.step(history_[replay_at_][0], kStep);
            puff(history_[replay_at_]);
            return;
        }
        const std::span<const race::CarState> cars = sim_->cars();
        // The start: the cars stand through the count.
        if (countdown_ > 0) {
            --countdown_;
            mapper_.update(pad, kStep);
            std::fill(inputs_.begin(), inputs_.end(), race::CarInput{});
            for (race::CarInput& in : inputs_) in.brake = 1;
            sim_->step(inputs_, kStep);
            return;
        }
        auto_drive = auto_drive || finished_;
        for (size_t c = 0; c < inputs_.size(); ++c)
            inputs_[c] = race::follow_line(rail_->line(), cars[c], c & 1 ? 2.0f : -2.0f, 1.0f - 0.05f * float(c));
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
        sim_->step(inputs_, kStep);
        course_->animate(kStep, sim_->cars());
        camera_.step(sim_->cars()[0], kStep);
        ++steps_;
        // The player's laps: a lap's time runs from one crossing of the line to the next (the
        // first from the start).
        const int lap = sim_->cars()[0].lap;
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
        if (history_.size() < kHistory) history_.emplace_back(sim_->cars().begin(), sim_->cars().end());
        puff(sim_->cars());
        // Five seconds after the finish the run is shown again.
        if (finished_ && finish_steps_ > 5 * 60) start_replay();
    }

    // Smoke and dust from the wheels for a step: a puff where a tire slides on the road, dust and
    // spray from any wheel that turns on a loose or wet surface. The kind of puff is the course's,
    // by the surface under the wheel.
    void puff(std::span<const race::CarState> cars) {
        if (!smoke_.loaded()) return;
        const course::Runway& road = course_->road();
        for (size_t c = 0; c < cars.size(); ++c) {
            const float radius = car_visuals_[c]->spec().tire_radius[0];
            for (int w = 0; w < 4; ++w) {
                const race::WheelState& ws = cars[c].wheels[w];
                if (!ws.on_ground) continue;
                const RVec contact{ws.center.x - cars[c].up.x * radius, ws.center.y - cars[c].up.y * radius, ws.center.z - cars[c].up.z * radius};
                float y;
                int tri = -1;
                int surface = 0;
                if (road.height_at(contact.x, contact.z, contact.y + 1.0f, y, &tri) && tri >= 0) {
                    const course::Runway::Triangle& t = road.triangles[size_t(tri)];
                    surface = t.bits & 0x1F;
                    if ((t.kind & 2) && surface == 0) surface = 12; // the wet road
                }
                const float speed = std::fabs(ws.spin_rate) * radius;
                float amount = std::max(0.0f, ws.slip - 0.6f) * 1.25f;
                if (surface != 0) amount = std::max(amount, std::min(0.25f, speed / 120.0f) * (w >= 2 ? 1.0f : 0.5f));
                smoke_.emit(contact, surface, amount, 1.0f, (w & 1) != 0);
            }
        }
        smoke_.update(kStep);
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
    std::span<const race::CarState> shown() const { return replaying_ ? std::span<const race::CarState>(history_[replay_at_]) : sim_->cars(); }

    // What the race display shows now.
    void fill(HudState& h, const RaceHud& hud) const {
        const std::span<const race::CarState> cars = shown();
        h.replay = replaying_;
        const race::CarState& me = cars[0];
        const race::CarSpec& spec = car_visuals_[0]->spec();
        h.speed = me.speed, h.rpm = me.engine_rpm, h.gear = me.gear;
        h.red_line = spec.red_line, h.rev_limit = spec.rev_limit;
        h.driver_view = mount_ != race::DriveCamera::kChase && mount_ != kTvView;
        // The place: by laps and distance along the course.
        const float length = rail_->line().length;
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
        const render::Mat4 body = car_visuals_[0]->body_matrix(me);
        race::ViewPose pose;
        // The replay cameras' shot for the car; the chase view where the script has none.
        if (mount_ != kTvView || !replay_camera_.pose(replay_script_, me, body, pose))
            pose = camera_.pose(me, body, car_visuals_[0]->mounts(), mount_ == kTvView ? race::DriveCamera::kChase : mount_, look_back || setup_.look_back);
        const float aspect = rect.w / std::max(rect.h, 1.0f);
        const gt3r::race::SunParams& env = course_->env();
        const float far = env.has_env && env.far_clip > 0 ? env.far_clip : 5000.0f;
        // (The backdrop's far plane lies a hundred times as far as the scenery's.)
        const render::Mat4 proj = render::perspective(pose.fovy(), aspect, 0.1f, far * 100.0f);

        course_->upload_animations(cars);
        fade_.update(course_->visible_nodes(pose.eye), pose.eye, kStep);
        items_.clear();
        // The view's axes in course space (the rows of the view matrix's turn).
        const RVec cam_right{pose.view.at(0, 0), pose.view.at(0, 1), pose.view.at(0, 2)}, cam_up{pose.view.at(1, 0), pose.view.at(1, 1), pose.view.at(1, 2)};
        course_->add_items(items_, pose.eye, race::CourseScene::View::Race, &fade_);
        if (sun_disc_.active()) {
            // The sun's disc right after the sky (the first item), before the backdrop.
            std::vector<render::DrawItem> disc;
            sun_disc_.add_items(disc, pose.eye, cam_right, cam_up);
            for (render::DrawItem& it : disc) it.background = false, it.in_order = true, it.no_fog = true;
            items_.insert(items_.begin() + 1, disc.begin(), disc.end());
        }
        if (billboards_.count() && (course_->env().view_flags[0] & 1)) {
            billboards_.add_items(items_, cam_right, fade_.solid());
            for (const gt3r::race::SceneryFade::Group& g : fade_.faded())
                if (!g.nodes.empty()) billboards_.add_items(items_, cam_right, &g.nodes, g.weight);
        }
        // The road's reflection: the mirrored scenery and its sky drawn into a small picture, which
        // goes on the road (where the road's mask lets it) after the scenery and before the cars.
        if (wet_.active()) {
            wet_items_.clear();
            course_->add_reflection_sky_items(wet_items_, pose.eye);
            wet_.add_buffer_items(wet_items_, pose.view, pose.eye, {});
            for (render::DrawItem& it : wet_items_) it.tint = course_->reflection_gain();
            const render::DrawOptions wo = draw_options(far);
            if (SDL_GPUTexture* t = wet_.render(*view_, pose.view, proj, pose.eye, wet_items_, wo)) wet_.add_frame_items(items_, pose.view, pose.fovy(), aspect, t);
        }
        for (size_t i = 0; i < cars.size(); ++i) car_visuals_[i]->add_shadow_items(items_, cars[i]);
        for (size_t i = 0; i < cars.size(); ++i) {
            if (i == 0 && pose.body_hidden) continue;
            car_visuals_[i]->add_items(items_, cars[i], pose.eye, kStep);
        }
        course_->add_after_items(items_);
        if (smoke_.loaded()) {
            // The puffs after everything else of the scene, facing the camera.
            gt3r::race::TyreSmoke::View sv;
            sv.forward = RVec{-pose.view.at(2, 0), -pose.view.at(2, 1), -pose.view.at(2, 2)};
            sv.proj_x = 1.0f / (std::tan(pose.fovy() * 0.5f) * std::max(aspect, 1e-3f));
            sv.near_m = 0.1f, sv.far_m = far;
            smoke_.add_items(items_, pose.eye, cam_right, cam_up, sv);
        }

        render::DrawOptions o = draw_options(far);
        // The cars' reflection picture: the course around the first car, refreshed a face at a time.
        if (view_) {
            env_items_.clear();
            const RVec at{me.position.x, me.position.y + 0.8f, me.position.z};
            const render::Mat4 sky_rot = render::rotate_x(env.sky_pitch * kPi / 180) * render::rotate_y(env.sky_yaw * kPi / 180);
            if (course_->add_env_items(env_items_, at, sky_rot)) {
                render::DrawOptions eo = o;
                eo.env_cube = nullptr;
                eo.draw_distance = 200, eo.far_clip = 0;
                eo.fog = false;
                eo.quantize_rgb5 = true;
                for (float& c : eo.clear) c = 0;
                eo.clear[3] = 1;
                const unsigned faces = env_ready_ ? 1u << (frame_ % 6) : 0x3Fu;
                if (view_->render_env_cube(at, env_items_, eo, 128, faces)) o.env_cube = view_->env_cube(), o.env_gt3_lookup = true, env_ready_ = true;
            }
        }
        // After the scene: the sun's glare by how much of it the scenery leaves in sight, and the
        // lamps' glows.
        overlay_.clear();
        overlay_.rect = rect;
        const render::Frustum frustum = render::Frustum::symmetric(pose.fovy(), aspect);
        sun_.add(overlay_, rect, pose.view, frustum, sun_visibility(pose.eye, pose.fovy()));
        const gt3r::race::Occluder& occluder = course_->occluder();
        lamps_.tick(kStep);
        lamps_.add(overlay_, rect, pose.view, pose.eye, frustum, [&](const RVec& a, const RVec& b) { return occluder.segment_blocked(a, b); }, &proj);
        if (car_lamps_.active()) {
            // The cars' lamps at night: the lights, the tail lamps, the brake lamps while braking.
            glow_cars_.clear();
            for (size_t i = 0; i < cars.size(); ++i) {
                if (i == 0 && pose.body_hidden) continue;
                gt3r::race::CarLampGlows::Car g;
                g.lamps = &car_visuals_[i]->lamps();
                g.model = car_visuals_[i]->body_matrix(cars[i]);
                g.k = cars[i].brake_lamps ? 1.0f : 0.0f;
                g.brake_now = cars[i].brake_lamps;
                glow_cars_.push_back(g);
            }
            car_lamps_.add(overlay_, rect, pose.view, pose.eye, frustum, glow_cars_, [&](const RVec& a, const RVec& b) { return occluder.segment_blocked(a, b); }, &proj);
        }
        o.overlay = &overlay_;
        view_->draw(ui.renderer(), rect, pose.view, proj, pose.eye, items_, o);
        if (frame_ == 5)
            LOG_INFO("Drive: the car at (%.1f, %.1f, %.1f) heading (%.2f, %.2f), eye (%.1f, %.1f, %.1f); %d meshes drawn, %zu triangles", me.position.x,
                     me.position.y, me.position.z, me.forward.x, me.forward.z, pose.eye.x, pose.eye.y, pose.eye.z, view_->stats().meshes_drawn,
                     view_->stats().triangles);
        ++frame_;
    }

    const race::CarState& player() const { return shown()[0]; }
    const std::string& car_name(size_t i) const { return names_[i]; }
    const race::CourseScene& course() const { return *course_; }
    const race::CarVisual& visual(size_t i) const { return *car_visuals_[i]; }
    const race::RailLine& line() const { return rail_->line(); }
    int steps() const { return steps_; }
    int mount() const { return mount_; }
    const render::DrawStats& draw_stats() const { return view_->stats(); }

private:
    // How much of the sun is in sight: rays through the box the game tests around it, against the
    // course's opaque triangles.
    float sun_visibility(const RVec& eye, float fovy) const {
        const gt3r::race::SunParams& sp = sun_.params();
        if (!sp.enabled || course_->occluder().empty()) return 1.0f;
        const RVec d = sp.direction();
        const float half = std::max(1.0f, sp.test_box) * 0.5f * fovy / 448.0f;
        const RVec r = render::normalize(render::cross(d, {0, 1, 0})), u = render::cross(r, d);
        int open = 0;
        for (int i = -1; i <= 1; ++i)
            for (int j = -1; j <= 1; ++j)
                open += course_->occluder().blocked(eye, render::normalize(d + r * (float(i) * half) + u * (float(j) * half)), 6000.0f) ? 0 : 1;
        return float(open) / 9.0f;
    }

    render::DrawOptions draw_options(float far) const {
        const gt3r::race::SunParams& env = course_->env();
        render::DrawOptions o;
        o.frustum_cull = true;
        o.sort_blended = true;
        o.far_clip = far;
        o.guard_near = 0.07f;
        for (int i = 0; i < 3; ++i) o.clear[i] = 0;
        if (env.has_env) {
            // The course's fog: visibility = b + (1 - b) min(1, A / z) toward the fog color.
            o.fog = true;
            o.fog_gt3 = true;
            o.fog_start = std::max(env.fog_a, 0.0f);
            o.fog_max = std::clamp(1.0f - env.fog_b, 0.0f, 1.0f);
            for (int i = 0; i < 3; ++i) o.fog_color[i] = std::clamp(env.fog_rgb[size_t(i)], 0.0f, 1.0f);
            // The cars' lights and the panoramic reflection's factors.
            o.gt3_lighting = true;
            if (env.car_light_mask) env.day_car_lights(o);
            o.panorama[0] = env.panorama[0], o.panorama[1] = env.panorama[1];
        }
        const float* sky = course_->sky_color();
        const float* ground = course_->ground_color();
        for (int i = 0; i < 3; ++i) o.env_sky[i] = sky[i], o.env_horizon[i] = ground[i], o.env_ground[i] = ground[i];
        o.reflection_fresnel = 0.8f;
        o.reflection_strength = 1.0f;
        return o;
    }

    Setup setup_;
    std::unique_ptr<race::CourseScene> course_;
    std::map<std::string, std::unique_ptr<race::CarVisual>> visuals_by_model_;
    std::vector<race::CarVisual*> car_visuals_;
    std::vector<std::string> names_;
    std::unique_ptr<race::RailSimulation> rail_;
    race::Simulation* sim_ = nullptr;
    std::vector<race::CarInput> inputs_;
    gt3r::race::ControlMapper mapper_;
    race::DriveCamera camera_;
    race::ReplayScript replay_script_;
    race::ReplayCamera replay_camera_;
    std::vector<std::vector<race::CarState>> history_;
    bool replaying_ = false;
    size_t replay_at_ = 0;
    gt3r::race::SceneryFade fade_;
    std::unique_ptr<render::View3D> view_;
    std::vector<render::DrawItem> items_, env_items_;
    gt3r::race::SunGlare sun_;
    gt3r::race::SunDisc sun_disc_;
    gt3r::race::LampGlows lamps_;
    gt3r::race::Billboards billboards_;
    gt3r::race::CarLampGlows car_lamps_;
    std::vector<gt3r::race::CarLampGlows::Car> glow_cars_;
    gt3r::race::TyreSmoke smoke_;
    gt3r::race::WetRoad wet_;
    std::vector<render::DrawItem> wet_items_;
    render::ScreenBatch overlay_;
    int countdown_ = 0, lap_start_ = 0, event_steps_ = -1, event_lap_ = 0, finish_steps_ = 0;
    bool started_with_count_ = false, finished_ = false;
    s64 best_ms_ = -1, last_ms_ = -1;
    bool night_ = false, env_ready_ = false;
    int mount_ = race::DriveCamera::kChase;
    int steps_ = 0, frame_ = 0;
};

class RaceScreen : public Screen {
public:
    explicit RaceScreen(App& app) : setup_(setup_from_env()) {
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
        if (m.pressed(PadButton::Start)) paused_ = !paused_;
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

} // namespace gt4r
