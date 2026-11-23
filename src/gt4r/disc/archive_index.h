#pragma once

#include "base/reader.h"
#include "gt4r/base/types.h"

#include <optional>
#include <string>
#include <vector>

namespace gt4r {

// The file index of a Gran Turismo 4 volume (GT4.VOL, GT4L1.VOL); docs/FORMATS.md "Volumes".
//
// A volume starts with a table of contents in Gran Turismo 3's layout that the game does not
// use (its entries are leftovers). The index the game reads starts on a later 0x800-byte page:
//
//   0x00 u32 magic 0xACB990AD, u16 1, u16 3
//   0x08 u32 index size in bytes (header, page table, packed pages)
//   0x0C u32 pages the index takes: file data starts at index + pages x 0x800
//   0x10 u16 page size (0x800), u16 packed page count
//   0x40 u32 page ends[count + 1], each XOR 0x14AC327A x (its position + 1): page i is the
//        bytes [ends[i], ends[i + 1]) from the index start
//
// A page is a raw DEFLATE stream of one page size; every byte of the result is XOR 0x55:
//
//   0x00 s16 kind (0: entries; else a search page, not needed to list the files)
//   0x02 s16 item count (two per entry: its key and its value)
//   0x04 s32 next page, s32 previous page
//   from the page's end, backwards, per entry: u16 key offset, u16 key size, u16 value offset,
//   u16 value size
//   key: u32 parent folder id (big endian), then the name
//   value: u8 0 folder: u32 id | 1 file: u32 page, u32 time, u32 size
//          | 2 packed file: u32 page, u32 time, u32 stored size, u32 size
//
// The root folder is "." (id 1, parent 0). A file's data is at index + (index pages + its
// page) x 0x800. A packed file is `C5 EE F7 FF`, s32 minus its size, a raw DEFLATE stream.
struct ArchiveFile {
    std::string path;        // '/' separated, as stored (case kept)
    u64 offset = 0;          // of its stored bytes, from the volume's start
    u32 size = 0;            // unpacked
    u32 stored_size = 0;     // on the disc
    bool packed = false;
    u32 modified = 0;        // seconds since 1970
};

struct ArchiveIndex {
    static constexpr u32 kMagic = 0xACB990ADu;
    static constexpr u32 kPage = 0x800;
    static constexpr u32 kPackedMagic = 0xFFF7EEC5u;

    u64 offset = 0;     // of the index, from the volume's start
    u32 size = 0;       // its bytes (what a disc's fingerprint hashes)
    u64 data_offset = 0; // of page 0 of the file data
    size_t folders = 0;
    std::vector<ArchiveFile> files; // in index order

    // Where the index starts in the volume at `base` of `file` (`length` bytes), when it has one.
    static std::optional<u64> find(gt3r::Reader& file, u64 base, u64 length);
    // Throws FormatError.
    static ArchiveIndex read(gt3r::Reader& file, u64 base, u64 length);
};

// The files of a volume whose front table is the one its game reads (Gran Turismo 4 Prologue):
// Gran Turismo 3's table of contents (the shared engine's disc/volume.h has the layout), where a
// file flagged as packed is packed as above instead of being a gzip member. Throws FormatError.
// `in_range`: false lists entries that reach past the volume too (a table the game does not read).
std::vector<ArchiveFile> read_front_table(gt3r::Reader& file, u64 base, u64 length, bool in_range = true);
// The size of that table in bytes (0 when the volume does not start with one).
u32 front_table_size(gt3r::Reader& file, u64 base, u64 length);

// A packed file's contents (`size` bytes). Throws FormatError.
Bytes unpack_file(ByteSpan stored, size_t size);

} // namespace gt4r
