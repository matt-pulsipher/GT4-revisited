#pragma once

// A player's car as it travels to the other machines (docs/specs/net.md "The car"): an arcade car by its label, its paint
// and the tires chosen; a garage car with its parts, its settings and its condition as well. Every machine builds the car
// from its own disc, and the digest of the parameter block its converter makes of it must be the owner's.

#include "gt4r/net/online_race.h"
#include "gt4r/sim/pit_tires.h"
#include "gt4r/sim/program.h"

#include "net/protocol.h"

#include <memory>
#include <optional>
#include <string>

namespace gt4r::sim {
class Showroom;
}

namespace gt4r::net {

struct CarChoice {
    std::string label;
    u8 paint = 0;
    int tire = -1;      // the kind of tires chosen (0 .. 12: the arcade's setting page); -1: the car's own
    bool garage = false; // with `equipment` and `condition`
    sim::Equipment equipment;
    std::array<u8, 0x20> condition{};
    bool automatic = true, asm_on = true, tcs_on = true, wheel_aid = false;
    u8 driver = 0;      // sim::Entry::driver
    u64 digest = 0;     // CarBuilder::digest of the car on a paved course
    // A mod car's (formats/db/mod_cars.h): its package's SHA-256 (lower-case hex) and size, and its donor; "" for a disc car.
    // A machine that lacks the car and has the donor fetches the package from its owner (net/mod_car_package.h).
    std::string package, donor;
    u32 package_size = 0;

    bool operator==(const CarChoice& o) const;
};
void write_choice(gt3r::net::Writer& w, const CarChoice& c);
bool read_choice(gt3r::net::Reader& r, CarChoice& c);

// Why an equipment's settings are none the settings pages give ("" : they are): each the default of its fitted part or within
// the page's range (the part's row: 0x1490B0 on, menu/game_car.cpp), the numbers no page sets as the program's set-car leaves
// them. A settable gearbox's ratios are held to the ranges round the set its slider and final drive make, a quarter wider
// (❓ judged: parts fitted after the box was set move the set, not the ratios).
std::string settings_problem(db::SpecDb& db, const sim::Program& program, const sim::Equipment& eq);

// The disc's side: builds the cars of descriptors.
class CarBuilder {
public:
    // All kept by reference; `read` reads a file of the disc by its path.
    CarBuilder(db::SpecDb& db, const sim::Program& program, sim::FileReader read);
    ~CarBuilder();

    // The car's equipment as it races on a course of `course_kind` (COURSE +0x10: 2 dirt, 3 snow), its tires on
    // (sim/race_tires.h); nullopt with the reason in `why` when this disc has no such car.
    std::optional<sim::Equipment> raced(const CarChoice& c, int course_kind, std::string* why = nullptr) const;
    // FNV-1a of the parameter block (0x1100 bytes) the converter makes of the car on a paved course, and of its condition;
    // 0 when it cannot be built.
    u64 digest(const CarChoice& c) const;
    // Why a car received cannot race here ("" : it can): no such car or paint, a part that does not fit it, another digest
    // than its owner's.
    std::string problem(const CarChoice& c) const;
    // The menus' figures of the car as raced: its power (PS) and weight (kg); 0 when it cannot be built.
    int power(const CarChoice& c) const;
    int weight(const CarChoice& c) const;
    db::SpecDb& db() const { return db_; }

private:
    db::SpecDb& db_;
    const sim::Program& program_;
    sim::FileReader read_;
    std::unique_ptr<sim::Showroom> showroom_;
};

} // namespace gt4r::net
