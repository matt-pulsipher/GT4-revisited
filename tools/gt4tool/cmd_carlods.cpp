// gt4tool car-lods: every race car body's levels of detail (docs/formats/model.md "The second entry point").
//
//   gt4tool car-lods <disc> [folder=car/lod] [model...]
//       per car file: the body program's level switch (0x07: centre, size, levels) and, per level, the shapes the
//       body draws there without its wheels' callbacks (11-18, 20-39: the race view draws those wheels as items of
//       their own), as triangles and a box. A level with under a tenth of level 0's triangles is listed as "MISSING"
//       (the 1886 Benz's levels 1 and 2: nothing, and its wheels alone).

#include "gt4r/disc/disc.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/race/car_lods.h"

#include <cstdio>
#include <string>
#include <vector>

namespace gt4r::tool {

int cmd_carlods(Disc& disc, const std::vector<std::string>& args) {
    std::string folder = "car/lod";
    std::vector<std::string> only;
    for (const std::string& a : args) {
        if (a.find('/') != std::string::npos) folder = a;
        else only.push_back(a);
    }
    std::vector<std::string> names = only;
    if (names.empty())
        for (const std::string& n : disc.volume().list_dir(folder))
            if (!n.empty() && n.find('.') == std::string::npos && n.back() != '/') names.push_back(n);
    int files = 0, flagged = 0;
    for (const std::string& name : names) {
        const gt3r::Volume::Entry* e = disc.volume().find(folder + "/" + name);
        if (!e) {
            std::printf("%s: no such file\n", name.c_str());
            continue;
        }
        const Bytes f = disc.volume().read(*e);
        if (!car::CarFile::is_car(f)) continue;
        ++files;
        try {
            const race::BodyLods l = race::body_lods(f);
            std::printf("%-10s switch %s size %.3f centre (%.2f %.2f %.2f) levels %d:", name.c_str(), l.has_switch ? "yes" : "no", l.size,
                        l.centre[0], l.centre[1], l.centre[2], l.levels);
            for (const race::BodyLods::Level& v : l.level)
                std::printf("  [%d] %5d tris %.2fx%.2fx%.2f%s", v.index, v.triangles, v.extent[0], v.extent[1], v.extent[2], v.missing ? " MISSING" : "");
            std::printf("\n");
            if (!l.has_switch || l.switches.size() > 1)
                for (const race::BodyLods::Switch& w : l.switches)
                    std::printf("           0x07 at %x: size %.3f centre (%.2f %.2f %.2f) levels %d\n", w.at, w.size, w.centre[0], w.centre[1], w.centre[2], w.levels);
            flagged += l.any_missing() ? 1 : 0;
        } catch (const std::exception& ex) {
            std::printf("%-10s error: %s\n", name.c_str(), ex.what());
        }
    }
    std::printf("%d car files, %d with a level that draws no body\n", files, flagged);
    return 0;
}

} // namespace gt4r::tool
