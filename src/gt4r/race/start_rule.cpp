#include "gt4r/race/start_rule.h"

#include "gt4r/formats/course/ai_line.h"
#include "gt4r/formats/db/specdb.h"

#include <algorithm>
#include <cstdlib>

namespace gt4r::race {

using gt3r::s8;

int start_code_digit(const std::string& code, size_t i) {
    if (i >= code.size()) return -1;
    const char c = code[i];
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void apply_drivers_file(CourseStart& s, ByteSpan ad) {
    u8 d = 0, v = 0, g = 0, fd = 0, fv = 0;
    try {
        if (course::AiData::looks_like(ad)) {
            const course::AiData a = course::AiData::parse(ad);
            if (a.sets[0]) d = (*a.sets[0])[0x23], v = (*a.sets[0])[0x24], g = (*a.sets[0])[0x25], fd = (*a.sets[0])[0x19], fv = (*a.sets[0])[0x1A];
        }
    } catch (const std::exception&) {
    }
    s.distance_m = d ? float(d) * 10 : 400;
    s.speed_kmh = v ? float(v) : 80;
    s.spacing_m = g ? float(g) : 20;
    s.free_distance_m = float(fd) * 10, s.free_speed_kmh = float(fv) * 5;
}

CourseStart course_start(db::SpecDb& db, const std::string& course) {
    CourseStart s;
    const db::Table* t = db.table("COURSE");
    const db::Labels* l = db.labels("COURSE");
    const db::Strings* pool = db.strings("UnistrDB");
    if (!t || !l || !pool) return s;
    const std::optional<u32> id = l->id(course);
    if (!id) return s;
    const ByteSpan row = t->row(*id);
    if (row.size() < 0x0C) return s;
    s.known = true;
    s.code = pool->at(rd32(row, 0x08));
    s.rolling = start_code_digit(s.code, 0) > 0;
    if (row.size() > 0x10) s.kind = row[0x10] == 0xFF ? 0 : row[0x10];
    if (row.size() > 0x12) s.way = row[0x12];
    if (row.size() > 0x13) s.strip = row[0x13] >= 2;
    if (row.size() > 0x14) s.fuel = row[0x14] != 0 && s.kind != 2 && s.kind != 3;
    return s;
}

std::array<u8, 0x80> event_rule_block(std::span<const u8> r) {
    std::array<u8, 0x80> c{};
    if (r.size() < 0x92) return c;
    c[0x01] = r[0x43], c[0x02] = r[0x44];
    c[0x38] = r[0x18], c[0x39] = r[0x19];
    for (size_t k = 0; k < 8; ++k) c[0x04 + k] = r[0x4B + k];
    for (size_t k = 0; k < 4; ++k) c[0x0C + k] = r[0x5F + k], c[0x10 + k] = r[0x5B + k], c[0x14 + k] = r[0x57 + k], c[0x18 + k] = r[0x53 + k];
    c[0x20] = r[0x63], c[0x22] = r[0x64], c[0x24] = r[0x65], c[0x25] = r[0x66], c[0x26] = r[0x67], c[0x28] = r[0x68], c[0x29] = r[0x69], c[0x2A] = r[0x6A];
    for (size_t k = 0; k < 5; ++k) c[0x2C + k] = r[0x6B + k];
    c[0x33] = r[0x70], c[0x34] = r[0x71], c[0x31] = r[0x72], c[0x32] = r[0x73];
    if (r[0x6B] != 0 && c[0x31] == 0) c[0x31] = 4;
    c[0x49] = r[0x8A], c[0x48] = r[0x8B], c[0x4B] = 0x32, c[0x4A] = r[0x88], c[0x00] = r[0x85], c[0x44] = r[0x87], c[0x46] = r[0x75];
    c[0x40] = u8(r[0x3E] - 1), c[0x4E] = r[0x42];
    c[0x54] = r[0x14], c[0x55] = r[0x15];
    for (size_t k = 0; k < 5; ++k) c[0x4F + k] = r[0x80 + k], c[0x56 + 2 * k] = r[0x2E + 2 * k], c[0x57 + 2 * k] = r[0x2F + 2 * k];
    c[0x60] = r[0x91];
    for (size_t k = 1; k < 6; ++k) c[0x60 + 2 * k] = r[0x8B + k];
    c[0x41] = r[0x86], c[0x4C] = 0x50;
    const s8 section = s8(r[0x89]);
    c[0x21] = u8(section < -1 ? -1 : section);
    return c;
}

void arcade_rules(std::vector<u8>& r, int penalty, int damage) {
    if (r.size() < 0x8C) return;
    // Hits: the penalty's seconds (0xFF the pit lane's mark), the wall's and the car's limits.
    r[0x88] = penalty == 1 ? 10 : penalty == 2 ? 0xFF : 0;
    r[0x8A] = penalty == 1 || penalty == 2 ? 8 : 0xFF;
    r[0x8B] = penalty == 1 || penalty == 2 ? 40 : 0xFF;
    put_tire_damage(r, damage);
}

void put_tire_damage(std::vector<u8>& r, int damage) {
    if (r.size() < 0x74) return;
    static constexpr u8 kWear[3][9] = {{0, 0, 0, 0, 0, 0, 0, 0, 0}, {8, 40, 12, 1, 5, 5, 5, 4, 0}, {6, 40, 12, 1, 5, 4, 5, 3, 0}};
    const u8(&k)[9] = kWear[damage == 1 || damage == 2 ? damage : 0];
    std::copy(std::begin(k), std::end(k), r.begin() + 0x6B);
}

Conductor conductor_for(const std::string& sequence, const std::string& event) {
    if (sequence == "mission") return Conductor::Mission;
    if (sequence != "license" && sequence != "license_race") return Conductor::Other;
    if (event.size() < 4) return Conductor::License;
    const int n = std::atoi(event.c_str() + event.size() - 4);
    return n == 5 || n == 10 || n == 15 ? Conductor::Training : Conductor::License;
}

RowStart RowStart::from_row(std::span<const u8> r) {
    RowStart s;
    if (r.size() < 0x92) return s;
    auto half = [&](size_t o) { return s16(u16(r[o] | r[o + 1] << 8)); };
    s.distance[0] = half(0x14), s.speed[0] = r[0x42];
    for (size_t k = 1; k < 6; ++k) s.distance[k] = half(0x2E + 2 * (k - 1)), s.speed[k] = r[0x80 + (k - 1)];
    return s;
}

int RowStart::distance_of(Conductor c, size_t car) const {
    return c == Conductor::License ? distance[0] : car < distance.size() ? distance[car] : 0;
}

int RowStart::speed_of(Conductor c, size_t car) const {
    if (c == Conductor::License) return speed[0];
    const int own = car < speed.size() ? speed[car] : 0;
    return own ? own : speed[0];
}

CarStart car_start(const RowStart& row, Conductor c, size_t car, int section_start) {
    CarStart s;
    const int distance = row.distance_of(c, car), speed = row.speed_of(c, car);
    if (distance < 0 && speed > 0) s.kind = 2; // (0x34D168)
    else if (distance != 0) s.kind = 1;        // (0x34D070)
    if (s.kind) s.distance = distance + section_start; // (0x34D0F8: the speed stays with the grid too)
    s.speed = speed;
    if (section_start != 0 && s.distance > 0) s.mark = s.kind == 2 || s.distance < section_start ? 1 : 2;
    return s;
}

int section_start_metres(float start, float lap) { return start >= 1 && start <= lap - 1 ? int(start) : 0; }

} // namespace gt4r::race
