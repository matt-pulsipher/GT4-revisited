// The Replay Theater's demo replays (replay/Rdemo<n>/Rdemo<n>; floats.h).

#include "floats.h"
#include "orig.h"

#include "gt4r/formats/replay/demonstration.h"
#include "gt4r/save/codec.h"
#include "gt4r/sim/program.h"

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
        if (!args.empty() && args.size() > 1 && args[1] == "run") {
            FloatDemoOptions opt;
            if (const char* e = std::getenv("GT4R_FLOATS_HOLD")) opt.hold = std::atoi(e);
            if (const char* e = std::getenv("GT4R_FLOATS_EXTRA")) opt.extra = std::atoi(e);
            opt.verbose = std::getenv("GT4R_FLOATS_VERBOSE") != nullptr;
            opt.ours = std::getenv("GT4R_FLOATS_OURS") != nullptr;
            const DemoScore s = run_recording(disc, *d, n, opt, true);
            if (!s.ran) std::printf("%s: %s\n", n.c_str(), s.error.c_str());
            if (s.ours_ran)
                std::printf("%s ours: first bit apart at step %d, 1 mm apart at %d, worst %.6f m\n", n.c_str(), s.ours_first_bits, s.ours_first_apart, s.ours_worst);
        }
    }
    return 0;
}

} // namespace gt4r::diff
