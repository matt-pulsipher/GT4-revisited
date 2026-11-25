// The Replay Theater's demo replays (replay/Rdemo<n>/Rdemo<n>; floats.h).

#include "floats.h"
#include "orig.h"
#include "taps.h"

#include "gt4r/formats/replay/demonstration.h"
#include "gt4r/race/recorded_race.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/car_layout.h"
#include "gt4r/save/codec.h"
#include "gt4r/sim/program.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <utility>

namespace gt4r::diff {

namespace {

u32 get32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
void put32(u8* p, u32 v) { p[0] = u8(v), p[1] = u8(v >> 8), p[2] = u8(v >> 16), p[3] = u8(v >> 24); }
void swap4(u8* buf, size_t a, size_t b) {
    for (int k = 0; k < 4; ++k) std::swap(buf[a + size_t(k)], buf[b + size_t(k)]);
}
// The whole part of n x the single-precision constant (the console's toward-zero product is the exact one's).
u32 times(u32 n, u32 bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return u32(double(n) * double(f));
}

// The memory card's data file unscrambled (save.md) with its class's two swap factors: FileGT4Replay's (0x1CFFF8 hands
// 0x1D4E60 0x3F322311 for the first word and 0x3E6B3EF8 for the second; the game data's are codec.cpp's), key 0.
std::optional<Bytes> unscramble_replay(ByteSpan file) {
    const size_t size = file.size();
    if (size < 0x40 || size % 0x40) return std::nullopt;
    Bytes buf(file.begin(), file.end());
    swap4(buf.data(), 0, times(u32(size - 4), 0x3F322311));
    swap4(buf.data(), 4, 4 + times(u32(size - 8), 0x3E6B3EF8));
    const u32 random = get32(buf.data()), check = get32(buf.data() + 4);
    save::Mt19937 mt(check + random);
    save::Lcg lcg(check ^ random);
    for (size_t k = 8; k + 4 <= size; k += 4) {
        const u32 w = get32(buf.data() + k);
        const u32 a = mt.next(), b = lcg.next();
        put32(buf.data() + k, (w + b) ^ a);
    }
    if (save::crc32(ByteSpan(buf.data() + 8, size - 8)) != check) return std::nullopt;
    return Bytes(buf.begin() + 8, buf.end());
}

} // namespace

std::vector<std::string> theater_replays(Disc& disc) {
    std::vector<std::string> names;
    for (const std::string& n : disc.volume().list_dir("replay"))
        if (n.rfind("Rdemo", 0) == 0) names.push_back(n.back() == '/' ? n.substr(0, n.size() - 1) : n);
    return names;
}

std::optional<replay::Demonstration> theater_replay(Disc& disc, const sim::Program& program, const std::string& name, std::string* error) {
    const gt3r::Volume::Entry* e = disc.volume().find("replay/" + name + "/" + name);
    if (!e) {
        if (error) *error = "no replay " + name;
        return std::nullopt;
    }
    const auto payload = unscramble_replay(disc.volume().read(*e));
    if (!payload) {
        if (error) *error = "does not unscramble";
        return std::nullopt;
    }
    return replay::Demonstration::parse(*payload, program, error);
}

// Our own race of the recording (race::RecordedRace) beside the program's: every car after each step, the first steps that differ.
struct AloneBeside {
    race::RecordedRace race;
    int driver_lists = 0;
    int steps_listed = 0, first_bits = -1, first_apart = -1;
    double worst = 0;
    int worst_car = -1;
    AloneBeside(Disc& disc, db::SpecDb& sdb, sim::ai::DemoRun run) : race(disc, sdb, std::move(run)) {}
    void compare(Ee& ee, OrigRace& o, int step) {
        const int list = std::getenv("GT4R_FLOATS_OURS_LIST") ? std::atoi(std::getenv("GT4R_FLOATS_OURS_LIST")) : 0;
        bool any = false;
        for (int i = 0; i < o.cars; ++i) {
            sim::Record theirs, mine;
            theirs.car = mine.car = race.session().record(size_t(i));
            theirs.x = mine.x = race.session().gt4_record(size_t(i));
            theirs.layout = mine.layout = race.session().wheel_layout(size_t(i));
            load(theirs, read_image(ee, o.car(i)));
            const u32 F0 = o.car(i) + kF;
            const auto& c = mine.car;
            const double dx = double(c.pos.x.host()) - ee.rf(F0), dy = double(c.pos.y.host()) - ee.rf(F0 + 4), dz = double(c.pos.z.host()) - ee.rf(F0 + 8);
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > worst) worst = d, worst_car = i;
            if (d > 0.001 && first_apart < 0) first_apart = step, std::printf("  ours: car %d 1 mm apart after step %d\n", i, step);
            const bool same = c.pos.x.bits == ee.r32(F0) && c.pos.y.bits == ee.r32(F0 + 4) && c.pos.z.bits == ee.r32(F0 + 8);
            if (!same && first_bits < 0) first_bits = step, std::printf("  ours: car %d's place differs after step %d\n", i, step);
            if (steps_listed < list && step >= (std::getenv("GT4R_FLOATS_LIST_FROM") ? std::atoi(std::getenv("GT4R_FLOATS_LIST_FROM")) : 0))
                for (const Slot& sl : slots()) {
                    const u8* a = reinterpret_cast<const u8*>(&theirs) + sl.member;
                    const u8* b = reinterpret_cast<const u8*>(&mine) + sl.member;
                    if (std::memcmp(a, b, sl.size) == 0) continue;
                    // (display's and the drivers' words the session does not keep)
                    if (sl.name.rfind("input.line_", 0) == 0 || sl.name.rfind("input.driver_", 0) == 0 || sl.name.rfind("input.pace_", 0) == 0 || sl.name == "input.overtake" || sl.name == "input.steer_kind" ||
                        sl.name == "near.driver_line" || sl.name == "lap_base" || sl.name == "aids.fuel" || sl.name == "start.line" || sl.name == "phases.wrong_way" || sl.name == "aids.scale_on" || sl.name == "start.x6c4" || sl.name == "aids.tcs_class")
                        continue;
                    u32 x = 0, y = 0;
                    std::memcpy(&x, a, std::min<u32>(sl.size, 4)), std::memcpy(&y, b, std::min<u32>(sl.size, 4));
                    std::printf("  ours step %d car %d: %s program %08X ours %08X (%g %g)\n", step, i, sl.name.c_str(), x, y, double(gt3r::ee::F::raw(x).host()), double(gt3r::ee::F::raw(y).host()));
                    any = true;
                }
        }
        // The drivers' decisions (their objects' words), the first that part.
        if (race.field() && step >= (std::getenv("GT4R_FLOATS_LIST_FROM") ? std::atoi(std::getenv("GT4R_FLOATS_LIST_FROM")) : 0) && driver_lists < (std::getenv("GT4R_FLOATS_DRIVER") ? std::atoi(std::getenv("GT4R_FLOATS_DRIVER")) : 0))
            for (int i = 0; i < o.cars; ++i) {
                const u32 D = o.car(i) + sim::layout::kDriver, F0 = o.car(i) + kF;
                const sim::ai::Field::Probe p = race.field()->probe_get(size_t(i));
                std::string out;
                auto bits = [&](const char* what, u32 a, u32 b) {
                    char t[96];
                    if (a != b) std::snprintf(t, sizeof t, " %s %08X %08X (%g %g)", what, a, b, double(gt3r::ee::F::raw(a).host()), double(gt3r::ee::F::raw(b).host())), out += t;
                };
                auto num = [&](const char* what, int a, int b) {
                    char t[64];
                    if (a != b) std::snprintf(t, sizeof t, " %s %d %d", what, a, b), out += t;
                };
                const bool pad = ee.r8(F0 + 0x466) == 0 && ee.r8(F0 + 0x467) == 0;
                if (pad && !std::getenv("GT4R_FLOATS_SHADOW")) continue; // (a pad car's driver only plans)
                if (!pad) bits("steer", ee.r32(D + 0x30), p.steer.bits), bits("throttle", ee.r32(D + 0x34), p.throttle.bits), bits("brake", ee.r32(D + 0x38), p.brake.bits), num("mode", ee.r8(D + 0x1E), p.mode);
                num("arc ticks", ee.r16(D + 0x22), p.arc_ticks), num("status", gt3r::s8(ee.r8(D + 0x43)), p.status), num("cooldown", gt3r::s8(0) + s16(ee.r16(D + 0x18)), p.cooldown), num("state", ee.r8(D + 0x1B), p.state);
                num("follow", gt3r::s8(ee.r8(D + 0x51)), p.follow), bits("contact", ee.r32(D + 0x44), p.contact.bits), num("start flag", ee.r8(D + 0x1A), p.start_flag), num("stop count", ee.r8(D + 0x52), p.stop_count), num("kind", ee.r8(F0 + 0x467), p.kind);
                num("rand", ee.r16(D + 0x68), p.rand);
                if (ee.r8(F0 + 0x466) != 0) bits("corner", ee.r32(D + 0xB8), p.corner_mult.bits), bits("braking", ee.r32(D + 0xBC), p.brake_mult.bits);
                bits("gap left", ee.r32(D + 0x58), p.gap_left.bits), bits("gap right", ee.r32(D + 0x5C), p.gap_right.bits);
                for (u32 k = 0; k < 8; ++k) num(("zone" + std::to_string(k)).c_str(), ee.r8(D + 0x48 + k), p.zone[k]);
                num("next", ee.r8(D + 0x1D), p.next), num("line", ee.r8(D + 0x1C), p.line), num("prev", ee.r8(D + 0x21), p.prev_line);
                bits("blend", ee.r32(D + 0x2C), p.blend.bits), bits("offset", ee.r32(D + 0x54), p.offset.bits), bits("v_cur", ee.r32(D + 0x24), p.v_cur.bits), bits("v_next", ee.r32(D + 0x28), p.v_next.bits);
                if (!pad) bits("aim x", ee.r32(F0 + 0x61C), p.aim_x.bits), bits("aim y", ee.r32(F0 + 0x620), p.aim_y.bits);
                if (!out.empty()) std::printf("  driver %d step %d (program ours, mode %d):%s\n", i, step, ee.r8(D + 0x1E), out.c_str()), ++driver_lists;
            }
        if (any) ++steps_listed;
    }
};

CipherResult run_floats_cipher(Disc& disc, bool print) {
    CipherResult r;
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc, sdb);
    auto one = [&](const std::string& name, ByteSpan file) {
        if (file.size() < 0x50) return;
        const u32 head = get32(file.data()), n = get32(file.data() + 4), key = get32(file.data() + 0xC);
        if (head + n > file.size()) return;
        ++r.files;
        const u32 src = ee.alloc_bytes(file.subspan(head, n), 16), dst = ee.alloc(n + 16, 16);
        const bool ok = ee.call(0x579E00, {Arg::ptr(dst), Arg::ptr(src), Arg::integer(s64(n)), Arg::integer(s64(s32(key)))}, 500'000'000).v0_s32() != 0;
        r.passed += ok ? 1 : 0;
        r.bytes += n;
        if (!ok && print && r.files - r.passed <= 5) std::printf("  %s: the key check fails\n", name.c_str());
        ee.free_all();
        boot(ee, disc, sdb);
    };
    const gt3r::Volume& vol = disc.volume();
    for (const std::string& n : vol.list_dir("replay/license")) {
        const gt3r::Volume::Entry* e = vol.find("replay/license/" + n);
        if (e && n.size() >= 9 && n.size() <= 10) one(n, vol.read(*e));
    }
    const size_t license = r.files, license_ok = r.passed;
    for (const std::string& n : theater_replays(disc)) {
        const gt3r::Volume::Entry* e = vol.find("replay/" + n + "/" + n);
        const auto payload = e ? unscramble_replay(vol.read(*e)) : std::nullopt;
        if (payload) one(n, *payload);
    }
    if (print)
        std::printf("cipher (0x579E00): %zu of %zu license files and %zu of %zu demo replays pass the key check\n", license_ok, license, r.passed - license_ok, r.files - license);
    return r;
}

int run_floats_replays(Disc& disc, const std::vector<std::string>& args) {
    const gt3r::Volume& vol = disc.volume();
    const sim::Program program = sim::Program::load(disc);
    for (const std::string& n : theater_replays(disc)) {
        if (!args.empty() && args[0] != n) continue;
        const gt3r::Volume::Entry* e = vol.find("replay/" + n + "/" + n);
        if (!e) continue;
        const Bytes file = vol.read(*e);
        std::string err;
        const auto d = theater_replay(disc, program, n, &err);
        if (!d) {
            std::printf("%-10s %zu bytes: %s\n", n.c_str(), file.size(), err.c_str());
            continue;
        }
        std::printf("%-10s %zu bytes: %s, mode %u, time %d ms (%d), codes %llx %llx, %zu entries, %zu streams, frames", n.c_str(), file.size(),
                    d->race_class.c_str(), d->race_mode, d->time_ms, d->time2_ms, (unsigned long long)d->code_b0, (unsigned long long)d->code_b8, d->entries.size(),
                    d->streams.size());
        for (const replay::ControlStream& s : d->streams) std::printf(" %zu", s.frames.size());
        std::printf(" kinds");
        for (const replay::ControlStream& s : d->streams) std::printf(" %u", s.frames.empty() ? 0u : unsigned(s.frames.back().kind()));
        for (const replay::DemoEntry& en : d->entries)
            std::printf(" [%u grid %d kind %u aids %u%u%u%u]", unsigned(u32(u64(en.equipment.part(sim::Equipment::Car)))), en.grid_place, en.driver_kind, en.aids[0], en.aids[1],
                        en.aids[2], en.aids[3]);
        std::printf("\n");
        if (args.size() > 1 && args[1] == "frames") { // the first records that change
            const replay::PadRecord* last = nullptr;
            int shown = 0;
            for (size_t i = 0; i < d->streams[0].frames.size() && shown < 60; ++i) {
                const replay::PadRecord& r = d->streams[0].frames[i];
                if (last && *last == r) continue;
                std::printf("  %5zu throttle %4d brake %4d steer %5d shift %d\n", i, r.throttle(), r.brake(), r.steer(), r.shift());
                last = &r, ++shown;
            }
        }
        if (args.size() > 1 && args[1] == "run") {
            FloatDemoOptions opt;
            if (const char* e = std::getenv("GT4R_FLOATS_HOLD")) opt.hold = std::atoi(e);
            if (const char* e = std::getenv("GT4R_FLOATS_EXTRA")) opt.extra = std::atoi(e);
            opt.verbose = std::getenv("GT4R_FLOATS_VERBOSE") != nullptr;
            opt.ours = std::getenv("GT4R_FLOATS_OURS") != nullptr;
            // GT4R_FLOATS_ALONE=1: our own race of it beside the program's (2: ours alone, no program).
            std::unique_ptr<AloneBeside> alone;
            db::SpecDb sdb = open_db(disc);
            if (const char* e = std::getenv("GT4R_FLOATS_ALONE")) {
                sim::ai::DemoRun run;
                run.label = n, run.demo = *d;
                for (const replay::PadRecord& f : run.demo.streams[0].frames) run.pads.push_back({f.bytes});
                alone = std::make_unique<AloneBeside>(disc, sdb, std::move(run));
                opt.on_step = [&](Ee& ee, OrigRace& o, int step) {
                    alone->race.step();
                    alone->compare(ee, o, step);
                };
                if (std::atoi(e) == 2) {
                    while (!alone->race.over()) alone->race.step();
                    std::printf("%s ours alone: laps", n.c_str());
                    for (const s64 ms : alone->race.laps()) std::printf(" %lld", (long long)ms);
                    std::printf(" total %lld (recorded %d, best %d): %s\n", (long long)alone->race.total_ms(), d->time_ms, d->time2_ms, alone->race.exact() ? "EXACT" : "not as recorded");
                    continue;
                }
            }
            const DemoScore s = run_recording(disc, *d, n, opt, true);
            if (alone) {
                std::printf("%s ours alone: first bit apart at step %d, 1 mm at %d, worst %.6f m (car %d); laps", n.c_str(), alone->first_bits, alone->first_apart, alone->worst, alone->worst_car);
                for (const s64 ms : alone->race.laps()) std::printf(" %lld", (long long)ms);
                std::printf(": %s\n", alone->race.exact() ? "EXACT" : "not as recorded");
            }
            if (!s.ran) std::printf("%s: %s\n", n.c_str(), s.error.c_str());
            if (s.ours_ran)
                std::printf("%s ours: first bit apart at step %d, 1 mm apart at %d, worst %.6f m\n", n.c_str(), s.ours_first_bits, s.ours_first_apart, s.ours_worst);
        }
    }
    return 0;
}

} // namespace gt4r::diff
