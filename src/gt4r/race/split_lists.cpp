#include "gt4r/race/split_lists.h"

#include "base/ee_float.h"

#include <algorithm>

namespace gt4r::race {

void SplitLists::reset(int sections) { lists_.assign(size_t(std::max(0, sections)), List{}), cars_.clear(); }

void SplitLists::pass(int car, int round, int section, s64 t) {
    if (section < 0 || section >= sections() || car < 0) return;
    // The section's time from the car's last section end, its best kept with the round (0x3578B0's first part; unsigned).
    if (size_t(car) >= cars_.size()) cars_.resize(size_t(car) + 1);
    Car& c = cars_[size_t(car)];
    if (c.best.size() != lists_.size()) c.best.assign(lists_.size(), 0x157529FFu), c.best_round.assign(lists_.size(), -1);
    const u32 took = u32(t - c.last);
    c.last = t;
    if (took < c.best[size_t(section)]) c.best[size_t(section)] = took, c.best_round[size_t(section)] = u16(round);
    List& l = lists_[size_t(section)];
    if (round > l.round) {
        l.round = round, l.cars.assign(1, {car, t});
        return;
    }
    if (round < l.round) return; // (a car behind a round: the lap's difference event instead, 0x35E8E8)
    // (from the end back past the times later than t: 0x357A08)
    size_t at = l.cars.size();
    while (at > 0 && u64(t) < u64(l.cars[at - 1].second)) --at;
    l.cars.insert(l.cars.begin() + std::ptrdiff_t(at), {car, t});
}

s64 SplitLists::time(int section, int car) const {
    if (section < 0 || section >= sections()) return -1;
    for (const auto& [c, t] : lists_[size_t(section)].cars)
        if (c == car) return t;
    return -1;
}

s64 SplitLists::best(int car, int section) const {
    if (car < 0 || size_t(car) >= cars_.size() || section < 0 || size_t(section) >= cars_[size_t(car)].best.size()) return -1;
    const u32 b = cars_[size_t(car)].best[size_t(section)];
    return b == 0x157529FFu ? -1 : s64(b);
}

int SplitLists::best_round(int car, int section) const {
    if (car < 0 || size_t(car) >= cars_.size() || section < 0 || size_t(section) >= cars_[size_t(car)].best_round.size()) return -1;
    return cars_[size_t(car)].best_round[size_t(section)];
}

u32 sector_color(int section, bool timed, int faster, int cars) {
    if (!timed) return (section & 1) == 0 ? 0x80282828u : 0x80181818u;
    using gt3r::ee::F;
    const F r = gt3r::ee::int_to_f(faster) / gt3r::ee::int_to_f(cars - 1);
    const F rest = F::raw(0x3F800000) - r;
    const F zero = r * F::raw(0);
    const F x02 = rest * F::raw(0x3E4CCCCC);
    const F red = (zero + rest) * F::raw(0x437F0000);
    const F blue = (r * F::raw(0x3ECCCCCC) + x02) * F::raw(0x437F0000);
    const F green = (zero + x02) * F::raw(0x437F0000);
    return u32(gt3r::ee::to_int(red)) | u32(gt3r::ee::to_int(blue)) << 16 | u32(gt3r::ee::to_int(green)) << 8 | 0x80000000u;
}

bool SplitLists::pace_up(int a, int b, int a_section, int a_crossings) const {
    if (a_crossings == 0 || (a_crossings == 1 && a_section == 0) || sections() == 0) return false;
    auto before = [&](int s) { return s - 1 < 0 ? sections() - 1 : s - 1; };
    const int last = before(a_section), prev = before(last);
    const s64 ta = time(last, a), tb = time(last, b);
    if (ta < 0 || tb < 0) return false;
    const s64 now = ta - tb;
    if (now <= 0) return false;
    const s64 pa = time(prev, a), pb = time(prev, b);
    if (pa < 0 || pb < 0) return false;
    const s64 then = pa - pb;
    return then > 0 && now < then;
}

} // namespace gt4r::race
