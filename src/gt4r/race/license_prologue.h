#pragma once

// Gran Turismo 4 Prologue's judge of a license test beside Gran Turismo 4's (license_rules.h): what its
// DynamicsConductor::vf44 (0x3C5E08) asks before the pit lane counts (docs/formats/prologue.md "The license tests").

#include "gt4r/race/license_rules.h"

#include <array>
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

// How the player's car's run ends (0x3C0DD8, 0x3C1050; the modes of Field::finish: 1 it coasts down and stops, 2 it brakes to a
// stop, 3 its driver drives on): by the rules' +0x41 (1: 3, 2: 1, 3: 2), else by the test's kind (3 and 4: 3, any other: 1), 2 for
// a test of cones, 3 with other cars in it.
int end_mode(const LicenseRules& r, bool others);
// ... of a test failed (0x3C0F28): leaving the course (3) and a hit (4) as end_mode, but alone a car coasts down rather than
// drives on; anything else brakes to a stop. The computer's cars drive on (mode 3) whenever other cars are in the test.
int fail_mode(const LicenseRules& r, bool others, LicenseFail fail);

// The grade (0x3C1050): 0 gold, 1 silver, 2 bronze, 3 none. With fewer than three other cars the first target time that is not
// beaten (`targets` gold, silver, bronze; <= 0 none: any time), but none with another car ahead; with three or more the place
// (1 gold, 2 silver, 3 bronze). A test of a mileage (the rules' +0x45, tenths of a kilometer per liter) gives none under it.
int grade(const std::array<s64, 3>& targets, s64 time_ms, const LicenseRules& r, int place, int mileage, int mileage_wanted);

} // namespace gt4r::race::prologue
