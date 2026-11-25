#pragma once

// Frame faces (mFrameImageFace): nine parts about the layout's frame_width x frame_height, and the colours of a face's
// corners. docs/formats/menu.md "Frames".

#include "gt4r/menu/layout.h"

#include <array>
#include <vector>

namespace gt4r::menu {

// A part of a frame: its picture's property, its rectangle in the face's (a side below 0: a frame wider than its
// face, drawn turned over as the program does).
struct FramePart {
    const char* picture;
    Rect at;
};
// In the order drawn (0x298288): the middle, the top, bottom, left and right sides, the four corners.
std::array<FramePart, 9> frame_parts(const Widget& face, float w, float h);

// A part's colours: the face's one as it is; of its four (corners clockwise from the top left) the colours at the part's
// corners, between the face's along both axes (0x2979A8).
std::vector<Rgba> frame_part_colors(const std::vector<Rgba>& face, const Rect& part, float w, float h);

// A vertex colour, red, green, blue and alpha of 0..1 (a doubled one to 2); `doubled`: a picture's corner colour, whose whole value (255) is twice a picture's own
// brightness (0x49A150; the GS counts 128 as one).
std::array<float, 4> corner_color(const Rgba& c, float opacity, bool doubled);

} // namespace gt4r::menu
