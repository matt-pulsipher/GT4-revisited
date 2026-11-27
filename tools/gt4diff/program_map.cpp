// The running program's addresses (program_map.h).

#include "program_map.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace gt4r::diff {

namespace {

sim::Release g_release = sim::Release::Us;

struct Pair {
    u32 us, jp;
};
// v1.01 -> SCPS-17001 v1.05, sorted by the v1.01 address. Rows marked `check` were placed positionally inside a routine
// whose instructions differ there, or from the nearest data pair.
constexpr Pair kJapan[] = {
#include "program_map_jp.inc"
};

} // namespace

void set_release(sim::Release r) { g_release = r; }
sim::Release running_release() { return g_release; }

u32 pa_or_0(u32 us) {
    if (g_release != sim::Release::Japan) return us;
    // The harness's stand-ins' entry points (two places of the program's first routine nothing of a race runs) are the same.
    if (us == 0x100000 || us == 0x100004) return us;
    const auto it = std::lower_bound(std::begin(kJapan), std::end(kJapan), us, [](const Pair& p, u32 a) { return p.us < a; });
    return it != std::end(kJapan) && it->us == us ? it->jp : 0;
}

u32 pa(u32 us) {
    const u32 a = pa_or_0(us);
    if (!a) {
        char b[96];
        std::snprintf(b, sizeof b, "v1.01 address 0x%06X has no counterpart in %s's program", us, sim::release_name(g_release));
        throw std::runtime_error(b);
    }
    return a;
}

u32 cond_off(u32 us) {
    // SCPS-17001 v1.05's conductor is 0x18 bytes shorter from +0x10024 on (its table at +0x10128: every virtual call of
    // its routines reads it there).
    if (g_release == sim::Release::Japan && us >= 0x10024) return us - 0x18;
    return us;
}

Segments segments() {
    if (g_release == sim::Release::Japan) return {0x60EFD4, 0x6CC100, 0x89991C, 0x6D40F0};
    return {0x617A14, 0x6D5E00, 0x8A215C, 0x6DDDF0};
}

} // namespace gt4r::diff
