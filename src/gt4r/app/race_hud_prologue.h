#pragma once

// Gran Turismo 4 Prologue's race display: Gran Turismo 3's display (the shared engine's
// race::Hud) on Prologue's own pictures (race/<region>/display/display.imgs), fonts (font/) and
// layout rows (database/racemode_e3.db, racedetail_e3.db), with the course's map.
// docs/formats/prologue.md "The race display".

#include "gt4r/app/app.h"
#include "gt4r/app/race_hud.h"

#include "race/hud.h"
#include "race/hud_layout.h"

#include <memory>
#include <string>

namespace gt4r {

class PrologueHud {
public:
    // `gtcm`: the course's map section (slot 33). Prologue's tables replace the engine's built-in
    // rows while this lives.
    PrologueHud(App& app, ByteSpan gtcm);
    ~PrologueHud();
    // False when the disc has no Prologue display (its pictures or its layout tables).
    bool ready() const { return ready_; }

    // What the display shows besides the race's HudState.
    struct Extra {
        gt3r::race::HudRun run = gt3r::race::HudRun::Race;
        bool automatic = true;
        bool live = true;              // not a replay
        double countdown = -1;         // seconds to the start, 0 GO, < 0 none
        double finish_t = -1;          // seconds since the player finished
        std::string car_name;          // a replay's car name (game text)
        std::array<double, 3> license_targets{-1, -1, -1};
    };
    void draw(Ui& ui, const HudState& s, const Extra& e);
    // The race's mode (by its sequence) and the display's data, as the engine's display takes them.
    static gt3r::race::HudData data(const HudState& s, const Extra& e);

private:
    std::unique_ptr<gt3r::race::Hud> hud_;
    bool ready_ = false;
};

} // namespace gt4r
