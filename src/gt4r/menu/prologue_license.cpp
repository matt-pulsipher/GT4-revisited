// Gran Turismo 4 Prologue's license record (prologue_license.h): the 48 units in the program's layout, the natives its
// scripts call (MLicense 0x15B4D0, MLicenseUnit 0x1582C0), a run's result as the license race's end records it (0x19ED58),
// and the units' bytes for the saved game. docs/formats/prologue.md "The license tests".

#include "gt4r/menu/prologue_license.h"

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/prologue_passcode.h"
#include "gt4r/menu/runtime_internal.h"

#include "disc/core_image.h"
#include "gt4r/race/race_display.h"
#include "gt4r/sim/program_prologue.h"

#include <algorithm>
#include <cstring>
#include <ctime>

namespace gt4r::menu {

using script::Args;
using script::Value;
using script::Vm;

namespace {

constexpr size_t kUnitStride = 0x2E8, kEntries = 10, kEntryBytes = 0x48, kEntryBase = 0x10;
constexpr u32 kNoTimeMs = 359'999'999;
// The program's name lists (pointers to text) and the empty pass code (0x3FDF80 copies it).
constexpr u32 kCategoryNames = 0x504FD0, kCategoryCount = 13, kMedalNames = 0x505008, kMedalCount = 4, kNoPassCode = 0x573200;

// The record: the units as the program keeps them, the names of its lists, the name the results page offers.
struct LicenseRecord : Object {
    LicenseRecord() : Object("MPrologueLicense") {}
    std::array<u8, kPrologueLicenseUnits * kUnitStride> bytes{};
    std::vector<std::string> categories, medals;
    std::string no_pass_code;
    std::optional<PassCoder> coder;
    std::string default_name;

    u8* unit(size_t i) { return bytes.data() + i * kUnitStride; }
    const u8* unit(size_t i) const { return bytes.data() + i * kUnitStride; }
    static u32 get32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
    static void put32(u8* p, u32 v) {
        for (int k = 0; k < 4; ++k) p[k] = u8(v >> (8 * k));
    }
    static u8* entry(u8* u, size_t k) { return u + kEntryBase + k * kEntryBytes; }
    static const u8* entry(const u8* u, size_t k) { return u + kEntryBase + k * kEntryBytes; }
    static std::string text(const u8* p, size_t n) { return std::string(reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), n)); }

    // 0x3FDF80: an empty entry.
    void clear_entry(u8* e) const {
        e[0] = 0;
        put32(e + 0x10, kNoTimeMs);
        std::memset(e + 0x18, 0, 8);
        put32(e + 0x20, 0);
        std::memset(e + 0x24, 0, 9);
        std::memcpy(e + 0x24, no_pass_code.data(), std::min<size_t>(no_pass_code.size(), 8));
    }
    // 0x3FDFC0: a unit of number n with nothing in it.
    void clear_unit(size_t i) {
        u8* u = unit(i);
        put32(u, 0), put32(u + 4, 0), put32(u + 8, 0);
        for (size_t k = 0; k < kEntries; ++k) clear_entry(entry(u, k));
        put32(u + 0x2E0, u32(i));
    }
    // 0x3FE3B0: the place a time takes in the unit's ranking: the first entry empty or slower (an equal time goes after);
    // -1 when all ten are as fast.
    int rank_of(size_t i, u32 t) const {
        for (size_t k = 0; k < kEntries; ++k) {
            const u32 e = get32(entry(unit(i), k) + 0x10);
            if (e == kNoTimeMs || t < e) return int(k);
        }
        return -1;
    }
    // 0x3FE548: the entries with a time.
    int ranked(size_t i) const {
        int n = 0;
        for (size_t k = 0; k < kEntries; ++k) n += get32(entry(unit(i), k) + 0x10) != kNoTimeMs;
        return n;
    }
    static int index_of(const std::vector<std::string>& names, const std::string& s) {
        for (size_t k = 0; k < names.size(); ++k)
            if (names[k] == s) return int(k);
        return 0;
    }
};

std::shared_ptr<LicenseRecord> record_of(const Object& game) {
    const Value v = game.get("license");
    if (v.kind != script::Kind::Object || !v.p) return nullptr;
    return std::dynamic_pointer_cast<LicenseRecord>(v.p);
}

// The program's text at `address` (its image loaded at `load`).
std::string program_text(ByteSpan image, u32 load, u32 address) {
    if (address < load || address - load >= image.size()) return {};
    const size_t at = address - load;
    return std::string(reinterpret_cast<const char*>(image.data() + at), strnlen(reinterpret_cast<const char*>(image.data() + at), image.size() - at));
}
std::vector<std::string> program_names(ByteSpan image, u32 load, u32 list, u32 count) {
    std::vector<std::string> out;
    for (u32 k = 0; k < count; ++k) {
        const u32 at = list + 4 * k;
        if (at < load || at + 4 - load > image.size()) return {};
        out.push_back(program_text(image, load, LicenseRecord::get32(image.data() + (at - load))));
    }
    return out;
}

// The seconds counted from Julian day 0 at noon of the console's clock (0x4762A8: the clock's seconds from 2000-01-01
// 0:00 plus the Julian day count of that moment, 2,451,544.5 days; the program's clock is local time).
u64 clock_seconds() {
    const std::time_t now = std::time(nullptr);
    std::tm tm = *std::localtime(&now);
    const std::time_t local = timegm(&tm);
    const s64 since_2000 = s64(local) - 946684800;
    return u64(since_2000 + s64(2451544) * 86400 + 43200);
}

} // namespace

s32 prologue_table_place(Database& d, const char* table, u32 table_id, const std::string& label) {
    const u64 code = d.code(table_id, label);
    const db::Table* t = d.db ? d.db->table(table) : nullptr;
    if (code == kNoCode || !t) return -1;
    const std::vector<u32>& ids = t->ids();
    const auto it = std::lower_bound(ids.begin(), ids.end(), u32(code));
    return it != ids.end() && *it == u32(code) ? s32(it - ids.begin()) : -1;
}

std::string prologue_time_text(u32 ms) { return race::format_lap_time(ms == kNoTimeMs ? -1 : s64(ms)); }

std::vector<u8> prologue_license_units(const Object& game) {
    const auto r = record_of(game);
    if (!r) return {};
    std::vector<u8> out(kPrologueLicenseUnits * kPrologueLicenseUnitBytes);
    for (size_t i = 0; i < kPrologueLicenseUnits; ++i) std::memcpy(out.data() + i * kPrologueLicenseUnitBytes, r->unit(i), kPrologueLicenseUnitBytes);
    return out;
}

bool prologue_license_load(Object& game, ByteSpan units) {
    const auto r = record_of(game);
    if (!r || units.size() != kPrologueLicenseUnits * kPrologueLicenseUnitBytes) return false;
    for (size_t i = 0; i < kPrologueLicenseUnits; ++i) std::memcpy(r->unit(i), units.data() + i * kPrologueLicenseUnitBytes, kPrologueLicenseUnitBytes);
    return true;
}

void install_prologue_license(Object& game, const RuntimeConfig* config) {
    if (game.fields.count("license")) return;
    auto lic = std::make_shared<LicenseRecord>();
    LicenseRecord* r = lic.get();
    if (config && (config->program || config->program_image)) {
        try {
            const std::shared_ptr<const gt3r::CoreImage> image = config->image();
            const gt3r::CoreImage& core = *image;
            if (sim::PrologueProgram::locate(core.program, core.load_address).recognized()) {
                r->categories = program_names(core.program, core.load_address, kCategoryNames, kCategoryCount);
                r->medals = program_names(core.program, core.load_address, kMedalNames, kMedalCount);
                r->no_pass_code = program_text(core.program, core.load_address, kNoPassCode);
                r->coder = PassCoder::from_program(core.program, core.load_address);
            }
        } catch (const std::exception&) {
        }
    }
    for (size_t i = 0; i < kPrologueLicenseUnits; ++i) r->clear_unit(i);
    auto units = std::make_shared<std::map<int, Value>>();

    // An entry's place from a call's first argument (0 when none; past the tenth: none).
    auto place = [](Args a) -> int {
        const s32 k = arg_int(a, 0, 0);
        return k >= 0 && size_t(k) < kEntries ? k : -1;
    };
    auto unit = [units, r, place](int i) {
        Value& v = (*units)[i];
        if (v.kind != script::Kind::Nil) return v;
        auto o = std::make_shared<Object>("MLicenseUnit");
        const size_t n = size_t(i);
        // category / detail: the unit's +0 and +4 by the program's names (0x3FE0A0 / 0x3FE030, 0x3FE158 / 0x3FE0E8).
        o->property(
            "category",
            [r, n] {
                const u32 c = LicenseRecord::get32(r->unit(n));
                return Value::string(c < r->categories.size() ? r->categories[c] : std::string());
            },
            [r, n](const Value& v) {
                if (v.kind == script::Kind::String) LicenseRecord::put32(r->unit(n), u32(LicenseRecord::index_of(r->categories, v.as_string())));
            });
        o->property(
            "detail",
            [r, n] {
                const u32 c = LicenseRecord::get32(r->unit(n) + 4);
                return Value::string(c < r->medals.size() ? r->medals[c] : std::string());
            },
            [r, n](const Value& v) {
                if (v.kind == script::Kind::String) LicenseRecord::put32(r->unit(n) + 4, u32(LicenseRecord::index_of(r->medals, v.as_string())));
            });
        // isPassed (0x153640): the medal not 0; isCompletePassed (0x153A40): gold.
        o->method("isPassed", [r, n](Vm&, const Value&, Args) { return Value::integer(LicenseRecord::get32(r->unit(n) + 4) != 0 ? 1 : 0); });
        o->method("isCompletePassed", [r, n](Vm&, const Value&, Args) { return Value::integer(LicenseRecord::get32(r->unit(n) + 4) == 3 ? 1 : 0); });
        o->method("getRankNum", [r, n](Vm&, const Value&, Args) { return Value::integer(r->ranked(n)); });
        // getBestTime([k]) (0x3FE5B8): entry k's time as text (0x409D88).
        o->method("getBestTime", [r, n, place](Vm&, const Value&, Args a) {
            const int k = place(a);
            return Value::string(prologue_time_text(k < 0 ? kNoTimeMs : LicenseRecord::get32(LicenseRecord::entry(r->unit(n), size_t(k)) + 0x10)));
        });
        // getDate([k]) (0x3FE648 with 0x3FDED8): "'YY/MM/DD HH:MM'SS", the dashes for none.
        o->method("getDate", [r, n, place](Vm&, const Value&, Args a) {
            const int k = place(a);
            u64 d = 0;
            if (k >= 0) std::memcpy(&d, LicenseRecord::entry(r->unit(n), size_t(k)) + 0x18, 8);
            if (!d) return Value::string("--/--/-- --:--'--\"");
            const s64 s = s64(d) - (s64(2451544) * 86400 + 43200) + 946684800; // (0x49ACF0: back to the calendar)
            const std::time_t t = std::time_t(s);
            const std::tm tm = *std::gmtime(&t);
            char b[40];
            std::snprintf(b, sizeof b, "'%02d/%02d/%02d %02d:%02d'%02d\"", (tm.tm_year + 1900) % 100, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
            return Value::string(b);
        });
        // getEntryName([k]) (0x3FE5F0), getPoint([k]) (0x3FE688: the entry's), getMaxPoint (0x3FE6B0: the unit's +8).
        o->method("getEntryName", [r, n, place](Vm&, const Value&, Args a) {
            const int k = place(a);
            return Value::string(k < 0 ? std::string() : LicenseRecord::text(LicenseRecord::entry(r->unit(n), size_t(k)), 16));
        });
        o->method("getPoint", [r, n, place](Vm&, const Value&, Args a) {
            const int k = place(a);
            return Value::integer(k < 0 ? 0 : s32(LicenseRecord::get32(LicenseRecord::entry(r->unit(n), size_t(k)) + 0x20)));
        });
        o->method("getMaxPoint", [r, n](Vm&, const Value&, Args) { return Value::integer(s32(LicenseRecord::get32(r->unit(n) + 8))); });
        // getPassCode(k[, part]) (0x1563D8): the entry's code, or its eight characters from part x 8 (0x3FE710).
        o->method("getPassCode", [r, n, place](Vm&, const Value&, Args a) {
            const int k = place(a);
            if (k < 0) return Value::string("");
            const u8* code = LicenseRecord::entry(r->unit(n), size_t(k)) + 0x24;
            if (a.size() > 1) return Value::string(LicenseRecord::text(code + 8 * size_t(std::clamp(arg_int(a, 1), 0, 3)), 8));
            return Value::string(LicenseRecord::text(code, 32));
        });
        // (ours, for the results page) 0x3FE3B0: the place a time would take in the ranking.
        o->method("getRankOf", [r, n](Vm&, const Value&, Args a) {
            if (a.empty() || a[0].kind != script::Kind::Int) return Value::integer(-1);
            return Value::integer(r->rank_of(n, u32(a[0].i)));
        });
        // (ours) entry k's time (ms) and name.
        o->method("getRankTime", [r, n, place](Vm&, const Value&, Args a) {
            const int k = place(a);
            return Value::integer(s32(k < 0 ? kNoTimeMs : LicenseRecord::get32(LicenseRecord::entry(r->unit(n), size_t(k)) + 0x10)));
        });
        o->method("getRankName", [r, n, place](Vm&, const Value&, Args a) {
            const int k = place(a);
            return Value::string(k < 0 ? std::string() : LicenseRecord::text(LicenseRecord::entry(r->unit(n), size_t(k)), 16));
        });
        v = object_value(o);
        return v;
    };
    // A unit by its number or its race code (the scripts' prologue::getLicenseID); -1 for another.
    auto unit_number = [](Vm& vm, const Value& v) -> int {
        if (v.kind == script::Kind::Int) return v.i >= 0 && size_t(v.i) < kPrologueLicenseUnits ? int(v.i) : -1;
        if (v.kind != script::Kind::String || v.as_string().empty()) return -1;
        const Value find = vm.lookup("prologue::getLicenseID");
        if (find.kind != script::Kind::Function) return -1;
        const Value id = vm.call(find, Args(&v, 1));
        return id.kind == script::Kind::Int && id.i >= 0 && size_t(id.i) < kPrologueLicenseUnits ? int(id.i) : -1;
    };
    lic->method("getUnit", [unit, unit_number](Vm& vm, const Value&, Args a) -> Value {
        if (a.empty()) return {};
        const int n = unit_number(vm, a[0]);
        return n >= 0 ? unit(n) : Value{};
    });
    // isPassedCategory / isCompletePassedCategory (0x3FF000 / 0x3FF080): every unit of the category with a medal / gold.
    auto every = [r](const Value& name, u32 least) {
        if (name.kind != script::Kind::String) return 0;
        const u32 c = u32(LicenseRecord::index_of(r->categories, name.as_string()));
        for (size_t i = 0; i < kPrologueLicenseUnits; ++i)
            if (LicenseRecord::get32(r->unit(i)) == c && LicenseRecord::get32(r->unit(i) + 4) < least) return 0;
        return 1;
    };
    lic->method("isPassedCategory", [every](Vm&, const Value&, Args a) { return Value::integer(a.empty() ? 0 : every(a[0], 1)); });
    lic->method("isCompletePassedCategory", [every](Vm&, const Value&, Args a) { return Value::integer(a.empty() ? 0 : every(a[0], 3)); });
    // getDetailCount(name) (0x3FEF40): the units whose medal is that of the name; getTotalPoint (0x3FEFA8): the units' most
    // points summed.
    lic->method("getDetailCount", [r](Vm&, const Value&, Args a) {
        const std::string name = arg_text(a, 0);
        int medal = -1;
        for (size_t k = 0; k < r->medals.size(); ++k)
            if (r->medals[k] == name) medal = int(k);
        int n = 0;
        for (size_t i = 0; i < kPrologueLicenseUnits; ++i) n += s32(LicenseRecord::get32(r->unit(i) + 4)) == medal;
        return Value::integer(n);
    });
    lic->method("getTotalPoint", [r](Vm&, const Value&, Args) {
        s32 n = 0;
        for (size_t i = 0; i < kPrologueLicenseUnits; ++i) n += s32(LicenseRecord::get32(r->unit(i) + 8));
        return Value::integer(n);
    });
    lic->method("forcePassedAll", [](Vm&, const Value&, Args) { return Value{}; }); // (0x3FEF38: nothing)
    // (ours) The name the results page offers (the user profile's +0x1133C: 0x402428 reads it, the page's NameEntry writes
    // it, 0x402438): the last one entered; empty at first (the page then takes one letter at a time).
    lic->method("getDefaultName", [r](Vm&, const Value&, Args) { return Value::string(r->default_name); });
    lic->method("setDefaultName", [r](Vm&, const Value&, Args a) {
        if (!a.empty() && a[0].kind == script::Kind::String) r->default_name = a[0].as_string();
        return Value{};
    });
    // A run's result as the license race's end records it (0x19ED58): (race code, result name, time ms, points[, name[,
    // the car's place, the course's place]]: prologue_table_place). A medal's result keeps the best medal (0x3FE1A0); a run with a time (359,999,999: none) also goes to
    // 0x3FE430: the most points kept, then the entry (the name, the time, the clock's date, the points, the pass code) put at the
    // time's place in the ranking, the slower ones moved down, the tenth dropped (0x3FE250); none past the tenth.
    lic->method("recordResult", [r, unit, unit_number](Vm& vm, const Value&, Args a) -> Value {
        if (a.size() < 4) return {};
        const int n = unit_number(vm, a[0]);
        if (n < 0) return {};
        unit(n);
        u8* u = r->unit(size_t(n));
        const std::string result = a[1].kind == script::Kind::String ? a[1].as_string() : "";
        for (size_t k = 1; k < r->medals.size(); ++k)
            if (result == r->medals[k] && LicenseRecord::get32(u + 4) < u32(k)) LicenseRecord::put32(u + 4, u32(k));
        const u32 t = a[2].kind == script::Kind::Int && a[2].i > 0 ? u32(a[2].i) : kNoTimeMs;
        if (t >= kNoTimeMs) return Value::integer(1);
        const s32 points = arg_int(a, 3, 0);
        if (s32(LicenseRecord::get32(u + 8)) < points) LicenseRecord::put32(u + 8, u32(points));
        const int k = r->rank_of(size_t(n), t);
        if (k < 0) return Value::integer(1);
        for (int j = int(kEntries) - 1; j > k; --j) std::memcpy(LicenseRecord::entry(u, size_t(j)), LicenseRecord::entry(u, size_t(j - 1)), kEntryBytes);
        u8* e = LicenseRecord::entry(u, size_t(k));
        std::memset(e, 0, kEntryBytes);
        const std::string name = arg_text(a, 4);
        std::memcpy(e, name.data(), std::min<size_t>(name.size(), 16));
        LicenseRecord::put32(e + 0x10, t);
        const u64 date = clock_seconds();
        std::memcpy(e + 0x18, &date, 8);
        LicenseRecord::put32(e + 0x20, u32(points));
        // The run's pass code (0x19EC08 over 0x1A31A0): the record's kind 11, the car's and the course's places in their
        // tables (none: the 9 and 8 bits of -1), the test's number (the race code's digits, 0x19F238), the time.
        std::string code = r->no_pass_code;
        if (r->coder && a.size() > 6 && a[0].kind == script::Kind::String && a[0].as_string().size() > 3) {
            PassFields f;
            f.car = u32(arg_int(a, 5, -1)), f.course = u32(arg_int(a, 6, -1));
            f.test = u32(std::atoi(a[0].as_string().c_str() + 3)), f.time = t;
            code = r->coder->encode(f);
        }
        std::memcpy(e + 0x24, code.data(), std::min<size_t>(code.size(), 31));
        return Value::integer(1);
    });
    game.set("license", object_value(lic));
}

s32 prologue_license_points(ByteSpan row, const std::string& result, u32 t) {
    if (row.size() < 0x7C) return 0;
    auto target = [&row](size_t k) {
        const u32 ms = u32(row[0x76 + 2 * k]) * 60000u + u32(row[0x77 + 2 * k]) * 1000u + u32(row[0x1A + 2 * k] | row[0x1B + 2 * k] << 8);
        return ms ? ms : kNoTimeMs;
    };
    const int r = result == "gold" ? 2 : result == "silver" ? 3 : result == "bronze" ? 4 : 0;
    if (r == 0) return 0;
    const u32 g = target(0), sv = target(1), b = target(2);
    // 0x19F410: by the test's kind (Prologue's row +0x22, Gran Turismo 4's columns +0x3E) and its byte +0x5B (+0x73).
    int kind = row[0x3E] >= 4 ? 1 : 0;
    if (row[0x73] > 0 && b == kNoTimeMs) kind = 2;
    if (b == kNoTimeMs && kind == 0) kind = 2;
    auto ratio = [](u32 scale, u32 above, u32 span) { return span ? s32((scale * above) / span) : 0; };
    if (kind == 1) return r == 2 ? 100 : r == 3 ? 50 : 10; // 0x19F620
    if (kind == 0) { // 0x19F508
        if (r == 2) return ratio(100, sv - t, sv - g);
        if (r == 3) return ratio(50, b - t, b - sv);
        return 10;
    }
    if (r == 2) return ratio(100, sv - t, sv - g); // 0x19F670
    return r == 3 ? 50 : 10;
}

} // namespace gt4r::menu
