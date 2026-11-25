#include "gt4r/race/posture_hands.h"

namespace gt4r::race::posture {

namespace ee = gt3r::ee;

namespace {

// A point of the rim at an angle (its sine and cosine), `r` from the centre and `d` away along the column (0x414FE8).
V3 rim_point(const Places& p, F s, F c, F r, F d) { return transform(p.wheel_frame, {-r * s, r * c, -d}) + p.wheel; }

// The rim's outward direction there for the palm (0x4150A0) and its tangent, tilted by an angle (0x415148), in the frame.
V3 rim_palm(const Places& p, int side, F s, F c) {
    const V3 v = side ? V3{-c, -s, f(0)} : V3{c, s, f(0)};
    return rotate(p.turn, transform(p.wheel_frame, v));
}
V3 rim_tangent(const Places& p, F tilt, F s, F c) {
    V3 v{-s, c, f(0)};
    if (!(tilt == f(0))) v = rotate(axis_angle({-c, s, f(0)}, tilt), v);
    return rotate(p.turn, transform(p.wheel_frame, v));
}

// On the rim between two angles (0x415E90).
V3 between_grips(const HandSolve& h, F from, F to, const Places& p, F t) {
    F s0, c0, s1, c1;
    ee::sincos(from, s0, c0);
    ee::sincos(to, s1, c1);
    const V3 a = rim_point(p, s0, c0, p.wheel_radius + h.out, h.back) * (f(1) - t);
    const V3 b = rim_point(p, s1, c1, p.wheel_radius + h.out, h.back) * t;
    return a + b;
}

// The palm and fingers of a hand on the rim (0x416040).
void grip(HandSolve& h, F angle, const Places& p) {
    if (h.grip_kind == 1) {
        h.palm = rim_tangent(p, f(0), f(h.side ? 1 : -1), f(0));
        h.finger = axis_y();
        return;
    }
    if (h.grip_kind == 2) {
        h.palm = -axis_y();
        h.finger = rim_tangent(p, f(0), f(h.side ? 1 : -1), f(0));
        return;
    }
    const F slope = f(0.11111109f), most = f(0.0872664526f), at = f(0.0872664377f);
    if (h.side == 0 && f(-0.785398126f) < angle) angle = angle + clamp((angle * slope) + at, f(0), most);
    else if (h.side == 1 && angle < f(0.785398126f)) angle = angle + clamp((angle * slope) + -at, -most, f(0));
    F s, c;
    ee::sincos(angle, s, c);
    h.palm = rim_palm(p, h.side, s, c);
    h.finger = rim_tangent(p, f(0), s, c);
}

int past_side(const HandSolve& h, F angle) {
    return h.side == 0 ? angle < f(-kHalfPi) : f(kHalfPi) < angle;
}

// On the rim (0x416298).
int on_rim(HandSolve& h, F from, F to, const Places& p, F t) {
    const F angle = (from * (f(1) - t)) + (to * t);
    h.pos = between_grips(h, from, to, p, t);
    grip(h, angle, p);
    h.grip_side = f(0.5f); // 0x416278
    h.hand_turn = f(1);
    return past_side(h, angle);
}

// The lever through the gear change (0x414E78).
V3 lever_at(const Places& p) {
    F q = p.gesture;
    V3 a, b;
    if (f(0) <= q && q <= f(1)) {
        a = p.lever[0], b = p.lever[1];
    } else if (q <= f(2)) {
        q = q - f(1);
        a = p.lever[1], b = p.lever[2];
    } else {
        q = q - f(2);
        a = p.lever[2], b = p.lever[0];
    }
    return (a * (f(1) - q)) + (b * q);
}

F bump(F x, F middle, F height) { // 0x415A68
    if (x <= f(0) || f(1) <= x) return f(0);
    const F t = x < middle ? x / middle : (f(1) - x) / (f(1) - middle);
    return height * ee::sin(t * f(kHalfPi));
}

// How far the hand comes towards the driver on its way past the rim (0x415AF8).
bool clear_rim(V3& out, const Places& p, F to, F part) {
    V3 d = lever_at(p) - p.wheel;
    const F dd = dot(d, d);
    if (dd < f(9.99999975e-05f)) return false;
    const F len = ee::sqrt(dd);
    d = d * (f(1) / len);
    const F a = signed_angle_about(d, axis_y(), p.column); // 0x415828
    const F u = len * ee::sin(a), w = len * ee::cos(a);
    const F rr = p.wheel_radius * p.wheel_radius, ll = (u * u) + (w * w);
    if (ll <= rr) return false; // 0x415978
    const F l = ee::sqrt(ll);
    const F spread = f(kHalfPi) - asin(p.wheel_radius / l);
    F at = f(kHalfPi) - asin(w / l);
    if (u < f(0)) at = -at;
    const F off = ee::abs(wrap(to - at, f(-kPi)));
    const F lo = spread * f(0.799999952f), hi = spread * f(1.19999993f);
    const F slope = f(1) / (hi - lo);
    const F start = slope * ((hi * f(0)) - lo);
    const F amount = clamp((slope * off) + start, f(0), f(1)) * bump(part, f(0.5f), f(0.099999994f));
    out = p.column * amount;
    return true;
}

// Between the rim and the lever (0x416390); x 0 .. 1 the way there and back.
int to_lever(HandSolve& h, F from, F to, const Places& p, F blend, F x) {
    const V3 lever = lever_at(p) + h.offset;
    const F quarter = f(0.25f);
    if (f(0) <= x && x <= quarter) {
        const F t = (x - f(0)) * f(4);
        const F rest = f(1) - t;
        h.pos = (between_grips(h, from, to, p, blend) * rest) + (lever * t);
        V3 v;
        if (clear_rim(v, p, to, rest)) h.pos = h.pos + v;
        h.grip_side = f(0.5f);
    } else if (quarter <= x && x <= f(0.75f)) {
        h.pos = lever;
        h.grip_side = f(0.5f);
    } else if (f(0.75f) <= x && x <= f(1)) {
        const F t = (x - f(0.75f)) * f(4);
        const V3 rim = between_grips(h, from, to, p, blend);
        h.pos = (lever * (f(1) - t)) + (rim * t);
        V3 v;
        if (clear_rim(v, p, to, t)) h.pos = h.pos + v;
        h.grip_side = f(0.5f);
    }
    F angle = f(0);
    const auto mixed = [&] { return (from * (f(1) - blend)) + (to * blend); };
    const F fifth = f(0.199999988f), four = f(0.799999952f);
    if (f(0) <= x && x <= fifth) {
        angle = mixed() * (f(1) - ((x - f(0)) / fifth));
        grip(h, angle, p);
        h.hand_turn = f(1);
        h.wrist = f(0);
    } else if (fifth <= x && x <= four) {
        const F t = (x - fifth) / f(0.599999964f);
        const V3 up = axis_y();
        if (t <= fifth) {
            const F u = t * f(5);
            h.finger = turned_towards(rim_tangent(p, f(0), f(0), f(1)), up, nullptr, u);
            h.hand_turn = f(1) - u;
            h.wrist = u * h.wrist_add;
        } else if (t <= four) {
            h.wrist = h.wrist_add;
            h.finger = up;
            h.hand_turn = f(0);
        } else {
            const F u = (t - four) * f(5);
            h.finger = turned_towards(up, rim_tangent(p, f(0), f(0), f(1)), nullptr, u);
            h.hand_turn = u;
            h.wrist = (f(1) - u) * h.wrist_add;
        }
        h.palm = rim_palm(p, h.side, f(0), f(1));
    } else if (four <= x && x <= f(1)) {
        angle = mixed() * ((x - four) / f(0.200000048f));
        grip(h, angle, p);
        h.hand_turn = f(1);
        h.wrist = f(0);
    }
    return past_side(h, angle);
}

} // namespace

bool shifts(const Places& p, int side) { return (p.shift_hand == 1 && side == 0) || (p.shift_hand == 2 && side != 0); }

void easy_hand(HandSolve& h, const Places& p) {
    h.pos = p.wheel;
    h.pos.x = h.side == 0 ? h.pos.x + p.wheel_radius : h.pos.x - p.wheel_radius;
    h.raised = 1;
}

void fixed_hand(HandSolve& h, const Places& p) {
    const F d = f(0.707106769f);
    h.pos = p.grip[h.side];
    h.palm = rotate(p.turn, p.grip_axis[h.side]);
    h.finger = rotate(p.turn, {h.side ? -d : d, f(0), d});
    h.hand_turn = f(1);
    h.grip_side = f(0.5f);
    h.raised = 0;
}

void free_hand(HandSolve& h, const Places& p) {
    const F x = f(0.049999997f), out = f(0.866025388f);
    h.pos = p.seat + V3{h.side ? -x : x, f(0.199999988f), f(0.299999982f)};
    h.palm = rotate(p.turn, -axis_y());
    h.finger = rotate(p.turn, {h.side ? -out : out, f(0), f(0.5f)});
    h.hand_turn = f(1);
    h.grip_side = f(0.5f);
    h.raised = 0;
}

int single_hand(HandSolve& h, F from, F to, const Places& p, F blend, F phase) {
    const F x = phase < f(0) ? -phase : phase;
    const F away = f(0.5f) < x ? (f(1) - x) + (f(1) - x) : x + x;
    const F part = h.weight[0] * clamp(away, f(0), f(1));
    if (f(0) < x && x <= f(1) && shifts(p, h.side)) {
        const int r = to_lever(h, from, to, p, blend, x);
        h.grip_side = f(0.5f) - (part / f(kPi));
        return r;
    }
    return on_rim(h, from, to, p, blend);
}

} // namespace gt4r::race::posture
