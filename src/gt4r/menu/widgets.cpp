// The widgets as the scripts see them: the members the program's widget classes register
// (docs/formats/menu.md lists them per class) over a layout's properties.

#include "gt4r/menu/runtime.h"
#include "gt4r/menu/runtime_internal.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gt4r::menu {

using script::Args;
using script::Kind;
using script::Value;
using script::Vm;

namespace {

// An attribute of a widget that is one of its layout's properties.
struct PropertyRef : script::Reference {
    std::shared_ptr<LiveWidget> widget;
    std::string name;
    Value get(Vm&) override { return widget->get(name); }
    void set(Vm&, const Value& v) override { widget->set(name, v); }
    void clear() override { widget.reset(); }
};

// The attributes the program's widget classes register, whether or not the layout gives them
// (MWidget's, and each face's and box's own).
bool class_attribute(const std::string& n) {
    static const char* const names[] = {
        // MWidget
        "x", "y", "w", "h", "sx", "sy", "visible", "can_focus", "can_default", "active", "insensitive", "scale", "rotate", "translate", "skew",
        "opacity", "navigate_source", "navigate_target", "navigate_box", "tooltip_key", "tooltip", "packing", "pack_allocate_x", "pack_allocate_y",
        "pack_reverse", "pack_expand_x", "pack_expand_y", "pack_fill_x", "pack_fill_y", "minimum_width", "minimum_height",
        // MColorFace, MColorWindow
        "alpha",
        // MImageFace, MFlashFace
        "image_path", "image_chunk", "cache", "from_disk", "block", "texcoord_top", "texcoord_bottom", "texcoord_left", "texcoord_right",
        "flash_path", "repeat", "pause",
        // MTextFace, MToolTipFace
        "key", "value", "text", "color", "shadow_color", "align", "scale_x", "scale_y", "proportional", "aligned", "equalized", "multiline", "lock",
        "action_type",
        // MOptionMenu, MScaleBar, MSliderBar, the select boxes, MListBox
        "index", "current_value", "repeat_start", "min", "max", "display_min", "display_max", "range", "step", "popup", "cancel", "is_popuped",
        "focus_index", "drag_index", "step_min", "step_max", "interpolate_ratio", "magnify_ratio", "focus_active", "alignment", "h_justify",
        "v_justify", "justify", "update_callback", "visible_callback", "focus_callback",
        // MProgressFace, MInputNumberFace, MInputTextFace, MMovieFace, MModelFace, MCarFace, MColorChipFace, MBlurFace, MGraphFace, MVirtualFace
        "progress", "raw_mode", "final_width", "movie_name", "AC3", "audio_track", "interlace", "model_translate", "model_rotate", "rotate_v",
        "model_rotate_v", "target_mode", "car_code", "wheel", "wing", "car_color", "perfect_dark", "quality_level", "mirror", "car_model",
        "car_data", "car_garage", "stream", "debug_display", "image_path0", "image_path1", "scale_target", "type", "line_width", "points",
        "callback", "parameter", "pipe",
        // layout properties the scripts also write
        "text_color", "width", "height", "font"};
    for (const char* k : names)
        if (n == k) return true;
    return false;
}

std::string number_text(float v) {
    char b[32];
    if (v == std::floor(v) && std::fabs(v) < 1e9f) std::snprintf(b, sizeof b, "%d", int(v));
    else std::snprintf(b, sizeof b, "%g", double(v));
    return b;
}

Value number_value(const std::string& s) {
    if (s.find_first_of(".eE") == std::string::npos) return Value::integer(s32(std::strtol(s.c_str(), nullptr, 10)));
    return Value::number(std::strtof(s.c_str(), nullptr));
}

Property* find_property(Widget& w, const std::string& name) {
    for (Property& p : w.properties)
        if (p.name == name) return &p;
    return nullptr;
}

Property& property(Widget& w, const std::string& name, const char* type) {
    if (Property* p = find_property(w, name)) return *p;
    w.properties.push_back({});
    w.properties.back().name = name, w.properties.back().type = type;
    return w.properties.back();
}

int rect_index(const std::string& n) { return n == "x" ? 0 : n == "y" ? 1 : n == "w" || n == "width" ? 2 : n == "h" || n == "height" ? 3 : -1; }

bool one_of(const std::string& type, std::initializer_list<const char*> names) {
    for (const char* n : names)
        if (type == n) return true;
    return false;
}

} // namespace

bool is_select_box(const LiveWidget& w) { return w.is("SelectBox") || w.is("SelectBar"); }

// ---------------------------------------------------------------------------------------------
// Live widgets
// ---------------------------------------------------------------------------------------------

std::shared_ptr<LiveWidget> LiveWidget::build(const Widget& layout, const std::shared_ptr<script::Module>& parent, Runtime* runtime) {
    auto w = std::make_shared<LiveWidget>();
    w->data.type = layout.type;
    for (const Property& p : layout.properties)
        if (p.name != "children") w->data.properties.push_back(p);
    w->name = layout.name();
    w->parent = parent;
    w->runtime = runtime;
    if (layout.number("active") != 0) w->state |= kActive;
    if (layout.number("insensitive") != 0) w->state |= kInsensitive;
    if (parent && !w->name.empty()) parent->define(w->name, Value::of(Kind::Module, w));
    for (const Widget& c : layout.children()) w->children.push_back(build(c, w, runtime));
    // A text face's key is looked up in the page the layout has it in, wherever the scripts
    // move or copy the widget to afterwards (a header kept in a template page): the page's
    // name goes with it. ❓ When the program looks the text up is not read.
    if (layout.type == "RootWindow") {
        const std::string page = w->name;
        std::function<void(LiveWidget&)> mark = [&](LiveWidget& x) {
            if (x.data.has("key") && !x.data.has("key_page")) x.data.properties.push_back(Property{"key_page", "string", {page}, {}, {}, false});
            for (auto& c : x.children) mark(*c);
        };
        mark(*w);
    }
    if (is_select_box(*w)) {
        for (const auto& c : w->children)
            if (c->is_composite()) {
                if (const std::optional<Rect> g = c->data.rect("geometry")) w->item_w = g->w, w->item_h = g->h;
                break;
            }
        select_box_layout(*w, true);
    }
    return w;
}

void LiveWidget::clear() {
    auto held = std::move(children);
    children.clear();
    auto rows = std::move(list_items);
    list_items.clear();
    auto tmpl = std::move(list_template);
    list_template.reset();
    auto ex = std::move(extras);
    extras.clear();
    for (auto& [n, cell] : ex)
        if (cell) cell->clear();
    if (actor) actor->clear();
    actor.reset();
    Module::clear();
    for (auto& c : held) c->clear();
    for (auto& row : rows) {
        if (row.widget) row.widget->clear();
        if (row.drawn) row.drawn->clear();
    }
    if (tmpl) tmpl->clear();
}

std::shared_ptr<LiveWidget> LiveWidget::copy() const {
    auto w = std::make_shared<LiveWidget>();
    w->data = data, w->name = name, w->parent = parent, w->runtime = runtime, w->state = state & (kActive | kInsensitive);
    w->item_w = item_w, w->item_h = item_h;
    w->is_class = is_class, w->base = base, w->attributes = attributes;
    // The script's functions and values; the children are copied below.
    for (const auto& [n, cell] : members)
        if (!cell->value.as<LiveWidget>()) w->members[n] = std::make_shared<script::Variable>(cell->value);
    for (const auto& c : children) w->append(c->copy());
    return w;
}

void LiveWidget::append(const std::shared_ptr<LiveWidget>& child, const LiveWidget* before) {
    if (LiveWidget* old = child->parent_widget(); old && old != this) old->remove(child);
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
    child->parent = std::static_pointer_cast<script::Module>(shared_from_this());
    child->runtime = runtime;
    auto at = children.end();
    if (before)
        at = std::find_if(children.begin(), children.end(), [&](const auto& c) { return c.get() == before; });
    // A select box's rows are all the size of the first one it is given (the program's 0x2DB8B8).
    if (is_select_box(*this) && child->is_composite()) {
        bool first = true;
        for (const auto& c : children) first = first && !c->is_composite();
        if (first)
            if (const std::optional<Rect> g = child->data.rect("geometry")) item_w = g->w, item_h = g->h;
    }
    children.insert(at, child);
    if (!child->name.empty()) define(child->name, Value::of(Kind::Module, child));
    arrange();
}

void LiveWidget::remove(const std::shared_ptr<LiveWidget>& child) {
    const auto it = members.find(child->name);
    if (it != members.end() && it->second->value.p == child) members.erase(it);
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
    arrange();
}

void LiveWidget::remove_children() {
    for (const auto& c : children) {
        const auto it = members.find(c->name);
        if (it != members.end() && it->second->value.p == c) members.erase(it);
    }
    children.clear();
}

void LiveWidget::rename(const std::string& to) {
    const std::shared_ptr<LiveWidget> me = self();
    if (LiveWidget* up = parent_widget()) {
        const auto it = up->members.find(name);
        if (it != up->members.end() && it->second->value.p == me) up->members.erase(it);
        if (!to.empty()) up->define(to, Value::of(Kind::Module, me));
    }
    name = to;
    set_property("name", Value::string(to));
}

std::shared_ptr<LiveWidget> LiveWidget::child(const std::string& n) const {
    for (const auto& c : children)
        if (c->name == n) return c;
    return nullptr;
}

bool LiveWidget::shown() const {
    for (const LiveWidget* w = this; w; w = w->parent_widget())
        if (!w->visible()) return false;
    return true;
}

bool LiveWidget::is_composite() const {
    return !one_of(data.type, {"ImageFace", "TextFace", "ColorFace", "FrameImageFace", "ToolTipFace", "ColorChipFace", "ModelFace", "InputTextFace",
                               "InputNumberFace", "CarFace", "SceneViewFace", "MovieFace", "FlashFace", "PhotoRenderFace", "GraphFace", "SlideShowFace",
                               "ProgressFace", "EyetoyFace", "LoggerFace", "RaceCourseMapFace", "PhotoViewFace", "BlurFace", "VirtualFace"});
}

// The kinds that take the focus by themselves, and any widget the layout or a script says can.
bool LiveWidget::can_focus() const {
    if (data.has("can_focus")) return data.number("can_focus") != 0;
    return one_of(data.type, {"Button", "OptionMenu", "SliderBar", "ScaleBar", "InputTextFace", "InputNumberFace"});
}

// A box's children are placed anew after one is added or taken away: a select box's rows go
// to their places at once (the program's 0x2DA5D0).
void LiveWidget::arrange() {
    if (is_select_box(*this)) select_box_layout(*this, true);
}

Value LiveWidget::get(const std::string& n) const {
    if (const int i = rect_index(n); i >= 0) {
        std::optional<Rect> r = data.rect("geometry");
        if (!r && i >= 2) // it fills its parent
            for (const LiveWidget* up = parent_widget(); up && !r; up = up->parent_widget())
                if (const std::optional<Rect> g = up->data.rect("geometry")) r = Rect{0, 0, g->w, g->h};
        const float v[4] = {r ? r->x : 0, r ? r->y : 0, r ? r->w : 0, r ? r->h : 0};
        return Value::number(v[i]);
    }
    if (n == "active") return Value::integer((state & kActive) != 0);
    if (n == "insensitive") return Value::integer((state & kInsensitive) != 0);
    if (n == "can_focus") return Value::integer(can_focus());
    const Property* p = data.find(n);
    if (!p || p->values.empty()) {
        if (n == "visible") return Value::integer(1);
        if (n == "alpha" || n == "opacity" || n == "scale_x" || n == "scale_y" || n == "sx" || n == "sy") return Value::number(1);
        if (n == "text" || n == "key" || n == "image_path" || n == "movie_name" || n == "car_code" || n == "tooltip_key") return Value::string({});
        if (n == "value" && (is("TextFace") || is("ToolTipFace") || is("InputTextFace"))) {
            // A text face without a value gives its key: Prologue's car page keeps the formats of
            // its figures there ("%Cfff%d %C222%s") and hands `face.value` to getCatPs and the
            // others. ❓ From the scripts' use.
            if (const Property* key = data.find("key"); key && !key->values.empty() && key->type == "string") return Value::string(key->values[0]);
            return Value::string({});
        }
        return Value::integer(0);
    }
    if (p->type == "string") return Value::string(p->values[0]);
    if (p->values.size() == 1) return number_value(p->values[0]);
    std::vector<Value> out;
    for (const std::string& s : p->values) out.push_back(number_value(s));
    return Value::array(std::move(out));
}

void LiveWidget::set(const std::string& n, const Value& v) {
    if (n == "active" || n == "insensitive") {
        const int bit = n == "active" ? kActive : kInsensitive;
        state = v.truthy() ? state | bit : state & ~bit;
        set_property(n, Value::integer(v.truthy()));
        return;
    }
    if (n == "index" && runtime) {
        widget_set_index(*runtime, *this, v.is_number() ? int(v.as_float()) : 0, false);
        return;
    }
    set_property(n, v);
    if (n == "index" && is_select_box(*this)) select_box_layout(*this, true);
    // A text given directly replaces the one looked up by key.
    if (n == "text" && v.is_nil()) set_property("text", Value::string({}));
}

void LiveWidget::set_property(const std::string& n, const Value& v) {
    if (const int i = rect_index(n); i >= 0) {
        if (!data.has("geometry")) {
            // Until now it filled its parent: it keeps that size.
            float pw = 640, ph = 480;
            for (const LiveWidget* up = parent_widget(); up; up = up->parent_widget())
                if (const std::optional<Rect> r = up->data.rect("geometry")) {
                    pw = r->w, ph = r->h;
                    break;
                }
            Property& made = property(data, "geometry", "rectangle");
            made.type = "rectangle";
            made.values = {"0", "0", number_text(pw), number_text(ph)};
        }
        Property& g = property(data, "geometry", "rectangle");
        g.values.resize(4, "0");
        g.values[size_t(i)] = number_text(v.as_float());
        return;
    }
    if (v.kind == Kind::String) {
        Property& p = property(data, n, "string");
        p.type = "string", p.values = {v.as_string()}, p.is_list = false, p.items.clear();
    } else if (v.is_number()) {
        Property& p = property(data, n, "digit");
        p.type = "digit", p.values = {v.kind == Kind::Int ? std::to_string(v.i) : number_text(v.f)}, p.is_list = false, p.items.clear();
    } else if (const std::vector<Value>* list = v.items()) {
        Property& p = property(data, n, "vector");
        p.values.clear();
        for (const Value& item : *list) {
            // A list of pairs (a graph face's points) is kept as its numbers in order.
            if (const std::vector<Value>* pair = item.items())
                for (const Value& v2 : *pair) p.values.push_back(number_text(v2.is_number() ? v2.as_float() : 0.0f));
            else p.values.push_back(number_text(item.is_number() ? item.as_float() : 0.0f));
        }
    } else if (v.is_nil()) {
        if (Property* p = find_property(data, n)) p->values.clear();
    }
}

LiveWidget* LiveWidget::page() {
    LiveWidget* w = this;
    while (w && w->data.type != "RootWindow") w = w->parent_widget();
    return w;
}

LiveWidget* LiveWidget::project() {
    LiveWidget* w = this;
    while (w && w->data.type != "Project") w = w->parent_widget();
    return w;
}

Rect LiveWidget::screen_rect() const {
    // A widget without a rectangle of its own fills its parent; the page is the screen.
    const LiveWidget* up = parent_widget();
    Rect r = up && up->data.type != "Project" ? up->screen_rect() : Rect{0, 0, 640, 480};
    if (const std::optional<Rect> g = data.rect("geometry")) r = {r.x + g->x, r.y + g->y, g->w, g->h};
    return r;
}

// ---------------------------------------------------------------------------------------------
// What the scripts call on a widget
// ---------------------------------------------------------------------------------------------

namespace {

using W = std::shared_ptr<LiveWidget>;

std::shared_ptr<script::Variable> constant(Value v) { return std::make_shared<script::Variable>(std::move(v)); }

W sibling(const W& w, int step, bool windows_only) {
    const LiveWidget* up = w->parent_widget();
    if (!up) return nullptr;
    const auto it = std::find(up->children.begin(), up->children.end(), w);
    if (it == up->children.end()) return nullptr;
    for (auto at = it - up->children.begin() + step; at >= 0 && at < std::ptrdiff_t(up->children.size()); at += step)
        if (!windows_only || up->children[size_t(at)]->is_composite()) return up->children[size_t(at)];
    return nullptr;
}

W edge_child(const W& w, bool first, bool windows_only) {
    if (first) {
        for (const auto& c : w->children)
            if (!windows_only || c->is_composite()) return c;
    } else {
        for (auto it = w->children.rbegin(); it != w->children.rend(); ++it)
            if (!windows_only || (*it)->is_composite()) return *it;
    }
    return nullptr;
}

// The widget argument of appendChild(context, widget) / appendChild(widget), and the like.
W widget_arg(Args a, size_t skip = 0) {
    for (size_t i = 0; i < a.size(); ++i)
        if (auto w = a[i].as<LiveWidget>()) {
            if (skip == 0) return w;
            --skip;
        }
    return nullptr;
}

// MWidget's and MComposite's members (the program's tables at 0x258670 and 0x205C80).
std::shared_ptr<script::Reference> common_member(const W& w, const std::string& n) {
    auto fn = [&](script::NativeFunction f) { return constant(Vm::native(n, std::move(f))); };
    if (n == "next") return constant(widget_value(sibling(w, 1, false)));
    if (n == "prev") return constant(widget_value(sibling(w, -1, false)));
    if (n == "next_window") return constant(widget_value(sibling(w, 1, true)));
    if (n == "prev_window") return constant(widget_value(sibling(w, -1, true)));
    if (n == "is_composite") return constant(Value::integer(w->is_composite()));
    if (n == "is_face") return constant(Value::integer(!w->is_composite()));
    if (n == "context" || n == "window_context") return constant(w->runtime ? w->runtime->context() : Value{});
    if (n == "actor") {
        auto a = std::make_shared<Accessor>();
        std::weak_ptr<LiveWidget> weak = w;
        a->read = [weak] {
            const W s = weak.lock();
            return s && s->actor ? object_value(s->actor) : Value{};
        };
        a->write = [weak](const Value& v) {
            if (const W s = weak.lock()) attach_actor(*s, v);
        };
        return a;
    }
    if (n == "getActor") return fn([w](Vm&, const Value&, Args) { return w->actor ? object_value(w->actor) : Value{}; });
    if (n == "setActor")
        return fn([w](Vm&, const Value&, Args a) {
            attach_actor(*w, a.empty() ? Value{} : a[0]);
            return Value{};
        });
    // setColor(color) on a picture or text face: its tint, an MColorObject's r, g, b, a (0..1).
    // (Prologue's paint page tints its color balls so: chip["base"].setColor(car.getColors()[i]).)
    if (n == "setColor" && (w->is("ImageFace") || w->is("TextFace")))
        return fn([w](Vm& vm, const Value&, Args a) {
            if (a.empty() || a[0].kind != Kind::Object) return Value{};
            auto part = [&](const char* c, float otherwise) {
                const Value v = vm.attribute(a[0], c);
                return v.is_number() ? std::clamp(v.as_float(), 0.0f, 1.0f) : otherwise;
            };
            Property& p = property(w->data, "color", "");
            Property rgba;
            rgba.type = "RGBA";
            for (float f : {part("r", 1), part("g", 1), part("b", 1), part("a", 1)}) rgba.values.push_back(std::to_string(int(std::lround(f * 255.0f))));
            p.type.clear(), p.values.clear(), p.widgets.clear(), p.is_list = true, p.items = {std::move(rgba)};
            return Value{};
        });
    // (Prologue's scripts name it getDeepCopy.)
    if (n == "doCopy" || n == "getDeepCopy") return fn([w](Vm&, const Value&, Args) { return widget_value(w->copy()); });
    // Reads its properties again (a picture's path changed): nothing is kept here that needs it.
    if (n == "doInitialize") return fn([](Vm&, const Value&, Args) { return Value{}; });
    // interpolateX(target, ratio): moves a ratio of the way; within one unit it arrives and
    // gives 1 (the program's 0x25BFE0).
    if (n == "interpolateX" || n == "interpolateY") {
        const std::string axis = n == "interpolateX" ? "x" : "y";
        return fn([w, axis](Vm&, const Value&, Args a) {
            const float target = arg_number(a, 0), ratio = arg_number(a, 1, 1);
            float v = w->get(axis).as_float();
            const bool there = interpolate(v, target, ratio);
            w->set_property(axis, Value::number(v));
            return Value::integer(there);
        });
    }
    if (!w->is_composite()) return nullptr;
    if (n == "first") return constant(widget_value(edge_child(w, true, false)));
    if (n == "last") return constant(widget_value(edge_child(w, false, false)));
    if (n == "first_window") return constant(widget_value(edge_child(w, true, true)));
    if (n == "last_window") return constant(widget_value(edge_child(w, false, true)));
    if (n == "appendChild") // (context, widget) or (widget)
        return fn([w](Vm&, const Value&, Args a) {
            if (const W c = widget_arg(a)) w->append(c);
            return Value{};
        });
    if (n == "insertChild") // (widget, before) or (context, widget, before)
        return fn([w](Vm&, const Value&, Args a) {
            const W c = widget_arg(a), before = widget_arg(a, 1);
            if (c) w->append(c, before.get());
            return Value{};
        });
    if (n == "removeChild")
        return fn([w](Vm&, const Value&, Args a) {
            if (const W c = widget_arg(a)) w->remove(c);
            return Value{};
        });
    if (n == "replaceChild") // (new, old)
        return fn([w](Vm&, const Value&, Args a) {
            const W c = widget_arg(a), old = widget_arg(a, 1);
            if (c && old) w->append(c, old.get()), w->remove(old);
            return Value{};
        });
    if (n == "clearChildren" || n == "clearWindow")
        return fn([w, n](Vm&, const Value&, Args) {
            if (n == "clearChildren") w->remove_children();
            else {
                // The windows go; the faces (the box's own pictures) stay.
                std::vector<W> gone;
                for (const auto& c : w->children)
                    if (c->is_composite()) gone.push_back(c);
                for (const auto& c : gone) w->remove(c);
            }
            if (w->runtime) widget_children_changed(*w->runtime, *w);
            return Value{};
        });
    if (n == "countChild") return fn([w](Vm&, const Value&, Args) { return Value::integer(s32(w->children.size())); });
    if (n == "getChild")
        return fn([w](Vm&, const Value&, Args a) {
            const s32 i = arg_int(a, 0, -1);
            return i >= 0 && size_t(i) < w->children.size() ? widget_value(w->children[size_t(i)]) : Value{};
        });
    if (n == "movetoLast")
        return fn([w](Vm&, const Value&, Args a) {
            if (const W c = widget_arg(a)) w->append(c);
            return Value{};
        });
    return nullptr;
}

} // namespace

std::shared_ptr<script::Reference> LiveWidget::member(Vm& vm, const Value& self_value, const std::string& n) {
    if (std::shared_ptr<script::Variable> cell = find(n)) return cell; // script functions, children, statics
    if (const auto it = extras.find(n); it != extras.end()) return it->second;
    const W me = self();
    if (n == "name") {
        auto a = std::make_shared<Accessor>();
        std::weak_ptr<LiveWidget> weak = me;
        a->read = [weak] {
            const W s = weak.lock();
            return Value::string(s ? s->name : std::string());
        };
        a->write = [weak](const Value& v) {
            if (const W s = weak.lock()) s->rename(v.to_string());
        };
        return a;
    }
    if (std::shared_ptr<script::Reference> r = common_member(me, n)) return r;
    if (runtime)
        if (std::shared_ptr<script::Reference> r = widget_class_member(*runtime, me, n)) return r;
    if (data.has(n) || class_attribute(n)) {
        auto ref = std::make_shared<PropertyRef>();
        ref->widget = me, ref->name = n;
        return ref;
    }
    if (std::shared_ptr<script::Reference> r = vm.builtin_member(self_value, n)) return r;
    // Anything else of the program's is a stand-in, kept so a value assigned to it stays.
    return extras[n] = std::make_shared<script::Variable>(vm.stub_natives && !by_name_ ? vm.make_stub(data.type + "." + n) : Value{});
}

// widget["label"]: a child or an attribute by name; nil when the widget has nothing of that
// name (the scripts ask `w["label"] != nil` of every widget of a page), and a value assigned
// to it stays.
std::shared_ptr<script::Reference> LiveWidget::element(Vm& vm, const Value& self_value, const std::string& n) {
    const bool was = by_name_;
    by_name_ = true;
    std::shared_ptr<script::Reference> r = member(vm, self_value, n);
    by_name_ = was;
    return r;
}

} // namespace gt4r::menu
