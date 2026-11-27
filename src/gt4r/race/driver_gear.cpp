#include "gt4r/race/driver_gear.h"

namespace gt4r::race::posture {

namespace ee = gt3r::ee;

GearLayout gear_layout(ByteSpan row) {
    GearLayout g;
    if (row.size() < 0x2D) return g;
    g.kind = row[0x2C];
    g.offset = gt3r::s8(row[0x2A]);
    g.reverse = gt3r::s8(row[0x2B]);
    g.count = row[0x24];
    return g;
}

void gate_place(const GearLayout& g, int gear, F& x, F& y) {
    x = f(0), y = f(0);
    if (g.kind == 4) {
        y = gear <= 0 ? f(1) : f(-1);
        return;
    }
    if (g.kind != 1) return;
    int s = gear < g.count ? gear : g.count;
    s = 0 < s ? (s + g.offset) - 1 : g.reverse;
    x = ee::int_to_f(0 < s ? ((s - 1) / 2) - 1 : (s / 2) - 2) * f(0.5f);
    y = (s & 1) ? f(1) : f(-1);
}

ShiftReach shift_reach(const ShiftInputs& s) {
    ShiftReach r;
    const int g = s.gear;
    const s32 up = g == 0 || g == s.top ? 99999 : s.up_rpm;
    const s32 down = g < 2 ? -99999 : s.down_rpm;
    if (g == 1) {
        if (up - 1000 < s.rpm && F::raw(0x3E4CCCCC) < s.throttle) r.up = true;
    } else if (up - 300 < s.rpm && F::raw(0x3F333333) < s.throttle) {
        r.up = true;
    }
    if (g == s.suggested) {
        if (s.rpm < down - 1000) r.down = true;
    } else if (f(0) < s.throttle) {
        if (s.rpm < down) r.down = true;
    } else if (s.brake == f(0)) {
        if (s.rpm < down + 500) r.down = true;
    } else {
        if (s.rpm < down + 1000) r.down = true;
        if (0 < s.suggested && s.suggested < g) r.far = true;
    }
    return r;
}

void ShownRpm::step(s32 rpm, F dt) {
    rate = rate + ((((ee::int_to_f(rpm) - value) * f(1000)) - (rate * f(50))) * dt);
    value = value + (rate * dt);
}

namespace {

// A row's spring (the tuning object's +0x2C 60, its top speed +0x24 30): `toward` 1 up, -1 down.
void row_step(F& y, F& rate, F toward, F damp, F dt) {
    F a = f(0) + (toward == f(1) ? f(60) : f(-60));
    a = a - (rate * damp);
    rate = rate + (a * dt);
    if (f(900) < rate * rate) rate = f(0) < rate ? f(30) : f(-30);
    y = y + (rate * dt);
}
// Across the gate (+0x28 30, its top speed +0x20 15), undamped.
void across_step(F& x, F& rate, F toward, F dt) {
    const F a = f(0) + (toward == f(1) ? f(30) : f(-30));
    rate = rate + (a * dt);
    if (f(225) < rate * rate) rate = f(0) < rate ? f(15) : f(-15);
    x = x + (rate * dt);
}

} // namespace

bool gate_hand(const GearLayout& g, int gear, F dt, F& x, F& y, F& rate_x, F& rate_y, F& wait, F& want_x, F& want_y) {
    const F zero = f(0), turn = F::raw(0x3F490FDA), tilt = F::raw(0x3D0EFA35);
    const F damp = abs(y) < f(0.75f) ? zero : f(3);
    F tx, ty;
    gate_place(g, gear, tx, ty);
    want_x = zero, want_y = zero;
    const F dx = tx - x, dy = ty - y;
    if (x == tx) {
        if (zero < dy) want_x = -turn;
        else if (dy < zero) want_x = turn;
    } else if (y == zero) {
        if (zero < dx) want_y = -tilt;
        else if (dx < zero) want_y = tilt;
    } else if (zero < y) {
        want_x = turn;
    } else if (y < zero) {
        want_x = -turn;
    }
    if (x == tx && y == ty) {
        wait = zero;
        return true;
    }
    if (wait < f(1)) {
        wait = wait + (f(6) * dt);
        return false;
    }
    if (x == tx) { // along the gear's column
        if (zero < dy) {
            row_step(y, rate_y, f(1), damp, dt);
            if (ty <= y) {
                rate_y = zero, y = ty, wait = zero;
                return true;
            }
            if (y < f(-1)) rate_y = zero, y = f(-1);
        } else if (dy < zero) {
            row_step(y, rate_y, f(-1), damp, dt);
            if (y <= ty) {
                rate_y = zero, y = ty, wait = zero;
                return true;
            }
            if (f(1) < y) rate_y = zero, y = f(1);
        }
        return false;
    }
    if (y == zero) { // across the middle row
        if (zero < dx) {
            across_step(x, rate_x, f(1), dt);
            if (tx <= x) rate_x = zero, x = tx;
            else if (x < f(-2)) rate_x = zero, x = f(-2);
        } else if (dx < zero) {
            across_step(x, rate_x, f(-1), dt);
            if (x <= tx) rate_x = zero, x = tx;
            else if (f(2) < x) rate_x = zero, x = f(2);
        }
        return false;
    }
    // Back to the middle row first.
    if (zero < y) {
        row_step(y, rate_y, f(-1), damp, dt);
        if (y < zero) rate_y = zero, y = zero;
        else if (f(1) < y) rate_y = zero, y = f(1);
    } else {
        row_step(y, rate_y, f(1), damp, dt);
        if (zero < y) rate_y = zero, y = zero;
        else if (y < f(-1)) rate_y = zero, y = f(-1);
    }
    return false;
}

} // namespace gt4r::race::posture
