#include "gt4r/race/sections.h"

#include <cstring>

namespace gt4r::race {

std::vector<float> road_list(ByteSpan d, size_t list) {
    std::vector<float> out;
    if (d.size() < 0x5C || !gt3r::has_magic(d, "RNW4")) return out;
    const u32 lists = rd16(d, 0x38), at = rd32(d, 0x58);
    if (list >= lists || u64(at) + 8 * u64(lists) > d.size()) return out;
    const u32 n = rd32(d, at + 8 * u32(list)), p = rd32(d, at + 8 * u32(list) + 4);
    if (n > 256 || u64(p) + 4 * u64(n) > d.size()) return out;
    for (u32 i = 0; i < n; ++i) {
        float f;
        std::memcpy(&f, d.data() + p + 4 * i, 4);
        out.push_back(f);
    }
    return out;
}

} // namespace gt4r::race
