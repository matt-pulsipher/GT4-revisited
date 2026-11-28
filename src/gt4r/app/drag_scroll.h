#pragma once

// A finger (or the mouse with its button held) dragged over a list: the rows follow it, one row for each `row` units it moves.
// feed() takes each of the pointer's actions and says how many rows the list moved since the last one, counted as the wheel's
// turns are (positive: toward the first row, the finger moving down). A drag counts only when it started on the list.

#include "app/ui.h"

#include <cmath>

namespace gt4r {

class DragScroll {
public:
    using Action = gt3r::PointerInput::Action;

    int feed(const Action& a, const gt3r::UiRect& area, float row) { return feed(a, area.contains(a.sx, a.sy), row); }
    int feed(const Action& a, bool started_here, float row) {
        if (a.kind != Action::Kind::Drag && a.kind != Action::Kind::DragEnd) return 0;
        if (!on_) {
            if (a.kind != Action::Kind::Drag || !started_here || row <= 0) return 0;
            on_ = true, row_ = row, moved_ = 0;
        }
        const int total = int(std::lround((a.y - a.sy) / row_));
        const int by = total - moved_;
        moved_ = total;
        if (a.kind == Action::Kind::DragEnd) on_ = false;
        return by;
    }
    bool active() const { return on_; }

private:
    bool on_ = false;
    float row_ = 1;
    int moved_ = 0;
};

} // namespace gt4r
