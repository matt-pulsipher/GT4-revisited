#pragma once

// Gran Turismo 4 Prologue's roads: every course of its disc keeps the road in Gran Turismo 3's
// "GTRW" section (slot 28) instead of an "RNW4" (slot 55). This reads it with the shared
// engine's reader and presents it as a Runway, so the simulation and the race view use it as
// they use Gran Turismo 4's; docs/formats/prologue.md "Courses".

#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway.h"

#include <optional>

namespace gt4r::course {

constexpr int kSlotRoadGtrw = 28; // GTRW, Gran Turismo 3's slot of the road

inline bool is_gtrw(ByteSpan d) { return d.size() >= 0x60 && gt3r::has_magic(d, "GTRW"); }

// The road of a GTRW section as a Runway:
// - the segments' vertices and triangles in one list (a triangle's material is its `bits`, its
//   flags its `kind`: 0x01 the pit lane, 0x02 wet, as in Gran Turismo 4's road);
// - a vertex's two light values (u16, / 65535) as the bytes `b` (/ 255) and `c` (/ 127);
// - the checkpoints' lines with their ends as left and right as a driver sees them, at the
//   road's height where the road is under them;
// - the start grid with Gran Turismo 4's heading (direction (sin h, cos h));
// - the walls' points as they are (the same chains);
// - a lookup tree made here over the triangles (GTRW's own lookup is a grid of 8 m cells).
// Throws FormatError.
Runway runway_from_gtrw(ByteSpan section);

// A course file's road: its RNW4 (slot 55), else its GTRW (slot 28); nullopt when it has neither.
// Throws FormatError when the section is there but does not read.
std::optional<Runway> course_road(const CourseFile& file, ByteSpan data);

} // namespace gt4r::course
