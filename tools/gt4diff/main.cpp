// gt4r_diff: a developer's check of our simulation against the user's own program (diff.h).
//
//   gt4r_diff <disc> [test]      tests: spec (every car's equipment and spec), params (the parameter block's fields)

#include "base/log.h"
#include "diff.h"

#include <cstdio>
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
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
