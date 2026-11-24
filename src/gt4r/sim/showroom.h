#pragma once

// Gran Turismo 4's showroom car (ShowRoomCar, 0x364760): a car built from its equipment the way the
// menus build one, for its figures and its evaluation (docs/specs/physics.md "The showroom car").
// The car's spec (0x3F3B68, a paved course), its parameter block (the converter 0x361908 with the
// program's rule block 0x6203C8 and a condition that leaves the engine as it is), then:
//  - the engine's figures (0x34F3F8): the curve's power and torque every 100 rpm with the power cap;
//  - the power the menus show (ShowRoomCar +0x1700, 0x364818): the peak power plus the driveline's
//    extra (0x356EA8);
//  - the evaluation (P +0xFE0, made at the converter's end by 0x360780 -> 0x3F86E8; ShowRoomCar
//    0x364F00 reads it): a number in (0, 1] that grows with power, torque, grip and downforce and falls
//    with mass and drag. Arcade's opponents are picked by it (menu.md "The race's entry table").
// Each step in the PlayStation 2's arithmetic, in the program's order; tools/gt4diff checks them against
// the user's own program on every car (run_showroom).

#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/equipment.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/record.h"

#include <memory>
#include <optional>
#include <vector>

namespace gt4r::sim {

using rules::F;

// 0x34F3F8's figures (its output block): the peak power and torque below the rev limit and their engine
// speeds; the curve's points' speeds rounded to 100 rpm (each above the one before) with the power and
// torque there.
struct EngineFigures {
    F ps, kgfm;                     // +0x00 / +0x04 PS, kgf m
    s16 power_rpm = 0, torque_rpm = 0; // +0x08 / +0x0A
    u16 rev_limit = 0;              // +0x0C the engine block's +0xCC
    std::vector<s16> rpm;           // +0x0E the count, +0x10 the speeds
    std::vector<F> point_ps, point_kgfm; // +0x40 / +0xA0, at the listed speeds
};
// `condition`: the engine condition's factor (0x350868; 1 for a condition block whose byte +0x14 is set,
// as the showroom's and the default condition 0x3433E8 are).
EngineFigures engine_figures(const rules::EngineBlock& e, F condition);

// 0x34EA68: a power-restricted engine's torque t at speed w (rad/s) under its cap (the engine block's
// +0xDB on, +0xDC the cap, +0xE0 its soft share).
F capped_torque(const rules::EngineBlock& e, F t, F w);

// 0x356EA8: the power the driveline adds to the engine's (PS): a car with motors (layout 7) the motors'
// (0x356E08 from 0x355978: twice the first motor's figure and the second's, the program's bytes 0x62045A /
// 0x62045B, over 0.7355); a driveline whose byte +0x13 is 4: 13.596193; else none.
F extra_power(const rules::DrivelineBlock& d, const Program& program);

// 0x3F86E8: the evaluation of a built car (its parameter block in r) with its engine's peak power `ps`
// (with the extra) and peak torque `kgfm`.
F evaluation(const Record& r, F ps, F kgfm);

// A showroom car's figures.
struct ShowroomFigures {
    F power;       // PS: ShowRoomCar +0x1700 (0x364E90 reads it)
    F torque;      // kgf m: +0x1704
    s16 power_rpm = 0, torque_rpm = 0;
    F evaluation;  // P +0xFE0 (0x364F00)
    F mass;        // P +0x48, kg
};

// Builds showroom cars: holds the shared engine's program data the parameter block is made with.
class Showroom {
public:
    Showroom(db::SpecDb& db, const Program& program);
    ~Showroom();
    // The car of an equipment; none when it has no chassis or no engine.
    std::optional<ShowroomFigures> figures(const Equipment& eq) const;
    // A car as sold (sim::stock_car); none when the database has no such car.
    std::optional<ShowroomFigures> stock(const std::string& car_label) const;
    // The parameter block's record of an equipment (record.h), as figures() builds it.
    bool record(const Equipment& eq, Spec& spec, Record& out) const;

private:
    struct Data;
    db::SpecDb& db_;
    const Program& program_;
    std::unique_ptr<Data> data_;
};

} // namespace gt4r::sim
