// gt4r_diff: Gran Turismo 4 Prologue's game data as its own program writes it (docs/formats/prologue.md "The saved game"):
// a Status made by its constructor (0x401E90), its size (0x402150) and its save (0x4021F0), the parts found where the
// layout puts them (the name, a license unit, the options), and the check (0x4021A8) on the bytes. Routines are named
// by address; nothing of the program is kept.

#include "prologue_race.h"

#include <cstdio>
#include <cstring>

namespace gt4r::diff::prologue {

bool run_save_layout(Disc& disc) {
    if (!is_prologue(disc)) {
        std::printf("not Prologue's disc\n");
        return false;
    }
    Ee ee;
    boot(ee, disc);
    // The kernel's thread and semaphore calls the constructors make (a lock each): answered here.
    for (const u32 call : {0x4BFF10u, 0x4BFF40u, 0x4BFF50u, 0x4C0020u, 0x4C0030u, 0x4C0040u, 0x4C0050u, 0x4C0060u, 0x4C0070u, 0x4C02A0u, 0x4C02F0u, 0x4C2848u, 0x4C00D0u, 0x4C0080u, 0x4BFED0u, 0x4BFFA0u, 0x4BFE70u, 0x4C0330u, 0x4C0360u, 0x4C0390u})
        ee.hle(call, [](Ee& e) { e.set_v0(1); }, "kernel");
    const u32 status = ee.alloc(0x40000, 64);
    ee.call(0x401E90, {Arg::ptr(status)});
    const u32 size = u32(ee.call(0x402150, {Arg::ptr(status)}).v0_s32());
    // Marks: the profile's name (UserProfile +0x1133C), license unit 0's first word (license object +8 = UserProfile +0x11358)
    // and unit 47's, the options' first word (Option +8 = Status +0x2F518).
    const u32 profile = status + 0x230;
    std::memcpy(ee.host_ptr(profile + 0x1133C, 16), "GT4R-NAME-MARK!", 16);
    ee.w32(profile + 0x11358, 0xA1A1A1A1), ee.w32(profile + 0x11358 + 47 * 0x2E8, 0xA4A4A4A4);
    ee.w32(profile + 0x19ED8 + 8 + 63 * 0x550, 0xC3C3C3C3);
    ee.w32(status + 0x2F518, 0xB2B2B2B2);
    const u32 buf = ee.alloc(0x40000, 64);
    std::memset(ee.host_ptr(buf, 0x40000), 0, 0x40000);
    ee.call(0x4021F0, {Arg::ptr(status), Arg::ptr(buf)});
    const u32 length = u32(-s32(ee.r32(buf + 4))) + 0x10;
    auto find = [&](u32 word) -> s64 {
        for (u32 o = 0; o + 4 <= length; o += 4)
            if (ee.r32(buf + o) == word) return o;
        return -1;
    };
    s64 name = -1;
    for (u32 o = 0; o + 16 <= length; ++o)
        if (std::memcmp(ee.host_ptr(buf + o, 16), "GT4R-NAME-MARK!", 16) == 0) name = o;
    const s32 ok = ee.call(0x4021A8, {Arg::ptr(status), Arg::ptr(buf)}).v0_s32();
    std::printf("Prologue game data: magic %08X, size (0x402150) 0x%X, written 0x%X, check %d\n", ee.r32(buf), size, length, ok);
    std::printf("  name at 0x%llX, license unit 0 at 0x%llX, unit 47 at 0x%llX, course unit 63 +8 at 0x%llX, options at 0x%llX\n", (long long)name,
                (long long)find(0xA1A1A1A1), (long long)find(0xA4A4A4A4), (long long)find(0xC3C3C3C3), (long long)find(0xB2B2B2B2));
    return true;
}

} // namespace gt4r::diff::prologue
