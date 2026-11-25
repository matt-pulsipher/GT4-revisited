// The Replay Theater: the title's third item runs the program's load_replay sequence (the
// project labomode at ReplayTheaterRoot); its card manager lists the replays saved in the data
// folder's replays/; Play loads the one chosen and the sequence shows it ("replay", with its
// file); after it the theater comes back; Exit returns to the title.

#include "menu_run_flow.h"

#include "gt4r/race/replay_file.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {

void write_replay(const std::filesystem::path& p, const std::string& course, const std::string& car) {
    race::ReplayFile f;
    f.course = course, f.sequence = "single_race", f.race = "sample_entry_6", f.laps = 2;
    f.cars.push_back({car, 0});
    f.steps.assign(10, std::vector<race::CarState>(1));
    const Bytes b = f.encode();
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

std::string pages(Flow& f) {
    std::string out;
    for (const auto& p : f.rt->open_pages()) out += p->name + " ";
    return out + "[" + f.focus() + "]";
}

} // namespace

TEST(disc_menu_replay_theater) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r-menu-test-replays";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "replays");
    write_replay(dir / "replays" / "tsukuba-20261124-101500.gt4rrep", "tsukuba", "panda_super_ie_90");
    Flow f(*disc, "US", dir.string());
    CHECK(f.boot_to_title());
    f.press({menu::Key::Down, menu::Key::Down});
    CHECK(f.focus() == "ReplayTheater");
    f.press({menu::Key::Accept});
    CHECK(f.rt->finished() && !f.rt->race_request());
    CHECK(f.rt->advance());
    f.rt->settle();
    std::printf("  the theater: %s\n", pages(f).c_str());
    for (const auto& line : f.rt->log())
        if (line.find("script error") != std::string::npos) std::printf("  %s\n", line.c_str());
    CHECK(f.rt->project() && f.rt->project()->name() == "labomode" && f.page() == "ReplayTheaterRoot");
    // The list shows the replay: its course and car as the database names them.
    CHECK(f.text_of("title") == "tsukuba-20261124-101500");
    // Right to the list, Accept chooses the replay, right to Play, Accept: the replay is loaded
    // and the theater's project ends asking for it to be shown.
    f.press({menu::Key::Right});
    CHECK(f.focus() == "List");
    f.press({menu::Key::Accept, menu::Key::Right});
    CHECK(f.focus() == "Play");
    f.press({menu::Key::Accept});
    const std::optional<menu::RaceRequest> replay = f.rt->race_request();
    CHECK(f.rt->finished() && replay && replay->sequence == "replay");
    if (replay) CHECK(replay->replay_file == (dir / "replays" / "tsukuba-20261124-101500.gt4rrep").string());
    // Shown (the app's part), the theater again.
    menu::GameState& g = f.rt->game();
    g.next_sequence.clear(), g.last_sequence = "replay";
    CHECK(f.rt->advance());
    f.rt->settle();
    CHECK(f.page() == "ReplayTheaterRoot");
    // Exit: back to the title.
    CHECK(f.focus_on("Exit"));
    f.press({menu::Key::Accept});
    CHECK(f.rt->finished() && !f.rt->race_request() && f.rt->wanted_project() == "arcade");
    CHECK(f.rt->advance());
    f.rt->settle();
    CHECK(f.page() == "TopRoot");
    std::printf("  back at %s\n", pages(f).c_str());
    CHECK(f.clean());
}

// The Demo tab: its list, a demo chosen and played: the race side shows the disc's recorded race, then the theater comes back.
TEST(disc_menu_replay_theater_demo) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r-menu-test-demos";
    std::filesystem::create_directories(dir);
    Flow f(*disc, "US", dir.string());
    CHECK(f.boot_to_title());
    f.press({menu::Key::Down, menu::Key::Down, menu::Key::Accept});
    CHECK(f.rt->advance());
    f.rt->settle();
    CHECK(f.page() == "ReplayTheaterRoot");
    f.press({menu::Key::Up, menu::Key::Accept});
    std::printf("  the theater: %s\n", pages(f).c_str());
    f.press({menu::Key::Right});
    CHECK(f.focus() == "List");
    f.press({menu::Key::Down, menu::Key::Accept, menu::Key::Right});
    CHECK(f.focus() == "Play");
    f.press({menu::Key::Accept});
    CHECK(f.rt->finished() && f.rt->game().replay_file == "replay/Rdemo1/Rdemo1");
    const std::optional<menu::RaceRequest> race = f.rt->race_request();
    CHECK(race && race->sequence == "replay" && race->replay_file == "replay/Rdemo1/Rdemo1");
    CHECK(f.clean());
}

// Save Replay: the race's menus open labomode on its save page, the run as the list's new file; its title is the mode's name
// and number, the keyboard gives it another, and saveNew writes it.
TEST(disc_menu_save_replay) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r-menu-test-save-replay";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "replays");
    Flow f(*disc, "US", dir.string());
    CHECK(f.boot_to_title());
    menu::GameState& g = f.rt->game();
    auto q = std::make_shared<menu::GameState::PendingReplay>();
    q->course = "tsukuba", q->car = "panda_super_ie_90", q->mode = "GTMODE_LICENSE_B", q->submode = 3, q->time_ms = 61234, q->kb = 40;
    q->picture.assign(64 * 64, 0xFF336699u);
    std::vector<std::pair<std::string, std::string>> saved;
    q->save = [&](const std::string& title, const std::string& path) {
        saved.emplace_back(title, path);
        write_replay(dir / "replays" / "new.gt4rrep", "tsukuba", "panda_super_ie_90");
        return true;
    };
    g.pending_replay = q;
    g.next_menu_arg = "MODE_SAVE_FULL_REPLAY";
    f.rt->start_project("labomode");
    f.rt->settle();
    CHECK(f.page() == "SaveReplayRoot");
    std::printf("  %s: %s\n", pages(f).c_str(), f.text_of("title").c_str());
    f.press({menu::Key::Accept}); // Create new file: the keyboard, on enter
    f.rt->settle();
    std::printf("  %s\n", pages(f).c_str());
    f.press({menu::Key::Accept}); // enter
    f.rt->settle();
    for (int i = 0; i < 6 && saved.empty(); ++i) f.press({menu::Key::Accept}), f.rt->settle();
    std::printf("  saved %zu: \"%s\" over \"%s\"\n", saved.size(), saved.empty() ? "" : saved[0].first.c_str(), saved.empty() ? "" : saved[0].second.c_str());
    CHECK(saved.size() == 1 && !saved.empty() && !saved[0].first.empty() && saved[0].first.back() == '3' && saved[0].second.empty());
    CHECK(f.clean());
}
