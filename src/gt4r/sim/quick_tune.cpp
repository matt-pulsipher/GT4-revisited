#include "gt4r/sim/quick_tune.h"

#include "gt4r/formats/db/car_parts.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/race_tires.h"

namespace gt4r::sim {

GearSliderRange quick_tune_gears(db::SpecDb& db, const Equipment& eq) {
    GearSliderRange g;
    const ByteSpan gear = db::part_row(db, eq.part(Equipment::Gear));
    if (gear.size() < 0x28 || gear[0x25] == 0) return g;
    g.value = eq.b(Equipment::GearSlider), g.min = gear[0x26], g.max = gear[0x27];
    return g;
}

void apply_quick_tune(db::SpecDb& db, const Program& program, const FileReader& read, Equipment& eq, const QuickTune& q, int course_kind) {
    eq.set_b(Equipment::PowerPct, u8(q.power_level + 100));
    eq.set_b(Equipment::WeightPct, u8(q.weight_level + 100));
    eq.set_b(0x160, u8(q.front_tire)), eq.set_b(0x161, u8(q.rear_tire));
    if (const u8 slider = u8(q.gear_top_speed); slider != 0) {
        eq.set_b(Equipment::GearSlider, slider);
        computed_gears(db, program, eq);
    }
    // (the race's reset fits the kinds and clears the flag: RaceArcade::vf92 0x387348)
    eq.set_b(0x165, 0);
    fit_race_tires(db, read, eq, course_kind, eq.b(0x160), eq.b(0x161));
}

} // namespace gt4r::sim
