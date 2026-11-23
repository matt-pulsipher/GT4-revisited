#include "gt4r/formats/course/ai_line.h"

#include <cmath>
#include <cstring>
#include <numbers>

namespace gt4r::course {

namespace {

float rdf(ByteSpan d, size_t o) {
    const u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

constexpr size_t kTable = 32 * 4;

void need(ByteSpan d, u64 off, u64 bytes, const char* what) {
    if (off > d.size() || d.size() - off < bytes) throw FormatError(std::string("AI file: ") + what + " out of range");
}

// "ADLN": +0x0C size, +0x14 line count, +0x18 the offset of its table of line offsets. A line: a 0x40-byte header (u32
// count), then count points of 0x40 bytes.
void parse_floats(ByteSpan s, AiData& out) {
    need(s, 0, 0x1C, "header");
    const u32 n = rd32(s, 0x14), table = rd32(s, 0x18);
    need(s, table, u64(n) * 4, "line table");
    for (u32 li = 0; li < n && li < AiData::kLines; ++li) {
        const u32 off = rd32(s, table + 4 * li);
        if (!off) continue;
        need(s, off, 0x40, "line");
        const u32 count = rd32(s, off);
        need(s, u64(off) + 0x40, u64(count) * 0x40, "line points");
        auto& line = out.lines[li];
        for (u32 k = 0; k < count; ++k) {
            const size_t p = off + 0x40 + size_t(k) * 0x40;
            AiLinePoint pt;
            pt.kind = s[p], pt.brake_adjust = s8(s[p + 1]), pt.speed_adjust = s8(s[p + 2]);
            pt.x = rdf(s, p + 4), pt.y = rdf(s, p + 8);
            pt.segment = rd16(s, p + 0x10);
            pt.radius = rdf(s, p + 0x18);
            pt.heading = rdf(s, p + 0x24);
            line.push_back(pt);
        }
    }
}

// The older layout: +0x00 size, +0x04 version 8, +0x08 eight line offsets (u32 count, then 0x28-byte points: the kind
// and the two adjustments in the first word's low bytes, x and y in 1/4096 m, the segment, the radius in 1/4096 m, the
// heading in the low half of the last word in 1/4096 turn).
void parse_fixed(ByteSpan s, AiData& out) {
    need(s, 0, 0x28, "header");
    for (int li = 0; li < AiData::kLines; ++li) {
        const u32 off = rd32(s, 0x08 + 4 * size_t(li));
        if (!off) continue;
        need(s, off, 4, "line");
        const u32 count = rd32(s, off);
        need(s, u64(off) + 4, u64(count) * 0x28, "line points");
        auto& line = out.lines[size_t(li)];
        for (u32 k = 0; k < count; ++k) {
            const size_t p = off + 4 + size_t(k) * 0x28;
            AiLinePoint pt;
            pt.kind = s[p], pt.brake_adjust = s8(s[p + 1]), pt.speed_adjust = s8(s[p + 2]);
            pt.x = float(s32(rd32(s, p + 4))) / 4096.0f, pt.y = float(s32(rd32(s, p + 8))) / 4096.0f;
            pt.segment = u16(rd32(s, p + 0x10));
            pt.radius = float(s32(rd32(s, p + 0x18))) / 4096.0f;
            pt.heading = float(s16(rd32(s, p + 0x24) & 0xFFFF)) * float(2 * std::numbers::pi / 4096.0);
            line.push_back(pt);
        }
    }
}

} // namespace

bool AiData::looks_like(ByteSpan d) {
    if (d.size() < kTable + 4 || rd32(d, 0) != 0 || rd32(d, 4) != kTable) return false;
    return true;
}

AiData AiData::parse(ByteSpan d) {
    if (!looks_like(d)) throw FormatError("not a drivers' file");
    AiData out;
    auto section = [&](int slot) -> ByteSpan {
        const u32 off = rd32(d, 4 * size_t(slot));
        if (!off) return {};
        u32 end = u32(d.size());
        for (int k = 0; k < 32; ++k) {
            const u32 o = rd32(d, 4 * size_t(k));
            if (o > off && o < end) end = o;
        }
        need(d, off, end - off, "section");
        return d.subspan(off, end - off);
    };
    const ByteSpan lines = section(1);
    if (lines.size() >= 4 && gt3r::has_magic(lines, "ADLN")) {
        out.format = Format::Floats;
        parse_floats(lines, out);
    } else if (lines.size() >= 0x28) {
        out.format = Format::Fixed;
        parse_fixed(lines, out);
    }
    for (int c = 0; c < kClasses; ++c) {
        const ByteSpan s = section(2 + c);
        if (s.empty()) continue;
        Set set{};
        std::memcpy(set.data(), s.data(), std::min(s.size(), kSetSize));
        out.sets[size_t(c)] = set;
    }
    return out;
}

} // namespace gt4r::course
