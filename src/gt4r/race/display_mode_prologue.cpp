#include "gt4r/race/display_mode_prologue.h"

#include "base/ee_float.h"

#include <algorithm>
#include <cmath>

namespace gt4r::race {

using gt3r::race::HudMode;
using gt3r::race::HudPlay;
using gt3r::race::HudRun;

HudMode prologue_display_mode(HudRun run, HudPlay play) {
    // The program's race classes: the arcade's races (and its demonstration) RACE_MODE_SINGLE, the school's tests and lessons
    // RACE_MODE_LICENSE, the machine tests theirs. It has no free run, time trial, free practice or split-screen race.
    HudMode mode = HudMode::Race;
    switch (run) {
    case HudRun::License: mode = HudMode::License; break;
    case HudRun::MachineTime: mode = HudMode::MachineTime; break;
    case HudRun::MachineSpeed: mode = HudMode::MachineSpeed; break;
    default: break;
    }
    if (play == HudPlay::Replay) return HudMode::Replay;
    if (play == HudPlay::Demo) return HudMode::Demo;
    return mode;
}

float prologue_times_opacity(int steps, float from) {
    if (steps <= 0) return from;
    constexpr float kRate = 6.66666698f * 0.0166666660f; // (the fades' rate, 1 / 0.15 as the program holds it, times its step 0x3C888888)
    constexpr int kHold = 480;                          // (the step on which 8 s less a 60th a step falls below 0)
    float a = std::max(0.0f, from - kRate);
    if (steps == 1) return a;
    a = std::min(1.0f, a + kRate * float(std::min(steps, kHold) - 1));
    if (steps > kHold) a = std::max(0.0f, a - kRate * float(steps - kHold));
    return a;
}

float prologue_screen_x(float x) {
    namespace ee = gt3r::ee;
    using gt3r::s32, gt3r::u32;
    // (VU0's products and sums each chopped; the conversion to fixed point truncates)
    const auto mul = [](u32 a, u32 b) { return ee::from_double(ee::to_double(a) * ee::to_double(b), ee::Round::Chop); };
    const u32 across = ee::sub(mul(ee::from_host(x), ee::from_host(1.0f / 320)), ee::from_host(1.0f));
    const u32 gs = ee::add(ee::from_host(2048.0f), mul(across, ee::from_host(320.0f)));
    const s32 fixed = s32(std::trunc(ee::to_double(gs) * 16));
    return float(fixed - 1728 * 16) / 16;
}

} // namespace gt4r::race
