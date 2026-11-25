// Gran Turismo 4's hub offsets (rules_wheels.h).

#include "gt4r/sim/rules_wheels.h"

#include "base/ee_math.h"

namespace gt4r::sim::rules {

namespace {
F arm(const ex::Params& p, u8 g) { return g ? p.dist_rear : p.dist_front; }
} // namespace

void hub_offsets_xy(ex::Car& c, const WheelLayout& l) {
    for (u32 i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[i];
        const u8 g = l.geometry[i] & 1, side = l.side[i];
        const F ht = c.p.half_track[g];
        F x = c.lat.x * ht, y = c.lat.y * ht;
        if (side == 0) x = -x, y = -y;
        else if (side != 1) x = F(), y = F();
        w.hub.x = x, w.hub.y = y;
        const F d = arm(c.p, g);
        const F fx = c.fwd.x * d, fy = c.fwd.y * d;
        const s8 fore = s8(l.fore_aft[i]);
        if (0 < fore) w.hub.x = w.hub.x + fx, w.hub.y = w.hub.y + fy;
        else if (fore < 0) w.hub.x = w.hub.x - fx, w.hub.y = w.hub.y - fy;
    }
}

void hub_offsets_z(ex::Car& c, const WheelLayout& l) {
    for (u32 i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[i];
        const u8 g = l.geometry[i] & 1, side = l.side[i];
        F z = c.lat.z * c.p.half_track[g];
        if (side == 0) z = -z;
        else if (side != 1) z = F();
        w.hub.z = z;
        const F fz = c.fwd.z * arm(c.p, g);
        const s8 fore = s8(l.fore_aft[i]);
        if (0 < fore) w.hub.z = w.hub.z + fz;
        else if (fore < 0) w.hub.z = w.hub.z - fz;
    }
}

void speed_ramp(ex::Car& c, const WheelLayout& l) {
    const F gain = F::raw(0x3CA3D70B), one = F::raw(0x3F800000);
    for (u32 i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[i];
        const F v = gt3r::ee::sqrt(w.hub_vx * w.hub_vx + w.hub_vy * w.hub_vy) * gain;
        w.speed_ramp = one < v ? one : v;
    }
}

void wheel_spin(ex::Car& c, std::array<F, 4>& anim, const WheelLayout& l, F special_x10, u8 category, F x65c, F throttle_share, F brake_share) {
    const F two_pi = F::raw(0x40C90FDA), minus_two_pi = F::raw(0xC0C90FDA), zero = F(), half = F::raw(0x3F000000), one = F::raw(0x3F800000);
    const F dt = c.dt;
    if (two_pi <= c.heading) c.heading = c.heading - two_pi, c.prev.heading = c.prev.heading - two_pi;
    else if (c.heading <= minus_two_pi) c.heading = c.heading + two_pi, c.prev.heading = c.prev.heading + two_pi;
    const int n = l.count < 4 ? l.count : 4;
    for (int i = 0; i < n; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        F d = w.rim_speed * c.p.inv_tire_radius[l.axle[size_t(i)] & 1];
        d = d * dt;
        if (c.race_hold != 0) d = zero;
        F a = w.spin + d;
        if (two_pi <= a) {
            F p = c.prev.spin[size_t(i)];
            do a = a - two_pi, p = p - two_pi;
            while (two_pi <= a);
            c.prev.spin[size_t(i)] = p;
        }
        if (a <= minus_two_pi) {
            F p = c.prev.spin[size_t(i)];
            do a = a + two_pi, p = p + two_pi;
            while (a <= minus_two_pi);
            c.prev.spin[size_t(i)] = p;
        }
        w.spin = a;
    }
    const F k = c.p.x54;
    c.draw_pos_prev = c.draw_pos;
    c.draw_pos = {c.pos.x - k * c.fwd.x, c.pos.z - k * c.fwd.z, -(c.pos.y - k * c.fwd.y)};
    if (l.kind == 1 || category == 6) { // (0x35A080: a special car)
        const F h = c.p.tire_radius[1] - special_x10 - c.p.cog_height;
        c.draw_pos.x = c.draw_pos.x + h * c.up.x;
        c.draw_pos.y = c.draw_pos.y + h * c.up.z;
        c.draw_pos.z = c.draw_pos.z + h * c.up.y;
    }
    c.draw_ground_prev = c.draw_ground;
    c.prev.steer_view = c.steer_view;
    c.draw_ground = {c.ground_pos.x - k * c.ground_fwd.x, c.ground_pos.z - k * c.ground_fwd.z, -(c.ground_pos.y - k * c.ground_fwd.y)};
    F v;
    if (l.kind == 1) v = half - x65c * half;
    else v = half - c.steer_cmd / c.p.max_lock * half;
    if (v < zero) v = zero;
    else if (one < v) v = one;
    c.steer_view = v;
    anim[2] = c.throttle * throttle_share + anim[2] * (one - throttle_share);
    anim[3] = c.brake_pedal * brake_share + anim[3] * (one - brake_share);
}

} // namespace gt4r::sim::rules
