#include "gt4r/formats/db/mod_cars.h"

#include "gt4r/formats/db/car_parts.h"

#include "base/config.h"
#include "base/file.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>

namespace gt4r::db {

namespace {

using gt3r::IniDocument;
using gt3r::s8;
using gt3r::to_lower;
using gt3r::trim;

void put8(Bytes& b, size_t o, u32 v) {
    if (o < b.size()) b[o] = u8(v);
}
void put16(Bytes& b, size_t o, u32 v) {
    if (o + 2 <= b.size()) b[o] = u8(v), b[o + 1] = u8(v >> 8);
}
void put32(Bytes& b, size_t o, u32 v) {
    if (o + 4 <= b.size())
        for (size_t i = 0; i < 4; ++i) b[o + i] = u8(v >> (8 * i));
}
void putf(Bytes& b, size_t o, float v) {
    if (o + 4 <= b.size()) std::memcpy(b.data() + o, &v, 4);
}
u32 get8(const Bytes& b, size_t o) { return o < b.size() ? b[o] : 0; }
u32 get16(const Bytes& b, size_t o) { return o + 2 <= b.size() ? u32(b[o] | b[o + 1] << 8) : 0; }
u32 get32(const Bytes& b, size_t o) { return o + 4 <= b.size() ? rd32(ByteSpan(b), o) : 0; }
float getf(const Bytes& b, size_t o) {
    float v = 0;
    if (o + 4 <= b.size()) std::memcpy(&v, b.data() + o, 4);
    return v;
}
u32 clamp8(double v) { return u32(std::clamp(std::lround(v), 0L, 255L)); }
u32 clamp16(double v) { return u32(std::clamp(std::lround(v), 0L, 65535L)); }

// A value without a trailing " ; comment" / " # comment".
std::string value_text(std::string_view v) {
    for (size_t i = 1; i < v.size(); ++i)
        if ((v[i] == ';' || v[i] == '#') && (v[i - 1] == ' ' || v[i - 1] == '\t')) {
            v = v.substr(0, i);
            break;
        }
    return std::string(trim(v));
}

std::optional<double> number(std::string_view s) {
    const std::string t(trim(s));
    if (t.empty()) return std::nullopt;
    char* end = nullptr;
    const double v = t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X') ? double(std::strtoull(t.c_str(), &end, 16)) : std::strtod(t.c_str(), &end);
    // (files come from other players too: no value needs more, and every conversion below stays defined)
    if (!end || *end || !std::isfinite(v) || std::fabs(v) > 1e9) return std::nullopt;
    return v;
}

std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == ',' || c == '\t') {
            if (!cur.empty()) out.push_back(std::move(cur)), cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

bool valid_label(const std::string& l) {
    if (l.empty() || l.size() > 40) return false;
    for (char c : l)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

bool yes(const std::string& v) {
    const std::string l = to_lower(v);
    return l != "no" && l != "0" && l != "off" && l != "false";
}

// The tables a car file's values change: their rows the car names are cloned for it.
constexpr u32 kCloned[] = {u32(PartTable::Brake),       u32(PartTable::Suspension), u32(PartTable::Chassis),   u32(PartTable::RacingModify),
                           u32(PartTable::Drivetrain), u32(PartTable::Gear),       u32(PartTable::Engine),    u32(PartTable::FrontTire),
                           u32(PartTable::RearTire)};
// Of those, the tables whose every row the car names takes the file's values (the shop's parts with the stock one); the
// others' values are the stock part's alone, and only the stock rows are cloned.
bool every_row(u32 t) {
    return t == u32(PartTable::Brake) || t == u32(PartTable::Suspension) || t == u32(PartTable::Chassis) || t == u32(PartTable::RacingModify) ||
           t == u32(PartTable::FrontTire) || t == u32(PartTable::RearTire);
}
constexpr u32 kDefaultParts = 36, kEnemyCars = 38, kVariation = 42;

std::optional<u32> table_id_of(std::string_view name) {
    for (u32 t = 0; t < u32(PartTable::Count); ++t)
        if (name == part_table_name(t)) return t;
    return std::nullopt;
}

} // namespace

std::optional<ModCarDef::TireSize> parse_tire_size(std::string_view s) {
    // "255/35R21", "255/35 R21", "255/35ZR21", "205/60R15.5"
    std::string t;
    for (char c : s)
        if (c != ' ') t.push_back(char(std::toupper(static_cast<unsigned char>(c))));
    const size_t slash = t.find('/'), r = slash == std::string::npos ? std::string::npos : t.find('R', slash);
    if (slash == std::string::npos || r == std::string::npos) return std::nullopt;
    std::string asp = t.substr(slash + 1, r - slash - 1);
    if (!asp.empty() && asp.back() == 'Z') asp.pop_back();
    const auto w = number(t.substr(0, slash)), a = number(asp), d = number(t.substr(r + 1));
    if (!w || !a || !d) return std::nullopt;
    const int wi = int(*w), ai = int(*a), di = int(std::lround(*d * 10));
    // TIRESIZE keeps a byte each: width / 5, aspect / 5, the rim in tenths of an inch (a stored value below 61 reads as inches).
    if (wi != *w || ai != *a || wi % 5 || ai % 5 || wi < 100 || wi > 1275 || ai < 5 || ai > 100 || di < 100 || di > 255) return std::nullopt;
    return ModCarDef::TireSize{u8(wi / 5), u8(ai / 5), u8(di)};
}

std::optional<ModCarDef> parse_mod_car(std::string_view text, std::string* error, std::vector<std::string>* warnings) {
    auto fail = [&](const std::string& m) -> std::optional<ModCarDef> {
        if (error) *error = m;
        return std::nullopt;
    };
    auto warn = [&](const std::string& m) {
        if (warnings) warnings->push_back(m);
    };
    const IniDocument ini = IniDocument::parse(text);
    ModCarDef d;
    for (const IniDocument::Section& sec : ini.sections()) {
        const std::string sn = to_lower(sec.name);
        std::string raw_table;
        if (sn.rfind("raw.", 0) == 0) {
            std::string t = sec.name.substr(4);
            for (char& c : t) c = char(std::toupper(static_cast<unsigned char>(c)));
            const std::optional<u32> id = table_id_of(t);
            if (!id || *id == 0 || std::find(std::begin(kCloned), std::end(kCloned), *id) == std::end(kCloned))
                return fail("[" + sec.name + "]: not a table of the car's own rows (BRAKE, SUSPENSION, CHASSIS, RACINGMODIFY, DRIVETRAIN, GEAR, ENGINE, FRONTTIRE, REARTIRE)");
            raw_table = t;
        }
        for (const auto& [key_raw, val_raw] : sec.entries) {
            const std::string key = to_lower(key_raw), v = value_text(val_raw);
            auto num = [&]() -> std::optional<double> {
                const auto n = number(v);
                if (!n) warn("[" + sec.name + "] " + key + ": not a number: " + v);
                return n;
            };
            auto set_u16 = [&](std::optional<u16>& o) {
                if (const auto n = num()) o = u16(clamp16(*n));
            };
            auto set_u8 = [&](std::optional<u8>& o) {
                if (const auto n = num()) o = u8(clamp8(*n));
            };
            auto set_f = [&](std::optional<float>& o) {
                if (const auto n = num()) o = float(*n);
            };
            bool known = true;
            if (!raw_table.empty()) {
                // 0x1D = 120   0x30:u16 = 1000   0x44:f32 = 2.1
                std::string off = key, type = "u8";
                if (const size_t c = key.find(':'); c != std::string::npos) off = key.substr(0, c), type = key.substr(c + 1);
                const auto o = number(off), n = number(v);
                const char t = type == "u8" ? 'b' : type == "u16" ? 'h' : type == "u32" ? 'i' : type == "f32" ? 'f' : 0;
                if (!o || !n || !t || *o < 0) return fail("[" + sec.name + "] bad value '" + key_raw + " = " + v + "'");
                d.pokes.push_back({raw_table, size_t(*o), t, *n});
                continue;
            }
            if (sn == "car") {
                if (key == "label") d.label = v;
                else if (key == "name") d.name = v;
                else if (key == "maker") d.maker = v;
                else if (key == "donor") d.donor = v;
                else if (key == "model") d.body = v;
                else if (key == "year") set_u16(d.year);
                else if (key == "price") {
                    if (const auto n = num()) d.price = u32(std::clamp(*n, 0.0, 4.0e9));
                } else if (key == "category") {
                    const std::string c = to_lower(v);
                    if (c == "normal" || c == "production") d.category = 0;
                    else if (c == "racing" || c == "race") d.category = 1;
                    else if (c == "tuning") d.category = 2;
                    else if (c == "concept") d.category = 3;
                    else if (c == "rally") warn("[car] category: this game has no rally category; the donor's is kept");
                    else return fail("[car] category is normal, racing, tuning or concept, not '" + v + "'");
                } else if (key == "fit_body" || key == "driver_side") {
                    // (the body's: read with [model])
                } else known = false;
            } else if (sn == "body") {
                if (key == "weight_kg") set_u16(d.weight_kg);
                else if (key == "front_weight_pct") set_u8(d.front_weight_pct);
                else if (key == "length_mm") set_u16(d.length_mm);
                else if (key == "width_mm") set_u16(d.width_mm);
                else if (key == "height_mm") set_u16(d.height_mm);
                else if (key == "wheelbase_mm") set_u16(d.wheelbase_mm);
                else if (key == "front_track_mm") set_u16(d.front_track_mm);
                else if (key == "rear_track_mm") set_u16(d.rear_track_mm);
                else if (key == "cd") set_f(d.cd);
                else if (key == "frontal_area_m2") set_f(d.frontal_area_m2);
                else if (key == "ride_height_mm") set_u16(d.ride_height_mm);
                else if (key == "scale_suspension") d.scale_suspension = yes(v);
                else if (key == "scale_brakes") d.scale_brakes = yes(v);
                else known = false;
            } else if (sn == "powertrain") {
                if (key == "type") {
                    const std::string t = to_lower(v);
                    if (t != "electric" && t != "engine" && t != "combustion") return fail("[powertrain] type is electric or engine");
                    d.electric = t == "electric";
                } else if (key == "power_kw") set_f(d.power_kw);
                else if (key == "power_hp") {
                    if (const auto n = num()) d.power_kw = float(*n * 0.7457);
                } else if (key == "power_ps") {
                    if (const auto n = num()) d.power_kw = float(*n * 0.73549875);
                } else if (key == "rated_kw") set_f(d.rated_kw);
                else if (key == "torque_nm") set_f(d.torque_nm);
                else if (key == "torque_lbft") {
                    if (const auto n = num()) d.torque_nm = float(*n * 1.35582);
                } else if (key == "max_rpm") set_f(d.max_rpm);
                else if (key == "idle_rpm") set_f(d.idle_rpm);
                else if (key == "output_pct") set_u8(d.output_pct);
                else if (key == "sound_id") set_u16(d.sound_id);
                else if (key == "layout") d.layout_text = v;
                else if (key == "aspiration") d.aspiration_text = v;
                else if (key == "displacement") d.displacement_text = v;
                else if (key == "curve") {
                    for (const std::string& w : words(v)) {
                        const size_t c = w.find(':');
                        const auto r = c == std::string::npos ? std::nullopt : number(w.substr(0, c));
                        const auto t = c == std::string::npos ? std::nullopt : number(w.substr(c + 1));
                        if (!r || !t || *r < 0 || *t < 0) return fail("[powertrain] curve: bad point '" + w + "' (rpm:Nm)");
                        d.curve.push_back({float(*r), float(*t)});
                    }
                    if (d.curve.size() > 24) return fail("[powertrain] curve: at most 24 points");
                } else if (key == "base_rpm" || key == "regen_g" || key == "regen_kw" || key == "regen_torque_nm" || key == "motor_inertia") {
                    // (the electric drive's)
                } else known = false;
            } else if (sn == "drivetrain") {
                if (key == "drive") {
                    const std::string t = to_lower(v);
                    if (t == "fr") d.drive = 0;
                    else if (t == "ff") d.drive = 1;
                    else if (t == "awd" || t == "4wd") d.drive = 2;
                    else if (t == "mr") d.drive = 3;
                    else if (t == "rr") d.drive = 4;
                    else return fail("[drivetrain] drive is FF, FR, MR, RR or AWD");
                } else if (key == "front_torque_pct") set_u8(d.front_torque_pct);
                else if (key == "gears") {
                    d.gears.clear();
                    for (const std::string& w : words(v)) {
                        const auto n = number(w);
                        if (!n || *n <= 0 || *n > 65) return fail("[drivetrain] gears: bad ratio '" + w + "'");
                        d.gears.push_back(float(*n));
                    }
                    if (d.gears.size() > 11) return fail("[drivetrain] gears: at most 11");
                } else if (key == "final_drive") set_f(d.final_drive);
                else if (key == "reverse") set_f(d.reverse);
                else known = false;
            } else if (sn == "tires") {
                if (key == "front" || key == "rear" || key == "size") {
                    const auto t = parse_tire_size(v);
                    if (!t) return fail("[tires] " + key + ": bad size '" + v + "' (e.g. 255/35R21; width and aspect in fives)");
                    if (key != "rear") d.front_tire = t;
                    if (key != "front") d.rear_tire = t;
                } else if (key == "grip_pct") set_f(d.grip_pct);
                else known = false;
            } else if (sn == "model") {
                if (key == "file") d.model_file = v;
                else if (key == "menu") d.model_menu = v;
                else if (key == "race") d.model_race = v;
                else if (key == "day") d.model_time[0] = v;
                else if (key == "eve") d.model_time[1] = v;
                else if (key == "night") d.model_time[2] = v;
                else if (key == "wheel") d.wheel = v;
                else if (key == "lower_mm") {
                    if (const auto n = num()) d.model_lower_mm = float(*n);
                } else known = false;
            } else if (sn == "paints") {
                std::string hex = v;
                if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
                char* end = nullptr;
                const unsigned long rgb = hex.size() == 6 ? std::strtoul(hex.c_str(), &end, 16) : 0;
                if (hex.size() != 6 || !end || *end) return fail("[paints] " + key_raw + ": #RRGGBB, not '" + v + "'");
                d.paints.push_back({key_raw, {u8(rgb >> 16), u8(rgb >> 8), u8(rgb)}});
            } else if (sn == "dealer") {
                if (key == "for_sale") d.for_sale = yes(v);
                else if (key == "description") d.description = v;
                else if (key == "picture") d.picture = v;
                else if (key == "thumb") d.thumb = v;
                else if (key == "logo") d.logo = v;
                else if (key == "picture_paint") {
                } else known = false;
            } else if (sn == "opponent") {
                if (key == "races")
                    for (const std::string& w : words(v)) d.opponent_races.push_back(to_lower(w));
                else known = false;
            } else if (sn == "reference" || sn == "arcade" || sn == "sound") {
                // ([reference]: figures for the checking tool; [arcade], [sound]: the other game's)
            } else {
                known = false;
            }
            if (!known) warn("[" + sec.name + "] unknown key '" + key_raw + "'");
        }
    }
    if (!valid_label(d.label)) return fail("[car] label is 1 to 40 characters of a-z, 0-9 and _");
    if (d.name.empty()) return fail("[car] name is needed");
    if (d.donor.empty()) return fail("[car] donor is needed (a car of the disc its other parts are taken from)");
    if (d.donor == d.label) return fail("[car] the donor is another car");
    if (d.max_rpm && (*d.max_rpm < 1000 || *d.max_rpm > 25500)) return fail("[powertrain] max_rpm: 1000 to 25500");
    if (d.front_weight_pct && (*d.front_weight_pct < 1 || *d.front_weight_pct > 99)) return fail("[body] front_weight_pct: 1 to 99");
    if (d.weight_kg && *d.weight_kg < 100) return fail("[body] weight_kg: at least 100");
    return d;
}

u32 mod_row_id(std::string_view car_label, std::string_view table, u32 n) {
    // FNV-1a over the label, the table and the number.
    u32 h = 2166136261u;
    auto add = [&h](u8 c) { h = (h ^ c) * 16777619u; };
    for (char c : car_label) add(u8(c));
    add(0);
    for (char c : table) add(u8(c));
    add(0);
    for (int i = 0; i < 4; ++i) add(u8(n >> (8 * i)));
    return 0x40000000u | (h & 0x3FFFFFFFu);
}

// ---- building -----------------------------------------------------------------------------------------------------

struct ModCarBuilder {
    SpecDb& db;
    ModCars& out;
    const std::map<int, std::string>& makers;
    std::map<std::string, std::set<u32>> used; // the ids taken in each table by the cars before

    // A table's rows by id as the disc has them (and the cars built before).
    ByteSpan disc_row(const std::string& table, u32 id) {
        for (const auto& [rid, bytes] : out.rows_[table])
            if (rid == id) return ByteSpan(bytes);
        const Table* t = db.table(table);
        return t ? t->row(id) : ByteSpan();
    }
    std::string label_of(const std::string& table, u32 id) {
        const Labels* l = db.labels(table);
        const std::string* s = l ? l->label(id) : nullptr;
        return s ? *s : std::string();
    }
    std::optional<u32> id_of(const std::string& table, const std::string& label) {
        const Labels* l = db.labels(table);
        return l ? l->id(label) : std::nullopt;
    }
    u32 new_id(const std::string& label, const std::string& table, u32 n) {
        u32 id = mod_row_id(label, table, n);
        std::set<u32>& taken = used[table];
        while (taken.count(id)) id = 0x40000000u | ((id + 1) & 0x3FFFFFFFu); // (another mod car's: the next one)
        taken.insert(id);
        return id;
    }
    u32 pool_index(const std::string& pool, const std::string& s) {
        const Strings* base = db.strings(pool);
        if (!base) return 0;
        out.pool_sizes_.emplace(pool, base->strings.size());
        std::vector<std::string>& added = out.strings_[pool];
        for (size_t i = 0; i < base->strings.size(); ++i)
            if (base->strings[i] == s) return u32(i);
        for (size_t i = 0; i < added.size(); ++i)
            if (added[i] == s) return u32(base->strings.size() + i);
        added.push_back(s);
        return u32(base->strings.size() + added.size() - 1);
    }
    // The donor's label in a label replaced by the car's ("ch_<donor>_a" -> "ch_<car>_a"); "<table>_<car>_<n>" when it has none.
    static std::string relabel(const std::string& l, const ModCarDef& d, const std::string& table, u32 n) {
        const size_t p = l.find(d.donor);
        if (p != std::string::npos) return l.substr(0, p) + d.label + l.substr(p + d.donor.size());
        std::string t = to_lower(table);
        return t + "_" + d.label + "_" + std::to_string(n);
    }

    bool add(const ModCarDef& d, ModCarStatus& st);
};

namespace {

// The car's labels of the donor's DEFAULT_PARTS sets: df_pt_<car>, df_pt_sim_<car>, df_pt_d_<car> (dirt), df_pt_s_<car> (snow).
constexpr const char* kSetPrefixes[] = {"df_pt_", "df_pt_sim_", "df_pt_d_", "df_pt_s_"};

} // namespace

bool ModCarBuilder::add(const ModCarDef& d, ModCarStatus& st) {
    auto fail = [&](const std::string& m) {
        st.error = m;
        return false;
    };
    if (db.prologue()) return fail("Gran Turismo 4 Prologue takes no mod cars");
    const Labels* car_labels = db.labels("GENERIC_CAR");
    const Table* cars = db.table("GENERIC_CAR");
    if (!car_labels || !cars) return fail("the disc's car table is not read");
    if (car_labels->id(d.label)) return fail("'" + d.label + "' is a car of the disc already");
    const std::optional<u32> donor_id = car_labels->id(d.donor);
    if (!donor_id) return fail("the donor '" + d.donor + "' is not a car of this disc");
    const ByteSpan donor_row = cars->row(*donor_id);
    if (donor_row.size() < 0x18) return fail("the donor's car row is not read");
    std::optional<u32> body_id = donor_id;
    if (!d.body.empty() && !(body_id = car_labels->id(d.body))) return fail("[car] model '" + d.body + "' is not a car of this disc");
    // Gran Turismo 4's program has no drive without an engine: its motors (the Priuses' hybrid box, the Insight's assist motor,
    // the DN-X's motors) each work beside one, and a drive of our own would bring behavior the game does not have.
    if (d.electric)
        return fail("[powertrain] type = electric: this game has no electric drive (a hybrid donor keeps its own motor; one gear "
                    "and a curve of your own come closest)");

    // The rows the car names: its sets and the parts that fit it.
    struct Set {
        u32 from;
        std::string label;
    };
    std::vector<Set> sets;
    const u32 car_set = rd32(donor_row, 0);
    sets.push_back({car_set, label_of("DEFAULT_PARTS", car_set)});
    for (const char* p : kSetPrefixes)
        if (const std::optional<u32> s = id_of("DEFAULT_PARTS", p + d.donor); s && *s != car_set) sets.push_back({*s, p + d.donor});
    struct Fit {
        u64 code;
        u8 rest[8];
    };
    std::vector<Fit> fits;
    if (const std::optional<Bytes> f = db.car_file(u64(*donor_id)); f && f->size() >= 8) {
        u64 n = 0;
        std::memcpy(&n, f->data(), 8);
        for (u64 i = 0; i < n && 8 + (i + 1) * 16 <= f->size(); ++i) {
            Fit e{};
            std::memcpy(&e.code, f->data() + 8 + i * 16, 8);
            std::memcpy(e.rest, f->data() + 16 + i * 16, 8);
            fits.push_back(e);
        }
    }

    // Clone them where the file changes their table.
    struct Clone {
        u32 table;
        u32 id;
        Bytes bytes;
        bool stock = false; // named by one of the car's sets
    };
    std::vector<Clone> clones;
    std::map<std::pair<u32, u32>, size_t> remap; // donor's {table, row} -> clones
    std::set<u32> poked;
    for (const ModCarDef::Poke& p : d.pokes) poked.insert(*table_id_of(p.table));
    std::map<u32, u32> counts;
    auto clone = [&](u32 table, u32 row, bool stock) -> std::optional<u32> {
        if (const auto it = remap.find({table, row}); it != remap.end()) {
            clones[it->second].stock = clones[it->second].stock || stock;
            return clones[it->second].id;
        }
        const std::string name = part_table_name(table);
        const ByteSpan r = disc_row(name, row);
        if (r.empty()) return std::nullopt;
        const u32 n = counts[table]++;
        const u32 id = new_id(d.label, name, n);
        out.labels_[name].emplace_back(relabel(label_of(name, row), d, name, n), id);
        remap[{table, row}] = clones.size();
        clones.push_back({table, id, Bytes(r.begin(), r.end()), stock});
        return id;
    };
    auto wanted = [&](u32 table, bool stock) {
        if (std::find(std::begin(kCloned), std::end(kCloned), table) == std::end(kCloned)) return false;
        return stock || every_row(table);
    };
    std::vector<Bytes> set_rows;
    for (const Set& s : sets) {
        const ByteSpan r = disc_row("DEFAULT_PARTS", s.from);
        if (r.size() < 26 * 8) return fail("the donor's parts are not read");
        Bytes b(r.begin(), r.end());
        for (size_t k = 0; k < 26; ++k) {
            const u32 row = rd32(r, 8 * k), t = rd32(r, 8 * k + 4);
            if (row == 0xFFFFFFFFu || !wanted(t, true)) continue;
            if (const std::optional<u32> id = clone(t, row, true)) put32(b, 8 * k, *id);
        }
        set_rows.push_back(std::move(b));
    }
    for (Fit& f : fits) {
        const u32 t = u32(f.code >> 32), row = u32(f.code);
        if (!wanted(t, false)) continue;
        if (const std::optional<u32> id = clone(t, row, false)) f.code = (u64(t) << 32) | *id;
    }
    auto stock_of = [&](PartTable t) -> Bytes* {
        const ByteSpan r = disc_row("DEFAULT_PARTS", car_set);
        for (size_t k = 0; k + 8 <= r.size(); k += 8)
            if (rd32(r, k + 4) == u32(t))
                if (const auto it = remap.find({u32(t), rd32(r, k)}); it != remap.end()) return &clones[it->second].bytes;
        return nullptr;
    };
    Bytes* ch = stock_of(PartTable::Chassis);
    Bytes* en = stock_of(PartTable::Engine);
    Bytes* dt = stock_of(PartTable::Drivetrain);
    Bytes* ge = stock_of(PartTable::Gear);
    Bytes* rm = stock_of(PartTable::RacingModify);
    Bytes* ft = stock_of(PartTable::FrontTire);
    Bytes* rt = stock_of(PartTable::RearTire);
    if (!ch || !en || !dt || !ge || !ft || !rt) return fail("the donor has no chassis, engine, drivetrain, gearbox or tires as sold");
    auto each = [&](PartTable t, auto&& f) {
        for (Clone& c : clones)
            if (c.table == u32(t)) f(c);
    };

    // ---- the tires' sizes (TIRESIZE: +0 aspect / 5, +2 the rim, +3 width / 5) ----
    const Table* sizes = db.table("TIRESIZE");
    auto size_of = [&](const Bytes& tire) -> std::optional<TireSize> {
        const ByteSpan s = sizes ? sizes->row(get32(tire, 0)) : ByteSpan();
        if (s.empty()) {
            for (const auto& [id, b] : out.rows_["TIRESIZE"])
                if (id == get32(tire, 0)) return TireSize::read(b);
            return std::nullopt;
        }
        return TireSize::read(s);
    };
    const std::optional<TireSize> donor_size[2] = {size_of(*ft), size_of(*rt)};
    auto size_id = [&](const ModCarDef::TireSize& want) -> std::optional<u32> {
        if (!sizes || sizes->size() == 0) return std::nullopt;
        const float rim = want.rim_10 * 0.1f;
        for (size_t i = 0; i < sizes->size(); ++i) {
            const ByteSpan r = sizes->row_at(i);
            const std::optional<TireSize> t = TireSize::read(r);
            if (t && t->width_mm == want.width_5 * 5 && t->aspect == want.aspect_5 * 5 && std::fabs(t->rim_inches - rim) < 0.01f) return sizes->ids()[i];
        }
        for (const auto& [id, b] : out.rows_["TIRESIZE"])
            if (b.size() >= 4 && b[0] == want.aspect_5 && b[2] == want.rim_10 && b[3] == want.width_5) return id;
        const std::string l = "ts_mod_" + std::to_string(want.width_5 * 5) + "_" + std::to_string(want.aspect_5 * 5) + "r" + std::to_string(want.rim_10);
        const u32 id = new_id(l, "TIRESIZE", 0);
        Bytes b(sizes->row_at(0).begin(), sizes->row_at(0).end());
        put8(b, 0, want.aspect_5), put8(b, 2, want.rim_10), put8(b, 3, want.width_5);
        out.rows_["TIRESIZE"].emplace_back(id, std::move(b));
        out.labels_["TIRESIZE"].emplace_back(l, id);
        return id;
    };
    float width_delta[2] = {0, 0}, diameter_delta[2] = {0, 0}; // mm
    for (int axle = 0; axle < 2; ++axle) {
        const std::optional<ModCarDef::TireSize>& want = axle == 0 ? d.front_tire : d.rear_tire;
        if (!want) continue;
        const std::optional<u32> id = size_id(*want);
        if (!id) return fail("[tires] the disc has no tire sizes");
        each(axle == 0 ? PartTable::FrontTire : PartTable::RearTire, [&](Clone& c) { put32(c.bytes, 0, *id); });
        if (donor_size[axle]) {
            const float w = want->width_5 * 5.0f, rim = want->rim_10 * 0.1f;
            width_delta[axle] = w - float(donor_size[axle]->width_mm);
            diameter_delta[axle] = (rim * 25.4f + 2 * w * want->aspect_5 * 5 * 0.01f) - donor_size[axle]->radius_m() * 2000.0f;
        }
    }
    // The car's own tires as sold: their compounds' grip (TIRECOMPOUND +0x01, x 100) scaled, as copies of its own.
    if (d.grip_pct && std::fabs(*d.grip_pct - 100) > 0.01f) {
        std::map<u32, u32> copies;
        u32 n = 0;
        for (Bytes* tire : {ft, rt})
            for (size_t k = 0; k < 3; ++k) {
                const u32 compound = get32(*tire, 8 + 8 * k);
                if (get32(*tire, 12 + 8 * k) != u32(PartTable::TireCompound)) continue;
                if (!copies.count(compound)) {
                    const ByteSpan c = disc_row("TIRECOMPOUND", compound);
                    if (c.size() < 2) continue;
                    Bytes b(c.begin(), c.end());
                    put8(b, 1, clamp8(c[1] * *d.grip_pct / 100.0f));
                    const u32 id = new_id(d.label, "TIRECOMPOUND", n);
                    out.labels_["TIRECOMPOUND"].emplace_back(d.label + "_" + label_of("TIRECOMPOUND", compound), id);
                    out.rows_["TIRECOMPOUND"].emplace_back(id, std::move(b));
                    copies[compound] = id, ++n;
                }
                put32(*tire, 8 + 8 * k, copies[compound]);
            }
    }

    // ---- the body (CHASSIS) ----
    const u32 d_len = get16(*ch, 0x00), d_height = get16(*ch, 0x02), d_wb = get16(*ch, 0x04), d_weight = get16(*ch, 0x06);
    const u32 d_front = get8(*ch, 0x1C);
    const u32 weight = d.weight_kg.value_or(u16(d_weight)), len = d.length_mm.value_or(u16(d_len));
    const u32 height = d.height_mm.value_or(u16(d_height)), wb = d.wheelbase_mm.value_or(u16(d_wb));
    const u32 front = d.front_weight_pct.value_or(u8(d_front));
    // The width: the body's box (VARIATION +0x14), the donor's paint row.
    float d_box[4] = {0, 0, 0, 0}, d_area = 0;
    u32 body_group = 0;
    if (const Table* cv = db.table("CAR_VARIATION_american"))
        if (const ByteSpan r = cv->row(*body_id); r.size() >= 4) body_group = rd32(r, 0);
    if (const Table* v = db.table("VARIATIONamerican")) {
        size_t first = 0;
        if (v->rows(body_group, first)) {
            const ByteSpan r = v->row_at(first);
            const Bytes b(r.begin(), r.end());
            for (int i = 0; i < 4; ++i) d_box[i] = getf(b, 0x10 + 4 * size_t(i));
            d_area = getf(b, 0x20);
        }
    }
    const float d_width = d_box[1];
    const float width = d.width_mm ? float(*d.width_mm) : d_width;
    each(PartTable::Chassis, [&](Clone& c) {
        Bytes& b = c.bytes;
        const u32 old_weight = get16(b, 0x06);
        put16(b, 0x00, len), put16(b, 0x02, height), put16(b, 0x04, wb), put16(b, 0x06, weight);
        if (get16(b, 0x08) == d_len) put16(b, 0x08, len); // (the length, height and weight again)
        if (get16(b, 0x0A) == d_height) put16(b, 0x0A, height);
        if (get16(b, 0x0C) == old_weight) put16(b, 0x0C, weight);
        put8(b, 0x1C, front);
        // The yaw inertia (x 100 kg m2) ~ m (L2 + W2), the donor's scaled; the center of gravity's height term by the height.
        if (d_weight && d_len) {
            const double was = double(d_len) * d_len + double(d_width) * d_width, now = double(len) * len + double(width) * width;
            put8(b, 0x1D, clamp8(get8(b, 0x1D) * double(weight) / d_weight * (was > 0 ? now / was : 1.0)));
        }
        if (d_height) put8(b, 0x1E, clamp8(get8(b, 0x1E) * double(height) / d_height));
        // The width across each axle's tires and how high a wheel's top reaches.
        const std::optional<u16> track[2] = {d.front_track_mm, d.rear_track_mm};
        for (int axle = 0; axle < 2; ++axle) {
            double across = double(get16(b, 0x12 + 2 * size_t(axle))) + width_delta[axle];
            if (track[axle] && rm) {
                const double tread = get16(*rm, 0x02 + 2 * size_t(axle)) - 2.0 * s8(get8(*rm, 0x13 + size_t(axle)));
                across += double(*track[axle]) - tread;
            }
            put16(b, 0x12 + 2 * size_t(axle), clamp16(across));
            put16(b, 0x16 + 2 * size_t(axle), clamp16(double(get16(b, 0x16 + 2 * size_t(axle))) + diameter_delta[axle]));
        }
    });
    // RACINGMODIFY: the treads (+0x02 / +0x04 mm; the track is the tread less twice the wheel offset at +0x13 / +0x14) and the
    // drag coefficient (+0x0C, x 100) of the stock row; the racing modification's moved and scaled with them.
    if (rm) {
        const u32 d_cd = get8(*rm, 0x0C);
        int tread_delta[2] = {0, 0};
        const std::optional<u16> track[2] = {d.front_track_mm, d.rear_track_mm};
        for (int axle = 0; axle < 2; ++axle)
            if (track[axle]) tread_delta[axle] = int(*track[axle]) + 2 * s8(get8(*rm, 0x13 + size_t(axle))) - int(get16(*rm, 0x02 + 2 * size_t(axle)));
        each(PartTable::RacingModify, [&](Clone& c) {
            for (int axle = 0; axle < 2; ++axle) put16(c.bytes, 0x02 + 2 * size_t(axle), clamp16(int(get16(c.bytes, 0x02 + 2 * size_t(axle))) + tread_delta[axle]));
            if (d.cd && d_cd) put8(c.bytes, 0x0C, clamp8(get8(c.bytes, 0x0C) * (*d.cd * 100.0f) / d_cd));
        });
    }

    // ---- the suspension and brakes, for the new weight ----
    if (d_weight && (weight != d_weight || front != d_front || d.ride_height_mm)) {
        const double rf = double(weight) * front / std::max(1.0, double(d_weight) * d_front);
        const double rr = double(weight) * (100 - front) / std::max(1.0, double(d_weight) * (100 - d_front));
        std::optional<int> ride;
        if (Bytes* su = stock_of(PartTable::Suspension); su && d.ride_height_mm) ride = int(*d.ride_height_mm) - int(get16(*su, 0x04));
        each(PartTable::Suspension, [&](Clone& c) {
            Bytes& b = c.bytes;
            if (d.scale_suspension) // the spring rates' least, greatest and default (0.1 kgf/mm), front then rear
                for (size_t o = 0; o < 3; ++o) put8(b, 0x34 + o, clamp8(get8(b, 0x34 + o) * rf)), put8(b, 0x37 + o, clamp8(get8(b, 0x37 + o) * rr));
            if (ride) // the ride heights' least, greatest and default, front then rear (mm)
                for (size_t o = 0; o < 6; ++o) put16(b, 2 * o, clamp16(int(get16(b, 2 * o)) + *ride));
        });
    }
    if (d.scale_brakes && d_weight && weight != d_weight)
        each(PartTable::Brake, [&](Clone& c) {
            for (size_t o : {3, 4, 5}) put8(c.bytes, o, clamp8(get8(c.bytes, o) * double(weight) / d_weight));
        });

    // ---- the engine (ENGINE: the curve +0x1E u16 x 24 in 0.01 kgf m at +0x55 u8 x 24 in 100 rpm, +0x54 its points) ----
    const std::optional<Engine> donor_engine = Engine::read(*en);
    std::vector<std::pair<float, float>> curve = d.curve; // rpm, N m
    if (curve.empty() && donor_engine && (d.power_kw || d.torque_nm)) {
        // The donor's curve scaled to the power (else the torque) asked for.
        const Engine::Peak p = donor_engine->peak();
        const float k = d.power_kw ? *d.power_kw / 0.73549875f / std::max(1.0f, p.ps) : *d.torque_nm / 9.80665f / std::max(0.1f, p.kgfm);
        const float out_k = float(donor_engine->output_pct ? donor_engine->output_pct : 100) * 0.01f;
        for (int i = 0; i < donor_engine->points; ++i)
            curve.push_back({float(donor_engine->rpm[size_t(i)]) * 100.0f, float(donor_engine->torque[size_t(i)]) * 0.01f * out_k * k * 9.80665f});
        if (!d.output_pct) put8(*en, 0x6F, 100); // (the factor is in the curve now)
    }
    if (!curve.empty()) {
        std::sort(curve.begin(), curve.end());
        int n = 0, last = -1;
        float peak_ps = 0, peak_kgfm = 0;
        int peak_ps_rpm = 0, peak_kgfm_rpm = 0;
        for (const auto& [rpm, nm] : curve) {
            const int a = int(std::lround(rpm / 100.0f));
            if (a <= last || a > 255 || n >= 24) continue;
            last = a;
            const float kgfm = nm / 9.80665f;
            put8(*en, 0x55 + size_t(n), u32(a));
            put16(*en, 0x1E + 2 * size_t(n), clamp16(kgfm * 100.0f));
            const float ps = kgfm * float(a) * 100.0f / 716.2f;
            if (ps > peak_ps) peak_ps = ps, peak_ps_rpm = a * 100;
            if (kgfm > peak_kgfm) peak_kgfm = kgfm, peak_kgfm_rpm = a * 100;
            ++n;
        }
        if (n < 2) return fail("[powertrain] the curve needs two points 100 rpm apart or more");
        for (int i = n; i < 24; ++i) put8(*en, 0x55 + size_t(i), 0), put16(*en, 0x1E + 2 * size_t(i), 0);
        put8(*en, 0x54, u32(n));
        if (!d.curve.empty() && !d.output_pct) put8(*en, 0x6F, 100);
        const float k = float(get8(*en, 0x6F) ? get8(*en, 0x6F) : 100) * 0.01f;
        // The catalog figures (+0x1A PS, +0x1C 0.01 kgf m) and their engine speeds' texts (+0x10, +0x14: UnistrDB).
        put16(*en, 0x1A, clamp16(d.rated_kw ? *d.rated_kw / 0.73549875f : peak_ps * k));
        put16(*en, 0x1C, clamp16((d.torque_nm ? *d.torque_nm / 9.80665f : peak_kgfm * k) * 100.0f));
        put32(*en, 0x10, pool_index("UnistrDB", std::to_string(peak_ps_rpm)));
        put32(*en, 0x14, pool_index("UnistrDB", std::to_string(peak_kgfm_rpm)));
        if (!d.max_rpm) { // the limits just past the curve's end, as the donor's were past its own
            const int over_rev = donor_engine ? int(donor_engine->rev_limit) - donor_engine->rpm[size_t(std::max(0, donor_engine->points - 1))] : 0;
            const int over_shift = donor_engine ? int(donor_engine->shift_limit) - donor_engine->rpm[size_t(std::max(0, donor_engine->points - 1))] : 0;
            if (!d.curve.empty()) put8(*en, 0x51, clamp8(last + std::max(0, over_rev))), put8(*en, 0x50, clamp8(last + over_shift));
        }
    }
    if (d.max_rpm) {
        const u32 lim = clamp8(*d.max_rpm / 100.0f);
        const int gap = int(get8(*en, 0x51)) - int(get8(*en, 0x50));
        put8(*en, 0x51, lim), put8(*en, 0x50, clamp8(int(lim) - std::max(0, gap)));
    }
    if (d.output_pct) put8(*en, 0x6F, *d.output_pct);
    if (d.idle_rpm) put8(*en, 0x52, clamp8(*d.idle_rpm / 10.0f));
    if (d.sound_id) put16(*en, 0x18, *d.sound_id);
    // The texts (+0x00 displacement, +0x04 layout, +0x0C aspiration: UnistrDB).
    if (!d.displacement_text.empty()) put32(*en, 0x00, pool_index("UnistrDB", d.displacement_text));
    if (!d.layout_text.empty()) put32(*en, 0x04, pool_index("UnistrDB", d.layout_text));
    if (!d.aspiration_text.empty()) put32(*en, 0x0C, pool_index("UnistrDB", d.aspiration_text));

    // ---- the drivetrain (+0x10 the layout, +0x11 the four wheel drive system, +0x12 .. +0x14 the front share) ----
    if (d.drive) {
        const u32 was = get8(*dt, 0x10);
        put8(*dt, 0x10, *d.drive);
        if (*d.drive == 2 && was != 2) {
            // A four wheel drive system the disc's four wheel drive cars have most (0xFE: none on two wheel drive).
            std::map<u32, int> systems;
            if (const Table* t = db.table("DRIVETRAIN"))
                for (size_t i = 0; i < t->size(); ++i)
                    if (const ByteSpan r = t->row_at(i); r.size() >= 0x15 && r[0x10] == 2) ++systems[r[0x11]];
            u32 best = 0;
            int most = -1;
            for (const auto& [s, c] : systems)
                if (c > most) best = s, most = c;
            put8(*dt, 0x11, best);
            if (!d.front_torque_pct) put8(*dt, 0x12, 50), put8(*dt, 0x13, 50), put8(*dt, 0x14, 50);
        } else if (*d.drive != 2) {
            put8(*dt, 0x11, 0xFE);
        }
    }
    if (d.front_torque_pct && get8(*dt, 0x10) == 2) {
        const u32 f = std::clamp<u32>(*d.front_torque_pct, 1, 99);
        put8(*dt, 0x12, std::min(get8(*dt, 0x12), f)), put8(*dt, 0x13, std::max(get8(*dt, 0x13), f)), put8(*dt, 0x14, f);
    }

    // ---- the gearbox (GEAR: x 1000; +0x24 the gears, +0x16 reverse, +0x18 .. +0x1C the final drive's least, greatest, default) ----
    if (!d.gears.empty()) {
        for (size_t i = 0; i < 11; ++i) put16(*ge, 2 * i, i < d.gears.size() ? clamp16(d.gears[i] * 1000.0f) : 0);
        put8(*ge, 0x24, u32(d.gears.size()));
        put8(*ge, 0x23, 0), put8(*ge, 0x25, 0); // a box of steps, its ratios as written
    }
    if (d.reverse) put16(*ge, 0x16, clamp16(*d.reverse * 1000.0f));
    if (d.final_drive) {
        const u32 f = clamp16(*d.final_drive * 1000.0f);
        put16(*ge, 0x18, std::min(get16(*ge, 0x18), f)), put16(*ge, 0x1A, std::max(get16(*ge, 0x1A), f)), put16(*ge, 0x1C, f);
    }

    // ---- [raw.*]: on the car's own rows of the table (its stock row first) ----
    for (const ModCarDef::Poke& p : d.pokes) {
        const u32 t = *table_id_of(p.table);
        for (Clone& c : clones) {
            if (c.table != t || !c.stock) continue;
            Bytes& b = c.bytes;
            const size_t need = p.type == 'b' ? 1 : p.type == 'h' ? 2 : 4;
            if (p.offset + need > b.size()) return fail("[raw." + p.table + "] " + std::to_string(p.offset) + " is past the row's " + std::to_string(b.size()) + " bytes");
            switch (p.type) {
            case 'b': put8(b, p.offset, clamp8(p.value)); break;
            case 'h': put16(b, p.offset, clamp16(p.value)); break;
            case 'i': put32(b, p.offset, u32(std::clamp(p.value, 0.0, 4294967295.0))); break;
            default: putf(b, p.offset, float(p.value)); break;
            }
        }
    }

    // ---- the rows ----
    for (Clone& c : clones) out.rows_[part_table_name(c.table)].emplace_back(c.id, std::move(c.bytes));
    std::vector<u32> set_ids;
    for (size_t i = 0; i < sets.size(); ++i) {
        const u32 id = new_id(d.label, "DEFAULT_PARTS", u32(i));
        set_ids.push_back(id);
        out.labels_["DEFAULT_PARTS"].emplace_back(relabel(sets[i].label, d, "DEFAULT_PARTS", u32(i)), id);
        out.rows_["DEFAULT_PARTS"].emplace_back(id, std::move(set_rows[i]));
    }
    auto set_of = [&](u32 donor_set) -> u32 {
        for (size_t i = 0; i < sets.size(); ++i)
            if (sets[i].from == donor_set) return set_ids[i];
        return donor_set;
    };

    // GENERIC_CAR (+0x00 its set, +0x08 price, +0x0C year, +0x0E country, +0x0F maker, +0x10 brand (the tuner table's), +0x11 category,
// +0x12 flags: bit 0 not for sale).
    const u32 car_id = new_id(d.label, "GENERIC_CAR", 0);
    Bytes car(donor_row.begin(), donor_row.end());
    put32(car, 0, set_ids[0]);
    if (d.price) put32(car, 0x08, *d.price);
    if (d.year) put16(car, 0x0C, *d.year);
    if (d.category) put8(car, 0x11, (get8(car, 0x11) / 100) * 100 + *d.category);
    put8(car, 0x12, d.for_sale ? get8(car, 0x12) & ~1u : get8(car, 0x12) | 1u);
    if (!d.maker.empty()) {
        std::optional<int> number;
        std::string want = to_lower(d.maker);
        want.erase(std::remove_if(want.begin(), want.end(), [](char c) { return c == ' ' || c == '-' || c == '_' || c == '.'; }), want.end());
        for (const auto& [n, name] : makers) {
            std::string have = to_lower(name);
            have.erase(std::remove_if(have.begin(), have.end(), [](char c) { return c == ' ' || c == '-' || c == '_' || c == '.'; }), have.end());
            if (have == want) number = n;
        }
        if (number) {
            put8(car, 0x0F, u32(*number));
            // The country of the maker's cars, and the brand (+0x10, the tuner: the dealers' and the pages' maker name) most of
            // them have (Nissan's own, not Nismo's).
            std::map<u32, int> brands;
            bool country = false;
            for (size_t i = 0; i < cars->size(); ++i)
                if (const ByteSpan r = cars->row_at(i); r.size() >= 0x18 && r[0x0F] == u32(*number)) {
                    if (!country) put8(car, 0x0E, r[0x0E]), country = true;
                    ++brands[r[0x10]];
                }
            u32 brand = get8(car, 0x10);
            int most = 0;
            for (const auto& [b, n] : brands)
                if (n > most) brand = b, most = n;
            put8(car, 0x10, brand);
        } else if (!makers.empty()) {
            st.warnings.push_back("[car] maker '" + d.maker + "' is not a maker of this game: the donor's is kept");
        }
    }
    out.rows_["GENERIC_CAR"].emplace_back(car_id, std::move(car));
    out.labels_["GENERIC_CAR"].emplace_back(d.label, car_id);

    // The parts that fit it (CARS/<code>: u64 count, then {u64 part, 8 bytes}).
    {
        Bytes f(8 + 16 * fits.size(), 0);
        const u64 n = fits.size();
        std::memcpy(f.data(), &n, 8);
        for (size_t i = 0; i < fits.size(); ++i) std::memcpy(f.data() + 8 + 16 * i, &fits[i].code, 8), std::memcpy(f.data() + 16 + 16 * i, fits[i].rest, 8);
        out.car_files_[u64(car_id)] = std::move(f);
    }

    // The names (CAR_NAME_<language>: four string numbers of <language>_StrDB, +0x00 the name, +0x08 the short one) and the
    // paints (CAR_VARIATION_<language>: {u32 the car's rows of VARIATION<language>, u32 42}).
    const u32 group = new_id(d.label, "VARIATION", 0);
    const std::string group_label = label_of("VARIATIONamerican", body_group);
    for (const std::string& t : db.table_names()) {
        if (t.rfind("CAR_NAME_", 0) == 0) {
            const std::string lang = t.substr(9);
            const u32 i = pool_index(lang + "_StrDB", d.name);
            Bytes b(16, 0);
            for (size_t k = 0; k < 4; ++k) put32(b, 4 * k, i);
            out.rows_[t].emplace_back(car_id, std::move(b));
            out.labels_[t].emplace_back(d.label, car_id);
        } else if (t.rfind("CAR_VARIATION_", 0) == 0) {
            Bytes b(8, 0);
            put32(b, 0, group), put32(b, 4, kVariation);
            out.rows_[t].emplace_back(car_id, std::move(b));
            out.labels_[t].emplace_back(d.label, car_id);
        } else if (t.rfind("VARIATION", 0) == 0 && !group_label.empty()) {
            out.labels_[t].emplace_back(group_label, group); // (the same label: the model's files are named by it)
        }
    }
    ModCars::PaintGroup pg;
    pg.from = body_group, pg.to = group;
    const float scale_len = d_len ? float(len) / float(d_len) : 1;
    pg.scale_box[0] = d_height ? float(height) / float(d_height) : 1;
    pg.scale_box[1] = d_width > 0 ? width / d_width : 1;
    pg.scale_box[2] = pg.scale_box[3] = scale_len;
    pg.frontal_area = d.frontal_area_m2;
    if (!pg.frontal_area && d_area > 0) pg.frontal_area = d_area * pg.scale_box[0] * pg.scale_box[1];
    out.paint_groups_.push_back(pg);

    // ARCADEINFO_NORMAL (by the car's label: its Arcade events, rows of RACE, and its dirt and snow versions at entries 6 and 7,
    // ENEMY_CARS rows {car, its set, its settings}) and TUNER_LIST (the tuners that tune it), the donor's under the car's label.
    if (const std::optional<u32> a = id_of("ARCADEINFO_NORMAL", d.donor)) {
        const ByteSpan ar = disc_row("ARCADEINFO_NORMAL", *a);
        Bytes row(ar.begin(), ar.end());
        for (size_t e = 6; e < 8 && 8 * e + 8 <= row.size(); ++e) {
            const u32 enemy = get32(row, 8 * e);
            if (enemy == 0xFFFFFFFFu || get32(row, 8 * e + 4) != kEnemyCars) continue;
            const ByteSpan er = disc_row("ENEMY_CARS", enemy);
            if (er.size() < 0x18) continue;
            Bytes b(er.begin(), er.end());
            put32(b, 0, car_id);
            put32(b, 8, set_of(get32(b, 8)));
            const u32 id = new_id(d.label, "ENEMY_CARS", u32(e));
            out.labels_["ENEMY_CARS"].emplace_back(relabel(label_of("ENEMY_CARS", enemy), d, "ENEMY_CARS", u32(e)), id);
            out.rows_["ENEMY_CARS"].emplace_back(id, std::move(b));
            put32(row, 8 * e, id);
        }
        const u32 id = new_id(d.label, "ARCADEINFO_NORMAL", 0);
        out.labels_["ARCADEINFO_NORMAL"].emplace_back(d.label, id);
        out.rows_["ARCADEINFO_NORMAL"].emplace_back(id, std::move(row));
    }
    // [opponent] races: an ENEMY_CARS row of the car as sold (no settings of its own, as the pools' cars), added to the end of
    // each race's pool (RACES/<35, row>: u32 count, u32 0, then {u32 row, u32 table 38, s64 paint + 1, -1 the race's choice}).
    if (!d.opponent_races.empty()) {
        const Labels* races = db.labels("RACE");
        if (!races) return fail("[opponent] races: the disc's races are not read");
        const u32 enemy = new_id(d.label, "ENEMY_CARS", 8);
        Bytes row(24, 0xFF);
        put32(row, 0, car_id), put32(row, 4, 0), put32(row, 8, set_ids[0]), put32(row, 12, kDefaultParts);
        out.rows_["ENEMY_CARS"].emplace_back(enemy, std::move(row));
        out.labels_["ENEMY_CARS"].emplace_back("gt4mod_" + d.label, enemy);
        size_t joined = 0;
        for (const auto& [label, race] : races->entries) {
            const bool match = std::any_of(d.opponent_races.begin(), d.opponent_races.end(), [&](const std::string& w) {
                return !w.empty() && w.back() == '*' ? label.compare(0, w.size() - 1, w, 0, w.size() - 1) == 0 : label == w;
            });
            if (!match) continue;
            Bytes f;
            if (const auto it = out.race_files_.find(race); it != out.race_files_.end()) f = it->second;
            else if (const std::optional<Bytes> file = db.race_file(race)) f = *file;
            if (f.size() < 8) continue; // (a race without a pool: a license test's, a mission's)
            const u32 n = get32(f, 0);
            f.resize(8 + 16 * size_t(n));
            Bytes entry(16, 0xFF);
            put32(entry, 0, enemy), put32(entry, 4, kEnemyCars);
            f.insert(f.end(), entry.begin(), entry.end());
            put32(f, 0, n + 1);
            out.race_files_[race] = std::move(f);
            ++joined;
        }
        if (!joined) st.warnings.push_back("[opponent] races: no race of this disc with a pool of opponents matches");
    }
    if (const std::optional<u32> t = id_of("TUNER_LIST", d.donor)) {
        const ByteSpan r = disc_row("TUNER_LIST", *t);
        const u32 id = new_id(d.label, "TUNER_LIST", 0);
        out.labels_["TUNER_LIST"].emplace_back(d.label, id);
        out.rows_["TUNER_LIST"].emplace_back(id, Bytes(r.begin(), r.end()));
    }
    return true;
}

// The cars of a list's files, read; `before`'s files and loaded labels passed over.
ModCars ModCars::build_list(SpecDb& db, std::string_view list, const std::map<int, std::string>& makers, const ModCars* before) {
    std::vector<ModCarDef> defs;
    std::vector<ModCarStatus> bad;
    std::map<std::string, std::vector<std::string>> warnings; // by file
    size_t at = 0;
    while (at < list.size()) {
        size_t end = list.find('\n', at);
        if (end == std::string_view::npos) end = list.size();
        const std::string_view line = list.substr(at, end - at);
        at = end + 1;
        const size_t tab = line.find('\t');
        if (tab == std::string_view::npos) continue;
        const std::string mod(line.substr(0, tab)), file(line.substr(tab + 1));
        if (file.find("/cars/") == std::string::npos) continue; // (a maker's or an event's)
        if (before && std::any_of(before->statuses().begin(), before->statuses().end(), [&](const ModCarStatus& s) { return s.file == file; })) continue;
        ModCarStatus st;
        st.mod = mod, st.file = file;
        const std::optional<Bytes> text = gt3r::read_host_file(file);
        std::optional<ModCarDef> d;
        if (!text) st.error = "the file is not read";
        else d = parse_mod_car(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()), &st.error, &warnings[file]);
        if (!d) {
            const size_t slash = file.find_last_of('/') + 1, dot = file.rfind(".ini");
            st.label = file.substr(slash, dot == std::string::npos || dot < slash ? std::string::npos : dot - slash);
            st.warnings = warnings[file];
            bad.push_back(std::move(st));
            continue;
        }
        d->mod = mod, d->file = file;
        if (before && before->def(d->label)) {
            st.label = d->label, st.name = d->name, st.donor = d->donor;
            st.error = "a car of that label is in the game already (the menus read the mods' cars again when they start)";
            bad.push_back(std::move(st));
            continue;
        }
        defs.push_back(std::move(*d));
    }
    ModCars m = build_defs(db, std::move(defs), makers, before);
    for (ModCarStatus& s : m.statuses_) {
        const std::vector<std::string>& w = warnings[s.file];
        s.warnings.insert(s.warnings.begin(), w.begin(), w.end());
    }
    for (ModCarStatus& b : bad) m.statuses_.push_back(std::move(b));
    return m;
}

ModCars ModCars::build(SpecDb& db, std::string_view list, const std::map<int, std::string>& makers) { return build_list(db, list, makers, nullptr); }

ModCars ModCars::build_more(SpecDb& db, std::string_view list, const std::map<int, std::string>& makers, const ModCars& before) {
    return build_list(db, list, makers, &before);
}

ModCars ModCars::build(SpecDb& db, std::vector<ModCarDef> defs, const std::map<int, std::string>& makers) { return build_defs(db, std::move(defs), makers, nullptr); }

void ModCars::merge(ModCars&& more) {
    statuses_.insert(statuses_.end(), more.statuses_.begin(), more.statuses_.end());
    for (auto& [l, d] : more.defs_) defs_[l] = std::move(d);
    for (auto& [t, rows] : more.rows_) rows_[t].insert(rows_[t].end(), rows.begin(), rows.end());
    for (auto& [t, labels] : more.labels_) labels_[t].insert(labels_[t].end(), labels.begin(), labels.end());
    for (auto& [pool, strings] : more.strings_) {
        const auto size = more.pool_sizes_.find(pool);
        if (size == more.pool_sizes_.end()) continue;
        const auto mine = pool_sizes_.find(pool);
        if (mine == pool_sizes_.end()) {
            pool_sizes_[pool] = size->second, strings_[pool] = strings;
            continue;
        }
        // (`more` read the pool with these strings in it: its own start after them)
        std::vector<std::string>& have = strings_[pool];
        if (size->second == mine->second + have.size()) have.insert(have.end(), strings.begin(), strings.end());
    }
    for (const auto& [pool, size] : more.pool_sizes_) pool_sizes_.emplace(pool, size);
    paint_groups_.insert(paint_groups_.end(), more.paint_groups_.begin(), more.paint_groups_.end());
    names_.insert(names_.end(), more.names_.begin(), more.names_.end());
    for (auto& [code, f] : more.car_files_) car_files_[code] = std::move(f);
    for (auto& [race, f] : more.race_files_) race_files_[race] = std::move(f); // (made on these)
}


ModCars ModCars::build_defs(SpecDb& db, std::vector<ModCarDef> defs, const std::map<int, std::string>& makers, const ModCars* before) {
    ModCars out;
    ModCarBuilder b{db, out, makers, {}};
    if (before) // (the ids its cars took)
        for (const auto& [table, rows] : before->rows_)
            for (const auto& [id, bytes] : rows) b.used[table].insert(id);
    // A later file's car of a label replaces an earlier one's.
    std::map<std::string, size_t> last;
    for (size_t i = 0; i < defs.size(); ++i) last[defs[i].label] = i;
    for (size_t i = 0; i < defs.size(); ++i) {
        const ModCarDef& d = defs[i];
        ModCarStatus st;
        st.mod = d.mod, st.file = d.file, st.label = d.label, st.name = d.name, st.donor = d.donor;
        if (last[d.label] != i) {
            st.error = "replaced by " + defs[last[d.label]].file;
            out.statuses_.push_back(std::move(st));
            continue;
        }
        // A car that fails leaves nothing behind.
        ModCars before = out;
        std::map<std::string, std::set<u32>> used = b.used;
        if (b.add(d, st)) out.defs_[d.label] = d;
        else {
            std::vector<ModCarStatus> statuses = std::move(out.statuses_);
            out = std::move(before), b.used = std::move(used);
            out.statuses_ = std::move(statuses);
        }
        out.statuses_.push_back(std::move(st));
    }
    return out;
}

const ModCarDef* ModCars::def(const std::string& label) const {
    const auto it = defs_.find(label);
    return it == defs_.end() ? nullptr : &it->second;
}

std::vector<std::string> ModCars::labels() const {
    std::vector<std::string> out;
    for (const ModCarStatus& s : statuses_)
        if (s.loaded()) out.push_back(s.label);
    return out;
}

void ModCars::extend(const std::string& table, Table& t) const {
    std::vector<std::pair<u32, Bytes>> add;
    if (const auto it = rows_.find(table); it != rows_.end()) add = it->second;
    if (table.rfind("VARIATION", 0) == 0)
        for (const PaintGroup& g : paint_groups_) {
            size_t first = 0;
            const size_t n = t.rows(g.from, first);
            for (size_t i = 0; i < n; ++i) {
                const ByteSpan r = t.row_at(first + i);
                Bytes b(r.begin(), r.end());
                for (int k = 0; k < 4; ++k) putf(b, 0x10 + 4 * size_t(k), std::round(getf(b, 0x10 + 4 * size_t(k)) * g.scale_box[k]));
                if (g.frontal_area) putf(b, 0x20, *g.frontal_area);
                add.emplace_back(g.to, std::move(b));
            }
        }
    if (!add.empty()) t.add_rows(std::move(add));
}

void ModCars::extend(const std::string& table, Labels& l) const {
    if (const auto it = labels_.find(table); it != labels_.end()) l.entries.insert(l.entries.end(), it->second.begin(), it->second.end());
}

void ModCars::extend(const std::string& pool, Strings& s) const {
    const auto it = strings_.find(pool);
    const auto size = pool_sizes_.find(pool);
    if (it == strings_.end() || size == pool_sizes_.end() || s.strings.size() != size->second) return;
    s.strings.insert(s.strings.end(), it->second.begin(), it->second.end());
}

std::optional<ModCars::Picture> ModCars::picture(std::string_view path) const {
    // "piece/<folder>/<label>.img" of the disc, "image/piece/<folder>/<label>.png" of a page's pack.
    while (!path.empty() && path[0] == '/') path.remove_prefix(1);
    const bool pack = path.rfind("image/", 0) == 0;
    if (pack) path.remove_prefix(6);
    const std::string_view ext = pack ? ".png" : ".img";
    if (path.rfind("piece/", 0) != 0 || path.size() < 10 || path.substr(path.size() - 4) != ext) return std::nullopt;
    const size_t slash = path.find('/', 6);
    if (slash == std::string_view::npos) return std::nullopt;
    const std::string folder(path.substr(6, slash - 6)), label(path.substr(slash + 1, path.size() - 4 - slash - 1));
    const ModCarDef* d = def(label);
    if (!d) return std::nullopt;
    // The mod's files are named from its folder (the one cars/ is in).
    std::string root = d->file.substr(0, d->file.find_last_of('/') + 1);
    if (root.size() >= 6 && root.compare(root.size() - 6, 6, "/cars/") == 0) root.resize(root.size() - 5);
    auto own = [&](const std::string& p) { return p.empty() ? std::string() : p[0] == '/' ? p : root + p; };
    const std::string body = d->body.empty() ? d->donor : d->body;
    const std::string stand_in = (pack ? "image/piece/" : "piece/") + folder + "/" + body + std::string(ext);
    Picture out;
    if (folder == "car_mini") out.disc = stand_in, out.png = own(d->thumb);
    else if (folder == "car_pict") out.disc = stand_in, out.png = own(d->picture);
    else if (folder == "car_logo" || folder == "car_logo2") out.png = own(d->logo);
    else return std::nullopt;
    return out;
}

std::optional<Bytes> ModCars::race_file(u32 race_row) const {
    const auto it = race_files_.find(race_row);
    if (it == race_files_.end()) return std::nullopt;
    return it->second;
}

std::optional<Bytes> ModCars::car_file(u64 code) const {
    const auto it = car_files_.find(code);
    if (it == car_files_.end()) return std::nullopt;
    return it->second;
}

} // namespace gt4r::db
