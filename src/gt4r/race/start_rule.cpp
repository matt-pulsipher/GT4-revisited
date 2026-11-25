#include "gt4r/race/start_rule.h"

#include "gt4r/formats/course/ai_line.h"
#include "gt4r/formats/db/specdb.h"

#include <cstdlib>

namespace gt4r::race {

int start_code_digit(const std::string& code, size_t i) {
    if (i >= code.size()) return -1;
    const char c = code[i];
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void apply_drivers_file(CourseStart& s, ByteSpan ad) {
    u8 d = 0, v = 0, g = 0;
    try {
        if (course::AiData::looks_like(ad)) {
            const course::AiData a = course::AiData::parse(ad);
            if (a.sets[0]) d = (*a.sets[0])[0x23], v = (*a.sets[0])[0x24], g = (*a.sets[0])[0x25];
        }
    } catch (const std::exception&) {
    }
    s.distance_m = d ? float(d) * 10 : 400;
    s.speed_kmh = v ? float(v) : 80;
    s.spacing_m = g ? float(g) : 20;
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
    if (row.size() > 0x14) s.fuel = row[0x14] != 0 && s.kind != 2 && s.kind != 3;
    return s;
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
    if (s.kind) s.distance = distance + section_start;
    s.speed = speed;
    if (section_start != 0 && s.distance > 0) s.mark = s.kind == 2 || s.distance < section_start ? 1 : 2;
    return s;
}

int section_start_metres(float start, float lap) { return start >= 1 && start <= lap - 1 ? int(start) : 0; }

} // namespace gt4r::race
