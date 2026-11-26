#pragma once

// The game's picture of the frame being drawn (App sets it; race/game_area.h says what the numbers are).

#include "gt4r/race/game_area.h"
#include "gt4r/race/race_display.h"

namespace gt4r::game_area {

inline float g_width = 640.0f;
inline void set_width(float units) {
    g_width = units;
    race::set_wide_push(race::anchor_push(race::canvas_side(units)));
}
inline float width() { return g_width; }                   // UI units, 360 tall
inline float side() { return race::canvas_side(g_width); }  // canvas units (640 x 480) beside the page
inline float top() { return race::canvas_top(g_width); }    // canvas units above and below it (a picture narrower than 4:3)
inline float scale() { return race::canvas_scale(g_width); } // UI units a canvas unit
// Where the 640-wide page begins on the screen (UI units).
inline float page_left() { return (g_width - 480.0f) * 0.5f; }

} // namespace gt4r::game_area
