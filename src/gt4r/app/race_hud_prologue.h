#pragma once

// Gran Turismo 4 Prologue's race display (its RaceDisplay, 0x388FC8): the layout rows of database/racemode_e3.db and
// racedetail_e3.db (Gran Turismo 3's tables), the panels' places in the program's code, the pictures of
// race/<region>/display/display.imgs, the fonts of font/race.fnt and the words of rtext/raceJP.rt2.
// docs/formats/prologue.md "The race display".

#include "gt4r/app/app.h"
#include "gt4r/app/race_hud.h"

#include "race/hud_layout.h"

#include <array>
#include <memory>
#include <string>

namespace gt4r {

class PrologueHud {
public:
    // `gtcm`: the course's map section (slot 33). Prologue's tables replace the engine's built-in
    // rows while this lives.
    PrologueHud(App& app, ByteSpan gtcm);
    ~PrologueHud();
    // False when the disc has no Prologue display (its pictures, its layout tables or its program).
    bool ready() const { return ready_; }
    // The course's logo (the course file's slot 49, a Tex1 picture) of the race's opening.
    void set_course_logo(ByteSpan tex1);

    // What the display shows besides the race's HudState.
    struct Extra {
        gt3r::race::HudRun run = gt3r::race::HudRun::Race;
        bool live = true;                     // not a replay
        double start_left = -1;               // seconds left of the start's hold (< 0: running), `start_total` the whole hold
        double start_total = 0;
        double since_start = 1e9;             // seconds since the start
        double finish_t = -1;                 // seconds since the player finished
        std::array<s64, 3> targets{-1, -1, -1}; // a test's gold, silver and bronze times (ms)
        float over_t = -1;   // seconds since a test's run ended (-1 not): the display fades out
        bool passed = false; // ... with a medal: its target times stay
        int penalty = 0;                      // the seconds left of the player's penalty
        u8 asm_status = 0, tcs_status = 0;    // 0 off, 1 on, 2 acting
        float rev_limit = 7000, shift_rpm = 6500; // the limiter's and the automatic up-shift's revolutions (P + 0x1C4, + 0x226)
        bool parking_brake = false;
        bool failed = false;                  // the test failed (the result's message)
        bool onboard = false;                 // the view's meters are the car's own (not a start camera's)
        bool wrong_way = false;               // the wrong-way sign (0x3C0878)
        bool auto_drive = false;              // the car's driver brings it to the line
        int time_left = 0;                    // the seconds left of a test's lead (the rule block's +0x44)
        int cones = 0, cones_total = 0;       // the cones knocked down, and a test's count of them (0: none counted)
        bool turbo = false;                   // the car has a boost gauge
        float boost = 0;                      // its pressure (F + 0x4F0)
        float accelerator = 0, brake = 0;     // the pedals (a replay's monitor)
        // A hybrid's: the box's flags (sim::rules::HybridState), its store 0 .. 1, the mileage (km per liter) and the fuel (liters).
        bool hybrid = false;
        u8 hybrid_flags = 0;
        float hybrid_store = 0, hybrid_mileage = 0, hybrid_fuel = 0;
        bool guide = false;                   // a lesson's guide film: the recorded run's monitors alone
    };
    void draw(Ui& ui, const HudState& s, const Extra& e);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool ready_ = false;
};

} // namespace gt4r
