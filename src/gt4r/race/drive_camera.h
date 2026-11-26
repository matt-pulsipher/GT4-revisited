#pragma once

// The driving views: the in-car view, the chase view and the roof view, each at one of the
// car's camera places. docs/formats/camera.md "The driving views".

#include <cmath>

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

// What a lens of A radians of the program's shows across a 4:3 picture: tan(A / 2) x 1.4556 to a side. The view's size factor K (0x456918,
// kept at 0x8465E4) of the original's states is 293.1175 / tan(A / 2) for every camera whose lens the disc gives (the replay shots, the
// scenic ones, the start clips, the roof view), and a picture's half width is 426.667 / K (docs/formats/camera.md).
inline float shown_angle(float lens) { return 2 * std::atan(std::tan(lens * 0.5f) * (426.6667f / 293.1175f)); }

// The in-car view's rear-view mirror: the tangent of half its picture's height, 0.325 (its 259 x 63 of a 640 x 480 picture shows
// 2 x 0.325 x 259 / 63 = 2.67 across, 106 degrees). Measured on the original's picture: in the saved state at Tsukuba's start
// (03-race/state_at_start) the start gantry behind is 103.5 of 640 pixels between its posts, and here 98.5 with 0.341 (the
// in-car lens zoomed 1.3) from the same eye (🔎 the program's mirror camera was not read).
inline constexpr float kMirrorHalfTan = 0.325f;

// A camera for one drawn frame.
struct ViewPose {
    gt3r::render::Vec3 eye;
    gt3r::render::Mat4 view;      // course space -> view space (x right, y up, looking along -z)
    float fov = 1.2f;       // radians across a 4:3 picture's width
    bool body_hidden = false; // the camera's own car is not drawn (the in-car view)
    // What the camera asks of the picture (its +0xEC bits), its focus (+8, metres) and its aperture (an f-number, +0x150 / 100):
    // the views the player does not drive in take them (race/replay_effects.h).
    bool haze = false, depth_of_field = false;
    float focus = 100, f_number = 8;

    // The vertical angle for the renderer: a picture wider than 4:3 shows more at the sides.
    float fovy() const { return 2 * std::atan(std::tan(fov * 0.5f) * 0.75f); }
};

// A pose between two steps' for a frame shown between them (`t` 0 at `a`, 1 at `b`): the eye and
// the view's turn blended (the turn made square again), the lens blended; `b` itself when the two
// are a cut apart (the eye more than `cut` metres away, or another lens or body setting).
ViewPose blend_poses(const ViewPose& a, const ViewPose& b, float t, float cut = 3.0f);

class DriveCamera {
public:
    // The views the change-view button steps through, as camera places of the car.
    static constexpr int kViews[3] = {0, 1, 6};
    static constexpr int kInCar = 0, kChase = 1, kRoof = 6;
    // Ours: the in-car view without the mirror, and a chase view from farther back.
    static constexpr int kInCarNoMirror = 100, kFarChase = 101;
    // The front bumper's place.
    static constexpr int kBumper = 5;
    static bool chase(int view) { return view == kChase || view == kFarChase; }
    static bool in_car(int view) { return view == kInCar || view == kInCarNoMirror; }

    // The start of a race, or a car placed anew.
    void reset(const CarState& car);
    // Once per simulation step (h seconds), after the car has moved.
    void step(const CarState& car, float h);
    // Looking sideways in the in-car view (0x37AF94): left or right held, 60 degrees. And ours, for a pad whose right stick is
    // free: the stick's direction (x right, y up, -1 .. 1) is where the view turns, in the in-car and chase views.
    void set_sideways(bool left, bool right) { side_held_ = (right ? 60.0f : 0.0f) - (left && !right ? 60.0f : 0.0f); }
    void set_stick(float x, float y) { stick_x_ = x, stick_y_ = y; }
    // The in-car view's shake with the speed and the tires' slips (0x37A7A8); off: steady.
    void set_shake(bool on) { shake_on_ = on; }
    // Turned aside (no mirror).
    bool aside() const;
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
    // The sideways look, degrees (+ right), and the stick's, radians.
    float side_held_ = 0, side_ = 0, stick_x_ = 0, stick_y_ = 0, look_ = 0;
    struct Shaker {
        float x = 0, v = 0, u = 0;
        void step(float input); // 0x384B70
    } shake_pitch_, shake_yaw_;
    // A hit's jolt of the in-car view (0x37A8E4 ..): three turns and three shifts, each a spring (0x3853A0) kicked for the
    // hit's step: pitch, yaw, roll (degrees), then across, up, along (metres).
    struct Spring {
        float x = 0, v = 0;
        void step(float input);
    };
    Spring jolt_[6];
    float shake_speed_ = 0;
    bool shake_on_ = true;
    u32 seed_ = 0x87654321u;
    float draw();
};

// A car's heading (radians about the vertical, + to the right of +z... as atan2 of its forward
// axis' x and z) and pitch (+ nose up).
float car_heading(const CarState& car);
float car_pitch(const CarState& car);

} // namespace gt4r::race
