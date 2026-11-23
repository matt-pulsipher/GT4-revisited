// gt4r_diff's checks of the race step (diff.h): the program's own step run in the interpreter on a race
// its own initialisation set up (orig.h), with our routines compared in place at every call (taps.h).

#include "diff.h"
#include "orig.h"
#include "taps.h"

#include "gt4r/sim/exact_bridge.h"
#include "gt4r/sim/rules.h"
#include "sim/exact/tick.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gt4r::diff {

namespace {

// A routine's car, where it is its first argument: the record, or its state block.
[[maybe_unused]] u32 car_a0(Ee& e) { return u32(e.a(0)); }
[[maybe_unused]] u32 car_of_state(Ee& e) { return u32(e.a(0)) - kF; }

void add_taps(Taps& taps) {
    taps.add({0x36C9F8, "body axes", [](Ee& e) { return u32(e.a(0)) - 0x110; }, [](TapCall& t) { sim::rules::body_axes(t.car); }});
}

// The pad of a test drive at step i: a standing start, through the gears, a lift, then turns each way and the brakes.
Pad drive(int i) {
    Pad p;
    const int t = i - 150;
    if (t < 0) return p;
    p.throttle = 1.0f;
    if (t > 500 && t < 560) p.throttle = 0.0f;
    if (t > 600 && t < 700) p.steer = 0.35f;
    if (t > 760 && t < 860) p.steer = -0.5f;
    if (t > 900) p.throttle = 0.0f, p.brake = 0.8f;
    return p;
}

} // namespace

bool run_step(Disc& disc, bool print) {
    db::SpecDb sdb = open_db(disc);
    const sim::Program program = sim::Program::load(disc);
    if (!program.known()) {
        if (print) std::printf("step: the disc's program is not a mapped build\n");
        return false;
    }
    Ee ee;
    boot(ee, disc, sdb);
    OrigRace o;
    std::string err;
    const char* course = std::getenv("GT4R_STEP_COURSE");
    const char* car = std::getenv("GT4R_STEP_CAR");
    if (!setup_race(ee, disc, sdb, course ? course : "tsukuba", {car ? car : "nsx_r_02"}, 1, o, err)) {
        if (print) std::printf("step: %s\n", err.c_str());
        return false;
    }
    const ex::ProgramData data = sim::core_program(program);
    const ex::RaceConfig config{};
    const ex::RaceInputs inputs = ex::race_inputs(data, ex::Conductor::SinglePlayer, config, false, 0);
    Taps taps(ee, o, inputs);
    add_taps(taps);
    if (std::getenv("GT4R_DUMP")) {
        const u32 at = o.car(0);
        for (u32 off = 0; off < kCarSize; off += 4) {
            const u32 v = ee.r32(at + off);
            if (!v) continue;
            const float f = ee.rf(at + off);
            if (std::fabs(f) > 1e-6f && std::fabs(f) < 1e7f) std::printf("  +%04X %12.6f\n", off, double(f));
            else std::printf("  +%04X %08X\n", off, v);
        }
    }
    const int steps = std::getenv("GT4R_STEPS") ? std::atoi(std::getenv("GT4R_STEPS")) : 1200;
    try {
        for (int i = 0; i < steps; ++i) {
            o.pad[0] = drive(i);
            orig_step(ee, o);
        }
    } catch (const std::exception& e) {
        if (print) std::printf("step: stopped: %s (pc %08X ra %08X)\n", e.what(), ee.cur_pc, ee.g32(31));
        return false;
    }
    if (print) {
        const u32 at = o.car(0);
        std::printf("step: %d steps of the program's own; the car at %.2f %.2f %.2f, %.1f km/h\n", steps, double(ee.rf(at + kF)), double(ee.rf(at + kF + 4)),
                    double(ee.rf(at + kF + 8)), double(ee.rf(at + kF + 0x53C)) * 3.6);
        taps.print();
    }
    return true;
}

} // namespace gt4r::diff
