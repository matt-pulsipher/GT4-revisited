// Gran Turismo 4's own routines of the race step: tire wear, the pit flags, the flaps and brake heat
// (rules_wear.h).

#include "gt4r/sim/rules_wear.h"

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
    k.tread_neg = rd(b, 0x00), k.tread_neg_loss = rd(b, 0x04), k.tread_knee1 = rd(b, 0x08), k.tread_loss1 = rd(b, 0x0C);
    k.tread_knee2 = rd(b, 0x10), k.tread_loss2 = rd(b, 0x14), k.tread_max = rd(b, 0x18), k.tread_max_loss = rd(b, 0x1C);
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

} // namespace gt4r::sim::rules
