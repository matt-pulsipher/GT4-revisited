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
        // (The driveline block: its tires' radii and widths, the differentials' settings and the axles' inertias are
        // still the engine's converter's, a unit or two in the last place: compared by size below.)
        if (f.name.rfind("extra d.", 0) == 0) continue;
        // (and fields made from the engine converter's values, a unit or two in the last place: the droops while held,
        // from the droops and hub offsets; the ABS's peak scale, from the tire radii; the front's share, 1 less the rear's)
        size_t allowed = f.name == "extra tire.grip" ? 250 : 0;
        if (f.name == "extra body.hold_droop" || f.name == "extra aids.abs_peak") allowed = 1444;
        else if (f.name == "extra body.front_share") allowed = 130;
        else if (f.name == "extra steer.axle_load" || f.name == "extra steer.compliance_per_n") allowed = 220; // (from the shares and masses)
        else if (f.name == "extra steer.toe") allowed = 1;                                                    // (from a tire radius)
        // (the lock curves the input phase reads: the engine converter's, a unit or two in the last place on most points; GT4's
        // converter (sim-parts' build_record) makes them bit for bit)
        else if (f.name.rfind("extra input.lock_", 0) == 0) allowed = 9386;
        if (f.off > allowed) std::printf("  %s: %zu of %zu differ (%s)\n", f.name.c_str(), f.off, f.values, f.where.c_str());
        CHECK(f.values >= r.cars && f.off <= allowed);
    }
    CHECK(extras > 30);
    // The driveline block by size: equal but for the tank's tires, one car's axle inertia, the down-shift speeds a
    // stepless box's hold speed sets (0x360780) or the tank's tires move, and the speed factors of the tank.
    for (const diff::Field& f : r.fields) {
        if (f.name.rfind("near d.", 0) != 0) continue;
        size_t allowed = 0;
        if (f.name == "near d.down_rpm") allowed = 4;
        else if (f.name == "near d.gear_speed") allowed = 8;
        else if (f.name == "near d.tire_radius" || f.name == "near d.inv_tire_radius" || f.name == "near d.wheel_gain") allowed = 2;
        else if (f.name == "near d.axle_inertia" || f.name == "near d.inv_axle_inertia") allowed = 1;
        if (f.off > allowed) std::printf("  %s: %zu of %zu differ (%s)\n", f.name.c_str(), f.off, f.values, f.where.c_str());
        CHECK(f.off <= allowed);
    }
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
    for (const char* car : {"nsx_r_02", "civic_r_01", "impreza_wrc_03", "r34_gtr_v2_nur_02", "fit_w_ff_01", "a3_32_quattro_03"}) {
        const diff::StepResult r = diff::run_step(*disc, false, "tsukuba", car, 900);
        if (!r.ran) std::printf("  %s: %s\n", car, r.error.c_str());
        CHECK(r.ran && !r.taps.empty());
        for (const diff::StepTap& t : r.taps) {
            if (t.equal != t.calls) std::printf("  %s, %s: %zu of %zu calls equal\n", car, t.name.c_str(), t.equal, t.calls);
            // (a stepless box's own gear step; the aids' speed caps' throttle and the TCS's rising slip, which a pad car runs only
            // under a cap or with its TCS on: gt4r_diff's GT4R_AIDS_VARY runs them; the two-wheeler's lean and the angle helpers
            // the steering's offset uses rolling backwards or in mode 2: GT4R_STEER_VARY; phase 14's pair routines, which need two
            // cars, and the speed-switched part's routines, on the cars that have one: test_sim_near.cpp; the input phase's taps
            // for other controllers and options: GT4R_INPUT_VARY)
            const bool rare = t.name.rfind("near:", 0) == 0 || t.name.rfind("speed part:", 0) == 0 || t.name.rfind("input:", 0) == 0 || t.name.rfind("phases: pod", 0) == 0 || t.name.rfind("phases: pit", 0) == 0 || t.name.rfind("phases: hit", 0) == 0 || (t.name == "gear" && std::string(car) == "fit_w_ff_01") || t.name == "aids: hold throttle" || t.name == "aids: rising slip" ||
                              t.name == "steer: lean angle" || t.name == "steer: angle difference" || t.name == "steer: angle blend" ||
                              t.name.rfind("start: hold lift", 0) == 0 || t.name == "start: rolling hold" || t.name == "start: release";
            CHECK((t.calls > 0 || rare) && t.equal == t.calls);
        }
    }
}
