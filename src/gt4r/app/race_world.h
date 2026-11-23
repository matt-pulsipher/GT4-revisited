#pragma once

// A race's world as the Drive screen shows it: the course, the cars on the simulation behind
// race/simulation.h, their sounds, and everything the view draws around them (the sun, the lamps'
// glows, billboards, smoke, the road's reflection). The race's flow, its cameras and its display
// are the screen's (screen_race.cpp); this draws a frame for a camera and a set of car states,
// live or recorded.

#include "gt4r/app/app.h"
#include "gt4r/race/car_visual.h"
#include "gt4r/race/course_scene.h"
#include "gt4r/race/rail_simulation.h"
#include "gt4r/race/replay_script.h"
#include "gt4r/race/simulation.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/equipment.h"

#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <variant>
#include <span>
#include <string>
#include <vector>

namespace gt4r {

namespace sim {
class Session;
}
namespace audio {
class RaceSound;
class RacePlaylist;
struct SoundListener;
} // namespace audio

// A car of the race.
struct WorldEntry {
    std::string label;     // the car's label in the database
    u32 paint = 0;         // by index in the car's paints
    int grid_place = -1;   // the course's start place (-1: the entry's index)
    bool automatic = true; // the gearbox shifts itself
    float start_speed = 0; // a rolling start: metres per second at time 0 (sim::Entry::start_speed)
    std::optional<sim::Equipment> equipment; // the car's parts and settings (a garage car's); none: as sold
    float start_distance = std::numeric_limits<float>::quiet_NaN(); // a rolling start's place (sim::Entry::start_distance)
};

struct WorldSetup {
    std::string course;              // a course file's name ("tsukuba")
    std::vector<WorldEntry> entries; // the first is the player's
    bool smoke = true;
    bool reflection = true;
    bool rail = false; // the stand-in simulation instead of the game's
    sim::ai::Pace pace; // the computer drivers' (the event's)
    bool computer_player = false; // the computer drivers drive the player's car too
    // The game's options (menu::GameState::options: the sound's volumes and output) and the race
    // play list; none: a new game's.
    std::map<std::string, std::variant<s32, float, std::string>> options;
    std::shared_ptr<audio::RacePlaylist> playlist;
};

class RaceWorld {
public:
    // Throws FormatError (no such course, a car without a race model).
    RaceWorld(App& app, const WorldSetup& setup);
    ~RaceWorld();

    // The cars back on their start places, at rest; the course's animations from their start.
    void restart();
    // The computer drivers drive the player's car too (B-spec, a scripted run) from the next restart.
    void set_computer_player(bool on) { computer_player_ = on; }
    // The game's computer drivers drive the cars (false: the caller's inputs, e.g. the stand-in's
    // simple driver, for every car).
    bool computer_drivers() const { return field_ != nullptr; }
    // One simulation step of 1/60 s. `hold`: the start's hold (the engines answer the throttle, the
    // cars stay).
    // `player_by_computer`: when the computer drivers were set up for the player's car too, they drive
    // it this step (else its input is the caller's: a rolling start handed over).
    void step(std::span<const race::CarInput> inputs, bool hold = false, bool player_by_computer = true);
    std::span<const race::CarState> cars() const { return sim_->cars(); }
    // What goes with the cars shown in a step, live (the simulation's) or recorded: smoke and dust
    // from the wheels, and the race's sound heard from `camera` (none: no sound) about car `target`.
    void present(std::span<const race::CarState> cars, const audio::SoundListener* camera, int target, bool live);
    void clear_effects();
    // The race's sound stopped (a menu).
    void quiet();
    // The race's sound (null without sound): its events (the count, the finish, a pause).
    audio::RaceSound* sound();

    // One frame: `cars` as seen from `pose`; `focus`: the car the view is about (its surroundings
    // make the cars' reflection picture); `hidden`: a car whose body is not drawn (the in-car
    // view's own), -1 for none.
    void render(Ui& ui, const SDL_FRect& rect, std::span<const race::CarState> cars, const race::ViewPose& pose, size_t focus, int hidden);
    // The rear-view mirror in `rect`: from `pose` (the in-car view) looking back, mirrored; the course
    // by its mirror lists and sky, the cars but `hidden`, the mirror's own fog, 120 m deep.
    void render_mirror(Ui& ui, const SDL_FRect& rect, std::span<const race::CarState> cars, const race::ViewPose& pose, int hidden);
    // The picture's correction (the screen settings, race/picture_settings.h) from the next frame.
    void set_grade(const gt3r::render::DrawOptions::PostGrade& g) { grade_ = g; }
    // The next frame's dissolve: `snapshot` freezes the last picture shown, drawn over the new one at
    // `weight`; `keep_last` keeps each picture for a later snapshot.
    void set_dissolve(bool snapshot, float weight, bool keep_last) { snapshot_ = snapshot, dissolve_ = weight, keep_last_ = keep_last; }

    // The cars' icons (the results page's): each car standing side-on, its front to the left, drawn
    // over `canvas` (the 640 x 480 page on the screen) with its middle at (x, y) of the page.
    struct Icon {
        size_t car = 0;
        float x = 0, y = 0;
        float opacity = 1;
    };
    void draw_icons(Ui& ui, const SDL_FRect& canvas, std::span<const Icon> icons);

    const race::CourseScene& course() const { return *course_; }
    const race::CarVisual& visual(size_t i) const { return *car_visuals_[i]; }
    const std::string& car_name(size_t i) const { return names_[i]; }
    const std::string& car_label(size_t i) const { return setup_.entries[i].label; }
    size_t car_count() const { return car_visuals_.size(); }
    const race::RailLine& line() const { return line_; }
    // The cars move by the game's simulation (false: the stand-in).
    bool simulated() const { return session_ != nullptr; }
    bool night() const { return night_; }
    const WorldSetup& setup() const { return setup_; }
    // The player's car's power as built, PS (0: not known).
    float player_power_ps() const { return player_ps_; }
    const gt3r::render::DrawStats& draw_stats() const;

private:
    struct Impl;
    void build_simulation();

    App& app_;
    WorldSetup setup_;
    std::unique_ptr<race::CourseScene> course_;
    std::vector<race::CarVisual*> car_visuals_;
    std::vector<std::string> names_;
    race::RailLine line_; // the road's line: the simple driver's guide, and the lap's length
    race::Simulation* sim_ = nullptr;
    std::unique_ptr<race::RailSimulation> rail_; // the stand-in, when the simulation is not running
    std::unique_ptr<sim::Session> session_;
    std::unique_ptr<sim::ai::Field> field_; // the computer drivers, on the game's simulation
    bool computer_player_ = false;
    float player_ps_ = 0;
    bool night_ = false, noted_ = false;
    gt3r::render::DrawOptions::PostGrade grade_;
    bool snapshot_ = false, keep_last_ = false;
    float dissolve_ = 0;
    std::unique_ptr<Impl> impl_;
};

} // namespace gt4r
