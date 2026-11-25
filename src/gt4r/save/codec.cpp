#include "gt4r/save/codec.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace gt4r::save {

namespace {

u32 get32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
void put32(u8* p, u32 v) { p[0] = u8(v), p[1] = u8(v >> 8), p[2] = u8(v >> 16), p[3] = u8(v >> 24); }

// The two places a file's head words are swapped to: the body's length times these single
// precision constants (0.135790..., 0.654860...), whole.
// The two fractions (float bits) a kind of file's head swaps take: the game data's (0x1CDAC8), the replays' (0x1CFE68, 0x1CFFF8).
// (A photo's 0.6809 and 0.404, 0x1D0B28; a film's 0.654321 and 0.345678, 0x1D0698; a photo's or a replay's record's 0.314159 and
// 0.468643, 0x1CF400.)
u32 swap_head(FileKind k) {
    switch (k) {
    case FileKind::Replay: return 0x3E6B3EF8u;
    case FileKind::Photo: return 0x3F2E4F76u;
    case FileKind::Film: return 0x3F278194u;
    case FileKind::Info: return 0x3EA0D979u;
    default: return 0x3E0B0C88u;
    }
}
u32 swap_random(FileKind k) {
    switch (k) {
    case FileKind::Replay: return 0x3F322311u;
    case FileKind::Photo: return 0x3ECED916u;
    case FileKind::Film: return 0x3EB0FCB4u;
    case FileKind::Info: return 0x3EEFF200u;
    default: return 0x3F27A4E7u;
    }
}
bool shuffled(FileKind k) { return k == FileKind::Film || k == FileKind::Info; }
// The bytes of a run shuffled (0x1D4BC8: the bit shuffle's order, over bytes) or put back (0x1D4C18).
void shuffle_bytes(u8* data, size_t n, u32 seed, bool back) {
    Mt19937 mt(seed);
    std::vector<std::pair<size_t, u32>> list;
    for (size_t i = n; i-- > 1;) list.push_back({i, mt.below(u32(i + 1))});
    if (back) std::reverse(list.begin(), list.end());
    for (const auto& [i, j] : list) std::swap(data[i], data[j]);
}
u32 times_constant(u32 n, u32 bits) {
    const u64 mantissa = (bits & 0x7FFFFF) | 0x800000;
    const int exponent = int((bits >> 23) & 0xFF) - 150;
    const u64 p = u64(n) * mantissa;
    return u32(exponent >= 0 ? p << exponent : p >> -exponent);
}

void swap4(u8* buf, size_t a, size_t b) {
    u8 t[4];
    std::memcpy(t, buf + a, 4), std::memcpy(buf + a, buf + b, 4), std::memcpy(buf + b, t, 4);
}

// Each 32-bit word: w' = (w ^ mt) - lcg; back: w = (w' + lcg) ^ mt (the twister drawn first).
void cipher(u8* p, size_t n, Mt19937 mt, Lcg lcg, bool encipher) {
    for (size_t o = 0; o + 4 <= n; o += 4) {
        const u32 a = mt.next(), b = lcg.next(), w = get32(p + o);
        put32(p + o, encipher ? (w ^ a) - b : (w + b) ^ a);
    }
}

std::vector<std::pair<u32, u32>> swaps(u32 n, u32 seed) {
    Mt19937 mt(seed);
    std::vector<std::pair<u32, u32>> out;
    if (n < 2) return out;
    out.reserve(n - 1);
    for (u32 i = n - 1; i >= 1; --i) out.emplace_back(i, mt.below(i + 1));
    return out;
}

void swap_bits(u8* d, u32 i, u32 j) {
    if (i == j) return;
    const int bi = (d[i >> 3] >> (i & 7)) & 1, bj = (d[j >> 3] >> (j & 7)) & 1;
    if (bi != bj) d[i >> 3] ^= u8(1 << (i & 7)), d[j >> 3] ^= u8(1 << (j & 7));
}

} // namespace

u32 crc32(ByteSpan data) {
    static const std::array<u32, 256> table = [] {
        std::array<u32, 256> t{};
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    u32 c = 0xFFFFFFFFu;
    for (u8 b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return ~c;
}

Mt19937::Mt19937(u32 seed) {
    mt_[0] = seed;
    for (u32 i = 1; i < 624; ++i) mt_[i] = 1812433253u * (mt_[i - 1] ^ (mt_[i - 1] >> 30)) + i;
}

u32 Mt19937::next() {
    if (at_ >= 624) {
        for (u32 k = 0; k < 624; ++k) {
            const u32 y = (mt_[k] & 0x80000000u) | (mt_[(k + 1) % 624] & 0x7FFFFFFFu);
            mt_[k] = mt_[(k + 397) % 624] ^ (y >> 1) ^ (y & 1 ? 0x9908B0DFu : 0u);
        }
        at_ = 0;
    }
    u32 y = mt_[at_++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9D2C5680u;
    y ^= (y << 15) & 0xEFC60000u;
    y ^= y >> 18;
    return y;
}

u32 Mt19937::below(u32 n) {
    u32 v = next();
    if (v == 0) return 0;
    int bits = 32;
    while (!(v >> (bits - 1))) --bits;
    if (bits > 24) v = (v >> (bits - 24)) << (bits - 24);
    return u32((u64(v) * n) >> 32);
}

Bytes scramble(ByteSpan payload, u32 key, u32 random, FileKind kind) {
    const size_t size = scrambled_size(payload.size());
    Bytes buf(size, 0);
    std::memcpy(buf.data() + 8, payload.data(), payload.size());
    const u32 check = crc32(ByteSpan(buf.data() + 8, size - 8)) ^ key;
    put32(buf.data(), random), put32(buf.data() + 4, check);
    cipher(buf.data() + 8, size - 8, Mt19937(check + random), Lcg(check ^ random), true);
    swap4(buf.data(), 4, 4 + times_constant(u32(size - 8), swap_head(kind)));
    if (shuffled(kind)) shuffle_bytes(buf.data() + 4, size - 4, random ^ key, false);
    swap4(buf.data(), 0, times_constant(u32(size - 4), swap_random(kind)));
    return buf;
}

std::optional<Bytes> unscramble(ByteSpan file, u32 key, FileKind kind) {
    const size_t size = file.size();
    if (size < 0x40 || size % 0x40) return std::nullopt;
    Bytes buf(file.begin(), file.end());
    swap4(buf.data(), 0, times_constant(u32(size - 4), swap_random(kind)));
    if (shuffled(kind)) shuffle_bytes(buf.data() + 4, size - 4, get32(buf.data()) ^ key, true);
    swap4(buf.data(), 4, 4 + times_constant(u32(size - 8), swap_head(kind)));
    const u32 random = get32(buf.data()), check = get32(buf.data() + 4);
    cipher(buf.data() + 8, size - 8, Mt19937(check + random), Lcg(check ^ random), false);
    if ((crc32(ByteSpan(buf.data() + 8, size - 8)) ^ key) != check) return std::nullopt;
    return Bytes(buf.begin() + 8, buf.end());
}

void shuffle_bits(u8* data, size_t bytes, u32 seed) {
    for (const auto& [i, j] : swaps(u32(bytes * 8), seed)) swap_bits(data, i, j);
}

void unshuffle_bits(u8* data, size_t bytes, u32 seed) {
    const auto list = swaps(u32(bytes * 8), seed);
    for (auto it = list.rbegin(); it != list.rend(); ++it) swap_bits(data, it->first, it->second);
}

void encode_header(u8* h, u32 key) {
    const u32 stamp = get32(h + 0x3C);
    Mt19937 mt(key + stamp);
    for (int i = 0; i < 0x3C; ++i) h[i] ^= u8(mt.next());
    Lcg lcg(key);
    put32(h + 0x3C, stamp ^ lcg.next());
    shuffle_bits(h, 0x40, lcg.next());
}

void decode_header(u8* h, u32 key) {
    Lcg lcg(key);
    const u32 a = lcg.next(), b = lcg.next();
    unshuffle_bits(h, 0x40, b);
    const u32 stamp = get32(h + 0x3C) ^ a;
    put32(h + 0x3C, stamp);
    Mt19937 mt(key + stamp);
    for (int i = 0; i < 0x3C; ++i) h[i] ^= u8(mt.next());
}

Bytes encode_slot(ByteSpan record, u32 key, u32 index, u32 stamp) {
    Bytes buf(kSlotSize, 0);
    std::memcpy(buf.data() + 8, record.data(), std::min(record.size(), kSlotSize - 8));
    const u32 check = crc32(ByteSpan(buf.data() + 8, kSlotChecked));
    put32(buf.data(), stamp), put32(buf.data() + 4, check);
    cipher(buf.data() + 8, kSlotSize - 8, Mt19937(key ^ stamp), Lcg(check), true);
    shuffle_bits(buf.data(), kSlotSize, key + index);
    return buf;
}

std::optional<Bytes> decode_slot(ByteSpan slot, u32 key, u32 index) {
    if (slot.size() < kSlotSize) return std::nullopt;
    Bytes buf(slot.begin(), slot.begin() + kSlotSize);
    unshuffle_bits(buf.data(), kSlotSize, key + index);
    const u32 stamp = get32(buf.data()), check = get32(buf.data() + 4);
    cipher(buf.data() + 8, kSlotSize - 8, Mt19937(key ^ stamp), Lcg(check), false);
    if (crc32(ByteSpan(buf.data() + 8, kSlotChecked)) != check) return std::nullopt;
    return Bytes(buf.begin() + 8, buf.begin() + 8 + kSlotChecked);
}

void fill_new_garage(u8* body, size_t bytes, u32 key) {
    Lcg lcg(key);
    Mt19937 mt(key);
    for (size_t i = 0; i < bytes; ++i) {
        const u32 a = mt.next();
        body[i] = u8(lcg.next() ^ a);
    }
}

} // namespace gt4r::save
