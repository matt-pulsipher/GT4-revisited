// The menu faces with content of their own: their settings and rules on made-up layouts, then
// what they show from the discs of GT4R_DISC.

#include "test_framework.h"

#include "gt4r/menu/faces_movie.h"
#include "gt4r/menu/layout.h"
#include "gt4r/menu/project.h"
#include "gt4r/menu/runtime.h"
#include "video/ipu.h"
#include "video/movie.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <functional>
#include <set>

using namespace gt4r;

namespace menu_draw_test {

menu::FileReader reader(Disc& d) {
    return [&d](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = d.volume().find(path);
        if (!e) return std::nullopt;
        return d.volume().read(*e);
    };
}
menu::FolderLister lister(Disc& d) {
    return [&d](const std::string& folder) {
        std::vector<std::string> out;
        for (const gt3r::Volume::Entry& e : d.volume().entries()) {
            if (e.path.compare(0, folder.size(), folder) != 0) continue;
            const size_t slash = e.path.find('/', folder.size());
            std::string name = e.path.substr(folder.size(), slash == std::string::npos ? slash : slash + 1 - folder.size());
            if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
        }
        return out;
    };
}
// The disc's main script run as at power on (the configuration's values, MpegRoot).
std::unique_ptr<menu::Runtime> booted(Disc& d) {
    menu::Runtime::Config config;
    config.list = lister(d);
    const std::vector<std::string> languages = menu::project_languages(config.list, "boot");
    if (!languages.empty()) config.language = languages[0];
    config.program = [&d] { return d.read_program(); };
    auto rt = std::make_unique<menu::Runtime>(reader(d), config);
    rt->boot();
    return rt;
}
void each_widget(const menu::Widget& w, const std::function<void(const menu::Widget&)>& f) {
    f(w);
    for (const menu::Widget& c : w.children()) each_widget(c, f);
}

} // namespace menu_draw_test

using namespace menu_draw_test;

// A movie face's settings: the program's defaults where the layout says nothing, the layout's
// and the scripts' values over them.
TEST(menu_movie_face_settings) {
    const menu::Widget none = menu::parse_layout(std::string_view(R"(MovieFace { name string {"Mpeg"} })"));
    const menu::MovieFaceSettings d = menu::movie_face_settings(none);
    CHECK(d.movie_name.empty() && d.pixel_width == 640 && d.pixel_height == 0);
    CHECK(d.interlace && !d.audio && d.repeat && !d.ac3 && d.audio_track == 0);
    const menu::Widget set = menu::parse_layout(std::string_view(
        R"(MovieFace { name string {"Mpeg"} movie_name string {"opening_us"} audio digit {1} repeat digit {0} interlace digit {0} AC3 digit {1} audio_track digit {2} })"));
    const menu::MovieFaceSettings s = menu::movie_face_settings(set);
    CHECK(s.movie_name == "opening_us" && s.audio && !s.repeat && !s.interlace && s.ac3 && s.audio_track == 2);

    CHECK(menu::movie_path("main", "start_game") == "mpeg/main/start_game.pss");
    CHECK(menu::movie_path("", "category") == "mpeg/preview/category.pss");
    CHECK(menu::movie_stills_path("main") == "mpeg/main/course.ipic");
}

// What the per-picture update tells the scripts: a frame hook on its frame, the end once and
// only for a movie that does not repeat.
TEST(menu_movie_face_events) {
    menu::MovieFaceSettings once;
    once.repeat = false;
    menu::MovieFaceSettings loop;
    CHECK(!menu::movie_face_events(once, 10, -1, false, false).hook);
    CHECK(menu::movie_face_events(once, 90, 90, false, false).hook);
    CHECK(!menu::movie_face_events(once, 89, 90, false, false).hook);
    CHECK(menu::movie_face_events(once, 200, -1, true, false).finished);
    CHECK(!menu::movie_face_events(once, 200, -1, true, true).finished);
    CHECK(!menu::movie_face_events(loop, 200, -1, true, false).finished);
}

// Every movie a face names in a layout is on the disc under the configuration's MpegRoot, the
// movies play from their first picture, and the stills the scripts load name the movies.
TEST(disc_menu_movies) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        std::unique_ptr<menu::Runtime> rt = booted(*disc);
        const script::Value root_value = rt->vm().lookup("MpegRoot");
        CHECK(root_value.kind == script::Kind::String);
        const std::string root = root_value.kind == script::Kind::String ? root_value.as_string() : std::string();
        gt3r::Vfs vfs(&disc->volume());
        std::set<std::string> named;
        size_t faces = 0;
        const menu::FolderLister list = lister(*disc);
        for (const std::string& name : menu::list_projects(list)) {
            const std::vector<std::string> languages = menu::project_languages(list, name);
            if (languages.empty()) continue;
            menu::Project project(reader(*disc), name, languages[0]);
            for (const menu::Widget& page : project.pages())
                each_widget(page, [&](const menu::Widget& w) {
                    if (w.type != "MovieFace") return;
                    ++faces;
                    const menu::MovieFaceSettings s = menu::movie_face_settings(w);
                    if (s.movie_name.empty()) return; // named by the scripts
                    named.insert(s.movie_name);
                    CHECK(vfs.exists(menu::movie_path(root, s.movie_name)));
                });
        }
        std::printf("  %s: MpegRoot \"%s\", %zu movie faces, %zu movies named in layouts\n", disc->info().serial.c_str(), root.c_str(), faces, named.size());
        CHECK(faces > 0);
        // The stills.
        const std::optional<Bytes> stills = vfs.read(menu::movie_stills_path(root));
        CHECK(stills.has_value());
        if (stills) {
            const std::vector<gt3r::video::IpuArchiveEntry> entries = gt3r::video::parse_ipua(*stills);
            CHECK(!entries.empty());
            if (!entries.empty()) {
                const gt3r::video::IpuArchiveEntry& e = entries.front();
                const gt3r::video::Frame f = gt3r::video::decode_ipum(ByteSpan(stills->data() + e.offset, e.size));
                std::printf("  stills: %zu, first %s %dx%d\n", entries.size(), e.name.c_str(), f.width, f.height);
                CHECK(f.width > 0 && f.height > 0);
            }
            for (const std::string& n : named) {
                const bool has = std::any_of(entries.begin(), entries.end(), [&](const auto& e) { return e.name == n; });
                if (!has) std::printf("  no still for %s\n", n.c_str());
            }
        }
        // The title's movie decodes from its first picture.
        if (!named.empty()) {
            std::unique_ptr<gt3r::video::Movie> movie = gt3r::video::open_movie(vfs, menu::movie_path(root, *named.begin()));
            gt3r::video::Frame f;
            CHECK(movie->next_frame(f) && f.width == 640);
        }
    }
}
