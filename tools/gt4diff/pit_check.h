#pragma once

// The pit pass of the user's own program against ours at every call (pit_check.cpp).

#include "orig.h"

#include "gt4r/sim/ai/course_lines.h"
#include "gt4r/sim/ai/pit_flow.h"

#include <map>
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

private:
    void enter();
    void leave();
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
    bool counting_ = false, crossed_ = false;
    int lap_state_ = -1;
    std::vector<u16> services_;
    std::vector<int> theirs_;
    sim::ai::PitCar before_;
    sim::ai::PitInputs inputs_;
    u64 calls_ = 0, equal_ = 0, active_ = 0, reported_ = 0;
    std::map<int, u64> counts_;
    int marker_k_ = 0;
    std::map<int, std::pair<u32, u32>> markers_;
    std::map<int, bool> marker_reported_;
};

} // namespace gt4r::diff
