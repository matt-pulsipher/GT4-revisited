#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): the last step's values (phase 1, 0x343ED0), the
// phases the game added (2: 0x355E10, 3: 0x35CA60, 6: 0x35D1D0), the preparation (phase 4, 0x357700: the pit
// stop's counter 0x35C0B8, the message timer 0x35ACB0, the step length 0x3658A0, the masses with the fuel 0x354AC0,
// then the aerodynamics and the tire part of rules_step.h) and the hit records (phase 12: 0x350308; the collision
// step's: 0x350530, 0x350328, the conductor's judge of a hit, virtual +0x190).
//
// Phases 3 and 6 and the hit's moods run only for a car whose category (P + 0x32, the spec's +0x01: the car's
// GENERIC_CAR +0x13) is 1, a car with moods; no car of the disc has that category (2: the MTR GMV, 3: the Nike One,
// 4: the two motor carriages of 1886, 5: the Honda Insight, 0 every other), so they run for none. Such a car's record
// keeps its state in the eight bytes at car + 0x788 (PhaseExtra::special): [0] the steps it has stood still with the
// handbrake pulled (its "sit", up to 34), [1] its mood (low four bits) and the mood before (high four), [2] the steps the
// mood has left (0xFF: for good, 0: none), [3] the steps of the lamps' fade, [4] the nod's phase (0 .. 47), [5] the lean
// (-30 .. 30), [6] the mood set this step (0: none). A car of driveline layout 7 keeps [5] cleared.

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"

#include <functional>

namespace gt4r::sim {
struct Extra;
}

namespace gt4r::sim::rules {

struct Tables;
struct WearRules;

using gt3r::s16;
using gt3r::u16;

// What these routines keep that the engine's record has no member for.
struct PhaseExtra {
    std::array<u8, 8> special{};  // car + 0x788 .. 0x78F: a car with moods' state (above)
    u8 finished = 0;              // F + 0x4B2: the car's race has ended
    u8 failed = 0;                // F + 0x4B3: bit 0, a license test's hit rule broken (0x35FE80)
    u16 pit_flags = 0;            // F + 0x4B8: the pit stop's flags (0x40 / 0x80 the front / rear tires changed, 0x10 waiting,
                                  // 0x100 held, 0x200 the wheels on)
    u8 wrong_way = 0;             // F + 0x5FA: 1 while the race's "wrong way" message is up
    u8 message = 0;               // F + 0x5FB: a message's countdown (0x35ACB0: down by one a step to 0; 0xFF stays)
    F hit;                        // F + 0x608: the strongest hit of the step for the pad (0.2 .. 1; 0 none)
    s8 hit_kind = 0;              // F + 0x60C: its kind (0 a wall, 1 a car, 2 a pylon)
    u8 hit_part = 0;              // F + 0x60D: its part of the body (0 .. 7 a corner, 8 .. 15 a side + 8; 9 a pylon)
    u8 penalty_marked = 0;        // F + 0x62E: the race's "mark" penalty given (the rule block's +0x4A = 0xFF)
    u8 incidents = 0;             // F + 0x62F: hits and courses left (a car with driver kind 0), up to 100
    u8 incident_flags = 0;        // F + 0x630: bit 0 off the course (0x369888), bit 1 a hit in the last 60 steps
    u8 hit_steps = 0;             // F + 0x631: the steps bit 1 of F + 0x630 stays (60 from a hit)
    u8 soft_wheels = 0;           // F + 0x62C: the wheels on a soft material (0x369888)
    u8 pit_wheels = 0;            // F + 0x62D: the wheels on the pit lane (0x369888)
    s16 pit_wait = 0;             // F + 0x64C: the steps the pit's wait has left (flag 0x10, the counter at 721)
    std::array<u8, 2> pit_tires{}; // F + 0x64E / + 0x64F: the tires chosen for the front and rear (0xFE: as they are,
                                  // 0xFF: none)
};
template <class X, class V> void phase_fields(X& x, V&& v) {
    using layout::Block;
    const Block C = Block::Car, P = Block::Params;
    const u32 S = layout::kState;
    for (int k = 0; k < 8; ++k) v("phases.special", k, C, 0x788 + u32(k), x.special[size_t(k)]);
    v("phases.finished", -1, C, S + 0x4B2, x.finished), v("phases.failed", -1, C, S + 0x4B3, x.failed);
    v("phases.pit_flags", -1, C, S + 0x4B8, x.pit_flags);
    v("phases.wrong_way", -1, C, S + 0x5FA, x.wrong_way), v("phases.message", -1, C, S + 0x5FB, x.message);
    v("phases.hit", -1, C, S + 0x608, x.hit), v("phases.hit_kind", -1, C, S + 0x60C, x.hit_kind);
    v("phases.hit_part", -1, C, S + 0x60D, x.hit_part);
    v("phases.penalty_marked", -1, C, S + 0x62E, x.penalty_marked), v("phases.incidents", -1, C, S + 0x62F, x.incidents);
    v("phases.incident_flags", -1, C, S + 0x630, x.incident_flags), v("phases.hit_steps", -1, C, S + 0x631, x.hit_steps);
    v("phases.soft_wheels", -1, C, S + 0x62C, x.soft_wheels), v("phases.pit_wheels", -1, C, S + 0x62D, x.pit_wheels);
    v("phases.pit_wait", -1, C, S + 0x64C, x.pit_wait);
    v("phases.pit_tires", 0, C, S + 0x64E, x.pit_tires[0]), v("phases.pit_tires", 1, C, S + 0x64F, x.pit_tires[1]);
}

// The category byte (P + 0x32, AidExtra::mark_mode) of a car with moods (none on the disc).
constexpr u8 kPodCategory = 1;
// 0x35C7C8: a car with moods (category 1).
bool pod(const Extra& x);

// ---- phase 1 ------------------------------------------------------------------------------------------------------

// 0x343ED0: this step's values into the last step's (car + 0x8BC, §3.2): the attitude and the ground's, each of the
// layout's wheels' travel, steer, camber, half track, spin, brake heat, ground record's two values, query point and
// ground height, flap; the view's two lags and the distance along the course. The engine's save_previous but on the
// layout's wheels only.
void save_previous(ex::Car& c, const WheelLayout& l);

// ---- phase 2 ------------------------------------------------------------------------------------------------------

// 0x355E10 (0x355D48: the driveline's layout, P + 0x234, is 7): car +0x78D (a byte of the motors' state) cleared.
void layout7_step(const ex::Car& c, Extra& x);

// ---- phases 3 and 6: the moods ------------------------------------------------------------------------------------

// The moods' tables (0x620880, 16 bytes each, by mood): the steps a mood lasts when the caller gives none; its lean (1
// leans one way, -1 the other, 2 nods; signed); its lamps' class (a change of class fades the lamps: 24 steps from class 0,
// 36 from another).
struct MoodTables {
    std::array<u8, 16> time{};
    std::array<s8, 16> lean{};
    std::array<u8, 16> lamp{};
};
constexpr u32 kMoodTime = 0x620880, kMoodLean = 0x620890, kMoodLamp = 0x6208A0;
MoodTables mood_tables(const Program& program);
template <class B> MoodTables mood_tables(B&& byte) {
    MoodTables t;
    for (u32 m = 0; m < 16; ++m) t.time[m] = byte(kMoodTime + m), t.lean[m] = s8(byte(kMoodLean + m)), t.lamp[m] = byte(kMoodLamp + m);
    return t;
}
// The moods (what asks for each).
enum Mood : int {
    kMoodRest = 0,
    kMoodRival = 1,    // a rival passing or passed, on the same lap (rival level 5): 120 steps
    kMoodHappy = 2,    // a win; a full slipstream: 120 steps
    kMoodPit = 3,      // on the pit lane
    kMoodHurt = 4,     // hit by a car at a corner (0, 3, 4, 7) or a side (11, 15)
    kMoodAngry = 5,    // hit by a car at the front (1, 2, 8 .. 10); a pylon
    kMoodWrongWay = 6, // the "wrong way" message
    kMoodSad = 7,      // a race finished not first
    kMoodScared = 8,   // hit by a car at the back (5, 6, 12 .. 14)
    kMoodBump = 9,     // a wall
    kMoodDirt = 10,    // every wheel on a soft material
};
// 0x35C828: the mood (special[1]'s low four bits).
inline u8 mood(const PhaseExtra& x) { return u8(x.special[1] & 0xF); }
// 0x35C9B0: the nod's position at its phase (0 .. 47): up to 12 over the first quarter, down to -12, back to 0.
s32 nod(u8 phase);
// 0x35C848: a pod asks for `mood` for `time` steps (-1: the mood's own; 0xFF: for good; 0: only when no mood's time runs).
// Nothing while the lamps fade (unless for good) or after a mood set for good. A new mood is noted in special[6] and moves
// the old into special[1]'s high bits; a change of the lamps' class starts their fade.
void set_mood(Extra& x, const MoodTables& t, s32 mood, s32 time);
// 0x35C9E0: every one of the layout's wheels on a soft material (0x3F4EA0: the word of 0x622430 by the ground record's
// material, `soft` as rules::BumpTables::soft).
bool all_soft(const ex::Car& c, const WheelLayout& l, const std::array<u32, 32>& soft);

// The race's pass-by states (conductor + 0xCD7C, kept by 0x4146E8 after the per-car phases) and the gap matrices the race's
// order keeps (conductor + 0xCCC8 laps, signed bytes, and + 0xCCEC gaps along the course, per pair of cars i, j).
struct PassBy {
    static constexpr int kCars = 6;
    std::array<std::array<u8, kCars>, kCars> state{}; // + 0x4C + 6 i + j: 2 and 3 a car passing (§)
    std::array<std::array<s8, kCars>, kCars> laps{};
    std::array<std::array<F, kCars>, kCars> gap{};
};
constexpr u32 kPassBy = 0xCD7C, kPassByStates = kPassBy + 0x4C, kGapLaps = 0xCCC8, kGaps = 0xCCEC;
// 0x366018: car i's laps ahead of car j with the gap folded into half a lap (`lap`: the lap's length); `gap` the gap so folded.
s32 laps_apart(const PassBy& p, int i, int j, F lap, F& gap);
// 0x4149D8: how close car i's nearest rival is, 0 .. 5 (5: one passing it or it passing one, on the same lap; 4 / 3 one
// it passes or passing it; 2 / 1 a lap apart), of the race's `count` cars; `who` the rival (-1 none).
s32 rival_level(const PassBy& p, int i, int count, F lap, s32* who = nullptr);

// 0x35CA60 (phase 3, a pod): the mood the race asks for, in order: a finished race (a win happy, else sad), the "wrong
// way" message, the pit lane, a rival alongside (`rival`: rival_level, 5), a full slipstream, all wheels on a soft
// material, else rest once no mood's time runs; the mood's time counted down (rest after it); the lamps' fade counted
// down (to its end at once into a class-0 mood); the lean moved one step toward the mood's (or back toward 0) while the
// nod is level, at most 30 either way; the nod moved on while the lean is 0 in a nodding mood or until it is level.
void moods(const ex::Car& c, Extra& x, const MoodTables& t, const WheelLayout& l, const std::array<u32, 32>& soft, s32 rival);
// 0x35D158: a pod standing still (|velocity| along and across the body not above 1 km/h) with the handbrake fully pulled.
bool standing(const ex::Car& c, const Extra& x);
// 0x35D1D0 (phase 6, after the driver's input): special[0] up by one to 34 while standing, down to 0 otherwise.
void stop_count(const ex::Car& c, Extra& x);
// 0x35D230: the pod sits (special[0] at 34).
inline bool sitting(const PhaseExtra& x) { return x.special[0] == 0x22; }

// ---- phase 4 ------------------------------------------------------------------------------------------------------

// 0x35ACB0: the message's countdown (F + 0x5FB) down by one, unless 0 or 0xFF.
void message_timer(PhaseExtra& x);
// 0x3658A0: the step length (F + 0x448) the time scale / 60 (0x3C888888: 1/60 a unit low) and its inverse 60 / the
// time scale.
void step_length(ex::Car& c);
// 0x354AC0: the masses with the fuel: F + 0x4D4 = P + 0x48 + 0.74 (0x3F3D70A3) x the fuel F + 0x4D0, F + 0x4D8 = P + 0x4C
// + the same.
void masses(const ex::Car& c, Extra& x);
// 0x362E58: wheel w's tires new: the wear grip 1, the tread wear, the HUD level, the second wear, the brake heat and the
// byte +0xB0 cleared; with the tread wear on (the race's wear rules), the tread wear at the rules' least value, its grip
// 1 - the loss there, the level -0x3F below 0 (with the second wear off); with the second wear on, likewise its least value
// and grip (times the first), the level by it. A car of category 4: all as new.
void fresh_tires(ex::Car& c, Extra& x, int w, const WearRules& r);
// What a pit stop asks of the race outside the car's record (the pit's choices, the equipment, the converter).
struct PitHooks {
    // 0x35BF08(car, 0xFE, 0xFE, 0xFE, 0xFF): the wait over, the car's request to leave the pit (the conductor's record at
    // + 0xF910 + 4 x index, the race's command 10 through 0x3F1BD8).
    std::function<void()> leave;
    // At the counter's 645: an axle whose choice (F + 0x64E / + 0x64F) is 0xFE keeps its tires (the conductor's byte
    // + 0xFB20 + 2 i + axle into + 0xDA20 + 8 i + axle); with a choice below 0xFE (`chosen`), the entry's equipment with the
    // chosen tires (0x35C6D8 from the pit's list, 0x4468F8; both conductor bytes the choice), the spec built again
    // (0x3F3B68, the course's kind) and the converter's tire part on it (0x361570) for `axles` (1 front, 2 rear).
    std::function<void(u8 axles, bool chosen)> change_tires;
    // At 644 with a choice: 0x361798 (the tire records' first words again at the axles' static loads) for `axles`.
    std::function<void(u8 axles)> retune;
};
// 0x35C0B8 (while the hold counter F + 0x4BA runs): waiting in the pit (flag 0x10 at 721) the wait F + 0x64C counted
// down, the leave hook at its end; held (flag 0x100 at 90) nothing; else the counter down by one, and with the tires
// changed (flags 0x40 / 0x80): at 645 and 644 the hooks above (the choices cleared to 0xFF), at 60 each wheel ahead of the
// center of gravity new with 0x40, behind with 0x80, at it with either (fresh_tires), the two flags cleared; at 59 the
// flag 0x200.
void pit_step(ex::Car& c, Extra& x, const WheelLayout& l, const WearRules& r, const PitHooks* hooks = nullptr);
// 0x357700 (phase 4): pit_step while the hold counter runs, message_timer, step_length, masses, the aerodynamics
// (0x3F3090), the engine's full-throttle and friction torques, F + 0x5BC, the wear's grip, peak forces and slip angles
// (rules_step.h prep_tires). `grip`: a computer driver's grip by tire class (0x467470), null for a player's car. The pit
// stop's fresh tires take the race's wear rules (Tables::wear). The start's hold (F + 0x4BC) is not set here: the step
// sets it before the phase (0x34C210 and the car's own start counter, conductor + 0xCBFC + 4 i).
void prep(ex::Car& c, Extra& x, const WheelLayout& l, const Tables& t, const std::array<F, 3>* grip = nullptr,
          const PitHooks* hooks = nullptr);

// ---- phases 12 and 13: the hit records ----------------------------------------------------------------------------

// The rule block's bytes (conductor + 0xF818 ..) the hit records read.
struct HitRules {
    u8 pylons = 0;     // +0x46 (0x35DF00): the pylons a license test lets a car hit (0: none; the pad feels none either)
    u8 car_limit = 0;  // +0x48: the car hit's strength x 100 a test or a race lets pass (0: none, 0xFF: any)
    u8 wall_limit = 0; // +0x49: the wall hit's change of speed (m/s) it lets pass
    u8 penalty = 0;    // +0x4A: a race's penalty for a hit past the limits: the seconds of the speed cap (0: none; 0xFF:
                       // a mark, F + 0x62E)
};
constexpr u32 kRuleBlock = 0xF818;
template <class B> HitRules hit_rules(B&& rule_byte) { // (the rule block's byte at an offset)
    return {rule_byte(0x46), rule_byte(0x48), rule_byte(0x49), rule_byte(0x4A)};
}
// The conductor's judge of a hit (its virtual +0x190): the single-player, battle, free run and mission conductors'
// penalty (0x35FCC8), the license and training conductors' test (0x3F6938 → 0x35FE80).
enum class Judge : u8 { None, Penalty, License };

// 0x350308 (phase 12): the hit record (F + 0x608) cleared unless it is a pylon's (kind 2).
void clear_hit(PhaseExtra& x);
// 0x350328: a hit for the pad: kind 0 a wall, the strength / 50,000; 1 a car, x 2; 2 a pylon (only with pylons allowed),
// / 50 and at least 0.2; others 0; at most 1, 0 below 0.2. Kept with its kind and part when stronger than the step's so
// far. Kind -1: nothing.
void hit_record(PhaseExtra& x, const HitRules& r, s32 kind, s32 part, F strength);
// 0x35CF40 / 0x35CF88 / 0x35D0C0: a pod's mood for a hit: a wall 9; a car by the part hit: 1, 2 angry, 5, 6 scared, 0, 3,
// 4, 7 hurt (their own times); 8 .. 10 angry, 11 and 15 hurt, 12 .. 14 scared for 210 steps; a pylon angry.
void hit_mood(Extra& x, const MoodTables& t, s32 kind, s32 part);
// 0x35FBC8: the part's class: 1 the front (corners 1, 2; sides 8, 9, 10), 2 the front corners' outer points (0, 3), 3
// the middle (4, 7, 11, 15), 4 the back (5, 6, 12 .. 14) and anything past 15.
s32 part_class(s32 part);
// 0x35FC10: a value past a limit: limit 0 always, 0xFF never, else the value (truncated) at or above it.
bool past_limit(u8 limit, F value);
// 0x35FCC8 (the penalty): for a player's car (control source and driver kind 0) whose race runs, with the rule block's
// +0x4A set: a car hit at the front (class 1 or 2) whose strength x 100 is past +0x48, or a wall hit whose change of speed
// (the impulse over the mass P + 0x48) is past +0x49 (also at the front only), gives the penalty: +0x4A seconds of the
// speed cap (F + 0x60E = 60 x seconds, 0x35FC48) unless one runs, or with 0xFF the mark F + 0x62E once.
void penalty(const ex::Car& c, Extra& x, const HitRules& r, s32 kind, s32 part, F strength);
// 0x35FE80 (a license test): for a player's car whose test runs: a wall hit whose change of speed is past +0x49, a car hit at
// the front whose strength x 100 is past +0x48, or a pylon past the pylons let (`pylons_hit`, conductor + 0x1003C: counted
// up to +0x46, 0x35DF28) fails the test (F + 0x4B3 bit 0).
void license_judge(const ex::Car& c, Extra& x, const HitRules& r, s16& pylons_hit, s32 kind, s32 part, F strength);
// 0x350530: a hit of the collision step (or a pylon's, 0x3E3C48) on a car: a pod's mood; for driver kind 0 the incident
// (F + 0x62F up by one unless the last 60 steps had one, to 100; F + 0x630 bit 1; F + 0x631 = 60); the conductor's judge;
// the pad's record (hit_record). The collision step's hits: a wall at a body's point (0x45E288) kind 0, the point's index,
// the impulse |j|; a wall's corner on a body's side (0x45E768) kind 0, the side + 8, |j|; two cars (0x45F340) kind 1,
// |j| / 15,000 on both: the car whose side was met its side + 8, the other its corner's index. A pylon (0x3E3C48) kind 2,
// part 9, the pylon's strength (0x3E3ED0).
void hit(const ex::Car& c, Extra& x, const MoodTables& t, const HitRules& r, Judge judge, s16* pylons_hit, s32 kind, s32 part,
         F strength);
// 0x369888's end (phase 18, after the bumps): the layout's wheels with a ground record on a soft material (F + 0x62C)
// and on the pit lane (F + 0x62D); for driver kind 0 a course left: every wheel on a soft material sets F + 0x630 bit 0
// and counts an incident (F + 0x62F, to 100) once, no wheel on one clears the bit.
void course_out(const ex::Car& c, Extra& x, const WheelLayout& l, const std::array<u32, 32>& soft);
// The step's end (0x34DB30 after the conductor's hooks): F + 0x631 down by one; at 0 the hit's bit of F + 0x630 cleared.
void hit_steps(PhaseExtra& x);

} // namespace gt4r::sim::rules
