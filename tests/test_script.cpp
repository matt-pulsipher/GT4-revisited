// The script reader on a made-up file, then on the discs of GT4R_DISC.

#include "test_framework.h"

#include "gt4r/script/adc.h"

#include <cstring>

using namespace gt4r;

namespace {

struct Writer {
    Bytes b;
    void byte(u8 v) { b.push_back(v); }
    void word(u32 v) {
        b.resize(b.size() + 4);
        std::memcpy(b.data() + b.size() - 4, &v, 4);
    }
    void name(const std::string& s) {
        b.push_back(u8(s.size())), b.push_back(u8(s.size() >> 8));
        b.insert(b.end(), s.begin(), s.end());
    }
    void in(u32 line, script::Op op) { word(line), byte(u8(op)); }
};

// module demo; function twice(x) { return x * 2.5; }  demo::value = twice(4) ...
Bytes make_script() {
    Writer w;
    w.b = {'A', 'D', 'C', 'H', '0', '0', '5', 0};
    w.name("demo.ad"), w.word(0), w.word(4), w.word(2), w.word(7);
    w.in(1, script::Op::ModuleDefine), w.word(1), w.name("demo");
    w.in(2, script::Op::FunctionDefine), w.name("twice");
    {
        w.name("demo.ad"), w.word(1), w.name("x"), w.word(3), w.word(1), w.word(4);
        w.in(3, script::Op::VariablePush), w.word(1), w.name("x"), w.word(0);
        w.in(3, script::Op::FloatConst);
        const float f = 2.5f;
        u32 bits;
        std::memcpy(&bits, &f, 4);
        w.word(bits);
        w.in(3, script::Op::BinaryOperator), w.name("*");
        w.in(3, script::Op::SetState), w.byte(1);
    }
    w.in(5, script::Op::VariablePush), w.word(3), w.name("demo"), w.name("value"), w.name("demo::value"), w.word(1);
    w.in(5, script::Op::IntConst), w.word(4);
    w.in(5, script::Op::StringConst), w.name("four");
    w.in(5, script::Op::JumpZero), w.word(6);
    w.in(5, script::Op::Assign);
    return w.b;
}

} // namespace

TEST(script_reads) {
    const Bytes file = make_script();
    CHECK(script::Script::version_of(file) == 5);
    const script::Script s = script::Script::parse(file);
    const script::Code& c = s.code;
    CHECK(c.source == "demo.ad" && c.parameters.empty() && c.variables == 4 && c.stack == 2 && c.instructions.size() == 7);
    CHECK(c.instructions[0].op == script::Op::ModuleDefine && c.instructions[0].symbols == std::vector<std::string>{"demo"});
    const script::Instruction& fn = c.instructions[1];
    CHECK(fn.op == script::Op::FunctionDefine && fn.text == "twice" && fn.body && fn.body->parameters == std::vector<std::string>{"x"});
    if (fn.body) {
        CHECK(fn.body->instructions.size() == 4 && fn.body->instructions[1].number == 2.5f && fn.body->instructions[2].text == "*");
        CHECK(fn.body->instructions[3].op == script::Op::SetState && fn.body->instructions[3].value == 1);
    }
    CHECK(c.instructions[2].symbols.size() == 3 && c.instructions[2].symbols[2] == "demo::value" && c.instructions[2].value == 1);
    CHECK(c.instructions[3].value == 4 && c.instructions[4].text == "four" && c.instructions[5].value == 6);
    const std::string listing = script::disassemble(c);
    CHECK(listing.find("FUNCTION_DEFINE") != std::string::npos && listing.find("2.5") != std::string::npos);

    // Another version, a cut file, data after the code, an opcode no version 5 file has.
    auto fails = [](Bytes f) {
        try {
            script::Script::parse(f);
        } catch (const FormatError&) {
            return true;
        }
        return false;
    };
    Bytes v3 = file;
    v3[6] = '3';
    CHECK(script::Script::version_of(v3) == 3 && fails(v3));
    CHECK(fails(Bytes(file.begin(), file.end() - 3)));
    Bytes more = file;
    more.push_back(0);
    CHECK(fails(more));
    Bytes bad = file;
    bad[0x21 + 4] = 99; // the first instruction's opcode
    CHECK(fails(bad));
    CHECK(script::Script::version_of(Bytes{'M', 'D', 'L', 'S', '0', '0', '5', 0}) == 0);
}

TEST(disc_scripts_read) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        size_t read = 0, other = 0, instructions = 0;
        for (const gt3r::Volume::Entry& e : disc->volume().entries()) {
            if (e.path.size() < 4 || e.path.compare(e.path.size() - 4, 4, ".adc") != 0) continue;
            const Bytes data = disc->volume().read(e);
            if (script::Script::version_of(data) != 5) {
                ++other;
                continue;
            }
            const script::Script s = script::Script::parse(data);
            ++read;
            instructions += s.code.instructions.size();
        }
        std::printf("  %s: %zu scripts of version 5, %zu of other versions\n", disc->info().serial.c_str(), read, other);
        if (disc->info().game == Game::GT4) {
            CHECK(read == 488 && other == 5 && instructions > 100000);
            const script::Script app = script::Script::parse(disc->volume().read(*disc->volume().find("projects/GT4Application.adc")));
            CHECK(app.code.source == "GT4Application.ad" && app.code.instructions[0].op == script::Op::ModuleDefine);
        } else {
            CHECK(read + other > 0);
        }
    }
}
