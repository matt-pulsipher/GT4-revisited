#include "gt4r/sim/ground.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gt4r::sim {

int surface_index(u8 material, u8 flags) {
    if (!(flags & 2)) return material;
    switch (material) {
    case 0:
    case 7: return 20;
    case 1:
    case 10:
    case 11:
    case 12: return 21;
    case 9: return 22;
    default: return material;
    }
}

RoadGround::RoadGround(const course::Runway& road) : road_(&road) {
    // The checkpoints rise along the list from the one with the least distance (the list starts mid-lap).
    for (size_t i = 1; i < road.checkpoints.size(); ++i)
        if (road.checkpoints[i].v < road.checkpoints[first_].v) first_ = i;
    // The walls' segments by cell.
    const auto segs = road.wall_segments();
    walls_.reserve(segs.size());
    for (const auto& [a, b] : segs) walls_.push_back({road.wall_vertices[a].pos, road.wall_vertices[b].pos});
    nx_ = std::max(1, int((road.max.x - road.min.x) / cell_) + 1);
    nz_ = std::max(1, int((road.max.z - road.min.z) / cell_) + 1);
    if (u64(nx_) * u64(nz_) > 4'000'000) nx_ = nz_ = 1, cell_ = 1e9f;
    wall_cells_.assign(size_t(nx_) * size_t(nz_), {});
    auto cell = [&](float x, float z, int& cx, int& cz) {
        cx = std::clamp(int((x - road.min.x) / cell_), 0, nx_ - 1);
        cz = std::clamp(int((z - road.min.z) / cell_), 0, nz_ - 1);
    };
    for (u32 i = 0; i < walls_.size(); ++i) {
        int x0, z0, x1, z1;
        cell(std::min(walls_[i].a.x, walls_[i].b.x), std::min(walls_[i].a.z, walls_[i].b.z), x0, z0);
        cell(std::max(walls_[i].a.x, walls_[i].b.x), std::max(walls_[i].a.z, walls_[i].b.z), x1, z1);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) wall_cells_[size_t(z) * size_t(nx_) + size_t(x)].push_back(i);
    }
}

// One cluster's triangles against the line down from `from` (0x3E5FD8): a triangle counts when the
// line passes inside it or on its edge, from its upper side; the first met from above wins, an equal
// one later in the list does not replace it.
void RoadGround::test_cluster(size_t leaf, const Vec3& from, float y_low, GroundHit& best, float& best_t) const {
    const course::Runway& r = *road_;
    if (leaf >= r.clusters.size()) return;
    for (u16 ti : r.clusters[leaf].triangles) {
        if (ti >= r.triangles.size()) continue;
        const course::Runway::Triangle& t = r.triangles[ti];
        const Vec3 &a = r.vertices[t.v[0]].pos, &b = r.vertices[t.v[1]].pos, &c = r.vertices[t.v[2]].pos;
        // The normal (b - a) x (c - a); only triangles facing up are road.
        const float e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z, e2x = c.x - a.x, e2y = c.y - a.y, e2z = c.z - a.z;
        const float nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
        if (!(ny > 0)) continue;
        // The point's weights of b and c in the triangle seen from above (ny is twice its area there).
        const float px = from.x - a.x, pz = from.z - a.z;
        const float wb = (pz * e2x - px * e2z) / ny, wc = (px * e1z - pz * e1x) / ny;
        if (wb < 0 || wc < 0 || wb + wc > 1) continue;
        const float y = a.y + wb * e1y + wc * e2y;
        if (y > from.y || y < y_low) continue;
        const float tt = from.y - y; // how far down
        if (!(tt < best_t)) continue;
        best_t = tt;
        best.triangle = int(ti), best.cluster = int(leaf);
        best.material = u8(t.bits & 0x1F), best.flags = t.kind;
        const float inv = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
        best.normal = {nx * inv, ny * inv, nz * inv};
        auto within = [](float v, float p, float q, float s) { return std::clamp(v, std::min({p, q, s}), std::max({p, q, s})); };
        best.point = {within(from.x, a.x, b.x, c.x), within(y, a.y, b.y, c.y), within(from.z, a.z, b.z, c.z)};
        best.plane_d = -(best.normal.x * best.point.x + best.normal.y * best.point.y + best.normal.z * best.point.z);
        const course::Runway::Vertex &va = r.vertices[t.v[0]], &vb = r.vertices[t.v[1]], &vc = r.vertices[t.v[2]];
        auto mix = [&](float fa, float fb, float fc) { return std::clamp(fa + (fb - fa) * wb + (fc - fa) * wc, 0.0f, 1.0f); };
        best.attr_a = mix(float(va.b) / 255.0f, float(vb.b) / 255.0f, float(vc.b) / 255.0f);
        best.attr_b = mix(float(va.c & 0x7F) / 127.0f, float(vb.c & 0x7F) / 127.0f, float(vc.c & 0x7F) / 127.0f);
    }
}

// The lookup tree for a vertical line (0x3E5BC8): a node that splits x or z leads to one side, one
// that splits the height to both.
void RoadGround::walk(size_t node, size_t leaf, u32 depth, float lo[3], float hi[3], const Vec3& from, float y_low, GroundHit& best, float& best_t) const {
    const course::Runway& r = *road_;
    if (depth == 0) {
        test_cluster(leaf, from, y_low, best, best_t);
        return;
    }
    if (node >= r.tree.size()) return;
    const u8 b = r.tree[node];
    const u32 axis = b >> 6;
    const size_t below = (size_t(1) << (depth - 1)) - 1;
    if (axis >= 3) {
        walk(node + 1, leaf * 2, depth - 1, lo, hi, from, y_low, best, best_t);
        return;
    }
    const float split = lo[axis] + (hi[axis] - lo[axis]) * (float(b & 63) + 0.5f) / 64.0f;
    const float p[3] = {from.x, from.y, from.z};
    const bool left = axis == 1 ? y_low < split : p[axis] < split;
    const bool right = axis == 1 ? from.y >= split : p[axis] >= split;
    if (right) { // the upper side first: the first triangle met from above wins
        const float keep = lo[axis];
        lo[axis] = split;
        walk(node + 1 + below, leaf * 2 + 1, depth - 1, lo, hi, from, y_low, best, best_t);
        lo[axis] = keep;
    }
    if (left) {
        const float keep = hi[axis];
        hi[axis] = split;
        walk(node + 1, leaf * 2, depth - 1, lo, hi, from, y_low, best, best_t);
        hi[axis] = keep;
    }
}

bool RoadGround::cast_down(const Vec3& from_in, GroundHit& out) const {
    const course::Runway& r = *road_;
    if (from_in.y < r.min.y) return false;
    Vec3 from = from_in;
    if (r.max.y < from.y) from.y = r.max.y;
    GroundHit best;
    float best_t = std::numeric_limits<float>::max();
    if (!r.tree.empty() && !r.clusters.empty()) {
        float lo[3] = {r.min.x, r.min.y, r.min.z}, hi[3] = {r.max.x, r.max.y, r.max.z};
        walk(0, 0, r.tree_depth, lo, hi, from, r.min.y, best, best_t);
    } else {
        for (size_t c = 0; c < r.clusters.size(); ++c) test_cluster(c, from, r.min.y, best, best_t);
    }
    if (best.triangle < 0) return false;
    out = best;
    return true;
}

bool RoadGround::distance_along(const Vec3& p, float& v, int* hint) const {
    const course::Runway& r = *road_;
    const int n = int(r.checkpoints.size());
    if (n < 2) return false;
    // How far p is past checkpoint i's line, along the course.
    auto past = [&](int i, float& side) {
        const course::Runway::Checkpoint& c = r.checkpoints[size_t(i)];
        const course::Runway::Checkpoint& next = r.checkpoints[size_t((i + 1) % n)];
        float lx = c.right.x - c.left.x, lz = c.right.z - c.left.z;
        const float len = std::sqrt(lx * lx + lz * lz);
        if (len < 1e-6f) return 0.0f;
        lx /= len, lz /= len;
        float fx = -lz, fz = lx; // across the line; turned to point at the next checkpoint
        if (fx * (next.middle.x - c.middle.x) + fz * (next.middle.z - c.middle.z) < 0) fx = -fx, fz = -fz;
        side = (p.x - c.middle.x) * lx + (p.z - c.middle.z) * lz;
        return (p.x - c.middle.x) * fx + (p.z - c.middle.z) * fz;
    };
    float best = std::numeric_limits<float>::max();
    bool found = false;
    auto try_pair = [&](int i) {
        i = ((i % n) + n) % n;
        const int j = (i + 1) % n;
        float side_i = 0, side_j = 0;
        const float a = past(i, side_i), b = past(j, side_j);
        if (a < 0 || b >= 0) return;
        const course::Runway::Checkpoint &c = r.checkpoints[size_t(i)], &d = r.checkpoints[size_t(j)];
        const float t = a / (a - b);
        // The nearest pair: by the distance from the middle line, a level apart counting ten times.
        const float off = std::fabs(side_i + (side_j - side_i) * t); // (the checkpoints' heights are not the road's)
        if (off >= best) return;
        float vj = d.v;
        if (vj < c.v) vj += r.length; // the pair across the lap's end
        best = off, found = true;
        v = c.v + (vj - c.v) * t;
        if (hint) *hint = i;
    };
    if (hint && *hint >= 0) {
        for (int k = -3; k <= 6; ++k) try_pair(*hint + k);
        if (found && best < 60.0f) return true;
    }
    for (int i = 0; i < n; ++i) try_pair(i);
    return found;
}

void RoadGround::middle_at(float v, Vec3& point, Vec3& direction) const {
    const course::Runway& r = *road_;
    const int n = int(r.checkpoints.size());
    point = {}, direction = {0, 0, 1};
    if (n < 2) return;
    const float first = r.checkpoints[first_].v;
    if (r.length > 0) {
        v = std::fmod(v - first, r.length);
        if (v < 0) v += r.length;
        v += first;
    }
    // The last checkpoint at or before v, in the order of rising distance.
    auto at = [&](int k) -> const course::Runway::Checkpoint& { return r.checkpoints[(first_ + size_t(k)) % size_t(n)]; };
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (at(mid).v <= v) lo = mid;
        else hi = mid - 1;
    }
    const course::Runway::Checkpoint &c = at(lo), &d = at(lo + 1);
    float span = d.v - c.v;
    if (span <= 0) span += r.length;
    const float t = span > 1e-6f ? std::clamp((v - c.v) / span, 0.0f, 1.0f) : 0.0f;
    point = {c.middle.x + (d.middle.x - c.middle.x) * t, c.middle.y + (d.middle.y - c.middle.y) * t, c.middle.z + (d.middle.z - c.middle.z) * t};
    direction = {d.middle.x - c.middle.x, d.middle.y - c.middle.y, d.middle.z - c.middle.z};
    const float len = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (len > 1e-6f) direction = {direction.x / len, direction.y / len, direction.z / len};
    else direction = {0, 0, 1};
}

bool RoadGround::nearest_wall(const Vec3& p, float reach, WallHit& out) const {
    const course::Runway& r = *road_;
    if (walls_.empty()) return false;
    const int x0 = std::clamp(int((p.x - reach - r.min.x) / cell_), 0, nx_ - 1), x1 = std::clamp(int((p.x + reach - r.min.x) / cell_), 0, nx_ - 1);
    const int z0 = std::clamp(int((p.z - reach - r.min.z) / cell_), 0, nz_ - 1), z1 = std::clamp(int((p.z + reach - r.min.z) / cell_), 0, nz_ - 1);
    float best = reach;
    bool found = false;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x)
            for (u32 i : wall_cells_[size_t(z) * size_t(nx_) + size_t(x)]) {
                const WallSeg& w = walls_[i];
                const float dx = w.b.x - w.a.x, dz = w.b.z - w.a.z;
                const float len2 = dx * dx + dz * dz;
                const float t = len2 > 1e-9f ? std::clamp(((p.x - w.a.x) * dx + (p.z - w.a.z) * dz) / len2, 0.0f, 1.0f) : 0.0f;
                const float qx = w.a.x + dx * t, qz = w.a.z + dz * t, qy = w.a.y + (w.b.y - w.a.y) * t;
                if (std::fabs(qy - p.y) > 6.0f) continue; // a wall of another level
                const float ex = p.x - qx, ez = p.z - qz;
                const float d = std::sqrt(ex * ex + ez * ez);
                if (d >= best) continue;
                best = d, found = true;
                out.point = {qx, qy, qz}, out.distance = d;
                out.normal = d > 1e-6f ? Vec3{ex / d, 0, ez / d} : Vec3{-dz, 0, dx};
            }
    return found;
}

} // namespace gt4r::sim
