#include "gt4r/menu/image_pack.h"

#include "gt4r/disc/archive_index.h"

#include <algorithm>

namespace gt4r::menu {

namespace {
constexpr u32 kMaxEntries = 0x4000;
}

bool ImagePack::is_pack(ByteSpan d) {
    return d.size() >= 8 && d[0] == 'g' && d[1] == 'p' && d[2] == 'b' && d[3] >= '0' && d[3] <= '2';
}

// gpb0: u32 files at +4, then {u32 name, u32 data} each.
// gpb1: u32 files at +0xC, the same pairs from +0x10.
// gpb2: u32 files at +0xC, then {u32 name, u32 data, u32 bytes, u32 0} each.
// Names are NUL-ended; a file ends where the next begins (they are in the table's order).
ImagePack ImagePack::parse(ByteSpan d) {
    if (!is_pack(d)) throw FormatError("not a gpb file");
    ImagePack p;
    p.file_ = d;
    p.version_ = d[3] - '0';
    const size_t count_at = p.version_ == 0 ? 4 : 0xC, table = p.version_ == 0 ? 8 : 0x10, row = p.version_ == 2 ? 16 : 8;
    if (d.size() < table) throw FormatError("gpb: short file");
    const u32 n = rd32(d, count_at);
    if (n > kMaxEntries || table + size_t(n) * row > d.size()) throw FormatError("gpb: bad file count");
    for (u32 i = 0; i < n; ++i) {
        const size_t at = table + size_t(i) * row;
        Entry e;
        const u32 name = rd32(d, at);
        e.offset = rd32(d, at + 4);
        if (name >= d.size() || e.offset > d.size()) throw FormatError("gpb: entry outside the file");
        const auto end = std::find(d.begin() + name, d.end(), u8(0));
        e.name.assign(d.begin() + name, end);
        if (p.version_ == 2) {
            e.size = rd32(d, at + 8);
            if (e.size > d.size() - e.offset) throw FormatError("gpb: entry outside the file");
        }
        p.entries_.push_back(std::move(e));
    }
    if (p.version_ != 2) {
        // No sizes: each file runs to the next one's start.
        std::vector<u32> starts;
        for (const Entry& e : p.entries_) starts.push_back(e.offset);
        std::sort(starts.begin(), starts.end());
        for (Entry& e : p.entries_) {
            const auto next = std::upper_bound(starts.begin(), starts.end(), e.offset);
            e.size = (next == starts.end() ? u32(d.size()) : *next) - e.offset;
        }
    }
    return p;
}

const ImagePack::Entry* ImagePack::find(std::string_view name) const {
    for (const Entry& e : entries_)
        if (e.name == name) return &e;
    return nullptr;
}

Bytes ImagePack::read(const Entry& e) const {
    const ByteSpan stored = file_.subspan(e.offset, e.size);
    if (stored.size() >= 8 && rd32(stored, 0) == ArchiveIndex::kPackedMagic) return unpack_file(stored, size_t(u32(0) - rd32(stored, 4)));
    return Bytes(stored.begin(), stored.end());
}

} // namespace gt4r::menu
