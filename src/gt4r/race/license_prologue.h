#pragma once

// Gran Turismo 4 Prologue's judge of a license test beside Gran Turismo 4's (license_rules.h): what its
// DynamicsConductor::vf44 (0x3C5E08) asks before the pit lane counts (docs/formats/prologue.md "The license tests").

#include "gt4r/race/license_rules.h"

#include <span>

namespace gt4r::sim::ai {
class CourseLines;
}

namespace gt4r::race::prologue {

// The course's pit zone (0x42A670 on the AI course object, cond + 0xDFE8): along the stop line (the race's line 4; six points
// or more) from its second point to its third last, by the distance along the lap (a lap added in its first half, as the
// line's own points have it). Outside it the pit lane's surface does not count.
struct PitZone {
    float lap = 0, from = 0, to = 0; // from == to == 0: the course has none
    bool any() const { return to > from; }
    bool holds(float v) const;
};
PitZone pit_zone(const sim::ai::CourseLines& lines);

// A step's wheels by the road under them: those on a material off the course (the table 0x503E28: 2, 3, 4 and 6) and, inside the pit
// zone, on the pit lane (the triangles' flag bit 0); leaving the course counts on every course kind but 2 (0x3B4FC0). The hits
// and the cones as Gran Turismo 4's.
LicenseStep license_step(const CarState& c, const PitZone& zone, int course_kind, float mass, int cones);

} // namespace gt4r::race::prologue
