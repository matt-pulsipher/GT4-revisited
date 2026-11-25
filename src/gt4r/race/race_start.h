#pragma once

// Where a race's cars start (the program's set-up 0x34D490 / 0x363B10 up to the place): on the grid (the road's, or a road
// section's own), or on a driving line before or past the start line (a rolling start, a license test that starts moving).
// docs/formats/race_flow.md "The start".

#include "gt4r/race/sections.h"
#include "gt4r/race/start_rule.h"
#include "gt4r/sim/ai/course_lines.h"
#include "gt4r/sim/session.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gt4r::race {

// What the starts read of the race: its conductor, the event's row, the road and its driving lines.
struct StartRace {
    Conductor conductor = Conductor::Other;
    RowStart row;                             // (the license's, training's and mission's)
    const course::Runway* road = nullptr;
    const RoadSection* section = nullptr;     // the race's section (CFG +0x21); none: the whole road
    float section_start = 0;                  // the section's start, else the road's own (road +0x18): metres, 0 none
    const sim::ai::CourseLines* lines = nullptr; // null: a car with a place on a line starts on the grid (0x34C5E0 answering 0)
    u8 way = 0;                               // COURSE +0x12 (K +0xCBDE): 1 a course without a lap
    std::vector<float> sectors;               // the split points (the sectors' ends but the last)
    float goal = -1;                          // the race's goal along the course (-1 none)
};

// The reading of a course's files for its starts: the sections, the split points, the goal, and (asked for) the driving lines.
class StartCourse {
public:
    // `runway`: the course's road file (RNW4; the sections and split points); `section` -1: the whole road.
    StartCourse(const course::Runway& road, ByteSpan runway, int way);
    // The race of a conductor on it, with an event's row. The result points into this object.
    const StartRace& set_race(Conductor conductor, std::span<const u8> event_row, int section);
    // Whether a car of index below `cars` may start on a driving line by the row.
    bool may_need_lines(size_t cars) const;
    // The driving lines (once): from the course's driver file.
    void add_lines(const course::AiData& data, const sim::ai::Tables& tables);
    bool has_lines() const { return lines_ != nullptr; }
    const StartRace& race() const { return race_; }

private:
    const course::Runway* road_;
    std::vector<RoadSection> sections_;
    std::vector<float> splits_;
    std::unique_ptr<sim::RoadGround> ground_;
    std::unique_ptr<sim::ai::CourseLines> lines_;
    StartRace race_;
};

// A car's start.
struct PlannedStart {
    CarStart start;                                    // (kind 0 where no line holds its place)
    std::optional<sim::Entry::LineStart> line;         // on a driving line
    std::optional<course::Runway::Place> place;        // a section's own grid place
};

// A car of index `car` at grid place `grid_place`: by the conductor's rule, or (`rolling`, metres along from the line and
// km/h) a single race's rolling start.
PlannedStart plan_start(const StartRace& race, size_t car, int grid_place, std::optional<CarStart> rolling = {});

// The car's crossings of the line at its start (F + 0x4AC, 0x363B10): on a driving line 0 before the line and one more than
// the whole laps past it; on the grid by its sector (start_crossings). `v`: the car's distance along the course.
int planned_crossings(const StartRace& race, const PlannedStart& p, float v);

} // namespace gt4r::race
