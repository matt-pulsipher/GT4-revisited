#include "gt4r/race/car_light.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::race {

namespace {

using gt3r::render::Vec3;

float rdf(ByteSpan d, size_t o) {
    const u32 v = gt3r::rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

} // namespace

void CarLights::load(ByteSpan d) {
    lamps_.clear(), near_.clear();
    per_place_ = 0;
    if (!course::Runway::is_runway(d)) return;
    const size_t n = gt3r::rd16(d, 0x40), at = gt3r::rd32(d, 0x68), list = gt3r::rd32(d, 0x6C);
    if (!n || at + n * 0x20 > d.size()) return;
    for (size_t i = 0; i < n; ++i) {
        const size_t o = at + i * 0x20;
        Lamp l;
        l.type = gt3r::rd32(d, o) & 3;
        l.axis = {rdf(d, o + 4), rdf(d, o + 8), rdf(d, o + 12)};
        l.pos = {rdf(d, o + 16), rdf(d, o + 20), rdf(d, o + 24)};
        l.kind = rdf(d, o + 28);
        lamps_.push_back(l);
    }
    per_place_ = std::min<size_t>(gt3r::rd16(d, 0x50), 9);
    // (The list has no length of its own: it ends where the next block starts.)
    size_t end = d.size();
    for (size_t o = 0x58; o < 0xA0; o += 4) {
        const size_t p = gt3r::rd32(d, o);
        if (o != 0x88 && p > list && p < end) end = p;
    }
    for (size_t o = list; list && o + 2 <= end; o += 2) near_.push_back(gt3r::rd16(d, o));
}

CarLights::Light CarLights::at(const course::Runway& road, const gt3r::race::SunParams& env, const Vec3& at, float y_max) const {
    Light out;
    float y = 0;
    int t = -1;
    if (!env.has_env || road.gtrw || !road.height_at(at.x, at.z, y_max, y, &t) || t < 0 || size_t(t) >= road.triangles.size()) return out;
    const course::Runway::Triangle& tri = road.triangles[size_t(t)];
    for (u16 v : tri.v)
        if (v >= road.vertices.size()) return out;
    const course::Runway::Vertex &a = road.vertices[tri.v[0]], &b = road.vertices[tri.v[1]], &c = road.vertices[tri.v[2]];
    if (lamps_.empty()) {
        const float det = (b.pos.z - c.pos.z) * (a.pos.x - c.pos.x) + (c.pos.x - b.pos.x) * (a.pos.z - c.pos.z);
        if (std::fabs(det) < 1e-12f) return out;
        const float k0 = ((b.pos.z - c.pos.z) * (at.x - c.pos.x) + (c.pos.x - b.pos.x) * (at.z - c.pos.z)) / det;
        const float k1 = ((c.pos.z - a.pos.z) * (at.x - c.pos.x) + (a.pos.x - c.pos.x) * (at.z - c.pos.z)) / det, k2 = 1 - k0 - k1;
        const float sun = std::clamp((k0 * float(a.b) + k1 * float(b.b) + k2 * float(c.b)) / 255.0f, 0.0f, 1.0f);
        const float sky = std::clamp((k0 * float(a.c & 0x7F) + k1 * float(b.c & 0x7F) + k2 * float(c.c & 0x7F)) / 127.0f, 0.0f, 1.0f);
        const std::array<float, 3> s = env.light_scale(sun, sky);
        std::copy(s.begin(), s.end(), out.scale);
        const float full = env.shadow_factor(1, 1);
        out.shadow = full > 0 ? std::clamp(env.shadow_factor(sun, sky) / full, 0.0f, 1.0f) : 1.0f;
        return out;
    }
    if (a.a < 0) return out;
    struct Pick {
        float w = 0;
        Vec3 dir;
        u32 type = 0;
    };
    std::vector<Pick> picks;
    for (size_t i = 0; i < per_place_ && size_t(a.a) + i < near_.size(); ++i) {
        const u16 k = near_[size_t(a.a) + i];
        if (k >= lamps_.size()) continue;
        const Lamp& l = lamps_[k];
        const gt3r::race::SunParams::LampType& type = env.lamps[l.type];
        Pick p;
        p.type = l.type;
        if (l.kind == 0) p.dir = l.pos, p.w = 1;
        else {
            const Vec3 d = l.pos - at;
            const float dist = std::max(gt3r::render::length(d), 1e-4f), den = type.c0 + type.c1 * dist + type.c2 * dist * dist;
            p.dir = d * (1 / dist);
            p.w = den > 1e-6f ? 1 / den : 0.0f;
            if (type.cutoff != 180.0f) {
                const float along = std::max(gt3r::render::dot(p.dir, l.axis), 0.0f);
                p.w = std::cos(type.cutoff * 3.14159265f / 180.0f) <= along ? p.w * std::pow(along, type.spot_exp) : 0.0f;
            }
        }
        picks.push_back(p);
    }
    std::stable_sort(picks.begin(), picks.end(), [](const Pick& x, const Pick& y2) { return x.w > y2.w; });
    out.own = true;
    for (int i = 0; i < 3; ++i) out.lights.ambient[i] = env.global_ambient[size_t(i)];
    for (const Pick& p : picks) {
        if (out.lights.count >= 4) break;
        const gt3r::race::SunParams::LampType& type = env.lamps[p.type];
        const int k = out.lights.count++;
        for (int i = 0; i < 3; ++i) {
            out.lights.ambient[i] += type.ambient[size_t(i)] * p.w;
            out.lights.dir[k][i] = (&p.dir.x)[i];
            out.lights.diffuse[k][i] = type.diffuse[size_t(i)] * p.w;
            out.lights.specular[k][i] = type.specular[size_t(i)] * p.w;
        }
        out.casts.push_back({Vec3{-p.dir.x, 0, -p.dir.z} * env.shadow_len, type.shadow_alpha * p.w});
    }
    return out;
}

} // namespace gt4r::race
