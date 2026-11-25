// Photos: the locations' blocks, the shapes the camera and the car keep to, the picture's arithmetic,
// the files on the card. docs/formats/photo.md.

#include "test_framework.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/menu/jpeg.h"
#include "gt4r/photo/photo_camera.h"
#include "gt4r/photo/photo_course.h"
#include "gt4r/photo/picture.h"
#include "gt4r/race/car_light.h"
#include "gt4r/save/card_icon.h"
#include "gt4r/save/codec.h"
#include "gt4r/save/photo_file.h"

#include <cmath>
#include <cstring>
#include <filesystem>

using namespace gt4r;

namespace {

Disc* gt4_disc() {
    for (const auto& disc : test::discs())
        if (disc->info().game == Game::GT4) return disc.get();
    return nullptr;
}

Bytes noise(size_t n, u32 seed) {
    Bytes b(n);
    for (u8& v : b) seed = seed * 1664525 + 1013904223, v = u8(seed >> 24);
    return b;
}

bool near(float a, float b, float by) { return std::fabs(a - b) <= by; }

// (An outline's inside is on the side the disc's have it; a hole runs the other way.)
photo::Shape square(float x0, float z0, float x1, float z1) { return {{{{x0, z0}, {x0, z1}, {x1, z1}, {x1, z0}}}}; }

// A ground of two cells: a square with a smaller one cut out of it, and a spot.
photo::PhotoCourse ground() {
    photo::PhotoCourse c;
    photo::Shape ring = square(-10, -10, 10, 10);
    ring.outlines.push_back({{-2, -2}, {2, -2}, {2, 2}, {-2, 2}});
    c.shapes = {ring, square(20, 20, 22, 22), square(-9, -9, -5, 9)};
    photo::Cell a;
    a.shape = 0, a.car_shapes = {2}, a.eye_low = 0.3f, a.eye_high = 8;
    photo::Cell b;
    b.spot = true, b.shape = 1, b.look = {0, 0}, b.eye_low = 0.3f, b.eye_high = 8;
    c.cells = {a, b};
    c.drag = {-100, -100, 100, 100}, c.view = {-100, -100, 100, 100};
    c.camera_start = {6, 0}, c.car_start = {-7, 0};
    return c;
}

} // namespace

TEST(photo_files_scrambled) {
    for (save::FileKind kind : {save::FileKind::Photo, save::FileKind::Film, save::FileKind::Info})
        for (size_t n : {size_t(0xE8), size_t(4000)}) {
            const Bytes plain = noise(n, u32(n));
            const Bytes file = save::scramble(plain, 0, 0x12345678, kind);
            CHECK(file.size() == save::scrambled_size(n));
            const auto back = save::unscramble(file, 0, kind);
            CHECK(back && std::equal(plain.begin(), plain.end(), back->begin()));
            Bytes broken = file;
            broken[file.size() / 2] ^= 1;
            CHECK(!save::unscramble(broken, 0, kind));
        }
}

TEST(photo_folder_letters) {
    for (u64 id : {u64(0), u64(1), u64(0x2ABCDEF1234), u64(5) << 32 | 0x80000001u}) {
        const std::string s = save::folder_letters(id);
        CHECK(s.size() == 7 && save::folder_id(s) == id);
    }
    CHECK(!save::folder_id("abc") && !save::folder_id("abc*efg"));
    save::PhotoInfo i;
    i.car = 0x18F00000004ull, i.course = 0x2200000098ull, i.name = "P0007", i.course_file = "ss_times", i.car_name = "A car", i.portrait = true;
    const Bytes rec = i.encode();
    const auto back = save::PhotoInfo::decode(rec);
    CHECK(rec.size() == save::kInfoSize && back && back->car == i.car && back->course == i.course && back->name == i.name && back->portrait);
}

TEST(photo_jpeg) {
    const int w = 80, h = 48;
    std::vector<u8> rgb(size_t(w) * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            u8* p = &rgb[(size_t(y) * w + size_t(x)) * 3];
            p[0] = u8(x * 3), p[1] = u8(y * 5), p[2] = u8(255 - x * 2);
        }
    menu::JpegPicture::Focus f;
    f.x = -0.25f, f.y = 0.5f, f.x0 = -1, f.x1 = 0.5f, f.y0 = 0, f.y1 = 1;
    size_t last = 0;
    for (const char* name : {"STANDARD", "FINE", "SUPER_FINE"}) {
        const Bytes file = photo::encode_jpeg(rgb, w, h, photo::jpeg_quality(name), f);
        CHECK(file.size() > last); // the better the larger
        last = file.size();
        const menu::JpegPicture back = menu::decode_jpeg(file);
        CHECK(back.width == w && back.height == h && back.focus && near(back.focus->x, f.x, 1e-6f) && near(back.focus->y1, f.y1, 1e-6f));
        double sum = 0;
        for (size_t i = 0; i < back.rgba.size(); ++i)
            for (int k = 0; k < 3; ++k) {
                const double d = double(u8(back.rgba[i] >> (8 * k))) - double(rgb[i * 3 + size_t(k)]);
                sum += d * d;
            }
        CHECK(std::sqrt(sum / double(rgb.size())) < 6);
    }
}

TEST(photo_exposure) {
    // The average's weight in whole 128ths, and what adding one color 32 times leaves of it.
    CHECK(photo::exposure_weight(0) == 0 && photo::exposure_weight(1) == 64 && photo::exposure_weight(2) == 85 && photo::exposure_weight(31) == 124);
    int same = 0;
    for (int k = 0; k < photo::kSamples; ++k) same = photo::expose(same, 100, photo::exposure_weight(k));
    CHECK(same == 100);
    // Two colors in turn: below their middle, by the rounding down.
    int mixed = 0;
    for (int k = 0; k < photo::kSamples; ++k) mixed = photo::expose(mixed, k & 1 ? 140 : 60, photo::exposure_weight(k));
    CHECK(mixed < 100 && mixed > 80);
    const std::vector<photo::Sample> s = photo::samples(photo::kSamples);
    CHECK(int(s.size()) == photo::kSamples);
    for (const photo::Sample& p : s) CHECK(p.lens_x * p.lens_x + p.lens_y * p.lens_y <= 1.0001f && p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1);
}

TEST(photo_filters) {
    photo::Filters f;
    CHECK(photo::correction(f).plain());
    f.brightness = 8;
    CHECK(near(photo::correction(f).gamma[1], 2, 1e-5f));
    f.brightness = 0, f.color = photo::ColorFilter::Monochrome;
    const auto gray = photo::correction(f).apply(200, 100, 50);
    CHECK(gray[0] == gray[1] && gray[1] == gray[2]);
    const photo::Vignette v = photo::vignette({});
    CHECK(v.alpha(0, 0) == 0 && v.alpha(0.5f, 0) == 0 && v.alpha(1, 1) > 0.05f && v.alpha(1, 1) < 0.07f);
}

TEST(photo_shapes) {
    const photo::PhotoCourse c = ground();
    CHECK(c.shapes[0].contains({5, 5}) && !c.shapes[0].contains({0, 0}) && !c.shapes[0].contains({11, 0}));
    photo::Vec2 p{5, 0};
    CHECK(c.cell_at(p, 0.5f) == 0);
    p = {21.2f, 21.1f};
    CHECK(c.cell_at(p, 0.5f) == 1 && near(p.x, 21, 1e-4f) && near(p.z, 21, 1e-4f)); // put on the spot's middle
    p = {0, 0};
    CHECK(c.cell_at(p, 0.5f) < 0);
    CHECK(c.car_shape_at({-7, 0}, 0) >= 0 && c.car_shape_at({7, 0}, 0) < 0);
    // A move that leaves the shape slides along its border and stays inside it.
    photo::BorderTrack track;
    photo::Vec2 at{9, 0};
    track.reset(at);
    for (int i = 0; i < 40; ++i) {
        at = c.move(0, at, {at.x + 0.2f, at.z + 0.2f}, track);
        CHECK(at.x <= 10.01f && at.z <= 10.01f);
    }
    CHECK(near(at.x, 10, 0.05f) && at.z > 5);
}

TEST(photo_slide_list) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r-photo-test-list";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    save::PhotoCard card(dir.string(), {});
    std::vector<save::PhotoCard::Listed> out;
    CHECK(card.load_slides(out) == save::Result::FileNotFound && !card.has_slides());
    std::vector<save::PhotoCard::Listed> in = {{u64(3) << 32 | 8, 'P'}, {u64(1) << 32 | 1, 'P'}};
    CHECK(card.save_slides(in, "Slides") == save::Result::Ok);
    CHECK(card.load_slides(out) == save::Result::Ok && out.size() == 2 && out[0].id == in[0].id && out[1].id == in[1].id && out[1].kind == 'P');
    CHECK(card.folder(out[0]) == "BASCUS-97328P" + save::folder_letters(in[0].id));
    CHECK(std::filesystem::file_size(dir / "BASCUS-97328LSTSLIDE" / "BASCUS-97328LSTSLIDE") == 0xCC0);
    CHECK(card.remove_slides() == save::Result::Ok && !card.has_slides());
    std::filesystem::remove_all(dir);
}

TEST(photo_camera_modes) {
    photo::PhotoCamera cam(ground(), [](float, float, float, float& y) { return y = 0, true; });
    cam.set_window(190, 286);
    CHECK(cam.cell == 0);
    // Panned by the stick: the heading changes, the eye stays.
    const auto eye = cam.eye;
    const float yaw = cam.yaw;
    photo::Pad pad;
    pad.x = 2;
    bool ended = false;
    for (int i = 0; i < 30; ++i) ended |= cam.step(photo::Mode::Pan, i == 0, pad);
    CHECK(!ended && cam.yaw != yaw && near(cam.eye.x, eye.x, 1e-5f) && near(cam.eye.z, eye.z, 1e-5f));
    // Moved: it stays in its cell.
    pad = {};
    pad.y = -2;
    for (int i = 0; i < 600; ++i) cam.step(photo::Mode::Move, i == 0, pad);
    photo::Vec2 at{cam.eye.x, cam.eye.z};
    CHECK(near(at.x, eye.x, 1e-3f) == false || near(at.z, eye.z, 1e-3f) == false);
    CHECK(std::fabs(at.x) < 10.1f && std::fabs(at.z) < 10.1f);
    // Raised to the cell's top and no further.
    for (int i = 0; i < 2000; ++i) cam.step(photo::Mode::Climb, i == 0, pad);
    CHECK(cam.eye.y <= 8.001f && cam.eye.y > 2);
}

TEST(disc_photo_locations) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    int found = 0;
    for (const gt3r::Volume::Entry& e : disc->volume().entries()) {
        if (e.path.rfind("crs/ss_", 0) != 0 || e.path.find('.') != std::string::npos) continue;
        const Bytes file = disc->volume().read(e);
        const ByteSpan section = course::CourseFile::parse(file).section(file, 51);
        CHECK(!section.empty());
        if (section.empty()) continue;
        const photo::PhotoCourse c = photo::PhotoCourse::parse(section);
        photo::Vec2 p = c.camera_start;
        const int cell = c.cell_at(p, 0.5f);
        CHECK(cell >= 0);
        CHECK(cell >= 0 && c.car_shape_at(c.car_start, cell) >= 0);
        CHECK(gt3r::has_magic(section.subspan(c.map_offset), "MDLS"));
        ++found;
    }
    CHECK(found == 15);
}

TEST(disc_photo_icon) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    std::vector<u16> small(size_t(save::kIconPicture) * save::kIconPicture);
    for (size_t i = 0; i < small.size(); ++i) small[i] = u16(i * 7 & 0x7FFF);
    for (const char* path : {"icon/GT4_photo_4_to_3_.ico", "icon/GT4_photo_3_to_4_.ico"}) {
        const gt3r::Volume::Entry* e = disc->volume().find(path);
        CHECK(e != nullptr);
        if (!e) continue;
        const Bytes icon = save::icon_with_picture(disc->volume().read(*e), small);
        const auto back = save::icon_picture(icon);
        CHECK(back && back->size() == small.size());
        if (!back) continue;
        size_t same = 0;
        for (size_t i = 0; i < small.size(); ++i) same += u32((*back)[i] & 0xFF) / 8 == u32(small[i] & 31);
        CHECK(same == small.size());
    }
}

// The light on a car (race/car_light.h): Times Square's road has no light of its own, the snow's is in full light, Shibuya's and
// Route 5's name lamps.
TEST(disc_car_light) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    for (const auto& [name, lamps, dark] : {std::tuple<const char*, bool, bool>{"ss_times", false, true}, {"ss_snow", false, false}, {"ss_shibuya", true, false}, {"route5", true, false}}) {
        const gt3r::Volume::Entry* e = disc->volume().find(std::string("crs/") + name);
        CHECK(e != nullptr);
        if (!e) continue;
        const Bytes file = disc->volume().read(*e);
        const course::CourseFile c = course::CourseFile::parse(file);
        const ByteSpan section = c.section(file, course::kSlotRunway);
        const course::Runway road = course::Runway::parse(section);
        const gt3r::race::SunParams env = gt3r::race::SunParams::from_env0(c.section(file, course::kSlotEnv));
        race::CarLights lights;
        lights.load(section);
        CHECK(lights.lamps() == lamps);
        // Somewhere on the road: the middle of its first large triangle.
        int seen = 0;
        for (size_t t = 0; t < road.triangles.size() && seen < 50; t += 37) {
            const auto& v = road.triangles[t].v;
            const auto &a = road.vertices[v[0]].pos, &b = road.vertices[v[1]].pos, &d = road.vertices[v[2]].pos;
            const gt3r::render::Vec3 at{(a.x + b.x + d.x) / 3, (a.y + b.y + d.y) / 3, (a.z + b.z + d.z) / 3};
            const race::CarLights::Light l = lights.at(road, env, at, at.y + 0.1f);
            ++seen;
            if (lamps) {
                if (l.lamps) CHECK(l.lights.count >= 1 && l.lights.count <= 4);
            } else if (dark) {
                CHECK(l.shadow == 0 && (l.own ? l.lights.count > 0 : l.scale[1] < 0.3f));
                if (l.own) CHECK(near(l.lights.ambient[0], env.global_ambient[0] + 0, 0.02f)); // only the one that is not shaded
            } else CHECK(!l.own && near(l.scale[0], 1, 1e-3f) && near(l.shadow, 1, 1e-3f));
        }
        CHECK(seen > 0);
    }
}
