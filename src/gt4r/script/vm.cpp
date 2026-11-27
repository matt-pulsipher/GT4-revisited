#include "gt4r/script/vm.h"

#include "base/ee_float.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gt4r::script {

namespace ee = gt3r::ee;

namespace {

constexpr int kMaxDepth = 200;         // calls inside calls
constexpr size_t kMaxStubPath = 160; // a stand-in's name: longer ones answer nil

Value deref(Vm& vm, Value v) {
    while (v.kind == Kind::Reference) v = static_cast<Reference*>(v.p.get())->get(vm);
    return v;
}

Value reference(std::shared_ptr<Reference> r) { return Value::of(Kind::Reference, std::move(r)); }

// A variable given as it is, not its value (`f(result)` where f assigns `*result`; the
// compiled code pushes the variable and leaves the EVAL out): a value that stands for the
// variable. Read, it gives the variable's value; assigned through, it sets the variable.
struct Pointer : Reference {
    std::shared_ptr<Reference> to;
    Value get(Vm& vm) override { return to->get(vm); }
    void set(Vm& vm, const Value& v) override { to->set(vm, v); }
};

// What a value pushed without EVAL is as an argument or as the right side of an assignment:
// a variable becomes a pointer to it, a pointer stays, anything else is read.
Value keep_variable(Vm& vm, Value v) {
    if (v.kind != Kind::Reference) return v;
    if (dynamic_cast<Pointer*>(v.p.get())) return v;
    if (auto cell = std::dynamic_pointer_cast<Variable>(v.p)) {
        if (cell->value.kind == Kind::Reference && dynamic_cast<Pointer*>(cell->value.p.get())) return cell->value; // a pointer handed on
        auto p = std::make_shared<Pointer>();
        p->to = std::move(cell);
        return reference(std::move(p));
    }
    return deref(vm, std::move(v));
}

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

// JUMP_ZERO, JUMP_NOT_ZERO, LOGICAL_AND and LOGICAL_OR ask the value its integer (0x31C858, ...): a float between -1 and 1,
// a text that does not start with a number and every value but a number or a text (an array, an object, a function) are
// false. (The program reads nil's through a null pointer; it is false here.) Nothing is known of a stand-in's value.
bool Value::truthy() const { return is_stub(*this) || value_int(*this) != 0; }

namespace {

// The program's atof (0x5A2F70) and the double made a float: decimal only (no "0x", "inf" or "nan"): the longest start that
// is a decimal number.
float decimal_float(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && std::isspace(u8(s[i]))) ++i;
    const size_t from = i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    size_t digits = 0;
    while (i < s.size() && std::isdigit(u8(s[i]))) ++i, ++digits;
    if (i < s.size() && s[i] == '.')
        for (++i; i < s.size() && std::isdigit(u8(s[i])); ++i) ++digits;
    if (!digits) return 0;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        size_t j = i + 1;
        if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
        if (j == s.size() || !std::isdigit(u8(s[j]))) return 0; // an "e" with no digits: nothing
        while (j < s.size() && std::isdigit(u8(s[j]))) ++j;
        i = j;
    }
    // The double made a float by the program's own conversion (0x57FA48): chopped, the exponent 255 an ordinary one.
    return ee::fast::host(ee::from_double(std::strtod(s.substr(from, i - from).c_str(), nullptr), ee::Round::Chop));
}

// fmodf (0x57D788) on the EE's floats: x when |x| < |y| (as the FPU compares), a zero of x's sign when y is 0, else the
// exact remainder of the two mantissas (the exponent 255 an ordinary one).
u32 ee_fmod(u32 a, u32 b) {
    const u32 sign = a & 0x80000000u, ax = a & 0x7FFFFFFFu, bx = b & 0x7FFFFFFFu;
    if (ee::fast::key(ax) < ee::fast::key(bx)) return a;
    const s32 eb = s32(bx >> 23), ea = s32(ax >> 23);
    if (eb == 0) return sign;
    const u32 mb = (bx & 0x7FFFFFu) | 0x800000u;
    u32 r = (ax & 0x7FFFFFu) | 0x800000u;
    if (r >= mb) r -= mb;
    for (s32 n = ea - eb; n > 0; --n) {
        r <<= 1;
        if (r >= mb) r -= mb;
    }
    if (r == 0) return sign;
    s32 e = eb;
    while (r < 0x800000u) r <<= 1, --e;
    if (e <= 0) return sign;
    return sign | (u32(e) << 23) | (r & 0x7FFFFFu);
}

} // namespace

std::map<std::string, size_t>* mixed_notes = [] {
    static std::map<std::string, size_t> notes;
    static std::string file;
    const char* to = std::getenv("GT4R_VM_NOTES");
    if (!to || !*to) return static_cast<std::map<std::string, size_t>*>(nullptr);
    file = to;
    std::atexit([] {
        if (FILE* out = std::fopen(file.c_str(), "a")) {
            for (const auto& [where, n] : notes) std::fprintf(out, "%zu\t%s\n", n, where.c_str());
            std::fclose(out);
        }
    });
    return &notes;
}();

s32 value_int(const Value& v) {
    switch (v.kind) {
    case Kind::Int: return v.i; // hInt 0x2FE260
    case Kind::Float: return ee::fast::to_int(ee::fast::bits(v.f)); // hFloat 0x2F9340: cvt.w.s
    case Kind::String: return s32(std::strtol(v.as_string().c_str(), nullptr, 10)); // hString 0x314A40
    default: return 0; // hObject 0x30B3F8
    }
}

float value_float(const Value& v) {
    switch (v.kind) {
    case Kind::Int: return ee::fast::host(ee::fast::from_int(v.i)); // hInt 0x2FE268: cvt.s.w
    case Kind::Float: return v.f;
    case Kind::String: return decimal_float(v.as_string()); // hString 0x314A88: atof, to a float
    default: return 0; // hObject 0x30B400
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
    case Kind::Float: // hFloat's toString (0x2F90B0): "%f"
        std::snprintf(b, sizeof b, "%f", ee::F::raw(ee::fast::bits(f)).value());
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
    const Value self = make_object();
    if (std::shared_ptr<Variable> init = find("__init__"))
        if (auto fn = init->value.as<Function>()) vm.invoke(*fn, self, args);
    return self;
}

bool Module::native_class() const {
    for (const Module* m = this; m; m = m->base.get())
        if (m->construct) return true;
    return false;
}

Value Module::make_object() {
    auto object = std::make_shared<Instance>();
    object->cls = std::static_pointer_cast<Module>(shared_from_this());
    std::vector<const Module*> chain;
    for (const Module* m = this; m; m = m->base.get()) chain.push_back(m);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        for (const std::string& a : (*it)->attributes) object->attributes[a] = std::make_shared<Variable>();
    return Value::of(Kind::Object, object);
}

void Module::clear() {
    auto held = std::move(members);
    members.clear();
    imported.clear();
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

// "%d of %s".format(3, "x"): C's conversions, an argument each, and the program's own
// "%.2t": a whole number written with a point before its last two digits (1234: "12.34").
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
        } else if (c == 't') {
            const size_t dot = spec.find('.');
            const int places = dot == std::string::npos ? 0 : std::atoi(spec.c_str() + dot + 1);
            const s64 n = v.kind == Kind::Float ? s64(v.f) : s64(v.i);
            std::string digits = std::to_string(n < 0 ? -n : n);
            if (places > 0) {
                if (digits.size() <= size_t(places)) digits.insert(0, size_t(places) + 1 - digits.size(), '0');
                digits.insert(digits.size() - size_t(places), ".");
            }
            out += (n < 0 ? "-" : "") + digits, ++next;
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
        // A string's: decimal, as atoi (hString's 0x314A40 -> 0x5A2F88: strtol base 10), so "0008" is 8.
        return method("toInt", self, [](Vm&, const Value& v, Args) { return Value::integer(value_int(v)); });
    if (name == "toFloat")
        return method("toFloat", self, [](Vm&, const Value& v, Args) { return Value::number(value_float(v)); });
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
    // build(args...): each "{n}" is the n-th argument as text (the program splits the text at
    // "{" and "}", 0x315B78; ❓ a mark with no such argument is left as it is).
    if (name == "build")
        return method("build", self, [](Vm&, V s, Args a) {
            const std::string t = s.as_string();
            std::string out;
            size_t at = 0;
            for (;;) {
                const size_t open = t.find('{', at), close = open == std::string::npos ? open : t.find('}', open);
                if (close == std::string::npos) break;
                const std::string n = t.substr(open + 1, close - open - 1);
                const bool digits = !n.empty() && n.find_first_not_of("0123456789") == std::string::npos;
                out += t.substr(at, open - at);
                if (digits && std::stoul(n) < a.size()) out += a[std::stoul(n)].to_string();
                else out += t.substr(open, close + 1 - open);
                at = close + 1;
            }
            return Value::string(out + t.substr(at));
        });
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
    if (name == "bsearch") // a value's index in the sorted array, -1 when it is not there (Prologue's scripts)
        return method("bsearch", self, [items](Vm& vm, V s, Args a) {
            if (a.empty()) return Value::integer(-1);
            const std::vector<Value>& v = items(s);
            size_t lo = 0, hi = v.size();
            while (lo < hi) {
                const size_t mid = (lo + hi) / 2;
                if (vm.binary("<", v[mid], a[0]).truthy()) lo = mid + 1;
                else hi = mid;
            }
            return Value::integer(lo < v.size() && vm.binary("==", v[lo], a[0]).truthy() ? s32(lo) : -1);
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
    std::vector<std::string> folders = folders_;
    if (!name.empty() && name[0] == '/') folders = {std::string()}; // a path from the top
    for (auto dir = folders.rbegin(); dir != folders.rend(); ++dir) {
        std::optional<Bytes> file = loader_ ? loader_(*dir + name) : std::nullopt;
        if (!file) continue;
        Script s = Script::parse(*file);
        run(std::make_shared<const Code>(std::move(s.code)), module);
        if (after_load) after_load(*this, name, module);
        return;
    }
    if (!stub_natives) throw ScriptError("no script \"" + name + "\" to load");
    ++stub_uses["load " + name];
}

// ---------------------------------------------------------------------------------------------
// The interpreter
// ---------------------------------------------------------------------------------------------

struct Frame {
    std::shared_ptr<const Code> code;
    size_t pc = 0;
    size_t base_scopes = 1;
    std::shared_ptr<Reference> evaluated; // what the last EVAL read, and where
    size_t evaluated_at = 0;
    bool constructs = false; // a class's __init__: the call gives the object
    Value object;
    std::function<Value(Vm&, const Value&)> after; // of a call a native asked for
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

Vm::~Vm() { root_->clear(); }

Task::Task() = default;
Task::~Task() = default;

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

Value Vm::call(const Value& callee, Args args) {
    Task t;
    t.can_wait = false;
    if (begin(t, callee, args)) return run_to_end(t);
    return t.result;
}

void Vm::push_frame(Task& t, const Function& fn, const Value& self, Args args) {
    if (t.frames.size() >= size_t(kMaxDepth)) throw ScriptError("calls nested too deep in " + fn.name);
    auto f = std::make_unique<Frame>();
    f->code = fn.code;
    const size_t params = f->code->parameters.size();
    f->slots.resize(std::max<size_t>(f->code->variables, params));
    for (size_t i = 0; i < params; ++i) f->slots[i] = std::make_shared<Variable>(i < args.size() ? args[i] : Value{});
    f->scopes.push_back(fn.scope ? fn.scope : root_);
    f->self = self;
    t.reg = {};
    t.frames.push_back(std::move(f));
}

// The call a native asked for in its place (then_call): a script function gets a frame; one of
// the program's is called here. True when a frame was pushed.
bool Vm::push_chained(Task& t, Value& value) {
    while (t.chained) {
        Task::Chained c = std::move(*t.chained);
        t.chained.reset();
        const Value callee = deref(*this, c.callee);
        if (auto fn = callee.as<Function>(); fn && !fn->native) {
            push_frame(t, *fn, fn->self, c.args);
            t.frames.back()->after = std::move(c.after);
            return true;
        }
        Value r = callee.p && callee.p->callable() ? deref(*this, callee.p->call(*this, c.args)) : Value{};
        if (c.after) {
            if (t.until && !t.after_wait) {
                // The function asked to wait: what follows it does so once the wait is over.
                std::function<Value(Vm&, const Value&)> after = std::move(c.after);
                t.after_wait = [after, r](Vm& vm) { after(vm, r); };
            } else r = c.after(*this, r);
        }
        value = std::move(r); // the value the script sees
    }
    return false;
}

void Vm::run_task(Task& t) {
    struct Running {
        Vm& vm;
        Task* outer;
        Running(Vm& v, Task& t) : vm(v), outer(v.current_) { v.current_ = &t, ++v.nesting_; }
        ~Running() { vm.current_ = outer, --vm.nesting_; }
    } running(*this, t);
    if (nesting_ > 64) throw ScriptError("calls from the program nested too deep");
    t.state = Task::State::Ready;
    t.until = nullptr;
    try {
        while (!t.frames.empty()) {
            Frame& f = *t.frames.back();
            const Step step = execute(t, f);
            if (step == Step::Waiting) {
                t.state = Task::State::Waiting;
                return;
            }
            if (step == Step::Called) continue;
            // The frame returned: its value goes to the caller's stack.
            Value v = f.constructs ? f.object : t.reg;
            std::function<Value(Vm&, const Value&)> after = std::move(f.after);
            t.frames.pop_back();
            if (after) v = after(*this, v);
            // What `after` chained runs in the frame's place; its value is the one passed on.
            if (t.chained && push_chained(t, v)) continue;
            if (t.frames.empty()) {
                t.result = v;
                break;
            }
            t.frames.back()->stack.push_back(std::move(v));
            if (t.until) {
                t.state = Task::State::Waiting;
                return;
            }
        }
    } catch (...) {
        t.frames.clear();
        t.chained.reset();
        t.until = nullptr, t.after_wait = nullptr;
        t.state = Task::State::Failed;
        throw;
    }
    t.state = Task::State::Done;
}

Value Vm::run_to_end(Task& t) {
    t.can_wait = false;
    for (;;) {
        run_task(t);
        if (t.state != Task::State::Waiting) break;
        t.until = nullptr; // a wait in a call that must end at once is passed over
        t.after_wait = nullptr;
    }
    return t.result;
}

Value Vm::invoke(Function& fn, const Value& self, Args args) {
    if (fn.native) return fn.native(*this, self, args);
    Task t;
    push_frame(t, fn, self, args);
    return run_to_end(t);
}

void Vm::run(std::shared_ptr<const Code> code, std::shared_ptr<Module> scope) {
    Task t;
    auto f = std::make_unique<Frame>();
    f->code = std::move(code);
    f->slots.resize(f->code->variables);
    f->scopes.push_back(scope ? std::move(scope) : root_);
    t.frames.push_back(std::move(f));
    run_to_end(t);
}

// Begins a call in a task: a script function gets a frame; one of the program's is called
// here (it may chain a script call or ask to wait). False when nothing is left to run.
bool Vm::begin(Task& t, const Value& callee_, Args args) {
    const Value callee = deref(*this, callee_);
    if (!callee.p || !callee.p->callable()) throw ScriptError("not a function: " + callee.to_string());
    if (auto fn = callee.as<Function>(); fn && !fn->native) {
        push_frame(t, *fn, fn->self, args);
        return true;
    }
    struct Running {
        Vm& vm;
        Task* outer;
        Running(Vm& v, Task& t) : vm(v), outer(v.current_) { v.current_ = &t; }
        ~Running() { vm.current_ = outer; }
    } running(*this, t);
    t.result = deref(*this, callee.p->call(*this, args));
    return t.chained && push_chained(t, t.result);
}

std::shared_ptr<Task> Vm::start(const Value& callee, Args args) {
    auto t = std::make_shared<Task>();
    try {
        if (begin(*t, callee, args)) run_task(*t);
        else t->state = t->until ? Task::State::Waiting : Task::State::Done;
    } catch (const ScriptError& e) {
        t->state = Task::State::Failed, t->error = e.what();
    }
    return t;
}

void Vm::resume(Task& t) {
    if (t.state != Task::State::Waiting) return;
    try {
        t.until = nullptr;
        if (t.after_wait) {
            // What the native that waited does now; a script call it asks for takes the place
            // of the value the native returned.
            std::function<void(Vm&)> then = std::move(t.after_wait);
            t.after_wait = nullptr;
            {
                struct Running {
                    Vm& vm;
                    Task* outer;
                    Running(Vm& v, Task& t) : vm(v), outer(v.current_) { v.current_ = &t; }
                    ~Running() { vm.current_ = outer; }
                } running(*this, t);
                then(*this);
                if (t.chained) {
                    Value v;
                    std::vector<Value>* stack = t.frames.empty() ? nullptr : &t.frames.back()->stack;
                    if (stack && !stack->empty()) v = std::move(stack->back()), stack->pop_back();
                    if (!push_chained(t, v)) {
                        if (stack) stack->push_back(std::move(v));
                        else t.result = std::move(v);
                    }
                }
            }
            if (t.until) return; // it waits again
        }
        if (t.frames.empty()) t.state = Task::State::Done;
        else run_task(t);
    } catch (const ScriptError& e) {
        t.frames.clear();
        t.state = Task::State::Failed, t.error = e.what();
    }
}

void Vm::wait(std::function<bool()> until, std::function<void(Vm&)> then) {
    if (current_ && current_->can_wait) {
        current_->until = std::move(until), current_->after_wait = std::move(then);
        return;
    }
    // In a call that must end at once nothing is waited for: what was to follow happens now.
    if (then) then(*this);
}

void Vm::then_call(const Value& callee, std::vector<Value> args, std::function<Value(Vm&, const Value&)> after) {
    if (current_) {
        current_->chained = Task::Chained{callee, std::move(args), std::move(after)};
        return;
    }
    // Outside any script call, the function is called here.
    const Value r = call(callee, args);
    if (after) after(*this, r);
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
        run(std::make_shared<const Code>(std::move(s.code)), scope);
        if (after_load) after_load(*this, name, scope ? scope : root_);
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
    for (auto s = f.scopes.rbegin(); !at && s != f.scopes.rend(); ++s)
        for (std::shared_ptr<Module> m = *s; !at && m; m = m->parent.lock()) {
            at = m->find(first);
            if (const auto it = m->imported.find(first); !at && it != m->imported.end()) at = it->second;
        }
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

Vm::Step Vm::execute(Task& t, Frame& f) {
    const std::vector<Instruction>& code = f.code->instructions;
    std::vector<Value>& st = f.stack;
    st.reserve(f.code->stack + 4);
    const size_t base_scopes = f.base_scopes;
    size_t& pc = f.pc;
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
                if (st.back().kind == Kind::Reference) {
                    f.evaluated = std::static_pointer_cast<Reference>(st.back().p), f.evaluated_at = pc;
                    // A variable that holds a pointer gives the pointer (`*result = 1` assigns
                    // through it); everything else its value.
                    const auto cell = std::dynamic_pointer_cast<Variable>(st.back().p);
                    if (cell && cell->value.kind == Kind::Reference && dynamic_cast<Pointer*>(cell->value.p.get())) st.back() = cell->value;
                    else st.back() = deref(*this, std::move(st.back()));
                }
                break;
            case Op::AttributePush: {
                // `x.value` of a number or a string is the variable it was just read from
                // (`count.value = 3` assigns `count`: in the program a number is an object and
                // `value` what it holds).
                if (in.text == "value" && f.evaluated && f.evaluated_at + 1 == pc && !st.empty()) {
                    const Kind k = st.back().kind;
                    if (k == Kind::Nil || k == Kind::Int || k == Kind::Float || k == Kind::String) {
                        st.back() = reference(f.evaluated);
                        break;
                    }
                }
                const Value of = deref(*this, pop());
                std::shared_ptr<Reference> r = of.p ? of.p->dotted(*this, of, in.text) : nullptr;
                if (!r) r = member(of, in.text);
                if (!r) fail(f, pc, "no \"" + in.text + "\" in " + of.to_string());
                st.push_back(reference(std::move(r)));
                break;
            }
            case Op::Assign: {
                Value v = keep_variable(*this, pop());
                const Value to = pop();
                if (to.kind != Kind::Reference) fail(f, pc, "assignment to a value");
                // A pointer is kept only by a variable; anything else takes the value.
                const bool pointer = v.kind == Kind::Reference;
                if (pointer && !dynamic_cast<Variable*>(to.p.get())) v = deref(*this, std::move(v));
                static_cast<Reference*>(to.p.get())->set(*this, v);
                st.push_back(deref(*this, std::move(v)));
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
                t.reg = deref(*this, pop());
                break;
            case Op::Call: {
                if (st.size() < in.value) fail(f, pc, "the value stack is short");
                std::vector<Value> raw(st.end() - in.value, st.end());
                st.resize(st.size() - in.value);
                const Value callee = deref(*this, pop());
                if (!callee.p || !callee.p->callable()) fail(f, pc, "not a function: " + callee.to_string());
                // A script function takes a variable pushed as it is as a pointer to it; the
                // program's own functions take values.
                const auto script_fn = callee.as<Function>();
                const bool to_script = script_fn && !script_fn->native;
                std::vector<Value> args;
                args.reserve(raw.size());
                for (Value& x : raw) args.push_back(to_script ? keep_variable(*this, std::move(x)) : deref(*this, std::move(x)));
                // A script function: a frame of its own, so the call can wait.
                if (auto fn = callee.as<Function>(); fn && !fn->native) {
                    ++pc;
                    push_frame(t, *fn, fn->self, args);
                    return Step::Called;
                }
                if (auto cls = callee.as<Module>(); cls && cls->is_class && !cls->native_class()) {
                    const Value self = cls->make_object();
                    const std::shared_ptr<Variable> init = cls->find("__init__");
                    const auto fn = init ? init->value.as<Function>() : nullptr;
                    if (fn && !fn->native) {
                        ++pc;
                        push_frame(t, *fn, self, args);
                        t.frames.back()->constructs = true, t.frames.back()->object = self;
                        return Step::Called;
                    }
                    if (fn) fn->native(*this, self, args);
                    st.push_back(self);
                    break;
                }
                Value r = callee.p->call(*this, args);
                if (t.chained) {
                    ++pc;
                    if (push_chained(t, r)) return Step::Called;
                    --pc;
                }
                st.push_back(std::move(r));
                if (t.until) {
                    ++pc;
                    return Step::Waiting;
                }
                break;
            }
            case Op::BinaryOperator: {
                Value b = deref(*this, pop()), a = deref(*this, pop());
                if (mixed_notes) note_mixed(f, pc, in.text, a, b);
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
                const Value was = deref(*this, r->get(*this));
                if (mixed_notes) note_mixed(f, pc, in.text, was, b);
                Value v = binary(in.text, was, b);
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
                for (const Value& v : pop_many(in.value)) {
                    if (mixed_notes && v.kind == Kind::Float) note_test(f, pc, v, "text of a float");
                    s += v.to_string();
                }
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
            case Op::JumpZero: {
                const Value v = deref(*this, pop());
                if (mixed_notes) note_test(f, pc, v);
                if (!v.truthy()) pc = size_t(in.value) - 1;
                break;
            }
            case Op::JumpNotZero: {
                const Value v = deref(*this, pop());
                if (mixed_notes) note_test(f, pc, v);
                if (v.truthy()) pc = size_t(in.value) - 1;
                break;
            }
            case Op::LogicalAnd: { // a && b: a false value stays as the result; else POP and b follow
                if (st.empty()) fail(f, pc, "the value stack is empty");
                const Value v = deref(*this, st.back());
                if (mixed_notes) note_test(f, pc, v);
                if (!v.truthy()) pc = size_t(in.value) - 1;
                break;
            }
            case Op::LogicalOr: {
                if (st.empty()) fail(f, pc, "the value stack is empty");
                const Value v = deref(*this, st.back());
                if (mixed_notes) note_test(f, pc, v);
                if (v.truthy()) pc = size_t(in.value) - 1;
                break;
            }
            case Op::ModuleDefine:
                f.scopes.push_back(module_at(f, in.symbols, false, {}, {}));
                break;
            case Op::ClassDefine:
                f.scopes.push_back(module_at(f, {}, true, in.text, in.symbols));
                break;
            case Op::SetState:
                // 0 ends a module's or class's block (the file's, at the top); 1 returns.
                if (in.value == 0 && f.scopes.size() > base_scopes) f.scopes.pop_back();
                else return Step::Returned;
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
                else if (std::shared_ptr<Reference> r = member(m, in.text)) {
                    // (In a module's body the name is in reach of the module's functions; so what one of its functions imports
                    // is in the others': Prologue's onLoad imports MMovieFace for its pages.)
                    if (f.scopes.size() <= f.base_scopes) f.imported[in.text] = r;
                    f.scopes.back()->imported[in.text] = std::move(r);
                }
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
    return Step::Returned;
}

// ---------------------------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------------------------

namespace {

const char* kind_word(const Value& v) {
    switch (v.kind) {
    case Kind::Nil: return "nil";
    case Kind::Int: return "int";
    case Kind::Float: return "float";
    case Kind::String: return "text";
    case Kind::Array: return "array";
    case Kind::Function: return "function";
    case Kind::Module: return "module";
    default: return "object";
    }
}

void note(const Frame& f, size_t pc, const std::string& what) {
    const u32 line = pc < f.code->instructions.size() ? f.code->instructions[pc].line : 0;
    ++(*mixed_notes)[f.code->source + ":" + std::to_string(line) + " " + what];
}

} // namespace

void Vm::note_mixed(const Frame& f, size_t pc, const std::string& op, const Value& a, const Value& b) {
    if (is_stub(a) || is_stub(b) || op == "[]") return;
    const bool fraction = b.kind == Kind::Float && b.f != std::trunc(b.f);
    if ((a.kind == Kind::Int && (fraction || b.kind == Kind::String || b.kind == Kind::Array || b.kind == Kind::Object || b.kind == Kind::Module)) ||
        (a.kind == Kind::String && b.kind != Kind::String && b.kind != Kind::Nil && op != "+") || (a.kind == Kind::String && b.kind == Kind::Float))
        note(f, pc, std::string(kind_word(a)) + " " + op + " " + kind_word(b));
}

void Vm::note_test(const Frame& f, size_t pc, const Value& v, const char* what) {
    if (what) return note(f, pc, what);
    if (v.kind == Kind::Int || v.kind == Kind::Nil || is_stub(v)) return;
    if (v.kind == Kind::Float && v.f == std::trunc(v.f)) return;
    note(f, pc, std::string("test of a ") + kind_word(v));
}

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
            if (std::shared_ptr<Reference> r = a.kind == Kind::Module ? a.p->element(*this, a, b.as_string()) : member(a, b.as_string())) return reference(std::move(r));
        if (is_stub(a)) return reference(a.p->member(*this, a, "[]"));
        throw ScriptError("no [" + b.to_string() + "] in " + a.to_string());
    }
    // The program asks the left value's class (each operator is a slot of its vtable, 22 to 49: the table 0x619CA8 the loader
    // reads an operator's name into, US v1.01); a nil on either side asks nil's, and nil's == is both nil.
    if (is_stub(a) || is_stub(b)) {
        // Nothing is known of a stand-in's value: it equals itself only, comparisons are false, arithmetic is another.
        if (op == "==") return Value::integer(a.p == b.p);
        if (op == "!=") return Value::integer(a.p != b.p);
        if (op == "<" || op == ">" || op == "<=" || op == ">=") return Value::integer(0);
        return is_stub(a) ? a : b;
    }
    if (a.kind == Kind::Nil || b.kind == Kind::Nil) { // hNil 0x3008B8, 0x300938
        if (op == "==") return Value::integer(a.kind == b.kind);
        if (op == "!=") return Value::integer(a.kind != b.kind);
        throw ScriptError("no " + a.to_string() + " " + op + " " + b.to_string()); // (the program calls through nothing)
    }
    if (a.kind == Kind::Int) {
        // hInt (0x2FCA50...): the operator on the other value's integer, an integer (a float's truncated, a text's strtol, an
        // object's 0): the regulations compare a length limit with the car's getLength text (share/regulation.ad).
        const s32 x = a.i, y = value_int(b);
        const u32 ux = u32(x), uy = u32(y);
        if (op == "+") return Value::integer(s32(ux + uy));
        if (op == "-") return Value::integer(s32(ux - uy));
        if (op == "*") return Value::integer(s32(ux * uy));
        if (op == "/" || op == "%") {
            if (y == 0) throw ScriptError("division by zero"); // (the program traps: break 7)
            if (x == INT32_MIN && y == -1) return Value::integer(op == "/" ? x : 0);
            return Value::integer(op == "/" ? x / y : x % y);
        }
        if (op == "**") // ❓ powf of the two as floats, truncated (0x2FCDA8 -> 0x57D6C0); the host's powf here
            return Value::integer(ee::fast::to_int(ee::fast::bits(std::pow(value_float(a), value_float(Value::integer(y))))));
        if (op == "==") return Value::integer(x == y);
        if (op == "!=") return Value::integer(x != y);
        if (op == "<") return Value::integer(x < y);
        if (op == ">") return Value::integer(x > y);
        if (op == "<=") return Value::integer(x <= y);
        if (op == ">=") return Value::integer(x >= y);
        if (op == "&") return Value::integer(x & y);
        if (op == "|") return Value::integer(x | y);
        if (op == "^") return Value::integer(x ^ y);
        if (op == "<<") return Value::integer(s32(ux << (uy & 31)));
        if (op == ">>") return Value::integer(x >> (uy & 31));
    } else if (a.kind == Kind::Float) {
        // hFloat (0x2F7D48...): the EE's arithmetic and comparisons with the other value's float; no bit operators.
        const ee::F x = ee::F::raw(ee::fast::bits(a.f)), y = ee::F::raw(ee::fast::bits(value_float(b)));
        const auto number = [](ee::F v) { return Value::number(ee::fast::host(v.bits)); };
        if (op == "+") return number(x + y);
        if (op == "-") return number(x - y);
        if (op == "*") return number(x * y);
        if (op == "/") return number(x / y);
        if (op == "%") return number(ee::F::raw(ee_fmod(x.bits, y.bits)));
        if (op == "**") return Value::number(std::pow(a.f, ee::fast::host(y.bits))); // ❓ the host's powf (0x57D6C0)
        if (op == "==") return Value::integer(x == y);
        if (op == "!=") return Value::integer(x != y);
        if (op == "<") return Value::integer(x < y);
        if (op == ">") return Value::integer(x > y);
        if (op == "<=") return Value::integer(x <= y);
        if (op == ">=") return Value::integer(x >= y);
    } else if (a.kind == Kind::String) {
        // hString (0x3124F8...): the other value as text (an int "%d", a float "%f"): joined, taken out, compared.
        const std::string& x = a.as_string();
        const std::string y = b.kind == Kind::String ? b.as_string() : b.to_string();
        if (op == "+") return Value::string(x + y);
        if (op == "-") { // the first place it is found taken out; the text itself when it is not there
            const size_t at = x.find(y);
            return at == std::string::npos ? a : Value::string(x.substr(0, at) + x.substr(at + y.size()));
        }
        const int c = x.compare(y);
        if (op == "==") return Value::integer(c == 0);
        if (op == "!=") return Value::integer(c != 0);
        if (op == "<") return Value::integer(c < 0);
        if (op == ">") return Value::integer(c > 0);
        if (op == "<=") return Value::integer(c <= 0);
        if (op == ">=") return Value::integer(c >= 0);
    } else {
        // Any other value (an array, an object, a module, a function): == and != are the same value or not (hObject's,
        // 0x309A50 / 0x309AD0).
        if (op == "==") return Value::integer(equal(a, b));
        if (op == "!=") return Value::integer(!equal(a, b));
        // ❓ Two arrays joined: the program's arrays have no + of their own (hObject's names one with no function).
        if (op == "+" && a.kind == Kind::Array && b.kind == Kind::Array) {
            std::vector<Value> v = *a.items();
            v.insert(v.end(), b.items()->begin(), b.items()->end());
            return Value::array(std::move(v));
        }
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
