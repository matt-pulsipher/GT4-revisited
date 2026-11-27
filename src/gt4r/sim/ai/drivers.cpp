#include "gt4r/sim/ai/drivers.h"

#include "base/ee_math.h"
#include "gt4r/sim/ai/drivers_prologue.h"
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
    std::array<F, 5> pd_last{}; // +0x00 .. +0x10 the PD steering's terms at the last step
    F pd_rate;              // F +0x484 the PD steering's rate (its integrator)
    Suggestion suggest;     // +0x40 / +0x41 the suggested gear's speed (0x469E58)
    std::array<u8, 0x40> record{}; // car +0x8C: the B-spec driver's record of the race
    int car_class = 0;      // 0x468458 of the car's +0xFE0
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
    ex::Driver ai;          // Prologue's driver record (the fields above follow it)
    bool event_done = false; // (the event's percents on a pad's car its driver took over)
};

struct Field::PrologueDrivers {
    prologue::DriverTables tables;
    ex::CourseQuery query;
    ex::AiCourse course;
    ex::WearParams wear{};
    PrologueDrivers(const prologue::DriverTables& t, const gt3r::course::Road& road, const course::AiData& data)
        : tables(t), query(road), course(prologue::driver_course(query, data, t)) {}
};

struct Field::Near {
    std::array<u8, 8> zone{};
    F gap_left{1000.0f}, gap_right{1000.0f}; // +0x58 / +0x5C
    F contact;                               // +0x44
    int follow = -1;                         // +0x51
    int chain = 0;                           // +0x6A
    // The threat record (0x469888): the car closed on soonest, its gap and closing speed (+0x53 / +0x60 / +0x64).
    int threat = -1;
    F threat_gap{1000.0f}, threat_rate{-1000.0f};
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
    if (!pace_.random_spread) tables_.tuning.corner_spread = 0, tables_.tuning.brake_spread = 0;
    // (SCPS-17001 v1.05: the switches are set by the race's preparation, after every car is set up: off for the set-up's plans)
    spread_live_ = !tables_.japan;
    sets_ = data.sets;
    if (program.known())
        for (u32 m = 0; m < 24; ++m) soft_[m] = program.w(Program::kSoft + 4 * m);
    else
        soft_[2] = soft_[3] = soft_[4] = soft_[6] = 1;
    const size_t n = session.size();
    if (!program.known() && n > 0 && session.prologue_part(0) && session.ground().road().gtrw)
        if (const prologue::DriverTables t = prologue::driver_tables(program); t.found)
            prologue_ = std::make_unique<PrologueDrivers>(t, *session.ground().road().gtrw, data);
    if (prologue_ && session.wear_on()) prologue_->wear.max = kOne;
    drivers_.resize(n);
    ranks_.assign(n, 0);
    near_.assign(n, Near{});
    scale_.assign(n, kOne);
    // The band (0x365480): percents x 0.01, the start gaps in metres, the full gaps x 10 m, the overall percent (0: 100).
    {
        const Pace& p = pace_;
        Band b;
        // (0x365480 is asked by the conductor's virtual 12: 1 for the races, the pace car tests and the missions, 0 for a run
        // alone. Without percents it still runs: 100 x 0.01 is just below 1 in the console's arithmetic.) Prologue's: in every
        // race of several.
        b.on = program.known() ? !p.solo : p.slow_pct || p.fast_pct || (p.band_global && p.band_global != 100) || (prologue_ && n > 1);
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
            if (prologue_) with_record(*d, [&](ex::Car& c) { prologue::driver_start(c, prologue_step(*d), prologue_->tables, CourseLines::kMain); });
            else replan(*d);
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
    // 0x4673D8: the downforce term (P +0x106C + P +0x1070) / the weight P +0x103C; the aero bonus 0, or for the lift under the
    // body (P +0x1077 kind 7) its lift P +0x1084 x 0.8 / the weight.
    if (c.p.weight != F()) {
        d.p.downforce = (c.p.downforce_coef[0] + c.p.downforce_coef[1]) / c.p.weight;
        const rules::BodyExtra& b = session_->extra(d.car).body;
        // (SCPS-17001 v1.05's 0x45F1C8 makes no aero bonus: its block has no word for it)
        d.p.aero_bonus = b.speed_part_kind == 7 && !tables_.japan ? (b.speed_part_lift * F::raw(0x3F4CCCCC)) / c.p.weight : F();
    }
    // The event's changes (0x467148): each class's percents, then the drivers' spread by grid index.
    int thr = pace_.throttle[size_t(d.drive_class)], brk = pace_.brake[size_t(d.drive_class)], cs = pace_.corner[size_t(d.drive_class)];
    if (thr == 0 || thr > 100) thr = 100;
    if (brk == 0) brk = 100;
    if (cs == 0) cs = 100;
    int off = 0;
    if (tables_.driver_spread && d.control == 2) {
        const int idx = d.index;
        // (SCPS-17001 v1.05's car set-up 0x35E050 does not set the draws' switches, 0x469468: its single race's preparation,
        // the conductor's virtual 27, does, after every car is set up. So its drivers differ by their grid index too.)
        const bool japan = tables_.japan;
        if (!tables_.tuning.brake_spread || japan) brk -= (idx % 3) * 7;
        if (!tables_.tuning.corner_spread || japan) cs -= (idx * 4) & 4, off = -2 * ((idx * 7) % 5);
        d.p.yaw_gain = d.p.yaw_gain * (int_f(((idx + 1) % 3) * 3 + 97) * kHundredth);
    }
    if (thr != 100) d.p.throttle = int_f(thr) * kHundredth;
    if (brk != 100) d.brake_mult = d.brake_mult * (int_f(brk) * kHundredth);
    if (cs != 100) d.corner_mult = d.corner_mult * (int_f(cs) * kHundredth);
    if (off) d.p.corner_offset = d.p.corner_offset + int_f(off) * kPerKmh;
    // The pace level (0x3F1890): a computer driver's starting level, passing allowed but in a run alone.
    {
        const Tables::PaceLevel& l = tables_.pace[tables_.pace_start[1]];
        d.pace = tables_.pace_start[1], d.overtake = !pace_.solo, d.pace_brake = l.brake, d.pace_corner = l.corner, d.shift = l.shift;
    }
    // The random draws (0x4639E0): seeded by the grid index for a computer car, then one step.
    d.rand = seed(d);
    // 0x463AE0: the long look ahead (D +0x1A = 0) for a computer car from a standing start on a paved course.
    d.start_flag = (d.control == 1 || d.control == 2) && pace_.standing_start && !session_->loose() && c.speed < F::raw(0x41055554) ? pace_.start_look : 1;
    if (prologue_) prologue_setup(d);
}

// ---- Prologue's drivers ------------------------------------------------------------------------------------------

// `fn` on the car's record with the driver's own record, kind and control in it, what it leaves of them kept.
template <class Fn> void Field::with_record(Driver& d, Fn fn) {
    ex::Car c = session_->record(d.car);
    c.ai = d.ai;
    c.driver_kind = u8(d.kind), c.control = s8(d.control), c.blip = d.blip;
    fn(c);
    d.ai = c.ai;
    d.kind = c.driver_kind, d.blip = c.blip;
    const ex::Driver& a = d.ai;
    d.line = a.line, d.prev_line = a.prev_line, d.next = a.next, d.mode = a.mode, d.next_kind = a.next_kind, d.v_cur = a.v_cur, d.v_next = a.v_next;
    d.blend = a.blend, d.state = a.state, d.status = a.status, d.cooldown = a.cooldown, d.pit = a.pit_limit, d.arc_ticks = a.arc_ticks;
    d.steer = a.steer, d.throttle = a.throttle, d.brake = a.brake, d.reverse = a.reverse != 0, d.shift_lock = u16(a.shift_lock);
}

// The set-up's (0x3C93A0): the set, the event's percents for a car no pad drives, the downforce.
void Field::prologue_setup(Driver& d) {
    const auto& set = sets_[size_t(d.drive_class)];
    d.ai = {};
    prologue::driver_set(d.ai, set ? set->data() : prologue_->tables.builtin[session_->course_kind() == 1 ? 1 : session_->course_kind() == 2 ? 2 : 0].data());
    const size_t k = size_t(d.drive_class);
    if (d.control != 0) prologue::driver_event(d.ai, pace_.throttle[k], pace_.brake[k], pace_.corner[k], d.control, d.index);
    prologue::driver_downforce(d.ai, session_->record(d.car).p);
    d.p.aids = d.ai.aids;
}

prologue::Step Field::prologue_step(const Driver& d) const {
    prologue::Step s;
    s.course = &prologue_->course, s.tables = &prologue_->tables, s.wear = prologue_->wear;
    s.part = session_->prologue_part(d.car);
    return s;
}

void Field::prologue_decide(Driver& d, bool plans_only, race::CarInput& in) {
    prologue::Step s = prologue_step(d);
    s.start_count = s32(hold_count_), s.clock = clock_;
    s.hit_front = d.hit_front, s.hit_rear = d.hit_rear;
    s.dirt = session_->course_kind() == 2, s.start_speed = pace_.start_speed;
    d.ai.zone = session_->record(d.car).ai.zone, d.ai.contact = session_->record(d.car).ai.contact;
    with_record(d, [&](ex::Car& c) {
        if (plans_only) prologue::braking(c, s);
        else prologue::step(c, s);
    });
    in = race::CarInput{};
    in.steer_angle = d.steer.host();
    const ex::Car& c = session_->record(d.car);
    const float hi = c.lock_hi.host() > 0.01f ? c.lock_hi.host() : c.p.max_lock.host(), lo = c.lock_lo.host() < -0.01f ? -c.lock_lo.host() : c.p.max_lock.host();
    in.steer = std::clamp(-(in.steer_angle >= 0 ? in.steer_angle / std::max(hi, 1e-3f) : in.steer_angle / std::max(lo, 1e-3f)), -1.0f, 1.0f);
    const F thr = pedal(d); // (0x425DC0: slewed by the step in the arcs)
    d.applied_throttle = thr.host();
    in.throttle = d.applied_throttle, in.brake = d.brake.host(), in.reverse = d.reverse;
    in.computer = d.control != 0 || c.driver_kind != 1;
    in.control = u8(d.control == 1 ? 1 : 2);
    in.shift_lock = u8(d.shift_lock);
}

u16 Field::seed(const Driver& d) { return u16((((d.control == 2 ? u32(d.index) + 1 : 1u) * 0x41C64E6Du + 0x3039u) >> 16) & 0x7FFF); }

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

void Field::set_control(size_t car, int control) {
    Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    if (d) d->control = control, d->suggest = {}; // (0x469E48)
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
        // (0x3F19E0 command 5: passing turned off sends a driver on a passing line back to the main line; SCPS-17001 v1.05's
        // 0x3EA7E8 keeps the line)
        if (!d.overtake && !tables_.japan && d.kind == 0 && (d.line == 1 || d.line == 2)) request_line(d, CourseLines::kMain, false);
    }
}

int Field::pace_level(size_t car) const { return car < drivers_.size() && drivers_[car] ? drivers_[car]->pace : -1; }
bool Field::overtaking(size_t car) const { return car < drivers_.size() && drivers_[car] && drivers_[car]->overtake; }
u8 Field::shift_factor(size_t car) const {
    const Driver* d = driver(car);
    return d && d->control == 1 ? d->shift : 128; // (the up-shift speeds take it for kind 0 alone: the gearbox's own, 0x36BEE0)
}

void Field::apply_start_delays(Session& session) {
    for (const auto& dp : drivers_)
        if (dp && dp->index < 32 && pace_.start_delay[size_t(dp->index)] > 0 && pace_.standing_start)
            session.set_start_delay(dp->car, u32(60 * pace_.start_delay[size_t(dp->index)]));
}

void Field::apply_shift_factors(Session& session) const {
    for (size_t i = 0; i < session.cars().size(); ++i) {
        const Driver* d = driver(i);
        session.set_shift_factor(i, d ? d->shift : 128);
    }
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
    d.pd_last = p.pd_last, d.pd_rate = p.pd_rate;
    d.record = p.record, d.car_class = p.car_class;
    z.follow = p.follow, z.chain = p.chain, z.contact = p.contact, z.zone = p.zone, z.gap_left = p.gap_left, z.gap_right = p.gap_right;
    z.threat = p.threat, z.threat_gap = p.threat_gap, z.threat_rate = p.threat_rate;
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
    p.aim_x = d.aim_x, p.aim_y = d.aim_y;
    p.pd_last = d.pd_last, p.pd_rate = d.pd_rate;
    p.record = d.record, p.car_class = d.car_class;
    p.follow = z.follow, p.chain = z.chain, p.contact = z.contact, p.zone = z.zone, p.gap_left = z.gap_left, p.gap_right = z.gap_right;
    p.threat = z.threat, p.threat_gap = z.threat_gap, p.threat_rate = z.threat_rate;
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

void Field::set_marks(size_t car, bool pit_lane, bool tire_out) {
    if (marks_.size() <= car) marks_.resize(car + 1, {false, false});
    marks_[car] = {pit_lane, tire_out};
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
    bool lane = false, tire_out = false;
    if (!session_->marks(d.car, lane, tire_out) && d.car < marks_.size()) lane = marks_[d.car][0], tire_out = marks_[d.car][1];
    F w = d.p.front_weight;
    // (halfway to a half when the front's and the rear's paved grips differ, P +0x104A, or with a tire out, F +0x5C2)
    const auto& tires = session_->extra(d.car).tires.tire;
    if (tires[0].mu[0].bits != tires[3].mu[0].bits || tire_out) w = (w + kHalf) * kHalf;
    const F g = ai_grip(d, s);
    const F mu0 = ((w * rf.mu) * rf.grip) * g, mu1 = (((kOne - w) * rr.mu) * rr.grip) * g;
    F a = mu0 + mu1;
    // With the race's tire wear on, off the pit lane: each wheel's wear grip (+0x50) of its axle's, shared over the axle's wheels.
    if (session_->wear_on() && !lane) {
        a = F();
        const F mu[2] = {mu0, mu1};
        const int n = d.lay_raw[1] < 4 ? d.lay_raw[1] : 4;
        for (int k = 0; k < n; ++k) {
            const size_t axle = d.lay_raw[0x14 + size_t(k)] & 1;
            a = a + (c.w[size_t(k)].wear_grip * mu[axle]) / int_f(d.lay_raw[4 + axle]);
        }
    }
    if (tire_out) a = a * F::raw(0x3F666666);
    const F r(pt.radius), bank(pt.bank), zero;
    if (zero <= r) {
        a = a * r;
        if (bank < zero) a = a / (kOne + gt3r::ee::sin(bank));
    } else {
        a = a * (-r);
        if (zero < bank) a = a / (kOne - gt3r::ee::sin(bank));
    }
    // (SCPS-17001 v1.05's 0x45FA50: no aero bonus, and every driver's own multipliers, a pad car's too)
    const bool japan = session_->japan();
    const F e = pt.crest || japan ? zero : d.p.aero_bonus;
    F q = e + kOne, v;
    for (int k = 0; k < 3; ++k) {
        v = gt3r::ee::sqrt(a * q);
        q = ((d.p.downforce * v) * v + kOne) + e;
    }
    F m = d.control == 0 && !japan ? kOne : d.corner_mult;
    const Tuning& t = tables_.tuning;
    if (t.corner_spread && spread_live_ && (d.control == 1 || d.control == 2) && d.kind == 0) m = m + draw(d.rand, t.corner_lo.at(m), t.corner_hi.at(m));
    F scale = d.p.corner_scale * m;
    if (d.control == 1 && d.kind == 0) scale = scale * d.pace_corner;
    if (const auto& zc = near_[d.car].zone; t.alongside_slow && (zc[kFrontLeft] || zc[kLeft] || zc[kRight] || zc[kFrontRight]))
        scale = scale * (kOne - t.alongside_share * (d.control == 0 && !japan ? kOne : d.alongside_mult));
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
    if (prologue_) { // (0x424C48)
        bool done = false;
        const prologue::Step s = prologue_step(d);
        with_record(d, [&](ex::Car& c) { done = prologue::request_line(c, s, line, at_once); });
        d.inc.lane_changes += done;
        return done;
    }
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
    if (changed) {
        // A B-spec driver in an arc within 30 km/h of its speed learns (0x4684A8, before the segment's shift).
        if (d.control == 1 && d.kind == 0 && (d.mode == 1 || d.mode == 2) && d.v_cur - F::raw(0x41055554) < c.speed)
            bspec_experience(d, 0, d.v_cur);
        bookkeeping_shift(d);
    }
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
        if (t.brake_spread && spread_live_ && (d.control == 1 || d.control == 2) && d.kind == 0) m = m + draw(d.rand, t.brake_lo.at(m), t.brake_hi.at(m));
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
        // (not in a run alone: the conductor's virtual 6)
        if (!pace_.solo && (d.control == 1 || d.control == 2)) {
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
            // A B-spec driver learns at each braking point by the car's speed (0x4684A8).
            if (d.control == 1 && d.kind == 0) bspec_experience(d, 1, v);
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

bool Field::place(int line, int hint, F s, F& x, F& y, F& h) const { return lines_.place(line, hint, s, x, y, h); }

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

// 0x465078's PD steering (T[2] of 2 or more, racing): the steering moved at a rate summed from five terms and their rates
// (the slip direction against the steering, the yaw rate short, the line's heading against the travel, the car's offset
// from the line, the line's heading against the car's), the rate integrated and bounded by the set's steering rate, the
// steering within 45 degrees. Its terms and rate kept in the driver (D +0x00 .. +0x10, F +0x484).
void Field::pd_steer(Driver& d, F dir, F rd, F r, F amax, F ky) {
    const ex::Car& c = session_->record(d.car);
    const Tuning& t = tables_.tuning;
    const F inv = c.inv_dt, dt = c.dt;
    F e = dir - d.steer;
    F de = e - d.pd_last[0];
    d.pd_last[0] = e;
    F sum = k::zero - (t.pd[8] * inv) * de;
    sum = sum + t.pd[0] * e;
    e = rd - r;
    de = e - d.pd_last[1];
    d.pd_last[1] = e;
    sum = sum + (t.pd[9] * inv) * de;
    if ((k::zero < rd && r < rd) || (rd < k::zero && rd < r)) sum = sum + (t.pd[1] * e) * ky;
    // The line's heading and place at the car's distance (0x4649F0).
    const F s = course_s(d.car);
    F px = c.pos.x, py = c.pos.y, ph = c.pos.z;
    place(d.line, d.next, s, px, py, ph);
    const F hd = line_heading(d, d.line, s);
    const F travel = ai::atan2(-c.vel.x, c.vel.y);
    const F a1 = angle_between(hd, travel), a2 = angle_between(hd, c.heading);
    F off = (c.pos.x - px) * gt3r::ee::cos(hd);
    off = off + (c.pos.y - py) * gt3r::ee::sin(hd);
    if (t.pd_bound < off) off = t.pd_bound;
    else if (off < -t.pd_bound) off = -t.pd_bound;
    de = a1 - d.pd_last[2];
    d.pd_last[2] = a1;
    sum = sum + (t.pd[10] * inv) * de;
    const F d3 = off - d.pd_last[3];
    d.pd_last[3] = off;
    sum = sum + t.pd[2] * a1;
    const F d4 = a2 - d.pd_last[4];
    d.pd_last[4] = a2;
    sum = sum - (t.pd[11] * inv) * d3;
    sum = sum + (t.pd[3] * off) * amax;
    sum = sum - (t.pd[12] * inv) * d4;
    sum = sum - t.pd[4] * a2;
    sum = sum - t.pd[7] * d.pd_rate;
    F rate = d.pd_rate + sum * dt;
    const F lim = d.p.steer_rate;
    if (lim < rate) rate = lim;
    else if (rate < -lim) rate = -lim;
    const F quarter_pi = F::raw(0x3F490FD9);
    const F st = d.steer + rate * dt;
    if (quarter_pi < st) d.steer = quarter_pi, d.pd_rate = k::zero;
    else if (st < -quarter_pi) d.steer = -quarter_pi, d.pd_rate = k::zero;
    else d.steer = st, d.pd_rate = rate;
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
    // (SCPS-17001 v1.05's 0x45D238: kind 5 after kind 4 too)
    if (kind == 6 || (kind < 7 && kind == 5 && (was != 4 || tables_.japan))) d.stop_count = 120;
}

// 0x464E40: the outputs cleared (0x464ED8), the steering straightened.
void Field::idle(Driver& d) {
    const ex::Car& c = session_->record(d.car);
    d.steer = c.steer_cmd, d.throttle = F(), d.brake = F(), d.reverse = false, d.shift_lock = 0;
    if (tables_.tuning.blend_by_distance == 0 && k::zero < d.blend) {
        const F b = d.blend - c.dt;
        d.blend = b < k::zero ? k::zero : b;
    }
    steer_to(d, F());
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
    if (prologue_) { // (0x3B2358: the event's percents for a pad's car its driver takes; its line the main one, blended)
        if (d.control == 0 && !d.event_done) {
            const size_t k = size_t(d.drive_class);
            prologue::driver_event(d.ai, pace_.throttle[k], pace_.brake[k], pace_.corner[k], 0, d.index);
        }
        d.event_done = true;
        if (mode == 0) {
            if (!(d.control == 0 && d.kind == 0) || request_line(d, CourseLines::kMain, false)) d.kind = 1;
        } else if (mode == 1 || mode == 3) {
            if (request_line(d, CourseLines::kMain, false)) d.kind = mode == 3 ? 5 : 6;
        } else d.kind = 7;
        return;
    }
    if (tables_.japan) {
        // SCPS-17001 v1.05 (0x33FAA8, 0x33FCC8, 0x33FC48, 0x33FD48): no change for control source 4; the main line asked
        // for from any line, blended, and the kind set only when the change is made (on the drag strip, modes 1 and 3 keep
        // the lane).
        if (d.control == 4) return;
        const bool driven = (d.kind >= 2 && d.kind <= 4) || (d.control == 0 && d.kind == 0);
        if (mode == 0) {
            if (!driven || request_line(d, CourseLines::kMain, false)) d.kind = 1;
        } else if (mode == 1 || mode == 3) {
            if (pace_.strip || request_line(d, CourseLines::kMain, false)) set_kind(d, mode == 3 ? 5 : 6);
        } else set_kind(d, 7);
        return;
    }
    if (mode == 0) {
        // 0x3452F0: the computer drives on (a pad car of kind 0 too), off the passing line 7.
        if (d.line == 7) request_line(d, CourseLines::kMain, true);
        d.kind = 1;
    } else if (mode == 1 || mode == 3) {
        // 0x345520 / 0x3454A8: off the passing line 7 but on the drag strip (K +0xCBE1).
        if (d.line == 7 && !pace_.strip) request_line(d, CourseLines::kMain, true);
        set_kind(d, mode == 3 ? 5 : 6);
    } else set_kind(d, 7);
}

void Field::finish_run(Session& session, size_t car, int mode) {
    session.set_finished(car);
    finish(car, mode);
}

void Field::end_computers() {
    for (const auto& d : drivers_)
        if (d && d->control == 2 && !(d->kind >= 2 && d->kind <= 4)) {
            if (d->line == 7) request_line(*d, CourseLines::kMain, true);
            set_kind(*d, 5);
        }
}

int Field::kind(size_t car) const { return car < drivers_.size() && drivers_[car] ? drivers_[car]->kind : 0; }
void Field::set_bspec_record(size_t car, const std::array<u8, 0x40>& record, int car_class) {
    Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    if (d) d->record = record, d->car_class = car_class;
}

std::array<u8, 0x40> Field::bspec_record(size_t car) const {
    const Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    return d ? d->record : std::array<u8, 0x40>{};
}

F Field::line_heading_at(size_t car) const {
    const Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    return d ? line_heading(*d, d->line, course_s(car)) : F();
}

u8 Field::line_mode_at(size_t car) const {
    const Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    return d ? u8(d->mode) : 0;
}

// 0x34AA88: the threat record as the on-board camera asks for it (none with CFG +0x02 = 6), the speed negated.
std::optional<Field::Threat> Field::threat(size_t car) const {
    if (!pace_.contact || car >= near_.size() || near_[car].threat == -1) return std::nullopt;
    const Near& z = near_[car];
    return Threat{z.threat, z.threat_gap, -z.threat_rate};
}

// 0x4684A8: the B-spec driver's experience. `table` 0 an arc's segment (v its speed, bins 60 .. 220 km/h), 1 a braking point,
// 2 a lane change (v the car's speed, bins 60 .. 260 km/h).
void Field::bspec_experience(Driver& d, int table, F v) {
    auto add = [&](size_t off, u32 n) {
        const u32 x = std::min<u32>(u32(d.record[off] | (d.record[off + 1] << 8)) + n, 0xFFFF);
        d.record[off] = u8(x), d.record[off + 1] = u8(x >> 8);
    };
    const F lo = F::raw(0x41855554), hi = table == 0 ? F::raw(0x427471C6) : F::raw(0x429071C6);
    F x = (v - lo) * (kOne / (hi - lo));
    if (x < k::zero) x = k::zero;
    else if (kOne < x) x = kOne;
    // 0x468478 / 0x4682D0: 64 shared between the two speed bins around the speed
    const int n = gt3r::ee::to_int((x * F::raw(0x42800000)).bits) - 4;
    const int bin = n / 8, r = n - bin * 8;
    int second = 8;
    u32 first_add = 0x40, second_add = 0;
    if (r > 0) second = bin + 1, second_add = u32(r) * 8, first_add = u32(8 - r) * 8;
    if (u32(bin) < 8) add(size_t(table * 16 + bin * 2), first_add);
    if (u32(second) < 8) add(size_t(table * 16 + second * 2), second_add);
    // Alone (a run alone: the conductor's virtual 6; a race of one; no car in its cells) or in company.
    bool near = false;
    if (!pace_.solo && session_->size() >= 2)
        for (u8 z : near_.at(d.car).zone) near = near || z != 0;
    const size_t half = size_t(d.car_class / 2);
    if (!near) add(0x30 + 2 * half, 0x1000);
    else add(0x30 + 2 * half, 0x1000 / 3), add(0x38 + 2 * half, 0x2000 / 3);
}

void Field::set_shadow(size_t car) {
    Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    if (!d) return;
    d->control = 0;
    setup(*d); // (its parameters without a computer driver's spread: 0x467148 runs for control source 2 only)
    // 0x3F1890: the player's starting level, whoever drives, no passing. Japan's (0x3EA6A8) gives it only to control source 1:
    // a pad car's driver takes the computer drivers' level, passing allowed but in a run alone.
    const int k = tables_.japan ? 1 : 0;
    const Tables::PaceLevel& l = tables_.pace[tables_.pace_start[size_t(k)]];
    d->pace = tables_.pace_start[size_t(k)], d->overtake = tables_.japan && !pace_.solo, d->pace_brake = l.brake, d->pace_corner = l.corner, d->shift = l.shift;
}

Field::Suggestion Field::suggestion(size_t car) const {
    const Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    return d ? d->suggest : Suggestion{};
}

std::optional<F> Field::least_target(size_t car) const {
    const Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    if (!d) return std::nullopt;
    return d->v_next < d->v_cur ? d->v_next : d->v_cur;
}

void Field::set_bspec_skill(size_t car, F corner, F brake, F alongside) {
    Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    if (!d) return;
    d->corner_mult = corner, d->brake_mult = brake, d->alongside_mult = alongside;
}

void Field::start_line(size_t car, int line) {
    Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    if (!d) return;
    if (prologue_) { // (0x424AB8)
        with_record(*d, [&](ex::Car& c) { prologue::driver_start(c, prologue_step(*d), prologue_->tables, line); });
        return;
    }
    // (0x4639E0 seeds the draws and makes the plan once, on the start's line: the constructor's plan took a draw)
    d->rand = seed(*d);
    if (request_line(*d, line, true)) --d->inc.lane_changes;
}

void Field::start_grid_line(size_t car, int grid_place) {
    const Driver* d = driver(car);
    if (!d || !prologue_) return;
    if (d->control == 0) return start_line(car, 7);
    int line = prologue_->tables.grid_line[size_t(std::max(grid_place, 0)) % 6];
    const auto& grid = prologue_->query.grid();
    if (grid.size() >= 2) { // (the second place to the first's other side: lines 1 and 2 the other way round)
        const F h = grid[0].yaw + F::raw(0x40490FDA);
        const F side = (grid[0].x - grid[1].x) * gt3r::ee::cos(h) - gt3r::ee::sin(h) * (grid[0].z - grid[1].z);
        if (!(side < F()) && (line == 1 || line == 2)) line = 3 - line;
    }
    start_line(car, line);
}

bool Field::hold_rolling(size_t car) {
    Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    if (!d || d->control == 4) return false;
    // 0x3452F0: off the passing line 7 (a pad car's), the computer driving.
    const bool driven = (d->kind >= 2 && d->kind <= 4) || (d->control == 0 && d->kind == 0);
    if (prologue_) { // (0x3C99E4: a pad's car; 0x3B3FD0: onto the main line, blended)
        if (d->control != 0) return false;
        if (driven && !request_line(*d, CourseLines::kMain, false)) return false;
    } else if (tables_.japan) { // (SCPS-17001 v1.05's 0x33FAA8: onto the main line from any line, blended; held only when it is made)
        if (driven && !request_line(*d, CourseLines::kMain, false)) return false;
    } else if (driven && d->line == 7) request_line(*d, CourseLines::kMain, true);
    d->kind = 1;
    return true;
}

bool Field::release_rolling(size_t car) {
    Driver* d = car < drivers_.size() ? drivers_[car].get() : nullptr;
    if (!d) return true; // (no driver: let go)
    if (prologue_) d->ai.state = 0; // (0x3B3F40)
    // 0x345228: a pad car's driver onto line 7 (its fallback), one on the pit lane's lines onto the main line; the kind cleared.
    if (d->control == 0) {
        if (!request_line(*d, 7, false)) return false;
    } else if (d->kind >= 2 && d->kind <= 4) {
        if (!request_line(*d, CourseLines::kMain, false)) return false;
    }
    d->kind = 0;
    return true;
}

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
// false when b is not ahead (or, within 20 m, not overlapping a sideways; SCPS-17001 v1.05's 0x3ECFE0: at any gap).
bool Field::closing(size_t a, size_t b, F v, F& gap, F& rate) const {
    const ex::Car &ca = session_->record(a), &cb = session_->record(b);
    const F lap(lines_.lap()), sa = course_s(a), sb = course_s(b);
    F g = lap * F::raw(0x3F400000) < sa && sb < lap * F::raw(0x3E800000) ? (sb + lap) - sa : sb - sa;
    g = g - cb.p.body_rear;
    const F gp = g - ca.p.body_front;
    if (gp <= k::zero) return false;
    const F rb = rate_[b], ra = rate_[a];
    const F diff = rb - ra;
    if (session_->japan() || gp < F::raw(0x41A00000)) {
        const F h = (ca.p.body_width + cb.p.body_width) * k::half;
        if (h < v || v < -h) return false;
    }
    gap = gp, rate = -diff;
    return true;
}

// 0x469918: a's contact with b ahead (a car to follow when closing within 5 m).
void Field::contact(size_t a, size_t b, F gap, F rate) {
    Near& z = near_[a];
    // 0x469888, the threat record: closing on b, the car reached soonest (gap / speed) since the last closing one; with
    // none closing yet, the nearest that is not.
    {
        bool take;
        if (k::zero < rate) take = !(k::zero < z.threat_rate) || gap / rate < z.threat_gap / z.threat_rate;
        else take = !(k::zero < z.threat_rate) && gap < z.threat_gap;
        if (take) z.threat = int(b), z.threat_gap = gap, z.threat_rate = rate;
    }
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
    for (Near& z : near_) z.contact = F(), z.zone = {}, z.gap_left = z.gap_right = F(1000.0f), z.chain = 0, z.threat = -1, z.threat_gap = F::raw(0x447A0000), z.threat_rate = F::raw(0xC47A0000);
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
            // (SCPS-17001 v1.05's phase 0x3ED338: the contacts only of cars less than 50 m apart)
            if (pace_.contact && !(session_->japan() && !(dist < F::raw(0x42480000)))) {
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
    else if ((d.status == 3 || d.status == 4) && (d.line == 1 || d.line == 2) && !tables_.japan) lanes = -1; // (not in SCPS-17001 v1.05's 0x462000)
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
            if (d.control == 1 && d.kind == 0) bspec_experience(d, 2, session_->record(d.car).speed);
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
    if (d.kind == 8) return steer_to(d, F()), void(d.throttle = F()); // (parked: 0x3F3400)
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
        if (d.stop_count && !tables_.japan) look = look + int_f(d.stop_count) * F::raw(0x3EAAAAAA); // (not in SCPS-17001 v1.05's 0x45D668)
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
    const bool slew = t.steer_mode < 2 || d.state != 0; // (T[2] of 2 or more, racing: the PD steering, pd_steer)
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
                    if (!slew) pd_steer(d, dir, rd, r, amax, ky);
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
    // (SCPS-17001 v1.05's 0x45D668 gives its stop driver the steering without the lock's bound)
    if (d.kind == 6 && tables_.japan) return stop(d, tgt);
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
        if (5000 + 100 * rank < s32(clock_ / 3) && !pace_.strip) traffic(d);
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
// from the reference car (the first of the order the computer does not drive; none: car 0) outwards along the order, each car
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
    for (size_t pos = 0; pos < n && ref == n; ++pos) // (0x34E418: the first of the order whose control source is not 2)
        if (!drivers_[order[pos]] || drivers_[order[pos]]->control != 2) ref = order[pos];
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
        if (d.control == 0 && d.kind == 0) continue; // (a pad's car: its own input, the engine answering its throttle)
        const ex::Car& c = session_->record(d.car);
        // 0x463CD8: a blip between 3/4 and 15/16 of the shift speed.
        const int up = c.p.d.up_rpm, rpm = c.rpm;
        if (d.blip == 0) {
            if (rpm < 3 * up / 4) d.blip = 1;
        } else if (rpm >= 15 * up / 16) d.blip = 0;
        d.applied_throttle = std::clamp(d.applied_throttle + (d.blip ? 4.0f : -4.0f) * kStep, 0.0f, 1.0f);
        race::CarInput in;
        in.throttle = d.applied_throttle;
        in.computer = d.control != 0, in.control = u8(d.control == 1 ? 1 : 2);
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

void Field::begin_step(bool near_pass) {
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
    // (Prologue's: the session's phase 14 leaves them in the records)
    if (near_pass && !prologue_) proximity();
}

void Field::end_step() {
    ++steps_;
    clock_ = std::min<u32>(clock_ + 50, 0x405F7DFD);
}

// The throttle pedal the driver's wanted throttle makes (0x464F38): slewed by the step in the arcs.
F Field::pedal(const Driver& d) const {
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
    return thr;
}

// 0x469E58: the suggested gear's speed.
void Field::suggest(Driver& d) {
    Suggestion& g = d.suggest;
    g = {};
    F target = d.v_next;
    if (d.mode == 4 && !(d.v_next < d.v_cur)) target = d.v_cur;
    if (!(session_->record(d.car).speed < target)) {
        g.on = true, g.speed = target;
        g.firm = d.mode == 3 || d.mode == 4;
    }
}

void Field::decide_car(size_t car, race::CarInput& in) {
    if (car >= drivers_.size() || !drivers_[car]) return;
    Driver& d = *drivers_[car];
    incidents_after(d);
    if (prologue_) {
        prologue_decide(d, d.control == 0 && d.kind == 0, in);
        suggest(d);
        return;
    }
    // (0x3F3400: a driven car stood in its pit box, F +0x4BA, or stopped for good, kind 8, decides nothing)
    {
        const ex::Car& c = session_->record(d.car);
        if ((d.control != 0 || d.kind != 0) && c.race_hold == 0 && (c.hold != 0 || d.kind == 8)) idle(d);
        else decide(d);
    }
    // The outputs into the controls (0x464F38): the throttle pedal slews to the wanted throttle by the step in the arcs.
    const ex::Car& c = session_->record(d.car);
    const F thr = pedal(d);
    d.applied_throttle = thr.host();
    suggest(d);
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
    // (a player's car its driver drives, in its rolling start, the pits or after its finish, keeps its control source, 0: F +0x467
    // tells the input)
    in.computer = d.control != 0 || c.driver_kind == 0;
    in.control = u8(d.control == 1 ? 1 : 2); // (F +0x466: the B-spec car 1)
    in.shift_lock = u8(d.shift_lock);
}

// 0x463CD8: the engine held through the start's hold, `count` steps of it left: 13 and more the throttle up and down at 5 and
// 10 a second less the car's rank's shares between 3/4 and 15/16 of the up-shift speed; then held by the speed.
void Field::blip(Driver& d, u32 count) {
    const ex::Car& c = session_->record(d.car);
    const F rank = int_f(c.rank);
    const int up = s32(session_->extra(d.car).drive.d.up_rpm), rpm = s32(c.rpm);
    if (count >= 13) {
        if (d.blip == 0) {
            if (rpm < ((3 * up) >> 2)) d.blip = 1;
        } else if (!(rpm < (15 * up) / 16)) d.blip = 0;
        if (d.blip != 0) {
            d.throttle = d.throttle + c.dt * (F::raw(0x40A00000) - rank * F::raw(0x3E4CCCCC));
            if (kOne < d.throttle) d.throttle = kOne;
        } else {
            d.throttle = d.throttle - c.dt * (F::raw(0x41200000) - rank * F::raw(0x3ECCCCCC));
            if (d.throttle < F()) d.throttle = F();
        }
        return;
    }
    const F q = int_f(rpm - up) / int_f(s32(session_->extra(d.car).drive.e.rev_limit));
    d.throttle = (F::raw(0x3F200000) - rank * F::raw(0x3B03126E)) - (q + q);
    if (kOne < d.throttle) d.throttle = kOne;
    else if (d.throttle < F()) d.throttle = F();
}

void Field::step(Session& session, std::span<race::CarInput> inputs, int keep, u32 count) {
    spread_live_ = true; // (the race prepared: the spreads' switches set)
    begin_step(false);
    hold_count_ = count;
    // (the band is run at a step's end, for the next: the race's first step runs at the cars' own step length)
    if (first_step_ && session.time() == 0) std::fill(scale_.begin(), scale_.end(), kOne);
    first_step_ = false;
    apply_time_scales(session);
    for (const auto& dp : drivers_)
        if (dp) {
            session.set_pace_level(dp->car, dp->pace);
            session.set_driver_kind(dp->car, u8(dp->kind));
            if (dp->control == 1) session.set_shift_factor(dp->car, session.tank(dp->car) > 0 ? shift_factor(dp->car) : 128); // (0x3F19E0: with a tank, 0x354E38)
        }
    auto plans_only = [&](const Driver& d) { return (d.control == 0 && d.kind == 0) || int(d.car) == keep; };
    for (auto& dp : drivers_)
        if (dp && dp->car < inputs.size() && !plans_only(*dp)) {
            race::CarInput& in = inputs[dp->car];
            in.computer = dp->control != 0 || session_->record(dp->car).driver_kind == 0;
            in.control = u8(dp->control == 1 ? 1 : 2);
        }
    auto decide_one = [&](size_t car, race::CarInput& in) {
        if (car >= drivers_.size() || !drivers_[car]) return;
        Driver& d = *drivers_[car];
        if (plans_only(d)) {
            race::CarInput unused;
            decide_car(car, unused);
        } else {
            const race::PadRecord* pad = in.pad; // (a player's car in its rolling start: its controller's frame is read all the same)
            decide_car(car, in);
            in.pad = pad;
            if (!prologue_ && d.control != 0 && session_->record(car).race_hold != 0) { // (0x3F3400)
                blip(d, count + session.start_counter(car));
                in.throttle = pedal(d).host(), in.brake = 0, in.steer_angle = 0;
            }
        }
        session.set_line_heading(car, line_heading_at(car), line_mode_at(car));
    };
    const auto near_cars = [this] { // (0x3F4638, within the step; Prologue's: the session's phase 14)
        if (!prologue_) proximity();
    };
    if (count > 0) return session.hold_once(inputs, decide_one, near_cars); // (no step of the race's clock)
    session.step_once(inputs, decide_one, near_cars);
    end_step();
}

void Field::drive(std::span<race::CarInput> inputs) {
    begin_step();
    for (auto& dp : drivers_) {
        if (!dp || dp->car >= inputs.size()) continue;
        if (dp->control == 0 && dp->kind == 0) { // the player's own car: planned, not driven
            race::CarInput unused;
            decide_car(dp->car, unused);
        } else {
            decide_car(dp->car, inputs[dp->car]);
        }
    }
    end_step();
}

} // namespace gt4r::sim::ai
