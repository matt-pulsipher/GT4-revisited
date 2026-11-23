// Gran Turismo 4's own routines of a car's start (rules_start.h).

#include "gt4r/sim/rules_start.h"

#include "base/ee_math.h"

namespace gt4r::sim::rules {

namespace {

constexpr F k0 = F::raw(0), k1 = F::raw(0x3F800000);
constexpr F kRpm = F::raw(0x3DD6774E);   // 2 pi / 60
constexpr F k60 = F::raw(0x42700000);
constexpr F kTwoPi = F::raw(0x40C90FDA);

} // namespace

int rolling_start_distance(int offset, int spacing, int count, int place) { return offset + (count - place - 1) * spacing; }

void moving_start(ex::Car& c, DriveExtra& x, F v, bool computer) {
    const DrivelineBlock& d = x.d;
    const EngineBlock& e = x.e;
    if (c.p.top_speed < v) v = c.p.top_speed;
    c.yaw_rate = k0;
    c.vel.x = c.fwd.x * v, c.vel.y = c.fwd.y * v, c.vel.z = c.fwd.z * v;
    // 0x363538: the speed records.
    c.speed_abs = v, c.speedo = v;
    if (c.max_speed < v) c.max_speed = v;
    c.gear = u8(start_gear(c, x, v));
    x.ratio_word = k1;
    F w = v * d.gear_speed[c.gear < 10 ? c.gear : 9];
    if (d.kind == 1) { // 0x3F1FB0: a stepless box of kind 1 held at its hold speed
        const F hold = gt3r::ee::int_to_f(d.down_rpm[0]) * kRpm;
        if (!(w < hold)) {
            x.ratio_word = hold / w;
            if (const F least = d.ratio[d.gears < 10 ? d.gears : 9]; x.ratio_word < least) x.ratio_word = least;
            w = x.ratio_word * w;
        }
    }
    c.omega = w;
    if (c.omega < gt3r::ee::int_to_f(e.idle) * kRpm) c.omega = gt3r::ee::int_to_f(e.idle) * kRpm;
    else if (const F limit = gt3r::ee::int_to_f(e.rev_limit) * kRpm; limit < c.omega) c.omega = limit;
    c.rpm = gt3r::s16(gt3r::ee::to_int((c.omega * k60) / kTwoPi));
    if (!(e.boost_max == k0)) {
        c.turbo_lag[0] = c.rpm;
        if (k0 < e.turbo_peak[1]) c.turbo_lag[1] = c.rpm;
        F sum = k0;
        for (int st = 0; st < 2; ++st)
            if (!(e.turbo_peak[size_t(st)] == k0)) sum = sum + turbo_boost(e, c.rpm, st);
        c.intake = sum + k1;
        if (e.boost_max + k1 < c.intake) c.intake = e.boost_max + k1;
        c.turbo_load = k1;
        c.turbo_pitch = gt3r::u16(c.rpm);
        c.blowoff = k0;
        c.turbo_load_prev = k1;
        c.blowoff_state = 0;
    }
    if (computer) c.throttle = k1, c.throttle_pedal = k1;
    c.clutch = 1;
    c.clutch_req = k1;
    c.speed = v;
    c.lat_speed = k0;
    for (ex::Wheel& wh : c.w) wh.rim_speed = v, wh.hub_vx = v;
    for (size_t a = 0; a < 2; ++a) c.axle_speed[a] = d.inv_tire_radius[d.unequal != 0 ? d.driven_axle & 1 : a] * v;
}

} // namespace gt4r::sim::rules
