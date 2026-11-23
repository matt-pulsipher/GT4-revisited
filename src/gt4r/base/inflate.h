#pragma once

#include "gt4r/base/types.h"

namespace gt4r {

// The first `size` bytes a raw DEFLATE stream unpacks to. The game's packer does not always end
// its streams (a volume's index pages stop after their last block without the end marker), so
// reaching `size` is what counts. Throws FormatError when the stream gives fewer bytes.
Bytes inflate_exact(ByteSpan src, size_t size);

} // namespace gt4r
