#pragma once

// A car as a race entry carries it: the car, one part per parts table and the settings the Settings
// pages edit (the program's car equipment record; docs/specs/physics.md "The car's equipment").
// Kept as the record's bytes so every field is at the offset the specification names.

#include "gt4r/formats/db/car_parts.h"

#include <array>
#include <string>

namespace gt4r::sim {

struct Equipment {
    static constexpr u32 kSize = 0x178;
    std::array<u8, kSize> bytes{};

    // The parts' places in the record (8 bytes each: db::PartId).
    enum Slot : u32 {
        Car = 0x00,
        Variation = 0x10, // the car's row of the paint table
        Brake = 0x20,
        BrakeController = 0x28,
        Chassis = 0x30,
        Engine = 0x38,
        Drivetrain = 0x40,
        Gear = 0x48,
        Suspension = 0x50,
        Lsd = 0x58,
        FrontTire = 0x60,
        RearTire = 0x68,
        Steer = 0x70,
        Lightweight = 0x78,
        RacingModify = 0x80,
        PortPolish = 0x88,
        EngineBalance = 0x90,
        Displacement = 0x98,
        Computer = 0xA0,
        NaTune = 0xA8,
        TurbineKit = 0xB0,
        Flywheel = 0xB8,
        Clutch = 0xC0,
        PropellerShaft = 0xC8,
        Muffler = 0xD0,
        Intercooler = 0xD8,
        Ascc = 0xE0,
        Tcsc = 0xE8,
        Nos = 0xF8,
        Wheel = 0x100,
        Supercharger = 0x108,
    };
    // The settings' places.
    enum Setting : u32 {
        GearRatios = 0x110,  // u16 x 12: reverse, gears 1 .. 11 (x 1000)
        FinalDrive = 0x128,  // u16 (x 1000)
        GearSlider = 0x12A,  // u8: a computed gearbox's design top speed / 10 km/h
        FinalDrive2 = 0x12C, // u16: the final drive again
        TorqueSplit = 0x12E, // u8
        BrakeFront = 0x12F, BrakeRear = 0x130,          // brake controller indices
        DownforceFront = 0x131, DownforceRear = 0x132,
        Turbo = 0x133,       // 6 bytes
        CamberFront = 0x139, CamberRear = 0x13A,
        RideFront = 0x13C, RideRear = 0x13E,            // u16 mm
        ToeFront = 0x140, ToeRear = 0x141,
        SpringFront = 0x142, SpringRear = 0x143,
        MotionFront = 0x144, MotionRear = 0x145,
        Dampers = 0x146,     // 8 indices: front bound 1 / 2, front rebound (unused: the bound's serve), rear bound 1 / 2, rear rebound 1 / 2
        AntiRollFront = 0x14E, AntiRollRear = 0x14F,
        LsdSettings = 0x150, // front initial, rear initial, front acceleration, rear, front deceleration, rear
        TcscIndex = 0x156,
        AsccIndices = 0x157, // 4
        Ballast = 0x15B, BallastPlace = 0x15C,
        OutputFactor = 0x15E, // u16: the engine's output factor (per mille) when not 0
        WeightPct = 0x162, PowerPct = 0x163,
        NosSetting = 0x167,
        GripAdd = 0x168,
    };

    Equipment();
    db::PartId part(u32 slot) const;
    void set_part(u32 slot, db::PartId id);
    u8 b(u32 o) const { return bytes[o]; }
    void set_b(u32 o, u8 v) { bytes[o] = v; }
    u16 h(u32 o) const { return u16(bytes[o] | (bytes[o + 1] << 8)); }
    void set_h(u32 o, u16 v) { bytes[o] = u8(v), bytes[o + 1] = u8(v >> 8); }
};

// A car as sold (the program's set-car routine): its default parts, then each part's default settings
// clamped to the part's range. False when the database has no such car.
bool stock_equipment(db::SpecDb& db, const std::string& car_label, Equipment& out);

} // namespace gt4r::sim
