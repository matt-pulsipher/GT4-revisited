// Gran Turismo 4 Prologue's program tables as values (program_tables_prologue.h), SCPS-15055 v1.01's, included by
// program_tables_prologue.cpp. Each area a struct whose fields pack back into its bytes in order (+offset, address).

// the demonstrations' delta bases: 0x5031D8 .. 0x5031F8.
struct DemoDeltas {
    std::array<u32, 8> delta_bases; // +0x000 (0x5031D8) a demonstration's delta bases (demonstration_prologue.cpp)
};
constexpr DemoDeltas kDemoDeltas{
    .delta_bases = {
        0x0, 0x2, 0xA, 0x2A, 0xAA, 0x2AA, 0xAAA, 0x2AAA,
    },
};
void pack(Pack& p, const DemoDeltas& s) { p(s.delta_bases); }

// the static physics block (Gran Turismo 3's layout): 0x503208 .. 0x503328.
struct StaticBlock {
    std::array<u8, 288> bytes; // +0x000 (0x503208) the block (PrologueProgram::kStatic; rules_prologue.h Tables, the converter, the drivers' +0xDB .. +0xDD)
};
constexpr StaticBlock kStaticBlock{
    .bytes = {
        12, 6, 12, 65, 5, 5, 20, 50, 30, 35, 0, 8, 0, 2, 10, 29,
        58, 100, 154, 200, 0, 0, 10, 15, 32, 72, 144, 255, 0, 10, 10, 24,
        20, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 30, 0, 10, 0,
        20, 0, 232, 3, 30, 0, 20, 0, 10, 0, 10, 0, 10, 0, 10, 0,
        10, 0, 10, 0, 10, 0, 10, 0, 10, 0, 10, 0, 5, 5, 25, 5,
        50, 5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 0, 0, 1, 1,
        1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 7, 20, 100, 5,
        0, 0, 0, 0, 20, 20, 1, 1, 50, 50, 100, 100, 20, 60, 100, 110,
        110, 110, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        30, 30, 50, 10, 20, 20, 50, 10, 30, 0, 0, 0, 0, 7, 5, 30,
        60, 0, 0, 0, 0, 15, 100, 0, 0, 1, 0, 15, 15, 20, 20, 50,
        50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 30, 30, 0, 0, 30, 30, 0, 0, 1, 1, 0, 0, 0, 1, 1,
        0, 0, 100, 0, 50, 50, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 64, 254, 86, 0, 88, 254, 86, 0,
        112, 254, 86, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 128, 63, 0, 0, 0, 0, 0, 0, 0, 0,
    },
};
void pack(Pack& p, const StaticBlock& s) { p(s.bytes); }

// the special kinds 7 and 8's ride bytes: 0x503CB0 .. 0x503CB8.
struct SpecialRide {
    std::array<u8, 8> ride; // +0x000 (0x503CB0) the ride 0x3B15E8 reads (rules_prologue suspension)
};
constexpr SpecialRide kSpecialRide{
    .ride = {
        1, 25, 50, 75, 75, 15, 125, 150,
    },
};
void pack(Pack& p, const SpecialRide& s) { p(s.ride); }

// the spec's tire curve B default, the soft materials and the machine test's sectors: 0x503E18 .. 0x503E78.
struct CurveArea {
    std::array<u8, 16> tire_curve_b; // +0x000 (0x503E18) the spec's tire curve B default (x 8 bytes, y 8: convert_prologue)
    std::array<u32, 16> soft; // +0x010 (0x503E28) per material: 1 a soft one (0x3B4738; the ground counts)
    std::array<u8, 16> test_sectors; // +0x050 (0x503E68) the machine test's sectors (0x3B511C)
};
constexpr CurveArea kCurveArea{
    .tire_curve_b = {
        0, 31, 62, 105, 165, 255, 0, 0, 0, 28, 61, 114, 180, 196, 0, 0,
    },
    .soft = {
        0x0, 0x0, 0x1, 0x1, 0x1, 0x0, 0x1, 0x0,
        0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    },
    .test_sectors = {
        0, 0, 200, 67, 0, 0, 122, 68, 0, 232, 213, 69, 0, 0, 128, 191,
    },
};
void pack(Pack& p, const CurveArea& s) { p(s.tire_curve_b)(s.soft)(s.test_sectors); }

// the tire effect parameters: 0x503EA0 .. 0x503EF8.
struct TireEffects {
    std::array<u8, 88> bytes; // +0x000 (0x503EA0) the tire effects' parameters (0x3B98A8)
};
constexpr TireEffects kTireEffects{
    .bytes = {
        30, 24, 90, 114, 60, 0, 100, 0, 50, 20, 20, 30, 30, 0, 100, 60,
        60, 26, 15, 30, 10, 0, 100, 60, 60, 27, 15, 30, 10, 0, 100, 60,
        70, 30, 15, 30, 10, 0, 100, 40, 100, 32, 126, 246, 4, 51, 100, 6,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
    },
};
void pack(Pack& p, const TireEffects& s) { p(s.bytes); }

// the steering force's bytes: 0x503F20 .. 0x503F28.
struct SteerForceBytes {
    std::array<u8, 8> bytes; // +0x000 (0x503F20) the steering force's table bytes 0x503F20 .. 0x503F26 (0x3BC350)
};
constexpr SteerForceBytes kSteerForceBytes{
    .bytes = {
        100, 0, 0, 128, 255, 32, 1, 0,
    },
};
void pack(Pack& p, const SteerForceBytes& s) { p(s.bytes); }

// the wheel layouts and the special rows: 0x503FA8 .. 0x504260.
struct WheelArea {
    std::array<u8, 336> wheel_layouts; // +0x000 (0x503FA8) the wheel layouts, 0x54 bytes a row (by the parameter block's +0x30)
    std::array<u8, 360> special_rows; // +0x150 (0x5040F8) the special table's rows, 0x78 bytes a row (the converter's kinds 3 and 4, the lean gain +0x30)
};
constexpr WheelArea kWheelArea{
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
};
void pack(Pack& p, const WheelArea& s) { p(s.wheel_layouts)(s.special_rows); }

// the camber force's reference and gain and what follows: 0x504294 .. 0x5042BC.
struct CamberArea {
    F camber_ref; // +0x000 (0x504294) the camber force's reference
    F camber_gain; // +0x004 (0x504298) ... and gain
    std::array<u8, 32> other_08; // +0x008 (0x50429C) (read by the step's tables; unnamed)
};
constexpr CamberArea kCamberArea{
    .camber_ref = F::raw(0x41F00000),
    .camber_gain = F::raw(0x3E4CCCCC),
    .other_08 = {
        0, 0, 160, 66, 0, 0, 0, 0, 204, 204, 76, 63, 0, 0, 32, 65,
        0, 0, 32, 65, 0, 0, 32, 65, 0, 0, 0, 0, 0, 0, 0, 0,
    },
};
void pack(Pack& p, const CamberArea& s) { p(s.camber_ref)(s.camber_gain)(s.other_08); }

// the flaps' constants: 0x50430C .. 0x504328.
struct Flaps {
    std::array<F, 7> constants; // +0x000 (0x50430C) the flaps' arithmetic (0x3C26F0)
};
constexpr Flaps kFlaps{
    .constants = {
        F::raw(0x40A00000), F::raw(0x40A00000), F::raw(0x3CA3D70A), F::raw(0x41A00000), F::raw(0x3D23D70A), F::raw(0x42C80000), F::raw(0x40A00000),
    },
};
void pack(Pack& p, const Flaps& s) { p(s.constants); }

// a computer's start line by its grid place: 0x504518 .. 0x504530.
struct GridLines {
    std::array<s32, 6> lines; // +0x000 (0x504518) by the grid place % 6 (drivers_prologue DriverTables::grid_line)
};
constexpr GridLines kGridLines{
    .lines = {
        6, 1, 2, 6, 2, 1,
    },
};
void pack(Pack& p, const GridLines& s) { p(s.lines); }

// the hybrid's tables and the finish camera: 0x504548 .. 0x504680.
struct HybridArea {
    std::array<u8, 300> bytes; // +0x000 (0x504548) the hybrid's box (0x504560, 0x5045B0, 0x504638, 0x504648; rules_hybrid)
    F finish_fov; // +0x12C (0x504674) the finish camera's field of view (start_camera_prologue)
    F finish_pitch; // +0x130 (0x504678) ... its pitch
    F finish_distance; // +0x134 (0x50467C) ... its distance
};
constexpr HybridArea kHybridArea{
    .bytes = {
        0, 0, 125, 67, 0, 0, 119, 67, 0, 0, 102, 67, 0, 0, 101, 67,
        0, 0, 99, 67, 0, 0, 103, 67, 6, 0, 0, 0, 72, 69, 80, 0,
        72, 69, 80, 0, 0, 0, 0, 0, 0, 0, 122, 68, 0, 0, 225, 68,
        0, 192, 15, 69, 0, 64, 103, 69, 0, 160, 140, 69, 0, 0, 150, 69,
        0, 64, 156, 69, 0, 0, 0, 0, 0, 0, 114, 67, 0, 0, 98, 67,
        0, 0, 96, 67, 0, 0, 99, 67, 0, 0, 101, 67, 0, 0, 103, 67,
        0, 0, 108, 67, 0, 0, 0, 0, 7, 0, 0, 0, 144, 69, 80, 0,
        144, 69, 80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 84, 85, 85, 64,
        84, 85, 213, 64, 84, 85, 5, 65, 113, 28, 131, 65, 226, 56, 182, 65,
        84, 85, 15, 66, 141, 227, 50, 66, 56, 142, 77, 66, 0, 0, 0, 0,
        0, 0, 32, 65, 0, 0, 200, 65, 0, 0, 52, 66, 0, 0, 76, 66,
        0, 0, 140, 66, 0, 0, 154, 66, 0, 0, 170, 66, 0, 0, 176, 66,
        0, 0, 180, 66, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 160, 192, 0, 0, 192, 192, 0, 0, 16, 193, 0, 0, 48, 193,
        0, 0, 72, 193, 0, 0, 96, 193, 0, 0, 112, 193, 0, 0, 0, 0,
        9, 0, 0, 0, 192, 69, 80, 0, 232, 69, 80, 0, 0, 0, 0, 0,
        9, 0, 0, 0, 192, 69, 80, 0, 16, 70, 80, 0, 0, 0, 0, 0,
        2, 0, 0, 0, 168, 81, 113, 0, 216, 81, 113, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 1, 0, 0, 0, 20, 0, 0, 0,
    },
    .finish_fov = F::raw(0x42820000),
    .finish_pitch = F::raw(0x41F00000),
    .finish_distance = F::raw(0x40800000),
};
void pack(Pack& p, const HybridArea& s) { p(s.bytes)(s.finish_fov)(s.finish_pitch)(s.finish_distance); }

// the input globals, the controller settings, the race start's constants and the effects block: 0x504930 .. 0x504D18.
struct Controls {
    std::array<u8, 32> input_globals; // +0x000 (0x504930) the input phase's globals (0x504930 .. 0x50493D read)
    std::array<u8, 292> controller; // +0x020 (0x504950) the controller settings (Gran Turismo 4's 0x622110 layout; rules_prologue Controls)
    F air_density; // +0x144 (0x504A74) the air's density (made by 0x3DBA58 from the static block)
    std::array<F, 4> slipstream; // +0x148 (0x504A78) the slipstream's constants
    std::array<u8, 56> control_words; // +0x158 (0x504A88) the steering wheel's slew and locks, the jog scales, the pedal steps (rules_prologue Controls)
    std::array<u8, 16> surface_class; // +0x190 (0x504AC0) per surface: 1 loose (the follower's rate class)
    std::array<u8, 8> diff_letters; // +0x1A0 (0x504AD0) the differential type letters
    u8 awd_gain; // +0x1A8 (0x504AD8) four wheel drive system 3's gain
    u8 awd_capacity; // +0x1A9 (0x504AD9) ... and capacity
    std::array<u8, 42> other_1aa; // +0x1AA (0x504ADA) (read by the step's tables; unnamed)
    u8 asm_keep; // +0x1D4 (0x504B04) an entry with the stability control off keeps its switches' bits 6 and 7
    std::array<u8, 3> other_1d5; // +0x1D5 (0x504B05) (not read)
    std::array<u8, 528> effects; // +0x1D8 (0x504B08) the effects block (the engine sound's scales +0x1FC .., the tire effects)
};
constexpr Controls kControls{
    .input_globals = {
        40, 15, 0, 0, 204, 204, 204, 62, 153, 153, 25, 62, 150, 150, 0, 0,
        0, 0, 192, 63, 0, 0, 192, 63, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .controller = {
        0, 0, 0, 0, 10, 0, 0, 0, 1, 5, 0, 0, 0, 0, 0, 0,
        1, 1, 0, 0, 10, 10, 5, 10, 1, 1, 0, 0, 150, 50, 40, 45,
        37, 37, 0, 0, 100, 100, 0, 0, 0, 0, 0, 0, 1, 0, 50, 0,
        250, 0, 0, 0, 45, 45, 90, 90, 60, 60, 120, 150, 75, 75, 120, 150,
        0, 0, 0, 0, 10, 10, 90, 90, 60, 60, 120, 150, 75, 75, 120, 150,
        0, 0, 0, 0, 90, 8, 15, 12, 20, 0, 9, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 3, 70, 0, 0, 1, 50, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 80, 0, 100, 100, 0, 0, 0, 50, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 80, 0, 100, 0, 60, 80, 100, 0,
        0, 0, 0, 0, 40, 2, 50, 50, 0, 0, 0, 50, 0, 0, 0, 0,
        0, 0, 1, 1, 0, 0, 0, 0, 90, 2, 98, 1, 60, 80, 100, 0,
        0, 0, 0, 0, 30, 0, 98, 50, 0, 20, 0, 50, 0, 0, 0, 0,
        0, 0, 1, 1, 0, 0, 0, 0, 75, 2, 98, 0, 60, 80, 100, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0,
    },
    .air_density = F::raw(0x3F4CCCCC),
    .slipstream = {
        F::raw(0x3F600000), F::raw(0x3F600000), F::raw(0x3F800000), F::raw(0x3E800000),
    },
    .control_words = {
        217, 15, 201, 63, 52, 250, 14, 62, 144, 10, 134, 62, 78, 119, 86, 62,
        193, 184, 178, 62, 0, 0, 128, 57, 0, 0, 128, 57, 204, 204, 204, 61,
        170, 170, 42, 62, 170, 170, 42, 62, 170, 170, 42, 62, 170, 170, 42, 62,
        0, 0, 128, 63, 0, 0, 128, 63,
    },
    .surface_class = {
        0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .diff_letters = {
        102, 114, 110, 109, 104, 118, 121, 116,
    },
    .awd_gain = 200,
    .awd_capacity = 0,
    .other_1aa = {
        1, 0, 0, 0, 128, 62, 25, 0, 0, 0, 153, 153, 25, 62, 15, 0,
        0, 0, 0, 0, 0, 0, 155, 165, 188, 190, 190, 200, 165, 175, 190, 192,
        190, 200, 170, 180, 192, 194, 190, 200, 1, 1,
    },
    .asm_keep = 1,
    .other_1d5 = {
        0, 0, 0,
    },
    .effects = {
        1, 4, 4, 4, 6, 82, 106, 131, 0, 152, 204, 255, 45, 76, 136, 255,
        0, 120, 255, 255, 45, 76, 136, 255, 0, 120, 255, 255, 30, 30, 30, 1,
        0, 3, 0, 0, 0, 0, 0, 0, 1, 4, 4, 4, 14, 30, 118, 127,
        0, 40, 232, 255, 12, 32, 112, 128, 0, 32, 224, 255, 12, 28, 60, 68,
        0, 52, 224, 255, 4, 3, 3, 0, 7, 1, 30, 0, 0, 0, 0, 0,
        1, 4, 4, 4, 14, 30, 118, 127, 0, 40, 232, 255, 12, 32, 112, 128,
        0, 32, 224, 255, 12, 28, 60, 68, 0, 52, 224, 255, 3, 4, 3, 0,
        0, 10, 12, 0, 0, 0, 0, 0, 1, 4, 4, 4, 14, 30, 118, 127,
        0, 40, 232, 255, 12, 32, 112, 128, 0, 32, 224, 255, 12, 28, 60, 68,
        0, 52, 224, 255, 15, 15, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 4, 4, 4, 4, 62, 110, 207, 0, 72, 132, 255, 4, 64, 156, 255,
        0, 80, 124, 151, 0, 64, 156, 255, 0, 80, 124, 155, 5, 8, 8, 0,
        75, 3, 0, 0, 0, 0, 0, 0, 1, 4, 4, 4, 10, 62, 162, 255,
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
        30, 100, 50, 35, 3, 0, 50, 100, 255, 3, 15, 50, 100, 3, 100, 10,
        1, 45, 25, 75, 5, 0, 20, 1, 32, 90, 150, 20, 13, 13, 20, 25,
        5, 1, 5, 20, 43, 10, 60, 0, 156, 222, 133, 62, 49, 118, 250, 62,
    },
};
void pack(Pack& p, const Controls& s) { p(s.input_globals)(s.controller)(s.air_density)(s.slipstream)(s.control_words)(s.surface_class)(s.diff_letters)(s.awd_gain)(s.awd_capacity)(s.other_1aa)(s.asm_keep)(s.other_1d5)(s.effects); }

// the stock tires' car labels: 0x505298 .. 0x5052A0.
struct LabelRefs {
    std::array<u32, 2> labels; // +0x000 (0x505298) the places of the two labels (fit_grip_tires; LabelText below)
};
constexpr LabelRefs kLabelRefs{
    .labels = {
        0x573A68, 0x573A80,
    },
};
void pack(Pack& p, const LabelRefs& s) { p(s.labels); }

// the computer drivers' tables: 0x506510 .. 0x506720.
struct DriverArea {
    std::array<s32, 3> nearest; // +0x000 (0x506510) the lines a car placed off the grid may start on
    std::array<u8, 148> other_0c; // +0x00C (0x50651C) (not read)
    std::array<u8, 96> builtin; // +0x0A0 (0x5065B0) the driver sets of a course without its own, by its kind (0x20 bytes each)
    std::array<u8, 168> other_100; // +0x100 (0x506610) (not read)
    std::array<u8, 4> steer_block; // +0x1A8 (0x5066B8) the drivers' switches: +2 the steering mode (0x5066BA)
    std::array<u8, 44> other_1ac; // +0x1AC (0x5066BC) (not read)
    std::array<u8, 8> order; // +0x1D8 (0x5066E8) the order the lines are prepared in
    std::array<s8, 48> fallback; // +0x1E0 (0x5066F0) per requested line the lines tried, -1 ending a row (6 a row)
};
constexpr DriverArea kDriverArea{
    .nearest = {
        6, 2, 1,
    },
    .other_0c = {
        0, 0, 0, 0, 0, 0, 64, 63, 0, 0, 0, 63, 0, 0, 128, 62,
        0, 0, 0, 0, 100, 100, 100, 24, 100, 0, 90, 15, 40, 15, 5, 10,
        0, 60, 100, 20, 9, 0, 80, 0, 0, 0, 0, 0, 100, 0, 0, 0,
        0, 0, 0, 0, 100, 100, 100, 24, 100, 0, 90, 15, 40, 15, 5, 10,
        0, 60, 100, 20, 9, 0, 80, 0, 0, 0, 0, 0, 100, 0, 0, 0,
        0, 0, 0, 0, 100, 100, 100, 24, 100, 0, 90, 15, 40, 15, 5, 10,
        0, 60, 100, 20, 9, 0, 80, 0, 0, 0, 0, 0, 100, 0, 0, 0,
        0, 0, 0, 0, 100, 100, 100, 24, 100, 0, 90, 15, 40, 15, 5, 10,
        0, 60, 100, 20, 9, 0, 80, 0, 0, 0, 0, 0, 100, 0, 0, 0,
        0, 0, 0, 0,
    },
    .builtin = {
        100, 100, 100, 24, 100, 0, 90, 15, 40, 15, 5, 10, 0, 60, 100, 20,
        9, 0, 80, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0,
        100, 100, 100, 24, 100, 0, 90, 15, 40, 15, 5, 10, 0, 60, 100, 20,
        9, 0, 80, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0,
        100, 100, 100, 24, 100, 0, 90, 30, 60, 0, 0, 10, 0, 60, 100, 20,
        9, 0, 80, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0,
    },
    .other_100 = {
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
        0, 0, 0, 0, 0, 0, 0, 0,
    },
    .steer_block = {
        0, 0, 1, 0,
    },
    .other_1ac = {
        0, 0, 0, 0, 0, 15, 20, 80, 2, 0, 100, 0, 50, 0, 30, 40,
        1, 0, 20, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    .order = {
        6, 0, 1, 2, 5, 3, 4, 7,
    },
    .fallback = {
        -1, -1, -1, -1, 0, 0, 1, 6, 2, -1, 0, 0, 2, 6, 1, -1,
        0, 0, 3, -1, -1, -1, 0, 0, 4, -1, -1, -1, 0, 0, -1, -1,
        -1, -1, 0, 0, 6, 1, 2, -1, 0, 0, 7, 6, -1, -1, 0, 0,
    },
};
void pack(Pack& p, const DriverArea& s) { p(s.nearest)(s.other_0c)(s.builtin)(s.other_100)(s.steer_block)(s.other_1ac)(s.order)(s.fallback); }

// the race display's dials and places: 0x5711C0 .. 0x5713F4.
struct HudArea {
    std::array<u8, 564> bytes; // +0x000 (0x5711C0) the HUD's tables (race_hud_prologue: the tachometer's 0x5711F8, the speedometer's 0x571358, ...)
};
constexpr HudArea kHudArea{
    .bytes = {
        54, 48, 48, 48, 0, 0, 0, 0, 55, 48, 48, 48, 0, 0, 0, 0,
        56, 48, 48, 48, 0, 0, 0, 0, 57, 48, 48, 48, 0, 0, 0, 0,
        49, 48, 48, 48, 48, 0, 0, 0, 49, 50, 48, 48, 48, 0, 0, 0,
        49, 56, 48, 48, 48, 0, 0, 0, 0, 128, 187, 69, 0, 128, 187, 69,
        0, 0, 250, 67, 192, 17, 87, 0, 0, 192, 218, 69, 0, 192, 218, 69,
        0, 0, 250, 67, 200, 17, 87, 0, 0, 0, 250, 69, 0, 0, 250, 69,
        0, 0, 250, 67, 208, 17, 87, 0, 0, 160, 12, 70, 0, 160, 12, 70,
        0, 0, 250, 67, 216, 17, 87, 0, 0, 64, 28, 70, 0, 64, 28, 70,
        0, 0, 250, 67, 224, 17, 87, 0, 0, 128, 59, 70, 0, 128, 59, 70,
        0, 0, 250, 67, 232, 17, 87, 0, 0, 160, 140, 70, 0, 160, 140, 70,
        0, 0, 250, 67, 240, 17, 87, 0, 255, 255, 255, 127, 0, 160, 140, 70,
        0, 0, 250, 67, 240, 17, 87, 0, 71, 84, 52, 95, 109, 101, 116, 101,
        114, 95, 116, 101, 120, 116, 117, 114, 101, 95, 115, 112, 114, 118, 95, 98,
        97, 115, 101, 0, 0, 0, 0, 0, 71, 84, 52, 95, 109, 101, 116, 101,
        114, 95, 116, 101, 120, 116, 117, 114, 101, 95, 114, 101, 118, 0, 0, 0,
        109, 101, 116, 101, 114, 95, 109, 105, 110, 105, 0, 0, 0, 0, 0, 0,
        50, 51, 82, 97, 99, 101, 82, 111, 117, 110, 100, 84, 97, 99, 104, 111,
        109, 101, 116, 101, 114, 66, 97, 115, 101, 0, 0, 0, 0, 0, 0, 0,
        50, 49, 82, 97, 99, 101, 79, 110, 98, 111, 97, 114, 100, 84, 97, 99,
        104, 111, 109, 101, 116, 101, 114, 0, 50, 48, 82, 97, 99, 101, 82, 101,
        109, 111, 116, 101, 84, 97, 99, 104, 111, 109, 101, 116, 101, 114, 0, 0,
        50, 48, 82, 97, 99, 101, 66, 97, 116, 116, 108, 101, 84, 97, 99, 104,
        111, 109, 101, 116, 101, 114, 0, 0, 49, 51, 82, 97, 99, 101, 77, 101,
        116, 101, 114, 66, 97, 115, 101, 0, 71, 84, 52, 95, 109, 101, 116, 101,
        114, 95, 116, 101, 120, 116, 117, 114, 101, 95, 115, 112, 101, 101, 100, 95,
        49, 54, 48, 109, 112, 104, 0, 0, 0, 0, 32, 67, 0, 0, 32, 67,
        56, 19, 87, 0, 255, 255, 255, 127, 0, 0, 32, 67, 56, 19, 87, 0,
        71, 84, 52, 95, 109, 101, 116, 101, 114, 95, 116, 101, 120, 116, 117, 114,
        101, 95, 115, 112, 101, 101, 100, 95, 49, 54, 48, 107, 109, 0, 0, 0,
        71, 84, 52, 95, 109, 101, 116, 101, 114, 95, 116, 101, 120, 116, 117, 114,
        101, 95, 115, 112, 101, 101, 100, 95, 50, 52, 48, 107, 109, 0, 0, 0,
        71, 84, 52, 95, 109, 101, 116, 101, 114, 95, 116, 101, 120, 116, 117, 114,
        101, 95, 115, 112, 101, 101, 100, 95, 51, 50, 48, 107, 109, 0, 0, 0,
        0, 0, 32, 67, 0, 0, 32, 67, 112, 19, 87, 0, 0, 0, 112, 67,
        0, 0, 112, 67, 144, 19, 87, 0, 255, 255, 255, 127, 0, 0, 160, 67,
        176, 19, 87, 0,
    },
};
void pack(Pack& p, const HudArea& s) { p(s.bytes); }

// the spec's curve defaults, surface classes and the view follower's gains: 0x571C78 .. 0x571CA4.
struct SpecDefaults {
    std::array<u8, 8> lock_curve; // +0x000 (0x571C78) the spec's lock curve default
    std::array<u8, 8> lock_curve_c; // +0x008 (0x571C80) the third steering curve's default
    std::array<u8, 16> surface_classes; // +0x010 (0x571C88) the spec's surface classes
    std::array<F, 2> view_gains; // +0x020 (0x571C98) the view follower's gains
    F view_more; // +0x028 (0x571CA0) ... and its third word
};
constexpr SpecDefaults kSpecDefaults{
    .lock_curve = {
        0, 32, 76, 112, 166, 255, 0, 0,
    },
    .lock_curve_c = {
        255, 202, 138, 94, 62, 47, 0, 0,
    },
    .surface_classes = {
        0, 0, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 2, 2, 2, 2,
    },
    .view_gains = {
        F::raw(0x41F00000), F::raw(0x42C80000),
    },
    .view_more = F::raw(0x41200000),
};
void pack(Pack& p, const SpecDefaults& s) { p(s.lock_curve)(s.lock_curve_c)(s.surface_classes)(s.view_gains)(s.view_more); }

// the stock tires' car labels' text: 0x573A68 .. 0x573A98.
struct LabelText {
    std::array<u8, 48> text; // +0x000 (0x573A68) two labels, 0x18 bytes apart
};
constexpr LabelText kLabelText{
    .text = {
        115, 108, 95, 99, 111, 112, 101, 110, 95, 97, 99, 116, 105, 118, 101, 116,
        111, 112, 95, 48, 50, 0, 0, 0, 115, 104, 95, 99, 111, 112, 101, 110,
        95, 97, 99, 116, 105, 118, 101, 116, 111, 112, 95, 48, 50, 0, 0, 0,
    },
};
void pack(Pack& p, const LabelText& s) { p(s.text); }

#define GT4R_PROLOGUE_TABLES(X) \
    X(DemoDeltas, "the demonstrations' delta bases", 0x5031D8, 0x20) \
    X(StaticBlock, "the static physics block (Gran Turismo 3's layout)", 0x503208, 0x120) \
    X(SpecialRide, "the special kinds 7 and 8's ride bytes", 0x503CB0, 0x8) \
    X(CurveArea, "the spec's tire curve B default, the soft materials and the machine test's sectors", 0x503E18, 0x60) \
    X(TireEffects, "the tire effect parameters", 0x503EA0, 0x58) \
    X(SteerForceBytes, "the steering force's bytes", 0x503F20, 0x8) \
    X(WheelArea, "the wheel layouts and the special rows", 0x503FA8, 0x2B8) \
    X(CamberArea, "the camber force's reference and gain and what follows", 0x504294, 0x28) \
    X(Flaps, "the flaps' constants", 0x50430C, 0x1C) \
    X(GridLines, "a computer's start line by its grid place", 0x504518, 0x18) \
    X(HybridArea, "the hybrid's tables and the finish camera", 0x504548, 0x138) \
    X(Controls, "the input globals, the controller settings, the race start's constants and the effects block", 0x504930, 0x3E8) \
    X(LabelRefs, "the stock tires' car labels", 0x505298, 0x8) \
    X(DriverArea, "the computer drivers' tables", 0x506510, 0x210) \
    X(HudArea, "the race display's dials and places", 0x5711C0, 0x234) \
    X(SpecDefaults, "the spec's curve defaults, surface classes and the view follower's gains", 0x571C78, 0x2C) \
    X(LabelText, "the stock tires' car labels' text", 0x573A68, 0x30)
