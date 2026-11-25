// Gran Turismo 4 Prologue's judge of a license test (license_prologue.h).

#include "gt4r/race/license_prologue.h"

#include "gt4r/sim/ai/course_lines.h"

namespace gt4r::race::prologue {

bool PitZone::holds(float v) const {
    if (!any()) return false;
    if (v < lap * 0.5f) v += lap;
    return from < v && v < to;
}

PitZone pit_zone(const sim::ai::CourseLines& lines) {
    PitZone z;
    z.lap = lines.lap();
    const auto& p = lines.line(sim::ai::CourseLines::kPitStop);
    if (p.size() < 6) return z;
    z.from = p[1].dist, z.to = p[p.size() - 3].dist;
    return z;
}

LicenseStep license_step(const CarState& c, const PitZone& zone, int course_kind, float mass, int cones) {
    LicenseStep s = race::license_step(c, [](u8 m) { return m == 2 || m == 3 || m == 4 || m == 6; }, course_kind, mass, cones);
    if (!zone.holds(c.course_v)) s.wheels_pit = 0;
    s.off_counts = course_kind != 2;
    return s;
}

} // namespace gt4r::race::prologue
