#pragma once

// The timing of the loading page between menu projects and before a race (app/menu_loading.h), apart from the window so a test
// can check it. Two styles (Settings > Display, [video] loading_page): "quick" (default) and "original" (the program's sequence).
//
//   Original, between projects: black in over 1 s; the picture and tachometer in over the next 1 s; the next project starts once
//   the black is whole; the page holds until max(t, 2) + 0.5 s; out in 0.25 s. Before a race: the last page held, the tachometer
//   fading in from 0.2 s, the race read once 1.2 s are shown.
//   Quick, between projects: black in over 0.25 s, no picture, the next project starts at once, out in 0.25 s (no hold).
//   Quick, before a race: the held page shows for one frame, then the race reads; the tachometer fades in (from 0.25 s, over
//   0.25 s) only if the reading takes that long.

#include <algorithm>

namespace gt4r::loading {

constexpr float kFadeOut = 0.25f;

struct Timing {
    bool quick = true;

    // Seconds of the black fading in; once past, the next project may start under it.
    float cover() const { return quick ? 0.25f : 1.0f; }
    float black(float t) const { return std::clamp(t / cover(), 0.0f, 1.0f); }
    // The picture and tachometer's strength (the quick style shows none between projects).
    float picture(float t) const { return quick ? 0.0f : std::clamp(t - 1.0f, 0.0f, 1.0f); }
    // The time the page starts to fade out, given the time the next project started.
    float ends_at(float t) const { return quick ? t : std::max(t, 2.0f) + 0.5f; }
    // The fade out: 1 whole, 0 gone.
    static float out(float t, float ending) { return ending >= 0 && t > ending ? std::clamp(1.0f - (t - ending) / kFadeOut, 0.0f, 1.0f) : 1.0f; }
    static bool gone(float t, float ending) { return ending >= 0 && t >= ending + kFadeOut; }

    // Before a race (the capture): the tachometer's strength over the held page, and when the race may be read (`drawn`: frames
    // shown).
    float capture_tacho(float t) const { return quick ? std::clamp((t - 0.25f) / 0.25f, 0.0f, 1.0f) : std::clamp(t - 0.2f, 0.0f, 1.0f); }
    bool capture_ready(float t, int drawn) const { return quick ? drawn >= 1 : t >= 1.2f; }
};

} // namespace gt4r::loading
