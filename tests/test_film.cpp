// A film's data (photo/film.h): its chunks written and read again, a car's state through the program's parameters.
// docs/formats/photo.md "Films".

#include "test_framework.h"

#include "gt4r/photo/film.h"
#include "gt4r/save/photo_file.h"

#include <algorithm>
#include <filesystem>

#include <cmath>
#include <cstring>

using namespace gt4r;
using namespace gt4r::photo::film;

namespace {

bool near(float a, float b, float by) { return std::fabs(a - b) <= by; }

race::CarState moving_car() {
    race::CarState s;
    axes_of({1.5f, -89.7f, -1.45f}, s.right, s.up, s.forward);
    s.position = {41.6f, 14.4f, -179.3f};
    s.velocity = {5.5f, 0.15f, 0.02f};
    s.engine_rpm = 1230;
    const float across[4] = {-0.8f, 0.8f, -0.81f, 0.81f}, along[4] = {-1.355f, -1.355f, 1.355f, 1.355f};
    for (int w = 0; w < 4; ++w) {
        race::WheelState& o = s.wheels[w];
        o.center = {s.position.x + s.right.x * across[w] + s.up.x * 0.23f - s.forward.x * along[w],
                    s.position.y + s.right.y * across[w] + s.up.y * 0.23f - s.forward.y * along[w],
                    s.position.z + s.right.z * across[w] + s.up.z * 0.23f - s.forward.z * along[w]};
        o.spin = 1.07f + float(w), o.spin_rate = 16.5f, o.steer = w < 2 ? 0.1f : 0;
    }
    return s;
}

} // namespace

TEST(film_chunks_round_trip) {
    // The chunk heads: the tag's letters in order, then the size with the 8-byte head.
    Writer w;
    w.open("PMDI"), w.u8_(1), w.close();
    const Bytes& d = w.data();
    CHECK(d.size() == 9 && d[0] == 'P' && d[3] == 'I' && d[4] == 9 && d[5] == 0 && d[8] == 1);

    Film f;
    f.photo.shutter_frames = 0.48f, f.photo.filter = 2, f.photo.kelvin = 6500, f.photo.exposure = -0.3f;
    f.course = "tsukuba";
    Car c;
    c.label = "ford_gt_05", c.paint = "ford0006", c.looks = {"pdi_0014", "pdi_0014", "default", "default"}, c.c = 1;
    f.cars.push_back(c);
    for (int i = 0; i < 2; ++i) {
        Frame fr;
        fr.camera.eye = {-31.4f, 13.3f, -165.25f}, fr.camera.target = {136, 21.25f, -197.6f}, fr.camera.fov = 13.02f;
        CarFrame cf;
        cf.params = params_of(moving_car());
        cf.params.at.x += 0.09f * float(i);
        if (i == 1) cf.skids = Bytes{0, 0, 0};
        fr.cars.push_back(cf);
        f.frames.push_back(fr);
    }
    const Bytes data = f.encode();
    const std::optional<Film> g = Film::decode(data);
    CHECK(g.has_value());
    if (!g) return;
    CHECK(g->encode() == data);
    CHECK(g->course == "tsukuba" && g->kind == "photo_drive" && g->cars.size() == 1 && g->cars[0].paint == "ford0006" && g->cars[0].looks[0] == "pdi_0014");
    CHECK(g->frames.size() == 2 && g->frames[1].cars[0].skids && !g->frames[0].cars[0].skids);
    CHECK(g->photo.filter == 2 && near(g->photo.kelvin, 6500, 0) && near(g->photo.shutter_frames, 0.48f, 0));
    CHECK(near(g->frames[1].cars[0].params.at.x, 41.69f, 1e-4f) && near(g->frames[0].camera.fov, 13.02f, 0));
    // A CPRM is 370 bytes with its head (as the original's).
    size_t at = 0;
    for (; at + 4 <= data.size(); ++at)
        if (data[at] == 'C' && data[at + 1] == 'P' && data[at + 2] == 'R' && data[at + 3] == 'M') break;
    CHECK(at + 8 <= data.size() && rd32(data, at + 4) == 370);
}

TEST(film_car_state_round_trip) {
    // The body's angles: yaw -90 heads along +x; the axes back from the angles.
    Vec3 r, u, fw;
    axes_of({0, -90, 0}, r, u, fw);
    CHECK(near(fw.x, 1, 1e-5f) && near(fw.z, 0, 1e-5f) && near(r.z, 1, 1e-5f) && near(u.y, 1, 1e-5f));
    const Vec3 a = angles_of(r, u, fw);
    CHECK(near(a.y, -90, 1e-3f) && near(a.x, 0, 1e-3f) && near(a.z, 0, 1e-3f));
    // A car's state through the parameters and back: its place, axes, wheels and their spin.
    const race::CarState s = moving_car();
    const Params p = params_of(s);
    CHECK(near(p.turn.x, 1.5f, 1e-3f) && near(p.turn.y, -89.7f, 1e-3f) && near(p.turn.z, -1.45f, 1e-3f));
    CHECK(near(p.wheels[0].at.x, -0.8f, 1e-4f) && near(p.wheels[0].at.z, -1.355f, 1e-4f) && near(p.wheels[0].at.y, 0.23f, 1e-4f));
    CHECK(near(p.wheels[0].spun, -16.5f / 60 * 57.29578f, 1e-3f));
    const race::CarState t = state_of(p);
    CHECK(near(t.position.x, s.position.x, 1e-5f) && near(t.forward.x, s.forward.x, 1e-5f) && near(t.up.z, s.up.z, 1e-5f));
    for (int w = 0; w < 4; ++w) {
        CHECK(near(t.wheels[w].center.x, s.wheels[w].center.x, 1e-4f) && near(t.wheels[w].center.y, s.wheels[w].center.y, 1e-4f));
        CHECK(near(t.wheels[w].spin, s.wheels[w].spin, 1e-5f) && near(t.wheels[w].steer, s.wheels[w].steer, 1e-6f) && near(t.wheels[w].spin_rate, 16.5f, 1e-3f));
    }
    CHECK(near(t.engine_rpm, 1230, 0) && t.body_placed);
}

TEST(film_on_the_card) {
    // A film on the card (FileGT4FilmData): its folder ...F + letters, its record F0000, its data read back as written.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r-film-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    save::PhotoCard card(dir.string(), {});
    Film f;
    f.course = "tsukuba";
    Car c;
    c.label = "ford_gt_05", c.paint = "ford0006";
    f.cars.push_back(c);
    Frame fr;
    CarFrame cf;
    cf.params = params_of(moving_car());
    fr.cars.push_back(cf);
    f.frames = {fr, fr};
    const Bytes data = f.encode();
    save::PhotoInfo info;
    info.course_file = "tsukuba", info.car_name = "Ford GT `05", info.mode = 13;
    CHECK(card.save_film(info, data, {}, "Film data") == save::Result::Ok);
    const std::vector<save::PhotoEntry> films = card.list('F');
    CHECK(films.size() == 1 && card.list('P').empty());
    if (films.size() == 1) {
        CHECK(films[0].folder.rfind("BASCUS-97328F", 0) == 0 && films[0].info && films[0].info->name == "F0000" && films[0].info->mode == 13);
        const std::optional<Bytes> back = card.film(films[0].folder);
        CHECK(back && back->size() >= data.size() && std::equal(data.begin(), data.end(), back->begin()));
        CHECK(back && Film::decode(*back).has_value());
    }
    std::filesystem::remove_all(dir);
}

// The last frame's cones and boards (GDGT, 0x3E5408) and particles (PTCL, 0x3C2990): 160 slots, a used one resting as "01 00",
// a moving one with its kind, its angles as bytes of 128 / pi and its place; no emitters "00 00". (✔ The original's film at
// Tsukuba: 18 resting, 189 bytes with the chunk's head.)
TEST(film_gadgets_and_particles) {
    std::vector<photo::film::GadgetSlot> slots(photo::film::kGadgetSlots);
    for (size_t i = 0; i < 18; ++i) slots[i].used = true;
    Bytes b = photo::film::gadgets_payload(slots);
    CHECK(b.size() == 181 && b[0] == 0 && b[1] == 0xA0 && b[2] == 0 && b[3] == 1 && b[4] == 0 && b[37] == 1 && b[38] == 0 && b[39] == 0);
    slots.assign(2, {});
    slots[0].used = slots[0].moving = true, slots[0].kind = 3;
    slots[0].angles = {3.14159274f, 0.5f, 6.28f}, slots[0].pos = {1, 2, 3};
    b = photo::film::gadgets_payload(slots);
    CHECK(b.size() == 3 + 2 + 1 + 3 + 12 + 1);
    CHECK(b[5] == 3 && b[6] == 128 && b[7] == 20 && b[8] == 255); // (0.5 x 128 / pi = 20.37; 6.28 x 128 / pi = 255.87)
    float y = 0;
    std::memcpy(&y, &b[13], 4);
    CHECK(y == 2.0f && b.back() == 0);
    CHECK(photo::film::no_particles() == Bytes({0, 0}));
}
