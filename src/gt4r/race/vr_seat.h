#pragma once

// Where a VR headset's seated space sits in a race (app/race_vr.cpp, docs/VR.md): its origin (the player's head when centred,
// -z ahead, y up) placed at the race camera's eye, turned as the view asks.

#include "gt4r/race/drive_camera.h"
#include "gt4r/race/simulation.h"

namespace gt4r::race {

enum class VrSeat {
    InCar, // the in-car view: with the car's attitude, the player's seat moved from the car's camera point
    OnCar, // a view on the car's body (the roof, the bumper): with the car's attitude
    Chase, // the chase views: level, along the camera's lagged heading
};

struct VrSeatOptions {
    bool level_horizon = false; // the car's heading alone, not its pitch and roll (the in-car and on-car views)
    gt3r::render::Vec3 seat;    // the in-car view's seat from the car's camera point, in the car's frame (x right, y up, z back)
};

// The seated space -> the course, from the race's flat view `flat` (its eye; a chase view's heading from its view), the car the
// view is about (its axes), looking back (the in-car and on-car views turn half round; a chase view's own look back is in its view).
gt3r::render::Mat4 vr_seat_place(const ViewPose& flat, VrSeat seat, const CarState& car, bool look_back, const VrSeatOptions& options);

} // namespace gt4r::race
