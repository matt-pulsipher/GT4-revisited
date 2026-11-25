#pragma once

// gt4r_diff: the whole race step from the program's state (whole_step.cpp): at a chosen step every car's record and
// collision body of the program's race are loaded into our session, both step once with the same drivers' outputs, and
// every placed field is compared. GT4R_AI_WHOLEFREE: the state loaded at the first step only, ours then running free on the
// drivers' outputs of the program's steps; =2: on the outputs of our drivers on our session (their own field), each
// compared with the program's race's drivers' outputs.

#include "orig.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gt4r::sim {
class Session;
namespace ai {
class Field;
}
} // namespace gt4r::sim

namespace gt4r::diff {

class WholeStep {
public:
    WholeStep(Ee& ee, const OrigRace& race, Disc& disc, const std::string& course, const std::vector<std::string>& cars, const sim::ai::Field* field);
    ~WholeStep();
    // Before the program's step: the state into ours. After it: ours stepped with the drivers' outputs of the program's
    // step, then compared.
    void before();
    void after();
    void print() const;

private:
    struct Stat {
        u64 off = 0;
        double worst = 0;
        std::string example, first;
    };
    Ee& ee_;
    const OrigRace& race_;
    std::unique_ptr<sim::Session> session_;
    std::map<std::string, Stat> fields_;
    u64 steps_ = 0, clean_ = 0;
    bool free_ = false, loaded_ = false;
    std::unique_ptr<sim::ai::Field> own_; // (GT4R_AI_WHOLEFREE=2)
    std::map<std::string, Stat> inputs_;
};

} // namespace gt4r::diff
