#pragma once

// A course's split points for the section times ("T1   0'27.247", docs/formats/race_display.md "Messages"):
// the road's (RNW4) first checkpoint list, distances along the lap (Tsukuba: 505.9 and 1072.2 m). The
// lap's step 0x358608 compares each car's distance with its next point (F +0x4B4, the distances by
// 0x3C0098 → 0x395498 → 0x3E6C20) and pushes event 15 with the time (0x359C10).

#include "gt4r/base/types.h"
#include "gt4r/formats/course/runway.h"

#include <vector>

namespace gt4r::race {

// The road file's (RNW4, course slot 55) checkpoint list `list` (+0x38 u16 the lists, +0x58 -> {u32 count,
// u32 -> f32[count]} each): its distances; empty without it. Prologue's road (GTRW, slot 28): +0x22 u16 records, +0x24 ->
// 8 bytes each {u16 type (0 the start, 1 the goal, 2 a split point), u16 the list, f32 the distance}: the list's split points.
std::vector<float> road_list(ByteSpan road, size_t list);

// The road's sections (RNW4 +0x54 u16 the count, +0x9C -> 0x20 bytes each): the stretch a license test or
// mission runs on, f32 +0x00 its start and +0x04 its goal, distances along the lap (DynamicsConductor::vf38 /
// vf39 → 0x395528 / 0x395588 take them when the course object's section +0xB0 is set, else the road's +0x18 /
// +0x1C). A race with a goal (1 m .. the lap less 1 m, 0x34C470) ends a car's run of kind 2 where it passes it
// (0x358608 → vf23). ❓ The rest of the record (+0x08 .. +0x1C: counts and three pointers, a place among them).
// +0x18 -> a section's own start places, u16 count at +0x0A (the program's grid place of the section, 0x3952C0:
// road +0x5C's records otherwise; 16 bytes each: the place and its heading).
struct RoadSection {
    float start = 0, goal = 0;
    std::vector<course::Runway::Place> grid;
};
std::vector<RoadSection> road_sections(ByteSpan rnw4);

} // namespace gt4r::race
