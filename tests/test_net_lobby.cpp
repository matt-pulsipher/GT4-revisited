// The online lobby (docs/specs/net.md "Lobby") on this machine alone: the records, the program's conditions, the cars'
// descriptors, the relay path through a server of the test's own, finding a lobby on the loopback.

#include "test_framework.h"

#include "gt4r/net/car_choice.h"
#include "gt4r/net/discovery.h"
#include "gt4r/net/identity.h"
#include "gt4r/net/lobby.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/race_tires.h"

#include "net/relay_server.h"

#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <thread>

using namespace gt4r;
using gt3r::net::Session;

namespace {

void nap() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }

// One machine.
struct Machine {
    Session session;
    net::Lobby lobby{session};
    explicit Machine(const char* disc = "gt4") { session.set_disc({disc, "another game"}); }
};

bool pump(std::vector<Machine*> all, const std::function<bool()>& done, int ms = 8000) {
    for (int t = 0; t < ms; t += 2) {
        for (Machine* m : all) m->lobby.poll();
        if (done()) return true;
        nap();
    }
    return false;
}

struct Pair {
    Machine a, b;
    bool ok = false;
    Pair() {
        Session::set_advertise_lan(false);
        std::string error;
        ok = a.session.host(0, "One", "", 0, &error);
        a.lobby.begin("One");
        ok = ok && b.session.join("127.0.0.1:" + std::to_string(a.session.port()), "Two", "", 0, &error);
        b.lobby.begin("Two");
        ok = ok && pump({&a, &b}, [this] { return b.lobby.active() && a.lobby.count() == 2 && b.lobby.count() == 2 && a.lobby.heard(1) && b.lobby.heard(0); });
        if (!error.empty()) std::printf("  %s\n", error.c_str());
    }
};

net::CarChoice some_choice(const char* label = "nsx_r_02") {
    net::CarChoice c;
    c.label = label, c.paint = 2, c.tire = 4, c.automatic = false, c.asm_on = false, c.driver = 1, c.digest = 0x1122334455667788ull;
    return c;
}

} // namespace

TEST(net_lobby_records) {
    net::Member m;
    m.account = "Player1", m.course = "r_tsukuba", m.car = "nsx_r_02", m.paint = 3, m.sequence = 7, m.client_count = 4, m.time = 0x89ABCDEF, m.sync = 3, m.ended = 2;
    m.entry = {1, 0, 1, 1, 0, 4, 2, 1};
    m.round = 9, m.closed = true, m.rules = {5, true, -3, 2, 1, 1, 200}, m.limits = {250, false, true};
    m.choice = some_choice();
    const Bytes host = net::encode_member(m, true), guest = net::encode_member(m, false);
    const std::optional<net::Member> back = net::decode_member(host);
    CHECK(back && *back == m && *back->choice == *m.choice && back->rules.handicap == -3 && back->rules.ghost_ping_ms == 200 && back->limits == m.limits);
    CHECK(net::decode_member(guest) && net::decode_member(guest)->round == 1 && net::decode_member(guest)->choice);
    // A garage car: its parts, settings and condition; still within the session's block.
    m.choice->garage = true;
    for (size_t i = 0; i < m.choice->equipment.bytes.size(); ++i) m.choice->equipment.bytes[i] = u8(i * 7);
    m.choice->condition[5] = 9;
    const Bytes big = net::encode_member(m, true);
    CHECK(big.size() <= 1024 && net::decode_member(big) && *net::decode_member(big)->choice == *m.choice);
    // Cut short or of another version: nothing.
    for (size_t n = 0; n < big.size(); n += 13) CHECK(!net::decode_member(ByteSpan(big.data(), n)));
    Bytes other = host;
    other[0] = 99;
    CHECK(!net::decode_member(other));
}

TEST(net_lobby_conditions) {
    Pair p;
    CHECK(p.ok);
    if (!p.ok) return;
    net::Lobby &a = p.a.lobby, &b = p.b.lobby;
    CHECK(a.host() && !b.host() && a.my_index() == 0 && b.my_index() == 1 && a.name(1) == "Two" && b.name(0) == "One");
    // The matching page: the host asks for two players from a second from now.
    CHECK(!a.all_connected() && !b.all_connected());
    a.change([&](net::Member& m) { m.sequence = 2, m.client_count = 2, m.time = a.game_time() + 300; });
    b.change([](net::Member& m) { m.sequence = 2; });
    CHECK(!a.all_connected());
    CHECK(pump({&p.a, &p.b}, [&] { return a.all_connected() && b.all_connected(); }));
    CHECK(std::abs(s32(a.game_time() - b.game_time())) < 60);
    CHECK(a.style_count("play") == 2 && a.style_count("live") == 0);
    // The barrier: the first to enter waits for the other, and an old sequence is forgotten with it.
    b.change([](net::Member& m) { m.sequence = 8; });
    a.enter_sync();
    CHECK(a.mine().sequence == 2);
    pump({&p.a, &p.b}, [] { return false; }, 60);
    a.change([](net::Member& m) { m.sequence = 3; });
    b.enter_sync();
    CHECK(pump({&p.a, &p.b}, [&] { return a.synced() && b.synced() && a.member(1).sequence == 2; }));
    // A guest proposes a course, the host takes it.
    b.change([](net::Member& m) { m.course = "suzuka", m.sequence = 4; });
    CHECK(pump({&p.a, &p.b}, [&] { return a.member(1).sequence == 4 && a.member(1).course == "suzuka"; }));
    CHECK(a.changed() && !a.changed());
    // Their cars; the host wants its race once every player's menus are through and their cars can race.
    int checks = 0;
    a.check_car = b.check_car = [&](const net::CarChoice& c, const net::LobbyLimits& l) {
        ++checks;
        return l.max_power && c.label == "too_fast" ? std::string("over the power limit") : std::string();
    };
    a.build_car = b.build_car = [](const net::CarChoice& c, const std::string&) {
        sim::Equipment e;
        e.set_b(0x18, c.paint);
        return std::optional<sim::Equipment>(e);
    };
    a.change([](net::Member& m) { m.rules = {7, true, 0, 1, 0, 0, 150}, m.limits.max_power = 300, m.choice = some_choice("nsx_r_02"), m.sequence = 8; });
    a.want_race("r_suzuka");
    pump({&p.a, &p.b}, [] { return false; }, 60);
    CHECK(a.waiting_for() == "Two" && p.a.session.in_lobby());
    b.change([](net::Member& m) { m.choice = some_choice("too_fast"), m.sequence = 8; });
    CHECK(pump({&p.a, &p.b}, [&] { return a.waiting_for() == "Two: over the power limit"; }));
    CHECK(checks > 0 && checks < 8 && p.a.session.in_lobby()); // (a record's check is kept)
    CHECK(b.car_problem(1) == "over the power limit" && b.car_problem(0).empty());
    b.change([](net::Member& m) { m.choice = some_choice("gto_90"), m.choice->paint = 5; });
    std::optional<net::OnlineRace> ra, rb;
    CHECK(pump({&p.a, &p.b}, [&] {
        if (!ra) ra = a.take_race();
        if (!rb) rb = b.take_race();
        return ra && rb;
    }));
    if (!ra || !rb) return;
    CHECK(ra->course == "r_suzuka" && rb->course == "r_suzuka" && ra->rules.laps == 7 && rb->rules.laps == 7 && rb->rules.tire_wear && rb->rules.boost == 1);
    CHECK(ra->cars.size() == 2 && rb->cars.size() == 2 && ra->ours == 0 && rb->ours == 1);
    CHECK(rb->cars[0].label == "nsx_r_02" && rb->cars[1].label == "gto_90" && rb->cars[1].paint == 5 && rb->cars[1].equipment && rb->cars[1].equipment->b(0x18) == 5);
    CHECK(rb->cars[0].player == "One" && !rb->cars[0].automatic && !rb->cars[0].asm_on && rb->cars[0].driver == 1);
    // After the race: the host takes everybody back, a new round; the guest is back first and waits at the barrier.
    b.end_race();
    b.enter_sync();
    CHECK(b.mine().sync == 2 && !b.mine().choice);
    pump({&p.b}, [] { return false; }, 40);
    CHECK(!b.synced());
    a.end_race();
    CHECK(a.round() == 2);
    a.enter_sync();
    CHECK(pump({&p.a, &p.b}, [&] { return a.synced() && b.synced() && b.round() == 2 && a.member(1).sync == 2; }));
    // The guest leaves: the host alone knows it lost everyone.
    a.change([](net::Member& m) { m.sequence = 3; });
    CHECK(!a.lost());
    b.end();
    CHECK(pump({&p.a}, [&] { return a.lost() && a.count() == 1; }));
}

TEST(net_lobby_host_leaves) {
    Pair p;
    CHECK(p.ok);
    if (!p.ok) return;
    p.a.lobby.end();
    CHECK(pump({&p.b}, [&] { return p.b.lobby.closed() && p.b.lobby.lost(); }));
    CHECK(!p.b.lobby.take_error().empty() && p.b.lobby.take_error().empty());
}

TEST(net_lobby_identity) {
    DiscInfo gt4, changed, pro;
    gt4.verified = true, gt4.digest = "0011";
    changed.modified = true, changed.digest = "0022";
    pro.game = Game::Prologue;
    const auto a = net::online_disc(gt4), b = net::online_disc(changed);
    CHECK(a && a->id == "gt4" && b && b->id == "gt4:0022" && !net::online_disc(pro));
    for (const auto& d : {a, b}) CHECK(d && d->name.find("Gran") == std::string::npos && d->name.find("4") == std::string::npos);
    // A lobby of the shared engine's other game and ours never meet, either way round, and neither message names ours.
    for (const bool we_host : {true, false}) {
        Session::set_advertise_lan(false);
        Machine ours, theirs("gt3");
        ours.session.set_disc(*a);
        theirs.session.set_disc({"gt3", "a Gran Turismo 3 release"});
        Machine &h = we_host ? ours : theirs, &g = we_host ? theirs : ours;
        std::string error;
        CHECK(h.session.host(0, "Host", "", 0, &error));
        h.lobby.begin("Host");
        CHECK(g.session.join("127.0.0.1:" + std::to_string(h.session.port()), "Guest", "", 0, &error));
        g.lobby.begin("Guest");
        std::string why;
        CHECK(pump({&h, &g}, [&] { return why.empty() ? (why = g.lobby.take_error(), !why.empty()) : true; }));
        CHECK(why.find("another game") != std::string::npos && why.find("Gran Turismo 4") == std::string::npos && why.find("discs must match") != std::string::npos);
    }
    // A copy the relay calls too old is told so, with no other game's name and no download.
    const std::string old = net::refusal_text("update required to play online: version 9.9.9", true);
    CHECK(old.find("must be updated") != std::string::npos && old.find("GT3") == std::string::npos && old.find("http") == std::string::npos);
    CHECK(net::refusal_text("update required: the host has a newer version of the game (online protocol 9, yours 6). Please update GT3 Revisited to play online", false)
              .find("GT3") == std::string::npos);
    CHECK(net::refusal_text("the session is full", false) == "the session is full");
}

// A lobby through a rendezvous server of the test's own on the loopback: registered, found by its code, joined; and with
// the direct path closed, joined and loaded through the relay.
TEST(net_lobby_relay) {
    for (const bool direct : {true, false}) {
        Session::set_advertise_lan(false);
        gt3r::net::RelayServer server;
        std::string error;
        CHECK(server.open(0, &error));
        const std::string at = "127.0.0.1:" + std::to_string(server.port());
        Machine h, g;
        if (!direct) h.session.set_block_direct(true), g.session.set_block_direct(true), h.session.set_punch_give_up_ms(300), g.session.set_punch_give_up_ms(300);
        CHECK(h.session.host_via(at, "One", "", 0, &error));
        h.lobby.begin("One");
        auto step = [&](const std::function<bool()>& done, int ms = 12000) {
            for (int t = 0; t < ms; t += 2) {
                server.step(0);
                h.lobby.poll(), g.lobby.poll();
                if (done()) return true;
                nap();
            }
            return false;
        };
        CHECK(step([&] { return gt3r::net::valid_code(h.session.code()); }));
        CHECK(g.session.join_code(at, gt3r::net::format_code(h.session.code()), "Two", "", 0, &error));
        g.lobby.begin("Two");
        CHECK(step([&] { return g.lobby.active() && h.lobby.count() == 2 && h.lobby.heard(1) && g.lobby.heard(0); }));
        CHECK(g.session.link(0).known && g.session.link(0).relay == !direct);
        // A race through it (the relay's link slots are asked for first).
        for (Machine* m : {&h, &g}) m->lobby.enter_sync();
        CHECK(step([&] { return h.lobby.synced() && g.lobby.synced(); }));
        for (Machine* m : {&h, &g}) m->lobby.change([](net::Member& r) { r.choice = some_choice(), r.sequence = 8; });
        h.lobby.want_race("tsukuba");
        std::optional<net::OnlineRace> ra, rb;
        CHECK(step([&] {
            if (!ra) ra = h.lobby.take_race();
            if (!rb) rb = g.lobby.take_race();
            return ra && rb;
        }));
        CHECK(ra && rb && rb->cars.size() == 2 && rb->course == "tsukuba");
        // A wrong code is refused.
        Machine x;
        CHECK(x.session.join_code(at, "ABCD-EFGH", "Three", "", 0, &error));
        x.lobby.begin("Three");
        std::string why;
        for (int t = 0; t < 4000 && why.empty(); t += 2) server.step(0), x.lobby.poll(), why = x.lobby.take_error(), nap();
        CHECK(!why.empty());
        g.lobby.end(), h.lobby.end();
    }
}

// A server that asks for a newer copy: the message says so (and the session's kept release page is not shown).
TEST(net_lobby_update_required) {
    Session::set_advertise_lan(false);
    gt3r::net::RelayServer server;
    std::string error;
    CHECK(server.open(0, &error));
    server.set_versions({*gt3r::net::parse_game_version("99.0.0"), *gt3r::net::parse_game_version("99.0.0"), "https://example.invalid/releases"});
    Machine h;
    CHECK(h.session.host_via("127.0.0.1:" + std::to_string(server.port()), "One", "", 0, &error));
    h.lobby.begin("One");
    std::string why;
    for (int t = 0; t < 4000 && why.empty(); t += 2) server.step(0), h.lobby.poll(), why = h.lobby.take_error(), nap();
    CHECK(why.find("must be updated") != std::string::npos && why.find("example") == std::string::npos);
}

TEST(net_lobby_discovery) {
    net::Discovery host, guest;
    if (!host.host(true)) SKIP("no free discovery port");
    CHECK(guest.search(true));
    net::Discovery::Answer answer;
    answer.port = 5555, answer.name = "One", answer.disc = "gt4", answer.players = 2, answer.open = false;
    for (int t = 0; t < 2000 && guest.found().empty(); t += 2) host.poll(answer), guest.poll(), nap();
    bool seen = false; // (another lobby of this machine may answer too)
    for (const net::FoundLobby& f : guest.found()) seen = seen || (f.address == "127.0.0.1:5555" && f.name == "One" && f.disc == "gt4" && f.players == 2 && f.limit == 6 && !f.open);
    CHECK(seen);
}

namespace {

struct DiscCars {
    Disc* disc = nullptr;
    std::unique_ptr<db::SpecDb> db;
    sim::Program program;
    std::unique_ptr<net::CarBuilder> builder;
    sim::FileReader read;
    explicit DiscCars() {
        for (const auto& d : gt4r::test::discs())
            if (d->info().game == Game::GT4) disc = d.get();
        if (!disc) return;
        db = std::make_unique<db::SpecDb>(sim::ai::open_specdb(*disc));
        program = sim::Program::load(*disc);
        gt3r::Volume* vol = &disc->volume();
        read = [vol](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        };
        builder = std::make_unique<net::CarBuilder>(*db, program, read);
    }
};

} // namespace

// Every car of the disc as an arcade car and as a garage car: through the wire it is the same car to another machine (a
// second builder on its own database), and the digest tells any change of a part, a setting or the condition.
TEST(disc_net_cars) {
    DiscCars a;
    if (!a.disc || !a.program.known()) SKIP("needs the Gran Turismo 4 disc");
    DiscCars b;
    int cars = 0, garage = 0, dirt = 0, snow = 0, tires = 0, fitted = 0;
    for (const db::CarEntry& e : a.db->cars()) {
        net::CarChoice c;
        c.label = e.label, c.paint = 1;
        sim::Equipment stock;
        if (!sim::stock_car(*a.db, a.program, e.label, stock)) continue;
        c.digest = a.builder->digest(c);
        if (!c.digest) continue;
        ++cars;
        gt3r::net::Writer w;
        net::write_choice(w, c);
        gt3r::net::Reader r(w.data);
        net::CarChoice got;
        CHECK(net::read_choice(r, got) && got == c);
        const std::string why = b.builder->problem(got);
        CHECK(why.empty());
        // The tires a race puts on: dirt's and snow's where the car has them.
        for (const int kind : {2, 3}) {
            const std::optional<sim::Equipment> eq = b.builder->raced(got, kind);
            CHECK(eq.has_value());
            if (eq && sim::tire_kind(*b.db, *eq, 0) == (kind == 2 ? 11 : 12)) ++(kind == 2 ? dirt : snow);
        }
        got.tire = sim::tire_kind(*b.db, stock, 0) <= 5 ? 3 : 8;
        if (const auto eq = b.builder->raced(got, 0); eq && sim::tire_kind(*b.db, *eq, 0) == got.tire && sim::tire_kind(*b.db, *eq, 1) == got.tire) ++tires;
        if (cars % 7) continue;
        // The garage's: its record.
        c.garage = true, c.equipment = stock, c.condition[0x14] = 0, c.condition[4] = 0x40;
        c.digest = a.builder->digest(c);
        CHECK(b.builder->problem(c).empty());
        net::CarChoice d = c;
        d.condition[6] ^= 1;
        CHECK(b.builder->problem(d) == "the car is different on this disc");
        d = c, d.equipment.set_b(sim::Equipment::Ballast, 20); // (another car than the digest's)
        CHECK(b.builder->problem(d) == "the car is different on this disc");
        // Settings no page gives, with a digest of their own.
        auto refused = [&](const std::function<void(sim::Equipment&)>& change) {
            net::CarChoice x = c;
            change(x.equipment);
            x.digest = a.builder->digest(x);
            return b.builder->problem(x).find("out of its range") != std::string::npos;
        };
        CHECK(refused([](sim::Equipment& e) { e.set_h(sim::Equipment::OutputFactor, 1500); }));
        CHECK(refused([](sim::Equipment& e) { e.set_b(sim::Equipment::PowerPct, 120); }));
        CHECK(refused([](sim::Equipment& e) { e.set_b(sim::Equipment::WeightPct, 80); }));
        CHECK(refused([](sim::Equipment& e) { e.set_b(sim::Equipment::GripAdd, 20); }));
        CHECK(refused([](sim::Equipment& e) { e.set_b(sim::Equipment::Ballast, 230); }));
        CHECK(refused([](sim::Equipment& e) { e.set_b(sim::Equipment::SpringFront, 255); }));
        CHECK(refused([](sim::Equipment& e) { e.set_h(sim::Equipment::GearRatios + 2, 60000); }));
        // Every part that fits it, one at a time with its default settings: a car the game can make.
        for (const sim::FittingPart& f : sim::fitting_parts(b.read, *b.db, stock.part(sim::Equipment::Car))) {
            const u32 table = db::part_table(f.part);
            std::optional<db::CarParts> parts = db::car_parts(*b.db, e.label);
            if (!parts || table < 1 || table > 26) continue;
            parts->parts[table - 1] = f.part;
            sim::Equipment tuned;
            if (!sim::parts_equipment(*b.db, *parts, tuned)) continue;
            sim::computed_gears(*b.db, b.program, tuned);
            const std::string p = net::settings_problem(*b.db, b.program, tuned);
            if (!p.empty()) std::printf("  %s with %s: %s\n", e.label.c_str(), db::part_table_name(table), p.c_str());
            CHECK(p.empty());
            ++fitted;
        }
        // Another car's engine.
        d = c;
        sim::Equipment other;
        if (sim::stock_car(*a.db, a.program, e.label == "nsx_r_02" ? "gto_90" : "nsx_r_02", other) && other.part(sim::Equipment::Engine) != stock.part(sim::Equipment::Engine)) {
            d.equipment.set_part(sim::Equipment::Engine, other.part(sim::Equipment::Engine));
            d.digest = a.builder->digest(d);
            CHECK(b.builder->problem(d).find("does not fit") != std::string::npos);
        }
        ++garage;
    }
    std::printf("  %d cars, %d as garage cars with %d parts fitted, dirt tires on %d, snow on %d, chosen tires on %d\n", cars, garage, fitted, dirt, snow, tires);
    CHECK(fitted > 2000 && cars > 600 && garage > 80 && tires > 400);
}
