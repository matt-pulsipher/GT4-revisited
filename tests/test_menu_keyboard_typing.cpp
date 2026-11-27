// Letters typed on a real keyboard into the boot's name entry (menu/keyboard_typing.h): each presses the page's own key, so
// the page's script takes it with its case rule (the first letter a capital, the rest small) and its keys for Backspace
// and Enter.

#include "menu_run_flow.h"

#include "gt4r/menu/keyboard_typing.h"

#include <filesystem>

namespace fs = std::filesystem;

TEST(disc_menu_name_entry_typed) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const fs::path dir = fs::temp_directory_path() / "gt4r-test-typed-name";
    fs::remove_all(dir);
    fs::create_directories(dir);
    Flow f(*disc, "US", dir.string(), false);
    f.rt->boot();
    f.rt->start_project(f.rt->first_project());
    for (int i = 0; i < 400 && f.page() != "EntryRoot" && !f.rt->finished(); ++i) f.rt->update();
    f.rt->settle();
    CHECK(f.page() == "EntryRoot");
    menu::LiveWidget* keyboard = menu::KeyboardTyping::keyboard_of(f.rt->current_page());
    CHECK(keyboard && keyboard->name == "Keyboard");
    if (!keyboard) return;
    const std::shared_ptr<menu::LiveWidget> input = keyboard->child("input");
    auto typed = [&](const std::string& text) {
        menu::KeyboardTyping t;
        t.type(text);
        for (int i = 0; i < 600 && t.pending(); ++i) t.step(*f.rt), f.rt->update();
        f.rt->settle();
        return input ? input->data.text("value") : std::string();
    };
    const std::string name = typed("dRIVER 7!");
    std::printf("  typed \"dRIVER 7!\": \"%s\"\n", name.c_str());
    CHECK(name == "Driver"); // (no keys for the digit, the space or the mark)
    CHECK(typed("\b\b") == "Driv");
    CHECK(typed("abcdefghij") == "Drivabcdef"); // (10 letters at most)
    CHECK(typed("\n") == "Drivabcdef" && f.page() != "EntryRoot"); // Enter: the page's own question
    std::printf("  after Enter: %s\n", f.page().c_str());
    fs::remove_all(dir);
}
