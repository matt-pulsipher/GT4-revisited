// Gran Turismo 4 Prologue's car at its start (rules_prologue.h): the pose's wheels (0x3C89D8's loop), the standing start
// 0x3C8E10 and the start on the move 0x3C9108, ported from its program (SCPS-15055 v1.01; docs/formats/prologue.md "What runs
// where"). Each runs over the car's wheel layout.

#include "gt4r/sim/rules_prologue.h"

#include "base/ee_math.h"
#include "gt4r/sim/rules_body.h"
#include "gt4r/sim/rules_drive.h"

namespace gt4r::sim::prologue {

namespace {

const F k0, k1 = F::raw(0x3F800000), kHalf = F::raw(0x3F000000);
const F kRpm = F::raw(0x3DD6774E);   // 2 pi / 60
const F k60 = F::raw(0x42700000), kTwoPi = F::raw(0x40C90FDA);

int wheels(const rules::WheelLayout& l) { return l.count < 4 ? int(l.count) : 4; }

// 0x3C8DF0: the speed records: the speed, the speedometer's (Prologue's car +0xC60, CarPart) and the greatest.
void speed_records(ex::Car& c, F v) {
    c.speed_abs = v;
    if (c.max_speed < v) c.max_speed = v;
}

} // namespace

F wheel_angle_by(const ex::Car& c, const CarPart& x, int w, F delta) {
    const rules::WheelLayout& l = x.layout;
    const size_t i = size_t(w);
    F a;
    const u8 side = l.side[i];
    if (w < l.count && gt3r::s8(l.fore_aft[i]) > 0) { // (a wheel ahead of the center of gravity: the command by its side's ratio)
        a = delta;
        if (side < 2) a = a * c.p.steer_ratio[k0 <= delta ? side : 1u - side];
    }
    const F toe = c.p.toe[w < l.count ? l.axle[i] & 1u : 0];
    if (side == 0) a = a - toe;
    else if (side == 1) a = a + toe;
    return a;
}

void pose_wheels(ex::Car& c, CarPart& x, const Tables& t) {
    const rules::WheelLayout& l = x.layout;
    for (int w = 0; w < wheels(l); ++w) {
        ex::Wheel& wh = c.w[size_t(w)];
        const size_t a = l.axle[size_t(w)] & 1u;
        wh.smoke_count = 0;
        wh.load = wh.peak_force = x.axle_load[a] * kHalf + c.p.susp[a].preload; // (half the axle's load whatever its wheels)
        wh.susp = c.p.susp[a].x_static;
        wh.steer = wheel_angle_by(c, x, w, k0); // (0x3B5898 at no command)
        x.tires.steer_offset[size_t(w)] = k0, x.offset_rate[size_t(w)] = k0;
        if (t.steer.mode == 2) x.offset_rate[size_t(w)] = k0, x.tires.steer_offset[size_t(w)] = wh.steer; // (0x3B5870: the steering mode)
        wh.camber = rules::wheel_camber(c, l, w, k0, k0);      // (0x375D78 = Gran Turismo 4's 0x36A578 in its layout)
        wh.half_track = rules::wheel_half_track(c, l, w, k0);  // (0x375E90 = 0x36A690)
    }
}

void standing_start(ex::Car& c, CarPart& x) {
    const rules::WheelLayout& l = x.layout;
    for (int w = 0; w < wheels(l); ++w) {
        ex::Wheel& wh = c.w[size_t(w)];
        wh.slip_dir = k0, wh.slip_fade = k0, wh.force_b = k0, wh.force_a = k0, wh.fx_drive = k0, wh.fx_brake = k0;
        wh.hub_vx = k0, wh.hub_vy = k0, wh.rim_speed = k0, wh.flags = 0, wh.slip_gain = k1;
        x.tires.camber_force[size_t(w)] = k0; // (car +0x4B0 + 4 w)
    }
    for (size_t a = 0; a < 2; ++a) {
        c.diff_lock[a] = u8(c.p.d.diff_type[a] - 3) < 2 ? 1 : 0;
        c.axle_speed[a] = k0;
    }
    c.centre_locked = u8(c.p.d.layout - 4) < 3 || c.p.d.layout == 9 ? 1 : 0;
    c.clutch_torque = k0, c.time_scale = k1;
    c.inv_mass_eff = k0; // (F +0x394)
    c.steer_cmd = k0;
    c.lock_lo = -c.p.max_lock, c.yaw_rate = k0, c.lock_hi = c.p.max_lock;
    c.steer_rate = k0;
    c.x420 = k0; // (the mean slip angle F +0x424: 0, where Gran Turismo 3's start has 1)
    c.slip_target = k0, c.slip_filter = k0;
    c.vel = {};
    c.steer_pct = 0;
    c.throttle_pedal = k0, c.throttle = k0, c.brake_pedal = k0;
    for (int w = 0; w < wheels(l); ++w) c.w[size_t(w)].brake = k0;
    c.full_throttle = 0, c.blip = 0;
    c.intake = k0, c.turbo_load_prev = k0, c.turbo_load = k0, c.turbo_lag = {}, c.turbo_pitch = 0, c.blowoff = k0, c.blowoff_state = 0;
    c.torque_wot = k0, c.torque_friction = k0;
    c.off_count = 0, c.course_out = 0; // (0x3C8DE0)
    c.rev_limiter = 0;
    c.max_speed = k0;
    speed_records(c, k0);
    c.gear = 1; // (0x3CD4A8)
    c.rpm = 0, c.ascc_status = 0, c.tcs_status = 0, c.hit_rumble = k0;
    // (0x3BC7D0 / 0x3BC7B0: the pad's vibration block in the conductor, +0x10DAC8 + 16 k: the session's feel)
    c.wheel_pos = 0, c.wheel_pos_prev = 0;
    c.x4e5 = 0x7F, c.x4e4 = 0x7F, c.turbo_spool = 0;
    for (int w = 0; w < wheels(l); ++w) { // 0x3C8940: the wheels' effects
        ex::Wheel& wh = c.w[size_t(w)];
        wh.smoke_emit = k0, wh.x29 = 0xFF, wh.slip_intensity = k0, wh.skid_pitch = k1, wh.spin = k0, wh.speed_ramp = k0, wh.slip_ratio = k0;
        wh.circle = k0, wh.bump = k0, wh.damper_v = k0, wh.spring_f = k0, wh.damper_f = k0, wh.flap = k0, wh.flap_v = k0;
    }
    c.speed = k0, c.roll_rate = k0, c.lat_speed = k0, c.clutch = 0, c.clutch_req = k0, c.omega = k0, c.pitch_rate = k0;
    c.bump_mask = 0, c.shift_lockout = 0;
    if (gt3r::s8(c.control) == 1 || c.driver_kind != 0 || c.pedal_style == 0 || c.pedal_style == 2) c.shift_lockout = 12;
    c.antihunt = 0, c.kickdown = 0, c.kickdown_timer = 0, c.kickdown_count = 0;
    c.downforce = {k0, k0}, c.a_x = k0, c.a_y = k0, c.heave_acc = k0, c.lat_g = k0, c.yaw_accel = k0, c.drive_sum = k0;
    c.x4d0 = k0, c.x4d4 = k0;
    // (0x3C26B8: car +0x808 .. +0x81B cleared, 0x3C5FC0: F +0x51E / +0x51F; neither read by the step)
}

void moving_start(ex::Car& c, const CarPart& x, F v) {
    const ex::Driveline& d = c.p.d;
    c.yaw_rate = k0;
    c.vel.x = c.fwd.x * v, c.vel.y = c.fwd.y * v, c.vel.z = c.fwd.z * v;
    speed_records(c, v);
    c.gear = u8(prologue::gear_for_speed(c, v)); // (0x3CD4C0, 0x3CD4A8)
    c.later.ratio = k1;                // (F +0x3A4)
    F w = v * c.p.d.gear_speed[c.gear & 7];
    if (d.kind == 1) { // 0x3DB670: a stepless box of kind 1 held at its hold speed (its least ratio the top gear's)
        const F hold = gt3r::ee::int_to_f(d.down_rpm[0]) * kRpm;
        if (!(w < hold)) {
            c.later.ratio = hold / w;
            if (const F least = d.ratio[d.gears & 7]; c.later.ratio < least) c.later.ratio = least;
            w = c.later.ratio * w;
        }
    }
    c.omega = w;
    if (const F idle = gt3r::ee::int_to_f(c.p.e.idle) * kRpm; c.omega < idle) c.omega = idle;
    else if (const F limit = gt3r::ee::int_to_f(c.p.e.rev_limit) * kRpm; limit < c.omega) c.omega = limit;
    c.rpm = gt3r::s16(gt3r::ee::to_int((c.omega * k60) / kTwoPi));
    if (!(c.p.e.boost_max == k0)) {
        c.turbo_lag[0] = c.rpm;
        if (k0 < c.p.e.turbo_peak[1]) c.turbo_lag[1] = c.rpm;
        c.intake = rules::boost_capped(x.drive.e, c.rpm, c.rpm); // (0x3B7DC8 = Gran Turismo 4's 0x34ECF0 in its layout)
        c.turbo_load = k1;
        c.turbo_pitch = gt3r::u16(c.rpm);
        c.blowoff = k0;
        c.turbo_load_prev = k1;
        c.blowoff_state = 0;
    }
    if (c.ai.mode == 0) c.throttle = k1, c.throttle_pedal = k1; // (0x424B68: the driver on a straight)
    c.clutch = 1;
    c.clutch_req = k1;
    c.speed = v;
    c.lat_speed = k0;
    for (int k = 0; k < wheels(x.layout); ++k) c.w[size_t(k)].rim_speed = v, c.w[size_t(k)].hub_vx = v;
    for (size_t a = 0; a < 2; ++a) c.axle_speed[a] = c.p.inv_tire_radius[c.p.d.unequal != 0 ? c.p.d.driven_axle & 1 : a] * v;
}

} // namespace gt4r::sim::prologue
