// The model set converter on a made-up set, then on the discs of GT4R_DISC.

#include "test_framework.h"

#include "formats/model/gtm1.h"
#include "gt4r/formats/model/mdls.h"

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
    }
}
