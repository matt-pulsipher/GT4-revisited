#include "gt4r/race/glow_filter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::race {

std::optional<GlowFilter> GlowFilter::from_env(ByteSpan env0) {
    if (env0.size() < 0x1768) return std::nullopt;
    u32 flags;
    std::memcpy(&flags, env0.data() + 0x1750, 4);
    if (!(flags & 1)) return std::nullopt;
    float a, b, c, r;
    std::memcpy(&a, env0.data() + 0x1754, 4), std::memcpy(&b, env0.data() + 0x1758, 4);
    std::memcpy(&c, env0.data() + 0x175C, 4), std::memcpy(&r, env0.data() + 0x1760, 4);
    s32 n;
    std::memcpy(&n, env0.data() + 0x1764, 4);
    // 0x3BADF8 draws nothing for a < 0, a radius of 0 or less, or no points.
    if (a < 0 || !(r > 0) || n <= 0 || a >= 1) return std::nullopt;
    // A GS fixed weight (0x4A2808): trunc(x x 128) held to 0 .. 255, in 128ths.
    const auto fix = [](float x) { return float(std::clamp(int(std::clamp(x * 128.0f, 0.0f, 255.0f)), 0, 255)) / 128.0f; };
    GlowFilter g;
    g.threshold = a;
    g.radius = r / 781.2196f; // (0x44434E0C: the diagonal of 640 x 448)
    g.samples = n;
    g.sum = float(n * n) * fix(c / ((1 - a) * float(n * n)));
    g.gain = fix((1 - a) * b);
    return g;
}

} // namespace gt4r::race
