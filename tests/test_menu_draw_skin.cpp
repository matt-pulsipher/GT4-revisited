// Skinned model sets and their motions: the skin blocks on made-up bytes, the motion clock, then
// the GT Auto mechanic of the US disc (GT4R_DISC) in its stored pose and walking in.

#include "test_framework.h"

#include "gt4r/formats/model/mdls.h"
#include "gt4r/formats/model/mdls_skin.h"
#include "gt4r/menu/image_pack.h"
#include "gt4r/menu/model_motion.h"

#include <cmath>
#include <cstring>
#include <functional>

using namespace gt4r;
namespace render = gt3r::render;

namespace menu_draw_skin_test {

bool throws(const std::function<void()>& f) {
    try {
        f();
    } catch (const FormatError&) {
        return true;
    }
    return false;
}

void put16(Bytes& b, u32 v) { b.push_back(u8(v)), b.push_back(u8(v >> 8)); }
void putf(Bytes& b, float f) {
    u8 x[4];
    std::memcpy(x, &f, 4);
    b.insert(b.end(), x, x + 4);
}

// The packed block: a scale, the count and the normals' place, then groups of bones and 13-byte
// vertices (s16 position x scale, s8 normal / 127, s8 weights / 127).
TEST(mdls_skin_packed_block) {
    Bytes b;
    putf(b, 0.001f);
    b.push_back(3), b.push_back(0x80);
    b.push_back(0x80 | 2), b.push_back(4), b.push_back(5), b.push_back(7), b.push_back(0), b.push_back(0);
    for (int i = 0; i < 2; ++i) {
        put16(b, u32(1000 * (i + 1))), put16(b, u32(-500 & 0xFFFF)), put16(b, 0);
        b.push_back(0), b.push_back(127), b.push_back(0);
        b.push_back(100), b.push_back(27), b.push_back(0), b.push_back(0);
    }
    b.push_back(1), b.push_back(4), b.push_back(9), b.push_back(0), b.push_back(0), b.push_back(0);
    put16(b, 0), put16(b, 0), put16(b, 2000);
    b.push_back(127), b.push_back(0), b.push_back(0);
    b.push_back(127), b.push_back(0), b.push_back(0), b.push_back(0);
    const mdls::SkinBlock s = mdls::read_skin_block(b, 0, true);
    CHECK(s.dest == 0x80 && s.vertices.size() == 3 && s.end == b.size());
    CHECK(std::fabs(s.vertices[1].pos[0] - 2.0f) < 1e-5f && std::fabs(s.vertices[1].pos[1] + 0.5f) < 1e-5f);
    CHECK(s.vertices[0].bones[0] == 5 && s.vertices[0].bones[1] == 7 && std::fabs(s.vertices[0].weights[0] - 100.0f / 127) < 1e-6f);
    CHECK(s.vertices[2].bones[0] == 9 && s.vertices[2].weights[0] == 1.0f && s.vertices[2].normal[0] == 1.0f);
    b.resize(b.size() - 5);
    CHECK(throws([&] { mdls::read_skin_block(b, 0, true); }));
}

// A motion's clock: time 0 until played, 1/60 s a picture, held past the end without a loop,
// back by length - loop start with one.
TEST(menu_model_motion_clock) {
    menu::MotionClock c;
    bool playing = true;
    CHECK(c.time(500, 2, 0, &playing) == 0 && !playing);
    c.play(100);
    CHECK(std::fabs(c.time(160, 2, 0.5f, &playing) - 1.0f) < 1e-6f && playing);
    CHECK(std::fabs(c.time(100 + 150, 2, 0.5f, &playing) - 1.0f) < 1e-4f && playing); // 2.5 s: 0.5 + (2.5 - 0.5) mod 1.5
    c.loop = false;
    c.time(100 + 120, 2, 0.5f, &playing);
    CHECK(playing);
    CHECK(std::fabs(c.time(100 + 400, 2, 0.5f, &playing) - 121.0f / 60) < 1e-5f && !playing);
    c.stop(130);
    CHECK(c.steps(1000) == 30);
}

std::optional<Bytes> gtauto_entry(Disc& d, const char* name) {
    const gt3r::Volume::Entry* e = d.volume().find("projects/gtmode/US/GTAutoRoot.gpb");
    if (!e) return std::nullopt;
    static Bytes file;
    file = d.volume().read(*e);
    const menu::ImagePack pack = menu::ImagePack::parse(file);
    const menu::ImagePack::Entry* en = pack.find(name);
    if (!en) return std::nullopt;
    return pack.read(*en);
}

// The mechanic: 20 bones, every skinned vertex found again in the decoded model, a man 1.8 m tall
// standing on the floor; walked in by GTAutoEntranceMotion he stands beside the car, feet down.
TEST(disc_gtauto_mechanic_skinned_and_walking) {
    Disc* gt4 = nullptr;
    for (const auto& disc : test::discs())
        if (disc->info().game == Game::GT4) gt4 = disc.get();
    if (!gt4) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const std::optional<Bytes> man = gtauto_entry(*gt4, "image/gtmode/gtauto/pitmanFix.mdl");
    const std::optional<Bytes> walk = gtauto_entry(*gt4, "image/gtmode/gtauto/GTAutoEntranceMotion.mot");
    CHECK(man && walk);
    if (!man || !walk) return;
    const std::vector<mdls::Bone> bones = mdls::read_bones(*man);
    CHECK(bones.size() == 20);
    bool pelvis = false;
    for (const mdls::Bone& b : bones) pelvis |= b.name == "PELVIS" && b.index == 19;
    CHECK(pelvis);
    const std::vector<mdls::Symbol> symbols = mdls::read_symbols(*man);
    CHECK(symbols.size() == 1 && symbols[0].name == "SKINNING" && symbols[0].value == 0);
    const mdls::SkinTable table = mdls::SkinTable::of(*man);
    CHECK(table.size() > 500);
    mdls::Options mo;
    const gt3r::scene::Model model = mdls::build_model(*man, mo, "pitman");
    size_t vertices = 0, found = 0;
    float lo = 1e9f, hi = -1e9f;
    for (const gt3r::scene::Mesh& m : model.meshes)
        for (const gt3r::scene::Vertex& v : m.vertices) {
            ++vertices, found += table.find(v.pos, v.normal) != nullptr;
            lo = std::min(lo, v.pos[1]), hi = std::max(hi, v.pos[1]);
        }
    for (const gt3r::scene::Mesh& m : model.meshes)
        for (const gt3r::scene::Primitive& p : m.primitives) {
            float y = 0; int k = 0;
            for (u32 i = p.first_index; i < p.first_index + p.index_count; ++i) y += m.vertices[m.indices[i]].pos[1], ++k;
            const gt3r::scene::Vertex& v0 = m.vertices[m.indices[p.first_index]];
            std::printf("  prim tex %d flags %x tris %u mean y %.2f uv %.2f %.2f col %08x\n", p.texture, p.flags, p.index_count / 3, double(k ? y / float(k) : 0), double(v0.uv[0]), double(v0.uv[1]), v0.color);
        }
    for (const gt3r::scene::Texture& t : model.textures) { u64 r = 0, g = 0, b = 0; for (u32 c : t.rgba) r += c & 0xFF, g += (c >> 8) & 0xFF, b += (c >> 16) & 0xFF; const u64 n = t.rgba.size() ? t.rgba.size() : 1; std::printf("  tex %ux%u avg %llu %llu %llu\n", t.width, t.height, r / n, g / n, b / n); }
    std::printf("  mechanic: %zu vertices, %zu skinned, height %.3f..%.3f, %zu textures\n", vertices, found, double(lo), double(hi), model.textures.size());
    CHECK(vertices > 1000 && found == vertices);
    CHECK(std::fabs(lo) < 0.02f && std::fabs(hi - 1.82f) < 0.03f);

    const menu::SkeletonMotion sm = menu::SkeletonMotion::parse(*walk);
    CHECK(sm.clips.size() == 1 && std::fabs(sm.length() - 8.35f) < 0.01f && sm.clips[0].channels.size() == 120);
    for (float t : {0.0f, 4.0f}) {
        const auto nodes = sm.nodes(t, render::Mat4::identity());
        CHECK(nodes.size() == 20);
        std::vector<gt3r::scene::Mesh> posed;
        CHECK(mdls::pose(model.meshes, table, menu::palette(bones, nodes), posed) == vertices);
        float plo = 1e9f, phi = -1e9f;
        for (const gt3r::scene::Mesh& m : posed)
            for (const gt3r::scene::Vertex& v : m.vertices) plo = std::min(plo, v.pos[1]), phi = std::max(phi, v.pos[1]);
        const render::Vec3 hip = nodes.at("PELVIS").transform_point({0, 0, 0});
        std::printf("  t %.0f: pelvis %.2f %.2f %.2f, height %.3f..%.3f\n", double(t), double(hip.x), double(hip.y), double(hip.z), double(plo), double(phi));
        CHECK(plo > -0.05f && plo < 0.1f && phi > 1.6f && phi < 1.9f);
    }
}

} // namespace menu_draw_skin_test
