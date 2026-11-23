#include "gt4r/menu/runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace gt4r::menu {

using script::Args;
using script::Kind;
using script::Value;
using script::Vm;

namespace {

constexpr size_t kMaxLog = 200;
constexpr int kMaxEventsPerFrame = 64;
constexpr u64 kCallBudget = 20'000'000; // instructions one call of a script may run

// An object of the program's with attributes the scripts read and write: the ones written
// here are real, any other is a stand-in made when first asked for.
struct Bag : script::Heap {
    std::string label;
    std::map<std::string, std::shared_ptr<script::Variable>> fields;

    explicit Bag(std::string l) : label(std::move(l)) {}
    std::shared_ptr<script::Reference> member(Vm& vm, const Value&, const std::string& name) override {
        std::shared_ptr<script::Variable>& cell = fields[name];
        if (!cell) cell = std::make_shared<script::Variable>(vm.stub_natives ? vm.make_stub(label + "." + name) : Value{});
        return cell;
    }
    std::string describe() const override { return "<" + label + ">"; }
    void clear() override {
        auto held = std::move(fields);
        fields.clear();
        for (auto& [n, cell] : held)
            if (cell) cell->clear();
    }
    void set(const std::string& name, Value v) { fields[name] = std::make_shared<script::Variable>(std::move(v)); }
    void method(const std::string& name, script::NativeFunction fn) { set(name, Vm::native(label + "." + name, std::move(fn))); }
    Value get(const std::string& name) const {
        const auto it = fields.find(name);
        return it == fields.end() ? Value{} : it->second->value;
    }
};

std::shared_ptr<Bag> make_bag(const std::string& label) { return std::make_shared<Bag>(label); }
Value object(const std::shared_ptr<script::Heap>& p) { return Value::of(Kind::Object, p); }

// An attribute of a widget that is one of its layout's properties.
struct PropertyRef : script::Reference {
    std::shared_ptr<LiveWidget> widget;
    std::string name;
    Value get(Vm&) override { return widget->get(name); }
    void set(Vm&, const Value& v) override { widget->set(name, v); }
};

// The properties scripts read and write whether or not the layout gives them.
bool common_property(const std::string& n) {
    static const char* const names[] = {"x", "y", "w", "h", "width", "height", "visible", "text", "alpha", "opacity", "active", "insensitive",
                                        "can_focus", "image_path", "key", "scale_x", "scale_y", "rotate", "text_color", "color", "index"};
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

} // namespace

// ---------------------------------------------------------------------------------------------
// Live widgets
// ---------------------------------------------------------------------------------------------

std::shared_ptr<LiveWidget> LiveWidget::build(const Widget& layout, const std::shared_ptr<script::Module>& parent) {
    auto w = std::make_shared<LiveWidget>();
    w->data.type = layout.type;
    for (const Property& p : layout.properties)
        if (p.name != "children") w->data.properties.push_back(p);
    w->name = layout.name();
    w->parent = parent;
    if (auto up = std::dynamic_pointer_cast<LiveWidget>(parent)) w->runtime = up->runtime;
    if (parent) parent->define(w->name, Value::of(Kind::Module, w));
    for (const Widget& c : layout.children()) w->children.push_back(build(c, w));
    return w;
}

void LiveWidget::clear() {
    auto held = std::move(children);
    children.clear();
    auto objs = std::move(objects);
    objects.clear();
    for (auto& [n, cell] : objs)
        if (cell) cell->clear();
    Module::clear();
    for (auto& c : held) c->clear();
}

std::shared_ptr<LiveWidget> LiveWidget::copy() const {
    auto w = std::make_shared<LiveWidget>();
    w->data = data, w->name = name, w->parent = parent, w->runtime = runtime;
    w->is_class = is_class, w->base = base, w->attributes = attributes;
    // The script's functions and values; the children are copied below.
    for (const auto& [n, cell] : members)
        if (!cell->value.as<LiveWidget>()) w->members[n] = std::make_shared<script::Variable>(cell->value);
    for (const auto& c : children) w->append(c->copy());
    return w;
}

void LiveWidget::append(const std::shared_ptr<LiveWidget>& child) {
    if (LiveWidget* old = child->parent_widget(); old && old != this)
        old->children.erase(std::remove(old->children.begin(), old->children.end(), child), old->children.end());
    child->parent = std::static_pointer_cast<script::Module>(shared_from_this());
    children.push_back(child);
    define(child->name, Value::of(Kind::Module, child));
    arrange();
}

void LiveWidget::remove_children() {
    for (const auto& c : children) {
        const auto it = members.find(c->name);
        if (it != members.end() && it->second->value.p == c) members.erase(it);
    }
    children.clear();
}

// ❓ By eye, not from the program: the chosen child fills the box, the others wait a box away
// on either side (`direction` 1: below and above; else right and left), cut off by the box.
void LiveWidget::arrange() {
    if (data.type != "SelectBox" && data.type != "SelectBar") return;
    const std::optional<Rect> box = data.rect("geometry");
    if (!box) return;
    const int index = int(data.number("index"));
    const bool across = data.number("direction") == 0;
    for (size_t i = 0; i < children.size(); ++i) {
        const float step = float(int(i) - index);
        children[i]->set_property("x", Value::number(across ? step * box->w : 0));
        children[i]->set_property("y", Value::number(across ? 0 : step * box->h));
        children[i]->set_property("w", Value::number(box->w));
        children[i]->set_property("h", Value::number(box->h));
    }
    set_property("clip", Value::integer(1));
}

namespace {

// What the scripts call on any widget.
std::shared_ptr<script::Reference> widget_method(const std::shared_ptr<LiveWidget>& w, const std::string& n) {
    auto fn = [&](script::NativeFunction f) { return std::make_shared<script::Variable>(Vm::native(n, std::move(f))); };
    auto sibling = [&](int step) -> Value {
        const LiveWidget* up = w->parent_widget();
        if (!up) return {};
        const auto it = std::find(up->children.begin(), up->children.end(), w);
        if (it == up->children.end()) return {};
        const auto at = it - up->children.begin() + step;
        return at < 0 || at >= std::ptrdiff_t(up->children.size()) ? Value{} : Value::of(Kind::Module, up->children[size_t(at)]);
    };
    if (n == "first") return std::make_shared<script::Variable>(w->children.empty() ? Value{} : Value::of(Kind::Module, w->children.front()));
    if (n == "last") return std::make_shared<script::Variable>(w->children.empty() ? Value{} : Value::of(Kind::Module, w->children.back()));
    if (n == "next") return std::make_shared<script::Variable>(sibling(1));
    if (n == "prev") return std::make_shared<script::Variable>(sibling(-1));
    if (n == "doCopy") return fn([w](Vm&, const Value&, Args) { return Value::of(Kind::Module, w->copy()); });
    if (n == "appendChild") // (context, widget) or (widget)
        return fn([w](Vm&, const Value&, Args a) {
            if (!a.empty())
                if (auto child = a.back().as<LiveWidget>()) w->append(child);
            return Value{};
        });
    if (n == "clearChildren" || n == "clearWindow")
        return fn([w](Vm&, const Value&, Args) {
            w->remove_children();
            return Value{};
        });
    if (n == "setFocus")
        return fn([w](Vm&, const Value&, Args a) {
            if (w->runtime) w->runtime->set_focus(a.empty() ? nullptr : a[0].as<LiveWidget>());
            return Value{};
        });
    return nullptr;
}

} // namespace

std::shared_ptr<script::Reference> LiveWidget::member(Vm& vm, const Value& self, const std::string& n) {
    if (std::shared_ptr<script::Variable> cell = find(n)) return cell; // script functions, children, statics
    if (const auto it = objects.find(n); it != objects.end()) return it->second;
    if (n == "actor") return objects[n] = std::make_shared<script::Variable>();
    if (std::shared_ptr<script::Reference> r = widget_method(std::static_pointer_cast<LiveWidget>(shared_from_this()), n)) return r;
    if (n != "name" && (data.has(n) || common_property(n))) {
        auto ref = std::make_shared<PropertyRef>();
        ref->widget = std::static_pointer_cast<LiveWidget>(shared_from_this()), ref->name = n;
        return ref;
    }
    return Module::member(vm, self, n);
}

Value LiveWidget::get(const std::string& n) const {
    if (const int i = rect_index(n); i >= 0) {
        const std::optional<Rect> r = data.rect("geometry");
        const float v[4] = {r ? r->x : 0, r ? r->y : 0, r ? r->w : 0, r ? r->h : 0};
        return Value::number(v[i]);
    }
    const Property* p = data.find(n);
    if (!p || p->values.empty()) {
        if (n == "visible") return Value::integer(1);
        if (n == "alpha" || n == "opacity" || n == "scale_x" || n == "scale_y") return Value::number(1);
        if (n == "text" || n == "key" || n == "image_path") return Value::string({});
        return Value::integer(0);
    }
    if (p->type == "string") return Value::string(p->values[0]);
    if (p->values.size() == 1) return number_value(p->values[0]);
    std::vector<Value> items;
    for (const std::string& s : p->values) items.push_back(number_value(s));
    return Value::array(std::move(items));
}

void LiveWidget::set(const std::string& n, const Value& v) {
    set_property(n, v);
    if (n == "index") arrange();
}

void LiveWidget::set_property(const std::string& n, const Value& v) {
    if (const int i = rect_index(n); i >= 0) {
        Property& g = property(data, "geometry", "rectangle");
        g.values.resize(4, "0");
        g.values[size_t(i)] = number_text(v.as_float());
        return;
    }
    if (v.kind == Kind::String) {
        Property& p = property(data, n, "string");
        p.type = "string", p.values = {v.as_string()};
    } else if (v.is_number()) {
        Property& p = property(data, n, "digit");
        p.type = "digit", p.values = {v.kind == Kind::Int ? std::to_string(v.i) : number_text(v.f)};
    } else if (const std::vector<Value>* items = v.items()) {
        Property& p = property(data, n, "vector");
        p.values.clear();
        for (const Value& item : *items) p.values.push_back(number_text(item.as_float()));
    } else if (v.is_nil()) {
        if (Property* p = find_property(data, n)) p->values.clear();
    }
}

LiveWidget* LiveWidget::page() {
    LiveWidget* w = this;
    while (w && w->data.type != "RootWindow") w = w->parent_widget();
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
// The runtime
// ---------------------------------------------------------------------------------------------

Runtime::Runtime(FileReader read, std::string region) : read_(std::move(read)), region_(std::move(region)) {
    vm_ = std::make_unique<Vm>([this](const std::string& name) -> std::optional<Bytes> {
        std::string path = name;
        while (!path.empty() && path[0] == '/') path.erase(0, 1);
        return read_(path + ".adc");
    });
    vm_->stub_natives = true; // most of the program's classes are not written yet
    Vm& vm = *vm_;

    // main::game, main::sound and the other objects the program's main script makes.
    auto game = make_bag("game");
    game->set("next_menu_project", Value::string({}));
    game->set("next_menu_arg", Value::string({}));
    game->set("major_menu_project", Value::string("arcade"));
    // The page a project opens first when nothing asked for another (the program's own default is not read yet).
    game->set("major_menu_page", Value::string("TopRoot"));
    // The settings a page tests before it builds itself: a retail game's (no maker's show build).
    auto option = make_bag("game.option");
    option->set("special_arcade_tuner", Value::string({}));
    option->set("can_watch_ending", Value::integer(0));
    game->set("option", object(option));
    game->set("last_menu_project", Value::string({}));
    game->set("last_sequence", Value::string({}));
    vm.root()->define("game", object(game));
    auto sound = make_bag("sound");
    sound->method("play", [this](Vm&, const Value&, Args a) {
        note("sound: " + (a.empty() ? std::string("?") : a[0].to_string()));
        return Value{};
    });
    vm.root()->define("sound", object(sound));

    // The program's "menu" module: the event classes; the rest are stand-ins.
    auto menu = std::make_shared<script::Module>();
    menu->name = "menu", menu->parent = vm.root(), menu->stubbed = true;
    vm.root()->define("menu", Value::of(Kind::Module, menu));
    menu->define("MScriptEvent", Vm::native("MScriptEvent", [](Vm&, const Value&, Args a) {
        // (context, module, function name, arguments)
        auto e = make_bag("MScriptEvent");
        e->set("module", a.size() > 1 ? a[1] : Value{});
        e->set("function", a.size() > 2 ? a[2] : Value{});
        e->set("args", a.size() > 3 ? a[3] : Value{});
        return object(e);
    }));
    menu->define("MFunctionEvent", Vm::native("MFunctionEvent", [](Vm&, const Value&, Args a) {
        // (function, arguments...)
        auto e = make_bag("MFunctionEvent");
        e->set("call", a.empty() ? Value{} : a[0]);
        e->set("args", Value::array(std::vector<Value>(a.begin() + (a.empty() ? 0 : 1), a.end())));
        return object(e);
    }));
    auto system = make_bag("menu::MSystem");
    system->method("GetRegionCode", [this](Vm&, const Value&, Args) { return Value::string(region_); });
    menu->define("MSystem", object(system));

    // The context every event function is given.
    auto context = make_bag("context");
    context->set("cursor_visible", Value::integer(1));
    context->set("transition", Value{});
    auto page_arg = [](Args a) { return a.empty() ? nullptr : a[0].as<LiveWidget>(); };
    context->method("startPage", [this, page_arg](Vm&, const Value&, Args a) {
        if (auto p = page_arg(a)) open_page(p, true);
        return Value{};
    });
    context->method("pushPage", [this, page_arg](Vm&, const Value&, Args a) {
        if (auto p = page_arg(a)) open_page(p, false);
        return Value{};
    });
    context->method("closePage", [this](Vm&, const Value&, Args) {
        close_page();
        return Value{};
    });
    context->method("finish", [this](Vm&, const Value&, Args) {
        while (!pages_.empty()) close_page();
        note("the project finished; next: " + wanted_project());
        return Value{};
    });
    context->method("pushEvent", [this](Vm&, const Value&, Args a) {
        if (!a.empty()) events_.push_back(a[0]);
        return Value{};
    });
    context->method("translate", [this](Vm&, const Value&, Args a) {
        // (the page's module, key)
        if (a.size() < 2) return Value{};
        const auto module = a[0].as<script::Module>();
        const std::string key = a[1].to_string();
        const std::string* text = module && project_ ? project_->text().find(module->path(), key) : nullptr;
        return Value::string(text ? *text : key);
    });
    for (const char* nothing : {"sync", "flushKeyEvent", "createRenderContext"}) context->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
    context->method("getRenderContext", [this](Vm&, const Value&, Args) { return context_; });
    context_ = object(context);
}

Runtime::~Runtime() {
    // The widgets are members of the interpreter's root module: it lets go of them.
    events_.clear();
    pages_.clear();
    focus_.reset();
    context_ = {};
    vm_.reset();
}

void Runtime::note(std::string line) {
    log_.push_back(std::move(line));
    while (log_.size() > kMaxLog) log_.pop_front();
}

std::string Runtime::wanted_project() const {
    const Value game = vm_->lookup("game");
    return vm_->attribute(game, "next_menu_project").to_string();
}

void Runtime::start_project(const std::string& name, const std::string& language) {
    events_.clear();
    pages_.clear();
    focus_.reset();
    if (root_widget_) vm_->root()->members.erase(root_widget_->name), root_widget_->clear();
    root_widget_.reset();
    project_ = std::make_unique<Project>(read_, name, language);
    root_widget_ = LiveWidget::build(project_->layout(), vm_->root());
    std::function<void(LiveWidget&)> own = [&](LiveWidget& w) {
        w.runtime = this;
        for (auto& c : w.children) own(*c);
    };
    own(*root_widget_);
    note("project " + name + " (" + root_widget_->name + ")");
    vm_->instructions = 0, vm_->budget = kCallBudget;
    vm_->require("/projects/" + name + "/" + name);
    call_handler(*root_widget_, "onLoad", {context_}, false);
    run_events();
}

Value Runtime::call_handler(LiveWidget& from, const std::string& name, std::vector<Value> args, bool bubble) {
    for (LiveWidget* w = &from; w; w = bubble ? w->parent_widget() : nullptr) {
        const auto it = w->members.find(name);
        if (it == w->members.end() || it->second->value.kind != Kind::Function) continue;
        try {
            vm_->instructions = 0, vm_->budget = kCallBudget;
            const Value r = vm_->call(it->second->value, args);
            if (!bubble || (r.kind == Kind::Int && r.i != 0)) return r; // 2: the event was dealt with
        } catch (const script::ScriptError& e) {
            note(std::string("script error in ") + w->path() + "::" + name + ": " + e.what());
            return {};
        }
    }
    return {};
}

void Runtime::run_events() {
    for (int n = 0; n < kMaxEventsPerFrame && !events_.empty(); ++n) {
        const Value e = events_.front();
        events_.pop_front();
        const auto bag = e.as<Bag>();
        if (!bag) continue;
        try {
            vm_->instructions = 0, vm_->budget = kCallBudget;
            std::vector<Value> args;
            const Value given = bag->get("args");
            Value callee = bag->get("call");
            if (callee.is_nil()) { // MScriptEvent: module.function(context, arguments...)
                callee = vm_->attribute(bag->get("module"), bag->get("function").to_string());
                args.push_back(context_);
            }
            if (const std::vector<Value>* items = given.items()) args.insert(args.end(), items->begin(), items->end());
            if (callee.p && callee.p->callable()) vm_->call(callee, args);
        } catch (const script::ScriptError& ex) {
            note(std::string("script error in an event: ") + ex.what());
        }
    }
}

void Runtime::update() { run_events(); }

void Runtime::open_page(const std::shared_ptr<LiveWidget>& page, bool replace) {
    if (replace && !pages_.empty()) close_page();
    pages_.push_back(page);
    note("page " + page->name);
    call_handler(*page, "onInitialize", {context_}, false);
    focus_first(*page);
}

void Runtime::close_page() {
    if (pages_.empty()) return;
    const std::shared_ptr<LiveWidget> page = pages_.back();
    call_handler(*page, "onFinalize", {context_}, false);
    pages_.pop_back();
    if (focus_ && focus_->page() == page.get()) focus_->state &= ~kFocus, focus_.reset();
    if (!pages_.empty()) focus_first(*pages_.back());
}

namespace {

void collect_focusable(const std::shared_ptr<LiveWidget>& w, std::vector<std::shared_ptr<LiveWidget>>& out) {
    if (!w->visible()) return;
    if ((w->data.type == "SelectBox" || w->data.type == "SelectBar") && !w->children.empty()) {
        // The box's chosen child is what has the focus.
        const size_t index = size_t(std::clamp(int(w->data.number("index")), 0, int(w->children.size()) - 1));
        out.push_back(w->children[index]);
        return;
    }
    if (w->can_focus()) out.push_back(w);
    for (const auto& c : w->children) collect_focusable(c, out);
}

} // namespace

void Runtime::focus_first(LiveWidget& page) {
    if (focus_ && focus_->page() == &page) return;
    std::vector<std::shared_ptr<LiveWidget>> all;
    for (const auto& c : page.children) collect_focusable(c, all);
    if (!all.empty()) set_focus(all.front());
}

void Runtime::set_focus(const std::shared_ptr<LiveWidget>& w) {
    if (focus_ == w) return;
    auto event = make_bag("event");
    if (focus_) {
        focus_->state &= ~kFocus;
        event->set("widget", Value::of(Kind::Module, focus_));
        call_handler(*focus_, "onFocusLeave", {context_, object(event)}, true);
    }
    focus_ = w;
    if (!w) return;
    w->state |= kFocus;
    auto enter = make_bag("event");
    enter->set("widget", Value::of(Kind::Module, w));
    call_handler(*w, "onFocusEnter", {context_, object(enter)}, true);
}

void Runtime::key(Key k) {
    LiveWidget* page = current_page();
    if (!page) return;
    if (k == Key::Accept || k == Key::Cancel) {
        auto event = make_bag("event");
        LiveWidget& from = focus_ ? *focus_ : *page;
        event->set("widget", focus_ ? Value::of(Kind::Module, focus_) : Value{});
        if (k == Key::Accept) call_handler(from, "onActivate", {context_, object(event)}, true);
        else call_handler(from, "onCancel", {context_, object(event), focus_ ? Value::of(Kind::Module, focus_) : Value{}}, true);
        run_events();
        return;
    }
    // In a box that shows one child at a time, the keys along it choose the child.
    if (LiveWidget* box = focus_ ? focus_->parent_widget() : nullptr; box && (box->data.type == "SelectBox" || box->data.type == "SelectBar")) {
        const bool across = box->data.number("direction") == 0;
        const int step = across ? (k == Key::Left ? -1 : k == Key::Right ? 1 : 0) : (k == Key::Up ? -1 : k == Key::Down ? 1 : 0);
        const int count = int(box->children.size());
        if (step && count) {
            int index = int(box->data.number("index")) + step;
            index = box->data.number("repeat") != 0 ? (index + count) % count : std::clamp(index, 0, count - 1);
            box->set("index", Value::integer(index));
            set_focus(box->children[size_t(index)]);
            run_events();
            return;
        }
    }
    // The nearest widget that can take the focus in the key's direction.
    std::vector<std::shared_ptr<LiveWidget>> all;
    for (const auto& c : page->children) collect_focusable(c, all);
    if (all.empty()) return;
    if (!focus_) return set_focus(all.front());
    const Rect at = focus_->screen_rect();
    const float cx = at.x + at.w / 2, cy = at.y + at.h / 2;
    const float dx = k == Key::Left ? -1.0f : k == Key::Right ? 1.0f : 0.0f, dy = k == Key::Up ? -1.0f : k == Key::Down ? 1.0f : 0.0f;
    std::shared_ptr<LiveWidget> best;
    float best_score = 0;
    for (const auto& w : all) {
        if (w == focus_) continue;
        const Rect r = w->screen_rect();
        const float ox = r.x + r.w / 2 - cx, oy = r.y + r.h / 2 - cy;
        const float along = ox * dx + oy * dy, across = std::fabs(ox * dy) + std::fabs(oy * dx);
        if (along <= 0.5f) continue;
        const float score = along + across * 2;
        if (!best || score < best_score) best = w, best_score = score;
    }
    if (best) set_focus(best);
    run_events();
}

} // namespace gt4r::menu
