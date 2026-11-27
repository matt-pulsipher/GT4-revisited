#pragma once

// The script interpreter: runs the game's compiled scripts (adc.h). What each instruction does
// is in docs/formats/script.md. The game's own classes the scripts drive (`menu::MSound`, the
// widgets) are added as native modules; names nothing provides can stand in as recording stubs
// (`stub_natives`), which is how the functions a script needs are listed.

#include "gt4r/script/adc.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace gt4r::script {

class Vm;
struct Frame;
struct Heap;
struct Reference;
struct Variable;
struct Module;
struct Function;

// A script that cannot go on: a name that is not there, a call of something that is no function.
struct ScriptError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class Kind : u8 { Nil, Int, Float, String, Array, Object, Module, Function, Reference };

struct Value {
    Kind kind = Kind::Nil;
    s32 i = 0;
    float f = 0;
    std::shared_ptr<Heap> p; // every kind from String on

    static Value integer(s32 v) {
        Value r;
        r.kind = Kind::Int, r.i = v;
        return r;
    }
    static Value number(float v) {
        Value r;
        r.kind = Kind::Float, r.f = v;
        return r;
    }
    static Value string(std::string s);
    static Value array(std::vector<Value> items);
    static Value of(Kind kind, std::shared_ptr<Heap> p) {
        Value r;
        r.kind = kind, r.p = std::move(p);
        return r;
    }

    bool is_nil() const { return kind == Kind::Nil; }
    bool is_number() const { return kind == Kind::Int || kind == Kind::Float; }
    bool truthy() const; // as JUMP_ZERO, &&, || and ! test it: value_int(*this) != 0
    float as_float() const { return kind == Kind::Int ? float(i) : f; }
    const std::string& as_string() const; // of a String; "" otherwise
    std::string to_string() const;        // as PRINT and string concatenation write it
    std::vector<Value>* items() const;    // of an Array; null otherwise
    template <class T> std::shared_ptr<T> as() const { return std::dynamic_pointer_cast<T>(p); }
};

bool equal(const Value& a, const Value& b);

// The integer and the float every value gives the program's operators and tests (its value classes' vtable slots 11 and
// 12, US v1.01): an int's own, a float's truncated (the EE's cvt.w.s) or converted, a string's strtol base 10 / atof; any
// other value's 0.
s32 value_int(const Value& v);
float value_float(const Value& v);

// A diagnostic for the tools: when set, each place where a script's operator or test meets kinds that would follow from a
// native of ours giving another kind than the program's (an int times a fraction, an int against a text, a text and a
// float, a test of a float, a text or an object) is counted by "file:line what". GT4R_VM_NOTES=<file> sets it for a
// whole run and writes the counts there at its end.
extern std::map<std::string, size_t>* mixed_notes;

using Args = std::span<const Value>;

struct Heap : std::enable_shared_from_this<Heap> {
    virtual ~Heap() = default;
    // A member by name, as something to read and assign; null when there is none. `self` is the
    // value it is asked of (a method found this way is bound to it).
    virtual std::shared_ptr<Reference> member(Vm&, const Value& self, const std::string& name) { return nullptr; }
    // value["name"]: the member, where a value answers differently for a name that is not there
    // (a widget without that child gives nil).
    virtual std::shared_ptr<Reference> element(Vm& vm, const Value& self, const std::string& name) { return member(vm, self, name); }
    // value.name: the member, where `.` and `::` name different things (a scale bar's state `popup` and its child of that name).
    virtual std::shared_ptr<Reference> dotted(Vm& vm, const Value& self, const std::string& name) { return member(vm, self, name); }
    virtual bool callable() const { return false; }
    virtual Value call(Vm&, Args) { throw ScriptError("not a function: " + describe()); }
    virtual std::string describe() const = 0;
    virtual void clear() {} // drops what it holds (the interpreter's teardown)
};

struct Text : Heap {
    std::string s;
    std::string describe() const override { return s; }
};

struct Array : Heap {
    std::vector<Value> items;
    std::string describe() const override;
    void clear() override { items.clear(); }
};

// What VARIABLE_PUSH, ATTRIBUTE_PUSH and `[]` leave on the stack: EVAL reads it, ASSIGN writes it.
struct Reference : Heap {
    virtual Value get(Vm&) = 0;
    virtual void set(Vm&, const Value&) = 0;
    std::string describe() const override { return "<reference>"; }
};

struct Variable : Reference {
    Value value;
    Variable() = default;
    explicit Variable(Value v) : value(std::move(v)) {}
    Value get(Vm&) override { return value; }
    void set(Vm&, const Value& v) override { value = v; }
    void clear() override { value = {}; }
};

// A module, or a class (a module that makes objects).
struct Module : Heap {
    std::string name;
    std::weak_ptr<Module> parent;
    bool is_class = false;
    std::shared_ptr<Module> base;        // a class's
    std::vector<std::string> attributes; // a class's ATTRIBUTE_DEFINEs
    std::map<std::string, std::shared_ptr<Variable>> members;
    std::map<std::string, std::shared_ptr<Reference>> imported; // IMPORT in its body: names its functions reach too
    bool stubbed = false; // a native module nothing provides: any name asked of it is a stub
    // A native class's objects (null: plain script objects).
    std::function<Value(Vm&, Args)> construct;

    std::shared_ptr<Variable> find(const std::string& name) const; // its own, then its bases'
    std::shared_ptr<Variable> define(const std::string& name, Value v = {});
    std::string path() const; // "BootProject::MovieRoot"
    std::shared_ptr<Reference> member(Vm&, const Value& self, const std::string& name) override;
    bool callable() const override { return is_class; }
    Value call(Vm&, Args) override;
    bool native_class() const; // its objects are the program's (it or a base has `construct`)
    Value make_object();       // a script class's object, its attributes nil, before __init__
    std::string describe() const override { return (is_class ? "<class " : "<module ") + path() + ">"; }
    void clear() override;
};

// An object of a script class.
struct Instance : Heap {
    std::shared_ptr<Module> cls;
    std::map<std::string, std::shared_ptr<Variable>> attributes;
    std::shared_ptr<Reference> member(Vm&, const Value& self, const std::string& name) override;
    std::string describe() const override { return "<" + (cls ? cls->path() : std::string("?")) + " object>"; }
    void clear() override { attributes.clear(); }
};

using NativeFunction = std::function<Value(Vm&, const Value& self, Args)>;

// A script function or method, or one of the program's; `self` when it is bound to an object.
struct Function : Heap {
    std::string name;
    std::shared_ptr<const Code> code; // a script's
    std::shared_ptr<Module> scope;    // where it was defined
    bool method = false;
    NativeFunction native;
    Value self;
    bool callable() const override { return true; }
    Value call(Vm&, Args) override;
    std::string describe() const override { return "<function " + name + ">"; }
    void clear() override { scope.reset(), self = {}; }
};

// A stand-in for something of the program's that is not written yet: any member of it is
// another, calling it gives another, and the interpreter counts what was asked of it.
struct Stub : Heap {
    std::string path;
    std::map<std::string, std::shared_ptr<Variable>> members;
    std::shared_ptr<Reference> member(Vm&, const Value& self, const std::string& name) override;
    bool callable() const override { return true; }
    Value call(Vm&, Args) override;
    std::string describe() const override { return "<stub " + path + ">"; }
    void clear() override { members.clear(); }
};

// A script call the interpreter can leave and come back to. The program runs its scripts on
// threads: a call that waits (`context.sync`, a nested event loop) blocks its thread while the
// rest of the program goes on. Here such a call is a task: its frames are kept while it waits.
class Task {
public:
    enum class State { Ready, Waiting, Done, Failed };
    Task();
    ~Task();
    State state = State::Ready;
    Value result;                // the call's value, once Done
    std::string error;           // why it Failed
    std::function<bool()> until; // what a Waiting task waits for (true: go on)
    std::function<void(Vm&)> after_wait; // run when the wait is over, before the script goes on
    bool can_wait = true;        // false: a call made from the program's side that must end at once
    bool finished() const { return state == State::Done || state == State::Failed; }

private:
    friend class Vm;
    std::vector<std::unique_ptr<Frame>> frames;
    Value reg; // the last value popped: what a function returns
    // A script function a native asked to be called in its place (Vm::then_call).
    struct Chained {
        Value callee;
        std::vector<Value> args;
        std::function<Value(Vm&, const Value&)> after;
    };
    std::optional<Chained> chained;
};

class Vm {
public:
    // A script file by the path REQUIRE names ("/script/MenuClassDefine"), without ".adc".
    using Loader = std::function<std::optional<Bytes>(const std::string& path)>;
    // Fills a module with one of the program's own (REQUIRE "menu").
    using NativeModule = std::function<void(Vm&, Module& into)>;

    explicit Vm(Loader loader = {});
    ~Vm();
    Vm(const Vm&) = delete;
    Vm& operator=(const Vm&) = delete;

    const std::shared_ptr<Module>& root() const { return root_; } // "main"
    void add_native_module(const std::string& name, NativeModule fill) { natives_[name] = std::move(fill); }
    static Value native(std::string name, NativeFunction fn);

    // Names nothing provides become stubs instead of errors.
    bool stub_natives = false;
    // Called when a script file has run into a module (Module.load, REQUIRE): its name as asked for and the module.
    std::function<void(Vm&, const std::string& name, const std::shared_ptr<Module>& into)> after_load;
    // What was asked of stubs: "menu::MSound()" -> times.
    std::map<std::string, u32> stub_uses;
    std::vector<std::string> output; // PRINT's lines
    u64 instructions = 0;            // run so far
    u64 budget = 200'000'000;        // ScriptError past this many (a script that never ends)

    // Runs a block's top level in a module (the root when null).
    void run(std::shared_ptr<const Code> code, std::shared_ptr<Module> scope = nullptr);
    // REQUIRE: a native module by name, or a script file ("/config/course").
    void require(const std::string& name, std::shared_ptr<Module> scope = nullptr);
    // Calls a function and runs it to its end; a wait asked for inside is not waited.
    Value call(const Value& callee, Args args);
    Value invoke(Function& fn, const Value& self, Args args);
    // Calls a function as a task: runs it until it ends or waits. Never throws: a script error
    // leaves the task Failed.
    std::shared_ptr<Task> start(const Value& callee, Args args);
    // Goes on with a Waiting task (the caller has seen that what it waited for is over).
    void resume(Task& task);
    // For the program's functions, while a script calls them:
    // the call that is running waits until `until` gives true (nothing happens in a call that
    // cannot wait). `then` runs when the wait is over, before the script goes on: it may ask
    // for a script call (then_call) or for another wait.
    void wait(std::function<bool()> until, std::function<void(Vm&)> then = {});
    bool can_wait() const { return current_ && current_->can_wait; }
    // After the native function returns, `callee` is called in its place, and `after` (when
    // given) makes the value the script sees from the callee's. So a script function called by
    // the program's side can wait like any other.
    void then_call(const Value& callee, std::vector<Value> args, std::function<Value(Vm&, const Value&)> after = {});
    // "BootProject::CourseData::akasaka" from the root; nil when a part is missing.
    Value lookup(const std::string& path);
    // A member's value (nil when the value has none of that name).
    Value attribute(const Value& of, const std::string& name);
    std::shared_ptr<Reference> member(const Value& of, const std::string& name);
    Value make_stub(const std::string& path);
    // A built-in method or value of a string, array, module or any value ("size", "push", "toString").
    std::shared_ptr<Reference> builtin_member(const Value& of, const std::string& name);
    // Module.load: runs a script beside the ones already read, in a module.
    void load_into(const std::shared_ptr<Module>& module, const std::string& name);

    Value binary(const std::string& op, const Value& a, const Value& b);
    Value unary(const std::string& op, const Value& a);

private:
    enum class Step { Returned, Called, Waiting };
    Step execute(Task& t, Frame& f);
    bool begin(Task& t, const Value& callee, Args args);
    void run_task(Task& t);          // until it ends or waits; throws ScriptError
    Value run_to_end(Task& t);       // waits are passed over
    void push_frame(Task& t, const Function& fn, const Value& self, Args args);
    bool push_chained(Task& t, Value& value);
    std::shared_ptr<Reference> resolve(Frame& f, const std::vector<std::string>& symbols);
    std::shared_ptr<Module> module_at(Frame& f, const std::vector<std::string>& symbols, bool as_class,
                                      const std::string& class_name, const std::vector<std::string>& base);
    std::shared_ptr<Variable> find_name(Frame& f, const std::string& name);
    [[noreturn]] void fail(const Frame& f, size_t pc, const std::string& what) const;
    void note_mixed(const Frame& f, size_t pc, const std::string& op, const Value& a, const Value& b); // (mixed_notes)
    void note_test(const Frame& f, size_t pc, const Value& v, const char* what = nullptr);

    Loader loader_;
    std::map<std::string, NativeModule> natives_;
    std::shared_ptr<Module> root_;
    std::vector<std::string> folders_; // of the script files read so far
    Task* current_ = nullptr; // the task whose code is running
    int nesting_ = 0;         // calls from the program's side inside script calls
};

} // namespace gt4r::script
