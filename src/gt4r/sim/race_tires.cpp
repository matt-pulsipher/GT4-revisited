#include "gt4r/sim/race_tires.h"

namespace gt4r::sim {

bool fit_race_tires(db::SpecDb& db, const FileReader& read, Equipment& eq, int course_kind, int chosen) {
    const bool asked = chosen >= 0;
    int kind[2] = {asked ? chosen : int(tire_kind(db, eq, 0)), asked ? chosen : int(tire_kind(db, eq, 1))};
    const int own[2] = {kind[0], kind[1]};
    if (course_kind == 2) kind[0] = kind[1] = 11;
    else if (course_kind == 3) kind[0] = kind[1] = 12;
    if (!asked && own[0] == kind[0] && own[1] == kind[1]) return false;
    const std::vector<FittingPart> parts = fitting_parts(read, db, eq.part(Equipment::Car));
    bool changed = false;
    for (int axle = 0; axle < 2; ++axle) {
        const db::PartId id = kind[axle] < 0 || kind[axle] > 12 ? db::kNoPart
                                                                 : fitting_part(parts, u32(axle == 0 ? db::PartTable::FrontTire : db::PartTable::RearTire), u8(kind[axle]));
        const u32 slot = axle == 0 ? Equipment::FrontTire : Equipment::RearTire;
        if (id == db::kNoPart || id == eq.part(slot)) continue;
        eq.set_part(slot, id), changed = true;
    }
    return changed;
}

bool fit_race_tires(db::SpecDb& db, const FileReader& read, Equipment& eq, int course_kind, int front, int rear) {
    const int own[2] = {int(tire_kind(db, eq, 0)), int(tire_kind(db, eq, 1))};
    int kind[2] = {front, rear};
    if (course_kind == 2) kind[0] = kind[1] = 11;
    else if (course_kind == 3) kind[0] = kind[1] = 12;
    if (own[0] == kind[0] && own[1] == kind[1]) return false;
    const std::vector<FittingPart> parts = fitting_parts(read, db, eq.part(Equipment::Car));
    bool changed = false;
    for (int axle = 0; axle < 2; ++axle) {
        const db::PartId id = kind[axle] < 0 || kind[axle] > 12 ? db::kNoPart
                                                                 : fitting_part(parts, u32(axle == 0 ? db::PartTable::FrontTire : db::PartTable::RearTire), u8(kind[axle]));
        const u32 slot = axle == 0 ? Equipment::FrontTire : Equipment::RearTire;
        if (id == db::kNoPart || id == eq.part(slot)) continue;
        eq.set_part(slot, id), changed = true;
    }
    return changed;
}

} // namespace gt4r::sim
