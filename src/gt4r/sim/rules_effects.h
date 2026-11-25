#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): each wheel's sliding measures (the tire wear's input, the skid
// sound's level and pitch, the smoke and dust) and the car's dirt, with the tables the race's start builds for them.

#include "gt4r/sim/rules.h"

namespace gt4r::sim::rules {

struct WheelLayout;

// One measure's curves for a kind of surface (0x84 bytes at 0x843A20 / 0x843C30 / 0x843E40 + 0x84 kind).
struct SlideCurves {
    ex::Table<8> a; // +0x00 (x), +0x10 (y), its table +0x20: on the hub's sideways speed or the slip angle (4 points)
    ex::Table<8> b; // +0x2C (x), +0x4C (y), its table +0x6C: on the slide speed or the slip ratio
    F base;         // +0x78 added to a measure that is not 0
    F knee;         // +0x7C the speed ramp's threshold
    F gain;         // +0x80 the measure's gain on the speed ramp above it
};
// What 0x366D68 (the conductor's configuration) builds from the program's bytes 0x6224B0 .. 0x6226B4 (after 0x3FAA10's
// fix-ups): [measure][kind] for the measures 0 (wheel +0xB4, the sliding measure), 1 (+0xB8, the skid's pitch) and 2 (+0xBC,
// the smoke and dust), the kinds of surface 0 .. 3; the inputs of each (the row's bytes +0x00 / +0x1F: not 0, curve a on
// the sideways speed, curve b on the slide speed); the loose ground's measures 0x844050 (the sliding measure's scale, the
// pitch's two speeds and values).
struct SlideTables {
    std::array<std::array<SlideCurves, 4>, 3> set;
    std::array<std::array<u8, 4>, 3> by_lat{}, by_slide{};
    F loose_scale, loose_x0, loose_y0, loose_x1, loose_y1;
};
constexpr u32 kSlideBytes = 0x6224B0, kSlideCurves = 0x843A20, kSlideLoose = 0x844050;
// The program's bytes 0x6224B0 .. 0x6226B4 with 0x3FAA10's fix-ups (each curve 4 points, the speed knees at most 50 km/h,
// the second measure's base 50 .. 200, four pairs of the cue bytes in order).
std::array<u8, 0x210> effect_bytes(const u8* raw);
// From the program's bytes (0x210 from kSlideBytes, as the image holds them).
SlideTables slide_tables(const u8* bytes);
SlideTables slide_tables(const Program& program);

// The kind of surface (0 .. 2) by its index (the jump table 0x69F7B0 on index - 2): 2 .. 5, 13, 14 kind 1; 6, 20 .. 22 kind 2;
// the others 0.
int slide_kind(u8 surface);

// 0x367148 (phase 19): each wheel's sliding measures (+0xB4, +0xB8, +0xBC; while a wheel has been off the ground six steps
// or more 0, 1, 0) from its slip angle or sideways speed and its slip ratio or slide speed through the curves of its kind
// of surface and its speed ramp; on surfaces 1, 10, 11 and 21 the sliding measure and the pitch by the load and the rim's
// speed instead; an odd wheel's pitch x 0.75 at the step its sliding starts. The smoke and dust of the wheels on a surface
// of kind 1 or 2 (surface 12 counts as kind 1) dirty the car (the condition block's +0xC, at most 0.8; x 4 on a course of
// kind 0; none for a car whose condition block's byte +0x14 is set). `course_kind`: the conductor's byte (0x34C968).
void slide_effects(ex::Car& c, const WheelLayout& l, const SlideTables& t, u8 course_kind);

// What 0x3667A8 hands the course's effects for a wheel (its record of 0x3C bytes, to 0x395C70 with the car's index F + 0x440;
// and with the car's velocity to 0x395C98 too when the ground's flags have bit 5): the smoke, dust and spray a wheel throws.
struct TireEmit {
    s32 wheel = 0;               // +0x00 the wheel
    u32 surface = 0;             // +0x04 its surface index (wheel +0x89)
    std::array<F, 3> point;      // +0x08 where, in the course's frame (x, height, z)
    F heading;                   // +0x14 the car's heading + the wheel's steer angle
    F attr;                      // +0x18 the ground record's +0x14
    std::array<F, 3> normal;     // +0x1C the ground's normal (ground record +0x1C)
    F plane;                     // +0x28 the ground plane's +0x28 less 0.05 x the normal's height: 5 cm above the road
    F smoke;                     // +0x2C the smoke and dust (wheel +0xBC)
    F slip_ratio;                // +0x30 wheel +0x38
    F hub_speed;                 // +0x34 the hub's speed along the car (wheel +0x28)
    F rim_speed;                 // +0x38 wheel +0x20
    std::array<F, 3> velocity;   // the car's velocity in the course's frame (x, height, z)
    u8 car = 0;                  // the car's index (F + 0x440)
    bool spray = false;          // the ground's flags bit 5 (0x20): to 0x395C98 as well
};
// 0x3668F0 (phase 19, after the wheel spin): the wheels' emits, as 0x3667A8 makes them: each wheel of the layout at its hub
// (the car's place + the hub offset) on its ground's height; a car of category 6 (P + 0x32) one emit per wheel group (the
// layout's +0x0C + k for k 0, 1, with its partner +0x24 + w): at the middle of the two wheels' hubs and ground heights, for
// the wheel whose ground record's +0x14 is the larger (the group's first on a tie). Returns how many of `out` it filled.
int tire_emits(const ex::Car& c, const WheelLayout& l, u8 category, std::array<TireEmit, 4>& out);

// 0x366678: how a car is drawn (3 a car in the race; a ghost: 0 not drawn, 1 hidden, 2 see-through): a car not in the race
// (F + 0x441) of control source 4 by F + 0x4B2 (0 when set, else 2); otherwise with neither the race's word +0xE0 nor its
// course's ghost word (`race_e0`, `ghost_word`) 1; a ghost of control source 2 or driver kind 1, 6 or 8 0; else 2.
int draw_mode(const ex::Car& c, u8 x4b2, u32 race_e0, u32 ghost_word);
// 0x366718 (phase 19): a car not drawn as one in the race (draw mode not 3) has its sliding measures cleared (the pitch 1)
// and the engine sound's levels and the turbo's spool (F + 0x604 .. + 0x606) 0.
void effects_reset(ex::Car& c, const WheelLayout& l, int draw_mode);

} // namespace gt4r::sim::rules
