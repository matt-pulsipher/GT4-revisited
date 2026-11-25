#include "gt4r/race/race_flow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

void RaceFlow::begin(const FlowSetup& setup, std::span<const CarState> cars) {
    setup_ = setup;
    cars_.assign(cars.size(), CarRecord{});
    for (size_t i = 0; i < cars.size(); ++i) {
        cars_[i].prev_v = cars[i].course_v, cars_[i].has_v = true;
        cars_[i].prev_lap = cars[i].lap;
    }
    hold_total_ = hold_ = start_hold_steps(setup.start, setup.start_seconds);
    clock_ = 0;
    finishers_ = 0;
    player_finish_steps_ = -1;
    started_steps_ = setup.start == StartType::Rolling ? -1 : hold_ > 0 ? -1 : 0;
    // A rolling start's clock stands until the first car takes the line (K +0xCBE4, cleared by vf20 0x347928).
    clock_running_ = setup.start != StartType::Rolling;
    order_.resize(cars.size());
    sort_order(cars);
}

float RaceFlow::progress(size_t i, const CarState& c) const {
    // Behind the line before the first crossing.
    const float v = cars_[i].counter == 0 && c.course_v > setup_.lap_length * 0.5f ? c.course_v - setup_.lap_length : c.course_v;
    return float(cars_[i].counter) * setup_.lap_length + v;
}

void RaceFlow::sort_order(std::span<const CarState> cars) {
    for (size_t i = 0; i < order_.size(); ++i) order_[i] = i;
    std::stable_sort(order_.begin(), order_.end(), [&](size_t a, size_t b) {
        const CarRecord &ra = cars_[a], &rb = cars_[b];
        // More crossings of the line first; on equal crossings a car still running ahead of a
        // finished one, two finished ones by their finish, two running ones by their distance.
        if (ra.counter != rb.counter) return ra.counter > rb.counter;
        if (ra.finished != rb.finished) return !ra.finished;
        if (ra.finished) return ra.finish_order < rb.finish_order;
        return progress(a, cars[a]) > progress(b, cars[b]);
    });
}

RaceFlow::Events RaceFlow::step(std::span<const CarState> cars) {
    Events ev;
    if (cars.size() != cars_.size()) return ev;
    if (hold_ > 0) {
        // The hold: the clock stands; a beep at each of the last three whole seconds.
        --hold_;
        if (hold_ > 0 && hold_ < 240 && hold_ % 60 == 0) ev.beep = true;
        if (hold_ == 0) ev.start = true, started_steps_ = 0;
        for (size_t i = 0; i < cars.size(); ++i) cars_[i].prev_v = cars[i].course_v, cars_[i].prev_lap = cars[i].lap;
        return ev;
    }
    if (started_steps_ >= 0) ++started_steps_;
    const s64 clock_before = clock_;
    if (clock_running_) clock_ += kClockPerStep;
    const float length = setup_.lap_length;
    for (size_t i = 0; i < cars.size(); ++i) {
        CarRecord& r = cars_[i];
        const CarState& c = cars[i];
        const int crossings = c.lap - r.prev_lap; // the simulation's count of the line's crossings
        if (crossings != 0 && !r.finished) {
            if (crossings > 0) {
                // The moment the line was passed: the step's share before it.
                float f = 1;
                if (length > 0) {
                    const float before = length - r.prev_v, run = c.course_v + length - r.prev_v;
                    f = run > 1e-4f ? std::clamp(before / run, 0.0f, 1.0f) : 1.0f;
                }
                const s64 at = clock_running_ ? clock_before + s64(std::trunc(f * float(kClockPerStep))) : clock_;
                const int old = r.counter;
                r.counter += 1;
                if (old == 0) {
                    // The start line (vf20 0x347928): the first car over it starts a rolling start's clock (from
                    // the next step: ❓ the conductor's clock step 0x357E80 and the crossings' order in a step not
                    // read); the player's crossing is his start signal (event 4 for car 0).
                    if (!clock_running_) clock_running_ = true, ev.clock_started = true;
                    if (setup_.start == StartType::Rolling && i == setup_.player && started_steps_ < 0) ev.start = true, started_steps_ = 0;
                } else {
                    r.laps_done += 1;
                    const s64 lap = clock_ms(at - r.lap_start_units);
                    r.last_lap_ms = lap;
                    if (r.best_lap_ms < 0 || lap < r.best_lap_ms) r.best_lap_ms = lap;
                    ev.lap_done = int(i);
                    // The finish: the leader has finished, or this car has driven the race's laps.
                    const size_t leader = order_.empty() ? i : order_[0];
                    if (cars_[leader].finished || (setup_.laps > 0 && r.laps_done >= setup_.laps)) {
                        r.finished = true;
                        r.finish_order = finishers_++;
                        r.finish_units = at;
                        ev.finished.push_back(i);
                        if (i == setup_.player) ev.player_finished = true, player_finish_steps_ = 0;
                    }
                    r.lap_start_units = at;
                }
            } else if (r.counter > 0) {
                // Backwards over the line: the lap is taken back.
                r.counter -= 1;
                r.laps_done = std::max(0, r.counter - 1);
            }
        }
        r.prev_v = c.course_v, r.prev_lap = c.lap;
    }
    if (player_finish_steps_ >= 0 && !ev.player_finished) ++player_finish_steps_;
    sort_order(cars);
    return ev;
}

void RaceFlow::finish_now(size_t car, float share) {
    if (car >= cars_.size() || cars_[car].finished) return;
    CarRecord& r = cars_[car];
    r.finished = true;
    r.finish_order = finishers_++;
    r.finish_units = std::max<s64>(0, clock_ - kClockPerStep + s64(std::trunc(std::clamp(share, 0.0f, 1.0f) * float(kClockPerStep))));
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
    if (r.finished) return r.last_lap_ms;
    if (holding() || !clock_running_ || (setup_.start != StartType::Rolling && started_steps_ < 0)) return 0;
    return clock_ms(clock_ - r.lap_start_units);
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
        row.total_ms = r.finished ? clock_ms(r.finish_units) : -1;
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

void ResultBoard::begin(size_t cars, size_t player, std::string lap_word, std::string laps_word) {
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
        // the laps short of the winner's.
        const RaceFlow::CarRecord& c = flow.car(car);
        const s64 ms = clock_ms(c.finish_units);
        if (row == 0) leader_ms_ = ms, leader_laps_ = c.laps_done;
        if (c.laps_done == leader_laps_) {
            r.time = result_time(row == 0 ? ms : ms - leader_ms_, row != 0);
        } else {
            const int behind = leader_laps_ - c.laps_done;
            r.time = "+" + std::to_string(behind) + (behind < 2 ? lap_word_ : laps_word_);
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

bool ResultBoard::take_changed(size_t row) {
    if (row >= rows_.size()) return false;
    const bool c = rows_[row].changed;
    rows_[row].changed = false;
    return c;
}

} // namespace gt4r::race
