#pragma once

// Gran Turismo 4's car record beside the shared engine's (docs/specs/physics.md "The car's record").
// The engine's vehicle step works on a mirror of Gran Turismo 3's record (sim/exact/car_record.h), every
// member named by its offset there (sim/exact/car_fields.h). Gran Turismo 4 keeps the same values in a
// record of another shape: 0x10A0 bytes for the car, with the parameter block in its own 0x1100 bytes.
// place() says where: the record images of the user's own program are read into the mirror through it
// (tools/gt4diff), and it is the list of what is known of the record.

#include "gt4r/base/types.h"

#include <string>
#include <vector>

namespace gt4r::sim::layout {

constexpr u32 kCarSize = 0x10A0, kParamsSize = 0x1100;
constexpr u32 kDriver = 0x14;                        // the computer driver's object
constexpr u32 kState = 0x104;                        // the state block F
constexpr u32 kWheel0 = 0x164, kWheelSize = 0xEC;    // wheel w
constexpr u32 kGround0 = 0x7B8, kGroundSize = 0x34;  // wheel w's ground record
constexpr u32 kPrevious = 0x8BC;                     // the last step's values

enum class Block : u8 { None, Car, Params };
struct Place {
    Block block = Block::None;
    u32 offset = 0;
    explicit operator bool() const { return block != Block::None; }
};

// Where Gran Turismo 4 keeps the value the engine's record has at `engine_offset` (None: not known, or
// not kept).
Place place(u32 engine_offset);

// The rows place() is made of, for listing: a stretch of the engine's record and where it is.
struct Row {
    u32 engine, size;
    Block block;
    u32 offset;
    const char* what;
};
const std::vector<Row>& rows();

} // namespace gt4r::sim::layout
