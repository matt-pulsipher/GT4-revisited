// Gran Turismo 4's own routines: proximity, slipstream and the speed-switched part (rules_near.h).

#include "gt4r/sim/rules_near.h"

#include "base/ee_math.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program.h"

namespace gt4r::sim::rules {

namespace {

using gt3r::ee::int_to_f;

const F k0 = F::raw(0), kHalf = F::raw(0x3F000000), kOne = F::raw(0x3F800000), kTwo = F::raw(0x40000000);
const F kThree = F::raw(0x40400000), kFour = F::raw(0x40800000), kFive = F::raw(0x40A00000), kSix = F::raw(0x40C00000);
const F kTwenty = F::raw(0x41A00000), kFifty = F::raw(0x42480000), kHundred = F::raw(0x42C80000), kThousand = F::raw(0x447A0000);
const F kQuarter = F::raw(0x3E800000), kThreeQuarters = F::raw(0x3F400000), kNineTenths = F::raw(0x3F666666);
const F kTenth = F::raw(0x3DCCCCCC), kFifth = F::raw(0x3E4CCCCC), kTwentieth = F::raw(0x3D4CCCCC);
const F kThousandth = F::raw(0x3A83126E);  // 0.001, a unit low
const F kKmh = F::raw(0x3E8E38E3);         // 1 / 3.6
const F kKmh2 = F::raw(0x3F0E38E3);        // 2 km/h in m/s
const F kKmh10 = F::raw(0x4031C71B);       // 10 km/h
const F kKmh20 = F::raw(0x40B1C71B);       // 20 km/h
const F kKmh01 = F::raw(0x3CE38E37);       // 0.1 km/h
const F kKmh100 = F::raw(0x41DE38E2);      // 100 km/h
const F kGravity = F::raw(0x411CCCCC);     // 9.8, a unit low
const F kPartOut = F::raw(0x3E1D89D8);     // 2 / 13: kind 6's state 1
const F kRateDefault = F::raw(0x43960000); // 300

enum Cell { kFront, kFrontLeft, kFrontRight, kLeft, kRight, kRearLeft, kRearRight, kRear };

// "Raised to 2" and "1 where not above 0" (the cells are signed bytes).
void at_least2(NearExtra& x, int cell) {
    if (s8(x.zone[size_t(cell)]) < 2) x.zone[size_t(cell)] = 2;
}
void at_least1(NearExtra& x, int cell) {
    if (!(s8(x.zone[size_t(cell)]) > 0)) x.zone[size_t(cell)] = 1;
}

F at(const ex::Vec3& v, int k) { return k == 0 ? v.x : k == 1 ? v.y : v.z; }

} // namespace

bool left_out(const ex::Car& c, const NearExtra& x) { return c.ghost != 0 || x.driver_line == 4; }

bool pair_view(const ex::Car& a, const NearExtra& xa, const ex::Car& b, const NearExtra& xb, PairView& out) {
    if (left_out(a, xa) || left_out(b, xb)) return false;
    if ((a.x535 & 0x10) != (b.x535 & 0x10)) return false;
    F d[3];
    for (int k = 0; k < 3; ++k) d[k] = at(b.pos, k) - at(a.pos, k);
    const F d0 = d[0] * d[0], d1 = d[1] * d[1], d2 = d[2] * d[2];
    out.dist = gt3r::ee::sqrt((d0 + d1) + d2);
    if (kHundred < out.dist) return false;
    if (d[2] < -(b.p.body_height + kFive)) return false;
    if (a.p.body_height + kFive < d[2]) return false;
    out.u_a = out.v_a = out.u_b = out.v_b = k0;
    for (int k = 0; k < 3; ++k) {
        out.u_a = out.u_a + at(a.fwd, k) * d[k];
        out.v_a = out.v_a + at(a.lat, k) * d[k];
        out.u_b = out.u_b - at(b.fwd, k) * d[k];
        out.v_b = out.v_b - at(b.lat, k) * d[k];
    }
    return true;
}

u8 position_code(F dist, F u, F v, F ahead, F behind, F w) {
    if (kFifty < dist) return 0;
    u8 c;
    if (ahead * kFour < u) c = 6;
    else if (ahead < u) c = 5;
    else if (k0 < u) c = 4;
    else if (-behind <= u) c = 3;
    else if (-behind * kFour <= u) c = 2;
    else c = 1;
    const F nw = -w;
    if (v < nw * kFour) return u8(c | 0x10);
    if (v < nw) return u8(c | 0x20);
    if (v < nw * kHalf) return u8(c | 0x30);
    if (v <= w * kHalf) return u8(c | 0x40);
    if (v <= w) return u8(c | 0x50);
    if (v <= w * kFour) return u8(c | 0x60);
    return u8(c | 0x70);
}

F slipstream(const ex::Car& b, F x, F y) {
    const F v = b.speed, h = b.p.body_width;
    if (!(kKmh100 < v)) return k0;
    if (!(x < k0)) return k0;
    const F reach = v + v * kHalf;
    if (!(-reach < x)) return k0;
    if (!(-h < y) || !(y < h)) return k0;
    return (reach + x) / reach;
}

void near_reset(NearExtra& x) {
    x.contact = k0;
    x.zone = {};
    x.threat = 0xFF;
    x.threat_rate = -kThousand;
    x.threat_gap = kThousand;
    x.gap_left = kThousand, x.gap_right = kThousand;
}

void zones(NearExtra& x, F dist, F u, F v, F ahead, F behind, F w) {
    if (kTwenty < dist) return;
    const F lim = w + kThree;
    if (v < -lim || lim < v) return;
    if (ahead * kNineTenths < u) {
        const F w75 = w * kThreeQuarters, gap = u - ahead;
        auto side = [&](int cell) {
            if (gap < kFive) at_least2(x, cell);
            else if (gap < kTwenty) at_least1(x, cell);
        };
        if (v < -w75) return side(kFrontLeft);
        if (w75 < v) return side(kFrontRight);
        if (gap < kTwenty) at_least2(x, kFront);
        if (v < -(w * kHalf)) side(kFrontLeft);
        if (w * kHalf < v) side(kFrontRight);
        return;
    }
    if (u < -(behind * kNineTenths)) {
        if (v < -w) return at_least2(x, kRearLeft);
        if (w < v) return at_least2(x, kRearRight);
        at_least2(x, kRear);
        if (k0 < v) at_least1(x, kRearRight);
        else at_least1(x, kRearLeft);
        return;
    }
    if (v < k0) {
        const F g = -v - w;
        if (g < x.gap_left) x.gap_left = g;
        at_least2(x, kLeft);
        if (ahead * kHalf < u) at_least1(x, kFrontLeft);
        else if (u < -(behind * kHalf)) at_least1(x, kRearLeft);
    } else {
        const F g = v - w;
        if (g < x.gap_right) x.gap_right = g;
        at_least2(x, kRight);
        if (ahead * kHalf < u) at_least1(x, kFrontRight);
        else if (u < -(behind * kHalf)) at_least1(x, kRearRight);
    }
}

bool closing(const ex::Car& a, const NearExtra& xa, const ex::Car& b, const NearExtra& xb, F lap, F v, F& gap, F& rate, bool japan) {
    gap = k0, rate = k0;
    const F sa = a.f3e4, sb = b.f3e4;
    const F s = lap * kThreeQuarters < sa && sb < lap * kQuarter ? (sb + lap) - sa : sb - sa;
    const F g = (s - b.p.body_rear) - a.p.body_front;
    if (g <= k0) return false;
    const F diff = xb.course_rate - xa.course_rate;
    if (japan || g < kTwenty) {
        const F h = (a.p.body_width + b.p.body_width) * kHalf;
        if (h < v || v < -h) return false;
    }
    gap = g, rate = -diff;
    return true;
}

void let_go(NearExtra& xa, const ex::Car& b) {
    if (xa.follow == b.index) xa.follow = 0xFF;
}

void threat(NearExtra& xa, const ex::Car& b, F gap, F rate) {
    bool take;
    if (k0 < rate) take = xa.threat_rate <= k0 || gap / rate < xa.threat_gap / xa.threat_rate;
    else take = !(k0 < xa.threat_rate) && gap < xa.threat_gap;
    if (!take) return;
    xa.threat_gap = gap, xa.threat_rate = rate, xa.threat = b.index;
}

void contact(NearExtra& xa, const ex::Car& b, F gap, F rate) {
    threat(xa, b, gap, rate);
    const u8 me = b.index;
    F cand = k0;
    int follow = -1;
    if (xa.follow == me && kSix < gap) let_go(xa, b);
    if (xa.follow != me && k0 < rate && gap < kFive) cand = kTwo, follow = me;
    else if (kKmh20 < rate) cand = kOne;
    else if (kKmh01 < rate) {
        if (gap / rate < kFive) cand = kHalf;
    }
    if (cand == k0) {
        if (gap < kHalf) cand = kTwentieth;
        else if (gap < kFour) cand = kTenth;
    }
    if (xa.contact < cand) {
        if (follow != -1) xa.follow = u8(follow), cand = kOne;
        xa.contact = cand;
    }
}

void proximity(std::span<const NearCar> cars, NearPairs& pairs, F lap, u8 rule_kind, bool japan) {
    const size_t n = cars.size() < size_t(NearPairs::kCars) ? cars.size() : size_t(NearPairs::kCars);
    for (size_t i = 0; i < n; ++i) cars[i].x->chain = 0;
    for (size_t i = 0; i < n; ++i) near_reset(*cars[i].x);
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            const ex::Car &a = *cars[i].car, &b = *cars[j].car;
            NearExtra &xa = *cars[i].x, &xb = *cars[j].x;
            PairView pv;
            if (!pair_view(a, xa, b, xb, pv)) {
                pairs.slip[i][j] = k0, pairs.slip[j][i] = k0;
                pairs.code[i][j] = 0, pairs.code[j][i] = 0;
                continue;
            }
            const F w = (a.p.body_width + b.p.body_width) * kHalf;
            const F ab = a.p.body_front + b.p.body_rear, ba = a.p.body_rear + b.p.body_front;
            zones(xa, pv.dist, pv.u_a, pv.v_a, ab, ba, w);
            zones(xb, pv.dist, pv.u_b, pv.v_b, ba, ab, w);
            pairs.code[i][j] = position_code(pv.dist, pv.u_a, pv.v_a, ab, ba, w);
            pairs.code[j][i] = position_code(pv.dist, pv.u_b, pv.v_b, ba, ab, w);
            if (rule_kind == 6 || (japan && !(pv.dist < F::raw(0x42480000)))) {
                let_go(xa, b), let_go(xb, a);
            } else {
                F gap, rate;
                if (closing(a, xa, b, xb, lap, pv.v_a, gap, rate, japan)) contact(xa, b, gap, rate);
                else let_go(xa, b);
                if (closing(b, xb, a, xa, lap, pv.v_b, gap, rate, japan)) contact(xb, a, gap, rate);
                else let_go(xb, a);
            }
            pairs.slip[i][j] = slipstream(b, pv.u_b, pv.v_b);
            pairs.slip[j][i] = slipstream(a, pv.u_a, pv.v_a);
        }
    for (size_t i = 0; i < n; ++i) {
        F sum = k0;
        for (size_t j = 0; j < n; ++j) sum = sum + pairs.slip[i][j];
        if (kOne < sum) sum = kOne;
        cars[i].car->slip_target = sum;
    }
    for (size_t i = 0; i < n; ++i) {
        u8 len = 0;
        s8 k = s8(i);
        for (size_t steps = 0; steps < n;) {
            ++steps;
            k = size_t(k) < n ? s8(cars[size_t(k)].x->follow) : s8(-1);
            if (k == -1) break;
            ++len;
        }
        cars[i].x->chain = len;
    }
}

// ---- phase 15 ----------------------------------------------------------------------------------------------------

void speed_part_params(NearExtra& x, const Spec& s) {
    x.speed_lo = x.speed_hi = x.speed_rate = k0;
    if (s.b(0x4C) == 0) return;
    x.speed_lo = int_to_f(s.b(0x4D)) * kKmh;
    x.speed_hi = int_to_f(s.b(0x4E)) * kKmh;
    x.speed_rate = s.b(0x4F) == 0 ? kRateDefault : F::raw(0x41200000) / int_to_f(s.b(0x4F));
}

F speed_part_share(const NearExtra& x, F v) {
    if (v <= x.speed_lo) return k0;
    if (x.speed_hi <= v) return kOne;
    return (v - x.speed_lo) / (x.speed_hi - x.speed_lo);
}

namespace {

// Kinds 4 and 5: out while braking above the upper speed, in below the lower or with the pedal let up.
bool braking_part(const ex::Car& c, NearExtra& x) {
    if (x.speed_state == 0) {
        if (x.speed_hi <= c.speed && kHalf <= c.brake_pedal) x.speed_state = 1;
    } else if (c.speed < x.speed_lo || c.brake_pedal < kTenth) {
        x.speed_state = 0;
        return false;
    }
    return x.speed_state == 1;
}

} // namespace

void speed_part(ex::Car& c, NearExtra& x, u8 kind, F& state) {
    if (kind == 0) return;
    F target[2] = {k0, k0};
    int n = 1;
    switch (kind) {
    case 1:
        if (x.speed_state == 0) {
            if (x.speed_hi <= c.speed) x.speed_state = 1;
        } else if (c.speed < x.speed_lo) x.speed_state = 0;
        if (x.speed_state == 1) target[0] = kOne;
        break;
    case 2: target[0] = speed_part_share(x, c.speed); break;
    case 3: break;
    case 4: {
        n = 2;
        if (braking_part(c, x)) target[0] = kOne;
        const F g = c.a_y / kGravity;
        if (x.speed_state2 == 0) {
            if (kHalf <= g) x.speed_state2 = 1;
            else if (g <= -kHalf) x.speed_state2 = 2;
        } else if (x.speed_state2 == 1) {
            if (g < kFifth) x.speed_state2 = 0;
        } else if (-kFifth < g) x.speed_state2 = 0;
        if (x.speed_state2 == 1) target[1] = kOne;
        else if (x.speed_state2 == 2) target[1] = -kOne;
        break;
    }
    case 5:
        if (braking_part(c, x)) target[0] = kOne;
        break;
    case 6: {
        const F v = c.speed;
        if (x.speed_state == 1) {
            if (kKmh10 <= v && kHalf <= c.brake_pedal) x.speed_state = 2;
            else if (v < x.speed_lo) x.speed_state = 0;
        } else if (x.speed_state == 0) {
            if (kKmh10 <= v && kHalf <= c.brake_pedal) x.speed_state = 2;
            else if (x.speed_hi <= v) x.speed_state = 1;
        } else if (x.speed_state == 2) {
            if (c.brake_pedal < kTenth) x.speed_state = x.speed_lo <= v ? 1 : 0;
            else if (v < kKmh2) x.speed_state = 0;
        }
        if (x.speed_state == 2) target[0] = kOne;
        else if (x.speed_state == 1) target[0] = kPartOut;
        break;
    }
    default: break;
    }
    for (int k = 0; k < n; ++k) {
        F& s = k == 0 ? state : x.speed_part_state2;
        const F rate = k == 1 ? x.speed_rate + x.speed_rate : x.speed_rate;
        const F t = target[k];
        if (s < t) {
            const F m = s + rate * c.dt;
            s = t < m ? t : m;
        } else if (t < s) {
            const F m = s - rate * c.dt;
            s = m < t ? t : m;
        }
    }
}

// ---- the part's damping (0x3F2650) -----------------------------------------------------------------------------------

SpeedDampTables speed_damp_tables(const Program& program) {
    SpeedDampTables t;
    if (!program.known()) return t;
    for (u32 r = 0; r < 5; ++r) {
        const u32 a = kSpeedDampBytes + 8 * r;
        SpeedDampRow& row = t.rows[r];
        row.kind = program.b(a);
        row.lift_travel = int_to_f(program.b(a + 1)) * kThousandth;
        row.lift_force = int_to_f(program.b(a + 2)) / kHundred;
        for (u32 k = 0; k < 2; ++k) row.lift_share[k] = int_to_f(program.b(a + 3 + k)) / kHundred;
        row.term = int_to_f(program.b(a + 5)) * kThousandth;
        row.speed_lo = int_to_f(program.b(a + 6)) * kKmh;
        row.speed_hi = int_to_f(program.b(a + 7)) * kKmh;
    }
    for (u32 s = 0; s < 24; ++s) t.fixed_aim[s] = program.b(0x620288 + s);
    return t;
}

bool same(const SpeedDampTables& a, const SpeedDampTables& b) {
    for (size_t r = 0; r < a.rows.size(); ++r) {
        const SpeedDampRow &x = a.rows[r], &y = b.rows[r];
        if (x.kind != y.kind || x.lift_travel.bits != y.lift_travel.bits || x.lift_force.bits != y.lift_force.bits || x.term.bits != y.term.bits ||
            x.speed_lo.bits != y.speed_lo.bits || x.speed_hi.bits != y.speed_hi.bits)
            return false;
        for (size_t k = 0; k < 2; ++k)
            if (x.lift_share[k].bits != y.lift_share[k].bits) return false;
    }
    return a.fixed_aim == b.fixed_aim;
}

F speed_damp_term(const ex::Car& c, const SpeedDampTables& t, u8 row, int w) {
    if (row >= t.rows.size()) return k0;
    const SpeedDampRow& r = t.rows[row];
    if (r.kind == 2) {
        const F v = c.speed;
        if (r.speed_hi <= v) return r.term;
        if (!(r.speed_lo < v)) return k0;
        return (r.term * (v - r.speed_lo)) / (r.speed_hi - r.speed_lo);
    }
    if (r.kind == 3) {
        const u8 s = c.w[size_t(w)].surface;
        if (s < t.fixed_aim.size() && t.fixed_aim[s] != 0) return k0;
        return r.term;
    }
    return k0;
}

F speed_damp(const ex::Car& c, const SpeedDampTables& t, u8 row, F axle_load, int w, F x) {
    if (row >= t.rows.size() || t.rows[row].kind == 0) return k0;
    const SpeedDampRow& r = t.rows[row];
    const size_t a = size_t(w) >> 1;
    const ex::Suspension& s = c.p.susp[a];
    const F e = x - s.knee;
    F progressive = k0;
    if (e < k0) progressive = -((e * e) * s.k2);
    const F term = speed_damp_term(c, t, row, w);
    const F y = (x - s.x_static) + term;
    F lift = k0;
    if (y < k0) {
        const F travel = -r.lift_travel;
        F q = r.lift_force / r.lift_share[a];
        if (travel < y) q = (q * y) / travel;
        lift = (q * axle_load) * kHalf;
    }
    return (lift - s.k * term) + progressive;
}

} // namespace gt4r::sim::rules
