// Gran Turismo 4's hybrid one-speed box (rules_hybrid.h): 0x368628 and its helpers, in the program's arithmetic and order.

#include "gt4r/sim/rules_hybrid.h"

#include "gt4r/sim/program.h"
#include "gt4r/sim/rules_drive.h"

namespace gt4r::sim::rules {

namespace {
using gt3r::ee::int_to_f;
const F k0 = F(), k1 = F::raw(0x3F800000), kSixty = F::raw(0x42700000), kTwoPi = F::raw(0x40C90FDA);
const F kKw = F::raw(0x38DBC307); // (rpm x N m to kW)

template <size_t N> F bisect(const ex::Table<N>& t, F v) { return t.n > 0 ? gt3r::sim::exact::lookup_bisect(t.n, t.x.data(), t.y.data(), v) : F(); }

ex::Table<12> read_table(const Program& p, u32 at) {
    ex::Table<12> t;
    t.n = s16(p.h(at));
    const u32 x = p.w(at + 4), y = p.w(at + 8);
    for (u32 k = 0; k < 12 && s32(k) < t.n; ++k) t.x[k] = F::raw(p.w(x + 4 * k)), t.y[k] = F::raw(p.w(y + 4 * k));
    return t;
}

F rpm_of(F omega) { return (omega * kSixty) / kTwoPi; }

// 0x367FD8 with 0x367DD0 / 0x367EB8: the motor's greatest torque at the driveline's speed (by the up-shift class: 3 the G
// Touring's curve).
F motor_torque(const DrivelineBlock& d, F omega) {
    F r = rpm_of(omega);
    if (r < k0) r = -r;
    if (d.x13 == 3) {
        if (r < F::raw(0x44960000)) return F::raw(0x43C7EB83);
        if (r < F::raw(0x44BB8000)) return F::raw(0x48E90B72) / r;
        if (r < F::raw(0x45B86000)) {
            F a = (r - F::raw(0x44BB8000)) * F::raw(0x3ACA90DF);
            a = F::raw(0x42880000) - a;
            a = a * F::raw(0x44330CCC);
            a = a * F::raw(0x411CCCCC);
            return a / r;
        }
        if (r < F::raw(0x45CB2000)) return ((F::raw(0x45CB2000) - r) / F::raw(0x44160000)) * F::raw(0x42919C07);
        return k0;
    }
    if (r < F::raw(0x43C80000)) return F::raw(0x43AEEE12);
    if (r < F::raw(0x44820000)) return F::raw(0x43AEEE12) - (r - F::raw(0x43C80000)) * F::raw(0x3D93B49B);
    if (r < F::raw(0x45AF0000)) return F::raw(0x489A3884) / r;
    if (r < F::raw(0x45BB8000)) return ((F::raw(0x45BB8000) - r) / F::raw(0x43C80000)) * F::raw(0x42619A5A);
    return k0;
}
// 0x368040 / 0x368088 / 0x3680C0 / 0x3680F8: the engine's speeds and the store's size by the up-shift class.
F engine_top(const DrivelineBlock& d) { return d.x13 == 3 ? F::raw(0x4402E651) : d.x13 == 2 ? F::raw(0x43EB9E92) : F::raw(0x43D17082); }
F follow_max(const DrivelineBlock& d) { return d.x13 == 3 ? F::raw(0x4482E651) : F::raw(0x442A2B69); }
F follow_min(const DrivelineBlock& d) { return d.x13 == 3 ? F::raw(0xC482E651) : F::raw(0xC42A2B69); }
F store_size(const DrivelineBlock& d) { return d.x13 == 3 ? F::raw(0x43FA0000) : F::raw(0x43890000); }
// 0x368128: the engine's fuel rate at its power, 0 below idle.
F fuel_rate(const ex::Car& c, const DrivelineBlock& d, const HybridTables& t, F omega, F power, F idle) {
    if (c.omega < idle) return k0;
    if (power <= k0) return k0;
    const F r = rpm_of(omega);
    return power * (d.x13 == 1 ? bisect(t.fuel_class1, r) : bisect(t.fuel_other, r));
}
// 0x3681E0: the power asked for at a speed and throttle.
F power_wanted(const HybridTables& t, F speed, F throttle) {
    const F least = F::raw(0x3DCCCCCC);
    F v, a, b;
    if (least <= throttle) v = bisect(t.power_up, speed), b = throttle - least, a = k1;
    else v = bisect(t.power_low, speed), b = throttle - least, a = k0;
    a = a - least;
    return (v * b) / a;
}
// 0x3685A0: the motor's demand from the throttle, raised at a walk.
F demand(const ex::Car& c, F throttle, F speed) {
    if (c.gear == 0) speed = -speed;
    const F walk = F::raw(0x40855554);
    if (speed <= walk) return throttle;
    F u = (walk - speed) / walk;
    if (k0 < u) u = k0;
    if (u < F::raw(0xBF800000)) u = F::raw(0xBF800000);
    u = u * F::raw(0x3F4CCCCC);
    return u + (k1 - u) * throttle;
}
} // namespace

HybridTables hybrid_tables(const Program& program) {
    HybridTables t;
    t.fuel_class1 = read_table(program, 0x620BA8), t.fuel_other = read_table(program, 0x620BF8);
    t.power_up = read_table(program, 0x620C80), t.power_low = read_table(program, 0x620C90);
    return t;
}

ex::Table<12> hybrid_engine_speeds(const ex::Table<24>& torque) {
    ex::Table<12> e{};
    s32 n = s32(torque.n) + 1;
    if (!(n < 13)) n = 12;
    e.n = s16(n);
    for (s32 k = 1; k < n; ++k) {
        const F w = torque.x[size_t(k - 1)];
        F p = torque.y[size_t(k - 1)] / F::raw(0x411CCCCC);
        p = p * ((w * kSixty) / kTwoPi);
        p = p / F::raw(0x44330CCC);
        e.x[size_t(k)] = p * F::raw(0x3F3C49BA);
        e.y[size_t(k)] = w;
    }
    return e;
}

void hybrid_clutch_request(ex::Car& c) {
    const F tenth = F::raw(0x3DCCCCCC);
    if (c.race_hold != 0 || c.hold != 0 || c.driver_kind == 8) return void(c.clutch_req = k0);
    if (c.clutch == 0) return void(c.clutch_req = c.brake_pedal < tenth ? k1 : k0);
    c.clutch_req = k1;
    if (!(tenth < c.brake_pedal)) return;
    if (c.speed < F::raw(0x3E8E38E3) && F::raw(0xBE8E38E3) < c.speed) c.clutch_req = k0;
}

void hybrid_start(HybridState& h) { h.store = k1, h.mode = 0, h.engine_target = k0, h.flags = 0; }

F hybrid_box(ex::Car& c, DriveExtra& x, HybridState& h, const HybridTables& t, u16 idle_rpm) {
    const DrivelineBlock& d = x.d;
    const F idle = int_to_f(idle_rpm) * F::raw(0x3DD6774E);
    const F dt = c.dt;
    bool engine_on = false, engine_stall = false; // sp+4 / sp+8
    const bool no_request = c.clutch_req == k0;   // sp+C
    F shaft = c.axle_speed[d.driven_axle & 1] * d.ratio[c.gear < 10 ? c.gear : 9];
    if (c.gear == 0) shaft = -shaft;
    bool braking = no_request && F::raw(0x3F4CCCCC) < c.brake_pedal && k0 < c.throttle_pedal;
    // The store's mode by its eighths.
    {
        const F e = h.store * F::raw(0x41000000) + F::raw(0x3F000000);
        if (h.mode == 1) {
            if (F::raw(0x40D99999) <= e) h.mode = 0;
            else if (e < F::raw(0x3FC00000)) h.mode = 2;
        } else if (h.mode == 0) {
            if (e < F::raw(0x40C66666)) h.mode = 1;
        } else if (h.mode == 2) {
            if (F::raw(0x40200000) <= e) h.mode = 1;
        }
    }
    const bool on = h.mode != 0, low = h.mode == 2;
    bool cruise = d.x13 == 3 && F::raw(0x3CE38E37) <= c.speed;
    if (c.gear == 0) cruise = false;
    F wanted = k0, target_power = k0; // f30, f23
    F speed = c.speed;
    if (cruise) {
        wanted = power_wanted(t, speed, c.throttle);
        if (!(wanted <= target_power)) {
            const F engine_power = (c.torque_wot * rpm_of(c.omega)) * kKw;
            if (!(engine_power < wanted)) target_power = wanted;
            else {
                F less = k0;
                if (!(speed < less)) less = speed < F::raw(0x40B1C71B) ? speed * F::raw(0x40900001) : F::raw(0x41C80000);
                target_power = wanted - less;
                if (target_power < k0) target_power = k0;
            }
        }
    }
    const F want = demand(c, c.throttle, speed); // f31
    F motor_demand;                               // f20
    if (k0 < c.brake_pedal && !no_request) {
        F s = shaft < k0 ? -shaft : shaft;
        motor_demand = (-c.brake_pedal) * F::raw(0x40800000);
        s = s * F::raw(0x3CC391D3);
        if (k1 < s) s = k1;
        motor_demand = motor_demand * s;
        if (motor_demand < F::raw(0xBF800000)) motor_demand = F::raw(0xBF800000);
    } else {
        motor_demand = want;
        if (shaft < k0) {
            F f = shaft / F::raw(0xC3517082);
            f = (k1 - f) * F::raw(0x40800000);
            if (k1 < f) f = k1;
            else if (f < k0) f = k0;
            motor_demand = want * f;
        }
    }
    F motor;                                     // f26: the motor's torque
    if (motor_demand < k0 && shaft < F::raw(0x3DD6774E) && F::raw(0xBDD6774E) < shaft) motor = k0;
    else motor = motor_torque(d, shaft) * motor_demand;
    if (braking || c.race_hold != 0 || cruise) motor = k0;
    F motor_power = (motor * rpm_of(shaft)) * kKw; // f24
    if (shaft < k0) motor_power = -motor_power;
    bool stopped = false;                         // s6
    bool driving = !braking && k0 < motor;        // sp+0x10
    bool running = false;                         // sp+0x14
    bool regen = motor < k0 && !no_request;       // fp
    const F top = engine_top(d);                  // f21
    F f22 = k0, f2 = k0, f29 = k0;
    if (c.gear == 1) f2 = c.throttle;
    F engine_speed = top;                         // f20
    F f27 = F::raw(0xC2D17082);
    if (braking) stopped = true;
    else {
        engine_speed = f22;
        if (!(speed < f22)) {
            bool skip = false;
            if (speed < F::raw(0x419B8E38) && !cruise && motor_power < F::raw(0x41200000)) skip = true;
            if (!skip) {
                if (cruise) {
                    engine_speed = bisect(hybrid_engine_speeds(x.e.torque), target_power);
                    if (top < engine_speed) engine_speed = top;
                } else
                    engine_speed = top * f2;
            }
        }
    }
    speed = c.speed;
    if (F::raw(0x419B8E38) <= speed && engine_speed < F::raw(0x42D17082)) {
        engine_speed = F::raw(0x42D17082);
        if (on) stopped = true, f29 = engine_speed;
        else f27 = F::raw(0x42D388AC);
    }
    F omega = c.omega;
    if (engine_speed < idle && !low) {
        engine_speed = k0;
        if (c.throttle_pedal == k0) f27 = F::raw(0x42D388AC);
    }
    if (omega < f27) engine_stall = true;
    F gap;
    if (on && !braking) {
        f29 = low ? F::raw(0x43517082) : F::raw(0x42D17082);
        const F before = engine_speed;
        if (engine_speed < idle) stopped = true;
        engine_speed = engine_speed + f29;
        if (top < engine_speed) {
            engine_speed = top;
            if (!low) f29 = engine_speed - before;
        }
        engine_stall = false;
    }
    gap = engine_speed - shaft;
    F follow = h.engine_target;                   // f22
    F aim = engine_speed + gap * F::raw(0x40200000); // f21
    {
        const F hi = follow_max(d), lo = follow_min(d);
        bool clipped = false;
        if (hi < aim) aim = hi, clipped = true;
        else if (aim < lo) aim = lo, clipped = true;
        if (clipped) {
            engine_speed = shaft + (aim - shaft) / F::raw(0x40600000);
            if (engine_speed < idle) engine_speed = k0;
        }
    }
    F rate = k0;
    if ((omega < idle && idle <= engine_speed) || (omega <= idle && engine_speed == k0)) rate = F::raw(0x45A39FE5);
    if (follow < aim) {
        if (rate == k0) rate = F::raw(0x4482E651);
        follow = follow + rate * dt;
        if (aim < follow) follow = aim;
    } else if (aim < follow) {
        if (rate == k0) rate = F::raw(0x4402E651);
        follow = follow - rate * dt;
        if (follow < aim) follow = aim;
    }
    h.engine_target = follow;
    c.omega = shaft + (follow - shaft) / F::raw(0x40600000);
    if (idle <= c.omega) engine_on = true;
    F engine_torque_v = k0, engine_power = k0;    // f21, f20
    omega = c.omega;
    if (engine_on && !engine_stall) {
        F throttle = k1;
        if (!cruise) {
            throttle = engine_speed / F::raw(0x43D17082) + k0;
            if (k1 < throttle) throttle = k1;
        }
        engine_torque_v = engine_torque(c, x, throttle);
        if (!cruise && want * engine_torque_v < k0) engine_torque_v = k0;
        omega = c.omega;
        engine_power = (engine_torque_v * rpm_of(omega)) * kKw;
    }
    if (cruise) motor_power = wanted - engine_power;
    x.fuel.used = x.fuel.used + fuel_rate(c, d, t, omega, engine_power, idle) * dt;
    F charge = k0, f4 = k0;                       // f22, f4
    if (k0 < engine_power) {
        if (stopped) {
            if (cruise) charge = engine_power, engine_torque_v = k0, engine_power = k0;
            else f4 = engine_torque_v, engine_torque_v = k0;
        } else if (cruise) {
            charge = (engine_power * f29) / c.omega;
            if (engine_power < charge) charge = engine_power;
            else if (charge < k0) charge = k0;
            engine_power = engine_power - charge;
        } else {
            f4 = (engine_torque_v * f29) / c.omega;
            if (engine_torque_v < f4) f4 = engine_torque_v;
            else if (f4 < k0) f4 = k0;
            engine_torque_v = engine_torque_v - f4;
            f4 = f4 + engine_torque_v / F::raw(0x40600000);
        }
        if (cruise) motor_power = motor_power + charge;
        else if (k0 < f4) charge = (f4 * rpm_of(c.omega)) * kKw;
    }
    if (engine_on && !stopped) {
        if (cruise) {
            const F r = rpm_of(shaft);
            engine_torque_v = r < F::raw(0x42C80000) ? k0 : engine_power / (r * kKw);
        } else
            engine_torque_v = (engine_torque_v * F::raw(0x40200000)) / F::raw(0x40600000);
    }
    if (cruise) {
        if (k0 < charge) running = true;
        if (k0 < wanted && k0 < engine_power) running = true;
    } else if (k0 < f4)
        running = true;
    if (low && charge < motor_power) {
        F s = charge / motor_power;
        if (s < k0) s = -s;
        motor = motor * s;
        motor_power = charge;
    }
    if (cruise) {
        const F most = motor_torque(d, shaft);
        const F r = rpm_of(shaft);
        const F per = r * kKw;
        F p = most * per;
        if (p < k0) p = -p;
        if (p < motor_power) motor_power = p, motor = most;
        else if (motor_power < -p) motor_power = -p, motor = -most;
        else if (F::raw(0xBDD6774E) < r && r < F::raw(0x3DD6774E)) motor = k0 <= motor_power ? most : -most;
        else motor = motor_power / per;
    }
    charge = charge - motor_power;
    {
        F q = (charge * F::raw(0x447A0000)) / store_size(d);
        q = (dt * q) / F::raw(0x46B6D000);
        F s = h.store + q;
        h.store = s;
        if (k1 < s) h.store = k1;
        else if (s < k0) h.store = k0;
    }
    if (cruise) {
        if (!braking && k0 < motor_power) driving = true;
        if (motor_power < k0 && !no_request) regen = true;
    }
    if (engine_stall) engine_on = false;
    h.flags = 0;
    if (braking) h.flags = 1;
    else if (engine_on && !stopped && k0 < engine_torque_v && k0 < wanted) driving = true, h.flags = 1;
    if (braking || driving) h.flags = u8(h.flags | 4);
    else if (regen && wanted < k0) h.flags = u8(h.flags | 2);
    if (running || h.mode != 0) h.flags = u8(h.flags | 8);
    if (k0 < charge) h.flags = u8(h.flags | 0x10);
    else if (charge < k0) h.flags = u8(h.flags | 0x20);
    return engine_torque_v + motor;
}

} // namespace gt4r::sim::rules
