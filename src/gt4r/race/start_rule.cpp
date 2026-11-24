#include "gt4r/race/start_rule.h"

#include "gt4r/formats/course/ai_line.h"
#include "gt4r/formats/db/specdb.h"

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
    return s;
}

} // namespace gt4r::race
