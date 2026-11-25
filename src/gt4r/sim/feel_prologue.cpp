// Gran Turismo 4 Prologue's phase 19 past its physics (feel_prologue.h).

#include "gt4r/sim/feel_prologue.h"

#include "base/ee_math.h"

#include "gt4r/sim/program.h"
#include "gt4r/sim/program_prologue.h"

namespace gt4r::sim::prologue {

namespace {
const F kZero = F(), kOne = F::raw(0x3F800000);
constexpr u32 kLeanOn = 0x5042B8, kRollOn = 0x5042B4;
} // namespace

rules::SpeedTables speed_tables(const Program& program, u8 special_row) {
    rules::SpeedTables t;
    t.lean_on = program.w(kLeanOn), t.roll_on = program.w(kRollOn);
    const u32 row = PrologueProgram::kSpecialBodies + 0x78 * (special_row < 3 ? special_row : 0);
    t.lean_row = {F::raw(program.w(row + 0x30)), F::raw(program.w(row + 0x34))};
    t.special_x10 = F::raw(program.w(row + 0x10));
    return t;
}

rules::FeelTables feel_tables(const Program& program, const std::array<F, 4>& sound_scales) {
    rules::FeelTables t;
    t.view_heading_gain = F::raw(program.w(0x571C98)), t.view_heading_damp = F::raw(program.w(0x571CA0));
    for (size_t k = 0; k < 4; ++k) t.cue[25 + k] = sound_scales[k];
    return t;
}

void speeds(ex::Car& c, FeelPart& x, const rules::WheelLayout& l, const rules::SpeedTables& t, const ex::Condition& k) {
    // (the two-wheeler's lean term reads F + 0x480 with the switch 0x5042B8, 0 in the image: ❓ not followed)
    rules::hub_velocities(c, l, t, F());
    c.speed_abs = c.speed;
    // 0x3C5698: the odometer and the distance total from the signed speed (Gran Turismo 3's 2 m odometer, 8,000 km wrap).
    if (!(c.speed_abs < kZero)) {
        const F d = c.speed_abs * c.dt;
        c.odo_carry = ex::odometer_add(c.odometer, c.odo_carry + d);
        const F wrap = F::raw(0x4AF42400);
        const F total = c.dist_total + d;
        c.dist_total = total;
        if (wrap <= total) c.dist_total = total - wrap;
    }
    if (c.speed_abs < kZero) c.speed_abs = -c.speed_abs;
    // 0x3B9700, 0x3B1860, 0x3B1958: the engine's wear and oil by the step's distance.
    const F d = c.speed_abs * c.dt;
    c.engine_wear = c.engine_wear + d;
    if (k.e3 < c.engine_wear) c.engine_wear = k.e3;
    const F o = c.oil;
    c.oil = o + d;
    if (o < kZero && !(c.oil < kZero)) c.oil = -(-k.omax);
    if (k.omax < c.oil) c.oil = k.omax;
    if (c.finished == 0 && c.max_speed < c.speed_abs) c.max_speed = c.speed_abs;
    // 0x3C55F8: the speedometer.
    {
        const F s = c.speed_abs, v = x.speedo, step = c.dt * F::raw(0x42DE38E2);
        if (v < s) {
            x.speedo = v + step;
            if (s < x.speedo) x.speedo = s;
        } else {
            x.speedo = v - step;
            if (x.speedo < s) x.speedo = s;
        }
    }
    rules::free_wheels(c, l);
}

bool hit_penalty(const ex::Car& c, const rules::HitRules& r, int kind, int part, F strength, bool running) {
    if (gt3r::s8(c.control) != 0 || c.finished != 0 || r.penalty == 0 || running) return false;
    if (rules::part_class(part) >= 3) return false;
    if (kind == 1) return rules::past_limit(r.car_limit, strength * F::raw(0x42C80000));
    if (kind == 0) return rules::past_limit(r.wall_limit, strength * c.p.inv_mass);
    return false;
}

namespace {
void part_params(const CarPart& x, rules::NearExtra& n) { n.speed_lo = x.part_lo, n.speed_hi = x.part_hi, n.speed_rate = x.part_rate; }
} // namespace

void part_start(const ex::Car& c, const CarPart& x, FeelPart& f) {
    f.part_state = f.part.speed_part_state2 = F(), f.part.speed_state = f.part.speed_state2 = 0;
    part_params(x, f.part);
    if (x.part_kind == 1) {
        if (x.part_hi <= c.speed) f.part_state = kOne, f.part.speed_state = 1;
    } else if (x.part_kind == 2) f.part_state = rules::speed_part_share(f.part, c.speed);
}

void part_step(ex::Car& c, const CarPart& x, FeelPart& f) {
    if (x.part_kind == 0) return;
    part_params(x, f.part);
    const u8 kind = x.part_kind;
    rules::speed_part(c, f.part, kind == 1 || kind == 2 || kind == 4 ? kind : 3, f.part_state);
}

void part_downforce(ex::Car& c, const CarPart& x, const FeelPart& f, const ex::Slipstream& s) {
    if (x.part_kind < 1 || x.part_kind > 2) return;
    const F a = (s.downforce_share + (kOne - s.downforce_share) * (kOne - c.slip_filter)) * (c.speed * c.speed);
    c.downforce[1] = c.downforce[1] + x.part_lift * a * f.part_state;
}

rules::SlideTables slide_tables(const Program& program) {
    std::array<u8, 0x210> b{};
    for (u32 k = 0; k < b.size(); ++k) b[k] = program.b(kEffectsBlock + k);
    return rules::slide_tables(b.data());
}

int slide_kind(u8 s) {
    if (s >= 2 && s <= 5) return 1;
    if (s == 6 || (s >= 12 && s <= 14)) return 2;
    return 0;
}

void tire_effects(ex::Car& c, const rules::WheelLayout& l, const rules::SlideTables& t, u8 course_kind) {
    using gt3r::ee::sqrt;
    F dirt = kZero;
    const int n = l.count < 4 ? l.count : 4;
    for (int i = 0; i < n; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        if (w.load == kZero) {
            if (w.smoke_count != 0) --w.smoke_count;
        } else
            w.smoke_count = 6;
        if (w.smoke_count == 0) {
            w.smoke_emit = kZero, w.skid_pitch = kOne, w.slip_intensity = kZero;
            continue;
        }
        const u8 s = w.surface;
        const int kind = slide_kind(s);
        F angle = c.t[size_t(i)].slip_angle;
        if (angle < kZero) angle = -angle;
        F lat = gt3r::ee::cos(w.steer) * w.hub_vy + gt3r::ee::sin(w.steer) * w.hub_vx;
        if (lat < kZero) lat = -lat;
        const F along = gt3r::ee::cos(w.steer) * w.hub_vx - gt3r::ee::sin(w.steer) * w.hub_vy;
        F slide = along - w.rim_speed;
        if (along < kZero) slide = -slide;
        const F ratio = w.slip_ratio;
        auto measure = [&](int m, F& ramp) {
            const rules::SlideCurves& e = t.set[size_t(m)][size_t(kind)];
            const F a = ex::lookup(e.a, t.by_lat[size_t(m)][size_t(kind)] ? lat : angle);
            const F b = ex::lookup(e.b, t.by_slide[size_t(m)][size_t(kind)] ? slide : ratio);
            ramp = w.speed_ramp - e.knee;
            return sqrt(b * b + a * a);
        };
        if (s != 1 && s != 13) {
            const rules::SlideCurves& e0 = t.set[0][size_t(kind)];
            F ramp;
            F m = measure(0, ramp);
            if (kZero < ramp) m = m + ramp * e0.gain;
            if (!(m == kZero)) m = m + e0.base;
            const bool starts = w.slip_intensity == e0.base && !(m == e0.base);
            w.slip_intensity = m;
            const rules::SlideCurves& e1 = t.set[1][size_t(kind)];
            F p = e1.base + measure(1, ramp);
            if (kZero < ramp) p = p + ramp * e1.gain;
            if (starts && (i & 1)) p = p * F::raw(0x3F400000);
            w.skid_pitch = p;
        } else {
            F k = w.load / F::raw(0x45191FFF) - F::raw(0x3E800000); // 2450 N, 0.25
            if (k < kZero) k = kZero;
            else if (kOne < k) k = kOne;
            F rim = w.rim_speed;
            if (rim < kZero) rim = -rim;
            F e = (rim - F::raw(0x3FB1C71B)) * F::raw(0x3E1374BD); // 5 km/h
            if (e < kZero) e = kZero;
            else if (kOne < e) e = kOne;
            w.slip_intensity = e * k * t.loose_scale;
            if (rim <= t.loose_x0) w.skid_pitch = t.loose_y0;
            else if (t.loose_x1 <= rim) w.skid_pitch = t.loose_y1;
            else w.skid_pitch = t.loose_y0 + (rim - t.loose_x0) * (t.loose_y1 - t.loose_y0) / (t.loose_x1 - t.loose_x0);
        }
        const rules::SlideCurves& e2 = t.set[2][size_t(kind)];
        F ramp;
        F m = measure(2, ramp);
        if (kZero < ramp) m = m + ramp * e2.gain;
        if (s == 8) m = kZero;
        if (!(m == kZero)) m = m + e2.base;
        if (kOne < m) m = kOne;
        w.smoke_emit = m;
        if (kind != 0) dirt = dirt + m;
    }
    if ((course_kind == 0xFF ? 0 : course_kind) == 0) dirt = dirt * F::raw(0x41000000);
    const F add = dirt * F::raw(0x3A83126E) * c.dt;
    c.dirt = c.dirt + add;
    if (F::raw(0x3F4CCCCC) < c.dirt) c.dirt = F::raw(0x3F4CCCCC);
}

void pad_rumble(const ex::Car& c, const rules::WheelLayout& l, std::array<u8, 2>& pad) {
    if (u8(u8(c.control) - 1) < 2 || c.driver_kind != 0) {
        pad = {0, 0};
        return;
    }
    const F kQuarter = F::raw(0x3E800000);
    const int n = l.count < 4 ? l.count : 4;
    F drive = kZero, brake = kZero, slip = kZero;
    int ahead = 0;
    for (int i = 0; i < n; ++i) {
        if (gt3r::s8(l.fore_aft[size_t(i)]) <= 0) continue;
        const ex::Wheel& w = c.w[size_t(i)];
        ++ahead;
        brake = brake + w.fx_brake;
        drive = drive + w.fx_drive;
        slip = slip + w.slip_ratio * w.slip_ratio;
    }
    if (ahead != 0) drive = drive * (slip / gt3r::ee::int_to_f(ahead) + F::raw(0x3DCCCCCC));
    if (drive < kZero) drive = -drive;
    const F cap = F::raw(0x3EAA7EF9), scale = F::raw(0x46991FFF); // 0.333, 19,600
    drive = drive * F::raw(0x3A2A7EF9);
    if (cap < drive) drive = cap;
    brake = brake / scale;
    brake = brake * brake;
    if (cap < brake) brake = cap;
    F hit = kZero;
    if (kZero < c.hit_rumble) hit = c.hit_rumble * F::raw(0x41000000) + kQuarter;
    const F forces = drive + brake;
    F bump = kZero, kerb = kZero;
    for (int i = 0; i < n; ++i) {
        if (c.race_hold != 0) continue;
        const ex::Wheel& w = c.w[size_t(i)];
        if (kZero < w.damper_f) {
            const F x = w.damper_f / scale;
            bump = bump + (kOne < x ? kOne : x);
        }
        if (gt3r::s8(l.fore_aft[size_t(i)]) <= 0) continue;
        const u8 s = w.surface;
        if (s != 1 && s != 13) continue;
        F k = w.load / F::raw(0x4537BFFF) * (c.speed * F::raw(0x3C800000)); // 2,940 N, / 64
        if (k < kZero) k = -k;
        kerb = kerb + (kQuarter < k ? kQuarter : k);
    }
    F level = bump + kerb;
    if (level < forces) level = forces;
    if (level < hit) level = hit;
    s32 v = gt3r::ee::to_int(level * F::raw(0x43800000));
    if (!(v < 0x80)) v = 0x7F;
    pad[1] = u8(v);
}

ForceTables force_tables(const Program& program, const Controls& k) {
    // (the settings as rules::force_mode takes them: its bytes +0x74 .. +0x7A, its rows from +0x7C; no rate table's bytes)
    std::array<u8, rules::kForceSettingsSize> K{};
    for (u32 i = 0; i < 7; ++i) K[0x74 + i] = program.b(0x503F20 + i);
    for (u32 i = 0; i < 0x60; ++i) K[0x7C + i] = program.b(PrologueProgram::kController + 0x74 + i);
    ForceTables t;
    for (int m = 0; m < 3; ++m) {
        t.smooth[size_t(m)] = K[0x7C + 0x20 * size_t(m) + 4];
        t.mode[size_t(m)] = rules::force_mode(K.data(), k.jog_scale[0], k.jog_scale[1], m);
    }
    return t;
}

void steer_force(const ex::Car& c, ForcePart& o, const CarPart& x, const ForceTables& t, const rules::SteerTables& g, u8 course_kind, int strength_index) {
    const F kMinus = F::raw(0xBF800000), kHalf = F::raw(0x3F000000), kTwo = F::raw(0x40000000), kEight = F::raw(0x41000000);
    const int m = c.steer_class == 3 ? (x.special_row ? 2 : 1) : 0;
    const rules::ForceMode& k = t.mode[size_t(m)];
    o.wheel = {};
    if (u8(u8(c.control) - 1) < 2 || c.driver_kind != 0) {
        s32 v = c.wheel_pos;
        if (v < 0) v += 0x1F;
        const s32 out = 0x80 - (v >> 5);
        o.out = u8(out < 0 ? 0 : out < 0x100 ? out : 0xFF);
        return;
    }
    const rules::WheelLayout& l = x.layout;
    const int n = l.count < 4 ? l.count : 4;
    auto ahead = [&](int i) { return gt3r::s8(l.fore_aft[size_t(i)]) > 0; };
    auto clamp1 = [&](F& v) {
        if (kOne < v) v = kOne;
        else if (v < kMinus) v = kMinus;
    };
    const bool loose = (course_kind == 0xFF ? 0 : course_kind) == 2;
    const F front = x.axle_load[0];
    F f = kZero;
    for (int i = 0; i < n; ++i) {
        if (!ahead(i)) continue;
        const ex::Wheel& w = c.w[size_t(i)];
        F a = w.force_a;
        if (k.slip_on) {
            F s = c.t[size_t(i)].slip_angle;
            if (s < kZero) s = -s;
            const F fx = w.fx_drive + w.fx_brake;
            const F rest = kOne - s * k.slip_inv;
            F share = k.slip_base + rest * k.slip_share;
            share = share + (fx / (front * kHalf)) * k.drive_share;
            if (kTwo < share) share = kTwo;
            else if (share < kMinus) share = kMinus;
            a = a * share;
        }
        f = f + a;
    }
    f = f / front;
    clamp1(f);
    f = rules::pedal_curve(k.curve[loose ? 1 : 0], f);
    const F strength = c.steer_class == 3 && strength_index >= 0 && strength_index < 3 ? k.strength[size_t(strength_index)] : kOne;
    const F gain = k.gain[loose ? 1 : 0];
    if (k.limit_kind == 1) {
        if (gain < f) f = gain;
        else if (f < -gain) f = -gain;
        f = f * strength;
    } else if (k.limit_kind == 2) {
        if (strength < f) f = strength;
        else if (f < -strength) f = -strength;
        f = f * gain;
    } else {
        f = f * gain;
        f = f * strength;
    }
    if (g.mode != 0) {
        for (int i = 0; i < n; ++i) {
            if (!ahead(i)) continue;
            F a = kZero;
            if (g.mode == 1) a = -x.tires.steer_offset[size_t(i)];
            else if (g.mode == 2) a = -(x.tires.steer_offset[size_t(i)] - c.w[size_t(i)].steer);
            const F y = rules::pedal_curve(k.offset_curve, a / g.limit[l.geometry[size_t(i)] & 1u]);
            f = f + k.offset_gain * y;
            f = f - x.offset_rate[size_t(i)] * k.offset_damp;
        }
        clamp1(f);
    }
    f = f + gt3r::ee::int_to_f((s32(c.wheel_pos) - s32(c.wheel_pos_prev)) * 60) * k.turn_gain;
    F st = c.steer_cmd;
    if (k.steer_max < st) st = k.steer_max;
    else if (st < -k.steer_max) st = -k.steer_max;
    f = f + k.steer_gain * st;
    // The smoothing.
    F out = kZero;
    switch (t.smooth[size_t(m)]) {
    case 0: out = f, o.last = f; break;
    case 1: out = (o.last + f) * kHalf, o.last = f; break;
    case 2:
        out = f;
        if (kZero <= f ? f < o.last : o.last < f) out = o.last;
        o.last = f;
        break;
    case 3: {
        if (o.count < 60) ++o.count;
        const bool up = kZero <= o.last;
        if (o.count >= 7) {
            o.last = up ? o.last - c.dt * kEight : o.last + c.dt * kEight;
            if (up ? o.last < kZero : kZero < o.last) o.last = kZero;
        }
        if (up ? (o.last < f || f < kZero) : (f < o.last || kZero < f)) o.last = f, o.count = 0;
        out = o.last;
        break;
    }
    default: break;
    }
    // The front wheels' aligning moment.
    F align = kZero;
    for (int i = 0; i < n; ++i) {
        if (!ahead(i)) continue;
        const ex::Wheel& w = c.w[size_t(i)];
        const F cs = gt3r::ee::cos(w.steer), sn = gt3r::ee::sin(w.steer);
        const F d[3] = {cs * c.lat.x - sn * c.fwd.x, cs * c.lat.y - sn * c.fwd.y, cs * c.lat.z - sn * c.fwd.z};
        const ex::Vec3& gn = c.g[size_t(i)].normal;
        const F nv[3] = {gn.x, -gn.z, gn.y};
        F dot = kZero;
        for (size_t j = 0; j < 3; ++j) dot = dot + d[j] * nv[j];
        align = align + dot * w.load;
    }
    const F spring = (c.p.susp[0].k + F::raw(0x473F67FF)) / F::raw(0x483F67FF); // 49,000, 196,000
    align = align * spring;
    out = out + (align * k.align_gain) / front;
    const F speed = gt3r::ee::sqrt(c.vel.x * c.vel.x + c.vel.y * c.vel.y);
    if (speed < F::raw(0x3F0E38E3) && g.mode == 0) { // 2 km/h
        F s = c.steer_cmd;
        if (F::raw(0x3DB2B8C1) < s) s = F::raw(0x3DB2B8C1);
        else if (s < F::raw(0xBDB2B8C1)) s = F::raw(0xBDB2B8C1);
        out = s * F::raw(0x40B758B5);
    }
    if (c.steer_class == 3 && !loose) o.wheel = k.wheel;
    clamp1(out);
    o.out = u8(-0x80 - gt3r::ee::to_int(out * F::raw(0x42FE0000)));
}

} // namespace gt4r::sim::prologue
