#pragma once

// The menus' 2D transforms (docs/formats/menu.md "Drawing"): a widget's own `translate`,
// `rotate`, `scale` and `skew` (mWidget, 0x2679B0), and where the cursor aims on the focused
// widget (0x230CF8).

#include "gt4r/menu/layout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string_view>

namespace gt4r::menu {

struct Point {
    float x = 0, y = 0;
};

// A 2D transform of the page's coordinates, row vectors as the program's mTransform:
// x' = x a + y c + e, y' = x b + y d + f.
struct Affine {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    bool identity() const { return a == 1 && b == 0 && c == 0 && d == 1 && e == 0 && f == 0; }
    Point apply(float x, float y) const { return {x * a + y * c + e, x * b + y * d + f}; }
    // `first` applied to a point, then this.
    Affine after(const Affine& l) const {
        return {l.a * a + l.b * c, l.a * b + l.b * d, l.c * a + l.d * c, l.c * b + l.d * d, l.e * a + l.f * c + e, l.e * b + l.f * d + f};
    }
};

// Whether a widget of a class cuts its children to its rectangle when its layout does not say:
// the boxes do (mBox's constructor, 0x200C08, sets `clip`), and so do the select box (0x2D8C90,
// the event halls' and Photo Travel's lists) and the root window (0x232E00); the option menu
// (0x2C7498), the scale bar (0x236860) and the slider bar (0x2DD820), boxes too, turn it off again
// (the slider's value tag stands above its bar); other composites do not cut (mComposite's,
// 0x204F10). The list box a layout's is copied from cuts (a saved state's; its constructor, 0x2AEA98, does not). The layouts write `clip 0` on boxes and `clip 1` on the others.
inline bool clips_by_default(std::string_view type) {
    for (std::string_view box : {"Box", "DBox", "HBox", "VBox", "FBox", "MBox", "RootWindow", "PhotoMapWindow", "SelectBox", "ListBox"})
        if (type == box) return true;
    return false;
}

// An item of a vector property ("scale vector {1 -1}", or an array the scripts set).
inline float vector_item(const Widget& w, std::string_view name, size_t i, float otherwise) {
    const menu::Property* p = w.find(name);
    if (!p || i >= p->values.size()) return otherwise;
    return std::strtof(p->values[i].c_str(), nullptr);
}

// A widget's own transform (mWidget, rebuilt by 0x2679B0) about the middle of its rectangle
// `at`: x moved by y tan(skew x), y by x tan(skew y), scaled, turned clockwise by `rotate`
// degrees, the middle put back, then `translate` added. Identity without any of them.
inline Affine widget_transform(const Widget& w, const Rect& at) {
    const float tx = vector_item(w, "translate", 0, 0), ty = vector_item(w, "translate", 1, 0);
    const float rot = w.number("rotate", 0), sx = vector_item(w, "scale", 0, 1), sy = vector_item(w, "scale", 1, 1);
    const float kx = vector_item(w, "skew", 0, 0), ky = vector_item(w, "skew", 1, 0);
    Affine m;
    m.e = tx, m.f = ty;
    if (rot == 0 && sx == 1 && sy == 1 && kx == 0 && ky == 0) return m;
    constexpr float kRad = 0.017453292f;
    const float tkx = std::tan(kx * kRad), tky = std::tan(ky * kRad), cs = std::cos(rot * kRad), sn = std::sin(rot * kRad);
    // Column form A = Rotate Scale SkewY SkewX: skew x [[1, tkx], [0, 1]], then skew y
    // [[1, 0], [tky, 1]], then the scale, then the turn.
    const float k00 = 1, k01 = tkx, k10 = tky, k11 = tky * tkx + 1;
    const float s00 = sx * k00, s01 = sx * k01, s10 = sy * k10, s11 = sy * k11;
    const float a00 = cs * s00 - sn * s10, a01 = cs * s01 - sn * s11, a10 = sn * s00 + cs * s10, a11 = sn * s01 + cs * s11;
    const float cx = at.x + at.w / 2, cy = at.y + at.h / 2;
    m.a = a00, m.c = a01, m.b = a10, m.d = a11;
    m.e = cx + tx - (a00 * cx + a01 * cy), m.f = cy + ty - (a10 * cx + a11 * cy);
    return m;
}

// A clip cut by another (0x231A50).
inline Rect cut(const Rect& a, const Rect& b) {
    const float x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y), x1 = std::min(a.x + a.w, b.x + b.w), y1 = std::min(a.y + a.h, b.y + b.h);
    return {x0, y0, std::max(x1 - x0, 0.0f), std::max(y1 - y0, 0.0f)};
}

// A clip as the program's scissor has it (0x215F78, 0x215FE0, 0x4A2DF8): the page's 480 lines on the 448 of the frame
// buffer, halved for a field's 224, and the place and the size each cut to a whole line (the place and the width to a
// whole pixel across). So a box that is not a whole number of lines high loses up to a line at its bottom: the gap
// between a regulations page's rows. Back in the page's units.
// (448 / 480 is the program's quotient, rounded toward zero: an exact number of lines comes out one less.) ❓ The page's
// own clip, which loses the frame's last line so (black in the original's captures), is kept whole here.
inline Rect scissor(const Rect& r) {
    if (r.x <= 0 && r.y <= 0 && r.w >= 640 && r.h >= 480) return r;
    constexpr double k = double(0.93333328f) * 0.5; // (0x3F6EEEEE)
    auto line = [](float v) { return float(int(double(v) * k) * (480.0 / 224.0)); };
    return {float(int(r.x)), line(r.y), float(int(r.w)), line(r.h)};
}

// The cursor's target on a widget's rectangle (x1, y1, x2, y2) by its `cursor_align` (the table
// at 0x6987E0): a point two pixels inside the rectangle and the angle the picture is turned
// (degrees, clockwise), kept on the screen.
struct CursorAim {
    float x = 0, y = 0, angle = 0;
};
inline CursorAim cursor_aim(float x1, float y1, float x2, float y2, int align) {
    const float l = x1 + 2, t = y1 + 2, r = x2 - 2, b = y2 - 2;
    const float mx = (l + r) / 2, my = (t + b) / 2;
    CursorAim a{mx, my, -22.5f};
    switch (align) {
    case 0: a = {l, my, 45}; break;
    case 1: a = {l, b, 22.5f}; break;
    case 2: a = {mx, b, 0}; break;
    case 4: a = {r, my, -45}; break;
    case 5: a = {r, t, 45}; break;
    case 6: a = {mx, t, 45}; break;
    case 7: a = {l, t, 45}; break;
    case 8: break;
    default: a = {r, b, -22.5f}; break; // 3, and 9 on
    }
    a.x = a.x < 0 ? 0 : a.x > 640 ? 640 : a.x;
    a.y = a.y < 0 ? 0 : a.y > 480 ? 480 : a.y;
    return a;
}

} // namespace gt4r::menu
