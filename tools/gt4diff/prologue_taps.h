#pragma once

// Gran Turismo 4 Prologue's car record read in Gran Turismo 3's layout (the shared engine's mirror, sim/exact/car_fields.h),
// and "taps": routines of Prologue's own race step compared in place, at every call, with the engine's routine of the same
// role run on the record as the program had it at the call's entry (docs/formats/prologue.md "The program").
//
// Prologue's record is Gran Turismo 3's without the parameter block: the engine's record offsets from +0xEC0 are Prologue's
// less 0xEAC up to the steering indicators, less 0xEA8 from there; its parameter block (car +0x10 points at it, 0xEDC
// bytes) is Gran Turismo 3's with each tire record 4 bytes longer (0x1B8) and 20 more bytes before the wear's load terms.

#include "prologue_race.h"

#include "sim/exact/car_record.h"
#include "sim/exact/race_inputs.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gt4r::diff::prologue {

namespace ex = gt3r::sim::exact;

// Where Prologue keeps what the engine's record has at `engine` (an offset in Gran Turismo 3's car record): in the car
// record (params false) or the parameter block.
struct Where {
    bool params = false;
    u32 offset = 0;
};
Where place(u32 engine);
// The record of the car at `car` as a Gran Turismo 3 record image (ex::kCarSize bytes).
std::vector<u8> gt3_image(Ee& ee, u32 car);

struct PTapCall {
    Ee& ee;
    u32 car_addr;
    int car_index;
    ex::Car& car;
    const ex::RaceInputs& inputs;
};
struct PTap {
    u32 address;
    const char* name;
    std::function<void(PTapCall&)> ours;
    std::vector<std::string> skip = {}; // fields not compared (prefixes of their names)
};
struct PFieldStat {
    u64 off = 0;
    double worst = 0;
    std::string example;
};
struct PTapStat {
    std::string name;
    u64 calls = 0, clean = 0;
    std::map<std::string, PFieldStat> fields;
    std::map<std::string, u64> written;
};

class PTaps {
public:
    PTaps(Ee& ee, const Race& race, const ex::RaceInputs& inputs) : ee_(ee), race_(race), inputs_(inputs) {}
    void add(PTap tap);
    const std::vector<PTapStat>& stats() const { return stats_; }
    void print() const;

private:
    struct Pending {
        size_t tap;
        u32 sp, car;
        int index;
        ex::Car before, mine;
    };
    void enter(size_t tap);
    void leave(u32 ra);
    Ee& ee_;
    const Race& race_;
    const ex::RaceInputs& inputs_;
    std::vector<PTap> taps_;
    std::vector<PTapStat> stats_;
    std::map<u32, std::vector<Pending>> pending_;
};

// The engine's routines at their Prologue counterparts (prologue_taps.cpp).
void add_engine_taps(PTaps& taps);
// Gran Turismo 4's routines and Prologue's own (rules_prologue.h) at their Prologue counterparts, on the Gran Turismo 4
// part of the record made from Prologue's (rules_prologue.h's prologue_extra).
void add_prologue_taps(PTaps& taps, const Disc& disc);
// The engine's driver input (the pad's frame of the race's pad 0) and preparation at Prologue's 0x3DEDE0 / 0x3BF608.
void add_input_taps(PTaps& taps, const Race& race);

// The test drive (prologue_step.cpp's): a standing start at full throttle, a lift, turns each way, the brakes.
Pad test_drive(int step);

// Prologue's step run `steps` steps of the test drive on a course with a car, with taps: "engine" (the engine's routines
// Prologue shares), "prologue" (Prologue's own and Gran Turismo 4's), "input". Empty with `err` set when it cannot run.
std::vector<PTapStat> check(Disc& disc, const std::string& course, const std::string& car, int steps, const std::string& which,
                            std::string& err);

} // namespace gt4r::diff::prologue
