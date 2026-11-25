#pragma once

// The options as the rest of the game reads them: GameState::options (MOption's attributes by
// name, as the option pages' scripts set them and the save keeps them) turned into plain values
// for the parts that act on them (the menus' sound, the race's picture, sound, display and
// controls). docs/formats/menu.md "The options and who reads them" lists, for each, the part
// that reads it and the call that part makes.

#include "gt4r/input/key_config.h"
#include "gt4r/menu/game.h"

#include <array>
#include <map>
#include <string>

namespace gt4r::menu {

struct GameSettings {
    // Sound: each pair's gains 0..1 (the raw volume 0..128 over 128, 0 when switched off).
    struct Volumes {
        float music = 115.0f / 128.0f, effects = 115.0f / 128.0f;
    };
    Volumes menu, race, replay;
    float slide_music = 115.0f / 128.0f;
    std::string sound_type = "STEREO"; // MONOPHONIC, STEREO, PROLOGIC2
    bool narration = true;

    // Picture: the four screen settings -5 .. +5 (brightness, contrast, saturation, color
    // balance) of the racing views and of the replays (race/picture_settings.h); the screen's
    // place (screen_adjust_h / _v), 16:9, the menus' sharpness.
    std::array<int, 4> race_picture{3, 0, 0, 0}, replay_picture{};
    int screen_adjust_h = 0, screen_adjust_v = 0;
    bool wide = false;
    std::string sharpness = "MILD";

    // Units of the menus and the race display.
    GameState::Unit velocity{1.0f, "km/h"}, power{1.0f, "ps"}, torque{1.0f, "kgf.m"};

    // Driving: the defaults the race menus offer (Arcade's car page takes them as its starting
    // choice) and the player's controls.
    bool automatic = true;                   // automatic_gear
    bool asm_on = true, tcs_on = true;       // assist_asm, assist_tcs
    bool license_manual = false;             // license_transmission_MT: license tests in manual
    std::string steering_assist = "AMATEUR"; // steering_assist_1p
    bool active_steering = false;            // active_steering_1p
    // The race entry's +0x344C: the steering wheel's lock (F +0x547): 0 AMATEUR, 1 PROFESSIONAL, 2 SIMULATION.
    u8 steering_assist_kind() const { return steering_assist == "PROFESSIONAL" ? 1 : steering_assist == "SIMULATION" ? 2 : 0; }
    input::Controllers controllers;          // controllers_1p / _2p: the Key Configuration, the vibration, the wheel's forces
    int laps = 2;                            // race_laps (Arcade)
    int tire_damage = 0;                     // tire_damage (Arcade): 0 .. 2
    std::string penalty = "NONE";            // penalty_type: NONE, SPEED_LIMITATION, PASSING_PITLANE
    std::string view = "SUBJECTIVE";         // view_type: the first view of a race (SUBJECTIVE, BONNET, OVERLOOK)

    // The race display's parts (race_display_*; bits of the option's word +0x1140) and whether a
    // replay shows the display at all.
    struct Display {
        bool map = true, steering = false, meter = true, time = true, lap = true, position = true, car_name = true, suggested_gear = true,
             g_meter = true;
    } display;
    bool replay_display = false;
    std::string replay_mode = "NORMAL"; // NORMAL, DIVE (the replay's cameras)
    bool replay_shuffle = false;        // replay_play_mode SHUFFLE

    // The menus' timing: the title's demo after this many seconds without a key (0: never; the
    // title's script reads it), the opening movie after a game is loaded, saving on the way back.
    int demo_after_s = 20, top_after_s = 600, demo_movie_interval = 3;
    bool opening_after_autoload = true, auto_save = true;
};

GameSettings settings_from(const std::map<std::string, OptionValue>& options);
inline GameSettings settings_of(const GameState& g) { return settings_from(g.options); }

// The screen settings' text in the options ("3 0 0 0") and back.
std::array<int, 4> picture_from_text(const std::string& text, std::array<int, 4> fallback = {});
std::string picture_text(const std::array<int, 4>& v);

} // namespace gt4r::menu
