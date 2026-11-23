#include "gt4r/sim/ai/drivers.h"

#include "gt4r/disc/disc.h"
#include "gt4r/sim/exact_bridge.h"
#include "sim/exact/tick.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace gt4r::sim::ai {

namespace ex = gt3r::sim::exact;

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
    float v_cur = 0, v_next = 0, blend = 0;
    int arc_ticks = 0;
    int state = 0;          // recovery: 0 racing, 1 backing out, 2 turning round
    int status = 0;         // 0x43: 1 nearing a braking point, 2 braking, 3 / 4 / 5 in an arc near the grip's limit
    int state_hold = 0;     // 0x6B: steps the state machine rests after a change
    s16 cooldown = 0;       // 0x18: the lane change's cooldown
    bool long_start = true; // 0x1A == 0: the standing start's long look ahead (5 s)
    float offset = 0;       // 0x54: the sideways offset of the aim (m, + right)
    u16 rand = 0;           // 0x68
    float corner_mult = 1, brake_mult = 1, alongside_mult = 1; // 0xB8 / 0xBC / 0xC0
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

// The course's set for a driveline class into a driver's parameters (0x466C20).
static Params params_from(const std::array<u8, 0x80>& s, int cls, const Tuning& t) {
    Params p;
    const auto b = [&](size_t i) { return float(s[i]); };
    p.brake = b(0) * 0.01f * t.brake_global;
    p.corner_scale = b(1) * 0.01f;
    p.corner_offset = (b(2) - 100.0f) * kKmh;
    p.yaw_gain = b(3) * 5.0f * 0.01f;
    p.sat_throttle = b(4) * 0.01f, p.sat_brake = b(5) * 0.01f;
    p.look_speed = s[6] ? 1.0f / (b(6) * 2.0f * kKmh) : 0.0f;
    p.look_min = b(7), p.look_max = b(8);
    p.front_weight = b(0xE) * 0.01f;
    p.corner_bonus = b(0xF) * kKmh;
    p.steer_rate = b(0x10) * 10.0f * kDeg;
    p.bonus_ticks = int(s[0x11]) * 10;
    p.pit_speed = float(s[0x12] ? s[0x12] : 80) * kKmh;
    p.throttle = 1;
    if (s[0x1C] && s[0x1D]) {
        p.straight_bonus = true;
        p.bonus_angle = b(0x1C) * kDeg;
        p.bonus_speed = b(0x1D) * kKmh;
        const u8 cb = t.class_bonus[size_t(cls) & 3];
        if (cb != 0 && cb != 100) p.bonus_speed *= float(cb) / 100.0f;
    }
    if (s[0x1B] == 0x20)
        for (size_t i = 0; i < 3; ++i)
            if (s[0x20 + i] != 0 && s[0x20 + i] != 100) p.grip[i] = b(0x20 + i) / 100.0f;
    return p;
}

Field::Field(const Session& session, const course::AiData& data, const Program& program, std::span<const size_t> computer, const Pace& pace)
    : session_(&session), tables_(Tables::load(program)), lines_(data, session.ground(), tables_), pace_(pace) {
    if (program.known())
        for (u32 m = 0; m < 24; ++m) soft_[m] = program.w(Program::kSoft + 4 * m);
    else
        soft_[2] = soft_[3] = soft_[4] = soft_[6] = 1;
    const size_t n = session.cars().size();
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
        std::array<u8, 0x80> set{};
        if (const auto& s = data.sets[size_t(d->drive_class)]) set = *s;
        else std::copy(tables_.builtin[session.loose() ? 1 : 0].begin(), tables_.builtin[session.loose() ? 1 : 0].end(), set.begin());
        d->p = params_from(set, d->drive_class, tables_.tuning);
        const ex::Car& c = session.record(car);
        const float mass = c.p.mass.host();
        if (mass > 0) d->p.downforce = (c.p.downforce_coef[0].host() + c.p.downforce_coef[1].host()) / mass;
        // The event's changes (0x467148): each class's percents, then the drivers' spread by grid index.
        int thr = pace.throttle[size_t(d->drive_class)], brk = pace.brake[size_t(d->drive_class)], cs = pace.corner[size_t(d->drive_class)];
        if (thr == 0 || thr > 100) thr = 100;
        if (brk == 0) brk = 100;
        if (cs == 0) cs = 100;
        int off = 0;
        if (tables_.driver_spread && d->control == 2) {
            const int idx = d->index;
            if (!tables_.tuning.brake_spread) brk -= (idx % 3) * 7;
            if (!tables_.tuning.corner_spread) cs -= (idx * 4) & 4, off = -2 * ((idx * 7) % 5);
            d->p.yaw_gain *= float(((idx + 1) % 3) * 3 + 97) * 0.01f;
        }
        if (thr != 100) d->p.throttle = float(thr) * 0.01f;
        if (brk != 100) d->brake_mult *= float(brk) * 0.01f;
        if (cs != 100) d->corner_mult *= float(cs) * 0.01f;
        if (off) d->p.corner_offset += float(off) * kKmh;
        // The random draws (0x4639E0): seeded by the grid index for a computer car, then one step.
        d->rand = u16(d->control == 2 ? d->index + 1 : 1);
        d->rand = u16(((u32(d->rand) * 0x41C64E6Du + 0x3039u) >> 16) & 0x7FFF);
        d->long_start = pace.standing_start && !session.loose();
        // The first line: the main line, or the nearest of the others to the car.
        d->line = d->prev_line = lines_.resolve(6);
        d->steer = 0;
        drivers_[car] = std::move(d);
    }
    for (auto& d : drivers_)
        if (d && d->line >= 0) {
            const race::CarState& st = session.cars()[d->car];
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
    o.v_cur = d->v_cur, o.v_next = d->v_next, o.steer = d->steer, o.lock = session_->record(car).lock_hi.host(), o.steer_cmd = session_->record(car).steer_cmd.host(), o.throttle = d->applied_throttle, o.brake = d->brake, o.contact = d->contact;
    const ex::Car& c = session_->record(car);
    float x, y, h;
    if (d->line >= 0 && lines_.evaluate(d->line, d->next, d->s, x, y, h)) {
        const float fx = c.fwd.x.host(), fy = c.fwd.y.host(), hd = std::atan2(-fx, fy);
        o.line_offset = std::cos(hd) * (c.pos.x.host() - x) + std::sin(hd) * (c.pos.y.host() - y);
    }
    return o;
}

// ---- the plan --------------------------------------------------------------------------------------------------

float Field::corner_speed(const Driver& d, int i) {
    const ex::Car& c = session_->record(d.car);
    const std::vector<LinePoint>& pts = lines_.line(d.line);
    if (pts.empty()) return c.p.top_speed.host();
    const LinePoint& pt = pts[size_t(i) % pts.size()];
    const int row = core_surface(pt.surface);
    const u8 tf = c.p.tire_index[0][size_t(row) & 15], tr = c.p.tire_index[1][size_t(row) & 15];
    const ex::TireRecord& rf = c.p.tire[std::min<size_t>(tf, 5)];
    const ex::TireRecord& rr = c.p.tire[std::min<size_t>(tr, 5)];
    const float w = d.p.front_weight;
    const float g = d.p.grip[std::min<size_t>(tf % 3, 2)];
    float a = (w * rf.mu[size_t(row)].host() * rf.grip.host() + (1 - w) * rr.mu[size_t(row)].host() * rr.grip.host()) * g;
    const float r = pt.radius;
    a *= std::fabs(r);
    if ((r >= 0 && pt.bank < 0) || (r < 0 && pt.bank > 0)) a /= (1 - std::sin(std::fabs(pt.bank)));
    const float e = pt.crest ? 0 : d.p.aero_bonus;
    float q = 1 + e, v = 0;
    for (int k = 0; k < 3; ++k) {
        v = std::sqrt(std::max(a * q, 0.0f));
        q = d.p.downforce * v * v + 1 + e;
    }
    float scale = d.p.corner_scale;
    float m = d.control == 0 ? 1 : d.corner_mult;
    const Tuning& t = tables_.tuning;
    if (t.corner_spread && (d.control == 1 || d.control == 2)) {
        const float lo = t.corner_lo.at(m), hi = t.corner_hi.at(m);
        m += lo + (hi - lo) * float(d.rand) / 32768.0f;
    }
    scale *= m;
    if (t.alongside_slow && (d.zone[kFrontLeft] || d.zone[kLeft] || d.zone[kRight] || d.zone[kFrontRight]))
        scale *= 1 - t.alongside_share * (d.control == 0 ? 1 : d.alongside_mult);
    const float offset = d.p.corner_offset + (pt.speed_adjust == -128 ? 0.0f : float(pt.speed_adjust) * kKmh);
    return std::clamp(scale * v + offset, 20 * kKmh, std::max(c.p.top_speed.host(), 20 * kKmh));
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
    d.v_next = d.next_kind == 0 ? c.p.top_speed.host() : corner_speed(d, i);
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
    d.v_next = d.next_kind ? corner_speed(d, cur) : c.p.top_speed.host();
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
float Field::braking(Driver& d) {
    d.status = 0;
    if (d.line == CourseLines::kPitStop) return 1;
    const ex::Car& c = session_->record(d.car);
    const std::vector<LinePoint>& pts = lines_.line(d.line);
    const int count = int(pts.size());
    if (count == 0) return 1;
    const float lap = lines_.lap(), s = d.s;
    float mult = 1;
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
    const float v = c.speed.host();
    if (d.mode == 3 || d.mode == 4) d.status = 2;
    else if (d.next_kind != 0 && d.v_next < v) {
        float kb = d.p.brake;
        float m = d.control == 0 ? 1 : d.brake_mult;
        const Tuning& t = tables_.tuning;
        if (t.brake_spread && (d.control == 1 || d.control == 2)) {
            const float lo = t.brake_lo.at(m), hi = t.brake_hi.at(m);
            m += lo + (hi - lo) * float(d.rand) / 32768.0f;
        }
        kb *= m;
        const LinePoint& q = pts[size_t(bp)];
        const int row = core_surface(q.surface);
        const u8 tf = c.p.tire_index[0][size_t(row) & 15];
        const ex::TireRecord& rf = c.p.tire[std::min<size_t>(tf, 5)];
        float mu = rf.mu[size_t(row)].host() * d.p.grip[std::min<size_t>(tf % 3, 2)];
        const float width = c.p.width[0].host();
        if (width > 0) mu *= ex::lookup(rf.load_curve, gt3r::ee::F(c.p.weight.host() * 0.5f / width)).host();
        mu *= rf.brake_peak.host() * c.p.cam_offset[0].host();
        const float vn = d.v_next;
        const float e = q.crest ? 0 : d.p.aero_bonus;
        const float qh = 1 + d.p.downforce * v * v + e, ql = 1 + d.p.downforce * vn * vn + e;
        const float coef = std::max(c.p.brake_coef.host(), 1.0f / std::max(mu * 9.8f * (qh + ql), 1e-3f)) * (1 - std::sin(q.slope));
        float dist = (v + vn) * (v - vn) * coef;
        dist *= std::max(0.1f, 1.0f + float(q.brake_adjust) * 0.01f);
        if (kb > 1) dist /= kb;
        if (d.control == 1 || d.control == 2) dist += float(d.chain);
        if (d.cooldown == 0 && d.contact > 0) dist += 10;
        if (d.follow >= 0 && session_->cars()[size_t(d.follow)].brake > 0) dist += 8;
        const float to = std::hypot(q.x - c.pos.x.host(), q.y - c.pos.y.host());
        if (dist < to) {
            const float soft = kb < 1 ? dist * (1 / kb - 1) : 0;
            const float margin = to - dist;
            if (margin < soft + 25) d.status = 1;
            if (soft > 0 && margin < soft) mult = margin / soft;
        } else {
            d.mode = d.mode ? 4 : 3;
            d.status = 2;
        }
    }
    if (d.line != CourseLines::kPitLane && d.p.throttle < 1) {
        const float wot = c.torque_wot.host(), fr = c.torque_friction.host(), sum = wot + fr;
        mult *= sum != 0 ? (d.p.throttle * wot + fr) / sum : d.p.throttle;
    }
    return mult;
}

// 0x464770: throttle and brake towards a speed.
void Field::speed_control(Driver& d, float vt, float gain) {
    const float v = session_->record(d.car).speed.host();
    const float dv = std::clamp(v - (vt - 2 * kKmh), -10 * kKmh, 10 * kKmh);
    d.throttle = std::clamp(0.75f + gain * dv, 0.0f, 1.0f);
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
    const auto& states = session_->cars();
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
                c.speed.host(), lines_.wrap_lap(states[i].course_v)};
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
    const race::CarState& st = session_->cars()[d.car];
    const Tuning& t = tables_.tuning;
    // Begin (0x464ED8): the outputs cleared, the steering from the car's command.
    d.throttle = 0, d.brake = 0, d.reverse = false;
    d.steer = c.steer_cmd.host();
    if (!t.blend_by_distance) d.blend = std::max(0.0f, d.blend - kStep);
    if (d.line < 0) return;
    const float scale = braking(d);
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
        look = v <= 0 ? d.p.look_min : std::min(1.0f, d.p.look_speed * v) * (d.p.look_max - d.p.look_min) + d.p.look_min;
        const float ms = float(steps_) * 1000.0f / 60.0f;
        if (d.long_start) {
            if (5000 - ms > 0) look += (5000 - ms) * 200 / 5000;
        } else if (2000 - ms > 0) look += (2000 - ms) * 40 / 2000;
    }
    float ax, ay;
    target(d, d.line, look, ax, ay);
    if (d.blend > 0 && lines_.has(d.prev_line)) {
        float ox, oy;
        target(d, d.prev_line, look, ox, oy);
        if (t.blend_by_distance) {
            const float gap = std::hypot(ox - ax, oy - ay), step = t.blend_rate * kStep;
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
            o = e * (d.line == 3 || d.line == 4 ? t.side_share_pit : t.side_share);
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
            const float lim = g >= t.grip_hi ? 0 : g > t.grip_lo ? t.offset_limit * (t.grip_hi - g) / (t.grip_hi - t.grip_lo) : t.offset_limit;
            o = std::clamp(o, -lim, lim);
        }
        const float step = t.offset_rate * kStep;
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
            float amax = 0, dir = 0;
            for (int w = 0; w < 2; ++w) {
                const int row = c.w[size_t(w)].surface & 15;
                const u8 ti = c.p.tire_index[0][size_t(row)];
                amax += c.p.tire[std::min<size_t>(ti, 5)].peak_slip.host() * 0.5f;
                dir += c.w[size_t(w)].slip_dir.host() * 0.5f;
            }
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
                const float ky = d.p.yaw_gain;
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
    const float step = d.p.steer_rate * kStep;
    d.steer += std::clamp(tgt - d.steer, -step, step);

    // Throttle and brake.
    if (drive == 1) {
        if (brakeflag) d.brake = 0.09f;
        float aim = d.v_cur, next = d.v_next;
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
            if (!(st.wheels[0].surface & 1) && d.arc_ticks >= d.p.bonus_ticks) tv += d.p.corner_bonus;
            if (d.p.straight_bonus && aeps < d.p.bonus_angle) tv += d.p.bonus_speed * (d.p.bonus_angle - aeps) / d.p.bonus_angle;
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
    const auto& states = session_->cars();
    size_t ref = states.size();
    for (size_t i = 0; i < states.size(); ++i)
        if (!drivers_[i]) {
            ref = i;
            break;
        }
    const float lap = lines_.lap();
    for (auto& d : drivers_) {
        if (!d) continue;
        d->time_scale = 1;
        if (ref >= states.size() || (pace_.slow == 0 && pace_.fast == 0)) continue;
        const double gap = (double(states[ref].lap) - double(states[d->car].lap)) * lap + double(states[ref].course_v) - double(states[d->car].course_v);
        const int laps_apart = std::abs(states[ref].lap - states[d->car].lap);
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
    const race::CarState& st = session_->cars()[d.car];
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
    const auto& states = session_->cars();
    for (size_t j = 0; j < states.size(); ++j) {
        if (j == d.car) continue;
        const float dx = states[j].position.x - st.position.x, dz = states[j].position.z - st.position.z;
        const float along = dx * fx + dz * fz, across = std::fabs(dx * fz - dz * fx);
        const ex::Car& o = session_->record(j);
        if (across < half + o.p.body_width.host() * 0.5f - 0.1f && std::fabs(along) < front + o.p.body_rear.host()) {
            car = true;
            if (along > 0) d.hit_front = true;
            else d.hit_rear = true;
        }
    }
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

void Field::drive(std::span<race::CarInput> inputs) {
    const auto& states = session_->cars();
    // The order by laps and distance (1 first).
    {
        std::vector<size_t> order(states.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const double pa = double(states[a].lap) * lines_.lap() + states[a].course_v, pb = double(states[b].lap) * lines_.lap() + states[b].course_v;
            return pa > pb;
        });
        for (size_t r = 0; r < order.size(); ++r) ranks_[order[r]] = int(r + 1);
    }
    for (auto& d : drivers_)
        if (d) d->s = lines_.wrap_lap(states[d->car].course_v);
    rubber_band();
    proximity();
    for (auto& dp : drivers_) {
        if (!dp || dp->car >= inputs.size()) continue;
        Driver& d = *dp;
        incidents_after(d);
        decide(d);
        // The outputs into the controls (0x464F38): the throttle slews in the arcs.
        if (tables_.tuning.smooth_throttle == 0 && (d.mode == 1 || d.mode == 2)) {
            d.applied_throttle += std::clamp(d.throttle - d.applied_throttle, -kStep, kStep);
        } else d.applied_throttle = d.throttle;
        const ex::Car& c = session_->record(d.car);
        race::CarInput in;
        // The steering as the pad's share of the lock it allows at the car's speed (what a pad reads), and the angle.
        const float hi = c.lock_hi.host() > 0.01f ? c.lock_hi.host() : c.p.max_lock.host();
        const float lo = c.lock_lo.host() < -0.01f ? -c.lock_lo.host() : c.p.max_lock.host();
        const float share = d.steer >= 0 ? d.steer / std::max(hi, 1e-3f) : d.steer / std::max(lo, 1e-3f);
        in.steer = std::clamp(-share, -1.0f, 1.0f);
        in.steer_angle = d.steer; // the session takes the angle itself, past the pad's lock curve
        in.throttle = std::clamp(d.applied_throttle, 0.0f, 1.0f);
        in.brake = std::clamp(d.brake, 0.0f, 1.0f);
        in.reverse = d.reverse;
        inputs[d.car] = in;
    }
    ++steps_;
}

} // namespace gt4r::sim::ai
