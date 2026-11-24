#pragma once

// gt4r_diff: the collision step (0x45BB60) and the road query (0x3E5A30) compared with ours at every call of a running
// race of the program's (collide_check.cpp).

#include "orig.h"

#include "gt4r/sim/collide.h"
#include "gt4r/sim/ground.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gt4r::course {
struct Runway;
}

namespace gt4r::diff {

class CollideCheck {
public:
    CollideCheck(Ee& ee, const OrigRace& race, Disc& disc, const std::string& course);
    ~CollideCheck();
    void install(bool bodies, bool queries);
    void print() const;
    u64 calls() const { return calls_; }
    u64 clean() const { return clean_; }
    u64 query_calls() const { return qcalls_; }
    u64 query_clean() const { return qclean_; }

private:
    struct Stat {
        u64 off = 0;
        std::string example;
    };
    struct Pending {
        u32 manager = 0, sp = 0;
        std::vector<sim::collide::Body> bodies;
        std::vector<sim::collide::CarIn> cars;
        std::vector<gt3r::ee::F> rumble;
        std::vector<u32> car_addr;
    };
    struct PendingQuery {
        u32 answer = 0, sp = 0;
        bool hit = false;
        sim::GroundHit mine;
    };
    void enter_step();
    void leave_step(u32 ra);
    void enter_query();
    void leave_query(u32 ra);
    void note(const std::string& name, double theirs, double ours, const char* where);
    void qnote(const std::string& name, double theirs, double ours, const char* where);

    Ee& ee_;
    const OrigRace& race_;
    std::unique_ptr<course::Runway> road_;
    std::unique_ptr<sim::RoadGround> ground_;
    std::unique_ptr<sim::collide::Walls> walls_;
    std::map<u32, std::vector<Pending>> steps_;
    std::map<u32, std::vector<PendingQuery>> queries_;
    std::map<std::string, Stat> fields_, qfields_;
    u64 calls_ = 0, clean_ = 0, qcalls_ = 0, qclean_ = 0;
};

} // namespace gt4r::diff
