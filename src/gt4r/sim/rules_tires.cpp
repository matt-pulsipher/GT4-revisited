// Gran Turismo 4's own routines of the race step: the tires (rules_tires.h).

#include "gt4r/sim/rules_tires.h"

namespace gt4r::sim::rules {

namespace {
size_t axle_of(const WheelLayout& l, int w) { return w < l.count ? l.axle[size_t(w)] & 1u : 0u; }
} // namespace

const Tire& tire_record(const TireExtra& x, const WheelLayout& l, int w, u8 surface) {
    return x.tire[x.tire_index[axle_of(l, w)][surface % 24] % 6];
}

F load_per_width(const TireExtra& x, const WheelLayout& l, int w, F load) { return load / x.width[axle_of(l, w)]; }

void peak_force(ex::Car& c, TireExtra& x, const WheelLayout& l, bool wear, F driver_grip) {
    const F d = c.roll - c.ground_roll;
    for (int i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        const Tire& r = tire_record(x, l, i, w.surface);
        const size_t a = axle_of(l, i);
        x.wheel_xdd[size_t(i)] = r.x1da;
        F f = w.load * table(r.load_curve, load_per_width(x, l, i, w.load));
        if (wear) f = f * w.wear_grip;
        f = f * x.axle_grip[a];
        const F camber = l.kind == 1 ? F() : (l.side[size_t(i)] ? d : -d) + w.camber;
        const F g = table(x.camber_grip[a], camber);
        w.peak_force = f * r.mu[w.surface % 24] * g;
        w.peak_force = w.peak_force * driver_grip;
    }
}

void slip_angles(ex::Car& c, const WheelLayout& l) {
    for (int i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        w.slip_dir = ex::slip_angle(c.behaviour, w.hub_vx, w.hub_vy, w.slip_fade);
    }
}

} // namespace gt4r::sim::rules
