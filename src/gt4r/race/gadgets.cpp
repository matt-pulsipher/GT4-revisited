#include "gt4r/race/gadgets.h"

#include <cmath>
#include <cstring>

namespace gt4r::race {

namespace {
float f32_at(ByteSpan d, size_t o) {
    float f;
    std::memcpy(&f, d.data() + o, 4);
    return f;
}
} // namespace

Gadgets Gadgets::parse(ByteSpan gdgt, ByteSpan rnw4) {
    Gadgets g;
    if (is_gadgets(gdgt)) {
        const u32 n = rd32(gdgt, 0x1C);
        if (n > 64 || 0x20 + 0xA0 * size_t(n) > gdgt.size()) throw FormatError("GDGT: kinds outside the block");
        for (u32 i = 0; i < n; ++i) {
            const size_t o = 0x20 + 0xA0 * size_t(i);
            Kind k;
            k.index = gdgt[o], k.hull_points = std::min<int>(gdgt[o + 3], 8);
            for (int j = 0; j < 3; ++j) k.inertia[size_t(j)] = f32_at(gdgt, o + 4 + 4 * size_t(j));
            for (int j = 0; j < k.hull_points; ++j) k.hull.push_back({f32_at(gdgt, o + 0x10 + 16 * size_t(j)), f32_at(gdgt, o + 0x14 + 16 * size_t(j)), f32_at(gdgt, o + 0x18 + 16 * size_t(j))});
            k.radius = f32_at(gdgt, o + 0x90), k.mass = f32_at(gdgt, o + 0x94);
            g.kinds.push_back(std::move(k));
        }
        if (gdgt.size() > 0x404 && gt3r::has_magic(gdgt.subspan(0x400), "MDLS")) g.models = gdgt.subspan(0x400);
    }
    if (rnw4.size() >= 0x74 && gt3r::has_magic(rnw4, "RNW4")) {
        const u32 n = rd16(rnw4, 0x44), at = rd32(rnw4, 0x70);
        if (n && (at + 0x20 * u64(n) > rnw4.size())) throw FormatError("RNW4: gadgets outside the block");
        for (u32 i = 0; i < n; ++i) {
            const size_t o = at + 0x20 * size_t(i);
            Placement p;
            p.kind = int(rd32(rnw4, o));
            p.position = {f32_at(rnw4, o + 8), f32_at(rnw4, o + 12), f32_at(rnw4, o + 16)};
            for (int j = 0; j < 3; ++j) p.angles[size_t(j)] = f32_at(rnw4, o + 20 + 4 * size_t(j));
            g.placements.push_back(p);
        }
    }
    return g;
}

std::array<float, 9> Gadgets::rotation(const std::array<float, 3>& a) {
    const float sa = std::sin(a[0]), ca = std::cos(a[0]), sb = std::sin(a[1]), cb = std::cos(a[1]), sc = std::sin(a[2]), cc = std::cos(a[2]);
    const float e[9] = {cb * cc - sa * sb * sc, -ca * sc, sb * cc + sa * sc * cb, cb * sc + sa * cc * sb, ca * cc, sb * sc - sa * cc * cb, -ca * sb, sa, ca * cb};
    // (The program multiplies row vectors: its matrix transposed.)
    return {e[0], e[3], e[6], e[1], e[4], e[7], e[2], e[5], e[8]};
}

} // namespace gt4r::race
