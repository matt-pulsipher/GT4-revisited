#include "gt4r/script/adc.h"

#include <cstdio>
#include <cstring>

namespace gt4r::script {

namespace {

constexpr const char* kNames[] = {
    "ARRAY_PUSH", "ASSIGN", "ATTRIBUTE_DEFINE", "ATTRIBUTE_PUSH", "BINARY_ASSIGN_OPERATOR", "BINARY_OPERATOR", "CALL",
    "CLASS_DEFINE", "EVAL", "FLOAT_CONST", "FUNCTION_DEFINE", "IMPORT", "INT_CONST", "JUMP", "JUMP_NOT_ZERO", "JUMP_ZERO",
    "LIST_ASSIGN", "LOCAL_DEFINE", "LOGICAL_AND", "LOGICAL_OR", "METHOD_DEFINE", "MODULE_DEFINE", "NIL_CONST", "NOP", "POP",
    "PRINT", "REQUIRE", "SET_STATE", "STATIC_DEFINE", "STRING_CONST", "STRING_PUSH", "THROW", "TRY_CATCH",
    "UNARY_ASSIGN_OPERATOR", "UNARY_OPERATOR", "UNDEF", "VARIABLE_PUSH"};

constexpr int kMaxDepth = 32;
constexpr u32 kMaxNames = 64, kMaxInstructions = 1u << 20;

struct Reader {
    ByteSpan d;
    size_t o = 0;
    u8 byte() {
        if (o >= d.size()) throw FormatError("ADCH: unexpected end");
        return d[o++];
    }
    u32 word() {
        const u32 v = rd32(d, o); // (throws past the end)
        o += 4;
        return v;
    }
    // A name or string: u16 length, the bytes.
    std::string name() {
        const u32 n = rd16(d, o);
        o += 2;
        if (d.size() - o < n) throw FormatError("ADCH: name past the end");
        std::string s(reinterpret_cast<const char*>(d.data()) + o, n);
        o += n;
        return s;
    }
    std::vector<std::string> names() {
        const u32 n = word();
        if (n > kMaxNames) throw FormatError("ADCH: too many names");
        std::vector<std::string> v;
        for (u32 i = 0; i < n; ++i) v.push_back(name());
        return v;
    }
};

// A block: the source file's name, u32 parameters and their names, u32 stack entries, u32 local
// slots, u32 instructions, then each instruction: u32 line, u8 opcode, its operands.
Code read_code(Reader& r, int depth) {
    if (depth > kMaxDepth) throw FormatError("ADCH: functions nested too deep");
    Code c;
    c.source = r.name();
    c.parameters = r.names();
    c.stack = r.word(), c.locals = r.word();
    const u32 count = r.word();
    if (count > kMaxInstructions) throw FormatError("ADCH: bad instruction count");
    c.instructions.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        Instruction in;
        in.line = r.word();
        const u8 op = r.byte();
        if (op >= u8(Op::Count)) throw FormatError("ADCH: unknown instruction");
        in.op = Op(op);
        switch (in.op) {
        case Op::Assign: case Op::Eval: case Op::NilConst: case Op::Nop: case Op::Pop: case Op::Require: case Op::Throw:
            break;
        case Op::ArrayPush: case Op::Call: case Op::IntConst: case Op::Jump: case Op::JumpNotZero: case Op::JumpZero:
        case Op::ListAssign: case Op::LogicalAnd: case Op::LogicalOr: case Op::Print: case Op::StringPush: case Op::TryCatch:
            in.value = r.word();
            break;
        case Op::AttributeDefine: case Op::AttributePush: case Op::BinaryAssignOperator: case Op::BinaryOperator:
        case Op::StaticDefine: case Op::StringConst: case Op::UnaryAssignOperator: case Op::UnaryOperator:
            in.text = r.name();
            break;
        case Op::FloatConst: {
            const u32 bits = r.word();
            std::memcpy(&in.number, &bits, 4);
            break;
        }
        case Op::FunctionDefine: case Op::MethodDefine:
            in.text = r.name();
            in.body = std::make_shared<const Code>(read_code(r, depth + 1));
            break;
        case Op::ClassDefine: // the class's name, then its base's path
            in.text = r.name();
            in.symbols = r.names();
            break;
        case Op::Import: // a module's path, then the name taken from it
            in.symbols = r.names();
            in.text = r.name();
            break;
        case Op::LocalDefine: // a name and its local slot
            in.text = r.name();
            in.value = r.word();
            break;
        case Op::ModuleDefine: case Op::Undef:
            in.symbols = r.names();
            break;
        case Op::SetState:
            in.value = r.byte();
            break;
        case Op::VariablePush: // a path and its local slot
            in.symbols = r.names();
            in.value = r.word();
            break;
        case Op::Count:
            break;
        }
        c.instructions.push_back(std::move(in));
    }
    return c;
}

} // namespace

const char* op_name(Op op) { return op < Op::Count ? kNames[size_t(op)] : "?"; }

int Script::version_of(ByteSpan d) {
    if (!is_script(d)) return 0;
    int v = 0;
    for (size_t i = 4; i < 7; ++i) {
        if (d[i] < '0' || d[i] > '9') return 0;
        v = v * 10 + (d[i] - '0');
    }
    return v;
}

Script Script::parse(ByteSpan file) {
    Script s;
    s.version = version_of(file);
    if (!s.version) throw FormatError("not an ADCH script");
    if (s.version != 5) throw FormatError("ADCH: bytecode version " + std::to_string(s.version) + " is not read");
    Reader r{file, 8};
    s.code = read_code(r, 0);
    if (r.o != file.size()) throw FormatError("ADCH: data after the code");
    return s;
}

std::string disassemble(const Code& code, int indent) {
    std::string out;
    const std::string pad(size_t(indent) * 2, ' ');
    char b[96];
    out += pad + "; " + code.source + "  (";
    for (size_t i = 0; i < code.parameters.size(); ++i) out += (i ? ", " : "") + code.parameters[i];
    std::snprintf(b, sizeof b, ")  stack %u, locals %u\n", code.stack, code.locals);
    out += b;
    for (size_t i = 0; i < code.instructions.size(); ++i) {
        const Instruction& in = code.instructions[i];
        std::snprintf(b, sizeof b, "%4zu  %5u  %-22s", i, in.line, op_name(in.op));
        out += pad + b;
        switch (in.op) {
        case Op::FloatConst:
            std::snprintf(b, sizeof b, " %g", double(in.number));
            out += b;
            break;
        case Op::StringConst:
            out += " \"" + in.text + "\"";
            break;
        case Op::Assign: case Op::Eval: case Op::NilConst: case Op::Nop: case Op::Pop: case Op::Require: case Op::Throw:
            break;
        default:
            for (const std::string& s : in.symbols) out += " " + s;
            if (!in.text.empty()) out += (in.symbols.empty() ? " " : " : ") + in.text;
            if ((in.symbols.empty() && in.text.empty()) || in.op == Op::VariablePush || in.op == Op::LocalDefine) {
                std::snprintf(b, sizeof b, " %u", in.value);
                out += b;
            }
            break;
        }
        out += "\n";
        if (in.body) out += disassemble(*in.body, indent + 2);
    }
    return out;
}

} // namespace gt4r::script
