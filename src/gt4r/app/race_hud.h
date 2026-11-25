#pragma once

// The race display drawn from the disc: the pictures of race_display/gt4/<region>/display.gpb,
// the fonts of font/system.fnt, the words of the race's text table, the course's map, and the
// places the game's program gives each element. docs/formats/race_display.md.

#include "gt4r/app/app.h"
#include "gt4r/race/race_display.h"
#include "gt4r/race/strobe.h"

#include <memory>
#include <span>
#include <set>
#include <string>
#include <vector>

namespace gt4r {

// What the display shows this frame.
struct HudState {
    float speed = 0;                  // metres per second
    float rpm = 0, red_line = 7000, rev_limit = 7500;
    float top_speed = 70;             // metres per second: the speedometer's face
    int gear = 1;                     // -1 reverse, 0 neutral
    bool clutch_closed = true;        // the clutch is locked (0x343BE0): the gear's digit in full, else at half
    std::array<int, 3> signs{}; // the stability aid's, the brake's and the oil's signs: 0 out, 1 half lit, 2 lit, 3 blinking
    float gear_flash = 0;             // 1 as a new gear is taken with the clutch locked, falling to 0 in 0.15 s
    bool stepless = false;            // a stepless gearbox: forward "C"
    bool driver_view = false;         // the in-car and roof views' panel, else the chase view's
    int place = 1;                    // 1 the leader
    int lap = 1, laps = 0;            // the lap being driven; the race's laps (0: no count)
    bool no_lap = false;              // no lap count at all
    s64 lap_ms = -1, best_ms = -1, last_ms = -1;
    struct Dot {
        float x = 0, z = 0;
    };
    std::vector<Dot> cars;            // every car's place on the course; the first is the player's
    std::vector<int> letters;         // a network race: each car's letter (0 "A" .. 5 "F", its entry), in place of the dots
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
    bool status_red = false;          // (a penalty's seconds)
    std::string points_title, points_line; // the A-spec points' two lines (0x39C570, 0x39C6D0)
    float points_opacity = 0;
    bool fuel_on = true; // the race counts fuel (its gauge over the chase views)
    bool refuel = false; // the refuel bar (0x2D78), with the fuel and the tank (liters)
    float refuel_fuel = 0, refuel_tank = 0;
    std::string tooltip; // the help strip's line in place of B-spec's (the pit menu's, the refuel bar's); UTF-8
    float logo_opacity = 0;           // a replay's opening: the course's logo at the bottom right
    std::string caption_title, caption_artist; // a replay: the music's caption at the bottom left
    float caption_opacity = 0;
    bool replay = false;              // a replay: its sign in place of the position, laps and times
    bool replay_display = false;      // the replay's display on (else only the logo)
    float opacity = 1;                // of the whole display
    // The panels' smaller parts.
    float lateral_g = 0;              // the car's sideways acceleration in g (+ right)
    float throttle = 0, brake = 0;    // the pedals, 0..1
    int suggested_gear = -1;          // the gear the car's driver would take for the corner ahead (-1 none)
    float trip_m = 0;                 // metres the car ran this race (the trip meter)
    float fuel = 1, tire_wear = 1;    // 0..1 left
    // The tire diagram, drawn only in a race that wears its tires (RaceTireWearDisplay's flag):
    // each wheel's color by its wear level (front left, front right, rear left, rear right), and
    // B-spec's pit command waiting ("P" lit, the box saying "Pit Stop").
    bool tires_on = false;
    u32 tire_rgb[4] = {0, 0, 0, 0}; // 0xBBGGRR
    bool pit_command = false;
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
    // A two-player half (0 the whole picture's display): 1 one above the other, 2 side by side; which of the two.
    int split = 0, half = 0;
    bool event_gap_lead = false; // the gap is a lead (blue), not behind (red)
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
    // Only a license test's three target times with their trophies (over its results page).
    void draw_license_targets(Ui& ui, const HudState& s);
    // The two-player halves' layouts are the program's (HudState::split).
    bool split_ready() const;
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
    // A movie's text field whose words are cut to its box's width with "..." (the Race Monitor's car names: 0x3DA668
    // fits them with 0x48F790 to the field's width).
    void fit_field(const race::StrobeMovie::TextField* field) { fitted_fields_.insert(field); }
    // The course map fitted in a box `size` canvas units across centred on (cx, cy), each car's letter at its
    // place (`lit` the one drawn orange): the Race Monitor's MAP_PANEL.
    struct Letter {
        float x = 0, z = 0;
        char letter = 'A';
    };
    // A paint's chip (its words' colors in bands, the gloss over them) at a place of the canvas.
    void draw_chip(float x, float y, float w, float h, const std::array<u32, 4>& words);
    // The help strip with its line (a movie's page).
    void draw_strip(const std::string& words);
    void draw_map_box(float cx, float cy, float size, std::span<const Letter> cars, size_t lit);
    // The course's logo (piece/crs_logo/<course>.img, a Tex1 picture) for the replays' opening.
    void set_course_logo(ByteSpan tex1);
    // The 640 x 480 canvas on the screen.
    static SDL_FRect canvas();

private:
    std::set<const race::StrobeMovie::TextField*> fitted_fields_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gt4r
