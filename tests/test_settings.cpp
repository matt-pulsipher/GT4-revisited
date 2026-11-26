// The app's settings (src/gt4r/settings), the picture's shape (race/game_area.h), the mods' order and the textures a pack
// replaces, with the disc's own files.

#include "test_framework.h"

#include "base/config.h"
#include "base/file.h"
#include "formats/png_writer.h"
#include "formats/texture_pack.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/input/player_controls.h"
#include "gt4r/race/game_area.h"
#include "gt4r/race/race_display.h"
#include "gt4r/settings/mod_files.h"
#include "gt4r/settings/texture_files.h"
#include "gt4r/settings/video.h"

#include <cmath>
#include <map>
#include <set>

#include <filesystem>

using namespace gt4r;
using namespace gt4r::settings;

#define CHECK_EQ(a, b) CHECK((a) == (b))
#define REQUIRE(cond) \
    do { \
        CHECK(cond); \
        if (!(cond)) return; \
    } while (0)

TEST(settings_video_round_trip) {
    // Nothing written: the defaults, and saving them writes nothing.
    gt3r::IniDocument ini;
    Video v = Video::load(ini);
    CHECK(v == Video{});
    CHECK_EQ(v.msaa, 4);
    v.save(ini);
    CHECK(ini.serialize().find("video") == std::string::npos);
    // Every field changed comes back, and only the changed ones are written.
    v.fullscreen = true, v.vsync = false, v.limit_60fps = true, v.show_fps = true, v.dynamic_resolution = true, v.sharp_menu_models = false, v.texture_packs = false;
    v.msaa = 8, v.anisotropy = 16, v.render_scale = 1.25f, v.gamma = 1.15f, v.aspect = gt3r::platform::Aspect::Classic, v.gpu = Gpu::Gles;
    v.save(ini);
    CHECK(Video::load(ini) == v);
    CHECK(Video::load(gt3r::IniDocument::parse(ini.serialize())) == v);
    // Back to the defaults: the same again (gamma's line goes).
    Video{}.save(ini);
    CHECK(Video::load(ini) == Video{});
    CHECK(!ini.get("video", "gamma"));
    // A hand-written file: other words, values out of range.
    const Video odd = Video::load(gt3r::IniDocument::parse("[video]\nmsaa = 6\nanisotropy = 100\nvsync = 0\nfullscreen = yes\nrender_scale = wide\naspect = 5:4\ngpu = 0\ngamma = 9\n"));
    CHECK_EQ(odd.msaa, 4);
    CHECK_EQ(odd.anisotropy, 16);
    CHECK(!odd.vsync && odd.fullscreen);
    CHECK(!odd.render_scale);
    CHECK(odd.aspect == gt3r::platform::Aspect::Fill);
    // The screen's shape by default; a config naming 16:9 or 4:3 keeps it.
    CHECK(Video{}.aspect == gt3r::platform::Aspect::Fill);
    CHECK(Video::load(gt3r::IniDocument::parse("[video]\nvsync = on\n")).aspect == gt3r::platform::Aspect::Fill);
    CHECK(Video::load(gt3r::IniDocument::parse("[video]\naspect = 16:9\n")).aspect == gt3r::platform::Aspect::Wide);
    CHECK(Video::load(gt3r::IniDocument::parse("[video]\naspect = 4:3\n")).aspect == gt3r::platform::Aspect::Classic);
    CHECK(Video::load(gt3r::IniDocument::parse("[video]\naspect = Fill\n")).aspect == gt3r::platform::Aspect::Fill);
    {
        // Another setting changed leaves the shape unwritten (it stays the default); the shape chosen is written.
        gt3r::IniDocument c;
        Video d;
        d.vsync = false;
        d.save(c);
        CHECK(!c.get("video", "aspect"));
        d.aspect = gt3r::platform::Aspect::Wide;
        d.save(c);
        CHECK(c.get("video", "aspect").value_or("") == "16:9");
    }
    CHECK(odd.gpu == Gpu::Off);
    CHECK(std::fabs(odd.gamma - 2.0f) < 1e-6f);
}

TEST(settings_frame_limit) {
    CHECK_EQ(limited_rate(120, false), 0.0);
    CHECK_EQ(limited_rate(60, true), 0.0);
    CHECK_EQ(limited_rate(59.94, true), 0.0);
    CHECK_EQ(limited_rate(90, true), 0.0);
    CHECK_EQ(limited_rate(120, true), 60.0);
    CHECK_EQ(limited_rate(144, true), 72.0);
    CHECK_EQ(limited_rate(240, true), 60.0);
    CHECK(std::fabs(limited_rate(119.88, true) - 59.94) < 1e-9);
    CHECK_EQ(limited_rate(0, true), 0.0);
}

TEST(settings_resolutions) {
    std::vector<Resolution> r = resolutions(1.0f, 1.0f, 4.0f);
    CHECK_EQ(r.size(), size_t(13));
    CHECK_EQ(r.front().name, std::string("50%"));
    CHECK_EQ(r.back().name, std::string("400%"));
    size_t at = resolution_index(r, std::nullopt);
    CHECK_EQ(r[at].name, std::string("Automatic (100%)"));
    CHECK(r[at + 1].scale && *r[at + 1].scale == 1.0f); // (in its place among the sizes)
    CHECK_EQ(r[resolution_index(r, 0.74f)].name, std::string("75%"));
    r = resolutions(0.62f, 1.0f, 1.0f);
    CHECK_EQ(r.back().name, std::string("100%"));
    CHECK_EQ(r[resolution_index(r, std::nullopt)].name, std::string("Automatic (up to 100%)"));
}

TEST(settings_game_area) {
    using namespace race;
    CHECK(std::fabs(canvas_side(640) - 106.6667f) < 1e-3f);
    CHECK_EQ(canvas_side(480), 0.0f);
    // The program's 96 at 16:9 and none at 4:3; wider pictures carry the parts out with their edge, at the same margin.
    CHECK_EQ(anchor_push(canvas_side(640)), 96.0f);
    CHECK_EQ(anchor_push(0), 0.0f);
    const float wide = canvas_side(960);
    CHECK(std::fabs((wide - anchor_push(wide)) - (canvas_side(640) - 96.0f)) < 1e-3f);
    DisplayPlace right;
    right.x = 600, right.anchor_x = 1 | 4 | 8;
    CHECK_EQ(place_x(right, true), 696.0f);
    set_wide_push(anchor_push(wide));
    CHECK(std::fabs(place_x(right, true) - (600 + anchor_push(wide))) < 1e-3f);
    CHECK_EQ(place_x(right, false), 600.0f);
    set_wide_push(96.0f);
}

TEST(settings_mod_priority_text) {
    CHECK_EQ(with_priority("", 3), std::string("[mod]\npriority = 3\n"));
    CHECK_EQ(with_priority("; note\n[mod]\nname = A\n", 3), std::string("; note\n[mod]\nname = A\npriority = 3\n"));
    CHECK_EQ(with_priority("[mod]\nname = A\npriority = 1 \nauthor = B\n", 12), std::string("[mod]\nname = A\npriority = 12\nauthor = B\n"));
    // Another section's line of that name is not it; line ends stay as they are.
    CHECK_EQ(with_priority("[mod]\r\nname = A\r\n\r\n[other]\r\npriority = 5\r\n", 2), std::string("[mod]\r\nname = A\r\npriority = 2\r\n\r\n[other]\r\npriority = 5\r\n"));
    const std::string text = with_priority("\xEF\xBB\xBF[mod]\nname = A\n", 7);
    CHECK_EQ(text.substr(3), std::string("[mod]\nname = A\npriority = 7\n"));
    CHECK_EQ(gt3r::IniDocument::parse(with_priority("[mod]\nname=A\n#priority = 9\n", 4)).get("mod", "priority").value_or(""), std::string("4"));
}

TEST(settings_mod_order) {
    const auto order = [](std::vector<ModPlace> mods) {
        sort_places(mods);
        std::string s;
        for (const ModPlace& m : mods) s += m.id;
        return s;
    };
    const auto moved = [&](std::vector<ModPlace> mods, size_t at, int d) {
        const std::vector<ModPlace> change = move_mod(mods, at, d);
        for (const ModPlace& c : change)
            for (ModPlace& m : mods)
                if (m.id == c.id) m.priority = c.priority;
        return std::pair<std::string, size_t>(order(mods), change.size());
    };
    // (equal numbers: the later name wins, as the shared engine's file layer has it)
    const std::vector<ModPlace> same{{"a", 0}, {"b", 0}, {"c", 0}};
    CHECK_EQ(order(same), std::string("cba"));
    CHECK_EQ(moved(same, 2, -1).first, std::string("cab"));
    CHECK_EQ(moved(same, 0, 1).first, std::string("bca"));
    CHECK_EQ(moved(same, 0, -1).second, size_t(0));
    CHECK_EQ(moved(same, 2, 1).second, size_t(0));
    const std::vector<ModPlace> spread{{"a", 10}, {"b", 5}, {"c", -3}};
    CHECK_EQ(order(spread), std::string("abc"));
    CHECK_EQ(moved(spread, 2, -1).first, std::string("acb"));
    CHECK_EQ(moved(spread, 0, 1).first, std::string("bac"));
    // Every move from every order comes out as asked.
    std::vector<ModPlace> mods{{"a", 0}, {"b", 2}, {"c", 2}, {"d", 1}, {"e", 0}};
    for (int round = 0; round < 40; ++round) {
        const size_t at = size_t(round * 7) % 5;
        const int d = round % 2 ? 1 : -1;
        std::string want = order(mods);
        if (int(at) + d >= 0 && int(at) + d < 5) std::swap(want[at], want[size_t(int(at) + d)]);
        for (const ModPlace& c : move_mod(mods, at, d))
            for (ModPlace& m : mods)
                if (m.id == c.id) m.priority = c.priority;
        CHECK_EQ(order(mods), want);
    }
}

TEST(settings_controls_words) {
    // The config keeps the game's words; the input layer reads its own.
    gt3r::InputManager in;
    gt3r::IniDocument ini = gt3r::IniDocument::parse("[player2]\nrace = original\nrumble = original\n");
    gt3r::IniDocument engine = ini;
    input::translate_config(engine);
    in.load(engine);
    CHECK(in.race_uses_menu_pad(1));
    in.set_background(true);
    gt3r::Chord c;
    CHECK(gt3r::Chord::parse("key.n+key.m", c));
    in.bind_action(0, gt3r::PadAction::NextSong, c, true);
    input::save_config(in, ini);
    CHECK_EQ(ini.get("player2", "race").value_or(""), std::string("original"));
    CHECK_EQ(ini.get("player2", "rumble").value_or(""), std::string("original"));
    CHECK_EQ(gt3r::to_lower(ini.get("input", "background").value_or("")), std::string("on"));
    CHECK(ini.get("player1.actions", "next_song").value_or("").size() > 4);
}

// A car's textures as the pack knows them: those its models are built from, in every paint; a picture put in the pack is what the
// model shows.
TEST(disc_texture_pack_car) {
    Disc* full = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4) full = d.get();
    if (!full) SKIP("no Gran Turismo 4 disc");
    Disc& disc = *full;
    const FileReader read = [&disc](const std::string& path) -> std::optional<Bytes> {
        if (const auto* e = disc.volume().find(path)) return disc.volume().read(*e);
        return std::nullopt;
    };
    std::string path;
    for (const ImageFile& f : disc.files())
        if (f.path.rfind("car/lod/", 0) == 0 && f.path.find('.') == std::string::npos && disc.volume().find(f.path + ".pat")) {
            path = f.path;
            break;
        }
    REQUIRE(!path.empty());
    REQUIRE(has_pack_textures(path));
    CHECK(!has_pack_textures(path + ".pat"));
    CHECK(!has_pack_textures("bgm/race_01.ads"));
    const Bytes file = *read(path), pat = *read(path + ".pat");
    const u32 paints = car::paint_count(pat);
    REQUIRE(paints > 1);
    std::map<u64, std::string> named;
    std::set<std::string> folders;
    for_each_texture(path, file, read, [&](const std::string& name, const gt3r::texture_pack::FileTexture& t) { named.emplace(t.key, name), folders.insert(name); });
    for (u32 p = 0; p < paints; ++p) {
        char folder[24];
        std::snprintf(folder, sizeof folder, "/paint%02u", p);
        CHECK(folders.count(path + folder));
        // (the plain textures of the model: those the decoder leaves as the set has them)
        car::Options o;
        o.paint = p, o.wheels = false;
        const gt3r::scene::Model m = car::build_car(file, pat, nullptr, o, "car");
        size_t found = 0;
        for (const gt3r::scene::Texture& t : m.textures) found += named.count(gt3r::texture_pack::key_of(t.width, t.height, t.rgba.data()));
        CHECK(found * 2 > m.textures.size());
    }
    // A pack's picture replaces the texture in the model.
    car::Options o;
    o.wheels = false;
    const gt3r::scene::Model plain = car::build_car(file, pat, nullptr, o, "car");
    const gt3r::scene::Texture* some = nullptr;
    for (const gt3r::scene::Texture& t : plain.textures)
        if (!some && named.count(gt3r::texture_pack::key_of(t.width, t.height, t.rgba.data()))) some = &t;
    REQUIRE(some != nullptr);
    const u64 key = gt3r::texture_pack::key_of(some->width, some->height, some->rgba.data());
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r-test-texture-pack";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string png = dir.string() + "/x-" + gt3r::texture_pack::key_name(key) + ".png";
    std::vector<u32> magenta(64 * 64, 0xFFFF00FFu);
    REQUIRE(gt3r::write_host_file(png, gt3r::encode_png(64, 64, magenta)));
    auto pack = std::make_shared<gt3r::texture_pack::Pack>();
    CHECK_EQ(pack->add_folder(dir.string()), size_t(1));
    gt3r::texture_pack::set_pack(pack);
    const gt3r::scene::Model changed = car::build_car(file, pat, nullptr, o, "car");
    gt3r::texture_pack::set_pack(nullptr);
    REQUIRE(changed.textures.size() == plain.textures.size());
    size_t replaced = 0;
    for (const gt3r::scene::Texture& t : changed.textures) replaced += t.width == 64 && t.height == 64 && t.rgba == magenta;
    CHECK(replaced >= 1);
    // Without the pack the same models as before.
    const gt3r::scene::Model again = car::build_car(file, pat, nullptr, o, "car");
    for (size_t i = 0; i < again.textures.size(); ++i) CHECK(again.textures[i].rgba == plain.textures[i].rgba);
    std::filesystem::remove_all(dir);
}
