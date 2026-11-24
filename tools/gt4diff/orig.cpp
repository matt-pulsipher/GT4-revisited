// The user's own program set up in the interpreter (orig.h).

#include "orig.h"

#include "disc/core_image.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/db/car_parts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// The interpreter's library stubs name Gran Turismo 3 addresses through this; they are not used here.
namespace gt3r::diff {
u32 pa(u32 a) { return a; }
} // namespace gt3r::diff

namespace gt4r::diff {

namespace {

constexpr u32 kTableCtor = 0x448F18;                        // a table object (0x6C bytes)
constexpr u32 kTableOpen = 0x4490F8;                        // (table, file image): the object's pointers into the image
constexpr u32 kCacheFind = 0x44AB20, kCacheKeep = 0x44A908; // the row cache (left out: every row is unpacked)
constexpr u32 kMemset = 0x5A48D8;                           // (to, byte, count)
constexpr u32 kRunwayRelocate = 0x3E5818;                   // (road): the file's offsets into addresses
constexpr u32 kConductorCtor = 0x3474C0;                    // DynamicsConductorSinglePlayer
constexpr u32 kCourseTable = 0x22;
constexpr u32 kStep = 0x34DB30;      // (conductor, first car)
// Two places in the program's code that nothing of a race runs, taken for the stand-ins' entry points.
constexpr u32 kStubZero = 0x100000, kStubPad = 0x100004;

} // namespace

db::SpecDb open_db(Disc& disc) {
    gt3r::Volume* vol = &disc.volume();
    return db::SpecDb(
        [vol](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
}

void boot(Ee& ee, Disc& disc, db::SpecDb& sdb) {
    const gt3r::CoreImage core = gt3r::CoreImage::decode(disc.read_program());
    ee.load_program(core.program, core.load_address, kTextEnd, kBssStart, kBssEnd, kGp);
    ee.hle(kCacheFind, [](Ee& e) { e.set_v0(0); }, "row cache find");
    ee.hle(kCacheKeep, [](Ee&) {}, "row cache keep");
    ee.hle(
        kMemset,
        [](Ee& e) {
            const u32 to = u32(e.a(0)), n = u32(e.a(2));
            if (n) std::memset(e.host_ptr(to, n), int(e.a(1)), n);
            e.set_v0(to);
        },
        "memset");
    const char* const more[] = {"COURSE", "RACE", "DEFAULT_PARTS", "DEFAULT_PARAM", "ENEMY_CARS", "CAR_NAME_american", "COURSE_NAME_american",
                                "CAR_VARIATION_american", "VARIATIONamerican"};
    for (u32 t = 0; t < 43; ++t) {
        const char* name = db::part_table_name(t);
        if (!name) name = more[t - 34];
        const gt3r::Volume::Entry* e = disc.volume().find(sdb.folder() + name + ".dbt");
        if (!e) continue;
        const Bytes file = disc.volume().read(*e);
        const u32 image = ee.alloc_bytes(file), table = ee.alloc(0x6C);
        ee.call(kTableCtor, {Arg::ptr(table)});
        ee.call(kTableOpen, {Arg::ptr(table), Arg::ptr(image)});
        ee.w32(kDatabase + 0x134 + 4 * t, table);
    }
}

bool setup_race(Ee& ee, Disc& disc, db::SpecDb& sdb, const std::string& course, const std::vector<std::string>& labels, int human, OrigRace& o,
                std::string& err, bool drivers, const std::vector<std::array<u8, 0x178>>* equipment) {
    o = OrigRace{};
    o.cars = int(labels.size());
    try {
        // The road, its offsets made addresses by the program's own loader; the course object's part the race reads.
        const gt3r::Volume::Entry* file = disc.volume().find("crs/" + course);
        if (!file) return err = "no course " + course, false;
        const Bytes data = disc.volume().read(*file);
        const course::CourseFile parsed = course::CourseFile::parse(data);
        const ByteSpan road = parsed.section(data, course::kSlotRunway);
        if (road.empty()) return err = "the course has no road", false;
        o.runway = ee.alloc_bytes(road, 64);
        ee.call(kRunwayRelocate, {Arg::ptr(o.runway)}, 50'000'000);
        o.container = ee.alloc(0x1600, 64);
        ee.w32(o.container + 4 * u32(course::kSlotRunway), o.runway);
        o.course = ee.alloc(0x200, 64);
        ee.w32(o.course + 4, o.container);
        ee.w32(o.course + 0xB0, 0xFFFFFFFF); // no layout of the course's sections: the road's own grid
        if (drivers) { // the driver file: its lines' section at +0x9C, the four driver sets at +0xA0 ..
            const gt3r::Volume::Entry* ad = disc.volume().find("crs/" + course + ".ad");
            if (!ad) return err = "no driver file crs/" + course + ".ad", false;
            const Bytes f = disc.volume().read(*ad);
            const u32 at = ee.alloc_bytes(f, 64);
            auto slot = [&](u32 k) { return f.size() >= 4 * (k + 1) ? rd32(f, 4 * k) : 0u; };
            if (slot(1)) ee.w32(o.course + 0x9C, at + slot(1));
            for (u32 k = 0; k < 4; ++k)
                if (slot(2 + k)) ee.w32(o.course + 0xA0 + 4 * k, at + slot(2 + k));
        }

        u32 course_id = 0;
        for (const db::CourseEntry& c : sdb.courses())
            if (c.label == course) course_id = c.id;
        // The race information: the entries' list, the course's row and object.
        o.info = ee.alloc(0x2000, 64);
        const u32 list = ee.alloc(0x20), arr = ee.alloc(4 * 8);
        ee.w32(list, u32(o.cars)), ee.w32(list + 8, arr);
        ee.w32(o.info + 0x60, list);
        ee.w64(o.info + 0x78, (u64(kCourseTable) << 32) | course_id);
        ee.w32(o.info + 0x80, o.course);
        // The race object the race information points at (+0x84): its virtuals answer 0 (no view options, no pause).
        ee.hle(kStubZero, [](Ee& e) { e.set_v0(0), e.fpr[0] = 0; }, "race object virtual");
        const u32 race = ee.alloc(0x1000, 64), race_vt = ee.alloc(0x800, 16);
        for (u32 k = 0; k < 0x800; k += 8) ee.w32(race_vt + k + 4, kStubZero);
        ee.w32(race + 0x64, race_vt);
        ee.w32(o.info + 0x84, race);
        ee.hle(0x3BFD00, [](Ee& e) { e.set_v0(0); }, "is the race an arcade single race"); // (a type test of the race object)
        // An entry's controller (+0x343C): its frame read (virtual +0x28 of the table at +0xD0) gives the harness's pad.
        const u32 pad_vt = ee.alloc(0x400, 16);
        for (u32 k = 0; k < 0x400; k += 8) ee.w32(pad_vt + k + 4, kStubZero);
        ee.w32(pad_vt + 0x28 + 4, kStubPad);
        OrigRace* race_state = &o;
        ee.hle(
            kStubPad,
            [race_state](Ee& e) {
                const u32 obj = u32(e.a(0)), frame = u32(e.a(1));
                const Pad& p = race_state->pad[e.r32(obj) % 6];
                if (p.frame) {
                    for (u32 k = 0; k < 0x20; ++k) e.w8(frame + k, p.frame[k]);
                    e.set_v0(0);
                    return;
                }
                auto axis = [](float v) { return s32(std::lround(std::clamp(v, -1.0f, 1.0f) * 4096.0f)); };
                e.w8(frame + 0, 1 | 2 | 4), e.w8(frame + 1, 3), e.w8(frame + 2, u8(gt3r::s8(p.shift)));
                e.w16(frame + 4, u16(gt3r::s16(-axis(p.steer)))), e.w16(frame + 8, u16(axis(p.throttle))), e.w16(frame + 0xA, u16(axis(p.brake)));
                e.set_v0(0);
            },
            "pad frame");
        for (int i = 0; i < o.cars; ++i) {
            const u32 entry = ee.alloc(0x4000, 64);
            const u32 controller = ee.alloc(0x200, 16);
            ee.w32(controller, u32(i)), ee.w32(controller + 0xD0, pad_vt);
            ee.w32(controller + 0x1CC, i < human ? 1 : 0); // a player's
            ee.w32(entry + 0x343C, controller);
            u32 car_id = 0;
            for (const db::CarEntry& c : sdb.cars())
                if (c.label == labels[size_t(i)]) car_id = c.id;
            if (!car_id) return err = "no car " + labels[size_t(i)], false;
            ee.call(kSetCar, {Arg::ptr(entry + 0x20), Arg::integer(s64(db::part_id(0, car_id)))}, 50'000'000);
            if (equipment && size_t(i) < equipment->size())
                for (u32 k = 0; k < 0x178; ++k) ee.w8(entry + 0x20 + k, (*equipment)[size_t(i)][k]);
            ee.w32(entry + 0x3444, u32(i));                // the start place
            ee.w32(entry + 0x344C, i < human ? 0 : 1);     // who drives
            ee.w32(arr + 4 * u32(i), entry);
            o.entries.push_back(entry);
        }
        if (!drivers) {
            // The computer drivers' lines are not loaded: their object's start is left out.
            for (u32 a : {0x4639E0u, 0x469E58u, 0x465078u}) ee.hle(a, [](Ee&) {}, "driver lines");
            // (the driving line's heading at a car, which the pad's steering reads: 0 without the lines)
            ee.hle(0x4649F0, [](Ee& e) { e.fpr[0] = 0; }, "line heading");
        }
        // The race's end (the finish's countdown and what it starts) is left out of the step.
        ee.hle(0x35F658, [](Ee&) {}, "race end");
        // The parts list of the pit stops' tire choices is a file of its own: its reads are left out (no choices).
        ee.hle(0x449E10, [](Ee&) {}, "parts information open");
        ee.hle(0x44A048, [](Ee& e) { e.set_v0(0); }, "parts information row");
        o.cond = ee.alloc(kCondSize, 64);
        ee.call(kConductorCtor, {Arg::ptr(o.cond)}, 50'000'000);
        ee.w32(o.cond, o.info);
        ee.w32(o.cond + kCondCars, u32(o.cars));
        const u32 vt = ee.r32(o.cond + kCondVtable);
        const u32 init = vt + 0xE0;
        ee.call(ee.r32(init + 4), {Arg::ptr(o.cond + u32(s32(s16(ee.r16(init))))), Arg::integer(0)}, 2'000'000'000);
    } catch (const std::exception& e) {
        char b[64];
        std::snprintf(b, sizeof b, " (pc %08X, ra %08X)", ee.cur_pc, ee.g32(31));
        err = std::string("the set-up stopped: ") + e.what() + b;
        return false;
    }
    return true;
}

void orig_step(Ee& ee, OrigRace& o) { ee.call(kStep, {Arg::ptr(o.cond), Arg::integer(0)}, 500'000'000); }

} // namespace gt4r::diff
