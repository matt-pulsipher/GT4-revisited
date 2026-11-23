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

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gt4r {

namespace sim {
class Session;
}

// A car of the race.
struct WorldEntry {
    std::string label;     // the car's label in the database
    u32 paint = 0;         // by index in the car's paints
    int grid_place = -1;   // the course's start place (-1: the entry's index)
    bool automatic = true; // the gearbox shifts itself
};

struct WorldSetup {
    std::string course;              // a course file's name ("tsukuba")
    std::vector<WorldEntry> entries; // the first is the player's
    bool smoke = true;
    bool reflection = true;
    bool rail = false; // the stand-in simulation instead of the game's
};

class RaceWorld {
public:
    // Throws FormatError (no such course, a car without a race model).
    RaceWorld(App& app, const WorldSetup& setup);
    ~RaceWorld();

    // The cars back on their start places, at rest; the course's animations from their start.
    void restart();
    // One simulation step of 1/60 s. `hold`: the start's hold (the engines answer the throttle, the
    // cars stay).
    void step(std::span<const race::CarInput> inputs, bool hold = false);
    std::span<const race::CarState> cars() const { return sim_->cars(); }
    // What goes with the cars shown in a step, live or recorded: smoke and dust from the wheels,
    // and (`sound`) the cars' sounds heard from car `listener`.
    void present(std::span<const race::CarState> cars, bool sound, size_t listener = 0);
    void clear_effects();
    // The cars' sounds stopped (a pause, a menu).
    void quiet();

    // One frame: `cars` as seen from `pose`; `focus`: the car the view is about (its surroundings
    // make the cars' reflection picture); `hidden`: a car whose body is not drawn (the in-car
    // view's own), -1 for none.
    void render(Ui& ui, const SDL_FRect& rect, std::span<const race::CarState> cars, const race::ViewPose& pose, size_t focus, int hidden);

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
    bool night_ = false, noted_ = false;
    std::unique_ptr<Impl> impl_;
};

} // namespace gt4r
