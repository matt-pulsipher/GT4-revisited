#include "gt4r/sim/equipment.h"

#include <algorithm>
#include <cstring>

namespace gt4r::sim {

using db::PartTable;

Equipment::Equipment() {
    // The cleared record (0x4442B8): every part none, the weight and power percentages 100.
    for (u32 o : {0x00u, 0x08u, 0x10u}) set_part(o, db::kNoPart);
    bytes[0x18] = 0xFF;
    for (u32 o = 0x20; o <= 0x108; o += 8) set_part(o, db::kNoPart);
    bytes[0x160] = 0xFF, bytes[0x161] = 0xFF;
    bytes[WeightPct] = 100, bytes[PowerPct] = 100, bytes[0x164] = 100, bytes[0x166] = 1;
}

db::PartId Equipment::part(u32 slot) const {
    db::PartId v;
    std::memcpy(&v, bytes.data() + slot, 8);
    return v;
}

void Equipment::set_part(u32 slot, db::PartId id) { std::memcpy(bytes.data() + slot, &id, 8); }

namespace {

// 0x4452A8: v into [lo, hi] (an empty range gives hi).
int clamp(int lo, int v, int hi) {
    if (v < lo) v = lo;
    if (hi < v) v = hi;
    return v;
}

} // namespace

// 0x444480 (the parts into their places), 0x4449C0 (each part's default settings) and 0x443240 (a
// computed gearbox's ratios, made by the caller: car_spec.h).
bool stock_equipment(db::SpecDb& db, const std::string& car_label, Equipment& eq) {
    const std::optional<db::CarParts> car = db::car_parts(db, car_label);
    if (!car) return false;
    eq = Equipment();
    eq.set_part(Equipment::Car, db::part_id(u32(PartTable::GenericCar), car->car_id));
    static constexpr std::pair<u32, PartTable> kPlaces[] = {
        {Equipment::Brake, PartTable::Brake},
        {Equipment::BrakeController, PartTable::BrakeController},
        {Equipment::Chassis, PartTable::Chassis},
        {Equipment::Engine, PartTable::Engine},
        {Equipment::Drivetrain, PartTable::Drivetrain},
        {Equipment::Gear, PartTable::Gear},
        {Equipment::Suspension, PartTable::Suspension},
        {Equipment::Lsd, PartTable::Lsd},
        {Equipment::FrontTire, PartTable::FrontTire},
        {Equipment::RearTire, PartTable::RearTire},
        {Equipment::Steer, PartTable::Steer},
        {Equipment::Lightweight, PartTable::Lightweight},
        {Equipment::RacingModify, PartTable::RacingModify},
        {Equipment::PortPolish, PartTable::PortPolish},
        {Equipment::EngineBalance, PartTable::EngineBalance},
        {Equipment::Displacement, PartTable::Displacement},
        {Equipment::Computer, PartTable::Computer},
        {Equipment::NaTune, PartTable::NaTune},
        {Equipment::TurbineKit, PartTable::TurbineKit},
        {Equipment::Flywheel, PartTable::Flywheel},
        {Equipment::Clutch, PartTable::Clutch},
        {Equipment::PropellerShaft, PartTable::PropellerShaft},
        {Equipment::Muffler, PartTable::Muffler},
        {Equipment::Intercooler, PartTable::Intercooler},
        {Equipment::Ascc, PartTable::Ascc},
        {Equipment::Tcsc, PartTable::Tcsc},
    };
    for (const auto& [slot, table] : kPlaces) eq.set_part(slot, car->part(table));
    // The car's row of the paint table and its first paint.
    if (const db::Table* v = db.table("CAR_VARIATION_american"))
        if (const ByteSpan row = v->row(car->car_id); row.size() >= 8) {
            db::PartId id;
            std::memcpy(&id, row.data(), 8);
            eq.set_part(Equipment::Variation, id);
            eq.bytes[0x18] = 0;
        }

    auto row = [&](u32 slot) { return db::part_row(db, eq.part(slot)); };
    // 0x444A88: the gears, the final drive and the slider.
    if (const ByteSpan r = row(Equipment::Gear); r.size() >= 0x2D) {
        eq.set_h(Equipment::GearRatios, rd16(r, 0x16));
        for (u32 i = 0; i < 11; ++i) eq.set_h(Equipment::GearRatios + 2 + 2 * i, rd16(r, 2 * i));
        eq.set_h(Equipment::FinalDrive, u16(clamp(rd16(r, 0x18), rd16(r, 0x1C), rd16(r, 0x1A))));
        eq.set_b(Equipment::GearSlider, u8(clamp(r[0x26], r[0x28], r[0x27])));
        eq.set_h(Equipment::FinalDrive2, eq.h(Equipment::FinalDrive));
    }
    // 0x444B88
    if (const ByteSpan r = row(Equipment::Drivetrain); r.size() >= 0x15) eq.set_b(Equipment::TorqueSplit, u8(clamp(r[0x12], r[0x14], r[0x13])));
    // 0x444C08
    if (const ByteSpan r = row(Equipment::BrakeController); r.size() >= 0xB) {
        eq.set_b(Equipment::BrakeFront, u8(clamp(1, r[6], r[3])));
        eq.set_b(Equipment::BrakeRear, u8(clamp(1, r[0xA], r[7])));
    }
    // 0x444CA0
    if (const ByteSpan r = row(Equipment::RacingModify); r.size() >= 0x1D) {
        eq.set_b(Equipment::DownforceFront, u8(clamp(r[0xD], r[0xF], r[0xE])));
        eq.set_b(Equipment::DownforceRear, u8(clamp(r[0x10], r[0x12], r[0x11])));
    }
    // 0x444D38
    if (const ByteSpan r = row(Equipment::TurbineKit); r.size() >= 0x12)
        for (u32 i = 0; i < 6; ++i) eq.set_b(Equipment::Turbo + i, r[0xA + i]);
    // 0x444DD0
    if (const ByteSpan r = row(Equipment::Suspension); r.size() >= 0x6F) {
        eq.set_b(Equipment::ToeRear, r[0x2C]), eq.set_b(Equipment::ToeFront, r[0x29]);
        eq.set_b(Equipment::CamberFront, u8(clamp(r[0xF], r[0x11], r[0x10])));
        eq.set_b(Equipment::CamberRear, u8(clamp(r[0x12], r[0x14], r[0x13])));
        eq.set_h(Equipment::RideFront, u16(clamp(rd16(r, 0), rd16(r, 4), rd16(r, 2))));
        eq.set_h(Equipment::RideRear, u16(clamp(rd16(r, 6), rd16(r, 0xA), rd16(r, 8))));
        eq.set_b(Equipment::SpringFront, u8(clamp(r[0x34], r[0x36], r[0x35])));
        eq.set_b(Equipment::SpringRear, u8(clamp(r[0x37], r[0x39], r[0x38])));
        eq.set_b(Equipment::MotionFront, r[0x3A]), eq.set_b(Equipment::MotionRear, r[0x3B]);
        static constexpr u8 kDamper[8][2] = {{0x4B, 0x48}, {0x4E, 0x48}, {0x52, 0x4F}, {0x55, 0x4F}, {0x59, 0x56}, {0x5C, 0x56}, {0x60, 0x5D}, {0x63, 0x5D}};
        for (u32 i = 0; i < 8; ++i) eq.set_b(Equipment::Dampers + i, u8(clamp(1, r[kDamper[i][0]], r[kDamper[i][1]])));
        eq.set_b(Equipment::AntiRollFront, u8(clamp(1, r[0x69], r[0x66])));
        eq.set_b(Equipment::AntiRollRear, u8(clamp(1, r[0x6D], r[0x6A])));
    }
    // 0x444FE0
    if (const ByteSpan r = row(Equipment::Lsd); r.size() >= 0x1B) {
        static constexpr u8 kLsd[6] = {0x04, 0x10, 0x08, 0x14, 0x0C, 0x18}; // each {min, max, default}
        for (u32 i = 0; i < 6; ++i) eq.set_b(Equipment::LsdSettings + i, u8(clamp(r[kLsd[i]], r[kLsd[i] + 2], r[kLsd[i] + 1])));
    }
    // 0x4450D8
    if (const ByteSpan r = row(Equipment::Ascc); r.size() >= 0x19) {
        static constexpr u8 kAscc[4][2] = {{0x07, 0x04}, {0x0D, 0x0A}, {0x12, 0x0F}, {0x18, 0x15}};
        for (u32 i = 0; i < 4; ++i) eq.set_b(Equipment::AsccIndices + i, u8(clamp(1, r[kAscc[i][0]], r[kAscc[i][1]])));
    }
    // 0x4451A0
    if (const ByteSpan r = row(Equipment::Tcsc); r.size() >= 0xA) eq.set_b(Equipment::TcscIndex, u8(clamp(1, r[9], r[6])));
    // 0x445220
    eq.set_b(Equipment::Ballast, 0), eq.set_b(Equipment::BallastPlace, 0);
    return true;
}

} // namespace gt4r::sim
