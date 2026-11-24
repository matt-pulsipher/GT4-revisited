#pragma once

// The menus' pointer (the render context's cursor, docs/formats/menu.md "The cursor"): the
// common project's `CursorRoot::Cursor` picture hanging from its tip, which travels to a point
// of the focused widget chosen by the widget's `cursor_align` and turns to that point's angle.

#include "gt4r/app/menu_page_drawer.h"

#include <cmath>

namespace gt4r {

class MenuCursor {
public:
    // One picture (0x230B68, 0x230CF8, 0x230378): the target from the focused widget, then a
    // step toward it. `visible`: the scripts' cursor_visible.
    void step(menu::Runtime& rt) {
        const bool visible = rt.cursor_visible() && rt.common_project();
        LiveWidget* focus = visible ? rt.cursor_widget() : nullptr; // a list box: its focused row
        if (focus && focus->shown() && (!focus->data.has("cursor_target") || focus->data.number("cursor_target") != 0)) {
            const Rect at = focus->screen_rect();
            const int align = focus->data.has("cursor_align") ? int(focus->data.number("cursor_align")) : 8;
            const float r[4] = {at.x, at.y, at.x + at.w, at.y + at.h};
            bool changed = align != align_;
            for (int i = 0; i < 4; ++i) changed = changed || std::fabs(r[i] - rect_[i]) > 0.1f;
            if (changed) {
                for (int i = 0; i < 4; ++i) rect_[i] = r[i];
                align_ = align;
                aim();
            }
        }
        // Shown again: it starts from where it last aimed (0x2306D0).
        if (visible && !visible_) x_ = tx_, y_ = ty_;
        visible_ = visible;
        if (!visible || !moving_) return;
        const float k = 0.15f; // cursor_speed_ratio 1
        const float dx = std::fabs(tx_ - x_), dy = std::fabs(ty_ - y_);
        x_ = dx < 1 ? tx_ : x_ + (tx_ - x_) * k;
        y_ = dy < 1 ? ty_ : y_ + (ty_ - y_) * k;
        if (dx < 1 && dy < 1) {
            const float da = std::fabs(ta_ - angle_);
            angle_ = da < 1 ? ta_ : angle_ + (ta_ - angle_) * k;
            if (da < 1) moving_ = false;
        }
    }

    // The picture with its tip (the middle of its top edge) on the pointer, turned clockwise by
    // its angle about the tip (0x230558).
    void draw(menu::Runtime& rt, PageDrawer& drawer, float x, float scale) const {
        if (!visible_ || !rt.common_project()) return;
        const std::shared_ptr<LiveWidget> root = rt.common_page("CursorRoot");
        const std::shared_ptr<LiveWidget> cursor = root ? root->child("Cursor") : nullptr;
        if (!cursor) return;
        const std::optional<Rect> size = cursor->data.rect("geometry");
        const float w = size ? size->w : 32, h = size ? size->h : 32;
        LiveWidget pointer;
        pointer.data = cursor->data;
        pointer.set_property("x", script::Value::number(0));
        pointer.set_property("y", script::Value::number(0));
        pointer.set_property("w", script::Value::number(w));
        pointer.set_property("h", script::Value::number(h));
        pointer.name = root->name;
        const float a = angle_ * 0.017453292f, cs = std::cos(a), sn = std::sin(a);
        Affine m;
        m.a = cs, m.b = sn, m.c = -sn, m.d = cs;
        m.e = x_ - w / 2 * cs, m.f = y_ - w / 2 * sn;
        drawer.draw_one(*rt.common_project(), root->name, pointer, x, 0, scale, m);
    }

private:
    // The point and angle for the kept rectangle and align.
    void aim() {
        const menu::CursorAim a = menu::cursor_aim(rect_[0], rect_[1], rect_[2], rect_[3], align_);
        tx_ = a.x, ty_ = a.y, ta_ = a.angle;
        moving_ = true;
    }

    float x_ = 0, y_ = 0, angle_ = 0;          // the pointer now
    float tx_ = 320, ty_ = 240, ta_ = 0;       // where it goes
    float rect_[4] = {0, 0, 0, 0};             // the focused widget's x1, y1, x2, y2 kept
    int align_ = 8;
    bool moving_ = false, visible_ = false;
};

} // namespace gt4r
