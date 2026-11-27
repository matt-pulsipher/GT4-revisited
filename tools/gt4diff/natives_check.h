#pragma once

// gt4r_diff's checks of the scripts' natives that are pure routines of the program (docs/RULES_AUDIT.md "The scripts'
// natives"): the calendar's day numbers (MCalendar.dateToDay / dayToDate through 0x5797A8 / 0x579898) against ours
// (menu::date_to_day, menu::day_to_date).

#include "gt4r/disc/disc.h"

#include <cstddef>

namespace gt4r::diff {

struct NativesResult {
    bool ran = false;
    size_t dates = 0, dates_equal = 0, days = 0, days_equal = 0;
};
NativesResult run_natives(Disc& disc, bool print);

} // namespace gt4r::diff
