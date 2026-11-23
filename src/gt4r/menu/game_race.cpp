// MRaceData: an event of the car database's RACE table as the menus read it (the natives at
// 0x1A0F80 on; docs/formats/database.md "RACE"), and the opponents' pool of a race (its file
// specdb/<set>/RACES/<the race's code>).

#include "gt4r/menu/game_internal.h"

#include "gt4r/formats/db/specdb_prologue.h"

#include <cstdio>

namespace gt4r::menu {

using script::Args;
using script::Kind;
using script::Value;
using script::Vm;

namespace {

u16 r16(ByteSpan r, size_t o) { return r.size() >= o + 2 ? u16(r[o] | r[o + 1] << 8) : 0; }
u32 r32(ByteSpan r, size_t o) { return r.size() >= o + 4 ? u32(r[o]) | u32(r[o + 1]) << 8 | u32(r[o + 2]) << 16 | u32(r[o + 3]) << 24 : 0; }
u8 r8(ByteSpan r, size_t o) { return o < r.size() ? r[o] : 0; }

} // namespace

// The race's pool of opponents: u32 count, u32 0, then {u64 ENEMY_CARS code, s64 paint + 1
// (-1: any)} each (0x4472C0).
std::vector<std::pair<std::string, int>> race_entries(Database& d, const std::string& race) {
    std::vector<std::pair<std::string, int>> out;
    const u64 code = d.code(kRaceTable, race);
    if (code == kNoCode || !d.db) return out;
    char name[40];
    std::snprintf(name, sizeof name, "RACES/%016llX", static_cast<unsigned long long>(code));
    // (Prologue's files carry its own table numbers: specdb_prologue.h.)
    const std::string path = d.db->prologue() ? db::prologue::races_file(u32(code)) : std::string(name);
    const std::optional<Bytes> file = d.rt->reader()(d.db->folder() + path);
    if (!file || file->size() < 8) return out;
    const u32 n = r32(*file, 0);
    for (u32 i = 0; i < n && 8 + (i + 1) * 16 <= file->size(); ++i) {
        const size_t o = 8 + i * 16;
        const u32 table = d.db->prologue() ? db::prologue::table_id(r32(*file, o + 4)) : r32(*file, o + 4);
        const u64 c = u64(r32(*file, o)) | u64(table) << 32;
        const s64 paint = s64(u64(r32(*file, o + 8)) | u64(r32(*file, o + 12)) << 32);
        out.emplace_back(d.label(c), paint < 0 ? -1 : int(paint - 1));
    }
    return out;
}

void install_race_classes(GameNatives& n, script::Module& menu) {
    GameNatives* np = &n;
    auto cls = make_class(menu, "MRaceData");
    // available(label): the RACE table has the label (0x1A2A90).
    static_fn(*cls, "available", [np](Vm&, const Value&, Args a) { return number(np->db().code(kRaceTable, arg_text(a, 0)) != kNoCode); });
    cls->construct = [np](Vm&, Args a) {
        auto race = std::make_shared<Object>("MRaceData");
        const std::string label = arg_text(a, 0);
        race->set("race_code", text(label));
        auto row = [np, label]() { return np->db().row("RACE", label); };
        // The medal times as text: minutes, seconds (+0x76 ..) and milliseconds (+0x1A ..).
        auto medal = [row](size_t m, size_t ms) {
            return [row, m, ms] {
                const ByteSpan r = row();
                u32 t = u32(r8(r, m)) * 60000 + u32(r8(r, m + 1)) * 1000 + r16(r, ms);
                if (!t) t = kNoTime;
                return text(time_text(t));
            };
        };
        race->property("gold_time", medal(0x76, 0x1A));
        race->property("silver_time", medal(0x78, 0x1C));
        race->property("bronze_time", medal(0x7A, 0x1E));
        race->property("entry_num", [row] { return number(r8(row(), 0x3E)); });
        race->property("course_code", [row, np] {
            const ByteSpan r = row();
            return r.size() >= 8 ? text(np->db().label(u64(r32(r, 0)) | u64(r32(r, 4)) << 32)) : text("");
        });
        // car_code(i): the label of entry i of the race's pool (an ENEMY_CARS row's).
        race->method("car_code", [np, label](Vm&, const Value&, Args a) {
            const auto entries = race_entries(np->db(), label);
            const s32 i = arg_int(a, 0);
            return i >= 0 && size_t(i) < entries.size() ? text(entries[size_t(i)].first) : Value{};
        });
        auto byte = [row](size_t at, s32 scale) {
            return [row, at, scale](Vm&, const Value&, Args) { return number(s32(r8(row(), at)) * scale); };
        };
        race->method("getNeedLicense", byte(0x45, 1));
        race->method("getNeedDrivetrain", byte(0x46, 1));
        race->method("getNeedAspiration", byte(0x48, 1));
        race->method("getNeedCarType", byte(0x49, 1));
        race->method("getNeedTire", byte(0x4A, 1));
        race->method("getNeedPs", byte(0x92, 10));
        race->method("getNeedPrice", byte(0x93, 10000));
        race->method("getLimitedWeight", byte(0x94, 10));
        race->method("getLimitedLength", byte(0x95, 100));
        race->method("getNeedCountryID", byte(0x96, 1));
        race->method("getNumberOfLaps", byte(0x43, 1));
        race->method("getLimitTime", [row](Vm&, const Value&, Args) { return number(r16(row(), 0x18)); });
        // getNeedCountry(): the countries by name (0x692860).
        race->method("getNeedCountry", [row](Vm&, const Value&, Args) {
            static const char* const one[7] = {"JP", "US", "GB", "IT", "FR", "DE"};
            const u8 c = r8(row(), 0x96);
            if (c >= 1 && c <= 6) return Value::array({text(one[c - 1])});
            if (c == 7) return Value::array({text("DE"), text("BE"), text("SE"), text("FR"), text("IT"), text("GB"), text("NL"), text("ES")});
            return Value::array({});
        });
        race->method("getNeedYearRange", [row](Vm&, const Value&, Args) {
            const ByteSpan r = row();
            return Value::array({number(s16(r16(r, 0x38))), number(s16(r16(r, 0x3A)))});
        });
        // Prizes: places 1 .. 6 (+0x20, u16 x 100 credits); the series' (+0x2C).
        race->method("getPrize", [row](Vm&, const Value&, Args a) {
            const s32 p = arg_int(a, 0);
            return number(p >= 0 && p < 6 ? s32(r16(row(), 0x20 + 2 * size_t(p))) * 100 : 0);
        });
        race->method("getPrizeGC", [row](Vm&, const Value&, Args) { return number(s32(s16(r16(row(), 0x2C))) * 100); });
        // The championship's points: 10, 6, 4, 3, 2, 1 (the program's table at 0x6A6EF0).
        race->method("getAdditionalDriversPoints", [](Vm&, const Value&, Args a) {
            static const s32 points[6] = {10, 6, 4, 3, 2, 1};
            const s32 i = arg_int(a, 0);
            return number(i >= 0 && i < 6 ? points[i] : 0);
        });
        // getAllowedCarList(): the GENERIC_CAR labels of the row's ALLOW_ENTRY row (60 codes, -1
        // ends); nil when the race has none.
        race->method("getAllowedCarList", [row, np](Vm&, const Value&, Args) {
            const ByteSpan r = row();
            if (r.size() < 0x10 || r32(r, 0x08) == 0xFFFFFFFFu) return Value{};
            const db::Table* allow = np->db().db ? np->db().db->table("ALLOW_ENTRY") : nullptr;
            const ByteSpan list = allow ? allow->row(r32(r, 0x08)) : ByteSpan{};
            std::vector<Value> out;
            for (size_t o = 0; o + 8 <= list.size(); o += 8) {
                if (r32(list, o) == 0xFFFFFFFFu) break;
                out.push_back(text(np->db().label(make_code(kCarTable, r32(list, o)))));
            }
            return Value::array(std::move(out));
        });
        return object_value(race);
    };
}

} // namespace gt4r::menu

namespace gt4r::menu {

// The program's result routines in outline (docs/formats/menu.md "After a race"): a finished
// event pays the place's prize (left to the scripts to add) and counts the place; a better
// place updates the race's record unit; a test keeps its result and its ranking; every run is
// a day of the diary.
s64 Runtime::race_finished(const RaceResult& r) {
    const std::shared_ptr<GameNatives> n = natives_of(*this);
    GameState& s = game();
    if (!n) return 0;
    Database& d = n->db();
    std::string race = r.race;
    for (const std::string& tried : {r.race, "us" + r.race, "jp" + r.race})
        if (!r.race.empty() && d.code(kRaceTable, tried) != kNoCode) {
            race = tried;
            break;
        }
    const u64 race_code = d.code(kRaceTable, race);
    const int unit = d.race_unit(race);
    auto update_unit = [&](u32 type, u32 result) {
        if (unit < 0) return;
        u32& w = s.race_units[size_t(unit)];
        const u32 have = w & 15;
        if (have == 0) w = (w & ~0xFFFu) | type | result << 4 | result << 8;
        else if (have == type) {
            w = (w & ~(15u << 8)) | result << 8;
            if (result < ((w >> 4) & 15)) w = (w & ~(15u << 4)) | result << 4;
        }
        if (r.score >= 0 && ((w >> 16) & 0xFF) < u32(std::min(r.score, 255))) w = (w & ~(0xFFu << 16)) | u32(std::min(r.score, 255)) << 16;
    };
    static const char* const kNames[16] = {"1", "2", "3", "4", "5", "6", "7", "8", "abandon", "gold", "silver", "bronze", "red", "failed", "view", "NONE"};
    auto result_number = [](const std::string& name) {
        for (u32 i = 0; i < 16; ++i)
            if (name == kNames[i]) return i;
        return 15u;
    };
    // The car's distance (❓ the adders 0x343518 on are the race's; their callers are not read).
    if (r.garage_slot >= 0 && size_t(r.garage_slot) < s.garage.size() && s.garage[size_t(r.garage_slot)].record && r.distance_m > 0) {
        CarRecord& rec = *s.garage[size_t(r.garage_slot)].record;
        rec.set_odometer(std::min<u32>(rec.odometer() + u32(r.distance_m), 100'000'000));
        if (!rec.no_wear()) {
            rec.set_condition(1, std::min(rec.condition(1) + r.distance_m, 11'000'000.0f));
            rec.set_condition(2, std::min(rec.condition(2) + r.distance_m, 6'000'000.0f));
            rec.set_condition(4, std::min(rec.condition(4) + r.distance_m, 3'000'000.0f));
        }
        s.aspec_distance += s64(r.distance_m);
    }
    DayEvent day;
    s64 prize = 0;
    switch (r.kind) {
    case RaceResult::Kind::Event: {
        if (!r.finished) {
            ++s.rank_counts[0];
            day.b1 = 15;
            day.word = kNoTime;
        } else {
            const int p = std::clamp(r.place, 0, 7);
            const ByteSpan row = d.row("RACE", race);
            prize = p < 6 && row.size() >= 0x2C ? s64(u16(row[0x20 + 2 * size_t(p)] | row[0x21 + 2 * size_t(p)] << 8)) * 100 : 0;
            if (p < 6) ++s.rank_counts[size_t(p) + 1];
            s.total_prize_money += prize;
            update_unit(2, u32(p));
            day.b1 = u8(p), day.word = r.time_ms;
        }
        day.b2 = 14;
        if (race == "family") day.type = 8, day.b3 = 2, day.code = d.code(kCourseTable, r.course);
        else day.type = 3, day.code = race_code;
        break;
    }
    case RaceResult::Kind::License:
    case RaceResult::Kind::Mission: {
        const bool license = r.kind == RaceResult::Kind::License;
        const u32 result = r.finished ? result_number(r.result) : 15;
        update_unit(license ? 1 : 3, result);
        day.type = license ? 4 : 5, day.b1 = u8(result), day.b2 = 14, day.word = r.finished ? r.time_ms : kNoTime, day.code = race_code;
        // The test's ranking (0x4320B8): a finished run with a time.
        if (r.finished && r.time_ms && result != 15) {
            const Value args[3] = {Value::integer(s32(r.time_ms)), Value::string(kNames[result]), Value::string(r.pass_code.empty() ? "--------" : r.pass_code)};
            const Value record = vm().attribute(vm().lookup("game"), "license_record");
            const Value label[1] = {Value::string(race)};
            const Value u = record.p ? vm().call(vm().attribute(record, "getUnit"), label) : Value{};
            if (u.p) vm().call(vm().attribute(u, "insertEntry"), args);
        }
        break;
    }
    case RaceResult::Kind::Course:
        day.type = 8, day.b3 = u8(r.course_kind), day.b1 = u8(r.finished ? 0 : 15), day.b2 = 14, day.word = r.finished ? r.time_ms : kNoTime;
        day.code = d.code(kCourseTable, r.course);
        break;
    }
    s.put_day(day);
    return prize;
}

} // namespace gt4r::menu
