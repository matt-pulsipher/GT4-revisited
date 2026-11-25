#pragma once

// The passes over the 3D picture of the views the player does not drive in (race +0xCC8 not 0: replays, B-spec races,
// demonstrations, the scene behind a race's menus): the depth of field, the heat haze and the vignette.
// docs/formats/course.md "The passes of a replay".

#include "gt4r/race/drive_camera.h"
#include "race/replay_post.h"
#include "render/view3d.h"

namespace gt3r {
class IniDocument;
}
namespace gt3r::race {
struct SunParams;
}

namespace gt4r::race {

// The player's choices ([video] of config.ini): heat_haze = on | off (default off: sharp
// pictures show its lenses slide), replay_vignette, replay_trail = on | off (default on: the game's own
// picture), depth_of_field = off | standard | high.
struct ReplayEffectSettings {
    bool haze = false, vignette = true, trail = true;
    enum class DepthOfField { Off, Standard, High } depth_of_field = DepthOfField::High;
    bool operator==(const ReplayEffectSettings&) const = default;
    static ReplayEffectSettings load(const gt3r::IniDocument& config);
};

// Where a frame is drawn: what the program's frame (0x33F2E8, 0x33E520) asks of it.
struct EffectView {
    bool replay = false; // the race's play mode is not 0 (0x3431B8, 0x33F78C)
    bool several = false; // a half of the screen or a linked one: none of the passes
};

class ReplayEffects {
public:
    // A race's set-up: the lenses from the course's Env0 +0xB0 (0x496488, 0x496548).
    void setup(const gt3r::race::SunParams& env);
    // One picture of 1/60 s (RacePS2Base::vf74 -> 0x4965D8): the lenses drift, whatever the camera.
    void step() { haze_.step(); }
    // The passes of a frame from `pose` (its haze and depth-of-field wishes, focus and aperture), put into `o`; the batches are
    // kept until the next.
    void apply(gt3r::render::DrawOptions& o, const ViewPose& pose, const gt3r::render::Mat4& proj, const EffectView& view, const ReplayEffectSettings& s, float far_clip);

private:
    gt3r::race::HeatHaze haze_;
    std::vector<gt3r::race::HazeLens> lenses_;
    gt3r::render::ScreenBatch haze_batch_, vignette_batch_;
    bool vignette_ = false, haze_on_ = false;
    float nearest_focus_ = 0;
};

} // namespace gt4r::race
