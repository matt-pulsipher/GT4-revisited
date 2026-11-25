// The photo shoot's pages and Photo Lab, run from a disc without the app: the modes the buttons put the camera in, the
// dialogs, a photo saved to a card, listed, shown, renamed and thrown away. docs/formats/photo.md.

#include "menu_run_flow.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/menu/photo_natives.h"
#include "gt4r/save/photo_file.h"

#include <filesystem>

namespace {

using Pad = menu::Runtime::Pad;

std::shared_ptr<menu::PhotoShoot> shoot_at(Disc& disc, const char* course) {
    const gt3r::Volume::Entry* e = disc.volume().find(std::string("crs/") + course);
    if (!e) return nullptr;
    const Bytes file = disc.volume().read(*e);
    const ByteSpan section = course::CourseFile::parse(file).section(file, 51);
    if (section.empty()) return nullptr;
    auto s = std::make_shared<menu::PhotoShoot>(photo::PhotoCourse::parse(section), [](float, float, float, float& y) { return y = 0, true; });
    s->course = course, s->car_label = "ford_gt_05";
    return s;
}

// One picture of the app's: the pad, the map's step, the menus.
void step(Flow& f, const Pad& pad, int n = 1) {
    for (int i = 0; i < n; ++i) {
        f.rt->set_pad(pad);
        menu::step_photo_map(*f.rt, *f.rt->photo);
        f.rt->update();
    }
}

void picture(Flow& f) {
    auto p = std::make_shared<menu::PhotoPicture>();
    p->width = 64, p->height = 48, p->version = 1;
    std::vector<u8> rgb(64 * 48 * 3);
    for (size_t i = 0; i < rgb.size(); ++i) rgb[i] = u8(i * 5);
    p->rgba.assign(64 * 48, 0xFF808080u);
    p->jpeg = photo::encode_jpeg(rgb, 64, 48, 80, std::nullopt);
    f.rt->photo_picture = p;
}

// Presses and lets go a button as the app does: the key, then its release.
void push(Flow& f, menu::Key key, u32 bit) {
    Pad held;
    held.buttons = bit;
    f.rt->key(key);
    step(f, held, 2);
    step(f, Pad{}, 10);
}

} // namespace

TEST(disc_photo_shoot_pages) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r-photo-test-pages";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "memorycard");
    {
        Flow f(*disc, "US", dir.string());
        f.rt->boot();
        f.rt->game().card.connected = true;
        f.rt->game().major_menu_project = "gtmode", f.rt->game().major_menu_page = "PhotoTravelRoot";
        f.rt->photo = shoot_at(*disc, "ss_times");
        CHECK(f.rt->photo != nullptr);
        if (!f.rt->photo) return;
        menu::PhotoShoot& s = *f.rt->photo;
        f.rt->start_project("photo_shoot");
        f.rt->settle();
        step(f, Pad{}, 10);
        CHECK(f.page() == "ShootRoot" && f.focus() == "Pan");
        CHECK(s.mode == photo::Mode::None && s.camera.cell >= 0);
        // Pan: Cross held on its button, the left stick turns the camera; let go, it stays.
        Pad held;
        held.buttons = Pad::kCross;
        const float yaw = s.camera.yaw;
        f.rt->key(menu::Key::Accept);
        held.sticks[2] = 0.8f;
        step(f, held, 20);
        CHECK(s.mode == photo::Mode::Pan && s.camera.yaw != yaw);
        step(f, Pad{}, 5);
        CHECK(s.mode == photo::Mode::None);
        // The shoulder buttons: L1 pans, L2 rolls, R1 moves, R2 climbs, while held.
        for (const auto& [bit, key, mode] : {std::tuple<u32, menu::Key, photo::Mode>{Pad::kL1, menu::Key::L1, photo::Mode::Pan}, {Pad::kL2, menu::Key::L2, photo::Mode::Roll},
                                             {Pad::kR1, menu::Key::R1, photo::Mode::Move}, {Pad::kR2, menu::Key::R2, photo::Mode::Climb}}) {
            Pad p;
            p.buttons = bit;
            f.rt->set_pad(p);
            f.rt->key(key);
            step(f, p, 5);
            CHECK(s.mode == mode);
            step(f, Pad{}, 5);
            CHECK(s.mode == photo::Mode::None);
        }
        // R1 and R2 together open the focal length's slider for as long as they are held.
        {
            Pad p;
            p.buttons = Pad::kR1;
            f.rt->set_pad(p);
            f.rt->key(menu::Key::R1);
            step(f, p, 3);
            p.buttons = Pad::kR1 | Pad::kR2;
            f.rt->set_pad(p);
            f.rt->key(menu::Key::R2);
            step(f, p, 5);
            CHECK(f.focus() == "FocalLen");
            const float angle = s.view_angle;
            for (int i = 0; i < 40; ++i) f.rt->key(menu::Key::Right, i > 0), step(f, p, 2);
            CHECK(s.view_angle > angle + 20); // a held direction goes faster and faster
            step(f, Pad{}, 5);
            CHECK(f.focus() == "Pan");
        }
        // The dialogs.
        CHECK(f.focus_on("Steer") || f.focus_on("Steering"));
        // A photo: the shutter takes it when the button is let go.
        CHECK(f.focus_on("Shoot"));
        push(f, menu::Key::Accept, Pad::kCross);
        step(f, Pad{}, 30);
        CHECK(f.page() == "PreviewRoot");
        CHECK(menu::photo_render_face(*f.rt) != nullptr);
        picture(f);
        step(f, Pad{}, 5);
        // Start saves it: no question where, no card to have or to fill.
        f.rt->game().card.connected = false;
        push(f, menu::Key::Start, Pad::kStart);
        CHECK(f.page() != "SaveDialog");
        for (int i = 0; i < 4 && f.page() != "PreviewRoot"; ++i) push(f, menu::Key::Accept, Pad::kCross), step(f, Pad{}, 90);
        std::printf("  after saving: %s\n", f.page().c_str());
        CHECK(f.clean());
        const save::PhotoCard card((dir / "memorycard").string(), {});
        const auto photos = card.list('P');
        CHECK(photos.size() == 1 && photos[0].info && photos[0].info->course_file == "ss_times" && photos[0].number == 0);
        CHECK(!photos.empty() && card.picture(photos[0].folder).has_value());
    }
    // Photo Lab: the photo in its list, its preview, its name changed, thrown away.
    {
        Flow f(*disc, "US", dir.string());
        f.rt->boot();
        f.rt->game().card.connected = true;
        f.rt->game().major_menu_project = "gtmode", f.rt->game().major_menu_page = "HomeRoot";
        if (const auto game = f.rt->vm().lookup("game").as<menu::Object>()) game->set("labo_folder", script::Value::integer(1));
        f.rt->game().next_sequence = "photo_labo";
        f.rt->start_project("labomode");
        f.rt->settle();
        for (int i = 0; i < 120; ++i) f.rt->update();
        CHECK(f.page() == "PhotoRoot");
        CHECK(f.clean());
        // The card manager as its scripts use it.
        script::Vm& vm = f.rt->vm();
        std::vector<script::Value> args = {script::Value::string("MODE_LOAD_PHOTO_PICTURE"), script::Value::integer(0)};
        const script::Value manager = vm.call(vm.attribute(vm.lookup("menu"), "MMemoryCardManager"), args);
        CHECK(vm.attribute(manager, "count").as_float() == 1);
        const auto call = [&vm](const script::Value& of, const char* name, std::vector<script::Value> a) { return vm.call(vm.attribute(of, name), a); };
        const script::Value file = call(manager, "getFile", {script::Value::integer(0)});
        CHECK(vm.attribute(file, "title").to_string() == "P0000" && vm.attribute(file, "courseName").to_string() == "ss_times");
        CHECK(call(file, "load", {manager}).to_string() == "OK");
        if (const auto o = file.as<menu::Object>()) menu::photo_file_picture(*f.rt, *o);
        CHECK(f.rt->photo_picture && f.rt->photo_picture->width == 64 && f.rt->photo_picture->height == 48);
        const auto title = vm.member(file, "title");
        if (title) title->set(vm, script::Value::string("Broadway"));
        CHECK(call(file, "writeInfo", {manager}).to_string() == "OK");
        const save::PhotoCard card((dir / "memorycard").string(), {});
        const auto photos = card.list('P');
        CHECK(photos.size() == 1 && photos[0].info && photos[0].info->name == "Broadway");
        // The slide show's list.
        const script::Value list = vm.call(vm.attribute(vm.lookup("menu"), "MMemoryCardPlayList"), {});
        const script::Value slides = vm.attribute(manager, "autoloadPlayList");
        if (const auto r = vm.member(slides, "list")) r->set(vm, list);
        CHECK(vm.attribute(manager, "autoloadPlayListResult").to_string() == "OK"); // (no list yet: an empty one, nothing to ask)
        CHECK(vm.attribute(manager, "isConnect").p && vm.attribute(manager, "free").as_float() == 7999);
        call(list, "append", {file});
        CHECK(call(slides, "saveNew", {manager, script::Value::string("Slides")}).to_string() == "OK");
        CHECK(menu::slide_folders(*f.rt).size() == 1);
        CHECK(call(file, "remove", {manager}).as_float() != 0 && card.list('P').empty());
    }
    std::filesystem::remove_all(dir);
}

TEST(disc_photo_music_list) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    f.rt->boot();
    const script::Value game = f.rt->vm().lookup("game");
    const script::Value list = f.rt->vm().call(f.rt->vm().attribute(game, "getAllMusicList"), {});
    const std::vector<script::Value>* rows = list.items();
    CHECK(rows && rows->size() == 65);
    if (rows && !rows->empty()) {
        const std::vector<script::Value>* first = (*rows)[0].items();
        CHECK(first && first->size() == 4 && !(*first)[0].to_string().empty() && (*first)[3].as_float() == 0);
    }
}
