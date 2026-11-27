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
};

} // namespace gt4r::race
