#include "gt4r/audio/sim_sound_state.h"

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

CarSoundState sound_state(const sim::Session& session, size_t i) {
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
    // The gearbox's whine (0x34FED0): the torque through the clutch, the gear's ratio against the
    // first's, the driven wheels' speed. 🔎 (the program reads its engine torque, F +0x51C, and an
    // axle speed by the layout, 0x350DA0)
    s.drive_torque = c.clutch_torque.host();
    const int gear = std::clamp(int(c.gear), 0, 7);
    s.gear_ratio = c.p.d.ratio[size_t(gear)].host(), s.low_ratio = c.p.d.ratio[1].host();
    const size_t axle = c.p.d.driven_axle ? 2 : 0;
    s.shaft_speed = 0.5f * (std::fabs(car.wheels[axle].spin_rate) + std::fabs(car.wheels[axle + 1].spin_rate));
    // The step's contact and landings, as the seam publishes them.
    const CarSoundState seam = sound_state(car);
    s.contact = seam.contact, s.contact_kind = seam.contact_kind;
    for (size_t w = 0; w < 4; ++w) s.wheels[w].landing = seam.wheels[w].landing;
    return s;
}

} // namespace gt4r::audio
