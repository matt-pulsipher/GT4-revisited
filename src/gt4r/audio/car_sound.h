#pragma once

// A car's sounds in a race, mixed as the game mixes them (docs/formats/sound.md "A race's
// sound"):
//
//   three "ENGN" players   the engine's bank, the exhaust's and the wind's (sound/roadnoiz.es):
//                          layers recorded at set revs, crossfaded and pitched by the revs
//                          (layer_player.h, the game's own player);
//   held notes             of the effect banks: the turbo's spool, the gearbox's whine, the
//                          cabin's rush, nitrous, and for each wheel the squeal, the road's
//                          roar and the sounds of the surface under it;
//   single notes           the blow-off valve, contacts, a wheel landing, backfires.
//
// Every sound's level is its own strength x the view's gain for its kind (race_tables.h) x a
// microphone's gain for its distance x the race's master level, panned by where it is from
// the camera; its pitch carries the car's Doppler factor.
//
// Which banks a car plays comes from the car database as the game reads it:
//   sound/engine/<id>           id: the car's ENGINE row, u16 at +0x18
//   sound/<variant>/<id>        variant: "normal" or "turbo" (the car's TURBINEKIT row has a
//                               non-zero byte at +9) and the MUFFLER row's byte at +6 (0..3)

#include "audio/surround_cue.h"
#include "gt4r/audio/layer_player.h"
#include "gt4r/audio/race_tables.h"
#include "gt4r/audio/sound_driver.h"
#include "gt4r/audio/sound_library.h"
#include "gt4r/race/simulation.h"

#include <array>
#include <map>
#include <memory>
#include <string>

namespace gt4r::db {
class SpecDb;
}
namespace gt4r::sim {
struct Equipment;
}

namespace gt4r::audio {

// The sound files of a car.
struct CarSoundFiles {
    int id = 0;          // the engine sound's number
    int muffler = 0;     // 0..3
    bool turbo = false;
    bool whine = false;  // a gearbox that whines: its GEAR row's byte +0x22 (0x4462A0) 🔎 or a car of kind 2 (not read)
    std::string variant; // "normal0" .. "turbo3"
    static std::string variant_name(int index); // index: muffler + 4 for a turbo
    std::string engine;  // "sound/engine/00123"; empty when the car is not in the database
    std::string exhaust; // "sound/normal0/00123"
    int variant_index() const { return muffler + (turbo ? 4 : 0); }
};
CarSoundFiles car_sound_files(db::SpecDb& db, const std::string& car_label); // as sold
// ... with the parts it carries (0x445518, 0x445548, 0x445650, 0x4462A0: its ENGINE, MUFFLER, TURBINEKIT and GEAR rows).
CarSoundFiles car_sound_files(db::SpecDb& db, const sim::Equipment& equipment);
CarSoundFiles car_sound_files(gt3r::Vfs& vfs, const sim::Equipment& equipment);
// The same, opening the database of the disc behind `vfs`; empty when it has none.
CarSoundFiles car_sound_files(gt3r::Vfs& vfs, const std::string& car_label);

// An "ENGN" bank. The shared engine's reader, and for the few banks whose layers are not
// stored in the order of the table, each layer's size from the next sample in the data.
gt3r::sound::EngineBank parse_engine_bank(ByteSpan file);

// A wheel's surface as the sounds tell surfaces apart (the game's surface index): the road's
// material, or for a wet triangle (flags bit 1) 20 for materials 0 and 7, 21 for 1, 10, 11
// and 12, 22 for 9.
int sound_surface(u8 material, u8 flags);

// What a wheel's sounds follow.
struct WheelSoundState {
    race::Vec3 position;    // course space
    int surface = 0;        // sound_surface()
    bool rolling = true;    // on the road, or in the few steps after leaving it
    float slip = 0;         // the slip intensity: 0 gripping, 1 and more sliding
    float pitch = 1;        // the skid's pitch factor
    float rim_speed = 0;    // metres per second
    float width = 0.2f;     // the tire's width, metres
    float landing = 0;      // the speed (m/s, downward) at which the wheel met the road this step
};

// What a car's sounds follow, one step of 1/60 s.
struct CarSoundState {
    bool live = true;                   // a car out of the session is silent
    race::Vec3 position, velocity;      // course space; metres, metres per second
    float course_distance = 0;          // metres along the course (the crowds' gates)
    int rank = 0;                       // the race position, 1 first (the crowds weigh the cars in this order); 0: not known
    float rpm = 0;
    float throttle = 0;                 // the engine's load, 0..1: the throttle, a boosted engine's times its boost (F +0x4F0)
    // The two banks' levels as the simulation keeps them (0x350080's bytes F +0x604 and +0x605), 0..127;
    // below 0: worked out here from the throttle and the torques.
    int engine_level = -1, exhaust_level = -1;
    // The engine's torque at these revs with the throttle open and what its friction takes:
    // the throttle at which the two cancel is where the engine is quietest.
    float torque_full = 1.0f, torque_friction = 0.6f;
    int clutch = 1;                     // 0 open, 1 locked, 2 slipping, 3 in a shift
    bool hybrid = false;                // the engine stops under its idle revs
    float idle_rpm = 0;
    float speed = 0;                    // along the car, metres per second
    float drag = 0.3f;                  // the body's drag coefficient
    float slipstream = 0;               // 0 in clean air .. 1 in another car's wake
    float cabin_wake = 0;               // the same for the cabin's rush
    int engine_place = 1;               // 1 front, 2 middle, 3 rear
    int turbine_level = 0;              // the spool's level, 0..127
    int turbine_pitch = 0;              // 6144: the recording's pitch
    float blow_off = 0;                 // the valve's strength this step
    bool gear_whine = false;            // a gearbox that whines (or the car's files say it does)
    float drive_torque = 0;             // through the clutch, N m (F +0x51C)
    float gear_ratio = 1, low_ratio = 1; // the gear's ratio and the whine's reference
    float shaft_speed = 0;              // the driven axles' speed, rad/s (0x350DA0)
    // A drive that makes its own whine instead of the gearbox's rule (0x34FED0's first two
    // branches): layout 7's motors (0x356D88) and a one-speed box (gearbox kind 3, 0x369510) give
    // the level and pitch themselves, whatever the gearbox's grade and clutch.
    bool whine_given = false;
    float whine_level = 0, whine_pitch = 1;
    bool heart = false;                 // a car of category 3 (P + 0x32, the Nike One): its heartbeat in the in-car view
    bool nitrous = false;               // flowing
    bool afterfire = false;             // a flame started at the exhaust this step
    // ... or the program's rule for one (0x3F3368, 0x3D6D30): the step that ends a shift (the
    // gearbox's lock-out at 11, one step after the clutch locks) with the throttle over 0.9 raises
    // the flame's strength to the car's chance (RaceSoundTables::afterfire_chance); as it rises past
    // 0.1 a flame starts when a random number under 1 falls under it.
    bool shift_flame = false;
    float afterfire_chance = 0;
    float contact = 0;                  // the strongest contact this step, 0..1 (under 0.2: none)
    int contact_kind = 0;               // 0, 1, 2: which of the three contact sounds
    std::array<WheelSoundState, 4> wheels{}; // front left, front right, rear left, rear right
};

// A contact's strength as the program records it (0x350328): kind 0 (a wall) its impulse |j| (N s)
// / 50,000, kind 1 (another car) twice |j| / 15,000, kind 2 (a course object) |value / 50| and at
// least 0.2; at most 1, and under 0.2 none (0).
float contact_strength(int kind, float value);

// From what the simulation's seam gives. What it lacks is estimated: the skid's pitch from
// the slip, the rim's speed from the car's, the engine's quietest throttle (3/8); no turbo or
// whine. The seam's contact (race::CarState::contact, the body's strength) is recorded as the
// program's kinds 0 and 1 (twice the body's strength, contact_strength); a wheel landing is heard at the
// strength of the car's speed downward (m/s) / 5, under 0.5 not at all. 🔎
CarSoundState sound_state(const race::CarState& car);

// Where the sounds are heard from: the camera.
struct SoundListener {
    enum class Kind : u8 { InCar, Chase, Other };
    race::Vec3 position, velocity;
    race::Vec3 right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1};
    Kind kind = Kind::Chase;
    float look = 0; // an in-car view turned to the right, radians
};
// The driving camera of a car: at the car, along its axes.
SoundListener listener_at(const race::CarState& car, SoundListener::Kind kind);

// What every car of a race shares.
struct RaceMix {
    RaceSoundTables tables = RaceSoundTables::flat();
    OutputMode mode = OutputMode::Stereo;
    float master = 1.0f; // the race's effects level: the option x the mix table's x any fade
};

SoundView sound_view(SoundListener::Kind kind, bool target);

// A sound's place from the camera, as the game's panner makes it: the gain for the distance
// and the two sides' shares.
struct Placement {
    float gain = 1, left = 0.70710678f, right = 0.70710678f;
    float x = 0, z = 1; // over the distance: to the right, ahead
};
// `x` to the right and `z` ahead of the camera, metres.
Placement place_sound(const RaceSoundTables::Mic& mic, float x, float z);

// "ENGN" banks read and parsed, by path (nullptr: none on the disc, or bad): a race's cars share the wind's and often an
// engine's.
using EngineBanks = std::map<std::string, std::shared_ptr<const gt3r::sound::EngineBank>>;

class CarSound {
public:
    // `effects` and `library`: for everything but the three "ENGN" players.
    CarSound(gt3r::Vfs& vfs, gt3r::VoiceMixer& mixer, SoundDriver* effects = nullptr, std::shared_ptr<SoundLibrary> library = nullptr);
    ~CarSound();

    // The banks of a car of the database, or by number. False when neither bank reads.
    bool load(const std::string& car_label);
    bool load(const sim::Equipment& equipment); // the car as it is equipped
    bool load(int sound_id, int variant);
    bool load(const CarSoundFiles& files);
    // Banks already read, taken from here and added to it (a race's cars').
    void share_banks(std::shared_ptr<EngineBanks> banks) { banks_ = std::move(banks); }
    bool loaded() const { return engine_.loaded() || exhaust_.loaded(); }
    const CarSoundFiles& files() const { return files_; }

    // The race's tables and levels (flat ones until set).
    void set_mix(std::shared_ptr<const RaceMix> mix);

    // One step: 1/60 s. `target`: the camera is this car's.
    void update(const CarSoundState& car, const SoundListener& listener, bool target);
    void stop();

    // What the last step set, for tests and tools.
    struct Report {
        SoundView view = SoundView::Other;
        float doppler = 1;
        int engine_level = 0, exhaust_level = 0; // 0..127
        Placement engine, body, exhaust;
        float engine_gain = 0, exhaust_gain = 0, wind_gain = 0; // before the two sides' shares
        float wind_rpm = 0;
        float turbine = 0, whine = 0, cabin = 0;
        std::array<std::array<float, 10>, 4> wheel{}; // each wheel's ten held notes
        int notes = 0; // single notes started
        int heartbeats = 0; // heartbeats so far (heard or not)
    };
    const Report& report() const { return report_; }
    // The three layer players: the engine's, the exhaust's, the wind's.
    const LayerPlayer& engine() const { return engine_; }
    const LayerPlayer& exhaust() const { return exhaust_; }
    const LayerPlayer& wind() const { return wind_; }

    // The held notes of a wheel, in the order of Report::wheel.
    enum WheelNote { OnSkill, OnRoad, Rain, OffRoad, OffSkill, Pebble, Sand, Rumble, Rumble2, Rumble3, kWheelNotes };
    static const char* wheel_note_name(int note);

private:
    struct Effect {
        std::shared_ptr<const sound::InstBank> bank;
        int program = -1;
        explicit operator bool() const { return bank && program >= 0; }
    };
    struct Loop {
        u32 id = 0;
        float held = 0; // the eased level
    };
    Effect effect(const char* name);
    void resolve();
    void send(Loop& loop, const Effect& e, float level, const Placement& at, float pitch);
    // A level that rises at once and falls by a twentieth a step.
    static float ease(Loop& loop, float level);
    void play_once(const Effect& e, float strength, float gain, const Placement& at);
    void stop_loops();
    // What the race writes to a layer player each step (0x392640): the revs, the two sides of
    // `level` at `at`, the Doppler factor, the priority.
    void feed(LayerPlayer& player, float rpm, float level, const Placement& at, float doppler);

    gt3r::Vfs& vfs_;
    gt3r::VoiceMixer& mixer_;
    SoundDriver* effects_;
    std::shared_ptr<SoundLibrary> library_;
    std::shared_ptr<const RaceMix> mix_;
    CarSoundFiles files_;
    LayerPlayer engine_, exhaust_, wind_;
    std::shared_ptr<EngineBanks> banks_;
    bool wind_tried_ = false, resolved_ = false;
    int engine_level_ = 0, exhaust_level_ = 0;
    bool was_afterfire_ = false; // the flame's strength over 0.1 last step
    u32 random_ = 0x2545F491;
    int heart_count_ = 0;
    float heart_phase_ = 0; // the heartbeat's phase (car + 0x788 for a car of category 3)

    Effect turbine_fx_, whine_fx_, cabin_fx_, nitrous_fx_, blow_off_fx_, landing_fx_, backfire_fx_[2], clash_fx_[3], heartbeat_fx_;
    std::array<Effect, kWheelNotes> wheel_fx_{};
    Loop turbine_, whine_, cabin_, nitrous_;
    gt3r::SurroundCue cue_[3], wheel_cue_[4]; // the engine's, the body's, the exhaust's places; each wheel's
    std::array<std::array<Loop, kWheelNotes>, 4> wheels_{};
    Report report_;
};

} // namespace gt4r::audio
