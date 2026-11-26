// A whole online race over loopback sockets in one process: lobby, load, the start at one moment, the cars' states, finishes,
// a player leaving, the host going back (docs/specs/net.md "Race"). No machine but this one is spoken to.

#include "net_machines.h"

#include "gt4r/net/net_race.h"
#include "gt4r/race/race_flow.h"
#include "net/relay_server.h"
#include "net/session.h"
#include "net/socket.h"
#include "net_ports.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace gt4r;
using namespace gt4r::test::net;

namespace {

struct FakeClock {
    static inline std::atomic<double> ms{0};
    static double read() { return ms.load(); }
    FakeClock() { ms = 1e6, gt3r::net::set_clock(&read); }
    ~FakeClock() { gt3r::net::set_clock(nullptr); }
};

// A player's machine: its session of the network, the race between the machines, the race's own flow.
struct Player {
    gt3r::net::Session net;
    std::unique_ptr<gt4r::net::NetRace> race;
    std::unique_ptr<sim::Session> sim;
    race::RaceFlow flow;
    std::function<race::CarInput(sim::Session&, s64)> drive;
    std::vector<std::pair<s64, course::Vec3>> own; // our car by the race's clock
    std::vector<std::pair<s64, course::Vec3>> other;
    double started_ms = 0;
    race::PadRecord pad;
    std::optional<gt3r::net::Event> load;

    void lobby() {
        net.poll();
        while (auto e = net.next_event())
            if (e->kind == gt3r::net::Event::Kind::Load) load = *e;
    }
    s64 clock() const { return gt4r::net::NetRace::race_clock(flow.hold_left(), flow.clock()); }
    void frame(double dt) {
        race->frame(dt);
        for (int n = race->steps(dt); n > 0; --n) {
            if (started_ms == 0) started_ms = FakeClock::read();
            race->before_step(clock());
            std::vector<race::CarInput> in(sim->cars().size());
            in[0] = drive ? drive(*sim, clock()) : race::CarInput{};
            pad = pad_of(in[0]);
            in[0] = {};
            in[0].pad = &pad;
            if (flow.holding()) sim->hold_once(in);
            else sim->step_once(in);
            const race::RaceFlow::Events ev = flow.step(sim->cars());
            race->after_step(clock(), in[0], flow.car(0).laps_done);
            for (const size_t i : ev.finished) {
                if (i == 0) race->finished(flow.car(0).finish_units, -1);
                else race->observed_finish(i, flow.car(i).seen_units);
            }
            for (const auto& f : race->take_finishes()) flow.claim_finish(f.car, f.units, -1);
            auto at = [&](size_t i) {
                const auto& c = sim->record(i);
                return course::Vec3{c.pos.x.host(), c.pos.z.host(), -c.pos.y.host()};
            };
            own.push_back({clock(), at(0)});
            if (sim->cars().size() > 1 && race->field()->has_state(1)) other.push_back({clock(), at(1)});
        }
    }
};

struct Party {
    FakeClock fake; // first: outlives the sessions
    gt3r::net::RelayServer relay;
    Player p[2];
    World& w;
    bool relayed;
    explicit Party(World& world, bool through_relay) : w(world), relayed(through_relay) {}

    void pump_relay() {
        if (!relayed) return;
        relay.step(0);
    }
    bool wait(const std::function<bool()>& done) {
        for (int i = 0; i < 4000; ++i) {
            for (Player& q : p) q.lobby();
            pump_relay();
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            FakeClock::ms.store(FakeClock::ms.load() + 5);
        }
        return false;
    }
    // Both in the race, the race about to start.
    bool start(int laps, int hold_seconds, double skew_ms, float loss) {
        gt3r::net::Session::set_advertise_lan(false);
        for (Player& q : p) q.net.set_disc({"gt4", "a test"}), q.net.set_loss(loss);
        p[1].net.set_clock_skew(skew_ms);
        if (relayed) {
            if (!test::on_free_port([&](u16 port) { return relay.open(port); })) return false;
            const std::string server = "127.0.0.1:" + std::to_string(relay.port());
            p[1].net.set_block_direct(true), p[1].net.set_punch_give_up_ms(500);
            if (!p[0].net.host_via(server, "One", "a", 0)) return false;
            if (!wait([&] { return !p[0].net.code().empty(); })) return false;
            p[1].net.join_code(server, p[0].net.code(), "Two", "b", 0);
        } else {
            if (!test::on_free_port([&](u16 port) { return p[0].net.host(port, "One", "a", 0); })) return false;
            p[1].net.join("127.0.0.1:" + std::to_string(p[0].net.port()), "Two", "b", 0);
        }
        if (!wait([&] { return p[0].net.players().size() == 2 && p[1].net.players().size() == 2; })) return false;
        gt3r::net::Settings st;
        st.course = "tsukuba", st.laps = laps;
        p[0].net.set_settings(st);
        p[1].net.set_ready(true);
        if (!wait([&] { return p[0].net.all_ready(); })) return false;
        p[0].net.load();
        if (!wait([&] { return p[0].load && p[1].load; })) return false;
        const std::vector<std::string> labels{"nsx_r_02", "r34_gtr_v2_nur_02"};
        std::vector<sim::Entry> all = w.entries(labels);
        for (int m = 0; m < 2; ++m) {
            gt4r::net::OnlineRace o;
            o.course = "tsukuba", o.rules.laps = laps;
            for (size_t k = 0; k < 2; ++k) {
                gt4r::net::RaceCar c;
                c.id = p[m].load->grid[k], c.player = k ? "Two" : "One", c.label = labels[k];
                o.cars.push_back(c);
                if (int(c.id) == p[m].net.my_id()) o.ours = int(k);
            }
            p[m].race = std::make_unique<gt4r::net::NetRace>(p[m].net, o);
            std::vector<sim::Entry> mine;
            for (const size_t s : p[m].race->seats()) mine.push_back(all[s]);
            p[m].sim = w.session(mine);
            race::FlowSetup fs;
            fs.start = race::StartType::Given, fs.start_seconds = hold_seconds, fs.laps = laps, fs.lap_length = w.road->length;
            p[m].flow.begin(fs, p[m].sim->cars());
            p[m].race->attach(*p[m].sim);
        }
        return true;
    }
    void frames(int n, const std::function<bool()>& until = {}) {
        for (int k = 0; k < n; ++k) {
            for (Player& q : p)
                if (q.race && !q.race->over()) q.frame(1.0 / 60);
            pump_relay();
            for (Player& q : p) q.net.poll();
            if (until && until()) return;
            std::this_thread::sleep_for(std::chrono::microseconds(2500));
            FakeClock::ms.store(FakeClock::ms.load() + 1000.0 / 60);
        }
    }
    // The other car's place on machine m against its owner's at the same race clock.
    Errors errors(int m) const {
        Errors e;
        size_t k = 0;
        for (const auto& [clock, at] : p[m].other) {
            while (k < p[1 - m].own.size() && p[1 - m].own[k].first < clock) ++k;
            if (k >= p[1 - m].own.size() || p[1 - m].own[k].first != clock) continue;
            const course::Vec3 b = p[1 - m].own[k].second;
            e.add(std::sqrt(double(at.x - b.x) * (at.x - b.x) + double(at.y - b.y) * (at.y - b.y) + double(at.z - b.z) * (at.z - b.z)));
        }
        return e;
    }
};

std::function<race::CarInput(sim::Session&, s64)> along_the_road(float weave) {
    auto line = std::make_shared<std::unique_ptr<sim::LineDriver>>();
    return [=](sim::Session& s, s64 clock) {
        if (!*line) *line = std::make_unique<sim::LineDriver>(s, 0);
        race::CarInput in = (*line)->drive(1.0f, true);
        in.steer = std::clamp(in.steer + weave * float(std::sin(double(clock) / 3000 * 4)), -1.0f, 1.0f);
        return in;
    };
}

} // namespace

// The start at one moment on both machines (their clocks 5 s apart), the cars where their owners have them; through a relay
// server on this machine and directly, with datagrams lost.
TEST(net_session_race) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    World w(*disc, "tsukuba");
    struct Case {
        bool relay;
        float loss;
    };
    for (const Case c : {Case{false, 0}, Case{false, 0.1f}, Case{true, 0}}) {
        Party party(w, c.relay);
        if (!party.start(1, 2, 5000, c.loss)) {
            CHECK(false && "the lobby did not start the race");
            continue;
        }
        for (Player& q : party.p) q.drive = along_the_road(0.1f);
        party.frames(12 * 60);
        const double apart = std::fabs(party.p[0].started_ms - party.p[1].started_ms);
        const Errors e0 = party.errors(0), e1 = party.errors(1);
        const auto s0 = party.p[0].race->standings(), s1 = party.p[1].race->standings();
        std::printf("  %s, loss %.0f %%: starts %.1f ms apart; the other car rms %.2f / %.2f cm, worst %.1f / %.1f cm over %llu steps; link %s\n", c.relay ? "relayed" : "direct", c.loss * 100, apart,
                    e0.rms() * 100, e1.rms() * 100, e0.worst * 100, e1.worst * 100, (unsigned long long)e0.n, s1[1].direct ? "direct" : "not direct");
        CHECK(apart < 20 && party.p[0].started_ms > 0);
        CHECK(e0.n > 400 && e1.n > 400 && e0.worst < 0.8 && e1.worst < 0.8);
        CHECK(s0.size() == 2 && s0[1].has_state && s1[1].has_state && !s0[1].ghost && s1[1].player == "One");
        // Two leaves: One's race goes on alone.
        party.p[1].race->retire();
        party.frames(30);
        CHECK(party.p[0].race->standings()[1].retired && party.p[0].race->car_gone(1));
        CHECK(!party.p[0].race->take_messages().empty());
        // The host goes back to the lobby: the race is over for both.
        party.p[0].net.lobby();
        party.frames(30, [&] { return party.p[1].race->over(); });
        CHECK(party.p[1].race->over());
    }
}

// The race's finish by the owners' clocks, and a machine whose connection goes: it drives on alone.
TEST(net_session_finish_and_loss) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    World w(*disc, "tsukuba");
    Party party(w, false);
    if (!party.start(1, 1, 0, 0)) {
        CHECK(false && "the lobby did not start the race");
        return;
    }
    for (Player& q : party.p) q.drive = along_the_road(0);
    party.frames(5 * 60);
    // (the lap is long for a test: the cars' finishes as the race's flow would report them)
    party.p[1].race->finished(123450, 60000);
    party.frames(20);
    const auto s = party.p[0].race->standings();
    CHECK(s[1].finished && s[1].time_units == 123450 && s[1].best_units == 60000 && s[1].ghost);
    CHECK(!party.p[0].race->all_done());
    party.p[0].flow.claim_finish(1, 123450, -1);
    // The host is gone.
    party.p[0].net.close();
    party.frames(20 * 60 / 4, [&] { return party.p[1].race->lost(); });
    CHECK(party.p[1].race->lost() && !party.p[1].race->ended_why().empty());
    const size_t ran = party.p[1].own.size();
    party.frames(30);
    CHECK(party.p[1].own.size() >= ran + 25);
    CHECK(party.p[1].race->all_done());
}

// The picture at 120 frames a second while the machines' clock estimates wander: it never steps back, and moves evenly.
TEST(net_session_120hz) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    World w(*disc, "tsukuba");
    Party party(w, false);
    if (!party.start(1, 1, 5000, 0)) {
        CHECK(false && "the lobby did not start the race");
        return;
    }
    for (Player& q : party.p) q.drive = along_the_road(0);
    double drawn[2] = {-1, -1}, least = 1e9, most = -1e9;
    size_t ran[2] = {0, 0};
    int back = 0;
    for (int k = 0; k < 120 * 6; ++k) {
        party.p[1].net.set_clock_skew(5000 + 4 * std::sin(double(k) * 0.02)); // (what the host's clock looks like from there)
        for (int m = 0; m < 2; ++m) {
            party.p[m].frame(1.0 / 120);
            ran[m] = party.p[m].own.size();
            if (ran[m] == 0) continue;
            const double d = double(ran[m]) - 1 + double(party.p[m].race->alpha());
            if (drawn[m] >= 0) {
                const double moved = d - drawn[m];
                if (moved < -1e-6) ++back;
                if (k > 240) least = std::min(least, moved), most = std::max(most, moved);
            }
            drawn[m] = d;
        }
        for (Player& q : party.p) q.net.poll();
        std::this_thread::sleep_for(std::chrono::microseconds(1500));
        FakeClock::ms.store(FakeClock::ms.load() + 1000.0 / 120);
    }
    std::printf("  a frame moves %.2f to %.2f steps (0.50 even), back %d times\n", least, most, back);
    CHECK(back == 0 && least > 0.3 && most < 0.7);
}
