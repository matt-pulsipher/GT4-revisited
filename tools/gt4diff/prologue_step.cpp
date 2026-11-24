// gt4r_diff's run of Gran Turismo 4 Prologue's own race step (prologue_race.h): a test drive of one car on a course,
// what the program's step makes of it.
//
//   gt4r_diff <prologue disc> prologue [course] [car] [steps]

#include "orig.h"
#include "prologue_race.h"
#include "prologue_taps.h"

#include "gt4r/sim/exact_bridge.h"
#include "gt4r/sim/rules_prologue.h"
#include "gt4r/sim/session.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/params_build.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>

namespace gt4r::diff::prologue {

namespace {

// The pad of a test drive at step i: a standing start at full throttle, a lift, turns each way and the brakes.
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

int run(Disc& disc, const std::string& course, const std::string& car, int steps) {
    if (!is_prologue(disc)) {
        std::printf("prologue: the disc's program is not Prologue's\n");
        return 1;
    }
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc);
    std::printf("prologue: the image's mean slip switch %u, steering mode %u\n", ee.r8(0x506612), ee.r32(0x714880));
    Race r;
    std::string err;
    // The spec the program's builder (0x3B3998) made, as the car set-up (0x3C93A0) is handed it.
    std::vector<u8> their_spec;
    ee.probe(0x3C93A0, [&](Ee& e) {
        if (their_spec.empty()) {
            their_spec.resize(gt3r::sim::exact::kSpecSize);
            e.read_bytes(u32(e.a(1)), their_spec.data(), u32(their_spec.size()));
        }
    });
    if (!setup_race(ee, disc, sdb, course, {car}, 1, r, err)) {
        std::printf("prologue: %s\n", err.c_str());
        return 1;
    }
    std::printf("prologue: the race is set up (%s, %s); the mean slip switch %u, steering mode %u\n", course.c_str(), car.c_str(), ee.r8(0x506612), ee.r32(0x714880));
    // The parameter block the program's converter made, against the engine's converter (Gran Turismo 3's, 0x1DBFD8) on the
    // program's own spec: word by word, the program's block read at the engine's offset + 0, 24 or 44.
    if (!their_spec.empty()) {
        std::vector<u8> theirs(kParamsSize);
        ee.read_bytes(r.params(0), theirs.data(), kParamsSize);
        const sim::Program program = sim::Program::load(disc);
        const gt3r::sim::exact::ProgramData data = sim::core_program(program);
        gt3r::sim::exact::ConvertInputs in;
        in.data = &data, in.base = r.params(0), in.control = 0;
        in.physics_globals();
        gt3r::sim::exact::Spec spec;
        std::copy(their_spec.begin(), their_spec.begin() + spec.size(), spec.begin());
        gt3r::sim::exact::ParamImage mine{};
        gt3r::sim::exact::convert_params(spec, in, mine);
        int equal[3] = {}, words = 0;
        const u32 shifts[3] = {0, 24, 44};
        std::string runs;
        int last = -1, run_start = 0;
        auto flush = [&](int k, u32 at) {
            if (last >= 0) {
                char b[64];
                std::snprintf(b, sizeof b, "  %03X..%03X:%s", run_start, at, last == 3 ? "differ" : last == 0 ? "+0" : last == 1 ? "+24" : "+44");
                runs += b;
            }
            last = k, run_start = int(at);
        };
        for (u32 o = 0; o + 4 <= mine.size(); o += 4) {
            u32 a = 0;
            std::memcpy(&a, &mine[o], 4);
            if (a == 0) continue;
            ++words;
            int k = 3;
            for (int s = 0; s < 3 && k == 3; ++s) {
                if (o + shifts[s] + 4 > theirs.size()) continue;
                u32 b = 0;
                std::memcpy(&b, &theirs[o + shifts[s]], 4);
                // (equal, or within 8 units in the last place: the program's own rounding)
                const s64 ulps = s64(s32(a)) - s64(s32(b));
                if (a == b || (((a ^ b) & 0x80000000u) == 0 && (a & 0x7F800000u) != 0 && ulps >= -8 && ulps <= 8)) k = s, ++equal[s];
            }
            if (k != last) flush(k, o);
            if (std::getenv("GT4R_PARAMS") && k == 3) {
                u32 b = 0;
                std::memcpy(&b, &theirs[o], 4);
                float fa, fb;
                std::memcpy(&fa, &a, 4), std::memcpy(&fb, &b, 4);
                std::printf("    P+%03X  ours %08X (%g)  the program's %08X (%g)\n", o, a, double(fa), b, double(fb));
            }
        }
        flush(-1, u32(mine.size()));
        // With the tire records 0x1B8 bytes apart (4 bytes after each of the engine's 0x1B4), what follows them 24 on,
        // and from GT4R_P44 (hex) 44 on.
        const u32 x44 = std::getenv("GT4R_P44") ? u32(std::strtoul(std::getenv("GT4R_P44"), nullptr, 16)) : 0xE6C;
        int same = 0, close = 0, off = 0;
        for (u32 o = 0; o + 4 <= mine.size(); o += 4) {
            u32 q = o;
            if (o >= 0x398 && o < 0xDD0) q = 0x398 + 0x1B8 * ((o - 0x398) / 0x1B4) + (o - 0x398) % 0x1B4;
            else if (o >= 0xDD0) q = o + (o >= x44 ? 44 : 24);
            u32 a = 0, b = 0;
            std::memcpy(&a, &mine[o], 4), std::memcpy(&b, &theirs[q], 4);
            const s64 ulps = s64(s32(a)) - s64(s32(b));
            if (a == b) ++same;
            else if (((a ^ b) & 0x80000000u) == 0 && (a & 0x7F800000u) != 0 && ulps >= -8 && ulps <= 8) ++close;
            else {
                ++off;
                float fa, fb;
                std::memcpy(&fa, &a, 4), std::memcpy(&fb, &b, 4);
                if (std::getenv("GT4R_PARAMS2")) std::printf("    P+%03X (theirs +%03X)  ours %08X (%g)  the program's %08X (%g)\n", o, q, a, double(fa), b, double(fb));
            }
        }
        for (u32 g = 0; g < 8; ++g) {
            float gs;
            std::memcpy(&gs, &theirs[0x1F8 + 0x5C + 4 * g], 4);
            std::printf("  gear %u: %g rad/s per m/s = %.1f rpm per km/h\n", g, double(gs), double(gs) * 60 / 6.2831853 / 3.6);
        }
        for (u32 t = 0; t < 6; ++t) std::printf("  tire %u: +0x1B4 %02X %02X %02X %02X\n", t, theirs[0x398 + 0x1B8 * t + 0x1B4], theirs[0x398 + 0x1B8 * t + 0x1B5], theirs[0x398 + 0x1B8 * t + 0x1B6], theirs[0x398 + 0x1B8 * t + 0x1B7]);
        for (u32 o = 0xE84; o < 0xE98; o += 4) {
            float f;
            u32 w;
            std::memcpy(&f, &theirs[o], 4), std::memcpy(&w, &theirs[o], 4);
            std::printf("  P+%03X %08X %g\n", o, w, double(f));
        }
        std::printf("  P+20A..20B %02X %02X\n", theirs[0x20A], theirs[0x20B]);
        std::printf("  parameters mapped: %d words equal, %d within 8 units in the last place, %d differ\n", same, close, off);
        std::printf("  parameters: %d nonzero words of the engine's; equal at +0: %d, +24: %d, +44: %d\n %s\n", words, equal[0], equal[1], equal[2], runs.c_str());
    }
    const int dump_at = std::getenv("GT4R_DUMP") ? std::atoi(std::getenv("GT4R_DUMP")) : -1;
    auto dump = [&] {
        const u32 at = r.car(0);
        for (u32 off = 0; off < kCarSize; off += 4) {
            const u32 v = ee.r32(at + off);
            if (!v) continue;
            const float f = ee.rf(at + off);
            if (std::fabs(f) > 1e-6f && std::fabs(f) < 1e7f) std::printf("  +%04X %12.6f\n", off, double(f));
            else std::printf("  +%04X %08X\n", off, v);
        }
    };
    if (dump_at == 0) dump();
    // GT4R_TAPS: the engine's routines compared with Prologue's at every call (prologue_taps.h).
    const sim::Program taps_program = sim::Program::load(disc);
    const gt3r::sim::exact::ProgramData taps_data = sim::core_program(taps_program);
    const gt3r::sim::exact::RaceInputs inputs =
        gt3r::sim::exact::race_inputs(taps_data, gt3r::sim::exact::Conductor::SinglePlayer, gt3r::sim::exact::RaceConfig{}, false, 0);
    std::unique_ptr<PTaps> taps;
    if (std::getenv("GT4R_TAPS")) {
        taps = std::make_unique<PTaps>(ee, r, inputs);
        if (std::string(std::getenv("GT4R_TAPS")) == "prologue") add_prologue_taps(*taps, disc);
        else if (std::string(std::getenv("GT4R_TAPS")) == "input") add_input_taps(*taps, r);
        else add_engine_taps(*taps);
    }
    struct Sample {
        float x, y, z, v, rpm;
        int gear;
    };
    std::vector<Sample> theirs, ours;
    try {
        for (int i = 0; i < steps; ++i) {
            r.pad[0] = drive(i);
            step(ee, r);
            if (i + 1 == dump_at) dump();
            const u32 F = r.car(0) + 0xD8;
            theirs.push_back({ee.rf(F), ee.rf(F + 4), ee.rf(F + 8), ee.rf(F + 0x450), float(ee.r16(F + 0x494)), int(ee.r32(r.car(0) + 0x510))});
        }
    } catch (const std::exception& e) {
        std::printf("prologue: the step stopped: %s (pc %08X ra %08X)\n", e.what(), ee.cur_pc, ee.g32(31));
        return 1;
    }
    if (taps) taps->print();
    // Ours on the same course and car with the same pad, from the same first step (no hold).
    const std::vector<std::string> cars{car};
    std::unique_ptr<sim::Session> session = sim::Session::open(disc, course, cars);
    if (const char* rules = std::getenv("GT4R_PROLOGUE_RULES")) session->set_gt4_rules(std::string(rules) == "gt4");
    for (const std::string& n : session->notes()) std::printf("  ours: %s\n", n.c_str());
    if (!their_spec.empty()) {
        // The parameter block again with Prologue's converter's own rules (rules_prologue.h convert) on the engine's.
        std::vector<u8> theirs(kParamsSize);
        ee.read_bytes(r.params(0), theirs.data(), kParamsSize);
        const sim::Program program = sim::Program::load(disc);
        const gt3r::sim::exact::ProgramData data = sim::core_program(program);
        gt3r::sim::exact::ConvertInputs in;
        in.data = &data, in.base = r.params(0), in.control = 0;
        in.physics_globals();
        gt3r::sim::exact::Spec spec;
        std::copy(their_spec.begin(), their_spec.begin() + spec.size(), spec.begin());
        gt3r::sim::exact::ParamImage img{};
        gt3r::sim::exact::convert_params(spec, in, img);
        std::vector<u8> rec(gt3r::sim::exact::kCarSize, 0);
        std::copy(img.begin(), img.end(), rec.begin() + gt3r::sim::exact::kParams);
        gt3r::sim::exact::Car car;
        gt3r::sim::exact::load_image(car, rec.data());
        sim::prologue::convert(car.p, session->spec(0));
        gt3r::sim::exact::store_image(car, rec.data());
        int same = 0, close = 0, off = 0;
        for (u32 o = 0; o + 4 <= img.size(); o += 4) {
            if (o >= 0xE7C) break; // (the aids: the entries' switches)
            u32 q = o;
            if (o >= 0x398 && o < 0xDD0) q = 0x398 + 0x1B8 * ((o - 0x398) / 0x1B4) + (o - 0x398) % 0x1B4;
            else if (o >= 0xDD0) q = o + (o >= 0xE6C ? 44 : 24);
            u32 a = 0, b = 0;
            std::memcpy(&a, &rec[gt3r::sim::exact::kParams + o], 4), std::memcpy(&b, &theirs[q], 4);
            if (a >= 0x01000000u && a < 0x02000000u) continue; // (pointers into the block)
            const s64 ulps = s64(s32(a)) - s64(s32(b));
            if (a == b) ++same;
            else if (((a ^ b) & 0x80000000u) == 0 && (a & 0x7F800000u) != 0 && ulps >= -8 && ulps <= 8) ++close;
            else {
                ++off;
                float fa, fb;
                std::memcpy(&fa, &a, 4), std::memcpy(&fb, &b, 4);
                std::printf("    P+%03X  ours %08X (%g)  the program's %08X (%g)\n", o, a, double(fa), b, double(fb));
            }
        }
        std::printf("  parameters with Prologue's converter rules: %d words equal, %d within 8 units in the last place, %d differ\n", same, close, off);
    }
    {
        gt3r::sim::exact::Spec mine;
        sim::core_spec(session->spec(0), mine);
        int differ = 0;
        for (size_t k = 0; k < mine.size() && k < their_spec.size(); ++k)
            if (mine[k] != their_spec[k]) ++differ;
        std::printf("  spec (the core's layout): %d of %zu bytes differ\n", differ, mine.size());
        for (int k = 0; k < 6; ++k) {
            const u32 b = sim::Spec::tire_block(k);
            std::printf("  our spec's tire block %d kinds: %u %u %u\n", k, session->spec(0).b(b + 3), session->spec(0).b(b + 4), session->spec(0).b(b + 5));
        }
        if (std::getenv("GT4R_SPEC"))
            for (size_t k = 0; k < mine.size() && k < their_spec.size(); ++k)
                if (mine[k] != their_spec[k]) std::printf("    +%03zX  the program's %3u  ours %3u\n", k, their_spec[k], mine[k]);
    }
    for (int i = 0; i < steps; ++i) {
        const Pad p = drive(i);
        race::CarInput in;
        in.steer = p.steer, in.throttle = p.throttle, in.brake = p.brake;
        session->step_once({&in, 1});
        const race::CarState& c = session->cars()[0];
        ours.push_back({c.position.x, c.position.y, c.position.z, c.speed, c.engine_rpm, c.gear});
    }
    std::printf("prologue: %d steps of the program's own (and ours, GT4 rules %s): step, speed km/h, rpm, gear\n", steps, session->gt4_rules() ? "on" : "off");
    const int every = std::getenv("GT4R_EVERY") ? std::atoi(std::getenv("GT4R_EVERY")) : 30;
    for (int i = every - 1; i < steps; i += every) {
        const Sample& a = theirs[size_t(i)];
        const Sample& b = ours[size_t(i)];
        auto moved = [&](const std::vector<Sample>& v) {
            const Sample& p = v[size_t(i - every + 1 > 0 ? i - every : 0)];
            const Sample& q = v[size_t(i)];
            return std::sqrt(double((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y) + (q.z - p.z) * (q.z - p.z))) * 60.0 / every * 3.6;
        };
        std::printf("  %5d  %8.3f %8.3f  (moved %8.3f %8.3f)  %7.0f %7.0f   %08X %d\n", i + 1, double(a.v) * 3.6, double(b.v) * 3.6, moved(theirs), moved(ours),
                    double(a.rpm), double(b.rpm), a.gear, b.gear);
    }
    return 0;
}

} // namespace gt4r::diff::prologue
