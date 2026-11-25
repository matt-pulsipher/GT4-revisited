#pragma once

// How Gran Turismo 4 scrambles its saved game's files on a memory card (docs/formats/save.md
// "Scrambling"): the game data file (an 8-byte head, a body enciphered with a Mersenne Twister
// and a small generator, two 4-byte swaps), the garage file's header (byte masks and a bit
// shuffle) and its car slots. No compression, no library cipher.

#include "gt4r/base/types.h"

#include <array>
#include <optional>

namespace gt4r::save {

// The standard reflected CRC-32 (polynomial 0xEDB88320, start and end inverted).
u32 crc32(ByteSpan data);

// MT19937, 32 bit, seeded as the standard init_genrand.
class Mt19937 {
public:
    explicit Mt19937(u32 seed);
    u32 next();
    // The draw as the program's float in [0, 1) times n, whole: the top 24 significant bits
    // of the draw over 2^32 (single precision, toward zero), then truncated.
    u32 below(u32 n);

private:
    std::array<u32, 624> mt_{};
    u32 at_ = 624;
};

// The program's small generator: s = s * 17 + 17; it gives s with its halves swapped, XORed
// with s.
struct Lcg {
    u32 s;
    explicit Lcg(u32 seed) : s(seed) {}
    u32 next() {
        s = s * 17 + 17;
        return s ^ ((s << 16) | (s >> 16));
    }
};

// A data file's size for a payload of n bytes: 8 bytes of head, rounded up to 64.
constexpr size_t scrambled_size(size_t n) { return (n + 0x47) & ~size_t(0x3F); }
// The file of a payload (the game data: key 0). `random`: the head's first word (any value).
enum class FileKind { GameData, Replay }; // (which the two 4-byte swaps' places follow)
Bytes scramble(ByteSpan payload, u32 key, u32 random, FileKind kind = FileKind::GameData);
// The payload back (with the zero padding up to the file's end); none when the CRC fails.
std::optional<Bytes> unscramble(ByteSpan file, u32 key, FileKind kind = FileKind::GameData);

// The bits of `data` shuffled / put back by a generator seeded with `seed` (the garage file's
// header and slots).
void shuffle_bits(u8* data, size_t bytes, u32 seed);
void unshuffle_bits(u8* data, size_t bytes, u32 seed);

// The garage file's 64-byte header, in place.
void encode_header(u8* h, u32 key);
void decode_header(u8* h, u32 key);

// A garage file slot (0x500 bytes) holding a car's record (0x4B8 bytes; 0x4C0 are checked).
constexpr size_t kSlotSize = 0x500, kSlotChecked = 0x4C0;
Bytes encode_slot(ByteSpan record, u32 key, u32 index, u32 stamp);
std::optional<Bytes> decode_slot(ByteSpan slot, u32 key, u32 index); // kSlotChecked bytes

// A new garage file's bytes after the header: noise from the key (no slot holds a car).
void fill_new_garage(u8* body, size_t bytes, u32 key);

} // namespace gt4r::save
