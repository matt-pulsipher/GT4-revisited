// gt4r_diff's checks of the race step (diff.h): the program's own step run in the interpreter on a race
// its own initialisation set up (orig.h), with our routines compared in place at every call (taps.h).

#include "diff.h"
#include "orig.h"
#include "taps.h"

#include "gt4r/sim/exact_bridge.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_chassis.h"
#include "gt4r/sim/rules_tires.h"
#include "gt4r/sim/rules_wear.h"
#include "sim/exact/tick.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gt4r::diff {

namespace {

// A routine's car, where it is its first argument: the record, or its state block.
[[maybe_unused]] u32 car_a0(Ee& e) { return u32(e.a(0)); }
[[maybe_unused]] u32 car_of_state(Ee& e) { return u32(e.a(0)) - kF; }

// The race's wear rules a car's routines read (its conductor + 4).
sim::rules::WearRules wear_rules_of(TapCall& t) {
    u8 b[0x6C];
    t.ee.read_bytes(t.ee.r32(t.race.car(t.car_index) + 4) + 4, b, sizeof b);
    return sim::rules::wear_rules(b);
}

// The race's per-surface tables as the program's race start left them.
sim::rules::RoadTables road_tables(Ee& ee) {
    sim::rules::RoadTables r;
    r.rolling_curve.n = s16(ee.r16(sim::rules::kRollingCurve));
    const u32 xs = ee.r32(sim::rules::kRollingCurve + 4), ys = ee.r32(sim::rules::kRollingCurve + 8);
    for (u32 k = 0; k < 8; ++k) r.rolling_curve.x[k] = gt3r::ee::F::raw(ee.r32(xs + 4 * k)), r.rolling_curve.y[k] = gt3r::ee::F::raw(ee.r32(ys + 4 * k));
    for (u32 s = 0; s < 24; ++s) r.rolling_mu[s] = gt3r::ee::F::raw(ee.r32(sim::rules::kRollingMu + 4 * s));
    return r;
}

sim::rules::SlipForceGlobals slip_globals(Ee& ee) {
    using namespace sim::rules;
    SlipForceGlobals g;
    g.steer_mode = ee.r32(kSteerMode), g.camber_ref = gt3r::ee::F::raw(ee.r32(kCamberRef));
    g.camber_gain = gt3r::ee::F::raw(ee.r32(kCamberGain)), g.mean_on = ee.r8(kMeanSlipOn);
    return g;
}

void add_taps(Taps& taps) {
    using namespace sim::rules;
    taps.add({0x3658A0, "step length", car_a0, [](TapCall& t) { ex::step_length(t.car); }});
    taps.add({0x356EF0, "wear grip", car_a0, [](TapCall& t) {
                  t.ret_i("returns", wear_grip(t.car, t.rec.x.wear, t.rec.x.tires, t.rec.layout, wear_rules_of(t)));
              }});
    taps.add({0x357418, "peak force", car_a0, [](TapCall& t) {
                  const bool wear = wear_grip(t.car, t.rec.x.wear, t.rec.x.tires, t.rec.layout, wear_rules_of(t));
                  peak_force(t.car, t.rec.x.tires, t.rec.layout, wear);
              }});
    taps.add({0x351778, "slip ratios", car_a0, [](TapCall& t) {
                  u8 b[8];
                  t.ee.read_bytes(kSlipBytes, b, 8);
                  slip_ratios(t.car, t.rec.x.tires, t.rec.layout, slip_bytes(b));
              }});
    taps.add({0x351850, "tire curves", car_a0, [](TapCall& t) { tire_curves(t.car, t.rec.x.tires, t.rec.layout); }});
    taps.add({0x3518E8, "tire long force", car_a0, [](TapCall& t) {
                  u8 b[8];
                  t.ee.read_bytes(kSlipBytes, b, 8);
                  tire_long_force(t.car, t.rec.layout, slip_bytes(b));
              }});
    taps.add({0x352408, "tire slip force", car_a0, [](TapCall& t) { tire_slip_force(t.car, t.rec.x.tires, t.rec.layout, slip_globals(t.ee)); }});
    taps.add({0x352848, "lateral and yaw", car_a0, [](TapCall& t) {
                  lateral_yaw(t.car, t.rec.x.chassis, t.rec.x.tires, t.rec.layout, t.ee.a(1) != 0, slip_globals(t.ee));
              }});
    taps.add({0x352C90, "stop hold", car_a0, [](TapCall& t) { stop_hold(t.car, t.rec.layout, t.ee.a(1) != 0); }});
    taps.add({0x351FA8, "forward step", car_a0, [](TapCall& t) { forward_step(t.car, t.rec.x.chassis, t.rec.x.tires, t.rec.layout); }});
    taps.add({0x3534A0, "rolling resistance", car_a0, [](TapCall& t) { rolling_resistance(t.car, t.rec.layout, road_tables(t.ee)); }});
    taps.add({0x357690, "slip angles", car_a0, [](TapCall& t) { slip_angles(t.car, t.rec.layout); }});
    taps.add({0x343ED0, "save previous", car_a0, [](TapCall& t) { ex::save_previous(t.car); }});
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

StepResult run_step(Disc& disc, bool print, const std::string& course_label, const std::string& car_label, int step_count) {
    StepResult result;
    db::SpecDb sdb = open_db(disc);
    const sim::Program program = sim::Program::load(disc);
    if (!program.known()) {
        result.error = "the disc's program is not a mapped build";
        if (print) std::printf("step: %s\n", result.error.c_str());
        return result;
    }
    Ee ee;
    boot(ee, disc, sdb);
    OrigRace o;
    std::string err;
    const char* course = std::getenv("GT4R_STEP_COURSE");
    const char* car = std::getenv("GT4R_STEP_CAR");
    const std::string course_name = !course_label.empty() ? course_label : course ? course : "tsukuba";
    const std::string car_name = !car_label.empty() ? car_label : car ? car : "nsx_r_02";
    if (!setup_race(ee, disc, sdb, course_name, {car_name}, 1, o, err)) {
        result.error = err;
        if (print) std::printf("step: %s\n", err.c_str());
        return result;
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
    // The race's wear switches (the conductor + 4 + 0x68 / 0x69), to run the wear's branches: bit 0 the tread wear, bit 1 the second.
    if (const char* w = std::getenv("GT4R_STEP_WEAR")) {
        const int m = std::atoi(w);
        ee.w8(o.cond + 4 + 0x68, u8(m & 1)), ee.w8(o.cond + 4 + 0x69, u8((m >> 1) & 1));
        // A made-up curve for each (the race's own are 0 here), and the wheels' wears spread over every branch.
        const float tread[8] = {-0.5f, 0.1f, 0.2f, 0.05f, 0.5f, 0.15f, 0.8f, 0.3f}, second[6] = {-0.4f, 0.1f, 0.3f, 0.1f, 0.9f, 0.25f};
        for (u32 k = 0; k < 8; ++k) ee.wf(o.cond + 4 + 4 * k, tread[k]);
        for (u32 k = 0; k < 6; ++k) ee.wf(o.cond + 4 + 0x50 + 4 * k, second[k]);
        const float a[4] = {-0.6f, -0.2f, 0.1f, 0.35f}, b[4] = {-0.5f, -0.1f, 0.2f, 0.6f};
        for (int i = 0; i < o.cars; ++i)
            for (u32 w = 0; w < 4; ++w) {
                const u32 W = o.car(i) + sim::layout::kWheel0 + sim::layout::kWheelSize * w;
                ee.wf(W + 0xAC, a[w] + 0.6f * float(m >> 2)), ee.wf(W + 0xA8, b[w] + 0.5f * float(m >> 2));
            }
    }
    if (std::getenv("GT4R_DUMP_WEAR")) {
        for (u32 off = 0; off < 0x6C; off += 4) std::printf("  wear +%02X %08X %g\n", off, ee.r32(o.cond + 4 + off), double(ee.rf(o.cond + 4 + off)));
    }
    if (std::getenv("GT4R_DUMP_POINTERS")) { // the parameter block's pointers into itself (its tables' arrays)
        const u32 pb = o.params(0);
        for (u32 off = 0; off < kParamsSize; off += 4) {
            const u32 v = ee.r32(pb + off);
            if (v >= pb && v < pb + kParamsSize) std::printf("  P+%04X -> P+%04X\n", off, v - pb);
        }
    }
    const int steps = step_count > 0 ? step_count : std::getenv("GT4R_STEPS") ? std::atoi(std::getenv("GT4R_STEPS")) : 1200;
    try {
        for (int i = 0; i < steps; ++i) {
            o.pad[0] = drive(i);
            orig_step(ee, o);
        }
    } catch (const std::exception& e) {
        char b[160];
        std::snprintf(b, sizeof b, "stopped: %s (pc %08X ra %08X)", e.what(), ee.cur_pc, ee.g32(31));
        result.error = b;
        if (print) std::printf("step: %s\n", b);
        return result;
    }
    if (print) {
        const u32 at = o.car(0);
        std::printf("step: %d steps of the program's own; the car at %.2f %.2f %.2f, %.1f km/h\n", steps, double(ee.rf(at + kF)), double(ee.rf(at + kF + 4)),
                    double(ee.rf(at + kF + 8)), double(ee.rf(at + kF + 0x53C)) * 3.6);
        taps.print();
    }
    for (const TapStat& st : taps.stats()) result.taps.push_back({st.name, size_t(st.calls), size_t(st.clean)});
    result.ran = true;
    return result;
}

} // namespace gt4r::diff
