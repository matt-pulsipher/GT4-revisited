#include "gt4r/race/replay_effects.h"

#include "base/config.h"
#include "race/sun_glare.h"

#include <algorithm>

namespace gt4r::race {

namespace {
constexpr float kFocalMm = 24; // (0x371CA8: the screen's aspect x 24 / 1000, metres; the aspect is 1 in a race)
}

ReplayEffectSettings ReplayEffectSettings::load(const gt3r::IniDocument& config) {
    ReplayEffectSettings s;
    auto word = [&](const char* key, const char* otherwise) { return gt3r::to_lower(config.get("video", key).value_or(otherwise)); };
    s.haze = word("heat_haze", "off") == "on";
    s.vignette = word("replay_vignette", "on") != "off";
    s.trail = word("replay_trail", "on") != "off";
    const std::string dof = word("depth_of_field", "high");
    s.depth_of_field = dof == "off" ? DepthOfField::Off : dof == "standard" ? DepthOfField::Standard : DepthOfField::High;
    return s;
}

void ReplayEffects::setup(const gt3r::race::SunParams& env) {
    haze_.setup(env);
    haze_on_ = env.has_env && env.haze, vignette_ = env.has_env && env.vignette;
    nearest_focus_ = env.has_env ? env.dof_min_focus : 0;
    vignette_batch_.clear();
    if (vignette_) gt3r::race::add_vignette(vignette_batch_, env);
    haze_batch_.clear();
}

void ReplayEffects::apply(gt3r::render::DrawOptions& o, const ViewPose& pose, const gt3r::render::Mat4& proj, const EffectView& view, const ReplayEffectSettings& s, float far_clip) {
    if (view.several) return;
    // The haze is asked for by the camera alone (0x33F568: flag 0x2000 and Env0 bit 5); only the replay script's shots have it.
    haze_batch_.clear();
    if (s.haze && haze_on_ && pose.haze && haze_.active()) {
        haze_.lenses(pose.view, proj, lenses_);
        gt3r::race::add_haze_lenses(haze_batch_, lenses_);
        o.haze = &haze_batch_;
    }
    if (!view.replay) return;
    if (s.depth_of_field != ReplayEffectSettings::DepthOfField::Off && pose.depth_of_field) {
        // (0x33E520: the lens of the camera's own aperture and 24 mm, focused no nearer than Env0 +0x938.)
        o.dof.on = true, o.dof.pyramid = s.depth_of_field == ReplayEffectSettings::DepthOfField::Standard;
        o.dof.focal_mm = kFocalMm, o.dof.f_number = pose.f_number;
        o.dof.focus = std::max(pose.focus, nearest_focus_);
        o.dof.far = far_clip;
    }
    if (s.vignette && vignette_) o.vignette = &vignette_batch_;
}

} // namespace gt4r::race
