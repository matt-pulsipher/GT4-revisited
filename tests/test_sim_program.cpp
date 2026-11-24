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

// Gran Turismo 4's converter (sim::convert) against the program's on every car, word by word over the whole block: as
// sold, and with a computer's car's inputs, a power cap, a worn engine, ballast and the entry's percentages.
TEST(sim_program_converter_block_equal) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const diff::ParamsResult r = diff::run_params(*disc, false);
    CHECK(r.ran && r.cars > 700 && r.unfinished == 0);
    for (const auto& [o, n] : r.block_off) std::printf("  block +0x%04X differs on %zu cars\n", o, n);
    for (const auto& [o, n] : r.varied_off) std::printf("  varied +0x%04X differs on %zu cars\n", o, n);
    CHECK(r.block_equal == r.cars);
    CHECK(r.varied_equal == r.cars);
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
    // The record as the session builds it from Gran Turismo 4's converter (sim::build_record): every field of its own part
    // that the parameter block holds, bit for bit on every car, the driveline block's too.
    size_t extras = 0;
    for (const diff::Field& f : r.fields) {
        if (f.name.rfind("extra ", 0) != 0 && f.name.rfind("near d.", 0) != 0) continue;
        ++extras;
        if (f.off != 0) std::printf("  %s: %zu of %zu differ (%s)\n", f.name.c_str(), f.off, f.values, f.where.c_str());
        CHECK(f.values >= r.cars && f.off == 0);
    }
    CHECK(extras > 30);
    for (const char* name : {"extra d.layout", "extra d.gears", "extra d.kind", "extra d.ratio", "extra d.awd_k", "extra d.awd_capacity", "extra d.launch_rpm"}) {
        const diff::Field* f = r.field(name);
        CHECK(f && f->off == 0);
    }
}

TEST(sim_program_leaves_equal) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The C library's sinf, cosf and tanf are the engine's; atan2f is 2002 Tokyo-Geneva's (rules::behaviour()).
    const diff::LeavesResult r = diff::run_leaves(*disc, false);
    CHECK(r.ran && r.samples > 0);
    CHECK(r.sin_off == 0 && r.cos_off == 0 && r.tan_off == 0 && r.sincos_off == 0);
    CHECK(r.atan2_off[2] == 0);
}

TEST(sim_program_collision_and_road_query_equal) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // Six computer drivers' cars at Grand Canyon (dirt, close walls) for a minute of the program's own race: at every call of
    // its collision step (0x45BB60) ours steps the same bodies on our walls, and at every call of its road query (0x3E5A30)
    // ours answers the same point on our road; every body field and every answer field equal.
    diff::AiRunOptions opt;
    opt.ours = true, opt.collide = 1, opt.seconds = 60;
    const diff::AiRunResult r = diff::run_ai(*disc, false, "grandcanyon",
                                             {"_1100_standard_type11_49", "z_act_70", "n360_67", "midget2_d_type_98", "s500_63", "ka_01"}, 3, opt);
    if (!r.ran) std::printf("  %s\n", r.error.c_str());
    CHECK(r.ran);
    std::printf("  collision steps %llu (%llu with a wall met), %llu equal; road queries %llu, %llu equal\n", (unsigned long long)r.collide_calls,
                (unsigned long long)r.collide_walls, (unsigned long long)r.collide_equal, (unsigned long long)r.query_calls, (unsigned long long)r.query_equal);
    CHECK(r.collide_calls == 3600 && r.collide_walls > 100 && r.collide_equal == r.collide_calls);
    CHECK(r.query_calls > 100000 && r.query_equal == r.query_calls);
}

TEST(sim_program_step_routines_equal) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The program's own race step on a drive (a start, the gears, turns, the brakes), with each of our
    // routines run in place at every call and compared field by field: equal at every call, for cars of
    // each drive.
    for (const char* car : {"nsx_r_02", "civic_r_01", "impreza_wrc_03", "r34_gtr_v2_nur_02", "fit_w_ff_01", "a3_32_quattro_03", "midget_mp5_63"}) {
        const diff::StepResult r = diff::run_step(*disc, false, "tsukuba", car, 900);
        if (!r.ran) std::printf("  %s: %s\n", car, r.error.c_str());
        CHECK(r.ran && !r.taps.empty());
        for (const diff::StepTap& t : r.taps) {
            if (t.equal != t.calls) std::printf("  %s, %s: %zu of %zu calls equal\n", car, t.name.c_str(), t.equal, t.calls);
            // (a stepless box's own gear step; the aids' speed caps' throttle and the TCS's rising slip, which a pad car runs only
            // under a cap or with its TCS on: gt4r_diff's GT4R_AIDS_VARY runs them; the two-wheeler's lean and the angle helpers
            // the steering's offset uses rolling backwards or in mode 2: GT4R_STEER_VARY)
            const bool rare = (t.name == "gear" && std::string(car) == "fit_w_ff_01") || t.name == "aids: hold throttle" || t.name == "aids: rising slip" ||
                              t.name == "steer: lean angle" || t.name == "steer: angle difference" || t.name == "steer: angle blend";
            CHECK((t.calls > 0 || rare) && t.equal == t.calls);
        }
    }
}
