#pragma once

// main::network (the program's MNetwork, 0x1E9A40) on a lobby of ours (net/lobby.h), and what the arcade project's LAN
// Battle pages need beside it. docs/specs/net.md "The game's own pages". Without a lobby (Runtime::network null) the natives
// answer as a console without the Network Adaptor.

#include "gt4r/menu/game.h"
#include "gt4r/net/lobby.h"

#include "gt4r/script/vm.h"

#include <optional>
#include <string>

namespace gt4r::menu {

class Runtime;
struct GameNatives;

// The MNetwork object.
script::Value make_network(Runtime& rt);

// The arcade project's pages with a lobby: every car (the makers' and the garages' pages of the 2-Player Battle) beside
// the favorites when the host lets them race, a car the host does not let race told at once. Called when the project's
// scripts are read.
void adapt_lan_pages(Runtime& rt);

// The race's settings as the LAN battle takes them (getBattleSettingRace, 0x161F28: the 2-Player Battle's options).
net::RaceRules rules_from_options(const GameState& g);
// The car the pages' player chose (the garage's car, else the car page's car, color and tires), with its driving settings;
// nullopt without one. The digest is not made.
std::optional<net::CarChoice> choice_of_pages(Runtime& rt);

// The pages left from outside: the lobby gone, the arcade's first page next (a lost connection, the lobby's Leave).
void leave_lan_pages(Runtime& rt);

// Arcade's Multi-LAN Race asked for without its pages (the home screen's Online battle): the boot project started for its onLoad
// (a new game's open cars, courses and favorites), the saved game loaded (saves of the app's own), then the state the arcade's GameRoot leaves when the
// icon is chosen (the menu stack's "LANBattle", the major project arcade and page MatchingRoot, the project `network` with the
// argument "LAN"), and the menus ended, as if the arcade's project had ended so. The connection's page then answers it.
void ask_for_lan_battle(Runtime& rt);
// The app's car picker in place of the makers' page (any car, the host's rule): the car the player picked there, which the car page
// then lists alone (its colors and the driving options are the page's own); "" clears it (the makers' page shown again).
void set_lan_pick(Runtime& rt, const std::string& car);
// The car picked on the makers' page (shown): `maker` its maker's key ("ford"), whose pictures the car page loads; the car page
// opens with that car alone. False when the makers' page is not shown.
bool pick_lan_car(Runtime& rt, const std::string& car, const std::string& maker);

} // namespace gt4r::menu
