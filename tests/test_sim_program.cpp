// Our simulation's set-up against the user's own program: its set-car, spec builder and converter run
// in the shared engine's interpreter on the Gran Turismo 4 disc of GT4R_DISC (tools/gt4diff/diff.h).

#include "test_framework.h"

#include "diff.h"
#include "gt4r/sim/program.h"

using namespace gt4r;

namespace {

Disc* full_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

} // namespace

TEST(sim_program_spec_equal) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // Every car's equipment as sold and its spec on paved and loose courses: byte for byte the program's.
    const diff::SpecResult r = diff::run_spec(*disc, false);
    CHECK(r.ran && r.cars > 700);
    CHECK(r.equipment_equal == r.cars);
    CHECK(r.spec_equal == r.cars);
}

TEST(sim_program_params_equal) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const diff::ParamsResult r = diff::run_params(*disc, false);
    CHECK(r.ran && r.cars > 700 && r.unfinished == 0);
    // The program's converter and ours agree on every car for these ...
    for (const char* name : {"body front", "body rear", "body width", "body height", "CoG to front axle", "CoG to rear axle", "wheelbase", "track",
                             "half-track front", "half-track rear", "CoG height", "ride height front", "ride height rear", "mass", "origin offset",
                             "steering lock", "yaw inertia", "pitch lever front", "pitch lever rear", "roll lever front", "roll lever rear",
                             "pitch tire term front", "pitch tire term rear", "weight", "drag", "downforce front", "downforce rear", "brake torque front",
                             "brake torque rear", "handbrake torque", "axle grip front", "axle grip rear", "engine curve speeds", "engine curve torques",
                             "rev limit", "idle", "engine friction", "tire width front", "tire width rear", "engine inertia", "front spring rate",
                             "rear spring rate", "front damper bound", "front damper rebound", "rear damper bound", "rear damper rebound", "front anti-roll",
                             "rear anti-roll", "front bump-stop rate", "rear bump-stop rate", "front unsprung weight", "rear unsprung weight",
                             "front bump damping", "rear bump damping", "tire wear rate", "tire grip by surface", "tire curve A", "tire curve B",
                             "tire curve C", "tire curve D", "tire optimum slips", "tire braking peak", "tire load curve"}) {
        const diff::Field* f = r.field(name);
        if (!f || f->off != 0) std::printf("  %s: %s\n", name, f ? f->where.c_str() : "not compared");
        CHECK(f && f->values >= r.cars && f->off == 0);
    }
    // ... and on all but the cars the engine's block cannot hold: the two three-wheelers (their loads and the places
    // along the suspension's travel that follow), the tires of one car, the 52 engines of more than 16 points, the two
    // gearboxes of eight gears, one four wheel drive system, a second reduction of an unknown kind.
    auto at_most = [&](const char* name, size_t n) {
        const diff::Field* f = r.field(name);
        if (!f || f->off > n) std::printf("  %s: %zu differ (%s)\n", name, f ? f->off : 0, f ? f->where.c_str() : "not compared");
        CHECK(f && f->off <= n);
    };
    for (const char* name : {"lateral transfer", "longitudinal transfer", "static load front", "static load rear", "front knee", "front bump limit",
                             "front droop", "front static travel", "rear knee", "rear bump limit", "rear droop", "rear static travel"})
        at_most(name, 2);
    at_most("front offset", 3), at_most("rear offset", 3);
    at_most("tire radius front", 1), at_most("tire radius rear", 1);
    at_most("engine curve points", 52), at_most("forward gears", 2), at_most("driveline layout", 1), at_most("gear speed factors", 10);
    // Gran Turismo 4's own part of the record (rules_build.h): bit for bit on every car, but the tire
    // records' base grip, which takes the static loads of the engine's converter (a unit in the last place
    // on a few records).
    size_t extras = 0;
    for (const diff::Field& f : r.fields) {
        if (f.name.rfind("extra ", 0) != 0) continue;
        ++extras;
        const size_t allowed = f.name == "extra tire.grip" ? 250 : 0;
        if (f.off > allowed) std::printf("  %s: %zu of %zu differ (%s)\n", f.name.c_str(), f.off, f.values, f.where.c_str());
        CHECK(f.values >= r.cars && f.off <= allowed);
    }
    CHECK(extras > 30);
}

TEST(sim_program_leaves_equal) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The C library's sinf, cosf and tanf are the engine's; atan2f is 2002 Tokyo-Geneva's (rules::behaviour()).
    const diff::LeavesResult r = diff::run_leaves(*disc, false);
    CHECK(r.ran && r.samples > 0);
    CHECK(r.sin_off == 0 && r.cos_off == 0 && r.tan_off == 0);
    CHECK(r.atan2_off[2] == 0);
}

TEST(sim_program_step_routines_equal) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The program's own race step on a drive (a start, the gears, turns, the brakes), with each of our
    // routines run in place at every call and compared field by field: equal at every call, for cars of
    // each drive.
    for (const char* car : {"nsx_r_02", "civic_r_01", "impreza_wrc_03"}) {
        const diff::StepResult r = diff::run_step(*disc, false, "tsukuba", car, 900);
        if (!r.ran) std::printf("  %s: %s\n", car, r.error.c_str());
        CHECK(r.ran && !r.taps.empty());
        for (const diff::StepTap& t : r.taps) {
            if (t.calls == 0 || t.equal != t.calls) std::printf("  %s, %s: %zu of %zu calls equal\n", car, t.name.c_str(), t.equal, t.calls);
            CHECK(t.calls > 0 && t.equal == t.calls);
        }
    }
}
