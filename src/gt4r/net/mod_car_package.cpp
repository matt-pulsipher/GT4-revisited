#include "gt4r/net/mod_car_package.h"

#include "gt4r/formats/db/mod_cars.h"

#include "base/file.h"
#include "formats/model/gltf.h"
#include "formats/png_reader.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace gt4r::net {

namespace {

bool fail(std::string* error, const std::string& m) {
    if (error) *error = m;
    return false;
}

std::string lower_ext(const std::string& p) {
    const size_t dot = p.rfind('.');
    std::string e = dot == std::string::npos ? std::string() : p.substr(dot + 1);
    for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

// The files of a car file a package carries, as written (paths from the mod's folder): its pictures and its bodies of its own.
std::vector<std::string> named_files(const db::ModCarDef& d) {
    std::vector<std::string> out;
    for (const std::string* s : {&d.thumb, &d.picture, &d.logo, &d.model_file, &d.model_menu, &d.model_race, &d.model_time[0], &d.model_time[1], &d.model_time[2]})
        if (!s->empty() && std::find(out.begin(), out.end(), *s) == out.end()) out.push_back(*s);
    return out;
}

template <class T> bool in_range(const std::optional<T>& v, double lo, double hi) { return !v || (double(*v) >= lo && double(*v) <= hi); }

std::optional<db::ModCarDef> definition(const gt3r::game::ModPackage& p, std::string* error) {
    const gt3r::game::ModPackage::File* ini = nullptr;
    for (const gt3r::game::ModPackage::File& f : p.files)
        if (lower_ext(f.path) == "ini") {
            if (ini) {
                fail(error, "two car files");
                return std::nullopt;
            }
            ini = &f;
        }
    if (!ini) {
        fail(error, "no car file");
        return std::nullopt;
    }
    std::string why;
    std::optional<db::ModCarDef> d = db::parse_mod_car(std::string_view(reinterpret_cast<const char*>(ini->data.data()), ini->data.size()), &why);
    if (!d) fail(error, "its car file: " + why);
    else if (ini->path != "cars/" + d->label + ".ini") {
        fail(error, "its car file is not cars/" + d->label + ".ini");
        return std::nullopt;
    }
    return d;
}

} // namespace

std::optional<Bytes> build_package(db::SpecDb& db, const std::string& label, std::string* error) {
    const db::ModCarDef* d = db.mod_cars().def(label);
    if (!d) {
        fail(error, label + " is not a mod car");
        return std::nullopt;
    }
    if (!d->records.empty()) { // (its rows and models are a disc's: they stay with the player who owns it)
        fail(error, label + " is a car exported from a disc: it is not sent");
        return std::nullopt;
    }
    const std::string root = d->folder();
    gt3r::game::ModPackage p;
    p.label = label;
    const std::optional<Bytes> ini = gt3r::read_host_file(d->file);
    if (!ini) {
        fail(error, "its car file is not read");
        return std::nullopt;
    }
    p.files.push_back({"cars/" + label + ".ini", *ini});
    for (const std::string& f : named_files(*d)) {
        if (!gt3r::game::safe_package_path(f)) {
            fail(error, "the file '" + f + "' is not one a package can hold");
            return std::nullopt;
        }
        const std::optional<Bytes> data = gt3r::read_host_file(root + f);
        if (!data) {
            fail(error, "the file '" + f + "' is not read");
            return std::nullopt;
        }
        p.files.push_back({f, *data});
    }
    Bytes blob = gt3r::game::serialize_mod_package(p);
    if (blob.size() > gt3r::game::kMaxPackageBytes) {
        fail(error, "the car's files are more than 32 MB");
        return std::nullopt;
    }
    return blob;
}

std::string package_donor(const gt3r::game::ModPackage& p) {
    const std::optional<db::ModCarDef> d = definition(p, nullptr);
    return d ? d->donor : std::string();
}

bool validate_package(const gt3r::game::ModPackage& p, db::SpecDb& db, const std::string& expected_label, std::string* error) {
    if (p.label != expected_label) return fail(error, "the package is " + p.label + "'s, not " + expected_label + "'s");
    const std::optional<db::ModCarDef> d = definition(p, error);
    if (!d) return false;
    if (d->label != p.label) return fail(error, "its car file names " + d->label);
    if (!d->records.empty()) return fail(error, "it is a car exported from a disc");
    const db::Labels* cars = db.labels("GENERIC_CAR");
    if (!cars || !cars->id(d->donor) || db.mod_cars().def(d->donor)) return fail(error, "its donor " + d->donor + " is not a car of this disc");
    // Values a car can have (a file is written by hand; these bound what the race is given).
    const std::pair<bool, const char*> ranges[] = {
        {in_range(d->weight_kg, 300, 5000), "weight"},
        {in_range(d->length_mm, 2000, 7000) && in_range(d->width_mm, 1200, 2700) && in_range(d->height_mm, 800, 2600), "dimensions"},
        {in_range(d->wheelbase_mm, 1500, 4500) && in_range(d->front_track_mm, 1000, 2400) && in_range(d->rear_track_mm, 1000, 2400), "wheelbase or track"},
        {in_range(d->front_weight_pct, 20, 80), "weight distribution"},
        {in_range(d->cd, 0.1, 1.5) && in_range(d->frontal_area_m2, 0.8, 6.0), "aerodynamics"},
        {in_range(d->ride_height_mm, 30, 400), "ride height"},
        {in_range(d->power_kw, 1, 1500) && in_range(d->torque_nm, 1, 3000) && in_range(d->rated_kw, 1, 1500), "power or torque"},
        {in_range(d->max_rpm, 1000, 25000) && in_range(d->idle_rpm, 0, 3000), "engine speed"},
        {in_range(d->output_pct, 50, 200), "output factor"},
        {in_range(d->front_torque_pct, 0, 100), "torque split"},
        {in_range(d->final_drive, 1, 15) && in_range(d->reverse, 0.5, 8), "final drive"},
        {in_range(d->grip_pct, 50, 150), "tire grip"},
        {d->pokes.size() <= 64, "raw values"},
        {std::fabs(d->model_lower_mm) <= 500, "body offset"},
    };
    for (const auto& [ok, what] : ranges)
        if (!ok) return fail(error, std::string("its ") + what + " is not a car's");
    for (float g : d->gears)
        if (!(g >= 0.2f && g <= 20.0f)) return fail(error, "its gears are not a car's");
    for (const auto& [rpm, nm] : d->curve)
        if (!(rpm >= 0 && rpm <= 25000 && nm >= 0 && nm <= 3000)) return fail(error, "its torque curve is not a car's");
    // Its other files: exactly the ones it names.
    std::set<std::string> named;
    for (const std::string& f : named_files(*d)) {
        if (!gt3r::game::safe_package_path(f)) return fail(error, "it names the file '" + f + "'");
        named.insert(f);
    }
    std::set<std::string> held;
    for (const gt3r::game::ModPackage::File& f : p.files) {
        const std::string e = lower_ext(f.path);
        if (e == "ini") continue;
        if (!named.count(f.path)) return fail(error, "it holds the file '" + f.path + "' it does not name");
        if (e == "png") {
            const std::optional<std::pair<u32, u32>> size = gt3r::png_size(f.data);
            if (!size || size->first == 0 || size->second == 0 || size->first > 2048 || size->second > 2048) return fail(error, f.path + " is not a picture of at most 2048 a side");
            try {
                gt3r::decode_png(f.data, 2048);
            } catch (const std::exception& ex) {
                return fail(error, f.path + ": " + ex.what());
            }
        } else if (e == "glb") {
            // A body: one the loader reads whole, its pictures at most 2048 a side.
            try {
                gt3r::GlbOptions o;
                o.max_texture_dim = 2048;
                if (gt3r::load_glb(f.data, o).meshes.empty()) return fail(error, f.path + " has nothing to draw");
            } catch (const std::exception& ex) {
                return fail(error, f.path + ": " + ex.what());
            }
        } else {
            return fail(error, "it holds the file '" + f.path + "'");
        }
        held.insert(f.path);
    }
    if (held != named) return fail(error, "files it names are missing");
    return true;
}

} // namespace gt4r::net
