// The race in a VR headset (race_vr.h). The headset's seated space (its origin at the player's head when centred, -z ahead, y up)
// is placed in the course at the race camera's eye: in the in-car view with the car's attitude (or its heading alone, [vr]
// level_horizon) and the seat moved from there by [vr] seat_side, seat_forward and seat_height; on the car's body the same
// without the seat; in the chase views level, along the camera's lagged heading. Each eye is that place times the eye's tracked
// pose, with its own off-centre lens.

#include "gt4r/app/race_vr.h"

#if GT3R_XR

#include <cmath>

namespace gt4r::vr {

namespace render = gt3r::render;
using RVec = render::Vec3;

namespace {

constexpr float kPi = 3.14159265f;

// A turn about y whose -z is `ahead` (its level part).
render::Mat4 heading(const RVec& ahead) { return render::rotate_y(std::atan2(-ahead.x, -ahead.z)); }

} // namespace

bool head_looks(App& app) {
    const gt3r::xr::Session* x = app.xr();
    return x && x->settings().stereo;
}

bool race_eyes(App& app, const race::ViewPose& flat, Seat seat, const race::CarState& car, bool look_back, std::optional<float> driver_x, RaceEyes& out) {
    gt3r::xr::Session* x = app.xr();
    if (!x || !x->settings().stereo || !x->eyes_valid()) return false;
    const gt3r::xr::Settings& st = x->settings();
    // The seated space's place in the course.
    render::Mat4 base;
    if (seat == Seat::Chase) {
        // (The view's backward axis is its turn's third row.)
        base = render::translate(flat.eye) * heading(RVec{-flat.view.at(2, 0), -flat.view.at(2, 1), -flat.view.at(2, 2)});
    } else {
        const RVec right{car.right.x, car.right.y, car.right.z}, up{car.up.x, car.up.y, car.up.z}, ahead{car.forward.x, car.forward.y, car.forward.z};
        render::Mat4 turn = heading(ahead);
        if (!st.level_horizon) {
            turn = render::Mat4{};
            for (int r = 0; r < 3; ++r) {
                const float v[3] = {r == 0 ? right.x : r == 1 ? right.y : right.z, r == 0 ? up.x : r == 1 ? up.y : up.z, r == 0 ? -ahead.x : r == 1 ? -ahead.y : -ahead.z};
                for (int c = 0; c < 3; ++c) turn.at(r, c) = v[c];
            }
        }
        base = render::translate(flat.eye) * turn;
        // The in-car view's seat: the car's camera point on its centre line ([vr] seat_side = center), a seat to its left or right,
        // or the car's driver's: its figure's seat.
        if (seat == Seat::InCar) {
            RVec at = st.seat(0);
            if (st.seat_side == gt3r::xr::SeatSide::Car && driver_x) at.x = *driver_x;
            base = base * render::translate(at);
        }
        if (look_back) base = base * render::rotate_y(kPi);
    }

    const render::Mat4 head = base * x->head().matrix();
    out.head = flat;
    out.head.eye = head.transform_point({0, 0, 0});
    out.head.view = render::inverse(head);
    const gt3r::xr::Eye& e0 = x->eye(0);
    // (ViewPose::fov is across a 4:3 picture's width: the one whose height is the eyes'.)
    const float half_tall = std::max((e0.tan_up - e0.tan_down) * 0.5f, 0.05f);
    out.head.fov = 2 * std::atan(half_tall / 0.75f);
    out.reflection_view = flat.view;
    out.own_car = seat == Seat::InCar && st.show_car;
    for (int i = 0; i < 2; ++i) {
        const gt3r::xr::Eye& e = x->eye(i);
        const render::Mat4 world = base * e.pose.matrix();
        RaceWorld::Eye& o = out.eyes[size_t(i)];
        o.view = render::inverse(world);
        o.eye = world.transform_point({0, 0, 0});
        o.frustum = {e.tan_left, e.tan_right, e.tan_down, e.tan_up};
        o.canvas = e.canvas, o.w = e.width, o.h = e.height;
    }
    return true;
}

void race_panel(App& app, Ui& ui) {
    gt3r::xr::Session* x = app.xr();
    if (!x) return;
    SDL_Renderer* r = ui.renderer();
    SDL_SetRenderTarget(r, x->canvas());
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    x->submit_stereo();
}

} // namespace gt4r::vr

#endif // GT3R_XR
