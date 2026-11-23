#include "gt4r/menu/faces_graph.h"

#include <algorithm>
#include <cstdlib>

namespace gt4r::menu {

GraphFaceSettings graph_face_settings(const Widget& w) {
    GraphFaceSettings s;
    // `type` as the scripts name it ("line", "break_line"); the layout's `graph_type` 0 / 1.
    if (w.has("type")) s.strip = w.text("type") == "line" || w.text("type") == "0";
    else if (w.has("graph_type")) s.strip = w.number("graph_type") == 0;
    if (w.has("line_width")) s.line_width = w.number("line_width");
    const std::vector<Rgba> colors = w.colors("color");
    if (!colors.empty()) s.color = colors[0];
    if (const Property* p = w.find("points"))
        for (size_t i = 0; i + 1 < p->values.size() && s.points.size() < 256; i += 2)
            s.points.push_back({std::strtof(p->values[i].c_str(), nullptr), std::strtof(p->values[i + 1].c_str(), nullptr)});
    return s;
}

std::vector<std::pair<GraphPoint, GraphPoint>> graph_segments(const GraphFaceSettings& s) {
    std::vector<std::pair<GraphPoint, GraphPoint>> out;
    const size_t n = s.points.size();
    if (s.strip)
        for (size_t i = 0; i + 1 < n; ++i) out.push_back({s.points[i], s.points[i + 1]});
    else
        for (size_t i = 0; i + 1 < n; i += 2) out.push_back({s.points[i], s.points[i + 1]});
    return out;
}

EngineCurve engine_curve(const sim::Spec& s) {
    // The engine's points as the spec has them (spec_figures: speeds +0xC0, torques +0xD8 and the
    // output factors); each curve over its own peak (the scripts call the builder without ranges).
    EngineCurve c;
    const int n = s.b(0xBF) <= 24 ? s.b(0xBF) : 24;
    if (n < 2) return c;
    const float out = (s.h(0x108) ? float(s.h(0x108)) : 1000.0f) * 0.001f * (s.b(0x07) ? float(s.b(0x07)) * 0.01f : 1.0f);
    const float lo = s.b(0x10C) ? float(s.b(0x10C)) * 0.01f : 1.0f, hi = s.h(0x10A) ? float(s.h(0x10A)) * 0.001f : 1.0f;
    const float w0 = float(s.b(0xC0)), w1 = float(s.b(0xC0 + u32(n) - 1));
    std::vector<float> rpm, ps, kgfm;
    for (int i = 0; i < n; ++i) {
        const float w = float(s.b(0xC0 + u32(i)));
        const float t = w1 > w0 ? (w - w0) / (w1 - w0) : 0;
        const float k = float(s.h(0xD8 + 2 * u32(i))) * 0.01f * out * (lo + (hi - lo) * t);
        rpm.push_back(w * 100.0f), kgfm.push_back(k), ps.push_back(k * w * 100.0f / 716.2f);
    }
    c.max_ps = *std::max_element(ps.begin(), ps.end());
    c.max_kgfm = *std::max_element(kgfm.begin(), kgfm.end());
    c.min_rpm = int(rpm.front()), c.max_rpm = int(rpm.back());
    const float span = std::max(rpm.back() - rpm.front(), 1.0f);
    for (int i = 0; i < n; ++i) {
        const float x = (rpm[size_t(i)] - rpm.front()) / span;
        c.power.push_back({x, c.max_ps > 0 ? ps[size_t(i)] / c.max_ps : 0});
        c.torque.push_back({x, c.max_kgfm > 0 ? kgfm[size_t(i)] / c.max_kgfm : 0});
    }
    return c;
}

script::Value engine_curve_value(const EngineCurve& c) {
    auto points = [](const std::vector<GraphPoint>& ps) {
        std::vector<script::Value> out;
        for (const GraphPoint& p : ps) out.push_back(script::Value::array({script::Value::number(p.x), script::Value::number(p.y)}));
        return out.empty() ? script::Value{} : script::Value::array(std::move(out));
    };
    return script::Value::array({points(c.power), points(c.torque), script::Value::number(c.max_ps), script::Value::number(c.max_kgfm),
                                 script::Value::number(float(c.min_rpm)), script::Value::number(float(c.max_rpm))});
}

} // namespace gt4r::menu
