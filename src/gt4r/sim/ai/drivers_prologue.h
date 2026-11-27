#pragma once

// Gran Turismo 4 Prologue's computer drivers (docs/formats/prologue.md "The computer drivers"): its program's driver object
// is Gran Turismo 3's record in layout (the shared engine's exact::Driver as the data, car + 0x1C; D below; its parameters the
// conductor's +0x10808 + 0x74 k) on a course object of Gran Turismo 3's layout (the lines of the course file's slot 31,
// exact::AiCourse as the data). Every routine is Prologue's own, ported from its program and checked against it at every call
// (gt4r_diff prologue-ai, the test disc_prologue_drivers_equal). Addresses are SCPS-15055 v1.01's. The race's drivers are
// sim::ai::Field, which runs these on a Prologue session's cars.

#include "gt4r/formats/course/ai_line.h"
#include "gt4r/sim/ai/tables.h"
#include "gt4r/sim/rules_prologue.h"
#include "sim/exact/ai_course.h"
#include "sim/exact/course_query.h"

#include <array>

namespace gt4r::sim {
class Program;
}

namespace gt4r::sim::ai::prologue {

namespace ex = gt3r::sim::exact;
using gt3r::ee::F;

// The program's tables its drivers read.
struct DriverTables {
    bool found = false;
    std::array<std::array<s8, 6>, 8> fallback{};          // 0x5066F0: per requested line the lines tried, -1 ending a row
    std::array<u8, 8> order{};                            // 0x5066E8: the order the lines are prepared in
    std::array<s32, 3> nearest{};                         // 0x506510: the lines a car placed off the grid may start on
    std::array<s32, 6> grid_line{};                       // 0x504518: the line a computer's car starts on by its grid place % 6
    std::array<std::array<u8, 0x20>, 3> builtin{};        // 0x5065B0: the driver sets of a course without its own, by its kind
    u8 steer_mode = 0;                                    // 0x5066BA: 0 the yaw gain as it is, 1 by the slip angle's share (2: PD)
    u8 own_saturation = 0, bonus_angle = 0, bonus_speed = 0; // the static block's +0xDB, +0xDC (degrees), +0xDD (km/h)
};
DriverTables driver_tables(const Program& program);

// 0x4292C8: the course's lines as the race prepares them: each point's road segment, its distance along the course, the road's
// height, slope, bank and surface under it.
ex::AiCourse driver_course(const ex::CourseQuery& q, const course::AiData& data, const DriverTables& t);

// 0x427450: a driver's parameters from its set; 0x427AA0: the corner speeds' downforce term from the parameter block.
void driver_set(ex::Driver& d, const u8* set);
void driver_downforce(ex::Driver& d, const ex::Params& p);
// 0x427840: the event's percents of the throttle, the braking and the arcs' speeds (0: 100; the throttle at most 100) and, for
// a computer's car (control source 2), the drivers' differences by the grid index.
void driver_event(ex::Driver& d, int throttle, int brake, int corner, int control, int index);
// 0x424AB8: the plan at a car's set-up on line `line` (-1: the nearest of the three, 0x4249A8; the fallback applies), the
// state cleared (`in`: the course, the car's wheel layout, the wear's switch).
struct Step;
void driver_start(ex::Car& c, const Step& in, const DriverTables& t, int line);

// ---- the course object's lines (drivers_prologue_plan.cpp) ----
// 0x42A330: the first line of the request's row of the fallback table (a request outside 0 .. 7 asks for 6) that the course
// has, -1 none.
int resolve(const ex::AiCourse& k, int line);
// 0x429B90: the span of line `line` holding race distance s: 1 before its first point, 2 at or past its last (both from = the
// last point, to = 0), 3 the span from = j - 1 .. to = j found trying j = hint, hint + 1, hint - 1, hint + 2, ..., 0 none.
int span(const ex::AiCourse& k, int line, int hint, F s, int& to, int& from);
// 0x429CA0: the line's place (x, y) and height h at race distance s (wrapped once into the lap), searched from point `hint`:
// straight spans interpolated, arcs turned about their center. The span's first point, -1 none (the outputs untouched).
int evaluate(const ex::AiCourse& k, int line, int hint, F s, F& x, F& y, F& h);
// 0x42A090: the stop line's (line 4) place: s in the lap, a lap added in its first half, the first span holding it
// interpolated. The span's first point, -1 outside the line (the outputs untouched).
int evaluate_stop(const ex::AiCourse& k, F s, F& x, F& y, F& h);
// 0x42A2B0: whether the side map (line 5) changes left and right over at race distance s (hint: the driver's next point).
bool side_swapped(const ex::AiCourse& k, int hint, F s);
// 0x425968: the line's heading at race distance s, to the first place at least 0.2 m on in 0.5 m steps (at most 10); (x, y,
// h) the place at s.
F line_heading(const ex::AiCourse& k, int line, int hint, F s, F& x, F& y, F& h);

// What a driver's step reads outside its car.
struct Step {
    const ex::AiCourse* course = nullptr;
    const DriverTables* tables = nullptr;
    const sim::prologue::CarPart* part = nullptr; // the wheel layout, the mean slip angle
    ex::WearParams wear{};                        // (max not 0: the race wears its tires)
    s32 start_count = 0;                          // the race information's +0x74: the start hold's steps left
    u32 clock = 0;                                // the conductor's +0x107E0 (50 a step; a third of it in milliseconds)
    bool hit_front = false, hit_rear = false;     // the body's +0x434 / +0x438
    bool dirt = false;                            // the course's kind 2 (0x3B63E0)
    u8 start_speed = 0;                           // the race's rolling start's speed (km/h; the conductor's virtual 33)
};

// ---- the plan (drivers_prologue_plan.cpp) ----
// 0x425D70: the step's outputs cleared (the steering to the car's command), the line blend's weight down by the step.
void begin(ex::Car& c);
// 0x425B28: the steering output toward `target` by at most the steering rate times the step.
void steer(ex::Car& c, F target);
// 0x425D20: the look ahead at forward speed v.
F lookahead(const ex::Driver& d, F v);
// 0x4257F8: the aim on line `line` at the car's race distance + look (the stop line by its own), the look 1 m longer while
// the aim is nearer than 5 m (at most 9 times); (dx, dy) the aim less the car's place. Returns the look used.
F target(const ex::Car& c, const ex::AiCourse& k, int line, int hint, F look, F& dx, F& dy);
// 0x4256A0: base + gain (v - vt), the difference held to 10 km/h each way, the result to [0, 1].
F throttle_for(F v, F vt, F base, F gain);
// 0x3C6EA8: wheel w's tire record on a surface by the wheel layout's axle.
const ex::TireRecord& tire(const ex::Params& p, const rules::WheelLayout& l, int w, s8 surface);
// 0x427AE8: the speed of the arc from point i of the driver's line.
F corner_speed(const ex::Car& c, const Step& in, int i);
// 0x427E00: the next point changed: the next segment's kind and speed become the current ones, the new next one's planned.
void next_segment(ex::Car& c, const Step& in);
// 0x427ED0: the next point from the race distance, the segment before it planned, then next_segment.
void replan(ex::Car& c, const Step& in);
// 0x424C20: the line at once (the blend's old line too, weight 0), then replan. 0x424C48: a line request (the fallback
// applies), blended (the old line the current, weight 1) or at once; false when the course has none of its row.
void set_line(ex::Car& c, const Step& in, int line);
bool request_line(ex::Car& c, const Step& in, int line, bool at_once);
// 0x424CC8: the engine held through the start's hold (n its steps left): from 13 on a blip between 3/4 and 15/16 of the up-shift
// speed at rates by the rank, then the throttle by the rpm; no clutch.
void hold(ex::Car& c, s32 n);

// 0x424E80: the point bookkeeping, the braking point, the status; the throttle's factor.
F braking(ex::Car& c, const Step& in);
// 0x425718: throttle and brake towards a speed.
void speed_control(ex::Car& c, F vt, F gain);
// 0x425F20: the decision.
void decide(ex::Car& c, const Step& in);
// 0x428610: lane changes, the lift behind a car, the return to the main line.
void traffic(ex::Car& c, const Step& in);
// 0x425BB0 / 0x425CD8: braking to a stand (c.driver_kind 8 once stopped); parked.
void stop(ex::Car& c, F steer);
void park(ex::Car& c);
// 0x3E8C98: a driven car's step of phase 5: the clutch's request, the engine held through a hold, parked, or the decision.
void step(ex::Car& c, const Step& in);
// 0x42A448 / 0x42A4A0: the line to the left / right of `line` at the car's place (swapped where the side map says so).
int lane_left(const ex::AiCourse& k, int line, int hint, F s);
int lane_right(const ex::AiCourse& k, int line, int hint, F s);

} // namespace gt4r::sim::ai::prologue
