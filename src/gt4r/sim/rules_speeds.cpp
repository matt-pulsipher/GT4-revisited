// Gran Turismo 4's phase 19 speeds and bookkeeping (rules_speeds.h).

#include "gt4r/sim/rules_speeds.h"

#include "base/ee_math.h"
#include "gt4r/sim/convert.h"
#include "gt4r/sim/program.h"

#include <cstring>

namespace gt4r::sim::rules {

namespace {
using gt3r::ee::int_to_f;
using gt3r::ee::to_int;
const F kZero = F(), kOne = F::raw(0x3F800000), kHalf = F::raw(0x3F000000);

// 0x343518: the odometer word (the condition block's +0x00) takes the whole metres of x (at most 1,600,000,000); the
// rest carries.
F odometer_add(u32& odometer, F x) {
    const s32 n = to_int(x);
    if (x <= kZero) return kZero;
    const u32 v = odometer + u32(n);
    if (0x5F5E0FFFu < v) {
        odometer = 0x5F5E1000u;
        return kZero;
    }
    odometer = v;
    return x - int_to_f(n);
}
} // namespace

SpeedTables speed_tables(const Program& program, u8 special_row) {
    SpeedTables t;
    const ConditionTable L = condition_table(program);
    for (int i = 0; i < 9; ++i) t.limits[i] = L.l[i];
    t.dirt_max = int_to_f(s16(program.h(0x620AF6))) * F::raw(0x447A0000);
    t.lean_on = program.w(0x620788), t.roll_on = program.w(0x620784);
    const u32 row = 0x6205C8 + 0x78 * (special_row < 3 ? special_row : 0);
    t.lean_row = {F::raw(program.w(row + 0x30)), F::raw(program.w(row + 0x34))};
    t.special_x10 = F::raw(program.w(row + 0x10));
    t.throttle_share = F::raw(program.w(0x62075C)), t.brake_share = F::raw(program.w(0x620760));
    return t;
}

void hub_velocities(ex::Car& c, const WheelLayout& l, const SpeedTables& t, F in_line_x480) {
    {
        const F v[3] = {c.vel.x, c.vel.y, c.vel.z};
        const F a[3] = {c.ground_fwd.x, c.ground_fwd.y, c.ground_fwd.z}, b[3] = {c.ground_lat.x, c.ground_lat.y, c.ground_lat.z};
        F u = kZero, s = kZero;
        for (int k = 0; k < 3; ++k) u = u + a[k] * v[k], s = s + b[k] * v[k];
        c.lat_speed = s, c.speed = u;
    }
    const F r = c.yaw_rate;
    for (u32 i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[i];
        const u8 g = l.geometry[i] & 1;
        const s8 fore = s8(l.fore_aft[i]);
        const F arm = g ? c.p.dist_rear : c.p.dist_front;
        F across = kZero;
        if (0 < fore) across = (-arm) * r;
        else if (fore < 0) across = arm * r;
        if (0 < fore && l.kind == 1 && t.lean_on != 0) { // (a two-wheeler's front hub: its lean)
            F radius = c.p.tire_radius[l.axle[i] & 1];
            const F a = t.lean_row[0];
            radius = radius * gt3r::ee::tan(a);
            const F off = t.lean_row[1] / gt3r::ee::cos(a);
            F lean = in_line_x480 * (radius - off);
            lean = lean * gt3r::ee::cos(c.roll);
            across = across + lean;
        }
        if (t.roll_on != 0) across = across - (c.roll_rate * c.p.ride_height[g]) * c.up.z;
        w.hub_vy = c.lat_speed + across;
        const u8 side = l.side[i];
        F along = kZero;
        if (side == 0) along = (-c.p.half_track[g]) * r;
        else if (side == 1) along = c.p.half_track[g] * r;
        w.hub_vx = c.speed + along;
    }
}

void free_wheels(ex::Car& c, const WheelLayout& l) {
    if (c.p.d.layout == 3 && !(c.handbrake == kZero) && F::raw(0xBE8E38E3) < c.speed && c.speed < F::raw(0x3E8E38E3)) return;
    u32 free[4] = {};
    for (u32 i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[i];
        if (w.flags != 0) continue;
        const F a = gt3r::ee::cos(w.steer) * w.hub_vx;
        w.rim_speed = a - gt3r::ee::sin(w.steer) * w.hub_vy;
        free[l.geometry[i] & 3] = 1;
    }
    for (u32 k = 0; k < l.groups && k < 2; ++k) {
        const u8 axle = l.group_axle[k] & 1;
        if (free[k] == 0 && c.p.d.diff_type[axle] != 0) continue;
        const u8 first = l.group_first[k] & 3;
        u8 other = l.partner[first];
        if (other == 0x7F) other = first;
        F m = c.w[first].rim_speed + c.w[other & 3].rim_speed;
        m = m * kHalf;
        const F inv = c.p.d.unequal != 0 ? c.p.inv_tire_radius[c.p.d.driven_axle & 1] : c.p.inv_tire_radius[axle];
        c.axle_speed[k] = inv * m;
    }
}

void speeds(ex::Car& c, SpeedExtra& x, const WheelLayout& l, const SpeedTables& t, F in_line_x480) {
    hub_velocities(c, l, t, in_line_x480);
    c.speed_abs = c.speed;
    // 0x35DD00: the odometer and the distances driven.
    if (!(c.speed_abs < kZero)) {
        const F d = c.speed_abs * c.dt;
        c.odo_carry = odometer_add(c.odometer, c.odo_carry + d);
        const F wrap = F::raw(0x4C989680); // (80,000 km)
        const F total = c.dist_total + d;
        c.dist_total = total;
        if (wrap <= total) c.dist_total = total - wrap;
        if (c.control == 1) {
            x.drive_distance = x.drive_distance + d;
            if (wrap <= x.drive_distance) x.drive_distance = x.drive_distance - wrap;
        }
    }
    if (c.speed_abs < kZero) c.speed_abs = -c.speed_abs;
    // The engine's condition: its wear (at most the limit +0x0C), its oil (a negative count that reaches 0 starts again at
    // 3 x the limit +0x14's size; at most +0x1C) and the word +0x10 (at most 0x620B18), each by the step's distance.
    if ((c.x408[1] & 0xFF) == 0) {
        const F d = c.speed_abs * c.dt;
        c.engine_wear = c.engine_wear + d;
        if (t.limits[3] < c.engine_wear) c.engine_wear = t.limits[3];
        const F o = c.oil;
        const F n = o + d;
        c.oil = n;
        if (o < kZero && !(n < kZero)) c.oil = -(t.limits[5] * F::raw(0x40400000));
        if (t.limits[7] < c.oil) c.oil = t.limits[7];
        F w10 = F::raw(c.x408[0]) + d;
        if (t.dirt_max < w10) w10 = t.dirt_max;
        c.x408[0] = w10.bits;
    }
    if (c.finished == 0 && c.max_speed < c.speed_abs) c.max_speed = c.speed_abs;
    // 0x35DC08: the speedometer and the lateral acceleration's follower.
    {
        const F s = c.speed_abs, v = x.speedo, step = c.dt * F::raw(0x42DE38E2);
        if (v < s) {
            x.speedo = v + step;
            if (s < x.speedo) x.speedo = s;
        } else {
            x.speedo = v - step;
            if (x.speedo < s) x.speedo = s;
        }
        const F sixtieth = F::raw(0x3C888888);
        F a = (c.a_y - x.g_follow) * F::raw(0x43480000);
        a = (a - x.g_follow_v * F::raw(0x41A00000)) * sixtieth;
        x.g_follow_v = x.g_follow_v + a;
        x.g_follow = x.g_follow + x.g_follow_v * sixtieth;
    }
    free_wheels(c, l);
}

} // namespace gt4r::sim::rules
