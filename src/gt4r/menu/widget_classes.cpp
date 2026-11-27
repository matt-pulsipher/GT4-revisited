// The widget classes' own members and behavior: the page (MRootWindow), the boxes that choose
// (MSelectBox, MSelectBar, MOptionMenu, MSliderBar, MScaleBar, MListBox), the scroll window and
// the faces. Each class's member names are the program's (its registration tables); what is
// read from the program's routines and what is judged from the scripts is in
// docs/formats/menu.md ("Running").

#include "formats/gs_memory.h"
#include "formats/tex1.h"
#include "gt4r/menu/faces_car.h"
#include "gt4r/menu/logger.h"
#include "gt4r/menu/photo_natives.h"
#include "gt4r/menu/runtime_internal.h"
#include "gt4r/menu/text_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gt4r::menu {

using script::Args;
using script::Kind;
using script::Value;
using script::Vm;

namespace {

using W = std::shared_ptr<LiveWidget>;

std::shared_ptr<script::Variable> constant(Value v) { return std::make_shared<script::Variable>(std::move(v)); }

// The rows of a box that chooses: its children that are windows (the faces are its own look).
std::vector<W> rows_of(const LiveWidget& box) {
    std::vector<W> out;
    for (const auto& c : box.children)
        if (c->is_composite()) out.push_back(c);
    return out;
}

// An option menu's list: popup::vbox (the layouts' own names).
W option_list(const LiveWidget& menu) {
    const W popup = menu.child("popup");
    return popup ? popup->child("vbox") : nullptr;
}

std::vector<W> option_items(const LiveWidget& menu) {
    const W list = option_list(menu);
    return list ? rows_of(*list) : std::vector<W>{};
}

bool popup_open(const LiveWidget& w) { return w.data.number("is_popuped") != 0; }

// ---- the option menu (docs/formats/menu.md "OptionMenu"; mOptionMenu, 0x2C7498 on)
//
// Its index is a counter over the list's rows (0..count, no wrap unless the layout's `repeat`,
// 0x2C9DB0). Opening (0x2C9098) stacks the rows of popup::vbox, puts the list so the chosen row
// lies on the menu, keeps it inside the page's band (a twentieth of the page's height off
// each edge), and fades the popup in; up and down move the index over the sensitive rows and
// scroll a list taller than the band (0x2C98F0, eased each picture by 0x2C9C90); accept keeps
// the index, cancel puts the old one back (0x2C93C0).

void om_play(Runtime& rt, const char* name) {
    const Value arg = Value::string(name);
    rt.sound("sound", "play", Args(&arg, 1));
}

float om_num(const LiveWidget& w, const char* name) { return w.data.number(name); }
void om_set(LiveWidget& w, const char* name, float v) { w.set_property(name, Value::number(v)); }

// The first sensitive row at or after `from` (0x2C9740), or at or before it (0x2C97E8); -1 none.
int option_sensitive(const LiveWidget& menu, int from, bool forward) {
    const std::vector<W> items = option_items(menu);
    if (forward) {
        for (int i = std::max(from, 0); i < int(items.size()); ++i)
            if (!items[size_t(i)]->insensitive()) return i;
    } else {
        for (int i = std::min(from, int(items.size()) - 1); i >= 0; --i)
            if (!items[size_t(i)]->insensitive()) return i;
    }
    return -1;
}

// The index kept in the counter's range (0x57CD48): 0 with no rows, else 0..count - 1.
int option_clamp(const LiveWidget& menu, int index) {
    const int n = int(option_items(menu).size());
    return n <= 0 ? 0 : std::clamp(index, 0, n - 1);
}

// The counter moved by `step` (0x57CF10): wrapping with `repeat`, else held at the ends; true
// when it changed.
bool option_step(LiveWidget& menu, int step) {
    const int n = int(option_items(menu).size());
    if (n <= 0) return false;
    const int before = menu.index();
    int at = before + step;
    if (menu.data.number("repeat") != 0) at = ((at % n) + n) % n;
    else at = std::clamp(at, 0, n - 1);
    menu.set_property("index", Value::integer(at));
    return at != before;
}

// What shows (0x2C8198, 0x2C8410): closed, the chosen row alone at the menu's top left (the
// program's copy of it, `selected_item`, without its active state); open, every row, the
// chosen one active (its dot). A row fills the list's width (0x2C8FA8 marks each x_expand and
// x_fill when the menu does not pack).
void arrange_option_menu(LiveWidget& menu) {
    const W popup = menu.child("popup");
    const W list = option_list(menu);
    if (!popup || !list) return;
    const bool open = popup_open(menu);
    const std::vector<W> items = rows_of(*list);
    const int index = menu.index();
    for (size_t i = 0; i < items.size(); ++i) {
        const bool chosen = int(i) == index;
        if (menu.data.number("packing") == 0) {
            items[i]->set_property("pack_expand_x", Value::integer(1));
            items[i]->set_property("pack_fill_x", Value::integer(1));
        }
        items[i]->set_property("visible", Value::integer(open || chosen));
        items[i]->set_state(kActive, open && chosen);
    }
    for (const auto& c : list->children)
        if (!c->is_composite()) c->set_property("visible", Value::integer(open));
    for (const auto& c : popup->children)
        if (!c->is_composite()) c->set_property("visible", Value::integer(open));
    if (!open) list->set_property("y", Value::number(0)), popup->set_property("opacity", Value::number(1));
}

// The list's rows one under the other (0x2C8FA8): the top pad, each row's height, `spacing`
// between two, the bottom pad. Returns the whole height and the chosen row's top.
float option_stack(const LiveWidget& menu, float& chosen_y) {
    const W list = option_list(menu);
    chosen_y = 0;
    if (!list) return 0;
    const float top = list->data.has("pad_top") ? list->data.number("pad_top") : list->data.number("border_height");
    const float bottom = list->data.has("pad_bottom") ? list->data.number("pad_bottom") : list->data.number("border_height");
    float y = top;
    const std::vector<W> items = rows_of(*list);
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) y += list->data.number("spacing");
        if (int(i) == menu.index()) chosen_y = y;
        const std::optional<Rect> g = items[i]->data.rect("geometry");
        y += g ? g->h : 128.0f;
    }
    return y + bottom;
}

// The list's place on the screen set (its parent's place taken off), and the shadow beside it,
// 4 to the right and 4 down, at its size.
void option_place(LiveWidget& menu, float screen_y) {
    const W popup = menu.child("popup"), list = option_list(menu);
    if (!popup || !list) return;
    list->set_property("y", Value::number(screen_y - popup->screen_rect().y));
    if (const W shadow = popup->child("shadow")) {
        const std::optional<Rect> g = list->data.rect("geometry");
        if (!g) return;
        shadow->set_property("x", Value::number(g->x + 4)), shadow->set_property("y", Value::number(g->y + 4));
        shadow->set_property("w", Value::number(g->w)), shadow->set_property("h", Value::number(g->h));
    }
}

void option_script(Runtime& rt, LiveWidget& menu, const char* name, std::vector<Value> args) {
    if (menu.data.number("has_script") != 0) rt.call_now(menu, name, std::move(args));
}

void option_open(Runtime& rt, LiveWidget& menu) {
    const W popup = menu.child("popup"), list = option_list(menu);
    om_play(rt, "cursor");
    option_script(rt, menu, "onPopupOpen", {rt.context()});
    if (!popup || !list) return;
    menu.set_property("is_popuped", Value::integer(1));
    arrange_option_menu(menu);
    float chosen_y = 0;
    const float total = option_stack(menu, chosen_y);
    om_set(menu, "om_total", total);
    list->set_property("h", Value::number(total));
    const std::vector<W> items = option_items(menu);
    LiveWidget* page = menu.page();
    if (!items.empty() && page) {
        // The band the list keeps inside: the page's height less a twentieth at each edge.
        const Rect pr = page->screen_rect();
        const float margin = pr.h / 20.0f;
        om_set(menu, "om_min", pr.y + margin), om_set(menu, "om_max", (pr.y + pr.h) - margin);
        list->set_property("y", Value::number(-chosen_y));
        float y = list->screen_rect().y;
        if (pr.h - (margin + margin) < total) {
            // Taller than the band: it stays, and scrolls with the chosen row.
            om_set(menu, "om_y", y), om_set(menu, "om_target", y), om_set(menu, "om_moving", 1);
        } else if (y < om_num(menu, "om_min")) y = om_num(menu, "om_min");
        else if (om_num(menu, "om_max") < y + total) y -= y + total - om_num(menu, "om_max");
        option_place(menu, y);
    }
    om_set(menu, "om_saved", float(menu.index()));
    popup->set_property("opacity", Value::number(0));
    if (page && !items.empty()) rt.set_focus(*page, items[size_t(option_clamp(menu, menu.index()))]);
}

void option_close(Runtime& rt, LiveWidget& menu, bool cancel) {
    om_play(rt, "cursor");
    if (cancel) menu.set_property("index", Value::integer(option_clamp(menu, int(om_num(menu, "om_saved")))));
    menu.set_property("is_popuped", Value::integer(0));
    om_set(menu, "om_moving", 0);
    arrange_option_menu(menu);
    if (LiveWidget* page = menu.page()) rt.set_focus(*page, menu.self());
    if (!cancel) option_script(rt, menu, "onValueChanged", {rt.context(), Value::integer(menu.index())});
    option_script(rt, menu, "onPopupClosed", {rt.context()});
}

// Up or down in the open list (0x2C98F0): the index steps to the next sensitive row that way,
// stays when there is none; the chosen row out of the band sets where the list scrolls to.
// True when the index moved.
bool option_move(LiveWidget& menu, int step) {
    const int before = menu.index();
    bool moved = option_step(menu, step);
    if (moved) {
        const int at = option_sensitive(menu, menu.index(), step > 0);
        if (at >= 0) menu.set_property("index", Value::integer(at));
        else menu.set_property("index", Value::integer(before)), moved = false;
    }
    const std::vector<W> items = option_items(menu);
    const W list = option_list(menu);
    if (items.empty() || !list) return moved;
    const Rect row = items[size_t(option_clamp(menu, menu.index()))]->screen_rect();
    const float list_y = list->screen_rect().y;
    if (row.y < om_num(menu, "om_min")) om_set(menu, "om_y", list_y), om_set(menu, "om_target", list_y + (om_num(menu, "om_min") - row.y));
    else if (om_num(menu, "om_max") < row.y + row.h)
        om_set(menu, "om_y", list_y), om_set(menu, "om_target", list_y - ((row.y + row.h) - om_num(menu, "om_max")));
    return moved;
}

// Each picture (0x2C9C90): the open popup fades in by 0.1, and a list taller than the band
// eases 0.4 of the way to where it scrolls to.
void step_option_menu(LiveWidget& menu) {
    if (!popup_open(menu)) return;
    const W popup = menu.child("popup");
    if (!popup) return;
    if (const float a = popup->opacity(); a < 1.0f) popup->set_property("opacity", Value::number(std::min(a + 0.1f, 1.0f)));
    if (om_num(menu, "om_moving") == 0) return;
    const float y = om_num(menu, "om_y") + (om_num(menu, "om_target") - om_num(menu, "om_y")) * 0.4f;
    om_set(menu, "om_y", y);
    option_place(menu, y);
}

// ---- the bars (docs/formats/menu.md "SliderBar, ScaleBar")
//
// A SliderBar (0x2DD820) holds a whole `index` in [min, max] (a counter whose end is max + 1,
// wrapping with the layout's `repeat`), a ScaleBar (0x236860) a number `value` in [min, max].
// Both draw their `pinch` between display_min and display_max, sliding after the value; with
// a popup (SliderBar: the layout's `popup`; ScaleBar: a child named `popup`) accept opens the
// popup and only then do the keys along the bar change the value.

namespace {

float bar_num(const LiveWidget& w, const char* name, float fallback) { return w.data.has(name) ? w.data.number(name) : fallback; }
void bar_set(LiveWidget& w, const char* name, float v) { w.set_property(name, Value::number(v)); }
void bar_seti(LiveWidget& w, const char* name, int v) { w.set_property(name, Value::integer(v)); }

// The defaults and the layout's properties, once.
void bar_init(LiveWidget& w) {
    if (w.data.number("bar_ready")) return;
    bar_seti(w, "bar_ready", 1);
    if (w.is("SliderBar")) {
        int lo = 0, hi = 1;
        // `range` is written as two values or as a list of two digits (`range [2] {digit {1} digit {50}}`).
        if (const Property* r = w.data.find("range"); r && r->values.size() >= 2) lo = std::atoi(r->values[0].c_str()), hi = std::atoi(r->values[1].c_str());
        else if (r && r->items.size() >= 2 && !r->items[0].values.empty() && !r->items[1].values.empty())
            lo = std::atoi(r->items[0].values[0].c_str()), hi = std::atoi(r->items[1].values[0].c_str());
        if (hi < lo) hi = lo;
        bar_seti(w, "min", lo), bar_seti(w, "max", hi), bar_seti(w, "display_min", lo), bar_seti(w, "display_max", hi);
        if (!w.data.has("step")) bar_seti(w, "step", 1);
        const int index = std::clamp(int(w.data.number("index")), lo, hi);
        bar_seti(w, "index", index), bar_set(w, "bar_shown", float(index));
    } else {
        // `scale_direction` 0 is horizontal; `horizontal` as given; the default horizontal.
        if (w.data.has("scale_direction")) bar_seti(w, "horizontal", w.data.number("scale_direction") == 0);
        else if (!w.data.has("horizontal")) bar_seti(w, "horizontal", 1);
        const float lo = bar_num(w, "min", 0), hi = bar_num(w, "max", 100);
        bar_set(w, "min", lo), bar_set(w, "max", hi), bar_set(w, "display_min", 0), bar_set(w, "display_max", 100);
        if (!w.data.has("step")) bar_set(w, "step", 1);
        const float v = std::clamp(bar_num(w, "value", 0), lo, std::max(lo, hi));
        bar_set(w, "value", bar_num(w, "value", 0)), bar_set(w, "current_value", v), bar_set(w, "repeat_start", v);
    }
    if (const W popup = w.child("popup")) popup->set_property("visible", Value::integer(0));
}

bool bar_popup_mode(const LiveWidget& w) { return w.is("SliderBar") ? w.data.number("popup") != 0 : w.child("popup") != nullptr; }

// SliderBar: move the counter by d (0x57CF10): wrap with `repeat`, else clamp. True when it changed.
bool slider_move(LiveWidget& w, int d) {
    const int lo = int(w.data.number("min")), hi = int(w.data.number("max")), before = int(w.data.number("index"));
    if (hi < lo) return false;
    int v = before + d;
    if (w.data.number("repeat") != 0) {
        const int n = hi - lo + 1;
        v = lo + ((v - lo) % n + n) % n;
    } else v = std::clamp(v, lo, hi);
    bar_seti(w, "index", v);
    return v != before;
}

void cursor_shown(Runtime& rt, bool on) {
    if (auto render = rt.context().as<Object>()) render->set("cursor_visible", Value::integer(on));
}

void play(Runtime& rt, const char* name) {
    const Value arg = Value::string(name);
    rt.sound("sound", "play", Args(&arg, 1));
}

// SliderBar: onValueChanged(context, index) at once, with `has_script` (0x2DF3A0).
void slider_notify(Runtime& rt, LiveWidget& w, bool sound) {
    if (sound) play(rt, "param");
    if (w.data.number("has_script") != 0) rt.call_now(w, "onValueChanged", {rt.context(), Value::integer(w.index())});
}

// ScaleBar: onValueChanged(context, bar) and onStepChanged(context, bar) queued (0x238978,
// 0x238A10).
void scale_value_notify(Runtime& rt, LiveWidget& w) {
    if (w.data.number("cancel") == 0) play(rt, "param");
    rt.queue_call(w, "onValueChanged", {rt.context(), widget_value(w.self())});
}
void scale_step_notify(Runtime& rt, LiveWidget& w) { rt.queue_call(w, "onStepChanged", {rt.context(), widget_value(w.self())}); }

void bar_open(Runtime& rt, LiveWidget& w) {
    play(rt, "cursor");
    bar_seti(w, "is_popuped", 1), bar_seti(w, "cancel", 0);
    cursor_shown(rt, false);
    bar_set(w, "bar_saved", w.is("SliderBar") ? float(w.index()) : bar_num(w, "value", 0));
    if (const W popup = w.child("popup")) popup->set_property("visible", Value::integer(1));
}

void bar_close(Runtime& rt, LiveWidget& w, bool cancel) {
    play(rt, "cursor");
    if (w.is("SliderBar")) {
        if (cancel) {
            bar_seti(w, "index", int(w.data.number("bar_saved")));
            slider_notify(rt, w, false);
        }
    } else {
        bar_seti(w, "cancel", cancel);
        if (cancel) {
            const float v = std::clamp(bar_num(w, "bar_saved", 0), bar_num(w, "min", 0), bar_num(w, "max", 100));
            bar_set(w, "value", v), bar_set(w, "current_value", v), bar_set(w, "repeat_start", v);
            scale_value_notify(rt, w), scale_step_notify(rt, w);
        }
    }
    bar_seti(w, "is_popuped", 0);
    cursor_shown(rt, true);
    if (const W popup = w.child("popup")) popup->set_property("visible", Value::integer(0));
}

// Where the pinch is: the popup while it is open, else the bar; along its width (or height).
void place_pinch(LiveWidget& w, float shown, float dmin, float dmax, float steps_over) {
    const W popup = w.child("popup");
    const W pinch = popup ? popup->child("pinch") : w.child("pinch");
    if (!pinch) return;
    const LiveWidget& box = popup && popup_open(w) ? *popup : w;
    const bool along_x = w.is("SliderBar") ? w.data.number("direction") == 0 : w.data.number("horizontal") != 0;
    const std::optional<Rect> b = box.data.rect("geometry"), p = pinch->data.rect("geometry");
    if (!b || !p) return;
    const float room = (along_x ? b->w - p->w : b->h - p->h);
    if (steps_over <= 0) return;
    const float at = room / steps_over * (shown - dmin);
    pinch->set_property(along_x ? "x" : "y", Value::number(at));
    (void)dmax;
}

} // namespace

// Each picture: the shown value slides after the value (SliderBar 0.4, ScaleBar 0.2) and the
// pinch is placed by it.
void arrange_slider(LiveWidget& w) {
    bar_init(w);
    if (w.is("SliderBar")) {
        float shown = bar_num(w, "bar_shown", float(w.index()));
        shown += (float(w.index()) - shown) * 0.4f;
        bar_set(w, "bar_shown", shown);
        const float dmin = bar_num(w, "display_min", 0), dmax = bar_num(w, "display_max", 1);
        place_pinch(w, shown, dmin, dmax, dmax - dmin);
    } else {
        float shown = bar_num(w, "current_value", 0);
        shown += (bar_num(w, "value", 0) - shown) * 0.2f;
        bar_set(w, "current_value", shown);
        const float dmin = bar_num(w, "display_min", 0), dmax = bar_num(w, "display_max", 100);
        place_pinch(w, shown, dmin, dmax, dmax - dmin);
    }
}

void scale_focus_entered(Runtime& rt, LiveWidget& w) {
    if (!w.is("ScaleBar")) return;
    bar_init(w);
    const float v = std::clamp(bar_num(w, "value", 0), bar_num(w, "min", 0), bar_num(w, "max", 100));
    bar_set(w, "current_value", v), bar_set(w, "repeat_start", v);
    scale_step_notify(rt, w);
}

namespace {

// A bar's keys (SliderBar 0x2DF848, ScaleBar 0x238D10): true when the bar dealt with the key.
bool bar_key(Runtime& rt, LiveWidget& w, Key k) {
    bar_init(w);
    const bool popup = bar_popup_mode(w), open = popup_open(w);
    const bool along_x = w.is("SliderBar") ? w.data.number("direction") == 0 : w.data.number("horizontal") != 0;
    const int dir = along_x ? (k == Key::Left ? -1 : k == Key::Right ? 1 : 0) : (k == Key::Up ? -1 : k == Key::Down ? 1 : 0);
    if (popup) {
        if (k == Key::Accept) {
            if (!open) bar_open(rt, w);
            else bar_close(rt, w, false);
            return true;
        }
        if (k == Key::Cancel && open) {
            bar_close(rt, w, true);
            return true;
        }
    }
    if (w.is("SliderBar")) {
        if (popup && !open) return false; // the value does not change; the key goes on
        if (dir && slider_move(w, dir * std::max(int(w.data.number("step")), 1))) {
            slider_notify(rt, w, true);
            return true;
        }
        return popup && open;
    }
    // ScaleBar (0x238D10): a press of a key along it starts a hold (repeat_start, 0x238FF0) and tells the step
    // (0x238A10); then the widget's own handling (its script's onKeyPress) has the key: the runtime calls
    // scale_bar_key_after when that did not take it.
    if (dir && !rt.key_repeated()) {
        bar_set(w, "repeat_start", std::clamp(bar_num(w, "value", 0), bar_num(w, "min", 0), bar_num(w, "max", 100)));
        scale_step_notify(rt, w);
    }
    return false;
}

} // namespace

} // namespace

// The ScaleBar's key after its script (0x238D10 from 0x238E14): closed in popup mode, nothing; else the value moves a
// step along the key (0x239098 / 0x239048) and the value and the step are told (0x238978, 0x238A10).
bool scale_bar_key_after(Runtime& rt, LiveWidget& w, Key k) {
    const bool popup = bar_popup_mode(w), open = popup_open(w);
    const bool along_x = w.data.number("horizontal") != 0;
    const int dir = along_x ? (k == Key::Left ? -1 : k == Key::Right ? 1 : 0) : (k == Key::Up ? -1 : k == Key::Down ? 1 : 0);
    if (popup && !open) return false;
    if (dir) {
        const float before = bar_num(w, "value", 0);
        const float v = std::clamp(before + float(dir) * bar_num(w, "step", 1), bar_num(w, "min", 0), bar_num(w, "max", 100));
        bar_set(w, "value", v);
        if (v != before) {
            scale_value_notify(rt, w), scale_step_notify(rt, w);
            return true;
        }
    }
    return popup && open;
}

namespace {

// ---- the list box (mListBox, 0x2AEA98 on; docs/formats/menu.md "ListBox") --------------------
//
// Its rows are items (setItemCount): a place and size each, eased toward the row's size (the
// focused one `magnify_ratio` larger), the list scrolled so the focused row sits at
// `alignment` of the box (0x2B2400), the rows in range made visible and told to the script
// (visible_callback), a countdown per row for update_callback, focus_callback(new, old) when
// the focus changed. A row without a widget of its own is drawn as the template.

using ListItem = LiveWidget::ListItem;

float lb_num(const LiveWidget& w, const char* name, float fallback) { return w.data.has(name) ? w.data.number(name) : fallback; }
bool lb_vertical(const LiveWidget& w) { return w.data.number("direction") != 0; }
int list_count(const LiveWidget& w) { return int(w.list_items.size()); }
int list_focus_index(const LiveWidget& w) { return w.data.has("focus_index") ? int(w.data.number("focus_index")) : -1; }
// The row's size along the list (0x2B1FE0) and across it.
float lb_item_along(const LiveWidget& w) { return lb_vertical(w) ? w.item_h : w.item_w; }
// The box's own length along and across (0x2B1F90, 0x2B1FB8).
Rect lb_box(const LiveWidget& w) { return w.data.rect("geometry").value_or(Rect{0, 0, 128, 128}); }
float lb_extent(const LiveWidget& w) { return lb_vertical(w) ? lb_box(w).h : lb_box(w).w; }
float lb_cross_extent(const LiveWidget& w) { return lb_vertical(w) ? lb_box(w).w : lb_box(w).h; }
float lb_chosen(const LiveWidget& w) { return lb_item_along(w) * lb_num(w, "magnify_ratio", 1.0f); } // 0x2B2340
float lb_pitch(const LiveWidget& w) { return lb_item_along(w) + w.data.number("spacing"); }
// Every row one pitch apart, the focused one magnified (0x2B2390).
float lb_total(const LiveWidget& w) { return lb_pitch(w) * float(list_count(w) - 1) + lb_chosen(w); }

// Where the list's start goes (0x2B2400): rows longer than the box put the focused row at
// `alignment` of it, held so neither end leaves a gap; rows that fit start at 0.
float lb_target(const LiveWidget& w) {
    const int focus = list_focus_index(w);
    if (focus < 0) return 0.0f;
    const float extent = lb_extent(w), total = lb_total(w), align = lb_num(w, "alignment", 0.5f);
    if (!(extent <= total)) return 0.0f;
    float at = extent * align - (lb_pitch(w) * float(focus) + lb_chosen(w) * align);
    if (0.0f < at) at = 0.0f;
    if (at + total < extent) at = extent - total;
    return at;
}

// The focus set (0x2B3AF8): the old row loses its active flag, the index is kept in the rows,
// the new row is active with `focus_active`.
void lb_set_focus(LiveWidget& w, int i) {
    const int n = list_count(w), before = list_focus_index(w);
    if (before >= 0 && before < n) w.list_items[size_t(before)].active = false;
    if (i < 0) i = n > 0 ? 0 : i;
    else if (i >= n) i = n - 1;
    w.set_property("focus_index", Value::integer(i));
    if (i >= 0 && i < n) w.list_items[size_t(i)].active = lb_num(w, "focus_active", 1) != 0;
}

// `step` rows on (0x2B3BC0) or back (0x2B3C88): insensitive rows are passed without counting,
// the ends wrap with `repeat`, else the move does not happen; true when the focus moved.
bool lb_step(LiveWidget& w, int step, bool forward) {
    const int n = list_count(w), focus = list_focus_index(w);
    if (focus < 0 || step <= 0) return false;
    int at = focus;
    while (step > 0) {
        at += forward ? 1 : -1;
        if (at >= n || at < 0) at = w.data.number("repeat") != 0 ? (at < 0 ? n - 1 : 0) : -1;
        if (at < 0) break;
        if (!w.list_items[size_t(at)].insensitive) --step;
        if (at == focus) {
            at = -1;
            break;
        }
    }
    if (at < 0) return false;
    lb_set_focus(w, at);
    return true;
}

// The row's widget, made from the template when asked with `make` (0x2B4DA0).
W lb_widget(Runtime& rt, LiveWidget& w, int i, bool make) {
    if (i < 0 || i >= list_count(w)) return nullptr;
    ListItem& item = w.list_items[size_t(i)];
    if (!item.widget && make && w.list_template) {
        item.widget = w.list_template->copy();
        item.widget->parent = std::static_pointer_cast<script::Module>(w.shared_from_this());
        item.widget->runtime = &rt;
    }
    return item.widget;
}

// What draws the row: its widget, else a copy of the template kept for it.
W lb_drawn(Runtime& rt, LiveWidget& w, ListItem& item) {
    if (item.widget) return item.widget;
    if (!item.drawn && w.list_template) {
        item.drawn = w.list_template->copy();
        item.drawn->parent = std::static_pointer_cast<script::Module>(w.shared_from_this());
        item.drawn->runtime = &rt;
    }
    return item.drawn;
}

Value list_callback(const LiveWidget& list, const char* name) {
    const auto it = list.extras.find(name);
    const auto cell = it == list.extras.end() ? nullptr : std::dynamic_pointer_cast<script::Variable>(it->second);
    return cell ? cell->value : Value{};
}

float lb_ease(float now, float to, float ratio) {
    if (now == to) return now;
    const float d = (to - now) * ratio;
    if (!(-0.1f <= d)) return now + d;
    return d <= 0.1f ? to : now + d;
}

// One picture (mListBox slot 70, 0x2B29C0) and its drawing (slot 93, 0x2B3848 / 0x2B3670).
void arrange_list(Runtime& rt, LiveWidget& list, bool) {
    if (list.list_redraw) {
        for (int i = list.list_first; i <= list.list_last && i < list_count(list); ++i)
            if (i >= 0) list.list_items[size_t(i)].visible = false;
        list.list_first = 0, list.list_last = -1, list.list_redraw = false;
    }
    list.children.clear();
    const int focus = list_focus_index(list), n = list_count(list);
    if (focus < 0 || n <= 0) return;
    const float pitch = lb_pitch(list), extent = lb_extent(list), total = lb_total(list), target = lb_target(list);
    // The scroll eases toward where the focus puts it (a jump of more than the box comes in
    // from a box away).
    {
        const float at = list.scroll, d = at - target;
        if (target == at) {
        } else if (extent < d) list.scroll = target + extent;
        else if (d < -extent) list.scroll = target - extent;
        else list.scroll = lb_ease(at, target, lb_num(list, "interpolate_ratio", 0.2f));
    }
    list.scroll_target = target;
    list.list_was_first = list.list_first, list.list_was_last = list.list_last;
    // The rows in range: all when they fit, else those within the box on each side of the
    // focused row (and of where the scroll still is).
    int first = 0, last = n - 1;
    if (!(total < extent)) {
        const float align = lb_num(list, "alignment", 0.5f), chosen = lb_chosen(list);
        float before = (extent - chosen) * align, after = (extent - chosen) * (1.0f - align);
        if (target < list.scroll) before += list.scroll - target;
        if (list.scroll < target) after += target - list.scroll;
        auto rows = [pitch](float px) {
            const float q = px / pitch;
            const int whole = int(q);
            return whole + (q - float(whole) > 0.0f ? 1 : 0);
        };
        first = focus - rows(before), last = focus + rows(after);
        int shift = first < 0 ? -first : 0;
        if (!(last < n)) shift = n - last - 1;
        first += shift, last += shift;
        if (first < 0) first = 0;
        if (!(last < n)) last = n - 1;
    }
    list.list_first = first, list.list_last = last;
    // Places: the rows along from the scroll (moved by `h_justify` of the room when they fit),
    // across at `v_justify` of the room; each row's size eases toward its own.
    const bool vertical = lb_vertical(list);
    const float magnify = lb_num(list, "magnify_ratio", 1.0f), ratio = lb_num(list, "interpolate_ratio", 0.2f);
    const float hj = lb_num(list, "h_justify", 0.0f), vj = lb_num(list, "v_justify", 0.0f), spacing = list.data.number("spacing");
    float along = list.scroll + float(first) * pitch;
    if (total < extent) along += (extent - total) * hj;
    for (int i = first; i <= last; ++i) {
        ListItem& item = list.list_items[size_t(i)];
        float tw = list.item_w, th = list.item_h;
        if (i == focus) tw *= magnify, th *= magnify;
        float cw = item.w, ch = item.h;
        if (cw <= 0.0f) cw = list.item_w;
        if (ch <= 0.0f) ch = list.item_h;
        item.w = lb_ease(cw, tw, ratio), item.h = lb_ease(ch, th, ratio);
        const float cross = (lb_cross_extent(list) - (vertical ? item.w : item.h)) * vj;
        item.x = vertical ? cross : along, item.y = vertical ? along : cross;
        along += (vertical ? item.h : item.w) + spacing;
        item.active = false;
    }
    list.list_items[size_t(focus)].active = lb_num(list, "focus_active", 1) != 0;
    // Rows that came into range or left it are told to visible_callback(context, [indexes]).
    std::vector<Value> changed, updates;
    for (int i = std::max(list.list_was_first, 0); i <= list.list_was_last && i < n; ++i)
        if ((i < first || i > last) && list.list_items[size_t(i)].visible) {
            list.list_items[size_t(i)].visible = false, list.list_items[size_t(i)].update = 0;
            changed.push_back(Value::integer(i));
        }
    for (int i = first; i <= last; ++i)
        if (!list.list_items[size_t(i)].visible) {
            list.list_items[size_t(i)].visible = true, list.list_items[size_t(i)].update = 0;
            changed.push_back(Value::integer(i));
        }
    // A row's update countdown (setItemUpdate): reaching 0 tells update_callback.
    for (int i = first; i <= last; ++i) {
        int& u = list.list_items[size_t(i)].update;
        if (u > 0 && --u == 0) updates.push_back(Value::integer(i));
    }
    auto queue = [&](const char* name, std::vector<Value> args) {
        const Value fn = list_callback(list, name);
        if (fn.p && fn.p->callable()) rt.queue_function(fn, std::move(args), list.path() + "::" + name);
    };
    if (!changed.empty()) queue("visible_callback", {rt.context(), Value::array(std::move(changed))});
    if (focus != list.list_told) {
        const int was = list.list_told;
        list.list_told = focus;
        queue("focus_callback", {rt.context(), Value::integer(focus), Value::integer(was)});
    }
    if (!updates.empty()) queue("update_callback", {rt.context(), Value::array(std::move(updates))});
    // The rows drawn: each in range at its item's place, shown as its item says.
    const bool has_focus = list.page() && list.page()->focus.lock().get() == &list;
    for (int i = first; i <= last; ++i) {
        ListItem& item = list.list_items[size_t(i)];
        const W row = lb_drawn(rt, list, item);
        if (!row) continue;
        row->set_property("x", Value::number(item.x)), row->set_property("y", Value::number(item.y));
        row->set_property("w", Value::number(item.w)), row->set_property("h", Value::number(item.h));
        row->set_property("visible", Value::integer(item.visible));
        row->set_state(kActive, item.active);
        row->set_state(kInsensitive, item.insensitive);
        // The cursor points at the chosen row while the list has the focus (prelight); the focused widget is the list
        // itself, so no row has the focus bit (0x2B3518 -> 0x263A68).
        row->state = (row->state & ~(kFocus | kPrelight)) | (i == focus && has_focus ? kPrelight : 0);
        list.children.push_back(row);
    }
}

} // namespace

// The row the cursor points at while the list has the focus (0x2B3670 -> 0x230B68).
LiveWidget* list_cursor_row(LiveWidget& list) {
    const int focus = list_focus_index(list);
    if (focus < 0 || focus >= list_count(list)) return nullptr;
    ListItem& item = list.list_items[size_t(focus)];
    return item.widget ? item.widget.get() : item.drawn.get();
}

// The pointer's moves (menu/pointer.cpp), as the keys make them: a list box's focus on row `i` (0x2B3AF8), an open option
// menu's index on row `i` with the focus on it (0x2C98F0, the list scrolled when the row is out of the band).
void list_set_focus(LiveWidget& list, int i) { lb_set_focus(list, i); }
bool option_point(Runtime& rt, LiveWidget& menu, int i) {
    const std::vector<W> items = option_items(menu);
    if (!popup_open(menu) || i < 0 || i >= int(items.size()) || items[size_t(i)]->insensitive()) return false;
    if (i == menu.index()) return false;
    menu.set_property("index", Value::integer(i));
    option_move(menu, 0);
    om_play(rt, "cursor");
    if (LiveWidget* page = menu.page()) rt.set_focus(*page, items[size_t(i)]);
    return true;
}

namespace {

// ---- the scroll window: its content moved so the focused widget shows -----------------------

// The widget whose children scroll: clip::space in the layouts, else the window itself.
LiveWidget* scroll_content(LiveWidget& window) {
    if (const W clip = window.child("clip"))
        if (const W space = clip->child("space")) return space.get();
    return nullptr;
}

// Where a scroll window (MScrollWindow) wants its `space` so `inside` shows (the program's
// 0x2D3430): the widget in the middle of the `clip`, the space kept against the clip's edges;
// 0 along a direction the space does not exceed the clip in. False when the widget is not
// inside the space.
bool scroll_window_target(LiveWidget& window, const LiveWidget& inside, float& x, float& y) {
    LiveWidget* space = scroll_content(window);
    const W clip = window.child("clip");
    if (!space || !clip) return false;
    float wx = 0, wy = 0;
    const LiveWidget* p = &inside;
    for (; p && p != space; p = p->parent_widget())
        if (const std::optional<Rect> g = p->data.rect("geometry")) wx += g->x, wy += g->y;
    if (p != space || &inside == space) return false;
    const std::optional<Rect> own = inside.data.rect("geometry"), view = clip->data.rect("geometry"), whole = space->data.rect("geometry");
    if (!view || !whole) return false;
    const float ww = own ? own->w : 0.0f, wh = own ? own->h : 0.0f;
    auto along = [](float view_size, float whole_size, float at, float size) {
        if (!(view_size < whole_size)) return 0.0f;
        return std::clamp((view_size - size) * 0.5f - at, view_size - whole_size, 0.0f);
    };
    x = along(view->w, whole->w, wx, ww), y = along(view->h, whole->h, wy, wh);
    return true;
}

} // namespace

void widget_focus_entered(Runtime& rt, LiveWidget& w) { scale_focus_entered(rt, w); }

// A select box (MSelectBox, MSelectBar): its rows one after the other along its direction, all
// the size of the first, the chosen one `magnify_ratio` larger and `active`. The row chosen is
// kept in the middle of the box while the rows are longer than the box (0.3 of a row may show
// past either end when `alignment` is 0); fewer rows are centered (alignment 0), start a
// `spacing` in (1) or at the start. The rows slide: each frame 0.2 of what is left, and a jump
// of more than a row (the list wrapping around) starts a row away on the far side.
// Read from the program: 0x2DA2B0 (where the chosen row puts the list), 0x2DA5D0 (sizes and
// the active row), 0x2DA6F0 (the slide), 0x2DAA10 (the rows' places).
void select_box_layout(LiveWidget& box, bool warp) {
    const std::vector<W> rows = rows_of(box);
    const std::optional<Rect> g = box.data.rect("geometry");
    if (rows.empty() || !g) return;
    const bool vertical = box.data.number("direction") != 0;
    const float magnify = box.data.has("magnify_ratio") ? box.data.number("magnify_ratio") : 1.0f;
    const float spacing = box.data.number("spacing"), border = box.data.number("border_width");
    const int alignment = int(box.data.number("alignment"));
    const int count = int(rows.size()), index = std::clamp(box.index(), 0, count - 1);
    const float item = vertical ? box.item_h : box.item_w, extent = vertical ? g->h : g->w;
    const float pitch = item + spacing, chosen = item * magnify;
    const float total = pitch * float(count - 1) + chosen;
    float target = 0;
    if (total < extent) target = alignment == 0 ? (extent - total) * 0.5f : alignment == 1 ? spacing : 0.0f;
    else {
        const float margin = item * (alignment == 0 ? 0.3f : 0.0f);
        target = extent * 0.5f - (pitch * float(index) + chosen * 0.5f);
        target = std::min(target, margin);
        target = std::max(target, extent - margin - total);
    }
    if (warp) box.scroll = target;
    else {
        const float d = (target - box.scroll) * 0.2f;
        if (d >= -0.1f && d <= 0.1f) box.scroll = target;
        else if (d < -box.item_h) box.scroll = target - box.item_h;
        else if (d > box.item_h) box.scroll = target + box.item_h;
        else box.scroll += d;
    }
    box.scroll_target = target;
    float at = box.scroll;
    for (int i = 0; i < count; ++i) {
        LiveWidget& row = *rows[size_t(i)];
        const float scale = i == index ? magnify : 1.0f;
        row.set_property("w", Value::number(box.item_w * scale));
        row.set_property("h", Value::number(box.item_h * scale));
        row.set_state(kActive, i == index);
        if (vertical) {
            row.set_property("x", Value::number(border));
            row.set_property("y", Value::number(at));
            at += spacing + box.item_h * scale;
        } else {
            row.set_property("x", Value::number(at));
            row.set_property("y", Value::number(g->h - box.item_h * scale - border));
            at += spacing + box.item_w * scale;
        }
    }
}

// Whether a select box's rows go on past the end of the box / before its start: what lights
// the arrows of the scroll box around it (the program's 0x2DBB80, 0x2DBC58).
bool select_box_more(const LiveWidget& box, bool forward) {
    const std::vector<W> rows = rows_of(box);
    const std::optional<Rect> g = box.data.rect("geometry");
    if (rows.empty() || !g) return false;
    const bool vertical = box.data.number("direction") != 0;
    const float magnify = box.data.has("magnify_ratio") ? box.data.number("magnify_ratio") : 1.0f;
    const float item = vertical ? box.item_h : box.item_w, extent = vertical ? g->h : g->w;
    const float total = (item + box.data.number("spacing")) * float(rows.size() - 1) + item * magnify;
    return forward ? extent < box.scroll + total : box.scroll < 0;
}

void collect_focusable(const W& w, std::vector<W>& out) {
    if (!w->visible() || w->insensitive()) return;
    if (is_select_box(*w)) {
        // The box's chosen row is what has the focus.
        const std::vector<W> rows = rows_of(*w);
        if (!rows.empty()) out.push_back(rows[size_t(std::clamp(w->index(), 0, int(rows.size()) - 1))]);
        return;
    }
    if (w->is("ListBox")) {
        // The list itself has the focus; its rows are its look.
        if (w->can_focus() && list_count(*w) > 0) out.push_back(w);
        return;
    }
    if (w->is("OptionMenu") || w->is("SliderBar") || w->is("ScaleBar")) {
        if (w->is("OptionMenu") && popup_open(*w)) {
            for (const W& item : option_items(*w))
                if (!item->insensitive()) out.push_back(item);
        } else if (w->can_focus()) out.push_back(w);
        return;
    }
    if (w->can_focus()) out.push_back(w);
    for (const auto& c : w->children) collect_focusable(c, out);
}

bool condition_holds(const LiveWidget& w) {
    if (!w.visible()) return false;
    const LiveWidget* up = w.parent_widget();
    return state_shows(is_face(w) && up ? up->state : w.state, int(w.data.number("visible_condition")), int(w.data.number("visible_compare")));
}

namespace {

// A navigate region of the layout: corners x0 y0 x1 y1 from the widget's top left (0x262668,
// 0x2626A0; kept as fractions of the widget's size, 0x268478); none: the widget's rectangle.
Rect navigate_region(const LiveWidget& w, const Rect& r, const char* name) {
    const Property* p = w.data.find(name);
    if ((!p || p->values.size() < 4) && std::string(name) == "navigate_target") p = w.data.find("navigate_box");
    if (!p || p->values.size() < 4) return r;
    const float v[4] = {std::strtof(p->values[0].c_str(), nullptr), std::strtof(p->values[1].c_str(), nullptr), std::strtof(p->values[2].c_str(), nullptr),
                        std::strtof(p->values[3].c_str(), nullptr)};
    // As fractions of the size the layout gives, then on the size now.
    const std::optional<Rect> g = w.data.rect("geometry");
    const float lw = g && g->w > 0 ? g->w : r.w, lh = g && g->h > 0 ? g->h : r.h;
    const float fx0 = lw > 0 ? v[0] / lw : 0, fx1 = lw > 0 ? v[2] / lw : 1, fy0 = lh > 0 ? v[1] / lh : 0, fy1 = lh > 0 ? v[3] / lh : 1;
    return {r.x + fx0 * r.w, r.y + fy0 * r.h, (fx1 - fx0) * r.w, (fy1 - fy0) * r.h};
}

Rect cut(const Rect& r, const Rect& c) {
    const float x0 = std::max(r.x, c.x), y0 = std::max(r.y, c.y), x1 = std::min(r.x + r.w, c.x + c.w), y1 = std::min(r.y + r.h, c.y + c.h);
    if (x1 <= x0 || y1 <= y0) return {std::clamp(r.x, c.x, c.x + c.w), std::clamp(r.y, c.y, c.y + c.h), 0, 0};
    return {x0, y0, x1 - x0, y1 - y0};
}

// The widget whose `space` is the scroll window's: the program marks a ScrollWindow's
// clip::space (0x2D2F88).
bool is_navigate_space(const LiveWidget& w) {
    const LiveWidget* clip = w.parent_widget();
    const LiveWidget* window = clip ? clip->parent_widget() : nullptr;
    return w.name == "space" && clip && clip->name == "clip" && window && window->is("ScrollWindow");
}

struct Collector {
    bool clipped;
    const LiveWidget* space; // the navigate space the focus is in
    std::vector<Rect> clips;
    int group = 0;
    std::vector<FocusCandidate> out;

    void add(const W& w) {
        FocusCandidate c;
        c.widget = w, c.group = group, c.rect = w->screen_rect();
        c.source = navigate_region(*w, c.rect, "navigate_source");
        c.target = navigate_region(*w, c.rect, "navigate_target");
        if (clipped && !clips.empty() && clips.back().w > 0 && clips.back().h > 0) {
            const Rect& top = clips.back();
            c.rect = cut(c.rect, top), c.source = cut(c.source, top), c.target = cut(c.target, top);
            if (c.rect.w <= 0 || c.rect.h <= 0) return;
        }
        out.push_back(std::move(c));
    }

    void visit(const W& w) {
        if (!condition_holds(*w) || w->insensitive()) return;
        // The classes whose focus is their own (a select box's chosen row, a list box, an
        // option menu's open list).
        if (is_select_box(*w)) {
            const std::vector<W> rows = rows_of(*w);
            if (!rows.empty()) add(rows[size_t(std::clamp(w->index(), 0, int(rows.size()) - 1))]);
            return;
        }
        if (w->is("ListBox")) { // nothing inside a list box is a candidate (0x5E8398)
            if (w->can_focus() && list_count(*w) > 0) add(w);
            return;
        }
        if (w->is("OptionMenu") && popup_open(*w)) {
            for (const W& item : option_items(*w))
                if (!item->insensitive()) add(item);
            return;
        }
        // A focusable widget is a candidate and hides what is inside it.
        if (w->can_focus()) {
            add(w);
            return;
        }
        if (!w->is_composite() || w->children.empty()) return;
        const bool pushed_space = clipped && w.get() == space;
        if (pushed_space) clips.push_back(w->screen_rect()), ++group;
        const bool pushed_clip = clipped && w->data.number("clip") != 0;
        if (pushed_clip) clips.push_back(clips.empty() ? w->screen_rect() : cut(w->screen_rect(), clips.back()));
        for (const auto& c : w->children) visit(c);
        if (pushed_clip) clips.pop_back();
        if (pushed_space) clips.pop_back(), --group;
    }
};

} // namespace

std::vector<FocusCandidate> focus_candidates(LiveWidget& page, bool clipped) {
    Collector c{clipped, nullptr, {}, 0, {}};
    for (const LiveWidget* w = page.focus.lock().get(); w && w != &page; w = w->parent_widget())
        if (is_navigate_space(*w)) {
            c.space = w;
            break;
        }
    for (const auto& child : page.children) c.visit(child);
    return std::move(c.out);
}

// The first focus of a page that has none (0x234D60 runs the "next" search, 0x26EEF8): the
// candidates in order of their parents' places (y, then x), then their own places, then the
// order they were made in.
W first_focus(LiveWidget& page) {
    std::vector<FocusCandidate> all = focus_candidates(page, false);
    if (all.empty()) return nullptr;
    std::stable_sort(all.begin(), all.end(), [](const FocusCandidate& a, const FocusCandidate& b) {
        const LiveWidget* pa = a.widget->parent_widget();
        const LiveWidget* pb = b.widget->parent_widget();
        if (pa != pb && pa && pb) {
            const Rect ra = pa->screen_rect(), rb = pb->screen_rect();
            if (ra.y != rb.y) return ra.y < rb.y;
            if (ra.x != rb.x) return ra.x < rb.x;
            const Rect wa = a.widget->screen_rect(), wb = b.widget->screen_rect();
            if (wa.y != wb.y) return wa.y < wb.y;
            return wa.x < wb.x;
        }
        return false;
    });
    return all.front().widget;
}

void widget_children_changed(Runtime& rt, LiveWidget& w) {
    if (LiveWidget* page = w.page()) {
        // A focus on a widget that is gone is dropped.
        const W at = page->focus.lock();
        if (at) {
            bool inside = false;
            for (LiveWidget* p = at.get(); p; p = p->parent_widget()) inside = inside || p == page;
            if (!inside) rt.set_focus(*page, nullptr);
        }
    }
}

void widget_set_index(Runtime& rt, LiveWidget& w, int index, bool notify) {
    if (is_select_box(w)) {
        const std::vector<W> rows = rows_of(w);
        const int n = int(rows.size());
        if (n > 0) index = w.data.number("repeat") != 0 ? ((index % n) + n) % n : std::clamp(index, 0, n - 1);
        const int before = w.index();
        w.set_property("index", Value::integer(index));
        if (n == 0) return;
        // A script's own setting puts the rows in place at once; a key's slides them.
        select_box_layout(w, !notify);
        if (LiveWidget* page = w.page()) {
            const W at = page->focus.lock();
            if (at && at->parent_widget() == &w && at != rows[size_t(index)]) rt.set_focus(*page, rows[size_t(index)]);
        }
        if (notify && index != before) {
            // The page's sound for a move, then the script (0x2DAB50).
            if (LiveWidget* page = w.page()) rt.navigate_sound(*page);
            rt.call_now(w, "onValueChanged", {rt.context(), Value::integer(index)});
        }
        return;
    }
    if (w.is("OptionMenu")) {
        // setIndex (0x2C9898): the first sensitive row at or after the index (none: as it was);
        // the script hears nothing (onValueChanged comes from the open list's accept).
        if (const int at = option_sensitive(w, index, true); at >= 0) w.set_property("index", Value::integer(option_clamp(w, at)));
        arrange_option_menu(w);
        (void)rt, (void)notify;
        return;
    }
    if (w.is("SliderBar") || w.is("ScaleBar")) {
        // `index = n` (0x2DDAE8, 0x236D10): the value clamped, no event, the pinch slides there.
        bar_init(w);
        if (w.is("SliderBar")) bar_seti(w, "index", std::clamp(index, int(w.data.number("min")), int(w.data.number("max"))));
        else bar_set(w, "value", std::clamp(float(index), bar_num(w, "min", 0), bar_num(w, "max", 100)));
        (void)rt, (void)notify;
        return;
    }
    w.set_property("index", Value::integer(index));
}

// A list box's key, after its script's onKeyPress (mListBox slot 72, 0x2B3918): back or on
// along its direction by `step_min` rows (`step_max` once a held key has gone 10 rows); moved,
// the page's navigate sound plays and the key is the list's; at an end it goes on.
bool list_key_after(Runtime& rt, LiveWidget& w, Key k) {
    const bool vertical = lb_vertical(w);
    const bool back = vertical ? k == Key::Up : k == Key::Left, on = vertical ? k == Key::Down : k == Key::Right;
    if (!back && !on) return false;
    if (!lb_step(w, std::max(int(lb_num(w, "step_min", 1)), 0), on)) return false;
    if (LiveWidget* page = w.page()) rt.navigate_sound(*page);
    return true;
}

bool widget_key(Runtime& rt, LiveWidget& w, Key k) {
    // A select box moves its index with the keys along it (the program's 0x2DAD18): moved, the
    // key is dealt with; at an end the script hears onValueDisabled and the key goes on.
    if (is_select_box(w)) {
        const bool across = w.data.number("direction") == 0;
        const int step = across ? (k == Key::Left ? -1 : k == Key::Right ? 1 : 0) : (k == Key::Up ? -1 : k == Key::Down ? 1 : 0);
        if (!step) return false;
        const int before = w.index();
        widget_set_index(rt, w, before + step, true);
        if (w.index() != before) return true;
        rt.call_now(w, "onValueDisabled", {rt.context(), Value::integer(before)});
        return false;
    }
    if (w.is("OptionMenu")) {
        // Closed, accept opens the list (the menu's activate, 0x2C9568); open, the menu has
        // every key (0x2C95E8): up and down move in the list, accept keeps, cancel puts back.
        if (!popup_open(w)) {
            if (k != Key::Accept || w.insensitive() || !option_list(w)) return false;
            option_open(rt, w);
            return true;
        }
        if (k == Key::Up || k == Key::Down) {
            if (option_move(w, k == Key::Up ? -1 : 1)) {
                om_play(rt, "cursor");
                const std::vector<W> items = option_items(w);
                if (LiveWidget* page = w.page(); page && !items.empty()) rt.set_focus(*page, items[size_t(option_clamp(w, w.index()))]);
            }
            return true;
        }
        if (k == Key::Accept || k == Key::Cancel) option_close(rt, w, k == Key::Cancel);
        return true;
    }
    if (w.is("SliderBar") || w.is("ScaleBar")) return bar_key(rt, w, k);
    if (w.is("LoggerFace")) return logger_face_key(rt, w, k);
    return false;
}

namespace {

// A color chip's words (0x1588C8, 0x158B78): kept as four numbers; words with no colour present
// (bit 24 of each) leave the chip as it was.
void set_chip_words(LiveWidget& w, const Value& words) {
    const std::vector<Value>* list = words.items();
    if (!list) return;
    bool any = false;
    for (size_t i = 0; i < 4 && i < list->size(); ++i) any |= (*list)[i].kind == Kind::Int && (u32((*list)[i].i) & 0x1000000u);
    if (!any) return;
    for (size_t i = 0; i < 4; ++i)
        w.set_property("chip_" + std::to_string(i), Value::integer(i < list->size() && (*list)[i].kind == Kind::Int ? s32((*list)[i].i) : 0));
}

// How a scrolling list stands in its box, as fractions of its whole length: where the shown part
// starts (mScrollable's slot 104: 0x2DB9E8 a select box's, 0x2B45E0 a list box's) and how long it
// is (slot 105: 0x2DBAD0, 0x2B4668).
bool scroll_fractions(const LiveWidget& list, float& start, float& part) {
    float extent = 0, total = 0;
    if (is_select_box(list)) {
        const std::vector<W> rows = rows_of(list);
        const std::optional<Rect> g = list.data.rect("geometry");
        if (rows.empty() || !g) return false;
        const bool vertical = list.data.number("direction") != 0;
        const float magnify = list.data.has("magnify_ratio") ? list.data.number("magnify_ratio") : 1.0f;
        const float item = vertical ? list.item_h : list.item_w;
        extent = vertical ? g->h : g->w;
        total = (item + list.data.number("spacing")) * float(rows.size() - 1) + item * magnify;
    } else if (list.is("ListBox")) {
        if (list.list_items.empty()) return false;
        extent = lb_extent(list), total = lb_total(list);
    } else {
        return false;
    }
    if (total <= 0) return false;
    const float moved = -list.scroll;
    start = moved < 0 ? 0.0f : std::min(1.0f - extent / total, moved / total);
    part = extent / total;
    return true;
}

// A scroll box's `pinch` (0x2CFBE0): along the list beside it, at the list's own start plus the
// shown part's start of its length, the shown part of it long.
void place_scroll_box_pinch(LiveWidget& box, const LiveWidget& list) {
    const W pinch = box.child("pinch");
    const std::optional<Rect> g = list.data.rect("geometry");
    float start = 0, part = 1;
    if (!pinch || !g || !scroll_fractions(list, start, part)) return;
    start = std::max(start, 0.0f), part = std::min(part, 1.0f);
    const bool vertical = list.data.number("direction") != 0;
    pinch->set_property(vertical ? "y" : "x", Value::number(vertical ? g->y + start * g->h : g->x + start * g->w));
    pinch->set_property(vertical ? "h" : "w", Value::number(part * (vertical ? g->h : g->w)));
}

// A scroll window's bars, `hbar` and `vbar`: their `base`, `pinch` and arrows `dec` / `inc`.
Rect own_rect(const LiveWidget& w) { return w.data.rect("geometry").value_or(Rect{0, 0, 128, 128}); }
void put_rect(LiveWidget& w, const Rect& r) {
    w.set_property("x", Value::number(r.x)), w.set_property("y", Value::number(r.y));
    w.set_property("w", Value::number(r.w)), w.set_property("h", Value::number(r.h));
}
float bar_arrow(const LiveWidget& bar, bool across) {
    const W dec = bar.child("dec");
    return !dec ? 0.0f : across ? own_rect(*dec).w : own_rect(*dec).h;
}
// The bar's parts along it (updateGeometry: 0x2D2068): the arrows at its ends, the base between.
void arrange_scroll_bar(LiveWidget& bar, bool across) {
    const Rect g = own_rect(bar);
    const float length = across ? g.w : g.h, breadth = across ? g.h : g.w, arrow = bar_arrow(bar, across);
    auto at = [across](LiveWidget& w, float along) {
        w.set_property("x", Value::number(across ? along : 0.0f)), w.set_property("y", Value::number(across ? 0.0f : along));
    };
    if (const W dec = bar.child("dec")) at(*dec, 0);
    if (const W inc = bar.child("inc")) at(*inc, length - arrow);
    if (const W base = bar.child("base"))
        put_rect(*base, across ? Rect{arrow, 0, length - 2 * arrow, breadth} : Rect{0, arrow, breadth, length - 2 * arrow});
    if (const W pinch = bar.child("pinch")) at(*pinch, arrow);
}
// Each picture (0x2D3970, 0x2D2288): the pinch as long on its base as the window is on the space.
void place_scroll_bar_pinch(const LiveWidget& window, const LiveWidget& space, LiveWidget& bar, bool across) {
    const W base = bar.child("base"), pinch = bar.child("pinch");
    if (!base || !pinch) return;
    const Rect g = own_rect(bar), b = own_rect(*base), view = own_rect(window), whole = own_rect(space);
    const float total = across ? whole.w : whole.h;
    if (total <= 0) return;
    const float ratio = (across ? b.w : b.h) / total, breadth = across ? g.h : g.w;
    const float at = bar_arrow(bar, across) - (across ? whole.x : whole.y) * ratio, length = (across ? view.w : view.h) * ratio;
    put_rect(*pinch, across ? Rect{at, 0, length, breadth} : Rect{0, at, breadth, length});
}

} // namespace

void step_widgets(Runtime& rt, LiveWidget& w) {
    if (is_select_box(w)) select_box_layout(w, false);
    if (w.is("SliderBar") || w.is("ScaleBar")) arrange_slider(w);
    if (w.is("OptionMenu")) step_option_menu(w);
    if (w.is("LoggerFace")) step_logger_face(rt, w);
    if (w.is("ListBox")) {
        arrange_list(rt, w, false);
        return; // its rows are placed; nothing below them moves by itself
    }
    if (w.is("ScrollBox")) {
        // A scroll box lights its arrows while the box inside it has more rows that way (the
        // program's 0x2CF868: its children named inc_arrow, dec_arrow, and the scrollable one).
        for (const auto& c : w.children) {
            bool more[2] = {false, false};
            if (is_select_box(*c)) more[0] = select_box_more(*c, true), more[1] = select_box_more(*c, false);
            else if (c->is("ListBox")) {
                const int count = list_count(*c), focus = list_focus_index(*c);
                // (mListBox's slots 106 and 107, 0x2B46B0, 0x2B4728: a row further on and the rows past the box's end; a row
                // before and the rows moved.)
                more[0] = focus + 1 < count && c->scroll + lb_total(*c) > lb_extent(*c), more[1] = focus != 0 && c->scroll < 0;
            } else continue;
            if (const W inc = w.child("inc_arrow")) inc->set_state(kActive, more[0]);
            if (const W dec = w.child("dec_arrow")) dec->set_state(kActive, more[1]);
            place_scroll_box_pinch(w, *c);
            break;
        }
    }
    if (w.is("ScrollWindow")) {
        // The space follows the page's focused widget: each picture it moves `interpolate_ratio`
        // (0.3 when the layout gives none, 0x2D2720) of the way to where it wants to be
        // (0x2D36A0). ❓ `floating` (the space follows a pointer) is not written.
        LiveWidget* page = w.page();
        const W focus = page ? page->focus.lock() : nullptr;
        LiveWidget* content = scroll_content(w);
        float tx = 0, ty = 0;
        if (focus && content && w.data.number("pointer_pan") == 0 && scroll_window_target(w, *focus, tx, ty)) { // (ours: held by a drag, pointer.cpp)
            const float ratio = w.data.has("interpolate_ratio") ? w.data.number("interpolate_ratio") : 0.3f;
            const float x = content->get("x").as_float(), y = content->get("y").as_float();
            if (x != tx || y != ty) {
                content->set_property("x", Value::number(std::fabs(tx - x) < 0.01f ? tx : x + (tx - x) * ratio));
                content->set_property("y", Value::number(std::fabs(ty - y) < 0.01f ? ty : y + (ty - y) * ratio));
            }
        }
        if (content) {
            if (const W bar = w.child("hbar")) place_scroll_bar_pinch(w, *content, *bar, true);
            if (const W bar = w.child("vbar")) place_scroll_bar_pinch(w, *content, *bar, false);
        }
    } else if (w.is("ScrollBox")) {
        // The content follows the focused widget: it moves a part of the way each frame.
        if (LiveWidget* content = scroll_content(w)) {
            float y = content->get("y").as_float();
            if (y != w.scroll_target) {
                interpolate(y, w.scroll_target, 0.25f);
                content->set_property("y", Value::number(y));
            }
        }
    }
    for (const auto& c : w.children) step_widgets(rt, *c);
}

// Moves a scroll window's content so a widget inside it shows.
void scroll_into_view(LiveWidget& inside) {
    for (LiveWidget* w = inside.parent_widget(); w; w = w->parent_widget()) {
        if (!w->is("ScrollBox")) continue; // a scroll window follows the focus as it is stepped
        LiveWidget* content = scroll_content(*w);
        const W clip = w->child("clip");
        if (!content || !clip) continue;
        const std::optional<Rect> view = clip->data.rect("geometry");
        const std::optional<Rect> whole = content->data.rect("geometry");
        if (!view || !whole) continue;
        // The widget's place inside the content.
        float top = 0;
        for (const LiveWidget* p = &inside; p && p != content; p = p->parent_widget())
            if (const std::optional<Rect> g = p->data.rect("geometry")) top += g->y;
        const float h = inside.data.rect("geometry") ? inside.data.rect("geometry")->h : 0.0f;
        float target = w->scroll_target;
        if (top + target < 0) target = -top;
        if (top + h + target > view->h) target = view->h - top - h;
        const float lowest = std::min(view->h - whole->h, 0.0f);
        w->scroll_target = std::clamp(target, lowest, 0.0f);
    }
}

// A car face's car model (face.car_model), made when first asked for: where the model stands, how
// it turns, the model sets' motions (menu/faces_car.cpp).
std::shared_ptr<CarModelObject> car_model_of(Runtime* r, const W& w) {
    const auto it = w->extras.find("car_model");
    if (it != w->extras.end())
        if (auto cell = std::dynamic_pointer_cast<script::Variable>(it->second))
            if (auto m = cell->value.as<CarModelObject>()) return m;
    const std::shared_ptr<CarModelObject> model = make_car_model_object([r] { return r->frame(); });
    w->extras["car_model"] = std::make_shared<script::Variable>(object_value(model));
    return model;
}

// A picture face's picture size (mImageFace's vf55 0x2A2360 with a picture: its width and height):
// the page's picture, or with `from_disk` a Tex1 file of the disc (`piece/crs_logo/<course>.img`)
// as drawn. None without a picture.
std::optional<std::pair<float, float>> picture_size(Runtime* r, LiveWidget& w) {
    LiveWidget* page = w.page();
    Project* project = page ? r->project_of(*page) : nullptr;
    const std::string path = w.data.text("image_path");
    if (w.data.number("from_disk") != 0) {
        std::string name = path;
        while (!name.empty() && name[0] == '/') name.erase(0, 1);
        try {
            if (const std::optional<Bytes> data = name.empty() ? std::nullopt : r->reader()(name); data && gt3r::Tex1::is_tex1(*data)) {
                const gt3r::Tex1 set = gt3r::Tex1::parse(*data);
                if (!set.textures.empty()) {
                    gt3r::GsMemory gs;
                    set.upload(*data, gs);
                    const gt3r::Tex1::Image image = set.decode(gs, 0);
                    return std::pair{float(image.width), float(image.height)};
                }
            }
        } catch (const std::exception&) {
        }
        return std::nullopt;
    }
    if (const gt3r::Tex1::Image* image = project && !path.empty() ? project->image(page->name, path) : nullptr)
        return std::pair{float(image->width), float(image->height)};
    if (!path.empty()) return mod_picture_size(*r, path); // (a mod maker's button: its own picture)
    return std::nullopt;
}

std::shared_ptr<script::Reference> widget_class_member(Runtime& rt, const W& w, const std::string& n) {
    Runtime* r = &rt;
    auto fn = [&](script::NativeFunction f) { return constant(Vm::native(n, std::move(f))); };
    auto nothing = [&] { return fn([](Vm&, const Value&, Args) { return Value{}; }); };
    const std::string& type = w->data.type;
    // The photo mode's (photo_natives.cpp).
    if (type == "PhotoMapWindow" || type == "PhotoRenderFace")
        if (auto m = photo_member(rt, w, n)) return m;
    // The Analyzer's face: its control, the progress object and grab are values it keeps (mLoggerFace +0xA4 .. +0xAC).
    if (type == "LoggerFace" && (n == "control" || n == "progress" || n == "grab")) {
        std::shared_ptr<script::Reference>& cell = w->extras[n];
        if (!cell) cell = std::make_shared<script::Variable>(n == "grab" ? Value::integer(0) : Value{});
        return cell;
    }

    if (type == "RootWindow") {
        if (n == "setFocus" || n == "setFocusWidget")
            return fn([r, w](Vm&, const Value&, Args a) {
                r->set_focus(*w, a.empty() ? nullptr : a[0].as<LiveWidget>());
                return Value{};
            });
        if (n == "getFocusedWidget" || n == "getFocusWidget") return fn([w](Vm&, const Value&, Args) { return widget_value(w->focus.lock()); });
        if (n == "focus_widget") {
            auto a = std::make_shared<Accessor>();
            a->read = [w] { return widget_value(w->focus.lock()); };
            a->write = [r, w](const Value& v) { r->set_focus(*w, v.as<LiveWidget>()); };
            return a;
        }
        if (n == "isClosed")
            return fn([r, w](Vm&, const Value&, Args) {
                const auto& open = r->open_pages();
                return Value::integer(std::find(open.begin(), open.end(), w) == open.end());
            });
        // setExtraGpbName(name) (Prologue's mRootWindow, 0x22CE20): the pictures of `<project>/<LANG>/<name>.gpb` beside the
        // page's own (a license test's title, icon and map, its race code's pack).
        if (n == "setExtraGpbName")
            return fn([r](Vm&, const Value&, Args a) {
                if (Project* p = r->project(); p && !a.empty()) p->load_pack("projects/" + p->name(), arg_text(a, 0));
                return Value{};
            });
        if (n == "buildDefaultList" || n == "fadein" || n == "fadeout" || n == "crossfade" || n == "resetToolTip") return nothing();
    }

    if (type == "SelectBox" || type == "SelectBar") {
        auto move = [r, w](int step) {
            return [r, w, step](Vm&, const Value&, Args) {
                const int before = w->index();
                widget_set_index(*r, *w, before + step, true);
                return Value::integer(w->index() != before);
            };
        };
        if (n == "incIndex") return fn(move(1));
        if (n == "decIndex") return fn(move(-1));
        if (n == "moveIndex")
            return fn([r, w](Vm&, const Value&, Args a) {
                widget_set_index(*r, *w, arg_int(a, a.size() > 1 ? 1 : 0), true);
                return Value{};
            });
        if (n == "getFocusedWidget")
            return fn([w](Vm&, const Value&, Args) {
                const std::vector<W> rows = rows_of(*w);
                return rows.empty() ? Value{} : widget_value(rows[size_t(std::clamp(w->index(), 0, int(rows.size()) - 1))]);
            });
        if (n == "Count") return fn([w](Vm&, const Value&, Args) { return Value::integer(s32(rows_of(*w).size())); });
        if (n == "getItem")
            return fn([w](Vm&, const Value&, Args a) {
                const std::vector<W> rows = rows_of(*w);
                const int i = a.empty() ? w->index() : arg_int(a, 0);
                return i >= 0 && size_t(i) < rows.size() ? widget_value(rows[size_t(i)]) : Value{};
            });
        // callFocus(page): the page's focus comes to the box's chosen row; outFocus leaves it.
        if (n == "callFocus" || n == "outFocus") {
            const bool in = n == "callFocus";
            return fn([r, w, in](Vm&, const Value&, Args a) {
                const W page = a.empty() ? nullptr : a[0].as<LiveWidget>();
                LiveWidget* at = page ? page.get() : w->page();
                if (!at) return Value{};
                const std::vector<W> rows = rows_of(*w);
                if (in && !rows.empty()) r->set_focus(*at, rows[size_t(std::clamp(w->index(), 0, int(rows.size()) - 1))]);
                else if (!in) {
                    const W f = at->focus.lock();
                    if (f && f->parent_widget() == w.get()) r->set_focus(*at, nullptr);
                }
                return Value{};
            });
        }
        // insertSortChild(context, item, before(a, b), equal(a, b)): the row goes in before the
        // first one it sorts ahead of; a row equal to one there is not added.
        if (n == "insertSortChild")
            return fn([w](Vm& vm, const Value&, Args a) {
                W item;
                std::vector<Value> rest;
                for (const Value& v : a)
                    if (!item && v.as<LiveWidget>()) item = v.as<LiveWidget>();
                    else if (item) rest.push_back(v);
                if (!item) return Value{};
                const Value before = rest.empty() ? Value{} : rest[0], equal = rest.size() > 1 ? rest[1] : Value{};
                const LiveWidget* at = nullptr;
                for (const W& row : rows_of(*w)) {
                    const Value pair[2] = {widget_value(item), widget_value(row)};
                    if (equal.p && equal.p->callable() && vm.call(equal, pair).truthy()) return Value::integer(0);
                    if (!at && before.p && before.p->callable() && vm.call(before, pair).truthy()) at = row.get();
                }
                w->append(item, at);
                return Value::integer(1);
            });
        if (n == "sort")
            return fn([w](Vm& vm, const Value&, Args a) {
                const Value by = a.empty() ? Value{} : a.back();
                if (!by.p || !by.p->callable()) return Value{};
                std::stable_sort(w->children.begin(), w->children.end(), [&](const W& x, const W& y) {
                    if (!x->is_composite() || !y->is_composite()) return false;
                    const Value pair[2] = {widget_value(x), widget_value(y)};
                    return vm.call(by, pair).truthy();
                });
                w->arrange();
                return Value{};
            });
        if (n == "incLimit" || n == "decLimit" || n == "makeDisableList") return nothing();
    }

    if (type == "OptionMenu") {
        // The row the closed menu shows. ❓ In the program a child of that name; here the
        // chosen row of the list, which is what shows.
        if (n == "selected_item") {
            auto shown = std::make_shared<Accessor>();
            shown->read = [w] {
                const std::vector<W> items = option_items(*w);
                const int i = w->index();
                return i >= 0 && size_t(i) < items.size() ? widget_value(items[size_t(i)]) : Value{};
            };
            shown->write = [](const Value&) {};
            return shown;
        }
        if (n == "setIndex") // (context, index)
            return fn([r, w](Vm&, const Value&, Args a) {
                widget_set_index(*r, *w, arg_int(a, a.size() > 1 ? 1 : 0), false);
                return Value{};
            });
        if (n == "getItem")
            return fn([w](Vm&, const Value&, Args a) {
                const std::vector<W> items = option_items(*w);
                const int i = a.empty() ? w->index() : arg_int(a, 0);
                return i >= 0 && size_t(i) < items.size() ? widget_value(items[size_t(i)]) : Value{};
            });
        if (n == "Count") return fn([w](Vm&, const Value&, Args) { return Value::integer(s32(option_items(*w).size())); });
        // The rows a script added to popup::vbox are taken up (0x2C8CC8): the index kept among
        // them, the list closed, the chosen row shown.
        if (n == "applyAppend")
            return fn([w](Vm&, const Value&, Args) {
                w->set_property("index", Value::integer(option_clamp(*w, w->index())));
                w->set_property("is_popuped", Value::integer(0));
                arrange_option_menu(*w);
                return Value{};
            });
        // (context, a flag per row) (0x2C9B68): the chosen row made insensitive moves the index to
        // the first sensitive row.
        if (n == "setInsensitive")
            return fn([r, w](Vm&, const Value&, Args a) {
                const std::vector<Value>* flags = a.empty() ? nullptr : a.back().items();
                const std::vector<W> items = option_items(*w);
                bool chosen_off = false;
                for (size_t i = 0; flags && i < items.size() && i < flags->size(); ++i) {
                    const bool off = (*flags)[i].truthy();
                    if (int(i) == w->index() && off) chosen_off = true;
                    items[i]->set("insensitive", Value::integer(off));
                }
                if (chosen_off) widget_set_index(*r, *w, 0, false);
                return Value{};
            });
    }

    if (type == "SliderBar" || type == "ScaleBar") {
        bar_init(*w);
        const bool slider = type == "SliderBar";
        auto attr = [](std::function<Value()> read, std::function<void(const Value&)> write) {
            auto a = std::make_shared<Accessor>();
            a->read = std::move(read), a->write = std::move(write);
            return a;
        };
        auto num = [](const Value& v) { return v.is_number() ? v.as_float() : 0.0f; };
        if (slider) {
            // SliderBar's members (0x2DE4E8): whole numbers.
            auto set_range = [w](int lo, int hi) {
                if (hi < lo) hi = lo;
                bar_seti(*w, "min", lo), bar_seti(*w, "max", hi), bar_seti(*w, "display_min", lo), bar_seti(*w, "display_max", hi);
                bar_seti(*w, "index", std::clamp(w->index(), lo, hi));
            };
            if (n == "index")
                return attr([w] { return Value::integer(w->index()); }, [w, num](const Value& v) {
                    bar_seti(*w, "index", std::clamp(int(num(v)), int(w->data.number("min")), int(w->data.number("max"))));
                    bar_set(*w, "bar_shown", num(v)); // shown as given: an index out of the range slides in
                });
            if (n == "min") // the counter's end is passed as it is: max goes one up (0x2DF948)
                return attr([w] { return Value::integer(int(w->data.number("min"))); }, [w, set_range, num](const Value& v) { set_range(int(num(v)), int(w->data.number("max")) + 1); });
            if (n == "max") return attr([w] { return Value::integer(int(w->data.number("max"))); }, [w, set_range, num](const Value& v) { set_range(int(w->data.number("min")), int(num(v))); });
            if (n == "range")
                return attr([w] { return Value::array({Value::integer(int(w->data.number("min"))), Value::integer(int(w->data.number("max")))}); },
                            [set_range, num](const Value& v) {
                                if (v.items() && v.items()->size() >= 2) set_range(int(num((*v.items())[0])), int(num((*v.items())[1])));
                            });
            for (const char* plain : {"display_min", "display_max", "step"})
                if (n == plain)
                    return attr([w, plain] { return Value::integer(int(w->data.number(plain))); }, [w, plain, num](const Value& v) { bar_seti(*w, plain, int(num(v))); });
            if (n == "is_popuped") return attr([w] { return Value::integer(popup_open(*w)); }, {});
            if (n == "incIndex" || n == "decIndex") {
                const int sign = n == "incIndex" ? 1 : -1;
                return fn([w, sign](Vm&, const Value&, Args a) { return Value::integer(slider_move(*w, sign * arg_int(a, 0, 1))); });
            }
        } else {
            // ScaleBar's members (0x237828): numbers.
            auto clamped = [w](float v) { return std::clamp(v, bar_num(*w, "min", 0), std::max(bar_num(*w, "min", 0), bar_num(*w, "max", 100))); };
            if (n == "value") return attr([w] { return Value::number(bar_num(*w, "value", 0)); }, [w, clamped, num](const Value& v) { bar_set(*w, "value", clamped(num(v))); });
            if (n == "current_value")
                return attr([w] { return Value::number(bar_num(*w, "current_value", 0)); }, [w, clamped, num](const Value& v) { bar_set(*w, "current_value", clamped(num(v))); });
            if (n == "index") // rounded half away from zero
                return attr([w] {
                    const float v = bar_num(*w, "value", 0);
                    return Value::integer(int(v >= 0 ? v + 0.5f : v - 0.5f));
                }, [w, clamped, num](const Value& v) { bar_set(*w, "value", clamped(num(v))); });
            if (n == "repeat_start")
                return attr([w] { return Value::number(bar_num(*w, "repeat_start", 0)); }, [w, clamped, num](const Value& v) { bar_set(*w, "repeat_start", clamped(num(v))); });
            if (n == "min") return attr([w] { return Value::number(bar_num(*w, "min", 0)); }, [w, num](const Value& v) { bar_set(*w, "min", num(v)), bar_set(*w, "display_min", num(v)); });
            if (n == "max") return attr([w] { return Value::number(bar_num(*w, "max", 100)); }, [w, num](const Value& v) { bar_set(*w, "max", num(v)), bar_set(*w, "display_max", num(v)); });
            if (n == "range")
                return attr([w] { return Value::array({Value::number(bar_num(*w, "min", 0)), Value::number(bar_num(*w, "max", 100))}); }, [w, num](const Value& v) {
                    if (!v.items() || v.items()->size() < 2) return;
                    bar_set(*w, "min", num((*v.items())[0])), bar_set(*w, "display_min", num((*v.items())[0]));
                    bar_set(*w, "max", num((*v.items())[1])), bar_set(*w, "display_max", num((*v.items())[1]));
                });
            for (const char* plain : {"display_min", "display_max", "step"})
                if (n == plain) return attr([w, plain] { return Value::number(bar_num(*w, plain, 0)); }, [w, plain, num](const Value& v) { bar_set(*w, plain, num(v)); });
            if (n == "popup") return attr([w] { return Value::integer(popup_open(*w)); }, [w](const Value& v) { bar_seti(*w, "is_popuped", v.truthy()); });
            if (n == "cancel") return attr([w] { return Value::integer(w->data.number("cancel") != 0); }, {});
            if (n == "incValue" || n == "decValue") {
                const float sign = n == "incValue" ? 1.0f : -1.0f;
                return fn([w, sign, clamped](Vm&, const Value&, Args a) {
                    bar_set(*w, "value", clamped(bar_num(*w, "value", 0) + sign * arg_number(a, 0, bar_num(*w, "step", 1))));
                    return Value{};
                });
            }
        }
    }

    if (type == "ListBox") {
        // setItemTemplate(context, widget) (0x2B48F0): a copy kept; the rows take its size.
        if (n == "setItemTemplate")
            return fn([w](Vm&, const Value&, Args a) {
                const W from = a.empty() ? nullptr : a.back().as<LiveWidget>();
                w->list_template = from ? from->copy() : nullptr;
                if (const std::optional<Rect> g = from ? from->data.rect("geometry") : std::nullopt) w->item_w = g->w, w->item_h = g->h;
                for (ListItem& item : w->list_items) item.drawn.reset();
                return Value{};
            });
        // setItemCount(n) (0x2B4A00): rows kept up to n, new ones empty; the focus kept among
        // them; every row in range enters again.
        if (n == "setItemCount")
            return fn([w](Vm&, const Value&, Args a) {
                const int count = std::max(arg_int(a, a.size() > 1 ? 1 : 0), 0);
                if (count == list_count(*w)) return Value{};
                w->list_items.resize(size_t(count));
                lb_set_focus(*w, list_focus_index(*w));
                w->list_redraw = true;
                return Value{};
            });
        if (n == "getItemCount") return fn([w](Vm&, const Value&, Args) { return Value::integer(list_count(*w)); });
        // getItemWidget(index): the row's widget or nil; getItemWidget(index, context): made
        // from the template when there is none (0x2AF738).
        if (n == "getItemWidget")
            return fn([r, w](Vm&, const Value&, Args a) { return widget_value(lb_widget(*r, *w, arg_int(a, 0, -1), a.size() >= 2)); });
        if (n == "setItemWidget") // (index, context, widget)
            return fn([w](Vm&, const Value&, Args a) {
                const int i = arg_int(a, 0, -1);
                if (i >= 0 && i < list_count(*w)) {
                    const W to = a.size() > 2 ? a[2].as<LiveWidget>() : nullptr;
                    if (to) to->parent = std::static_pointer_cast<script::Module>(w->shared_from_this());
                    w->list_items[size_t(i)].widget = to;
                }
                return Value{};
            });
        if (n == "postRedraw")
            return fn([w](Vm&, const Value&, Args) {
                w->list_redraw = true;
                return Value{};
            });
        if (n == "incIndex" || n == "decIndex") {
            const bool on = n == "incIndex";
            return fn([w, on](Vm&, const Value&, Args a) { return Value::integer(lb_step(*w, a.empty() ? 1 : arg_int(a, 0, 1), on)); });
        }
        if (n == "focus_index") {
            auto a = std::make_shared<Accessor>();
            a->read = [w] { return Value::integer(list_focus_index(*w)); };
            a->write = [w](const Value& v) { lb_set_focus(*w, v.is_number() ? int(v.as_float()) : 0); };
            return a;
        }
        if (n == "justify") { // the same as v_justify (the registration's 0x2AF470)
            auto a = std::make_shared<Accessor>();
            a->read = [w] { return Value::number(lb_num(*w, "v_justify", 0)); };
            a->write = [w](const Value& v) { w->set_property("v_justify", Value::number(v.is_number() ? v.as_float() : 0)); };
            return a;
        }
        if (n == "update_callback" || n == "visible_callback" || n == "focus_callback") {
            auto& cell = w->extras[n];
            if (!cell) cell = std::make_shared<script::Variable>();
            return cell;
        }
        // A row's flags: getItemX(index), setItemX(index, value) (0x2AFAD8 on).
        auto row_of = [w](Args a) -> ListItem* {
            const int i = arg_int(a, 0, -1);
            return i >= 0 && i < list_count(*w) ? &w->list_items[size_t(i)] : nullptr;
        };
        if (n == "getItemVisible") return fn([row_of](Vm&, const Value&, Args a) { const ListItem* it = row_of(a); return Value::integer(it && it->visible); });
        if (n == "setItemVisible") return fn([row_of](Vm&, const Value&, Args a) { if (ListItem* it = row_of(a)) it->visible = arg_int(a, 1, 1) != 0; return Value{}; });
        if (n == "getItemUpdate") return fn([row_of](Vm&, const Value&, Args a) { const ListItem* it = row_of(a); return Value::integer(it ? it->update : 0); });
        if (n == "setItemUpdate") return fn([row_of](Vm&, const Value&, Args a) { if (ListItem* it = row_of(a)) it->update = arg_int(a, 1, 1); return Value{}; });
        if (n == "getItemActive") return fn([row_of](Vm&, const Value&, Args a) { const ListItem* it = row_of(a); return Value::integer(it && it->active); });
        if (n == "setItemActive") return fn([row_of](Vm&, const Value&, Args a) { if (ListItem* it = row_of(a)) it->active = arg_int(a, 1, 1) != 0; return Value{}; });
        if (n == "getItemInsensitive") return fn([row_of](Vm&, const Value&, Args a) { const ListItem* it = row_of(a); return Value::integer(it && it->insensitive); });
        if (n == "setItemInsensitive") return fn([row_of](Vm&, const Value&, Args a) { if (ListItem* it = row_of(a)) it->insensitive = arg_int(a, 1, 1) != 0; return Value{}; });
        if (n == "enterDragMode" || n == "leaveDragMode") return nothing();
    }

    if (type == "ScrollWindow" || type == "ScrollBox") {
        if (n == "updateGeometry")
            return fn([w](Vm&, const Value&, Args) {
                // A scroll window's space is what its children cover with its own start (0x2D2BF8,
                // 0x208568; with none, the window's size), and its bars are laid out.
                if (w->is("ScrollWindow")) {
                    LiveWidget* space = scroll_content(*w);
                    if (!space) return Value{};
                    if (space->children.empty()) put_rect(*space, {0, 0, own_rect(*w).w, own_rect(*w).h});
                    else {
                        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                        for (const auto& c : space->children) {
                            const Rect g = own_rect(*c);
                            x0 = std::min(x0, g.x), y0 = std::min(y0, g.y);
                            x1 = std::max(x1, g.x + g.w), y1 = std::max(y1, g.y + g.h);
                        }
                        put_rect(*space, {x0, y0, x1 - x0, y1 - y0});
                    }
                    if (const W bar = w->child("hbar")) arrange_scroll_bar(*bar, true);
                    if (const W bar = w->child("vbar")) arrange_scroll_bar(*bar, false);
                    return Value{};
                }
                // The content is as tall as what is in it.
                if (LiveWidget* content = scroll_content(*w)) {
                    float bottom = 0;
                    for (const auto& c : content->children)
                        if (const std::optional<Rect> g = c->data.rect("geometry"); g && c->visible()) bottom = std::max(bottom, g->y + g->h);
                    if (bottom > 0) content->set_property("h", Value::number(bottom));
                }
                return Value{};
            });
        // warp(widget): the space is at once where it shows the widget (0x2D2918).
        if (n == "warp")
            return fn([w](Vm&, const Value&, Args a) {
                LiveWidget* content = scroll_content(*w);
                if (!content) return Value{};
                const W to = a.empty() ? nullptr : a.back().as<LiveWidget>();
                float x = 0, y = 0;
                if (w->is("ScrollWindow") && to && scroll_window_target(*w, *to, x, y)) {
                    content->set_property("x", Value::number(x));
                    content->set_property("y", Value::number(y));
                } else if (!w->is("ScrollWindow")) content->set_property("y", Value::number(w->scroll_target));
                return Value{};
            });
    }

    if (type == "TextFace" || type == "ToolTipFace" || type == "InputTextFace") {
        // `value`: what the face shows: the value set, else its text, else its key's text in
        // the page the layout has it in (the memory card pages' StoreHouse texts).
        // `color`: the text's colour (`text_color`): the race menus copy a hidden face's to the
        // player's grid row (quick-arcade's MyGridColor) and to the tires' column.
        if (n == "color") {
            auto a = std::make_shared<Accessor>();
            a->read = [w] { return w->get("text_color"); };
            a->write = [w, r](const Value& v) {
                // A color object (MColorObject: its r, g, b, a, 0 .. 1), as the diary's days give it.
                if (v.kind == Kind::Object && r->vm().attribute(v, "r").is_number()) {
                    std::vector<Value> rgba;
                    for (const char* part : {"r", "g", "b", "a"}) {
                        const Value p = r->vm().attribute(v, part);
                        rgba.push_back(Value::integer(s32(std::lround((p.is_number() ? std::clamp(p.as_float(), 0.0f, 1.0f) : 1.0f) * 255.0f))));
                    }
                    w->set_property("text_color", Value::array(std::move(rgba)));
                    return;
                }
                w->set_property("text_color", v);
            };
            return a;
        }
        if (n == "value" && type == "TextFace") {
            auto a = std::make_shared<Accessor>();
            a->read = [w, r] {
                if (const std::string v = w->data.text("value"); !v.empty()) return Value::string(v);
                if (const std::string t = w->data.text("text"); !t.empty()) return Value::string(t);
                const std::string key = w->data.text("key");
                if (key.empty()) return Value::string({});
                LiveWidget* page = w->page();
                Project* p = page ? r->project_of(*page) : nullptr;
                const std::string where = w->data.has("key_page") ? w->data.text("key_page") : page ? page->name : std::string();
                return Value::string(p ? p->translate(where, key) : key);
            };
            a->write = [w](const Value& v) { w->set_property("value", v); };
            return a;
        }
        if (n == "getTextSize")
            return fn([w](Vm&, const Value&, Args) {
                float tw = 0, th = 0;
                if (text_measure().measure) text_measure().measure(*w, tw, th);
                else {
                    // Without a font: a guess from the characters (8 x 16 at scale 1).
                    const std::string text = w->data.text("text");
                    tw = float(text.size()) * 8.0f * (w->data.has("scale_x") ? w->data.number("scale_x") : 1.0f);
                    th = 16.0f * (w->data.has("scale_y") ? w->data.number("scale_y") : 1.0f);
                }
                return Value::array({Value::number(tw), Value::number(th)});
            });
        // abbreviate(): a text too long for its box ends in dots; adjustScale(): it is drawn
        // narrower to fit (1: across, 2: down; a scale the script sets after it stands). Both are
        // done when the text is drawn.
        if (n == "abbreviate")
            return fn([w](Vm&, const Value&, Args) {
                w->set_property("abbreviate", Value::integer(1));
                return Value{};
            });
        if (n == "adjustScale")
            return fn([w](Vm&, const Value&, Args) {
                w->set_property("adjust_scale", Value::integer(3));
                return Value{};
            });
    }
    if (type == "ToolTipFace") {
        // A tip is its page's when it loads (0x24A020) and shows the focused widget's tool tip
        // (its `tooltip_key`); remove() takes it off the page (0x249FF0) and its text away,
        // attach() puts it back (0x249FB8).
        if (n == "remove")
            return fn([w](Vm&, const Value&, Args) {
                w->set_property("value", Value::string({}));
                w->set_property("detached", Value::integer(1));
                return Value{};
            });
        if (n == "attach")
            return fn([w](Vm&, const Value&, Args) {
                w->set_property("attached", Value::integer(1));
                w->set_property("detached", Value::integer(0));
                return Value{};
            });
        if (n == "resetAction") return nothing();
    }

    // texcoord_top / _bottom / _left / _right = t (mImageFace's, 0x2A1748 / 0x2A17E8 / 0x2A1888 / 0x2A1928): an edge
    // of the picture's corners (`texcoord`, clockwise from the top left; the whole picture until set) moved to t:
    // the top corners' v, the bottom ones', the left corners' u, the right ones'. A bar drawn narrower with its
    // right edge at the same part of its picture shows that part, not the whole squeezed.
    if ((type == "ImageFace" || type == "FrameImageFace" || type == "MovieFace" || type == "ProgressFace") && n.rfind("texcoord_", 0) == 0) {
        const int edge = n == "texcoord_top" ? 0 : n == "texcoord_bottom" ? 1 : n == "texcoord_left" ? 2 : n == "texcoord_right" ? 3 : -1;
        if (edge >= 0) {
            static const int corners[4][2] = {{0, 1}, {3, 2}, {0, 3}, {1, 2}};
            const size_t axis = edge < 2 ? 1 : 0; // v for top and bottom, u for left and right
            auto a = std::make_shared<Accessor>();
            a->read = [w, edge, axis] {
                const Property* p = w->data.find("texcoord");
                const size_t c = size_t(corners[edge][0]);
                if (p && p->items.size() >= 4 && p->items[c].values.size() > axis) return Value::number(std::strtof(p->items[c].values[axis].c_str(), nullptr));
                return Value::number(edge == 1 || edge == 3 ? 1.0f : 0.0f);
            };
            a->write = [w, edge, axis](const Value& v) {
                Property* p = nullptr;
                for (Property& q : w->data.properties)
                    if (q.name == "texcoord") p = &q;
                if (!p) {
                    w->data.properties.push_back(Property{});
                    p = &w->data.properties.back();
                    p->name = "texcoord";
                }
                if (p->items.size() < 4) {
                    static const char* whole[4][2] = {{"0", "0"}, {"1", "0"}, {"1", "1"}, {"0", "1"}};
                    p->is_list = true, p->items.assign(4, Property{});
                    for (size_t i = 0; i < 4; ++i) p->items[i].type = "vector", p->items[i].values = {whole[i][0], whole[i][1]};
                }
                char text[32];
                std::snprintf(text, sizeof text, "%g", double(v.is_number() ? v.as_float() : 0.0f));
                for (const int c : corners[edge]) {
                    p->items[size_t(c)].values.resize(2, "0");
                    p->items[size_t(c)].values[axis] = text;
                }
            };
            return a;
        }
    }
    if (type == "ImageFace" || type == "FrameImageFace" || type == "ColorChipFace" || type == "CarFace") {
        if (n == "getImagePath") return fn([w](Vm&, const Value&, Args) { return w->get("image_path"); });
        if (n == "setImagePath")
            return fn([w](Vm&, const Value&, Args a) {
                if (!a.empty()) w->set_property("image_path", a.back());
                return Value{};
            });
        // adjustSize(): the face takes its picture's size.
        if (n == "adjustSize")
            return fn([r, w](Vm&, const Value&, Args) {
                if (const auto size = picture_size(r, *w)) {
                    w->set_property("w", Value::number(size->first));
                    w->set_property("h", Value::number(size->second));
                }
                return Value{};
            });
        // setData(car, index): a paint chip takes the car's paint of that index; setData(garage
        // car): that car's own paint (the header's car, the used cars' rows).
        if (type == "ColorChipFace" && n == "setData")
            return fn([w](Vm& vm, const Value&, Args a) {
                if (a.empty()) return Value{};
                s32 i = 0;
                if (a.size() >= 2) i = arg_int(a, 1);
                else {
                    const Value own = vm.attribute(a[0], "getColorIndex");
                    if (own.is_nil()) return Value{};
                    const Value v = vm.call(own, {});
                    if (!v.is_number()) return Value{};
                    i = s32(v.as_float());
                }
                const Value colors = vm.call(vm.attribute(a[0], "getColors"), {});
                const std::vector<Value>* list = colors.items();
                if (!list || i < 0 || size_t(i) >= list->size()) return Value{};
                set_chip_words(*w, vm.attribute((*list)[size_t(i)], "chip"));
                return Value{};
            });
        // setColor(words): the chip words of a paint (MQuickWork.getColorChipInfo).
        if (type == "ColorChipFace" && n == "setColor")
            return fn([w](Vm&, const Value&, Args a) {
                if (!a.empty()) set_chip_words(*w, a[0]);
                return Value{};
            });
        if (type == "CarFace" && n == "loadModelOptionMotion")
            return fn([r, w](Vm&, const Value&, Args a) {
                w->set_property("option_motion", a.empty() ? Value::string({}) : a[0]);
                w->set_property("option_motion_frame", Value::number(float(r->frame())));
                // Its clip's length: isPlayingModelMotion answers for it.
                const std::shared_ptr<CarModelObject> model = car_model_of(r, w);
                model->m.has_option_motion = false;
                LiveWidget* page = w->page();
                Project* project = page ? r->project_of(*page) : nullptr;
                if (!a.empty() && project) try {
                        if (const std::optional<Bytes> f = project->entry(page->name, a[0].to_string()); f && SkeletonMotion::is_motion(*f)) {
                            const SkeletonMotion sm = SkeletonMotion::parse(*f);
                            model->m.has_option_motion = true;
                            model->m.option_length = sm.length(), model->m.option_loop_start = sm.loop_start();
                        }
                    } catch (const std::exception&) {
                    }
                return Value{};
            });
        // loadModelMotion(path, set, align = 0, shadow = 0, rot = 0): a motion for a model set
        // (0x139030 -> 0x1540C8), appended to the car model's.
        if (type == "CarFace" && n == "loadModelMotion")
            return fn([r, w](Vm&, const Value&, Args a) {
                if (a.empty()) return Value{};
                CarMotion::ModelMotion e;
                e.path = a[0].to_string();
                e.set = a.size() > 1 ? arg_int(a, 1) : 0;
                e.align = a.size() > 2 ? arg_int(a, 2) : 0;
                e.shadow = a.size() > 3 && a[3].truthy();
                e.rot = a.size() > 4 && a[4].is_number() ? a[4].as_float() : 0.0f;
                std::vector<CarMotion::ModelMotion>& list = car_model_of(r, w)->m.model_motions;
                // (A page set up again on the same face asks for the same entries again.)
                for (const CarMotion::ModelMotion& o : list)
                    if (o.path == e.path && o.set == e.set && o.align == e.align && o.shadow == e.shadow && o.rot == e.rot) return Value{};
                list.push_back(e);
                return Value{};
            });
        if (type == "CarFace" && n == "loadModelSet")
            return fn([w](Vm&, const Value&, Args a) {
                if (a.empty()) return Value{};
                const std::string path = a[0].to_string(), sets = w->data.text("model_sets");
                if (("|" + sets + "|").find("|" + path + "|") == std::string::npos)
                    w->set_property("model_sets", Value::string(sets.empty() ? path : sets + "|" + path));
                return Value{};
            });
        if (n == "setData" || n == "setColor" || (n == "setColorIndex" && type != "CarFace") || n == "syncLoading" || n == "loadModelSet" || n == "loadModelMotion" ||
            n == "loadModelOptionMotion")
            return nothing();
    }
    if (type == "CarFace") {
        // car_code: the car shown; car_data: an MCarGarage of it; car_model: where the model
        // stands and how it turns (MCarModel's attributes, kept for the scene that draws it).
        if (n == "car_data" || n == "car_garage") {
            auto a = std::make_shared<Accessor>();
            a->read = [r, w]() -> Value {
                const std::string code = w->data.text("car_code");
                if (code.empty()) return Value{};
                const auto it = w->extras.find("car_data of " + code);
                if (it != w->extras.end())
                    if (auto cell = std::dynamic_pointer_cast<script::Variable>(it->second)) return cell->value;
                const Value car = make_car_object(*r, code, int(w->data.number("car_color")));
                w->extras["car_data of " + code] = std::make_shared<script::Variable>(car);
                return car;
            };
            a->write = [r, w](const Value& v) {
                const std::string code = v.is_nil() ? std::string() : r->vm().attribute(v, "car_code").to_string();
                w->set_property("car_code", Value::string(code));
                if (!code.empty()) w->extras["car_data of " + code] = std::make_shared<script::Variable>(v);
                // A new car stands where the layout says from now: the script's own placing after
                // it (Home's garage car turned to its front, ry 180 and warp) holds.
                const std::shared_ptr<CarModelObject> model = car_model_of(r, w);
                if (!code.empty() && model->m.placed_car != code) {
                    const CarFaceSettings s = car_face_settings(w->data);
                    model->m.set_car(s.trans, s.rotate);
                    model->m.placed_car = code;
                }
                // A garage car shows in its own paint (its record's).
                if (!code.empty()) {
                    const Value index = r->vm().attribute(v, "getColorIndex");
                    if (!index.is_nil()) {
                        const Value paint = r->vm().call(index, {});
                        if (paint.is_number()) w->set_property("car_color", Value::integer(s32(paint.as_float())));
                    }
                }
            };
            return a;
        }
        // stream (Prologue's MModelStream): load(name), sync_mpeg; the app moves the face's camera
        // and lights by it (menu/model_stream_prologue.h).
        if (n == "stream") {
            auto& cell = w->extras["stream"];
            if (!cell) {
                auto stream = std::make_shared<Object>("MModelStream");
                stream->method("load", [w](Vm&, const Value&, Args a) {
                    if (!a.empty()) w->set_property("model_stream", Value::string(a.back().to_string()));
                    return Value{};
                });
                stream->set("sync_mpeg", Value::integer(0));
                cell = std::make_shared<script::Variable>(object_value(stream));
            }
            return cell;
        }
        if (n == "car_code") {
            auto a = std::make_shared<Accessor>();
            a->read = [w] { return Value::string(w->data.text("car_code")); };
            a->write = [r, w](const Value& v) {
                const std::string code = v.is_nil() ? std::string() : v.to_string();
                w->set_property("car_code", Value::string(code));
                // (A car set stands where the layout says from now, before the script's own placing: the arcade car page
                // brings it in from 8.5 m toward the camera, its front first.)
                const std::shared_ptr<CarModelObject> model = car_model_of(r, w);
                if (!code.empty() && model->m.placed_car != code) {
                    const CarFaceSettings s = car_face_settings(w->data);
                    model->m.set_car(s.trans, s.rotate);
                    model->m.placed_car = code;
                }
            };
            return a;
        }
        if (n == "car_model") {
            car_model_of(r, w);
            return w->extras["car_model"];
        }
        if (n == "setColorIndex")
            return fn([w](Vm&, const Value&, Args a) {
                w->set_property("car_color", Value::integer(arg_int(a, 0)));
                return Value{};
            });
    }
    if (type == "ColorFace" || type == "ColorWindow") {
        if (n == "getAlpha") return fn([w](Vm&, const Value&, Args) { return Value::number(own_alpha(*w)); });
        if (n == "setAlpha")
            return fn([w](Vm&, const Value&, Args a) {
                if (!a.empty()) w->set_property("alpha", a[0]);
                return Value{};
            });
        if (n == "getColor") return nothing();
    }
    if (type == "SlideShowFace") {
        // doPlay(), doStop(), isPlaying() (the state at +0xAC): the photos are the drawing's
        // (app/menu_slides.h); `shuffle` and `demo` are plain attributes.
        if (n == "doPlay" || n == "doStop")
            return fn([w, n](Vm&, const Value&, Args) {
                w->set_property("slide_playing", Value::integer(n == "doPlay" ? 1 : 0));
                return Value{};
            });
        if (n == "isPlaying") return fn([w](Vm&, const Value&, Args) { return Value::integer(w->data.number("slide_playing") != 0); });
    }
    if (type == "MovieFace") {
        // setFrameHook(frame, function name): that function of the face is called when the
        // movie shows that frame; setUnUpdate(): the movie stops moving (app/menu_movies.cpp).
        if (n == "setFrameHook")
            return fn([w](Vm&, const Value&, Args a) {
                w->set_property("frame_hook", Value::integer(arg_int(a, 0, -1)));
                w->set_property("frame_hook_event", a.size() > 1 ? a[1] : Value::string({}));
                return Value{};
            });
        if (n == "setUnUpdate")
            return fn([w](Vm&, const Value&, Args a) {
                w->set_property("un_update", Value::integer(a.empty() ? 1 : a[0].truthy()));
                return Value{};
            });
        // reserveName(name), load(context, name): the movie it plays from its next picture. (A movie
        // is let go when its face is no longer drawn, not by unload.)
        if (n == "reserveName" || n == "load")
            return fn([w](Vm&, const Value&, Args a) {
                if (!a.empty() && a.back().kind == script::Kind::String) w->set_property("movie_name", a.back());
                return Value{};
            });
        if (n == "refOther" || n == "unload") return nothing();
    }
    if (type == "ModelFace") {
        // setModelSetPath(path, a, from disk, b) (0x2BB220: the path, then three flags of +0x308;
        // the second is model_set_from_disk, 0x2BE938): the prize page's money pouch,
        // piece/prize/other.mdl. getModelSetPath: the path.
        if (n == "setModelSetPath")
            return fn([w](Vm&, const Value&, Args a) {
                if (a.empty()) return Value{};
                w->set_property("model_set_path", Value::string(a[0].to_string()));
                if (a.size() > 2) w->set_property("model_set_from_disk", Value::integer(a[2].truthy() ? 1 : 0));
                return Value{};
            });
        if (n == "getModelSetPath")
            return fn([w](Vm&, const Value&, Args) { return Value::string(w->data.text("model_set_path")); });
        for (const char* m : {"getEnvImagePath", "setEnvImagePath", "getModelMotionPath", "setModelMotionPath",
                              "reset", "stopRotate"})
            if (n == m) return nothing();
    }
    if (type == "TextBoxFace" && n == "clear") return nothing();
    if (type == "FlashFace") {
        // reset(t): the movie from t seconds (0x292170): counted by the drawer from now.
        if (n == "reset")
            return fn([w](Vm&, const Value&, Args a) {
                w->set_property("flash_restarts", Value::integer(int(w->data.number("flash_restarts")) + 1));
                w->set_property("flash_restart_at", Value::number(a.empty() || !a[0].is_number() ? 0.0f : a[0].as_float()));
                return Value{};
            });
        if (n == "getFlashPath") return fn([w](Vm&, const Value&, Args) { return w->get("flash_path"); });
        if (n == "setFlashPath")
            return fn([w](Vm&, const Value&, Args a) {
                if (!a.empty()) w->set_property("flash_path", a.back());
                return Value{};
            });
    }
    if (type == "GraphFace" && n == "changePoints")
        // changePoints(pairs, start = 0): points from `start` replaced, as many as there are.
        return fn([w](Vm&, const Value&, Args a) {
            Property* p = nullptr;
            for (Property& q : w->data.properties)
                if (q.name == "points") p = &q;
            const std::vector<Value>* pairs = a.empty() ? nullptr : a[0].items();
            if (!p || !pairs) return Value{};
            const size_t start = a.size() > 1 && a[1].is_number() ? size_t(std::max(0.0f, a[1].as_float())) : 0;
            for (size_t i = 0; i < pairs->size() && 2 * (start + i) + 1 < p->values.size(); ++i)
                if (const std::vector<Value>* xy = (*pairs)[i].items(); xy && xy->size() >= 2) {
                    char b[32];
                    std::snprintf(b, sizeof b, "%g", double((*xy)[0].as_float()));
                    p->values[2 * (start + i)] = b;
                    std::snprintf(b, sizeof b, "%g", double((*xy)[1].as_float()));
                    p->values[2 * (start + i) + 1] = b;
                }
            return Value{};
        });
    if (type == "InputTextFace") {
        // The text typed (`value`), at most `max_input_length` characters, and the place the next letter goes: kept as the
        // letters after it (`input_after`), the end to begin with. (The program's key handling, 0x2A6D10.)
        auto place = [](const LiveWidget& face, const std::string& value) {
            size_t at = value.size();
            for (int k = int(face.data.number("input_after")); k > 0 && at > 0; --k) at = utf8_before(value, at);
            return at;
        };
        auto after = [](LiveWidget& face, int n) { face.set_property("input_after", Value::integer(std::max(n, 0))); };
        if (n == "putString")
            return fn([w, place](Vm&, const Value&, Args a) {
                std::string value = w->data.text("value");
                const std::string typed = a.empty() ? std::string() : a[0].to_string();
                const size_t most = w->data.has("max_input_length") ? size_t(std::max(w->data.number("max_input_length"), 0.0f)) : value.size() + typed.size();
                if (value.size() >= most) return Value{};
                value.insert(place(*w, value), typed.substr(0, most - value.size()));
                w->set_property("value", Value::string(value));
                return Value{};
            });
        if (n == "backspace" || n == "delete")
            return fn([w, place, after, back = n == "backspace"](Vm&, const Value&, Args) {
                std::string value = w->data.text("value");
                size_t at = place(*w, value);
                if (back ? at == 0 : at >= value.size()) return Value{};
                size_t from = back ? utf8_before(value, at) : at;
                size_t to = at;
                if (!back) {
                    to = at + 1;
                    while (to < value.size() && (u8(value[to]) & 0xC0) == 0x80) ++to;
                    after(*w, int(w->data.number("input_after")) - 1);
                }
                value.erase(from, to - from);
                w->set_property("value", Value::string(value));
                return Value{};
            });
        if (n == "left" || n == "right")
            return fn([w, place, after, left = n == "left"](Vm&, const Value&, Args) {
                if (!left || place(*w, w->data.text("value")) > 0) after(*w, int(w->data.number("input_after")) + (left ? 1 : -1));
                return Value{};
            });
    }
    // The boxes that lay their children out: adjustSize() and the automatic rows and columns.
    if (w->is_composite()) {
        // adjustSize() (mComposite's, 0x207DB0): each face child goes to the box's top left, and the
        // box takes the largest of their sizes: a picture face its picture's (vf55, 0x2A2360), any
        // other face its own. Other children neither move nor count. (A face without a rectangle
        // of its own stays so: it is at the top left already and fills the box at its new size.)
        if (n == "adjustSize")
            return fn([r, w](Vm&, const Value&, Args) {
                float width = 0, height = 0;
                for (const auto& c : w->children) {
                    if (!is_face(*c)) continue;
                    const std::string& t = c->data.type;
                    std::optional<std::pair<float, float>> size;
                    if (t == "ImageFace" || t == "FrameImageFace" || t == "ProgressFace") size = picture_size(r, *c);
                    if (!size) size = std::pair{c->get("w").as_float(), c->get("h").as_float()};
                    if (c->data.has("geometry")) c->set_property("x", Value::number(0)), c->set_property("y", Value::number(0));
                    width = std::max(width, size->first), height = std::max(height, size->second);
                }
                w->set_property("w", Value::number(width));
                w->set_property("h", Value::number(height));
                return Value{};
            });
        // arrangeColAuto(columns, x gap, y gap): the children in rows of that many.
        if (n == "arrangeColAuto" || n == "arrangeRowAuto") {
            const bool by_columns = n == "arrangeColAuto";
            return fn([w, by_columns](Vm&, const Value&, Args a) {
                const int per = std::max(arg_int(a, 0, 1), 1);
                const float gap_x = arg_number(a, 1), gap_y = arg_number(a, 2);
                int i = 0;
                float bottom = 0;
                for (const auto& c : w->children) {
                    if (!c->visible() || !c->is_composite()) continue;
                    const std::optional<Rect> g = c->data.rect("geometry");
                    const float cw = g ? g->w : 0, ch = g ? g->h : 0;
                    const int col = by_columns ? i % per : i / per, row = by_columns ? i / per : i % per;
                    c->set_property("x", Value::number(float(col) * (cw + gap_x)));
                    c->set_property("y", Value::number(float(row) * (ch + gap_y)));
                    bottom = std::max(bottom, float(row) * (ch + gap_y) + ch);
                    ++i;
                }
                if (bottom > 0) w->set_property("h", Value::number(bottom));
                return Value{};
            });
        }
    }
    return nullptr;
}

TextMeasure& text_measure() {
    static TextMeasure m;
    return m;
}

} // namespace gt4r::menu
