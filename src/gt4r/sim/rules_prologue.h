#pragma once

// Gran Turismo 4 Prologue's own race step (docs/formats/prologue.md "The race step"). Its car record is Gran Turismo 3's
// (the shared engine's mirror, sim/exact/car_record.h), its parameter block Gran Turismo 3's with each tire record's
// three kind bytes after it (Gran Turismo 4's +0x1D8 .. +0x1DA). Its step's routines are each either the engine's
// (Gran Turismo 3's, equal at every call: tools/gt4diff's Prologue taps), Gran Turismo 4's (rules_*.h) on the record's
// Gran Turismo 4 part made from Prologue's (prologue_extra), or Prologue's own (below). Addresses are SCPS-15055 v1.01's.

#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/record.h"
#include "gt4r/sim/rules_chassis.h"
#include "sim/exact/tick.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_tires.h"

#include <array>

namespace gt4r::sim {
class Program;
}

namespace gt4r::sim::prologue {

namespace ex = gt3r::sim::exact;
using gt3r::ee::F;

// Prologue's tables the step reads, from its program (program_prologue.h places them).
struct Tables {
    bool found = false;                // the program is Prologue's
    rules::SlipBytes slip;             // the static block's +0x84 .. +0x8B (Gran Turismo 3's place)
    std::array<u8, 3> direct_speed{};  // the static block's +0xE4 (lateral), +0xE5, +0xE6 (slip ratio): km/h, 50 when 0
    rules::SlipForceGlobals globals;   // the camber force's reference and gain (0x504294 / 0x504298), the mean slip's switch
                                       // (0x506612), the steering mode (0x714880: 1 in a race; the slip angle takes each wheel's
                                       // offset, the drive block's +0x1C)
    std::array<std::array<u8, rules::kWheelLayoutSize>, 8> wheel_layouts{}; // 0x503FA8, by the parameter block's byte +0x30
};
Tables tables(const Program& program);

// A tire record's kind bytes (Prologue's parameter block: the record's +0x1B4 .. +0x1B6; its spec: the tire block's +0x6C ..
// +0x6E): the direct slip ratio, the direct lateral speed, the lateral force from curve A alone (rules::Tire's).
using TireKinds = std::array<std::array<u8, 3>, 6>;

// The wheel layout of the parameter block's row (P +0x30).
rules::WheelLayout wheel_layout(const Tables& t, u8 row);
// Gran Turismo 4's tire part of the record (rules::TireExtra) made from Prologue's parameter block: its 16 surfaces as
// surfaces 0 .. 15, the tire index, widths, camber curves and axle grips as they are; the direct lateral speed's scale pi / 2
// over the static byte +0xE4 (0x3BB138).
rules::TireExtra tire_extra(const ex::Params& p, const TireKinds& kinds, const Tables& t);

// 0x3BA450 (phase 8) with 0x3BA070: each wheel's slip ratio. Gran Turismo 4's (0x3513F8) but for the direct slip ratio:
// the difference of the hub's and the rim's speeds over a speed by the slip's state, the static byte +0xE6 driving (state 1),
// +0xE5 braking, each falling back to the other and then to 50 km/h (a division where Gran Turismo 4 multiplies by one
// scale).
void slip_ratios(ex::Car& c, const rules::TireExtra& x, const rules::WheelLayout& l, const Tables& t);

// Prologue's converter (0x3C74B8) where it is not the engine's (Gran Turismo 3's 0x1DBFD8), on the engine's parameter
// block: each tire record's base grip times its load curve at its axle's static load per wheel (half the axle's static
// load plus the unsprung weight, over the axle's width: Gran Turismo 4's rule, 0x361798), and for a record read at its
// sideways speed the slip angle of curve A's peak the axis point of its greatest value (Gran Turismo 4's 0x353800).
void convert(ex::Params& p, const Spec& spec);

// A tire record's kind bytes from Gran Turismo 4's spec of the car (Spec::tire_block(k) +3 .. +5: the builder writes
// Prologue's there as its spec's tire block +0x6C .. +0x6E).
TireKinds tire_kinds(const Spec& spec);

// What a car of Prologue's step keeps beside the engine's record: Gran Turismo 4's tire part, the mass the forward step
// divides by (the parameter block's), its wheel layout.
struct CarPart {
    rules::TireExtra tires;
    rules::ChassisExtra chassis;
    rules::WheelLayout layout;
};
CarPart car_part(const ex::Car& c, const Spec& spec, const Tables& t);

// Each wheel's tire's x1da for the forward step and the lateral forces: the record of the wheel's surface (Gran Turismo 4
// copies it in its peak force, 0x357418; Prologue reads the record).
void wheel_kinds(const ex::Car& c, CarPart& x);

// 0x3BB5D0: the forces across the car and the yaw. Gran Turismo 4's (0x352848) but for: no follower of the yaw
// acceleration, the lateral acceleration the force sum times the parameter block's inverse mass (+0x4C), and no computer
// driver's grip in the lateral acceleration over the tires' grip.
void lateral_yaw(ex::Car& c, const CarPart& x, bool airborne, const Tables& t);

// 0x3BBFE0 (phase 10): the engine's phase (brakes and clutch, rolling resistance, tire curves, the class grip, the
// longitudinal force, wheel torques, vectoring, axles, wheel speeds, engine speed and gearbox, the hold: each equal to
// Prologue's at every call) with Gran Turismo 4's tire slip force (0x3BB138; its steering mode 1, the wheels' offsets
// the drive block's +0x1C, the mean slip angle the record's F +0x420) and forward step (0x3BAC98), and its lateral forces.
void dynamics(ex::Car& c, CarPart& x, const ex::Statics& s, const ex::GearSettings& g, const ex::ClassGrip& grip, const Tables& t);

// 0x3C4D80: the ground's pitch, roll and height as the engine's (Gran Turismo 3's) and its axes as Gran Turismo 4's
// (0x377310: the pitch before the roll).
void ground_frame(ex::Car& c);

} // namespace gt4r::sim::prologue
