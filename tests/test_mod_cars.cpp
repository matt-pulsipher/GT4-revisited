// Mod cars (formats/db/mod_cars.*): the car file, then a car made of a disc car's rows on the discs of GT4R_DISC.

#include "test_framework.h"

#include "gt4r/formats/db/car_parts.h"
#include "gt4r/formats/db/mod_cars.h"
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

db::SpecDb open_db(Disc& disc, std::string list) {
    gt3r::Volume* vol = &disc.volume();
    return db::SpecDb(
        [vol, list](const std::string& path) -> std::optional<Bytes> {
            if (path == "mods/.cars") return Bytes(list.begin(), list.end());
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
}

Disc* gt4_disc() {
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4) return d;
    return nullptr;
}

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
    sdb.set_makers({{1, "mazda"}, {2, "toyota"}});
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
    CHECK(car->car_row[0x0F] == 1 && car->car_row[0x11] % 100 == 2);
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
