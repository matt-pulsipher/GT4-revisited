// Gran Turismo 4 Prologue's recorded runs (demonstration_prologue.h).

#include "gt4r/formats/replay/demonstration_prologue.h"

#include "gt4r/save/codec.h"
#include "gt4r/sim/program.h"

#include <algorithm>
#include <cstring>

namespace gt4r::replay::prologue {

namespace {

constexpr u32 kDeltaBases = 0x5031D8;
constexpr size_t kHeader = 0x149C, kEntries = 0x518, kEntrySize = 0x1F0, kMaxEntries = 8, kRecorderHeader = 0x1088;

// The recorder's bit reader (0x49DEC8): most significant bit first, zeros past the end; the deltas as 0x373230 reads them.
class Bits {
public:
    explicit Bits(ByteSpan d) : d_(d) {}
    u32 read(int n) {
        u32 v = 0;
        for (int i = 0; i < n; ++i, ++pos_) v = v << 1 | (pos_ >> 3 < d_.size() ? d_[pos_ >> 3] >> (7 - (pos_ & 7)) & 1u : 0u);
        return v;
    }
    u32 gamma() {
        int k = 0;
        while (read(1) && k < 31) ++k;
        return (1u << k) + read(k);
    }
    s32 delta(const std::array<s32, 8>& base) {
        const u32 k = read(3);
        const bool neg = read(1) != 0;
        const s32 v = base[k] + s32(read(int(2 * k + 1)));
        return neg ? -v : v;
    }

private:
    ByteSpan d_;
    size_t pos_ = 0;
};

// One recorder's stream (u32 T, u32 its length with these 8 bytes, the bits) played as 0x3732B0 plays it.
std::optional<ControlStream> read_stream(ByteSpan d, const std::array<s32, 8>& base, size_t& used) {
    if (d.size() < 8) return std::nullopt;
    ControlStream s;
    s.frame_count = rd32(d, 0);
    const u32 len = rd32(d, 4);
    if (len < 8 || len > d.size() || s.frame_count > 0x100000) return std::nullopt;
    used = (size_t(len) + 3) & ~size_t(3);
    Bits bits(d.subspan(8, len - 8));
    PadRecord rec;
    s32 prev = 0;
    u32 run = 0;
    const auto add8 = [&](size_t o) { rec.bytes[o] = u8(rec.bytes[o] + bits.delta(base)); };
    const auto add16 = [&](size_t o) {
        const u16 x = u16(u16(rec.bytes[o] | rec.bytes[o + 1] << 8) + bits.delta(base));
        rec.bytes[o] = u8(x), rec.bytes[o + 1] = u8(x >> 8);
    };
    for (u32 f = 1; f < s.frame_count; ++f) {
        // 0x373178: the steering carried on as 2x less the one before, within +-4096, while |x| >= 16.
        const s16 x = rec.steer();
        const s32 next = 2 * s32(x) - prev;
        prev = x;
        if (std::abs(s32(x)) >= 16) {
            const s16 c = s16(std::clamp(next, -0x1000, 0x1000));
            rec.bytes[4] = u8(c), rec.bytes[5] = u8(u16(c) >> 8);
        }
        if (run) --run;
        else {
            const u32 mask = bits.gamma() - 1;
            if (mask & 0x100) add8(0);
            if (mask & 0x080) add8(1);
            if (mask & 0x010) add8(2);
            if (mask & 0x002) add16(4);
            if (mask & 0x001) add16(6);
            if (mask & 0x004) add16(8);
            if (mask & 0x008) add16(0xA);
            if (mask & 0x040) add16(0xC);
            if (mask & 0x020) add16(0xE);
            run = bits.gamma() - 1;
        }
        s.frames.push_back(rec);
    }
    return s;
}

} // namespace

u64 Entry::code(size_t at) const { return u64(rd32(equipment, at)) | u64(rd32(equipment, at + 4)) << 32; }

std::array<s32, 8> delta_bases(const sim::Program& program) {
    std::array<s32, 8> b{};
    for (u32 k = 0; k < 8; ++k) b[k] = s32(program.w(kDeltaBases + 4 * k));
    return b;
}

std::optional<Run> Run::parse(ByteSpan file, const std::array<s32, 8>& bases, std::string* error) {
    const auto fail = [&](const char* m) -> std::optional<Run> {
        if (error) *error = m;
        return std::nullopt;
    };
    if (file.size() < 0x40) return fail("too small");
    const u32 hdr = rd32(file, 0), len = rd32(file, 4), key = rd32(file, 0xC);
    if (hdr < 0x10 || hdr > file.size() || len > file.size() - hdr || rd16(file, 0xA) != 4) return fail("bad Serialize header");
    Bytes b(file.begin() + hdr, file.begin() + hdr + len);
    if (save::crc32(b) != key) return fail("the body is not its key's"); // (of the body as it is stored)
    for (u8& c : b) c = u8(~c);
    if (b.size() < kHeader + 4 || std::memcmp(&b[4], "GT2K", 4) != 0) return fail("not a race header (GT2K)");
    Run r;
    r.race_class.assign(reinterpret_cast<const char*>(&b[0x0C]), strnlen(reinterpret_cast<const char*>(&b[0x0C]), 0x40));
    std::memcpy(r.information.data(), &b[0x4C], r.information.size());
    r.event = u64(rd32(b, 0x68)) | u64(rd32(b, 0x6C)) << 32, r.course = u64(rd32(b, 0x70)) | u64(rd32(b, 0x74)) << 32;
    r.time_ms = s32(rd32(b, 0x78)), r.time2_ms = s32(rd32(b, 0x7C));
    const u32 count = rd32(b, 0x510);
    if (count > kMaxEntries) return fail("bad entry count");
    for (u32 i = 0; i < count; ++i) {
        const size_t e = kEntries + kEntrySize * i;
        Entry n;
        std::memcpy(n.equipment.data(), &b[e], n.equipment.size());
        std::memcpy(n.condition.data(), &b[e + 0x140], n.condition.size());
        n.x160 = rd32(b, e + 0x160), n.grid_place = s32(rd32(b, e + 0x164));
        n.pedal_style = b[e + 0x168], n.driver_kind = b[e + 0x169], n.x16a = b[e + 0x16A], n.asm_on = b[e + 0x16B], n.tcs_on = b[e + 0x16C];
        r.entries.push_back(n);
    }
    const u32 n = rd32(b, kHeader);
    size_t at = kHeader + 4;
    const ByteSpan bs(b);
    for (u32 i = 0; i < std::max<u32>(n, 1); ++i) {
        if (n != 0) at += kRecorderHeader;
        if (at > b.size()) return fail("a recorder past the body's end");
        size_t used = 0;
        std::optional<ControlStream> s = read_stream(bs.subspan(at), bases, used);
        if (!s) return fail("bad control stream");
        r.streams.push_back(std::move(*s));
        at += used;
    }
    return r;
}

std::string run_path(const std::string& label) { return "replay/JP/" + label + "/" + label; }

} // namespace gt4r::replay::prologue
