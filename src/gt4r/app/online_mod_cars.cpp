// The lobby's part for mod cars (docs/specs/net.md "Mod cars"): our mod car travels with its package's hash, size and donor,
// and the package is offered to the session; another player's mod car this disc lacks (its donor a car of this disc) is
// fetched from its owner, checked (net/mod_car_package.h) and installed as a mod of its own, mods/dl-<hash>/; the database is
// read again with it and the car checked as any other.

#include "gt4r/app/online.h"
#include "gt4r/formats/db/mod_cars.h"
#include "gt4r/net/mod_car_package.h"

#include "base/log.h"
#include "base/sha256.h"

#include <map>

namespace gt4r {

void online_mod_cars(App& app, net::Lobby& l) {
    App* a = &app;
    // (set once: they read the database each time, which a package installed replaces)
    if (!l.describe_car)
        l.describe_car = [a, &l](net::CarChoice& c) {
            db::SpecDb* sdb = a->db();
            const db::ModCarDef* def = sdb ? sdb->mod_cars().def(c.label) : nullptr;
            if (!def) {
                c.package.clear(), c.package_size = 0, c.donor.clear();
                return;
            }
            static std::map<std::string, std::pair<std::string, u32>> made; // a car's file -> its package's hash and size
            auto it = made.find(def->file);
            if (it == made.end()) {
                std::string why;
                const std::optional<Bytes> blob = net::build_package(*sdb, c.label, &why);
                if (!blob) {
                    LOG_WARN("Online: mod car %s: no package: %s", c.label.c_str(), why.c_str());
                    return;
                }
                const std::string hash = gt3r::sha256_hex(*blob);
                l.session().offer_package(hash, *blob);
                it = made.emplace(def->file, std::pair{hash, u32(blob->size())}).first;
            }
            c.package = it->second.first, c.package_size = it->second.second, c.donor = def->donor;
        };
    if (!l.wants_package)
        l.wants_package = [a](const net::CarChoice& c) {
            db::SpecDb* sdb = a->db();
            const db::Labels* cars = sdb ? sdb->labels("GENERIC_CAR") : nullptr;
            return cars && !cars->id(c.label) && cars->id(c.donor) && !sdb->mod_cars().def(c.donor);
        };
    if (!l.on_package)
        l.on_package = [a](const std::string& hash, Bytes blob, const net::CarChoice& c, const std::string& from) -> std::string {
            db::SpecDb* sdb = a->db();
            if (!sdb) return "no car database";
            std::string why;
            const std::optional<gt3r::game::ModPackage> p = gt3r::game::parse_mod_package(blob, &why);
            if (!p || !net::validate_package(*p, *sdb, c.label, &why)) {
                LOG_WARN("Online: %s's package for %s refused: %s", from.c_str(), c.label.c_str(), why.c_str());
                return "its files were refused: " + why;
            }
            std::string name = c.label;
            if (const std::optional<db::ModCarDef> d = [&]() -> std::optional<db::ModCarDef> {
                    for (const auto& f : p->files)
                        if (f.path.size() > 4 && f.path.compare(f.path.size() - 4, 4, ".ini") == 0)
                            return db::parse_mod_car(std::string_view(reinterpret_cast<const char*>(f.data.data()), f.data.size()), nullptr);
                    return std::nullopt;
                }())
                name = d->name;
            const std::optional<std::string> dir = gt3r::game::install_mod_package(*p, hash, a->mods_dir(), name, from, &why);
            if (!dir) return "its files were not written: " + why;
            LOG_INFO("Online: %s's car %s installed in %s", from.c_str(), c.label.c_str(), dir->c_str());
            a->rescan_mods();
            online_setup(*a); // (the builder of the database read again)
            return {};
        };
}

} // namespace gt4r
