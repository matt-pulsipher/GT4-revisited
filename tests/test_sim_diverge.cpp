// Whole races of our session against the program's (gt4r_diff ai): a race car's computer driver at Tsukuba and a road
// car's at Seoul Central, which spun in our session while the program's drivers ran clean (the drivers' downforce term,
// the course distance F +0x4A4).

#include "test_framework.h"

#include "diff.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/program.h"

#include <cmath>
#include <cstdio>

using namespace gt4r;

namespace {

Disc* gt4_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

// The best lap of a run (0: none).
double best(const std::vector<double>& laps) {
    double b = 0;
    for (double l : laps)
        if (b == 0 || l < b) b = l;
    return b;
}

} // namespace

TEST(ai_program_race_car_lap) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The Mazda 787B's driver: its downforce term (0x4673D8: the lift coefficients over the weight) and every decision
    // equal to the program's drivers' in the program's race.
    diff::AiRunOptions opt;
    opt.checks = true;
    opt.seconds = 60;
    const diff::AiRunResult r = diff::run_ai(*disc, false, "tsukuba", {"_787b_91"}, 1, opt);
    CHECK(r.ran);
    auto equal = [&](const char* name) {
        const auto it = r.checks.find(name);
        return it != r.checks.end() && it->second.compared > 0 && it->second.differ == 0;
    };
    for (const char* n : {"params downforce", "params aero bonus", "decision throttle", "decision brake", "decision mode", "decision status", "decision steer"}) {
        if (!equal(n)) std::printf("  %s differs\n", n);
        CHECK(equal(n));
    }
    // Our session with our drivers: no spin, and a lap within 1 % of the program's drivers' in the program's race.
    const diff::AiRunResult theirs = diff::run_ai(*disc, false, "tsukuba", {"_787b_91"}, 2);
    const sim::ai::FieldRun ours = sim::ai::run_field(*disc, "tsukuba", std::vector<std::string>{"_787b_91"}, 2);
    CHECK(theirs.ran && theirs.cars.size() == 1 && ours.cars.size() == 1);
    if (theirs.cars.empty() || ours.cars.empty()) return;
    const double t = best(theirs.cars[0].laps), o = best(ours.cars[0].laps);
    std::printf("  787B at Tsukuba: the program's best %.3f s, ours %.3f s, %d spins, %d times off the road\n", t, o, ours.cars[0].incidents.spins,
                ours.cars[0].incidents.off_road);
    CHECK(ours.cars[0].incidents.spins == 0);
    CHECK(t > 0 && o > 0 && std::fabs(o - t) < t * 0.01);
}

TEST(ai_disc_seoul_lone_car_clean) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The NSX alone at Seoul Central: no spin (the drivers read the program's course distance), a lap near the program's.
    const diff::AiRunResult theirs = diff::run_ai(*disc, false, "seoul", {"nsx_r_02"}, 2);
    const sim::ai::FieldRun ours = sim::ai::run_field(*disc, "seoul", std::vector<std::string>{"nsx_r_02"}, 2);
    CHECK(theirs.ran && theirs.cars.size() == 1 && ours.cars.size() == 1);
    if (theirs.cars.empty() || ours.cars.empty()) return;
    const double t = best(theirs.cars[0].laps), o = best(ours.cars[0].laps);
    std::printf("  NSX at Seoul: the program's best %.3f s, ours %.3f s, %d spins\n", t, o, ours.cars[0].incidents.spins);
    CHECK(ours.cars[0].incidents.spins == 0);
    CHECK(t > 0 && o > 0 && std::fabs(o - t) < t * 0.01);
}
