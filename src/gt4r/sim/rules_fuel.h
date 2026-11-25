#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): the fuel and the nitrous. A race whose rule block's byte +0x4C
// is set fills each car's tank (the spec's +0x11C liters, the parameter block's +0x104B) at the start, less the program's
// byte 0x620E24; the engine's output burns it at the parameter block's rate +0x104C (the spec's +0x112) per 2,664,000, and the
// fuel's weight (0.74 kg a liter) rides in the mass. A tank of 0 (the races seen) keeps the car as it is.

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"

namespace gt4r::sim::rules {

struct FuelState {
    F used;                    // F + 0x4C0 the fuel burnt since the start (the economy's display reads it)
    F economy_used, economy_distance; // F + 0x4C4 / + 0x4C8 the economy display's last readings
    u8 economy_count = 0;      // F + 0x4BD steps until the economy display's next reading (0x354CE8: every 30)
    u16 economy = 0;           // F + 0x4BE the economy display's value
    F capacity, fuel;          // F + 0x4CC / + 0x4D0 the tank and what is in it (liters)
    F nitrous_power;           // F + 0x4DC the nitrous's share of the engine's output this step
    F nitrous_left;            // F + 0x4E0 the nitrous left (the block's +0x10D8 at the start when +0x10D4 is set)
    u8 nitrous_steps = 0;      // F + 0x4E4 steps the nitrous still runs (the input phase sets 6 while it is pressed)
    F rate;                    // P + 0x104C the fuel rate
    F nitrous_share;           // P + 0x10D4 the nitrous's share of the output (the spec's +0x11D percent)
    F nitrous_stock;           // P + 0x10D8 the nitrous's stock (the spec's s16 +0x11E x 100)
    u8 tank = 0;               // P + 0x104B the tank (liters)
    F sprung;                  // P + 0x4C the sprung mass (the mass less the wheels')
};
template <class X, class V> void fuel_fields(X& x, V&& v) {
    using layout::Block;
    const Block C = Block::Car, P = Block::Params;
    const u32 S = layout::kState;
    v("fuel.used", -1, C, S + 0x4C0, x.used), v("fuel.economy_used", -1, C, S + 0x4C4, x.economy_used);
    v("fuel.economy_distance", -1, C, S + 0x4C8, x.economy_distance), v("fuel.economy_count", -1, C, S + 0x4BD, x.economy_count);
    v("fuel.economy", -1, C, S + 0x4BE, x.economy);
    v("fuel.nitrous_power", -1, C, S + 0x4DC, x.nitrous_power), v("fuel.nitrous_left", -1, C, S + 0x4E0, x.nitrous_left);
    v("fuel.nitrous_steps", -1, C, S + 0x4E4, x.nitrous_steps);
    v("fuel.capacity", -1, C, S + 0x4CC, x.capacity), v("fuel.fuel", -1, C, S + 0x4D0, x.fuel);
    v("fuel.rate", -1, P, 0x104C, x.rate), v("fuel.nitrous_share", -1, P, 0x10D4, x.nitrous_share);
    v("fuel.nitrous_stock", -1, P, 0x10D8, x.nitrous_stock), v("fuel.tank", -1, P, 0x104B, x.tank), v("fuel.sprung", -1, P, 0x4C, x.sprung);
}

// 0x354AF8 with the car set-up's nitrous (0x3642A0): the counters cleared; the tank filled (the block's +0x104B when
// `fuel_rule`, the rule block's byte +0x4C, is set, less the program's byte 0x620E24, at least 0); the nitrous's stock when
// the car has a share of it. Then the masses (masses()).
void fuel_start(ex::Car& c, FuelState& f, u8 fuel_rule, u8 less, F& mass, F& sprung_mass);
// 0x354AC0 (phase 4, and the start): the masses the step reads, F + 0x4D4 / + 0x4D8: the block's mass and sprung mass plus
// 0.74 x the fuel.
void masses(const ex::Car& c, const FuelState& f, F& mass, F& sprung_mass);
// 0x354B78 (the end of the engine's torque, 0x3554A0): the nitrous's steps counted down and its stock used by its share of
// the output; the fuel burnt by the output (torque x rpm / 7,018.76, x the rate / 2,664,000 x the step), and taken from the
// tank unless the hold counter runs. Nothing for a one-speed hybrid box (gearbox kind 3).
void fuel_use(ex::Car& c, FuelState& f, u8 gearbox_kind, F torque);
// 0x472AE0: the economy display's value of a distance (m) and the fuel burnt over it in the game's units (0x6244E8: 0 km per
// liter x 0.01, 1 / 2 miles per US / UK gallon x 0.01, 3 liters per 1,000 km), 0 .. 999.
u16 economy_value(F distance, F used, u32 units);
// 0x354CE8 (the axle step): every 30 steps the economy display's value over the distance and fuel since the last.
void fuel_economy(ex::Car& c, FuelState& f, u32 units = 0);

} // namespace gt4r::sim::rules
