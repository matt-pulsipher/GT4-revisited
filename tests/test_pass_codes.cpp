// Gran Turismo 4's ranking pass codes (menu/prologue_passcode.h, PassLayout::gt4; game_race.cpp ranking_pass_code): the fields of
// runs the original wrote on the reference cards found again in their codes.

#include "menu_run_flow.h"

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/prologue_license.h"
#include "gt4r/menu/prologue_passcode.h"
#include "gt4r/sim/car_spec.h"

#include <cstdio>

TEST(disc_gt4_pass_codes_of_the_original) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.boot_to_title());
    menu::Database& d = menu::natives_of(*f.rt)->db();
    const sim::Program& p = d.program;
    if (!p.known()) SKIP("the program is not mapped");
    std::array<u8, 8> a, b;
    for (u32 i = 0; i < 8; ++i) a[i] = p.b(0x6A7C88 + i), b[i] = p.b(0x6A7C90 + i);
    const menu::PassCoder coder(a, b, menu::PassLayout::gt4());
    struct Known {
        const char* car;
        const char* course;
        u32 kind, time;
        u32 f8, f9;
        const char* code;
    };
    // (round 3's B-1 bronze on the reference card; the time trial at Tsukuba with the Ka, round 5's)
    const Known known[] = {{"demio_glx_99", "testcourse_100m", 2, 10832, 0, 0, "06632988481512934096208333138160"},
                           {"ka_01", "tsukuba", 0, 124152, ~0u, ~0u, "19467735748752162593161036664679"}};
    // Ours decodes what it encodes.
    const std::vector<u32> made{17, 100, 20, 1, 95, 960, 1, 83'457, 3, 4};
    const std::optional<std::vector<u32>> again = coder.decode(coder.encode(made, 0));
    CHECK(again && *again == made);
    for (const Known& k : known) {
        const s32 car = menu::prologue_table_place(d, "GENERIC_CAR", menu::kCarTable, k.car), course = menu::prologue_table_place(d, "COURSE", menu::kCourseTable, k.course);
        const std::optional<std::vector<u32>> v = coder.decode(k.code);
        CHECK(v.has_value());
        if (!v) continue;
        std::printf("  %s: fields", k.code);
        for (u32 x : *v) std::printf(" %u", x);
        std::printf("; ours: car %d, course %d\n", car, course);
        CHECK((*v)[0] == 17 && (*v)[1] == u32(car) && (*v)[2] == u32(course) && (*v)[6] == k.kind && (*v)[7] == k.time);
        if (k.f8 != ~0u) CHECK((*v)[8] == k.f8 && (*v)[9] == k.f9);
    }
    // Ours made from the runs themselves: the Ka as sold at Tsukuba (the time trial), the Demio of B-1 (its test's car).
    auto run = [&](const char* car, const char* course, const char* race, menu::RaceResult::Kind kind) {
        menu::RaceResult r;
        r.kind = kind, r.course = course, r.race = race;
        menu::RaceResult::Finisher me;
        me.car = car;
        r.order.push_back(me);
        sim::Equipment eq;
        if (sim::stock_car(*d.db, p, car, eq)) r.equipment = eq;
        return r;
    };
    const std::string tt = menu::ranking_pass_code(d, run("ka_01", "tsukuba", "", menu::RaceResult::Kind::Course), 0, 124152);
    const std::string b1 = menu::ranking_pass_code(d, run("demio_glx_99", "testcourse_100m", "l0b0001", menu::RaceResult::Kind::License), 2, 10832);
    for (const std::string& c : {tt, b1}) {
        std::printf("  ours: %s:", c.c_str());
        if (const auto v = coder.decode(c))
            for (u32 x : *v) std::printf(" %u", x);
        std::printf("\n");
    }
    // (the same fields; the first four bits are a hash of the clock's seconds when the entry was written)
    CHECK(coder.decode(tt) == coder.decode(known[1].code));
    CHECK(coder.decode(b1) == coder.decode(known[0].code));
}

