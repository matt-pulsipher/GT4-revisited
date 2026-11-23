#pragma once

// What the runtime's parts share: the widget classes' own members and keys (widget_classes.cpp),
// the actors (actors.cpp), the game's objects (game_natives.cpp).

#include "gt4r/menu/runtime.h"

namespace gt4r::menu {

// Moves a value a ratio of the way to its target; within one unit it arrives. True once there
// (the program's 0x25BFE0).
inline bool interpolate(float& v, float target, float ratio) {
    const float d = target - v;
    if (d > -1.0f && d < 1.0f) {
        v = target;
        return true;
    }
    v += d * ratio;
    return false;
}

bool is_select_box(const LiveWidget& w);
// Sizes and places a select box's rows (the program's 0x2DA5D0, 0x2DA6F0, 0x2DAA10): `warp`
// puts them where the index says at once; else they move a part of the way (a frame).
void select_box_layout(LiveWidget& box, bool warp);

// ---- actors.cpp
// The program's actors (MFadeActor, MMoveActor, MBlinkActor, ...), transitions and watchers.
void install_actor_natives(Runtime& rt, script::Module& menu);
// `widget.actor = actor` (nil takes it away).
void attach_actor(LiveWidget& w, const script::Value& actor);
// One frame of an actor.
void step_actor(script::Heap& actor);
// A watcher's frame: true when its function is due.
bool step_watcher(Object& watcher);
// A due watcher's function and its argument; watcher_called counts the call once it is made.
bool watcher_call(Object& watcher, script::Value& function, script::Value& context);
void watcher_called(Object& watcher);
// Moves a scroll window's content so a widget inside it shows.
void scroll_into_view(LiveWidget& inside);
// A transition's cover of the page: 0 none, 1 whole.
float transition_cover(const script::Value& transition);
void step_transition(const script::Value& transition);
// Where a transition is: -1 none or over, 0 covering the page, 1 covered, 2 uncovering.
int transition_phase(const script::Value& transition);

// ---- widget_classes.cpp
// A member of the widget's own class (MRootWindow, MSelectBox, MOptionMenu, ...); null when
// the class has none of that name.
std::shared_ptr<script::Reference> widget_class_member(Runtime& rt, const std::shared_ptr<LiveWidget>& w, const std::string& name);
// A key the focused widget's class deals with itself (a select box's index, an option menu's
// list, a slider). True when it did.
bool widget_key(Runtime& rt, LiveWidget& focus, Key k);
// `widget.index = i`; `notify`: the script's onValueChanged is called.
void widget_set_index(Runtime& rt, LiveWidget& w, int index, bool notify);
void widget_children_changed(Runtime& rt, LiveWidget& w);
// One frame of the widgets' own movement (scrolling, a list's rows).
void step_widgets(Runtime& rt, LiveWidget& page);
// The widget of a page the focus goes to first.
std::shared_ptr<LiveWidget> first_focus(LiveWidget& page);
// The widgets of a page that can take the focus now.
void collect_focusable(const std::shared_ptr<LiveWidget>& w, std::vector<std::shared_ptr<LiveWidget>>& out);
// Text measured as the page's font draws it: the runtime has no fonts, so the app gives one.
struct TextMeasure {
    std::function<void(const LiveWidget& text, float& w, float& h)> measure;
};
TextMeasure& text_measure();

// ---- game_natives.cpp
// main::game and the classes of the game's state (MGame, MOption, MGarage, MCarData, ...).
void install_game_natives(Runtime& rt, script::Module& menu);
// The objects the main script makes when it does not run (a disc without one).
void install_fallback_world(Runtime& rt);
// An MCarGarage object for a car by its label (what a car face's `car_data` is).
script::Value make_car_object(Runtime& rt, const std::string& label, int paint);

} // namespace gt4r::menu
