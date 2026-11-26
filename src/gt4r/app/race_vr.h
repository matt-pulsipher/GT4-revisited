#pragma once

// The race in a VR headset (docs/VR.md; the engine's docs/dev/VR.md): while the player drives, each eye's view from the tracked
// head, seated in the car (the in-car view), riding it (the roof and bumper views) or floating behind it (the chase views); the
// race's display and mirror go on the panel in front (the session's canvas). Replays, the opening shots, the trackside cameras,
// the pit's camera, the race's menus and the split screen stay on the virtual screen. Builds without GT3R_XR have none of it.

#include "gt4r/app/app.h"
#include "gt4r/app/race_world.h"
#include "gt4r/race/drive_camera.h"

#include <array>
#include <optional>

namespace gt4r::vr {

// Where the race's camera sits.
enum class Seat {
    InCar, // the in-car view: the player's head in the car's driver's place, turning with the car
    OnCar, // a view on the car's body (the roof, the bumper): turning with the car
    Chase, // the chase views: behind the car, level, turning with the camera's lagged heading
};

struct RaceEyes {
    race::ViewPose head;                // the head: its eye between the eyes, its view, a lens as tall as the eyes'
    gt3r::render::Mat4 reflection_view; // the race camera's view (RaceWorld::render_eyes)
    std::array<RaceWorld::Eye, 2> eyes;
    bool own_car = false;               // [vr] car_body: the car's body drawn round the in-car view
};

#if GT3R_XR
// This frame's eyes when the headset shows the race in stereo ([vr] stereo on, the eyes tracked): placed from the race's flat
// view `flat` (its eye) in `seat`, `car` the car it is about (its attitude), looking back or not; `driver_x` where the car's
// driver sits across it (metres right of its centre line, its figure's seat: [vr] seat_side = car), none when it has no figure.
// False: the race goes on the virtual screen as on a screen.
bool race_eyes(App& app, const race::ViewPose& flat, Seat seat, const race::CarState& car, bool look_back, std::optional<float> driver_x, RaceEyes& out);
// After the eyes: the frame's canvas again, cleared transparent for the race's display on the panel; the frame goes out in stereo.
void race_panel(App& app, Ui& ui);
// The head looks round: the race's stick look is off ([vr] stereo).
bool head_looks(App& app);
#else
inline bool race_eyes(App&, const race::ViewPose&, Seat, const race::CarState&, bool, std::optional<float>, RaceEyes&) { return false; }
inline void race_panel(App&, Ui&) {}
inline bool head_looks(App&) { return false; }
#endif

} // namespace gt4r::vr
