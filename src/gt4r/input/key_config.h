#pragma once

// The players' controller settings as Gran Turismo 4 keeps them (Option +0x11D0, OptionRaceControllerPS2): for each of
// the two ports, four kinds of device, each with what its axes and buttons do, the preset they were set from and whether
// (and how) it answers with forces. The option pages' scripts read and write them through MOption (menu/game_controllers.cpp),
// the saved game holds them (docs/formats/save.md), and the race's input is made through them (race_input.h).
// docs/specs/input.md.

#include "base/bytes.h"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gt4r::input {

using gt3r::s8;
using gt3r::u8;

// What an axis does (the program's table 0x622E08: their names).
enum class AxisAction : s8 { None = -1, Steer, ShiftDown, ShiftUp, Accel, Brake, SideBrake, ReverseGear, Count };
// What a button does (0x622E28).
enum class ButtonAction : s8 {
    None = -1, SteerLeft, SteerRight, ShiftDown, ShiftUp, Accel, Brake, SideBrake, ReverseGear, Special, PlayView, BackView,
    FrontView, RearView, LeftView, RightView, LicenseBestLine, Ghost, Pause, Count
};
// Their names ("ANALOG_STEER", "BUTTON_ACCEL"); "" for None. The other way: None for an unknown name.
std::string_view axis_action_name(AxisAction a);
std::string_view button_action_name(ButtonAction a);
AxisAction axis_action(std::string_view name);
ButtonAction button_action(std::string_view name);

// The kinds of device (the port's virtual +0x18, 0x43CDC0): the pads, and the steering wheels by the program's own names:
// COUGAR (USB 046D:C293, GT FORCE), JAGUAR (046D:C294, Driving Force), CHEETAH (046D:C298, Driving Force Pro).
enum class Device : u8 { DualShock2, Cougar, Jaguar, Cheetah, Count };
constexpr size_t kDeviceCount = size_t(Device::Count);
// By the name a script gives (0x43CE08: the pads' five names are one device); -1 unknown.
int device_index(std::string_view name);

// The axes', buttons' and feedbacks' names of a device, in the order its settings keep them.
std::span<const std::string_view> axis_names(Device d);
std::span<const std::string_view> button_names(Device d);
std::span<const std::string_view> feedback_names(Device d);

// One device's settings.
struct DeviceConfig {
    s8 preset = 1;            // 0: changed by hand
    std::vector<s8> axes;     // AxisAction per axis
    std::vector<s8> buttons;  // ButtonAction per button
    std::vector<s8> feedback; // per feedback: "ON" (vibration, the wheel's forces) 0 / 1; the Cheetah's "POWER" 0 .. 2 and
                              // "POWER_ASSIST" 0 / 1
    bool operator==(const DeviceConfig&) const = default;
};
// A device's settings as a new game has them; a preset's axes and buttons (0x43B808, 0x43B8D8, 0x43B928, 0x43B9D8: 0 as 1).
DeviceConfig default_config(Device d);
void set_preset(DeviceConfig& c, Device d, int preset);
// A device's keys differ from its first preset's (the Key Configuration page's OK leaves the preset 0 without a change too).
bool keys_changed(const DeviceConfig& c, Device d);

// A port's settings, and their saved form (0x43D8A8: four pieces of 0x20, 0xC, 0x18 and 0x1C bytes).
struct PortConfig {
    std::array<DeviceConfig, kDeviceCount> device{default_config(Device::DualShock2), default_config(Device::Cougar), default_config(Device::Jaguar),
                                                  default_config(Device::Cheetah)};
    static constexpr size_t kSize = 0x60;
    void read(const u8* b);
    void write(u8* b) const;
    DeviceConfig& of(Device d) { return device[size_t(d)]; }
    const DeviceConfig& of(Device d) const { return device[size_t(d)]; }
    bool operator==(const PortConfig&) const = default;
};
// As the options keep it (GameState::options "controllers_1p" / "controllers_2p"): the saved form's bytes in hexadecimal;
// a new game's settings for anything else.
std::string port_text(const PortConfig& c);
PortConfig port_from_text(std::string_view text);
struct Controllers {
    std::array<PortConfig, 2> port;
    bool operator==(const Controllers&) const = default;
};

// The scripts' calls (MOption's, 0x190A00 ..) on a device's settings. An axis and a button are named as axis_names /
// button_names have them; an unknown name changes nothing and reads as None. Setting one by name makes the preset 0.
s8 get_axis(const DeviceConfig& c, Device d, std::string_view key);
s8 get_button(const DeviceConfig& c, Device d, std::string_view key);
void set_axis(DeviceConfig& c, Device d, std::string_view key, AxisAction a);
void set_button(DeviceConfig& c, Device d, std::string_view key, ButtonAction a);
// Every key with that action, by name in the settings' order.
std::vector<std::string> keys_with(const DeviceConfig& c, Device d, AxisAction a);
std::vector<std::string> keys_with(const DeviceConfig& c, Device d, ButtonAction a);
// The port's options by name (0x43D6C0 / 0x43D780): "VIBRATION" the pad's, "WHEEL FEEDBACK" any wheel's, set on all three.
bool get_option(const PortConfig& c, std::string_view name);
void set_option(PortConfig& c, std::string_view name, bool on);
int get_feedback(const DeviceConfig& c, Device d, std::string_view name);
void set_feedback(DeviceConfig& c, Device d, std::string_view name, int value);

} // namespace gt4r::input
