// The boxes' packing as the program runs it every picture (docs/formats/menu.md "Boxes"): the
// size request bottom up (part of a composite's update, slot 96), the allocation top down (part
// of its draw, slot 97). A widget takes part in its box's packing when it is no face (`is_face`
// 0; the face classes are faces), is visible and its `packing` equals the box's. Four kinds of
// box place their children: HBox / VBox in a line (mDBox), FBox each by itself in the box (also a
// page, a ScaleBar, an OptionMenu, a SliderBar), MBox in a grid of `column` columns; any other
// composite only gives each child the size it asks for. Faces keep their rectangle as fractions
// of their parent's, so they follow a box that grows.

#include "gt4r/menu/runtime_internal.h"

#include <algorithm>
#include <map>
#include <vector>

namespace gt4r::menu {

using script::Value;

namespace {

enum class BoxKind { None, Line, Free, Grid };

BoxKind kind_of(const LiveWidget& w) {
    if (w.is("HBox") || w.is("VBox")) return BoxKind::Line;
    if (w.is("FBox") || w.is("RootWindow") || w.is("ScaleBar") || w.is("OptionMenu") || w.is("SliderBar")) return BoxKind::Free;
    if (w.is("MBox")) return BoxKind::Grid;
    return BoxKind::None;
}

bool packing(const LiveWidget& w) { return w.data.number("packing") != 0; }
bool takes_part(const LiveWidget& box, const LiveWidget& c) { return !is_face(c) && c.visible() && packing(box) == packing(c); }
float num(const LiveWidget& w, const char* name, float fallback = 0) { return w.data.has(name) ? w.data.number(name) : fallback; }
bool flag(const LiveWidget& w, const char* a, const char* b) { return num(w, a) != 0 || num(w, b) != 0; }

struct Box {
    float x = 0, y = 0, w = 0, h = 0;
};
// A widget's rectangle; without one, the prototype's size (the disc's
// script/Prototypes.mwidget: a page 640 x 480, a window 128 x 128).
Box rect_of(const LiveWidget& w) {
    if (const std::optional<Rect> g = w.data.rect("geometry")) return {g->x, g->y, g->w, g->h};
    if (w.is("RootWindow") || w.is("Project")) return {0, 0, 640, 480};
    if (w.is("ColorWindow")) return {0, 0, 100, 100};
    return is_face(w) ? Box{} : Box{0, 0, 128, 128};
}
void put(LiveWidget& w, const Box& b) {
    const std::optional<Rect> g = w.data.rect("geometry");
    if (g && g->x == b.x && g->y == b.y && g->w == b.w && g->h == b.h) return;
    w.set_property("x", Value::number(b.x)), w.set_property("y", Value::number(b.y));
    w.set_property("w", Value::number(b.w)), w.set_property("h", Value::number(b.h));
}

struct Pads {
    float l, r, t, b;
};
// `border_width` sets both pads of its axis; a `pad_*` its own side.
Pads pads_of(const LiveWidget& w) {
    const float bw = num(w, "border_width"), bh = num(w, "border_height");
    return {num(w, "pad_left", bw), num(w, "pad_right", bw), num(w, "pad_top", bh), num(w, "pad_bottom", bh)};
}

struct Packer {
    std::map<const LiveWidget*, Box> request; // a widget's asked size (its +0x4C, +0x50)
    struct LineInfo {
        int count = 0, expanding = 0;
    };
    std::map<const LiveWidget*, LineInfo> lines;

    // What a child asks of its box (0x263B80): its size, or with `packing` its request on the
    // axes it allocates (`pack_allocate_x/y`, on by default), at least its minimum.
    Box asked(const LiveWidget& c) {
        Box b = rect_of(c);
        if (packing(c)) {
            const Box r = request[&c];
            if (num(c, "pack_allocate_x", 1) != 0) b.w = r.w;
            if (num(c, "pack_allocate_y", 1) != 0) b.h = r.h;
            b.w = std::max(b.w, num(c, "minimum_width")), b.h = std::max(b.h, num(c, "minimum_height"));
        }
        return b;
    }

    // The size request, bottom up.
    void update(LiveWidget& w) {
        if (w.is("TextFace")) {
            float tw = 0, th = 0;
            if (text_measure().measure) text_measure().measure(w, tw, th);
            request[&w] = {0, 0, tw, th};
            return;
        }
        if (w.children.empty() || !w.visible()) return;
        for (const auto& c : w.children) update(*c);
        const BoxKind k = kind_of(w);
        if (k == BoxKind::None) return;
        const Pads p = pads_of(w);
        Box& r = request[&w];
        if (k == BoxKind::Line) {
            const bool across = w.is("HBox");
            float along = 0, side = 0;
            LineInfo& li = lines[&w];
            li = {};
            for (const auto& c : w.children) {
                if (!takes_part(w, *c)) continue;
                const Box a = asked(*c);
                if (li.count) along += num(w, "spacing");
                along += across ? a.w : a.h, side = std::max(side, across ? a.h : a.w);
                li.expanding += across ? flag(*c, "pack_expand_x", "x_expand") : flag(*c, "pack_expand_y", "y_expand");
                ++li.count;
            }
            if (across) r.w = along + p.l + p.r, r.h = side + p.t + p.b;
            else r.w = side + p.l + p.r, r.h = along + p.t + p.b;
        } else if (k == BoxKind::Free) {
            float mw = 0, mh = 0;
            for (const auto& c : w.children)
                if (takes_part(w, *c)) {
                    const Box a = asked(*c);
                    mw = std::max(mw, a.w), mh = std::max(mh, a.h);
                }
            r.w = mw + p.l + p.r, r.h = mh + p.t + p.b;
        } else {
            const int columns = int(num(w, "column", 2));
            const float cs = num(w, "column_spacing"), rs = num(w, "row_spacing");
            int n = 0, rows = 0;
            float row_w = 0, row_h = 0, max_w = 0, total_h = 0;
            for (const auto& c : w.children) {
                if (!takes_part(w, *c)) continue;
                const Box a = asked(*c);
                if (n > 0) row_w += cs;
                row_w += a.w, row_h = std::max(row_h, a.h), ++n;
                if (n == columns) {
                    max_w = std::max(max_w, row_w);
                    if (rows > 0) total_h += rs;
                    total_h += row_h, n = 0, row_h = row_w = 0, ++rows;
                }
            }
            if (n > 0) {
                max_w = std::max(max_w, row_w);
                if (rows > 0) total_h += rs;
                total_h += row_h;
            }
            r.w = max_w + p.l + p.r, r.h = total_h + p.t + p.b;
        }
    }

    // A child takes the size it asks for (0x263C40).
    void to_request(LiveWidget& c) {
        if (is_face(c) || !c.visible() || !packing(c)) return;
        Box b = rect_of(c);
        const Box a = asked(c);
        b.w = a.w, b.h = a.h;
        put(c, b);
    }

    // A face follows its parent's size: its rectangle scales when the parent's changes (the
    // program keeps it as fractions of the parent, 0x268478).
    void follow(LiveWidget& c, const LiveWidget& parent) {
        if (!is_face(c) || !c.data.rect("geometry")) return;
        const Box pb = rect_of(parent);
        if (!c.data.has("pack_pw")) {
            c.set_property("pack_pw", Value::number(pb.w)), c.set_property("pack_ph", Value::number(pb.h));
            return;
        }
        const float ow = c.data.number("pack_pw"), oh = c.data.number("pack_ph");
        if (ow == pb.w && oh == pb.h) return;
        Box b = rect_of(c);
        if (ow > 0) b.x *= pb.w / ow, b.w *= pb.w / ow;
        if (oh > 0) b.y *= pb.h / oh, b.h *= pb.h / oh;
        put(c, b);
        c.set_property("pack_pw", Value::number(pb.w)), c.set_property("pack_ph", Value::number(pb.h));
    }

    void child_draw(LiveWidget& c, const LiveWidget& parent) {
        follow(c, parent);
        draw(c);
    }

    // The allocation, top down.
    void draw(LiveWidget& w) {
        if (!w.visible()) return;
        const BoxKind k = kind_of(w);
        const Box self = rect_of(w), r = request[&w];
        const Pads p = pads_of(w);
        if (k == BoxKind::Line) {
            const bool across = w.is("HBox");
            const float ps = across ? p.l : p.t, pe = across ? p.r : p.b, cs = across ? p.t : p.l, ce = across ? p.b : p.r;
            const LineInfo li = lines[&w];
            const float extra = li.expanding > 0 ? ((across ? self.w : self.h) - (across ? r.w : r.h)) / float(li.expanding) : 0.0f;
            const float room = (across ? self.h : self.w) - (cs + ce), gap = li.count >= 2 ? num(w, "spacing") : 0.0f;
            float front = ps, back = (across ? self.w : self.h) - pe;
            for (const auto& cp : w.children) {
                LiveWidget& c = *cp;
                to_request(c);
                if (takes_part(w, c)) {
                    const Box cb = rect_of(c);
                    float cm = across ? cb.w : cb.h, cc = across ? cb.h : cb.w, om = 0, oc = 0;
                    float slot = cm;
                    const bool ex_along = across ? flag(c, "pack_expand_x", "x_expand") : flag(c, "pack_expand_y", "y_expand");
                    const bool fill_along = across ? flag(c, "pack_fill_x", "x_fill") : flag(c, "pack_fill_y", "y_fill");
                    const bool ex_cross = across ? flag(c, "pack_expand_y", "y_expand") : flag(c, "pack_expand_x", "x_expand");
                    const bool fill_cross = across ? flag(c, "pack_fill_y", "y_fill") : flag(c, "pack_fill_x", "x_fill");
                    if (ex_along) {
                        slot = cm + extra;
                        if (fill_along) cm += extra;
                        else om = extra * 0.5f;
                    }
                    if (ex_cross) {
                        if (fill_cross) cc = room;
                        else oc = (room - cc) * 0.5f;
                    }
                    float pos;
                    if (num(c, "pack_end") != 0 || num(c, "pack_reverse") != 0) pos = back - slot + om, back -= slot + gap;
                    else pos = front + om, front += slot + gap;
                    if (across) put(c, {pos, cs + oc, std::max(cm, 0.0f), std::max(cc, 0.0f)});
                    else put(c, {cs + oc, pos, std::max(cc, 0.0f), std::max(cm, 0.0f)});
                }
                child_draw(c, w);
            }
        } else if (k == BoxKind::Free) {
            const float ew = self.w - r.w, eh = self.h - r.h;
            for (const auto& cp : w.children) {
                LiveWidget& c = *cp;
                to_request(c);
                if (takes_part(w, c)) {
                    Box b = rect_of(c);
                    float ox = 0, oy = 0;
                    if (flag(c, "pack_expand_x", "x_expand")) {
                        b.x = p.l;
                        if (flag(c, "pack_fill_x", "x_fill")) b.w += ew;
                        else ox = ew * 0.5f;
                    }
                    if (flag(c, "pack_expand_y", "y_expand")) {
                        b.y = p.t;
                        if (flag(c, "pack_fill_y", "y_fill")) b.h += eh;
                        else oy = eh * 0.5f;
                    }
                    put(c, {b.x + ox, b.y + oy, std::max(b.w, 0.0f), std::max(b.h, 0.0f)});
                }
                child_draw(c, w);
            }
        } else if (k == BoxKind::Grid) {
            const int columns = int(num(w, "column", 2));
            std::map<int, float> col_w, row_h;
            std::map<int, bool> col_ex, row_ex;
            int col = 0, row = 0;
            row_h[0] = 0;
            for (const auto& cp : w.children) {
                LiveWidget& c = *cp;
                if (!takes_part(w, c)) continue;
                if (col == columns) ++row, row_h[row] = 0, col = 0;
                if (row == 0) col_w[col] = 0, col_ex[col] = false;
                const Box a = asked(c);
                col_w[col] = std::max(col_w[col], a.w), row_h[row] = std::max(row_h[row], a.h);
                if (flag(c, "pack_expand_x", "x_expand")) col_ex[col] = true;
                if (flag(c, "pack_expand_y", "y_expand")) row_ex[row] = true;
                ++col;
            }
            const int rows = col == columns ? row + 1 : row; // as the program counts them
            int ncex = 0, nrex = 0;
            for (int i = 0; i < col; ++i) ncex += col_ex[i];
            for (int i = 0; i < rows; ++i) nrex += row_ex[i];
            const float ew = ncex > 0 ? (self.w - r.w) / float(ncex) : 0.0f, eh = nrex > 0 ? (self.h - r.h) / float(nrex) : 0.0f;
            const float cgap = col >= 2 ? num(w, "column_spacing") : 0.0f, rgap = rows >= 2 ? num(w, "row_spacing") : 0.0f;
            float y = p.t, front = p.l, back = self.w - p.r;
            col = 0, row = 0;
            for (const auto& cp : w.children) {
                LiveWidget& c = *cp;
                to_request(c);
                if (takes_part(w, c)) {
                    const Box cb = rect_of(c);
                    float cw = cb.w, ch = cb.h, cellw = col_w[col], cellh = row_h[row], ox = 0, oy = 0;
                    if (col_ex[col]) {
                        cellw += ew;
                        if (flag(c, "pack_fill_x", "x_fill")) cw = cellw;
                        else ox = (cellw - cw) * 0.5f;
                    }
                    if (row_ex[row]) {
                        cellh += eh;
                        if (flag(c, "pack_fill_y", "y_fill")) ch = cellh;
                        else oy = (cellh - ch) * 0.5f;
                    }
                    float x;
                    if (num(c, "pack_end") != 0 || num(c, "pack_reverse") != 0) x = back - cellw + ox, back -= cellw + cgap;
                    else x = front + ox, front += cellw + cgap;
                    put(c, {x, y + oy, std::max(cw, 0.0f), std::max(ch, 0.0f)});
                    if (++col == columns) back = self.w - p.r, y += cellh + rgap, col = 0, front = p.l, ++row;
                }
                child_draw(c, w);
            }
        } else {
            for (const auto& cp : w.children) {
                to_request(*cp);
                child_draw(*cp, w);
            }
        }
    }
};

} // namespace

// The faces' fractions are of their parent's size as it was before any packing (the
// program's load, 0x268478): noted when the page is made, before its scripts run; a copy of a
// widget keeps them.
void note_face_sizes(LiveWidget& w) {
    for (const auto& c : w.children) {
        if (is_face(*c) && c->data.rect("geometry") && !c->data.has("pack_pw")) {
            const Box pb = rect_of(w);
            c->set_property("pack_pw", Value::number(pb.w)), c->set_property("pack_ph", Value::number(pb.h));
        }
        note_face_sizes(*c);
    }
}

void pack_page(LiveWidget& page) {
    note_face_sizes(page); // (widgets made since)
    Packer p;
    p.update(page);
    p.draw(page);
}

} // namespace gt4r::menu
