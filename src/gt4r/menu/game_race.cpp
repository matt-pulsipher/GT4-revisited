// MRaceData: an event of the car database's RACE table as the menus read it (the natives at
// 0x1A0F80 on; docs/formats/database.md "RACE"), and the opponents' pool of a race (its file
// specdb/<set>/RACES/<the race's code>).

#include "gt4r/menu/game_bspec.h"
#include "gt4r/formats/db/race_labels.h"
#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/race_natives.h"
#include "gt4r/sim/showroom.h"

#include "gt4r/formats/db/specdb_prologue.h"

#include <cstdio>
#include <cstdlib>

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
        // The license tests' and missions' rows carry the region ("usl0m0001" for "l0m0001"): the
        // labels tried as Database::race_unit does (0x430D20).
        std::string found = label;
        for (const std::string& tried : db::race_labels(label, np->rt.region()))
            if (!label.empty() && np->db().code(kRaceTable, tried) != kNoCode) {
                found = tried;
                break;
            }
        auto row = [np, found]() { return np->db().row("RACE", found); };
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
        race->method("getAdditionalDriversPoints", [np](Vm&, const Value&, Args a) { return number(championship_points(np->db(), arg_int(a, 0))); });
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

// A championship's points for a place (0 first; the program's signed bytes at 0x6A6EF0, read
// from the user's own program; none when its tables are not mapped).
s32 championship_points(Database& d, int place) {
    if (place < 0 || place >= 6 || !d.program.known()) return 0;
    return s32(s8(d.program.b(kChampionshipPoints + u32(place))));
}

s32 Runtime::points_for_place(int place) {
    const std::shared_ptr<GameNatives> n = natives_of(*this);
    return n ? championship_points(n->db(), place) : 0;
}

// A distance driven added to a car's condition as the race's step does (0x35DE20, 0x35DD00):
// the odometer (whole meters, capped at 100,000,000; 0x343518), and unless the car has no
// wear the engine's distance up to its limit (0x3435E8), the oil's (0x343708: from below 0 it
// counts up to 0 and then starts again at three times its run-in length; else up to its limit)
// and the body's (0x3439E8). The limits from the program's static block (0x620CD8 +0xD0 on, in
// 100,000 m: the new engine -3, its limit 100 from that, the run-in -2 x 0.5, the oil's limit
// 60; 0x350678) and the body's from 0x620AF6 (x 1,000 m; 0x366100). ❓ Added once for the whole
// distance here, where the race adds it a step at a time (the oil's start again differs by the
// step that crosses 0).
void add_distance(Database& d, CarRecord& rec, float meters) {
    rec.set_odometer(std::min<u32>(rec.odometer() + u32(meters), 100'000'000));
    if (rec.no_wear() || !d.program.known()) return;
    const sim::Program& p = d.program;
    const float k = 100'000.0f;
    const float engine_new = -float(p.b(0x620CD8 + 0xD0)) * k, engine_max = float(p.b(0x620CD8 + 0xD3)) * k + engine_new;
    const float run_in = -float(p.b(0x620CD8 + 0xD5)) * k * 0.5f, oil_max = float(p.b(0x620CD8 + 0xD7)) * k;
    const float body_max = float(s16(p.h(0x620AF6))) * 1000.0f;
    rec.set_condition(1, std::min(rec.condition(1) + meters, engine_max));
    float oil = rec.condition(2);
    if (oil < 0) {
        oil += meters;
        if (oil >= 0) oil = -run_in * 3.0f + oil;
    } else
        oil = std::min(oil + meters, oil_max);
    rec.set_condition(2, oil);
    rec.set_condition(4, std::min(rec.condition(4) + meters, body_max));
}

// The race count of a championship's label and the index of one of its races: the label ends
// in two digits of each ("..._0500": five races, the championship itself; "..._0503": its third).
int series_race_count(const std::string& label) { return label.size() >= 6 ? std::atoi(label.substr(label.size() - 4, 2).c_str()) : 0; }

namespace {

// A race record unit's update (0x430A58): a unit of no kind takes the kind and the result as
// best and last; one of the same kind takes the last, and the best when it is better; another
// kind is left.
void unit_result(u32& w, u32 type, u32 result) {
    const u32 have = w & 15;
    result &= 15;
    if (have == 0) {
        w = (w & ~0xFFFu) | type | result << 4 | result << 8;
        return;
    }
    if (have != type || have > 3) return;
    w = (w & ~0xF00u) | result << 8;
    if (result < ((w >> 4) & 15)) w = (w & ~0xF0u) | result << 4;
}
// The unit's best score (0x430A20): kept when higher, capped at 255.
void unit_score(u32& w, int score) {
    const u32 v = u32(std::clamp(score, 0, 255));
    if (((w >> 16) & 0xFF) < v) w = (w & ~(0xFFu << 16)) | v << 16;
}

} // namespace

std::vector<int> championship_grid_places(const std::vector<s32>& kg, const std::vector<s32>& ps) {
    const size_t n = std::min(kg.size(), ps.size());
    std::vector<s32> ratio(n, 0);
    for (size_t i = 0; i < n; ++i) ratio[i] = ps[i] > 0 ? kg[i] / ps[i] : 0;
    std::vector<int> rank(n, 0), places(n, int(n) - 1);
    for (size_t i = 1; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            if (ratio[i] == ratio[j]) continue;
            ++rank[ratio[j] < ratio[i] ? i : j];
        }
    std::vector<bool> taken(n, false);
    for (size_t k = 1; k < n; ++k) {
        size_t at = size_t(rank[k]);
        while (at < n && taken[at]) ++at;
        if (at >= n) at = n - 1;
        places[k] = int(at), taken[at] = true;
    }
    if (n) places[0] = int(n) - 1;
    return places;
}

std::vector<int> championship_grid_places(Runtime& rt, const std::vector<std::pair<std::string, std::optional<sim::Equipment>>>& entries) {
    const std::shared_ptr<GameNatives> n = natives_of(rt);
    const sim::Showroom* showroom = n ? n->db().showroom() : nullptr;
    if (!showroom) return {};
    std::vector<s32> kg, ps;
    for (const auto& [car, eq] : entries) {
        const std::optional<sim::ShowroomFigures> fig = eq ? showroom->figures(*eq) : n->db().stock_showroom(car);
        kg.push_back(fig ? s32(fig->weight) : 0), ps.push_back(fig ? gt3r::ee::to_int(fig->power) : 0);
    }
    return championship_grid_places(kg, ps);
}

void start_quick_work(Runtime& rt, const RaceRequest& q) {
    GameState& s = rt.game();
    s.quick_work.reset();
    QuickWork& w = quick_work(s);
    if (q.sequence == "time_trial" || q.sequence == "freerun" || q.sequence == "machinetest") w.set("BestTime", s32(QuickWork::kNoTime)); // 0x112E78, 0x117718, 0x116070
    if (q.sequence == "freerun") w.set("canLoadGhost", 1); // 0x117718
    // The B-spec button (Arcade's race 0x111A40, its time trial 0x112E78, the free run 0x117718: 0x110CB8): on a paved course
    // (its COURSE row's +0x10 0) whose row's byte +0x14 is not 0.
    const bool arcade = q.sequence == "single_race" || q.sequence == "time_trial";
    if (const std::shared_ptr<GameNatives> n = natives_of(rt); n && (arcade || q.sequence == "freerun")) {
        const ByteSpan course = n->db().row("COURSE", q.course);
        const int kind = course.size() > 0x10 ? course[0x10] : -1;
        w.set("IsBSpec", kind == 0 && course.size() > 0x14 && course[0x14] ? 1 : 0);
        if (arcade) { // (0x111B50, 0x112FE8)
            const int tire = kind == 3 ? 12 : q.tire;
            w.set("IsDryCourse", kind == 2 || kind == 3 ? 0 : 1);
            w.set("QuickTunePowerLevel", 0), w.set("QuickTuneWeightLevel", 0);
            w.set("QuickTuneFrontTireType", tire), w.set("QuickTuneRearTireType", tire);
            w.set("QuickTuneTransmission", q.manual ? 1 : 0);
            w.set("QuickTuneDrivingAssist", q.assist_asm ? 2 : q.assist_tcs ? 1 : 0);
            w.set("IsQuickTuned", 0);
        }
    }
    // The player's own start of an event (0x114334, a championship's 0x119A78: the event's license).
    if (const std::shared_ptr<GameNatives> n = natives_of(rt); n && (q.sequence == "solo_event" || q.sequence == "championship")) {
        ByteSpan row;
        for (const std::string& label : db::race_labels(q.race, rt.region()))
            if (row.empty() && !label.empty()) row = n->db().row("RACE", label);
        w.set("IsASpec", event_aspec_allowed(n->db(), s, r8(row, 0x45)) ? 1 : 0);
    }
    if (q.sequence == "machinetest") w.set("DisableLogger", 1); // 0x116124
    if (q.sequence == "license_race") w.set("BestTime", s32(QuickWork::kNoTime)), w.set("canLoadGhost", 1); // ❓ (Prologue's constructor is not read: dashes, as before; ✔ its card icon is lit on lessons 1 and 2)
    if (q.sequence == "license") {
        // Tests 5, 10 and 15 of B, A, IB and IA run in 0x114E38 (its fill 0x115310: no ghost to load, no Analyzer), the others
        // in 0x11CB38 (0x11D340: a ghost to load unless the RACE row's byte +0x75 is set; ❓ the Analyzer's two other cases,
        // 0x11D400, are not read).
        const std::shared_ptr<GameNatives> n = natives_of(rt);
        ByteSpan row;
        for (const std::string& label : db::race_labels(q.race, rt.region()))
            if (row.empty() && n && !q.race.empty()) row = n->db().row("RACE", label);
        const int test = q.race.size() >= 7 ? std::atoi(q.race.c_str() + 3) : 0;
        const bool short_one = q.race.compare(0, 3, "l0s") != 0 && q.race.compare(0, 3, "l0c") != 0 && (test == 5 || test == 10 || test == 15);
        if (short_one) w.set("DisableLogger", 1);
        else w.set("canLoadGhost", row.size() > 0x75 && row[0x75] ? 0 : 1);
    }
}

// The program's result routines (docs/formats/menu.md "After a race"). The race's menus' work
// (MQuickWork) gets what the routines write into it: the prize, the rows in finishing order
// (0x114390, 0x11A168, 0x11C1D8), the player's place, and a championship's grand prize and rank.
s64 Runtime::race_finished(const RaceResult& r) {
    const std::shared_ptr<GameNatives> n = natives_of(*this);
    GameState& s = game();
    if (!n) return 0;
    Database& d = n->db();
    auto known = [&d, region = region()](const std::string& label) {
        for (const std::string& tried : db::race_labels(label, region))
            if (!label.empty() && d.code(kRaceTable, tried) != kNoCode) return tried;
        return label;
    };
    const std::string race = known(r.race);
    const u64 race_code = d.code(kRaceTable, race);
    const int unit = d.race_unit(race);
    u32 dummy = 0;
    u32& w = unit >= 0 ? s.race_units[size_t(unit)] : dummy;
    const ByteSpan row = d.row("RACE", race);
    auto prize_of = [&row](int place) { return place >= 0 && place < 6 && row.size() >= 0x2C ? s64(u16(row[0x20 + 2 * size_t(place)] | row[0x21 + 2 * size_t(place)] << 8)) * 100 : 0; };
    static const char* const kNames[16] = {"1", "2", "3", "4", "5", "6", "7", "8", "abandon", "gold", "silver", "bronze", "red", "failed", "view", "NONE"};
    auto result_number = [](const std::string& name) {
        for (u32 i = 0; i < 16; ++i)
            if (name == kNames[i]) return i;
        return 15u;
    };
    // The B-spec driver's record back into its book (0x468AF0 -> 0x468860), and the course's marks.
    // (0x468AF0 is called by the event's sequence 0x113B48, the championship's 0x118EF0, the free run's 0x116E20 and photo
    // drive's 0x11ABE0; not by practice's 0x1181C8 or the machine test's 0x115A90.)
    const bool course_book = r.kind == RaceResult::Kind::Course && (r.course_kind == 1 || r.course_kind == 4);
    if (r.bspec_record && (r.kind == RaceResult::Kind::Event || r.kind == RaceResult::Kind::Championship || course_book)) {
        std::optional<size_t> at = r.bspec_course ? bspec_course_unit(d, s, r.course) : std::nullopt;
        // Doubled (0x468A60): a run whose conductor's vf6 gives 0 (a race; a free run's 0x3F5518 gives 1, photo drive's is a
        // race's, but has one car), of two cars or more (cond + 0xF8A0), that the car finished (+0x5B6) in first place (its
        // place byte +0x5B3 at 1).
        int first = r.place;
        for (size_t i = 0; i < r.order.size(); ++i)
            if (r.order[i].entry == 0) first = int(i);
        const bool doubled = r.bspec_doubled || (r.kind != RaceResult::Kind::Course && r.order.size() >= 2 && r.finished && first == 0);
        const u8* marks = at ? s.course_record.data() + *at + 0x228 : nullptr;
        // Only over the book (and marks) the run started from. And not a doubled change that would go below zero: the
        // program's 16-bit store would wrap there (❓ whether a run can lower its record is not read; a doubled fall that
        // stays at or above zero is written as the program writes it).
        const bool same = r.bspec_start && *r.bspec_start == s.bspec_book && (!marks || std::equal(marks, marks + 8, r.bspec_start_marks.begin()));
        bool below = false;
        const int cls = std::clamp(r.bspec_class, 0, 7);
        for (int t = 0; t < 3 && doubled; ++t)
            for (int c = 0; c < 8; ++c)
                if (!(cls == 0 && c == 7)) {
                    const s32 v = (*r.bspec_record)[size_t(t * 16 + c * 2)] | (*r.bspec_record)[size_t(t * 16 + c * 2 + 1)] << 8;
                    below = below || 2 * v < s32(s.bspec_book[size_t(t * 64 + c * 8 + cls)]);
                }
        for (int m = 0; m < 8 && doubled && marks; ++m) below = below || 2 * s32((*r.bspec_record)[size_t(0x30 + m * 2 + 1)]) < s32(marks[m]);
        if (!same || below) {
            note(same ? "B-spec: a doubled change below zero, the book kept" : "B-spec: the run did not start from the saved book, the book kept");
        } else {
            bspec::write_back(s.bspec_book, cls, *r.bspec_record, doubled, at ? 2 : 0, at ? s.course_record.data() + *at + 0x228 : nullptr);
            // The sequence's next run starts from the book as it is now (0x468710 is called before every run).
            s.race.bspec_book = s.bspec_book;
            if (at) std::copy_n(s.course_record.data() + *at + 0x228, 8, s.race.bspec_course_marks.begin());
        }
    }
    // The player's place in the finishing order (0x110570), or the one given.
    int place = r.place;
    for (size_t i = 0; i < r.order.size(); ++i)
        if (r.order[i].entry == 0) place = int(i);
    // The race's menus' rows: every car in finishing order with its time (0x114390); a race the
    // player did not finish has "-" rows and no times (0x110898; ❓ here empty rows).
    QuickWork& work = quick_work(s);
    auto fill_rows = [&](bool cleared) {
        if (r.order.empty()) return;
        work.rows.assign(r.order.size(), QuickRow{});
        for (size_t i = 0; i < r.order.size(); ++i) {
            QuickRow& q = work.rows[i];
            if (cleared) {
                q.time_ms = QuickWork::kNoTime;
                continue;
            }
            q.car = r.order[i].car, q.paint = r.order[i].paint;
            q.time_ms = r.order[i].finished ? r.order[i].time_ms : QuickWork::kNoTime;
            if (r.order[i].entry == 0) work.set("playerGridNumber", s32(i));
        }
        work.set("numberOfEntries", s32(r.order.size()));
    };
    // The car's distance: what the race's step adds to the car's condition (0x35DE20).
    if (r.garage_slot >= 0 && size_t(r.garage_slot) < s.garage.size() && s.garage[size_t(r.garage_slot)].record && r.distance_m > 0)
        add_distance(d, *s.garage[size_t(r.garage_slot)].record, r.distance_m);
    // The player's totals (0x10FA58, called by the event's sequence 0x113B48, the machine test's 0x115A90, the free run's
    // 0x116E20, practice's 0x1181C8, the championship's 0x118EF0 and photo drive's 0x11ABE0; not by the license tests',
    // the missions' or Arcade's; added at the sequence's end, 0x114058): the meters the B-spec driver drove (the car's
    // +0x7B0) to the B-spec total, the rest (+0x6D4 less those) to the A-spec one, truncated.
    const bool counted = r.kind == RaceResult::Kind::Event || r.kind == RaceResult::Kind::Championship || (r.kind == RaceResult::Kind::Course && r.course_kind != 7);
    if (counted && r.distance_m > 0) s.bspec_distance += s64(r.bspec_distance_m), s.aspec_distance += s64(r.distance_m - r.bspec_distance_m);
    DayEvent day;
    s64 prize = 0;
    switch (r.kind) {
    case RaceResult::Kind::Event: {
        // 0x114498: not finished, no prize, a race counted as such; finished, the place's
        // prize and count, the score of a driven race, and the record when the place is the
        // sequence's best so far.
        day.b2 = 14;
        if (!r.finished) {
            ++s.rank_counts[0];
            day.b1 = 15, day.word = kNoTime;
            fill_rows(true);
        } else {
            const int p = std::clamp(place, 0, 15);
            prize = prize_of(p);
            if (p < 6) ++s.rank_counts[size_t(p) + 1];
            s.total_prize_money += prize;
            if (!r.watched && r.score >= 0) unit_score(w, r.score);
            if (p < s.race_session.best_place) {
                if (unit >= 0) unit_result(w, 2, u32(p));
                s.race_session.best_place = p;
            }
            day.b1 = u8(p), day.word = r.time_ms;
            fill_rows(false);
            work.set("playerGridNumber", s32(p));
        }
        if (race == "family") day.type = 8, day.b3 = 2, day.code = d.code(kCourseTable, r.course.empty() ? s.race.course : r.course);
        else day.type = 3, day.code = race_code;
        work.set("prize", s32(prize));
        break;
    }
    case RaceResult::Kind::Championship: {
        // 0x119CB8 a race of the series, 0x119F28 its last: points by the finishing order
        // (0x3EEFE0; a player who did not finish gets none and the others move up, 0x3EF1E8),
        // the race's prize, its record; on the last race the series is won when no other
        // entrant has as many points as the player (0x3EF298): the series' record and its grand
        // prize (the row's +0x2C x 100); the player's rank among the first six (0x3EF2D8).
        const std::string series = known(r.series);
        if (s.series.label != series) s.series = {series, 0, series_race_count(series), {}};
        auto add_points = [&](bool skip_player) {
            int k = 0;
            for (const RaceResult::Finisher& f : r.order) {
                if (skip_player && f.entry == 0) continue;
                if (f.entry < 0) continue;
                if (size_t(f.entry) >= s.series.entrants.size()) s.series.entrants.resize(size_t(f.entry) + 1);
                GameState::Series::Entrant& e = s.series.entrants[size_t(f.entry)];
                if (e.code == ~u64(0)) e.code = d.code(kCarTable, f.car), e.paint = f.paint;
                e.points += championship_points(d, k);
                ++k;
            }
        };
        ++s.series.races_run;
        const bool last = s.series.races_run >= s.series.race_count;
        day.type = 3, day.code = race_code, day.b2 = 14;
        if (!r.finished) {
            fill_rows(true);
            add_points(true);
            ++s.rank_counts[0];
            day.b1 = 15, day.word = kNoTime;
            place = -1;
        } else {
            add_points(false);
            const int p = std::clamp(place, 0, 15);
            prize = prize_of(p);
            if (p < 6) ++s.rank_counts[size_t(p) + 1];
            if (!r.watched && r.score >= 0) unit_score(w, r.score);
            if (!last && !r.watched && unit >= 0) unit_result(w, 2, u32(p));
            day.b1 = u8(p), day.word = r.time_ms;
            fill_rows(false);
            work.set("playerGridNumber", s32(p));
            // 0x11A168: each row (finishing order) the place's points (+0x37C + 4 i, getWeight: the Pts column) and the
            // entrant's points so far (+0x364 + 4 i, getPower: Total).
            for (size_t i = 0; i < work.rows.size() && i < r.order.size(); ++i) {
                const int entry = r.order[i].entry;
                work.rows[i].weight = championship_points(d, int(i));
                work.rows[i].power = entry >= 0 && size_t(entry) < s.series.entrants.size() ? s.series.entrants[size_t(entry)].points : 0;
            }
        }
        // The session's race (the series' label with the race's number, as the sequence asked for it): the prize
        // page's "<event> Race <n>" reads its last two digits (race_util's getSeriesRaceIndex).
        if (!r.race.empty()) work.race_label = r.race;
        s64 grand = 0;
        if (last) {
            if (s.series.entrants.empty()) s.series.entrants.resize(1);
            const s32 mine = s.series.entrants[0].points;
            bool won = true;
            int rank = 0;
            for (size_t k = 1; k < 6; ++k) {
                const s32 other = k < s.series.entrants.size() ? s.series.entrants[k].points : 0;
                if (other >= mine) won = false, ++rank;
            }
            const int series_unit = d.race_unit(series);
            if (won) {
                const ByteSpan srow = row;
                grand = srow.size() >= 0x2E ? s64(s16(u16(srow[0x2C] | srow[0x2D] << 8))) * 100 : 0;
                if (series_unit >= 0) unit_result(s.race_units[size_t(series_unit)], 2, 0);
                day.b2 = 0;
            }
            if (unit >= 0) unit_result(w, 2, u32(place < 0 ? 15 : place));
            work.set("GrandPrize", s32(grand));
            work.set("SeriesRank", s32(rank));
        }
        s.total_prize_money += prize + grand;
        work.set("prize", s32(prize));
        prize += grand;
        break;
    }
    case RaceResult::Kind::Mission: {
        // A run the player's car did not finish (the pause's Exit, a time-out: its +0x5B6 clear) only goes in the
        // diary (0x11C2E0: the mission's day with place 15, note 15, no time); no prize, record or rows.
        if (!r.finished) {
            day.type = 5, day.b1 = 15, day.b2 = 15, day.word = kNoTime, day.code = race_code;
            break;
        }
        // 0x11C328 (the car finished): the place's prize whatever the place, the record's kind 3 with the place,
        // the score; the diary's mission day (place, note 15, time).
        const int p = std::clamp(place, 0, 15);
        prize = prize_of(p);
        s.total_prize_money += prize;
        if (unit >= 0) unit_result(w, 3, u32(p));
        if (r.score >= 0) unit_score(w, r.score);
        day.type = 5, day.b1 = u8(p), day.b2 = 15, day.word = r.time_ms, day.code = race_code;
        fill_rows(false);
        work.set("playerGridNumber", s32(p));
        work.set("prize", s32(prize));
        break;
    }
    case RaceResult::Kind::License: {
        const u32 result = r.finished ? result_number(r.result) : 15;
        if (!d.db || !d.db->prologue()) {
            // 0x1105A8: only a graded run (the race's grade +0x88: 2 gold, 3 silver, 4 bronze, as results 9, 10, 11) is
            // kept: the test's unit, its ranking (time, result, the real date, the pass code); any other run (failed, not
            // finished, 13) is kept nowhere. The sequence's day takes the graded run whose time beats the sequence's best
            // so far (its result and time, the best then that time); none graded: a day with nothing in it (0x110800).
            if (result < 9 || result > 11) break;
            if (unit >= 0) unit_result(w, 1, result);
            if (r.time_ms < s.race_session.license_best) {
                s.race_session.license_best = r.time_ms;
                day.type = 4, day.b1 = u8(result), day.b2 = 14, day.word = r.time_ms, day.code = race_code;
            }
            const Value args[3] = {Value::integer(s32(r.time_ms)), Value::string(kNames[result]), Value::string(r.pass_code.empty() ? "--------" : r.pass_code)};
            const Value record = vm().attribute(vm().lookup("game"), "license_record");
            const Value label[1] = {Value::string(race)};
            const Value u = record.p ? vm().call(vm().attribute(record, "getUnit"), label) : Value{};
            if (u.p) vm().call(vm().attribute(u, "insertEntry"), args);
            break;
        }
        if (unit >= 0) unit_result(w, 1, result);
        { // Prologue's license (prologue_natives.cpp): the medal, points and time into its record.
            const Value lic = vm().attribute(vm().lookup("game"), "license");
            const Value rec = lic.p ? vm().attribute(lic, "recordResult") : Value{};
            if (rec.kind == script::Kind::Function) {
                std::string car; // (the player's car, and the course: their places for the pass code, prologue_license.h)
                for (const RaceResult::Finisher& f : r.order)
                    if (f.entry == 0) car = f.car;
                const Value args[7] = {Value::string(r.race), Value::string(kNames[result]), Value::integer(r.finished ? s32(r.time_ms) : s32(kNoTime)),
                                       Value::integer(r.finished ? prologue_license_points(row, kNames[result], r.time_ms) : 0),
                                       Value::string(r.name), Value::integer(prologue_table_place(d, "GENERIC_CAR", kCarTable, car)),
                                       Value::integer(prologue_table_place(d, "COURSE", kCourseTable, r.course))};
                vm().call(rec, args);
            }
        }
        day.type = 4, day.b1 = u8(result), day.b2 = 14, day.word = r.finished ? r.time_ms : kNoTime, day.code = race_code;
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
    // Arcade's sequences put no day (0x111498 and 0x112620 reach none of the routines that do). A single race the player
    // drove and finished keeps its A-spec points when they are the course's best (0x111E98: the course of the race's row,
    // its record made when it has none, 0x433C10; 0x4336D0: at most 255), shown on the course page.
    if (r.kind == RaceResult::Kind::Course && r.course_kind == 7) {
        if (s.race_session.sequence == "single_race" && r.finished && !r.watched && r.score >= 0) {
            const Value label[1] = {Value::string(r.course)};
            const Value record = vm().attribute(vm().lookup("game"), "course_record");
            const Value course = record.p ? vm().call(vm().attribute(record, "getUnit"), label) : Value{};
            const Value score[1] = {Value::integer(r.score)};
            if (course.p) vm().call(vm().attribute(course, "updateBestScore"), score);
        }
        return prize;
    }
    // A license test's day is the sequence's, put when it ends (0x110800); a run outside a
    // sequence puts it at once. (Gran Turismo 4's: only a graded run's that beat the sequence's best, above.)
    if (r.kind == RaceResult::Kind::License && s.race_session.license_open) {
        if (day.type == 4) s.race_session.license_day = day;
    } else if (r.kind != RaceResult::Kind::License || day.type == 4)
        s.put_day(day);
    return prize;
}

} // namespace gt4r::menu
