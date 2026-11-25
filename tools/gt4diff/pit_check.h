#pragma once

// The pit pass of the user's own program against ours at every call (pit_check.cpp).

#include "orig.h"

#include "gt4r/sim/ai/course_lines.h"
#include "gt4r/sim/ai/pit_flow.h"
#include "gt4r/sim/pit_tires.h"

#include <map>
#include <memory>
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
    u64 need_calls() const { return need_calls_; }
    u64 need_equal() const { return need_equal_; }
    u64 need_asked() const { return need_asked_; }
    u64 hold_equal() const { return hold_equal_; }
    // The tire change (GT4R_AI_PITTIRES=<front>,<rear>: the kinds a stop's car is given to choose, F +0x64E / +0x64F): the
    // program's parameter block after its 645th and 644th steps against ours from the block before them (sim/pit_tires.h),
    // byte for byte, with the conductor's bytes (the kinds fitted, the stops' record); and each car's pit tires (0x35C4D8)
    // against pit_tires.
    u64 change_calls() const { return change_calls_; }
    u64 change_equal() const { return change_equal_; }
    u64 list_calls() const { return list_calls_; }
    u64 list_equal() const { return list_equal_; }

private:
    void change_enter(const sim::ai::PitCar& c, u32 C);
    void change_leave();
    void enter();
    void leave();
    void hold_enter(Ee& e);
    void need_enter(Ee& e);
    void need_leave();
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
    bool need_in_ = false, need_allowed_ = false;
    int need_car_ = -1, need_lap_ = 0;
    u32 need_sp_ = 0;
    sim::ai::PitCar need_before_;
    sim::ai::PitNeed need_;
    std::set<u32> need_probed_;
    u64 need_calls_ = 0, need_equal_ = 0, need_reported_ = 0, need_asked_ = 0;
    int marker_k_ = 0;
    std::map<int, std::pair<u32, u32>> markers_;
    std::map<int, bool> marker_reported_;
    // (the tire change)
    const sim::Program& program_;
    std::unique_ptr<db::SpecDb> db_;
    int want_front_ = -1, want_rear_ = -1;
    std::set<int> listed_;
    int change_kind_ = 0; // 1 the 645th step, 2 the 644th
    u32 change_block_ = 0;
    sim::ParamBlock change_mine_;
    std::array<u8, sim::ParamBlock::kSize> change_before_{};
    std::vector<u8> change_cond_; // ours: cond + 0xFB20 + 2 i (2), + 0xDA20 + 8 i (2)
    u64 change_calls_ = 0, change_equal_ = 0, list_calls_ = 0, list_equal_ = 0, change_reported_ = 0;
};

} // namespace gt4r::diff
