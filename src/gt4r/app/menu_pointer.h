#pragma once

// The mouse and fingers on the running menus' shown page (menu/pointer.h): what App::pointer() did this frame, from the
// UI's units to the page's 640 x 480 (drawn at `x`, `scale` UI units a page unit, from the top), each to the page.

#include "gt4r/app/app.h"
#include "gt4r/menu/pointer.h"

namespace gt4r {

inline void menu_pointer_frame(App& app, menu::Runtime& rt, const std::vector<menu::DrawnBox>& boxes, menu::PointerDrag& drag, float x, float scale) {
    using Kind = gt3r::PointerInput::Action::Kind;
    if (scale <= 0) return;
    const auto px = [x, scale](float ux) { return (ux - x) / scale; };
    const auto py = [scale](float uy) { return uy / scale; };
    for (const gt3r::PointerInput::Action& a : app.pointer()) {
        switch (a.kind) {
        case Kind::Hover: menu::pointer_hover(rt, boxes, px(a.x), py(a.y)); break;
        case Kind::Click: menu::pointer_tap(rt, boxes, px(a.x), py(a.y)); break;
        case Kind::Wheel: menu::pointer_wheel(rt, boxes, px(a.x), py(a.y), a.wheel); break;
        case Kind::Drag:
        case Kind::DragEnd: menu::pointer_drag(rt, boxes, drag, px(a.sx), py(a.sy), px(a.x), py(a.y), a.kind == Kind::DragEnd); break;
        case Kind::Back: menu::pointer_back(rt); break;
        }
    }
}

} // namespace gt4r
