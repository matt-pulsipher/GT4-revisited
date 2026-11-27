#pragma once

// A disc car exported whole as a mod car (docs/PLAYING.md "Mod cars", the Car database's Export): the player's own disc's
// car for their other discs (a car one release has and another lacks: the European release's Vauxhalls on the American disc).
//
// The mod (mods/cars_<disc>/) gets per car:
//   cars/<label>_<disc>.ini          the car file: its donor the car itself (a disc that has it builds the car from its
//                                    own rows), `records` the file of its rows for a disc that lacks it
//   cars/<label>_<disc>.records      every row the car names (its sets, parts, the parts that fit it, tire sizes and
//                                    compounds, names, paints, Arcade versions), the texts they name, its CARS/ file
//   files/car/lod|menu/<model>_<disc>[.pat], files/piece/car_*/<label>_<disc>.img
//                                    its models and pictures, named so that they never replace another disc's
// On load (ModCars), a disc that lacks the donor adds the rows under the car's label: a row whose label the disc has in that
// table is the disc's (the shared steering, compounds, shop parts), every other row the file's, its references made to
// the rows it is added as.

#include "gt4r/formats/db/specdb.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt4r::db {

struct CarRecords {
    struct Row {
        std::string table, label;
        u32 id = 0;
        Bytes bytes; // empty: named only (a RACE row an Arcade entry names: the disc's by its label)
    };
    struct Text {
        std::string pool;
        u32 index = 0;
        std::string text;
    };
    std::string car; // the car's label on its disc
    u32 car_id = 0;
    std::vector<Row> rows;   // a VARIATION group's rows each (one id)
    std::vector<Text> texts; // the pools' texts the rows name
    Bytes car_file;          // its CARS/ file

    Bytes serialize() const;
    static std::optional<CarRecords> parse(ByteSpan data, std::string* error = nullptr);
    const std::string* text(const std::string& pool, u32 index) const;
};

// Every row car `label` of `db` names; none (with `error`) for a car the disc lacks or a mod car.
std::optional<CarRecords> collect_car_records(SpecDb& db, const std::string& label, std::string* error = nullptr);

// The suffix of a disc's exports, from its key (lower-case letters and digits: "SLES-51893" -> "sles51893").
std::string export_suffix(std::string_view disc_key);
// The mod folder of a disc's exports: "cars_<suffix>".
std::string export_mod_id(std::string_view disc_key);

struct CarExport {
    std::string label;              // the exported car's (<label>_<suffix>)
    std::vector<std::string> files; // written, from the mod folder
};
// Writes car `label` of `db` (the disc read through `read`) into `mod_dir` (made, with its mod.ini naming `source` when it
// has none). None (with `error`) when it is not a disc car or a file is not written.
std::optional<CarExport> export_car(SpecDb& db, const std::function<std::optional<Bytes>(const std::string&)>& read, const std::string& label,
                                    const std::string& mod_dir, std::string_view disc_key, const std::string& source, std::string* error = nullptr);

} // namespace gt4r::db
