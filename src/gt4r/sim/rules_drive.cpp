// Gran Turismo 4's own routines of the race step: the engine, clutch, gearbox and driveline (rules_drive.h).

#include "gt4r/sim/rules_drive.h"

#include "gt4r/sim/program.h"
#include "sim/exact/tick.h"

namespace gt4r::sim::rules {

namespace {

using gt3r::s16;
using gt3r::u16;
using gt3r::ee::int_to_f;

constexpr F k0 = F::raw(0), kOne = F::raw(0x3F800000), kHalf = F::raw(0x3F000000);
constexpr F kRpm = F::raw(0x3DD6774E);    // 2 pi / 60
constexpr F k60 = F::raw(0x42700000);
constexpr F kTwoPi = F::raw(0x40C90FDA);
constexpr F kKmh1 = F::raw(0x3E8E38E3);   // 1 km/h in m/s
constexpr F kKmh10 = F::raw(0x4031C71B);  // 10 km/h
constexpr F kFull = F::raw(0x47BF67FF);   // the four wheel drive's "full transfer"
constexpr F kHand = F::raw(0x3E4CCCCC);   // 0.2: the handbrake that opens the coupling

bool stepless(const DrivelineBlock& d) { return u8(d.kind - 1) < 2; } // (0x3F1F98)
size_t ratio_gear(const ex::Car& c, const DrivelineBlock& d) { return d.kind == 2 && !(c.gear < 2) ? 1 : c.gear; }
bool reverse_locked(const ex::Car& c) { return c.clutch == 1 && c.gear == 0; } // (F +0x520 & 0xFF00FF == 0x10000)
u8 style_of(const ex::Car& c) { return c.control == 1 ? 0 : c.driver_kind == 0 ? c.pedal_style : 0; }
// A wheel of the layout's count: its axle, side of the center of gravity.
u8 axle_of(const WheelLayout& l, int w) { return w < l.count ? l.axle[size_t(w)] : 0; }
bool ahead(const WheelLayout& l, int w) { return w < l.count && gt3r::s8(l.fore_aft[size_t(w)]) > 0; }
bool behind(const WheelLayout& l, int w) { return w < l.count && gt3r::s8(l.fore_aft[size_t(w)]) < 0; }
int count(const WheelLayout& l) { return l.count < 4 ? l.count : 4; }
// A group's other wheel (itself when it has none).
int partner(const WheelLayout& l, int w) {
    const u8 p = l.partner[size_t(w)];
    return p == 0x7F || p >= 4 ? w : p;
}

// 0x350F48: the rpm from the engine's speed (truncated); not below idle - 1 (a gearbox of kind 4: 0).
void engine_rpm(ex::Car& c, const DriveExtra& x) {
    s32 n = gt3r::ee::to_int((c.omega * k60) / kTwoPi);
    if (x.d.kind == 4) {
        if (n < 0) n = 0;
    } else if (const s32 floor = s32(x.e.idle) - 1; n < floor) n = floor;
    c.rpm = s16(n);
}

// 0x34EA68: the power cap of a power-restricted event.
F power_cap(const EngineBlock& e, F t, F w) {
    if (e.power_cap_on == 0) return t;
    const F soft = e.power_cap_soft, cap = e.power_cap;
    const F p = t * w;
    if (p <= cap * (kOne - soft)) return t;
    F lim = cap * (kOne - soft * kHalf);
    if (!(cap <= p)) {
        const F d = cap - p;
        lim = lim - (d * d) / ((soft + soft) * cap);
    }
    lim = lim / w;
    return lim < t ? lim : t;
}

// 0x355410: the rev limiter (on at the rev limit, off 500 rpm below it) and the throttle's torque less the friction.
F engine_output(ex::Car& c, const EngineBlock& e, F x) {
    if (c.rev_limiter == 0) {
        if (!(s32(c.rpm) < s32(e.rev_limit))) c.rev_limiter = 1;
    } else if (s32(c.rpm) < s32(e.rev_limit) - 500) c.rev_limiter = 0;
    F t = k0;
    if (c.rev_limiter == 0) t = x * (c.torque_wot + c.torque_friction);
    return power_cap(e, t - c.torque_friction, c.omega);
}

// 0x34EB00: a turbo stage's boost at rpm n.
F turbo_stage(const EngineBlock& e, s16 n, int i) {
    const size_t k = size_t(i);
    const s32 knee = e.turbo_knee[k];
    if (n < knee) {
        switch (e.turbo_shape[k]) {
        case 0: {
            const F d = int_to_f(n - knee);
            return e.turbo_peak[k] - e.turbo_coef[k] * d * d;
        }
        case 1: {
            const F d = int_to_f(n);
            return e.turbo_coef[k] * d * d;
        }
        case 2: return (e.turbo_peak[k] * int_to_f(n)) / int_to_f(knee);
        default: return k0;
        }
    }
    if (!(n < knee + knee + knee)) return k0;
    const F d = int_to_f(n - knee);
    return e.turbo_peak[k] - e.turbo_coef[k] * d * d * F::raw(0x3E800000);
}

// 0x34ECF0 (0x34EC30): 1 plus the stages' boosts at rpm n0 / n1, at most 1 + the cap.
F boost(const EngineBlock& e, s16 n0, s16 n1) {
    const s16 n[2] = {n0, n1};
    F sum = k0;
    for (int i = 0; i < 2; ++i)
        if (!(e.turbo_peak[size_t(i)] == k0)) sum = sum + turbo_stage(e, n[i], i);
    F b = sum + kOne;
    const F cap = e.boost_max + kOne;
    if (cap < b) b = cap;
    return b;
}

// 0x36D738: the slipping clutch's factor by the speeds' difference.
F clutch_slip_factor(F d) {
    if (d < k0) d = -d;
    d = d - F::raw(0x42517082);
    if (d < k0) d = k0;
    F f = d / F::raw(0x44517082) + kOne;
    if (F::raw(0x3F900000) <= f) f = F::raw(0x3F900000);
    return f;
}

// 0x36D7A8: some wheel of the layout braked.
bool braked(const ex::Car& c, const WheelLayout& l) {
    for (int w = 0; w < count(l); ++w)
        if (!(c.t[size_t(w)].brake_torque == k0)) return true;
    return false;
}

// 0x3F2110: a stepless gearbox's ratio word for this step and the engine's torque through it.
F stepless_torque(ex::Car& c, DriveExtra& x, const DriveTables& t) {
    const DrivelineBlock& d = x.d;
    F r = kOne;
    if (c.clutch == 1 && c.gear != 0) {
        r = c.omega / c.clutch_wheel;
        const F last = d.ratio[d.gears < 10 ? d.gears : 9];
        if (kOne < r) r = kOne;
        else if (r < last) r = last;
        F target = kOne, rate;
        bool lock = false;
        if (d.kind == 1) {
            const F pedal = c.throttle_pedal;
            const F hold = (int_to_f(d.down_rpm[0]) * kRpm) * (pedal * F::raw(0x3ECCCCCC) + F::raw(0x3F199999));
            if (hold < c.clutch_wheel) {
                target = hold / c.clutch_wheel;
                if (target < last) target = last;
                else if (kOne < target) target = kOne;
            }
            rate = t.stepless.none + pedal * (t.stepless.full - t.stepless.none);
        } else {
            if (!(c.gear < 2)) target = d.ratio[c.gear < 10 ? c.gear : 9];
            rate = target < r ? t.stepless.down : t.stepless.up;
            lock = c.control == 1 || c.driver_kind != 0 || c.pedal_style == 0;
        }
        if (!(target == r)) {
            const F step = (rate * c.dt) * x.ratio_word;
            bool reached;
            if (r < target) r = r + step, reached = target <= r;
            else r = r - step, reached = r <= target;
            if (reached) r = target;
            else if (lock) c.shift_lockout = 1;
        }
    }
    x.ratio_word = r;
    return r * engine_torque(c, x, c.throttle);
}

// 0x36D548: four wheel drive system 5's coupling (the full-transfer value, or a gain).
F awd5_gain(const ex::Car& c, const DrivelineBlock& d, F tg, F ff, F fr, const ex::Awd3& k) {
    F g = c.lat_g;
    if (g < k0) g = -g;
    F f = kOne;
    if (!(g <= k.fade_start)) {
        if (kOne <= g) f = k.fade_floor;
        else f = kOne - ((kOne - k.fade_floor) * (g - k.fade_start)) / (kOne - k.fade_start);
    }
    F u = c.axle_speed[1] - c.axle_speed[0];
    if (u < k0) u = -u;
    u = u * d.awd_k;
    if (c.gear == 0) tg = -tg;
    if (k.negative_full && tg < k0) return -kFull;
    const F cap = d.awd_capacity;
    if (!(cap == k0)) {
        if (reverse_locked(c) || c.clutch == 0) return kFull;
        if (f == kOne) {
            if (c.clutch == 2 || F::raw(0x3F400000) < c.brake_pedal) return kFull;
            const F sum = (tg + ff) + fr;
            if (k0 < tg) {
                if (k0 < sum) return kFull;
            } else if (tg < k0 && sum < k0) return kFull;
        }
    }
    u = u * f;
    if (kOne <= u) u = cap == k0 ? kOne : kFull;
    return u;
}

// 0x36C098: the kick-down level (pedal styles 2): the throttle pedal in three steps, changed after a hold.
void kickdown_level(ex::Car& c) {
    s32 level = -1;
    if (!(c.throttle_pedal <= k0)) level = gt3r::ee::to_int((kOne - c.throttle_pedal) * F::raw(0x40400000));
    if (c.brake_pedal == kOne) level = 0;
    if (c.kickdown_timer != 0) c.kickdown_timer = u8(c.kickdown_timer - 1);
    const s32 cur = c.kickdown;
    if (level == cur || level == -1) {
        c.kickdown_count = 0;
        return;
    }
    c.kickdown_count = u8(c.kickdown_count + 1);
    if (c.kickdown_count < (level < cur ? 13 : 61)) return;
    c.kickdown_timer = 60;
    c.kickdown_count = 0;
    c.kickdown = u8(level < cur ? cur - 1 : cur + 1);
}

// 0x36C160: the automatic gearbox (`kickdown`: pedal style 2).
// 0x36BEE0 / 0x36BF28 with 0x3F1E30: the up-shift speed, and a down-shift speed, times the car's shift byte (car +0x83) / 128
// (truncated) for a car its B-spec driver drives (control source 1; the up-shift's with driver kind 0 too).
s32 up_speed(const ex::Car& c, const DriveExtra& x) {
    const u32 v = x.d.up_rpm;
    return c.control == 1 && c.driver_kind == 0 ? s32((v * x.shift_factor) >> 7) : s32(v);
}
s32 down_speed(const ex::Car& c, const DriveExtra& x, u16 v) { return c.control == 1 ? s32((u32(v) * x.shift_factor) >> 7) : s32(v); }

int shift_auto(ex::Car& c, const DriveExtra& x, const WheelLayout& l, const TireExtra& tires, const DriveTables& t, bool kickdown) {
    const DrivelineBlock& d = x.d;
    const int g = c.gear;
    if (c.antihunt != 0) c.antihunt = u16(c.antihunt - 1);
    if (kickdown) kickdown_level(c);
    if (c.shift_lockout != 0) {
        c.shift_lockout = u8(c.shift_lockout - 1);
        if (t.gears.mode == 2 && c.shift_lockout == 0 && c.shift_req != 0) c.shift_lockout = 1;
        return -1;
    }
    if (c.reverse_req != 0) return g != 0 ? 0 : -1;
    const F v = c.speed;
    if (kKmh1 < v && g == 0) return 1;
    if (k0 < c.throttle) {
        if (v < kKmh1) return g != 1 ? 1 : -1;
    } else if (v < -kKmh1) return g != 0 ? 0 : -1;
    if (g > 0 && g < d.gears) { // over-rev
        const F top = (int_to_f(s32(x.e.rev_limit) + 500) * kRpm) / d.gear_speed[size_t(g)];
        if (top < v) return g + 1;
    }
    if (t.gears.mode == 1) {
        if (c.shift_req != 0) return -1;
    } else if (t.gears.mode == 2) {
        if (c.shift_req > 0 && g < d.gears) {
            c.shift_lockout = 1, c.antihunt = 240;
            return g + 1;
        }
        if (c.shift_req < 0 && !(g < 2)) {
            c.antihunt = 240, c.shift_lockout = 1;
            return g - 1;
        }
        if (c.antihunt != 0) return -1;
    }
    if (c.clutch != 1) return -1;
    if (kickdown) {
        if (g == 0) return -1;
        int n = gear_for_speed(c, x, v) + c.kickdown;
        if (d.gears < n) n = d.gears;
        return g == n ? -1 : n;
    }
    if (g == 0) return -1;
    s32 down, up;
    if (g == 1) {
        if (d.gears == 1) return -1;
        up = up_speed(c, x);
        down = -20000;
    } else {
        up = g < d.gears ? up_speed(c, x) : 20000;
        down = down_speed(c, x, d.down_rpm[size_t(g - 2) < 8 ? size_t(g - 2) : 7]);
        if (c.throttle_pedal < F::raw(0x3D4CCCCC)) down += 100 * s32(t.gears.down_lift);
        if (F::raw(0x3F4CCCCC) < c.brake_pedal) down += 100 * s32(t.gears.down_brake);
        if (F::raw(0x3E800000) < c.throttle_pedal) down -= 100 * s32(t.gears.down_throttle);
    }
    // The driven wheels the traction veto looks at, by the layout.
    bool front = false, rear = false;
    switch (d.layout) {
    case 1:
    case 2:
    case 9: front = true; break;
    case 3:
    case 8:
    case 10: front = rear = true; break;
    default: rear = true; break;
    }
    if (c.rpm < down) return g - 1;
    if (!(up < c.rpm)) return -1;
    for (int w = 0; w < count(l); ++w) {
        if (!((front && ahead(l, w)) || (rear && behind(l, w)))) continue;
        const ex::Wheel& wh = c.w[size_t(w)];
        if (wh.load == k0) return -1;
        if (wh.slip_ratio < tire_record(tires, l, w, wh.surface).slip_drive * wh.slip_gain) return -1;
    }
    return g + 1;
}

// A held lock-out ends its count at 1 while `held`.
int lockout_tick(ex::Car& c, bool held) {
    c.shift_lockout = u8(c.shift_lockout - 1);
    if (c.shift_lockout == 0 && held) c.shift_lockout = 1;
    return -1;
}

// The shift inputs within `lowest` .. the top gear.
int manual_step(ex::Car& c, const DrivelineBlock& d, int lowest) {
    const int g = c.gear;
    if (c.shift_req > 0 && g < d.gears) {
        c.shift_lockout = 1;
        return g + 1;
    }
    if (c.shift_req < 0 && g > lowest) {
        c.shift_lockout = 1;
        return g - 1;
    }
    return -1;
}

// 0x36C8E8: the gear decision by the pedal style; -1 none.
int gear_decision(ex::Car& c, const DriveExtra& x, const WheelLayout& l, const TireExtra& tires, const DriveTables& t) {
    if (c.hold != 0) return c.gear != 1 ? 1 : -1;
    const int g = c.gear;
    switch (style_of(c)) {
    case 0: return shift_auto(c, x, l, tires, t, false);
    case 2: return shift_auto(c, x, l, tires, t, true);
    case 1: // 0x36C690
        if (c.reverse_req != 0) return g != 0 ? 0 : -1;
        if (g == 0 && k0 < c.throttle_pedal) {
            const int n = gear_for_speed(c, x, c.speed);
            return n == 0 ? 1 : n;
        }
        if (c.shift_lockout != 0) return lockout_tick(c, c.shift_req != 0);
        return manual_step(c, x.d, 1);
    case 3:
    case 4:
    case 6: // 0x36C780
        if (c.shift_lockout != 0) return lockout_tick(c, c.shift_req != 0 || c.reverse_req != 0);
        if (c.reverse_req != 0) {
            if (g == 0) return -1;
            c.shift_lockout = 1;
            return 0;
        }
        return manual_step(c, x.d, 1);
    case 5: // 0x36C850
        if (c.shift_lockout != 0) return lockout_tick(c, c.shift_req != 0);
        return manual_step(c, x.d, 0);
    default: return -1;
    }
}

// 0x36BEB8: the gear set (a special car of kind 4 never takes reverse).
void change_gear(ex::Car& c, int g) {
    if (c.p.special == 4 && g == 0) return;
    c.gear = u8(g);
}

} // namespace

F turbo_boost(const EngineBlock& e, s16 n, int stage) { return turbo_stage(e, n, stage); }

DriveTables drive_tables(const u8* awd, const u8* stepless_bytes, const u8* settings) {
    DriveTables t;
    auto word = [](const u8* p) { return F::raw(u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24); };
    t.awd.negative_full = awd[0], t.awd.fade_start = word(awd + 2), t.awd.fade_floor = word(awd + 10);
    const F pc = F::raw(0x3C23D70A);
    t.stepless.full = int_to_f(stepless_bytes[0]) * pc, t.stepless.none = int_to_f(stepless_bytes[1]) * pc;
    t.stepless.down = int_to_f(stepless_bytes[12]) * pc, t.stepless.up = int_to_f(stepless_bytes[13]) * pc;
    t.gears.mode = settings[8], t.gears.down_throttle = settings[9], t.gears.down_lift = settings[10], t.gears.down_brake = settings[11];
    return t;
}

DriveTables drive_tables(const Program& program) {
    DriveTables r = drive_tables_bytes(program);
    r.hybrid = hybrid_tables(program);
    return r;
}

DriveTables drive_tables_bytes(const Program& program) {
    u8 a[14], s[14], g[12];
    for (u32 i = 0; i < 14; ++i) a[i] = program.b(kAwdFade + i), s[i] = program.b(kSteplessBytes + i);
    for (u32 i = 0; i < 12; ++i) g[i] = program.b(kControllerSettings + i);
    return drive_tables(a, s, g);
}

F driven_axle_speed(const ex::Car& c, const DrivelineBlock& d) {
    const F wf = c.axle_speed[0], wr = c.axle_speed[1];
    switch (d.layout) {
    case 0:
    case 4:
    case 5:
    case 6:
    case 7: return wr;
    case 1:
    case 2:
    case 9: return wf;
    case 3:
    case 10: return (wr + wf) * kHalf;
    case 8: return wr + d.awd_k * (wf - wr);
    default: return k0;
    }
}

F wheel_side_speed(const ex::Car& c, const DrivelineBlock& d) {
    F w = driven_axle_speed(c, d) * d.ratio[ratio_gear(c, d) < 10 ? ratio_gear(c, d) : 9];
    if (w <= k0) w = k0;
    return w;
}

F engine_torque(ex::Car& c, const DriveExtra& x, F throttle) {
    const EngineBlock& e = x.e;
    F xe = throttle;
    if (e.boost_max == k0) {
        c.turbo_load = throttle;
        c.turbo_spool = 0;
        c.turbo_load_prev = throttle;
    } else {
        const F dt = c.dt;
        const F b = boost(e, c.rpm, c.rpm);
        c.turbo_load_prev = c.turbo_load;
        const F target = (c.turbo_load * int_to_f(c.rpm)) / b;
        for (size_t i = 0; i < 2; ++i) {
            if (e.turbo_peak[i] == k0) continue;
            const F d = ((target - int_to_f(c.turbo_lag[i])) * e.turbo_rate[i]) * dt;
            const u32 lag = u32(u16(c.turbo_lag[i])) + u32(gt3r::ee::to_int(d));
            c.turbo_lag[i] = s16(u16(lag));
            if (s32(lag << 16) < 0) c.turbo_lag[i] = 0;
        }
        const F load = throttle * boost(e, c.turbo_lag[0], c.turbo_lag[1]);
        F k = kHalf - (int_to_f(c.rpm) * F::raw(0x39800000)) * F::raw(0x3E800000);
        if (k < F::raw(0x3D800000)) k = F::raw(0x3D800000);
        c.blowoff = k0;
        c.intake = (c.intake + (load + (kOne - throttle) * k)) * kHalf;
        if (c.blowoff_state == 0) {
            if (throttle < F::raw(0x3F400000)) {
                F l = c.turbo_load - F::raw(0x3F4CCCCC);
                if (l < c.blowoff) l = c.blowoff;
                c.blowoff_state = 1;
                c.blowoff = l * F::raw(0x3B800000);
            }
        } else if (c.blowoff_state == 1) {
            if (F::raw(0x3F666666) < throttle) c.blowoff_state = 0;
        }
        c.turbo_load = load;
        xe = kOne;
        if (!(b <= load)) xe = load / b;
        c.turbo_pitch = u16(c.turbo_lag[0]);
        if (s32(u16(c.turbo_lag[0])) < s32(c.turbo_lag[1])) c.turbo_pitch = u16(c.turbo_lag[1]);
    }
    const F t = engine_output(c, e, xe);
    if (k0 < e.boost_max) {
        const F l = c.turbo_load_prev - F::raw(0x3F400000);
        if (l <= k0 || t <= k0) c.turbo_spool = 1;
        else {
            F q = F::raw(0x4412FFFF) <= t ? l : (l * t) * F::raw(0x3ADEE95D); // 588 N m, 1 / 588
            q = q * q;
            s32 n = gt3r::ee::to_int(q * F::raw(0x43000000));
            if (!(n < 128)) n = 127;
            if (!(0 < n)) n = 1;
            c.turbo_spool = u8(n);
        }
    }
    fuel_use(c, x.fuel, x.d.kind, t); // (0x354B78)
    return t;
}

void engine_torques(ex::Car& c, const DriveExtra& x, const ex::Condition& cond) {
    c.torque_wot = ex::lookup_bisect(x.e.torque, c.omega) * ex::condition_factor(c.behaviour, cond, c.engine_wear, c.oil);
    c.torque_friction = x.e.friction * c.omega; // (0x620E14 0: the friction's other rule unused)
}

void brakes_and_clutch(ex::Car& c, DriveExtra& x, const WheelLayout& l) {
    const DrivelineBlock& d = x.d;
    c.clutch_engine = k0;
    c.clutch_wheel = k0;
    if (d.kind == 3) { // 0x351178: a one-speed box
        c.clutch = c.clutch_req == k0 ? 0 : 1;
    } else { // 0x350FB8
        if (c.clutch == 0) {
            if (!(c.clutch_req == k0)) {
                const F v = c.speed;
                const bool moving = (kKmh10 < v && c.gear != 0) || (v < -kKmh10 && c.gear == 0);
                if (moving) {
                    if (F::raw(0x3E800000) < c.throttle) c.clutch = 2;
                } else if (s32(d.launch_rpm) < s32(c.rpm)) c.clutch = 2;
                else c.clutch_req = k0;
            }
        } else if (c.clutch == 1) {
            if (c.clutch_req == k0) c.clutch = 0;
            else if (stepless(d)) c.clutch_wheel = wheel_side_speed(c, d);
        }
        if (u32(c.clutch) - 2u < 2u) {
            const u8 style = style_of(c);
            c.clutch_engine = c.omega;
            const F ws = wheel_side_speed(c, d);
            c.clutch_wheel = ws;
            if (style != 3 && style != 6 && c.clutch == 3) c.throttle = ws < c.clutch_engine ? k0 : kOne; // the shift's cut or blip
        }
    }
    if (d.kind == 4) c.clutch = 1;
    for (int w = 0; w < count(l); ++w) c.t[size_t(w)].brake_torque = c.w[size_t(w)].brake * c.p.brake_torque[axle_of(l, w) & 1];
    const F hb = c.handbrake * c.p.handbrake_torque;
    for (int w = 0; w < count(l); ++w)
        if (behind(l, w)) c.t[size_t(w)].brake_torque = c.t[size_t(w)].brake_torque + hb;
    for (int w = 0; w < count(l); ++w) {
        u8 f = c.t[size_t(w)].brake_torque == k0 ? 0 : 2;
        if (c.w[size_t(w)].load == k0) f |= 1;
        c.w[size_t(w)].flags = f;
    }
}

void wheel_torques(ex::Car& c, const DriveExtra& x, const WheelLayout& l) {
    for (int w = 0; w < count(l); ++w) {
        ex::Drive& t = c.t[size_t(w)];
        const F f = -x.d.tire_radius[axle_of(l, w) & 1] * (t.react_drive + t.react_brake);
        t.torque = f;
        const F rim = c.w[size_t(w)].rim_speed;
        if (rim == k0) continue;
        t.torque = k0 < rim ? f - t.brake_torque : f + t.brake_torque;
    }
}

void vectoring(ex::Car& c, const DriveExtra& x, const WheelLayout& l) {
    // (layout 7: the front motors first, 0x356418: not ported)
    const DrivelineBlock& d = x.d;
    for (int k = 0; k < l.groups && k < 4; ++k) {
        const int a = l.group_first[size_t(k)] & 3, b = l.partner[size_t(a)];
        if (b == 0x7F || b >= 4) continue;
        const u8 axle = l.group_axle[size_t(k)] & 1;
        const u8 type = d.diff_type[axle];
        if (type < 6 || !(type < 8)) continue;
        const F v = c.speed;
        if (v < kKmh10 || F::raw(0x4247FFFF) < v) continue; // 10 km/h .. 50 m/s
        const F steer = c.steer_cmd;
        F s = steer < k0 ? -steer : steer;
        s = s * F::raw(0x40747648); // 1 / 15 degrees
        if (kOne < s) s = kOne;
        const F tq = s * d.diff_initial[axle];
        if (tq == k0) continue;
        F m = (c.w[size_t(a)].rim_speed + c.w[size_t(b)].rim_speed) * kHalf;
        const F dm = m * F::raw(0x3E000000);
        if (k0 < steer) m = m + dm;
        else if (steer < k0) m = m - dm;
        const F more = tq * F::raw(0x3F100000), less = tq * F::raw(0x3EE00000);
        ex::Drive& ta = c.t[size_t(a)];
        ex::Drive& tb = c.t[size_t(b)];
        const F rb = c.w[size_t(b)].rim_speed;
        if (rb < m) tb.torque = tb.torque + less, ta.torque = ta.torque - more;
        else if (m < rb) tb.torque = tb.torque - more, ta.torque = ta.torque + less;
    }
}

void axle_step(ex::Car& c, DriveExtra& x, const WheelLayout& l, const DriveTables& t) {
    const DrivelineBlock& d = x.d;
    const F dt = c.dt;
    F tc = k0; // the clutch's torque, then (times the gear's ratio) the gearbox's
    if (c.clutch != 0) {
        for (int w = 0; w < count(l); ++w) {
            u8& f = c.w[size_t(w)].flags;
            const bool fr = ahead(l, w), rr = behind(l, w);
            switch (d.layout) {
            case 0:
            case 4:
            case 5:
            case 6:
                if (rr) f = u8(f | 8);
                break;
            case 1:
            case 9:
                if (fr) f = u8(f | 8);
                break;
            case 2:
                if (fr) f = u8(f | 8);
                if (rr) f = u8(f | 4);
                break;
            case 3:
            case 7:
            case 8:
            case 10:
                if (fr) f = u8(f | 4);
                if (rr) f = u8(f | 8);
                break;
            default: break;
            }
        }
    }
    if (d.kind == 3) {
        // 0x368628: the hybrid one-speed box's torque (none to the driveline while the clutch is open).
        tc = hybrid_box(c, x, x.hybrid, t.hybrid, x.e.idle);
        if (c.clutch == 0) tc = k0;
    } else if (c.clutch != 0) {
        // (layout 7's engine and motors, 0x3560F0 / 0x356BB8, and a box of kind 4's motor, 0x356E58: not ported)
        const F te = stepless(d) ? stepless_torque(c, x, t) : engine_torque(c, x, c.throttle);
        if (c.clutch == 1) tc = te;
        else {
            if (c.clutch == 3) tc = d.shift_clutch_torque;
            else {
                tc = c.torque_wot * F::raw(0x3D4CCCCC);
                if (k0 < te) tc = tc + clutch_slip_factor(c.clutch_engine - c.clutch_wheel) * te;
            }
            if (c.clutch_engine < c.clutch_wheel) tc = -tc;
            c.omega = c.omega + ((te - tc) * d.inv_engine_inertia) * dt;
        }
    }
    fuel_economy(c, x.fuel); // (0x354CE8: the fuel economy's display, in the game's km per liter)
    if (d.layout != 7) c.clutch_torque = k0 <= tc ? tc : -tc;
    const size_t g = ratio_gear(c, d) < 10 ? ratio_gear(c, d) : 9;
    const F tg = tc * d.ratio[g];
    std::array<F, 2> f{k0, k0}; // the wheels' torques per axle
    for (int w = 0; w < count(l); ++w) f[axle_of(l, w) & 1] = f[axle_of(l, w) & 1] + c.t[size_t(w)].torque;
    if (d.unequal != 0) {
        const size_t da = d.driven_axle & 1, o = 1 - da;
        f[o] = f[o] * (d.tire_radius[o] * d.inv_tire_radius[da]);
    }
    int mode = 0;
    F q = k0;
    switch (d.layout) {
    case 2: mode = 1; break;
    case 3:
    case 10: mode = 2; break;
    case 4:
    case 5:
    case 6:
    case 8:
    case 9: {
        F qa = k0;
        if (d.layout == 4) {
            if (d.awd_k == kOne) qa = kFull;
            else if (kHand < c.handbrake) qa = k0;
            else if (tg <= k0 || reverse_locked(c)) qa = kFull;
            else qa = d.awd_k * tg;
        } else if (d.layout == 5) {
            qa = awd5_gain(c, d, tg, f[0], f[1], t.awd);
            if (!(qa == kFull) && !(qa == -kFull)) qa = (qa * tg) * kHalf;
        } else if (d.layout == 6) {
            if (kHand < c.handbrake) qa = k0;
            else if (reverse_locked(c)) qa = kFull;
            else {
                qa = kFull;
                F st = c.steer_cmd;
                if (st < k0) st = -st;
                F gn = kOne - st * d.awd_k;
                if (gn < k0) gn = k0;
                if (gn < F::raw(0x3F7D70A3)) qa = gn;
            }
            if (!(qa == kFull) && !(qa == -kFull)) qa = (qa * tg) * kHalf;
        } else qa = d.awd_k * tg;
        bool locked = false;
        if (c.centre_locked != 0) {
            if (qa == kFull) locked = true;
            else if (qa == -kFull) c.centre_locked = 0;
            else {
                const size_t da = d.driven_axle & 1;
                F dx = (f[1 - da] - f[da]) - tg;
                if (dx < k0) dx = -dx;
                const F a = qa < k0 ? -qa : qa;
                if (a < dx) c.centre_locked = 0;
                else locked = true;
            }
        }
        if (locked) mode = 3;
        else {
            if (qa == -kFull) q = k0;
            else if (qa == kFull) q = d.awd_capacity;
            else q = qa < k0 ? -qa : qa;
            if (!(q == k0)) mode = 1;
        }
        if (mode != 0)
            for (int w = 0; w < count(l); ++w)
                if (l.geometry[size_t(w)] != d.driven_axle) c.w[size_t(w)].flags = u8(c.w[size_t(w)].flags | 4);
        break;
    }
    default: break;
    }
    F jref = k0;
    if (d.kind != 3 && c.clutch == 1) {
        jref = d.engine_inertia * d.ratio[g];
        if (c.gear == 0) jref = -jref;
        if (stepless(d)) jref = jref * x.ratio_word;
    }
    if (mode == 3 || mode == 2) {
        const F j = (d.axle_inertia[0] + d.axle_inertia[1]) + jref;
        const F acc = ((tg + f[0]) + f[1]) / j;
        const F half = tg * kHalf;
        const bool br = braked(c, l);
        if (mode == 3) {
            const F w0 = c.axle_speed[0];
            F w = w0 + acc * dt;
            if (br && (w * w0 < k0 || w0 == k0)) w = k0;
            c.axle_speed[0] = w;
            c.axle_speed[1] = w;
            c.drive_torque[1] = half;
            c.drive_torque[0] = half;
            return;
        }
        const F m = (c.axle_speed[0] + c.axle_speed[1]) * kHalf;
        F m1 = m + acc * dt;
        if (br && (m1 * m < k0 || m == k0)) m1 = k0;
        const F diff = c.axle_speed[0] - c.axle_speed[1];
        const F h = diff * kHalf;
        F h1 = h + (((f[0] - f[1]) - d.awd_k * diff) * d.inv_axle_inertia) * dt;
        if (h * h1 < k0) h1 = k0;
        c.drive_torque[1] = half;
        c.axle_speed[1] = m1 - h1;
        c.axle_speed[0] = m1 + h1;
        c.drive_torque[0] = half;
        return;
    }
    std::array<F, 2> j{k0, k0};
    switch (d.layout) {
    case 0:
    case 7:
        c.drive_torque[0] = k0, c.drive_torque[1] = tg;
        j = {k0, d.axle_inertia[1] + jref};
        break;
    case 1:
        c.drive_torque[0] = tg, c.drive_torque[1] = k0;
        j = {d.axle_inertia[0] + jref, k0};
        break;
    case 2: {
        const F kd = d.awd_k * (c.axle_speed[0] - c.axle_speed[1]);
        j = {d.axle_inertia[0] + jref, d.axle_inertia[1]};
        c.drive_torque[1] = kd;
        c.drive_torque[0] = tg - kd;
        break;
    }
    case 4:
    case 5:
    case 6:
    case 8:
        j = {d.axle_inertia[0], d.axle_inertia[1] + jref};
        if (c.axle_speed[1] < c.axle_speed[0]) c.drive_torque[1] = tg + q, c.drive_torque[0] = -q;
        else c.drive_torque[0] = q, c.drive_torque[1] = tg - q;
        break;
    case 9:
        j = {d.axle_inertia[0] + jref, d.axle_inertia[1]};
        if (c.axle_speed[1] < c.axle_speed[0]) c.drive_torque[1] = q, c.drive_torque[0] = tg - q;
        else c.drive_torque[0] = tg + q, c.drive_torque[1] = -q;
        break;
    default: break;
    }
    F d0 = k0;
    if (mode == 1) d0 = c.axle_speed[0] - c.axle_speed[1];
    for (int k = 0; k < l.groups && k < 4; ++k) {
        const int wa = l.group_first[size_t(k)] & 3;
        const size_t a = l.group_axle[size_t(k)] & 1;
        if (d.diff_type[a] == 0) continue;
        const F w0 = c.axle_speed[a];
        F acc = k0;
        if (!(j[a] == k0)) acc = ((c.drive_torque[a] + f[a]) / j[a]) * dt;
        const F w = w0 + acc;
        c.axle_speed[a] = w;
        if (c.t[size_t(wa)].brake_torque == k0 && c.t[size_t(partner(l, wa))].brake_torque == k0) continue;
        if (w * w0 < k0 || w0 == k0) c.axle_speed[a] = k0;
    }
    if (mode != 1) return;
    const F d1 = c.axle_speed[0] - c.axle_speed[1];
    const bool crossed = k0 < d0 ? d1 <= k0 : (d0 < k0 && k0 <= d1);
    if (!crossed) return;
    const size_t da = d.driven_axle & 1;
    c.axle_speed[1 - da] = c.axle_speed[da];
    if (u32(d.layout) - 4u < 3u || d.layout == 9) c.centre_locked = 1;
}

void wheel_speeds(ex::Car& c, const DriveExtra& x, const WheelLayout& l) {
    const DrivelineBlock& d = x.d;
    const F dt = c.dt;
    for (int k = 0; k < l.groups && k < 2; ++k) {
        const int a = l.group_first[size_t(k)] & 3, b = partner(l, a);
        const size_t axle = l.group_axle[size_t(k)] & 1;
        ex::Wheel& wa = c.w[size_t(a)];
        ex::Wheel& wb = c.w[size_t(b)];
        ex::Drive& ta = c.t[size_t(a)];
        ex::Drive& tb = c.t[size_t(b)];
        const F dw = wa.rim_speed - wb.rim_speed; // the last step's speeds
        const u8 type = d.diff_type[axle];
        u8 kind = type; // (0 each wheel alone, 1 locked, any other through the differential)
        auto transfer = [&](F v) {
            if (k0 < dw) ta.torque = ta.torque - v, tb.torque = tb.torque + v;
            else if (dw < k0) ta.torque = ta.torque + v, tb.torque = tb.torque - v;
        };
        if (type == 3 || type == 4) { // the clutch-pack differential
            const F td = c.drive_torque[size_t(k)];
            F cap = k0 <= td ? td * d.diff_accel[axle] : d.diff_decel[axle] * -td;
            if (cap < d.diff_initial[axle]) cap = d.diff_initial[axle];
            bool slip = true;
            if (c.diff_lock[size_t(k)] != 0) {
                const F dx = ta.torque - tb.torque;
                const F c2 = cap + cap;
                if (c2 < dx || dx < -c2) c.diff_lock[size_t(k)] = 0;
                else kind = 1, slip = false;
            }
            if (slip) transfer(cap);
        } else if (type == 5) { // the viscous one
            F v = dw * dw;
            if (d.diff_initial[axle] < v) v = d.diff_initial[axle];
            transfer(d.diff_accel[axle] * v);
        }
        const F radius = d.unequal != 0 ? d.tire_radius[d.driven_axle & 1] : d.tire_radius[axle];
        if (kind == 0) { // each wheel alone
            for (int i = 0; i < l.group_wheels[size_t(k)] && i < 2; ++i) {
                const int w = i == 0 ? a : b;
                ex::Wheel& wh = c.w[size_t(w)];
                const ex::Drive& t = c.t[size_t(w)];
                const F w0 = wh.rim_speed;
                const F w1 = w0 + (t.torque * d.wheel_gain[axle]) * dt;
                bool zero = false;
                if (!(t.brake_torque == k0)) {
                    if (k0 < w0) zero = w1 < k0;
                    else if (w0 < k0) zero = k0 < w1;
                    else zero = true;
                }
                wh.rim_speed = zero ? k0 : w1;
            }
        } else if (kind == 1) { // both at the axle's speed
            const F v = radius * c.axle_speed[size_t(k)];
            wb.rim_speed = v;
            wa.rim_speed = v;
        } else {
            F h = dw * kHalf + (((ta.torque - tb.torque) * d.wheel_gain[axle]) * kHalf) * dt;
            if (dw * h < k0) {
                if (u32(type) - 3u < 2u) c.diff_lock[size_t(k)] = 1;
                h = k0;
            }
            const F v = radius * c.axle_speed[size_t(k)];
            wa.rim_speed = v + h;
            wb.rim_speed = v - h;
        }
    }
}

void engine_speed(ex::Car& c, DriveExtra& x, const WheelLayout& l) {
    const DrivelineBlock& d = x.d;
    const EngineBlock& e = x.e;
    const F dt = c.dt;
    if (c.clutch == 0) {
        const F idle = int_to_f(e.idle) * kRpm;
        // (layout 7: the engine charging its store, 0x355F80: not ported)
        const F t = engine_torque(c, x, c.throttle);
        const F w = c.omega + (t * d.inv_engine_inertia) * dt;
        c.omega = w < idle ? idle : w;
        x.marks[0] = 0;
        engine_rpm(c, x);
        return;
    }
    const F ws = wheel_side_speed(c, d);
    F w = c.omega;
    u8 s = c.clutch;
    if (s == 1) {
        w = stepless(d) ? x.ratio_word * ws : ws;
        c.omega = w;
    } else if (u32(s) - 2u < 2u) {
        const u8 style = style_of(c);
        if ((style == 3 || style == 6) && c.clutch_req == kOne) {
            // The pedal clutch pressed home: each group's axle and wheels to the engine's speed (each group from the last
            // one's wheel speed on, as the program carries it).
            F q = c.omega / d.ratio[ratio_gear(c, d) < 10 ? ratio_gear(c, d) : 9];
            for (int k = 0; k < l.groups && k < 2; ++k) {
                c.axle_speed[size_t(k)] = q;
                const int a = l.group_first[size_t(k)] & 3;
                q = q * d.tire_radius[l.group_axle[size_t(k)] & 1];
                c.w[size_t(a)].rim_speed = q;
                if (l.partner[size_t(a)] != 0x7F && l.partner[size_t(a)] < 4) c.w[l.partner[size_t(a)]].rim_speed = q;
            }
            s = 1;
            c.clutch = 1;
            w = c.omega;
        } else {
            const F e0 = c.clutch_engine, w0 = c.clutch_wheel;
            if ((e0 <= w0 && ws <= w) || (w0 <= e0 && w <= ws)) { // the speeds crossed: locked
                c.clutch = 1;
                c.shift_lockout = 12;
                c.omega = ws;
                w = ws;
                s = 1;
            }
        }
        if (s == 3 && c.clutch_wheel <= int_to_f(e.idle) * kRpm) c.clutch = 0, s = 0;
        if (s == 2 && F::raw(0x3F400000) < c.brake_pedal) {
            if (w < int_to_f(s32(e.idle) + 500) * kRpm) c.clutch = 0;
            else if (c.speed < kKmh10 && -kKmh10 < c.speed) c.clutch = 0;
        }
    }
    if (w < k0) c.omega = k0;
    else if (const F cap = int_to_f(s32(e.rev_limit) + 10) * kRpm; cap <= w) c.omega = cap;
    if (c.clutch != 3) x.marks[0] = 0;
    engine_rpm(c, x);
}

int gear_for_speed(const ex::Car& c, const DriveExtra& x, F v) {
    if (v < k0) return 0;
    const DrivelineBlock& d = x.d;
    if (d.kind == 3 || d.kind == 1) return 1;
    const int n = d.gears;
    for (int g = 1; g < n && g < 10; ++g) {
        const s32 rpm = s32(d.up_rpm) - (g == 1 ? 2000 : 500);
        if (v < (int_to_f(rpm) * kRpm) / d.gear_speed[size_t(g)]) return g;
    }
    return n;
}

void gear_step(ex::Car& c, DriveExtra& x, const WheelLayout& l, const TireExtra& tires, const DriveTables& t) {
    const DrivelineBlock& d = x.d;
    if (d.kind == 3 || d.kind == 1 || c.p.special == 2) {
        // 0x351EB0 (a one-speed box after 0x3694D0, not ported; a stepless box of kind 1; a special car of kind 2):
        // forward or reverse alone.
        int g = -1;
        if (c.hold != 0) {
            if (c.gear != 1) g = 1;
        } else if (c.reverse_req != 0) {
            if (c.gear != 0) g = 0;
        } else if (kKmh1 < c.speed || k0 < c.throttle) {
            if (c.gear == 0) g = 1;
        } else if (c.speed < -kKmh1 && c.gear != 0) g = 0;
        if (g == -1) return;
        change_gear(c, g);
        if (stepless(d)) c.clutch = 2;
        return;
    }
    const int g = gear_decision(c, x, l, tires, t);
    if (g == -1) return;
    if (c.clutch != 0) {
        if (g == 0 || c.gear == 0) c.clutch = 2;
        else {
            if (d.kind != 2) c.clutch = 3;
            if (c.gear < g) x.marks[0] = 1;
            else if (g < c.gear) x.marks[0] = -1;
        }
    }
    change_gear(c, g);
}

} // namespace gt4r::sim::rules
