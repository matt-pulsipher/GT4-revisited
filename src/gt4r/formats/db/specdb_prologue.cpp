#include "gt4r/formats/db/specdb_prologue.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace gt4r::db::prologue {

namespace {

// A copy of `n` bytes from the Prologue row's `from` to the Gran Turismo 4 row's `to`.
struct Copy {
    u16 to, from, n;
};
// The words that hold a table id (the second word of a {row id, table id} pair).
struct Layout {
    const char* name;
    u16 size; // Gran Turismo 4's row size
    std::vector<Copy> copies;
    std::vector<u16> table_ids; // offsets in Gran Turismo 4's row
};

// The columns as both programs' readers take them (docs/formats/prologue.md "The car database":
// the spec builders 0x404620 of Prologue and 0x4413C8 of Gran Turismo 4, and the rows of the 60
// cars both discs have).
const std::vector<Layout>& layouts() {
    static const std::vector<Layout> k = {
        // The parts' rows start with five bytes Prologue's builder does not read, Gran Turismo 4's
        // with three.
        {"BRAKE", 6, {{0, 2, 6}}, {}},
        {"BRAKECONTROLLER", 11, {{0, 2, 11}}, {}},
        {"ASCC", 25, {{0, 2, 25}}, {}},
        {"TCSC", 10, {{0, 2, 10}}, {}},
        {"CLUTCH", 8, {{0, 2, 8}}, {}},
        {"LSD", 27, {{0, 2, 27}}, {}},
        // The three curves and the lock four bytes on.
        {"STEER", 38, {{0, 4, 38}}, {}},
        // The ride heights after four bytes; the rest two bytes on; Gran Turismo 4's last byte is new.
        {"SUSPENSION", 111, {{0, 4, 12}, {12, 14, 98}}, {}},
        // The same columns but for the widths across the tires and the wheel tops (+0x12 .. +0x19,
        // made below) and the bytes from +0x1F on.
        {"CHASSIS", 37, {{0, 0, 0x12}, {0x1A, 0x12, 5}}, {}},
        // The parts' columns after 0x1C bytes Prologue's builder does not read; no part that
        // changes with speed (+0x18 .. +0x1C).
        {"RACINGMODIFY", 29, {{0, 0x1C, 0x18}}, {}},
        // The inertias and frictions four bytes on, the drive and torque split two.
        {"DRIVETRAIN", 21, {{0, 4, 12}, {0x0E, 0x10, 7}}, {}},
        // Seven gears (Gran Turismo 4: eleven), reverse, the final drive's range; no second
        // reduction (+0x1E, +0x29).
        {"GEAR", 45, {{0, 4, 14}, {0x16, 0x12, 8}, {0x22, 0x1A, 7}}, {}},
        // The texts; four more bytes before the sound, power and torque; a curve of 16 points
        // (Gran Turismo 4: 24); the output factor last.
        {"ENGINE", 113, {{0, 0, 0x18}, {0x18, 0x1C, 6}, {0x1E, 0x22, 32}, {0x4E, 0x42, 7}, {0x55, 0x49, 16}, {0x6F, 0x59, 1}}, {}},
        {"TURBINEKIT", 18, {{0, 0, 18}}, {}},
        {"MUFFLER", 7, {{0, 4, 4}, {6, 8, 1}}, {}},
        // Seven parts {row, table}; the price (u16 here, u32 there) and the kind.
        {"FRONTTIRE", 59, {{0, 0, 0x3A}, {0x3A, 0x3C, 1}}, {4, 0x0C, 0x14, 0x1C, 0x24, 0x2C, 0x34}},
        {"REARTIRE", 57, {{0, 0, 0x38}, {0x38, 0x3C, 1}}, {4, 0x0C, 0x14, 0x1C, 0x24, 0x2C, 0x34}},
        {"TIRESIZE", 4, {{0, 0, 4}}, {}},
        {"TIRECOMPOUND", 90, {{0, 0, 90}}, {}},
        // Surfaces 0 .. 9, then the wet variants of 0, 1 and 9 (Gran Turismo 4: 20, 22, 21 at
        // +0x0A, +0x0B, +0x0C); none of 10 .. 14 (0: the spec builder's default).
        {"TIREFORCEVOL", 18, {{0, 0, 10}, {0x0A, 0x0A, 1}, {0x0C, 0x0B, 1}, {0x0B, 0x0C, 1}}, {}},
        // The car's parts as sold; nothing of the price, year, makers and kinds.
        {"GENERIC_CAR", 24, {{0, 0, 8}}, {4}},
        {"DEFAULT_PARTS", 208, {{0, 0, 208}}, {}},
        {"DEFAULT_PARAM", 37, {{0, 0, 37}}, {}},
        {"ENEMY_CARS", 24, {{0, 0, 24}}, {4, 0x0C, 0x14}},
        // The paint's texts, the body's box and the color.
        {"VARIATION", 52, {{0, 0, 39}}, {}},
        {"CAR_VARIATION_", 8, {{0, 0, 8}}, {4}},
        {"COURSE_NAME_", 4, {{0, 0, 4}}, {}},
        // The name; Gran Turismo 4's row has it three times, then the short name.
        {"CAR_NAME_", 16, {{0, 0, 4}, {4, 0, 4}, {8, 0, 4}, {12, 4, 4}}, {}},
        // The course's texts and its surface (+0x10: 0 dry, 1 wet, 2 dirt, 3 snow).
        {"COURSE", 22, {{0, 0, 8}, {0x10, 8, 1}}, {}},
        // The course {row, table}; the grid's size and the computer drivers' columns where the
        // race's copy of a row (Gran Turismo 4 0x35E108, Prologue 0x3C5978) reads them; the laps (+0x29, the race's
        // getter 0x37CFD0: 2, 3 or 5 on the championship races by their variant, 2 on lesson 1); the gold, silver and bronze times as
        // the program reads them (0x409B70 / 0x409BB0 / 0x409BF0: minutes +0x5C / +0x5E / +0x60, seconds +0x5D / +0x5F /
        // +0x61, u16 milliseconds +0x0C / +0x0E / +0x10; all 0: none); ❓ the rest.
        // A test's rules (the race's copy 0x3C5978: the rule block +0x02 the kind ← +0x2A, +0x41 ← the u16 +0x12, +0x44 ← +0x14,
        // +0x46 ← +0x5B, +0x48 ← +0x18 (the car-hit limit), +0x49 ← +0x16 (the wall-hit limit), +0x4A ← +0x1A, each through the
        // row's u16 the switch 0x409AB8 picks; their low bytes) in Gran Turismo 4's columns, as its judge reads them.
        {"RACE", 151,
         {{0, 0, 8}, {0x3E, 0x22, 1}, {0x43, 0x29, 1}, {0x44, 0x2A, 1}, {0x4B, 0x31, 0x27}, {0x72, 0x5A, 2}, {0x75, 0x5B, 1}, {0x76, 0x5C, 6},
          {0x1A, 0x0C, 6}, {0x86, 0x12, 1}, {0x87, 0x14, 1}, {0x88, 0x1A, 1}, {0x8A, 0x16, 1}, {0x8B, 0x18, 1}, {0x42, 0x27, 1}, {0x14, 0x1E, 2}},
         {4}},
    };
    return k;
}

const Layout* layout_of(const std::string& name) {
    for (const Layout& l : layouts()) {
        const std::string n = l.name;
        if (n.back() == '_' ? name.compare(0, n.size(), n) == 0 : name == n) return &l;
    }
    // Gran Turismo 4's VARIATION<language> is Prologue's one VARIATION.
    if (name.compare(0, 9, "VARIATION") == 0) return layout_of("VARIATION");
    return nullptr;
}

// The car of a part's label ("ch__117_coupe_68_a": "_117_coupe_68").
std::string car_of(const std::string& label) {
    const size_t a = label.find('_'), b = label.rfind('_');
    return a == std::string::npos || b <= a ? std::string() : label.substr(a + 1, b - a - 1);
}

void put16(Bytes& d, size_t o, u32 v) {
    d[o] = u8(v), d[o + 1] = u8(v >> 8);
}

// The part of table `table_id` among a car's parts as sold: its GENERIC_CAR row's set of
// DEFAULT_PARTS (the pair of table t at pair t - 1).
std::optional<u32> part_of(SpecDb& db, const std::string& car, u32 table_id) {
    const Labels* l = db.labels("GENERIC_CAR");
    const Table* cars = db.table("GENERIC_CAR");
    const Table* sets = db.table("DEFAULT_PARTS");
    const std::optional<u32> id = l ? l->id(car) : std::nullopt;
    if (!id || !cars || !sets) return std::nullopt;
    const ByteSpan c = cars->row(*id);
    if (c.size() < 8) return std::nullopt;
    const ByteSpan set = sets->row(rd32(c, 0));
    const size_t o = 8 * size_t(table_id - 1);
    if (table_id == 0 || set.size() < o + 8 || rd32(set, o + 4) != table_id || rd32(set, o) == 0xFFFFFFFFu) return std::nullopt;
    return rd32(set, o);
}

// CHASSIS +0x12 / +0x14: the width across a pair of tires' outer faces, the tread and a tire's
// width; +0x16 / +0x18: how high the wheel's top may reach, the tire's diameter. Prologue has
// neither: they are made from the car's RACINGMODIFY (+0x02 / +0x04 tread) and tires.
void chassis_wheels(SpecDb& db, const std::string& label, Bytes& row) {
    const std::string car = car_of(label);
    const Table* rm = db.table("RACINGMODIFY");
    const Table* sizes = db.table("TIRESIZE");
    const std::optional<u32> body = part_of(db, car, 7);
    if (!rm || !sizes || !body) return;
    const ByteSpan r = rm->row(*body);
    if (r.size() < 6) return;
    for (int axle = 0; axle < 2; ++axle) {
        const std::optional<u32> tire = part_of(db, car, axle == 0 ? 25 : 26);
        const Table* tt = db.table(axle == 0 ? "FRONTTIRE" : "REARTIRE");
        if (!tire || !tt) continue;
        const ByteSpan t = tt->row(*tire);
        if (t.size() < 4) continue;
        const ByteSpan s = sizes->row(rd32(t, 0));
        if (s.size() < 4) continue;
        const float rim_in = s[2] < 61 ? float(s[2]) : float(s[2]) / 10.0f;
        const float width = float(s[3]) * 5.0f, aspect = float(s[0]) * 5.0f;
        put16(row, 0x12 + 2 * size_t(axle), rd16(r, 2 + 2 * size_t(axle)) + u32(width));
        put16(row, 0x16 + 2 * size_t(axle), u32(rim_in * 25.4f + 2.0f * width * aspect / 100.0f + 0.5f));
    }
}

} // namespace

bool is_prologue_folder(const std::string& folder) { return folder.find("GT4_PROLOGUE") != std::string::npos; }

bool is_plain_table(ByteSpan d) { return Table::is_table(d) && !(rd32(d, 4) & 1); }

Table parse_plain(ByteSpan d) {
    if (!Table::is_table(d)) throw FormatError("not a GTDB table");
    const u32 flags = rd32(d, 4), rows = rd32(d, 8);
    const size_t row_size = rd32(d, 12);
    if (row_size == 0 || row_size > 0x10000 || 0x10 + u64(rows) * 8 > d.size()) throw FormatError("GTDB: bad header");
    const size_t base = 0x10 + 8 * size_t(rows);
    std::vector<u32> ids;
    Bytes data(size_t(rows) * row_size);
    for (u32 r = 0; r < rows; ++r) {
        ids.push_back(rd32(d, 0x10 + 8 * size_t(r)));
        const size_t at = base + rd32(d, 0x10 + 8 * size_t(r) + 4);
        if (at + row_size > d.size()) throw FormatError("GTDB: row out of range");
        std::memcpy(data.data() + size_t(r) * row_size, d.data() + at, row_size);
    }
    if (!std::is_sorted(ids.begin(), ids.end())) throw FormatError("GTDB: rows not in id order");
    return Table::make(flags, row_size, std::move(ids), std::move(data));
}

std::string file_name(const std::string& name) {
    if (name.compare(0, 9, "VARIATION") == 0) return "VARIATION";
    return name;
}

u32 table_id(u32 id) {
    // Prologue has no NOS, SUPERCHARGER, WHEEL or WING (Gran Turismo 4's 27 .. 30).
    return id >= 27 && id <= 38 ? id + 4 : id;
}

Table to_gt4(const std::string& name, const Table& t, SpecDb& db) {
    const Layout* l = layout_of(name);
    if (!l) return t;
    const Labels* labels = db.labels(name);
    std::vector<u32> ids = t.ids();
    Bytes data(ids.size() * l->size);
    for (size_t i = 0; i < ids.size(); ++i) {
        const ByteSpan from = t.row_at(i);
        Bytes row(l->size);
        for (const Copy& c : l->copies)
            for (size_t k = 0; k < c.n; ++k)
                if (c.to + k < row.size() && c.from + k < from.size()) row[c.to + k] = from[c.from + k];
        for (u16 o : l->table_ids)
            if (o + 4u <= row.size()) {
                const u32 v = table_id(rd32(row, o));
                for (size_t k = 0; k < 4; ++k) row[o + k] = u8(v >> (8 * k));
            }
        if (name == "CHASSIS" && labels)
            if (const std::string* label = labels->label(ids[i])) chassis_wheels(db, *label, row);
        std::memcpy(data.data() + i * l->size, row.data(), l->size);
    }
    return Table::make(t.flags(), l->size, std::move(ids), std::move(data));
}

Labels to_gt4(Labels l) {
    l.table_id = table_id(l.table_id);
    return l;
}

std::string races_file(u32 race_row) {
    char name[32];
    std::snprintf(name, sizeof name, "RACES/%08X%08X", 31u, race_row);
    return name;
}

} // namespace gt4r::db::prologue
