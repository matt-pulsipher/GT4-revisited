#include "gt4r/formats/db/car_export.h"

#include "gt4r/formats/db/car_parts.h"
#include "gt4r/formats/db/mod_cars.h"

#include "base/file.h"

#include <cctype>
#include <cstring>
#include <deque>
#include <filesystem>
#include <set>

namespace gt4r::db {

namespace {

constexpr char kMagic[8] = {'G', 'T', '4', 'R', 'R', 'E', 'C', '1'};

void put32(Bytes& b, u32 v) {
    for (int i = 0; i < 4; ++i) b.push_back(u8(v >> (8 * i)));
}
void put_text(Bytes& b, const std::string& s) {
    put32(b, u32(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}
struct Reader {
    ByteSpan d;
    size_t at = 0;
    bool ok = true;
    u32 u() {
        if (at + 4 > d.size()) return ok = false, 0;
        const u32 v = rd32(d, at);
        at += 4;
        return v;
    }
    Bytes bytes() {
        const u32 n = u();
        if (!ok || n > d.size() - at) return ok = false, Bytes();
        Bytes out(d.begin() + std::ptrdiff_t(at), d.begin() + std::ptrdiff_t(at + n));
        at += n;
        return out;
    }
    std::string text() {
        const Bytes b = bytes();
        return std::string(b.begin(), b.end());
    }
};

// A table's name by the id the rows name it with: the parts 0 .. 33, then the event tables.
std::string table_name(u32 t) {
    if (const char* n = part_table_name(t)) return n;
    switch (t) {
    case 35: return "RACE";
    case 36: return "DEFAULT_PARTS";
    case 37: return "DEFAULT_PARAM";
    case 38: return "ENEMY_CARS";
    default: return {};
    }
}

} // namespace

Bytes CarRecords::serialize() const {
    Bytes b(kMagic, kMagic + 8);
    put_text(b, car), put32(b, car_id);
    put32(b, u32(rows.size()));
    for (const Row& r : rows) put_text(b, r.table), put_text(b, r.label), put32(b, r.id), put32(b, u32(r.bytes.size())), b.insert(b.end(), r.bytes.begin(), r.bytes.end());
    put32(b, u32(texts.size()));
    for (const Text& t : texts) put_text(b, t.pool), put32(b, t.index), put_text(b, t.text);
    put32(b, u32(car_file.size())), b.insert(b.end(), car_file.begin(), car_file.end());
    return b;
}

std::optional<CarRecords> CarRecords::parse(ByteSpan data, std::string* error) {
    if (data.size() < 8 || std::memcmp(data.data(), kMagic, 8) != 0) {
        if (error) *error = "not a records file";
        return std::nullopt;
    }
    Reader r{data, 8};
    CarRecords c;
    c.car = r.text(), c.car_id = r.u();
    const u32 rows = r.u();
    for (u32 i = 0; r.ok && i < rows && i < 100000; ++i) {
        Row row;
        row.table = r.text(), row.label = r.text(), row.id = r.u(), row.bytes = r.bytes();
        c.rows.push_back(std::move(row));
    }
    const u32 texts = r.u();
    for (u32 i = 0; r.ok && i < texts && i < 100000; ++i) {
        Text t;
        t.pool = r.text(), t.index = r.u(), t.text = r.text();
        c.texts.push_back(std::move(t));
    }
    c.car_file = r.bytes();
    if (!r.ok || c.rows.empty()) {
        if (error) *error = "the records file is cut short";
        return std::nullopt;
    }
    return c;
}

const std::string* CarRecords::text(const std::string& pool, u32 index) const {
    for (const Text& t : texts)
        if (t.pool == pool && t.index == index) return &t.text;
    return nullptr;
}

std::optional<CarRecords> collect_car_records(SpecDb& db, const std::string& label, std::string* error) {
    auto fail = [&](const std::string& m) -> std::optional<CarRecords> {
        if (error) *error = m;
        return std::nullopt;
    };
    if (db.mod_cars().def(label)) return fail("a mod car is not exported (its mod is shared as it is)");
    const Labels* cars = db.labels("GENERIC_CAR");
    const std::optional<u32> car_id = cars ? cars->id(label) : std::nullopt;
    if (!car_id) return fail(label + " is not a car of this disc");
    CarRecords out;
    out.car = label, out.car_id = *car_id;
    std::set<std::pair<std::string, u32>> seen;
    std::set<std::pair<std::string, u32>> texts;
    std::deque<std::pair<std::string, u32>> todo;
    auto add = [&](const std::string& table, u32 id) {
        if (id == 0xFFFFFFFFu || table.empty() || !seen.insert({table, id}).second) return;
        todo.push_back({table, id});
    };
    auto text = [&](const std::string& pool, u32 index) {
        if (!texts.insert({pool, index}).second) return;
        if (const Strings* s = db.strings(pool); s && index < s->strings.size()) out.texts.push_back({pool, index, s->strings[index]});
    };
    add("GENERIC_CAR", *car_id);
    for (const std::string& t : db.table_names())
        if (t.rfind("CAR_NAME_", 0) == 0 || t.rfind("CAR_VARIATION_", 0) == 0) add(t, *car_id);
    for (const char* t : {"ARCADEINFO_NORMAL", "TUNER_LIST"})
        if (const Labels* l = db.labels(t))
            if (const std::optional<u32> id = l->id(label)) add(t, *id);
    if (const std::optional<Bytes> f = db.car_file(u64(*car_id)); f && f->size() >= 8) {
        out.car_file = *f;
        u64 n = 0;
        std::memcpy(&n, f->data(), 8);
        for (u64 i = 0; i < n && 8 + (i + 1) * 16 <= f->size(); ++i) add(table_name(rd32(ByteSpan(*f), 12 + 16 * i)), rd32(ByteSpan(*f), 8 + 16 * i));
    }
    while (!todo.empty()) {
        const auto [table, id] = todo.front();
        todo.pop_front();
        const Table* t = db.table(table);
        const Labels* l = db.labels(table);
        const std::string* name = l ? l->label(id) : nullptr;
        if (table == "RACE") { // (named only: the disc's own by its label)
            if (name) out.rows.push_back({table, *name, id, {}});
            continue;
        }
        size_t first = 0;
        const size_t n = t ? t->rows(id, first) : 0;
        for (size_t k = 0; k < n; ++k) {
            const ByteSpan b = t->row_at(first + k);
            out.rows.push_back({table, name ? *name : std::string(), id, Bytes(b.begin(), b.end())});
            // What the row names.
            if (table == "GENERIC_CAR" && b.size() >= 4) add("DEFAULT_PARTS", rd32(b, 0));
            else if ((table == "DEFAULT_PARTS") && b.size() >= 26 * 8) {
                for (size_t p = 0; p < 26; ++p) add(table_name(rd32(b, 8 * p + 4)), rd32(b, 8 * p));
            } else if ((table == "FRONTTIRE" || table == "REARTIRE") && b.size() >= 0x38) {
                add("TIRESIZE", rd32(b, 0));
                for (size_t p = 0; p < 3; ++p) add(table_name(rd32(b, 12 + 8 * p)), rd32(b, 8 + 8 * p)), add(table_name(rd32(b, 0x24 + 8 * p)), rd32(b, 0x20 + 8 * p));
            } else if (table == "ENGINE" && b.size() >= 0x18) {
                for (size_t p = 0; p < 6; ++p) text("UnistrDB", rd32(b, 4 * p));
            } else if (table == "ENEMY_CARS" && b.size() >= 0x18) {
                add("DEFAULT_PARTS", rd32(b, 8));
                if (rd32(b, 0x14) == 37) add("DEFAULT_PARAM", rd32(b, 0x10));
            } else if (table == "ARCADEINFO_NORMAL" && b.size() >= 0x40) {
                for (size_t e = 0; e < 8; ++e) add(table_name(rd32(b, 8 * e + 4)), rd32(b, 8 * e));
            } else if (table.rfind("CAR_NAME_", 0) == 0 && b.size() >= 16) {
                for (size_t p = 0; p < 4; ++p) text(table.substr(9) + "_StrDB", rd32(b, 4 * p));
            } else if (table.rfind("CAR_VARIATION_", 0) == 0 && b.size() >= 4) {
                add("VARIATION" + table.substr(14), rd32(b, 0));
            } else if (table.rfind("VARIATION", 0) == 0 && b.size() >= 0x10) {
                text(table.substr(9) + "_StrDB", rd32(b, 0x0C));
            }
        }
    }
    return out;
}

std::string export_suffix(std::string_view disc_key) {
    std::string out;
    for (char c : disc_key)
        if (std::isalnum(static_cast<unsigned char>(c))) out.push_back(char(std::tolower(static_cast<unsigned char>(c))));
    return out.empty() ? std::string("disc") : out;
}

std::string export_mod_id(std::string_view disc_key) { return "cars_" + export_suffix(disc_key); }

std::optional<CarExport> export_car(SpecDb& db, const std::function<std::optional<Bytes>(const std::string&)>& read, const std::string& label,
                                    const std::string& mod_dir, std::string_view disc_key, const std::string& source, std::string* error) {
    auto fail = [&](const std::string& m) -> std::optional<CarExport> {
        if (error) *error = m;
        return std::nullopt;
    };
    const std::optional<CarRecords> rec = collect_car_records(db, label, error);
    if (!rec) return std::nullopt;
    const std::string suffix = export_suffix(disc_key), own = label + "_" + suffix;
    CarExport out;
    out.label = own;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(fs::path(mod_dir) / "cars", ec);
    auto write = [&](const std::string& rel, ByteSpan data) {
        const fs::path p = fs::path(mod_dir) / rel;
        fs::create_directories(p.parent_path(), ec);
        if (!gt3r::write_host_file(p.string(), data)) return false;
        out.files.push_back(rel);
        return true;
    };
    if (!fs::exists(fs::path(mod_dir) / "mod.ini")) {
        const std::string ini = "[mod]\nname = Cars from " + source + "\nversion = 1\nauthor = exported from " + source + "\n";
        if (!write("mod.ini", ByteSpan(reinterpret_cast<const u8*>(ini.data()), ini.size()))) return fail("mod.ini is not written");
    }
    // Its models and pictures under names of their own.
    std::string model;
    for (const db::CarEntry& e : db.cars())
        if (e.label == label) model = e.model;
    if (model.empty()) return fail(label + " has no model");
    for (const std::string& dir : {"car/lod/", "car/menu/"})
        for (const std::string& ext : {"", ".pat"})
            if (const std::optional<Bytes> f = read(dir + model + ext))
                if (!write("files/" + dir + model + "_" + suffix + ext, *f)) return fail("its model is not written");
    for (const std::string& dir : {"car_mini", "car_pict", "car_logo", "car_logo2"})
        if (const std::optional<Bytes> f = read("piece/" + dir + "/" + label + ".img"))
            if (!write("files/piece/" + dir + "/" + own + ".img", *f)) return fail("its pictures are not written");
    const Bytes records = rec->serialize();
    if (!write("cars/" + own + ".records", records)) return fail("its records are not written");
    // The car file: what the menus show of it, as the disc has it (edit them to taste).
    std::string name = own, maker;
    for (const db::CarEntry& e : db.cars())
        if (e.label == label && !e.name.empty()) name = e.name;
    u32 price = 0, year = 0, category = 0;
    bool for_sale = true;
    for (const CarRecords::Row& r : rec->rows)
        if (r.table == "GENERIC_CAR" && r.bytes.size() >= 0x18) {
            price = rd32(ByteSpan(r.bytes), 8), year = u32(r.bytes[0x0C] | r.bytes[0x0D] << 8), category = r.bytes[0x11] % 100u;
            for_sale = !(r.bytes[0x12] & 1);
            if (const auto m = db.makers().find(r.bytes[0x0F]); m != db.makers().end()) maker = m->second;
        }
    for (char& c : name)
        if (c == '\n' || c == '\r') c = ' ';
    static const char* const kCategory[] = {"normal", "racing", "tuning", "concept"};
    std::string ini = "; " + name + " from " + source + ", exported whole by the Car database: a disc that has " + label +
                      " builds it from that car, another takes its rows from the records file.\n[car]\nlabel = " + own + "\nname = " + name +
                      "\n";
    if (!maker.empty()) ini += "maker = " + maker + "\n";
    if (year) ini += "year = " + std::to_string(year) + "\n";
    if (price) ini += "price = " + std::to_string(price) + "\n";
    ini += std::string("category = ") + kCategory[category < 4 ? category : 0] + "\ndonor = " + label + "\nrecords = " + own + ".records\nmodel_name = " +
           model + "_" + suffix + "\n[dealer]\nfor_sale = " + (for_sale ? "yes" : "no") + "\n";
    if (!write("cars/" + own + ".ini", ByteSpan(reinterpret_cast<const u8*>(ini.data()), ini.size()))) return fail("its car file is not written");
    return out;
}

} // namespace gt4r::db
