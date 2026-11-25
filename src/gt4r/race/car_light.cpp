#include "gt4r/race/car_light.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace gt4r::race {

using gt3r::render::Vec3;

CarLight car_light(const course::Runway& road, const gt3r::race::SunParams& env, const Vec3& at, float y_max) {
    CarLight out;
    if (!env.has_env || road.gtrw) return out;
    if (road.lamps.empty()) {
        float sun = 1, sky = 1;
        if (!road.light_at(at.x, at.z, y_max, sun, sky)) return out;
        const std::array<float, 3> s = env.light_scale(sun, sky);
        // The global ambient (Env0 +0xEF0) is not shaded: with one, the lights are made out here.
        const auto& g = env.global_ambient;
        if ((g[0] != 0 || g[1] != 0 || g[2] != 0) && s[0] < 1) {
            for (int i = 0; i < 3; ++i) out.lights.ambient[i] = g[size_t(i)];
            for (size_t n = 0; n < 4; ++n) {
                if (!((env.car_light_mask >> n) & 1)) continue;
                const gt3r::race::SunParams::LampType& type = env.lamps[n];
                for (int i = 0; i < 3; ++i) out.lights.ambient[i] += type.ambient[size_t(i)] * s[0];
                const Vec3 dir{env.car_light_dir[n][0], env.car_light_dir[n][1], env.car_light_dir[n][2]};
                if (gt3r::render::length(dir) < 1e-6f) continue;
                const Vec3 to = gt3r::render::normalize(dir);
                const int k = out.lights.count++;
                for (int i = 0; i < 3; ++i) {
                    out.lights.dir[k][i] = (&to.x)[i];
                    out.lights.diffuse[k][i] = type.diffuse[size_t(i)] * s[1];
                    out.lights.specular[k][i] = type.specular[size_t(i)] * s[2];
                }
            }
            out.own = out.lights.count > 0;
        }
        if (!out.own) std::copy(s.begin(), s.end(), out.scale);
        const float full = env.shadow_factor(1, 1);
        out.shadow = full > 0 ? std::clamp(env.shadow_factor(sun, sky) / full, 0.0f, 1.0f) : 1.0f;
        return out;
    }
    float y = 0;
    int t = -1;
    if (!road.height_at(at.x, at.z, y_max, y, &t) || t < 0 || size_t(t) >= road.triangles.size()) return out;
    const u16 corner = road.triangles[size_t(t)].v[0];
    const int first = corner < road.vertices.size() ? road.vertices[corner].a : -1;
    if (first < 0) return out;
    struct Pick {
        float w = 0;
        Vec3 dir;
        int type = 0;
    };
    std::vector<Pick> picks;
    for (size_t i = 0; i < road.lamps_per_list && size_t(first) + i < road.lamp_lists.size(); ++i) {
        const int k = road.lamp_lists[size_t(first) + i];
        if (k < 0 || size_t(k) >= road.lamps.size()) continue;
        const course::Runway::Lamp& l = road.lamps[size_t(k)];
        if (l.kind < 0 || l.kind > 3) continue;
        const Vec3 pos{l.pos.x, l.pos.y, l.pos.z}, axis{l.axis.x, l.axis.y, l.axis.z};
        const gt3r::race::SunParams::LampType& type = env.lamps[size_t(l.kind)];
        Pick p;
        p.type = l.kind;
        if (!l.point) p.dir = pos, p.w = 1;
        else {
            const Vec3 d = pos - at;
            const float dist = std::max(gt3r::render::length(d), 1e-4f), den = type.c0 + type.c1 * dist + type.c2 * dist * dist;
            p.dir = d * (1 / dist);
            p.w = den > 1e-6f ? 1 / den : 0.0f;
            if (type.cutoff != 180.0f) {
                const float along = std::max(gt3r::render::dot(p.dir, axis), 0.0f);
                p.w = std::cos(type.cutoff * 3.14159265f / 180.0f) <= along ? p.w * std::pow(along, type.spot_exp) : 0.0f;
            }
        }
        picks.push_back(p);
    }
    std::stable_sort(picks.begin(), picks.end(), [](const Pick& x, const Pick& y2) { return x.w > y2.w; });
    out.own = out.lamps = true;
    for (int i = 0; i < 3; ++i) out.lights.ambient[i] = env.global_ambient[size_t(i)];
    for (const Pick& p : picks) {
        if (out.lights.count >= 4) break;
        const gt3r::race::SunParams::LampType& type = env.lamps[size_t(p.type)];
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
