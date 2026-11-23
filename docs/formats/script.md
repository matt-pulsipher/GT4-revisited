# Scripts: `.adc`

Code: `src/gt4r/script/adc.*`; `gt4tool adc`. The game's menus, game flow and settings are
script programs: the program file holds an interpreter and the classes the scripts drive
(`menu::MSound`, `menu::MGame`, ...). The US disc has 493 compiled scripts: 488 of bytecode
version 5, which all read (484,273 instructions, 4,653 functions), and five leftover files of
versions 1 to 3 under `script/primitive/`.

| Where | Files | What |
|---|---|---|
| `projects/GT4Application.adc` | 1 | The program's main script: loads the modules and the configuration |
| `projects/<name>/<name>.adc` | 40 | A menu project each (`boot`, `arcade`, `gtmode`, `option`, `race`, ...) |
| `config/*.adc` | 46 | Settings by build and show (`config-subaru`, `course`, ...) |
| `usedcar/*.adc` | 400 | The used-car lists by week |
| `script/` | 6 | `MenuClassDefine` and the leftovers |

## Layout

```
"ADCH", three digits of version ("005"), 0
a block of code
```

A block (a file's top level, or a function's body):

```
name     the source file ("MenuClassDefine.ad"): u16 length, the bytes
u32      parameters, then their names
u32      value-stack entries the block needs
u32      local variable slots
u32      instructions
each:    u32 source line, u8 opcode, operands
```

A *name* is `u16 length` and the bytes; a *path* is `u32 count` and that many names (a
variable's path lists each part and then the whole: `menu`, `MManager`, `menu::MManager`).

## Instructions

The opcodes are numbered in the alphabetical order of the classes that build them in the
program (`mArrayPush` ... `mVariablePush`, read from the loader's dispatch).

| # | Instruction | Operands |
|---|---|---|
| 0 | `ARRAY_PUSH` | u32 |
| 1 | `ASSIGN` | |
| 2 | `ATTRIBUTE_DEFINE` | name |
| 3 | `ATTRIBUTE_PUSH` | name |
| 4 | `BINARY_ASSIGN_OPERATOR` | name: the operator (`+=`) |
| 5 | `BINARY_OPERATOR` | name: the operator (`+`, `==`) |
| 6 | `CALL` | u32 arguments |
| 7 | `CLASS_DEFINE` | name, path: the base class |
| 8 | `EVAL` | |
| 9 | `FLOAT_CONST` | f32 |
| 10 | `FUNCTION_DEFINE` | name, a block |
| 11 | `IMPORT` | path: the module, name |
| 12 | `INT_CONST` | u32 |
| 13, 14, 15 | `JUMP`, `JUMP_NOT_ZERO`, `JUMP_ZERO` | u32: an instruction's number in the block |
| 16 | `LIST_ASSIGN` | u32 |
| 17 | `LOCAL_DEFINE` | name, u32 local slot |
| 18, 19 | `LOGICAL_AND`, `LOGICAL_OR` | u32: an instruction's number |
| 20 | `METHOD_DEFINE` | name, a block |
| 21 | `MODULE_DEFINE` | path |
| 22, 23, 24 | `NIL_CONST`, `NOP`, `POP` | |
| 25 | `PRINT` | u32 |
| 26 | `REQUIRE` | |
| 27 | `SET_STATE` | u8 |
| 28 | `STATIC_DEFINE` | name |
| 29 | `STRING_CONST` | name: the string |
| 30 | `STRING_PUSH` | u32 |
| 31 | `THROW` | |
| 32 | `TRY_CATCH` | u32 |
| 33, 34 | `UNARY_ASSIGN_OPERATOR`, `UNARY_OPERATOR` | name: the operator |
| 35 | `UNDEF` | path |
| 36 | `VARIABLE_PUSH` | path, u32 local slot |

Checked on every version 5 script of the disc: each block reads exactly the number of
instructions it gives, and a file ends where its top block does.

❓ What each instruction does to the interpreter's state, and the functions of the program's
classes the scripts call.
