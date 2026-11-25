#pragma once

// A player's devices as the race reads them: the shared engine's input layer (SDL's gamepads, wheels and the keyboard on a
// virtual DualShock 2) turned into what Gran Turismo 4's controller makes of a game port (race_input.h).
//
// Two layouts, as the player chooses ([player1] race in config.ini):
// - "modern" (the default): triggers accelerate and brake, the buttons as the engine's modern race layout has them. Once the
//   player has changed the game's own Key Configuration page for the DualShock 2 by hand (its preset is 0), the pad is read
//   through that page instead.
// - "original": the pad as the console's DualShock 2, always through the Key Configuration page.
// A steering wheel is the console's Driving Force Pro (or a GT FORCE), through its own Key Configuration page either way.
// What differs from the console, here: the sticks' fineness and dead zone, the triggers' travel, a wheel's rotation.

#include "gt4r/input/race_input.h"
#include "input/input_manager.h"

namespace gt3r {
class IniDocument;
}

namespace gt4r::input {

// [wheel] in config.ini: which of the console's wheels a steering wheel is (3 the Driving Force Pro, whose 900 degrees turn
// the road wheels directly; 2 the Driving Force, whose 200 degrees ask for a lock that depends on the speed), and how far it
// turns lock to lock, in degrees, as set in its own driver.
struct WheelSettings {
    u8 wheel = 3;
    float rotation = 900;
    static WheelSettings load(const gt3r::IniDocument& ini);
    void save(gt3r::IniDocument& ini) const;
};

// Whether the player's pad is read as the console's (above).
bool console_layout(const gt3r::InputManager& input, int player, const PortConfig& config);

// The controller's answer for a player this frame.
RaceInput read_player(const gt3r::InputManager& input, int player, const PortConfig& config, const WheelSettings& wheel, Mode mode = Mode::Drive);

// Its parts, on the engine's pads: `menu` the console's buttons, `race` the race layout's (gt3r::race_action_name).
GamePort pad_port(const gt3r::PadState& menu, const gt3r::PadState& race);
GamePort wheel_port(const gt3r::PadState& menu, const gt3r::PadState& race, const WheelSettings& wheel);
RaceInput modern_input(const gt3r::PadState& menu, const gt3r::PadState& race);

// The config's own words: [player1] race = original / modern, rumble = auto / modern / original, [ffb] wheel_mode =
// auto / modern / original. The engine's "gt3" in each is its word for the original's.
std::string engine_word(std::string_view word);
std::string our_word(std::string_view engine);
// Those words of a config as the engine reads them; call before InputManager::load.
void translate_config(gt3r::IniDocument& ini);

} // namespace gt4r::input
