#include "gt4r/save/card_icon.h"

namespace gt4r::save {

namespace {

constexpr u32 kSide = 128;

struct Reader {
    ByteSpan data;
    size_t at = 0;
    bool good = true;
    u32 get(size_t n) {
        u32 v = 0;
        if (at > data.size() || data.size() - at < n) good = false;
        else
            for (size_t i = 0; i < n; ++i) v |= u32(data[at + i]) << (8 * i);
        at += n;
        return v;
    }
    void skip(size_t n) {
        if (at > data.size() || data.size() - at < n) good = false;
        at += n;
    }
};

u32 rgba(u32 c) {
    const auto five = [](u32 v) { return (v << 3 | v >> 2) & 0xFF; };
    return five(c & 31) | five(c >> 5 & 31) << 8 | five(c >> 10 & 31) << 16 | 0xFF000000u;
}

} // namespace

std::optional<std::vector<u32>> icon_picture(ByteSpan icon) {
    Reader r{icon};
    const u32 magic = r.get(4), shapes = r.get(4), texture = r.get(4);
    r.skip(4);
    const u32 vertices = r.get(4);
    if (!r.good || magic != 0x10000 || shapes == 0 || shapes > 64 || vertices > 0x10000) return std::nullopt;
    r.skip(size_t(vertices) * (8 * shapes + 16)); // each shape's place, the normal, the texture's place, the color
    r.skip(16);                                   // the animation's head: tag, length, speed, offset
    for (u32 n = r.get(4); n && r.good; --n) {
        r.skip(4);
        r.skip(8 * size_t(r.get(4)));
    }
    std::vector<u16> pixels;
    pixels.reserve(kSide * kSide);
    if (texture & 8) { // packed: a count and one pixel repeated, or (negative) minus that count of pixels as they are
        const u32 length = r.get(4);
        const size_t end = r.at + length;
        while (r.good && r.at < end && pixels.size() < kSide * kSide) {
            const u32 code = r.get(2);
            if (code >= 0x8000)
                for (u32 n = 0x10000 - code; n && r.good; --n) pixels.push_back(u16(r.get(2)));
            else pixels.insert(pixels.end(), code, u16(r.get(2)));
        }
    } else
        while (r.good && pixels.size() < kSide * kSide) pixels.push_back(u16(r.get(2)));
    if (!r.good || pixels.size() < kSide * kSide) return std::nullopt;
    std::vector<u32> out;
    out.reserve(kIconPicture * kIconPicture);
    for (int y = 0; y < kIconPicture; ++y)
        for (int x = 0; x < kIconPicture; ++x) out.push_back(rgba(pixels[size_t(y) * kSide + size_t(x)]));
    return out;
}

} // namespace gt4r::save
