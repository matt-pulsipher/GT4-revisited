// Gran Turismo 4's own routines of the race step: the tires (rules_tires.h).

#include "gt4r/sim/rules_tires.h"

#include "base/ee_math.h"

namespace gt4r::sim::rules {

namespace {
const F kOne = F::raw(0x3F800000), kMinusOne = F::raw(0xBF800000);
const F kPercent = F::raw(0x3C23D70A);
const F kKmh = F::raw(0x3E8E38E3);                                      // 1 / 3.6
const F kEps = F::raw(0x3C8E38E3), kMinusEps = F::raw(0xBC8E38E3);       // 1 / 57.6 m/s
size_t axle_of(const WheelLayout& l, int w) { return w < l.count ? l.axle[size_t(w)] & 1u : 0u; }
} // namespace

const Tire& tire_record(const TireExtra& x, const WheelLayout& l, int w, u8 surface) {
    return x.tire[x.tire_index[axle_of(l, w)][surface % 24] % 6];
}

F load_per_width(const TireExtra& x, const WheelLayout& l, int w, F load) { return load / x.width[axle_of(l, w)]; }

void peak_force(ex::Car& c, TireExtra& x, const WheelLayout& l, bool wear, F driver_grip) {
    const F d = c.roll - c.ground_roll;
    for (int i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        const Tire& r = tire_record(x, l, i, w.surface);
        const size_t a = axle_of(l, i);
        x.wheel_xdd[size_t(i)] = r.x1da;
        F f = w.load * table(r.load_curve, load_per_width(x, l, i, w.load));
        if (wear) f = f * w.wear_grip;
        f = f * x.axle_grip[a];
        const F camber = l.kind == 1 ? F() : (l.side[size_t(i)] ? d : -d) + w.camber;
        const F g = table(x.camber_grip[a], camber);
        w.peak_force = f * r.mu[w.surface % 24] * g;
        w.peak_force = w.peak_force * driver_grip;
    }
}

SlipBytes slip_bytes(const u8* b) {
    SlipBytes s;
    for (size_t k = 0; k < 2; ++k) s.fade[k] = b[k], s.gain_on[k] = b[2 + k], s.gain_base[k] = b[4 + k], s.gain_knee[k] = b[6 + k];
    return s;
}

F slip_gain(const SlipBytes& sb, int k, F u) {
    F g = kOne;
    if (!sb.gain_on[size_t(k)]) return g;
    const F base = gt3r::ee::int_to_f(sb.gain_base[size_t(k)]) * kPercent;
    const F knee = gt3r::ee::int_to_f(sb.gain_knee[size_t(k)]) * kKmh;
    const F room = g - base;
    if (u < F()) u = -u;
    if (u < knee) g = base + room * u / knee;
    return g;
}

void slip_ratio(ex::Car& c, const TireExtra& x, int wi, const Tire& r, F along, const SlipBytes& sb) {
    ex::Wheel& w = c.w[size_t(wi)];
    ex::Drive& t = c.t[size_t(wi)];
    const F rim = w.rim_speed;
    t.slip_state = 0;
    if (r.direct_slip) {
        F s = along - rim;
        if (F() <= rim) {
            const s8 v = rim < along ? -1 : 1;
            t.slip_state = v, t.slip_sign = v;
        } else {
            s = -s;
            if (rim <= along) t.slip_sign = -1, t.slip_state = 1;
            else t.slip_sign = 1, t.slip_state = -1;
        }
        s = s * x.direct_slip_scale;
        w.slip_gain = kOne;
        if (kOne < s) s = kOne;
        else if (s < kMinusOne) s = kMinusOne;
        w.slip_ratio = s;
        return;
    }
    F s, g;
    if (kMinusEps < rim && rim < kEps) {
        g = slip_gain(sb, 0, w.hub_vx);
        if (rim < along) s = kOne, t.slip_sign = -1;
        else if (along < rim) s = kOne, t.slip_sign = 1;
        else s = F(), t.slip_sign = 0;
        t.slip_state = -1;
    } else {
        s = F();
        if (F() < rim) {
            if (along <= rim) {
                g = slip_gain(sb, 1, w.hub_vx);
                s = (along - rim) * g / rim;
                t.slip_state = 1, t.slip_sign = 1;
            } else {
                g = slip_gain(sb, 0, w.hub_vx);
                if (!(along < kEps && kMinusEps < along)) s = (along - rim) * g / along;
                t.slip_state = -1, t.slip_sign = -1;
            }
        } else if (rim <= along) {
            g = slip_gain(sb, 1, w.hub_vx);
            s = (along - rim) * g / rim;
            t.slip_sign = -1, t.slip_state = 1;
        } else {
            g = slip_gain(sb, 0, w.hub_vx);
            if (!(along < kEps && kMinusEps < along)) s = (along - rim) * g / along;
            t.slip_sign = 1, t.slip_state = -1;
        }
    }
    w.slip_gain = g;
    if (kOne < s) s = kOne;
    else if (s < kMinusOne) s = kMinusOne;
    const s8 st = t.slip_state;
    if (st == 0) {
        w.slip_ratio = F();
        return;
    }
    // The low-speed fade by the hub's speed (braking) or the rim's (driving).
    F v = st < 0 ? along : rim;
    const u8 b = sb.fade[st < 0 ? 0 : 1];
    if (b) {
        const F v0 = gt3r::ee::int_to_f(b) * kKmh;
        if (v < F()) v = -v;
        if (v < v0) s = s * (v / v0);
    }
    w.slip_ratio = s;
}

void slip_ratios(ex::Car& c, const TireExtra& x, const WheelLayout& l, const SlipBytes& sb) {
    for (int i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        const Tire& r = tire_record(x, l, i, w.surface);
        const F a = w.steer;
        const F along = gt3r::ee::cos(a) * w.hub_vx;
        slip_ratio(c, x, i, r, along - gt3r::ee::sin(a) * w.hub_vy, sb);
    }
}

namespace {
// 0x3598F8: curve C and the curve on its axis (curve D) at v.
void lookup2(const ex::Table<12>& c, const std::array<F, 12>& y2, F v, F& out_c, F& out_2) {
    const u32 last = u32(s32(c.n) - 1);
    if (v <= c.x[0]) {
        out_c = c.y[0], out_2 = y2[0];
        return;
    }
    if (c.x[last] <= v) {
        out_c = c.y[last], out_2 = y2[last];
        return;
    }
    u32 k = 1;
    while (k < last && !(v < c.x[k])) ++k;
    const F dv = v - c.x[k - 1];
    const F dx = c.x[k] - c.x[k - 1];
    out_c = c.y[k - 1] + dv * (c.y[k] - c.y[k - 1]) / dx;
    out_2 = y2[k - 1] + dv * (y2[k] - y2[k - 1]) / dx;
}
} // namespace

void tire_curves(ex::Car& c, const TireExtra& x, const WheelLayout& l) {
    for (int i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        const Tire& r = tire_record(x, l, i, w.surface);
        lookup2(r.curve_c, r.curve_d, w.slip_ratio, c.t[size_t(i)].long_frac, w.circle);
    }
}

void tire_long_force(ex::Car& c, const WheelLayout& l, const SlipBytes& sb) {
    for (int i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        ex::Drive& t = c.t[size_t(i)];
        F f = t.long_frac * w.peak_force;
        if (t.slip_sign < 0) f = -f;
        const F g = w.slip_gain * f;
        const bool drive = t.slip_state > 0;
        t.react_drive = drive ? g : F();
        t.react_brake = drive ? F() : g;
        w.fx_brake = drive ? F() : (sb.gain_on[0] != 2 ? f : g);
        w.fx_drive = drive ? (sb.gain_on[1] != 2 ? f : g) : F();
    }
}

void rolling_resistance(ex::Car& c, const WheelLayout& l, const RoadTables& road) {
    c.x4d0 = F(), c.a_x = F(), c.rolling = F();
    F load = c.x4d0;
    for (int i = 0; i < l.count && i < 4; ++i) load = load + c.w[size_t(i)].load * road.rolling_mu[c.w[size_t(i)].surface % 24];
    const F u = c.speed;
    const F rr = F() < u ? -table(road.rolling_curve, u) : table(road.rolling_curve, -u);
    c.rolling = rr * load;
}

void slip_angles(ex::Car& c, const WheelLayout& l) {
    for (int i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        w.slip_dir = ex::slip_angle(c.behaviour, w.hub_vx, w.hub_vy, w.slip_fade);
    }
}

} // namespace gt4r::sim::rules
