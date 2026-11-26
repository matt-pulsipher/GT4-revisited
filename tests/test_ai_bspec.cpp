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
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d;
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
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d;
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

TEST(ai_decisions_with_tire_wear) {
    Disc* disc = nullptr;
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d;
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The race's tire wear on (a tread curve from the start, a limit above a new tire's grip: F +0x5C2 set): the arcs' speeds by
    // the wheels' wear grips (0x4674F0) and every decision as the program's.
    setenv("GT4R_STEP_TREAD", "1", 1), setenv("GT4R_AI_WEAR", "1.5", 1), setenv("GT4R_AI_PITCHECK", "1", 1);
    diff::AiRunOptions opt;
    opt.checks = true;
    opt.seconds = 40;
    const diff::AiRunResult r = diff::run_ai(*disc, false, "tsukuba", {"nsx_r_02", "ka_01"}, 1, opt);
    unsetenv("GT4R_STEP_TREAD"), unsetenv("GT4R_AI_WEAR"), unsetenv("GT4R_AI_PITCHECK");
    CHECK(r.ran);
    for (const char* n : {"corner speed", "decision v_cur", "decision v_next", "decision steer", "decision throttle", "decision brake"}) {
        const auto it = r.checks.find(n);
        CHECK(it != r.checks.end() && it->second.compared > 500 && it->second.differ == 0);
    }
}

TEST(ai_suggested_gear_speed_equal_to_the_program) {
    Disc* disc = nullptr;
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d;
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
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d;
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // A B-spec driver in a field (car 4, the fastest pace and passing from 5 s) learns during the race (0x4684A8, its record
    // empty at the start): its record (car +0x8C) as ours after every decision, its arcs, braking points and lane changes
    // alone and with a car near; then in a run alone (the conductor's virtual 6, GT4R_AI_SOLO), never with a car near.
    const std::vector<std::string> field{"_1100_standard_type11_49", "z_act_70", "n360_67", "midget2_d_type_98", "s500_63", "ka_01"};
    for (const bool solo : {false, true}) {
        setenv("GT4R_AI_BSPEC", "4,4,5", 1);
        setenv("GT4R_AI_PASS", "1", 1);
        if (solo) setenv("GT4R_AI_SOLO", "1", 1);
        diff::AiRunOptions opt;
        opt.checks = true;
        opt.seconds = 120;
        const diff::AiRunResult r = diff::run_ai(*disc, false, "tsukuba", field, 3, opt);
        unsetenv("GT4R_AI_BSPEC"), unsetenv("GT4R_AI_PASS"), unsetenv("GT4R_AI_SOLO");
        CHECK(r.ran);
        const auto rec = r.checks.find("decision bspec record");
        CHECK(rec != r.checks.end() && rec->second.compared > 10000 && rec->second.differ == 0);
        const auto ch = r.checks.find("decision bspec record (the program's changes)");
        CHECK(ch != r.checks.end() && ch->second.compared >= 5);
        const auto near = r.checks.find("decision bspec record (the program's company)");
        const u64 company = near == r.checks.end() ? 0 : near->second.compared;
        CHECK(solo ? company == 0 : company > 0);
        std::printf("  %s: %llu changes, %llu with a car near\n", solo ? "a run alone" : "a race", ch == r.checks.end() ? 0ull : (unsigned long long)ch->second.compared,
                    (unsigned long long)company);
        for (const char* n : {"decision mode", "decision status", "decision line", "decision brake", "decision throttle", "proximity threat car", "proximity threat gap", "proximity threat rate"}) {
            const auto it = r.checks.find(n);
            CHECK(it != r.checks.end() && it->second.compared > 10000 && it->second.differ == 0);
        }
    }
}


TEST(ai_bspec_shift_factor_on_our_session) {
    Disc* disc = nullptr;
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d;
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // A B-spec Ka at its slowest pace (the shift byte 115) shifts up at a lower engine speed than at its fastest (129).
    auto top_rpm_in_first = [&](int level) {
        const std::vector<std::string> labels{"ka_01"};
        auto session = sim::Session::open(*disc, "tsukuba", labels);
        std::vector<size_t> computer{0};
        auto field = sim::ai::Field::open(*disc, "tsukuba", *session, computer);
        field->set_bspec(0);
        field->command(0, {level, -1});
        std::vector<race::CarInput> in(1);
        float top = 0;
        for (int k = 0; k < sim::Session::kHoldSteps; ++k) field->hold(in), session->hold_once(in);
        for (int step = 0; step < 600; ++step) {
            field->drive(in);
            field->apply_shift_factors(*session);
            session->step_once(in);
            if (session->cars()[0].gear == 1) top = std::max(top, session->cars()[0].engine_rpm);
        }
        return top;
    };
    const float slow = top_rpm_in_first(0), fast = top_rpm_in_first(4);
    std::printf("  first gear's top: pace 0 %.0f rpm, pace 4 %.0f rpm\n", double(slow), double(fast));
    CHECK(slow > 0 && fast > slow);
}


TEST(ai_held_starts_on_our_session) {
    Disc* disc = nullptr;
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d;
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // A place's held start (missions, Pace::start_delay): the session holds its car 60 steps a second of it after the
    // start's count (the program's own count per car, K + 0xCBFC: 240 steps there let it go at 4.0 s, `gt4r_diff ai
    // GT4R_AI_DELAY`), then it goes as the others.
    const std::vector<std::string> labels{"s500_63", "ka_01", "ka_01"};
    auto session = sim::Session::open(*disc, "tsukuba", labels);
    std::vector<size_t> computer{0, 1, 2};
    sim::ai::Pace pace;
    pace.start_delay[1] = 4;
    auto field = sim::ai::Field::open(*disc, "tsukuba", *session, computer, pace);
    field->apply_start_delays(*session);
    std::vector<race::CarInput> in(3);
    std::array<int, 3> moved{-1, -1, -1};
    for (int step = 0; step < 900; ++step) {
        field->drive(in);
        field->apply_time_scales(*session);
        session->step_once(in);
        for (size_t i = 0; i < 3; ++i)
            if (moved[i] < 0 && session->record(i).speed.host() > 1.0f) moved[i] = step;
    }
    std::printf("  first moving: car 0 at step %d, car 1 (4 s held) at %d, car 2 at %d\n", moved[0], moved[1], moved[2]);
    CHECK(moved[0] >= 0 && moved[0] < 240);
    CHECK(moved[2] >= 0 && moved[2] < 240);
    CHECK(moved[1] >= 240 && moved[1] < 300);
}
