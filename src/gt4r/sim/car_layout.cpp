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
    f(0x4D8, 0x04, 0x564, "the forces along the body");
    f(0x4B8, 0x04, 0x5A4, "drag");
    f(0x4C8, 0x04, 0x554, "lateral acceleration");
    f(0x474, 0x04, 0x58C, "lateral acceleration over the tires' grip");
    f(0x478, 0x04, 0x588, "yaw acceleration");
    f(0x47C, 0x04, 0x57C, "yaw rate");
    f(0x480, 0x08, 0x580, "pitch and roll rates");
    f(0x536, 0x02, 0x4BA, "hold counter");
    f(0x4DC, 0x01, 0x4BC, "the start's hold");
    // the clutch, the engine and the axles
    f(0x437, 0x01, 0x522, "clutch state");
    f(0x418, 0x04, 0x514, "clutch request");
    f(0x38C, 0x08, 0x50C, "the clutch's engine and wheel-side speeds");
    f(0x490, 0x02, 0x4FA, "engine rpm");
    f(0x458, 0x08, 0x52C, "axle speeds");
    f(0x4F4, 0x04, 0x51C, "the clutch's torque");
    f(0x4B0, 0x08, 0x4E8, "the engine's full-throttle and friction torques");
    f(0x43B, 0x01, 0x521, "rev limiter");
    f(0x438, 0x02, 0x524, "the differentials' locks (per wheel group)");
    f(0x384, 0x08, 0x534, "the axles' drive torques");
    f(0x460, 0x01, 0x523, "the center clutch locked");
    f(0x43A, 0x01, 0x526, "the gearbox's lock-out");
    f(0x462, 0x02, 0x52A, "the shift's anti-hunt count");
    f(0x464, 0x03, 0x527, "kick-down level, its timer and count");
    f(0x380, 0x02, 0x462, "reverse and shift requests");
    f(0x440, 0x04, 0x4F0, "turbo load");
    f(0x43C, 0x04, 0x500, "turbo load, last step");
    f(0x444, 0x04, 0x4F4, "turbo lags (rpm)");
    f(0x4E2, 0x02, 0x4F8, "turbo pitch (rpm)");
    f(0x4E8, 0x04, 0x504, "blow-off");
    f(0x4EC, 0x04, 0x508, "intake");
    f(0x4E6, 0x01, 0x606, "turbo spool");
    f(0x4E7, 0x01, 0x607, "blow-off state");
    // the suspension and the body (0x36A490 .. 0x36BA88)
    f(0x3B4, 0x10, 0x410, "each wheel's place along its travel at its ground");
    f(0x3C4, 0x10, 0x420, "each wheel's force on the body");
    f(0x3A4, 0x04, 0x5B4, "the body's up axis . the ground's");
    f(0x4BC, 0x08, 0x5A8, "downforce, front and rear");
    f(0x528, 0x08, 0x59C, "the slipstream's target and filter");
    f(0x4CC, 0x04, 0x558, "heave acceleration");
    f(0x4DD, 0x01, 0x4B5, "the wheels past their bump stop");
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
    wheel(0x80, 8, 0x68, "the slip angle's forces: curve A, curve B");
    drive(0x20, 4, 0x34, "slip angle");
    wheel(0x5F, 1, 0x46, "flags (0: loaded, not braked)");
    wheel(0x08, 4, 0x48, "load");
    wheel(0x14, 4, 0x08, "half track");
    wheel(0x18, 4, 0x94, "suspension travel");
    wheel(0x24, 3, 0x89, "surface, ground bytes");
    wheel(0x3C, 0x0C, 0x0C, "hub offset");
    wheel(0x54, 4, 0x4C, "peak force");
    wheel(0x58, 4, 0x50, "wear grip");
    wheel(0x90, 4, 0x64, "brake pressure");
    drive(0x00, 4, 0x78, "brake torque");
    wheel(0x60, 4, 0x90, "the road's bump");
    wheel(0x6C, 4, 0x40, "speed ramp");
    wheel(0x1C, 4, 0x98, "travel speed");
    wheel(0x70, 8, 0x9C, "spring and damper forces");
    drive(0x0C, 4, 0x84, "net torque");

    // ---- the ground records ----
    for (u32 w = 0; w < 4; ++w) r.push_back({eG + 0x34 * w, 0x34, C, kGround0 + kGroundSize * w, "ground record"});

    // ---- the parameter block ----
    p(0x000, 0x20, 0x000, "body box, axle distances, wheelbase, track");
    p(0x020, 0x04, 0x020, "the rear axle's share of the weight");
    p(0x028, 0x08, 0x028, "half tracks");
    p(0x031, 0x01, 0x031, "drive class (the class grip's row)");
    p(0x034, 0x18, 0x034, "load transfers, center of gravity's height, ride heights, mass");
    p(0x054, 0x04, 0x050, "origin offset");
    p(0x058, 0x04, 0x054, "steering lock");
    p(0x118, 0x04, 0x114, "yaw inertia");
    p(0x11C, 0x04, 0x118, "1 / yaw inertia");
    p(0x120, 0x18, 0x11C, "pitch and roll levers, the pitch's tire term");
    p(0x2B0, 0x08, 0x300, "a free wheel's extra mass, per axle");
    // the engine block (GT4 +0x134: a curve of 24 points, what follows it 0x40 further on): the first 16 points
    p(0x138, 0x02, 0x134, "the torque curve's points");
    p(0x144, 0x40, 0x140, "the torque curve's speeds (16)");
    p(0x184, 0x40, 0x1A0, "the torque curve's torques (16)");
    p(0x1C4, 0x34, 0x200, "rev limit, idle, friction, turbo, power cap, boost");
    // the driveline block (GT4 +0x234: eight down-shift speeds, ten ratios and speed factors)
    p(0x1F8, 0x30, 0x234, "layout, gears, the four wheel drive, the differentials, launch and up-shift speeds");
    p(0x228, 0x0C, 0x264, "down-shift speeds (6)");
    p(0x234, 0x20, 0x274, "gear ratios (8)");
    p(0x254, 0x20, 0x29C, "gear speed factors (8)");
    p(0x274, 0x18, 0x2C4, "tire radii, their inverses, widths");
    p(0x294, 0x1C, 0x2E4, "shift clutch torque, wheel gains, axle inertias");
    p(0xE28, 0x0C, 0x1010, "brake torques, handbrake torque");
    p(0xE60, 0x0C, 0x1068, "drag and downforce coefficients");
    p(0xDF8, 0x04, 0xFD8, "top speed");
    p(0x2B8, 0x44, 0x308, "front suspension block");
    p(0x2FC, 0x44, 0x360, "rear suspension block");
    p(0x110, 0x08, 0x10C, "the steering's camber ratios");
    p(0xDFC, 0x08, 0xFE4, "camber");
    p(0xE04, 0x08, 0xFEC, "camber gain over travel");
    p(0xE14, 0x08, 0xFFC, "swing arms");
    p(0xE1C, 0x04, 0x1004, "the steering's camber");
    p(0xDF0, 0x08, 0xFD0, "the axles' grips");
    p(0xE0C, 0x08, 0xFF4, "the camber's grip at the camber");
    p(0xE20, 0x08, 0x1008, "toe");
    p(0xE34, 0x04, 0x101C, "the braking distance's least coefficient");
    p(0xE38, 0x08, 0x1024, "the ABS's slope");
    p(0xE40, 0x08, 0x102C, "the braking slip target's scale");
    p(0xE48, 0x08, 0x1034, "the ABS's peak force scale");
    p(0xE50, 0x0C, 0x103C, "weight, static loads");
    p(0xE6C, 0x10, 0x1088, "the wear's load terms");
    p(0xE7C, 0x34, 0x1098, "the car's own aid block");
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
