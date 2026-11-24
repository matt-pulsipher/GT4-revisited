// The pit stops (sim/ai/pit_flow.h, pit_stops.h): the program's pit pass and stop timing against ours at every call in
// the program's own race (gt4r_diff, a car sent in by its pit request), and a stop on our session.

#include "test_framework.h"

#include "diff.h"
#include "gt4r/sim/ai/pit_stops.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/session.h"

#include <cstdio>
#include <cstdlib>

using namespace gt4r;

namespace {

Disc* gt4_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

} // namespace

TEST(ai_pits_pure) {
    // The service of a computer's car: tires (and fuel when counted); a stop's last steps.
    sim::ai::PitCar c;
    sim::ai::ServiceInputs s;
    s.pits_on = true, s.control = 2;
    CHECK(sim::ai::service_of(c, s) == 0xC0);
    s.fuel_on = true;
    CHECK(sim::ai::service_of(c, s) == 0x1C0);
    c.hold = 61, c.service = 0xC0;
    std::vector<gt3r::s8> axle{1, 1, -1, -1};
    sim::ai::HoldEvents e = sim::ai::pit_hold_step(c, axle);
    CHECK(c.hold == 60 && e.new_tires.size() == 4 && c.service == 0);
    e = sim::ai::pit_hold_step(c, axle);
    CHECK(c.hold == 59 && c.service == 0x200);
    // Refuelling holds the count at 90 until the tank is full.
    c.hold = 90, c.service = 0x100;
    sim::ai::pit_hold_step(c, axle);
    CHECK(c.hold == 90);
    gt3r::ee::F fuel(59.99f); // (2 a second: a step puts 0.033 in)
    sim::ai::pit_refuel(c, fuel, gt3r::ee::F(60.0f), gt3r::ee::F(1.0f / 60.0f), 20);
    CHECK(fuel.host() == 60.0f && c.service == 0 && c.hold == 90);
}

TEST(ai_pits_equal_to_the_program) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // Car 0 asks for the pits at 20 s (as B-spec's command does): the program's whole stop and ours, call by call.
    setenv("GT4R_AI_PITCHECK", "1", 1);
    setenv("GT4R_AI_PIT", "0,20", 1);
    diff::AiRunOptions opt;
    opt.seconds = 190;
    const diff::AiRunResult r = diff::run_ai(*disc, false, "tsukuba", {"s500_63", "ka_01"}, 3, opt);
    unsetenv("GT4R_AI_PITCHECK");
    unsetenv("GT4R_AI_PIT");
    CHECK(r.ran);
    CHECK(r.pit_calls > 20000);
    CHECK(r.pit_equal == r.pit_calls);
    CHECK(r.pit_stops == 1);
    CHECK(r.hold_calls >= 721 && r.hold_equal == r.hold_calls);
}

TEST(ai_pits_on_our_session) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // Two cars at Tsukuba, car 0 sent in at 20 s: down the pit lane, the stop line, 12 s standing, back to racing.
    const std::vector<std::string> labels{"s500_63", "ka_01"};
    auto session = sim::Session::open(*disc, "tsukuba", labels);
    std::vector<size_t> computer{0, 1};
    auto field = sim::ai::Field::open(*disc, "tsukuba", *session, computer);
    const sim::Program program = sim::Program::load(*disc);
    sim::ai::PitStops pits(*field, program, 2);
    std::vector<race::CarInput> in(2);
    for (int k = 0; k < sim::Session::kHoldSteps; ++k) field->hold(in), session->hold_once(in);
    bool lane = false, stop_line = false, out = false;
    int held = 0, stood = 0;
    for (int step = 0; step < 60 * 200; ++step) {
        if (step == 20 * 60) pits.request(0);
        field->drive(in);
        pits.before(*field, in);
        session->step_once(in);
        pits.after(*session, *field);
        const int kind = field->kind(0);
        lane = lane || kind == 2, stop_line = stop_line || kind == 3;
        if (pits.car(0).hold > 0) ++held;
        if (pits.car(0).hold > 0 && std::fabs(session->cars()[0].speed) < 0.1f) ++stood;
        if (stop_line && kind == 0 && held > 0) out = true;
    }
    std::printf("  pit lane %d, stop line %d, held %d steps (%d standing), back out %d, stops %d\n", int(lane), int(stop_line), held, stood, int(out), pits.stops(0));
    CHECK(lane && stop_line && out);
    CHECK(pits.stops(0) == 1);
    CHECK(held >= 700 && held <= 730);
    CHECK(stood > 600);
}
