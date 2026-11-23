#include "gt4r/app/race_world.h"

#include "base/log.h"
#include "gt4r/audio/race_sound.h"
#include "gt4r/audio/app_sound.h"
#include "gt4r/audio/race_tables.h"
#include "audio/audio_engine.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/race/car_spec.h"
#include "gt4r/sim/session.h"
#include "race/billboards.h"
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

} // namespace

struct RaceWorld::Impl {
    std::map<std::string, std::unique_ptr<race::CarVisual>> visuals_by_model;
    std::unique_ptr<audio::RaceSound> sound;
    gt3r::race::SceneryFade fade;
    std::unique_ptr<render::View3D> view;
    std::vector<render::DrawItem> items, env_items, wet_items;
    gt3r::race::SunGlare sun;
    gt3r::race::SunDisc sun_disc;
    gt3r::race::LampGlows lamps;
    gt3r::race::Billboards billboards;
    gt3r::race::CarLampGlows car_lamps;
    std::vector<gt3r::race::CarLampGlows::Car> glow_cars;
    gt3r::race::TyreSmoke smoke;
    gt3r::race::WetRoad wet;
    render::ScreenBatch overlay;
    bool env_ready = false;
    int frame = 0;
};

RaceWorld::RaceWorld(App& app, const WorldSetup& setup) : app_(app), setup_(setup), computer_player_(setup.computer_player), impl_(std::make_unique<Impl>()) {
    Impl& m = *impl_;
    render::GpuContext& gpu = *app.gpu();
    const std::optional<Bytes> course = app.vfs()->read("crs/" + setup.course);
    if (!course || !course::CourseFile::looks_like_course(*course)) throw FormatError("There is no course file crs/" + setup.course + ".");
    course_ = std::make_unique<race::CourseScene>(gpu, *course, setup.course);
    if (course_->road().checkpoints.size() < 3) throw FormatError("This place has no lap to drive.");
    if (setup.entries.empty()) throw FormatError("A race needs a car.");
    const u32 flags = course_->section(course::kSlotEnv).size() >= 0x14 ? rd32(course_->section(course::kSlotEnv), 0x10) : 0;
    night_ = (flags & 0x8000) != 0;

    const std::vector<db::CarEntry>& cars = app.db()->cars();
    for (const WorldEntry& e : setup.entries) {
        const db::CarEntry* entry = nullptr;
        for (const db::CarEntry& c : cars)
            if (c.label == e.label) entry = &c;
        if (!entry) throw FormatError("The car database has no car " + e.label + ".");
        const race::CarSpec spec = race::read_car_spec(*app.db(), entry->label);
        std::unique_ptr<race::CarVisual>& visual = m.visuals_by_model[entry->model + "#" + std::to_string(e.paint)];
        if (!visual) {
            const std::optional<Bytes> file = app.vfs()->read("car/lod/" + entry->model);
            if (!file || !car::CarFile::is_car(*file)) throw FormatError("This car has no race model on the disc (" + entry->model + ").");
            const std::optional<Bytes> pat = app.vfs()->read("car/lod/" + entry->model + ".pat");
            const std::vector<u32> paints = race::car_paints(*app.db(), entry->label);
            race::CarVisual::Options o;
            o.paint = paints.empty() ? e.paint : paints[e.paint % paints.size()];
            o.lamp_level = night_ ? 1 : 0;
            visual = std::make_unique<race::CarVisual>(gpu, *file, pat ? ByteSpan(*pat) : ByteSpan{}, spec, o, entry->label);
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
        names_.push_back(entry->name.empty() ? entry->label : entry->name);
    }
    line_ = race::RailLine(course_->road());
    build_simulation();
    // The race's sound: every car, the course's ambience, the count and the music.
    if (audio::AppSound* sound = app.sound()) {
        gt3r::AudioEngine& engine = app.audio();
        gt3r::Vfs& vfs = *app.vfs();
        audio::StreamPlayer music;
        music.play = [&engine, &vfs](const std::string& path, bool repeat, float level) { return engine.play_music(vfs, path, repeat, level); };
        music.stop = [&engine] { engine.stop_music(); };
        music.pause = [&engine](bool paused) { paused ? engine.pause_music() : engine.resume_music(); };
        music.playing = [&engine] { return engine.music_playing(); };
        const Bytes program = app.disc() ? app.disc()->read_program() : Bytes{};
        m.sound = std::make_unique<audio::RaceSound>(vfs, engine.voices(), sound->driver(), sound->library_ptr(), audio::RaceSoundTables::from_core(program), music);
        for (const WorldEntry& e : setup.entries)
            if (!m.sound->add_car(e.label)) LOG_WARN("Drive: no engine sound for %s", e.label.c_str());
        m.sound->set_course(course_->section(course::kSlotSound), std::make_shared<const course::Runway>(course_->road()));
    }
    m.view = std::make_unique<render::View3D>(gpu);
    // The sun, the lamps' glows (their meshes are the course's own, slot 37) and the billboards:
    // the shared engine's layouts, read by its own code.
    const ByteSpan env0 = course_->section(course::kSlotEnv);
    m.sun.load(*app.vfs(), app.renderer(), &gpu, env0);
    m.sun_disc.load(gpu, m.sun.params());
    m.lamps.load(app.renderer(), &gpu, course_->section(course::kSlotEffects), env0, course_->section(course::kSlotWorldVis),
                 course_->section(course::kSlotFlareShape));
    m.billboards.load(gpu, course_->section(course::kSlotBillboards), setup.course);
    m.car_lamps.load(app.renderer(), &gpu, env0, course_->section(course::kSlotFlareShape));
    if (setup.reflection && !course_->reflection_course().empty()) m.wet.load(gpu, *app.vfs(), course_->reflection_course(), env0);
    if (setup.smoke) m.smoke.load(gpu, env0, course_->section(course::kSlotSmoke));
    LOG_INFO("Drive: %zu lamps, %zu billboards, sun %s", m.lamps.count(), m.billboards.count(), m.sun.params().enabled ? "on" : "off");
}

RaceWorld::~RaceWorld() = default;

// The cars move by the simulation; the stand-in serves when it cannot be set up for this disc,
// course or car (and when asked for).
void RaceWorld::build_simulation() {
    sim_ = nullptr;
    field_.reset();
    session_.reset(), rail_.reset();
    if (!setup_.rail && app_.disc()) {
        try {
            const sim::Program program = sim::Program::load(*app_.disc());
            std::vector<sim::Entry> entries(setup_.entries.size());
            for (size_t i = 0; i < entries.size(); ++i) {
                if (!sim::stock_car(*app_.db(), program, setup_.entries[i].label, entries[i].equipment)) throw std::runtime_error("no parts for " + setup_.entries[i].label);
                entries[i].grid_place = setup_.entries[i].grid_place;
                entries[i].automatic = setup_.entries[i].automatic;
            }
            session_ = std::make_unique<sim::Session>(*app_.db(), program, std::make_shared<const course::Runway>(course_->road()), entries);
            if (!noted_)
                for (const std::string& note : session_->notes()) LOG_INFO("Drive: %s", note.c_str());
            sim_ = session_.get();
            // The computer drivers: every car but the player's (his too when he watches).
            std::vector<size_t> computer;
            for (size_t i = computer_player_ ? 0 : 1; i < setup_.entries.size(); ++i) computer.push_back(i);
            field_.reset();
            if (!computer.empty()) {
                try {
                    field_ = sim::ai::Field::open(*app_.disc(), setup_.course, *session_, computer, setup_.pace);
                } catch (const std::exception& e) {
                    if (!noted_) LOG_WARN("Drive: no computer drivers (%s); the simple driver drives", e.what());
                }
            }
        } catch (const std::exception& e) {
            if (!noted_) LOG_WARN("Drive: the simulation could not be set up (%s); the cars follow the road's line", e.what());
            session_.reset();
        }
    }
    if (!sim_) {
        std::vector<race::RailCar> rails;
        for (const race::CarVisual* v : car_visuals_) rails.push_back(race::rail_car(v->spec()));
        rail_ = std::make_unique<race::RailSimulation>(course_->road(), std::move(rails));
        sim_ = rail_.get();
    }
    noted_ = true;
}

void RaceWorld::restart() {
    build_simulation();
    course_->restart_animations();
    clear_effects();
}

void RaceWorld::step(std::span<const race::CarInput> inputs, bool hold) {
    // The computer drivers write their own cars' inputs.
    std::vector<race::CarInput> driven;
    if (field_) {
        driven.assign(inputs.begin(), inputs.end());
        driven.resize(car_visuals_.size());
        hold ? field_->hold(driven) : field_->drive(driven);
        inputs = driven;
    }
    if (hold && session_) session_->hold_once(inputs);
    else if (hold) {
        // The stand-in has no hold: the cars stand on their brakes.
        std::vector<race::CarInput> held(inputs.begin(), inputs.end());
        held.resize(car_visuals_.size());
        for (race::CarInput& in : held) in = {}, in.brake = 1;
        sim_->step(held, kStep);
    } else sim_->step(inputs, kStep);
    if (!hold) course_->animate(kStep, sim_->cars());
}

// Smoke and dust from the wheels for a step: a puff where a tire slides on the road, dust and
// spray from any wheel that turns on a loose or wet surface. The kind of puff is the course's,
// by the surface under the wheel.
void RaceWorld::present(std::span<const race::CarState> cars, const audio::SoundListener* camera, int target, bool live) {
    Impl& m = *impl_;
    if (camera && m.sound) {
        if (live && session_) m.sound->update(*session_, *camera, target);
        else m.sound->update(cars, *camera, target);
    }
    if (!m.smoke.loaded()) return;
    const course::Runway& road = course_->road();
    for (size_t c = 0; c < cars.size() && c < car_visuals_.size(); ++c) {
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
            m.smoke.emit(contact, surface, amount, 1.0f, (w & 1) != 0);
        }
    }
    m.smoke.update(kStep);
}

void RaceWorld::clear_effects() { impl_->smoke.clear(); }

void RaceWorld::quiet() {
    if (impl_->sound) impl_->sound->stop();
}

audio::RaceSound* RaceWorld::sound() { return impl_->sound.get(); }

const gt3r::render::DrawStats& RaceWorld::draw_stats() const { return impl_->view->stats(); }

namespace {

render::DrawOptions draw_options(const race::CourseScene& course, float far) {
    const gt3r::race::SunParams& env = course.env();
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
    const float* sky = course.sky_color();
    const float* ground = course.ground_color();
    for (int i = 0; i < 3; ++i) o.env_sky[i] = sky[i], o.env_horizon[i] = ground[i], o.env_ground[i] = ground[i];
    o.reflection_fresnel = 0.8f;
    o.reflection_strength = 1.0f;
    return o;
}

// How much of the sun is in sight: rays through the box the game tests around it, against the
// course's opaque triangles.
float sun_visibility(const race::CourseScene& course, const gt3r::race::SunParams& sp, const RVec& eye, float fovy) {
    if (!sp.enabled || course.occluder().empty()) return 1.0f;
    const RVec d = sp.direction();
    const float half = std::max(1.0f, sp.test_box) * 0.5f * fovy / 448.0f;
    const RVec r = render::normalize(render::cross(d, {0, 1, 0})), u = render::cross(r, d);
    int open = 0;
    for (int i = -1; i <= 1; ++i)
        for (int j = -1; j <= 1; ++j)
            open += course.occluder().blocked(eye, render::normalize(d + r * (float(i) * half) + u * (float(j) * half)), 6000.0f) ? 0 : 1;
    return float(open) / 9.0f;
}

} // namespace

void RaceWorld::render(Ui& ui, const SDL_FRect& rect, std::span<const race::CarState> cars, const race::ViewPose& pose, size_t focus, int hidden) {
    Impl& m = *impl_;
    const race::CarState& me = cars[std::min(focus, cars.size() - 1)];
    const float aspect = rect.w / std::max(rect.h, 1.0f);
    const gt3r::race::SunParams& env = course_->env();
    const float far = env.has_env && env.far_clip > 0 ? env.far_clip : 5000.0f;
    // (The backdrop's far plane lies a hundred times as far as the scenery's.)
    const render::Mat4 proj = render::perspective(pose.fovy(), aspect, 0.1f, far * 100.0f);

    course_->upload_animations(cars);
    m.fade.update(course_->visible_nodes(pose.eye), pose.eye, kStep);
    m.items.clear();
    // The view's axes in course space (the rows of the view matrix's turn).
    const RVec cam_right{pose.view.at(0, 0), pose.view.at(0, 1), pose.view.at(0, 2)}, cam_up{pose.view.at(1, 0), pose.view.at(1, 1), pose.view.at(1, 2)};
    course_->add_items(m.items, pose.eye, race::CourseScene::View::Race, &m.fade);
    if (m.sun_disc.active()) {
        // The sun's disc right after the sky (the first item), before the backdrop.
        std::vector<render::DrawItem> disc;
        m.sun_disc.add_items(disc, pose.eye, cam_right, cam_up);
        for (render::DrawItem& it : disc) it.background = false, it.in_order = true, it.no_fog = true;
        m.items.insert(m.items.begin() + 1, disc.begin(), disc.end());
    }
    if (m.billboards.count() && (course_->env().view_flags[0] & 1)) {
        m.billboards.add_items(m.items, cam_right, m.fade.solid());
        for (const gt3r::race::SceneryFade::Group& g : m.fade.faded())
            if (!g.nodes.empty()) m.billboards.add_items(m.items, cam_right, &g.nodes, g.weight);
    }
    // The road's reflection: the mirrored scenery and its sky drawn into a small picture, which
    // goes on the road (where the road's mask lets it) after the scenery and before the cars.
    if (m.wet.active()) {
        m.wet_items.clear();
        course_->add_reflection_sky_items(m.wet_items, pose.eye);
        m.wet.add_buffer_items(m.wet_items, pose.view, pose.eye, {});
        for (render::DrawItem& it : m.wet_items) it.tint = course_->reflection_gain();
        const render::DrawOptions wo = draw_options(*course_, far);
        if (SDL_GPUTexture* t = m.wet.render(*m.view, pose.view, proj, pose.eye, m.wet_items, wo)) m.wet.add_frame_items(m.items, pose.view, pose.fovy(), aspect, t);
    }
    const size_t n = std::min(cars.size(), car_visuals_.size());
    for (size_t i = 0; i < n; ++i) car_visuals_[i]->add_shadow_items(m.items, cars[i]);
    for (size_t i = 0; i < n; ++i) {
        if (int(i) == hidden) continue;
        car_visuals_[i]->add_items(m.items, cars[i], pose.eye, kStep);
    }
    course_->add_after_items(m.items);
    if (m.smoke.loaded()) {
        // The puffs after everything else of the scene, facing the camera.
        gt3r::race::TyreSmoke::View sv;
        sv.forward = RVec{-pose.view.at(2, 0), -pose.view.at(2, 1), -pose.view.at(2, 2)};
        sv.proj_x = 1.0f / (std::tan(pose.fovy() * 0.5f) * std::max(aspect, 1e-3f));
        sv.near_m = 0.1f, sv.far_m = far;
        m.smoke.add_items(m.items, pose.eye, cam_right, cam_up, sv);
    }

    render::DrawOptions o = draw_options(*course_, far);
    // The cars' reflection picture: the course around the car the view is about, refreshed a face
    // at a time.
    {
        m.env_items.clear();
        const RVec at{me.position.x, me.position.y + 0.8f, me.position.z};
        const render::Mat4 sky_rot = render::rotate_x(env.sky_pitch * kPi / 180) * render::rotate_y(env.sky_yaw * kPi / 180);
        if (course_->add_env_items(m.env_items, at, sky_rot)) {
            render::DrawOptions eo = o;
            eo.env_cube = nullptr;
            eo.draw_distance = 200, eo.far_clip = 0;
            eo.fog = false;
            eo.quantize_rgb5 = true;
            for (float& c : eo.clear) c = 0;
            eo.clear[3] = 1;
            const unsigned faces = m.env_ready ? 1u << (m.frame % 6) : 0x3Fu;
            if (m.view->render_env_cube(at, m.env_items, eo, 128, faces)) o.env_cube = m.view->env_cube(), o.env_gt3_lookup = true, m.env_ready = true;
        }
    }
    // After the scene: the sun's glare by how much of it the scenery leaves in sight, and the
    // lamps' glows.
    m.overlay.clear();
    m.overlay.rect = rect;
    const render::Frustum frustum = render::Frustum::symmetric(pose.fovy(), aspect);
    m.sun.add(m.overlay, rect, pose.view, frustum, sun_visibility(*course_, m.sun.params(), pose.eye, pose.fovy()));
    const gt3r::race::Occluder& occluder = course_->occluder();
    m.lamps.tick(kStep);
    m.lamps.add(m.overlay, rect, pose.view, pose.eye, frustum, [&](const RVec& a, const RVec& b) { return occluder.segment_blocked(a, b); }, &proj);
    if (m.car_lamps.active()) {
        // The cars' lamps at night: the lights, the tail lamps, the brake lamps while braking.
        m.glow_cars.clear();
        for (size_t i = 0; i < n; ++i) {
            if (int(i) == hidden) continue;
            gt3r::race::CarLampGlows::Car g;
            g.lamps = &car_visuals_[i]->lamps();
            g.model = car_visuals_[i]->body_matrix(cars[i]);
            g.k = cars[i].brake_lamps ? 1.0f : 0.0f;
            g.brake_now = cars[i].brake_lamps;
            m.glow_cars.push_back(g);
        }
        m.car_lamps.add(m.overlay, rect, pose.view, pose.eye, frustum, m.glow_cars, [&](const RVec& a, const RVec& b) { return occluder.segment_blocked(a, b); }, &proj);
    }
    o.overlay = &m.overlay;
    m.view->draw(ui.renderer(), rect, pose.view, proj, pose.eye, m.items, o);
    if (m.frame == 5)
        LOG_INFO("Drive: the car at (%.1f, %.1f, %.1f) heading (%.2f, %.2f), eye (%.1f, %.1f, %.1f); %d meshes drawn, %zu triangles", me.position.x,
                 me.position.y, me.position.z, me.forward.x, me.forward.z, pose.eye.x, pose.eye.y, pose.eye.z, m.view->stats().meshes_drawn,
                 m.view->stats().triangles);
    ++m.frame;
}

} // namespace gt4r
