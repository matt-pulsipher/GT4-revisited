#include "gt4r/app/race_world.h"

#include "gt4r/net/lan_rules.h"
#include "gt4r/net/net_field.h"

#include "gt4r/race/aspec_points.h"
#include "gt4r/race/race_flow.h"

#include "gt4r/sim/showroom.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program_tables_prologue.h"

#include "base/log.h"
#include "gt4r/audio/race_sound.h"
#include "gt4r/audio/app_sound.h"
#include "gt4r/audio/race_tables.h"
#include "audio/audio_engine.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/db/car_parts.h"
#include "gt4r/menu/game_bspec.h"
#include "gt4r/sim/ai/bspec_skill.h"
#include "gt4r/sim/ai/pit_tire_change.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/race/car_spec.h"
#include "gt4r/sim/race_rules.h"
#include "gt4r/sim/record_image.h"
#include "gt4r/race/driver_figure.h"
#include "gt4r/race/driver_state.h"
#include "gt4r/race/license_scene.h"
#include "gt4r/sim/program_prologue.h"
#include "gt4r/race/picture_scale.h"
#include "gt4r/race/pit_scene.h"
#include "gt4r/race/sections.h"
#include "gt4r/race/start_rule.h"
#include "gt4r/sim/rules_drive.h"
#include "gt4r/sim/rules_wear.h"
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
#include <future>
#include <map>
#include <thread>

namespace gt4r {

namespace {

// The pace car's label, which the race's set-up lights whatever the course (RaceBase::vf38, the program's string 0x68D018).
const std::string kPaceCar = "r34_gtr_v_gtcolor_pc_01";


namespace render = gt3r::render;
using RVec = render::Vec3;

constexpr float kStep = 1.0f / 60;
constexpr float kPi = 3.14159265f;

// A car being made for the race: what it is made from, and its shapes while a thread decodes them.
struct CarJob {
    std::string key, label;
    std::optional<Bytes> file, pat, wing;
    race::CarSpec spec;
    race::CarVisual::Options options;
    std::future<std::unique_ptr<race::CarVisual>> made; // (after what it reads: it ends before that goes)
};

// Starts job `at`'s car and the next few (no more: each waits in memory).
void start_cars(render::GpuContext& gpu, std::vector<std::unique_ptr<CarJob>>& jobs, size_t at) {
    const size_t ahead = std::clamp<size_t>(std::thread::hardware_concurrency() / 4, 1, 3);
    for (size_t k = at; k < jobs.size() && k <= at + ahead; ++k) {
        CarJob& j = *jobs[k];
        if (j.made.valid() || !j.file) continue;
        j.made = std::async([&gpu, &j] { return race::CarVisual::make_unuploaded(gpu, *j.file, j.pat ? ByteSpan(*j.pat) : ByteSpan{}, j.spec, j.options, j.label); });
    }
}
// Job `at`'s car, on the GPU; the next few cars' shapes are decoded meanwhile.
std::unique_ptr<race::CarVisual> take_car(render::GpuContext& gpu, std::vector<std::unique_ptr<CarJob>>& jobs, size_t at) {
    start_cars(gpu, jobs, at);
    std::unique_ptr<race::CarVisual> car = jobs[at]->made.get();
    car->upload();
    jobs[at].reset(); // (its files)
    return car;
}

} // namespace

struct RaceWorld::Impl {
    std::map<std::string, std::unique_ptr<race::CarVisual>> visuals_by_model;
    std::unique_ptr<audio::RaceSound> sound;
    // What each of the screen's views keeps from picture to picture: the race's one view, or a split screen's two.
    struct ViewState {
        gt3r::race::SceneryFade fade;
        std::unique_ptr<render::View3D> view, mirror;
        bool env_ready = false;
        RVec env_at; // where the cars' reflection picture was last drawn from
        int frame = 0;
    };
    std::array<ViewState, 2> views;
    std::array<std::unique_ptr<render::View3D>, 2> eye_views; // a VR headset's (render_eyes)
    std::vector<audio::SoundListener> more_cameras; // a split screen's second view's, while it is heard
    std::unique_ptr<render::View3D> icon_view; // the results' car icons
    std::vector<render::DrawItem> mirror_items;
    std::vector<render::DrawItem> items, env_items, wet_items;
    gt3r::race::SunGlare sun;
    gt3r::race::SunDisc sun_disc;
    gt3r::race::LampGlows lamps;
    gt3r::race::Billboards billboards;
    gt3r::race::CarLampGlows car_lamps;
    gt3r::race::CarLampGlows car_lamps_lit; // a daytime race's: the cars lit whatever the course (CarVisual::lamp_level)
    std::vector<gt3r::race::CarLampGlows::Car> glow_cars;
    gt3r::race::TyreSmoke smoke;
    gt3r::race::WetRoad wet;
    std::vector<race::CarLight> lights; // the cars', this frame
    // The drivers that move (race/driver_figure.h): a car's figures and what each does.
    struct MovingDriver {
        size_t car = 0;
        const race::CarVisual* visual = nullptr;
        race::posture::Cockpit cockpit;
        bool co_driver = false;
        race::posture::DriverState state;
        std::unique_ptr<race::DriverFigure> figure;
        bool posed = false;
        race::posture::F follow, follow_rate; // the steering command's follower (0x40CC58), here of the front wheels' angle
        // The gear change's: the lever's layout (from the car's GEAR part, at the first step), the engine speed as the race
        // shows it (car +0x738, 0x350ED0: kept here beside the driver, the race display's own is not this).
        bool gear_known = false;
        race::posture::GearInput gear;
        race::posture::ShownRpm shown_rpm;
    };
    std::vector<MovingDriver> drivers;
    // Each car's body's moving parts (a few cars' scripts: null for the others).
    std::vector<std::unique_ptr<race::CarVisual::Motion>> motions;
    const race::CarVisual::Motion* motion(size_t i) const { return i < motions.size() ? motions[i].get() : nullptr; }
    void add_drivers(std::vector<render::DrawItem>& items, size_t car, const race::CarState& s, const RVec& eye, const race::CarVisual::LodView* view);
    // The pit crew, and what each car's is told; the sounds its steps asked for (the car last watched's).
    std::unique_ptr<race::PitScene> pit_scene;
    std::vector<race::PitScene::CarSizes> pit_sizes;
    std::vector<race::PitScene::Car> pit_cars;
    std::vector<const char*> pit_sounds;
    int pit_shown = 0;
    bool pit_live = true; // (a replay's cars have no stops)
    std::vector<race::PitScene::Wheel> pit_wheels;
    void add_pit_crew(std::vector<render::DrawItem>& items, size_t car, const race::CarVisual& visual);
    // A license test's staff (one car on a road section of a license test: RaceLicense virtual 38, 0x3BC5D0).
    std::unique_ptr<race::LicenseScene> license_scene;
    race::LicenseScene::CarSizes license_sizes;
    render::ScreenBatch overlay;
};

RaceWorld::RaceWorld(App& app, const WorldSetup& setup) : app_(app), setup_(setup), computer_player_(setup.computer_player), impl_(std::make_unique<Impl>()) {
    Impl& m = *impl_;
    render::GpuContext& gpu = *app.gpu();
    const std::optional<Bytes> course = app.vfs()->read("crs/" + setup.course);
    if (!course || !course::CourseFile::looks_like_course(*course)) throw FormatError("There is no course file crs/" + setup.course + ".");
    if (setup.entries.empty()) throw FormatError("A race needs a car.");
    float reflection_c0 = -1, reflection_c1 = 0;
    {
        const ByteSpan env = course::CourseFile::parse(*course).section(*course, course::kSlotEnv);
        night_ = env.size() >= 0x14 && (rd32(env, 0x10) & 0x8000) != 0;
        // The cars' reflection passes are weighted by the course's factors (0x397CE0 sets the draw's: C0 = +0x34,
        // C1 = +0x34 x +0x30).
        if (env.size() >= 0x38) {
            float k[2];
            std::memcpy(k, env.data() + 0x30, 8);
            reflection_c0 = k[1], reflection_c1 = k[1] * k[0];
        }
    }

    const std::vector<db::CarEntry>& cars = app.db()->cars();
    std::map<std::string, std::optional<Bytes>> character_files;
    std::optional<Bytes> brake_parts; // race/brake.bin, for Gran Turismo 3 car files (Prologue's)
    // The cars' shapes: decoded on threads, a few cars ahead of the one the main thread hands to the GPU.
    std::vector<std::unique_ptr<CarJob>> jobs;
    std::map<std::string, size_t> job_of;
    std::vector<std::pair<const db::CarEntry*, std::string>> entries; // each entry's car and its visual's key
    for (const WorldEntry& e : setup.entries) {
        const size_t index = size_t(&e - setup.entries.data());
        const db::CarEntry* entry = nullptr;
        for (const db::CarEntry& c : cars)
            if (c.label == e.label) entry = &c;
        if (!entry) throw FormatError("The car database has no car " + e.label + ".");
        const race::CarSpec spec = race::read_car_spec(*app.db(), entry->label);
        // A fitted wing: the entry's, else its equipment's WING part (record +0x100, table 30).
        std::string wing = e.wing;
        if (wing.empty() && e.equipment) {
            const db::PartId part = e.equipment->part(0x100);
            if (part != db::kNoPart && db::part_table(part) == u32(db::PartTable::Wing))
                if (const db::Labels* l = app.db()->labels("WING"))
                    if (const std::string* name = l->label(db::part_row_id(part))) wing = *name;
        }
        // The car's lamp level (RaceBase::vf38 0x3882F8, through RaceCarModel::vf43): the course's night flag, but the pace car
        // (the program's name for it, 0x68D018) always lit: its glows and lit lenses in a daytime race too.
        const int lamp_level = night_ || entry->label == kPaceCar ? 1 : 0;
        const std::string key = entry->model + "#" + std::to_string(e.paint) + "#" + wing + "#" + std::to_string(lamp_level);
        if (const auto [at, fresh] = job_of.try_emplace(key, jobs.size()); fresh) {
            jobs.push_back(std::make_unique<CarJob>());
            CarJob& job = *jobs.back();
            job.key = key, job.label = entry->label, job.spec = spec;
            job.file = app.vfs()->read("car/lod/" + entry->model);
            job.pat = app.vfs()->read("car/lod/" + entry->model + ".pat");
            const std::optional<Bytes>& file = job.file;
            if (!file || (!car::CarFile::is_car(*file) && !race::CarVisual::is_gt3_car(*file))) throw FormatError("This car has no race model on the disc (" + entry->model + ").");
            const std::vector<u32> paints = race::car_paints(*app.db(), entry->label);
            race::CarVisual::Options& o = job.options;
            o.paint = paints.empty() ? e.paint : paints[e.paint % paints.size()];
            o.lamp_level = lamp_level;
            if (car::CarFile::is_car(*file)) o.reflection_c0 = reflection_c0, o.reflection_c1 = reflection_c1;
            if (!wing.empty()) job.wing = app.vfs()->read("wing/lod/" + wing);
            if (job.wing) o.wing = *job.wing;
            o.driver = setup.drivers;
            if (race::CarVisual::is_gt3_car(*file)) {
                if (!brake_parts) brake_parts = app.vfs()->read("race/brake.bin");
                if (brake_parts) o.brake_parts = *brake_parts;
            }
            if (setup.drivers && car::CarFile::is_car(*file) && !car::CarFile::parse(*file).driver) try {
                    // A car without a driver of its own: a character file by its cockpit kind (0x3B5438).
                    const race::CockpitBytes cp = race::read_cockpit_bytes(*file);
                    const std::string path = race::character_driver_file(cp.kind, cp.co_driver, int(index));
                    if (!path.empty()) {
                        auto [it, fresh] = character_files.try_emplace(path);
                        if (fresh) it->second = app.vfs()->read(path);
                        if (it->second) o.driver_set = *it->second;
                    }
                } catch (const std::exception&) {
                }
        }
        // What the pit crew needs of it.
        race::PitScene::CarSizes sizes;
        for (int a = 0; a < 2; ++a) sizes.tire_width[a] = spec.tire_width[a], sizes.tire_radius[a] = spec.tire_radius[a];
        if (const auto box = race::car_box(*app.db(), entry->label)) sizes.width = (*box)[1], sizes.front = (*box)[2], sizes.rear = (*box)[3];
        if (const std::optional<Bytes>& file = jobs[job_of[key]]->file; file && car::CarFile::is_car(*file)) try {
                race::PitScene::read_car_file(*file, sizes);
            } catch (const std::exception&) {
            }
        m.pit_sizes.push_back(sizes);
        entries.push_back({entry, key});
    }
    start_cars(gpu, jobs, 0); // (the first cars while the course is read)
    course_ = std::make_unique<race::CourseScene>(gpu, *course, setup.course, setup.section);
    glow_ = race::GlowFilter::from_env(course_->section(course::kSlotEnv)); // (the course's glow filter, Env0 +0x1750)
    if (glow_) LOG_INFO("Course: glow filter a %.2f, radius %.4f of the diagonal, %d x %d points, sum %.3f, gain %.3f", glow_->threshold, glow_->radius, glow_->samples, glow_->samples, glow_->sum, glow_->gain);
    if (course_->road().checkpoints.size() < 3) throw FormatError("This place has no lap to drive.");
    for (const auto& [entry, key] : entries) {
        std::unique_ptr<race::CarVisual>& visual = m.visuals_by_model[key];
        if (!visual) {
            visual = take_car(gpu, jobs, job_of[key]);
            // The course's strengths of the three lamp lists (Env0 +0x1794: lights, brake, tail).
            if (const ByteSpan e0 = course_->section(course::kSlotEnv); e0.size() >= 0x17A4) {
                float v[3];
                std::memcpy(v, e0.data() + 0x1794, 12);
                visual->set_lamp_strengths(v[0], v[1], v[2]);
                float power;
                std::memcpy(&power, e0.data() + 0x17A0, 4);
                visual->set_lamp_type_factor(power);
                // The brake discs' glow: heat^2 x Env0 +0x4C / +0x38 (the race's sponsor, 0x3D75F0).
                float gain, scale;
                std::memcpy(&gain, e0.data() + 0x4C, 4), std::memcpy(&scale, e0.data() + 0x38, 4);
                visual->set_brake_glow_gain(scale > 0 ? gain / scale : 0.0f);
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
        impl_->motions.push_back(visual->make_motion());
        if (const race::DriverModels* models = visual->moving_driver()) {
            for (size_t seat = 0; seat < visual->cockpits().size(); ++seat) {
                Impl::MovingDriver d;
                d.car = car_visuals_.size() - 1;
                d.visual = visual.get();
                d.cockpit = visual->cockpits()[seat];
                d.co_driver = seat == 1;
                d.state.reset(d.cockpit);
                d.figure = std::make_unique<race::DriverFigure>(*models);
                m.drivers.push_back(std::move(d));
            }
        }
        names_.push_back(entry->name.empty() ? entry->label : entry->name);
        // The short name (CAR_NAME_<language> +0x08: "VW Beetle 1100 ..."): in the name table whose
        // first name is the one above.
        std::string short_name;
        for (const char* lang : {"american", "british", "japanese", "french", "german", "italian", "spanish"}) {
            db::SpecDb& sdb = *app.db();
            const db::Table* t = sdb.table(std::string("CAR_NAME_") + lang);
            const db::Strings* pool = sdb.strings(std::string(lang) + "_StrDB");
            if (!t || !pool) continue;
            const ByteSpan row = t->row(entry->id);
            if (row.size() < 12 || pool->at(rd32(row, 0)) != entry->name) continue;
            short_name = pool->at(rd32(row, 8));
            break;
        }
        short_names_.push_back(short_name.empty() ? names_.back() : short_name);
    }
    gadget_bodies_.reset(course_->gadgets(), course_->road());
    course_->set_gadget_bodies(&gadget_bodies_);
    line_job_ = std::async(std::launch::async, [road = &course_->road()] { return race::RailLine(*road); });
    build_simulation();
    open_license_scene();
    // The race's sound: every car, the course's ambience, the count and the music.
    if (audio::AppSound* sound = app.sound()) {
        gt3r::AudioEngine& engine = app.audio();
        gt3r::Vfs& vfs = *app.vfs();
        const audio::StreamPlayer music = sound->streams();
        std::shared_ptr<const gt3r::CoreImage> program;
        try {
            if (app.disc()) program = app.disc()->program_image();
        } catch (const std::exception&) { // (the sound's tables stay flat)
        }
        m.sound = std::make_unique<audio::RaceSound>(vfs, engine.voices(), sound->driver(), sound->library_ptr(), audio::RaceSoundTables::from_program(program ? ByteSpan(program->program) : ByteSpan{}), music);
        for (const WorldEntry& e : setup.entries)
            if (!m.sound->add_car(*app.db(), e.label, e.equipment ? &*e.equipment : nullptr)) LOG_WARN("Drive: no engine sound for %s", e.label.c_str());
        m.sound->set_course(course_->section(course::kSlotSound), std::make_shared<const course::Runway>(course_->road()));
        // The volumes and output of the game's options, and the player's play list of racing music.
        m.sound->set_options(audio::race_sound_options(setup.options));
        if (setup.playlist) m.sound->set_playlist(setup.playlist);
    }
    for (int i = 0; i < std::clamp(setup.players, 1, 2); ++i) m.views[size_t(i)].view = std::make_unique<render::View3D>(gpu);
    // The sun, the lamps' glows (their meshes are the course's own, slot 37) and the billboards:
    // the shared engine's layouts, read by its own code.
    const ByteSpan env0 = course_->section(course::kSlotEnv);
    m.sun.load(*app.vfs(), app.renderer(), &gpu, env0);
    m.sun_disc.load(gpu, m.sun.params());
    effects_.setup(course_->env());
    m.lamps.load(app.renderer(), &gpu, course_->section(course::kSlotEffects), env0, course_->section(course::kSlotWorldVis),
                 course_->section(course::kSlotFlareShape));
    m.billboards.load(gpu, course_->section(course::kSlotBillboards), setup.course);
    m.car_lamps.load(app.renderer(), &gpu, env0, course_->section(course::kSlotFlareShape));
    // A daytime race's lit cars (the pace car): their glows by the same lamp type of the course (Env0 +0x9F0 ..), which every
    // course has; the shared glows read it at night only, so they are given the course's block with its night flag on.
    if (!night_ && env0.size() >= 0x14) {
        Bytes lit(env0.begin(), env0.end());
        lit[0x11] = u8(lit[0x11] | 0x80);
        m.car_lamps_lit.load(app.renderer(), &gpu, lit, course_->section(course::kSlotFlareShape));
    }
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
    starts_.assign(setup_.entries.size(), {});
    ended_.clear();
    if (!start_course_) start_course_ = std::make_unique<race::StartCourse>(course_->road(), course_->section(course::kSlotRunway), race::course_start(*app_.db(), setup_.course).way);
    start_course_->set_race(setup_.conductor, setup_.event_row, setup_.section);
    if (!setup_.rail && app_.disc()) {
        // A program that is neither Gran Turismo 4's nor a placed Prologue build races nothing (no stand-in): the race says so.
        if (const sim::Program p = sim::Program::load(*app_.disc()); !p.known() && !sim::PrologueProgram::locate(p).recognized()) {
            LOG_WARN("Drive: %s", p.issue().c_str());
            throw std::runtime_error("This release of the game isn't supported yet.");
        }
        try {
            const sim::Program program = sim::Program::load(*app_.disc());
            if (program.known() && !hand_speeds_ && !impl_->drivers.empty()) {
                hand_speeds_.emplace();
                for (u32 k = 0; k < 11; ++k) (*hand_speeds_)[k] = race::posture::F::raw(program.w(0x622A20 + 4 * k));
            }
            // The materials off the course (0x622430, read by 0x3F4EA0; Prologue's 0x503E28, by 0x3B4738).
            for (u32 m = 0; m < soft_.size(); ++m)
                soft_[m] = program.prologue_values() ? (m < sim::kPrologueSoftCount ? program.w(sim::kPrologueSoft + 4 * m) : 0)
                                                     : program.w(sim::Program::kSoft + 4 * m);
            std::vector<sim::Entry> entries(setup_.entries.size());
            for (size_t i = 0; i < entries.size(); ++i) {
                const WorldEntry& we = i < overrides_.size() && overrides_[i] ? *overrides_[i] : setup_.entries[i];
                if (we.equipment) entries[i].equipment = *we.equipment; // a garage car's own
                else if (!sim::stock_car(*app_.db(), program, we.label, entries[i].equipment)) throw std::runtime_error("no parts for " + we.label);
                entries[i].condition = we.condition;
                entries[i].grid_place = we.grid_place;
                entries[i].automatic = we.automatic, entries[i].pedal_style = we.pedal_style;
                entries[i].asm_on = we.asm_on, entries[i].tcs_on = we.tcs_on, entries[i].wheel_aid = we.wheel_aid;
                entries[i].driver = we.driver;
                entries[i].control = players_car(i) ? 0 : i == 0 ? 1 : 2;
                // An event's race counts fuel (0x35E108 sets the rule block's +0x4C to 80; vf26 0x34CA20 clears it on a course
                // whose row's +0x14 is 0, a loose course and in a run alone, 0x3F7058 a training's and a mission's); a run
                // without laps (practice, a free run) runs on the free-run conductor, whose rule block holds 0 there (the
                // original's Practice state); an endurance race of a time limit has no laps and counts it.
                entries[i].fuel = setup_.event_row.size() > 0x43 && (setup_.event_row[0x43] != 0 || race::row_time_limit_ms(setup_.event_row) > 0) &&
                                  setup_.conductor == race::Conductor::Other &&
                                  !setup_.pace.solo && race::course_start(*app_.db(), setup_.course).fuel;
            }
            counts_fuel_ = !entries.empty() && entries[0].fuel;
            plan_starts(program, entries);
            entries_ = entries;
            // The player's car's power as built (the replay's card).
            try {
                sim::Spec spec;
                sim::build_spec(*app_.db(), program, entries[0].equipment, 0, spec);
                player_ps_ = sim::spec_figures(spec).ps;
            } catch (const std::exception&) {
                player_ps_ = 0;
            }
            sim::Session::Options options;
            if (setup_.own_hold) options.hold_steps = 0;
            options.rules = race::event_rule_block(setup_.event_row);
            if (!setup_.event_row.empty()) options.rules[0x40] = u8(entries.size() - 1); // (the cars on the grid less one: 5 of six, 1 of two in the original's states)
            session_ = std::make_unique<sim::Session>(*app_.db(), program, std::make_shared<const course::Runway>(course_->road()), entries,
                                                      sim::course_row(*app_.db(), setup_.course), &options);
            if (!noted_)
                for (const std::string& note : session_->notes()) LOG_INFO("Drive: %s", note.c_str());
            sim_ = session_.get();
            for (size_t i = 0; i < choices_.size(); ++i) session_->set_wheel_options(i, choices_[i].wheel_strength, choices_[i].wheel_assist);
            const sim::RaceRules race_rules = sim::race_rules(setup_.event_row, program);
            session_->set_race_rules(race_rules.hits, race_rules.speed_cap, race_rules.penalty_cap);
            session_->set_pit_rule(setup_.pit_rule);
            { // the race's class grips (the rule block's +0x18 .. +0x1B from the row's +0x53 .. +0x56, 0x35E108)
                std::array<gt3r::s8, 4> grip{};
                if (setup_.event_row.size() > 0x56)
                    for (size_t k = 0; k < 4; ++k) grip[k] = gt3r::s8(setup_.event_row[0x53 + k]);
                // (Prologue's program has no rule that turns a 0 into 20, 0x3BC048: its single races' rows carry the 20)
                session_->set_class_grip(setup_.event_grips || app_.db()->prologue() ? grip : sim::rules::single_race_class_grip(grip));
            }
            if (setup_.event_row.size() >= 0x74) { // the event's tire wear (the rule block's +0x2C .. +0x34, 0x35E108)
                u8 rule_block[0x80] = {};
                sim::rules::event_wear_bytes(setup_.event_row.data(), rule_block);
                // (vf26 sets the wear's rules on a course that counts fuel alone)
                if (!race::course_start(*app_.db(), setup_.course).fuel && !app_.db()->prologue()) std::memset(rule_block, 0, sizeof rule_block);
                session_->set_wear_rules(rule_block, !session_->loose(), program);
            }
            if (setup_.lan_wear >= 0) { // (a battle between machines: 0x354F30 in place of the event's)
                u8 rule_block[0x80] = {};
                const std::array<u8, 9> wear = net::lan_wear(program, setup_.lan_wear);
                std::copy(wear.begin(), wear.end(), rule_block + 0x2C);
                session_->set_wear_rules(rule_block, false, program);
                net::lan_boost(program, setup_.lan_boost, setup_.pace);
            }
            // The computer drivers: every car but the player's (his too when he watches).
            // The player's own car keeps a driver that plans but does not drive (the program's pad car: its decision each
            // step, the suggested gear from it).
            std::vector<size_t> computer;
            for (size_t i = 0; i < setup_.entries.size(); ++i) computer.push_back(i);
            field_.reset(), pits_.reset(), bspec_driven_ = false;
            if (!computer.empty()) {
                try {
                    sim::ai::Pace pace = setup_.pace;
                    const WorldEntry& first = !overrides_.empty() && overrides_[0] ? *overrides_[0] : setup_.entries[0];
                    pace.start_speed = u8(std::lround(std::clamp(first.start_speed * 3.6f, 0.0f, 255.0f)));
                    const race::CourseStart cs = race::course_start(*app_.db(), setup_.course);
                    pace.strip = cs.strip, pace.start_look = u8(std::max(0, race::start_code_digit(cs.code, 3)));
                    field_ = sim::ai::Field::open(*app_.disc(), setup_.course, *session_, computer, pace);
                    // B-spec: the player's car's driver takes his commands.
                    if (computer_player_) field_->set_bspec(0);
                    for (size_t i = 0; i < setup_.entries.size(); ++i)
                        if (players_car(i)) field_->set_shadow(i);
                    for (size_t c = 0; c < commands_.size(); ++c) field_->command(c, {commands_[c].pace - 1, commands_[c].overtake ? 1 : 0});
                    // (their aids: the player's too, which a car its driver drives in its rolling start takes)
                    for (size_t car : computer) session_->set_driver_aids(car, field_->params(car).aids, field_->params(car).grip);
                    first_lines(program);
                    // (its record and skill at the race's start, the conductor's virtual 28 0x3F1E60: after the set-up's plans)
                    if (computer_player_) arm_bspec(program);
                    field_->apply_start_delays(*session_); // (a mission's held places)
                    // Their pit stops (the program's pit pass, request and stop, the fuel, wear and tires the session's).
                    sim::ai::PitHooks pit_hooks;
                    if (program.known()) {
                        pit_hooks = sim::ai::session_pit_hooks(*session_);
                        program_ = std::make_shared<const sim::Program>(program);
                        gt3r::Volume* vol = &app_.disc()->volume();
                        sim::ai::add_tire_change(pit_hooks, *session_, *app_.db(), *program_, [vol](const std::string& path) -> std::optional<Bytes> {
                            const gt3r::Volume::Entry* e = vol->find(path);
                            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
                        });
                    }
                    // (an online race: what the stop does to the player's car outside its state, for the others: net/car_service.h)
                    {
                        using K = net::ServiceEvent::Kind;
                        auto note = [this](bool after, K kind, u32 value) {
                            if (service_on_ && service_.size() < 256) service_.push_back({after, kind, value});
                        };
                        if (auto f = pit_hooks.hold) pit_hooks.hold = [f, note](size_t i, u16 h) { f(i, h), i == 0 ? note(false, K::Hold, h) : void(); };
                        if (auto f = pit_hooks.change_tires)
                            pit_hooks.change_tires = [f, note](size_t i, u8 front, u8 rear, u8 axles) {
                                f(i, front, rear, axles), i == 0 ? note(false, K::Tires, u32(front) | u32(rear) << 8 | u32(axles) << 16) : void();
                            };
                        if (auto f = pit_hooks.retune_tires) pit_hooks.retune_tires = [f, note](size_t i, u8 axles) { f(i, axles), i == 0 ? note(false, K::Grips, axles) : void(); };
                        if (auto f = pit_hooks.new_tire) pit_hooks.new_tire = [f, note](size_t i, int w) { f(i, w), i == 0 ? note(false, K::NewTires, 1u << w) : void(); };
                        if (auto f = pit_hooks.place_in_box) pit_hooks.place_in_box = [f, note](size_t i, gt3r::ee::F h) { f(i, h), i == 0 ? note(true, K::Box, h.bits) : void(); };
                        if (auto f = pit_hooks.face_box) pit_hooks.face_box = [f, note](size_t i, gt3r::ee::F h) { f(i, h), i == 0 ? note(true, K::Face, h.bits) : void(); };
                        if (auto f = pit_hooks.set_fuel) pit_hooks.set_fuel = [f, note](size_t i, gt3r::ee::F l) { f(i, l), i == 0 ? note(true, K::Fuel, l.bits) : void(); };
                        if (auto f = pit_hooks.flags) pit_hooks.flags = [f, note](size_t i, u8 x) { f(i, x), i == 0 ? note(true, K::Flags, x) : void(); };
                    }
                    sim::ai::PitRace pr;
                    pr.laps = setup_.laps;
                    pr.mode = computer_player_ ? 1 : 0; // (K +0xCBDC: a B-spec race)
                    pr.limit_minutes = u32(race::row_time_limit_ms(setup_.event_row) / 60000); // (the conductor's +0xF850)
                    pr.pit_menu = setup_.players < 2; // (a two-player battle has no pit menu: 0x32F028 makes none)
                    pit_asked_.assign(setup_.entries.size(), 0);
                    tires_fitted_ = pit_hooks.tires;
                    pits_ = std::make_unique<sim::ai::PitStops>(*field_, program, setup_.entries.size(), pit_hooks, pr);
                    open_pit_scene();
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
    // A rolling start's cars (0x3452F0 gives each its driver kind 1 and the start's driver line).
    pace_kmh_.assign(setup_.entries.size(), 0.0f);
    for (size_t i = 0; i < pace_kmh_.size(); ++i) {
        const WorldEntry& we = i < overrides_.size() && overrides_[i] ? *overrides_[i] : setup_.entries[i];
        pace_kmh_[i] = we.start_speed * 3.6f;
    }
}

// The cars' starts by the race's rule, into the session's entries.
void RaceWorld::plan_starts(const sim::Program& program, std::vector<sim::Entry>& entries) {
    const course::Runway& road = course_->road();
    const race::StartRace& r = start_course_->race();
    // The driving lines, when a car may start on one (the drivers' own need the session).
    bool on_line = start_course_->may_need_lines(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const WorldEntry& we = i < overrides_.size() && overrides_[i] ? *overrides_[i] : setup_.entries[i];
        on_line = on_line || we.start_speed > 0 || !std::isnan(we.start_distance);
    }
    if (on_line && !start_course_->has_lines() && app_.disc())
        if (const std::optional<course::AiData> data = sim::ai::Field::read(*app_.disc(), setup_.course)) start_course_->add_lines(*data, sim::ai::Tables::load(program));
    starts_.clear();
    for (size_t i = 0; i < entries.size(); ++i) {
        const WorldEntry& we = i < overrides_.size() && overrides_[i] ? *overrides_[i] : setup_.entries[i];
        std::optional<race::CarStart> rolling;
        if (we.start_kind == 6) rolling.emplace(), rolling->kind = 6; // (a pit start on its grid place: the free practice's player)
        else if ((we.start_speed > 0 || we.start_kind == 1) && !std::isnan(we.start_distance)) { // a single race's rolling start (0x347618), a two-player battle's handicap (0x347108)
            rolling.emplace();
            rolling->kind = we.start_kind ? we.start_kind : we.start_distance < 0 ? 2 : road.gtrw && we.start_distance == 0 ? 0 : 1; // (Prologue: at the line the grid)
            rolling->distance = int(we.start_distance), rolling->speed = int(std::lround(we.start_speed * 3.6f));
        }
        starts_.push_back(race::plan_start(r, i, we.grid_place >= 0 ? we.grid_place : int(i), rolling));
        const race::PlannedStart& p = starts_.back();
        entries[i].start_speed = p.line ? 0.0f : we.start_speed;
        race::put_start(entries[i], r, p, players_car(i), we.grid_place >= 0 ? we.grid_place : int(i), program);
        if (rolling && !p.line && rolling->kind != 6) entries[i].start_distance = we.start_distance; // (no driving line: the road's middle)
    }
}

int RaceWorld::start_crossings(size_t car) const {
    return car < starts_.size() ? race::planned_crossings(start_course_->race(), starts_[car], sim_->cars()[car].course_v) : 0;
}

int RaceWorld::start_back(size_t car) const {
    return car < starts_.size() ? race::planned_back(start_course_->race(), starts_[car], sim_->cars()[car].course_v) : 0;
}

// The line each car's driver starts on, and the cars their drivers hold in a rolling start.
void RaceWorld::first_lines(const sim::Program& program) {
    if (!field_) return;
    if (course_->road().gtrw) { // (Prologue, 0x3C93A0: kinds 2 .. 5 the main line, else by the grid place)
        for (size_t i = 0; i < starts_.size(); ++i) {
            const WorldEntry& we = i < overrides_.size() && overrides_[i] ? *overrides_[i] : setup_.entries[i];
            const int kind = starts_[i].line ? starts_[i].start.kind : 0;
            if (kind >= 2 && kind <= 5) field_->start_line(i, sim::ai::CourseLines::kMain);
            else field_->start_grid_line(i, we.grid_place >= 0 ? we.grid_place : int(i));
            if (kind == 2) field_->hold_rolling(i);
        }
        return;
    }
    if (!program.known()) return;
    for (size_t i = 0; i < starts_.size(); ++i) {
        const WorldEntry& we = i < overrides_.size() && overrides_[i] ? *overrides_[i] : setup_.entries[i];
        field_->start_line(i, race::first_line(start_course_->race(), starts_[i], players_car(i), we.grid_place >= 0 ? we.grid_place : int(i), program));
        if (session_->cars()[i].rolling) field_->hold_rolling(i);
    }
}

void RaceWorld::set_entry_overrides(std::vector<std::optional<WorldEntry>> overrides) {
    overrides_ = std::move(overrides);
    restart();
}

bool RaceWorld::set_player(const PlayerChoice& c, size_t player) {
    if (player >= setup_.entries.size()) return false;
    WorldEntry& e = setup_.entries[player];
    const bool anew = e.automatic != c.automatic || e.asm_on != c.asm_on || e.tcs_on != c.tcs_on || e.wheel_aid != c.wheel_aid || e.driver != c.wheel_lock;
    if (choices_.size() <= player) choices_.resize(player + 1);
    choices_[player] = c;
    e.automatic = c.automatic, e.asm_on = c.asm_on, e.tcs_on = c.tcs_on, e.wheel_aid = c.wheel_aid, e.driver = c.wheel_lock;
    if (session_) session_->set_wheel_options(player, c.wheel_strength, c.wheel_assist);
    return anew;
}

void RaceWorld::restart() {
    build_simulation();
    open_license_scene();
    gadget_bodies_.reset(course_->gadgets(), course_->road());
    course_->restart_animations();
    for (auto& [key, visual] : impl_->visuals_by_model)
        if (visual) visual->restart_animation();
    for (size_t i = 0; i < impl_->motions.size() && i < car_visuals_.size(); ++i)
        if (impl_->motions[i]) car_visuals_[i]->restart_motion(*impl_->motions[i]);
    clear_effects();
    for (Impl::MovingDriver& d : impl_->drivers) d.state.reset(d.cockpit), d.follow = d.follow_rate = race::posture::F(0.0f), d.gear_known = false;
}

void RaceWorld::step(std::span<const race::CarInput> inputs, bool hold, bool player_by_computer, u32 hold_left) {
    // The computer drivers write their own cars' inputs (the player's too while they drive it).
    std::vector<race::CarInput> driven;
    if (field_) {
        driven.assign(inputs.begin(), inputs.end());
        driven.resize(car_visuals_.size());
        if (!hold && pits_) pits_->before(*field_, driven); // (a stop's timing, 0x35C0B8: the car standing in its box held)
        // B-spec: the driver's shift speeds into the gearbox (0x3F1E30, control source 1).
        if (computer_player_ && session_) session_->set_shift_factor(0, field_->shift_factor(0));
        inputs = driven;
    }
    // A rolling start's pace on the stand-in simulation (the game's caps its cars itself: Session's start pace
    // 0x3F7880 / 0x3F76E8): a car not yet over the line and more than 3 s at the start's speed short of it is capped
    // at that speed (judged: the throttle eased toward the speed, a touch of brake above it); from 3 s out it runs free.
    std::vector<race::CarInput> capped;
    if (!hold && !session_ && std::any_of(pace_kmh_.begin(), pace_kmh_.end(), [](float k) { return k > 0; })) {
        capped.assign(inputs.begin(), inputs.end());
        capped.resize(car_visuals_.size());
        const std::span<const race::CarState> now = sim_->cars();
        const float len = lap_length();
        for (size_t i = 0; i < capped.size() && i < now.size() && i < pace_kmh_.size(); ++i) {
            if (!(pace_kmh_[i] > 0) || !(len > 0)) continue;
            const race::CarState& c = now[i];
            const float to_line = c.course_v > len * 0.5f ? len - c.course_v : 0.0f;
            const float mps = pace_kmh_[i] / 3.6f;
            if (!(to_line > 3 * mps)) continue;
            const float gap = mps - c.speed;
            race::CarInput& in = capped[i];
            in.throttle = std::min(in.throttle, std::clamp(0.3f + gap * 0.5f, 0.0f, 1.0f));
            in.brake = std::max(in.brake, std::clamp((-gap - 1.0f) * 0.15f, 0.0f, 0.4f));
        }
        inputs = capped;
    }
    for (size_t i = 0; field_ && session_ && i < ended_.size(); ++i)
        if (ended_[i]) session_->set_ended_kind(i, u8(field_->kind(i)));
    if (field_ && session_) // (each driver deciding within the step; the player's own input kept once he drives)
        field_->step(*session_, driven, computer_player_ && !player_by_computer ? 0 : -1, hold ? std::max(1u, hold_left) : 0);
    else if (hold && session_) session_->hold_once(inputs);
    else if (hold) {
        // The stand-in has no hold: the cars stand on their brakes.
        std::vector<race::CarInput> held(inputs.begin(), inputs.end());
        held.resize(car_visuals_.size());
        for (race::CarInput& in : held) in = {}, in.brake = 1;
        sim_->step(held, kStep);
    } else sim_->step(inputs, kStep);
    if (!hold && pits_ && session_) pits_->after(*session_, *field_);
    // (an online race: the pace and shift factor its drivers gave the player's car for the step, for the others)
    if (service_on_ && session_ && !hold && service_.size() < 256)
        service_.push_back({false, net::ServiceEvent::Driver, u32(session_->gt4_record(0).aids.tcs_class) | u32(session_->gt4_record(0).drive.shift_factor) << 8});
    if (!hold) step_pit_scene();
    if (!hold) step_license_scene();
    if (!hold) course_->animate(kStep, sim_->cars());
    // The cones and boards the cars run into: each car's box (0x3E5108) at its center of mass, by its heading,
    // its body's extents (❓ the program reads them from its car record +0x48C .. +0x494, taken here as the
    // collision body's: P +0x00 .. +0x08, half the width).
    gadget_hits_ = 0;
    if (!course_->gadgets().placements.empty()) {
        std::vector<race::GadgetBodies::Car> boxes;
        const std::span<const race::CarState> now = sim_->cars();
        for (size_t i = 0; i < now.size() && i < car_visuals_.size(); ++i) {
            const race::CarState& c = now[i];
            race::GadgetBodies::Car b;
            b.right = c.right, b.up = c.up, b.forward = c.forward, b.vel = c.velocity;
            if (session_) b = race::gadget_box(session_->record(i), c);
            else {
                const race::CarSpec& spec = car_visuals_[i]->spec();
                b.pos = {(c.wheels[0].center.x + c.wheels[1].center.x + c.wheels[2].center.x + c.wheels[3].center.x) / 4,
                         (c.wheels[0].center.y + c.wheels[1].center.y + c.wheels[2].center.y + c.wheels[3].center.y) / 4,
                         (c.wheels[0].center.z + c.wheels[1].center.z + c.wheels[2].center.z + c.wheels[3].center.z) / 4};
                b.front = b.rear = spec.wheelbase / 2 + 0.9f;
                b.half_width = std::max(spec.half_track[0], spec.half_track[1]) + spec.tire_width[0] / 2;
            }
            boxes.push_back(b);
        }
        gadget_hits_ = gadget_bodies_.step(course_->gadgets(), course_->road(), boxes, kStep);
    }
}

const race::RailLine& RaceWorld::line() const {
    if (!line_) line_.emplace(line_job_.valid() ? line_job_.get() : race::RailLine(course_->road()));
    return *line_;
}

void RaceWorld::stand_car(size_t car, float x, float z, float heading) {
    if (!session_ || course_->road().gtrw) return;
    float y = 0;
    if (!course_->road().height_at(x, z, 1000, y)) y = 0;
    using gt3r::ee::F;
    session_->place_standing(car, F(x), F(-z), F(y), F(heading));
}

float RaceWorld::lap_length() const { return course_->road().length; }

int RaceWorld::gadget_contacts(size_t i) const {
    const std::vector<int>& c = gadget_bodies_.contacts();
    return i < c.size() ? c[i] : 0;
}

float RaceWorld::gadget_impulse(size_t i) const {
    const std::vector<float>& j = gadget_bodies_.impulses();
    return i < j.size() ? j[i] : 0.0f;
}

float RaceWorld::car_top_speed(size_t i) const {
    return session_ && i < car_visuals_.size() ? session_->record(i).p.top_speed.host() : 0.0f;
}

gt3r::ee::F RaceWorld::car_max_speed(size_t i) const {
    if (!session_ || i >= car_visuals_.size()) return gt3r::ee::F();
    const auto& c = session_->record(i);
    return c.max_speed < c.speed_abs ? c.speed_abs : c.max_speed; // (vf23 0x3F6C30: with the speed now)
}

void RaceWorld::arm_bspec(const sim::Program& program) {
    // The driver's record of the book for the car's class (0x468710) and its skill from it (0x468C50).
    const int k = sim::ai::bspec_class(gt3r::ee::F(session_->rating(0)));
    // (the skill weighs the record's course marks where the race's course flag is set: cond + 0xCBE7, the
    // start code's second digit, 0x34C7E0; without marks they are 0)
    const bool course_flag = race::start_code_digit(race::course_start(*app_.db(), setup_.course).code, 1) > 0;
    const std::array<u8, 0x40> rec = setup_.bspec_info ? *setup_.bspec_info : menu::bspec::race_record(setup_.bspec_book, k, setup_.bspec_course_known ? setup_.bspec_course_marks.data() : nullptr);
    field_->set_bspec_record(0, rec, k);
    if (const std::optional<sim::ai::BSpecSkill> sk =
            sim::ai::bspec_skill(sim::ai::BSpecTables::load(program), sim::ai::BSpecRecord::from_bytes(rec.data()), gt3r::ee::F(session_->rating(0)), course_flag))
        field_->set_bspec_skill(0, sk->corner, sk->brake, sk->alongside);
    bspec_driven_ = true;
}

int RaceWorld::aspec_points(bool mission) const {
    if (!session_) return 0;
    std::vector<gt3r::ee::F> ratings;
    for (size_t i = 0; i < car_visuals_.size(); ++i) ratings.push_back(gt3r::ee::F(session_->rating(i)));
    // (0x445E20: the DRIVETRAIN row's +0x10, 2 four wheel drive; 0x447550: the COURSE row's +0x10)
    bool loose = race::course_start(*app_.db(), setup_.course).kind == 2;
    if (const db::PartId train = session_->equipment(0).part(sim::Equipment::Drivetrain); loose && train != db::kNoPart)
        if (const db::Table* t = app_.db()->table("DRIVETRAIN"))
            if (const ByteSpan r = t->row(db::part_row_id(train)); r.size() > 0x10) loose = r[0x10] != 2;
    return race::aspec_points(ratings, 0, mission, loose);
}

void RaceWorld::set_driving_mode(bool bspec) {
    if (!field_ || !session_ || !program_ || computer_player_ == bspec) return;
    computer_player_ = bspec;
    if (bspec && !bspec_driven_) arm_bspec(*program_);
    field_->set_control(0, bspec ? 1 : 0);
    session_->set_shift_factor(0, field_->shift_factor(0));
}

std::optional<std::array<u8, 0x40>> RaceWorld::bspec_record(size_t car) const {
    if (!field_ || car != 0 || !bspec_driven_) return std::nullopt;
    return field_->bspec_record(car);
}

int RaceWorld::bspec_class(size_t car) const { return session_ && car < car_visuals_.size() ? sim::ai::bspec_class(gt3r::ee::F(session_->rating(car))) : 0; }

int RaceWorld::suggested_gear(size_t i) const {
    // The car's driver's suggestion (0x469E58) and the gearbox's gear for its speed (0x36BF68); -1 none.
    if (!field_ || !session_ || i >= car_visuals_.size()) return -1;
    const sim::ai::Field::Suggestion sg = field_->suggestion(i);
    return sg.on ? session_->gear_for_speed(i, sg.speed) : -1;
}

std::optional<RaceWorld::PitOffer> RaceWorld::pit_offer(size_t car) {
    if (!pits_ || car >= pit_asked_.size()) return std::nullopt;
    const sim::ai::PitCar& c = pits_->car(car);
    const bool asked = (c.service & 0x10) != 0, was = std::exchange(pit_asked_[car], asked) != 0;
    if (!asked || was) return std::nullopt;
    return PitOffer{(c.service & 4) != 0, (c.service & 8) != 0, field_ && field_->control(car) == 1};
}

bool RaceWorld::pit_offer_over(size_t car) const {
    if (!pits_ || car >= pit_asked_.size()) return true;
    const sim::ai::PitCar& c = pits_->car(car);
    return !(c.flags & 0x10) || c.x64C == 1;
}

void RaceWorld::pit_wait(size_t car) {
    if (!pits_ || car >= pit_asked_.size()) return;
    sim::ai::PitCar& c = pits_->car(car);
    c.x64C = c.x64C <= 0 ? -1 : 0;
}

int RaceWorld::fitted_tire(size_t car, int axle) const {
    if (!tires_fitted_ || car >= pit_asked_.size()) return -1;
    const std::pair<int, int> t = tires_fitted_(car);
    return axle == 0 ? t.first : t.second;
}

std::vector<int> RaceWorld::pit_tire_kinds(size_t car, int axle) const {
    // 0x441300 (Japan's 0x439B88, the same): a kind's class, 0 for kinds 0 .. 5, 1 for 6 .. 10, 2 for 11, 3 for 12 (its
    // switch's branches; the table of them, 0x6A69E0, is the code's, not a value served by sim::Program).
    constexpr int kKinds = 13;
    auto class_of = [](int k) { return k <= 5 ? 0 : k <= 10 ? 1 : k == 11 ? 2 : 3; };
    const int own = fitted_tire(car, axle);
    if (own < 0 || own >= kKinds || !program_) return {};
    std::vector<int> out;
    for (int k = 0; k < kKinds; ++k)
        if (class_of(k) == class_of(own)) out.push_back(k);
    return out;
}

std::array<int, 3> RaceWorld::signs(size_t i) const {
    std::array<int, 3> out{};
    if (!session_ || i >= car_visuals_.size()) return out;
    const sim::rules::AidExtra& a = session_->gt4_record(i).aids;
    const int aid = std::max(a.ascc_status, a.tcs_status);
    out[0] = aid == 2 ? 3 : aid == 1 ? 1 : 0;
    const auto& c = session_->record(i);
    out[1] = c.handbrake.host() > 0.25f ? 2 : 0;
    if (program_ && program_->known() && !oil_from_) oil_from_ = sim::condition_limits(*program_).oil_from.host(); // (Gran Turismo 4's static block)
    out[2] = oil_from_ && c.oil.host() >= *oil_from_ ? 2 : 0;
    return out;
}

int RaceWorld::pit_stage(size_t car) const {
    if (!pits_ || !field_ || car >= pit_asked_.size()) return 0;
    const sim::ai::PitCar& c = pits_->car(car);
    const int kind = field_->kind(car);
    if (kind == 0 || !(c.flags & 0x10)) return 0;
    if (kind == 2) return 1;
    if (kind == 4) return 5;
    if (kind != 3) return 0;
    if (c.hold == 0) return c.service ? 1 : 5;
    return c.hold == 0x2D1 ? 1 : c.hold >= 0x285 ? 2 : c.hold >= 0x3C ? 3 : 4;
}

void RaceWorld::pit_command(size_t car) { pit_command(car, !pit_requested(car)); }

bool RaceWorld::resume(const replay::RecorderState& state) {
    if (!session_) return false;
    sim::Record r;
    r.car = session_->record(0), r.x = session_->gt4_record(0), r.layout = session_->wheel_layout(0);
    sim::load_car_record(r, state.car);
    if (field_) field_->release_rolling(0);
    session_->load_moment(0, r.car, r.x);
    if (!pace_kmh_.empty()) pace_kmh_[0] = 0;
    return true;
}

// The pit's crew (race/pit_scene.h), on a course with a pit to stop in.
void RaceWorld::open_pit_scene() {
    Impl& m = *impl_;
    if (!m.pit_scene && app_.gpu() && field_ && field_->lines().has(sim::ai::CourseLines::kPitStop)) {
        const std::optional<Bytes> file = app_.vfs()->read("pit/pit_001");
        if (!file) return;
        try {
            m.pit_scene = std::make_unique<race::PitScene>(app_.gpu(), *file);
        } catch (const std::exception& e) {
            LOG_WARN("Drive: no pit crew (%s)", e.what());
            return;
        }
        for (size_t i = 0; i < m.pit_sizes.size(); ++i) m.pit_scene->set_car(i, m.pit_sizes[i]);
        if (const gt3r::race::SunParams& env = course_->env(); env.has_shadow) {
            const float el = -env.shadow_elev * kPi / 180, az = -env.shadow_az * kPi / 180;
            m.pit_scene->set_shadow_shear(RVec{std::sin(az), 0, std::cos(az)} * (env.shadow_len * std::cos(el)));
        }
    }
    if (m.pit_scene) m.pit_scene->reset();
}

void RaceWorld::step_pit_scene() {
    Impl& m = *impl_;
    if (!m.pit_scene || !pits_ || !field_) return;
    const std::span<const race::CarState> now = sim_->cars();
    const auto& stop = field_->lines().line(sim::ai::CourseLines::kPitStop);
    m.pit_cars.resize(std::min(now.size(), car_visuals_.size()));
    for (size_t i = 0; i < m.pit_cars.size(); ++i) {
        race::PitScene::Car& c = m.pit_cars[i];
        const sim::ai::PitCar& pc = pits_->car(i);
        if (const int stage = pits_->in_stretch(i, field_->lines().lap()) ? pit_stage(i) : 0; stage != c.stage) {
            LOG_INFO("Drive: car %zu's pit stop: stage %d to %d (service %03X, hold %03X)", i, c.stage, stage, pc.service, pc.hold);
            c.stage = stage;
        }
        c.kind = u8(field_->kind(i));
        c.service = pc.service;
        c.refuelling = refuelling(i);
        race::PitScene::place_car(c, *car_visuals_[i], now[i]);
        // Its box: the point of the stop line the car stops at.
        c.has_box = pc.box >= 0 && size_t(pc.box) < stop.size();
        if (c.has_box) {
            const sim::ai::LinePoint& p = stop[size_t(pc.box)];
            c.box = {p.x, p.height, -p.y}, c.box_heading = p.heading;
            // The pit's stand: the course's nearest pit stop, else the box itself (0x3CB070).
            c.gantry = c.box, c.gantry_heading = c.box_heading;
            float best = -1;
            for (const course::Runway::Place& at : course_->road().pit_stops) {
                const RVec d{at.pos.x - c.box.x, at.pos.y - c.box.y, at.pos.z - c.box.z};
                if (best >= 0 && !(render::dot(d, d) < best)) continue;
                best = render::dot(d, d);
                c.gantry = {at.pos.x, at.pos.y, at.pos.z}, c.gantry_heading = at.heading + 3.1415f;
            }
        }
    }
    m.pit_scene->step(m.pit_cars, m.pit_shown, kStep, [&](const char* name) { m.pit_sounds.push_back(name); });
}

// The license test's staff (race/license_scene.h): set up as the test starts, the men placed by the test car's seat and front wheels.
void RaceWorld::open_license_scene() {
    Impl& m = *impl_;
    std::vector<course::Runway::Place> places;
    if (setup_.conductor == race::Conductor::License && setup_.section >= 0 && car_visuals_.size() < 2 && session_ && app_.gpu() && !course_->road().gtrw) {
        const std::vector<race::RoadSection> sections = race::road_sections(course_->section(course::kSlotRunway));
        if (size_t(setup_.section) < sections.size()) places = sections[size_t(setup_.section)].staff;
    }
    if (!places.empty() && !m.license_scene) {
        if (const std::optional<Bytes> file = app_.vfs()->read("pit/license_001")) try {
                m.license_scene = std::make_unique<race::LicenseScene>(app_.gpu(), *file);
                if (const gt3r::race::SunParams& env = course_->env(); env.has_shadow) {
                    const float el = -env.shadow_elev * kPi / 180, az = -env.shadow_az * kPi / 180;
                    m.license_scene->set_shadow_shear(RVec{std::sin(az), 0, std::cos(az)} * (env.shadow_len * std::cos(el)));
                }
            } catch (const std::exception& e) {
                LOG_WARN("Drive: no license staff (%s)", e.what());
            }
    }
    if (!m.license_scene) return;
    // The front wheels across and along the car as it stands (0x344AA8, 0x344C08).
    race::LicenseScene::CarSizes sizes = m.license_sizes;
    if (!places.empty()) {
        race::PitScene::Car c;
        race::PitScene::place_car(c, *car_visuals_[0], sim_->cars()[0]);
        for (int w = 0; w < 2; ++w) sizes.wheel_x[w] = c.hub[w].x, sizes.wheel_z[w] = c.hub[w].z;
    }
    m.license_scene->start(places, sizes, [road = &course_->road()](float x, float z, float& y, RVec* normal) {
        int t = -1;
        if (!road->height_at(x, z, 1000, y, &t)) return false;
        // The road triangle's plane (0x409B68).
        if (normal && t >= 0 && size_t(t) < road->triangles.size()) {
            const auto& tri = road->triangles[size_t(t)];
            const auto p = [&](int k) { const auto& v = road->vertices[tri.v[size_t(k)]].pos; return RVec{v.x, v.y, v.z}; };
            RVec n = render::cross(p(1) - p(0), p(2) - p(0));
            if (n.y < 0) n = n * -1.0f;
            if (const float l = render::length(n); l > 0) *normal = n * (1 / l);
        }
        return true;
    });
    if (m.license_scene->on())
        LOG_INFO("Drive: the license staff at (%.2f, %.2f), %zu places", double(m.license_scene->place().x), double(m.license_scene->place().z), places.size());
}

void RaceWorld::step_license_scene() {
    Impl& m = *impl_;
    if (!m.license_scene || !m.license_scene->on()) return;
    const int was = m.license_scene->state();
    // The men at the stopping place: the test car put there at the scene's heading (0x3FAB10).
    if (m.license_scene->step(kStep)) {
        const RVec& at = m.license_scene->place();
        stand_car(0, at.x, at.z, m.license_scene->heading());
        LOG_INFO("Drive: the license staff come out (%s)", m.license_scene->result() == 1 ? "passed" : "slower than the targets");
    }
    if (m.license_scene->state() != was) LOG_INFO("Drive: the license staff's state %d", m.license_scene->state());
}

void RaceWorld::license_result(bool passed) {
    if (impl_->license_scene) impl_->license_scene->set_result(passed);
}

bool RaceWorld::license_camera(race::ViewPose& out, bool before) const {
    const Impl& m = *impl_;
    race::LicenseScene::CameraShot shot;
    if (!m.license_scene || !m.license_scene->on() || !m.license_scene->camera(shot, before)) return false;
    out = {};
    out.eye = shot.eye, out.view = shot.view, out.fov = shot.angle;
    return true;
}

float RaceWorld::license_brightness() const { return impl_->license_scene && impl_->license_scene->on() ? impl_->license_scene->brightness() : 1.0f; }

bool RaceWorld::pit_camera(size_t car, race::ViewPose& out, bool before) const {
    const Impl& m = *impl_;
    race::PitScene::CameraShot shot;
    if (!m.pit_scene || !m.pit_scene->camera(car, shot, before)) return false;
    out = {};
    out.eye = shot.eye, out.view = shot.view, out.fov = shot.angle;
    return true;
}

bool RaceWorld::refuel_begun(size_t car) {
    if (!pits_ || car >= pit_asked_.size() || !(pits_->car(car).service & 0x20)) return false;
    pits_->car(car).service ^= 0x20;
    return true;
}

bool RaceWorld::refuelling(size_t car) const {
    if (!pits_ || car >= pit_asked_.size()) return false;
    const sim::ai::PitCar& c = pits_->car(car);
    return (c.service & 0x100) && u32(c.hold - 0x5A) < 0x22C;
}

float RaceWorld::fuel_share(size_t i) const {
    if (!session_ || i >= car_visuals_.size()) return 1.0f;
    const float tank = session_->tank(i);
    return tank > 0 ? std::clamp(session_->fuel(i) / tank, 0.0f, 1.0f) : 1.0f;
}

int RaceWorld::penalty_seconds(size_t i) const { return session_ && i < car_visuals_.size() ? session_->penalty_seconds(i) : 0; }

void RaceWorld::aid_status(size_t i, u8& asm_status, u8& tcs_status) const {
    asm_status = tcs_status = 0;
    if (!session_ || i >= car_visuals_.size()) return;
    asm_status = u8(session_->record(i).ascc_status), tcs_status = u8(session_->record(i).tcs_status);
}

void RaceWorld::rev_marks(size_t i, float& limit, float& shift) const {
    limit = shift = 0;
    if (!session_ || i >= car_visuals_.size()) return;
    limit = float(session_->record(i).p.e.rev_limit), shift = float(session_->record(i).p.d.up_rpm);
}

float RaceWorld::car_mass(size_t i) const {
    // The parameter block's mass, P +0x48 (what the license rule 0x35FE80 divides a wall hit by).
    return session_ && i < car_visuals_.size() ? session_->record(i).p.mass.host() : 0.0f;
}

namespace {

// A car's axes (right, up, back: the columns) as a quaternion.
race::posture::Quat turn_of(const race::CarState& c) {
    const float m[3][3] = {{c.right.x, c.up.x, -c.forward.x}, {c.right.y, c.up.y, -c.forward.y}, {c.right.z, c.up.z, -c.forward.z}};
    float w, x, y, z;
    if (const float t = m[0][0] + m[1][1] + m[2][2]; t > 0) {
        const float s = std::sqrt(t + 1) * 2;
        w = s / 4, x = (m[2][1] - m[1][2]) / s, y = (m[0][2] - m[2][0]) / s, z = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        const float s = std::sqrt(1 + m[0][0] - m[1][1] - m[2][2]) * 2;
        w = (m[2][1] - m[1][2]) / s, x = s / 4, y = (m[0][1] + m[1][0]) / s, z = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        const float s = std::sqrt(1 + m[1][1] - m[0][0] - m[2][2]) * 2;
        w = (m[0][2] - m[2][0]) / s, x = (m[0][1] + m[1][0]) / s, y = s / 4, z = (m[1][2] + m[2][1]) / s;
    } else {
        const float s = std::sqrt(1 + m[2][2] - m[0][0] - m[1][1]) * 2;
        w = (m[1][0] - m[0][1]) / s, x = (m[0][2] + m[2][0]) / s, y = (m[1][2] + m[2][1]) / s, z = s / 4;
    }
    using F = race::posture::F;
    return {F(x), F(y), F(z), F(w)};
}

} // namespace

// Smoke and dust from the wheels for a step: a puff where a tire slides on the road, dust and
// spray from any wheel that turns on a loose or wet surface. The kind of puff is the course's,
// by the surface under the wheel.
void RaceWorld::present(std::span<const race::CarState> cars, std::span<const audio::SoundListener> cameras, bool live) {
    impl_->more_cameras.assign(cameras.begin() + (cameras.empty() ? 0 : 1), cameras.end());
    present(cars, cameras.empty() ? nullptr : &cameras[0], 0, live);
    impl_->more_cameras.clear();
}
void RaceWorld::present(std::span<const race::CarState> cars, const audio::SoundListener* camera, int target, bool live, const sim::Session* heard) {
    Impl& m = *impl_;
    m.pit_live = live;
    effects_.step();
    if (camera && m.sound) {
        m.sound->set_more_cameras(m.more_cameras);
        if (live && session_) m.sound->update(*session_, *camera, target);
        else if (heard) m.sound->update(*heard, *camera, target);
        else m.sound->update(cars, *camera, target);
        // The pit scene of the car shown (0x3CFA98 through 0x35BE38): the stop's "tires on" bit (F +0x4B8 0x200) taken
        // and cleared, the jack let down.
        if (live && pits_ && target >= 0 && size_t(target) < car_visuals_.size()) {
            sim::ai::PitCar& pc = pits_->car(size_t(target));
            if (pc.service & 0x200) pc.service = u16(pc.service ^ 0x200), m.sound->pit_effect("jackdown");
            for (const char* name : m.pit_sounds) m.sound->pit_effect(name);
        }
        m.sound->refueling(live && m.pit_scene && m.pit_scene->hose());
    }
    m.pit_sounds.clear();
    // The cars' own scripts (a pace car's light bar), each model once a step shown: a model's time runs on with its
    // draws (0x451110), the start's hold and the replays too.
    for (auto& [key, visual] : m.visuals_by_model)
        if (visual) visual->animate(kStep);
    // Each car's moving parts by its values (RaceCarModel vf22 / vf31): the brake, the engine's speed as the race display
    // shows it (int(F +0x738)), the pedals (0: the cockpits that give them, styles 1 and 3, run the driver's gear change and
    // feet, not here).
    for (size_t i = 0; i < m.motions.size() && i < cars.size() && i < car_visuals_.size(); ++i)
        if (m.motions[i]) {
            race::CarVisual::MotionInput in;
            in.brake = cars[i].brake, in.brake_lamps = cars[i].brake_lamps;
            in.engine_rpm = std::trunc(cars[i].engine_rpm);
            car_visuals_[i]->step_motion(*m.motions[i], kStep, in);
        }
    // The moving drivers' step: their cars' steering (the program's follower of 11.25 x the command).
    if (hand_speeds_)
        for (Impl::MovingDriver& d : m.drivers) {
            if (d.car >= cars.size()) continue;
            using F = race::posture::F;
            const F aim(-11.25f * 0.5f * (cars[d.car].wheels[0].steer + cars[d.car].wheels[1].steer)), dt(kStep);
            d.follow_rate = d.follow_rate + (((aim - d.follow) * F(200.0f)) - (d.follow_rate * F(20.0f))) * dt;
            d.follow = d.follow + d.follow_rate * dt;
            // The car's place and turn (a quaternion of its axes).
            const race::CarState& car = cars[d.car];
            race::posture::DriverMotion motion;
            motion.pos = {F(car.position.x), F(car.position.y), F(car.position.z)};
            motion.velocity = {F(car.velocity.x), F(car.velocity.y), F(car.velocity.z)};
            motion.turn = turn_of(car);
            motion.roll = F(-std::atan2(car.right.y, car.up.y));
            // The gaze's: the front wheels' angle, the computer driver's aim (its x, -z; 🔎 at the car's own height, the
            // program's is the height where the car was put on the road) when the car has one.
            motion.front_wheels = F(-0.5f * (car.wheels[0].steer + car.wheels[1].steer)); // (the program's + is to the left)
            motion.co_driver = d.co_driver;
            if (field_ && d.car < field_->size()) {
                const sim::ai::Field::Probe p = field_->probe_get(d.car);
                if (!(p.aim_x == F(0.0f) && p.aim_y == F(0.0f))) motion.aim = race::posture::V3{p.aim_x, F(car.position.y), -p.aim_y};
            }
            // The gear change's inputs (a Gran Turismo 4 race's cars; the lever stays in a replay without its session).
            const sim::Session* sim_cars = live ? session_.get() : heard;
            if (sim_cars && d.car < sim_cars->cars().size() && sim_cars->gt4_rules()) {
                const gt3r::sim::exact::Car& rec = sim_cars->record(d.car);
                const sim::rules::DriveExtra& drive = sim_cars->gt4_record(d.car).drive;
                if (!d.gear_known) { // (0x385410 at the set-up; 0x402320 puts the lever at the gear's place, 0x350EB0 the shown speed at the rpm)
                    d.gear_known = true;
                    d.gear.layout = race::posture::gear_layout(db::part_row(*app_.db(), sim_cars->equipment(d.car).part(sim::Equipment::Gear)));
                    d.state.reset(d.cockpit, d.gear.layout, rec.gear);
                    d.shown_rpm.start(rec.rpm);
                }
                d.shown_rpm.step(rec.rpm, dt);
                race::posture::ShiftInputs si;
                si.rpm = gt3r::ee::to_int(d.shown_rpm.value), si.top = drive.d.gears, si.gear = rec.gear;
                si.throttle = rec.throttle_pedal, si.brake = rec.brake_pedal;
                const std::optional<F> target = live && field_ ? field_->least_target(d.car) : std::nullopt;
                si.suggested = target ? sim_cars->gear_for_speed(d.car, *target) : 0;
                si.up_rpm = sim::rules::up_shift_rpm(rec, drive);
                si.down_rpm = rec.gear >= 2 ? sim::rules::down_shift_rpm(rec, drive, rec.gear) : 0;
                d.gear.gear = rec.gear, d.gear.reach = race::posture::shift_reach(si), d.gear.start_hold = sim_cars->countdown() > 0;
                motion.gear = &d.gear;
            }
            d.state.step(d.cockpit, d.follow, dt, *hand_speeds_, &motion);
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

const gt3r::render::DrawStats& RaceWorld::draw_stats() const { return impl_->views[0].view->stats(); }

namespace {

render::DrawOptions draw_options(const race::CourseScene& course, float far) {
    const gt3r::race::SunParams& env = course.env();
    render::DrawOptions o;
    o.frustum_cull = true;
    o.sort_blended = true;
    o.far_clip = far;
    // The guard band's near plane is the projection's: the screen's 0.1 m (render() sets the camera's own).
    o.guard_near = race::kScreenNear;
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
    // The cars' reflection passes: weighted C0 + C1 min(cos, 0) with the course's C0 = Env0 +0x34 and C1 = +0x34 x +0x30 (put on the
    // passes' own rows: CarVisual's weigh_reflections); a pass without rows takes 1 + +0x30 min(cos, 0) here, at full strength.
    o.reflection_fresnel = 0.8f;
    if (const ByteSpan e0 = course.section(course::kSlotEnv); e0.size() >= 0x38) {
        float k;
        std::memcpy(&k, e0.data() + 0x30, 4);
        if (std::isfinite(k)) o.reflection_fresnel = std::clamp(k, 0.0f, 1.0f);
    }
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

// The light a car stands in, on its items from `first`.
void light_items(std::vector<render::DrawItem>& items, size_t first, const race::CarLight& light) {
    for (size_t k = first; k < items.size(); ++k) {
        std::copy(light.scale, light.scale + 3, items[k].light_scale);
        if (light.own) items[k].lights = &light.lights;
    }
}

} // namespace

// A car's moving drivers, posed once a picture.
void RaceWorld::Impl::add_drivers(std::vector<render::DrawItem>& out, size_t car, const race::CarState& s, const RVec& eye, const race::CarVisual::LodView* lod) {
    for (MovingDriver& d : drivers) {
        if (d.car != car || !d.visual->driver_shows(s, eye, lod)) continue;
        if (!d.posed) {
            namespace rp = race::posture;
            d.figure->pose(rp::driving_posture(d.cockpit, d.state.after, {}, {}, d.co_driver));
            d.posed = true;
        }
        d.figure->add_items(out, d.visual->body_matrix(s));
    }
}

// The car's pit crew and the wheels they have.
void RaceWorld::Impl::add_pit_crew(std::vector<render::DrawItem>& out, size_t car, const race::CarVisual& visual) {
    if (!pit_scene) return;
    pit_wheels.clear();
    pit_scene->add_items(out, pit_wheels, car);
    for (const race::PitScene::Wheel& w : pit_wheels) visual.add_loose_wheel(out, w.wheel, w.frame);
}

void RaceWorld::render(Ui& ui, const SDL_FRect& rect, std::span<const race::CarState> cars, const race::ViewPose& pose, size_t focus, int hidden, int half) {
    Impl& m = *impl_;
    const bool first_view = half <= 0;
    Impl::ViewState& v = m.views[first_view ? 0 : 1];
    if (!v.view) v.view = std::make_unique<render::View3D>(*app_.gpu());
    const float aspect = rect.w / std::max(rect.h, 1.0f);
    Scene scene = collect(cars, pose, focus, hidden, half, aspect, rect.h);
    // The camera's near plane, where the guard band clips too (docs/formats/camera.md "Near and far"). (The backdrop's far plane
    // lies a hundred times as far as the scenery's.)
    scene.options.guard_near = pose.near;
    render::Mat4 proj = render::perspective(pose.fovy(aspect), aspect, pose.near, scene.far * 100.0f);
    if (lens_) proj.at(0, 2) += lens_->shear_x, proj.at(1, 2) += lens_->shear_y;
    draw_view(ui, *v.view, rect, cars, pose, proj, render::Frustum::symmetric(pose.fovy(aspect), aspect), scene);
    if (v.frame == 5) {
        const race::CarState& me = cars[std::min(focus, cars.size() - 1)];
        LOG_INFO("Drive: the car at (%.1f, %.1f, %.1f) heading (%.2f, %.2f), eye (%.1f, %.1f, %.1f); %d meshes drawn, %zu triangles", me.position.x,
                 me.position.y, me.position.z, me.forward.x, me.forward.z, pose.eye.x, pose.eye.y, pose.eye.z, v.view->stats().meshes_drawn,
                 v.view->stats().triangles);
    }
    ++v.frame;
}

void RaceWorld::render_eyes(Ui& ui, std::span<const race::CarState> cars, const race::ViewPose& head, const render::Mat4& reflection_view,
                            std::span<const Eye> eyes, size_t focus, int hidden) {
    Impl& m = *impl_;
    Impl::ViewState& v = m.views[0];
    if (!v.view) v.view = std::make_unique<render::View3D>(*app_.gpu());
    if (eyes.empty() || eyes[0].h <= 0) return;
    const float aspect = float(eyes[0].w) / float(eyes[0].h);
    Scene scene = collect(cars, head, focus, hidden, -1, aspect, float(Ui::kHeight));
    scene.options.reflection_view = reflection_view;
    // The sky round each eye, not round the head between them: a dome tens of metres across centred between the eyes stands at
    // that distance in stereo, nearer than the scenery drawn over it; round the eye it has no parallax, as far as it should be.
    // (The items drawn round the camera are placed at its eye: course_scene.cpp, the sun's disc.)
    std::vector<std::pair<size_t, render::Mat4>> round_eye;
    for (size_t k = 0; k < m.items.size(); ++k) {
        const render::Mat4& t = m.items[k].transform;
        if (t.at(0, 3) == head.eye.x && t.at(1, 3) == head.eye.y && t.at(2, 3) == head.eye.z) round_eye.emplace_back(k, t);
    }
    SDL_Renderer* r = ui.renderer();
    for (size_t i = 0; i < eyes.size(); ++i) {
        const Eye& e = eyes[i];
        if (!e.canvas || e.w <= 0 || e.h <= 0) continue;
        if (!m.eye_views[i % m.eye_views.size()]) m.eye_views[i % m.eye_views.size()] = std::make_unique<render::View3D>(*app_.gpu());
        for (const auto& [k, t] : round_eye) m.items[k].transform = render::translate(e.eye - head.eye) * t;
        race::ViewPose pose = head;
        pose.eye = e.eye, pose.view = e.view;
        const render::Frustum& f = e.frustum;
        const render::Mat4 proj = render::frustum(f.left, f.right, f.down, f.up, 0.1f, scene.far * 100.0f);
        SDL_SetRenderTarget(r, e.canvas);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        draw_view(ui, *m.eye_views[i % m.eye_views.size()], {0, 0, float(e.w), float(e.h)}, cars, pose, proj, f, scene);
    }
    for (const auto& [k, t] : round_eye) m.items[k].transform = t;
    ++v.frame;
}

RaceWorld::Scene RaceWorld::collect(std::span<const race::CarState> cars, const race::ViewPose& pose, size_t focus, int hidden, int half, float aspect, float lines) {
    Impl& m = *impl_;
    Scene s;
    const bool split = half >= 0, first_view = half <= 0;
    s.split = split, s.hidden = hidden;
    Impl::ViewState& v = m.views[first_view ? 0 : 1];
    const race::CourseScene::View lists = split ? race::CourseScene::View::Half : race::CourseScene::View::Race;
    const float dt = first_view ? kStep : 0.0f; // (what both views show steps once a picture)
    const race::CarState& me = cars[std::min(focus, cars.size() - 1)];
    const gt3r::race::SunParams& env = course_->env();
    const float far = (env.has_env && env.far_clip > 0 ? env.far_clip : 5000.0f) * (split ? 0.5f : 1.0f);
    s.far = far;

    m.pit_shown = int(focus);
    if (first_view) {
        course_->upload_animations(cars);
        for (Impl::MovingDriver& d : m.drivers) d.posed = false;
    }
    v.fade.update(lens_ && lens_->full_detail ? nullptr : course_->visible_nodes(pose.eye, lists), pose.eye, kStep);
    m.items.clear();
    // The view's axes in course space (the rows of the view matrix's turn).
    const RVec cam_right{pose.view.at(0, 0), pose.view.at(0, 1), pose.view.at(0, 2)}, cam_up{pose.view.at(1, 0), pose.view.at(1, 1), pose.view.at(1, 2)};
    course_->add_items(m.items, pose.eye, lists, &v.fade);
    if (m.sun_disc.active()) {
        // The sun's disc right after the sky (the first item), before the backdrop. GT4 draws it as the shared engine's routine does
        // (fn_00398650 -> 0x4986D8: the sun's RGB x gain / 2, added at As / 128) into the frame that is then multiplied by the
        // picture's scale (Env0 +0x38, docs/formats/course.md "The picture's scale"): once where the engine multiplies the picture;
        // where it does not, the stored colors carry the scale and the disc's passes go in once per unit of it (race::stored_color_scale).
        std::vector<render::DrawItem> disc;
        const int times = std::clamp(int(std::lround(race::stored_color_scale(course_->color_scale()))), 1, 4);
        for (int k = 0; k < times; ++k) m.sun_disc.add_items(disc, pose.eye, cam_right, cam_up);
        for (render::DrawItem& it : disc) it.background = false, it.in_order = true, it.no_fog = true;
        m.items.insert(m.items.begin() + 1, disc.begin(), disc.end());
    }
    if (m.billboards.count() && (course_->env().view_flags[split ? 1 : 0] & 1)) {
        m.billboards.add_items(m.items, cam_right, v.fade.solid());
        for (const gt3r::race::SceneryFade::Group& g : v.fade.faded())
            if (!g.nodes.empty()) m.billboards.add_items(m.items, cam_right, &g.nodes, g.weight);
    }
    // The road's reflection goes here, after the scenery and before the cars (draw_view: each view its own).
    // (A split screen's halves have none but where the course asks for it: Env0 +0x10 bit 24.)
    const ByteSpan env0 = course_->section(course::kSlotEnv);
    s.wet = m.wet.active() && (!split || (env0.size() >= 0x14 && (rd32(env0, 0x10) & 0x1000000)));
    s.wet_at = m.items.size();
    const size_t n = cars_hidden_ ? 0 : std::min(cars.size(), car_visuals_.size());
    m.lights.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const RVec at{cars[i].position.x, cars[i].position.y, cars[i].position.z};
        m.lights[i] = race::car_light(course_->road(), env, at, at.y + 1.0f);
        // (A Gran Turismo 3 car file's shadows go with its body, as its program draws them: none for the in-car view's own car.)
        if (int(i) == hidden && car_visuals_[i]->gt3_file()) continue;
        car_visuals_[i]->add_shadow_items(m.items, cars[i], &m.lights[i]);
    }
    // The cars' levels of detail by their size on screen (race::CarVisual::level): K, the program's
    // factor of the view (0x456918), one unit's height at depth 1 on a picture 448 lines high x 10 / 7:
    // 224 / (0.75 tan(A / 2)) x 10 / 7, A the lens across a 4:3 picture's width (721.2 in the original's
    // chase view, its 0x8465E4 in a saved state).
    race::CarVisual::LodView lod_view;
    lod_view.forward = RVec{-pose.view.at(2, 0), -pose.view.at(2, 1), -pose.view.at(2, 2)};
    lod_view.k = (224.0f / 0.75f) * (10.0f / 7.0f) / std::max(std::tan(pose.fov * 0.5f), 1e-3f);
    if (split) lod_view.k *= lines / float(Ui::kHeight); // (its lines of the picture)
    for (size_t i = 0; i < n; ++i) {
        if (int(i) == hidden) continue;
        const size_t first = m.items.size();
        m.add_drivers(m.items, i, cars[i], pose.eye, lens_ && lens_->full_detail ? nullptr : &lod_view);
        car_visuals_[i]->add_items(m.items, cars[i], pose.eye, dt, lens_ && lens_->full_detail ? nullptr : &lod_view, !(m.pit_live && m.pit_scene && int(i) == m.pit_shown && m.pit_scene->wheels_off(i)), m.motion(i));
        if (const float a = car_alpha(i); a < 1)
            for (size_t k = first; k < m.items.size(); ++k) m.items[k].alpha = a;
        if (m.pit_live && int(i) == m.pit_shown) m.add_pit_crew(m.items, i, *car_visuals_[i]);
        if (m.pit_live && i == 0 && m.license_scene) m.license_scene->add_items(m.items);
        light_items(m.items, first, m.lights[i]);
    }
    if (ghost_state_ && !cars_hidden_ && !car_visuals_.empty()) { // (the ghost: the player's car's model again)
        const size_t first = m.items.size();
        car_visuals_[0]->add_items(m.items, *ghost_state_, pose.eye, 0.0f, lens_ && lens_->full_detail ? nullptr : &lod_view);
        for (size_t k = first; k < m.items.size(); ++k) m.items[k].alpha = ghost_alpha_;
        if (!m.lights.empty()) light_items(m.items, first, m.lights[0]);
    }
    if (!split) course_->add_after_items(m.items);
    if (m.smoke.loaded()) {
        // The puffs after everything else of the scene, facing the camera.
        gt3r::race::TyreSmoke::View sv;
        sv.forward = RVec{-pose.view.at(2, 0), -pose.view.at(2, 1), -pose.view.at(2, 2)};
        sv.proj_x = 1.0f / (std::tan(pose.fovy(aspect) * 0.5f) * std::max(aspect, 1e-3f));
        sv.near_m = pose.near, sv.far_m = far;
        m.smoke.add_items(m.items, pose.eye, cam_right, cam_up, sv);
    }

    render::DrawOptions& o = s.options;
    o = draw_options(*course_, far);
    const float scale = course_->color_scale();
    race::draw_at_picture_scale(o, scale);
    race::light_at_picture_scale(m.items, 0, scale);
    // The cars' reflection picture: the course around the car the view is about, refreshed a face
    // at a time. GT4's map camera (DynamicEnvmapCamera, 0x44EEA8 / 0x44E660) is the shared engine's: 0.8 m above the car, tilted
    // 15 degrees, three panels to 200 m warped at 0.9 into a sphere map, so the engine's lookup is GT4's; the map is 128 pixels
    // across (race + 0xE330: 256 / 2) and 32 bits a pixel (its frame's format 0, PSMCT32), not GT3's 16: no 5-bit steps.
    {
        m.env_items.clear();
        const RVec at{me.position.x, me.position.y + 0.8f, me.position.z};
        const render::Mat4 sky_rot = render::rotate_x(env.sky_pitch * kPi / 180) * render::rotate_y(env.sky_yaw * kPi / 180);
        if (course_->add_env_items(m.env_items, at, sky_rot)) {
            race::light_at_picture_scale(m.env_items, 0, scale);
            render::DrawOptions eo = draw_options(*course_, far);
            race::draw_below_picture_scale(eo, scale);
            eo.env_cube = nullptr;
            eo.draw_distance = 200, eo.far_clip = 0;
            eo.fog = false;
            eo.quantize_rgb5 = false;
            for (float& c : eo.clear) c = 0;
            eo.clear[3] = 1;
            // (Whole when it starts and after a jump: another car, a restart, a replay's start.)
            const bool whole = !v.env_ready || render::length(at - v.env_at) > 20;
            v.env_at = at;
            const unsigned faces = whole ? 0x3Fu : 1u << (v.frame % 6);
            if (v.view->render_env_cube(at, m.env_items, eo, 128, faces)) o.env_cube = v.view->env_cube(), o.env_gt3_lookup = true, v.env_ready = true;
        }
    }
    if (first_view) m.lamps.tick(kStep);
    return s;
}

void RaceWorld::draw_view(Ui& ui, render::View3D& view3d, const SDL_FRect& rect, std::span<const race::CarState> cars, const race::ViewPose& pose,
                          const render::Mat4& proj, const render::Frustum& frustum, const Scene& scene) {
    Impl& m = *impl_;
    const float far = scene.far;
    const bool split = scene.split;
    const int hidden = scene.hidden;
    // The road's reflection: the mirrored scenery and its sky drawn into a small picture, which
    // goes on the road (where the road's mask lets it) after the scenery and before the cars.
    size_t wet_items = 0;
    if (scene.wet) {
        m.wet_items.clear();
        course_->add_reflection_sky_items(m.wet_items, pose.eye);
        // The cars mirrored in the wet road when the course asks for them (Env0 +0x10 bit 16).
        if (m.wet.params().cars && !cars_hidden_)
            for (size_t i = 0; i < std::min(cars.size(), car_visuals_.size()); ++i)
                if (int(i) != hidden) car_visuals_[i]->add_mirror_items(m.wet_items, cars[i], pose.eye, m.motion(i));
        m.wet.add_buffer_items(m.wet_items, pose.view, pose.eye, {});
        for (render::DrawItem& it : m.wet_items) it.tint = course_->reflection_gain();
        race::light_at_picture_scale(m.wet_items, 0, course_->color_scale());
        render::DrawOptions wo = draw_options(*course_, far);
        race::draw_below_picture_scale(wo, course_->color_scale());
        if (SDL_GPUTexture* t = m.wet.render(view3d, pose.view, proj, pose.eye, m.wet_items, wo)) {
            std::vector<render::DrawItem> frame;
            m.wet.add_frame_items(frame, pose.view, frustum, t);
            m.items.insert(m.items.begin() + std::ptrdiff_t(scene.wet_at), frame.begin(), frame.end());
            wet_items = frame.size();
        }
    }
    render::DrawOptions o = scene.options;
    // After the scene: the sun's glare by how much of it the scenery leaves in sight, and the
    // lamps' glows.
    m.overlay.clear();
    m.overlay.rect = rect;
    m.sun.add(m.overlay, rect, pose.view, frustum, sun_visibility(*course_, m.sun.params(), pose.eye, pose.fovy()));
    const gt3r::race::Occluder& occluder = course_->occluder();
    // (The several-view draw tests no glow against the scenery.)
    const auto blocked = [&](const RVec& a, const RVec& b) { return !split && occluder.segment_blocked(a, b); };
    m.lamps.add(m.overlay, rect, pose.view, pose.eye, frustum, blocked, &proj);
    const size_t n = cars_hidden_ ? 0 : std::min(cars.size(), car_visuals_.size());
    if (gt3r::race::CarLampGlows& glows = m.car_lamps.active() ? m.car_lamps : m.car_lamps_lit; glows.active()) {
        // The cars' lamps (RaceCarModel::vf36: none at lamp level 0): at night every car's, by day the lit cars' (the pace
        // car's flashing lamps): the lights, the tail lamps, the brake lamps while braking.
        m.glow_cars.clear();
        for (size_t i = 0; i < n; ++i) {
            if (int(i) == hidden || car_visuals_[i]->lamp_level() == 0) continue;
            gt3r::race::CarLampGlows::Car g;
            g.lamps = &car_visuals_[i]->lamps();
            g.model = car_visuals_[i]->body_matrix(cars[i]);
            g.k = cars[i].brake_lamps ? 1.0f : 0.0f;
            g.brake_now = cars[i].brake_lamps;
            m.glow_cars.push_back(g);
        }
        glows.add(m.overlay, rect, pose.view, pose.eye, frustum, m.glow_cars, blocked, &proj);
    }
    o.overlay = &m.overlay;
    o.post_grade = grade_;
    race::apply_glow(o, glow_);
    o.snapshot = snapshot_, o.dissolve = dissolve_, o.keep_last = keep_last_ || trail_ > 0, o.trail = trail_;
    effects_.apply(o, pose, proj, effect_view_, effect_settings_, far);
    if (lens_) o.dof = lens_->dof, o.vignette = lens_->vignette;
    view3d.draw(ui.renderer(), rect, pose.view, proj, pose.eye, m.items, o);
    if (wet_items) m.items.erase(m.items.begin() + std::ptrdiff_t(scene.wet_at), m.items.begin() + std::ptrdiff_t(scene.wet_at + wet_items));
}

race::MusicMarkers RaceWorld::music_markers(const std::string& file) const {
    race::MusicMarkers m;
    const size_t dot = file.rfind('.');
    if (dot == std::string::npos || !app_.vfs()) return m;
    const std::optional<Bytes> data = app_.vfs()->read(file.substr(0, dot) + ".adm");
    if (!data) return m;
    try {
        const gt3r::sound::MadsMarkers markers = sound::parse_markers(*data);
        if (markers.rate == 0) return m;
        for (u32 s : markers.tracks[0]) m.at.push_back(float(s) / float(markers.rate));
        m.length = float(markers.length) / float(markers.rate);
    } catch (const std::exception& e) {
        LOG_WARN("Markers of %s: %s", file.c_str(), e.what());
    }
    return m;
}

bool RaceWorld::load_dive(race::DiveDirector& d) const {
    const std::shared_ptr<const gt3r::CoreImage> image = app_.disc() ? app_.disc()->program_image() : nullptr;
    return image && d.load(image->program, image->load_address);
}

race::DiveDirector::World RaceWorld::dive_world(std::span<const race::CarState> cars, const race::ReplayScript& script) const {
    race::DiveDirector::World w;
    w.cars = std::min(cars.size(), car_visuals_.size());
    w.script = &script, w.lap_length = lap_length();
    w.car = [this, cars](size_t i) {
        const race::CarVisual& v = *car_visuals_[i];
        return race::DiveDirector::Car{&cars[i], v.body_matrix(cars[i]), v.bounds_min(), v.bounds_max(), v.mounts()};
    };
    w.ground = [this](const RVec& at, float y_max, float& y) { return course_->road().height_at(at.x, at.z, y_max, y); };
    w.blocked = [this](const RVec& a, const RVec& b) { return course_->occluder().segment_blocked(a, b); };
    return w;
}

float RaceWorld::nearest_depth(size_t i, const race::CarState& state, const render::Mat4& view) const {
    if (i >= car_visuals_.size()) return 0;
    const race::CarVisual& v = *car_visuals_[i];
    const render::Mat4 to_view = view * v.body_matrix(state);
    const RVec lo = v.bounds_min(), hi = v.bounds_max();
    float nearest = 1e9f;
    for (int k = 0; k < 8; ++k) nearest = std::min(nearest, -to_view.transform_point({k & 1 ? hi.x : lo.x, k & 2 ? hi.y : lo.y, k & 4 ? hi.z : lo.z}).z);
    return nearest;
}

// A car's length and height, metres (its CHASSIS row's +0x08 / +0x0A, mm: 0x445D90 / 0x445DC0).
std::array<u32, 4> RaceWorld::paint_chip(size_t i) const {
    if (i >= setup_.entries.size() || !app_.db()) return {};
    return race::paint_chip(*app_.db(), setup_.entries[i].label, setup_.entries[i].paint);
}

std::pair<float, float> RaceWorld::car_size(size_t i) const {
    if (session_ && i < setup_.entries.size()) {
        const db::Table* t = app_.db()->table("CHASSIS");
        const db::PartId part = session_->equipment(i).part(sim::Equipment::Chassis);
        if (t && part != db::kNoPart)
            if (const ByteSpan r = t->row(db::part_row_id(part)); r.size() >= 0x0C && rd16(r, 0x08) > 0 && rd16(r, 0x0A) > 0)
                return {float(rd16(r, 0x08)) / 1000, float(rd16(r, 0x0A)) / 1000};
    }
    return {4.4f, 1.35f};
}

// The icons: one picture over the whole page, an orthographic camera looking down -z, each car
// side-on, its front to the left, at its icon's place, as the program's small icon stands
// (CarIconMaker 0x393D20 with its 64 x 32 picture: turned 90 degrees, the middle of its height in the
// picture's, scaled to 0.85 of the picture's width or height, whichever fills first). The program
// makes the pictures once at the race's set-up through a ShowRoomCar (0x364760), in a light of its own
// whatever the course (0x394A10: one light, the light set 0x66's ambient 0.8, diffuse 1.5 and specular
// 1.0 from 0x621480 / 0x621490 / 0x6214A0, toward (1, 0.5, -10.5) from 0x621470; the others off): here
// the same light each frame, no course fog, car lights or reflection of the course. ❓ The light's
// direction in our view (the program's view z taken as into the picture), the reflection the maker
// gives the car, and the edge it darkens (eight faint copies) are not read.
void RaceWorld::draw_icons(Ui& ui, const SDL_FRect& canvas, std::span<const Icon> icons) {
    Impl& m = *impl_;
    if (icons.empty() || !app_.gpu()) return;
    if (!m.icon_view) m.icon_view = std::make_unique<render::View3D>(*app_.gpu());
    constexpr float kIconWidth = 64, kIconHeight = 32, kFill = 0.85f;
    constexpr float kMetre = 0.085f; // metres a page unit: a car of 4.4 m about 52 units long
    std::vector<render::DrawItem> items;
    float opacity = 0;
    for (const Icon& ic : icons) {
        if (ic.car >= car_visuals_.size() || ic.opacity <= 0) continue;
        const race::CarVisual& v = *car_visuals_[ic.car];
        const race::CarSpec& spec = v.spec();
        const auto [length, height] = car_size(ic.car);
        const float units = length / height < kIconWidth / kIconHeight ? kIconHeight / height : kIconWidth / length; // (page units a metre)
        float k = units * kFill * kMetre;
        const render::Mat4 at = render::translate({ic.x * kMetre, -ic.y * kMetre, -20});
        render::Mat4 turn = render::rotate_y(kPi / 2);
        if (ic.large) {
            // Kind 0 (0x394100): 30 units a metre (the lens's 18 degrees taken as none), turned as far as the car fills the picture's
            // width, 158 degrees at most, and 2 degrees down.
            k = 30 * kMetre;
            const float half = 128.0f / 30 / 2, L = length / 2, W = (2 * spec.half_track[0] + 0.25f) / 2, d2 = L * L + W * W;
            float yaw = 158;
            if (d2 > half * half) {
                const float root = std::sqrt(d2 - half * half) * L;
                float c = (W * half + root) / d2;
                if (c > 1 || c < -1) c = (W * half - root) / d2;
                yaw = std::min(158.0f, (180 - std::acos(std::clamp(c, -1.0f, 1.0f)) * 180 / kPi) * 1.05f);
            }
            turn = render::rotate_x(2 * kPi / 180) * render::rotate_y(yaw * kPi / 180);
        }
        const render::Mat4 model = at * turn * render::translate({0, -height / 2, 0});
        const render::Mat4 scaled = at * render::scale({k, k, k}) * render::translate({-ic.x * kMetre, ic.y * kMetre, 20});
        const size_t first = items.size();
        auto apply = [&](const RVec& p, float w) {
            return RVec{model.at(0, 0) * p.x + model.at(0, 1) * p.y + model.at(0, 2) * p.z + model.at(0, 3) * w,
                        model.at(1, 0) * p.x + model.at(1, 1) * p.y + model.at(1, 2) * p.z + model.at(1, 3) * w,
                        model.at(2, 0) * p.x + model.at(2, 1) * p.y + model.at(2, 2) * p.z + model.at(2, 3) * w};
        };
        auto cv = [](const RVec& a) { return course::Vec3{a.x, a.y, a.z}; };
        race::CarState st;
        st.position = cv(apply({0, spec.ride_height[0], 0}, 1));
        st.right = cv(render::normalize(apply({1, 0, 0}, 0)));
        st.up = cv(render::normalize(apply({0, 1, 0}, 0)));
        st.forward = cv(render::normalize(apply({0, 0, -1}, 0)));
        for (int w = 0; w < 4; ++w) {
            const int axle = w / 2;
            const float x = (w & 1) ? spec.half_track[axle] : -spec.half_track[axle];
            const float z = axle == 0 ? -spec.wheelbase / 2 : spec.wheelbase / 2;
            st.wheels[w].center = cv(apply({x, spec.tire_radius[axle], z}, 1));
        }
        v.add_items(items, st, RVec{st.position.x, st.position.y, st.position.z + 100});
        for (size_t j = first; j < items.size(); ++j) items[j].transform = scaled * items[j].transform; // (the car's own metres to the icon's)
        opacity = std::max(opacity, ic.opacity);
    }
    if (items.empty()) return;
    // Orthographic, depth reversed (1 at the near plane, 0 at the far one) as the 3D view's.
    const float w = 640 * kMetre, h = 480 * kMetre, zn = 1, zf = 60;
    render::Mat4 proj;
    proj.m.fill(0);
    proj.at(0, 0) = 2 / w, proj.at(0, 3) = -1;
    proj.at(1, 1) = 2 / h, proj.at(1, 3) = 1;
    proj.at(2, 2) = 1 / (zf - zn), proj.at(2, 3) = zf / (zf - zn);
    proj.at(3, 3) = 1;
    render::DrawOptions o;
    o.sort_blended = true;
    for (float& c : o.clear) c = 0;
    o.composite_alpha = std::clamp(opacity, 0.0f, 1.0f);
    o.gt3_lighting = true;
    for (int i = 0; i < 3; ++i) o.light_ambient[i] = 0.8f, o.light_diffuse[i] = 1.5f, o.light_specular[i] = 1.0f;
    o.light_dir = render::normalize(RVec{1, 0.5f, 10.5f});
    o.reflection_strength = 0;
    m.icon_view->draw(ui.renderer(), canvas, render::Mat4::identity(), proj, RVec{0, 0, 100}, items, o);
}

void RaceWorld::draw_model(Ui& ui, const SDL_FRect& rect, const render::GpuModel& model, const render::Mat4& place, const render::Mat4& proj, float alpha, bool own_reflection,
                           const render::ItemLights* lights) {
    Impl& m = *impl_;
    if (!app_.gpu() || alpha <= 0) return;
    if (!m.icon_view) m.icon_view = std::make_unique<render::View3D>(*app_.gpu());
    std::vector<render::DrawItem> items{render::DrawItem{&model, place, 0}};
    items[0].sphere_map = own_reflection; // (the reflection picture is in the model)
    items[0].lights = lights;
    render::DrawOptions o = draw_options(*course_, 100);
    o.fog = false;
    o.frustum_cull = false;
    for (float& c : o.clear) c = 0;
    o.composite_alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (!own_reflection && m.views[0].view && m.views[0].env_ready) o.env_cube = m.views[0].view->env_cube(), o.env_gt3_lookup = true;
    m.icon_view->draw(ui.renderer(), rect, render::Mat4::identity(), proj, RVec{0, 0, 0}, items, o);
}

// The rear-view mirror (docs/formats/course.md "Drawing", step 9: the mirror's fog, its sky,
// slot 1 by slot 3's list, the cars). ❓ Its lens and depth are the shared engine's mirror's (the
// in-car view's angle zoomed 1.3 times, 120 m), not read from this program.
void RaceWorld::render_mirror(Ui& ui, const SDL_FRect& rect, std::span<const race::CarState> cars, const race::ViewPose& pose, int hidden, int half, float zoom) {
    Impl& m = *impl_;
    if (!app_.gpu() || rect.w <= 0 || rect.h <= 0) return;
    Impl::ViewState& v = m.views[half <= 0 ? 0 : 1];
    if (!v.mirror) v.mirror = std::make_unique<render::View3D>(*app_.gpu());
    const float kDepth = half >= 0 ? 60 : 120;
    m.mirror_items.clear();
    course_->add_items(m.mirror_items, pose.eye, half >= 0 ? race::CourseScene::View::HalfMirror : race::CourseScene::View::Mirror);
    const size_t n = std::min(cars.size(), car_visuals_.size());
    for (size_t i = 0; i < n; ++i) {
        if (int(i) == hidden) continue;
        const race::CarLight* light = i < m.lights.size() ? &m.lights[i] : nullptr;
        car_visuals_[i]->add_shadow_items(m.mirror_items, cars[i], light);
        const size_t first = m.mirror_items.size();
        m.add_drivers(m.mirror_items, i, cars[i], pose.eye, nullptr);
        car_visuals_[i]->add_items(m.mirror_items, cars[i], pose.eye, half <= 0 ? kStep : 0.0f, nullptr, true, m.motion(i));
        if (light) light_items(m.mirror_items, first, *light);
        if (const float a = car_alpha(i); a < 1)
            for (size_t k = first; k < m.mirror_items.size(); ++k) m.mirror_items[k].alpha = a;
    }
    if (ghost_state_ && !car_visuals_.empty()) {
        const size_t first = m.mirror_items.size();
        car_visuals_[0]->add_items(m.mirror_items, *ghost_state_, pose.eye, 0.0f);
        for (size_t k = first; k < m.mirror_items.size(); ++k) m.mirror_items[k].alpha = ghost_alpha_;
    }
    render::DrawOptions o = draw_options(*course_, kDepth);
    o.mirrored = true;
    o.draw_distance = kDepth;
    o.far_clip = 0;
    o.post_grade = grade_;
    const gt3r::race::SunParams& env = course_->env();
    if (env.has_env) {
        o.fog = true, o.fog_gt3 = true;
        o.fog_start = std::max(env.mirror_fog_a, 0.0f);
        o.fog_max = std::clamp(1.0f - env.mirror_fog_b, 0.0f, 1.0f);
        for (int i = 0; i < 3; ++i) o.fog_color[i] = std::clamp(env.mirror_fog_rgb[size_t(i)], 0.0f, 1.0f);
    }
    race::draw_at_picture_scale(o, course_->color_scale());
    race::light_at_picture_scale(m.mirror_items, 0, course_->color_scale());
    if (v.view && v.env_ready) o.env_cube = v.view->env_cube(), o.env_gt3_lookup = true;
    const float t = std::tan(pose.fovy() * 0.5f) / zoom;
    const render::Mat4 proj = render::perspective(2.0f * std::atan(t), rect.w / rect.h, 0.1f, kDepth);
    const render::Mat4 view = render::scale({1, 1, -1}) * pose.view;
    SDL_SetRenderDrawColor(ui.renderer(), 0, 0, 0, 255);
    SDL_RenderFillRect(ui.renderer(), &rect);
    v.mirror->draw(ui.renderer(), rect, view, proj, pose.eye, m.mirror_items, o);
    // Its frame: a thin light edge (❓ judged from the original's in-car view).
    SDL_SetRenderDrawColor(ui.renderer(), 200, 200, 200, 255);
    SDL_RenderRect(ui.renderer(), &rect);
}

std::function<void(sim::Session& solo, net::RemoteCar& car)> RaceWorld::prediction_tire_change() const {
    db::SpecDb* db = app_.db();
    if (!program_ || !db || !app_.disc()) return {};
    gt3r::Volume* vol = &app_.disc()->volume();
    const std::shared_ptr<const sim::Program> program = program_;
    return [db, vol, program](sim::Session& solo, net::RemoteCar& car) {
        net::tire_change_on(*db, *program, [vol](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        })(solo, car);
    };
}

} // namespace gt4r
