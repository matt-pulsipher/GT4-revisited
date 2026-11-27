// A race's rules of hits and speed caps (race_rules.h).

#include "gt4r/sim/race_rules.h"

#include "gt4r/sim/program.h"
#include "gt4r/sim/program_prologue.h"
#include "gt4r/sim/program_tables_prologue.h"

namespace gt4r::sim {

namespace {

// Gran Turismo 4's penalty's speed cap (km/h): the constant 0x35E108 loads at 0x35E2B4 (SCUS-97328 v1.01; the disc test
// race_rules_penalty_cap_is_the_program's reads it from the program's code).
constexpr u8 kPenaltyCap = 50;

} // namespace

RaceRules race_rules(ByteSpan row, const Program& program) {
    RaceRules r;
    if (row.size() < 0x8C) return r;
    r.hits.pylons = row[0x75], r.hits.car_limit = row[0x8B], r.hits.wall_limit = row[0x8A], r.hits.penalty = row[0x88];
    if (program.known()) {
        r.penalty_cap = kPenaltyCap;
    } else if (PrologueProgram::locate(program).recognized()) {
        r.penalty_cap = kProloguePenaltyCap; // (the constant 0x3C5B48 loads)
        // The test's case of the switch 0x3E1C20 (its table 0x572CC0, tests 1 .. 36), as values (program_tables_prologue.h).
        const u32 test = row[0x72];
        r.start_speed[0] = row[0x42], r.start_distance[0] = s16(row[0x14] | row[0x15] << 8);
        if (test >= 1 && test <= 36) {
            const PrologueLessonRule& k = prologue_lesson_rules()[test - 1];
            r.speed_cap[0] = k.speed_cap, r.mileage = k.mileage;
            for (size_t j = 1; j < 6; ++j) r.start_speed[j] = k.start_speed[j - 1], r.start_distance[j] = k.start_distance[j - 1];
        }
        for (size_t k = 1; k < 6; ++k)
            if (r.start_speed[k] == 0) r.start_speed[k] = r.start_speed[0]; // (DynamicsConductorTraining's virtual 33)
    }
    return r;
}

} // namespace gt4r::sim
