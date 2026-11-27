#pragma once

// A mouse or a finger on the game's pages (the menus running, runtime.h). The program reads the pad alone; these are
// Gran Turismo 4 Revisited's, made of the moves the pad makes: a hover puts the focus on what the pointer is over, a tap
// puts it there and a tap on what has it is Accept; the wheel and a drag along a list step it as its keys do; the
// mouse's right button is Cancel. What a point is over comes from where the app drew each widget this picture
// (`DrawnBox`, the page's 640 x 480), the last drawn on top; what can take the focus is the focus search's own
// candidates (widget_classes.cpp), so a widget the pad cannot reach is no target either. A scroll box's arrows
// (`dec_arrow`, `inc_arrow`) step the list in it. docs/formats/menu.md "Mouse and
// touch".

#include "gt4r/menu/runtime.h"

#include <memory>
#include <vector>

namespace gt4r::menu {

// A widget as it was drawn: its box through its transforms, cut to its clip, in the page's coordinates.
struct DrawnBox {
    const LiveWidget* widget = nullptr;
    Rect box;
    std::weak_ptr<const script::Heap> alive; // the widget's own (none: one drawn by itself, never a target)
};

// What a point of the shown page is over.
struct PointerHit {
    enum class Kind { None, Widget, SelectRow, ListRow, OptionRow, Arrow } kind = Kind::None;
    std::shared_ptr<LiveWidget> widget; // Widget: what takes the focus; a row: the row's widget
    std::shared_ptr<LiveWidget> box;    // a row's select box, list box or option menu
    int row = -1; // Arrow: -1 back, 1 on
};
PointerHit pointer_hit(Runtime& rt, const std::vector<DrawnBox>& drawn, float x, float y);

// A drag's progress along a list (pointer_drag).
struct PointerDrag {
    std::weak_ptr<LiveWidget> box; // the list it started on (none: it moves nothing)
    bool started = false;
    float left = 0;     // what has not made a step yet (page units along the list)
    float x = 0, y = 0; // where it was last
};

// The pointer's moves on the shown page, (x, y) in its 640 x 480; true when one was used. Nothing while the scripts take
// no keys (the context's event mask) or a transition covers the page.
bool pointer_hover(Runtime& rt, const std::vector<DrawnBox>& drawn, float x, float y);
bool pointer_tap(Runtime& rt, const std::vector<DrawnBox>& drawn, float x, float y);
// `notches` up (> 0) step back along the list under the point (or the focused one), down on.
bool pointer_wheel(Runtime& rt, const std::vector<DrawnBox>& drawn, float x, float y, float notches);
// A press moved from (x0, y0) to (x, y): a step of the list it started on for each row's length it went, the list's
// content following the finger (a drag up is on); `end`: let go.
bool pointer_drag(Runtime& rt, const std::vector<DrawnBox>& drawn, PointerDrag& drag, float x0, float y0, float x, float y, bool end);
// The right button: Cancel.
void pointer_back(Runtime& rt);

} // namespace gt4r::menu
