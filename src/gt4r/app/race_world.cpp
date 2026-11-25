#include "gt4r/app/race_world.h"
#include "gt4r/sim/car_spec.h"

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
#include "gt4r/race/driver_figure.h"
#include "gt4r/race/start_rule.h"
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
    std::unique_ptr<render::View3D> icon_view; // the results' car icons
    std::unique_ptr<render::View3D> mirror_view; // the rear-view mirror
    std::vector<render::DrawItem> mirror_items;
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
    // The drivers' skeleton in its stored pose (every driver set's skeleton has the same sizes).
    const std::optional<Bytes> driver_skeleton = app.vfs()->read("character/driver.mdl");
    std::map<std::string, std::optional<Bytes>> character_files;
    for (const WorldEntry& e : setup.entries) {
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
        std::unique_ptr<race::CarVisual>& visual = m.visuals_by_model[entry->model + "#" + std::to_string(e.paint) + "#" + wing];
        if (!visual) {
            const std::optional<Bytes> file = app.vfs()->read("car/lod/" + entry->model);
            if (!file || (!car::CarFile::is_car(*file) && !race::CarVisual::is_gt3_car(*file))) throw FormatError("This car has no race model on the disc (" + entry->model + ").");
            const std::optional<Bytes> pat = app.vfs()->read("car/lod/" + entry->model + ".pat");
            const std::vector<u32> paints = race::car_paints(*app.db(), entry->label);
            race::CarVisual::Options o;
            o.paint = paints.empty() ? e.paint : paints[e.paint % paints.size()];
            o.lamp_level = night_ ? 1 : 0;
            const std::optional<Bytes> wing_file = wing.empty() ? std::nullopt : app.vfs()->read("wing/lod/" + wing);
            if (wing_file) o.wing = *wing_file;
            if (driver_skeleton) o.driver_skeleton = *driver_skeleton;
            o.driver = setup.drivers;
            if (setup.drivers && car::CarFile::is_car(*file) && !car::CarFile::parse(*file).driver) try {
                    // A car without a driver of its own: a character file by its cockpit kind (0x3B5438).
                    const race::CockpitPlaces cp = race::read_cockpit(*file);
                    const std::string path = race::character_driver_file(cp.cockpit_kind, cp.co_driver, int(car_visuals_.size()));
                    if (!path.empty()) {
                        auto [it, fresh] = character_files.try_emplace(path);
                        if (fresh) it->second = app.vfs()->read(path);
                        if (it->second) o.driver_set = *it->second;
                    }
                } catch (const std::exception&) {
                }
            visual = std::make_unique<race::CarVisual>(gpu, *file, pat ? ByteSpan(*pat) : ByteSpan{}, spec, o, entry->label);
            // The course's strengths of the three lamp lists (Env0 +0x1794: lights, brake, tail).
            if (const ByteSpan e0 = course_->section(course::kSlotEnv); e0.size() >= 0x17A0) {
                float v[3];
                std::memcpy(v, e0.data() + 0x1794, 12);
                visual->set_lamp_strengths(v[0], v[1], v[2]);
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
    // The race's sound: every car, the course's ambience, the count and the music.
    if (audio::AppSound* sound = app.sound()) {
        gt3r::AudioEngine& engine = app.audio();
        gt3r::Vfs& vfs = *app.vfs();
        const audio::StreamPlayer music = sound->streams();
        const Bytes program = app.disc() ? app.disc()->read_program() : Bytes{};
        m.sound = std::make_unique<audio::RaceSound>(vfs, engine.voices(), sound->driver(), sound->library_ptr(), audio::RaceSoundTables::from_core(program), music);
        for (const WorldEntry& e : setup.entries)
            if (!m.sound->add_car(e.label)) LOG_WARN("Drive: no engine sound for %s", e.label.c_str());
        m.sound->set_course(course_->section(course::kSlotSound), std::make_shared<const course::Runway>(course_->road()));
        // The volumes and output of the game's options, and the player's play list of racing music.
        m.sound->set_options(audio::race_sound_options(setup.options));
        if (setup.playlist) m.sound->set_playlist(setup.playlist);
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
    starts_.assign(setup_.entries.size(), {});
    if (!start_course_) start_course_ = std::make_unique<race::StartCourse>(course_->road(), course_->section(course::kSlotRunway), race::course_start(*app_.db(), setup_.course).way);
    start_course_->set_race(setup_.conductor, setup_.event_row, setup_.section);
    if (!setup_.rail && app_.disc()) {
        try {
            const sim::Program program = sim::Program::load(*app_.disc());
            // The materials off the course (0x622430, read by 0x3F4EA0).
            for (u32 m = 0; m < soft_.size(); ++m) soft_[m] = program.w(sim::Program::kSoft + 4 * m);
            std::vector<sim::Entry> entries(setup_.entries.size());
            for (size_t i = 0; i < entries.size(); ++i) {
                const WorldEntry& we = i < overrides_.size() && overrides_[i] ? *overrides_[i] : setup_.entries[i];
                if (we.equipment) entries[i].equipment = *we.equipment; // a garage car's own
                else if (!sim::stock_car(*app_.db(), program, we.label, entries[i].equipment)) throw std::runtime_error("no parts for " + we.label);
                entries[i].condition = we.condition;
                entries[i].grid_place = we.grid_place;
                entries[i].automatic = we.automatic;
                entries[i].asm_on = we.asm_on, entries[i].tcs_on = we.tcs_on;
                entries[i].control = i > 0 ? 2 : computer_player_ ? 1 : 0;
                // An event's race counts fuel (0x35E108 sets the rule block's +0x4C to 80; vf26 0x34CA20 clears it on a course
                // whose row's +0x14 is 0, a loose course and in a run alone, 0x3F7058 a training's and a mission's); a run
                // without laps (practice, a free run) runs on the free-run conductor, whose rule block holds 0 there (the
                // original's Practice state).
                entries[i].fuel = setup_.event_row.size() > 0x43 && setup_.event_row[0x43] != 0 && setup_.conductor == race::Conductor::Other &&
                                  !setup_.pace.solo && race::course_start(*app_.db(), setup_.course).fuel;
            }
            plan_starts(program, entries);
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
            session_ = std::make_unique<sim::Session>(*app_.db(), program, std::make_shared<const course::Runway>(course_->road()), entries,
                                                      sim::course_row(*app_.db(), setup_.course), &options);
            if (!noted_)
                for (const std::string& note : session_->notes()) LOG_INFO("Drive: %s", note.c_str());
            sim_ = session_.get();
            { // the race's class grips (the rule block's +0x18 .. +0x1B from the row's +0x53 .. +0x56, 0x35E108)
                std::array<gt3r::s8, 4> grip{};
                if (setup_.event_row.size() > 0x56)
                    for (size_t k = 0; k < 4; ++k) grip[k] = gt3r::s8(setup_.event_row[0x53 + k]);
                session_->set_class_grip(setup_.event_grips ? grip : sim::rules::single_race_class_grip(grip));
            }
            if (setup_.event_row.size() >= 0x74) { // the event's tire wear (the rule block's +0x2C .. +0x34, 0x35E108)
                u8 rule_block[0x80] = {};
                sim::rules::event_wear_bytes(setup_.event_row.data(), rule_block);
                session_->set_wear_rules(rule_block, !session_->loose(), program);
            }
            // The computer drivers: every car but the player's (his too when he watches).
            // The player's own car keeps a driver that plans but does not drive (the program's pad car: its decision each
            // step, the suggested gear from it).
            std::vector<size_t> computer;
            for (size_t i = 0; i < setup_.entries.size(); ++i) computer.push_back(i);
            field_.reset(), pits_.reset(), bspec_driven_ = false;
            if (!computer.empty()) {
                try {
                    field_ = sim::ai::Field::open(*app_.disc(), setup_.course, *session_, computer, setup_.pace);
                    // B-spec: the player's car's driver takes his commands.
                    if (computer_player_) {
                        field_->set_bspec(0);
                        // The driver's record of the book for the car's class (0x468710) and its skill from it (0x468C50).
                        const int k = sim::ai::bspec_class(gt3r::ee::F(session_->rating(0)));
                        // (the skill weighs the record's course marks where the race's course flag is set: cond + 0xCBE7, the
                        // start code's second digit, 0x34C7E0; without marks they are 0)
                        const bool course_flag = race::start_code_digit(race::course_start(*app_.db(), setup_.course).code, 1) > 0;
                        const std::array<u8, 0x40> rec =
                            menu::bspec::race_record(setup_.bspec_book, k, setup_.bspec_course_known ? setup_.bspec_course_marks.data() : nullptr);
                        field_->set_bspec_record(0, rec, k);
                        if (const std::optional<sim::ai::BSpecSkill> sk = sim::ai::bspec_skill(sim::ai::BSpecTables::load(program),
                                                                                                 sim::ai::BSpecRecord::from_bytes(rec.data()),
                                                                                                 gt3r::ee::F(session_->rating(0)), course_flag))
                            field_->set_bspec_skill(0, sk->corner, sk->brake, sk->alongside);
                        bspec_driven_ = true;
                    } else field_->set_shadow(0);
                    for (size_t c = 0; c < commands_.size(); ++c) field_->command(c, {commands_[c].pace - 1, commands_[c].overtake ? 1 : 0});
                    // (their aids: the player's too, which a car its driver drives in its rolling start takes)
                    for (size_t car : computer) session_->set_driver_aids(car, field_->params(car).aids, field_->params(car).grip);
                    first_lines(program);
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
                    sim::ai::PitRace pr;
                    pr.laps = setup_.laps;
                    pits_ = std::make_unique<sim::ai::PitStops>(*field_, program, setup_.entries.size(), pit_hooks, pr);
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
    for (size_t i = 0; i < entries.size(); ++i) on_line = on_line || (i < overrides_.size() && overrides_[i] ? *overrides_[i] : setup_.entries[i]).start_speed > 0;
    if (on_line && !start_course_->has_lines() && !road.gtrw && app_.disc())
        if (const std::optional<course::AiData> data = sim::ai::Field::read(*app_.disc(), setup_.course)) start_course_->add_lines(*data, sim::ai::Tables::load(program));
    starts_.clear();
    for (size_t i = 0; i < entries.size(); ++i) {
        const WorldEntry& we = i < overrides_.size() && overrides_[i] ? *overrides_[i] : setup_.entries[i];
        std::optional<race::CarStart> rolling;
        if (we.start_speed > 0 && !std::isnan(we.start_distance)) { // a single race's rolling start (0x347618)
            rolling.emplace();
            rolling->kind = we.start_distance < 0 ? 2 : 1;
            rolling->distance = int(we.start_distance), rolling->speed = int(std::lround(we.start_speed * 3.6f));
        }
        starts_.push_back(race::plan_start(r, i, we.grid_place >= 0 ? we.grid_place : int(i), rolling));
        const race::PlannedStart& p = starts_.back();
        entries[i].line_start = p.line, entries[i].place = p.place;
        entries[i].start_speed = p.line ? 0.0f : we.start_speed;
        if (rolling && !p.line) entries[i].start_distance = we.start_distance; // (no driving line: the road's middle)
    }
}

int RaceWorld::start_crossings(size_t car) const {
    return car < starts_.size() ? race::planned_crossings(start_course_->race(), starts_[car], sim_->cars()[car].course_v) : 0;
}

// The line each car's driver starts on (0x363B10: on the grid a computer's car by its place, 0x620AD8's row of six, mirrored
// where the second place lies on the first's other side; a car started on a driving line the main line, the player's the
// passing line 7), and the cars their drivers hold in a rolling start.
void RaceWorld::first_lines(const sim::Program& program) {
    if (!program.known() || !field_ || course_->road().gtrw) return;
    const course::Runway& road = course_->road();
    for (size_t i = 0; i < starts_.size(); ++i) {
        const int kind = starts_[i].start.kind;
        const bool pad = i == 0 && !computer_player_;
        int line = 6;
        if (kind < 2) {
            if (pad) line = 7;
            else if (start_course_->race().way != 1 && road.grid.size() >= 2) {
                const WorldEntry& we = i < overrides_.size() && overrides_[i] ? *overrides_[i] : setup_.entries[i];
                const int place = we.grid_place >= 0 ? we.grid_place : int(i);
                line = int(program.w(0x620AD8 + 4 * u32(place % 6)));
                const float h = road.grid[0].heading + kPi, dx = road.grid[0].pos.x - road.grid[1].pos.x, dz = road.grid[0].pos.z - road.grid[1].pos.z;
                if (!(dx * std::cos(h) - std::sin(h) * dz < 0) && (line == 1 || line == 2)) line = 3 - line;
            }
        }
        field_->start_line(i, line);
        if (session_->cars()[i].rolling) field_->hold_rolling(i);
    }
}

void RaceWorld::set_entry_overrides(std::vector<std::optional<WorldEntry>> overrides) {
    overrides_ = std::move(overrides);
    restart();
}

void RaceWorld::restart() {
    build_simulation();
    gadget_bodies_.reset(course_->gadgets(), course_->road());
    course_->restart_animations();
    clear_effects();
}

void RaceWorld::step(std::span<const race::CarInput> inputs, bool hold, bool player_by_computer) {
    // The computer drivers write their own cars' inputs (the player's too while they drive it).
    std::vector<race::CarInput> driven;
    if (field_) {
        driven.assign(inputs.begin(), inputs.end());
        driven.resize(car_visuals_.size());
        hold ? field_->hold(driven) : field_->drive(driven);
        if (!hold && session_) field_->apply_time_scales(*session_); // the rubber band's step lengths
        // The pad cars' active steering aims along their driver's line (0x348850 → 0x4649F0, after their decision).
        if (!hold && session_)
            for (size_t i = 0; i < driven.size(); ++i) session_->set_line_heading(i, field_->line_heading_at(i), field_->line_mode_at(i));
        if (computer_player_ && !player_by_computer && !inputs.empty()) driven[0] = inputs[0];
        if (!inputs.empty() && inputs[0].pad) driven[0].pad = inputs[0].pad; // (a demonstration's record, kept while its driver drives)
        if (!hold && pits_) pits_->before(*field_, driven); // (a car standing in its box held)
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
    if (hold && session_) session_->hold_once(inputs);
    else if (hold) {
        // The stand-in has no hold: the cars stand on their brakes.
        std::vector<race::CarInput> held(inputs.begin(), inputs.end());
        held.resize(car_visuals_.size());
        for (race::CarInput& in : held) in = {}, in.brake = 1;
        sim_->step(held, kStep);
    } else sim_->step(inputs, kStep);
    if (!hold && pits_ && session_) pits_->after(*session_, *field_);
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
            if (session_) {
                const auto& r = session_->record(i);
                b.pos = {r.pos.x.host(), r.pos.z.host(), -r.pos.y.host()};
                b.yaw_rate = r.yaw_rate.host();
                b.front = r.p.body_front.host(), b.rear = r.p.body_rear.host(), b.half_width = r.p.body_width.host() / 2;
            } else {
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

float RaceWorld::fuel_share(size_t i) const {
    if (!session_ || i >= car_visuals_.size()) return 1.0f;
    const float tank = session_->tank(i);
    return tank > 0 ? std::clamp(session_->fuel(i) / tank, 0.0f, 1.0f) : 1.0f;
}

float RaceWorld::car_mass(size_t i) const {
    // The parameter block's mass, P +0x48 (what the license rule 0x35FE80 divides a wall hit by).
    return session_ && i < car_visuals_.size() ? session_->record(i).p.mass.host() : 0.0f;
}

// Smoke and dust from the wheels for a step: a puff where a tire slides on the road, dust and
// spray from any wheel that turns on a loose or wet surface. The kind of puff is the course's,
// by the surface under the wheel.
void RaceWorld::present(std::span<const race::CarState> cars, const audio::SoundListener* camera, int target, bool live) {
    Impl& m = *impl_;
    if (camera && m.sound) {
        if (live && session_) m.sound->update(*session_, *camera, target);
        else m.sound->update(cars, *camera, target);
        // The pit scene of the car shown (0x3CFA98 through 0x35BE38): the stop's "tires on" bit (F +0x4B8 0x200) taken
        // and cleared, the jack let down.
        if (live && pits_ && target >= 0 && size_t(target) < car_visuals_.size()) {
            sim::ai::PitCar& pc = pits_->car(size_t(target));
            if (pc.service & 0x200) pc.service = u16(pc.service ^ 0x200), m.sound->pit_effect("jackdown");
        }
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
        // The cars mirrored in the wet road when the course asks for them (Env0 +0x10 bit 16).
        if (m.wet.params().cars)
            for (size_t i = 0; i < std::min(cars.size(), car_visuals_.size()); ++i)
                if (int(i) != hidden) car_visuals_[i]->add_mirror_items(m.wet_items, cars[i], pose.eye);
        m.wet.add_buffer_items(m.wet_items, pose.view, pose.eye, {});
        for (render::DrawItem& it : m.wet_items) it.tint = course_->reflection_gain();
        const render::DrawOptions wo = draw_options(*course_, far);
        if (SDL_GPUTexture* t = m.wet.render(*m.view, pose.view, proj, pose.eye, m.wet_items, wo)) m.wet.add_frame_items(m.items, pose.view, pose.fovy(), aspect, t);
    }
    const size_t n = std::min(cars.size(), car_visuals_.size());
    for (size_t i = 0; i < n; ++i) car_visuals_[i]->add_shadow_items(m.items, cars[i]);
    // The cars' levels of detail by their size on screen (race::CarVisual::level): K, the program's
    // factor of the view (0x456918), one unit's height at depth 1 on a picture 448 lines high x 10 / 7:
    // 224 / (0.75 tan(A / 2)) x 10 / 7, A the lens across a 4:3 picture's width (721.2 in the original's
    // chase view, its 0x8465E4 in a saved state).
    race::CarVisual::LodView lod_view;
    lod_view.forward = RVec{-pose.view.at(2, 0), -pose.view.at(2, 1), -pose.view.at(2, 2)};
    lod_view.k = (224.0f / 0.75f) * (10.0f / 7.0f) / std::max(std::tan(pose.fov * 0.5f), 1e-3f);
    for (size_t i = 0; i < n; ++i) {
        if (int(i) == hidden) continue;
        car_visuals_[i]->add_items(m.items, cars[i], pose.eye, kStep, &lod_view);
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
    o.post_grade = grade_;
    o.snapshot = snapshot_, o.dissolve = dissolve_, o.keep_last = keep_last_;
    m.view->draw(ui.renderer(), rect, pose.view, proj, pose.eye, m.items, o);
    if (m.frame == 5)
        LOG_INFO("Drive: the car at (%.1f, %.1f, %.1f) heading (%.2f, %.2f), eye (%.1f, %.1f, %.1f); %d meshes drawn, %zu triangles", me.position.x,
                 me.position.y, me.position.z, me.forward.x, me.forward.z, pose.eye.x, pose.eye.y, pose.eye.z, m.view->stats().meshes_drawn,
                 m.view->stats().triangles);
    ++m.frame;
}

// The icons: one picture over the whole page, an orthographic camera looking down -z, each car
// turned side-on (its front to the left, a little toward the camera) and tilted to be seen from
// slightly above, at its icon's place. ❓ The program renders them once per car with its own camera
// (CarIconMaker, 0x393B70: a 128 x 64 and a 64 x 32 picture); the angles and the size are judged
// from the original's results page.
void RaceWorld::draw_icons(Ui& ui, const SDL_FRect& canvas, std::span<const Icon> icons) {
    Impl& m = *impl_;
    if (icons.empty() || !app_.gpu()) return;
    if (!m.icon_view) m.icon_view = std::make_unique<render::View3D>(*app_.gpu());
    constexpr float kDegree = kPi / 180;
    constexpr float kMetre = 0.085f; // metres a page unit: a car of 4.4 m about 52 units long
    std::vector<render::DrawItem> items;
    float opacity = 0;
    for (const Icon& ic : icons) {
        if (ic.car >= car_visuals_.size() || ic.opacity <= 0) continue;
        const race::CarVisual& v = *car_visuals_[ic.car];
        const race::CarSpec& spec = v.spec();
        const render::Mat4 model = render::translate({ic.x * kMetre, -ic.y * kMetre, -20}) *
                                   render::rotate_x(10 * kDegree) * render::rotate_y(100 * kDegree) *
                                   render::translate({0, -(spec.ride_height[0] + spec.tire_radius[0]) / 2, 0});
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
    render::DrawOptions o = draw_options(*course_, zf);
    o.fog = false;
    o.frustum_cull = false;
    for (float& c : o.clear) c = 0;
    o.composite_alpha = std::clamp(opacity, 0.0f, 1.0f);
    if (m.view && m.env_ready) o.env_cube = m.view->env_cube(), o.env_gt3_lookup = true;
    m.icon_view->draw(ui.renderer(), canvas, render::Mat4::identity(), proj, RVec{0, 0, 100}, items, o);
}

void RaceWorld::draw_model(Ui& ui, const SDL_FRect& rect, const render::GpuModel& model, const render::Mat4& place, const render::Mat4& proj, float alpha) {
    Impl& m = *impl_;
    if (!app_.gpu() || alpha <= 0) return;
    if (!m.icon_view) m.icon_view = std::make_unique<render::View3D>(*app_.gpu());
    std::vector<render::DrawItem> items{render::DrawItem{&model, place, 0}};
    render::DrawOptions o = draw_options(*course_, 100);
    o.fog = false;
    o.frustum_cull = false;
    for (float& c : o.clear) c = 0;
    o.composite_alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (m.view && m.env_ready) o.env_cube = m.view->env_cube(), o.env_gt3_lookup = true;
    m.icon_view->draw(ui.renderer(), rect, render::Mat4::identity(), proj, RVec{0, 0, 0}, items, o);
}

// The rear-view mirror (docs/formats/course.md "Drawing", step 9: the mirror's fog, its sky,
// slot 1 by slot 3's list, the cars). ❓ Its lens and depth are the shared engine's mirror's (the
// in-car view's angle zoomed 1.3 times, 120 m), not read from this program.
void RaceWorld::render_mirror(Ui& ui, const SDL_FRect& rect, std::span<const race::CarState> cars, const race::ViewPose& pose, int hidden) {
    Impl& m = *impl_;
    if (!app_.gpu() || rect.w <= 0 || rect.h <= 0) return;
    if (!m.mirror_view) m.mirror_view = std::make_unique<render::View3D>(*app_.gpu());
    constexpr float kDepth = 120;
    m.mirror_items.clear();
    course_->add_items(m.mirror_items, pose.eye, race::CourseScene::View::Mirror);
    const size_t n = std::min(cars.size(), car_visuals_.size());
    for (size_t i = 0; i < n; ++i) {
        if (int(i) == hidden) continue;
        car_visuals_[i]->add_shadow_items(m.mirror_items, cars[i]);
        car_visuals_[i]->add_items(m.mirror_items, cars[i], pose.eye, kStep);
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
    if (m.view && m.env_ready) o.env_cube = m.view->env_cube(), o.env_gt3_lookup = true;
    const float t = std::tan(pose.fovy() * 0.5f) / 1.3f;
    const render::Mat4 proj = render::perspective(2.0f * std::atan(t), rect.w / rect.h, 0.1f, kDepth);
    const render::Mat4 view = render::scale({1, 1, -1}) * pose.view;
    SDL_SetRenderDrawColor(ui.renderer(), 0, 0, 0, 255);
    SDL_RenderFillRect(ui.renderer(), &rect);
    m.mirror_view->draw(ui.renderer(), rect, view, proj, pose.eye, m.mirror_items, o);
    // Its frame: a thin light edge (❓ judged from the original's in-car view).
    SDL_SetRenderDrawColor(ui.renderer(), 200, 200, 200, 255);
    SDL_RenderRect(ui.renderer(), &rect);
}

} // namespace gt4r
