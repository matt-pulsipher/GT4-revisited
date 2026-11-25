// The picture of a save's icon file: a made-up icon's, its texture as it is and packed.
#include "test_framework.h"

#include "gt4r/save/card_icon.h"

using namespace gt4r;

namespace {

void put(Bytes& b, u32 v, int n) {
    for (int i = 0; i < n; ++i) b.push_back(u8(v >> (8 * i)));
}

// Two shapes, three vertices, one animation frame with two keys; each pixel its place (x | y << 8, 15 bits kept).
Bytes icon(bool packed) {
    Bytes b;
    put(b, 0x10000, 4), put(b, 2, 4), put(b, packed ? 15 : 7, 4), put(b, 0x3F800000, 4), put(b, 3, 4);
    b.resize(b.size() + 3 * (2 * 8 + 16));
    put(b, 1, 4), put(b, 1, 4), put(b, 0x3F800000, 4), put(b, 0, 4), put(b, 1, 4);
    put(b, 0, 4), put(b, 2, 4);
    b.resize(b.size() + 16);
    Bytes pixels;
    for (u32 y = 0; y < 128; ++y) {
        if (!packed) {
            for (u32 x = 0; x < 128; ++x) put(pixels, (x >> 2) | (y >> 2) << 5, 2);
            continue;
        }
        // Half of the line one color, the rest as it is.
        put(pixels, 32, 2), put(pixels, y >> 2, 2);
        put(pixels, 0x10000 - 96, 2);
        for (u32 x = 32; x < 128; ++x) put(pixels, (x >> 2) | (y >> 2) << 5, 2);
    }
    if (packed) put(b, u32(pixels.size()), 4);
    b.insert(b.end(), pixels.begin(), pixels.end());
    return b;
}

u32 five(u32 v) { return v << 3 | v >> 2; }

} // namespace

TEST(save_icon_picture) {
    const auto plain = save::icon_picture(icon(false)), packed = save::icon_picture(icon(true));
    CHECK(plain && plain->size() == 64 * 64 && packed && packed->size() == 64 * 64);
    if (!plain || !packed) return;
    for (u32 y = 0; y < 64; y += 7)
        for (u32 x = 0; x < 64; x += 5) {
            const u32 want = five(x >> 2) | five(y >> 2) << 8 | 0xFF000000u;
            CHECK((*plain)[y * 64 + x] == want);
            CHECK((*packed)[y * 64 + x] == (x < 32 ? five(y >> 2) | 0xFF000000u : want));
        }
}

TEST(save_icon_not_one) {
    CHECK(!save::icon_picture({}));
    Bytes b = icon(true);
    b[0] = 1;
    CHECK(!save::icon_picture(b));
    // Cut anywhere it is none, and nothing is read past its end.
    b = icon(true);
    for (size_t n = 0; n < b.size(); n += 301) CHECK(!save::icon_picture(ByteSpan(b.data(), n)));
}
