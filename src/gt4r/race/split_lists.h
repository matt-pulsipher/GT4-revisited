#pragma once

// The race's lists of the cars through each section's end, by round (the conductor's virtual 18, 0x3578B0, in the conductor's
// record at +0xD3EC .. +0xD48C), and the Race Monitor's pace-up mark read from them (0x3FA508).
// docs/formats/race_display.md "The Race Monitor".

#include "gt4r/base/types.h"

#include <vector>

namespace gt4r::race {

class SplitLists {
public:
    // `sections`: the lap's sections (the split points and the line: the conductor's vf42); the last ends at the line.
    explicit SplitLists(int sections = 0) { reset(sections); }
    void reset(int sections);
    int sections() const { return int(lists_.size()); }
    // Car `car` through the end of section `section` in round `round` (its crossings of the line) at time `t` (vf18): a round
    // later than the list's starts it afresh with this car; the list's round puts the car in it by its time (after those not
    // later); an earlier round's car is not listed.
    void pass(int car, int round, int section, s64 t);
    // The time car `car` is listed with at a section's end (-1: not listed).
    s64 time(int section, int car) const;
    // vf18's section times: car `car`'s best time through section `section` (from its last section end passed; the clock's 0
    // before its first), -1 none yet; the round it was made in. (The conductor's +0xD7A4 / +0xD924, read by 0x3FA3D0.)
    s64 best(int car, int section) const;
    int best_round(int car, int section) const;
    // 0x3FA508: car `a` (in section `a_section`, F +0x4AE, with `a_crossings`, F +0x4AC) gaining on car `b`: a has crossed the
    // line and is past the first section's end, and at the last section end a passed it was behind b by less than at the one
    // before (both listed at both).
    bool pace_up(int a, int b, int a_section, int a_crossings) const;

private:
    struct List {
        int round = -1;
        std::vector<std::pair<int, s64>> cars; // in time order
    };
    std::vector<List> lists_;
    struct Car {
        s64 last = 0;                 // its last section end passed (+0xD60C)
        std::vector<u32> best;        // each section's best (0x157529FF none)
        std::vector<int> best_round;  // (u16, -1 none)
    };
    std::vector<Car> cars_;
};

// The Race Monitor's sector bar colour (0x3DC7B0) for a car's best time through a section: no time, the sections' greys by turn
// (0x80282828 even, 0x80181818 odd); else by `faster` (the cars whose best there is under it) of `cars`: r = faster / (cars - 1),
// red (1 - r) x 255, green (1 - r) x 0.2 x 255, blue (0.4 r + 0.2 (1 - r)) x 255, in EE floats, OR-ed into 0x80000000 as
// the program packs them: the fastest red (0x803232FF), the slowest dark blue (0x80650000).
u32 sector_color(int section, bool timed, int faster, int cars);

} // namespace gt4r::race
