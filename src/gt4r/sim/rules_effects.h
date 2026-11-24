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
// From the program's bytes (0x210 from kSlideBytes).
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

} // namespace gt4r::sim::rules
