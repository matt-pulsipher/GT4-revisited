// The boxes' packing: a window with `packing` takes the size it asks for and its box places it
// (VBox one under the other, HBox side by side, any other box where the layout has it). A
// dialog grows with its message this way. ❓ A reduced form of the program's two passes
// (MDBox's size request 0x2E9880 and allocation 0x2E99E0): the expanding children share what
// is left over, `pack_fill` stretches a child across its place, `pack_reverse` turns the order;
// `pack_end` and the per-child padding are not written.

#include "gt4r/menu/runtime_internal.h"

#include <algorithm>

namespace gt4r::menu {

using script::Value;

namespace {

struct Size {
    float w = 0, h = 0;
};

// What a box places: its windows that ask for it, and a face the layout marks as no face
// (`is_face` 0: a dialog's message). A face with `packing` is the box's own look: it takes the
// box's size.
bool packed(const LiveWidget& w) {
    if (w.data.number("packing") == 0 || !w.visible()) return false;
    return w.is_composite() || (w.data.has("is_face") && w.data.number("is_face") == 0);
}

bool any_packed(const LiveWidget& w) {
    for (const auto& c : w.children)
        if (packed(*c)) return true;
    return false;
}

float pad(const LiveWidget& w, const char* side) { return w.data.number(side); }

Size request(LiveWidget& w) {
    const std::optional<Rect> g = w.data.rect("geometry");
    Size own{w.data.has("minimum_width") ? w.data.number("minimum_width") : g ? g->w : 0, w.data.has("minimum_height") ? w.data.number("minimum_height") : g ? g->h : 0};
    if (w.is("TextFace")) {
        // The text's own size: its lines as the text breaks them.
        float tw = 0, th = 0;
        if (text_measure().measure) text_measure().measure(w, tw, th);
        return {std::max(own.w, tw), std::max(own.h, th)};
    }
    if (!w.is_composite() || !any_packed(w)) return own;
    const bool down = w.is("VBox"), across = w.is("HBox");
    const float spacing = w.data.number("spacing");
    Size sum;
    int n = 0;
    for (const auto& c : w.children) {
        if (!packed(*c)) continue;
        const Size r = request(*c);
        if (down) sum.h += r.h + (n ? spacing : 0), sum.w = std::max(sum.w, r.w);
        else if (across) sum.w += r.w + (n ? spacing : 0), sum.h = std::max(sum.h, r.h);
        else sum.w = std::max(sum.w, r.w), sum.h = std::max(sum.h, r.h);
        ++n;
    }
    sum.w += pad(w, "pad_left") + pad(w, "pad_right") + 2 * pad(w, "border_width");
    sum.h += pad(w, "pad_top") + pad(w, "pad_bottom") + 2 * pad(w, "border_height");
    // A box that orders its children is as long as they are (rows a script hid are left out),
    // and at least its minimum.
    const float least_w = w.data.number("minimum_width"), least_h = w.data.number("minimum_height");
    if (down) return {std::max(own.w, sum.w), std::max(least_h, sum.h)};
    if (across) return {std::max(least_w, sum.w), std::max(own.h, sum.h)};
    return {std::max(own.w, sum.w), std::max(own.h, sum.h)};
}

// A widget's new size. Its own faces that the layout gave a rectangle (a frame, a shadow) grow
// and shrink with it. ❓ How the program keeps a face to its box is not read.
void resize(LiveWidget& w, float width, float height) {
    const std::optional<Rect> before = w.data.rect("geometry");
    w.set_property("w", Value::number(width));
    w.set_property("h", Value::number(height));
    if (!before || (before->w == width && before->h == height)) return;
    for (const auto& c : w.children) {
        if (c->is_composite() || packed(*c)) continue;
        if (const std::optional<Rect> g = c->data.rect("geometry")) {
            c->set_property("w", Value::number(std::max(g->w + width - before->w, 0.0f)));
            c->set_property("h", Value::number(std::max(g->h + height - before->h, 0.0f)));
        }
    }
}

void allocate(LiveWidget& w) {
    if (!w.is_composite()) return;
    if (!any_packed(w)) {
        for (const auto& c : w.children) allocate(*c);
        return;
    }
    const std::optional<Rect> g = w.data.rect("geometry");
    const float W = g ? g->w : 640.0f, H = g ? g->h : 480.0f;
    const bool down = w.is("VBox"), across = w.is("HBox");
    const float left = pad(w, "pad_left") + pad(w, "border_width"), right = pad(w, "pad_right") + pad(w, "border_width");
    const float top = pad(w, "pad_top") + pad(w, "border_height"), bottom = pad(w, "pad_bottom") + pad(w, "border_height");
    std::vector<std::shared_ptr<LiveWidget>> kids;
    for (const auto& c : w.children)
        if (packed(*c)) kids.push_back(c);
    if (w.data.number("pack_reverse") != 0) std::reverse(kids.begin(), kids.end());
    std::vector<Size> wants;
    float used = 0;
    int expanding = 0;
    const float spacing = w.data.number("spacing");
    for (const auto& c : kids) {
        wants.push_back(request(*c));
        used += down ? wants.back().h : wants.back().w;
        expanding += c->data.number(down ? "pack_expand_y" : "pack_expand_x") != 0;
    }
    used += spacing * float(std::max(int(kids.size()) - 1, 0));
    if (down || across) {
        const float room = (down ? H - top - bottom : W - left - right);
        const float extra = expanding ? std::max(room - used, 0.0f) / float(expanding) : 0.0f;
        float at = down ? top : left;
        for (size_t i = 0; i < kids.size(); ++i) {
            LiveWidget& c = *kids[i];
            const bool expand = c.data.number(down ? "pack_expand_y" : "pack_expand_x") != 0;
            const bool fill = c.data.number(down ? "pack_fill_y" : "pack_fill_x") != 0;
            const bool cross_fill = c.data.number(down ? "pack_fill_x" : "pack_fill_y") != 0;
            const float want = down ? wants[i].h : wants[i].w, slot = want + (expand ? extra : 0.0f);
            const float size = fill ? slot : want, offset = fill ? 0.0f : (slot - want) * 0.5f;
            const float cross_room = down ? W - left - right : H - top - bottom;
            const float cross_want = down ? wants[i].w : wants[i].h;
            const float cross = cross_fill ? cross_room : std::min(cross_want, cross_room);
            const float cross_at = (down ? left : top) + (cross_room - cross) * 0.5f;
            c.set_property(down ? "y" : "x", Value::number(at + offset));
            c.set_property(down ? "x" : "y", Value::number(cross_at));
            resize(c, down ? cross : size, down ? size : cross);
            at += slot + spacing;
        }
    } else {
        // A box that does not order its children: each keeps its place and takes the size it
        // asks for, or the box's room where it fills.
        for (size_t i = 0; i < kids.size(); ++i) {
            LiveWidget& c = *kids[i];
            const bool fill_x = c.data.number("pack_fill_x") != 0, fill_y = c.data.number("pack_fill_y") != 0;
            const float cw = fill_x ? W - left - right : wants[i].w, ch = fill_y ? H - top - bottom : wants[i].h;
            // A child that expands sits in the middle of the room it is given.
            if (fill_x) c.set_property("x", Value::number(left));
            else if (c.data.number("pack_expand_x") != 0) c.set_property("x", Value::number(left + (W - left - right - cw) * 0.5f));
            if (fill_y) c.set_property("y", Value::number(top));
            else if (c.data.number("pack_expand_y") != 0) c.set_property("y", Value::number(top + (H - top - bottom - ch) * 0.5f));
            resize(c, cw, ch);
        }
    }
    for (const auto& c : w.children) allocate(*c);
}

bool uses_packing(const LiveWidget& w) {
    if (w.data.number("packing") != 0) return true;
    for (const auto& c : w.children)
        if (uses_packing(*c)) return true;
    return false;
}

} // namespace

void pack_page(LiveWidget& page) {
    if (!uses_packing(page)) return;
    // A box takes the size its children ask for before it places them: twice settles a box
    // inside a box.
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& c : page.children)
            if (packed(*c)) {
                const Size want = request(*c);
                // In the page it keeps its top; across, an expanding one stays in the middle.
                if (c->data.number("pack_expand_x") != 0) c->set_property("x", Value::number((640.0f - want.w) * 0.5f));
                resize(*c, want.w, want.h);
            }
        allocate(page);
    }
}

} // namespace gt4r::menu
