// What the race's side gets of the request the scripts made: RaceScreen reads the game's own request (game().race), so
// what the runtime fills in (the garage car's place, the B-spec driver's book and the course's marks) has to be there
// when the sequence starts. And the B-spec book's write-back: only of a run that started from that book, and never a
// change doubled below zero.

#include "menu_run_flow.h"

#include "gt4r/menu/game_bspec.h"
#include "gt4r/menu/race_natives.h"

#include <cstdio>

namespace {

constexpr size_t kCell = 2; // the book's table 0, row 0, class 2
constexpr int kClass = 2;

// A run asked for through the menus: Gran Turismo mode's World Circuits, the first course's first run.
bool ask_for_a_run(Flow& f) {
    if (!f.gt_mode_with_a_car()) return false;
    f.rt->game().bspec_book[kCell] = 777;
    f.map_focus("RealCircuit");
    f.press({menu::Key::Accept});
    for (int n = 0; n < 6 && !f.rt->race_request(); ++n) {
        for (int i = 0; i < 120; ++i) f.rt->update();
        f.press({menu::Key::Accept});
    }
    return f.rt->race_request().has_value();
}

// The result as RaceScreen makes it: the place from the game's request, the driver's record from the book the race was
// given, up by `gain`.
template <class Result> void start_book(Result& r, const std::array<u16, 3 * 64>& book) {
    if constexpr (requires { r.bspec_start; }) r.bspec_start = book;
}
menu::RaceResult bspec_run(const std::array<u16, 3 * 64>& book, const std::string& car, int place, u16 gain) {
    menu::RaceResult r;
    r.kind = menu::RaceResult::Kind::Event, r.race = "am_sunday_0001", r.course = "tsukuba", r.finished = true, r.place = place;
    std::array<u8, 0x40> rec = menu::bspec::race_record(book, kClass, nullptr);
    const u16 v = u16(rec[0] | rec[1] << 8) + gain;
    rec[0] = u8(v), rec[1] = u8(v >> 8);
    r.bspec_record = rec, r.bspec_class = kClass;
    start_book(r, book);
    for (int e = 0; e < 6; ++e) {
        menu::RaceResult::Finisher f;
        f.entry = e == place ? 0 : (e < place ? e + 1 : e), f.car = car;
        r.order.push_back(f);
    }
    return r;
}

} // namespace

// The garage car's parts and settings reach the race (RaceScreen: race_equipment of the game's request's place).
TEST(disc_race_request_garage_equipment) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(ask_for_a_run(f));
    menu::GameState& g = f.rt->game();
    CHECK(g.riding_car >= 0 && g.garage[size_t(std::max(0, g.riding_car))].record.has_value());
    if (g.riding_car < 0 || !g.garage[size_t(g.riding_car)].record) return;
    // A setting changed on the sheet in use.
    g.garage[size_t(g.riding_car)].record->active()[0x18] = 1;
    const std::optional<sim::Equipment> eq = menu::race_equipment(g, g.race.garage_slot);
    CHECK(eq.has_value());
    if (eq) CHECK(eq->bytes[0x18] == 1);
    // A car picked by its label after it (Arcade) has no place in the garage.
    menu::RaceRequest arcade = g.race;
    arcade.sequence = "single_race", arcade.car = "ka_01", arcade.garage_slot = g.riding_car;
    menu::complete_race_request(*f.rt, arcade);
    CHECK(arcade.garage_slot == -1);
}

// The car's odometer and the engine's distance move by the run.
TEST(disc_race_request_odometer_moves) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(ask_for_a_run(f));
    menu::GameState& g = f.rt->game();
    if (g.riding_car < 0 || !g.garage[size_t(g.riding_car)].record) return;
    const menu::CarRecord& rec = *g.garage[size_t(g.riding_car)].record;
    const u32 odometer = rec.odometer();
    const float engine = rec.condition(1);
    menu::RaceResult r;
    r.kind = menu::RaceResult::Kind::Course, r.course = "tsukuba", r.finished = true;
    r.garage_slot = g.race.garage_slot, r.distance_m = 4200;
    f.rt->race_finished(r);
    std::printf("  odometer %u -> %u, engine %.0f -> %.0f\n", odometer, rec.odometer(), double(engine), double(rec.condition(1)));
    CHECK(rec.odometer() == odometer + 4200 && rec.condition(1) == engine + 4200);
}

// A B-spec race starts from the saved book and adds to it; the sequence's next run starts from the book as it then is.
TEST(disc_race_request_bspec_book_kept) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(ask_for_a_run(f));
    menu::GameState& g = f.rt->game();
    CHECK(g.race.bspec_book == g.bspec_book);
    f.rt->race_finished(bspec_run(g.race.bspec_book, "ka_01", 1, 5)); // second: the change as it is
    std::printf("  the book's cell: 777 -> %d\n", int(g.bspec_book[kCell]));
    CHECK(g.bspec_book[kCell] == 782);
    CHECK(g.race.bspec_book == g.bspec_book);
    f.rt->race_finished(bspec_run(g.race.bspec_book, "ka_01", 0, 5)); // won: doubled
    CHECK(g.bspec_book[kCell] == 792);
}

// A run that started from another book (an empty one) is not written back, so its doubled change cannot go below zero
// and wrap; nor is a record below the book it says it started from.
TEST(disc_race_request_bspec_book_not_spoiled) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(ask_for_a_run(f));
    menu::GameState& g = f.rt->game();
    const std::array<u16, 3 * 64> saved = g.bspec_book;
    f.rt->race_finished(bspec_run({}, "ka_01", 0, 10)); // won, from an empty book
    std::printf("  won from an empty book: 777 -> %d\n", int(g.bspec_book[kCell]));
    CHECK(g.bspec_book == saved);
    f.rt->race_finished(bspec_run({}, "ka_01", 1, 10)); // second, from an empty book
    CHECK(g.bspec_book == saved);
    g.bspec_book = saved;
    menu::RaceResult below = bspec_run(saved, "ka_01", 0, 0); // won, the record fallen to 10
    (*below.bspec_record)[0] = 10, (*below.bspec_record)[1] = 0;
    f.rt->race_finished(below);
    std::printf("  won with a record below the book: 777 -> %d\n", int(g.bspec_book[kCell]));
    CHECK(g.bspec_book[kCell] <= 777);
}

// Whose car a race takes, by its sequence (0x10F948 the garage's driven car in every Gran Turismo mode sequence, 0x10F880 /
// 0x10F8E0 the event's own in license tests and missions, 0x10F7D8 the scripts' car_code in Arcade's): a car code left by
// Arcade or by a license's next test is not taken by a Gran Turismo mode race.
TEST(disc_race_request_car_by_sequence) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    menu::GameState& g = f.rt->game();
    if (g.riding_car < 0) return;
    const std::string mine = g.garage[size_t(g.riding_car)].car;
    for (const char* sequence : {"solo_event", "championship", "freerun", "practice", "photo_drive", "machinetest"}) {
        menu::RaceRequest r;
        r.sequence = sequence, r.race = "am_sunday_0001", r.course = "tsukuba", r.car = "ka_01";
        menu::complete_race_request(*f.rt, r);
        CHECK(r.car == mine && r.garage_slot == g.riding_car);
    }
    menu::RaceRequest test;
    test.sequence = "license", test.race = "l0b0002", test.car = "ka_01";
    menu::complete_race_request(*f.rt, test);
    CHECK(test.car != "ka_01" && !test.car.empty() && test.garage_slot == -1);
    menu::RaceRequest mission;
    mission.sequence = "mission", mission.race = "l0m0001", mission.car = "ka_01";
    menu::complete_race_request(*f.rt, mission);
    CHECK(mission.car != "ka_01" && !mission.car.empty() && mission.garage_slot == -1);
    for (const char* sequence : {"single_race", "time_trial"}) {
        menu::RaceRequest arcade;
        arcade.sequence = sequence, arcade.course = "tsukuba", arcade.car = "ka_01";
        menu::complete_race_request(*f.rt, arcade);
        CHECK(arcade.car == "ka_01" && arcade.garage_slot == -1);
    }
}

// The meters and the B-spec book by the kind of run: an Arcade time trial counts no meters (0x112620 does not call
// 0x10FA58); practice and the machine test write no B-spec record back (0x468AF0 is not called by 0x1181C8, 0x115A90).
TEST(disc_race_request_course_runs) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(ask_for_a_run(f));
    menu::GameState& g = f.rt->game();
    const s64 a0 = g.aspec_distance, b0 = g.bspec_distance;
    menu::RaceResult r;
    r.kind = menu::RaceResult::Kind::Course, r.course_kind = 7, r.course = "tsukuba", r.finished = true, r.distance_m = 4200;
    f.rt->race_finished(r);
    CHECK(g.aspec_distance == a0 && g.bspec_distance == b0);
    for (const int kind : {3, 6}) {
        menu::RaceResult p = bspec_run(g.race.bspec_book, "ka_01", 1, 5);
        p.kind = menu::RaceResult::Kind::Course, p.course_kind = kind, p.race.clear();
        f.rt->race_finished(p);
        CHECK(g.bspec_book[kCell] == 777);
    }
    menu::RaceResult free_run = bspec_run(g.race.bspec_book, "ka_01", 0, 5);
    free_run.kind = menu::RaceResult::Kind::Course, free_run.course_kind = 1, free_run.race.clear();
    f.rt->race_finished(free_run);
    CHECK(g.bspec_book[kCell] == 782);
}
