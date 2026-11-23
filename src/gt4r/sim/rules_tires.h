#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): the tires: grip, peak force, slip angles
// and ratios, the tire forces.

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"

namespace gt4r::sim::rules {

// A tire record of the parameter block (P + 0x410 + 0x1EC r; r = 0 .. 2 the front axle's paved, loose
// and wet tires, 3 .. 5 the rear's): the engine's record (sim/exact/car_record.h TireRecord) with the
// grip of 24 surfaces, a floor for the wear's grip and a byte for the wheel.
struct Tire {
    F grip;                    // +0x00
    F wear_rate;               // +0x04
    std::array<F, 24> mu;      // +0x08 grip per surface
    F wear_floor;              // +0x68 the least grip the wear leaves (0x356EF0)
    ex::Table<8> curve_a;      // +0x6C (x +0x78, y +0x98)
    F peak_slip;               // +0xB8
    ex::Table<6> curve_b;      // +0xBC (x +0xC8, y +0xE0)
    ex::Table<12> curve_c;     // +0xF8 (x +0x104, y +0x134)
    F slip_drive, slip_brake;  // +0x164 / +0x168
    F brake_peak;              // +0x16C
    std::array<F, 12> curve_d; // +0x17C (its table +0x170 on curve C's axis)
    ex::Table<4> load_curve;   // +0x1AC (x +0x1B8, y +0x1C8)
    u8 direct_lateral = 0;     // +0x1D9 not 0: curve A is read at the hub's sideways speed, scaled, not at the slip angle (0x352408)
    u8 direct_slip = 0;        // +0x1D8 not 0: the slip ratio is the hub's and the rim's speeds' difference, scaled (0x3513F8)
    u8 x1da = 0;               // +0x1DA copied to the wheel's +0xDD by 0x357418; 1: the lateral force is curve A alone, held in the
                               // friction circle, with the slip angle's sign turned when the wheel rolls backwards
};
constexpr u32 kTire0 = 0x410, kTireSize = 0x1EC;

// What these routines keep that the engine's record has no member for.
struct TireExtra {
    std::array<Tire, 6> tire;
    std::array<std::array<u8, 24>, 2> tire_index{}; // P + 0xF98 + 0x18 axle: the axle's tire record per surface
    std::array<F, 2> width;                         // P + 0x2D4 per axle: the load curve's width
    std::array<ex::Table<4>, 2> camber_grip;        // P + 0x3B8 + 0xC axle (x +0x3D0 + 0x10 axle, y +0x3F0 + 0x10 axle)
    std::array<F, 2> axle_grip;                     // P + 0xFD0 per axle
    F direct_slip_scale;                            // P + 0xFC8 the direct slip ratio's scale (1 / (m/s))
    F direct_lateral_scale;                         // P + 0xFCC
    std::array<u8, 4> wheel_xdd{};                  // wheel +0xDD: the tire record's byte +0x1DA
    std::array<F, 4> steer_offset;                  // wheel +0x1C: added to the steer angle (or in its place) for the slip angle
                                                    // under the program's steering mode 1 (2)
    std::array<F, 4> curve_a;                       // wheel +0x5C: curve A at the slip angle
    std::array<F, 4> camber_force;                  // car + 0x534 + 4 w: the camber's lateral force
    F mean_slip;                                    // F + 0x46C: the mean slip angle of the wheels ahead of the center of gravity
};

// Every member of TireExtra with its place in Gran Turismo 4's record: v(name, index (-1: single),
// block, offset, member). An array's elements are numbered on together (tire r's point k: r x 32 + k).
template <class X, class V> void tires_fields(X& x, V&& v) {
    using layout::Block;
    const Block P = Block::Params, C = Block::Car;
    auto table = [&](const char* n, const char* xs, const char* ys, int i, u32 head, u32 xo, u32 yo, auto& t) {
        v(n, i, P, head, t.n);
        for (u32 k = 0; k < t.x.size(); ++k) v(xs, i * 32 + int(k), P, xo + 4 * k, t.x[k]), v(ys, i * 32 + int(k), P, yo + 4 * k, t.y[k]);
    };
    for (int r = 0; r < 6; ++r) {
        auto& t = x.tire[size_t(r)];
        const u32 R = kTire0 + kTireSize * u32(r);
        v("tire.grip", r, P, R + 0x00, t.grip), v("tire.wear_rate", r, P, R + 0x04, t.wear_rate);
        for (u32 s = 0; s < 24; ++s) v("tire.mu", r * 32 + int(s), P, R + 0x08 + 4 * s, t.mu[s]);
        v("tire.wear_floor", r, P, R + 0x68, t.wear_floor);
        table("tire.curve_a.n", "tire.curve_a.x", "tire.curve_a.y", r, R + 0x6C, R + 0x78, R + 0x98, t.curve_a);
        v("tire.peak_slip", r, P, R + 0xB8, t.peak_slip);
        table("tire.curve_b.n", "tire.curve_b.x", "tire.curve_b.y", r, R + 0xBC, R + 0xC8, R + 0xE0, t.curve_b);
        table("tire.curve_c.n", "tire.curve_c.x", "tire.curve_c.y", r, R + 0xF8, R + 0x104, R + 0x134, t.curve_c);
        v("tire.slip_drive", r, P, R + 0x164, t.slip_drive), v("tire.slip_brake", r, P, R + 0x168, t.slip_brake);
        v("tire.brake_peak", r, P, R + 0x16C, t.brake_peak);
        for (u32 k = 0; k < 12; ++k) v("tire.curve_d", r * 32 + int(k), P, R + 0x17C + 4 * k, t.curve_d[k]);
        table("tire.load_curve.n", "tire.load_curve.x", "tire.load_curve.y", r, R + 0x1AC, R + 0x1B8, R + 0x1C8, t.load_curve);
        v("tire.direct_slip", r, P, R + 0x1D8, t.direct_slip), v("tire.direct_lateral", r, P, R + 0x1D9, t.direct_lateral), v("tire.x1da", r, P, R + 0x1DA, t.x1da);
    }
    for (int a = 0; a < 2; ++a) {
        for (u32 s = 0; s < 24; ++s) v("tire_index", a * 32 + int(s), P, 0xF98 + 0x18 * u32(a) + s, x.tire_index[size_t(a)][s]);
        v("width", a, P, 0x2D4 + 4 * u32(a), x.width[size_t(a)]);
        table("camber_grip.n", "camber_grip.x", "camber_grip.y", a, 0x3B8 + 0xC * u32(a), 0x3D0 + 0x10 * u32(a), 0x3F0 + 0x10 * u32(a),
              x.camber_grip[size_t(a)]);
        v("axle_grip", a, P, 0xFD0 + 4 * u32(a), x.axle_grip[size_t(a)]);
    }
    v("direct_slip_scale", -1, P, 0xFC8, x.direct_slip_scale), v("direct_lateral_scale", -1, P, 0xFCC, x.direct_lateral_scale);
    for (int w = 0; w < 4; ++w) {
        const u32 W = layout::kWheel0 + layout::kWheelSize * u32(w);
        v("w.steer_offset", w, C, W + 0x1C, x.steer_offset[size_t(w)]), v("w.curve_a", w, C, W + 0x5C, x.curve_a[size_t(w)]);
        v("camber_force", w, C, 0x534 + 4 * u32(w), x.camber_force[size_t(w)]);
    }
    v("mean_slip", -1, C, layout::kState + 0x46C, x.mean_slip);
    for (int w = 0; w < 4; ++w) v("w.xdd", w, C, layout::kWheel0 + layout::kWheelSize * u32(w) + 0xDD, x.wheel_xdd[size_t(w)]);
}

// 0x360F80: wheel w's tire record for a surface (its axle's index byte for the surface).
const Tire& tire_record(const TireExtra& x, const WheelLayout& l, int w, u8 surface);
// 0x3573A8: wheel w's load over its axle's width.
F load_per_width(const TireExtra& x, const WheelLayout& l, int w, F load);
// 0x357418: each wheel's peak force: its load by the load curve, the wear's grip (when `wear`), the
// axle's grip, the surface's grip, the camber's grip at the body's roll over the ground plus the
// wheel's own camber; times the computer driver's grip for the tire (`driver_grip`, 1 for a player's
// car: 0x467470).
void peak_force(ex::Car& c, TireExtra& x, const WheelLayout& l, bool wear, F driver_grip = F(1.0f));
// The static block's slip ratio bytes (0x620CD8 + 0xB4, 8 bytes): the fade speeds (km/h) by the hub's
// speed (braking) and the rim's (driving), then per side (0 braking, 1 driving) the gain's switch, its
// value at rest (percent) and the speed it reaches 1 at (km/h).
struct SlipBytes {
    std::array<u8, 2> fade{}, gain_on{}, gain_base{}, gain_knee{};
};
constexpr u32 kSlipBytes = 0x620CD8 + 0xB4;
SlipBytes slip_bytes(const u8* b);
// 0x351358: the slip gain on side k at hub speed u.
F slip_gain(const SlipBytes& sb, int k, F u);
// 0x3513F8: wheel w's slip ratio, state and the force's sign for its hub speed x along its heading.
void slip_ratio(ex::Car& c, const TireExtra& x, int w, const Tire& r, F along, const SlipBytes& sb);
// 0x351778: every wheel's slip ratio.
void slip_ratios(ex::Car& c, const TireExtra& x, const WheelLayout& l, const SlipBytes& sb);

// The race's per-surface tables the step reads (built from the static block at a race's start by
// 0x35F808): the rolling resistance's curve over speed (0x620938) and its factor per surface
// (0x620984, 24 words).
struct RoadTables {
    ex::Table<8> rolling_curve;
    std::array<F, 24> rolling_mu;
};
constexpr u32 kRollingCurve = 0x620938, kRollingMu = 0x620984;

// 0x351850: each wheel's longitudinal force fraction (curve C) and friction circle (curve D) at its
// slip ratio (0x3598F8: both on curve C's axis).
void tire_curves(ex::Car& c, const TireExtra& x, const WheelLayout& l);
// 0x3518E8: each wheel's longitudinal force and its reaction, on the driving or the braking side.
void tire_long_force(ex::Car& c, const WheelLayout& l, const SlipBytes& sb);
// 0x3534A0: the rolling resistance's force.
void rolling_resistance(ex::Car& c, const WheelLayout& l, const RoadTables& road);

// What the slip force reads of the program's globals: the steering mode (0x845BF0; 1 the wheel's
// offset added to its steer angle, 2 in its place), the camber force's reference angle (degrees,
// 0x620764) and its gain (0x620768), the mean slip angle's switch (0x623E52).
struct SlipForceGlobals {
    u32 steer_mode = 0;
    F camber_ref, camber_gain;
    u8 mean_on = 0;
};
constexpr u32 kSteerMode = 0x845BF0, kCamberRef = 0x620764, kCamberGain = 0x620768, kMeanSlipOn = 0x623E52;
// 0x352408: each wheel's slip angle and its lateral forces: curve A across the velocity and curve B
// against it at the folded slip angle, faded at low speed and held in the friction circle; the camber's
// force for the layout's marked wheels.
void tire_slip_force(ex::Car& c, TireExtra& x, const WheelLayout& l, const SlipForceGlobals& g);

// 0x357690: each wheel's slip direction and its low-speed fade (the engine's 2002 Tokyo-Geneva rule).
void slip_angles(ex::Car& c, const WheelLayout& l);

} // namespace gt4r::sim::rules
