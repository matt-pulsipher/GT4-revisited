#pragma once

// The LAN battle's own race rules (docs/specs/net.md "The race's rules"), read from the program: the conductor of a battle
// between machines (DynamicsConductorBattleMP) sets them from the host's options in place of an event's row.

#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/program.h"

#include <array>

namespace gt4r::net {

// The tire wear (0x354F30, the rule block's +0x2C .. +0x34) for the battle's tire damage setting: 0 none, 1 and 2.
// All 0 for a program whose routines are not mapped.
std::array<u8, 9> lan_wear(const sim::Program& program, int tire_damage);
// The boost (0x354EB8): the cars behind hurried by up to a percent past a gap, fully past another; 0 none, 1 and 2.
void lan_boost(const sim::Program& program, int boost, sim::ai::Pace& pace);

} // namespace gt4r::net
