#include "gt4r/menu/stage_env.h"

#include <algorithm>
#include <cmath>

namespace gt4r::menu {

namespace {

using V = std::array<float, 3>;
V sub(const V& a, const V& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V cross(const V& a, const V& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
float dot(const V& a, const V& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// A texture's colour at (u, v), bilinear, kept at its edges; 0..255 with the alpha.
std::array<float, 4> texel(const gt3r::scene::Texture& t, float u, float v) {
    const float x = u * float(t.width) - 0.5f, y = v * float(t.height) - 0.5f;
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = x - float(x0), fy = y - float(y0);
    std::array<float, 4> out{};
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            const u32 c = t.rgba[size_t(std::clamp(y0 + j, 0, int(t.height) - 1)) * t.width + size_t(std::clamp(x0 + i, 0, int(t.width) - 1))];
            const float w = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
            for (int k = 0; k < 4; ++k) out[size_t(k)] += w * float((c >> (8 * k)) & 0xFF);
        }
    return out;
}

} // namespace

std::array<float, 3> sphere_map_direction(float s, float t, const std::array<std::array<float, 3>, 3>& rows, bool& inside) {
    const float x = 2 * s - 1, y = 1 - 2 * t, rr = x * x + y * y;
    inside = rr <= 0.81f;
    // The kernel's inverse: the normal of a unit sphere at (x, y), the view reflected in it.
    const float nz = std::sqrt(std::max(1 - rr, 0.0f));
    const V r{2 * nz * x, 2 * nz * y, 2 * nz * nz - 1};
    constexpr float c = 0.9659258f, sn = 0.2588190f; // 15 degrees
    const V p{r[0], c * r[1] + sn * r[2], c * r[2] - sn * r[1]};
    V d{};
    for (size_t k = 0; k < 3; ++k) d[k] = rows[0][k] * p[0] + rows[1][k] * p[1] + rows[2][k] * p[2];
    return d;
}

std::vector<std::array<float, 3>> sphere_map(const gt3r::scene::Model& model, const V& eye, const std::array<V, 3>& rows, int size) {
    struct Tri {
        V a, e1, e2;
        const gt3r::scene::Vertex* v[3];
        const gt3r::scene::Texture* texture;
        u32 flags;
    };
    std::vector<Tri> tris;
    for (const gt3r::scene::Mesh& mesh : model.meshes)
        for (const gt3r::scene::Primitive& p : mesh.primitives)
            for (u32 i = p.first_index; i + 2 < p.first_index + p.index_count; i += 3) {
                Tri t;
                V at[3];
                for (u32 k = 0; k < 3; ++k) {
                    t.v[k] = &mesh.vertices[mesh.indices[i + k]];
                    at[k] = {t.v[k]->pos[0], t.v[k]->pos[1], t.v[k]->pos[2]};
                }
                t.a = sub(at[0], eye), t.e1 = sub(at[1], at[0]), t.e2 = sub(at[2], at[0]);
                t.texture = p.texture >= 0 && size_t(p.texture) < model.textures.size() ? &model.textures[size_t(p.texture)] : nullptr;
                if (t.texture && (!t.texture->width || !t.texture->height)) t.texture = nullptr;
                t.flags = p.flags;
                tris.push_back(t);
            }
    std::vector<std::array<float, 3>> out(size_t(size) * size_t(size), V{0, 0, 0});
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            bool inside = false;
            const V d = sphere_map_direction((float(x) + 0.5f) / float(size), (float(y) + 0.5f) / float(size), rows, inside);
            if (!inside) continue;
            V& c = out[size_t(y) * size_t(size) + size_t(x)];
            float depth = 200; // (the faces' far plane)
            // In the order drawn, as the GS does: nearer or as near passes, blended over what stands.
            for (const Tri& t : tris) {
                const V q = cross(d, t.e2);
                const float det = dot(t.e1, q);
                if (std::fabs(det) < 1e-12f) continue;
                const V from{-t.a[0], -t.a[1], -t.a[2]};
                const float u = dot(from, q) / det;
                if (u < 0 || u > 1) continue;
                const V r = cross(from, t.e1);
                const float v = dot(d, r) / det;
                if (v < 0 || u + v > 1) continue;
                const float far = dot(t.e2, r) / det;
                if (far < 0.005f || far > depth) continue;
                const float w0 = 1 - u - v;
                std::array<float, 4> col{};
                for (int k = 0; k < 4; ++k)
                    col[size_t(k)] = w0 * float((t.v[0]->color >> (8 * k)) & 0xFF) + u * float((t.v[1]->color >> (8 * k)) & 0xFF) + v * float((t.v[2]->color >> (8 * k)) & 0xFF);
                if (t.texture) {
                    const std::array<float, 4> tx = texel(*t.texture, w0 * t.v[0]->uv[0] + u * t.v[1]->uv[0] + v * t.v[2]->uv[0],
                                                          w0 * t.v[0]->uv[1] + u * t.v[1]->uv[1] + v * t.v[2]->uv[1]);
                    for (size_t k = 0; k < 3; ++k) col[k] = col[k] * tx[k] / 128; // (the vertex colour 128 is one)
                    col[3] = col[3] * tx[3] / 255;
                }
                const bool add = t.flags & gt3r::scene::kAdditive;
                const float a = add || (t.flags & gt3r::scene::kAlphaBlend) ? std::clamp(col[3] / 255, 0.0f, 1.0f) : 1.0f;
                for (size_t k = 0; k < 3; ++k) c[k] = std::clamp(add ? c[k] + col[k] * a : c[k] + (std::min(col[k], 255.0f) - c[k]) * a, 0.0f, 255.0f);
                if (!(t.flags & gt3r::scene::kNoDepthWrite)) depth = far;
            }
        }
    return out;
}

} // namespace gt4r::menu
