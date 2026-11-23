#include "gt4r/sim/session.h"

#include "base/ee_math.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/sim/exact_bridge.h"
#include "sim/exact/build_math.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/race_flow.h"
#include "sim/exact/race_inputs.h"
#include "sim/exact/start.h"

#include <algorithm>
#include <cmath>
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

// The road's answer into a wheel's ground record: the surface as its row of the core's tables.
void fill(ex::Ground& g, bool hit, const GroundHit& h) {
    if (!hit) {
        g.miss = 1, g.height = kNoGround;
        return;
    }
    g.miss = 0;
    g.height = F(h.point.y);
    g.attr_a = F(h.attr_a), g.attr_b = F(h.attr_b);
    g.attr_a_raw = u16(h.attr_a * 65535.0f), g.attr_b_raw = u16(h.attr_b * 65535.0f);
    Vec3 n = h.normal;
    float d = h.plane_d;
    if (n.y < 0.5f) n = {0, 1, 0}, d = -h.point.y; // 0x344458: too steep a triangle reads as level
    g.normal = {F(n.x), F(n.y), F(n.z)}, g.plane_d = F(d);
    g.seg = h.cluster;
    g.material = u8(core_surface(surface_index(h.material, h.flags)));
    g.x32 = 0;
    g.flags = u8(h.flags & 1); // the pit lane; the wet variant is in the surface already
}

} // namespace

// The course's walls for the collision step: the road's wall vertices are its wall point records as
// they are (position, the chain link in the tag's low half, the flags in its high half). A body's
// hint is its car's index: the walls and the other cars it can meet are those near its height.
class Session::Walls final : public gt3r::sim::collide::exact::Course {
public:
    using WallPt = gt3r::sim::collide::exact::WallPt;
    explicit Walls(const course::Runway& r, size_t cars) : heights_(cars, 0.0f) {
        pts_.reserve(r.wall_vertices.size() + 1);
        for (const course::Runway::WallVertex& v : r.wall_vertices) {
            WallPt p;
            p.x = F(v.pos.x), p.h = F(v.pos.y), p.z = F(v.pos.z);
            p.link = s16(u16(v.tag)), p.flags = u16(v.tag >> 16);
            pts_.push_back(p);
        }
        min_x_ = r.min.x, min_z_ = r.min.z;
        nx_ = std::max(1, int((r.max.x - r.min.x) / kCell) + 1), nz_ = std::max(1, int((r.max.z - r.min.z) / kCell) + 1);
        if (u64(nx_) * u64(nz_) > 4'000'000) nx_ = nz_ = 1;
        cells_.assign(size_t(nx_) * size_t(nz_), {});
        if (pts_.empty()) return;
        for (int i = 0; i < int(pts_.size()); ++i) {
            const int j = gt3r::sim::collide::exact::wall_next(pts_.data(), i);
            if (j < 0 || j >= int(pts_.size())) continue;
            const float ax = pts_[size_t(i)].x.host(), az = pts_[size_t(i)].z.host(), bx = pts_[size_t(j)].x.host(), bz = pts_[size_t(j)].z.host();
            const int x0 = cell_x(std::min(ax, bx)), x1 = cell_x(std::max(ax, bx)), z0 = cell_z(std::min(az, bz)), z1 = cell_z(std::max(az, bz));
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x) cells_[size_t(z) * size_t(nx_) + size_t(x)].push_back(i);
        }
    }
    void set_height(size_t car, float h) { heights_[car] = h; }

    const WallPt* walls() const override { return pts_.data(); }
    void wall_candidates(F x0, F z0, F x1, F z1, int hint, std::vector<int>& out) const override {
        if (pts_.empty()) return;
        const float level = hint >= 0 && size_t(hint) < heights_.size() ? heights_[size_t(hint)] : 0.0f;
        const int cx0 = cell_x(x0.host() - 8.0f), cx1 = cell_x(x1.host() + 8.0f), cz0 = cell_z(z0.host() - 8.0f), cz1 = cell_z(z1.host() + 8.0f);
        const size_t first = out.size();
        for (int z = cz0; z <= cz1; ++z)
            for (int x = cx0; x <= cx1; ++x)
                for (int i : cells_[size_t(z) * size_t(nx_) + size_t(x)])
                    if (std::fabs(pts_[size_t(i)].h.host() - level) < kLevel) out.push_back(i);
        std::sort(out.begin() + std::ptrdiff_t(first), out.end());
        out.erase(std::unique(out.begin() + std::ptrdiff_t(first), out.end()), out.end());
    }
    bool linked(int a, int b) const override {
        if (a < 0 || b < 0 || size_t(a) >= heights_.size() || size_t(b) >= heights_.size()) return true;
        return std::fabs(heights_[size_t(a)] - heights_[size_t(b)]) < kLevel;
    }

private:
    static constexpr float kCell = 16.0f, kLevel = 6.0f;
    int cell_x(float x) const { return std::clamp(int((x - min_x_) / kCell), 0, nx_ - 1); }
    int cell_z(float z) const { return std::clamp(int((z - min_z_) / kCell), 0, nz_ - 1); }
    std::vector<WallPt> pts_;
    std::vector<float> heights_;
    float min_x_ = 0, min_z_ = 0;
    int nx_ = 1, nz_ = 1;
    std::vector<std::vector<int>> cells_;
};

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
    const ByteSpan section = parsed.section(data, course::kSlotRunway);
    if (!course::Runway::is_runway(section)) throw std::runtime_error("session: the course " + course + " has no road");
    const Program program = Program::load(disc);
    std::vector<Entry> entries(cars.size());
    for (size_t i = 0; i < cars.size(); ++i)
        if (!stock_car(db, program, cars[i], entries[i].equipment)) throw std::runtime_error("session: no car " + cars[i]);
    return std::make_unique<Session>(db, program, std::make_shared<const course::Runway>(course::Runway::parse(section)), entries);
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
        c.behaviour = data_->behaviour();
        c.index = u8(i);
        c.control = 0, c.driver_kind = 0, c.ghost = 0, c.wheel_type = 0;
        { // the parameter block from the spec, as the car's set-up makes it
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
        // The start place: the entry's, or one further back along the first place's line when the grid is shorter.
        const int want = e.grid_place >= 0 ? e.grid_place : int(i);
        course::Runway::Place at = places[size_t(want) % places.size()];
        if (size_t(want) >= places.size()) {
            at = places[0];
            const float back = 8.0f * float(want);
            at.pos.x -= std::sin(at.heading) * back, at.pos.z -= std::cos(at.heading) * back;
            GroundHit h;
            if (ground_->at_place(at.pos, h)) at.pos.y = h.point.y;
        }
        place(i, at);
        publish(i);
    }
    // The collision bodies (the engine's race start: an octagon from the body's extents, the mass and the yaw inertia).
    walls_ = std::make_unique<Walls>(*road_, cars_.size());
    bodies_.resize(cars_.size());
    for (size_t i = 0; i < cars_.size(); ++i) {
        const ex::Params& p = cars_[i].p;
        bodies_[i].behaviour = cars_[i].behaviour;
        gt3r::sim::collide::exact::build(bodies_[i], p.body_front, p.body_rear, p.body_width, p.mass, p.yaw_inertia, int(i));
        bodies_[i].course = walls_.get();
    }
    gt3r::sim::collide::exact::pair_tables(bodies_);
    // The start's hold: the cars held as during a start's countdown until the engines idle and the
    // bodies have settled on their springs.
    for (int i = 0; i < kHoldSteps; ++i) hold_once({});
}

void Session::hold_once(std::span<const race::CarInput> inputs) {
    const u64 steps = steps_;
    race_.prep.start_count = 1;
    step_once(inputs);
    race_.prep.start_count = 0;
    steps_ = steps;
}

// The car at rest on a start place (the engine's race start: the pose on the road, the standing
// start), with the nose at the place.
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
    c.road_seg = under ? h.cluster : -1;
    c.pos.z = ground + c.p.cog_height;
    ex::body_axes(c);
    ex::hub_offsets(c);
    for (size_t w = 0; w < 4; ++w) {
        ex::Wheel& wheel = c.w[w];
        ex::Ground& g = c.g[w];
        g.x = c.pos.x + wheel.hub.x, g.z = -(c.pos.y + wheel.hub.y), g.h = c.pos.z + wheel.hub.z;
        GroundHit wh;
        const bool hit = ground_->under({g.x.host(), g.h.host(), g.z.host()}, wh);
        fill(g, hit, wh);
        extra_[i].kinds[w] = hit ? wh.flags : 0;
        if (hit) {
            wheel.ground = wheel.ground0 = g.height;
            wheel.surface = g.material, wheel.x25 = 0, wheel.surface_flags = g.flags;
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
    ex::ground_frame(c);
    c.pitch = c.ground_pitch, c.roll = c.ground_roll;
    c.pitch = c.pitch + ex::atan2(c.behaviour, c.p.ride_height[0] - c.p.ride_height[1], c.p.wheelbase);
    ex::body_axes(c);
    c.pos.z = c.ground_pos.z + c.up.z * c.p.cog_height;
    ex::hub_offsets(c);
    c.lag_heading = c.heading, c.lag_pitch = c.pitch;
    ex::standing_start(c);
    ex::tyres_start(c, race_.prep.wear);
    ex::step_length(c);
    c.best_lap = 0x157529FF;
    c.rank = gt3r::s8(i + 1);
    c.x16d1 = 1;
    c.dist_total = k0;
    ex::save_previous(c);
    ex::wheel_spin(c);
}

// The ground contact (the engine's phase 12 step with our road query: 0x344458 and 0x369888).
void Session::ground_contact(ex::Car& c, Extra& x) {
    for (size_t i = 0; i < 4; ++i) {
        ex::Wheel& w = c.w[i];
        ex::Ground& g = c.g[i];
        g.x = c.pos.x + w.hub.x, g.h = c.pos.z + w.hub.z, g.z = -(c.pos.y + w.hub.y);
        GroundHit h;
        const bool hit = ground_->under({g.x.host(), g.h.host(), g.z.host()}, h);
        fill(g, hit, h);
        if (!hit) {
            w.bump = k0;
            continue;
        }
        x.kinds[i] = h.flags;
        w.ground0 = g.height, w.ground = g.height;
        w.surface = g.material, w.x25 = 0, w.surface_flags = g.flags;
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
    c.x535 = u8(c.x535 & 0xEF);
    c.road_seg = c.g[0].seg;
    for (const ex::Ground& g : c.g)
        if (!g.miss && (g.flags & 1)) c.x535 = u8(c.x535 | 0x10);
}

// Phase 12 in the engine's order, the ground contact ours.
void Session::chassis(ex::Car& c, Extra& x) {
    ex::hub_offsets_xy(c);
    ex::speed_ramp(c);
    ground_contact(c, x);
    ex::terrain_factor(c);
    ex::suspension(c, nullptr);
    ex::attitude(c, k0);
    ex::bump_nudge(c);
    ex::pad_rumble(c);
    ex::steer_force(c, race_.feel);
    ex::tyre_effects(c, race_.effects, race_.aids.course_category);
    ex::phase12_stub(c);
    ex::engine_sound(c, race_.sound_scales);
    ex::effects_reset(c, 0, 0);
    ex::view_follow(c);
    ex::hub_offsets(c);
    ex::body_axes(c);
    ex::ground_frame(c);
    ex::speed_bookkeeping(c, race_.prep.condition);
    ex::wheel_spin(c);
}

// The collision step (the engine's phase 10: Gran Turismo 3's collision manager, whose routines Gran
// Turismo 4 keeps): every car's body moved through the step against the walls and the other cars.
void Session::collide() {
    namespace cx = gt3r::sim::collide::exact;
    for (size_t i = 0; i < cars_.size(); ++i) {
        cx::Body& b = bodies_[i];
        ex::Car& c = cars_[i];
        b.cx = c.pos.x, b.cy = c.pos.y, b.cz = c.pos.z;
        b.cvx = c.vel.x, b.cvy = c.vel.y, b.cvz = c.vel.z;
        b.ch = c.heading, b.cw = c.yaw_rate;
        b.hint = int(i); // the walls' level table (Walls)
        b.dt_in = c.dt, b.steer = c.steer_pct, b.ghost = c.ghost != 0, b.control = c.control;
        b.rumble = &c.hit_rumble;
        walls_->set_height(i, c.pos.z.host());
    }
    cx::step_all(bodies_);
    for (size_t i = 0; i < cars_.size(); ++i) {
        cx::Body& b = bodies_[i];
        ex::Car& c = cars_[i];
        b.rumble = nullptr;
        c.pos = {b.cx, b.cy, b.cz};
        c.vel = {b.cvx, b.cvy, b.cvz};
        c.heading = b.ch, c.yaw_rate = b.cw;
    }
}

void Session::step_once(std::span<const race::CarInput> inputs) {
    const u8 course_kind = ex::course_category(race_.aids.course_category);
    for (size_t i = 0; i < cars_.size(); ++i) {
        ex::Car& c = cars_[i];
        Extra& x = extra_[i];
        const race::CarInput in = i < inputs.size() ? inputs[i] : race::CarInput{};
        // The driver's frame: an analog pad.
        ex::PadFrame f;
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
        f.shift = pressed & 1 ? 1 : pressed & 2 ? -1 : 0;

        ex::save_previous(c);
        ex::prep(c, race_.prep);
        ex::driver_input_pad(c, race_.pad, f, course_kind);
        ex::wheel_steer(c);
        ex::slip_ratios(c, race_.statics);
        ex::aids(c, race_.aids);
        ex::dynamics(c, race_.statics, race_.gears, race_.class_grip);
        ex::brake_heat(c, race_.brake_heat);
        ex::clear_feedback(c);
    }
    collide();
    for (size_t i = 0; i < cars_.size(); ++i) {
        ex::Car& c = cars_[i];
        chassis(c, extra_[i]);
        ex::tire_wear(c, race_.prep.wear);
        ex::wear_flags(c, race_.prep.wear, {});
        ex::flaps(c, rng_);
        publish(i);
    }
    ++steps_;
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
        o.surface = x.kinds[w];
    }
    float v = 0;
    if (ground_->distance_along(cog, v, &x.checkpoint)) {
        if (x.has_v && road_->length > 0) {
            const float d = v - x.last_v;
            if (d < -road_->length * 0.5f) ++s.lap;
            else if (d > road_->length * 0.5f) --s.lap;
        }
        x.last_v = v, x.has_v = true;
        s.course_v = v;
    }
}

} // namespace gt4r::sim
