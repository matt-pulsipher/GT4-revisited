#include "gt4r/script/vm.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gt4r::script {

namespace {

constexpr int kMaxDepth = 200;         // calls inside calls
constexpr size_t kMaxStubPath = 160; // a stand-in's name: longer ones answer nil

Value deref(Vm& vm, Value v) {
    while (v.kind == Kind::Reference) v = static_cast<Reference*>(v.p.get())->get(vm);
    return v;
}

Value reference(std::shared_ptr<Reference> r) { return Value::of(Kind::Reference, std::move(r)); }

// An array's element.
struct Element : Reference {
    std::shared_ptr<Array> array;
    s32 index = 0;
    Value get(Vm&) override {
        if (index < 0 || size_t(index) >= array->items.size())
            throw ScriptError("index " + std::to_string(index) + " outside an array of " + std::to_string(array->items.size()));
        return array->items[size_t(index)];
    }
    void set(Vm&, const Value& v) override {
        if (index < 0) throw ScriptError("index " + std::to_string(index) + " outside an array");
        if (size_t(index) >= array->items.size()) array->items.resize(size_t(index) + 1); // assigning past the end grows it
        array->items[size_t(index)] = v;
    }
};

bool is_stub(const Value& v) { return v.kind == Kind::Object && dynamic_cast<Stub*>(v.p.get()); }

} // namespace

// ---------------------------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------------------------

Value Value::string(std::string s) {
    auto t = std::make_shared<Text>();
    t->s = std::move(s);
    return of(Kind::String, std::move(t));
}

Value Value::array(std::vector<Value> items) {
    auto a = std::make_shared<Array>();
    a->items = std::move(items);
    return of(Kind::Array, std::move(a));
}

bool Value::truthy() const {
    switch (kind) {
    case Kind::Nil: return false;
    case Kind::Int: return i != 0;
    case Kind::Float: return f != 0;
    default: return true;
    }
}

const std::string& Value::as_string() const {
    static const std::string empty;
    return kind == Kind::String ? static_cast<const Text*>(p.get())->s : empty;
}

std::vector<Value>* Value::items() const { return kind == Kind::Array ? &static_cast<Array*>(p.get())->items : nullptr; }

std::string Value::to_string() const {
    char b[48];
    switch (kind) {
    case Kind::Nil: return "nil";
    case Kind::Int: return std::to_string(i);
    case Kind::Float:
        std::snprintf(b, sizeof b, "%g", double(f));
        return b;
    default: return p ? p->describe() : "nil";
    }
}

bool equal(const Value& a, const Value& b) {
    if (a.is_number() && b.is_number()) return a.kind == Kind::Int && b.kind == Kind::Int ? a.i == b.i : a.as_float() == b.as_float();
    if (a.kind != b.kind) return false;
    if (a.kind == Kind::Nil) return true;
    if (a.kind == Kind::String) return a.as_string() == b.as_string();
    return a.p == b.p;
}

std::string Array::describe() const {
    std::string s = "[";
    for (size_t i = 0; i < items.size(); ++i) s += (i ? ", " : "") + items[i].to_string();
    return s + "]";
}

// ---------------------------------------------------------------------------------------------
// Modules, classes, objects, functions
// ---------------------------------------------------------------------------------------------

std::shared_ptr<Variable> Module::find(const std::string& n) const {
    for (const Module* m = this; m; m = m->base.get())
        if (auto it = m->members.find(n); it != m->members.end()) return it->second;
    return nullptr;
}

std::shared_ptr<Variable> Module::define(const std::string& n, Value v) {
    std::shared_ptr<Variable>& cell = members[n];
    if (!cell) cell = std::make_shared<Variable>();
    cell->value = std::move(v);
    return cell;
}

std::string Module::path() const {
    const std::shared_ptr<Module> up = parent.lock();
    return up && up->parent.lock() ? up->path() + "::" + name : name; // the root's own name is left off
}

std::shared_ptr<Reference> Module::member(Vm& vm, const Value&, const std::string& n) {
    if (std::shared_ptr<Variable> cell = find(n)) return cell;
    if (std::shared_ptr<Reference> r = vm.builtin_member(Value::of(Kind::Module, shared_from_this()), n)) return r;
    if (stubbed || vm.stub_natives) return define(n, vm.make_stub((parent.lock() ? path() + "::" : std::string()) + n));
    return nullptr;
}

Value Module::call(Vm& vm, Args args) {
    if (!is_class) throw ScriptError("not a class: " + path());
    for (const Module* m = this; m; m = m->base.get())
        if (m->construct) return m->construct(vm, args);
    auto object = std::make_shared<Instance>();
    object->cls = std::static_pointer_cast<Module>(shared_from_this());
    std::vector<const Module*> chain;
    for (const Module* m = this; m; m = m->base.get()) chain.push_back(m);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        for (const std::string& a : (*it)->attributes) object->attributes[a] = std::make_shared<Variable>();
    const Value self = Value::of(Kind::Object, object);
    if (std::shared_ptr<Variable> init = find("__init__"))
        if (auto fn = init->value.as<Function>()) vm.invoke(*fn, self, args);
    return self;
}

void Module::clear() {
    auto held = std::move(members);
    members.clear();
    base.reset();
    construct = nullptr;
    for (auto& [n, cell] : held)
        if (cell) cell->clear();
}

std::shared_ptr<Reference> Instance::member(Vm&, const Value& self, const std::string& n) {
    if (auto it = attributes.find(n); it != attributes.end()) return it->second;
    if (!cls) return nullptr;
    std::shared_ptr<Variable> cell = cls->find(n);
    if (!cell) return nullptr;
    if (auto fn = cell->value.as<Function>(); fn && fn->method) { // a method, bound to this object
        auto bound = std::make_shared<Function>(*fn);
        bound->self = self;
        return std::make_shared<Variable>(Value::of(Kind::Function, std::move(bound)));
    }
    return cell;
}

Value Function::call(Vm& vm, Args args) { return native ? native(vm, self, args) : vm.invoke(*this, self, args); }

std::shared_ptr<Reference> Stub::member(Vm& vm, const Value&, const std::string& n) {
    std::shared_ptr<Variable>& cell = members[n];
    // A chain of stand-ins ends in nil, so a walk along one (widget.first, .next, .next, ...) ends.
    if (!cell) cell = std::make_shared<Variable>(path.size() > kMaxStubPath ? Value{} : vm.make_stub(path + "." + n));
    return cell;
}

Value Stub::call(Vm& vm, Args) {
    ++vm.stub_uses[path + "()"];
    std::shared_ptr<Variable>& cell = members["()"];
    if (!cell) cell = std::make_shared<Variable>(path.size() > kMaxStubPath ? Value{} : vm.make_stub(path + "()"));
    return cell->value;
}

// ---------------------------------------------------------------------------------------------
// The built-in methods. Their names are the program's (the tables its classes register); what
// each does is written from how the scripts use them (docs/formats/script.md).
// ---------------------------------------------------------------------------------------------

namespace {

std::shared_ptr<Reference> method(const char* name, const Value& self, NativeFunction fn) {
    Value f = Vm::native(name, std::move(fn));
    static_cast<Function*>(f.p.get())->self = self;
    return std::make_shared<Variable>(std::move(f));
}

s32 int_arg(Args a, size_t i, s32 fallback = 0) {
    if (i >= a.size()) return fallback;
    return a[i].kind == Kind::Int ? a[i].i : a[i].kind == Kind::Float ? s32(a[i].f) : fallback;
}

// "%d of %s".format(3, "x"): C's conversions, an argument each.
std::string format(const std::string& f, Args args) {
    std::string out;
    size_t next = 0;
    for (size_t i = 0; i < f.size(); ++i) {
        if (f[i] != '%') {
            out += f[i];
            continue;
        }
        size_t j = i + 1;
        while (j < f.size() && std::strchr("-+ #0123456789.l", f[j])) ++j;
        if (j >= f.size()) break;
        const char c = f[j];
        std::string spec = f.substr(i, j - i);
        spec.erase(std::remove(spec.begin(), spec.end(), 'l'), spec.end());
        char b[128];
        const Value v = next < args.size() ? args[next] : Value{};
        if (c == '%') out += '%';
        else if (std::strchr("diuxXc", c)) {
            std::snprintf(b, sizeof b, (spec + c).c_str(), v.kind == Kind::Float ? s32(v.f) : v.i);
            out += b, ++next;
        } else if (std::strchr("feEgG", c)) {
            std::snprintf(b, sizeof b, (spec + c).c_str(), double(v.as_float()));
            out += b, ++next;
        } else if (c == 's') {
            std::snprintf(b, sizeof b, (spec + c).c_str(), v.to_string().c_str());
            out += b, ++next;
        } else out += f.substr(i, j - i + 1);
        i = j;
    }
    return out;
}

// Every value's: toString, toInt, toFloat.
std::shared_ptr<Reference> value_method(const Value& self, const std::string& name) {
    if (name == "toString") return method("toString", self, [](Vm&, const Value& v, Args) { return Value::string(v.to_string()); });
    if (name == "toInt")
        return method("toInt", self, [](Vm&, const Value& v, Args) {
            if (v.kind == Kind::String) return Value::integer(s32(std::strtol(v.as_string().c_str(), nullptr, 0)));
            return Value::integer(v.kind == Kind::Float ? s32(v.f) : v.i);
        });
    if (name == "toFloat")
        return method("toFloat", self, [](Vm&, const Value& v, Args) {
            if (v.kind == Kind::String) return Value::number(std::strtof(v.as_string().c_str(), nullptr));
            return Value::number(v.as_float());
        });
    return nullptr;
}

std::shared_ptr<Reference> string_method(const Value& self, const std::string& name) {
    using V = const Value&;
    if (name == "size") return std::make_shared<Variable>(Value::integer(s32(self.as_string().size())));
    if (name == "value") return std::make_shared<Variable>(self);
    if (name == "at") // a character's code
        return method("at", self, [](Vm&, V s, Args a) {
            const s32 i = int_arg(a, 0);
            const std::string& t = s.as_string();
            return i >= 0 && size_t(i) < t.size() ? Value::integer(u8(t[size_t(i)])) : Value::integer(0);
        });
    if (name == "substr") // from, length
        return method("substr", self, [](Vm&, V s, Args a) {
            const std::string& t = s.as_string();
            const size_t from = std::min<size_t>(size_t(std::max(int_arg(a, 0), 0)), t.size());
            return Value::string(a.size() > 1 ? t.substr(from, size_t(std::max(int_arg(a, 1), 0))) : t.substr(from));
        });
    if (name == "split")
        return method("split", self, [](Vm&, V s, Args a) {
            const std::string& t = s.as_string();
            const std::string sep = a.empty() ? std::string(" ") : a[0].to_string();
            std::vector<Value> parts;
            size_t at = 0;
            for (;;) {
                const size_t end = sep.empty() ? std::string::npos : t.find(sep, at);
                parts.push_back(Value::string(t.substr(at, end == std::string::npos ? end : end - at)));
                if (end == std::string::npos) break;
                at = end + sep.size();
            }
            return Value::array(std::move(parts));
        });
    if (name == "format") return method("format", self, [](Vm&, V s, Args a) { return Value::string(format(s.as_string(), a)); });
    if (name == "upcase" || name == "downcase") {
        const bool up = name == "upcase";
        return method(up ? "upcase" : "downcase", self, [up](Vm&, V s, Args) {
            std::string t = s.as_string();
            for (char& c : t) c = char(up ? std::toupper(u8(c)) : std::tolower(u8(c)));
            return Value::string(std::move(t));
        });
    }
    return nullptr;
}

std::shared_ptr<Reference> array_method(const Value& self, const std::string& name) {
    using V = const Value&;
    auto items = [](V a) -> std::vector<Value>& { return *a.items(); };
    if (name == "size") return std::make_shared<Variable>(Value::integer(s32(items(self).size())));
    if (name == "push")
        return method("push", self, [items](Vm&, V s, Args a) {
            for (V v : a) items(s).push_back(v);
            return Value{};
        });
    if (name == "pop")
        return method("pop", self, [items](Vm&, V s, Args) {
            if (items(s).empty()) return Value{};
            Value v = items(s).back();
            items(s).pop_back();
            return v;
        });
    if (name == "shift")
        return method("shift", self, [items](Vm&, V s, Args) {
            if (items(s).empty()) return Value{};
            Value v = items(s).front();
            items(s).erase(items(s).begin());
            return v;
        });
    if (name == "unshift")
        return method("unshift", self, [items](Vm&, V s, Args a) {
            items(s).insert(items(s).begin(), a.begin(), a.end());
            return Value{};
        });
    if (name == "erase") // the element at an index
        return method("erase", self, [items](Vm&, V s, Args a) {
            const s32 i = int_arg(a, 0, -1);
            if (i >= 0 && size_t(i) < items(s).size()) items(s).erase(items(s).begin() + i);
            return Value{};
        });
    if (name == "join")
        return method("join", self, [items](Vm&, V s, Args a) {
            const std::string sep = a.empty() ? std::string() : a[0].to_string();
            std::string out;
            for (size_t i = 0; i < items(s).size(); ++i) out += (i ? sep : std::string()) + items(s)[i].to_string();
            return Value::string(std::move(out));
        });
    if (name == "sort") // by a compare function's "a before b", or by value
        return method("sort", self, [items](Vm& vm, V s, Args a) {
            std::vector<Value> v = items(s);
            const Value by = a.empty() ? Value{} : a[0];
            std::stable_sort(v.begin(), v.end(), [&](V x, V y) {
                if (by.p) {
                    const Value args[2] = {x, y};
                    return vm.call(by, args).truthy();
                }
                return vm.binary("<", x, y).truthy();
            });
            items(s) = std::move(v);
            return Value{};
        });
    return nullptr;
}

std::shared_ptr<Reference> module_method(const Value& self, const std::string& name) {
    using V = const Value&;
    auto mod = [](V m) { return static_cast<Module*>(m.p.get()); };
    if (name == "name") return std::make_shared<Variable>(Value::string(mod(self)->name));
    if (name == "defined")
        return method("defined", self, [mod](Vm&, V m, Args a) { return Value::integer(!a.empty() && mod(m)->find(a[0].to_string()) != nullptr); });
    if (name == "defineStatic") // a name and its value
        return method("defineStatic", self, [mod](Vm&, V m, Args a) {
            if (!a.empty()) mod(m)->define(a[0].to_string(), a.size() > 1 ? a[1] : Value{});
            return Value{};
        });
    if (name == "removeStatic")
        return method("removeStatic", self, [mod](Vm&, V m, Args a) {
            if (!a.empty()) mod(m)->members.erase(a[0].to_string());
            return Value{};
        });
    if (name == "clearStatic")
        return method("clearStatic", self, [mod](Vm&, V m, Args) {
            // Its values go; its functions and modules stay.
            for (auto it = mod(m)->members.begin(); it != mod(m)->members.end();)
                it = it->second->value.kind == Kind::Function || it->second->value.kind == Kind::Module ? std::next(it) : mod(m)->members.erase(it);
            return Value{};
        });
    if (name == "load") // a script of the module's own folder, run in the module
        return method("load", self, [](Vm& vm, V m, Args a) {
            if (!a.empty() && a[0].kind == Kind::String) vm.load_into(std::static_pointer_cast<Module>(m.p), a[0].as_string());
            return Value{};
        });
    return nullptr;
}

} // namespace

std::shared_ptr<Reference> Vm::builtin_member(const Value& of, const std::string& name) {
    std::shared_ptr<Reference> r;
    if (of.kind == Kind::String) r = string_method(of, name);
    else if (of.kind == Kind::Array) r = array_method(of, name);
    else if (of.kind == Kind::Module) r = module_method(of, name);
    if (!r && !of.is_nil()) r = value_method(of, name);
    return r;
}

// Module.load: the file is looked for beside each folder a script was read from.
void Vm::load_into(const std::shared_ptr<Module>& module, const std::string& name) {
    const std::vector<std::string> folders = folders_;
    for (auto dir = folders.rbegin(); dir != folders.rend(); ++dir) {
        std::optional<Bytes> file = loader_ ? loader_(*dir + name) : std::nullopt;
        if (!file) continue;
        Script s = Script::parse(*file);
        run(std::make_shared<const Code>(std::move(s.code)), module);
        return;
    }
    if (!stub_natives) throw ScriptError("no script \"" + name + "\" to load");
    ++stub_uses["load " + name];
}

// ---------------------------------------------------------------------------------------------
// The interpreter
// ---------------------------------------------------------------------------------------------

struct Vm::Frame {
    std::shared_ptr<const Code> code;
    std::vector<std::shared_ptr<Reference>> slots;
    std::vector<std::shared_ptr<Module>> scopes; // MODULE_DEFINE and CLASS_DEFINE push, SET_STATE 0 pops
    std::vector<Value> imports;                  // IMPORT "*": modules whose names are in reach
    std::map<std::string, std::shared_ptr<Reference>> imported;
    Value self;
    std::vector<Value> stack;
};

Vm::Vm(Loader loader) : loader_(std::move(loader)) {
    root_ = std::make_shared<Module>();
    root_->name = "main";
    // The class every script class names as its base.
    auto object = std::make_shared<Module>();
    object->name = "Object", object->parent = root_, object->is_class = true;
    root_->define("Object", Value::of(Kind::Module, object));
}

Vm::~Vm() {
    result_ = {};
    root_->clear();
}

Value Vm::native(std::string name, NativeFunction fn) {
    auto f = std::make_shared<Function>();
    f->name = std::move(name), f->native = std::move(fn);
    return Value::of(Kind::Function, std::move(f));
}

Value Vm::make_stub(const std::string& path) {
    auto s = std::make_shared<Stub>();
    s->path = path;
    ++stub_uses[path];
    return Value::of(Kind::Object, std::move(s));
}

void Vm::fail(const Frame& f, size_t pc, const std::string& what) const {
    const u32 line = pc < f.code->instructions.size() ? f.code->instructions[pc].line : 0;
    throw ScriptError(f.code->source + ":" + std::to_string(line) + ": " + what);
}

std::shared_ptr<Reference> Vm::member(const Value& of_, const std::string& name) {
    const Value of = deref(*this, of_);
    std::shared_ptr<Reference> r = of.p ? of.p->member(*this, of, name) : nullptr;
    if (!r) r = builtin_member(of, name);
    if (!r && stub_natives && !of.is_nil()) {
        const char* type = of.kind == Kind::Int ? "int" : of.kind == Kind::Float ? "float" : of.kind == Kind::String ? "string"
                         : of.kind == Kind::Array ? "array" : of.kind == Kind::Function ? "function" : "object";
        r = std::make_shared<Variable>(make_stub(std::string("(") + type + ")." + name));
    }
    return r;
}

Value Vm::attribute(const Value& of, const std::string& name) {
    const std::shared_ptr<Reference> r = member(of, name);
    return r ? deref(*this, r->get(*this)) : Value{};
}

Value Vm::lookup(const std::string& path) {
    Value v = Value::of(Kind::Module, root_);
    size_t at = 0;
    while (at <= path.size()) {
        const size_t end = std::min(path.find("::", at), path.size());
        const std::string part = path.substr(at, end - at);
        at = end + 2;
        if (part.empty() || (v.p == root_ && part == root_->name)) continue;
        const std::shared_ptr<Reference> r = v.p ? v.p->member(*this, v, part) : nullptr;
        if (!r) return {};
        v = deref(*this, r->get(*this));
    }
    return v;
}

Value Vm::call(const Value& callee_, Args args) {
    const Value callee = deref(*this, callee_);
    if (!callee.p || !callee.p->callable()) throw ScriptError("not a function: " + callee.to_string());
    return deref(*this, callee.p->call(*this, args));
}

Value Vm::invoke(Function& fn, const Value& self, Args args) {
    if (fn.native) return fn.native(*this, self, args);
    if (depth_ >= kMaxDepth) throw ScriptError("calls nested too deep in " + fn.name);
    Frame f;
    f.code = fn.code;
    const size_t params = f.code->parameters.size();
    f.slots.resize(std::max<size_t>(f.code->variables, params));
    for (size_t i = 0; i < params; ++i) f.slots[i] = std::make_shared<Variable>(i < args.size() ? args[i] : Value{});
    f.scopes.push_back(fn.scope ? fn.scope : root_);
    f.self = self;
    result_ = {};
    ++depth_;
    try {
        execute(f);
    } catch (...) {
        --depth_;
        throw;
    }
    --depth_;
    return result_;
}

void Vm::run(std::shared_ptr<const Code> code, std::shared_ptr<Module> scope) {
    Frame f;
    f.code = std::move(code);
    f.slots.resize(f.code->variables);
    f.scopes.push_back(scope ? std::move(scope) : root_);
    execute(f);
}

void Vm::require(const std::string& name, std::shared_ptr<Module> scope) {
    if (!scope) scope = root_;
    if (auto it = natives_.find(name); it != natives_.end()) {
        it->second(*this, *scope);
        return;
    }
    // A script: "/config/course" from the top; a bare name beside the scripts read so far.
    std::optional<Bytes> file;
    std::string found = name;
    if (loader_) {
        if (!name.empty() && name[0] != '/')
            for (auto dir = folders_.rbegin(); !file && dir != folders_.rend(); ++dir) file = loader_(found = *dir + name);
        if (!file) file = loader_(found = name);
    }
    if (file) {
        const size_t slash = found.rfind('/');
        // Its folder is searched first from now on (Module.load and bare names name files beside it).
        const std::string folder = slash == std::string::npos ? std::string() : found.substr(0, slash + 1);
        folders_.erase(std::remove(folders_.begin(), folders_.end(), folder), folders_.end());
        folders_.push_back(folder);
        Script s = Script::parse(*file);
        run(std::make_shared<const Code>(std::move(s.code)), std::move(scope));
        return;
    }
    if (!stub_natives) throw ScriptError("nothing provides \"" + name + "\"");
    ++stub_uses["require " + name];
    if (name.find('/') == std::string::npos) scope->stubbed = true; // one of the program's modules
}

// A name in reach of a frame: the object's own (in a method), what was imported, then each
// module from the innermost outward.
std::shared_ptr<Variable> Vm::find_name(Frame& f, const std::string& name) {
    for (auto s = f.scopes.rbegin(); s != f.scopes.rend(); ++s)
        for (std::shared_ptr<Module> m = *s; m; m = m->parent.lock())
            if (std::shared_ptr<Variable> cell = m->find(name)) return cell;
    return nullptr;
}

std::shared_ptr<Reference> Vm::resolve(Frame& f, const std::vector<std::string>& symbols) {
    // The path's parts, then the whole ("menu", "MSound", "menu::MSound").
    const size_t parts = symbols.size() - 1;
    const std::string& first = symbols[0];
    std::shared_ptr<Reference> at;
    if (f.self.p) at = f.self.p->member(*this, f.self, first);
    if (!at)
        if (auto it = f.imported.find(first); it != f.imported.end()) at = it->second;
    if (!at) at = find_name(f, first);
    if (!at && first == root_->name) at = std::make_shared<Variable>(Value::of(Kind::Module, root_));
    for (size_t i = 0; !at && i < f.imports.size(); ++i) at = f.imports[i].p ? f.imports[i].p->member(*this, f.imports[i], first) : nullptr;
    if (!at) {
        // A stubbed module in reach stands for the name; otherwise the root does.
        if (!stub_natives) throw ScriptError("no \"" + first + "\" in reach");
        for (auto s = f.scopes.rbegin(); !at && s != f.scopes.rend(); ++s)
            for (std::shared_ptr<Module> m = *s; !at && m; m = m->parent.lock())
                if (m->stubbed) at = m->member(*this, {}, first);
        if (!at) at = root_->define(first, make_stub(first));
    }
    for (size_t i = 1; i < parts; ++i) {
        const Value v = deref(*this, at->get(*this));
        at = member(v, symbols[i]);
        if (!at) throw ScriptError("no \"" + symbols[i] + "\" in " + v.to_string());
    }
    return at;
}

// MODULE_DEFINE's and CLASS_DEFINE's module: the one of that name in reach, or a new one in
// the current module.
std::shared_ptr<Module> Vm::module_at(Frame& f, const std::vector<std::string>& symbols, bool as_class,
                                      const std::string& class_name, const std::vector<std::string>& base) {
    const std::shared_ptr<Module> here = f.scopes.back();
    auto child = [&](const std::shared_ptr<Module>& in, const std::string& name, std::shared_ptr<Variable> cell) {
        std::shared_ptr<Module> m = cell ? cell->value.as<Module>() : nullptr;
        if (!m) { // nothing there, or a stand-in: a module of the script's own takes its place
            m = std::make_shared<Module>();
            m->name = name, m->parent = in;
            in->define(name, Value::of(Kind::Module, m));
        }
        return m;
    };
    if (as_class) {
        std::shared_ptr<Module> m = child(here, class_name, here->find(class_name));
        m->is_class = true;
        if (!base.empty()) {
            const Value b = deref(*this, resolve(f, base.size() == 1 ? std::vector<std::string>{base[0], base[0]} : base)->get(*this));
            if (auto bm = b.as<Module>(); bm && bm != m) m->base = bm;
        }
        return m;
    }
    const size_t parts = symbols.size() > 1 ? symbols.size() - 1 : 1;
    std::shared_ptr<Module> m;
    if (symbols[0] == root_->name) m = root_;
    else {
        std::shared_ptr<Variable> cell = find_name(f, symbols[0]);
        m = cell && cell->value.as<Module>() ? cell->value.as<Module>() : child(here, symbols[0], here->find(symbols[0]));
    }
    for (size_t i = 1; i < parts; ++i) m = child(m, symbols[i], m->find(symbols[i]));
    return m;
}

void Vm::execute(Frame& f) {
    const std::vector<Instruction>& code = f.code->instructions;
    std::vector<Value>& st = f.stack;
    st.reserve(f.code->stack + 4);
    const size_t base_scopes = f.scopes.size();
    size_t pc = 0;
    auto pop = [&]() {
        if (st.empty()) fail(f, pc, "the value stack is empty");
        Value v = std::move(st.back());
        st.pop_back();
        return v;
    };
    auto pop_many = [&](u32 n) {
        if (st.size() < n) fail(f, pc, "the value stack is short");
        std::vector<Value> v(st.end() - n, st.end());
        st.resize(st.size() - n);
        for (Value& x : v) x = deref(*this, std::move(x));
        return v;
    };
    try {
        for (; pc < code.size(); ++pc) {
            const Instruction& in = code[pc];
            if (++instructions > budget) fail(f, pc, "the script does not end");
            switch (in.op) {
            case Op::Nop:
                break;
            case Op::IntConst:
                st.push_back(Value::integer(s32(in.value)));
                break;
            case Op::FloatConst:
                st.push_back(Value::number(in.number));
                break;
            case Op::StringConst:
                st.push_back(Value::string(in.text));
                break;
            case Op::NilConst:
                st.emplace_back();
                break;
            case Op::VariablePush: {
                if (in.value >= f.slots.size()) f.slots.resize(in.value + 1);
                std::shared_ptr<Reference>& slot = f.slots[in.value];
                // One name: a variable of this block. A path: a name in reach, found once.
                if (!slot) slot = in.symbols.size() < 2 ? std::make_shared<Variable>() : resolve(f, in.symbols);
                st.push_back(reference(slot));
                break;
            }
            case Op::Eval:
                if (st.empty()) fail(f, pc, "the value stack is empty");
                if (st.back().kind == Kind::Reference) st.back() = deref(*this, std::move(st.back()));
                break;
            case Op::AttributePush: {
                const Value of = deref(*this, pop());
                std::shared_ptr<Reference> r = member(of, in.text);
                if (!r) fail(f, pc, "no \"" + in.text + "\" in " + of.to_string());
                st.push_back(reference(std::move(r)));
                break;
            }
            case Op::Assign: {
                Value v = deref(*this, pop());
                const Value to = pop();
                if (to.kind != Kind::Reference) fail(f, pc, "assignment to a value");
                static_cast<Reference*>(to.p.get())->set(*this, v);
                st.push_back(std::move(v));
                break;
            }
            case Op::ListAssign: { // [a, b] = value
                Value v = deref(*this, pop());
                if (st.size() < in.value) fail(f, pc, "the value stack is short");
                const std::vector<Value>* items = v.items();
                for (u32 i = 0; i < in.value; ++i) {
                    const Value& to = st[st.size() - in.value + i];
                    if (to.kind != Kind::Reference) fail(f, pc, "assignment to a value");
                    Value item = items ? (i < items->size() ? (*items)[i] : Value{}) : is_stub(v) ? v : Value{};
                    static_cast<Reference*>(to.p.get())->set(*this, item);
                }
                st.resize(st.size() - in.value);
                st.push_back(std::move(v));
                break;
            }
            case Op::Pop:
                result_ = deref(*this, pop());
                break;
            case Op::Call: {
                std::vector<Value> args = pop_many(in.value);
                const Value callee = deref(*this, pop());
                if (!callee.p || !callee.p->callable()) fail(f, pc, "not a function: " + callee.to_string());
                st.push_back(callee.p->call(*this, args));
                break;
            }
            case Op::BinaryOperator: {
                Value b = deref(*this, pop()), a = deref(*this, pop());
                st.push_back(binary(in.text, a, b));
                break;
            }
            case Op::UnaryOperator:
                st.push_back(unary(in.text, deref(*this, pop())));
                break;
            case Op::BinaryAssignOperator: { // a += b
                const Value b = deref(*this, pop()), to = pop();
                if (to.kind != Kind::Reference) fail(f, pc, "assignment to a value");
                Reference* r = static_cast<Reference*>(to.p.get());
                Value v = binary(in.text, deref(*this, r->get(*this)), b);
                r->set(*this, v);
                st.push_back(std::move(v));
                break;
            }
            case Op::UnaryAssignOperator: { // "@++" leaves the value before, "++@" the one after
                const Value to = pop();
                if (to.kind != Kind::Reference) fail(f, pc, "assignment to a value");
                Reference* r = static_cast<Reference*>(to.p.get());
                const Value before = deref(*this, r->get(*this));
                const bool up = in.text.find("++") != std::string::npos;
                const Value after = binary(up ? "+" : "-", before, Value::integer(1));
                r->set(*this, after);
                st.push_back(in.text[0] == '@' ? before : after);
                break;
            }
            case Op::ArrayPush:
                st.push_back(Value::array(pop_many(in.value)));
                break;
            case Op::StringPush: {
                std::string s;
                for (const Value& v : pop_many(in.value)) s += v.to_string();
                st.push_back(Value::string(std::move(s)));
                break;
            }
            case Op::Print: {
                std::string s;
                for (const Value& v : pop_many(in.value)) s += v.to_string();
                output.push_back(std::move(s));
                st.emplace_back();
                break;
            }
            case Op::Jump:
                pc = size_t(in.value) - 1;
                break;
            case Op::JumpZero:
                if (!deref(*this, pop()).truthy()) pc = size_t(in.value) - 1;
                break;
            case Op::JumpNotZero:
                if (deref(*this, pop()).truthy()) pc = size_t(in.value) - 1;
                break;
            case Op::LogicalAnd: // a && b: a false value stays as the result; else POP and b follow
                if (st.empty()) fail(f, pc, "the value stack is empty");
                if (!deref(*this, st.back()).truthy()) pc = size_t(in.value) - 1;
                break;
            case Op::LogicalOr:
                if (st.empty()) fail(f, pc, "the value stack is empty");
                if (deref(*this, st.back()).truthy()) pc = size_t(in.value) - 1;
                break;
            case Op::ModuleDefine:
                f.scopes.push_back(module_at(f, in.symbols, false, {}, {}));
                break;
            case Op::ClassDefine:
                f.scopes.push_back(module_at(f, {}, true, in.text, in.symbols));
                break;
            case Op::SetState:
                // 0 ends a module's or class's block (the file's, at the top); 1 returns.
                if (in.value == 0 && f.scopes.size() > base_scopes) f.scopes.pop_back();
                else return;
                break;
            case Op::FunctionDefine: case Op::MethodDefine: {
                auto fn = std::make_shared<Function>();
                fn->name = in.text, fn->code = in.body, fn->scope = f.scopes.back(), fn->method = in.op == Op::MethodDefine;
                f.scopes.back()->define(in.text, Value::of(Kind::Function, std::move(fn)));
                break;
            }
            case Op::StaticDefine:
                if (!f.scopes.back()->members.count(in.text)) f.scopes.back()->define(in.text);
                break;
            case Op::AttributeDefine: {
                std::vector<std::string>& a = f.scopes.back()->attributes;
                if (std::find(a.begin(), a.end(), in.text) == a.end()) a.push_back(in.text);
                break;
            }
            case Op::LocalDefine:
                if (in.value >= f.slots.size()) f.slots.resize(in.value + 1);
                f.slots[in.value] = std::make_shared<Variable>();
                break;
            case Op::Import: { // a module's path, then one name of it or "*"
                const Value m = deref(*this, resolve(f, in.symbols.size() == 1 ? std::vector<std::string>{in.symbols[0], in.symbols[0]}
                                                                                : in.symbols)->get(*this));
                if (in.text == "*") f.imports.push_back(m);
                else if (std::shared_ptr<Reference> r = member(m, in.text)) f.imported[in.text] = std::move(r);
                else fail(f, pc, "no \"" + in.text + "\" in " + m.to_string());
                break;
            }
            case Op::Require: {
                const Value name = deref(*this, pop());
                if (name.kind != Kind::String) fail(f, pc, "REQUIRE of " + name.to_string());
                require(name.as_string(), f.scopes.back());
                break;
            }
            case Op::Throw:
                fail(f, pc, "thrown: " + deref(*this, pop()).to_string());
            case Op::TryCatch: case Op::Undef: case Op::Count:
                // No script on the discs has these.
                fail(f, pc, std::string(op_name(in.op)) + " is not written");
            }
        }
    } catch (const ScriptError& e) {
        // The place, once: an error from a call below already names its own.
        const std::string what = e.what();
        if (what.find(".ad:") != std::string::npos) throw;
        fail(f, pc, what);
    }
}

// ---------------------------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------------------------

Value Vm::binary(const std::string& op, const Value& a, const Value& b) {
    if (op == "[]") {
        if (a.kind == Kind::Array && b.kind == Kind::Int) {
            auto e = std::make_shared<Element>();
            e->array = std::static_pointer_cast<Array>(a.p), e->index = b.i;
            return reference(std::move(e));
        }
        if (a.kind == Kind::String && b.kind == Kind::Int) {
            const std::string& s = a.as_string();
            if (b.i < 0 || size_t(b.i) >= s.size()) throw ScriptError("index outside a string");
            return Value::string(s.substr(size_t(b.i), 1));
        }
        if (a.p && b.kind == Kind::String) // a member by name: page["open"], Root["Grid1"]
            if (std::shared_ptr<Reference> r = member(a, b.as_string())) return reference(std::move(r));
        if (is_stub(a)) return reference(a.p->member(*this, a, "[]"));
        throw ScriptError("no [" + b.to_string() + "] in " + a.to_string());
    }
    if (op == "==") return Value::integer(equal(a, b));
    if (op == "!=") return Value::integer(!equal(a, b));
    if (is_stub(a) || is_stub(b)) {
        // Nothing is known of a stand-in's value: comparisons are false, arithmetic is another.
        if (op == "<" || op == ">" || op == "<=" || op == ">=") return Value::integer(0);
        return is_stub(a) ? a : b;
    }
    if (op == "+" && (a.kind == Kind::String || b.kind == Kind::String)) return Value::string(a.to_string() + b.to_string());
    if (a.kind == Kind::String && b.kind == Kind::String) {
        const int c = a.as_string().compare(b.as_string());
        if (op == "<") return Value::integer(c < 0);
        if (op == ">") return Value::integer(c > 0);
        if (op == "<=") return Value::integer(c <= 0);
        if (op == ">=") return Value::integer(c >= 0);
    }
    if (op == "+" && a.kind == Kind::Array && b.kind == Kind::Array) {
        std::vector<Value> v = *a.items();
        v.insert(v.end(), b.items()->begin(), b.items()->end());
        return Value::array(std::move(v));
    }
    if (!a.is_number() || !b.is_number()) throw ScriptError("no " + a.to_string() + " " + op + " " + b.to_string());
    if (a.kind == Kind::Int && b.kind == Kind::Int) {
        const s32 x = a.i, y = b.i;
        const u32 ux = u32(x), uy = u32(y);
        if (op == "+") return Value::integer(s32(ux + uy));
        if (op == "-") return Value::integer(s32(ux - uy));
        if (op == "*") return Value::integer(s32(ux * uy));
        if (op == "/" || op == "%") {
            if (y == 0) throw ScriptError("division by zero");
            if (x == INT32_MIN && y == -1) return Value::integer(op == "/" ? x : 0);
            return Value::integer(op == "/" ? x / y : x % y);
        }
        if (op == "<") return Value::integer(x < y);
        if (op == ">") return Value::integer(x > y);
        if (op == "<=") return Value::integer(x <= y);
        if (op == ">=") return Value::integer(x >= y);
        if (op == "&") return Value::integer(x & y);
        if (op == "|") return Value::integer(x | y);
        if (op == "^") return Value::integer(x ^ y);
        if (op == "<<") return Value::integer(s32(ux << (uy & 31)));
        if (op == ">>") return Value::integer(x >> (uy & 31));
    } else {
        const float x = a.as_float(), y = b.as_float();
        if (op == "+") return Value::number(x + y);
        if (op == "-") return Value::number(x - y);
        if (op == "*") return Value::number(x * y);
        if (op == "/") return Value::number(x / y);
        if (op == "%") return Value::number(std::fmod(x, y));
        if (op == "<") return Value::integer(x < y);
        if (op == ">") return Value::integer(x > y);
        if (op == "<=") return Value::integer(x <= y);
        if (op == ">=") return Value::integer(x >= y);
    }
    throw ScriptError("no operator " + op + " for " + a.to_string() + " and " + b.to_string());
}

Value Vm::unary(const std::string& op, const Value& a) {
    if (op == "!") return Value::integer(!a.truthy());
    if (is_stub(a)) return a;
    if (op == "-@" && a.kind == Kind::Int) return Value::integer(s32(0u - u32(a.i)));
    if (op == "-@" && a.kind == Kind::Float) return Value::number(-a.f);
    if (op == "+@" && a.is_number()) return a;
    if (op == "~" && a.kind == Kind::Int) return Value::integer(~a.i);
    throw ScriptError("no operator " + op + " for " + a.to_string());
}

} // namespace gt4r::script
