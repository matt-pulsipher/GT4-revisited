// The model set converter on a made-up set, then on the discs of GT4R_DISC.

#include "test_framework.h"

#include "formats/model/gtm1.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/formats/model/model_script.h"

#include <cmath>
#include <cstring>

using namespace gt4r;

namespace {

void put16(Bytes& b, size_t o, u32 v) { b[o] = u8(v), b[o + 1] = u8(v >> 8); }
void put32(Bytes& b, size_t o, u32 v) { std::memcpy(b.data() + o, &v, 4); }
void putf(Bytes& b, size_t o, float f) { std::memcpy(b.data() + o, &f, 4); }
void word(Bytes& b, u32 v) {
    b.resize(b.size() + 4);
    put32(b, b.size() - 4, v);
}
void unpack(Bytes& b, u32 vn, u32 vl, u32 addr, u32 count, std::initializer_list<int> values, bool masked = false) {
    word(b, (0x60u | vn << 2 | vl | (masked ? 0x10u : 0u)) << 24 | count << 16 | 0x8000u | addr);
    for (int v : values) {
        if (vl == 1) b.push_back(u8(v)), b.push_back(u8(v >> 8));
        else b.push_back(u8(v));
    }
    while (b.size() & 3) b.push_back(0);
}

// One model drawing one shape of one packed packet: a strip of four vertices.
Bytes make_set() {
    Bytes b(0x200, 0);
    std::memcpy(b.data(), "MDLS", 4);
    put16(b, 0x16, 1), put16(b, 0x18, 1), put16(b, 0x1A, 1);
    put32(b, 0x38, 0x80), put32(b, 0x3C, 0xB0), put32(b, 0x40, 0xC0), put32(b, 0x44, 0x1F0);
    // Model record: radius 2, centre (0, 1, 0), program at 0x110.
    putf(b, 0x80 + 0x08, 2.0f), putf(b, 0x80 + 0x10, 1.0f);
    put32(b, 0x80 + 0x18, 0x110);
    put32(b, 0xB0, 0x140); // the shape
    putf(b, 0xC0 + 0x10, 0.5f), putf(b, 0xC0 + 0x40, 16.0f); // a material: diffuse red 0.5, shininess
    // The program: the script, a register switch whose first branch draws shape 0 between a
    // callback and a cull-face command, the other branch nothing.
    const u8 program[] = {0x43, 0x4D, 0x05, 0x00, 0x02, 0x05, 0x00, 0x08, 0x00, 0x00, // 0x110: switch -> 0x11A, 0x11D
                          0x59, 0x27, 0x4F, 0x04, 0x00, 0x4E, 0x00};                   // 0x11A: callback, face, draw 0, face, end
    std::memcpy(b.data() + 0x110, program, sizeof program);
    b[0x110 + 5] = 0x05, b[0x110 + 6] = 0x00; // rel of branch 0, from 0x115: 0x11A
    b[0x110 + 7] = 0x09, b[0x110 + 8] = 0x00; // rel of branch 1, from 0x117: 0x120 (the end)
    // The shape: scale 0.01, kind 1 with packed data, one packet.
    putf(b, 0x140 + 4, 0.01f);
    put16(b, 0x140 + 8, 0x4081), put16(b, 0x140 + 10, 1), put16(b, 0x140 + 12, 4), put16(b, 0x140 + 14, 2);
    Bytes p;
    word(p, 0x6801C0C0); // GIF tag: 4 vertices, a strip
    word(p, 0x00008004), word(p, 0x301E4000), word(p, 0x00001412);
    unpack(p, 2, 1, 0x00, 4, {100, 0, 0, 100, 200, 0, -100, 0, 0, -100, 200, 0}); // positions x 0.01
    unpack(p, 2, 2, 0x80, 4, {0, 0, 127, 0, 0, 127, 0, 0, 127, 0, 0, 127});       // normals / 127
    unpack(p, 1, 1, 0x40, 2, {0, 0, 4096, 0});                                    // two coordinates, 16 bits
    word(p, 0x30000000), word(p, 2048), word(p, 2048), word(p, 0), word(p, 0);    // STROW 2048, 2048
    word(p, 0x05000001);                                                          // STMOD 1: offsets from the row
    unpack(p, 1, 2, 0x42, 2, {-128, 127, 0, -100});                               // two more as bytes: 1920, 2175; 2048, 1948
    word(p, 0x05000000);
    word(p, 0x20000000), word(p, 0x3F3F3F3F);
    word(p, 0x7202C040), p.push_back(1), p.push_back(6), p.push_back(0), p.push_back(0); // strips: one of 4 vertices
    while (p.size() & 15) p.push_back(0);
    put32(b, 0x140 + 0x10, 0x20), put16(b, 0x140 + 0x14, u32(p.size() / 16)), put16(b, 0x140 + 0x16, 1 << 9);
    b.resize(0x160);
    b.insert(b.end(), p.begin(), p.end());
    while (b.size() & 15) b.push_back(0);
    // (No texture lists: count 0.)
    put32(b, 0x44, 0);
    put32(b, 0x10, u32(b.size()));
    return b;
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

} // namespace

TEST(mdls_becomes_gtm1) {
    const Bytes set = make_set();
    CHECK(mdls::is_mdls(set));
    const mdls::Header h = mdls::read_header(set);
    CHECK(h.models == 1 && h.shapes == 1 && h.materials == 1 && h.texture_lists == 0);
    CHECK(mdls::find_model_sets(set) == std::vector<size_t>{0});

    mdls::ConvertStats st;
    const Bytes block = mdls::to_gtm1(set, &st);
    CHECK(st.shapes == 1 && st.packets == 1 && st.packed_packets == 1 && st.unknown_unpacks == 0);
    CHECK(st.replaced_commands == 5); // the script, the switch, the callback, the two face commands

    const gt3r::gtm1::Gtm1 g = gt3r::gtm1::Gtm1::parse(block);
    CHECK(g.nodes.size() == 1 && g.chunks.size() == 1 && g.materials.size() == 1);
    CHECK(near(g.spheres[0][1], 1.0f) && near(g.spheres[0][3], 2.0f));
    CHECK(near(g.materials[0].rows[1][0], 0.5f) && near(g.materials[0].shininess, 16.0f));
    CHECK((g.chunks[0].flags & 0xFF) == 1 && g.chunks[0].packets.size() == 1);
    // The program draws the shape once (the switch's first branch).
    const std::vector<gt3r::gtm1::Draw> draws = g.run(0);
    CHECK(draws.size() == 1 && draws[0].chunk == 0);
    // The packet's vertices as floats.
    const gt3r::gtm1::DecodedPacket p = g.decode_packet(g.chunks[0].packets[0]);
    CHECK(p.vertices.size() == 4 && p.strips == std::vector<u32>{4} && p.has_normals && p.has_uv);
    if (p.vertices.size() == 4) {
        CHECK(near(p.vertices[0].pos[0], 1.0f) && near(p.vertices[1].pos[1], 2.0f) && near(p.vertices[2].pos[0], -1.0f));
        CHECK(near(p.vertices[0].normal[2], 1.0f));
        CHECK(near(p.vertices[1].uv[0], 1.0f) && near(p.vertices[1].uv[1], 0.0f));
        CHECK(near(p.vertices[2].uv[0], 1920.0f / 4096) && near(p.vertices[2].uv[1], 2175.0f / 4096));
        CHECK(near(p.vertices[3].uv[0], 2048.0f / 4096) && near(p.vertices[3].uv[1], 1948.0f / 4096));
    }
    // The whole set through the shared decoder.
    const gt3r::scene::Model m = mdls::build_model(set);
    CHECK(m.meshes.size() == 1 && m.meshes[0].indices.size() == 6);

    // A truncated block and a program with a command no table has.
    bool threw = false;
    try {
        mdls::read_header(ByteSpan(set).first(0x100));
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
    Bytes bad = set;
    bad[0x110] = 0x7F;
    threw = false;
    try {
        mdls::to_gtm1(bad);
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);

    // The reflection factors: 0x57 (constant) and 0x56 (the draw's) become GTM1's 0x36 / 0x32 in a copy of the
    // program, C1 = -0 marking the constant ones; the jump between them is aimed again.
    Bytes factors = set;
    const u8 program[] = {0x57, 0x08, 0x02, 0x00, 0x04, 0x00, 0x56, 0x04, 0x00, 0x00}; // 0x111: jump over the end to 0x114
    std::memset(factors.data() + 0x110, 0, 0x20);
    std::memcpy(factors.data() + 0x110, program, sizeof program);
    const Bytes factors_block = mdls::to_gtm1(factors);
    const gt3r::gtm1::Gtm1 fg = gt3r::gtm1::Gtm1::parse(factors_block);
    const std::vector<gt3r::gtm1::Draw> fd = fg.run(0);
    CHECK(fd.size() == 2);
    if (fd.size() == 2) {
        CHECK(fd[0].env_c0[0] == 1 && fd[0].env_c1[0] == 0 && std::signbit(fd[0].env_c1[0]));
        CHECK(fd[1].env_c0[0] == 1 && fd[1].env_c1[0] == 0 && !std::signbit(fd[1].env_c1[0]));
    }
}

TEST(disc_models_decode) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        if (disc->info().game != Game::GT4) continue;
        // Every tire, wing and prize object, and a car body with its textures.
        size_t sets = 0, built = 0;
        for (const gt3r::Volume::Entry& e : disc->volume().entries()) {
            const bool wanted = e.path.compare(0, 5, "tire/") == 0 || e.path.compare(0, 5, "wing/") == 0 || e.path.compare(0, 12, "piece/prize/") == 0;
            if (!wanted) continue;
            const Bytes data = disc->volume().read(e);
            for (size_t off : mdls::find_model_sets(data)) {
                ++sets;
                mdls::Options o;
                o.textures = false;
                built += !mdls::build_model(ByteSpan(data).subspan(off), o).meshes.empty();
            }
        }
        CHECK(sets > 200 && built == sets);
        const gt3r::Volume::Entry* car = disc->volume().find("car/lod/ac__0014");
        CHECK(car != nullptr);
        if (!car) continue;
        const Bytes data = disc->volume().read(*car);
        const std::vector<size_t> found = mdls::find_model_sets(data);
        CHECK(found.size() >= 3);
        const gt3r::scene::Model body = mdls::build_model(ByteSpan(data).subspan(found[0]));
        size_t triangles = 0;
        for (const gt3r::scene::Mesh& m : body.meshes) triangles += m.indices.size() / 3;
        CHECK(triangles > 3000 && body.textures.size() > 20);
        gt3r::scene::Model b = body;
        b.compute_bounds();
        CHECK(b.bounds_max[2] - b.bounds_min[2] > 3.0f && b.bounds_max[2] - b.bounds_min[2] < 5.0f); // a car's length in metres
        // A body whose program draws reflection passes with its constant factors (0x57) and the draw's (0x56).
        if (const gt3r::Volume::Entry* civic = disc->volume().find("car/lod/hond0077")) {
            const Bytes cd = disc->volume().read(*civic);
            const Bytes block = mdls::to_gtm1(ByteSpan(cd).subspan(mdls::find_model_sets(cd).at(0)));
            const gt3r::gtm1::Gtm1 g = gt3r::gtm1::Gtm1::parse(block);
            size_t plain = 0, by_draw = 0;
            for (const gt3r::gtm1::Draw& d : g.run(0))
                if ((g.chunks[d.chunk].flags & 0x1F) == 2) (std::signbit(d.env_c1[0]) ? plain : by_draw)++;
            CHECK(plain >= 1 && by_draw > plain);
            // Every car's sets convert, the programs that switch the factors copied.
            size_t sets = 0, converted = 0;
            for (const gt3r::Volume::Entry& e : disc->volume().entries()) {
                if (e.path.compare(0, 8, "car/lod/") != 0 || e.path.find('.') != std::string::npos) continue;
                const Bytes data = disc->volume().read(e);
                for (size_t off : mdls::find_model_sets(data)) try {
                        ++sets;
                        converted += !mdls::to_gtm1(ByteSpan(data).subspan(off)).empty();
                    } catch (const std::exception& x) {
                        std::printf("  %s +0x%zx: %s\n", e.path.c_str(), off, x.what());
                    }
            }
            CHECK(sets > 2000 && converted == sets);
            std::printf("  hond0077: %zu reflection passes by the draw's factors, %zu constant\n", by_draw, plain);
        }
    }
}

TEST(course_container_reads) {
    // A made-up course: the table, a model set in slot 1, an environment block in slot 32.
    Bytes file(course::kSlotCount * 4, 0);
    const Bytes set = make_set();
    put32(file, 4 * course::kSlotWorld, u32(file.size()));
    file.insert(file.end(), set.begin(), set.end());
    while (file.size() & 0xFF) file.push_back(0);
    put32(file, 4 * course::kSlotEnv, u32(file.size()));
    Bytes env(0x80, 0);
    std::memcpy(env.data(), "Env0", 4);
    putf(env, 0x38, 2.0f);
    file.insert(file.end(), env.begin(), env.end());
    CHECK(course::CourseFile::looks_like_course(file));
    const course::CourseFile c = course::CourseFile::parse(file);
    CHECK(c.sections().size() == 2 && c.find(course::kSlotWorld) && c.find(course::kSlotWorld)->tag == "MDLS");
    CHECK(c.section(file, course::kSlotEnv).size() == 0x80 && c.section(file, course::kSlotSky).empty());
    CHECK(course::scenery_color_scale(c.section(file, course::kSlotEnv)) == 2.0f);
    CHECK(course::scenery_color_scale({}) == 1.0f);
    const gt3r::scene::Model m = course::build_course_model(file);
    CHECK(m.meshes.size() == 1 && m.meshes[0].name.compare(0, 6, "world/") == 0);
    CHECK(!course::CourseFile::looks_like_course(set));
}

TEST(disc_courses_decode) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        if (disc->info().game != Game::GT4) continue;
        size_t courses = 0, with_road = 0;
        for (const gt3r::Volume::Entry& e : disc->volume().entries()) {
            if (e.path.compare(0, 4, "crs/") != 0 || e.path.find('/', 4) != std::string::npos || e.path.find('.') != std::string::npos) continue;
            const Bytes data = disc->volume().read(e);
            CHECK(course::CourseFile::looks_like_course(data));
            const course::CourseFile c = course::CourseFile::parse(data);
            ++courses;
            with_road += c.find(course::kSlotRunway) && c.find(course::kSlotRunway)->tag == "RNW4";
            CHECK(c.find(course::kSlotEnv) && c.find(course::kSlotEnv)->tag == "Env0");
            if (e.path == "crs/tsukuba") {
                course::ModelOptions o;
                o.textures = false;
                gt3r::scene::Model m = course::build_course_model(data, o);
                CHECK(m.meshes.size() > 500);
                // A two-kilometre circuit: its scenery spans several hundred metres either way.
                CHECK(m.bounds_max[0] - m.bounds_min[0] > 500 && m.bounds_max[0] - m.bounds_min[0] < 5000);
            }
        }
        CHECK(courses == 110 && with_road == courses);
    }
}

TEST(runway_reads_and_finds_the_road) {
    // A made-up road: a 20 x 10 m slope of two cells, each two triangles, split at x = 10 by a
    // tree of depth 1.
    Bytes d(0xA0, 0);
    std::memcpy(d.data(), "RNW4", 4);
    put32(d, 0x10, 4);
    putf(d, 0x14, 123.0f);
    putf(d, 0x20, 0), putf(d, 0x24, 0), putf(d, 0x28, 0), putf(d, 0x2C, 20), putf(d, 0x30, 2), putf(d, 0x34, 10);
    put16(d, 0x3A, 1), put16(d, 0x46, 6), put16(d, 0x48, 4), put16(d, 0x4A, 2), put16(d, 0x4C, 1);
    auto at = [&](size_t header) {
        while (d.size() & 15) d.push_back(0);
        put32(d, header, u32(d.size()));
        return d.size();
    };
    size_t o = at(0x5C); // grid: one place at (5, 0.5, 5)
    d.resize(o + 0x10, 0);
    putf(d, o, 5), putf(d, o + 4, 0.5f), putf(d, o + 8, 5), putf(d, o + 12, 1.5f);
    o = at(0x74); // vertices: x 0, 10, 20 at z 0 and 10; the height rises with x (y = x / 10)
    d.resize(o + 6 * 0x10, 0);
    for (int i = 0; i < 6; ++i) putf(d, o + 0x10 * size_t(i), float(10 * (i % 3))), putf(d, o + 0x10 * size_t(i) + 4, float(i % 3)), putf(d, o + 0x10 * size_t(i) + 8, float(10 * (i / 3)));
    o = at(0x78);
    d.resize(o + 4 * 0x0C, 0);
    const u16 tris[4][3] = {{0, 1, 3}, {1, 4, 3}, {1, 2, 4}, {2, 5, 4}};
    for (int t = 0; t < 4; ++t) {
        for (int k = 0; k < 3; ++k) put16(d, o + 0x0C * size_t(t) + 2 * size_t(k), tris[t][k]);
        d[o + 0x0C * size_t(t) + 7] = u8(t >= 2 ? 8 : 0); // the far cell is another kind
    }
    const size_t lists = at(0x94); // (the clusters' triangle lists; the header slot is not read)
    d.resize(lists + 8, 0);
    put16(d, lists, 0), put16(d, lists + 2, 1), put16(d, lists + 4, 2), put16(d, lists + 6, 3);
    o = at(0x7C);
    d.resize(o + 2 * 0x10, 0);
    put16(d, o, 2), put32(d, o + 8, u32(lists));
    put16(d, o + 0x10, 2), put32(d, o + 0x18, u32(lists + 4));
    o = at(0x80);
    d.push_back(u8(0 << 6 | 31)); // split x at 0 + 20 x 31.5 / 64 = 9.84
    put32(d, 0x08, u32(d.size()));

    const course::Runway r = course::Runway::parse(d);
    CHECK(r.version == 4 && r.length == 123.0f && r.vertices.size() == 6 && r.triangles.size() == 4 && r.clusters.size() == 2);
    CHECK(r.grid.size() == 1 && r.grid[0].pos.x == 5 && r.grid[0].heading == 1.5f);
    CHECK(r.cluster_at(3, 5) == 0 && r.cluster_at(15, 5) == 1);
    float y = 0;
    int tri = -1;
    CHECK(r.height_at(5, 5, 10, y, &tri) && std::fabs(y - 0.5f) < 1e-4f && tri <= 1);
    CHECK(r.height_at(15, 2, 10, y, &tri) && std::fabs(y - 1.5f) < 1e-4f && tri >= 2 && r.triangles[size_t(tri)].kind == 8);
    CHECK(!r.height_at(25, 5, 10, y));     // off the road
    CHECK(!r.height_at(15, 2, 1.0f, y));   // the road there is above the limit
    CHECK(course::make_road_mesh(r).indices.size() == 12);
}

TEST(disc_roads_read) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        if (disc->info().game != Game::GT4) continue;
        size_t roads = 0, grids_on_road = 0;
        for (const gt3r::Volume::Entry& e : disc->volume().entries()) {
            if (e.path.compare(0, 4, "crs/") != 0 || e.path.find('/', 4) != std::string::npos || e.path.find('.') != std::string::npos) continue;
            const Bytes data = disc->volume().read(e);
            const course::CourseFile c = course::CourseFile::parse(data);
            const course::Runway r = course::Runway::parse(c.section(data, course::kSlotRunway));
            ++roads;
            CHECK(r.length > 100 && r.length < 30000 && !r.triangles.empty() && !r.checkpoints.empty());
            float y;
            if (!r.grid.empty() && r.height_at(r.grid[0].pos.x, r.grid[0].pos.z, r.grid[0].pos.y + 2, y)) ++grids_on_road;
            if (e.path == "crs/tsukuba") CHECK(r.length > 2000 && r.length < 2200); // a two-kilometre lap (the file: 2115.1 m)
        }
        CHECK(roads == 110 && grids_on_road > 90);
    }
}

TEST(paint_patches_apply) {
    const Bytes set = make_set();
    // Two paints of two patches: the material's diffuse red and its shininess.
    Bytes pat(0x20 + 16, 0);
    std::memcpy(pat.data(), "Pat0", 4);
    put16(pat, 0x10, 2), put16(pat, 0x12, 2);
    auto patch = [&](size_t slot, u32 target, float value) {
        put32(pat, 0x20 + 4 * slot, u32(pat.size()));
        pat.resize(pat.size() + 12, 0);
        put32(pat, pat.size() - 12, target), put32(pat, pat.size() - 8, 4), putf(pat, pat.size() - 4, value);
    };
    patch(0, 0xC0 + 0x10, 0.25f), patch(1, 0xC0 + 0x40, 8.0f);
    patch(2, 0xC0 + 0x10, 0.75f), patch(3, 0xC0 + 0x40, 32.0f);
    CHECK(car::paint_count(pat) == 2 && car::paint_count(set) == 0);
    for (u32 paint = 0; paint < 2; ++paint) {
        const Bytes painted = car::apply_paint(set, pat, paint);
        const Bytes block = mdls::to_gtm1(painted);
        const gt3r::gtm1::Gtm1 g = gt3r::gtm1::Gtm1::parse(block);
        CHECK(near(g.materials[0].rows[1][0], paint ? 0.75f : 0.25f) && near(g.materials[0].shininess, paint ? 32.0f : 8.0f));
    }
    CHECK(car::apply_paint(set, pat, 5) == Bytes(set.begin(), set.end())); // no such paint: as stored
}

TEST(disc_cars_assemble) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        if (disc->info().game != Game::GT4) continue;
        gt3r::Volume* vol = &disc->volume();
        db::SpecDb sdb(
            [vol](const std::string& path) -> std::optional<Bytes> {
                const gt3r::Volume::Entry* e = vol->find(path);
                return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
            },
            [vol](const std::string& dir) { return vol->list_dir(dir); });
        db::SpecDb::WheelLayout l;
        CHECK(sdb.car_wheels("ac_427_sc_66", l));
        CHECK(near(l.wheelbase, 2.286f) && near(l.tire_diameter[0], 0.610f) && near(l.tire_diameter[1], 0.620f));
        CHECK(near(l.tire_width[0], 0.245f) && near(l.tire_width[1], 0.275f) && near(l.outer_width[0], 1.684f));
        car::Wheels w;
        w.wheelbase = l.wheelbase;
        for (int k = 0; k < 2; ++k) w.outer_width[k] = l.outer_width[k], w.tire_diameter[k] = l.tire_diameter[k], w.tire_width[k] = l.tire_width[k];
        const Bytes file = vol->read(*vol->find("car/lod/ac__0014"));
        const Bytes pat = vol->read(*vol->find("car/lod/ac__0014.pat"));
        CHECK(car::CarFile::is_car(file) && car::paint_count(pat) == 16);
        car::Options o;
        o.textures = false;
        const gt3r::scene::Model whole = car::build_car(file, pat, &w, o, "cobra");
        size_t tires = 0, wheels = 0;
        for (const gt3r::scene::Mesh& m : whole.meshes) tires += m.name == "tire", wheels += m.name == "wheel";
        CHECK(tires == 4 && wheels == 4);
        // The tires stand on the ground and stay within the car's width.
        CHECK(std::fabs(whole.bounds_min[1]) < 0.15f && whole.bounds_max[0] < 0.95f && whole.bounds_min[0] > -0.95f);
        // Every car has wheel data and a file that parses.
        size_t ok = 0;
        const std::vector<db::CarEntry> cars = sdb.cars();
        for (const db::CarEntry& c : cars) {
            db::SpecDb::WheelLayout each;
            const gt3r::Volume::Entry* e = vol->find("car/lod/" + c.model);
            if (!e || !sdb.car_wheels(c.label, each)) continue;
            ok += each.wheelbase > 1.0f && each.wheelbase < 4.5f && each.tire_diameter[0] > 0.3f && each.tire_width[0] > 0.08f;
        }
        CHECK(ok + 3 >= cars.size()); // (all but the three oldest cars, whose wheels are narrower than the check allows)
        std::printf("  %zu of %zu cars have a wheel layout\n", ok, cars.size());
    }
}

// A car body's brake lamps by script (bmw_0002): the lit lenses drawn under 0x54 with the alpha `gtala`, which the script sets
// 1 while `brake` is above 0: hidden with the brake off, drawn with it on (more triangles), every register 0 as before (still).
TEST(disc_brake_lamps_by_script) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    bool ran = false;
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        const gt3r::Volume::Entry* e = disc->volume().find("car/lod/bmw_0002");
        if (!e) continue;
        const Bytes f = disc->volume().read(*e);
        const std::vector<size_t> sets = mdls::find_model_sets(f);
        if (sets.empty()) continue;
        const ByteSpan body = ByteSpan(f).subspan(sets[0]);
        const mdls::Script script = mdls::Script::parse(body);
        CHECK(!script.empty() && script.host_cell("brake") >= 0);
        auto triangles = [&](float brake) {
            mdls::Options o;
            o.model = 0, o.textures = false;
            const std::pair<std::string, float> hosts[] = {{"brake", brake}};
            o.scene.registers = mdls::still_registers(script, hosts);
            size_t n = 0;
            for (const auto& m : mdls::build_model(body, o).meshes) n += m.indices.size() / 3;
            return n;
        };
        const size_t off = triangles(0), on = triangles(1);
        std::printf("  bmw_0002: %zu triangles with the brake off, %zu on\n", off, on);
        CHECK(off > 0 && on > off);
        ran = true;
    }
    if (!ran) SKIP("no Gran Turismo 4 US disc");
}
