#pragma once

// The course's glow filter over the finished picture (docs/formats/course.md: Env0 +0x1750..+0x1764; the race's 0x33F1C8 ->
// 0x3BADF8), on the photo locations that set it (ss_gion, ss_sagano, ss_sanmarco, ss_shibuya).

#include "gt4r/base/types.h"

#include <optional>

namespace gt4r::race {

struct GlowFilter {
    float threshold = 0; // +0x1754: a, the level taken off (1 = 255)
    float radius = 0;    // +0x1760 / 781.22: the box as a fraction of the picture's diagonal (781.22 = 640 x 448's)
    int samples = 0;     // +0x1764: n, the box's points across and down
    float sum = 0;       // n^2 x the GS weight of each of the n^2 copies, min(trunc(128 c / ((1 - a) n^2)), 255) / 128
    float gain = 0;      // the weight the sum is added with, min(trunc(128 (1 - a) b), 255) / 128
    // Env0 (slot 32); none when its switch (+0x1750 bit 0) is off or its values draw nothing (a < 0, no radius or points).
    static std::optional<GlowFilter> from_env(ByteSpan env0);
};

// The filter into the engine's draw options where the shared engine has the pass (DrawOptions::glow); an older one draws none.
template <class Options> void apply_glow(Options& o, const std::optional<GlowFilter>& g) {
    if constexpr (requires { o.glow.on; o.glow.samples; }) {
        o.glow.on = g.has_value();
        if (g) o.glow.threshold = g->threshold, o.glow.radius = g->radius, o.glow.samples = g->samples, o.glow.sum = g->sum, o.glow.gain = g->gain;
    }
}

} // namespace gt4r::race
