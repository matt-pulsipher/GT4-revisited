#include "gt4r/disc/archive_index.h"

#include "gt4r/base/inflate.h"

#include <algorithm>
#include <map>

namespace gt4r {

namespace {

constexpr u32 kPageKey = 0x14AC327Au;
constexpr u8 kByteKey = 0x55;
constexpr u64 kSearchBytes = 64ull << 20; // the index is within the volume's first pages
constexpr int kMaxDepth = 64;

u32 be32(ByteSpan d, size_t o) {
    if (o + 4 > d.size()) throw FormatError("GT4 volume: entry past the page");
    return u32(d[o]) << 24 | u32(d[o + 1]) << 16 | u32(d[o + 2]) << 8 | d[o + 3];
}

bool index_header(ByteSpan h) { return h.size() >= 8 && rd32(h, 0) == ArchiveIndex::kMagic && rd16(h, 4) == 1 && rd16(h, 6) == 3; }

struct Folder {
    u32 parent = 0;
    std::string name;
};

} // namespace

std::optional<u64> ArchiveIndex::find(gt3r::Reader& file, u64 base, u64 length) {
    const u64 end = std::min(length, kSearchBytes);
    const size_t block = 1 << 20;
    for (u64 at = kPage; at + 8 <= end; at += block) {
        const size_t n = size_t(std::min<u64>(block, end - at));
        const Bytes buf = file.read_at(base + at, n);
        for (size_t o = 0; o + 8 <= n; o += kPage)
            if (index_header(ByteSpan(buf).subspan(o, 8))) return at + o;
    }
    return std::nullopt;
}

ArchiveIndex ArchiveIndex::read(gt3r::Reader& file, u64 base, u64 length) {
    const std::optional<u64> at = find(file, base, length);
    if (!at) throw FormatError("GT4 volume: no file index");
    ArchiveIndex out;
    out.offset = *at;
    const Bytes hdr = file.read_at(base + *at, 0x40);
    out.size = rd32(hdr, 0x08);
    const u32 index_pages = rd32(hdr, 0x0C);
    const u32 page_size = rd16(hdr, 0x10), count = rd16(hdr, 0x12);
    if (page_size != kPage || out.size < 0x44 + count * 4 || *at + out.size > length ||
        u64(index_pages) * kPage < out.size)
        throw FormatError("GT4 volume: bad file index header");
    out.data_offset = *at + u64(index_pages) * kPage;
    const Bytes index = file.read_at(base + *at, out.size);

    struct Item {
        u32 parent;
        std::string name;
        u8 kind;
        u32 page, time, stored, size;
    };
    std::vector<Item> items;
    std::map<u32, Folder> folders;
    u32 prev_end = rd32(index, 0x40) ^ kPageKey;
    for (u32 i = 0; i < count; ++i) {
        const u32 page_end = rd32(index, 0x44 + size_t(i) * 4) ^ (kPageKey * (i + 2));
        if (page_end < prev_end || page_end > out.size) throw FormatError("GT4 volume: bad index page table");
        Bytes page = inflate_exact(ByteSpan(index).subspan(prev_end, page_end - prev_end), kPage);
        prev_end = page_end;
        for (u8& b : page) b ^= kByteKey;
        if (s16(rd16(page, 0)) != 0) continue; // a search page
        const size_t entries = size_t(rd16(page, 2)) / 2;
        if (0x0C + entries * 8 > page.size()) throw FormatError("GT4 volume: bad index page");
        for (size_t k = 0; k < entries; ++k) {
            const size_t row = page.size() - 8 * (k + 1);
            const size_t key = rd16(page, row), key_size = rd16(page, row + 2), value = rd16(page, row + 4);
            if (key_size < 4 || key + key_size > page.size() || value >= page.size())
                throw FormatError("GT4 volume: bad index entry");
            Item it{};
            it.parent = be32(page, key);
            it.name.assign(reinterpret_cast<const char*>(page.data()) + key + 4, key_size - 4);
            it.kind = page[value];
            if (it.kind == 0) {
                folders[rd32(page, value + 1)] = Folder{it.parent, it.name};
                continue;
            }
            if (it.kind > 2) throw FormatError("GT4 volume: unknown index entry kind");
            it.page = rd32(page, value + 1), it.time = rd32(page, value + 5);
            it.stored = rd32(page, value + 9);
            it.size = it.kind == 2 ? rd32(page, value + 13) : it.stored;
            items.push_back(std::move(it));
        }
    }

    // Folder paths, each resolved once.
    std::map<u32, std::string> paths;
    auto path_of = [&](u32 id) -> const std::string& {
        if (auto hit = paths.find(id); hit != paths.end()) return hit->second;
        std::vector<const Folder*> chain;
        for (u32 f = id; int(chain.size()) < kMaxDepth;) {
            auto it = folders.find(f);
            if (it == folders.end() || it->second.name == "." || it->second.name.empty()) break;
            chain.push_back(&it->second);
            f = it->second.parent;
        }
        std::string p;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) p += (*it)->name + "/";
        return paths[id] = std::move(p);
    };
    out.folders = folders.size();
    out.files.reserve(items.size());
    for (Item& it : items) {
        ArchiveFile f;
        f.path = path_of(it.parent) + it.name;
        f.offset = out.data_offset + u64(it.page) * kPage;
        f.size = it.size, f.stored_size = it.stored;
        f.packed = it.kind == 2;
        f.modified = it.time;
        if (f.offset + f.stored_size > length) throw FormatError("GT4 volume: file out of range: " + f.path);
        out.files.push_back(std::move(f));
    }
    return out;
}

u32 front_table_size(gt3r::Reader& file, u64 base, u64 length) {
    if (length < 0x14) return 0;
    const Bytes hdr = file.read_at(base, 0x14);
    if (rd32(hdr, 0) != ArchiveIndex::kMagic || rd16(hdr, 4) != 2 || rd16(hdr, 6) != 2) return 0;
    const u32 size = rd32(hdr, 8);
    return size >= 0x14 && size <= length ? size : 0;
}

std::vector<ArchiveFile> read_front_table(gt3r::Reader& file, u64 base, u64 length, bool in_range) {
    const u32 size = front_table_size(file, base, length);
    if (!size) throw FormatError("GT4 volume: no table of contents");
    const Bytes toc = file.read_at(base, size);
    std::vector<ArchiveFile> out;
    auto name_at = [&](u32 o) {
        std::string s;
        for (; o < toc.size() && toc[o] != 0xFF; ++o) s.push_back(char(~toc[o]));
        return s;
    };
    // Folder nodes: u32 name | 1 << 24, u32 count, u32 parent, u32 children[count - 1].
    // File nodes: u32 name | flags << 24 (2: packed), u32 sector, u32 size[, u32 stored size].
    auto walk = [&](auto&& self, u32 node, const std::string& prefix, int depth) -> void {
        if (depth > kMaxDepth) throw FormatError("GT4 volume: folders too deep");
        const u32 count = rd32(toc, node + 4);
        for (u32 i = 1; i < count; ++i) {
            const u32 child = rd32(toc, node + 8 + size_t(i) * 4);
            const u32 word = rd32(toc, child), flags = word >> 24;
            const std::string name = prefix + name_at(word & 0xFFFFFF);
            if (flags & 1) {
                self(self, child, name + "/", depth + 1);
                continue;
            }
            ArchiveFile f;
            f.path = name;
            f.offset = u64(rd32(toc, child + 4)) * ArchiveIndex::kPage;
            f.size = rd32(toc, child + 8);
            f.packed = (flags & 2) != 0;
            f.stored_size = f.packed ? rd32(toc, child + 12) : f.size;
            if (in_range && f.offset + f.stored_size > length) throw FormatError("GT4 volume: file out of range: " + f.path);
            out.push_back(std::move(f));
        }
    };
    walk(walk, 0x14, "", 0);
    return out;
}

Bytes unpack_file(ByteSpan stored, size_t size) {
    if (stored.size() < 8 || rd32(stored, 0) != ArchiveIndex::kPackedMagic) throw FormatError("GT4 volume: not a packed file");
    if (u32(0) - rd32(stored, 4) != u32(size)) throw FormatError("GT4 volume: packed file size mismatch");
    return inflate_exact(stored.subspan(8), size);
}

} // namespace gt4r
