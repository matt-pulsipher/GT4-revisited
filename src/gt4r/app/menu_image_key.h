#pragma once

// A key for a decoded picture's texture by what the picture holds: the menus' pictures are
// decoded per page and freed with their packs, so a picture's address can come back holding
// another picture (a cache by address then showed the wrong one).

#include "formats/tex1.h"
#include "gt4r/base/types.h"

namespace gt4r {

inline u64 image_key(const gt3r::Tex1::Image& img) {
    u64 h = 1469598103934665603ull;
    auto mix = [&h](u64 v) { h = (h ^ v) * 1099511628211ull; };
    mix(img.width), mix(img.height);
    const size_t words = img.rgba.size();
    const size_t step = words > 512 ? words / 512 : 1;
    for (size_t i = 0; i < words; i += step) mix(img.rgba[i]);
    return h;
}

} // namespace gt4r
