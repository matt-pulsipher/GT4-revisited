#pragma once

// How a course's race starts when the event does not say: the course's row in the car database
// (docs/formats/race_flow.md "The start"). The program reads the COURSE row's start code (a short
// text of hex digits, the string its +0x08 names in UnistrDB) into the conductor (K +0xCBE6..,
// 0x34C7E0); a first digit other than 0 gives a rolling start, the cars running up to the line from
// the course's distance at its speed (DynamicsConductor::vf28 0x34D490: an event's own values, else
// the program's defaults 400 m and 80 km/h).

#include "gt4r/base/types.h"

#include <array>
#include <span>
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

// ---- an event's own start places (the race information's row in the conductor's configuration CFG, 0x35E108) ----------

// Which conductor runs a race, as far as its start goes: a license test's (and a coffee break's), a license's pace car test's
// (tests 5, 10 and 15: RaceTraining), a mission's, any other.
enum class Conductor : u8 { Other, License, Training, Mission };
// A sequence's conductor ("license" or "license_race" with the test's label, "mission", any other).
Conductor conductor_for(const std::string& sequence, const std::string& event);

// The row's start places: place 0 the player's (CFG +0x54 s16 from the row's +0x14, CFG +0x4E from +0x42), places 1 .. 5
// (CFG +0x56 + 2 k from +0x2E + 2 k, CFG +0x4F + k from +0x80 + k).
struct RowStart {
    std::array<s16, 6> distance{}; // metres: from the section's start on a road section, else from the line; 0: the grid
    std::array<u8, 6> speed{};     // km/h
    // From a RACE row (151 bytes); a short row gives none.
    static RowStart from_row(std::span<const u8> row);
    // The distance (virtual 35) and speed (virtual 36) a conductor reads for the car of index `car`: the license's always
    // place 0's (0x3F6070 / 0x3F6080), the others' the car's own, a speed of 0 the player's (0x34D030 / 0x34D048).
    int distance_of(Conductor c, size_t car) const;
    int speed_of(Conductor c, size_t car) const;
    // The race has no start's hold: the car of index 0 has a speed (virtual 10, 0x3F6090).
    bool moving(Conductor c) const { return c != Conductor::Other && speed_of(c, 0) != 0; }
};

// How a car starts (virtual 30, 0x3F6278: the license's, the training's and the mission's): the start kind (0 on the grid;
// 1 on the driving line at its distance, the player driving; 2 the same with its computer driver driving it at the start's
// pace until it takes the line, or on a road section its start), the distance along the course (metres; negative: before the
// line) and the speed (km/h; 0 standing). `section_start`: the section's start (0x34C3E0: truncated metres), 0 without one.
struct CarStart {
    int kind = 0;
    int distance = 0;
    int speed = 0;
    // F + 0x65B (0x363B10): 1 the car starts before the section's start (or is of kind 2), where its clock starts; 2 past it.
    u8 mark = 0;
    bool on_line() const { return kind != 0; }
};
CarStart car_start(const RowStart& row, Conductor c, size_t car, int section_start);

// A section's start as the starts count it (0x34C338, 0x34C3E0): truncated metres when it lies 1 m .. the lap less 1 m
// along; else 0.
int section_start_metres(float start, float lap);

// A start code's digit (0-15; -1: not a hex digit).
int start_code_digit(const std::string& code, size_t i);

} // namespace gt4r::race
