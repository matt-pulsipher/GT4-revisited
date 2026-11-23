// Gran Turismo 4's own routines of the race step: the ground contact, suspension, attitude and the body's motion (rules_chassis.h).

#include "gt4r/sim/rules_chassis.h"

#include "base/ee_math.h"

namespace gt4r::sim::rules {

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
