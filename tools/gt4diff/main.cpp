// gt4r_diff: a developer's check of our simulation against the user's own program (diff.h).
//
//   gt4r_diff <disc> [test]      tests: spec (every car's equipment and spec), params (the parameter block's fields)
//   gt4r_diff <disc> ai [course] [cars] [laps]   the program's own computer drivers run a field (ai.cpp)
//   gt4r_diff <disc> showroom [cars]             every car's showroom figures and evaluation (showroom.cpp)

#include "base/log.h"
#include "diff.h"
#include "showroom_check.h"

namespace gt4r::diff::prologue {
int run(Disc& disc, const std::string& course, const std::string& car, int steps);
}

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: gt4r_diff <disc.iso> [spec | params]\n");
        return 2;
    }
    try {
        gt3r::log_set_level(gt3r::LogLevel::Warn);
        const std::unique_ptr<gt4r::Disc> disc = gt4r::Disc::open(argv[1]);
        const std::string test = argc > 2 ? argv[2] : "";
        bool ok = true;
        if (test.empty() || test == "spec") {
            const gt4r::diff::SpecResult r = gt4r::diff::run_spec(*disc, true);
            if (!r.ran) std::printf("spec: the disc's program is not a mapped build\n");
            ok = ok && r.ran && r.equipment_equal == r.cars && r.spec_equal == r.cars;
        }
        if (test.empty() || test == "params") ok = gt4r::diff::run_params(*disc, true).ran && ok;
        if (test.empty() || test == "leaves") ok = gt4r::diff::run_leaves(*disc, true).ran && ok;
        if (test.empty() || test == "step") ok = gt4r::diff::run_step(*disc, true).ran && ok;
        if (test == "prologue") // gt4r_diff <prologue disc> prologue [course] [car] [steps] (prologue_step.cpp)
            return gt4r::diff::prologue::run(*disc, argc > 3 ? argv[3] : "tsukuba", argc > 4 ? argv[4] : "civic_typer_97", argc > 5 ? std::atoi(argv[5]) : 1200);
        if (test == "showroom") ok = gt4r::diff::run_showroom(*disc, true, argc > 3 ? size_t(std::atoi(argv[3])) : 0).ran && ok;
        if (test == "showroom" || test == "picker") ok = gt4r::diff::run_picker(*disc, true).ran && ok;
        if (test == "showroom" || test == "charts") ok = gt4r::diff::run_charts(*disc, true).ran && ok;
        if (test == "ai") { // gt4r_diff <disc> ai [course] [car,car,...] [laps]
            std::vector<std::string> cars;
            const std::string list = argc > 4 ? argv[4] : "_1100_standard_type11_49,z_act_70,n360_67,midget2_d_type_98,s500_63,ka_01";
            for (size_t a = 0; a <= list.size();) {
                const size_t b = std::min(list.find(',', a), list.size());
                if (b > a) cars.push_back(list.substr(a, b - a));
                a = b + 1;
            }
            ok = gt4r::diff::run_ai(*disc, true, argc > 3 ? argv[3] : "tsukuba", cars, argc > 5 ? std::atoi(argv[5]) : 3).ran && ok;
        }
        if (test == "demo") ok = gt4r::diff::run_demo(*disc, true, argc > 3 ? argv[3] : "usl0b0001").ran && ok; // gt4r_diff <disc> demo [label]
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
