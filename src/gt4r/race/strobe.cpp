#include "gt4r/race/strobe.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace gt4r::race {

namespace {

float f32_at(ByteSpan d, size_t o) {
    float f;
    std::memcpy(&f, d.data() + o, 4);
    return f;
}

struct List {
    u32 at = 0, count = 0;
};
List list_at(ByteSpan d, size_t o) { return {rd32(d, o), rd32(d, o + 4)}; }
void need(ByteSpan d, u64 at, u64 size) {
    if (at + size > d.size()) throw FormatError("STRB: a table is outside the file");
}
List table(ByteSpan d, size_t o, u32 stride, u32 most = 1 << 16) {
    const List l = list_at(d, o);
    if (l.count > most) throw FormatError("STRB: a table is too long");
    need(d, l.at, u64(l.count) * stride);
    return l;
}
std::string chars(ByteSpan d, List l) {
    need(d, l.at, l.count);
    std::string s(reinterpret_cast<const char*>(d.data() + l.at), l.count);
    while (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

using Point = StrobeMovie::Point;

float cross(const Point& o, const Point& a, const Point& b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

// A simple polygon's triangles by cutting ears.
void triangulate(std::vector<Point> p, std::vector<Point>& out) {
    // (A closing point that repeats the first is dropped.)
    if (p.size() > 1 && std::fabs(p.front().x - p.back().x) < 1e-4f && std::fabs(p.front().y - p.back().y) < 1e-4f) p.pop_back();
    if (p.size() < 3) return;
    float area = 0;
    for (size_t i = 0; i < p.size(); ++i) area += p[i].x * p[(i + 1) % p.size()].y - p[(i + 1) % p.size()].x * p[i].y;
    if (area < 0) std::reverse(p.begin(), p.end());
    std::vector<size_t> idx(p.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    size_t guard = 0;
    while (idx.size() > 3 && guard++ < 10000) {
        bool cut = false;
        for (size_t i = 0; i < idx.size(); ++i) {
            const Point &a = p[idx[(i + idx.size() - 1) % idx.size()]], &b = p[idx[i]], &c = p[idx[(i + 1) % idx.size()]];
            if (cross(a, b, c) <= 1e-6f) continue; // reflex or flat
            bool inside = false;
            for (size_t k : idx) {
                const Point& q = p[k];
                if (&q == &a || &q == &b || &q == &c) continue;
                if (cross(a, b, q) >= 0 && cross(b, c, q) >= 0 && cross(c, a, q) >= 0) {
                    inside = true;
                    break;
                }
            }
            if (inside) continue;
            out.push_back(a), out.push_back(b), out.push_back(c);
            idx.erase(idx.begin() + ptrdiff_t(i));
            cut = true;
            break;
        }
        if (!cut) {
            // Not simple: a fan from the first point.
            for (size_t i = 1; i + 1 < idx.size(); ++i) out.push_back(p[idx[0]]), out.push_back(p[idx[i]]), out.push_back(p[idx[i + 1]]);
            return;
        }
    }
    if (idx.size() == 3) out.push_back(p[idx[0]]), out.push_back(p[idx[1]]), out.push_back(p[idx[2]]);
}

} // namespace

const StrobeMovie::Key& StrobeMovie::Track::key_at(int frame) const {
    static const Key none;
    if (keys.empty()) return none;
    const size_t k = size_t(std::max(frame - first, 0));
    return keys[std::min(k, keys.size() - 1)];
}

int StrobeMovie::Sprite::label(const std::string& name) const {
    const std::string want = lower(name);
    for (const auto& [frame, l] : labels)
        if (lower(l) == want) return frame;
    return -1;
}

ByteSpan StrobeMovie::texture_set(size_t i) const {
    if (i >= textures.size()) return {};
    const Texture& t = textures[i];
    if (u64(t.offset) + t.size > data.size()) return {};
    return ByteSpan(data).subspan(t.offset, t.size);
}

StrobeMovie StrobeMovie::parse(ByteSpan d) {
    if (!is_movie(d)) throw FormatError("not an STRB movie");
    StrobeMovie m;
    m.data.assign(d.begin(), d.end());
    m.width = f32_at(d, 0x10), m.height = f32_at(d, 0x14), m.fps = f32_at(d, 0x1C);
    if (!(m.fps > 0 && m.fps < 1000)) m.fps = 30;
    // Sprites, their tracks and keys.
    const List sprites = table(d, 0x30, 0x24, 4096);
    for (u32 s = 0; s < sprites.count; ++s) {
        const size_t o = sprites.at + 0x24 * size_t(s);
        Sprite sp;
        sp.frames = std::max<int>(1, int(rd32(d, o + 0x20)));
        const List tracks = table(d, o, 0x28), actions = table(d, o + 8, 4), labels = table(d, o + 0x10, 0xC);
        for (u32 i = 0; i < actions.count; ++i) {
            const u32 v = rd32(d, actions.at + 4 * i);
            sp.frame_actions.push_back({int(v & 0xFFFF), int(v >> 16)});
        }
        for (u32 i = 0; i < labels.count; ++i) {
            const size_t q = labels.at + 0xC * size_t(i);
            sp.labels.push_back({int(rd32(d, q)), chars(d, {rd32(d, q + 4), rd32(d, q + 8)})});
        }
        for (u32 t = 0; t < tracks.count; ++t) {
            const size_t q = tracks.at + 0x28 * size_t(t);
            Track tr;
            tr.kind = rd16(d, q), tr.first = s16(rd16(d, q + 2)), tr.end = s16(rd16(d, q + 4)), tr.index = rd16(d, q + 6);
            tr.depth = rd32(d, q + 8) & 0xFFFF, tr.clip = rd16(d, q + 10);
            const List clip = table(d, q + 0x10, 8), keys = table(d, q + 0x18, 0x24);
            for (u32 i = 0; i < clip.count; ++i) tr.clip_actions.push_back({rd32(d, clip.at + 8 * i), int(s16(rd16(d, clip.at + 8 * i + 4)))});
            for (u32 i = 0; i < keys.count; ++i) {
                const size_t k = keys.at + 0x24 * size_t(i);
                Key key;
                key.rotation = f32_at(d, k), key.skew = f32_at(d, k + 4), key.sx = f32_at(d, k + 8), key.sy = f32_at(d, k + 12);
                key.x = f32_at(d, k + 16), key.y = f32_at(d, k + 20), key.mul = rd32(d, k + 24), key.add = rd32(d, k + 28);
                tr.keys.push_back(key);
            }
            tr.name = chars(d, list_at(d, q + 0x20));
            sp.tracks.push_back(std::move(tr));
        }
        std::stable_sort(sp.tracks.begin(), sp.tracks.end(), [](const Track& a, const Track& b) { return a.depth < b.depth; });
        m.sprites.push_back(std::move(sp));
    }
    if (m.sprites.empty()) throw FormatError("STRB: no timeline");
    // Fills, gradients, matrices.
    const List fills = table(d, 0x68, 8);
    for (u32 i = 0; i < fills.count; ++i) m.fills.push_back({rd16(d, fills.at + 8 * i), rd16(d, fills.at + 8 * i + 2), rd32(d, fills.at + 8 * i + 4)});
    const List grads = table(d, 0x78, 0xC);
    for (u32 i = 0; i < grads.count; ++i) {
        Gradient g;
        const List stops = table(d, grads.at + 0xC * i, 8, 64);
        for (u32 k = 0; k < stops.count; ++k) g.stops.push_back({f32_at(d, stops.at + 8 * k), rd32(d, stops.at + 8 * k + 4)});
        m.gradients.push_back(std::move(g));
    }
    const List mats = table(d, 0x98, 0x40);
    for (u32 i = 0; i < mats.count; ++i) {
        std::array<float, 16> a{};
        for (size_t k = 0; k < 16; ++k) a[k] = f32_at(d, mats.at + 0x40 * i + 4 * k);
        m.matrices.push_back(a);
    }
    // Shapes: each group's paths filled.
    const List shapes = table(d, 0x38, 0x18);
    for (u32 i = 0; i < shapes.count; ++i) {
        const size_t o = shapes.at + 0x18 * size_t(i);
        Shape sh;
        sh.x0 = f32_at(d, o), sh.y0 = f32_at(d, o + 4), sh.x1 = f32_at(d, o + 8), sh.y1 = f32_at(d, o + 12);
        const List parts = table(d, o + 0x10, 8);
        for (u32 p = 0; p < parts.count; ++p) {
            const List groups = table(d, parts.at + 8 * p, 0xC);
            for (u32 g = 0; g < groups.count; ++g) {
                const size_t q = groups.at + 0xC * size_t(g);
                Shape::Group gr;
                gr.fill = rd16(d, q), gr.flags = rd16(d, q + 2);
                const List paths = table(d, q + 4, 0x14);
                for (u32 k = 0; k < paths.count; ++k) {
                    const size_t r = paths.at + 0x14 * size_t(k);
                    std::vector<Point> poly{{f32_at(d, r + 4), f32_at(d, r + 8)}};
                    const List edges = table(d, r + 0xC, 0x14);
                    for (u32 e = 0; e < edges.count; ++e) {
                        const size_t x = edges.at + 0x14 * size_t(e);
                        const Point c{f32_at(d, x + 4), f32_at(d, x + 8)}, a{f32_at(d, x + 12), f32_at(d, x + 16)};
                        const Point s = poly.back();
                        // A quadratic edge in four straight pieces (a straight one has its control on it).
                        const bool straight = std::fabs((s.x + a.x) * 0.5f - c.x) < 0.01f && std::fabs((s.y + a.y) * 0.5f - c.y) < 0.01f;
                        if (!straight)
                            for (int j = 1; j < 4; ++j) {
                                const float t = float(j) / 4, u = 1 - t;
                                poly.push_back({u * u * s.x + 2 * u * t * c.x + t * t * a.x, u * u * s.y + 2 * u * t * c.y + t * t * a.y});
                            }
                        poly.push_back(a);
                    }
                    triangulate(std::move(poly), gr.triangles);
                }
                sh.groups.push_back(std::move(gr));
            }
        }
        m.shapes.push_back(std::move(sh));
    }
    // Fonts, text fields, buttons, actions, textures.
    const List fonts = table(d, 0x50, 0x18);
    for (u32 i = 0; i < fonts.count; ++i) m.fonts.push_back(chars(d, list_at(d, fonts.at + 0x18 * i + 8)));
    const List texts = table(d, 0x60, 0x34);
    for (u32 i = 0; i < texts.count; ++i) {
        const size_t o = texts.at + 0x34 * size_t(i);
        TextField t;
        t.x0 = f32_at(d, o), t.y0 = f32_at(d, o + 4), t.x1 = f32_at(d, o + 8), t.y1 = f32_at(d, o + 12);
        t.color = rd32(d, o + 0x10);
        t.font = s16(rd16(d, o + 0x14));
        t.height = float(rd16(d, o + 0x16)) / 20.0f;
        t.align = d[o + 0x1B];
        t.text = chars(d, list_at(d, o + 0x2C));
        m.texts.push_back(std::move(t));
    }
    const List buttons = table(d, 0xA0, 0x14);
    for (u32 i = 0; i < buttons.count; ++i) {
        const size_t o = buttons.at + 0x14 * size_t(i);
        Button b;
        for (size_t k = 0; k < 4; ++k) b.states[k] = rd16(d, o + 2 * k);
        const List conds = table(d, o + 0xC, 4);
        for (u32 k = 0; k < conds.count; ++k) b.conditions.push_back({rd16(d, conds.at + 4 * k), int(rd16(d, conds.at + 4 * k + 2))});
        m.buttons.push_back(std::move(b));
    }
    const List actions = table(d, 0xA8, 8);
    for (u32 i = 0; i < actions.count; ++i) {
        const u32 at = rd32(d, actions.at + 8 * i), n = rd32(d, actions.at + 8 * i + 4);
        need(d, at, n);
        m.actions.emplace_back(d.begin() + at, d.begin() + at + n);
    }
    const List tex = table(d, 0xB0, 0x10);
    for (u32 i = 0; i < tex.count; ++i) {
        const size_t o = tex.at + 0x10 * size_t(i);
        m.textures.push_back({rd32(d, o), rd32(d, o + 4), rd16(d, o + 8), rd16(d, o + 10)});
    }
    const List ext = table(d, 0xB8, 0x18);
    for (u32 i = 0; i < ext.count; ++i) {
        const size_t o = ext.at + 0x18 * size_t(i);
        External e{f32_at(d, o), f32_at(d, o + 4), f32_at(d, o + 8), f32_at(d, o + 12), chars(d, {rd32(d, o + 16), rd32(d, o + 20)})};
        m.externals.push_back(std::move(e));
    }
    return m;
}

} // namespace gt4r::race
