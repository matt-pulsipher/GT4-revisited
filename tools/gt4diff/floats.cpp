// The license demonstrations through the program's race step, judged by the program (floats.h).

#include "floats.h"
#include "orig.h"
#include "taps.h"

#include "gt4r/sim/exact_bridge.h"

#include "gt4r/formats/db/race_events.h"
#include "gt4r/formats/replay/demonstration.h"
#include "gt4r/sim/program.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gt4r::diff {

namespace {

constexpr u32 kLicenseCtor = 0x3F5F68;     // DynamicsConductorLicense
constexpr u32 kTrainingVtable = 0x685F50;  // DynamicsConductorTraining: the license's constructor, then its table
constexpr u32 kConfigure = 0x34D418;       // (conductor, race information): the event's row (info +0x70) into CFG
constexpr u32 kRunTimed = 0x3F8AB0;        // (conductor, car, time ms): the run ends timed
constexpr u32 kRunFailed = 0x3F8980;       // (conductor, car, kind): the run ends failed
constexpr u32 kCfg = 0xF818;               // the conductor's configuration (CFG)
constexpr int kLicenseHold = 480;          // the license tests' start: 8 s

} // namespace

std::vector<std::string> demonstration_labels(Disc& disc) {
    db::SpecDb sdb = open_db(disc);
    gt3r::Volume* vol = &disc.volume();
    const db::RaceEvents events(sdb, [vol](const std::string& p) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* f = vol->find(p);
        return f ? std::optional<Bytes>(vol->read(*f)) : std::nullopt;
    });
    std::vector<std::string> out;
    for (const std::string& n : vol->list_dir("replay/license"))
        if (events.event(n)) out.push_back(n);
    return out;
}

DemoScore run_float_demo(Disc& disc, const std::string& label, const FloatDemoOptions& opt, bool print) {
    DemoScore r;
    r.label = label;
    const sim::Program program = sim::Program::load(disc);
    if (!program.known()) return r.error = "the disc's program is not a mapped build", r;
    const gt3r::Volume::Entry* e = disc.volume().find(replay::demonstration_path(label));
    if (!e) return r.error = "no demonstration " + label, r;
    std::string err;
    const auto d = replay::Demonstration::parse(disc.volume().read(*e), program, &err);
    if (!d) return r.error = err, r;
    if (d->streams.empty()) return r.error = "no controls", r;
    db::SpecDb sdb = open_db(disc);
    gt3r::Volume* vol = &disc.volume();
    const db::RaceEvents events(sdb, [vol](const std::string& p) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* f = vol->find(p);
        return f ? std::optional<Bytes>(vol->read(*f)) : std::nullopt;
    });
    // (the file's own event code may be another region's row of the same test: the label names the event)
    auto ev = std::getenv("GT4R_FLOATS_BY_CODE") ? events.event(u32(d->code_b0)) : events.event(label);
    if (!ev) ev = events.event(u32(d->code_b0));
    if (!ev) ev = events.event(label);
    if (!ev) return r.error = "the demonstration's event is not in the database", r;
    r.race_class = d->race_class, r.course = ev->course, r.recorded_ms = d->time_ms;
    r.frames = int(d->streams[0].frames.size());

    std::vector<std::string> labels;
    std::vector<std::array<u8, 0x178>> equipment;
    for (const replay::DemoEntry& en : d->entries) {
        const db::Labels* l = sdb.labels("GENERIC_CAR");
        const std::string* s = l ? l->label(u32(u64(en.equipment.part(sim::Equipment::Car)))) : nullptr;
        labels.push_back(s ? *s : "");
        std::array<u8, 0x178> b{};
        std::copy(en.equipment.bytes.begin(), en.equipment.bytes.end(), b.begin());
        equipment.push_back(b);
    }
    if (labels.empty()) return r.error = "no entries", r;
    r.car = labels[0];
    const bool training = d->race_class == "RaceTraining" || labels.size() > 1;
    // The driving lines (the course's driver file): the pace car's driver, and the line's heading the player's active
    // steering reads (0x4649F0); a course without the file runs without them (its heading 0).
    const bool lines = disc.volume().find("crs/" + ev->course + ".ad") != nullptr;

    Ee ee;
    boot(ee, disc, sdb);
    OrigRace o;
    const auto before_init = [&] {
        if (print && opt.verbose) {
            ee.probe(0x3640DC, [&o](Ee& x) { std::printf("  set-up: 0x46B108 gives line %u, cond +0xCBDE %u\n", x.g32(2), x.r8(o.cond + 0xCBDE)); });
            ee.probe(0x34C5E0, [](Ee& x) { std::printf("  set-up: 0x34C5E0 a0 %08X a1 %08X a2 %d a3 %08X f12 %.3f f13 %.3f (from %08X)\n", x.g32(4), x.g32(5), s32(x.g32(6)), x.g32(7),
                                                    double(x.r8(0) * 0 + gt3r::ee::F::raw(x.fpr[12]).host()), double(gt3r::ee::F::raw(x.fpr[13]).host()), x.g32(31)); });
        }
        // The event's row where the race information keeps it (+0x70), copied into CFG as RaceBase::vf38 does.
        // The race information's first 0x60 bytes as the file keeps them (+0 the laps the conductor's virtual 37 answers).
        for (u32 k = 0; k < 0x60; ++k) ee.w8(o.info + k, d->information[k]);
        const u32 row = ee.alloc_bytes(ev->bytes, 16);
        ee.w32(o.info + 0x70, row);
        ee.call(kConfigure, {Arg::ptr(o.cond), Arg::ptr(o.info)}, 50'000'000);
        // The entries' own words from the file's header.
        for (size_t i = 0; i < d->entries.size() && i < o.entries.size(); ++i) {
            const replay::DemoEntry& en = d->entries[i];
            const u32 entry = o.entries[i];
            ee.w32(entry + 0x198, en.x198);
            ee.w32(entry + 0x3444, u32(en.grid_place));
            ee.w8(entry + 0x3448, en.pedal_style);
            ee.w8(entry + 0x344C, en.driver_kind);
            for (u32 k = 0; k < 4; ++k) ee.w8(entry + 0x3450 + 4 * k, en.aids[k]);
            for (u32 k = 0; k < 0x20; ++k) ee.w8(entry + 0x3468 + k, en.condition[k]);
        }
    };
    if (!setup_race(ee, disc, sdb, ev->course, labels, 1, o, err, lines, &equipment, before_init, kLicenseCtor, training ? kTrainingVtable : 0))
        return r.error = err, r;
    r.kind = ee.r8(o.cond + kCfg + 2);
    // The order before the first step (the conductor's virtual 3, which the step runs after the judge): a training
    // test's pace car ahead of the player. ❓ Where the race orders its field before the first step is not read.
    {
        const u32 vt = ee.r32(o.cond + kCondVtable), slot = vt + 8 * 3;
        ee.call(ee.r32(slot + 4), {Arg::ptr(o.cond + u32(s32(s16(ee.r16(slot)))))}, 50'000'000);
    }
    if (print && opt.verbose) {
        const u32 vt = ee.r32(o.cond + kCondVtable);
        auto slot = [&](u32 k, u32 arg) {
            const u32 sl = vt + 8 * k;
            return ee.call(ee.r32(sl + 4), {Arg::ptr(o.cond + u32(s32(s16(ee.r16(sl))))), Arg::integer(arg)}, 5'000'000);
        };
        const u32 n = u32(slot(42, 0).v0.lo);
        std::printf("  virtual 42 (sectors): %u; virtual 43:", n);
        for (u32 k = 0; k + 1 < n; ++k) std::printf(" (%u) %.3f", k, double(slot(43, k).f0_host()));
        std::printf("\n");
    }
    if (print && opt.verbose) std::printf("  course kind (K +0xCBDD) %u, CFG section %d, the course object's section %d, start bytes CFG +0x4E %u +0x6E %u heading %.4f\n", ee.r8(o.cond + 0xCBDD),
                                                  int(gt3r::s8(ee.r8(o.cond + kCfg + 0x21))), s32(ee.r32(o.course + 0xB0)), ee.r8(o.cond + kCfg + 0x4E), ee.r8(o.cond + kCfg + 0x6E),
                                                  double(ee.rf(o.car(0) + kF + 0x2C)));
    if (print && opt.verbose)
        for (int i = 0; i < o.cars; ++i)
            std::printf("  car %d: place %d crossings %d dist %.2f at %.2f %.2f %.2f speed %.2f\n", i, gt3r::s8(ee.r8(o.car(i) + 0x5B3)), gt3r::s8(ee.r8(o.car(i) + kF + 0x4AC)),
                        double(ee.rf(o.car(i) + kF + 0x4A4)), double(ee.rf(o.car(i) + kF)), double(ee.rf(o.car(i) + kF + 4)), double(ee.rf(o.car(i) + kF + 8)),
                        double(ee.rf(o.car(i) + kF + 0x53C)));

    // The run's end as the judge gives it.
    struct End {
        bool done = false;
        int ms = -1, fail = -1;
    } end;
    // (the judge runs for every car: the player's, car 0, is the one scored; a training test's pace car is judged too)
    const u32 player = o.car(0);
    ee.probe(kRunTimed, [&end, player](Ee& x) {
        if (!end.done && x.g32(5) == player) end.done = true, end.ms = s32(x.g32(6));
    });
    ee.probe(kRunFailed, [&end, player](Ee& x) {
        if (!end.done && x.g32(5) == player) end.done = true, end.fail = s32(x.g32(6));
    });
    if (print && opt.verbose) { // (the line's crossings: the conductor's virtuals 20 and 21)
        ee.probe(0x3F64D0, [](Ee& x) { std::printf("  vf20 car %u a2 %u\n", x.g32(5), x.g32(6)); });
        ee.probe(0x3F6588, [](Ee& x) { std::printf("  vf21 car %u a2 %u a3 %u t0 %u t1 %u\n", x.g32(5), x.g32(6), x.g32(7), x.g32(8), x.g32(9)); });
    }
    if (const char* w = std::getenv("GT4R_FLOATS_WATCH")) ee.watch = (o.car(0) + u32(std::strtoul(w, nullptr, 16))) & ~3u; // (debugging)
    // GT4R_FLOATS_TAPS=1: every routine of ours tapped in at each of the program's calls (step.cpp's taps), the report at
    // the end (where ours and the program part on a demonstration).
    std::unique_ptr<Taps> taps;
    std::unique_ptr<sim::Program> tap_program;
    std::unique_ptr<ex::RaceInputs> tap_inputs;
    if (std::getenv("GT4R_FLOATS_TAPS")) {
        tap_program = std::make_unique<sim::Program>(sim::Program::load(disc));
        set_step_program(tap_program.get());
        tap_inputs = std::make_unique<ex::RaceInputs>(ex::race_inputs(sim::core_program(*tap_program), ex::Conductor::SinglePlayer, ex::RaceConfig{}, false, 0));
        taps = std::make_unique<Taps>(ee, o, *tap_inputs);
        add_step_taps(*taps), add_taps_aids(*taps), add_taps_steer(*taps), add_taps_input(*taps), add_taps_start(*taps);
        add_taps_near(*taps), add_taps_phases(*taps);
    }
    if (opt.on_setup) opt.on_setup(ee);

    // The start's hold: a standing start's 8 s (the race information's count, 0x3C0208: its seconds x 60, counted down a
    // frame at a time before the step); a test that starts moving (the event's start speed) runs at once. ❓ The
    // program's choice between them (the race organization's state at info +0xB0 / +0xB4) is not read: taken here from
    // the car's speed after the set-up.
    const bool moving = std::fabs(ee.rf(o.car(0) + kF + 0x53C)) > 0.01f;
    if (const char* e = std::getenv("GT4R_FLOATS_RANDOM")) ee.w32(0x874D50, u32(std::strtoul(e, nullptr, 16))); // (experiment: the shared random stream's state)
    r.hold = opt.hold >= 0 ? opt.hold : moving ? 0 : kLicenseHold;
    ee.w32(o.info + 0xBC, u32(r.hold));
    const replay::ControlStream& s = d->streams[0];
    const int steps = int(s.frames.size()) + opt.extra;
    if (print)
        std::printf("demo %s: %s kind %d, %s on %s, %zu frames, recorded %d ms, hold %d\n", label.c_str(), d->race_class.c_str(), r.kind,
                    r.car.c_str(), r.course.c_str(), s.frames.size(), r.recorded_ms, r.hold);
    if (print && opt.verbose) {
        std::printf("  information:");
        for (size_t k = 0; k < d->information.size(); ++k) std::printf("%s%02x", k % 16 ? " " : "\n   ", d->information[k]);
        std::printf("\n");
    }
    const u32 F = o.car(0) + kF;
    if (print && opt.verbose) std::printf("  shared random state %08X\n", ee.r32(0x874D50));
    std::vector<replay::PadRecord> rec(1);
    std::unique_ptr<OursBeside> ours;
    if (opt.ours) {
        ours = std::make_unique<OursBeside>(disc);
        if (const char* e = std::getenv("GT4R_FLOATS_OURS_LIST")) ours->list_steps = std::atoi(e);
        ours->resync = std::getenv("GT4R_FLOATS_RESYNC") != nullptr;
        if (!ours->begin(ee, o, sdb, ev->course, r.hold)) return r.error = ours->error, r;
    }
    try {
        for (int i = 0; i < steps && !end.done; ++i) {
            // The race's frame counts the start's hold down before the step.
            const u32 count = ee.r32(o.info + 0xBC);
            if (count > 0) ee.w32(o.info + 0xBC, count - 1);
            rec[0] = s.frame(size_t(i));
            o.pad[0] = Pad{};
            o.pad[0].frame = rec[0].bytes.data();
            if (ours) ours->before_step(ee);
            orig_step(ee, o);
            if (ours) ours->step(ee, rec[0].bytes.data());
            if (end.done) r.ended_step = i;
            const char* from = std::getenv("GT4R_FLOATS_FROM"); // (from this step on every step, 60 of them)
            if (print && opt.verbose && (i % 60 == 0 || end.done || (from && i >= std::atoi(from) && i < std::atoi(from) + 60)))
                std::printf("  %5d  clock %6u  thr %4d brk %4d steer %5d  %7.2f km/h  at %.3f %.3f %.3f  dist %.2f\n", i, ee.r32(o.cond + 0x10000 - 0x768), int(rec[0].throttle()), int(rec[0].brake()), int(rec[0].steer()),
                            double(ee.rf(F + 0x53C)) * 3.6, double(ee.rf(F)), double(ee.rf(F + 4)), double(ee.rf(F + 8)), double(ee.rf(F + 0x4A4)));
            if (print && opt.verbose && o.cars > 1 && (i % 60 == 0 || end.done)) {
                const u32 G = o.car(1) + kF;
                std::printf("         car 1  %7.2f km/h  dist %.2f  %.2f m from car 0, its rule bits %u, place %d, driver modes %u %u\n", double(ee.rf(G + 0x53C)) * 3.6,
                            double(ee.rf(G + 0x4A4)), std::hypot(double(ee.rf(G) - ee.rf(F)), double(ee.rf(G + 4) - ee.rf(F + 4))), ee.r8(G + 0x4B3), int(gt3r::s8(ee.r8(o.car(1) + 0x5B3))),
                            ee.r8(o.car(1) + 0x14 + 0x1E), ee.r8(G + 0x466));
            }
        }
    } catch (const std::exception& ex) {
        char b[160];
        std::snprintf(b, sizeof b, "stopped: %s (pc %08X ra %08X)", ex.what(), ee.cur_pc, ee.g32(31));
        r.error = b;
        if (print) std::printf("demo %s: %s\n", label.c_str(), b);
        return r;
    }
    r.ended_ms = end.ms, r.fail_kind = end.fail;
    r.ran = true;
    if (taps) taps->print();
    if (ours) {
        r.ours_ran = true;
        r.ours_first_bits = ours->first_bits, r.ours_first_apart = ours->first_apart;
        r.ours_worst = ours->worst, r.ours_end = ours->last_distance;
    }
    if (print)
        std::printf("demo %s: %s at step %d: %s (recorded %d ms)\n", label.c_str(),
                    end.ms >= 0 ? "timed" : end.fail >= 0 ? "failed" : "no end", r.ended_step,
                    end.ms >= 0 ? (std::to_string(end.ms) + " ms").c_str() : end.fail >= 0 ? ("kind " + std::to_string(end.fail)).c_str() : "-",
                    r.recorded_ms);
    return r;
}

} // namespace gt4r::diff

namespace gt4r::diff {

int run_floats(Disc& disc, const std::vector<std::string>& args) {
    const std::string what = args.empty() ? "demos" : args[0];
    FloatDemoOptions opt;
    if (const char* e = std::getenv("GT4R_FLOATS_HOLD")) opt.hold = std::atoi(e);
    if (const char* e = std::getenv("GT4R_FLOATS_EXTRA")) opt.extra = std::atoi(e);
    opt.verbose = std::getenv("GT4R_FLOATS_VERBOSE") != nullptr;
    opt.ours = std::getenv("GT4R_FLOATS_OURS") != nullptr;
    if (what == "list") {
        for (const std::string& l : demonstration_labels(disc)) std::printf("%s\n", l.c_str());
        return 0;
    }
    if (what == "census") {
        const std::string label = args.size() > 1 ? args[1] : "usl0b0003";
        FloatCensus c;
        opt.on_setup = [&c](Ee& ee) { c.attach(ee); };
        const DemoScore s = run_float_demo(disc, label, opt, true);
        if (!s.ran) return 1;
        const double steps = double(std::max(1, s.ended_step + 1));
        std::printf("census of %s: %d steps\n", label.c_str(), s.ended_step + 1);
        for (const auto& [k, n] : c.by_kind) std::printf("  %-10s %12llu  %10.1f a step\n", k.c_str(), (unsigned long long)n, double(n) / steps);
        for (const auto& [what, by] : c.special) {
            u64 n = 0;
            for (const auto& [a, k] : by) n += k;
            std::printf("  special: %-32s %10llu in %zu places\n", what.c_str(), (unsigned long long)n, by.size());
        }
        if (const char* out = std::getenv("GT4R_FLOATS_OUT")) {
            if (FILE* f = std::fopen(out, "w")) {
                for (const auto& [a, n] : c.at)
                    if (n) std::fprintf(f, "%08X\t%s\t%llu\n", a, c.kind_at[a].c_str(), (unsigned long long)n);
                for (const auto& [a, n] : c.calls)
                    if (n) std::fprintf(f, "%08X\tcall\t%llu\n", a, (unsigned long long)n);
                std::fclose(f);
            }
        }
        return 0;
    }
    std::vector<std::string> labels;
    const std::string list = what == "demos" ? (args.size() > 1 ? args[1] : "all") : what;
    if (list == "all") labels = demonstration_labels(disc);
    else
        for (size_t a = 0; a <= list.size();) {
            const size_t b = std::min(list.find(',', a), list.size());
            if (b > a) labels.push_back(list.substr(a, b - a));
            a = b + 1;
        }
    int exact = 0, ran = 0;
    for (const std::string& l : labels) {
        const DemoScore s = run_float_demo(disc, l, opt, std::getenv("GT4R_FLOATS_VERBOSE") != nullptr);
        if (!s.ran) {
            std::printf("%-12s error: %s\n", l.c_str(), s.error.c_str());
            continue;
        }
        ++ran;
        exact += s.exact() ? 1 : 0;
        char got[64];
        if (s.ended_ms >= 0) std::snprintf(got, sizeof got, "%d ms", s.ended_ms);
        else if (s.fail_kind >= 0) std::snprintf(got, sizeof got, "FAIL %d", s.fail_kind);
        else std::snprintf(got, sizeof got, "no end");
        std::printf("%-12s %-12s kind %d %-28s %-24s recorded %7d  program %-10s step %5d  %s\n", l.c_str(), s.race_class.c_str(), s.kind,
                    s.car.c_str(), s.course.c_str(), s.recorded_ms, got, s.ended_step,
                    s.exact() ? "EXACT" : s.ended_ms >= 0 ? (std::to_string(s.ended_ms - s.recorded_ms) + " ms").c_str() : "");
        if (s.ours_ran)
            std::printf("%-12s ours: first bit apart at step %d, 1 mm apart at %d, worst %.6f m, at the end %.6f m\n", l.c_str(), s.ours_first_bits,
                        s.ours_first_apart, s.ours_worst, s.ours_end);
        std::fflush(stdout);
    }
    std::printf("floats: %d of %d demonstrations end on their recorded time (%zu asked)\n", exact, ran, labels.size());
    return 0;
}

} // namespace gt4r::diff
