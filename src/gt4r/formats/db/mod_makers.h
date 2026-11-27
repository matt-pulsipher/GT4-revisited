#pragma once

// Mod makers: car makers a mod adds, mods/<id>/makers/<key>.ini (docs/PLAYING.md "Mod cars"). A maker gets numbers past the
// program's own lists (the car table's maker, GENERIC_CAR +0x0F, and brand, +0x10, the tuners' list the dealers and pages name
// a car's maker by), so the mod's cars that name it are its cars; Gran Turismo mode's dealers of its country's page sell them
// (menu/mod_car_lists.cpp), with the mod's pictures and its name.
//
//   [maker]
//   name = Tesla                 ; what the pages show; a car's [car] maker names it by this or the file's name
//   country = US                 ; the program's country codes (JP US GB DE FR IT AU KR BE NL SE ES); `region` as GT3 Revisited's
//                                ; files have it (jp us gr fr it uk bel aus) too
//   icon = images/tesla_icon.png ; the country page's button (128 x 84)
//   logo = images/tesla_logo.png ; the dealer page's logo (378 x 135, transparent)

#include "gt4r/formats/db/mod_cars.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt4r::db {

struct ModMakerDef {
    std::string key;     // the file's name, lower case: the menus' name of the maker ("tesla": NewCarData.tesla_carlist)
    std::string name;    // what the pages show
    std::string country; // a country code of the program's list ("US")
    std::string icon, logo, description; // the mod's files, from its folder
    std::string mod, file;
    int maker = -1, tuner = -1, country_number = -1; // the numbers it is given (the car table's columns)
    std::string folder() const;                      // the mod's folder (the one makers/ is in), with its slash
};

// A maker file; none with `error` set when it cannot be one. `key` is the file's name.
std::optional<ModMakerDef> parse_mod_maker(std::string_view text, const std::string& key, std::string* error, std::vector<std::string>* warnings = nullptr);

class ModMakers {
public:
    struct Status {
        std::string mod, file, key, name, error; // error empty: loaded
        std::vector<std::string> warnings;
    };
    // The makers of the maker files `list` names ("<mod id>\t<host path>" lines, gt3r::Vfs::kModCarsList), in that order (a later
    // file's maker of the same key replaces an earlier one), numbered after the program's makers, tuners (each maker the next
    // free number of each list, < 256) and matched with its countries. A key of the program's own makers or tuners is no new
    // maker (its cars name it already). `before`: makers kept as they are (their numbers too); only new files are read.
    static ModMakers read(std::string_view list, const std::map<int, std::string>& makers, const std::map<int, std::string>& tuners,
                          const std::map<int, std::string>& countries, const ModMakers* before = nullptr);

    const std::vector<ModMakerDef>& makers() const { return makers_; }
    const std::vector<Status>& statuses() const { return statuses_; }
    bool empty() const { return makers_.empty(); }
    // The maker a car file's `maker` names: its name or its key (case, spaces, dashes, dots and underscores aside).
    const ModMakerDef* named(std::string_view name) const;
    const ModMakerDef* by_key(std::string_view key) const;
    const ModMakerDef* by_maker(int number) const;

    // A mod maker's picture the menus ask for by its key: the country page's button "image/piece/tuner_logo2/<key>.png" (its
    // icon), the dealer page's logo "piece/tuner_logo/<key>.img" and its tune shop's "piece/tuneshop_logo/<key>.img" (DealerData
    // getSubTuner: the dealer's own without a sub-tuner; its logo). `png` the mod's file; no stand-in of the disc's.
    std::optional<ModCars::Picture> picture(std::string_view path) const;

private:
    std::vector<ModMakerDef> makers_;
    std::vector<Status> statuses_;
};

// A maker's name as a car file's `maker` is matched (lower case, no spaces, dashes, dots or underscores).
std::string maker_match_key(std::string_view name);

} // namespace gt4r::db
