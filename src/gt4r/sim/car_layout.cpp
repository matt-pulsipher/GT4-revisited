#include "gt4r/sim/car_layout.h"

#include <array>

namespace gt4r::sim::layout {

namespace {

// The engine's record (Gran Turismo 3's): the parameter block at +0x10, the state block F at +0xF84, the
// wheels at F +0x60 + 0xA4 w, the driven wheels' blocks at +0x1274 + 0x24 w, the ground records at
// +0x14BC + 0x34 w, the last step's values at +0x15C0.
constexpr u32 eP = 0x10, eF = 0xF84, eW = 0xFE4, eWSize = 0xA4, eT = 0x1274, eTSize = 0x24, eG = 0x14BC, ePrev = 0x15C0;

std::vector<Row> build() {
    std::vector<Row> r;
    const Block C = Block::Car, P = Block::Params;
    auto f = [&](u32 engine, u32 size, u32 gt4, const char* what) { r.push_back({eF + engine, size, C, kState + gt4, what}); };
    auto p = [&](u32 engine, u32 size, u32 gt4, const char* what) { r.push_back({eP + engine, size, P, gt4, what}); };
    auto wheel = [&](u32 engine, u32 size, u32 gt4, const char* what) {
        for (u32 w = 0; w < 4; ++w) r.push_back({eW + eWSize * w + engine, size, C, kWheel0 + kWheelSize * w + gt4, what});
    };
    auto drive = [&](u32 engine, u32 size, u32 gt4, const char* what) {
        for (u32 w = 0; w < 4; ++w) r.push_back({eT + eTSize * w + engine, size, C, kWheel0 + kWheelSize * w + gt4, what});
    };

    // ---- the state block ----
    f(0x000, 0x60, 0x000, "position, body axes, the ground under the car and its axes");
    f(0x468, 0x0C, 0x568, "pitch, roll, heading");
    f(0x494, 0x08, 0x574, "the ground's pitch and roll");
    f(0x44C, 0x0C, 0x544, "velocity");
    f(0x488, 0x08, 0x53C, "speed along the body, sideways speed");
    f(0x448, 0x04, 0x4FC, "engine speed");
    f(0x436, 0x01, 0x520, "gear");
    f(0x3E8, 0x01, 0x440, "the car's index");
    f(0x3E9, 0x01, 0x466, "control source");
    f(0x3EA, 0x01, 0x441, "not in the race");
    f(0x534, 0x01, 0x467, "driver kind");
    f(0x535, 0x01, 0x4B1, "flags (bit 4: on the pit lane)");
    f(0x461, 0x01, 0x442, "pedal style");
    f(0x41C, 0x04, 0x468, "steering command");
    f(0x428, 0x08, 0x488, "throttle and brake pedals");
    f(0x430, 0x04, 0x490, "handbrake");
    f(0x4A4, 0x04, 0x494, "throttle");
    f(0x4A8, 0x08, 0x498, "steering bounds");
    f(0x523, 0x01, 0x460, "steering, percent");
    f(0x522, 0x01, 0x461, "full throttle flag");
    f(0x524, 0x04, 0x444, "time scale");
    f(0x4A0, 0x04, 0x44C, "1 / step length");
    f(0x3E4, 0x04, 0x4A4, "distance along the course");
    f(0x3EC, 0x04, 0x4A8, "road cell");
    f(0x434, 0x02, 0x4AC, "laps");
    f(0x467, 0x01, 0x4AE, "lap base");
    f(0x4E1, 0x01, 0x4AF, "rank");
    f(0x3F0, 0x08, 0x5D0, "distance driven and its carry");
    f(0x3F8, 0x20, 0x5D8, "the engine condition's words");
    f(0x4F8, 0x08, 0x5FC, "the view's heading and pitch lags");
    f(0x398, 0x04, 0x5B0, "rolling resistance");
    f(0x4C4, 0x04, 0x550, "longitudinal acceleration");
    f(0x4D0, 0x04, 0x564, "(the rolling resistance's starting load)");
    r.push_back({0xEC0, 4, C, kState + 0x448, "step length"});
    r.push_back({0x16A8, 8, C, kState + 0x5C4, "speed, highest speed"});
    r.push_back({0x16C4, 4, C, kState + 0x618, "best lap"});

    // ---- a wheel ----
    wheel(0x0C, 4, 0x00, "steer angle");
    wheel(0x10, 4, 0x04, "camber");
    wheel(0x04, 4, 0x8C, "ground height");
    wheel(0x38, 4, 0x24, "spin angle");
    wheel(0x94, 4, 0xA4, "brake heat");
    wheel(0x98, 4, 0xC0, "flap");
    wheel(0x4C, 8, 0x28, "hub velocity along and across the car");
    wheel(0x78, 4, 0x30, "slip direction");
    wheel(0x7C, 4, 0x60, "slip direction's low-speed fade");
    wheel(0xA0, 4, 0xAC, "tread wear");
    wheel(0x5D, 1, 0xB1, "the HUD's wear level");
    wheel(0x20, 4, 0x20, "rim speed");
    wheel(0x48, 4, 0x3C, "slip gain");
    wheel(0x68, 4, 0x38, "slip ratio");
    drive(0x10, 1, 0x44, "slip state");
    drive(0x12, 1, 0x45, "the longitudinal force's sign");
    drive(0x14, 4, 0x54, "the longitudinal force's fraction (curve C)");
    wheel(0x64, 4, 0x58, "friction circle (curve D)");
    drive(0x04, 8, 0x7C, "the longitudinal force's reaction, driving and braking");
    wheel(0x88, 8, 0x70, "the longitudinal force, driving and braking");
    wheel(0x08, 4, 0x48, "load");
    wheel(0x14, 4, 0x08, "half track");
    wheel(0x18, 4, 0x94, "suspension travel");
    wheel(0x24, 3, 0x89, "surface, ground bytes");
    wheel(0x3C, 0x0C, 0x0C, "hub offset");
    wheel(0x54, 4, 0x4C, "peak force");
    wheel(0x58, 4, 0x50, "wear grip");
    wheel(0x90, 4, 0x64, "brake pressure");
    drive(0x00, 4, 0x78, "brake torque");

    // ---- the ground records ----
    for (u32 w = 0; w < 4; ++w) r.push_back({eG + 0x34 * w, 0x34, C, kGround0 + kGroundSize * w, "ground record"});

    // ---- the parameter block ----
    p(0x000, 0x20, 0x000, "body box, axle distances, wheelbase, track");
    p(0x028, 0x08, 0x028, "half tracks");
    p(0x034, 0x18, 0x034, "load transfers, center of gravity's height, ride heights, mass");
    p(0x054, 0x04, 0x050, "origin offset");
    p(0x058, 0x04, 0x054, "steering lock");
    p(0x118, 0x04, 0x114, "yaw inertia");
    p(0x120, 0x18, 0x11C, "pitch and roll levers, the pitch's tire term");
    // ---- the last step's values (0x343ED0) ----
    r.push_back({ePrev + 0x00, 0x14, C, kPrevious + 0x00, "last attitude and the ground's"});
    r.push_back({ePrev + 0x14, 0x80, C, kPrevious + 0x14, "last suspension, steer, camber, half track, spin, brake heat, ground values"});
    r.push_back({ePrev + 0x94, 0x30, C, kPrevious + 0x94, "last hub points"});
    r.push_back({ePrev + 0xC4, 0x10, C, kPrevious + 0xC4, "last flaps"});
    r.push_back({ePrev + 0xD8, 0x0C, C, kPrevious + 0xD8, "last heading lag, pitch lag, distance along the course"});
    return r;
}

} // namespace

const std::vector<Row>& rows() {
    static const std::vector<Row> table = build();
    return table;
}

Place place(u32 engine_offset) {
    static const std::vector<Place> by_offset = [] {
        std::vector<Place> v(0x16DC);
        for (const Row& row : rows())
            for (u32 i = 0; i < row.size && row.engine + i < v.size(); ++i) v[row.engine + i] = {row.block, row.offset + i};
        return v;
    }();
    return engine_offset < by_offset.size() ? by_offset[engine_offset] : Place{};
}

} // namespace gt4r::sim::layout
