#include "gt4r/race/split_lists.h"

#include <algorithm>

namespace gt4r::race {

void SplitLists::reset(int sections) { lists_.assign(size_t(std::max(0, sections)), List{}); }

void SplitLists::pass(int car, int round, int section, s64 t) {
    if (section < 0 || section >= sections()) return;
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
