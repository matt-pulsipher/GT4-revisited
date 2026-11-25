// Gran Turismo 4's own routines of the race step: tire wear, the pit flags, the flaps and brake heat
// (rules_wear.h).

#include "gt4r/sim/rules_wear.h"

#include "gt4r/sim/program.h"

#include <cstring>

namespace gt4r::sim::rules {

namespace {
F rd(const u8* b, u32 o) {
    u32 v;
    std::memcpy(&v, b + o, 4);
    return F::raw(v);
}
const F kOne = F::raw(0x3F800000);
const F kLevelHi = F::raw(0x427F9999); // 63.9
const F kLevelLo = F::raw(0x41FF3333); // 31.9
} // namespace

WearRules wear_rules(const u8* b) {
    WearRules k;
    std::memcpy(k.raw.data(), b, 0x6C);
    k.tread_neg = rd(b, 0x00), k.tread_neg_loss = rd(b, 0x04), k.tread_knee1 = rd(b, 0x08), k.tread_loss1 = rd(b, 0x0C);
    k.tread_knee2 = rd(b, 0x10), k.tread_loss2 = rd(b, 0x14), k.tread_max = rd(b, 0x18), k.tread_max_loss = rd(b, 0x1C);
    k.tread_pit = rd(b, 0x20), k.tread_cap = rd(b, 0x24), k.tread_skid = rd(b, 0x28), k.tread_skid_slip = rd(b, 0x2C);
    k.second_rate = rd(b, 0x30), k.second_drive = rd(b, 0x34), k.second_brake = rd(b, 0x38), k.second_lateral = rd(b, 0x3C);
    k.second_slide = rd(b, 0x40), k.second_fall_max = rd(b, 0x44), k.second_fall_neg = rd(b, 0x48), k.second_force = rd(b, 0x4C);
    k.second_neg = rd(b, 0x50), k.second_neg_loss = rd(b, 0x54), k.second_knee = rd(b, 0x58), k.second_loss = rd(b, 0x5C);
    k.second_max = rd(b, 0x60), k.second_max_loss = rd(b, 0x64);
    k.tread_on = b[0x68], k.second_on = b[0x69];
    return k;
}

bool wear_grip(ex::Car& c, WearExtra& x, const TireExtra& t, const WheelLayout& l, const WearRules& k) {
    using gt3r::ee::to_int;
    if (!k.tread_on && !k.second_on) return false;
    if (c.x535 & 0x10) return false; // on the pit lane
    const int n = l.count < 4 ? l.count : 4;
    if (k.tread_on) {
        for (int i = 0; i < n; ++i) {
            ex::Wheel& w = c.w[size_t(i)];
            const F e = w.wear;
            if (k.tread_max <= e) {
                w.wear = k.tread_max;
                w.wear_grip = kOne - k.tread_max_loss;
                if (!k.second_on) w.wear_level = 0x7F;
            } else if (k.tread_knee2 <= e) {
                const F u = (e - k.tread_knee2) / (k.tread_max - k.tread_knee2);
                w.wear_grip = (kOne - k.tread_loss2) - u * (k.tread_max_loss - k.tread_loss2);
                if (!k.second_on) w.wear_level = s8(kOne <= u ? 0x7F : to_int(u * kLevelHi) + 0x40);
            } else if (k.tread_knee1 <= e) {
                const F u = (e - k.tread_knee1) / (k.tread_knee2 - k.tread_knee1);
                w.wear_grip = (kOne - k.tread_loss1) - u * (k.tread_loss2 - k.tread_loss1);
                if (!k.second_on) w.wear_level = s8(kOne <= u ? 0x40 : to_int(u * kLevelLo) + 0x20);
            } else if (F() <= e) {
                const F u = e / k.tread_knee1;
                w.wear_grip = kOne - k.tread_loss1 * u;
                if (!k.second_on) w.wear_level = s8(kOne <= u ? 0x20 : to_int(u * kLevelLo));
            } else if (k.tread_neg <= e) {
                if (k.second_on) w.wear_grip = kOne;
                else {
                    const F u = e / k.tread_neg;
                    w.wear_grip = kOne - k.tread_neg_loss * u;
                    w.wear_level = s8(kOne <= u ? -0x3F : -to_int(u * kLevelHi));
                }
            } else {
                w.wear_grip = kOne;
                if (!k.second_on) w.wear_level = -0x80;
            }
        }
    }
    if (k.second_on) {
        for (int i = 0; i < n; ++i) {
            ex::Wheel& w = c.w[size_t(i)];
            const F e = x.second[size_t(i)];
            F g = kOne;
            s32 level;
            if (k.second_knee < e) {
                F u = (e - k.second_knee) / (k.second_max - k.second_knee);
                if (kOne < u) u = kOne;
                level = to_int(u * kLevelHi) + 0x40;
                g = (kOne - k.second_loss) - (k.second_max_loss - k.second_loss) * u;
            } else if (F() <= e) {
                const F u = e / k.second_knee;
                level = to_int(u * kLevelHi);
                g = kOne - k.second_loss * u;
            } else if (k.second_neg <= e) {
                const F u = e / k.second_neg;
                level = -to_int(u * kLevelHi);
                g = kOne - k.second_neg_loss * u;
            } else {
                level = -0x80;
            }
            w.wear_level = s8(level);
            w.wear_grip = k.tread_on ? w.wear_grip * g : g;
        }
    }
    for (int i = 0; i < n; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        const F floor = tire_record(t, l, i, w.surface).wear_floor;
        if (w.wear_grip < floor) w.wear_grip = floor;
    }
    return true;
}

void tire_wear(ex::Car& c, WearExtra& x, const TireExtra& t, const WheelLayout& l, const WearRules& k) {
    using gt3r::ee::sqrt;
    using gt3r::ee::to_int;
    if (x.category == 4) return;
    const int n = l.count < 4 ? l.count : 4;
    const F dt = c.dt, zero = F();
    if (k.second_on) {
        for (int i = 0; i < n; ++i) {
            ex::Wheel& w = c.w[size_t(i)];
            const u8 a = l.axle[size_t(i)] & 1;
            const F lon = w.fx_drive * k.second_drive + w.fx_brake * k.second_brake;
            const F lat = w.force_a * k.second_lateral;
            const F scale = k.second_force / t.width[a];
            F d = sqrt(lat * lat + lon * lon) * scale;
            if (w.surface == 0) d = d + w.slip_intensity * k.second_slide;
            const F e = x.second[size_t(i)];
            const F fall = (k.second_fall_max - k.second_fall_neg) * (e - k.second_neg) / (k.second_max - k.second_neg) + k.second_fall_neg;
            d = d - fall;
            F v = e + d * dt * k.second_rate;
            if (v < k.second_neg) v = k.second_neg;
            else if (k.second_max < v) v = k.second_max;
            x.second[size_t(i)] = v;
        }
    }
    if (!k.tread_on) return;
    for (int i = 0; i < n; ++i) x.hud[size_t(i)] = 0;
    if (c.x535 & 0x10) return; // on the pit lane
    const F one = kOne, full = F::raw(0x437F0000);
    for (int i = 0; i < n; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        const u8 a = l.axle[size_t(i)] & 1;
        if (w.surface == 1 || w.load == zero) continue;
        F load = c.p.wear_load_offset[a] + w.load * c.p.wear_load_gain[a];
        if (load < zero) load = zero;
        const Tire& r = tire_record(t, l, i, w.surface);
        F m = w.slip_intensity;
        if (zero < k.tread_skid && (w.flags & 0xC)) {
            const F s = w.slip_ratio;
            if (s <= k.tread_skid_slip) m = m * (k.tread_skid + one);
            else if (s < zero) m = m * ((k.tread_skid + one) * s / k.tread_skid_slip);
        }
        if (k.tread_cap < m) m = k.tread_cap;
        m = m * r.wear_rate;
        w.wear = w.wear + m * load * dt;
        F level = m * full;
        if (full < level) level = full;
        x.hud[size_t(i)] = u8(to_int(level));
    }
}

void brake_heat(ex::Car& c, const WheelLayout& l, const ex::BrakeHeatConsts& k, const std::array<F, 2>& radius) {
    const F dt = c.dt;
    const F zero = F::raw(0), one = F::raw(0x3F800000);
    for (int w = 0; w < l.count && w < 4; ++w) {
        ex::Wheel& wh = c.w[size_t(w)];
        const size_t a = l.axle[size_t(w)] & 1;
        F v = wh.rim_speed;
        const F torque = c.p.brake_torque[a] * radius[a];
        if (v < zero) v = -v;
        F heat = ((torque * wh.brake) * v) * k.c0;
        if (c.brake_pedal == zero) heat = zero;
        const F cool = ((v + k.c2) * k.c1) * (wh.brake_heat + k.c3);
        F h = wh.brake_heat + (heat - cool) * dt;
        if (one < h) h = one;
        else if (h < zero) h = zero;
        wh.brake_heat = h;
    }
}

u32 random_next(u32& state) {
    const u32 v = state * 17 + 0x11;
    state = v;
    return v ^ ((v << 16) | (v >> 16));
}

F random_float(u32& state, F scale) {
    u32 a = random_next(state);
    if (a != 0) {
        // (the EE's PLZCW: the bits after the top one that equal it)
        const u32 top = a >> 31;
        u32 n = 0;
        for (int b = 30; b >= 0 && ((a >> b) & 1) == top; --b) ++n;
        s32 k = s32(n & 0x1F);
        if (top) k = -1;
        const u32 e = u32(0x7D - k);
        const u32 shift = (0u - e) & 31;
        a = (e << 23) | ((a << shift) >> 9);
    }
    return F::raw(a) * scale;
}

void flaps(ex::Car& c, const WheelLayout& l, u32& random_state) {
    const F minus = F::raw(0xBF800000), sixtieth = F::raw(0x3C888888);
    for (u32 i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[i];
        F x = w.flap, v = w.flap_v;
        F reach = w.hub_vx * F::raw(0x3DCCCCCC);
        if (reach < F()) reach = -reach;
        reach = F::raw(0x40000000) < reach ? F::raw(0x40000000) + minus : reach + minus;
        const F r = random_float(random_state, kOne);
        F a = (reach - minus) * r;
        a = a + a;
        a = a + minus;
        a = a - x;
        a = a * F::raw(0x43FA0000);
        a = a - v * F::raw(0x41200000);
        a = a * sixtieth;
        v = v + a;
        x = x + v * sixtieth;
        if (kOne < x) x = kOne, v = F();
        else if (x < minus) x = minus, v = F();
        w.flap = x, w.flap_v = v;
    }
}

void event_wear_bytes(const u8* row, u8* r) {
    for (u32 k = 0; k < 5; ++k) r[0x2C + k] = row[0x6B + k];
    r[0x33] = row[0x70], r[0x34] = row[0x71];
    r[0x31] = row[0x72], r[0x32] = row[0x73];
    if (row[0x6B] != 0 && row[0x72] == 0) r[0x31] = 4;
}

std::array<u8, 0x6C> wear_rules_block(const u8* r, bool second_on, const Program& program) {
    using gt3r::ee::int_to_f;
    std::array<u8, 0x6C> b{};
    auto put = [&](u32 o, F v) { std::memcpy(&b[o], &v.bits, 4); };
    const F milli = F::raw(0x3A83126E), centi = F::raw(0x3C23D70A), tenk = F::raw(0x461C4000), hundred = F::raw(0x42C80000);
    if (second_on && program.b(0x620B60) != 0) {
        for (u32 k = 0; k < 7; ++k) put(0x30 + 4 * k, int_to_f(s16(program.h(0x620B62 + 2 * k))) * milli);
        put(0x4C, (int_to_f(s16(program.h(0x620B70))) * milli) * F::raw(0x38D5FEC0));
        put(0x50, int_to_f(program.b(0x620B72)) * F::raw(0xBC23D70A));
        put(0x54, int_to_f(program.b(0x620B73)) * centi);
        put(0x58, int_to_f(program.b(0x620B74)) * centi);
        put(0x60, kOne);
        put(0x5C, int_to_f(program.b(0x620B75)) * centi);
        b[0x69] = 1;
        put(0x64, int_to_f(program.b(0x620B76)) * centi);
    }
    const u8 k0 = r[0x2C], k1 = r[0x2D], k2 = r[0x2E], n0 = r[0x2F], n1 = r[0x30], a0 = r[0x31], a1 = r[0x32], m0 = r[0x33], m1 = r[0x34];
    if (k0 != 0 && m0 < k0 && m1 < k1 && a0 < m0 && a1 < m1 && k2 < k1) {
        put(0x18, int_to_f(k0) * tenk);
        put(0x1C, int_to_f(k1) * centi);
        put(0x20, int_to_f(100 - s32(k2)) * centi);
        if (b[0x69] == 0) put(0x00, (-int_to_f(n0)) * tenk), put(0x04, int_to_f(n1) * centi);
        put(0x08, int_to_f(a0) * tenk), put(0x0C, int_to_f(a1) * centi);
        put(0x10, int_to_f(m0) * tenk), put(0x14, int_to_f(m1) * centi);
        const u32 S = Program::kStatic;
        put(0x24, int_to_f(program.b(S + 0xBE)) / hundred);
        put(0x28, int_to_f(program.b(S + 0xC2)) / hundred);
        b[0x68] = 1;
        put(0x2C, (-int_to_f(program.b(S + 0xC3))) / hundred);
    }
    return b;
}

void new_tire(ex::Car& c, WearExtra& x, int i, const WearRules& k) {
    ex::Wheel& w = c.w[size_t(i)];
    w.wear_grip = kOne, w.wear = F(), w.wear_level = 0, w.brake_heat = F(), x.second[size_t(i)] = F();
    x.hud[size_t(i)] = 0;
    if (k.tread_on) {
        w.wear = k.tread_neg;
        if (!k.second_on) w.wear_level = w.wear < F() ? -0x3F : 0;
        w.wear_grip = kOne - k.tread_neg_loss;
    }
    if (k.second_on) {
        x.second[size_t(i)] = k.second_neg;
        w.wear_level = k.second_neg < F() ? -0x3F : 0;
        w.wear_grip = w.wear_grip * (kOne - k.second_neg_loss);
    }
    if (x.category == 4) w.wear_grip = kOne, w.wear = F(), x.second[size_t(i)] = F(), w.wear_level = 0;
}

} // namespace gt4r::sim::rules
