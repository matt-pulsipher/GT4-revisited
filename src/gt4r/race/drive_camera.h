#pragma once

// The driving views: the in-car view, the chase view and the roof view, each at one of the
// car's camera places. docs/formats/camera.md "The driving views".

#include "gt4r/race/car_spec.h"
#include "gt4r/race/simulation.h"
#include "render/math3d.h"

#include <span>
#include <vector>

namespace gt4r::race {

// One of a car's 25 camera places (CAR4 info +0x40), turned into the model's frame: x right,
// y up, the front at -z; angles in degrees.
struct CameraMount {
    gt3r::render::Vec3 pos;
    float pitch = 0, yaw = 0, roll = 0;
    float zoom = 0; // 1 / tan(half the view angle)
    u32 flags = 0;  // 0: unused; bit 1: the car's own body is drawn
};
// The places of a car file (car/lod/<model>); empty when it has none.
std::vector<CameraMount> read_camera_mounts(ByteSpan car_file);

// A camera for one drawn frame.
struct ViewPose {
    gt3r::render::Vec3 eye;
    gt3r::render::Mat4 view;      // course space -> view space (x right, y up, looking along -z)
    float fov = 1.2f;       // radians across a 4:3 picture's width
    bool body_hidden = false; // the camera's own car is not drawn (the in-car view)

    // The vertical angle for the renderer: a picture wider than 4:3 shows more at the sides.
    float fovy() const { return 2 * std::atan(std::tan(fov * 0.5f) * 0.75f); }
};

class DriveCamera {
public:
    // The views the change-view button steps through, as camera places of the car.
    static constexpr int kViews[3] = {0, 1, 6};
    static constexpr int kInCar = 0, kChase = 1, kRoof = 6;

    // The start of a race, or a car placed anew.
    void reset(const CarState& car);
    // Once per simulation step (h seconds), after the car has moved.
    void step(const CarState& car, float h);
    // The view from the car's place `mount` of `mounts`, for the body's frame `body`
    // (car_pose.h body_matrix). `look_back`: held.
    ViewPose pose(const CarState& car, const gt3r::render::Mat4& body, std::span<const CameraMount> mounts, int mount, bool look_back) const;

private:
    // The chase view's frame: the car's heading and pitch, each following through a spring.
    float heading_ = 0, heading_rate_ = 0, heading_accel_ = 0;
    float pitch_ = 0, pitch_rate_ = 0;
    // The in-car view's nod: the body's pitch against the road's, on a spring.
    float nod_ = 0, nod_rate_ = 0;
    float road_pitch_ = 0;
};

// A car's heading (radians about the vertical, + to the right of +z... as atan2 of its forward
// axis' x and z) and pitch (+ nose up).
float car_heading(const CarState& car);
float car_pitch(const CarState& car);

} // namespace gt4r::race
