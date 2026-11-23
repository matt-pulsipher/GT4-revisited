#include "gt4r/race/car_pose.h"

#include <cmath>

namespace gt4r::race {

namespace {

namespace render = gt3r::render;
using render::Mat4;

constexpr float kPi = 3.14159265f;

render::Vec3 v3(const course::Vec3& v) { return {v.x, v.y, v.z}; }

Mat4 basis(const render::Vec3& right, const render::Vec3& up, const render::Vec3& back, const render::Vec3& origin) {
    Mat4 m;
    m.m = {right.x, right.y, right.z, 0, up.x, up.y, up.z, 0, back.x, back.y, back.z, 0, origin.x, origin.y, origin.z, 1};
    return m;
}

} // namespace

Mat4 body_matrix(const CarState& s, const CarSpec& spec) {
    const render::Vec3 right = v3(s.right), up = v3(s.up), forward = v3(s.forward), pos = v3(s.position);
    float lift = 0;
    for (int w = 0; w < 4; ++w) {
        const int axle = w / 2;
        lift += render::dot(v3(s.wheels[w].center) - pos, up) - (spec.tire_radius[axle] - spec.ride_height[axle] + s.wheels[w].travel);
    }
    return basis(right, up, forward * -1.0f, pos + up * (lift * 0.25f));
}

Mat4 wheel_frame(const CarState& s, const CarSpec& spec, int w) {
    const bool right_side = (w & 1) != 0;
    const WheelState& ws = s.wheels[w];
    // (A positive steering angle turns the wheel's front to the right: about +y that is a negative turn.)
    return basis(v3(s.right), v3(s.up), v3(s.forward) * -1.0f, v3(ws.center)) * render::rotate_y(-ws.steer) *
           render::translate({(right_side ? 0.5f : -0.5f) * spec.tire_width[w / 2], 0, 0});
}

Mat4 wheel_matrix(const CarState& s, const CarSpec& spec, int w) {
    const float r = spec.rim_radius[w / 2];
    // (Rolling forward takes the top of the wheel toward the front, -z: a negative turn about +x.)
    Mat4 m = wheel_frame(s, spec, w) * render::rotate_x(-s.wheels[w].spin) * render::scale({r, r, r});
    if (w & 1) m = m * render::rotate_y(kPi);
    return m;
}

bool wheel_turning(const WheelState& w, float step) { return std::fabs(w.spin_rate * step) * (180.0f / kPi) > 5.0f; }

} // namespace gt4r::race
