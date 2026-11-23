#pragma once

// gt4r_diff's comparison of the race step (step.cpp): the car record of the user's own program read
// into the engine's mirror through sim::layout, and "taps": routines of the program compared with ours
// in place, at every call the running step makes of them.

#include "orig.h"

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/program.h"
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
// The mirror's members that have a place (sim::layout), from an image; the others stay as they are.
void load(ex::Car& c, const Image& image);

// A field of the mirror that has a place in Gran Turismo 4's record.
struct Slot {
    std::string name; // "w[2].susp"
    u32 engine = 0, size = 0;
    sim::layout::Place place;
    size_t member = 0; // its offset in ex::Car
    bool real = false; // a float
};
const std::vector<Slot>& slots();

// What a tap's routine is given: the interpreter at the routine's entry (its argument registers), the
// car's record there in the mirror, and the race's tables as our session builds them.
struct TapCall {
    Ee& ee;
    const OrigRace& race;
    int car_index;
    ex::Car& car;
    const ex::RaceInputs& inputs;
};
struct Tap {
    u32 address;
    const char* name;
    // Which car the call is for, from the registers at entry (the car's address; 0: not a car's call).
    std::function<u32(Ee&)> car_of;
    // Ours, on the mirror.
    std::function<void(TapCall&)> ours;
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
    void print() const;

private:
    struct Pending {
        size_t tap;
        u32 sp, car;
        int index;
        Image before;
        ex::Car mine;
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
