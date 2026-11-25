#include "gt4r/formats/course/runway.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::course {

namespace {

float rdf(ByteSpan d, size_t o) {
    const u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
Vec3 vec(ByteSpan d, size_t o) { return {rdf(d, o), rdf(d, o + 4), rdf(d, o + 8)}; }

} // namespace

Runway Runway::parse(ByteSpan d) {
    if (!is_runway(d)) throw FormatError("not an RNW4 road");
    Runway r;
    r.flags = rd32(d, 0x0C), r.version = rd32(d, 0x10);
    r.length = rdf(d, 0x14), r.start_v = rdf(d, 0x18), r.goal_v = rdf(d, 0x1C);
    r.min = vec(d, 0x20), r.max = vec(d, 0x2C);
    const u32 grids = rd16(d, 0x3A), checkpoints = rd16(d, 0x3C), lookups = rd16(d, 0x3E);
    const u32 vertices = rd16(d, 0x46), triangles = rd16(d, 0x48), clusters = rd16(d, 0x4A);
    r.tree_depth = rd16(d, 0x4C);
    const u32 wall_vertices = rd16(d, 0x4E), pit_stops = rd16(d, 0x52);
    const u32 grid_at = rd32(d, 0x5C), checkpoint_at = rd32(d, 0x60), lookup_at = rd32(d, 0x64);
    const u32 vertex_at = rd32(d, 0x74), triangle_at = rd32(d, 0x78), cluster_at = rd32(d, 0x7C), tree_at = rd32(d, 0x80);
    const u32 wall_at = rd32(d, 0x84), wall_indices = rd32(d, 0x88), wall_index_at = rd32(d, 0x8C), pit_at = rd32(d, 0x90);
    auto need = [&](u32 off, u64 bytes) {
        if (off > d.size() || d.size() - off < bytes) throw FormatError("RNW4: table out of range");
    };
    need(grid_at, u64(grids) * 0x10), need(checkpoint_at, u64(checkpoints) * 0x28), need(lookup_at, u64(lookups) * 2);
    need(vertex_at, u64(vertices) * 0x10), need(triangle_at, u64(triangles) * 0x0C), need(cluster_at, u64(clusters) * 0x10);
    need(wall_at, u64(wall_vertices) * 0x10), need(wall_index_at, u64(wall_indices) * 2), need(pit_at, u64(pit_stops) * 0x10);
    for (u32 i = 0; i < grids; ++i) r.grid.push_back({vec(d, grid_at + 0x10 * size_t(i)), rdf(d, grid_at + 0x10 * size_t(i) + 12)});
    for (u32 i = 0; i < pit_stops; ++i) r.pit_stops.push_back({vec(d, pit_at + 0x10 * size_t(i)), rdf(d, pit_at + 0x10 * size_t(i) + 12)});
    for (u32 i = 0; i < checkpoints; ++i) {
        const size_t o = checkpoint_at + 0x28 * size_t(i);
        r.checkpoints.push_back({vec(d, o), vec(d, o + 12), vec(d, o + 24), rdf(d, o + 36)});
    }
    for (u32 i = 0; i < lookups; ++i) r.checkpoint_lookup.push_back(u16(rd16(d, lookup_at + 2 * size_t(i))));
    for (u32 i = 0; i < vertices; ++i) {
        const size_t o = vertex_at + 0x10 * size_t(i);
        r.vertices.push_back({vec(d, o), s16(rd16(d, o + 12)), d[o + 14], d[o + 15]});
    }
    for (u32 i = 0; i < triangles; ++i) {
        const size_t o = triangle_at + 0x0C * size_t(i);
        Triangle t;
        for (size_t k = 0; k < 3; ++k) {
            t.v[k] = u16(rd16(d, o + 2 * k));
            if (t.v[k] >= vertices) throw FormatError("RNW4: triangle vertex out of range");
        }
        t.bits = d[o + 6], t.kind = d[o + 7], t.flags = rd32(d, o + 8);
        r.triangles.push_back(t);
    }
    for (u32 i = 0; i < clusters; ++i) {
        const size_t o = cluster_at + 0x10 * size_t(i);
        Cluster c;
        const u32 n = rd16(d, o), at = rd32(d, o + 8);
        c.checkpoint_count = u16(rd16(d, o + 2)), c.checkpoint_first = u16(rd16(d, o + 4));
        need(at, u64(n) * 2);
        for (u32 k = 0; k < n; ++k) {
            const u16 t = u16(rd16(d, at + 2 * size_t(k)));
            if (t >= triangles) throw FormatError("RNW4: cluster triangle out of range");
            c.triangles.push_back(t);
        }
        r.clusters.push_back(std::move(c));
    }
    if (r.tree_depth > 16) throw FormatError("RNW4: tree too deep");
    if (clusters && tree_at) {
        const size_t nodes = (size_t(1) << r.tree_depth) - 1;
        need(tree_at, nodes);
        r.tree.assign(d.begin() + tree_at, d.begin() + tree_at + ptrdiff_t(nodes));
    }
    for (u32 i = 0; i < wall_vertices; ++i) {
        const size_t o = wall_at + 0x10 * size_t(i);
        r.wall_vertices.push_back({vec(d, o), rd32(d, o + 12)});
    }
    for (u32 i = 0; i < wall_indices; ++i) r.wall_indices.push_back(s16(rd16(d, wall_index_at + 2 * size_t(i))));
    r.index_triangles();
    return r;
}

void Runway::index_triangles() {
    cells = {};
    if (triangles.empty() || triangles.size() > 0xFFFF) return;
    float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
    for (const Triangle& t : triangles)
        for (u16 v : t.v) {
            const Vec3& p = vertices[v].pos;
            lo[0] = std::min(lo[0], p.x), lo[1] = std::min(lo[1], p.z), hi[0] = std::max(hi[0], p.x), hi[1] = std::max(hi[1], p.z);
        }
    Cells c;
    c.x = lo[0] - 1, c.z = lo[1] - 1;
    c.size = std::max({8.0f, (hi[0] - lo[0] + 2) / 256, (hi[1] - lo[1] + 2) / 256});
    if (!std::isfinite(c.size)) return;
    c.nx = int((hi[0] - c.x + 1) / c.size) + 1, c.nz = int((hi[1] - c.z + 1) / c.size) + 1;
    // A triangle's cells: its box with room for height_at's tolerance (a point a little outside counts as on it).
    const auto each = [&](auto&& put) {
        for (size_t i = 0; i < triangles.size(); ++i) {
            const Vec3 &p = vertices[triangles[i].v[0]].pos, &q = vertices[triangles[i].v[1]].pos, &r = vertices[triangles[i].v[2]].pos;
            const float x0 = std::min({p.x, q.x, r.x}), x1 = std::max({p.x, q.x, r.x}), z0 = std::min({p.z, q.z, r.z}), z1 = std::max({p.z, q.z, r.z});
            const float extent = std::max(x1 - x0, z1 - z0), room = 0.01f + 1e-3f * extent;
            if (std::fabs((q.z - r.z) * (p.x - r.x) + (r.x - q.x) * (p.z - r.z)) < 1e-3f * std::max(extent, 1.0f)) {
                put(i, -1, -1, -1, -1);
                continue;
            }
            const auto at = [&](float v, float from, int n) { return std::clamp(int((v - from) / c.size), 0, n - 1); };
            put(i, at(x0 - room, c.x, c.nx), at(x1 + room, c.x, c.nx), at(z0 - room, c.z, c.nz), at(z1 + room, c.z, c.nz));
        }
    };
    c.start.assign(size_t(c.nx) * size_t(c.nz) + 1, 0);
    each([&](size_t i, int x0, int x1, int z0, int z1) {
        if (x0 < 0) return c.thin.push_back(u16(i));
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) ++c.start[size_t(z) * size_t(c.nx) + size_t(x) + 1];
    });
    for (size_t k = 1; k < c.start.size(); ++k) c.start[k] += c.start[k - 1];
    c.list.resize(c.start.back());
    std::vector<u32> fill(c.start.begin(), c.start.end() - 1);
    each([&](size_t i, int x0, int x1, int z0, int z1) {
        if (x0 < 0) return;
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) c.list[fill[size_t(z) * size_t(c.nx) + size_t(x)]++] = u16(i);
    });
    c.triangles = triangles.size();
    cells = std::move(c);
}

int Runway::cluster_at(float x, float z) const {
    if (tree.empty() || clusters.empty()) return -1;
    float lo[3] = {min.x, min.y, min.z}, hi[3] = {max.x, max.y, max.z};
    const float p[3] = {x, (min.y + max.y) * 0.5f, z};
    size_t node = 0, leaf = 0;
    // A subtree of depth d holds 2^d - 1 nodes: the left child is the next byte, the right one
    // comes after the left subtree.
    for (u32 depth = tree_depth; depth > 0; --depth) {
        const u8 b = tree[node];
        const u32 axis = b >> 6;
        const size_t below = (size_t(1) << (depth - 1)) - 1; // nodes of each child's subtree
        bool right = false;
        if (axis < 3) {
            const float split = lo[axis] + (hi[axis] - lo[axis]) * (float(b & 63) + 0.5f) / 64.0f;
            right = p[axis] >= split;
            (right ? lo[axis] : hi[axis]) = split;
        }
        leaf = leaf * 2 + (right ? 1 : 0);
        node += 1 + (right ? below : 0);
    }
    return leaf < clusters.size() ? int(leaf) : -1;
}

bool Runway::height_at(float x, float z, float y_max, float& y, int* triangle) const {
    bool found = false;
    auto test = [&](int ti) {
        const Triangle& t = triangles[size_t(ti)];
        const Vec3& a = vertices[t.v[0]].pos;
        const Vec3& b = vertices[t.v[1]].pos;
        const Vec3& c = vertices[t.v[2]].pos;
        const float det = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (std::fabs(det) < 1e-9f) return;
        const float u = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / det;
        const float v = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / det;
        const float w = 1 - u - v;
        const float e = -1e-4f;
        if (u < e || v < e || w < e) return;
        const float h = u * a.y + v * b.y + w * c.y;
        if (h > y_max || (found && h <= y)) return;
        found = true, y = h;
        if (triangle) *triangle = ti;
    };
    const int c = cluster_at(x, z);
    if (c >= 0)
        for (u16 t : clusters[size_t(c)].triangles) test(int(t));
    if (found) return true;
    // A point on a cell's edge or off the road, or a road without a tree.
    if (cells.triangles != triangles.size() || cells.start.empty()) {
        for (size_t t = 0; t < triangles.size(); ++t) test(int(t));
        return found;
    }
    const float cx = (x - cells.x) / cells.size, cz = (z - cells.z) / cells.size;
    const u16 *a = nullptr, *a_end = nullptr, *b = cells.thin.data(), *b_end = b + cells.thin.size();
    if (cx >= 0 && cz >= 0 && cx < float(cells.nx) && cz < float(cells.nz)) {
        const size_t k = size_t(cz) * size_t(cells.nx) + size_t(cx);
        a = cells.list.data() + cells.start[k], a_end = cells.list.data() + cells.start[k + 1];
    }
    while (a != a_end || b != b_end) test(int(b == b_end || (a != a_end && *a < *b) ? *a++ : *b++)); // (in their order)
    return found;
}

std::vector<std::pair<u32, u32>> Runway::wall_segments() const {
    std::vector<std::pair<u32, u32>> out;
    const u32 n = u32(wall_vertices.size());
    for (u32 i = 0; i < n; ++i) {
        const s32 link = s16(wall_vertices[i].tag & 0xFFFF);
        if (link < 0) { // the chain's last vertex: back to its first
            const s64 first = s64(i) + link + 1;
            if (first >= 0 && first < i) out.emplace_back(i, u32(first));
        } else if (i + 1 < n) {
            out.emplace_back(i, i + 1);
        }
    }
    return out;
}

gt3r::scene::Mesh make_road_mesh(const Runway& r) {
    gt3r::scene::Mesh mesh;
    mesh.name = "road";
    // A colour per kind of triangle, spread over the hues so neighbours differ.
    auto color = [](u32 kind) {
        static const u32 kPalette[] = {0xFF808080u, 0xFF40B040u, 0xFF30A0E0u, 0xFF4060C0u, 0xFFC0A040u, 0xFF3030C0u,
                                       0xFFC060C0u, 0xFF60C0C0u, 0xFF2080F0u, 0xFFA0A0A0u, 0xFF207020u, 0xFF8040A0u};
        return kPalette[kind % (sizeof kPalette / sizeof kPalette[0])];
    };
    for (const Runway::Triangle& t : r.triangles) {
        const u32 base = u32(mesh.vertices.size());
        for (u16 i : t.v) {
            gt3r::scene::Vertex v;
            const Vec3& p = r.vertices[i].pos;
            v.pos[0] = p.x, v.pos[1] = p.y + 0.05f, v.pos[2] = p.z;
            v.normal[1] = 1;
            v.color = color(t.kind);
            mesh.vertices.push_back(v);
        }
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
    }
    mesh.primitives.push_back({0, u32(mesh.indices.size()), -1, gt3r::scene::kUnlit | gt3r::scene::kDoubleSided});
    return mesh;
}

gt3r::scene::Mesh make_wall_mesh(const Runway& r, float height) {
    gt3r::scene::Mesh mesh;
    mesh.name = "walls";
    auto quad = [&](const Vec3& a, const Vec3& b, float h, u32 color) {
        const u32 base = u32(mesh.vertices.size());
        for (int k = 0; k < 4; ++k) {
            const Vec3& p = (k & 1) ? b : a;
            gt3r::scene::Vertex v;
            v.pos[0] = p.x, v.pos[1] = p.y + ((k & 2) ? h : 0.0f), v.pos[2] = p.z;
            v.normal[1] = 1;
            v.color = color;
            mesh.vertices.push_back(v);
        }
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
    };
    for (const auto& [from, to] : r.wall_segments())
        quad(r.wall_vertices[from].pos, r.wall_vertices[to].pos, height, 0xFF4040E0u);
    for (const Runway::Place& g : r.grid) { // a marker along the heading
        const Vec3 tip{g.pos.x + std::sin(g.heading) * 4.0f, g.pos.y, g.pos.z + std::cos(g.heading) * 4.0f};
        quad(g.pos, tip, 1.5f, 0xFF40E0E0u);
    }
    mesh.primitives.push_back({0, u32(mesh.indices.size()), -1, gt3r::scene::kUnlit | gt3r::scene::kDoubleSided});
    return mesh;
}

} // namespace gt4r::course
