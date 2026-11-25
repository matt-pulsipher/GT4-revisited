// Gran Turismo 4 Prologue's pass codes (prologue_passcode.h). SAFER SK-128 (exponent 45 modulo 257) as the program has
// it: its key schedule (0x1A3618), its block (0x1A3898), the hash (0x1A2C40 .. 0x1A2EA8), the packing (0x1A4660) and the
// digits (0x1A48B0). Checked against the program's 0x1A31A0 in the interpreter (gt4r_diff <prologue disc> prologue-passcode).

#include "gt4r/menu/prologue_passcode.h"

#include "gt4r/save/codec.h"

#include <cstring>
#include <vector>

namespace gt4r::menu {

namespace {

constexpr u32 kKeyA = 0x55CAD0, kKeyB = 0x55CAD8; // (0x1A2FA8's key)
constexpr int kCipherRounds = 13, kHashRounds = 10, kMaxRounds = 13;
constexpr size_t kScheduleBytes = 1 + 16 * kMaxRounds + 8; // 0xD9

// The exponent and logarithm tables (0x1A3340): exp[i] = 45^i mod 257 (256 as 0), log[exp[i]] = i.
struct Tables {
    std::array<u8, 256> exp{}, log{};
    Tables() {
        u32 v = 1;
        for (u32 i = 0; i < 256; ++i) {
            exp[i] = u8(v), log[u8(v)] = u8(i);
            v = (v * 45) % 257;
        }
    }
};
const Tables& tables() {
    static const Tables t;
    return t;
}

u8 rotl(u8 v, int n) { return u8(v << n | v >> (8 - n)); }

using Schedule = std::array<u8, kScheduleBytes>;

// 0x1A3618: the strengthened key schedule of two 8-byte halves.
Schedule schedule(const u8* a, const u8* b, int rounds) {
    const Tables& t = tables();
    Schedule k{};
    if (rounds > kMaxRounds) rounds = kMaxRounds;
    std::array<u8, 9> ka{}, kb{};
    k[0] = u8(rounds);
    size_t at = 1;
    for (int j = 0; j < 8; ++j) {
        ka[j] = rotl(a[j], 5), ka[8] ^= ka[j];
        kb[j] = b[j], kb[8] ^= kb[j];
        k[at++] = b[j];
    }
    for (int i = 1; i <= rounds; ++i) {
        for (int j = 0; j < 9; ++j) ka[j] = rotl(ka[j], 6), kb[j] = rotl(kb[j], 6);
        for (int j = 0; j < 8; ++j) k[at++] = u8(t.exp[t.exp[u8(18 * i + j + 1)]] + ka[(j + 2 * i - 1) % 9]);
        for (int j = 0; j < 8; ++j) k[at++] = u8(t.exp[t.exp[u8(18 * i + j + 10)]] + kb[(j + 2 * i) % 9]);
    }
    return k;
}

// 0x1A3898: one 8-byte block.
void encrypt(u8* out, const u8* in, const Schedule& key) {
    const Tables& t = tables();
    u8 a = in[0], b = in[1], c = in[2], d = in[3], e = in[4], f = in[5], g = in[6], h = in[7];
    int rounds = key[0];
    if (rounds > kMaxRounds) rounds = kMaxRounds;
    const u8* k = key.data() + 1;
    auto pht = [](u8& x, u8& y) { y = u8(y + x), x = u8(x + y); };
    for (int r = 0; r < rounds; ++r, k += 16) {
        a ^= k[0], b = u8(b + k[1]), c = u8(c + k[2]), d ^= k[3], e ^= k[4], f = u8(f + k[5]), g = u8(g + k[6]), h ^= k[7];
        a = u8(t.exp[a] + k[8]), b = u8(t.log[b] ^ k[9]), c = u8(t.log[c] ^ k[10]), d = u8(t.exp[d] + k[11]);
        e = u8(t.exp[e] + k[12]), f = u8(t.log[f] ^ k[13]), g = u8(t.log[g] ^ k[14]), h = u8(t.exp[h] + k[15]);
        pht(a, b), pht(c, d), pht(e, f), pht(g, h);
        pht(a, c), pht(e, g), pht(b, d), pht(f, h);
        pht(a, e), pht(b, f), pht(c, g), pht(d, h);
        u8 x = b;
        b = e, e = c, c = x;
        x = d, d = f, f = g, g = x;
    }
    out[0] = a ^ k[0], out[1] = u8(b + k[1]), out[2] = u8(c + k[2]), out[3] = d ^ k[3];
    out[4] = e ^ k[4], out[5] = u8(f + k[5]), out[6] = u8(g + k[6]), out[7] = h ^ k[7];
}

// 0x1A2C40: the hash's step on an 8-byte block m, its 16 bytes of state h.
void compress(std::array<u8, 16>& h, const u8* m) {
    const Schedule k1 = schedule(h.data() + 8, m, kHashRounds);
    u8 t[8];
    encrypt(t, h.data(), k1);
    const Schedule k2 = schedule(m, h.data(), kHashRounds);
    for (int i = 0; i < 8; ++i) h[size_t(i)] ^= t[i], t[i] = u8(~h[size_t(8 + i)]);
    encrypt(t, t, k2);
    for (int i = 0; i < 8; ++i) h[size_t(8 + i)] ^= t[i];
}

// 0x1A2EA8: the 32-bit hash of the seed word (and no text): the state from zero, the word's 4 bytes and 4 zeros as one
// block, the 16 bytes folded 4 apart.
u32 hash32(u32 seed) {
    std::array<u8, 16> h{};
    const u8 block[8] = {u8(seed), u8(seed >> 8), u8(seed >> 16), u8(seed >> 24), 0, 0, 0, 0};
    compress(h, block);
    u32 v = 0;
    for (int k = 0; k < 4; ++k) v |= u32(h[size_t(k)] ^ h[size_t(4 + k)] ^ h[size_t(8 + k)] ^ h[size_t(12 + k)]) << (8 * k);
    return v;
}

// 0x1A4660: `bits` bits of v at bit `at`, low bit first.
void put_bits(u8* buf, u32 at, u32 v, u32 bits) {
    if (bits < 32) v &= (1u << bits) - 1;
    for (u32 k = 0; k < bits; ++k, ++at) {
        const u8 m = u8(1u << (at & 7));
        if ((v >> k) & 1) buf[at >> 3] |= m;
        else buf[at >> 3] &= u8(~m);
    }
}

} // namespace

PassCoder::PassCoder(const std::array<u8, 8>& key_a, const std::array<u8, 8>& key_b) : key_a_(key_a), key_b_(key_b) {}

std::optional<PassCoder> PassCoder::from_program(ByteSpan image, u32 load) {
    if (kKeyA < load || kKeyB + 8 - load > image.size()) return std::nullopt;
    std::array<u8, 8> a, b;
    std::memcpy(a.data(), image.data() + (kKeyA - load), 8);
    std::memcpy(b.data(), image.data() + (kKeyB - load), 8);
    return PassCoder(a, b);
}

std::string PassCoder::encode(const PassFields& f, u32 seed) const {
    // 0x1A31A0: the fields packed into 80 bits.
    u8 packed[10] = {};
    const u32 values[10] = {f.kind, f.car, f.course, f.flag, f.test, f.word14, f.word18, f.time, f.word20, f.word24};
    const u32 widths[10] = {5, 9, 8, 1, 12, 12, 3, 22, 4, 4};
    u32 at = 0;
    for (int k = 0; k < 10; ++k) put_bits(packed, at, values[k], widths[k]), at += widths[k];
    // 0x1A2FA8: after two zero bytes; the hash's nibble, the CRC's fold; enciphered at each offset.
    u8 b[12] = {};
    std::memcpy(b + 2, packed, 10);
    u32 x = hash32(seed);
    x ^= x >> 16, x ^= x >> 8, x ^= x >> 4;
    put_bits(b, 0, x & 0xF, 4);
    const u32 crc = save::crc32(ByteSpan(b, 12));
    put_bits(b, 4, (crc ^ (crc >> 12) ^ (crc >> 24)) & 0xFFF, 12);
    const Schedule key = schedule(key_a_.data(), key_b_.data(), kCipherRounds);
    for (size_t o = 0; o + 8 <= 12; ++o) encrypt(b + o, b + o, key);
    // 0x1A48B0: the little-endian number's digits, least first, padded with '0' to 30 and turned round.
    std::vector<u8> n(b, b + 12);
    std::string digits;
    auto zero = [&n] {
        for (u8 v : n)
            if (v) return false;
        return true;
    };
    while (!zero()) {
        u32 rem = 0;
        for (size_t i = n.size(); i-- > 0;) {
            const u32 cur = rem << 8 | n[i];
            n[i] = u8(cur / 10), rem = cur % 10;
        }
        digits.push_back(char('0' + rem));
    }
    constexpr size_t kDigits = 30;
    if (digits.size() < kDigits) digits.append(kDigits - digits.size(), '0');
    std::string out(digits.rbegin(), digits.rend());
    // The check digit over the rest.
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 1; i < out.size(); ++i) c = c * 3 + u32(s32(gt3r::s8(out[i])));
    out[0] = char('0' + c % 10);
    return out;
}

} // namespace gt4r::menu
