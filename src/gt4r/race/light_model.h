#pragma once
// Gran Turismo 4's light model where it differs from the shared renderer's defaults (docs/formats/model.md "Lighting").

#include "render/view3d.h"

namespace gt4r::race {

// The road sheen's viewer (course chunk kind 4, 0x49EB90): the light model's viewer in eye space, +0xAB0 of the renderer's
// state, starts at (0, 0, 0, 1) (0x4A0080) and no caller moves it, so the sheen is seen from the camera itself; the shared
// renderer's (Gran Turismo 3's) is 2 m above it (DrawOptions::sheen_viewer_up's default).
inline void use_gt4_light_model(gt3r::render::DrawOptions& o) { o.sheen_viewer_up = 0.0f; }

} // namespace gt4r::race
