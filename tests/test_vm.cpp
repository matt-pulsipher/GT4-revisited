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
