#pragma once

// The race picture's screen settings (the pause's Screen Settings, the options' Picture Quality):
// four numbers -5..+5, brightness, contrast, saturation and color balance, one record for the
// racing views and one for the replays and B-spec races (docs/formats/strobe.md "The pause as
// the program drives it"). The race's last picture pass is corrected by them (0x3EFDF0 ->
// 0x3EFC10): a gain of 2^(brightness / 10) on every channel, the red and blue gains moved by
// -/+ 0.02 x balance (5500 K + 200 K a step against 5500 K), a contrast term 1 - 0.06 x contrast,
// a saturation (saturation + 10) / 10.

#include "render/view3d.h"

#include <array>

namespace gt4r::race {

struct PictureSettings {
    // A new game's: the racing views brightness +3, the rest 0 (the original's options and pause
    // after a new game).
    static constexpr std::array<int, 4> kRaceDefault = {3, 0, 0, 0};
    static constexpr std::array<int, 4> kReplayDefault = {0, 0, 0, 0};
    std::array<int, 4> v = kRaceDefault; // brightness, contrast, saturation, balance

    struct Correction {
        float gain[3] = {1, 1, 1}; // red, green, blue
        float contrast = 1;        // the program's term 1 - 0.06 x contrast
        float saturation = 1;
    };
    Correction correction() const;
    // The correction as the 3D view's grade pass does it: the green gain and the contrast as levels
    // (❓ how the program applies the contrast term is not read: taken as a stretch about the middle
    // by 1 / term), the saturation as the grade's; ❓ the balance (the red and blue gains' shares)
    // is left out.
    gt3r::render::DrawOptions::PostGrade grade() const;
};

} // namespace gt4r::race
