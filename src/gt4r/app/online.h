#pragma once

// Online races in the app: the connection's page (in place of the disc's `network` project, which sets a console's network
// adaptor up), the players' panel over the LAN Battle's pages, and the way from the pages to the race and back
// (docs/specs/net.md). The pages themselves are the game's (menu/game_network.h), the race netcore's (net/net_race.h).

#include "gt4r/app/app.h"
#include "gt4r/net/discovery.h"
#include "gt4r/net/lobby.h"

#include <memory>
#include <string>

namespace gt4r {

namespace menu {
class Runtime;
}

// The relay server's name offered (the player approves it before it is ever contacted).
inline constexpr const char* kOfferedServer = "gt3r.gameversions.com";

// The disc's side of the lobby (cars built, digests made): set up for the open disc; the lobby's session named for it.
// False when this disc has no online races (Prologue, a program whose tables are not mapped).
bool online_setup(App& app);
// The lobby's part for mod cars (online_mod_cars.cpp): ours described with their packages and offered, another player's that
// this disc lacks fetched, checked and installed as a mod.
void online_mod_cars(App& app, net::Lobby& lobby);

// The lobby while the menus run.
class OnlineMenus {
public:
    OnlineMenus(App& app, menu::Runtime& rt);
    ~OnlineMenus();

    // Each frame, the menus running. True when the panel has the pad this frame (the pages get no key).
    bool frame(App& app, const MenuInput& m);
    // The menus ended (Runtime::finished) asking for something of ours: the `network` project's connection, or the LAN race.
    // True while it is ours to answer.
    bool finished(App& app);
    void render(App& app, Ui& ui, float x, float scale);

private:
    void panel(App& app, Ui& ui, float x, float y, float w, float h, bool compact);
    void leave();
    void offer_car_picker(App& app);
    bool picker_offered_ = false; // on this showing of the makers' page
    menu::Runtime& rt_;
    net::Discovery discovery_;
    bool connecting_ = false, racing_ = false, open_ = false;
    int sel_ = 0, confirm_ = -1;
    std::string notice_; // what ended the lobby, until read
    bool code_written_ = false; // GT4R_LOBBY_CODEFILE written
    int shown_ = 0;
};

// The connection: host or join, on the local network, by address or through the relay server; the host's rules. Leaves with
// the lobby active (its player's name in the game's options) or not.
std::unique_ptr<Screen> make_connect_screen(App& app, menu::Runtime& rt);

// A scripted online race (GT4R_ONLINE, the top of screen_online.cpp) without the menus, for tests and pictures.
std::unique_ptr<Screen> make_online_script_screen(App& app);

} // namespace gt4r
