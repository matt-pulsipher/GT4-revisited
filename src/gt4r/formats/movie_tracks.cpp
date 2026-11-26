#include "gt4r/formats/movie_tracks.h"

#include "formats/pss.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace gt4r {

namespace {

constexpr u8 kPadding = 0xBE;

struct TrackFilter {
    gt3r::video::Movie::ReadFn read;
    u64 size = 0;
    int track = -1; // the one kept: the first met

    // The packet whose start code is at `pos`: its private header's first and last bytes, from
    // `buf` (holding the file from `buf_pos`) or read past its end. False when it is not an
    // MPEG-2 PES packet with one.
    bool sub_header(u64 pos, const u8* buf, u64 buf_pos, size_t buf_len, u8& kind, u8& track_out) {
        auto byte_at = [&](u64 at, u8& out) {
            if (at >= size) return false;
            if (at >= buf_pos && at < buf_pos + buf_len) out = buf[at - buf_pos];
            else read(at, &out, 1);
            return true;
        };
        u8 len_hi = 0, len_lo = 0, flags = 0, header_len = 0;
        if (!byte_at(pos + 4, len_hi) || !byte_at(pos + 5, len_lo) || !byte_at(pos + 6, flags) || (flags & 0xC0) != 0x80 ||
            !byte_at(pos + 8, header_len))
            return false;
        const size_t packet_len = size_t(len_hi) << 8 | len_lo; // after the length's two bytes
        if (3 + size_t(header_len) + gt3r::PssDemuxer::kAudioSubHeader > packet_len) return false;
        const u64 sub = pos + 9 + header_len;
        return byte_at(sub, kind) && byte_at(sub + 3, track_out);
    }

    void filter(u64 offset, u8* buf, size_t len) {
        for (size_t i = 0; i + 4 <= len; ++i) {
            if (buf[i] != 0 || buf[i + 1] != 0 || buf[i + 2] != 1 || buf[i + 3] != gt3r::PssDemuxer::kPrivateStream1) continue;
            u8 kind = 0, t = 0;
            if (!sub_header(offset + i, buf, offset, len, kind, t) || kind != 0xFF) continue;
            if (track < 0) track = t;
            else if (t != track) buf[i + 3] = kPadding;
        }
    }
};

} // namespace

gt3r::video::Movie::ReadFn first_sound_track_only(gt3r::video::Movie::ReadFn read, u64 size) {
    auto f = std::make_shared<TrackFilter>();
    f->read = std::move(read);
    f->size = size;
    return [f](u64 offset, void* out, size_t len) {
        f->read(offset, out, len);
        f->filter(offset, static_cast<u8*>(out), len);
    };
}

std::unique_ptr<gt3r::video::Movie> open_movie(gt3r::Vfs& vfs, const std::string& path) {
    auto stream = vfs.open_stream(path);
    if (!stream) throw gt3r::FormatError("movie not found: " + path);
    return std::make_unique<gt3r::video::Movie>(first_sound_track_only(std::move(stream->read), stream->size), stream->size);
}

} // namespace gt4r
