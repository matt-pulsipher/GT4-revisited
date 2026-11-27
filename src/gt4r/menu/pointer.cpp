// A mouse or a finger on the game's pages (pointer.h).

#include "gt4r/menu/pointer.h"

#include "gt4r/menu/runtime_internal.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace gt4r::menu {

namespace {

using W = std::shared_ptr<LiveWidget>;

bool inside(const Rect& r, float x, float y) { return r.w > 0 && r.h > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

// The rows of a box that chooses (a select box's, an option menu's list's): its children that are windows.
std::vector<W> rows_of(const LiveWidget& box) {
    std::vector<W> out;
    for (const auto& c : box.children)
        if (c->is_composite()) out.push_back(c);
    return out;
}
int row_in(const LiveWidget& box, const LiveWidget& w) {
    const std::vector<W> rows = rows_of(box);
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].get() == &w) return int(i);
    return -1;
}
bool popup_open(const LiveWidget& w) { return w.data.number("is_popuped") != 0; }
// The option menu whose open list `w` is a row of (popup::vbox's child).
LiveWidget* open_option_menu_of(const LiveWidget& w) {
    const LiveWidget* vbox = w.parent_widget();
    const LiveWidget* popup = vbox ? vbox->parent_widget() : nullptr;
    LiveWidget* menu = popup ? popup->parent_widget() : nullptr;
    return vbox && vbox->name == "vbox" && popup && popup->name == "popup" && menu && menu->is("OptionMenu") && popup_open(*menu) ? menu : nullptr;
}
bool vertical_list(const LiveWidget& box) { return box.data.number("direction") != 0; } // a list box's and a select box's 1: top to bottom

// The scripts take keys (the context's event mask, Runtime::key) and no transition covers the page.
bool taking(Runtime& rt) {
    const script::Value mask = rt.vm().attribute(rt.context(), "event_mask");
    if (mask.is_number() && mask.as_float() == 0) return false;
    return rt.fade_cover() <= 0 && rt.current_page() != nullptr;
}

// The focused bar whose popup is open: every key is its own until it closes.
LiveWidget* open_bar(Runtime& rt) {
    LiveWidget* page = rt.current_page();
    const W focus = page ? page->focus.lock() : nullptr;
    return focus && (focus->is("SliderBar") || focus->is("ScaleBar")) && popup_open(*focus) ? focus.get() : nullptr;
}

// The option menu of the page whose list is open: then it alone takes the pointer.
LiveWidget* open_option_menu(const std::vector<FocusCandidate>& candidates) {
    for (const FocusCandidate& c : candidates)
        if (LiveWidget* m = open_option_menu_of(*c.widget)) return m;
    return nullptr;
}

PointerHit hit_in(Runtime& rt, const std::vector<DrawnBox>& drawn, const std::vector<FocusCandidate>& candidates, float x, float y) {
    PointerHit out;
    LiveWidget* page = rt.current_page();
    if (!page) return out;
    std::unordered_set<const LiveWidget*> can;
    for (const FocusCandidate& c : candidates) can.insert(c.widget.get());
    LiveWidget* open_menu = open_option_menu(candidates);
    for (auto it = drawn.rbegin(); it != drawn.rend(); ++it) {
        if (!it->widget || !inside(it->box, x, y)) continue;
        const std::shared_ptr<const script::Heap> alive = it->alive.lock(); // (a widget gone since it was drawn: none)
        if (!alive) continue;
        // What was drawn, then each parent up to the page: the first that is a target.
        for (const LiveWidget* w = it->widget; w && w != page; w = w->parent_widget()) {
            LiveWidget* up = w->parent_widget();
            if (!up) break;
            if (LiveWidget* m = open_option_menu_of(*w)) {
                if (m != open_menu) break;
                out.kind = PointerHit::Kind::OptionRow, out.box = m->self(), out.row = row_in(*up, *w);
                out.widget = const_cast<LiveWidget*>(w)->self();
                return out.row >= 0 && !w->insensitive() ? out : PointerHit{};
            }
            if (open_menu) continue; // (the open list alone)
            if ((w->name == "dec_arrow" || w->name == "inc_arrow") && up->is("ScrollBox")) {
                // A scroll box's arrow: a step of the list (or select box) in it, when that takes the focus.
                for (const auto& c : up->children) {
                    bool reachable = c->is("ListBox") && can.count(c.get());
                    if (is_select_box(*c)) {
                        const std::vector<W> rows = rows_of(*c);
                        reachable = !rows.empty() && can.count(rows[size_t(std::clamp(c->index(), 0, int(rows.size()) - 1))].get());
                    }
                    if (!reachable) continue;
                    out.kind = PointerHit::Kind::Arrow, out.box = c, out.row = w->name == "dec_arrow" ? -1 : 1;
                    out.widget = const_cast<LiveWidget*>(w)->self();
                    return out;
                }
                break;
            }
            if (is_select_box(*up) && w->is_composite()) {
                // A select box's rows: its chosen row is the candidate; the box is one when that is.
                const std::vector<W> rows = rows_of(*up);
                const bool reachable = !rows.empty() && can.count(rows[size_t(std::clamp(up->index(), 0, int(rows.size()) - 1))].get());
                if (!reachable) continue;
                out.kind = PointerHit::Kind::SelectRow, out.box = up->self(), out.row = row_in(*up, *w);
                out.widget = const_cast<LiveWidget*>(w)->self();
                return out;
            }
            if (up->is("ListBox") && can.count(up)) {
                for (size_t i = 0; i < up->list_items.size(); ++i) {
                    const LiveWidget::ListItem& item = up->list_items[i];
                    if (item.widget.get() != w && item.drawn.get() != w) continue;
                    if (item.insensitive) return PointerHit{};
                    out.kind = PointerHit::Kind::ListRow, out.box = up->self(), out.row = int(i);
                    out.widget = const_cast<LiveWidget*>(w)->self();
                    return out;
                }
                continue;
            }
            if (can.count(w)) {
                out.kind = PointerHit::Kind::Widget, out.widget = const_cast<LiveWidget*>(w)->self();
                return out;
            }
        }
    }
    return out;
}

// A list box fits its box (its rows do not scroll), a select box shows every row: a hover may move its choice without
// the rows moving under the pointer.
bool fits(const LiveWidget& box) {
    const std::optional<Rect> g = box.data.rect("geometry");
    if (!g) return false;
    const bool vertical = vertical_list(box);
    const float extent = vertical ? g->h : g->w;
    if (box.is("ListBox")) {
        const float pitch = (vertical ? box.item_h : box.item_w) + box.data.number("spacing");
        return pitch * float(box.list_items.size()) <= extent + 0.5f;
    }
    const float pitch = (vertical ? box.item_h : box.item_w) + box.data.number("spacing");
    return pitch * float(rows_of(box).size()) <= extent + 0.5f;
}

// The focus on `w` as a move of the pad's makes it: the page's navigate sound.
void focus_on(Runtime& rt, const W& w) {
    LiveWidget* page = rt.current_page();
    if (!page || !w || page->focus.lock() == w) return;
    rt.set_focus(*page, w);
    rt.navigate_sound(*page);
}

// A box's choice moved to a row: true when it moved (the move's sound and the scripts as its keys have them).
bool choose_row(Runtime& rt, const PointerHit& h) {
    LiveWidget* page = rt.current_page();
    if (!page || !h.box) return false;
    LiveWidget& box = *h.box;
    if (h.kind == PointerHit::Kind::SelectRow) {
        const std::vector<W> rows = rows_of(box);
        if (h.row < 0 || h.row >= int(rows.size())) return false;
        const bool moved = box.index() != h.row;
        if (moved) widget_set_index(rt, box, h.row, true); // (its sound and onValueChanged)
        if (page->focus.lock() != rows[size_t(h.row)]) {
            rt.set_focus(*page, rows[size_t(h.row)]);
            if (!moved) rt.navigate_sound(*page);
        }
        return true;
    }
    if (h.kind == PointerHit::Kind::ListRow) {
        const int was = box.data.has("focus_index") ? int(box.data.number("focus_index")) : -1;
        if (was == h.row && page->focus.lock() == h.box) return false;
        list_set_focus(box, h.row);
        if (page->focus.lock() != h.box) rt.set_focus(*page, h.box);
        rt.navigate_sound(*page);
        return true;
    }
    if (h.kind == PointerHit::Kind::OptionRow) return option_point(rt, box, h.row);
    return false;
}

// Whether the hit is what has the focus now (a tap on it is Accept).
bool is_focus(Runtime& rt, const PointerHit& h) {
    LiveWidget* page = rt.current_page();
    const W focus = page ? page->focus.lock() : nullptr;
    if (!focus) return false;
    switch (h.kind) {
    case PointerHit::Kind::Widget: return focus == h.widget;
    case PointerHit::Kind::SelectRow: return focus == h.widget && h.box->index() == h.row;
    case PointerHit::Kind::ListRow: return focus == h.box && h.box->data.has("focus_index") && int(h.box->data.number("focus_index")) == h.row;
    case PointerHit::Kind::OptionRow: return focus == h.widget && h.box->index() == h.row;
    default: return false;
    }
}

// The keys back and on along a list or a select box.
std::pair<Key, Key> keys_along(const LiveWidget& box) {
    return vertical_list(box) ? std::pair{Key::Up, Key::Down} : std::pair{Key::Left, Key::Right};
}

// What a wheel or a drag moves: the list or select box at the point, else (`or_focus`) the one with the focus.
W list_at(Runtime& rt, const std::vector<DrawnBox>& drawn, float x, float y, bool or_focus) {
    const std::vector<FocusCandidate> candidates = focus_candidates(*rt.current_page(), false);
    if (open_option_menu(candidates)) return nullptr;
    const PointerHit h = hit_in(rt, drawn, candidates, x, y);
    if (h.kind == PointerHit::Kind::SelectRow || h.kind == PointerHit::Kind::ListRow) return h.box;
    if (h.kind == PointerHit::Kind::Widget || !or_focus) return nullptr; // (a widget of its own: nothing to scroll)
    const W focus = rt.current_page()->focus.lock();
    if (!focus) return nullptr;
    if (focus->is("ListBox")) return focus;
    if (LiveWidget* up = focus->parent_widget(); up && is_select_box(*up)) return up->self();
    return nullptr;
}

// The list's focus on it, then `n` presses of a key along it.
void step_list(Runtime& rt, const W& box, int n) {
    LiveWidget* page = rt.current_page();
    if (!page || !box || n == 0) return;
    W focus = box;
    if (is_select_box(*box)) {
        const std::vector<W> rows = rows_of(*box);
        if (rows.empty()) return;
        focus = rows[size_t(std::clamp(box->index(), 0, int(rows.size()) - 1))];
    }
    if (page->focus.lock() != focus) rt.set_focus(*page, focus);
    const auto [back, on] = keys_along(*box);
    for (int i = 0; i < std::min(std::abs(n), 8); ++i) rt.key(n < 0 ? back : on);
}

// The pitch of a list's rows (a row and the spacing), along it.
float pitch_of(const LiveWidget& box) {
    const bool vertical = vertical_list(box);
    float item = vertical ? box.item_h : box.item_w;
    if (is_select_box(box)) {
        const std::vector<W> rows = rows_of(box);
        if (!rows.empty())
            if (const std::optional<Rect> g = rows[0]->data.rect("geometry")) item = vertical ? g->h : g->w;
    }
    return std::max(item + box.data.number("spacing"), 8.0f);
}

} // namespace

PointerHit pointer_hit(Runtime& rt, const std::vector<DrawnBox>& drawn, float x, float y) {
    LiveWidget* page = rt.current_page();
    if (!page) return {};
    return hit_in(rt, drawn, focus_candidates(*page, false), x, y);
}

bool pointer_hover(Runtime& rt, const std::vector<DrawnBox>& drawn, float x, float y) {
    if (!taking(rt) || open_bar(rt)) return false;
    const PointerHit h = pointer_hit(rt, drawn, x, y);
    switch (h.kind) {
    case PointerHit::Kind::Widget:
        // (A scroll window's content follows the focus: it would move under a pointer that stays.)
        for (const LiveWidget* up = h.widget->parent_widget(); up; up = up->parent_widget())
            if (up->is("ScrollWindow") || up->is("ScrollBox")) return false;
        focus_on(rt, h.widget);
        return true;
    case PointerHit::Kind::SelectRow:
    case PointerHit::Kind::ListRow:
        // (Rows that scroll would move under a pointer that stays: only the list's own focus then.)
        if (is_focus(rt, h)) return true;
        if (fits(*h.box)) return choose_row(rt, h);
        return false;
    case PointerHit::Kind::OptionRow: return choose_row(rt, h);
    default: return false;
    }
}

bool pointer_tap(Runtime& rt, const std::vector<DrawnBox>& drawn, float x, float y) {
    if (!taking(rt)) return false;
    LiveWidget* page = rt.current_page();
    const std::vector<FocusCandidate> candidates = focus_candidates(*page, false);
    const PointerHit h = hit_in(rt, drawn, candidates, x, y);
    // Off a bar whose popup is open: Accept, keeping its value.
    if (LiveWidget* bar = open_bar(rt); bar && (h.kind != PointerHit::Kind::Widget || h.widget.get() != bar)) return rt.key(Key::Accept), true;
    if (h.kind == PointerHit::Kind::None) {
        // Off an open option list: Cancel closes it. A page with nothing to choose ("press a button"): Accept.
        if (open_option_menu(candidates)) return rt.key(Key::Cancel), true;
        if (candidates.empty() && !page->focus.lock()) return rt.key(Key::Accept), true;
        return false;
    }
    if (h.kind == PointerHit::Kind::Arrow) return step_list(rt, h.box, h.row), true;
    if (!is_focus(rt, h)) {
        if (h.kind == PointerHit::Kind::Widget) focus_on(rt, h.widget);
        else choose_row(rt, h);
        return true;
    }
    // On what has the focus: a bar moves toward the tap (its keys along it) while its value changes by them, else Accept.
    const LiveWidget& w = *h.widget;
    if (h.kind == PointerHit::Kind::Widget && (w.is("SliderBar") || w.is("ScaleBar"))) {
        const bool popup_mode = w.is("SliderBar") ? w.data.number("popup") != 0 : w.child("popup") != nullptr;
        if (!popup_mode || popup_open(w)) {
            const bool along_x = w.is("SliderBar") ? w.data.number("direction") == 0 : w.data.number("horizontal") != 0;
            const W popup = popup_mode ? w.child("popup") : nullptr;
            const W pinch = popup ? popup->child("pinch") : w.child("pinch");
            const Rect at = pinch ? pinch->screen_rect() : w.screen_rect();
            const float mid = along_x ? at.x + at.w / 2 : at.y + at.h / 2, p = along_x ? x : y;
            const float half = std::max(along_x ? at.w / 2 : at.h / 2, 4.0f);
            if (std::fabs(p - mid) > half) {
                rt.key(along_x ? (p < mid ? Key::Left : Key::Right) : (p < mid ? Key::Up : Key::Down));
                return true;
            }
            if (!popup_mode) return true; // (on the pinch: nothing to move)
        }
    }
    rt.key(Key::Accept);
    return true;
}

bool pointer_wheel(Runtime& rt, const std::vector<DrawnBox>& drawn, float x, float y, float notches) {
    if (!taking(rt) || notches == 0 || open_bar(rt)) return false;
    const W box = list_at(rt, drawn, x, y, true);
    if (!box) return false;
    const int n = int(std::lround(notches));
    step_list(rt, box, n != 0 ? -n : (notches > 0 ? -1 : 1));
    return true;
}

bool pointer_drag(Runtime& rt, const std::vector<DrawnBox>& drawn, PointerDrag& drag, float x0, float y0, float x, float y, bool end) {
    if (!drag.started) {
        drag = {};
        drag.started = true;
        if (taking(rt)) drag.box = list_at(rt, drawn, x0, y0, false);
        drag.left = 0;
        drag.x = x0, drag.y = y0;
    }
    const W box = drag.box.lock();
    bool used = false;
    if (box && taking(rt)) {
        const bool vertical = vertical_list(*box);
        // The content follows the finger: moved up (or left) by a row's pitch, the choice goes a row on.
        drag.left += vertical ? drag.y - y : drag.x - x;
        const float pitch = pitch_of(*box);
        const int steps = int(drag.left / pitch);
        if (steps != 0) step_list(rt, box, steps), drag.left -= float(steps) * pitch;
        used = true;
    }
    drag.x = x, drag.y = y;
    if (end) drag = {};
    return used;
}

void pointer_back(Runtime& rt) {
    if (taking(rt)) rt.key(Key::Cancel);
}

} // namespace gt4r::menu
