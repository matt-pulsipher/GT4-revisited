#include "gt4r/race/driver_gaze.h"

namespace gt4r::race::posture {

namespace ee = gt3r::ee;

V3 ahead_of_seat(const Cockpit& c, F yaw) {
    const V3 p{c.seat.x + f(0), c.seat.y + f(0), c.seat.z + f(-6.25f)};
    // 0x487C68 of (0, yaw): the half angles' sines and cosines.
    F s0, c0, s1, c1;
    ee::sincos(f(0) * f(0.5f), s0, c0);
    ee::sincos(yaw * f(0.5f), s1, c1);
    const Quat q{s0 * c1, c0 * s1, -(s0 * s1), c0 * c1};
    return rotate(q, p);
}

std::optional<V3> road_target(const Cockpit& c, V3 aim) {
    // 0x40DA48: across the ground, within 60 degrees of straight ahead (-z).
    const F len2 = (aim.x * aim.x) + (aim.z * aim.z);
    if (!(f(0) < len2)) return std::nullopt;
    const F r = ee::rsqrt(f(1), len2), nx = aim.x * r, nz = aim.z * r;
    V3 out = aim;
    if (!(nz < f(-0.5f))) {
        if (!(nz < f(0.707106769f))) return std::nullopt; // behind
        const F edge_x = f(0) < nx ? f(0.866025388f) : f(-0.866025388f);
        F angle = f(1.57079625f) - asin((edge_x * nx) + (nz * f(-0.5f)));
        if (!(f(0) < nx)) angle = -angle;
        F s, cs;
        ee::sincos(angle, s, cs); // 0x4256F0: the aim turned about y onto the edge
        out.y = aim.y;
        out.z = (-aim.x * s) + (aim.z * cs);
        out.x = (aim.x * cs) + (aim.z * s);
    }
    // Its height to the eye's: 15 degrees either way left alone, 30 beyond at most.
    const F across = ee::sqrt((out.x * out.x) + (out.z * out.z));
    const F eye = c.seat.y + f(0.799999952f);
    F pitch = posture::atan2(out.y - eye, across);
    const F free = f(0.261799335f);
    if (free < pitch) pitch = pitch - free;
    else if (pitch < -free) pitch = pitch + free;
    else pitch = f(0);
    F up;
    if (f(0.523598671f) < pitch) up = f(0.577349961f) * across;
    else if (pitch < f(-0.523598671f)) up = f(-0.577349961f) * across;
    else up = ee::tan(pitch) * across;
    out.y = up + eye;
    return out;
}

void DriverGaze::step(const Cockpit& c, const Inputs& in, DriverRecord& r) {
    const int was = state;
    // The target (state 1: 0x40E710).
    if (state == 0 || state == 1 || state == 5 || state == 7) {
        bool straight = state == 0;
        bool have_aim = false;
        if (!straight) {
            if (abs(r.at(0x34)) < F::raw(0x3D32B8C2)) held = held + in.dt; // the wheel held straight (2.5 degrees)
            else held = f(0);
            if (!in.aim) {
                straight = true;
            } else {
                have_aim = true;
                const V3 a = *in.aim;
                const F angle = posture::atan2(a.x, -a.z), lim = F::raw(0x3DB2B8C2); // (the tuning object's +0x6C, +0x70, +0x74)
                if (abs(angle) < lim) held = f(0);
                else if ((lim <= aim_angle && angle <= -lim) || (aim_angle <= -lim && lim <= angle)) held = f(0);
                else if (f(2) <= held) straight = true; // the aim to a side while the wheel stands straight for 2 s
                aim_angle = angle;
            }
        }
        std::optional<V3> road;
        if (!straight && have_aim && f(0) < dot(*in.aim, *in.aim)) road = road_target(c, *in.aim);
        if (road) {
            target = *road;
        } else {
            // Straight ahead, turned by the front wheels (30 degrees at most; a tenth of that in state 0).
            const F most = F::raw(0x3F060A90);
            F yaw = min(max(in.front_wheels, -most), most);
            if (state == 0) yaw = yaw * f(0.099999994f);
            target = ahead_of_seat(c, yaw);
        }
    }
    // The place follows its target (0x40F07C): springs per axis toward a point 2.5 m from the eye on the way to the
    // target, at most 60 degrees from straight ahead.
    if (state != was) rate = V3{f(0), f(0), f(0)};
    const F k = in.co_driver ? f(25) : f(50), damp = in.co_driver ? f(10) : f(14.1399994f);
    const V3 eye{c.seat.x, c.seat.y + f(0.799999952f), c.seat.z};
    V3 d{target.x - eye.x, target.y - eye.y, target.z - eye.z};
    const F len2 = ((d.x * d.x) + (d.y * d.y)) + (d.z * d.z);
    if (f(0.000999999931f) < len2) {
        const F n = ee::rsqrt(f(1), len2);
        d.z = d.z * n, d.x = d.x * n, d.y = d.y * n;
        const F off = f(1.57079625f) - asin(-d.z);
        if (f(1.04719734f) < off) {
            if (off < f(3.10668564f)) {
                const F t = f(1.57079625f) - posture::atan2(d.y, d.x);
                const F s = ee::sin(t), cs = ee::cos(t);
                d.z = f(-0.5f);
                d.x = (cs * f(0)) + (s * f(0.866025388f));
                d.y = (-s * f(0)) + (cs * f(0.866025388f));
            } else {
                d = V3{f(0), f(0.866025388f), f(-0.5f)};
            }
        }
        d = V3{d.x * f(2.5f), d.y * f(2.5f), d.z * f(2.5f)};
    } else {
        d = V3{f(0), f(0), f(-2.5f)};
    }
    const V3 want{d.x + eye.x, d.y + eye.y, d.z + eye.z};
    F* pos[3] = {&r.at(0x58), &r.at(0x5C), &r.at(0x60)};
    F* spd[3] = {&rate.x, &rate.y, &rate.z};
    const F to[3] = {want.x, want.y, want.z};
    if (in.first) {
        for (int i = 0; i < 3; ++i) *spd[i] = f(0), *pos[i] = to[i];
        return;
    }
    for (int i = 0; i < 3; ++i) {
        F a = f(0) + ((to[i] - *pos[i]) * k);
        a = a - (*spd[i] * damp);
        *spd[i] = *spd[i] + (a * in.dt);
        if (f(56.25f) < (*spd[i] * *spd[i])) *spd[i] = f(0) < *spd[i] ? f(7.5f) : f(-7.5f);
        *pos[i] = *pos[i] + (*spd[i] * in.dt);
    }
}

} // namespace gt4r::race::posture
