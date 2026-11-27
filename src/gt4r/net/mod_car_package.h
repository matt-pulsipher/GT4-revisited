#pragma once

// A mod car's package (formats/db/mod_cars.h) as an online player sends it to one who lacks the car: its car file
// (cars/<label>.ini) and the pictures it names, paths from the mod's folder, in the shared engine's
// package layout (game/mod_package.h: serialize_mod_package, parse_mod_package, install_mod_package) and moved by the shared
// session (Session::offer_package / request_package, by the package's SHA-256). Nothing is added to the protocol.
//
// A package comes from another player, so before it is written it is checked against this disc (validate_package): one car
// file that reads as a car, of the label the lobby named, whose donor is a car of this disc and whose values are within
// bounds a car can have; its other files exactly the pictures it names, PNG of at most 2048 a side. The install writes the
// pictures as our own encoding (install_mod_package), never the sender's bytes.

#include "gt4r/formats/db/specdb.h"

#include "game/mod_package.h"

#include <optional>
#include <string>

namespace gt4r::net {

// The package of a loaded mod car; none (and `error`) for another car or a file not read.
std::optional<Bytes> build_package(db::SpecDb& db, const std::string& label, std::string* error = nullptr);
// The checks above.
bool validate_package(const gt3r::game::ModPackage& p, db::SpecDb& db, const std::string& expected_label, std::string* error = nullptr);
// The donor a package's car names; "" when it has none that reads.
std::string package_donor(const gt3r::game::ModPackage& p);

} // namespace gt4r::net
