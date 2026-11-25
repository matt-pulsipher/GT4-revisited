#include "gt4r/race/split_battle.h"

#include <algorithm>

namespace gt4r::race {

SplitRules SplitRules::from_options(const std::map<std::string, std::variant<s32, float, std::string>>& options) {
    SplitRules r;
    const auto number = [&](const char* name, int& out) {
        if (const auto it = options.find(name); it != options.end())
            if (const s32* v = std::get_if<s32>(&it->second)) out = *v;
    };
    number("split_race_laps", r.laps), number("split_tire_damage", r.tire_damage), number("split_handicap", r.handicap), number("split_boost_level", r.boost);
    return r;
}

void put_boost(std::vector<u8>& row, int level) {
    if (row.size() < 0x6B) return;
    const bool on = level == 1 || level == 2;
    row[0x64] = on ? 0 : 0xFF;
    row[0x68] = level == 1 ? 7 : level == 2 ? 20 : 0;
    row[0x69] = level == 1 ? 20 : level == 2 ? 10 : 0;
    row[0x6A] = on ? 20 : 0;
}

std::optional<CarStart> handicap_start(int handicap, size_t car) {
    if (handicap == 0) return std::nullopt;
    CarStart s;
    s.kind = 1;
    if (handicap > 0) s.distance = car == 0 ? -10 : -std::max(handicap, 20);
    else s.distance = car == 0 ? std::min(handicap, -20) : -10;
    return s;
}

int split_tire(int course_kind, int chosen) { return course_kind == 2 ? 11 : course_kind == 3 ? 12 : chosen; }

SplitView split_view(bool side_by_side, int half) {
    SplitView v;
    if (side_by_side) {
        v.lens = 1, v.mirror = {90, 41, 140, 46}, v.mirror_zoom = 1.3f, v.picture = {320, 448};
    } else {
        v.lens = 1.2f, v.mirror = {200, half == 0 ? 16.0f : 8.0f, 240, 40}, v.mirror_zoom = 2, v.picture = {640, 224};
    }
    return v;
}

void ArrivalBoard::arrive(size_t car, int lap, size_t point, s64 time_ms) {
    if (point >= points_.size()) return;
    Point& p = points_[point];
    if (lap > p.lap) {
        p.lap = lap, p.told = 0;
        p.arrivals.assign(1, {car, time_ms});
    } else if (lap == p.lap) {
        // Among those not told yet, by their times.
        size_t at = p.arrivals.size();
        while (at > p.told && at > 0 && time_ms < p.arrivals[at - 1].ms) --at;
        p.arrivals.insert(p.arrivals.begin() + ptrdiff_t(at), {car, time_ms});
    } else laps_down_.push_back({car, p.lap - lap});
}

} // namespace gt4r::race
