#include "gt4r/race/vr_seat.h"

#include <cmath>

namespace gt4r::race {

namespace render = gt3r::render;
using RVec = render::Vec3;

namespace {
constexpr float kPi = 3.14159265f;
// A turn about y whose -z is `ahead`'s level part.
render::Mat4 heading(const RVec& ahead) { return render::rotate_y(std::atan2(-ahead.x, -ahead.z)); }
} // namespace

render::Mat4 vr_seat_place(const ViewPose& flat, VrSeat seat, const CarState& car, bool look_back, const VrSeatOptions& options) {
    if (seat == VrSeat::Chase) // (the view's backward axis is its turn's third row)
        return render::translate(flat.eye) * heading(RVec{-flat.view.at(2, 0), -flat.view.at(2, 1), -flat.view.at(2, 2)});
    const RVec right{car.right.x, car.right.y, car.right.z}, up{car.up.x, car.up.y, car.up.z}, ahead{car.forward.x, car.forward.y, car.forward.z};
    render::Mat4 turn = heading(ahead);
    if (!options.level_horizon) { // (the columns: the car's right, up and backward axes)
        const RVec axes[3] = {right, up, ahead * -1.0f};
        for (int c = 0; c < 3; ++c) turn.at(0, c) = axes[c].x, turn.at(1, c) = axes[c].y, turn.at(2, c) = axes[c].z;
    }
    render::Mat4 place = render::translate(flat.eye) * turn;
    if (seat == VrSeat::InCar) place = place * render::translate(options.seat);
    if (look_back) place = place * render::rotate_y(kPi);
    return place;
}

} // namespace gt4r::race
