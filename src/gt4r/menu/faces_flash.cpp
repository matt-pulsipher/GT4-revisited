#include "gt4r/menu/faces_flash.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::menu {

namespace {

float f32_at(ByteSpan d, size_t o) {
    float f;
    std::memcpy(&f, d.data() + o, 4);
    return f;
}
void need(ByteSpan d, u64 at, u64 n) {
    if (at + n > d.size()) throw FormatError("STRB: a table is outside the file");
}

} // namespace

FlashMovie FlashMovie::parse(ByteSpan d) {
    if (!is_movie(d)) throw FormatError("not a STRB movie");
    FlashMovie m;
    m.width = f32_at(d, 0x10), m.height = f32_at(d, 0x14), m.rate = f32_at(d, 0x1C);
    m.frames = s16(rd16(d, 0x20));
    // The pictures (0xB0: 0x10 each).
    const u32 img = rd32(d, 0xB0), images = rd32(d, 0xB4);
    need(d, img, u64(images) * 0x10);
    for (u32 i = 0; i < images; ++i) m.images.push_back({rd32(d, img + 0x10 * i), rd32(d, img + 0x10 * i + 4), rd16(d, img + 0x10 * i + 8), rd16(d, img + 0x10 * i + 0xA)});
    // The shapes (0x38: bounds, groups of items), each item's fill style (0x68: kind 3 / 4 a
    // picture, its texture matrix 0x98).
    const u32 sh = rd32(d, 0x38), shapes = rd32(d, 0x3C), fs = rd32(d, 0x68), styles = rd32(d, 0x6C), tm = rd32(d, 0x98), mats = rd32(d, 0x9C);
    need(d, sh, u64(shapes) * 0x18);
    for (u32 i = 0; i < shapes; ++i) {
        const u32 o = sh + 0x18 * i;
        Shape s;
        s.x0 = f32_at(d, o), s.y0 = f32_at(d, o + 4), s.x1 = f32_at(d, o + 8), s.y1 = f32_at(d, o + 0xC);
        // The first group's first item with a picture fill.
        const u32 groups = rd32(d, o + 0x10), gcount = rd32(d, o + 0x14);
        if (gcount) {
            need(d, groups, 8);
            const u32 items = rd32(d, groups), icount = rd32(d, groups + 4);
            for (u32 k = 0; k < icount && s.image < 0; ++k) {
                need(d, items + 0xC * k, 0xC);
                const int style = s16(rd16(d, items + 0xC * k));
                if (style < 0 || u32(style) >= styles) continue;
                const u32 so = fs + 8 * u32(style);
                need(d, so, 8);
                const u16 kind = rd16(d, so);
                if (kind != 3 && kind != 4) continue;
                const u32 mi = rd32(d, so + 4);
                if (mi < mats) {
                    need(d, tm + 0x40 * mi, 0x40);
                    for (int e = 0; e < 16; ++e) s.m[e] = f32_at(d, tm + 0x40 * mi + 4 * u32(e));
                }
                s.image = rd16(d, so + 2);
            }
        }
        m.shapes_.push_back(s);
    }
    // Sprite 0, the root timeline (0x30): its placements, their keys, and the frames' lists.
    const u32 sp = rd32(d, 0x30);
    need(d, sp, 0x24);
    const u32 pl = rd32(d, sp), pcount = rd32(d, sp + 4), fr = rd32(d, sp + 0x18), fcount = rd32(d, sp + 0x1C);
    m.total_ = s32(rd32(d, sp + 0x20));
    need(d, pl, u64(pcount) * 0x28);
    for (u32 i = 0; i < pcount; ++i) {
        const u32 o = pl + 0x28 * i;
        Placement p;
        p.kind = s16(rd16(d, o)), p.first = s16(rd16(d, o + 2)), p.end = s16(rd16(d, o + 4)), p.index = s16(rd16(d, o + 6));
        const u32 ko = rd32(d, o + 0x18), kn = rd32(d, o + 0x1C);
        need(d, ko, u64(kn) * 0x24);
        for (u32 k = 0; k < kn; ++k) {
            const u32 q = ko + 0x24 * k;
            Key key;
            key.sx = f32_at(d, q + 8), key.sy = f32_at(d, q + 0xC), key.x = f32_at(d, q + 0x10), key.y = f32_at(d, q + 0x14);
            for (int c = 0; c < 4; ++c) key.mul[c] = float(d[q + 0x18 + u32(c)]) / 128.0f;
            p.keys.push_back(key);
        }
        m.placements_.push_back(std::move(p));
    }
    need(d, fr, u64(fcount) * 8);
    for (u32 i = 0; i < fcount; ++i) {
        const u32 lo = rd32(d, fr + 8 * i), ln = rd32(d, fr + 8 * i + 4);
        need(d, lo, u64(ln) * 2);
        std::vector<int> list;
        for (u32 k = 0; k < ln; ++k) list.push_back(s16(rd16(d, lo + 2 * k)));
        m.frame_lists_.push_back(std::move(list));
    }
    return m;
}

std::vector<FlashQuad> FlashMovie::at(float time) const {
    std::vector<FlashQuad> out;
    const float f = time * rate;
    if (frames > 0 && f >= float(frames)) return out; // past its end nothing is drawn
    if (frame_lists_.empty() || total_ <= 0) return out;
    const int i = int(f) % total_;
    const std::vector<int>& list = frame_lists_[size_t(std::min(i, int(frame_lists_.size()) - 1))];
    for (int pi : list) {
        if (pi < 0 || size_t(pi) >= placements_.size()) continue;
        const Placement& p = placements_[size_t(pi)];
        if (p.kind != 0 || p.index < 0 || size_t(p.index) >= shapes_.size() || p.keys.empty()) continue;
        // The keys k and k + 1 blended by the frame's fraction (the player does not round).
        const float k = f - float(p.first);
        const int k0 = std::clamp(int(k), 0, int(p.keys.size()) - 1);
        Key key = p.keys[size_t(k0)];
        if (k0 + 1 < int(p.keys.size())) {
            const Key& n = p.keys[size_t(k0 + 1)];
            const float t = k - float(k0);
            key.sx += (n.sx - key.sx) * t, key.sy += (n.sy - key.sy) * t, key.x += (n.x - key.x) * t, key.y += (n.y - key.y) * t;
            for (int c = 0; c < 4; ++c) key.mul[c] += (n.mul[c] - key.mul[c]) * t;
        }
        const Shape& s = shapes_[size_t(p.index)];
        if (s.image < 0) continue;
        FlashQuad q;
        q.x0 = key.x + s.x0 * key.sx, q.y0 = key.y + s.y0 * key.sy, q.x1 = key.x + s.x1 * key.sx, q.y1 = key.y + s.y1 * key.sy;
        // Shape space to the picture: u = m0 x + m4 y + m12, v = m1 x + m5 y + m13.
        q.u0 = s.m[0] * s.x0 + s.m[4] * s.y0 + s.m[12], q.v0 = s.m[1] * s.x0 + s.m[5] * s.y0 + s.m[13];
        q.u1 = s.m[0] * s.x1 + s.m[4] * s.y1 + s.m[12], q.v1 = s.m[1] * s.x1 + s.m[5] * s.y1 + s.m[13];
        q.image = s.image;
        q.r = key.mul[0], q.g = key.mul[1], q.b = key.mul[2], q.a = key.mul[3];
        out.push_back(q);
    }
    return out;
}

} // namespace gt4r::menu
