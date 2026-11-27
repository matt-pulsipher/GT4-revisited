#include "gt4r/race/race_flow.h"

#include "base/ee_float.h"
#include "base/ee_math.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace gt4r::race {

std::vector<int> grid_places(size_t entries, size_t player) {
    std::vector<int> place(entries, 0);
    int next = 0;
    for (size_t i = 0; i < entries; ++i)
        if (i != player) place[i] = next++;
    if (player < entries) place[player] = int(entries) - 1;
    return place;
}

int start_hold_steps(StartType type, int given_seconds) {
    switch (type) {
    case StartType::Given: return std::max(0, given_seconds) * 60;
    case StartType::Eight: return 8 * 60;
    case StartType::Ten: return 10 * 60;
    default: return 0;
    }
}

bool row_start(std::span<const u8> row, StartType& type, int& seconds) {
    if (row.size() <= 0x41) return false;
    switch (row[0x40]) {
    case 1: type = StartType::Eight; break;
    case 2: type = StartType::Ten; break;
    case 3: type = StartType::None; break;
    default: type = StartType::Given; break;
    }
    seconds = row[0x41];
    return true;
}

int start_crossings(float v, float lap_length, std::span<const float> sectors, float goal) {
    for (const float end : sectors)
        if (v < end) return 1; // (not in the last sector)
    if (goal >= 0 && v < goal) return 1;
    return v < lap_length * 0.5f ? 1 : 0;
}

void RaceFlow::begin(const FlowSetup& setup, std::span<const CarState> cars) {
    setup_ = setup;
    cars_.assign(cars.size(), CarRecord{});
    for (size_t i = 0; i < cars.size(); ++i) {
        CarRecord& r = cars_[i];
        r.prev_v = cars[i].course_v, r.has_v = true;
        if (i < setup.crossings.size()) r.counter = setup.crossings[i];
        else if (setup.counted && setup.lap_length > 0) r.counter = start_crossings(cars[i].course_v, setup.lap_length, setup.sectors, setup.goal);
        if (i < setup.past_line.size() && setup.past_line[i]) r.counter = 1;
        if (i < setup.marks.size()) r.mark = setup.marks[i];
        if (i < setup.back.size()) r.back = setup.back[i];
    }
    practice_.clear();
    if (setup.practice_grid.size() == cars.size()) { // (vf2 0x3F5270: the list by grid place)
        practice_.resize(cars.size());
        for (size_t i = 0; i < cars.size(); ++i) practice_[i] = i;
        std::stable_sort(practice_.begin(), practice_.end(), [&](size_t a, size_t b) { return setup.practice_grid[a] < setup.practice_grid[b]; });
    }
    signal_start_ = false;
    claims_.clear();
    hold_total_ = hold_ = start_hold_steps(setup.start, setup.start_seconds);
    clock_ = 0;
    finishers_ = 0;
    player_finish_steps_ = -1;
    started_steps_ = setup.start == StartType::Rolling || setup.start == StartType::Moving ? -1 : hold_ > 0 ? -1 : 0;
    // A rolling start's clock stands until the first car takes the line (K +0xCBE4, cleared by vf20 0x347928).
    clock_running_ = setup.start != StartType::Rolling || setup.player_clock;
    order_.resize(cars.size());
    sort_order(cars);
}

float RaceFlow::progress(size_t i, const CarState& c) const {
    // Behind the line before the first crossing.
    const float v = cars_[i].counter == 0 && c.course_v > setup_.lap_length * 0.5f ? c.course_v - setup_.lap_length : c.course_v;
    return float(cars_[i].counter) * setup_.lap_length + v;
}

void RaceFlow::sort_order(std::span<const CarState> cars) {
    if (!practice_.empty()) {
        // A free practice's places (vf3 0x3F5350): the list kept from step to step, sorted by the best lap (0x35E3F0, unsigned:
        // no time is 0x157529FF), a car moved up only past a strictly slower one.
        auto best = [&](size_t i) { return cars_[i].best_lap_ms < 0 ? u64(0x157529FF) : u64(cars_[i].best_lap_ms); };
        std::stable_sort(practice_.begin(), practice_.end(), [&](size_t a, size_t b) { return best(a) < best(b); });
        order_ = practice_;
        return;
    }
    for (size_t i = 0; i < order_.size(); ++i) order_[i] = i;
    std::stable_sort(order_.begin(), order_.end(), [&](size_t a, size_t b) {
        const CarRecord &ra = cars_[a], &rb = cars_[b];
        // 0x3659E8: more crossings of the line first; on equal crossings a car still running ahead of a finished one (behind it in
        // a test that ends at its goal and on a course without a lap), two finished ones by their finish, two running ones by
        // their distance.
        if (ra.counter != rb.counter) return ra.counter > rb.counter;
        if (ra.finished != rb.finished) return (setup_.test_kind == 2 || setup_.no_lap) ? ra.finished : !ra.finished;
        if (ra.finished) return ra.finish_order < rb.finish_order;
        return progress(a, cars[a]) > progress(b, cars[b]);
    });
}

// A car's start counted (the conductor's virtual 20, at ms `t` of the clock): the license's (0x3F64D0) and the single
// player's (0x347928).
void RaceFlow::start_car(size_t i, s64 t, Events& ev) {
    CarRecord& r = cars_[i];
    ev.started.push_back(i);
    if (!practice_.empty()) { // (a free practice's, vf20 0x3F50F0: its times from here)
        r.base_ms = t;
        return;
    }
    if (setup_.start == StartType::Moving) {
        if (r.mark == 0) r.base_ms = setup_.mission ? 0 : t;
        if (i == setup_.player && started_steps_ < 0) ev.start = true, started_steps_ = 0;
        return;
    }
    if (setup_.start != StartType::Rolling) return;
    // The first car over the line starts a rolling start's clock; the player's crossing is his start signal (event 4, car 0).
    if (setup_.player_clock) r.base_ms = r.start_ms = t, ev.clock_started = i == setup_.player; // (Prologue's 0x3B4C88: the car's own)
    if (!clock_running_) clock_running_ = true, ev.clock_started = true;
    if (i == setup_.player && started_steps_ < 0) ev.start = true, started_steps_ = 0;
}

void RaceFlow::end_car(size_t i, s64 at, Events& ev) {
    CarRecord& r = cars_[i];
    r.finished = true;
    r.finish_order = finishers_++;
    r.seen_units = at;
    r.finish_units = i < claims_.size() && claims_[i] >= 0 ? claims_[i] : at;
    if (!claims_.empty()) order_finishers();
    ev.finished.push_back(i);
    if (i == setup_.player) ev.player_finished = true, player_finish_steps_ = 0;
}

void RaceFlow::claim_finish(size_t car, s64 units, s64 best_ms) {
    if (car >= cars_.size()) return;
    claims_.resize(cars_.size(), -1);
    claims_[car] = units;
    if (best_ms >= 0) cars_[car].best_lap_ms = best_ms;
    if (cars_[car].finished) cars_[car].finish_units = units;
    order_finishers();
}

void RaceFlow::order_finishers() {
    std::vector<size_t> f;
    for (size_t i = 0; i < cars_.size(); ++i)
        if (cars_[i].finished) f.push_back(i);
    std::stable_sort(f.begin(), f.end(), [&](size_t a, size_t b) {
        if (cars_[a].counter != cars_[b].counter) return cars_[a].counter > cars_[b].counter;
        return cars_[a].finish_units < cars_[b].finish_units;
    });
    for (size_t k = 0; k < f.size(); ++k) cars_[f[k]].finish_order = int(k);
}

void RaceFlow::resume(size_t car, int crossings, s64 base_ms, s64 clock, float v) {
    if (car >= cars_.size()) return;
    CarRecord& r = cars_[car];
    r.counter = crossings, r.base_ms = base_ms, r.prev_v = v, r.back = 0;
    hold_ = 0, clock_ = clock, clock_running_ = true, started_steps_ = 0;
}

// 0x358608 (the step's end, before the clock's step): a car's distance along the course against its last, in the program's
// arithmetic.
void RaceFlow::cross(size_t i, float was, float now, Events& ev) {
    using gt3r::ee::F;
    CarRecord& r = cars_[i];
    const F prev(was), cur(now), lap(setup_.lap_length), half = F::raw(0x3F000000), fifty = F::raw(0x42480000);
    auto units = [&](F share) { return s64(gt3r::ee::to_int((share * fifty).bits)); };
    if (prev <= cur) {
        if (prev + lap * half < cur) { // backwards over the line
            r.back = r.back < 250 ? u8(r.back + 1) : u8(250); // (0x358608: at most 250)
            return;
        }
        if (r.back != 0 || r.counter == 0 || r.finished) return;
        const F run = cur - prev;
        // A section's start: the license's virtual 22 (the mark cleared, its start counted), then the mark 2.
        if (r.mark == 1 && setup_.section_start > 0) {
            const F d(setup_.section_start);
            if (prev < d && d <= cur) {
                const s64 t = clock_ms(clock_ + units((d - prev) / run));
                r.mark = 0;
                start_car(i, t, ev);
                r.mark = 2;
            }
        }
        // The goal (virtual 23: a test of kind 2 ends there).
        if (setup_.goal > 0 && (setup_.test_kind == 2 || setup_.machine_test != 0)) {
            const F d(setup_.goal);
            if (prev < d && d <= cur) {
                end_car(i, clock_ + units((d - prev) / run), ev);
                if (setup_.machine_test != 0) r.best_lap_ms = run_ms(i); // (❓ where the program writes it: read back by 0x35E3F0)
            }
        }
        return;
    }
    if (!(cur + lap * half < prev)) return; // (a little backwards)
    if (r.back != 0) {
        --r.back;
        return;
    }
    if (r.finished) return;
    const F before = lap - prev;
    const s64 at = clock_ + units(before / (before + cur));
    const s64 t = clock_ms(at);
    const int old = r.counter++;
    if (old == 0) {
        start_car(i, t, ev);
        return;
    }
    // A lap (virtual 21).
    r.laps_done += 1;
    // (0x3F6588: a test with other cars, not of kind 6, keeps and shows no lap times)
    if (setup_.test_kind == 0 || setup_.test_kind == 6 || cars_.size() <= 1) {
        const s64 ms = t - r.base_ms;
        r.last_lap_ms = ms;
        if (r.best_lap_ms < 0 || ms < r.best_lap_ms) r.best_lap_ms = ms;
        ev.lap_done = int(i);
    }
    if (setup_.test_kind != 0) { // the license's (0x3F6588): kinds 3 and 6 end at the laps
        if ((setup_.test_kind == 3 || setup_.test_kind == 6) && setup_.laps == old) end_car(i, at, ev);
        return;
    }
    // The finish (0x357B98): the leader has finished, or this car has driven the race's laps, or the race's time is over (the
    // crossing's moment, ms, at or past the limit: 0x34C158) and this car leads.
    const size_t leader = order_.empty() ? i : order_[0];
    const bool time_over = setup_.limit_ms > 0 && t >= setup_.limit_ms;
    if (cars_[leader].finished || (setup_.laps > 0 && old >= setup_.laps) || (time_over && leader == i)) end_car(i, at, ev);
    r.base_ms = t;
}

RaceFlow::Events RaceFlow::step(std::span<const CarState> cars) {
    Events ev;
    if (cars.size() != cars_.size()) return ev;
    if (std::exchange(signal_start_, false) && started_steps_ < 0) ev.start = true, started_steps_ = 0;
    if (hold_ > 0) {
        // The hold: counted down before the step, the clock stands; a beep at each of the last three whole seconds.
        --hold_;
        if (hold_ > 0 && hold_ < 240 && hold_ % 60 == 0) ev.beep = true;
        if (hold_ > 0) {
            for (size_t i = 0; i < cars.size(); ++i) cars_[i].prev_v = cars[i].course_v;
            return ev;
        }
        ev.start = true, started_steps_ = -1;
    }
    const bool was_started = started_steps_ >= 0;
    if (setup_.lap_length > 0)
        for (size_t i = 0; i < cars.size(); ++i) {
            cross(i, cars_[i].prev_v, cars[i].course_v, ev);
            cars_[i].prev_v = cars[i].course_v;
        }
    // The clock's step (virtual 15, after the crossings: a rolling start's first in the step the line is taken).
    if (clock_running_) clock_ = std::min<s64>(clock_ + kClockPerStep, 0x405F7DFD);
    if (was_started) ++started_steps_;
    else if (ev.start) started_steps_ = 0;
    if (player_finish_steps_ >= 0 && !ev.player_finished) ++player_finish_steps_;
    sort_order(cars);
    return ev;
}

void RaceFlow::finish_now(size_t car, float share) {
    if (car >= cars_.size() || cars_[car].finished) return;
    CarRecord& r = cars_[car];
    r.finished = true;
    r.finish_order = finishers_++;
    r.finish_units = std::max<s64>(0, clock_ - kClockPerStep + s64(gt3r::ee::to_int((gt3r::ee::F(std::clamp(share, 0.0f, 1.0f)) * gt3r::ee::F::raw(0x42480000)).bits)));
    if (car == setup_.player) player_finish_steps_ = 0;
}

int RaceFlow::place(size_t car) const {
    for (size_t i = 0; i < order_.size(); ++i)
        if (order_[i] == car) return int(i) + 1;
    return 0;
}

int RaceFlow::lap_shown(size_t car) const {
    const CarRecord& r = cars_[car];
    const int lap = std::max(1, r.laps_done + 1);
    return setup_.laps > 0 ? std::min(lap, setup_.laps) : lap;
}

s64 RaceFlow::lap_ms(size_t car) const {
    const CarRecord& r = cars_[car];
    if (r.finished) return r.last_lap_ms >= 0 ? r.last_lap_ms : clock_ms(r.finish_units) - r.base_ms;
    if (hold_ > 0 || !clock_running_ || (setup_.player_clock && r.counter == 0)) return 0;
    if (!practice_.empty() && r.counter == 0) return 0; // (a free practice's, vf34 → 0x357830: none before its start is counted)
    // (0x357830: a license's run that starts moving shows none before its clock starts; a mission's shows the race's clock, 0x3F6ED0)
    if (setup_.start == StartType::Moving && !setup_.mission && (r.counter == 0 || r.mark == 1)) return 0;
    if (setup_.start != StartType::Rolling && setup_.start != StartType::Moving && started_steps_ < 0) return 0;
    return clock_ms(clock_) - r.base_ms;
}

s64 RaceFlow::run_ms(size_t car) const {
    const CarRecord& r = cars_[car];
    return clock_ms(r.finish_units) - (setup_.player_clock ? r.start_ms : setup_.test_kind != 0 ? r.base_ms : 0);
}

bool RaceFlow::everyone_finished() const {
    return std::all_of(cars_.begin(), cars_.end(), [](const CarRecord& r) { return r.finished; });
}

std::vector<RaceFlow::ResultRow> RaceFlow::results() const {
    std::vector<ResultRow> rows;
    int winner_laps = 0;
    for (const CarRecord& r : cars_) winner_laps = std::max(winner_laps, r.laps_done);
    for (size_t i : order_) {
        const CarRecord& r = cars_[i];
        ResultRow row;
        row.car = i;
        row.finished = r.finished;
        row.total_ms = r.finished ? run_ms(i) : -1;
        row.laps_behind = r.finished ? 0 : std::max(0, winner_laps - r.laps_done);
        row.best_lap_ms = r.best_lap_ms;
        rows.push_back(row);
    }
    return rows;
}

std::string result_time(s64 ms, bool plus) {
    ms = std::max<s64>(0, ms);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%c%d'%02d.%03d", plus ? '+' : ' ', int(ms / 60000), int(ms / 1000 % 60), int(ms % 1000));
    return buf;
}

std::vector<int> qualify_places(std::span<const s64> best_ms) {
    const size_t n = best_ms.size();
    auto t = [&](size_t i) { return best_ms[i] < 0 ? u64(0x157529FF) : u64(best_ms[i]); };
    std::vector<int> rank(n, 0), place(n, 0);
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            if (t(i) != t(j)) ++rank[t(j) < t(i) ? i : j];
    std::vector<bool> taken(n, false);
    for (size_t i = 0; i < n; ++i) {
        size_t p = size_t(rank[i]);
        while (p < n && taken[p]) ++p;
        if (p >= n) p = 0;
        while (taken[p]) ++p;
        taken[p] = true, place[i] = int(p);
    }
    return place;
}

s64 row_time_limit_ms(std::span<const u8> row) {
    if (row.size() < 0x1A) return 0;
    return s64(u32(row[0x18]) | u32(row[0x19]) << 8) * 60000;
}

void ResultBoard::begin(size_t cars, size_t player, std::string lap_word, std::string laps_word, bool time_limit) {
    time_limit_ = time_limit;
    rows_.assign(cars, Row{});
    recorded_.assign(cars, false);
    player_ = player;
    player_row_ = 0;
    all_fixed_ = false;
    leader_ms_ = 0, leader_laps_ = 0;
    lap_word_ = std::move(lap_word), laps_word_ = std::move(laps_word);
}

void ResultBoard::set_player_row(int row) {
    // Both the old and the new row are marked (the place's colour moves).
    if (player_row_ != row && size_t(player_row_) < rows_.size()) rows_[size_t(player_row_)].changed = true;
    player_row_ = row;
    if (size_t(row) < rows_.size()) rows_[size_t(row)].changed = true;
}

void ResultBoard::put(size_t row, int car) {
    rows_[row].car = car;
    rows_[row].changed = true;
    if (size_t(car) == player_) set_player_row(int(row));
}

void ResultBoard::finished(const RaceFlow& flow, std::span<const size_t> cars) {
    if (all_fixed_) return;
    const std::vector<size_t>& order = flow.order();
    for (size_t car : cars) {
        if (car >= recorded_.size() || recorded_[car]) continue;
        size_t row = 0;
        while (row < order.size() && order[row] != car) ++row;
        if (row >= rows_.size()) continue;
        recorded_[car] = true;
        put(row, int(car));
        Row& r = rows_[row];
        r.fixed = true;
        // The time (ResultArcade::vf9, kind 0): the winner's whole time; the others' gap to it, or
        // the laps short of the winner's. A race with a time limit (kind 1, 0x3E00B0): the winner's laps (" 123Laps"), the
        // others on its laps their gap to it, the rest their own laps.
        const RaceFlow::CarRecord& c = flow.car(car);
        const s64 ms = flow.run_ms(car);
        if (row == 0) leader_ms_ = ms, leader_laps_ = c.laps_done;
        auto laps = [&](int n, char sign) { return std::string(1, sign) + std::to_string(n) + (n < 2 ? lap_word_ : laps_word_); };
        if (time_limit_) {
            r.time = row != 0 && c.laps_done == leader_laps_ ? result_time(ms - leader_ms_, true) : laps(c.laps_done, ' ');
        } else if (c.laps_done == leader_laps_) {
            r.time = result_time(row == 0 ? ms : ms - leader_ms_, row != 0);
        } else {
            r.time = laps(leader_laps_ - c.laps_done, '+');
        }
    }
    all_fixed_ = std::all_of(recorded_.begin(), recorded_.end(), [](bool b) { return b; });
}

void ResultBoard::follow(const RaceFlow& flow) {
    if (all_fixed_) return;
    const std::vector<size_t>& order = flow.order();
    for (size_t row = 0; row < order.size() && row < rows_.size(); ++row) {
        const size_t car = order[row];
        if (recorded_[car] || rows_[row].car == int(car)) continue;
        put(row, int(car));
    }
}

void ResultBoard::touch() {
    for (Row& r : rows_) r.changed = true;
}

bool ResultBoard::take_changed(size_t row) {
    if (row >= rows_.size()) return false;
    const bool c = rows_[row].changed;
    rows_[row].changed = false;
    return c;
}

} // namespace gt4r::race
