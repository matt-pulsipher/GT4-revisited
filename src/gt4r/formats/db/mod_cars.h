#pragma once

// Mod cars: new cars a mod defines in small text files, mods/<id>/cars/<label>.ini (docs/MODDING.md "Adding cars").
//
// A file names a donor, a car on the player's disc, and what is different: dimensions, weight, engine, gearing, drive,
// tires, price, maker. The car is made of the donor's rows of Gran Turismo 4's own tables: every row its parts as sold and
// the parts that fit it name (its DEFAULT_PARTS sets, its CARS/ file) is cloned under the new car's labels where the file
// changes that table, the file's values are written over the clones, and the rows are added to the tables as the database
// reads them (SpecDb). The car then works wherever a disc car does: the race builds it from its rows, the menus read its
// name, price and paints, a save keeps it by its code.

#include "gt4r/formats/db/specdb.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt4r::db {

// One car file. Unset values keep the donor's.
struct ModCarDef {
    std::string label, name, maker, donor, body; // body: another disc car whose body and paints the car takes ([car] model)
    std::string mod, file;                       // the mod's id, the file's path (the loader's)
    std::optional<u16> year;
    std::optional<u32> price;
    std::optional<u8> category; // GENERIC_CAR +0x11: 0 normal, 1 racing, 2 tuning, 3 concept
    bool for_sale = true;       // [dealer] for_sale

    // [body]
    std::optional<u16> weight_kg, length_mm, width_mm, height_mm, wheelbase_mm, front_track_mm, rear_track_mm, ride_height_mm;
    std::optional<u8> front_weight_pct;
    std::optional<float> cd, frontal_area_m2;
    bool scale_suspension = true, scale_brakes = true;

    // [powertrain]
    bool electric = false;
    std::optional<float> power_kw, torque_nm, max_rpm, idle_rpm, rated_kw;
    std::vector<std::pair<float, float>> curve; // rpm, N m
    std::optional<u8> output_pct;
    std::optional<u16> sound_id;
    std::string layout_text, aspiration_text, displacement_text;

    // [drivetrain]
    std::optional<u8> drive; // DRIVETRAIN +0x10: 0 FR, 1 FF, 2 4WD, 3 MR, 4 RR
    std::optional<u8> front_torque_pct;
    std::vector<float> gears; // forward ratios, first gear on
    std::optional<float> final_drive, reverse;

    // [tires]
    struct TireSize {
        u8 width_5 = 0, aspect_5 = 0, rim_10 = 0; // as TIRESIZE keeps them: width / 5 mm, aspect / 5, rim in tenths of an inch
    };
    std::optional<TireSize> front_tire, rear_tire;
    std::optional<float> grip_pct; // the car's tires as sold: their compounds' grip, percent

    // [model] a body of its own (binary glTF, relative to the mod folder), [paints] its colors
    std::string model_file, model_menu, model_race;
    std::array<std::string, 3> model_time; // day, eve, night
    std::string wheel;
    float model_lower_mm = 0;
    std::vector<std::pair<std::string, std::array<u8, 3>>> paints;

    // [dealer]
    std::string description, picture, thumb, logo;

    // [raw.<TABLE>] bytes of the car's own rows of a table (its clones of the donor's).
    struct Poke {
        std::string table; // "CHASSIS"
        size_t offset = 0;
        char type = 'b';   // 'b' u8, 'h' u16, 'i' u32, 'f' f32
        double value = 0;
    };
    std::vector<Poke> pokes;
};

// "255/35R21" as TIRESIZE keeps it; none when it is not a size the table can hold (width and aspect in fives).
std::optional<ModCarDef::TireSize> parse_tire_size(std::string_view s);
// A car file; none with `error` set when it cannot be a car. Keys it does not know go to `warnings`.
std::optional<ModCarDef> parse_mod_car(std::string_view text, std::string* error, std::vector<std::string>* warnings = nullptr);

// A mod car's row id in a table: the same for the same label whatever else is loaded (a save keeps the car by its code), in
// 0x40000000 .. 0x7FFFFFFF, above every row of the disc's.
u32 mod_row_id(std::string_view car_label, std::string_view table, u32 n);

// A car the mods define, as loaded.
struct ModCarStatus {
    std::string mod, file, label, name, donor;
    std::string error;                 // empty: loaded
    std::vector<std::string> warnings;
    bool loaded() const { return error.empty(); }
};

// The rows the mods' cars add to a database, worked out once from the disc's tables (ModCars::build) and added to each table,
// label list and string pool as the database reads it (SpecDb).
class ModCars {
public:
    // The cars of the files `list` names ("<mod id>\t<host path>" lines, gt3r::Vfs::kModCarsList; makers' and events' files
    // are passed over), in that order: a later file's car replaces an earlier one of the same label. `makers` are the program's
    // names by number (menu::ProgramTables::makers: "toyota"); without them a car keeps its donor's maker.
    static ModCars build(SpecDb& db, std::string_view list, const std::map<int, std::string>& makers);
    // The same from definitions already read (tests, tools).
    static ModCars build(SpecDb& db, std::vector<ModCarDef> defs, const std::map<int, std::string>& makers);

    bool empty() const { return statuses_.empty(); }
    const std::vector<ModCarStatus>& statuses() const { return statuses_; }
    // The loaded cars' definitions by label.
    const ModCarDef* def(const std::string& label) const;
    std::vector<std::string> labels() const;

    // What is added to a table, a label list, a string pool as the database reads it.
    void extend(const std::string& table, Table& t) const;
    void extend(const std::string& table, Labels& l) const;
    void extend(const std::string& pool, Strings& s) const;
    // A mod car's CARS/<code> file (the parts that fit it); none for any other code.
    std::optional<Bytes> car_file(u64 code) const;

    // A mod car's picture the menus ask for by its label, "piece/<folder>/<label>.img" (a leading slash or none): the strip's
    // (car_mini), the showroom's (car_pict), the name's artwork (car_logo, car_logo2). `disc`: the body car's picture of that
    // folder, for its size and as the picture where the mod brings none (none for the name's artwork, which would name the
    // other car); `png`: the mod's own picture ([dealer] thumb, picture, logo), a host path. None for any other path.
    struct Picture {
        std::string disc, png;
    };
    std::optional<Picture> picture(std::string_view path) const;

private:
    struct PaintGroup {
        u32 from = 0, to = 0;              // the donor's (or body car's) paint group and the car's
        float scale_box[4] = {1, 1, 1, 1}; // VARIATION +0x10 height, +0x14 width, +0x18 ahead, +0x1C behind
        std::optional<float> frontal_area; // +0x20, m2 (else the donor's x the width and height scales)
    };
    struct Names {
        u32 car_id = 0;
        std::string name;
    };
    std::vector<ModCarStatus> statuses_;
    std::map<std::string, ModCarDef> defs_;
    std::map<std::string, std::vector<std::pair<u32, Bytes>>> rows_;               // by table
    std::map<std::string, std::vector<std::pair<std::string, u32>>> labels_;       // by table
    std::map<std::string, std::vector<std::string>> strings_;                      // by pool
    std::map<std::string, size_t> pool_sizes_;                                     // the pools' own sizes (where the added strings start)
    std::vector<PaintGroup> paint_groups_;                                         // VARIATION<language>
    std::vector<Names> names_;                                                     // CAR_NAME_<language>, CAR_VARIATION_<language>
    std::map<u64, Bytes> car_files_;
    friend struct ModCarBuilder;
};

} // namespace gt4r::db
