#pragma once

// The program's opponent picker, MCarData.PickUpEnemyCars (0x1308C8; docs/formats/menu.md "The race's
// entry table"): Arcade's single race fields the cars of a list whose showroom evaluation (sim/showroom.h)
// is nearest the player's car's, shifted by the opponents' level. Its steps are here one by one so that
// tools/gt4diff can run each beside the program's.

#include "gt4r/base/types.h"
#include "sim/exact/car_record.h"

#include <functional>
#include <string>
#include <vector>

namespace gt4r::menu {

struct ProgramRandom;

namespace opponents {

using gt3r::ee::F;

// A car of the list the picker keeps (0x44 bytes in the program: its label, then its evaluation).
struct Candidate {
    std::string label;
    F score;
};

// The category a car is matched by: GENERIC_CAR +0x11 modulo 100 (0x448908), a concept car (3) as a
// normal one (0).
int match_category(int category_byte);
// Whether a car of category `c` may race the player's car of category `player`: the same category, or a
// racing car (1) against a tuned player's car (2).
bool category_allowed(int player, int c);

// The C library's qsort (0x5A4BF8: the BSD quicksort, insertion sort below 7, the median of three and of
// nine above 40) with the picker's order (0x130660: the higher evaluation first).
void sort(std::vector<Candidate>& list);
// 0x130618: the binary search over [lo, hi): halves until the middle is lo; the upper half when the middle's
// evaluation is not below x. On a list in falling order, the last place from lo whose evaluation is at least
// x (lo itself when none is).
int search(const std::vector<Candidate>& list, int lo, int hi, F x);
// 0x130690: `count` places drawn from [lo, hi), each a draw of the program's generator times the window's
// size; a place drawn before or `exclude` drawn again; -1 where the window runs out (with `exclude` in it,
// one place before it does).
std::vector<int> draw(ProgramRandom& random, int lo, int hi, int count, int exclude);
// 0x1307E8 (a dirt course): each place moved on, toward lower evaluations, to the first car that has a dirt
// race of its own (`dirt_race`: 1 when the car's ARCADEINFO entry 6 names a race, 0 when not, -1 when the
// car has no row) and is not `exclude`; the step stops at a car with no row or a place outside the list.
using DirtRace = std::function<int(const std::string&)>;
void dirt_places(std::vector<int>& places, const std::vector<Candidate>& list, int exclude, const DirtRace& dirt_race);

// The whole pick (0x1308C8) once each car's evaluation is known: the list's cars of an allowed category in
// list order with their evaluations, sorted; the window of evaluations within target ± range (target: the
// player's evaluation + offset) widened, first toward lower evaluations and then higher, until it holds
// count + 1; the player's own car in it excluded; count places drawn; on a dirt course moved to dirt cars;
// a paint drawn for each place in turn (`paints`: the car's number of paints); the places put in falling
// order of place (the weakest car first), each with the paint drawn in that turn.
struct Pick {
    std::string car;
    int paint = 0;
};
struct PickInput {
    std::string player_label;
    int player_category = 0; // GENERIC_CAR +0x11
    F player_score;
    std::vector<std::string> list;
    F offset, range;
    int count = 5;
    bool dirt = false;
    std::function<bool(const std::string&, int& category)> category;  // false: no such car
    std::function<F(const std::string&)> score;                         // 0 for a car that cannot be built
    DirtRace dirt_race;
    std::function<int(const std::string&)> paints;
};
std::vector<Pick> pick(const PickInput& in, ProgramRandom& random);

} // namespace opponents
} // namespace gt4r::menu
