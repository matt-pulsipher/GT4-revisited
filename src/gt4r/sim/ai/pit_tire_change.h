#pragma once

// The pit stops' tire change on a session (sim/pit_tires.h): the pit's tires per car as the race prepares them (0x35C4D8),
// and the hooks the stop's timing calls (PitHooks::tires, change_tires, retune_tires): at the stop's 645th step from its end
// the entry's equipment with the chosen kinds, its spec for the session's course and the converter's tire part on the
// car's block (Session::change_tires), at the 644th the base grips (Session::retune_tires).

#include "gt4r/sim/ai/pit_stops.h"
#include "gt4r/sim/pit_tires.h"

namespace gt4r::sim::ai {

// Fills `hooks`' tire change from `session` (the cars' equipment and blocks), the database and the program, which must
// outlive the hooks; `read` reads the database's files (the cars' fitting parts).
void add_tire_change(PitHooks& hooks, Session& session, db::SpecDb& db, const Program& program, const FileReader& read);

} // namespace gt4r::sim::ai
