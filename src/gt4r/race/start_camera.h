#pragma once

// The race's opening shots: the course's camera motions (`Mot1`, course slots 52 and 53) played
// through the start's hold, each clip chosen by the seconds left. docs/formats/camera.md "Camera
// motions" and "The start's shots".

#include "gt4r/base/types.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/race/drive_camera.h"
#include "gt4r/race/start_camera_prologue.h"

#include <array>
#include <string>
#include <vector>

namespace gt4r::race {

// The course file's slots of the motions: the race's start and the replay's.
constexpr int kSlotStartMotion = 52, kSlotReplayStartMotion = 53;

// A channel of a motion: a value keyed in seconds, a cubic Bezier between each two keys.
struct MotionChannel {
    float first = 0;
    struct Piece {
        float c1 = 0, c2 = 0, end_time = 0, end_value = 0;
    };
    std::vector<Piece> pieces;
    float at(float t) const;
};

struct MotionClip {
    std::string name;
    float length = 0; // seconds
    std::vector<MotionChannel> channels;
};

// A course's camera motions: the clips in the file's order.
struct CameraMotion {
    std::vector<MotionClip> clips;
    float total = 0; // the sequence's seconds as the file states them

    static bool is_motion(ByteSpan d) { return d.size() >= 0x20 && gt3r::has_magic(d, "Mot1"); }
    // Throws FormatError.
    static CameraMotion parse(ByteSpan section);
};

// A camera clip's channels at time t: the eye, the point looked at and the up vector in the
// clip's own frame (x across, y up, z toward the anchor's back), and the field of view (radians).
struct MotionPose {
    course::Vec3 eye, look, up{0, 1, 0};
    float fov = 0.7f;
};
MotionPose motion_pose(const MotionClip& clip, float t);

// The opening's camera. The race's motion (slot 52) runs for the hold less one second; the
// replay's (slot 53) for the hold and one second more. The clip is chosen by the seconds left:
// race: the long clip while 2 s or more remain, then the second, then the third; replay: the long
// clip while 4 s or more remain, then one clip a second.
class StartCamera {
public:
    // `grid`: the course's start place 0 (the long clip's anchor); `car`: the followed car where the
    // camera begins (the other clips' anchor).
    void begin(const CameraMotion* motion, bool replay, int hold_steps, const course::Runway::Place& grid, const CarState& car);
    // Prologue's (its courses have no motions): the path of slot 45 or 46 on the hold's count.
    void begin_prologue(const ReplayScript* script, bool replay, int hold_steps) {
        motion_ = nullptr;
        prologue_.begin(script, replay, hold_steps);
    }
    // Once a step. False once the camera has run its time (the race's own view follows).
    bool step();
    bool active() const { return prologue_.active() || (motion_ && left_ > 0); }
    // The view now; false when the camera has no shot. `car_body`: the followed car's body frame now (Prologue's path runs in it).
    bool pose(ViewPose& out, const gt3r::render::Mat4* car_body = nullptr) const;
    // The seconds of the dissolve this step begins (Prologue's; 0 none).
    float dissolve() const { return prologue_.dissolve(); }
    // Which clip plays (-1 none).
    int clip() const { return clip_; }
    float clip_time() const { return t_; }

private:
    int choose(float seconds_left) const;

    PrologueStartCamera prologue_;
    const CameraMotion* motion_ = nullptr;
    bool replay_ = false;
    int left_ = 0; // steps
    int clip_ = -1;
    float t_ = 0;
    course::Vec3 grid_pos_, car_pos_;
    float grid_heading_ = 0, car_heading_ = 0;
};

} // namespace gt4r::race
