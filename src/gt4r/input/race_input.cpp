#include "gt4r/input/race_input.h"

#include <algorithm>
#include <cmath>

namespace gt4r::input {

namespace {

// The program's own orders (0x6A64C0, 0x6A64D0, 0x6A64E0): the settings' axes, buttons and pressures in the port's.
constexpr u8 kPadAxis[12] = {2, 3, 0, 1, 20, 21, 22, 23, 16, 17, 18, 19};
constexpr u8 kPadButton[16] = {kPadLeft, kPadRight, kPadUp, kPadDown, kPadTriangle, kPadCircle, kPadCross, kPadSquare,
                               kPadL1,   kPadR1,    kPadL2, kPadR2,   kPadL3,       kPadR3,     kPadStart, kPadSelect};
constexpr u8 kPressure[12] = {5, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
constexpr u8 kCougarButton[6] = {42, 43, 40, 41, kWheelL, kWheelR};
constexpr u8 kJaguarButton[16] = {kWheelLeft, kWheelRight, kWheelUp, kWheelDown, kWheelTriangle, kWheelCircle, kWheelCross, kWheelSquare,
                                  kWheelL1,   kWheelR1,    kWheelL2, kWheelR2,   kWheelL,        kWheelR,      kWheelStart, kWheelSelect};
constexpr u8 kCheetahButton[18] = {kWheelLeft, kWheelRight, kWheelUp, kWheelDown, kWheelTriangle, kWheelCircle, kWheelCross, kWheelSquare, kWheelL2,
                                   kWheelR2,   kWheelL3,    kWheelR3, kWheelL,    kWheelR,        kWheelStart,  kWheelSelect, kWheelShiftUp, kWheelShiftDown};

// 0x4386C8: a travel value as the frame's 0 .. 4096.
s32 pedal(s32 v) { return v < 0 ? 0 : std::min((v + 4) >> 3, 0x1000); }

// A pressure doubled and scaled to full at `share` percent of its range (0x43BEE8 ..): the frame's 0 .. 4096.
u16 pressed(int byte, int share) {
    const s32 p = std::min((byte & 0xFF) << 1, 0x100);
    return u16(std::min(p * 100 / share, 0x100) << 4);
}

void dual_shock(RaceInput& in, const GamePort& port, const DeviceConfig& c, Mode mode) {
    sim::rules::PadFrame& f = in.frame;
    std::array<s8, 16> buttons{};
    std::copy_n(c.buttons.begin(), 16, buttons.begin());
    if (mode == Mode::DriveNoDirections) buttons[0] = buttons[1] = buttons[2] = buttons[3] = -1;
    // Left or right held: up and down are not read.
    const bool sideways = port.button(kPadLeft) || port.button(kPadRight);
    for (size_t i = 0; i < 16; ++i)
        if (port.button(kPadButton[i]) && !(sideways && (kPadButton[i] == kPadUp || kPadButton[i] == kPadDown))) press(in, ButtonAction(buttons[i]));
    const bool pressures = port.pad == GamePort::Pad::DualShock2;
    if (pressures) { // the direction buttons steer by their pressures (whatever they are set to)
        const s32 left = pressed(port.axis[5], 60) >> 4, right = pressed(port.axis[4], 60) >> 4;
        if (left == 0 && right == 0) f.kind = 0, f.flags &= u8(~1), f.steer = 0;
        else f.kind = 3, f.flags |= 1, f.steer = s16((left - right) << 4);
    }
    // The sticks and their halves (a pad without sticks reports them centered).
    for (size_t i = 0; i < 12; ++i) {
        const u8 at = kPadAxis[i];
        const AxisAction a = AxisAction(c.axes[i]);
        s32 v;
        if (a == AxisAction::Steer) {
            if (f.steer != 0) continue;
            v = at < 4 && !std::isnan(port.fine[at]) ? s32(std::lround(std::clamp(port.fine[at], -1.0f, 1.0f) * 32767.0f)) : signed_value(port.axis[at]);
        } else {
            v = travel_value(port.axis[at]);
        }
        move(in, 2, a, v);
    }
    if (pressures) {
        for (size_t i = 0; i < 12; ++i) {
            const s16 p = port.axis[kPressure[i]];
            if (p == 0) continue;
            switch (ButtonAction(buttons[i])) {
            case ButtonAction::Accel: f.flags |= 2, f.throttle = pressed(p, 35); break;
            case ButtonAction::Brake: f.flags |= 4, f.brake = pressed(p, 65); break;
            case ButtonAction::SideBrake: f.flags |= 0x10, f.third = pressed(p, 20); break;
            case ButtonAction::ReverseGear: f.flags |= 8, f.back = pressed(p, 35); break;
            default: break;
            }
        }
    }
    for (size_t t = 0; t < 2; ++t) { // (ours: the triggers' travel)
        const float v = port.trigger[t];
        if (std::isnan(v) || !port.button(t == 0 ? kPadL2 : kPadR2)) continue;
        const s32 value = s32(std::lround(std::clamp(v, 0.0f, 1.0f) * 32767.0f));
        switch (ButtonAction(buttons[10 + t])) {
        case ButtonAction::Accel: move(in, 2, AxisAction::Accel, value); break;
        case ButtonAction::Brake: move(in, 2, AxisAction::Brake, value); break;
        case ButtonAction::SideBrake: move(in, 2, AxisAction::SideBrake, value); break;
        case ButtonAction::ReverseGear: move(in, 2, AxisAction::ReverseGear, value); break;
        default: break;
        }
    }
}

// 0x43C260 with the device's own button list (0x43C4D0, 0x43C768, 0x43CA78).
void wheel(RaceInput& in, const GamePort& port, const DeviceConfig& c, Device d, u8 kind, Mode mode) {
    for (size_t i = 0; i < c.buttons.size(); ++i) {
        // (the Cougar has no direction buttons and takes no notice)
        if (mode == Mode::DriveNoDirections && d != Device::Cougar && i < 4) continue;
        if (port.button(button_bit(d, i))) press(in, ButtonAction(c.buttons[i]));
    }
    for (size_t i = 0; i < 3; ++i) {
        const s16 v = port.axis[24 + i];
        move(in, kind, AxisAction(c.axes[i]), i == 0 ? -s32(v) * 4 : travel_value(v));
    }
}

} // namespace

s32 signed_value(int byte) {
    byte &= 0xFF;
    return byte < 128 ? (byte - 128) * 256 : byte == 255 ? 32767 : (byte - 128) * 258;
}

s32 travel_value(int byte) { return ((byte & 0xFF) * 32767 + 127) / 255; }

void GamePort::make_halves() {
    for (size_t i = 0; i < 4; ++i) {
        axis[16 + 2 * i] = s16(std::max(127 - axis[i], 0));
        axis[17 + 2 * i] = s16(std::max(axis[i] - 128, 0));
    }
}

int button_bit(Device d, size_t i) {
    switch (d) {
    case Device::DualShock2: return i < 16 ? kPadButton[i] : 0;
    case Device::Cougar: return i < 6 ? kCougarButton[i] : 0;
    case Device::Jaguar: return i < 16 ? kJaguarButton[i] : 0;
    default: return i < 18 ? kCheetahButton[i] : 0;
    }
}

void press(RaceInput& in, ButtonAction action) {
    sim::rules::PadFrame& f = in.frame;
    switch (action) {
    case ButtonAction::SteerLeft:
    case ButtonAction::SteerRight: f.flags &= u8(~1), f.kind = 0, f.steer = s16(f.steer + (action == ButtonAction::SteerLeft ? 2 : -2)); break;
    case ButtonAction::ShiftDown: f.shift = s8(f.shift - 1); break;
    case ButtonAction::ShiftUp: f.shift = s8(f.shift + 1); break;
    case ButtonAction::Accel: f.throttle = 1, f.flags &= u8(~2); break;
    case ButtonAction::Brake: f.brake = 1, f.flags &= u8(~4); break;
    case ButtonAction::SideBrake: f.third = 1, f.flags &= u8(~0x10); break;
    case ButtonAction::ReverseGear: f.back = 1, f.flags &= u8(~8); break;
    case ButtonAction::Special: f.nitrous = 1; break;
    case ButtonAction::PlayView: in.views |= RaceInput::kPlayView; break;
    case ButtonAction::BackView: in.views |= RaceInput::kBackView; break;
    case ButtonAction::FrontView: in.views |= RaceInput::kFrontView; break;
    case ButtonAction::RearView: in.views |= RaceInput::kRearView; break;
    case ButtonAction::LeftView: in.views |= RaceInput::kLeftView; break;
    case ButtonAction::RightView: in.views |= RaceInput::kRightView; break;
    case ButtonAction::LicenseBestLine: in.special |= RaceInput::kBestLine; break;
    case ButtonAction::Ghost: in.special |= RaceInput::kGhost; break;
    case ButtonAction::Pause: in.menu |= RaceInput::kPause; break;
    default: break;
    }
}

void move(RaceInput& in, u8 kind, AxisAction action, s32 value) {
    sim::rules::PadFrame& f = in.frame;
    switch (action) {
    case AxisAction::Steer: {
        s32 v = (value < 0 ? value - 4 : value + 4) >> 3;
        v = std::clamp(v, -0x1000, 0x1000);
        if (kind == 2) { // a stick: right is the axis' positive side, and its center leaves the frame as it is
            if (v == 0) return;
            v = -v;
        }
        f.steer = s16(v), f.kind = kind, f.flags |= 1;
        break;
    }
    case AxisAction::ShiftDown:
        if (value >= 0x4000) f.shift = s8(f.shift - 1);
        break;
    case AxisAction::ShiftUp:
        if (value >= 0x4000) f.shift = s8(f.shift + 1);
        break;
    case AxisAction::Accel:
        if (const s32 v = pedal(value)) f.throttle = u16(v), f.flags |= 2;
        break;
    case AxisAction::Brake:
        if (const s32 v = pedal(value)) f.brake = u16(v), f.flags |= 4;
        break;
    case AxisAction::SideBrake:
        if (const s32 v = pedal(value)) f.third = u16(v), f.flags |= 0x10;
        break;
    case AxisAction::ReverseGear:
        if (const s32 v = pedal(value)) f.back = u16(v), f.flags |= 8;
        break;
    default: break;
    }
}

RaceInput read_port(const GamePort& port, const PortConfig& config, Mode mode) {
    RaceInput in;
    in.menu = u32(port.buttons & 0xFFFF);
    const auto stick = [&](int i) { // 0x43BAC8
        const int v = port.axis[size_t(i)];
        return float(v - (v < 128 ? 127 : 128)) / 127.0f;
    };
    in.stick = {stick(2), -stick(3), stick(0), -stick(1)};
    if (mode == Mode::Watch) return in;
    if (port.pad != GamePort::Pad::None) dual_shock(in, port, config.of(Device::DualShock2), mode);
    switch (port.wheel) {
    case 1: wheel(in, port, config.of(Device::Cougar), Device::Cougar, 4, mode); break;
    case 2: wheel(in, port, config.of(Device::Jaguar), Device::Jaguar, 4, mode); break;
    case 3: wheel(in, port, config.of(Device::Cheetah), Device::Cheetah, 6, mode); break;
    default: break;
    }
    return in;
}

s16 wheel_axis(u8 wheel, float axis, float rotation) {
    const float full = wheel == 3 ? 900.0f : 200.0f;
    return s16(std::clamp(std::lround(axis * rotation / full * 8192.0f), -8192L, 8191L));
}

} // namespace gt4r::input
