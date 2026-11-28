// gt4tool modcar <disc> <folder> [disc:<label> ...]: the mod cars of a mods folder (or of one mod) as the game loads them,
// each with what is wrong in its file, and the figures the race builds of it next to the disc cars named.

#include "gt4r/disc/disc.h"
#include "gt4r/formats/db/mod_cars.h"
#include "gt4r/sim/car_spec.h"

#include "base/file.h"
#include "vfs/vfs.h"

#include <cstdio>
#include <string>
#include <vector>

namespace gt4r::tool {

namespace {

void figures(db::SpecDb& sdb, const sim::Program& program, const std::string& label, const std::string& name) {
    sim::Equipment eq;
    if (!sim::stock_car(sdb, program, label, eq)) return void(std::printf("  %-32s not built\n", label.c_str()));
    sim::Spec s;
    sim::build_spec(sdb, program, eq, 0, s);
    const sim::SpecFigures f = sim::spec_figures(s);
    std::printf("  %-32s %-28s %5d kg %6.1f PS @ %5d  %5.1f kgfm @ %5d  limit %5d  %d gears  tires %.3f / %.3f m\n", label.c_str(), name.c_str(), s.weight_kg(),
                double(f.ps), f.power_rpm, double(f.kgfm), f.torque_rpm, f.rev_limit_rpm, s.gears(), double(f.tire_radius[0]), double(f.tire_radius[1]));
}

} // namespace

int cmd_modcar(Disc& disc, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::fprintf(stderr, "usage: gt4tool modcar <disc> <mods folder | a mod's folder> [disc:<label> ...]\n");
        return 2;
    }
    // The car files: <folder>/cars/*.ini, else <folder>/<mod>/cars/*.ini.
    std::string list;
    std::string root = args[0];
    while (root.size() > 1 && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    auto add_mod = [&](const std::string& dir, const std::string& id) {
        for (const std::string& f : gt3r::list_host_dir(dir + "/cars"))
            if (f.size() > 4 && f.compare(f.size() - 4, 4, ".ini") == 0) list += id + "\t" + dir + "/cars/" + f + "\n";
    };
    add_mod(root, root.substr(root.find_last_of("/\\") + 1));
    if (list.empty())
        for (const std::string& m : gt3r::list_host_dir(root)) add_mod(root + "/" + m, m);
    gt3r::Volume* vol = &disc.volume();
    db::SpecDb sdb(
        [vol, list](const std::string& path) -> std::optional<Bytes> {
            if (path == "mods/.cars") return Bytes(list.begin(), list.end());
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
    const db::ModCars& mods = sdb.mod_cars();
    if (mods.empty()) std::printf("no car files under %s\n", root.c_str());
    const sim::Program program = sim::Program::load(disc);
    int bad = 0;
    for (const db::ModCarStatus& s : mods.statuses()) {
        std::printf("%s (%s, %s)\n", s.label.c_str(), s.mod.c_str(), s.file.c_str());
        for (const std::string& w : s.warnings) std::printf("  warning: %s\n", w.c_str());
        if (!s.loaded()) {
            std::printf("  not loaded: %s\n", s.error.c_str());
            ++bad;
            continue;
        }
        std::printf("  donor %s\n", s.donor.c_str());
        figures(sdb, program, s.label, s.name);
        figures(sdb, program, s.donor, "(its donor)");
    }
    for (size_t i = 1; i < args.size(); ++i)
        if (args[i].rfind("disc:", 0) == 0) figures(sdb, program, args[i].substr(5), "");
    return bad ? 1 : 0;
}

} // namespace gt4r::tool
