// Mod makers (formats/db/mod_makers.*): the maker file, its numbers after the program's lists, its cars, and its dealer on its
// country's page of Gran Turismo mode with its pictures and name.

#include "test_framework.h"
#include "menu_run_flow.h"

#include "gt4r/formats/db/mod_cars.h"
#include "gt4r/formats/db/mod_makers.h"
#include "gt4r/menu/car_database.h"
#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/program_tables.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/menu/save_editor.h"

#include "base/file.h"

#include <algorithm>
#include <filesystem>

using namespace gt4r;

namespace {

// The file written in a mod's folder (<tmp>/gt4r_test_mod_makers/<sub>/<name>), "<mod>\t<path>" as the file layer lists it.
std::string write_file(const std::string& sub, const std::string& name, const std::string& text) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r_test_mod_makers" / sub;
    std::filesystem::create_directories(dir);
    const std::string path = (dir / name).string();
    gt3r::write_host_file(path, ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()));
    return "maker_mod\t" + path + "\n";
}

const char* const kMaker = "[maker]\nname = Voltra\ncountry = US\nicon = images/voltra_icon.png\nlogo = images/voltra_logo.png\n";
const char* const kCar = "[car]\nlabel = voltra_one_26\nname = Voltra One `26\nmaker = Voltra\nyear = 2026\nprice = 45000\ndonor = impreza_wrx_sti_00\n";

std::string the_list() { return write_file("makers", "voltra.ini", kMaker) + write_file("cars", "voltra_one_26.ini", kCar); }

menu::Runtime::Config menus_config(Disc& d) {
    menu::Runtime::Config config;
    config.language = "US";
    config.list = [&d](const std::string& folder) {
        std::vector<std::string> out;
        for (const gt3r::Volume::Entry& e : d.volume().entries()) {
            if (e.path.compare(0, folder.size(), folder) != 0) continue;
            const size_t slash = e.path.find('/', folder.size());
            std::string name = e.path.substr(folder.size(), slash == std::string::npos ? slash : slash + 1 - folder.size());
            if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
        }
        return out;
    };
    config.program = [&d] { return d.read_program(); };
    return config;
}
menu::FileReader reader(Disc& d, const std::string& list) {
    return [&d, list](const std::string& path) -> std::optional<Bytes> {
        if (path == "mods/.cars") return Bytes(list.begin(), list.end());
        const gt3r::Volume::Entry* e = d.volume().find(path);
        return e ? std::optional<Bytes>(d.volume().read(*e)) : std::nullopt;
    };
}
std::unique_ptr<menu::Runtime> menus_with(Disc& d, const std::string& list) { return std::make_unique<menu::Runtime>(reader(d, list), menus_config(d)); }

} // namespace

TEST(mod_maker_file) {
    std::string error;
    std::optional<db::ModMakerDef> m = db::parse_mod_maker(kMaker, "Voltra", &error);
    CHECK(m && m->key == "voltra" && m->name == "Voltra" && m->country == "US" && m->icon == "images/voltra_icon.png");
    m = db::parse_mod_maker("[maker]\nname = Tesla\nregion = gr\n", "tesla", &error); // (GT3 Revisited's regions)
    CHECK(m && m->country == "DE");
    CHECK(!db::parse_mod_maker("[maker]\nname = X\n", "x", &error) && error.find("country") != std::string::npos);
    CHECK(!db::parse_mod_maker("[maker]\ncountry = US\n", "x", &error));
    CHECK(!db::parse_mod_maker(kMaker, "bad key!", &error));
    // Numbered after the program's lists; a key of the game's own makers is none.
    const std::map<int, std::string> makers = {{0, "nothing"}, {3, "alfaromeo"}, {78, "au_ford"}}, tuners = {{3, "alfaromeo"}, {96, "proto"}},
                                       countries = {{2, "JP"}, {3, "US"}};
    const std::string list = the_list() + write_file("makers", "alfaromeo.ini", "[maker]\nname = Alfa\ncountry = IT\n") + write_file("makers", "kia.ini", "[maker]\nname = Kia\ncountry = KR\n");
    const db::ModMakers mm = db::ModMakers::read(list, makers, tuners, countries);
    CHECK(mm.makers().size() == 1 && mm.statuses().size() == 3);
    if (!mm.makers().empty()) {
        const db::ModMakerDef& v = mm.makers()[0];
        CHECK(v.key == "voltra" && v.maker == 79 && v.tuner == 97 && v.country_number == 3);
        CHECK(mm.named("VOLTRA") == &v && mm.by_maker(79) == &v);
        const std::optional<db::ModCars::Picture> icon = mm.picture("image/piece/tuner_logo2/voltra.png"), logo = mm.picture("piece/tuner_logo/voltra.img");
        CHECK(icon && logo && icon->png.size() > 20 && icon->png.compare(icon->png.size() - 22, 22, "images/voltra_icon.png") == 0 && logo->png.find("voltra_logo") != std::string::npos);
        CHECK(!mm.picture("piece/tuner_logo/toyota.img"));
    }
}

// The maker's car is its car (its maker, brand and country), at its dealer on the USA page, the dealer's page with its logo,
// its car for sale there; the pages and the app name it.
TEST(disc_mod_maker_dealer) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("no Gran Turismo 4 disc");
    std::unique_ptr<menu::Runtime> rt = menus_with(*disc, the_list());
    auto press = [&](std::initializer_list<menu::Key> keys) {
        for (menu::Key k : keys) rt->key(k), rt->settle();
    };
    auto page = [&] { return rt->current_page() ? rt->current_page()->name : std::string(); };
    auto focus = [&] { return rt->focused() ? rt->focused()->name : std::string(); };
    menu::GameState& game = rt->game();
    game.card.has_game_data = true, game.username = "Player", game.money = 500000;
    rt->boot();
    rt->start_project(rt->first_project());
    rt->settle();
    if (page() == "MovieRoot") press({menu::Key::Start});
    CHECK(rt->finished() && rt->advance());
    rt->settle();
    press({menu::Key::Down, menu::Key::Accept});
    CHECK(rt->finished() && rt->wanted_project() == "gtmode" && rt->advance());
    rt->settle();
    // The car's row: the maker's numbers and country.
    menu::Database& d = menu::natives_of(*rt)->db();
    const db::ModMakerDef* v = d.db ? d.db->mod_makers().by_key("voltra") : nullptr;
    CHECK(v != nullptr);
    if (!v) return;
    const ByteSpan row = d.car_row("voltra_one_26");
    CHECK(row.size() > 0x10 && row[0x0F] == v->maker && row[0x10] == v->tuner && v->country_number >= 0 && row[0x0E] == v->country_number);
    CHECK(d.names.makers[v->maker] == "voltra" && d.names.tuners[v->tuner] == "voltra");
    std::printf("  voltra: maker %d, tuner %d, country %d\n", v->maker, v->tuner, v->country_number);
    // The USA page lists it after the disc's dealers.
    const script::Value usa = rt->vm().lookup("GTmodeProject::NationData::usa_dealers");
    CHECK(usa.items() && !usa.items()->empty() && usa.items()->back().as_string() == "voltra");
    const script::Value list = rt->vm().lookup("GTmodeProject::NewCarData::voltra_carlist");
    CHECK(list.items() && list.items()->size() == 1 && (*list.items())[0].as_string() == "voltra_one_26");
    rt->set_focus(*rt->current_page(), rt->current_page()->child("ScrollWindow")->child("clip")->child("space")->child("DealerUSA"));
    press({menu::Key::Accept});
    CHECK(page() == "NationRoot");
    std::shared_ptr<menu::LiveWidget> item;
    std::function<void(menu::LiveWidget&)> find = [&](menu::LiveWidget& w) {
        for (const auto& c : w.children) {
            if (item) return;
            if (c->name == "voltra") item = c;
            else find(*c);
        }
    };
    find(*rt->current_page());
    CHECK(item != nullptr);
    if (!item) return;
    const std::shared_ptr<menu::LiveWidget> title = item->child("title");
    CHECK(title && title->get("image_path").to_string() == "image/piece/tuner_logo2/voltra.png");
    rt->set_focus(*rt->current_page(), item);
    press({menu::Key::Accept});
    CHECK(page() == "DealerRoot" && focus() == "New");
    const std::shared_ptr<menu::LiveWidget> logo = rt->current_page()->child("Logo");
    CHECK(logo && logo->get("image_path").to_string() == "piece/tuner_logo/voltra.img");
    const std::optional<db::ModPicture> p = d.db->mod_picture("piece/tuner_logo/voltra.img");
    CHECK(p && p->png.find("voltra_logo.png") != std::string::npos);
    press({menu::Key::Accept});
    CHECK(page() == "BuyCarRoot");
    // The pages' and the app's name of the maker (the car picker's and the save editor's makers).
    CHECK(rt->translate(rt->vm().lookup("GTmodeProject::NationRoot"), "TunerName", "voltra") == "Voltra");
    menu::SaveEditor editor(reader(*disc, the_list()), menus_config(*disc));
    editor.start(menu::GameState{});
    const auto car = std::find_if(editor.cars().begin(), editor.cars().end(), [](const menu::SaveEditor::CarKind& k) { return k.label == "voltra_one_26"; });
    CHECK(car != editor.cars().end() && car->maker == "Voltra" && car->maker_key == "voltra");
    const menu::CarDatabase cdb(reader(*disc, the_list()), menus_config(*disc));
    const menu::CarDatabase::Car* c = cdb.car("voltra_one_26");
    CHECK(c && c->maker == "Voltra");
    if (c) std::printf("  the Car database: %s, %s, %s\n", c->name.c_str(), c->maker.c_str(), c->country.c_str());
    bool clean = true;
    for (const std::string& line : rt->log())
        if (line.find("script error") != std::string::npos) clean = false, std::printf("  %s\n", line.c_str());
    CHECK(clean);
}
