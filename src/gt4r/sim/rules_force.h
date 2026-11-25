#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): the steering wheel's force, phase 19 after the pad's vibration
// (0x353EE8), into the conductor's pad block of the car (cond + 0xCA58 + 0x10 x the car's index F + 0x440: +0x02 the force,
// 0x80 centered), and its tables (0x353938, at the race's start: the tail of the input's tables 0x347A00).

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_input.h"
#include "gt4r/sim/rules_steer.h"

#include <string>

namespace gt4r::sim {
struct Extra;
class Program;
} // namespace gt4r::sim

namespace gt4r::sim::rules {

// One mode's table (0xB0 bytes at 0x8436B0 + 0xB0 m), from the controller settings' row S = K + 0x7C + 0x20 m (K the
// settings 0x622110) and K's own bytes +0x74 .. +0x7A. Mode 0 a pad's or a wheel's other than kind 6; 1 a kind 6 wheel whose
// steering ratio byte (P + 0x10D3) is 0; 2 one whose byte is not 0 (the steering prefilter's rows, K + 0x8A + 0x20 m, are the
// same rows' bytes +0x0E / +0x0F).
struct ForceMode {
    F align_gain;                 // +0x00: S[3] x 0.1: the front wheels' aligning term (their lateral axes against the ground)
    std::array<F, 2> gain;        // +0x04 / +0x08: S[0] / S[0x14] x 0.02: the lateral term's gain on a paved / loose course
                                  // (with limit kind 1 its bound instead)
    std::array<PedalCurve, 2> curve; // +0x0C / +0x1C: (S[1], S[2]) / (S[0x15], S[0x16]): the lateral term's curve (0x35A818)
    F turn_gain;                  // +0x2C: S[5] x 0.01 / 4096: the frame's steering change a second
    F steer_gain;                 // +0x30: S[6] x 0.01 / steer_max (0 when S[6] is 0): the steering command
    F steer_max;                  // +0x34: S[7] tenths of a degree: the command's bound for that term
    F offset_gain;                // +0x38: S[0x10] x 0.01: the steering offset's angle (with the steering mode on)
    F offset_damp;                // +0x3C: S[0x11] (signed) x 0.01 / (pi / 180): its rate
    PedalCurve offset_curve;      // +0x40: (S[0x12], S[0x13]): the offset's curve, on the offset over its limit
    u8 slip_on = 0;               // +0x50: 1 when S[0xA] + S[0xB] and S[0xC] are not 0: each front wheel's curve A force
                                  // scaled by the shares below (else as it is)
    F slip_base, slip_share, slip_inv, drive_share; // +0x54 / +0x58 / +0x5C / +0x60: A / (A + B), B / (A + B), 1 / (C degrees),
                                  // D / (A + B) for A .. D = S[0xA] .. S[0xD] (left as they were with slip_on 0)
    F x64;                        // +0x64: K[0x74] / 100 ❓ its readers
    F x68, x6c;                   // +0x68 / +0x6C: 8 / the words 0x62025C / 0x620264 (the kind 6 ratios) ❓ their readers
    F x70;                        // +0x70: K[0x75] x 2.55 with K[0x7A] set, else 0 ❓
    F x74;                        // +0x74: K[0x76] x 2.55 ❓
    u8 x78 = 0;                   // +0x78: K[0x75] with K[0x7A] 0, else 0 ❓
    std::array<u8, 3> wheel{};    // +0x79 .. +0x7B: K[0x77] .. K[0x79]: a kind 6 wheel's bytes for the pad block's +0x0D ..
                                  // +0x0F on a paved course
    u8 limit_kind = 0;            // +0x7C: S[0x17]: the lateral term after its curve: 0 (and 3 ..) x gain x strength, 1
                                  // within +-gain then x strength, 2 within +-strength then x gain
    std::array<F, 3> strength;    // +0x80 + 4 k: S[0x18 + k] x 0.01: a kind 6 wheel's strength by the wheel's own choice
    // +0x8C / +0x98 / +0xA4 (0x359A38): a table of three points (x the frame's steering change a step, y its factor): x 0,
    // S[0x1D] x 10, S[0x1E] x 10 degrees a second (8192 units over 200 degrees for mode 0, 900 for 1 and 2), y 1, S[0x1B] x
    // 0.01, S[0x1C] x 0.01
    s16 rate_n = 0;
    std::array<F, 3> rate_x, rate_y;
};
struct ForceTables {
    std::array<ForceMode, 3> mode;
    u8 offset_feel = 0; // 0x622173 (K + 0x63): the steering offset's term with the steering mode (0x845BF0) on: 1 below 2 km/h,
                        // 2 always
};
constexpr u32 kForceTables = 0x8436B0, kForceModeSize = 0xB0, kForceRows = 0x62218C, kForceRowSize = 0x20, kOffsetFeel = 0x622173;
// 0x353938's mode `mode` on the settings' bytes K (kForceSettingsSize of them from kPadSettings; the rows at K + 0x7C) and the
// kind 6 ratios the input's tables made before it (PadTables::wheel6_ratio: the words 0x62025C and 0x620264).
constexpr u32 kForceSettingsSize = 0xE0;
ForceMode force_mode(const u8* settings, F ratio_0x62025c, F ratio_0x620264, int mode);
// Built from the program's bytes as the race's start builds them.
ForceTables force_tables(const Program& program);
// As the race's start left them in memory (`read(address)` a word, `byte(address)` a byte).
template <class W, class B> ForceTables read_force_tables(W&& read, B&& byte) {
    ForceTables t;
    for (u32 m = 0; m < 3; ++m) {
        const u32 T = kForceTables + kForceModeSize * m;
        auto f = [&](u32 o) { return F::raw(read(T + o)); };
        auto curve = [&](u32 o) { return PedalCurve{read(T + o), f(o + 4), f(o + 8), f(o + 0xC)}; };
        ForceMode& k = t.mode[m];
        k.align_gain = f(0), k.gain = {f(4), f(8)}, k.curve = {curve(0xC), curve(0x1C)};
        k.turn_gain = f(0x2C), k.steer_gain = f(0x30), k.steer_max = f(0x34), k.offset_gain = f(0x38), k.offset_damp = f(0x3C);
        k.offset_curve = curve(0x40);
        k.slip_on = byte(T + 0x50), k.slip_base = f(0x54), k.slip_share = f(0x58), k.slip_inv = f(0x5C), k.drive_share = f(0x60);
        k.x64 = f(0x64), k.x68 = f(0x68), k.x6c = f(0x6C), k.x70 = f(0x70), k.x74 = f(0x74), k.x78 = byte(T + 0x78);
        for (u32 j = 0; j < 3; ++j) k.wheel[j] = byte(T + 0x79 + j), k.strength[j] = f(0x80 + 4 * j);
        k.limit_kind = byte(T + 0x7C);
        k.rate_n = s16(u16(read(T + 0xA4) & 0xFFFF));
        const u32 xs = read(T + 0xA8), ys = read(T + 0xAC);
        for (u32 j = 0; j < 3; ++j) k.rate_x[j] = F::raw(read(xs + 4 * j)), k.rate_y[j] = F::raw(read(ys + 4 * j));
    }
    t.offset_feel = byte(kOffsetFeel);
    return t;
}
// The tables' values equal, bit for bit (the slip shares only where slip_on is set); `why` names the first that is not.
bool same(const ForceTables& a, const ForceTables& b, std::string* why = nullptr);

// The pad block's bytes the force writes (cond + 0xCA58 + 0x10 k; +0x00 / +0x01 are the vibration's, FeelExtra::pad). Every
// call clears +0x08 (s16), +0x0A, +0x0B and +0x0D .. +0x0F first; +0x0C is not the force's.
struct ForceExtra {
    u8 out = 0x80;               // +0x02: 0x80 - the force (within +-1) x 127, truncated
    std::array<u8, 3> wheel{};   // +0x0D .. +0x0F: ForceMode::wheel for a kind 6 wheel on a paved course, else 0
    F scaled;                    // the force x 127 before the truncation (the program's $f0 at the return; a computer's car: as it was)
};

// What the race gives the routine beside the car.
struct ForceRace {
    bool loose = false;       // 0x34C998 on the conductor not 0: a loose course (the course's kind 2 or 3)
    bool lan_average = false; // the race object's virtual +0x318 > 0 (a LAN battle's): the steering change a step from the mean
                              // of this frame and the last
    // The controller's answer to its virtual +0x20 of the table at controller + 0xD0 (RaceInput::vf4: 1 and 1, unless its port
    // gives its own: OptionRaceInputCheetah::vf11 (1) and (2)): the strength's index (ForceMode::strength) and, not 0, the
    // steering change's factor on; both for a kind 6 wheel only.
    s16 strength = 1, turn_factor = 1;
};

// 0x353EE8: the steering wheel's force. A car a computer drives (control source 1 / 2 or a driver kind): the frame's steering
// (F + 0x454, doubled for a kind 6 wheel) / 32 from 0x80, within 0 .. 255. Else the mode by F + 0x465 (3: 1 + (P + 0x10D3 not 0),
// else 0): the front wheels' curve A forces (with slip_on, each x (base + (1 - |slip angle| x inv) x share + its driving and
// braking forces / half the front axle's load x drive_share), within -1 .. 2) over the front axle's load within +-1, its curve
// (paved or loose), the limit kind's gain and bound; with the steering mode on (and offset_feel's switch) the front wheels'
// offsets (0x845BF0's mode 1 the offset, 2 the offset less the steer angle, over 0x845BF8's limit, its curve) x offset_gain less
// their rates x offset_damp, within +-1; + turn_gain x the frame's steering change a second, + steer_gain x the command within
// +-steer_max, + align_gain x the front wheels' aligning moment (their lateral axes turned by the steer angle . the ground's
// normal, x the load) x (P + 0x31C + 49,000) / 196,000 / the axle's load. Below 2 km/h (and not the offset's term): the
// command within +-5 degrees x 1 / 10 degrees. Within +-1; a kind 6 wheel with turn_factor x the table on the frame's steering
// change in the force's own direction; the start's release count (F + 0x633) counted down, x (30 - it) / 30.
void steer_force(ex::Car& c, sim::Extra& x, const WheelLayout& l, const ForceTables& t, const SteerTables& g, const ForceRace& r);

} // namespace gt4r::sim::rules
