#pragma once

// The parts tables of the car database as the simulation reads them: the tables by their ids, a
// car's parts as sold, and the columns of the rows that are known (docs/formats/database.md,
// "The parts tables' columns").

#include "gt4r/formats/db/specdb.h"

#include <array>
#include <optional>
#include <string>

namespace gt4r::db {

// The parts tables' ids (a label list's table id; the second word of a part's {row id, table id}).
enum class PartTable : u32 {
    GenericCar = 0,
    Brake = 1,
    BrakeController = 2,
    Suspension = 3,
    Ascc = 4,
    Tcsc = 5,
    Chassis = 6,
    RacingModify = 7,
    Lightweight = 8,
    Steer = 9,
    Drivetrain = 10,
    Gear = 11,
    Engine = 12,
    NaTune = 13,
    TurbineKit = 14,
    PortPolish = 15,
    EngineBalance = 16,
    Displacement = 17,
    Computer = 18,
    Intercooler = 19,
    Muffler = 20,
    Clutch = 21,
    Flywheel = 22,
    PropellerShaft = 23,
    Lsd = 24,
    FrontTire = 25,
    RearTire = 26,
    Nos = 27,
    Supercharger = 28,
    Wheel = 29,
    Wing = 30,
    TireSize = 31,
    TireCompound = 32,
    TireForceVol = 33,
    Count
};
// The table's name ("CHASSIS"); null past the parts tables.
const char* part_table_name(u32 table_id);
// A row of a parts table; empty when the set has no such table or row.
ByteSpan part_row(SpecDb& db, u32 table_id, u32 row_id);
inline ByteSpan part_row(SpecDb& db, PartTable t, u32 row_id) { return part_row(db, u32(t), row_id); }

// A part: its row and table packed as the program keeps them (table id in the high word); none is -1.
using PartId = s64;
constexpr PartId kNoPart = -1;
constexpr PartId part_id(u32 table_id, u32 row_id) { return PartId((u64(table_id) << 32) | row_id); }
constexpr u32 part_table(PartId id) { return u32(u64(id) >> 32); }
constexpr u32 part_row_id(PartId id) { return u32(u64(id)); }
inline ByteSpan part_row(SpecDb& db, PartId id) { return id == kNoPart ? ByteSpan() : part_row(db, part_table(id), part_row_id(id)); }

// A car's row of GENERIC_CAR (table 0) and its parts as sold (its row of DEFAULT_PARTS: 26 parts, the
// part of table t at index t - 1).
struct CarParts {
    u32 car_id = 0;
    Bytes car_row;                    // GENERIC_CAR, 24 bytes
    std::array<PartId, 26> parts{};   // by table id - 1; kNoPart: none
    PartId part(PartTable t) const { return u32(t) >= 1 && u32(t) <= 26 ? parts[u32(t) - 1] : kNoPart; }
};
std::optional<CarParts> car_parts(SpecDb& db, const std::string& car_label);

// CHASSIS (table 6, 37 bytes).
struct Chassis {
    u16 length_mm = 0, height_mm = 0, wheelbase_mm = 0, weight_kg = 0;
    u16 grip_front = 0, grip_rear = 0;   // +0x0E / +0x10: per mille of the axle's tire grip
    u16 outer_width_mm[2] = {};          // +0x12 / +0x14: across the front / rear tires
    u16 wheel_top_mm[2] = {};            // +0x16 / +0x18: how high the front / rear wheel's top may reach in the body
    u8 front_weight_pct = 0;             // +0x1C
    u8 yaw_inertia = 0;                  // +0x1D (x 100 kg m2)
    u8 cog_height_mm = 0;                // +0x1E the center of gravity's height parameter
    static std::optional<Chassis> read(ByteSpan row);
};

// ENGINE (table 12, 113 bytes).
struct Engine {
    u16 catalog_ps = 0;                  // +0x1A
    u16 catalog_torque = 0;              // +0x1C, 0.01 kgf m
    int points = 0;                      // +0x54 (at most 24)
    std::array<u16, 24> torque{};        // +0x1E, 0.01 kgf m
    std::array<u8, 24> rpm{};            // +0x55, x 100 rpm
    u8 idle_rpm10 = 0;                   // +0x52, x 10 rpm
    u8 rev_limit = 0;                    // +0x51, x 100 rpm
    u8 shift_limit = 0;                  // +0x50, x 100 rpm
    u8 clutch_rpm10 = 0;                 // +0x53, x 10 rpm
    u8 output_pct = 0;                   // +0x6F the engine's output factor, percent
    static std::optional<Engine> read(ByteSpan row);
    // The curve's peak power (PS) and torque (kgf m) with the output factor, and their engine speeds.
    struct Peak {
        float ps = 0, kgfm = 0;
        int power_rpm = 0, torque_rpm = 0;
    };
    Peak peak() const;
};

// DRIVETRAIN (table 10, 21 bytes).
struct Drivetrain {
    u16 engine_inertia = 0;     // +0x00 (x 0.001)
    u16 engine_friction = 0;    // +0x02 (x 0.98 N m)
    u16 wheel_inertia[2] = {};  // +0x04 / +0x06 (x 0.01)
    u16 shaft_inertia[2] = {};  // +0x08 / +0x0A (x 0.01)
    u8 drive = 0;               // +0x10: 0 front engine rear drive, 1 front drive, 2 four wheel drive, 3 mid engine, 4 rear engine
    u8 awd_system = 0;          // +0x11 (0xFE: none)
    u8 front_torque_pct = 0;    // +0x14 the front axle's share of a four wheel drive
    static std::optional<Drivetrain> read(ByteSpan row);
    const char* layout() const; // "FR", "FF", "4WD", "MR", "RR"
};

// GEAR (table 11, 45 bytes). Ratios x 1000.
struct Gear {
    int gears = 0;                    // +0x24 forward gears
    std::array<u16, 11> ratio{};      // +0x00 first gear on
    u16 reverse = 0;                  // +0x16
    u16 final_min = 0, final_max = 0, final_drive = 0; // +0x18 / +0x1A / +0x1C
    u8 kind = 0;                      // +0x23 the gearbox's kind
    bool computed = false;            // +0x25: the ratios are made from the slider
    u8 slider_min = 0, slider_max = 0, slider = 0; // +0x26 / +0x27 / +0x28
    static std::optional<Gear> read(ByteSpan row);
};

// TIRESIZE (table 31, 4 bytes).
struct TireSize {
    int width_mm = 0, aspect = 0;
    float rim_inches = 0;
    static std::optional<TireSize> read(ByteSpan row);
    float radius_m() const { return rim_inches * 0.0127f + float(width_mm) * float(aspect) * 0.00001f; }
};

} // namespace gt4r::db
