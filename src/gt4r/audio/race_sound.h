#pragma once

// A race's sound, everything the Drive screen hears (docs/formats/sound.md "A race's sound"):
// every car's sounds (car_sound.h) at the race's master level, the course's ambience, tunnels and
// reverb (course_sound.h), the count's beeps and the race's music.
//
//   audio::RaceSound sound(vfs, mixer, app_sound.driver(), app_sound.library_ptr(), tables, streams);
//   sound.add_car("nsx_r_02");  ...                         (in grid order)
//   sound.set_course(course.section(course::kSlotSound), runway);
//   each step:   sound.update(session, camera, target_car)  (or update(states, camera, target))
//   the count:   sound.count(frames_left) every step of it  (3, 2, 1 beep; at 0 the start)
//   a rolling start: sound.rolling() on its first step (the racing music), then
//                sound.start_signal() when the player's car reaches the line (the start's sound)
//   events:      sound.contact(car, kind, force), sound.finish(), sound.pause(true / false)
//   the replay:  sound.replay() as it begins (the replay's levels; the race's first piece again),
//                then sound.set_replay_shot(designer) every step (the master's duck)
//
// The master level of the race's effects is the race effects option (on / off, 0..128 of 128)
// x the mix table's race level for the output mode x a fade (in over 0.3 s as the race begins).

#include "gt4r/audio/car_sound.h"
#include "gt4r/audio/course_sound.h"
#include "gt4r/audio/menu_sound.h"
#include "gt4r/audio/race_playlist.h"

#include <map>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace gt4r::course {
struct Runway;
}
namespace gt4r::sim {
class Session;
class RoadGround;
} // namespace gt4r::sim

namespace gt4r::audio {

// The volume options of the game's record (raw 0..128; a new game has every flag on and 115).
struct RaceSoundOptions {
    bool effects_on = true, music_on = true;
    int effects = 115, music = 115; // the race's
    bool replay_effects_on = true, replay_music_on = true;
    int replay_effects = 115, replay_music = 115; // a replay's
    OutputMode mode = OutputMode::Stereo;
};

// The options of the game's record (menu::GameState::options: race_se, race_se_volume_raw, ...,
// sound_type) as the race's sound takes them; what is missing keeps the new game's value.
RaceSoundOptions race_sound_options(const std::map<std::string, std::variant<s32, float, std::string>>& options);

// Which pair of jingles a race plays at its start and its finish (the race type sets them).
// (Race: the races of the arcade and the events; Championship and Last: a series' races and its last; TimeTrial: the free
// runs and the machine test; License: the tests and the driving trainings; Coffee: the license center's coffee breaks;
// Mission: the missions; Other: the free practice.)
enum class RaceMusic : u8 { Race, Championship, TimeTrial, License, Coffee, Other, Last, Mission };
// A race's kind: the sequence's name (as the menus name it), the event's label (a license's letter: a coffee break's is c),
// whether it is a series' last race.
RaceMusic race_music(const std::string& sequence, const std::string& label, bool last_race);
// Whether the racing music starts at the start (the race's virtual +79, 0x389D48; the licenses' and trainings' is empty:
// 0x3BC528, 0x3ED180).
bool racing_music(RaceMusic kind);
// The streamed music groups (sound_index.h) of a kind's start and finish.
const char* race_start_group(RaceMusic kind);
const char* race_goal_group(RaceMusic kind);

class RaceSound {
public:
    RaceSound(gt3r::Vfs& vfs, gt3r::VoiceMixer& mixer, SoundDriver& driver, std::shared_ptr<SoundLibrary> library, const RaceSoundTables& tables,
              StreamPlayer music = {});
    ~RaceSound();
    RaceSound(const RaceSound&) = delete;
    RaceSound& operator=(const RaceSound&) = delete;

    // The cars in grid order. False when the car has no engine sound (it stays silent).
    // With `equipment` the car as it is equipped (its engine, muffler, turbine kit and gearbox), else as sold.
    bool add_car(const std::string& label, const sim::Equipment* equipment = nullptr);
    bool add_car(db::SpecDb& db, const std::string& label, const sim::Equipment* equipment = nullptr); // (the car database already open)
    bool add_car(int sound_id, int variant);
    size_t cars() const { return cars_.size(); }
    CarSound& car(size_t i) { return *cars_[i]; }

    // The course's ambience emitters (its CRSS section) and road (for the tunnels and the gates).
    void set_course(ByteSpan crss, std::shared_ptr<const course::Runway> road);
    void set_options(const RaceSoundOptions& o);
    void set_music_kind(RaceMusic kind) { kind_ = kind; }
    // The player's play list of racing music (the game's options): its next piece plays at each
    // race's start. Without one the group's pieces play in the group's order.
    void set_playlist(std::shared_ptr<RacePlaylist> list) { playlist_ = std::move(list); }
    // A helicopter's sound overhead (a replay's view from the air).
    void set_helicopter(bool on) { helicopter_ = on; }
    // A split screen's other views' cameras (0x3B5E80: car i is heard as its own view's, i its number; 0x398B70: the course by
    // the greatest of them all).
    void set_more_cameras(std::vector<SoundListener> cameras) { more_cameras_ = std::move(cameras); }

    // One step of 1/60 s: the cars' states in grid order, the camera, and the camera's car (-1: none).
    void update(std::span<const CarSoundState> cars, const SoundListener& camera, int target);
    void update(const sim::Session& session, const SoundListener& camera, int target);
    void update(std::span<const race::CarState> cars, const SoundListener& camera, int target);

    // --- events ---
    // Every step of the count with the steps left to the start: a beep at 180, 120 and 60; the
    // start's sound and the racing music at 0.
    void count(int frames_left);
    // A standing start: the start's sound and the racing music.
    void start();
    // A rolling start: the racing music as the race begins to run, the start's sound at the line.
    void rolling();
    void start_signal();
    // The replay begins: the replay's two options instead of the race's, the race's first piece
    // from its beginning.
    void replay();
    // A contact of car `car` (the camera's car only is heard): kind 0 a wall (the impulse |j|, N s),
    // 1 a car (|j| / 15,000), 2 a course object; its strength as contact_strength() makes it.
    void contact(size_t car, int kind, float force);
    // A pit stop's sound (0x393428: the effect at the master's level, both sides): "jackdown" as the tires are on
    // (the stop's bit 0x200, taken by the pit scene), "wheelgun", "dropnut", "droptire", "jackup" by the crew's
    // motions (their channels "wheel_gun", "wheel_nut", "tire_sound2", "jack_up"), "oiltank" as the crew's fifth changes motion.
    void pit_effect(const char* name) { play_effect(name, master()); }
    // The hose's sound (0x3934A0 effect 70, both sides at the master's level): held from the step a hose of the pits
    // begins to run until none does (0x393528).
    void refueling(bool on);
    // The sound inputs of every step heard from the session (update(session, ...)) are kept for the run's replay: from
    // replay() on, the steps heard from recorded states (update(cars, ...)) take the kept ones in turn, the first
    // replayed step being the run's second. forget_recording() as a new run begins or another run is played.
    void forget_recording() { recorded_.clear(), replay_step_ = 0; }
    size_t recorded_steps() const { return recorded_.size(); }
    // A run begins (also a retry): its kind of music, and by its start (the race information's +0xB4, 0x3C0208): 1 and 2 (a
    // hold of 8 or 10 s) the kind's start jingle, -1 (rolling) the racing music at once (rolling()), any other nothing until
    // the start.
    void begin_run(RaceMusic kind, int start);
    // Ours: the racing music's next song, or the one heard before this one (-1), while the racing music plays.
    bool skip_song(int direction);
    // The race's start jingle (before the count) and finish jingle (0x3A0228; the license tests' display, 0x3BD330, plays
    // none at their finish).
    void intro();
    void finish();
    // A license test's result (0x3E1C80, 0x3E1EA0): the grade's jingle (0 gold, 1 silver, 2 bronze, anything else failed; a
    // coffee break's the kind's goal jingle), the cars and the course faded out over a second (0x391DC8).
    void license_result(int grade);
    void pause(bool paused);
    void stop();
    // The master's fade: toward `target` over `seconds`.
    void fade(float target, float seconds);
    // The master's two ducks (0x391DF0): in a replay, every step, toward 0.45 while the shot is
    // one of the course's replay script (the designer camera) and toward 1 otherwise; in a free
    // run, toward 0.2 while its recording plays (when it changes). Each over 1 s.
    void set_replay_shot(bool designer) { designer_shot_ = designer; }
    void set_free_run_duck(bool on);

    // The master level now, and what the last step did.
    float master() const { return mix_->master; }
    const CourseSound& course() const { return course_; }
    struct Report {
        int beeps = 0, starts = 0;
        std::string music; // the last stream started
        float ambience[CourseSound::kChannels] = {};
        float tunnel1 = 0, tunnel2 = 0, reverb = 0;
    };
    const Report& report() const { return report_; }

private:
    bool add_car(const std::string& label, const CarSoundFiles& files);
    struct Held {
        std::shared_ptr<const sound::InstBank> bank;
        int program = -1;
        u32 id = 0;
    };
    Held held(const char* name);
    void send(Held& h, float left, float right, float pitch);
    bool play_effect(const char* name, float level);
    bool play_stream(const char* group, int number, bool racing);
    bool play_racing();
    float stream_level(bool racing) const;
    void quiet_course();

    gt3r::Vfs& vfs_;
    gt3r::VoiceMixer& mixer_;
    SoundDriver& driver_;
    std::shared_ptr<SoundLibrary> library_;
    std::shared_ptr<EngineBanks> banks_ = std::make_shared<EngineBanks>(); // the cars' banks, each read once
    StreamPlayer music_;
    std::shared_ptr<RaceMix> mix_;
    RaceSoundOptions options_;
    RaceMusic kind_ = RaceMusic::Race;
    std::vector<std::unique_ptr<CarSound>> cars_;
    std::vector<float> contact_; // contacts waiting for the next step
    std::vector<int> contact_kind_;
    CourseSound course_;
    std::shared_ptr<const course::Runway> road_;
    std::unique_ptr<sim::RoadGround> ground_;
    Held ambience_[CourseSound::kChannels], tunnel_[4], heri_, refueling_;
    std::vector<SoundListener> more_cameras_;
    bool helicopter_ = false, refueling_on_ = false, paused_ = false, racing_ = false, started_ = false;
    float fade_ = 0, fade_target_ = 1, fade_step_ = 1.0f / 18.0f;
    int racing_piece_ = -1;
    int first_piece_ = -1; // the race's first racing piece (a replay plays it again)
    bool replay_ = false;
    bool designer_shot_ = false, free_run_duck_ = false;
    void duck();
    std::shared_ptr<RacePlaylist> playlist_;
    std::vector<int> songs_heard_; // this run's racing pieces, for skip_song()
    static constexpr size_t kRecordSteps = 60 * 60 * 10;
    std::vector<std::vector<CarSoundState>> recorded_;
    size_t replay_step_ = 0;
    s16 reverb_before_ = 0;
    Report report_;
};

} // namespace gt4r::audio
