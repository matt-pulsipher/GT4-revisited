#pragma once

// The photo pause's camera (Replay Theater Photo Mode and the photo drive's replays: `PhotoPause` 0x3C3C20 over the camera
// manager's camera kind 7, `CameraShot`): the cameras it offers (the ShotManager's list, 0x3FFD10), the pad as it reads it (the
// race input's mode 2, 0x3FAD78), and a step of the camera (CameraShot::vf10 0x37FFB0). docs/formats/photo.md "The photo pause".

#include "gt4r/base/types.h"
#include "gt4r/race/drive_camera.h"

#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace gt4r::race::photo {

using Vec3 = gt3r::render::Vec3;
using gt3r::s8;

// The settings' steps (0x620EB0 .. 0x620FB8): an f-number x 100, a shutter time (negative: 1 / -n s), an exposure (EV), a white
// balance (K).
inline constexpr std::array<int, 16> kApertures = {140, 180, 200, 220, 250, 280, 320, 350, 400, 480, 560, 670, 800, 1100, 1600, 2200};
inline constexpr std::array<int, 19> kShutters = {-60, -80, -100, -125, -160, -200, -250, -320, -400, -500, -640, -800, -1000, -1250, -1600, -2000, -2500, -3200, -4000};
inline constexpr std::array<float, 21> kExposures = {-1.0f, -0.9f, -0.8f, -0.7f, -0.6f, -0.5f, -0.4f, -0.3f, -0.2f, -0.1f, 0.0f,
                                                     0.1f,  0.2f,  0.3f,  0.4f,  0.5f,  0.6f,  0.7f,  0.8f,  0.9f,  1.0f};
inline constexpr std::array<float, 11> kBalances = {2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500};
// A shutter time, seconds (0x36FA20).
float shutter_seconds(int value);

// The angles of the program's cameras, degrees (0x36EA98, 0x36EA50, 0x36EAD8, 0x36EB98): the turn of a direction about y (0 along
// -z, 90 along +x), its pitch (negative above the horizon), an angle brought into 0 .. 360, the difference a - b into -180 .. 180.
float yaw_of(const Vec3& d);
float pitch_of(const Vec3& d);
float wrap360(float a);
float difference(float a, float b);

// One camera of the list (the ShotManager's records of 0x74 bytes). Each keeps its own settings while the pause lasts.
struct Shot {
    u32 kind = 0;                    // +0x00: a roadside point's kind (bit 2: the eye may be raised and lowered)
    Vec3 eye;                        // +0x04
    float yaw = 0, pitch = 0;        // +0x10: the way the camera first looks (at the car), degrees
    float fov_max = 83.2f;           // +0x1C: the widest view, degrees over the picture's height
    float distance = 10;             // +0x20: from the eye to the car
    float pitch_low = -90, pitch_high = 41.6f; // +0x24, +0x28: the view's top and bottom, turned from the first way
    float y_low = 0, y_high = 0;     // +0x2C, +0x30: the eye's lowest and highest
    float turn_yaw = 0, turn_pitch = 0, turn_roll = 0; // +0x34: the player's turn of it
    float fov = 30;                  // +0x40
    float focus = 10;                // +0x44
    int aperture = 5, shutter = 3, exposure = 10, balance = 6; // +0x48 .. +0x54: indices of the steps (F2.8, 1/125 s, 0 EV, 5,500 K)
    bool portrait = false;           // +0x58: held upright
    Vec3 focus_point;                // +0x5C
    float fov_min = 2;               // +0x68: the closest view (a 0.7 m wide stretch at the car's distance)
    bool fov_min_set = false;        // +0x6E
    u8 limits = 0;                   // +0x6D: the view at its edge: 1 left, 2 right, 4 top, 8 bottom
    bool manual_focus = false;       // +0x6F
    bool focus_point_found = false;  // +0x70
};

// What the list is made from (0x3FFD10).
struct ShotScene {
    Vec3 car;                    // the followed car's place (0x3FC618)
    Vec3 right{1, 0, 0}, back{0, 0, 1}; // its frame across the ground
    float half_width = 0.9f, front = 2.2f, rear = 2.2f; // its box (0x3F3708 .. 0x3F3748)
    // The camera the replay shows (its eye, view and angle: the program's, radians).
    Vec3 eye;
    gt3r::render::Mat4 view;
    float lens = 0.5f;
    // The course's roadside points that cover the car's place on the lap (0x3FDC50: at most 63): kind and place.
    std::vector<std::pair<u32, Vec3>> points;
    // The ground's height under (x, z) from a height (0x3FE358); false where there is none.
    std::function<bool(float x, float z, float from, float& y)> ground;
    // A line from the car's middle to a point, shortened to 0.15 m before what it meets (0x3FE7B0).
    std::function<Vec3(const Vec3& from, const Vec3& to)> clear;
    u32 seed = 1;
};
std::vector<Shot> make_shots(const ShotScene& scene);

// The photo pause's pad (the race input in mode 2, 0x3FAD78; the replay's words of 0x43D288) as the camera reads it.
struct PadButtons {
    bool l1 = false, l2 = false, r1 = false, r2 = false;   // held
    bool up = false, down = false;                         // held
    int vertical_repeat = 0, horizontal_repeat = 0;        // the direction buttons as they repeat: -1 up / left, +1 down / right
    bool circle = false, triangle = false, square = false, cross = false; // pressed
    float left_x = 0, left_y = 0, right_x = 0, right_y = 0; // -1 .. 1, down positive
};
struct Pad {
    float left_x = 0, left_y = 0, right_x = 0, right_y = 0; // +0x3C .. +0x48
    s8 climb = 0, camera = 0, exposure = 0, balance = 0, aperture = 0, shutter = 0; // +0x4C .. +0x51
    bool portrait = false, filter = false, focus = false, shoot = false, shoot_on_point = false; // +0x52 .. +0x56
    u8 held = 0; // +0x57: 1 L1, 2 L2, 3 R1, 4 R2 (alone)
};
Pad read_pad(const PadButtons& b);
// The value the pause's movie blinks (GETBLINK, PhotoPause +0x1AC): 0 white balance (L2), 1 exposure (L1), 2 shutter (R2),
// 3 aperture (R1); -1 none or more than one held.
int blinking(const PadButtons& b);

// The camera on one shot (CameraShot: vf39 0x37FBE0 begin, vf10 0x37FFB0 a step).
class ShotCamera {
public:
    // `aspect`: the picture's width over its height (the manager's screen, 4 / 3).
    void begin(Shot& shot, float aspect = 4.0f / 3.0f);

    struct World {
        // What the finder's middle shows, metres along the view (the scenery or the car).
        std::function<float(const Vec3& eye, const gt3r::render::Mat4& view)> middle_depth;
        // A focus on the car by itself for the view (0x3FD3D8; none: no car).
        std::function<std::optional<float>(const gt3r::render::Mat4& view, float f_number, float fov_degrees)> car_focus;
    };
    struct Events {
        bool refused = false; // the "disable" sound: a limit met
        bool focused = false; // Square: the focus taken (ISFOCUSTRIGGER, the "zoom" sound)
        bool shutter = false;
    };
    Events step(const Pad& pad, const World& world);

    const Shot& shot() const { return *shot_; }
    Vec3 eye() const { return shot_->eye; }
    float yaw() const { return shot_->yaw + shot_->turn_yaw; }
    float pitch() const { return shot_->pitch + shot_->turn_pitch; }
    float roll() const { return shot_->turn_roll; }
    gt3r::render::Mat4 view() const; // Rz(roll) Rx(pitch) Ry(yaw) T(-eye)
    float fov() const { return shot_->fov; }   // degrees over the picture's height
    float focus() const { return focus_; }
    float f_number() const;
    float shutter_seconds() const;
    float exposure() const;
    float kelvin() const;
    bool portrait() const { return shot_->portrait; }
    bool manual_focus() const { return shot_->manual_focus; }
    u8 limits() const { return shot_->limits; }
    // The focal length the movie shows (GETZOOM: CameraVariable::vf33 0x3793A8, the film 24 mm high), mm.
    int focal_mm() const;

private:
    Shot* shot_ = nullptr;
    float aspect_ = 4.0f / 3.0f;
    float focus_ = 10;
    // The speed-ups (0x36F6A0: a count up to a limit): the turn, the roll, the zoom, the height.
    struct Ramp {
        int count = 0, limit = 18;
        float step(bool on, float most, int sign = 1);
    };
    Ramp turn_, roll_, zoom_, climb_{0, 30};
    int roll_hold_ = 0;  // +0x1B8: steps the roll stays at 0 when it crosses it
    bool at_fov_limit_ = false, at_roll_limit_ = false; // +0x1BC, +0x1BD
    std::array<bool, 4> pushed_{}; // +0x1C0: the view pushed against an edge
    bool clamp_again_ = true;      // +0x1DC
};

} // namespace gt4r::race::photo
