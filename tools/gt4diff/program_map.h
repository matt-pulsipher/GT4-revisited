#pragma once

// The harness names the program's routines, call sites and globals by their SCUS-97328 v1.01 addresses. pa() gives the
// loaded program's own (set_release at boot): v1.01's as they are, SCPS-17001 v1.05's (NTSC-J) through the table
// program_map_jp.inc (US address, Japan's address; made from the function map and the aligned instructions of each
// routine, docs/specs/physics.md "Japan's program in the interpreter"). An address with no counterpart throws, naming it.

#include "gt4r/base/types.h"
#include "gt4r/sim/program_tables.h"

namespace gt4r::diff {

// The release the interpreter runs (Us until boot sets it).
void set_release(sim::Release r);
sim::Release running_release();
inline bool japan() { return running_release() == sim::Release::Japan; }

// The running program's address of v1.01 address `us`.
u32 pa(u32 us);
// The same, 0 for an address with no counterpart (what needs it is left out).
u32 pa_or_0(u32 us);

// A v1.01 address held as a constant, the running program's where it is used.
struct Pa {
    u32 us;
    constexpr explicit Pa(u32 a) : us(a) {}
    operator u32() const { return pa(us); }
};

// A field of the conductor (DynamicsConductor) by its v1.01 offset: the running program's offset.
u32 cond_off(u32 us);
struct Co {
    u32 us;
    constexpr explicit Co(u32 o) : us(o) {}
    operator u32() const { return cond_off(us); }
};

// The program's segments (the interpreter's load_program).
struct Segments {
    u32 text_end, bss_start, bss_end, gp;
};
Segments segments();

} // namespace gt4r::diff
