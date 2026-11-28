// A finger dragged over one of the app's lists (src/gt4r/app/drag_scroll.h): the rows follow it. No game data.

#include "test_framework.h"

#include "gt4r/app/drag_scroll.h"

using gt4r::DragScroll;
using Action = gt3r::PointerInput::Action;
using Kind = Action::Kind;

namespace {
Action drag(Kind kind, float from_y, float to_y, float x = 50) {
    Action a{kind, x, to_y, x, from_y};
    a.finger = true;
    return a;
}
} // namespace

TEST(drag_scroll_follows_the_finger) {
    const gt3r::UiRect list{20, 40, 300, 170}; // ten rows of 17
    DragScroll d;
    // Down a row and a half (the nearest whole row), then on to three rows: the list moves toward its first row, as the wheel
    // turned up does.
    CHECK(d.feed(drag(Kind::Drag, 100, 125), list, 17) == 1);
    CHECK(d.active());
    CHECK(d.feed(drag(Kind::Drag, 100, 151), list, 17) == 2);
    // Back up past where it started: the other way.
    CHECK(d.feed(drag(Kind::Drag, 100, 66), list, 17) == -5);
    // Let go where it is: nothing more, and the drag is over.
    CHECK(d.feed(drag(Kind::DragEnd, 100, 66), list, 17) == 0);
    CHECK(!d.active());
}

TEST(drag_scroll_only_from_the_list) {
    const gt3r::UiRect list{20, 40, 300, 170};
    DragScroll d;
    // A drag that started beside the list (on a slider, another panel) moves nothing, even when it goes over the list.
    CHECK(d.feed(drag(Kind::Drag, 100, 160, 400), list, 17) == 0);
    CHECK(!d.active());
    CHECK(d.feed(drag(Kind::DragEnd, 100, 160, 400), list, 17) == 0);
    // Taps, hovers and the wheel are not drags.
    CHECK(d.feed(Action{Kind::Click, 50, 100}, list, 17) == 0);
    Action wheel{Kind::Wheel, 50, 100};
    wheel.wheel = 3;
    CHECK(d.feed(wheel, list, 17) == 0);
    // Once started on the list, it goes on wherever the finger goes.
    CHECK(d.feed(drag(Kind::Drag, 100, 105), list, 17) == 0);
    CHECK(d.active());
    CHECK(d.feed(drag(Kind::DragEnd, 100, 300, 600), list, 17) == 12);
    CHECK(!d.active());
}
