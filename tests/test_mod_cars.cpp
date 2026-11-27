// Mod cars (formats/db/mod_cars.*): the car file, then a car made of a disc car's rows on the discs of GT4R_DISC.

#include "test_framework.h"
#include "menu_run_flow.h"

#include "gt4r/audio/car_sound.h"
#include "gt4r/formats/db/car_parts.h"
#include "gt4r/formats/db/mod_cars.h"
#include "gt4r/save/game_data.h"
#include "gt4r/sim/car_spec.h"

#include "base/file.h"

#include <cmath>
#include <filesystem>

using namespace gt4r;

namespace {

const char* const kCar = R"(
[car]
label = gt4r_test_coupe_26   ; a comment
name = Test Coupe `26
maker = Mazda
year = 2026
price = 31500
donor = impreza_wrx_sti_00
category = tuning

[body]
weight_kg = 1180
front_weight_pct = 52
length_mm = 4300
wheelbase_mm = 2500
front_track_mm = 1500
cd = 0.31

[powertrain]
power_kw = 220
max_rpm = 8000
layout = Inline4

[drivetrain]
drive = FR
gears = 3.2, 2.1, 1.5, 1.15, 0.92, 0.78
final_drive = 4.1

[tires]
front = 225/40R18
rear = 255/35R18
grip_pct = 105

[wobble]
x = 1
)";

// `list` may change after (a mod installed while the game runs).
db::SpecDb open_db(Disc& disc, std::shared_ptr<const std::string> list) {
    gt3r::Volume* vol = &disc.volume();
    return db::SpecDb(
        [vol, list](const std::string& path) -> std::optional<Bytes> {
            if (path == "mods/.cars") return Bytes(list->begin(), list->end());
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
}
db::SpecDb open_db(Disc& disc, std::string list) { return open_db(disc, std::make_shared<const std::string>(std::move(list))); }


// The file written in a folder of its own, "<mod>\t<path>" as the file layer lists it.
std::string write_car(const std::string& name, const std::string& text) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r_test_mod_cars" / "cars";
    std::filesystem::create_directories(dir);
    const std::string path = (dir / name).string();
    gt3r::write_host_file(path, ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()));
    return "test_mod\t" + path + "\n";
}

} // namespace

TEST(mod_car_file) {
    CHECK(db::parse_tire_size("225/40R18") && db::parse_tire_size("225/40R18")->width_5 == 45 && db::parse_tire_size("225/40R18")->rim_10 == 180);
    CHECK(db::parse_tire_size("165/70 ZR 14.5") && db::parse_tire_size("165/70 ZR 14.5")->rim_10 == 145);
    CHECK(!db::parse_tire_size("223/40R18"));
    CHECK(!db::parse_tire_size("225-40-18"));
    std::string error;
    std::vector<std::string> warnings;
    const std::optional<db::ModCarDef> d = db::parse_mod_car(kCar, &error, &warnings);
    CHECK(d);
    if (!d) return;
    CHECK(d->label == "gt4r_test_coupe_26" && d->name == "Test Coupe `26" && d->donor == "impreza_wrx_sti_00");
    CHECK(d->price && *d->price == 31500 && d->category && *d->category == 2);
    CHECK(d->gears.size() == 6 && std::fabs(d->gears[5] - 0.78f) < 1e-6f);
    CHECK(d->drive && *d->drive == 0);
    CHECK(d->front_tire && d->rear_tire && d->rear_tire->width_5 == 51);
    CHECK(warnings.size() == 1); // [wobble] x
    // What a car cannot be.
    CHECK(!db::parse_mod_car("[car]\nlabel = Bad Label\nname = x\ndonor = y\n", &error));
    CHECK(!db::parse_mod_car("[car]\nlabel = ok\nname = x\n", &error) && error.find("donor") != std::string::npos);
    CHECK(!db::parse_mod_car("[car]\nlabel = ok\nname = x\ndonor = y\n[raw.TIRESIZE]\n0 = 1\n", &error));
    CHECK(db::parse_mod_car("[car]\nlabel = ok\nname = x\ndonor = y\n[raw.chassis]\n0x1D = 120\n0x00:u16 = 4000\n", &error));
    // A row's id is the label's, whatever else is loaded.
    CHECK(db::mod_row_id("a", "GENERIC_CAR", 0) == db::mod_row_id("a", "GENERIC_CAR", 0));
    CHECK(db::mod_row_id("a", "GENERIC_CAR", 0) != db::mod_row_id("b", "GENERIC_CAR", 0));
    CHECK(db::mod_row_id("a", "CHASSIS", 0) >= 0x40000000u && db::mod_row_id("a", "CHASSIS", 0) <= 0x7FFFFFFFu);
}

TEST(mod_car_rows) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("no Gran Turismo 4 disc");
    db::SpecDb plain = open_db(*disc, "");
    std::string list = write_car("gt4r_test_coupe_26.ini", kCar);
    list += write_car("broken.ini", "[car]\nlabel = gt4r_broken\nname = Broken\ndonor = no_such_car\n");
    db::SpecDb sdb = open_db(*disc, list);
    sdb.set_makers({{0x16, "mazda"}, {2, "toyota"}}); // (the US program's numbers: rx8_types_03's maker)
    const db::ModCars& mods = sdb.mod_cars();
    CHECK(mods.statuses().size() == 2);
    CHECK(mods.labels() == std::vector<std::string>{"gt4r_test_coupe_26"});
    for (const db::ModCarStatus& s : mods.statuses())
        if (s.label == "gt4r_broken") CHECK(s.error.find("no_such_car") != std::string::npos);

    // The car in the tables: its row, its name, its paints (the donor's body).
    const std::optional<db::CarParts> car = db::car_parts(sdb, "gt4r_test_coupe_26");
    const std::optional<db::CarParts> donor = db::car_parts(sdb, "impreza_wrx_sti_00");
    CHECK(car && donor);
    if (!car || !donor) return;
    CHECK(car->car_id == db::mod_row_id("gt4r_test_coupe_26", "GENERIC_CAR", 0));
    CHECK(rd32(ByteSpan(car->car_row), 8) == 31500 && rd16(ByteSpan(car->car_row), 0x0C) == 2026);
    CHECK(car->car_row[0x0F] == 0x16 && car->car_row[0x11] % 100 == 2);
    // Mazda's brand (the pages' maker name: rx8_types_03's +0x10) and country, not the donor's Subaru.
    const std::optional<db::CarParts> rx8 = db::car_parts(sdb, "rx8_types_03");
    CHECK(rx8 && rx8->car_row.size() > 0x10 && car->car_row[0x10] == rx8->car_row[0x10] && car->car_row[0x0E] == rx8->car_row[0x0E]);
    const std::vector<db::CarEntry> all = sdb.cars(), disc_cars = plain.cars();
    CHECK(all.size() == disc_cars.size() + 1);
    const db::CarEntry* entry = nullptr;
    const db::CarEntry* donor_entry = nullptr;
    for (const db::CarEntry& e : all) {
        if (e.label == "gt4r_test_coupe_26") entry = &e;
        if (e.label == "impreza_wrx_sti_00") donor_entry = &e;
    }
    CHECK(entry && donor_entry);
    if (entry && donor_entry) {
        CHECK(entry->name == "Test Coupe `26");
        CHECK(entry->model == donor_entry->model && entry->paints == donor_entry->paints && entry->paints > 0);
    }
    // The donor is as it was.
    const std::optional<db::CarParts> plain_donor = db::car_parts(plain, "impreza_wrx_sti_00");
    CHECK(plain_donor && plain_donor->parts == donor->parts && plain_donor->car_row == donor->car_row);

    // Its own rows where the file changes them, the donor's where it does not.
    CHECK(car->part(db::PartTable::Chassis) != donor->part(db::PartTable::Chassis));
    CHECK(car->part(db::PartTable::Steer) == donor->part(db::PartTable::Steer));
    const std::optional<db::Chassis> ch = db::Chassis::read(db::part_row(sdb, car->part(db::PartTable::Chassis)));
    CHECK(ch && ch->weight_kg == 1180 && ch->length_mm == 4300 && ch->wheelbase_mm == 2500 && ch->front_weight_pct == 52);
    const std::optional<db::Engine> en = db::Engine::read(db::part_row(sdb, car->part(db::PartTable::Engine)));
    CHECK(en && en->rev_limit == 80);
    if (en) CHECK(std::fabs(en->peak().ps - 220 / 0.73549875f) < 3);
    const std::optional<db::Drivetrain> dt = db::Drivetrain::read(db::part_row(sdb, car->part(db::PartTable::Drivetrain)));
    CHECK(dt && dt->drive == 0 && dt->awd_system == 0xFE);
    const std::optional<db::Gear> ge = db::Gear::read(db::part_row(sdb, car->part(db::PartTable::Gear)));
    CHECK(ge && ge->gears == 6 && ge->ratio[0] == 3200 && ge->ratio[5] == 780 && ge->final_drive == 4100);
    const ByteSpan ft = db::part_row(sdb, car->part(db::PartTable::FrontTire)), rt = db::part_row(sdb, car->part(db::PartTable::RearTire));
    const std::optional<db::TireSize> fs = db::TireSize::read(db::part_row(sdb, db::PartTable::TireSize, rd32(ft, 0)));
    const std::optional<db::TireSize> rs = db::TireSize::read(db::part_row(sdb, db::PartTable::TireSize, rd32(rt, 0)));
    CHECK(fs && fs->width_mm == 225 && fs->aspect == 40 && std::fabs(fs->rim_inches - 18) < 0.01f);
    CHECK(rs && rs->width_mm == 255 && rs->aspect == 35);
    // The stock tires' compounds 5 % more grip.
    const ByteSpan dft = db::part_row(sdb, donor->part(db::PartTable::FrontTire));
    const ByteSpan c = db::part_row(sdb, db::PartTable::TireCompound, rd32(ft, 8)), dc = db::part_row(sdb, db::PartTable::TireCompound, rd32(dft, 8));
    CHECK(!c.empty() && !dc.empty() && c[1] == u8(std::lround(dc[1] * 1.05)));

    // The parts that fit it: the donor's, its own tires among them.
    const std::optional<Bytes> fits = sdb.car_file(u64(car->car_id)), donor_fits = sdb.car_file(u64(donor->car_id));
    CHECK(fits && donor_fits && fits->size() == donor_fits->size() && fits->size() > 8);
    bool own_tire = false;
    if (fits)
        for (size_t o = 8; o + 16 <= fits->size(); o += 16) {
            const u64 code = rd32(ByteSpan(*fits), o) | (u64(rd32(ByteSpan(*fits), o + 4)) << 32);
            if (u32(code >> 32) == u32(db::PartTable::FrontTire) && u32(code) >= 0x40000000u) own_tire = true;
        }
    CHECK(own_tire);

    // A save keeps it by its code, and the menus' pictures stand in with the donor's.
    const save::Codes codes = save::database_codes(sdb);
    CHECK(codes.code(0, "gt4r_test_coupe_26") == u64(car->car_id) && codes.label(u64(car->car_id)) == "gt4r_test_coupe_26");
    const std::optional<db::ModCars::Picture> mini = mods.picture("/piece/car_mini/gt4r_test_coupe_26.img");
    CHECK(mini && mini->disc == "piece/car_mini/impreza_wrx_sti_00.img" && mini->png.empty());
    const std::optional<db::ModCars::Picture> logo = mods.picture("piece/car_logo2/gt4r_test_coupe_26.img");
    CHECK(logo && logo->disc.empty());
    CHECK(!mods.picture("piece/car_mini/impreza_wrx_sti_00.img"));

    // Its engine sounds: the donor's.
    const audio::CarSoundFiles sound = audio::car_sound_files(sdb, "gt4r_test_coupe_26"), donor_sound = audio::car_sound_files(sdb, "impreza_wrx_sti_00");
    CHECK(!sound.engine.empty() && sound.engine == donor_sound.engine && sound.exhaust == donor_sound.exhaust);

    // The race builds it: its weight, power, gears and tires in the spec.
    const sim::Program program = sim::Program::load(*disc);
    sim::Equipment eq;
    CHECK(sim::stock_car(sdb, program, "gt4r_test_coupe_26", eq));
    sim::Spec spec;
    sim::build_spec(sdb, program, eq, 0, spec);
    CHECK(spec.weight_kg() == 1180 && spec.gears() == 6 && spec.drive() == 0);
    CHECK(spec.h(0x1C) != 0); // the tread
    CHECK(spec.b(0x42) == 31); // the drag coefficient
}

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/runtime.h"

#include <algorithm>

namespace {

// The game's menus on the disc with the mods' cars of `list` (the file layer's mods/.cars).
std::unique_ptr<menu::Runtime> menus_with(Disc& d, std::shared_ptr<const std::string> list) {
    menu::Runtime::Config config;
    config.language = "US";
    config.list = [&d](const std::string& folder) {
        std::vector<std::string> out;
        for (const gt3r::Volume::Entry& e : d.volume().entries()) {
            if (e.path.compare(0, folder.size(), folder) != 0) continue;
            const size_t slash = e.path.find('/', folder.size());
            std::string name = e.path.substr(folder.size(), slash == std::string::npos ? slash : slash + 1 - folder.size());
            if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
        }
        return out;
    };
    config.program = [&d] { return d.read_program(); };
    return std::make_unique<menu::Runtime>(
        [&d, list](const std::string& path) -> std::optional<Bytes> {
            if (path == "mods/.cars") return Bytes(list->begin(), list->end());
            const gt3r::Volume::Entry* e = d.volume().find(path);
            return e ? std::optional<Bytes>(d.volume().read(*e)) : std::nullopt;
        },
        config);
}
std::unique_ptr<menu::Runtime> menus_with(Disc& d, std::string list) { return menus_with(d, std::make_shared<const std::string>(std::move(list))); }

} // namespace

// A mod car of Alfa Romeo's at the end of its dealer's list, bought there into the garage.
TEST(mod_car_at_its_dealer) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("no Gran Turismo 4 disc");
    const std::string list = write_car("gt4r_test_alfa_26.ini", "[car]\nlabel = gt4r_test_alfa_26\nname = Alfa Test `26\nmaker = Alfa Romeo\nprice = 23456\ndonor = _147_20_twinspark_02\n");
    std::unique_ptr<menu::Runtime> rt = menus_with(*disc, list);
    auto press = [&](std::initializer_list<menu::Key> keys) {
        for (menu::Key k : keys) rt->key(k), rt->settle();
    };
    auto page = [&] { return rt->current_page() ? rt->current_page()->name : std::string(); };
    auto focus = [&] { return rt->focused() ? rt->focused()->name : std::string(); };
    menu::GameState& game = rt->game();
    game.card.has_game_data = true, game.username = "Player", game.money = 500000;
    rt->boot();
    rt->start_project(rt->first_project());
    rt->settle();
    if (page() == "MovieRoot") press({menu::Key::Start});
    CHECK(rt->finished() && rt->advance());
    rt->settle();
    press({menu::Key::Down, menu::Key::Accept});
    CHECK(rt->finished() && rt->wanted_project() == "gtmode" && rt->advance());
    rt->settle();
    rt->set_focus(*rt->current_page(), rt->current_page()->child("ScrollWindow")->child("clip")->child("space")->child("DealerItaly"));
    press({menu::Key::Accept});
    CHECK(page() == "NationRoot" && focus() == "alfaromeo");
    // The dealer's list: the disc's five, then the mod car.
    const script::Value carlist = rt->vm().lookup("GTmodeProject::NewCarData::alfaromeo_carlist");
    const std::vector<script::Value>* items = carlist.items();
    CHECK(items && items->size() == 6 && items->back().as_string() == "gt4r_test_alfa_26");
    CHECK(rt->vm().lookup("GTmodeProject::NewCarData::alfaromeo_carnum").i == 6);
    press({menu::Key::Accept, menu::Key::Accept});
    CHECK(page() == "BuyCarRoot" && focus() == "CarItem");
    for (int i = 0; i < 5; ++i) press({menu::Key::Right});
    press({menu::Key::Accept, menu::Key::Accept}); // the car, its paint
    CHECK(focus() == "Buy");
    press({menu::Key::Accept, menu::Key::Right, menu::Key::Accept});
    CHECK(page() == "GetCarRoot");
    CHECK(game.garage.size() == 1 && !game.garage.empty() && game.garage[0].car == "gt4r_test_alfa_26");
    press({menu::Key::Accept}); // "ride it now?" yes: paid
    CHECK(game.money == 500000 - 23456 && game.riding_car == 0);
    bool clean = true;
    for (const std::string& line : rt->log())
        if (line.find("script error") != std::string::npos) clean = false, std::printf("  %s\n", line.c_str());
    CHECK(clean);
}

// A mod installed while the game runs (an online download): its car added to the database read already, the cars in before kept.
TEST(mod_car_added_later) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("no Gran Turismo 4 disc");
    auto list = std::make_shared<std::string>(write_car("gt4r_test_coupe_26.ini", kCar));
    db::SpecDb sdb = open_db(*disc, list);
    const size_t before = sdb.cars().size();
    const std::optional<db::CarParts> coupe = db::car_parts(sdb, "gt4r_test_coupe_26");
    CHECK(coupe && !db::car_parts(sdb, "gt4r_test_alfa_26"));
    const db::Table* names = sdb.table("CAR_NAME_american");
    *list += write_car("gt4r_test_alfa_26.ini", "[car]\nlabel = gt4r_test_alfa_26\nname = Alfa Test `26\nprice = 23456\ndonor = _147_20_twinspark_02\n");
    CHECK(sdb.add_mod_cars() == std::vector<std::string>{"gt4r_test_alfa_26"});
    CHECK(sdb.add_mod_cars().empty()); // (once)
    CHECK(sdb.table("CAR_NAME_american") == names); // the same table, rows added
    CHECK(sdb.cars().size() == before + 1);
    const std::optional<db::CarParts> alfa = db::car_parts(sdb, "gt4r_test_alfa_26"), coupe2 = db::car_parts(sdb, "gt4r_test_coupe_26");
    CHECK(alfa && coupe2 && coupe2->parts == coupe->parts && coupe2->car_row == coupe->car_row);
    if (alfa) CHECK(rd32(ByteSpan(alfa->car_row), 8) == 23456);
    bool named = false;
    for (const db::CarEntry& e : sdb.cars())
        if (e.label == "gt4r_test_alfa_26") named = e.name == "Alfa Test `26" && e.paints > 0;
    CHECK(named);
    // As a database made with both at once has it.
    db::SpecDb both = open_db(*disc, *list);
    const std::optional<db::CarParts> alfa2 = db::car_parts(both, "gt4r_test_alfa_26");
    CHECK(alfa && alfa2 && alfa->car_id == alfa2->car_id && alfa->car_row == alfa2->car_row && alfa->parts == alfa2->parts);
    const sim::Program program = sim::Program::load(*disc);
    sim::Equipment eq, eq2;
    CHECK(sim::stock_car(sdb, program, "gt4r_test_alfa_26", eq) && sim::stock_car(both, program, "gt4r_test_alfa_26", eq2));
    sim::Spec spec, spec2;
    sim::build_spec(sdb, program, eq, 0, spec), sim::build_spec(both, program, eq2, 0, spec2);
    CHECK(spec.weight_kg() == spec2.weight_kg() && spec.weight_kg() > 0 && spec.gears() == spec2.gears());

    // The menus: the car joins its maker's dealer list already read.
    auto menus_list = std::make_shared<std::string>();
    std::unique_ptr<menu::Runtime> rt = menus_with(*disc, menus_list);
    auto press = [&](std::initializer_list<menu::Key> keys) {
        for (menu::Key k : keys) rt->key(k), rt->settle();
    };
    menu::GameState& game = rt->game();
    game.card.has_game_data = true, game.username = "Player", game.money = 500000;
    rt->boot();
    rt->start_project(rt->first_project());
    rt->settle();
    if (rt->current_page() && rt->current_page()->name == "MovieRoot") press({menu::Key::Start});
    CHECK(rt->finished() && rt->advance());
    rt->settle();
    press({menu::Key::Down, menu::Key::Accept});
    CHECK(rt->finished() && rt->wanted_project() == "gtmode" && rt->advance());
    rt->settle();
    rt->set_focus(*rt->current_page(), rt->current_page()->child("ScrollWindow")->child("clip")->child("space")->child("DealerItaly"));
    press({menu::Key::Accept});
    const std::vector<script::Value>* items = rt->vm().lookup("GTmodeProject::NewCarData::alfaromeo_carlist").items();
    CHECK(items && items->size() == 5);
    *menus_list = *list;
    CHECK(menu::refresh_mod_cars(*rt).size() == 2);
    items = rt->vm().lookup("GTmodeProject::NewCarData::alfaromeo_carlist").items();
    CHECK(items && items->size() == 6 && items->back().as_string() == "gt4r_test_alfa_26");
    CHECK(rt->vm().lookup("GTmodeProject::NewCarData::alfaromeo_carnum").i == 6);
    CHECK(menu::refresh_mod_cars(*rt).empty());
}


// A save with a mod car whose mod was removed: the place is kept as it was (vacant to the pages), the car driven too, and the
// car is back with its mod.
TEST(mod_car_save_without_its_mod) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("no Gran Turismo 4 disc");
    Flow h(*disc);
    CHECK(h.gt_mode_with_a_car());
    const std::string list = write_car("gt4r_test_alfa_26.ini", "[car]\nlabel = gt4r_test_alfa_26\nname = Alfa Test `26\nprice = 23456\ndonor = _147_20_twinspark_02\n");
    db::SpecDb with = open_db(*disc, list), without = open_db(*disc, "");
    const save::Codes mod_codes = save::database_codes(with), plain_codes = save::database_codes(without);
    menu::GameState state = h.rt->game();
    CHECK(state.garage.size() == 1 && state.garage[0].record);
    menu::GarageEntry e = state.garage[0];
    e.car = "gt4r_test_alfa_26", e.paint = 1, e.odometer = 1234;
    state.garage.push_back(e);
    state.riding_car = 1;
    const Bytes saved = save::write_game(state, mod_codes);

    // Without the mod: the place vacant to the pages, its code kept; no car driven.
    menu::GameState lacking;
    std::vector<int> wanted; // (the garage file's records, as the card has them)
    CHECK(save::read_game(saved, lacking, plain_codes, &wanted));
    for (int i : wanted) lacking.garage[size_t(i)].record = state.garage[size_t(i)].record;
    CHECK(lacking.garage.size() == 2 && lacking.garage[0].used && lacking.garage[1].missing());
    CHECK(lacking.garage[1].missing_code == mod_codes.code(0, "gt4r_test_alfa_26"));
    CHECK(lacking.riding_car == -1 && lacking.riding_missing == 1 && lacking.garage_count() == 1);
    // Saved again so: the same game.
    const Bytes again = save::write_game(lacking, plain_codes);
    CHECK(again == saved);
    // The mod back: the car and its driving.
    menu::GameState back;
    CHECK(save::read_game(again, back, mod_codes));
    CHECK(back.garage.size() == 2 && back.garage[1].used && back.garage[1].car == "gt4r_test_alfa_26" && back.garage[1].odometer == 1234);
    CHECK(back.riding_car == 1 && back.garage[1].record && back.garage[1].record->bytes == state.garage[1].record->bytes);
    // Another car driven meanwhile: that one is driven, the missing car's place still kept.
    lacking.riding_car = 0;
    menu::GameState other;
    CHECK(save::read_game(save::write_game(lacking, plain_codes), other, mod_codes));
    CHECK(other.riding_car == 0 && other.garage[1].car == "gt4r_test_alfa_26");

    // The menus with the game: the garage shows the one car; the pages run clean.
    h.rt->game() = lacking;
    h.rt->game().riding_car = -1;
    h.map_focus("Home");
    h.press({menu::Key::Accept});
    CHECK(h.page() == "HomeRoot");
    h.focus_on("Garage");
    h.press({menu::Key::Accept});
    CHECK(h.page() == "GarageRoot");
    std::printf("  garage: %s\n", h.text_of("cars").c_str());
    const std::string count = h.text_of("cars"); // (the US disc's "1/1 cars", Europe's "1 of 1 cars")
    CHECK(count.size() > 2 && count[0] == '1' && count.find('1', 1) != std::string::npos);
    CHECK(h.clean());
}

#include "gt4r/formats/db/race_events.h"

// A mod car in Gran Turismo mode's pools of opponents ([opponent] races): at the end of each race's pool it names, as sold.
TEST(mod_car_opponent) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("no Gran Turismo 4 disc");
    const std::string list = write_car("gt4r_test_alfa_26.ini", "[car]\nlabel = gt4r_test_alfa_26\nname = Alfa Test `26\ndonor = _147_20_twinspark_02\n"
                                                                "[opponent]\nraces = am_sunday_*, no_such_race\n");
    db::SpecDb plain = open_db(*disc, ""), sdb = open_db(*disc, list);
    gt3r::Volume* vol = &disc->volume();
    auto read = [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    };
    const db::RaceEvents before(plain, read), after(sdb, read);
    int joined = 0;
    for (const std::string& label : after.labels()) {
        const std::optional<db::RaceEvent> e = after.event(label), e0 = before.event(label);
        if (!e || !e0) continue;
        const std::vector<db::Opponent> p = after.pool(*e), p0 = before.pool(*e0);
        const bool sunday = label.rfind("am_sunday_", 0) == 0;
        if (!sunday || p0.empty()) {
            CHECK(p.size() == p0.size());
            continue;
        }
        ++joined;
        CHECK(p.size() == p0.size() + 1);
        if (p.empty()) continue;
        const db::Opponent& o = p.back();
        // (its parts as sold: the set its car row names, the donor's df_pt_sim_ made its own)
        CHECK(o.car == "gt4r_test_alfa_26" && o.parts == "df_pt_sim_gt4r_test_alfa_26" && o.paint == -1);
    }
    CHECK(joined > 0);
    std::printf("  joined %d pools\n", joined);
    bool warned = false;
    for (const db::ModCarStatus& s : sdb.mod_cars().statuses()) warned = warned || !s.warnings.empty();
    CHECK(!warned); // (one name matched)
    // The race draws it as it draws the disc's cars.
    const std::optional<db::RaceEvent> sunday = after.event("am_sunday_0001");
    CHECK(sunday);
    if (sunday) {
        const size_t n = after.pool(*sunday).size();
        float x = 0.999f; // the last entry first
        const std::vector<db::Opponent> drawn = after.draw(*sunday, 1, [&] { return x; });
        CHECK(drawn.size() == 1 && drawn[0].car == "gt4r_test_alfa_26" && n > 1);
    }
}

// An electric drive is refused, with the reason (the game has none of its own).
TEST(mod_car_electric_refused) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("no Gran Turismo 4 disc");
    db::SpecDb sdb = open_db(*disc, write_car("gt4r_test_ev_26.ini", "[car]\nlabel = gt4r_test_ev_26\nname = EV `26\ndonor = prius_g_02\n[powertrain]\ntype = electric\n"));
    const std::vector<db::ModCarStatus>& st = sdb.mod_cars().statuses();
    CHECK(st.size() == 1 && !st[0].loaded() && st[0].error.find("no electric drive") != std::string::npos);
    CHECK(!db::car_parts(sdb, "gt4r_test_ev_26"));
}

#include "gt4r/race/mod_car_body.h"
#include "formats/model/gltf.h"
#include "formats/model/gltf_export.h"

// A mod car's body: the borrowed one stretched to its dimensions, or one of its own (a .glb) with its [paints].
TEST(mod_car_body) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("no Gran Turismo 4 disc");
    // A body of its own: a triangle as the loader reads it.
    gt3r::scene::Model tri;
    gt3r::scene::Mesh mesh;
    mesh.name = "body";
    for (int i = 0; i < 3; ++i) {
        gt3r::scene::Vertex v;
        v.pos[0] = float(i == 1), v.pos[1] = float(i == 2), v.normal[2] = 1;
        mesh.vertices.push_back(v);
        mesh.indices.push_back(u32(i));
    }
    gt3r::scene::Primitive p;
    p.index_count = 3;
    mesh.primitives.push_back(p);
    tri.meshes.push_back(mesh);
    const Bytes glb = gt3r::export_glb(tri);
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r_test_mod_cars";
    std::filesystem::create_directories(dir / "models");
    gt3r::write_host_file((dir / "models" / "own.glb").string(), ByteSpan(glb));
    CHECK(!gt3r::load_glb(glb).meshes.empty());

    std::string list = write_car("gt4r_test_own_26.ini", "[car]\nlabel = gt4r_test_own_26\nname = Own `26\ndonor = _350z_03\n"
                                                         "[model]\nfile = models/own.glb\nlower_mm = 10\n"
                                                         "[paints]\nRed = #FF0000\nPearl White = #E7E5DF\nBlue = #0000FF\n");
    list += write_car("gt4r_test_long_26.ini", "[car]\nlabel = gt4r_test_long_26\nname = Long `26\ndonor = impreza_wrx_sti_00\n"
                                               "[body]\nlength_mm = 5200\nwheelbase_mm = 3100\nheight_mm = 1250\n");
    list += write_car("gt4r_test_same_26.ini", "[car]\nlabel = gt4r_test_same_26\nname = Same `26\ndonor = impreza_wrx_sti_00\n"
                                               "[body]\nlength_mm = 5200\n[paints]\nRed = #FF0000\n");
    list += write_car("gt4r_test_unfit_26.ini", "[car]\nlabel = gt4r_test_unfit_26\nname = Unfit `26\ndonor = impreza_wrx_sti_00\nfit_body = no\n"
                                                "[body]\nlength_mm = 5200\n");
    db::SpecDb sdb = open_db(*disc, list);
    for (const db::ModCarStatus& s : sdb.mod_cars().statuses()) CHECK(s.loaded());

    // [paints]: the car's paint rows, their names and colors.
    bool listed = false;
    for (const db::CarEntry& e : sdb.cars())
        if (e.label == "gt4r_test_own_26") listed = e.paints == 3;
    CHECK(listed);
    const db::Table* names = sdb.table("VARIATIONamerican");
    const db::Strings* pool = sdb.strings("american_StrDB");
    const db::Table* link = sdb.table("CAR_VARIATION_american");
    const std::optional<u32> id = sdb.labels("GENERIC_CAR")->id("gt4r_test_own_26");
    CHECK(names && pool && link && id);
    if (names && pool && link && id) {
        size_t first = 0;
        const size_t n = names->rows(rd32(link->row(*id), 0), first);
        CHECK(n == 3);
        if (n == 3) {
            const ByteSpan r = names->row_at(first + 1);
            CHECK(rd32(r, 4) == 2 && pool->at(rd32(r, 0x0C)) == "Pearl White" && r[0x24] == 0xE7 && r[0x25] == 0xE5 && r[0x26] == 0xDF);
        }
    }
    // The body: its own, in the paint chosen.
    const std::optional<race::ModCarBody> own = race::mod_car_body(sdb, "gt4r_test_own_26", false, 0, 1);
    CHECK(own && own->glb && *own->glb == glb && !own->fit && std::fabs(own->lower - 0.01f) < 1e-6f);
    if (own && own->paint) CHECK(std::fabs((*own->paint)[0] - 0xE7 / 255.0f) < 1e-6f);
    // Stretched: from the donor's dimensions to the car's.
    const std::optional<race::ModCarBody> longer = race::mod_car_body(sdb, "gt4r_test_long_26", false, 0, 0);
    CHECK(longer && !longer->glb && longer->fit);
    if (longer && longer->fit) {
        CHECK(longer->fit->to_length == 5200 && longer->fit->to_wheelbase == 3100 && longer->fit->to_height == 1250);
        CHECK(std::fabs(longer->fit->to_width - longer->fit->from_width) < 1); // (the width the donor's)
        CHECK(longer->fit->from_length > 4000 && longer->fit->from_length < 4800);
    }
    CHECK(!race::mod_car_body(sdb, "gt4r_test_unfit_26", false, 0, 0));
    CHECK(!race::mod_car_body(sdb, "impreza_wrx_sti_00", false, 0, 0));
    // [paints] without a body of its own: warned, the body car's paints kept.
    bool warned = false;
    for (const db::ModCarStatus& s : sdb.mod_cars().statuses())
        if (s.label == "gt4r_test_same_26")
            for (const std::string& w : s.warnings) warned = warned || w.find("[paints]") != std::string::npos;
    CHECK(warned);
}

// A mod switched off while the game runs: its car leaves the menus' database, its dealer's list and the pools it joined, the
// garage keeps its place as a game loaded without the mod keeps it (the same save written), and it is back with its mod.
TEST(mod_car_removed_later) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("no Gran Turismo 4 disc");
    const std::string with = write_car("gt4r_test_alfa_26.ini", "[car]\nlabel = gt4r_test_alfa_26\nname = Alfa Test `26\nmaker = Alfa Romeo\nprice = 23456\n"
                                                                "donor = _147_20_twinspark_02\n[opponent]\nraces = am_sunday_0001\n");
    auto list = std::make_shared<std::string>(with);
    std::unique_ptr<menu::Runtime> rt = menus_with(*disc, list);
    auto press = [&](std::initializer_list<menu::Key> keys) {
        for (menu::Key k : keys) rt->key(k), rt->settle();
    };
    menu::GameState& game = rt->game();
    game.card.has_game_data = true, game.username = "Player", game.money = 500000;
    rt->boot();
    rt->start_project(rt->first_project());
    rt->settle();
    if (rt->current_page() && rt->current_page()->name == "MovieRoot") press({menu::Key::Start});
    CHECK(rt->finished() && rt->advance());
    rt->settle();
    press({menu::Key::Down, menu::Key::Accept});
    CHECK(rt->finished() && rt->wanted_project() == "gtmode" && rt->advance());
    rt->settle();
    rt->set_focus(*rt->current_page(), rt->current_page()->child("ScrollWindow")->child("clip")->child("space")->child("DealerItaly"));
    press({menu::Key::Accept});
    const auto carlist = [&] { return rt->vm().lookup("GTmodeProject::NewCarData::alfaromeo_carlist").items(); };
    CHECK(carlist() && carlist()->size() == 6);
    menu::Database& d = menu::natives_of(*rt)->db();
    CHECK(d.car("gt4r_test_alfa_26") != nullptr);
    // The garage: the mod car, driven.
    menu::GarageEntry e;
    e.car = "gt4r_test_alfa_26", e.used = true, e.paint = 1, e.odometer = 1234, e.day = game.today, e.power = 15000, e.weight = 1300;
    game.garage = {e};
    game.riding_car = 0;
    const save::Codes mod_codes = save::database_codes(*d.db);
    const Bytes saved = save::write_game(game, mod_codes);
    const db::RaceEvents events(*d.db, [&](const std::string& path) { return rt->reader()(path); });
    const auto pool_size = [&] {
        const std::optional<db::RaceEvent> s = events.event("am_sunday_0001");
        return s ? events.pool(*s).size() : size_t(0);
    };
    const size_t with_pool = pool_size();

    *list = "";
    menu::refresh_mod_cars(*rt);
    CHECK(d.car("gt4r_test_alfa_26") == nullptr && !d.db->labels("GENERIC_CAR")->id("gt4r_test_alfa_26"));
    CHECK(std::none_of(d.cars.begin(), d.cars.end(), [](const db::CarEntry& c) { return c.label == "gt4r_test_alfa_26"; }));
    CHECK(carlist() && carlist()->size() == 5 && rt->vm().lookup("GTmodeProject::NewCarData::alfaromeo_carnum").i == 5);
    CHECK(pool_size() + 1 == with_pool);
    CHECK(game.garage.size() == 1 && game.garage[0].missing() && game.riding_car == -1 && game.riding_missing == 0);
    const Bytes without = save::write_game(game, save::database_codes(*d.db));
    CHECK(without == saved); // (as a game loaded without the mod writes it)

    *list = with;
    CHECK(menu::refresh_mod_cars(*rt) == std::vector<std::string>{"gt4r_test_alfa_26"});
    CHECK(carlist() && carlist()->size() == 6 && pool_size() == with_pool);
    CHECK(game.garage.size() == 1 && game.garage[0].car == "gt4r_test_alfa_26" && game.garage[0].used && game.riding_car == 0);
    CHECK(save::write_game(game, save::database_codes(*d.db)) == saved);
    bool clean = true;
    for (const std::string& line : rt->log())
        if (line.find("script error") != std::string::npos) clean = false, std::printf("  %s\n", line.c_str());
    CHECK(clean);
}
