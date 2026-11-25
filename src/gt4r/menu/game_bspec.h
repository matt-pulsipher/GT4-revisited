#pragma once

// The B-spec driver's book (GranTurismo4::Status +0x3E4, the user profile's +0x9C; the save's payload 0xA4,
// docs/formats/save.md): three tables of 8 x 8 u16, a row a class of the course's ❓ / the car's and a column the
// car's class (0x468418), the driver's experience in them; their cell [7][0] of the second and third tables holds
// the sum of the courses' own marks (the course record's +0x228 bytes) instead. The menus' figures (MGame's
// getBspec*Experience, 0x468B58) and the race's record of 0x40 bytes (0x468710) come from it, the record after a
// B-spec race goes back into it (0x468860).

#include "gt4r/base/types.h"

#include <array>

namespace gt4r::menu::bspec {

using Book = std::array<u16, 3 * 64>; // table t, row r, column c at t * 64 + r * 8 + c
using Weights = std::array<u8, 8>;    // the program's 0x623E40

// The classes a class's figures take in: [max(0, k - 4), min(8, k + 5)) (0x467BE8, 0x467C20).
void class_range(int k, int& lo, int& hi);

// getBspecCarExperience (0x467C58), getBspecCourseExperience (0x467D90), getBspecBattleExperience (0x467F60):
// 0 .. 100. getBspecTotalExperience (0x4680D8): 0 .. 10000.
int car_experience(const Book& b, const Weights& w);
int course_experience(const Book& b, const Weights& w);
int battle_experience(const Book& b, const Weights& w);
int total_experience(const Book& b, const Weights& w);

// The race's record for a car of class `k` (0x468710): the column k of each table (its row 7 left 0 for class
// 0, that cell being the courses' sum), then the course's four marks twice (the course record's +0x228 .. +0x22F
// bytes, each x 256).
std::array<u8, 0x40> race_record(const Book& b, int k, const u8* course_marks);
// The record after the race back into the book (0x468860): each cell of column k takes the record's value, or
// with `doubled` the old one plus twice the change (at most 0xFFFF); with `courses` (1 or 2 sets) the course's
// marks take the record's high bytes the same way (at most 255) and the change is added to the courses' sums.
void write_back(Book& b, int k, const std::array<u8, 0x40>& record, bool doubled, int courses, u8* course_marks);

} // namespace gt4r::menu::bspec
