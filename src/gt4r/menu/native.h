#pragma once

// Objects of the program's own classes as the scripts see them (`menu::MFadeActor`,
// `main::game`, the contexts): attributes with real values or accessors, and methods. A member
// that is not written is a recording stand-in (`gt4tool menu-run ... stubs` lists them), so a
// script goes on and what it asked for is known.

#include "gt4r/script/vm.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace gt4r::menu {

// An attribute read and written through functions.
struct Accessor : script::Reference {
    std::function<script::Value()> read;
    std::function<void(const script::Value&)> write; // none: the attribute cannot be assigned
    script::Value get(script::Vm&) override { return read ? read() : script::Value{}; }
    void set(script::Vm&, const script::Value& v) override {
        if (write) write(v);
    }
    void clear() override { read = nullptr, write = nullptr; }
};

struct Object : script::Heap {
    std::string label; // its class: "MFadeActor"
    std::map<std::string, std::shared_ptr<script::Reference>> fields;

    explicit Object(std::string l) : label(std::move(l)) {}
    std::shared_ptr<script::Reference> member(script::Vm& vm, const script::Value&, const std::string& name) override {
        std::shared_ptr<script::Reference>& cell = fields[name];
        if (!cell) cell = std::make_shared<script::Variable>(vm.stub_natives ? vm.make_stub(label + "." + name) : script::Value{});
        return cell;
    }
    std::string describe() const override { return "<" + label + ">"; }
    void clear() override {
        auto held = std::move(fields);
        fields.clear();
        for (auto& [n, cell] : held)
            if (cell) cell->clear();
    }

    // A plain attribute.
    void set(const std::string& name, script::Value v) { fields[name] = std::make_shared<script::Variable>(std::move(v)); }
    script::Value get(const std::string& name) const {
        const auto it = fields.find(name);
        if (it == fields.end()) return {};
        if (auto v = std::dynamic_pointer_cast<script::Variable>(it->second)) return v->value;
        if (auto a = std::dynamic_pointer_cast<Accessor>(it->second)) return a->read ? a->read() : script::Value{};
        return {};
    }
    float number(const std::string& name, float fallback = 0) const {
        const script::Value v = get(name);
        return v.is_number() ? v.as_float() : fallback;
    }
    void method(const std::string& name, script::NativeFunction fn) { set(name, script::Vm::native(label + "." + name, std::move(fn))); }
    // An attribute behind functions.
    void property(const std::string& name, std::function<script::Value()> read, std::function<void(const script::Value&)> write = {}) {
        auto a = std::make_shared<Accessor>();
        a->read = std::move(read), a->write = std::move(write);
        fields[name] = std::move(a);
    }
};

inline script::Value object_value(const std::shared_ptr<script::Heap>& p) { return script::Value::of(script::Kind::Object, p); }

// Arguments as numbers and text, with a fallback when missing or of another kind.
inline float arg_number(script::Args a, size_t i, float fallback = 0) { return i < a.size() && a[i].is_number() ? a[i].as_float() : fallback; }
inline s32 arg_int(script::Args a, size_t i, s32 fallback = 0) {
    if (i >= a.size()) return fallback;
    return a[i].kind == script::Kind::Int ? a[i].i : a[i].kind == script::Kind::Float ? s32(a[i].f) : fallback;
}
inline std::string arg_text(script::Args a, size_t i, const std::string& fallback = {}) {
    return i < a.size() && a[i].kind == script::Kind::String ? a[i].as_string() : fallback;
}

} // namespace gt4r::menu
