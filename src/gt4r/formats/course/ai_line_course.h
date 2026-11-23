#pragma once

// Gran Turismo 4 Prologue's computer drivers: its disc has no crs/<name>.ad files. The course file
// itself carries what Gran Turismo 3's did: the driving lines in slot 31 (the section the older
// .ad files hold in their slot 1: version 8, seven lines in 1/4096 m) and a driver set per
// driveline class in slots 37 to 40 (0x40 bytes; Gran Turismo 4's sets begin with the same 27
// bytes). docs/formats/prologue.md "The drivers".

#include "gt4r/formats/course/ai_line.h"
#include "gt4r/formats/course/course_file.h"

#include <optional>

namespace gt4r::course {

constexpr int kSlotAiLines = 31;
constexpr int kSlotDriverSets = 37; // 37 + the driveline class

// The course file's lines and sets as a drivers' file reads them; nullopt when slot 31 is not a
// line section. Throws FormatError when it does not read.
std::optional<AiData> ai_from_course(const CourseFile& file, ByteSpan data);

} // namespace gt4r::course
