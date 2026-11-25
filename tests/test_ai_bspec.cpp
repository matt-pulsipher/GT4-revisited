// The B-spec driver's skill (sim/ai/bspec_skill.h) against the program's 0x468C50 on made-up driver records.

#include "test_framework.h"

#include "diff.h"
#include "gt4r/sim/ai/bspec_skill.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/session.h"
#include "gt4r/sim/program.h"

#include <algorithm>
#include <cstdio>
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

TEST(ai_suggested_gear_speed_equal_to_the_program) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d.get();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The pad car's suggested gear (0x469E58): whether it is set and firm, with the driver's targets and mode made up around
    // the car's speed at every call.
    setenv("GT4R_AI_PAD", "0", 1);
    setenv("GT4R_AI_SUGFORCE", "1", 1);
    diff::AiRunOptions opt;
    opt.checks = true;
    opt.seconds = 12;
    const diff::AiRunResult r = diff::run_ai(*disc, false, "tsukuba", {"s500_63", "ka_01"}, 1, opt);
    unsetenv("GT4R_AI_PAD");
    unsetenv("GT4R_AI_SUGFORCE");
    CHECK(r.ran);
    for (const char* n : {"suggested gear set", "suggested gear firm"}) {
        const auto it = r.checks.find(n);
        CHECK(it != r.checks.end() && it->second.compared > 500 && it->second.differ == 0);
    }
    const auto set = r.checks.find("suggested gear (the program's set)");
    CHECK(set != r.checks.end() && set->second.compared > 200);
}

TEST(ai_bspec_experience_equal_to_the_program) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d.get();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // A B-spec driver (car 0, at the fastest pace from 5 s) learns during the race (0x4684A8): its record (car +0x8C) as
    // ours after every decision.
    setenv("GT4R_AI_BSPEC", "0,4,5", 1);
    diff::AiRunOptions opt;
    opt.checks = true;
    opt.seconds = 200;
    const diff::AiRunResult r = diff::run_ai(*disc, false, "tsukuba", {"s500_63", "ka_01"}, 2, opt);
    unsetenv("GT4R_AI_BSPEC");
    CHECK(r.ran);
    const auto rec = r.checks.find("decision bspec record");
    CHECK(rec != r.checks.end() && rec->second.compared > 10000 && rec->second.differ == 0);
    const auto ch = r.checks.find("decision bspec record (the program's changes)");
    CHECK(ch != r.checks.end() && ch->second.compared >= 2);
}
