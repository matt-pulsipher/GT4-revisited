#include "gt4r/disc/archive_image.h"

#include "gt4r/disc/archive_index.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>

namespace gt4r {

namespace {

constexpr size_t kCacheBytes = 64u << 20;
constexpr size_t kCacheFiles = 8;

struct Folder {
    std::map<std::string, std::unique_ptr<Folder>> folders;
    std::vector<std::pair<std::string, size_t>> files; // name, index
    u32 node = 0;
    u32 name = 0;
};

void put32(Bytes& b, size_t o, u32 v) { std::memcpy(b.data() + o, &v, 4); }

} // namespace

ArchiveImage::ArchiveImage(gt3r::Reader& disc, std::vector<ImageFile> files) : disc_(disc), files_(std::move(files)) {
    // The folder tree.
    Folder root;
    for (size_t i = 0; i < files_.size(); ++i) {
        const std::string& p = files_[i].path;
        Folder* f = &root;
        size_t at = 0;
        for (size_t slash; (slash = p.find('/', at)) != std::string::npos; at = slash + 1) {
            std::unique_ptr<Folder>& sub = f->folders[p.substr(at, slash - at)];
            if (!sub) sub = std::make_unique<Folder>();
            f = sub.get();
        }
        f->files.emplace_back(p.substr(at), i);
    }
    // Node offsets (a folder, its files, then its folders), then the names.
    u32 next = 0x14;
    Bytes names;
    auto name = [&](const std::string& s) {
        const u32 o = u32(names.size());
        for (char c : s) names.push_back(u8(~u8(c)));
        names.push_back(0xFF);
        return o;
    };
    std::vector<u32> file_node(files_.size()), file_name(files_.size());
    auto place = [&](auto&& self, Folder& f, const std::string& own) -> void {
        f.node = next;
        f.name = name(own);
        next += 8 + 4 * u32(1 + f.files.size() + f.folders.size());
        for (auto& [n, i] : f.files) file_node[i] = next, file_name[i] = name(n), next += 12;
        for (auto& [n, sub] : f.folders) self(self, *sub, n);
    };
    place(place, root, "");
    const u32 pool = next;
    if (u64(pool) + names.size() >= (1u << 24)) throw FormatError("GT4 volume: too many files");
    toc_.assign(pool + names.size(), 0);
    put32(toc_, 0x00, ArchiveIndex::kMagic);
    put32(toc_, 0x04, 0x00020002);
    put32(toc_, 0x08, u32(toc_.size()));
    put32(toc_, 0x0C, pool);
    std::memcpy(toc_.data() + pool, names.data(), names.size());

    // The files' places: each from a sector of its own.
    starts_.resize(files_.size());
    u64 sector = (toc_.size() + kSector - 1) / kSector;
    for (size_t i = 0; i < files_.size(); ++i) {
        if (sector > 0xFFFFFFFFull) throw FormatError("GT4 volume: too large");
        starts_[i] = sector * kSector;
        put32(toc_, file_node[i], pool + file_name[i]);
        put32(toc_, file_node[i] + 4, u32(sector));
        put32(toc_, file_node[i] + 8, files_[i].size);
        sector += (u64(files_[i].size) + kSector - 1) / kSector;
    }
    size_ = sector * kSector;
    auto write = [&](auto&& self, const Folder& f, u32 parent) -> void {
        put32(toc_, f.node, (pool + f.name) | 1u << 24);
        put32(toc_, f.node + 4, u32(1 + f.files.size() + f.folders.size()));
        put32(toc_, f.node + 8, parent);
        u32 slot = f.node + 12;
        for (auto& [n, i] : f.files) put32(toc_, slot, file_node[i]), slot += 4;
        for (auto& [n, sub] : f.folders) put32(toc_, slot, sub->node), slot += 4, self(self, *sub, f.node);
    };
    write(write, root, 0);
}

std::shared_ptr<const Bytes> ArchiveImage::unpacked(size_t file) {
    {
        std::lock_guard lock(mutex_);
        for (auto it = cache_.begin(); it != cache_.end(); ++it)
            if (it->file == file) {
                cache_.splice(cache_.begin(), cache_, it);
                return cache_.front().data;
            }
    }
    const ImageFile& f = files_[file];
    auto data = std::make_shared<const Bytes>(unpack_file(disc_.read_at(f.disc_offset, f.stored_size), f.size));
    std::lock_guard lock(mutex_);
    cache_.push_front({file, data});
    cache_bytes_ += data->size();
    while (cache_.size() > 1 && (cache_.size() > kCacheFiles || cache_bytes_ > kCacheBytes)) {
        cache_bytes_ -= cache_.back().data->size();
        cache_.pop_back();
    }
    return data;
}

void ArchiveImage::read_at(u64 offset, void* out, size_t len) {
    if (offset > size_ || size_ - offset < len) throw FormatError("GT4 volume: read past the end");
    u8* dst = static_cast<u8*>(out);
    while (len) {
        size_t n;
        if (offset < toc_.size()) {
            n = size_t(std::min<u64>(len, toc_.size() - offset));
            std::memcpy(dst, toc_.data() + offset, n);
        } else {
            // The file at or before the offset: its bytes, or the padding up to the next file.
            const size_t next = size_t(std::upper_bound(starts_.begin(), starts_.end(), offset) - starts_.begin());
            const u64 gap_end = next < starts_.size() ? starts_[next] : size_;
            size_t i = next; // the last file starting at or before `offset` that has bytes there
            bool inside = false;
            while (i-- > 0) {
                if (offset - starts_[i] < files_[i].size) {
                    inside = true;
                    break;
                }
                if (files_[i].size) break; // (empty files share their start with the next file)
            }
            if (!inside) {
                n = size_t(std::min<u64>(len, gap_end - offset));
                std::memset(dst, 0, n);
            } else {
                const ImageFile& f = files_[i];
                const u64 rel = offset - starts_[i];
                n = size_t(std::min<u64>(len, f.size - rel));
                if (f.packed) std::memcpy(dst, unpacked(i)->data() + rel, n);
                else disc_.read_at(f.disc_offset + rel, dst, n);
            }
        }
        dst += n, offset += n, len -= n;
    }
}

} // namespace gt4r
