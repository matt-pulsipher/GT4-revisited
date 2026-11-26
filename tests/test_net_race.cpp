// The race of an online race between machines in one process (docs/specs/net.md "Race").

#include "net_machines.h"

#include "gt4r/net/lan_rules.h"
#include "base/bytes.h"
#include "disc/core_image.h"

#include <bit>
#include <chrono>
#include <cstdlib>

using namespace gt4r;
using namespace gt4r::test::net;
using gt3r::ee::F;

namespace {

struct Run {
    Errors e, apart; // (apart: no contact in the last two seconds)
    s64 contact = 0; // steps in contact
    u32 warnings = 0;
    std::string warning;
};
enum class Kind { Own, Dead };
Run run(World& w, std::span<const std::string> labels, const Link& link, int seconds, bool smooth, Kind kind) {
    Race r(w, labels, link, 0, [&](std::vector<sim::Entry>& e) {
        if (!smooth) e[0].automatic = false;
    });
    for (size_t m = 0; m < r.machines.size(); ++m) {
        r.machines[m]->drive = smooth ? weaving(1.0f, 0.25f, double(m) * 1.3) : jerky(u32(77 + m * 1000), m == 0);
        for (size_t k = 1; k < labels.size(); ++k) r.machines[m]->field->remote(k).set_physics(kind == Kind::Own);
    }
    Run out;
    int touched = -1000;
    std::vector<s64> was(r.machines.size(), 0);
    for (int k = 0; k < seconds * 60; ++k) {
        r.step_all();
        for (size_t m = 0; m < r.machines.size(); ++m) {
            const race::CarState& me = r.machines[m]->session->cars()[0];
            if (r.machines[m]->contact_steps != was[m] || me.contact_kind != 0) touched = k, ++out.contact, was[m] = r.machines[m]->contact_steps;
        }
        if (k < 30) continue;
        for (size_t m = 0; m < r.machines.size(); ++m)
            for (size_t c = 0; c < r.machines.size(); ++c)
                if (c != m) {
                    out.e.add(r.error(m, c));
                    if (k - touched > 120) out.apart.add(r.error(m, c));
                }
    }
    for (auto& m : r.machines) out.warnings += m->warnings, out.warning += m->warning;
    return out;
}

} // namespace

// The remote car against its owner's, at several delays and losses (GT4R_NET_TABLE=1: every one of the specification's table).
TEST(net_race_remote_car_error) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    World w(*disc, "tsukuba");
    const std::vector<std::string> labels{"nsx_r_02", "r34_gtr_v2_nur_02"};
    const bool table = std::getenv("GT4R_NET_TABLE") != nullptr;
    struct Case {
        Link link;
        bool smooth;
        double rms_cm, worst_cm; // at most
    };
    std::vector<Case> cases{{{0, 0, 0}, true, 0.01, 0.1}, {{50, 10, 0.05}, true, 0.5, 8}, {{100, 20, 0.05}, true, 2, 20}, {{100, 20, 0.2}, false, 12, 150}};
    if (table) {
        cases.clear();
        for (bool smooth : {true, false})
            for (double ms : {0.0, 50.0, 100.0, 250.0})
                for (double loss : {0.0, 0.05, 0.2}) cases.push_back({{ms, ms * 0.2, loss}, smooth, 1e9, 1e9});
    }
    for (const Case& c : cases) {
        const Run own = run(w, labels, c.link, table ? 60 : 20, c.smooth, Kind::Own);
        const Run dead = table ? run(w, labels, c.link, 60, c.smooth, Kind::Dead) : Run{};
        std::printf("  %s %3.0f ms + %2.0f, loss %2.0f %%: rms %6.2f cm, worst %6.1f cm (%lld steps in contact; without: %.2f / %.1f); dead reckoning rms %6.2f, worst %6.1f; warnings %u %s\n", c.smooth ? "smooth" : "jerky ", c.link.delay_ms,
                    c.link.jitter_ms, c.link.loss * 100, own.e.rms() * 100, own.e.worst * 100, (long long)own.contact, own.apart.rms() * 100, own.apart.worst * 100, dead.e.rms() * 100, dead.e.worst * 100, own.warnings, own.warning.c_str());
        CHECK(own.e.rms() * 100 < c.rms_cm && own.e.worst * 100 < c.worst_cm);
        CHECK(own.warnings == 0);
    }
}

// A car driven into the one ahead: both machines feel the hit (each solves it for its own car), the cars do not pass through
// each other, and the impulse the other machine sent is not applied where ours saw it too.
TEST(net_race_contact) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    World w(*disc, "tsukuba");
    const std::vector<std::string> labels{"nsx_r_02", "r34_gtr_v2_nur_02"};
    for (const Link& link : {Link{0, 0, 0}, Link{50, 5, 0.02}, Link{100, 10, 0.05}}) {
        Race r(w, labels, link);
        r.machines[0]->drive = weaving(0.35f, 0.0f); // ahead, slow
        r.machines[1]->drive = chasing();            // from behind
        Errors e, near;
        s64 first[2] = {-1, -1};
        double least = 1e9;
        for (int k = 0; k < 20 * 60; ++k) {
            r.step_all();
            for (size_t m = 0; m < 2; ++m)
                if (first[m] < 0 && r.machines[m]->contact_steps) first[m] = r.machines[m]->clock;
            e.add(r.error(0, 1)), e.add(r.error(1, 0));
            const course::Vec3 a = r.place(0, 0), b = r.place(1, 1);
            least = std::min(least, std::hypot(double(a.x - b.x), double(a.z - b.z)));
        }
        const auto &f0 = *r.machines[0]->field, &f1 = *r.machines[1]->field;
        std::printf("  %3.0f ms: first contact at %.2f s / %.2f s; steps in contact %lld / %lld; hits sent %u / %u, applied %u / %u; least distance between centers %.2f m; error rms %.1f cm, worst %.0f cm; warnings %u %s\n",
                    link.delay_ms, double(first[0]) / 3000, double(first[1]) / 3000, (long long)r.machines[0]->contact_steps, (long long)r.machines[1]->contact_steps, f0.hits_sent(), f1.hits_sent(),
                    f0.hits_applied(), f1.hits_applied(), least, e.rms() * 100, e.worst * 100, r.machines[0]->warnings + r.machines[1]->warnings, r.machines[0]->warning.c_str());
        // (each machine solved the hit, or was told of one it did not see)
        CHECK((first[0] > 0 || f0.hits_applied() > 0) && (first[1] > 0 || f1.hits_applied() > 0));
        if (first[0] > 0 && first[1] > 0) CHECK(std::abs(first[0] - first[1]) <= 300 + s64(link.delay_ms * 6));
        CHECK(least > 1.5); // (never in each other)
        CHECK(r.machines[0]->warnings + r.machines[1]->warnings == 0);
    }
}

// What a posed car does to ours and not to itself, and what a ghost does not.
TEST(net_race_posed_car) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    World w(*disc, "tsukuba");
    std::vector<sim::Entry> e = w.entries(std::vector<std::string>{"nsx_r_02", "r34_gtr_v2_nur_02"});
    for (const bool ghost : {false, true}) {
        std::unique_ptr<sim::Session> s = w.session(e);
        for (int k = 0; k < 60; ++k) s->hold_once({});
        const std::unique_ptr<sim::Session> other = s->solo(1);
        s->set_posed(1, true);
        s->set_ghost(1, ghost);
        // The other car driven at ours from 12 m behind, at 15 m/s, ours standing on its brakes.
        const auto me = s->record(0);
        gt3r::sim::exact::Car c = other->record(0);
        const sim::Extra x = other->gt4_record(0);
        const gt3r::sim::exact::Vec3 was = c.pos;
        const F fx = me.fwd.x, fy = me.fwd.y, back(12.0f), v(15.0f);
        c.pos = {me.pos.x - fx * back, me.pos.y - fy * back, c.pos.z};
        c.heading = me.heading, c.pitch = me.pitch, c.roll = me.roll;
        c.vel = {fx * v, fy * v, F()};
        c.yaw_rate = F();
        other->load_state(0, c, x);
        other->settle_moved(0, was);
        race::CarInput brake;
        brake.brake = 1;
        const F dt(1.0f / 60);
        bool touched = false;
        float push = 0;
        for (int k = 0; k < 90; ++k) {
            gt3r::sim::exact::Car n = other->record(0);
            const gt3r::sim::exact::Vec3 at = n.pos;
            n.pos = {n.pos.x + n.vel.x * dt, n.pos.y + n.vel.y * dt, n.pos.z};
            other->load_state(0, n, other->gt4_record(0));
            other->settle_moved(0, at);
            s->pose(1, *other);
            s->set_ghost(1, ghost);
            s->step_once({&brake, 1});
            // Its record keeps the pose, whatever it met.
            CHECK(s->record(1).pos.x.bits == n.pos.x.bits && s->record(1).vel.x.bits == n.vel.x.bits && s->record(1).index == 1);
            for (const sim::Session::CarContact& h : s->car_contacts()) {
                touched = true;
                // The impulse on ours points along its motion.
                const float sign = h.struck == 0 ? 1.0f : -1.0f;
                push += sign * (h.jx * fx.host() - h.jz * fy.host());
            }
        }
        std::printf("  %s: touched %d, impulse on ours along its way %.0f N s, ours at %.2f m/s\n", ghost ? "a ghost" : "a car", touched, push, s->cars()[0].speed);
        CHECK(touched == !ghost);
        if (!ghost) CHECK(push > 1000);
        else CHECK(std::fabs(s->cars()[0].speed) < 0.2f);
    }
    // The impulse of a hit another machine saw: the velocity by j / m, the yaw by its moment.
    std::unique_ptr<sim::Session> s = w.session(e);
    const float m = s->record(0).p.mass.host();
    s->apply_impulse(0, s->cars()[0].position.x, s->cars()[0].position.z, m * 2.0f, 0);
    CHECK(std::fabs(s->record(0).vel.x.host() - 2.0f) < 0.01f);
}

// Our machine's frames stall and its steps come in a burst: the other car is where it was at our race's time, and no warning.
TEST(net_race_stalled_machine) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    World w(*disc, "tsukuba");
    const std::vector<std::string> labels{"nsx_r_02", "r34_gtr_v2_nur_02"};
    Race r(w, labels, {20, 4, 0});
    r.machines[0]->drive = weaving(1.0f, 0.2f), r.machines[1]->drive = weaving(1.0f, 0.2f, 1);
    Errors e;
    std::vector<std::pair<s64, course::Vec3>> owner; // machine 1's own car by its clock
    int behind = 0;
    for (int k = 0; k < 20 * 60; ++k) {
        r.step(1);
        owner.push_back({r.machines[1]->clock, r.place(1, 1)});
        const bool stall = (k % 97) < 12;
        if (stall) ++behind;
        else
            for (int n = behind + 1; n > 0; --n) {
                if (n == 1) behind = 0;
                r.step(0);
                if (k < 60) continue;
                for (auto o = owner.rbegin(); o != owner.rend(); ++o)
                    if (o->first == r.machines[0]->clock) {
                        const course::Vec3 p = r.place(0, 1);
                        e.add(std::hypot(double(p.x - o->second.x), double(p.z - o->second.z)));
                        break;
                    }
            }
        r.now_ms += 1000.0 / 60;
    }
    std::printf("  stalls of 12 frames: rms %.2f cm, worst %.1f cm; warnings %u\n", e.rms() * 100, e.worst * 100, r.machines[0]->warnings);
    CHECK(e.n > 800 && e.worst < 0.5 && r.machines[0]->warnings == 0);
}

// Six machines, a car each, all on one wire.
TEST(net_race_six_cars) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    World w(*disc, "tsukuba");
    const std::vector<std::string> labels{"nsx_r_02", "r34_gtr_v2_nur_02", "mtr_gmv_04", "prius_g_02", "s500_63", "civic_r_98"};
    Race r(w, labels, {40, 8, 0.02}, 120);
    for (size_t m = 0; m < r.machines.size(); ++m) r.machines[m]->drive = weaving(1.0f, 0.15f, double(m));
    Errors e;
    u32 contacts = 0, warnings = 0;
    for (int k = 0; k < 12 * 60; ++k) {
        r.step_all();
        for (size_t m = 0; m < 6; ++m)
            for (size_t c = 0; c < 6; ++c)
                if (c != m && k > 10) e.add(r.error(m, c));
    }
    for (auto& m : r.machines) contacts += m->field->hits_sent(), warnings += m->warnings;
    std::printf("  six cars: rms %.2f cm, worst %.1f cm; hits sent %u, warnings %u\n", e.rms() * 100, e.worst * 100, contacts, warnings);
    CHECK(e.worst < 2.0 && warnings == 0);
}

// The battle's own rules as the program has them: the tire wear's curve and the boost by the host's settings.
TEST(net_lan_rules) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const sim::Program program = sim::Program::load(*disc);
    const std::array<u8, 9> none = net::lan_wear(program, 0), one = net::lan_wear(program, 1), two = net::lan_wear(program, 2);
    CHECK(none == (std::array<u8, 9>{}));
    auto show = [](const std::array<u8, 9>& b) {
        for (u8 v : b) std::printf(" %d", v);
    };
    std::printf("  wear 1:"), show(one), std::printf("; 2:"), show(two);
    // (a faster wear for 2: shorter distances; both on, past the same load)
    CHECK(one[0] > two[0] && one[0] > 0 && one[1] == two[1] && one[2] == two[2] && one[3] == 1 && two[3] == 1);
    sim::ai::Pace p0, p1, p2;
    net::lan_boost(program, 0, p0), net::lan_boost(program, 1, p1), net::lan_boost(program, 2, p2);
    std::printf("; boost 1: %d %% from %d m to %d m, 2: %d %% from %d m to %d m\n", p1.fast_pct, p1.fast_start_m, p1.fast_full_10m * 10, p2.fast_pct, p2.fast_start_m, p2.fast_full_10m * 10);
    CHECK(p0.fast_pct == 0 && p1.fast_pct > 0 && p2.fast_pct > p1.fast_pct && p2.fast_start_m < p1.fast_start_m && p1.fast_full_10m == p2.fast_full_10m && p1.slow_pct == 0);
    // Our values are the constants the routines load in the disc's program (each an addiu / ori from $zero).
    const std::shared_ptr<const gt3r::CoreImage> core = disc->program_image();
    auto c = [&](u32 at) {
        const u32 w = gt3r::rd32(core->program, at - core->load_address), op = w >> 26;
        return (op == 9 || op == 13) && ((w >> 21) & 31) == 0 ? int(w & 0xFF) : -1;
    };
    CHECK(one == (std::array<u8, 9>{u8(c(0x354F60)), u8(c(0x354F64)), u8(c(0x354F68)), 1, u8(c(0x354F58)), u8(c(0x354F5C)), 0, u8(c(0x354F58)), u8(c(0x354F58))}));
    CHECK(two == (std::array<u8, 9>{u8(c(0x354FA0)), u8(c(0x354FA4)), u8(c(0x354FA8)), u8(c(0x354F34)), u8(c(0x354F4C)), u8(c(0x354F98)), 0, u8(c(0x354F9C)), u8(c(0x354F4C))}));
    CHECK(p1.fast_pct == c(0x354EDC) && p1.fast_start_m == c(0x354ED8) && p1.fast_full_10m == c(0x354ED8));
    CHECK(p2.fast_pct == c(0x354EF8) && p2.fast_start_m == c(0x354EF8) && p2.fast_full_10m == c(0x354ECC));
}

// A car put somewhere else by its own race (a pit stop's box): the others take it there at once, and no check takes it for a
// teleport.
TEST(net_race_car_put) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    World w(*disc, "tsukuba");
    Race r(w, std::vector<std::string>{"nsx_r_02", "r34_gtr_v2_nur_02"}, {50, 5, 0.02});
    r.machines[0]->drive = weaving(1.0f, 0), r.machines[1]->drive = weaving(0.8f, 0);
    for (int k = 0; k < 600; ++k) r.step_all();
    sim::Session& s = *r.machines[0]->session;
    const auto& c = s.record(0);
    s.place_standing(0, c.pos.x - c.lat.x * F(6.0f), c.pos.y - c.lat.y * F(6.0f), c.pos.z, c.heading);
    Errors late;
    for (int k = 0; k < 240; ++k) {
        r.step_all();
        if (k > 60) late.add(r.error(1, 0));
    }
    std::printf("  put 6 m aside and stood: a second later rms %.2f cm, worst %.1f cm; warnings %u %s\n", late.rms() * 100, late.worst * 100, r.machines[1]->warnings, r.machines[1]->warning.c_str());
    CHECK(late.worst < 0.5 && r.machines[1]->warnings == 0);
}

// States no game sends: refused, or taken without harm.
TEST(net_race_hostile_states) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    World w(*disc, "tsukuba");
    Race r(w, std::vector<std::string>{"nsx_r_02", "prius_g_02"}, {});
    r.machines[0]->drive = weaving(1.0f, 0), r.machines[1]->drive = weaving(1.0f, 0);
    for (int k = 0; k < 60; ++k) r.step_all();
    Machine& m = *r.machines[0];
    u32 seed = 99;
    auto rnd = [&] { return seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, seed; };
    u32 tick = 1000, taken = 0;
    for (int k = 0; k < 3000; ++k) {
        net::CarSnapshot s = m.field->remote(1).latest();
        s.tick = ++tick, s.clock = s32(m.clock) + s32(rnd() % 2000) - 1000;
        // Some of its bytes anything at all.
        gt3r::net::Writer out;
        gt3r::net::write_snapshot(out, s);
        for (u32 n = rnd() % 12; n > 0; --n) out.data[15 + rnd() % (out.data.size() - 15)] = u8(rnd());
        gt3r::net::Reader in(out.data);
        net::CarSnapshot t;
        if (!gt3r::net::read_snapshot(in, t)) continue;
        const u32 before = m.field->remote(1).states();
        m.field->receive(1, t, m.clock);
        taken += m.field->remote(1).states() - before;
        if (rnd() % 4 == 0) m.field->take_hit(1, {tick, std::bit_cast<float>(rnd()), std::bit_cast<float>(rnd()), std::bit_cast<float>(rnd()), std::bit_cast<float>(rnd())}, m.clock);
        if (k % 3 == 0) {
            m.field->before_step(m.clock);
            race::CarInput in0;
            m.session->step_once({&in0, 1});
            m.clock += net::kClockPerStep;
            m.field->after_step(m.clock);
            const race::CarState& me = m.session->cars()[0];
            CHECK(std::isfinite(me.position.x) && std::isfinite(me.position.y) && std::isfinite(me.speed));
        }
    }
    std::printf("  %u odd states taken\n", taken);
    CHECK(taken > 200);
}

// What a remote car costs: its prediction's steps.
TEST(net_race_prediction_cost) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    World w(*disc, "tsukuba");
    Race r(w, std::vector<std::string>{"nsx_r_02", "r34_gtr_v2_nur_02"}, {});
    r.machines[0]->drive = weaving(1.0f, 0), r.machines[1]->drive = weaving(1.0f, 0);
    for (int k = 0; k < 300; ++k) r.step_all();
    const std::unique_ptr<sim::Session> s = r.machines[0]->session->solo(1);
    sim::Session::Memory m;
    s->save(0, m);
    race::CarInput in;
    in.throttle = 1;
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < 2000; ++k) {
        s->step_once({&in, 1});
        if (k % 10 == 9) s->restore(0, m);
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 2000;
    std::printf("  a step of a remote car alone: %.1f us (five of them, seven steps each: %.2f ms a frame)\n", us, us * 35 / 1000);
    CHECK(us < 2000);
}
