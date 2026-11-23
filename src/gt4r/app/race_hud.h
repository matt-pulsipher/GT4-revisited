#pragma once

// The race display drawn from the disc: the pictures of race_display/gt4/<region>/display.gpb,
// the fonts of font/system.fnt, the words of the race's text table, the course's map, and the
// places the game's program gives each element. docs/formats/race_display.md.

#include "gt4r/app/app.h"
#include "gt4r/race/race_display.h"

#include <memory>
#include <string>
#include <vector>

namespace gt4r {

// What the display shows this frame.
struct HudState {
    float speed = 0;                  // metres per second
    float rpm = 0, red_line = 7000, rev_limit = 7500;
    float top_speed = 70;             // metres per second: the speedometer's face
    int gear = 1;                     // -1 reverse, 0 neutral
    bool driver_view = false;         // the in-car and roof views' panel, else the chase view's
    int place = 1;                    // 1 the leader
    int lap = 1, laps = 0;            // the lap being driven; the race's laps (0: no count)
    s64 lap_ms = -1, best_ms = -1, last_ms = -1;
    struct Dot {
        float x = 0, z = 0;
    };
    std::vector<Dot> cars;            // every car's place on the course; the first is the player's
    double time = 0;                  // seconds since the race began, for the pulsing marker
    std::string message;              // the big middle message ("3", "START", "FINISH"); UTF-8
    float message_opacity = 0;
    std::string event;                // the line under the times ("L1/3   1'23.456")
    float event_opacity = 0;
    float opacity = 1;                // of the whole display
};

class RaceHud {
public:
    // `gtcm`: the course's map section (slot 33).
    RaceHud(App& app, ByteSpan gtcm);
    ~RaceHud();
    // False when the disc's program is not the one the places were read from, or a file is
    // missing: nothing is drawn.
    bool ready() const;
    void draw(Ui& ui, const HudState& s);
    // The race's words by key ("RaceStart"); the key itself when the table has none.
    std::string word(const std::string& key) const;
    bool mph() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gt4r
