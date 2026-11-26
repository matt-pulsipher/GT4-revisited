#pragma once

// The game's picture on the screen: 360 units tall and as wide as Settings > Display > Aspect ratio makes it (640 at 16:9, 480 at
// 4:3, the window's shape with fill; a race's picture with fill down to 1:1). The game's pages and race display are laid out on
// 640 x 480 in its middle.

#include <algorithm>

namespace gt4r::race {

// The race display's canvas (640 x 480) on the picture: UI units a canvas unit. 0.75 (its 480 lines over the 360) on a picture at
// least 4:3 wide; on a narrower one (fill on a screen squarer than 4:3) the canvas fits the picture's width (ours).
constexpr float canvas_scale(float picture_width) { return picture_width >= 480.0f ? 0.75f : std::max(picture_width, 1.0f) / 640.0f; }
// What the canvas has beside it on each side, in its units: 106.667 at 16:9, 0 at 4:3 and narrower.
constexpr float canvas_side(float picture_width) { return std::max(0.0f, (picture_width * (480.0f / 360.0f) - 640.0f) * 0.5f); }
// What it has above and below it, in its units: 0 on a picture at least 4:3 wide; on a narrower one the canvas is in the middle
// and the display's parts at the top and the bottom move this far out to the picture's edges (ours).
constexpr float canvas_top(float picture_width) { return std::max(0.0f, (360.0f / canvas_scale(picture_width) - 480.0f) * 0.5f); }
// How far a display part anchored to the screen's side moves outward: the program's 96 at 16:9 and 0 at 4:3
// (race_display.h); ours between them in proportion, and beyond 16:9 with the picture's edge.
constexpr float anchor_push(float side) {
    constexpr float kWide = 320.0f / 3.0f, kPush = 96.0f;
    if (side > kWide - 0.01f && side < kWide + 0.01f) return kPush;
    return side < kWide ? kPush * side / kWide : kPush + (side - kWide);
}

} // namespace gt4r::race
