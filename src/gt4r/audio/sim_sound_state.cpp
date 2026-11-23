#include "gt4r/audio/sim_sound_state.h"

#include "gt4r/sim/session.h"

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
    return s;
}

} // namespace gt4r::audio
