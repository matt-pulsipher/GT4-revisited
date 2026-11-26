// A license test's road section: its own cones and gates.
#include "test_framework.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/race/gadgets.h"

#include <cmath>
#include <cstdio>

using namespace gt4r;

TEST(license_section_gadgets_disc) {
    // A race on a road section adds the section's own cones and gates to the road's (0x395B60): B-3's (Deep Forest's
    // section 2) are its start and finish gates (two posts and a banner each) and a row of cones; the road has none.
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        const gt3r::Volume::Entry* e = disc->volume().find("crs/deepforest");
        CHECK(e != nullptr);
        if (!e) continue;
        const Bytes data = disc->volume().read(*e);
        const course::CourseFile f = course::CourseFile::parse(data);
        const ByteSpan road = f.section(data, course::kSlotRunway);
        const race::Gadgets none = race::Gadgets::parse(f.section(data, course::kSlotGadgets), road);
        CHECK(none.placements.empty() && none.kinds.size() == 7);
        const race::Gadgets b3 = race::Gadgets::parse(f.section(data, course::kSlotGadgets), road, 2);
        CHECK(b3.section_from == 0 && b3.placements.size() == 25);
        if (b3.placements.size() < 6) continue;
        // The finish gate: posts of kind 3 either side of the road, the banner (kind 1) between them 4 m up.
        const auto& banner = b3.placements[5];
        CHECK(banner.kind == 1 && std::fabs(banner.position.x + 273.34f) < 0.01f && std::fabs(banner.position.z + 135.15f) < 0.01f);
        CHECK(b3.placements[3].kind == 3 && b3.placements[4].kind == 3);
        // Tsukuba's road keeps its own 18 and adds a section's after them.
        const gt3r::Volume::Entry* t = disc->volume().find("crs/tsukuba");
        if (!t) continue;
        const Bytes td = disc->volume().read(*t);
        const course::CourseFile tf = course::CourseFile::parse(td);
        const race::Gadgets ts = race::Gadgets::parse(tf.section(td, course::kSlotGadgets), tf.section(td, course::kSlotRunway), 0);
        CHECK(ts.section_from == 18 && ts.placements.size() == 32);
    }
}
