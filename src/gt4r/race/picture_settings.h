#pragma once

// The race picture's screen settings (the pause's Screen Settings, the options' Picture Quality):
// four numbers -5..+5, brightness, contrast, saturation and color balance, one record for the
// racing views and one for the replays and B-spec races (docs/formats/strobe.md "The pause as
// the program drives it"). The race's last picture pass is corrected by them (0x3EFDF0 ->
// 0x3EFC10; the same record the photo mode's corrections are): a gamma of 2^(brightness / 10) on
// every channel, the red and blue ones moved by -/+ 0.02 x balance (5500 K + 200 K a step against
// 5500 K), the white level 1 - 0.06 x contrast, a saturation (saturation + 10) / 10.

#include "gt4r/base/types.h"
#include "render/view3d.h"

#include <array>

namespace gt4r::race {

// The correction's levels (0x4A7D90 -> 0x4A8CB8): for each input level i (x = i / 255), 0 at or under the black level, else
// trunc(255 t^(1 / gamma_c)) held to `most` with t = min((x - black) / (white - black), 1). `most` is 255 for the screen
// settings and the photos, 220 for the Dive camera's own colors, 235 for a course's (Env0 +0x980). In the EE's arithmetic with
// the program's own log and exp series (0x4A8AD0, 0x4A8BB0), level for level the program's (gt4r_diff grade).
std::array<std::array<u8, 3>, 256> correction_levels(const float gamma[3], float black, float white, float most);
// The correction as the 3D view's grade pass: the levels above (with a shared engine that takes a grade's own tables; an older
// one fits its curve through the gammas' middle), then the saturation mix with the saturation held to -1 .. 1 (0x4A7D90:
// more than 1 is 1). Off when the correction changes nothing (0x3EFD08).
gt3r::render::DrawOptions::PostGrade correction_grade(const float gamma[3], float black, float white, float saturation, float most);

struct PictureSettings {
    // A new game's: the racing views brightness +3, the rest 0 (the original's options and pause
    // after a new game).
    static constexpr std::array<int, 4> kRaceDefault = {3, 0, 0, 0};
    static constexpr std::array<int, 4> kReplayDefault = {0, 0, 0, 0};
    std::array<int, 4> v = kRaceDefault; // brightness, contrast, saturation, balance

    struct Correction {
        float gamma[3] = {1, 1, 1}; // red, green, blue: out = in^(1 / gamma)
        float contrast = 1;         // the white level
        float saturation = 1;
    };
    // (0x3EFDF0 in the EE's arithmetic: the program's numbers bit for bit.)
    Correction correction() const;
    // The correction as the 3D view's grade pass does it.
    gt3r::render::DrawOptions::PostGrade grade() const;
};

// The correction from its four numbers (0x3EFC10, which the photo mode's corrections use too), in the EE's arithmetic: a gain of
// 2^b (the program's pow, 0x57D6C0: its own log and exp, 0x57D5A8 and 0x57D488; 1 when b is 0), red and blue moved by -/+
// (kelvin - 5500) x 0.2 / 2000, the white level 1 - k / 10, the saturation as it is.
PictureSettings::Correction screen_correction(float b, float k, float saturation, float kelvin);

} // namespace gt4r::race
