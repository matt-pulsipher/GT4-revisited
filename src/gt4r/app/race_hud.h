#pragma once

// The race display drawn from the disc: the pictures of race_display/gt4/<region>/display.gpb,
// the fonts of font/system.fnt, the words of the race's text table, the course's map, and the
// places the game's program gives each element. docs/formats/race_display.md.

#include "gt4r/app/app.h"
#include "gt4r/race/race_display.h"
#include "gt4r/race/strobe.h"

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
    std::string event_gap;            // a section time's gap to the car ahead at the split ("+0'05.800"), under the line
    float event_gap_opacity = 0;
    std::string card_title, card_line; // a replay's opening card: the car, its power, tires and the race
    float card_opacity = 0;
    std::string status;               // the status line over the panel ("FULL THROTTLE"); UTF-8
    float status_opacity = 0;
    float logo_opacity = 0;           // a replay's opening: the course's logo at the bottom right
    std::string caption_title, caption_artist; // a replay: the music's caption at the bottom left
    float caption_opacity = 0;
    bool replay = false;              // a replay: its sign in place of the position, laps and times
    bool replay_display = false;      // the replay's display on (else only the logo)
    float opacity = 1;                // of the whole display
    // The panels' smaller parts.
    float lateral_g = 0;              // the car's sideways acceleration in g (+ right)
    float throttle = 0, brake = 0;    // the pedals, 0..1
    float trip_m = 0;                 // metres the car ran this race (the trip meter)
    float fuel = 1, tire_wear = 1;    // 0..1 left
    // B-spec (the player directs, the computer drives his car): display mode 1's places (the total
    // time in place of the best and last laps), the commands' bar and the help strip.
    bool bspec = false;
    int pace = 3;                     // 1 .. 5 (the words key_pace1 .. 5)
    bool overtake = false;            // the overtake command is on
    s64 total_ms = -1;                // the race's time
    bool help = true;                 // the help strip along the bottom
    // A license test (the race's kind 3): no place, the three target times with their trophies in place
    // of the last lap (ms; <= 0 none).
    bool license = false;
    s64 targets[3] = {-1, -1, -1};
    // The display's parts the options leave on (race_display_*).
    bool show_map = true, show_time = true, show_lap = true, show_position = true, show_meter = true;
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
    // The speed in mph (else km/h): the options' unit (unit_velocity); the disc's region by default.
    void set_mph(bool on);
    // A race movie (the pause menu, the results) as its player shows it now, over the whole canvas:
    // its shapes, pictures and texts (the texts' words looked up in the race's text table).
    // `externals`: the pictures the program supplies (the results' car icons), where they go on the
    // canvas, for the caller to draw.
    struct MovieExternal {
        std::string name;
        float x = 0, y = 0; // the canvas (640 x 480)
        float scale = 1;    // the placed picture's scale
        float opacity = 1;
    };
    void draw_movie(const race::StrobeMovie& movie, const race::StrobePlayer& player, std::vector<MovieExternal>* externals = nullptr);
    // The course's logo (piece/crs_logo/<course>.img, a Tex1 picture) for the replays' opening.
    void set_course_logo(ByteSpan tex1);
    // The 640 x 480 canvas on the screen.
    static SDL_FRect canvas();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gt4r
