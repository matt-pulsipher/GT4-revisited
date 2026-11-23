#include "gt4r/sim/program_prologue.h"

#include "gt4r/sim/program.h"
#include "sim/exact/program_locate.h"

namespace gt4r::sim {

namespace {

namespace ex = gt3r::sim::exact;
using T = ex::ProgramData::Table;

// Prologue's routines by the hash of their code with what moves between builds masked
// (program_locate.h), their length in instructions, and where SCPS-15055 v1.01 has them.
struct Routine {
    const char* what;
    u64 hash;
    u32 words;
    u32 address;
};
constexpr Routine kRoutines[] = {
    {"the car spec builder (Gran Turismo 3's spec)", 0xeddea1d3d2f80984ull, 310, 0x404620},
    {"  its chassis helper", 0xc99099db52f39217ull, 27, 0x404CC8},
    {"  its engine helper", 0x50d8261453ac5305ull, 80, 0x404D38},
    {"the database's loader (specdb/GT4_PROLOGUE1000)", 0xe2904343275a6171ull, 203, 0x4070E0},
    {"a part's row by its id", 0x2b547f42bd3b6366ull, 12, 0x407520},
};

} // namespace

const std::vector<PrologueProgram::TablePlace>& PrologueProgram::tables() {
    static const std::vector<TablePlace> k = {
        {T::Static, 0, 0x120, 0x503208, "0x3764E0, 0x3B3748, 0x3B57D8 and 23 more"},
        {T::DriverSets, 0, 0x60, 0x5065B0, "0x427424"},
        {T::LineOrder, 0, 8, 0x5066E8, "0x429340"},
        {T::SpecCurves, 0, 0x10, 0x571C78, "0x3B3CA0"},
        {T::SpecCurves, 0x10, 0x0E, 0x503E18, "0x3B3E9C"},
        {T::SpecCurveB, 0, 0x0E, 0x503E18, "0x3B3E9C"},
        {T::SpecSurfaceClass, 0, 16, 0x571C88, "0x3B3DDC"},
        {T::LsdTypes, 0, 8, 0x504AD0, "0x3DF0E0"},
        {T::MachineSectors, 0, 16, 0x503E68, "0x3B511C"},
        {T::EffectParams, 0, 11 * 8, 0x503EA0, "0x3B98A8"},
    };
    return k;
}

PrologueProgram PrologueProgram::locate(ByteSpan image, u32 load_address) {
    PrologueProgram p;
    const ex::CodeIndex index(image, load_address, 0);
    p.recognized_ = true;
    for (const Routine& r : kRoutines) {
        const std::vector<u32> at = index.find(r.hash, r.words);
        const u32 found = at.size() == 1 ? at[0] : 0;
        p.places_.push_back({r.what, found, found ? "its code" : "not found"});
        if (found != r.address) p.recognized_ = false;
    }
    return p;
}

PrologueProgram PrologueProgram::locate(const Program& program) { return locate(program.image(), program.base()); }

bool PrologueProgram::apply(const Program& program, ex::ProgramData& d) const {
    if (!recognized_) return false;
    for (const TablePlace& t : tables())
        for (u32 i = 0; i < t.size; ++i) d.poke(t.table, t.offset + i, program.b(t.address + i));
    return true;
}

} // namespace gt4r::sim
