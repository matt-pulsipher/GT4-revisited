#include "gt4r/race/car_pose.h"

#include <algorithm>

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

namespace {
Vec3 mix(const Vec3& a, const Vec3& b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }
float dot3(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 unit(const Vec3& v) {
    const float n = std::sqrt(dot3(v, v));
    return n > 1e-9f ? Vec3{v.x / n, v.y / n, v.z / n} : v;
}
Vec3 cross3(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
} // namespace

CarState blend_states(const CarState& a, const CarState& b, float t) {
    CarState s = b;
    if (!(t < 1)) return s;
    t = std::max(t, 0.0f);
    s.position = mix(a.position, b.position, t);
    // The axes: forward, then up square to it, then right square to both on the side it was.
    s.forward = unit(mix(a.forward, b.forward, t));
    Vec3 u = mix(a.up, b.up, t);
    const float k = dot3(u, s.forward);
    s.up = unit({u.x - s.forward.x * k, u.y - s.forward.y * k, u.z - s.forward.z * k});
    const Vec3 r = cross3(s.up, s.forward), r_was = mix(a.right, b.right, t);
    s.right = dot3(r, r_was) >= 0 ? r : Vec3{-r.x, -r.y, -r.z};
    for (int w = 0; w < 4; ++w) {
        WheelState& o = s.wheels[w];
        const WheelState &p = a.wheels[w], &q = b.wheels[w];
        o.center = mix(p.center, q.center, t);
        o.steer = p.steer + (q.steer - p.steer) * t;
        o.spin = p.spin + (q.spin - p.spin) * t;
        o.travel = p.travel + (q.travel - p.travel) * t;
    }
    s.course_v = a.lap == b.lap ? a.course_v + (b.course_v - a.course_v) * t : b.course_v;
    return s;
}

} // namespace gt4r::race
