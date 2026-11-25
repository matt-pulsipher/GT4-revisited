#pragma once

// A pit stop's tire change as the program makes it (docs/specs/ai.md "The pits", physics.md §4.3): the race prepares, per
// car, the tires its pit can fit (0x35C4D8); at the stop's 645th step from its end, with a tire kind chosen (F +0x64E /
// +0x64F below 0xFE), the stop's timing (0x35C0B8) takes a copy of the entry's equipment, puts the chosen kinds' parts on
// it (0x35C6D8, 0x4468F8), builds its spec (0x3F3B68, the course's kind) and runs the converter's tire part on the car's
// parameter block for the axles served (0x361570); at the 644th the records' base grips (0x361798). The converter itself
// does not run again, and the entry's own equipment is not changed (a later stop starts again from it).

#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/convert.h"
#include "gt4r/sim/equipment.h"
#include "gt4r/sim/program.h"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::sim {

// A file of the disc by its path ("specdb/<set>/CARS/..."), as db::SpecDb::Reader.
using FileReader = std::function<std::optional<Bytes>(const std::string& path)>;

// The parts that fit a car (0x449E10: the set's file CARS/<the car's part code, 16 hex digits>: u64 count, then 16 bytes
// each: u64 the part, u8 its kind, 7 bytes), in the file's order (by table).
struct FittingPart {
    db::PartId part = db::kNoPart;
    u8 kind = 0;
};
std::vector<FittingPart> fitting_parts(const FileReader& read, const db::SpecDb& db, db::PartId car);

// 0x44A048: the fitting part of `table` whose kind is `kind` (the search stops at the first part of a later table);
// kNoPart when there is none.
db::PartId fitting_part(const std::vector<FittingPart>& parts, u32 table, u8 kind);

// 0x4462E0 / 0x446320: the kind of the tire an equipment has on an axle (0 front, 1 rear): the FRONTTIRE row's byte +0x3A,
// the REARTIRE row's +0x38 (0 .. 12); 0xFF with no tire part.
u8 tire_kind(db::SpecDb& db, const Equipment& eq, int axle);

// What the pit can fit on a car (0x35C4D8, the race's preparation; the conductor's bytes per car i): per axle the kind
// fitted (+0xFB20 + 2 i + axle), the kinds it may choose (+0xF928 + 4 i + 2 axle: the least and the most, the program's
// bytes 0x620860 / 0x620870 by the kind fitted: 0 .. 2, 3 .. 5, 6 .. 10, 11, 12) and each kind's part from the car's
// fitting parts (+0xF940 + 0x50 i + 0x28 axle + 8 (kind - least): FRONTTIRE / REARTIRE by its kind; -1 none).
struct PitTires {
    std::array<u8, 2> fitted{0xFF, 0xFF};
    std::array<u8, 2> least{}, most{};
    std::array<std::array<db::PartId, 5>, 2> parts{{{db::kNoPart, db::kNoPart, db::kNoPart, db::kNoPart, db::kNoPart},
                                                    {db::kNoPart, db::kNoPart, db::kNoPart, db::kNoPart, db::kNoPart}}};
    // 0x35C6D8: the part of `kind` on `axle` (0 front, 1 rear); kNoPart for a kind outside the least .. most.
    db::PartId part(int axle, int kind) const;
};
PitTires pit_tires(db::SpecDb& db, const Program& program, const Equipment& eq, const std::vector<FittingPart>& parts);

// 0x35C0B8 at 645 with a choice: each axle's chosen kind (F +0x64E front, +0x64F rear; 0xFF none, 0xFE the kind fitted)
// whose part the pit has (0x35C6D8) put on the equipment (0x4468F8: +0x60 / +0x68) and made the kind fitted (the stops'
// record takes it too, conductor + 0xDA20 + 8 i + axle). Answers the axles changed (bit 0 the front, 1 the rear).
u8 fit_chosen_tires(Equipment& eq, PitTires& t, u8 front, u8 rear);

// 0x35C0B8 at 645, the rest: the equipment's spec (0x3F3B68, the course's kind as 0x34C968 reads it) and the converter's
// tire part on it (0x361570) for `axles` (the axles served, F +0x4B8: 1 the front 0x40, 2 the rear 0x80, 3 both). The spec
// built is left in `spec` when given.
void change_tire_records(db::SpecDb& db, const Program& program, const Equipment& eq, int course_kind, ParamBlock& p, u32 axles,
                         Spec* spec = nullptr);

} // namespace gt4r::sim
