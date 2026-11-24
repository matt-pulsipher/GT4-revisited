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
    float spacing_m = 20;   // between places (K +0xF887)
    int kind = 0;           // the row's +0x10 (K +0xCBDD, 0x34C968; 255 reads 0): 2 and 3 the loose courses, where a
                            // license test does not count the wheels off the course (0x34C998)
    float speed() const { return speed_kmh / 3.6f; } // metres per second
};

// The course's start by its label ("tsukuba", "r_grandcanyon").
CourseStart course_start(db::SpecDb& db, const std::string& course);

// The rolling start's distance, speed and spacing from the course's drivers' file (crs/<course>.ad):
// its first driver set (the file's slot 2; the course object's +0xA0, 0x466B90) bytes +0x23 (x 10 m
// behind the line), +0x24 (km/h), +0x25 (m); without that set the program's table at 0x623DC0 (a row for
// the tarmac courses, one for the loose: zeros); a zero takes the default (400 m, 80 km/h, 20 m: vf28
// 0x34D490).
void apply_drivers_file(CourseStart& s, ByteSpan ad);

// A start code's digit (0-15; -1: not a hex digit).
int start_code_digit(const std::string& code, size_t i);

} // namespace gt4r::race
