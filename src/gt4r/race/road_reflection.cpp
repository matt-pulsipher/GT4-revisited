#include "gt4r/race/road_reflection.h"

#include "base/log.h"
#include "render/gpu_context.h"
#include "render/gpu_model.h"
#include "scene/model.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::race {

namespace render = gt3r::render;
namespace scene = gt3r::scene;

namespace {

float rdf(ByteSpan d, size_t o) {
    u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

constexpr u32 kQuadFlags = scene::kUnlit | scene::kNoDepthWrite | scene::kDoubleSided;

std::unique_ptr<render::GpuModel> make_quad(render::GpuContext& ctx, const std::array<render::Vec3, 4>& pos,
                                            const std::array<std::array<float, 2>, 4>& uv, u32 color, u32 flags) {
    scene::Model m;
    scene::Mesh mesh;
    mesh.name = "quad";
    for (int i = 0; i < 4; ++i) {
        scene::Vertex v;
        v.pos[0] = pos[size_t(i)].x, v.pos[1] = pos[size_t(i)].y, v.pos[2] = pos[size_t(i)].z;
        v.normal[2] = 1;
        v.uv[0] = uv[size_t(i)][0], v.uv[1] = uv[size_t(i)][1];
        v.color = color;
        mesh.vertices.push_back(v);
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    scene::Primitive p;
    p.index_count = 6;
    p.texture = -1;
    p.flags = flags;
    mesh.primitives.push_back(p);
    m.meshes.push_back(std::move(mesh));
    m.compute_bounds();
    return std::make_unique<render::GpuModel>(ctx, m);
}

} // namespace

float road_reflection_extent(int size, int blur) {
    // 0x341600: the blur count as a float, less 0.5, taken from the picture's far edge.
    return float(size) - (float(blur) - 0.5f);
}

std::array<u8, 3> road_reflection_color(ByteSpan env0) {
    std::array<u8, 3> c{128, 128, 128};
    if (env0.size() < 0xC20) return c;
    const float gain = rdf(env0, 0xC1C);
    for (int i = 0; i < 3; ++i) {
        const float v = std::max(rdf(env0, 0xC10 + size_t(i) * 4) * gain, 0.0f); // 0x3415F0: each channel times the gain
        c[size_t(i)] = u8(std::min(v * 128.0f + 0.5f, 255.0f));                    // 0x4AA1D8: chopped to a byte
    }
    return c;
}

RoadReflection::RoadReflection() = default;
RoadReflection::~RoadReflection() = default;

bool RoadReflection::load(render::GpuContext& ctx, const gt3r::Vfs& vfs, ByteSpan course, ByteSpan env0) {
    ctx_ = &ctx;
    blur_quad_.reset();
    for (Composite& c : composites_) c = Composite{};
    if (env0.size() < 0xE80) return false;
    blur_ = int(std::clamp<u32>(rd32(env0, 0xC2C), 0, 16));
    color_ = road_reflection_color(env0);
    // The shared engine's pass draws the picture without blur passes: ours follow.
    Bytes env(env0.begin(), env0.end());
    std::memset(env.data() + 0xC2C, 0, 4);
    if (!wet_.load(ctx, vfs, course, env)) return false;
    width_ = wet_.params().width, height_ = wet_.params().height;
    // A blur pass: the whole other buffer through an NDC quad, texture coordinates (0.5, 0.5) .. (w + 0.5, h + 0.5) texels
    // (0x3414C8 .. 0x341534): each pixel the GS's filter of its own texel and the next ones right and down.
    const float du = 0.5f / float(width_), dv = 0.5f / float(height_);
    blur_quad_ = make_quad(ctx, {render::Vec3{-1, 1, 0}, {1, 1, 0}, {1, -1, 0}, {-1, -1, 0}},
                           {{{du, dv}, {1 + du, dv}, {1 + du, 1 + dv}, {du, 1 + dv}}}, 0xFFFFFFFFu, kQuadFlags);
    LOG_INFO("Road reflection: GT4's %d blur pass(es) and composite (0 .. %.1f x %.1f texels, color %d %d %d)", blur_,
             double(road_reflection_extent(width_, blur_)), double(road_reflection_extent(height_, blur_)), color_[0], color_[1], color_[2]);
    return true;
}

SDL_GPUTexture* RoadReflection::render(render::View3D& v, const render::Mat4& view, const render::Mat4& proj, const render::Vec3& eye,
                                       const std::vector<render::DrawItem>& items, const render::DrawOptions& main) {
    SDL_GPUTexture* t = wet_.render(v, view, proj, eye, items, main);
    if (!t || !blur_quad_) return t;
    render::DrawOptions bo;
    for (float& c : bo.clear) c = 0;
    bo.fog = false;
    bo.frustum_cull = false;
    bo.sort_blended = false;
    for (int k = 0; k < blur_; ++k) { // (the engine's picture is its slot 0; the passes take 1 and 2 by turns)
        render::DrawItem it{blur_quad_.get(), {}, 0};
        it.texture = t;
        it.gs_bilinear = true;
        it.wheel_steps = {float(width_), float(height_), 0, 0};
        t = v.render_offscreen(1 + (k & 1), width_, height_, {}, {}, {0, 0, 0}, {it}, bo);
        if (!t) return nullptr;
    }
    return t;
}

const render::GpuModel* RoadReflection::composite(const render::Frustum& fr) {
    auto same = [&](const render::Frustum& a) {
        return std::fabs(a.left - fr.left) < 1e-5f && std::fabs(a.right - fr.right) < 1e-5f && std::fabs(a.down - fr.down) < 1e-5f &&
               std::fabs(a.up - fr.up) < 1e-5f;
    };
    Composite* c = nullptr;
    for (Composite& k : composites_)
        if (k.quad && same(k.fr)) c = &k;
    if (!c) {
        c = composites_[0].used <= composites_[1].used ? &composites_[0] : &composites_[1];
        c->fr = fr;
        // The engine's quad: camera space just past the near plane, 1 % beyond the frustum on each side. Its texture
        // coordinates put (0, 0) and (w + 0.5 - n, h + 0.5 - n) texels at the frustum's edges.
        const float d = 0.105f * 1.01f;
        const float l = fr.left * d, r = fr.right * d, b = fr.down * d, t = fr.up * d;
        const std::array<render::Vec3, 4> pos = {render::Vec3{l, t, -0.105f}, {r, t, -0.105f}, {r, b, -0.105f}, {l, b, -0.105f}};
        const float us = road_reflection_extent(width_, blur_) / float(width_), vs = road_reflection_extent(height_, blur_) / float(height_);
        const float w = fr.right - fr.left, h = fr.up - fr.down;
        const float u0 = (fr.left * 1.01f - fr.left) / w * us, u1 = (fr.right * 1.01f - fr.left) / w * us;
        const float v0 = (fr.up - fr.up * 1.01f) / h * vs, v1 = (fr.up - fr.down * 1.01f) / h * vs;
        // (A GS byte g is g / 128; the quad's bytes are over 255, rounded up so that the shader's truncation of
        // texel x color gives the GS's (texel x g) >> 7.)
        auto byte = [](u8 g) { return u32(std::min(255, (int(g) * 255 + 127) / 128)); };
        const u32 color = byte(color_[0]) | byte(color_[1]) << 8 | byte(color_[2]) << 16 | 0xFF000000u;
        c->quad = make_quad(*ctx_, pos, {{{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}}}, color, scene::kAlphaBlend | kQuadFlags);
    }
    c->used = ++uses_;
    return c->quad.get();
}

void RoadReflection::add_frame_items(std::vector<render::DrawItem>& items, const render::Mat4& view, const render::Frustum& fr,
                                     SDL_GPUTexture* buffer) {
    const size_t first = items.size();
    wet_.add_frame_items(items, view, fr, buffer);
    if (items.size() == first || !ctx_) return;
    // The engine's last item is its composite: the same item through GT4's quad.
    render::DrawItem& comp = items.back();
    if (comp.texture != buffer) return;
    if (const render::GpuModel* q = composite(fr)) comp.model = q;
}

} // namespace gt4r::race
