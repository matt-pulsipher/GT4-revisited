// The game's own LAN Battle pages on a lobby: two games in one process, each its menus and its lobby, over the loopback
// (docs/specs/net.md "The game's own pages").

#include "menu_run_flow.h"

#include "gt4r/menu/game.h"
#include "gt4r/menu/game_network.h"
#include "gt4r/menu/game_progress.h"
#include "gt4r/net/lobby.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/pit_tires.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/car_spec.h"
#include "net_ports.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <random>
#include <thread>

namespace {

// One player: the menus and the lobby.
struct Player {
    Flow f;
    gt3r::net::Session session;
    net::Lobby lobby{session};
    explicit Player(Disc& d, const std::string& data_dir = {}) : f(d, "US", data_dir) {
        session.set_disc({"gt4", "another game"});
        f.rt->network = &lobby;
    }
    std::string page() const { return f.page(); }
};

struct Table {
    std::vector<Player*> players;
    void frame() {
        for (Player* p : players) p->lobby.poll(), p->f.rt->update();
        if (++n % 4 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // (so many frames and so long: a loaded machine's sockets take their time)
    bool until(const std::function<bool()>& done, int frames = 4000) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < frames || std::chrono::steady_clock::now() - start < std::chrono::seconds(20); ++i) {
            if (done()) return true;
            frame();
        }
        std::string where;
        for (Player* p : players) where += " " + p->page();
        std::printf("  stopped at%s\n", where.c_str());
        return false;
    }
    void run(int frames) {
        for (int i = 0; i < frames; ++i) frame();
    }
    void press(Player& p, std::initializer_list<menu::Key> keys, int after = 50) {
        for (menu::Key k : keys) p.f.rt->key(k), run(after);
    }
    int n = 0;
};

// Arcade's LAN Battle chosen, and the connection as our own page leaves it (the `network` project's part).
bool to_matching(Player& p, bool host, u16 port, int players, bool saved = false) {
    Flow& f = p.f;
    if (saved) { // (a game on the card: it is loaded as the game starts)
        f.rt->boot();
        f.rt->start_project(f.rt->first_project());
        f.rt->settle();
        if (f.page() == "MovieRoot") f.press({menu::Key::Start});
        // (it goes on where it was saved, in Gran Turismo mode: here to the title)
        menu::GameState& g = f.rt->game();
        g.next_menu_project = "arcade", g.major_menu_page.clear(), g.menu_stack.clear();
        if (!f.rt->finished() || !f.rt->advance()) return false;
        f.rt->settle();
        if (f.page() != "TopRoot") return false;
    } else if (!f.boot_to_title()) {
        return false;
    }
    f.press({menu::Key::Accept});
    if (f.page() != "GameRoot" || !f.focus_on("LANBattle")) return false;
    f.press({menu::Key::Accept});
    if (!f.rt->finished() || f.rt->wanted_project() != "network") return false;
    std::string error;
    menu::GameState& g = f.rt->game();
    g.options["LAN_host"] = s32(host), g.options["LAN_style"] = std::string("play"), g.options["LAN_machine_name"] = std::string(host ? "One" : "Two");
    g.options["LAN_player_num"] = s32(players), g.options["LAN_monitor_num"] = s32(0);
    gt3r::net::Session::set_advertise_lan(false);
    if (host ? !test::on_free_port([&](u16 at) { return p.session.host(at, "One", "", 0, &error); }) : !p.session.join("127.0.0.1:" + std::to_string(port), "Two", "", 0, &error)) return false;
    p.lobby.begin(host ? "One" : "Two");
    return true;
}
bool on_to_arcade(Player& p) {
    menu::GameState& g = p.f.rt->game();
    g.next_menu_project = g.major_menu_project;
    return p.f.rt->advance();
}

struct Builder {
    db::SpecDb db;
    sim::Program program;
    net::CarBuilder builder;
    explicit Builder(Disc& d)
        : db(sim::ai::open_specdb(d)), program(sim::Program::load(d)), builder(db, program, [&d](const std::string& path) -> std::optional<Bytes> {
              const gt3r::Volume::Entry* e = d.volume().find(path);
              return e ? std::optional<Bytes>(d.volume().read(*e)) : std::nullopt;
          }) {}
    void give(net::Lobby& l) {
        l.digest_car = [this](const net::CarChoice& c) { return builder.digest(c); };
        l.check_car = [this](const net::CarChoice& c, const net::LobbyLimits& limits) { return net::limits_problem(builder, c, limits); };
        l.build_car = [this](const net::CarChoice& c, const std::string&) { return builder.raced(c, 0); };
    }
};

} // namespace

TEST(disc_net_lan_pages) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Builder cars(*disc);
    if (!cars.program.known()) SKIP("the disc's program is not known");
    Player one(*disc), two(*disc);
    cars.give(one.lobby), cars.give(two.lobby);
    CHECK(to_matching(one, true, 0, 2));
    Table t{{&one}};
    CHECK(to_matching(two, false, one.session.port(), 2));
    t.players.push_back(&two);
    CHECK(t.until([&] { return two.lobby.active() && one.lobby.active(); }));
    // The host's rules: the 2-Player Battle's options.
    menu::GameState& g = one.f.rt->game();
    g.options["split_race_laps"] = 3, g.options["split_tire_damage"] = 1, g.options["split_boost_level"] = 2, g.options["split_handicap"] = 1, g.options["penalty_type"] = std::string("SPEED_LIMITATION");
    one.lobby.change([](net::Member& m) { m.limits.max_power = 400; });
    CHECK(on_to_arcade(one) && on_to_arcade(two));
    // The matching page: the host waits for the players asked for, then both take the course page at about the same time.
    CHECK(t.until([&] { return one.page() == "CourseRoot" && two.page() == "CourseRoot"; }));
    CHECK(one.f.clean() && two.f.clean());
    t.run(60);
    CHECK(one.lobby.member(0).sequence == 3 && one.lobby.member(1).sequence == 3 && two.lobby.member(0).sequence == 3);
    // A course's page has no way back.
    t.press(two, {menu::Key::Cancel});
    CHECK(two.page() == "CourseRoot");
    // The guest proposes a course: both go on to the car with it (and to its direction where it has two).
    t.press(two, {menu::Key::Right, menu::Key::Accept});
    if (two.page() == "CourseRoot") t.press(two, {menu::Key::Accept});
    CHECK(t.until([&] { return one.page() == "MakerRoot" && two.page() == "MakerRoot"; }));
    const std::string course = one.lobby.host_member().course;
    CHECK(!course.empty() && two.lobby.host_member().course == course);
    // Each a car: the host a maker's first, the guest another maker's second, too strong for the host's rules first.
    t.press(one, {menu::Key::Accept, menu::Key::Accept});
    CHECK(t.until([&] { return one.page() == "CarRoot"; }));
    t.press(one, {menu::Key::Cancel}); // back to the makers and in again
    CHECK(t.until([&] { return one.page() == "MakerRoot"; }));
    t.press(one, {menu::Key::Accept});
    CHECK(t.until([&] { return one.page() == "CarRoot"; }));
    t.run(60);
    t.press(one, {menu::Key::Accept, menu::Key::Accept});
    CHECK(t.until([&] { return one.page() == "SettingPopup"; }));
    t.press(one, {menu::Key::Accept});
    CHECK(t.until([&] { return one.page() == "PregridRoot"; }));
    CHECK(one.lobby.member(0).rules.laps == 3 && t.until([&] { return two.lobby.host_member().rules.laps == 3; }));
    CHECK(two.lobby.host_member().rules.tire_wear && two.lobby.host_member().rules.boost == 2 && two.lobby.host_member().rules.handicap == 1 && two.lobby.host_member().rules.penalty == 1);
    t.run(30);
    CHECK(one.lobby.member(0).sequence == 7 && !one.lobby.menus_ended());
    t.press(two, {menu::Key::Accept, menu::Key::Accept});
    CHECK(t.until([&] { return two.page() == "CarRoot"; }));
    t.run(60);
    t.press(two, {menu::Key::Accept, menu::Key::Accept});
    CHECK(t.until([&] { return two.page() == "SettingPopup"; }));
    one.lobby.change([](net::Member& m) { m.limits.max_power = 20; });
    CHECK(t.until([&] { return two.lobby.host_member().limits.max_power == 20; }));
    t.press(two, {menu::Key::Accept});
    CHECK(two.page() == "ConfirmDialog" && two.f.text_of("message").find("20") != std::string::npos);
    std::printf("  refused: %s\n", two.f.text_of("message").c_str());
    one.lobby.change([](net::Member& m) { m.limits.max_power = 0; });
    CHECK(t.until([&] { return !two.lobby.host_member().limits.max_power; }));
    t.press(two, {menu::Key::Accept});
    CHECK(t.until([&] { return two.page() == "SettingPopup"; }));
    t.press(two, {menu::Key::Accept});
    CHECK(t.until([&] { return two.page() == "PregridRoot" || two.f.rt->finished(); }));
    // The pregrid page: everyone is there, the count runs, both menus end asking for the LAN race; then the host loads it.
    CHECK(t.until([&] { return one.f.rt->finished() && two.f.rt->finished(); }));
    const std::optional<menu::RaceRequest> ra = one.f.rt->race_request(), rb = two.f.rt->race_request();
    CHECK(ra && rb && ra->sequence == "lan_race" && rb->sequence == "lan_race" && ra->course == course && rb->course == course);
    one.lobby.want_race(course);
    std::optional<net::OnlineRace> oa, ob;
    CHECK(t.until([&] {
        if (!oa) oa = one.lobby.take_race();
        if (!ob) ob = two.lobby.take_race();
        return oa && ob;
    }));
    if (oa && ob) {
        CHECK(oa->course == course && ob->course == course && oa->rules.laps == 3 && oa->cars.size() == 2 && ob->cars.size() == 2);
        for (size_t i = 0; i < 2 && oa->cars.size() == 2 && ob->cars.size() == 2; ++i) {
            CHECK(oa->cars[i].label == ob->cars[i].label && oa->cars[i].digest == ob->cars[i].digest && oa->cars[i].digest);
            CHECK(oa->cars[i].equipment && ob->cars[i].equipment && oa->cars[i].equipment->bytes == ob->cars[i].equipment->bytes);
            std::printf("  %s: %s, paint %u\n", oa->cars[i].player.c_str(), oa->cars[i].label.c_str(), oa->cars[i].paint);
        }
    }
    // After the race: the course page again, for both, the guest's first at the barrier.
    two.lobby.end_race();
    for (Player* p : {&two}) {
        menu::GameState& s = p->f.rt->game();
        s.next_sequence.clear();
        if (s.next_menu_project.empty()) s.next_menu_project = s.major_menu_project;
        CHECK(p->f.rt->advance());
    }
    t.run(120);
    CHECK(two.lobby.mine().sequence == 2 && !two.lobby.synced());
    one.lobby.end_race();
    {
        menu::GameState& s = one.f.rt->game();
        s.next_sequence.clear();
        if (s.next_menu_project.empty()) s.next_menu_project = s.major_menu_project;
        CHECK(one.f.rt->advance());
    }
    CHECK(t.until([&] { return one.page() == "CourseRoot" && two.page() == "CourseRoot" && one.lobby.member(0).sequence == 3 && one.lobby.member(1).sequence == 3; }));
    CHECK(one.lobby.round() == 2 && two.lobby.round() == 2);
    // The host takes its own course this time.
    t.press(one, {menu::Key::Accept});
    if (one.page() == "CourseRoot") t.press(one, {menu::Key::Accept});
    CHECK(t.until([&] { return one.page() == "MakerRoot" && two.page() == "MakerRoot"; }));
    // Left from outside: the arcade's first page, no lobby.
    menu::leave_lan_pages(*two.f.rt);
    CHECK(two.f.rt->finished() && two.f.rt->wanted_project() == "arcade" && two.f.rt->advance());
    two.f.rt->settle();
    CHECK(two.page() == "TopRoot" && !two.lobby.active());
    CHECK(t.until([&] { return one.lobby.count() == 1; }));
    CHECK(one.f.clean() && two.f.clean());
}

// A garage car in the LAN Battle: the card's garage by the makers' page, the car's parts, settings and condition to the
// other machine; a host that allows cars as sold alone has no garages there.
TEST(disc_net_lan_garage_car) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Builder cars(*disc);
    if (!cars.program.known()) SKIP("the disc's program is not known");
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("gt4r-net-test-garage-" + std::to_string(std::random_device{}()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string label;
    {
        Flow f(*disc, "US", dir.string());
        menu::ProgressPlan plan = menu::default_progress_plan(*f.rt);
        plan.licenses.clear();
        CHECK(menu::play_progress(*f.rt, plan).ok);
        for (const menu::GarageEntry& e : f.rt->game().garage)
            if (e.used && label.empty()) label = e.car;
    }
    Player one(*disc, dir.string()), two(*disc);
    cars.give(one.lobby), cars.give(two.lobby);
    CHECK(to_matching(one, true, 0, 2, true));
    Table t{{&one}};
    CHECK(to_matching(two, false, one.session.port(), 2));
    t.players.push_back(&two);
    CHECK(t.until([&] { return two.lobby.active() && one.lobby.active(); }));
    CHECK(on_to_arcade(one) && on_to_arcade(two));
    CHECK(t.until([&] { return one.page() == "CourseRoot" && two.page() == "CourseRoot" && one.lobby.member(1).sequence == 3; }));
    t.run(60);
    t.press(one, {menu::Key::Accept});
    if (one.page() == "CourseRoot") t.press(one, {menu::Key::Accept});
    CHECK(t.until([&] { return one.page() == "MakerRoot" && two.page() == "MakerRoot"; }));
    CHECK(one.f.focus_on("Slot1"));
    t.press(one, {menu::Key::Accept});
    for (int i = 0; i < 6 && one.page() != "GarageRoot"; ++i) {
        std::printf("  %s [%s] %s\n", one.page().c_str(), one.f.focus().c_str(), one.f.text_of("message").c_str());
        t.press(one, {menu::Key::Accept}, 200);
    }
    CHECK(one.page() == "GarageRoot" && one.f.focus_on("Line0"));
    t.press(one, {menu::Key::Accept}, 200);
    CHECK(t.until([&] { return one.page() == "SettingPopup"; }));
    t.press(one, {menu::Key::Accept});
    CHECK(t.until([&] { return one.page() == "PregridRoot"; }));
    const std::optional<net::CarChoice> mine = menu::choice_of_pages(*one.f.rt);
    CHECK(mine && mine->garage);
    if (mine) std::printf("  the garage's car: %s (the garage's first is %s), paint %d\n", mine->label.c_str(), label.c_str(), mine->paint);
    CHECK(two.f.focus_on("Maker"));
    t.press(two, {menu::Key::Accept, menu::Key::Accept});
    CHECK(t.until([&] { return two.page() == "CarRoot"; }));
    t.run(60);
    t.press(two, {menu::Key::Accept, menu::Key::Accept});
    CHECK(t.until([&] { return two.page() == "SettingPopup"; }));
    t.press(two, {menu::Key::Accept});
    CHECK(t.until([&] { return one.f.rt->finished() && two.f.rt->finished(); }));
    const std::string course = one.lobby.host_member().course;
    one.lobby.want_race(course);
    std::optional<net::OnlineRace> oa, ob;
    CHECK(t.until([&] {
        if (!oa) oa = one.lobby.take_race();
        if (!ob) ob = two.lobby.take_race();
        return oa && ob;
    }));
    if (oa && ob && mine && oa->cars.size() == 2 && ob->cars.size() == 2) {
        CHECK(ob->cars[0].label == mine->label && ob->cars[0].condition && *ob->cars[0].condition == mine->condition && ob->cars[0].digest);
        CHECK(ob->cars[0].equipment && oa->cars[0].equipment && ob->cars[0].equipment->bytes == oa->cars[0].equipment->bytes && !ob->cars[1].condition);
    }
    // The limits: a garage car is not a car as sold.
    if (mine) {
        net::CarChoice c = *mine;
        c.digest = cars.builder.digest(c);
        net::LobbyLimits limits;
        CHECK(net::limits_problem(cars.builder, c, limits).empty());
        limits.garage_cars = false;
        CHECK(!net::limits_problem(cars.builder, c, limits).empty());
    }
    CHECK(one.f.clean() && two.f.clean());
    std::filesystem::remove_all(dir);
}

// The app's car picker over the makers' page (any car, the host's rule; app/car_picker.h through menu::pick_lan_car): the car
// page opens with the car picked alone, its colors and the driving options as before, and the lobby gets that car.
TEST(disc_net_lan_app_car_picker) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Builder cars(*disc);
    if (!cars.program.known()) SKIP("the disc's program is not known");
    Player one(*disc), two(*disc);
    cars.give(one.lobby), cars.give(two.lobby);
    CHECK(to_matching(one, true, 0, 2));
    Table t{{&one}};
    CHECK(to_matching(two, false, one.session.port(), 2));
    t.players.push_back(&two);
    CHECK(t.until([&] { return two.lobby.active() && one.lobby.active(); }));
    CHECK(on_to_arcade(one) && on_to_arcade(two));
    CHECK(t.until([&] { return one.page() == "CourseRoot" && two.page() == "CourseRoot"; }));
    t.run(60);
    t.press(one, {menu::Key::Accept});
    if (one.page() == "CourseRoot") t.press(one, {menu::Key::Accept});
    CHECK(t.until([&] { return one.page() == "MakerRoot" && two.page() == "MakerRoot"; }));
    // (a car open in a new game, of Arcade's Ford list)
    std::string car;
    if (const std::vector<script::Value>* fords = one.f.rt->vm().attribute(one.f.rt->vm().lookup("ArcadeProject::CarData"), "ford_carlist").items())
        for (const script::Value& v : *fords)
            if (car.empty() && one.f.rt->game().available_cars.count(v.to_string())) car = v.to_string();
    CHECK(!car.empty());
    CHECK(menu::pick_lan_car(*one.f.rt, car, "ford"));
    CHECK(t.until([&] { return one.page() == "CarRoot"; }));
    const script::Value list = one.f.rt->vm().lookup("ArcadeProject::CarRoot::car_list");
    CHECK(list.items() && list.items()->size() == 1 && (*list.items())[0].to_string() == car);
    t.run(60);
    t.press(one, {menu::Key::Accept, menu::Key::Accept});
    CHECK(t.until([&] { return one.page() == "SettingPopup"; }));
    t.press(one, {menu::Key::Accept});
    CHECK(t.until([&] { return one.page() == "PregridRoot"; }));
    // The guest the same car.
    CHECK(menu::pick_lan_car(*two.f.rt, car, "ford"));
    CHECK(t.until([&] { return two.page() == "CarRoot"; }));
    t.run(60);
    t.press(two, {menu::Key::Accept, menu::Key::Accept});
    CHECK(t.until([&] { return two.page() == "SettingPopup"; }));
    t.press(two, {menu::Key::Accept});
    CHECK(t.until([&] { return one.f.rt->finished() && two.f.rt->finished(); }));
    one.lobby.want_race(one.lobby.host_member().course);
    std::optional<net::OnlineRace> oa, ob;
    CHECK(t.until([&] {
        if (!oa) oa = one.lobby.take_race();
        if (!ob) ob = two.lobby.take_race();
        return oa && ob;
    }));
    CHECK(oa && ob && oa->cars.size() == 2 && ob->cars.size() == 2 && oa->cars[0].label == car && oa->cars[1].label == car && ob->cars[0].label == car);
    if (oa && !oa->cars.empty()) std::printf("  picked: %s, paint %u\n", oa->cars[0].label.c_str(), oa->cars[0].paint);
    CHECK(one.f.clean() && two.f.clean());
}
