#include "gt4r/formats/replay/demonstration.h"

#include "base/ee_float.h"
#include "gt4r/save/codec.h"
#include "gt4r/sim/program.h"

#include <algorithm>
#include <cstring>

namespace gt4r::replay {

namespace {

// The cipher's tables in the program: a byte permutation (the bits of each byte reversed) and 256 factors.
constexpr u32 kCipherPerm = 0x655C90, kCipherFactors = 0x655888;
// The delta's bases (0x620218).
constexpr u32 kDeltaBases = 0x620218;
// The race header's size (RaceBase's virtual 27), its entries' place, size and room.
constexpr size_t kHeader = 0x1AA4, kEntries = 0x960, kEntrySize = 0x228, kMaxEntries = 8;
// A recorder's own header in a several-recorder save (0x346CA0: 0x14E0 + 0x14 bytes).
constexpr size_t kRecorderHeader = 0x14F4;

// The recorder's bit reader (PDISTD::BitStreamBuffer 0x57AD58): most significant bit first, zeros past the end.
class BitReader {
public:
    explicit BitReader(ByteSpan d) : d_(d) {}
    u32 read(int n) {
        u32 v = 0;
        for (int i = 0; i < n; ++i) {
            const size_t byte = pos_ >> 3;
            const u32 bit = byte < d_.size() ? (d_[byte] >> (7 - (pos_ & 7))) & 1 : 0;
            v = (v << 1) | bit;
            ++pos_;
        }
        return v;
    }
    // 0x345C90: k one bits, a zero, k bits: 2^k + those bits.
    u32 gamma() {
        int k = 0;
        while (read(1) && k < 31) ++k;
        return (1u << k) + read(k);
    }
    // 0x345CF0: a 3-bit class k, a sign, 2k + 1 bits added to the class's base.
    s32 delta(const std::array<s32, 8>& base) {
        const u32 k = read(3);
        const bool neg = read(1) != 0;
        const s32 v = base[k] + s32(read(int(2 * k + 1)));
        return neg ? -v : v;
    }
    size_t bits() const { return pos_; }

private:
    ByteSpan d_;
    size_t pos_ = 0;
};

void add8(PadRecord& r, size_t o, s32 v) { r.bytes[o] = u8(r.bytes[o] + v); }
void add16(PadRecord& r, size_t o, s32 v) {
    const u16 x = u16(u16(r.bytes[o] | (r.bytes[o + 1] << 8)) + v);
    r.bytes[o] = u8(x), r.bytes[o + 1] = u8(x >> 8);
}

// The recorder's bit writer (0x57ADE0): most significant bit first; a byte is kept once it is whole.
class BitWriter {
public:
    void write(u32 v, int n) {
        for (int i = n - 1; i >= 0; --i) {
            byte_ = u8(byte_ << 1 | ((v >> i) & 1));
            if (++have_ == 8) out_.push_back(byte_), byte_ = 0, have_ = 0;
        }
    }
    // 0x345FD0: k one bits, a zero, the rest in k bits.
    void gamma(u32 v) {
        u32 rest = v ? v - 1 : 0;
        int k = 0;
        while (rest >= (1u << k)) rest -= 1u << k, ++k;
        for (int i = 0; i < k; ++i) write(1, 1);
        write(0, 1);
        write(rest, k);
    }
    // 0x346080: a class of 3 bits, a sign, 2k + 1 bits.
    void delta(s32 d) {
        const u32 neg = d < 0;
        u32 v = u32(neg ? -d : d);
        int bits = 1;
        while (v >= (1u << bits)) v -= 1u << bits, bits += 2;
        write(u32(bits >> 1), 3), write(neg, 1), write(v, bits);
    }
    // The whole bytes (a last part one is not in the file: its bits are the run's closing code, which no playback reads).
    const Bytes& bytes() const { return out_; }

private:
    Bytes out_;
    u8 byte_ = 0;
    int have_ = 0;
};

// One recorder's stream at `d` (u32 T, u32 the length with these 8 bytes, the bits) played as 0x345D70 plays it.
std::optional<ControlStream> read_stream(ByteSpan d, const std::array<s32, 8>& base, size_t* used) {
    if (d.size() < 8) return std::nullopt;
    ControlStream s;
    s.frame_count = rd32(d, 0);
    const u32 len = rd32(d, 4);
    if (len < 8 || len > d.size() || s.frame_count > 0x100000) return std::nullopt;
    *used = (size_t(len) + 3) & ~size_t(3);
    BitReader bits(d.subspan(8, len - 8));
    PadRecord rec;
    s32 prev = 0; // the steering before (recorder +0x14)
    u32 run = 0;
    s.frames.reserve(s.frame_count);
    for (u32 f = 1; f <= s.frame_count; ++f) {
        // 0x345C38: the steering carried on as 2x - the one before, within +-4096, while |x| >= 16.
        const s16 x = rec.steer();
        const s32 next = 2 * s32(x) - prev;
        prev = x;
        if (std::abs(s32(x)) >= 16) {
            const s16 c = s16(std::clamp(next, -0x1000, 0x1000));
            rec.bytes[4] = u8(c), rec.bytes[5] = u8(u16(c) >> 8);
        }
        if (run) {
            --run;
        } else {
            const u32 mask = bits.gamma() - 1;
            if (mask & 0x100) add8(rec, 0, bits.delta(base));
            if (mask & 0x080) add8(rec, 1, bits.delta(base));
            if (mask & 0x010) add8(rec, 2, bits.delta(base));
            if (mask & 0x002) add16(rec, 4, bits.delta(base));
            if (mask & 0x001) add8(rec, 7, bits.delta(base));
            if (mask & 0x004) add16(rec, 8, bits.delta(base));
            if (mask & 0x008) add16(rec, 0xA, bits.delta(base));
            if (mask & 0x040) add16(rec, 0xC, bits.delta(base));
            if (mask & 0x020) add16(rec, 0xE, bits.delta(base));
            if (mask & 0x200) {
                const u32 n = bits.gamma() - 1;
                rec.bytes[3] = u8(n);
                for (u32 i = 0; i < n && 0x10 + i < rec.bytes.size(); ++i) add8(rec, 0x10 + i, bits.delta(base));
            }
            run = bits.gamma() - 1;
        }
        if (f < s.frame_count) s.frames.push_back(rec);
        else if (bits.bits() <= size_t(len - 8) * 8) s.last = rec; // (its bits may be in the part byte the file leaves out)
    }
    return s;
}

} // namespace

Bytes encode_stream(const std::vector<PadRecord>& frames, const std::optional<PadRecord>& last) {
    BitWriter w;
    PadRecord state;
    s32 prev = 0;
    u32 count = 0, run = 0;
    auto step = [&](const PadRecord& r) {
        const s16 x = state.steer();
        const s32 next = 2 * s32(x) - prev;
        prev = x;
        if (std::abs(s32(x)) >= 16) {
            const s16 c = s16(std::clamp(next, -0x1000, 0x1000));
            state.bytes[4] = u8(c), state.bytes[5] = u8(u16(c) >> 8);
        }
        const std::array<u8, 0x20>&a = state.bytes, &b = r.bytes;
        auto h = [](const std::array<u8, 0x20>& v, size_t o) { return s32(s16(v[o] | v[o + 1] << 8)); };
        u32 mask = 0;
        if (a[0] != b[0]) mask |= 0x100;
        if (a[1] != b[1]) mask |= 0x80;
        if (a[2] != b[2]) mask |= 0x10;
        if (h(a, 4) != h(b, 4)) mask |= 2;
        if (a[7] != b[7]) mask |= 1;
        if (h(a, 8) != h(b, 8)) mask |= 4;
        if (h(a, 0xA) != h(b, 0xA)) mask |= 8;
        if (h(a, 0xC) != h(b, 0xC)) mask |= 0x40;
        if (h(a, 0xE) != h(b, 0xE)) mask |= 0x20;
        if (a[3] != b[3]) mask |= 0x200;
        else
            for (size_t i = 0; i < b[3] && 0x10 + i < b.size(); ++i)
                if (a[0x10 + i] != b[0x10 + i]) mask |= 0x200;
        ++count;
        if (count == 1 || mask != 0) {
            if (run != 0) w.gamma(run);
            w.gamma(mask + 1);
            if (mask & 0x100) w.delta(s32(b[0]) - s32(a[0]));
            if (mask & 0x080) w.delta(s32(b[1]) - s32(a[1]));
            if (mask & 0x010) w.delta(s32(s8(b[2])) - s32(s8(a[2])));
            if (mask & 0x002) w.delta(h(b, 4) - h(a, 4));
            if (mask & 0x001) w.delta(s32(b[7]) - s32(a[7]));
            if (mask & 0x004) w.delta(h(b, 8) - h(a, 8));
            if (mask & 0x008) w.delta(h(b, 0xA) - h(a, 0xA));
            if (mask & 0x040) w.delta(h(b, 0xC) - h(a, 0xC));
            if (mask & 0x020) w.delta(h(b, 0xE) - h(a, 0xE));
            if (mask & 0x200) {
                w.gamma(u32(b[3]) + 1);
                for (size_t i = 0; i < b[3] && 0x10 + i < b.size(); ++i) w.delta(s32(b[0x10 + i]) - s32(a[0x10 + i]));
            }
            run = 0;
        }
        ++run;
        state = r;
    };
    for (const PadRecord& r : frames) step(r);
    if (last) step(*last);
    else if (!frames.empty()) step(frames.back());
    if (count != 0) w.gamma(run), w.gamma(1);
    const u32 len = u32(w.bytes().size()) + 8;
    Bytes out(8);
    for (int k = 0; k < 4; ++k) out[size_t(k)] = u8(count >> (8 * k)), out[size_t(4 + k)] = u8(len >> (8 * k));
    out.insert(out.end(), w.bytes().begin(), w.bytes().end());
    out.resize(size_t(len + 3) & ~size_t(3));
    return out;
}

Bytes encipher_body(ByteSpan plain, u32& key, const sim::Program& program) {
    key = (save::crc32(plain) & 0xFFFFFFu) | (plain.empty() ? 0u : u32(plain[0]) << 24);
    Bytes out(plain.size());
    u32 a = (key & 0xFFFFFFu) | 0x3F000000u;
    for (size_t i = 0; i < plain.size(); ++i) {
        const u8 c = u8(plain[i] ^ (a >> 8) ^ (a ^ (a >> 16)));
        out[i] = c;
        const u32 g = 0x3F000000u | (u32(program.b(kCipherPerm + (a & 0xFF))) << 16) | (u32(program.b(kCipherPerm + ((a >> 8) & 0xFF))) << 8) | program.b(kCipherPerm + ((a >> 16) & 0xFF));
        a = gt3r::ee::div(gt3r::ee::mul(program.w(kCipherFactors + 4 * u32(c)), a), g);
    }
    return out;
}

Bytes Demonstration::encode(const sim::Program& program) const {
    Bytes b = header;
    b.resize(kHeader);
    auto w32 = [](Bytes& to, size_t at, u32 v) {
        for (int k = 0; k < 4; ++k) to[at + size_t(k)] = u8(v >> (8 * k));
    };
    std::memcpy(&b[4], "GT4M", 4);
    w32(b, 8, version ? version : 0x40000), w32(b, 0xC, race_mode);
    std::fill(b.begin() + 0x10, b.begin() + 0x50, u8(0));
    std::copy_n(race_class.begin(), std::min<size_t>(race_class.size(), 0x3F), b.begin() + 0x10);
    std::copy(information.begin(), information.end(), b.begin() + 0x50);
    w32(b, 0xB0, u32(code_b0)), w32(b, 0xB4, u32(code_b0 >> 32)), w32(b, 0xB8, u32(code_b8)), w32(b, 0xBC, u32(code_b8 >> 32));
    w32(b, 0xC0, u32(time_ms)), w32(b, 0xC4, u32(time2_ms));
    w32(b, 0x958, u32(entries.size()));
    for (size_t i = 0; i < entries.size() && i < kMaxEntries; ++i) {
        const DemoEntry& en = entries[i];
        const size_t e = kEntries + kEntrySize * i;
        std::copy(en.equipment.bytes.begin(), en.equipment.bytes.end(), b.begin() + std::ptrdiff_t(e));
        std::copy(en.condition.begin(), en.condition.end(), b.begin() + std::ptrdiff_t(e + 0x178));
        w32(b, e + 0x198, en.x198), w32(b, e + 0x19C, u32(en.grid_place));
        b[e + 0x1A0] = en.pedal_style, b[e + 0x1A1] = en.driver_kind;
        std::copy(en.aids.begin(), en.aids.end(), b.begin() + std::ptrdiff_t(e + 0x1A2));
    }
    w32(b, 0x1AA0, 1);
    const bool several = !streams.empty() && !streams[0].state.empty();
    b.resize(kHeader + 4);
    w32(b, kHeader, several ? u32(streams.size()) : 0);
    for (const ControlStream& s : streams) {
        if (several) b.insert(b.end(), s.state.begin(), s.state.end());
        const Bytes bits = encode_stream(s.frames, s.last);
        b.insert(b.end(), bits.begin(), bits.end());
    }
    w32(b, 0, u32(b.size()));
    u32 k = 0;
    const Bytes cipher = encipher_body(b, k, program);
    Bytes out(0x40);
    w32(out, 0, 0x40), w32(out, 4, u32(b.size())), out[0xA] = 4, w32(out, 0xC, k);
    std::memcpy(&out[0x10], "Base", 4);
    out.insert(out.end(), cipher.begin(), cipher.end());
    return out;
}

std::optional<Bytes> decipher_body(ByteSpan cipher, u32 key, const sim::Program& program) {
    if (!program.known()) return std::nullopt;
    std::array<u8, 256> perm{};
    std::array<u32, 256> factor{};
    for (u32 i = 0; i < 256; ++i) perm[i] = program.b(kCipherPerm + i), factor[i] = program.w(kCipherFactors + 4 * i);
    Bytes out(cipher.size());
    // The state: a float of the console's, its bits (key & 0xFFFFFF) | 0x3F000000 at first; each byte is the cipher's
    // byte XOR its bits 8..15 XOR (bits 0..7 XOR bits 16..23); then the state x the factor of the cipher's byte, over the
    // float made of 0x3F and the permuted bytes of the state (EE multiply and divide).
    u32 a = (key & 0xFFFFFFu) | 0x3F000000u;
    for (size_t i = 0; i < cipher.size(); ++i) {
        const u8 c = cipher[i];
        out[i] = u8(c ^ (a >> 8) ^ (a ^ (a >> 16)));
        const u32 g = 0x3F000000u | (u32(perm[a & 0xFF]) << 16) | (u32(perm[(a >> 8) & 0xFF]) << 8) | perm[(a >> 16) & 0xFF];
        a = gt3r::ee::div(gt3r::ee::mul(factor[c], a), g);
    }
    const u32 crc = save::crc32(out);
    const u32 check = (crc & 0xFFFFFFu) | (out.empty() ? 0u : u32(out[0]) << 24);
    if (!out.empty() && check != key) return std::nullopt;
    return out;
}

std::optional<Demonstration> Demonstration::parse(ByteSpan file, const sim::Program& program, std::string* error) {
    auto fail = [&](const char* m) -> std::optional<Demonstration> {
        if (error) *error = m;
        return std::nullopt;
    };
    if (file.size() < 0x40) return fail("too small");
    const u32 hdr = rd32(file, 0), len = rd32(file, 4);
    if (hdr < 0x10 || hdr > file.size() || len > file.size() - hdr) return fail("bad Serialize header");
    if (rd16(file, 0xA) != 4) return fail("not the Serialize layout of version 4");
    if (!program.known()) return fail("the cipher's tables need the mapped program");
    Demonstration d;
    d.key = rd32(file, 0xC);
    const std::optional<Bytes> body = decipher_body(file.subspan(hdr, len), d.key, program);
    if (!body) return fail("the body does not decipher to its key");
    const Bytes& b = *body;
    if (b.size() < kHeader + 4 || std::memcmp(&b[4], "GT4M", 4) != 0) return fail("not a race header (GT4M)");
    d.version = rd32(b, 8);
    d.header.assign(b.begin(), b.begin() + std::ptrdiff_t(kHeader));
    d.race_mode = rd32(b, 0xC);
    d.race_class.assign(reinterpret_cast<const char*>(&b[0x10]), strnlen(reinterpret_cast<const char*>(&b[0x10]), 0x40));
    std::memcpy(d.information.data(), &b[0x50], d.information.size());
    d.code_b0 = u64(rd32(b, 0xB0)) | (u64(rd32(b, 0xB4)) << 32);
    d.code_b8 = u64(rd32(b, 0xB8)) | (u64(rd32(b, 0xBC)) << 32);
    d.time_ms = s32(rd32(b, 0xC0));
    d.time2_ms = s32(rd32(b, 0xC4));
    const u32 count = rd32(b, 0x958);
    if (count > kMaxEntries) return fail("bad entry count");
    for (u32 i = 0; i < count; ++i) {
        const size_t e = kEntries + kEntrySize * i;
        DemoEntry en;
        std::memcpy(en.equipment.bytes.data(), &b[e], sim::Equipment::kSize);
        std::memcpy(en.condition.data(), &b[e + 0x178], en.condition.size());
        en.x198 = rd32(b, e + 0x198);
        en.grid_place = s32(rd32(b, e + 0x19C));
        en.pedal_style = b[e + 0x1A0], en.driver_kind = b[e + 0x1A1];
        std::memcpy(en.aids.data(), &b[e + 0x1A2], 4);
        d.entries.push_back(en);
    }
    d.controls_flag = rd32(b, 0x1AA0);
    std::array<s32, 8> base{};
    for (u32 k = 0; k < 8; ++k) base[k] = s32(program.w(kDeltaBases + 4 * k));
    // The controls (0x346BB8): u32 n; 0: one stream (recorder 0); else n recorders, each its own header then its stream.
    const u32 n = rd32(b, kHeader);
    size_t at = kHeader + 4;
    const ByteSpan bs(b);
    for (u32 i = 0; i < std::max<u32>(n, 1); ++i) {
        const size_t header = at;
        if (n != 0) at += kRecorderHeader;
        if (at > b.size()) return fail("a recorder past the body's end");
        size_t used = 0;
        std::optional<ControlStream> s = read_stream(bs.subspan(at), base, &used);
        if (!s) return fail("bad control stream");
        if (n != 0) s->state.assign(b.begin() + std::ptrdiff_t(header), b.begin() + std::ptrdiff_t(at));
        d.streams.push_back(std::move(*s));
        at += used;
    }
    return d;
}

std::optional<RecorderState> recorder_state(const ControlStream& s) {
    if (s.state.size() < kRecorderHeader) return std::nullopt;
    RecorderState r;
    r.units = s.state[2], r.remainder = s.state[3];
    r.frame = rd32(s.state, 4), r.share = rd32(s.state, 0xC);
    r.car = std::span<const u8>(s.state).subspan(0x54, 0x10A0);
    r.body = std::span<const u8>(s.state).subspan(0x10F4, 0x400);
    return r;
}

std::string demonstration_path(const std::string& event_label) { return "replay/license/" + event_label; }

} // namespace gt4r::replay
