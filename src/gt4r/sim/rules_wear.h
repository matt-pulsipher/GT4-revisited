#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): tire wear, the pit flags, the flaps and brake heat.

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"

namespace gt4r::sim::rules {

// What these routines keep that the engine's record has no member for.
struct WearExtra {};
// Every member of WearExtra with its place in Gran Turismo 4's record: v(name, index (-1: single), block, offset, member).
template <class X, class V> void wear_fields(X& x, V&& v) {
    (void)x, (void)v;
}

} // namespace gt4r::sim::rules
