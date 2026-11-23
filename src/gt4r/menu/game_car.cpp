// MCarGarage: a car with its record (three settings sheets, the parts owned, the condition) as
// the scripts see it. The members are the program's (registered at 0x141B90 and 0x14EF00);
// docs/formats/save.md ("The car record") and docs/formats/menu.md ("A car") say what each does.

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/faces_graph.h"

#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/equipment.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace gt4r::menu {

using script::Args;
using script::Kind;
using script::Value;
using script::Vm;

namespace {

u16 r16(ByteSpan r, size_t o) { return r.size() >= o + 2 ? u16(r[o] | r[o + 1] << 8) : 0; }
u32 r32(ByteSpan r, size_t o) { return r.size() >= o + 4 ? u32(r[o]) | u32(r[o + 1]) << 8 | u32(r[o + 2]) << 16 | u32(r[o + 3]) << 24 : 0; }
u64 r64(const u8* p) {
    u64 v;
    std::memcpy(&v, p, 8);
    return v;
}
void w64(u8* p, u64 v) { std::memcpy(p, &v, 8); }

// The parts types (the database's table ids) as the program's tables have them (0x6A7C28
// categories, 0x6A7C08 the stock part is a row, 0x44A218 reversible, 0x44A250 removable,
// 0x43F270 the first owned bit, the row's category byte, 0x44A2E0 the price column).
struct PartsType {
    int categories;
    bool stock_row, reversible, removable;
    int owned_from;   // -1: none
    int category_at;  // the row's byte; -1: none
    int price_at;     // -1: none
    int price_bytes;
    u32 slot;         // the equipment record's place of the part
};
const PartsType kTypes[32] = {
    {0, false, false, false, -1, -1, -1, 0, 0x00},   // 0 the car
    {2, true, true, false, 0, 0x02, 0x00, 2, 0x20},  // 1 BRAKE
    {2, true, true, false, 2, 0x02, 0x00, 2, 0x28},  // 2 BRAKECONTROLLER
    {5, true, true, false, 4, 0x0E, 0x0C, 2, 0x50},  // 3 SUSPENSION
    {2, true, true, false, 9, 0x02, 0x00, 2, 0xE0},  // 4 ASCC
    {2, true, true, false, 11, 0x02, 0x00, 2, 0xE8}, // 5 TCSC
    {0, true, false, false, -1, -1, -1, 0, 0x30},    // 6 CHASSIS
    {0, true, false, false, -1, 0x0A, -1, 0, 0x80},  // 7 RACINGMODIFY
    {4, false, false, false, 13, 0x04, 0x02, 2, 0x78}, // 8 LIGHTWEIGHT
    {0, true, false, false, -1, -1, -1, 0, 0x70},    // 9 STEER
    {2, true, true, false, 17, 0x0E, 0x0C, 2, 0x40}, // 10 DRIVETRAIN
    {4, true, true, false, 19, 0x22, 0x20, 2, 0x48}, // 11 GEAR
    {0, true, false, false, -1, -1, -1, 0, 0x38},    // 12 ENGINE
    {4, false, true, true, 23, 0x08, 0x00, 4, 0xA8}, // 13 NATUNE
    {6, true, true, false, 27, 0x08, 0x00, 4, 0xB0}, // 14 TURBINEKIT
    {2, false, false, false, 33, 0x06, 0x04, 2, 0x88}, // 15 PORTPOLISH
    {2, false, false, false, 35, 0x06, 0x04, 2, 0x90}, // 16 ENGINEBALANCE
    {2, false, false, false, 37, 0x06, 0x04, 2, 0x98}, // 17 DISPLACEMENT
    {2, false, true, true, 39, 0x06, 0x04, 2, 0xA0},   // 18 COMPUTER
    {3, false, true, true, 41, 0x06, 0x04, 2, 0xD8},   // 19 INTERCOOLER
    {4, false, true, true, 44, 0x06, 0x04, 2, 0xD0},   // 20 MUFFLER
    {4, true, true, false, 48, 0x02, 0x00, 2, 0xC0},   // 21 CLUTCH
    {4, false, true, true, 52, 0x02, 0x00, 2, 0xB8},   // 22 FLYWHEEL
    {2, false, true, true, 56, 0x02, 0x00, 2, 0xC8},   // 23 PROPELLERSHAFT
    {6, true, true, false, 58, 0x02, 0x00, 2, 0x58},   // 24 LSD
    {13, true, true, false, 64, 0x3A, 0x38, 2, 0x60},  // 25 FRONTTIRE
    {13, true, true, false, 77, 0x38, -1, 0, 0x68},    // 26 REARTIRE
    {2, false, true, true, 90, 0x04, 0x02, 2, 0xF8},   // 27 NOS
    {2, false, true, true, 92, 0x06, 0x04, 2, 0x108},  // 28 SUPERCHARGER
    {0, false, false, false, -1, -1, -1, 0, 0xF0},     // 29 WHEEL
    {0, false, false, false, -1, -1, -1, 0, 0x100},    // 30 WING
    {2, false, false, false, 94, -1, -1, 0, 0x168},    // 31 the body's stiffness work
};
constexpr u64 kStiffness = make_code(31, 0); // the stiffness "part"
constexpr u32 kStiffnessPrice = 30000;       // 0x44A458

const PartsType* type_of(int t) { return t >= 1 && t <= 31 ? &kTypes[t] : nullptr; }

u64 sheet_part(const u8* eq, int type) {
    const PartsType* p = type_of(type);
    return p && type != 31 ? r64(eq + p->slot) : kNoCode;
}

// The category of a part: its row's byte (the stiffness work: the sheet's flag).
int part_category(Database& d, u64 code) {
    if (code == kNoCode) return -1;
    const int t = int(code >> 32);
    const PartsType* p = type_of(t);
    if (!p || p->category_at < 0 || !d.db) return -1;
    const ByteSpan row = db::part_row(*d.db, db::PartId(code));
    if (row.size() <= size_t(p->category_at)) return -1;
    return t == 3 ? int(s8(row[size_t(p->category_at)])) : int(row[size_t(p->category_at)]);
}

// The fitted category (0x43F4C8): the part's; nothing fitted: 0 when the stock part is a row,
// else -1.
int equipped(Database& d, const u8* eq, int type) {
    if (type == 31) return eq[0x168] ? 1 : 0;
    const PartsType* p = type_of(type);
    if (!p) return -1;
    const u64 code = sheet_part(eq, type);
    if (code == kNoCode) return p->stock_row ? 0 : -1;
    return part_category(d, code);
}

void mark_stock_owned(Database& d, CarRecord& r) {
    // 0x43FA10: the category each part the car comes with is owned.
    static const int kOrder[] = {1, 2, 11, 3, 25, 26, 10, 24, 4, 5, 8, 15, 16, 17, 18, 13, 14, 22, 21, 23, 19, 20, 27, 28, 31};
    for (int t : kOrder) {
        const PartsType& p = kTypes[t];
        if (t == 31) {
            if (r.sheet(0)[0x168]) r.set_owned(p.owned_from + 1, true);
            continue;
        }
        const u64 code = sheet_part(r.sheet(0), t);
        if (code == kNoCode) continue;
        const int c = part_category(d, code);
        if (c >= 0 && c < p.categories) r.set_owned(p.owned_from + c, true);
    }
}

// The car's settings of a part, to the row's defaults (0x440B50's cases).
s32 clampi(s32 lo, s32 v, s32 hi) { return std::max(lo, std::min(v, hi)); }
void part_settings(Database& d, u8* eq, int type, ByteSpan row) {
    if (row.empty()) return;
    auto b = [&](size_t o) { return o < row.size() ? s32(row[o]) : 0; };
    switch (type) {
    case 2:
        eq[0x12F] = u8(clampi(1, b(0x06), b(0x03))), eq[0x130] = u8(clampi(1, b(0x0A), b(0x07)));
        break;
    case 3: {
        eq[0x140] = u8(b(0x29)), eq[0x141] = u8(b(0x2C));
        eq[0x139] = u8(clampi(b(0x0F), b(0x11), b(0x10))), eq[0x13A] = u8(clampi(b(0x12), b(0x14), b(0x13)));
        const s32 rf = clampi(r16(row, 0x00), r16(row, 0x04), r16(row, 0x02)), rr = clampi(r16(row, 0x06), r16(row, 0x0A), r16(row, 0x08));
        eq[0x13C] = u8(rf), eq[0x13D] = u8(rf >> 8), eq[0x13E] = u8(rr), eq[0x13F] = u8(rr >> 8);
        eq[0x142] = u8(clampi(b(0x34), b(0x36), b(0x35))), eq[0x143] = u8(clampi(b(0x37), b(0x39), b(0x38)));
        eq[0x144] = u8(b(0x3A)), eq[0x145] = u8(b(0x3B));
        const size_t def[8] = {0x4B, 0x4E, 0x52, 0x55, 0x59, 0x5C, 0x60, 0x63}, steps[8] = {0x48, 0x48, 0x4F, 0x4F, 0x56, 0x56, 0x5D, 0x5D};
        for (int i = 0; i < 8; ++i) eq[0x146 + i] = u8(clampi(1, b(def[i]), b(steps[i])));
        eq[0x14E] = u8(clampi(1, b(0x69), b(0x66))), eq[0x14F] = u8(clampi(1, b(0x6D), b(0x6A)));
        break;
    }
    case 4: {
        const size_t def[4] = {0x07, 0x0D, 0x12, 0x18}, steps[4] = {0x04, 0x0A, 0x0F, 0x15};
        for (int i = 0; i < 4; ++i) eq[0x157 + i] = u8(clampi(1, b(def[i]), b(steps[i])));
        break;
    }
    case 5: eq[0x156] = u8(clampi(1, b(0x09), b(0x06))); break;
    case 10: eq[0x12E] = u8(clampi(b(0x12), b(0x14), b(0x13))); break;
    case 11: {
        eq[0x110] = u8(b(0x16)), eq[0x111] = u8(b(0x17));
        for (int i = 0; i < 14; ++i) eq[0x112 + i] = u8(b(size_t(i)));
        const s32 fin = clampi(r16(row, 0x18), r16(row, 0x1C), r16(row, 0x1A));
        eq[0x128] = u8(fin), eq[0x129] = u8(fin >> 8), eq[0x12C] = u8(fin), eq[0x12D] = u8(fin >> 8);
        eq[0x12A] = u8(clampi(b(0x26), b(0x28), b(0x27)));
        sim::Equipment e;
        std::memcpy(e.bytes.data(), eq, sim::Equipment::kSize);
        if (d.db && sim::computed_gears(*d.db, d.program, e)) std::memcpy(eq, e.bytes.data(), sim::Equipment::kSize);
        break;
    }
    case 14:
        for (int i = 0; i < 6; ++i) eq[0x133 + i] = u8(b(0x0A + size_t(i)));
        break;
    case 24: {
        const size_t at[6] = {0x150, 0x151, 0x152, 0x153, 0x154, 0x155}, def[6] = {0x06, 0x12, 0x0A, 0x16, 0x0E, 0x1A}, lo[6] = {0x04, 0x10, 0x08, 0x14, 0x0C, 0x18};
        for (int i = 0; i < 6; ++i) eq[at[i]] = u8(clampi(b(lo[i]), b(def[i]), b(lo[i] + 1)));
        break;
    }
    case 27: eq[0x167] = u8(b(0x05)); break;
    default: break;
    }
}

// The parts that fit a car: its file specdb/<set>/CARS/<the car's code, 16 hex digits>: u64
// count, then {u64 part code, u8 category, 7 bytes} each (0x449E10).
void load_parts(Database& d, CarHandle& c) {
    if (c.parts_for == c.label) return;
    c.parts.clear(), c.parts_for = c.label;
    if (!d.db || c.label.empty()) return;
    const u64 car = d.code(kCarTable, c.label);
    if (car == kNoCode) return;
    char name[40];
    std::snprintf(name, sizeof name, "CARS/%016llX", static_cast<unsigned long long>(car));
    const std::optional<Bytes> file = d.rt->reader()(d.db->folder() + name);
    if (!file || file->size() < 8) return;
    const u64 n = r64(file->data());
    for (u64 i = 0; i < n && 8 + (i + 1) * 16 <= file->size(); ++i) {
        const u8* e = file->data() + 8 + i * 16;
        c.parts.emplace_back(r64(e), int(e[8]));
    }
}

u64 find_part(const CarHandle& c, int type, int category) {
    if (type == 31) return kStiffness;
    for (const auto& [code, cat] : c.parts)
        if (code != kNoCode && int(code >> 32) == type && cat == category) return code;
    return kNoCode;
}

} // namespace

CarRecord& CarHandle::record() {
    if (refers && state && garage_slot >= 0 && size_t(garage_slot) < state->garage.size() && state->garage[size_t(garage_slot)].record)
        return *state->garage[size_t(garage_slot)].record;
    return own;
}

bool set_car(Database& d, CarRecord& r, const std::string& label, int paint) {
    r = CarRecord{};
    if (!d.db) return false;
    sim::Equipment eq;
    if (!sim::stock_car(*d.db, d.program, label, eq)) return false;
    const ByteSpan car = d.car_row(label);
    const bool strange = car.size() > 0x11 && car[0x11] >= 100;
    const int paints = int(d.spec(label).paints.size());
    eq.bytes[0x18] = u8(paint >= 0 && paint < std::max(paints, 1) ? paint : 0);
    eq.bytes[0x166] = strange;
    for (int s = 0; s < 3; ++s) std::memcpy(r.sheet(s), eq.bytes.data(), CarRecord::kSheet);
    for (int c = 0; c < 4; ++c) r.set_wheel(c, -1);
    r.set_active_sheet(0);
    mark_stock_owned(d, r);
    r.set_no_wear(strange);
    if (!strange) r.set_condition(1, -300000.0f); // a new engine
    return true;
}

void make_used_condition(Database&, CarRecord& r, u32 meters) {
    // 0x440F78: the odometer, and the engine's, the oil's and the body's distances from it.
    r.set_odometer(std::min<u32>(meters, 100'000'000));
    if (!r.no_wear()) {
        r.set_condition(1, std::min(float(meters), 11'000'000.0f));
        r.set_condition(2, std::min(float(meters), 6'000'000.0f));
        r.set_condition(4, std::min(float(meters), 3'000'000.0f));
    }
    r.set_condition(3, 0);
}

void car_figures(Database& d, const CarRecord& r, s32& power100, s32& torque100, s32& weight) {
    power100 = torque100 = weight = 0;
    if (!d.db) return;
    sim::Equipment eq;
    std::memcpy(eq.bytes.data(), r.active(), sim::Equipment::kSize);
    sim::Spec spec;
    sim::build_spec(*d.db, d.program, eq, 0, spec);
    const sim::SpecFigures f = sim::spec_figures(spec);
    // The engine's wear and oil (0x350868): a new engine 0.97, run in 1.00, old 0.95; fresh
    // oil 1.05, old 0.95.
    float factor = 1;
    if (!r.no_wear()) {
        const float e = r.condition(1) / 1000.0f, o = r.condition(2) / 1000.0f; // km
        const float ef = e < -300 ? 0.97f : e < 0 ? 0.97f + 0.03f * (e + 300) / 300 : e < 1000 ? 1.0f : e < 11000 ? 1.0f - 0.05f * (e - 1000) / 10000 : 0.95f;
        const float of = o < -100 ? 1.05f : o < 0 ? 1.0f - 0.05f * o / 100 : o < 5000 ? 1.0f : o < 6000 ? 1.0f - 0.05f * (o - 5000) / 1000 : 0.95f;
        factor = ef * of;
    }
    const ByteSpan car = d.db->table("GENERIC_CAR") ? d.db->table("GENERIC_CAR")->row(u32(r64(r.active()))) : ByteSpan{};
    if (car.size() > 0x12 && (car[0x12] & 8)) return; // the specs are hidden
    power100 = s32(f.ps * factor * 100), torque100 = s32(f.kgfm * factor * 100), weight = spec.weight_kg();
}

void entry_from_record(Database& d, GarageEntry& e) {
    if (!e.record) return;
    s32 p, t, w;
    car_figures(d, *e.record, p, t, w);
    e.power = u32(std::max(p, 0)) & 0x3FFFF, e.weight = u32(std::max(w, 0)) & 0x1FFF;
    e.paint = e.record->sheet(0)[0x18];
    e.odometer = e.record->odometer();
}

std::shared_ptr<CarHandle> car_from_entry(Database& d, GameState& s, int slot, bool refer) {
    auto c = std::make_shared<CarHandle>();
    c->state = &s, c->garage_slot = slot;
    if (slot < 0 || size_t(slot) >= s.garage.size() || !s.garage[size_t(slot)].used) return c;
    GarageEntry& e = s.garage[size_t(slot)];
    c->label = e.car, c->day = e.day, c->ride_history = e.ride_rank;
    c->power100 = s32(e.power), c->weight = s32(e.weight);
    if (e.record) {
        if (refer) c->refers = true;
        else c->own = *e.record;
    } else {
        // 0x145B20: a car never driven is the car as sold in its paint, with its odometer.
        set_car(d, c->own, e.car, e.paint);
        make_used_condition(d, c->own, e.odometer);
        if (refer) {
            e.record = c->own;
            c->refers = true;
        }
    }
    return c;
}

namespace {

// The current sheet's row of a part type.
ByteSpan sheet_row(Database& d, const CarRecord& r, int type) {
    const u64 code = sheet_part(r.active(), type);
    return code == kNoCode || !d.db ? ByteSpan{} : db::part_row(*d.db, db::PartId(code));
}

Value int_array(std::initializer_list<s32> v) {
    std::vector<Value> out;
    for (s32 x : v) out.push_back(number(x));
    return Value::array(std::move(out));
}

} // namespace

Value make_car_value(GameNatives& n, std::shared_ptr<CarHandle> c) {
    auto car = std::make_shared<CarObject>();
    car->car = c;
    Database* d = n.database.get();
    GameNatives* np = &n;
    GameState* st = &n.state();
    auto db = [np]() -> Database& { return np->db(); };
    auto rec = [c]() -> CarRecord& { return c->record(); };

    // The car and its paint.
    auto set_label = [c, db](const std::string& label) {
        Database& dd = db();
        c->label = label, c->refers = false;
        return set_car(dd, c->own, label, 0);
    };
    car->property("car_code", [c] { return text(c->label); }, [set_label](const Value& v) { set_label(v.to_string()); });
    car->method("getCarCode", [c](Vm&, const Value&, Args) { return text(c->label); });
    car->method("getCarLabel", [c](Vm&, const Value&, Args) { return text(c->label); });
    car->method("setCarCode", [set_label](Vm&, const Value&, Args a) { return number(set_label(arg_text(a, 0))); });
    car->method("isVacant", [c](Vm&, const Value&, Args) { return number(c->label.empty()); });
    // name, short_name (CAR_NAME_<language> +0x00, +0x08), short_name_no_year (cut at the
    // year's backquote).
    auto name_at = [c, db](size_t at) {
        Database& dd = db();
        const db::Labels* labels = dd.db ? dd.db->labels("GENERIC_CAR") : nullptr;
        const db::Table* names = dd.db ? dd.db->table("CAR_NAME_american") : nullptr;
        const db::Strings* pool = dd.db ? dd.db->strings("american_StrDB") : nullptr;
        const std::optional<u32> id = labels ? labels->id(c->label) : std::nullopt;
        if (!id || !names || !pool) return std::string();
        const ByteSpan row = names->row(*id);
        return row.size() >= at + 4 ? pool->at(r32(row, at)) : std::string();
    };
    car->property("name", [name_at] { return text(name_at(0)); });
    car->property("short_name", [name_at] { return text(name_at(8)); });
    car->property("short_name_no_year", [name_at] {
        std::string s = name_at(8);
        const size_t q = s.rfind('`');
        if (q != std::string::npos && q + 2 < s.size() + 0 && std::isdigit(u8(s[q + 1])) && std::isdigit(u8(s[q + 2]))) {
            s.erase(q);
            while (!s.empty() && s.back() == ' ') s.pop_back();
        }
        return text(s);
    });
    auto spec = [c, db]() -> const Database::Spec& { return db().spec(c->label); };
    // getColorNum (0x445828): the paints, less the last one when the car table hides it.
    car->method("getColorNum", [c, spec, db](Vm&, const Value&, Args) {
        const ByteSpan row = db().car_row(c->label);
        const int n = int(spec().paints.size()) - (row.size() > 0x12 && (row[0x12] & 2) ? 1 : 0);
        return number(std::max(n, 1));
    });
    auto colors = [c, spec, db](bool all) {
        return [c, spec, db, all](Vm&, const Value&, Args) {
            const ByteSpan row = db().car_row(c->label);
            size_t n = spec().paints.size();
            if (!all && n > 1 && row.size() > 0x12 && (row[0x12] & 2)) --n;
            std::vector<Value> out;
            for (size_t i = 0; i < n; ++i) {
                const Database::Paint& p = spec().paints[i];
                auto col = std::make_shared<Object>("MColorObject");
                // The row's colour through the color constructor, each part times 1.35 (0x145C30).
                col->set("r", Value::number(std::min(1.0f, float(p.rgb[0]) / 255.0f * 1.35f)));
                col->set("g", Value::number(std::min(1.0f, float(p.rgb[1]) / 255.0f * 1.35f)));
                col->set("b", Value::number(std::min(1.0f, float(p.rgb[2]) / 255.0f * 1.35f)));
                col->set("a", Value::number(1));
                out.push_back(object_value(col));
            }
            return Value::array(std::move(out));
        };
    };
    car->method("getColors", colors(false));
    car->method("getAllColors", colors(true));
    car->method("getColorName", [spec, rec](Vm&, const Value&, Args a) {
        const std::vector<Database::Paint>& paints = spec().paints;
        const s32 i = a.empty() ? s32(s8(rec().active()[0x18])) : arg_int(a, 0);
        return text(i >= 0 && size_t(i) < paints.size() ? paints[size_t(i)].name : std::string());
    });
    car->method("getColorIndex", [rec](Vm&, const Value&, Args) { return number(s8(rec().active()[0x18])); });

    // The catalog's figures (the car as sold).
    auto train_name = [c, db]() -> std::string {
        static const char* const kinds[] = {"FR", "FF", "4WD", "MR", "RR"};
        const int t = db().spec(c->label).drivetrain;
        return t >= 0 && t < 5 ? kinds[t] : "--";
    };
    car->method("getDTrainType", [train_name](Vm&, const Value&, Args) { return text(train_name()); });
    car->method("getDTrain", [train_name](Vm& vm, const Value&, Args a) {
        return a.empty() ? text(train_name()) : format_with(vm, a[0], {text(train_name())});
    });
    car->method("getCatPs", [spec, st](Vm& vm, const Value&, Args a) {
        const Database::Spec& sp = spec();
        if (!sp.engine || sp.power_rpm.empty() || sp.power_rpm[0] == '-') return text("----");
        const GameState::Unit unit = st->power_unit();
        const Value v = number(s32(float(sp.ps) * unit.factor));
        return a.empty() ? v : format_with(vm, a[0], {v, text(unit.name), text(sp.power_rpm), text("rpm")});
    });
    car->method("getCatTq", [spec, st](Vm& vm, const Value&, Args a) {
        const Database::Spec& sp = spec();
        if (!sp.engine) return text("----");
        const GameState::Unit unit = st->torque_unit();
        const float v = sp.torque * unit.factor;
        if (a.empty()) return Value::number(v);
        return format_with(vm, a[0], {fixed_for(a[0].to_string(), v), text(unit.name), text(sp.torque_rpm), text("rpm")});
    });
    car->method("getCatMs", [spec](Vm& vm, const Value&, Args a) {
        if (!spec().weight) return text("----");
        return a.empty() ? number(spec().weight) : format_with(vm, a[0], {number(spec().weight), text("kg")});
    });
    car->method("getCatDisplacement", [spec](Vm&, const Value&, Args) { return text(spec().displacement); });
    car->method("getCatEngineType", [spec](Vm&, const Value&, Args) { return text(spec().layout); });
    car->method("getCatEngineCam", [spec](Vm&, const Value&, Args) { return text(spec().valves); });
    // The sizes: CHASSIS +0x00 .. +0x0A and the body's (RACINGMODIFY +0x06, +0x08) width (mm).
    auto chassis = [rec, db](size_t at, int type) -> int {
        const ByteSpan row = sheet_row(db(), rec(), type);
        return r16(row, at);
    };
    auto size_text = [](int v, bool dash) { return text(dash && !v ? std::string("-") : std::to_string(v)); };
    car->method("getLength", [chassis, size_text](Vm&, const Value&, Args) { return size_text(chassis(0x00, 6), false); });
    car->method("getHeight", [chassis, size_text](Vm&, const Value&, Args) { return size_text(chassis(0x02, 6), false); });
    car->method("getWheelBase", [chassis, size_text](Vm&, const Value&, Args) { return size_text(chassis(0x04, 6), false); });
    car->method("getWidth", [chassis, size_text](Vm&, const Value&, Args) { return size_text(chassis(0x06, 7), false); });
    car->method("getCatLength", [chassis, size_text](Vm&, const Value&, Args) { return size_text(chassis(0x08, 6), true); });
    car->method("getCatHeight", [chassis, size_text](Vm&, const Value&, Args) { return size_text(chassis(0x0A, 6), true); });
    car->method("getCatWidth", [chassis, size_text](Vm&, const Value&, Args) { return size_text(chassis(0x08, 7), true); });

    // The figures now (updateCurrentStatus: the spec of the current sheet with the engine's wear).
    auto update = [c, rec, db] {
        car_figures(db(), rec(), c->power100, c->torque100, c->weight);
    };
    car->method("updateCurrentStatus", [update](Vm&, const Value&, Args) {
        update();
        return Value{};
    });
    car->method("getCurrentPower", [c](Vm&, const Value&, Args) { return number(s32(std::lround(c->power100 / 100.0))); });
    car->method("getCurrentWeight", [c](Vm&, const Value&, Args) { return number(c->weight); });
    car->method("getCurrentPs", [c, spec, st](Vm& vm, const Value&, Args a) {
        const GameState::Unit unit = st->power_unit();
        const Value v = number(s32(float(c->power100) / 100.0f * unit.factor + 0.5f));
        if (a.empty()) return v;
        if (spec().power_rpm.empty() || spec().power_rpm[0] == '-') return text("----");
        return format_with(vm, a[0], {v, text(unit.name), text(spec().power_rpm), text("rpm")});
    });
    car->method("getCurrentTq", [c, spec, st](Vm& vm, const Value&, Args a) {
        const GameState::Unit unit = st->torque_unit();
        const float v = float(c->torque100) / 100.0f * unit.factor;
        if (a.empty()) return Value::number(v);
        return format_with(vm, a[0], {fixed_for(a[0].to_string(), v), text(unit.name), text(spec().torque_rpm), text("rpm")});
    });
    car->method("getCurrentMs", [c](Vm& vm, const Value&, Args a) {
        if (c->weight <= 0) return text("----");
        return a.empty() ? number(c->weight) : format_with(vm, a[0], {number(c->weight), text("kg")});
    });
    // kg a PS, three decimals ("%.3t").
    car->method("getCurrentPWR", [c](Vm& vm, const Value&, Args a) {
        if (c->weight <= 0 || c->power100 <= 0) return text("----");
        const s32 v = s32(float(c->weight) / (float(c->power100) / 100.0f) * 1000.0f + 0.5f);
        return a.empty() ? number(v) : format_with(vm, a[0], {number(v)});
    });
    car->method("getOdometer", [rec, st](Vm& vm, const Value&, Args a) {
        const bool miles = st->option_text("unit_velocity") == "MPH";
        const s32 v = s32(std::min<u32>(rec().odometer() / (miles ? 160 : 100), 999'999'999));
        return a.empty() ? number(v) : format_with(vm, a[0], {number(v), text(miles ? "mile" : "km")});
    });
    car->method("getDate", [c](Vm&, const Value&, Args) { return number(s32(c->day)); });
    car->method("getGarageIndex", [c](Vm&, const Value&, Args) { return number(c->garage_slot); });
    car->property("rideHistory", [c] { return number(c->ride_history); });
    car->method("dump", [](Vm&, const Value&, Args) { return Value{}; });
    car->method("copyFrom", [c, np](Vm& vm, const Value&, Args a) {
        if (a.empty()) return Value{};
        if (const std::shared_ptr<CarObject> o = std::dynamic_pointer_cast<CarObject>(a[0].p)) {
            const CarHandle& from = *o->car;
            const CarRecord copy = from.record();
            c->own = copy, c->refers = false, c->label = from.label, c->parts = from.parts, c->parts_for = from.parts_for;
            c->weight = from.weight, c->power100 = from.power100, c->torque100 = from.torque100, c->day = from.day;
            c->ride_history = from.ride_history, c->garage_slot = from.garage_slot;
        }
        (void)vm, (void)np;
        return Value{};
    });

    // The car table's flags.
    auto flag = [c, db](size_t at, int mask) {
        const ByteSpan row = db().car_row(c->label);
        return row.size() > at && (row[at] & mask) != 0;
    };
    car->method("isStrange", [c, db](Vm&, const Value&, Args) {
        const ByteSpan row = db().car_row(c->label);
        return number(row.size() > 0x11 && row[0x11] >= 100);
    });
    car->method("isOpenCar", [flag](Vm&, const Value&, Args) { return number(flag(0x12, 0x10)); });
    car->method("isChangeWheelDisabled", [flag](Vm&, const Value&, Args) { return number(flag(0x15, 0xFF)); });
    car->method("isChangeWingDisabled", [flag](Vm&, const Value&, Args) { return number(flag(0x16, 0xFF)); });
    car->method("isAutomaticOnly", [rec, db](Vm&, const Value&, Args) {
        const ByteSpan row = sheet_row(db(), rec(), 11);
        return number(row.size() > 0x23 && (row[0x23] == 1 || row[0x23] == 3));
    });
    car->method("isTireSelectable", [c, db](Vm&, const Value&, Args) {
        const ByteSpan row = db().row("ARCADEINFO_NORMAL", c->label);
        return number(row.size() > 0x40 && row[0x40] != 0);
    });
    auto turbo = [rec, db] {
        const ByteSpan row = sheet_row(db(), rec(), 14);
        return row.size() > 0x09 && row[0x09] != 0;
    };
    auto charger = [rec, db, flag] { return flag(0x17, 0xFF) || equipped(db(), rec().active(), 28) == 1; };
    car->method("isTurbo", [turbo](Vm&, const Value&, Args) { return number(turbo()); });
    car->method("isSuperCharger", [charger](Vm&, const Value&, Args) { return number(charger()); });
    car->method("isNAtune", [turbo, charger](Vm&, const Value&, Args) { return number(!turbo() && !charger()); });
    car->method("getAspiration", [turbo, charger](Vm&, const Value&, Args) { return number(turbo() ? 1 : charger() ? 2 : 0); });

    // ---- Parts
    auto parts = [c, db]() -> CarHandle& {
        load_parts(db(), *c);
        return *c;
    };
    car->method("loadPartsInfo", [parts](Vm&, const Value&, Args) {
        parts();
        return Value{};
    });
    car->method("setupPartsBuffer", [](Vm&, const Value&, Args) { return Value{}; });
    car->method("beginSetting", [parts](Vm&, const Value&, Args) {
        parts();
        return Value{};
    });
    // endSetting = syncWheel (0x441350): each sheet's wheel is the one chosen for its front
    // tires' category.
    auto sync = [rec, db](Vm&, const Value&, Args) {
        CarRecord& r = rec();
        for (int s = 0; s < 3; ++s) {
            const int kind = equipped(db(), r.sheet(s), 25);
            const int cat = kind <= 5 ? 0 : kind <= 10 ? 1 : kind == 11 ? 2 : 3;
            w64(r.sheet(s) + 0xF0, u64(r.wheel(cat)));
        }
        return Value{};
    };
    car->method("endSetting", sync);
    car->method("syncWheel", sync);
    car->method("getPartsTypeCount", [parts](Vm&, const Value&, Args a) {
        const int t = arg_int(a, 0);
        int n = 0;
        for (const auto& [code, cat] : parts().parts) n += code != kNoCode && int(code >> 32) == t;
        return number(n);
    });
    car->method("getPartsTypeMax", [](Vm&, const Value&, Args a) {
        const PartsType* p = type_of(arg_int(a, 0));
        return number(p ? p->categories : 0);
    });
    car->method("isExistPartsType", [parts](Vm&, const Value&, Args a) {
        const int t = arg_int(a, 0);
        int n = 0, last = -1;
        for (const auto& [code, cat] : parts().parts)
            if (code != kNoCode && int(code >> 32) == t) ++n, last = cat;
        return number(n >= 2 || (n == 1 && last != 0));
    });
    car->method("isExistParts", [parts](Vm&, const Value&, Args a) { return number(find_part(parts(), arg_int(a, 0), arg_int(a, 1)) != kNoCode); });
    car->method("getPartsPrice", [parts, db](Vm&, const Value&, Args a) {
        const int t = arg_int(a, 0);
        const u64 code = find_part(parts(), t, arg_int(a, 1));
        const PartsType* p = type_of(t);
        if (code == kNoCode || !p) return number(-1);
        if (t == 31) return number(s32(kStiffnessPrice));
        if (p->price_at < 0 || !db().db) return number(-1);
        const ByteSpan row = db::part_row(*db().db, db::PartId(code));
        return number(s32(p->price_bytes == 4 ? r32(row, size_t(p->price_at)) : r16(row, size_t(p->price_at))));
    });
    car->method("getEquipedParts", [rec, db](Vm&, const Value&, Args a) { return number(equipped(db(), rec().active(), arg_int(a, 0))); });
    // getParts(type, category): owned (the script pays and fits it itself).
    car->method("getParts", [rec](Vm&, const Value&, Args a) {
        const PartsType* p = type_of(arg_int(a, 0));
        if (p && p->owned_from >= 0) rec().set_owned(p->owned_from + arg_int(a, 1), true);
        return Value{};
    });
    car->method("isHavingParts", [rec](Vm&, const Value&, Args a) {
        const int t = arg_int(a, 0), cat = arg_int(a, 1);
        const PartsType* p = type_of(t);
        if (!p || p->owned_from < 0) return number(0);
        if (p->removable && !p->stock_row && cat == 0) return number(1);
        return number(rec().owned(p->owned_from + cat));
    });
    car->method("isHavingStiffnessParts", [](Vm&, const Value&, Args) { return number(0); });
    car->method("isAllowedParts", [rec, parts](Vm&, const Value&, Args a) {
        const int t = arg_int(a, 0), cat = arg_int(a, 1);
        if (t != 8 || cat <= 1) return number(1);
        if (rec().owned(kTypes[8].owned_from + cat - 1)) return number(1);
        return number(find_part(parts(), 8, cat - 1) == kNoCode);
    });
    car->method("isReversibilityType", [](Vm&, const Value&, Args a) {
        const PartsType* p = type_of(arg_int(a, 0));
        return number(p && p->reversible);
    });
    car->method("isRemovableType", [](Vm&, const Value&, Args a) {
        const PartsType* p = type_of(arg_int(a, 0));
        return number(p && p->removable);
    });
    car->method("isRemovedNormalType", [](Vm&, const Value&, Args a) { return number(arg_int(a, 0) == 20 || arg_int(a, 0) == 22); });
    // isDefaultNoParts(type): a muffler that the car does not come with (0x4455B0).
    car->method("isDefaultNoParts", [c, db](Vm&, const Value&, Args a) {
        if (arg_int(a, 0) != 20 || !db().db) return number(0);
        return number(!db().db->default_part(c->label, 20).has_value());
    });
    // setParts = changeParts (0x1403A0): the part of the car's list; category 0 of a removable
    // type that has none takes it off. Parts that cannot be undone go on all three sheets.
    auto set_parts = [rec, parts, db, update](Vm&, const Value&, Args a) {
        const int t = arg_int(a, 0), cat = arg_int(a, 1);
        const PartsType* p = type_of(t);
        if (!p) return Value{};
        CarRecord& r = rec();
        const u64 code = find_part(parts(), t, cat);
        if (code == kNoCode) {
            if (cat == 0 && p->removable) w64(r.active() + p->slot, kNoCode);
            return Value{};
        }
        if (t == 31) {
            for (int s = 0; s < 3; ++s) r.sheet(s)[0x168] = 1;
            return Value{};
        }
        const bool every_sheet = !p->reversible || t == 29 || t == 30;
        for (int s = 0; s < 3; ++s) {
            u8* eq = every_sheet ? r.sheet(s) : r.active();
            w64(eq + p->slot, code);
            if (db().db) part_settings(db(), eq, t, db::part_row(*db().db, db::PartId(code)));
            if (!every_sheet) break;
        }
        return Value{};
    };
    car->method("setParts", set_parts);
    car->method("changeParts", set_parts);
    car->method("removeParts", [rec](Vm&, const Value&, Args a) {
        const PartsType* p = type_of(arg_int(a, 0));
        if (p && arg_int(a, 0) <= 30) w64(rec().active() + p->slot, kNoCode);
        return Value{};
    });
    car->property("slot", [rec] { return number(rec().active_sheet()); }, [rec](const Value& v) { rec().set_active_sheet(v.kind == Kind::Int ? v.i : s32(v.as_float())); });
    // canTune(dealer): the dealer is the car's tuner or one of its TUNER_LIST row's (0x140D68).
    auto tuners = [c, db]() {
        std::vector<int> out;
        const ByteSpan row = db().car_row(c->label);
        if (row.size() > 0x10) out.push_back(row[0x10]);
        const ByteSpan list = db().db && db().db->table("TUNER_LIST") ? db().db->table("TUNER_LIST")->row(db().code(kCarTable, c->label) == kNoCode ? ~0u : u32(db().code(kCarTable, c->label))) : ByteSpan{};
        for (u8 t : list) {
            if (!t) break;
            out.push_back(t);
        }
        return out;
    };
    car->method("canTune", [tuners, db](Vm&, const Value&, Args a) {
        const std::string dealer = arg_text(a, 0);
        for (int t : tuners()) {
            const auto it = db().names.tuners.find(t);
            if (it != db().names.tuners.end() && it->second == dealer) return number(1);
        }
        return number(0);
    });
    car->method("getTunerList", [tuners, db](Vm&, const Value&, Args) {
        std::vector<Value> out;
        for (int t : tuners()) {
            const auto it = db().names.tuners.find(t);
            if (it != db().names.tuners.end()) out.push_back(text(it->second));
        }
        return Value::array(std::move(out));
    });

    // ---- Wheels and wings by their labels; a tire category by its name.
    auto category = [](const Value& v) {
        if (v.kind != Kind::String) return std::clamp(int(v.kind == Kind::Int ? v.i : 0), 0, 3);
        const std::string s = v.as_string();
        return s == "RACING" ? 1 : s == "DIRT" ? 2 : s == "SNOW" ? 3 : 0;
    };
    car->method("haveTireCategoryWheel", [rec, db, category](Vm&, const Value&, Args a) {
        if (a.size() < 2) return number(0);
        return number(rec().wheel(category(a[0])) == s64(db().code(kWheelTable, arg_text(a, 1))));
    });
    car->method("setTireCategoryWheel", [rec, db, category](Vm&, const Value&, Args a) {
        if (a.size() < 2) return Value{};
        rec().set_wheel(category(a[0]), s64(db().code(kWheelTable, arg_text(a, 1))));
        return Value{};
    });
    car->method("getCurrentWheel", [rec, db, category](Vm&, const Value&, Args a) {
        const s64 id = rec().wheel(a.empty() ? 0 : category(a[0]));
        return text(id == -1 ? std::string() : db().label(u64(id)));
    });
    car->method("haveWing", [rec, db](Vm&, const Value&, Args a) {
        const u64 want = a.empty() || a[0].is_nil() ? kNoCode : db().code(kWingTable, arg_text(a, 0));
        return number(r64(rec().active() + 0x100) == want);
    });
    car->method("setWing", [rec, db](Vm&, const Value&, Args a) {
        const u64 id = a.empty() || a[0].is_nil() ? kNoCode : db().code(kWingTable, arg_text(a, 0));
        for (int s = 0; s < 3; ++s) w64(rec().sheet(s) + 0x100, id);
        return Value{};
    });
    car->method("getCurrentWing", [rec, db](Vm&, const Value&, Args) {
        const u64 id = r64(rec().active() + 0x100);
        return text(id == kNoCode ? std::string() : db().label(id));
    });

    // ---- The car's condition.
    car->method("getOilLifeRate", [rec](Vm&, const Value&, Args) {
        const CarRecord& r = rec();
        if (r.no_wear()) return Value::number(0);
        const float o = r.condition(2), dd = o >= 0 ? o : o + 300000.0f;
        return Value::number(std::min(1.0f, dd / 6'000'000.0f));
    });
    car->property("oil", [rec] { return Value::number(rec().condition(2)); }, [rec](const Value& v) {
        if (!rec().no_wear()) rec().set_condition(2, std::min(v.as_float(), 6'000'000.0f));
    });
    car->method("replaceOil", [rec](Vm&, const Value&, Args) {
        if (!rec().no_wear()) rec().set_condition(2, -300000.0f);
        return Value{};
    });
    car->property("dirtiness", [rec] { return Value::number(rec().condition(3)); }, [rec](const Value& v) { rec().set_condition(3, std::clamp(v.as_float(), 0.0f, 0.8f)); });
    car->method("getBodyLifeRate", [rec](Vm&, const Value&, Args) {
        const CarRecord& r = rec();
        if (r.no_wear()) return Value::number(0);
        return Value::number(std::clamp((r.condition(4) - 500'000.0f) / 2'500'000.0f, 0.0f, 1.0f));
    });
    car->property("bodyLife", [rec] { return Value::number(rec().condition(4)); }, [rec](const Value& v) {
        if (!rec().no_wear()) rec().set_condition(4, std::clamp(v.as_float(), 0.0f, 3'000'000.0f));
    });
    car->method("refreshBody", [rec](Vm&, const Value&, Args) {
        if (!rec().no_wear()) rec().set_condition(4, 0);
        return Value{};
    });
    car->property("odometer", [rec] { return number(s32(rec().odometer())); }, [rec](const Value& v) { rec().set_odometer(std::min<u32>(u32(std::max<s32>(arg_int(Args(&v, 1), 0), 0)), 100'000'000)); });

    // ---- Used cars: makeUsed(seed, [paint = -1], [meters = -1]) (0x440FB8).
    car->method("makeUsed", [c, rec, db, spec](Vm&, const Value&, Args a) {
        ProgramRandom random{u32(arg_int(a, 0))};
        s32 paint = arg_int(a, 1, -1), meters = arg_int(a, 2, -1);
        CarRecord& r = rec();
        if (paint < 0) paint = s32(random.next01() * float(spec().paints.size()));
        if (paint >= 0 && size_t(paint) < spec().paints.size())
            for (int s = 0; s < 3; ++s) r.sheet(s)[0x18] = u8(paint);
        if (meters < 0) {
            // Thousands of km driven by the model year (0x6A6910).
            static const int lo[15] = {60, 55, 50, 50, 45, 45, 40, 35, 30, 25, 20, 15, 15, 10, 3};
            static const int hi[15] = {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 40, 30, 20, 10};
            const ByteSpan row = db().car_row(c->label);
            const int year = std::clamp(int(s16(r16(row, 0x0C))) - 1990, 0, 14);
            meters = s32(float(lo[year]) * 1'000'000.0f + float(hi[year] - lo[year]) * 1'000'000.0f * random.next01());
        }
        make_used_condition(db(), r, u32(meters));
        return Value{};
    });
    // getUsedPrice (0x441128): the new price times a factor by the thousands of km driven.
    auto used_price = [c, rec, db]() -> s32 {
        static const u32 below[9] = {1, 3, 5, 10, 15, 25, 40, 60, 100};
        static const float factor[10] = {0.85f, 0.80f, 0.78f, 0.77f, 0.76f, 0.75f, 0.65f, 0.50f, 0.35f, 0.10f};
        const u32 k = rec().odometer() / 1'000'000;
        size_t i = 0;
        while (i < 9 && k >= below[i]) ++i;
        const u32 price = r32(db().car_row(c->label), 0x08);
        return s32(float(price) * factor[i]);
    };
    car->method("getUsedPrice", [used_price](Vm&, const Value&, Args) { return number(used_price()); });
    car->method("getUsedSellPrice", [used_price](Vm&, const Value&, Args) { return number(s32(float(used_price()) * 0.25f)); });

    // ---- The settings pages (0x1490B0 on): each a range of the fitted part's row and the
    // sheet's values. Levels are stored as they are shown (1 .. steps).
    auto row_of = [rec, db](int type) { return sheet_row(db(), rec(), type); };
    auto get_byte = [rec](u32 at) { return number(rec().active()[at]); };
    auto put_byte = [rec](u32 at, s32 v) { rec().active()[at] = u8(v); };
    car->method("getBrakeControllerRange", [row_of](Vm&, const Value&, Args) {
        const ByteSpan r = row_of(2);
        auto b = [&](size_t o) { return s32(o < r.size() ? r[o] : 0); };
        return int_array({b(3), b(4), b(5), b(7), b(8), b(9)});
    });
    car->method("getBrakeControllerSetting", [get_byte](Vm&, const Value&, Args a) { return arg_int(a, 0) == 1 ? get_byte(0x130) : arg_int(a, 0) == 0 ? get_byte(0x12F) : number(-1); });
    car->method("setBrakeControllerSetting", [put_byte](Vm&, const Value&, Args a) {
        if (arg_int(a, 0) == 0 || arg_int(a, 0) == 1) put_byte(arg_int(a, 0) ? 0x130 : 0x12F, arg_int(a, 1));
        return Value{};
    });
    car->method("getDriveTrainRange", [row_of](Vm&, const Value&, Args) {
        const ByteSpan r = row_of(10);
        return int_array({s32(r.size() > 0x12 ? r[0x12] : 0), s32(r.size() > 0x13 ? r[0x13] : 0)});
    });
    car->method("getDriveTrainSetting", [get_byte](Vm&, const Value&, Args a) { return arg_int(a, 0) == 0 ? get_byte(0x12E) : number(-1); });
    car->method("setDriveTrainSetting", [put_byte](Vm&, const Value&, Args a) {
        if (arg_int(a, 0) == 0) put_byte(0x12E, arg_int(a, 1));
        return Value{};
    });
    car->method("getRacingModifyRange", [row_of, rec, db](Vm&, const Value&, Args) {
        const ByteSpan r = row_of(7);
        auto b = [&](size_t o) { return s32(o < r.size() ? r[o] : 0); };
        s32 v[4] = {b(0x0D), b(0x0E), b(0x10), b(0x11)};
        const u64 wing = r64(rec().active() + 0x100);
        if (wing != kNoCode && db().db) {
            const ByteSpan w = db::part_row(*db().db, db::PartId(wing));
            if (w.size() > 4) v[0] += w[3], v[2] += w[3], v[1] += w[4], v[3] += w[4];
        }
        return int_array({v[0], v[1], v[2], v[3]});
    });
    car->method("getRacingModifySetting", [get_byte](Vm&, const Value&, Args a) { return arg_int(a, 0) == 1 ? get_byte(0x132) : arg_int(a, 0) == 0 ? get_byte(0x131) : number(-1); });
    car->method("setRacingModifySetting", [put_byte](Vm&, const Value&, Args a) {
        if (arg_int(a, 0) == 0 || arg_int(a, 0) == 1) put_byte(arg_int(a, 0) ? 0x132 : 0x131, arg_int(a, 1));
        return Value{};
    });
    car->method("getLSDRange", [row_of](Vm&, const Value&, Args) {
        const ByteSpan r = row_of(24);
        std::vector<Value> out;
        for (size_t o : {0x04, 0x05, 0x08, 0x09, 0x0C, 0x0D, 0x10, 0x11, 0x14, 0x15, 0x18, 0x19, 0x06, 0x0A, 0x0E, 0x12, 0x16, 0x1A}) out.push_back(number(o < r.size() ? r[o] : 0));
        return Value::array(std::move(out));
    });
    static const u32 kLsd[6] = {0x150, 0x152, 0x154, 0x151, 0x153, 0x155};
    car->method("getLSDSetting", [get_byte](Vm&, const Value&, Args a) {
        const s32 i = arg_int(a, 0);
        return i >= 0 && i < 6 ? get_byte(kLsd[i]) : number(-1);
    });
    car->method("setLSDSetting", [put_byte](Vm&, const Value&, Args a) {
        const s32 i = arg_int(a, 0);
        if (i >= 0 && i < 6) put_byte(kLsd[i], arg_int(a, 1));
        return Value{};
    });
    car->method("getSuspensionRange", [row_of](Vm&, const Value&, Args) {
        const ByteSpan r = row_of(3);
        std::vector<Value> out;
        for (size_t o : {0x00, 0x02, 0x06, 0x08}) out.push_back(number(r16(r, o)));
        for (size_t o : {0x0F, 0x10, 0x12, 0x13, 0x27, 0x28, 0x2A, 0x2B, 0x34, 0x35, 0x37, 0x38, 0x48, 0x49, 0x4A, 0x4C, 0x4D, 0x4F, 0x50, 0x51, 0x53, 0x54, 0x56,
                         0x57, 0x58, 0x5A, 0x5B, 0x5D, 0x5E, 0x5F, 0x61, 0x62, 0x66, 0x67, 0x68, 0x6A, 0x6B, 0x6C})
            out.push_back(number(o < r.size() ? r[o] : 0));
        return Value::array(std::move(out));
    });
    // Setting index: 0 .. 7 the dampers, 8 / 9 camber, 10 / 11 toe, 12 / 13 ride height (u16),
    // 14 / 15 springs, 16 / 17 anti-roll bars.
    auto suspension_at = [](s32 i) -> u32 {
        if (i >= 0 && i < 8) return 0x146 + u32(i);
        static const u32 at[10] = {0x139, 0x13A, 0x140, 0x141, 0x13C, 0x13E, 0x142, 0x143, 0x14E, 0x14F};
        return i >= 8 && i < 18 ? at[i - 8] : 0;
    };
    car->method("getSuspensionSetting", [rec, suspension_at](Vm&, const Value&, Args a) {
        const s32 i = arg_int(a, 0);
        const u32 at = suspension_at(i);
        if (!at) return number(-1);
        const u8* eq = rec().active();
        return number(i == 12 || i == 13 ? s32(eq[at] | eq[at + 1] << 8) : s32(eq[at]));
    });
    car->method("setSuspensionSetting", [rec, suspension_at](Vm&, const Value&, Args a) {
        const s32 i = arg_int(a, 0), v = arg_int(a, 1);
        const u32 at = suspension_at(i);
        if (!at) return Value{};
        u8* eq = rec().active();
        eq[at] = u8(v);
        if (i == 12 || i == 13) eq[at + 1] = u8(v >> 8);
        return Value{};
    });
    car->method("getTCSCRange", [row_of](Vm&, const Value&, Args) {
        const ByteSpan r = row_of(5);
        auto b = [&](size_t o) { return s32(o < r.size() ? r[o] : 0); };
        return int_array({b(6), b(7), b(8)});
    });
    car->method("getTCSCSetting", [get_byte](Vm&, const Value&, Args a) { return arg_int(a, 0) == 0 ? get_byte(0x156) : number(-1); });
    car->method("setTCSCSetting", [put_byte](Vm&, const Value&, Args a) {
        if (arg_int(a, 0) == 0) put_byte(0x156, arg_int(a, 1));
        return Value{};
    });
    car->method("getASCCRange", [row_of](Vm&, const Value&, Args) {
        const ByteSpan r = row_of(4);
        std::vector<Value> out;
        for (size_t o : {0x04, 0x05, 0x06, 0x0A, 0x0B, 0x0C, 0x0F, 0x10, 0x11, 0x15, 0x16, 0x17}) out.push_back(number(o < r.size() ? r[o] : 0));
        return Value::array(std::move(out));
    });
    car->method("getASCCSetting", [get_byte](Vm&, const Value&, Args a) { return arg_int(a, 0) == 0 ? get_byte(0x157) : arg_int(a, 0) == 1 ? get_byte(0x159) : number(-1); });
    car->method("setASCCSetting", [put_byte](Vm&, const Value&, Args a) {
        // Each setting drives a pair of the four channels (0x43EA58, 0x43EAA0).
        if (arg_int(a, 0) == 0) put_byte(0x157, arg_int(a, 1)), put_byte(0x158, arg_int(a, 1));
        else if (arg_int(a, 0) == 1) put_byte(0x159, arg_int(a, 1)), put_byte(0x15A, arg_int(a, 1));
        return Value{};
    });
    car->method("getBallastRange", [](Vm&, const Value&, Args) { return int_array({0, 200, -50, 50}); });
    car->method("getBallastSetting", [rec](Vm&, const Value&, Args a) {
        return arg_int(a, 0) == 0 ? number(rec().active()[0x15B]) : arg_int(a, 0) == 1 ? number(s8(rec().active()[0x15C])) : number(-1);
    });
    car->method("setBallastSetting", [put_byte](Vm&, const Value&, Args a) {
        if (arg_int(a, 0) == 0 || arg_int(a, 0) == 1) put_byte(arg_int(a, 0) ? 0x15C : 0x15B, arg_int(a, 1));
        return Value{};
    });
    car->method("getNOSRange", [row_of](Vm&, const Value&, Args) {
        const ByteSpan r = row_of(27);
        auto b = [&](size_t o) { return s32(o < r.size() ? r[o] : 0); };
        return int_array({b(5), b(6), b(7)});
    });
    car->method("getNOSSetting", [get_byte](Vm&, const Value&, Args a) { return arg_int(a, 0) == 0 ? get_byte(0x167) : number(-1); });
    car->method("setNOSSetting", [put_byte](Vm&, const Value&, Args a) {
        if (arg_int(a, 0) == 0) put_byte(0x167, arg_int(a, 1));
        return Value{};
    });
    // Gears: 0 reverse, 1 .. 7 the gears, 8 the final drive (x 1000, u16), 9 the slider (u8),
    // 10 the final drive the slider's set was made with.
    auto gear_at = [](s32 i) -> u32 { return i == 0 ? 0x110 : i >= 1 && i <= 7 ? 0x110 + 2 * u32(i) : i == 8 ? 0x128 : i == 9 ? 0x12A : i == 10 ? 0x12C : 0; };
    car->method("getGearSetting", [rec, gear_at](Vm&, const Value&, Args a) {
        const s32 i = arg_int(a, 0);
        const u32 at = gear_at(i);
        if (!at) return number(-1);
        const u8* eq = rec().active();
        return number(i == 9 ? s32(eq[at]) : s32(eq[at] | eq[at + 1] << 8));
    });
    car->method("setGearSetting", [rec, gear_at](Vm&, const Value&, Args a) {
        const s32 i = arg_int(a, 0), v = arg_int(a, 1);
        const u32 at = gear_at(i);
        if (!at) return Value{};
        u8* eq = rec().active();
        eq[at] = u8(v);
        if (i != 9) eq[at + 1] = u8(v >> 8);
        return Value{};
    });
    // getGearRange: [0, 1] the final drive's ends, [2, 3] the slider's, [4] the forward gears,
    // [7 + 2g], [8 + 2g] each gear's ends. ❓ The gears' ends are made by a routine of the
    // simulation (0x3553A0) not written here: 0.
    car->method("getGearRange", [row_of](Vm&, const Value&, Args) {
        const ByteSpan r = row_of(11);
        std::vector<Value> out(23, number(0));
        out[0] = number(r16(r, 0x18)), out[1] = number(r16(r, 0x1A));
        out[2] = number(r.size() > 0x26 ? r[0x26] : 0), out[3] = number(r.size() > 0x27 ? r[0x27] : 0), out[4] = number(r.size() > 0x24 ? r[0x24] : 0);
        return Value::array(std::move(out));
    });
    car->method("setGearAuto", [rec, db](Vm&, const Value&, Args) {
        u8* eq = rec().active();
        eq[0x12C] = eq[0x128], eq[0x12D] = eq[0x129];
        sim::Equipment e;
        std::memcpy(e.bytes.data(), eq, sim::Equipment::kSize);
        if (db().db && sim::computed_gears(*db().db, db().program, e)) std::memcpy(eq, e.bytes.data(), sim::Equipment::kSize);
        return Value{};
    });
    // ❓ The charts' points (0x148940, 0x148B60) are for the graph faces: none here.
    car->method("buildGearRatioData", [](Vm&, const Value&, Args) { return Value{}; });
    // The graph faces' engine curves (menu-draw's faces_graph.*).
    car->method("buildEngineCurveData", [c, update, rec, db](Vm&, const Value&, Args) {
        update();
        if (!db().db)
            return Value::array({Value{}, Value{}, Value::number(float(c->power100) / 100.0f), Value::number(float(c->torque100) / 100.0f), Value::number(0), Value::number(0)});
        sim::Equipment eq;
        std::memcpy(eq.bytes.data(), rec().active(), sim::Equipment::kSize);
        sim::Spec spec;
        sim::build_spec(*db().db, db().program, eq, 0, spec);
        return engine_curve_value(menu::engine_curve(spec));
    });
    // The engine speeds of the spec (+0xBD rev limit, +0xBE shift limit, +0x110 the red line).
    auto engine_speed = [rec, db](u32 at) {
        return [rec, db, at](Vm& vm, const Value&, Args a) {
            if (!db().db) return text("");
            sim::Equipment eq;
            std::memcpy(eq.bytes.data(), rec().active(), sim::Equipment::kSize);
            sim::Spec spec;
            sim::build_spec(*db().db, db().program, eq, 0, spec);
            const Value fmt = a.empty() ? text("%d%s") : a[0];
            return format_with(vm, fmt, {number(s32(spec.b(at)) * 100), text("rpm")});
        };
    };
    car->method("getRevLimit", engine_speed(0xBD));
    car->method("getShiftLimit", engine_speed(0xBE));
    car->method("getRedLine", engine_speed(0x110));

    (void)d;
    return object_value(car);
}

void install_car_class(GameNatives& n, script::Module& menu) {
    GameNatives* np = &n;
    auto cls = make_class(menu, "MCarGarage");
    // MCarGarage([label[, paint]]): a car of its own.
    cls->construct = [np](Vm&, Args a) {
        auto c = std::make_shared<CarHandle>();
        c->state = &np->state();
        if (!a.empty() && a[0].kind == Kind::String) {
            c->label = a[0].as_string();
            set_car(np->db(), c->own, c->label, arg_int(a, 1));
        }
        return make_car_value(*np, c);
    };
    // sortByX(cars[, ascending]): in place, one key each (0x1415E0 on).
    struct Order {
        const char* name;
        std::function<Value(Vm&, const Value&)> key;
    };
    auto handle = [](const Value& car) -> CarHandle* {
        const std::shared_ptr<CarObject> o = std::dynamic_pointer_cast<CarObject>(car.p);
        return o ? o->car.get() : nullptr;
    };
    auto row_key = [np, handle](size_t at, int bytes, bool is_signed) {
        return [np, handle, at, bytes, is_signed](Vm&, const Value& car) {
            const CarHandle* c = handle(car);
            const ByteSpan row = c ? np->db().car_row(c->label) : ByteSpan{};
            if (row.size() < at + size_t(bytes)) return Value::number(0);
            s64 v = bytes == 4 ? s64(r32(row, at)) : bytes == 2 ? s64(is_signed ? s16(r16(row, at)) : r16(row, at)) : s64(row[at]);
            return Value::number(float(v));
        };
    };
    auto member = [](const char* m) {
        return [m](Vm& vm, const Value& car) {
            Value v = vm.attribute(car, m);
            if (v.p && v.p->callable()) v = vm.call(v, {});
            return v;
        };
    };
    const std::vector<Order> orders = {
        {"sortByPower", [handle](Vm&, const Value& car) { const CarHandle* c = handle(car); return Value::number(c ? float(c->power100) : 0); }},
        {"sortByWeight", [handle](Vm&, const Value& car) { const CarHandle* c = handle(car); return Value::number(c ? float(c->weight) : 0); }},
        {"sortByPWR", [handle](Vm&, const Value& car) {
             const CarHandle* c = handle(car);
             return Value::number(c && c->power100 > 0 ? float(c->weight) / (float(c->power100) / 100.0f) : 0.0f);
         }},
        {"sortByDate", [handle](Vm&, const Value& car) { const CarHandle* c = handle(car); return Value::number(c ? float(c->day) : 0); }},
        {"sortByRideHistory", [handle](Vm&, const Value& car) { const CarHandle* c = handle(car); return Value::number(c ? float(c->ride_history) : 0); }},
        {"sortByAspiration", member("getAspiration")},
        {"sortByName", [member](Vm& vm, const Value& car) {
             std::string s = member("short_name")(vm, car).to_string();
             for (char& ch : s) ch = char(std::tolower(u8(ch)));
             return text(s);
         }},
        {"sortByPrice", row_key(0x08, 4, false)},
        {"sortByYear", row_key(0x0C, 2, true)},
        {"sortByCountry", row_key(0x0E, 1, false)},
        {"sortByTuner", row_key(0x10, 1, false)},
        {"sortByDriveTrain", member("getDTrainType")},
        {"sortByColor", [np, handle](Vm&, const Value& car) {
             // 0x43DDB0: whites, greys to black, then the hues from 120 degrees before red.
             const CarHandle* c = handle(car);
             if (!c) return Value::number(0);
             const std::vector<Database::Paint>& paints = np->db().spec(c->label).paints;
             const int i = s8(c->record().active()[0x18]);
             if (i < 0 || size_t(i) >= paints.size()) return Value::number(0);
             const Database::Paint& p = paints[size_t(i)];
             const float r = p.rgb[0] / 255.0f, g = p.rgb[1] / 255.0f, b = p.rgb[2] / 255.0f;
             const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), v = mx, s = mx > 0 ? (mx - mn) / mx : 0;
             float h = 0;
             if (mx > mn) h = mx == r ? 60 * std::fmod((g - b) / (mx - mn), 6.0f) : mx == g ? 60 * ((b - r) / (mx - mn) + 2) : 60 * ((r - g) / (mx - mn) + 4);
             if (h < 0) h += 360;
             float key = s >= 0.25f ? (std::fmod(h + 120, 360.0f) + 152) / 512 : (1 - v) * 152 / 512;
             if ((p.flags[7] & 1) && !(p.flags[3] & 4)) key += 1;
             return Value::number(key);
         }},
    };
    for (const Order& o : orders)
        static_fn(*cls, o.name, [key = o.key](Vm& vm, const Value&, Args a) {
            if (a.empty() || a[0].kind != Kind::Array) return Value{};
            const bool ascending = a.size() < 2 || a[1].truthy();
            std::vector<std::pair<Value, Value>> keyed;
            for (const Value& car : *a[0].items()) keyed.emplace_back(key(vm, car), car);
            std::stable_sort(keyed.begin(), keyed.end(), [&](const auto& x, const auto& y) {
                const Value& p = ascending ? x.first : y.first;
                const Value& q = ascending ? y.first : x.first;
                if (p.kind == Kind::String || q.kind == Kind::String) return p.to_string() < q.to_string();
                return p.as_float() < q.as_float();
            });
            std::vector<Value>& items = *a[0].items();
            for (size_t i = 0; i < keyed.size(); ++i) items[i] = keyed[i].second;
            return Value{};
        });
}

} // namespace gt4r::menu
