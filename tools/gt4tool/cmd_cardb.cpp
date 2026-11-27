// gt4tool cardb: the Car database page's facts (menu/car_database.h).
//
//   gt4tool cardb <disc> list                 every car: label, maker, name, year, power, weight, drivetrain, price
//   gt4tool cardb <disc> <car label> [what]   a car's spec sheet, curve, upgrades and events (what: specs | curve | parts |
//                                             events; all by default)

#include "gt4r/disc/disc.h"
#include "gt4r/menu/car_database.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace gt4r;

namespace gt4tool {
menu::FileReader disc_reader(Disc& disc);  // cmd_menu.cpp
menu::FolderLister disc_lister(Disc& disc); // cmd_menu.cpp
}

namespace gt4r::tool {

int cmd_cardb(Disc& disc, const std::vector<std::string>& a) {
    menu::RuntimeConfig config;
    config.list = gt4tool::disc_lister(disc);
    config.program_image = [&disc] { return disc.program_image(); };
    config.region = disc.info().region, config.product = disc.info().product;
    const std::vector<std::string> languages = menu::project_languages(config.list, disc.info().game == Game::Prologue ? "prologue" : "boot");
    if (!languages.empty()) {
        config.language = languages[0];
        if (std::find(languages.begin(), languages.end(), disc.info().language) != languages.end()) config.language = disc.info().language;
    }
    menu::CarDatabase cdb(gt4tool::disc_reader(disc), config);
    if (!cdb.error().empty()) std::printf("%s\n", cdb.error().c_str());
    std::printf("%zu cars, %s, %s / %s\n", cdb.cars().size(), cdb.prologue() ? "Prologue" : "Gran Turismo 4", cdb.power_unit().c_str(), cdb.torque_unit().c_str());
    if (a[0] == "list") {
        for (const menu::CarDatabase::Car& c : cdb.cars())
            std::printf("%-28s %-16s %-44s %4d %5d %s %5d kg %-3s %s%s\n", c.label.c_str(), c.maker.c_str(), c.name.c_str(), c.year, c.power, cdb.power_unit().c_str(), c.weight,
                        c.drivetrain.c_str(), menu::CarDatabase::credits(c.price).c_str(), c.for_sale ? "" : " (not for sale)");
        return 0;
    }
    const std::string& label = a[0];
    const menu::CarDatabase::Car* c = cdb.car(label);
    if (!c) {
        std::printf("no car %s\n", label.c_str());
        return 1;
    }
    const std::string what = a.size() > 1 ? a[1] : "";
    std::printf("%s  (%s, %s)\n", c->name.c_str(), c->maker.c_str(), c->country.c_str());
    if (what.empty() || what == "specs")
        for (const menu::CarDatabase::Row& r : cdb.specs(label))
            std::printf(r.heading ? "-- %s\n" : "   %-22s %s\n", r.label.c_str(), r.value.c_str());
    if (what.empty() || what == "curve") {
        if (const std::optional<menu::CarDatabase::Curve> cv = cdb.curve(label)) {
            std::printf("-- curve: peak %.1f %s, %.2f %s, rev limit %d\n", double(cv->peak_power), cv->power_unit.c_str(), double(cv->peak_torque), cv->torque_unit.c_str(), cv->rev_limit);
            for (size_t i = 0; i < cv->rpm.size(); ++i) std::printf("   %5d rpm  %7.1f  %7.2f\n", cv->rpm[i], double(cv->power[i]), double(cv->torque[i]));
        } else std::printf("-- no curve\n");
    }
    if (what.empty() || what == "parts")
        for (const menu::CarDatabase::PartKind& k : cdb.upgrades(label)) {
            std::printf("-- %s: %s\n", k.group.c_str(), k.name.c_str());
            for (const menu::CarDatabase::Part& p : k.parts) std::printf("   %-40s %s%s\n", p.name.c_str(), menu::CarDatabase::credits(p.price).c_str(), p.stock ? "  (as sold)" : "");
        }
    if (what.empty() || what == "events") {
        int allowed = 0, n = 0;
        for (const menu::CarDatabase::Event& e : cdb.events(label)) {
            ++n, allowed += e.allowed;
            std::printf("   %-3s %-24s %-40s %s %s\n", e.allowed ? "yes" : "no", e.hall.c_str(), e.name.c_str(), e.code.c_str(), e.level.c_str());
            for (const menu::CarDatabase::Row& r : e.rules) std::printf("        %s %-26s %s\n", r.bad ? "x" : " ", r.label.c_str(), r.value.c_str());
        }
        std::printf("-- %d of %d events\n", allowed, n);
    }
    return 0;
}

} // namespace gt4r::tool
