// The race in a VR headset (race_vr.h). The headset's seated space (its origin at the player's head when centred, -z ahead, y up)
// is placed in the course at the race camera's eye: in the in-car view with the car's attitude (or its heading alone, [vr]
// level_horizon) and the seat moved from there by [vr] seat_side, seat_forward and seat_height; on the car's body the same
// without the seat; in the chase views level, along the camera's lagged heading. Each eye is that place times the eye's tracked
// pose, with its own off-centre lens.

#include "gt4r/app/race_vr.h"

#if GT3R_XR

#include "gt4r/race/vr_seat.h"

#include <cmath>

namespace gt4r::vr {

namespace render = gt3r::render;

bool head_looks(App& app) {
    const gt3r::xr::Session* x = app.xr();
    return x && x->settings().stereo;
}

bool race_eyes(App& app, const race::ViewPose& flat, race::VrSeat seat, const race::CarState& car, bool look_back, std::optional<float> driver_x, RaceEyes& out) {
    gt3r::xr::Session* x = app.xr();
    if (!x || !x->settings().stereo || !x->eyes_valid()) return false;
    const gt3r::xr::Settings& st = x->settings();
    // The seated space's place in the course; the in-car view's seat from the car's camera point on its centre line ([vr]
    // seat_side = center), to its left or right, or the car's driver's: its figure's seat.
    race::VrSeatOptions place;
    place.level_horizon = st.level_horizon;
    place.seat = st.seat(0);
    if (st.seat_side == gt3r::xr::SeatSide::Car && driver_x) place.seat.x = *driver_x;
    const render::Mat4 base = race::vr_seat_place(flat, seat, car, look_back, place);

    const render::Mat4 head = base * x->head().matrix();
    out.head = flat;
    out.head.eye = head.transform_point({0, 0, 0});
    out.head.view = render::inverse(head);
    const gt3r::xr::Eye& e0 = x->eye(0);
    // (ViewPose::fov is across a 4:3 picture's width: the one whose height is the eyes'.)
    const float half_tall = std::max((e0.tan_up - e0.tan_down) * 0.5f, 0.05f);
    out.head.fov = 2 * std::atan(half_tall / 0.75f);
    out.reflection_view = flat.view;
    out.own_car = seat == race::VrSeat::InCar && st.show_car;
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
