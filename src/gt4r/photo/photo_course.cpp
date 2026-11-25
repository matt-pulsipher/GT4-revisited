#include "gt4r/photo/photo_course.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::photo {

namespace {

constexpr float kPi = 3.14159274f;

float f32_at(ByteSpan d, size_t o) {
    float v = 0;
    const u32 w = rd32(d, o);
    std::memcpy(&v, &w, 4);
    return v;
}

// The cosine of the corner b of two lines a-b, c-b (0x3C8F90).
float corner_cos(const Vec2& at, const Vec2& a, const Vec2& b) {
    const float ax = a.x - at.x, az = a.z - at.z, bx = b.x - at.x, bz = b.z - at.z;
    const float n = std::sqrt((ax * ax + az * az) * (bx * bx + bz * bz));
    return n > 0 ? (ax * bx + az * bz) / n : 0.0f;
}

// Which side of the edge a -> c the point b lies on (0x3C9010).
float side(const Vec2& a, const Vec2& b, const Vec2& c) { return (b.x - a.x) * (c.z - b.z) - (b.z - a.z) * (c.x - b.x); }

bool inside(const std::vector<Vec2>& outline, const Vec2& p) {
    bool in = false;
    const size_t n = outline.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const Vec2 *a = &outline[j], *b = &outline[i];
        if (a->z == b->z) continue;
        if (!(a->z < b->z)) std::swap(a, b);
        if (!(a->z <= p.z) || !(p.z < b->z)) continue;
        if ((p.x - a->x) * (b->z - a->z) - (p.z - a->z) * (b->x - a->x) > 0) in = !in;
    }
    return in;
}

// A move that met the edge a-b: `share` of it goes on along the edge, to the end that lies ahead (0x3C9050). Answers what is
// left over past that end.
float slide(BorderTrack& t, const Vec2& a, const Vec2& b, int a_index, int b_index, const Vec2& from, const Vec2& to, float share) {
    const Vec2 *near = &a, *far = &b;
    int far_index = b_index;
    if (corner_cos(from, to, b) < corner_cos(from, to, a)) near = &b, far = &a, far_index = a_index;
    const float mx = to.x - from.x, mz = to.z - from.z;
    const float length2 = share * share * (mx * mx + mz * mz);
    const float ex = far->x - near->x, ez = far->z - near->z;
    float dx = 0, dz = 0;
    if (std::fabs(ez) < std::fabs(ex)) {
        dx = std::sqrt(length2 / (ez / ex * (ez / ex) + 1));
        if (ex < 0) dx = -dx;
        dz = dx * ez / ex;
    } else if (ez != 0) {
        dz = std::sqrt(length2 / (ex / ez * (ex / ez) + 1));
        if (ez < 0) dz = -dz;
        dx = dz * ex / ez;
    }
    t.state = 1, t.at = {from.x + dx, from.z + dz}, t.corner = -1;
    const float went = dx * dx + dz * dz;
    const float reach = (far->x - from.x) * (far->x - from.x) + (far->z - from.z) * (far->z - from.z);
    if (!(reach <= went)) return 0;
    t.state = 2, t.at = *far, t.corner = far_index;
    return went > 0 ? (went - reach) / went : 0.0f;
}

// A free move against an outline's edges: the nearest edge it crosses (0x3C9298).
void meet(const std::vector<Vec2>& outline, int index, const Vec2& from, const Vec2& to, BorderTrack& t) {
    const size_t n = outline.size();
    const float x1 = std::max(from.x, to.x), x0 = std::min(from.x, to.x), z1 = std::max(from.z, to.z), z0 = std::min(from.z, to.z);
    const float mx = to.x - from.x, mz = to.z - from.z;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const Vec2 &a = outline[j], &b = outline[i];
        if ((x1 < a.x && x1 < b.x) || (a.x < x0 && b.x < x0) || (z1 < a.z && z1 < b.z) || (a.z < z0 && b.z < z0)) continue;
        const float ex = b.x - a.x, ez = b.z - a.z;
        const float s0 = (from.z - a.z) * ex + (a.x - from.x) * ez, s1 = (to.z - a.z) * ex + (a.x - to.x) * ez;
        if (s0 * s1 >= 0) continue;
        if (((a.z - from.z) * mx + (from.x - a.x) * mz) * ((b.z - from.z) * mx + (from.x - b.x) * mz) >= 0) continue;
        float x, z;
        if (ex == 0) {
            if (mx == 0) continue;
            x = a.x, z = x * (mz / mx) + from.z - from.x * (mz / mx);
        } else if (mx == 0) {
            x = from.x, z = x * (ez / ex) + a.z - a.x * (ez / ex);
        } else {
            const float ke = ez / ex, km = mz / mx;
            if (ke == km) continue;
            x = (from.z - a.z - from.x * km + a.x * ke) / (ke - km);
            z = x * ke + a.z - a.x * ke;
        }
        const float d = std::sqrt((x - from.x) * (x - from.x) + (z - from.z) * (z - from.z));
        if (t.met && t.distance < d) continue;
        const float e2 = ex * ex + ez * ez, m2 = mx * mx + mz * mz;
        if (e2 <= 0 || m2 <= 0) continue;
        const float along = std::fabs((ex * mx + ez * mz) / (std::sqrt(e2) * std::sqrt(m2)));
        slide(t, a, b, int(j), int(i), {x, z}, to, along);
        t.met = 1, t.distance = d, t.angle = (1.5707964f - std::asin(std::min(along, 1.0f))) * 180.0f / kPi;
        t.outline = index, t.edge = int(i);
    }
}

// A move that starts on an edge (0x3C96E0): off it when it leads inside, else along it (not at all below 0.3 of it).
float along_edge(const std::vector<Vec2>& outline, const Vec2& from, const Vec2& to, BorderTrack& t, bool to_inside) {
    const int n = int(outline.size());
    const int i = t.edge > 0 ? t.edge : 0, j = t.edge > 0 ? t.edge - 1 : n - 1;
    const Vec2 &a = outline[size_t(j)], &b = outline[size_t(i)];
    if (0 < side(a, to, b)) {
        if (to_inside) t.reset(to);
        return 0;
    }
    const float ex = b.x - a.x, ez = b.z - a.z, mx = to.x - from.x, mz = to.z - from.z;
    const float e2 = ex * ex + ez * ez, m2 = mx * mx + mz * mz;
    if (e2 <= 0 || m2 <= 0) return 0;
    const float share = std::fabs((ex * mx + ez * mz) / (std::sqrt(e2) * std::sqrt(m2)));
    if (share < 0.3f) return 0;
    const float left = slide(t, a, b, j, i, from, to, share);
    t.met = 1, t.distance = 0, t.angle = (1.5707964f - std::asin(std::min(share, 1.0f))) * 180.0f / kPi;
    return left;
}

} // namespace

float angle_deg(float dx, float dz) {
    if (dx == 0 && dz == 0) return 0;
    const float a = std::atan2(dz, dx) * 180.0f / kPi;
    return a < 0 ? a + 360.0f : a;
}

float wrap360(float deg) {
    if (deg >= 360) deg -= float(int(deg / 360.0f)) * 360.0f;
    if (deg < 0) deg = 360.0f - (-deg - float(int(-deg / 360.0f)) * 360.0f);
    return deg;
}

float turn_deg(float to, float from) {
    float d = to - from;
    if (d > 180) d -= 360;
    if (d < -180) d += 360;
    return d;
}

bool Shape::contains(const Vec2& p) const {
    bool in = false;
    for (const std::vector<Vec2>& o : outlines) in ^= inside(o, p);
    return in;
}

Vec2 Shape::middle(size_t outline) const {
    Vec2 m;
    if (outline >= outlines.size() || outlines[outline].empty()) return m;
    for (const Vec2& p : outlines[outline]) m.x += p.x, m.z += p.z;
    m.x /= float(outlines[outline].size()), m.z /= float(outlines[outline].size());
    return m;
}

void BorderTrack::reset(const Vec2& p) {
    state = 0, at = p, met = 0, outline = edge = corner = -1, distance = -1, angle = 0;
}

PhotoCourse PhotoCourse::parse(ByteSpan d) {
    if (d.size() < 0x60 || std::memcmp(d.data(), "CRPM", 4) != 0) throw FormatError("photo location: no CRPM block");
    const size_t size = d.size();
    auto need = [size](size_t at, size_t bytes) {
        if (at > size || bytes > size - at) throw FormatError("photo location: bad offset");
    };
    PhotoCourse c;
    c.map_offset = rd32(d, 0x14);
    const u32 cells = rd32(d, 0x18), cell_at = rd32(d, 0x1C), shapes = rd32(d, 0x20), shape_at = rd32(d, 0x24);
    for (int i = 0; i < 4; ++i) c.drag[size_t(i)] = f32_at(d, 0x28 + size_t(i) * 4), c.view[size_t(i)] = f32_at(d, 0x4C + size_t(i) * 4);
    c.camera_start = {f32_at(d, 0x38), f32_at(d, 0x3C)};
    c.car_start = {f32_at(d, 0x40), f32_at(d, 0x44)};
    c.car_heading = f32_at(d, 0x48);
    if (cells > 4096 || shapes > 4096) throw FormatError("photo location: bad counts");
    need(cell_at, cells * 0x40), need(shape_at, shapes * 0x10);
    if (c.map_offset) need(c.map_offset, 4);
    auto points = [&](size_t at, u32 n) {
        need(at, size_t(n) * 8);
        std::vector<Vec2> out;
        for (u32 i = 0; i < n; ++i) out.push_back({f32_at(d, at + i * 8), f32_at(d, at + i * 8 + 4)});
        return out;
    };
    for (u32 i = 0; i < shapes; ++i) {
        const size_t at = shape_at + i * 0x10;
        const u16 outlines = rd16(d, at + 2);
        const u32 list = rd32(d, at + 4);
        Shape s;
        need(list, size_t(outlines) * 4);
        for (u16 k = 0; k < outlines; ++k) {
            const u32 o = rd32(d, list + size_t(k) * 4);
            need(o, 4);
            s.outlines.push_back(points(o + 4, rd32(d, o)));
        }
        c.shapes.push_back(std::move(s));
    }
    for (u32 i = 0; i < cells; ++i) {
        const size_t at = cell_at + i * 0x40;
        Cell e;
        e.spot = rd32(d, at) & 1;
        e.shape = rd16(d, at + 4);
        if (size_t(e.shape) >= c.shapes.size()) throw FormatError("photo location: bad shape");
        e.targets = points(rd32(d, at + 0x10), rd16(d, at + 6));
        const u16 n = rd16(d, at + 8);
        const u32 list = rd32(d, at + 0x14);
        need(list, size_t(n) * 2);
        for (u16 k = 0; k < n; ++k) e.car_shapes.push_back(rd16(d, list + size_t(k) * 2));
        e.look = {f32_at(d, at + 0x18), f32_at(d, at + 0x1C)};
        e.angle = f32_at(d, at + 0x20), e.eye_low = f32_at(d, at + 0x24), e.eye_high = f32_at(d, at + 0x28);
        c.cells.push_back(std::move(e));
    }
    return c;
}

int PhotoCourse::cell_at(Vec2& p, float radius) const {
    for (size_t i = 0; i < cells.size(); ++i) {
        const Shape& s = shapes[size_t(cells[i].shape)];
        if (cells[i].spot) {
            const Vec2 m = s.middle();
            const float dx = m.x - p.x, dz = m.z - p.z;
            if (dx * dx + dz * dz < radius * radius) return p = m, int(i);
        } else if (s.contains(p)) return int(i);
    }
    return -1;
}

int PhotoCourse::car_shape_at(const Vec2& p, int cell) const {
    if (cell < 0 || size_t(cell) >= cells.size()) return -1;
    for (u16 s : cells[size_t(cell)].car_shapes) {
        if (s >= shapes.size()) return -1;
        if (shapes[s].contains(p)) return s;
    }
    return -1;
}

Vec2 PhotoCourse::move(int cell, const Vec2& from, const Vec2& to, BorderTrack& t) const {
    if (cell < 0 || size_t(cell) >= cells.size()) return t.at;
    const Shape& shape = shapes[size_t(cells[size_t(cell)].shape)];
    Vec2 a = from, b = to;
    for (int pass = 0; pass < 4; ++pass) { // (0x3C9A38, again with what is left of the move)
        const bool in = shape.contains(b);
        float left = 0;
        const bool known = t.outline >= 0 && size_t(t.outline) < shape.outlines.size();
        if (t.state == 0) {
            t.reset(a);
            t.at = b;
            for (size_t i = 0; i < shape.outlines.size(); ++i)
                if (!shape.outlines[i].empty()) meet(shape.outlines[i], int(i), a, b, t);
            if (!t.met && !in) t.reset(a); // ❓ a start outside every outline: stays
        } else if (!known) {
            if (in) t.reset(b);
        } else if (t.state == 1) {
            left = along_edge(shape.outlines[size_t(t.outline)], a, b, t, in);
        } else {
            // At a corner: along the edge that leads more the move's way (0x3C9908).
            const std::vector<Vec2>& o = shape.outlines[size_t(t.outline)];
            const int n = int(o.size()), at = t.corner;
            const int before = at - 1 < 0 ? n - 1 : at - 1, after = at + 1 < n ? at + 1 : 0;
            t.edge = corner_cos(o[size_t(at)], b, o[size_t(before)]) < corner_cos(o[size_t(at)], b, o[size_t(after)]) ? after : at;
            left = along_edge(o, a, b, t, in);
        }
        if (left <= 0) break;
        const float dx = b.x - a.x, dz = b.z - a.z;
        a = t.at;
        b = {a.x + dx * left, a.z + dz * left};
    }
    return t.at;
}

std::array<float, 3> PhotoCourse::limits(int cell, const Vec2& p) const {
    const Cell& c = cells[size_t(cell)];
    const float to = wrap360(angle_deg(c.look.x - p.x, c.look.z - p.z) + 90.0f);
    std::array<float, 3> r{to, -360.0f, 720.0f};
    for (const Vec2& t : c.targets) {
        const float a = wrap360(angle_deg(t.x - p.x, t.z - p.z) + 90.0f);
        const float below = to < a ? a - 360.0f : a, above = a < to ? a + 360.0f : a;
        r[1] = std::max(r[1], below), r[2] = std::min(r[2], above);
    }
    return r;
}

float PhotoCourse::spot_heading(int cell) const {
    const Vec2 m = shapes[size_t(cells[size_t(cell)].shape)].middle();
    return limits(cell, m)[0];
}

} // namespace gt4r::photo
