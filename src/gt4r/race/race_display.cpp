#include "gt4r/race/race_display.h"

#include "gt4r/sim/program_tables.h"

#include "base/ee_float.h"
#include "formats/gs_memory.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace gt4r::race {

u32 tire_color(int t) {
    int r = 0, g = 0, b = 0;
    const int hi = 0xFC; // (the program's -4, a byte)
    if (t >= 0x40) r = hi, g = 2 * (0x7F - t);
    else if (t >= 0x20) r = hi, g = 4 * (0x5F - t);
    else if (t >= 0) r = 8 * t, g = hi;
    else if (t >= -0x3F) g = hi + 4 * t, b = -4 * t;
    auto byte = [](int v) { return u32(v & 0xFF); };
    return byte(r) | byte(g) << 8 | byte(b) << 16;
}

namespace {

// The single race's tables in SCUS-97328 v1.01's program: the elements every race of that kind
// shows, then the lap times' side.
constexpr u32 kTableCommon = 0x6A09A8, kTableTimes = 0x6A0838, kTableTotal = 0x6A08F0, kTableMode1 = 0x6A0BA0, kTableStacked = 0x6A0DE0, kTableSide = 0x6A0FD8;
constexpr size_t kRow = 0x24;

// The same tables in the other releases' programs: Europe's at v1.01's places; Japan's (SCPS-17001) where its routines
// address them (0x39B790 for the common, times, stacked and side tables, 0x39B978 / 0x39B9C8: v1.01's 0x3A2360, 0x3A2548,
// 0x3A2598).
u32 table_at(ByteSpan program, u32 load_address, u32 address) {
    if (sim::release_of(program, load_address) != sim::Release::Japan) return address;
    switch (address) {
    case 0x6A3D38: return 0x69A1F0; // the rev counter's faces (Japan's 0x3E52F8; v1.01's 0x3EC4E0)
    case 0x6A3C88: return 0x69A140; // the speedometer's in km/h, in mph (Japan's 0x3E4678; v1.01's 0x3EB860)
    case 0x6A3C50: return 0x69A108;
    case kTableCommon: return 0x696E88;
    case kTableTimes: return 0x696D18;
    case kTableTotal: return 0x696DD0;
    case kTableMode1: return 0x6970C8;
    case kTableStacked: return 0x697308;
    case kTableSide: return 0x697500;
    default: return address;
    }
}

// Reads a table's rows (ending at a zero first word); false when what is there is not a table.
bool read_table(ByteSpan program, u32 load_address, u32 address, std::vector<DisplayPlace>& out) {
    // (Japan's display object has its members 4 bytes before v1.01's: its rows name them so, 0x1395 for v1.01's 0x1399)
    const u32 member_shift = sim::release_of(program, load_address) == sim::Release::Japan ? 4 : 0;
    address = table_at(program, load_address, address);
    if (address < load_address) return false;
    size_t at = address - load_address;
    size_t rows = 0;
    for (;; at += kRow, ++rows) {
        if (at + kRow > program.size() || rows > 64) return false;
        const u32 member = rd32(program, at);
        if (member == 0) break;
        DisplayPlace p;
        p.member = member - 1 + member_shift;
        p.x = s32(rd32(program, at + 8)), p.y = s32(rd32(program, at + 12));
        p.w = s32(rd32(program, at + 16)), p.h = s32(rd32(program, at + 20));
        const u32 ax = rd32(program, at + 0x18), ay = rd32(program, at + 0x1C);
        // A row: an element's offset, places on or near the canvas, anchors of a few bits.
        if (member > 0x10000 || rd32(program, at + 4) != 0 || rd32(program, at + 0x20) != 0 || std::abs(p.x) > 2000 || std::abs(p.y) > 2000 ||
            p.w < 0 || p.w > 2000 || p.h < 0 || p.h > 2000 || ax > 15 || ay > 3)
            return false;
        p.anchor_x = u8(ax), p.anchor_y = u8(ay);
        out.push_back(p);
    }
    return rows >= 4;
}

} // namespace

namespace {
float g_wide_push = 96.0f;
// Touch screens: each side's lower places, their move (+ right) and the box they took (0 left, 1 right).
float g_touch_shift[2] = {0, 0};
DisplayRect g_lower[2] = {};
bool g_lower_any[2] = {false, false};
int lower_side(const DisplayPlace& p) { return p.y < 240 || !(p.anchor_x & 4) ? -1 : (p.anchor_x & 8) ? 1 : 0; }
void note_lower(int side, const DisplayRect& r) {
    DisplayRect& b = g_lower[side];
    if (!g_lower_any[side]) {
        b = r, g_lower_any[side] = true;
        return;
    }
    const float x0 = std::min(b.x, r.x), y0 = std::min(b.y, r.y);
    const float x1 = std::max(b.x + b.w, r.x + r.w), y1 = std::max(b.y + b.h, r.y + r.h);
    b = {x0, y0, x1 - x0, y1 - y0};
}
} // namespace
void set_wide_push(float units) { g_wide_push = units; }
// Ours: a half of two side by side wider than 16:9's 426.667 by `g_half_extra`: its parts by where they stand in the half at 16:9.
float g_half_extra = 0;
void set_half_extra(float units) { g_half_extra = std::max(0.0f, units); }
void set_touch_shift(float right, float left) { g_touch_shift[1] = -right, g_touch_shift[0] = left; }
DisplayRect take_lower_box(bool right_side) {
    const int side = right_side ? 1 : 0;
    const DisplayRect r = g_lower_any[side] ? g_lower[side] : DisplayRect{0, 0, 0, 0};
    g_lower_any[side] = false;
    return r;
}

float place_x(const DisplayPlace& p, bool wide) {
    float x = float(p.x);
    if (wide && (p.anchor_x & 4)) x += (p.anchor_x & 8) ? g_wide_push : -g_wide_push;
    if (g_half_extra > 0) {
        // (x + 106.667: the place in the half at 16:9; the left third keeps to the half's left edge, the right third to its right
        // edge, the middle third to its middle)
        const float h = x + 320.0f / 3.0f;
        x -= h < 426.667f / 3 ? g_half_extra : h < 426.667f * 2 / 3 ? g_half_extra / 2 : 0.0f;
    }
    if (const int side = lower_side(p); side >= 0) {
        note_lower(side, {x, float(p.y), 0, 0});
        x += g_touch_shift[side];
    }
    return x;
}


namespace {

namespace ee = gt3r::ee;
// VU0's products and sums: each rounded toward zero.
u32 vu_mul(u32 a, u32 b) { return ee::from_double(ee::to_double(a) * ee::to_double(b), ee::Round::Chop); }
u32 vu_add(u32 a, u32 b) { return ee::add(a, b, ee::Round::Chop); }
float host(u32 b) { return ee::to_host(b); }
// The canvas' edges as the program passes them: 0, 640, and with the wide flag -106.66666 (0xC2D55554) and 746.66666
// (0x443AAAAA); other sides of ours as they are.
constexpr u32 kWideLeft = 0xC2D55554u, kWideRight = 0x443AAAAAu;
bool is_wide(float side) { return std::fabs(side - 320.0f / 3) < 0.01f; }
float side_left(float side) { return side <= 0.001f ? 0.0f : is_wide(side) ? host(kWideLeft) : -side; }
float side_right(float side) { return side <= 0.001f ? 640.0f : is_wide(side) ? host(kWideRight) : 640 + side; }

} // namespace

DisplayFrame single_frame(float side) {
    DisplayFrame f;
    f.left = side_left(side), f.right = side_right(side);
    return f;
}

DisplayFrame stacked_frame(int half, float side) {
    DisplayFrame f;
    f.left = side_left(side), f.right = side_right(side);
    f.top = half == 0 ? -10.0f : 0.0f, f.bottom = f.top + 240, f.height = 224;
    return f;
}

DisplayFrame side_by_side_frame(float side) {
    DisplayFrame f;
    f.left = side_left(side), f.right = 320, f.width = 320;
    return f;
}

FramePlace frame_place(const DisplayFrame& f, float x, float y) {
    // The projection (0x39BB38, on the FPU): 2 / (r - l), 2 / (t - b), (l + r) / (l - r), (b + t) / (b - t); moved up by the
    // shift (0x4A7844 on VU0: the row's y times -shift, plus the row's translation).
    const u32 l = ee::from_host(f.left), r = ee::from_host(f.right), b = ee::from_host(f.bottom), t = ee::from_host(f.top);
    const u32 two = ee::from_host(2.0f);
    const u32 m00 = ee::div(two, ee::sub(r, l)), m11 = ee::div(two, ee::sub(t, b));
    const u32 m30 = ee::div(ee::add(l, r), ee::sub(l, r));
    u32 m31 = ee::div(ee::add(b, t), ee::sub(b, t));
    if (f.shift_y != 0) m31 = vu_add(vu_mul(m11, ee::from_host(-f.shift_y)), m31);
    // The place through it on VU0 (0x44DCC0: w stays 1, so the divide by w and the clamp leave it), then to pixels on the FPU.
    const u32 nx = vu_add(vu_mul(ee::from_host(x), m00), m30), ny = vu_add(vu_mul(ee::from_host(y), m11), m31);
    const u32 one = ee::from_host(1.0f), half = ee::from_host(0.5f);
    FramePlace p;
    p.x = host(ee::mul(ee::mul(ee::add(nx, one), ee::from_int(f.width)), half));
    p.y = host(ee::mul(ee::mul(ee::sub(one, ny), ee::from_int(f.height)), half));
    return p;
}

TextStart text_start(const DisplayFrame& f, float x, float y, int flags, float width, int ascent, int line_height, float scale_y) {
    const FramePlace p = frame_place(f, x, y);
    const int ix = int(std::trunc(p.x)), iy = int(std::trunc(p.y));
    TextStart s;
    const u32 w = ee::from_host(width);
    switch (flags & 3) {
    case 2: s.x = host(ee::sub(ee::from_int(ix), w)); break; // (the box 0 .. ix, the line at its right)
    case 3: s.x = host(ee::add(ee::from_int(ix - 320), ee::mul(ee::sub(ee::from_int(640), w), ee::from_host(0.5f)))); break; // (ix - 320 .. ix + 320, centered)
    case 1: s.x = 0; break;
    default: s.x = float(ix); break;
    }
    s.top = iy;
    const u32 sy = ee::from_host(scale_y);
    if ((flags & 12) == 0) s.top -= ee::to_int(ee::mul(ee::from_int(ascent), sy));
    else if ((flags & 12) == 12) s.top -= ee::to_int(ee::mul(ee::from_int(line_height >> 1), sy));
    return s;
}

float frame_to_canvas_x(const DisplayFrame& f, float px) { return f.left + px * (f.right - f.left) / float(f.width); }
float frame_to_canvas_y(const DisplayFrame& f, float py) { return f.top + py * (f.bottom - f.top) / float(f.height); }

DisplayRect place_rect(const DisplayPlace& p, float w, float h, bool wide) {
    if (p.w > 0) w = float(p.w);
    if (p.h > 0) h = float(p.h);
    DisplayRect r;
    r.w = w, r.h = h;
    const float x = place_x(p, wide), y = float(p.y);
    const int ax = p.anchor_x & 3, ay = p.anchor_y & 3;
    r.x = ax == 0 ? x : ax == 1 ? x - w : x - w * 0.5f;
    r.y = ay == 0 ? y : ay == 1 ? y - h : y - h * 0.5f;
    if (const int side = lower_side(p); side >= 0) note_lower(side, {r.x - g_touch_shift[side], r.y, r.w, r.h});
    return r;
}

const DisplayPlace* DisplayLayout::find(u32 member) const {
    for (const DisplayPlace& p : places)
        if (p.member == member) return &p;
    return nullptr;
}

DisplayLayout read_single_race_layout(ByteSpan program, u32 load_address) {
    DisplayLayout l;
    if (!read_table(program, load_address, kTableCommon, l.places) || !read_table(program, load_address, kTableTimes, l.places)) l.places.clear();
    // The tables of this release name the position, the lap count and the map.
    if (!l.find(element::kPosition) || !l.find(element::kLapCount) || !l.find(element::kMap)) l.places.clear();
    return l;
}

DisplayLayout read_total_race_layout(ByteSpan program, u32 load_address) {
    DisplayLayout l;
    if (!read_table(program, load_address, kTableCommon, l.places) || !read_table(program, load_address, kTableTotal, l.places)) l.places.clear();
    if (!l.find(element::kLapCount) || !l.find(element::kTotalTime) || !l.find(element::kBestTime) || l.find(element::kLastTime)) l.places.clear();
    return l;
}

DisplayLayout read_mode1_layout(ByteSpan program, u32 load_address) {
    DisplayLayout l;
    if (!read_table(program, load_address, kTableMode1, l.places)) l.places.clear();
    if (!l.find(element::kPosition) || !l.find(element::kTotalTime) || l.find(element::kBestTime)) l.places.clear();
    return l;
}

DisplayLayout read_split_layout(ByteSpan program, u32 load_address, bool side_by_side) {
    DisplayLayout l;
    if (!read_table(program, load_address, side_by_side ? kTableSide : kTableStacked, l.places)) l.places.clear();
    if (!l.find(element::kPosition) || !l.find(element::kMap) || !l.find(element::kLapTime) || l.find(element::kLapCount)) l.places.clear();
    return l;
}

std::string format_total_time(s64 ms) {
    if (ms < 0 || ms >= s64(100) * 3600000) return "\x7F:\x7F\x7F'\x7F\x7F.\x7F\x7F\x7F";
    char b[32];
    std::snprintf(b, sizeof b, "%d:%02d'%02d.%03d", int(ms / 3600000), int(ms / 60000 % 60), int(ms / 1000 % 60), int(ms % 1000));
    return b;
}

namespace {

constexpr u32 kRevFaces = 0x6A3D38, kSpeedFacesKmh = 0x6A3C88, kSpeedFacesMph = 0x6A3C50;

float f32_at(ByteSpan d, size_t o) {
    float f;
    std::memcpy(&f, d.data() + o, 4);
    return f;
}
// A picture's name at an address of the program: letters, digits and underscores.
std::string name_at(ByteSpan program, u32 load_address, u32 address) {
    std::string s;
    if (address < load_address) return s;
    for (size_t o = address - load_address; o < program.size() && program[o] && s.size() < 32; ++o) {
        const char c = char(program[o]);
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return {};
        s += c;
    }
    return s;
}
// Rows of {f32 limit (the last: 0x7FFFFFFF), f32 max, [f32 min,] u32 -> name}.
std::vector<DialFace> read_faces(ByteSpan program, u32 load_address, u32 address, bool with_min, const std::string& prefix) {
    std::vector<DialFace> out;
    address = table_at(program, load_address, address);
    if (address < load_address) return out;
    const size_t row = with_min ? 16 : 12;
    for (size_t at = address - load_address; at + row <= program.size() && out.size() < 16; at += row) {
        DialFace f;
        const bool last = rd32(program, at) == 0x7FFFFFFFu;
        f.limit = last ? 1e30f : f32_at(program, at);
        f.max = f32_at(program, at + 4);
        f.min = with_min ? f32_at(program, at + 8) : 0.0f;
        // A speedometer's row names its picture ("speed_240km"); a rev counter's the number in
        // its picture's name ("8000": rev_8000).
        f.picture = name_at(program, load_address, rd32(program, at + row - 4));
        if (f.picture.empty() || !(f.max > f.min) || f.max > 1e6f) return {};
        if (with_min) f.picture = prefix + f.picture;
        else if (f.picture.rfind(prefix, 0) != 0) return {};
        out.push_back(std::move(f));
        if (last) return out;
    }
    return {};
}

} // namespace

std::vector<DialFace> read_rev_faces(ByteSpan program, u32 load_address) { return read_faces(program, load_address, kRevFaces, true, "rev_"); }
std::vector<DialFace> read_speed_faces(ByteSpan program, u32 load_address, bool mph) {
    return read_faces(program, load_address, mph ? kSpeedFacesMph : kSpeedFacesKmh, false, "speed_");
}
const DialFace* pick_face(const std::vector<DialFace>& faces, float value) {
    for (const DialFace& f : faces)
        if (value <= f.limit) return &f;
    return faces.empty() ? nullptr : &faces.back();
}
float rev_dial_maximum(float rev_limit) { return std::floor((rev_limit + 900.0f) / 1000.0f) * 1000.0f; }
float needle_angle(float value, float min, float max, float a0, float a1) {
    const float a = a0 + (a1 - a0) * (value - min) / std::max(max - min, 1e-6f);
    return std::clamp(a, a0, a1 + 2.0f) + 90.0f;
}

std::string format_lap_time(s64 ms) {
    if (ms < 0 || ms > 359999998) return "\x7F'\x7F\x7F.\x7F\x7F\x7F";
    ms = std::min<s64>(ms, 999 * 60000 + 59999);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d'%02d.%03d", int(ms / 60000), int(ms / 1000 % 60), int(ms % 1000));
    return buf;
}

float rev_bar_fraction(float rpm, float red_line) {
    if (red_line <= 0) return 0;
    return std::floor(40.0f * std::clamp(0.7f * rpm / red_line, 0.0f, 1.0f)) / 40.0f;
}

Rgba8 gs_color(u32 c, float opacity) {
    auto ch = [](u32 v) { return u8(std::min(255u, v * 255u / 128u)); };
    Rgba8 o;
    o.r = ch(c & 0xFF), o.g = ch((c >> 8) & 0xFF), o.b = ch((c >> 16) & 0xFF);
    o.a = u8(std::clamp(float(ch(c >> 24)) * opacity, 0.0f, 255.0f));
    return o;
}

void CourseMap::map(float x, float z, float& px, float& py) const {
    const float r = rotation * 3.14159265f / 180.0f, dx = x - center_x, dz = z - center_z;
    px = float(image.width) * 0.5f + scale * (dx * std::cos(r) - dz * std::sin(r));
    py = float(image.height) * 0.5f + scale * (dx * std::sin(r) + dz * std::cos(r));
}

CourseMap CourseMap::parse(ByteSpan d) {
    CourseMap m;
    if (d.size() < 0x30 || !gt3r::has_magic(d, "GTCM")) return m;
    std::memcpy(&m.center_x, d.data() + 0x18, 4);
    std::memcpy(&m.center_z, d.data() + 0x1C, 4);
    std::memcpy(&m.scale, d.data() + 0x20, 4);
    std::memcpy(&m.rotation, d.data() + 0x24, 4);
    const u32 tex = rd32(d, 0x28);
    if (tex < 0x30 || tex + 0x40 > d.size() || !gt3r::has_magic(d.subspan(tex), "Tex1") || !(m.scale > 0)) return m;
    try {
        const ByteSpan sub = d.subspan(tex);
        const gt3r::Tex1 set = gt3r::Tex1::parse(sub);
        if (set.textures.empty()) return m;
        gt3r::GsMemory gs;
        set.upload(sub, gs);
        m.image = set.decode(gs, 0);
    } catch (const std::exception&) {
        m.image = {};
    }
    return m;
}

} // namespace gt4r::race
