// Gran Turismo 4 Prologue's own race step (rules_prologue.h).

#include "gt4r/sim/rules_prologue.h"

#include "base/ee_math.h"
#include "sim/exact/start.h"
#include "sim/exact/build_math.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/course_ground.h"
#include "gt4r/formats/db/car_parts.h"

#include <cstdlib>
#include "gt4r/sim/program.h"
#include "gt4r/sim/program_prologue.h"
#include "gt4r/sim/rules_hybrid.h"
#include "gt4r/sim/rules_input.h"

namespace gt4r::sim::prologue {

namespace {
const F kOne = F::raw(0x3F800000), kMinusOne = F::raw(0xBF800000);
const F kKmh = F::raw(0x3E8E38E3);                                // 1 / 3.6
const F kHalfPi = F::raw(0x3FC90FD9);

F speed_of(u8 kmh) { return gt3r::ee::int_to_f(kmh ? kmh : 50) * kKmh; }
F f_of(u32 v) { return gt3r::ee::int_to_f(s32(v)); }
} // namespace

Controls controls(const u8* c) {
    Controls k;
    const F deg = F::raw(0x3C8EFA34), hundred = F::raw(0x42C80000), sixty = F::raw(0x42700000);
    auto step = [&](u8 b) {
        const F v = hundred / (f_of(b) * sixty);
        return kOne < v ? kOne : v;
    };
    k.throttle_up = step(c[0x14]), k.throttle_down = step(c[0x15]), k.brake_up = step(c[0x16]), k.brake_down = step(c[0x17]);
    k.third_up = step(c[0x18]), k.third_down = step(c[0x19]);
    k.analog_jump = f_of(c[0x04]) * F::raw(0x3C23D70A);
    SteerFollow& s = k.follow;
    for (u32 r = 0; r < 2; ++r) {
        s.centering[r] = f_of(c[0x1E + r]);
        s.gain[r] = f_of(c[0x1C + r]);
        s.toward[r] = c[0x10] ? f_of(c[0x20 + r]) : F();
        s.damping[r] = f_of(c[0x24 + r]) * F::raw(0x3DCCCCCC);
        for (u32 m = 0; m < 2; ++m) {
            const u32 i = 2 * r + m;
            s.rate[i] = f_of(c[0x34 + i]) * deg, s.rate_back[i] = f_of(c[0x38 + i]) * deg, s.rate_toward[i] = f_of(c[0x3C + i]) * deg;
            s.fast[i] = f_of(c[0x44 + i]) * deg, s.fast_back[i] = f_of(c[0x48 + i]) * deg, s.fast_toward[i] = f_of(c[0x4C + i]) * deg;
        }
        s.by_speed[r] = c[0x2E + r] < c[0x30 + r] ? 1 : 0;
        s.speed_lo[r] = f_of(c[0x2E + r]) * kKmh, s.speed_hi[r] = f_of(c[0x30 + r]) * kKmh;
    }
    k.wheel_slew = f_of(c[0x54]) * deg;
    k.wheel_lock[0] = f_of(c[0x55]) * deg, k.wheel_lock[1] = f_of(c[0x56]) * deg;
    for (u32 j = 0; j < 2; ++j) {
        const u32 o = 0x144 + 4 * j; // (0x504A94 / 0x504A98: the set-up leaves them)
        k.wheel_lock[2 + j] = F::raw(u32(c[o]) | u32(c[o + 1]) << 8 | u32(c[o + 2]) << 16 | u32(c[o + 3]) << 24);
        const u8 b = c[0x59 + j];
        k.jog_scale[j] = b ? F::raw(0x44610000) / (f_of(60u * b) * F::raw(0x45800000)) : F::raw(0x39800000);
    }
    for (u32 s2 = 0; s2 < k.surface_class.size(); ++s2) k.surface_class[s2] = c[0x170 + s2];
    for (u32 r = 0; r < 3; ++r) k.prefilter_mode[r] = c[0x82 + 0x20 * r], k.prefilter_gain[r] = c[0x83 + 0x20 * r];
    k.wheel_throttle = rules::pedal_curve_of(c[0x64], c[0x65]), k.wheel_brake = rules::pedal_curve_of(c[0x68], c[0x69]);
    return k;
}

Tables tables(const Program& program) {
    Tables t;
    const PrologueProgram p = PrologueProgram::locate(program);
    if (!p.recognized()) return t;
    t.found = true;
    const u32 s = PrologueProgram::kStatic;
    u8 b[8];
    for (u32 k = 0; k < 8; ++k) b[k] = program.b(s + 0x84 + k);
    t.slip = rules::slip_bytes(b);
    for (u32 k = 0; k < 3; ++k) t.direct_speed[k] = program.b(s + 0xE4 + k);
    t.globals.camber_ref = F::raw(program.w(PrologueProgram::kCamberRef));
    t.globals.camber_gain = F::raw(program.w(PrologueProgram::kCamberGain));
    // (the image has 0; the race's set-up leaves it 1, ❓ its writer: a block 0x506610 the computer drivers' routines read)
    t.globals.mean_on = 1;
    {
        rules::SteerTables& g = t.steer;
        g.mode = program.b(s + 0xB9), g.scale = program.b(s + 0xBA);
        for (u32 k = 0; k < 2; ++k) {
            const u8 lim = program.b(s + 0xBB + k);
            if (lim == 0) g.mode = 0;
            g.limit[k] = gt3r::ee::int_to_f(lim) * F::raw(0x3AE4C385);
            g.spring[k] = gt3r::ee::int_to_f(program.b(s + 0xBD + k)) * F::raw(0x41200000);
            g.damping[k] = gt3r::ee::int_to_f(program.b(s + 0xBF + k));
        }
        for (u32 f = 0; f < 24; ++f) g.fixed_aim[f] = f < 16 ? program.b(PrologueProgram::kFixedAim + f) : 0;
        t.globals.steer_mode = g.mode;
    }
    {
        std::array<u8, kControlBytes> c{};
        for (u32 k = 0; k < kControlBytes; ++k) c[k] = program.b(PrologueProgram::kController + k);
        t.controls = controls(c.data());
    }
    { // 0x3B14A8
        const auto b = [&](u32 k) { return gt3r::ee::int_to_f(s32(program.b(PrologueProgram::kKind7Tire + k))); };
        const F milli = F::raw(0x3A83126E), kmh = F::raw(0x3E8E38E3), hundred = F::raw(0x42C80000);
        t.ride.on = program.b(PrologueProgram::kKind7Tire) != 0;
        t.ride.travel = b(1) * milli, t.ride.lift = b(2) / hundred;
        for (u32 a = 0; a < 2; ++a) t.ride.share[a] = b(3 + a) / hundred;
        t.ride.drop = b(5) * milli, t.ride.v0 = b(6) * kmh, t.ride.v1 = b(7) * kmh;
    }
    t.hybrid = rules::hybrid_tables(program, 0x504560, 0x5045B0, 0x504638, 0x504648);
    t.pedal_ramps = program.b(PrologueProgram::kController + 0x11) != 0;
    t.throttle_up = t.controls.throttle_up, t.throttle_down = t.controls.throttle_down;
    t.brake_up = t.controls.brake_up, t.brake_down = t.controls.brake_down;
    t.pitch_radius = program.b(s + 0xD9), t.normal_mode = program.b(s + 0xE0);
    {
        const u8 heat = program.b(s + 0x7C) ? program.b(s + 0x7C) : 1, cool = program.b(s + 0x7D) ? program.b(s + 0x7D) : 1;
        t.brake_heat.c0 = kOne / (f_of(heat) * F::raw(0x46191FFF));
        t.brake_heat.c1 = kOne / ((f_of(cool) * F::raw(0x3DCCCCCC)) * F::raw(0x42C80000));
        t.brake_heat.c2 = f_of(program.b(s + 0x7E)) * F::raw(0x3E8E38E3);
        t.brake_heat.c3 = f_of(program.b(s + 0x7F)) * F::raw(0x3C23D70A);
    }
    t.awd3.negative_full = program.b(PrologueProgram::kAwd3 + 2);
    t.awd3.fade_start = f_of(program.b(PrologueProgram::kAwd3 + 8)) / F::raw(0x42C80000), t.awd3.fade_floor = f_of(program.b(PrologueProgram::kAwd3 + 0x10)) / F::raw(0x42C80000);
    {
        const auto rate = [&](u32 o) { return f_of(program.b(PrologueProgram::kStepless + o)) * F::raw(0x3C23D70A); };
        t.stepless.full = rate(0), t.stepless.none = rate(1), t.stepless.down = rate(0xC), t.stepless.up = rate(0xD);
    }
    t.rolling_n = program.b(s + 0x0B);
    for (u32 k = 0; k < 8 && k < t.rolling_n; ++k) {
        t.rolling_x[k] = f_of(u32(program.b(s + 0x0C + k)) << 1) * F::raw(0x3E8E38E3);
        t.rolling_y[k] = f_of(program.b(s + 0x14 + k)) * F::raw(0x3A83126E);
    }
    for (u32 k = 0; k < 16; ++k) {
        t.bumps.amp[k] = f_of(program.b(s + 0x1C + k)) * F::raw(0x3A83126E);
        t.bumps.freq[k] = F::raw(0x40C90FDA) / (f_of(program.b(s + 0x4C + k)) * F::raw(0x3DCCCCCC));
        t.bumps.scaled[k] = gt3r::s8(program.b(s + 0x5C + k)), t.bumps.mode[k] = gt3r::s8(program.b(s + 0x6C + k));
    }
    for (u32 k = 0; k < 6; ++k) t.in_line[k] = F::raw(program.w(PrologueProgram::kInLine + 4 * k));
    for (u32 r = 0; r < t.wheel_layouts.size(); ++r)
        for (u32 k = 0; k < rules::kWheelLayoutSize; ++k) t.wheel_layouts[r][k] = program.b(PrologueProgram::kWheelLayouts + rules::kWheelLayoutSize * r + k);
    t.convert = convert_data(program);
    t.asm_keep = program.b(PrologueProgram::kAsmKeep);
    return t;
}

void statics(ex::Statics& s, const Tables& t) {
    if (!t.found) return;
    s.awd3 = t.awd3, s.stepless = t.stepless;
    for (u32 k = 0; k < 8 && k < t.rolling_n; ++k) s.rolling_curve.x[k] = t.rolling_x[k], s.rolling_curve.y[k] = t.rolling_y[k];
}

void race_inputs(ex::RaceInputs& r, const Tables& t) {
    if (!t.found) return;
    statics(r.statics, t);
    r.brake_heat = t.brake_heat;
    r.bumps.amp = t.bumps.amp, r.bumps.freq = t.bumps.freq, r.bumps.scaled = t.bumps.scaled, r.bumps.mode = t.bumps.mode;
}

rules::WheelLayout wheel_layout(const Tables& t, u8 row) {
    if (!t.found || row >= t.wheel_layouts.size()) return {};
    return rules::wheel_layout(t.wheel_layouts[row].data());
}

rules::TireExtra tire_extra(const ex::Params& p, const TireKinds& kinds, const Tables& t) {
    rules::TireExtra x;
    for (size_t r = 0; r < 6; ++r) {
        const ex::TireRecord& a = p.tire[r];
        rules::Tire& o = x.tire[r];
        o.grip = a.grip, o.wear_rate = a.wear_rate;
        for (size_t s = 0; s < 16; ++s) o.mu[s] = a.mu[s];
        o.wear_floor = a.mu[11];
        o.curve_a = a.curve_a, o.peak_slip = a.peak_slip, o.curve_b = a.curve_b, o.curve_c = a.curve_c;
        o.slip_drive = a.slip_drive, o.slip_brake = a.slip_brake, o.brake_peak = a.brake_peak;
        o.curve_d = a.curve_d, o.load_curve = a.load_curve;
        o.direct_slip = kinds[r][0], o.direct_lateral = kinds[r][1], o.x1da = kinds[r][2];
    }
    for (size_t a = 0; a < 2; ++a) {
        for (size_t s = 0; s < 16; ++s) x.tire_index[a][s] = p.tire_index[a][s];
        x.width[a] = p.width[a], x.camber_grip[a] = p.camber_grip[a], x.axle_grip[a] = p.axle_grip[a];
    }
    x.direct_lateral_scale = kHalfPi / speed_of(t.direct_speed[0]);
    return x;
}

void slip_ratios(ex::Car& c, const rules::TireExtra& x, const rules::WheelLayout& l, const Tables& t) {
    for (int i = 0; i < l.count && i < 4; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        const rules::Tire& r = rules::tire_record(x, l, i, w.surface);
        const F a = w.steer;
        const F along = gt3r::ee::cos(a) * w.hub_vx - gt3r::ee::sin(a) * w.hub_vy;
        if (!r.direct_slip) {
            rules::slip_ratio(c, x, i, r, along, t.slip);
            continue;
        }
        ex::Drive& d = c.t[size_t(i)];
        const F rim = w.rim_speed;
        F s = along - rim;
        if (F() <= rim) {
            const gt3r::s8 v = along <= rim ? 1 : -1;
            d.slip_state = v, d.slip_sign = v;
        } else {
            s = -s;
            if (rim <= along) d.slip_sign = -1, d.slip_state = 1;
            else d.slip_sign = 1, d.slip_state = -1;
        }
        // The speed by the state: driving +0xE6 (then +0xE5), braking +0xE5 (then +0xE6), then 50 km/h.
        const u8 first = d.slip_state == 1 ? t.direct_speed[2] : t.direct_speed[1];
        const u8 second = d.slip_state == 1 ? t.direct_speed[1] : t.direct_speed[2];
        s = s / speed_of(first ? first : second);
        w.slip_gain = kOne;
        if (kOne < s) s = kOne;
        else if (s < kMinusOne) s = kMinusOne;
        w.slip_ratio = s;
    }
}

TireKinds tire_kinds(const Block& p) {
    TireKinds k{};
    for (u32 r = 0; r < 6; ++r)
        for (u32 j = 0; j < 3; ++j) k[r][j] = p[0x398 + 0x1B8 * r + 0x1B4 + j];
    return k;
}

CarPart car_part(const ex::Car& c, const Block& p, const Tables& t) {
    CarPart x;
    x.layout = wheel_layout(t, p[0x30]);
    x.tires = tire_extra(c.p, tire_kinds(p), t);
    x.chassis.mass = c.p.mass;
    x.axle_load = {c.p.xe54, c.p.xe58};
    image_part(x, p.data());
    return x;
}

CarPart build_car(db::SpecDb& db, const Spec& s, const Equipment& eq, int course_kind, bool asm_on, bool tcs_on, const Tables& t, u32 base,
                  ex::Car& c, Block* block, const ConvertIn& in) {
    std::string label;
    const u32 car = db::part_row_id(eq.part(Equipment::Car));
    for (const db::CarEntry& e : db.cars())
        if (e.id == car) label = e.label;
    ex::Spec spec{};
    core_spec(s, label, course_kind, t.convert, spec);
    if (!asm_on) spec[0x109] = t.asm_keep ? u8(spec[0x109] & 0xC0) : 0;
    if (!tcs_on) spec[0x110] = 0;
    Block p{};
    convert(spec, t.convert, in, base, p);
    const ex::ParamImage image = engine_image(p, base);
    std::vector<u8> record(ex::kCarSize, 0);
    std::copy(image.begin(), image.end(), record.begin() + ex::kParams);
    ex::Car loaded;
    ex::load_image(loaded, record.data());
    c.p = loaded.p;
    if (block) *block = p;
    return car_part(c, p, t);
}

void image_part(CarPart& x, const u8* p) {
    auto word = [&](u32 o) { return F::raw(u32(p[o]) | u32(p[o + 1]) << 8 | u32(p[o + 2]) << 16 | u32(p[o + 3]) << 24); };
    x.tire_drop = {word(0x28C), word(0x290)};
    x.front_share = word(0x24);
    x.special_row = p[0x33], x.shift_class = p[0x20B];
    x.part_kind = p[0xE84];
    x.part_lo = word(0xE88), x.part_hi = word(0xE8C), x.part_rate = word(0xE90), x.part_lift = word(0xE94);
}

void wheel_kinds(const ex::Car& c, CarPart& x) {
    for (int w = 0; w < 4; ++w) x.tires.wheel_xdd[size_t(w)] = rules::tire_record(x.tires, x.layout, w, c.w[size_t(w)].surface).x1da;
}

void lateral_yaw(ex::Car& c, const CarPart& x, bool air, const Tables& t) {
    using gt3r::ee::cos;
    using gt3r::ee::sin;
    const rules::WheelLayout& l = x.layout;
    const rules::TireExtra& tx = x.tires;
    const int n = l.count < 4 ? l.count : 4;
    if (air) {
        c.lat_g = F(), c.a_y = F();
        return;
    }
    F lat = F(), torque = F();
    for (int i = 0; i < n; ++i) {
        ex::Wheel& w = c.w[size_t(i)];
        const F steer = w.steer;
        if (t.globals.steer_mode == 1) w.steer = w.steer + tx.steer_offset[size_t(i)];
        else if (t.globals.steer_mode == 2) w.steer = tx.steer_offset[size_t(i)];
        const F gx = w.fx_drive + w.fx_brake;
        F fa, fb;
        if (rules::tire_record(tx, l, i, w.surface).x1da == 1) {
            fa = -cos(w.steer) * w.force_a;
            fb = -sin(w.steer) * w.force_a;
        } else {
            const F cs = cos(w.slip_dir), sn = sin(w.slip_dir);
            fa = -cs * w.force_a + sn * w.force_b;
            fb = -sn * w.force_a + -cs * w.force_b;
        }
        const F cam = tx.camber_force[size_t(i)];
        fa = fa + -cos(w.steer) * cam;
        fb = fb + -sin(w.steer) * cam;
        const F hold = ex::low_speed_hold(w, true);
        fa = fa - cos(w.steer) * hold;
        fb = fb - sin(w.steer) * hold;
        fa = fa - sin(w.steer) * gx;
        fb = fb + cos(w.steer) * gx;
        lat = lat + fa;
        const u8 k = l.geometry[size_t(i)] & 1u;
        const u8 side = l.side[size_t(i)];
        const F lx = side == 0 ? -c.p.half_track[k] : side == 1 ? c.p.half_track[k] : F();
        const F dist = k ? c.p.dist_rear : c.p.dist_front;
        const gt3r::s8 fa_pos = gt3r::s8(l.fore_aft[size_t(i)]);
        const F ly = fa_pos > 0 ? dist : fa_pos < 0 ? -dist : F();
        torque = torque + (lx * fb - ly * fa);
        w.steer = steer;
    }
    c.yaw_accel = torque * c.p.inv_yaw_inertia;
    const F r = c.yaw_rate + c.yaw_accel * c.dt;
    const F hi = F::raw(0x40C90FDA), lo = F::raw(0xC0C90FDA);
    c.yaw_rate = hi < r ? hi : r < lo ? lo : r;
    c.a_y = lat * c.p.inv_mass;
    F grip = F();
    for (int i = 0; i < n; ++i) {
        const u8 s = c.w[size_t(i)].surface;
        const rules::Tire& r2 = rules::tire_record(tx, l, i, s);
        grip = grip + r2.mu[s % 24] * r2.grip;
    }
    c.lat_g = c.a_y * gt3r::ee::int_to_f(n) / grip;
}

void hybrid_start(const ex::Car& c, CarPart& x) {
    if (c.p.d.kind != 3) return;
    rules::hybrid_start(x.hybrid);
    x.hybrid_fuel = x.hybrid_fuel_mark = x.hybrid_odo_mark = F();
    x.mileage = 0, x.mileage_steps = 30;
}

int gear_for_speed(const ex::Car& c, F v) {
    if (v < F()) return 0;
    if (c.p.d.kind == 3 || c.p.d.kind == 1) return 1; // (one speed; stepless)
    const int n = c.p.d.gears;
    for (int k = 1; k < n; ++k) {
        const F w = gt3r::ee::int_to_f(s32(c.p.d.up_rpm) - (k == 1 ? 2000 : 500)) * F::raw(0x3DD6774E);
        if (v < w / c.p.d.gear_speed[size_t(k)]) return k;
    }
    return n;
}

void moving_start(ex::Car& c, F v) {
    ex::moving_start(c, v); // (the engine's but for the gear)
    const int g = prologue::gear_for_speed(c, v);
    if (g == int(c.gear)) return;
    c.gear = u8(g);
    const F rpm_to_w = F::raw(0x3DD6774E);
    const F idle = gt3r::ee::int_to_f(c.p.e.idle) * rpm_to_w, limit = gt3r::ee::int_to_f(c.p.e.rev_limit) * rpm_to_w;
    F w = v * c.p.d.gear_speed[size_t(g)];
    if (w < idle) w = idle;
    else if (limit < w) w = limit;
    c.omega = w;
    c.rpm = gt3r::s16(gt3r::ee::to_int((c.omega * F::raw(0x42700000)) / F::raw(0x40C90FDA)));
    if (!(c.p.e.boost_max == F())) {
        if (F() < c.p.e.turbo_peak[1]) c.turbo_lag[1] = c.rpm;
        c.turbo_lag[0] = c.rpm;
        c.intake = ex::boost(c.p.e, c.rpm, c.rpm);
        c.turbo_pitch = u16(c.rpm);
    }
}

u16 mileage(F fuel, F distance) {
    if (distance < F::raw(0x3A83126F)) return 0;
    const F liters = fuel / F::raw(0x44390000); // (740 g a liter)
    if (liters < F::raw(0x358637BD)) return 999;
    const F v = (distance * F::raw(0x3C23D70A)) / liters;
    return F::raw(0x4479C000) < v ? u16(999) : u16(gt3r::ee::to_int(v));
}

namespace {
// The axles with the hybrid's box in place of the engine and clutch (0x3CEAE0 with 0x3CC448): the engine's axle step with the
// box's torque given it as a slipping clutch's, the engine's speed the box's and its inertia not on the axle.
void hybrid_axles(ex::Car& c, CarPart& x, const ex::Statics& s, const Tables& t) {
    ex::Driveline& d = c.p.d;
    rules::HybridCar k;
    k.shift_class = x.shift_class, k.driven_axle = u8(d.driven_axle), k.ratio = d.ratio[c.gear];
    k.torque = [&](F throttle) { return ex::engine_torque(c, throttle); };
    k.speed = [&](F power) { // (0x3CBF50)
        ex::Table<24> e;
        e.n = c.p.e.torque.n;
        for (size_t i = 0; i < c.p.e.torque.x.size() && i < e.x.size(); ++i) e.x[i] = c.p.e.torque.x[i], e.y[i] = c.p.e.torque.y[i];
        const ex::Table<12> p = rules::hybrid_engine_speeds(e);
        return p.n > 0 ? ex::lookup_bisect(p.n, p.x.data(), p.y.data(), power) : F();
    };
    k.fuel = &x.hybrid_fuel, k.fuel_hourly = true;
    k.burnt = [&] {
        if (--x.mileage_steps != 0) return;
        x.mileage = mileage(x.hybrid_fuel - x.hybrid_fuel_mark, c.dist_total - x.hybrid_odo_mark);
        x.hybrid_odo_mark = c.dist_total, x.hybrid_fuel_mark = x.hybrid_fuel, x.mileage_steps = 30;
    };
    const F torque = rules::hybrid_box(c, k, x.hybrid, t.hybrid, c.p.e.idle);
    const u8 clutch = c.clutch;
    const F omega = c.omega, shift = d.shift_clutch_torque, ce = c.clutch_engine, cw = c.clutch_wheel;
    const F load = c.turbo_load, load_prev = c.turbo_load_prev;
    const auto spool = c.turbo_spool;
    const auto limiter = c.rev_limiter;
    if (clutch != 0) c.clutch = 3, d.shift_clutch_torque = torque, c.clutch_engine = c.clutch_wheel = F();
    ex::axle_step(c, s.awd3, s.stepless, s.hybrid);
    c.clutch = clutch, c.omega = omega, c.clutch_engine = ce, c.clutch_wheel = cw;
    d.shift_clutch_torque = shift;
    c.turbo_load = load, c.turbo_load_prev = load_prev, c.turbo_spool = spool, c.rev_limiter = limiter; // (what its own engine torque left)
}

// 0x3BABC8: the gearbox without steps: forward or reverse alone (the engine's for a stepless box, which also slips its clutch).
void one_speed_gear(ex::Car& c) {
    const F walk = F::raw(0x3E8E38E3);
    int g = -1;
    if (c.reverse_req != 0) {
        if (c.gear != 0) g = 0;
    } else if (walk < c.speed || F() < c.throttle) {
        if (c.gear == 0) g = 1;
    } else if (c.speed < -walk && c.gear != 0) g = 0;
    if (g >= 0) ex::set_gear(c, u8(g));
}
} // namespace

void dynamics(ex::Car& c, CarPart& x, const ex::Statics& s, const ex::GearSettings& g, const ex::ClassGrip& grip, const Tables& t) {
    const F request = c.clutch_req;
    if (c.p.d.kind == 3) c.clutch = 0, c.clutch_req = F(); // (the hybrid's by its request alone, 0x3B9DE8)
    ex::brakes_and_clutch(c);
    if (c.p.d.kind == 3) c.clutch_req = request, c.clutch = request == F() ? 0 : 1;
    if (c.p.special == 8) c.clutch = 1;
    ex::rolling_resistance(c, s);
    ex::tyre_curves(c);
    ex::class_grip_scale(c, grip);
    ex::tyre_long_force(c, s);
    rules::tire_slip_force(c, x.tires, x.layout, t.globals);
    if (t.globals.mean_on) c.x420 = x.tires.mean_slip;
    wheel_kinds(c, x);
    rules::forward_step(c, x.chassis, x.tires, x.layout);
    ex::wheel_net_torque(c);
    ex::torque_vectoring(c, s.hybrid);
    const bool hybrid = c.p.d.kind == 3;
    if (hybrid) hybrid_axles(c, x, s, t);
    else ex::axle_step(c, s.awd3, s.stepless, s.hybrid);
    ex::wheel_speeds(c);
    if (hybrid) {
        c.rpm = s16(gt3r::ee::to_int((c.omega * F::raw(0x42700000)) / F::raw(0x40C90FDA))); // (0x3CD338)
        one_speed_gear(c);
    } else {
        ex::engine_speed(c, s.hybrid);
        ex::gear_step(c, g);
    }
    const bool air = ex::airborne(c);
    lateral_yaw(c, x, air, t);
    ex::stop_hold(c, air);
}

namespace {

const F kHalf = F::raw(0x3F000000), kPi = F::raw(0x40490FDA);

// The aim of wheel w's steering offset.
F offset_aim(const ex::Car& c, const CarPart& x, const rules::SteerTables& g, int w) {
    const ex::Wheel& wh = c.w[size_t(w)];
    const bool fixed = wh.surface < g.fixed_aim.size() && g.fixed_aim[wh.surface] == 1;
    if (g.mode == 2) {
        if (fixed) return wh.steer;
        if (wh.slip_fade == F()) return x.tires.steer_offset[size_t(w)];
        if (wh.hub_vx < F()) return rules::angle_blend(kHalf, wh.slip_dir - kPi, kHalf, wh.steer);
        return (wh.slip_dir + wh.steer) * kHalf;
    }
    if (fixed || wh.slip_fade == F()) return F();
    if (wh.hub_vx < F()) return rules::angle_diff(wh.slip_dir - kPi, wh.steer) * kHalf;
    return (wh.slip_dir - wh.steer) * kHalf;
}

} // namespace

void wheel_steer(ex::Car& c, CarPart& x, const Tables& t) {
    ex::wheel_steer(c);
    const rules::SteerTables& g = t.steer;
    if (g.mode == 0) return;
    const rules::WheelLayout& l = x.layout;
    for (int w = 0; w < l.count && w < 4; ++w) {
        const size_t i = size_t(w), k = l.geometry[i] & 1u;
        const ex::Wheel& wh = c.w[i];
        const F aim = offset_aim(c, x, g, w);
        const F off = x.tires.steer_offset[i], rate = x.offset_rate[i];
        F push = g.spring[k] * (aim - off) - g.damping[k] * rate;
        F lim = g.limit[k];
        if (g.scale != 0) {
            const F share = (wh.peak_force * gt3r::ee::int_to_f(l.group_wheels[k])) / x.axle_load[k];
            if (g.scale == 2 || g.scale == 3) push = push * share;
            if (g.scale == 1 || g.scale == 3) lim = lim * share;
        }
        x.offset_rate[i] = rate + push * c.dt;
        x.tires.steer_offset[i] = off + x.offset_rate[i] * c.dt;
        const F o = x.tires.steer_offset[i];
        if (g.mode == 2) {
            const F d = o - wh.steer;
            if (lim < d) x.tires.steer_offset[i] = wh.steer + lim, x.offset_rate[i] = F();
            else if (d < -lim) x.tires.steer_offset[i] = wh.steer - lim, x.offset_rate[i] = F();
        } else {
            if (lim < o) x.tires.steer_offset[i] = lim, x.offset_rate[i] = F();
            else if (o < -lim) x.tires.steer_offset[i] = -lim, x.offset_rate[i] = F();
        }
    }
}

void steer_follow(ex::Car& c, const SteerFollow& k, int r, int m, F target, F mean) {
    const size_t a = size_t(r & 1), i = size_t(2 * (r & 1) + (m & 1));
    const F cmd = c.steer_cmd, dt = c.dt;
    F acc = (target - cmd) * k.gain[a] - c.steer_rate * k.damping[a];
    F lim = k.rate[i], fast = k.fast[i];
    if (!(k.toward[a] == F()) && ((cmd < target && cmd < mean) || (target < cmd && mean < cmd))) {
        acc = acc + (mean - cmd) * k.toward[a]; // (toward the slip direction, beyond the command on the target's side)
        lim = k.rate_toward[i], fast = k.fast_toward[i];
    } else if ((F() < cmd && target < cmd) || (cmd < F() && cmd < target)) { // returning toward the center
        acc = acc - cmd * k.centering[a];
        lim = k.rate_back[i], fast = k.fast_back[i];
    }
    F w = c.steer_rate + acc * dt;
    if (k.by_speed[a] != 0) {
        const F v = c.speed, lo = k.speed_lo[a], hi = k.speed_hi[a];
        if (lo < v) lim = hi <= v ? fast : lim + ((fast - lim) * (v - lo)) / (hi - lo);
    }
    if (lim < w) w = lim;
    else if (w < -lim) w = -lim;
    const F next = cmd + w * dt;
    if ((cmd <= target && target <= next) || (target <= cmd && next <= target)) {
        c.steer_cmd = target;
        c.steer_rate = F();
        return;
    }
    c.steer_rate = w;
    c.steer_cmd = next;
}

void pad_steering(ex::Car& c, const ex::PadFrame& f, u8 course_kind, const rules::WheelLayout& l, const Controls& k) {
    const int n = l.count < 4 ? l.count : 4;
    // The steered wheels' (ahead of the center of gravity) mean slip direction and least surface class.
    F mean = F();
    int steered = 0;
    for (int w = 0; w < n; ++w)
        if (gt3r::s8(l.fore_aft[size_t(w)]) > 0) mean = mean + c.w[size_t(w)].slip_dir, ++steered;
    mean = mean / gt3r::ee::int_to_f(steered);
    u8 cls = 2;
    for (int w = 0; w < n; ++w)
        if (gt3r::s8(l.fore_aft[size_t(w)]) > 0) {
            const u8 s = c.w[size_t(w)].surface;
            const u8 v = s < k.surface_class.size() ? k.surface_class[s] : 0;
            if (v < cls) cls = v;
        }
    if (cls >= 2) cls = 0;
    const bool dirt = course_kind == 2;
    int rate = dirt ? 1 : 0;
    F v = c.speed;
    if (v < F()) v = F();
    F hi;
    if (f.kind == 6) hi = c.p.max_lock;
    else if (f.kind == 4) hi = ex::lookup(dirt ? c.p.steer_c : c.p.steer_a, v);
    else {
        rate = cls;
        hi = ex::lookup(rate == 1 ? c.p.steer_c : c.p.steer_b, v);
    }
    bool extend = f.kind != 6; // the slip direction's extension
    if (f.kind == 4) {
        const gt3r::s8 type = c.wheel_type;
        if (type == 1 || type == 2 || type == 3) { // the steering wheel's least lock
            const F least = k.wheel_lock[size_t((type == 2 ? 2 : 0) + (dirt ? 1 : 0))];
            if (hi < least) hi = least;
        }
        extend = !(type == 3 || type == 4);
    }
    F lo = -hi;
    if (extend && !(lo <= mean && mean <= hi)) {
        if (F() < mean) hi = mean;
        else if (mean < F()) lo = mean;
    }
    const F max = c.p.max_lock;
    if (max < hi) hi = max;
    if (lo < -max) lo = -max;

    const F stick = F::raw(0x39800000); // 1 / 4096
    if (!(f.flags & 1)) { // digital
        c.steer_class = 1;
        c.lock_hi = hi, c.lock_lo = lo;
        const F target = f.steer > 0 ? hi : f.steer < 0 ? lo : F();
        steer_follow(c, k.follow, rate, 0, target, mean);
    } else if (f.kind == 4) { // the steering wheel: its position is the command
        c.steer_class = 0;
        const F s = gt3r::ee::int_to_f(f.steer) * stick;
        const gt3r::s8 type = c.wheel_type;
        if (type >= 0 && type < 4) {
            ex::slew(c.lock_hi, hi, k.wheel_slew * c.dt);
            ex::slew(c.lock_lo, lo, k.wheel_slew * c.dt);
            c.steer_cmd = F() <= s ? c.lock_hi * s : -c.lock_lo * s;
        } else if (type == 4) c.steer_cmd = c.p.max_lock * s;
        else c.steer_cmd = F();
        return;
    } else if (f.kind == 6) { // the position scaled, within the full lock
        c.steer_class = 3;
        F s = gt3r::ee::int_to_f(f.steer) * k.jog_scale[dirt ? 1 : 0];
        if (kOne < s) s = kOne;
        else if (s < kMinusOne) s = kMinusOne;
        c.steer_cmd = c.p.max_lock * s;
        return;
    } else if (f.kind == 2 || f.kind == 3) {
        c.lock_hi = hi, c.lock_lo = lo;
        const F s = gt3r::ee::int_to_f(f.steer) * stick;
        const F target = F() <= s ? hi * s : -lo * s;
        if (f.steer != 0) c.steer_class = f.kind == 3 ? 1 : 2;
        steer_follow(c, k.follow, rate, c.steer_class == 1 ? 0 : 1, target, mean);
    }
    // The HUD's steering percentage for the followed kinds (0x3DDC40: of 15 degrees, within +-100).
    if (c.steer_class == 1 || c.steer_class == 2) {
        F x = c.steer_cmd * F::raw(0x43BEFC68);
        if (F::raw(0x42C80000) < x) x = F::raw(0x42C80000);
        else if (x < F::raw(0xC2C80000)) x = F::raw(0xC2C80000);
        c.steer_pct = gt3r::s8(gt3r::ee::to_int(x));
    }
}

ex::PadFrame prefilter(ex::Car& c, const ex::PadFrame& in, const Controls& k, u8 special_row) {
    ex::PadFrame f = in;
    const size_t row = f.kind != 6 ? 0 : special_row ? 2 : 1;
    const u8 mode = k.prefilter_mode[row];
    const s32 gain = k.prefilter_gain[row];
    const s32 pos = c.wheel_pos, prev = c.wheel_pos_prev; // (signed halves)
    const s32 d1 = mode ? s32(f.steer) - pos : 0, d0 = mode ? pos - prev : 0;
    c.wheel_pos_prev = c.wheel_pos;
    c.wheel_pos = f.steer;
    if (mode == 0) return f;
    s32 s = s32(f.steer) + gain * d1;
    if (mode >= 2) s = s + gain * gain * (d1 - d0);
    f.steer = gt3r::s16(s < -0x1000 ? -0x1000 : s > 0x1000 ? 0x1000 : s);
    return f;
}

namespace {
F analog_pedal(const Controls& k, F p) { return p + k.analog_jump * (kOne - p); }
F ease(F from, F to, F up, F down) {
    if (from < to) {
        const F v = from + up;
        return to < v ? to : v;
    }
    if (!(to < from)) return from;
    const F v = from - down;
    return v < to ? to : v;
}
F up_to_one(F v, F step) {
    v = v + step;
    return kOne < v ? kOne : v;
}
F down_to_zero(F v, F step) {
    v = v - step;
    return v < F() ? F() : v;
}

bool stepless(const ex::Driveline& d) { return u8(d.kind - 1) < 2; } // 0x3DB658

// 0x3DD608: the clutch request by the pedal style.
void clutch_by_style(ex::Car& c, const ex::PadFrame& f, u8 style) {
    const F held = F::raw(0x3F4CCCCC), walk = F::raw(0x3E8E38E3); // (0.8; 1 km/h)
    c.clutch_req = F();
    if (c.race_hold != 0 || c.hold != 0 || c.driver_kind == 8) return;
    if (style == 3) { // (the brake axis is the clutch pedal)
        const F b = kOne - c.brake_pedal;
        c.brake_pedal = F();
        c.clutch_req = b;
        if (b == F()) return;
        c.reverse_req = 0, c.shift_req = 0;
        return;
    }
    if (style == 6 && (f.shift != 0 || s16(f.back) != 0)) return;
    if (c.p.d.driven_axle == 1 && held < c.handbrake) return;
    if (stepless(c.p.d) && held < c.handbrake && gt3r::ee::sqrt(c.speed * c.speed + c.lat_speed * c.lat_speed) < walk) return;
    if (c.clutch == 0) {
        if (c.brake_pedal < F::raw(0x3E800000)) c.clutch_req = kOne;
        return;
    }
    if (!(s32(c.rpm) < s32(c.p.e.idle))) c.clutch_req = kOne;
}
} // namespace

void pad_pedals(ex::Car& c, const ex::PadFrame& f, const Tables& t) {
    const Controls& k = t.controls;
    const F kStick = F::raw(0x39800000), walk = F::raw(0x3E8E38E3); // (1 / 4096; 1 km/h)
    const bool wheel = f.kind == 4 || f.kind == 6, hybrid = c.p.d.kind == 3;
    const u8 style = c.control != 1 && c.driver_kind == 0 ? c.pedal_style : 0;

    // The throttle and the back axis.
    bool th_analog = (f.flags & 2) != 0, back_analog = (f.flags & 8) != 0;
    u32 th = th_analog ? f.throttle : u32(f.throttle) << 12 & 0xFFFF, back = back_analog ? f.back : u32(f.back) << 12 & 0xFFFF;
    if (c.hold != 0) back = 0;
    if (style <= 2) { // (the back axis replaces a smaller throttle)
        if (back < th) back = 0;
        else th_analog = back_analog, th = back;
    } else if (style == 5) back = 0;
    c.reverse_req = back != 0;
    F p = F();
    u8 state = 0;
    if (!th_analog) {
        p = c.throttle_pedal;
        if (th != 0) {
            p = up_to_one(p, k.throttle_up);
            c.throttle_state = state = back != 0 ? 2 : 1;
        } else {
            p = p - k.throttle_down;
            if (p < F()) c.throttle_state = 0, p = F();
            state = c.throttle_state;
            if (state == 2) c.reverse_req = 1;
        }
    } else if (th != 0) {
        p = analog_pedal(k, gt3r::ee::int_to_f(s32(th)) * kStick);
        if (!wheel && t.pedal_ramps) p = ease(c.throttle_pedal, p, k.throttle_up, k.throttle_down);
        c.throttle_state = 0;
    } else if ((state = c.throttle_state) != 0) {
        p = c.throttle_pedal - k.throttle_down;
        if (p <= F()) c.throttle_state = state = 0, p = F();
        if (state == 2) c.reverse_req = 1;
    }
    if (state == 0 && wheel && !hybrid) p = rules::pedal_curve(k.wheel_throttle, p);
    c.throttle_pedal = p;

    // The brake and the third axis.
    if (!(f.flags & 4)) c.brake_pedal = s16(f.brake) != 0 ? up_to_one(c.brake_pedal, k.brake_up) : down_to_zero(c.brake_pedal, k.brake_down);
    else {
        F b = gt3r::ee::int_to_f(s16(f.brake)) * kStick;
        if (!(b == F())) b = analog_pedal(k, b);
        if (wheel) b = rules::pedal_curve(k.wheel_brake, b);
        else if (t.pedal_ramps) b = ease(c.brake_pedal, b, k.brake_up, k.brake_down);
        c.brake_pedal = b;
    }
    c.shift_req = c.hold != 0 ? gt3r::s8(0) : f.shift;
    if (!(f.flags & 0x10)) c.handbrake = s16(f.third) != 0 ? up_to_one(c.handbrake, k.third_up) : down_to_zero(c.handbrake, k.third_down);
    else {
        const F h = gt3r::ee::int_to_f(s16(f.third)) * kStick;
        c.handbrake = h == F() ? h : analog_pedal(k, h);
    }

    // The clutch by the car and its gearbox.
    if (c.p.special == 8) {
        if (c.race_hold != 0) c.throttle_pedal = F();
        c.clutch_req = kOne;
        return;
    }
    const bool released = s16(f.throttle) == 0 && s16(f.back) == 0;
    if (hybrid) {
        rules::hybrid_clutch_request(c);
        if (c.clutch_req == F()) return;
        const F v = c.gear != 0 ? c.speed : -c.speed, walk_top = F::raw(0x40855554); // (15 km/h)
        if (-walk <= v) { // (0x3CC368)
            F share = (walk_top - v) / walk_top;
            if (share < F()) share = F();
            share = share * F::raw(0x3E999999);
            c.throttle_pedal = share + (kOne - share) * c.throttle_pedal;
        }
        if (released && c.speed < -walk) c.reverse_req = 1;
        return;
    }
    pad_clutch(c, f, style);
    if (!stepless(c.p.d)) return;
    if (!(c.handbrake < F::raw(0x3F4CCCCC)) && !(walk <= gt3r::ee::sqrt(c.speed * c.speed + c.lat_speed * c.lat_speed))) return;
    // (the throttle at least what holds the engine 500 rpm above its idle)
    const F q = c.torque_friction / (c.torque_wot + c.torque_friction);
    F floor = ((gt3r::ee::int_to_f(c.p.e.idle) + F::raw(0x43FA0000)) - gt3r::ee::int_to_f(s16(c.rpm))) * F::raw(0x3A03126E) + q;
    if (floor < F()) floor = F();
    else if (kOne < floor) floor = kOne;
    if (c.throttle_pedal < floor) c.throttle_pedal = floor;
    if (released && c.gear == 0) c.reverse_req = 1;
}

void driver_input_pad(ex::Car& c, const ex::PadConstants&, const ex::PadFrame& in, u8 course_kind, const rules::WheelLayout& l,
                      const Tables& t, u8 special_row) {
    const ex::PadFrame f = prefilter(c, in, t.controls, special_row);
    c.full_throttle = 1;
    ex::pad_hud(c, f);
    if (c.p.special != 4) pad_steering(c, f, course_kind, l, t.controls); // (4, the in-line lean's 0x3DE310, no car of the disc)
    pad_pedals(c, f, t);
    c.throttle = c.throttle_pedal;
    ex::set_brake(c, c.brake_pedal);
}

void pad_watched(ex::Car& c, const ex::PadFrame& in, const Tables& t, u8 special_row) {
    const ex::PadFrame f = prefilter(c, in, t.controls, special_row);
    c.full_throttle = 1;
    if (c.control == 0) ex::pad_hud(c, f);
}

void pitch_terms(const ex::Car& c, const rules::WheelLayout& l, const Tables& t, F& moment, F& lift) {
    F m = F(), up = F();
    const int n = l.count < 4 ? l.count : 4;
    for (int w = 0; w < n; ++w) {
        const ex::Wheel& wh = c.w[size_t(w)];
        const size_t a = l.axle[size_t(w)] & 1u, geo = l.geometry[size_t(w)] & 1u;
        const F f = (gt3r::ee::cos(wh.steer) * (wh.fx_drive + wh.fx_brake)) * (t.pitch_radius ? c.p.tire_radius[a] : c.p.width[a]);
        m = m + f * c.p.pitch_tire[geo];
        const gt3r::s8 fa = gt3r::s8(l.fore_aft[size_t(w)]);
        const F dist = geo ? c.p.dist_rear : c.p.dist_front;
        if (fa > 0) up = up + f / dist;
        else if (fa < 0) up = up - f / dist;
    }
    moment = m, lift = up;
}

void attitude(ex::Car& c, const rules::WheelLayout& l, const Tables& t, F in_line_lean) {
    using gt3r::ee::cos;
    using gt3r::ee::sin;
    const F kRate = F::raw(0x3FC90FDA), kRollRate = F::raw(0x41490FDA), kAngle = F::raw(0x3F490FD9), kGravity = F::raw(0x411CCCCC);
    const F kHalf = F::raw(0x3F000000);
    const int n = l.count < 4 ? l.count : 4;
    const F dt = c.dt;
    F moment, lift;
    pitch_terms(c, l, t, moment, lift);
    // The start's hold (0x376330): the engine's settling and turn toward the box, Gran Turismo 4's axes.
    if (c.hold != 0 && ex::spawn_hold(c, F())) {
        rules::body_axes(c);
        return;
    }
    // Pitch: the axles' body forces and dampers about their levers, and the tires' moment.
    std::array<F, 4> s{};
    for (int w = 0; w < n; ++w) s[l.axle[size_t(w)] & 3] = s[l.axle[size_t(w)] & 3] + c.body_force[size_t(w)];
    for (int w = 0; w < n; ++w) s[l.axle[size_t(w)] & 3] = s[l.axle[size_t(w)] & 3] + c.w[size_t(w)].damper_f;
    F rate = c.pitch_rate + (((s[0] * c.p.pitch_lever[0]) - (s[1] * c.p.pitch_lever[1])) + moment) * dt;
    if (kRate < rate) rate = kRate;
    else if (rate < -kRate) rate = -kRate;
    F angle = c.pitch + rate * dt;
    if (angle < -kAngle) angle = -kAngle, rate = F();
    else if (kAngle < angle) angle = kAngle, rate = F();
    c.pitch = angle, c.pitch_rate = rate;
    // Roll: the same by side (left plus, right minus) per axle.
    s = {};
    for (int pass = 0; pass < 2; ++pass)
        for (int w = 0; w < n; ++w) {
            const u8 side = l.side[size_t(w)];
            const F v = pass == 0 ? c.body_force[size_t(w)] : c.w[size_t(w)].damper_f;
            F& at = s[l.axle[size_t(w)] & 3];
            if (side == 0) at = at + v;
            else if (side == 1) at = at - v;
        }
    if (l.kind == 1) { // two wheels in line: a lean of their own
        F lean = -c.x4d0 * t.in_line[0];
        const F k = t.in_line[1];
        if (F() < k) {
            const F roll = c.roll;
            const F hw = c.p.width[0] * kHalf;
            const F tn = gt3r::ee::tan(roll), sn = sin(roll), cs = cos(roll);
            const F lean_angle = ex::atan2(rules::behaviour(), k * sn - hw * tn, k * cs);
            const F arm = (k * cos(roll)) / cos(lean_angle);
            lean = lean + ((c.p.mass * kGravity) * sin(lean_angle)) * arm;
        }
        lean = lean - in_line_lean * t.in_line[2];
        std::array<F, 4> spin{};
        for (int w = 0; w < n; ++w)
            spin[size_t(w)] = (c.w[size_t(w)].rim_speed / c.p.tire_radius[l.axle[size_t(w)] & 1u]) * cos(c.w[size_t(w)].steer);
        lean = lean + (t.in_line[3] * spin[0]) * c.inv_mass_eff;
        lean = lean + (t.in_line[4] * spin[0]) * c.yaw_rate;
        lean = lean + (t.in_line[5] * spin[1]) * c.yaw_rate;
        s[1] = s[1] + lean, s[0] = s[0] + lean;
    }
    rate = c.roll_rate + ((s[0] * c.p.roll_lever[0]) + (s[1] * c.p.roll_lever[1])) * dt;
    if (kRollRate < rate) rate = kRollRate;
    else if (rate < -kRollRate) rate = -kRollRate;
    angle = c.roll + rate * dt;
    if (angle < -kAngle) angle = -kAngle, rate = F();
    else if (kAngle < angle) angle = kAngle, rate = F();
    c.roll = angle, c.roll_rate = rate;
    rules::body_axes(c);
    // Heave.
    const u8 mode = t.normal_mode != 0 ? t.normal_mode : l.kind == 1 ? 1 : 0;
    if (mode == 0) { // the body forces and dampers along the body's up axis
        F sum = F();
        for (int w = 0; w < n; ++w) sum = sum + c.body_force[size_t(w)];
        for (int w = 0; w < n; ++w) sum = sum + c.w[size_t(w)].damper_f;
        sum = (sum + lift) * c.p.inv_sprung_mass;
        c.heave_acc = sum - c.up.z * kGravity;
        const F du = sum * dt;
        c.vel.x = c.vel.x + c.up.x * du, c.vel.y = c.vel.y + c.up.y * du, c.vel.z = c.vel.z + c.up.z * du;
        c.vel.z = c.vel.z - dt * kGravity;
        return;
    }
    // Each wheel's load: its ground's normal (mode 2) or the body's up axis against the ground's up axis, along the latter.
    const std::array<F, 3> g{c.ground_up.x, c.ground_up.y, c.ground_up.z};
    std::array<F, 3> acc{};
    for (int w = 0; w < n; ++w) {
        std::array<F, 3> d;
        if (mode == 2) {
            const ex::Vec3& nn = c.g[size_t(w)].normal;
            d = {nn.x, -nn.z, nn.y};
        } else d = {c.up.x, c.up.y, c.up.z};
        F f = F();
        for (size_t i = 0; i < 3; ++i) f = f + g[i] * d[i];
        f = f * c.w[size_t(w)].load;
        for (size_t i = 0; i < 3; ++i) acc[i] = acc[i] + f * g[i];
    }
    const F up = lift - (c.downforce[0] + c.downforce[1]);
    const std::array<F, 3> u{c.up.x, c.up.y, c.up.z};
    for (size_t i = 0; i < 3; ++i) acc[i] = acc[i] + up * u[i];
    for (size_t i = 0; i < 3; ++i) acc[i] = acc[i] * c.p.inv_sprung_mass;
    acc[2] = acc[2] - kGravity;
    F heave = F();
    std::array<F*, 3> v{&c.vel.x, &c.vel.y, &c.vel.z};
    for (size_t i = 0; i < 3; ++i) {
        *v[i] = *v[i] + acc[i] * dt;
        heave = heave + acc[i] * u[i];
    }
    c.heave_acc = heave;
}

namespace {

// 0x3753C8: the hub's rest point under the wheel's travel at its static place.
F rest_drop(const ex::Car& c, const CarPart& x, int w) {
    const rules::WheelLayout& l = x.layout;
    const size_t a = w < l.count ? (l.axle[size_t(w)] & 1u) : 0;
    const ex::Suspension& s = c.p.susp[a];
    return (-s.x_static - (c.p.tire_radius[a] - x.tire_drop[a])) + s.offset;
}

// 0x3B4390: the hubs' heights from the body's axes, by the wheel layout.
void hub_heights(ex::Car& c, const rules::WheelLayout& l) {
    const int n = l.count < 4 ? l.count : 4;
    for (int w = 0; w < n; ++w) {
        ex::Wheel& wh = c.w[size_t(w)];
        const size_t g = l.geometry[size_t(w)] & 1u;
        const u8 side = l.side[size_t(w)];
        const F h = c.lat.z * c.p.half_track[g];
        if (side == 0) wh.hub.z = -h;
        else if (side == 1) wh.hub.z = h;
        else wh.hub.z = F();
        const F d = c.fwd.z * (g ? c.p.dist_rear : c.p.dist_front);
        const gt3r::s8 fa = gt3r::s8(l.fore_aft[size_t(w)]);
        if (fa > 0) wh.hub.z = wh.hub.z + d;
        else if (fa < 0) wh.hub.z = wh.hub.z - d;
    }
}

} // namespace

void query_points(ex::Car& c, const CarPart& x) {
    const rules::WheelLayout& l = x.layout;
    const int n = l.count < 4 ? l.count : 4;
    for (int w = 0; w < n; ++w) {
        const ex::Wheel& wh = c.w[size_t(w)];
        const F drop = rest_drop(c, x, w);
        ex::Ground& g = c.g[size_t(w)];
        g.x = (c.pos.x + wh.hub.x) + c.up.x * drop;
        g.h = (c.pos.z + wh.hub.z) + c.up.z * drop;
        g.z = -((c.pos.y + wh.hub.y) + c.up.y * drop);
    }
}

void road_bumps(ex::Car& c, const CarPart& x, const ex::SurfaceBumps& b) {
    const rules::WheelLayout& l = x.layout;
    const int n = l.count < 4 ? l.count : 4;
    for (int w = 0; w < n; ++w) {
        ex::Wheel& wh = c.w[size_t(w)];
        const ex::Ground& g = c.g[size_t(w)];
        if (g.miss) {
            wh.bump = F();
            continue;
        }
        const size_t s = wh.surface & 15;
        const F amp = b.amp[s];
        if (amp == F()) {
            wh.bump = F();
            continue;
        }
        F v = ex::bump_wave(b.mode[s], g.x, g.z, b.freq[s]);
        if (b.scaled[s]) v = v * wh.speed_ramp;
        wh.bump = v * amp;
    }
}

ex::GroundCounts ground_contact(ex::Car& c, const CarPart& x, const ex::CourseGround& course, const ex::SurfaceBumps& b, bool mark_flagged) {
    query_points(c, x);
    course.query(c.g, c.behaviour);
    const rules::WheelLayout& l = x.layout;
    const int n = l.count < 4 ? l.count : 4;
    for (int w = 0; w < n; ++w) {
        ex::Wheel& wh = c.w[size_t(w)];
        const ex::Ground& g = c.g[size_t(w)];
        if (g.miss) continue;
        wh.ground0 = g.height, wh.ground = g.height;
        wh.surface = u8(ex::surface_index(g));
        wh.x25 = g.x32, wh.surface_flags = g.flags;
    }
    road_bumps(c, x, b);
    const ex::GroundCounts k = ex::ground_counts(c, b);
    c.x535 = u8(c.x535 & 0xEF);
    c.road_seg = c.g[0].seg;
    if (k.flagged && mark_flagged) c.x535 = u8(c.x535 | 0x10);
    return k;
}

void susp_coords(ex::Car& c, const CarPart& x) {
    const rules::WheelLayout& l = x.layout;
    const int n = l.count < 4 ? l.count : 4;
    for (int w = 0; w < n; ++w) {
        const ex::Wheel& wh = c.w[size_t(w)];
        const size_t a = l.axle[size_t(w)] & 1u;
        const F drop = rest_drop(c, x, w);
        const F hub = (c.pos.z + wh.hub.z) + c.up.z * drop;
        F h = (hub - (wh.ground0 + wh.bump)) * c.ground_up.z;
        h = h / c.terrain;
        c.susp_x[size_t(w)] = ((h - c.p.tire_radius[a]) + c.p.susp[a].offset) - drop;
    }
}

void terrain(ex::Car& c, const CarPart& x) {
    F d = F();
    d = d + c.ground_up.x * c.up.x;
    d = d + c.ground_up.y * c.up.y;
    d = d + c.ground_up.z * c.up.z;
    c.terrain = d;
    susp_coords(c, x);
    c.bump_mask = u8(rules::bump_mask(c, x.layout, 0));
}

void bump_nudge(ex::Car& c, const CarPart& x) {
    const rules::WheelLayout& l = x.layout;
    const F kHalf = F::raw(0x3F000000);
    if (c.bump_mask == 0) return;
    hub_heights(c, l);
    susp_coords(c, x);
    const u32 mask = rules::bump_mask(c, l, u32(c.bump_mask) << 4);
    c.bump_mask = u8(mask);
    const int n = l.count < 4 ? l.count : 4;
    if (mask & 0xF) {
        std::array<F, 4> d{};
        for (int w = 0; w < n; ++w) {
            const F p = c.p.susp[w < l.count ? (l.axle[size_t(w)] & 1u) : 0].bump_limit - c.susp_x[size_t(w)];
            d[size_t(w)] = p < F() ? F() : p;
        }
        F front = F(), rear = F(), left = F(), right = F();
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
        F lift = F();
        if (!(left == F() && right == F())) {
            if (right < left) {
                c.roll = c.roll + ex::atan2(rules::behaviour(), left - right, c.p.track);
                lift = right * kHalf;
            } else if (left < right) {
                c.roll = c.roll + ex::atan2(rules::behaviour(), left - right, c.p.track);
                lift = left * kHalf;
            } else lift = left * kHalf;
        }
        if (!(front == F() && rear == F())) {
            F half;
            if (rear < front) {
                c.pitch = c.pitch + ex::atan2(rules::behaviour(), front - rear, c.p.wheelbase);
                half = rear * kHalf;
            } else if (front < rear) {
                c.pitch = c.pitch + ex::atan2(rules::behaviour(), front - rear, c.p.wheelbase);
                half = front * kHalf;
            } else half = front * kHalf;
            lift = lift + half;
        }
        c.pos.z = c.pos.z + lift;
    }
    rules::body_axes(c);
    // The downward speed held to the vertical part of the velocity in the ground's plane.
    F along = F(), side = F();
    along = along + c.ground_fwd.x * c.vel.x, side = side + c.ground_lat.x * c.vel.x;
    along = along + c.ground_fwd.y * c.vel.y, side = side + c.ground_lat.y * c.vel.y;
    along = along + c.ground_fwd.z * c.vel.z, side = side + c.ground_lat.z * c.vel.z;
    F floor = along * c.ground_fwd.z + side * c.ground_lat.z;
    if (F() < floor) floor = F();
    if (c.vel.z < floor) c.vel.z = floor;
}

void ground_frame(ex::Car& c) {
    ex::ground_frame(c);
    rules::body_axes(c.ground_pitch, c.ground_roll, c.heading, c.ground_fwd, c.ground_lat, c.ground_up);
}

} // namespace gt4r::sim::prologue
