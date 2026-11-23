// The script interpreter on blocks built here, then on the discs' own scripts.

#include "test_framework.h"

#include "gt4r/script/vm.h"

using namespace gt4r;
using script::Op;

namespace {

script::Instruction in(Op op, u32 value = 0) {
    script::Instruction i;
    i.op = op, i.value = value, i.line = 1;
    return i;
}
script::Instruction in(Op op, std::string text) {
    script::Instruction i;
    i.op = op, i.text = std::move(text), i.line = 1;
    return i;
}
script::Instruction num(float v) {
    script::Instruction i;
    i.op = Op::FloatConst, i.number = v, i.line = 1;
    return i;
}
// A block's own variable, or (more names) a name in reach.
script::Instruction var(std::vector<std::string> path, u32 slot) {
    script::Instruction i;
    i.op = Op::VariablePush, i.symbols = std::move(path), i.value = slot, i.line = 1;
    return i;
}
script::Instruction name(u32 slot, const std::string& n) { return var({n, n}, slot); }
script::Instruction module(std::vector<std::string> path) {
    script::Instruction i;
    i.op = Op::ModuleDefine, i.symbols = std::move(path), i.line = 1;
    return i;
}
script::Instruction function(Op op, std::string n, std::vector<std::string> params, u32 variables,
                             std::vector<script::Instruction> body) {
    script::Code c;
    c.source = "demo.ad", c.parameters = std::move(params), c.variables = variables, c.stack = 8, c.instructions = std::move(body);
    script::Instruction i;
    i.op = op, i.text = std::move(n), i.body = std::make_shared<const script::Code>(std::move(c)), i.line = 1;
    return i;
}
std::shared_ptr<const script::Code> block(u32 variables, std::vector<script::Instruction> body) {
    script::Code c;
    c.source = "demo.ad", c.variables = variables, c.stack = 16, c.instructions = std::move(body);
    return std::make_shared<const script::Code>(std::move(c));
}

} // namespace

// module demo {
//   class Point : Object { attribute x; attribute y;
//     method __init__(ax, ay) { x = ax; y = ay; }   method sum() { return x + y; } }
//   function twice(v) { return v * 2; }
//   static p = Point(3, 4);   static total = twice(p.sum()) + [10, 20, 30][1];
//   static n = 0;  for (var i = 0; i < 5; i++) n += i;
//   static text = "a" + 1 + "b";   static both = 0 && missing();   static either = 7 || missing();
//   static list = [];  list.push(5);  list.push("x");  [first, second] = list;
// }
TEST(vm_runs_a_block) {
    using I = script::Instruction;
    std::vector<I> point = {
        in(Op::AttributeDefine, "x"), in(Op::AttributeDefine, "y"),
        function(Op::MethodDefine, "__init__", {"ax", "ay"}, 5,
                 {name(3, "x"), var({"ax"}, 0), in(Op::Eval), in(Op::Assign), in(Op::Pop),
                  name(4, "y"), var({"ay"}, 1), in(Op::Eval), in(Op::Assign), in(Op::Pop), in(Op::SetState, 1)}),
        function(Op::MethodDefine, "sum", {}, 3,
                 {name(1, "x"), in(Op::Eval), name(2, "y"), in(Op::Eval), in(Op::BinaryOperator, "+"), in(Op::Pop), in(Op::SetState, 1)}),
        in(Op::SetState, 0)};
    std::vector<I> body = {module({"demo"})};
    I cls;
    cls.op = Op::ClassDefine, cls.text = "Point", cls.symbols = {"Object"};
    body.push_back(cls);
    body.insert(body.end(), point.begin(), point.end());
    auto add = [&](std::vector<I> more) { body.insert(body.end(), more.begin(), more.end()); };
    // A jump whose target is set once the place is reached.
    auto jump = [&](Op op) {
        body.push_back(in(op));
        return body.size() - 1;
    };
    auto lands = [&](size_t at) { body[at].value = u32(body.size()); };
    add({function(Op::FunctionDefine, "twice", {"v"}, 2,
                  {var({"v"}, 0), in(Op::Eval), in(Op::IntConst, 2), in(Op::BinaryOperator, "*"), in(Op::Pop), in(Op::SetState, 1)}),
         // static p = Point(3, 4)
         in(Op::StaticDefine, "p"), name(1, "p"), name(2, "Point"), in(Op::Eval), in(Op::IntConst, 3), in(Op::IntConst, 4), in(Op::Call, 2), in(Op::Eval),
         in(Op::Assign), in(Op::Pop),
         // static total = twice(p.sum()) + [10, 20, 30][1]
         in(Op::StaticDefine, "total"), name(3, "total"), name(4, "twice"), in(Op::Eval), name(1, "p"), in(Op::Eval), in(Op::AttributePush, "sum"), in(Op::Eval),
         in(Op::Call, 0), in(Op::Eval), in(Op::Call, 1), in(Op::Eval), in(Op::IntConst, 10), in(Op::IntConst, 20), in(Op::IntConst, 30), in(Op::ArrayPush, 3),
         in(Op::IntConst, 1), in(Op::BinaryOperator, "[]"), in(Op::Eval), in(Op::BinaryOperator, "+"), in(Op::Assign), in(Op::Pop),
         // static n = 0; for (i = 0; i < 5; i++) n += i
         in(Op::StaticDefine, "n"), name(5, "n"), in(Op::IntConst, 0), in(Op::Assign), in(Op::Pop),
         var({"i"}, 6), in(Op::IntConst, 0), in(Op::Assign), in(Op::Pop)});
    const size_t loop = body.size();
    add({var({"i"}, 6), in(Op::Eval), in(Op::IntConst, 5), in(Op::BinaryOperator, "<")});
    const size_t out = jump(Op::JumpZero);
    add({name(5, "n"), var({"i"}, 6), in(Op::Eval), in(Op::BinaryAssignOperator, "+"), in(Op::Pop),
         var({"i"}, 6), in(Op::UnaryAssignOperator, "@++"), in(Op::Pop), in(Op::Jump, u32(loop))});
    lands(out);
    // static text = "a" + 1 + "b"
    add({in(Op::StaticDefine, "text"), name(7, "text"), in(Op::StringConst, "a"), in(Op::IntConst, 1), in(Op::StringConst, "b"), in(Op::StringPush, 3),
         in(Op::Assign), in(Op::Pop),
         // static both = 0 && missing()   (the call would fail: "missing" is not there)
         in(Op::StaticDefine, "both"), name(8, "both"), in(Op::IntConst, 0)});
    const size_t skip_and = jump(Op::LogicalAnd);
    add({in(Op::Pop), name(9, "missing"), in(Op::Eval), in(Op::Call, 0)});
    lands(skip_and);
    // static either = 7 || missing()
    add({in(Op::Assign), in(Op::Pop), in(Op::StaticDefine, "either"), name(10, "either"), in(Op::IntConst, 7)});
    const size_t skip_or = jump(Op::LogicalOr);
    add({in(Op::Pop), name(9, "missing"), in(Op::Eval), in(Op::Call, 0)});
    lands(skip_or);
    add({in(Op::Assign), in(Op::Pop),
         // static list = []; list.push(5); list.push("x"); [first, second] = list
         in(Op::StaticDefine, "list"), name(11, "list"), in(Op::ArrayPush, 0), in(Op::Assign), in(Op::Pop),
         name(11, "list"), in(Op::Eval), in(Op::AttributePush, "push"), in(Op::Eval), in(Op::IntConst, 5), in(Op::Call, 1), in(Op::Eval), in(Op::Pop),
         name(11, "list"), in(Op::Eval), in(Op::AttributePush, "push"), in(Op::Eval), in(Op::StringConst, "x"), in(Op::Call, 1), in(Op::Eval), in(Op::Pop),
         in(Op::StaticDefine, "first"), in(Op::StaticDefine, "second"),
         name(12, "first"), name(13, "second"), name(11, "list"), in(Op::Eval), in(Op::ListAssign, 2), in(Op::Pop),
         in(Op::StringConst, "done\n"), in(Op::Print, 1), in(Op::Pop),
         // static half = 2.5 * 2
         in(Op::StaticDefine, "half"), name(0, "half"), num(2.5f), in(Op::IntConst, 2), in(Op::BinaryOperator, "*"), in(Op::Assign), in(Op::Pop),
         in(Op::SetState, 0), in(Op::SetState, 0)});

    script::Vm vm;
    vm.run(block(14, body));
    CHECK(vm.lookup("demo::total").kind == script::Kind::Int && vm.lookup("demo::total").i == 34);
    CHECK(vm.lookup("demo::n").i == 10 && vm.lookup("demo::half").f == 5.0f);
    CHECK(vm.lookup("demo::text").as_string() == "a1b");
    CHECK(vm.lookup("demo::both").kind == script::Kind::Int && vm.lookup("demo::both").i == 0 && vm.lookup("demo::either").i == 7);
    CHECK(vm.lookup("demo::first").i == 5 && vm.lookup("demo::second").as_string() == "x");
    CHECK(vm.attribute(vm.lookup("demo::p"), "y").i == 4 && vm.attribute(vm.lookup("demo::list"), "size").i == 2);
    CHECK(vm.lookup("main::demo::Point").kind == script::Kind::Module && vm.lookup("demo::nothing").is_nil());
    CHECK(vm.output.size() == 1 && vm.output[0] == "done\n");
    // A function called from here, and a method through its object.
    const script::Value args[1] = {script::Value::number(1.5f)};
    const script::Value r = vm.call(vm.lookup("demo::twice"), args);
    CHECK(r.kind == script::Kind::Float && r.f == 3.0f);
    CHECK(vm.call(vm.attribute(vm.lookup("demo::p"), "sum"), {}).i == 7);
}

TEST(vm_operators_and_builtins) {
    script::Vm vm;
    using V = script::Value;
    CHECK(vm.binary("/", V::integer(7), V::integer(2)).i == 3 && vm.binary("/", V::integer(7), V::number(2)).f == 3.5f);
    CHECK(vm.binary("%", V::integer(-7), V::integer(3)).i == -1 && vm.binary("<<", V::integer(1), V::integer(4)).i == 16);
    CHECK(vm.binary("+", V::string("Grid"), V::integer(2)).as_string() == "Grid2");
    CHECK(vm.binary("==", V::string("a"), V::string("a")).i == 1 && vm.binary("!=", V{}, V::integer(0)).i == 1);
    CHECK(vm.binary("==", V::integer(2), V::number(2)).i == 1 && vm.binary("<", V::string("a"), V::string("b")).i == 1);
    CHECK(vm.unary("!", V{}).i == 1 && vm.unary("-@", V::integer(5)).i == -5 && vm.unary("~", V::integer(0)).i == -1);
    bool threw = false;
    try {
        vm.binary("/", V::integer(1), V::integer(0));
    } catch (const script::ScriptError&) {
        threw = true;
    }
    CHECK(threw);
    auto call = [&](const V& of, const char* method, std::vector<V> args) { return vm.call(vm.attribute(of, method), args); };
    CHECK(call(V::string("r_tsukuba"), "substr", {V::integer(0), V::integer(2)}).as_string() == "r_");
    CHECK(call(V::string("1abc"), "at", {V::integer(0)}).i == 49);
    CHECK(call(V::string("%d laps, %s, %.1f"), "format", {V::integer(3), V::string("wet"), V::number(2.25f)}).as_string() == "3 laps, wet, 2.2");
    // The program's own conversion: a whole number with a point before its last digits.
    CHECK(call(V::string("%.1t%%"), "format", {V::integer(123)}).as_string() == "12.3%" && call(V::string("%.2t"), "format", {V::integer(-5)}).as_string() == "-0.05");
    CHECK(call(V::string("%t|%.3t"), "format", {V::integer(42), V::integer(1234567)}).as_string() == "42|1234.567");
    CHECK(call(V::string("a,b,c"), "split", {V::string(",")}).items()->size() == 3);
    CHECK(call(V::integer(12), "toString", {}).as_string() == "12" && call(V::string("42"), "toInt", {}).i == 42);
    const V list = V::array({V::integer(3), V::integer(1), V::integer(2)});
    call(list, "sort", {});
    CHECK(call(list, "join", {V::string("-")}).as_string() == "1-2-3" && call(list, "pop", {}).i == 3 && call(list, "shift", {}).i == 1);
    CHECK(vm.attribute(list, "size").i == 1);
    // A module's own: defineStatic, defined, name.
    const V root = V::of(script::Kind::Module, vm.root());
    call(root, "defineStatic", {V::string("answer"), V::integer(42)});
    CHECK(vm.lookup("answer").i == 42 && call(root, "defined", {V::string("answer")}).i == 1 && call(root, "defined", {V::string("no")}).i == 0);
    // A native function, and a stand-in for what is not there.
    vm.root()->define("add", script::Vm::native("add", [](script::Vm&, const V&, script::Args a) { return V::integer(a[0].i + a[1].i); }));
    CHECK(call(root, "add", {V::integer(2), V::integer(3)}).i == 5);
    CHECK(vm.attribute(root, "missing").is_nil());
    vm.stub_natives = true;
    const V stub = vm.attribute(root, "missing");
    CHECK(stub.kind == script::Kind::Object && call(stub, "anything", {}).kind == script::Kind::Object && vm.stub_uses.count("missing.anything()"));
    // A script that never ends stops.
    vm.budget = 1000;
    threw = false;
    try {
        vm.run(block(0, {in(Op::Jump, 0)}));
    } catch (const script::ScriptError&) {
        threw = true;
    }
    CHECK(threw);
}

// A call that waits is left and gone on with later, at any depth of calls:
//   function inner() { pause(); return 10; }
//   function outer() { step = 1; pause(); step = 2; return inner() + 1; }
//   function init(page) { return 3; }   // called by the native "open" in its place
//   function opener() { return open() + 1; }
TEST(vm_tasks_wait_and_go_on) {
    using V = script::Value;
    script::Vm vm;
    int waits = 0;
    bool go = false;
    vm.root()->define("pause", script::Vm::native("pause", [&](script::Vm& v, const V&, script::Args) {
        ++waits;
        go = false;
        v.wait([&] { return go; });
        return V{};
    }));
    vm.root()->define("open", script::Vm::native("open", [&](script::Vm& v, const V&, script::Args) {
        v.then_call(v.lookup("init"), {V::integer(1)}, [](script::Vm&, const V& r) { return V::integer(r.i * 2); });
        return V{};
    }));
    vm.run(block(4, {
        in(Op::StaticDefine, "step"),
        function(Op::FunctionDefine, "inner", {}, 2,
                 {name(1, "pause"), in(Op::Eval), in(Op::Call, 0), in(Op::Pop), in(Op::IntConst, 10), in(Op::Pop), in(Op::SetState, 1)}),
        function(Op::FunctionDefine, "outer", {}, 4,
                 {name(1, "step"), in(Op::IntConst, 1), in(Op::Assign), in(Op::Pop),
                  name(2, "pause"), in(Op::Eval), in(Op::Call, 0), in(Op::Pop),
                  name(1, "step"), in(Op::IntConst, 2), in(Op::Assign), in(Op::Pop),
                  name(3, "inner"), in(Op::Eval), in(Op::Call, 0), in(Op::Eval), in(Op::IntConst, 1), in(Op::BinaryOperator, "+"), in(Op::Pop),
                  in(Op::SetState, 1)}),
        function(Op::FunctionDefine, "init", {"page"}, 2, {in(Op::IntConst, 3), in(Op::Pop), in(Op::SetState, 1)}),
        function(Op::FunctionDefine, "opener", {}, 2,
                 {name(1, "open"), in(Op::Eval), in(Op::Call, 0), in(Op::Eval), in(Op::IntConst, 1), in(Op::BinaryOperator, "+"), in(Op::Pop),
                  in(Op::SetState, 1)}),
    }));
    const std::shared_ptr<script::Task> t = vm.start(vm.lookup("outer"), {});
    CHECK(t->state == script::Task::State::Waiting && vm.lookup("step").i == 1 && waits == 1);
    CHECK(t->until && !t->until());
    go = true;
    CHECK(t->until());
    vm.resume(*t);
    CHECK(t->state == script::Task::State::Waiting && vm.lookup("step").i == 2 && waits == 2); // inside inner()
    vm.resume(*t);
    CHECK(t->state == script::Task::State::Done && t->result.i == 11);
    // The same call made from the program's side ends at once: its waits are passed over.
    CHECK(vm.call(vm.lookup("outer"), {}).i == 11 && waits == 4);
    // A native's chained call: the script function runs in the native's place.
    const std::shared_ptr<script::Task> o = vm.start(vm.lookup("opener"), {});
    CHECK(o->state == script::Task::State::Done && o->result.i == 7);
    CHECK(vm.call(vm.lookup("opener"), {}).i == 7 && vm.call(vm.lookup("open"), {}).i == 6);
    // A script error leaves the task failed, and the interpreter usable.
    vm.root()->define("broken", script::Vm::native("broken", [](script::Vm&, const V&, script::Args) -> V { throw script::ScriptError("no"); }));
    const std::shared_ptr<script::Task> bad = vm.start(vm.lookup("broken"), {});
    CHECK(bad->state == script::Task::State::Failed && bad->error == "no");
    CHECK(vm.call(vm.lookup("init"), {}).i == 3);
}

// A variable pushed without EVAL as an argument is the variable itself: the function sets it.
//   function set(out) { *out = 7; }            (VARIABLE_PUSH out, EVAL, 7, ASSIGN)
//   function relay(out) { set(out); }          (the pointer handed on)
//   function caller() { result = 1; relay(&result); other = result + 1; return other; }
TEST(vm_variables_passed_as_themselves) {
    using V = script::Value;
    script::Vm vm;
    V seen;
    vm.root()->define("show", script::Vm::native("show", [&](script::Vm&, const V&, script::Args a) {
        seen = a[0]; // the program's own functions are given the value
        return V{};
    }));
    vm.run(block(2, {
        function(Op::FunctionDefine, "set", {"out"}, 2,
                 {var({"out"}, 0), in(Op::Eval), in(Op::IntConst, 7), in(Op::Assign), in(Op::Pop), in(Op::SetState, 1)}),
        function(Op::FunctionDefine, "relay", {"out"}, 4,
                 {name(2, "set"), in(Op::Eval), var({"out"}, 0), in(Op::Eval), in(Op::Call, 1), in(Op::Pop),
                  name(3, "show"), in(Op::Eval), var({"out"}, 0), in(Op::Eval), in(Op::Call, 1), in(Op::Pop), in(Op::SetState, 1)}),
        function(Op::FunctionDefine, "caller", {}, 4,
                 {var({"result"}, 1), in(Op::IntConst, 1), in(Op::Assign), in(Op::Pop),
                  name(2, "relay"), in(Op::Eval), var({"result"}, 1), in(Op::Call, 1), in(Op::Pop),
                  var({"other"}, 3), var({"result"}, 1), in(Op::Eval), in(Op::IntConst, 1), in(Op::BinaryOperator, "+"), in(Op::Assign), in(Op::Pop),
                  var({"other"}, 3), in(Op::Eval), in(Op::Pop), in(Op::SetState, 1)}),
    }));
    CHECK(vm.call(vm.lookup("caller"), {}).i == 8);
    CHECK(seen.kind == script::Kind::Int && seen.i == 7);
}

// Every script of the discs runs its top level (the program's own classes stood in for), and
// the boot project's course list is what its script builds.
TEST(disc_scripts_run) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        auto loader = [&](const std::string& n) -> std::optional<Bytes> {
            std::string path = n;
            while (!path.empty() && path[0] == '/') path.erase(0, 1);
            const gt3r::Volume::Entry* e = disc->volume().find(path + ".adc");
            if (!e) return std::nullopt;
            return disc->volume().read(*e);
        };
        size_t ran = 0, stopped = 0;
        for (const gt3r::Volume::Entry& e : disc->volume().entries()) {
            if (e.path.size() < 4 || e.path.compare(e.path.size() - 4, 4, ".adc") != 0) continue;
            if (script::Script::version_of(disc->volume().read(e)) != 5) continue;
            script::Vm vm(loader);
            vm.stub_natives = true, vm.budget = 5'000'000; // a stand-in's answers can keep a loop going
            try {
                vm.require("/" + e.path.substr(0, e.path.size() - 4));
                ++ran;
            } catch (const script::ScriptError&) {
                ++stopped;
            }
        }
        std::printf("  %s: %zu scripts ran their top level, %zu stopped\n", disc->info().serial.c_str(), ran, stopped);
        CHECK(ran > 0 && stopped * 20 <= ran);
        if (disc->info().game != Game::GT4) continue;
        script::Vm vm(loader);
        vm.stub_natives = true;
        vm.require("/projects/boot/boot");
        const script::Value akasaka = vm.lookup("BootProject::CourseData::akasaka");
        CHECK(akasaka.kind == script::Kind::Object && vm.attribute(akasaka, "code").as_string() == "akasaka");
        CHECK(std::abs(vm.attribute(akasaka, "length").f - 5116.7f) < 0.01f && vm.attribute(akasaka, "has_reverse").i == 1);
        CHECK(vm.lookup("BootProject::openConfirmDialog").kind == script::Kind::Function);
    }
}
