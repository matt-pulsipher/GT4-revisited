#include "gt4r/race/race_start.h"

#include "gt4r/race/race_flow.h"

#include "base/ee_math.h"

#include <algorithm>
#include <cmath>

namespace gt4r::race {

using gt3r::ee::F;

StartCourse::StartCourse(const course::Runway& road, ByteSpan runway, int way) : road_(&road), sections_(road_sections(runway)), splits_(road_list(runway, 0)) {
    race_.road = road_;
    race_.way = u8(way);
}

const StartRace& StartCourse::set_race(Conductor conductor, std::span<const u8> event_row, int section) {
    StartRace& r = race_;
    r.conductor = road_->gtrw ? Conductor::Other : conductor; // (Prologue's own start is not read)
    r.row = RowStart::from_row(event_row);
    r.section = r.conductor != Conductor::Other && section >= 0 && size_t(section) < sections_.size() ? &sections_[size_t(section)] : nullptr;
    r.section_start = 0, r.goal = -1;
    r.sectors.clear();
    if (road_->gtrw) return r;
    r.section_start = r.section ? r.section->start : road_->start_v;
    const float goal = r.section ? r.section->goal : road_->goal_v;
    if (goal >= 1 && goal <= road_->length - 1) r.goal = goal;
    if (!r.section) r.sectors = splits_;
    return r;
}

bool StartCourse::may_need_lines(size_t cars) const {
    if (race_.conductor == Conductor::Other) return false;
    for (size_t i = 0; i < cars; ++i)
        if (const CarStart s = car_start(race_.row, race_.conductor, i, 0); s.on_line() || s.speed > 0) return true;
    return false;
}

void StartCourse::add_lines(const course::AiData& data, const sim::ai::Tables& tables) {
    if (lines_) return;
    ground_ = std::make_unique<sim::RoadGround>(*road_);
    lines_ = std::make_unique<sim::ai::CourseLines>(data, *ground_, tables);
    race_.lines = lines_.get();
}

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

int first_line(const StartRace& r, const PlannedStart& p, bool pad, int grid_place, const sim::Program& program) {
    if (p.start.kind >= 2) return sim::ai::CourseLines::kMain;
    if (pad) return 7;
    if (r.way == 1 || !r.road || r.road->grid.size() < 2 || !program.known()) return sim::ai::CourseLines::kMain;
    constexpr u32 kGridLines = 0x620AD8;
    int line = int(program.w(kGridLines + 4 * u32(std::max(0, grid_place) % 6)));
    const course::Runway::Place &a = r.road->grid[0], &b = r.road->grid[1];
    const F h = F(a.heading) + F::raw(0x40490FDA);
    const F side = (F(a.pos.x) - F(b.pos.x)) * gt3r::ee::cos(h) - (-gt3r::ee::sin(h)) * (-(F(a.pos.z) - F(b.pos.z)));
    if (!(side < F()) && (line == 1 || line == 2)) line = 3 - line;
    return line;
}

void put_start(sim::Entry& entry, const StartRace& race, const PlannedStart& p, bool pad, int grid_place, const sim::Program& program) {
    entry.line_start = p.line, entry.place = p.place;
    entry.grid_kind = p.line || p.start.on_line() ? 0 : p.start.kind;
    if (p.line || p.start.kind != 0 || p.start.speed <= 0) return;
    entry.start_speed = float(p.start.speed) / 3.6f;
    if (!race.lines) return;
    const sim::ai::CourseLines* lines = race.lines;
    const int line = first_line(race, p, pad, grid_place, program);
    entry.open_throttle = [lines, line](F at) { return lines->mode_at(line, at) == 0; };
}

int planned_crossings(const StartRace& r, const PlannedStart& p, float v) {
    const float lap = r.road ? r.road->length : 0;
    if (p.start.kind == 6) return 0;
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

int planned_back(const StartRace& r, const PlannedStart& p, float v) {
    const F lap(r.road ? r.road->length : 0);
    return p.start.kind == 6 && lap * F::raw(0x3F000000) < F(v) ? 1 : 0;
}

} // namespace gt4r::race
