#pragma once

// A course's split points for the section times ("T1   0'27.247", docs/formats/race_display.md "Messages"):
// the road's (RNW4) first checkpoint list, distances along the lap (Tsukuba: 505.9 and 1072.2 m). The
// lap's step 0x358608 compares each car's distance with its next point (F +0x4B4, the distances by
// 0x3C0098 → 0x395498 → 0x3E6C20) and pushes event 15 with the time (0x359C10).

#include "gt4r/base/types.h"

#include <vector>

namespace gt4r::race {

// The road file's (RNW4, course slot 55) checkpoint list `list` (+0x38 u16 the lists, +0x58 -> {u32 count,
// u32 -> f32[count]} each): its distances; empty without it.
std::vector<float> road_list(ByteSpan rnw4, size_t list);

} // namespace gt4r::race
