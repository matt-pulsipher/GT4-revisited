#pragma once

// gt4r_diff's comparison of the race step (step.cpp): the car record of the user's own program read
// into ours (sim::Record: the engine's mirror through sim::layout, and the extra part each area of
// sim::rules keeps), and "taps": routines of the program compared with ours in place, at every call the
// running step makes of them.

#include "orig.h"

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/record.h"
#include "sim/exact/car_record.h"
#include "sim/exact/race_inputs.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gt4r::diff {

namespace ex = gt3r::sim::exact;

// The record's bytes as the program has them.
struct Image {
    std::vector<u8> car, params;
};
Image read_image(Ee& ee, u32 car);
// The members of ours that have a place (sim::layout, sim::extra_fields), from an image; the others stay
// as they are.
void load(sim::Record& r, const Image& image);

// A member of ours that has a place in Gran Turismo 4's record.
struct Slot {
    std::string name; // "w[2].susp"
    u32 size = 0;
    sim::layout::Place place;
    size_t member = 0; // its offset in sim::Record
    bool real = false; // a float
};
const std::vector<Slot>& slots();

// What a routine returns or leaves behind a pointer, which our routine gives at the call's entry and the
// program's is compared with at its exit.
struct Out {
    enum Where : u8 { F0, V0, Mem } where = F0;
    std::string name;
    u32 address = 0; // Mem
    u32 bits = 0;
    u32 size = 4;
    bool real = true;
};

// What a tap's routine is given: the interpreter at the routine's entry (its argument registers), the
// car's record there, and the race's tables as our session builds them.
struct TapCall {
    Ee& ee;
    const OrigRace& race;
    int car_index;
    sim::Record& rec;
    ex::Car& car;
    const ex::RaceInputs& inputs;
    std::vector<Out> outs;
    // The results ours expects the program's call to give.
    void ret_f(const char* name, gt3r::ee::F v) { outs.push_back({Out::F0, name, 0, v.bits, 4, true}); }
    void ret_i(const char* name, u32 v) { outs.push_back({Out::V0, name, 0, v, 4, false}); }
    void mem_f(const char* name, u32 address, gt3r::ee::F v) { outs.push_back({Out::Mem, name, address, v.bits, 4, true}); }
    void mem_i(const char* name, u32 address, u32 v, u32 size = 4) { outs.push_back({Out::Mem, name, address, v, size, false}); }
};
struct Tap {
    u32 address;
    const char* name;
    // Which car the call is for, from the registers at entry (the car's address; 0: not a car's call).
    std::function<u32(Ee&)> car_of;
    // Ours, on our record.
    std::function<void(TapCall&)> ours;
    // Fields not compared (their names' prefixes), for what the routine writes and ours does elsewhere.
    std::vector<std::string> skip = {};
};

struct FieldStat {
    u64 calls = 0, off = 0;
    double worst = 0; // the largest difference (relative for floats)
    std::string example;
};
struct TapStat {
    std::string name;
    u64 calls = 0, clean = 0;         // calls, and those with every compared field equal
    std::map<std::string, FieldStat> fields; // the fields that differed at least once
    std::map<u32, u64> unplaced_car, unplaced_params; // record offsets the program wrote that no field of ours has a place for
    std::map<std::string, u64> written;               // the fields the program changed
};

class Taps {
public:
    Taps(Ee& ee, const OrigRace& race, const ex::RaceInputs& inputs) : ee_(ee), race_(race), inputs_(inputs) {}
    void add(Tap tap);
    const std::vector<TapStat>& stats() const { return stats_; }
    const TapStat* stat(const std::string& name) const {
        for (const TapStat& s : stats_)
            if (s.name == name) return &s;
        return nullptr;
    }
    void print() const;

private:
    struct Pending {
        size_t tap;
        u32 sp, car;
        int index;
        Image before;
        sim::Record mine;
        std::vector<Out> outs;
    };
    void enter(size_t tap);
    void leave(u32 ra);
    Ee& ee_;
    const OrigRace& race_;
    const ex::RaceInputs& inputs_;
    std::vector<Tap> taps_;
    std::vector<TapStat> stats_;
    std::map<u32, std::vector<Pending>> pending_; // by return address
};

} // namespace gt4r::diff
