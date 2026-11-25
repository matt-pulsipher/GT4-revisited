// The steering wheel's force (0x353EE8, rules_force.h) against the user's own program: its tables as the race's start builds
// them (0x353938 at the end of 0x347A00, on the disc's controller settings and made-up ones), and the routine at every call of
// the program's own race step for each controller kind, a computer's car, the tables' switches, the steering offset's term
// and slow driving (tools/gt4diff taps_force.cpp, GT4R_FORCE_VARY), on several cars and courses. Reads the Gran Turismo 4
// disc of GT4R_DISC; skips without it.

#include "test_framework.h"

#include "diff.h"
#include "orig.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/record.h"
#include "gt4r/sim/rules_force.h"
#include "gt4r/sim/session.h"

#include <cstdlib>
#include <memory>
#include <string>

using namespace gt4r;

namespace {

Disc* full_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

} // namespace

// A car a computer drives: the frame's steering / 32 from 0x80 (doubled for a kind 6 wheel), within the byte; the start's
// release count left alone.
TEST(sim_force_computer_car) {
    gt3r::sim::exact::Car c;
    sim::Extra x;
    sim::rules::WheelLayout l;
    sim::rules::ForceTables t;
    sim::rules::SteerTables g;
    c.control = 2, c.driver_kind = 0;
    x.start.release_count = 30;
    auto run = [&](gt3r::s16 steer, u8 kind) {
        x.input.steer_in = steer, x.input.steer_kind = kind;
        sim::rules::steer_force(c, x, l, t, g, {});
        return int(x.force.out);
    };
    CHECK(run(0, 0) == 0x80);
    CHECK(run(320, 0) == 0x80 - 10);
    CHECK(run(-31, 0) == 0x80); // (toward 0)
    CHECK(run(-320, 3) == 0x80 + 20);
    CHECK(run(8000, 3) == 0 && run(-8000, 3) == 0xFF);
    CHECK(x.start.release_count == 30 && x.force.wheel[0] == 0);
}

TEST(sim_force_tables_equal) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = diff::open_db(*disc);
    diff::Ee ee;
    diff::boot(ee, *disc, sdb);
    const u32 cond = ee.alloc(diff::kCondSize, 64);
    // 0x347A00 (which builds the force's tables last) on the disc's settings and on made-up ones: each limit kind, the slip
    // shares on with a zero sum or angle and on, the pedal curves' kinds, the offset's damping negative, K + 0x7A set, the
    // rate table's points out of order, the kind 6 ratios changed.
    const u32 K = sim::rules::kPadSettings;
    struct Change {
        u32 offset;
        u8 value;
    };
    auto row = [](int m, u32 o) { return 0x7C + 0x20 * u32(m) + o; };
    const std::vector<std::vector<Change>> runs = {
        {},
        {{row(0, 0x17), 1}, {row(1, 0x17), 2}, {row(2, 0x17), 3}, {row(0, 0xA), 30}, {row(0, 0xB), 70}, {row(0, 0xC), 8}, {row(0, 0xD), 20},
         {row(1, 0xA), 0}, {row(1, 0xB), 0}, {row(1, 0xC), 5}, {row(2, 0xA), 10}, {row(2, 0xC), 0}, {0x7A, 1}, {0x75, 90}, {0x76, 200}},
        {{row(0, 1), 1}, {row(0, 2), 40}, {row(0, 0x15), 2}, {row(0, 0x16), 70}, {row(1, 1), 3}, {row(1, 2), 30}, {row(2, 0x12), 2}, {row(2, 0x13), 0},
         {row(1, 0x11), 0xF0}, {row(0, 6), 0}, {row(1, 7), 0}, {row(2, 0x1D), 90}, {row(2, 0x1E), 20}, {0x61, 3}, {0x62, 0}, {0x77, 9}, {0x79, 200}},
    };
    size_t checked = 0, equal = 0;
    for (const std::vector<Change>& run : runs) {
        for (const Change& c : run) ee.w8(K + c.offset, c.value);
        ee.call(0x347A00, {diff::Arg::ptr(cond)}, 50'000'000);
        Bytes image(diff::kBssStart - 0x100000);
        ee.read_bytes(0x100000, image.data(), u32(image.size()));
        const sim::Program program = sim::Program::from_image(std::move(image), 0x100000);
        const sim::rules::ForceTables theirs = sim::rules::read_force_tables([&](u32 a) { return ee.r32(a); }, [&](u32 a) { return ee.r8(a); });
        std::string why;
        const bool same = sim::rules::same(sim::rules::force_tables(program), theirs, &why);
        if (!same) std::printf("  settings run %zu: %s\n", checked, why.c_str());
        ++checked, equal += same;
    }
    CHECK(checked == 3 && equal == checked);
}

TEST(sim_force_step_equal) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The program's own race step with ours in place at every call of the force: a pad car from its standing start (the
    // release count) through the step's run, with each variation of GT4R_FORCE_VARY on the NSX at Tsukuba, and other cars
    // and courses as they run.
    struct Run {
        const char* course;
        const char* car;
        std::vector<int> modes;
    };
    for (const Run& run : {Run{"tsukuba", "nsx_r_02", {0, 1, 2, 3, 4, 5}}, Run{"grandcanyon", "nsx_r_02", {0, 1}}, Run{"seoul", "fit_w_ff_01", {0, 4}},
                           Run{"tsukuba", "prius_g_02", {0}}, Run{"tsukuba", "patent_motor_wagen_886", {0, 3}}, Run{"grandcanyon", "mtr_gmv_04", {0, 3}}}) {
        for (int mode : run.modes) {
            if (mode) setenv("GT4R_FORCE_VARY", std::to_string(mode).c_str(), 1);
            const diff::StepResult r = diff::run_step(*disc, false, run.course, run.car, 600);
            unsetenv("GT4R_FORCE_VARY");
            if (!r.ran) std::printf("  %s at %s, variation %d: %s\n", run.car, run.course, mode, r.error.c_str());
            CHECK(r.ran && !r.taps.empty());
            size_t calls = 0;
            for (const diff::StepTap& t : r.taps) {
                if (t.name != "steer force") continue;
                if (t.equal != t.calls) std::printf("  %s at %s, variation %d: %zu of %zu calls equal\n", run.car, run.course, mode, t.equal, t.calls);
                CHECK(t.equal == t.calls);
                calls = t.calls;
            }
            CHECK(calls > 500);
        }
    }
}

// The session: a pad car's release count runs out; its force turns over with the steering; the accessor gives the pad block's
// byte; a computer's car without a frame's steering stays centered. (A computer's car's count is not checked: the constructor's
// hold steps drive every car as the pad's, which counts it down; the program's stays at 30.)
TEST(sim_force_session) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = diff::open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    const gt3r::Volume::Entry* file = disc->volume().find("crs/tsukuba");
    CHECK(file != nullptr);
    if (!file) return;
    const Bytes data = disc->volume().read(*file);
    const course::CourseFile parsed = course::CourseFile::parse(data);
    const auto road = std::make_shared<const course::Runway>(course::Runway::parse(parsed.section(data, course::kSlotRunway)));
    std::vector<sim::Entry> e(2);
    for (int i = 0; i < 2; ++i) {
        CHECK(sim::stock_car(sdb, program, "nsx_r_02", e[size_t(i)].equipment));
        e[size_t(i)].grid_place = i;
    }
    sim::Session s(sdb, program, road, e);
    CHECK(s.gt4_rules());
    std::vector<race::CarInput> in(2);
    in[1].computer = true;
    in[0].throttle = 0.6f, in[1].throttle = 0.2f;
    s.set_countdown(0);
    for (int k = 0; k < 240; ++k) s.step_once(in);
    CHECK(s.gt4_record(0).start.release_count == 0);
    const float turn = 0.3f;
    in[0].throttle = 0.2f;
    in[0].steer = turn;
    for (int k = 0; k < 45; ++k) s.step_once(in);
    const int one = s.steer_force(0);
    in[0].steer = -turn;
    for (int k = 0; k < 90; ++k) s.step_once(in);
    const int other = s.steer_force(0);
    CHECK(one != 0x80 && other != 0x80 && (one < 0x80) != (other < 0x80));
    CHECK(s.steer_force(0) == s.gt4_record(0).force.out && s.steer_force(1) == s.gt4_record(1).force.out);
    CHECK(s.steer_force(1) == 0x80);
}
