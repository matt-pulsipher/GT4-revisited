#pragma once

// Baseline JPEG pictures (the disc's slide show, photo/NNN.JPG, and the photo mode's): 8 bits,
// Huffman coded, gray or YCbCr with any sampling up to 4 x 4, restart intervals. Progressive files
// are not read.

#include "gt4r/base/types.h"

#include <optional>
#include <vector>

namespace gt4r::menu {

struct JpegPicture {
    int width = 0, height = 0;
    std::vector<u32> rgba; // R, G, B, A bytes in memory order
    // The photo's focus record (an APP8 "G4PI PDI" segment of type 3): the point looked at and a
    // rectangle round it, in -1 .. 1 across and up the picture.
    struct Focus {
        float x = 0, y = 0, x0 = -1, y0 = -1, x1 = 1, y1 = 1;
    };
    std::optional<Focus> focus;
};
// Throws FormatError.
JpegPicture decode_jpeg(ByteSpan file);

} // namespace gt4r::menu
