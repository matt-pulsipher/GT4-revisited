#pragma once

// A race's world as the Drive screen shows it: the course, the cars on the simulation behind
// race/simulation.h, their sounds, and everything the view draws around them (the sun, the lamps'
// glows, billboards, smoke, the road's reflection). The race's flow, its cameras and its display
// are the screen's (screen_race.cpp); this draws a frame for a camera and a set of car states,
// live or recorded.

#include "gt4r/app/app.h"
#include "gt4r/race/car_visual.h"
#include "gt4r/race/course_scene.h"
#include "gt4r/race/gadget_bodies.h"
#include "gt4r/race/rail_simulation.h"
#include "gt4r/race/replay_script.h"
#include "gt4r/race/simulation.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/ai/pit_stops.h"
#include "gt4r/sim/equipment.h"

#include <array>
#include <future>
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
    bool asm_on = true, tcs_on = true; // the aids (sim::Entry)
    float start_speed = 0; // a rolling start: metres per second at time 0 (sim::Entry::start_speed)
    std::optional<sim::Equipment> equipment; // the car's parts and settings (a garage car's); none: as sold
    std::string wing; // a fitted wing's label (a WING row); empty: the equipment's (record +0x100), else none
    float start_distance = std::numeric_limits<float>::quiet_NaN(); // a rolling start's place (sim::Entry::start_distance)
    std::optional<course::Runway::Place> place; // the car's own start place (a license test's road section: sim::Entry::place)
};

struct WorldSetup {
    std::string course;              // a course file's name ("tsukuba")
    std::vector<WorldEntry> entries; // the first is the player's
    bool smoke = true;
    bool reflection = true;
    bool drivers = false; // the drivers in their stored pose (the posture is not ported: for checks)
    bool rail = false; // the stand-in simulation instead of the game's
    sim::ai::Pace pace; // the computer drivers' (the event's)
    std::vector<u8> event_row; // the event's RACE row: the race's tire wear (sim::rules::event_wear_bytes); empty: no wear
    int laps = 0;       // the race's laps (the pit stops' last lap; 0 a run without an end)
    bool computer_player = false; // the computer drivers drive the player's car too
    // The B-spec driver's book (menu::bspec::Book: three tables of 8 x 8) and the course's marks, which give the race's
    // record of the player's driver (0x468710) when B-spec drives his car.
    std::array<u16, 3 * 64> bspec_book{};
    std::array<u8, 8> bspec_course_marks{};
    bool bspec_course_known = false;
    // The game's options (menu::GameState::options: the sound's volumes and output) and the race
    // play list; none: a new game's.
    std::map<std::string, std::variant<s32, float, std::string>> options;
    std::shared_ptr<audio::RacePlaylist> playlist;
};

class RaceWorld {
public:
    // The B-spec driver's book and the course's marks the next run's driver starts from (a run again in the same sequence).
    void set_bspec_book(const std::array<u16, 3 * 64>& book, const std::array<u8, 8>& marks) { setup_.bspec_book = book, setup_.bspec_course_marks = marks; }
    // Throws FormatError (no such course, a car without a race model).
    RaceWorld(App& app, const WorldSetup& setup);
    ~RaceWorld();

    // The cars back on their start places, at rest; the course's animations from their start.
    void restart();
    // The entries' cars as a demonstration's file enters them (each its equipment, grid place, gearbox and
    // aids; sim/ai/demo_driver.h), or back as set up (empty); the world starts again.
    void set_entry_overrides(std::vector<std::optional<WorldEntry>> overrides);
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
    // A rolling start (an entry with a start speed): each such car is held to the start's pace until it is
    // released as it takes the line (DynamicsConductorSinglePlayer::vf20 0x347928 → 0x345228). A released car
    // stays released until the next restart.
    // With the game's simulation the session holds the cars itself (F +0x467 = 1, the aids' start pace 0x3F7880 /
    // 0x3F76E8) and lets each go (0x345228, 0x3F37B8); the stand-in simulation is held here.
    void release_start(size_t car) {
        if (car < pace_kmh_.size() && pace_kmh_[car] > 0 && sim_) sim_->release_start(int(car));
        if (car < pace_kmh_.size()) pace_kmh_[car] = 0;
    }
    // Whether a car is still held to the start's pace (its driver kind 1, F +0x467).
    bool rolling(size_t car) const {
        if (session_ && sim_ && car < sim_->cars().size()) return sim_->cars()[car].rolling;
        return car < pace_kmh_.size() && pace_kmh_[car] > 0;
    }
    // B-spec's commands for a car the computer drives: its pace 1 .. 5 (3 the race's own) and the
    // overtake, to its driver (sim::ai::Field::command; kept for the next restart).
    void command(size_t car, int pace, bool overtake) {
        if (car >= commands_.size()) commands_.resize(car + 1);
        commands_[car] = {pace, overtake};
        if (field_) field_->command(car, {pace - 1, overtake ? 1 : 0});
    }
    // B-spec's pit command (Circle) for a car the computer drives: it takes the pit lane at its entry,
    // stops in its box 12 s with new tires and goes out again (sim/ai/pit_stops.h). False without the drivers.
    bool pit(size_t car) {
        if (!pits_) return false;
        pits_->request(car);
        return true;
    }
    // The car's pit message (F +0x5FA, 0x35ACD8's codes; 0 none) and its stops made.
    int pit_message(size_t car) const { return pits_ && car < car_visuals_.size() ? pits_->message(car) : 0; }
    // The B-spec driver's record after the race (car +0x8C, 0x40 bytes) and the car's class (0x468418); nullopt when
    // no B-spec driver drove the car.
    std::optional<std::array<u8, 0x40>> bspec_record(size_t car) const;
    int bspec_class(size_t car) const;
    // The race wears its tires (its rules on), and a wheel's wear level as the display takes it (-0x80 .. 0x7F).
    bool wears_tires() const { return session_ && (session_->wear_rules().tread_on || session_->wear_rules().second_on); }
    int tire_level(size_t car, int wheel) const { return session_ ? session_->wear_level(car, wheel) : -0x80; }
    // B-spec's pit command waiting for this car (0x3F19E0 command 6: F +0x4B1 bit 3).
    bool pit_requested(size_t car) const { return pits_ && car < car_visuals_.size() && (pits_->car(car).flags & 8) != 0; }
    int pit_stops(size_t car) const { return pits_ && car < car_visuals_.size() ? pits_->stops(car) : 0; }
    // A car's last pit stops, newest first (the lap, the tires fitted; lap 0 none): the Race Monitor's pit panel.
    std::array<sim::ai::PitStops::StopRecord, 4> pit_record(size_t car) const {
        return pits_ && car < car_visuals_.size() ? pits_->stop_record(car) : std::array<sim::ai::PitStops::StopRecord, 4>{};
    }
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
    // One model in its own view over `rect` (the license results' trophy), in the course's light:
    // `place` its model matrix in a camera at the origin looking down -z, `proj` the lens.
    void draw_model(Ui& ui, const SDL_FRect& rect, const gt3r::render::GpuModel& model, const gt3r::render::Mat4& place, const gt3r::render::Mat4& proj, float alpha, bool own_reflection = false);

    const race::CourseScene& course() const { return *course_; }
    const race::CarVisual& visual(size_t i) const { return *car_visuals_[i]; }
    const std::string& car_name(size_t i) const { return names_[i]; }
    // The short name the results show ("VW Beetle 1100 Standard (Type-11) `49").
    const std::string& car_short_name(size_t i) const { return short_names_[i]; }
    const std::string& car_label(size_t i) const { return setup_.entries[i].label; }
    size_t car_count() const { return car_visuals_.size(); }
    // The road's line for the simple driver, made on a thread of its own from the race's set-up (it walks
    // the road across every checkpoint: seconds on the long test course); asking waits for it.
    const race::RailLine& line() const;
    float lap_length() const; // the road's lap (m)
    // The cars that touched a cone or board in the last step (a bit each, the first 32).
    unsigned gadget_hits() const { return gadget_hits_; }
    // The cones and boards car `i` met in the last step (each a contact report of kind 2).
    int gadget_contacts(size_t i) const;
    // The strongest impulse |j| of car i's contacts with the course's objects this step (0: none).
    float gadget_impulse(size_t i) const;
    // A car's mass as the step reads it (the spec's weight, kg; 0 not known): the license tests' wall rule.
    float car_mass(size_t i) const;
    // The seconds left of a car's penalty (the speed cap a hit gave; 0: none).
    int penalty_seconds(size_t i) const;
    // A car's stability and traction controls: 0 off, 1 on, 2 acting (Prologue's record F + 0x524 / 0x525).
    void aid_status(size_t i, u8& asm_status, u8& tcs_status) const;
    // A car's limiter and automatic up-shift revolutions (P + 0x1C4 and + 0x226; 0 not known).
    void rev_marks(size_t i, float& limit, float& shift) const;
    // The suggested gear for a car the player drives (-1 none).
    int suggested_gear(size_t i) const;
    // A car's fuel left as a share of its tank (1 without a tank).
    float fuel_share(size_t i) const;
    // A car's top speed as the program estimates it from the gearing (P +0xFD8, m/s; 0 not known): the
    // speedometer's face.
    float car_top_speed(size_t i) const;
    // The materials a wheel counts as off the course on (the program's 0x622430, a word per material).
    bool soft_material(u8 material) const { return material < soft_.size() && soft_[material] != 0; }
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
    std::vector<std::string> names_, short_names_;
    mutable std::optional<race::RailLine> line_; // the road's line: the simple driver's guide
    mutable std::future<race::RailLine> line_job_;
    race::Simulation* sim_ = nullptr;
    std::unique_ptr<race::RailSimulation> rail_; // the stand-in, when the simulation is not running
    std::unique_ptr<sim::Session> session_;
    std::unique_ptr<sim::ai::Field> field_; // the computer drivers, on the game's simulation
    std::unique_ptr<sim::ai::PitStops> pits_; // their pit stops
    bool bspec_driven_ = false;               // a B-spec driver drove the player's car (its record set)
    bool computer_player_ = false;
    float player_ps_ = 0;
    std::vector<float> pace_kmh_; // each car's rolling start speed until released (0: none)
    race::GadgetBodies gadget_bodies_; // the cones and boards as the cars leave them
    unsigned gadget_hits_ = 0;
    std::vector<std::optional<WorldEntry>> overrides_;
    std::array<u32, 32> soft_{};
    struct Command {
        int pace = 3;
        bool overtake = false;
    };
    std::vector<Command> commands_;
    bool night_ = false, noted_ = false;
    gt3r::render::DrawOptions::PostGrade grade_;
    bool snapshot_ = false, keep_last_ = false;
    float dissolve_ = 0;
    std::unique_ptr<Impl> impl_;
};

} // namespace gt4r
