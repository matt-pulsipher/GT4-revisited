#include "gt4r/race/sections.h"

#include <cstring>

namespace gt4r::race {

std::vector<float> road_list(ByteSpan d, size_t list) {
    std::vector<float> out;
    if (d.size() >= 0x60 && gt3r::has_magic(d, "GTRW")) { // (Prologue's 0x415FE0 / 0x416028: the records of type 2 of the list)
        const u32 n = rd16(d, 0x22), at = rd32(d, 0x24);
        if (u64(at) + 8 * u64(n) > d.size()) return out;
        for (u32 i = 0; i < n; ++i) {
            if (rd16(d, at + 8 * i) != 2 || rd16(d, at + 8 * i + 2) != list) continue;
            float f;
            std::memcpy(&f, d.data() + at + 8 * i + 4, 4);
            out.push_back(f);
        }
        return out;
    }
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

std::vector<RoadSection> road_sections(ByteSpan d) {
    std::vector<RoadSection> out;
    if (d.size() < 0xA0 || !gt3r::has_magic(d, "RNW4")) return out;
    const u32 n = rd16(d, 0x54), at = rd32(d, 0x9C);
    if (n > 64 || u64(at) + 0x20 * u64(n) > d.size()) return out;
    for (u32 i = 0; i < n; ++i) {
        RoadSection r;
        std::memcpy(&r.start, d.data() + at + 0x20 * i, 4);
        std::memcpy(&r.goal, d.data() + at + 0x20 * i + 4, 4);
        const size_t rec = at + 0x20 * size_t(i);
        const auto read_places = [&](u32 count, u32 from, std::vector<course::Runway::Place>& to) {
            for (u32 j = 0; j < count && j < 16 && u64(from) + 16 * (u64(j) + 1) <= d.size(); ++j) {
                const size_t o = from + 16 * size_t(j);
                course::Runway::Place place;
                std::memcpy(&place.pos.x, d.data() + o, 4), std::memcpy(&place.pos.y, d.data() + o + 4, 4), std::memcpy(&place.pos.z, d.data() + o + 8, 4);
                std::memcpy(&place.heading, d.data() + o + 12, 4);
                to.push_back(place);
            }
        };
        read_places(rd16(d, rec + 0x0A), rd32(d, rec + 0x18), r.grid);
        read_places(rd16(d, rec + 0x0E), rd32(d, rec + 0x10), r.staff);
        out.push_back(r);
    }
    return out;
}

} // namespace gt4r::race
