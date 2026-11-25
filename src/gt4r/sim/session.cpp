#include "gt4r/sim/session.h"

#include "sim/exact/course_ground.h"

#include "gt4r/sim/convert.h"
#include "gt4r/sim/rules_hybrid.h"
#include "gt4r/sim/rules_speeds.h"
#include "gt4r/sim/rules_wheels.h"
#include "gt4r/sim/rules_build.h"
#include "gt4r/sim/rules_start.h"

#include "base/ee_math.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/sim/exact_bridge.h"
#include "sim/exact/build_math.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/race_flow.h"
#include "sim/exact/race_inputs.h"
#include "sim/exact/start.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace gt4r::sim {

namespace ex = gt3r::sim::exact;
using gt3r::ee::F;

namespace {

const F k0 = F::raw(0), kHalf = F::raw(0x3F000000);
const F kPi = F::raw(0x40490FDB);
const F kNoGround = F::raw(0xFF7FFFFF); // the query's height when nothing is under the point

// The record's frame (x, y, z up) from the course's (x, y up, z): (x, -z, y), and back.
Vec3 to_course(const ex::Vec3& v) { return {v.x.host(), v.z.host(), -v.y.host()}; }

// A surface is loose (the spec's tire block class 1: 2 .. 5 and 13 .. 15).
bool loose_surface(int s) { return (s >= 2 && s <= 5) || (s >= 13 && s <= 15); }

// The road's answer into a wheel's ground record (0x3956F8): the surface as its row of the core's tables, +0x0C the
// triangle and its cluster (cluster << 16 | triangle: wheel 0's is the collision step's hint). A miss only sets +0x30.
// With Gran Turismo 4's rules the record keeps the triangle's material byte (+0x31) and the wheel's surface is the 24-row
// index (0x3F4E40, as 0x369888 sets wheel +0x89); with the engine's rules both are the core's 16-row index.
void fill(ex::Ground& g, bool hit, const GroundHit& h, bool gt4) {
    if (!hit) {
        g.miss = 1;
        return;
    }
    g.miss = 0;
    g.height = F(h.point.y);
    g.attr_a = F(h.attr_a), g.attr_b = F(h.attr_b);
    if (!gt4) g.attr_a_raw = u16(h.attr_a * 65535.0f), g.attr_b_raw = u16(h.attr_b * 65535.0f); // (0x3956F8 leaves +0x2C alone)
    g.normal = {F(h.normal.x), F(h.normal.y), F(h.normal.z)}, g.plane_d = F(h.plane_d);
    g.seg = s32(u32(u16(s16(h.cluster))) << 16 | u32(u16(h.triangle)));
    g.material = gt4 ? h.material : u8(core_surface(surface_index(h.material, h.flags)));
    g.x32 = 0;
    g.flags = u8(h.flags & 1); // the pit lane; the wet variant is in the surface already
}

// 0x344458: a record whose normal is too steep (y below 0.5) reads as level (its plane kept).
void level_steep(ex::Ground& g) {
    if (g.normal.y < F::raw(0x3F000000)) g.normal = {F(), F::raw(0x3F800000), F()};
}

} // namespace

Session::~Session() = default;

std::unique_ptr<Session> Session::open(Disc& disc, const std::string& course, std::span<const std::string> cars) {
    gt3r::Volume* vol = &disc.volume();
    db::SpecDb db(
        [vol](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
    const gt3r::Volume::Entry* file = vol->find("crs/" + course);
    if (!file) throw std::runtime_error("session: no course " + course);
    const Bytes data = vol->read(*file);
    if (!course::CourseFile::looks_like_course(data)) throw std::runtime_error("session: " + course + " is not a course");
    const course::CourseFile parsed = course::CourseFile::parse(data);
    std::optional<course::Runway> road = course::course_road(parsed, data); // (RNW4, or Prologue's GTRW)
    if (!road) throw std::runtime_error("session: the course " + course + " has no road");
    const Program program = Program::load(disc);
    std::vector<Entry> entries(cars.size());
    for (size_t i = 0; i < cars.size(); ++i)
        if (!stock_car(db, program, cars[i], entries[i].equipment)) throw std::runtime_error("session: no car " + cars[i]);
    return std::make_unique<Session>(db, program, std::make_shared<const course::Runway>(std::move(*road)), entries);
}

Session::Session(db::SpecDb& db, const Program& program, std::shared_ptr<const course::Runway> road, std::span<const Entry> entries) : road_(std::move(road)) {
    if (!road_) throw std::runtime_error("session: no road");
    if (entries.empty()) throw std::runtime_error("session: no cars");
    ground_ = std::make_unique<RoadGround>(*road_);
    if (!program.known()) notes_.push_back(program.issue() + ": the shared engine's tables serve");
    data_ = std::make_unique<ex::ProgramData>(core_program(program));

    // The start places that stand on the road, each at the road's height.
    std::vector<course::Runway::Place> places;
    GroundHit start;
    for (course::Runway::Place at : road_->grid) {
        GroundHit h;
        if (!ground_->at_place(at.pos, h)) continue;
        if (places.empty()) start = h;
        at.pos.y = h.point.y;
        places.push_back(at);
    }
    if (places.empty()) throw std::runtime_error("session: no start place is on the road");
    loose_ = loose_surface(surface_index(start.material, start.flags));
    const u8 category = loose_ ? 2 : 0;
    const ex::RaceConfig config{};
    race_ = ex::race_inputs(*data_, ex::Conductor::SinglePlayer, config, false, category);
    rng_.init(gt3r::Vu0Random::kRunSeed);

    cars_.resize(entries.size());
    extra_.resize(entries.size());
    gx_.resize(entries.size());
    layouts_.resize(entries.size());
    speed_tables_.resize(entries.size());
    gt4_ready_ = program.known();
    // (GT4R_SIM_RULES=engine: the engine's rules throughout, for comparison)
    const char* rules_env = std::getenv("GT4R_SIM_RULES");
    gt4_rules_ = gt4_ready_ && !(rules_env && std::string(rules_env) == "engine");
    if (gt4_ready_) tables_ = rules::tables(program, race_.statics);
    // (the active steering aims along the driving line, 0x4649F0 on the computer drivers' line object: it runs for a car only
    // while its caller gives the line's heading, set_line_heading)
    active_on_ = tables_.pad.active_on;
    line_heading_.assign(entries.size(), std::nullopt);
    if (!gt4_ready_ && !(rules_env && std::string(rules_env) == "engine")) { // Prologue's own step (rules_prologue.h)
        prologue::Tables t = prologue::tables(program);
        if (t.found) prologue_ = t, prologue_cars_.resize(entries.size()), prologue_feel_.resize(entries.size());
        if (prologue_ && road_->gtrw) course_ground_ = std::make_unique<ex::CourseGround>(*road_->gtrw);
        if (prologue_) prologue::race_inputs(race_, *prologue_), prologue_aids_ = prologue::aid_tables(program);
    }
    // What the aids read of the race: the course's kind (a loose course 1), no speed caps, the lap.
    aid_race_.course_kind = loose_ ? 1 : 0;
    aid_race_.lap_length = F(road_->length);
    if (gt4_ready_) tables_.bumps = rules::bump_tables(program, aid_race_.course_kind);
    driver_aids_.assign(entries.size(), std::nullopt);
    mail_.assign(entries.size(), {});
    if (gt4_ready_) mood_tables_ = rules::mood_tables(program);
    start_tables_ = rules::start_tables(program);
    start_counters_.assign(entries.size(), 0);
    // A rolling start's pace (0x3F7880 while a car is held in it): the race's speed (the rule block's +0x6E, km/h).
    for (const Entry& e : entries)
        if (e.start_speed > 0 && aid_race_.start_speed == 0) aid_race_.start_speed = u8(std::lround(std::min(e.start_speed * 3.6f, 255.0f)));
    driver_grip_.assign(entries.size(), std::nullopt);
    specs_.resize(entries.size());
    states_.resize(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        build_spec(db, program, e.equipment, loose_ ? 2 : 0, specs_[i]);
        if (specs_[i].weight_kg() <= 0 || specs_[i].b(0xBF) == 0) throw std::runtime_error("session: a car without a chassis or an engine");
        ex::Spec spec;
        core_spec(specs_[i], spec);
        if (!e.asm_on) spec[0x109] = 0;
        if (!e.tcs_on) spec[0x110] = 0;
        ex::Car& c = cars_[i];
        c = ex::Car{};
        // (the program's C library and constants: 2002 Tokyo-Geneva's, rules::behaviour(); the engine's own build otherwise)
        c.behaviour = gt4_ready_ || prologue_ ? rules::behaviour() : data_->behaviour();
        c.index = u8(i);
        c.control = 0, c.driver_kind = 0, c.ghost = 0, c.wheel_type = 0;
        if (gt4_ready_) { // the parameter block from the spec, as the car's set-up makes it (Gran Turismo 4's converter)
            Record r;
            r.car = c;
            build_record(specs_[i], program, tables_, ConvertIn{}, r);
            // The entry's aid switches: ASM's channels and the TCS of the car's own block off.
            if (!e.asm_on) r.x.aids.own.on = {}, r.car.p.aids.on = {};
            if (!e.tcs_on) r.x.aids.own.tcs_gain = F(), r.car.p.aids.tcs_gain = F();
            c.p = r.car.p;
            // The fuel and the nitrous at the start (0x354AF8; the race's fuel rule, the rule block's +0x4C: Entry::fuel).
            rules::fuel_start(c, r.x.drive.fuel, e.fuel ? 1 : 0, program.b(0x620E24), r.x.chassis.mass, r.x.body.sprung_mass);
            r.x.aids.fuel = r.x.drive.fuel.fuel, r.x.aids.fuel_capacity = r.x.drive.fuel.capacity;
            rules::step_params(specs_[i], r); // (the step's own routines' words of the parameter block)
            gx_[i] = r.x;
            layouts_[i] = r.layout;
            speed_tables_[i] = rules::speed_tables(program, r.x.steer.lean_row);
        } else if (prologue_) { // Prologue's own spec and converter (rules_prologue.h build_car)
            prologue::Block block{};
            prologue_cars_[i] = prologue::build_car(db, specs_[i], e.equipment, loose_ ? 2 : 0, e.asm_on, e.tcs_on, *prologue_,
                                                    0x01000000u + 0x2000u * u32(i) + ex::kParams, c, &block);
            speed_tables_[i] = prologue::speed_tables(program, block[0x33]); // (feel_prologue.h)
        } else { // (a program that is not a known build: the engine's converter on the core's tables)
            ex::ConvertInputs in;
            in.data = data_.get();
            const u32 address = 0x01000000u + 0x2000u * u32(i);
            in.base = address + ex::kParams;
            in.control = 0;
            in.physics_globals();
            ex::ParamImage p{};
            ex::convert_params(spec, in, p);
            ex::Record image{};
            std::copy(p.begin(), p.end(), image.begin() + ex::kParams);
            ex::Car loaded;
            ex::load_image(loaded, image.data());
            c.p = loaded.p;
            gt4_params(specs_[i], program, c.p);
        }
        c.pedal_style = e.automatic ? 0 : 1;
        // The start place: the entry's, or one further back along the first place's line when the grid is shorter;
        // a rolling start's on the road's middle at its distance along the course (❓ the program's on the course's
        // driving line there, 0x34C5E0 through the line object 0x46ADE8: the session has no line). The start's kind
        // (the conductor's virtual +0xF0): 0 the grid, 1 a rolling start at or past the line, 2 before it.
        const int want = e.grid_place >= 0 ? e.grid_place : int(i);
        course::Runway::Place at = places[size_t(want) % places.size()];
        int kind = 0;
        if (!std::isnan(e.start_distance) && road_->length > 0) {
            kind = e.start_distance < 0 ? 2 : 1;
            float v = std::fmod(e.start_distance, road_->length);
            if (v < 0) v += road_->length;
            Vec3 point, dir, middle;
            ground_->middle_at(v, point, dir);
            if (ground_->road_middle(v, start.material, point, middle)) point = middle;
            at.pos = point, at.heading = std::atan2(dir.x, dir.z);
            GroundHit h;
            if (ground_->at_place(at.pos, h)) at.pos.y = h.point.y;
        } else if (size_t(want) >= places.size()) {
            at = places[0];
            const float back = 8.0f * float(want);
            at.pos.x -= std::sin(at.heading) * back, at.pos.z -= std::cos(at.heading) * back;
            GroundHit h;
            if (ground_->at_place(at.pos, h)) at.pos.y = h.point.y;
        }
        if (gt4_rules_) setup(i, at, kind, e.start_speed, e.fuel);
        else place(i, at);
        publish(i);
    }
    // The collision bodies (0x45B978: an octagon from the body's extents, the mass and the yaw inertia).
    walls_ = std::make_unique<collide::Walls>(*road_, *ground_);
    bodies_.resize(cars_.size());
    body_cars_.resize(cars_.size());
    for (size_t i = 0; i < cars_.size(); ++i) {
        const ex::Params& p = cars_[i].p;
        collide::build(bodies_[i], int(i), p.body_front, p.body_rear, p.body_width, p.mass, p.yaw_inertia);
    }
    collide::pair_tables(bodies_);
    if (prologue_ && road_->gtrw) prologue_collide_ = std::make_unique<prologue::Collision>(*road_->gtrw, cars_); // (Prologue's: Gran Turismo 3's step)
    // A standing start: the last two seconds of the race's countdown before the session's time 0, the cars held
    // (F + 0x4BC) while the engines idle and the bodies settle on their springs (the original's countdown runs
    // longer, through the opening shot and the count: a caller adds its own with hold_once or set_countdown). A
    // rolling start goes at once, the cars moving from the set-up (0x363868).
    bool moving = false;
    for (const Entry& e : entries) moving = moving || e.start_speed > 0;
    if (!moving)
        for (int i = 0; i < kHoldSteps; ++i) hold_once({});
    if (!gt4_rules_) // (the engine's rules: its moving start after the hold)
        for (size_t i = 0; i < cars_.size(); ++i) {
            if (!(entries[i].start_speed > 0)) continue;
            rules::moving_start(cars_[i], gx_[i].drive, F(entries[i].start_speed), true);
            publish(i);
        }
}

rules::PoseHooks Session::pose_hooks() {
    rules::PoseHooks h;
    h.query = [this](ex::Ground& g) -> u8 { // 0x395760: from 5 m above the point
        GroundHit hit;
        const F at[3] = {g.x, g.h + F::raw(0x40A00000), g.z};
        const bool found = ground_->query(at, hit);
        fill(g, found, hit, true);
        return found ? u8(surface_index(hit.material, hit.flags)) : 0;
    };
    h.soft = [this](const ex::Ground& g) { return tables_.bumps.soft[size_t(g.material & 31)] != 0; };
    h.distance = [this](const ex::Car& c) {
        if (!prologue_) { // (the conductor's virtual +0x108, 0x3955E8, as at each step's end)
            const F at[3] = {c.pos.x, c.pos.z, -c.pos.y};
            return ground_->course_distance(at, c.road_seg);
        }
        float v = 0;
        return ground_->distance_along(to_course(c.pos), v) ? F(v) : F();
    };
    return h;
}

// Gran Turismo 4's set-up of a car on its start place (0x363B10 from the converter on, rules::car_setup).
void Session::setup(size_t i, const course::Runway::Place& at, int kind, float speed, bool fuel) {
    ex::Car& c = cars_[i];
    rules::SetupArgs a;
    a.index = int(i);
    a.control = 0;
    a.pedal_style = c.pedal_style;
    a.start_kind = kind;
    a.fuel_rule = fuel ? 1 : 0; // (the rule block's +0x4C)
    a.speed = F(speed * 3.6f);
    a.hint = -1;
    // The place in the record's frame (x, y, z up); the car faces the place's heading turned half a turn.
    a.x = F(at.pos.x), a.y = F(-at.pos.z), a.z = F(at.pos.y);
    a.heading = F(at.heading) + kPi;
    rules::SetupHooks h;
    h.pose = pose_hooks();
    rules::car_setup(c, gx_[i], layouts_[i], start_tables_, tables_.wear, a, h);
    // (the session's own ground records: the kinds and materials publish reads)
    for (size_t w = 0; w < 4; ++w) extra_[i].kinds[w] = c.g[w].miss ? 0 : c.g[w].flags, extra_[i].materials[w] = c.g[w].miss ? 0 : c.g[w].material;
    c.x16d1 = 1;
    c.dist_total = k0;
    rules::save_previous(c, layouts_[i]);
    ex::wheel_spin(c);
}

void Session::release_start(int car) {
    if (car < 0 || size_t(car) >= cars_.size()) return;
    // (the session has no line object: the driver's line change is taken as made)
    rules::release(cars_[size_t(car)], gx_[size_t(car)], true);
    publish(size_t(car));
}

void Session::place_standing(size_t car, F x, F y, F z, F heading) {
    if (car >= cars_.size() || !gt4_rules_) return;
    ex::Car& c = cars_[car];
    sim::Extra& g = gx_[car];
    rules::pose(c, g, layouts_[car], start_tables_, pose_hooks(), c.road_seg, x, y, z, heading);
    rules::clear_view_lags(c);
    rules::standing_start(c, g, layouts_[car]);
    for (size_t w = 0; w < 4; ++w) extra_[car].kinds[w] = c.g[w].miss ? 0 : c.g[w].flags, extra_[car].materials[w] = c.g[w].miss ? 0 : c.g[w].material;
    publish(car);
}

void Session::set_pit_hold(size_t car, u16 steps) {
    if (car >= cars_.size()) return;
    cars_[car].hold = steps;
    if (pit_by_caller_.size() < cars_.size()) pit_by_caller_.resize(cars_.size(), 0);
    pit_by_caller_[car] = 1;
}

void Session::set_wear_rules(const u8* rule_block, bool second_on, const Program& program) {
    const auto b = rules::wear_rules_block(rule_block, second_on, program);
    tables_.wear = rules::wear_rules(b.data());
    if (steps_ == 0 && gt4_ready_) // (before the start: the cars' set-up fits new tires by the rules, 0x362E58)
        for (size_t i = 0; i < cars_.size(); ++i)
            for (int w = 0; w < 4; ++w) rules::new_tire(cars_[i], gx_[i].wear, w, tables_.wear);
}

void Session::set_fuel(size_t i, gt3r::ee::F liters) {
    if (i >= cars_.size()) return;
    gx_[i].drive.fuel.fuel = liters, gx_[i].aids.fuel = liters;
}

int Session::gear_for_speed(size_t i, gt3r::ee::F speed) const {
    if (i >= cars_.size() || !gt4_ready_) return 0;
    return rules::gear_for_speed(cars_[i], gx_[i].drive, speed);
}

void Session::new_tire(size_t i, int wheel) {
    if (i < cars_.size() && wheel >= 0 && wheel < 4) rules::new_tire(cars_[i], gx_[i].wear, wheel, tables_.wear);
}

void Session::set_line_heading(size_t car, F heading) {
    if (car < line_heading_.size()) line_heading_[car] = heading;
}

void Session::set_driver_aids(size_t car, const gt3r::sim::exact::Aids& block) {
    if (car < driver_aids_.size()) driver_aids_[car] = block;
}

void Session::set_driver_aids(size_t car, const gt3r::sim::exact::Aids& block, const std::array<F, 3>& grip) {
    set_driver_aids(car, block);
    if (car < driver_grip_.size()) driver_grip_[car] = grip;
}

void Session::hold_once(std::span<const race::CarInput> inputs) {
    holding_ = true;
    step_once(inputs);
    holding_ = false;
}

// The car at rest on a start place with the engine's rules (GT4R_SIM_RULES=engine: the engine's race start, the pose
// on the road and the standing start), with the nose at the place.
void Session::place(size_t i, const course::Runway::Place& at) {
    ex::Car& c = cars_[i];
    // The record's heading: the place's turned half a turn (forward is (sin, cos) of the place's in x, z).
    const F heading = F(at.heading) + kPi;
    F x = F(at.pos.x), y = F(-at.pos.z);
    const F front = c.p.body_front;
    x = x + front * gt3r::ee::sin(heading);
    y = y - front * gt3r::ee::cos(heading);
    GroundHit h;
    const bool under = ground_->under({x.host(), at.pos.y, -y.host()}, h);
    const F ground = under ? F(h.point.y) : F(at.pos.y);
    c.pos.x = x, c.pos.y = y;
    c.pitch = k0, c.roll = k0, c.heading = heading;
    c.road_seg = -1;
    c.pos.z = ground + c.p.cog_height;
    ex::body_axes(c);
    if (gt4_ready_) rules::hub_offsets(c, layouts_[i]);
    else ex::hub_offsets(c);
    for (size_t w = 0; w < 4; ++w) {
        ex::Wheel& wheel = c.w[w];
        ex::Ground& g = c.g[w];
        g.x = c.pos.x + wheel.hub.x, g.z = -(c.pos.y + wheel.hub.y), g.h = c.pos.z + wheel.hub.z;
        GroundHit wh;
        const bool hit = ground_->under({g.x.host(), g.h.host(), g.z.host()}, wh);
        g.height = kNoGround;
        fill(g, hit, wh, gt4_rules_);
        level_steep(g);
        extra_[i].kinds[w] = hit ? wh.flags : 0;
        extra_[i].materials[w] = hit ? wh.material : 0;
        if (hit) {
            wheel.ground = wheel.ground0 = g.height;
            wheel.surface = gt4_rules_ ? u8(surface_index(wh.material, wh.flags)) : g.material, wheel.x25 = 0, wheel.surface_flags = g.flags;
        } else {
            wheel.surface = 0, wheel.x25 = 0, wheel.surface_flags = 0;
            wheel.ground = wheel.ground0 = ground + wheel.hub.z;
        }
    }
    for (int w = 0; w < 4; ++w) {
        ex::Wheel& wheel = c.w[size_t(w)];
        const size_t a = size_t(w >> 1);
        wheel.smoke_count = 0;
        wheel.load = wheel.peak_force = (a ? c.p.xe58 : c.p.xe54) * kHalf + c.p.susp[a].preload;
        wheel.susp = c.p.susp[a].x_static;
        wheel.steer = ex::wheel_steer_angle(c.p, w, k0);
        wheel.camber = ex::wheel_camber(c.p, w, k0, k0);
        wheel.half_track = ex::hub_half_track(c.behaviour, c.p, w, k0);
    }
    c.road_seg = c.g[0].miss ? -1 : c.g[0].seg; // (0x36315C: the set-up's hint, wheel 0's triangle)
    if (gt4_rules_) rules::ground_frame(c, layouts_[i]);
    else ex::ground_frame(c);
    c.pitch = c.ground_pitch, c.roll = c.ground_roll;
    c.pitch = c.pitch + ex::atan2(c.behaviour, c.p.ride_height[0] - c.p.ride_height[1], c.p.wheelbase);
    ex::body_axes(c);
    c.pos.z = c.ground_pos.z + c.up.z * c.p.cog_height;
    if (gt4_ready_) rules::hub_offsets(c, layouts_[i]);
    else ex::hub_offsets(c);
    c.lag_heading = c.heading, c.lag_pitch = c.pitch;
    rules::view_start(c, gx_[i].feel.view); // (0x3455F0)
    ex::standing_start(c);
    if (gt4_ready_) // (0x362E58 for each wheel: new tires by the race's wear rules)
        for (int w = 0; w < 4; ++w) rules::new_tire(c, gx_[i].wear, w, tables_.wear);
    else ex::tyres_start(c, race_.prep.wear);
    ex::step_length(c);
    c.best_lap = 0x157529FF;
    c.rank = gt3r::s8(i + 1);
    c.x16d1 = 1;
    c.dist_total = k0;
    ex::save_previous(c);
    if (gt4_rules_) { // (0x344040)
        const rules::SpeedTables& k = speed_tables_[i];
        rules::wheel_spin(c, gx_[i].feel.anim, layouts_[i], k.special_x10, gx_[i].wear.category, gx_[i].body.in_line_x65c, k.throttle_share, k.brake_share);
    } else
        ex::wheel_spin(c);
    if (gt4_rules_) rules::sway(c, gx_[i].feel.view, tables_.feel, gx_[i].feel.blend); // (0x34A668, 0x34A788)
}

// The ground contact (phases 16 .. 18: the query points 0x3697A8, the road under each wheel 0x344458, then 0x369888).
void Session::ground_contact(ex::Car& c, Extra& x) {
    const size_t car = size_t(&c - cars_.data());
    if (prologue_ && course_ground_) { // (Prologue's 0x3754D8 on its GTRW road, as the program queries it)
        prologue::ground_contact(c, prologue_cars_[car], *course_ground_, prologue_->bumps, true);
        for (size_t i = 0; i < 4; ++i) x.kinds[i] = c.g[i].flags, x.materials[i] = c.g[i].material;
        return;
    }
    if (gt4_rules_) rules::query_points(c, gx_[car].body, layouts_[car]);
    else if (prologue_) prologue::query_points(c, prologue_cars_[car]); // (Prologue's 0x3754D8)
    for (size_t i = 0; i < 4; ++i) {
        ex::Wheel& w = c.w[i];
        ex::Ground& g = c.g[i];
        if (!gt4_rules_ && !prologue_) g.x = c.pos.x + w.hub.x, g.h = c.pos.z + w.hub.z, g.z = -(c.pos.y + w.hub.y);
        GroundHit h;
        const F at[3] = {g.x, g.h + F::raw(0x40A00000), g.z}; // (0x395760: from 5 m above)
        const bool hit = ground_->query(at, h);
        fill(g, hit, h, gt4_rules_);
        level_steep(g);
        if (!hit) {
            w.bump = k0;
            continue;
        }
        x.kinds[i] = h.flags;
        x.materials[i] = h.material;
        w.ground0 = g.height, w.ground = g.height;
        w.surface = gt4_rules_ ? u8(surface_index(h.material, h.flags)) : g.material, w.x25 = 0, w.surface_flags = g.flags;
        if (gt4_rules_ || prologue_) continue; // (the bumps below: rules::road_bumps, prologue::road_bumps)
        const size_t s = w.surface & 15;
        const F amp = race_.bumps.amp[s];
        if (amp == k0) {
            w.bump = k0;
            continue;
        }
        F b = ex::bump_wave(race_.bumps.mode[s], g.x, g.z, race_.bumps.freq[s]);
        if (race_.bumps.scaled[s]) b = b * w.speed_ramp;
        w.bump = b * amp;
    }
    if (prologue_) prologue::road_bumps(c, prologue_cars_[car], prologue_->bumps); // (Prologue's 0x3754D8: its own tables)
    // Gran Turismo 4's bumps (0x369888) on its 24 surfaces, from the materials and flags the query found.
    if (gt4_rules_) {
        std::array<u8, 4> surface{}, material{};
        for (size_t i = 0; i < 4; ++i) material[i] = x.materials[i], surface[i] = u8(surface_index(x.materials[i], x.kinds[i]));
        rules::road_bumps(c, layouts_[car], tables_.bumps, surface, material);
        rules::course_out(c, gx_[car], layouts_[car], tables_.bumps.soft); // (0x369888's end: soft ground, the pit lane, leaving the course)
    }
    c.x535 = u8(c.x535 & 0xEF);
    c.road_seg = c.g[0].seg;
    for (const ex::Ground& g : c.g)
        if (!g.miss && (g.flags & 1)) c.x535 = u8(c.x535 | 0x10);
}

// Phase 12 in the engine's order, the ground contact ours.
void Session::chassis(size_t i) {
    ex::Car& c = cars_[i];
    Extra& x = extra_[i];
    if (gt4_rules_) rules::hub_offsets_xy(c, layouts_[i]); // (0x3458D0: by the wheel layout)
    else ex::hub_offsets_xy(c);
    ex::speed_ramp(c);
    ground_contact(c, x);
    if (gt4_rules_) { // Gran Turismo 4's suspension and attitude (0x36A490 .. 0x36BA88)
        sim::Extra& g = gx_[i];
        const rules::WheelLayout& l = layouts_[i];
        rules::terrain(c, g.body, l);
        rules::suspension(c, g.body, g.chassis, l, tables_.body);
        // The attitude (0x36B0C8), first its start hold while the hold counter runs (0x36AE20). (❓ The hold turns the car
        // toward its driving line's heading; the session has no line: its own heading, no turn.)
        if (!rules::attitude_hold(c, g.body, c.heading)) rules::attitude(c, g.body, g.chassis, g.tires, l, tables_.body);
        rules::bump_nudge(c, g.body, l);
    } else {
        if (prologue_) prologue::terrain(c, prologue_cars_[i]); // (Prologue's 0x375D00)
        else ex::terrain_factor(c);
        ex::suspension(c, nullptr);
        if (prologue_) { // (Prologue's 0x3765F8 and 0x376EC8)
            prologue::attitude(c, prologue_cars_[i].layout, *prologue_);
            prologue::bump_nudge(c, prologue_cars_[i]);
        } else {
            ex::attitude(c, k0);
            ex::bump_nudge(c);
        }
    }
    if (gt4_rules_) { // (0x36CEE8: into the conductor's pad block; the record's byte kept with it)
        rules::pad_rumble(c, layouts_[i], gx_[i].feel.pad);
        c.rumble = gx_[i].feel.pad[1];
    } else
        ex::pad_rumble(c);
    ex::steer_force(c, race_.feel);
    if (gt4_rules_) rules::slide_effects(c, layouts_[i], tables_.slide, loose_ ? 2 : 0); // (0x367148)
    else ex::tyre_effects(c, race_.effects, race_.aids.course_category);
    ex::phase12_stub(c);
    if (gt4_rules_) rules::engine_sound(c, tables_.feel, gx_[i].drive.d.kind, rules::draw_mode(c, 0, 0, 0)); // (0x350080)
    else ex::engine_sound(c, race_.sound_scales);
    if (gt4_rules_) rules::effects_reset(c, layouts_[i], rules::draw_mode(c, 0, 0, 0)); // (0x366718; no ghosts here)
    else ex::effects_reset(c, 0, 0);
    if (gt4_rules_) rules::view_follow(c, gx_[i].feel.view, tables_.feel); // (0x345658)
    else ex::view_follow(c);
    if (gt4_rules_) rules::hub_offsets(c, layouts_[i]); // (0x345A20)
    else ex::hub_offsets(c);
    if (gt4_rules_) { // (0x36C9F8: the pitch before the roll; 0x35D350)
        rules::body_axes(c);
        rules::ground_frame(c, layouts_[i]);
    } else if (prologue_) { // (Prologue's 0x377310 and 0x3C4D80)
        rules::body_axes(c);
        prologue::ground_frame(c);
    } else {
        ex::body_axes(c);
        ex::ground_frame(c);
    }
    if (gt4_rules_) rules::speeds(c, gx_[i].speeds, layouts_[i], speed_tables_[i], gx_[i].body.in_line_x480); // (0x35DE20)
    else if (prologue_) prologue::speeds(c, prologue_feel_[i], prologue_cars_[i].layout, speed_tables_[i], race_.prep.condition); // (0x3C5740)
    else ex::speed_bookkeeping(c, race_.prep.condition);
    ex::wheel_spin(c);
}

// The collision step (phase 13: 0x3F4B20, 0x45BB60, 0x3F4C18): every car's body moved through the step against the
// road's walls and the other cars.
void Session::collide() {
    if (prologue_collide_) { // Prologue's (0x3B6FB8 with 0x41CA90)
        prologue_collide_->step(cars_, prologue_contacts_);
        for (size_t i = 0; i < cars_.size() && i < prologue_contacts_.size(); ++i) {
            const prologue::Collision::Contact& k = prologue_contacts_[i];
            extra_[i].contact = k.strength, extra_[i].contact_kind = k.kind, extra_[i].contact_car = k.car;
        }
        return;
    }
    for (size_t i = 0; i < cars_.size(); ++i) { // 0x3F4B20
        collide::Body& b = bodies_[i];
        const ex::Car& c = cars_[i];
        b.cx = c.pos.x, b.cy = c.pos.y, b.cz = c.pos.z;
        b.cvx = c.vel.x, b.cvy = c.vel.y, b.cvz = c.vel.z;
        b.pitch = c.pitch, b.roll = c.roll, b.heading = c.heading;
        b.yaw_rate = c.yaw_rate;
        b.hint = c.road_seg;
        collide::CarIn& in = body_cars_[i];
        in.dt = c.dt;
        in.ghost = c.ghost != 0;
        in.player = c.control == 0 && c.driver_kind == 0;
        // F +0x45C, the steering's value as the input phase (0x349FD0) leaves it; with the engine's rules 0x348800 of the command
        if (gt4_rules_) in.steer = gx_[i].input.steer_view;
        else {
            const F v = c.steer_cmd * F::raw(0x40747648), one = F::raw(0x3F800000), minus = F::raw(0xBF800000);
            in.steer = one < v ? one : v < minus ? minus : v;
        }
        in.rumble = &cars_[i].hit_rumble;
    }
    hits_.clear();
    collide::step_all(bodies_, body_cars_, *walls_, gt4_rules_ ? &hits_ : nullptr);
    // The hit records (0x350530, in the step's order): the moods, the incident count, the conductor's judge, the pad's record.
    for (const collide::Hit& h : hits_)
        rules::hit(cars_[size_t(h.car)], gx_[size_t(h.car)], mood_tables_, hit_rules_, rules::Judge::Penalty, &pylons_hit_, h.kind, h.part, h.strength);
    for (size_t i = 0; i < cars_.size(); ++i) { // 0x3F4C18
        collide::Body& b = bodies_[i];
        ex::Car& c = cars_[i];
        c.pos = {b.cx, b.cy, b.cz};
        c.vel = {b.cvx, b.cvy, b.cvz};
        c.heading = b.heading, c.yaw_rate = b.yaw_rate;
        // The step's contact as the collision step leaves it on the body: the summed impulses' strength (at most 1),
        // whether a wall or a car was met, and the car: one that was met this step whose edge this body's vertex met
        // (the body's last pair), or whose vertex met this body's edge (that body's last pair), or on whose edge a
        // vertex of this body is held.
        Extra& x = extra_[i];
        x.contact = b.car_hit || b.wall_hit ? std::min(b.strength.host(), 1.0f) : 0.0f;
        x.contact_kind = b.car_hit ? 2 : b.wall_hit ? 1 : 0;
        x.contact_car = -1;
        if (b.car_hit) {
            auto met = [&](int k) { return k >= 0 && size_t(k) < bodies_.size() && size_t(k) != i && bodies_[size_t(k)].car_hit; };
            if (met(b.other) && size_t(bodies_[size_t(b.other)].other) != i) x.contact_car = b.other;
            for (size_t k = 0; x.contact_car < 0 && k < bodies_.size(); ++k)
                if (met(int(k)) && size_t(bodies_[k].other) == i) x.contact_car = int(k);
            for (const collide::Point& q : b.p)
                if (x.contact_car < 0 && (q.flags & 2) && met(q.other)) x.contact_car = q.other;
            if (x.contact_car < 0 && met(b.other)) x.contact_car = b.other;
        }
    }
}

void Session::step_once(std::span<const race::CarInput> inputs) {
    const u8 course_kind = ex::course_category(race_.aids.course_category);
    // The race's countdown (the race information's +0xBC; 0x34C210): the cars held while it runs.
    const bool countdown = holding_ || countdown_ > 0;
    race_.prep.start_count = countdown ? 1 : 0; // (the engine's rules' hold)
    for (size_t i = 0; i < cars_.size(); ++i) {
        ex::Car& c = cars_[i];
        Extra& x = extra_[i];
        const race::CarInput in = i < inputs.size() ? inputs[i] : race::CarInput{};
        // The driver's frame: a recorded one, or an analog pad's from the input's fields.
        rules::PadFrame f;
        if (in.pad) f = rules::PadFrame::from_bytes(in.pad->bytes.data());
        else {
            f.flags = 1 | 2 | 4, f.kind = 3;
            f.steer = gt3r::s16(std::lround(std::clamp(-in.steer, -1.0f, 1.0f) * 4096.0f));
            const u16 throttle = u16(std::lround(std::clamp(in.throttle, 0.0f, 1.0f) * 4096.0f));
            if (in.reverse) f.flags |= 8, f.back = throttle;
            else f.throttle = throttle;
            f.brake = u16(std::lround(std::clamp(in.brake, 0.0f, 1.0f) * 4096.0f));
            f.third = in.handbrake ? 1 : 0;
            const u8 buttons = u8((in.shift_up ? 1 : 0) | (in.shift_down ? 2 : 0));
            const u8 pressed = u8(buttons & ~x.prev_buttons);
            x.prev_buttons = buttons;
            f.shift = gt3r::s8(pressed & 1 ? 1 : pressed & 2 ? -1 : 0);
        }
        // (a computer's car: its driver's grip, 0x467470)
        const std::array<F, 3>* grip = in.computer && driver_grip_[i] ? &*driver_grip_[i] : nullptr;
        // A computer driver's outputs into the controls as the program's drivers write them (0x464F38).
        auto computer_outputs = [&] {
            c.full_throttle = 1;
            c.throttle_pedal = c.throttle = gt3r::ee::F(std::clamp(in.throttle, 0.0f, 1.0f));
            ex::set_brake_pedal(c, gt3r::ee::F(std::clamp(in.brake, 0.0f, 1.0f)));
            c.reverse_req = gt3r::s8(in.reverse ? 1 : 0);
            if (in.shift_lock) c.shift_lockout = in.shift_lock;
            c.shift_req = 0;
        };
        // The clutch's request (0x3F3400, for an ordinary gearbox): released for a stopped driver (kind 8); racing, with the
        // clutch open it is requested once the rpm passes three quarters of the up-shift speed, with it closed it stays
        // requested down to idle. (Under the start's hold and the hold counter the program leaves it as it was: ours 0.)
        auto computer_clutch = [&] {
            const F one = gt3r::ee::F::raw(0x3F800000);
            if (c.race_hold != 0 || c.hold != 0 || c.driver_kind == 8) c.clutch_req = F();
            else if (!gt4_rules_) c.clutch_req = one;
            else if (c.clutch == 0) c.clutch_req = s32(c.rpm) > ((s32(gx_[i].drive.d.up_rpm) * 3) >> 2) ? one : F();
            else c.clutch_req = s32(c.rpm) < s32(c.p.e.idle) ? F() : one;
            // (a hybrid one-speed box's: 0x348368)
            if (gt4_rules_ && gx_[i].drive.d.kind == 3) rules::hybrid_clutch_request(c);
        };

        if (gt4_rules_) {
            sim::Extra& g = gx_[i];
            const rules::WheelLayout& l = layouts_[i];
            rules::save_previous(c, l);                                  // phase 1 (0x343ED0)
            rules::layout7_step(c, g);                                   // phase 2 (0x355E10)
            rules::moods(c, g, mood_tables_, l, tables_.bumps.soft, 0);  // phase 3 (0x35CA60; ❓ no pass-by states: no rival)
            rules::start_hold(c, countdown, start_counters_[i]);          // (0x34C210 and the car's own counter)
            { // phase 4 (0x357700; the pit stop's counter 0x35C0B8 not when the caller keeps it: set_pit_hold)
                const u16 hold = c.hold;
                if (i < pit_by_caller_.size() && pit_by_caller_[i]) c.hold = 0;
                rules::prep(c, g, l, tables_, race_.prep.condition, grip);
                c.hold = hold;
            }
            // (the masses with the fuel, 0x354AC0, as the fuel's state has them, and the fuel the aids read)
            rules::masses(c, g.drive.fuel, g.chassis.mass, g.body.sprung_mass);
            g.aids.fuel = g.drive.fuel.fuel, g.aids.fuel_capacity = g.drive.fuel.capacity;
            // Phase 5 (0x349FD0): the driver's input, by the control source (a computer's car 2, the B-spec car 1).
            c.control = in.computer ? gt3r::s8(in.control) : 0;
            rules::post_mail(f, mail_[i], c.control, !in.computer);
            rules::InputRace race;
            race.course_kind = loose_ ? 2 : 0; // ❓ wet and snow courses not told apart
            rules::InputHooks hk;
            hk.computer_clutch = computer_clutch;
            hk.computer_outputs = [&] {
                computer_outputs();
                if (c.driver_kind == 8) c.handbrake = F::raw(0x3F800000);
            };
            hk.ground = [&](const F p[3]) {
                rules::GroundAnswer a;
                GroundHit h;
                const F at[3] = {p[0], p[1] + F::raw(0x40A00000), p[2]};
                if (ground_->query(at, h)) a.found = true, a.height = F(h.point.y), a.triangle = u32(u16(gt3r::s16(h.cluster))) << 16 | u32(u16(h.triangle));
                return a;
            };
            race.line_heading = line_heading_[i] ? *line_heading_[i] : F();
            tables_.pad.active_on = line_heading_[i] ? active_on_ : 0;
            rules::driver_input(c, g, l, tables_.pad, tables_.steer, race, hk, f);
            rules::stop_count(c, g); // phase 6 (0x35D1D0)
        } else {
            ex::save_previous(c);
            ex::prep(c, race_.prep);
            if (in.computer) {
                computer_outputs();
                computer_clutch();
            } else {
                ex::PadFrame e;
                e.flags = f.flags, e.kind = f.kind == 6 ? 4 : f.kind, e.shift = f.shift, e.steer = f.steer, e.throttle = f.throttle;
                e.brake = f.brake, e.back = f.back, e.third = f.third;
                if (prologue_) prologue::driver_input_pad(c, race_.pad, e, course_kind, prologue_cars_[i].layout, *prologue_);
                else ex::driver_input_pad(c, race_.pad, e, course_kind);
            }
        }
        // A computer driver's own command: the road wheels' angle as the game's drivers set it (0x464F38 writes it into the
        // steering command directly), within the full lock, past the pad's lock curve (no later phase reads the bounds).
        if (!std::isnan(in.steer_angle)) {
            const gt3r::ee::F lock = c.p.max_lock;
            c.steer_cmd = gt3r::ee::F(std::clamp(in.steer_angle, -lock.host(), lock.host()));
            // (F +0x45C as the computer's path of 0x349FD0 makes it of the command 0x464F38 wrote)
            if (gt4_rules_) gx_[i].input.steer_view = rules::steer_unit(c.steer_cmd) * F::raw(0x3F2AACD9);
        }
        if (gt4_rules_) {
            // Gran Turismo 4's steering (0x3F7EA8): the angles, the follower, the slip angle's offset and the compliance.
            sim::Extra& g = gx_[i];
            rules::wheel_steer(c, g.steer, g.tires, layouts_[i], tables_.steer);
            for (size_t w = 0; w < 4; ++w) // (for the engine's routines that still read them: its start hold)
                c.t[w].steer_cos = gt3r::ee::cos(c.w[w].steer), c.t[w].steer_sin = gt3r::ee::sin(c.w[w].steer);
        } else if (prologue_) prologue::wheel_steer(c, prologue_cars_[i], *prologue_);
        else ex::wheel_steer(c);
        if (gt4_rules_) rules::slip_phase(c, gx_[i], layouts_[i], tables_);
        else if (prologue_) prologue::slip_ratios(c, prologue_cars_[i].tires, prologue_cars_[i].layout, *prologue_);
        else ex::slip_ratios(c, race_.statics);
        if (gt4_rules_) {
            // Gran Turismo 4's aids (0x3F79A0): a computer's car (control source 2) by its driver's block.
            const ex::Aids* driver = in.computer && driver_aids_[i] ? &*driver_aids_[i] : nullptr;
            sim::Extra& g = gx_[i];
            rules::aids(c, g.aids, g.tires, g.drive, layouts_[i], tables_.aids, aid_race_, driver);
        } else if (prologue_) { // Prologue's (0x3E0838)
            if (in.computer && driver_aids_[i]) c.ai.aids = *driver_aids_[i];
            prologue::AidRace r;
            r.dirt = loose_, r.speed_cap = aid_race_.speed_cap, r.start_cap = aid_race_.start_cap;
            prologue::aids(c, prologue_cars_[i], prologue_aids_, r, race_.aids);
        } else ex::aids(c, race_.aids);
        if (gt4_rules_) rules::dynamics(c, gx_[i], layouts_[i], tables_, grip);
        else if (prologue_) prologue::dynamics(c, prologue_cars_[i], race_.statics, race_.gears, race_.class_grip, *prologue_);
        else ex::dynamics(c, race_.statics, race_.gears, race_.class_grip);
        if (gt4_rules_) rules::brake_heat(c, layouts_[i], tables_.brake_heat, gx_[i].drive.d.tire_radius);
        else ex::brake_heat(c, race_.brake_heat);
        ex::clear_feedback(c); // (the engine's feedback, for its routines that still read it)
        if (gt4_rules_) rules::clear_hit(gx_[i].phases); // phase 12 (0x350308)
    }
    // (0x34DB30 after phase 12, before the collision step: each car's recent hit's steps counted down)
    if (gt4_rules_)
        for (size_t i = 0; i < cars_.size(); ++i) rules::hit_steps(gx_[i].phases);
    collide();
    if (gt4_rules_) {
        // Phase 14 (0x3F4638): which cars are near which, the slipstream's targets; phase 15 (0x3F28B8): the part that
        // changes with speed.
        std::vector<rules::NearCar> near(cars_.size());
        for (size_t i = 0; i < cars_.size(); ++i) near[i] = {&cars_[i], &gx_[i].near};
        rules::proximity(near, pairs_, F(road_->length), 0);
        for (size_t i = 0; i < cars_.size(); ++i) rules::speed_part(cars_[i], gx_[i].near, gx_[i].body.speed_part_kind, gx_[i].chassis.speed_part_state);
    }
    for (size_t i = 0; i < cars_.size(); ++i) {
        ex::Car& c = cars_[i];
        chassis(i);
        if (gt4_rules_) {
            // Phases 20 .. 22: the tire wear (0x352EA0, by the race's wear rules), the pit flags (0x353218's: the computer
            // drivers' pit flow, sim/ai), the flaps (0x35A678).
            rules::tire_wear(c, gx_[i].wear, gx_[i].tires, layouts_[i], tables_.wear); // (0x352EA0)
            rules::flaps(c, layouts_[i], random_state_);
        } else {
            ex::tire_wear(c, race_.prep.wear);
            ex::wear_flags(c, race_.prep.wear, {});
            ex::flaps(c, rng_);
        }
        publish(i);
    }
    // (❓ the race flow counts its countdown down after the step)
    if (countdown_ > 0) --countdown_;
    if (!countdown) ++steps_;
}

void Session::step(std::span<const race::CarInput> inputs, float dt) {
    pending_ += dt;
    int guard = 0;
    while (pending_ >= kStep * 0.999f && guard++ < 240) {
        step_once(inputs);
        pending_ -= kStep;
    }
    if (pending_ < 0) pending_ = 0;
}

void Session::publish(size_t i) {
    const ex::Car& c = cars_[i];
    Extra& x = extra_[i];
    race::CarState& s = states_[i];
    s.forward = to_course(c.fwd), s.up = to_course(c.up);
    // The car's right side: forward x up (the course's frame is right-handed: x, y up, z).
    s.right = {s.forward.y * s.up.z - s.forward.z * s.up.y, s.forward.z * s.up.x - s.forward.x * s.up.z, s.forward.x * s.up.y - s.forward.y * s.up.x};
    // The body's reference point: the middle of the wheelbase at the body's underside (the car
    // model's origin). The record keeps the center of mass: the middle is P +0x54 behind it, and it is at its
    // own height above the road; the underside is the ride height above it.
    const Vec3 cog = to_course(c.pos);
    const float back = c.p.x54.host(), lift = (c.p.ride_height[0].host() + c.p.ride_height[1].host()) * 0.5f - c.p.cog_height.host();
    s.position = {cog.x - s.forward.x * back + s.up.x * lift, cog.y - s.forward.y * back + s.up.y * lift, cog.z - s.forward.z * back + s.up.z * lift};
    s.velocity = to_course(c.vel);
    s.speed = c.speed.host();
    s.engine_rpm = c.omega.host() * 9.549297f;
    s.gear = c.gear == 0 ? -1 : int(c.gear);
    s.throttle = c.throttle.host(), s.brake = c.brake_pedal.host();
    const float lock = c.p.max_lock.host();
    s.steer = lock > 0 ? -c.steer_cmd.host() / lock : 0;
    s.brake_lamps = c.brake_pedal.host() > 0;
    s.reverse_lamps = c.gear == 0;
    s.contact = x.contact, s.contact_kind = x.contact_kind, s.contact_car = x.contact_car;
    s.rolling = gt4_rules_ && c.driver_kind == 1;
    // A wheel back on the ground: loaded at this step's end after a step without load.
    s.landed = false;
    for (size_t w = 0; w < 4; ++w) {
        const bool air = (c.w[w].flags & 1) != 0 || c.g[w].miss;
        if (x.airborne[w] && !air) s.landed = true;
        x.airborne[w] = air;
    }
    for (size_t w = 0; w < 4; ++w) {
        const ex::Wheel& wheel = c.w[w];
        race::WheelState& o = s.wheels[w];
        const size_t a = w >> 1;
        const float radius = c.p.tire_radius[a].host();
        o.on_ground = !(wheel.flags & 1) && !c.g[w].miss;
        // The wheel's center: over the suspension's point at the wheel, a tire radius above the road
        // while the tire is on it; off the ground it hangs where it was at rest under the body.
        const float px = c.pos.x.host() + wheel.hub.x.host(), py = c.pos.z.host() + wheel.hub.z.host(), pz = -(c.pos.y.host() + wheel.hub.y.host());
        if (o.on_ground) {
            o.center = {px, wheel.ground.host() + radius, pz};
            if (!x.rest_set[w]) x.rest_drop[w] = py - o.center.y, x.rest_set[w] = true;
        } else {
            o.center = {px, py - x.rest_drop[w], pz};
        }
        o.steer = -wheel.steer.host();
        o.spin_rate = radius > 0 ? wheel.rim_speed.host() / radius : 0;
        x.spin[w] = std::fmod(x.spin[w] + o.spin_rate * kStep, 6.2831855f);
        o.spin = x.spin[w];
        o.travel = c.p.susp[a].x_static.host() - wheel.susp.host();
        o.slip = std::clamp(wheel.slip_intensity.host(), 0.0f, 1.0f);
        o.brake_heat = wheel.brake_heat.host();
        o.surface = x.kinds[w];
        o.material = x.materials[w];
    }
    float v = 0;
    bool along = false;
    if (gt4_rules_ && !prologue_) {
        // F +0x4A4 at the step's end: the program's distance along the course (0x3955E8 at the center of gravity, the car's
        // road cell F +0x4A8 its hint; 0 where no gate holds the point). The drivers and the laps read it where it is found.
        const F at[3] = {c.pos.x, c.pos.z, -c.pos.y};
        const F d = ground_->course_distance(at, c.road_seg, &along);
        cars_[i].f3e4 = d;
        v = d.host();
    } else
        along = ground_->distance_along(cog, v, &x.checkpoint);
    if (along) {
        // (The checkpoint pair across the lap's end answers past the length: the line is at 0.)
        if (road_->length > 0 && v >= road_->length) v -= road_->length;
        if (x.has_v && road_->length > 0) {
            const float d = v - x.last_v;
            if (d < -road_->length * 0.5f) ++s.lap;
            else if (d > road_->length * 0.5f) --s.lap;
        }
        // The distance's change a step x 60, F +0x650 (0x358608 after the step, across the line by a lap: phase 14 reads it).
        if (i < gx_.size()) {
            F rate = F();
            if (x.has_v && road_->length > 0) {
                const F s(v), was(x.last_v), lap(road_->length), k60 = F::raw(0x42700000);
                if (was <= s) rate = (was + lap * kHalf < s ? s - (was + lap) : s - was) * k60;
                else rate = (s + lap * kHalf < was ? (s + lap) - was : s - was) * k60;
            }
            if (!gt4_rules_ || prologue_) cars_[i].f3e4 = F(v);
            gx_[i].near.course_rate = rate;
        }
        // A start place behind the line: the first lap begins when the car crosses it.
        if (!x.has_v && road_->length > 0 && v > road_->length * 0.5f) s.lap = -1;
        x.last_v = v, x.has_v = true;
        s.course_v = v;
    }
}

} // namespace gt4r::sim
