// Gran Turismo 4's own routines of the race step: the driver's input, phase 5 (rules_input.h).

#include "gt4r/sim/rules_input.h"

#include "base/ee_math.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/record.h"
#include "sim/exact/build_math.h"
#include "sim/exact/tick.h"

#include <cstring>

namespace gt4r::sim::rules {

namespace {

using gt3r::ee::int_to_f;

const F k0 = F::raw(0), kOne = F::raw(0x3F800000), kMinusOne = F::raw(0xBF800000);
const F kStick = F::raw(0x39800000);         // 1 / 4096
const F kHundredth = F::raw(0x3C23D70A);     // 0.01
const F kTenth = F::raw(0x3DCCCCCC);         // 0.1, a unit low
const F kQuarter = F::raw(0x3E800000);
const F kDeg = F::raw(0x3C8EFA34);           // pi / 180, a unit low
const F kTenthDeg = F::raw(0x3AE4C385);      // pi / 1800
const F kTwoPi = F::raw(0x40C90FDA);
const F kKmh = F::raw(0x3E8E38E3);           // 1 / 3.6
const F kTenKmh = F::raw(0x4031C71B);        // 10 km/h in m/s
const F kWalk = F::raw(0x3E8E38E3);          // 1 km/h in m/s
const F kBackLimit = F::raw(0x40B1C71B);     // 20 km/h: the back axis is off above it
const F kActiveSpeed = F::raw(0x41055554);   // 30 km/h: the active steering's least speed
const F kHandbrakeHeld = F::raw(0x3F4CCCCC); // 0.8
const F kCreepRpm = F::raw(0x43FA0000), kCreepGain = F::raw(0x3A03126E); // 500 rpm, 0.0005
const F kCreepSpeed = F::raw(0x40855554), kCreepShare = F::raw(0x3E999999); // 15 km/h, 0.3
const F kGravity = F::raw(0x411CCCCC);       // 9.8, a unit low
const F kHalfPi = F::raw(0x3FC90FDA);
const F kSteerUnit = F::raw(0x40747648);     // 12 / pi
const F kComputerView = F::raw(0x3F2AACD9);  // 0.6667
const F kWheelView = F::raw(0x3F8E38E3);     // 1.1111
const F kWheel6View = F::raw(0x40A00000);    // 5
const F kSixty = F::raw(0x42700000), kHundred = F::raw(0x42C80000), kNineHundred = F::raw(0x44610000), k4096 = F::raw(0x45800000);
const F kThree = F::raw(0x40400000), kFour = F::raw(0x40800000), kEight = F::raw(0x41000000);
const F kThousandth = F::raw(0x3A83126E);

F f_of(s32 v) { return int_to_f(v); }
bool stepless(const ex::Driveline& d) { return u8(d.kind - 1) < 2; } // 0x3F1F98
bool one_speed(const ex::Driveline& d) { return d.kind == 3; }      // 0x368398
s16 h(u16 v) { return gt3r::s16(v); }

// x + j (1 - x): an analog axis once it moves.
F jump(const PadTables& t, F x) { return x + t.analog_jump * (kOne - x); }

// 0x35A818: a pedal curve from its kind and percentage.
PedalCurve make_curve(u32 kind, s32 pct) {
    PedalCurve k;
    k.kind = kind;
    if (kind == 1) {
        const s32 p = pct > 0 ? (pct < 101 ? pct : 100) : 0;
        k.c = f_of(p) * kHundredth;
        if (k.c < kThousandth) k.c = kThousandth;
        const F c = k.c, two = c + c, rest = -(kOne - c);
        k.a = rest / two;
        k.b = ((-c) * k.a) * k.a;
    } else if (kind == 2) {
        const s32 p = pct >= 0 ? (pct < 101 ? pct : 100) : 0;
        const F c = f_of(100 - p) * kHundredth + kOne, c2 = c * c;
        k.c = c;
        const F m = c2 - kOne, q = m * m;
        k.a = (-q) / (c2 * kFour);
        k.b = (-c) * gt3r::ee::sqrt(-k.a);
    } else if (kind == 3) {
        const s32 p = pct > 0 ? (pct < 101 ? pct : 100) : 0;
        k.a = k0;
        k.c = f_of(p) * kHundredth;
        k.b = kOne - k.c;
    } else {
        k.c = k0, k.a = k0, k.b = k0;
    }
    return k;
}

int count(const WheelLayout& l) { return l.count < 4 ? l.count : 4; }

// The brake pedal onto the layout's wheels (0x343CF8).
void set_brakes(ex::Car& c, const WheelLayout& l, F p) {
    for (int w = 0; w < count(l); ++w) c.w[size_t(w)].brake = p;
}

// The slip aid's angle (the steering's correction of the slip direction): atan2 of the side speed less the yaw's
// (with the yaw acceleration's share `k`) at the front axle, over the speed.
F slip_aim(const ex::Car& c, F yaw_acc, F k) {
    const F y = -(c.lat_speed - (c.yaw_rate + yaw_acc * k) * c.p.dist_front);
    return ex::atan2(behaviour(), y, c.speed);
}

} // namespace

// ---- the frame -------------------------------------------------------------------------------------------------

PadFrame PadFrame::from_bytes(const u8* b) {
    PadFrame f;
    auto w16 = [&](int o) { return u16(b[o] | (b[o + 1] << 8)); };
    f.flags = b[0], f.kind = b[1], f.shift = gt3r::s8(b[2]), f.count = b[3];
    f.steer = gt3r::s16(w16(4)), f.b6 = b[6], f.nitrous = b[7];
    f.throttle = w16(8), f.brake = w16(0xA), f.back = w16(0xC), f.third = w16(0xE);
    std::memcpy(f.buttons.data(), b + 0x10, 16);
    return f;
}

void PadFrame::to_bytes(u8* b) const {
    auto w16 = [&](int o, u16 v) { b[o] = u8(v), b[o + 1] = u8(v >> 8); };
    b[0] = flags, b[1] = kind, b[2] = u8(shift), b[3] = count;
    w16(4, u16(steer)), b[6] = b6, b[7] = nitrous;
    w16(8, throttle), w16(0xA, brake), w16(0xC, back), w16(0xE, third);
    std::memcpy(b + 0x10, buttons.data(), 16);
}

void post_mail(PadFrame& frame, PadMail& mail, s8 control, bool player) {
    u8 request[6] = {};
    if (mail.pit[0] != 0) {
        for (size_t k = 0; k < 4; ++k) request[k] = mail.pit[k];
        mail.pit = {};
    }
    const u8 n = u8(control) < 2 ? 6 : 4;
    if (n == 6 && mail.command[0] != 0) {
        request[4] = mail.command[0], request[5] = mail.command[1];
        mail.command = {};
    }
    if (!player) return; // (a computer's controller leaves its frame cleared)
    frame.count = n;
    for (size_t k = 0; k < n; ++k) frame.buttons[k] = request[k];
}

// ---- the race's tables -----------------------------------------------------------------------------------------

PadTables pad_tables(const Program& program, u8 course_road) {
    PadTables t;
    if (!program.known()) return t;
    const u32 K = kPadSettings;
    auto b = [&](u32 o) { return program.b(K + o); };
    auto w = [&](u32 a) { return F::raw(program.w(a)); };
    // 0x620270 ..: 100 / (60 b), at most 1
    for (u32 k = 0; k < 6; ++k) {
        const F r = kHundred / (f_of(b(0x14 + k)) * kSixty);
        t.ramp[k] = kOne < r ? kOne : r;
    }
    t.analog_jump = f_of(b(0x04)) * kHundredth;
    t.ext_yaw_gain = f_of(b(0x27)) * kHundredth, t.ext_slip_gain = f_of(b(0x23)) * kHundredth, t.aid_yaw_gain = f_of(b(0x29)) * kHundredth;
    t.aid_speed_lo = f_of(s32(b(0x2A)) * 10) * kKmh, t.aid_speed_hi = f_of(s32(b(0x2B)) * 10) * kKmh;
    if (t.aid_speed_lo < kTenKmh) t.aid_speed_lo = kTenKmh;
    if (t.aid_speed_hi < kTenKmh) t.aid_speed_hi = kTenKmh;
    t.active_on = b(0x2C);
    if (course_road == 2) t.active_on = 0;
    t.active_gain = f_of(b(0x2D)) * kTenth;
    t.active_damping = f_of(gt3r::s8(b(0x2E))) * kHundredth;
    t.active_max = f_of(b(0x2F)) * kTenthDeg;
    t.active_mode = b(0x30);
    t.active_window = b(0x31) == 0 ? kTwoPi : f_of(b(0x31)) * kDeg;
    const bool slip_on = b(0x10) != 0;
    for (u32 r = 0; r < 2; ++r) {
        t.follow_k[r] = f_of(b(0x1C + r)), t.follow_back[r] = f_of(b(0x1E + r));
        t.follow_slip[r] = slip_on ? f_of(b(0x20 + r)) : k0;
        t.follow_damping[r] = f_of(b(0x24 + r)) * kTenth;
        for (u32 m = 0; m < 2; ++m) {
            const u32 i = 2 * r + m;
            t.follow_rate[i] = f_of(b(0x3C + i)) * kDeg, t.follow_rate_back[i] = f_of(b(0x40 + i)) * kDeg;
            t.follow_rate_slip[i] = f_of(b(0x44 + i)) * kDeg;
        }
        // (the table's own address is compared with a constant: the flag is 1)
        t.follow_by_speed[r] = b(0x36 + r) < b(0x38 + r) ? 1u : 0u;
        t.follow_speed_lo[r] = f_of(b(0x36 + r)) * kKmh, t.follow_speed_hi[r] = f_of(b(0x38 + r)) * kKmh;
        for (u32 m = 0; m < 2; ++m) {
            const u32 i = 2 * r + m;
            t.fast_rate[i] = f_of(b(0x4C + i)) * kDeg, t.fast_rate_back[i] = f_of(b(0x50 + i)) * kDeg;
            t.fast_rate_slip[i] = f_of(b(0x54 + i)) * kDeg;
        }
    }
    t.wheel_slew = f_of(b(0x5C)) * kDeg;
    t.least_lock_a = {f_of(b(0x5D)) * kDeg, f_of(b(0x5E)) * kDeg};
    auto ratio = [&](u8 v) { return v == 0 ? kStick : kNineHundred / (f_of(s32(v) * 60) * k4096); };
    t.wheel6_ratio = {ratio(b(0x61)), ratio(b(0x64)), ratio(b(0x62))};
    t.curve = {make_curve(b(0x65), b(0x66)), make_curve(b(0x6C), b(0x6D)), make_curve(b(0x70), b(0x71))};
    // read as they are
    t.least_lock_b = {w(0x620254), w(0x620258)};
    t.slew_analog = b(0x11), t.lock_by_yaw = b(0x26), t.lock_by_slip = b(0x22);
    for (u32 r = 0; r < 3; ++r)
        for (u32 k = 0; k < 2; ++k) t.prefilter[r][k] = b(0x8A + 0x20 * r + k);
    for (u32 k = 0; k < 4; ++k) t.lean_force[k] = w(0x620730 + 4 * k), t.lean_steer[k] = w(0x62078C + 4 * k);
    for (u32 k = 0; k < 5; ++k) t.lean_follow[k] = w(0x620748 + 4 * k);
    t.lean_rate_max = w(0x62079C), t.lean_yaw = w(0x6207A0), t.special_blend = w(0x6207B4);
    for (u32 k = 0; k < 5; ++k) { // 0x3F1790
        t.pace_shift[k] = u8((s32(gt3r::s8(program.b(0x622088 + k))) * 128 + 0x3200) / 100);
        t.pace_brake[k] = f_of(program.b(0x622090 + k)) / kHundred, t.pace_corner[k] = f_of(program.b(0x622098 + k)) / kHundred;
    }
    return t;
}

bool same(const PadTables& a, const PadTables& b, std::string* why) {
    bool ok = true;
    if (why) why->clear();
    auto fail = [&](const char* what) {
        if (why) *why += (why->empty() ? "" : ", ") + std::string(what);
        return ok = false;
    };
    auto eq = [](F x, F y) { return x.bits == y.bits; };
    auto arr = [&](const auto& x, const auto& y) {
        for (size_t k = 0; k < x.size(); ++k)
            if (!eq(x[k], y[k])) return false;
        return true;
    };
    if (!eq(a.wheel_slew, b.wheel_slew)) fail("wheel slew 0x620248");
    if (!arr(a.least_lock_a, b.least_lock_a)) fail("least lock 0x62024C");
    if (!arr(a.least_lock_b, b.least_lock_b)) fail("least lock 0x620254");
    if (!arr(a.wheel6_ratio, b.wheel6_ratio)) fail("kind 6 ratio 0x62025C");
    if (!eq(a.analog_jump, b.analog_jump)) fail("analog jump 0x620268");
    if (!arr(a.ramp, b.ramp)) fail("ramps 0x620270");
    if (a.slew_analog != b.slew_analog || a.lock_by_yaw != b.lock_by_yaw || a.lock_by_slip != b.lock_by_slip || a.prefilter != b.prefilter)
        fail("settings bytes");
    for (size_t k = 0; k < 3; ++k)
        if (a.curve[k].kind != b.curve[k].kind || !eq(a.curve[k].a, b.curve[k].a) || !eq(a.curve[k].b, b.curve[k].b) || !eq(a.curve[k].c, b.curve[k].c))
            fail("pedal curves 0x843538");
    if (!eq(a.ext_yaw_gain, b.ext_yaw_gain) || !eq(a.ext_slip_gain, b.ext_slip_gain) || !eq(a.aid_yaw_gain, b.aid_yaw_gain))
        fail("gains 0x843568");
    if (!eq(a.aid_speed_lo, b.aid_speed_lo) || !eq(a.aid_speed_hi, b.aid_speed_hi)) fail("aid speeds 0x843574");
    if (a.active_on != b.active_on || a.active_mode != b.active_mode || !eq(a.active_gain, b.active_gain) || !eq(a.active_damping, b.active_damping) ||
        !eq(a.active_max, b.active_max) || !eq(a.active_window, b.active_window))
        fail("active steering 0x84357C");
    if (!arr(a.follow_k, b.follow_k) || !arr(a.follow_back, b.follow_back) || !arr(a.follow_slip, b.follow_slip) || !arr(a.follow_damping, b.follow_damping))
        fail("follower gains 0x843598");
    if (!arr(a.follow_rate, b.follow_rate) || !arr(a.follow_rate_back, b.follow_rate_back) || !arr(a.follow_rate_slip, b.follow_rate_slip))
        fail("follower rates 0x8435B8");
    if (a.follow_by_speed != b.follow_by_speed || !arr(a.follow_speed_lo, b.follow_speed_lo) || !arr(a.follow_speed_hi, b.follow_speed_hi))
        fail("follower speeds 0x8435E8");
    if (!arr(a.fast_rate, b.fast_rate) || !arr(a.fast_rate_back, b.fast_rate_back) || !arr(a.fast_rate_slip, b.fast_rate_slip))
        fail("follower fast rates 0x843600");
    if (!arr(a.lean_force, b.lean_force) || !arr(a.lean_follow, b.lean_follow) || !arr(a.lean_steer, b.lean_steer) || !eq(a.lean_rate_max, b.lean_rate_max) ||
        !eq(a.lean_yaw, b.lean_yaw))
        fail("lean 0x620730");
    if (!eq(a.special_blend, b.special_blend)) fail("blend 0x6207B4");
    if (a.pace_shift != b.pace_shift || !arr(a.pace_brake, b.pace_brake) || !arr(a.pace_corner, b.pace_corner)) fail("pace 0x6220A8");
    return ok;
}

// ---- the routines ----------------------------------------------------------------------------------------------

F steer_unit(F steer) {
    F v = steer * kSteerUnit;
    if (kOne < v) return kOne;
    if (v < kMinusOne) v = kMinusOne;
    return v;
}

void slew(F& x, F target, F step) {
    F v = x;
    if (v < target) {
        v = v + step;
        if (target < v) v = target;
    } else if (target < v) {
        v = v - step;
        if (v < target) v = target;
    }
    x = v;
}

F pedal_curve(const PedalCurve& k, F x, F* slope) {
    const F ax = x < k0 ? -x : x;
    F y = ax;
    if (k.kind == 1) {
        const F d = ax - k.a;
        y = (k.c * d) * d + k.b;
        if (slope) *slope = (k.c + k.c) * d;
    } else if (k.kind == 2) {
        const F d = ax - k.a;
        F r = gt3r::ee::sqrt(d);
        y = k.c * r + k.b;
        if (slope) {
            if (r < kHundredth) r = kHundredth;
            *slope = k.c / (r + r);
        }
    } else if (k.kind == 3) {
        const F x2 = ax * ax, x4 = x2 * x2, x3 = ax * x2;
        y = (k.c * x4) * x4 + k.b * ax;
        if (slope) *slope = ((k.c * kEight) * x4) * x3 + k.b;
    } else if (slope) {
        *slope = kOne;
    }
    return x < k0 ? -y : y;
}

void pad_hud(ex::Car& c, const PadFrame& f) {
    if (!(f.flags & 1)) c.steer_pct = gt3r::s8(f.steer == 0 ? 0 : f.steer > 0 ? 100 : -100);
    else {
        const s32 p = s32(f.steer) * 100;
        c.steer_pct = gt3r::s8((p < 0 ? p + 0xFFF : p) >> 12);
    }
    if (f.flags & 2) {
        if (h(f.throttle) != 0x1000) c.full_throttle = 0;
    } else if (h(f.throttle) == 0) {
        c.full_throttle = 0;
    }
}

void steer_prefilter(ex::Car& c, InputExtra& x, const PadTables& t, const InputRace& race, PadFrame& f) {
    (void)c;
    const size_t row = f.kind == 6 ? (x.wheel_ratio == 0 ? 1 : 2) : 0;
    const s32 order = t.prefilter[row][0];
    s32 gain = t.prefilter[row][1];
    if (race.lan_gain) gain += 2;
    s32 d1 = 0, d2 = 0;
    if (order != 0) {
        d1 = s32(f.steer) - s32(x.steer_in), d2 = s32(x.steer_in) - s32(x.steer_in_last);
        if (race.lan_average) {
            const s32 sum = s32(f.steer) + s32(x.steer_in_last), mean = (sum + s32(u32(sum) >> 31)) >> 1;
            d2 = mean - s32(x.steer_in_last), d1 = s32(f.steer) - mean;
        }
    }
    const s16 raw = f.steer;
    x.steer_in_last = x.steer_in, x.steer_in = raw;
    if (order == 0) return;
    s32 v = s32(raw) + gain * d1;
    if (order >= 2) v = v + (gain * gain) * (d1 - d2);
    if (v < -0x1000) v = -0x1000;
    if (!(v < 0x1001)) v = 0x1000;
    f.steer = gt3r::s16(v);
}

void steer_follow(ex::Car& c, InputExtra& x, const PadTables& t, int row, int back, F target, F slip) {
    const size_t r = size_t(row), i = size_t(2 * row + back);
    const F cmd = c.steer_cmd, v = x.steer_rate, dt = c.dt;
    const F kslip = t.follow_slip[r], kback = t.follow_back[r];
    F a = (target - cmd) * t.follow_k[r] - v * t.follow_damping[r];
    F rate = t.follow_rate[i], fast = t.fast_rate[i];
    bool pulled = false;
    if (!(kslip == k0)) {
        if ((cmd < target && cmd < slip) || (target < cmd && slip < cmd)) {
            fast = t.fast_rate_slip[i], rate = t.follow_rate_slip[i];
            a = a + (slip - cmd) * kslip;
            pulled = true;
        }
    }
    if (!pulled && ((k0 < cmd && target < cmd) || (cmd < k0 && cmd < target))) { // returning toward the center
        fast = t.fast_rate_back[i], rate = t.follow_rate_back[i];
        a = a - cmd * kback;
    }
    F nv = v + a * dt;
    if (t.follow_by_speed[r] != 0) {
        const F s = c.speed, lo = t.follow_speed_lo[r];
        if (lo < s) {
            const F hi = t.follow_speed_hi[r];
            if (hi <= s) rate = fast;
            else rate = rate + ((fast - rate) * (s - lo)) / (hi - lo);
        }
    }
    if (rate < nv) nv = rate;
    else if (nv < -rate) nv = -rate;
    const F next = cmd + nv * dt;
    if ((cmd <= target && target <= next) || (target <= cmd && next <= target)) {
        c.steer_cmd = target;
        x.steer_rate = k0;
        return;
    }
    x.steer_rate = nv;
    c.steer_cmd = next;
}

void steer_command(ex::Car& c, sim::Extra& xx, const WheelLayout& l, const PadTables& t, const SteerTables& g, const InputRace& race,
                   const PadFrame& f) {
    InputExtra& x = xx.input;
    const int n = count(l);
    // the mean slip direction of the wheels ahead of the center of gravity
    F slip = k0;
    s32 ahead = 0;
    for (int w = 0; w < n; ++w)
        if (gt3r::s8(l.fore_aft[size_t(w)]) > 0) slip = slip + c.w[size_t(w)].slip_dir, ++ahead;
    slip = slip / f_of(ahead);
    // every wheel ahead on a marked surface (0x620288): the loose row
    u8 marked = 2;
    for (int w = 0; w < n; ++w)
        if (gt3r::s8(l.fore_aft[size_t(w)]) > 0) {
            const u8 m = (c.w[size_t(w)].surface < 24 ? g.fixed_aim[c.w[size_t(w)].surface] : u8(0));
            if (m < marked) marked = m;
        }
    if (!(marked < 2)) marked = 0;
    const u8 course = race.course_kind;
    int row = course == 2 || course == 3 ? 1 : 0; // (a wheel's: the loose course)
    F speed = c.speed;
    if (speed < k0) speed = k0;
    const u8 kind = f.kind;
    F lock;
    if (kind == 6) lock = c.p.max_lock;
    else if (kind == 4) lock = table(row == 1 ? x.lock_loose : x.lock_wheel, speed);
    else {
        row = marked;
        lock = table(row == 1 ? x.lock_loose : x.lock_pad, speed);
    }
    bool extend = kind != 6;
    if (kind == 4) {
        const gt3r::s8 type = x.wheel_type;
        extend = true;
        if (type == 1 || type == 2 || type == 3) {
            const F least = type == 2 ? t.least_lock_b[size_t(row == 1)] : t.least_lock_a[size_t(row == 1)];
            if (lock < least) lock = least;
            if (type == 3) extend = false;
        } else if (type == 4) {
            extend = false;
        }
    }
    F hi = lock, lo = -hi;
    if (extend) {
        const F ya = xx.chassis.yaw_follow, yr = c.yaw_rate;
        F hi2 = hi, lo2 = lo;
        if (t.lock_by_yaw != 0 && kTenKmh < c.speed && k0 < ya * yr) {
            const F d = ex::atan2(behaviour(), -(c.lat_speed - (yr + ya * t.ext_yaw_gain) * c.p.dist_front), c.speed) - slip;
            if (k0 < c.yaw_rate) {
                hi2 = hi - d;
                if (t.lock_by_yaw < 2 && hi2 < k0) hi2 = k0;
            } else if (c.yaw_rate < k0) {
                lo2 = lo - d;
                if (t.lock_by_yaw < 2 && k0 < lo2) lo2 = k0;
            }
        }
        F mirror = slip;
        if (t.lock_by_slip != 0) {
            const F a = ex::atan2(behaviour(), -(c.lat_speed - (c.yaw_rate + ya * t.ext_slip_gain) * c.p.dist_front), c.speed);
            mirror = slip - (a - slip);
        }
        if (lo <= slip && slip <= lock) {
            if (t.lock_by_yaw != 0) hi = hi2, lo = lo2;
        } else if (k0 < slip) {
            hi = slip;
            if (t.lock_by_slip != 0 && k0 < ya && c.yaw_rate < k0) {
                if (t.lock_by_slip == 2) hi = mirror < k0 ? k0 : mirror;
                else if (lock < mirror) hi = mirror;
            }
            if (t.lock_by_yaw != 0) lo = lo2;
        } else if (slip < k0) {
            if (t.lock_by_yaw != 0) hi = hi2;
            lo = slip;
            if (t.lock_by_slip != 0 && ya < k0 && k0 < c.yaw_rate) {
                if (t.lock_by_slip == 2) lo = k0 < mirror ? k0 : mirror;
                else if (mirror < -lock) lo = mirror;
            }
        }
    }
    {
        const F full = c.p.max_lock;
        if (full < hi) hi = full;
        if (lo < -full) lo = -full;
    }
    // the pad's kinds: digital, 2, 3
    const bool pad = !(f.flags & 1) || kind == 3 || kind == 2;
    bool active = false;
    if (pad && !(c.x535 & 1) && kActiveSpeed <= c.speed && xx.phases.soft_wheels == 0 && !(t.active_mode == 1 && f.steer != 0)) {
        if (t.active_on == 2) active = true;
        else if (t.active_on == 1 && x.driver_32 == 0) active = true;
    }
    if (kind == 4) {
        x.steer_kind = 0;
        const F s = f_of(f.steer) * kStick;
        x.steer_view = s * kWheelView;
        const gt3r::s8 type = x.wheel_type;
        F cmd = k0;
        if (type >= 0 && type < 4) {
            slew(c.lock_hi, hi, t.wheel_slew * c.dt);
            slew(c.lock_lo, lo, t.wheel_slew * c.dt);
            cmd = k0 <= s ? c.lock_hi * s : -c.lock_lo * s;
        } else if (type == 4) {
            cmd = c.p.max_lock * s;
        }
        c.steer_cmd = cmd;
    } else if (kind == 6) {
        x.steer_kind = 3;
        F s = f_of(f.steer);
        x.steer_view = (s * kWheel6View) * kStick;
        const bool loose = u8(course - 2) < 2;
        s = s * (loose ? t.wheel6_ratio[2] : x.wheel_ratio != 0 ? t.wheel6_ratio[1] : t.wheel6_ratio[0]);
        if (kOne < s) s = kOne;
        else if (s < kMinusOne) s = kMinusOne;
        if (x.wheel_ratio != 0 && !loose) s = pedal_curve(t.curve[0], s);
        c.lock_hi = hi, c.lock_lo = lo;
        c.steer_cmd = c.p.max_lock * s;
    } else if (pad) {
        c.lock_hi = hi, c.lock_lo = lo;
        F target;
        if (!(f.flags & 1)) target = f.steer > 0 ? hi : f.steer < 0 ? lo : k0;
        else {
            const F s = f_of(f.steer) * kStick;
            target = k0 <= s ? hi * s : -lo * s;
        }
        if (f.steer != 0) x.steer_kind = kind == 2 ? 2 : 1;
        if (xx.aids.assist & 0x10) {
            if (t.aid_speed_lo < c.speed) {
                const F d = slip_aim(c, xx.chassis.yaw_follow, t.aid_yaw_gain) - slip;
                F share = kOne;
                if (c.speed < t.aid_speed_hi) share = c.speed / (t.aid_speed_hi - t.aid_speed_lo);
                target = target - d * share;
            }
        }
        if (active) {
            const F err = angle_diff(c.heading, race.line_heading);
            const F win = t.active_window;
            const bool inside = !(err < -win) && !(win < err);
            bool against = false;
            if (inside && t.active_mode == 2) against = (k0 < err && f.steer > 0) || (err < k0 && f.steer < 0);
            if (inside && !against) {
                const F last = x.active_last;
                x.active_last = err;
                F corr = (-err) * t.active_gain - ((err - last) * c.inv_dt) * t.active_damping;
                if (t.active_max < corr) corr = t.active_max;
                else if (corr < -t.active_max) corr = -t.active_max;
                target = target + corr;
            }
        }
        steer_follow(c, x, t, row, x.steer_kind == 1 ? 0 : 1, target, slip);
    } // (another analog kind leaves the command and its bounds as they are)
    if ((kind == 4 || kind == 6) && (xx.aids.assist & 8) && t.aid_speed_lo < c.speed) {
        const F d = slip_aim(c, xx.chassis.yaw_follow, t.aid_yaw_gain) - slip;
        F share = kOne;
        if (c.speed < t.aid_speed_hi) share = c.speed / (t.aid_speed_hi - t.aid_speed_lo);
        F cmd = c.steer_cmd - d * share;
        c.steer_cmd = cmd;
        const F full = c.p.max_lock;
        if (full < cmd) c.steer_cmd = full;
        else if (cmd < -full) c.steer_cmd = -full;
    }
    if (!active) x.active_last = k0;
    if (x.steer_kind < 3 && x.steer_kind != 0) x.steer_view = steer_unit(c.steer_cmd);
}

void lean_steer(ex::Car& c, sim::Extra& xx, const PadTables& t, const SteerTables& g, const PadFrame& f) {
    InputExtra& x = xx.input;
    const F dt = c.dt;
    const F s = (-f_of(f.steer)) * kStick;
    const F a = ex::atan2(behaviour(), s, kOne);
    const F tilt = c.roll - c.ground_roll;
    F aim = t.lean_follow[4] * ((-a) - tilt);
    if (kOne < aim) aim = kOne;
    else if (aim < kMinusOne) aim = kMinusOne;
    // the lean's follower (F + 0x65C, its rate F + 0x660)
    F& lean = xx.body.in_line_x65c;
    const F l0 = lean, v0 = x.lean_rate;
    F acc = (aim - l0) * t.lean_follow[0] - v0 * t.lean_follow[1];
    if ((k0 < l0 && aim < l0) || (l0 < k0 && l0 < aim)) acc = acc - l0 * t.lean_follow[2];
    F v = v0 + acc * dt;
    x.lean_rate = v;
    const F vmax = t.lean_follow[3];
    if (vmax < v) x.lean_rate = vmax;
    else if (v < -vmax) x.lean_rate = -vmax;
    const F next = l0 + x.lean_rate * dt;
    lean = next;
    if ((l0 < aim && aim < next) || (aim < l0 && next < aim)) lean = aim, x.lean_rate = k0;
    if (kOne < lean) lean = kOne;
    else if (lean < kMinusOne) lean = kMinusOne;
    // the steering command toward the lean's
    const ex::Wheel& w0 = c.w[0];
    const F cos_tilt = gt3r::ee::cos(tilt);
    F torque = k0;
    {
        const F roll_lean = ((gt3r::ee::sin(c.roll) * gt3r::ee::cos(c.steer_cmd)) * t.lean_force[0]) * kHalfPi;
        const F force = (w0.force_a * t.lean_force[1]) * cos_tilt;
        const F camber = (xx.tires.camber_force[0] * t.lean_force[2]) * cos_tilt;
        torque = ((torque - roll_lean) - force) - camber;
    }
    const F yaw = s - (c.speed * c.yaw_rate) / kGravity;
    const F factor = gt3r::ee::cos(g.lean_gain[xx.steer.lean_row < 3 ? xx.steer.lean_row : 0]) * (kThree - (cos_tilt + cos_tilt));
    F target = (-t.lean_yaw) * yaw + c.t[0].slip_angle / factor;
    const F full = c.p.max_lock;
    if (full < target) target = full;
    else if (target < -full) target = -full;
    const F cmd = c.steer_cmd, d = target - cmd;
    F& rate = xx.body.in_line_x480;
    const F r0 = rate;
    const bool toward = (k0 < d && k0 < r0) || (d < k0 && r0 < k0);
    torque = torque + d * (toward ? t.lean_steer[1] : t.lean_steer[0]);
    torque = torque - r0 * (toward ? t.lean_steer[3] : t.lean_steer[2]);
    torque = torque - ((c.roll_rate * (w0.rim_speed / c.p.tire_radius[0])) * t.lean_force[3]);
    F nr = r0 + torque * dt;
    rate = nr;
    if (t.lean_rate_max < nr) rate = t.lean_rate_max;
    else if (nr < -t.lean_rate_max) rate = -t.lean_rate_max;
    const F next_cmd = cmd + rate * dt;
    c.steer_cmd = next_cmd;
    if (full < next_cmd) c.steer_cmd = full, rate = k0;
    else if (next_cmd < -full) c.steer_cmd = -full, rate = k0;
}

void pad_clutch(ex::Car& c, const PadFrame& f, u8 style) {
    if (c.race_hold != 0 || c.hold != 0 || c.driver_kind == 8) {
        c.clutch_req = k0;
        return;
    }
    if (style == 3) { // the brake axis is the clutch pedal
        const F b = c.brake_pedal;
        c.brake_pedal = k0;
        c.clutch_req = kOne - b;
        if (c.clutch_req == c.brake_pedal) return;
        c.reverse_req = 0;
        c.shift_req = 0;
        return;
    }
    if (style == 6 && (f.shift != 0 || h(f.back) != 0)) {
        c.clutch_req = k0;
        return;
    }
    if (c.p.d.driven_axle == 1 && kHandbrakeHeld < c.handbrake) {
        c.clutch_req = k0;
        return;
    }
    if (stepless(c.p.d) && kHandbrakeHeld < c.handbrake && gt3r::ee::sqrt(c.speed * c.speed + c.lat_speed * c.lat_speed) < kWalk) {
        c.clutch_req = k0;
        return;
    }
    if (c.clutch == 0) {
        c.clutch_req = c.brake_pedal < kQuarter ? kOne : k0;
        return;
    }
    c.clutch_req = s32(c.rpm) < s32(c.p.e.idle) ? k0 : kOne;
}

void one_speed_clutch(ex::Car& c) {
    if (c.race_hold != 0 || c.hold != 0 || c.driver_kind == 8) {
        c.clutch_req = k0;
        return;
    }
    if (c.clutch == 0) {
        c.clutch_req = c.brake_pedal < kTenth ? kOne : k0;
        return;
    }
    c.clutch_req = kOne;
    if (!(kTenth < c.brake_pedal)) return;
    if (c.speed < kWalk && -kWalk < c.speed) c.clutch_req = k0;
}

void one_speed_creep(ex::Car& c, const PadFrame& f) {
    if (c.clutch_req == k0) return;
    const F v = c.gear != 0 ? c.speed : -c.speed;
    if (-kWalk <= v) { // 0x368548
        F share = (kCreepSpeed - v) / kCreepSpeed;
        if (share < k0) share = k0;
        share = share * kCreepShare;
        c.throttle_pedal = share + (kOne - share) * c.throttle_pedal;
    }
    if (h(f.throttle) == 0 && h(f.back) == 0 && c.speed < -kWalk) c.reverse_req = 1;
}

void pad_controls(ex::Car& c, sim::Extra& xx, const WheelLayout& l, const PadTables& t, const SteerTables& g, const InputRace& race,
                  const PadFrame& f) {
    InputExtra& x = xx.input;
    x.steer_seen = x.steer_in, x.steer_seen_last = x.steer_in_last;
    const bool wheel = f.kind == 4 || f.kind == 6;
    if (l.kind == 1) lean_steer(c, xx, t, g, f);
    else steer_command(c, xx, l, t, g, race, f);

    // the throttle and the back axis
    u8 style = 0;
    if (c.control != 1 && c.driver_kind == 0) style = c.pedal_style;
    bool th_analog = true, back_analog = true;
    u32 th = f.throttle, back = f.back;
    if (!(f.flags & 2)) th = (th << 12) & 0xFFFF, th_analog = false;
    if (!(f.flags & 8)) back = (back << 12) & 0xFFFF, back_analog = false;
    if (c.hold != 0) back = 0;
    else if (kBackLimit < c.speed) back = 0;
    if (xx.aids.mark_mode == 4) {
        back = 0;
        if (0 < h(f.brake)) th = 0;
    }
    if (style < 7) {
        if (style <= 2) { // the back axis replaces a smaller throttle
            if (back < th) back = 0;
            else th_analog = back_analog, th = back;
        } else if (style == 5) {
            back = 0;
        }
    }
    c.reverse_req = gt3r::s8(back != 0 ? 1 : 0);
    F p = k0;
    u8 state = 0;
    if (!th_analog) {
        p = c.throttle_pedal;
        if (th != 0) {
            p = p + t.ramp[0];
            if (kOne < p) p = kOne;
            state = back != 0 ? 2 : 1;
            x.throttle_state = state;
        } else {
            p = p - t.ramp[1];
            if (p < k0) x.throttle_state = 0, p = k0;
            state = x.throttle_state;
            if (state == 2) c.reverse_req = 1;
        }
    } else if (th != 0) {
        p = f_of(s32(th)) * kStick;
        p = jump(t, p);
        if (!wheel && t.slew_analog != 0) {
            const F target = p;
            p = c.throttle_pedal;
            if (p < target) {
                p = p + t.ramp[0];
                if (target < p) p = target;
            } else if (target < p) {
                p = p - t.ramp[1];
                if (p < target) p = target;
            }
        }
        x.throttle_state = 0;
        state = 0;
    } else {
        state = x.throttle_state;
        if (state != 0) {
            p = c.throttle_pedal - t.ramp[1];
            if (p <= k0) x.throttle_state = 0, p = k0, state = 0;
            else if (state == 2) c.reverse_req = 1;
        }
    }
    if (state == 0 && wheel && !one_speed(c.p.d)) p = pedal_curve(t.curve[1], p);
    c.throttle_pedal = p;

    // the brake
    if (!(f.flags & 4)) {
        if (h(f.brake) != 0) {
            const F b = c.brake_pedal + t.ramp[2];
            c.brake_pedal = kOne < b ? kOne : b;
        } else {
            const F b = c.brake_pedal - t.ramp[3];
            c.brake_pedal = b < k0 ? k0 : b;
        }
    } else {
        F b = f_of(s32(f.brake)) * kStick;
        if (!(b == k0)) b = jump(t, b);
        if (wheel) b = pedal_curve(t.curve[2], b);
        else if (t.slew_analog != 0) {
            const F target = b;
            b = c.brake_pedal;
            if (b < target) {
                b = b + t.ramp[2];
                if (target < b) b = target;
            } else if (target < b) {
                b = b - t.ramp[3];
                if (b < target) b = target;
            }
        }
        c.brake_pedal = b;
    }
    c.shift_req = c.hold != 0 ? gt3r::s8(0) : f.shift;

    // the third axis
    if (!(f.flags & 0x10)) {
        if (h(f.third) != 0) {
            const F v = c.handbrake + t.ramp[4];
            c.handbrake = kOne < v ? kOne : v;
        } else {
            const F v = c.handbrake - t.ramp[5];
            c.handbrake = v < k0 ? k0 : v;
        }
    } else {
        F v = f_of(h(f.third)) * kStick;
        if (!(v == k0)) v = jump(t, v);
        c.handbrake = v;
    }

    // the clutch by the gearbox
    if (c.p.d.kind == 4) {
        if (c.race_hold != 0) c.throttle_pedal = k0;
        c.clutch_req = kOne;
        return;
    }
    if (one_speed(c.p.d)) {
        one_speed_clutch(c);
        one_speed_creep(c, f);
        return;
    }
    pad_clutch(c, f, style);
    if (!stepless(c.p.d)) return;
    // a stepless box creeps: the throttle at least what holds the engine 500 rpm above idle
    if (!(c.handbrake < kHandbrakeHeld) && !(kWalk <= gt3r::ee::sqrt(c.speed * c.speed + c.lat_speed * c.lat_speed))) return;
    const F q = c.torque_friction / (c.torque_wot + c.torque_friction);
    const F r = ((f_of(s32(c.p.e.idle)) + kCreepRpm) - f_of(s32(c.rpm))) * kCreepGain;
    F floor = r + q;
    if (floor < k0) floor = k0;
    else if (kOne < floor) floor = kOne;
    if (c.throttle_pedal < floor) c.throttle_pedal = floor;
    if (h(f.throttle) == 0 && h(f.back) == 0 && c.gear == 0) c.reverse_req = 1;
}

void pad_input(ex::Car& c, sim::Extra& x, const WheelLayout& l, const PadTables& t, const SteerTables& g, const InputRace& race,
               const PadFrame& f) {
    pad_controls(c, x, l, t, g, race, f);
    c.throttle = c.throttle_pedal;
    set_brakes(c, l, c.brake_pedal);
}

void pit_answer(ex::Car& c, sim::Extra& x, const PadFrame& f) {
    u16& flags = x.phases.pit_flags;
    const u8* b = f.buttons.data();
    if (b[0] == 1) {
        u16 a = flags;
        if (!(a & 0x10)) return;
        if (a & 4) {
            if (b[1] != 0xFF) flags = a = u16(a | 0x40);
            if (b[2] != 0xFF) flags = a = u16(a | 0x80);
            x.phases.pit_tires[0] = b[1], x.phases.pit_tires[1] = b[2];
        }
        if ((a & 8) && b[3] != 0) flags = a = u16(a | 0x100);
        flags = u16(a & 0xFFE3);
        if (flags == 0) c.hold = 0;
    } else if (b[0] == 2) {
        const u16 a = flags;
        if (!(a & 0x100)) return;
        flags = u16(a ^ 0x100);
        if ((flags & 0xC0) == 0) c.hold = 90;
    }
}

void bspec_command(ex::Car& c, sim::Extra& xx, const PadTables& t, const InputHooks& hk, u8 kind, u8 value) {
    InputExtra& x = xx.input;
    if (kind == 4) {
        if (xx.aids.tcs_class == value) return;
        xx.aids.tcs_class = value;
        const size_t v = value < 5 ? value : 4; // ❓ (the program reads past its five levels for a value over 4)
        x.pace_corner = t.pace_corner[v], x.pace_brake = t.pace_brake[v];
        if (k0 < xx.aids.fuel_capacity) x.pace_shift = t.pace_shift[v]; // (0x354E38)
    } else if (kind == 5) {
        if (x.overtake == value) return;
        x.overtake = value;
        if (value != 0 || c.driver_kind != 0) return;
        if ((xx.near.driver_line == 1 || xx.near.driver_line == 2) && hk.line_change) hk.line_change();
    } else if (kind == 6) {
        if (c.laps == 0) return;
        if (hk.no_pit && hk.no_pit()) return;
        if (value != 0) {
            if (c.x535 & 8) return;
            c.x535 = u8(c.x535 | 8);
            x.pit_request = 0;
        } else {
            if (!(c.x535 & 8)) return;
            c.x535 = u8(c.x535 & 0xF7);
            if (c.x535 & 4) x.pit_request = 1;
        }
    } else if (kind == 10) {
        if (value == 0) {
            c.control = 0;
            if (hk.race_control) hk.race_control(0);
        } else {
            c.control = 1;
            if (hk.race_control) hk.race_control(1);
            x.driver_40 = -1, xx.aids.scale_on = 0; // 0x469E48
        }
    }
}

void line_ground(ex::Car& c, InputExtra& x, const InputHooks& hk) {
    x.line_height = c.pos.z;
    if (!hk.ground) return;
    const F p[3] = {x.line_point[0], x.line_height, -x.line_point[1]};
    const GroundAnswer a = hk.ground(p); // (the second try with the road cell's hint asks the same: the query starts without one)
    if (a.found) {
        x.line_height = a.height;
        x.line_triangle = a.triangle;
    } else {
        x.line_triangle = u32(-s32(x.line_triangle));
    }
}

void driver_input(ex::Car& c, sim::Extra& x, const WheelLayout& l, const PadTables& t, const SteerTables& g, const InputRace& race,
                  const InputHooks& hk, PadFrame f) {
    if (f.buttons[0] != 0) pit_answer(c, x, f);
    if (u8(c.control) < 2 && f.buttons[4] != 0) bspec_command(c, x, t, hk, f.buttons[4], f.buttons[5]);
    // (not a ghost: the conductor's virtual 25, nothing in a single player's race)
    // the nitrous
    if (f.nitrous != 0 && c.driver_kind == 0 && k0 < x.input.nitrous_gain && k0 < x.input.nitrous) {
        x.input.nitrous_state = 6;
        c.torque_wot = c.torque_wot * (x.input.nitrous_gain + kOne);
    }
    c.full_throttle = 1;
    steer_prefilter(c, x.input, t, race, f);
    bool computer = false;
    if (c.control == 1) {
        pad_hud(c, f);
        if (hk.computer_clutch) hk.computer_clutch();
        computer = true;
        if (hk.computer_outputs) hk.computer_outputs();
    } else if (c.control == 0) {
        pad_hud(c, f);
        if (c.driver_kind == 0) {
            if (hk.line_begin) hk.line_begin();
            pad_input(c, x, l, t, g, race, f);
            if (hk.line_end) hk.line_end();
        } else {
            if (hk.computer_clutch) hk.computer_clutch();
            if (hk.line_end) hk.line_end();
            computer = true;
            if (hk.computer_outputs) hk.computer_outputs();
        }
    } else if (c.control == 2) {
        if (hk.computer_clutch) hk.computer_clutch();
        computer = true;
        if (hk.computer_outputs) hk.computer_outputs();
    }
    if (x.aids.mark_mode == 4 && c.clutch == 3) { // a category 4 car shifting: the pedals off
        c.throttle_pedal = k0, c.brake_pedal = k0;
        set_brakes(c, l, c.throttle_pedal);
    }
    line_ground(c, x.input, hk);
    if (x.aids.mark_mode == 6) {
        const F k = t.special_blend;
        x.input.special_steer = x.input.special_steer * (kOne - k) + c.steer_cmd * k;
    }
    if (computer) x.input.steer_view = steer_unit(c.steer_cmd) * kComputerView;
}

} // namespace gt4r::sim::rules
