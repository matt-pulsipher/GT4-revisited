#include "gt4r/audio/sim_sound_state.h"

#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_drive.h"
#include "gt4r/sim/session.h"

#include <algorithm>
#include <cmath>

namespace gt4r::audio {

int engine_place(const sim::Spec& spec) {
    int place = spec.b(3);
    if (place == 0) {
        const int drive = spec.drive();
        place = drive == 3 ? 2 : drive == 4 ? 3 : 1;
    }
    return place >= 1 && place <= 3 ? place : 1; // (4 is heard as the front)
}

CarSoundState sound_state(const sim::Session& session, size_t i, const RaceSoundTables* tables) {
    const race::CarState& car = session.cars()[i];
    const gt3r::sim::exact::Car& c = session.record(i);
    CarSoundState s;
    s.position = car.position, s.velocity = car.velocity;
    s.course_distance = car.course_v;
    s.rpm = c.omega.host() * 9.549297f;
    s.throttle = c.throttle.host();
    s.torque_full = c.torque_wot.host(), s.torque_friction = c.torque_friction.host();
    s.clutch = c.clutch;
    s.speed = c.speed.host();
    s.drag = c.p.drag_coef.host();
    s.slipstream = s.cabin_wake = c.slip_filter.host();
    s.engine_place = engine_place(session.spec(i));
    // (the record's spool level and blow-off already carry the program's two scales)
    s.turbine_level = c.turbo_spool;
    s.turbine_pitch = c.turbo_pitch;
    s.blow_off = c.blowoff.host();
    for (size_t w = 0; w < 4; ++w) {
        const gt3r::sim::exact::Wheel& in = c.w[w];
        const race::WheelState& at = car.wheels[w];
        WheelSoundState& o = s.wheels[w];
        o.position = at.center;
        o.surface = sound_surface(at.material, at.surface);
        o.rolling = in.smoke_count != 0 || at.on_ground;
        o.slip = in.slip_intensity.host();
        o.pitch = in.skid_pitch.host();
        o.rim_speed = in.rim_speed.host();
        o.width = c.p.width[w >> 1].host();
    }
    // The gearbox's whine (0x34FED0): the clutch's torque (F +0x51C), the gear's ratio against the
    // first's (the driveline block's +0x40 + 4 gear), the driven axles' speed (0x350DA0, by the
    // layout). Without Gran Turismo 4's routines running, the engine's record and the driven wheels.
    s.drive_torque = c.clutch_torque.host();
    const int gear = std::clamp(int(c.gear), 0, 9);
    if (session.gt4_rules()) {
        const sim::rules::DrivelineBlock& d = session.gt4_record(i).drive.d;
        s.gear_ratio = d.ratio[size_t(gear)].host(), s.low_ratio = d.ratio[1].host();
        s.shaft_speed = sim::rules::driven_axle_speed(c, d).host();
        if (d.layout == 7) {
            // Layout 7's motors (0x356D88): the front wheels' mean rim speed x 0.012 + 0.4, the
            // level the motors' output (F +0x51C) / 4, its sign kept.
            s.whine_given = true;
            s.whine_pitch = 0.5f * (std::fabs(c.w[0].rim_speed.host()) + std::fabs(c.w[1].rim_speed.host())) * 0.012f + 0.4f;
            s.whine_level = c.clutch_torque.host() * 0.25f;
        } else if (d.kind == 3) {
            // A one-speed box (0x369510): only with the brake pedal down and the clutch asked for, the
            // driven group's two wheels: their mean rim speed x 0.012 + 0.3, their brake pressures x 0.2.
            s.whine_given = true;
            if (c.brake_pedal.host() != 0 && c.clutch_req.host() != 0) {
                const sim::rules::WheelLayout& l = session.wheel_layout(i);
                const size_t a = l.group_first[std::min<size_t>(d.driven_axle, 3)] & 3, b = l.partner[a] < 4 ? l.partner[a] : a;
                s.whine_pitch = std::fabs(0.5f * (c.w[a].rim_speed.host() + c.w[b].rim_speed.host())) * 0.012f + 0.3f;
                s.whine_level = (c.w[a].brake.host() + c.w[b].brake.host()) * 0.2f;
            }
        }
    } else {
        s.gear_ratio = c.p.d.ratio[size_t(std::min(gear, 7))].host(), s.low_ratio = c.p.d.ratio[1].host();
        const size_t axle = c.p.d.driven_axle ? 2 : 0;
        s.shaft_speed = 0.5f * (std::fabs(car.wheels[axle].spin_rate) + std::fabs(car.wheels[axle + 1].spin_rate));
    }
    // An afterfire's moment (0x3F3368): the shift's end at full throttle; and the car's chance.
    s.shift_flame = c.shift_lockout == 11 && c.throttle.host() > 0.9f;
    if (tables) s.afterfire_chance = tables->afterfire_chance(session.spec(i).b(0x120), c.p.e.boost_max.host() > 0, spec_figures(session.spec(i)).ps);
    // The step's strongest contact as the hit sound's recorder keeps it (0x350328 from the collision
    // step), and the landings as the seam publishes them.
    if (i < session.cars().size()) {
        const sim::collide::Body& b = session.body(i);
        s.contact = b.hit_kind >= 0 ? b.hit_sound.host() : 0.0f;
        s.contact_kind = std::max(b.hit_kind, 0);
    }
    // A wheel's landing (0x3FA7B8): the step it met the ground (its contact state, wheel + 0xDC, 2),
    // the hub's speed into the ground (wheel + 0xD8, negated). Without Gran Turismo 4's routines
    // running, the seam's estimate.
    if (session.gt4_rules()) {
        const sim::rules::BodyExtra& b = session.gt4_record(i).body;
        for (size_t w = 0; w < 4; ++w) s.wheels[w].landing = b.contact[w] == 2 ? -b.normal_speed[w].host() : 0.0f;
    } else {
        const CarSoundState seam = sound_state(car);
        for (size_t w = 0; w < 4; ++w) s.wheels[w].landing = seam.wheels[w].landing;
    }
    return s;
}

} // namespace gt4r::audio
