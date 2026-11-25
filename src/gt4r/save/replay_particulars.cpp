#include "gt4r/save/replay_particulars.h"

#include "gt4r/save/codec.h"

#include <algorithm>
#include <cstring>

namespace gt4r::save {

namespace {

// The payload (0xE8 bytes): +0x00 u64 the car's code, +0x08 u64 the course's, +0x10 the title, +0x50 the course's label, +0x90
// the car's name (0x40 bytes each), +0xD0 u32 the time in ms, +0xD8 s8 the mode, +0xD9 s8 its number, +0xE0 u32 the B-spec
// driver's run, +0xE4 u32 a license test's.
constexpr size_t kPayload = 0xE8, kText = 0x40;

std::string text_at(const Bytes& b, size_t at) {
    const char* p = reinterpret_cast<const char*>(b.data() + at);
    return std::string(p, strnlen(p, kText - 1));
}

} // namespace

std::optional<ReplayParticulars> decode_particulars(ByteSpan file) {
    if (file.size() != scrambled_size(kPayload)) return std::nullopt;
    const std::optional<Bytes> b = unscramble(file, 0, FileKind::Particulars);
    if (!b || b->size() < kPayload) return std::nullopt;
    ReplayParticulars p;
    p.car = u64(rd32(*b, 0)) | u64(rd32(*b, 4)) << 32, p.course = u64(rd32(*b, 8)) | u64(rd32(*b, 12)) << 32;
    p.title = text_at(*b, 0x10), p.course_label = text_at(*b, 0x50), p.car_name = text_at(*b, 0x90);
    p.time_ms = rd32(*b, 0xD0);
    p.mode = static_cast<signed char>((*b)[0xD8]), p.submode = static_cast<signed char>((*b)[0xD9]);
    if (p.mode < 0 || p.mode >= int(std::size(kReplayModes))) p.mode = 0;
    p.spectator = rd32(*b, 0xE0) != 0, p.license = rd32(*b, 0xE4) != 0;
    return p;
}

std::optional<Bytes> rename_particulars(ByteSpan file, const std::string& title) {
    if (file.size() != scrambled_size(kPayload)) return std::nullopt;
    std::optional<Bytes> b = unscramble(file, 0, FileKind::Particulars);
    if (!b) return std::nullopt;
    const size_t n = std::min(title.size(), kText - 1);
    std::memset(b->data() + 0x10, 0, kText);
    std::memcpy(b->data() + 0x10, title.data(), n);
    return scramble(*b, 0, u32(b->size()) * 0x9E3779B9u ^ crc32(*b), FileKind::Particulars);
}

} // namespace gt4r::save
