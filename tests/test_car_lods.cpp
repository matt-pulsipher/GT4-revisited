// Every race car body's levels of detail on the US disc (race/car_lods.h): what each level of the body program's
// switch draws without the wheels' callbacks. docs/formats/model.md "The second entry point".

#include "test_framework.h"

#include "gt4r/formats/model/car_model.h"
#include "gt4r/race/car_lods.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace gt4r;

TEST(disc_car_body_levels) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    bool ran = false;
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        int files = 0, switches = 0, wheel_faces = 0, first = 0;
        std::string missing;
        for (const std::string& name : disc->volume().list_dir("car/lod")) {
            if (name.empty() || name.find('.') != std::string::npos || name.back() == '/') continue;
            const gt3r::Volume::Entry* e = disc->volume().find("car/lod/" + name);
            if (!e) continue;
            const Bytes f = disc->volume().read(*e);
            if (!car::CarFile::is_car(f)) continue;
            ++files;
            const race::BodyLods l = race::body_lods(f);
            if (l.has_switch) {
                ++switches;
                CHECK(l.levels >= 1 && l.levels <= 4);
                CHECK(std::fabs(l.size - 3.0f) < 1e-4f);
            }
            wheel_faces += l.wheel_face ? 1 : 0;
            // (The body's switch is the program's first on the bodies whose far level draws its own wheels; on the others
            // it follows the wheels' constructs and their switches.)
            if (l.has_switch && !l.switches.empty() && l.switches[0].centre[1] == l.centre[1] && l.switches[0].centre[2] == l.centre[2]) ++first;
            if (l.any_missing()) missing += (missing.empty() ? "" : " ") + name;
            if (name == "benz0001") {
                // The disc's own body: level 1 draws the four wheels' callbacks and nothing else (its table's second
                // entry jumps past the far level to the end), level 2 its wheels alone (256 triangles, 1.12 m tall:
                // the rear wheels' height); only level 0 draws the body, and the race view shows it at every distance.
                CHECK((l.level.size()) == (size_t(3)));
                if (l.level.size() == 3) {
                    CHECK(l.level[0].triangles > 4000 && !l.level[0].missing);
                    CHECK((l.level[1].triangles) == (0));
                    CHECK(l.level[1].missing && l.level[2].missing);
                    CHECK(l.level[2].triangles > 200 && l.level[2].extent[1] < 1.2f);
                }
            }
        }
        std::printf("  car/lod: %d car files, %d with a level switch at the program's top level (%d its first), %d far levels with their own wheels; a level without a body: %s\n", files, switches, first, wheel_faces,
                    missing.c_str());
        CHECK((files) == (807));
        // (789 have it first, 785 of them with nothing before it but render states; 16 after the four wheels' constructs,
        // whose switches are their own; chry0001 and toyt0030 have none outside the wheels: one level.)
        CHECK((switches) == (805));
        CHECK((wheel_faces) == (785));
        CHECK((first) == (789));
        CHECK((missing) == (std::string("benz0001")));
        ran = true;
    }
    if (!ran) SKIP("no US disc");
}
