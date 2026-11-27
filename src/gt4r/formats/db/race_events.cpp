#include "gt4r/formats/db/race_events.h"

#include "gt4r/formats/db/specdb_prologue.h"

#include <cstdio>

namespace gt4r::db {

namespace {



std::string label_of(SpecDb& db, const char* table, u32 id) {
    const Labels* l = db.labels(table);
    const std::string* s = l ? l->label(id) : nullptr;
    return s ? *s : std::string();
}

} // namespace

RaceEvents::RaceEvents(SpecDb& db, SpecDb::Reader read) : db_(&db), read_(std::move(read)) {}

std::vector<std::string> RaceEvents::labels() const {
    std::vector<std::string> out;
    if (const Labels* l = db_->labels("RACE"))
        for (const auto& [label, id] : l->entries) out.push_back(label);
    return out;
}

std::optional<RaceEvent> RaceEvents::event(const std::string& label) const {
    const Labels* l = db_->labels("RACE");
    if (!l) return std::nullopt;
    const std::optional<u32> id = l->id(label);
    if (!id) return std::nullopt;
    return event(*id);
}

std::optional<RaceEvent> RaceEvents::event(u32 id) const {
    const Table* t = db_->table("RACE");
    if (!t) return std::nullopt;
    const ByteSpan r = t->row(id);
    if (r.size() < 0x97) return std::nullopt;
    RaceEvent e;
    e.id = id;
    e.label = label_of(*db_, "RACE", id);
    e.bytes.assign(r.begin(), r.end());
    e.course_id = rd32(r, 0x00);
    e.course = label_of(*db_, "COURSE", e.course_id);
    e.laps = r[0x43], e.grid = r[0x3E], e.x44 = r[0x44];
    e.time_limit = rd16(r, 0x18);
    for (size_t k = 0; k < 3; ++k) {
        const u32 ms = u32(r[0x76 + 2 * k]) * 60000u + u32(r[0x77 + 2 * k]) * 1000u + rd16(r, 0x1A + 2 * k);
        e.target_ms[k] = ms;
    }
    for (size_t k = 0; k < 6; ++k) e.prize[k] = rd16(r, 0x20 + 2 * k);
    e.prize_x2c = s16(rd16(r, 0x2C));
    e.need_license = r[0x45], e.need_drivetrain = r[0x46], e.need_aspiration = r[0x48], e.need_car_type = r[0x49], e.need_tire = r[0x4A];
    e.need_ps = int(r[0x92]) * 10;
    e.need_price = r[0x93], e.limit_weight = r[0x94], e.limit_length = r[0x95], e.need_country = r[0x96];
    e.year_from = s16(rd16(r, 0x38)), e.year_to = s16(rd16(r, 0x3A));
    for (size_t c = 0; c < 4; ++c) e.ai_corner[c] = r[0x57 + c], e.ai_brake[c] = r[0x5B + c], e.ai_throttle[c] = r[0x5F + c];
    e.band_global = r[0x63], e.band_mode = r[0x64];
    e.slow_pct = r[0x65], e.slow_start = r[0x66], e.slow_full = r[0x67];
    e.fast_pct = r[0x68], e.fast_start = r[0x69], e.fast_full = r[0x6A];
    return e;
}

std::optional<Opponent> RaceEvents::opponent(u32 enemy_id) const {
    const Table* t = db_->table("ENEMY_CARS");
    if (!t) return std::nullopt;
    const ByteSpan r = t->row(enemy_id);
    if (r.size() < 0x18) return std::nullopt;
    Opponent o;
    o.enemy_id = enemy_id;
    o.enemy = label_of(*db_, "ENEMY_CARS", enemy_id);
    o.car_id = rd32(r, 0x00);
    o.car = label_of(*db_, "GENERIC_CAR", o.car_id);
    o.parts_id = rd32(r, 0x08);
    o.parts = label_of(*db_, "DEFAULT_PARTS", o.parts_id);
    o.param_id = rd32(r, 0x10);
    o.param = label_of(*db_, "DEFAULT_PARAM", o.param_id);
    return o;
}

std::vector<Opponent> RaceEvents::pool(const RaceEvent& e) const {
    std::vector<Opponent> out;
    // The pool's file is named by the event as a part: its table (35) in the high word, its row in the low one.
    // (the mods' cars that join it with it: SpecDb::race_file)
    const std::optional<Bytes> f = db_->prologue() ? read_(db_->folder() + prologue::races_file(e.id)) : db_->race_file(e.id);
    if (!f || f->size() < 8) return out;
    const ByteSpan d(*f);
    const u32 n = rd32(d, 0);
    for (u32 i = 0; i < n && 8 + 16 * size_t(i + 1) <= d.size(); ++i) {
        const size_t at = 8 + 16 * size_t(i);
        const u32 row = rd32(d, at), second = rd32(d, at + 8);
        const u32 table = db_->prologue() ? prologue::table_id(rd32(d, at + 4)) : rd32(d, at + 4);
        if (table != 38) continue; // ENEMY_CARS
        std::optional<Opponent> o = opponent(row);
        if (!o) continue;
        o->paint = second == 0xFFFFFFFFu ? -1 : int(second) - 1;
        out.push_back(*o);
    }
    return out;
}

} // namespace gt4r::db
