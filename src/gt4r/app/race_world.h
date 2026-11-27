#pragma once

// A race's world as the Drive screen shows it: the course, the cars on the simulation behind
// race/simulation.h, their sounds, and everything the view draws around them (the sun, the lamps'
// glows, billboards, smoke, the road's reflection). The race's flow, its cameras and its display
// are the screen's (screen_race.cpp); this draws a frame for a camera and a set of car states,
// live or recorded.

#include "gt4r/formats/replay/demonstration.h"
#include "gt4r/app/app.h"
#include "base/ee_float.h"
#include "gt4r/race/car_visual.h"
#include "gt4r/race/course_scene.h"
#include "gt4r/race/dive_camera.h"
#include "gt4r/race/gadget_bodies.h"
#include "gt4r/race/race_start.h"
#include "gt4r/race/rail_simulation.h"
#include "gt4r/race/replay_effects.h"
#include "gt4r/race/replay_script.h"
#include "gt4r/race/simulation.h"
#include "gt4r/net/car_service.h"
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
    int pedal_style = -1;  // (sim::Entry::pedal_style)
    bool asm_on = true, tcs_on = true; // the aids (sim::Entry)
    bool wheel_aid = false;            // the steering wheel's slip aid (sim::Entry::wheel_aid)
    u8 driver = 0;                     // who drives (sim::Entry::driver)
    float start_speed = 0; // a rolling start: metres per second at time 0 (sim::Entry::start_speed)
    std::optional<sim::Equipment> equipment; // the car's parts and settings (a garage car's); none: as sold
    std::optional<std::array<u8, 0x20>> condition; // a garage car's condition block (sim::Entry::condition)
    std::string wing; // a fitted wing's label (a WING row); empty: the equipment's (record +0x100), else none
    int start_kind = 0; // a rolling start's kind (RaceFlow's; 0: 2 behind the line, 1 past it); 6 a pit start on its grid place
    float start_distance = std::numeric_limits<float>::quiet_NaN(); // a rolling start's place (sim::Entry::start_distance)
    std::optional<course::Runway::Place> place; // the car's own start place (a license test's road section: sim::Entry::place)
    bool remote = false; // another player's car of an online race: posed from its owner's states (net/net_race.h), its driver plans only
};

struct WorldSetup {
    std::string course;              // a course file's name ("tsukuba")
    std::vector<WorldEntry> entries; // the first is the player's
    bool smoke = true;
    bool reflection = true;
    bool drivers = true; // the drivers in their cars
    bool rail = false; // the stand-in simulation instead of the game's
    sim::ai::Pace pace; // the computer drivers' (the event's)
    // The event's class grips as they are (a license test's, training's, mission's and practice's conductor); else a byte the
    // event leaves 0 is 20 (a single race's and the free run's: 0x353570).
    bool event_grips = false;
    std::vector<u8> event_row; // the event's RACE row: the race's tire wear (sim::rules::event_wear_bytes); empty: no wear
    // The race's conductor (as far as its start goes) and its road section (CFG +0x21; -1 none): a license test's, a pace car
    // test's and a mission's cars start by the event's row (race/start_rule.h RowStart).
    race::Conductor conductor = race::Conductor::Other;
    int section = -1;
    bool own_hold = false; // the race counts its start's hold itself, from the set-up's state (no steps of the session's own)
    // An online race's rules (a battle's conductor sets them in place of the event's: net/lan_rules.h): the tire damage (-1: no
    // such race) and the boost.
    int lan_wear = -1, lan_boost = 0;
    int laps = 0;       // the race's laps (the pit stops' last lap; 0 a run without an end)
    int players = 1;    // the first cars are players' (two in the two-player battle), the others the computer's
    bool computer_player = false; // the computer drivers drive the player's car too
    // The B-spec driver's book (menu::bspec::Book: three tables of 8 x 8) and the course's marks, which give the race's
    // record of the player's driver (0x468710) when B-spec drives his car.
    std::array<u16, 3 * 64> bspec_book{};
    std::optional<std::array<u8, 0x40>> bspec_info; // a recorded race's: the driver's record as the recording keeps it (the race information's +0x20)
    std::array<u8, 8> bspec_course_marks{};
    bool bspec_course_known = false;
    // The game's options (menu::GameState::options: the sound's volumes and output) and the race
    // play list; none: a new game's.
    std::map<std::string, std::variant<s32, float, std::string>> options;
    std::shared_ptr<audio::RacePlaylist> playlist;
};

namespace net {
class RemoteCar;
}

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
    // An entry's condition block from the next restart (a garage car's, as the last run left it: each run of a sequence
    // starts from the riding car's, 0x10F948).
    void set_condition(size_t entry, const std::optional<std::array<u8, 0x20>>& c) {
        if (entry < setup_.entries.size()) setup_.entries[entry].condition = c;
    }
    // The computer drivers drive the player's car too (B-spec, a scripted run) from the next restart.
    void set_computer_player(bool on) { computer_player_ = on; }
    // The game's computer drivers drive the cars (false: the caller's inputs, e.g. the stand-in's
    // simple driver, for every car).
    bool computer_drivers() const { return field_ != nullptr; }
    // One simulation step of 1/60 s. `hold`: the start's hold (the engines answer the throttle, the
    // cars stay).
    // `player_by_computer`: when the computer drivers were set up for the player's car too, they drive
    // it this step (else its input is the caller's: a rolling start handed over).
    // `hold_left`: the hold's steps left in this step (the race information's +0xBC).
    void step(std::span<const race::CarInput> inputs, bool hold = false, bool player_by_computer = true, u32 hold_left = 0);
    std::span<const race::CarState> cars() const { return sim_->cars(); }
    // How the cars start (race/race_start.h): what their starts read, each car's, its crossings of the line at the start.
    const race::StartRace& start_race() const { return start_course_->race(); }
    const race::PlannedStart& start_of(size_t car) const { return starts_[car]; }
    int start_crossings(size_t car) const;
    // Its backward crossings to take back first (F + 0x4B0: a pit start's, race::planned_back).
    int start_back(size_t car) const;
    // The race's count of a car's crossings of the line, for the simulation (F +0x4AC).
    void set_back_crossings(size_t car, u8 n) {
        if (session_) session_->set_back_crossings(car, n);
    }
    void set_crossings(size_t car, int n) {
        if (session_) session_->set_crossings(car, n);
    }
    void set_rank(size_t car, int place) {
        if (session_) session_->set_rank(car, place);
    }
    // A rolling start (an entry with a start speed): each such car is held to the start's pace until it is
    // released as it takes the line (DynamicsConductorSinglePlayer::vf20 0x347928 → 0x345228). A released car
    // stays released until the next restart.
    // With the game's simulation the session holds the cars itself (F +0x467 = 1, the aids' start pace 0x3F7880 /
    // 0x3F76E8) and lets each go (0x345228, 0x3F37B8); the stand-in simulation is held here.
    bool count_rolling(size_t car) { return session_ && session_->count_rolling(car); }
    // A car's run is over (ai::Field::finish; the session's driver kind follows its driver's each step). False: no driver.
    bool finish_car(size_t car, int mode, bool player) {
        if (!field_ || !session_ || computer_player_) return false;
        field_->finish(car, mode, player);
        if (ended_.size() <= car) ended_.resize(car + 1);
        ended_[car] = true;
        return true;
    }
    // A car's boost gauge (Prologue's): whether it has a turbocharger, and its pressure (F + 0x4F0).
    void boost(size_t car, bool& turbo, float& pressure) const {
        if (!session_) return;
        const gt3r::sim::exact::Car& c = session_->record(car);
        turbo = c.p.e.boost_max.host() != 0, pressure = c.intake.host();
    }
    // A hybrid's box as the display shows it (Prologue's; false: another car).
    bool hybrid(size_t car, u8& flags, float& store, float& fuel) const {
        if (!session_ || !session_->prologue_rules() || session_->record(car).p.d.kind != 3) return false;
        const sim::prologue::CarPart& p = session_->prologue_part(car);
        flags = p.hybrid.flags, store = float(int(p.hybrid.store.host() * 8 + 0.5f)) / 8, fuel = p.hybrid_fuel.host() / 740;
        return true;
    }
    // The mileage of a hybrid's run so far (Prologue's 0x3CC320, tenths of a kilometer per liter; 0 for another car).
    float mileage_now(size_t car) const { return float(session_->prologue_part(car).mileage) * 0.1f; } // (of the last 30 steps)
    int mileage(size_t car) const {
        if (!session_ || !session_->prologue_rules()) return 0;
        const sim::prologue::CarPart& p = session_->prologue_part(car);
        return session_->record(car).p.d.kind == 3 ? sim::prologue::mileage(p.hybrid_fuel_mark, p.hybrid_odo_mark) : 0;
    }
    void release_start(size_t car) {
        // (its driver's line first: a driver without it is not let go)
        if (session_ && field_ && rolling(car) && !field_->release_rolling(car)) return;
        if (sim_ && (session_ ? rolling(car) : car < pace_kmh_.size() && pace_kmh_[car] > 0)) sim_->release_start(int(car));
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
    // An online race: what a pit stop did to car 0 outside its state in the last step (net/car_service.h), oldest first; `after`
    // the step (the pit pass, the refuelling) or before it (the stop's timing, with each step's hold counter). Noted once asked.
    struct ServiceNote {
        bool after = false;
        net::ServiceEvent::Kind kind = net::ServiceEvent::Hold;
        u32 value = 0;
    };
    void note_service(bool on) { service_on_ = on, service_.clear(); }
    std::vector<ServiceNote> take_service() { return std::exchange(service_, {}); }
    // And the program's pit tire change for the others' predictions (NetRace::set_tire_change); empty without the program.
    std::function<void(sim::Session& solo, net::RemoteCar& car)> prediction_tire_change() const;
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
    // The PIT STOP menu of the player's car (0x35BC70): the stop's choice just asked (service 0x10 newly set), with what
    // may be chosen; nullopt otherwise. Asked each step.
    struct PitOffer {
        bool tires = false, fuel = false, bspec = false;
    };
    std::optional<PitOffer> pit_offer(size_t car);
    // The menu's end (0x35BCE8): the car off the pit lane again, or its B-spec wait about to run out, or the choice taken.
    bool pit_offer_over(size_t car) const;
    // The player at the menu (0x35BCC8): B-spec's wait stopped.
    void pit_wait(size_t car);
    // The kinds of tire the pit may fit to an axle (0 front, 1 rear), in order: those of the fitted kind's class
    // (0x4076E0 by 0x441300), and the kind fitted.
    std::vector<int> pit_tire_kinds(size_t car, int axle) const;
    int fitted_tire(size_t car, int axle) const;
    // The choice (0x35BF08): tire kinds (0xFF none), fuel.
    void pit_choose(size_t car, u8 front, u8 rear, bool fuel) {
        if (pits_ && car < car_visuals_.size()) pits_->choose(car, front, rear, fuel ? 1 : 0);
    }
    // The PIT STOP menu's driving mode (0x3F19E0 command 10): the player's car driven by the pad, or by the B-spec driver.
    void set_driving_mode(bool bspec);
    // The car held to its rolling start's pace (the opening shots' end); nullopt without the game's own step.
    std::optional<bool> held_to_pace(size_t car) const { return session_ && !course_->road().gtrw ? std::optional<bool>(session_->held_to_pace(car)) : std::nullopt; }
    // The A-spec points the race offers the player's car (race/aspec_points.h).
    int aspec_points(bool mission) const;
    // Where the car is in a pit visit (0x35BBD8): 0 none, 1 down the lane or waiting for the choice, 2 the stop's start, 3 its work,
    // 4 its end, 5 out of the box to the lane's end.
    int pit_stage(size_t car) const;
    // F +0x62E (the arcade's "Passing pit lane" mark, the pit pass's pad visit): 0 none.
    int pit_mark(size_t car) const { return session_ && car < car_visuals_.size() ? session_->pit_mark(car) : 0; }
    // A car's race is over (0x343D78: the computer cruises it on, or as `mode` asks: ai::Field::finish); a test's player's: the
    // computer's cars too (0x3F88E8).
    void finish_car(size_t car, bool test, int mode = 3) {
        if (!field_ || !session_) return;
        field_->finish_run(*session_, car, mode);
        if (test) field_->end_computers();
    }
    // B-spec's pit command taken back or given (0x3F19E0 command 6: sim::ai::PitStops::command).
    void pit_command(size_t car);
    void pit_command(size_t car, bool on) {
        if (pits_ && car < car_visuals_.size()) pits_->command(car, on);
    }
    // The player's car put at the moment a free run's recording starts at (its record, formats/replay.md).
    bool resume(const replay::RecorderState& state);
    // The player's car by a recording: its pit choices by its records alone (after the restart); the choice made in the last step
    // for a B-spec car whose wait ran out.
    void pits_by_record() {
        if (pits_) pits_->set_recorded(0);
    }
    bool pit_took_default(size_t car) const { return pits_ && pits_->took_default(car); }
    // The refuelling: just begun (0x35BE10: service 0x20 taken), going on (0x35BEB0), stopped by the player (0x35BFC0).
    bool refuel_begun(size_t car);
    bool refuelling(size_t car) const;
    // The pit's camera for a car (race/pit_scene.h), now or as it was before the last step; false: the race's view.
    bool pit_camera(size_t car, race::ViewPose& out, bool before = false) const;
    // A license test's staff (race/license_scene.h): the run's result once it has ended with a time (passed, or slower than every
    // target), the scene's own camera (now or before the last step; false: the race's view) and the race's picture brightness.
    void license_result(bool passed);
    bool license_camera(race::ViewPose& out, bool before = false) const;
    float license_brightness() const;
    void stop_refuel(size_t car) {
        if (pits_ && car < car_visuals_.size()) pits_->choose(car, 0xFF, 0xFF, 0, 2);
    }
    // The race counts fuel (its gauge shown).
    bool counts_fuel() const { return counts_fuel_; }
    float fuel(size_t car) const { return session_ && car < car_visuals_.size() ? session_->fuel(car) : 0; }
    float tank(size_t car) const { return session_ && car < car_visuals_.size() ? session_->tank(car) : 0; }
    // What goes with the cars shown in a step, live (the simulation's) or recorded: smoke and dust
    // from the wheels, and the race's sound heard from `camera` (none: no sound) about car `target`.
    // `heard`: a recorded step's simulation run again (a saved replay's recording): heard in place of the states.
    void present(std::span<const race::CarState> cars, const audio::SoundListener* camera, int target, bool live, const sim::Session* heard = nullptr);
    // The same with the cameras of a split screen's views, each about the car of its number.
    void present(std::span<const race::CarState> cars, std::span<const audio::SoundListener> cameras, bool live);
    void clear_effects();
    // The race's sound stopped (a menu).
    void quiet();
    // The race's sound (null without sound): its events (the count, the finish, a pause).
    audio::RaceSound* sound();

    void set_cars_hidden(bool on) { cars_hidden_ = on; } // (a lesson's guide film: the course alone)
    // The best lap's ghost (screen_race.cpp): the player's car once more, drawn see-through at a state of its own, no shadow,
    // no driver, no sound; none: not drawn.
    void set_ghost(const std::optional<race::CarState>& state, float alpha) { ghost_state_ = state, ghost_alpha_ = alpha; }
    // One frame: `cars` as seen from `pose`; `focus`: the car the view is about (its surroundings
    // make the cars' reflection picture); `hidden`: a car whose body is not drawn (the in-car
    // view's own), -1 for none. `half`: one of the two views of a split screen (0 and 1: the several-view draw
    // 0x33FD88: the course's own lists and flags for it, its far plane half as far), -1 the race's one view.
    void render(Ui& ui, const SDL_FRect& rect, std::span<const race::CarState> cars, const race::ViewPose& pose, size_t focus, int hidden, int half = -1);
    // The rear-view mirror in `rect`: from `pose` (the in-car view) looking back, mirrored; the course
    // by its mirror lists and sky, the cars but `hidden`, the mirror's own fog, 120 m deep (a split screen's half 60);
    // `zoom`: its lens against the view's (0x341E50: 1.3, a stacked half's 2).
    void render_mirror(Ui& ui, const SDL_FRect& rect, std::span<const race::CarState> cars, const race::ViewPose& pose, int hidden, int half = -1, float zoom = 1.3f);
    // A VR headset's frame (app/race_vr.h): the scene collected once from `head` (its eye between the eyes, its lens the eyes'
    // height), then drawn for each eye into its canvas (a render target, `w` x `h`) from its own place and off-centre lens. The
    // reflections are looked up from `reflection_view`, the race camera's view, so turning the head does not turn them.
    struct Eye {
        gt3r::render::Mat4 view;
        gt3r::render::Vec3 eye;
        gt3r::render::Frustum frustum; // the tangents of its half angles (left and down negative)
        SDL_Texture* canvas = nullptr;
        int w = 0, h = 0;
    };
    void render_eyes(Ui& ui, std::span<const race::CarState> cars, const race::ViewPose& head, const gt3r::render::Mat4& reflection_view,
                     std::span<const Eye> eyes, size_t focus, int hidden);
    // The picture's correction (the screen settings, race/picture_settings.h) from the next frame.
    void set_grade(const gt3r::render::DrawOptions::PostGrade& g) { grade_ = g; }
    // The next frame's passes of a replay (race/replay_effects.h), and the player's choices about them.
    void set_effects(const race::EffectView& v, const race::ReplayEffectSettings& s) { effect_view_ = v, effect_settings_ = s; }
    // The motion trail: the last picture shown over the next one at this weight (0 none).
    void set_trail(float weight) { trail_ = weight; }
    // The markers of a piece of music (its .adm beside `file`; track 0), none when it has none.
    race::MusicMarkers music_markers(const std::string& file) const;
    // The Dive director's tables, from the disc's program.
    bool load_dive(race::DiveDirector& d) const;
    // What the director sees of the cars' states.
    race::DiveDirector::World dive_world(std::span<const race::CarState> cars, const race::ReplayScript& script) const;
    // The depth of car `i`'s nearest point before a view (its box's corners; negative behind it), for the camera's focus.
    float nearest_depth(size_t i, const race::CarState& state, const gt3r::render::Mat4& view) const;
    // The next frame's dissolve: `snapshot` freezes the last picture shown, drawn over the new one at
    // `weight`; `keep_last` keeps each picture for a later snapshot.
    void set_dissolve(bool snapshot, float weight, bool keep_last) { snapshot_ = snapshot, dissolve_ = weight, keep_last_ = keep_last; }

    // The photo mode's lens on the next frames (none: the plain one): a shear of the projection (photo/picture.h), the depth of
    // field, the cars in their nearest level of detail.
    struct Lens {
        float shear_x = 0, shear_y = 0;
        gt3r::render::DrawOptions::DepthOfField dof;
        bool full_detail = true;
        const gt3r::render::ScreenBatch* vignette = nullptr;
    };
    void set_lens(std::optional<Lens> lens) { lens_ = std::move(lens); }
    // A car stood still on the road at (x, z) of the course, `heading` in radians (Session::place_standing: the photo mode's 0x3C75F0).
    void stand_car(size_t car, float x, float z, float heading);

    // The cars' icons (the results page's): each car standing side-on, its front to the left, drawn
    // over `canvas` (the 640 x 480 page on the screen) with its middle at (x, y) of the page.
    struct Icon {
        size_t car = 0;
        float x = 0, y = 0;
        float opacity = 1;
        bool large = false; // the maker's other picture (kind 0: 128 x 64, from the front at an angle)
    };
    // A car's paint chip's words (race::paint_chip).
    std::array<u32, 4> paint_chip(size_t car) const;
    std::pair<float, float> car_size(size_t i) const;
    void draw_icons(Ui& ui, const SDL_FRect& canvas, std::span<const Icon> icons);
    // One model in its own view over `rect` (the license results' trophy), in the course's light:
    // `place` its model matrix in a camera at the origin looking down -z, `proj` the lens.
    // `lights`: the model's own light set (none: the course's).
    void draw_model(Ui& ui, const SDL_FRect& rect, const gt3r::render::GpuModel& model, const gt3r::render::Mat4& place, const gt3r::render::Mat4& proj, float alpha, bool own_reflection = false,
                    const gt3r::render::ItemLights* lights = nullptr);

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
    // The signs' states of a car (RaceDisplay::vf2 0x39E280): the aids' (the greater of ASCC's and TCS's status: on 1, acting 3), the
    // handbrake past 0.25 (2), the oil past its distance (2).
    std::array<int, 3> signs(size_t i) const;
    // The car's gearbox is a stepless one (0x3F1F78).
    // The race display's G meter (RaceDisplay::vf2 0x39E1E0 into RaceSimplePanel::vf27): the car's follower of its
    // lateral acceleration (F +0x560, 0x35DE20) over -9.81, in g: false off Gran Turismo 4's rules.
    bool meter_g(size_t i, float& g) const {
        if (!session_ || !session_->gt4_rules() || i >= session_->cars().size()) return false;
        g = -session_->gt4_record(i).speeds.g_follow.host() / 9.81f;
        return true;
    }
    bool stepless(size_t i) const { return session_ && i < car_visuals_.size() && session_->gt4_record(i).drive.d.kind == 1; }
    // A car's fuel left as a share of its tank (1 without a tank).
    float fuel_share(size_t i) const;
    // A car's top speed as the program estimates it from the gearing (P +0xFD8, m/s; 0 not known): the
    // speedometer's face.
    float car_top_speed(size_t i) const;
    // A car's top speed while racing (car +0x6CC, m/s: the most of its speed, 0x35DEC4, kept until its run ends) and its speed
    // now (+0x6C8) if more: a machine test's top speed at its goal (vf23 0x3F6C30).
    gt3r::ee::F car_max_speed(size_t i) const;
    // The materials a wheel counts as off the course on (the program's 0x622430, a word per material).
    bool soft_material(u8 material) const { return material < soft_.size() && soft_[material] != 0; }
    // The cars move by the game's simulation (false: the stand-in).
    bool simulated() const { return session_ != nullptr; }
    bool night() const { return night_; }
    const WorldSetup& setup() const { return setup_; }
    // The game's simulation (null: the stand-in runs), and its entries as the race was set up with them.
    const sim::Session* session() const { return session_.get(); }
    sim::Session* session() { return session_.get(); }
    // How solid a car is drawn (a ghost of an online race 0.5; 1).
    void set_car_alpha(size_t car, float alpha) {
        if (car >= alpha_.size()) alpha_.resize(car + 1, 1.0f);
        alpha_[car] = alpha;
    }
    float car_alpha(size_t car) const { return car < alpha_.size() ? alpha_[car] : 1.0f; }
    const std::vector<sim::Entry>& session_entries() const { return entries_; }
    // What the player chose for his own car and controls: the gearbox, the aids, the steering wheel's (its lock by the options'
    // AMATEUR .. SIMULATION: the entry's +0x344C, its active steering +0x3458, its force's strength and assist). True when the
    // cars must be set up anew for it (the next restart).
    struct PlayerChoice {
        bool automatic = true, asm_on = true, tcs_on = true, wheel_aid = false;
        u8 wheel_lock = 0;
        gt3r::s16 wheel_strength = 1, wheel_assist = 1;
        bool operator==(const PlayerChoice&) const = default;
    };
    bool set_player(const PlayerChoice& c, size_t player = 0);
    // A car's parts and settings from the next restart on (Arcade's Quick Tune on the player's car: sim/quick_tune.h).
    void set_equipment(size_t car, const sim::Equipment& eq) {
        if (car < setup_.entries.size()) setup_.entries[car].equipment = eq;
    }
    // The player's car's power as built, PS (0: not known).
    float player_power_ps() const { return player_ps_; }
    const gt3r::render::DrawStats& draw_stats() const;

private:
    std::optional<std::array<gt3r::ee::F, 11>> hand_speeds_; // the drivers' (race/driver_state.h), from the program
    void open_pit_scene();
    void step_pit_scene();
    void open_license_scene();
    void step_license_scene();
    struct Impl;
    // render's two halves: the frame's scene from one place (the items, the cars' reflection picture), then a view of it.
    struct Scene {
        gt3r::render::DrawOptions options;
        float far = 0;
        size_t wet_at = 0; // where the road's reflection goes among the items (after the scenery, before the cars)
        bool wet = false;
        int hidden = -1;
        bool split = false;
    };
    Scene collect(std::span<const race::CarState> cars, const race::ViewPose& pose, size_t focus, int hidden, int half, float aspect, float lines);
    void draw_view(Ui& ui, gt3r::render::View3D& view3d, const SDL_FRect& rect, std::span<const race::CarState> cars, const race::ViewPose& pose,
                   const gt3r::render::Mat4& proj, const gt3r::render::Frustum& frustum, const Scene& scene);
    void build_simulation();
    void arm_bspec(const sim::Program& program);

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
    std::vector<sim::Entry> entries_;
    std::unique_ptr<sim::ai::Field> field_; // the computer drivers, on the game's simulation
    std::unique_ptr<sim::ai::PitStops> pits_; // their pit stops
    bool counts_fuel_ = false;
    mutable std::optional<float> oil_from_;   // the oil's distance its sign lights from
    std::vector<u8> pit_asked_;               // each car's choice was already asked at the last look (pit_offer)
    std::function<std::pair<int, int>(size_t)> tires_fitted_; // (their hook: the kinds on the car)
    std::shared_ptr<const sim::Program> program_; // (what their tire change reads)
    bool service_on_ = false;                 // car 0's service noted (note_service)
    std::vector<ServiceNote> service_;
    bool bspec_driven_ = false;               // a B-spec driver drove the player's car (its record set)
    bool computer_player_ = false, cars_hidden_ = false;
    std::optional<race::CarState> ghost_state_; // set_ghost
    float ghost_alpha_ = 0.5f;
    std::vector<bool> ended_; // the cars whose run is over (finish_car)
    float player_ps_ = 0;
    std::unique_ptr<race::StartCourse> start_course_;
    std::vector<race::PlannedStart> starts_;
    void plan_starts(const sim::Program& program, std::vector<sim::Entry>& entries);
    void first_lines(const sim::Program& program);
    std::vector<float> pace_kmh_; // each car's rolling start speed until released (0: none)
    race::GadgetBodies gadget_bodies_; // the cones and boards as the cars leave them
    unsigned gadget_hits_ = 0;
    std::vector<std::optional<WorldEntry>> overrides_;
    std::vector<float> alpha_; // set_car_alpha
    std::vector<PlayerChoice> choices_{1}; // by player
    // The car is a player's, driven by his pad (not by the B-spec driver or a scripted run's computer).
    bool pad_car(size_t i) const { return i < size_t(setup_.players) && !(i == 0 && computer_player_); }
    // Or another player's, on his own machine (an online race).
    bool players_car(size_t i) const { return pad_car(i) || (i < setup_.entries.size() && setup_.entries[i].remote); }
    std::array<u32, 32> soft_{};
    struct Command {
        int pace = 3;
        bool overtake = false;
    };
    std::vector<Command> commands_;
    bool night_ = false, noted_ = false;
    gt3r::render::DrawOptions::PostGrade grade_;
    bool snapshot_ = false, keep_last_ = false;
    float dissolve_ = 0, trail_ = 0;
    race::ReplayEffects effects_;
    race::EffectView effect_view_;
    race::ReplayEffectSettings effect_settings_;
    std::optional<Lens> lens_;
    std::unique_ptr<Impl> impl_;
};

} // namespace gt4r
