// The menus' drawing, round six: the skinned blend in the program's order (VU0 0x2E8).

#include "test_framework.h"

#include "gt4r/formats/model/mdls.h"
#include "gt4r/formats/model/mdls_skin.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/equipment.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/showroom.h"

#include <cmath>
#include <cstdio>
#include <memory>

using namespace gt4r;
namespace render = gt3r::render;

namespace menu_draw6_test {

mdls::SkinVertex packed_vertex() {
    mdls::SkinVertex v;
    v.packed = true;
    v.scale = 1.0e-4f;
    v.ipos[0] = 1000, v.ipos[1] = -2000, v.ipos[2] = 3000;
    v.inormal[0] = 0, v.inormal[1] = 127, v.inormal[2] = 0;
    v.iweight[0] = 100, v.iweight[1] = 27;
    v.bones = {0, 1, 0, 0};
    for (int k = 0; k < 3; ++k) v.pos[k] = float(v.ipos[k]) * v.scale, v.normal[k] = float(v.inormal[k]) / 127.0f;
    return v;
}

} // namespace menu_draw6_test

using namespace menu_draw6_test;

// Identity bones: the stored position times the weights' float sum (int x 1/127, never renormalized), the normal unit.
TEST(menu_skin_blend_identity) {
    const mdls::SkinVertex v = packed_vertex();
    const mdls::Palette pal(2, render::Mat4::identity());
    float p[3], n[3];
    mdls::blend(v, pal, p, n);
    CHECK(std::fabs(p[0] - 0.1f) < 1e-5f && std::fabs(p[1] + 0.2f) < 1e-5f && std::fabs(p[2] - 0.3f) < 1e-5f);
    CHECK(std::fabs(n[1] - 1.0f) < 1e-6f && n[0] == 0.0f && n[2] == 0.0f);
}

// Two bones: one moved 1 m up, weighted 27/127: the vertex moves 27/127 m; the normal unchanged.
TEST(menu_skin_blend_two_bones) {
    const mdls::SkinVertex v = packed_vertex();
    mdls::Palette pal(2, render::Mat4::identity());
    pal[1] = render::translate({0, 1, 0});
    float p[3], n[3];
    mdls::blend(v, pal, p, n);
    CHECK(std::fabs(p[1] - (-0.2f + 27.0f / 127.0f)) < 1e-5f);
    CHECK(std::fabs(p[0] - 0.1f) < 1e-5f && std::fabs(n[1] - 1.0f) < 1e-6f);
    // A bone past the palette draws with the identity.
    mdls::SkinVertex far = v;
    far.bones = {0, 9, 0, 0};
    mdls::blend(far, pal, p, n);
    CHECK(std::fabs(p[1] + 0.2f) < 1e-5f);
}

// How far a car's wheels hang on the oil change's jack: each axle's remaining travel from where the car sits
// (0x364B30), a few centimetres; the same for the same car again.
TEST(disc_menu_wheel_droop) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d.get();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = sim::ai::open_specdb(*disc);
    const sim::Program program = sim::Program::load(*disc);
    const sim::Showroom sr(sdb, program);
    for (const char* label : {"panda_super_ie_90", "ka_01", "skyline_gtr_r34_vspec2_nur_02"}) {
        sim::Equipment eq;
        if (!sim::stock_equipment(sdb, label, eq)) continue;
        const auto d = sr.droop(eq);
        CHECK(d.has_value());
        if (!d) continue;
        std::printf("  %s: front %.3f rear %.3f m\n", label, double((*d)[0].host()), double((*d)[1].host()));
        CHECK((*d)[0].host() > 0 && (*d)[0].host() < 0.2f && (*d)[1].host() > 0 && (*d)[1].host() < 0.2f);
    }
}
