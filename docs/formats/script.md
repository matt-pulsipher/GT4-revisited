# Scripts: `.adc`

Code: `src/gt4r/script/adc.*` (the files), `src/gt4r/script/vm.*` (the interpreter);
`gt4tool adc`, `gt4tool run`. The game's menus, game flow and settings are
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
u32      variable slots: the parameters, one spare, then every name the block uses
u32      value-stack entries the block needs
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
| 17 | `LOCAL_DEFINE` | name, u32 variable slot |
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
| 36 | `VARIABLE_PUSH` | path, u32 variable slot |

Checked on every version 5 script of the disc: each block reads exactly the number of
instructions it gives, and a file ends where its top block does.

No script on the discs has `LOCAL_DEFINE`, `THROW`, `TRY_CATCH` or `UNDEF`.

## What the instructions do

The interpreter keeps, per call, a stack of values and the block's variable slots; a module
stack (which module definitions land in); and one register for the last value popped, which
is what a function returns. A value is nil, an int (32 bits), a float (32 bits), a string, an
array, an object, a module or class, or a function (the program's `hNil`, `hInt`, `hFloat`,
`hString`, `hArray`, `hObject`, `hModule`, `hClass`, `h...Function`); there is no map and no
boolean (comparisons give the ints 0 and 1).

Read from the program's instruction classes (each one's "execute" is slot 9 of its vtable) and
from how the compiler uses them; then checked two ways: a count of pushes and pops along every
path of every block of the disc never goes below zero nor above the stack size the block
declares, and the interpreter runs the scripts themselves.

| Instruction | Effect |
|---|---|
| `VARIABLE_PUSH` | Pushes a *reference* to a variable. One name: the block's own variable (made on first use, kept in its slot). A path: a name in reach, looked up once and kept in the slot: the object's own attributes and methods in a method, what `IMPORT` brought, then each module from the current one outward to `main`; the path's further parts are members (`main`, `menu`, `MSound`) |
| `ATTRIBUTE_PUSH` | Pops an object, pushes a reference to its member of that name. `value` of a number, a string or nil that `EVAL` just read from a variable is the variable itself: `count.value = 3` assigns `count` (numbers are objects in the program) |
| `EVAL` | Replaces a reference on top of the stack with its value. A variable that holds a pointer (below) gives the pointer |
| `ASSIGN` | Pops a value and a reference, stores, pushes the value |
| `LIST_ASSIGN n` | `[a, b] = list`: pops the list and n references, stores an element in each, pushes the list |
| `POP` | Pops a value into the result register |
| `CALL n` | Pops n arguments and the callee, pushes the call's result. Calling a class makes an object (its attributes nil), runs its `__init__` method and gives the object. A call of the program's that waits (below) holds the script there |
| `INT_CONST`, `FLOAT_CONST`, `STRING_CONST`, `NIL_CONST` | Push the constant |
| `ARRAY_PUSH n` | Pops n values, pushes an array of them |
| `STRING_PUSH n` | Pops n values, pushes them joined as text |
| `PRINT n` | Pops n values and writes them; pushes nil |
| `BINARY_OPERATOR` | Pops two values, pushes the result. `[]` gives a reference: an array's element (assigning past the end grows the array), or an object's or module's member by name (`page["open"]`; nil for a widget that has nothing of that name). `+` with a string joins text |
| `UNARY_OPERATOR` | `!`, `-@`, `+@`, `~` on the top value |
| `BINARY_ASSIGN_OPERATOR` | `a += b`: pops the value and the reference, stores, pushes the result |
| `UNARY_ASSIGN_OPERATOR` | `@++`, `@--` push the value before; `++@`, `--@` the value after |
| `JUMP`, `JUMP_ZERO`, `JUMP_NOT_ZERO` | The conditional ones pop the value they test |
| `LOGICAL_AND`, `LOGICAL_OR` | `a && b`: when the top value settles the result it stays and the jump is taken; otherwise the `POP` that follows drops it and `b` is evaluated |
| `MODULE_DEFINE` | Enters a module: the one of that name in reach, or a new one in the current module. A path names nested ones (`BootProject`, `MovieRoot`, `Mpeg`) |
| `CLASS_DEFINE` | Enters a class of the current module, with its base class |
| `SET_STATE 0` | Leaves the module or class entered last; at a file's top, ends it |
| `SET_STATE 1` | Returns (the result register holds the value: `return x` is `x`, `POP`, `SET_STATE 1`) |
| `FUNCTION_DEFINE`, `METHOD_DEFINE` | Define the function in the current module or class. A method runs with its object: the object's attributes are names in reach |
| `STATIC_DEFINE` | A variable of the current module |
| `ATTRIBUTE_DEFINE` | An attribute every object of the current class gets |
| `IMPORT` | Brings one name of a module, or all (`*`), in reach of the block |
| `REQUIRE` | Pops a name. `menu`, `gt4app`: one of the program's own modules, filled into the current module. `/config/course`: a script file (`config/course.adc`), run in the current module; a bare name is a file beside the one running. Names are in any case (`us_carlist` for `US_carlist.adc`) |

A compiled statement can leave a value behind on the stack (an expression whose result is not
used and not popped), so the stack is the call's own and is dropped with it.

**A variable passed as itself.** Where the source gives a variable and not its value (an
"out" argument: the caller's `result` that the function sets), the compiled code pushes the
variable and leaves the `EVAL` out. As an argument of a script function, or as the right side
of an assignment to a variable, such a push is a *pointer* to the variable: reading it gives
the variable's value, and `VARIABLE_PUSH p`, `EVAL`, value, `ASSIGN` sets the variable it
points to (the memory card code's `resultRef`). The program's own functions are given values.

## Calls that wait

The program runs scripts on threads: a script that calls `context.sync(seconds)` stops there
while the pictures go on, and one that opens a dialog waits inside the call for the answer
([menu.md](menu.md), "Scripts wait"). The interpreter has no threads; a call is a *task*
(`Vm::start`, `Vm::resume`): the frames of the script functions it is in, kept as data, so a
function of the program's can ask to wait (`Vm::wait(until, then)`) and the task is taken up
again where it stopped, any number of calls deep. A function of the program's can also hand
over to a script function and go on with its result (`Vm::then_call`). `Vm::call` runs a call
to its end, passing over the waits (the tools and tests that want an answer at once).

## Built-in members

The names are the program's (the tables its value classes register); what each does is
written from how the scripts use it, not yet compared with the program's own code:

| Of | Members |
|---|---|
| Any value | `toString()`, `toInt()`, `toFloat()` |
| String | `size`, `at(i)` (the character's code), `substr(from, length)`, `split(separator)`, `format(...)`, `build(...)`, `upcase()`, `downcase()` |
| Array | `size`, `push(v)`, `pop()`, `shift()`, `unshift(v)`, `erase(i)`, `join(separator)`, `sort(compare)` |
| Module | `name`, `defined(name)`, `defineStatic(name, value)`, `removeStatic(name)`, `clearStatic()`, `load(file)` (runs a script beside its own in the module) |

`format` takes C's conversions and one of the program's own: `%t` writes a whole number with
a point before its last digits, as many as the precision (`"%.1t"` of 123 is `12.3`, of 5
`0.5`; the program's 0x493918, US v1.01). The menus give tenths and hundredths this way
(`"%.2t%s/%s%s"`: a torque, its unit, the engine speed, its unit).

`build` puts the n-th argument, as text, in place of each `{n}` (the program splits the text at
`{` and `}`, 0x315B78): `"{0} Race No. {1}"`. ❓ A mark with no such argument is left as it is.

`Array(n)` makes an array of n nils.

Not written: the array's `pack`, `bsearch`, `move`; `Math` (`sin`,
`cos`), `Path`, `Time`, `FileIO`, `Thread`, `ThreadGroup`, `Exception`, `System`.

## What the scripts ask of the program

`gt4tool run <disc>` runs every script's top level with stand-ins for the program's own
classes and counts what is asked of them: 485 of the 488 scripts run to their end (three need
a real answer: the configuration file's name, the region code, a widget's children). The main
script loads the program's `menu` and `gt4app` modules into `menu`, makes
`menu::MSound()`, `MNetwork()`, `MShell()`, `MGame()`, runs `/script/MenuClassDefine` (which
registers the widget classes with `menu::MManager().registerClass`) and the configuration
script `menu::MGame::getConfigScriptFile()` names. A menu project's script fills the module
of its name (`BootProject`) with a module per page and widget, holding the event functions
(`onInitialize`, `onKeyPress`, `onCancel`, ...) the program calls.

The program's classes the menus use (the widgets, the actors, the game's state) are in
[menu.md](menu.md) ("Running"). The program registers 199 classes with the interpreter, each
with its attributes, methods and static functions by name (US v1.01: a method through
0x2F3818, an attribute's getter and setter through 0x2F3860, a static function through
0x3068A8, a constructor through 0x306780, the base class through 0x2F3A30).

❓ `TRY_CATCH` and the instructions no script uses; the other built-ins compared with the
program; `Thread` and `ThreadGroup` as the scripts could use them themselves.
