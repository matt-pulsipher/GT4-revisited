#pragma once

// A license test's, mission's or coffee break's rules: the bytes of the event's RACE row the race keeps
// in its configuration (0x35E108 copies the row into K +0xF818, "CFG"), and the judge that ends the
// test (DynamicsConductorLicense::vf15 -> 0x3F8D70) from what happens to the player's car each step.
// docs/formats/race_flow.md "The license tests' rules".

#include "gt4r/base/types.h"

#include <span>

namespace gt4r::race {

struct LicenseRules {
    // CFG +0x02 <- row +0x44: how the test ends. 1: the car stopped in the stopping box (0x3F91F0);
    // 3, 6: at the lap's line (vf21 -> 0x3F8AB0); 2: at vf23 (❓ its moment).
    int kind = 0;
    int opponents = 0;     // CFG +0x40 <- row +0x3E less one: the other cars (3 or more: graded by place)
    int x41 = 0;           // CFG +0x41 <- row +0x86 (0x3F8818: 1, 2 or 3 picks the car's end state) ❓
    int time_limit = 0;    // CFG +0x44 <- row +0x87: seconds the clock may run while the car's place byte is 1 (0: none)
    int pylons = 0;        // CFG +0x46 <- row +0x75: cones to knock down (the test ends at that count); 0: a cone touched fails
    int car_limit = 0;     // CFG +0x48 <- row +0x8B: a car hit's strength x 100 at or over it fails (0: any, 255: none)
    int wall_limit = 0;    // CFG +0x49 <- row +0x8A: a wall hit's impulse over the car's mass (m/s), likewise
    int x4a = 0;           // CFG +0x4A <- row +0x88 ❓
    // CFG +0x21 <- row +0x89 (signed; below -1 read as -1): the road's section the race runs on (the course
    // object's +0xB0, DynamicsConductor::vf26 0x34CA20): its start and goal distances (road.md "Sections"); -1 none.
    int section = -1;

    // From a RACE row (151 bytes); a short row gives the defaults.
    static LicenseRules from_row(std::span<const u8> row);
    bool any() const { return kind != 0; }
};

// Why a test failed: the judge's kinds (0x3F8980's argument). All of them end the run with the race's
// event 10 and no time; the kind only picks the car's end state.
enum class LicenseFail : u8 {
    None = 0,
    Hit = 4,       // F +0x4B3 bit 1: a wall, car or cone hit over the test's limit (0x35FE80)
    OffCourse = 3, // F +0x4B3 bit 2: every wheel on a surface the program's table 0x622430 marks (0x369888 -> vf49)
    PitLane = 5,   // F +0x4B3 bit 4: every wheel on the pit lane (the road's flags bit 0)
    Kind6 = 6,     // a test of kind 6 when the car's byte +0x5B3 is 1 ❓
};

// What the judge reads of one step of the player's car.
struct LicenseStep {
    int wheels = 4;        // the wheel layout's count (0x359538 +0x01)
    int wheels_off = 0;    // wheels on the ground on an off-course material
    int wheels_pit = 0;    // wheels on the ground on the pit lane
    bool off_counts = true; // vf47: off-course counts (the course's mode is neither 2 nor 3, 0x34C998)
    // This step's hits, 0 none: a wall hit's impulse |j| over the car's mass P +0x48 (m/s), a car hit's
    // strength (|j| / 15000, the collision step's measure); the cones touched.
    float wall_hit = 0, car_hit = 0;
    int cones = 0;
};

// The judge's state for the player's car (F +0x4B3, the cone counter K +0x1003C, the time-limit counter
// K +0x1003E).
class LicenseJudge {
public:
    explicit LicenseJudge(const LicenseRules& rules = {}) : rules_(rules) {}
    const LicenseRules& rules() const { return rules_; }

    // One step: the rule bits (0x35FE80 for hits, vf49 0x35FA98 for the wheels), then the judge's order:
    // bit 2, bit 4, bit 1. Returns the failing kind once (None while the test runs).
    LicenseFail step(const LicenseStep& s);
    // The cones counted (K +0x1003C) and whether the test's count is reached (the run ends there, timed).
    int cones() const { return cones_; }
    bool cones_done() const { return rules_.pylons > 0 && cones_ == rules_.pylons; }
    // The time limit (CFG +0x44 seconds): count a frame while the car's place byte F +0x4AF is 1, else
    // start again (0x3FA248 / 0x3FA238); the seconds left (0x3FA2B0: -1 without a limit), 0 ends the run.
    void count_time(bool counting);
    int seconds_left() const;
    bool failed() const { return fail_ != LicenseFail::None; }
    LicenseFail fail() const { return fail_; }
    u8 bits() const { return bits_; }

private:
    LicenseRules rules_;
    u8 bits_ = 0;
    int off_count_ = 0;
    int cones_ = 0;
    int frames_ = 0;
    LicenseFail fail_ = LicenseFail::None;
};

// 0x35FC10: a hit of `value` against a limit byte: 0 any hit fails, 255 none, else value (truncated) at or over it.
bool hit_over(int limit, float value);

} // namespace gt4r::race
