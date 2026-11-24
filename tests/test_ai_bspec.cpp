// The B-spec driver's skill (sim/ai/bspec_skill.h) against the program's 0x468C50 on made-up driver records.

#include "test_framework.h"

#include "diff.h"
#include "gt4r/sim/ai/bspec_skill.h"
#include "gt4r/sim/program.h"

#include <cstdlib>

using namespace gt4r;

TEST(ai_bspec_skill_equal_to_the_program) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d.get();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // Cars of the lowest, a middle and the highest class, records over and under the caps, the course byte both ways.
    const diff::BSpecSkillResult r = diff::run_bspec_skill(*disc, false, {"s500_63", "nsx_r_02", "_787b_91"}, 48);
    CHECK(r.ran);
    CHECK(r.calls == 144);
    CHECK(r.equal == r.calls);
    // An empty record: the lowest of each multiplier (the course byte clear: its records read as full).
    const sim::ai::BSpecTables t = sim::ai::BSpecTables::load(sim::Program::load(*disc));
    const auto s = sim::ai::bspec_skill(t, {}, gt3r::ee::F(0.5f), false);
    CHECK(s.has_value());
    if (s) CHECK(s->corner.host() > 0.89f && s->corner.host() < 0.94f && s->alongside.host() < 1.3f);
}

TEST(ai_pd_steering_equal_to_the_program) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d.get();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The steering mode this program leaves unused (T[2] = 2: the PD steering), set in both races: every decision equal.
    setenv("GT4R_AI_STEERMODE", "2", 1);
    diff::AiRunOptions opt;
    opt.checks = true;
    opt.seconds = 25;
    const diff::AiRunResult r = diff::run_ai(*disc, false, "tsukuba", {"s500_63", "ka_01"}, 1, opt);
    unsetenv("GT4R_AI_STEERMODE");
    CHECK(r.ran);
    for (const char* n : {"decision steer", "decision pd terms", "decision pd rate", "decision throttle", "decision state"}) {
        const auto it = r.checks.find(n);
        CHECK(it != r.checks.end() && it->second.compared > 1000 && it->second.differ == 0);
    }
}
