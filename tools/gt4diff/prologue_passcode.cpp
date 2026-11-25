// gt4r_diff: Gran Turismo 4 Prologue's pass codes (docs/formats/prologue.md "The pass codes"): the program's 0x1A31A0 in
// the interpreter on fields made up here against ours (menu/prologue_passcode.h). Routines are named by address; nothing of
// the program is kept.

#include "prologue_race.h"

#include "gt4r/menu/prologue_passcode.h"

#include <cstdio>
#include <cstring>
#include <random>

namespace gt4r::diff::prologue {

bool run_passcode(Disc& disc, int count) {
    if (!is_prologue(disc)) {
        std::printf("not Prologue's disc\n");
        return false;
    }
    Ee ee;
    boot(ee, disc);
    std::array<u8, 8> a, b;
    std::memcpy(a.data(), ee.host_ptr(0x55CAD0, 8), 8);
    std::memcpy(b.data(), ee.host_ptr(0x55CAD8, 8), 8);
    const menu::PassCoder ours(a, b);
    const u32 fields = ee.alloc(0x30, 16), out = ee.alloc(0x40, 16);
    std::mt19937 rng(1234);
    int equal = 0;
    for (int n = 0; n < count; ++n) {
        menu::PassFields f;
        if (n > 0) {
            f.kind = rng() & 31, f.car = rng() & 511, f.course = rng() & 255, f.flag = rng() & 1, f.test = rng() & 4095;
            f.word14 = rng() & 4095, f.word18 = rng() & 7, f.time = rng() & 0x3FFFFF, f.word20 = rng() & 15, f.word24 = rng() & 15;
            if (n % 4 == 1) f.word14 = f.word18 = f.word20 = f.word24 = f.flag = 0, f.kind = 11;
        } else
            f.car = 5, f.course = 3, f.test = 1, f.time = 107500;
        const u32 v[10] = {f.kind, f.car, f.course, f.flag, f.test, f.word14, f.word18, f.time, f.word20, f.word24};
        for (int k = 0; k < 10; ++k) ee.w32(fields + 4 * u32(k), v[k]);
        ee.w32(fields + 0x28, 0);
        std::memset(ee.host_ptr(out, 0x40), 0, 0x40); // (its caller 0x19EC08 clears the 32 bytes)
        ee.call(0x1A31A0, {Arg::ptr(fields), Arg::ptr(out), Arg::integer(0x1E)});
        const std::string program(reinterpret_cast<const char*>(ee.host_ptr(out, 0x20)), strnlen(reinterpret_cast<const char*>(ee.host_ptr(out, 0x20)), 0x20));
        const std::string mine = ours.encode(f, ee.r32(0x5057C0));
        equal += program == mine;
        if (n < 4 || program != mine) std::printf("  %2d %s %s %s\n", n, program.c_str(), mine.c_str(), program == mine ? "equal" : "DIFFER");
    }
    std::printf("pass codes: %d of %d equal\n", equal, count);
    return equal == count;
}

} // namespace gt4r::diff::prologue
