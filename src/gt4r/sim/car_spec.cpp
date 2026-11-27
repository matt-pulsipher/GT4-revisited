// The spec builder: docs/specs/physics.md "The spec". Each helper is the program's routine named at
// it; R is the part's row, E the equipment, S the spec.

#include "gt4r/sim/car_spec.h"

#include "base/ee_float.h"
#include "gt4r/sim/convert.h"
#include "sim/car_spec.h" // (the shared engine's gear ratio generator: Prologue's for now)

#include <cstring>

namespace gt4r::sim {

namespace {

using E = Equipment;

// A row padded with zeros to `n` bytes (a row the database lacks reads as zeros).
struct Row {
    std::array<u8, 0xD0> d{};
    bool found = false;
    Row(db::SpecDb& db, db::PartId id) {
        const ByteSpan r = db::part_row(db, id);
        found = !r.empty();
        std::memcpy(d.data(), r.data(), std::min(r.size(), d.size()));
    }
    u8 b(u32 o) const { return d[o]; }
    u16 h(u32 o) const { return u16(d[o] | (d[o + 1] << 8)); }
    db::PartId id(u32 o) const {
        db::PartId v;
        std::memcpy(&v, d.data() + o, 8);
        return v;
    }
    float f(u32 o) const {
        float v;
        std::memcpy(&v, d.data() + o, 4);
        return v;
    }
};

// 0x4452C0: a setting index (1 .. steps) into the part's range; integer division.
int interp(int steps, int lo, int hi, int index) {
    if (steps < 2) return lo;
    return lo + (hi - lo) * (index - 1) / (steps - 1);
}

// A percentage of a spec word, truncated (the builder's v = v x b / 100).
void scale_h(Spec& s, u32 o, int pct, bool is_signed = false) { s.set_h(o, u32((is_signed ? int(s.sh(o)) : int(s.h(o))) * pct / 100)); }
void scale_b(Spec& s, u32 o, int pct) { s.set_b(o, u32(int(s.b(o)) * pct / 100)); }
// The two engine factors: +0x10A (per mille, the top of the curve) and +0x10C (percent, its start).
void power_factors(Spec& s, int high, int low) { scale_h(s, 0x10A, high), scale_b(s, 0x10C, low); }

// 0x442490: a tire block from a compound row C and a force-volume row V.
void tire_block(Spec& s, u32 t, const Row& c, const Row& v) {
    auto put = [&](u32 o, u32 x) { s.set_b(t + o, x); };
    put(0x00, c.b(0x01)), put(0x01, c.b(0x00)), put(0x02, c.b(0x0A));
    put(0x08, 8); // curve A's points
    for (u32 i = 0; i < 16; ++i) put(0x10 + i, c.b(0x0B + i));
    put(0x09, 6); // the slip curves' points below zero
    for (u32 i = 0; i < 6; ++i) put(0x20 + i, c.b(0x27 + i)), put(0x30 + i, c.b(0x2D + i)), put(0x40 + i, c.b(0x39 + i));
    put(0x0A, 6); // and above
    for (u32 i = 0; i < 6; ++i) put(0x28 + i, c.b(0x3F + i)), put(0x38 + i, c.b(0x45 + i)), put(0x48 + i, c.b(0x51 + i));
    put(0x0B, 4); // the load curve's points
    for (u32 i = 0; i < 8; ++i) put(0x5C + i, c.b(0x02 + i));
    put(0x0C, 6); // curve B's points
    for (u32 i = 0; i < 12; ++i) put(0x50 + i, c.b(0x1B + i));
    for (u32 i = 0; i < 3; ++i) put(0x03 + i, c.b(0x57 + i));
    for (u32 i = 0; i < 10; ++i) put(0x64 + i, v.b(i));    // surfaces 0 .. 9
    for (u32 i = 0; i < 5; ++i) put(0x6E + i, v.b(0x0D + i)); // surfaces 10 .. 14
    put(0x78, v.b(0x0A)), put(0x79, v.b(0x0C)), put(0x7A, v.b(0x0B)); // surfaces 20, 21, 22
}

// 0x442218 / 0x442358: an axle's three tire blocks and its tire size.
// (Prologue's 0x406258 / 0x406400: the compounds of another tire row, `grip`.)
void tires(db::SpecDb& db, Spec& s, const Row& r, int axle, db::PartId grip) {
    const std::optional<Row> other = grip != db::kNoPart ? std::optional<Row>(Row(db, grip)) : std::nullopt;
    for (int i = 0; i < 3; ++i) {
        const Row compound(db, (other ? *other : r).id(8 + 8 * u32(i))), volume(db, r.id(0x20 + 8 * u32(i)));
        tire_block(s, Spec::tire_block(3 * axle + i), compound, volume);
    }
    const Row size(db, r.id(0));
    s.set_b(0x1CA + u32(axle), size.b(2)), s.set_b(0x1CC + u32(axle), size.b(0)), s.set_b(0x1C8 + u32(axle), size.b(3));
    s.set_b(0x1CE + u32(axle), 4);
}

// 0x4413C8.
void builder(db::SpecDb& db, const Program& program, const Equipment& eq, Spec& s) {
    auto row = [&](u32 slot) { return Row(db, eq.part(slot)); };
    auto has = [&](u32 slot) { return eq.part(slot) != db::kNoPart; };
    s.set_b(0x10C, 100), s.set_h(0x10A, 1000), s.set_b(0x113, 0);
    { // 0x441950 BRAKE
        const Row r = row(E::Brake);
        s.set_b(0x178, r.b(3)), s.set_b(0x179, r.b(4)), s.set_b(0x17A, r.b(5));
    }
    { // 0x441970 BRAKECONTROLLER
        const Row r = row(E::BrakeController);
        s.set_b(0x1A8, u32(interp(r.b(3), r.b(4), r.b(5), eq.b(E::BrakeFront))));
        s.set_b(0x1A9, u32(interp(r.b(7), r.b(8), r.b(9), eq.b(E::BrakeRear))));
    }
    { // 0x4419E0 STEER: three curves of six speeds and six limits
        const Row r = row(E::Steer);
        s.set_b(0x180, 6), s.set_b(0x181, 6), s.set_b(0x182, 6);
        s.set_b(0x28, r.b(0x25));
        for (u32 i = 0; i < 12; ++i) s.set_b(0x184 + i, r.b(0x01 + i)), s.set_b(0x19C + i, r.b(0x0D + i)), s.set_b(0x190 + i, r.b(0x19 + i));
    }
    const Row front_tire = row(E::FrontTire), rear_tire = row(E::RearTire);
    { // 0x441B20 CHASSIS
        const Row r = row(E::Chassis);
        s.set_b(0x22, r.b(0x1C));
        s.set_b(0x2A, r.b(0x0E) + 0x7Cu), s.set_b(0x2B, r.b(0x10) + 0x7Cu);
        s.set_h(0x1A, r.h(4)), s.set_h(0x20, r.h(6)), s.set_h(0x3A, r.h(6)), s.set_h(0x10, r.h(0));
        s.set_b(0x24, r.b(0x1D)), s.set_b(0x27, r.b(0x1E)), s.set_b(0x02, r.b(0x1F));
        for (u32 i = 0; i < 4; ++i) s.set_h(0x30 + 2 * i, r.h(0x12 + 2 * i));
        s.set_b(0x38, r.b(0x22) + u32(eq.b(E::GripAdd))), s.set_b(0x39, r.b(0x23) + u32(eq.b(E::GripAdd)));
        // Tire kinds 6 .. 10 (the tire rows' last byte) take the chassis' two tire bytes.
        if (u32(front_tire.b(0x3A)) - 6 < 5) s.set_b(0x1E2, r.b(0x20)), s.set_b(0x1E4, r.b(0x21));
        if (u32(rear_tire.b(0x38)) - 6 < 5) s.set_b(0x1E3, r.b(0x20)), s.set_b(0x1E5, r.b(0x21));
    }
    { // 0x441C58 ENGINE
        const Row r = row(E::Engine);
        for (u32 i = 0; i < 24; ++i) s.set_h(0xD8 + 2 * i, r.h(0x1E + 2 * i)), s.set_b(0xC0 + i, r.b(0x55 + i));
        s.set_h(0x108, u32(r.b(0x6F)) * 10);
        s.set_b(0x160, r.b(0x53)), s.set_b(0xBC, r.b(0x52)), s.set_b(0xBD, r.b(0x51)), s.set_b(0xBE, r.b(0x50)), s.set_b(0xBF, r.b(0x54));
        s.set_b(0x110, r.b(0x6D)), s.set_b(0x111, r.b(0x6E));
    }
    { // 0x441E28 DRIVETRAIN
        const Row r = row(E::Drivetrain);
        s.set_b(0x12A, r.b(0x14)), s.set_b(0x128, r.b(0x10)), s.set_b(0x129, r.b(0x11));
        s.set_h(0xBA, r.h(2)), s.set_h(0x174, r.h(4)), s.set_h(0x176, r.h(6)), s.set_h(0x164, r.h(8)), s.set_h(0x166, r.h(0xA)), s.set_h(0xB8, r.h(0));
    }
    { // 0x441E78 GEAR: the ratios are the equipment's
        const Row r = row(E::Gear);
        s.set_h(0x152, r.h(0x1E)), s.set_b(0x154, r.b(0x29));
        for (u32 i = 0; i < 12; ++i) s.set_h(0x138 + 2 * i, eq.h(E::GearRatios + 2 * i));
        s.set_h(0x150, eq.h(E::FinalDrive)), s.set_b(0x157, eq.b(E::GearSlider));
        s.set_b(0x156, 0), s.set_b(0x155, r.b(0x24)), s.set_b(0x158, r.b(0x23));
    }
    { // 0x441EE0 SUSPENSION
        const Row r = row(E::Suspension);
        s.set_b(0x92, r.b(0x2D)), s.set_b(0x93, r.b(0x2E)), s.set_b(0x94, r.b(0x31)), s.set_b(0x95, r.b(0x32));
        s.set_b(0x5A, r.b(0x3C)), s.set_b(0x72, r.b(0x3D));
        for (u32 i = 0; i < 4; ++i) {
            s.set_b(0x1D0 + i, r.b(0x17 + i)), s.set_b(0x1D8 + i, r.b(0x1B + i));
            s.set_b(0x1D4 + i, r.b(0x1F + i)), s.set_b(0x1DC + i, r.b(0x23 + i));
            s.set_b(0x5C + 2 * i, r.b(0x40 + 2 * i)), s.set_b(0x74 + 2 * i, r.b(0x41 + 2 * i));
        }
        s.set_b(0x90, r.b(0x15)), s.set_b(0x91, r.b(0x16));
        s.set_b(0x5B, r.b(0x64)), s.set_b(0x73, r.b(0x65));
        s.set_h(0x66, r.b(0x2F)), s.set_h(0x7E, r.b(0x30));
        s.set_b(0x65, r.b(0x3E)), s.set_b(0x7D, r.b(0x3F));
        s.set_h(0x9A, eq.h(E::RideFront)), s.set_h(0x9C, eq.h(E::RideRear));
        s.set_b(0x88, eq.b(E::CamberFront)), s.set_b(0x89, eq.b(E::CamberRear));
        s.set_b(0x8E, eq.b(E::ToeFront)), s.set_b(0x8F, eq.b(E::ToeRear));
        s.set_b(0x58, eq.b(E::SpringFront)), s.set_b(0x70, eq.b(E::SpringRear));
        s.set_b(0x8C, eq.b(E::MotionFront)), s.set_b(0x8D, eq.b(E::MotionRear));
        // The dampers: the front's rebound stage takes the bound stage's indices.
        const u32 d = E::Dampers;
        s.set_b(0x5D, u32(interp(r.b(0x48), r.b(0x49), r.b(0x4A), eq.b(d + 0))));
        s.set_b(0x5F, u32(interp(r.b(0x48), r.b(0x4C), r.b(0x4D), eq.b(d + 1))));
        s.set_b(0x61, u32(interp(r.b(0x4F), r.b(0x50), r.b(0x51), eq.b(d + 0))));
        s.set_b(0x63, u32(interp(r.b(0x4F), r.b(0x53), r.b(0x54), eq.b(d + 1))));
        s.set_b(0x75, u32(interp(r.b(0x56), r.b(0x57), r.b(0x58), eq.b(d + 4))));
        s.set_b(0x77, u32(interp(r.b(0x56), r.b(0x5A), r.b(0x5B), eq.b(d + 5))));
        s.set_b(0x79, u32(interp(r.b(0x5D), r.b(0x5E), r.b(0x5F), eq.b(d + 6))));
        s.set_b(0x7B, u32(interp(r.b(0x5D), r.b(0x61), r.b(0x62), eq.b(d + 7))));
        s.set_b(0x59, u32(interp(r.b(0x66), r.b(0x67), r.b(0x68), eq.b(E::AntiRollFront))));
        s.set_b(0x71, u32(interp(r.b(0x6A), r.b(0x6B), r.b(0x6C), eq.b(E::AntiRollRear))));
        s.set_b(0xA8, r.b(0x6E));
    }
    { // 0x4421D0 LSD
        const Row r = row(E::Lsd);
        s.set_b(0x168, r.b(0x03)), s.set_b(0x169, r.b(0x0F));
        for (u32 i = 0; i < 6; ++i) s.set_b(0x16A + i, eq.b(E::LsdSettings + i));
    }
    tires(db, s, front_tire, 0, eq.grip_front);
    tires(db, s, rear_tire, 1, eq.grip_rear);
    if (has(E::Lightweight)) { // 0x4427B8
        const Row r = row(E::Lightweight);
        s.set_h(0x20, u32(int(s.sh(0x20)) * int(r.h(0)) / 1000)), scale_b(s, 0x24, r.b(5));
    }
    { // 0x442800 RACINGMODIFY
        const Row r = row(E::RacingModify);
        s.set_h(0x20, u32(int(s.sh(0x20)) * int(r.h(0)) / 1000)), scale_b(s, 0x24, r.b(0x17));
        s.set_b(0x42, r.b(0x0C)), s.set_h(0x1C, r.h(2)), s.set_h(0x1E, r.h(4));
        s.set_b(0x43, eq.b(E::DownforceFront)), s.set_b(0x44, eq.b(E::DownforceRear));
        for (u32 i = 0; i < 4; ++i) s.set_b(0x96 + i, r.b(0x13 + i));
        for (u32 i = 0; i < 5; ++i) s.set_b(0x4C + i, r.b(0x18 + i));
    }
    if (has(E::PortPolish)) { // 0x4428B8
        const Row r = row(E::PortPolish);
        power_factors(s, r.h(0), r.h(2));
    }
    // 0x442900 / 0x442AE8: the curve's speeds and the rev limit moved up, then the factors.
    auto rpm_tune = [&](const Row& r, u32 factors, u32 shift) {
        for (u32 i = 0; i < 16; ++i) s.set_b(0xC0 + i, s.b(0xC0 + i) + u32(r.b(shift)));
        s.set_b(0xBD, s.b(0xBD) + u32(r.b(shift + 1)));
        power_factors(s, r.h(factors), r.h(factors + 2));
    };
    if (has(E::EngineBalance)) rpm_tune(row(E::EngineBalance), 0, 7);
    if (has(E::Displacement)) { // 0x442A58
        const Row r = row(E::Displacement);
        power_factors(s, r.h(0), r.h(2));
    }
    if (has(E::Computer)) { // 0x442AA0
        const Row r = row(E::Computer);
        power_factors(s, r.h(0), r.h(2));
    }
    if (has(E::NaTune)) rpm_tune(row(E::NaTune), 4, 9);
    { // 0x442C40 TURBINEKIT
        const Row r = row(E::TurbineKit);
        power_factors(s, r.h(4), r.h(6));
        s.set_b(0x113, r.b(9));
        s.set_b(0xBD, s.b(0xBD) + u32(r.b(0x10))), s.set_b(0xBE, s.b(0xBE) + u32(r.b(0x11)));
        static constexpr u16 kBoost[6] = {0x118, 0x116, 0x11A, 0x119, 0x117, 0x11B};
        for (u32 i = 0; i < 6; ++i) s.set_b(kBoost[i], eq.b(E::Turbo + i));
    }
    // 0x442CE0 / 0x442D60 / 0x442DE8: the driveline's inertias.
    if (has(E::Flywheel)) {
        const Row r = row(E::Flywheel);
        scale_h(s, 0xBA, r.b(4)), scale_h(s, 0xB8, r.b(3)), scale_h(s, 0x174, r.b(5), true), scale_h(s, 0x176, r.b(6), true);
    }
    if (has(E::Clutch)) {
        const Row r = row(E::Clutch);
        scale_h(s, 0xBA, r.b(4)), scale_h(s, 0xB8, r.b(3)), scale_h(s, 0x174, r.b(5), true), scale_h(s, 0x176, r.b(6), true);
        s.set_b(0x161, r.b(7));
    }
    if (has(E::PropellerShaft)) {
        const Row r = row(E::PropellerShaft);
        scale_h(s, 0xBA, r.b(3)), scale_h(s, 0x174, r.b(4), true), scale_h(s, 0x176, r.b(5), true);
        scale_h(s, 0x164, r.b(6), true), scale_h(s, 0x166, r.b(7), true);
    }
    for (u32 slot : {u32(E::Muffler), u32(E::Intercooler), u32(E::Supercharger)}) // 0x442E80, 0x442EC8, 0x4431F8
        if (has(slot)) {
            const Row r = row(slot);
            power_factors(s, r.h(0), r.h(2));
        }
    { // 0x442F10 ASCC
        const Row r = row(E::Ascc);
        const int a = eq.b(E::AsccIndices), c = eq.b(E::AsccIndices + 2);
        s.set_b(0x1BA, u32(interp(r.b(4), r.b(5), r.b(6), a ? a : 1)));
        s.set_b(0x1BB, r.b(8));
        s.set_b(0x1BC, u32(interp(r.b(0xA), r.b(0xB), r.b(0xC), eq.b(E::AsccIndices + 1))));
        s.set_b(0x1BD, u32(interp(r.b(0xF), r.b(0x10), r.b(0x11), c ? c : 1)));
        s.set_b(0x1BE, r.b(0x13));
        s.set_b(0x1BF, u32(interp(r.b(0x15), r.b(0x16), r.b(0x17), eq.b(E::AsccIndices + 3))));
        u32 flags = (r.b(3) & 3u) | ((r.b(9) & 3u) << 2) | ((r.b(0xE) & 3u) << 4) | ((r.b(0x14) & 3u) << 6);
        if (a == 0) flags &= 0xF0;                                               // 0x3F16C0
        if (c == 0) flags &= program.b(Program::kAsccSwitch) != 0 ? 0xCF : 0x0F; // 0x3F16D0
        s.set_b(0x1B8, flags);
    }
    { // 0x443040 TCSC
        const Row r = row(E::Tcsc);
        const int t = eq.b(E::TcscIndex);
        s.set_b(0x1C2, r.b(5)), s.set_b(0x1C0, r.b(3)), s.set_b(0x1C1, r.b(4));
        s.set_b(0x1C3, u32(interp(r.b(6), r.b(7), r.b(8), t ? t : 1)));
        if (t == 0) s.set_b(0x1C2, 0); // 0x3F1748
    }
    s.set_b(0x05, eq.b(E::Ballast)), s.set_b(0x06, eq.b(E::BallastPlace)); // 0x4430B8
    if (has(E::Nos)) { // 0x4430D8
        const Row r = row(E::Nos);
        s.set_b(0x11D, eq.b(E::NosSetting)), s.set_h(0x11E, r.h(0));
    }
    // 0x4458D0 / 0x4430F0: the body's box and frontal area from the car's paint row (millimetres as floats; square metres).
    if (const db::Table* v = db.table("VARIATIONamerican")) {
        size_t first = 0;
        const db::PartId id = eq.part(E::Variation);
        if (id != db::kNoPart && v->rows(db::part_row_id(id), first) != 0) {
            const ByteSpan r = v->row_at(first);
            auto f = [&](u32 o) {
                float x = 0;
                if (o + 4 <= r.size()) std::memcpy(&x, r.data() + o, 4);
                return x;
            };
            auto mm = [&](u32 o) { return u32(f(o) < 0 ? 0 : s64(f(o))); };
            s.set_h(0x16, mm(0x14)), s.set_h(0x18, mm(0x10)), s.set_h(0x12, mm(0x18)), s.set_h(0x14, mm(0x1C));
            s.set_h(0x40, u32(s32(f(0x20) * 10000.0f)));
        }
    }
    s.set_b(0x25, s.b(0x24)), s.set_b(0x26, s.b(0x24) >> 1);
    const Row car = row(E::Car);
    { // 0x4461F8: by the car's kind byte
        u32 kind = 0;
        switch (car.b(0x11)) {
        case 1: kind = 3; break;
        case 2: kind = 2; break;
        case 0:
        case 3: kind = has(E::Muffler) && row(E::Muffler).b(6) > 0 ? 1 : 0; break;
        default: break;
        }
        s.set_b(0x120, car.found ? kind : 0);
    }
    s.set_b(0x01, car.b(0x13)), s.set_b(0x04, car.b(0x14));
    for (u32 i = u32(s.b(0x155)) + 1; i < 12; ++i) s.set_h(0x138 + 2 * i, 0xFFFF);
    for (u32 i = s.b(0xBF); i < 16; ++i) s.set_b(0xC0 + i, 0xFF), s.set_h(0xD8 + 2 * i, 0);
    s.set_b(0x07, eq.b(E::PowerPct)), s.set_b(0x08, eq.b(E::WeightPct));
    if (eq.h(E::OutputFactor) != 0) s.set_h(0x108, eq.h(E::OutputFactor));
}

} // namespace

u8 aid_switches(Spec& s, const Program& program, bool asm_on, bool tcs_on, bool wheel_aid, bool loose) {
    if (loose) asm_on = tcs_on = false;
    const bool ascc = program.b(Program::kAsccSwitch) != 0; // (the settings' ASCC switch: the channels' bits 6 and 7 apart)
    if (!asm_on) s.set_b(0x1B8, ascc ? s.b(0x1B8) & 0xC0 : 0);
    if (!tcs_on) s.set_b(0x1C2, 0);
    if ((ascc ? s.b(0x1B8) & 0x3F : s.b(0x1B8)) == 0) asm_on = false;
    if (s.b(0x1C2) == 0) tcs_on = false;
    u8 bits = 0;
    if (tcs_on) bits |= 1;
    if (asm_on) bits |= 4;
    switch (program.b(0x620E09)) {
    case 1: bits |= tcs_on ? 2 : 0; break;
    case 2: bits |= asm_on ? 2 : 0; break;
    case 3: bits |= 2; break;
    default: break;
    }
    if (wheel_aid) bits |= 8;
    switch (program.b(0x620E0B)) {
    case 1: bits |= asm_on ? 0x10 : 0; break;
    case 2: bits |= 0x10; break;
    default: break;
    }
    return bits;
}

void build_spec(db::SpecDb& db, const Program& program, const Equipment& eq, int course_kind, Spec& s) {
    s.bytes.fill(0);
    builder(db, program, eq, s);
    // 0x3F3B68's fix-ups.
    s.set_b(0x00, 2);
    // (Gran Turismo 4's defaults: none on a Prologue program, whose own builder's are its core_spec's)
    if (s.b(0x11C) == 0 && program.known()) s.set_b(0x11C, program.b(Program::kSpec11CDefault));
    if (s.b(0x112) == 0 && program.known()) s.set_b(0x112, program.b(Program::kSpec112Default));
    if (s.b(0x01) >= 2 && s.b(0x01) <= 4) s.set_b(0x11C, 0), s.set_b(0x112, 0);
    if (s.b(0x01) == 2 && (course_kind == 2 || course_kind == 3)) { // a rally car on a loose course
        const u32 t = Program::kLooseSetup;
        s.set_b(0xA8, 3);
        s.set_h(0x66, s.h(0x66) + u32(program.b(t + 1))), s.set_h(0x7E, s.h(0x7E) + u32(program.b(t + 1)));
        s.set_b(0x92, s.b(0x92) + u32(program.b(t + 2))), s.set_b(0x93, s.b(0x93) + u32(program.b(t + 2)));
        s.set_h(0x9A, s.h(0x9A) + u32(program.b(t))), s.set_h(0x9C, s.h(0x9C) + u32(program.b(t)));
        s.set_b(0x94, s.b(0x94) + u32(program.b(t + 3))), s.set_b(0x95, s.b(0x95) + u32(program.b(t + 3)));
        s.set_b(0x58, program.b(t + 4)), s.set_b(0x70, program.b(t + 5));
    }
    for (u32 o : {0x12u, 0x14u})
        if (s.sh(o) == 0) s.set_h(o, u32(int(s.sh(0x10)) / 2));
    if (const u8 p = program.known() ? program.b(Program::kPitchInertiaPct) : 0) s.set_b(0x25, u32(s.b(0x24)) * p / 100);
    if (const u8 p = program.known() ? program.b(Program::kRollInertiaPct) : 0) s.set_b(0x26, u32(s.b(0x24)) * p / 100);
    // Europe's builder (0x3F3E70 there) then raises the yaw, pitch and roll inertia bytes to the static block's +0x153 ..
    // +0x155 where those are set (0, 0, 4: the roll's at least 4). v1.01 and Japan's have no such step.
    if (program.release() == Release::Europe)
        for (u32 k = 0; k < 3; ++k)
            if (const u8 least = program.b(Program::kStatic + 0x153 + k); least && s.b(0x24 + k) < least) s.set_b(0x24 + k, least);
    auto default_curve = [&](u32 x, u32 y, u32 from) {
        for (u32 i = 0; i < 6; ++i) s.set_b(x + i, program.b(from + i)), s.set_b(y + i, program.b(from + 8 + i));
    };
    if (s.b(0x182) == 0) s.set_b(0x182, 6), default_curve(0x19C, 0x1A2, Program::kSteerCurve);
    if (s.b(0x195) == 0) s.set_b(0x181, 6), default_curve(0x190, 0x196, Program::kSteerCurve);
    if (s.b(0x1AA) == 0) s.set_b(0x1AA, 0x8C);
    if (s.b(0x1AB) == 0) s.set_b(0x1AB, 100);
    if (s.b(0x1C3) == 100) s.set_b(0x1C2, 0);
    // A surface's tire block class (1 loose, 2 wet), from +0x1F0.
    s.set_b(0x1E0, 1);
    for (u32 o : {0x1F2u, 0x1F3u, 0x1F4u, 0x1F5u, 0x1FDu, 0x1FEu, 0x1FFu}) s.set_b(o, 1);
    for (u32 o : {0x1F6u, 0x204u, 0x205u, 0x206u, 0x207u}) s.set_b(o, 2);
    // The tire blocks' defaults.
    const u32 first = Spec::tire_block(0);
    for (int k = 0; k < 6; ++k) {
        const u32 t = Spec::tire_block(k);
        for (u32 i = 0; i < 5; ++i)
            if (s.b(t + 0x6E + i) == 0) s.set_b(t + 0x6E + i, 100);
        if (s.b(t + 0x73) == 0) s.set_b(t + 0x73, 0x50);
    }
    if (s.b(first + 0x78) == 0) s.set_b(first + 0x78, s.b(first + 0x64));
    if (s.b(first + 0x79) == 0) s.set_b(first + 0x79, s.b(first + 0x65));
    if (s.b(first + 0x7A) == 0) s.set_b(first + 0x7A, s.b(first + 0x6D));
    for (int k = 0; k < 6; ++k) {
        const u32 t = Spec::tire_block(k);
        if (s.b(t + 0x02) == 0) s.set_b(t + 0x02, 0x2D);
        if (s.b(t + 0x0C) == 0) s.set_b(t + 0x0C, 6), default_curve(t + 0x50, t + 0x56, Program::kTireCurveB);
        for (u32 i = 0; i < 0x20; ++i)
            if (s.b(t + 0x64 + i) == 0) s.set_b(t + 0x64 + i, s.b(first + 0x64 + i));
    }
}

// 0x354FF8 (from 0x443240): a made gearbox's ratios x 1000 from the spec's gears (+0x155), rev byte (+0xBD), final drive
// (+0x150, x 0.001) and the driven tire's radius, and the slider x 10 (km/h): the top gear's ratio from the speed the slider
// gives at the rev byte's rpm; each gear below by the steps of the program's table 0x620448 (6 bytes a row, the rows of 5, 6
// and 7 gears and more), eased between a row's pairs by (slider x 10 - 150) / 250; first gear once more by the third pair;
// reverse (3 first + second) / 4.
void gear_ratios(const Spec& s, const Program& program, int slider10, gt3r::s16 out[8]) {
    using gt3r::ee::F;
    using gt3r::ee::int_to_f;
    const int n = s.b(0x155);
    const u32 row = 0x62042A + 6 * u32(n < 5 ? 5 : n < 8 ? n : 7);
    const F k200 = F::raw(0x43480000);
    auto step = [&](u32 k) { return k200 / int_to_f(program.b(row + k)); };
    const F t = int_to_f(slider10 - 150) / F::raw(0x437A0000);
    F r = int_to_f(100 * s32(s.b(0xBD))) * F::raw(0x3DD6774E); // (rpm to rad/s)
    r = r * driven_tire_radius(s, program);
    r = r / (int_to_f(slider10) * F::raw(0x3E8E38E3)); // (km/h to m/s)
    r = r / (int_to_f(gt3r::s16(s.h(0x150))) * F::raw(0x3A83126E));
    auto eased = [&](u32 a, u32 b) {
        const F lo = step(b);
        return lo + t * (step(a) - lo);
    };
    F q1 = eased(0, 1), q2 = eased(2, 3);
    const F q3 = eased(4, 5);
    for (int i = n; i > 0; --i) {
        if (i == 1) r = r * q3;
        const F v = r * F::raw(0x447A0000);
        r = r * q1, q1 = q1 * q2;
        if (i < 8) out[i] = gt3r::s16(u16(u32(gt3r::ee::to_int(v)))); // (truncated and saturated, the low 16 bits kept)
    }
    const int rev = 3 * int(out[1]) + int(out[2]);
    out[0] = gt3r::s16(rev / 4); // (toward zero)
}

bool computed_gears(db::SpecDb& db, const Program& program, Equipment& eq) {
    const ByteSpan gear = db::part_row(db, eq.part(E::Gear));
    if (gear.size() < 0x2D || gear[0x25] == 0) return false;
    Spec s;
    build_spec(db, program, eq, 0, s);
    const int n = s.gears();
    if (n < 1 || n > 7) return false;
    gt3r::s16 out[8] = {};
    if (program.known()) gear_ratios(s, program, 10 * int(eq.b(E::GearSlider)), out);
    else { // ❓ Prologue: the shared engine's generator (Gran Turismo 3's 0x1E28A8) until Prologue's own is placed
        const int axle = s.drive() == 1 || (s.drive() == 2 && s.b(0x129) == 0) ? 0 : 1;
        const int rim = s.b(0x1CA + u32(axle));
        gt3r::sim::generate_gear_ratios(n, s.b(0xBD), s.h(0x150), rim < 61 ? rim : (rim + 5) / 10, s.b(0x1C8 + u32(axle)), s.b(0x1CC + u32(axle)),
                                        eq.b(E::GearSlider), out, s.drive(), s.b(0x129));
    }
    for (int i = 0; i <= n; ++i) eq.set_h(E::GearRatios + 2 * u32(i), u16(out[i]));
    return true;
}

bool stock_car(db::SpecDb& db, const Program& program, const std::string& car_label, Equipment& out) {
    if (!stock_equipment(db, car_label, out)) return false;
    computed_gears(db, program, out);
    return true;
}

SpecFigures spec_figures(const Spec& s) {
    SpecFigures f;
    const int n = s.b(0xBF) <= 24 ? s.b(0xBF) : 24;
    f.rev_limit_rpm = (s.b(0xBD) ? s.b(0xBD) : n ? s.b(0xC0 + u32(n) - 1) : 0) * 100;
    const float out = (s.h(0x108) ? float(s.h(0x108)) : 1000.0f) * 0.001f * (s.b(0x07) ? float(s.b(0x07)) * 0.01f : 1.0f);
    const float lo = s.b(0x10C) ? float(s.b(0x10C)) * 0.01f : 1.0f, hi = s.h(0x10A) ? float(s.h(0x10A)) * 0.001f : 1.0f;
    const float w0 = n ? float(s.b(0xC0)) : 0, w1 = n ? float(s.b(0xC0 + u32(n) - 1)) : 0;
    for (int i = 0; i < n; ++i) {
        const float w = float(s.b(0xC0 + u32(i)));
        const float t = w1 > w0 ? (w - w0) / (w1 - w0) : 0;
        const float kgfm = float(s.h(0xD8 + 2 * u32(i))) * 0.01f * out * (lo + (hi - lo) * t);
        const float rpm = w * 100.0f, ps = kgfm * rpm / 716.2f;
        if (rpm > float(f.rev_limit_rpm)) continue;
        if (ps > f.ps) f.ps = ps, f.power_rpm = int(rpm);
        if (kgfm > f.kgfm) f.kgfm = kgfm, f.torque_rpm = int(rpm);
    }
    for (u32 a = 0; a < 2; ++a) { // 0x34BA30 without the tire kinds' changes
        const float width = float(s.b(0x1C8 + a) ? s.b(0x1C8 + a) * 5 : 10) * 0.001f;
        const float rim = s.b(0x1CA + a) < 61 ? float(s.b(0x1CA + a)) : float(s.b(0x1CA + a)) * 0.1f;
        f.tire_radius[a] = rim * 0.0127f + width * float(s.b(0x1CC + a)) * 0.05f;
    }
    return f;
}

} // namespace gt4r::sim
