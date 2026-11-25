#pragma once

#include <array>
#include <optional>

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

// MSwitchActor's colours (r, g, b, a in 0..1) by its button's state, Gran Turismo 4 Prologue's
// (prologue_natives.cpp): read from the disc's program; none for another program.
struct SwitchColors {
    std::array<float, 4> base_active, base_idle, base_insensitive_active, base_insensitive;
    std::array<float, 4> text_active, text_idle, text_insensitive_active, text_insensitive;
    std::array<float, 4> ring_blink_on, ring_blink_off, ring_insensitive, ring_idle;
};
std::optional<SwitchColors> prologue_switch_colors(const RuntimeConfig& config);
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
// A page has been opened (mRootWindow's start, Prologue 0x22FB04 -> 0x24AF40): a panOut that covered
// the screen is over, the new page shows.
void transition_page_opened(const script::Value& transition);
// Where a transition is: -1 none or over, 0 covering the page, 1 covered, 2 uncovering.
int transition_phase(const script::Value& transition);
// What a transition puts on the screen this picture: a colour's cover; the cross's pages'
// opacity and size; the block's timers (32 x 24, row by row; a block shows the screen held at
// the start at alpha min(timer, 1)), and `capture` once when that screen is to be held.
struct TransitionPicture {
    float cover = 0, page_opacity = 1, page_scale = 1;
    const std::vector<float>* blocks = nullptr;
    bool capture = false;
    // A cross transition coming in (0x2182E0): the picture shown last drawn over the new one at
    // `trail_ratio`, its source cut by `trail_diff` pixels a side, then black at 0.1.
    bool trail = false, trail_black = false;
    float trail_ratio = 0, trail_diff = 0;
};
TransitionPicture transition_picture(const script::Value& transition);

// ---- widget_classes.cpp
// A member of the widget's own class (MRootWindow, MSelectBox, MOptionMenu, ...); null when
// the class has none of that name.
std::shared_ptr<script::Reference> widget_class_member(Runtime& rt, const std::shared_ptr<LiveWidget>& w, const std::string& name);
// A key the focused widget's class deals with itself (a select box's index, an option menu's
// list, a slider). True when it did.
bool widget_key(Runtime& rt, LiveWidget& focus, Key k);
// A ScaleBar's key after its script did not take it: the value's step (widget_classes.cpp).
bool scale_bar_key_after(Runtime& rt, LiveWidget& w, Key k);
bool list_key_after(Runtime& rt, LiveWidget& w, Key k);
inline bool class_key_after(Runtime& rt, LiveWidget& w, Key k) { return w.is("ListBox") ? list_key_after(rt, w, k) : scale_bar_key_after(rt, w, k); }
LiveWidget* list_cursor_row(LiveWidget& list);
// `widget.index = i`; `notify`: the script's onValueChanged is called.
void widget_set_index(Runtime& rt, LiveWidget& w, int index, bool notify);
void widget_children_changed(Runtime& rt, LiveWidget& w);
// The class's own step when a widget takes the focus (a scale bar's, 0x238790).
void widget_focus_entered(Runtime& rt, LiveWidget& w);
// Sizes and places a page's widgets that are packed by their boxes (packing.cpp).
void pack_page(LiveWidget& page);
void note_face_sizes(LiveWidget& w); // the faces under it: their parents' sizes they are fractions of
// One frame of the widgets' own movement (scrolling, a list's rows).
void step_widgets(Runtime& rt, LiveWidget& page);
// The widget of a page the focus goes to first.
std::shared_ptr<LiveWidget> first_focus(LiveWidget& page);
// The widgets of a page that can take the focus now.
void collect_focusable(const std::shared_ptr<LiveWidget>& w, std::vector<std::shared_ptr<LiveWidget>>& out);
// The focus search's candidates (the program's collector, 0x26F9E0): each with its rectangle,
// its navigate source and target on the screen, and its group (1 inside the scroll window's
// space the focus is in). `clipped`: a direction search, cut to the clips it is under.
struct FocusCandidate {
    std::shared_ptr<LiveWidget> widget;
    int group = 0;
    Rect rect, source, target;
};
std::vector<FocusCandidate> focus_candidates(LiveWidget& page, bool clipped);
// The visible flag and the layout's visible condition against the widget's state (0x265DC8).
bool condition_holds(const LiveWidget& w);
// Text measured as the page's font draws it: the runtime has no fonts, so the app gives one.
struct TextMeasure {
    std::function<void(const LiveWidget& text, float& w, float& h)> measure;
};
TextMeasure& text_measure();

// A face's own alpha (mWidget's slots 56 to 59; mColorFace 0x2862C0): its first color's, or what a script or an actor set.
inline float own_alpha(const LiveWidget& w) {
    if (w.data.has("alpha")) return w.data.number("alpha", 1);
    const std::vector<Rgba> colors = w.is("TextFace") ? std::vector<Rgba>{} : w.data.colors("color");
    return colors.empty() ? 1.0f : float(colors[0].a) / 255;
}

// ---- game_natives.cpp
// main::game and the classes of the game's state (MGame, MOption, MGarage, MCarData, ...).
void install_game_natives(Runtime& rt, script::Module& menu);
// The objects the main script makes when it does not run (a disc without one).
void install_fallback_world(Runtime& rt);
// What a race request leaves to the program: an event's course from its RACE row, the car
// driven when the scripts name none.
void complete_race_request(Runtime& rt, RaceRequest& r);
// An MCarGarage object for a car by its label (what a car face's `car_data` is).
script::Value make_car_object(Runtime& rt, const std::string& label, int paint);

// ---- race_natives.cpp
// The objects the race's menus call (MQuickWork).
void install_race_natives(Runtime& rt, script::Module& menu);

} // namespace gt4r::menu
