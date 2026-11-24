// gt4r_diff's checks of leaf routines: the program's C library floats against the shared engine's (the
// routines every rule of the step is built on).

#include "diff.h"
#include "orig.h"

#include "base/ee_math.h"
#include "sim/exact/build_math.h"

#include <cstdio>
#include <random>

namespace gt4r::diff {

namespace {
constexpr u32 kSin = 0x57D2B8, kCos = 0x57D1F0, kTan = 0x57D380, kAtan2 = 0x57D118, kSinCos = 0x57D8A0;
}

LeavesResult run_leaves(Disc& disc, bool print) {
    using gt3r::ee::F;
    LeavesResult result;
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc, sdb);
    std::mt19937 rng(4);
    auto angle = [&] {
        const u32 k = rng() % 8;
        const float span = k == 0 ? 0.01f : k == 1 ? 100.0f : k == 2 ? 1e-4f : 6.5f;
        return F((float(rng() % 2000001) / 1000000.0f - 1.0f) * span);
    };
    auto call1 = [&](u32 fn, F x) { return ee.call(fn, {Arg::fbits(x.bits)}).f0; };
    const int n = 20000;
    const std::vector<u8> zeros(8, 0);
    const u32 out = ee.alloc_bytes(zeros, 16);
    for (int i = 0; i < n; ++i) {
        const F x = angle();
        result.sin_off += call1(kSin, x) != gt3r::ee::sin(x).bits;
        result.cos_off += call1(kCos, x) != gt3r::ee::cos(x).bits;
        result.tan_off += call1(kTan, x) != gt3r::ee::tan(x).bits;
        // sincosf (x, &sin, &cos)
        ee.call(kSinCos, {Arg::fbits(x.bits), Arg::ptr(out), Arg::ptr(out + 4)});
        F s, c;
        gt3r::ee::sincos(x, s, c);
        result.sincos_off += ee.r32(out) != s.bits || ee.r32(out + 4) != c.bits;
    }
    // atan2f: which build's (the engine has Gran Turismo 3's, the later builds' and 2002 Tokyo-Geneva's).
    const gt3r::sim::Program builds[] = {gt3r::sim::Program::NtscU, gt3r::sim::Program::ConceptTokyo, gt3r::sim::Program::ConceptGenevaEu};
    for (int i = 0; i < n; ++i) {
        F y = angle(), x = angle();
        if (i % 50 == 0) x = -y;
        if (i % 77 == 0) x = F();
        if (i % 91 == 0) y = F();
        const u32 theirs = ee.call(kAtan2, {Arg::fbits(y.bits), Arg::fbits(x.bits)}).f0;
        for (size_t b = 0; b < 3; ++b) result.atan2_off[b] += theirs != gt3r::sim::exact::atan2(gt3r::sim::release_behaviour(builds[b]), y, x).bits;
    }
    result.samples = size_t(n);
    result.ran = true;
    if (print)
        std::printf("leaves: of %d samples each, sin differs on %zu, cos %zu, tan %zu, sincos %zu; atan2 from Gran Turismo 3's on %zu, the later builds' %zu, 2002 "
                    "Tokyo-Geneva's %zu\n",
                    n, result.sin_off, result.cos_off, result.tan_off, result.sincos_off, result.atan2_off[0], result.atan2_off[1], result.atan2_off[2]);
    return result;
}

} // namespace gt4r::diff
