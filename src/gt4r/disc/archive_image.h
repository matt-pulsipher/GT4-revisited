#pragma once

#include "base/reader.h"
#include "gt4r/base/types.h"

#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gt4r {

// One file of a disc's volumes: where its bytes are in the disc image.
struct ImageFile {
    std::string path;
    u64 disc_offset = 0;
    u32 size = 0;        // unpacked
    u32 stored_size = 0; // on the disc
    bool packed = false;
    u8 volume = 0; // which of the disc's volumes holds it
};

// A disc's files as one volume in Gran Turismo 3's layout with every file stored plain, so the
// shared engine's Volume reads them, and with it its file layer (mods over the disc), music
// and movie streams and decoders.
//
// The image is the table of contents (the layout in the shared engine's disc/volume.h), then
// every file from a sector of its own in the order given. A plain file's bytes are read from
// the disc where they are; a packed file is unpacked when a read first touches it (a few stay
// unpacked: a stream reads its file in pieces). Nothing is written anywhere.
class ArchiveImage : public gt3r::Reader {
public:
    static constexpr u64 kSector = 0x800;

    // `files`: paths unique. The disc reader must outlive the image.
    ArchiveImage(gt3r::Reader& disc, std::vector<ImageFile> files);

    u64 size() const override { return size_; }
    const std::string& path() const override { return disc_.path(); }
    void read_at(u64 offset, void* out, size_t len) override;
    using gt3r::Reader::read_at;

    const std::vector<ImageFile>& files() const { return files_; }
    // The table of contents as the image presents it.
    ByteSpan table() const { return toc_; }

private:
    std::shared_ptr<const Bytes> unpacked(size_t file);

    gt3r::Reader& disc_;
    std::vector<ImageFile> files_;
    std::vector<u64> starts_; // each file's first byte in the image, ascending
    Bytes toc_;
    u64 size_ = 0;

    struct Cached {
        size_t file;
        std::shared_ptr<const Bytes> data;
    };
    std::mutex mutex_;
    std::list<Cached> cache_; // most recent first
    size_t cache_bytes_ = 0;
};

} // namespace gt4r
