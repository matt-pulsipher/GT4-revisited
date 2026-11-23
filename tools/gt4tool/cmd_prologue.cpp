// gt4tool's Gran Turismo 4 Prologue commands (docs/formats/prologue.md):
//
//   gt4tool prologue <disc> program    the program's build and the shared engine's tables found in it
//   gt4tool prologue <disc> survey     the disc's formats against Gran Turismo 3's and Gran Turismo 4's, with counts
//   gt4tool prologue <disc> drivers    every course's driving lines and driver sets (course slots 31, 37 .. 40)

#include "disc/core_image.h"
#include "formats/model/car_model.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/ai_line_course.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/sim/program_prologue.h"
#include "sim/exact/program_data.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace gt4r {

namespace {

using Args = std::vector<std::string>;
using gt3r::sim::exact::ProgramData;

int program(Disc& disc, bool verbose) {
    const gt3r::CoreImage core = gt3r::CoreImage::decode(disc.read_program());
    std::printf("program: load 0x%08X, entry 0x%08X, %u bytes\n", core.load_address, core.entry_point, core.image_size);
    const sim::PrologueProgram pp = sim::PrologueProgram::locate(core.program, core.load_address);
    std::printf("Prologue's build: %s\n", pp.recognized() ? "recognized" : "not recognized");
    for (const sim::PrologueProgram::Place& p : pp.places()) std::printf("  %-40s 0x%08X  %s\n", p.what, p.address, p.how);
    const ProgramData pd = ProgramData::load(core.program, core.load_address, core.entry_point);
    std::printf("the shared engine's (Gran Turismo 3's) tables in this program:\n");
    for (int t = 0; t < ProgramData::kTables; ++t) {
        const auto table = ProgramData::Table(t);
        std::printf("  %-48s tier %d  0x%08X%s\n", ProgramData::what(table), pd.tier(table), pd.table_start(table),
                    pd.tier(table) && pd.table_matched(table) ? "  (a GT3 release's values)" : "");
    }
    if (verbose)
        for (const std::string& i : pd.issues()) std::printf("  issue: %s\n", i.c_str());
    // Prologue's own tables (program_prologue.h) against Gran Turismo 3's values.
    std::printf("Prologue's tables (its values / the bytes equal to Gran Turismo 3's):\n");
    for (const sim::PrologueProgram::TablePlace& t : sim::PrologueProgram::tables()) {
        size_t same = 0;
        for (u32 i = 0; i < t.size; ++i) same += core.program[t.address + i - core.load_address] == pd.b(t.table, t.offset + i);
        std::printf("  %-36s +0x%02X 0x%03X bytes at 0x%08X (addressed by %s): %zu equal\n", ProgramData::what(t.table), t.offset, t.size,
                    t.address, t.addressed_by, same);
    }
    // A table the locator does not find: where in the program's data its Gran Turismo 3 values are
    // most alike (the bytes equal at the best place), a lead for mapping it by hand.
    const ByteSpan img(core.program);
    const u32 data_from = 0x4FC200 - core.load_address; // (past the code)
    for (int t = 0; t < ProgramData::kTables; ++t) {
        const auto table = ProgramData::Table(t);
        if (pd.tier(table)) continue;
        const u32 n = ProgramData::size(table);
        std::vector<u8> v(n);
        size_t nonzero = 0;
        for (u32 i = 0; i < n; ++i) nonzero += (v[i] = pd.b(table, i)) != 0;
        size_t best = 0, at = 0;
        for (size_t o = data_from; o + n <= img.size(); o += 4) {
            size_t eq = 0;
            for (u32 i = 0; i < n; ++i) eq += img[o + i] == v[i] && v[i];
            if (eq > best) best = eq, at = o;
        }
        std::printf("  %-48s most alike at 0x%08zX: %zu of its %zu non-zero bytes\n", ProgramData::what(table), at + core.load_address, best, nonzero);
    }
    return 0;
}

std::string magic(ByteSpan d) {
    if (d.size() < 4) return "-";
    for (size_t i = 0; i < 4; ++i)
        if (d[i] < 0x20 || d[i] > 0x7E) return "-";
    return std::string(reinterpret_cast<const char*>(d.data()), 4);
}

int survey(Disc& disc) {
    gt3r::Volume& vol = disc.volume();
    auto read = [&](const std::string& p) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol.find(p);
        return e ? std::optional<Bytes>(vol.read(*e)) : std::nullopt;
    };
    // The database.
    db::SpecDb sdb(read, [&](const std::string& d) { return vol.list_dir(d); }, "japanese");
    size_t rows = 0, plain = 0;
    for (const std::string& n : sdb.table_names()) {
        const Bytes f = read(sdb.folder() + n + ".dbt").value_or(Bytes{});
        plain += !f.empty() && !(rd32(f, 4) & 1);
        if (const db::Table* t = sdb.table(n)) rows += t->size();
    }
    std::printf("database %s: %zu tables (%zu stored plain), %zu rows, %zu cars, %zu courses\n", sdb.folder().c_str(),
                sdb.table_names().size(), plain, rows, sdb.cars().size(), sdb.courses().size());
    // The courses.
    size_t courses = 0, gt3_table = 0, gtrw = 0, rnw4 = 0, lines = 0, sets = 0, mdls = 0, gtm1 = 0;
    for (const std::string& name : vol.list_dir("crs")) {
        if (name.empty() || name.back() == '/') continue;
        const Bytes d = read("crs/" + name).value_or(Bytes{});
        if (d.size() < 0x100 || rd32(d, 0) != 0) continue;
        ++courses;
        const u32 table = rd32(d, 4);
        gt3_table += table == 0xC0;
        const u32 slot1 = table;
        mdls += magic(ByteSpan(d).subspan(slot1)) == "MDLS";
        gtm1 += magic(ByteSpan(d).subspan(slot1)) == "GTM1";
        if (!course::CourseFile::looks_like_course(d)) continue;
        const course::CourseFile f = course::CourseFile::parse(d);
        rnw4 += course::Runway::is_runway(f.section(d, course::kSlotRunway));
        gtrw += course::is_gtrw(f.section(d, course::kSlotRoadGtrw));
        if (const std::optional<course::AiData> ai = course::ai_from_course(f, d)) {
            ++lines;
            for (const auto& s : ai->sets) sets += s.has_value();
        }
    }
    std::printf("courses: %zu files; %zu with Gran Turismo 3's 48-slot table (GTM1 scenery: %zu), %zu with the 64-slot table "
                "(MDLS scenery: %zu); of the 64-slot ones %zu GTRW roads, %zu RNW4, %zu line sections, %zu driver sets\n",
                courses, gt3_table, gtm1, courses - gt3_table, mdls, gtrw, rnw4, lines, sets);
    // The cars' files.
    std::map<std::string, size_t> cars;
    for (const char* dir : {"car/lod", "car/menu"})
        for (const std::string& name : vol.list_dir(dir)) {
            if (name.empty() || name.back() == '/') continue;
            const Bytes d = read(std::string(dir) + "/" + name).value_or(Bytes{});
            const std::string m = magic(d);
            cars[std::string(dir) + " " + (m == "CAR4" ? "CAR4" : m == "Pat0" ? "Pat0" : gt3r::CarFile::is_car(d) ? "GT3 car file" : "other")]++;
        }
    for (const auto& [k, n] : cars) std::printf("%-28s %zu\n", k.c_str(), n);
    // The sounds.
    std::map<std::string, size_t> sounds;
    for (const std::string& dir : vol.list_dir("sound")) {
        const std::string path = "sound/" + dir;
        if (dir.back() != '/') {
            sounds[magic(read(path).value_or(Bytes{}))]++;
            continue;
        }
        for (const std::string& name : vol.list_dir(path)) sounds[dir + magic(read(path + name).value_or(Bytes{}))]++;
    }
    for (const auto& [k, n] : sounds) std::printf("sound %-22s %zu\n", k.c_str(), n);
    return 0;
}

int drivers(Disc& disc) {
    gt3r::Volume& vol = disc.volume();
    for (const std::string& name : vol.list_dir("crs")) {
        if (name.empty() || name.back() == '/') continue;
        const Bytes d = vol.read(*vol.find("crs/" + name));
        if (!course::CourseFile::looks_like_course(d)) {
            std::printf("%-22s (Gran Turismo 3's table: not read)\n", name.c_str());
            continue;
        }
        const course::CourseFile f = course::CourseFile::parse(d);
        const std::optional<course::AiData> ai = course::ai_from_course(f, d);
        if (!ai) {
            std::printf("%-22s no line section\n", name.c_str());
            continue;
        }
        std::printf("%-22s lines", name.c_str());
        for (int k = 0; k < course::AiData::kLines; ++k) std::printf(" %zu", ai->lines[size_t(k)].size());
        int sets = 0;
        for (const auto& s : ai->sets) sets += s.has_value();
        std::printf("  sets %d", sets);
        if (ai->sets[0]) std::printf(" (class 0: braking %u%%, arcs %u%%, pit %u km/h)", (*ai->sets[0])[0], (*ai->sets[0])[1], (*ai->sets[0])[18]);
        std::printf("\n");
    }
    return 0;
}

} // namespace

int cmd_prologue(Disc& disc, const Args& a) {
    if (a[0] == "program") return program(disc, a.size() > 1 && a[1] == "issues");
    if (a[0] == "survey") return survey(disc);
    if (a[0] == "drivers") return drivers(disc);
    std::fprintf(stderr, "usage: gt4tool prologue <disc> program | survey | drivers\n");
    return 2;
}

} // namespace gt4r
