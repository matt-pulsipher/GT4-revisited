#pragma once

// The driving views: the in-car view, the chase view and the roof view, each at one of the
// car's camera places. docs/formats/camera.md "The driving views".

#include "gt4r/race/car_visual.h"
#include "gt4r/race/simulation.h"
#include "render/math3d.h"

namespace gt4r::race {

// A camera for one drawn frame.
struct ViewPose {
    render::Vec3 eye;
    render::Mat4 view;      // course space -> view space (x right, y up, looking along -z)
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
    // The view from the car's place `mount`. `look_back`: held.
    ViewPose pose(const CarState& car, const CarVisual& visual, int mount, bool look_back) const;

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
