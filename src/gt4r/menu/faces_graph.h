#pragma once

// The graph faces (mGraphFace: the settings pages' power and torque curves, the gear chart):
// their settings, and the points MCarData.buildEngineCurveData gives them (0x148B60).
// docs/formats/menu.md "Graphs".

#include "gt4r/menu/layout.h"
#include "gt4r/script/vm.h"
#include "gt4r/sim/car_spec.h"

#include <vector>

namespace gt4r::menu {

struct GraphPoint {
    float x = 0, y = 0;
};

struct GraphFaceSettings {
    bool strip = false;     // `type` "line": one segment from each point to the next; "break_line"
                            // (the default): separate pairs
    float line_width = 2.0f; // in the page's units after the face's transforms
    std::vector<GraphPoint> points; // fractions of the face's rectangle (y up with the layouts' scale {1 -1})
    Rgba color{255, 255, 255, 255};
};
// Throws nothing: a face without points has none.
GraphFaceSettings graph_face_settings(const Widget& face);

// The segments a graph draws, as pairs of points (0x218548 / 0x218740).
std::vector<std::pair<GraphPoint, GraphPoint>> graph_segments(const GraphFaceSettings& s);

// An engine's curves for the graphs: x the engine speed from the first point's to the last's, y
// the power (or torque) of the curve's own peak; the peaks in PS and kgf·m, the speeds in rpm.
struct EngineCurve {
    std::vector<GraphPoint> power, torque;
    float max_ps = 0, max_kgfm = 0;
    int min_rpm = 0, max_rpm = 0;
};
EngineCurve engine_curve(const sim::Spec& spec);
// As the scripts get it: [power points, torque points, peak PS, peak kgf·m, lowest rpm, highest
// rpm], a point an array [x, y].
script::Value engine_curve_value(const EngineCurve& c);

} // namespace gt4r::menu
