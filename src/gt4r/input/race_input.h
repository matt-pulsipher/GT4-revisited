#pragma once

// The race's input as Gran Turismo 4's controller makes it (RaceInput::vf2 0x426F00 → OptionRaceInputPortPS2::vf6
// 0x43CF90): what a game port reports (the pad's sticks, buttons and pressures, a steering wheel's axes and buttons) turned,
// through the player's settings (key_config.h), into the driver's frame of 32 bytes the input phase reads (0x349FD0) and the
// race's other controls (views, the ghost, the pause). docs/specs/input.md.

#include "gt4r/input/key_config.h"
#include "gt4r/sim/rules_input.h"

#include <array>
#include <cmath>

namespace gt4r::input {

using gt3r::s16;
using gt3r::s32;
using gt3r::u16;
using gt3r::u32;
using gt3r::u64;

// What is plugged into a game port (PlayStation2::GT4GamePort::vf2, 0x564C10), with the 16 menu buttons the controller's own
// configuration puts before the pad's.
struct GamePort {
    enum class Pad : u8 { None, Digital, Joystick, SingleShock, DualShock, DualShock2 }; // the pad by what it can do: 5 reports pressures
    Pad pad = Pad::None;
    u8 wheel = 0; // 0 none, 1 Cougar, 2 Jaguar, 3 Cheetah
    // The axes: 0 .. 3 the right stick's X, Y, the left's X, Y (0 .. 255, 128 centered); 4 .. 15 the pressures (Right, Left, Up,
    // Down, Triangle, Circle, Cross, Square, L1, R1, L2, R2: 0 .. 255); 16 .. 23 the sticks' halves (the right's left, right, up,
    // down, then the left's; 0 .. 127); 24 .. 26 a wheel's steering (-8192 .. 8191, + right), throttle and brake (0 .. 255).
    std::array<s16, 27> axis{128, 128, 128, 128};
    // The buttons, a bit each: 0 .. 15 the menu's, 16 .. 31 the pad's (Select, L3, R3, Start, Up, Right, Down, Left, L2, R2, L1, R1,
    // Triangle, Circle, Cross, Square), 32 .. 63 a wheel's.
    u64 buttons = 0;
    // A stick's value finer than its byte (-1 .. 1; NaN: the byte's), by the axis' place 0 .. 3: ours, for pads whose sticks have
    // more than 256 steps. Steering alone reads it.
    std::array<float, 4> fine{NAN, NAN, NAN, NAN};
    // A trigger's travel (0 .. 1; NaN: none), for L2 and R2: ours, for pads without a DualShock 2's pressures. Where the button
    // accelerates, brakes or reverses, the travel does so as an axis.
    std::array<float, 2> trigger{NAN, NAN};

    bool button(int bit) const { return (buttons >> bit) & 1; }
    void set(int bit, bool on = true) { buttons = on ? buttons | (u64(1) << bit) : buttons & ~(u64(1) << bit); }
    // 0x564D8C: the halves from the sticks' bytes.
    void make_halves();
};
// The pad's buttons' bits, and a wheel's.
enum PadBit : int { kPadSelect = 16, kPadL3, kPadR3, kPadStart, kPadUp, kPadRight, kPadDown, kPadLeft, kPadL2, kPadR2, kPadL1, kPadR1, kPadTriangle, kPadCircle, kPadCross, kPadSquare };
enum WheelBit : int {
    kWheelSelect = 32, kWheelL3 = 33, kWheelR3 = 34, kWheelStart = 35, kWheelUp = 36, kWheelRight = 37, kWheelDown = 38, kWheelLeft = 39, kWheelL2 = 40, kWheelR2 = 41,
    kWheelL1 = 42, kWheelR1 = 43, kWheelTriangle = 44, kWheelCircle = 45, kWheelCross = 46, kWheelSquare = 47, kWheelL = 58, kWheelR = 59, kWheelShiftUp = 60, kWheelShiftDown = 61,
    kWheelAccel = 62, kWheelBrake = 63
};
// A button's bit in the port, by its place in the device's settings (0x6A64D0, 0x6A6588, 0x6A6590, 0x6A65D0).
int button_bit(Device d, size_t index);

// What the controller does with the port (RaceInput +0x1C4 / +0x1D4, 0x426F48): driving; a replay's or the computer driver's
// controls (no frame); driving with the pad's direction buttons taken by something else.
enum class Mode : u8 { Drive, Watch, DriveNoDirections };

// The controller's answer (the 0x24 bytes at RaceInput +0x170, its frame at +0x150).
struct RaceInput {
    sim::rules::PadFrame frame;
    u32 views = 0;   // +0x04: kBackView | kPlayView | kLeftView ..
    u32 menu = 0;    // +0x18: the menu's buttons (bits 0 .. 15); kPause
    u32 special = 0; // +0x20: kBestLine | kGhost
    static constexpr u32 kBackView = 1, kPlayView = 2, kLeftView = 0x4000, kRightView = 0x8000, kFrontView = 0x10000, kRearView = 0x20000;
    static constexpr u32 kPause = 0x40, kBestLine = 1, kGhost = 2;
    std::array<float, 4> stick{}; // +0x08: the left stick's x and y, the right's (right and up positive)
};

// 0x43CF90: the frame and the words cleared, then what the port and the settings give.
RaceInput read_port(const GamePort& port, const PortConfig& config, Mode mode = Mode::Drive);

// Its parts. A button pressed (0x438870) and an axis' action (0x4386F0: `kind` the frame's, 2 a stick, 4 and 6 the wheels;
// `value` -32768 .. 32767).
void press(RaceInput& in, ButtonAction action);
void move(RaceInput& in, u8 kind, AxisAction action, s32 value);
// The program's tables: an axis' byte as a signed value (0x622F58) and as a half's or a pressure's (0x623158).
s32 signed_value(int byte);
s32 travel_value(int byte);

// The units of the frame's steering (+-4096 from lock to lock) are 200 degrees of a GT FORCE wheel and 900 of a Driving
// Force Pro (the steering force's rate tables 0x359A38). A wheel's port value for an axis (-1 .. 1, + right) whose travel is
// `rotation` degrees lock to lock: the angle kept, past the original wheel's lock held there.
s16 wheel_axis(u8 wheel, float axis, float rotation);

} // namespace gt4r::input
