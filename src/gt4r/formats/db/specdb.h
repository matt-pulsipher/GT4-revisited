#pragma once

// The car database (specdb/<set>/): tables of fixed-size rows (.dbt, "GTDB"), each with a list
// of labels for its rows (.idi, "GTID"), and string pools (.sdb, "GTST");
// docs/formats/database.md.

#include "gt4r/base/types.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::db {

// A table: rows by id, every row `row_size` bytes. Rows are stored packed (a Huffman code per
// byte, or as changes to one of up to 64 plain rows) and unpacked here.
class Table {
public:
    static bool is_table(ByteSpan d) { return d.size() >= 0x10 && gt3r::has_magic(d, "GTDB"); }
    // Throws FormatError.
    static Table parse(ByteSpan file);
    // A table of the given rows (`ids` ascending, `data` their rows in that order).
    static Table make(u32 flags, size_t row_size, std::vector<u32> ids, Bytes data);

    u32 flags() const { return flags_; }
    size_t row_size() const { return row_size_; }
    size_t size() const { return ids_.size(); }
    const std::vector<u32>& ids() const { return ids_; } // ascending
    // A row's bytes (the first with that id); empty when the table has no such row.
    ByteSpan row(u32 id) const;
    // How many rows have the id (a car's paint rows share their id), from index `first`.
    size_t rows(u32 id, size_t& first) const;
    ByteSpan row_at(size_t index) const { return ByteSpan(data_).subspan(index * row_size_, row_size_); }
    // Rows added (a mod car's, mod_cars.h): each its id and row_size() bytes; the ids stay in order.
    void add_rows(std::vector<std::pair<u32, Bytes>> rows);

private:
    u32 flags_ = 0;
    size_t row_size_ = 0;
    std::vector<u32> ids_;
    Bytes data_; // the rows in id order
};

// A table's labels: one per row id.
struct Labels {
    u32 table_id = 0;
    std::vector<std::pair<std::string, u32>> entries; // label, row id (in file order: by label)
    static bool is_labels(ByteSpan d) { return d.size() >= 0x10 && gt3r::has_magic(d, "GTID"); }
    static Labels parse(ByteSpan file);
    std::optional<u32> id(std::string_view label) const;
    const std::string* label(u32 id) const;
};

// A string pool: strings by index.
struct Strings {
    std::vector<std::string> strings;
    static bool is_strings(ByteSpan d) { return d.size() >= 0x10 && gt3r::has_magic(d, "GTST"); }
    static Strings parse(ByteSpan file);
    const std::string& at(u32 i) const {
        static const std::string kNone;
        return i < strings.size() ? strings[i] : kNone;
    }
};

// A car as the car list needs it.
struct CarEntry {
    u32 id = 0;
    std::string label; // "_117_coupe_68"
    std::string name;  // "Isuzu 117 Coupe `68" in the language asked for
    std::string model; // "isuz0001": its files are car/lod/<model>, car/menu/<model>
    u32 paints = 0;    // its rows of the paint table
};
struct CourseEntry {
    u32 id = 0;
    std::string label; // "capri": the course file is crs/<label>
    std::string name;  // "Amalfi"
};

// The database of a disc: its tables, labels and string pools, read as they are asked for.
class ModCars;

class SpecDb {
public:
    using Reader = std::function<std::optional<Bytes>(const std::string& path)>;
    using Lister = std::function<std::vector<std::string>(const std::string& dir)>;
    // `language`: the name tables' and string pool's ("american", "british", "japanese", ...).
    // Finds the set's folder under specdb/. Throws FormatError when the disc has none.
    SpecDb(Reader read, const Lister& list, std::string language = "american");

    const std::string& folder() const { return folder_; } // "specdb/GT4_US2560/"
    // Gran Turismo 4 Prologue's set, read in Gran Turismo 4's layout (specdb_prologue.h).
    bool prologue() const { return prologue_; }
    std::vector<std::string> table_names() const { return names_; }
    // A table, its labels, a pool; null when the set has none by that name.
    const Table* table(const std::string& name);
    const Labels* labels(const std::string& name);
    const Strings* strings(const std::string& name);

    std::vector<CarEntry> cars();
    // The row of table `table_id` among the parts a car has as sold (DEFAULT_PARTS, the row
    // labelled "df_pt_<car label>": pairs of u32 row id, u32 table id); nullopt when it has none.
    std::optional<u32> default_part(const std::string& car_label, u32 table_id);
    // A car's wheel layout from its chassis and tire rows (car_model.h Wheels); false when a row
    // is missing.
    struct WheelLayout {
        float wheelbase = 0, outer_width[2] = {}, tire_diameter[2] = {}, tire_width[2] = {};
    };
    bool car_wheels(const std::string& car_label, WheelLayout& out);
    std::vector<CourseEntry> courses();

    // The mods' cars (mod_cars.h): the cars of the files the reader lists (gt3r::Vfs::kModCarsList), worked out when the first
    // table, label list or pool is read and added to each as it is. `makers` (menu::ProgramTables::makers) match a car's
    // maker; set them before anything is read.
    void set_makers(std::map<int, std::string> makers) { makers_ = std::move(makers); }
    const std::map<int, std::string>& makers() const { return makers_; }
    const ModCars& mod_cars();
    // The mods' cars the reader lists now that are not in yet (a mod installed since they were worked out), added to every table
    // read and to come; the labels added. The ones in stay as they are (a removed mod's car stays until the database is made
    // again), and so do the tables, label lists and pools already handed out (rows are added to them).
    std::vector<std::string> add_mod_cars();
    // The parts that fit a car (specdb/<set>/CARS/<the car's code, 16 hex digits>), a mod car's included; none when it has none.
    std::optional<Bytes> car_file(u64 code);
    // A race's pool of opponents (specdb/<set>/RACES/<35, the RACE row>), with the mod cars that join it; none without one.
    // Gran Turismo 4's tables (Prologue's files are named otherwise: specdb_prologue.h).
    std::optional<Bytes> race_file(u32 race_row);
    // A mod car's alone, once the mods' cars are worked out (any table read); none for a disc car.
    std::optional<Bytes> mod_car_file(u64 code) const;

    ~SpecDb();
    SpecDb(SpecDb&&) noexcept;
    SpecDb& operator=(SpecDb&&) noexcept;

private:
    void prepare_mods();
    Reader read_;
    std::string folder_, language_;
    bool prologue_ = false;
    std::vector<std::string> names_;
    std::map<std::string, std::optional<Table>> tables_;
    std::map<std::string, std::optional<Labels>> labels_;
    std::map<std::string, std::optional<Strings>> strings_;
    std::map<int, std::string> makers_;
    std::unique_ptr<ModCars> mods_;
    int mods_state_ = 0; // 0 not worked out, 1 being worked out (the tables read as the disc has them), 2 done
};

} // namespace gt4r::db
