#include "gt4r/sim/ai/drivers_prologue.h"

#include "base/ee_double.h"
#include "base/ee_math.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/program_prologue.h"
#include "gt4r/sim/rules_build.h"
#include "gt4r/sim/rules_input.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

#include <cmath>
#include <numbers>

namespace gt4r::sim::ai::prologue {

namespace {

// The program's constants (the build folds its products and quotients toward zero).
const F kZero, kOne = F::raw(0x3F800000), kHalf = F::raw(0x3F000000), kTwo = F::raw(0x40000000);
const F kHundredth = F::raw(0x3C23D70A), kKmh = F::raw(0x3E8E38E3), kDeg = F::raw(0x3C8EFA34), kHundred = F::raw(0x42C80000);
const F kmh2 = F::raw(0x3F0E38E3), kmh3 = F::raw(0x3F555554), kmh5 = F::raw(0x3FB1C71B), kmh20 = F::raw(0x40B1C71B), kmh30 = F::raw(0x41055554),
        kmh50 = F::raw(0x415E38E2);
const F deg10 = F::raw(0x3E32B8C1), deg20 = F::raw(0x3EB2B8C1), deg30 = F::raw(0x3F060A90), deg40 = F::raw(0x3F32B8C1), deg45 = F::raw(0x3F490FD9),
        deg50 = F::raw(0x3F5F66F1), deg60 = F::raw(0x3F860A90), deg80 = F::raw(0x3FB2B8C1);
const F kTwoPi = F::raw(0x40C90FDA), kPi = F::raw(0x40490FDA);

F int_f(s32 v) { return gt3r::ee::int_to_f(v); }
F angle_diff(const ex::Car& c, F a, F b) { return ex::angle_diff(a, b, c.behaviour.rule(gt3r::sim::Rule::AngleWrap)); }

bool steered(const rules::WheelLayout& l, int w) { return w < int(l.count) && gt3r::s8(l.fore_aft[size_t(w)]) > 0; }

} // namespace

DriverTables driver_tables(const Program& p) {
    DriverTables t;
    if (p.known() || !PrologueProgram::locate(p).recognized()) return t;
    t.found = true;
    for (u32 r = 0; r < 8; ++r)
        for (u32 k = 0; k < 6; ++k) t.fallback[r][k] = s8(p.b(0x5066F0 + 6 * r + k));
    for (u32 k = 0; k < 8; ++k) t.order[k] = p.b(0x5066E8 + k) & 7;
    for (u32 k = 0; k < 3; ++k) t.nearest[k] = s32(p.w(0x506510 + 4 * k));
    for (u32 k = 0; k < 6; ++k) t.grid_line[k] = s32(p.w(0x504518 + 4 * k));
    for (u32 s = 0; s < 3; ++s)
        for (u32 k = 0; k < 0x20; ++k) t.builtin[s][k] = p.b(0x5065B0 + 0x20 * s + k);
    t.steer_mode = p.b(0x5066B8 + 2);
    t.own_saturation = p.b(PrologueProgram::kStatic + 0xDB);
    t.bonus_angle = p.b(PrologueProgram::kStatic + 0xDC), t.bonus_speed = p.b(PrologueProgram::kStatic + 0xDD);
    return t;
}

// ---- the lines ---------------------------------------------------------------------------------------------------

namespace {

// The road's segment under (x, y) from the hint, -1 off the road (0x428AD0).
s32 segment_at(const ex::CourseQuery& q, s32 hint, F x, F y) {
    ex::Ground g;
    g.x = x, g.h = kHundred, g.z = -y, g.seg = hint;
    q.ground(g);
    return g.miss ? -1 : g.seg;
}

// The road's height under (x, y) (0x428E58; off the road 0, the hint kept).
F height_at(const ex::CourseQuery& q, s32& seg, bool& miss, F x, F y) {
    ex::Ground g;
    g.x = x, g.h = kHundred, g.z = -y, g.seg = seg;
    q.ground(g);
    miss = g.miss != 0;
    if (miss) return kZero;
    seg = g.seg;
    return g.height;
}

// 0x428B30: point i's segment, traced from the one before along the line (every 10 m, or 10 degrees of an arc).
s32 trace(const gt3r::sim::Behaviour& bh, const ex::CourseQuery& q, std::vector<ex::AiPoint>& p, size_t i, s32 seg, int line) {
    ex::AiPoint& a = p[i - 1];
    const ex::AiPoint& b = p[i];
    const F ten = F::raw(0x41200000);
    if (a.kind == 0) {
        const F dx = b.x - a.x, dy = b.y - a.y;
        s32 n = gt3r::ee::to_int(gt3r::ee::sqrt(dx * dx + dy * dy) / ten);
        if (n == 0) n = 1;
        for (s32 k = 1; k <= n; ++k) {
            const F t = int_f(k) / int_f(n);
            seg = segment_at(q, seg, a.x + t * dx, a.y + t * dy);
        }
        return seg;
    }
    const F s = -gt3r::ee::sin(a.heading), c = gt3r::ee::cos(a.heading);
    F r = a.radius;
    const F cx = a.x - c * r, cy = a.y + s * r;
    F a0 = ex::atan2(bh, cx - a.x, a.y - cy), a1 = ex::atan2(bh, cx - b.x, b.y - cy), steps;
    if (kZero < r) {
        a.kind = 1;
        if (a1 < a0) a1 = a1 + kTwoPi;
        steps = (a1 - a0) / deg10;
    } else {
        a.kind = 2;
        r = -r;
        if (a0 < a1) a0 = a0 + kTwoPi;
        steps = (a0 - a1) / deg10;
    }
    s32 n = gt3r::ee::to_int(steps);
    if (n == 0) n = 1;
    if (line == 5) a.kind = 1;
    const F sweep = a1 - a0;
    for (s32 k = 1; k <= n; ++k) {
        const F angle = a0 + (int_f(k) / int_f(n)) * sweep;
        seg = segment_at(q, seg, cx - gt3r::ee::sin(angle) * r, cy + gt3r::ee::cos(angle) * r);
    }
    return seg;
}

// 0x428ED0: the road under a point: its height, surface, slope and bank along and across the heading.
void road_under(const gt3r::sim::Behaviour& bh, const ex::CourseQuery& q, ex::AiPoint& p, s32 seg) {
    ex::Ground g;
    g.x = p.x, g.h = kHundred, g.z = -p.y, g.seg = seg;
    q.ground(g);
    if (g.miss) return;
    s32 hint = g.seg;
    const F at = g.height, s = gt3r::ee::sin(p.heading) * kHalf, c = gt3r::ee::cos(p.heading) * kHalf;
    bool m1 = false, m2 = false, m3 = false, m4 = false;
    const F h1 = height_at(q, hint, m1, p.x - s, p.y + c), h2 = height_at(q, hint, m2, p.x + s, p.y - c);
    const F h3 = height_at(q, hint, m3, p.x - c, p.y - s), h4 = height_at(q, hint, m4, p.x + c, p.y + s);
    if ((m1 && m2) || (m3 && m4)) return;
    p.slope = m1 ? ex::atan2(bh, at - h2, kHalf) : m2 ? ex::atan2(bh, h1 - at, kHalf) : ex::atan2(bh, h1 - h2, kOne);
    p.bank = m3 ? ex::atan2(bh, at - h4, kHalf) : m4 ? ex::atan2(bh, h3 - at, kHalf) : ex::atan2(bh, h3 - h4, kOne);
    p.height = at, p.surface = s8(ex::surface_index(g)), p.x13 = u8(g.x32);
}

} // namespace

ex::AiCourse driver_course(const ex::CourseQuery& q, const course::AiData& data, const DriverTables& t) {
    ex::AiCourse k;
    k.behaviour = rules::behaviour();
    k.lap_length = q.lap_length();
    k.fallback = t.fallback;
    k.table = true;
    const F fixed = F::raw(0x39800000); // 1 / 4096
    for (size_t l = 0; l < 8; ++l)
        for (const course::AiLinePoint& f : data.lines[l]) {
            ex::AiPoint p;
            const auto raw = [](float v) { return int_f(s32(std::lround(double(v) * 4096))); };
            p.kind = f.kind, p.brake_adjust = f.brake_adjust, p.speed_adjust = f.speed_adjust;
            p.heading = (int_f(s32(std::lround(double(f.heading) * 4096.0 / (2 * std::numbers::pi)))) * kTwoPi) * fixed + kPi;
            p.x = (-raw(f.x)) * fixed, p.y = (-raw(f.y)) * fixed, p.radius = raw(f.radius) * fixed;
            p.seg = s16(f.segment);
            k.lines[l].push_back(p);
        }
    // (the points' segments from the file when any has one; else traced along the lines)
    bool file_segments = false;
    for (const auto& line : k.lines)
        for (const ex::AiPoint& p : line) file_segments = file_segments || p.seg != 0;
    for (const u8 l : t.order) {
        std::vector<ex::AiPoint>& p = k.lines[l];
        bool past_half = false;
        s32 seg = -1;
        for (size_t i = 0; i < p.size(); ++i) {
            ex::AiPoint& a = p[i];
            if (file_segments) seg = a.seg;
            else if (l == 4 && i == 0) { // 0x4291A8: the first of the main line's points' segments, from its last back, the road is met from
                const int m = resolve(k, 6);
                if (m >= 0)
                    for (size_t j = k.lines[size_t(m)].size(); j-- > 0;) {
                        s32 hint = k.lines[size_t(m)][j].seg;
                        bool miss = false;
                        height_at(q, hint, miss, a.x, a.y);
                        if (!miss) {
                            seg = hint;
                            break;
                        }
                    }
            } else if (l == 4 || i == 0) seg = segment_at(q, seg, a.x, a.y);
            else seg = trace(k.behaviour, q, p, i, seg, l);
            a.seg = s16(seg);
            a.dist = q.progress(a.x, kHundred, -a.y, seg);
            a.height = kZero, a.slope = kZero, a.bank = kZero, a.surface = 0, a.x13 = 0;
            if (l != 5) road_under(k.behaviour, q, a, seg);
            if (l == 4) {
                if (a.dist < k.lap_length * kHalf) past_half = true;
                if (past_half) a.dist = a.dist + k.lap_length;
            }
        }
    }
    k.pit_lines = !k.lines[3].empty() && !k.lines[4].empty();
    return k;
}

// ---- the parameters ----------------------------------------------------------------------------------------------

void driver_set(ex::Driver& d, const u8* s) {
    const auto b = [&](size_t i) { return int_f(s32(s[i])); };
    d.yaw_gain = int_f(s32(s[3]) * 5) * kHundredth;
    d.look_min = b(7), d.look_max = b(8);
    d.throttle_scale = kOne;
    d.look_speed = kOne / (int_f(s32(s[6]) * 2) * kKmh);
    d.brake_factor = kHundred / b(0);
    d.speed_scale = b(1) * kHundredth;
    d.speed_offset = int_f(s32(s[2]) - 100) * kKmh;
    d.sat_throttle = b(4) * kHundredth, d.sat_brake = b(5) * kHundredth;
    d.front_weight = b(0xE) * kHundredth;
    d.corner_bonus = b(0xF) * kKmh;
    d.steer_rate = int_f(s32(s[0x10]) * 10) * kDeg;
    d.bonus_ticks = s32(s[0x11]) * 10;
    d.pit_speed = int_f(s32(s[0x12] ? s[0x12] : 80)) * kKmh;
    d.aids = rules::driver_block(s);
}

void driver_downforce(ex::Driver& d, const ex::Params& p) { d.downforce = (p.downforce_coef[0] + p.downforce_coef[1]) / p.weight; }

void driver_event(ex::Driver& d, int throttle, int brake, int corner, int control, int index) {
    if (throttle == 0 || throttle > 100) throttle = 100;
    if (brake == 0) brake = 100;
    if (corner == 0) corner = 100;
    int off = 0;
    if (control == 2) {
        corner -= (index * 4) & 4;
        brake -= (index % 3) * 7;
        off = -2 * ((index * 7) % 5);
        d.yaw_gain = d.yaw_gain * (int_f(((index + 1) % 3) * 3 + 97) * kHundredth);
    }
    if (throttle != 100) d.throttle_scale = int_f(throttle) * kHundredth;
    if (brake != 100) d.brake_factor = d.brake_factor * (kHundred / int_f(brake));
    if (corner != 100) d.speed_scale = d.speed_scale * (int_f(corner) * kHundredth);
    if (off != 0) d.speed_offset = d.speed_offset + int_f(off) * kKmh;
}

void driver_start(ex::Car& c, const Step& in, const DriverTables& t, int line) {
    ex::Driver& d = c.ai;
    const ex::AiCourse& k = *in.course;
    d.state = 0;
    d.v_cur = d.v_next = c.p.top_speed;
    if (line == -1) { // 0x4249A8: the nearest of the three within 100 m
        F best = kHundred;
        line = 6;
        for (const s32 want : t.nearest) {
            const int r = resolve(k, want);
            if (r != want) continue;
            F x, y, h;
            evaluate(k, r, 0, c.f3e4, x, y, h);
            const F dx = x - c.pos.x, dy = y - c.pos.y;
            const F dist = gt3r::ee::sqrt(dx * dx + dy * dy);
            if (dist < best) best = dist, line = r;
        }
    }
    // (the line resolved twice: a line the course has none of the row of asks for line 6's row; with none of that either
    // the program's line byte would be 0xFF, which no course here has)
    const int r = resolve(k, resolve(k, line));
    Step at = in;
    at.wear = {}; // (the set-up's plan: no wear)
    if (r >= 0) set_line(c, at, r);
    d.pit_limit = 0, d.arc_ticks = 0, d.zone = {}, d.contact = kZero;
    d.gear_braking = 0, d.gear_hint = -1; // (0x4289E8: no suggested gear)
    d.cooldown = 0, d.stop = 0;
}

// ---- the plan ----------------------------------------------------------------------------------------------------

int lane_left(const ex::AiCourse& k, int line, int hint, F s) {
    const auto has = [&](int l) { return !k.lines[size_t(l)].empty(); };
    // 0x42A3C8 / 0x42A408: the left of line 1 is the main line (line 2 without it), of the main line line 2; the right of line
    // 2 the main line (line 1 without it), of the main line line 1.
    if (side_swapped(k, hint, s)) return line == 2 ? (has(6) ? 6 : has(1) ? 1 : -1) : line == 6 && has(1) ? 1 : -1;
    return line == 1 ? (has(6) ? 6 : has(2) ? 2 : -1) : line == 6 && has(2) ? 2 : -1;
}

int lane_right(const ex::AiCourse& k, int line, int hint, F s) {
    const auto has = [&](int l) { return !k.lines[size_t(l)].empty(); };
    if (!side_swapped(k, hint, s)) return line == 2 ? (has(6) ? 6 : has(1) ? 1 : -1) : line == 6 && has(1) ? 1 : -1;
    return line == 1 ? (has(6) ? 6 : has(2) ? 2 : -1) : line == 6 && has(2) ? 2 : -1;
}

F braking(ex::Car& c, const Step& in) {
    ex::Driver& d = c.ai;
    const ex::AiCourse& k = *in.course;
    F mult = kOne;
    d.status = 0;
    if (d.line == 4) return mult;
    const std::vector<ex::AiPoint>& p = k.lines[d.line & 7];
    const int count = int(p.size());
    if (count == 0) return mult;
    const int n = d.next;
    bool changed = true;
    if (n == 0 && k.lap_length * kHalf < c.f3e4) d.next = u8(count);
    else if (n < count && p[size_t(n)].dist <= c.f3e4) d.next = u8(n + 1);
    else if (n == count && c.f3e4 < k.lap_length * kHalf) d.next = 0;
    else if (n != 0 && c.f3e4 < p[size_t(n - 1)].dist) d.next = u8(n - 1);
    else changed = false;
    if (changed) next_segment(c, in);
    const F vn = d.v_next;
    if (d.mode == 3 || d.mode == 4) d.status = 2;
    else if (d.next_kind != 0 && vn < c.speed) {
        const F v = c.speed, kb = d.brake_factor;
        const ex::AiPoint& q = p[size_t(int(d.next) != count ? d.next : 0)];
        const rules::WheelLayout& l = in.part->layout;
        const ex::TireRecord& rf = tire(c.p, l, l.group_first[0], q.surface);
        F mu = rf.mu[size_t(q.surface) & 15] * ex::lookup_bisect(rf.load_curve, (c.p.weight * kHalf) / c.p.width[size_t(l.group_axle[0] & 1)]);
        mu = (mu * rf.brake_peak) * c.p.cam_offset[0];
        if (!(c.x535 & 0x10)) {
            F sum;
            int wheels = 0;
            for (int w = 0; w < 4; ++w)
                if (steered(l, w)) sum = sum + c.w[size_t(w)].wear_grip, ++wheels;
            if (wheels) mu = (mu * sum) / int_f(wheels);
        }
        const F qn = (d.downforce * vn) * vn + kOne, qv = (d.downforce * v) * v + kOne;
        const F g = ((mu * F::raw(0x411CCCCC)) * (qv + qn)) * kHalf;
        F coef = c.p.brake_coef;
        const F least = kOne / (g + g);
        if (coef < least) coef = least;
        coef = coef * (kOne - gt3r::ee::sin(q.slope));
        F dist = ((v + vn) * (v - vn)) * coef;
        const gt3r::sim::Behaviour& h = c.behaviour;
        F adjust = ex::narrow(h, ex::add(h, ex::mul(h, ex::widen(h, int_f(q.brake_adjust)), gt3r::ee::D::raw(0x3F847AE147AE147Bull)), gt3r::ee::D::raw(0x3FF0000000000000ull)));
        if (const F lo = F::raw(0x3DCCCCCC); adjust < lo) adjust = lo;
        dist = dist * adjust;
        if (kb < kOne) dist = dist * kb;
        if (d.cooldown == 0 && kZero < d.contact) dist = dist + (d.contact * F::raw(0x41900000) + kTwo);
        const F dx = q.x - c.pos.x, dy = q.y - c.pos.y;
        const F to = gt3r::ee::sqrt(dx * dx + dy * dy);
        if (dist < to) {
            const F soft = kOne < kb ? dist * (kb - kOne) : kZero, margin = to - dist;
            if (margin < soft + F::raw(0x41C80000)) d.status = 1; // (0x4281A0: 25 m)
            if (kZero < soft && margin < soft) mult = margin / soft;
        } else {
            d.mode = d.mode != 0 ? 4 : 3;
            d.status = 2;
        }
    }
    if (d.line != 3) {
        const F ts = d.throttle_scale;
        const F sum = c.torque_wot + c.torque_friction;
        if (ts < kOne && !(sum == kZero)) mult = mult * ((ts * c.torque_wot + c.torque_friction) / sum);
        else mult = mult * ts;
    }
    return mult;
}

void speed_control(ex::Car& c, F vt, F gain) {
    ex::Driver& d = c.ai;
    const F thr = throttle_for(c.speed, vt - kmh2, F::raw(0x3F400000), gain);
    d.throttle = thr;
    if (!(d.mode == 3 || d.mode == 4)) vt = vt + kmh20;
    if (thr == kZero && vt < c.speed) d.brake = kOne;
    if (!(thr == kOne) && !(thr == kZero)) d.shift_lock = 12;
}

// ---- the stops ---------------------------------------------------------------------------------------------------

void stop(ex::Car& c, F steer) {
    ex::Driver& d = c.ai;
    begin(c);
    prologue::steer(c, steer);
    d.throttle = kZero;
    const F v = gt3r::ee::sqrt(c.vel.x * c.vel.x + c.vel.y * c.vel.y + c.vel.z * c.vel.z);
    const F w = gt3r::ee::sqrt(c.yaw_rate * c.yaw_rate + c.roll_rate * c.roll_rate + c.pitch_rate * c.pitch_rate);
    if (v < F::raw(0x3D8E38E3) && w < F::raw(0x3D56774E)) {
        d.brake = kZero;
        c.driver_kind = 8;
        return;
    }
    d.brake = c.driver_kind == 6 ? F::raw(0x3E4CCCCC) : kHalf;
}

void park(ex::Car& c) {
    begin(c);
    steer(c, kZero);
    c.ai.throttle = c.ai.brake = kZero;
}

// ---- traffic -----------------------------------------------------------------------------------------------------

void traffic(ex::Car& c, const Step& in) {
    ex::Driver& d = c.ai;
    const ex::AiCourse& k = *in.course;
    if (d.cooldown > 0) d.cooldown = s16(d.cooldown - 1);
    if (d.line == 4) return;
    const auto z = [&](int cell) { return gt3r::s8(d.zone[size_t(cell)]); }; // front, front left, front right, left, right, ..
    bool lanes = true;
    if (d.line == 3) {
        lanes = false;
        if ((z(3) != 0 && z(1) != 0) || (z(4) != 0 && z(2) != 0)) {
            if (c.speed < kmh30) d.cooldown = s16(d.cooldown + 1);
            else d.throttle = d.throttle * kHalf;
        }
    }
    if (d.status > 0 && (d.status < 3 || d.status == 5)) lanes = false;
    F contact = d.cooldown > 0 ? kZero : d.contact;
    if (kZero < d.blend) lanes = false;
    if (lanes && kZero < contact) {
        const int left = z(1) < 2 ? lane_left(k, d.line, d.next, c.f3e4) : -1, right = z(2) < 2 ? lane_right(k, d.line, d.next, c.f3e4) : -1;
        const int right_ok = right >= 0 && d.status != 4 ? right : -1;
        int pick;
        if (d.next_kind == 1) pick = left >= 0 ? left : right_ok;
        else if (d.next_kind == 2 && right >= 0) pick = right;
        else pick = left >= 0 && d.status != 3 ? left : right_ok;
        if (pick >= 0) {
            request_line(c, in, pick, false);
            d.cooldown = 180;
            contact = kZero;
        }
    }
    d.throttle = d.throttle * (kOne - contact);
    if (!lanes) return;
    const int l = z(3), r = z(4);
    if (z(0) + r + (z(2) + z(1)) + l == 0) {
        request_line(c, in, 6, false);
        return;
    }
    if ((l != 0) == (r != 0)) return;
    if (l != 0 && d.status != 4) {
        if (const int to = lane_right(k, d.line, d.next, c.f3e4); to != -1) request_line(c, in, to, false);
    } else if (r != 0 && d.status != 3) {
        if (const int to = lane_left(k, d.line, d.next, c.f3e4); to != -1) request_line(c, in, to, false);
    }
}

// ---- the decision ------------------------------------------------------------------------------------------------

void decide(ex::Car& c, const Step& in) {
    ex::Driver& d = c.ai;
    const ex::AiCourse& k = *in.course;
    const DriverTables& t = *in.tables;
    begin(c);
    if (c.driver_kind == 7) return stop(c, kZero);
    const F scale = braking(c, in);
    const F nsin = -gt3r::ee::sin(c.heading), cosh = gt3r::ee::cos(c.heading);

    // The look ahead: 5 m on the stop line and, slower than 30 km/h, off it (else 20), 10 / 40 m backing out and turning round;
    // racing by the speed, longer over a start's first seconds.
    F look = F::raw(0x40A00000);
    if (c.driver_kind == 3) {
    } else if (c.driver_kind == 4 && d.pit_limit != 0) {
        if (!(c.speed < kmh30)) look = F::raw(0x41A00000);
    } else if (d.state != 0) look = F::raw(d.state == 1 ? 0x41200000u : 0x42200000u);
    else {
        look = lookahead(d, c.speed);
        const bool far = c.control == 2 && !in.dirt && in.start_speed < 30;
        const s32 left = (far ? 5000 : 2000) - s32(in.clock / 3u);
        if (left > 0) look = look + (far ? int_f(left) * F::raw(0x43480000) / F::raw(0x459C4000) : int_f(left) * F::raw(0x42200000) / F::raw(0x44FA0000));
    }
    F x, y;
    look = target(c, k, d.line, d.next, look, x, y);
    if (kZero < d.blend) {
        F xo, yo;
        target(c, k, d.prev_line, d.next, look, xo, yo);
        const F w = d.blend;
        x = x + (xo - x) * w, y = y + (yo - y) * w;
    }
    const F eps = angle_diff(c, ex::atan2(c.behaviour, -x, y), c.heading);
    const F aeps = eps < kZero ? -eps : eps;

    // The drive: 1 forward, -1 back, 0 braking; then the recovery state.
    const u8 st = d.state;
    const F v = c.speed;
    F theta = deg60;
    if (st == 1) {
        if (kZero <= v) theta = deg10;
        else if (-kmh30 < v) theta = deg10 - (v * deg50) / kmh30;
    }
    int drive = 0;
    bool brake = false;
    if (aeps < theta || st == 2) {
        if (-kmh2 < v) drive = 1;
        else brake = true;
    } else if (kmh2 < v) brake = true;
    else drive = -1;
    if (st == 1) {
        if (aeps < theta) d.state = kmh3 < v ? 0 : 2;
        else if (in.hit_rear) d.state = 2;
    } else if (st == 2) {
        if (aeps < deg40 && kmh30 < v) d.state = 0;
        else if (in.hit_front) {
            F lx, ly, lh;
            F dh = line_heading(k, d.line, d.next, c.f3e4, lx, ly, lh) - c.heading;
            if (dh < kZero) dh = -dh;
            if (deg60 < dh || deg60 < aeps) d.state = 1;
        }
    } else {
        if (deg80 < aeps) d.state = 2;
        else if (in.hit_rear && deg45 < aeps) d.state = 2;
    }
    const int moving = kmh2 < v ? 1 : v < -kmh2 ? -1 : 0;

    // The steering's target; `over`: how far past the steered wheels' peak slip angle it asked to go.
    F aim_steer, over;
    if (moving != 0) {
        if (drive == 1) {
            const rules::WheelLayout& l = in.part->layout;
            F amax, dirs;
            int wheels = 0;
            for (int w = 0; w < 4; ++w)
                if (steered(l, w)) amax = amax + tire(c.p, l, w, gt3r::s8(c.w[size_t(w)].surface)).peak_slip, dirs = dirs + c.w[size_t(w)].slip_dir, ++wheels;
            F dir;
            if (wheels) amax = amax / int_f(wheels), dir = dir + dirs / int_f(wheels);
            if (d.state != 0) {
                if (deg80 < eps) aim_steer = amax;
                else if (eps < -deg80) aim_steer = -amax;
            }
            if (aim_steer == kZero) {
                const F vx = c.vel.x, vy = c.vel.y;
                const F vxy = gt3r::ee::sqrt(vx * vx + vy * vy), p2 = x * x + y * y;
                F cs = cosh, ns = nsin;
                if (!(vxy < kmh5)) {
                    const F a = ex::atan2(c.behaviour, -vx, vy);
                    ns = -gt3r::ee::sin(a), cs = gt3r::ee::cos(a);
                }
                F rd = ((vxy * F::raw(0xC0000000)) * (cs * x - ns * y)) / p2;
                if (c.speed < kZero) rd = -rd;
                const F r = c.yaw_rate;
                F ky = d.yaw_gain;
                if (t.steer_mode != 0) { // by the mean slip angle's share of the peak
                    F q = (-(in.part->tires.mean_slip - amax)) / amax;
                    if (kOne < q) q = kOne;
                    else if (q < -kOne) q = -kOne;
                    ky = ky * q;
                }
                if (kZero < rd) {
                    if (r < rd) {
                        aim_steer = aim_steer + (rd - r) * ky;
                        if (amax < aim_steer) {
                            over = aim_steer / amax;
                            if (!in.dirt) aim_steer = amax;
                        }
                    }
                } else if (rd < kZero && rd < r) {
                    aim_steer = aim_steer - (r - rd) * ky;
                    if (aim_steer < -amax) {
                        over = (-aim_steer) / amax;
                        if (!in.dirt) aim_steer = -amax;
                    }
                }
            }
            aim_steer = aim_steer + dir;
            if (deg45 < aim_steer) aim_steer = deg45;
            else if (aim_steer < -deg45) aim_steer = -deg45;
        } else aim_steer = eps < -deg20 ? -deg30 : deg20 < eps ? deg30 : eps * F::raw(0x3FBFFFFE);
        if (moving == -1) aim_steer = -aim_steer;
    }
    if (c.driver_kind == 6) return stop(c, aim_steer);
    if (c.control == 1 || c.control == 2 || c.driver_kind != 0) steer(c, aim_steer);

    // Throttle and brake.
    if (drive == 1) {
        d.pit_limit = 0;
        F aim = d.v_cur, next = d.v_next;
        u8 mode = d.mode;
        if (d.line == 4) aim = next = kmh20, mode = 3, d.pit_limit = 1; // the stop line
        else if (d.line == 3) { // the pit lane, between the stop line's second point and its third from the end
            const std::vector<ex::AiPoint>& stop_line = k.lines[4];
            F s = c.f3e4;
            if (s < k.lap_length * kHalf) s = s + k.lap_length;
            if (stop_line.size() >= 3 && !(s < stop_line[1].dist)) {
                if (s < stop_line[2].dist) next = d.pit_speed + kmh2;
                else if (s < stop_line[stop_line.size() - 3].dist) aim = next = d.pit_speed + kmh2, mode = 1, d.pit_limit = 1;
            }
        } else if (d.state == 2) aim = kmh50;
        if (d.state == 2) {
            speed_control(c, aim, -kHalf);
            d.arc_ticks = 0;
        } else {
            if (mode == 0) d.throttle = kOne, d.arc_ticks = 0;
            else if (mode == 3) speed_control(c, next, -kTwo), d.arc_ticks = 0;
            else if (mode == 4) speed_control(c, next, -kOne), ++d.arc_ticks;
            else if (mode == 1 || mode == 2) {
                if (!(c.x535 & 0x10) && !(s32(d.arc_ticks) < d.bonus_ticks)) {
                    aim = aim + d.corner_bonus;
                    if (t.bonus_angle != 0 && t.bonus_speed != 0) {
                        const F lim = int_f(t.bonus_angle) * kDeg;
                        if (aeps < lim) aim = aim + ((int_f(t.bonus_speed) * kKmh) * (lim - aeps)) / lim;
                    }
                }
                speed_control(c, aim, -kHalf);
                if (d.status == 0) d.status = !(d.throttle == kOne) ? 5 : mode == 1 ? 4 : 3;
                ++d.arc_ticks;
            }
            if (d.arc_ticks > 6000) d.arc_ticks = 6000;
        }
        d.throttle = d.throttle * scale;
        if (s32(5000 + 100 * s32(c.rank)) < s32(in.clock / 3u)) traffic(c, in);
    } else {
        if (drive != -1) d.throttle = kZero;
        else d.reverse = 1, d.xa9 = 0, d.throttle = kOne;
        d.brake = brake ? kOne : kZero;
        d.arc_ticks = 0;
    }
    // Past the tires' peak: the throttle cut and the brake by the set's shares. (The static block's +0xDB, clear on the disc,
    // would take the car's own measure F +0x39C in the steering's place.)
    if (kZero < over && !(c.gear < 2)) {
        const F e = over - kOne;
        F f = kOne - d.sat_throttle * e;
        if (f < kZero) f = kZero;
        d.throttle = d.throttle * f;
        const F b = d.brake + e * d.sat_brake;
        d.brake = kOne < b ? kOne : b;
    }
}

void step(ex::Car& c, const Step& in) {
    c.handbrake = kZero;
    c.reverse_req = 0, c.shift_req = 0;
    if (c.p.d.kind == 3) { // the hybrid's box (0x3DD798): no hold of its engine
        rules::hybrid_clutch_request(c);
        if (c.clutch_req == kZero && c.driver_kind == 8) return park(c);
        return decide(c, in);
    }
    if (c.race_hold != 0 || c.hold != 0) {
        hold(c, c.race_hold != 0 ? in.start_count : s32(c.hold));
        rules::brake_pedal(c, in.part->layout, kZero); // (0x3B2338: Gran Turismo 4's 0x343D60 in its layout)
        c.steer_cmd = kZero;
        return;
    }
    if (c.driver_kind == 8) {
        c.clutch_req = kZero;
        return park(c);
    }
    if (c.clutch == 0) c.clutch_req = 3 * s32(c.p.d.up_rpm) / 4 < s32(c.rpm) ? kOne : kZero;
    else c.clutch_req = s32(c.rpm) < s32(c.p.e.idle) ? kZero : kOne;
    decide(c, in);
}

} // namespace gt4r::sim::ai::prologue
