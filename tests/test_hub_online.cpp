// The home screen's Online battle: Arcade's Multi-LAN Race asked for without the arcade's pages (menu::ask_for_lan_battle), the
// connection's page then answering it as it answers the arcade's icon. With a lobby the arcade opens its LAN Battle's matching
// page, as after the icon; without one the menus ask for the arcade (the app then goes back to the home screen).

#include "menu_run_flow.h"

#include "gt4r/menu/game.h"
#include "gt4r/menu/game_network.h"
#include "gt4r/net/lobby.h"
#include "net_ports.h"

#include <chrono>
#include <thread>

namespace {

struct Player {
    Flow f;
    gt3r::net::Session session;
    net::Lobby lobby{session};
    explicit Player(Disc& d) : f(d) {
        session.set_disc({"gt4", "another game"});
        f.rt->network = &lobby;
    }
};

// The menus as the shortcut starts them: booted, no project, the Multi-LAN Race asked for.
bool ask(Player& p) {
    p.f.rt->boot();
    menu::ask_for_lan_battle(*p.f.rt);
    // (a new game's open cars, as at power on: the boot project's)
    return !p.f.rt->game().available_cars.empty() && p.f.rt->finished() && p.f.rt->wanted_project() == "network" && !p.f.rt->race_request();
}

// What the connection's page does once a lobby is open or joined (app/screen_online.cpp ConnectScreen::begin), then the arcade.
bool on_to_arcade(Player& p) {
    menu::GameState& g = p.f.rt->game();
    g.next_menu_project = g.major_menu_project.empty() ? std::string("arcade") : g.major_menu_project;
    g.next_menu_arg.clear();
    return p.f.rt->advance();
}

} // namespace

TEST(disc_hub_online_battle) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    if (disc->info().game != Game::GT4) SKIP("Prologue has no Multi-LAN Race");
    gt3r::net::Session::set_advertise_lan(false);
    // Back from the connection with no lobby: the arcade is asked for, and no lobby is there (the app leaves for the home screen).
    {
        Player p(*disc);
        CHECK(ask(p));
        menu::GameState& g = p.f.rt->game();
        g.next_menu_project = g.major_menu_project;
        CHECK(p.f.rt->wanted_project() == "arcade" && !p.lobby.active());
    }
    // A host by the shortcut and a guest by the arcade's own icon: both on the matching page, then the course page together.
    Player one(*disc), two(*disc);
    CHECK(ask(one));
    std::string error;
    CHECK(test::on_free_port([&](u16 at) { return one.session.host(at, "One", "", 0, &error); }));
    menu::GameState& g1 = one.f.rt->game();
    g1.options["LAN_host"] = 1, g1.options["LAN_style"] = std::string("play"), g1.options["LAN_machine_name"] = std::string("One");
    g1.options["LAN_player_num"] = 2, g1.options["LAN_monitor_num"] = 0;
    one.lobby.begin("One");
    CHECK(two.f.boot_to_title());
    two.f.press({menu::Key::Accept});
    CHECK(two.f.page() == "GameRoot" && two.f.focus_on("LANBattle"));
    two.f.press({menu::Key::Accept});
    CHECK(two.f.rt->finished() && two.f.rt->wanted_project() == "network");
    menu::GameState& g2 = two.f.rt->game();
    // The shortcut leaves the menus as the icon leaves them.
    CHECK(g1.available_cars == g2.available_cars && g1.favorite_cars == g2.favorite_cars);
    CHECK(g1.menu_stack == g2.menu_stack && g1.major_menu_page == g2.major_menu_page && g1.major_menu_project == g2.major_menu_project);
    g2.options["LAN_host"] = 0, g2.options["LAN_style"] = std::string("play"), g2.options["LAN_machine_name"] = std::string("Two");
    g2.options["LAN_player_num"] = 2, g2.options["LAN_monitor_num"] = 0;
    CHECK(two.session.join("127.0.0.1:" + std::to_string(one.session.port()), "Two", "", 0, &error));
    two.lobby.begin("Two");
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; !(one.lobby.active() && two.lobby.active()) && std::chrono::steady_clock::now() - start < std::chrono::seconds(20); ++i)
        one.lobby.poll(), two.lobby.poll(), std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(one.lobby.active() && two.lobby.active());
    CHECK(on_to_arcade(one) && on_to_arcade(two));
    bool both = false;
    for (int i = 0; !both && (i < 4000 || std::chrono::steady_clock::now() - start < std::chrono::seconds(20)); ++i) {
        for (Player* p : {&one, &two}) p->lobby.poll(), p->f.rt->update();
        if (i % 4 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        both = one.f.page() == "CourseRoot" && two.f.page() == "CourseRoot";
    }
    if (!both) std::printf("  stopped at %s / %s (%d %s)\n", one.f.page().c_str(), two.f.page().c_str(), two.f.rt->finished(), two.f.rt->wanted_project().c_str());
    CHECK(both && one.lobby.count() == 2);
    // The lobby left: the arcade asked for, no lobby (the home screen next).
    menu::leave_lan_pages(*one.f.rt);
    CHECK(one.f.rt->finished() && one.f.rt->wanted_project() == "arcade" && !one.lobby.active());
    CHECK(one.f.clean() && two.f.clean());
}
