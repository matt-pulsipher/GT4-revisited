// gt4tool: command line access to a Gran Turismo 4 disc image (your own), for format research.
//
//   gt4tool info <disc>                    identify the disc, its volumes and its program
//   gt4tool ls <disc> [folder]             list a folder of the volumes
//   gt4tool survey <disc>                  count the files by folder, extension and magic
//   gt4tool extract <disc> <prefix> <dir>  write the files under a prefix (unpacked) to a folder
//   gt4tool tex <disc> <path> <dir>        decode every texture set in a file to PNGs
//   gt4tool adc <disc> [path]              a script's listing; without a path, a count of all
//   gt4tool menu <disc> [project]         the menu projects: pages, widgets, pictures and text found
//   gt4tool menu-run <disc> <project> [keys]  runs a menu project's script on its widgets: the pages it opens,
//                                          what it asks for; keys: up down left right accept cancel
//   gt4tool run <disc> [path]              runs a script's top level with stand-ins for the program's own
//                                          classes and lists what it asked of them; without a path, every script
//   gt4tool db <disc> [cars | courses | <table> [rows]]   the car database
//   gt4tool model <disc> <path>            describe a file's model sets as decoded
//   gt4tool runway-survey <disc> [prefix]  read every course's road
//   gt4tool model-survey <disc> [prefix] [textures]   decode every model set under a prefix
//   gt4tool race <disc> <what> ...        the race view's data (cmd_race.cpp)
//
// Extracted files are for your own study; never commit them (docs/REVERSE_ENGINEERING.md).

#include "base/file.h"
#include "base/log.h"
#include "disc/core_image.h"
#include "formats/gs_memory.h"
#include "formats/png_writer.h"
#include "formats/tex1.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/menu/font.h"
#include "gt4r/menu/project.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/script/adc.h"
#include "gt4r/script/vm.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gt4r {
int cmd_sound(Disc& disc, const std::vector<std::string>& args); // cmd_sound.cpp
namespace tool { int cmd_ai(Disc& disc, const std::vector<std::string>& args); } // cmd_ai.cpp
}
using namespace gt4r;

int cmd_race(Disc& disc, const std::vector<std::string>& args); // cmd_race.cpp
namespace gt4tool {
int cmd_menu_run(Disc& disc, const std::vector<std::string>& args); // cmd_menu.cpp
int cmd_db_strings(Disc& disc, const std::vector<std::string>& args);
int cmd_menu_names(Disc& disc, const std::vector<std::string>& args);
}

namespace {

using Args = std::vector<std::string>;

bool save(const std::string& path, ByteSpan data) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (!gt3r::write_host_file(path, data)) {
        std::fprintf(stderr, "error: cannot write %s\n", path.c_str());
        return false;
    }
    return true;
}

std::string magic_of(ByteSpan d) {
    if (d.size() < 4) return "(short)";
    for (size_t i = 0; i < 4; ++i)
        if (d[i] < 0x20 || d[i] > 0x7E) return "(binary)";
    return std::string(reinterpret_cast<const char*>(d.data()), 4);
}

int cmd_info(Disc& disc, const Args&) {
    const DiscInfo& i = disc.info();
    std::printf("%s\n", i.description.c_str());
    std::printf("  serial   %s  version %s  video %s\n", i.serial.c_str(), i.version.c_str(), i.video.c_str());
    std::printf("  game     %s\n", game_name(i.game));
    std::printf("  files    %s%s\n", i.digest.c_str(), i.verified ? " (verified release)" : i.modified ? " (modified copy)" : " (not in the tested list)");
    if (!i.problem.empty()) std::printf("  problem  %s\n", i.problem.c_str());
    for (const VolumeInfo& v : disc.volumes()) {
        std::printf("  %-10s %zu files, %.2f GB, %s", v.name.c_str(), v.files, double(v.size) / 1e9, v.paged ? "paged index" : "front table");
        if (v.unused_entries) std::printf(" (front table: %zu entries the game does not read)", v.unused_entries);
        std::printf("\n");
    }
    u64 bytes = 0, stored = 0;
    size_t packed = 0;
    for (const ImageFile& f : disc.files()) bytes += f.size, stored += f.stored_size, packed += f.packed;
    std::printf("  total    %zu files (%zu packed), %.2f GB on the disc, %.2f GB unpacked\n", disc.files().size(), packed,
                double(stored) / 1e9, double(bytes) / 1e9);
    const gt3r::CoreImage core = gt3r::CoreImage::decode(disc.read_program());
    std::printf("  program  CORE.GT4: load 0x%08X, entry 0x%08X, %u bytes\n", core.load_address, core.entry_point, core.image_size);
    return 0;
}

int cmd_ls(Disc& disc, const Args& a) {
    const std::string dir = a.empty() ? "" : a[0];
    for (const std::string& name : disc.volume().list_dir(dir)) {
        if (name.back() == '/') {
            std::printf("%12s  %s\n", "", name.c_str());
            continue;
        }
        const std::string prefix = dir.empty() || dir.back() == '/' ? dir : dir + "/";
        const gt3r::Volume::Entry* e = disc.volume().find(prefix + name);
        std::printf("%12u  %s\n", e ? e->size : 0, name.c_str());
    }
    return 0;
}

int cmd_survey(Disc& disc, const Args&) {
    struct Row {
        size_t files = 0;
        u64 bytes = 0;
        std::string example;
    };
    std::map<std::string, Row> rows;
    for (const ImageFile& f : disc.files()) {
        Bytes head;
        try {
            if (f.packed) head = disc.volume().read(*disc.volume().find(f.path)); // (unpacked whole: packed files are small)
            else head = disc.read_stored({f.path, f.disc_offset, std::min<u32>(f.size, 16), std::min<u32>(f.size, 16), false, f.volume});
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s: %s\n", f.path.c_str(), e.what());
            return 1;
        }
        const std::string top = f.path.find('/') == std::string::npos ? "(root)" : f.path.substr(0, f.path.find('/'));
        const std::string leaf = f.path.substr(f.path.find_last_of('/') + 1);
        const std::string ext = leaf.find('.') == std::string::npos ? "-" : leaf.substr(leaf.find_last_of('.') + 1);
        char key[160];
        std::snprintf(key, sizeof key, "%-13s .%-9s %-9s", top.c_str(), ext.c_str(), magic_of(head).c_str());
        Row& r = rows[key];
        if (!r.files++) r.example = f.path;
        r.bytes += f.size;
    }
    for (const auto& [key, r] : rows) std::printf("%s %6zu files %12llu bytes  %s\n", key.c_str(), r.files, (unsigned long long)r.bytes, r.example.c_str());
    return 0;
}

int cmd_extract(Disc& disc, const Args& a) {
    size_t n = 0;
    for (const gt3r::Volume::Entry& e : disc.volume().entries()) {
        if (e.path.compare(0, a[0].size(), a[0]) != 0) continue;
        if (!save(a[1] + "/" + e.path, disc.volume().read(e))) return 1;
        ++n;
    }
    std::printf("%zu files written to %s\n", n, a[1].c_str());
    return n ? 0 : 1;
}

int cmd_tex(Disc& disc, const Args& a) {
    const gt3r::Volume::Entry* e = disc.volume().find(a[0]);
    if (!e) {
        std::fprintf(stderr, "error: no such file: %s\n", a[0].c_str());
        return 1;
    }
    const Bytes data = disc.volume().read(*e);
    const std::vector<size_t> sets = gt3r::find_tex1_sets(data);
    std::printf("%zu texture set(s)\n", sets.size());
    for (size_t s = 0; s < sets.size(); ++s) {
        const ByteSpan sub = ByteSpan(data).subspan(sets[s]);
        const gt3r::Tex1 tex = gt3r::Tex1::parse(sub);
        gt3r::GsMemory gs;
        tex.upload(sub, gs);
        for (size_t i = 0; i < tex.textures.size(); ++i) {
            const gt3r::Tex1::Image img = tex.decode(gs, i);
            char name[64];
            std::snprintf(name, sizeof name, "/set%02zu_tex%02zu.png", s, i);
            std::printf("  set %zu @0x%zx tex %zu: %ux%u %s\n", s, sets[s], i, img.width, img.height, gt3r::psm_name(tex.textures[i].tex0.psm));
            if (!save(a[1] + name, gt3r::encode_png(img.width, img.height, img.rgba))) return 1;
        }
    }
    return 0;
}

// A file's model sets as the shared decoder builds them: meshes, primitives and their draw state.
int cmd_model(Disc& disc, const Args& a) {
    const gt3r::Volume::Entry* e = disc.volume().find(a[0]);
    if (!e) {
        std::fprintf(stderr, "error: no such file: %s\n", a[0].c_str());
        return 1;
    }
    const Bytes data = disc.volume().read(*e);
    for (size_t off : mdls::find_model_sets(data)) {
        const ByteSpan set = ByteSpan(data).subspan(off);
        const mdls::Header h = mdls::read_header(set);
        std::printf("model set @0x%zx: %u models, %u shapes, %u materials, %u texture sets x %u lists, %u variations%s\n", off, h.models,
                    h.shapes, h.materials, h.texture_sets, h.texture_lists, h.variations, h.script ? ", script" : "");
        mdls::Options o;
        if (a[0].compare(0, 6, "wheel/") == 0) o.scene.outside_sets[mdls::kWheelFaceSlot] = mdls::kWheelFaceSet;
        try {
            const gt3r::scene::Model m = mdls::build_model(set, o);
            for (const gt3r::scene::Mesh& mesh : m.meshes) {
                std::printf("  mesh node %d lod %d: %zu vertices, %zu triangles\n", mesh.node, mesh.lod, mesh.vertices.size(), mesh.indices.size() / 3);
                for (const gt3r::scene::Primitive& p : mesh.primitives)
                    std::printf("    %5u tris  texture %2d  flags %05x  alpha ref %.3f  env %d  diffuse %.2f %.2f %.2f\n", p.index_count / 3,
                                p.texture, p.flags, double(p.alpha_ref), p.env_kind, double(p.material.diffuse[0]),
                                double(p.material.diffuse[1]), double(p.material.diffuse[2]));
            }
            for (size_t i = 0; i < m.textures.size(); ++i) std::printf("  texture %zu: %ux%u\n", i, m.textures[i].width, m.textures[i].height);
        } catch (const std::exception& ex) {
            std::printf("  could not build: %s\n", ex.what());
        }
    }
    return 0;
}

// Every model set under a prefix through the converter and the shared decoder.
int cmd_model_survey(Disc& disc, const Args& a) {
    const std::string prefix = a.empty() ? "" : a[0];
    const bool textures = a.size() > 1 && a[1] == "textures";
    size_t files = 0, sets = 0, ok = 0, meshes = 0, triangles = 0, tex = 0;
    mdls::ConvertStats st;
    std::map<std::string, std::pair<size_t, std::string>> errors;
    for (const gt3r::Volume::Entry& e : disc.volume().entries()) {
        if (e.path.compare(0, prefix.size(), prefix) != 0 || e.size > (64u << 20)) continue;
        const Bytes data = disc.volume().read(e);
        const std::vector<size_t> found = mdls::find_model_sets(data);
        if (found.empty()) continue;
        ++files;
        for (size_t off : found) {
            ++sets;
            try {
                const ByteSpan set = ByteSpan(data).subspan(off);
                mdls::to_gtm1(set, &st);
                mdls::Options o;
                o.textures = textures;
                const gt3r::scene::Model m = mdls::build_model(set, o);
                ++ok;
                meshes += m.meshes.size();
                tex += m.textures.size();
                for (const gt3r::scene::Mesh& mesh : m.meshes) triangles += mesh.indices.size() / 3;
            } catch (const std::exception& ex) {
                auto& row = errors[ex.what()];
                if (!row.first++) row.second = e.path;
            }
        }
    }
    std::printf("%zu files, %zu model sets, %zu decoded: %zu meshes, %zu triangles, %zu textures\n", files, sets, ok, meshes, triangles, tex);
    std::printf("  %zu shapes, %zu packets (%zu packed), %zu vertex arrays left out, %zu commands (%zu replaced)\n", st.shapes,
                st.packets, st.packed_packets, st.unknown_unpacks, st.commands, st.replaced_commands);
    for (const auto& [kind, n] : st.unknown_kinds) std::printf("  left out: %zu x %s\n", n, kind.c_str());
    for (const auto& [what, row] : errors) std::printf("  %zu x %s (first: %s)\n", row.first, what.c_str(), row.second.c_str());
    return ok == sets ? 0 : 1;
}

// Every course's road: sizes, and how its tables hang together.
int cmd_runway_survey(Disc& disc, const Args& a) {
    const std::string prefix = a.empty() ? "crs/" : a[0];
    size_t n = 0, bad = 0;
    std::map<u32, size_t> kinds, flag_bits, wall_tags;
    for (const gt3r::Volume::Entry& e : disc.volume().entries()) {
        if (e.path.compare(0, prefix.size(), prefix) != 0) continue;
        const Bytes data = disc.volume().read(e);
        if (!course::CourseFile::looks_like_course(data)) continue;
        const course::CourseFile file = course::CourseFile::parse(data);
        try {
            const course::Runway r = course::Runway::parse(file.section(data, course::kSlotRunway));
            ++n;
            // The grid stands on the road, and every triangle is found through the tree.
            size_t on_road = 0, through_tree = 0;
            for (const course::Runway::Place& g : r.grid) {
                float y;
                on_road += r.height_at(g.pos.x, g.pos.z, g.pos.y + 2.0f, y) && std::fabs(y - g.pos.y) < 1.0f;
            }
            for (size_t t = 0; t < r.triangles.size(); t += 7) {
                const course::Runway::Triangle& tri = r.triangles[t];
                const course::Vec3 &p0 = r.vertices[tri.v[0]].pos, &p1 = r.vertices[tri.v[1]].pos, &p2 = r.vertices[tri.v[2]].pos;
                const int c = r.cluster_at((p0.x + p1.x + p2.x) / 3, (p0.z + p1.z + p2.z) / 3);
                if (c < 0) continue;
                const auto& list = r.clusters[size_t(c)].triangles;
                through_tree += std::find(list.begin(), list.end(), u16(t)) != list.end();
            }
            for (const course::Runway::Triangle& t : r.triangles) {
                ++kinds[t.kind];
                for (u32 b = 0; b < 32; ++b)
                    if (t.flags >> b & 1) ++flag_bits[b];
            }
            for (const course::Runway::WallVertex& w : r.wall_vertices) ++wall_tags[w.tag];
            std::printf("%-28s v%u %8.1f m  %5zu verts %5zu tris %4zu clusters depth %u  %4zu checkpoints  grid %zu (%zu on road)  pits %zu  walls %zu  tree %zu/%zu\n",
                        e.path.c_str(), r.version, double(r.length), r.vertices.size(), r.triangles.size(), r.clusters.size(), r.tree_depth,
                        r.checkpoints.size(), r.grid.size(), on_road, r.pit_stops.size(), r.wall_vertices.size(), through_tree,
                        (r.triangles.size() + 6) / 7);
        } catch (const std::exception& ex) {
            ++bad;
            std::printf("%-28s %s\n", e.path.c_str(), ex.what());
        }
    }
    std::printf("%zu roads read, %zu failed\n  triangle kinds:", n, bad);
    for (const auto& [k, c] : kinds) std::printf(" %u:%zu", k, c);
    std::printf("\n  triangle flag bits:");
    for (const auto& [k, c] : flag_bits) std::printf(" %u:%zu", k, c);
    std::printf("\n  wall tags: %zu distinct\n", wall_tags.size());
    return bad ? 1 : 0;
}

db::SpecDb open_db(Disc& disc, const std::string& language = "american") {
    gt3r::Volume* vol = &disc.volume();
    return db::SpecDb(
        [vol](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, language);
}

// The car database: its tables; a table's rows; the cars and courses by name.
int cmd_db(Disc& disc, const Args& a) {
    db::SpecDb sdb = open_db(disc);
    if (a.empty()) {
        size_t rows = 0;
        for (const std::string& name : sdb.table_names()) {
            const db::Table* t = sdb.table(name);
            if (!t) continue;
            rows += t->size();
            const db::Labels* l = sdb.labels(name);
            std::printf("%-28s %5zu rows x %3zu bytes  flags %x  %zu labels\n", name.c_str(), t->size(), t->row_size(), t->flags(), l ? l->entries.size() : 0);
        }
        std::printf("%zu tables, %zu rows in %s\n", sdb.table_names().size(), rows, sdb.folder().c_str());
        return 0;
    }
    if (a[0] == "cars") {
        for (const db::CarEntry& c : sdb.cars()) std::printf("%5u  %-32s %-10s %2u paints  %s\n", c.id, c.label.c_str(), c.model.c_str(), c.paints, c.name.c_str());
        return 0;
    }
    if (a[0] == "wheels") {
        for (const db::CarEntry& c : sdb.cars()) {
            db::SpecDb::WheelLayout l;
            const bool ok = sdb.car_wheels(c.label, l);
            std::printf("%-32s %s wheelbase %.3f  width %.3f %.3f  tire diameter %.3f %.3f  tire width %.3f %.3f\n", c.label.c_str(),
                        ok ? "  " : "no", double(l.wheelbase), double(l.outer_width[0]), double(l.outer_width[1]), double(l.tire_diameter[0]),
                        double(l.tire_diameter[1]), double(l.tire_width[0]), double(l.tire_width[1]));
        }
        return 0;
    }
    if (a[0] == "courses") {
        for (const db::CourseEntry& c : sdb.courses()) std::printf("%5u  %-24s %s\n", c.id, c.label.c_str(), c.name.c_str());
        return 0;
    }
    const db::Table* t = sdb.table(a[0]);
    if (!t) {
        std::fprintf(stderr, "error: no table named %s\n", a[0].c_str());
        return 1;
    }
    const db::Labels* l = sdb.labels(a[0]);
    const size_t limit = a.size() > 1 ? size_t(std::atoi(a[1].c_str())) : t->size();
    for (size_t i = 0; i < t->size() && i < limit; ++i) {
        const u32 id = t->ids()[i];
        const std::string* label = l ? l->label(id) : nullptr;
        std::printf("%5u %-28s", id, label ? label->c_str() : "");
        for (u8 b : t->row_at(i)) std::printf(" %02x", b);
        std::printf("\n");
    }
    return 0;
}

// A script's listing, or with no path how many of the disc's scripts read.
int cmd_adc(Disc& disc, const Args& a) {
    if (!a.empty()) {
        const gt3r::Volume::Entry* e = disc.volume().find(a[0]);
        if (!e) {
            std::fprintf(stderr, "error: no such file: %s\n", a[0].c_str());
            return 1;
        }
        const script::Script s = script::Script::parse(disc.volume().read(*e));
        std::fputs(script::disassemble(s.code).c_str(), stdout);
        return 0;
    }
    size_t read = 0, instructions = 0, functions = 0;
    std::map<int, size_t> other;
    std::map<std::string, size_t> ops;
    std::function<void(const script::Code&)> count = [&](const script::Code& c) {
        instructions += c.instructions.size();
        for (const script::Instruction& in : c.instructions) {
            ++ops[script::op_name(in.op)];
            if (in.body) ++functions, count(*in.body);
        }
    };
    for (const gt3r::Volume::Entry& e : disc.volume().entries()) {
        if (e.path.size() < 4 || e.path.compare(e.path.size() - 4, 4, ".adc") != 0) continue;
        const Bytes data = disc.volume().read(e);
        const int v = script::Script::version_of(data);
        if (v != 5) {
            ++other[v];
            continue;
        }
        try {
            count(script::Script::parse(data).code);
            ++read;
        } catch (const std::exception& ex) {
            std::printf("%s: %s\n", e.path.c_str(), ex.what());
        }
    }
    std::printf("%zu scripts read: %zu instructions, %zu functions\n", read, instructions, functions);
    for (const auto& [v, n] : other) std::printf("  %zu of version %d (not read)\n", n, v);
    for (const auto& [name, n] : ops) std::printf("  %-24s %zu\n", name.c_str(), n);
    return 0;
}

// The interpreter on a script file: what REQUIRE names is read from the disc, the program's
// own modules are stand-ins.
script::Vm::Loader script_loader(Disc& disc) {
    return [&disc](const std::string& name) -> std::optional<Bytes> {
        std::string path = name;
        while (!path.empty() && path[0] == '/') path.erase(0, 1);
        const gt3r::Volume::Entry* e = disc.volume().find(path + ".adc");
        if (!e) return std::nullopt;
        return disc.volume().read(*e);
    };
}

void print_module(const script::Module& m, int depth, int max_depth) {
    size_t functions = 0, values = 0;
    std::vector<const script::Module*> inner;
    for (const auto& [name, cell] : m.members) {
        if (auto sub = cell->value.as<script::Module>()) {
            if (sub->parent.lock().get() == &m) inner.push_back(sub.get());
        } else if (cell->value.kind == script::Kind::Function) ++functions;
        else ++values;
    }
    std::printf("%*s%s %s: %zu functions, %zu values, %zu modules\n", depth * 2, "", m.is_class ? "class" : "module", m.name.c_str(),
                functions, values, inner.size());
    if (depth < max_depth)
        for (const script::Module* sub : inner) print_module(*sub, depth + 1, max_depth);
}

int cmd_run(Disc& disc, const Args& a) {
    if (!a.empty()) {
        script::Vm vm(script_loader(disc));
        vm.stub_natives = true, vm.budget = 5'000'000; // a stand-in's answers can keep a loop going
        std::string name = a[0];
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".adc") == 0) name.resize(name.size() - 4);
        vm.require("/" + name);
        std::printf("%llu instructions run\n", static_cast<unsigned long long>(vm.instructions));
        print_module(*vm.root(), 0, 2);
        for (const std::string& line : vm.output) std::printf("print: %s", line.c_str());
        std::printf("asked of the program:\n");
        for (const auto& [what, n] : vm.stub_uses) std::printf("  %5u  %s\n", n, what.c_str());
        return 0;
    }
    size_t ran = 0, failed = 0;
    std::map<std::string, u32> uses;
    std::map<std::string, size_t> errors;
    for (const gt3r::Volume::Entry& e : disc.volume().entries()) {
        if (e.path.size() < 4 || e.path.compare(e.path.size() - 4, 4, ".adc") != 0) continue;
        const Bytes data = disc.volume().read(e);
        if (script::Script::version_of(data) != 5) continue;
        script::Vm vm(script_loader(disc));
        vm.stub_natives = true, vm.budget = 5'000'000;
        try {
            vm.require("/" + e.path.substr(0, e.path.size() - 4));
            ++ran;
        } catch (const std::exception& ex) {
            ++failed;
            if (++errors[ex.what()] == 1) std::printf("%s: %s\n", e.path.c_str(), ex.what());
        }
        for (const auto& [what, n] : vm.stub_uses) uses[what] += n;
    }
    std::printf("%zu scripts ran their top level, %zu stopped\n", ran, failed);
    std::printf("asked of the program (%zu names):\n", uses.size());
    std::vector<std::pair<u32, std::string>> top;
    for (const auto& [what, n] : uses) top.emplace_back(n, what);
    std::sort(top.rbegin(), top.rend());
    for (size_t i = 0; i < top.size() && i < 60; ++i) std::printf("  %6u  %s\n", top[i].first, top[i].second.c_str());
    return 0;
}

menu::FileReader file_reader(Disc& disc) {
    return [&disc](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = disc.volume().find(path);
        if (!e) return std::nullopt;
        return disc.volume().read(*e);
    };
}

// A folder's entries from the volume's flat list.
menu::FolderLister folder_lister(Disc& disc) {
    return [&disc](const std::string& folder) {
        std::vector<std::string> out;
        for (const gt3r::Volume::Entry& e : disc.volume().entries()) {
            if (e.path.compare(0, folder.size(), folder) != 0) continue;
            const size_t slash = e.path.find('/', folder.size());
            std::string name = e.path.substr(folder.size(), slash == std::string::npos ? slash : slash + 1 - folder.size());
            if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
        }
        return out;
    };
}

void print_widget(const menu::Widget& w, int depth, int max_depth) {
    std::string extra;
    if (const auto r = w.rect("geometry")) {
        char b[64];
        std::snprintf(b, sizeof b, "  %g,%g %gx%g", double(r->x), double(r->y), double(r->w), double(r->h));
        extra += b;
    }
    for (const char* p : {"image_path", "key", "text", "font"})
        if (w.has(p)) extra += std::string("  ") + p + "=" + w.text(p);
    std::printf("%*s%s %s%s\n", depth * 2, "", w.type.c_str(), w.name().c_str(), extra.c_str());
    if (depth < max_depth)
        for (const menu::Widget& c : w.children()) print_widget(c, depth + 1, max_depth);
}

int cmd_menu(Disc& disc, const Args& a) {
    const menu::FolderLister list = folder_lister(disc);
    const menu::FileReader read = file_reader(disc);
    // The fonts.
    std::vector<std::string> fonts;
    for (const std::string& f : list("font/")) {
        if (f.size() < 4 || f.compare(f.size() - 4, 4, ".fnt") != 0) continue;
        try {
            for (const gt3r::text::LatinFont& font : menu::read_fonts(*read("font/" + f)).fonts) {
                size_t glyphs = 0;
                for (const gt3r::text::FontGlyph& g : font.glyphs) glyphs += g.defined();
                char b[160];
                std::snprintf(b, sizeof b, "%s: %s, %zu glyphs (%x..%x), line %u, picture %ux%u", f.c_str(), font.name.c_str(), glyphs, font.first,
                              font.last, font.line_height, font.atlas[0].width, font.atlas[0].height);
                fonts.push_back(b);
            }
        } catch (const std::exception& e) {
            std::printf("font/%s: %s\n", f.c_str(), e.what());
        }
    }
    if (a.empty())
        for (const std::string& f : fonts) std::printf("%s\n", f.c_str());
    size_t projects = 0, pages = 0, widgets = 0, images = 0, missing_images = 0, keys = 0, missing_keys = 0;
    std::map<std::string, size_t> types;
    for (const std::string& name : menu::list_projects(list)) {
        if (!a.empty() && a[0] != name) continue;
        const std::vector<std::string> languages = menu::project_languages(list, name);
        try {
            menu::Project project(read, name, languages.empty() ? "US" : languages[0]);
            ++projects;
            size_t p_widgets = 0, p_images = 0, p_missing = 0, p_keys = 0, p_nokey = 0;
            for (const menu::Widget& page : project.pages()) {
                ++pages;
                std::function<void(const menu::Widget&)> walk = [&](const menu::Widget& w) {
                    ++p_widgets, ++types[w.type];
                    for (const menu::Property& p : w.properties) {
                        if (p.name.size() >= 10 && p.name.compare(p.name.size() - 10, 10, "image_path") == 0 && !p.values.empty() && !p.values[0].empty()) {
                            ++p_images;
                            if (!project.image(page.name(), p.values[0])) {
                                if (++p_missing <= 3 && !a.empty()) std::printf("  no picture %s in %s\n", p.values[0].c_str(), page.name().c_str());
                            }
                        }
                        if (p.name == "key" && !p.values.empty() && !p.values[0].empty()) {
                            ++p_keys;
                            if (!project.text().find(project.module_name() + "::" + page.name(), p.values[0])) ++p_nokey;
                        }
                        for (const menu::Widget& c : p.widgets) walk(c);
                    }
                };
                walk(page);
            }
            std::printf("%-20s %-3s %-22s %3zu pages %5zu widgets, pictures %4zu (%zu not found), keys %4zu (%zu without text)\n", name.c_str(),
                        project.language().c_str(), project.module_name().c_str(), project.pages().size(), p_widgets, p_images, p_missing, p_keys, p_nokey);
            widgets += p_widgets, images += p_images, missing_images += p_missing, keys += p_keys, missing_keys += p_nokey;
            if (!a.empty())
                for (const menu::Widget& page : project.pages()) print_widget(page, 1, a.size() > 1 ? 9 : 2);
        } catch (const std::exception& e) {
            std::printf("%s: %s\n", name.c_str(), e.what());
        }
    }
    if (a.empty()) {
        std::printf("%zu projects, %zu pages, %zu widgets; pictures %zu (%zu not found); keys %zu (%zu without text)\n", projects, pages, widgets,
                    images, missing_images, keys, missing_keys);
        for (const auto& [type, n] : types) std::printf("  %-18s %zu\n", type.c_str(), n);
    }
    return 0;
}

// menu-run is in cmd_menu.cpp.
int cmd_menu_run(Disc& disc, const Args& a) { return gt4tool::cmd_menu_run(disc, a); }

struct Command {
    const char* name;
    const char* usage;
    size_t min_args;
    int (*run)(Disc&, const Args&);
};
const Command kCommands[] = {
    {"info", "<disc>", 0, cmd_info},
    {"ls", "<disc> [folder]", 0, cmd_ls},
    {"survey", "<disc>", 0, cmd_survey},
    {"extract", "<disc> <prefix> <outdir>", 2, cmd_extract},
    {"tex", "<disc> <path> <outdir>", 2, cmd_tex},
    {"adc", "<disc> [path]", 0, cmd_adc},
    {"menu", "<disc> [project] [all]", 0, cmd_menu},
    {"menu-run", "<disc> <project> [steps...]", 1, cmd_menu_run},
    {"menu-names", "<disc>", 0, [](Disc& d, const Args& a) { return gt4tool::cmd_menu_names(d, a); }},
    {"db-strings", "<disc> <pool> [index...]", 1, [](Disc& d, const Args& a) { return gt4tool::cmd_db_strings(d, a); }},
    {"run", "<disc> [path]", 0, cmd_run},
    {"db", "<disc> [cars | wheels | courses | <table> [rows]]", 0, cmd_db},
    {"model", "<disc> <path>", 1, cmd_model},
    {"runway-survey", "<disc> [prefix]", 0, cmd_runway_survey},
    {"model-survey", "<disc> [prefix] [textures]", 0, cmd_model_survey},
    {"sound", "<disc> survey | bank <path> | effects | music | seq... | se-wav... | car... (see cmd_sound.cpp)", 1, cmd_sound},
    {"race", "<disc> rail <course> [seconds] | sections <course>", 2, cmd_race},
    {"ai", "<disc> <course> [cars] [laps] | lines <course> [line] | survey (cmd_ai.cpp)", 1, [](Disc& d, const Args& a) { return gt4r::tool::cmd_ai(d, a); }},
};

} // namespace

namespace gt4r::tool { int sim_main(int argc, char** argv); void sim_usage(); } // cmd_sim.cpp

int main(int argc, char** argv) {
    if (const int r = gt4r::tool::sim_main(argc, argv); r >= 0) return r;
    if (argc >= 3)
        for (const Command& c : kCommands) {
            if (std::strcmp(argv[1], c.name) != 0) continue;
            const Args args(argv + 3, argv + argc);
            if (args.size() < c.min_args) break;
            try {
                gt3r::log_set_level(gt3r::LogLevel::Warn);
                const std::unique_ptr<Disc> disc = Disc::open(argv[2]);
                return c.run(*disc, args);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "error: %s\n", e.what());
                return 1;
            }
        }
    std::fprintf(stderr, "usage: gt4tool <command> <disc.iso|disc.chd> [args]\n\n");
    for (const Command& c : kCommands) std::fprintf(stderr, "  %-13s %s\n", c.name, c.usage);
    gt4r::tool::sim_usage();
    return 2;
}
