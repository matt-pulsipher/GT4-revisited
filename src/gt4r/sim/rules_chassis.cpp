// Gran Turismo 4's own routines of the race step: the ground contact, suspension, attitude and the body's motion (rules_chassis.h).

#include "gt4r/sim/rules_chassis.h"

#include "base/ee_math.h"

namespace gt4r::sim::rules {

void lateral_yaw(ex::Car& c, ChassisExtra& x, const TireExtra& t, const WheelLayout& l, bool air, const SlipForceGlobals& g, F driver_grip) {
    using gt3r::ee::cos;
    using gt3r::ee::sin;
    const int n = l.count < 4 ? l.count : 4;
    const F dt = c.dt;
    if (air) {
        c.lat_g = F(), c.a_y = F();
        for (int i = 0; i < n; ++i) x.wheel_lat[size_t(i)] = F();
        return;
    }
    F lat = F(), torque = F();
    for (int i = 0; i < n; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        const F steer = w.steer;
        if (g.steer_mode == 1) w.steer = w.steer + t.steer_offset[size_t(i)];
        else if (g.steer_mode == 2) w.steer = t.steer_offset[size_t(i)];
        const F gx = w.fx_drive + w.fx_brake;
        F fa, fb;
        if (t.wheel_xdd[size_t(i)] == 1) {
            fa = -cos(w.steer) * w.force_a;
            fb = -sin(w.steer) * w.force_a;
        } else {
            const F cs = cos(w.slip_dir), sn = sin(w.slip_dir);
            fa = -cs * w.force_a + sn * w.force_b;
            fb = -sn * w.force_a + -cs * w.force_b;
        }
        const F cam = t.camber_force[size_t(i)];
        fa = fa + -cos(w.steer) * cam;
        fb = fb + -sin(w.steer) * cam;
        const F hold = ex::low_speed_hold(w, true);
        fa = fa - cos(w.steer) * hold;
        fb = fb - sin(w.steer) * hold;
        fa = fa - sin(w.steer) * gx;
        fb = fb + cos(w.steer) * gx;
        x.wheel_lat[size_t(i)] = fa;
        lat = lat + fa;
        const u8 k = l.geometry[size_t(i)] & 1u;
        const u8 side = l.side[size_t(i)];
        const F lx = side == 0 ? -c.p.half_track[k] : side == 1 ? c.p.half_track[k] : F();
        const F dist = k ? c.p.dist_rear : c.p.dist_front;
        const s8 fa_pos = s8(l.fore_aft[size_t(i)]);
        const F ly = fa_pos > 0 ? dist : fa_pos < 0 ? -dist : F();
        torque = torque + (lx * fb - ly * fa);
        w.steer = steer;
    }
    c.yaw_accel = torque * c.p.inv_yaw_inertia;
    // 0x350D40: the yaw acceleration's follower.
    {
        const F k60 = F::raw(0x3C888888);
        const F e = c.yaw_accel - x.yaw_follow;
        x.yaw_follow_v = x.yaw_follow_v + (e * F::raw(0x43FA0000) - x.yaw_follow_v * F::raw(0x41A00000)) * k60;
        x.yaw_follow = x.yaw_follow + x.yaw_follow_v * k60;
    }
    const F r = c.yaw_rate + c.yaw_accel * dt;
    const F hi = F::raw(0x40C90FDA), lo = F::raw(0xC0C90FDA);
    c.yaw_rate = hi < r ? hi : r < lo ? lo : r;
    c.a_y = lat / x.mass;
    F grip = F();
    for (int i = 0; i < n; ++i) {
        const u8 s = c.w[size_t(i)].surface;
        const Tire& r2 = tire_record(t, l, i, s);
        grip = grip + (r2.mu[s % 24] * driver_grip) * r2.grip;
    }
    c.lat_g = c.a_y * gt3r::ee::int_to_f(n) / grip;
}

void stop_hold(ex::Car& c, const WheelLayout& l, bool air) {
    const F dt = c.dt;
    if (c.hold != 0) {
        c.vel.y = F(), c.drive_sum = F(), c.yaw_rate = F(), c.vel.x = F();
        return;
    }
    if (air) return;
    auto stops = [&] {
        if (c.race_hold != 0 || c.driver_kind == 8) return true;
        if (u8(c.driver_kind - 1) < 2 || c.driver_kind == 4 || c.control == 1 || c.control == 2) return false;
        if (!(F::raw(0xBE8E38E3) < c.speed && c.speed < F::raw(0x3E8E38E3))) return false;
        for (int i = 0; i < l.count && i < 4; ++i)
            if (!(c.w[size_t(i)].slip_fade == F())) return false;
        return F() < c.brake_pedal || F() < c.handbrake;
    };
    if (stops()) {
        c.lat_speed = F(), c.yaw_rate = F(), c.vel.x = F(), c.vel.y = F(), c.drive_sum = F();
        c.a_x = F(), c.a_y = F(), c.lat_g = F(), c.speed = F();
        return;
    }
    const F ax = c.a_x * dt, ay = c.a_y * dt;
    c.vel.x = c.vel.x + (ax * c.fwd.x + ay * c.lat.x);
    c.vel.y = c.vel.y + (ax * c.fwd.y + ay * c.lat.y);
}

bool airborne(const ex::Car& c, const WheelLayout& l) {
    F sum = F();
    for (int i = 0; i < l.count && i < 4; ++i) sum = sum + c.w[size_t(i)].load;
    return sum == F();
}

void forward_step(ex::Car& c, const ChassisExtra& x, const TireExtra& t, const WheelLayout& l) {
    using gt3r::ee::cos;
    using gt3r::ee::sin;
    const int n = l.count < 4 ? l.count : 4;
    const F dt = c.dt;
    F fx = F();
    for (int i = 0; i < n; ++i) fx = fx + cos(c.w[size_t(i)].steer) * c.w[size_t(i)].fx_drive;
    c.drive_sum = (c.drive_sum + fx) + c.rolling;
    F extra = F();
    for (int i = 0; i < n; ++i)
        if (c.w[size_t(i)].flags == 0) extra = extra + c.p.wheel_mass[l.axle[size_t(i)] & 1u];
    const F inv = F::raw(0x3F800000) / (extra + x.mass);
    c.a_x = c.a_x + (c.drive_sum + c.drag) * inv;
    c.speed = c.speed + c.a_x * dt;
    if (c.speed == F()) return;
    F ft = F();
    for (int i = 0; i < n; ++i) {
        const ex::Wheel& w = c.w[size_t(i)];
        ft = ft + cos(w.steer) * w.fx_brake;
        if (t.wheel_xdd[size_t(i)] == 1) {
            ft = ft - sin(w.steer) * w.force_a;
        } else {
            ft = ft - sin(w.slip_dir) * w.force_a;
            ft = ft - cos(w.slip_dir) * w.force_b;
        }
    }
    const F at = ft * inv;
    const F u1 = c.speed, u2 = u1 + at * dt;
    if (F() < u1 * u2) {
        c.drive_sum = c.drive_sum + ft;
        c.a_x = c.a_x + at;
        c.speed = u2;
    } else {
        c.a_x = c.a_x + c.inv_dt * -u1;
        c.speed = F();
    }
}

} // namespace gt4r::sim::rules
