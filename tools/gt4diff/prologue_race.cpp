// Gran Turismo 4 Prologue's own program set up in the interpreter (prologue_race.h).

#include "prologue_race.h"

#include "disc/core_image.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/db/car_parts.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/program_prologue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace gt4r::diff::prologue {

namespace {

constexpr u32 kMemset = 0x4BC174;
constexpr u32 kDatabase = 0x505358;      // the database object: table t's object at +0x124 + 4 t (t < 39), the language at +4
constexpr u32 kTableNames = 0x5052A0;    // the tables' names (39 pointers)
constexpr u32 kLanguageTables = 0x505340; // the tables named with the language after them
constexpr u32 kLanguageNames = 0x505350;
constexpr u32 kTableCtor = 0x406908;      // a table object (8 bytes: +0 the file's image, +4 its vtable)
constexpr u32 kLabelsCtor = 0x4065B0;     // a table's label index (8 bytes: +0 the .idi file's image, +4 its vtable)
constexpr u32 kLabelTables = 0x573D00;    // the tables with a label index (0x27 ends the list)
constexpr u32 kSetCar = 0x407A08;         // (equipment, car)
constexpr u32 kCourseRelocate = 0x3A8058; // (the course's container)
constexpr u32 kConductorCtor = 0x3B5468;  // DynamicsConductorSinglePlayer
constexpr u32 kStep = 0x3B7020;           // (conductor, first car)
constexpr u32 kCourseTable = 0x1E;
// Places in the program's code that nothing of a race runs, taken for the stand-ins' entry points.
constexpr u32 kStubZero = 0x100000, kStubPad = 0x100004;

// The entry's layout (0x4000 bytes): +0x20 the equipment, +0x3FA4 the controller (+0x184 set: a player's), +0x3FA8 the
// grid place, +0x3FAC / +0x3FB0 the driver's words the conductor reads, +0x3FCC the condition block.
constexpr u32 kEntrySize = 0x4000, kEntryController = 0x3FA4, kEntryPlace = 0x3FA8, kEntryAsm = 0x3FB8, kEntryTcs = 0x3FBC;

} // namespace

bool is_prologue(Disc& disc) {
    const sim::Program p = sim::Program::load(disc);
    return !p.known() && sim::PrologueProgram::locate(p).recognized();
}

void boot(Ee& ee, Disc& disc) {
    const gt3r::CoreImage core = gt3r::CoreImage::decode(disc.read_program());
    ee.load_program(core.program, core.load_address, kTextEnd, kBssStart, kBssEnd, kGp);
    ee.hle(
        kMemset,
        [](Ee& e) {
            const u32 to = u32(e.a(0)), n = u32(e.a(2));
            if (n) std::memset(e.host_ptr(to, n), int(e.a(1)), n);
            e.set_v0(to);
        },
        "memset");
    // The tables, each file's image under a table object as the loader leaves it (the language Japanese, index 0 of its names).
    const std::string folder = "specdb/GT4_PROLOGUE1000/";
    const std::string language = ee.read_cstr(ee.r32(kLanguageNames));
    ee.w32(kDatabase + 4, 0);
    for (u32 t = 0; t < 39; ++t) {
        std::string name = ee.read_cstr(ee.r32(kTableNames + 4 * t));
        for (u32 k = 0; k < 3; ++k)
            if (ee.r32(kLanguageTables + 4 * k) == t) name += language;
        const gt3r::Volume::Entry* e = disc.volume().find(folder + name + ".dbt");
        if (!e) continue;
        const Bytes file = disc.volume().read(*e);
        const u32 image = ee.alloc_bytes(file, 16), table = ee.alloc(8);
        ee.call(kTableCtor, {Arg::ptr(table)});
        ee.w32(table, image);
        ee.w32(kDatabase + 0x124 + 4 * t, table);
    }
    // The label indexes of the tables listed at 0x573D00 (up to 0x27), each file's (.idi) image under a LabelInformation
    // object (0x4065B0) at the database's +0x88 + 4 t: what a lookup by label (0x407450) reads, e.g. the spec builder's
    // special cars (0x3B3998).
    for (u32 k = 0; k < 40; ++k) {
        const u32 t = ee.r32(kLabelTables + 4 * k);
        if (t >= 39) break;
        std::string name = ee.read_cstr(ee.r32(kTableNames + 4 * t));
        for (u32 j = 0; j < 3; ++j)
            if (ee.r32(kLanguageTables + 4 * j) == t) name += language;
        const gt3r::Volume::Entry* e = disc.volume().find(folder + name + ".idi");
        if (!e) continue;
        const Bytes file = disc.volume().read(*e);
        const u32 image = ee.alloc_bytes(file, 16), labels = ee.alloc(8);
        ee.call(kLabelsCtor, {Arg::ptr(labels)});
        ee.w32(labels, image);
        ee.w32(kDatabase + 0x88 + 4 * t, labels);
    }
}

bool aids_from_env() { return !(std::getenv("GT4R_AIDS") && std::string(std::getenv("GT4R_AIDS")) == "0"); }

bool setup_race(Ee& ee, Disc& disc, db::SpecDb& sdb, const std::string& course, const std::vector<std::string>& labels, int human, Race& r,
                std::string& err, bool aids, const Lesson* lesson) {
    r = Race{};
    r.cars = int(labels.size());
    try {
        if (std::getenv("GT4R_PROLOGUE_ROWS"))
            ee.probe(0x407550, [](Ee& e) { std::printf("  row: table %d id %llX (ra %08X)\n", int(e.a(2)), (unsigned long long)e.a(1), e.g32(31)); });
        // The course's container with the blocks the race reads (28 the road, 31 the drivers' lines, 37 .. 40 the driver
        // sets), relocated by the program's own loader; the course object (+0 the container).
        const gt3r::Volume::Entry* file = disc.volume().find("crs/" + course);
        if (!file) return err = "no course " + course, false;
        const Bytes data = disc.volume().read(*file);
        const course::CourseFile parsed = course::CourseFile::parse(data);
        std::vector<u8> out(0x100, 0);
        for (int slot : {28, 31, 37, 38, 39, 40}) {
            const ByteSpan s = parsed.section(data, slot);
            if (s.empty()) continue;
            while (out.size() % 0x40) out.push_back(0);
            const u32 off = u32(out.size());
            out.insert(out.end(), s.begin(), s.end());
            std::memcpy(out.data() + 4 * slot, &off, 4);
        }
        while (out.size() % 0x40) out.push_back(0);
        if (parsed.section(data, 28).empty()) return err = "the course has no road", false;
        r.container = ee.alloc_bytes(ByteSpan(out.data(), out.size()), 64);
        ee.call(kCourseRelocate, {Arg::ptr(r.container)}, 200'000'000);
        r.course = ee.alloc(0x200, 64);
        ee.w32(r.course, r.container);

        u32 course_id = 0;
        for (const db::CourseEntry& c : sdb.courses())
            if (c.label == course) course_id = c.id;
        // The race information: +0x18 the entries' list {count, +8 the entries}, +0x30 the course's row, +0x38 the course object.
        r.info = ee.alloc(0x2000, 64);
        const u32 list = ee.alloc(0x20), arr = ee.alloc(4 * 8);
        ee.w32(list, u32(r.cars)), ee.w32(list + 8, arr);
        ee.w32(r.info + 0x18, list);
        ee.w64(r.info + 0x30, (u64(kCourseTable) << 32) | course_id);
        ee.w32(r.info + 0x38, r.course);
        ee.hle(kStubZero, [](Ee& e) { e.set_v0(0), e.fpr[0] = 0; }, "virtual answering 0");
        Race* race = &r;
        ee.hle(
            kStubPad,
            [race](Ee& e) {
                const u32 obj = u32(e.a(0)), frame = u32(e.a(1));
                const Pad& p = race->pad[e.r32(obj) % 6];
                if (p.raw) {
                    for (u32 k = 0; k < 16; ++k) e.w8(frame + k, p.raw[k]);
                    e.set_v0(0);
                    return;
                }
                auto axis = [](float v) { return s32(std::lround(std::clamp(v, -1.0f, 1.0f) * 4096.0f)); };
                e.w8(frame + 0, 1 | 2 | 4), e.w8(frame + 1, p.kind), e.w8(frame + 2, u8(gt3r::s8(p.shift)));
                e.w16(frame + 4, u16(gt3r::s16(-axis(p.steer)))), e.w16(frame + 8, u16(axis(p.throttle))), e.w16(frame + 0xA, u16(axis(p.brake)));
                e.w8(frame + 3, 0), e.w16(frame + 6, 0), e.w16(frame + 0xC, 0), e.w16(frame + 0xE, 0); // (the back and third axes unused)
                e.set_v0(0);
            },
            "pad frame");
        const u32 pad_vt = ee.alloc(0x400, 16);
        for (u32 k = 0; k < 0x400; k += 8) ee.w32(pad_vt + k + 4, kStubZero);
        ee.w32(pad_vt + 0x28 + 4, kStubPad); // (the frame read: car +0x0C the controller, its table +0xD0, virtual +0x28)
        for (int i = 0; i < r.cars; ++i) {
            const u32 entry = ee.alloc(kEntrySize, 64);
            const u32 controller = ee.alloc(0x200, 16);
            ee.w32(controller, u32(i)), ee.w32(controller + 0xD0, pad_vt);
            ee.w32(controller + 0x184, i < human ? 1 : 0);
            ee.w32(entry + kEntryController, controller);
            u32 car_id = 0;
            for (const db::CarEntry& c : sdb.cars())
                if (c.label == labels[size_t(i)]) car_id = c.id;
            if (!car_id) return err = "no car " + labels[size_t(i)], false;
            ee.call(kSetCar, {Arg::ptr(entry + 0x20), Arg::integer(s64(db::part_id(0, car_id)))}, 50'000'000);
            ee.w32(entry + kEntryPlace, u32(i));
            // The entry's stability and traction control switches (+0x3FB8 / +0x3FBC, read by the car's set-up 0x3C93A0).
            ee.w32(entry + kEntryAsm, aids ? 1 : 0), ee.w32(entry + kEntryTcs, aids ? 1 : 0);
            if (lesson && lesson->entry) lesson->entry(ee, entry, i);
            ee.w32(arr + 4 * u32(i), entry);
            r.entries.push_back(entry);
        }
        r.cond = ee.alloc(kCondSize, 64);
        ee.call(lesson ? lesson->conductor : kConductorCtor, {Arg::ptr(r.cond)}, 50'000'000);
        ee.w32(r.cond, r.info);
        if (lesson && !lesson->row.empty()) {
            const u32 row = ee.alloc_bytes(ByteSpan(lesson->row.data(), lesson->row.size()), 16);
            ee.w32(r.info + 0x28, row);
            ee.call(0x3B6B20, {Arg::ptr(r.cond), Arg::ptr(r.info)}, 50'000'000);
        }
        ee.w32(r.cond + kCondCars, u32(r.cars));
        const u32 vt = ee.r32(r.cond + kCondVtable);
        const u32 init = vt + 0xC8;
        ee.call(ee.r32(init + 4), {Arg::ptr(r.cond + u32(s32(s16(ee.r16(init))))), Arg::integer(0)}, 2'000'000'000);
    } catch (const std::exception& e) {
        char b[64];
        std::snprintf(b, sizeof b, " (pc %08X, ra %08X)", ee.cur_pc, ee.g32(31));
        err = std::string("the set-up stopped: ") + e.what() + b;
        return false;
    }
    return true;
}

void step(Ee& ee, Race& r) { ee.call(kStep, {Arg::ptr(r.cond), Arg::integer(0)}, 500'000'000); }

} // namespace gt4r::diff::prologue
