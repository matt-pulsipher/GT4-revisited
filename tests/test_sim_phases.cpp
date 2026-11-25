// Phases 1, 2, 3 and 6, the preparation (phase 4) and the hit records (phases 12 and 13) against the user's own program
// (rules_phases.h): the moods' tables, which cars have moods, and the routines at every call of the program's own race
// step (tools/gt4diff taps_phases.cpp). Reads the Gran Turismo 4 disc of GT4R_DISC; skips without it.

#include "test_framework.h"

#include "diff.h"
#include "orig.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/rules_phases.h"

#include <cstdlib>
#include <map>
#include <string>

using namespace gt4r;

namespace gt4r::diff {
std::vector<StepTap> phase_split_results(); // (taps_phases.cpp: the checks at a tail jump or return of the last run)
}

namespace {

Disc* full_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

// Every tap and check of the phases' routines equal at every call; `least`: the checks that must have been called.
void check_run(const char* car, const diff::StepResult& r, std::initializer_list<const char*> least) {
    std::vector<diff::StepTap> all = r.taps;
    for (const diff::StepTap& t : diff::phase_split_results()) all.push_back(t);
    for (const diff::StepTap& t : all) {
        if (t.name.rfind("phases: ", 0) != 0) continue;
        if (t.equal != t.calls) std::printf("  %s, %s: %zu of %zu calls equal\n", car, t.name.c_str(), t.equal, t.calls);
        CHECK(t.equal == t.calls);
    }
    for (const char* name : least) {
        size_t calls = 0;
        for (const diff::StepTap& t : all)
            if (t.name == name) calls += t.calls;
        if (calls == 0) std::printf("  %s, %s: not called\n", car, name);
        CHECK(calls > 0);
    }
}

} // namespace

TEST(sim_phases_mood_tables_and_pods) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = diff::open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    // The moods' tables as the loaded program holds them, against ours from the program's file.
    diff::Ee ee;
    diff::boot(ee, *disc, sdb);
    const sim::rules::MoodTables ours = sim::rules::mood_tables(program);
    const sim::rules::MoodTables theirs = sim::rules::mood_tables([&](u32 a) { return ee.r8(a); });
    CHECK(ours.time == theirs.time && ours.lean == theirs.lean && ours.lamp == theirs.lamp);
    // The cars with moods (the spec's +0x01, the parameter block's +0x32, 1).
    size_t pods = 0, cars = 0;
    std::map<int, std::string> kinds;
    for (const db::CarEntry& c : sdb.cars()) {
        sim::Equipment eq;
        if (!sim::stock_car(sdb, program, c.label, eq)) continue;
        sim::Spec spec;
        sim::build_spec(sdb, program, eq, 0, spec);
        ++cars;
        if (std::getenv("GT4R_PHASES_LIST")) kinds[spec.b(0x01)] += " " + c.label;
        if (spec.b(0x01) == sim::rules::kPodCategory) {
            ++pods;
            if (std::getenv("GT4R_PHASES_LIST")) std::printf("  %s: moods\n", c.label.c_str());
        }
    }
    for (const auto& [k, labels] : kinds) std::printf("  category %d:%s\n", k, labels.substr(0, 300).c_str());
    std::printf("  %zu cars, %zu with moods\n", cars, pods);
    CHECK(cars > 700 && pods == 0); // (no car of the disc has moods: the step's runs below give one the category)
}

TEST(sim_phases_equal_in_the_step) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // A car without moods: phase 1, 2, 3 (its early return), 6, the preparation and its parts, the hit record's clearing and the
    // wall hits of the test drive at Tsukuba.
    {
        const diff::StepResult r = diff::run_step(*disc, false, "tsukuba", "nsx_r_02", 1200);
        CHECK(r.ran);
        check_run("nsx_r_02", r,
                  {"phases: save previous", "phases: layout 7", "phases: moods", "phases: stop count", "phases: prep", "phases: message timer",
                   "phases: course out", "phases: clear hit", "phases: hit record", "phases: hit"});
    }
    // The same car given the category with moods (no car of the disc has it) and its handbrake pulled while it stands
    // (gt4r_diff's GT4R_PHASES_POD and GT4R_PHASES_HANDBRAKE): the moods, the rival's level, the stop count.
    ::setenv("GT4R_PHASES_POD", "1", 1), ::setenv("GT4R_PHASES_HANDBRAKE", "1", 1);
    {
        const diff::StepResult r = diff::run_step(*disc, false, "tsukuba", "nsx_r_02", 600);
        CHECK(r.ran);
        check_run("nsx_r_02 (moods)", r, {"phases: moods", "phases: pod rival", "phases: pod set mood", "phases: pod all soft", "phases: stop count"});
    }
    ::unsetenv("GT4R_PHASES_POD"), ::unsetenv("GT4R_PHASES_HANDBRAKE");
}
