#pragma once

// The driver in a race car (HumanModel / RaceDriverModel): the car file's own driver set (+0x34:
// on most cars a dark head-and-shoulders silhouette bound to the node CRANIUM, on the open race
// cars a whole skinned figure), or, for the few cars without one, a character file chosen by the
// car's cockpit kind. Each model of the set named by a symbol is drawn at the skeleton's node of
// that name, the skinned model ("SKINNING") by the bones. The skeleton stands in its stored pose
// at the seat: the program's posture (0x3B2708 and the full-body solver it drives) is not ported.
// docs/formats/model.md "The driver in the car".

#include "gt4r/base/types.h"
#include "render/math3d.h"

#include <optional>
#include <string>
#include <vector>

namespace gt4r::race {

namespace render = gt3r::render;

// The cockpit's places the posture reads from the car's parts block (info +0x50), in the car's frame
// (metres, x right, y up, the front at -z), through the car's geometry object (NormalCarGeometry).
struct CockpitPlaces {
    render::Vec3 wheel{0, 0, 0};     // +0x00: the steering wheel's centre
    render::Vec3 column{0, 0, 1};    // +0x0C: the steering column's axis (unit, towards the driver)
    float wheel_radius = 0;          // +0x18 ❓ (R34 0.150: the wheel's size)
    render::Vec3 pedals[2];          // +0x1C, +0x28
    render::Vec3 seat{0, 0, 0};      // +0x34: where the figure sits
    render::Vec3 shifter{0, 0, 0};   // +0x4C ❓ (a hand's place: the gear lever)
    render::Vec3 point_6c{0, 0, 0};  // +0x6C ❓
    std::optional<render::Vec3> co_seat; // +0xC4: the other seat (all zero: the seat mirrored, x = -x)
    int shifter_kind = 0;            // +0x6A u16
    int cockpit_kind = 0;            // info +0x02 (0x20 on most cars, 0x11 on single-seat racers)
    bool co_driver = false;          // info +0x03: a second figure in the other seat (rally cars)
};
// Throws FormatError on a car file without an info block.
CockpitPlaces read_cockpit(ByteSpan car_file);

// The other seat's places: everything moved by (other seat - seat), the other seat the parts
// block's +0xC4 or, when that is all zero, the seat mirrored across the car's middle (0x38CDE8).
render::Vec3 co_seat_offset(const CockpitPlaces& c);

// The character file the race gives a car without a driver set of its own (0x3B5438), by its
// cockpit kind; `index` picks one of three for kinds with a choice (the car's number). Empty: no
// figure.
std::string character_driver_file(int cockpit_kind, bool co_driver, int index);

// A driver set's models placed in the figure's frame: models named by symbols at the bind (stored)
// matrix of the bone of the same name, the skinned model as stored. The set's own bones give the
// nodes; a set without bones (the silhouettes) takes `skeleton`'s (character/driver.mdl: every
// set's skeleton is built from the same sizes, its extension record 0x800000).
struct DriverPart {
    u32 model = 0;
    render::Mat4 node; // in the figure's frame (the stored pose's model space)
    bool skinned = false;
};
std::vector<DriverPart> driver_parts(ByteSpan set, ByteSpan skeleton);

// The figure's frame in the car's frame: the skeleton's frame turned half a turn about y (the
// posture works in the car's frame with x and z negated) with its PELVIS node at the seat.
render::Mat4 driver_root(const CockpitPlaces& c, ByteSpan set, ByteSpan skeleton, bool co_driver);

} // namespace gt4r::race
