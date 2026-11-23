#include "gt4r/formats/course/ai_line_course.h"

#include <cstring>

namespace gt4r::course {

std::optional<AiData> ai_from_course(const CourseFile& file, ByteSpan data) {
    const ByteSpan lines = file.section(data, kSlotAiLines);
    // u32 size, u32 version 8, u32 0, then the line offsets.
    if (lines.size() < 0x28 || rd32(lines, 4) != 8 || rd32(lines, 0) > lines.size()) return std::nullopt;
    // A drivers' file of the same sections: 32 offsets, slot 1 the lines, slots 2 .. 5 the sets.
    constexpr size_t kTable = 32 * 4;
    Bytes ad(kTable);
    auto add = [&](int slot, ByteSpan s, size_t size) {
        const size_t at = ad.size();
        for (size_t k = 0; k < 4; ++k) ad[4 * size_t(slot) + k] = u8(at >> (8 * k));
        ad.resize(at + size);
        std::memcpy(ad.data() + at, s.data(), std::min(s.size(), size));
    };
    add(1, lines, lines.size());
    for (int c = 0; c < AiData::kClasses; ++c) {
        const ByteSpan set = file.section(data, kSlotDriverSets + c);
        if (set.size() >= 0x20) add(2 + c, set.first(std::min<size_t>(set.size(), 0x40)), AiData::kSetSize);
    }
    return AiData::parse(ad);
}

} // namespace gt4r::course
