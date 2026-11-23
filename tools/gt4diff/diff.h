#pragma once

// A developer's check of our simulation against the user's own program: tools/gt4diff/diff.cpp.

#include "gt4r/disc/disc.h"

#include <string>
#include <vector>

namespace gt4r::diff {

// Every car's equipment as sold (the program's set-car) and spec (its spec builder, on a paved and
// on loose courses) against sim::stock_car and sim::build_spec, byte for byte.
struct SpecResult {
    bool ran = false; // the disc's program is a mapped build
    size_t cars = 0, equipment_equal = 0, spec_equal = 0;
};
SpecResult run_spec(Disc& disc, bool print);

// Every car's parameter block: the program's converter on its spec against the engine's converter on
// our translated spec with sim::gt4_params, field by field (equal: within a part in 100,000).
struct Field {
    std::string name;
    size_t values = 0, off = 0; // compared, and not equal
    double worst = 0;           // the largest relative difference
    std::string where;
};
struct ParamsResult {
    bool ran = false;
    size_t cars = 0, unfinished = 0;
    std::vector<Field> fields;
    const Field* field(const std::string& name) const {
        for (const Field& f : fields)
            if (f.name == name) return &f;
        return nullptr;
    }
};
ParamsResult run_params(Disc& disc, bool print);

bool run_step(Disc& disc, bool print);

} // namespace gt4r::diff
