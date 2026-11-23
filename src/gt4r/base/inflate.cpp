#include "gt4r/base/inflate.h"

#include <zlib.h>

namespace gt4r {

Bytes inflate_exact(ByteSpan src, size_t size) {
    Bytes out(size + 1); // (zlib wants a buffer even for an empty file)
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) throw FormatError("inflateInit2 failed");
    zs.next_in = const_cast<Bytef*>(src.data());
    zs.avail_in = static_cast<uInt>(src.size());
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(size);
    int rc = Z_OK;
    while (zs.avail_out > 0 && rc == Z_OK) rc = inflate(&zs, Z_SYNC_FLUSH);
    const size_t produced = size - zs.avail_out;
    inflateEnd(&zs);
    if (produced != size || (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR)) throw FormatError("inflate failed or stream too short");
    out.resize(size);
    return out;
}

} // namespace gt4r
