// Gran Turismo 4's suspension and attitude (rules_body.h).

#include "gt4r/sim/rules_body.h"

#include "base/ee_math.h"
#include "sim/exact/build_math.h"
#include "gt4r/sim/program.h"


namespace gt4r::sim::rules {

namespace {

const F k0 = F::raw(0), kHalf = F::raw(0x3F000000), kOne = F::raw(0x3F800000), kMinusOne = F::raw(0xBF800000), kTwo = F::raw(0x40000000);
const F kTen = F::raw(0x41200000), kTenth = F::raw(0x3DCCCCCC);
const F kGravity = F::raw(0x411CCCCC);                                     // 9.8, a unit low
const F kPitchRate = F::raw(0x3FC90FDA), kRollRate = F::raw(0x41490FDA); // pi / 2 and 4 pi per second
const F kAngle = F::raw(0x3F490FD9);                                       // pi / 4

int count(const WheelLayout& l) { return l.count < 4 ? l.count : 4; }
// A wheel's axle, as the program reads it (0 past the layout's count).
size_t axle_of(const WheelLayout& l, int w) { return w < l.count ? l.axle[size_t(w)] : 0; }
s8 fore_aft(const WheelLayout& l, int w) { return w < l.count ? s8(l.fore_aft[size_t(w)]) : s8(0); }

// 0x3696C8: the hub's rest point under the wheel's travel at its static place: -(x_s) - (radius - drop) + offset.
F rest_drop(const ex::Car& c, const BodyExtra& x, const WheelLayout& l, int w) {
    const size_t a = axle_of(l, w);
    const ex::Suspension& s = c.p.susp[a];
    return (-s.x_static - (c.p.tire_radius[a] - x.tire_drop[a])) + s.offset;
}

// 0x3457B0: the hubs' heights from the body's axes (the engine's hub_offsets_z on the wheel layout).
void hub_heights(ex::Car& c, const WheelLayout& l) {
    for (int w = 0; w < count(l); ++w) {
        ex::Wheel& wh = c.w[size_t(w)];
        const size_t g = l.geometry[size_t(w)];
        const u8 side = l.side[size_t(w)];
        const F h = c.lat.z * c.p.half_track[g];
        if (side == 0) wh.hub.z = -h;
        else if (side == 1) wh.hub.z = h;
        else wh.hub.z = k0;
        const F d = c.fwd.z * (g ? c.p.dist_rear : c.p.dist_front);
        const s8 fa = fore_aft(l, w);
        if (fa > 0) wh.hub.z = wh.hub.z + d;
        else if (fa < 0) wh.hub.z = wh.hub.z - d;
    }
}

// 0x36A690: the wheel's half track (signed by side), shortened by its swing arm's arc at the travel.
F half_track(const ex::Car& c, const WheelLayout& l, int w, F travel) {
    const u8 side = l.side[size_t(w)];
    if (side == 2) return k0;
    const size_t g = l.geometry[size_t(w)];
    const F arm = c.p.swing_arm[g];
    F h = c.p.half_track[g];
    if (!(arm == k0)) h = h - (kOne - gt3r::ee::cos(ex::atan2(behaviour(), travel, arm))) * arm;
    return side == 0 ? -h : h;
}

// 0x36A578: the wheel's camber at its travel, with the steering's share on a wheel ahead of the centre of gravity.
F camber(const ex::Car& c, const WheelLayout& l, int w, F steer, F travel) {
    const size_t a = axle_of(l, w);
    F r = c.p.camber[a] + c.p.camber_gain[a] * travel;
    if (!(fore_aft(l, w) > 0)) return r;
    const F sc = c.p.steer_camber;
    if (sc == k0) return r;
    F t = steer * sc;
    if (l.partner[size_t(w)] != 0x7F) {
        if (k0 <= t) t = w != 0 ? -(c.p.steer_ratio[1] * t) : t * c.p.steer_ratio[0];
        else t = w != 0 ? -(c.p.steer_ratio[0] * t) : t * c.p.steer_ratio[1];
    }
    return r + t;
}

// 0x369DE8: the wheel's travel, its spring and damper and its load. `x_ground` is the travel at which it
// would stand on its ground (0x36A080); its travel moved on by its speed and its own mass's acceleration
// (held within the droop and the bump stop) more compressed than that leaves it off the ground.
void wheel(ex::Car& c, BodyExtra& x, const BodyGlobals& g, int w, size_t a, F x_ground, F k) {
    ex::Wheel& wh = c.w[size_t(w)];
    const ex::Suspension& s = c.p.susp[a];
    const AxleBody& b = x.axle[a];
    const F droop = c.hold != 0 ? b.hold_droop : s.droop;
    const F dt = c.dt;
    F free = wh.susp + (wh.damper_v + (((wh.spring_f + wh.damper_f) / b.wheel_mass) * kHalf) * dt) * dt;
    if (droop < free) free = droop;
    else if (free < s.bump_limit) free = s.bump_limit;
    if (free < x_ground) { // off the ground
        const F at = free;
        wh.spring_f = -at * k;
        if (at == droop) wh.damper_v = k0;
        else {
            F v = c.inv_dt * (at - wh.susp);
            if (kOne < v) v = kOne;
            else if (v < kMinusOne) v = kMinusOne;
            wh.damper_v = v;
        }
        wh.damper_f = damper(s, b, g, wh.damper_v, u32(a));
        wh.susp = free;
        wh.load = k0;
        return;
    }
    const F xc = x_ground < s.bump_limit ? s.bump_limit : x_ground;
    const F e = xc - s.knee;
    F fs = -xc * k;
    if (e < k0) fs = fs + (e * e) * s.k2;
    F v = c.inv_dt * (x_ground - wh.susp);
    wh.damper_v = v;
    F fd = damper(s, b, g, v, u32(a));
    if (v < k0 && e < k0) {
        if (v < kMinusOne) v = kMinusOne;
        fd = fd - s.bump_damping * v;
    }
    // (P + 0x1076, the part that changes with speed, adds 0x3F2650's term here: not ported)
    wh.susp = xc;
    wh.spring_f = fs;
    wh.damper_f = fd;
    const F load = (fs + fd) + s.preload;
    wh.load = load;
    if (load < k0) {
        wh.load = k0;
        wh.damper_f = k0;
        wh.spring_f = -s.preload;
    }
}

} // namespace

void ground_frame(ex::Car& c, const WheelLayout& l) {
    const gt3r::sim::Behaviour b = behaviour();
    F g[4] = {};
    for (int w = 0; w < count(l); ++w) g[w] = c.w[size_t(w)].ground;
    c.ground_pos.x = c.pos.x, c.ground_pos.y = c.pos.y;
    const ex::Params& p = c.p;
    switch (l.kind) {
    case 0: { // four wheels
        const F front = g[0] + g[1], rear = g[2] + g[3];
        const F u = g[0] + ((g[2] - g[0]) * p.rear_weight), v = g[1] + ((g[3] - g[1]) * p.rear_weight);
        const F wheelbase = p.wheelbase * gt3r::ee::cos(c.pitch), track = p.track * gt3r::ee::cos(c.roll);
        c.ground_pitch = ex::atan2(b, (front - rear) * kHalf, wheelbase);
        c.ground_roll = ex::atan2(b, u - v, track);
        c.ground_pos.z = (u + v) * kHalf;
        break;
    }
    case 1: { // two wheels in line: the roll from each wheel's ground normal across the body
        c.ground_pitch = ex::atan2(b, g[0] - g[1], p.wheelbase);
        ex::Vec3 fwd, lat, up;
        body_axes(c.ground_pitch, k0, c.heading, fwd, lat, up);
        F r[2];
        for (size_t w = 0; w < 2; ++w) {
            const ex::Ground& gr = c.g[w];
            const F nx = gr.normal.x, ny = -gr.normal.z, nz = gr.normal.y; // (the record's frame)
            const F d = ((nx * fwd.x) + (ny * fwd.y)) + (nz * fwd.z);
            const F ax = nx - (d * fwd.x), ay = ny - (d * fwd.y), az = nz - (d * fwd.z);
            r[w] = ex::atan2(b, ((ax * lat.x) + (ay * lat.y)) + (az * lat.z), kOne);
        }
        c.ground_roll = r[0] + ((r[1] - r[0]) * p.rear_weight);
        c.ground_pos.z = g[0] + ((g[1] - g[0]) * p.rear_weight);
        break;
    }
    case 2: { // one wheel ahead, two behind
        const F mid = (g[1] + g[2]) * kHalf;
        c.ground_pitch = ex::atan2(b, g[0] - mid, p.wheelbase);
        c.ground_pos.z = g[0] + ((mid - g[0]) * p.rear_weight);
        c.ground_roll = ex::atan2(b, g[1] - g[2], p.half_track[1] + p.half_track[1]);
        break;
    }
    case 3: { // two wheels ahead, one behind
        const F mid = (g[0] + g[1]) * kHalf;
        c.ground_pitch = ex::atan2(b, mid - g[2], p.wheelbase);
        c.ground_pos.z = mid + ((g[2] - mid) * p.rear_weight);
        c.ground_roll = ex::atan2(b, g[0] - g[1], p.half_track[0] + p.half_track[0]);
        break;
    }
    default: break;
    }
    body_axes(c.ground_pitch, c.ground_roll, c.heading, c.ground_fwd, c.ground_lat, c.ground_up);
}

void query_points(ex::Car& c, const BodyExtra& x, const WheelLayout& l) {
    for (int w = 0; w < count(l); ++w) {
        const F d = rest_drop(c, x, l, w);
        const ex::Vec3& h = c.w[size_t(w)].hub;
        // 0x369760: (position + hub) + up x d, per axis
        const F px = (c.pos.x + h.x) + (c.up.x * d), py = (c.pos.y + h.y) + (c.up.y * d), pz = (c.pos.z + h.z) + (c.up.z * d);
        ex::Ground& g = c.g[size_t(w)];
        g.x = px, g.h = pz, g.z = -py;
    }
}


BodyGlobals body_globals(const Program& program) {
    BodyGlobals g = body_globals([&](u32 a) { return program.w(a); }, [&](u32 a) { return program.b(a); });
    // The dampers' square-law table as the converter sets it from the static block (0x360A78; the image holds 0): on with
    // the byte +0x102; each axle's knees +0x105 / +0x109 x 0.01, shares +0x107 / +0x10B / 100.
    const u32 S = Program::kStatic;
    g.damper_table = program.b(S + 0x102) != 0 ? 1 : 0;
    if (g.damper_table)
        for (u32 a = 0; a < 2; ++a) {
            const F hundredth = F::raw(0x3C23D70A), hundred = F::raw(0x42C80000);
            g.bump_knee[a] = gt3r::ee::int_to_f(program.b(S + 0x105 + a)) * hundredth;
            g.bump_share[a] = gt3r::ee::int_to_f(program.b(S + 0x107 + a)) / hundred;
            g.rebound_knee[a] = gt3r::ee::int_to_f(program.b(S + 0x109 + a)) * hundredth;
            g.rebound_share[a] = gt3r::ee::int_to_f(program.b(S + 0x10B + a)) / hundred;
        }
    return g;
}

BumpTables bump_tables(const Program& program, u8 course_kind) {
    const u32 S = 0x620CD8;
    BumpTables t;
    for (u32 s = 0; s < 24; ++s) {
        const u32 r = course_kind == 2 && s == 5 ? 15 : s;
        t.amp[s] = gt3r::ee::int_to_f(program.b(S + 0x1C + r)) * F::raw(0x3A83126E);
        t.freq[s] = F::raw(0x40C90FDA) / (gt3r::ee::int_to_f(program.b(S + 0x64 + r)) * F::raw(0x3DCCCCCC));
        t.scaled[s] = program.b(S + 0x7C + r), t.mode[s] = program.b(S + 0x94 + r);
    }
    for (u32 m = 0; m < 32; ++m) t.soft[m] = program.w(kSoftMaterials + 4 * m);
    return t;
}

void road_bumps(ex::Car& c, const WheelLayout& l, const BumpTables& t, const std::array<u8, 4>& surface, const std::array<u8, 4>& material) {
    const int n = count(l);
    int on = 0, soft = 0;
    for (int w = 0; w < n; ++w) {
        if (c.g[size_t(w)].miss) continue;
        ++on;
        if (t.soft[material[size_t(w)] & 31]) ++soft;
    }
    (void)on;
    const bool all_soft = n == soft;
    for (int w = 0; w < n; ++w) {
        ex::Wheel& wh = c.w[size_t(w)];
        const ex::Ground& g = c.g[size_t(w)];
        if (g.miss) {
            wh.bump = k0;
            continue;
        }
        const size_t s = surface[size_t(w)] < 24 ? surface[size_t(w)] : 0;
        const F amp = t.amp[s];
        if (amp == k0) {
            wh.bump = k0;
            continue;
        }
        F b = ex::bump_wave(s8(t.mode[s]), g.x, g.z, t.freq[s]);
        if (t.soft[material[size_t(w)] & 31] && !all_soft) b = b * kHalf;
        if (t.scaled[s]) b = b * wh.speed_ramp;
        wh.bump = b * amp;
    }
}

void susp_coords(ex::Car& c, const BodyExtra& x, const WheelLayout& l) {
    for (int w = 0; w < count(l); ++w) {
        const ex::Wheel& wh = c.w[size_t(w)];
        const F drop = rest_drop(c, x, l, w);
        const F hub = (c.pos.z + wh.hub.z) + c.up.z * drop; // 0x369760
        const size_t a = axle_of(l, w);
        F h = hub - (wh.ground + wh.bump);
        h = (h * c.ground_up.z) / x.contact_up[size_t(w)];
        c.susp_x[size_t(w)] = ((h - c.p.tire_radius[a]) + c.p.susp[a].offset) - drop;
    }
}

u32 bump_mask(const ex::Car& c, const WheelLayout& l, u32 mask) {
    for (int w = 0; w < count(l); ++w)
        if (c.susp_x[size_t(w)] < c.p.susp[axle_of(l, w)].bump_limit) mask |= 1u << w;
    return mask;
}

void terrain(ex::Car& c, BodyExtra& x, const WheelLayout& l) {
    F d = k0;
    d = d + c.ground_up.x * c.up.x;
    d = d + c.ground_up.y * c.up.y;
    d = d + c.ground_up.z * c.up.z;
    c.terrain = d;
    for (int w = 0; w < count(l); ++w) {
        const ex::Vec3& n = c.g[size_t(w)].normal;
        x.contact_up[size_t(w)] = ((n.x * c.up.x) - (n.z * c.up.y)) + (n.y * c.up.z);
    }
    susp_coords(c, x, l);
    c.bump_mask = u8(bump_mask(c, l, 0));
}

F damper(const ex::Suspension& s, const AxleBody& b, const BodyGlobals& g, F v, u32 axle) {
    using gt3r::ee::sqrt;
    if (g.damper_linear) {
        if (v < k0) {
            if (v <= -s.bump_g) return b.linear_bump_max;
            if (-s.bump_t <= v) return (-v * s.bump_o) / s.bump_t;
            return s.bump_o + ((-v - s.bump_t) * (b.linear_bump_max - s.bump_o)) / (s.bump_g - s.bump_t);
        }
        if (!(k0 < v)) return k0;
        if (s.reb_g <= v) return -b.linear_rebound_max;
        if (v <= s.reb_t) return (-v * s.reb_o) / s.reb_t;
        return -(s.reb_o + ((v - s.reb_t) * (b.linear_rebound_max - s.reb_o)) / (s.reb_g - s.reb_t));
    }
    const size_t a = axle & 1;
    if (v < k0) {
        if (v < kMinusOne) v = kMinusOne;
        F f = s.bump_o + s.bump_g * sqrt(-v - s.bump_t);
        if (g.damper_table != 0) {
            const F knee = g.bump_knee[a];
            const F lo = -knee;
            if (lo < v) {
                const F at = s.bump_o + s.bump_g * sqrt(knee - s.bump_t);
                const F share = g.bump_share[a];
                f = (kOne - share) * f + share * ((at * (v * v)) / (lo * lo));
            }
        }
        return f;
    }
    if (kOne < v) v = kOne;
    F f = -s.reb_o - s.reb_g * sqrt(v - s.reb_t);
    if (g.damper_table != 0) {
        const F knee = g.rebound_knee[a];
        if (v < knee) {
            const F at = -s.reb_o - s.reb_g * sqrt(knee - s.reb_t);
            const F share = g.rebound_share[a];
            f = (kOne - share) * f + share * ((at * (v * v)) / (knee * knee));
        }
    }
    return f;
}

void suspension(ex::Car& c, BodyExtra& x, const ChassisExtra& ch, const WheelLayout& l, const BodyGlobals& g) {
    const int n = count(l);
    // The hubs' speeds along their ground's normals.
    for (int w = 0; w < n; ++w) {
        const size_t geo = l.geometry[size_t(w)];
        const F roll = c.p.half_track[geo] * c.roll_rate;
        const F pitch = (geo ? c.p.dist_rear : c.p.dist_front) * c.pitch_rate;
        F u = k0;
        const u8 side = l.side[size_t(w)];
        if (side == 0) u = roll + u;
        else if (side == 1) u = u - roll;
        const s8 fa = fore_aft(l, w);
        if (fa > 0) u = u + pitch;
        else if (fa < 0) u = u - pitch;
        const ex::Vec3& nn = c.g[size_t(w)].normal;
        const F vx = c.vel.x + c.up.x * u, vy = c.vel.y + c.up.y * u, vz = c.vel.z + c.up.z * u;
        x.normal_speed[size_t(w)] = ((vx * nn.x) + (vy * -nn.z)) + (vz * nn.y);
    }
    // Each wheel's spring and damper, its contact, half track and camber.
    for (int w = 0; w < n; ++w) {
        ex::Wheel& wh = c.w[size_t(w)];
        const size_t a = axle_of(l, w);
        u8 state = k0 < wh.load ? 1 : 0;
        // (0x35D248: the spring rate; a special car of kind 1 scales it by a count of its own ❓)
        wheel(c, x, g, w, a, c.susp_x[size_t(w)], c.p.susp[a].k);
        if (state == 0) {
            if (k0 < wh.load) state = 2;
        } else if (wh.load == k0) state = 3;
        x.contact[size_t(w)] = state;
        const F travel = wh.susp - c.p.susp[a].x_static;
        wh.half_track = half_track(c, l, w, travel);
        if (l.kind == 1) wh.camber = c.steer_cmd * (c.p.steer_camber * gt3r::ee::cos(c.roll - c.ground_roll));
        else wh.camber = camber(c, l, w, c.steer_cmd, travel);
    }
    // The anti-roll bars, per wheel group (0x36A048).
    std::array<F, 4> bar{};
    for (int k = 0; k < l.groups && k < 4; ++k) {
        bar[size_t(k)] = k0;
        const u8 first = l.group_first[size_t(k)];
        const u8 other = l.partner[size_t(first)];
        if (other == 0x7F) continue;
        const size_t ax = k < l.groups ? l.group_axle[size_t(k)] : 0;
        const F d = c.w[first].susp - c.w[other].susp;
        const F gain = c.p.susp[ax].anti_roll;
        bar[size_t(k)] = k0 <= d ? (gain * d) * d : (-gain * d) * d;
    }
    F lateral = k0;
    if (!g.lateral_geometry) lateral = c.p.lat_transfer * -c.a_y;
    x.in_line_lean = k0;
    if (l.kind == 1) x.in_line_lean = (c.a_y * ch.mass) * c.up.z;
    const F along = c.p.long_transfer * c.drive_sum;
    for (int w = 0; w < n; ++w) {
        const ex::Wheel& wh = c.w[size_t(w)];
        F f = wh.spring_f;
        if (wh.load == k0) f = -c.p.susp[axle_of(l, w)].preload;
        const u8 side = l.side[size_t(w)];
        if (l.partner[size_t(w)] != 0x7F) {
            const F r = bar[l.geometry[size_t(w)] & 3];
            if (side == 0) f = f - r;
            else if (side == 1) f = f + r;
        }
        const s8 fa = fore_aft(l, w);
        if (fa > 0 || fa < 0) {
            const u8 m = fa > 0 ? l.axle_wheels[0] : l.axle_wheels[1];
            const F wheels = gt3r::ee::int_to_f(s32(m));
            f = f - (fa > 0 ? c.downforce[0] : c.downforce[1]) / wheels;
            F share = along;
            if (m != 2) share = along * (kTwo / wheels);
            f = fa > 0 ? f + share : f - share;
        }
        if (l.partner[size_t(w)] == 0x7F) {
            c.body_force[size_t(w)] = f;
            continue;
        }
        if (g.lateral_geometry) {
            const size_t a = axle_of(l, w);
            const F lever = x.roll_centre + ((c.susp_x[size_t(w)] - c.p.susp[a].offset) + c.p.tire_radius[a]);
            const F t = (lever / c.p.half_track[a]) * ch.wheel_lat[size_t(w)];
            if (side == 0) f = f - t;
            else if (side == 1) f = f + t;
        } else {
            if (side == 0) f = f + lateral;
            else if (side == 1) f = f - lateral;
        }
        c.body_force[size_t(w)] = f;
    }
}

void pitch_terms(const ex::Car& c, const WheelLayout& l, F& moment, F& lift) {
    F m = k0, up = k0;
    for (int w = 0; w < count(l); ++w) {
        const ex::Wheel& wh = c.w[size_t(w)];
        const size_t a = axle_of(l, w), geo = l.geometry[size_t(w)];
        const F f = (gt3r::ee::cos(wh.steer) * (wh.fx_drive + wh.fx_brake)) * c.p.tire_radius[a];
        m = m + f * c.p.pitch_tire[geo];
        const s8 fa = fore_aft(l, w);
        const F dist = geo ? c.p.dist_rear : c.p.dist_front;
        if (fa > 0) up = up + f / dist;
        else if (fa < 0) up = up - f / dist;
    }
    moment = m, lift = up;
}

void attitude(ex::Car& c, BodyExtra& x, const ChassisExtra& ch, const TireExtra& t, const WheelLayout& l, const BodyGlobals& g) {
    using gt3r::ee::cos;
    using gt3r::ee::sin;
    const int n = count(l);
    const F dt = c.dt;
    F moment, lift;
    pitch_terms(c, l, moment, lift);
    auto damper_of = [&](int w) { return c.w[size_t(w)].load == k0 ? k0 : c.w[size_t(w)].damper_f; };
    // Pitch: the axles' body forces and dampers about their levers, and the tires' moment.
    std::array<F, 4> s{};
    for (int w = 0; w < n; ++w) s[l.axle[size_t(w)] & 3] = s[l.axle[size_t(w)] & 3] + c.body_force[size_t(w)];
    for (int w = 0; w < n; ++w) s[l.axle[size_t(w)] & 3] = s[l.axle[size_t(w)] & 3] + damper_of(w);
    F rate = c.pitch_rate + (((s[0] * c.p.pitch_lever[0]) - (s[1] * c.p.pitch_lever[1])) + moment) * dt;
    if (kPitchRate < rate) rate = kPitchRate;
    else if (rate < -kPitchRate) rate = -kPitchRate;
    F angle = c.pitch + rate * dt;
    if (angle < -kAngle) angle = -kAngle, rate = k0;
    else if (kAngle < angle) angle = kAngle, rate = k0;
    c.pitch = angle, c.pitch_rate = rate;
    // Roll: the same by side (left plus, right minus) per axle.
    s = {};
    for (int w = 0; w < n; ++w) {
        const u8 side = l.side[size_t(w)];
        F& at = s[l.axle[size_t(w)] & 3];
        if (side == 0) at = at + c.body_force[size_t(w)];
        else if (side == 1) at = at - c.body_force[size_t(w)];
    }
    for (int w = 0; w < n; ++w) {
        const u8 side = l.side[size_t(w)];
        F& at = s[l.axle[size_t(w)] & 3];
        if (side == 0) at = at + damper_of(w);
        else if (side == 1) at = at - damper_of(w);
    }
    if (l.kind == 1) { // two wheels in line: a lean of their own
        F lean = -x.in_line_x65c * g.in_line[0];
        const F k = g.in_line[1];
        if (k0 < k) {
            const F roll = c.roll;
            const F hw = t.width[0] * kHalf;
            const F tn = gt3r::ee::tan(roll), sn = sin(roll), cs = cos(roll);
            const F lean_angle = ex::atan2(behaviour(), k * sn - hw * tn, k * cs);
            const F arm = (k * cos(roll)) / cos(lean_angle);
            lean = lean + ((ch.mass * kGravity) * sin(lean_angle)) * arm;
        }
        lean = lean - x.in_line_lean * g.in_line[2];
        std::array<F, 4> spin{};
        for (int w = 0; w < n; ++w)
            spin[size_t(w)] = (c.w[size_t(w)].rim_speed / c.p.tire_radius[axle_of(l, w)]) * cos(c.w[size_t(w)].steer);
        lean = lean + (g.in_line[3] * spin[0]) * x.in_line_x480;
        lean = lean + (g.in_line[4] * spin[0]) * c.yaw_rate;
        lean = lean + (g.in_line[5] * spin[1]) * c.yaw_rate;
        s[1] = s[1] + lean, s[0] = s[0] + lean;
    }
    rate = c.roll_rate + ((s[0] * c.p.roll_lever[0]) + (s[1] * c.p.roll_lever[1])) * dt;
    if (kRollRate < rate) rate = kRollRate;
    else if (rate < -kRollRate) rate = -kRollRate;
    angle = c.roll + rate * dt;
    if (angle < -kAngle) angle = -kAngle, rate = k0;
    else if (kAngle < angle) angle = kAngle, rate = k0;
    c.roll = angle, c.roll_rate = rate;
    rules::body_axes(c);
    // Heave.
    const u8 mode = g.normal_mode != 0 ? g.normal_mode : l.kind == 1 ? 1 : 0;
    if (mode == 0) { // the body forces and dampers along the body's up axis
        F sum = k0;
        for (int w = 0; w < n; ++w) sum = sum + c.body_force[size_t(w)];
        for (int w = 0; w < n; ++w) sum = sum + damper_of(w);
        sum = (sum + lift) / x.sprung_mass;
        c.heave_acc = sum - c.up.z * kGravity;
        const F du = sum * dt;
        c.vel.x = c.vel.x + c.up.x * du, c.vel.y = c.vel.y + c.up.y * du, c.vel.z = c.vel.z + c.up.z * du;
        c.vel.z = c.vel.z - dt * kGravity;
        return;
    }
    // Each wheel's load (less its own weight) along its ground's normal (mode 2) or the ground's up axis.
    std::array<F, 3> acc{};
    for (int w = 0; w < n; ++w) {
        std::array<F, 3> d;
        if (mode == 2) {
            const ex::Vec3& nn = c.g[size_t(w)].normal;
            d = {nn.x, -nn.z, nn.y};
        } else d = {c.ground_up.x, c.ground_up.y, c.ground_up.z};
        F f = k0;
        f = f + d[0] * c.up.x, f = f + d[1] * c.up.y, f = f + d[2] * c.up.z;
        F load = c.w[size_t(w)].load - c.p.susp[axle_of(l, w)].preload;
        if (load < k0) load = k0;
        f = f * load;
        for (size_t i = 0; i < 3; ++i) acc[i] = acc[i] + f * d[i];
    }
    F up = lift - (c.downforce[0] + c.downforce[1]);
    if (x.speed_part_kind == 7) { // a lift under the body near the ground, gone 10 cm higher
        const F h = (c.pos.z - c.ground_pos.z) - (c.p.cog_height + kTenth);
        if (h <= k0) up = up - x.speed_part_lift;
        else if (!(kTenth < h)) up = up - x.speed_part_lift * (kOne - h * kTen);
    }
    const std::array<F, 3> u{c.up.x, c.up.y, c.up.z};
    for (size_t i = 0; i < 3; ++i) acc[i] = acc[i] + up * u[i];
    for (size_t i = 0; i < 3; ++i) acc[i] = acc[i] / x.sprung_mass;
    acc[2] = acc[2] - kGravity;
    F heave = k0;
    std::array<F*, 3> v{&c.vel.x, &c.vel.y, &c.vel.z};
    for (size_t i = 0; i < 3; ++i) {
        *v[i] = *v[i] + acc[i] * dt;
        heave = heave + acc[i] * u[i];
    }
    c.heave_acc = heave;
}

void bump_nudge(ex::Car& c, const BodyExtra& x, const WheelLayout& l) {
    if (c.bump_mask == 0) return;
    hub_heights(c, l);
    susp_coords(c, x, l);
    const u32 mask = bump_mask(c, l, u32(c.bump_mask) << 4);
    c.bump_mask = u8(mask);
    if (mask & 0xF) {
        std::array<F, 4> d{};
        for (int w = 0; w < count(l); ++w) {
            const F p = c.p.susp[axle_of(l, w)].bump_limit - c.susp_x[size_t(w)];
            d[size_t(w)] = p < k0 ? k0 : p;
        }
        F front = k0, rear = k0, left = k0, right = k0;
        const F rw = c.p.rear_weight;
        switch (l.kind) {
        case 0:
            left = d[0] + (d[2] - d[0]) * rw, right = d[1] + (d[3] - d[1]) * rw;
            front = (d[0] + d[1]) * kHalf, rear = (d[2] + d[3]) * kHalf;
            break;
        case 1: front = d[0], rear = d[1]; break;
        case 2:
            front = d[0];
            left = front + (d[1] - front) * rw, right = front + (d[2] - front) * rw;
            rear = (d[1] + d[2]) * kHalf;
            break;
        case 3:
            rear = d[2];
            left = rear + (d[0] - rear) * x.front_share, right = rear + (d[1] - rear) * x.front_share;
            front = (d[0] + d[1]) * kHalf;
            break;
        default: break;
        }
        F lift = k0;
        if (!(left == k0 && right == k0)) {
            if (right < left) {
                c.roll = c.roll + ex::atan2(behaviour(), left - right, c.p.track);
                lift = right * kHalf;
            } else if (left < right) {
                c.roll = c.roll + ex::atan2(behaviour(), left - right, c.p.track);
                lift = left * kHalf;
            } else lift = left * kHalf;
        }
        if (!(front == k0 && rear == k0)) {
            F half;
            if (rear < front) {
                c.pitch = c.pitch + ex::atan2(behaviour(), front - rear, c.p.wheelbase);
                half = rear * kHalf;
            } else if (front < rear) {
                c.pitch = c.pitch + ex::atan2(behaviour(), front - rear, c.p.wheelbase);
                half = front * kHalf;
            } else half = front * kHalf;
            lift = lift + half;
        }
        c.pos.z = c.pos.z + lift;
    }
    rules::body_axes(c);
    // The downward speed held to the vertical part of the velocity in the ground's plane.
    F along = k0, side = k0;
    along = along + c.ground_fwd.x * c.vel.x, side = side + c.ground_lat.x * c.vel.x;
    along = along + c.ground_fwd.y * c.vel.y, side = side + c.ground_lat.y * c.vel.y;
    along = along + c.ground_fwd.z * c.vel.z, side = side + c.ground_lat.z * c.vel.z;
    F floor = along * c.ground_fwd.z + side * c.ground_lat.z;
    if (k0 < floor) floor = k0;
    if (c.vel.z < floor) c.vel.z = floor;
}

} // namespace gt4r::sim::rules
