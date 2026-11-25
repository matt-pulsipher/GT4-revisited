// Prologue's opening shots (start_camera_prologue.h).

#include "gt4r/race/start_camera_prologue.h"

#include <algorithm>
#include <cmath>

namespace gt4r::race {

void PrologueStartCamera::begin(const ReplayScript* script, bool replay, int hold_steps) {
    on_ = script && !script->records.empty();
    if (on_) {
        record_ = script->records.back();
        record_.kind &= ~0xCu; // (the camera looks at the record's own point whatever its kind)
    }
    replay_ = replay, count_ = hold_steps;
    set();
}

void PrologueStartCamera::set() {
    n_ = replay_ ? count_ + 60 : count_ - 60;
    dissolve_ = !replay_ && (n_ == 0 || n_ == 60 || n_ == 120) ? 0.5f : 0.0f;
}

bool PrologueStartCamera::step() {
    if (!active()) {
        dissolve_ = 0;
        return false;
    }
    --count_;
    set();
    return active();
}

bool PrologueStartCamera::pose(const gt3r::render::Mat4& car_body, ViewPose& out) const {
    // (below a count of 59 the program leaves the view as it was)
    if (!active()) return false;
    // The path's frame: x to the car's left, z ahead (the body frame's turned half round; ✔ the original's grid page at
    // Tsukuba: nose, back, side, front).
    gt3r::render::Mat4 frame = car_body;
    for (size_t k = 0; k < 3; ++k) frame.m[k] = -frame.m[k], frame.m[8 + k] = -frame.m[8 + k];
    return shot_pose(record_, std::clamp(1.0f - float(std::max(n_, 59)) / 1200.0f, 0.0f, 1.0f), &frame, out, false);
}

bool PrologueFinishCamera::pose(const gt3r::render::Vec3& car, ViewPose& out) const {
    if (!(distance > 0)) return false;
    namespace render = gt3r::render;
    const float rad = 3.14159265f / 180, p = pitch * rad, a = angle * rad;
    const render::Vec3 eye = car + render::Vec3{-std::sin(a) * std::cos(p), std::sin(p), std::cos(a) * std::cos(p)} * distance;
    const render::Vec3 ahead = render::normalize(car - eye), right = render::normalize(render::cross(ahead, {0, 1, 0})), up = render::cross(right, ahead);
    out.eye = eye;
    out.view.m = {right.x, up.x, -ahead.x, 0, right.y, up.y, -ahead.y, 0, right.z, up.z, -ahead.z, 0,
                  -render::dot(right, eye), -render::dot(up, eye), render::dot(ahead, eye), 1};
    out.fov = fov * rad;
    out.body_hidden = false;
    return true;
}

} // namespace gt4r::race
