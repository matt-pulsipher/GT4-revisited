#pragma once

// How a course's race starts when the event does not say: the course's row in the car database
// (docs/formats/race_flow.md "The start"). The program reads the COURSE row's start code (a short
// text of hex digits, the string its +0x08 names in UnistrDB) into the conductor (K +0xCBE6..,
// 0x34C7E0); a first digit other than 0 gives a rolling start, the cars running up to the line from
// the course's distance at its speed (DynamicsConductor::vf28 0x34D490: an event's own values, else
// the program's defaults 400 m and 80 km/h).

#include "gt4r/base/types.h"

#include <string>

namespace gt4r::db {
class SpecDb;
}

namespace gt4r::race {

struct CourseStart {
    bool known = false;     // the course has a row
    std::string code;       // the start code ("1ff0")
    bool rolling = false;   // its first digit is not 0
    float distance_m = 400; // behind the line (the program's default, -400 in K +0xF884)
    float speed_kmh = 80;   // the program's default (K +0xF886)
    float speed() const { return speed_kmh / 3.6f; } // metres per second
};

// The course's start by its label ("tsukuba", "r_grandcanyon").
CourseStart course_start(db::SpecDb& db, const std::string& course);

// A start code's digit (0-15; -1: not a hex digit).
int start_code_digit(const std::string& code, size_t i);

} // namespace gt4r::race
