#pragma once

// A car file (car/lod/<model>, car/menu/<model>): "CAR4", a header of offsets to the car's
// parts; docs/formats/model.md "Cars".

#include "gt4r/base/types.h"
#include "gt4r/formats/model/mdls.h"

#include <string>

namespace gt4r::car {

struct CarFile {
    u32 info = 0;        // +0x10: lamps, camera mounts, the driver's place
    u32 collision = 0;   // +0x14
    u32 body = 0;        // +0x18: MDLS
    u32 body_paint = 0;  // +0x1C: Pat0 (0: in the file next to this one, <model>.pat)
    u32 wheel = 0;       // +0x20: MDLS, four models (front left, front right, rear left, rear right)
    u32 wheel_paint = 0; // +0x24
    u32 wing = 0;        // +0x28: MDLS
    u32 tire[2] = {};    // +0x2C, +0x30: MDLS, front and rear
    u32 driver = 0;      // +0x34

    static bool is_car(ByteSpan d) { return d.size() >= 0x40 && gt3r::has_magic(d, "CAR4"); }
    // Throws FormatError.
    static CarFile parse(ByteSpan file);
};

// "Pat0": a car's paints as patches of its model set. Header: +0x10 s16 paints, +0x12 s16
// patches per paint; +0x20 u32 offsets[paints x patches], each -> u32 target (from the model
// set's start), u32 bytes, the bytes. A paint is put on by copying its patches over the set.
u32 paint_count(ByteSpan pat);
// The model set with a paint's patches applied. Throws FormatError.
Bytes apply_paint(ByteSpan mdls, ByteSpan pat, u32 paint);

// Where a car's wheels go, from the database (specdb.h car_wheels()). Index 0 front, 1 rear.
struct Wheels {
    float wheelbase = 0;      // m
    float outer_width[2] = {}; // m: across the tires' outer faces
    float tire_diameter[2] = {};
    float tire_width[2] = {};
    bool valid() const { return wheelbase > 0 && tire_diameter[0] > 0 && tire_width[0] > 0; }
};

struct Options {
    u32 paint = 0;
    bool wheels = true;
    bool textures = true;
    int lod = 0;
};
// The whole car as a still model: the painted body, and with `wheels` its four wheels and
// tires. `pat`: the paint file's bytes when the car file has none inside (empty: unpainted).
gt3r::scene::Model build_car(ByteSpan file, ByteSpan pat, const Wheels* wheels, const Options& opt = {}, const std::string& name = {});

} // namespace gt4r::car
