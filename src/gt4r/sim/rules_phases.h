#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): the last step's values (phase 1, 0x343ED0), the
// phases the game added (2: 0x355E10, 3: 0x35CA60, 6: 0x35D1D0), the preparation's own part (phase 4: 0x35C0B8,
// 0x35ACB0, the masses with the fuel 0x354AC0) and the hit records (phases 12 and 13: 0x350308, 0x350530, 0x350328).

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"

namespace gt4r::sim::rules {

// What these routines keep that the engine's record has no member for.
struct PhaseExtra {};
template <class X, class V> void phase_fields(X& x, V&& v) {
    (void)x, (void)v;
}

} // namespace gt4r::sim::rules
