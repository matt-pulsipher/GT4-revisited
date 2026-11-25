#include "gt4r/race/race_display.h"

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
constexpr u32 kTableCommon = 0x6A09A8, kTableTimes = 0x6A0838, kTableMode1 = 0x6A0BA0;
constexpr size_t kRow = 0x24;

// Reads a table's rows (ending at a zero first word); false when what is there is not a table.
bool read_table(ByteSpan program, u32 load_address, u32 address, std::vector<DisplayPlace>& out) {
    if (address < load_address) return false;
    size_t at = address - load_address;
    size_t rows = 0;
    for (;; at += kRow, ++rows) {
        if (at + kRow > program.size() || rows > 64) return false;
        const u32 member = rd32(program, at);
        if (member == 0) break;
        DisplayPlace p;
        p.member = member - 1;
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

DisplayRect place_rect(const DisplayPlace& p, float w, float h, bool wide) {
    if (p.w > 0) w = float(p.w);
    if (p.h > 0) h = float(p.h);
    DisplayRect r;
    r.w = w, r.h = h;
    const float x = place_x(p, wide), y = float(p.y);
    const int ax = p.anchor_x & 3, ay = p.anchor_y & 3;
    r.x = ax == 0 ? x : ax == 1 ? x - w : x - w * 0.5f;
    r.y = ay == 0 ? y : ay == 1 ? y - h : y - h * 0.5f;
    return r;
}

namespace {
float g_wide_push = 96.0f;
}
void set_wide_push(float units) { g_wide_push = units; }

float place_x(const DisplayPlace& p, bool wide) {
    float x = float(p.x);
    if (wide && (p.anchor_x & 4)) x += (p.anchor_x & 8) ? g_wide_push : -g_wide_push;
    return x;
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

DisplayLayout read_mode1_layout(ByteSpan program, u32 load_address) {
    DisplayLayout l;
    if (!read_table(program, load_address, kTableMode1, l.places)) l.places.clear();
    if (!l.find(element::kPosition) || !l.find(element::kTotalTime) || l.find(element::kBestTime)) l.places.clear();
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
