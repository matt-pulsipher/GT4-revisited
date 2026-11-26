// Where a VR headset's seated space sits in a race (src/gt4r/race/vr_seat.h).

#include "test_framework.h"

#include "gt4r/race/vr_seat.h"

#include <cmath>

using gt3r::render::Mat4;
using gt3r::render::Vec3;
using gt4r::race::CarState;
using gt4r::race::ViewPose;
using gt4r::race::VrSeat;
using gt4r::race::VrSeatOptions;

namespace {

bool near(const Vec3& a, const Vec3& b, float eps = 1e-4f) { return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps; }
Vec3 dir(const Mat4& m, const Vec3& v) { return m.transform_point(v) - m.transform_point({0, 0, 0}); }

// A car heading along +x, nose 10 degrees up (its forward axis), at the origin.
CarState climbing_car() {
    CarState c;
    const float a = 10 * 3.14159265f / 180;
    c.forward = {std::cos(a), std::sin(a), 0};
    c.up = {-std::sin(a), std::cos(a), 0};
    c.right = {0, 0, 1}; // (forward x up)
    return c;
}

} // namespace

TEST(vr_seat_in_car_rides_the_car) {
    const CarState car = climbing_car();
    ViewPose flat;
    flat.eye = {5, 1, 2};
    VrSeatOptions o;
    const Mat4 m = gt4r::race::vr_seat_place(flat, VrSeat::InCar, car, false, o);
    CHECK(near(m.transform_point({0, 0, 0}), flat.eye));                                     // the head at the camera's point
    CHECK(near(dir(m, {0, 0, -1}), {car.forward.x, car.forward.y, car.forward.z}));         // ahead: the car's nose, pitch and all
    CHECK(near(dir(m, {1, 0, 0}), {car.right.x, car.right.y, car.right.z}));
    // The seat 0.36 m to the right, 0.1 m up, 0.2 m forward: in the car's frame.
    o.seat = {0.36f, 0.1f, -0.2f};
    const Mat4 s = gt4r::race::vr_seat_place(flat, VrSeat::InCar, car, false, o);
    const Vec3 want = Vec3{5, 1, 2} + Vec3{0, 0, 1} * 0.36f + Vec3{car.up.x, car.up.y, car.up.z} * 0.1f + Vec3{car.forward.x, car.forward.y, car.forward.z} * 0.2f;
    CHECK(near(s.transform_point({0, 0, 0}), want));
    // Level horizon: the heading alone.
    o.level_horizon = true;
    const Mat4 l = gt4r::race::vr_seat_place(flat, VrSeat::InCar, car, false, o);
    CHECK(near(dir(l, {0, 0, -1}), {1, 0, 0}));
    CHECK(near(dir(l, {0, 1, 0}), {0, 1, 0}));
    // Looking back: half round about the car's up.
    o.level_horizon = false, o.seat = {};
    const Mat4 b = gt4r::race::vr_seat_place(flat, VrSeat::InCar, car, true, o);
    CHECK(near(dir(b, {0, 0, -1}), {-car.forward.x, -car.forward.y, -car.forward.z}));
    // On the body (the roof): the car's attitude, no seat.
    o.seat = {0.36f, 0, 0};
    CHECK(near(gt4r::race::vr_seat_place(flat, VrSeat::OnCar, car, false, o).transform_point({0, 0, 0}), flat.eye));
}

TEST(vr_seat_chase_is_level) {
    // A chase view looking down 8 degrees along -z, turned 30 degrees to the left.
    const float yaw = 30 * 3.14159265f / 180, pitch = -8 * 3.14159265f / 180;
    ViewPose flat;
    flat.eye = {1, 2, 3};
    flat.view = gt3r::render::inverse(gt3r::render::translate(flat.eye) * gt3r::render::rotate_y(yaw) * gt3r::render::rotate_x(pitch));
    const Mat4 m = gt4r::race::vr_seat_place(flat, VrSeat::Chase, climbing_car(), false, {});
    CHECK(near(m.transform_point({0, 0, 0}), flat.eye));
    CHECK(near(dir(m, {0, 1, 0}), {0, 1, 0}));                                                // level
    const Vec3 ahead = dir(m, {0, 0, -1}), want = dir(gt3r::render::rotate_y(yaw), {0, 0, -1}); // the camera's heading
    CHECK(near(ahead, want));
}
