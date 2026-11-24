// The pit pass (0x359008 → 0x35B060) of the user's own program against ours (sim/ai/pit_flow.h), at every call: the car's
// pit record and the race's answers as the program has them at the call's entry, the routines the program's pass calls
// (the drivers' line steps, the box, the stop's counting, the refuelling) and the record as it leaves it, against ours from
// the same record.

#include "pit_check.h"

#include "gt4r/sim/ai/pit_flow.h"

#include <cstdio>

namespace gt4r::diff {

namespace {

constexpr u32 kPass = 0x359008;
using F = gt3r::ee::F;

const char* action_name(int k) {
    static const char* const n[] = {"enter", "stop", "leave", "return", "end run", "place in box", "heading", "count stop", "recount", "refuel", "driver"};
    return k >= 0 && k < 11 ? n[k] : "?";
}

} // namespace

PitCheck::PitCheck(Ee& ee, const OrigRace& o, const sim::ai::CourseLines& lines, const sim::Program& program, float lap)
    : ee_(ee), o_(o), flow_(program), lap_(lap) {
    const std::vector<sim::ai::LinePoint>& stop = lines.line(sim::ai::CourseLines::kPitStop);
    for (const sim::ai::LinePoint& p : stop) line_.dist.push_back(F(p.dist)), line_.heading.push_back(F(p.heading));
}

void PitCheck::install() {
    ee_.add_probe(kPass, [this](Ee&) { enter(); });
    // What the pass's callees answer, read at their return into the pass.
    ee_.add_probe(0x359060, [this](Ee& e) { if (in_) counting_ = e.g32(2) != 0; });     // 0x34C210
    ee_.add_probe(0x359078, [this](Ee& e) { if (in_) crossed_ = e.g32(2) == 1, lap_count_ = false; }); // 0x358608
    // (the lap count 0x358608 is the race's: what it calls, the end of a car's run among them, is not the pass's)
    ee_.add_probe(0x358608, [this](Ee&) { if (in_) lap_count_ = true; });
    ee_.add_probe(0x35B2D8, [this](Ee& e) { if (in_) lap_state_ = int(e.g32(2)); });   // 0x343C20
    for (u32 ret : {0x35B3B4u, 0x35B990u, 0x35BA1Cu}) // 0x35AF18
        ee_.add_probe(ret, [this](Ee& e) { if (in_) services_.push_back(u16(e.g32(2))); });
    const std::pair<u32, int> acts[] = {{0x3453A0, sim::ai::PitAction::Enter},     {0x345400, sim::ai::PitAction::Stop},
                                        {0x345450, sim::ai::PitAction::Leave},     {0x345228, sim::ai::PitAction::Return},
                                        {0x343D78, sim::ai::PitAction::EndRun},    {0x363560, sim::ai::PitAction::PlaceInBox},
                                        {0x35AD10, sim::ai::PitAction::CountStop}, {0x35ADF0, sim::ai::PitAction::Recount},
                                        {0x354D60, sim::ai::PitAction::Refuel},    {0x463E80, sim::ai::PitAction::Driver}};
    for (const auto& [a, k] : acts) {
        const int kind = k;
        ee_.add_probe(a, [this, kind](Ee&) { if (in_ && depth_ok()) theirs_.push_back(kind); });
    }
    // The stop line's places the pass reads (0x46BDC0: k at entry, the distance at its return) against ours.
    ee_.add_probe(0x46BDC0, [this](Ee& e) { marker_k_ = int(s32(e.g32(5))); });
    ee_.add_probe(0x46BE18, [this](Ee& e) {
        const F theirs = F::raw(u32(e.fpr[0]));
        const F mine = line_.marker(marker_k_);
        auto& m = markers_[marker_k_];
        if (m.first == 0 && m.second == 0) m = {theirs.bits, mine.bits};
        if (theirs.bits != mine.bits && !marker_reported_[marker_k_]) {
            marker_reported_[marker_k_] = true;
            std::printf("  stop line point %d: the program's %.4f (%08X), ours %.4f (%08X)\n", marker_k_, double(theirs.host()), theirs.bits, double(mine.host()), mine.bits);
        }
    });
    // The pit request (0x353218, the wear phase) against pit_need: the race's answer (its virtual +0x180) read at its return.
    ee_.add_probe(0x353218, [this](Ee& e) { need_enter(e); });
    ee_.add_probe(0x353284, [this](Ee& e) { if (need_in_) need_allowed_ = e.g32(2) != 0; });
    ee_.add_probe(0x3532B0, [this](Ee& e) { if (need_in_) need_lap_ = int(e.g32(2)); });
    // The stop's timing (0x35C0B8, phase 4 while the hold counter runs) against pit_hold_step.
    ee_.add_probe(0x35C0B8, [this](Ee& e) { hold_enter(e); });
    ee_.add_probe(0x362E58, [this](Ee& e) { if (hold_in_) hold_tires_.push_back(int(e.g32(5))); });
    ee_.add_probe(0x36C9F8, [this](Ee&) { if (in_ && depth_ok()) theirs_.push_back(sim::ai::PitAction::Heading); });
}

bool PitCheck::depth_ok() const { return !lap_count_; }

sim::ai::PitCar PitCheck::read_car(u32 F_) const {
    sim::ai::PitCar c;
    c.flags = ee_.r8(F_ + 0x4B1), c.x4B2 = ee_.r8(F_ + 0x4B2), c.box = gt3r::s8(ee_.r8(F_ + 0x4B7));
    c.service = ee_.r16(F_ + 0x4B8), c.hold = ee_.r16(F_ + 0x4BA), c.x5C1 = ee_.r8(F_ + 0x5C1), c.tire_out = ee_.r8(F_ + 0x5C2);
    c.speeding = ee_.r8(F_ + 0x5C3), c.status = ee_.r8(F_ + 0x5FA), c.status_time = ee_.r8(F_ + 0x5FB), c.pad_state = ee_.r8(F_ + 0x62E);
    c.x64C = s16(ee_.r16(F_ + 0x64C)), c.tire_front = ee_.r8(F_ + 0x64E), c.tire_rear = ee_.r8(F_ + 0x64F);
    return c;
}

void PitCheck::enter() {
    const u32 cond = u32(ee_.a(0));
    if (cond != o_.cond) return;
    car_ = int(ee_.a(1));
    if (car_ < 0 || car_ >= o_.cars) return;
    in_ = true;
    ret_ = ee_.g32(31), sp_ = ee_.g32(29);
    counting_ = crossed_ = false, lap_state_ = -1;
    services_.clear(), theirs_.clear();
    const u32 C = o_.car(car_), Fp = C + kF;
    before_ = read_car(Fp);
    sim::ai::PitInputs& in = inputs_;
    in = {};
    in.d = F::raw(ee_.r32(Fp + 0x4A4)), in.d_prev = F::raw(ee_.r32(C + 0x99C));
    in.lap = F(lap_);
    in.speed = F::raw(ee_.r32(Fp + 0x53C)), in.side_speed = F::raw(ee_.r32(Fp + 0x540));
    in.control = gt3r::s8(ee_.r8(Fp + 0x466)), in.kind = ee_.r8(Fp + 0x467), in.full_throttle = ee_.r8(Fp + 0x461);
    in.index = ee_.r8(Fp + 0x440);
    const u32 body = cond + 0xDA78 + u32(in.index) * 0x4F0;
    in.contact_43C = ee_.r32(body + 0x43C) != 0;
    in.contact = in.contact_43C || ee_.r32(body + 0x440) != 0;
    in.lines = s16(ee_.r16(cond + 0xDA5C)) != 0;
    in.race_over = ee_.r8(cond + 0xCBDE) == 2;
    if (!probed_)
        ee_.add_probe(ret_, [this](Ee& e) {
            if (in_ && e.g32(29) == sp_) leave();
        });
    probed_ = true;
}

void PitCheck::leave() {
    in_ = false;
    sim::ai::PitInputs in = inputs_;
    // The race state the pass asked 0x343C20 for, against ours from the same fields.
    if (lap_state_ >= 0) {
        sim::ai::LapStateInputs ls;
        const u32 Fc = o_.car(car_) + kF;
        ls.finished = ee_.r8(Fc + 0x4B0) != 0;
        ls.clock_ms = ee_.r32(o_.cond + 0xF898) / 3;
        ls.limit_minutes = ee_.r16(o_.cond + 0xF850);
        ls.laps = int(ee_.r32(ee_.r32(o_.cond)));
        ls.lap = int(s16(ee_.r16(Fc + 0x4AC)));
        ls.d = inputs_.d, ls.lap_length = inputs_.lap;
        ++lap_calls_;
        if (sim::ai::lap_state(ls) == lap_state_) ++lap_equal_;
    }
    in.counting = counting_, in.crossed = crossed_, in.lap_state = lap_state_ < 0 ? 0 : lap_state_;
    sim::ai::PitCar mine = before_;
    std::vector<sim::ai::PitAction> acts;
    size_t next_service = 0;
    bool service_short = false;
    flow_.step(mine, in, line_, acts, [&]() -> u16 {
        if (next_service < services_.size()) return services_[next_service++];
        service_short = true;
        return 0;
    });
    const u32 Fp = o_.car(car_) + kF;
    const sim::ai::PitCar th = read_car(Fp);
    ++calls_;
    std::vector<int> ours;
    for (const sim::ai::PitAction& a : acts) ours.push_back(a.kind);
    // (a car put in its box, then turned: the program's pose routine calls the body's axes too)
    std::vector<int> theirs;
    for (size_t k = 0; k < theirs_.size(); ++k) {
        if (theirs_[k] == sim::ai::PitAction::Heading && k > 0 && theirs_[k - 1] == sim::ai::PitAction::PlaceInBox) continue;
        theirs.push_back(theirs_[k]);
    }
    bool same = ours == theirs && !service_short && next_service == services_.size();
    auto field = [&](const char* name, int a, int b) {
        if (a != b) {
            same = false;
            if (reported_ < 12) std::printf("  pit car %d step %llu: %s the program's %d, ours %d\n", car_, (unsigned long long)calls_, name, a, b);
        }
    };
    field("flags", th.flags, mine.flags);
    field("service", th.service, mine.service);
    field("hold", th.hold, mine.hold);
    field("speeding", th.speeding, mine.speeding);
    field("message", th.status, mine.status);
    field("message time", th.status_time, mine.status_time);
    field("pad state", th.pad_state, mine.pad_state);
    field("x64C", th.x64C, mine.x64C);
    if (ours != theirs && reported_ < 12) {
        std::printf("  pit car %d step %llu (kind %u, d %.2f): the program's", car_, (unsigned long long)calls_, in.kind, double(in.d.host()));
        for (int k : theirs) std::printf(" %s", action_name(k));
        std::printf("; ours");
        for (int k : ours) std::printf(" %s", action_name(k));
        std::printf("\n");
    }
    if (!acts.empty()) ++active_;
    for (const sim::ai::PitAction& a : acts) ++counts_[a.kind];
    if (same) ++equal_;
    else ++reported_;
}

void PitCheck::need_enter(Ee& e) {
    const u32 C = u32(e.a(0));
    need_car_ = -1;
    for (int i = 0; i < o_.cars; ++i)
        if (o_.car(i) == C) need_car_ = i;
    if (need_car_ < 0) return;
    need_in_ = true, need_allowed_ = false, need_lap_ = 0;
    need_sp_ = e.g32(29);
    need_before_ = read_car(C + kF);
    // what the phase reads, as it is at the call
    sim::ai::PitNeed& n = need_;
    n = {};
    const u32 Fp = C + kF, cond = ee_.r32(C + 4);
    n.fuel_on = ee_.rf(C + 0x5D0) > 0;
    n.fuel = F::raw(ee_.r32(C + 0x5D4)), n.fuel_distance = F::raw(ee_.r32(Fp + 0x4C8)), n.fuel_used = F::raw(ee_.r32(Fp + 0x4C4));
    n.d = F::raw(ee_.r32(Fp + 0x4A4)), n.lap = F(lap_), n.pit_marker = line_.marker(1);
    n.wear_on = ee_.r8(cond + 4 + 0x68) != 0;
    n.wear_limit = F::raw(ee_.r32(cond + 4 + 0x20));
    const u32 rec = 0x620478 + 0x54 * u32(ee_.r8(ee_.r32(C + 0x10) + 0x30));
    const u32 wheels = ee_.r8(rec + 1);
    for (u32 w = 0; w < wheels; ++w) {
        n.wear.push_back(F::raw(ee_.r32(C + 0x164 + 0xEC * w + 0x50)));
        n.burst.push_back(F::raw(ee_.r32(C + 0x164 + 0xEC * w + 0xAC)));
        n.damaged.push_back(ee_.r8(C + 0x1AB + 0xEC * w));
    }
    n.control = gt3r::s8(ee_.r8(Fp + 0x466));
    const u32 ra = e.g32(31);
    if (need_probed_.insert(ra).second)
        ee_.add_probe(ra, [this](Ee& x) {
            if (need_in_ && x.g32(29) == need_sp_) need_leave();
        });
}

void PitCheck::need_leave() {
    need_in_ = false;
    sim::ai::PitNeed n = need_;
    n.allowed = need_allowed_, n.lap_state = need_lap_;
    n.tire_state_4 = false; // (0x35FA50's answer: the car's +0x56A 0 asks the conductor ❓; checked as 0 here)
    sim::ai::PitCar mine = need_before_;
    sim::ai::pit_need(mine, n);
    const sim::ai::PitCar th = read_car(o_.car(need_car_) + kF);
    ++need_calls_;
    if (th.flags == mine.flags && th.tire_out == mine.tire_out) ++need_equal_;
    else if (need_reported_++ < 6)
        std::printf("  request car %d: the program's flags %02X out %u, ours %02X %u\n", need_car_, th.flags, th.tire_out, mine.flags, mine.tire_out);
    if (th.flags & 8 && !(need_before_.flags & 8)) ++need_asked_;
}

void PitCheck::hold_enter(Ee& e) {
    const u32 C = u32(e.a(0));
    hold_car_ = -1;
    for (int i = 0; i < o_.cars; ++i)
        if (o_.car(i) == C) hold_car_ = i;
    if (hold_car_ < 0) return;
    hold_in_ = true;
    hold_sp_ = e.g32(29);
    hold_before_ = read_car(C + kF);
    hold_tires_.clear();
    const u32 ra = e.g32(31);
    if (hold_probed_.insert(ra).second)
        ee_.add_probe(ra, [this](Ee& x) {
            if (hold_in_ && x.g32(29) == hold_sp_) hold_leave();
        });
}

void PitCheck::hold_leave() {
    hold_in_ = false;
    const u32 C = o_.car(hold_car_);
    // each wheel's layout byte (+0x3C + w of the layout record, 0x359538: 0x620478 + 0x54 x the parameter block's +0x30)
    std::vector<gt3r::s8> axle;
    const u32 rec = 0x620478 + 0x54 * u32(ee_.r8(ee_.r32(C + 0x10) + 0x30));
    const u32 n = rec ? ee_.r8(rec + 1) : 4;
    for (u32 w = 0; w < n; ++w) axle.push_back(gt3r::s8(ee_.r8(rec + 0x3C + w)));
    sim::ai::PitCar mine = hold_before_;
    const sim::ai::HoldEvents ev = sim::ai::pit_hold_step(mine, axle);
    const sim::ai::PitCar th = read_car(C + kF);
    ++hold_calls_;
    const bool same = th.hold == mine.hold && th.service == mine.service && th.x64C == mine.x64C && th.tire_front == mine.tire_front &&
                      th.tire_rear == mine.tire_rear && ev.new_tires == hold_tires_;
    if (same) ++hold_equal_;
    else if (hold_reported_++ < 8)
        std::printf("  hold car %d: the program's hold %u service %04X tires %zu, ours %u %04X %zu\n", hold_car_, th.hold, th.service, hold_tires_.size(), mine.hold,
                    mine.service, ev.new_tires.size());
    hold_new_tires_ += ev.new_tires.size();
}

void PitCheck::print() const {
    std::printf("pit request (0x353218): %llu calls, %llu equal, %llu asked\n", (unsigned long long)need_calls_, (unsigned long long)need_equal_,
                (unsigned long long)need_asked_);
    std::printf("race state (0x343C20): %llu calls, %llu equal\n", (unsigned long long)lap_calls_, (unsigned long long)lap_equal_);
    std::printf("stop timing (0x35C0B8): %llu calls, %llu equal (hold, service, tires, the wheels given new tires: %llu)\n", (unsigned long long)hold_calls_,
                (unsigned long long)hold_equal_, (unsigned long long)hold_new_tires_);
    std::printf("pit pass (0x359008 / 0x35B060): %llu calls, %llu equal (record, messages, the routines called), %llu with a pit step;", (unsigned long long)calls_,
                (unsigned long long)equal_, (unsigned long long)active_);
    for (const auto& [k, n] : counts_) std::printf(" %s %llu", action_name(k), (unsigned long long)n);
    std::printf("\n");
}

} // namespace gt4r::diff
