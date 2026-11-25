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
    for (int h = 0; h < 2; ++h) hand[h] = r.at(0x48 + 4 * u32(h)), way[h] = f(0), state[h] = was[h] = 0;
    idle = f(0);
    rates.fill(f(0));
}

namespace {

F clamped(F v, F least, F most) { return min(max(v, least), most); }
// 0x40F7C8: up and down again over the way.
F hump(F t) {
    const F u = clamped(t, f(0), f(1)) - f(0.5f), v = u + u;
    return (f(1) - (v * v)) * f(0.099999994f);
}
// 0x4145C8: the table between its eleven places, times the pace (0x40F670: 1.1 of the wheel's rate, 14.05 .. 28.1 rad/s).
F hand_speed(F t, F rate, const HandSpeeds& table) {
    const F pace = clamped(abs(rate) * f(1.0999999f), f(14.0499001f), f(28.0998001f));
    F share;
    if (t <= f(0)) share = table[0];
    else if (f(1) <= t) share = table[10];
    else {
        const F x = t * f(10);
        const s32 i = gt3r::ee::to_int(x);
        const F u = x - gt3r::ee::int_to_f(i);
        share = (table[size_t(i)] * (f(1) - u)) + (table[size_t(i) + 1] * u);
    }
    return share * pace;
}
// 0x40F5C0: where a hand goes beside the other.
F beside(int h, F other) {
    F a = h == 0 ? other + f(-1.88495529f) : other + f(1.88495529f);
    a = clamped(a, f(-2.51327372f), f(2.51327372f));
    if (h == 0 && f(-1.17809713f) < a) a = f(-1.17809713f);
    if (h == 1 && a < f(1.17809713f)) a = f(1.17809713f);
    return a;
}

} // namespace

// One hand (0x40FC78), the other as it stands.
void DriverState::hand_step(int h, bool regrip, F turned, F dt, F rate, const HandSpeeds& speeds) {
    const int o = 1 - h;
    const u32 hh = u32(h), oo = u32(o);
    F &from = after.at(0x48 + 4 * hh), &to = after.at(0x40 + 4 * hh), &blend = after.at(0x50 + 4 * hh), &back = after.at(0x78 + 4 * hh);
    const F other_from = after.at(0x48 + 4 * oo), other_to = after.at(0x40 + 4 * oo), other_blend = after.at(0x50 + 4 * oo);
    F& cur = hand[h];
    const F zero = f(0), one = f(1), fast = abs(rate);
    F least = f(h == 0 ? -2.98451281f : -2.82743311f), most = f(h == 0 ? 2.82743311f : 2.98451281f);
    if (state[o] != 2) {
        const F more = fast * f(0.099999994f);
        least = least + more, most = most - more;
    }
    most = max(most, f(2.35619426f));
    least = min(least, f(-2.35619426f));
    const F close = f(0.314159214f);
    // Toward `to`; whether it got there.
    const auto go = [&](F t) {
        if (cur < to) {
            cur = cur + (hand_speed(t, rate, speeds) * dt);
            return to < cur;
        }
        cur = cur - (hand_speed(t, rate, speeds) * dt);
        return cur < to;
    };
    const auto share = [&] { return min(max(abs(to - cur) / way[h], zero), one); };
    for (bool again = true; again;) {
        again = false;
        switch (state[h]) {
        case 0: // with the wheel
            cur = cur + turned;
            if (regrip) {
                if (cur < least) cur = least, state[h] = 1;
                else if (most < cur) cur = most, state[h] = 1;
            }
            from = cur, to = cur;
            blend = zero;
            break;
        case 1: // at the end of its turn: a new place on the rim
            if (state[o] == 2 || state[o] == 3 || state[o] == 5 || state[o] == 6) {
                state[h] = 0, again = true;
                break;
            }
            if (fast < f(3.1415925f)) {
                to = min(max((f(-0.111111157f) * fast) + f(1.60570252f), close), f(1.57079625f));
                if (from < zero) to = -to;
            } else {
                to = min(max((f(0.0666666552f) * fast) + f(-0.209439471f), close), f(1.09955728f));
                if (!(from < zero)) to = -to;
            }
            blend = zero;
            state[h] = h == 0 ? (other_from < to ? 2 : 3) : (to < other_from ? 2 : 3);
            way[h] = abs(to - from);
            if (way[h] < close) state[h] = 0;
            cur = from;
            again = true;
            break;
        case 2:
        case 3: {
            const F t = share();
            if (state[h] == 3) {
                blend = one - t;
                back = hump(t) * f(0.099999994f);
            } else {
                blend = zero;
                back = hump(t);
            }
            const bool there = go(t);
            if (state[h] == 2) from = cur;
            if (there) {
                from = to, cur = to;
                blend = zero, back = zero;
                state[h] = 0, state[o] = 4;
            }
            break;
        }
        case 4: // the other hand has gone: beside it
            if (state[o] == 2 || state[o] == 3 || state[o] == 5 || state[o] == 6) {
                state[h] = 0, again = true;
                break;
            }
            blend = zero;
            to = beside(h, (other_from * (one - other_blend)) + (other_to * other_blend));
            state[h] = h == 0 ? (other_from < to ? 5 : 6) : (to < other_from ? 5 : 6);
            way[h] = abs(to - from);
            if (way[h] < close) state[h] = 0;
            cur = from;
            again = true;
            break;
        case 5:
        case 6: {
            const F t = share();
            if (state[h] == 6) {
                blend = one - t;
                back = zero;
            } else {
                blend = zero;
                back = hump(t);
            }
            const bool there = go(t);
            if (state[h] == 5) from = cur;
            if (there) {
                from = to, cur = to;
                blend = zero, back = zero;
                state[h] = 0;
            }
            break;
        }
        case 7: // back to rest
            state[h] = 8;
            to = f(h == 0 ? -1.09955728f : 1.09955728f);
            way[h] = abs(to - from);
            if (way[h] < f(0.157079607f)) state[h] = 0;
            cur = from;
            again = true;
            break;
        case 8: {
            if (state[o] == 2) {
                state[h] = 0, state[o] = 4;
                break;
            }
            const F t = share();
            back = hump(t);
            const bool there = go(t);
            from = cur;
            if (there) {
                from = to, cur = to;
                blend = zero, back = zero;
                state[h] = 0;
            }
            break;
        }
        default: break;
        }
    }
}

void DriverState::step(const Cockpit& c, F follow, F dt, const HandSpeeds& speeds) {
    before = after;
    if (c.style != 0) return;
    const F rate_of = f(1) / dt;
    // The wheel (0x403F90): 0.4 of the follower, 480 degrees each way (90 in a car with a yoke).
    const bool yoke = c.lever_kind == 1;
    F most = f(8.37757969f), least = -most;
    if (yoke) most = min(f(1.57079625f), most), least = max(f(-1.57079625f), least);
    const F angle = min(max((-follow) * f(0.399999976f), least), most);
    const F rate = (angle - after.at(0x34)) * rate_of;
    after.at(0x34) = angle;
    F sum = f(0);
    for (size_t k = 3; k > 0; --k) {
        rates[k] = rates[k - 1];
        sum = sum + rates[k];
    }
    rates[0] = rate;
    const F mean = (sum + rate) / f(4);
    // The hands.
    was[0] = state[0], was[1] = state[1];
    const F turned = rate * dt;
    hand_step(0, !yoke, turned, dt, mean, speeds);
    hand_step(1, !yoke, turned, dt, mean, speeds);
    if (!yoke) { // 0x411150: too close together, or crossed, for a while
        if (state[0] == 0 && state[1] == 0) {
            const F left = after.at(0x48), right = after.at(0x4C);
            if (abs(left - right) < f(1.57079625f)) {
                idle = idle + dt;
                if (f(0.25f) < idle) {
                    int h = 0;
                    if (f(0) < left) h = 0;
                    else if (right < f(0)) h = 1;
                    else if (-left < right) h = 0;
                    else h = 1;
                    state[h] = 4;
                    idle = f(0);
                }
            } else if (right < f(0)) {
                idle = idle + dt;
                if (f(2) < idle) state[1] = 7, idle = f(0);
            } else if (f(0) < left) {
                idle = idle + dt;
                if (f(2) < idle) state[0] = 7, idle = f(0);
            } else {
                idle = f(0);
            }
        }
        if (state[0] != was[0] || state[1] != was[1]) idle = f(0);
    }
    // The elbows (0x406388) and the clavicles (0x4053A8).
    const F step_most = (f(1.83259571f) * dt) / f(1.57079625f);
    const F lift[2] = {(hand[0] + f(1.57079625f)) * f(0.599999964f), (f(1.57079625f) - hand[1]) * f(0.599999964f)};
    for (u32 h = 0; h < 2; ++h) {
        const F want = min(max(min(max(lift[h], f(0.17453292f)), f(1.04719746f)) / f(1.57079625f), f(0)), f(1));
        F& e = after.at(0x38 + 4 * h);
        const F d = want - e;
        if (abs(d) < step_most) e = want;
        else if (d < f(0)) e = e - step_most;
        else e = e + step_most;
    }
    for (u32 h = 0; h < 2; ++h) after.at(0x8C + 4 * h) = max((after.at(0x38 + 4 * h) * f(1.57079625f)) - f(0.767944753f), f(0)) * f(0.174999997f);
}

} // namespace gt4r::race::posture
