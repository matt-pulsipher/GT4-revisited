#pragma once

// Gran Turismo 4 Prologue's opening shots (CameraSys::CameraStarting, 0x3D3A90): the last record of the course's REP4 in
// slot 45 (a race) or 46 (a replay), a path in the followed car's frame, run on the start's count. docs/formats/camera.md
// "Prologue's start camera".

#include "gt4r/race/replay_script.h"

namespace gt4r::race {

constexpr int kSlotPrologueStart = 45, kSlotPrologueReplayStart = 46;

class PrologueStartCamera {
public:
    // `hold_steps`: the steps left of the start's hold (the race information's +0x74).
    void begin(const ReplayScript* script, bool replay, int hold_steps);
    // Once a step, after the step's count went down. False once the camera has run its time.
    bool step();
    bool active() const { return on_ && n_ >= 0; }
    // The view on a car's body frame now; false when the camera has none.
    bool pose(const gt3r::render::Mat4& car_body, ViewPose& out) const;
    // The seconds of the dissolve this step begins (0 none): a race's at its cuts 2, 1 and 0 s before its end.
    float dissolve() const { return dissolve_; }

private:
    ReplayRecord record_;
    bool on_ = false;
    bool replay_ = false;
    int count_ = 0; // the hold's steps left, then below 0 the steps since the start
    int n_ = -1;    // the camera's own count: a race's count - 60, a replay's + 60
    float dissolve_ = 0;
    void set();
};

} // namespace gt4r::race
