// The B-spec driver's book after a race (menu/game_race.cpp, 0x468AF0): the change doubled when the race was won
// against others (0x468A60), and the course's marks only on a course whose attribute's flag is set (0x468A40).

#include "menu_run_flow.h"

#include "gt4r/menu/game_bspec.h"
#include "gt4r/menu/race_natives.h"

#include <cstdio>

namespace {

menu::RaceResult bspec_result(const menu::RaceRequest& q, int place, u16 delta, int k) {
    menu::RaceResult r;
    r.kind = menu::RaceResult::Kind::Event, r.race = q.race, r.course = q.course, r.finished = true, r.place = place;
    std::array<u8, 0x40> rec = menu::bspec::race_record(q.bspec_book, k, q.bspec_course_known ? q.bspec_course_marks.data() : nullptr);
    // Table 0, row 0 of the car's column: its experience up by `delta`.
    const u16 v = u16(rec[0] | rec[1] << 8) + delta;
    rec[0] = u8(v), rec[1] = u8(v >> 8);
    r.bspec_record = rec, r.bspec_class = k, r.bspec_course = q.bspec_course_known;
    for (int e = 0; e < 6; ++e) {
        menu::RaceResult::Finisher f;
        f.entry = e == place ? 0 : (e < place ? e + 1 : e), f.car = q.car;
        r.order.push_back(f);
    }
    return r;
}

} // namespace

TEST(disc_gt_mode_bspec_book_after_a_race) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    menu::GameState& g = f.rt->game();
    // The course's flag from its attribute text: Tsukuba's "1ff0" set, Grand Canyon's "0000" not.
    menu::RaceRequest tsukuba, canyon;
    tsukuba.race = "am_sunday_0001", tsukuba.course = "tsukuba", tsukuba.car = "panda_super_ie_90";
    canyon.course = "grandcanyon", canyon.car = tsukuba.car;
    menu::complete_race_request(*f.rt, tsukuba);
    menu::complete_race_request(*f.rt, canyon);
    std::printf("  course marks: tsukuba %d, grand canyon %d\n", int(tsukuba.bspec_course_known), int(canyon.bspec_course_known));
    CHECK(!canyon.bspec_course_known);
    const int k = 2;
    const size_t cell = size_t(k); // table 0, row 0, column k
    const u16 before = g.bspec_book[cell];
    // Won against five others: the change doubled.
    f.rt->race_finished(bspec_result(tsukuba, 0, 10, k));
    CHECK(g.bspec_book[cell] == u16(before + 20));
    // Second: the change as it is.
    tsukuba.bspec_book = g.bspec_book;
    f.rt->race_finished(bspec_result(tsukuba, 1, 10, k));
    CHECK(g.bspec_book[cell] == u16(before + 30));
    // The meters: a B-spec race's to the B-spec total, a driven one's to the A-spec total (0x10FA58), no garage car needed.
    tsukuba.bspec_book = g.bspec_book;
    menu::RaceResult driven = bspec_result(tsukuba, 1, 0, k);
    driven.distance_m = 4200.75f;
    const s64 a0 = g.aspec_distance, b0 = g.bspec_distance;
    f.rt->race_finished(driven);
    CHECK(g.bspec_distance == b0 + 4200 && g.aspec_distance == a0);
    driven.bspec_record.reset();
    f.rt->race_finished(driven);
    CHECK(g.bspec_distance == b0 + 4200 && g.aspec_distance == a0 + 4200);
    // A mission's sequence (0x11BB60) does not count its meters.
    driven.kind = menu::RaceResult::Kind::Mission;
    f.rt->race_finished(driven);
    CHECK(g.bspec_distance == b0 + 4200 && g.aspec_distance == a0 + 4200);
    // The options' volumes as the scripts read them (0x180AA8): the raw 115 / 128, and back.
    {
        script::Vm& vm = f.rt->vm();
        const script::Value opt = vm.attribute(vm.lookup("game"), "option");
        CHECK(vm.attribute(opt, "menu_bgm_volume").as_float() == 115.0f / 128.0f && vm.attribute(opt, "menu_bgm_volume_raw").i == 115);
        vm.member(opt, "replay_bgm_volume")->set(vm, script::Value::number(0.5f));
        CHECK(vm.attribute(opt, "replay_bgm_volume_raw").i == 64);
    }
    // A free run (a course's run) is never doubled.
    tsukuba.bspec_book = g.bspec_book;
    menu::RaceResult run = bspec_result(tsukuba, 0, 10, k);
    run.kind = menu::RaceResult::Kind::Course;
    f.rt->race_finished(run);
    CHECK(g.bspec_book[cell] == u16(before + 40));
}
