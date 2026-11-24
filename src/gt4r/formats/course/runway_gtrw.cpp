#include "gt4r/formats/course/runway_gtrw.h"

#include <memory>

#include "formats/course/road.h"

#include <algorithm>
#include <cmath>

namespace gt4r::course {

namespace {

struct Box {
    float lo[3], hi[3];
};

// The tree over the triangles: x and z split by turns at the place nearest the middle of the
// triangles' centres, a leaf listing every triangle whose box meets its cell (in x and z).
struct TreeBuilder {
    const Runway& r;
    std::vector<Box> boxes; // per triangle
    std::vector<float> centre[3];
    Bytes nodes;
    std::vector<Runway::Cluster> leaves;

    void build(u32 depth, Box cell, std::vector<u16>&& tris) {
        if (depth == 0) {
            Runway::Cluster c;
            c.triangles = std::move(tris);
            leaves.push_back(std::move(c));
            return;
        }
        const float ex = cell.hi[0] - cell.lo[0], ez = cell.hi[2] - cell.lo[2];
        const int axis = ex >= ez ? 0 : 2;
        const float lo = cell.lo[axis], span = cell.hi[axis] - cell.lo[axis];
        // The middle of the triangles' centres, rounded to the 64 places a node can name.
        std::vector<float> c;
        c.reserve(tris.size());
        for (u16 t : tris) c.push_back(centre[axis][t]);
        float mid = lo + span * 0.5f;
        if (!c.empty()) {
            std::nth_element(c.begin(), c.begin() + ptrdiff_t(c.size() / 2), c.end());
            mid = c[c.size() / 2];
        }
        int place = span > 0 ? int(std::lround((mid - lo) / span * 64.0f - 0.5f)) : 31;
        place = std::clamp(place, 0, 63);
        nodes.push_back(u8((axis == 0 ? 0 : 2) << 6 | place));
        const float split = lo + span * (float(place) + 0.5f) / 64.0f;
        std::vector<u16> left, right;
        for (u16 t : tris) {
            if (boxes[t].lo[axis] < split) left.push_back(t);
            if (boxes[t].hi[axis] >= split) right.push_back(t);
        }
        tris.clear();
        tris.shrink_to_fit();
        Box a = cell, b = cell;
        a.hi[axis] = split, b.lo[axis] = split;
        build(depth - 1, a, std::move(left));  // the left subtree first, as stored
        build(depth - 1, b, std::move(right));
    }
};

} // namespace

Runway runway_from_gtrw(ByteSpan section) {
    if (!is_gtrw(section)) throw FormatError("not a GTRW road");
    auto shared = std::make_shared<const gt3r::course::Road>(gt3r::course::Road::parse(section));
    const gt3r::course::Road& road = *shared;
    Runway r;
    r.gtrw = shared;
    r.version = 9;
    r.flags = road.header_flag;
    r.length = road.length;
    r.goal_v = road.start_distance;
    // The segments' vertices and triangles, one list.
    for (const gt3r::course::RoadSegment& s : road.segments) {
        const size_t base = r.vertices.size();
        if (base + s.vertices.size() > 0x10000) throw FormatError("GTRW: more vertices than a Runway holds");
        for (size_t i = 0; i < s.vertices.size(); ++i) {
            Runway::Vertex v;
            v.pos = {s.vertices[i].x, s.vertices[i].y, s.vertices[i].z};
            const u32 attrs = i < s.vertex_attrs.size() ? s.vertex_attrs[i] : 0xFFFFFFFFu;
            const u32 a = attrs & 0xFFFF, b = attrs >> 16;
            v.a = s16(u16(a));
            v.b = u8((a * 255 + 32767) / 65535);
            v.c = u8((b * 127 + 32767) / 65535);
            r.vertices.push_back(v);
        }
        for (const gt3r::course::RoadTriangle& t : s.triangles) {
            if (t.a >= s.vertices.size() || t.b >= s.vertices.size() || t.c >= s.vertices.size()) throw FormatError("GTRW: triangle vertex out of range");
            Runway::Triangle o;
            o.v = {u16(base + t.a), u16(base + t.b), u16(base + t.c)};
            o.bits = u8(t.material & 0x1F);
            o.kind = u8(t.flags);
            o.flags = t.surface;
            r.triangles.push_back(o);
        }
    }
    if (r.triangles.size() > 0x10000) throw FormatError("GTRW: more triangles than a Runway's clusters name");
    // The bounds.
    if (!r.vertices.empty()) {
        r.min = r.max = r.vertices[0].pos;
        for (const Runway::Vertex& v : r.vertices) {
            r.min.x = std::min(r.min.x, v.pos.x), r.min.y = std::min(r.min.y, v.pos.y), r.min.z = std::min(r.min.z, v.pos.z);
            r.max.x = std::max(r.max.x, v.pos.x), r.max.y = std::max(r.max.y, v.pos.y), r.max.z = std::max(r.max.z, v.pos.z);
        }
    }
    // The lookup tree: about 32 triangles a leaf.
    if (!r.triangles.empty()) {
        TreeBuilder tb{r, {}, {}, {}, {}};
        tb.boxes.resize(r.triangles.size());
        for (auto& c : tb.centre) c.resize(r.triangles.size());
        std::vector<u16> all(r.triangles.size());
        for (size_t i = 0; i < r.triangles.size(); ++i) {
            const Runway::Triangle& t = r.triangles[i];
            Box b{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
            for (u16 k : t.v) {
                const float p[3] = {r.vertices[k].pos.x, r.vertices[k].pos.y, r.vertices[k].pos.z};
                for (int a = 0; a < 3; ++a) b.lo[a] = std::min(b.lo[a], p[a]), b.hi[a] = std::max(b.hi[a], p[a]);
            }
            tb.boxes[i] = b;
            for (int a = 0; a < 3; ++a) tb.centre[a][i] = (b.lo[a] + b.hi[a]) * 0.5f;
            all[i] = u16(i);
        }
        u32 depth = 1;
        while (depth < 14 && (size_t(32) << depth) < r.triangles.size()) ++depth;
        r.tree_depth = depth;
        tb.build(depth, Box{{r.min.x, r.min.y, r.min.z}, {r.max.x, r.max.y, r.max.z}}, std::move(all));
        r.tree = std::move(tb.nodes);
        r.clusters = std::move(tb.leaves);
    }
    // The checkpoints: (x0, z0) to (x1, z1) across the course, the direction against the travel.
    auto height = [&](float x, float z) {
        float y = 0;
        return r.height_at(x, z, 1e30f, y) ? y : 0.0f;
    };
    for (const gt3r::course::Checkpoint& c : road.checkpoints) {
        Vec3 p0{c.x0, 0, c.z0}, p1{c.x1, 0, c.z1};
        // Right less left is (dir_z, -dir_x) when the travel is -dir.
        if ((p1.x - p0.x) * c.dir_z - (p1.z - p0.z) * c.dir_x < 0) std::swap(p0, p1);
        Runway::Checkpoint k;
        k.left = p0, k.right = p1;
        k.middle = {(p0.x + p1.x) * 0.5f, 0, (p0.z + p1.z) * 0.5f};
        k.middle.y = height(k.middle.x, k.middle.z);
        k.left.y = k.right.y = k.middle.y;
        k.v = c.distance;
        r.checkpoints.push_back(k);
    }
    // The start grid: Gran Turismo 3's yaw faces (-sin(yaw + pi), -cos(yaw + pi)) = (sin yaw, cos yaw).
    for (const gt3r::course::GridSlot& g : road.grid) r.grid.push_back({{g.pos.x, g.pos.y, g.pos.z}, g.yaw});
    for (const gt3r::course::WallPoint& w : road.wall_points) r.wall_vertices.push_back({{w.pos.x, w.pos.y, w.pos.z}, w.tag});
    return r;
}

std::optional<Runway> course_road(const CourseFile& file, ByteSpan data) {
    const ByteSpan rnw = file.section(data, kSlotRunway);
    if (Runway::is_runway(rnw)) return Runway::parse(rnw);
    const ByteSpan gtrw = file.section(data, kSlotRoadGtrw);
    if (is_gtrw(gtrw)) return runway_from_gtrw(gtrw);
    return std::nullopt;
}

} // namespace gt4r::course
