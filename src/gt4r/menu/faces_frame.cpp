#include "gt4r/menu/faces_frame.h"

#include <algorithm>

namespace gt4r::menu {

std::array<FramePart, 9> frame_parts(const Widget& face, float w, float h) {
    // (16 x 16 when the layout gives none: the constructor 0x2999C0.)
    const float fw = face.number("frame_width", 16), fh = face.number("frame_height", 16);
    const float iw = w - 2 * fw, ih = h - 2 * fh;
    return {{{"image_path", {fw, fh, iw, ih}},
             {"t_image_path", {fw, 0, iw, fh}},
             {"b_image_path", {fw, h - fh, iw, fh}},
             {"l_image_path", {0, fh, fw, ih}},
             {"r_image_path", {w - fw, fh, fw, ih}},
             {"tl_image_path", {0, 0, fw, fh}},
             {"tr_image_path", {w - fw, 0, fw, fh}},
             {"bl_image_path", {0, h - fh, fw, fh}},
             {"br_image_path", {w - fw, h - fh, fw, fh}}}};
}

std::vector<Rgba> frame_part_colors(const std::vector<Rgba>& face, const Rect& part, float w, float h) {
    if (face.size() < 4 || w == 0 || h == 0) return face;
    auto at = [&](float x, float y) {
        const float u = x / w, v = y / h;
        auto mix = [&](u8 tl, u8 tr, u8 br, u8 bl) {
            const float top = tl * (1 - u) + tr * u, bottom = bl * (1 - u) + br * u;
            return u8(std::clamp(top * (1 - v) + bottom * v, 0.0f, 255.0f));
        };
        const Rgba &a = face[0], &b = face[1], &c = face[2], &d = face[3];
        return Rgba{mix(a.r, b.r, c.r, d.r), mix(a.g, b.g, c.g, d.g), mix(a.b, b.b, c.b, d.b), mix(a.a, b.a, c.a, d.a)};
    };
    const float x1 = part.x + part.w, y1 = part.y + part.h;
    return {at(part.x, part.y), at(x1, part.y), at(x1, y1), at(part.x, y1)};
}

std::array<float, 4> corner_color(const Rgba& c, float opacity, bool doubled) {
    const float k = doubled ? 1.0f / 128.0f : 1.0f / 255.0f;
    return {c.r * k, c.g * k, c.b * k, std::clamp(c.a * opacity / 255.0f, 0.0f, 1.0f)};
}

} // namespace gt4r::menu
