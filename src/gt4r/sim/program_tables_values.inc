// Gran Turismo 4's program tables as values (program_tables.h), SCUS-97328 v1.01's, included by program_tables.cpp.

// the race globals: the demonstration deltas, the steering input words, the aids' and the four wheel drive's bytes: 0x620218 .. 0x620320.
struct RaceGlobals {
    std::array<u32, 8> delta_bases; // +0x000 (0x620218) a demonstration's delta bases (demonstration.cpp)
    std::array<u8, 16> other_20; // +0x020 (0x620238) (not read)
    F wheel_slew; // +0x030 (0x620248) the wheel's slew x dt (rules_input: PadTables::wheel_slew)
    std::array<F, 2> least_lock_a; // +0x034 (0x62024C) the least lock, set a
    std::array<F, 2> least_lock_b; // +0x03C (0x620254) the least lock, set b
    std::array<F, 3> wheel6_ratio; // +0x044 (0x62025C) a kind 6 wheel's ratios
    F analog_jump; // +0x050 (0x620268) the analog pedals' jump
    std::array<u8, 4> other_54; // +0x054 (0x62026C) (not read)
    std::array<F, 6> ramps; // +0x058 (0x620270) the pedals' ramps: 100 / (60 b)
    std::array<u8, 24> fixed_aim; // +0x070 (0x620288) per surface: the steering offset aims at 0 (rules_steer, rules_near)
    std::array<u8, 16> ascc; // +0x088 (0x6202A0) the ASCC's bytes (rules_aids kAscc)
    std::array<u8, 10> sway; // +0x098 (0x6202B0) the view's sway bytes (rules_feel kSwayBytes): +0 on .. +9
    u8 hit_jolt_on; // +0x0A2 (0x6202BA) a hit's change of velocity kept (1)
    std::array<u8, 6> hit_speeds; // +0x0A3 (0x6202BB) km/h bytes of the hit record (10, 200, 1, 50, 0, 20)
    std::array<u8, 39> other_a9; // +0x0A9 (0x6202C1) (not read)
    F hit_least; // +0x0D0 (0x6202E8) the least change of velocity kept (m/s)
    std::array<F, 4> hit_scales; // +0x0D4 (0x6202EC) its scales
    std::array<u8, 4> other_e4; // +0x0E4 (0x6202FC) (not read)
    std::array<u8, 8> diff_letters; // +0x0E8 (0x620300) the differential type letters (convert kDiffLetters)
    u8 awd_gain; // +0x0F0 (0x620308) four wheel drive system 3's gain (%)
    u8 awd_capacity; // +0x0F1 (0x620309) its capacity (x 10 kgf m)
    u8 awd5_negative_full; // +0x0F2 (0x62030A) four wheel drive system 5's switch: a negative torque gives minus the full transfer
    u8 other_f3; // +0x0F3 (0x62030B) (not read) ❓ read by GT4R
    F awd_fade_start; // +0x0F4 (0x62030C) its fade's start
    std::array<u8, 4> awd_fade_start_pct; // +0x0F8 (0x620310) ... from these bytes (%)
    F awd_fade_floor; // +0x0FC (0x620314) its fade's floor
    std::array<u8, 8> awd_fade_floor_pct; // +0x100 (0x620318) ... from these bytes (%)
};
constexpr RaceGlobals kRaceGlobals{
    .delta_bases = {
        0x0, 0x2, 0xA, 0x2A,
        0xAA, 0x2AA, 0xAAA, 0x2AAA,
    },
    .other_20 = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    .wheel_slew = F::raw(0x3FC90FD9),
    .least_lock_a = {F::raw(0x3E0EFA34), F::raw(0x3E860A90)},
    .least_lock_b = {F::raw(0x3E56774E), F::raw(0x3EB2B8C1)},
    .wheel6_ratio = {F::raw(0x39800000), F::raw(0x39800000), F::raw(0x39800000)},
    .analog_jump = F::raw(0x3DCCCCCC),
    .other_54 = {0, 0, 0, 0},
    .ramps = {
        F::raw(0x3E2AAAAA), F::raw(0x3E2AAAAA), F::raw(0x3E2AAAAA), F::raw(0x3E2AAAAA),
        F::raw(0x3F800000), F::raw(0x3F800000),
    },
    .fixed_aim = {
        0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1,
        0, 0, 0, 0, 0, 0, 0, 0,
    },
    .ascc = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    .sway = {1, 35, 30, 100, 20, 1, 35, 30, 100, 20},
    .hit_jolt_on = 1,
    .hit_speeds = {10, 200, 1, 50, 0, 20},
    .other_a9 = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0,
    },
    .hit_least = F::raw(0x00000000),
    .hit_scales = {F::raw(0x00000000), F::raw(0x00000000), F::raw(0x00000000), F::raw(0x00000000)},
    .other_e4 = {0, 0, 0, 0},
    .diff_letters = {102, 114, 110, 109, 104, 118, 121, 116},
    .awd_gain = 200,
    .awd_capacity = 0,
    .awd5_negative_full = 1,
    .other_f3 = 0,
    .awd_fade_start = F::raw(0x3E800000),
    .awd_fade_start_pct = {25, 0, 0, 0},
    .awd_fade_floor = F::raw(0x3E199999),
    .awd_fade_floor_pct = {15, 0, 0, 0, 0, 0, 0, 0},
};
void pack(Pack& p, const RaceGlobals& s) { p(s.delta_bases)(s.other_20)(s.wheel_slew)(s.least_lock_a)(s.least_lock_b)(s.wheel6_ratio)(s.analog_jump)(s.other_54)(s.ramps)(s.fixed_aim)(s.ascc)(s.sway)(s.hit_jolt_on)(s.hit_speeds)(s.other_a9)(s.hit_least)(s.hit_scales)(s.other_e4)(s.diff_letters)(s.awd_gain)(s.awd_capacity)(s.awd5_negative_full)(s.other_f3)(s.awd_fade_start)(s.awd_fade_start_pct)(s.awd_fade_floor)(s.awd_fade_floor_pct); }

// the car globals: the showroom's rule block, the motors' bytes, the wheel layouts, the special rows, the lean and the pit tables: 0x6203C8 .. 0x6208B0.
struct CarGlobals {
    std::array<u8, 128> showroom_rules; // +0x000 (0x6203C8) the rule block ShowRoomCar's build passes (showroom.cpp, 0x364868)
    std::array<u8, 18> gear_steps; // +0x080 (0x620448) a made gearbox's steps between gears, the rows of 5, 6 and 7 gears (and more), 6 bytes each: three pairs eased by the slider (car_spec gear_ratios, 0x354FF8)
    std::array<u8, 30> motor_bytes; // +0x092 (0x62045A) layout 7's motors' bytes (rules_motors kMotorBytes)
    std::array<u8, 336> wheel_layouts; // +0x0B0 (0x620478) the wheel layouts, 0x54 bytes a row (rules.h kWheelLayouts)
    std::array<u8, 360> special_rows; // +0x200 (0x6205C8) the special cars' bodies and tires, 0x78 bytes a row (convert kSpecial, rules_steer kTwoWheelRows)
    std::array<F, 4> lean_force; // +0x368 (0x620730) the lean's force terms
    std::array<u8, 8> other_378; // +0x378 (0x620740) (not read)
    std::array<F, 5> lean_follow; // +0x380 (0x620748) the lean follower's spring and damping
    F anim_throttle_share; // +0x394 (0x62075C) the driver's animation's following of the throttle
    F anim_brake_share; // +0x398 (0x620760) ... and of the brake
    F camber_ref; // +0x39C (0x620764) the camber force's reference angle
    F camber_gain; // +0x3A0 (0x620768) ... and gain
    std::array<F, 6> in_line; // +0x3A4 (0x62076C) the in-line layout's roll terms (rules_body kInLine)
    u32 roll_on; // +0x3BC (0x620784) the roll rate in each hub's sideways speed (0)
    u32 lean_on; // +0x3C0 (0x620788) a two-wheeler's lean in its front hub's sideways speed
    std::array<F, 4> lean_steer; // +0x3C4 (0x62078C) the steering's spring toward the lean
    F lean_rate_max; // +0x3D4 (0x62079C)
    F lean_yaw; // +0x3D8 (0x6207A0)
    std::array<u8, 16> other_3dc; // +0x3DC (0x6207A4) (not read)
    F special_blend; // +0x3EC (0x6207B4) a car of category 6's follower of the steering
    std::array<u8, 44> other_3f0; // +0x3F0 (0x6207B8) (not read)
    std::array<u8, 124> pit_messages; // +0x41C (0x6207E4) the pit messages, 8 bytes each: +0 their time (pit_flow.cpp)
    std::array<u8, 16> pit_least_kind; // +0x498 (0x620860) the least tire kind choosable by the kind fitted (pit_tires.cpp)
    std::array<u8, 16> pit_most_kind; // +0x4A8 (0x620870) ... and the most
    std::array<u8, 16> mood_time; // +0x4B8 (0x620880) special car 1's moods: per mood its time (rules_phases)
    std::array<s8, 16> mood_lean; // +0x4C8 (0x620890) ... its lean
    std::array<u8, 16> mood_lamp; // +0x4D8 (0x6208A0) ... its lamp class
};
constexpr CarGlobals kCarGlobals{
    .showroom_rules = {
        0, 0, 0, 0, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
        100, 100, 100, 100, 100, 100, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0,
        100, 0, 2, 0, 10, 15, 3, 0, 10, 20, 4, 0, 0, 0, 100, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .gear_steps = {
        155, 165, 188, 190, 190, 200, 165, 175, 190, 192, 190, 200, 170, 180, 192, 194,
        190, 200,
    },
    .motor_bytes = {
        20, 35, 6, 15, 6, 15, 50, 15, 20, 35, 20, 50, 75, 70, 1, 50,
        10, 1, 7, 255, 20, 50, 50, 10, 50, 50, 10, 0, 0, 0,
    },
    .wheel_layouts = {
        0, 4, 2, 127, 2, 2, 127, 127, 2, 2, 127, 127, 0, 2, 127, 127,
        0, 1, 127, 127, 0, 0, 1, 1, 127, 127, 127, 127, 0, 1, 0, 1,
        127, 127, 127, 127, 1, 0, 3, 2, 127, 127, 127, 127, 0, 0, 1, 1,
        127, 127, 127, 127, 248, 8, 248, 8, 127, 127, 127, 127, 8, 8, 248, 248,
        127, 127, 127, 127, 0, 0, 0, 0, 127, 127, 127, 127, 0, 1, 2, 3,
        127, 127, 127, 127, 1, 2, 2, 127, 1, 1, 127, 127, 1, 1, 127, 127,
        0, 1, 127, 127, 0, 1, 127, 127, 0, 1, 127, 127, 127, 127, 127, 127,
        2, 2, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127,
        0, 1, 127, 127, 127, 127, 127, 127, 0, 0, 127, 127, 127, 127, 127, 127,
        8, 248, 127, 127, 127, 127, 127, 127, 2, 2, 127, 127, 127, 127, 127, 127,
        0, 0, 1, 1, 127, 127, 127, 127, 2, 3, 2, 127, 1, 2, 127, 127,
        1, 2, 127, 127, 0, 1, 127, 127, 0, 1, 127, 127, 0, 1, 1, 127,
        127, 127, 127, 127, 2, 0, 1, 127, 127, 127, 127, 127, 127, 2, 1, 127,
        127, 127, 127, 127, 0, 1, 1, 127, 127, 127, 127, 127, 0, 248, 8, 127,
        127, 127, 127, 127, 8, 248, 248, 127, 127, 127, 127, 127, 1, 1, 1, 127,
        127, 127, 127, 127, 0, 0, 1, 2, 127, 127, 127, 127, 3, 3, 2, 127,
        2, 1, 127, 127, 2, 1, 127, 127, 0, 2, 127, 127, 0, 1, 127, 127,
        0, 0, 1, 127, 127, 127, 127, 127, 0, 1, 2, 127, 127, 127, 127, 127,
        1, 0, 127, 127, 127, 127, 127, 127, 0, 0, 1, 127, 127, 127, 127, 127,
        248, 8, 0, 127, 127, 127, 127, 127, 8, 8, 248, 127, 127, 127, 127, 127,
        1, 1, 1, 127, 127, 127, 127, 127, 0, 1, 2, 2, 127, 127, 127, 127,
    },
    .special_rows = {
        153, 153, 153, 62, 143, 194, 245, 61, 102, 102, 166, 62, 20, 174, 71, 62,
        235, 81, 184, 189, 147, 24, 4, 63, 139, 108, 7, 63, 149, 67, 11, 191,
        129, 149, 195, 62, 108, 231, 27, 191, 139, 108, 7, 63, 149, 67, 11, 191,
        48, 158, 214, 62, 40, 92, 15, 61, 217, 206, 55, 63, 221, 36, 198, 62,
        104, 145, 109, 62, 106, 188, 116, 62, 57, 180, 72, 189, 120, 233, 38, 62,
        73, 12, 130, 62, 243, 253, 84, 188, 26, 47, 93, 61, 0, 0, 128, 63,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 153, 153, 153, 62, 143, 194, 245, 61,
        10, 215, 163, 62, 165, 155, 68, 62, 149, 67, 11, 190, 153, 153, 25, 63,
        6, 129, 213, 62, 239, 167, 6, 191, 24, 4, 150, 62, 32, 176, 18, 191,
        6, 129, 213, 62, 239, 167, 6, 191, 200, 126, 185, 62, 235, 81, 56, 61,
        92, 143, 34, 63, 146, 24, 196, 62, 26, 47, 93, 62, 133, 235, 81, 62,
        143, 194, 117, 188, 98, 16, 88, 62, 251, 169, 113, 62, 118, 190, 31, 189,
        165, 155, 196, 59, 0, 0, 128, 63, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        153, 153, 153, 62, 143, 194, 245, 61, 153, 153, 153, 62, 235, 81, 56, 62,
        69, 182, 243, 189, 155, 196, 64, 63, 8, 172, 220, 62, 2, 43, 199, 190,
        26, 47, 157, 62, 100, 59, 223, 190, 8, 172, 220, 62, 2, 43, 199, 190,
        200, 126, 185, 62, 104, 145, 237, 60, 100, 59, 31, 63, 36, 6, 193, 62,
        204, 204, 204, 62, 141, 151, 110, 62, 110, 18, 131, 186, 71, 225, 186, 62,
        141, 151, 110, 62, 143, 194, 245, 188, 110, 18, 3, 61, 0, 0, 128, 63,
        0, 0, 0, 0, 0, 0, 0, 0, 110, 18, 67, 63, 40, 92, 15, 190,
        110, 18, 35, 191, 133, 235, 177, 63,
    },
    .lean_force = {F::raw(0x3F800000), F::raw(0x3D11E530), F::raw(0x3D11E530), F::raw(0x00000000)},
    .other_378 = {204, 204, 204, 61, 226, 56, 94, 65},
    .lean_follow = {
        F::raw(0x41200000), F::raw(0x3FC00000), F::raw(0x40A00000), F::raw(0x40A00000),
        F::raw(0x3FF47648),
    },
    .anim_throttle_share = F::raw(0x3EE66666),
    .anim_brake_share = F::raw(0x3E999999),
    .camber_ref = F::raw(0x41F00000),
    .camber_gain = F::raw(0x3E4CCCCC),
    .in_line = {
        F::raw(0x42A00000), F::raw(0x00000000), F::raw(0x3F4CCCCC), F::raw(0x41200000),
        F::raw(0x41200000), F::raw(0x41200000),
    },
    .roll_on = 0x0,
    .lean_on = 0x0,
    .lean_steer = {F::raw(0x00000000), F::raw(0x00000000), F::raw(0x00000000), F::raw(0x00000000)},
    .lean_rate_max = F::raw(0x00000000),
    .lean_yaw = F::raw(0x00000000),
    .other_3dc = {102, 102, 102, 63, 77, 228, 189, 63, 204, 204, 204, 62, 143, 194, 117, 61},
    .special_blend = F::raw(0x3CCCCCCC),
    .other_3f0 = {
        153, 153, 153, 62, 144, 10, 6, 62, 0, 0, 0, 64, 235, 81, 56, 62,
        52, 250, 14, 61, 30, 133, 107, 62, 78, 119, 214, 60, 102, 102, 166, 63,
        78, 119, 86, 61, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .pit_messages = {
        0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0,
        0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0,
        0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0,
        120, 0, 0, 0, 7, 0, 0, 0, 120, 0, 0, 0, 8, 0, 0, 0,
        120, 0, 0, 0, 9, 0, 0, 0, 120, 0, 0, 0, 10, 0, 0, 0,
        0, 0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 12, 0, 0, 0,
        120, 0, 0, 0, 13, 0, 0, 0, 255, 0, 0, 0, 14, 0, 0, 0,
        255, 0, 0, 0, 15, 0, 0, 0, 0, 0, 0, 0,
    },
    .pit_least_kind = {0, 0, 0, 3, 3, 3, 6, 6, 6, 6, 6, 11, 12, 0, 0, 0},
    .pit_most_kind = {2, 2, 2, 5, 5, 5, 10, 10, 10, 10, 10, 11, 12, 0, 0, 0},
    .mood_time = {0, 180, 0, 180, 180, 180, 180, 180, 180, 180, 0, 0, 0, 0, 0, 0},
    .mood_lean = {0, 2, 1, 2, 1, 1, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0},
    .mood_lamp = {0, 4, 4, 4, 1, 1, 1, 2, 2, 2, 3, 0, 0, 0, 0, 0},
};
void pack(Pack& p, const CarGlobals& s) { p(s.showroom_rules)(s.gear_steps)(s.motor_bytes)(s.wheel_layouts)(s.special_rows)(s.lean_force)(s.other_378)(s.lean_follow)(s.anim_throttle_share)(s.anim_brake_share)(s.camber_ref)(s.camber_gain)(s.in_line)(s.roll_on)(s.lean_on)(s.lean_steer)(s.lean_rate_max)(s.lean_yaw)(s.other_3dc)(s.special_blend)(s.other_3f0)(s.pit_messages)(s.pit_least_kind)(s.pit_most_kind)(s.mood_time)(s.mood_lean)(s.mood_lamp); }

// the physics block area: the grid lines, the steering's compliance, the wear's second set, the hybrid's tables, the dampers' table and the static physics block: 0x620AD8 .. 0x620E38.
struct StaticArea {
    std::array<u8, 24> grid_lines; // +0x000 (0x620AD8) the start's lines by grid place (race_start kGridLines)
    std::array<u8, 6> compliance_bytes; // +0x018 (0x620AF0) the steering's compliance bytes (rules_steer kComplianceBytes); +6 the dirt speed's s16 (x 1000)
    s16 dirt_max; // +0x01E (0x620AF6) the greatest dirt (x 1000): the condition's body (rules_speeds, game_car, game_race)
    std::array<u8, 8> compliance_more; // +0x020 (0x620AF8)
    std::array<u8, 96> compliance; // +0x028 (0x620B00) the steering's compliance words the race's start makes (kCompliance)
    u8 wear_second_on; // +0x088 (0x620B60) the tire wear's second set: on (rules_wear)
    u8 other_89; // +0x089 (0x620B61) (not read)
    std::array<s16, 7> wear_second; // +0x08A (0x620B62) ... its terms (x 0.001)
    s16 wear_second_rate; // +0x098 (0x620B70)
    std::array<u8, 5> wear_second_bytes; // +0x09A (0x620B72)
    std::array<u8, 25> other_9f; // +0x09F (0x620B77) (not read)
    std::array<u8, 240> hybrid; // +0x0B8 (0x620B90) the hybrid box's tables (rules_hybrid: 0x620BA8 / 0x620BF8 by rpm, 0x620C80 / 0x620C90 / 0x620CA0)
    std::array<u8, 48> hybrid_more; // +0x1A8 (0x620C80)
    std::array<u8, 40> damper_table; // +0x1D8 (0x620CB0) the dampers' square-law table (rules_body kDamperTable, made at the start)
    std::array<u8, 28> static_head; // +0x200 (0x620CD8) the static physics block (Program::kStatic): +0x00 air density x 10, +0x01 / +0x02 slipstream shares, +0x03 / +0x04 added mass, +0x05 / +0x06 slipstream build-up and decay, +0x07 the pitch's tire term, +0x08, +0x0A, +0x0B the rolling-resistance curve's points, +0x0C its speeds, +0x14 its values
    std::array<u8, 24> bump_amplitude; // +0x21C (0x620CF4) per surface: bump amplitude (mm)
    std::array<s16, 24> rolling_mu; // +0x234 (0x620D0C) ... rolling resistance (x 0.01)
    std::array<u8, 24> bump_wavelength; // +0x264 (0x620D3C) ... bump wavelength (dm)
    std::array<u8, 24> bump_scaled; // +0x27C (0x620D54) ... bump scaled by speed
    std::array<u8, 24> bump_mode; // +0x294 (0x620D6C) ... bump mode
    std::array<u8, 180> static_tail; // +0x2AC (0x620D84) +0xAC brake heat, +0xB4 the slip ratio's bytes, +0xBC / +0xBD the tire load knees, +0xBF three grip bytes, +0xD0 the engine condition's nine bytes, +0xDD / +0xDF the oversteer indicator's angles and gains, +0xE5 .. +0xE7 the dirt steering aid, +0x102 .. +0x10B the dampers' knees and shares, +0x13D / +0x13E the spec builder's inertia shares, +0x14A / +0x14B two spec defaults, +0x14C fuel (rules_*, convert, car_spec)
};
constexpr StaticArea kStaticArea{
    .grid_lines = {
        6, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 6, 0, 0, 0,
        2, 0, 0, 0, 1, 0, 0, 0,
    },
    .compliance_bytes = {1, 100, 10, 50, 244, 1},
    .dirt_max = 3000,
    .compliance_more = {50, 50, 0, 45, 0, 0, 0, 0},
    .compliance = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .wear_second_on = 0,
    .other_89 = 0,
    .wear_second = {50, 1000, 1000, 1000, 1000, 500, 20},
    .wear_second_rate = 300,
    .wear_second_bytes = {10, 10, 60, 0, 20},
    .other_9f = {
        0, 0, 0, 122, 68, 0, 0, 150, 68, 0, 128, 237, 68, 0, 128, 9,
        69, 0, 192, 40, 69, 0, 0, 122, 69,
    },
    .hybrid = {
        157, 42, 199, 56, 114, 113, 194, 56, 119, 15, 181, 56, 240, 69, 180, 56,
        226, 178, 178, 56, 254, 216, 181, 56, 6, 0, 0, 0, 144, 11, 98, 0,
        144, 11, 98, 0, 0, 0, 0, 0, 0, 0, 122, 68, 0, 0, 225, 68,
        0, 192, 15, 69, 0, 64, 103, 69, 0, 160, 140, 69, 0, 0, 150, 69,
        0, 64, 156, 69, 0, 0, 0, 0, 206, 129, 190, 56, 90, 233, 177, 56,
        76, 86, 176, 56, 226, 178, 178, 56, 240, 69, 180, 56, 254, 216, 181, 56,
        162, 200, 185, 56, 0, 0, 0, 0, 7, 0, 0, 0, 216, 11, 98, 0,
        216, 11, 98, 0, 0, 0, 0, 0, 0, 0, 0, 0, 84, 85, 85, 64,
        84, 85, 213, 64, 84, 85, 5, 65, 113, 28, 131, 65, 226, 56, 182, 65,
        84, 85, 15, 66, 141, 227, 50, 66, 56, 142, 77, 66, 0, 0, 0, 0,
        0, 0, 32, 65, 0, 0, 200, 65, 0, 0, 52, 66, 0, 0, 76, 66,
        0, 0, 140, 66, 0, 0, 154, 66, 0, 0, 170, 66, 0, 0, 176, 66,
        0, 0, 180, 66, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 160, 192, 0, 0, 192, 192, 0, 0, 16, 193, 0, 0, 48, 193,
        0, 0, 72, 193, 0, 0, 96, 193, 0, 0, 112, 193, 0, 0, 0, 0,
    },
    .hybrid_more = {
        9, 0, 0, 0, 8, 12, 98, 0, 48, 12, 98, 0, 0, 0, 0, 0,
        9, 0, 0, 0, 8, 12, 98, 0, 88, 12, 98, 0, 0, 0, 0, 0,
        2, 0, 0, 0, 104, 64, 132, 0, 152, 64, 132, 0, 0, 0, 0, 0,
    },
    .damper_table = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
    },
    .static_head = {
        12, 6, 12, 65, 5, 5, 20, 50, 30, 0, 40, 8, 0, 2, 10, 29,
        58, 100, 154, 200, 0, 0, 10, 15, 32, 72, 144, 255,
    },
    .bump_amplitude = {
        0, 5, 16, 14, 20, 5, 0, 0, 0, 0, 3, 2, 0, 0, 0, 2,
        0, 0, 0, 0, 0, 0, 0, 0,
    },
    .rolling_mu = {
        30, 10, 20, 4000, 30, 20, 10, 10,
        10, 10, 10, 10, 60, 10, 10, 20,
        10, 10, 10, 10, 10, 10, 10, 10,
    },
    .bump_wavelength = {
        5, 5, 45, 40, 50, 5, 10, 10, 10, 10, 5, 4, 5, 10, 10, 5,
        10, 10, 10, 10, 10, 10, 10, 10,
    },
    .bump_scaled = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
    },
    .bump_mode = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1,
    },
    .static_tail = {
        7, 20, 100, 5, 0, 0, 0, 0, 10, 10, 0, 0, 0, 0, 0, 0,
        20, 40, 100, 80, 80, 80, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 3, 30, 10, 100, 50, 2, 50, 60, 50, 0, 0, 0,
        0, 7, 5, 30, 60, 0, 0, 0, 0, 15, 100, 0, 0, 0, 0, 0,
        0, 1, 0, 2, 2, 20, 20, 20, 20, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 1, 0, 0, 8, 8, 90, 90, 8, 8, 90,
        90, 0, 0, 0, 0, 1, 1, 0, 2, 0, 100, 1, 50, 50, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 2, 0, 2, 0, 0, 1, 0, 20, 1, 0, 0,
        0, 73, 13, 0, 0, 0, 0, 0, 0, 0, 1, 20, 40, 200, 80, 200,
        0, 50, 100, 100, 100, 100, 100, 0, 0, 4, 0, 0, 0, 0, 0, 0,
        0, 0, 52, 67,
    },
};
void pack(Pack& p, const StaticArea& s) { p(s.grid_lines)(s.compliance_bytes)(s.dirt_max)(s.compliance_more)(s.compliance)(s.wear_second_on)(s.other_89)(s.wear_second)(s.wear_second_rate)(s.wear_second_bytes)(s.other_9f)(s.hybrid)(s.hybrid_more)(s.damper_table)(s.static_head)(s.bump_amplitude)(s.rolling_mu)(s.bump_wavelength)(s.bump_scaled)(s.bump_mode)(s.static_tail); }

// the photo shoot's apertures (16 floats; game_photos): 0x620E70 .. 0x620EB0.
struct Apertures {
    std::array<F, 16> apertures; // +0x000 (0x620E70)
};
constexpr Apertures kApertures{
    .apertures = {
        F::raw(0x3FB33333), F::raw(0x3FE66666), F::raw(0x40000000), F::raw(0x400CCCCC),
        F::raw(0x40200000), F::raw(0x40333333), F::raw(0x404CCCCC), F::raw(0x40600000),
        F::raw(0x40800000), F::raw(0x40999999), F::raw(0x40B33333), F::raw(0x40D66666),
        F::raw(0x41000000), F::raw(0x41300000), F::raw(0x41800000), F::raw(0x41B00000),
    },
};
void pack(Pack& p, const Apertures& s) { p(s.apertures); }

// the computer drivers' pace, the TCS classes, the stepless rates, the controller settings, the speed part's damping, the aerodynamics' globals and the converter's kinds: 0x622080 .. 0x622330.
struct DriverArea {
    std::array<u8, 2> pace_start; // +0x000 (0x622080) the pace levels' start (ai/tables)
    std::array<u8, 6> other_2; // +0x002 (0x622082) (not read)
    std::array<s8, 8> pace_shift; // +0x008 (0x622088) the pace levels: shift (x 128 + 0x3200) / 100
    std::array<u8, 8> pace_brake; // +0x010 (0x622090) ... brake / 100
    std::array<u8, 8> pace_corner; // +0x018 (0x622098) ... corner / 100
    std::array<s8, 8> tcs_class_bytes; // +0x020 (0x6220A0) the TCS target's factor by a computer driver's class: (b + 100) / 100
    std::array<u8, 56> pace_made; // +0x028 (0x6220A8) the pace levels the race's configuration makes (0x3F1790)
    std::array<F, 5> tcs_class; // +0x060 (0x6220E0) ... the TCS classes it makes
    std::array<u8, 4> stepless_bytes; // +0x074 (0x6220F4) the stepless boxes' bytes (x 0.01)
    std::array<u8, 24> stepless; // +0x078 (0x6220F8) ... the rates made from them (0x3F2070)
    std::array<u8, 224> controller; // +0x090 (0x622110) the controller settings (GT3's 0x2D4628's layout; rules_input kPadSettings, rules_force, rules_drive): +0x08 the automatic gearbox's mode, +0x09 .. +0x0B the down-shift's offsets, +0x11 the analog slew, +0x22 / +0x26 the lock by slip / yaw, +0x4C .. the follower rates, +0x5C the wheel's slew, +0x61 .. the kind 6 ratios, +0x65 .. +0x71 the pedal curves, +0x8A + 0x20 r the prefilter
    std::array<u8, 80> other_170; // +0x170 (0x6221F0) (not read)
    std::array<u8, 40> speed_damp_bytes; // +0x1C0 (0x622240) the speed part's damping bytes (rules_near kSpeedDampBytes)
    std::array<u8, 160> speed_damp_rows; // +0x1E8 (0x622268) ... the rows made from them, 0x20 bytes each
    std::array<F, 5> aero; // +0x288 (0x622308) the aerodynamics' globals: density, the drag's and downforce's shares, the slipstream's rates up and down
    std::array<u8, 4> other_29c; // +0x29C (0x62231C) (not read)
    std::array<u8, 16> kinds; // +0x2A0 (0x622320) the converter's kind table (convert kKindTable)
};
constexpr DriverArea kDriverArea{
    .pace_start = {2, 2},
    .other_2 = {0, 0, 0, 0, 0, 0},
    .pace_shift = {-10, -5, 0, 1, 1, 0, 0, 0},
    .pace_brake = {90, 95, 100, 105, 110, 0, 0, 0},
    .pace_corner = {90, 95, 100, 105, 110, 0, 0, 0},
    .tcs_class_bytes = {-20, -10, 0, 10, 20, 0, 0, 0},
    .pace_made = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
    },
    .tcs_class = {
        F::raw(0x00000000), F::raw(0x00000000), F::raw(0x00000000), F::raw(0x00000000),
        F::raw(0x00000000),
    },
    .stepless_bytes = {40, 15, 0, 0},
    .stepless = {
        204, 204, 204, 62, 153, 153, 25, 62, 150, 150, 0, 0, 0, 0, 192, 63,
        0, 0, 192, 63, 0, 0, 0, 0,
    },
    .controller = {
        0, 0, 0, 0, 10, 0, 0, 0, 1, 5, 0, 0, 0, 0, 0, 0,
        1, 1, 0, 0, 10, 10, 5, 10, 1, 1, 0, 0, 100, 50, 25, 45,
        30, 37, 0, 50, 200, 100, 0, 100, 0, 50, 8, 16, 1, 5, 100, 2,
        1, 10, 0, 0, 1, 0, 50, 0, 250, 0, 0, 0, 30, 75, 90, 90,
        60, 60, 120, 150, 60, 75, 120, 150, 0, 0, 0, 0, 10, 10, 90, 90,
        60, 60, 120, 150, 60, 75, 120, 150, 0, 0, 0, 0, 90, 8, 15, 12,
        20, 0, 9, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 100, 0, 0, 128, 255, 32, 1, 0, 80, 0, 100, 100,
        0, 0, 0, 50, 0, 0, 0, 0, 0, 0, 0, 0, 8, 1, 0, 0,
        80, 0, 100, 0, 60, 80, 100, 100, 100, 1, 60, 0, 50, 2, 50, 50,
        0, 0, 0, 50, 0, 0, 0, 0, 0, 0, 1, 1, 15, 1, 2, 80,
        90, 2, 98, 1, 60, 80, 100, 90, 50, 20, 60, 0, 50, 2, 50, 50,
        0, 0, 0, 50, 0, 0, 0, 0, 0, 0, 1, 1, 15, 1, 2, 80,
        90, 2, 98, 1, 60, 80, 100, 90, 50, 20, 60, 0, 10, 10, 5, 5,
    },
    .other_170 = {
        0, 0, 0, 0, 6, 6, 16, 9, 9, 9, 0, 0, 15, 20, 30, 20,
        20, 0, 15, 15, 10, 20, 20, 0, 0, 0, 0, 30, 20, 0, 0, 0,
        0, 15, 15, 0, 2, 2, 5, 5, 5, 10, 0, 0, 10, 20, 20, 30,
        30, 0, 10, 10, 15, 10, 10, 0, 0, 0, 0, 30, 30, 0, 0, 0,
        0, 10, 10, 0, 2, 2, 5, 2, 2, 10, 0, 0, 0, 0, 0, 0,
    },
    .speed_damp_bytes = {
        0, 0, 0, 100, 100, 0, 0, 0, 2, 25, 50, 75, 75, 15, 125, 150,
        2, 25, 50, 78, 75, 15, 125, 150, 3, 25, 50, 78, 75, 180, 0, 0,
        1, 25, 50, 90, 90, 0, 0, 0,
    },
    .speed_damp_rows = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .aero = {
        F::raw(0x3F4CCCCC), F::raw(0x3F600000), F::raw(0x3F600000), F::raw(0x3F800000),
        F::raw(0x3E800000),
    },
    .other_29c = {0, 0, 0, 0},
    .kinds = {50, 100, 25, 50, 20, 40, 15, 40, 20, 40, 15, 40, 10, 40, 10, 40},
};
void pack(Pack& p, const DriverArea& s) { p(s.pace_start)(s.other_2)(s.pace_shift)(s.pace_brake)(s.pace_corner)(s.tcs_class_bytes)(s.pace_made)(s.tcs_class)(s.stepless_bytes)(s.stepless)(s.controller)(s.other_170)(s.speed_damp_bytes)(s.speed_damp_rows)(s.aero)(s.other_29c)(s.kinds); }

// tire curve B's default, the soft materials, the rally set-up, the ASCC switches, the slide effects' bytes and the cue bytes: 0x622420 .. 0x6226C0.
struct MaterialArea {
    std::array<u8, 16> tire_curve_b; // +0x000 (0x622420) the default tire curve B (x 6 bytes, y 6 bytes at +8)
    std::array<u32, 24> soft; // +0x010 (0x622430) a material's soft flag
    std::array<u8, 16> other_70; // +0x070 (0x622490) the machine tests' distances (floats 400, 1000, 6845, -1: DynamicsConductorMachineTest vf39 / vf43, race::machine_test_course)
    std::array<u8, 6> loose_setup; // +0x080 (0x6224A0) a rally car's changes on a loose course
    std::array<u8, 2> ascc_switches; // +0x086 (0x6224A6) the ASCC's switches
    u8 ascc_switch; // +0x088 (0x6224A8) the ASCC switch
    std::array<u8, 7> other_89; // +0x089 (0x6224A9) (read with the soft flags) ❓ read by GT4R
    std::array<u8, 480> slide; // +0x090 (0x6224B0) the slide effects' bytes (rules_effects kSlideBytes: +0xA0 measure, +0x28 kind ..)
    std::array<u8, 48> cue; // +0x270 (0x622690) the view cue's bytes (rules_feel kCueBytes)
};
constexpr MaterialArea kMaterialArea{
    .tire_curve_b = {0, 31, 62, 105, 165, 255, 0, 0, 0, 28, 61, 114, 180, 196, 0, 0},
    .soft = {
        0x0, 0x0, 0x1, 0x1,
        0x1, 0x0, 0x1, 0x0,
        0x0, 0x0, 0x0, 0x0,
        0x0, 0x0, 0x0, 0x0,
        0x0, 0x0, 0x0, 0x0,
        0x0, 0x0, 0x0, 0x0,
    },
    .other_70 = {0, 0, 200, 67, 0, 0, 122, 68, 0, 232, 213, 69, 0, 0, 128, 191},
    .loose_setup = {180, 80, 0, 0, 50, 50},
    .ascc_switches = {1, 1},
    .ascc_switch = 0,
    .other_89 = {0, 0, 0, 0, 0, 0, 0},
    .slide = {
        1, 4, 4, 4, 8, 50, 122, 255, 0, 56, 128, 183, 21, 96, 204, 255,
        0, 88, 143, 155, 25, 96, 204, 255, 0, 88, 143, 155, 30, 30, 30, 1,
        0, 3, 0, 0, 0, 0, 0, 0, 1, 4, 4, 4, 14, 30, 118, 127,
        0, 40, 232, 255, 12, 32, 112, 128, 0, 32, 224, 255, 12, 28, 60, 68,
        0, 52, 224, 255, 4, 3, 3, 0, 7, 1, 30, 0, 0, 0, 0, 0,
        1, 4, 4, 4, 14, 30, 118, 127, 0, 40, 232, 255, 12, 32, 112, 128,
        0, 32, 224, 255, 12, 28, 60, 68, 0, 52, 224, 255, 3, 4, 3, 0,
        0, 10, 12, 0, 0, 0, 0, 0, 1, 4, 4, 4, 14, 30, 118, 127,
        0, 40, 232, 255, 12, 32, 112, 128, 0, 32, 224, 255, 12, 28, 60, 68,
        0, 52, 224, 255, 15, 15, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 4, 4, 4, 8, 36, 102, 255, 0, 48, 100, 123, 4, 64, 156, 255,
        0, 80, 124, 151, 2, 64, 156, 255, 0, 80, 124, 155, 5, 5, 5, 1,
        50, 3, 0, 0, 0, 0, 0, 0, 1, 4, 4, 4, 10, 62, 162, 255,
        0, 60, 196, 203, 12, 28, 76, 88, 0, 32, 224, 255, 12, 20, 48, 72,
        0, 52, 224, 255, 20, 8, 8, 0, 50, 10, 10, 0, 0, 0, 0, 0,
        1, 4, 4, 4, 14, 46, 106, 255, 0, 96, 192, 187, 12, 32, 112, 128,
        0, 32, 224, 255, 12, 28, 60, 68, 0, 52, 224, 255, 4, 3, 3, 0,
        50, 30, 15, 0, 0, 0, 0, 0, 1, 4, 4, 4, 14, 30, 118, 127,
        0, 40, 232, 255, 12, 32, 112, 128, 0, 32, 224, 255, 12, 28, 60, 68,
        0, 52, 224, 255, 15, 15, 15, 0, 50, 0, 0, 0, 0, 0, 0, 0,
        1, 4, 4, 4, 104, 116, 124, 147, 0, 40, 128, 151, 168, 176, 192, 212,
        0, 32, 224, 255, 196, 204, 220, 240, 0, 52, 224, 255, 3, 1, 1, 1,
        30, 0, 0, 0, 0, 0, 0, 0, 1, 4, 4, 4, 28, 60, 236, 255,
        0, 40, 232, 255, 12, 32, 112, 128, 0, 32, 224, 255, 12, 28, 60, 68,
        0, 52, 224, 255, 5, 5, 2, 1, 0, 1, 40, 0, 0, 0, 0, 0,
        1, 4, 4, 4, 48, 88, 236, 255, 0, 40, 232, 255, 84, 108, 136, 164,
        0, 32, 224, 255, 56, 76, 100, 140, 0, 52, 232, 255, 1, 1, 1, 1,
        20, 10, 10, 0, 0, 0, 0, 0, 1, 4, 4, 4, 28, 60, 236, 255,
        0, 40, 232, 255, 12, 32, 112, 128, 0, 32, 224, 255, 12, 28, 60, 68,
        0, 52, 224, 255, 15, 15, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .cue = {
        30, 100, 50, 35, 3, 0, 50, 100, 100, 3, 25, 50, 100, 10, 70, 7,
        12, 65, 25, 75, 60, 0, 20, 1, 62, 60, 150, 28, 10, 10, 20, 25,
        5, 1, 5, 20, 43, 10, 60, 0, 30, 50, 0, 0, 0, 0, 0, 0,
    },
};
void pack(Pack& p, const MaterialArea& s) { p(s.tire_curve_b)(s.soft)(s.other_70)(s.loose_setup)(s.ascc_switches)(s.ascc_switch)(s.other_89)(s.slide)(s.cue); }

// the drawn driver's hands' speeds (11 floats; race_world): 0x622A20 .. 0x622A4C.
struct HandSpeeds {
    std::array<F, 11> speeds; // +0x000 (0x622A20)
};
constexpr HandSpeeds kHandSpeeds{
    .speeds = {
        F::raw(0x3E85DE9C), F::raw(0x3EFA7631), F::raw(0x3F527E95), F::raw(0x3F9A2584),
        F::raw(0x3FC260D4), F::raw(0x3FD21522), F::raw(0x3FC260D4), F::raw(0x3F9A2584),
        F::raw(0x3F527E95), F::raw(0x3EFA7631), F::raw(0x3E85DE9C),
    },
};
void pack(Pack& p, const HandSpeeds& s) { p(s.speeds); }

// the built-in driver sets, the B-spec skill, the tuning bytes, the line order and fallbacks (ai/tables, ai/bspec_skill): 0x623DC0 .. 0x624060.
struct DriverSets {
    std::array<u8, 128> sets; // +0x000 (0x623DC0) two driver sets of 0x40 bytes (+0x40 on a loose course)
    std::array<u8, 8> bspec_weight; // +0x080 (0x623E40) the B-spec skill's weights
    u8 bspec_on; // +0x088 (0x623E48) ... on
    u8 other_89; // +0x089 (0x623E49) (not read)
    std::array<s8, 6> bspec_base; // +0x08A (0x623E4A) ... its bases
    std::array<u8, 2> other_90; // +0x090 (0x623E50)
    u8 mean_slip_on; // +0x092 (0x623E52) the mean slip angle's switch
    std::array<u8, 357> other_93; // +0x093 (0x623E53) (not read)
    std::array<u8, 96> tuning; // +0x1F8 (0x623FB8) the bytes the tuning block is built from
    std::array<u8, 8> line_order; // +0x258 (0x624018) the order the lines are loaded in
    std::array<u8, 16> other_260; // +0x260 (0x624020) (not read)
    std::array<s8, 48> line_fallback; // +0x270 (0x624030) per line asked for, the six tried (-1 ends a row)
};
constexpr DriverSets kDriverSets{
    .sets = {
        130, 97, 100, 40, 100, 0, 55, 3, 25, 100, 0, 0, 0, 30, 100, 0,
        9, 0, 80, 85, 100, 150, 0, 100, 50, 63, 6, 32, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        100, 100, 100, 40, 100, 0, 60, 2, 30, 255, 10, 0, 0, 0, 100, 0,
        9, 0, 80, 85, 70, 230, 0, 190, 50, 63, 6, 32, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .bspec_weight = {8, 9, 10, 11, 12, 13, 14, 15},
    .bspec_on = 1,
    .other_89 = 0,
    .bspec_base = {90, 10, 90, 10, -126, -30},
    .other_90 = {0, 0},
    .mean_slip_on = 0,
    .other_93 = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0,
    },
    .tuning = {
        0, 0, 1, 0, 0, 0, 0, 0, 0, 15, 20, 80, 2, 0, 100, 0,
        50, 0, 30, 40, 1, 0, 20, 0, 0, 0, 0, 0, 100, 0, 0, 0,
        100, 0, 100, 100, 1, 20, 0, 0, 1, 1, 1, 1, 10, 40, 5, 0,
        1, 50, 80, 0, 1, 5, 0, 0, 1, 80, 100, 12, 4, 18, 2, 1,
        80, 100, 12, 4, 18, 2, 100, 11, 100, 110, 120, 130, 135, 140, 150, 160,
        170, 180, 190, 0, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 0,
    },
    .line_order = {6, 0, 1, 2, 5, 3, 4, 7},
    .other_260 = {6, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
    .line_fallback = {
        -1, -1, -1, -1, 0, 0, 1, 6, 2, -1, 0, 0, 2, 6, 1, -1,
        0, 0, 3, -1, -1, -1, 0, 0, 4, -1, -1, -1, 0, 0, -1, -1,
        -1, -1, 0, 0, 6, 1, 2, -1, 0, 0, 7, 6, -1, -1, 0, 0,
    },
};
void pack(Pack& p, const DriverSets& s) { p(s.sets)(s.bspec_weight)(s.bspec_on)(s.other_89)(s.bspec_base)(s.other_90)(s.mean_slip_on)(s.other_93)(s.tuning)(s.line_order)(s.other_260)(s.line_fallback); }

// the demonstration cipher: 256 factors and a byte permutation (demonstration.cpp): 0x655888 .. 0x655D90.
struct Cipher {
    std::array<u32, 256> factors; // +0x000 (0x655888)
    std::array<u8, 8> other_400; // +0x400 (0x655C88) (not read)
    std::array<u8, 256> permutation; // +0x408 (0x655C90) the bits of each byte reversed
};
constexpr Cipher kCipher{
    .factors = {
        0x3F804010, 0x3F80C030, 0x3F814050, 0x3F81C070,
        0x3F824090, 0x3F82C0B0, 0x3F8340D0, 0x3F83C0F0,
        0x3F844110, 0x3F84C130, 0x3F854150, 0x3F85C170,
        0x3F864190, 0x3F86C1B0, 0x3F8741D0, 0x3F87C1F0,
        0x3F884210, 0x3F88C230, 0x3F894250, 0x3F89C270,
        0x3F8A4290, 0x3F8AC2B0, 0x3F8B42D0, 0x3F8BC2F0,
        0x3F8C4310, 0x3F8CC330, 0x3F8D4350, 0x3F8DC370,
        0x3F8E4390, 0x3F8EC3B0, 0x3F8F43D0, 0x3F8FC3F0,
        0x3F904411, 0x3F90C431, 0x3F914451, 0x3F91C471,
        0x3F924491, 0x3F92C4B1, 0x3F9344D1, 0x3F93C4F1,
        0x3F944511, 0x3F94C531, 0x3F954551, 0x3F95C571,
        0x3F964591, 0x3F96C5B1, 0x3F9745D1, 0x3F97C5F1,
        0x3F984611, 0x3F98C631, 0x3F994651, 0x3F99C671,
        0x3F9A4691, 0x3F9AC6B1, 0x3F9B46D1, 0x3F9BC6F1,
        0x3F9C4711, 0x3F9CC731, 0x3F9D4751, 0x3F9DC771,
        0x3F9E4791, 0x3F9EC7B1, 0x3F9F47D1, 0x3F9FC7F1,
        0x3FA04812, 0x3FA0C832, 0x3FA14852, 0x3FA1C872,
        0x3FA24892, 0x3FA2C8B2, 0x3FA348D2, 0x3FA3C8F2,
        0x3FA44912, 0x3FA4C932, 0x3FA54952, 0x3FA5C972,
        0x3FA64992, 0x3FA6C9B2, 0x3FA749D2, 0x3FA7C9F2,
        0x3FA84A12, 0x3FA8CA32, 0x3FA94A52, 0x3FA9CA72,
        0x3FAA4A92, 0x3FAACAB2, 0x3FAB4AD2, 0x3FABCAF2,
        0x3FAC4B12, 0x3FACCB32, 0x3FAD4B52, 0x3FADCB72,
        0x3FAE4B92, 0x3FAECBB2, 0x3FAF4BD2, 0x3FAFCBF2,
        0x3FB04C13, 0x3FB0CC33, 0x3FB14C53, 0x3FB1CC73,
        0x3FB24C93, 0x3FB2CCB3, 0x3FB34CD3, 0x3FB3CCF3,
        0x3FB44D13, 0x3FB4CD33, 0x3FB54D53, 0x3FB5CD73,
        0x3FB64D93, 0x3FB6CDB3, 0x3FB74DD3, 0x3FB7CDF3,
        0x3FB84E13, 0x3FB8CE33, 0x3FB94E53, 0x3FB9CE73,
        0x3FBA4E93, 0x3FBACEB3, 0x3FBB4ED3, 0x3FBBCEF3,
        0x3FBC4F13, 0x3FBCCF33, 0x3FBD4F53, 0x3FBDCF73,
        0x3FBE4F93, 0x3FBECFB3, 0x3FBF4FD3, 0x3FBFCFF3,
        0x3FC05014, 0x3FC0D034, 0x3FC15054, 0x3FC1D074,
        0x3FC25094, 0x3FC2D0B4, 0x3FC350D4, 0x3FC3D0F4,
        0x3FC45114, 0x3FC4D134, 0x3FC55154, 0x3FC5D174,
        0x3FC65194, 0x3FC6D1B4, 0x3FC751D4, 0x3FC7D1F4,
        0x3FC85214, 0x3FC8D234, 0x3FC95254, 0x3FC9D274,
        0x3FCA5294, 0x3FCAD2B4, 0x3FCB52D4, 0x3FCBD2F4,
        0x3FCC5314, 0x3FCCD334, 0x3FCD5354, 0x3FCDD374,
        0x3FCE5394, 0x3FCED3B4, 0x3FCF53D4, 0x3FCFD3F4,
        0x3FD05415, 0x3FD0D435, 0x3FD15455, 0x3FD1D475,
        0x3FD25495, 0x3FD2D4B5, 0x3FD354D5, 0x3FD3D4F5,
        0x3FD45515, 0x3FD4D535, 0x3FD55555, 0x3FD5D575,
        0x3FD65595, 0x3FD6D5B5, 0x3FD755D5, 0x3FD7D5F5,
        0x3FD85615, 0x3FD8D635, 0x3FD95655, 0x3FD9D675,
        0x3FDA5695, 0x3FDAD6B5, 0x3FDB56D5, 0x3FDBD6F5,
        0x3FDC5715, 0x3FDCD735, 0x3FDD5755, 0x3FDDD775,
        0x3FDE5795, 0x3FDED7B5, 0x3FDF57D5, 0x3FDFD7F5,
        0x3FE05816, 0x3FE0D836, 0x3FE15856, 0x3FE1D876,
        0x3FE25896, 0x3FE2D8B6, 0x3FE358D6, 0x3FE3D8F6,
        0x3FE45916, 0x3FE4D936, 0x3FE55956, 0x3FE5D976,
        0x3FE65996, 0x3FE6D9B6, 0x3FE759D6, 0x3FE7D9F6,
        0x3FE85A16, 0x3FE8DA36, 0x3FE95A56, 0x3FE9DA76,
        0x3FEA5A96, 0x3FEADAB6, 0x3FEB5AD6, 0x3FEBDAF6,
        0x3FEC5B16, 0x3FECDB36, 0x3FED5B56, 0x3FEDDB76,
        0x3FEE5B96, 0x3FEEDBB6, 0x3FEF5BD6, 0x3FEFDBF6,
        0x3FF05C17, 0x3FF0DC37, 0x3FF15C57, 0x3FF1DC77,
        0x3FF25C97, 0x3FF2DCB7, 0x3FF35CD7, 0x3FF3DCF7,
        0x3FF45D17, 0x3FF4DD37, 0x3FF55D57, 0x3FF5DD77,
        0x3FF65D97, 0x3FF6DDB7, 0x3FF75DD7, 0x3FF7DDF7,
        0x3FF85E17, 0x3FF8DE37, 0x3FF95E57, 0x3FF9DE77,
        0x3FFA5E97, 0x3FFADEB7, 0x3FFB5ED7, 0x3FFBDEF7,
        0x3FFC5F17, 0x3FFCDF37, 0x3FFD5F57, 0x3FFDDF77,
        0x3FFE5F97, 0x3FFEDFB7, 0x3FFF5FD7, 0x3FFFDFF7,
    },
    .other_400 = {200, 200, 108, 0, 0, 0, 0, 0},
    .permutation = {
        0, 128, 64, 192, 32, 160, 96, 224, 16, 144, 80, 208, 48, 176, 112, 240,
        8, 136, 72, 200, 40, 168, 104, 232, 24, 152, 88, 216, 56, 184, 120, 248,
        4, 132, 68, 196, 36, 164, 100, 228, 20, 148, 84, 212, 52, 180, 116, 244,
        12, 140, 76, 204, 44, 172, 108, 236, 28, 156, 92, 220, 60, 188, 124, 252,
        2, 130, 66, 194, 34, 162, 98, 226, 18, 146, 82, 210, 50, 178, 114, 242,
        10, 138, 74, 202, 42, 170, 106, 234, 26, 154, 90, 218, 58, 186, 122, 250,
        6, 134, 70, 198, 38, 166, 102, 230, 22, 150, 86, 214, 54, 182, 118, 246,
        14, 142, 78, 206, 46, 174, 110, 238, 30, 158, 94, 222, 62, 190, 126, 254,
        1, 129, 65, 193, 33, 161, 97, 225, 17, 145, 81, 209, 49, 177, 113, 241,
        9, 137, 73, 201, 41, 169, 105, 233, 25, 153, 89, 217, 57, 185, 121, 249,
        5, 133, 69, 197, 37, 165, 101, 229, 21, 149, 85, 213, 53, 181, 117, 245,
        13, 141, 77, 205, 45, 173, 109, 237, 29, 157, 93, 221, 61, 189, 125, 253,
        3, 131, 67, 195, 35, 163, 99, 227, 19, 147, 83, 211, 51, 179, 115, 243,
        11, 139, 75, 203, 43, 171, 107, 235, 27, 155, 91, 219, 59, 187, 123, 251,
        7, 135, 71, 199, 39, 167, 103, 231, 23, 151, 87, 215, 55, 183, 119, 247,
        15, 143, 79, 207, 47, 175, 111, 239, 31, 159, 95, 223, 63, 191, 127, 255,
    },
};
void pack(Pack& p, const Cipher& s) { p(s.factors)(s.other_400)(s.permutation); }

// the in-car view's heading follower (rules_feel): 0x69F4C8 .. 0x69F4D4.
struct ViewFeel {
    F heading_gain; // +0x000 (0x69F4C8)
    std::array<u8, 4> other_4; // +0x004 (0x69F4CC) (not read)
    F heading_damp; // +0x008 (0x69F4D0)
};
constexpr ViewFeel kViewFeel{
    .heading_gain = F::raw(0x42F00000),
    .other_4 = {0, 0, 200, 66},
    .heading_damp = F::raw(0x41700000),
};
void pack(Pack& p, const ViewFeel& s) { p(s.heading_gain)(s.other_4)(s.heading_damp); }

// the spec's default steering lock curve (x 6 bytes, y 6 bytes at +8; car_spec): 0x6A4028 .. 0x6A4038.
struct SteerCurve {
    std::array<u8, 6> x; // +0x000 (0x6A4028)
    std::array<u8, 2> pad_6; // +0x006 (0x6A402E)
    std::array<u8, 6> y; // +0x008 (0x6A4030)
    std::array<u8, 2> pad_e; // +0x00E (0x6A4036)
};
constexpr SteerCurve kSteerCurve{
    .x = {0, 32, 76, 112, 166, 255},
    .pad_6 = {0, 0},
    .y = {255, 202, 138, 94, 62, 47},
    .pad_e = {0, 0},
};
void pack(Pack& p, const SteerCurve& s) { p(s.x)(s.pad_6)(s.y)(s.pad_e); }

// the screen's figures MOption's monitor_size copies (game_natives apply_monitor, 0x436D60): 4:3 at 0x6A6188, 16:9 (wide_mode)
// at 0x6A61B8, "subaru" at 0x6A61E8, 0x2C bytes each: 0x6A6188 .. 0x6A6214.
struct GameNatives {
    std::array<u32, 11> words;      // +0x000 (0x6A6188) 4:3
    std::array<u32, 1> pad_2c;      // +0x02C (0x6A61B4)
    std::array<u32, 11> wide;       // +0x030 (0x6A61B8) 16:9
    std::array<u32, 1> pad_5c;      // +0x05C (0x6A61E4)
    std::array<u32, 11> subaru;     // +0x060 (0x6A61E8) "subaru"
};
constexpr GameNatives kGameNatives{
    .words = {
        0x0, 0x3F800000, 0x436E2831, 0x43898000,
        0x434D0000, 0x3F7851EB, 0x3F800000, 0x0,
        0x0, 0x0, 0x0,
    },
    .pad_2c = {0x0},
    .wide = {
        0x0, 0x3F800000, 0x438A05E3, 0x43D48000,
        0x43700000, 0x3F7AE147, 0x3F800000, 0x0,
        0x0, 0x0, 0x0,
    },
    .pad_5c = {0x0},
    .subaru = {
        0x1, 0x3EFBC371, 0x4482A000, 0x43A8F333,
        0x43872666, 0x3F676C8B, 0x3F800000, 0x4174CCCC,
        0x4174CCCC, 0x41ADEB84, 0x42ADEB84,
    },
};
void pack(Pack& p, const GameNatives& s) { p(s.words)(s.pad_2c)(s.wide)(s.pad_5c)(s.subaru); }

// the used-car tables: thousands of km by model year, the price's factor by thousands of km (game_car): 0x6A6910 .. 0x6A69D0.
struct UsedCars {
    std::array<u8, 120> meters; // +0x000 (0x6A6910)
    std::array<u8, 72> prices; // +0x078 (0x6A6988)
};
constexpr UsedCars kUsedCars{
    .meters = {
        60, 0, 0, 0, 100, 0, 0, 0, 55, 0, 0, 0, 95, 0, 0, 0,
        50, 0, 0, 0, 90, 0, 0, 0, 50, 0, 0, 0, 85, 0, 0, 0,
        45, 0, 0, 0, 80, 0, 0, 0, 45, 0, 0, 0, 75, 0, 0, 0,
        40, 0, 0, 0, 70, 0, 0, 0, 35, 0, 0, 0, 65, 0, 0, 0,
        30, 0, 0, 0, 60, 0, 0, 0, 25, 0, 0, 0, 55, 0, 0, 0,
        20, 0, 0, 0, 50, 0, 0, 0, 15, 0, 0, 0, 40, 0, 0, 0,
        15, 0, 0, 0, 30, 0, 0, 0, 10, 0, 0, 0, 20, 0, 0, 0,
        3, 0, 0, 0, 10, 0, 0, 0,
    },
    .prices = {
        1, 0, 0, 0, 153, 153, 89, 63, 3, 0, 0, 0, 204, 204, 76, 63,
        5, 0, 0, 0, 20, 174, 71, 63, 10, 0, 0, 0, 184, 30, 69, 63,
        15, 0, 0, 0, 92, 143, 66, 63, 25, 0, 0, 0, 0, 0, 64, 63,
        40, 0, 0, 0, 102, 102, 38, 63, 60, 0, 0, 0, 0, 0, 0, 63,
        100, 0, 0, 0, 51, 51, 179, 62,
    },
};
void pack(Pack& p, const UsedCars& s) { p(s.meters)(s.prices); }

// a championship's points by place (game_internal): 0x6A6EF0 .. 0x6A6EF8.
struct ChampionshipPoints {
    std::array<u8, 8> points; // +0x000 (0x6A6EF0)
};
constexpr ChampionshipPoints kChampionshipPoints{
    .points = {10, 6, 4, 3, 2, 1, 0, 0},
};
void pack(Pack& p, const ChampionshipPoints& s) { p(s.points); }

// ---- the list ----
#define GT4R_PROGRAM_TABLES(X) \
    X(RaceGlobals, "the race globals", 0x620218, 0x108) \
    X(CarGlobals, "the car globals", 0x6203C8, 0x4E8) \
    X(StaticArea, "the physics block area", 0x620AD8, 0x360) \
    X(Apertures, "the photo shoot's apertures", 0x620E70, 0x40) \
    X(DriverArea, "the computer drivers' pace, the TCS classes, the stepless rates, the controller settings, the speed part's damping, the aerodynamics' globals and the converter's kinds", 0x622080, 0x2B0) \
    X(MaterialArea, "tire curve B's default, the soft materials, the rally set-up, the ASCC switches, the slide effects' bytes and the cue bytes", 0x622420, 0x2A0) \
    X(HandSpeeds, "the drawn driver's hands' speeds", 0x622A20, 0x2C) \
    X(DriverSets, "the built-in driver sets, the B-spec skill, the tuning bytes, the line order and fallbacks", 0x623DC0, 0x2A0) \
    X(Cipher, "the demonstration cipher", 0x655888, 0x508) \
    X(ViewFeel, "the in-car view's heading follower", 0x69F4C8, 0xC) \
    X(SteerCurve, "the spec's default steering lock curve", 0x6A4028, 0x10) \
    X(GameNatives, "the screen's figures by monitor size", 0x6A6188, 0x8C) \
    X(UsedCars, "the used-car tables", 0x6A6910, 0xC0) \
    X(ChampionshipPoints, "a championship's points by place", 0x6A6EF0, 0x8)
