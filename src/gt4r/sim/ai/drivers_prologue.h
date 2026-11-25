#pragma once

// Gran Turismo 4 Prologue's computer drivers (docs/formats/prologue.md "The computer drivers"): its program's driver object
// is Gran Turismo 3's record (the shared engine's exact::Driver, car + 0x1C; D below) on Gran Turismo 3's course object (the
// lines of the course file's slot 31, exact::AiCourse). Its routines are the engine's where they are Gran Turismo 3's in its
// later builds' constants (the line queries, the corner speeds, the segments, the target, the outputs' slew), its own below.
// Addresses are SCPS-15055 v1.01's. The race's drivers are sim::ai::Field, which runs these on a Prologue session's cars.

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
// state cleared.
void driver_start(ex::Car& c, const ex::AiCourse& k, const DriverTables& t, int line);

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
