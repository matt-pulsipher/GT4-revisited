#include "save_check.h"

#include "orig.h"
#include "prologue_race.h"

#include "gt4r/save/card_image.h"
#include "gt4r/save/codec.h"
#include "gt4r/save/game_data.h"
#include "gt4r/save/prologue_data.h"
#include "gt4r/save/save_edit.h"

#include <cstdio>
#include <cstring>

namespace gt4r::diff {

namespace {
u32 get32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
} // namespace

SaveCheck check_save(Disc& disc, const Bytes& game, const Bytes& garage, const std::vector<int>& slots) {
    SaveCheck r;
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc, sdb);
    r.ran = true;
    ee.hle(0x5B8450, [](Ee& e) { e.set_v0(12345); }, "clock"); // (the console's timer, which the constructors read)
    // The kernel calls (a wrapper of 16 bytes each) the constructors make for their locks.
    for (u32 call = 0x5AD880; call < 0x5ADF70; call += 16) ee.hle(call, [](Ee& e) { e.set_v0(1); }, "kernel");
    // The game data file: deciphered in place (0x1D4E60: the buffer, its size, the key, no second seed, the swaps' factors);
    // it gives the payload's size, -1 when the CRC is wrong.
    const u32 file = ee.alloc(u32(game.size()) + 64, 64);
    std::memcpy(ee.host_ptr(file, u32(game.size())), game.data(), game.size());
    float f12, f13;
    const u32 b12 = 0x3E0B0C88, b13 = 0x3F27A4E7;
    std::memcpy(&f12, &b12, 4), std::memcpy(&f13, &b13, 4);
    const s32 n = ee.call(0x1D4E60, {Arg::ptr(file), Arg::integer(s32(game.size())), Arg::integer(0), Arg::integer(0), Arg::flt(f12), Arg::flt(f13)}).v0_s32();
    r.decoded = n > 0;
    if (!r.decoded) return r;
    // (The caller, 0x1CDAC8, moves the payload to the buffer's start.)
    const u32 payload = ee.alloc(save::kPayloadSize + 64, 64), back = ee.alloc(save::kPayloadSize + 64, 64);
    std::memcpy(ee.host_ptr(payload, save::kPayloadSize), ee.host_ptr(file + 8, save::kPayloadSize), save::kPayloadSize);
    const u32 status = ee.alloc(0x50000, 64);
    ee.call(0x43A528, {Arg::ptr(status)});
    r.valid = ee.call(0x43AA68, {Arg::ptr(status), Arg::ptr(payload)}).v0_s32() != 0;
    if (!r.valid) return r;
    ee.call(0x43ABA0, {Arg::ptr(status), Arg::ptr(payload)});
    std::memset(ee.host_ptr(back, save::kPayloadSize), 0, save::kPayloadSize);
    ee.call(0x43AAF0, {Arg::ptr(status), Arg::ptr(back)});
    const u8* a = ee.host_ptr(payload, save::kPayloadSize);
    const u8* b = ee.host_ptr(back, save::kPayloadSize);
    for (size_t i = 0; i < save::kPayloadSize; ++i)
        if (a[i] != b[i] && !r.differ++) r.first = i;
    // The garage's header (0x1CB9B8) against the game's key and serial.
    const u32 key = get32(a + save::kGarageKeyAt), serial = get32(a + save::kGarageSerialAt);
    if (garage.size() < 0x40) return r;
    const u32 header = ee.alloc(0x40, 64);
    std::memcpy(ee.host_ptr(header, 0x40), garage.data(), 0x40);
    ee.call(0x1CB9B8, {Arg::ptr(header), Arg::integer(s32(key))});
    const u8* h = ee.host_ptr(header, 0x40);
    r.garage = get32(h + 0x38) == key && (get32(h + 0x2C) == serial || get32(h + 0x30) == serial);
    r.money = s64(u64(get32(h)) << 32 | get32(h + 4));
    // The slots: 0x1CEEC0's steps on each.
    const u32 slot = ee.alloc(save::kSlotSize, 64), mt = ee.alloc(0x9D0, 64), lcg = ee.alloc(16, 64);
    for (int i : slots) {
        const size_t at = 0x40 + size_t(i) * save::kSlotSize;
        bool good = garage.size() >= at + save::kSlotSize;
        if (good) {
            std::memcpy(ee.host_ptr(slot, save::kSlotSize), garage.data() + at, save::kSlotSize);
            ee.call(0x579420, {Arg::ptr(mt), Arg::integer(s32(key + u32(i)))});
            ee.call(0x1D4BA0, {Arg::ptr(slot), Arg::integer(s32(save::kSlotSize)), Arg::ptr(mt)});
            const u32 t = ee.r32(slot), crc = ee.r32(slot + 4);
            ee.call(0x579420, {Arg::ptr(mt), Arg::integer(s32(key ^ t))});
            ee.call(0x5792E8, {Arg::ptr(lcg), Arg::integer(s32(crc))});
            ee.call(0x5D2750, {Arg::ptr(slot + 8), Arg::integer(s32(save::kSlotSize - 8)), Arg::ptr(mt), Arg::ptr(lcg)});
            good = u32(ee.call(0x572C90, {Arg::ptr(slot + 8), Arg::integer(s32(save::kSlotChecked))}).v0_s32()) == crc;
        }
        if (!good) r.slots_bad.push_back(i);
    }
    return r;
}

SaveCheck check_prologue_save(Disc& disc, const Bytes& game) {
    SaveCheck r;
    if (!prologue::is_prologue(disc) || game.size() < save::kProloguePayloadSize) return r;
    Ee ee;
    prologue::boot(ee, disc);
    // (The kernel's thread and semaphore calls the constructors make: prologue_save.cpp.)
    for (const u32 call : {0x4BFF10u, 0x4BFF40u, 0x4BFF50u, 0x4C0020u, 0x4C0030u, 0x4C0040u, 0x4C0050u, 0x4C0060u, 0x4C0070u, 0x4C02A0u, 0x4C02F0u, 0x4C2848u, 0x4C00D0u, 0x4C0080u, 0x4BFED0u, 0x4BFFA0u, 0x4BFE70u, 0x4C0330u, 0x4C0360u, 0x4C0390u})
        ee.hle(call, [](Ee& e) { e.set_v0(1); }, "kernel");
    r.ran = r.decoded = r.garage = true;
    const u32 status = ee.alloc(0x40000, 64), in = ee.alloc(0x40000, 64), out = ee.alloc(0x40000, 64);
    ee.call(0x401E90, {Arg::ptr(status)});
    std::memset(ee.host_ptr(out, 0x40000), 0, 0x40000);
    std::memcpy(ee.host_ptr(in, u32(game.size())), game.data(), game.size());
    r.valid = ee.call(0x4021A8, {Arg::ptr(status), Arg::ptr(in)}).v0_s32() != 0;
    if (!r.valid) return r;
    ee.call(0x402288, {Arg::ptr(status), Arg::ptr(in)});
    ee.call(0x4021F0, {Arg::ptr(status), Arg::ptr(out)});
    const u8* a = ee.host_ptr(in, save::kProloguePayloadSize);
    const u8* b = ee.host_ptr(out, save::kProloguePayloadSize);
    for (size_t i = 0; i < save::kProloguePayloadSize; ++i)
        if (a[i] != b[i] && !r.differ++) r.first = i;
    return r;
}

int run_save_check(Disc& disc, const std::string& path) {
    const bool prologue = prologue::is_prologue(disc);
    const std::string product = prologue ? "BISCPS-15055" : "BASCUS-97328";
    std::string why;
    const std::optional<save::EditPlace> place = save::edit_place(path, product, &why);
    const std::optional<save::EditFiles> files = place ? save::read_files(*place, product, &why) : std::nullopt;
    if (!files) {
        std::printf("%s\n", why.c_str());
        return 2;
    }
    std::vector<int> slots;
    if (!prologue)
        if (const std::optional<Bytes> payload = save::unscramble(files->game, 0)) {
            // The places whose cars have a record and are not the car driven.
            const s32 riding = s32(get32(payload->data() + 0x135E0));
            for (int i = 0; i < 1000; ++i)
                if (const u32 flags = get32(payload->data() + 0xB8E0 + 0x20 * size_t(i) + 8); (flags & 3) == 3 && i != riding) slots.push_back(i);
        }
    const SaveCheck r = prologue ? check_prologue_save(disc, files->game) : check_save(disc, files->game, files->garage, slots);
    std::printf("the program's reading: file %s, payload %s, written again %zu bytes differ", r.decoded ? "deciphered" : "refused", r.valid ? "valid" : "refused", r.differ);
    if (r.differ) std::printf(" (the first at 0x%zX)", r.first);
    if (!prologue) std::printf(", garage %s, money %lld, %zu of %zu slots whole", r.garage ? "the game's" : "another's", static_cast<long long>(r.money), slots.size() - r.slots_bad.size(), slots.size());
    std::printf("\n%s\n", r.ok() ? "ok" : "NOT ok");
    return r.ok() ? 0 : 1;
}

} // namespace gt4r::diff
