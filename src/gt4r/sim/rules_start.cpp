// Gran Turismo 4's own routines of a car's start (rules_start.h).

#include "gt4r/sim/rules_start.h"

#include "base/ee_math.h"
#include "sim/exact/start.h"

namespace gt4r::sim::rules {

namespace {

constexpr F k0 = F::raw(0), k1 = F::raw(0x3F800000);
constexpr F kRpm = F::raw(0x3DD6774E);   // 2 pi / 60
constexpr F k60 = F::raw(0x42700000);
constexpr F kTwoPi = F::raw(0x40C90FDA);

} // namespace

int rolling_start_distance(int offset, int spacing, int count, int place) { return offset + (count - place - 1) * spacing; }

int start_gear(const ex::Car& c, F v) {
    if (v < k0) return 0;
    const ex::Driveline& d = c.p.d;
    if (d.kind == 3 || d.kind == 1) return 1; // (0x368398, 0x3F1F78)
    const int n = d.gears;
    for (int g = 1; g < n; ++g) {
        // 0x36BEE0: the up-shift speed (D +0x2E; a computer's own when its driver word F +0x464 says so: not for a player)
        const s32 rpm = s32(d.up_rpm) - (g == 1 ? 2000 : 500);
        if (v < (gt3r::ee::int_to_f(rpm) * kRpm) / d.gear_speed[size_t(g)]) return g;
    }
    return n;
}

void moving_start(ex::Car& c, F v, bool computer) {
    if (c.p.top_speed < v) v = c.p.top_speed;
    c.yaw_rate = k0;
    c.vel.x = c.fwd.x * v, c.vel.y = c.fwd.y * v, c.vel.z = c.fwd.z * v;
    // 0x363538: the speed records.
    c.speed_abs = v, c.speedo = v;
    if (c.max_speed < v) c.max_speed = v;
    ex::set_gear(c, u8(start_gear(c, v)));
    c.later.ratio = k1;
    F w = v * c.p.d.gear_speed[c.gear];
    ex::stepless_ratio(c.p.d, w, c.later.ratio, true); // (0x3F1FB0: kind 1 only)
    c.omega = w;
    if (c.omega < gt3r::ee::int_to_f(c.p.e.idle) * kRpm) c.omega = gt3r::ee::int_to_f(c.p.e.idle) * kRpm;
    else if (const F limit = gt3r::ee::int_to_f(c.p.e.rev_limit) * kRpm; limit < c.omega) c.omega = limit;
    c.rpm = gt3r::s16(gt3r::ee::to_int((c.omega * k60) / kTwoPi));
    if (!(c.p.e.boost_max == k0)) {
        c.turbo_lag[0] = c.rpm;
        if (k0 < c.p.e.turbo_peak[1]) c.turbo_lag[1] = c.rpm;
        c.intake = ex::boost(c.p.e, c.rpm, c.rpm);
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
    for (size_t a = 0; a < 2; ++a) c.axle_speed[a] = c.p.inv_tire_radius[c.p.d.unequal != 0 ? c.p.d.driven_axle : a] * v;
}

} // namespace gt4r::sim::rules
