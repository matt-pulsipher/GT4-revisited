#include "gt4r/formats/db/specdb.h"

#include "gt4r/formats/db/mod_cars.h"
#include "gt4r/formats/db/specdb_prologue.h"

#include "base/log.h"

#include <algorithm>
#include <cstring>

namespace gt4r::db {

namespace {

// The packed rows' bit stream: bits from the low end of each byte.
struct Bits {
    ByteSpan d;
    size_t pos; // in bits
    u32 peek(u32 n) const {
        u32 v = 0;
        for (u32 i = 0; i < n; ++i) {
            const size_t p = pos + i;
            if (p / 8 < d.size()) v |= u32((d[p / 8] >> (p & 7)) & 1) << i;
        }
        return v;
    }
};

struct Code {
    u8 bits, data;
    u32 code;
};

std::string cstring(ByteSpan d, size_t o, size_t n) {
    std::string s(reinterpret_cast<const char*>(d.data()) + o, n);
    while (!s.empty() && s.back() == '\0') s.pop_back(); // (the stored length takes in the terminator)
    return s;
}

} // namespace

// Layout: "GTDB", u32 flags, u32 rows, u32 row size; {u32 id, u32 offset}[rows] by id; then
//   u32 dictionary size (from here), u32 codes,
//   {u8 data, u8 bits}[256]: the byte a code of up to 8 bits stands for, by the next 8 bits of the
//       stream (bits 0: a longer code),
//   {u8 bits, u8 data, u16, u32 code}[codes];
//   u32 size of the plain rows (from here, with these 8 bytes), u32 plain rows (at most 64), the rows;
//   the packed rows, a row at its offset: u8 codes that follow, then the codes bit by bit:
//     the first is a byte: bits 0-5 a plain row, bits 6-7 the kind:
//       0 the plain row as it is; 1 a code per byte of the row;
//       2 the plain row with changes: a code per 8 bytes of the row (bit i: byte i changes),
//         then a code per changed byte.
Table Table::parse(ByteSpan d) {
    if (prologue::is_plain_table(d)) return prologue::parse_plain(d);
    if (!is_table(d)) throw FormatError("not a GTDB table");
    Table t;
    t.flags_ = rd32(d, 4);
    const u32 rows = rd32(d, 8);
    t.row_size_ = rd32(d, 12);
    if (t.row_size_ == 0 || t.row_size_ > 0x10000 || u64(rows) * 8 > d.size()) throw FormatError("GTDB: bad header");
    if (rows == 0) return t;
    size_t o = 0x10 + 8 * size_t(rows);
    const u32 dict_size = rd32(d, o), ncodes = rd32(d, o + 4);
    std::vector<Code> long_codes;
    ByteSpan prefix;
    if (ncodes) {
        prefix = d.subspan(o + 8, 512);
        for (u32 i = 0; i < ncodes; ++i) {
            const size_t c = o + 8 + 512 + 8 * size_t(i);
            if (d[c] > 8) long_codes.push_back({d[c], d[c + 1], rd32(d, c + 4)});
        }
    }
    const size_t plain_at = o + dict_size;
    const u32 plain_size = rd32(d, plain_at), plain_rows = rd32(d, plain_at + 4);
    if (plain_size < 8 || u64(plain_rows) * t.row_size_ > plain_size - 8 || plain_at + plain_size > d.size())
        throw FormatError("GTDB: bad plain rows");
    const ByteSpan plain = d.subspan(plain_at + 8, size_t(plain_rows) * t.row_size_);
    const size_t stream = plain_at + plain_size;

    t.data_.resize(size_t(rows) * t.row_size_);
    for (u32 r = 0; r < rows; ++r) {
        t.ids_.push_back(rd32(d, 0x10 + 8 * size_t(r)));
        const size_t at = stream + rd32(d, 0x10 + 8 * size_t(r) + 4);
        if (at >= d.size()) throw FormatError("GTDB: row out of range");
        Bits b{d, (at + 1) * 8};
        auto code = [&]() -> u8 {
            if (!ncodes) throw FormatError("GTDB: packed row without a dictionary");
            const u32 next = b.peek(8);
            if (const u8 bits = prefix[next * 2 + 1]) {
                b.pos += bits;
                return prefix[next * 2];
            }
            const u32 v = b.peek(24);
            for (const Code& c : long_codes)
                if ((v & ((1u << c.bits) - 1)) == c.code) {
                    b.pos += c.bits;
                    return c.data;
                }
            throw FormatError("GTDB: unknown code");
        };
        u8* out = t.data_.data() + size_t(r) * t.row_size_;
        const u8 head = code();
        const u32 kind = head >> 6, base = head & 63;
        if (kind != 1) {
            if (base >= plain_rows) throw FormatError("GTDB: plain row out of range");
            std::memcpy(out, plain.data() + size_t(base) * t.row_size_, t.row_size_);
        }
        if (kind == 1) {
            for (size_t k = 0; k < t.row_size_; ++k) out[k] = code();
        } else if (kind == 2) {
            std::vector<u8> mask((t.row_size_ + 7) / 8);
            for (u8& m : mask) m = code();
            for (size_t k = 0; k < t.row_size_; ++k)
                if (mask[k / 8] >> (k & 7) & 1) out[k] = code();
        } else if (kind == 3) {
            throw FormatError("GTDB: unknown row kind");
        }
        if ((b.pos + 7) / 8 > d.size()) throw FormatError("GTDB: row runs past the file");
    }
    if (!std::is_sorted(t.ids_.begin(), t.ids_.end())) throw FormatError("GTDB: rows not in id order");
    return t;
}

Table Table::make(u32 flags, size_t row_size, std::vector<u32> ids, Bytes data) {
    Table t;
    t.flags_ = flags, t.row_size_ = row_size, t.ids_ = std::move(ids), t.data_ = std::move(data);
    return t;
}

ByteSpan Table::row(u32 id) const {
    const auto it = std::lower_bound(ids_.begin(), ids_.end(), id);
    if (it == ids_.end() || *it != id) return {};
    return row_at(size_t(it - ids_.begin()));
}

void Table::add_rows(std::vector<std::pair<u32, Bytes>> rows) {
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& [id, bytes] : rows) {
        bytes.resize(row_size_);
        const size_t at = size_t(std::upper_bound(ids_.begin(), ids_.end(), id) - ids_.begin());
        ids_.insert(ids_.begin() + std::ptrdiff_t(at), id);
        data_.insert(data_.begin() + std::ptrdiff_t(at * row_size_), bytes.begin(), bytes.end());
    }
}

size_t Table::rows(u32 id, size_t& first) const {
    const auto range = std::equal_range(ids_.begin(), ids_.end(), id);
    first = size_t(range.first - ids_.begin());
    return size_t(range.second - range.first);
}

// "GTID", u32 count, u32 0, u32 table id; {u32 label offset, u32 row id}[count]; the labels from
// the end of that table: u16 length, the bytes.
Labels Labels::parse(ByteSpan d) {
    if (!is_labels(d)) throw FormatError("not a GTID label list");
    Labels l;
    const u32 n = rd32(d, 4);
    l.table_id = rd32(d, 12);
    const size_t base = 0x10 + 8 * size_t(n);
    if (base > d.size()) throw FormatError("GTID: bad count");
    for (u32 i = 0; i < n; ++i) {
        const size_t at = base + rd32(d, 0x10 + 8 * size_t(i));
        const u32 len = rd16(d, at);
        if (at + 2 + len > d.size()) throw FormatError("GTID: label out of range");
        l.entries.emplace_back(cstring(d, at + 2, len), rd32(d, 0x10 + 8 * size_t(i) + 4));
    }
    return l;
}

std::optional<u32> Labels::id(std::string_view label) const {
    for (const auto& [name, id] : entries)
        if (name == label) return id;
    return std::nullopt;
}

const std::string* Labels::label(u32 id) const {
    for (const auto& e : entries)
        if (e.second == id) return &e.first;
    return nullptr;
}

// "GTST", u32 count, 8 bytes; u32 offsets[count]; the strings from the end of that table: u16
// length, the bytes.
Strings Strings::parse(ByteSpan d) {
    if (!is_strings(d)) throw FormatError("not a GTST string pool");
    Strings s;
    const u32 n = rd32(d, 4);
    const size_t base = 0x10 + 4 * size_t(n);
    if (base > d.size()) throw FormatError("GTST: bad count");
    for (u32 i = 0; i < n; ++i) {
        const size_t at = base + rd32(d, 0x10 + 4 * size_t(i));
        const u32 len = rd16(d, at);
        if (at + 2 + len > d.size()) throw FormatError("GTST: string out of range");
        s.strings.push_back(cstring(d, at + 2, len));
    }
    return s;
}

SpecDb::SpecDb(Reader read, const Lister& list, std::string language) : read_(std::move(read)), language_(std::move(language)) {
    for (const std::string& name : list("specdb"))
        if (!name.empty() && name.back() == '/') {
            folder_ = "specdb/" + name;
            break;
        }
    if (folder_.empty()) throw FormatError("This disc has no car database (specdb/).");
    prologue_ = prologue::is_prologue_folder(folder_);
    for (const std::string& name : list(folder_))
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".dbt") == 0) names_.push_back(name.substr(0, name.size() - 4));
}

SpecDb::~SpecDb() = default;
SpecDb::SpecDb(SpecDb&&) noexcept = default;
SpecDb& SpecDb::operator=(SpecDb&&) noexcept = default;

// The mods' cars are worked out from the tables as the disc has them; then what they add goes into every table, label list
// and pool read so far, and into the others as they are read.
void SpecDb::prepare_mods() {
    if (mods_state_ != 0) return;
    mods_state_ = 1;
    ModCars m;
    if (!prologue_)
        if (const std::optional<Bytes> list = read_("mods/.cars"); list && !list->empty()) try {
                m = ModCars::build(*this, std::string_view(reinterpret_cast<const char*>(list->data()), list->size()), makers_);
                for (const ModCarStatus& s : m.statuses())
                    if (!s.loaded()) LOG_WARN("Mod car %s (%s): %s", s.label.c_str(), s.mod.c_str(), s.error.c_str());
            } catch (const std::exception& e) {
                LOG_WARN("Mod cars: %s", e.what());
                m = ModCars();
            }
    mods_ = std::make_unique<ModCars>(std::move(m));
    for (auto& [name, t] : tables_)
        if (t) mods_->extend(name, *t);
    for (auto& [name, l] : labels_)
        if (l) mods_->extend(name, *l);
    for (auto& [name, s] : strings_)
        if (s) mods_->extend(name, *s);
    mods_state_ = 2;
}

const ModCars& SpecDb::mod_cars() {
    prepare_mods();
    return *mods_;
}

std::optional<Bytes> SpecDb::mod_car_file(u64 code) const { return mods_ ? mods_->car_file(code) : std::nullopt; }

std::optional<Bytes> SpecDb::car_file(u64 code) {
    prepare_mods();
    if (mods_)
        if (std::optional<Bytes> f = mods_->car_file(code)) return f;
    char name[40];
    std::snprintf(name, sizeof name, "CARS/%016llX", static_cast<unsigned long long>(code));
    return read_(folder_ + name);
}

const Table* SpecDb::table(const std::string& name) {
    prepare_mods();
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        std::optional<Table> t;
        const std::string file_name = prologue_ ? prologue::file_name(name) : name;
        if (const std::optional<Bytes> file = read_(folder_ + file_name + ".dbt")) t = Table::parse(*file);
        if (t && prologue_) t = prologue::to_gt4(name, *t, *this);
        if (t && mods_) mods_->extend(name, *t);
        it = tables_.emplace(name, std::move(t)).first;
    }
    return it->second ? &*it->second : nullptr;
}

const Labels* SpecDb::labels(const std::string& name) {
    prepare_mods();
    auto it = labels_.find(name);
    if (it == labels_.end()) {
        std::optional<Labels> l;
        if (const std::optional<Bytes> file = read_(folder_ + (prologue_ ? prologue::file_name(name) : name) + ".idi")) l = Labels::parse(*file);
        if (l && prologue_) l = prologue::to_gt4(std::move(*l));
        if (l && mods_) mods_->extend(name, *l);
        it = labels_.emplace(name, std::move(l)).first;
    }
    return it->second ? &*it->second : nullptr;
}

const Strings* SpecDb::strings(const std::string& name) {
    prepare_mods();
    auto it = strings_.find(name);
    if (it == strings_.end()) {
        std::optional<Strings> s;
        if (const std::optional<Bytes> file = read_(folder_ + name + ".sdb")) s = Strings::parse(*file);
        if (s && mods_) mods_->extend(name, *s);
        it = strings_.emplace(name, std::move(s)).first;
    }
    return it->second ? &*it->second : nullptr;
}

// CAR_NAME_<language> rows are four string numbers of <language>_StrDB, the first the car's name;
// COURSE_NAME_<language> rows one. The rows have the ids of GENERIC_CAR's and COURSE's.
// CAR_VARIATION_<language> rows start with the id of the car's rows in VARIATION<language>, one
// row per paint, labelled "<model>_<n>".
std::vector<CarEntry> SpecDb::cars() {
    std::vector<CarEntry> out;
    const Labels* l = labels("GENERIC_CAR");
    const Table* names = table("CAR_NAME_" + language_);
    const Strings* pool = strings(language_ + "_StrDB");
    const Table* car_paints = table("CAR_VARIATION_" + language_);
    const Table* paints = table("VARIATION" + language_);
    const Labels* paint_labels = labels("VARIATION" + language_);
    if (!l) return out;
    for (const auto& [label, id] : l->entries) {
        CarEntry c{id, label, {}, {}, 0};
        if (names && pool)
            if (const ByteSpan row = names->row(id); row.size() >= 4) c.name = pool->at(rd32(row, 0));
        if (car_paints && paints && paint_labels)
            if (const ByteSpan row = car_paints->row(id); row.size() >= 4) {
                const u32 group = rd32(row, 0);
                size_t first = 0;
                c.paints = u32(paints->rows(group, first));
                if (const std::string* pl = paint_labels->label(group)) c.model = pl->substr(0, pl->rfind('_'));
            }
        out.push_back(std::move(c));
    }
    std::sort(out.begin(), out.end(), [](const CarEntry& a, const CarEntry& b) { return a.name != b.name ? a.name < b.name : a.id < b.id; });
    return out;
}

std::optional<u32> SpecDb::default_part(const std::string& car_label, u32 table_id) {
    const Labels* l = labels("DEFAULT_PARTS");
    const Table* t = table("DEFAULT_PARTS");
    if (!l || !t) return std::nullopt;
    const std::optional<u32> id = l->id("df_pt_" + car_label);
    if (!id) return std::nullopt;
    const ByteSpan row = t->row(*id);
    for (size_t o = 0; o + 8 <= row.size(); o += 8)
        if (rd32(row, o + 4) == table_id && rd32(row, o) != 0xFFFFFFFFu) return rd32(row, o);
    return std::nullopt;
}

// CHASSIS (table 6) rows, u16 millimetres: +0x00 length, +0x02 height, +0x04 wheelbase, +0x06 weight
// (kg), +0x12 / +0x14 the width across the front / rear tires' outer faces, +0x16 / +0x18 the front /
// rear tires' diameter. FRONTTIRE / REARTIRE (25 / 26) rows start with the id of a TIRESIZE row
// (31): +0x00 u8 aspect / 5, +0x02 u8 rim inches x 10, +0x03 u8 width / 5 mm.
bool SpecDb::car_wheels(const std::string& car_label, WheelLayout& out) {
    // A car without a row of default parts: its own rows by label ("ch_<car label>_a").
    auto own = [&](const char* table_name, const char* prefix) -> std::optional<u32> {
        const Labels* l = labels(table_name);
        if (!l) return std::nullopt;
        const std::string start = prefix + car_label + "_";
        std::optional<std::pair<std::string, u32>> best;
        for (const auto& [label, id] : l->entries)
            if (label.size() == start.size() + 1 && label.compare(0, start.size(), start) == 0 && (!best || label < best->first))
                best.emplace(label, id);
        return best ? std::optional<u32>(best->second) : std::nullopt;
    };
    std::optional<u32> chassis = default_part(car_label, 6);
    if (!chassis) chassis = own("CHASSIS", "ch_");
    const Table* ct = table("CHASSIS");
    if (!chassis || !ct) return false;
    const ByteSpan c = ct->row(*chassis);
    if (c.size() < 0x1A) return false;
    out.wheelbase = float(rd16(c, 0x04)) * 0.001f;
    const Table* sizes = table("TIRESIZE");
    for (int axle = 0; axle < 2; ++axle) {
        out.outer_width[axle] = float(rd16(c, 0x12 + 2 * size_t(axle))) * 0.001f;
        out.tire_diameter[axle] = float(rd16(c, 0x16 + 2 * size_t(axle))) * 0.001f;
        std::optional<u32> tire = default_part(car_label, axle == 0 ? 25 : 26);
        if (!tire) tire = own(axle == 0 ? "FRONTTIRE" : "REARTIRE", axle == 0 ? "ft_" : "rt_");
        const Table* tt = table(axle == 0 ? "FRONTTIRE" : "REARTIRE");
        if (!tire || !tt || !sizes) return false;
        const ByteSpan tr = tt->row(*tire);
        if (tr.size() < 4) return false;
        const ByteSpan sz = sizes->row(rd32(tr, 0));
        if (sz.size() < 4) return false;
        out.tire_width[axle] = float(sz[3]) * 0.005f;
    }
    return out.wheelbase > 0;
}

std::vector<CourseEntry> SpecDb::courses() {
    std::vector<CourseEntry> out;
    const Labels* l = labels("COURSE");
    const Table* names = table("COURSE_NAME_" + language_);
    const Strings* pool = strings(language_ + "_StrDB");
    if (!l) return out;
    for (const auto& [label, id] : l->entries) {
        CourseEntry c{id, label, {}};
        if (names && pool)
            if (const ByteSpan row = names->row(id); row.size() >= 4) c.name = pool->at(rd32(row, 0));
        out.push_back(std::move(c));
    }
    std::sort(out.begin(), out.end(), [](const CourseEntry& a, const CourseEntry& b) { return a.id < b.id; });
    return out;
}

} // namespace gt4r::db
