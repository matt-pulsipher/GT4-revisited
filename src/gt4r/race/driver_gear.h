#pragma once

// The driver's hand on the gear lever (the program's 0x411FB0 → 0x412018, a step of the driver's record for the cars whose
// lever has a layout of kind 1 or 4): the layout from the car's gearbox, the lever's place for a gear, what the driver
// reaches for before a shift, and the hand along the lever's gate. docs/formats/model.md "What the driver does".

#include "gt4r/race/posture_math.h"

namespace gt4r::race::posture {

// The lever's layout (the car +0x1080 .. +0x108C, set at a race's set-up from the car's GEAR part by 0x385410 → 0x446178:
// the row's +0x2C, +0x2A, +0x2B, +0x24). Kind 1: a gate of two rows, the gears from slot `offset` on, reverse at slot
// `reverse`; kind 4: a lever to and fro (forward or not); others (0, 3): no lever.
struct GearLayout {
    int kind = 0;    // +0x1080 (GEAR +0x2C)
    int offset = 0;  // +0x1084 (GEAR +0x2A, signed): first gear's slot
    int reverse = 0; // +0x1088 (GEAR +0x2B, signed): reverse's slot
    int count = 0;   // +0x108C (GEAR +0x24): the forward gears
};
// From a GEAR row (at least 0x2D bytes); none from a shorter one.
GearLayout gear_layout(ByteSpan gear_row);

// The lever's place for a gear (0x411EB0): x across the gate (-1 .. 1, a slot pair a half apart), y up (1) or down (-1);
// gear 0 is reverse.
void gate_place(const GearLayout& g, int gear, F& x, F& y);

// What the shift reach looks at (0x40CA58), from the car: the shown engine speed int(car +0x738) (0x350F30), the forward
// gears (P +0x236), the gear (car +0x624), the throttle and brake pedals (car +0x58C / +0x590), the gear for the lesser of the
// driver's two target speeds (D +0x24 / +0x28; 0x36BF68), the up-shift rpm (0x36BEE0) and the gear's down-shift rpm (0x36BF28,
// read only from second gear on).
struct ShiftInputs {
    int rpm = 0, top = 0, gear = 0;
    F throttle, brake;
    int suggested = 0;
    s32 up_rpm = 0, down_rpm = 0;
};
// The three wants: an up-shift near (the rpm within 1,000 of the up-shift in first gear with the throttle over 0.2, within 300
// in the others over 0.7), a down-shift near (the rpm under the down-shift: by 1,000 in the suggested gear, by none on the
// throttle, by 500 more off both pedals, by 1,000 more on the brake), a down-shift of more than one (on the brake, the suggested
// gear lower).
struct ShiftReach {
    bool up = false, down = false, far = false;
    bool any() const { return up || down || far; }
};
ShiftReach shift_reach(const ShiftInputs& s);

// The engine speed as the race shows it (car +0x738 and its rate +0x73C; 0x350ED0 each step after the gearbox, 0x350EB0 at the
// start): a spring (1,000, damping 50) after the rpm (F +0x4FA).
struct ShownRpm {
    F value, rate;
    void start(s32 rpm) { value = gt3r::ee::int_to_f(rpm), rate = F(0.0f); }
    void step(s32 rpm, F dt);
};

// The hand moving the lever along the gate to a gear's place (0x411628): it waits a sixth of a second (`wait` to 1 at 6 a
// second), then, off the gear's column, back to the middle row, across, then up or down to the gear, springs of 60 (rows,
// damping 3 when 0.75 or more from the middle) and 30 (across, none) at most 30 and 15 a second. `want_x` / `want_y` the hand's
// grip turns it asks for (0.7854 / 0.0349 rad). Returns whether the lever is at the gear's place (the wait cleared).
bool gate_hand(const GearLayout& g, int gear, F dt, F& x, F& y, F& rate_x, F& rate_y, F& wait, F& want_x, F& want_y);

} // namespace gt4r::race::posture
