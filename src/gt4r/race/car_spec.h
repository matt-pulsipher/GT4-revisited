#pragma once

// What the race view needs of a car from the database: where its wheels are and how large, the
// rev counter's numbers, its paints. docs/formats/model.md "Wheels as the game places them",
// docs/formats/race_display.md "The meters".

#include "gt4r/formats/db/specdb.h"
#include "gt4r/race/rail_simulation.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::race {

// Index 0 front, 1 rear; metres.
struct CarSpec {
    float wheelbase = 2.6f;
    float half_track[2] = {0.75f, 0.75f};  // from the car's middle to a hub's middle
    float ride_height[2] = {0.13f, 0.13f}; // the body's underside above the road
    float tire_width[2] = {0.2f, 0.2f};
    float tire_radius[2] = {0.31f, 0.31f}; // R: rim radius + sidewall
    float rim_radius[2] = {0.2f, 0.2f};    // r
    float red_line = 7000, rev_limit = 7500; // revolutions per minute
    bool from_database = false; // false: a row was missing and the values above are stand-ins
};
CarSpec read_car_spec(db::SpecDb& db, const std::string& car_label);

// The Pat0 paint of each of a car's paint rows, in the rows' order (VARIATION +0x04, less 1).
std::vector<u32> car_paints(db::SpecDb& db, const std::string& car_label);
// A paint's chip words (its row's +0x24 .. +0x33, 0x445B28): its colors and their flags, as the menus' color chips take them.
std::array<u32, 4> paint_chip(db::SpecDb& db, const std::string& car_label, u32 paint);

// The body's box (VARIATION +0x10, metres): height above the model's y = 0, width, length ahead of and behind its origin.
std::optional<std::array<float, 4>> car_box(db::SpecDb& db, const std::string& car_label);

// The stand-in simulation's car of those sizes.
RailCar rail_car(const CarSpec& spec);

} // namespace gt4r::race
