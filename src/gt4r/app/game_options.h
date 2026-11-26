#pragma once

// The game's own controller options as our Controls page shows them, in place of the Options' pages that hide them (the button
// configurations, Vibration, Steering): the values the game's pages set, in GameState::options (menu/game_settings.h,
// input/key_config.h), so races, the game's rules and its saves read them as before.
//
// Two places hold them: the game running under the page (its state, saved as the game saves: the change marks it to be saved,
// as the Options page's OK does), or, from the home screen, the saved game being played (the save library's current one),
// written back when the page closes.

#include "gt4r/input/key_config.h"
#include "gt4r/menu/game.h"

#include <memory>
#include <optional>
#include <string>

namespace gt4r {

class App;

class GameOptions {
public:
    virtual ~GameOptions() = default;
    // The options; null when there is no game (`why()` says so in plain words).
    virtual menu::GameState* state() = 0;
    virtual std::string why() const { return {}; }
    // After a change.
    virtual void changed() = 0;
    // The page is closing: a saved game's are written (an error for the player, "" when done or nothing changed).
    virtual std::string close() { return {}; }

    // ---- The values, per player (0 the first, 1 the second), as the game's pages read and write them.
    input::PortConfig port(int player);
    void set_port(int player, const input::PortConfig& c);
    // Vibration (the DualShock 2's "ON": `vibration_1p`), the wheels' forces ("WHEEL FEEDBACK": `steering_forcefeedback_1p`).
    bool vibration(int player) { return input::get_option(port(player), "VIBRATION"); }
    void set_vibration(int player, bool on);
    bool wheel_feedback(int player) { return input::get_option(port(player), "WHEEL FEEDBACK"); }
    void set_wheel_feedback(int player, bool on);
    // Steering (the Steering page): active steering (`active_steering_1p`), the steering type of the GT FORCE and the Driving
    // Force (`steering_assist_1p`: AMATEUR, PROFESSIONAL, SIMULATION), the Driving Force Pro's power assist and its force's
    // strength (the Cheetah's POWER_ASSIST 0 / 1 and POWER: 0 weak, 1 strong, as the page offers them).
    bool active_steering(int player);
    void set_active_steering(int player, bool on);
    std::string steering_type(int player);
    void set_steering_type(int player, const std::string& type);
    bool power_assist(int player);
    void set_power_assist(int player, bool on);
    int force_strength(int player);
    void set_force_strength(int player, int v);
    // The button configurations: every device's keys as its first preset has them (the options' reset, 0x4363A0); the vibration
    // and the forces stay.
    void restore_keys(int player);
    bool keys_changed(int player);
};

// The running game's.
std::unique_ptr<GameOptions> running_game_options(menu::GameState& game);
// The saved game being played (none without a disc with a game database, or a game saved).
std::unique_ptr<GameOptions> saved_game_options(App& app);

} // namespace gt4r
