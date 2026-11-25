#include "gt4r/race/driver_state.h"

namespace gt4r::race::posture {

namespace ee = gt3r::ee;

void DriverState::reset(const Cockpit& c) {
    DriverRecord r;
    // Ahead of the seat (0x40DC08).
    r.at(0x58) = c.seat.x + f(0);
    r.at(0x5C) = c.seat.y + f(0);
    r.at(0x60) = c.seat.z + f(-6.25f);
    r.at(0x38) = r.at(0x3C) = f(0.299999982f);
    F left = f(-1.09955728f), right = f(1.09955728f);
    r.at(0xB4) = f(-0.872664511f);
    if (c.lever_kind == 1) {
        left = f(-kHalfPi), right = f(kHalfPi);
        r.at(0xB4) = f(0);
    } else if (c.style == 3) {
        left = f(-2.35619426f), right = f(2.35619426f);
        r.at(0xB4) = f(-1.09955728f);
        r.at(0xCC) = r.at(0xDC) = f(-kHalfPi);
    } else if (c.style == 4) {
        left = f(-kPi), right = f(kPi);
        r.at(0xB4) = f(-kHalfPi);
    } else if (c.style == 5) {
        // The right hand at the column lever's turn.
        const V3 place{c.lever_pivot.x, c.lever_pivot.y, c.lever_pivot.z};
        V3 axis = axis_y();
        if (!(c.column.y == f(0))) axis = normalized({f(0), c.column.z, -c.column.y});
        right = angle_between(axis, place);
        r.at(0xB4) = f(0);
        const V3 col = c.column, w = c.wheel, l = c.lever;
        r.at(0x74) = c.lever_scale - c.wheel_radius;
        r.at(0x7C) = ((((col.x * l.x) + (col.y * l.y)) + (col.z * l.z)) + (((-col.x * w.x) - (col.y * w.y)) - (col.z * w.z))) - f(0.0399999991f);
    }
    r.at(0x48) = r.at(0x40) = left;
    r.at(0x4C) = r.at(0x44) = right;
    r.at(0x50) = r.at(0x54) = f(1);
    r.at(0x64) = f(0.5f);
    r.at(0x80) = r.at(0x84) = f(1);
    r.at(0x98) = f(0.5f);
    const bool lying = c.style == 1;
    r.at(0x6C) = lying ? f(kHalfPi) : f(0);
    r.at(0xBC) = lying ? f(0.5f) : f(1);
    r.at(0xE4) = r.at(0xE8) = f(0.099999994f);
    before = after = r;
}

} // namespace gt4r::race::posture
