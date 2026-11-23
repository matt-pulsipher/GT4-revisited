#include "gt4r/sim/ai/drivers.h"

#include "base/ee_math.h"
#include "gt4r/disc/disc.h"
#include "gt4r/sim/exact_bridge.h"
#include "gt4r/sim/rules_build.h"
#include "sim/exact/tick.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace gt4r::sim::ai {

namespace ex = gt3r::sim::exact;
using gt3r::ee::F;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDeg = kPi / 180.0f;
constexpr float kKmh = 1.0f / 3.6f;
constexpr float kStep = Session::kStep;

float wrap(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

// The zone cells around a car (0x4694F8's bytes +0x48 ..).
enum Zone { kFront, kFrontLeft, kFrontRight, kLeft, kRight, kRearLeft, kRearRight, kRear };

} // namespace

struct Field::Driver {
    size_t car = 0;
    int control = 2;        // F+0x466: 2 a computer car
    int index = 0;          // the car's grid index
    int drive_class = 0;
    Params p;
    // The plan (0x463C00 ..).
    int line = 6, prev_line = 6, next = 0, mode = 0, next_kind = 0;
    F v_cur, v_next;
    float blend = 0;
    int arc_ticks = 0;
    int state = 0;          // recovery: 0 racing, 1 backing out, 2 turning round
    int status = 0;         // 0x43: 1 nearing a braking point, 2 braking, 3 / 4 / 5 in an arc near the grip's limit
    int state_hold = 0;     // 0x6B: steps the state machine rests after a change
    s16 cooldown = 0;       // 0x18: the lane change's cooldown
    bool long_start = true; // 0x1A == 0: the standing start's long look ahead (5 s)
    float offset = 0;       // 0x54: the sideways offset of the aim (m, + right)
    u16 rand = 0;           // 0x68
    F corner_mult{1.0f}, brake_mult{1.0f}, alongside_mult{1.0f}; // 0xB8 / 0xBC / 0xC0
    // Proximity, every step (0x4694A0).
    float contact = 0;
    std::array<u8, 8> zone{};
    int follow = -1;        // 0x51: the car ahead it follows
    float gap_left = 1000, gap_right = 1000; // 0x58 / 0x5C
    int chain = 0;          // 0x6A: the follow chain's length (m of braking added)
    // Outputs.
    float steer = 0, throttle = 0, brake = 0, applied_throttle = 0;
    bool reverse = false;
    // Checks.
    Incidents inc;
    bool was_wall = false, was_car = false, was_off = false;
    int prev_state = 0;
    float s = 0;            // the car's distance along the course this step
    int course_hint = -1;
    bool hit_front = false, hit_rear = false;
    u8 blip = 0;
    float time_scale = 1;
    float look = 0;
    float brake_dist = 0, brake_to = 0;
    u8 shift_lock = 0;      // 0x3C: the gearbox held this many steps (a part throttle)
    rules::WheelLayout lay; // the car's wheels (0x359510)
    std::array<u8, 0x54> lay_raw{0, 4, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 2}; // its row of the program's table
    int kind = 0;           // F+0x467: the driver's kind (0 racing)
};

std::optional<course::AiData> Field::read(Disc& disc, const std::string& course) {
    gt3r::Volume& vol = disc.volume();
    const gt3r::Volume::Entry* e = vol.find("crs/" + course + ".ad");
    if (!e) return std::nullopt;
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
    if (program.known())
        for (u32 m = 0; m < 24; ++m) soft_[m] = program.w(Program::kSoft + 4 * m);
    else
        soft_[2] = soft_[3] = soft_[4] = soft_[6] = 1;
    const size_t n = session.size();
    drivers_.resize(n);
    ranks_.assign(n, 0);
    for (size_t car : computer) {
        if (car >= n) continue;
        auto d = std::make_unique<Driver>();
        d->car = car, d->index = int(car);
        // The driveline class (0x466B28): FF 1; four wheel drive by its system: 3 -> 3, 0 -> 1, 5 -> 0, others 2; the rest 0.
        const Spec& spec = session.spec(car);
        const int drive = spec.drive(), system = spec.b(0x129);
        d->drive_class = drive == 1 ? 1 : drive == 2 ? (system == 3 ? 3 : system == 0 ? 1 : system == 5 ? 0 : 2) : 0;
        if (program.known()) {
            const u8 row = wheel_layout_index(spec);
            d->lay = rules::wheel_layout(program, row);
            for (u32 k = 0; k < 0x54; ++k) d->lay_raw[k] = program.b(rules::kWheelLayouts + rules::kWheelLayoutSize * row + k);
        }
        std::array<u8, 0x80> set{};
        if (const auto& s = data.sets[size_t(d->drive_class)]) set = *s;
        else std::copy(tables_.builtin[session.loose() ? 1 : 0].begin(), tables_.builtin[session.loose() ? 1 : 0].end(), set.begin());
        d->p = params_from(set, d->drive_class, tables_.tuning);
        const ex::Car& c = session.record(car);
        // 0x4673D8: the downforce term (the aero bonus's lift part is ❓ not mirrored: 0).
        if (c.p.mass != F()) d->p.downforce = (c.p.downforce_coef[0] + c.p.downforce_coef[1]) / c.p.mass;
        // The event's changes (0x467148): each class's percents, then the drivers' spread by grid index.
        int thr = pace_.throttle[size_t(d->drive_class)], brk = pace_.brake[size_t(d->drive_class)], cs = pace_.corner[size_t(d->drive_class)];
        if (thr == 0 || thr > 100) thr = 100;
        if (brk == 0) brk = 100;
        if (cs == 0) cs = 100;
        int off = 0;
        if (tables_.driver_spread && d->control == 2) {
            const int idx = d->index;
            if (!tables_.tuning.brake_spread) brk -= (idx % 3) * 7;
            if (!tables_.tuning.corner_spread) cs -= (idx * 4) & 4, off = -2 * ((idx * 7) % 5);
            d->p.yaw_gain = d->p.yaw_gain * (int_f(((idx + 1) % 3) * 3 + 97) * kHundredth);
        }
        if (thr != 100) d->p.throttle = int_f(thr) * kHundredth;
        if (brk != 100) d->brake_mult = d->brake_mult * (int_f(brk) * kHundredth);
        if (cs != 100) d->corner_mult = d->corner_mult * (int_f(cs) * kHundredth);
        if (off) d->p.corner_offset = d->p.corner_offset + int_f(off) * kPerKmh;
        // The random draws (0x4639E0): seeded by the grid index for a computer car, then one step.
        d->rand = u16(d->control == 2 ? d->index + 1 : 1);
        d->rand = u16(((u32(d->rand) * 0x41C64E6Du + 0x3039u) >> 16) & 0x7FFF);
        d->long_start = pace_.standing_start && !session.loose();
        // The first line: the main line, or the nearest of the others to the car.
        d->line = d->prev_line = lines_.resolve(6);
        d->steer = 0;
        drivers_[car] = std::move(d);
    }
    for (auto& d : drivers_)
        if (d && d->line >= 0) {
            const race::CarState& st = session.state(d->car);
            d->s = lines_.wrap_lap(st.course_v);
            replan(*d);
        }
}

Field::~Field() = default;

const Field::Driver* Field::driver(size_t car) const { return car < drivers_.size() ? drivers_[car].get() : nullptr; }
const Params& Field::params(size_t car) const { return drivers_.at(car)->p; }
const Incidents& Field::incidents(size_t car) const { return drivers_.at(car)->inc; }
int Field::line(size_t car) const { return drivers_.at(car)->line; }
int Field::recovery(size_t car) const { return drivers_.at(car)->state; }
float Field::time_scale(size_t car) const { return car < drivers_.size() && drivers_[car] ? drivers_[car]->time_scale : 1.0f; }

Field::Snapshot Field::snapshot(size_t car) const {
    Snapshot o;
    const Driver* d = driver(car);
    if (!d) return o;
    o.line = d->line, o.next = d->next, o.mode = d->mode, o.status = d->status, o.state = d->state, o.follow = d->follow;
    o.wanted_throttle = d->throttle, o.reverse = d->reverse, o.look = d->look, o.brake_dist = d->brake_dist, o.brake_to = d->brake_to;
    o.v_cur = d->v_cur.host(), o.v_next = d->v_next.host(), o.steer = d->steer, o.lock = session_->record(car).lock_hi.host(), o.steer_cmd = session_->record(car).steer_cmd.host(), o.throttle = d->applied_throttle, o.brake = d->brake, o.contact = d->contact;
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
    d.line = p.line, d.next = p.next, d.mode = p.mode, d.next_kind = p.next_kind, d.status = p.status, d.follow = p.follow, d.chain = p.chain;
    d.v_cur = p.v_cur, d.v_next = p.v_next, d.contact = p.contact, d.s = p.s, d.rand = p.rand;
    d.corner_mult = p.corner_mult, d.brake_mult = p.brake_mult, d.alongside_mult = p.alongside_mult;
    d.zone = p.zone, d.cooldown = p.cooldown;
}

Field::Probe Field::probe_get(size_t car) const {
    const Driver& d = *drivers_.at(car);
    Probe p;
    p.line = d.line, p.next = d.next, p.mode = d.mode, p.next_kind = d.next_kind, p.status = d.status, p.follow = d.follow, p.chain = d.chain;
    p.v_cur = d.v_cur, p.v_next = d.v_next, p.contact = d.contact, p.s = d.s, p.rand = d.rand;
    p.corner_mult = d.corner_mult, p.brake_mult = d.brake_mult, p.alongside_mult = d.alongside_mult;
    p.zone = d.zone, p.cooldown = d.cooldown;
    return p;
}

F Field::probe_corner_speed(size_t car, int i) { return corner_speed(*drivers_.at(car), i); }
F Field::probe_braking(size_t car) { return braking(*drivers_.at(car)); }

// ---- the plan --------------------------------------------------------------------------------------------------

// 0x463988: a + (b - a) x the driver's draw / 32768.
static F draw(u16 r, F a, F b) { return a + (b - a) * (int_f(s32(r)) * F::raw(0x38000000)); }

// 0x467470: the computer drivers' grip factor for the front's tire record on a surface (1 for a pad car without a driver).
F Field::ai_grip(const Driver& d, int surface) const {
    if (d.control != 1 && d.control != 2 && d.kind == 0) return kOne;
    const u8 cls = session_->extra(d.car).tires.tire_index[0][size_t(surface) % 24];
    return d.p.grip[std::min<size_t>(cls, 2)];
}

// 0x4674F0, in the program's arithmetic.
F Field::corner_speed(const Driver& d, int i) {
    const ex::Car& c = session_->record(d.car);
    const std::vector<LinePoint>& pts = lines_.line(d.line);
    if (pts.empty()) return c.p.top_speed;
    const LinePoint& pt = pts[size_t(i) % pts.size()];
    const sim::Extra& x = session_->extra(d.car);
    const u8 s = u8(pt.surface % 24);
    const rules::Tire& rf = rules::tire_record(x.tires, d.lay, d.lay_raw[0xC], s);
    const rules::Tire& rr = rules::tire_record(x.tires, d.lay, d.lay_raw[0xD], s);
    const F w = d.p.front_weight; // (❓ two flags of the car halve it towards 0.5: not mirrored)
    const F g = ai_grip(d, s);
    const F mu0 = ((w * rf.mu[s]) * rf.grip) * g, mu1 = (((kOne - w) * rr.mu[s]) * rr.grip) * g;
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
    if (t.alongside_slow && (d.zone[kFrontLeft] || d.zone[kLeft] || d.zone[kRight] || d.zone[kFrontRight]))
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

// 0x463C28: a line change, blended unless at once.
bool Field::request_line(Driver& d, int line, bool at_once) {
    const int r = lines_.resolve(line);
    if (r < 0 || r == d.line) return false;
    if (at_once) d.prev_line = r, d.blend = 0;
    else {
        d.blend = r == d.prev_line ? 1 - d.blend : 1;
        d.prev_line = d.line;
    }
    d.line = r;
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
        const LinePoint& q = pts[size_t(bp)];
        const sim::Extra& x = session_->extra(d.car);
        const u8 s = u8(q.surface % 24), axle = d.lay_raw[0x10] & 1;
        const rules::Tire& rf = rules::tire_record(x.tires, d.lay, d.lay_raw[0xC], s);
        F mu = rf.mu[s] * ai_grip(d, s);
        mu = mu * ex::lookup_bisect(rf.load_curve, (c.p.weight * kHalf) / x.tires.width[axle]);
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
            dist = dist + int_f(d.chain);
            if (d.cooldown == 0 && zero < F(d.contact)) {
                dist = dist + F::raw(0x41200000);
                if (d.follow >= 0 && session_->state(size_t(d.follow)).brake > 0) dist = dist + F::raw(0x41000000);
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

// 0x464770: throttle and brake towards a speed.
void Field::speed_control(Driver& d, float vt, float gain) {
    const float v = session_->record(d.car).speed.host();
    const float dv = std::clamp(v - (vt - 2 * kKmh), -10 * kKmh, 10 * kKmh);
    d.throttle = std::clamp(0.75f + gain * dv, 0.0f, 1.0f);
    if (d.throttle != 0 && d.throttle != 1) d.shift_lock = 12;
    float vb = vt;
    if (d.mode != 3 && d.mode != 4) vb += 20 * kKmh;
    if (d.throttle == 0 && vb < v) d.brake = 1;
}

// 0x464868: the aim on a line `look` metres on (at least 5 m from the car), relative to the car.
void Field::target(const Driver& d, int line, float look, float& dx, float& dy) const {
    const ex::Car& c = session_->record(d.car);
    const float px = c.pos.x.host(), py = c.pos.y.host();
    float x = px, y = py, h = 0;
    for (int i = 0; i < 10; ++i) {
        lines_.evaluate(line, d.next, d.s + look, x, y, h);
        dx = x - px, dy = y - py;
        if (std::hypot(dx, dy) >= 5.0f) break;
        look += 1;
    }
}

// ---- traffic ---------------------------------------------------------------------------------------------------

// 0x3F4638 with 0x4694F8 (the zones), 0x3F42D0 (closing on a car ahead) and 0x469918 (the contact).
void Field::proximity() {
    const size_t n = drivers_.size();
    const Cars& cars = *session_;
    for (auto& d : drivers_)
        if (d) {
            d->chain = 0, d->contact = 0, d->zone = {};
            d->gap_left = d->gap_right = 1000;
        }
    const float lap = lines_.lap();
    struct Body {
        float x, y, h, hx, hy, front, rear, half_width, speed, s;
    };
    std::vector<Body> b(n);
    for (size_t i = 0; i < n; ++i) {
        const ex::Car& c = session_->record(i);
        const float fx = c.fwd.x.host(), fy = c.fwd.y.host(), l = std::max(std::hypot(fx, fy), 1e-6f);
        b[i] = {c.pos.x.host(), c.pos.y.host(), c.pos.z.host(), fx / l, fy / l, c.p.body_front.host(), c.p.body_rear.host(), c.p.body_width.host() * 0.5f,
                c.speed.host(), lines_.wrap_lap(cars.state(i).course_v)};
    }
    auto zones = [&](Driver& d, float dist, float u, float v, float ahead, float behind, float w) {
        auto& z = d.zone;
        auto mark = [&](int cell, u8 level) {
            if (z[size_t(cell)] < level) z[size_t(cell)] = level;
        };
        if (dist > 20 || std::fabs(v) > w + 3) return;
        if (u > 0.9f * ahead) {
            const float gap = u - ahead;
            const u8 near = gap < 5 ? 2 : gap < 20 ? 1 : 0;
            if (v < -0.75f * w) {
                if (near) mark(kFrontLeft, near);
            } else if (v > 0.75f * w) {
                if (near) mark(kFrontRight, near);
            } else if (gap < 20) {
                mark(kFront, 2);
                if (v < -w * 0.5f && near) mark(kFrontLeft, near);
                if (v > w * 0.5f && near) mark(kFrontRight, near);
            }
        } else if (u < -0.9f * behind) {
            if (v < -w) mark(kRearLeft, 2);
            else if (v > w) mark(kRearRight, 2);
            else mark(kRear, 2), mark(v > 0 ? kRearRight : kRearLeft, 1);
        } else if (v < 0) {
            d.gap_left = std::min(d.gap_left, -v - w);
            mark(kLeft, 2);
            if (u > ahead * 0.5f) mark(kFrontLeft, 1);
            if (u < -behind * 0.5f) mark(kRearLeft, 1);
        } else {
            d.gap_right = std::min(d.gap_right, v - w);
            mark(kRight, 2);
            if (u > ahead * 0.5f) mark(kFrontRight, 1);
            if (u < -behind * 0.5f) mark(kRearRight, 1);
        }
    };
    // A on B ahead: the gap along the course and the closing speed; false when B is not ahead of A.
    auto closing = [&](size_t a, size_t bb, float v, float& gap, float& cl) {
        float sb = b[bb].s;
        if (b[a].s > 0.75f * lap && sb < 0.25f * lap) sb += lap;
        gap = sb - b[a].s - b[bb].rear - b[a].front;
        if (gap <= 0) return false;
        if (gap < 20 && std::fabs(v) > b[a].half_width + b[bb].half_width) return false;
        cl = b[a].speed - b[bb].speed;
        return true;
    };
    auto contact = [&](Driver& d, size_t other, float gap, float cl) {
        if (d.follow == int(other) && gap > 6) d.follow = -1;
        float cand = 0;
        int follow = -1;
        if (d.follow != int(other) && cl > 0 && gap < 5) cand = 2, follow = int(other);
        else if (cl > 20 * kKmh) cand = 1;
        else if (cl > 0.1f * kKmh && gap / cl < 5) cand = 0.5f;
        if (cand == 0) cand = gap < 0.5f ? 0.05f : gap < 4 ? 0.1f : 0;
        if (d.contact < cand) {
            if (follow != -1) d.follow = follow, cand = 1;
            d.contact = cand;
        }
    };
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            const float dx = b[j].x - b[i].x, dy = b[j].y - b[i].y;
            const float dist = std::hypot(dx, dy);
            if (dist > 100 || std::fabs(b[j].h - b[i].h) > 5.0f + 1.5f) continue;
            const float w = b[i].half_width + b[j].half_width;
            // B on A's axes (forward, right), and A on B's.
            const float ui = dx * b[i].hx + dy * b[i].hy, vi = dx * b[i].hy - dy * b[i].hx;
            const float uj = -dx * b[j].hx - dy * b[j].hy, vj = -dx * b[j].hy + dy * b[j].hx;
            if (Driver* d = drivers_[i].get()) zones(*d, dist, ui, vi, b[i].front + b[j].rear, b[i].rear + b[j].front, w);
            if (Driver* d = drivers_[j].get()) zones(*d, dist, uj, vj, b[j].front + b[i].rear, b[j].rear + b[i].front, w);
            float gap, cl;
            if (Driver* d = drivers_[i].get()) {
                if (closing(i, j, vi, gap, cl)) contact(*d, j, gap, cl);
                else if (d->follow == int(j)) d->follow = -1;
            }
            if (Driver* d = drivers_[j].get()) {
                if (closing(j, i, vj, gap, cl)) contact(*d, i, gap, cl);
                else if (d->follow == int(i)) d->follow = -1;
            }
        }
    // The follow chains: a car following a car that follows brakes that many metres earlier.
    for (auto& d : drivers_) {
        if (!d) continue;
        int k = d->follow, len = 0;
        while (k >= 0 && len < int(n)) {
            ++len;
            const Driver* o = drivers_[size_t(k)].get();
            k = o ? o->follow : -1;
        }
        d->chain = len;
    }
}

// 0x469AE8: lane changes, the lift behind a car, the return to the main line.
void Field::traffic(Driver& d) {
    if (d.cooldown > 0) --d.cooldown;
    if (d.line == CourseLines::kPitStop) return;
    int lanes = 1;
    const float v = session_->record(d.car).speed.host();
    if (d.line == CourseLines::kPitLane) {
        lanes = 0;
        if ((d.zone[kLeft] && d.zone[kFrontLeft]) || (d.zone[kRight] && d.zone[kFrontRight])) {
            if (v < 30 * kKmh) ++d.cooldown;
            else d.throttle *= 0.5f;
        }
    }
    if (d.status == 1 || d.status == 2 || d.status == 5) lanes = 0;
    else if ((d.status == 3 || d.status == 4) && (d.line == 1 || d.line == 2)) lanes = -1;
    float c = d.cooldown > 0 ? 0 : d.contact;
    if (d.blend > 0) lanes = 0;
    if (lanes > 0 && c > 0) {
        // The farthest line on each side: one beside, then the one beside that if there is one.
        int L = lines_.left(d.line), R = lines_.right(d.line);
        if (L >= 0 && lines_.left(L) >= 0) L = lines_.left(L);
        if (R >= 0 && lines_.right(R) >= 0) R = lines_.right(R);
        int pick = -1;
        if (d.next_kind == 1) pick = L >= 0 ? L : d.status != 4 ? R : -1;
        else if (d.next_kind == 2) pick = R >= 0 ? R : d.status != 3 ? L : -1;
        else {
            if (d.status != 3) pick = L;
            if (pick < 0 && d.status != 4) pick = R;
        }
        if (pick >= 0 && request_line(d, pick, false)) {
            d.cooldown = 180;
            c = 0;
        }
    }
    d.throttle *= 1 - std::min(c, 1.0f);
    if (lanes != 0) {
        const auto& z = d.zone;
        if (!z[kFront] && !z[kRight] && !z[kFrontRight] && !z[kFrontLeft] && !z[kLeft] && d.line != 6) request_line(d, 6, false);
    }
}

// ---- the decision ----------------------------------------------------------------------------------------------

void Field::decide(Driver& d) {
    const ex::Car& c = session_->record(d.car);
    const race::CarState& st = session_->state(d.car);
    const Tuning& t = tables_.tuning;
    // Begin (0x464ED8): the outputs cleared, the steering from the car's command.
    d.throttle = 0, d.brake = 0, d.reverse = false, d.shift_lock = 0;
    d.steer = c.steer_cmd.host();
    if (!t.blend_by_distance) d.blend = std::max(0.0f, d.blend - kStep);
    if (d.line < 0) return;
    const float scale = braking(d).host();
    const float v = c.speed.host();
    const float px = c.pos.x.host(), py = c.pos.y.host();
    const float fx = c.fwd.x.host(), fy = c.fwd.y.host();
    const float heading = std::atan2(-fx, fy);
    // The right side across the heading.
    const float rx = std::cos(heading), ry = std::sin(heading);

    // The look ahead.
    float look;
    if (d.state != 0) look = d.state == 1 ? 10.0f : 40.0f;
    else {
        // 0x464E88
        const F fv(v), f = d.p.look_speed * fv;
        look = (fv <= F() ? d.p.look_min : kOne <= f ? d.p.look_max : d.p.look_min + f * (d.p.look_max - d.p.look_min)).host();
        const float ms = float(steps_) * 1000.0f / 60.0f;
        if (d.long_start) {
            if (5000 - ms > 0) look += (5000 - ms) * 200 / 5000;
        } else if (2000 - ms > 0) look += (2000 - ms) * 40 / 2000;
    }
    d.look = look;
    float ax, ay;
    target(d, d.line, look, ax, ay);
    if (d.blend > 0 && lines_.has(d.prev_line)) {
        float ox, oy;
        target(d, d.prev_line, look, ox, oy);
        if (t.blend_by_distance) {
            const float gap = std::hypot(ox - ax, oy - ay), step = t.blend_rate.host() * kStep;
            d.blend = gap < step ? 0 : std::max(0.0f, d.blend - step / gap);
        }
        ax += (ox - ax) * d.blend, ay += (oy - ay) * d.blend;
    }
    // The aim's sideways offset (T[5]).
    if (t.offset_flags && d.state == 0) {
        float o = 0;
        if (t.offset_flags & 1) {
            float lx = px, ly = py, lh;
            lines_.evaluate(d.line, d.next, d.s, lx, ly, lh);
            const float e = rx * (lx - px) + ry * (ly - py);
            o = e * (d.line == 3 || d.line == 4 ? t.side_share_pit : t.side_share).host();
        }
        if ((t.offset_flags & 2) && d.line != 4) {
            if (d.gap_left < 2) o += 2 - d.gap_left;
            if (d.gap_right < 2) o -= 2 - d.gap_right;
        }
        if ((t.offset_flags & 4) && !session_->loose() && (d.mode == 0 || d.status == 0)) {
            int a = 0, b = 0;
            for (int w = 0; w < 4; ++w)
                if (st.wheels[w].on_ground && soft_[st.wheels[w].material & 31]) (w % 2 == 0 ? a : b) = 1;
            o += float(a - b) * 0.5f;
        }
        if (t.offset_by_grip) {
            const float g = std::fabs(c.lat_g.host());
            const float hi = t.grip_hi.host(), lo = t.grip_lo.host(), lm = t.offset_limit.host();
            const float lim = g >= hi ? 0 : g > lo ? lm * (hi - g) / (hi - lo) : lm;
            o = std::clamp(o, -lim, lim);
        }
        const float step = t.offset_rate.host() * kStep;
        d.offset += std::clamp(o - d.offset, -step, step);
        ax += d.offset * rx, ay += d.offset * ry;
    }
    const float eps = wrap(std::atan2(-ax, ay) - heading);
    const float aeps = std::fabs(eps);

    // The drive decision.
    float theta = 60 * kDeg;
    if (d.state == 1) {
        if (v >= 0) theta = 10 * kDeg;
        else if (v > -30 * kKmh) theta = 10 * kDeg - v * (50 * kDeg) / (30 * kKmh);
    }
    int drive = 0;
    bool brakeflag = false;
    if (d.state == 1) drive = -1;
    else if (aeps < theta || d.state == 2) {
        if (v > -2 * kKmh) drive = 1;
        else brakeflag = true;
    } else {
        if (v > 2 * kKmh) brakeflag = true;
        else drive = -1;
    }
    // The recovery state machine, resting 3 s after a change.
    if (d.state_hold > 0) --d.state_hold;
    else {
        const int before = d.state;
        if (d.state == 1) {
            if (aeps < theta) d.state = v > 3 * kKmh ? 0 : 2;
            else if (d.hit_rear) d.state = 2;
        } else if (d.state == 2) {
            if (aeps < 40 * kDeg && v > 30 * kKmh) d.state = 0;
            else if (d.hit_front && std::fabs(wrap(lines_.heading_at(d.line, d.next, d.s) - heading)) > 60 * kDeg) d.state = 1;
            else if (std::fabs(v) < 1 * kKmh) d.state = 1;
            else if (aeps > 60 * kDeg && d.hit_front) d.state = 1;
        } else {
            if (aeps >= 60 * kDeg) d.state = 2;
            else if (d.hit_front && std::fabs(v) < 1 * kKmh) d.state = 1;
            else if (d.hit_rear && aeps > 45 * kDeg) d.state = 2;
        }
        if (d.state != before) d.state_hold = 180;
    }

    // Steering.
    const int moving = v > 2 * kKmh ? 1 : v < -2 * kKmh ? -1 : 0;
    float tgt = 0;
    if (moving != 0) {
        if (drive == 1) {
            // The steered wheels' (those ahead of the center of gravity) mean peak slip angle and slip direction.
            const sim::Extra& x = session_->extra(d.car);
            float amax = 0, dir = 0;
            int steered = 0;
            for (int w = 0; w < int(d.lay.count) && w < 4; ++w)
                if (gt3r::s8(d.lay.fore_aft[size_t(w)]) > 0) {
                    amax += rules::tire_record(x.tires, d.lay, w, c.w[size_t(w)].surface).peak_slip.host();
                    dir += c.w[size_t(w)].slip_dir.host();
                    ++steered;
                }
            if (steered) amax /= float(steered), dir /= float(steered);
            if (d.state != 0) {
                if (eps > 80 * kDeg) tgt = amax;
                else if (eps < -80 * kDeg) tgt = -amax;
            }
            if (tgt == 0) {
                const float vx = c.vel.x.host(), vy = c.vel.y.host(), vxy = std::hypot(vx, vy);
                float cx = fx, cy = fy;
                if (vxy >= 5 * kKmh) cx = vx, cy = vy;
                const float cl = std::max(std::hypot(cx, cy), 1e-6f);
                cx /= cl, cy /= cl;
                // The aim's offset to the left of that direction.
                const float yl = -cy * ax + cx * ay;
                const float p2 = std::max(ax * ax + ay * ay, 1e-3f);
                float rd = 2 * vxy * yl / p2;
                if (v < 0) rd = -rd;
                const float r = c.yaw_rate.host();
                float ky = d.p.yaw_gain.host();
                // T[2]: the gain falls as the steered wheels' mean slip angle nears their peak.
                if (t.steer_mode && amax != 0) ky *= std::clamp((amax - x.tires.mean_slip.host()) / amax, -1.0f, 1.0f);
                if (rd > 0 && r < rd) {
                    tgt += (rd - r) * ky;
                    if (tgt > amax && !session_->loose()) tgt = amax;
                } else if (rd < 0 && rd < r) {
                    tgt -= (r - rd) * ky;
                    if (tgt < -amax && !session_->loose()) tgt = -amax;
                }
            }
            tgt = std::clamp(tgt + dir, -45 * kDeg, 45 * kDeg);
            if (moving == -1) tgt = -tgt;
        } else {
            tgt = eps < -20 * kDeg ? -30 * kDeg : eps > 20 * kDeg ? 30 * kDeg : eps * 1.5f;
            if (moving == -1) tgt = -tgt;
        }
    }
    const float lock = c.p.max_lock.host();
    tgt = std::clamp(tgt, -lock, lock);
    const float step = d.p.steer_rate.host() * kStep;
    d.steer += std::clamp(tgt - d.steer, -step, step);

    // Throttle and brake.
    if (drive == 1) {
        if (brakeflag) d.brake = 0.09f;
        float aim = d.v_cur.host(), next = d.v_next.host();
        int mode = d.mode;
        if (d.follow >= 0 && d.cooldown < 120) {
            const float margin = d.status == 1 || d.status == 2 ? 20 * kKmh : d.status >= 3 ? 15 * kKmh : 5 * kKmh;
            const float vo = std::max(session_->record(size_t(d.follow)).speed.host() - margin, 30 * kKmh);
            mode = 4;
            if (vo < next) next = vo;
        }
        if (d.state == 2) {
            speed_control(d, 50 * kKmh, -0.5f);
            d.arc_ticks = 0;
        } else if (mode == 0) {
            d.throttle = 1, d.arc_ticks = 0;
        } else if (mode == 3) {
            speed_control(d, next, -2), d.arc_ticks = 0;
        } else if (mode == 4) {
            speed_control(d, next, -1), ++d.arc_ticks;
        } else {
            float tv = aim;
            if (!(st.wheels[0].surface & 1) && d.arc_ticks >= d.p.bonus_ticks) tv += d.p.corner_bonus.host();
            if (d.p.straight_bonus && aeps < d.p.bonus_angle.host())
                tv += d.p.bonus_speed.host() * (d.p.bonus_angle.host() - aeps) / d.p.bonus_angle.host();
            speed_control(d, tv, -0.5f);
            if (d.status == 0) {
                const float g = std::fabs(c.lat_g.host());
                if (g > 0.9f) d.status = 5;
                else if (g > 0.5f) d.status = mode == 1 ? 4 : 3;
            }
            ++d.arc_ticks;
        }
        d.arc_ticks = std::min(d.arc_ticks, 6000);
        d.throttle *= scale;
        if (5000.0f + 100.0f * float(ranks_[d.car]) < float(steps_) * 1000.0f / 60.0f) traffic(d);
    } else {
        if (drive == -1) d.reverse = true, d.throttle = 1;
        d.brake = brakeflag ? 1 : 0;
        d.arc_ticks = 0;
    }
}

// 0x365790 with 0x365688 / 0x365718: each computer car's time scale against the reference car (the first car the field
// does not drive): ahead of it slowed by up to the slow share, behind it hurried by up to the fast share, ramped from the
// start gap to the full gap (and fully a lap or more apart), times the overall share, at most 1.3.
void Field::rubber_band() {
    const Cars& cars = *session_;
    size_t ref = cars.size();
    for (size_t i = 0; i < cars.size(); ++i)
        if (!drivers_[i]) {
            ref = i;
            break;
        }
    const float lap = lines_.lap();
    for (auto& d : drivers_) {
        if (!d) continue;
        d->time_scale = 1;
        if (ref >= cars.size() || (pace_.slow == 0 && pace_.fast == 0)) continue;
        const double gap = (double(cars.state(ref).lap) - double(cars.state(d->car).lap)) * lap + double(cars.state(ref).course_v) - double(cars.state(d->car).course_v);
        const int laps_apart = std::abs(cars.state(ref).lap - cars.state(d->car).lap);
        auto ramp = [&](float start, float full, float g) {
            if (laps_apart > 0 || g >= full) return 1.0f;
            if (g <= start) return 0.0f;
            return full > start ? (g - start) / (full - start) : 1.0f;
        };
        float scale = gap > 0 ? 1 + pace_.fast * ramp(pace_.fast_start, pace_.fast_full, float(gap))
                              : 1 - pace_.slow * ramp(pace_.slow_start, pace_.slow_full, float(-gap));
        d->time_scale = std::min(scale * pace_.global, 1.3f);
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
    for (auto& d : drivers_)
        if (d) d->s = lines_.wrap_lap(cars.state(d->car).course_v);
    rubber_band();
    proximity();
}

void Field::decide_car(size_t car, race::CarInput& in) {
    if (car >= drivers_.size() || !drivers_[car]) return;
    Driver& d = *drivers_[car];
    incidents_after(d);
    decide(d);
    // The outputs into the controls (0x464F38): the throttle slews in the arcs.
    if (tables_.tuning.smooth_throttle == 0 && (d.mode == 1 || d.mode == 2)) {
        d.applied_throttle += std::clamp(d.throttle - d.applied_throttle, -kStep, kStep);
    } else d.applied_throttle = d.throttle;
    const ex::Car& c = session_->record(d.car);
    in = race::CarInput{};
    // The steering as the pad's share of the lock it allows at the car's speed (what a pad reads), and the angle.
    const float hi = c.lock_hi.host() > 0.01f ? c.lock_hi.host() : c.p.max_lock.host();
    const float lo = c.lock_lo.host() < -0.01f ? -c.lock_lo.host() : c.p.max_lock.host();
    const float share = d.steer >= 0 ? d.steer / std::max(hi, 1e-3f) : d.steer / std::max(lo, 1e-3f);
    in.steer = std::clamp(-share, -1.0f, 1.0f);
    in.steer_angle = d.steer; // the session takes the angle itself, past the pad's lock curve
    in.throttle = std::clamp(d.applied_throttle, 0.0f, 1.0f);
    in.brake = std::clamp(d.brake, 0.0f, 1.0f);
    in.reverse = d.reverse;
    in.computer = true;
    in.shift_lock = d.shift_lock;
}

void Field::drive(std::span<race::CarInput> inputs) {
    begin_step();
    for (auto& dp : drivers_)
        if (dp && dp->car < inputs.size()) decide_car(dp->car, inputs[dp->car]);
    end_step();
}

} // namespace gt4r::sim::ai
