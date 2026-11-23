#pragma once

// The small programs a model set carries for its animated models (a course's flags and people,
// a car's moving parts): a stack machine over cells of 4 bytes, whose results are the registers
// the draw programs read. docs/formats/course.md "Scripts of the model sets".

#include "gt4r/base/types.h"

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace gt4r::mdls {

// A set's script: its code, its variables and each model's entry points.
struct Script {
    struct Variable {
        std::string name;
        u16 cells = 1, cell = 0;
    };
    struct Entries {
        s32 setup = -1, tick = -1, draw = -1; // offsets into the code; -1: none
    };
    Bytes code;
    u32 register_cells = 0;           // segment 0: what the draw programs read
    u32 second_cells = 0;             // segment 1
    std::vector<Variable> host;       // segment 2: values the game gives by name ("time", "deltaT")
    std::vector<Variable> registers;  // the named cells of segment 0 ("builtin_tw0")
    std::vector<Entries> models;      // one per model of the set

    bool empty() const { return code.empty(); }
    // The cell of a host variable; -1 when the set does not ask for it.
    int host_cell(const std::string& name) const;
    // A model set's script; empty when it has none. Throws FormatError.
    static Script parse(ByteSpan mdls);
};

// The cells a script works on. A value is an i32 or the bits of an f32.
struct ScriptState {
    std::vector<u32> registers, second, host;
    explicit ScriptState(const Script& s) : registers(s.register_cells, 0), second(s.second_cells, 0), host(s.host.size(), 0) {}
    ScriptState() = default;
    float register_f(u32 cell) const;
    s32 register_i(u32 cell) const { return cell < registers.size() ? s32(registers[cell]) : 0; }
    void set_host(int cell, u32 bits) {
        if (cell >= 0 && size_t(cell) < host.size()) host[size_t(cell)] = bits;
    }
    void set_host_f(int cell, float value);
};

// What a script asks its set's owner (commands 0xC0, 0xC1): the id, and the cells from the
// variable it names on (a course's people give their place: x, y, z, then their facing).
using ScriptCallback = std::function<float(u32 id, std::span<const u32> cells)>;

// Runs the code from `entry` to its end. False when it stops on an unknown command, runs past
// the code or takes more than 10,000 commands.
bool run_script(const Script& s, s32 entry, ScriptState& state, const ScriptCallback& callback = {});

} // namespace gt4r::mdls
