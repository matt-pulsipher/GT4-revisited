// Gran Turismo 4's hub offsets (rules_wheels.h).

#include "gt4r/sim/rules_wheels.h"

namespace gt4r::sim::rules {

namespace {
F arm(const ex::Params& p, u8 g) { return g ? p.dist_rear : p.dist_front; }
} // namespace

void hub_offsets_xy(ex::Car& c, const WheelLayout& l) {
    for (u32 i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[i];
        const u8 g = l.geometry[i] & 1, side = l.side[i];
        const F ht = c.p.half_track[g];
        F x = c.lat.x * ht, y = c.lat.y * ht;
        if (side == 0) x = -x, y = -y;
        else if (side != 1) x = F(), y = F();
        w.hub.x = x, w.hub.y = y;
        const F d = arm(c.p, g);
        const F fx = c.fwd.x * d, fy = c.fwd.y * d;
        const s8 fore = s8(l.fore_aft[i]);
        if (0 < fore) w.hub.x = w.hub.x + fx, w.hub.y = w.hub.y + fy;
        else if (fore < 0) w.hub.x = w.hub.x - fx, w.hub.y = w.hub.y - fy;
    }
}

void hub_offsets_z(ex::Car& c, const WheelLayout& l) {
    for (u32 i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[i];
        const u8 g = l.geometry[i] & 1, side = l.side[i];
        F z = c.lat.z * c.p.half_track[g];
        if (side == 0) z = -z;
        else if (side != 1) z = F();
        w.hub.z = z;
        const F fz = c.fwd.z * arm(c.p, g);
        const s8 fore = s8(l.fore_aft[i]);
        if (0 < fore) w.hub.z = w.hub.z + fz;
        else if (fore < 0) w.hub.z = w.hub.z - fz;
    }
}

} // namespace gt4r::sim::rules
