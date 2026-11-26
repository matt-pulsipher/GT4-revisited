// The earlier games' saves the Status page's transfer reads (save/earlier_game.h, menu/earlier_saves.h): Gran Turismo 3's data
// file as the program reads it, GT3 Revisited's saves where it keeps them, card images, Prologue's games; the program's rules;
// and the scripts' own flow on the Status page with them. Made-up saves only.

#include "menu_run_flow.h"

#include "gt4r/menu/earlier_saves.h"
#include "gt4r/menu/game_internal.h"
#include "gt4r/save/earlier_game.h"

#include "formats/ps2/memcard.h"
#include "game/gt_save.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using script::Value;

namespace {

fs::path fresh(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void put(const fs::path& path, ByteSpan b) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}
void put(const fs::path& path, const std::string& s) { put(path, ByteSpan(reinterpret_cast<const u8*>(s.data()), s.size())); }

// A made-up Gran Turismo 3 data file in the layout the program reads (a 0x40-byte header, the payload inverted): `cars` cars,
// one wheel, the credits, the name, the first 16 license tests' medals.
Bytes gt3_file(s64 credits, const std::vector<int>& medals, u32 cars = 2, u16 minor = 1, const std::string& name = "Tester") {
    const size_t licenses = 0x130 + cars * 0x204 + 0x44;
    Bytes p(licenses + 48 * 0x154, 0);
    auto put32 = [&p](size_t at, u32 v) { std::memcpy(&p[at], &v, 4); };
    std::memcpy(&p[0x10], &credits, 8);
    put32(0x30, cars), put32(0x34, 1);
    std::memcpy(&p[0x70], name.data(), name.size());
    for (size_t k = 0; k < 48; ++k) put32(licenses + k * 0x154, k < medals.size() ? u32(s32(medals[k])) : ~0u);
    Bytes f(0x40, 0);
    const u32 header = 0x40, size = u32(p.size());
    std::memcpy(&f[0], &header, 4), std::memcpy(&f[4], &size, 4);
    f[8] = u8(minor), f[0xA] = 7;
    for (u8 b : p) f.push_back(u8(~b));
    return f;
}

std::vector<int> all(int medal, size_t n = 16) { return std::vector<int>(n, medal); }

// A GT3 Revisited save of its own format, made up.
std::string gt3r_save(s64 credits, const std::vector<int>& medals, const std::string& name) {
    gt3r::game::GameState s;
    s.credits = credits, s.file_comment = name, s.player_name = "Driver";
    for (size_t k = 0; k < medals.size(); ++k) s.license_tests[k].medal = gt3r::game::Medal(medals[k]);
    return gt3r::game::save_to_text(s);
}

// A made-up Prologue game data file: its header and 25 license units with these medals (0 none .. 3 gold).
Bytes prologue_file(const std::vector<int>& medals, const std::string& name = "PRO") {
    Bytes f(0x30 + 48 * 0x2E0 + 0x10, 0);
    const u32 magic = 0xFF60BAD0u;
    std::memcpy(&f[0], &magic, 4);
    std::memcpy(&f[0x20], name.data(), name.size());
    for (size_t k = 0; k < medals.size(); ++k) f[0x34 + k * 0x2E0] = u8(medals[k]);
    return f;
}

} // namespace

TEST(earlier_gt3_data_file_as_the_program_reads_it) {
    std::string why;
    // Credits: at most 100,000; none: nothing to take; Japan's yen / 100.
    std::vector<int> medals = all(2, 8);
    medals.insert(medals.end(), 7, 0), medals.push_back(-1); // B all passed, A's last test not
    const std::optional<save::EarlierGame> g = save::read_gt3_data_file(gt3_file(946'020, medals), "us", &why);
    CHECK(g && g->kind == save::EarlierGame::Kind::Gt3 && g->credits == 946'020 && g->name == "Tester" && g->medals.size() == 16);
    CHECK(save::transferred_credits(*g, "us") == 100'000 && save::transferred_licenses(*g, "us") == 1);
    const std::optional<save::EarlierGame> few = save::read_gt3_data_file(gt3_file(2'650, all(0), 5), "us");
    CHECK(few && save::transferred_credits(*few, "us") == 2'650 && save::transferred_licenses(*few, "us") == 2);
    CHECK(!save::transferred_credits(*save::read_gt3_data_file(gt3_file(0, all(-1)), "us"), "us"));
    CHECK(save::transferred_licenses(*save::read_gt3_data_file(gt3_file(10, all(-1)), "us"), "us") == 0);
    const std::optional<save::EarlierGame> yen = save::read_gt3_data_file(gt3_file(5'000'000, all(1), 1, 0), "jp");
    CHECK(yen && save::transferred_credits(*yen, "jp") == 50'000);
    // Another release's: not this disc's (its folder is another).
    CHECK(!save::read_gt3_data_file(gt3_file(10, all(0), 1, 2), "us", &why) && why.find("European") != std::string::npos);
    CHECK(!save::read_gt3_data_file(Bytes(0x400, 0), "us", &why));
    // The folders by region (0x1D4500, 0x1D4598).
    CHECK(save::gt3_folder("us") == "BASCUS-97102GAMEDATA" && save::gt3_folder("uk") == "BESCES-50294GAMEDATA" && save::gt3_folder("jp") == "BISCPS-15009GAMEDATA");
    CHECK(save::license_folder("us") == "BASCUS-97102GAMEDATA" && save::license_folder("jp") == "BISCPS-15055GAMEDATA" && save::license_folder("kr") == "BKSCKA-20022GAMEDATA");
}

TEST(earlier_files_the_player_chooses) {
    std::string why;
    // A card image with the US game and another one's folder.
    gt3r::ps2::McDir game{"BASCUS-97102GAMEDATA", {{"BASCUS-97102GAMEDATA", gt3_file(300'000, all(0))}, {"icon.sys", Bytes(964, 0)}}};
    gt3r::ps2::McDir other{"BASCUS-97328GAMEDATA", {{"BASCUS-97328GAMEDATA", Bytes(100, 1)}}};
    const Bytes card = gt3r::ps2::make_card({other, game});
    std::optional<save::EarlierGame> g = save::read_earlier_file(card, "/x/My card.ps2", "us", &why);
    CHECK(g && g->from == "PS2 memory card image" && g->credits == 300'000 && g->name == "Tester" && g->where == "/x/My card.ps2");
    // Only the European game on it.
    gt3r::ps2::McDir pal{"BESCES-50294GAMEDATA", {{"BESCES-50294GAMEDATA", gt3_file(1, all(0), 1, 2)}}};
    CHECK(!save::read_earlier_file(gt3r::ps2::make_card({pal}), "pal.ps2", "us", &why) && why.find("BESCES-50294GAMEDATA") != std::string::npos);
    std::printf("  a European card: %s\n", why.c_str());
    // A .psu, the data file alone, GT3 Revisited's save.
    CHECK(save::read_earlier_file(gt3r::ps2::write_psu(game), "save.psu", "us") && save::read_earlier_file(gt3r::ps2::write_psu(game), "content://x/7", "us"));
    CHECK(save::read_earlier_file(gt3_file(5, all(0)), "BASCUS-97102GAMEDATA", "us"));
    const std::string text = gt3r_save(1'234'567, all(1), "My GT3");
    g = save::read_earlier_file(ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()), "My GT3.sav", "us");
    CHECK(g && g->from == "GT3 Revisited" && g->name == "My GT3" && save::transferred_credits(*g, "us") == 100'000 && save::transferred_licenses(*g, "us") == 2);
    CHECK(!save::read_earlier_file(Bytes(5000, 7), "photo.jpg", "us", &why) && !why.empty());
}

TEST(earlier_gt3r_saves_where_it_keeps_them) {
    const fs::path dir = fresh("gt4r-test-gt3r");
    // Its library: the US release's in saves/, the others' in saves/eu/ and saves/jp/; the game being played first.
    put(dir / "saves" / "Old.sav", gt3r_save(10, all(0), "Old"));
    put(dir / "saves" / "Played.sav", gt3r_save(20, all(0), "Played"));
    put(dir / "saves" / "current.txt", std::string("Played.sav\n"));
    put(dir / "saves" / "eu" / "Euro.sav", gt3r_save(30, all(0), "Euro"));
    put(dir / "saves" / "broken.sav", std::string("GT3R-GTMODE 1\ncredits x\n"));
    std::string looked;
    std::vector<save::EarlierGame> found = save::find_gt3r_games(dir.string(), "us", &looked);
    CHECK(found.size() == 2 && found[0].name == "Played" && found[0].current && found[1].name == "Old" && looked == (dir / "saves").string() + "/");
    found = save::find_gt3r_games(dir.string(), "uk");
    CHECK(found.size() == 1 && found[0].name == "Euro");
    // The save folder its Settings chose.
    const fs::path chosen = fresh("gt4r-test-gt3r-chosen");
    put(chosen / "saves" / "Synced.sav", gt3r_save(40, all(0), "Synced"));
    put(dir / "config.ini", "[saves]\nfolder = " + chosen.string() + "\n");
    found = save::find_gt3r_games(dir.string(), "us", &looked);
    CHECK(found.size() == 1 && found[0].name == "Synced" && looked.rfind(chosen.string(), 0) == 0);
    // Its first version's single save, before it had a library.
    const fs::path old = fresh("gt4r-test-gt3r-old");
    put(old / "gtmode.sav", gt3r_save(50, all(0), ""));
    found = save::find_gt3r_games(old.string(), "us");
    CHECK(found.size() == 1 && found[0].name == "gtmode" && found[0].credits == 50);
    // A card folder: the game's folder copied from a card, and a card image put there.
    const fs::path card = fresh("gt4r-test-gt3-card");
    put(card / "BASCUS-97102GAMEDATA" / "BASCUS-97102GAMEDATA", gt3_file(60, all(0)));
    put(card / "gt3.ps2", gt3r::ps2::make_card({{"BASCUS-97102GAMEDATA", {{"BASCUS-97102GAMEDATA", gt3_file(70, all(0))}}}}));
    found = save::find_card_games(card.string(), "us");
    CHECK(found.size() == 2 && found[0].credits == 60 && found[1].credits == 70 && found[1].from == "PS2 memory card image");
    for (const fs::path& p : {dir, chosen, old, card}) fs::remove_all(p);
}

TEST(earlier_prologue_games) {
    // The first 6 units passed open B; the first 25, A too (the Japanese program's rule, 0x1D4698).
    std::vector<int> medals = all(1, 6);
    std::optional<save::EarlierGame> g = save::read_prologue_data_file(prologue_file(medals));
    CHECK(g && g->name == "PRO" && save::transferred_licenses(*g, "jp") == 1 && save::transferred_licenses(*g, "us") == 0);
    g = save::read_prologue_data_file(prologue_file(all(3, 25)));
    CHECK(g && save::transferred_licenses(*g, "jp") == 2);
    g = save::read_prologue_data_file(prologue_file(all(0, 25)));
    CHECK(g && save::transferred_licenses(*g, "jp") == 0);
    // Our library's Prologue games count where the program reads Prologue's (not the US program).
    const fs::path dir = fresh("gt4r-test-prologue-lib");
    put(dir / "memorycard" / "BISCPS-15055GAMEDATA" / "BISCPS-15055GAMEDATA", prologue_file(all(2, 25)));
    CHECK(save::find_prologue_games(dir.string(), "jp").size() == 1 && save::find_prologue_games(dir.string(), "us").empty());
    fs::remove_all(dir);
}

namespace {

// Gran Turismo mode without a card (menu/no_card.h), a new game, the Status page.
struct StatusFlow : Flow {
    std::vector<std::string> dialogs; // the dialogs' messages seen
    StatusFlow(Disc& d, const fs::path& dir, const std::string& gt3r) : Flow(d, "US", dir.string(), false) {
        menu::Runtime::Config config = rt->config();
        config.gt3r_dir = gt3r;
        rt = std::make_unique<menu::Runtime>(
            [&d](const std::string& path) -> std::optional<Bytes> {
                const gt3r::Volume::Entry* e = d.volume().find(path);
                return e ? std::optional<Bytes>(d.volume().read(*e)) : std::nullopt;
            },
            config);
    }
    std::string message() {
        if (page() != "ConfirmDialog") return {};
        return rt->current_page()->child("Dialog")->child("Message")->child("message")->data.text("text");
    }
    void see() {
        if (const std::string m = message(); !m.empty() && (dialogs.empty() || dialogs.back() != m)) dialogs.push_back(m);
    }
    // Power on to Gran Turismo mode's map with a new game.
    bool to_map() {
        rt->boot();
        rt->start_project(rt->first_project());
        for (int i = 0; i < 400 && page() != "MovieRoot" && !rt->finished(); ++i) rt->update();
        rt->settle();
        if (page() == "MovieRoot") press({menu::Key::Start});
        if (!rt->finished() || !rt->advance()) return false;
        rt->settle();
        press({menu::Key::Down, menu::Key::Accept});
        if (!rt->finished() || !rt->advance()) return false;
        rt->settle();
        if (page() != "EntryRoot") return false;
        press({menu::Key::Up, menu::Key::Left, menu::Key::Accept, menu::Key::Right, menu::Key::Down});
        rt->key(menu::Key::Accept);
        rt->settle();
        press({menu::Key::Accept});
        for (int i = 0; i < 40 && !rt->finished() && page() != "MovieRoot"; ++i) rt->settle();
        if (page() == "MovieRoot") press({menu::Key::Start});
        if (!rt->finished() || !rt->advance()) return false;
        rt->settle();
        for (int i = 0; i < 6 && focus() != "GTmode"; ++i) press({menu::Key::Down});
        press({menu::Key::Accept});
        if (!rt->finished() || rt->wanted_project() != "gtmode" || !rt->advance()) return false;
        rt->settle();
        return page() == "TopRoot";
    }
    bool to_status() {
        map_focus("Home");
        press({menu::Key::Accept});
        press({menu::Key::Right, menu::Key::Accept});
        return page() == "StatusRoot";
    }
    // Every dialog answered with its focused (or the given) choice until the Status page is back.
    void answer_all(menu::Key key = menu::Key::Accept) {
        for (int i = 0; i < 12 && page() == "ConfirmDialog"; ++i) see(), press({key});
    }
    u32 unit(const std::string& label) {
        const int u = menu::natives_of(*rt)->db().race_unit(label);
        return u < 0 ? 0 : rt->game().race_units[size_t(u)];
    }
};

} // namespace

TEST(disc_earlier_gt3_transfer_on_the_status_page) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const fs::path dir = fresh("gt4r-test-transfer"), gt3r = fresh("gt4r-test-transfer-gt3r");
    put(gt3r / "saves" / "Done.sav", gt3r_save(946'020, all(0), "Done"));
    put(gt3r / "saves" / "Started.sav", gt3r_save(2'650, {2, 1, 2}, "Started"));
    put(gt3r / "saves" / "current.txt", std::string("Started.sav\n"));
    StatusFlow f(*disc, dir, gt3r.string());
    CHECK(f.to_map() && f.to_status());
    menu::set_transfer_chooser(*f.rt, true);
    const s64 money = f.rt->game().money;
    // Start: "Transfer game data". The game asks for the save: the choice is offered (the game being played first).
    f.press({menu::Key::Start});
    std::optional<menu::TransferChoice> c = menu::take_transfer_choice(*f.rt);
    CHECK(c && c->found.size() == 2 && c->found[0].name == "Started" && c->kind == save::EarlierGame::Kind::Gt3 && !menu::take_transfer_choice(*f.rt));
    CHECK(f.page() == "StatusRoot"); // (the scripts wait)
    menu::answer_transfer_choice(*f.rt, c->found[1]);
    f.rt->settle();
    // The licenses' question names the save; yes; then the credits'.
    f.see();
    CHECK(!f.dialogs.empty() && f.dialogs.back().rfind("GT3 game save data: Done\nThe B and A Licenses", 0) == 0);
    std::printf("  asked: %s\n", f.dialogs.back().c_str());
    for (int i = 0; i < 8 && f.page() == "ConfirmDialog"; ++i) {
        f.see();
        if (f.focus() != "Yes" && f.focus() != "OK") f.press({menu::Key::Right});
        f.press({menu::Key::Accept});
    }
    for (const std::string& d : f.dialogs) std::printf("  shown: %s\n", d.c_str());
    CHECK(f.page() == "StatusRoot");
    CHECK(f.rt->game().money == money + 100'000 && f.rt->game().withdrawn && f.rt->game().withdrawn_gt4p);
    // Every B and A test opened as passed ("red", 12), the license's kind (1).
    CHECK((f.unit("l0b0001") & 0xFFF) == 0xCC1 && (f.unit("l0b0016") & 0xFFF) == 0xCC1 && (f.unit("l0a0016") & 0xFFF) == 0xCC1 && (f.unit("l0ib0001") & 0xF) == 0);
    const std::vector<std::string> seen = f.dialogs;
    CHECK(std::find(seen.begin(), seen.end(), "Licenses have now been transferred.") != seen.end() && std::find(seen.begin(), seen.end(), "Credits have been transferred.") != seen.end());
    // Once each: Start again says so, and asks for nothing.
    f.dialogs.clear();
    f.press({menu::Key::Start});
    f.answer_all();
    CHECK(!menu::take_transfer_choice(*f.rt) && f.dialogs.size() == 2 && f.rt->game().money == money + 100'000 && f.clean());
    for (const std::string& d : f.dialogs) std::printf("  again: %s\n", d.c_str());
    // Nothing read from GT3 Revisited's folder was written.
    CHECK(std::filesystem::file_size(gt3r / "saves" / "current.txt") == 12);
    fs::remove_all(dir), fs::remove_all(gt3r);
}
