// gt4tool's Gran Turismo 4 Prologue commands (docs/formats/prologue.md):
//
//   gt4tool prologue <disc> program    the program's build and the shared engine's tables found in it
//   gt4tool prologue <disc> survey     the disc's formats against Gran Turismo 3's and Gran Turismo 4's, with counts
//   gt4tool prologue <disc> drivers    every course's driving lines and driver sets (course slots 31, 37 .. 40)
//   gt4tool prologue <disc> strobe <path> a race movie's text fields, named tracks and the words its scripts use
//   gt4tool prologue <disc> hud        the race display's rows by mode (database/racemode_e3.db, racedetail_e3.db)
//                                       beside Gran Turismo 3's

#include "disc/core_image.h"
#include <cstdlib>
#include "formats/model/car_model.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/ai_line_course.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/program_prologue.h"
#include "formats/menu/imgs.h"
#include "formats/png_writer.h"
#include "gt4r/race/strobe.h"
#include "race/hud_layout.h"
#include "sim/exact/program_data.h"

#include <cstdio>
#include <map>
#include <set>
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

int hud(Disc& disc) {
    namespace hr = gt3r::race;
    auto read = [&disc](const char* path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = disc.volume().find(path);
        if (!e) return std::nullopt;
        return disc.volume().read(*e);
    };
    const std::optional<Bytes> modes = read("database/racemode_e3.db"), rows = read("database/racedetail_e3.db");
    if (!modes || !rows || !hr::set_hud_tables(*modes, *rows)) {
        std::fprintf(stderr, "error: the display's tables do not read\n");
        return 1;
    }
    for (int m = 0; m <= 15; ++m) {
        const auto ours_span = hr::hud_layout(hr::HudMode(m));
        const std::vector<hr::HudRow> ours(ours_span.begin(), ours_span.end());
        hr::clear_hud_tables();
        const auto gt3 = hr::hud_layout(hr::HudMode(m));
        std::vector<hr::HudRow> gt3_rows(gt3.begin(), gt3.end());
        hr::set_hud_tables(*modes, *rows);
        std::printf("mode %d: %zu rows (Gran Turismo 3: %zu)\n", m, ours.size(), gt3_rows.size());
        for (const hr::HudRow& r : ours) {
            bool same = false;
            for (const hr::HudRow& g : gt3_rows)
                same |= g.part == r.part && g.sub == r.sub && g.view == r.view && g.x == r.x && g.y == r.y && g.w == r.w && g.h == r.h && g.align == r.align &&
                        g.valign == r.valign;
            std::printf("   part %2d sub %3d view %3d  at %4d,%4d  size %4d,%4d  align %d,%d%s\n", int(r.part), r.sub, r.view, r.x, r.y, r.w, r.h, r.align,
                        r.valign, same ? "" : "   (not Gran Turismo 3's)");
        }
    }
    hr::clear_hud_tables();
    // The display's pictures (race/<region>/display/display.imgs).
    for (const char* region : {"JP", "US", "BR"}) {
        const std::string path = std::string("race/") + region + "/display/display.imgs";
        const std::optional<Bytes> imgs = read(path.c_str());
        if (!imgs || !gt3r::menu::Imgs::is_imgs(*imgs)) continue;
        const gt3r::menu::Imgs set = gt3r::menu::Imgs::parse(*imgs);
        std::printf("%s: %zu pictures:", path.c_str(), set.entries.size());
        for (const auto& e : set.entries) std::printf(" %s", e.name.c_str());
        std::printf("\n");
    }
    return 0;
}

int strobe(Disc& disc, const std::string& path) {
    const gt3r::Volume::Entry* e = disc.volume().find(path);
    if (!e) {
        std::fprintf(stderr, "error: no file %s\n", path.c_str());
        return 1;
    }
    const Bytes data = disc.volume().read(*e);
    if (!race::StrobeMovie::is_movie(data)) {
        std::fprintf(stderr, "error: %s is not a race movie\n", path.c_str());
        return 1;
    }
    const race::StrobeMovie m = race::StrobeMovie::parse(data);
    std::printf("%zu sprites, %zu shapes, %zu texts, %zu buttons, %zu actions, %zu textures\n", m.sprites.size(), m.shapes.size(), m.texts.size(),
                m.buttons.size(), m.actions.size(), m.textures.size());
    for (size_t i = 0; i < m.textures.size(); ++i) {
        const ByteSpan set = m.texture_set(i);
        std::printf("texture %zu: %u x %u, %u bytes at 0x%X, %s", i, m.textures[i].width, m.textures[i].height, m.textures[i].size, m.textures[i].offset,
                    gt3r::Tex1::is_tex1(set) ? "Tex1" : "not Tex1");
        if (gt3r::Tex1::is_tex1(set)) {
            const gt3r::Tex1 tex = gt3r::Tex1::parse(set);
            for (const auto& t : tex.textures) std::printf(" [%u x %u psm %u]", t.tex0.width(), t.tex0.height(), t.tex0.psm);
            if (std::getenv("GT4R_STROBE_PIXELS")) {
                gt3r::GsMemory gs;
                tex.upload(set, gs);
                const gt3r::Tex1::Image img = tex.decode(gs, 0);
                std::printf(" decoded %u x %u:", img.width, img.height);
                if (const char* dir = std::getenv("GT4R_STROBE_DUMP")) { // each picture as a PNG
                    char name[256];
                    std::snprintf(name, sizeof name, "%s/texture%zu.png", dir, i);
                    const Bytes png = gt3r::encode_png(img.width, img.height, img.rgba);
                    if (std::FILE* f = std::fopen(name, "wb")) std::fwrite(png.data(), 1, png.size(), f), std::fclose(f);
                }
                const size_t n = img.rgba.size();
                for (size_t k = 0; k < n; k += std::max<size_t>(1, n / 16)) std::printf(" %08X", img.rgba[k]);
            }
        } else if (set.size() >= 16) {
            std::printf(" (");
            for (size_t k = 0; k < 16; ++k) std::printf("%02X", set[k]);
            std::printf(")");
        }
        std::printf("\n");
    }
    if (const char* run = std::getenv("GT4R_STROBE_RUN")) { // "<prize level>,<seconds>": the results movie played with the answers of a run
        int prize = 0;
        float seconds = 1;
        std::sscanf(run, "%d,%f", &prize, &seconds);
        auto movie = std::make_shared<const race::StrobeMovie>(m);
        race::StrobePlayer p(movie, [prize](const std::string& name, const std::vector<race::StrobePlayer::Value>& args) {
            using V = race::StrobePlayer::Value;
            std::printf("(host %s", name.c_str());
            for (const V& a : args) std::printf(" %s", a.to_string().c_str());
            std::printf(")\n");
            std::string n = name;
            for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
            if (n == "PRIZELEVEL") return V::number(prize);
            if (n == "GETTIME") return V::string("0'07.867");
            if (n == "GETRANK") return V::number(0);
            if (n == "GETDEFAULTNAME") return V::string("AAA");
            return V{};
        });
        p.restart();
        for (float t = 0; t < seconds; t += 1.0f / 30.0f) p.advance(1.0f / 30.0f);
        std::printf("root frame %d\n", p.frame());
        for (const std::string& l : p.tree()) std::printf("%s\n", l.c_str());
        for (const race::StrobePlayer::Draw& d : p.draw_list()) {
            if (d.text) std::printf("text \"%s\" at %.0f,%.0f clipped %d mul %.2f %.2f %.2f %.2f\n", d.words.c_str(), double(d.m[4]), double(d.m[5]), int(d.clipped), double(d.mul[0]), double(d.mul[1]), double(d.mul[2]), double(d.mul[3]));
        }
    }
    if (std::getenv("GT4R_STROBE_SHAPES")) { // the fills, their matrices, the shapes with their fills, the tracks
        for (size_t i = 0; i < m.fills.size(); ++i) {
            const auto& f = m.fills[i];
            std::printf("fill %zu: kind %u index %u value %08X", i, f.kind, f.index, f.value);
            if (f.kind == 4 && f.value < m.matrices.size()) {
                const auto& t = m.matrices[f.value];
                std::printf(" matrix [%g %g %g %g / %g %g %g %g / %g %g %g %g / %g %g %g %g]", t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8], t[9], t[10], t[11], t[12], t[13], t[14], t[15]);
            }
            std::printf("\n");
        }
        for (size_t i = 0; i < m.shapes.size(); ++i) {
            const auto& sh = m.shapes[i];
            std::printf("shape %zu: %g %g .. %g %g", i, sh.x0, sh.y0, sh.x1, sh.y1);
            for (const auto& g : sh.groups) {
                float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f};
                for (const auto& q : g.triangles) lo[0] = std::min(lo[0], q.x), lo[1] = std::min(lo[1], q.y), hi[0] = std::max(hi[0], q.x), hi[1] = std::max(hi[1], q.y);
                std::printf(" group fill %u flags %X tris %zu (%g %g .. %g %g)", g.fill, g.flags, g.triangles.size() / 3, lo[0], lo[1], hi[0], hi[1]);
            }
            std::printf("\n");
        }
        for (size_t k = 0; k < m.sprites.size(); ++k) {
            std::printf("sprite %zu:", k);
            for (const auto& t : m.sprites[k].tracks) std::printf(" [kind %u index %u depth %u%s]", t.kind, t.index, t.depth, t.name.empty() ? "" : (" " + t.name).c_str());
            std::printf("\n");
        }
    }
    for (size_t i = 0; i < m.texts.size(); ++i)
        std::printf("text %zu: \"%s\" font %d (%s) color %08X box %g %g .. %g %g height %g align %u\n", i, m.texts[i].text.c_str(), m.texts[i].font,
                    m.texts[i].font >= 0 && size_t(m.texts[i].font) < m.fonts.size() ? m.fonts[size_t(m.texts[i].font)].c_str() : "?", m.texts[i].color,
                    double(m.texts[i].x0), double(m.texts[i].y0), double(m.texts[i].x1), double(m.texts[i].y1), double(m.texts[i].height), m.texts[i].align);
    for (size_t s = 0; s < m.sprites.size(); ++s)
        for (const auto& t : m.sprites[s].tracks)
            if (!t.name.empty()) std::printf("sprite %zu track \"%s\"\n", s, t.name.c_str());
    // The words of the scripts (their constants), each once.
    std::set<std::string> words;
    for (const Bytes& a : m.actions) {
        std::string run;
        for (u8 c : a) {
            if (c >= 0x20 && c < 0x7F) run += char(c);
            else {
                if (run.size() >= 3) words.insert(run);
                run.clear();
            }
        }
    }
    std::printf("words:");
    for (const std::string& w : words) std::printf(" %s", w.c_str());
    std::printf("\n");
    return 0;
}

// Words (or a string) of the program's image at an address, for reading its tables.
int peek(Disc& disc, u32 at, u32 n, bool text) {
    const sim::Program program = sim::Program::load(disc);
    if (text) {
        std::string s;
        for (u32 k = 0; k < n; ++k) {
            const u8 c = program.b(at + k);
            if (!c) break;
            s += char(c);
        }
        std::printf("%08X: \"%s\"\n", at, s.c_str());
        return 0;
    }
    for (u32 k = 0; k < n; ++k) std::printf("%08X: %08X\n", at + 4 * k, program.w(at + 4 * k));
    return 0;
}

} // namespace

int cmd_prologue(Disc& disc, const Args& a) {
    if ((a[0] == "peek" || a[0] == "peeks") && a.size() > 1)
        return peek(disc, u32(std::strtoul(a[1].c_str(), nullptr, 16)), a.size() > 2 ? u32(std::strtoul(a[2].c_str(), nullptr, 0)) : 1, a[0] == "peeks");
    if (a[0] == "program") return program(disc, a.size() > 1 && a[1] == "issues");
    if (a[0] == "survey") return survey(disc);
    if (a[0] == "drivers") return drivers(disc);
    if (a[0] == "hud") return hud(disc);
    if (a[0] == "strobe" && a.size() > 1) return strobe(disc, a[1]);
    std::fprintf(stderr, "usage: gt4tool prologue <disc> program | survey | drivers | hud | strobe <path> | peek <hex address> [words] | peeks <hex address>\n");
    return 2;
}

} // namespace gt4r
