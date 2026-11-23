#pragma once

// Where a car's parts are for a state of the simulation: the body's frame and each wheel's.
// Plain arithmetic, apart from the drawing (car_visual.h). docs/formats/model.md "Wheels as the
// game places them".

#include "gt4r/race/car_spec.h"
#include "gt4r/race/simulation.h"
#include "render/math3d.h"

namespace gt4r::race {

// The model's frame in course space: x to the car's right, y up, the front at -z, the origin at
// the middle of the wheelbase on the body's underside (the ride height above the road).
// Whichever point of the car the state's position is (that origin, or the road under it), the
// wheels say how far along the body's up axis the origin lies from it: a hub is at
// y = R - q + its travel in the model's frame.
gt3r::render::Mat4 body_matrix(const CarState& s, const CarSpec& spec);

// Wheel `w`'s frame (0 front left, 1 front right, 2 rear left, 3 rear right) at the outer face
// of its tire: the hub's place, the body's axes steered about its up axis, moved out by half the
// tire's width. The brake parts are drawn in it as they are.
gt3r::render::Mat4 wheel_frame(const CarState& s, const CarSpec& spec, int w);
// The tire's and wheel's matrix: that frame turned about the axle by the wheel's spin, scaled by
// the rim's radius (the models' unit), the right side's turned half round (the models are a left
// wheel's).
gt3r::render::Mat4 wheel_matrix(const CarState& s, const CarSpec& spec, int w);

// Whether a wheel shows its turning pictures: it turns more than 5 degrees in a simulation step.
bool wheel_turning(const WheelState& w, float step);

} // namespace gt4r::race
