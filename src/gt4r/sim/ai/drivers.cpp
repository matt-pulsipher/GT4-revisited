#include "gt4r/sim/ai/drivers.h"

#include "base/ee_math.h"
#include "gt4r/sim/ai/ee_math.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/ai_line_course.h"
#include "gt4r/sim/exact_bridge.h"
#include "gt4r/sim/rules_build.h"
#include "sim/exact/tick.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>

namespace gt4r::sim::ai {

namespace ex = gt3r::sim::exact;
using gt3r::ee::F;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kKmh = 1.0f / 3.6f;
constexpr float kStep = Session::kStep;


// The zone cells around a car (0x4694F8's bytes +0x48 ..).
enum Zone { kFront, kFrontLeft, kFrontRight, kLeft, kRight, kRearLeft, kRearRight, kRear };

} // namespace

struct Field::Driver {
    size_t car = 0;
    int control = 2;        // F+0x466: 2 a computer car, 1 the player's car with a computer driver (B-spec)
    int index = 0;          // the car's grid index
    int drive_class = 0;
    Params p;
    // The plan (the driver object D, 0x463C00 ..).
    int line = 6, prev_line = 6, next = 0, mode = 0, next_kind = 0; // +0x1C / +0x21 / +0x1D / +0x1E / +0x1F
    F v_cur, v_next;        // +0x24 / +0x28
    F blend;                // +0x2C the old line's share of the aim
    u16 arc_ticks = 0;      // +0x22 steps in the arc
    int state = 0;          // +0x1B recovery: 0 racing, 1 backing out, 2 turning round
    int status = 0;         // +0x43: 1 nearing a braking point, 2 braking, 3 / 4 / 5 in an arc near the grip's limit
    int state_hold = 0;     // +0x6B steps the state machine rests after a change
    s16 cooldown = 0;       // +0x18 the lane change's cooldown
    int start_flag = 0;     // +0x1A 0: the standing start's long look ahead (5 s)
    int stop_count = 0;     // +0x52 the stop driver's count down
    int pit = 0;            // +0x20 held to the pit speed
    bool overtake = true;   // +0x6E a B-spec driver may pass
    int pace = 2;           // +0x6D the pace level
    u8 shift = 128;         // +0x6F the shift speeds' factor / 128
    F pace_brake{1.0f}, pace_corner{1.0f}; // +0x70 / +0x74 the pace level's braking and arcs' factors (B-spec)
    F offset;               // +0x54 the sideways offset of the aim (m, + right)
    u16 rand = 0;           // +0x68
    F corner_mult{1.0f}, brake_mult{1.0f}, alongside_mult{1.0f}; // +0xB8 / +0xBC / +0xC0
    // Outputs (+0x30 / +0x34 / +0x38 / +0x3E / +0x3C).
    F steer, throttle, brake;
    bool reverse = false;
    u16 shift_lock = 0;
    float applied_throttle = 0;
    // Checks.
    Incidents inc;
    bool was_wall = false, was_car = false, was_off = false;
    int prev_state = 0;
    float s = 0;            // the car's distance along the course this step, wrapped into the lap
    bool hit_front = false, hit_rear = false;
    u8 blip = 0;
    float look = 0;
    F eps, aim_x, aim_y;    // the heading's error to the aim, the aim (F +0x61C / +0x620)
    float brake_dist = 0, brake_to = 0;
    rules::WheelLayout lay; // the car's wheels (0x359510)
    std::array<u8, 0x54> lay_raw{0, 4, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 2}; // its row of the program's table
    int kind = 0;           // F+0x467: the driver's kind (0 racing)
};

struct Field::Near {
    std::array<u8, 8> zone{};
    F gap_left{1000.0f}, gap_right{1000.0f}; // +0x58 / +0x5C
    F contact;                               // +0x44
    int follow = -1;                         // +0x51
    int chain = 0;                           // +0x6A
};

std::optional<course::AiData> Field::read(Disc& disc, const std::string& course) {
    gt3r::Volume& vol = disc.volume();
    const gt3r::Volume::Entry* e = vol.find("crs/" + course + ".ad");
    if (!e) { // Prologue: the lines and sets in the course file (ai_line_course.h)
        const gt3r::Volume::Entry* c = vol.find("crs/" + course);
        if (!c) return std::nullopt;
        const Bytes file = vol.read(*c);
        if (!course::CourseFile::looks_like_course(file)) return std::nullopt;
        return course::ai_from_course(course::CourseFile::parse(file), file);
    }
    const Bytes data = vol.read(*e);
    if (!course::AiData::looks_like(data)) return std::nullopt;
    return course::AiData::parse(data);
}

std::unique_ptr<Field> Field::open(Disc& disc, const std::string& course, const Session& session, std::span<const size_t> computer, const Pace& pace) {
    const std::optional<course::AiData> data = read(disc, course);
    if (!data) throw std::runtime_error("ai: no driver file crs/" + course + ".ad");
    const Program program = Program::load(disc);
    return std::make_unique<Field>(session, *data, program, computer, pace);
}

// The program's constants.
static const F kHundredth = F::raw(0x3C23D70A), kPerKmh = F::raw(0x3E8E38E3), kPerDeg = F::raw(0x3C8EFA34), kHundred = F::raw(0x42C80000);
static const F kOne = F::raw(0x3F800000), kHalf = F::raw(0x3F000000);

static F int_f(s32 v) { return gt3r::ee::int_to_f(v); }

// The course's set for a driveline class into a driver's parameters (0x466C20), in the program's arithmetic.
static Params params_from(const std::array<u8, 0x80>& s, int cls, const Tuning& t) {
    Params p;
    const auto b = [&](size_t i) { return int_f(s32(s[i])); };
    p.yaw_gain = int_f(s32(s[3]) * 5) * kHundredth;
    p.look_min = b(7), p.look_max = b(8);
    p.throttle = kOne;
    p.look_speed = kOne / (int_f(s32(s[6]) * 2) * kPerKmh);
    p.brake = b(0) * kHundredth;
    p.corner_scale = b(1) * kHundredth;
    p.corner_offset = int_f(s32(s[2]) - 100) * kPerKmh;
    p.sat_throttle = b(4) * kHundredth, p.sat_brake = b(5) * kHundredth;
    p.front_weight = b(0xE) * kHundredth;
    p.corner_bonus = b(0xF) * kPerKmh;
    p.steer_rate = int_f(s32(s[0x10]) * 10) * kPerDeg;
    p.bonus_ticks = int(s[0x11]) * 10;
    p.pit_speed = int_f(s32(s[0x12] ? s[0x12] : 80)) * kPerKmh;
    p.brake = p.brake * t.brake_global;
    p.aids = rules::driver_block(s.data());
    if (s[0x1D] && s[0x1C]) {
        p.straight_bonus = true;
        p.bonus_angle = b(0x1C) * kPerDeg;
        p.bonus_speed = b(0x1D) * kPerKmh;
        const u8 cb = t.class_bonus[size_t(cls) & 3];
        if (cb != 0 && cb != 100) p.bonus_speed = p.bonus_speed * (int_f(cb) / kHundred);
    }
    if (s[0x1B] == 0x20)
        for (size_t i = 0; i < 3; ++i)
            if (s[0x20 + i] != 0 && s[0x20 + i] != 100) p.grip[i] = b(0x20 + i) / kHundred;
    return p;
}

Field::Field(const Session& session, const course::AiData& data, const Program& program, std::span<const size_t> computer, const Pace& pace)
    : own_cars_(std::make_unique<SessionCars>(session)), session_(own_cars_.get()), tables_(Tables::load(program)), lines_(data, session.ground(), tables_),
      pace_(pace) {
    init(data, program, computer);
}

Field::Field(const Cars& cars, const course::AiData& data, const Program& program, std::span<const size_t> computer, const Pace& pace)
    : session_(&cars), tables_(Tables::load(program)), lines_(data, cars.ground(), tables_), pace_(pace) {
    init(data, program, computer);
}

void Field::init(const course::AiData& data, const Program& program, std::span<const size_t> computer) {
    const Cars& session = *session_;
    sets_ = data.sets;
    if (program.known())
        for (u32 m = 0; m < 24; ++m) soft_[m] = program.w(Program::kSoft + 4 * m);
    else
        soft_[2] = soft_[3] = soft_[4] = soft_[6] = 1;
    const size_t n = session.size();
    drivers_.resize(n);
    ranks_.assign(n, 0);
    near_.assign(n, Near{});
    scale_.assign(n, kOne);
    // The band (0x365480): percents x 0.01, the start gaps in metres, the full gaps x 10 m, the overall percent (0: 100).
    {
        const Pace& p = pace_;
        Band b;
        b.on = p.slow_pct || p.fast_pct || (p.band_global && p.band_global != 100);
        b.mode = p.band_mode;
        b.slow = int_f(p.slow_pct) * kHundredth, b.slow_start = int_f(p.slow_start_m), b.slow_full = int_f(p.slow_full_10m) * F::raw(0x41200000);
        b.fast = int_f(p.fast_pct) * kHundredth, b.fast_start = int_f(p.fast_start_m), b.fast_full = int_f(p.fast_full_10m) * F::raw(0x41200000);
        b.global = int_f(p.band_global ? p.band_global : 100) * kHundredth;
        band_ = b;
    }
    lat_v_.assign(n, F()), lat_x_.assign(n, F()), rate_.assign(n, F()), last_s_.assign(n, F());
    for (size_t i = 0; i < n; ++i) last_s_[i] = F(session.state(i).course_v);
    for (size_t car : computer) {
        if (car >= n) continue;
        auto d = std::make_unique<Driver>();
        d->car = car, d->index = int(car);
        // The driveline class (0x466B28): FF 1; four wheel drive by its system: 3 -> 3, 0 -> 1, 5 -> 0, others 2; the rest 0.
        const Spec& spec = session.spec(car);
        const int drive = spec.drive(), system = spec.b(0x129);
        d->drive_class = drive == 1 ? 1 : drive == 2 ? (system == 3 ? 3 : system == 0 ? 1 : system == 5 ? 0 : 2) : 0;
        if (!program.known()) { // the front wheels steer; wheels 0 and 2 on the left
            d->lay.fore_aft = {1, 1, 0xFF, 0xFF};
            d->lay_raw[0x3C] = d->lay_raw[0x3D] = 1;
            d->lay_raw[0x1D] = d->lay_raw[0x1F] = 1;
        } else {
            const u8 row = wheel_layout_index(spec);
            d->lay = rules::wheel_layout(program, row);
            for (u32 k = 0; k < 0x54; ++k) d->lay_raw[k] = program.b(rules::kWheelLayouts + rules::kWheelLayoutSize * row + k);
        }
        setup(*d);
        // The first line: the main line, or the nearest of the others to the car.
        d->line = d->prev_line = lines_.resolve(6);
        drivers_[car] = std::move(d);
    }
    for (auto& d : drivers_)
        if (d && d->line >= 0) {
            const race::CarState& st = session.state(d->car);
            d->s = lines_.wrap_lap(st.course_v);
            replan(*d);
        }
}

// A driver's parameters from the course's set for its driveline class (0x466C20), the event's changes (0x467148), its pace
// level (0x3F1890) and its draws' seed (0x4639E0), by its control source.
void Field::setup(Driver& d) {
    d.corner_mult = d.brake_mult = d.alongside_mult = kOne;
    std::array<u8, 0x80> set{};
    if (const auto& s = sets_[size_t(d.drive_class)]) set = *s;
    else std::copy(tables_.builtin[session_->loose() ? 1 : 0].begin(), tables_.builtin[session_->loose() ? 1 : 0].end(), set.begin());
    d.p = params_from(set, d.drive_class, tables_.tuning);
    const ex::Car& c = session_->record(d.car);
    // 0x4673D8: the downforce term (the aero bonus's lift part is ❓ not mirrored: 0).
    if (c.p.mass != F()) d.p.downforce = (c.p.downforce_coef[0] + c.p.downforce_coef[1]) / c.p.mass;
    // The event's changes (0x467148): each class's percents, then the drivers' spread by grid index.
    int thr = pace_.throttle[size_t(d.drive_class)], brk = pace_.brake[size_t(d.drive_class)], cs = pace_.corner[size_t(d.drive_class)];
    if (thr == 0 || thr > 100) thr = 100;
    if (brk == 0) brk = 100;
    if (cs == 0) cs = 100;
    int off = 0;
    if (tables_.driver_spread && d.control == 2) {
        const int idx = d.index;
        if (!tables_.tuning.brake_spread) brk -= (idx % 3) * 7;
        if (!tables_.tuning.corner_spread) cs -= (idx * 4) & 4, off = -2 * ((idx * 7) % 5);
        d.p.yaw_gain = d.p.yaw_gain * (int_f(((idx + 1) % 3) * 3 + 97) * kHundredth);
    }
    if (thr != 100) d.p.throttle = int_f(thr) * kHundredth;
    if (brk != 100) d.brake_mult = d.brake_mult * (int_f(brk) * kHundredth);
    if (cs != 100) d.corner_mult = d.corner_mult * (int_f(cs) * kHundredth);
    if (off) d.p.corner_offset = d.p.corner_offset + int_f(off) * kPerKmh;
    // The pace level (0x3F1890): a computer driver's starting level, passing allowed.
    {
        const Tables::PaceLevel& l = tables_.pace[tables_.pace_start[1]];
        d.pace = tables_.pace_start[1], d.overtake = true, d.pace_brake = l.brake, d.pace_corner = l.corner, d.shift = l.shift;
    }
    // The random draws (0x4639E0): seeded by the grid index for a computer car, then one step.
    d.rand = u16(d.control == 2 ? d.index + 1 : 1);
    d.rand = u16(((u32(d.rand) * 0x41C64E6Du + 0x3039u) >> 16) & 0x7FFF);
    // 0x463AE0: the long look ahead (D +0x1A = 0) for a computer car from a standing start on a paved course.
    d.start_flag = (d.control == 1 || d.control == 2) && pace_.standing_start && !session_->loose() && c.speed < F::raw(0x41055554) ? 0 : 1;
}

Field::~Field() = default;

void Field::set_bspec(size_t car) {
    if (car >= drivers_.size() || !drivers_[car]) return;
    Driver& d = *drivers_[car];
    d.control = 1;
    // 0x3F1890: the B-spec driver's starting level, no passing; 0x4639E0: its draws seeded with 1; 0x463AE0: the long
    // look ahead off a standing start as a computer car's.
    const Tables::PaceLevel& l = tables_.pace[tables_.pace_start[0]];
    setup(d);
    d.pace = tables_.pace_start[0], d.overtake = false, d.pace_brake = l.brake, d.pace_corner = l.corner, d.shift = l.shift;
}

void Field::command(size_t car, const BSpecCommand& c) {
    if (car >= drivers_.size() || !drivers_[car]) return;
    Driver& d = *drivers_[car];
    if (c.pace >= 0 && c.pace <= 4 && c.pace != d.pace) {
        const Tables::PaceLevel& l = tables_.pace[size_t(c.pace)];
        d.pace = c.pace, d.pace_corner = l.corner, d.pace_brake = l.brake, d.shift = l.shift;
    }
    if (c.overtake >= 0 && bool(c.overtake) != d.overtake) {
        d.overtake = c.overtake != 0;
        if (!d.overtake && d.kind == 0 && (d.line == 1 || d.line == 2)) request_line(d, CourseLines::kMain, false);
    }
}

int Field::pace_level(size_t car) const { return car < drivers_.size() && drivers_[car] ? drivers_[car]->pace : -1; }
bool Field::overtaking(size_t car) const { return car < drivers_.size() && drivers_[car] && drivers_[car]->overtake; }
u8 Field::shift_factor(size_t car) const {
    const Driver* d = driver(car);
    return d && d->control == 1 && d->kind == 0 ? d->shift : 128;
}

const Field::Driver* Field::driver(size_t car) const { return car < drivers_.size() ? drivers_[car].get() : nullptr; }
const Params& Field::params(size_t car) const { return drivers_.at(car)->p; }
const Incidents& Field::incidents(size_t car) const { return drivers_.at(car)->inc; }
int Field::line(size_t car) const { return drivers_.at(car)->line; }
int Field::recovery(size_t car) const { return drivers_.at(car)->state; }
float Field::time_scale(size_t car) const { return car < scale_.size() ? scale_[car].host() : 1.0f; }
u32 Field::time_scale_bits(size_t car) const { return car < scale_.size() ? scale_[car].bits : 0x3F800000u; }

Field::Snapshot Field::snapshot(size_t car) const {
    Snapshot o;
    const Driver* d = driver(car);
    if (!d) return o;
    const Near& z = near_[car];
    o.line = d->line, o.next = d->next, o.mode = d->mode, o.status = d->status, o.state = d->state, o.follow = z.follow;
    o.wanted_throttle = d->throttle.host(), o.reverse = d->reverse, o.look = d->look, o.brake_dist = d->brake_dist, o.brake_to = d->brake_to;
    o.v_cur = d->v_cur.host(), o.v_next = d->v_next.host(), o.steer = d->steer.host(), o.lock = session_->record(car).lock_hi.host();
    o.steer_cmd = session_->record(car).steer_cmd.host(), o.throttle = d->applied_throttle, o.brake = d->brake.host(), o.contact = z.contact.host();
    const ex::Car& c = session_->record(car);
    float x, y, h;
    if (d->line >= 0 && lines_.evaluate(d->line, d->next, d->s, x, y, h)) {
        const float fx = c.fwd.x.host(), fy = c.fwd.y.host(), hd = std::atan2(-fx, fy);
        o.line_offset = std::cos(hd) * (c.pos.x.host() - x) + std::sin(hd) * (c.pos.y.host() - y);
    }
    return o;
}

void Field::probe_set(size_t car, const Probe& p) {
    Driver& d = *drivers_.at(car);
    Near& z = near_.at(car);
    d.line = p.line, d.next = p.next, d.mode = p.mode, d.next_kind = p.next_kind, d.status = p.status;
    d.v_cur = p.v_cur, d.v_next = p.v_next, d.s = p.s, d.rand = p.rand;
    d.corner_mult = p.corner_mult, d.brake_mult = p.brake_mult, d.alongside_mult = p.alongside_mult;
    d.cooldown = p.cooldown;
    d.state = p.state, d.prev_line = p.prev_line, d.state_hold = p.state_hold, d.start_flag = p.start_flag, d.stop_count = p.stop_count;
    d.pit = p.pit, d.arc_ticks = p.arc_ticks, d.blend = p.blend, d.offset = p.offset;
    d.steer = p.steer, d.throttle = p.throttle, d.brake = p.brake, d.reverse = p.reverse, d.shift_lock = p.shift_lock;
    d.kind = p.kind;
    z.follow = p.follow, z.chain = p.chain, z.contact = p.contact, z.zone = p.zone, z.gap_left = p.gap_left, z.gap_right = p.gap_right;
}

Field::Probe Field::probe_get(size_t car) const {
    const Driver& d = *drivers_.at(car);
    const Near& z = near_.at(car);
    Probe p;
    p.line = d.line, p.next = d.next, p.mode = d.mode, p.next_kind = d.next_kind, p.status = d.status;
    p.v_cur = d.v_cur, p.v_next = d.v_next, p.s = d.s, p.rand = d.rand;
    p.corner_mult = d.corner_mult, p.brake_mult = d.brake_mult, p.alongside_mult = d.alongside_mult;
    p.cooldown = d.cooldown;
    p.state = d.state, p.prev_line = d.prev_line, p.state_hold = d.state_hold, p.start_flag = d.start_flag, p.stop_count = d.stop_count;
    p.pit = d.pit, p.arc_ticks = d.arc_ticks, p.blend = d.blend, p.offset = d.offset;
    p.steer = d.steer, p.throttle = d.throttle, p.brake = d.brake, p.reverse = d.reverse, p.shift_lock = d.shift_lock;
    p.kind = d.kind;
    p.follow = z.follow, p.chain = z.chain, p.contact = z.contact, p.zone = z.zone, p.gap_left = z.gap_left, p.gap_right = z.gap_right;
    return p;
}

F Field::probe_corner_speed(size_t car, int i) { return corner_speed(*drivers_.at(car), i); }
F Field::probe_braking(size_t car) { return braking(*drivers_.at(car)); }

Field::Probe Field::probe_decide(size_t car) {
    Driver& d = *drivers_.at(car);
    if (F x; session_->lat_filter(car, x)) lat_x_[car] = x;
    if (int r; session_->rank(car, r)) ranks_[car] = r;
    if (u32 ck; session_->clock(ck)) clock_ = ck;
    d.s = lines_.wrap_lap(session_->state(car).course_v);
    incidents_after(d);
    decide(d);
    Probe p = probe_get(car);
    p.aim_x = d.aim_x, p.aim_y = d.aim_y;
    return p;
}

void Field::probe_proximity() {
    for (size_t i = 0; i < near_.size(); ++i)
        if (F r; session_->course_rate(i, r)) rate_[i] = r;
    proximity();
}

// ---- the plan --------------------------------------------------------------------------------------------------

// 0x463988: a + (b - a) x the driver's draw / 32768.
static F draw(u16 r, F a, F b) { return a + (b - a) * (int_f(s32(r)) * F::raw(0x38000000)); }

// A wheel's tire record on a surface (0x360F80): Gran Turismo 4's records where the program is mapped, else the shared
// engine's (16 surfaces) the session builds.
struct TireView {
    F grip, mu, peak_slip, brake_peak;
    const ex::Table<4>* load_curve = nullptr;
};
static TireView tire_view(const Cars& cars, bool gt4, size_t car, const rules::WheelLayout& lay, int wheel, int surface) {
    TireView v;
    if (gt4) {
        const rules::Tire& t = rules::tire_record(cars.extra(car).tires, lay, wheel, u8(surface % 24));
        v.grip = t.grip, v.mu = t.mu[size_t(surface % 24)], v.peak_slip = t.peak_slip, v.brake_peak = t.brake_peak, v.load_curve = &t.load_curve;
        return v;
    }
    const ex::Car& c = cars.record(car);
    const int row = core_surface(surface) & 15;
    const size_t axle = wheel < 2 ? 0 : 1;
    const ex::TireRecord& t = c.p.tire[std::min<size_t>(c.p.tire_index[axle][size_t(row)], 5)];
    v.grip = t.grip, v.mu = t.mu[size_t(row)], v.peak_slip = t.peak_slip, v.brake_peak = t.brake_peak, v.load_curve = &t.load_curve;
    return v;
}

// 0x467470: the computer drivers' grip factor for the front's tire record on a surface (1 for a pad car without a driver).
F Field::ai_grip(const Driver& d, int surface) const {
    if (d.control != 1 && d.control != 2 && d.kind == 0) return kOne;
    const u8 cls = tables_.known ? session_->extra(d.car).tires.tire_index[0][size_t(surface) % 24]
                                 : session_->record(d.car).p.tire_index[0][size_t(core_surface(surface) & 15)] % 3;
    return d.p.grip[std::min<size_t>(cls, 2)];
}

// 0x4674F0, in the program's arithmetic.
F Field::corner_speed(const Driver& d, int i) {
    const ex::Car& c = session_->record(d.car);
    const std::vector<LinePoint>& pts = lines_.line(d.line);
    if (pts.empty()) return c.p.top_speed;
    const LinePoint& pt = pts[size_t(i) % pts.size()];
    const u8 s = u8(pt.surface % 24);
    const TireView rf = tire_view(*session_, tables_.known, d.car, d.lay, d.lay_raw[0xC], s);
    const TireView rr = tire_view(*session_, tables_.known, d.car, d.lay, d.lay_raw[0xD], s);
    const F w = d.p.front_weight; // (❓ two flags of the car halve it towards 0.5: not mirrored)
    const F g = ai_grip(d, s);
    const F mu0 = ((w * rf.mu) * rf.grip) * g, mu1 = (((kOne - w) * rr.mu) * rr.grip) * g;
    F a = mu0 + mu1; // (with tire wear on: the wheels' wear grips weigh it: not done)
    const F r(pt.radius), bank(pt.bank), zero;
    if (zero <= r) {
        a = a * r;
        if (bank < zero) a = a / (kOne + gt3r::ee::sin(bank));
    } else {
        a = a * (-r);
        if (zero < bank) a = a / (kOne - gt3r::ee::sin(bank));
    }
    const F e = pt.crest ? zero : d.p.aero_bonus;
    F q = e + kOne, v;
    for (int k = 0; k < 3; ++k) {
        v = gt3r::ee::sqrt(a * q);
        q = ((d.p.downforce * v) * v + kOne) + e;
    }
    F m = d.control == 0 ? kOne : d.corner_mult;
    const Tuning& t = tables_.tuning;
    if (t.corner_spread && (d.control == 1 || d.control == 2) && d.kind == 0) m = m + draw(d.rand, t.corner_lo.at(m), t.corner_hi.at(m));
    F scale = d.p.corner_scale * m;
    if (d.control == 1 && d.kind == 0) scale = scale * d.pace_corner;
    if (const auto& zc = near_[d.car].zone; t.alongside_slow && (zc[kFrontLeft] || zc[kLeft] || zc[kRight] || zc[kFrontRight]))
        scale = scale * (kOne - t.alongside_share * (d.control == 0 ? kOne : d.alongside_mult));
    const F offset = d.p.corner_offset + int_f(pt.speed_adjust == -128 ? 0 : pt.speed_adjust) * kPerKmh;
    // (on the front's first tire record the scale takes T+0x158's factor of its paved grip: 1 throughout in this program)
    F out = scale * v + offset;
    const F top = c.p.top_speed, least = F::raw(0x40B1C71B); // 20 km/h
    if (top < out) out = top;
    else if (out < least) out = least;
    return out;
}

// 0x4679C0: the next segment: a random draw, the current one takes the next's kind and speed.
void Field::bookkeeping_shift(Driver& d) {
    d.rand = u16(((u32(d.rand) * 0x41C64E6Du + 0x3039u) >> 16) & 0x7FFF);
    const ex::Car& c = session_->record(d.car);
    const std::vector<LinePoint>& pts = lines_.line(d.line);
    const int count = int(pts.size());
    d.mode = d.next_kind;
    d.v_cur = d.v_next;
    if (count == 0) return;
    int i = d.next == count ? 0 : d.next;
    if (pts[size_t(i)].speed_adjust == -128) i = (i + 1) % count;
    d.next_kind = pts[size_t(i)].kind;
    d.v_next = d.next_kind == 0 ? c.p.top_speed : corner_speed(d, i);
}

// 0x467AA8: the plan made again from the car's place on its line.
void Field::replan(Driver& d) {
    const ex::Car& c = session_->record(d.car);
    const std::vector<LinePoint>& pts = lines_.line(d.line);
    const int count = int(pts.size());
    if (count == 0) return;
    int n = count;
    for (int i = 0; i < count; ++i)
        if (d.s < pts[size_t(i)].dist) {
            n = i;
            break;
        }
    d.next = n;
    int cur = (n + count - 1) % count;
    if (pts[size_t(cur)].speed_adjust == -128) cur = (cur - 1 + count) % count;
    d.next_kind = pts[size_t(cur)].kind;
    d.v_next = d.next_kind ? corner_speed(d, cur) : c.p.top_speed;
    bookkeeping_shift(d);
}

// 0x463C28: a line change (the request's fallback resolved), blended unless at once: the old line's share of the aim
// starts at 1, or at what is left of it when the request is for the line being left.
bool Field::request_line(Driver& d, int line, bool at_once) {
    const int r = lines_.resolve(line);
    if (r < 0) return false;
    if (at_once) { // 0x463C00
        d.prev_line = d.line = r;
        d.offset = F(), d.blend = F();
    } else {
        d.blend = line == d.prev_line ? kOne - d.blend : kOne;
        d.prev_line = d.line;
        d.line = r;
    }
    replan(d);
    ++d.inc.lane_changes;
    return true;
}

// 0x463EA8: the point bookkeeping, the braking point, the status; the throttle's multiplier.
F Field::braking(Driver& d) {
    d.status = 0;
    if (d.line == CourseLines::kPitStop) return kOne;
    const ex::Car& c = session_->record(d.car);
    const std::vector<LinePoint>& pts = lines_.line(d.line);
    const int count = int(pts.size());
    if (count == 0) return kOne;
    const float lap = lines_.lap(), s = d.s;
    // At most one step a tick.
    int n = d.next;
    bool changed = false;
    if (n == 0 && s > lap * 0.5f) n = count, changed = true;
    else if (n < count && pts[size_t(n)].dist <= s) ++n, changed = true;
    else if (n == count && s < lap * 0.5f) n = 0; // the start line crossed: no shift
    else if (n != 0 && n <= count && s < pts[size_t(n - 1)].dist) --n, changed = true;
    d.next = n;
    if (changed) bookkeeping_shift(d);
    int bp = d.next == count ? 0 : d.next;
    if (pts[size_t(bp)].speed_adjust == -128) bp = (bp + 1) % count;
    // The braking point (in the program's arithmetic).
    const F v = c.speed, zero;
    F mult = kOne;
    if (d.mode == 3 || d.mode == 4) d.status = 2;
    else if (d.next_kind != 0 && d.v_next < v) {
        F kb = d.p.brake;
        F m = d.control == 0 ? kOne : d.brake_mult;
        const Tuning& t = tables_.tuning;
        if (t.brake_spread && (d.control == 1 || d.control == 2) && d.kind == 0) m = m + draw(d.rand, t.brake_lo.at(m), t.brake_hi.at(m));
        kb = kb * m;
        if (d.control == 1 && d.kind == 0) kb = kb * d.pace_brake;
        const LinePoint& q = pts[size_t(bp)];
        const u8 s = u8(q.surface % 24), axle = d.lay_raw[0x10] & 1;
        const TireView rf = tire_view(*session_, tables_.known, d.car, d.lay, d.lay_raw[0xC], s);
        F mu = rf.mu * ai_grip(d, s);
        const F width = tables_.known ? session_->extra(d.car).tires.width[axle] : c.p.width[axle];
        if (rf.load_curve->n > 0 && width != F()) mu = mu * ex::lookup_bisect(*rf.load_curve, (c.p.weight * kHalf) / width);
        mu = (mu * rf.brake_peak) * c.p.cam_offset[axle];
        if (!(c.x535 & 0x10)) { // off the pit lane: the steered wheels' wear grips
            F sum;
            int k = 0;
            for (int w = 0; w < int(d.lay.count) && w < 4; ++w)
                if (gt3r::s8(d.lay.fore_aft[size_t(w)]) > 0) sum = sum + c.w[size_t(w)].wear_grip, ++k;
            if (k) mu = (mu * sum) / int_f(k);
        }
        const F vn = d.v_next;
        const F e = q.crest ? zero : d.p.aero_bonus;
        const F qh = (d.p.downforce * v) * v + kOne, ql = (d.p.downforce * vn) * vn + kOne;
        const F hq = ((mu * F::raw(0x411CCCCC)) * ((qh + e) + (ql + e))) * kHalf; // 9.8
        F coef = kOne / (hq + hq);
        if (coef < c.p.brake_coef) coef = c.p.brake_coef;
        coef = coef * (kOne - gt3r::ee::sin(F(q.slope)));
        F dist = zero + ((v + vn) * (v - vn)) * coef;
        if (d.control != 1 && d.control != 2 && d.kind == 0) dist = dist + c.speed;
        F adj = int_f(q.brake_adjust) * kHundredth + kOne;
        if (adj < F::raw(0x3DCCCCCC)) adj = F::raw(0x3DCCCCCC);
        dist = dist * adj;
        if (kOne < kb) dist = dist / kb;
        if (d.control == 1 || d.control == 2) {
            const Near& z = near_[d.car];
            dist = dist + int_f(z.chain);
            if (d.cooldown == 0 && zero < z.contact) {
                dist = dist + F::raw(0x41200000);
                // (+8 m when the car followed is itself in contact: its driver object's +0x44)
                if (z.follow >= 0 && zero < near_[size_t(z.follow)].contact) dist = dist + F::raw(0x41000000);
            }
        }
        const F dx = F(q.x) - c.pos.x, dy = F(q.y) - c.pos.y;
        const F to = gt3r::ee::sqrt(dx * dx + dy * dy);
        d.brake_dist = dist.host(), d.brake_to = to.host();
        if (dist < to) {
            const F soft = kb < kOne ? dist * (kOne / kb - kOne) : zero;
            const F margin = to - dist;
            if (margin < soft + F::raw(0x41C80000)) d.status = 1;
            if (zero < soft && margin < soft) mult = margin / soft;
        } else {
            d.mode = d.mode ? 4 : 3;
            d.status = 2;
        }
    }
    // The event's throttle share as a share of the full-throttle torque.
    if (d.line != CourseLines::kPitLane && d.p.throttle < kOne) {
        const F wot = c.torque_wot, fr = c.torque_friction, sum = wot + fr;
        if (sum != F()) mult = mult * ((d.p.throttle * wot + fr) / sum);
    }
    return mult;
}

// ---- the aim -------------------------------------------------------------------------------------------------------

// The program's constants (speeds in m/s, angles in radians).
namespace k {
const F zero, one = F::raw(0x3F800000), half = F::raw(0x3F000000), two = F::raw(0x40000000), five = F::raw(0x40A00000);
const F kmh1 = F::raw(0x3E8E38E3), kmh2 = F::raw(0x3F0E38E3), kmh3 = F::raw(0x3F555554), kmh5 = F::raw(0x3FB1C71B), kmh10 = F::raw(0x4031C71B),
        kmh20 = F::raw(0x40B1C71B), kmh25 = F::raw(0x40DE38E2), kmh30 = F::raw(0x41055554), kmh50 = F::raw(0x415E38E2);
const F deg10 = F::raw(0x3E32B8C1), deg20 = F::raw(0x3EB2B8C1), deg30 = F::raw(0x3F060A90), deg40 = F::raw(0x3F32B8C1), deg45 = F::raw(0x3F490FD9),
        deg50 = F::raw(0x3F5F66F1), deg60 = F::raw(0x3F860A90), deg80 = F::raw(0x3FB2B8C1);
const F pi = F::raw(0x40490FDA), two_pi = F::raw(0x40C90FDA), three_pi = F::raw(0x4116CBE3);
} // namespace k

// 0x36CB90: an angle into [-pi, pi) (beyond three half turns by whole turns, 0x36CB20).
static F angle_in_turn(F x) {
    if (x < -k::pi) {
        if (x < -k::three_pi) {
            const F n = gt3r::ee::int_to_f(gt3r::ee::to_int(((gt3r::ee::abs(x) + k::pi) / k::two_pi).bits));
            return x + n * k::two_pi;
        }
        return x + k::two_pi;
    }
    if (k::pi <= x) {
        if (k::three_pi < x) {
            const F n = gt3r::ee::int_to_f(gt3r::ee::to_int(((gt3r::ee::abs(x) + k::pi) / k::two_pi).bits));
            return x + n * (-k::two_pi);
        }
        return x - k::two_pi;
    }
    return x;
}

// 0x36CC40: a - b as an angle in [-pi, pi].
static F angle_between(F a, F b) {
    F d = angle_in_turn(a) - angle_in_turn(b);
    if (k::pi <= d) d = d - k::two_pi;
    else if (d < -k::pi) d = d + k::two_pi;
    return d;
}

F Field::course_s(size_t car) const { return F(session_->state(car).course_v); }

bool Field::place(int line, int hint, F s, F& x, F& y, F& h) const {
    const std::vector<LinePoint>& p = lines_.line(line);
    if (p.empty()) return false;
    const F lap(lines_.lap());
    if (line == CourseLines::kPitStop) {
        // 0x46B7B8: the stop line, its distances a lap on in the lap's first half: straight between its points.
        if (s < k::zero) s = s + lap;
        else if (lap <= s) s = s - lap;
        if (s < lap * k::half) s = s + lap;
        const size_t n = p.size();
        if (s < F(p[0].dist) || F(p[n - 1].dist) <= s) return false;
        size_t i = 0;
        while (i + 1 < n && !(F(p[i].dist) <= s && s < F(p[i + 1].dist))) ++i;
        if (i + 1 >= n) return false;
        const LinePoint &a = p[i], &b = p[i + 1];
        const F t = (s - F(a.dist)) / (F(b.dist) - F(a.dist));
        x = t * (F(b.x) - F(a.x)) + F(a.x);
        y = t * (F(b.y) - F(a.y)) + F(a.y);
        h = t * (F(b.height) - F(a.height)) + F(a.height);
        return true;
    }
    if (s < k::zero) s = s + lap;
    else if (lap <= s) s = s - lap;
    // 0x46B310: the span: before the first point or past the last (the last to the first), else searched from the hint
    // outwards (hint, hint + 1, hint - 1, hint + 2, ...).
    const int n = int(p.size());
    int from = -1, to = -1, kind = 0;
    if (s < F(p[0].dist)) from = n - 1, to = 0, kind = 1;
    else if (F(p[size_t(n - 1)].dist) <= s) from = n - 1, to = 0, kind = 2;
    else {
        int at = hint;
        for (int step = 0; step <= n; ++step) {
            int cand = (step & 1) ? at + step : at - step;
            at = cand;
            if (cand < 0) cand += n;
            else if (!(cand < n)) cand -= n;
            if (cand == 0 || cand < 0 || cand >= n) continue;
            if (F(p[size_t(cand - 1)].dist) <= s && s < F(p[size_t(cand)].dist)) {
                from = cand - 1, to = cand, kind = 3;
                break;
            }
        }
    }
    if (kind == 0) return false;
    const LinePoint &a = p[size_t(from)], &b = p[size_t(to)];
    const F ad(a.dist), bd(b.dist);
    F num, den;
    if (kind == 1) num = (s + lap) - ad, den = (bd + lap) - ad;
    else if (kind == 2) num = s - ad, den = (bd + lap) - ad;
    else num = s - ad, den = bd - ad;
    const F t = num / den;
    const F ax(a.x), ay(a.y), bx(b.x), by(b.y);
    if (a.kind == 0) {
        x = t * (bx - ax) + ax;
        y = t * (by - ay) + ay;
    } else {
        const F hd(a.heading);
        const F sn = -gt3r::ee::sin(hd);
        const F cs = gt3r::ee::cos(hd);
        F r(a.radius);
        const F cx = ax - cs * r, cy = ay + sn * r;
        const F a0 = ai::atan2(cx - ax, ay - cy);
        F a1 = ai::atan2(cx - bx, by - cy);
        F from_ang = a0, d;
        if (k::zero < r) {
            if (a1 < from_ang) a1 = a1 + k::two_pi;
            d = a1 - from_ang;
        } else {
            r = -r;
            if (from_ang < a1) from_ang = from_ang + k::two_pi;
            d = a1 - from_ang;
        }
        const F ang = from_ang + t * d;
        x = cx - gt3r::ee::sin(ang) * r;
        y = cy + gt3r::ee::cos(ang) * r;
    }
    const F ah(a.height), bh(b.height);
    h = t * (bh - ah) + ah;
    return true;
}

F Field::target(const Driver& d, int line, F look, F& ox, F& oy) const {
    const ex::Car& c = session_->record(d.car);
    const F s = course_s(d.car);
    F x = c.pos.x, y = c.pos.y, h = c.pos.z, dx, dy;
    for (int i = 0; i < 10;) {
        place(line, d.next, s + look, x, y, h);
        dx = x - c.pos.x, dy = y - c.pos.y;
        if (k::five <= gt3r::ee::sqrt(dx * dx + dy * dy)) break;
        if (i < 9) look = look + k::one;
        ++i;
    }
    ox = dx, oy = dy;
    return look;
}

F Field::line_heading(const Driver& d, int line, F s) const {
    const ex::Car& c = session_->record(d.car);
    F x0 = c.pos.x, y0 = c.pos.y, h0 = c.pos.z;
    place(line, d.next, s, x0, y0, h0);
    F x1 = x0, y1 = y0, h1 = h0, dx, dy;
    for (int i = 0; i < 10; ++i) {
        s = s + k::half;
        place(line, d.next, s, x1, y1, h1);
        dx = x1 - x0, dy = y1 - y0;
        if (F::raw(0x3E4CCCCC) <= gt3r::ee::sqrt(dx * dx + dy * dy)) break;
    }
    return ai::atan2(-dx, dy);
}

// 0x464770 (0x4646F8): throttle and brake towards a speed.
void Field::speed_control(Driver& d, F vt, F gain) {
    const ex::Car& c = session_->record(d.car);
    const F v = c.speed;
    F dv = v - (vt - k::kmh2);
    if (dv < -k::kmh10) dv = -k::kmh10;
    else if (k::kmh10 < dv) dv = k::kmh10;
    F thr = F::raw(0x3F400000) + gain * dv;
    if (k::one < thr) thr = k::one;
    else if (thr < k::zero) thr = k::zero;
    d.throttle = thr;
    if (!(d.mode == 3 || d.mode == 4) && !(d.kind >= 2 && d.kind <= 4)) vt = vt + k::kmh20;
    if (thr == k::zero && vt < v) d.brake = k::one;
    if (thr != k::one && thr != k::zero) d.shift_lock = 12;
}

// 0x464BB0: the steering slews to its target at the set's rate.
void Field::steer_to(Driver& d, F t) {
    const F step = d.p.steer_rate * session_->record(d.car).dt;
    const F s = d.steer;
    if (s < t) {
        const F n = s + step;
        d.steer = t < n ? t : n;
    } else if (t < s) {
        const F n = s - step;
        d.steer = n < t ? t : n;
    }
}

// 0x3F76E8: the throttle that holds a speed: the road's load (F +0x5BC: rolling, drag, the slope's pull) and the engine's
// friction over its full torque in the gear, less 1 per 5 km/h over the speed (+ 0.5 km/h).
F Field::hold_speed(const Driver& d, F vt) const {
    const ex::Car& c = session_->record(d.car);
    vt = vt + F::raw(0x3E0E38E3);
    const F v = c.gear == 0 ? -c.speed : c.speed;
    const F sum = c.torque_wot + c.torque_friction;
    if (sum <= F()) return kOne;
    const F load = -((c.rolling + c.drag) + (-((c.ground_fwd.z * c.p.mass) * F::raw(0x411CCCCC))));
    const F over = (v - vt) / k::kmh5;
    const F ratio = c.p.d.gear_speed[std::min<size_t>(c.gear, 7)];
    F t = (load + c.torque_friction * ratio) / (sum * ratio) - over;
    if (kOne < t) return kOne;
    return t < F() ? F() : t;
}

// 0x464C38: a driver's kind; the stop driver's count of 120 steps from kind 6, or kind 5 but after kind 4.
void Field::set_kind(Driver& d, int kind) {
    const int was = d.kind;
    d.kind = kind;
    if (kind == 6 || (kind < 7 && kind == 5 && was != 4)) d.stop_count = 120;
}

// 0x464C80: the stop driver: the steering to `steer`, no throttle; stopped (under 0.25 km/h, turning under 3 degrees a
// second) the brake off and kind 8; else kind 6 holds a speed falling by 1 km/h a step through its count, the brake coming
// on over its last 30 steps (0.8 after), any other kind brakes at half.
void Field::stop(Driver& d, F steer) {
    const ex::Car& c = session_->record(d.car);
    d.steer = c.steer_cmd, d.throttle = F(), d.brake = F(), d.reverse = false, d.shift_lock = 0;
    if (tables_.tuning.blend_by_distance == 0 && k::zero < d.blend) {
        const F b = d.blend - c.dt;
        d.blend = b < k::zero ? k::zero : b;
    }
    steer_to(d, steer);
    d.throttle = F();
    const F v = gt3r::ee::sqrt((c.vel.x * c.vel.x + c.vel.y * c.vel.y) + c.vel.z * c.vel.z);
    const F w = gt3r::ee::sqrt((c.yaw_rate * c.yaw_rate + c.roll_rate * c.roll_rate) + c.pitch_rate * c.pitch_rate);
    if (v < F::raw(0x3D8E38E3) && w < F::raw(0x3D56774E)) {
        d.brake = F();
        if (d.control != 4) d.kind = 8;
        return;
    }
    if (d.kind != 6) {
        d.brake = k::half;
        return;
    }
    if (d.stop_count == 0) {
        d.brake = F::raw(0x3F4CCCCC);
        return;
    }
    d.throttle = hold_speed(d, c.speed - k::kmh1);
    if (d.stop_count < 30) {
        const F f = int_f(d.stop_count) * F::raw(0x3D088888);
        d.throttle = d.throttle * f;
        d.brake = (kOne - f) * F::raw(0x3F4CCCCC);
    }
    --d.stop_count;
}

void Field::finish(size_t car, int mode, bool player_car) {
    if (car >= drivers_.size() || !drivers_[car]) return;
    Driver& d = *drivers_[car];
    if (d.kind >= 2 && d.kind <= 4) return; // (on the pit lane: kept)
    if (player_car) d.control = 0;
    if (mode == 0) {
        // 0x3452F0: the computer drives on (a pad car of kind 0 too), off the passing line 7.
        if (d.line == 7) request_line(d, CourseLines::kMain, true);
        d.kind = 1;
    } else if (mode == 1 || mode == 3) {
        if (d.line == 7) request_line(d, CourseLines::kMain, true);
        set_kind(d, mode == 3 ? 5 : 6);
    } else set_kind(d, 7);
}

int Field::kind(size_t car) const { return car < drivers_.size() && drivers_[car] ? drivers_[car]->kind : 0; }
int Field::control(size_t car) const { return car < drivers_.size() && drivers_[car] ? drivers_[car]->control : -1; }

bool Field::pit_enter(size_t car) {
    Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    if (!d || d->control == 4 || !request_line(*d, CourseLines::kPitLane, true)) return false;
    d->kind = 2;
    return true;
}
bool Field::pit_stop(size_t car) {
    Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    if (!d || d->control == 4 || !request_line(*d, CourseLines::kPitStop, true)) return false;
    d->kind = 3;
    return true;
}
bool Field::pit_leave(size_t car) {
    Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    if (!d || d->control == 4 || !request_line(*d, CourseLines::kPitLane, true)) return false;
    d->kind = 4;
    return true;
}
bool Field::pit_return(size_t car) {
    Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    if (!d) return false;
    if (d->control == 0) { // a pad car: onto line 7 (its fallback), the computer's kind cleared
        if (request_line(*d, 7, false)) d->kind = 0;
        return true;
    }
    if (d->kind < 2 || d->kind > 4) return false;
    if (request_line(*d, CourseLines::kMain, false)) d->kind = 0;
    return true;
}

// ---- traffic ---------------------------------------------------------------------------------------------------

// 0x4694F8: the zone cells around a car from another at (u ahead, v right), `dist` apart.
void Field::zones(Near& z, F dist, F u, F v, F ahead, F behind, F w) const {
    auto at_least2 = [&](int cell) {
        if (gt3r::s8(z.zone[size_t(cell)]) < 2) z.zone[size_t(cell)] = 2;
    };
    auto at_least1 = [&](int cell) {
        if (!(gt3r::s8(z.zone[size_t(cell)]) > 0)) z.zone[size_t(cell)] = 1;
    };
    const F twenty = F::raw(0x41A00000);
    if (twenty < dist) return;
    const F lim = w + F::raw(0x40400000);
    if (v < -lim || lim < v) return;
    if (ahead * F::raw(0x3F666666) < u) {
        const F w75 = w * F::raw(0x3F400000), gap = u - ahead;
        auto side = [&](int cell) {
            if (gap < k::five) at_least2(cell);
            else if (gap < twenty) at_least1(cell);
        };
        if (v < -w75) return side(kFrontLeft);
        if (w75 < v) return side(kFrontRight);
        if (gap < twenty) at_least2(kFront);
        if (v < -(w * k::half)) side(kFrontLeft);
        if (w * k::half < v) side(kFrontRight);
        return;
    }
    if (u < -(behind * F::raw(0x3F666666))) {
        if (v < -w) return at_least2(kRearLeft);
        if (w < v) return at_least2(kRearRight);
        at_least2(kRear);
        if (k::zero < v) at_least1(kRearRight);
        else at_least1(kRearLeft);
        return;
    }
    if (v < k::zero) {
        const F g = -v - w;
        if (g < z.gap_left) z.gap_left = g;
        at_least2(kLeft);
        if (ahead * k::half < u) at_least1(kFrontLeft);
        else if (u < -(behind * k::half)) at_least1(kRearLeft);
    } else {
        const F g = v - w;
        if (g < z.gap_right) z.gap_right = g;
        at_least2(kRight);
        if (ahead * k::half < u) at_least1(kFrontRight);
        else if (u < -(behind * k::half)) at_least1(kRearRight);
    }
}

// 0x3F42D0: car a closing on car b ahead along the course: the gap less the two cars' lengths and the closing speed;
// false when b is not ahead (or, within 20 m, not overlapping a sideways).
bool Field::closing(size_t a, size_t b, F v, F& gap, F& rate) const {
    const ex::Car &ca = session_->record(a), &cb = session_->record(b);
    const F lap(lines_.lap()), sa = course_s(a), sb = course_s(b);
    F g = lap * F::raw(0x3F400000) < sa && sb < lap * F::raw(0x3E800000) ? (sb + lap) - sa : sb - sa;
    g = g - cb.p.body_rear;
    const F gp = g - ca.p.body_front;
    if (gp <= k::zero) return false;
    const F rb = rate_[b], ra = rate_[a];
    const F diff = rb - ra;
    if (gp < F::raw(0x41A00000)) {
        const F h = (ca.p.body_width + cb.p.body_width) * k::half;
        if (h < v || v < -h) return false;
    }
    gap = gp, rate = -diff;
    return true;
}

// 0x469918: a's contact with b ahead (a car to follow when closing within 5 m).
void Field::contact(size_t a, size_t b, F gap, F rate) {
    Near& z = near_[a];
    if (z.follow == int(b) && F::raw(0x40C00000) < gap) z.follow = -1;
    F cand;
    int follow = -1;
    if (z.follow != int(b) && k::zero < rate && gap < k::five) cand = k::two, follow = int(b);
    else if (k::kmh20 < rate) cand = k::one;
    else if (F::raw(0x3CE38E37) < rate) {
        if (gap / rate < k::five) cand = k::half;
    }
    if (cand == k::zero) {
        if (gap < k::half) cand = F::raw(0x3D4CCCCC);
        else if (gap < F::raw(0x40800000)) cand = F::raw(0x3DCCCCCC);
    }
    if (z.contact < cand) {
        if (follow != -1) z.follow = follow, cand = k::one;
        z.contact = cand;
    }
}

// 0x3F4638: every pair of cars (0x3F40F8: both in the race, both on or both off the pit lane, within 100 m and a body's
// height + 5 m of each other) marks the zone cells and the contacts; then each car's follow count.
void Field::proximity() {
    const size_t n = near_.size();
    for (Near& z : near_) z.contact = F(), z.zone = {}, z.gap_left = z.gap_right = F(1000.0f), z.chain = 0;
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            const ex::Car &a = session_->record(i), &b = session_->record(j);
            if (a.ghost || b.ghost) continue;
            if ((drivers_[i] && drivers_[i]->line == CourseLines::kPitStop) || (drivers_[j] && drivers_[j]->line == CourseLines::kPitStop)) continue;
            if ((a.x535 & 0x10) != (b.x535 & 0x10)) continue;
            const F dx = b.pos.x - a.pos.x, dy = b.pos.y - a.pos.y, dz = b.pos.z - a.pos.z;
            const F dist = gt3r::ee::sqrt(dx * dx + dy * dy + dz * dz);
            if (F::raw(0x42C80000) < dist) continue;
            if (dz < -(b.p.body_height + k::five)) continue;
            if (a.p.body_height + k::five < dz) continue;
            F ua, va, ub, vb;
            ua = ua + a.fwd.x * dx, va = va + a.lat.x * dx, ub = ub - b.fwd.x * dx, vb = vb - b.lat.x * dx;
            ua = ua + a.fwd.y * dy, va = va + a.lat.y * dy, ub = ub - b.fwd.y * dy, vb = vb - b.lat.y * dy;
            ua = ua + a.fwd.z * dz, va = va + a.lat.z * dz, ub = ub - b.fwd.z * dz, vb = vb - b.lat.z * dz;
            const F w = (a.p.body_width + b.p.body_width) * k::half;
            const F ab = a.p.body_front + b.p.body_rear, ba = a.p.body_rear + b.p.body_front;
            zones(near_[i], dist, ua, va, ab, ba, w);
            zones(near_[j], dist, ub, vb, ba, ab, w);
            F gap, rate;
            if (pace_.contact) {
                if (closing(i, j, va, gap, rate)) contact(i, j, gap, rate);
                else if (near_[i].follow == int(j)) near_[i].follow = -1;
                if (closing(j, i, vb, gap, rate)) contact(j, i, gap, rate);
                else if (near_[j].follow == int(i)) near_[j].follow = -1;
            } else {
                if (near_[i].follow == int(j)) near_[i].follow = -1;
                if (near_[j].follow == int(i)) near_[j].follow = -1;
            }
        }
    // Each car's +0x6A: the length of its follow chain (the car it follows, the car that one follows, ..., at most n).
    for (size_t i = 0; i < n; ++i) {
        int len = 0, k = near_[i].follow;
        while (size_t(len) < n && k != -1) ++len, k = near_[size_t(k)].follow;
        near_[i].chain = len;
    }
}

// 0x469AE8: lane changes, the lift behind a car, the return to the main line.
void Field::traffic(Driver& d) {
    Near& z = near_[d.car];
    if (d.cooldown > 0) d.cooldown = s16(u16(d.cooldown) - 1);
    if (d.line == CourseLines::kPitStop) return;
    int lanes = 1;
    const ex::Car& c = session_->record(d.car);
    if (d.line == CourseLines::kPitLane) {
        if ((z.zone[kLeft] && z.zone[kFrontLeft]) || (z.zone[kRight] && z.zone[kFrontRight])) {
            if (c.speed < k::kmh30) d.cooldown = s16(u16(d.cooldown) + 1);
            else d.throttle = d.throttle * k::half;
        }
        lanes = 0;
    }
    if (d.status == 1 || d.status == 2 || d.status == 5) lanes = 0;
    else if ((d.status == 3 || d.status == 4) && (d.line == 1 || d.line == 2)) lanes = -1;
    F cn = d.cooldown > 0 ? F() : z.contact;
    if (k::zero < d.blend) lanes = 0;
    if (d.control == 1 && !d.overtake && lanes == 1) lanes = -1;
    if (lanes > 0 && k::zero < cn) {
        // The farthest line on each side: one beside, then the one beside that if there is one.
        int L = lines_.left(d.line), R = lines_.right(d.line);
        if (const int l2 = lines_.left(L); l2 >= 0) L = l2;
        if (const int r2 = lines_.right(R); r2 >= 0) R = r2;
        int pick = -1;
        if (d.next_kind == 1) pick = L >= 0 ? L : (R >= 0 && d.status != 4) ? R : -1;
        else if (d.next_kind == 2) pick = R >= 0 ? R : (L >= 0 && d.status != 3) ? L : -1;
        else pick = (L >= 0 && d.status != 3) ? L : (R >= 0 && d.status != 4) ? R : -1;
        if (pick >= 0) {
            request_line(d, pick, false);
            d.cooldown = 180;
            cn = F();
        }
    }
    d.throttle = d.throttle * (k::one - cn);
    if (lanes == 0) return;
    const int around = gt3r::s8(z.zone[kFront]) + gt3r::s8(z.zone[kRight]) + gt3r::s8(z.zone[kFrontRight]) + gt3r::s8(z.zone[kFrontLeft]) +
                       gt3r::s8(z.zone[kLeft]);
    if (around == 0 && d.line != CourseLines::kMain) request_line(d, CourseLines::kMain, false);
}

// ---- the decision (0x465078) -------------------------------------------------------------------------------------

void Field::decide(Driver& d) {
    const ex::Car& c = session_->record(d.car);
    const Near& z = near_[d.car];
    const Tuning& t = tables_.tuning;
    const bool holding = (d.control == 1 || d.control == 2) && c.race_hold != 0;
    const F kept = d.throttle;
    // 0x464ED8: the outputs cleared, the steering from the car's command, the blend run off by time.
    d.steer = c.steer_cmd, d.throttle = F(), d.brake = F(), d.reverse = false, d.shift_lock = 0;
    if (t.blend_by_distance == 0 && k::zero < d.blend) {
        const F b = d.blend - c.dt;
        d.blend = b < k::zero ? k::zero : b;
    }
    if (holding) d.throttle = kept;
    if (d.kind == 7) return stop(d, F());
    if (d.line < 0) return;
    const F scale = braking(d);
    const bool hit_front = d.hit_front, hit_rear = d.hit_rear;
    const F heading = c.heading;
    const F fwd_x = -gt3r::ee::sin(heading), fwd_y = gt3r::ee::cos(heading);
    const F v = c.speed;

    // The look ahead.
    F look = k::five;
    if (d.kind == 3) look = k::five;
    else if (d.kind == 4 && d.pit) look = v < k::kmh30 ? k::five : F::raw(0x41200000);
    else if (d.state != 0) look = d.state == 1 ? F::raw(0x41200000) : F::raw(0x42200000);
    else {
        // 0x464E88
        if (v <= k::zero) look = d.p.look_min;
        else {
            const F f = d.p.look_speed * v;
            look = k::one <= f ? d.p.look_max : d.p.look_min + f * (d.p.look_max - d.p.look_min);
        }
        const bool long_start = d.start_flag == 0;
        if (!long_start && k::kmh30 <= v) d.start_flag = 1;
        const s32 ms = s32(clock_ / 3), delay = d.index < 32 ? pace_.start_delay[size_t(d.index)] : 0;
        const s32 left = (long_start ? 5000 : 2000) - ms + 1000 * delay;
        if (left > 0) {
            if (long_start) look = look + (int_f(left) * F::raw(0x43480000)) / F::raw(0x459C4000);
            else look = look + (int_f(left) * F::raw(0x42200000)) / F::raw(0x44FA0000);
        }
        if (d.stop_count) look = look + int_f(d.stop_count) * F::raw(0x3EAAAAAA);
    }
    d.look = look.host();
    F ax, ay;
    look = target(d, d.line, look, ax, ay);
    if (k::zero < d.blend) {
        F ox, oy;
        target(d, d.prev_line, look, ox, oy);
        if (t.blend_by_distance == 1) {
            const F dx = ax - ox, dy = ay - oy;
            const F step = t.blend_rate * c.dt;
            const F gap = gt3r::ee::sqrt(dx * dx + dy * dy);
            if (gap < step) d.blend = F();
            else {
                const F b = d.blend - step / gap;
                d.blend = b < k::zero ? k::zero : b;
            }
        }
        ax = ax + (ox - ax) * d.blend;
        ay = ay + (oy - ay) * d.blend;
    }
    // The aim's sideways offset (T[5]).
    if (t.offset_flags && d.state == 0) {
        F o;
        if (t.offset_flags & 1) {
            F px, py;
            target(d, d.line, F(), px, py);
            if (d.blend != k::zero) {
                F qx, qy;
                target(d, d.prev_line, F(), qx, qy);
                px = px + (qx - px) * d.blend;
                py = py + (qy - py) * d.blend;
            }
            const F e = gt3r::ee::cos(heading) * px + gt3r::ee::sin(heading) * py;
            const F share = d.line == CourseLines::kPitLane || d.line == CourseLines::kPitStop ? t.side_share_pit : t.side_share;
            o = share * e + o;
        }
        if ((t.offset_flags & 2) && d.line != CourseLines::kPitStop) {
            F a;
            if (z.gap_left < k::two) a = (k::two - z.gap_left) + a;
            if (z.gap_right < k::two) a = a - (k::two - z.gap_right);
            o = o + a;
        }
        if (!session_->loose() && (t.offset_flags & 4) && (d.mode == 0 || d.status == 0)) {
            int left = 0, right = 0;
            for (int w = 0; w < int(d.lay_raw[1]) && w < 4; ++w)
                if (soft_[c.g[size_t(w)].material & 31]) {
                    if (d.lay_raw[0x1C + size_t(w)] == 0) left = 1;
                    else if (d.lay_raw[0x1C + size_t(w)] == 1) right = 1;
                }
            if (left - right) o = o + int_f(left - right) * k::half;
        }
        if (t.offset_by_grip) {
            F a = c.lat_g;
            if (a < k::zero) a = -a;
            F lim;
            if (t.grip_hi <= a) lim = F();
            else if (t.grip_lo < a) lim = (t.grip_hi - a) / (t.grip_hi - t.grip_lo);
            else lim = t.offset_limit;
            if (o < -lim) o = -lim;
            else if (lim < o) o = lim;
        }
        const F step = t.offset_rate * c.dt, cur = d.offset;
        if (cur < o) {
            const F nx = cur + step;
            d.offset = o < nx ? o : nx;
        } else if (o < cur) {
            const F nx = cur - step;
            d.offset = nx < o ? o : nx;
        }
        ax = ax + d.offset * c.lat.x;
        ay = ay + d.offset * c.lat.y;
    }
    d.aim_x = c.pos.x + ax, d.aim_y = c.pos.y + ay;
    if (d.control == 0 && (d.kind == 0 || c.hold != 0)) return;
    if (holding) return;

    const F eps = angle_between(ai::atan2(-ax, ay), heading);
    const F aeps = eps < k::zero ? -eps : eps;
    d.eps = eps;

    // The drive decision.
    F theta = k::deg60;
    int drive = 0;
    bool brakeflag = false;
    if (d.state == 1) {
        if (k::zero <= v) theta = k::deg10;
        else if (-k::kmh30 < v) theta = k::deg10 - (v * k::deg50) / k::kmh30;
        drive = -1;
    } else if (aeps < theta || d.state == 2) {
        if (-k::kmh2 < v) {
            drive = 1;
            if (v < k::zero && c.clutch == 0) brakeflag = true;
        } else brakeflag = true;
    } else {
        if (k::kmh2 < v) brakeflag = true;
        else drive = -1;
    }
    // The recovery state machine, resting 180 steps after a change.
    if (d.state_hold != 0) --d.state_hold;
    else {
        const int before = d.state;
        if (before == 1) {
            if (aeps < theta) d.state = k::kmh3 < v ? 0 : 2;
            else if (hit_rear) d.state = 2;
        } else if (before == 2) {
            if (aeps < k::deg40 && k::kmh30 < v) d.state = 0;
            else if (hit_front) {
                F dh = angle_between(line_heading(d, d.line, course_s(d.car)), heading);
                if (dh < k::zero) dh = -dh;
                if (k::deg60 < dh) d.state = 1;
                else if (v < k::kmh1 && -k::kmh1 < v) d.state = 1;
                else if (k::deg60 < aeps) d.state = 1;
            }
        } else {
            if (theta <= aeps) d.state = 2;
            else if (hit_front && v < k::kmh1 && -k::kmh1 < v) d.state = 1;
            else if (hit_rear && k::deg45 < aeps) d.state = 2;
        }
        if (d.state != before) d.state_hold = 180;
    }
    const bool slew = t.steer_mode < 2 || d.state != 0; // (T[2] of 2 or more: a PD steering in state 0, not ported)
    const int moving = k::kmh2 < v ? 1 : v < -k::kmh2 ? -1 : 0;

    // Steering. `over`: how far past the steered wheels' peak slip angle the yaw-rate steering asked to go (its share).
    F tgt, over;
    if (moving != 0) {
        if (drive == 1) {
            // The steered wheels' (layout +0x3C above 0) mean peak slip angle and slip direction.
            F amax, dir_sum;
            int steered = 0;
            for (int w = 0; w < int(d.lay_raw[1]) && w < 4; ++w)
                if (gt3r::s8(d.lay_raw[0x3C + size_t(w)]) > 0) {
                    amax = amax + tire_view(*session_, tables_.known, d.car, d.lay, w, c.w[size_t(w)].surface).peak_slip;
                    dir_sum = dir_sum + c.w[size_t(w)].slip_dir;
                    ++steered;
                }
            F dir;
            if (steered) amax = amax / int_f(steered), dir = dir + dir_sum / int_f(steered);
            if (d.state != 0) {
                if (k::deg80 < eps) tgt = amax;
                else if (eps < -k::deg80) tgt = -amax;
            }
            if (tgt == k::zero) {
                const F vx = c.vel.x, vy = c.vel.y;
                const F vxy = gt3r::ee::sqrt(vx * vx + vy * vy);
                const F p2 = ax * ax + ay * ay;
                F cx = fwd_x, cy = fwd_y;
                if (!(vxy < k::kmh5)) {
                    const F a = ai::atan2(-vx, vy);
                    cx = -gt3r::ee::sin(a), cy = gt3r::ee::cos(a);
                }
                F rd = ((vxy * F::raw(0xC0000000)) * (cy * ax - cx * ay)) / p2;
                if (c.speed < k::zero) rd = -rd;
                const F r = c.yaw_rate;
                F ky = d.p.yaw_gain;
                if (t.steer_mode != 0) {
                    F q = (-(session_->extra(d.car).tires.mean_slip - amax)) / amax;
                    if (k::one < q) q = k::one;
                    if (q < -k::one) q = -k::one;
                    ky = ky * q;
                }
                if (k::zero < rd) {
                    if (r < rd) {
                        tgt = tgt + (rd - r) * ky;
                        if (amax < tgt) {
                            over = tgt / amax;
                            if (!session_->loose()) tgt = amax;
                        }
                    }
                } else if (rd < k::zero) {
                    if (rd < r) {
                        tgt = tgt - (r - rd) * ky;
                        if (tgt < -amax) {
                            over = (-tgt) / amax;
                            if (!session_->loose()) tgt = -amax;
                        }
                    }
                }
            }
            tgt = tgt + dir;
            if (k::deg45 < tgt) tgt = k::deg45;
            else if (tgt < -k::deg45) tgt = -k::deg45;
            if (moving == -1) tgt = -tgt;
        } else {
            tgt = eps < -k::deg20 ? -k::deg30 : k::deg20 < eps ? k::deg30 : eps * F::raw(0x3FBFFFFE);
            if (moving == -1) tgt = -tgt;
        }
    }
    const F lock = c.p.max_lock;
    if (lock < tgt) tgt = lock;
    else if (tgt < -lock) tgt = -lock;
    if (d.kind == 6) return stop(d, tgt);
    if ((d.control == 1 || d.control == 2 || d.kind != 0) && slew) steer_to(d, tgt);

    // Throttle and brake.
    if (drive == 1) {
        if (brakeflag) d.brake = F::raw(0x3DB851EB);
        d.pit = 0;
        F aim = d.v_cur, next = d.v_next;
        int mode = d.mode;
        if (d.line == CourseLines::kPitStop) aim = next = k::kmh25, mode = 3, d.pit = 1;
        else if (d.line == CourseLines::kPitLane) {
            // Along the pit lane at the set's pit speed between the stop line's second and its fourth point from the end.
            const std::vector<LinePoint>& stop = lines_.line(CourseLines::kPitStop);
            const F lap(lines_.lap());
            F s = course_s(d.car);
            if (s < lap * k::half) s = s + lap;
            const int sn = int(stop.size());
            if (sn >= 4 && !(s < F(stop[1].dist)) && !(s < F(stop[2].dist)) && s < F(stop[size_t(sn - 3)].dist)) {
                mode = 1;
                aim = d.p.pit_speed + k::kmh2;
                d.pit = 1;
                if (d.v_cur < aim) d.pit = 0, aim = d.v_cur, mode = d.mode;
                next = d.p.pit_speed + k::kmh2;
                if (d.v_next < next) next = d.v_next;
            }
        } else if (z.follow >= 0 && d.cooldown < 120) {
            F vo = session_->record(size_t(z.follow)).speed;
            if (d.status <= 0 || d.status >= 6) vo = vo - k::kmh5;
            else if (d.status < 3) vo = vo - k::kmh20;
            else vo = (vo - k::kmh10) - k::kmh5;
            if (vo < k::kmh30) vo = k::kmh30;
            mode = 4;
            if (vo < next) next = vo;
        }
        if (d.state == 2) {
            const bool pit_lines = d.line == CourseLines::kPitStop || d.line == CourseLines::kPitLane;
            speed_control(d, pit_lines ? aim : k::kmh50, -k::half);
            d.arc_ticks = 0;
        } else if (mode == 0) {
            d.throttle = k::one, d.arc_ticks = 0;
        } else if (mode == 3) {
            speed_control(d, next, -k::two), d.arc_ticks = 0;
        } else if (mode == 4) {
            speed_control(d, next, -k::one), ++d.arc_ticks;
        } else if (mode == 1 || mode == 2) {
            F tv = aim;
            if (!(c.x535 & 0x10) && !(int(d.arc_ticks) < d.p.bonus_ticks)) {
                tv = tv + d.p.corner_bonus;
                if (d.p.straight_bonus && aeps < d.p.bonus_angle) tv = tv + (d.p.bonus_speed * (d.p.bonus_angle - aeps)) / d.p.bonus_angle;
            }
            speed_control(d, tv, -k::half);
            if (d.status == 0) {
                F grip;
                const int count = int(d.lay_raw[1]);
                for (int w = 0; w < count && w < 4; ++w) grip = grip + tire_view(*session_, tables_.known, d.car, d.lay, w, c.w[size_t(w)].surface).grip;
                F g = lat_x_[d.car] / (grip / int_f(count));
                if (g < k::zero) g = -g;
                if (F::raw(0x3F666666) < g) d.status = 5;
                else if (k::half < g) d.status = mode == 1 ? 4 : 3;
            }
            ++d.arc_ticks;
        }
        if (!(d.arc_ticks < 6001)) d.arc_ticks = 6000;
        d.throttle = d.throttle * scale;
        int rank = ranks_[d.car];
        session_->rank(d.car, rank);
        if (5000 + 100 * rank < s32(clock_ / 3)) traffic(d);
    } else {
        if (drive == -1) d.reverse = true, d.throttle = k::one;
        else d.throttle = F();
        d.brake = brakeflag ? k::one : F();
        d.arc_ticks = 0;
    }
    // Past the tires' peak: the throttle cut and the brake by the set's shares (0x620DE7 set: the car's own measure of it,
    // F +0x470, not mirrored; clear in this program, so the steering's overshoot serves).
    if (k::zero < over && c.gear >= 2) {
        const F e = tables_.saturation_cut == 0 ? over - k::one : over; // (the car's own measure taken as it is)
        F f = k::one - d.p.sat_throttle * e;
        if (f < k::zero) f = k::zero;
        d.throttle = d.throttle * f;
        const F b = d.brake + e * d.p.sat_brake;
        d.brake = k::one < b ? k::one : b;
    }
    // Kind 5 (a run over): the throttle held to the race's limit (0x3F76E8).
    if (d.kind == 5) {
        u8 lim = d.control == 2 ? limit_computer_ : limit_player_;
        if (!(lim < 0x51)) lim = 0;
        F vt = lim ? int_f(lim) * kPerKmh : F::raw(0x41B1C71B), share = kOne;
        if (d.stop_count) {
            // Without a limit: a speed falling by 1 km/h a step, its share over the count's last 30 steps.
            if (!lim) {
                vt = c.speed - k::kmh1;
                if (d.stop_count < 30) share = int_f(d.stop_count) * F::raw(0x3D088888);
            }
            --d.stop_count;
        }
        const F h = hold_speed(d, vt) * share;
        if (h < d.throttle) d.throttle = h;
    }
}

// The rubber band (the conductor's virtual 4, 0x365790, with 0x365910, 0x365688 and 0x365718, in the program's arithmetic):
// from the reference car (the first of the order the field does not drive; none: car 0) outwards along the order, each car
// against the car between it and the reference, the factors chained: a car ahead slowed by up to the slowing's share past
// its start gap, fully past its full gap or a lap ahead; a car behind hurried the same way (not on the pit lane); a finished
// car keeps the chain's factor; each at most 1.3, times the overall factor.
void Field::rubber_band() {
    const Cars& cars = *session_;
    const size_t n = cars.size();
    if (!band_.on || n == 0) return;
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[size_t(std::clamp(ranks_[i], 1, int(n))) - 1] = i;
    size_t ref = n;
    for (size_t pos = 0; pos < n && ref == n; ++pos)
        if (!drivers_[order[pos]]) ref = order[pos];
    if (ref == n) ref = 0;
    const F lap(lines_.lap()), one = kOne;
    auto scale = [&](size_t r, size_t car, F prev) {
        F s = one;
        if (!cars.finished(car)) {
            // 0x365910: the reference's lead in whole laps and metres, a lap taken over where the two disagree in sign.
            int laps = cars.state(r).lap - cars.state(car).lap;
            F gap = course_s(r) - course_s(car);
            if (laps > 0) {
                if (gap < F()) --laps, gap = gap + lap;
            } else if (laps < 0 && F() < gap) ++laps, gap = gap - lap;
            if (laps > 0 || (laps == 0 && F() < gap)) {
                if (band_.fast != F() && !(session_->record(car).x535 & 0x10)) {
                    if (laps > 0 || band_.fast_full <= gap) s = band_.fast + one;
                    else if (band_.fast_start < gap) s = band_.fast * ((gap - band_.fast_start) / (band_.fast_full - band_.fast_start)) + one;
                }
            } else if (band_.slow != F()) {
                const F g = -gap;
                if (-laps > 0 || band_.slow_full <= g) s = one - band_.slow;
                else if (band_.slow_start < g) s = one - band_.slow * ((g - band_.slow_start) / (band_.slow_full - band_.slow_start));
            }
        }
        s = s * prev;
        if (F::raw(0x3FA66666) < s) s = F::raw(0x3FA66666);
        scale_[car] = s * band_.global;
        return s;
    };
    const int r = std::clamp(ranks_[ref], 1, int(n));
    if (band_.mode == 1) {
        for (size_t i = 0; i < n; ++i)
            if (i != ref) scale(ref, i, one);
    } else if (band_.mode == 2) {
        for (int i = 0; i < r - 1; ++i) scale(order[size_t(i + 1)], order[size_t(i)], one);
        for (int i = r; i < int(n); ++i) scale(order[size_t(i - 1)], order[size_t(i)], one);
    } else if (band_.mode == 0) {
        const int j = r - 1;
        F s = one;
        for (int i = j; i > 0; --i) s = scale(order[size_t(i)], order[size_t(i - 1)], s);
        s = one;
        for (int i = j; i < int(n) - 1; ++i) s = scale(order[size_t(i)], order[size_t(i + 1)], s);
    }
}

// ---- the field -------------------------------------------------------------------------------------------------

void Field::hold(std::span<race::CarInput> inputs) {
    for (auto& dp : drivers_) {
        if (!dp || dp->car >= inputs.size()) continue;
        Driver& d = *dp;
        const ex::Car& c = session_->record(d.car);
        // 0x463CD8: a blip between 3/4 and 15/16 of the shift speed.
        const int up = c.p.d.up_rpm, rpm = c.rpm;
        if (d.blip == 0) {
            if (rpm < 3 * up / 4) d.blip = 1;
        } else if (rpm >= 15 * up / 16) d.blip = 0;
        d.applied_throttle = std::clamp(d.applied_throttle + (d.blip ? 4.0f : -4.0f) * kStep, 0.0f, 1.0f);
        race::CarInput in;
        in.throttle = d.applied_throttle;
        inputs[d.car] = in;
    }
}

void Field::incidents_after(Driver& d) {
    const race::CarState& st = session_->state(d.car);
    const ex::Car& c = session_->record(d.car);
    // Walls and cars against the body's ends (the collision step's front and rear flags are not public).
    const float fx = st.forward.x, fz = st.forward.z;
    const ex::Car& rc = c;
    const float front = rc.p.body_front.host(), rear = rc.p.body_rear.host(), half = rc.p.body_width.host() * 0.5f;
    d.hit_front = d.hit_rear = false;
    bool wall = false, car = false;
    const Vec3 cog{c.pos.x.host(), c.pos.z.host(), -c.pos.y.host()};
    RoadGround::WallHit wh;
    if (session_->ground().nearest_wall(cog, std::max(front, rear) + 0.3f, wh)) {
        const float along = (wh.point.x - cog.x) * fx + (wh.point.z - cog.z) * fz;
        const float across = std::fabs((wh.point.x - cog.x) * fz - (wh.point.z - cog.z) * fx);
        if (along > front - 0.4f) d.hit_front = true, wall = true;
        else if (along < -(rear - 0.4f)) d.hit_rear = true, wall = true;
        else if (across < half + 0.2f) wall = true;
    }
    const Cars& cars = *session_;
    for (size_t j = 0; j < cars.size(); ++j) {
        if (j == d.car) continue;
        const float dx = cars.state(j).position.x - st.position.x, dz = cars.state(j).position.z - st.position.z;
        const float along = dx * fx + dz * fz, across = std::fabs(dx * fz - dz * fx);
        const ex::Car& o = session_->record(j);
        if (across < half + o.p.body_width.host() * 0.5f - 0.1f && std::fabs(along) < front + o.p.body_rear.host()) {
            car = true;
            if (along > 0) d.hit_front = true;
            else d.hit_rear = true;
        }
    }
    // The collision step's own flags where the cars give them.
    if (bool hf, hr; session_->hits(d.car, hf, hr)) d.hit_front = hf, d.hit_rear = hr;
    if (wall && !d.was_wall) ++d.inc.wall_hits;
    if (car && !d.was_car) ++d.inc.car_hits;
    d.was_wall = wall, d.was_car = car;
    bool off = true;
    for (const race::WheelState& w : st.wheels)
        if (!w.on_ground || !soft_[w.material & 31]) off = false;
    if (off && !d.was_off) ++d.inc.off_road;
    d.was_off = off;
    if (d.state == 2 && d.prev_state != 2) ++d.inc.spins;
    d.prev_state = d.state;
    if (steps_ > 600 && std::fabs(st.speed) < 5 * kKmh) d.inc.stuck_seconds += kStep;
}

void Field::begin_step() {
    const Cars& cars = *session_;
    // The order by laps and distance (1 first).
    {
        std::vector<size_t> order(cars.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const double pa = double(cars.state(a).lap) * lines_.lap() + cars.state(a).course_v, pb = double(cars.state(b).lap) * lines_.lap() + cars.state(b).course_v;
            return pa > pb;
        });
        for (size_t r = 0; r < order.size(); ++r) ranks_[order[r]] = int(r + 1);
    }
    for (size_t i = 0; i < cars.size(); ++i) {
        if (int r; cars.rank(i, r)) ranks_[i] = r;
        // The filtered lateral acceleration (0x35DC08): a second-order follower of F +0x554 at 1/60 s.
        if (F x; cars.lat_filter(i, x)) lat_x_[i] = x;
        else {
            const F k60 = F::raw(0x3C888888);
            const F a = (cars.record(i).a_y - lat_x_[i]) * F::raw(0x43480000) - lat_v_[i] * F::raw(0x41A00000);
            lat_v_[i] = lat_v_[i] + a * k60;
            lat_x_[i] = lat_x_[i] + lat_v_[i] * k60;
        }
        // The speed along the course (0x358608: the distance's change x 60, across the line by a lap).
        const F s(cars.state(i).course_v);
        if (F r; cars.course_rate(i, r)) rate_[i] = r;
        else {
            const F lap(lines_.lap()), was = last_s_[i];
            if (was <= s) rate_[i] = (was + lap * kHalf < s ? s - (was + lap) : s - was) * F::raw(0x42700000);
            else rate_[i] = (s + lap * kHalf < was ? (s + lap) - was : s - was) * F::raw(0x42700000);
        }
        last_s_[i] = s;
    }
    if (u32 ck; cars.clock(ck)) clock_ = ck;
    for (auto& d : drivers_)
        if (d) d->s = lines_.wrap_lap(cars.state(d->car).course_v);
    rubber_band();
    proximity();
}

void Field::end_step() {
    ++steps_;
    clock_ = std::min<u32>(clock_ + 50, 0x405F7DFD);
}

void Field::decide_car(size_t car, race::CarInput& in) {
    if (car >= drivers_.size() || !drivers_[car]) return;
    Driver& d = *drivers_[car];
    incidents_after(d);
    decide(d);
    // The outputs into the controls (0x464F38): the throttle pedal slews to the wanted throttle by the step in the arcs.
    const ex::Car& c = session_->record(d.car);
    F thr = d.throttle;
    if (tables_.tuning.smooth_throttle == 0 && (d.mode == 1 || d.mode == 2)) {
        const F cur = c.throttle_pedal;
        if (cur < thr) {
            const F n = cur + c.dt;
            thr = thr < n ? thr : n;
        } else {
            const F n = cur - c.dt;
            thr = n < thr ? thr : n;
        }
    }
    d.applied_throttle = thr.host();
    in = race::CarInput{};
    // The steering as the pad's share of the lock it allows at the car's speed (what a pad reads), and the angle.
    const float steer = d.steer.host();
    const float hi = c.lock_hi.host() > 0.01f ? c.lock_hi.host() : c.p.max_lock.host();
    const float lo = c.lock_lo.host() < -0.01f ? -c.lock_lo.host() : c.p.max_lock.host();
    const float share = steer >= 0 ? steer / std::max(hi, 1e-3f) : steer / std::max(lo, 1e-3f);
    in.steer = std::clamp(-share, -1.0f, 1.0f);
    in.steer_angle = steer; // the session takes the angle itself (the command F +0x468), past the pad's lock curve
    in.throttle = d.applied_throttle;
    in.brake = d.brake.host();
    in.reverse = d.reverse;
    in.computer = true;
    in.shift_lock = u8(d.shift_lock);
}

void Field::drive(std::span<race::CarInput> inputs) {
    begin_step();
    for (auto& dp : drivers_)
        if (dp && dp->car < inputs.size()) decide_car(dp->car, inputs[dp->car]);
    // A place with a held start (missions: Pace::start_delay) stays on the line, its engine held as in the count, until
    // the race's clock passes its seconds (❓ judged: the program's hold itself is not found; its decision only lengthens
    // the start's look ahead by the delay).
    for (auto& dp : drivers_)
        if (dp && dp->car < inputs.size() && dp->index < 32 && pace_.start_delay[size_t(dp->index)] > 0 &&
            s32(clock_ / 3) < 1000 * pace_.start_delay[size_t(dp->index)]) {
            Driver& d = *dp;
            const ex::Car& c = session_->record(d.car);
            const int up = c.p.d.up_rpm, rpm = c.rpm;
            if (d.blip == 0) {
                if (rpm < 3 * up / 4) d.blip = 1;
            } else if (rpm >= 15 * up / 16) d.blip = 0;
            d.applied_throttle = std::clamp(d.applied_throttle + (d.blip ? 4.0f : -4.0f) * kStep, 0.0f, 1.0f);
            race::CarInput in;
            in.throttle = d.applied_throttle, in.brake = 1, in.computer = true;
            inputs[d.car] = in;
        }
    end_step();
}

} // namespace gt4r::sim::ai
