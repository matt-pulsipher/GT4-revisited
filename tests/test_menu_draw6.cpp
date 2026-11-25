// The menus' drawing, round six: the skinned blend in the program's order (VU0 0x2E8).

#include "test_framework.h"

#include "gt4r/formats/model/mdls_skin.h"

#include <cmath>

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
