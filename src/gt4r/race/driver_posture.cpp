#include "gt4r/race/driver_posture.h"

#include "gt4r/formats/model/car_model.h"

#include <cstring>

namespace gt4r::race::posture {

namespace ee = gt3r::ee;

FigureClass figure_class(int kind) {
    if (kind == 0) return FigureClass::None;
    if (kind == 0x10 || kind == 0x11) return FigureClass::Torso;
    if (kind == 0x20 || kind == 0x21) return FigureClass::Board;
    return FigureClass::Whole;
}

namespace {

F read_f(ByteSpan d, size_t o) { return F::raw(rd32(d, o)); }
V3 read_v(ByteSpan d, size_t o) { return {read_f(d, o), read_f(d, o + 4), read_f(d, o + 8)}; }

// The solver's frame: the car's with x and z negated.
V3 turned_about(V3 v) { return {-v.x, v.y, -v.z}; }

// The monster-truck-like car's own places (MTRGeometry), for the rider and the one behind.
void mtr_cockpit(Cockpit& c, bool second) {
    const auto v = [](float x, float y, float z) { return V3{f(x), f(y), f(z)}; };
    c.seat = second ? v(0, 0.250999987f, -0.497999996f) : v(0, 0.280999988f, 0.289999992f);
    const float px = second ? 0.152999997f : 0.0709999949f, py = second ? -0.0319999978f : 0.0229999982f, pz = second ? -0.520999968f : -1.25699997f;
    c.pedal[0] = v(-px, py, pz), c.pedal[1] = v(px, py, pz);
    const float wy = second ? 0.542999983f : 0.550999999f, wz = second ? -0.0239999983f : -0.813999951f;
    c.wheel = v(0, wy, wz);
    c.column = v(0, 0.246846989f, 0.969054997f);
    c.wheel_radius = f(0.189999998f);
    c.lever = c.place_6c = v(-0.220999986f, wy, wz);
    c.recline = f(second ? -0.367999971f : -0.242999986f);
    c.lever_scale = c.scale_78 = f(0.121299997f);
    c.handlebars = true;
    c.no_heave = true;
    const float gx = second ? -0.205999985f : -0.221599966f, gy = second ? 0.492499948f : 0.438399941f, gz = second ? -0.0229999982f : -0.867949843f;
    c.grip[0] = v(gx, gy, gz), c.grip[1] = v(-gx, gy, gz);
    const float ax = second ? -0.254183978f : -0.354090989f, ay = second ? -0.925774992f : -0.916163981f, az = second ? -0.27987799f : 0.187785f;
    c.grip_axis[0] = v(ax, ay, az), c.grip_axis[1] = v(-ax, ay, az);
}

// Kind 0x0A's (SpecialCarGeometry).
void special_cockpit(Cockpit& c) {
    const auto v = [](float x, float y, float z) { return V3{f(x), f(y), f(z)}; };
    c.seat = v(0, 0.375f, 0.099999994f);
    c.pedal[0] = v(-0.224999994f, 0.202999994f, 0.289999992f), c.pedal[1] = v(0.224999994f, 0.202999994f, 0.289999992f);
    c.wheel = v(0, 1.051f, -0.813999951f);
    c.column = v(0, 0.246846989f, 0.969054997f);
    c.wheel_radius = f(0.189999998f);
    c.lever = c.place_6c = v(-0.220999986f, 0.550999999f, -0.813999951f);
    c.recline = f(1.08155537f);
    c.handlebars = true;
}

} // namespace

Cockpit read_posture_cockpit(ByteSpan file, bool co_driver) {
    const car::CarFile car = car::CarFile::parse(file);
    if (!car.info || car.info + 0x58 > file.size()) throw FormatError("car file without an info block");
    const u32 parts = rd32(file, car.info + 0x50);
    if (!parts || parts + 0x100 > file.size()) throw FormatError("car file without a parts block");
    Cockpit c;
    c.kind = file[car.info + 2];
    c.wheel = read_v(file, parts);
    c.column = read_v(file, parts + 0x0C);
    c.wheel_radius = read_f(file, parts + 0x18);
    c.pedal[0] = read_v(file, parts + 0x1C);
    c.pedal[1] = read_v(file, parts + 0x28);
    c.seat = read_v(file, parts + 0x34);
    c.recline = read_f(file, parts + 0x40);
    c.lever = read_v(file, parts + 0x4C);
    c.lever_pivot = read_v(file, parts + 0x58);
    c.lever_scale = read_f(file, parts + 0x64);
    c.place_6c = read_v(file, parts + 0x6C);
    c.scale_78 = read_f(file, parts + 0x78);
    c.place_7c = read_v(file, parts + 0x7C);
    if (c.kind == 0x0A) {
        special_cockpit(c);
    } else if (c.kind == 0x0F) {
        mtr_cockpit(c, co_driver);
    } else if (co_driver) { // moved to the other seat (0x38CDE8)
        const V3 other = read_v(file, parts + 0xC4);
        const V3 move = other.x == f(0) && other.y == f(0) && other.z == f(0) ? V3{-c.seat.x + -c.seat.x, f(0), f(0)} : other - c.seat;
        c.wheel = c.wheel + move;
        c.pedal[0] = c.pedal[0] + move;
        c.pedal[1] = c.pedal[1] + move;
        c.seat = c.seat + move;
    }
    c.lever_kind = rd16(file, parts + 0x6A);
    if (c.kind == 0x0A) c.style = 1;
    else if (c.kind == 0x0F) c.style = 2;
    else if (c.lever_kind >= 3 && c.lever_kind <= 5) c.style = c.lever_kind;
    return c;
}

DriverRecord blend_records(const DriverRecord& a, const DriverRecord& b, F t) {
    if (t == f(0)) return a;
    if (t == f(1)) return b;
    DriverRecord r;
    const F one = f(1), zero = f(0), rest = one - t;
    for (size_t i = 0; i < r.v.size(); ++i) r.v[i] = (a.v[i] * rest) + (b.v[i] * t);
    // Values that went round, and a hand that took a new grip.
    const F ra = clamp(a.at(0x1C), zero, one), rb = clamp(b.at(0x1C), zero, one);
    if (f(0.799999952f) < rb - ra) {
        const F v = (rb * t) + ((ra + one) * rest);
        r.at(0x1C) = v - ee::int_to_f(ee::to_int(v));
    }
    for (u32 k = 0; k < 2; ++k) {
        const u32 from = 0x48 + 4 * k, to = 0x40 + 4 * k, part = 0x50 + 4 * k;
        if (!(a.at(from) == a.at(to)) || b.at(from) == b.at(to)) continue;
        r.at(from) = b.at(from);
        r.at(to) = a.at(to);
        r.at(part) = clamp((clamp(b.at(part), zero, one) * t) + rest, zero, one);
    }
    const F pa = a.at(0xE0), pb = b.at(0xE0);
    if (f(0.5f) < pb - pa || f(0.5f) < pa - pb) {
        const s32 whole_b = ee::to_int(pb);
        const F whole_a = ee::int_to_f(ee::to_int(pa));
        const F v = (pb * t) + ((ee::int_to_f(whole_b) + (pa - whole_a)) * rest);
        const s32 n = ee::to_int(v);
        r.at(0xE0) = n == whole_b ? v : whole_a + (v - ee::int_to_f(n));
    }
    return r;
}

namespace {

// The places both postures share. Returns which hand changes gear (an index, 1 right).
int set_places(Places& p, const Cockpit& c, V3 seat_move) {
    for (int k = 0; k < 2; ++k) p.foot[k] = turned_about(c.pedal[k]);
    p.seat = turned_about(c.seat + seat_move);
    p.wheel = turned_about(c.wheel);
    set_column(p, turned_about(c.column));
    p.lever[0] = turned_about(c.lever);
    p.wheel_radius = c.wheel_radius;
    int hand = 0;
    if (c.handlebars) {
        p.handlebars = 1;
        for (int k = 0; k < 2; ++k) p.grip[k] = turned_about(c.grip[k]), p.grip_axis[k] = turned_about(c.grip_axis[k]);
        p.shift_hand = 0;
    } else if (c.wheel.x < c.lever.x) {
        p.shift_hand = 2;
        hand = 1;
    } else {
        p.shift_hand = 1;
    }
    p.steer_high = f(kHalfPi);
    p.steer_low = c.recline;
    return hand;
}

constexpr Quat half_turn() { return {F::raw(0), F::raw(0x3F800000), F::raw(0), F::raw(0)}; } // about y

} // namespace

PostureInput still_posture(const Cockpit& c) {
    PostureInput in;
    Controls& k = in.controls;
    Places& p = in.places;
    p.turn = mul(identity(), half_turn());
    const int hand = set_places(p, c, V3{});
    p.pos = V3{};
    k.wheel_angle = f(0);
    const F a = p.steer_low + f(0);
    k.recline = k.fixed_recline ? a : clamp(a, k.recline_min, k.recline_max);
    k.phase_steer[0] = f(0);
    k.elbow[hand] = k.elbow[!hand] = clamp(f(0.199999988f), f(0), f(1));
    in.gaze.kind = 1;
    in.gaze.at = {c.seat.x, c.seat.y, c.seat.z - f(5)};
    return in;
}

namespace {

// What a lever's turn does to the hand on it (0x3B1DB0's block and the three after it).
struct LeverMotion {
    F along{}, across{}; // +0x0C, +0x10: its two turns
    F foot[2]{};         // +0x58, +0x5C
};

LeverMotion lever_motion(int style, const DriverRecord& r) { // 0x3B1DB0
    LeverMotion m;
    if (style == 3) {
        m.across = r.at(0x24);
    } else if (style == 4 || style == 5) {
        m.along = r.at(0x24);
    } else {
        m.across = (r.at(0x20) * f(3.13999987f)) * f(0.0625f);
        m.along = (r.at(0x24) * f(3.13999987f)) * f(0.0625f);
    }
    if (style == 1) {
        m.foot[0] = m.foot[1] = r.at(0x14);
        const F w = r.at(0x34), zero = f(0), one = f(1);
        if (w < zero) m.foot[0] = m.foot[0] * (one - (clamp(-w / f(kHalfPi), zero, one) * f(0.5f)));
        else m.foot[1] = m.foot[1] * (one - (clamp(w / f(kHalfPi), zero, one) * f(0.5f)));
    } else if (style == 3) {
        m.foot[0] = r.at(0xD8);
        m.foot[1] = r.at(0xC8);
    }
    return m;
}

V3 lever_offset(const Cockpit& c, const LeverMotion& m) { // 0x3B21D8
    F s1, c1, s0, c0;
    ee::sincos(m.across, s1, c1);
    const F x = -c.lever_pivot.x, y = c.lever_pivot.y, z = -c.lever_pivot.z;
    const F w = (x * s1) + (y * c1);
    ee::sincos(m.along, s0, c0);
    return {(((x * c1) - (y * s1)) + c.lever_pivot.x) * c.lever_scale, (((w * c0) - (z * s0)) - c.lever_pivot.y) * c.lever_scale,
            (((w * s0) + (z * c0)) + c.lever_pivot.z) * c.lever_scale};
}

V3 column_lever_offset(const Cockpit& c, const LeverMotion& m) { // 0x3B1FA8
    const Quat q = axis_angle(turned_about(c.column), m.along);
    const V3 v = rotate(q, {-c.lever_pivot.x, c.lever_pivot.y, -c.lever_pivot.z});
    return {(v.x + c.lever_pivot.x) * c.lever_scale, (v.y - c.lever_pivot.y) * c.lever_scale, (v.z + c.lever_pivot.z) * c.lever_scale};
}

V3 second_lever_offset(const Cockpit& c, F angle) { // 0x3B2370
    F s, k;
    ee::sincos(angle, s, k);
    const F z = -c.place_7c.z;
    return {(-c.place_7c.x + c.place_7c.x) * c.scale_78, (((c.place_7c.y * k) - (z * s)) - c.place_7c.y) * c.scale_78,
            (((c.place_7c.y * s) + (z * k)) + c.place_7c.z) * c.scale_78};
}

V3 third_lever_offset(F angle) { // 0x3B2440
    F s, k;
    ee::sincos(angle, s, k);
    const F y = f(-0.972279966f), z = f(-0.197115988f), scale = f(0.826922953f);
    return {f(0), (((k * y) - (s * z)) - y) * scale, (((s * y) + (k * z)) + f(0.197115988f)) * scale};
}

// A curve through points 1 / spacing apart (0x4860E8 on the table of a kind 0x0A car's pedaling, 0x6A2478): the lying
// driver's heave.
F pedaling(F x) {
    static constexpr float kTable[10] = {0, 0, 0.2f, 0.8f, 0.25f, 1, 0.8f, 0.4f, 1, 0.2f};
    const float* k = kTable + (x < f(kTable[4]) ? 0 : 4);
    const F t = (x - f(k[0])) / (f(k[4]) - f(k[0]));
    const F a = f(k[1]), b = f(k[2]), e = f(k[3]), d = f(k[5]);
    const F p = (b - e) * f(3), q = (a - b) * f(3);
    return (((((((d - a) + p) * t) + (q - p)) * t) - q) * t) + a;
}

} // namespace

PostureInput driving_posture(const Cockpit& c, const DriverRecord& r, V3 pos, V3 angles, bool co_driver) {
    const F zero = f(0), one = f(1);
    PostureInput in;
    Controls& k = in.controls;
    Places& p = in.places;
    const FigureClass cls = figure_class(c.kind);
    bool move_lever = true, move_feet = true;
    if (cls == FigureClass::Board) {
        k.no_twist = 1;
        k.arms = 0;
        k.legs = 0;
        move_lever = move_feet = false;
    } else if (cls == FigureClass::Torso) {
        k.legs = 0;
        k.no_twist = 1;
        k.plain_hands = 1;
        move_feet = false;
    }
    if (co_driver) k.free_hands = 1;
    const int style = c.style;
    F seat_up = zero, seat_back = zero, lean_more = zero;
    if (style == 1) {
        set_reclined(k);
        seat_up = (r.at(0x14) * f(0.0124999993f)) + zero;
        const F most = f(0.314159214f), least = -most;
        const F inv = one / (most - least);
        const F slope = inv * f(-0.199999988f), at = inv * ((most * f(0.099999994f)) - (least * f(-0.099999994f)));
        seat_back = ((r.at(0x14) * f(-0.049999997f)) + zero) + clamp((slope * r.at(0x0C)) + at, f(-0.099999994f), f(0.099999994f));
    }
    if (style == 3) {
        k.grip_kind = 1;
        lean_more = r.at(0x9C) + zero;
        k.recline_max = f(kHalfPi);
    } else if (style == 4) {
        k.grip_kind = 2;
        lean_more = zero + r.at(0x9C);
    }
    const LeverMotion motion = lever_motion(style, r);
    F chest_up = zero;
    const F heave = r.at(0x10), little = f(0.00999999978f);
    if (little < heave) {
        chest_up = little;
        seat_up = seat_up + (heave - little);
    } else {
        chest_up = heave + chest_up;
    }
    k.chest_offset_world = {zero, chest_up, zero};
    const int hand = set_places(p, c, V3{zero, seat_up, seat_back});
    p.lever[1] = turned_about(c.place_6c);
    p.lever[2] = style == 3 ? turned_about({f(0.512999952f), f(1.02199996f), f(-0.104999997f)}) : turned_about(c.place_6c);
    if (!c.handlebars) {
        k.wrist_phase[0][hand] = r.at(0xB4);
        k.wrist_phase[1][hand] = r.at(0xCC);
        k.wrist_phase[2][hand] = r.at(0xDC);
    }
    const Quat car_turn = from_euler(angles.x, angles.y, angles.z);
    p.pos = pos;
    p.turn = mul(car_turn, half_turn());
    p.gesture = r.at(0xE0);
    k.phase = r.at(0xE0);
    k.wheel_angle = r.at(0x34);
    for (u32 n = 0; n < 2; ++n) {
        k.grip_from[n] = r.at(0x48 + 4 * n);
        k.grip_to[n] = r.at(0x40 + 4 * n);
        k.grip_blend[n] = clamp(r.at(0x50 + 4 * n), zero, one);
        k.grip_out[n] = r.at(0x70 + 4 * n);
        k.grip_back[n] = r.at(0x78 + 4 * n);
        if (style == 5) k.grip_out[n] = k.grip_out[n] * r.at(0xD0), k.grip_back[n] = k.grip_back[n] * r.at(0xD0);
        k.wrist_base[n] = r.at(0xAC + 4 * n);
        k.knee[n] = clamp(r.at(0xE4 + 4 * n), zero, one);
        k.clavicle_forward_base[n] = r.at(0x8C + 4 * n);
        k.gesture_grip[0][n] = r.at(0x28 + 4 * n);
    }
    if (style == 5) k.raise_choice[1] = 1;
    const F lean = r.at(0x08);
    {
        F a, twist = (lean * f(0.199999988f)) + r.at(0x88);
        if (style != 1) {
            a = p.steer_low + (lean_more + r.at(0x0C));
        } else {
            a = p.steer_low + (r.at(0x0C) * f(0.099999994f));
            twist = twist * f(0.099999994f);
        }
        k.recline = k.fixed_recline ? a : clamp(a, k.recline_min, k.recline_max);
        k.raw_lean = 1;
        k.lean = clamp(lean, f(-0.785398126f), f(0.785398126f));
        k.twist = clamp(twist, f(-0.942477643f), f(0.942477643f));
    }
    k.pelvis_roll = clamp(r.at(0xA8), f(-0.314159214f), f(0.314159214f));
    k.spine_length = r.at(0xB8);
    k.gaze_twist = r.at(0xBC);
    // The gear change.
    const F steer[3] = {r.at(0x1C), r.at(0xC0), r.at(0xD0)};
    const F ph = r.at(0xE0);
    F busy = zero;
    if (ph <= one) {
        busy = ((one - ph) * steer[0]) + (ph * steer[1]);
    } else if (ph <= f(2)) {
        const F q = ph - one;
        busy = ((one - q) * steer[1]) + (q * steer[2]);
    } else if (ph <= f(3)) {
        const F q = ph - f(2);
        busy = ((one - q) * steer[2]) + (q * steer[0]);
    }
    F elbow = f(0.199999988f), other = f(0.199999988f);
    if (!co_driver && !c.handlebars) {
        elbow = hand == 0 ? r.at(0x38) : r.at(0x3C);
        other = hand != 0 ? r.at(0x38) : r.at(0x3C);
        if (move_lever) {
            k.gesture_offset[0] = style == 5 ? column_lever_offset(c, motion) : lever_offset(c, motion);
            if (style == 3) {
                k.gesture_offset[1] = second_lever_offset(c, r.at(0xC4));
                k.gesture_offset[2] = third_lever_offset(r.at(0xD4));
            }
            if (busy < f(0.5f)) {
                const F t = busy + busy;
                elbow = (clamp((f(0.900000036f) * busy) + f(0.299999982f), f(0.299999982f), f(0.75f)) * t) + (elbow * (one - t));
            } else {
                const F u = one - busy;
                const F t = u + u;
                elbow = (clamp((f(-0.900000036f) * busy) + f(1.19999993f), f(0.299999982f), f(0.75f)) * t) + (elbow * (one - t));
            }
        }
    }
    for (int n = 0; n < 3; ++n) k.phase_steer[n] = clamp(steer[n], zero, one);
    k.elbow[hand] = clamp(elbow, zero, one);
    k.elbow[!hand] = clamp(other, zero, one);
    in.gaze.kind = 1;
    in.gaze.at = {r.at(0x58), r.at(0x5C), r.at(0x60)};
    F head_up_part = one;
    if (move_feet) {
        if (style == 1) {
            k.foot_offset[0] = {zero, motion.foot[0] * f(0.060999997f), motion.foot[0] * f(-0.466999978f)};
            k.foot_offset[1] = {zero, motion.foot[1] * f(0.060999997f), motion.foot[1] * f(-0.466999978f)};
            k.seat_offset = {zero, pedaling(r.at(0x14)) * f(0.099999994f), zero};
            head_up_part = f(0.25f);
        } else if (style != 3 && style != 4 && !c.no_heave) {
            k.foot_offset[0].y = r.at(0x30) * f(-0.0799999982f);
            k.foot_offset[1].y = ee::max(r.at(0x14), r.at(0x18)) * f(-0.0799999982f);
        }
    }
    k.head_up_part = clamp(r.at(0x64) * head_up_part, zero, one);
    k.head_pitch = ((r.at(0x04) - r.at(0x6C)) + (r.at(0x68) * f(0.099999994f))) + f(0.0872664526f);
    k.head_roll = r.at(0x00) + (r.at(0x68) * f(0.25f));
    F s, cs;
    ee::sincos(-r.at(0x68), s, cs);
    k.head_up = rotate(car_turn, {zero, cs, s});
    if (style == 1) in.has_gaze = false;
    return in;
}

} // namespace gt4r::race::posture
