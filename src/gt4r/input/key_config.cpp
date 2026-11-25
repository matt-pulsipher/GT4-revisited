#include "gt4r/input/key_config.h"

#include <algorithm>
#include <cstring>

namespace gt4r::input {

namespace {

constexpr std::array<std::string_view, size_t(AxisAction::Count)> kAxisActions{
    "ANALOG_STEER", "ANALOG_SHIFTDOWN", "ANALOG_SHIFTUP", "ANALOG_ACCEL", "ANALOG_BRAKE", "ANALOG_SIDEBRAKE", "ANALOG_REVERSEGEAR"};
constexpr std::array<std::string_view, size_t(ButtonAction::Count)> kButtonActions{
    "BUTTON_LSTEER",   "BUTTON_RSTEER",   "BUTTON_SHIFTDOWN", "BUTTON_SHIFTUP",   "BUTTON_ACCEL",     "BUTTON_BRAKE",
    "BUTTON_SIDEBRAKE", "BUTTON_REVERSEGEAR", "BUTTON_SPECIAL", "BUTTON_PLAYVIEW", "BUTTON_BACKVIEW", "BUTTON_FRONTVIEW",
    "BUTTON_REARVIEW", "BUTTON_LEFTVIEW", "BUTTON_RIGHTVIEW", "BUTTON_LICENCE_BEST_LINE", "BUTTON_GHOST", "BUTTON_PAUSE"};

constexpr std::string_view kPadAxes[] = {"LX", "LY", "RX", "RY", "LXF", "LXL", "LYF", "LYL", "RXF", "RXL", "RYF", "RYL"};
constexpr std::string_view kPadButtons[] = {"LEFT", "RIGHT", "UP", "DOWN", "TRIANGLE", "CIRCLE", "CROSS", "SQUARE",
                                            "L1",   "R1",    "L2", "R2",   "L3",       "R3",     "START", "SELECT"};
constexpr std::string_view kWheelAxes[] = {"STEER", "ACCEL", "BRAKE"};
constexpr std::string_view kCougarButtons[] = {"X", "Y", "A", "B", "L", "R"};
constexpr std::string_view kJaguarButtons[] = {"LEFT", "RIGHT", "UP", "DOWN", "TRIANGLE", "CIRCLE", "CROSS", "SQUARE",
                                               "L1",   "R1",    "L2", "R2",   "L",        "R",      "START", "SELECT"};
constexpr std::string_view kCheetahButtons[] = {"LEFT", "RIGHT", "UP", "DOWN", "TRIANGLE", "CIRCLE", "CROSS",   "SQUARE",  "L2",
                                                "R2",   "L3",    "R3", "L1",   "R1",       "START",  "SELECT", "SHIFTUP", "SHIFTDOWN"};
constexpr std::string_view kOn[] = {"ON"};
constexpr std::string_view kCheetahFeedback[] = {"ON", "POWER", "POWER_ASSIST"};

// The saved pieces: each a preset byte, the axes, the buttons, the feedbacks, and zeros to its length.
constexpr size_t kPieces[kDeviceCount] = {0x20, 0xC, 0x18, 0x1C};

int find(std::span<const std::string_view> names, std::string_view name) {
    const auto it = std::find(names.begin(), names.end(), name);
    return it == names.end() ? -1 : int(it - names.begin());
}

} // namespace

std::string_view axis_action_name(AxisAction a) { return s8(a) < 0 || a >= AxisAction::Count ? std::string_view() : kAxisActions[size_t(a)]; }
std::string_view button_action_name(ButtonAction a) { return s8(a) < 0 || a >= ButtonAction::Count ? std::string_view() : kButtonActions[size_t(a)]; }
AxisAction axis_action(std::string_view name) { return AxisAction(find(kAxisActions, name)); }
ButtonAction button_action(std::string_view name) { return ButtonAction(find(kButtonActions, name)); }

int device_index(std::string_view name) {
    for (std::string_view pad : {"CONTROLLER", "JOYSTICK", "SINGLESHOCK", "DUALSHOCK", "DUALSHOCK 2"})
        if (name == pad) return int(Device::DualShock2);
    return name == "COUGAR" ? int(Device::Cougar) : name == "JAGUAR" ? int(Device::Jaguar) : name == "CHEETAH" ? int(Device::Cheetah) : -1;
}

std::span<const std::string_view> axis_names(Device d) {
    if (d == Device::DualShock2) return kPadAxes;
    return kWheelAxes;
}
std::span<const std::string_view> button_names(Device d) {
    switch (d) {
    case Device::DualShock2: return kPadButtons;
    case Device::Cougar: return kCougarButtons;
    case Device::Jaguar: return kJaguarButtons;
    default: return kCheetahButtons;
    }
}
std::span<const std::string_view> feedback_names(Device d) {
    if (d == Device::Cheetah) return kCheetahFeedback;
    return kOn;
}

void set_preset(DeviceConfig& c, Device d, int preset) {
    using B = ButtonAction;
    const auto b = [](B a) { return s8(a); };
    const s8 none = -1;
    c.preset = s8(preset != 0 ? preset : 1);
    switch (d) {
    case Device::DualShock2:
        c.axes = {s8(AxisAction::Steer), none, none, none, none, none, none, none, none, none, s8(AxisAction::Accel), s8(AxisAction::Brake)};
        c.buttons = {b(B::SteerLeft), b(B::SteerRight), b(B::Ghost), none, b(B::ReverseGear), b(B::SideBrake), b(B::Accel), b(B::Brake),
                     b(B::BackView), b(B::Special), b(B::ShiftDown), b(B::ShiftUp), none, b(B::LicenseBestLine), b(B::Pause), b(B::PlayView)};
        break;
    case Device::Cougar:
        c.axes = {s8(AxisAction::Steer), s8(AxisAction::Accel), s8(AxisAction::Brake)};
        c.buttons = {b(B::BackView), b(B::Special), b(B::Pause), b(B::ReverseGear), b(B::ShiftDown), b(B::ShiftUp)};
        break;
    case Device::Jaguar:
        c.axes = {s8(AxisAction::Steer), s8(AxisAction::Accel), s8(AxisAction::Brake)};
        c.buttons = {b(B::LeftView), b(B::RightView), b(B::Ghost), b(B::RearView), b(B::ReverseGear), b(B::SideBrake), none, none,
                     b(B::BackView), b(B::Special), b(B::ShiftDown), b(B::ShiftUp), b(B::ShiftDown), b(B::ShiftUp), b(B::Pause), b(B::PlayView)};
        break;
    default:
        c.axes = {s8(AxisAction::Steer), s8(AxisAction::Accel), s8(AxisAction::Brake)};
        // (the lever's SHIFTUP and SHIFTDOWN by the preset: 1 as named, 2 the other way, 3 the side brake and reverse)
        c.buttons = {b(B::LeftView), b(B::RightView), b(B::Ghost), b(B::RearView), b(B::ReverseGear), b(B::SideBrake), none, none, b(B::BackView), b(B::Special),
                     none, b(B::LicenseBestLine), b(B::ShiftDown), b(B::ShiftUp), b(B::Pause), b(B::PlayView),
                     b(c.preset == 2 ? B::ShiftDown : c.preset == 3 ? B::SideBrake : B::ShiftUp),
                     b(c.preset == 2 ? B::ShiftUp : c.preset == 3 ? B::ReverseGear : B::ShiftDown)};
        break;
    }
}

DeviceConfig default_config(Device d) {
    DeviceConfig c;
    set_preset(c, d, 0);
    c.feedback.assign(feedback_names(d).size(), 1);
    return c;
}

void PortConfig::read(const u8* b) {
    for (size_t i = 0; i < kDeviceCount; b += kPieces[i], ++i) {
        DeviceConfig& c = device[i];
        const u8* p = b;
        c.preset = s8(*p++);
        for (std::vector<s8>* list : {&c.axes, &c.buttons, &c.feedback})
            for (s8& v : *list) v = s8(*p++);
    }
}

void PortConfig::write(u8* b) const {
    std::memset(b, 0, kSize);
    for (size_t i = 0; i < kDeviceCount; b += kPieces[i], ++i) {
        const DeviceConfig& c = device[i];
        u8* p = b;
        *p++ = u8(c.preset);
        for (const std::vector<s8>* list : {&c.axes, &c.buttons, &c.feedback})
            for (s8 v : *list) *p++ = u8(v);
    }
}

std::string port_text(const PortConfig& c) {
    u8 b[PortConfig::kSize];
    c.write(b);
    std::string out;
    for (u8 v : b)
        for (u8 d : {u8(v >> 4), u8(v & 15)}) out += char(d < 10 ? '0' + d : 'a' + d - 10);
    return out;
}

PortConfig port_from_text(std::string_view text) {
    PortConfig c;
    if (text.size() != 2 * PortConfig::kSize) return c;
    u8 b[PortConfig::kSize] = {};
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        const int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
        if (d < 0) return c;
        b[i / 2] = u8(b[i / 2] << 4 | d);
    }
    c.read(b);
    return c;
}

bool get_option(const PortConfig& c, std::string_view name) {
    const auto on = [&](Device d) { return get_feedback(c.of(d), d, "ON") != 0; };
    if (name == "VIBRATION") return on(Device::DualShock2);
    return name == "WHEEL FEEDBACK" && (on(Device::Cougar) || on(Device::Jaguar) || on(Device::Cheetah));
}

void set_option(PortConfig& c, std::string_view name, bool value) {
    if (name == "VIBRATION") set_feedback(c.of(Device::DualShock2), Device::DualShock2, "ON", value);
    else if (name == "WHEEL FEEDBACK")
        for (Device d : {Device::Cougar, Device::Jaguar, Device::Cheetah}) set_feedback(c.of(d), d, "ON", value);
}

s8 get_axis(const DeviceConfig& c, Device d, std::string_view key) {
    const int i = find(axis_names(d), key);
    return i < 0 ? s8(-1) : c.axes[size_t(i)];
}
s8 get_button(const DeviceConfig& c, Device d, std::string_view key) {
    const int i = find(button_names(d), key);
    return i < 0 ? s8(-1) : c.buttons[size_t(i)];
}
void set_axis(DeviceConfig& c, Device d, std::string_view key, AxisAction a) {
    if (const int i = find(axis_names(d), key); i >= 0) c.preset = 0, c.axes[size_t(i)] = s8(a);
}
void set_button(DeviceConfig& c, Device d, std::string_view key, ButtonAction a) {
    if (const int i = find(button_names(d), key); i >= 0) c.preset = 0, c.buttons[size_t(i)] = s8(a);
}
std::vector<std::string> keys_with(const DeviceConfig& c, Device d, AxisAction a) {
    std::vector<std::string> out;
    const auto names = axis_names(d);
    for (size_t i = 0; i < names.size(); ++i)
        if (c.axes[i] == s8(a)) out.emplace_back(names[i]);
    return out;
}
std::vector<std::string> keys_with(const DeviceConfig& c, Device d, ButtonAction a) {
    std::vector<std::string> out;
    const auto names = button_names(d);
    for (size_t i = 0; i < names.size(); ++i)
        if (c.buttons[i] == s8(a)) out.emplace_back(names[i]);
    return out;
}
int get_feedback(const DeviceConfig& c, Device d, std::string_view name) {
    const int i = find(feedback_names(d), name);
    return i < 0 ? 0 : c.feedback[size_t(i)];
}
void set_feedback(DeviceConfig& c, Device d, std::string_view name, int value) {
    if (const int i = find(feedback_names(d), name); i >= 0) c.feedback[size_t(i)] = s8(value);
}

} // namespace gt4r::input
