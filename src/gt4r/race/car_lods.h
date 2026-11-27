#pragma once

// A race car body's levels of detail as the race view builds them (car_visual.cpp): the body program's level switch
// (0x07 at the program's start) and what each level draws without its wheels' callbacks. docs/formats/model.md "The
// second entry point".

#include "gt4r/base/types.h"

#include <vector>

namespace gt4r::race {

struct BodyLods {
    bool has_switch = false;
    float centre[3] = {0, 0, 0};
    float size = 0;
    int levels = 0; // the switch's n
    bool wheel_face = false; // the program calls callback 46: its far level draws the wheels as shapes of its own
    struct Level {
        int index = 0;
        int triangles = 0;                // the body's shapes there (its wheels' callbacks left out)
        float extent[3] = {0, 0, 0};      // their box's size
        bool missing = false;             // no body there: under a tenth of level 0's triangles (the race view then shows another)
    };
    std::vector<Level> level;
    // Every level switch model 0's program reaches (the cars without one at the start have theirs inside the wheels'
    // constructs and the body's parts): its centre, size and levels.
    struct Switch {
        u32 at = 0;
        float centre[3] = {0, 0, 0};
        float size = 0;
        int levels = 0;
    };
    std::vector<Switch> switches;
    bool any_missing() const {
        for (const Level& l : level)
            if (l.missing) return true;
        return false;
    }
};

// A car file's (car/lod/<model>, "CAR4") body levels, unpainted. Throws FormatError.
BodyLods body_lods(ByteSpan car_file);

// Whether a body level draws the body: at least a tenth of the nearest level's triangles.
bool draws_body(int triangles, int nearest);
// The level the race view shows for the program's `level` (clamped to `levels` - 1): a level that does not draw the
// body (`drawn` false) is shown as the nearest that does, the farther one first. This departs from the program, which
// draws such a level as it is (the 1886 Benz's levels 1 and 2: its wheels alone).
int shown_level(int level, int levels, const bool* drawn, int drawn_count);

} // namespace gt4r::race
