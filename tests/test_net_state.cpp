// A car's state as it travels (docs/specs/net.md "The car's state"): into the wire format and back, put into another machine's
// car of the same kind.

#include "net_machines.h"

#include <cmath>
#include <cstring>

using namespace gt4r;
using namespace gt4r::test::net;
using gt3r::ee::F;

namespace {

// Driven for a while on the pad, a car's state goes out, through the bytes and into a fresh car of its kind.
struct Trip {
    double worst_rate = 0, worst_rim = 0, worst_susp = 0; // the quantized fields' errors
    bool exact = true, frames = true;
    bool clipped = false; // a quantized field past what its 16 bits hold
    size_t bytes = 0;
};
Trip trip(World& w, const sim::Entry& e) {
    Trip t;
    std::unique_ptr<sim::Session> a = w.session({&e, 1}), b = w.session({&e, 1});
    sim::LineDriver pad(*a, 0);
    for (int k = 0; k < 30; ++k) a->hold_once({}), b->hold_once({});
    net::PadInput hist[3];
    race::PadRecord rec;
    for (int k = 0; k < 240; ++k) {
        race::CarInput in = pad.drive(1.0f, true);
        in.steer = std::clamp(in.steer + 0.3f * std::sin(float(k) * 0.1f), -1.0f, 1.0f);
        if (k > 200) in.throttle = 0, in.brake = 0.4f;
        in.handbrake = k > 180 && k < 190;
        rec = pad_of(in);
        if (k == 120) rec.bytes[7] = 1; // (the nitrous button)
        race::CarInput p;
        p.pad = &rec;
        a->step_once({&p, 1});
        for (int h = 2; h > 0; --h) hist[h] = hist[h - 1];
        hist[0] = net::pad_input(p);
        if (k == 120) t.frames = t.frames && net::pad_record(hist[0]).bytes[7] == 1;
        t.frames = t.frames && net::pad_record(hist[0]).bytes == rec.bytes;
    }
    net::CarSnapshot s = net::make_snapshot(*a, 0, net::car_entry(*a, 0, e.asm_on, e.tcs_on), 240, 240 * net::kClockPerStep, 0, hist, 3), r;
    gt3r::net::Writer out;
    gt3r::net::write_snapshot(out, s);
    t.bytes = out.data.size();
    gt3r::net::Reader in(out.data);
    if (!gt3r::net::read_snapshot(in, r) || !in.at_end()) t.exact = false;
    for (int h = 0; h < 3; ++h) t.frames = t.frames && r.inputs[h] == hist[h];
    net::put_motion(*b, 0, r.motion);
    const auto &ca = a->record(0), &cb = b->record(0);
    auto same = [&](F x, F y) { t.exact = t.exact && x.bits == y.bits; };
    same(ca.pos.x, cb.pos.x), same(ca.pos.y, cb.pos.y), same(ca.pos.z, cb.pos.z), same(ca.vel.x, cb.vel.x), same(ca.vel.y, cb.vel.y), same(ca.vel.z, cb.vel.z);
    same(ca.heading, cb.heading), same(ca.pitch, cb.pitch), same(ca.roll, cb.roll), same(ca.yaw_rate, cb.yaw_rate), same(ca.omega, cb.omega), same(ca.steer_cmd, cb.steer_cmd);
    same(ca.axle_speed[0], cb.axle_speed[0]), same(ca.axle_speed[1], cb.axle_speed[1]);
    same(ca.fwd.x, cb.fwd.x), same(ca.up.z, cb.up.z), same(ca.w[2].hub.x, cb.w[2].hub.x);
    t.exact = t.exact && ca.gear == cb.gear && ca.clutch == cb.clutch;
    auto off = [&](F x, F y) { return std::fabs(double(x.host()) - double(y.host())); };
    t.worst_rate = std::max(off(ca.pitch_rate, cb.pitch_rate), off(ca.roll_rate, cb.roll_rate));
    for (size_t i = 0; i < 4; ++i) {
        t.worst_rim = std::max(t.worst_rim, off(ca.w[i].rim_speed, cb.w[i].rim_speed)), t.worst_susp = std::max(t.worst_susp, off(ca.w[i].susp, cb.w[i].susp));
        t.clipped = t.clipped || std::fabs(ca.w[i].susp.host()) > 0.49f || std::fabs(ca.w[i].rim_speed.host()) > 250;
    }
    return t;
}

} // namespace

TEST(net_state_round_trip) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    World w(*disc, "tsukuba");
    // Every eighth car, and those with a layout or gearbox of their own: three wheels, stepless and hybrid boxes, motors.
    std::vector<std::string> cars{"mtr_gmv_04", "prius_g_02", "prius_g_touring_03", "acura_dnx_02", "nsx_r_02", "r34_gtr_v2_nur_02"};
    const std::vector<db::CarEntry>& all = w.db.cars();
    for (size_t i = 0; i < all.size(); i += 8) cars.push_back(all[i].label);
    Trip worst;
    size_t n = 0, bytes_max = 0;
    for (const std::string& label : cars) {
        sim::Entry e;
        if (!sim::stock_car(w.db, w.program, label, e.equipment)) continue;
        Trip t;
        try {
            t = trip(w, e);
        } catch (const std::exception&) {
            continue;
        }
        ++n;
        if (!t.exact || !t.frames || t.clipped) std::printf("  %s: %s%s%s\n", label.c_str(), t.exact ? "" : "not exact ", t.frames ? "" : "frames differ ", t.clipped ? "clipped" : "");
        CHECK(t.exact && t.frames && !t.clipped);
        worst.worst_rate = std::max(worst.worst_rate, t.worst_rate), worst.worst_rim = std::max(worst.worst_rim, t.worst_rim), worst.worst_susp = std::max(worst.worst_susp, t.worst_susp);
        bytes_max = std::max(bytes_max, t.bytes);
    }
    std::printf("  %zu cars: rates within %.6f rad/s, rim speeds %.6f m/s, suspension %.7f m; a state %zu bytes at most\n", n, worst.worst_rate, worst.worst_rim, worst.worst_susp, bytes_max);
    CHECK(n > 80);
    CHECK(worst.worst_rate <= 0.5 / 1024 + 1e-6 && worst.worst_rim <= 0.5 / 128 + 1e-4 && worst.worst_susp <= 0.5 / 65536 + 1e-7);
}
