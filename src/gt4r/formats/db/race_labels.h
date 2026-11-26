#pragma once

// The labels a license test's, coffee break's or mission's code ("l0b0001") is looked up by in the RACE table (0x430D20): as it
// is, under the program's region ("usl0b0001" on SCUS-97328, "ukl0b0001" on SCES-51719, "jpl0b0001" on SCPS-17001; DiscInfo /
// RuntimeConfig region), then under "jp". The same names the demonstrations' files (replay/license/<label>).

#include <array>
#include <string>

namespace gt4r::db {

inline std::array<std::string, 3> race_labels(const std::string& label, const std::string& region) {
    return {label, (region.empty() ? std::string("us") : region) + label, "jp" + label};
}

} // namespace gt4r::db
