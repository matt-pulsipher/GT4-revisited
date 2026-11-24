#pragma once

// Gran Turismo 4's showroom car (ShowRoomCar, 0x364760): a car built from its equipment the way the
// menus build one, for its figures and its evaluation (docs/formats/menu.md "The showroom car").
// The car's spec (0x3F3B68, a paved course), its parameter block (GT4's converter 0x361908, sim::build_record, with
// the program's rule block 0x6203C8 and a condition that leaves the engine as it is), then:
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

#include <array>
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

// 0x350868: the engine condition's factor on the engine's torque, from a car's condition block (its record
// +0x498: +0x04 the engine's distance, +0x08 the oil's, m; +0x14 no wear), by the limits the program makes
// once from the static block's bytes +0xD0 .. +0xD8 (0x350678; US 1.01: a new engine from -300,000 m, 0.97
// rising to 1 at 0; 1 to 1,000 km, then down to 0.95 at 11,000 km; the oil 1.05 at -100,000 m, 1 from 0 to
// 5,000 km, 0.95 from 6,000 km). 1 with no wear.
F engine_condition(const Program& program, F engine, F oil, bool no_wear);
// Those limits (0x350678's table, 0x620320 in the program; m): a new engine's distance (+0x00) and loss
// (+0x04), run in to (+0x08), worn at (+0x0C: the most an engine's distance is kept at, 0x3507A8) and its
// loss (+0x10), fresh oil (+0x14) and its gain (+0x18), old oil at (+0x1C: the most the oil's distance is
// kept at, 0x350808), the oil's loss from (+0x20).
struct ConditionLimits {
    F new_engine, new_loss, run_in, worn, worn_loss, fresh_oil, oil_gain, old_oil, oil_from;
};
ConditionLimits condition_limits(const Program& program);

// 0x3F86E8: the evaluation of a built car (its parameter block in r) with its engine's peak power `ps`
// (with the extra) and peak torque `kgfm`.
F evaluation(const Record& r, F ps, F kgfm);

// 0x34B808: a gearbox's chart: per gear (reverse and 1 .. gears) the engine speeds it runs between (the
// lowest: 1,000 rpm for reverse and first, else the down-shift speed into it; the highest: the up-shift speed,
// the rev limit for reverse and the top gear) and the road speeds there (m/s; reverse negative).
struct GearChart {
    int gears = 0;                          // +0x00
    std::array<u16, 10> low_rpm{}, high_rpm{}; // +0x04 / +0x18
    std::array<F, 10> low_speed, high_speed;   // +0x2C / +0x54
};
GearChart gear_chart(const rules::DrivelineBlock& d, const rules::EngineBlock& e);

// The settings pages' charts (0x34B960 for the engine's, 0x34B8E8 for the gears'): the engine's figures with the
// car's condition (the peak power with the driveline's extra) and the gear chart.
struct Charts {
    EngineFigures engine;
    GearChart gears;
};

// A showroom car's figures.
struct ShowroomFigures {
    F power;       // PS: ShowRoomCar +0x1700 (0x364E90 reads it)
    F torque;      // kgf m: +0x1704
    s16 power_rpm = 0, torque_rpm = 0;
    F evaluation;  // P +0xFE0 (0x364F00)
    F mass;        // P +0x48, kg
    s16 weight = 0; // P +0x1048, kg (0x364FA8): the spec's weight by its percentage, with the ballast
};

// Builds showroom cars: holds the shared engine's program data the parameter block is made with.
class Showroom {
public:
    Showroom(db::SpecDb& db, const Program& program);
    ~Showroom();
    // The car of an equipment; none when it has no chassis or no engine.
    // `condition`: engine_condition's factor (ShowRoomCar's own condition block leaves it 1).
    std::optional<ShowroomFigures> figures(const Equipment& eq, F condition = F::raw(0x3F800000)) const;
    // A car as sold (sim::stock_car); none when the database has no such car.
    std::optional<ShowroomFigures> stock(const std::string& car_label) const;
    // The charts of an equipment; `automatic` (0 / 1, -1: as the spec has it) is put in the spec's +0x156 first, as
    // the gear chart's argument does (0x148940).
    std::optional<Charts> charts(const Equipment& eq, F condition, int automatic = -1) const;
    // The parameter block's record of an equipment (record.h), as figures() builds it; `x156` (0 .. 255) put in
    // the spec's +0x156 when not -1.
    bool record(const Equipment& eq, Spec& spec, Record& out, int x156 = -1) const;

private:
    struct Data;
    db::SpecDb& db_;
    const Program& program_;
    std::unique_ptr<Data> data_;
};

} // namespace gt4r::sim
