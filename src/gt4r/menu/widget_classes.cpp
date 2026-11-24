// The widget classes' own members and behavior: the page (MRootWindow), the boxes that choose
// (MSelectBox, MSelectBar, MOptionMenu, MSliderBar, MScaleBar, MListBox), the scroll window and
// the faces. Each class's member names are the program's (its registration tables); what is
// read from the program's routines and what is judged from the scripts is in
// docs/formats/menu.md ("Running").

#include "formats/gs_memory.h"
#include "formats/tex1.h"
#include "gt4r/menu/faces_car.h"
#include "gt4r/menu/runtime_internal.h"

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

// Where an option menu's rows go: one under the other at their own height, the list moved so
// the chosen row lies over the menu. ❓ By eye.
void arrange_option_menu(LiveWidget& menu) {
    const W popup = menu.child("popup");
    const W list = option_list(menu);
    if (!popup || !list) return;
    float y = 0, chosen_y = 0;
    const std::vector<W> items = rows_of(*list);
    const int index = menu.index();
    for (size_t i = 0; i < items.size(); ++i) {
        const float h = items[i]->data.rect("geometry") ? items[i]->data.rect("geometry")->h : 24.0f;
        items[i]->set_property("x", Value::number(0));
        items[i]->set_property("y", Value::number(y));
        if (int(i) == index) chosen_y = y;
        y += h;
    }
    list->set_property("h", Value::number(std::max(y, 1.0f)));
    for (const auto& c : list->children)
        if (!c->is_composite()) c->set_property("h", Value::number(std::max(y, 1.0f))); // the list's own background
    for (const auto& c : popup->children)
        if (!c->is_composite()) c->set_property("h", Value::number(std::max(y, 1.0f)));
    popup->set_property("h", Value::number(std::max(y, 1.0f)));
    popup->set_property("y", Value::number(popup_open(menu) ? -chosen_y : 0.0f));
    // Closed, only the chosen row shows.
    for (size_t i = 0; i < items.size(); ++i) {
        const bool chosen = int(i) == index;
        items[i]->set_property("visible", Value::integer(popup_open(menu) || chosen));
        if (!popup_open(menu) && chosen) items[i]->set_property("y", Value::number(0));
    }
    for (const auto& c : list->children)
        if (!c->is_composite()) c->set_property("visible", Value::integer(popup_open(menu)));
    for (const auto& c : popup->children)
        if (!c->is_composite()) c->set_property("visible", Value::integer(popup_open(menu)));
}

void set_popup(Runtime& rt, LiveWidget& menu, bool open) {
    if (popup_open(menu) == open) return;
    menu.set_property("is_popuped", Value::integer(open));
    arrange_option_menu(menu);
    LiveWidget* page = menu.page();
    if (!page) return;
    if (open) {
        const std::vector<W> items = option_items(menu);
        const int i = std::clamp(menu.index(), 0, std::max(int(items.size()) - 1, 0));
        rt.call_now(menu, "onPopupOpen", {rt.context()});
        if (!items.empty()) rt.set_focus(*page, items[size_t(i)]);
    } else {
        rt.set_focus(*page, menu.self());
        rt.call_now(menu, "onPopupClosed", {rt.context()});
    }
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
    // ScaleBar: a press of a key along it starts a hold (repeat_start); ❓ the script's
    // onKeyPress, which the program calls before the value moves, comes after it here.
    if (dir) {
        bar_set(w, "repeat_start", std::clamp(bar_num(w, "value", 0), bar_num(w, "min", 0), bar_num(w, "max", 100)));
        scale_step_notify(rt, w);
    }
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

} // namespace

// ---- the list box: rows made from a template as they come into view ------------------------

int list_count(const LiveWidget& w) { return int(w.data.number("item_count")); }

W list_item(Runtime& rt, LiveWidget& list, int i, bool make) {
    if (i < 0 || i >= list_count(list)) return nullptr;
    if (list.items.size() < size_t(list_count(list))) list.items.resize(size_t(list_count(list)));
    W& row = list.items[size_t(i)];
    if (!row && make) {
        const auto it = list.extras.find("item_template");
        const auto tmpl = it == list.extras.end() ? nullptr : std::dynamic_pointer_cast<script::Variable>(it->second);
        const W from = tmpl ? tmpl->value.as<LiveWidget>() : nullptr;
        if (!from) return nullptr;
        row = from->copy();
        row->parent = std::static_pointer_cast<script::Module>(list.shared_from_this());
        row->runtime = &rt;
        row->set_property("visible", Value::integer(1));
    }
    return row;
}

Value list_callback(const LiveWidget& list, const char* name) {
    const auto it = list.extras.find(name);
    const auto cell = it == list.extras.end() ? nullptr : std::dynamic_pointer_cast<script::Variable>(it->second);
    return cell ? cell->value : Value{};
}

// A list box (MListBox): rows made from a template, as many as its item count, one after the
// other along its direction (across unless `direction` is 1), `spacing` apart, the focused row
// `magnify_ratio` larger and kept in the middle while the rows are longer than the box.
// ❓ Laid out as the select box is (the two share MScrollable and these attributes); MListBox's
// own routines (0x2B24D8 on) are not read: its `h_justify` / `v_justify` are taken as where a
// row sits across the box (0 start, 0.5 middle, 1 end), `step_min` / `step_max` are not used.
void arrange_list(Runtime& rt, LiveWidget& list, bool warp) {
    const std::optional<Rect> box = list.data.rect("geometry");
    const int n = list_count(list);
    list.children.clear();
    if (!box || n <= 0) return;
    const bool across = list.data.number("direction") == 0;
    const int focus = std::clamp(int(list.data.number("focus_index")), 0, n - 1);
    const W first = list_item(rt, list, focus, true);
    if (!first) return;
    const std::optional<Rect> size = first->data.has("item_w") ? std::optional<Rect>(Rect{0, 0, first->data.number("item_w"), first->data.number("item_h")})
                                                              : first->data.rect("geometry");
    const float item_w = size ? size->w : 32.0f, item_h = size ? size->h : 24.0f;
    const float magnify = list.data.has("magnify_ratio") ? list.data.number("magnify_ratio") : 1.0f;
    const float spacing = list.data.number("spacing"), border = list.data.number("border_width");
    const float item = across ? item_w : item_h, extent = across ? box->w : box->h;
    const float pitch = item + spacing, chosen = item * magnify, total = pitch * float(n - 1) + chosen;
    float target = 0;
    if (total < extent) target = (extent - total) * 0.5f;
    else {
        target = extent * 0.5f - (pitch * float(focus) + chosen * 0.5f);
        target = std::min(target, item * 0.3f);
        target = std::max(target, extent - item * 0.3f - total);
    }
    if (warp) list.scroll = target;
    else {
        const float ratio = list.data.has("interpolate_ratio") ? list.data.number("interpolate_ratio") : 0.2f;
        const float d = (target - list.scroll) * ratio;
        list.scroll = d >= -0.1f && d <= 0.1f ? target : list.scroll + d;
    }
    list.scroll_target = target;
    const bool has_focus = list.page() && list.page()->focus.lock().get() == &list;
    const float justify = across ? (list.data.has("v_justify") ? list.data.number("v_justify") : 0.0f)
                                 : (list.data.has("h_justify") ? list.data.number("h_justify") : 0.0f);
    // The rows in view (❓ from the scripts' use: labomode's and list_box's callbacks): the ones
    // that came into view are given to `visible_callback(context, [indexes])`, which makes their
    // widgets (getItemVisible answers whether a row is in view); then every row in view whose
    // update flag is set to `update_callback(context, [indexes])` (the scripts clear the flag
    // when a row is drawn in full).
    auto call = [&rt](const Value& fn, const std::vector<int>& rows) {
        if (rows.empty() || !fn.p || !fn.p->callable()) return;
        std::vector<Value> list;
        for (int i : rows) list.push_back(Value::integer(i));
        try {
            const Value args[2] = {rt.context(), Value::array(std::move(list))};
            rt.vm().call(fn, args);
        } catch (const script::ScriptError& e) {
            rt.note(std::string("script error in a list's callback: ") + e.what());
        }
    };
    std::vector<u8> was = list.items_in_view;
    list.items_in_view.assign(size_t(n), 0);
    std::vector<int> appeared;
    {
        float at = list.scroll;
        for (int i = 0; i < n; ++i) {
            const float length = item * (i == focus ? magnify : 1.0f);
            if (at + length >= -pitch && at <= extent + pitch) {
                list.items_in_view[size_t(i)] = 1;
                if (size_t(i) >= was.size() || !was[size_t(i)]) appeared.push_back(i);
            }
            at += length + spacing;
        }
    }
    call(list_callback(list, "visible_callback"), appeared);
    std::vector<int> updates;
    float at = list.scroll;
    for (int i = 0; i < n; ++i) {
        const float scale = i == focus ? magnify : 1.0f;
        const float length = item * scale;
        if (list.items_in_view[size_t(i)]) {
            if (const W row = list_item(rt, list, i, true)) {
                if (!row->data.has("item_w")) row->set_property("item_w", Value::number(item_w)), row->set_property("item_h", Value::number(item_h));
                const float w = item_w * scale, h = item_h * scale;
                const float cross = ((across ? box->h : box->w) - (across ? h : w) - border) * justify + border * (1.0f - justify);
                row->set_property("w", Value::number(w));
                row->set_property("h", Value::number(h));
                row->set_property("x", Value::number(across ? at : cross));
                row->set_property("y", Value::number(across ? cross : at));
                row->state = i == focus ? row->state | kActive : row->state & ~kActive;
                row->state = i == focus && has_focus ? row->state | (kFocus | kPrelight) : row->state & ~(kFocus | kPrelight);
                list.children.push_back(row);
                if (row->data.number("item_update") != 0) updates.push_back(i);
            }
        }
        at += length + spacing;
    }
    call(list_callback(list, "update_callback"), updates);
}

void list_focus(Runtime& rt, LiveWidget& list, int index, bool notify) {
    const int n = list_count(list);
    if (n <= 0) return;
    index = list.data.number("repeat") != 0 ? ((index % n) + n) % n : std::clamp(index, 0, n - 1);
    const int before = int(list.data.number("focus_index"));
    list.set_property("focus_index", Value::integer(index));
    arrange_list(rt, list, !notify);
    if (notify && index != before) {
        if (LiveWidget* page = list.page()) rt.navigate_sound(*page);
        // focus_callback(context, index), else the list's own onValueChanged.
        const Value cb = list_callback(list, "focus_callback");
        if (cb.p && cb.p->callable()) {
            try {
                const Value args[2] = {rt.context(), Value::integer(index)};
                rt.vm().call(cb, args);
            } catch (const script::ScriptError& e) {
                rt.note(std::string("script error in a list's focus: ") + e.what());
            }
        } else rt.call_now(list, "onValueChanged", {rt.context(), Value::integer(index)});
    }
}

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
        row.state = i == index ? row.state | kActive : row.state & ~kActive;
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
    const int condition = int(w.data.number("visible_condition")), mask = int(w.data.number("visible_compare"));
    if (mask) return ((w.state ^ condition) & mask) == 0;
    if (condition) return (w.state & condition) != 0;
    return true;
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
        const int n = int(option_items(w).size());
        if (n > 0) index = std::clamp(index, 0, n - 1);
        const int before = w.index();
        w.set_property("index", Value::integer(index));
        arrange_option_menu(w);
        if (notify && index != before) rt.call_now(w, "onValueChanged", {rt.context(), Value::integer(index)});
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
    if (w.is("ListBox")) {
        const bool across = w.data.number("direction") == 0;
        const int step = across ? (k == Key::Left ? -1 : k == Key::Right ? 1 : 0) : (k == Key::Up ? -1 : k == Key::Down ? 1 : 0);
        if (!step) return false;
        const int before = int(w.data.number("focus_index"));
        list_focus(rt, w, before + step, true);
        return int(w.data.number("focus_index")) != before;
    }
    if (w.is("OptionMenu")) {
        if (!popup_open(w)) {
            if (k != Key::Accept || w.insensitive()) return false;
            set_popup(rt, w, true);
            return true;
        }
        const std::vector<W> items = option_items(w);
        LiveWidget* page = w.page();
        const W at = page ? page->focus.lock() : nullptr;
        int cursor = -1;
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i] == at) cursor = int(i);
        if (k == Key::Up || k == Key::Down) {
            for (int i = cursor + (k == Key::Up ? -1 : 1); i >= 0 && i < int(items.size()); i += k == Key::Up ? -1 : 1)
                if (!items[size_t(i)]->insensitive()) {
                    rt.set_focus(*page, items[size_t(i)]);
                    break;
                }
            return true;
        }
        if (k == Key::Accept) {
            set_popup(rt, w, false);
            if (cursor >= 0) widget_set_index(rt, w, cursor, true);
            return true;
        }
        if (k == Key::Cancel) {
            set_popup(rt, w, false);
            return true;
        }
        return k == Key::Left || k == Key::Right; // the list keeps the keys while it is open
    }
    if (w.is("SliderBar") || w.is("ScaleBar")) return bar_key(rt, w, k);
    return false;
}

void step_widgets(Runtime& rt, LiveWidget& w) {
    if (is_select_box(w)) select_box_layout(w, false);
    if (w.is("SliderBar") || w.is("ScaleBar")) arrange_slider(w);
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
                const int count = list_count(*c), focus = int(c->data.number("focus_index"));
                more[0] = focus < count - 1, more[1] = focus > 0;
            } else continue;
            if (const W inc = w.child("inc_arrow")) inc->state = more[0] ? inc->state | kActive : inc->state & ~kActive;
            if (const W dec = w.child("dec_arrow")) dec->state = more[1] ? dec->state | kActive : dec->state & ~kActive;
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
        if (focus && content && scroll_window_target(w, *focus, tx, ty)) {
            const float ratio = w.data.has("interpolate_ratio") ? w.data.number("interpolate_ratio") : 0.3f;
            const float x = content->get("x").as_float(), y = content->get("y").as_float();
            if (x != tx || y != ty) {
                content->set_property("x", Value::number(std::fabs(tx - x) < 0.01f ? tx : x + (tx - x) * ratio));
                content->set_property("y", Value::number(std::fabs(ty - y) < 0.01f ? ty : y + (ty - y) * ratio));
            }
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

std::shared_ptr<script::Reference> widget_class_member(Runtime& rt, const W& w, const std::string& n) {
    Runtime* r = &rt;
    auto fn = [&](script::NativeFunction f) { return constant(Vm::native(n, std::move(f))); };
    auto nothing = [&] { return fn([](Vm&, const Value&, Args) { return Value{}; }); };
    const std::string& type = w->data.type;

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
        // The rows a script added to popup::vbox are taken up.
        if (n == "applyAppend")
            return fn([w](Vm&, const Value&, Args) {
                arrange_option_menu(*w);
                return Value{};
            });
        if (n == "setInsensitive") // (context, a flag per row)
            return fn([w](Vm&, const Value&, Args a) {
                const std::vector<Value>* flags = a.empty() ? nullptr : a.back().items();
                const std::vector<W> items = option_items(*w);
                for (size_t i = 0; flags && i < items.size() && i < flags->size(); ++i) items[i]->set("insensitive", Value::integer((*flags)[i].truthy()));
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
        if (n == "setItemTemplate") // (context, widget)
            return fn([w](Vm&, const Value&, Args a) {
                for (const Value& v : a)
                    if (v.as<LiveWidget>()) w->extras["item_template"] = std::make_shared<script::Variable>(v);
                return Value{};
            });
        if (n == "setItemCount")
            return fn([r, w](Vm&, const Value&, Args a) {
                const int count = std::max(arg_int(a, a.size() > 1 ? 1 : 0), 0);
                w->set_property("item_count", Value::integer(count));
                w->items.clear();
                w->items.resize(size_t(count));
                w->items_in_view.clear();
                w->set_property("focus_index", Value::integer(std::clamp(int(w->data.number("focus_index")), 0, std::max(count - 1, 0))));
                arrange_list(*r, *w, true);
                return Value{};
            });
        if (n == "getItemCount") return fn([w](Vm&, const Value&, Args) { return Value::integer(list_count(*w)); });
        if (n == "getItemWidget") // (index[, context]): the row, made from the template when it is not there yet
            return fn([r, w](Vm&, const Value&, Args a) { return widget_value(list_item(*r, *w, arg_int(a, 0, -1), true)); });
        if (n == "setItemWidget") // (index, context, widget)
            return fn([r, w](Vm&, const Value&, Args a) {
                const int i = arg_int(a, 0, -1);
                if (i >= 0 && i < list_count(*w)) {
                    if (w->items.size() < size_t(list_count(*w))) w->items.resize(size_t(list_count(*w)));
                    w->items[size_t(i)] = a.size() > 2 ? a[2].as<LiveWidget>() : nullptr;
                    arrange_list(*r, *w, true);
                }
                return Value{};
            });
        if (n == "postRedraw")
            return fn([r, w](Vm&, const Value&, Args) {
                for (const W& row : w->items)
                    if (row) row->set_property("item_update", Value::integer(1));
                arrange_list(*r, *w, true);
                return Value{};
            });
        if (n == "incIndex" || n == "decIndex") {
            const int step = n == "incIndex" ? 1 : -1;
            return fn([r, w, step](Vm&, const Value&, Args) {
                const int before = int(w->data.number("focus_index"));
                list_focus(*r, *w, before + step, true);
                return Value::integer(int(w->data.number("focus_index")) != before);
            });
        }
        if (n == "focus_index") {
            auto a = std::make_shared<Accessor>();
            a->read = [w] { return Value::integer(int(w->data.number("focus_index"))); };
            a->write = [r, w](const Value& v) { list_focus(*r, *w, v.is_number() ? int(v.as_float()) : 0, false); };
            return a;
        }
        if (n == "update_callback" || n == "visible_callback" || n == "focus_callback") return w->extras[n] = std::make_shared<script::Variable>();
        // A row's flags: (index[, value]).
        struct Flag {
            const char* get;
            const char* set;
            const char* property;
        };
        if (n == "getItemVisible") // (index): the row is in view
            return fn([w](Vm&, const Value&, Args a) {
                const int i = arg_int(a, 0, -1);
                return Value::integer(i >= 0 && size_t(i) < w->items_in_view.size() && w->items_in_view[size_t(i)]);
            });
        for (const Flag& f : {Flag{"getItemVisible", "setItemVisible", "visible"}, Flag{"getItemUpdate", "setItemUpdate", "item_update"},
                              Flag{"getItemActive", "setItemActive", "active"}, Flag{"getItemInsensitive", "setItemInsensitive", "insensitive"}}) {
            if (n == f.get)
                return fn([r, w, f](Vm&, const Value&, Args a) {
                    const W row = list_item(*r, *w, arg_int(a, 0, -1), false);
                    return row ? row->get(f.property) : Value::integer(0);
                });
            if (n == f.set)
                return fn([r, w, f](Vm&, const Value&, Args a) {
                    if (const W row = list_item(*r, *w, arg_int(a, 0, -1), true)) row->set(f.property, Value::integer(arg_int(a, 1, 1)));
                    return Value{};
                });
        }
        if (n == "enterDragMode" || n == "leaveDragMode") return nothing();
    }

    if (type == "ScrollWindow" || type == "ScrollBox") {
        if (n == "updateGeometry")
            return fn([w](Vm&, const Value&, Args) {
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
            a->write = [w](const Value& v) { w->set_property("text_color", v); };
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
        // narrower to fit. Both are done when the text is drawn.
        if (n == "abbreviate")
            return fn([w](Vm&, const Value&, Args) {
                w->set_property("abbreviate", Value::integer(1));
                return Value{};
            });
        if (n == "adjustScale")
            return fn([w](Vm&, const Value&, Args) {
                w->set_property("adjust_scale", Value::integer(1));
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
                LiveWidget* page = w->page();
                Project* project = page ? r->project_of(*page) : nullptr;
                const std::string path = w->data.text("image_path");
                if (w->data.number("from_disk") != 0) {
                    // A picture of the disc (`piece/crs_logo/<course>.img`): its size as drawn.
                    std::string name = path;
                    while (!name.empty() && name[0] == '/') name.erase(0, 1);
                    try {
                        if (const std::optional<Bytes> data = name.empty() ? std::nullopt : r->reader()(name); data && gt3r::Tex1::is_tex1(*data)) {
                            const gt3r::Tex1 set = gt3r::Tex1::parse(*data);
                            if (!set.textures.empty()) {
                                gt3r::GsMemory gs;
                                set.upload(*data, gs);
                                const gt3r::Tex1::Image image = set.decode(gs, 0);
                                w->set_property("w", Value::number(float(image.width)));
                                w->set_property("h", Value::number(float(image.height)));
                            }
                        }
                    } catch (const std::exception&) {
                    }
                    return Value{};
                }
                if (const gt3r::Tex1::Image* image = project ? project->image(page->name, path) : nullptr) {
                    w->set_property("w", Value::number(float(image->width)));
                    w->set_property("h", Value::number(float(image->height)));
                }
                return Value{};
            });
        // setData(car, index): a paint chip takes the car's paint of that index.
        if (type == "ColorChipFace" && n == "setData")
            return fn([w](Vm& vm, const Value&, Args a) {
                if (a.size() < 2) return Value{};
                const Value colors = vm.call(vm.attribute(a[0], "getColors"), {});
                const std::vector<Value>* list = colors.items();
                const s32 i = arg_int(a, 1);
                if (!list || i < 0 || size_t(i) >= list->size()) return Value{};
                const Value& c = (*list)[size_t(i)];
                w->set_property("chip_r", Value::integer(s32(vm.attribute(c, "r").as_float() * 255.0f)));
                w->set_property("chip_g", Value::integer(s32(vm.attribute(c, "g").as_float() * 255.0f)));
                w->set_property("chip_b", Value::integer(s32(vm.attribute(c, "b").as_float() * 255.0f)));
                return Value{};
            });
        // setColor(words): the chip words of a paint (MQuickWork.getColorChipInfo); the first is
        // its colour, 0x00BBGGRR.
        if (type == "ColorChipFace" && n == "setColor")
            return fn([w](Vm&, const Value&, Args a) {
                const std::vector<Value>* words = a.empty() ? nullptr : a[0].items();
                if (!words || words->empty() || (*words)[0].kind != Kind::Int) return Value{};
                const u32 c = u32((*words)[0].i);
                w->set_property("chip_r", Value::integer(s32(c & 0xFF)));
                w->set_property("chip_g", Value::integer(s32((c >> 8) & 0xFF)));
                w->set_property("chip_b", Value::integer(s32((c >> 16) & 0xFF)));
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
        if (n == "setData" || n == "setColor" || n == "setColorIndex" || n == "syncLoading" || n == "loadModelSet" || n == "loadModelMotion" ||
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
        if (n == "getAlpha") return fn([w](Vm&, const Value&, Args) { return w->get("alpha"); });
        if (n == "setAlpha")
            return fn([w](Vm&, const Value&, Args a) {
                if (!a.empty()) w->set_property("alpha", a[0]);
                return Value{};
            });
        if (n == "getColor" || n == "setColor") return nothing();
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
        for (const char* m : {"reserveName", "refOther", "load", "unload"})
            if (n == m) return nothing();
    }
    if (type == "ModelFace") {
        for (const char* m : {"getModelSetPath", "setModelSetPath", "getEnvImagePath", "setEnvImagePath", "getModelMotionPath", "setModelMotionPath",
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
        // The text typed (`value`), at most `max_input_length` characters. The cursor is kept
        // at the end: left and right do nothing here.
        if (n == "putString")
            return fn([w](Vm&, const Value&, Args a) {
                std::string value = w->data.text("value") + (a.empty() ? std::string() : a[0].to_string());
                const size_t most = w->data.has("max_input_length") ? size_t(std::max(w->data.number("max_input_length"), 0.0f)) : value.size();
                if (value.size() > most) value.resize(most);
                w->set_property("value", Value::string(value));
                return Value{};
            });
        if (n == "backspace" || n == "delete")
            return fn([w](Vm&, const Value&, Args) {
                std::string value = w->data.text("value");
                if (!value.empty()) value.pop_back();
                w->set_property("value", Value::string(value));
                return Value{};
            });
        if (n == "left" || n == "right") return nothing();
    }
    // The boxes that lay their children out: adjustSize() and the automatic rows and columns.
    if (w->is_composite()) {
        if (n == "adjustSize") return nothing();
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
