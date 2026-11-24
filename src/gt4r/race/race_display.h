#pragma once

// The race display's numbers: where the game's program puts each element, how it writes a lap
// time, how far the rev bar is lit, where a car is on the course map. The drawing itself is the
// app's (race_hud.h). docs/formats/race_display.md.

#include "formats/tex1.h"
#include "gt4r/base/types.h"

#include <string>
#include <vector>

namespace gt4r::race {

// An element's place on the display's 640 x 480 canvas, as a row of the program's layout tables
// gives it.
struct DisplayPlace {
    u32 member = 0;  // which element: its offset in the program's display object
    int x = 0, y = 0, w = 0, h = 0; // w, h 0: the picture's own size
    u8 anchor_x = 2, anchor_y = 2;
};

struct DisplayRect {
    float x = 0, y = 0, w = 0, h = 0;
};
// The rectangle of a place for an element w x h large (the place's own size when it has one).
// Anchor bits 0-1: 0 the left (top) edge is at x (y), 1 the right (bottom) edge, 2 the middle.
// On a wide screen (`wide`) a horizontal anchor with bit 2 set moves 96 units to the left, with
// bit 3 as well 96 to the right: the element belongs to that side of the screen.
DisplayRect place_rect(const DisplayPlace& p, float w, float h, bool wide);
// The x the anchor refers to, moved to its side on a wide screen.
float place_x(const DisplayPlace& p, bool wide);

// The elements of the single race, by their numbers in the program's tables.
namespace element {
constexpr u32 kPositionWord = 0x1668, kPosition = 0x1398, kLapWord = 0x1684, kLapCount = 0x1300, kMap = 0x1108;
constexpr u32 kLapTime = 0x15F8, kBestWord = 0x16A0, kBestTime = 0x14A8, kLastWord = 0x16BC, kLastTime = 0x1588;
constexpr u32 kEventLine = 0x17BC, kMessage = 0x252C, kReplaySign = 0x2A6C;
constexpr u32 kTotalTime = 0x1438; // display mode 1: the race's total time (under the word at kBestWord)
} // namespace element

struct DisplayLayout {
    std::vector<DisplayPlace> places;
    bool valid() const { return !places.empty(); }
    const DisplayPlace* find(u32 member) const;
};
// The single race's layout from the game's program (the unpacked CORE.GT4, `load_address` its
// first byte's address): the two tables the program applies for a race of the usual kind. They
// are read at the addresses of the release this was read on (SCUS-97328 v1.01) and checked for
// the tables' shape; empty when the program is another one.
DisplayLayout read_single_race_layout(ByteSpan program, u32 load_address);
// The layout of display mode 1 (the table 0x6A0BA0: the place, laps, map, the lap's time and the
// total time, the message lower): the B-spec race's display here.
DisplayLayout read_mode1_layout(ByteSpan program, u32 load_address);

// A total time as display mode 1 writes it: H:MM'SS.mmm.
std::string format_total_time(s64 milliseconds);

// A dial's face for a car: the first row of the program's table whose limit is at least the
// car's value (the last row takes any).
struct DialFace {
    float limit = 0;       // the largest dial maximum the face serves
    float min = 0, max = 0; // the needle's range
    std::string picture;   // "rev_8000", "speed_240km"
};
// The rev counter's faces and the speedometer's (in km/h, or with `mph` the mph table's), from
// the same program; empty when the program is another one.
std::vector<DialFace> read_rev_faces(ByteSpan program, u32 load_address);
std::vector<DialFace> read_speed_faces(ByteSpan program, u32 load_address, bool mph);
const DialFace* pick_face(const std::vector<DialFace>& faces, float value);
// A rev counter's dial maximum for a rev limit: the next whole thousand at or above limit - 100.
float rev_dial_maximum(float rev_limit);
// A needle's direction for a value: degrees clockwise on the screen from the +x axis.
float needle_angle(float value, float min, float max, float a0, float a1);

// A lap time as the display writes it: M'SS.mmm; no time (negative, or past 99 hours):
// -'--.--- with the font's dash (character 0x7F).
std::string format_lap_time(s64 milliseconds);

// The lit part of the rev bar (0..1): 40 segments, full at the red line / 0.7, so the red line
// is where the bar's red segments begin.
float rev_bar_fraction(float rpm, float red_line);

// A GS color 0xAABBGGRR with 0x80 as full, as 8-bit RGBA (a tint: 0x80 leaves a picture as it is).
struct Rgba8 {
    u8 r = 255, g = 255, b = 255, a = 255;
};
Rgba8 gs_color(u32 abgr, float opacity = 1);

// The course map (GTCM, course slot 33): a picture of the course and the numbers that put a
// place of the course on it.
struct CourseMap {
    gt3r::Tex1::Image image;
    float center_x = 0, center_z = 0; // the course's point at the picture's middle
    float scale = 1;                  // picture pixels per metre
    float rotation = 0;               // degrees
    bool valid() const { return image.width > 0 && image.height > 0; }
    // A place of the course in picture pixels from the picture's top left.
    void map(float x, float z, float& px, float& py) const;
    // Empty when the section is not a map or has no picture.
    static CourseMap parse(ByteSpan gtcm);
};

} // namespace gt4r::race
