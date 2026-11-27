#pragma once

// The Analyzer's graph (mLoggerFace's picture, 0x120CF0): docs/formats/logger.md "The face". Drawn by the running menus'
// painter in the face's place (PageDrawer::draw_live).

#include "gt4r/app/menu_page_drawer.h"
#include "gt4r/menu/logger.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace gt4r {

namespace logger_face_detail {

struct LColor {
    float r, g, b, a;
};
// The tracks' colours (0x618730 throttle, 0x618750 brake, 0x618770 speed: A, B).
inline constexpr LColor kThrottle{0.25f, 1.0f, 0.15f, 1}, kBrake{1.0f, 0.25f, 0.15f, 1};
inline constexpr LColor kSpeed[2] = {{0.809f, 0.29f, 0.295f, 1}, {0.318f, 0.489f, 0.739f, 1}};

inline menu::Property prop(const char* name, const char* type, std::vector<std::string> values) {
    menu::Property p;
    p.name = name, p.type = type, p.values = std::move(values);
    return p;
}

} // namespace logger_face_detail

// `at`: the face's rectangle in the page.
inline void draw_logger_face(PageDrawer& d, const LiveWidget& face, const Rect& at, float opacity) {
    using namespace logger_face_detail;
    const menu::Logger* L = menu::logger_of_face(face);
    if (!L) return;
    SDL_Renderer* r = d.renderer();
    const float W = at.w, H = at.h;
    const float left = L->view_left(), right = L->view_right(), ppm = L->scale_m;
    const float bottom = L->scroll, top = L->scroll + H;
    // A point of the graph (x metres, y up from the face's bottom) in the page.
    auto px = [&](float m) { return at.x + (m - left) * ppm; };
    auto py = [&](float y) { return at.y + (top - y); };
    // Clipped to the face (its buffer).
    SDL_Rect was{};
    const bool clipped = SDL_RenderClipEnabled(r);
    if (clipped) SDL_GetRenderClipRect(r, &was);
    {
        const SDL_FPoint a = d.screen_point(at.x, at.y), b = d.screen_point(at.x + at.w, at.y + at.h);
        SDL_Rect c{int(std::lround(a.x)), int(std::lround(a.y)), int(std::lround(b.x - a.x)), int(std::lround(b.y - a.y))};
        if (clipped) SDL_GetRectIntersection(&c, &was, &c);
        SDL_SetRenderClipRect(r, &c);
    }
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    auto fc = [opacity](LColor c) { return SDL_FColor{std::min(c.r, 1.0f), std::min(c.g, 1.0f), std::min(c.b, 1.0f), std::clamp(c.a * opacity, 0.0f, 1.0f)}; };
    auto flush = [&] {
        if (v.empty()) return;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_RenderGeometry(r, nullptr, v.data(), int(v.size()), idx.data(), int(idx.size()));
        v.clear(), idx.clear();
    };
    // A quad of the page, corners in order.
    auto quad = [&](SDL_FPoint p0, SDL_FPoint p1, SDL_FPoint p2, SDL_FPoint p3, SDL_FColor c0, SDL_FColor c1, SDL_FColor c2, SDL_FColor c3) {
        const int base = int(v.size());
        for (const auto& [p, c] : {std::pair{p0, c0}, std::pair{p1, c1}, std::pair{p2, c2}, std::pair{p3, c3}}) v.push_back({d.screen_point(p.x, p.y), c, {0, 0}});
        idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    // A line of the page `w` pixels of the page wide.
    auto line = [&](float x0, float y0, float x1, float y1, LColor col, float w = 1.0f) {
        const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0) return;
        const float ox = -dy / len * w * 0.5f, oy = dx / len * w * 0.5f;
        const SDL_FColor c = fc(col);
        quad({x0 + ox, y0 + oy}, {x1 + ox, y1 + oy}, {x1 - ox, y1 - oy}, {x0 - ox, y0 - oy}, c, c, c, c);
    };
    auto grey = [](float k) { return LColor{k, k, k, 1}; };
    // A text as the program's text engine puts it: at its pen (the baseline's left, 0x490890), or in a box whose top is
    // at y (0x490700; `right`: right-aligned in the box ending at x).
    auto label = [&](const std::string& s, const char* font, float scale, LColor col, float x, float y, bool right = false, float box = 128) {
        if (!right) y -= d.font_ascent(font) * scale;
        flush();
        menu::Widget w;
        w.type = "TextFace";
        char sc[16];
        std::snprintf(sc, sizeof sc, "%g", double(scale));
        auto byte = [](float c) { return std::to_string(int(std::lround(std::clamp(c, 0.0f, 1.0f) * 255))); };
        w.properties = {prop("text", "string", {s}), prop("font", "string", {font}), prop("scale_x", "digit", {sc}), prop("scale_y", "digit", {sc}),
                        prop("align", "digit", {right ? "2" : "0"}), prop("text_color", "RGBA", {byte(col.r), byte(col.g), byte(col.b), byte(col.a)})};
        const float h = d.font_line_height(font) * scale;
        d.draw_text(w, right ? Rect{at.x + x - box, at.y + y, box, h} : Rect{at.x + x, at.y + y, 0, h}, opacity);
    };
    // A grid (0x121C00): lines across every 25 of `n` units (the units' factor), white at 0, grey every 100, darker
    // between; lines up every grid step but the fourth's.
    auto grid = [&](int n, float y, float h) {
        const int i0 = int(left / L->grid) - 1, i1 = int(right / L->grid) + 1;
        for (int i = i0; i <= i1; ++i)
            if (i & 3) line(px(float(i) * L->grid), py(y), px(float(i) * L->grid), py(y + h), grey(0.25f));
        for (int k = 0; k <= n; k += 25) {
            const float yy = y + h * float(k) / float(n) * L->line_factor;
            line(at.x, py(yy), at.x + W, py(yy), grey(k % 100 ? 0.25f : k ? 0.5f : 1.0f));
        }
    };
    const bool shown[2] = {L->shown(0), L->shown(1)};
    const std::array<u8, 5>& sw = *L->display;
    const bool pedals = L->pedals();
    // The pedal bands: B's then A's above it.
    float y = pedals ? 4.0f : 0.0f, boundary = y;
    auto letter = [&](int t, float base) {
        const LColor c{kSpeed[t].r, kSpeed[t].g, kSpeed[t].b, 0.4f};
        label(t ? "B" : "A", "hb_24", 2.0f, c, 64, top - (base + 12)); // (0x122A50)
    };
    if (pedals) {
        if (shown[1]) letter(1, y), grid(100, y, 50), y = 54;
        boundary = y;
        if (shown[0]) letter(0, boundary), grid(100, boundary, 50), y = boundary + 50;
    }
    const float base = y + 10; // the speed graph's
    grid(875, base, 875.0f / L->scale_v);
    // The distance lines every 4 grid steps, their numbers under the speed graph's base (0x121340).
    {
        const float step = L->grid * 4;
        const float th = d.font_line_height("hc_20") * 0.5f;
        for (int i = int(left / step) - 1; i <= int(right / step) + 1; ++i) {
            const int m = int(float(i) * L->grid * 4);
            line(px(float(m)), py(bottom), px(float(m)), py(top), grey(0.5f));
            std::string s;
            if (L->metric) s = std::to_string(m) + "m";
            else if ((i & 15) == 0) s = std::to_string(m / 1600) + "ml";
            if (!s.empty()) label(s, "hc_20", 0.5f, grey(1), (float(m) - left) * ppm, top - (base - (th - 1)));
        }
    }
    if (pedals) {
        // The bands' boundary line and their 0% and 100% (0x121688).
        line(at.x, py(boundary), at.x + W, py(boundary), grey(1));
        const float zero = shown[0] ? (shown[1] ? boundary - 4 : boundary + 4) + 1 : boundary - 8 + 1;
        label("0%", "hc_20", 0.4f, grey(1), 16, top - zero);
        if (shown[0]) label("100%", "hc_20", 0.4f, grey(1), 16, top - (boundary + 50 - 8 - 1));
        if (shown[1]) label("100%", "hc_20", 0.4f, grey(1), 16, top - (boundary - 50 + 4 + 1));
        // Each track's throttle and brake filled from the boundary, A up and B down (0x121590, 0x1218C0): the colour at
        // the base, brighter to the top (x (1 + v x 127 / 128)), half opaque.
        for (int t = 0; t < 2; ++t) {
            if (!shown[t] || L->track[size_t(t)].empty()) continue;
            const std::vector<menu::LoggerSample>& s = L->track[size_t(t)];
            const float h = t ? -50.0f : 50.0f;
            const int last = int(s.size()) - 1;
            int i0 = std::max(int(left / L->spacing) - 1, 0), i1 = int(right / L->spacing) + 1;
            if (i1 >= last + 1) i1 = last;
            if (float(i0) * L->spacing > right || float(i1) * L->spacing < left) continue;
            int k = 1;
            while ((i1 - i0) / k > 1000) ++k;
            for (int which = 0; which < 2; ++which) {
                if (!sw[size_t(3 + which)]) continue;
                const LColor c = which ? kBrake : kThrottle;
                for (int i = i0; i + k <= i1; i += k) {
                    const float a = which ? s[size_t(i)].brake / 255.0f : s[size_t(i)].throttle / 255.0f;
                    const float b = which ? s[size_t(i + k)].brake / 255.0f : s[size_t(i + k)].throttle / 255.0f;
                    auto lit = [&](float u) {
                        const float m = 1.0f + u * 127.0f / 128.0f;
                        return fc({c.r * m, c.g * m, c.b * m, 0.5f});
                    };
                    const SDL_FColor c0 = fc({c.r, c.g, c.b, 0.5f});
                    const float x0 = px(float(i) * L->spacing), x1 = px(float(i + k) * L->spacing);
                    quad({x0, py(boundary)}, {x0, py(boundary + a * h)}, {x1, py(boundary + b * h)}, {x1, py(boundary)}, c0, lit(a), lit(b), c0);
                }
            }
        }
    }
    // The speed's labels every 100 to 800 at the right (0x122830).
    {
        const float lh = d.font_line_height("hc_20") * 0.7f;
        for (int k = 0; k <= 800; k += 100) {
            const float yy = base + float(k) / L->scale_v * L->line_factor;
            label(std::to_string(k) + (L->metric ? "km/h" : "mph"), "hc_20", 0.7f, grey(1), W - 16, top - yy - lh, true);
        }
    }
    // Each track's speed, B first, two pixels thick (0x1226C8, 0x121DD0).
    if (sw[2])
        for (int t = 1; t >= 0; --t) {
            if (!shown[t] || L->track[size_t(t)].empty()) continue;
            const std::vector<menu::LoggerSample>& s = L->track[size_t(t)];
            const int last = int(s.size()) - 1;
            int i0 = std::max(int(left / L->spacing) - 1, 0), i1 = int(right / L->spacing) + 1;
            if (i1 >= last + 1) i1 = last;
            int k = 1;
            while ((i1 - i0) / k > 1000) ++k;
            for (int i = i0; i + k <= i1; i += k) {
                const float y0 = base + s[size_t(i)].speed() * 3.6f / L->scale_v, y1 = base + s[size_t(i + k)].speed() * 3.6f / L->scale_v;
                line(px(float(i) * L->spacing) + 0.5f, py(y0) - 0.5f, px(float(i + k) * L->spacing) + 0.5f, py(y1) - 0.5f, kSpeed[t], 2.0f);
            }
        }
    // The readout at the middle (0x121FF0).
    {
        const float mid = (left + right) * 0.5f;
        const float floor_y = std::max(base, bottom);
        line(px(L->end - L->start), py(top), px(L->end - L->start), py(bottom), {0.9f, 0.5f, 0.1f, 1});
        const std::string dist = L->metric ? menu::logger_tenths(int(mid * 10)) + "m" : menu::logger_tenths(int(mid * 0.00062137f * 1000)) + "ml";
        label(dist, "hc_20", 0.7f, {0.9f, 0.9f, 0.9f, 1}, W * 0.5f + 15, top - (floor_y + 4 + 2));
        float v[2] = {0, 0};
        for (int t = 0; t < 2; ++t)
            if (L->analyzed) v[t] = L->speed_at(t, mid) * 3.6f;
        float dx[2] = {1, 1}, dy[2] = {-1, -1};
        if (shown[0] && shown[1]) {
            if (v[1] < v[0]) dy[0] = 1, dx[0] = -1;
            else dy[1] = 1;
        }
        for (int t = 0; t < 2; ++t) {
            if (!shown[t]) continue;
            float x = px(mid), yy = base + v[t] / L->scale_v;
            if (yy - 30 < bottom) dy[t] = 1;
            // The leader: 15 across and 30 up or down, then 30 across.
            const float ex = x + 15 * dx[t], ey = yy + 30 * dy[t];
            line(x, py(yy), ex, py(ey), kSpeed[t]);
            line(ex, py(ey), ex + 30 * dx[t], py(ey), kSpeed[t]);
            const int tenths = int(L->metric ? v[t] * 10 : v[t] * 0.6214f * 10);
            const std::string s = menu::logger_tenths(tenths) + (L->metric ? "km/h" : "mph");
            if (dx[t] < 0) label(s, "hc_20", 0.8f, kSpeed[t], W * 0.5f - 45, top - ey - d.font_ascent("hc_20") * 0.8f, true);
            else label(s, "hc_20", 0.8f, kSpeed[t], W * 0.5f + 45, top - ey);
        }
        // The middle's red line, two pixels wide, and its mark.
        line(px(mid), py(top), px(mid), py(bottom), {1, 0, 0, 1}, 2.0f);
        const SDL_FColor red = fc({1, 0, 0, 1});
        // (0x120540: its base 11 above the graph's base, its point 1 above: pointing down)
        quad({px(mid) - 8, py(floor_y + 11)}, {px(mid) + 8, py(floor_y + 11)}, {px(mid), py(floor_y + 1)}, {px(mid), py(floor_y + 1)}, red, red, red, red);
    }
    flush();
    if (clipped) SDL_SetRenderClipRect(r, &was);
    else SDL_SetRenderClipRect(r, nullptr);
}

} // namespace gt4r
