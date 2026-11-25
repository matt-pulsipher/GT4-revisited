// The game without a memory card to be missing (menu/no_card.h): the boot, a new game, several of them, saving by itself, what is
// told when the folder cannot be written; both games.

#include "menu_run_flow.h"

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/no_card.h"
#include "gt4r/save/card_image.h"
#include "gt4r/save/save_library.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using script::Value;

namespace {

fs::path fresh(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::permissions(dir, fs::perms::owner_all, ec);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

struct NoCard : Flow {
    NoCard(Disc& d, const fs::path& dir, const std::string& new_card = {})
        : Flow(d, d.info().game == Game::Prologue ? "JP" : "US", dir.string(), false, new_card) {}
    std::vector<std::string> dialogs; // the dialogs seen
    void see() {
        if (page() == "ConfirmDialog") {
            const auto text = rt->current_page()->child("Dialog")->child("Message")->child("message");
            dialogs.push_back(text->data.text("text"));
        }
    }
    // From power on to the title page: no question on the way.
    bool title() {
        rt->boot();
        rt->start_project(rt->first_project());
        for (int i = 0; i < 400 && page() != "MovieRoot" && !rt->finished(); ++i) rt->update(), see();
        rt->settle();
        if (page() == "MovieRoot") press({menu::Key::Start});
        if (!rt->finished() || rt->wanted_project() != "arcade" || !rt->advance()) return false;
        rt->settle();
        return page() == "TopRoot" && dialogs.empty();
    }
    // A name of one letter entered, to the title again.
    bool enter_name() {
        if (page() != "EntryRoot") return false;
        press({menu::Key::Up, menu::Key::Left, menu::Key::Accept, menu::Key::Right, menu::Key::Down});
        if (focus() != "key_enter") return false;
        rt->key(menu::Key::Accept);
        rt->settle();
        see(); // "The name entered is": the game's own question
        press({menu::Key::Accept});
        for (int i = 0; i < 40 && !rt->finished() && page() != "MovieRoot"; ++i) rt->settle(), see();
        if (page() == "MovieRoot") press({menu::Key::Start});
        if (!rt->finished() || !rt->advance()) return false;
        rt->settle();
        return page() == "TopRoot";
    }
};

Disc* prologue_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::Prologue) return d.get();
    return nullptr;
}

} // namespace

TEST(menu_card_text_templates) {
    CHECK(menu::matches_template("A 12KB b", "A %dKB b") && menu::matches_template("A 12KB b 3", "A %dKB b %d") && menu::matches_template("same", "same"));
    CHECK(!menu::matches_template("A 12KB c", "A %dKB b") && !menu::matches_template("same!", "same") && !menu::matches_template("B 12KB b", "A %dKB b"));
    CHECK(menu::matches_template("x at y in z", "x at %s in z") && !menu::matches_template("x at y", "x at %s in z"));
}

TEST(disc_menu_no_card_new_game) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const fs::path dir = fresh("gt4r-test-nocard");
    NoCard f(*disc, dir);
    CHECK(f.title());
    for (const std::string& d : f.dialogs) std::printf("  shown: %s\n", d.c_str());
    f.press({menu::Key::Down, menu::Key::Accept});
    CHECK(f.rt->finished() && f.rt->advance());
    f.rt->settle();
    const bool named = f.enter_name();
    for (const std::string& d : f.dialogs) std::printf("  shown: %s\n", d.c_str());
    if (!named)
        for (const auto& line : f.rt->log()) std::printf("  %s\n", line.c_str());
    CHECK(named && f.dialogs.size() == 1);
    CHECK(fs::exists(dir / "memorycard" / "BASCUS-97328GAMEDATA" / "BASCUS-97328GAMEDATA") && f.rt->game().valid_game_data);
    CHECK(f.clean());
}

// With a game: loaded at the boot, Gran Turismo mode at once; saved by itself when it has changed; a second game beside it; the
// other loaded; the title's Save Game is the saved games' page.
TEST(disc_menu_no_card_games) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const fs::path dir = fresh("gt4r-test-nocard2");
    {
        NoCard f(*disc, dir);
        CHECK(f.title());
        f.press({menu::Key::Down, menu::Key::Accept});
        CHECK(f.rt->finished() && f.rt->advance());
        f.rt->settle();
        CHECK(f.enter_name() && f.rt->game().username == "P");
    }
    save::SaveLibrary lib(dir.string(), "BASCUS-97328");
    const std::string first = lib.current();
    save::Codes codes;
    {
        NoCard f(*disc, dir);
        CHECK(f.title() && f.rt->game().valid_game_data && f.rt->game().username == "P" && f.dialogs.empty());
        codes = menu::natives_of(*f.rt)->codes();
        // The title's Save Game.
        for (int i = 0; i < 6 && f.focus() != "Save"; ++i) f.press({menu::Key::Down});
        CHECK(f.focus() == "Save");
        f.press({menu::Key::Accept});
        CHECK(f.rt->take_saves_asked() && !f.rt->take_saves_asked() && f.page() == "TopRoot");
        for (int i = 0; i < 6 && f.focus() != "GTmode"; ++i) f.press({menu::Key::Up});
        f.press({menu::Key::Accept});
        CHECK(f.rt->finished() && f.rt->wanted_project() == "gtmode" && f.rt->advance());
        f.rt->settle();
        CHECK(f.page() == "TopRoot" && f.dialogs.empty());
        // A change, then the next page: saved, with no question.
        const u64 mark = lib.change_mark(first);
        f.rt->game().money = 123456;
        f.press({menu::Key::Accept});
        f.rt->settle(), f.see();
        CHECK(lib.change_mark(first) != mark && f.dialogs.empty());
        for (const auto& line : f.rt->log())
            if (line.rfind("autosave", 0) == 0) std::printf("  %s\n", line.c_str());
        const std::vector<save::SavedGame> games = lib.games(codes);
        CHECK(games.size() == 1 && games[0].readable && games[0].current && games[0].name == "P" && games[0].money == 123456 && games[0].day == 1);
        // No other game: the slot 2 of a console.
        script::Vm& vm = f.rt->vm();
        const Value args[2] = {Value::string("MODE_LOAD_GAME"), Value::integer(1)};
        const Value manager = vm.call(vm.lookup("menu::MMemoryCardManager"), args);
        CHECK(vm.call(vm.attribute(manager, "isConnect"), {}).i == 0);
        const std::optional<menu::CardAnswer> a = menu::card_answer(*f.rt, *f.rt->project()->text().find("MemoryCard", "no_memory_card_on_slot_2"));
        CHECK(a && a->answer == 0 && a->shown == "There is no other saved game.");
        CHECK(!menu::card_answer(*f.rt, "Do you really want to quit Gran Turismo Mode?"));
        CHECK(f.clean());
    }
    {
        // A second game: in its own folder, the first as it was.
        const u64 mark = lib.change_mark(first);
        const std::string second = lib.new_card();
        NoCard f(*disc, dir, second);
        f.rt->boot();
        f.rt->game().next_menu_arg = "EntryRoot";
        f.rt->start_project("boot");
        f.rt->settle();
        CHECK(f.enter_name() && f.rt->game().valid_game_data && f.rt->game().money == 10000);
        CHECK(lib.current() == second && second != first && lib.cards().size() == 2 && lib.change_mark(first) == mark);
        // The other one is what slot 2 is.
        script::Vm& vm = f.rt->vm();
        const Value args[2] = {Value::string("MODE_LOAD_GAME"), Value::integer(1)};
        const Value manager = vm.call(vm.lookup("menu::MMemoryCardManager"), args);
        CHECK(vm.call(vm.attribute(manager, "isConnect"), {}).i == 1);
        const Value file = vm.call(vm.attribute(manager, "getNewFile"), {});
        const Value instance[1] = {manager};
        const Value loaded = vm.call(vm.attribute(file, "loadInstance"), instance);
        CHECK(loaded.items() && loaded.items()->size() == 2 && (*loaded.items())[0].to_string() == "OK" && !(*loaded.items())[1].is_nil());
        CHECK(f.clean());
    }
    // The first one again.
    CHECK(lib.set_current(first));
    NoCard f(*disc, dir);
    f.title(); // (saved on Gran Turismo mode's map: the boot leads there, as the program's does)
    CHECK(f.dialogs.empty() && f.rt->wanted_project() == "gtmode" && f.rt->game().money == 123456);
    fs::remove_all(dir);
}

// A folder that cannot be written: said once, with the folder; a game that cannot be read: said once at the boot, which goes on,
// and stays as it is when a new one is made.
TEST(disc_menu_no_card_failures) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const fs::path dir = fresh("gt4r-test-nocard3");
    {
        NoCard f(*disc, dir);
        CHECK(f.title());
        f.press({menu::Key::Down, menu::Key::Accept});
        CHECK(f.rt->finished() && f.rt->advance());
        f.rt->settle();
        CHECK(f.enter_name());
        f.press({menu::Key::Down, menu::Key::Accept});
        for (int i = 0; i < 6 && !f.rt->finished(); ++i) f.press({menu::Key::Down, menu::Key::Accept});
        CHECK(f.rt->finished() && f.rt->advance());
        f.rt->settle();
        const fs::path folder = dir / "memorycard" / "BASCUS-97328GAMEDATA";
        fs::permissions(folder, fs::perms::owner_read | fs::perms::owner_exec);
        f.dialogs.clear();
        f.rt->game().money = 5;
        f.press({menu::Key::Accept});
        f.rt->settle(), f.see();
        CHECK(f.dialogs.size() == 1);
        if (!f.dialogs.empty()) std::printf("  shown: %s\n", f.dialogs[0].c_str());
        f.press({menu::Key::Accept});
        f.rt->game().money = 6;
        f.press({menu::Key::Cancel});
        f.rt->settle();
        CHECK(f.page() != "ConfirmDialog");
        fs::permissions(folder, fs::perms::owner_all);
    }
    // The file cut short.
    const fs::path file = dir / "memorycard" / "BASCUS-97328GAMEDATA" / "BASCUS-97328GAMEDATA";
    std::ofstream(file, std::ios::binary | std::ios::trunc) << "no game";
    {
        NoCard f(*disc, dir);
        f.rt->boot();
        f.rt->start_project(f.rt->first_project());
        for (int i = 0; i < 400 && f.page() != "ConfirmDialog" && f.page() != "MovieRoot"; ++i) f.rt->update();
        f.rt->settle(), f.see();
        CHECK(f.dialogs.size() == 1);
        if (f.dialogs.empty())
            for (const auto& line : f.rt->log()) std::printf("  %s\n", line.c_str());
        if (!f.dialogs.empty()) std::printf("  shown: %s\n", f.dialogs[0].c_str());
        f.press({menu::Key::Accept});
        for (int i = 0; i < 400 && f.page() != "MovieRoot" && !f.rt->finished(); ++i) f.rt->update();
        f.rt->settle();
        f.press({menu::Key::Start});
        CHECK(f.rt->finished() && f.rt->advance());
        f.rt->settle();
        CHECK(f.page() == "TopRoot" && !f.rt->game().valid_game_data);
        f.press({menu::Key::Down, menu::Key::Accept});
        CHECK(f.rt->finished() && f.rt->advance());
        f.rt->settle();
        CHECK(f.enter_name() && fs::file_size(file) == 7);
        const save::SaveLibrary lib(dir.string(), "BASCUS-97328");
        CHECK(lib.cards().size() == 2 && lib.current() != (dir / "memorycard").string());
        CHECK(f.clean());
    }
    fs::remove_all(dir);
}

// Every text a rule names is the disc's, in every project that has the card's texts, and is answered with its numbers in it.
TEST(disc_menu_no_card_texts) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const fs::path dir = fresh("gt4r-test-nocard4");
    NoCard f(*disc, dir);
    f.rt->boot();
    for (const char* project : {"boot", "arcade", "gtmode", "labomode", "photo_shoot"}) {
        f.rt->start_project(project);
        const menu::TextTable& text = f.rt->project()->text();
        const auto* page = text.page("MemoryCard");
        CHECK(page != nullptr);
        if (!page) continue;
        int answered = 0, left = 0;
        for (const auto& [key, value] : *page) {
            std::string message = value;
            for (size_t at; (at = message.find("%d")) != std::string::npos;) message.replace(at, 2, "12");
            if (menu::card_answer(*f.rt, message)) ++answered;
            else ++left;
        }
        std::printf("  %s: %d of the card's texts are answered, %d shown\n", project, answered, left);
        CHECK(answered >= 25);
        // No text left to the player asks for a card.
        for (const auto& [key, value] : *page)
            if (value.find("Insert a memory card") != std::string::npos || value.find("o memory card") != std::string::npos) {
                std::string message = value;
                for (size_t at; (at = message.find("%d")) != std::string::npos;) message.replace(at, 2, "12");
                if (!menu::card_answer(*f.rt, message)) std::printf("    left: %s\n", key.c_str());
            }
    }
    fs::remove_all(dir);
}

// Prologue: no question at the boot; its save and load items are the saved games' page; its first progress makes the game.
TEST(disc_prologue_no_card) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    const fs::path dir = fresh("gt4r-test-nocard5");
    const save::SaveLibrary lib(dir.string(), "BISCPS-15055");
    auto board = [](NoCard& f) {
        f.rt->boot();
        f.rt->start_project(f.rt->first_project());
        for (int i = 0; i < 8; ++i) {
            f.rt->settle();
            if (f.page() == "SchoolRoot") return true;
            if (f.rt->finished()) f.rt->advance();
            else f.press({menu::Key::Start});
        }
        f.rt->settle();
        return f.page() == "SchoolRoot";
    };
    {
        NoCard f(*disc, dir);
        CHECK(board(f));
        std::printf("  at %s of %s\n", f.page().c_str(), f.rt->project() ? f.rt->project()->name().c_str() : "-");
        CHECK(lib.cards().empty());
        // A name in the license record: the game's first change.
        script::Vm& vm = f.rt->vm();
        const Value license = vm.attribute(vm.lookup("game"), "license");
        const Value name[1] = {Value::string("XYZ")};
        vm.call(vm.attribute(license, "setDefaultName"), name);
        menu::autosave(*f.rt); // (as at the next page)
        // A state no boot has set up is not saved over a game.
        NoCard bare(*disc, dir);
        bare.rt->boot();
        bare.rt->start_project("prologue");
        bare.rt->settle();
        const u64 mark = lib.change_mark(lib.current());
        menu::autosave(*bare.rt), menu::autosave(*bare.rt);
        CHECK(mark != 0 && lib.change_mark(lib.current()) == mark && !menu::save_game_now(*bare.rt).empty());
        CHECK(lib.cards().size() == 1 && lib.current() == lib.cards()[0]);
        CHECK(f.clean());
    }
    NoCard f(*disc, dir);
    CHECK(board(f) && f.rt->game().username == "XYZ");
    f.rt->game().next_menu_arg = "save_game";
    f.rt->start_project("memcard");
    f.rt->settle();
    CHECK(f.rt->take_saves_asked() && f.rt->project()->name() != "memcard");
    fs::remove_all(dir);
}
