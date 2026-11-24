#include "gt4r/race/license_rules.h"

#include <algorithm>
#include <cmath>

namespace gt4r::race {

LicenseRules LicenseRules::from_row(std::span<const u8> r) {
    LicenseRules l;
    if (r.size() < 0x8C) return l;
    l.kind = r[0x44];
    l.opponents = r[0x3E] > 0 ? r[0x3E] - 1 : 0;
    l.x41 = r[0x86];
    l.time_limit = r[0x87];
    l.x4a = r[0x88];
    l.pylons = r[0x75];
    l.wall_limit = r[0x8A];
    l.car_limit = r[0x8B];
    return l;
}

bool hit_over(int limit, float value) {
    if (limit == 0) return true;
    if (limit == 0xFF) return false;
    return int(std::trunc(value)) >= limit;
}

LicenseFail LicenseJudge::step(const LicenseStep& s) {
    if (fail_ != LicenseFail::None) return LicenseFail::None;
    // The hits (0x35FE80): kind 0 a wall, the impulse over the car's mass against CFG +0x49; kind 1 a car,
    // the strength x 100 against CFG +0x48; kind 2 a cone, counted toward the test's cones or, without
    // a count, a fail.
    bool hit = false;
    if (s.wall_hit > 0 && hit_over(rules_.wall_limit, s.wall_hit)) hit = true;
    if (s.car_hit > 0 && hit_over(rules_.car_limit, s.car_hit * 100)) hit = true;
    for (int k = 0; k < s.cones; ++k) {
        if (rules_.pylons <= 0) hit = true;
        else if (cones_ < rules_.pylons) ++cones_;
    }
    if (hit) bits_ |= 1;
    // The wheels (vf49 0x35FA98, while no rule bit is set): every wheel on the pit lane (vf46: 1), or on
    // an off-course material where the course counts it (vf47); the step's count goes up (1) or is
    // held at -1 (2), and any count sets the bit: 4 for the pit lane, 2 off the course.
    if (bits_ == 0) {
        const bool pit = s.wheels > 0 && s.wheels_pit == s.wheels;
        int v = pit ? 1 : 0;
        if (s.wheels > 0 && s.wheels_off == s.wheels) v = std::max(v, s.off_counts ? 1 : 0);
        if (v == 1) ++off_count_;
        else if (v == 2) off_count_ = -1;
        else off_count_ = 0;
        if (off_count_ != 0) bits_ = pit ? 4 : 2;
    }
    // The judge (0x3F8D70): bit 2 fails with kind 3, bit 4 with kind 5, bit 1 with kind 4.
    if (bits_ & 2) fail_ = LicenseFail::OffCourse;
    else if (bits_ & 4) fail_ = LicenseFail::PitLane;
    else if (bits_ & 1) fail_ = LicenseFail::Hit;
    return fail_;
}

void LicenseJudge::count_time(bool counting) {
    if (!counting) {
        frames_ = 0;
        return;
    }
    if (rules_.time_limit != 0 && frames_ < rules_.time_limit * 60) ++frames_;
}

int LicenseJudge::seconds_left() const {
    if (rules_.time_limit == 0) return -1;
    if (frames_ == 0) return -1;
    return rules_.time_limit - frames_ / 60;
}

} // namespace gt4r::race
