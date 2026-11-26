#include "gt4r/input/player_controls.h"

#include "base/config.h"

#include <algorithm>
#include <cstdlib>

namespace gt4r::input {

using gt3r::PadAxis;
using gt3r::PadButton;
using gt3r::PadState;

WheelSettings WheelSettings::load(const gt3r::IniDocument& ini) {
    WheelSettings w;
    if (const auto v = ini.get("wheel", "kind")) w.wheel = *v == "driving_force" || *v == "gt_force" ? 2 : 3;
    if (const auto v = ini.get("wheel", "rotation"))
        if (const float r = float(std::atof(v->c_str())); r > 0) w.rotation = std::clamp(r, 90.0f, 1440.0f);
    return w;
}

void WheelSettings::save(gt3r::IniDocument& ini) const {
    ini.set("wheel", "kind", wheel == 2 ? "driving_force" : "driving_force_pro");
    ini.set("wheel", "rotation", std::to_string(int(std::lround(rotation))));
}

std::string engine_word(std::string_view word) { return word == "original" ? "gt3" : std::string(word); }
std::string our_word(std::string_view engine) { return engine == "gt3" ? "original" : std::string(engine); }

void translate_config(gt3r::IniDocument& ini) {
    constexpr std::pair<const char*, const char*> kWords[] = {{"player1", "race"}, {"player1", "rumble"}, {"player2", "race"}, {"player2", "rumble"}, {"ffb", "wheel_mode"}};
    for (const auto& [section, key] : kWords)
        if (const auto v = ini.get(section, key); v && *v == "original") ini.set(section, key, engine_word(*v));
}

void save_config(const gt3r::InputManager& input, gt3r::IniDocument& ini) {
    input.save(ini);
    constexpr std::pair<const char*, const char*> kWords[] = {{"player1", "race"}, {"player1", "rumble"}, {"player2", "race"}, {"player2", "rumble"}, {"ffb", "wheel_mode"}};
    for (const auto& [section, key] : kWords)
        if (const auto v = ini.get(section, key)) ini.set(section, key, our_word(*v));
}

void register_extra_controls() {
#if GT4R_EXTRA_CONTROLS
    gt3r::clear_extra_controls();
    gt3r::name_extra_control(kNitrousControl, "Nitrous");
    gt3r::add_extra_control_default(kNitrousControl, "wheel", true, "joy.button6");
#endif
}

bool nitrous_held(const gt3r::InputManager& input, int player) {
#if GT4R_EXTRA_CONTROLS
    return input.race_pad(player).extra_down(kNitrousControl) || input.pad(player).extra_down(kNitrousControl);
#else
    (void)input, (void)player;
    return false;
#endif
}

namespace {

// The keys the player's button configuration of a device gives an action, pressed on the port.
void press_keys(GamePort& port, const PortConfig& config, Device d, ButtonAction action) {
    const std::span<const std::string_view> names = button_names(d);
    for (const std::string& key : keys_with(config.of(d), d, action))
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == key)
                if (const int bit = button_bit(d, i); bit > 0) port.set(bit);
}

} // namespace

bool console_layout(const gt3r::InputManager& input, int player, const PortConfig& config) {
    return input.race_uses_menu_pad(player) || keys_changed(config.of(Device::DualShock2), Device::DualShock2);
}

GamePort pad_port(const PadState& menu, const PadState& race) {
    GamePort port;
    port.pad = GamePort::Pad::DualShock; // (a pad without a DualShock 2's pressures: its buttons are pressed or not)
    port.buttons = u64(menu.buttons) << kPadSelect;
    for (PadAxis a : {PadAxis::LX, PadAxis::LY, PadAxis::RX, PadAxis::RY}) {
        const size_t at = a == PadAxis::RX ? 0 : a == PadAxis::RY ? 1 : a == PadAxis::LX ? 2 : 3;
        const PadState& from = a == PadAxis::LX ? race : menu; // (the steering's dead zone and curve are the race's)
        port.axis[at] = from.axis(a);
        port.fine[at] = from.axis_fine(a);
    }
    port.make_halves();
    if (menu.down(PadButton::L2)) port.trigger[0] = float(menu.press(PadButton::L2)) / 255.0f;
    if (menu.down(PadButton::R2)) port.trigger[1] = float(menu.press(PadButton::R2)) / 255.0f;
    return port;
}

GamePort wheel_port(const PadState& menu, const PadState& race, const WheelSettings& wheel) {
    GamePort port;
    port.wheel = wheel.wheel;
    port.axis[24] = wheel_axis(wheel.wheel, race.axis_fine(PadAxis::LX), wheel.rotation);
    port.axis[25] = race.down(PadButton::Cross) ? race.press(PadButton::Cross) : 0;
    port.axis[26] = race.down(PadButton::Square) ? race.press(PadButton::Square) : 0;
    // (Cross and Square are the pedals; the two paddles are the wheel layout's L1 and R1)
    constexpr std::pair<PadButton, int> kButtons[] = {
        {PadButton::Left, kWheelLeft}, {PadButton::Right, kWheelRight}, {PadButton::Up, kWheelUp}, {PadButton::Down, kWheelDown}, {PadButton::Triangle, kWheelTriangle},
        {PadButton::Circle, kWheelCircle}, {PadButton::L1, kWheelL}, {PadButton::R1, kWheelR}, {PadButton::L3, kWheelL3}, {PadButton::R3, kWheelR3}, {PadButton::Start, kWheelStart}};
    for (const auto& [button, bit] : kButtons)
        if (race.down(button)) port.set(bit);
    // (a lever: the Driving Force Pro's, or the Driving Force's lower paddles)
    if (race.down(PadButton::R2)) port.set(wheel.wheel == 3 ? kWheelShiftUp : kWheelR2);
    if (race.down(PadButton::L2)) port.set(wheel.wheel == 3 ? kWheelShiftDown : kWheelL2);
    if (menu.down(PadButton::Select)) port.set(kWheelSelect);
    return port;
}

RaceInput modern_input(const PadState& menu, const PadState& race) {
    RaceInput in;
    const auto axis = [](u8 pressure) { return s32(pressure) * 32767 / 255; };
    if (race.down(PadButton::Left)) press(in, ButtonAction::SteerLeft);
    if (race.down(PadButton::Right)) press(in, ButtonAction::SteerRight);
    if (in.frame.steer == 0) move(in, 2, AxisAction::Steer, s32(std::lround(std::clamp(race.axis_fine(PadAxis::LX), -1.0f, 1.0f) * 32767.0f)));
    if (race.down(PadButton::Cross)) move(in, 2, AxisAction::Accel, axis(race.press(PadButton::Cross)));
    if (race.down(PadButton::Square)) move(in, 2, AxisAction::Brake, axis(race.press(PadButton::Square)));
    if (race.down(PadButton::Circle)) press(in, ButtonAction::SideBrake);
    if (race.down(PadButton::Triangle)) {
        if (const u8 p = race.press(PadButton::Triangle); p < 255) move(in, 2, AxisAction::ReverseGear, axis(p));
        else press(in, ButtonAction::ReverseGear);
    }
    if (race.down(PadButton::R2)) press(in, ButtonAction::ShiftUp);
    if (race.down(PadButton::L2)) press(in, ButtonAction::ShiftDown);
    if (race.down(PadButton::R1)) press(in, ButtonAction::PlayView);
    if (race.down(PadButton::L1)) press(in, ButtonAction::BackView);
    if (race.down(PadButton::Up)) press(in, ButtonAction::Ghost);
    // The nitrous: the console's R1 (a keyboard's key for it shifts up in races), the direction buttons' Down, the left stick pressed.
    const bool r1 = menu.down(PadButton::R1) && !race.down(PadButton::R2) && !race.down(PadButton::R1);
    if (r1 || race.down(PadButton::Down) || menu.down(PadButton::L3)) press(in, ButtonAction::Special);
    if (menu.down(PadButton::Select)) press(in, ButtonAction::PlayView);
    if (race.down(PadButton::Start)) press(in, ButtonAction::Pause);
    in.stick = {race.axis_fine(PadAxis::LX), -race.axis_fine(PadAxis::LY), race.axis_fine(PadAxis::RX), -race.axis_fine(PadAxis::RY)};
    return in;
}

RaceInput read_player(const gt3r::InputManager& input, int player, const PortConfig& config, const WheelSettings& wheel, Mode mode) {
    const PadState &menu = input.pad(player), &race = input.race_pad(player);
    const bool nitrous = nitrous_held(input, player);
    if (input.wheel_steering(player)) {
        GamePort port = wheel_port(menu, race, wheel);
        if (nitrous) press_keys(port, config, wheel.wheel == 2 ? Device::Jaguar : Device::Cheetah, ButtonAction::Special);
        return read_port(port, config, mode);
    }
    if (console_layout(input, player, config)) {
        GamePort port = pad_port(menu, race);
        if (nitrous) press_keys(port, config, Device::DualShock2, ButtonAction::Special);
        return read_port(port, config, mode);
    }
    if (mode == Mode::Watch) return {};
    RaceInput in = modern_input(menu, race);
    if (nitrous) press(in, ButtonAction::Special);
    if (mode == Mode::DriveNoDirections && (in.frame.flags & 1) == 0) in.frame.steer = 0;
    return in;
}

} // namespace gt4r::input
