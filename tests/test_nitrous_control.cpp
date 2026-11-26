// Nitrous as the engine's extra control (input/player_controls.h kNitrousControl): held on a player's pads, it presses the game's
// own nitrous: directly on the modern layout, through the key the button configuration gives Special on the console's.
// Built against an engine without extra controls, it is skipped (and nothing presses the nitrous that way).

#include "test_framework.h"

#include "gt4r/input/player_controls.h"

#include <SDL3/SDL.h>

using namespace gt4r;
using namespace gt4r::input;

TEST(input_nitrous_control) {
#if !GT4R_EXTRA_CONTROLS
    SKIP("the engine has no extra controls");
#else
    register_extra_controls();
    CHECK(gt3r::extra_control_name(kNitrousControl) == "Nitrous");
    gt3r::add_extra_control_default(kNitrousControl, "keyboard", true, "key.B"); // (a key to press here)
    gt3r::InputManager in;
    in.set_device_filter(0, gt3r::InputManager::kKeyboardOnly);
    const auto press_b = [&in] {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.scancode = SDL_SCANCODE_B;
        in.handle_event(e);
        in.update(1.0 / 60);
    };
    in.update(1.0 / 60);
    PortConfig c;
    CHECK(!nitrous_held(in, 0) && read_player(in, 0, c, WheelSettings{}).frame.nitrous == 0);
    press_b();
    CHECK(nitrous_held(in, 0) && read_player(in, 0, c, WheelSettings{}).frame.nitrous == 1);
    // The console's layout (its keys changed: the pad is read through them): the key that has Special, R1 as it comes.
    set_button(c.of(Device::DualShock2), Device::DualShock2, "CROSS", ButtonAction::ShiftUp);
    CHECK(console_layout(in, 0, c));
    press_b();
    CHECK(read_player(in, 0, c, WheelSettings{}).frame.nitrous == 1);
    set_button(c.of(Device::DualShock2), Device::DualShock2, "R1", ButtonAction::None); // (no key has it: none)
    press_b();
    CHECK(read_player(in, 0, c, WheelSettings{}).frame.nitrous == 0);
    gt3r::clear_extra_controls();
    register_extra_controls();
#endif
}
