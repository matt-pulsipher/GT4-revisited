#pragma once

// Compiled scripts (.adc, "ADCH"): the game's menus, game flow and settings are script
// programs run by an interpreter in the game program; docs/formats/script.md.

#include "gt4r/base/types.h"

#include <memory>
#include <string>
#include <vector>

namespace gt4r::script {

// The instructions of bytecode version 5, in the order the game's loader numbers them (it is
// alphabetical): each is built by the class of that name in the program ("mArrayPush", ...).
enum class Op : u8 {
    ArrayPush, Assign, AttributeDefine, AttributePush, BinaryAssignOperator, BinaryOperator, Call, ClassDefine, Eval,
    FloatConst, FunctionDefine, Import, IntConst, Jump, JumpNotZero, JumpZero, ListAssign, LocalDefine, LogicalAnd,
    LogicalOr, MethodDefine, ModuleDefine, NilConst, Nop, Pop, Print, Require, SetState, StaticDefine, StringConst,
    StringPush, Throw, TryCatch, UnaryAssignOperator, UnaryOperator, Undef, VariablePush,
    Count
};
const char* op_name(Op op); // "ARRAY_PUSH"

struct Code;

struct Instruction {
    u32 line = 0; // of the source file
    Op op = Op::Nop;
    u32 value = 0;                    // a count, an index, a jump target (an instruction's number), SetState's byte
    float number = 0;                 // FloatConst
    std::vector<std::string> symbols; // names: a path ("menu", "MManager", "menu::MManager"), an operator ("+")
    std::string text;                 // a single name or StringConst's string
    std::shared_ptr<const Code> body; // FunctionDefine, MethodDefine
};

// A block of code: a file's top level, or a function's body.
struct Code {
    std::string source;                  // the source file's name ("MenuClassDefine.ad")
    std::vector<std::string> parameters; // a function's
    u32 stack = 0;                       // value-stack entries the block needs
    u32 locals = 0;                      // local variable slots
    std::vector<Instruction> instructions;
};

struct Script {
    int version = 0;
    Code code;

    static bool is_script(ByteSpan d) { return d.size() >= 8 && gt3r::has_magic(d, "ADCH"); }
    // The bytecode version of a file (the three digits after the magic); 0 when it is not one.
    static int version_of(ByteSpan d);
    // Throws FormatError; only version 5 is read (the game's own scripts; a few leftover files of
    // versions 1 to 3 are on the disc).
    static Script parse(ByteSpan file);
};

// A listing of a block, one instruction per line, nested bodies indented.
std::string disassemble(const Code& code, int indent = 0);

} // namespace gt4r::script
