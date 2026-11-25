// The saved games of a data folder (save/save_library.h): the first in memorycard/, the others in games/, the one played named.

#include "test_framework.h"

#include "gt4r/save/save_library.h"

#include <filesystem>
#include <fstream>

using namespace gt4r;
namespace fs = std::filesystem;

namespace {

fs::path fresh(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void put_game(const std::string& card, const std::string& folder = "BASCUS-97328GAMEDATA") {
    fs::create_directories(fs::path(card) / folder);
    std::ofstream(fs::path(card) / folder / folder) << "x";
}

} // namespace

TEST(save_library_folders) {
    const fs::path dir = fresh("gt4r-test-library");
    save::SaveLibrary lib(dir.string(), "BASCUS-97328"), prologue(dir.string(), "BISCPS-15055");
    const std::string first = (dir / "memorycard").string();
    CHECK(lib.ok() && lib.cards().empty() && lib.current() == first && lib.new_card() == first && lib.change_mark(first) == 0);
    put_game(first);
    CHECK(lib.cards().size() == 1 && lib.current() == first && lib.holds_game(first) && prologue.cards().empty() && prologue.new_card() == first);
    const std::string second = lib.new_card();
    CHECK(second == (dir / "games" / "0002").string());
    // Named before it holds a game: the one played stays.
    CHECK(lib.set_current(second) && lib.current() == first);
    put_game(second);
    CHECK(lib.current() == second && lib.cards().size() == 2 && lib.new_card() == (dir / "games" / "0003").string());
    CHECK(save::SaveLibrary(dir.string(), "BASCUS-97328").current() == second && prologue.current() == first);
    const u64 mark = lib.change_mark(second);
    CHECK(mark != 0 && mark == lib.change_mark(second));
    std::ofstream(fs::path(second) / "BASCUS-97328GAMEDATA" / "BASCUS-97328GAMEDATA") << "longer";
    CHECK(lib.change_mark(second) != mark);
    // Taken away: its own files alone, and the other one is played.
    put_game(second, "BISCPS-15055GAMEDATA");
    CHECK(lib.remove(second) && !lib.remove(second) && lib.current() == first && prologue.holds_game(second));
    CHECK(prologue.remove(second) && !fs::exists(second));
    fs::remove_all(dir);
}

// A name that leads out of the data folder is not followed.
TEST(save_library_marker_stays_inside) {
    const fs::path dir = fresh("gt4r-test-library2");
    save::SaveLibrary lib(dir.string(), "BASCUS-97328");
    fs::create_directories(dir / "games");
    std::ofstream(dir / "games" / "current-BASCUS-97328") << "../elsewhere\n";
    CHECK(lib.current() == (dir / "memorycard").string());
    fs::remove_all(dir);
}
