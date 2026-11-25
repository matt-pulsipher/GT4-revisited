#pragma once

// Arcade's 2 Player Battle (RaceSplitBattle, DynamicsConductorBattle2P; docs/formats/race_flow.md "2 Player Battle"): the rules
// the options' page gives it, where its cars start, how the screen is split for it, and who is told which gap. The screen that
// runs it is the app's (screen_split.cpp).

#include "gt4r/base/types.h"
#include "gt4r/race/start_rule.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace gt4r::race {

// The race rules' block (0x3BFB10) as the battle reads it: the options' split_race_laps, split_tire_damage (0 none, 1, 2),
// split_handicap (metres) and split_boost_level (0 none, 1 mild, 2 strong); a new game's 2, 0, 0, 1 (0x4363A0).
struct SplitRules {
    int laps = 2, tire_damage = 0, handicap = 0, boost = 1;
    static SplitRules from_options(const std::map<std::string, std::variant<s32, float, std::string>>& options);
};

// The rubber band by the boost, in the race's row of its event (the conductor puts it into the configuration CFG the row is
// copied into, 0x35E108; virtual 12: 0x354EB8; CFG +0x22 and +0x28 .. +0x2A from the row's +0x64 and +0x68 .. +0x6A): the car
// behind hurried by up to 7 % (mild, from 20 m) or 20 % (strong, from 10 m), in full at 200 m. (The tire damage:
// start_rule.h's put_tire_damage.)
void put_boost(std::vector<u8>& row, int level);

// How a car starts with a handicap (virtual 30, 0x347108): none on the grid; else both stand on the driving line: the first
// player 10 m before the line and the second the handicap (at least 20 m) before it, or with a negative one the other way round.
std::optional<CarStart> handicap_start(int handicap, size_t car);

// The kind of tire the battle fits by the course's kind (0x330650): 11 on kind 2, 12 on kind 3, else the one chosen (-1: the
// car's own).
int split_tire(int course_kind, int chosen);

// How the screen is split (the race's draw, 0x32FCC8): one view above the other, or with the options' wide screen side by side.
struct SplitView {
    float lens = 1;       // the lens' factor: what a half shows up its height is the race view's over it
    // The rear-view mirror in the half's picture (640 x 224, or side by side 320 x 448), and its lens against the race view's.
    std::array<float, 4> mirror{};
    float mirror_zoom = 1;
    std::array<float, 2> picture{}; // the half's picture
};
SplitView split_view(bool side_by_side, int half);

// Who is told which gap (0x3578B0 and 0x359238): each point of the lap (the road's split points and the line) keeps the cars'
// arrivals on the highest lap any has reached it on. The step's new arrivals from the second on are told their time after the
// first to arrive, unless their place in the race is already better than the arrival's; and the first is told the time of the
// second arrival, as a lead, when that car is the race's second. A car that arrives a lap or more down is told by how many laps.
class ArrivalBoard {
public:
    void reset(size_t points) { points_.assign(points, {}); }
    void arrive(size_t car, int lap, size_t point, s64 time_ms);
    struct Gap {
        size_t car = 0;  // whose display shows it
        s64 ms = 0;      // behind (positive) or ahead of the other
    };
    struct LapsDown {
        size_t car = 0;
        int laps = 0;
    };
    struct Told {
        std::vector<Gap> gaps;
        std::vector<LapsDown> laps_down;
    };
    // Once a step, after its arrivals: what is told. `place`: a car's place in the race, 1 the leader.
    template <class Place> Told tell(Place&& place) {
        Told t;
        t.laps_down = std::move(laps_down_), laps_down_.clear();
        for (Point& p : points_) {
            for (size_t k = p.told; k < p.arrivals.size(); ++k) {
                if (k == 0) continue;
                const Arrival& a = p.arrivals[k];
                if (int(k) + 1 >= place(a.car)) t.gaps.push_back({a.car, a.ms - p.arrivals[0].ms});
                if (k == 1 && place(a.car) == 2) t.gaps.push_back({p.arrivals[0].car, p.arrivals[0].ms - a.ms});
            }
            p.told = p.arrivals.size();
        }
        return t;
    }

private:
    struct Arrival {
        size_t car = 0;
        s64 ms = 0;
    };
    struct Point {
        int lap = -1;
        std::vector<Arrival> arrivals; // in order of time
        size_t told = 0;
    };
    std::vector<Point> points_;
    std::vector<LapsDown> laps_down_;
};

} // namespace gt4r::race
