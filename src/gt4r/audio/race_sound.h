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
enum class RaceMusic : u8 { Race, Championship, TimeTrial, License, Coffee, Other, Last };
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
    bool add_car(const std::string& label);
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
    // The race's start jingle (before the count) and finish jingle.
    void intro();
    void finish();
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
    Held ambience_[CourseSound::kChannels], tunnel_[4], heri_;
    bool helicopter_ = false, paused_ = false, racing_ = false, started_ = false;
    float fade_ = 0, fade_target_ = 1, fade_step_ = 1.0f / 18.0f;
    int racing_piece_ = -1;
    int first_piece_ = -1; // the race's first racing piece (a replay plays it again)
    bool replay_ = false;
    bool designer_shot_ = false, free_run_duck_ = false;
    void duck();
    std::shared_ptr<RacePlaylist> playlist_;
    s16 reverb_before_ = 0;
    Report report_;
};

} // namespace gt4r::audio
