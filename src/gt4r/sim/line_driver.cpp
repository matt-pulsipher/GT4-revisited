#include "gt4r/sim/line_driver.h"

#include <algorithm>
#include <cmath>

namespace gt4r::sim {

LineDriver::LineDriver(const Session& session, size_t car) : session_(&session), car_(car) {
    GroundHit h;
    if (session.ground().under(session.cars()[car].position, h)) road_ = h.material;
}

race::CarInput LineDriver::drive(float throttle, bool brake_for_turns) {
    const race::CarState& car = session_->cars()[car_];
    const RoadGround& ground = session_->ground();
    race::CarInput in;
    in.throttle = throttle;
    const float speed = std::fabs(car.speed);
    const float ahead = 10.0f + speed * 0.7f;
    Vec3 aim;
    const Vec3 near = has_aim_ ? last_aim_ : car.position;
    if (!ground.road_middle(car.course_v + ahead, road_, near, aim)) {
        Vec3 direction;
        ground.middle_at(car.course_v + ahead, aim, direction);
    }
    last_aim_ = aim, has_aim_ = true;
    const float dx = aim.x - car.position.x, dz = aim.z - car.position.z;
    const float along = dx * car.forward.x + dz * car.forward.z, across = dx * car.right.x + dz * car.right.z;
    const float angle = std::atan2(across, std::max(along, 1.0f));
    in.steer = std::clamp(angle * 2.5f, -1.0f, 1.0f);
    if (brake_for_turns) {
        // The bend of the road further ahead against the speed it allows (about 1 g across).
        Vec3 far;
        if (ground.road_middle(car.course_v + ahead * 2.5f, road_, aim, far)) {
            const float fx = far.x - car.position.x, fz = far.z - car.position.z;
            const float bend = std::fabs(std::atan2(fx * car.right.x + fz * car.right.z, std::max(fx * car.forward.x + fz * car.forward.z, 1.0f)));
            const float reach = std::sqrt(fx * fx + fz * fz);
            const float radius = bend > 0.02f ? reach / (2.0f * std::sin(bend)) : 1e6f;
            const float allowed = std::sqrt(9.0f * radius);
            if (speed > allowed * 1.05f) in.throttle = 0, in.brake = std::clamp((speed - allowed) / 10.0f, 0.2f, 1.0f);
            else if (speed > allowed * 0.95f) in.throttle = std::min(in.throttle, 0.3f);
        }
    }
    return in;
}

} // namespace gt4r::sim
