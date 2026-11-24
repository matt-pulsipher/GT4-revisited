// Gran Turismo 4's fuel and nitrous (rules_fuel.h).

#include "gt4r/sim/rules_fuel.h"

namespace gt4r::sim::rules {

namespace {
using gt3r::ee::int_to_f;
const F kZero = F(), kOne = F::raw(0x3F800000);
} // namespace

void masses(const ex::Car& c, const FuelState& f, F& mass, F& sprung_mass) {
    const F w = f.fuel * F::raw(0x3F3D70A3);
    mass = c.p.mass + w;
    sprung_mass = f.sprung + w;
}

void fuel_start(ex::Car& c, FuelState& f, u8 fuel_rule, u8 less, F& mass, F& sprung_mass) {
    f.economy_count = 1, f.used = kZero, f.economy = 0, f.economy_used = kZero, f.economy_distance = kZero;
    const u8 tank = fuel_rule ? f.tank : 0;
    f.fuel = int_to_f(tank), f.capacity = int_to_f(tank);
    if (tank != 0) {
        const F v = f.capacity - int_to_f(less);
        f.fuel = v;
        if (v < f.used) f.fuel = f.used;
    }
    masses(c, f, mass, sprung_mass);
    // (the car set-up, 0x3642A0: the nitrous)
    if (kZero < f.nitrous_share) f.nitrous_left = f.nitrous_stock;
    f.nitrous_steps = 0, f.nitrous_power = kZero;
}

void fuel_use(ex::Car& c, FuelState& f, u8 gearbox_kind, F torque) {
    if (gearbox_kind == 3) return;
    bool nitrous = false;
    f.nitrous_power = kZero;
    if (f.nitrous_steps != 0) --f.nitrous_steps, nitrous = true;
    if (torque <= kZero) return;
    F rpm = c.omega * F::raw(0x42700000);
    rpm = rpm / F::raw(0x40C90FDA);
    const F power = (torque * rpm) / F::raw(0x45DB5612);
    if (nitrous) {
        const F share = f.nitrous_share;
        F n = power * share;
        n = n / (share + kOne);
        f.nitrous_power = n;
        const F left = f.nitrous_left - n * c.dt;
        f.nitrous_left = left;
        if (left <= kZero) f.nitrous_left = kZero, f.nitrous_steps = 0;
    }
    F burnt = f.rate / F::raw(0x4A229900);
    burnt = burnt * power;
    burnt = burnt * c.dt;
    f.used = f.used + burnt;
    if (kZero < f.capacity && c.hold == 0) {
        const F v = f.fuel - burnt;
        f.fuel = v;
        if (v < kZero) f.fuel = kZero;
    }
}

u16 economy_value(F distance, F used, u32 units) {
    // (0x472A78: the fuel in US or UK gallons for units 1 / 2; the distance in miles)
    F fuel = used;
    if (units == 1) fuel = used * F::raw(0x3E874251);
    else if (units == 2) fuel = used * F::raw(0x3E61413B);
    F d = distance;
    if (units == 1 || units == 2) d = distance * F::raw(0x3F1F136A);
    else if (units == 3) {
        if (d < F::raw(0x3A83126E)) return 999;
        const F v = (fuel * F::raw(0x49742400)) / d;
        if (F::raw(0x4479C000) <= v) return 999;
        return u16(gt3r::ee::to_int(v));
    }
    if (d < F::raw(0x3C23D70A)) return 0;
    if (fuel < F::raw(0x358637BD)) return 999;
    const F v = (d * F::raw(0x3C23D70A)) / fuel;
    if (F::raw(0x4479C000) < v) return 999;
    return u16(gt3r::ee::to_int(v));
}

void fuel_economy(ex::Car& c, FuelState& f, u32 units) {
    const u8 n = u8(f.economy_count - 1);
    f.economy_count = n;
    if (n != 0) return;
    f.economy = economy_value(c.dist_total - f.economy_distance, f.used - f.economy_used, units);
    f.economy_count = 30;
    f.economy_distance = c.dist_total, f.economy_used = f.used;
}

} // namespace gt4r::sim::rules
