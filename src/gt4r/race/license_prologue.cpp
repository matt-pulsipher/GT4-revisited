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

int end_mode(const LicenseRules& r, bool others) {
    if (r.x41 >= 1 && r.x41 <= 3) return r.x41 == 1 ? 3 : r.x41 == 2 ? 1 : 2;
    if (others) return 3;
    if (r.pylons > 0) return 2;
    return r.kind == 3 || r.kind == 4 ? 3 : 1;
}

int fail_mode(const LicenseRules& r, bool others, LicenseFail fail) {
    if (fail != LicenseFail::OffCourse && fail != LicenseFail::Hit) return 2;
    const int m = end_mode(r, others);
    return !others && m == 3 ? 1 : m;
}

int grade(const std::array<s64, 3>& targets, s64 time_ms, const LicenseRules& r, int place, int mileage, int mileage_wanted) {
    int level = 3;
    if (r.opponents < 3) {
        for (int k = 0; k < 3 && level == 3; ++k)
            if (targets[size_t(k)] <= 0 || time_ms <= targets[size_t(k)]) level = k;
        if (r.opponents > 0 && place >= 2) level = 3;
    } else if (place < 4) level = place - 1;
    if (mileage_wanted > 0 && mileage < mileage_wanted) level = 3;
    return level;
}

} // namespace gt4r::race::prologue
