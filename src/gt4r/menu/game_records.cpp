// The calendar and its diary (MCalendar), the records (MRaceRecord, MLicenseRecord,
// MCourseRecord and their units), the presents (MPresent), the used cars sold (MUsedCar) and
// the favorites (MFavorite): the program's natives over GameState's saved blocks
// (docs/formats/save.md; the natives' addresses there and in docs/formats/menu.md).

#include "gt4r/menu/game_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace gt4r::menu {

using script::Args;
using script::Kind;
using script::Value;
using script::Vm;

namespace {

u32 get32(const Bytes& b, size_t o) { return u32(b[o]) | u32(b[o + 1]) << 8 | u32(b[o + 2]) << 16 | u32(b[o + 3]) << 24; }
void put32(Bytes& b, size_t o, u32 v) { b[o] = u8(v), b[o + 1] = u8(v >> 8), b[o + 2] = u8(v >> 16), b[o + 3] = u8(v >> 24); }
u64 get64(const Bytes& b, size_t o) { return u64(get32(b, o)) | u64(get32(b, o + 4)) << 32; }
void put64(Bytes& b, size_t o, u64 v) { put32(b, o, u32(v)), put32(b, o + 4, u32(v >> 32)); }

// The result names (0x6A55C8), shared by a race unit's three results and a ranking's entries.
const char* const kResults[16] = {"1", "2", "3", "4", "5", "6", "7", "8", "abandon", "gold", "silver", "bronze", "red", "failed", "view", "NONE"};
int result_number(const Value& v) {
    if (v.kind == Kind::Int) return v.i & 15;
    const std::string s = v.is_nil() ? std::string() : v.to_string();
    for (int i = 0; i < 16; ++i)
        if (s == kResults[i]) return i;
    return 15;
}
// A unit's kinds (0x622D80).
const char* const kUnitTypes[4] = {"none", "license", "event", "mission"};
int unit_type(const std::string& s) {
    for (int i = 0; i < 4; ++i)
        if (s == kUnitTypes[i]) return i;
    return 15;
}
// The diary's names: day events (0x622D58), how a car was got (0x6A5138), the notes (0x6A56A8),
// the wheels' categories (0x6A5180), the kinds of a course's run (0x6A51F8).
const char* const kEventTypes[9] = {"NO_EVENT", "GET_CAR", "SELL_CAR", "RUN_RACE", "RUN_LICENSE", "RUN_MISSION", "BUY_WHEEL", "BUY_WING", "RUN_COURSE"};
const char* const kGetKinds[5] = {"NONE", "BUY_NEW", "BUY_USED", "BUY_TRADE", "GET_PRESENT"};
const char* const kNotes[15] = {"1", "2", "3", "4", "5", "6", "7", "8", "license_0b", "license_0a", "license_ib", "license_ia", "license_0s", "new_record_time", "NONE"};
const char* const kWheelKinds[5] = {"NONE", "NORMAL", "RACING", "DIRT", "SNOW"};
const char* const kRunKinds[8] = {"NONE", "FREERUN", "EVENT", "PRACTICE", "PHOTO", "TRAVEL", "MACHINE_TEST", "TIME_TRIAL"};
template <size_t N>
int name_number(const char* const (&names)[N], const Value& v, int none) {
    if (v.kind == Kind::Int) return v.i;
    const std::string s = v.is_nil() ? std::string() : v.to_string();
    for (size_t i = 0; i < N; ++i)
        if (s == names[i]) return int(i);
    return none;
}
template <size_t N>
const char* name_of(const char* const (&names)[N], int i) {
    return i >= 0 && size_t(i) < N ? names[i] : "";
}

// The real clock's date for a ranking entry (0x548918, 0x579BD8).
void today_on_clock(u16& y, u8& m, u8& d) {
    const std::time_t t = std::time(nullptr);
    const std::tm tm = *std::localtime(&t);
    y = u16(tm.tm_year + 1900), m = u8(tm.tm_mon + 1), d = u8(tm.tm_mday);
}

std::string date_text(const Bytes& b, size_t o) {
    const u16 y = u16(b[o] | b[o + 1] << 8);
    if (!y) return "-/-/-";
    char t[32];
    std::snprintf(t, sizeof t, "%d/%d/%d", y, b[o + 2], b[o + 3]);
    return t;
}

// The license tests' units (0x4323D0): "us" dropped; l + category + number: 0b, 0a, ib, ia,
// 0s by 16, then the coffee breaks (0c) and the missions (0m).
int license_unit(std::string label) {
    if (label.size() >= 8) label = label.substr(2);
    if (label.size() < 4 || label[0] != 'l') return -1;
    static const char* const cats[7] = {"0b", "0a", "ib", "ia", "0s", "0c", "0m"};
    int cat = -1;
    for (int i = 0; i < 7; ++i)
        if (label.compare(1, 2, cats[i]) == 0) cat = i;
    if (cat < 0) return -1;
    const int n = std::atoi(label.c_str() + 3);
    const int unit = cat < 5 ? cat * 16 + n - 1 : cat == 5 ? 79 + n : 84 + n;
    return unit >= 0 && unit < int(kLicenseUnits) ? unit : -1;
}

// Every test 1 .. `last` of a license's category ("l0b", ...) by a rule on its unit's type and result (0x430B58 on).
template <class Rule> bool all_tests(Database& d, const GameState& s, const std::string& cat, int last, Rule rule) {
    for (int i = 1; i <= last; ++i) {
        char label[16];
        std::snprintf(label, sizeof label, "%s%04d", cat.c_str(), i);
        const int unit = d.race_unit(label);
        if (unit < 0) return false;
        const u32 w = s.race_units[size_t(unit)];
        if (!rule(w & 15, (w >> 4) & 15)) return false;
    }
    return true;
}

} // namespace

bool license_all_bronze(Database& d, const GameState& s, const std::string& cat) {
    return all_tests(d, s, cat, 16, [](u32, u32 r) { return r != 15 && r <= 11; });
}

bool event_aspec_allowed(Database& d, const GameState& s, u8 license) {
    static const char* const kCategories[5] = {"l0b", "l0a", "lib", "lia", "l0s"};
    if (license == 0) return true;
    return license <= 5 && license_all_bronze(d, s, kCategories[license - 1]);
}

// The unit of a course (0x433C10): the units kept in order of the course's code, one made there when `make`.
int course_record_unit(Database& d, GameState& s, const std::string& label, bool make) {
    const u64 code = d.code(kCourseTable, label);
    if (code == kNoCode) return -1;
    Bytes& b = s.course_record;
    size_t i = 0;
    for (; i < kCourseUnits; ++i) {
        const u64 c = get64(b, i * kCourseUnit);
        if (c == code) return int(i);
        if (c == kNoCode || c > code) break;
    }
    if (!make || i >= kCourseUnits || get64(b, (kCourseUnits - 1) * kCourseUnit) != kNoCode) return -1;
    std::memmove(b.data() + (i + 1) * kCourseUnit, b.data() + i * kCourseUnit, (kCourseUnits - 1 - i) * kCourseUnit);
    // A fresh unit, as a new game has them.
    GameState fresh;
    std::memcpy(b.data() + i * kCourseUnit, fresh.course_record.data(), kCourseUnit);
    put64(b, i * kCourseUnit, code);
    return int(i);
}

// A ranking's insert (0x433260 by time: before the first entry empty or slower; 0x433348 by top speed: before the first
// slower): the entries after it moved down, the tenth dropped; the entry: the value, the real clock's date, the car's code,
// the pass code (32 bytes).
bool ranking_insert(Database& d, Bytes& b, size_t base, bool by_speed, u32 v, const std::string& car, const std::string& pass) {
    size_t at = 0;
    for (; at < 10; ++at) {
        const u32 w = get32(b, base + 8 + at * 0x30);
        if (by_speed) {
            float have, want;
            std::memcpy(&have, &w, 4), std::memcpy(&want, &v, 4);
            if (have < want) break;
        } else if (w == kNoTime || w > v) break;
    }
    if (at >= 10) return false;
    std::memmove(b.data() + base + 8 + (at + 1) * 0x30, b.data() + base + 8 + at * 0x30, (9 - at) * 0x30);
    const size_t o = base + 8 + at * 0x30;
    std::memset(b.data() + o, 0, 0x30);
    put32(b, o, v);
    u16 y;
    u8 m, dd;
    today_on_clock(y, m, dd);
    b[o + 4] = u8(y), b[o + 5] = u8(y >> 8), b[o + 6] = m, b[o + 7] = dd;
    put64(b, o + 8, d.code(kCarTable, car));
    std::memcpy(b.data() + o + 0x10, pass.data(), std::min<size_t>(pass.size(), 32));
    return true;
}

void install_records(GameNatives& n, Object& g, script::Module& menu) {
    GameNatives* np = &n;
    GameState* s = &n.state();
    auto db = [np]() -> Database& { return np->db(); };

    // ---- The calendar (GranTurismo4::Calendar; save.md "The calendar").
    auto calendar = std::make_shared<Object>("MCalendar");
    calendar->property("today", [s] {
        int y, m, d;
        day_to_date(s->today, y, m, d);
        return Value::array({number(y), number(m), number(d)});
    });
    // Days since the first; with n: modulo n (the used wheels' lists).
    calendar->method("getElapsedDate", [s](Vm&, const Value&, Args a) {
        const s32 e = s32(s->today - GameState::kFirstDay), m = arg_int(a, 0);
        return number(m > 0 ? e % m : e);
    });
    calendar->method("getElapsedWeek", [s](Vm&, const Value&, Args a) {
        const s32 w = s32(s->today - GameState::kFirstDay) / 7, m = arg_int(a, 0);
        return number(m > 0 ? w % m : w);
    });
    // Every event put is a day passing (0x42F0E0); incDate is a day with nothing in it.
    calendar->method("incDate", [s](Vm&, const Value&, Args) {
        s->put_day(DayEvent{});
        return Value{};
    });
    auto put_car = [s, db](u8 kind) {
        return [s, db, kind](Vm&, const Value&, Args a) {
            DayEvent e;
            e.type = 1, e.b1 = u8(arg_int(a, 1)), e.b2 = kind, e.code = db().code(kCarTable, arg_text(a, 0));
            s->put_day(e);
            return Value{};
        };
    };
    calendar->method("putBuyCarEvent", put_car(1));
    calendar->method("putBuyNewCarEvent", put_car(1));
    calendar->method("putBuyUsedCarEvent", put_car(2));
    calendar->method("putBuyTradeCarEvent", put_car(3));
    calendar->method("putPresentCarEvent", put_car(4));
    calendar->method("putSellCarEvent", [s, db](Vm&, const Value&, Args a) {
        DayEvent e;
        const u32 price = u32(std::clamp<s32>(arg_int(a, 2), 0, 0xFFFF));
        e.type = 2, e.b1 = u8(arg_int(a, 1)), e.b2 = u8(price), e.b3 = u8(price >> 8), e.code = db().code(kCarTable, arg_text(a, 0));
        s->put_day(e);
        return Value{};
    });
    auto put_run = [s, db](u8 type) {
        return [s, db, type](Vm&, const Value&, Args a) {
            DayEvent e;
            e.type = type, e.b1 = u8(result_number(a.size() > 1 ? a[1] : Value{}));
            e.b2 = u8(name_number(kNotes, a.size() > 2 ? a[2] : Value{}, 14)), e.word = u32(arg_int(a, 3, s32(kNoTime)));
            e.code = db().code(kRaceTable, arg_text(a, 0));
            s->put_day(e);
            return Value{};
        };
    };
    calendar->method("putRunRaceEvent", put_run(3));
    calendar->method("putRunLicenseEvent", put_run(4));
    calendar->method("putRunMissionEvent", put_run(5));
    calendar->method("putBuyWheelEvent", [s, db](Vm&, const Value&, Args a) {
        DayEvent e;
        e.type = 6, e.b1 = u8(name_number(kWheelKinds, a.size() > 1 ? a[1] : Value{}, 0)), e.b2 = u8(arg_int(a, 2));
        e.code = db().code(kWheelTable, arg_text(a, 0));
        s->put_day(e);
        return Value{};
    });
    calendar->method("putBuyWingEvent", [s, db](Vm&, const Value&, Args a) {
        DayEvent e;
        e.type = 7, e.code = db().code(kWingTable, arg_text(a, 0));
        s->put_day(e);
        return Value{};
    });
    // putRunCourseEvent(course, kind, result, note, time); putRunFreeRunEvent(course, result,
    // note, time) is the kind FREERUN.
    auto put_course = [s, db](bool free_run) {
        return [s, db, free_run](Vm&, const Value&, Args a) {
            const size_t k = free_run ? 0 : 1;
            DayEvent e;
            e.type = 8, e.b3 = u8(free_run ? 1 : name_number(kRunKinds, a.size() > 1 ? a[1] : Value{}, 0));
            e.b1 = u8(result_number(a.size() > k + 1 ? a[k + 1] : Value{})), e.b2 = u8(name_number(kNotes, a.size() > k + 2 ? a[k + 2] : Value{}, 14));
            e.word = u32(arg_int(a, k + 3, s32(kNoTime))), e.code = db().code(kCourseTable, arg_text(a, 0));
            s->put_day(e);
            return Value{};
        };
    };
    calendar->method("putRunCourseEvent", put_course(false));
    calendar->method("putRunFreeRunEvent", put_course(true));
    // getEvent(y, m, d): the day's event as an array by its type; nil for today, a day to come
    // or one the diary no longer holds (0x42F060).
    calendar->method("getEvent", [s, db](Vm&, const Value&, Args a) {
        const s64 back = s64(s->today) - s64(date_to_day(arg_int(a, 0), arg_int(a, 1), arg_int(a, 2)));
        if (back <= 0 || back > s64(s->diary.size())) return Value{};
        const DayEvent& e = s->diary[s->diary.size() - size_t(back)];
        const Value type = text(name_of(kEventTypes, e.type));
        switch (e.type) {
        case 1: return Value::array({type, text(db().label(e.code)), number(e.b1), text(name_of(kGetKinds, e.b2))});
        case 2: return Value::array({type, text(db().label(e.code)), number(e.b1), number(e.b2 | e.b3 << 8)});
        case 3:
        case 4:
        case 5: return Value::array({type, text(db().label(e.code)), text(name_of(kResults, e.b1 & 15)), text(name_of(kNotes, e.b2)), text(time_text(e.word))});
        case 6: return Value::array({type, text(db().label(e.code)), text(name_of(kWheelKinds, e.b1)), number(e.b2)});
        case 7: return Value::array({type, e.code == kNoCode ? Value{} : text(db().label(e.code))});
        case 8:
            return Value::array({type, text(db().label(e.code)), text(name_of(kRunKinds, e.b3)), text(name_of(kResults, e.b1 & 15)), text(name_of(kNotes, e.b2)),
                                 text(time_text(e.word))});
        default: return Value::array({type});
        }
    });
    const Value calendar_value = object_value(calendar);
    g.set("calendar", calendar_value);
    {
        auto cls = make_class(menu, "MCalendar");
        cls->construct = [calendar_value](Vm&, Args) { return calendar_value; };
        static_fn(*cls, "dateToDay", [](Vm&, const Value&, Args a) { return number(s32(date_to_day(arg_int(a, 0), arg_int(a, 1), arg_int(a, 2)))); });
        static_fn(*cls, "dayToDate", [](Vm&, const Value&, Args a) {
            int y, m, d;
            day_to_date(u32(arg_int(a, 0)), y, m, d);
            return Value::array({number(y), number(m), number(d)});
        });
        static_fn(*cls, "dayToWeekDay", [](Vm&, const Value&, Args a) { return number(((arg_int(a, 0) - 5) % 7 + 7) % 7); });
        static_fn(*cls, "dayToGameDay", [](Vm&, const Value&, Args a) { return number(arg_int(a, 0) - s32(GameState::kFirstDay)); });
        static_fn(*cls, "gameDayToDay", [](Vm&, const Value&, Args a) { return number(arg_int(a, 0) + s32(GameState::kFirstDay)); });
    }

    // ---- The race record: a word a race (save.md "Records").
    auto race_unit = [s](int unit) {
        auto u = std::make_shared<Object>("MRaceRecordUnit");
        u32* w = &s->race_units[size_t(unit)];
        auto bits = [w](int at) {
            return std::make_pair([w, at] { return text(kResults[(*w >> at) & 15]); },
                                  [w, at](const Value& v) { *w = (*w & ~(15u << at)) | u32(result_number(v)) << at; });
        };
        u->property("type", [w] { return (*w & 15) < 4 ? text(kUnitTypes[*w & 15]) : text(""); }, [w](const Value& v) {
            *w = (*w & ~15u) | u32(unit_type(v.is_nil() ? std::string() : v.to_string()) & 15);
        });
        for (const auto& [name, at] : {std::pair<const char*, int>{"result", 4}, {"present_result", 8}, {"spot_result", 12}}) {
            auto [get, set] = bits(at);
            u->property(name, get, set);
        }
        u->method("refreshResult", [w](Vm&, const Value&, Args) {
            *w |= 15u << 12;
            return Value{};
        });
        // update(type, result) (0x430A58): the first run sets the type; a better result is kept.
        u->method("update", [w](Vm&, const Value&, Args a) {
            const u32 t = u32(unit_type(arg_text(a, 0)) & 15), r = u32(result_number(a.size() > 1 ? a[1] : Value{}));
            const u32 have = *w & 15;
            if (have == 0) {
                *w = (*w & ~0xFFFu) | t | r << 4 | r << 8;
                return number(1);
            }
            if (have != t) return number(0);
            *w = (*w & ~(15u << 8)) | r << 8;
            if (r < ((*w >> 4) & 15)) {
                *w = (*w & ~(15u << 4)) | r << 4;
                return number(1);
            }
            return number(0);
        });
        u->property("best_score", [w] { return number(s32((*w >> 16) & 0xFF)); });
        u->method("updateBestScore", [w](Vm&, const Value&, Args a) {
            const u32 v = u32(std::clamp(arg_int(a, 0), 0, 255));
            if (((*w >> 16) & 0xFF) < v) *w = (*w & ~(0xFFu << 16)) | v << 16;
            return Value{};
        });
        return object_value(u);
    };
    auto race_record = std::make_shared<Object>("MRaceRecord");
    race_record->method("getUnit", [db, race_unit](Vm&, const Value&, Args a) {
        const int unit = db().race_unit(arg_text(a, 0));
        return unit < 0 ? Value{} : race_unit(unit);
    });
    // The license predicates: tests 1 .. 16 (or 15) of a category ("l0b", ...), each by a rule.
    auto all = [s, db](const std::string& cat, int last, auto rule) { return all_tests(db(), *s, cat, last, rule); };
    race_record->method("isLicensePerfect", [all](Vm&, const Value&, Args a) {
        return number(all(arg_text(a, 0), 16, [](u32 t, u32 r) { return t == 1 ? r == 9 : (t == 2 || t == 3) && r == 0; }));
    });
    race_record->method("isLicenseAllSilver", [all](Vm&, const Value&, Args a) {
        return number(all(arg_text(a, 0), 16, [](u32 t, u32 r) { return r != 15 && (t == 1 ? r < 11 : (t == 2 || t == 3) && r < 2); }));
    });
    race_record->method("isLicensePassed", [all](Vm&, const Value&, Args a) {
        return number(all(arg_text(a, 0), 16, [](u32 t, u32 r) { return t == 1 ? r < 13 : (t == 2 || t == 3) && r == 0; }));
    });
    race_record->method("isLicenseAllBronze", [s, db](Vm&, const Value&, Args a) { return number(license_all_bronze(db(), *s, arg_text(a, 0))); });
    race_record->method("canTryLastLicense", [all](Vm&, const Value&, Args a) { return number(all(arg_text(a, 0), 15, [](u32, u32 r) { return r != 15 && r <= 11; })); });
    // isMissionClear("from,to", result): every mission of the range at least that result.
    race_record->method("isMissionClear", [s, db](Vm&, const Value&, Args a) {
        const std::string range = arg_text(a, 0);
        const size_t comma = range.find(',');
        const int from = std::atoi(range.c_str()), to = comma == std::string::npos ? from : std::atoi(range.c_str() + comma + 1);
        const u32 want = u32(result_number(a.size() > 1 ? a[1] : Value{}));
        for (int i = from; i <= to; ++i) {
            char label[16];
            std::snprintf(label, sizeof label, "l0m%04d", i);
            const int unit = db().race_unit(label);
            if (unit < 0) return number(0);
            const u32 r = (s->race_units[size_t(unit)] >> 4) & 15;
            if (r == 15 || r > want) return number(0);
        }
        return number(1);
    });
    g.set("race_record", object_value(race_record));

    // ---- The license tests' rankings (0x1F8 a test: 10 entries of 0x2C, best first).
    auto license_unit_value = [s](int unit) {
        auto u = std::make_shared<Object>("MLicenseRecordUnit");
        const size_t base = size_t(unit) * kLicenseUnit;
        Bytes* b = &s->license_record;
        auto entry = [base](s32 i) { return base + size_t(std::clamp(i, 0, 9)) * 0x2C; };
        u->method("getRankNum", [b, base](Vm&, const Value&, Args) {
            int n = 0;
            for (size_t e = 0; e < 10; ++e) n += get32(*b, base + e * 0x2C) != kNoTime;
            return number(n);
        });
        u->method("getBestTime", [b, entry](Vm&, const Value&, Args a) { return text(arg_int(a, 0) >= 10 ? time_text(kNoTime) : time_text(get32(*b, entry(arg_int(a, 0))))); });
        u->method("getDate", [b, entry](Vm&, const Value&, Args a) { return text(date_text(*b, entry(arg_int(a, 0)) + 4)); });
        u->method("getResult", [b, entry](Vm&, const Value&, Args a) { return text(kResults[(*b)[entry(arg_int(a, 0)) + 8] & 15]); });
        u->method("getPassCode", [b, entry](Vm&, const Value&, Args a) {
            const size_t o = entry(arg_int(a, 0)) + 9 + 8 * size_t(std::clamp(arg_int(a, 1), 0, 3));
            return text(std::string(reinterpret_cast<const char*>(b->data() + o), strnlen(reinterpret_cast<const char*>(b->data() + o), 8)));
        });
        // insertEntry(time, result[, pass code]) (0x4320B8): in order of time; the real date.
        u->method("insertEntry", [b, base](Vm&, const Value&, Args a) {
            const u32 time = u32(arg_int(a, 0, s32(kNoTime)));
            size_t at = 0;
            while (at < 10 && get32(*b, base + at * 0x2C) != kNoTime && get32(*b, base + at * 0x2C) <= time) ++at;
            if (at >= 10) return number(0);
            std::memmove(b->data() + base + (at + 1) * 0x2C, b->data() + base + at * 0x2C, (9 - at) * 0x2C);
            const size_t o = base + at * 0x2C;
            std::memset(b->data() + o, 0, 0x2C);
            put32(*b, o, time);
            u16 y;
            u8 m, d;
            today_on_clock(y, m, d);
            (*b)[o + 4] = u8(y), (*b)[o + 5] = u8(y >> 8), (*b)[o + 6] = m, (*b)[o + 7] = d;
            (*b)[o + 8] = u8(result_number(a.size() > 1 ? a[1] : Value{}));
            const std::string pass = arg_text(a, 2, "--------");
            std::memcpy(b->data() + o + 9, pass.data(), std::min<size_t>(pass.size(), 32));
            return number(1);
        });
        u->method("deleteEntry", [b, base](Vm&, const Value&, Args a) {
            const s32 i = arg_int(a, 0, -1);
            if (i < 0 || i >= 10) return Value{};
            std::memmove(b->data() + base + size_t(i) * 0x2C, b->data() + base + size_t(i + 1) * 0x2C, size_t(9 - i) * 0x2C);
            const size_t o = base + 9 * 0x2C;
            std::memset(b->data() + o, 0, 0x2C);
            put32(*b, o, kNoTime), (*b)[o + 8] = 15, std::memset(b->data() + o + 9, '-', 8);
            return Value{};
        });
        return object_value(u);
    };
    auto license_record = std::make_shared<Object>("MLicenseRecord");
    license_record->method("getUnit", [license_unit_value](Vm&, const Value&, Args a) {
        const int unit = license_unit(arg_text(a, 0));
        return unit < 0 ? Value{} : license_unit_value(unit);
    });
    g.set("license_record", object_value(license_record));

    // ---- The courses' rankings: units kept in order of the course's code, made when first
    // asked (0x433C10, 0x433AA0); the machine tests' four (0x433C98).
    auto course_unit_value = [s, db](Bytes* b, size_t base, bool speed) {
        auto u = std::make_shared<Object>("MCourseRecordUnit");
        auto entry = [base](s32 i) { return base + 8 + size_t(std::clamp(i, 0, 9)) * 0x30; };
        auto used = [b, base, speed] {
            int n = 0;
            for (size_t e = 0; e < 10; ++e) {
                const u32 v = get32(*b, base + 8 + e * 0x30);
                n += speed ? v != 0 : v != kNoTime;
            }
            return n;
        };
        u->method("getRankNum", [used](Vm&, const Value&, Args) { return number(used()); });
        u->method("getRankNumMaxSpeed", [used](Vm&, const Value&, Args) { return number(used()); });
        u->method("getBestTime", [b, entry](Vm&, const Value&, Args a) { return text(time_text(get32(*b, entry(arg_int(a, 0))))); });
        u->method("getBestSpeed", [b, entry, s](Vm&, const Value&, Args a) {
            float v;
            const u32 bits = get32(*b, entry(arg_int(a, 0)));
            std::memcpy(&v, &bits, 4);
            const GameState::Unit unit = s->velocity_unit();
            char t[32];
            if (v <= 0) std::snprintf(t, sizeof t, "--%s", unit.name);
            else std::snprintf(t, sizeof t, "%d%s", int(v * 3.6f * unit.factor), unit.name);
            return text(t);
        });
        u->method("getDate", [b, entry](Vm&, const Value&, Args a) { return text(date_text(*b, entry(arg_int(a, 0)) + 4)); });
        u->method("getCarName", [b, entry, db](Vm&, const Value&, Args a) {
            const std::string label = db().label(get64(*b, entry(arg_int(a, 0)) + 8));
            const db::CarEntry* c = db().car(label);
            return text(c ? c->name : std::string());
        });
        u->method("getPassCode", [b, entry](Vm&, const Value&, Args a) {
            const size_t o = entry(arg_int(a, 0)) + 0x10 + 8 * size_t(std::clamp(arg_int(a, 1), 0, 3));
            return text(std::string(reinterpret_cast<const char*>(b->data() + o), strnlen(reinterpret_cast<const char*>(b->data() + o), 8)));
        });
        // insertEntry(time, car[, pass code]) / insertEntryMaxSpeed(speed, car[, pass code]).
        auto insert = [b, base, db](bool by_speed) {
            return [b, base, db, by_speed](Vm&, const Value&, Args a) {
                u32 v;
                if (by_speed) {
                    const float f = arg_number(a, 0);
                    std::memcpy(&v, &f, 4);
                } else v = u32(arg_int(a, 0, s32(kNoTime)));
                return number(ranking_insert(db(), *b, base, by_speed, v, arg_text(a, 1), arg_text(a, 2, "--------")));
            };
        };
        u->method("insertEntry", insert(false));
        u->method("insertEntryMaxSpeed", insert(true));
        auto erase = [b, base](bool by_speed) {
            return [b, base, by_speed](Vm&, const Value&, Args a) {
                const s32 i = arg_int(a, 0, -1);
                if (i < 0 || i >= 10) return Value{};
                std::memmove(b->data() + base + 8 + size_t(i) * 0x30, b->data() + base + 8 + size_t(i + 1) * 0x30, size_t(9 - i) * 0x30);
                const size_t o = base + 8 + 9 * 0x30;
                std::memset(b->data() + o, 0, 0x30);
                put32(*b, o, by_speed ? 0 : kNoTime), put64(*b, o + 8, kNoCode), std::memset(b->data() + o + 0x10, '-', 8);
                return Value{};
            };
        };
        u->method("deleteEntry", erase(false));
        u->method("deleteEntryMaxSpeed", erase(true));
        u->property("best_score", [b, base] { return number((*b)[base + 0x230]); });
        u->method("updateBestScore", [b, base](Vm&, const Value&, Args a) {
            const u8 v = u8(std::clamp(arg_int(a, 0), 0, 255));
            if ((*b)[base + 0x230] < v) (*b)[base + 0x230] = v;
            return Value{};
        });
        return object_value(u);
    };
    auto course_unit = [s, db](const std::string& label, bool make) { return course_record_unit(db(), *s, label, make); };
    auto course_record = std::make_shared<Object>("MCourseRecord");
    course_record->method("getUnit", [s, course_unit, course_unit_value](Vm&, const Value&, Args a) {
        const int unit = course_unit(arg_text(a, 0), true);
        return unit < 0 ? Value{} : course_unit_value(&s->course_record, size_t(unit) * kCourseUnit, false);
    });
    course_record->method("create", [course_unit](Vm&, const Value&, Args a) { return number(course_unit(arg_text(a, 0), true) >= 0); });
    g.set("course_record", object_value(course_record));
    // ❓ The four machine tests' names (a table at 0x846320) are not read: by the RACE labels'
    // order, 400 m, 1000 m, the top speed (the unit kept as speeds), and a fourth.
    auto machine_record = std::make_shared<Object>("MCourseRecord");
    machine_record->method("getMTUnit", [s, course_unit_value](Vm&, const Value&, Args a) {
        const std::string name = arg_text(a, 0);
        const int unit = name.find("400") != std::string::npos ? 0 : name.find("1000") != std::string::npos ? 1 : name.find("max") != std::string::npos ? 2 : 3;
        return course_unit_value(&s->machine_record, size_t(unit) * kCourseUnit, unit == 2);
    });
    g.set("machine_record", object_value(machine_record));

    // ---- Presents given: a bit a race (0 .. 867), then a bit a time present (0x439DC8 on).
    auto present = std::make_shared<Object>("MPresent");
    auto bit = [s](size_t n) -> std::pair<u32*, u32> { return {&s->presents[std::min<size_t>(n >> 5, s->presents.size() - 1)], 1u << (n & 31)}; };
    present->method("getByRace", [bit](Vm&, const Value&, Args a) {
        auto [w, m] = bit(size_t(std::max(arg_int(a, 0), 0)));
        return number((*w & m) != 0);
    });
    present->method("setByRace", [bit](Vm&, const Value&, Args a) {
        auto [w, m] = bit(size_t(std::max(arg_int(a, 0), 0)));
        *w |= m;
        return Value{};
    });
    present->method("getByTime", [bit](Vm&, const Value&, Args a) {
        auto [w, m] = bit(size_t(std::max(arg_int(a, 0), 0)) + 868);
        return number((*w & m) != 0);
    });
    present->method("setByTime", [bit](Vm&, const Value&, Args a) {
        auto [w, m] = bit(size_t(std::max(arg_int(a, 0), 0)) + 868);
        *w |= m;
        return Value{};
    });
    present->method("dump", [](Vm&, const Value&, Args) { return Value{}; });
    g.set("present", object_value(present));

    // ---- Used cars sold this week: a bit a car of the week's lot (0x43B608 on).
    auto used = std::make_shared<Object>("MUsedCar");
    auto week = [s](s32 w) {
        if (u32(w) != s->used_week) s->used_sold = {}, s->used_week = u32(w);
    };
    used->method("get", [s, week](Vm&, const Value&, Args a) {
        week(arg_int(a, 0));
        const s32 id = std::clamp(arg_int(a, 1), 0, 255);
        return number((s->used_sold[size_t(id) >> 5] >> (id & 31)) & 1);
    });
    used->method("set", [s, week](Vm&, const Value&, Args a) {
        week(arg_int(a, 0));
        const s32 id = std::clamp(arg_int(a, 1), 0, 255);
        s->used_sold[size_t(id) >> 5] |= 1u << (id & 31);
        return Value{};
    });
    used->property("week", [s] { return number(s32(s->used_week)); }, [s](const Value& v) { s->used_week = u32(v.kind == Kind::Int ? v.i : s32(v.as_float())); });
    g.set("used_car", object_value(used));

    // ---- Favorites: in the order added, 30 at most (0x4303B0 on).
    auto favorite = [](std::vector<std::string>* list) {
        auto f = std::make_shared<Object>("MFavorite");
        f->method("clear", [list](Vm&, const Value&, Args) {
            list->clear();
            return Value{};
        });
        f->method("has", [list](Vm&, const Value&, Args a) { return number(std::find(list->begin(), list->end(), arg_text(a, 0)) != list->end()); });
        f->method("append", [list](Vm&, const Value&, Args a) {
            const std::string l = arg_text(a, 0);
            if (l.empty() || std::find(list->begin(), list->end(), l) != list->end() || list->size() >= GameState::kFavoriteLimit) return number(-1);
            list->push_back(l);
            return number(s32(list->size()) - 1);
        });
        f->method("remove", [list](Vm&, const Value&, Args a) {
            const auto it = std::find(list->begin(), list->end(), arg_text(a, 0));
            if (it != list->end()) list->erase(it);
            return Value{};
        });
        f->method("buildLabelList", [list](Vm&, const Value&, Args) {
            std::vector<Value> v;
            for (const std::string& l : *list) v.push_back(text(l));
            return Value::array(std::move(v));
        });
        f->method("getSize", [list](Vm&, const Value&, Args) { return number(s32(list->size())); });
        f->method("isFull", [list](Vm&, const Value&, Args) { return number(list->size() >= GameState::kFavoriteLimit); });
        f->method("dump", [](Vm&, const Value&, Args) { return Value{}; });
        return object_value(f);
    };
    g.set("favorite_car", favorite(&s->favorite_cars));
    g.set("favorite_course", favorite(&s->favorite_courses));
}

} // namespace gt4r::menu
