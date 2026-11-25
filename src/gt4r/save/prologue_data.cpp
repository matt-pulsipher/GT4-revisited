#include "gt4r/save/prologue_data.h"

#include "gt4r/save/codec.h"

#include <algorithm>
#include <cstring>

namespace gt4r::save {

namespace {

u32 get32(ByteSpan b, size_t o) { return u32(b[o]) | u32(b[o + 1]) << 8 | u32(b[o + 2]) << 16 | u32(b[o + 3]) << 24; }
void put32(Bytes& b, size_t o, u32 v) { b[o] = u8(v), b[o + 1] = u8(v >> 8), b[o + 2] = u8(v >> 16), b[o + 3] = u8(v >> 24); }
void put_text(Bytes& b, size_t o, size_t n, const std::string& s) {
    std::memset(b.data() + o, 0, n);
    std::memcpy(b.data() + o, s.data(), std::min(s.size(), n - 1));
}

constexpr u32 kNoTime = 359'999'999;

// A ranking's ten empty entries (`stride` apart from `at`): no time (+0x10), the pass code "--------" (`code_at`).
void empty_ranking(Bytes& b, size_t at, size_t stride, size_t code_at) {
    for (size_t k = 0; k < 10; ++k) {
        put32(b, at + k * stride + 0x10, kNoTime);
        std::memset(b.data() + at + k * stride + code_at, '-', 8);
    }
}

// A controller setting as the controller settings' constructor makes it (Option +0x6C0 + 4 and
// +0x76C + 4; 0x4041F0): the pad's assignments.
const u8 kControllerMap[0x54] = {
    0x01, 0xFF, 0xFF, 0x00, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x07, 0x06, 0x04, 0x05, 0x08, 0x09, 0x02, 0x03, 0xFF, 0x0B, 0x0A, 0xFF,
    0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x02, 0x06, 0x08, 0x0A, 0x07, 0x02, 0x03, 0x01, 0x00, 0x01, 0x00, 0x01, 0x02, 0xFF, 0xFF,
    0xFF, 0xFF, 0x07, 0x06, 0xFF, 0xFF, 0x08, 0x09, 0x02, 0x03, 0x02, 0x03, 0x0A, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01,
    0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x06, 0xFF, 0xFF, 0x08, 0x09, 0xFF, 0x0B, 0x02, 0x03, 0x0A, 0xFF, 0x03, 0x02, 0x01, 0x01,
};

void header(Bytes& b) {
    put32(b, 0, kProloguePayloadMagic), put32(b, 4, u32(-s32(kProloguePayloadSize - 0x10))), put32(b, 12, 0);
    put32(b, 8, crc32(ByteSpan(b.data() + 0x10, kProloguePayloadSize - 0x10)));
}

} // namespace

Bytes new_prologue_payload() {
    Bytes b(kProloguePayloadSize, 0);
    // The license units' rankings (menu/prologue_license.h): ten entries of 0x48 from the unit's +0x10, the
    // time at +0x10, the pass code at +0x24 (0x3FDF80).
    for (size_t k = 0; k < kPrologueLicenseUnits; ++k) empty_ranking(b, kPrologueLicense + k * kPrologueLicenseUnit + 0x10, 0x48, 0x24);
    // The course units' rankings: ten entries of 0x88 from the unit's +0x00, the time at +0x10, the pass code
    // at +0x68 (❓ the course unit's other fields).
    for (size_t k = 0; k < kPrologueCourseUnits; ++k) empty_ranking(b, kPrologueCourse + k * kPrologueCourseUnit, 0x88, 0x68);
    // The options (Option +8 on; written over the last course unit's last 16 bytes): the block's
    // own size, then the constructor's values.
    const size_t o = kPrologueOptions;
    put32(b, o, 0x6B8);
    for (size_t at : {0x10, 0x14, 0x18, 0x20, 0x24, 0x2C, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x60, 0x70}) put32(b, o + at, 1);
    put32(b, o + 0x28, 0x3F666666); // 0.9
    put32(b, o + 0x34, 2), put32(b, o + 0x54, 2), put32(b, o + 0x74, 1200);
    b[o + 0xAE] = 1, b[o + 0xAF] = 1;
    put32(b, o + 0xB0, 0xC63);
    for (size_t k = 0; k < 64; ++k) put32(b, o + 0xC0 + 0x10 * k, 0x100);
    // The two controller settings.
    std::memcpy(b.data() + o + 0x6C0, kControllerMap, sizeof kControllerMap);
    std::memcpy(b.data() + o + 0x714, kControllerMap, sizeof kControllerMap);
    // The LAN battle's settings: the machine's name, three switches, two numbers.
    put_text(b, o + 0x770, 0x10, "machine01");
    put32(b, o + 0x7F4, 1), put32(b, o + 0x7F8, 1), put32(b, o + 0x7FC, 1), put32(b, o + 0x840, 2), put32(b, o + 0x84C, 6);
    // The network settings: the setting's name, the addresses.
    put_text(b, o + 0x850, 0x20, "default setting");
    put32(b, o + 0x870, 1), put32(b, o + 0x8D4, 1);
    put_text(b, o + 0x874, 0x20, "192.168.0.2"), put_text(b, o + 0x894, 0x20, "255.255.255.0"), put_text(b, o + 0x8B4, 0x20, "192.168.0.1");
    put_text(b, o + 0x8D8, 0x20, "192.168.0.100"), put_text(b, o + 0x8F8, 0x20, "192.168.0.101"), put_text(b, o + 0x91C, 0x20, "192.168.0.200");
    header(b);
    return b;
}

Bytes write_prologue(const menu::GameState& s) {
    Bytes b = s.saved_image.size() == kProloguePayloadSize && valid_prologue(s.saved_image) ? s.saved_image : new_prologue_payload();
    put32(b, 0x10, 0); // the garage's car count: Prologue keeps no garage
    put_text(b, kPrologueName, kPrologueNameSize, s.username);
    if (s.prologue_license.size() == kPrologueLicenseUnits * kPrologueLicenseUnit)
        std::memcpy(b.data() + kPrologueLicense, s.prologue_license.data(), s.prologue_license.size());
    header(b);
    return b;
}

bool valid_prologue(ByteSpan p) {
    if (p.size() < kProloguePayloadSize) return false;
    return get32(p, 0) == kProloguePayloadMagic && get32(p, 4) == u32(-s32(kProloguePayloadSize - 0x10)) &&
           get32(p, 8) == crc32(ByteSpan(p.data() + 0x10, kProloguePayloadSize - 0x10));
}

bool read_prologue(ByteSpan p, menu::GameState& s) {
    if (!valid_prologue(p)) return false;
    s.saved_image.assign(p.begin(), p.begin() + kProloguePayloadSize);
    size_t n = 0;
    while (n < kPrologueNameSize && p[kPrologueName + n]) ++n;
    s.username.assign(reinterpret_cast<const char*>(p.data() + kPrologueName), n);
    s.prologue_license.assign(p.begin() + kPrologueLicense, p.begin() + kPrologueLicense + kPrologueLicenseUnits * kPrologueLicenseUnit);
    return true;
}

} // namespace gt4r::save
