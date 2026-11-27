#pragma once

// The course's glow filter over the finished picture (docs/formats/course.md: Env0 +0x1750..+0x1764; the race's 0x33F1C8 ->
// 0x3BADF8), on the photo locations that set it (ss_gion, ss_sagano, ss_sanmarco, ss_shibuya).

#include "gt4r/base/types.h"
#include "render/view3d.h"

#include <optional>

namespace gt4r::race {

struct GlowFilter {
    float threshold = 0; // +0x1754: a, the level taken off (1 = 255)
    float radius = 0;    // +0x1760 / 781.22: the box as a fraction of the picture's diagonal (781.22 = 640 x 448's)
    int samples = 0;     // +0x1764: n, the box's points across and down
    float sum = 0;       // n^2 x the GS weight of each of the n^2 copies, min(trunc(128 c / ((1 - a) n^2)), 255) / 128
    float gain = 0;      // the weight the sum is added with, min(trunc(128 (1 - a) b), 255) / 128
    // Where the copies' sum sits: each place gets the picture around half a pixel of the 640 x 448 frame right of and
    // below it, as fractions of the diagonal (docs/formats/course.md "Glow filter"); the copies' points reach the radius
    // on each side, weighted down linearly (each copy is stretched back linearly from points a radius apart).
    float offset_x = 0, offset_y = 0;
    // Env0 (slot 32); none when its switch (+0x1750 bit 0) is off or its values draw nothing (a < 0, no radius or points).
    static std::optional<GlowFilter> from_env(ByteSpan env0);
};

// The filter into the engine's draw options (DrawOptions::glow, the engine's glow pass).
template <class Options = gt3r::render::DrawOptions> void apply_glow(Options& o, const std::optional<GlowFilter>& g) {
    o.glow.on = g.has_value();
    if (g) o.glow.threshold = g->threshold, o.glow.radius = g->radius, o.glow.samples = g->samples, o.glow.sum = g->sum, o.glow.gain = g->gain;
    // The copies' placement and their linear weights (the engine's Glow::tent / offset).
    if (g) o.glow.tent = true, o.glow.offset[0] = g->offset_x, o.glow.offset[1] = g->offset_y;
}

} // namespace gt4r::race
