#pragma once

// gt4r_diff's check of the showroom car (sim/showroom.h): the program's own ShowRoomCar built on every car
// as sold against ours.

#include "gt4r/disc/disc.h"

#include <string>

namespace gt4r::diff {

struct ShowroomResult {
    bool ran = false;
    size_t cars = 0, built = 0;                     // cars, and those both sides built
    size_t evaluation_equal = 0, power_equal = 0, torque_equal = 0; // bit for bit
    double worst_evaluation = 0, worst_power = 0;   // the largest relative differences
    std::string first_evaluation, first_power;      // the first car that differs
};
// The program's set-car (0x444480) and its showroom car build (0x364868 on a ShowRoomCar's 0x1800 bytes),
// read: the evaluation (+0x15E0, 0x364F00) and the power and torque (+0x1700 / +0x1704); ours by
// sim::Showroom on our equipment as sold. `limit`: at most that many cars (0: all).
ShowroomResult run_showroom(Disc& disc, bool print, size_t limit = 0);

// The opponent picker's steps (menu/game_opponents.h) against the program's, each on made-up lists:
// the sort (the C library's qsort 0x5A4BF8 with the order 0x130660), the search (0x130618), the draw
// (0x130690 on the generator at 0x874D50) and the dirt step (0x1307E8 on the disc's cars).
struct PickerResult {
    bool ran = false;
    size_t sorts = 0, sorts_equal = 0, searches = 0, searches_equal = 0, draws = 0, draws_equal = 0, dirt = 0, dirt_equal = 0;
};
PickerResult run_picker(Disc& disc, bool print);

} // namespace gt4r::diff
