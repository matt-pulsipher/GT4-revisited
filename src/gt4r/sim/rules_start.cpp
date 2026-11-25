// Gran Turismo 4's own routines of a car's start (rules_start.h).

#include "gt4r/sim/rules_start.h"

#include "base/ee_math.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/record.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

#include <cstring>

namespace gt4r::sim::rules {

namespace {

constexpr F k0 = F::raw(0), k1 = F::raw(0x3F800000);
constexpr F kRpm = F::raw(0x3DD6774E);   // 2 pi / 60
constexpr F k60 = F::raw(0x42700000);
constexpr F kTwoPi = F::raw(0x40C90FDA);
constexpr F kPerKmh = F::raw(0x3E8E38E3); // 1 / 3.6
constexpr F kHoldHeight = F::raw(0x3EEB851D); // 0.46 m
constexpr F kHoldTurn = F::raw(0x3F060A90);   // the heading's turn a second while held (rad)
constexpr F kSpeedPartFull = F::raw(0x3E1D89D8);

int wheels(const WheelLayout& l) { return l.count < 4 ? l.count : 4; }
size_t axle_of(const WheelLayout& l, int w) { return w < l.count ? l.axle[size_t(w)] : 0; }

// 0x363538: the speed records at v: the current speed and the speedometer v, the highest speed at least v.
void speed_records(ex::Car& c, F v) {
    c.speed_abs = v;
    c.speedo = v;
    if (c.max_speed < v) c.max_speed = v;
}

// 0x363018: each of the layout's wheels' effects, spin, slip ratio, friction circle, bump, damper and spring, flap,
// curve A at the slip angle and compliance cleared (the effects' level 0xFF, their pitch 1).
void wheel_effects(ex::Car& c, Extra& x, const WheelLayout& l) {
    for (int w = 0; w < wheels(l); ++w) {
        ex::Wheel& wh = c.w[size_t(w)];
        // (wheel + 0xBC, + 0xB2, + 0xB4, + 0xB8: the engine's members of the tire effects, unplaced)
        wh.smoke_emit = k0, wh.x29 = 0xFF, wh.slip_intensity = k0, wh.skid_pitch = k1;
        wh.spin = k0, wh.speed_ramp = k0, wh.slip_ratio = k0, wh.circle = k0, wh.bump = k0;
        wh.damper_v = k0, wh.spring_f = k0, wh.damper_f = k0, wh.flap = k0, wh.flap_v = k0;
        x.tires.curve_a[size_t(w)] = k0, x.steer.compliance[size_t(w)] = k0, x.steer.compliance_rate[size_t(w)] = k0;
        x.chassis.wheel_lat[size_t(w)] = k0;
    }
}

} // namespace

StartTables start_tables(const Program& program) {
    StartTables t;
    if (!program.known()) return t;
    t.fuel_less = program.b(0x620E24);
    t.steer_mode = program.w(kSteerMode);
    return t;
}

int rolling_start_distance(int offset, int spacing, int count, int place) { return offset + (count - place - 1) * spacing; }

void moving_start(ex::Car& c, DriveExtra& x, F v, bool computer) {
    const DrivelineBlock& d = x.d;
    const EngineBlock& e = x.e;
    if (c.p.top_speed < v) v = c.p.top_speed;
    c.yaw_rate = k0;
    c.vel.x = c.fwd.x * v, c.vel.y = c.fwd.y * v, c.vel.z = c.fwd.z * v;
    speed_records(c, v);
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

// ---- the car's set-up ---------------------------------------------------------------------------------------------

void pose(ex::Car& c, Extra& x, const WheelLayout& l, const StartTables& t, const PoseHooks& h, s32 hint, F px, F py, F pz, F heading) {
    // The road under the place.
    ex::Ground probe{};
    probe.x = px, probe.h = pz, probe.z = -py, probe.seg = hint;
    h.query(probe);
    const F ground = probe.height;
    c.pos.x = px, c.pos.y = py;
    c.road_seg = probe.seg;
    c.pitch = k0, c.roll = k0, c.heading = heading;
    c.pos.z = ground + c.p.cog_height;
    rules::body_axes(c);
    ex::hub_offsets(c);
    // Each wheel's ground record at its hub.
    u8 soft = 0, pit = 0;
    for (int w = 0; w < wheels(l); ++w) {
        ex::Wheel& wh = c.w[size_t(w)];
        ex::Ground& g = c.g[size_t(w)];
        const F hx = c.pos.x + wh.hub.x, hy = c.pos.y + wh.hub.y, hz = c.pos.z + wh.hub.z;
        g.x = hx, g.h = hz, g.z = -hy;
        g.seg = c.road_seg;
        const u8 surface = h.query(g);
        if (g.miss == 0) {
            wh.ground = g.height;
            wh.surface = surface;
            wh.x25 = g.x32, wh.surface_flags = g.flags;
            pit = u8(pit + (g.flags & 1));
            if (h.soft && h.soft(g)) ++soft;
        } else {
            wh.surface = 0, wh.x25 = 0, wh.surface_flags = 0;
            wh.ground = ground + wh.hub.z;
        }
    }
    c.x535 = u8(c.x535 & 0xEF);
    if (pit != 0 && (!h.pit_lane || h.pit_lane())) c.x535 = u8(c.x535 | 0x10);
    x.phases.soft_wheels = soft, x.phases.pit_wheels = pit;
    // Each wheel at rest under its axle's share of the load.
    for (int w = 0; w < wheels(l); ++w) {
        ex::Wheel& wh = c.w[size_t(w)];
        const size_t a = axle_of(l, w);
        const F load = x.steer.axle_load[a] / gt3r::ee::int_to_f(s32(l.axle_wheels[a])) + c.p.susp[a].preload;
        wh.peak_force = load, wh.load = load;
        wh.susp = c.p.susp[a].x_static;
        wh.steer = wheel_angle(c, x.steer, l, w, k0);
        x.tires.steer_offset[size_t(w)] = k0, x.steer.offset_rate[size_t(w)] = k0;
        if (t.steer_mode == 2) x.steer.offset_rate[size_t(w)] = k0, x.tires.steer_offset[size_t(w)] = wh.steer;
        wh.camber = wheel_camber(c, l, w, k0, k0);
        wh.half_track = wheel_half_track(c, l, w, k0);
    }
    ground_frame(c, l);
    c.pitch = c.ground_pitch, c.roll = c.ground_roll;
    c.pitch = c.pitch + ex::atan2(c.behaviour, c.p.ride_height[0] - c.p.ride_height[1], c.p.wheelbase);
    rules::body_axes(c);
    c.pos.z = c.ground_pos.z + c.up.z * c.p.cog_height;
    ex::hub_offsets(c);
    c.f3e4 = h.distance ? h.distance(c) : k0;
    x.input.line_triangle = u32(c.road_seg);
    x.input.line_height = c.ground_pos.z;
    x.input.line_point[0] = c.ground_pos.x, x.input.line_point[1] = c.ground_pos.y;
    x.near.course_rate = k0;
}

void clear_view_lags(ex::Car& c) { c.lag_heading_diff = k0, c.lag_pitch_diff = k0; }

void standing_start(ex::Car& c, Extra& x, const WheelLayout& l) {
    for (int w = 0; w < wheels(l); ++w) {
        ex::Wheel& wh = c.w[size_t(w)];
        wh.slip_dir = k0, wh.slip_fade = k0, wh.force_b = k0, wh.force_a = k0, wh.fx_drive = k0, wh.fx_brake = k0;
        wh.hub_vx = k0, wh.hub_vy = k0, wh.rim_speed = k0, wh.flags = 0, wh.slip_gain = k1;
        x.tires.camber_force[size_t(w)] = k0;
    }
    for (size_t a = 0; a < 2; ++a) {
        c.diff_lock[a] = 0;
        if (u8(c.p.d.diff_type[a] - 3) < 2) c.diff_lock[a] = 1;
        c.axle_speed[a] = k0;
    }
    c.centre_locked = 0;
    if (u8(c.p.d.layout - 4) < 3 || c.p.d.layout == 9) c.centre_locked = 1;
    c.clutch_torque = k0, c.time_scale = k1;
    x.body.in_line_x480 = k0;
    c.steer_cmd = k0;
    c.lock_lo = -c.p.max_lock, c.lock_hi = c.p.max_lock;
    c.yaw_rate = k0;
    x.input.steer_rate = k0, x.input.active_last = k0;
    x.tires.mean_slip = k0;
    c.slip_target = k0, c.slip_filter = k0;
    c.vel = {};
    x.input.steer_view = k0;
    c.steer_pct = 0;
    c.throttle_pedal = k0, c.throttle = k0, c.brake_pedal = k0;
    for (int w = 0; w < wheels(l); ++w) c.w[size_t(w)].brake = k0;
    c.intake = k0, c.full_throttle = 0;
    c.turbo_lag[1] = 0, c.turbo_load_prev = k0, c.blowoff = k0, c.torque_wot = k0, c.torque_friction = k0, c.turbo_load = k0;
    c.turbo_lag[0] = 0, c.turbo_pitch = 0, c.blowoff_state = 0, c.rev_limiter = 0;
    c.max_speed = k0;
    speed_records(c, k0);
    c.gear = 1; // (0x36BEB8 with 1: first gear; a car of category 4 keeps its gear only when asked for none)
    c.rpm = 0;
    x.aids.ascc_status = 0, x.aids.tcs_status = 0;
    c.hit_rumble = k0; // (F + 0x610, unplaced)
    x.input.steer_in = 0, x.input.steer_in_last = 0, x.input.steer_seen = 0, x.input.steer_seen_last = 0;
    x.start.release_count = 30; // 0x353ED8
    c.x4e5 = 0x7F, c.x4e4 = 0x7F; // (F + 0x605 / + 0x604: the engine sound's levels, unplaced)
    c.turbo_spool = 0;
    wheel_effects(c, x, l);
    c.speed = k0, c.roll_rate = k0, c.lat_speed = k0, c.clutch = 0, c.clutch_req = k0, c.omega = k0, c.pitch_rate = k0;
    c.bump_mask = 0, c.shift_lockout = 0;
    if (s8(c.control) == 1 || c.driver_kind != 0 || c.pedal_style == 0 || c.pedal_style == 2) c.shift_lockout = 12;
    x.input.lean_rate = k0;
    c.antihunt = 0, c.kickdown = 0, c.kickdown_timer = 0, c.kickdown_count = 0;
    c.downforce = {k0, k0}, c.a_x = k0, c.a_y = k0, c.heave_acc = k0, c.lat_g = k0, c.yaw_accel = k0, c.drive_sum = k0;
    x.aids.road_load = k0;
    x.aids.understeer = k0, x.aids.oversteer = k0, x.aids.understeer_v = k0, x.aids.oversteer_v = k0;
    x.body.in_line_x65c = k0;
    x.drive.marks = {}; // 0x35A638 (and car + 0x778 .., unplaced)
}

void start_fuel(ex::Car& c, Extra& x, u8 fuel_rule, const StartTables& t) {
    (void)c;
    StartExtra& s = x.start;
    s.fuel_on = 1, s.fuel_least = k0, s.x4be = 0, s.x4c4 = k0, s.x4c8 = k0;
    const u8 tank = fuel_rule != 0 ? s.tank : 0;
    const F full = gt3r::ee::int_to_f(s32(tank));
    x.aids.fuel = full, x.aids.fuel_capacity = full;
    if (tank != 0) {
        const F f = full - gt3r::ee::int_to_f(s32(t.fuel_less));
        x.aids.fuel = f;
        if (f < s.fuel_least) x.aids.fuel = s.fuel_least;
    }
    masses(c, x);
}

namespace {

// 0x3F27E8: the speed-switched part's state at the start: cleared; kind 2 at its share of the speed; kind 1 out (1) at
// or above its upper speed; kind 6 out a little (0.1538) there.
void speed_part_start(ex::Car& c, Extra& x) {
    NearExtra& n = x.near;
    x.chassis.speed_part_state = k0, n.speed_part_state2 = k0, n.speed_state = 0, n.speed_state2 = 0;
    const u8 kind = x.body.speed_part_kind;
    if (kind == 2) x.chassis.speed_part_state = speed_part_share(n, c.speed);
    else if (kind == 1) {
        if (n.speed_hi <= c.speed) n.speed_state = kind, x.chassis.speed_part_state = k1;
    } else if (kind == 6) {
        if (n.speed_hi <= c.speed) n.speed_state = 1, x.chassis.speed_part_state = kSpeedPartFull;
    }
}

} // namespace

void car_setup(ex::Car& c, Extra& x, const WheelLayout& l, const StartTables& t, const WearRules& wear, const SetupArgs& a, const SetupHooks& h) {
    // Before the converter: the index, the driver's kind for the steering wheel (0x3487F0), the control source (0x362E00).
    c.index = u8(a.index), c.ghost = a.not_racing;
    x.input.wheel_type = s8(a.entry_driver == 1 ? 2 : a.entry_driver == 2 ? 3 : 1);
    c.control = a.control, c.driver_kind = 0;
    // 0x362E10: the pedal style.
    if (a.control == 0 || a.control == 1 || a.control == 4) c.pedal_style = a.pedal_style;
    else if (a.control == 2) c.pedal_style = 0;
    c.x535 = 0, x.start.x5c2 = 0, x.phases.pit_tires = {0xFF, 0xFF}, x.input.pit_request = 0;
    x.phases.pit_flags = 0, x.phases.pit_wait = 0, x.phases.soft_wheels = 0, x.phases.pit_wheels = 0;
    // The place: a car on the grid with its nose there.
    F px = a.x, py = a.y;
    const int kind = a.start_kind;
    if (kind == 0 || kind == 8) {
        const F front = c.p.body_front;
        px = px + front * gt3r::ee::sin(a.heading);
        py = py - front * gt3r::ee::cos(a.heading);
    }
    x.start.start_mark = a.start_mark != 0 ? (kind == 2 ? 1 : a.start_mark) : 0;
    pose(c, x, l, t, h.pose, a.hint, px, py, a.z, a.heading);
    clear_view_lags(c);
    standing_start(c, x, l);
    for (int w = 0; w < wheels(l); ++w) fresh_tires(c, x, w, wear); // 0x362FA8
    x.start.x4b6 = 0, x.phases.failed = 0;                            // 0x363528
    x.aids.start_count = 0;                                           // 0x35FC48
    x.phases.penalty_marked = 0, x.aids.pit_limit = 0, x.phases.incidents = 0, x.phases.incident_flags = 0, x.phases.hit_steps = 0;
    start_fuel(c, x, a.fuel_rule, t);
    x.input.nitrous = k0 < x.input.nitrous_gain ? x.start.nitrous_full : k0;
    x.input.nitrous_state = 0, x.start.x4dc = 0;
    x.start.x6c4 = 0;                                                  // 0x3F3360
    if (x.aids.mark_mode == 3) x.phases.special.fill(0);               // 0x355840: a category 3 car's words cleared
    if (c.p.d.layout == 7) {                                           // 0x355DC8: layout 7's motors
        const F v = k1 * t.motor_scale;
        std::memcpy(x.phases.special.data(), &v.bits, 4);
        x.phases.special[5] = 0, x.phases.special[4] = 0;
    }
    if (x.aids.mark_mode == 1) x.phases.special[0] = 0;                // 0x35D108(car, 0)
    rules::step_length(c);
    x.start.lap_started = 0, x.start.x4b4 = 0;
    // (the laps and sectors from the place: the race's)
    x.start.x614 = 0;
    c.best_lap = 0x157529FF; // 0x35E3B8
    c.race_hold = 0, c.hold = 0;
    x.phases.finished = 0, x.start.x632 = 0, x.phases.wrong_way = 0, x.phases.message = 0;
    x.start.line = a.line;
    c.rank = gt3r::s8(a.index + 1);
    // A pit start: held on the stop line for two seconds.
    if (kind == 6 || kind == 7) {
        if (h.to_stop_line && h.to_stop_line() && s8(c.control) != 4) c.driver_kind = 3;
        c.hold = 120;
    }
    x.start.rolling_steps = 0;
    // A rolling start: the computer drives the car on line 6 until it is let go.
    if ((kind >= 2 && kind <= 4) || kind == 8) {
        if (rolling_hold(c, x) && h.to_line6) h.to_line6();
        c.shift_lockout = 12;
        if (kind == 3) x.start.rolling_steps = 180;
        else if (kind == 8) x.start.rolling_steps = 300;
    }
    const F v = kind == 4 ? (h.driver_speed ? h.driver_speed() : k0) : a.speed * kPerKmh;
    if (!(v == k0)) moving_start(c, x.drive, v, h.open_throttle ? h.open_throttle() : true);
    // 0x3F9CF0, 0x3F27E8, 0x35C7E0, 0x350EB0, 0x366388.
    x.aids.tcs_throttle_v = k0, x.aids.tcs_throttle = c.throttle;
    speed_part_start(c, x);
    if (x.aids.mark_mode == 1) { // 0x35C7E0: a car with moods
        x.phases.special[6] = 0, x.phases.special[2] = 0x3C;
        x.phases.special[1] = 0, x.phases.special[3] = 0, x.phases.special[4] = 0, x.phases.special[5] = 0;
    }
}

bool rolling_hold(ex::Car& c, Extra& x) {
    if (s8(c.control) == 4) return false;
    const u8 k = c.driver_kind;
    const bool driven = k == 2 || k == 3 || k == 4 || (c.control == 0 && k == 0);
    const bool to6 = driven && x.near.driver_line == 7;
    c.driver_kind = 1;
    return to6;
}

bool release(ex::Car& c, Extra& x, bool line_ok) {
    if (c.driver_kind != 1) return false;
    bool done = false;
    if (s8(c.control) == 0) done = line_ok;
    else {
        const u8 k = c.driver_kind;
        done = (k == 2 || k == 3 || k == 4) ? line_ok : true;
    }
    if (done) c.driver_kind = 0, x.start.release_count = 30;
    x.start.rolling_steps = 0;
    return done;
}

// ---- the countdown and the attitude's hold --------------------------------------------------------------------------

bool start_hold(ex::Car& c, bool countdown, u32& counter) {
    if (countdown) {
        c.race_hold = 1;
        return false;
    }
    if (counter == 0) {
        c.race_hold = 0;
        return false;
    }
    if (--counter != 0) {
        c.race_hold = 1;
        return false;
    }
    return true;
}

F hold_lift(u32 n, F v, u32& kind) {
    if (n >= 720) return kind = 0, k0;
    if (n >= 660) return kind = 1, (v * gt3r::ee::int_to_f(s32(720 - n))) / k60;
    if (n < 60) return kind = 0, k0;
    return kind = 2, v;
}

bool hold_lift_body(ex::Car& c, const BodyExtra& b, F v) {
    u32 kind = 0;
    const F front = hold_lift(c.hold, v, kind);
    const F rear = hold_lift(u32(c.hold) + 15, v, kind);
    const u32 out = kind != 0 ? 1 : 0;
    const F ground = c.ground_pos.z, z = c.pos.z;
    const F lo_front = ground + front, lo_rear = ground + rear;
    F hf = z + c.fwd.z * c.p.dist_front;
    F hr = z - c.fwd.z * c.p.dist_rear;
    bool lifted = false;
    if (hf < lo_front) hf = lo_front, lifted = true;
    if (hr < lo_rear) hr = lo_rear, lifted = true;
    if (!lifted) return false;
    if (out == 2) { // (0x36A318 compares its kind with 2: never, it keeps 0 or 1)
        c.pos.z = ground + v;
        c.pitch = k0, c.heave_acc = k0;
    } else {
        c.pos.z = hf * b.front_share + hr * c.p.rear_weight;
        c.pitch = ex::atan2(c.behaviour, hf - hr, c.p.wheelbase);
        c.heave_acc = (c.pos.z - z) * c.inv_dt;
    }
    c.roll = k0;
    c.vel.z = k0, c.roll_rate = k0, c.pitch_rate = k0;
    return true;
}

bool attitude_hold(ex::Car& c, const BodyExtra& b, F line_heading) {
    if (c.hold == 0) return false;
    const F v = kHoldHeight - (c.p.susp[0].offset - b.axle[0].hold_droop);
    if (!hold_lift_body(c, b, v)) return false;
    const F d = angle_diff(line_heading, c.heading);
    if (!(d == k0)) {
        const F step = c.dt * kHoldTurn;
        if (step < d) c.heading = c.heading + step;
        else if (d < -step) c.heading = c.heading - step;
        else c.heading = line_heading;
    }
    rules::body_axes(c);
    return true;
}

} // namespace gt4r::sim::rules
