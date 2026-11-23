#pragma once

// The car database (specdb/<set>/): tables of fixed-size rows (.dbt, "GTDB"), each with a list
// of labels for its rows (.idi, "GTID"), and string pools (.sdb, "GTST");
// docs/formats/database.md.

#include "gt4r/base/types.h"

#include <functional>
#include <map>
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

    u32 flags() const { return flags_; }
    size_t row_size() const { return row_size_; }
    size_t size() const { return ids_.size(); }
    const std::vector<u32>& ids() const { return ids_; } // ascending
    // A row's bytes (the first with that id); empty when the table has no such row.
    ByteSpan row(u32 id) const;
    // How many rows have the id (a car's paint rows share their id), from index `first`.
    size_t rows(u32 id, size_t& first) const;
    ByteSpan row_at(size_t index) const { return ByteSpan(data_).subspan(index * row_size_, row_size_); }

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
class SpecDb {
public:
    using Reader = std::function<std::optional<Bytes>(const std::string& path)>;
    using Lister = std::function<std::vector<std::string>(const std::string& dir)>;
    // `language`: the name tables' and string pool's ("american", "british", "japanese", ...).
    // Finds the set's folder under specdb/. Throws FormatError when the disc has none.
    SpecDb(Reader read, const Lister& list, std::string language = "american");

    const std::string& folder() const { return folder_; } // "specdb/GT4_US2560/"
    std::vector<std::string> table_names() const { return names_; }
    // A table, its labels, a pool; null when the set has none by that name.
    const Table* table(const std::string& name);
    const Labels* labels(const std::string& name);
    const Strings* strings(const std::string& name);

    std::vector<CarEntry> cars();
    std::vector<CourseEntry> courses();

private:
    Reader read_;
    std::string folder_, language_;
    std::vector<std::string> names_;
    std::map<std::string, std::optional<Table>> tables_;
    std::map<std::string, std::optional<Labels>> labels_;
    std::map<std::string, std::optional<Strings>> strings_;
};

} // namespace gt4r::db
