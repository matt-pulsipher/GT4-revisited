// Gran Turismo 4's own routines of the race step: layout 7's motors (rules_motors.h).

#include "gt4r/sim/rules_motors.h"

#include "base/ee_math.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/rules_drive.h"
#include "gt4r/sim/rules_hybrid.h"
#include "gt4r/sim/rules_tires.h"

namespace gt4r::sim::rules {

namespace {
using gt3r::ee::int_to_f;
const F kZero = F(), kOne = F::raw(0x3F800000), kMinusOne = F::raw(0xBF800000), kHalf = F::raw(0x3F000000);
const F kKw = F::raw(0x38DBC307);    // rpm x N m -> kW (2 pi / 60,000)
const F kRpmKw = F::raw(0x46151B5C); // kW / rpm -> N m (60,000 / 2 pi)

s8 counter(const HybridState& h) { return s8(u8(h.store.bits >> 8)); }
void set_counter(HybridState& h, s8 v) { h.store.bits = (h.store.bits & ~0xFF00u) | (u32(u8(v)) << 8); }

// 0x355E40: the flow counter.
void count(HybridState& h, const MotorTables& t, F x) {
    s32 v = s32(counter(h)) + gt3r::ee::to_int(x * t.flow_scale);
    if (v < -100) v = -100;
    if (!(v < 101)) v = 100;
    set_counter(h, s8(v));
}

// The engine's share of the full power with the rear motor's beside it (3560F0 / 3561D0).
F engine_share(const ex::Car& c, const MotorTables& t) {
    F p = c.torque_wot + c.torque_friction;
    p = p * int_to_f(c.rpm);
    p = p * kKw;
    return p / (p + t.rear + t.rear);
}

// A front motor's torque limits by its wheel's speed (356418's loops).
} // namespace

MotorTables motor_tables(const u8* b) {
    MotorTables t;
    const F hundred = F::raw(0x42C80000), g = F::raw(0x4474FFFE); // 979.99
    const F f6 = int_to_f(b[0]), f18 = int_to_f(b[1]);
    const F f11 = f6 + f6 + f18;
    const F f5 = int_to_f(b[7]) * f11;
    t.front = f6, t.rear = f18;
    t.front_drive = int_to_f(s32(b[2]) * 10), t.rear_drive = int_to_f(s32(b[3]) * 10);
    t.front_regen = int_to_f(s32(b[4]) * 10), t.rear_regen = int_to_f(s32(b[5]) * 10);
    t.front_scale = int_to_f(b[6]) * F::raw(0x3DCCCCCC);
    t.capacity = f5;
    t.front_charge_cap = int_to_f(s32(b[8]) * 5), t.rear_charge_cap = int_to_f(s32(b[9]) * 5);
    t.idle_rpm = int_to_f(b[10]) * hundred;
    t.idle_gain = int_to_f(b[11]) / F::raw(0x47C35000);
    t.idle_share = int_to_f(b[12]) / hundred;
    t.idle_power = int_to_f(b[13]);
    t.low = f5 * int_to_f(b[14]) / hundred;
    t.high = f5 * int_to_f(b[15]) / hundred;
    t.reserve = int_to_f(b[16]) / f6;
    t.gear_lo = s8(b[17]), t.gear_hi = s8(b[18]), t.gear_min = s8(b[19]);
    t.front_share = int_to_f(b[20]) / hundred;
    t.drive_accel = int_to_f(b[21]) / g, t.drive_lat = int_to_f(b[22]) / g, t.drive_dead = int_to_f(b[23]) / hundred;
    t.regen_accel = int_to_f(b[24]) / g, t.regen_lat = int_to_f(b[25]) / g, t.regen_dead = int_to_f(b[26]) / hundred;
    t.flow_scale = F::raw(0x45BB8000) / f11;
    return t;
}

MotorTables motor_tables(const Program& program) {
    u8 b[0x1B];
    for (u32 k = 0; k < sizeof b; ++k) b[k] = program.b(kMotorBytes + k);
    return motor_tables(b);
}

F motor_store(const HybridState& h) { return h.engine_target; }

void motor_start(HybridState& h, const MotorTables& t) {
    h.engine_target = kOne * t.capacity;
    h.store.bits &= ~0xFFFFu;
}

F motor_charge(HybridState& h, const MotorTables& t, F x) {
    const F s = h.engine_target;
    count(h, t, x);
    const F v = s + x;
    if (t.capacity < v) {
        x = x - (v - t.capacity);
        h.engine_target = t.capacity;
    } else
        h.engine_target = v;
    return x;
}

F motor_draw(HybridState& h, const MotorTables& t, F x) {
    F s = h.engine_target;
    count(h, t, -x);
    if (s < x) {
        const F d = x - s;
        s = kZero;
        x = x - d;
    } else
        s = s - x;
    h.engine_target = s;
    return x;
}

F motor_level(const HybridState& h, const MotorTables& t) {
    const F s = h.engine_target;
    if (t.high <= s) return kOne;
    if (s <= t.low) return kZero;
    return (s - t.low) / (t.high - t.low);
}

F motor_demand(const ex::Car& c, const HybridState& h, const MotorTables& t) {
    const F share = engine_share(c, t);
    const F x = c.clutch == 2 ? c.throttle_pedal : c.throttle;
    if (x <= share) return kMinusOne;
    F d = (x - share) + (x - share);
    d = d / (kOne - share);
    d = d * motor_level(h, t);
    const s32 gear = s32(c.gear);
    if (!(s32(t.gear_min) < gear)) d = kZero;
    else if (kOne < d && (gear < s32(t.gear_lo) || s32(t.gear_hi) < gear)) d = kOne;
    return d + kMinusOne;
}

F motor_engine_throttle(const ex::Car& c, const MotorTables& t) {
    F v = c.throttle / engine_share(c, t);
    if (kOne < v) v = kOne;
    return v;
}

F motor_wanted_lateral(const ex::Car& c, const TireExtra& x, const WheelLayout& l) {
    F v = c.speed * gt3r::ee::tan(c.steer_cmd);
    v = v / c.p.wheelbase;
    F a = c.speed * v;
    if (a < kZero) a = -a;
    const int w = kZero < c.steer_cmd ? 1 : 0;
    const Tire& r = tire_record(x, l, w, c.w[size_t(w)].surface);
    F s = c.steer_cmd * F::raw(0x3FC00000);
    if (s < kZero) s = -s;
    F g = ex::lookup(r.curve_a, s) * r.grip;
    g = g + g;
    if (g < kZero) g = -g;
    if (g < a) a = g;
    if (c.steer_cmd < kZero) a = -a;
    return a;
}

void front_motors(ex::Car& c, DriveExtra& x, const TireExtra& tires, const WheelLayout& l, const MotorTables& t) {
    HybridState& h = x.hybrid;
    const F dt = c.dt;
    c.clutch_torque = kZero;
    const F pedal = c.clutch == 3 ? c.throttle_pedal : c.throttle;
    F power = engine_output_torque(c, x, pedal) * int_to_f(c.rpm);
    power = power * kKw;
    const F demand = motor_demand(c, h, t);
    power = power + t.rear * demand;
    power = power * t.front_share;
    power = power / (kOne - t.front_share);
    F want = power / (t.front + t.front);
    if (kOne < want) want = kOne;
    else if (want < kMinusOne) want = kMinusOne;
    {
        const F level = motor_level(h, t);
        const F cap = level + t.reserve * (kOne - level);
        if (cap < want) want = cap;
    }
    // The front wheels' braking charge; which of them is all but stopped (under 1 km/h).
    std::array<bool, 2> slow{};
    for (int i = 0; i < 2; ++i) {
        F v = c.w[size_t(i)].rim_speed;
        if (v < kZero) v = -v;
        slow[size_t(i)] = v < F::raw(0x3E8E38E3);
        F r = c.t[size_t(i)].react_brake;
        if (r < kZero) r = -r;
        F e = r * v * F::raw(0x3A832923);
        if (t.front_charge_cap < e) e = t.front_charge_cap;
        motor_charge(h, t, e * dt);
    }
    F k = x.d.tire_radius[0] * F::raw(0x40C90FDA);
    k = k * F::raw(0x411CCCCC);
    k = k * F::raw(0x44330CCC);
    const F torque_k = k / F::raw(0x4230851E);
    std::array<F, 2> share{want, want}, out{};
    if (kZero < want) {
        const F cap = t.front_scale * t.front_drive;
        F d = motor_wanted_lateral(c, tires, l) * t.drive_lat - c.a_y * t.drive_accel;
        if (kZero < d) {
            d = d - t.drive_dead;
            if (!(d < kZero)) {
                d = d * kHalf;
                share[1] = want * (d + kOne), share[0] = want * (kOne - d);
                if (kOne < share[1]) share[0] = share[0] - (share[1] - kOne), share[1] = kOne;
                if (share[0] < kMinusOne) share[0] = kMinusOne;
            }
        } else if (d < kZero) {
            d = d + t.drive_dead;
            if (!(kZero < d)) {
                d = d * kHalf;
                share[0] = want * (kOne - d), share[1] = want * (d + kOne);
                if (kOne < share[0]) share[1] = share[1] - (share[0] - kOne), share[0] = kOne;
                if (share[1] < kMinusOne) share[1] = kMinusOne;
            }
        }
        for (int i = 0; i < 2; ++i) {
            out[size_t(i)] = kZero;
            if (!slow[size_t(i)]) {
                const F rim = c.w[size_t(i)].rim_speed;
                F s = share[size_t(i)] * (t.front * torque_k / rim);
                if (rim < kZero) s = -s;
                if (cap < s) s = cap;
                else if (s < -cap) s = -cap;
                share[size_t(i)] = s;
                F o = s * rim / torque_k;
                if (rim < kZero) o = -o;
                out[size_t(i)] = o;
            } else if (c.brake_pedal == kZero)
                share[size_t(i)] = cap;
        }
        const F asked = (out[0] + out[1]) * dt;
        if (kZero < asked) {
            const F f = motor_draw(h, t, asked) / asked;
            share[1] = share[1] * f, out[0] = out[0] * f, share[0] = share[0] * f, out[1] = out[1] * f;
        }
        F drawn = kZero;
        if (kZero < out[0]) drawn = out[0] + drawn;
        if (kZero < out[1]) drawn = drawn + out[1];
        c.clutch_torque = drawn / t.front;
        if (c.gear == 0) share[0] = -share[0], share[1] = -share[1];
        c.t[0].torque = c.t[0].torque + share[0];
        c.t[1].torque = c.t[1].torque + share[1];
        return;
    }
    const F cap = t.front_scale * t.front_regen;
    std::array<F, 2> s{want, want};
    F d = motor_wanted_lateral(c, tires, l) * t.regen_lat - c.a_y * t.regen_accel;
    if (kZero < d) {
        d = d - t.regen_dead;
        if (!(d < kZero)) {
            d = d * kHalf;
            s[0] = want * (d + kOne), s[1] = want * (kOne - d);
            if (s[0] < kMinusOne) s[1] = s[1] - (s[0] + kOne), s[0] = kMinusOne;
            if (kZero < s[1]) s[1] = kZero;
        }
    } else if (d < kZero) {
        d = d + t.regen_dead;
        if (!(kZero < d)) {
            d = d * kHalf;
            s[1] = want * (kOne - d), s[0] = want * (d + kOne);
            if (s[1] < kMinusOne) s[0] = s[0] - (s[1] + kOne), s[1] = kMinusOne;
            if (kZero < s[0]) s[0] = kZero;
        }
    }
    for (int i = 0; i < 2; ++i) {
        F v = s[size_t(i)];
        if (!slow[size_t(i)]) {
            const F rim = c.w[size_t(i)].rim_speed;
            v = v * (t.front * torque_k / rim);
            if (rim < kZero) v = -v;
            if (v < -cap) v = -cap;
            else if (cap < v) v = cap;
        }
        if (c.gear == 0) v = -v;
        c.t[size_t(i)].torque = c.t[size_t(i)].torque + v;
    }
}

F rear_motor(ex::Car& c, HybridState& h, const MotorTables& t) {
    const F demand = motor_demand(c, h, t);
    const F dt = c.dt;
    F e = kZero;
    for (int i = 2; i < 4; ++i) {
        const ex::Wheel& w = c.w[size_t(i)];
        if (w.brake == kZero) continue;
        F p = c.t[size_t(i)].react_brake * w.rim_speed;
        if (p < kZero) p = -p;
        e = e + p * F::raw(0x3A832923);
    }
    if (t.rear_charge_cap < e) e = t.rear_charge_cap;
    motor_charge(h, t, e * dt);
    const F rpm = int_to_f(c.rpm);
    if (kZero < demand) {
        F q = t.rear * kRpmKw / rpm;
        if (t.rear_drive < q) q = t.rear_drive;
        F p = q * rpm;
        q = q * demand;
        p = p / kRpmKw;
        p = p * demand;
        p = p * dt;
        return q * (motor_draw(h, t, p) / p);
    }
    F q = t.rear * kRpmKw / rpm;
    if (t.rear_regen < q) q = t.rear_regen;
    F p = q * rpm;
    q = q * demand;
    p = p / kRpmKw;
    p = p * demand;
    p = -p;
    motor_charge(h, t, p * dt);
    return q;
}

F idle_charge(ex::Car& c, DriveExtra& x, const MotorTables& t) {
    const F rpm = int_to_f(c.rpm);
    F throttle = c.throttle;
    if (kOne <= x.hybrid.engine_target / t.capacity) return engine_torque(c, x, throttle);
    F cap = t.idle_power * kRpmKw;
    F share = c.torque_wot * t.idle_share;
    cap = cap / rpm;
    F raise = (t.idle_rpm - rpm) * t.idle_gain;
    if (cap < share) share = cap;
    const F f = (share + c.torque_friction) / (c.torque_wot + c.torque_friction);
    raise = raise + f;
    if (raise < kZero) raise = kZero;
    if (throttle < raise) throttle = raise;
    if (kOne < throttle) throttle = kOne;
    c.throttle = throttle;
    const F te = engine_torque(c, x, throttle);
    F p = share * rpm;
    const F out = te - share;
    p = p * kKw;
    p = p * c.dt;
    motor_charge(x.hybrid, t, p);
    return out;
}

} // namespace gt4r::sim::rules
