// The checks of gt4r_diff (diff.h): routines of the user's own CORE.GT4 run in the shared engine's
// R5900 interpreter (engine/gt3r/tools/diffharness) on the disc's own data, against ours. The
// routines are named by address (docs/specs/physics.md); nothing of the program is kept.

#include "diff.h"

#include "orig.h"

#include "disc/core_image.h"
#include "ee.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/db/car_parts.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/exact_bridge.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/start.h"

#include <cmath>
#include <cstdlib>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace gt4r::diff {

SpecResult run_spec(Disc& disc, bool print) {
    SpecResult result;
    db::SpecDb sdb = open_db(disc);
    const sim::Program program = sim::Program::load(disc);
    if (!program.known()) return result;
    result.ran = true;
    Ee ee;
    boot(ee, disc, sdb);
    const u32 eq_at = ee.alloc(0x200), spec_at = ee.alloc(0x600);
    size_t shown = 0, cars = 0, eq_same = 0, spec_same = 0, spec_same_given_eq = 0;
    std::map<u32, size_t> eq_bad, spec_bad;
    std::string first_eq, first_spec;
    for (const db::CarEntry& c : sdb.cars()) {
        sim::Equipment ours;
        if (!sim::stock_car(sdb, program, c.label, ours)) continue;
        ++cars;
        for (u32 o = 0; o < 0x200; o += 4) ee.w32(eq_at + o, 0);
        ee.call(kSetCar, {Arg::ptr(eq_at), Arg::integer(db::part_id(0, c.id))}, 50'000'000);
        std::array<u8, sim::Equipment::kSize> theirs{};
        ee.read_bytes(eq_at, theirs.data(), u32(theirs.size()));
        // (The gear words past the gearbox's count keep what the last car's generator left: not compared.)
        const u32 gears = db::part_row(sdb, ours.part(sim::Equipment::Gear))[0x24];
        for (u32 i = gears + 1; i < 12; ++i) theirs[0x110 + 2 * i] = ours.bytes[0x110 + 2 * i], theirs[0x111 + 2 * i] = ours.bytes[0x111 + 2 * i];
        bool same = true;
        for (u32 o = 0; o < theirs.size(); ++o)
            if (theirs[o] != ours.bytes[o]) {
                same = false, ++eq_bad[o];
                if (first_eq.empty()) first_eq = c.label;
            }
        eq_same += same;
        if (!same && shown++ < 6) {
            std::printf("  %s gears %d, slider %d; the program's:", c.label.c_str(), db::part_row(sdb, ours.part(sim::Equipment::Gear))[0x24], ours.b(sim::Equipment::GearSlider));
            for (u32 i = 0; i < 12; ++i) std::printf(" %d", gt3r::s16(theirs[0x110 + 2 * i] | (theirs[0x111 + 2 * i] << 8)));
            std::printf("; ours:");
            for (u32 i = 0; i < 12; ++i) std::printf(" %d", gt3r::s16(ours.h(0x110 + 2 * i)));
            std::printf("\n");
        }
        // The spec of the program's own equipment, by the program and by us.
        sim::Equipment given;
        given.bytes = theirs;
        for (int kind : {0, 2, 3}) {
            ee.call(kBuildSpec, {Arg::ptr(spec_at), Arg::ptr(eq_at), Arg::integer(0), Arg::integer(kind)}, 50'000'000);
            std::array<u8, sim::Spec::kSize> want{};
            ee.read_bytes(spec_at, want.data(), u32(want.size()));
            sim::Spec got;
            sim::build_spec(sdb, program, given, kind, got);
            bool equal = true;
            for (u32 o = 0; o < want.size(); ++o)
                if (want[o] != got.bytes[o]) {
                    equal = false, ++spec_bad[o];
                    if (first_spec.empty()) first_spec = c.label + (kind ? " (loose course)" : "");
                }
            if (kind == 0) spec_same_given_eq += equal;
            if (kind == 0 && same) spec_same += equal;
        }
    }
    result.cars = cars, result.equipment_equal = eq_same, result.spec_equal = spec_same_given_eq;
    if (print) {
        std::printf("spec: %zu cars; the equipment equal on %zu, the spec (of the program's equipment) equal on %zu\n", cars, eq_same, spec_same_given_eq);
        auto list = [](const char* what, const std::map<u32, size_t>& bad, const std::string& first) {
            if (bad.empty()) return;
            std::printf("  %s differs at (offset: cars):", what);
            size_t n = 0;
            for (const auto& [o, k] : bad)
                if (n++ < 60) std::printf(" %03X:%zu", o, k);
            std::printf("%s; first on %s\n", bad.size() > 60 ? " ..." : "", first.c_str());
        };
        list("the equipment", eq_bad, first_eq);
        list("the spec", spec_bad, first_spec);
    }
    (void)spec_same;
    return result;
}

// The parameter block: the program's converter on its spec against the engine's on ours translated, field by field.
ParamsResult run_params(Disc& disc, bool print) {
    ParamsResult result;
    namespace ex = gt3r::sim::exact;
    db::SpecDb sdb = open_db(disc);
    const sim::Program program = sim::Program::load(disc);
    if (!program.known()) return result;
    result.ran = true;
    Ee ee;
    boot(ee, disc, sdb);
    ee.call(kStatics, {Arg::integer(0)});
    const ex::ProgramData core = sim::core_program(program);
    const u32 spec_at = ee.alloc(0x600), p_at = ee.alloc(0x1400), rules_at = ee.alloc(0x100), cond_at = ee.alloc(0x100);
    std::vector<Field>& fields = result.fields;
    auto field = [&](const char* name) -> Field& {
        for (Field& f : fields)
            if (f.name == name) return f;
        fields.push_back({name});
        return fields.back();
    };
    size_t cars = 0, trapped = 0;
    for (const db::CarEntry& c : sdb.cars()) {
        sim::Equipment eq;
        if (!sim::stock_car(sdb, program, c.label, eq)) continue;
        sim::Spec spec;
        sim::build_spec(sdb, program, eq, 0, spec);
        // The program's: its converter on the same spec.
        ee.write_bytes(spec_at, spec.bytes);
        for (u32 o = 0; o < 0x1400; o += 4) ee.w32(p_at + o, 0);
        try {
            ee.call(kConvert, {Arg::ptr(p_at), Arg::ptr(spec_at), Arg::integer(0), Arg::integer(0), Arg::ptr(rules_at), Arg::ptr(cond_at)}, 200'000'000);
        } catch (const std::exception& e) {
            if (trapped++ < 3 && print) std::printf("  %s: %s\n", c.label.c_str(), e.what());
            continue;
        }
        // Ours: the engine's converter on the translated spec, then Gran Turismo 4's own rules.
        ex::Spec cs;
        sim::core_spec(spec, cs);
        ex::ConvertInputs in;
        in.data = &core, in.base = 0x01000010;
        in.physics_globals();
        ex::ParamImage image{};
        ex::convert_params(cs, in, image);
        ex::Record record{};
        std::copy(image.begin(), image.end(), record.begin() + ex::kParams);
        ex::Car car;
        ex::load_image(car, record.data());
        sim::gt4_params(spec, program, car.p);
        const ex::Params& p = car.p;
        ++cars;
        // Equal: within a part in 100,000 (ours is in the same arithmetic but not everywhere in the same order).
        auto check = [&](const char* name, double theirs, double ours, double within = 1e-5) {
            Field& f = field(name);
            ++f.values;
            const double d = std::fabs(theirs - ours), rel = d / std::max(std::fabs(theirs), 1e-9);
            if (d > 1e-6 && rel > within) {
                ++f.off;
                if (rel > f.worst) f.worst = rel, f.where = c.label + ": the program's " + std::to_string(theirs) + ", ours " + std::to_string(ours);
            }
        };
        auto pf = [&](u32 o) { return double(ee.rf(p_at + o)); };
        check("body front", pf(0x00), p.body_front.host()), check("body rear", pf(0x04), p.body_rear.host());
        check("body width", pf(0x08), p.body_width.host()), check("body height", pf(0x0C), p.body_height.host());
        check("CoG to front axle", pf(0x10), p.dist_front.host()), check("CoG to rear axle", pf(0x14), p.dist_rear.host());
        check("wheelbase", pf(0x18), p.wheelbase.host()), check("track", pf(0x1C), p.track.host());
        check("half-track front", pf(0x28), p.half_track[0].host()), check("half-track rear", pf(0x2C), p.half_track[1].host());
        check("lateral transfer", pf(0x34), p.lat_transfer.host()), check("longitudinal transfer", pf(0x38), p.long_transfer.host());
        check("CoG height", pf(0x3C), p.cog_height.host());
        check("ride height front", pf(0x40), p.ride_height[0].host()), check("ride height rear", pf(0x44), p.ride_height[1].host());
        check("mass", pf(0x48), p.mass.host()), check("origin offset", pf(0x50), p.x54.host()), check("steering lock", pf(0x54), p.max_lock.host());
        check("yaw inertia", pf(0x114), p.yaw_inertia.host());
        check("pitch lever front", pf(0x11C), p.pitch_lever[0].host()), check("pitch lever rear", pf(0x120), p.pitch_lever[1].host());
        check("roll lever front", pf(0x124), p.roll_lever[0].host()), check("roll lever rear", pf(0x128), p.roll_lever[1].host());
        check("pitch tire term front", pf(0x12C), p.pitch_tire[0].host()), check("pitch tire term rear", pf(0x130), p.pitch_tire[1].host());
        check("weight", pf(0x103C), p.weight.host());
        check("static load front", pf(0x1040), p.xe54.host()), check("static load rear", pf(0x1044), p.xe58.host());
        check("drag", pf(0x1068), p.drag_coef.host());
        check("downforce front", pf(0x106C), p.downforce_coef[0].host()), check("downforce rear", pf(0x1070), p.downforce_coef[1].host());
        check("brake torque front", pf(0x1010), p.brake_torque[0].host()), check("brake torque rear", pf(0x1014), p.brake_torque[1].host());
        check("handbrake torque", pf(0x1018), p.handbrake_torque.host());
        check("axle grip front", pf(0xFD0), p.axle_grip[0].host()), check("axle grip rear", pf(0xFD4), p.axle_grip[1].host());
        // The engine (the program's block at +0x134: the curve's points, speeds from +0x0C, torques from +0x6C).
        const u32 E = p_at + 0x134;
        const int n = gt3r::s16(ee.r16(E));
        check("engine curve points", n, p.e.torque.n);
        if (n == p.e.torque.n)
            for (int i = 0; i < n && i < 16; ++i) {
                check("engine curve speeds", double(ee.rf(E + 0x0C + 4 * u32(i))), p.e.torque.x[size_t(i)].host());
                check("engine curve torques", double(ee.rf(E + 0x6C + 4 * u32(i))), p.e.torque.y[size_t(i)].host());
            }
        check("rev limit", ee.r16(E + 0xCC), p.e.rev_limit), check("idle", ee.r16(E + 0xCE), p.e.idle);
        check("engine friction", double(ee.rf(E + 0xD0)), p.e.friction.host());
        // The driveline (+0x234): gears, each gear's speed factor (+0x68), tire radius (+0x90) and width (+0xA0).
        const u32 D = p_at + 0x234;
        check("forward gears", ee.r8(D + 2), p.d.gears);
        check("driveline layout", ee.r8(D), p.d.layout);
        for (u32 g = 0; g <= ee.r8(D + 2) && g <= p.d.gears && g < 8; ++g) check("gear speed factors", double(ee.rf(D + 0x68 + 4 * g)), p.d.gear_speed[g].host(), 1e-3); // (a second reduction: to a ratio's last digit)
        check("tire radius front", double(ee.rf(D + 0x90)), p.tire_radius[0].host()), check("tire radius rear", double(ee.rf(D + 0x94)), p.tire_radius[1].host());
        check("tire width front", double(ee.rf(D + 0xA0)), p.width[0].host()), check("tire width rear", double(ee.rf(D + 0xA4)), p.width[1].host());
        check("engine inertia", double(ee.rf(D + 0x0C)), p.d.engine_inertia.host());
        // The tire records (+0x410 + 0x1EC x record; the core's record with 24 surfaces, what follows them 0x24 further on):
        // those the core built (a surface's class names them), surface by surface as the core's rows stand for them.
        for (u32 r = 0; r < 6; ++r) {
            const ex::TireRecord& t = p.tire[r];
            if (t.mu[0] == gt3r::ee::F()) continue;
            const u32 T = p_at + 0x410 + 0x1EC * r;
            auto tf = [&](u32 o) { return double(ee.rf(T + o)); };
            check("tire base grip", tf(0x00), t.grip.host()), check("tire wear rate", tf(0x04), t.wear_rate.host());
            for (int row = 0; row < 16; ++row) {
                if (row == 11) continue; // (the wear grip's floor in the core's record)
                check("tire grip by surface", tf(0x08 + 4 * u32(sim::surface_of_core_row(row))), t.mu[size_t(row)].host());
            }
            for (u32 i = 0; i < 8; ++i) check("tire curve A", tf(0x78 + 4 * i) + tf(0x98 + 4 * i), double(t.curve_a.x[i].host()) + double(t.curve_a.y[i].host()));
            check("tire peak slip angle", tf(0xB8), t.peak_slip.host());
            for (u32 i = 0; i < 6; ++i) check("tire curve B", tf(0xC8 + 4 * i) + tf(0xE0 + 4 * i), double(t.curve_b.x[i].host()) + double(t.curve_b.y[i].host()));
            for (u32 i = 0; i < 11; ++i) {
                check("tire curve C", tf(0x104 + 4 * i) + tf(0x134 + 4 * i), double(t.curve_c.x[i].host()) + double(t.curve_c.y[i].host()));
                check("tire curve D", tf(0x17C + 4 * i), t.curve_d[i].host());
            }
            check("tire optimum slips", tf(0x164) + tf(0x168), double(t.slip_drive.host()) + double(t.slip_brake.host()));
            check("tire braking peak", tf(0x16C), t.brake_peak.host());
            for (u32 i = 0; i < 4; ++i) check("tire load curve", tf(0x1B8 + 4 * i) * 1e-4 + tf(0x1C8 + 4 * i), double(t.load_curve.x[i].host()) * 1e-4 + double(t.load_curve.y[i].host()));
        }
        // The suspension blocks (+0x308 + 0x58 x axle), the fields the core's blocks have at the same places.
        for (u32 a = 0; a < 2; ++a) {
            const u32 B = p_at + 0x308 + 0x58 * a;
            const ex::Suspension& u = p.susp[a];
            auto bf = [&](u32 o) { return double(ee.rf(B + o)); };
            check(a ? "rear knee" : "front knee", bf(0x00), u.knee.host());
            check(a ? "rear bump limit" : "front bump limit", bf(0x04), u.bump_limit.host());
            check(a ? "rear droop" : "front droop", bf(0x08), u.droop.host());
            check(a ? "rear offset" : "front offset", bf(0x0C), u.offset.host());
            check(a ? "rear static travel" : "front static travel", bf(0x10), u.x_static.host());
            check(a ? "rear spring rate" : "front spring rate", bf(0x14), u.k.host());
            check(a ? "rear damper bound" : "front damper bound", bf(0x18) + bf(0x1C) * 1e-3 + bf(0x20) * 1e-6, u.bump_t.host() + u.bump_o.host() * 1e-3 + u.bump_g.host() * 1e-6);
            check(a ? "rear damper rebound" : "front damper rebound", bf(0x24) + bf(0x28) * 1e-3 + bf(0x2C) * 1e-6, u.reb_t.host() + u.reb_o.host() * 1e-3 + u.reb_g.host() * 1e-6);
            check(a ? "rear anti-roll" : "front anti-roll", bf(0x30), u.anti_roll.host());
            check(a ? "rear bump-stop rate" : "front bump-stop rate", bf(0x34), u.k2.host());
            check(a ? "rear unsprung weight" : "front unsprung weight", bf(0x3C), u.preload.host());
            check(a ? "rear bump damping" : "front bump damping", bf(0x40), u.bump_damping.host());
        }
    }
    result.cars = cars, result.unfinished = trapped;
    if (print) {
        std::printf("params: %zu cars (%zu the program's converter did not finish)\n", cars, trapped);
        for (const Field& f : fields) {
            if (f.off == 0) std::printf("  %-24s equal (%zu values)\n", f.name.c_str(), f.values);
            else std::printf("  %-24s differs on %zu of %zu, most by %.3g%% (%s)\n", f.name.c_str(), f.off, f.values, f.worst * 100, f.where.c_str());
        }
    }
    return result;
}

} // namespace gt4r::diff
