#include "gt4r/race/race_start.h"

#include "gt4r/race/race_flow.h"

#include <cmath>

namespace gt4r::race {

using gt3r::ee::F;

PlannedStart plan_start(const StartRace& r, size_t car, int grid_place, std::optional<CarStart> rolling) {
    PlannedStart p;
    const float lap = r.road ? r.road->length : 0;
    if (rolling) p.start = *rolling;
    else if (r.conductor != Conductor::Other) p.start = car_start(r.row, r.conductor, car, section_start_metres(r.section_start, lap));
    if (p.start.on_line()) {
        // 0x363B10: the distance into the lap (0x34C230 truncated), then 0x34C5E0: no more than the lap less 20 m before the
        // line; on a course without a lap no further than 20 m short of the last split point.
        int d = p.start.distance;
        const int whole = int(lap);
        if (whole > 0) {
            while (d >= whole && d > 0) d -= whole;
            while (d <= -whole && d < 0) d += whole;
        }
        F s = gt3r::ee::int_to_f(d);
        const F twenty = F::raw(0x41A00000), least = -(F(lap) - twenty);
        if (s < least) s = least;
        const bool one_way = r.way == 1;
        if (one_way && !r.sectors.empty())
            if (const F most = F(r.sectors.back()) - twenty; most < s) s = most;
        sim::ai::CourseLines::StartPlace at;
        if (r.lines && r.lines->start_place(sim::ai::CourseLines::kMain, s, one_way, at)) {
            sim::Entry::LineStart l;
            l.kind = p.start.kind, l.x = at.x, l.y = at.y, l.z = at.h, l.heading = at.heading, l.hint = at.hint;
            l.speed = p.start.speed, l.mark = p.start.mark;
            // (the driver's line at the set-up: the main line for kinds 2 .. 5, the passing line 7 for a pad car of kind 1)
            const sim::ai::CourseLines* lines = r.lines;
            const int line = p.start.kind == 1 ? 7 : sim::ai::CourseLines::kMain;
            l.open_throttle = [lines, line](F at_distance) { return lines->mode_at(line, at_distance) == 0; };
            p.line = std::move(l);
            return p;
        }
        p.start.kind = 0; // (no line: the grid)
    }
    if (r.section && !r.section->grid.empty()) p.place = r.section->grid[size_t(std::max(0, grid_place)) % r.section->grid.size()];
    return p;
}

int planned_crossings(const StartRace& r, const PlannedStart& p, float v) {
    const float lap = r.road ? r.road->length : 0;
    if (p.start.on_line()) {
        if (p.start.distance < 0) return 0;
        const int whole = int(lap);
        int d = p.start.distance, n = 1;
        if (d > 0) {
            while (whole > 0 && d >= whole) d -= whole, ++n;
            return n;
        }
    }
    return start_crossings(v, lap, r.sectors, r.goal);
}

} // namespace gt4r::race
