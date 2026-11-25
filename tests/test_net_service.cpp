// What changes another player's car outside its state (docs/specs/net.md "Service"): a pit stop's hold, box, tires and fuel
// sent with the car-info block and applied to the others' predictions where the owner's car met them.

#include "net_machines.h"

#include "gt4r/net/car_service.h"
#include "gt4r/net/lan_rules.h"
#include "gt4r/sim/ai/pit_stops.h"
#include "gt4r/sim/ai/pit_tire_change.h"
#include "gt4r/sim/pit_tires.h"

#include <cstdlib>
#include <cstring>
#include <map>

using namespace gt4r;
using namespace gt4r::test::net;
using gt4r::net::ServiceEvent;

namespace {

sim::FileReader reader(Disc& disc) {
    gt3r::Volume* vol = &disc.volume();
    return [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    };
}

// A kind the pit has a part for on `axle` other than the one fitted (-1: none), the softest (or the hardest).
int other_kind(const sim::PitTires& t, int axle, bool hardest = false) {
    const int lo = int(t.least[size_t(axle)]), hi = int(t.most[size_t(axle)]);
    for (int i = 0; i <= hi - lo; ++i)
        if (const int k = hardest ? lo + i : hi - i; k != t.fitted[size_t(axle)] && t.part(axle, k) != db::kNoPart) return k;
    return -1;
}

// Machine 0's car is driven by its computer driver (B-spec) and sent into the pits at `pit_at` s, where it takes the other
// kind of tires and fuel; its pit stop's events go into its service log, as the race screen's hooks put them there.
struct PitOwner {
    std::unique_ptr<sim::ai::Field> field;
    std::unique_ptr<sim::ai::PitStops> pits;
    sim::ai::PitHooks hooks;
    int want = -1;
    bool chose = false;
    s64 boxed = -1, left = -1; // the race's clock at the box and when the hold ran out
};

struct Result {
    Errors stop, after, after_apart; // during the stop (the box to the hold's end); 30 s after it; and those 2 s from a contact
    float grip_owner = 0, grip_seen = 0, fuel_owner = 0, fuel_seen = 0; // (the owner's car and the other's of it at its newest state)
    float wear_owner = 0, wear_seen = 0;
    int kinds_before = -1, kinds_after = -1;
    s64 contact = 0;
    u32 reruns = 0, warnings = 0;
    std::string warning;
};

Result pit_race(World& w, const Link& link, bool service, bool hardest, int seconds = 240) {
    const std::vector<std::string> labels{"ka_01", "s500_63"};
    // (a race that counts fuel, the stop filling the tank; and wears tires, the LAN battle's setting 1)
    const std::array<u8, 9> wear = gt4r::net::lan_wear(w.program, 1);
    u8 block[0x80] = {};
    std::copy(wear.begin(), wear.end(), block + 0x2C);
    Race r(
        w, labels, link, sim::Session::kHoldSteps, [](std::vector<sim::Entry>& e) {
            for (sim::Entry& x : e) x.fuel = true;
        },
        [&](sim::Session& s) { s.set_wear_rules(block, false, w.program); });
    if (const char* seed = std::getenv("GT4R_NET_SEED")) r.seed = u32(std::strtoul(seed, nullptr, 0)) | 1;
    auto owner = std::make_shared<PitOwner>();
    Machine& m0 = *r.machines[0];
    const std::vector<size_t> computer{0};
    owner->field = sim::ai::Field::open(*w.disc, w.course, *m0.session, computer);
    owner->field->set_bspec(0);
    sim::ai::PitHooks h = sim::ai::session_pit_hooks(*m0.session);
    sim::ai::add_tire_change(h, *m0.session, w.db, w.program, reader(*w.disc));
    const std::pair<int, int> fitted = h.tires(0);
    {
        sim::Equipment eq = m0.session->equipment(0);
        const sim::PitTires t = sim::pit_tires(w.db, w.program, eq, sim::fitting_parts(reader(*w.disc), w.db, eq.part(sim::Equipment::Car)));
        owner->want = other_kind(t, 0, hardest);
    }
    // (the hooks as the race screen's: the player's car's service logged by the race's clock; before the step its start's,
    // after it its end's)
    Machine* mp = &m0;
    auto note = [mp](bool after, ServiceEvent::Kind k, u32 v) { mp->service.add({gt4r::net::s32(mp->clock + (after ? gt4r::net::kClockPerStep : 0)), k, v}); };
    using K = ServiceEvent::Kind;
    h.hold = [f = h.hold, note](size_t i, u16 n) { f(i, n), i == 0 ? note(false, K::Hold, n) : void(); };
    h.change_tires = [f = h.change_tires, note](size_t i, u8 a, u8 b, u8 x) { f(i, a, b, x), i == 0 ? note(false, K::Tires, u32(a) | u32(b) << 8 | u32(x) << 16) : void(); };
    h.retune_tires = [f = h.retune_tires, note](size_t i, u8 x) { f(i, x), i == 0 ? note(false, K::Grips, x) : void(); };
    h.new_tire = [f = h.new_tire, note](size_t i, int wh) { f(i, wh), i == 0 ? note(false, K::NewTires, 1u << wh) : void(); };
    h.place_in_box = [f = h.place_in_box, note, owner, mp](size_t i, gt3r::ee::F hd) {
        f(i, hd);
        if (i == 0) note(true, K::Box, hd.bits), owner->boxed = mp->clock + gt4r::net::kClockPerStep;
    };
    h.face_box = [f = h.face_box, note](size_t i, gt3r::ee::F hd) { f(i, hd), i == 0 ? note(true, K::Face, hd.bits) : void(); };
    h.set_fuel = [f = h.set_fuel, note](size_t i, gt3r::ee::F l) { f(i, l), i == 0 ? note(true, K::Fuel, l.bits) : void(); };
    owner->hooks = h;
    owner->pits = std::make_unique<sim::ai::PitStops>(*owner->field, w.program, m0.session->cars().size(), h);
    m0.inputs = [owner](Machine& m, std::vector<race::CarInput>& in) {
        if (m.clock < 0) return owner->field->hold(in);
        if (m.clock == 20 * 3000) owner->pits->request(0);
        const sim::ai::PitCar& c = owner->pits->car(0);
        if (!owner->chose && (c.service & 0x10) && c.hold == 721) owner->pits->choose(0, u8(owner->want), u8(owner->want), 1), owner->chose = true;
        owner->field->drive(in);
        owner->pits->before(*owner->field, in);
    };
    m0.after = [owner](Machine& m) {
        if (m.clock < 0) return;
        owner->pits->after(*m.session, *owner->field);
        if (owner->boxed >= 0 && owner->left < 0 && owner->pits->car(0).hold == 0) owner->left = m.clock + gt4r::net::kClockPerStep;
    };
    r.machines[1]->drive = weaving(0.9f, 0.2f, 1.3);
    r.machines[1]->take_service = service;
    // (the other machine's predictions change tires as the program does)
    for (auto& m : r.machines) m->field->set_tire_change(gt4r::net::tire_change_on(w.db, w.program, reader(*w.disc)));

    Result out;
    out.kinds_before = fitted.first;
    struct Owned {
        float grip, fuel, wear;
    };
    std::map<s64, Owned> owned; // (the owner's car by the race's clock)
    int touched = -1000;
    s64 was_contact = 0;
    const int steps = (seconds * 60) + sim::Session::kHoldSteps;
    for (int k = 0; k < steps && !(owner->left >= 0 && r.machines[1]->clock > owner->left + 30 * 3000); ++k) {
        r.step_all();
        Machine& m1 = *r.machines[1];
        {
            const sim::Session& o = *m0.session;
            owned[m0.clock] = {o.gt4_record(0).tires.tire[0].mu[0].host(), o.fuel(0), o.record(0).w[0].wear.host()};
        }
        if (r.machines[0]->contact_steps + m1.contact_steps != was_contact || m1.session->cars()[0].contact_kind != 0)
            touched = k, ++out.contact, was_contact = r.machines[0]->contact_steps + m1.contact_steps;
        const s64 clock = m1.clock;
        if (owner->boxed < 0 || clock <= owner->boxed) continue;
        const double e = r.error(1, 0);
        if (owner->left < 0 || clock <= owner->left) out.stop.add(e);
        else if (clock <= owner->left + 30 * 3000) {
            out.after.add(e);
            if (k - touched > 120) out.after_apart.add(e);
        }
    }
    const gt4r::net::RemoteCar& seen = r.machines[1]->field->remote(1);
    if (const sim::Session::Memory* b = seen.base(); b && owned.count(seen.base_clock())) {
        const Owned& o = owned[seen.base_clock()];
        out.grip_owner = o.grip, out.fuel_owner = o.fuel, out.wear_owner = o.wear;
        out.grip_seen = b->gt4.tires.tire[0].mu[0].host(), out.fuel_seen = b->gt4.drive.fuel.fuel.host(), out.wear_seen = b->car.w[0].wear.host();
    }
    out.kinds_after = owner->hooks.tires(0).first;
    out.reruns = r.machines[1]->field->remote(1).reruns();
    for (auto& m : r.machines) out.warnings += m->warnings, out.warning += m->warning;
    std::printf("    (%s: the box at %.2f s, out at %.2f s, %d stop, kind %d, fuel %.2f of %.2f)\n", service ? "with" : "without", double(owner->boxed) / 3000, double(owner->left) / 3000,
                owner->pits->stops(0), owner->field->kind(0), double(m0.session->fuel(0)), double(m0.session->tank(0)));
    if (owner->boxed < 0 || owner->left < 0) out.stop = {}, out.after = {};
    return out;
}

} // namespace

// The section of a car-info block: written after the lobby's record (which reads as before), read back, cut to fit.
TEST(net_service_section) {
    gt3r::net::Writer rec;
    rec.u8_(1), rec.str("Player1"), rec.u32_(0xDEADBEEF);
    gt4r::net::CarService s;
    s.round = 7;
    s.events = {{-50, ServiceEvent::Hold, 721}, {100, ServiceEvent::Box, 0x40490FDB}, {150, ServiceEvent::Hold, 721 | 0x10000},
                {150, ServiceEvent::Tires, 5 | 5 << 8 | 3 << 16}, {200, ServiceEvent::Grips, 3}, {900, ServiceEvent::NewTires, 0xF},
                {40000, ServiceEvent::Fuel, 0x41A00000}, {40050, ServiceEvent::Hold, 90 | 0x10000}};
    const Bytes b = gt4r::net::with_service(rec.data, s);
    const ByteSpan back = gt4r::net::record_of(b);
    CHECK(Bytes(back.begin(), back.end()) == rec.data);
    const std::optional<gt4r::net::CarService> got = gt4r::net::service_of(b);
    CHECK(got && *got == s);
    // Again on a block that has one: the old section replaced.
    s.events.resize(2);
    const Bytes b2 = gt4r::net::with_service(b, s);
    CHECK(gt4r::net::service_of(b2) && *gt4r::net::service_of(b2) == s);
    CHECK(gt4r::net::record_of(b2).size() == rec.data.size());
    CHECK(!gt4r::net::service_of(rec.data));
    // Cut to the limit: the oldest left out.
    std::vector<ServiceEvent> many;
    for (int k = 0; k < 200; ++k) many.push_back({k * 50, ServiceEvent::Fuel, u32(k)});
    const Bytes cut = gt4r::net::with_service(Bytes(900, 0x55), {1, many});
    const std::optional<gt4r::net::CarService> c = gt4r::net::service_of(cut);
    CHECK(cut.size() <= 1024 && c && !c->events.empty() && c->events.back() == many.back());
    // The hold counter between events: counted down, or standing.
    std::vector<ServiceEvent> h{{1000, ServiceEvent::Hold, 721}, {1500, ServiceEvent::Hold, 711 | 0x10000}, {3000, ServiceEvent::Hold, 711}};
    CHECK(gt4r::net::hold_at(h, 950) == 0 && gt4r::net::hold_at(h, 1000) == 721 && gt4r::net::hold_at(h, 1450) == 712);
    CHECK(gt4r::net::hold_at(h, 2900) == 711 && gt4r::net::hold_at(h, 3100) == 709 && gt4r::net::hold_at(h, 3000 + 711 * 50 + 500) == 0);
    // The owner's log: a hold counted down sends nothing new; standing and the change do.
    gt4r::net::ServiceLog log;
    for (int k = 0; k < 10; ++k) log.hold(k * 50, 0);
    CHECK(!log.take_changed());
    log.hold(500, 721), log.hold(550, 721), log.hold(600, 721), log.hold(650, 720), log.hold(700, 719);
    CHECK(log.take_changed() && log.events().size() == 3);
    CHECK(gt4r::net::hold_at({log.events().begin(), log.events().end()}, 700) == 719);
    // A refuelling: its last only, once a step passes without one.
    log.add({750, ServiceEvent::Fuel, 1}), log.end_step(750), log.add({800, ServiceEvent::Fuel, 2}), log.end_step(800);
    CHECK(!log.take_changed());
    log.end_step(850);
    CHECK(log.take_changed() && log.events().back() == (ServiceEvent{800, ServiceEvent::Fuel, 2}));
    std::printf("  a state kept to run again from: %zu bytes (%zu kept: %.1f MB a car)\n", sizeof(sim::Session::Memory), size_t(180),
                double(sizeof(sim::Session::Memory)) * 180 / 1048576);
}

// A pit stop of one car (B-spec, sent in at 20 s): another kind of tires (the softest the pit has for it, or the hardest), new
// tread and fuel. The other machine's prediction of it, without the service and with it, at several delays with a fifth lost:
// with it the stop runs as its owner's (held, in its box) and the prediction's car as its owner last sent it is on the owner's
// tires and fuel, bit for bit. GT4R_NET_TABLE=1: every delay with and without loss, both ways.
TEST(net_race_pit_stop_service) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    World w(*disc, "tsukuba");
    struct Case {
        Link link;
        bool hardest;
    };
    std::vector<Case> cases{{{0, 0, 0.2}, false}, {{100, 20, 0.2}, true}, {{250, 50, 0.2}, false}};
    if (std::getenv("GT4R_NET_TABLE")) {
        cases.clear();
        for (bool hardest : {false, true})
            for (double ms : {0.0, 50.0, 100.0, 250.0})
                for (double loss : {0.0, 0.2}) cases.push_back({{ms, ms * 0.2, loss}, hardest});
    }
    for (const Case& c : cases) {
        const Link& link = c.link;
        const Result without = pit_race(w, link, false, c.hardest), with = pit_race(w, link, true, c.hardest);
        std::printf("  %3.0f ms + %2.0f, loss %2.0f %%, kinds %d -> %d: in the stop rms / worst %6.2f / %6.1f cm -> %6.2f / %6.1f; 30 s after: all %6.2f / %6.1f -> %6.2f / %6.1f, apart %6.2f / %6.1f -> %6.2f / %6.1f;"
                    " front grip owner %.4f, seen %.4f -> %.4f; fuel %.3f, seen %.3f -> %.3f; tread %.3f, seen %.3f -> %.3f; %u runs again; %lld steps in contact; warnings %u %s\n",
                    link.delay_ms, link.jitter_ms, link.loss * 100, with.kinds_before, with.kinds_after, without.stop.rms() * 100, without.stop.worst * 100, with.stop.rms() * 100,
                    with.stop.worst * 100, without.after.rms() * 100, without.after.worst * 100, with.after.rms() * 100, with.after.worst * 100, without.after_apart.rms() * 100,
                    without.after_apart.worst * 100, with.after_apart.rms() * 100, with.after_apart.worst * 100, double(with.grip_owner), double(without.grip_seen), double(with.grip_seen),
                    double(with.fuel_owner), double(without.fuel_seen), double(with.fuel_seen), double(with.wear_owner), double(without.wear_seen), double(with.wear_seen), with.reruns,
                    (long long)with.contact, with.warnings, with.warning.c_str());
        CHECK(with.stop.n > 600 && with.after.n > 0); // (the stop was made and the car raced on)
        CHECK(with.kinds_after != with.kinds_before);  // (on the other tires)
        // The prediction's car at its newest state on the owner's tires and fuel, bit for bit; without the service on the old
        // ones. The new tread worn as the owner's (the states' units leave a little apart).
        CHECK(with.grip_seen == with.grip_owner && without.grip_seen != with.grip_owner);
        CHECK(with.fuel_seen == with.fuel_owner && without.fuel_seen != without.fuel_owner);
        CHECK(std::fabs(with.wear_seen - with.wear_owner) * 50 < std::fabs(without.wear_seen - with.wear_owner));
        // The stop as the owner's: held in its box.
        CHECK(with.stop.rms() < without.stop.rms());
        if (link.delay_ms == 0) CHECK(with.stop.worst < 1e-4 && with.after_apart.worst * 100 < 2.0);
        // After it the car's own step on the new tires against its frames held (a softer tire makes a held steering's error
        // a little larger: within 5 %).
        CHECK(with.after_apart.rms() <= without.after_apart.rms() * 1.05 + 1e-4);
        CHECK(with.warnings == 0);
    }
}
