#pragma once

// The game's picture on the screen: 360 units tall and as wide as Settings > Display > Aspect ratio makes it (640 at 16:9, 480 at
// 4:3, the window's shape with fill). The game's pages and race display are laid out on 640 x 480 in its middle.

#include <algorithm>

namespace gt4r::race {

// What the race display's canvas (640 x 480) has beside it on each side, in its units: 106.667 at 16:9, 0 at 4:3.
constexpr float canvas_side(float picture_width) { return std::max(0.0f, (picture_width * (480.0f / 360.0f) - 640.0f) * 0.5f); }
// How far a display part anchored to the screen's side moves outward: the program's 96 at 16:9 and 0 at 4:3
// (race_display.h); ours between them in proportion, and beyond 16:9 with the picture's edge.
constexpr float anchor_push(float side) {
    constexpr float kWide = 320.0f / 3.0f, kPush = 96.0f;
    return side <= kWide ? kPush * side / kWide : kPush + (side - kWide);
}

} // namespace gt4r::race
