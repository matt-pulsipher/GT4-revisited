// gt4r_diff's checks of the scripts' natives that are pure routines of the program (natives_check.h): the program's own
// routine run in the interpreter on many inputs against ours.

#include "natives_check.h"

#include "orig.h"
#include "gt4r/menu/game_internal.h"
#include "gt4r/sim/program.h"

#include <cstdio>

namespace gt4r::diff {

namespace {

constexpr u32 kDateToDay = 0x5797A8; // (year, month, day): the day's number (MCalendar.dateToDay 0x135F50)
constexpr u32 kDayToDate = 0x579898; // (day): year u16, month u8, day u8 packed (MCalendar.dayToDate 0x1360A0)

} // namespace

NativesResult run_natives(Disc& disc, bool print) {
    NativesResult r;
    db::SpecDb sdb = open_db(disc);
    if (!sim::Program::load(disc).known()) return r;
    r.ran = true;
    Ee ee;
    boot(ee, disc, sdb);
    // Every day from 1950 to 2049 and months out of 1 .. 12 (the scripts step a month on past December).
    for (int y = 1950; y < 2050; y += 1)
        for (int m = -13; m <= 26; ++m)
            for (int d = 1; d <= 31; d += (m >= 1 && m <= 12) ? 1 : 9) {
                const u32 theirs = u32(ee.call(kDateToDay, {Arg::integer(y), Arg::integer(m), Arg::integer(d)}).v0_s32());
                const u32 ours = menu::date_to_day(y, m, d);
                ++r.dates;
                if (theirs == ours) ++r.dates_equal;
                else if (print && r.dates - r.dates_equal <= 5) std::printf("  dateToDay(%d, %d, %d): program %u, ours %u\n", y, m, d, theirs, ours);
            }
    const u32 first = menu::date_to_day(1950, 1, 1), last = menu::date_to_day(2050, 1, 1);
    for (u32 day = first; day < last; ++day) {
        const u32 packed = u32(ee.call(kDayToDate, {Arg::integer(s32(day))}).v0.lo);
        int y = 0, m = 0, d = 0;
        menu::day_to_date(day, y, m, d);
        ++r.days;
        if (int(packed & 0xFFFF) == y && int((packed >> 16) & 0xFF) == m && int(packed >> 24) == d) ++r.days_equal;
        else if (print && r.days - r.days_equal <= 5)
            std::printf("  dayToDate(%u): program %u-%u-%u, ours %d-%d-%d\n", day, packed & 0xFFFF, (packed >> 16) & 0xFF, packed >> 24, y, m, d);
    }
    if (print) std::printf("natives: dateToDay equal on %zu of %zu, dayToDate %zu of %zu\n", r.dates_equal, r.dates, r.days_equal, r.days);
    return r;
}

} // namespace gt4r::diff
