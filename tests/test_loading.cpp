// What a race's loading works out once: the disc's program unpacked, the cars' sound banks, the triangles under a point
// off the road.

#include "test_framework.h"

#include "disc/core_image.h"
#include "gt4r/audio/render.h"
#include "gt4r/audio/race_sound.h"
#include "gt4r/audio/sound_library.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/session.h"
#include "vfs/vfs.h"

#include <bit>
#include <thread>

using namespace gt4r;

TEST(disc_program_unpacked_once) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        disc->prepare_program();
        std::shared_ptr<const gt3r::CoreImage> a, b;
        std::thread other([&] { a = disc->program_image(); });
        b = disc->program_image();
        other.join();
        CHECK(a && a == b);
        const gt3r::CoreImage own = gt3r::CoreImage::decode(disc->read_program());
        CHECK(a->program == own.program && a->load_address == own.load_address);
        // Every reader of the program's tables holds that one image.
        const sim::Program p = sim::Program::load(*disc), q = p;
        CHECK(p.image().data() == a->program.data() && q.image().data() == p.image().data() && p.base() == a->load_address);
        const sim::Program of_bytes = sim::Program::from_image(Bytes(own.program), own.load_address);
        CHECK(of_bytes.known() == p.known() && of_bytes.issue() == p.issue() && of_bytes.w(sim::Program::kStatic) == p.w(sim::Program::kStatic));
    }
    CHECK(!sim::Program{}.known() && sim::Program{}.image().empty() && sim::Program{}.b(0x100000) == 0);
}

// The cells give the height the whole list of triangles gives, on the road, beside it and far off it.
TEST(disc_road_heights_by_cells) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        size_t roads = 0, points = 0, found = 0, different = 0;
        for (const ImageFile& f : disc->files()) {
            if (f.path.compare(0, 4, "crs/") != 0 || f.path.find('.') != std::string::npos || f.path.find('/', 4) != std::string::npos) continue;
            const Bytes data = disc->volume().read(*disc->volume().find(f.path));
            if (!course::CourseFile::looks_like_course(data)) continue;
            std::optional<course::Runway> road;
            try {
                road = course::course_road(course::CourseFile::parse(data), data);
            } catch (const std::exception&) {
            }
            if (!road || road->triangles.empty()) continue;
            if (++roads % 8 != 1) continue; // (one in eight)
            CHECK(road->cells.triangles == road->triangles.size());
            course::Runway plain = *road;
            plain.cells = {};
            u32 seed = 12345 + u32(roads);
            const auto draw = [&seed] { return float((seed = seed * 1664525u + 1013904223u) >> 8) / float(1 << 24); };
            for (int i = 0; i < 1500; ++i) {
                float x, z;
                if (i % 3 == 0) {
                    x = road->min.x - 50 + (road->max.x - road->min.x + 100) * draw(), z = road->min.z - 50 + (road->max.z - road->min.z + 100) * draw();
                } else { // near a triangle's corner or edge
                    const course::Runway::Triangle& t = road->triangles[size_t(draw() * 0.9999f * float(road->triangles.size()))];
                    const race::Vec3 &a = road->vertices[t.v[0]].pos, &b = road->vertices[t.v[1]].pos;
                    const float k = i % 3 == 1 ? 0.0f : draw(), room = i % 2 ? 0.0f : 3.0f;
                    x = a.x + (b.x - a.x) * k + room * (draw() - 0.5f), z = a.z + (b.z - a.z) * k + room * (draw() - 0.5f);
                }
                const float top = i % 5 ? road->max.y + 10 : road->min.y + (road->max.y - road->min.y) * draw();
                float y1 = 0, y2 = 0;
                int t1 = -1, t2 = -1;
                const bool f1 = road->height_at(x, z, top, y1, &t1), f2 = plain.height_at(x, z, top, y2, &t2);
                ++points, found += f1;
                different += f1 != f2 || t1 != t2 || std::bit_cast<u32>(y1) != std::bit_cast<u32>(y2);
            }
        }
        std::printf("  %zu roads, %zu points asked, %zu on a triangle, %zu different\n", roads, points, found, different);
        CHECK(points > 0 && found > 0 && different == 0);
    }
}

// A race's cars share the banks they have in common, each read once; the files are the car database's either way.
TEST(sound_race_banks_read_once_on_disc) {
    for (const auto& disc : test::discs()) {
        if (disc->info().game != Game::GT4) continue;
        gt3r::Vfs vfs(&disc->volume());
        db::SpecDb sdb([&vfs](const std::string& path) { return vfs.read(path); }, [&vfs](const std::string& dir) { return vfs.list_dir(dir); });
        auto library = std::make_shared<audio::SoundLibrary>(vfs);
        audio::OfflineSound s(false);
        audio::RaceSound sound(vfs, s.mixer(), s.driver(), library, audio::RaceSoundTables::from_program(disc->program_image()->program));
        CHECK(sound.add_car(sdb, "nsx_r_02") && sound.add_car("nsx_r_02") && sound.add_car(sdb, "nsx_r_02"));
        CHECK(sound.car(0).files().engine == sound.car(1).files().engine && sound.car(0).files().exhaust == sound.car(1).files().exhaust &&
              sound.car(0).files().whine == sound.car(1).files().whine);
        for (size_t i = 1; i < 3; ++i)
            CHECK(sound.car(i).engine().bank() == sound.car(0).engine().bank() && sound.car(i).exhaust().bank() == sound.car(0).exhaust().bank() &&
                  sound.car(i).wind().bank() == sound.car(0).wind().bank());
        CHECK(sound.car(0).engine().loaded() && sound.car(0).wind().loaded());
        return;
    }
    SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
}
