// gt4r_diff: a developer's check of our simulation against the user's own program (diff.h).
//
//   gt4r_diff <disc> [test]      tests: spec (every car's equipment and spec), params (the parameter block's fields)
//   gt4r_diff <disc> ai [course] [cars] [laps]   the program's own computer drivers run a field (ai.cpp)
//   gt4r_diff <disc> showroom [cars]             every car's showroom figures and evaluation (showroom.cpp)
//   gt4r_diff <disc> states <state 0> <state 1> [keys]   the step from the original's saved states (states.cpp)

#include "base/log.h"
#include "diff.h"
#include "floats.h"
#include "prologue_race.h"
#include "save_check.h"
#include "showroom_check.h"
#include "states.h"

namespace gt4r::diff {
int run_posture(Disc& disc, const std::vector<std::string>& paths); // posture.cpp
int run_gear(Disc& disc, const std::vector<std::string>& paths);    // gear.cpp
int run_course_script(Disc& disc, const std::string& course, const std::vector<std::string>& paths); // course_script.cpp
}

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
        if (test == "prologue-near") // gt4r_diff <prologue disc> prologue-near [course] [car,car,...] [steps]
            return gt4r::diff::prologue::run_near(*disc, argc > 3 ? argv[3] : "tsukuba", argc > 4 ? argv[4] : "civic_r_98,impreza_wrc_03,civic_r_98", argc > 5 ? std::atoi(argv[5]) : 1200);
        if (test == "save" && argc > 3) return gt4r::diff::run_save_check(*disc, argv[3]); // gt4r_diff <disc> save <folder or card image> (save_check.cpp)
        if (test == "prologue-passcode") return gt4r::diff::prologue::run_passcode(*disc, argc > 3 ? std::atoi(argv[3]) : 200) ? 0 : 1;
        if (test == "prologue-save") return gt4r::diff::prologue::run_save_layout(*disc) ? 0 : 1; // gt4r_diff <prologue disc> prologue-save
        if (test == "prologue-start") { // gt4r_diff <prologue disc> prologue-start (prologue_start_tables.cpp)
            const auto st = gt4r::diff::prologue::start_tables_check(*disc, true);
            std::printf("%d of %d values equal%s%s\n", st.equal, st.checks, st.err.empty() ? "" : ": ", st.err.c_str());
            return st.err.empty() && st.equal == st.checks ? 0 : 1;
        }
        if (test == "prologue-demo") return gt4r::diff::prologue::run_demo(*disc, argc > 3 ? argv[3] : "l0p0005", argc > 4 ? std::atoi(argv[4]) : 100000); // <label> [steps] (prologue_demo.cpp)
        if (test == "prologue") // gt4r_diff <prologue disc> prologue [course] [car] [steps] (prologue_step.cpp)
            return gt4r::diff::prologue::run(*disc, argc > 3 ? argv[3] : "tsukuba", argc > 4 ? argv[4] : "civic_typer_97", argc > 5 ? std::atoi(argv[5]) : 1200);
        if (test == "showroom") ok = gt4r::diff::run_showroom(*disc, true, argc > 3 ? size_t(std::atoi(argv[3])) : 0).ran && ok;
        if (test == "showroom" || test == "picker") ok = gt4r::diff::run_picker(*disc, true).ran && ok;
        if (test == "showroom" || test == "charts") ok = gt4r::diff::run_charts(*disc, true).ran && ok;
        if (test == "showroom" || test == "book") ok = gt4r::diff::run_bspec_book(*disc, true).ran && ok;
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
        if (test == "bspec") // gt4r_diff <disc> bspec: the B-spec skill on made-up records
            ok = gt4r::diff::run_bspec_skill(*disc, true, {"s500_63", "nsx_r_02", "ka_01", "_787b_91", "impreza_wrx_sti_02"}).ran && ok;
        if (test == "states") { // gt4r_diff <disc> states <state 0 eeMemory.bin> <state 1 eeMemory.bin> [keys] (states.cpp)
            gt4r::diff::StatesOptions so;
            if (argc > 5) so.keys = argv[5];
            if (const char* e = std::getenv("GT4R_STATES_FRAMES")) so.frames = std::atoi(e);
            so.resync = std::getenv("GT4R_STATES_RESYNC") != nullptr;
            so.ours = std::getenv("GT4R_STATES_PROGRAM_ONLY") == nullptr;
            if (const char* e = std::getenv("GT4R_STATES_OWN_START")) so.own_start = std::atoi(e);
            const gt4r::diff::StatesResult r = gt4r::diff::run_states(*disc, argc > 3 ? argv[3] : "", argc > 4 ? argv[4] : "", so, true);
            if (!r.ran) std::printf("states: %s\n", r.error.c_str());
            return r.ran ? 0 : 1;
        }
        if (test == "course_script" && argc > 4) return gt4r::diff::run_course_script(*disc, argv[3], std::vector<std::string>(argv + 4, argv + argc)); // gt4r_diff <disc> course_script <course> <eeMemory.bin>...
        if (test == "posture") return gt4r::diff::run_posture(*disc, std::vector<std::string>(argv + 3, argv + argc)); // gt4r_diff <disc> posture <eeMemory.bin>...
        if (test == "gear") return gt4r::diff::run_gear(*disc, std::vector<std::string>(argv + 3, argv + argc)); // gt4r_diff <disc> gear <eeMemory.bin>...
        if (test == "floats") return gt4r::diff::run_floats(*disc, std::vector<std::string>(argv + 3, argv + argc)); // (floats.h)
        if (test == "demo") ok = gt4r::diff::run_demo(*disc, true, argc > 3 ? argv[3] : "usl0b0001").ran && ok; // gt4r_diff <disc> demo [label]
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
