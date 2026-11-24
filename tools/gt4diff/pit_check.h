#pragma once

// The pit pass of the user's own program against ours at every call (pit_check.cpp).

#include "orig.h"

#include "gt4r/sim/ai/course_lines.h"
#include "gt4r/sim/ai/pit_flow.h"

#include <map>
#include <set>
#include <vector>

namespace gt4r::diff {

class PitCheck {
public:
    PitCheck(Ee& ee, const OrigRace& o, const sim::ai::CourseLines& lines, const sim::Program& program, float lap);
    void install();
    void print() const;
    u64 calls() const { return calls_; }
    u64 equal() const { return equal_; }
    u64 active() const { return active_; }
    const std::map<int, u64>& counts() const { return counts_; }
    u64 hold_calls() const { return hold_calls_; }
    u64 hold_equal() const { return hold_equal_; }

private:
    void enter();
    void leave();
    void hold_enter(Ee& e);
    void hold_leave();
    bool depth_ok() const;
    sim::ai::PitCar read_car(u32 F) const;

    Ee& ee_;
    const OrigRace& o_;
    sim::ai::PitFlow flow_;
    sim::ai::PitLine line_;
    float lap_;
    bool in_ = false, probed_ = false;
    u32 ret_ = 0, sp_ = 0;
    int car_ = -1;
    bool counting_ = false, crossed_ = false, lap_count_ = false;
    int lap_state_ = -1;
    std::vector<u16> services_;
    std::vector<int> theirs_;
    sim::ai::PitCar before_;
    sim::ai::PitInputs inputs_;
    u64 calls_ = 0, equal_ = 0, active_ = 0, reported_ = 0;
    std::map<int, u64> counts_;
    bool hold_in_ = false;
    int hold_car_ = -1;
    u32 hold_sp_ = 0;
    sim::ai::PitCar hold_before_;
    std::vector<int> hold_tires_;
    std::set<u32> hold_probed_;
    u64 hold_calls_ = 0, hold_equal_ = 0, hold_reported_ = 0, hold_new_tires_ = 0;
    u64 lap_calls_ = 0, lap_equal_ = 0;
    int marker_k_ = 0;
    std::map<int, std::pair<u32, u32>> markers_;
    std::map<int, bool> marker_reported_;
};

} // namespace gt4r::diff
