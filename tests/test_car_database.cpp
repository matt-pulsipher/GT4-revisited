// The Car database page's facts (menu/car_database.h) on the user's discs: the dealer's spec sheet as the original's page
// shows it, the curve, the tune shop's parts and the events' regulations by the game's own scripts.

#include "test_framework.h"

#include "gt4r/disc/disc.h"
#include "gt4r/menu/car_database.h"
#include "gt4r/menu/project.h"

#include <algorithm>
#include <cstdio>

using namespace gt4r;

namespace {

menu::FileReader reader(Disc& d) {
    return [&d](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = d.volume().find(path);
        if (!e) return std::nullopt;
        return d.volume().read(*e);
    };
}

menu::RuntimeConfig config(Disc& d) {
    menu::RuntimeConfig c;
    c.list = [&d](const std::string& folder) {
        std::vector<std::string> out;
        const std::string f = !folder.empty() && folder.back() == '/' ? folder : folder + "/";
        for (const gt3r::Volume::Entry& e : d.volume().entries()) {
            if (e.path.compare(0, f.size(), f) != 0) continue;
            const size_t slash = e.path.find('/', f.size());
            std::string name = e.path.substr(f.size(), slash == std::string::npos ? slash : slash + 1 - f.size());
            if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
        }
        return out;
    };
    c.program_image = [&d] { return d.program_image(); };
    c.region = d.info().region, c.product = d.info().product;
    const std::vector<std::string> languages = menu::project_languages(c.list, d.info().game == Game::Prologue ? "prologue" : "boot");
    if (!languages.empty()) {
        c.language = languages[0];
        if (std::find(languages.begin(), languages.end(), d.info().language) != languages.end()) c.language = d.info().language;
    }
    return c;
}

std::string row(const std::vector<menu::CarDatabase::Row>& rows, const std::string& label) {
    for (const menu::CarDatabase::Row& r : rows)
        if (!r.heading && r.label == label) return r.value;
    return "(none)";
}

Disc* find_disc(bool prologue, const std::string& region = "us") {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if ((d->info().game == Game::Prologue) == prologue && (prologue || d->info().region == region)) return d.get();
    return nullptr;
}

} // namespace

// The US disc: the dealers' spec sheet of the Daihatsu MOVE Custom RS Limited '02 and the Copen's strip as the original shows
// them (reference 05-gtmode, 17_dealer_new_car_copen.png, 18_purchase_spec_sheet.png).
TEST(disc_car_database_spec_sheet) {
    Disc* disc = find_disc(false);
    if (!disc) SKIP("no Gran Turismo 4 US disc");
    menu::CarDatabase cdb(reader(*disc), config(*disc));
    CHECK(cdb.error().empty());
    CHECK(cdb.cars().size() >= 700);
    CHECK(cdb.power_unit() == "HP" && cdb.torque_unit() == "ft.lb");
    const menu::CarDatabase::Car* move = nullptr;
    const menu::CarDatabase::Car* copen = nullptr;
    for (const menu::CarDatabase::Car& c : cdb.cars()) {
        if (c.name == "Daihatsu MOVE Custom RS Limited '02") move = &c;
        if (c.name == "Daihatsu Copen Active Top '02") copen = &c;
    }
    CHECK(move && copen);
    if (!move || !copen) return;
    CHECK(move->maker == "Daihatsu");
    const auto s = cdb.specs(move->label);
    CHECK(row(s, "Type") == "L3:DOHC");
    CHECK(row(s, "Displacement") == "659cc");
    CHECK(row(s, "Power") == "63HP/6400rpm");
    CHECK(row(s, "Torque") == "75.94ft.lb/3200rpm");
    CHECK(row(s, "Dimensions") == "3395x1475x1610");
    CHECK(row(s, "Weight") == "920kg");
    CHECK(row(s, "Drivetrain") == "4WD");
    CHECK(row(s, "Price") == "Cr. 15,720");
    const auto c = cdb.specs(copen->label);
    CHECK(row(c, "Drivetrain") == "FF");
    CHECK(row(c, "Power") == "63HP/6000rpm");
    CHECK(row(c, "Torque") == "81.00ft.lb/3200rpm");
    CHECK(row(c, "Price") == "Cr. 14,980");
    // The curve: the showroom car's, its peak the catalog's power (within the unit's rounding).
    const auto cv = cdb.curve(move->label);
    CHECK(cv && cv->rpm.size() >= 6);
    if (cv) CHECK(cv->peak_power > 55 && cv->peak_power < 70 && cv->rev_limit > 6400);
}

// The tune shop's parts and the events, by the game's scripts.
TEST(disc_car_database_parts_and_events) {
    Disc* disc = find_disc(false);
    if (!disc) SKIP("no Gran Turismo 4 US disc");
    menu::CarDatabase cdb(reader(*disc), config(*disc));
    std::string ka, viper;
    for (const menu::CarDatabase::Car& c : cdb.cars()) {
        if (c.label == "ka_01") ka = c.label;
        if (c.drivetrain == "FR" && c.power > 400 && viper.empty()) viper = c.label;
    }
    CHECK(!ka.empty() && !viper.empty());
    if (ka.empty() || viper.empty()) return;
    const auto parts = cdb.upgrades(ka);
    bool exhaust = false, tires = false;
    for (const menu::CarDatabase::PartKind& k : parts) {
        if (k.group == "Exhaust")
            for (const menu::CarDatabase::Part& p : k.parts) exhaust = exhaust || p.price > 0;
        if (k.group == "Tires") tires = k.parts.size() >= 8;
    }
    CHECK(exhaust && tires);
    // The Sunday Cup takes the Ka; the FF Challenge does not take an FR car, its drivetrain the rule broken.
    auto find = [](const std::vector<menu::CarDatabase::Event>& events, const std::string& code) -> const menu::CarDatabase::Event* {
        for (const menu::CarDatabase::Event& e : events)
            if (e.code == code) return &e;
        return nullptr;
    };
    const auto ka_events = cdb.events(ka);
    CHECK(ka_events.size() > 50);
    const menu::CarDatabase::Event* sunday = find(ka_events, "am_sunday_0000");
    CHECK(sunday && sunday->allowed);
    const auto fr_events = cdb.events(viper);
    const menu::CarDatabase::Event* ff = find(fr_events, "am_ff_0000");
    CHECK(ff && !ff->allowed);
    if (ff) {
        bool drivetrain = false;
        for (const menu::CarDatabase::Row& r : ff->rules) drivetrain = drivetrain || (r.bad && r.label == "Drivetrain");
        CHECK(drivetrain);
    }
}

// Prologue: the cars and their sheets from the database and the program alone; no tune shops, no events.
TEST(disc_car_database_prologue) {
    Disc* disc = find_disc(true);
    if (!disc) SKIP("no Prologue disc");
    menu::CarDatabase cdb(reader(*disc), config(*disc));
    CHECK(cdb.prologue());
    CHECK(cdb.cars().size() >= 40);
    CHECK(cdb.power_unit() == "PS");
    size_t curves = 0;
    for (const menu::CarDatabase::Car& c : cdb.cars()) {
        CHECK(menu::CarDatabase::drawable(c.name));
        CHECK(row(cdb.specs(c.label), "Power") != "(none)");
        curves += cdb.curve(c.label).has_value();
    }
    CHECK(curves >= cdb.cars().size() / 2);
    CHECK(cdb.upgrades(cdb.cars()[0].label).empty() && cdb.events(cdb.cars()[0].label).empty());
}

// Every Gran Turismo 4 release: the names, makers and events in words the app's font draws (the Japanese disc's own are kana:
// its American car names, the makers by their cars' names, our words for the rest).
TEST(disc_car_database_each_release) {
    size_t seen = 0;
    for (const std::unique_ptr<Disc>& d : test::discs()) {
        if (d->info().game == Game::Prologue) continue;
        ++seen;
        menu::CarDatabase cdb(reader(*d), config(*d));
        CHECK(cdb.error().empty());
        CHECK(cdb.cars().size() >= 700);
        for (const menu::CarDatabase::Car& c : cdb.cars()) {
            if (!menu::CarDatabase::drawable(c.name) || !menu::CarDatabase::drawable(c.maker)) std::printf("  not drawn: %s [%s] [%s]\n", c.label.c_str(), c.name.c_str(), c.maker.c_str());
            CHECK(menu::CarDatabase::drawable(c.name) && menu::CarDatabase::drawable(c.maker));
        }
        const std::string label = cdb.cars()[size_t(cdb.cars().size() / 2)].label;
        const auto events = cdb.events(label);
        CHECK(events.size() > 100);
        for (const menu::CarDatabase::Event& e : events) {
            CHECK(menu::CarDatabase::drawable(e.name) && menu::CarDatabase::drawable(e.hall));
            for (const menu::CarDatabase::Row& r : e.rules) CHECK(menu::CarDatabase::drawable(r.label) && menu::CarDatabase::drawable(r.value));
        }
        for (const menu::CarDatabase::PartKind& k : cdb.upgrades(label))
            for (const menu::CarDatabase::Part& p : k.parts) CHECK(menu::CarDatabase::drawable(p.name));
        std::printf("  %s: %zu cars, %zu events\n", d->info().product.c_str(), cdb.cars().size(), events.size());
    }
    if (!seen) SKIP("no Gran Turismo 4 disc");
}
