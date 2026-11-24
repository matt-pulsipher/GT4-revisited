#include "gt4r/formats/model/mdls_skin.h"

#include "gt4r/formats/model/mdls.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace gt4r::mdls {

namespace {

float f32_at(ByteSpan d, size_t o) {
    float f;
    std::memcpy(&f, d.data() + o, 4);
    return f;
}

std::string text_at(ByteSpan d, size_t o) {
    std::string s;
    while (o < d.size() && d[o]) s += char(d[o++]);
    return s;
}

void need(ByteSpan d, size_t at, size_t n) {
    if (at > d.size() || d.size() - at < n) throw FormatError("MDLS skin: data out of range");
}

} // namespace

std::vector<Bone> read_bones(ByteSpan d) {
    const Header h = read_header(d);
    std::vector<Bone> out;
    if (!h.bones) return out;
    const u32 table = rd32(d, 0x48);
    need(d, table, 0x50 * size_t(h.bones));
    for (u32 i = 0; i < h.bones; ++i) {
        const size_t o = table + 0x50 * size_t(i);
        Bone b;
        for (int k = 0; k < 16; ++k) b.inverse_bind.m[size_t(k)] = f32_at(d, o + 4 * size_t(k));
        b.index = rd32(d, o + 0x40);
        const u32 name = rd32(d, o + 0x44);
        if (name < d.size()) b.name = text_at(d, name);
        out.push_back(std::move(b));
    }
    return out;
}

std::vector<Symbol> read_symbols(ByteSpan d) {
    std::vector<Symbol> out;
    if (d.size() < 0x80) return out;
    const u32 count = rd16(d, 0x2A), table = rd32(d, 0x58);
    if (!count || !table) return out;
    need(d, table, 8 * size_t(count));
    for (u32 i = 0; i < count; ++i) {
        const u32 name = rd32(d, table + 8 * size_t(i));
        out.push_back({name < d.size() ? text_at(d, name) : std::string(), rd32(d, table + 8 * size_t(i) + 4)});
    }
    return out;
}

SkinBlock read_skin_block(ByteSpan d, size_t at, bool packed) {
    SkinBlock b;
    if (packed) {
        // f32 scale, u8 vertices, u8 the normals' place; groups: u8 count (| 0x80: another
        // follows), u8 4, u8 bone[4], then per vertex s16 position[3] x scale, (s8 normal[3] / 127
        // when they have a place), s8 weight[4] / 127.
        need(d, at, 6);
        const float scale = f32_at(d, at);
        const u32 n = d[at + 4];
        b.dest = d[at + 5];
        const size_t stride = b.dest ? 13 : 10;
        size_t p = at + 6;
        for (bool more = true; more && b.vertices.size() < n;) {
            need(d, p, 6);
            const u32 count = d[p] & 0x7F;
            more = (d[p] & 0x80) != 0;
            std::array<u8, 4> bones{d[p + 2], d[p + 3], d[p + 4], d[p + 5]};
            p += 6;
            if (count > n - b.vertices.size()) throw FormatError("MDLS skin: group past the vertex count");
            need(d, p, stride * count);
            for (u32 i = 0; i < count; ++i, p += stride) {
                SkinVertex v;
                v.bones = bones;
                for (int k = 0; k < 3; ++k) v.pos[k] = float(s16(rd16(d, p + 2 * size_t(k)))) * scale;
                size_t w = p + 6;
                if (b.dest) {
                    for (int k = 0; k < 3; ++k) v.normal[k] = float(std::int8_t(d[p + 6 + size_t(k)])) / 127.0f;
                    w = p + 9;
                }
                for (int k = 0; k < 4; ++k) v.weights[size_t(k)] = float(std::int8_t(d[w + size_t(k)])) / 127.0f;
                b.vertices.push_back(v);
            }
        }
        if (b.vertices.size() != n) throw FormatError("MDLS skin: fewer vertices than counted");
        b.end = p;
        return b;
    }
    // The float layout: u16 vertices, u16 the normals' place; groups: u16 count (| 0x8000:
    // another follows), u16 4, u8 bone[4], then per vertex f32 position[3], normal[3], weight[4].
    need(d, at, 4);
    const u32 n = rd16(d, at);
    b.dest = rd16(d, at + 2);
    size_t p = at + 4;
    for (bool more = true; more && b.vertices.size() < n;) {
        need(d, p, 8);
        const u32 count = rd16(d, p) & 0x7FFF;
        more = (rd16(d, p) & 0x8000) != 0;
        std::array<u8, 4> bones{d[p + 4], d[p + 5], d[p + 6], d[p + 7]};
        p += 8;
        if (count > n - b.vertices.size()) throw FormatError("MDLS skin: group past the vertex count");
        need(d, p, 40 * size_t(count));
        for (u32 i = 0; i < count; ++i, p += 40) {
            SkinVertex v;
            v.bones = bones;
            for (int k = 0; k < 3; ++k) v.pos[k] = f32_at(d, p + 4 * size_t(k)), v.normal[k] = f32_at(d, p + 12 + 4 * size_t(k));
            for (int k = 0; k < 4; ++k) v.weights[size_t(k)] = f32_at(d, p + 24 + 4 * size_t(k));
            b.vertices.push_back(v);
        }
    }
    if (b.vertices.size() != n) throw FormatError("MDLS skin: fewer vertices than counted");
    b.end = p;
    return b;
}

bool SkinTable::Key::operator==(const Key& o) const { return std::memcmp(w, o.w, sizeof w) == 0; }

size_t SkinTable::Hash::operator()(const Key& k) const {
    size_t h = 1469598103934665603ull;
    for (u32 x : k.w) h = (h ^ x) * 1099511628211ull;
    return h;
}

SkinTable SkinTable::of(ByteSpan d) {
    SkinTable t;
    const Header h = read_header(d);
    for (u32 s = 0; s < h.shapes; ++s) {
        const u32 so = rd32(d, h.shape_table + 4 * size_t(s));
        need(d, so, 0x10);
        const u32 flags = rd16(d, so + 8);
        if (!(flags & kShapeSkinned)) continue;
        const u32 count = rd16(d, so + 10);
        need(d, so + 0x10, 8 * size_t(count));
        for (u32 k = 0; k < count; ++k) {
            const size_t e = so + 0x10 + 8 * size_t(k);
            const size_t at = so + rd32(d, e) + size_t(rd16(d, e + 4)) * 16;
            for (const SkinVertex& v : read_skin_block(d, at, (flags & kShapeSkinPacked) != 0).vertices) {
                Key key{};
                std::memcpy(&key.w[0], v.pos, 12);
                std::memcpy(&key.w[3], v.normal, 12);
                t.map_.emplace(key, v);
            }
        }
    }
    return t;
}

const SkinVertex* SkinTable::find(const float pos[3], const float normal[3]) const {
    Key key{};
    std::memcpy(&key.w[0], pos, 12);
    std::memcpy(&key.w[3], normal, 12);
    const auto it = map_.find(key);
    return it == map_.end() ? nullptr : &it->second;
}

size_t pose(const std::vector<gt3r::scene::Mesh>& bind, const SkinTable& table, const Palette& palette, std::vector<gt3r::scene::Mesh>& out) {
    using gt3r::render::Mat4;
    out = bind;
    size_t moved = 0;
    for (gt3r::scene::Mesh& m : out) {
        const Mat4 own = Mat4::from(m.transform);
        for (gt3r::scene::Vertex& v : m.vertices) {
            const SkinVertex* s = table.find(v.pos, v.normal);
            float p[3] = {0, 0, 0}, n[3] = {0, 0, 0};
            if (s) {
                for (int k = 0; k < 4; ++k) {
                    const float w = s->weights[size_t(k)];
                    if (w == 0 || s->bones[size_t(k)] >= palette.size()) continue;
                    const Mat4& b = palette[s->bones[size_t(k)]];
                    for (int r = 0; r < 3; ++r) {
                        p[r] += w * (b.at(r, 0) * v.pos[0] + b.at(r, 1) * v.pos[1] + b.at(r, 2) * v.pos[2] + b.at(r, 3));
                        n[r] += w * (b.at(r, 0) * v.normal[0] + b.at(r, 1) * v.normal[1] + b.at(r, 2) * v.normal[2]);
                    }
                }
                ++moved;
            } else {
                for (int r = 0; r < 3; ++r) {
                    p[r] = own.at(r, 0) * v.pos[0] + own.at(r, 1) * v.pos[1] + own.at(r, 2) * v.pos[2] + own.at(r, 3);
                    n[r] = own.at(r, 0) * v.normal[0] + own.at(r, 1) * v.normal[1] + own.at(r, 2) * v.normal[2];
                }
            }
            const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            for (int r = 0; r < 3; ++r) v.pos[r] = p[r], v.normal[r] = len > 1e-6f ? n[r] / len : v.normal[r];
        }
        m.transform = Mat4::identity().m;
    }
    return moved;
}

} // namespace gt4r::mdls
