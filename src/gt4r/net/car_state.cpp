#include "gt4r/net/car_state.h"

#include "gt4r/sim/rules_input.h"

#include <algorithm>
#include <cmath>

namespace gt4r::net {

namespace ex = gt3r::sim::exact;
using gt3r::ee::F;
using gt3r::s16;
using gt3r::s8;

namespace {

constexpr float kRate = 1024.0f, kRim = 128.0f, kSusp = 65536.0f; // (the wire's units: net::CarMotion)

s16 fixed(F v, float scale) {
    const double x = double(v.host()) * double(scale);
    if (!(std::fabs(x) < 32767.0)) return x > 0 ? s16(32767) : s16(-32768);
    return s16(std::lround(x));
}
// A value the wire has in fixed units: kept as it is when it is that already.
void put(F& v, s16 sent, float scale) {
    if (fixed(v, scale) != sent) v = F(float(sent) / scale);
}

} // namespace

CarMotion car_motion(const sim::Session& s, size_t car) {
    const ex::Car& c = s.record(car);
    CarMotion m;
    m.pos = {c.pos.x.bits, c.pos.y.bits, c.pos.z.bits};
    m.vel = {c.vel.x.bits, c.vel.y.bits, c.vel.z.bits};
    m.heading = c.heading.bits, m.pitch = c.pitch.bits, m.roll = c.roll.bits;
    m.yaw_rate = c.yaw_rate.bits;
    m.omega = c.omega.bits;
    m.steer_cmd = c.steer_cmd.bits;
    m.axle_speed = {c.axle_speed[0].bits, c.axle_speed[1].bits};
    m.pitch_rate = fixed(c.pitch_rate, kRate), m.roll_rate = fixed(c.roll_rate, kRate);
    for (size_t i = 0; i < 4; ++i) m.rim_speed[i] = fixed(c.w[i].rim_speed, kRim), m.susp[i] = fixed(c.w[i].susp, kSusp);
    m.gear = c.gear, m.clutch = c.clutch;
    return m;
}

float put_motion(sim::Session& s, size_t car, const CarMotion& m) {
    ex::Car c = s.record(car);
    const ex::Vec3 was = c.pos;
    c.pos = {F::raw(m.pos[0]), F::raw(m.pos[1]), F::raw(m.pos[2])};
    c.vel = {F::raw(m.vel[0]), F::raw(m.vel[1]), F::raw(m.vel[2])};
    const bool turned = c.heading.bits != m.heading || c.pitch.bits != m.pitch || c.roll.bits != m.roll;
    c.heading = F::raw(m.heading), c.pitch = F::raw(m.pitch), c.roll = F::raw(m.roll);
    c.yaw_rate = F::raw(m.yaw_rate);
    c.omega = F::raw(m.omega);
    c.steer_cmd = F::raw(m.steer_cmd);
    c.axle_speed = {F::raw(m.axle_speed[0]), F::raw(m.axle_speed[1])};
    put(c.pitch_rate, m.pitch_rate, kRate), put(c.roll_rate, m.roll_rate, kRate);
    for (size_t i = 0; i < 4; ++i) put(c.w[i].rim_speed, m.rim_speed[i], kRim), put(c.w[i].susp, m.susp[i], kSusp);
    c.gear = m.gear, c.clutch = m.clutch;
    const double dx = double(c.pos.x.host()) - double(was.x.host()), dy = double(c.pos.y.host()) - double(was.y.host()), dz = double(c.pos.z.host()) - double(was.z.host());
    const bool moved = c.pos.x.bits != was.x.bits || c.pos.y.bits != was.y.bits || c.pos.z.bits != was.z.bits;
    const sim::Extra x = s.gt4_record(car);
    s.load_state(car, c, x);
    if (moved || turned) s.settle_moved(car, was);
    return float(std::sqrt(dx * dx + dy * dy + dz * dz));
}

PadInput pad_input(const race::CarInput& in) {
    PadInput p;
    if (in.computer) {
        p.throttle = u16(std::lround(std::clamp(in.throttle, 0.0f, 1.0f) * 4096.0f));
        p.brake = u16(std::lround(std::clamp(in.brake, 0.0f, 1.0f) * 4096.0f));
        if (in.reverse) p.back = 1;
        return p;
    }
    sim::rules::PadFrame f;
    if (in.pad) f = sim::rules::PadFrame::from_bytes(in.pad->bytes.data());
    p.flags = u8(f.flags & 0x1F);
    p.kind = u8((f.kind & 7) | (f.nitrous ? 8 : 0));
    p.shift = f.shift;
    p.steer = f.steer, p.throttle = f.throttle, p.brake = f.brake, p.back = f.back, p.third = f.third;
    return p;
}

race::PadRecord pad_record(const PadInput& p) {
    sim::rules::PadFrame f;
    f.flags = p.flags, f.kind = u8(p.kind & 7), f.nitrous = p.kind & 8 ? 1 : 0;
    f.shift = p.shift, f.steer = p.steer, f.throttle = p.throttle, f.brake = p.brake, f.back = p.back, f.third = p.third;
    race::PadRecord r;
    f.to_bytes(r.bytes.data());
    return r;
}

race::CarInput driver_input(const PadInput& p, const CarEntry& e, const CarMotion& m, race::PadRecord& pad, bool keep_shift) {
    race::CarInput in;
    if (e.control == 0) {
        PadInput q = p;
        if (!keep_shift) q.shift = 0;
        pad = pad_record(q);
        in.pad = &pad;
        return in;
    }
    in.computer = true;
    in.control = e.control;
    in.throttle = float(p.throttle) / 4096.0f, in.brake = float(p.brake) / 4096.0f;
    in.reverse = p.back != 0;
    in.steer_angle = F::raw(m.steer_cmd).host();
    return in;
}

CarEntry car_entry(const sim::Session& s, size_t car, bool asm_on, bool tcs_on) {
    const ex::Car& c = s.record(car);
    CarEntry e;
    e.pedal_style = u8(c.pedal_style & 7);
    e.wheel_type = u8(u8(s.gt4_record(car).input.wheel_type) & 7);
    e.asm_on = asm_on, e.tcs_on = tcs_on;
    e.control = u8(u8(c.control) & 3);
    e.driver_kind = u8(c.driver_kind & 15);
    return e;
}

CarSnapshot make_snapshot(const sim::Session& s, size_t car, const CarEntry& entry, u32 tick, s32 clock, int laps, const PadInput* inputs, int n) {
    CarSnapshot r;
    r.tick = tick;
    r.clock = clock;
    r.motion = car_motion(s, car);
    r.entry = entry;
    r.laps = u8(std::clamp(laps, 0, 255));
    r.rev_limiter = s.record(car).rev_limiter != 0;
    r.input_count = u8(std::clamp(n, 0, gt3r::net::kInputHistory));
    for (int i = 0; i < r.input_count; ++i) r.inputs[i] = inputs[i];
    complete_snapshot(r);
    return r;
}

bool sane(CarSnapshot& s, int gears) {
    const CarMotion& m = s.motion;
    auto within = [](u32 bits, float most) {
        if ((bits & 0x7F800000u) == 0x7F800000u) return false;
        return std::fabs(F::raw(bits).host()) <= most;
    };
    for (const u32 v : m.pos)
        if (!within(v, 1e5f)) return false;
    for (const u32 v : m.vel)
        if (!within(v, 500.0f)) return false;
    if (!within(m.heading, 1e3f) || !within(m.pitch, 7.0f) || !within(m.roll, 7.0f) || !within(m.yaw_rate, 100.0f)) return false;
    if (!within(m.omega, 1e4f) || !within(m.steer_cmd, 4.0f) || !within(m.axle_speed[0], 1e4f) || !within(m.axle_speed[1], 1e4f)) return false;
    if (int(m.gear) > std::max(1, gears) || m.clutch > 3) return false;
    if (s.clock < -3000 * 600 || s.clock > 3000 * 86400) return false;
    for (int i = 0; i < s.input_count && i < gt3r::net::kInputHistory; ++i) {
        PadInput& p = s.inputs[i];
        const u8 kind = p.kind & 7;
        if (kind == 1 || kind == 5 || kind == 7) p.kind = u8((p.kind & 8) | 3);
        p.steer = std::clamp<s16>(p.steer, -4096, 4096);
        for (u16* a : {&p.throttle, &p.brake, &p.back, &p.third}) *a = std::min<u16>(*a, 4096);
        p.shift = std::clamp<s8>(p.shift, -1, 1);
    }
    return true;
}

void complete_snapshot(CarSnapshot& s) {
    const CarMotion& m = s.motion;
    auto h = [](u32 bits) { return F::raw(bits).host(); };
    s.race_time = float(double(s.clock) / 3000.0);
    gt3r::sim::VehicleState& st = s.state;
    st = {};
    st.position = {h(m.pos[0]), h(m.pos[2]), -h(m.pos[1])};
    st.velocity = {h(m.vel[0]), h(m.vel[2]), -h(m.vel[1])};
    st.speed_kmh = st.velocity.length() * 3.6f;
    st.gear = m.gear == 0 ? -1 : int(m.gear);
    st.rev_limiter = s.rev_limiter;
    s.body_x = st.position.x, s.body_z = st.position.z;
    s.heading = h(m.heading), s.yaw_rate = h(m.yaw_rate);
    s.brake = s.input_count ? std::min(1.0f, float(s.inputs[0].brake) / 4096.0f) : 0.0f;
}

} // namespace gt4r::net
