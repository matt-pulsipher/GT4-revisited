#pragma once

// The Analyzer (the project `logger`): MLoggerControl (0x11F1E0), its analysis of two laps (0x122FA8), the face's pad and
// readouts (mLoggerFace, 0x1206A0) and what its picture shows (0x120CF0). docs/formats/logger.md.

#include "gt4r/base/types.h"
#include "gt4r/menu/native.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::menu {

class Runtime;
struct LiveWidget;
enum class Key;

// A picture of a lap run again: the car's distance along the course (its distance in the lap with the laps before it, the
// sampler's 0x343B48 / +0x5A8), speed (m/s, +0x6C8), throttle and brake (0 .. 1, 0x344698 / 0x3446A0) and gear (+0x624).
struct LoggerPicture {
    float distance = 0, speed = 0, throttle = 0, brake = 0;
    int gear = 0;
};

// A track's lap run again picture by picture, as the race gives it.
struct LoggerLap {
    size_t frames = 0; // its length in pictures (the progress)
    std::function<bool(LoggerPicture&)> next; // the next picture; false at the lap's end
};

// A track as the pages show it before the analysis.
struct LoggerTrackInfo {
    bool has = false;  // a lap there (a recorder's +0x1D0 0)
    std::string car;   // the car's label (getCarName)
    int power = -1;    // its power, PS (getPower: -1 "----")
    s64 ms = -1;       // the lap's time (+0x290)
};

// What the race hands the Analyzer before it runs the project `logger` (the sequence's side, 0x10FEE8).
struct LoggerSource {
    std::array<LoggerTrackInfo, 2> info;                    // Track A (0), Track B (1)
    std::function<std::optional<LoggerLap>(int track)> lap; // the track's lap to run again
    float course_length = 0;          // 0x395400
    std::string course_label;         // 0x120020
    std::string race_mode_label;      // 0x120100
    int race_mode_subnumber = -1;     // 0x1201F0
    int load_track = -1;              // set by the page: the track a card file is to be loaded into (0x618714)
};

// A sample (0x11E450): the speed x 40 in bits 3 .. 15 and the gear in 0 .. 2, the throttle and brake x 255.
struct LoggerSample {
    u16 speed_gear = 0;
    u8 throttle = 0, brake = 0;
    float speed() const { return float(speed_gear >> 3) / 40.0f; } // m/s (0x11DBE8)
};

// The control's state (MLoggerControl), the fields by the program's offsets.
struct Logger {
    static constexpr size_t kSamples = 0x4000;
    std::shared_ptr<LoggerSource> source;
    bool metric = true;            // +0x2B8
    float grid = 25.0f;            // +0x2BC: the distance grid's step (imperial 1/64 mile)
    float line_factor = 1.0f;      // +0x2C0
    float middle = 0;              // +0x2CC: the metres at the face's middle
    float scroll = 0;              // +0x2D0
    float scale_m = 1;             // +0x2E4: pixels a metre
    float scale_v = 1;             // +0x2E8: the speed graph's scale (km/h a pixel)
    float width = 640, height = 274; // +0x2EC, +0x2F0: the face's
    float start = 0, end = 0;      // +0x2F8, +0x2FC
    float length = 0;              // +0x300
    float speed_top = 800;         // +0x308 (and +0x30C, whole)
    int edge[2] = {0, 0};          // +0x2C4, +0x2C8: the ends' stops
    float spacing = 0.5f;          // the block's +0x40008
    std::array<std::vector<LoggerSample>, 2> track;
    std::array<s64, 2> ms{-1, -1};
    std::array<bool, 2> has{false, false}; // a lap in the track (a recorder's +0x1D0 0)
    std::array<std::string, 2> car;
    bool running = false, analyzed = false; // +0x2A4, +0x2A8
    size_t progress_at = 0, progress_of = 0; // +0x2AC, +0x2B0
    std::shared_ptr<LiveWidget> time_face[2], speed_face[2], map_face;
    std::array<u8, 5>* display = nullptr; // the save's switches (logger_display)

    // The analysis, a share of it each picture: true when it ended this call.
    bool analyze_step(double budget_seconds);
    void analyze_start();
    void analyze_stop();
    // The face's pad (0x1206A0): sticks as the program's pad reads them (index 0 .. 3: right x, right y, left x, left y).
    void input(const float sticks[4]);
    float graph_height() const; // 0x1233E0
    float speed_at(int t, float metres) const; // m/s at metres from the start (0x11DBE8)
    float view_left() const { return middle - width * 0.5f / scale_m; }
    float view_right() const { return view_left() + width / scale_m; }
    // A track shown (not empty, its display switch on); the pedal bands shown.
    bool shown(int t) const { return has[size_t(t)] && display && (*display)[size_t(t)] != 0; }
    bool pedals() const { return (shown(0) || shown(1)) && display && ((*display)[3] || (*display)[4]); }

private:
    struct Sampler {
        std::vector<LoggerSample>* out = nullptr;
        float spacing = 0.5f, inv = 2, length = 0;
        bool first = true;
        float start = 0, far = 0, prev_d = 0, prev_v = 0, prev_t = 0, prev_b = 0;
        float sum_v = 0, sum_t = 0, sum_b = 0, top = 0;
        void sample(const LoggerPicture& p);
    };
    std::array<std::optional<LoggerLap>, 2> laps_;
    std::array<Sampler, 2> samplers_;
    std::array<bool, 2> done_{true, true};
    std::array<size_t, 2> at_{0, 0};
    void finish();
};

// The script class MLoggerControl, and the face's step and keys (widget_classes.cpp calls them).
void install_logger_natives(Runtime& rt, script::Module& menu);
void step_logger_face(Runtime& rt, LiveWidget& face);
bool logger_face_key(Runtime& rt, LiveWidget& face, Key k);
// The control a face shows (its `control`), or none.
Logger* logger_of_face(const LiveWidget& face);
// The display switches (the save's block 0x39F1C: Track A, Track B, speed, accel, brake).
std::array<u8, 5>& logger_display(Runtime& rt);

// The texts the program writes.
std::string logger_time_text(s64 ms); // 0x42E478: M'SS.mmm
std::string logger_tenths(int v);     // "%.1t": v / 10 with one decimal

} // namespace gt4r::menu
