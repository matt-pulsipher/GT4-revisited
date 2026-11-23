#pragma once

// A course's file for the computer drivers, crs/<name>.ad (docs/formats/course.md "The drivers'
// file"): the driving lines and the driver parameter sets. The lines' points are in the
// simulation's frame (x, y across the ground, the course's z = -y), as the file has them; what the
// race adds to them when it loads them (heights, distances, slopes) is the drivers' (sim/ai).

#include "gt4r/base/types.h"

#include <array>
#include <optional>
#include <vector>

namespace gt4r::course {

using gt3r::s8;

struct AiLinePoint {
    u8 kind = 0;          // the stretch from this point: 0 straight, 1 an arc of `radius`
    s8 brake_adjust = 0;  // the braking distance's adjustment (percent)
    s8 speed_adjust = 0;  // the arc's speed adjustment (km/h); -128: the point is passed over
    float x = 0, y = 0;   // simulation frame (metres)
    float radius = 0;     // the arc's signed radius (metres; 0 on a straight)
    float heading = 0;    // radians: the direction (-sin h, cos h) in (x, y)
    u16 segment = 0xFFFF; // the road segment as the file has it (0xFFFF: none)
};

struct AiData {
    static constexpr int kLines = 8;     // the race's line numbers 0..7 (0 is never used)
    static constexpr int kClasses = 4;   // driver sets: one per driveline class
    static constexpr size_t kSetSize = 0x80;
    using Set = std::array<u8, kSetSize>;

    enum class Format { Floats, Fixed }; // "ADLN" with 0x40-byte points; the older 0x28-byte fixed point
    Format format = Format::Floats;
    std::array<std::vector<AiLinePoint>, kLines> lines; // by the race's line number
    std::array<std::optional<Set>, kClasses> sets;      // slots 2..5

    // The file's table of 32 section offsets, slot 1 the lines.
    static bool looks_like(ByteSpan file);
    // Throws FormatError.
    static AiData parse(ByteSpan file);
};

} // namespace gt4r::course
