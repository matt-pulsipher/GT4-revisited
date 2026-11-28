// A disc car exported whole as a mod car (formats/db/car_export.*): written from one disc, loaded on another that lacks it
// (its rows from the records file) and on its own (made from the disc's car). Needs the European and the American discs in
// GT4R_DISC; skips without them.

#include "test_framework.h"

#include "gt4r/formats/db/car_export.h"
#include "gt4r/formats/db/car_parts.h"
#include "gt4r/formats/db/mod_cars.h"
#include "gt4r/sim/car_spec.h"

#include "base/file.h"

#include <cmath>
#include <filesystem>

using namespace gt4r;

namespace {

Disc* disc_of(const std::string& region) {
    for (const auto& d : test::discs())
        if (d->info().game == Game::GT4 && d->info().region == region) return d.get();
    return nullptr;
}

std::function<std::optional<Bytes>(const std::string&)> reader(Disc& d) {
    gt3r::Volume* vol = &d.volume();
    return [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    };
}

// The disc's database with the mods' files `list` names; the files of `files_dir` over the disc's (the mods' files/).
std::unique_ptr<db::SpecDb> db_with(Disc& d, std::string list, std::string files_dir = {}) {
    auto read = reader(d);
    gt3r::Volume* vol = &d.volume();
    return std::make_unique<db::SpecDb>(
        [read, list, files_dir](const std::string& path) -> std::optional<Bytes> {
            if (path == "mods/.cars") return Bytes(list.begin(), list.end());
            if (!files_dir.empty())
                if (std::optional<Bytes> f = gt3r::read_host_file(files_dir + "/" + path)) return f;
            return read(path);
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
}

} // namespace

TEST(car_export) {
    Disc* eu = disc_of("uk");
    Disc* us = disc_of("us");
    if (!eu || !us) SKIP("needs the European and American Gran Turismo 4 discs");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "gt4r_test_car_export";
    fs::remove_all(dir);
    std::unique_ptr<db::SpecDb> source = db_with(*eu, "");
    // The Vauxhall VX220 is the European disc's alone.
    std::string why;
    const std::optional<db::CarRecords> rec = db::collect_car_records(*source, "vauxhall_vx220_00", &why);
    CHECK(rec && rec->rows.size() > 20 && rec->car_file.size() > 8);
    if (rec) {
        const std::optional<db::CarRecords> back = db::CarRecords::parse(rec->serialize());
        CHECK(back && back->rows.size() == rec->rows.size() && back->texts.size() == rec->texts.size() && back->car_file == rec->car_file);
    }
    const std::optional<db::CarExport> ex = db::export_car(*source, reader(*eu), "vauxhall_vx220_00", (dir / "cars_sles").string(), eu->info().key, "Europe", &why);
    CHECK(ex);
    if (!ex) return std::printf("  %s\n", why.c_str()), void();
    CHECK(ex->label == "vauxhall_vx220_00_" + db::export_suffix(eu->info().key));
    for (const std::string& f : ex->files) std::printf("  wrote %s\n", f.c_str());
    const std::string ini = (dir / "cars_sles" / "cars" / (ex->label + ".ini")).string();
    const std::string list = "cars_sles\t" + ini + "\n";

    // On the American disc: made of its records.
    std::unique_ptr<db::SpecDb> target = db_with(*us, list, (dir / "cars_sles" / "files").string());
    const std::vector<db::ModCarStatus>& st = target->mod_cars().statuses();
    CHECK(st.size() == 1 && st[0].loaded());
    if (!st.empty() && !st[0].loaded()) std::printf("  %s\n", st[0].error.c_str());
    const std::optional<db::CarParts> car = db::car_parts(*target, ex->label);
    CHECK(car);
    bool listed = false;
    std::string model;
    for (const db::CarEntry& e : target->cars())
        if (e.label == ex->label) listed = !e.name.empty() && e.paints > 0, model = e.model;
    std::string source_model;
    for (const db::CarEntry& e : source->cars())
        if (e.label == "vauxhall_vx220_00") source_model = e.model;
    CHECK(listed && !source_model.empty() && model == source_model + "_" + db::export_suffix(eu->info().key)); // (renamed: the files/ the export wrote)
    std::printf("  model %s\n", model.c_str());
    CHECK(gt3r::read_host_file((dir / "cars_sles" / "files" / "car" / "lod" / model).string()).has_value());
    // The race builds it as the European disc builds the car.
    const sim::Program us_program = sim::Program::load(*us), eu_program = sim::Program::load(*eu);
    sim::Equipment eq, eq0;
    CHECK(sim::stock_car(*target, us_program, ex->label, eq) && sim::stock_car(*source, eu_program, "vauxhall_vx220_00", eq0));
    sim::Spec spec, spec0;
    sim::build_spec(*target, us_program, eq, 0, spec), sim::build_spec(*source, eu_program, eq0, 0, spec0);
    CHECK(spec.weight_kg() == spec0.weight_kg() && spec.weight_kg() > 0 && spec.gears() == spec0.gears() && spec.drive() == spec0.drive());
    const sim::SpecFigures f = sim::spec_figures(spec), f0 = sim::spec_figures(spec0);
    std::printf("  %d kg %.1f PS (Europe %d kg %.1f PS)\n", spec.weight_kg(), double(f.ps), spec0.weight_kg(), double(f0.ps));
    CHECK(std::fabs(f.ps - f0.ps) < 0.01f && f.tire_radius[0] == f0.tire_radius[0]);

    // On its own disc: made from the disc's car (a copy under the new label).
    std::unique_ptr<db::SpecDb> own = db_with(*eu, list);
    const std::vector<db::ModCarStatus>& st2 = own->mod_cars().statuses();
    CHECK(st2.size() == 1 && st2[0].loaded() && !own->mod_cars().def(ex->label)->imported);
    fs::remove_all(dir);
}
