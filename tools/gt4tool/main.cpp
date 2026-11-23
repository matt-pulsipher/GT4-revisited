// gt4tool: command line access to a Gran Turismo 4 disc image (your own), for format research.
//
//   gt4tool info <disc>                    identify the disc, its volumes and its program
//   gt4tool ls <disc> [folder]             list a folder of the volumes
//   gt4tool survey <disc>                  count the files by folder, extension and magic
//   gt4tool extract <disc> <prefix> <dir>  write the files under a prefix (unpacked) to a folder
//   gt4tool tex <disc> <path> <dir>        decode every texture set in a file to PNGs
//   gt4tool model <disc> <path>            describe a file's model sets as decoded
//   gt4tool runway-survey <disc> [prefix]  read every course's road
//   gt4tool model-survey <disc> [prefix] [textures]   decode every model set under a prefix
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
#include "gt4r/formats/model/mdls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace gt4r;

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
    {"model", "<disc> <path>", 1, cmd_model},
    {"runway-survey", "<disc> [prefix]", 0, cmd_runway_survey},
    {"model-survey", "<disc> [prefix] [textures]", 0, cmd_model_survey},
};

} // namespace

int main(int argc, char** argv) {
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
    return 2;
}
