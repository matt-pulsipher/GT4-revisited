// The Replay Theater: the title's third item runs the program's load_replay sequence (the
// project labomode at ReplayTheaterRoot); its card manager lists the replays saved in the data
// folder's replays/; Play loads the one chosen and the sequence shows it ("replay", with its
// file); after it the theater comes back; Exit returns to the title.

#include "menu_run_flow.h"

#include "gt4r/race/replay_file.h"
#include "gt4r/save/best_lap_card.h"
#include "gt4r/save/codec.h"
#include "gt4r/save/replay_list.h"
#include "gt4r/menu/game_internal.h"

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

// Load Best Lap Replay (the race's menus' LOAD_GHOST: labomode in MODE_LOAD_BESTLAP_REPLAY): the list holds the best lap replays (a
// card's folders <product>B..., save/best_lap_card.h) of the race's course and car alone, not full replays nor another course's.
TEST(disc_menu_load_best_lap_list) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r-menu-test-bestlap";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "replays");
    std::filesystem::create_directories(dir / "memorycard");
    Flow f(*disc, "US", dir.string());
    CHECK(f.boot_to_title());
    menu::Database& db = menu::natives_of(*f.rt)->db();
    auto best = [&](const std::string& title, const std::string& course, const std::string& car) {
        save::ReplayParticulars p;
        p.title = title, p.course_label = course, p.car = db.code(menu::kCarTable, car), p.course = db.code(menu::kCourseTable, course), p.time_ms = 83'457, p.mode = 2;
        const Bytes data(0x2000, 0x5A); // (the list reads the particulars; the data file is not opened)
        CHECK(save::write_best_lap((dir / "memorycard").string(), "BASCUS-97328", {}, data, p, "", "Best lap replay", "") == save::Result::Ok);
    };
    best("mine", "tsukuba", "ka_01");
    best("other course", "fuji_90s", "ka_01");
    best("other car", "tsukuba", "panda_super_ie_90");
    write_replay(dir / "replays" / "tsukuba-20261124-101500.gt4rrep", "tsukuba", "ka_01");
    menu::GameState& g = f.rt->game();
    g.race.course = "tsukuba", g.race.car = "ka_01";
    g.next_menu_arg = "MODE_LOAD_BESTLAP_REPLAY";
    f.rt->start_project("labomode");
    f.rt->settle();
    std::printf("  the load page: %s, first row %s\n", pages(f).c_str(), f.text_of("title").c_str());
    CHECK(f.page() == "LoadReplayRoot" && f.text_of("title") == "mine");
    std::filesystem::remove_all(dir);
}

// The play list's file (FileGT4ReplayList): 400 u64 entries, the count at 0xC80, 0xCC0 bytes; an entry is a card folder's
// letters as a number with the letter after the product on top.
TEST(save_replay_list_layout) {
    const std::vector<u64> entries = {0x52000000000123ull, 0x42000003FFFFFFFFull, 0x2Aull};
    const Bytes b = save::encode_play_list(entries);
    CHECK(b.size() == 0xCC0 && rd32(b, 0xC80) == 3 && rd32(b, 0) == 0x123 && rd32(b, 4) == 0x520000);
    const std::optional<std::vector<u64>> back = save::decode_play_list(b);
    CHECK(back && *back == entries);
    Bytes bad = b;
    bad[0xC80] = 0x91, bad[0xC81] = 0x01; // 401
    CHECK(!save::decode_play_list(bad));
    // "BASCUS-97328R" and seven letters: 'R' on top, the letters' number below (the first letter the lowest six bits).
    const std::optional<u64> e = save::folder_entry("BASCUS-97328RzYYYYYY", "BASCUS-97328");
    CHECK(e && *e == (u64('R') << 56 | 1));
    CHECK(!save::folder_entry("BASCUS-97328R", "BASCUS-97328"));
}

// The Replay Theater's play list: ToListMov puts the chosen replay on the card's list (asked first to make the list), the
// PlayList folder lists it, its Play plays the list one replay after another from the demonstration's count until the player
// ends one; shuffled, in the order the program draws from the seed.
TEST(disc_menu_replay_play_list) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r-menu-test-playlist";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "replays");
    std::filesystem::create_directories(dir / "memorycard");
    const std::filesystem::path a = dir / "replays" / "tsukuba-20261124-101500.gt4rrep", b = dir / "replays" / "fuji_90s-20261124-101600.gt4rrep";
    write_replay(a, "tsukuba", "panda_super_ie_90");
    write_replay(b, "fuji_90s", "panda_super_ie_90");
    std::filesystem::last_write_time(a, std::filesystem::last_write_time(b) - std::chrono::seconds(60)); // (b the newest: first)
    Flow f(*disc, "US", dir.string());
    CHECK(f.boot_to_title());
    f.press({menu::Key::Down, menu::Key::Down, menu::Key::Accept});
    CHECK(f.rt->advance());
    f.rt->settle();
    CHECK(f.page() == "ReplayTheaterRoot");
    // The first replay chosen, ToListMov: no list yet, the page asks to make one (and how much room it takes).
    f.press({menu::Key::Right, menu::Key::Accept});
    CHECK(f.focus_on("ToListMov"));
    std::printf("  %s\n", pages(f).c_str());
    f.press({menu::Key::Accept});
    for (int i = 0; i < 4 && !save::ReplayList((dir / "memorycard").string(), {}).exists(); ++i) {
        std::printf("  asked: %s\n", pages(f).c_str());
        f.press({menu::Key::Accept});
        f.rt->settle();
    }
    std::vector<u64> on_card;
    CHECK(save::ReplayList((dir / "memorycard").string(), {}).load(on_card) == save::Result::Ok && on_card.size() == 1);
    // The second too: the list is there, it is written at once.
    f.press({menu::Key::Accept}); // (save_done)
    f.press({menu::Key::Left});
    std::printf("  back: %s\n", pages(f).c_str());
    f.press({menu::Key::Down, menu::Key::Accept});
    std::printf("  then: %s\n", pages(f).c_str());
    CHECK(f.focus_on("ToListMov"));
    f.press({menu::Key::Accept});
    f.rt->settle();
    CHECK(save::ReplayList((dir / "memorycard").string(), {}).load(on_card) == save::Result::Ok && on_card.size() == 2);
    std::printf("  on the card: %zu, %s\n", on_card.size(), pages(f).c_str());
    // The PlayList folder: its two rows, the newest added on top (addPlayList's rotate).
    CHECK(f.focus_on("PlayList"));
    f.press({menu::Key::Accept});
    f.rt->settle();
    std::printf("  the list: %s, first row %s\n", pages(f).c_str(), f.text_of("title").c_str());
    CHECK(f.text_of("title") == "tsukuba-20261124-101500");
    // Play: the list from the demonstration's count, the next after each replay, until the player ends one.
    menu::GameState& g = f.rt->game();
    g.demonstration.card_at = 0;
    CHECK(f.focus_on("Play"));
    f.press({menu::Key::Accept});
    std::optional<menu::RaceRequest> replay = f.rt->race_request();
    CHECK(f.rt->finished() && replay && replay->sequence == "replay" && replay->replay_file == a.string());
    CHECK(menu::play_list_next(*f.rt, false) && g.replay_file == b.string());
    CHECK(menu::play_list_next(*f.rt, false) && g.replay_file == a.string()); // (past the end, the first again)
    CHECK(!menu::play_list_next(*f.rt, false, true) && !g.replay_list);
    // A replay gone from the card is passed over.
    std::filesystem::remove(b);
    CHECK(menu::play_list_next(*f.rt, true) && g.replay_file == a.string());
    CHECK(!menu::play_list_next(*f.rt, false)); // (missed as many times as the list is long: over)
    // Shuffled: 0x6012C0's order from the seed (here 5489, the generator's usual one): for two, the second swapped with the first when the draw x 2 is 0.
    write_replay(b, "fuji_90s", "panda_super_ie_90");
    g.options["replay_play_mode"] = std::string("SHUFFLE");
    g.demonstration.card_seed = 5489, g.demonstration.card_at = 0;
    save::Mt19937 mt(5489);
    const bool swapped = mt.below(2) == 0;
    const bool played = menu::play_list_next(*f.rt, true);
    std::printf("  shuffled (%s, %s): %d %s\n", swapped ? "swapped" : "in order", g.option_text("replay_play_mode").c_str(), played, g.replay_file.c_str());
    CHECK(played && g.replay_file == (swapped ? b : a).string());
    CHECK(menu::play_list_next(*f.rt, false) && g.replay_file == (swapped ? a : b).string());
    CHECK(f.clean());
    std::filesystem::remove_all(dir);
}

// The title's time-out with replays on the card's play list (0x10BEF0 over 0x4349A8): each try of kind replay plays the
// list's next replay instead of the disc's recording; a list shorter than the time-out's tries (repeat_time) ends the
// time-out after one; a replay gone from the card plays nothing. No list: the disc's recordings.
TEST(disc_menu_title_card_replays) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r-menu-test-title-card";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "replays");
    std::filesystem::create_directories(dir / "memorycard");
    std::vector<std::filesystem::path> files;
    std::vector<u64> entries;
    for (const char* name : {"tsukuba-20261124-101500.gt4rrep", "fuji_90s-20261124-101600.gt4rrep", "tsukuba-20261124-101700.gt4rrep"}) {
        files.push_back(dir / "replays" / name);
        write_replay(files.back(), "tsukuba", "panda_super_ie_90");
        u64 h = 0xCBF29CE484222325ull; // (our replays' entries: the file's name hashed, FNV-1a, in 42 bits)
        for (const char* c = name; *c; ++c) h = (h ^ u8(*c)) * 0x100000001B3ull;
        entries.push_back(h & ((u64(1) << 42) - 1));
    }
    Flow f(*disc, "US", dir.string());
    CHECK(f.boot_to_title());
    menu::GameState& g = f.rt->game();
    menu::GameState::Demonstration& d = g.demonstration;
    g.options["demo_movie_interval"] = 0; // (no movie: every try of the time-out is the list's kind replay)
    std::printf("  restart_from_beginning %d, tries %d\n", d.restart, d.repeat);
    d.restart = 0; // (the count kept from one time-out to the next)
    auto timeout = [&] {
        std::vector<std::string> played;
        for (auto step = menu::autodemo_step(*f.rt, true); step; step = menu::autodemo_step(*f.rt, false)) {
            played.push_back(step->replay);
            g.menu_result.clear();
        }
        return played;
    };
    const save::ReplayList list((dir / "memorycard").string(), {});
    // No list: the disc's recordings, three per time-out.
    std::vector<std::string> seen = timeout();
    CHECK(seen.size() == 3 && seen[0].rfind("replay/Rdemo", 0) == 0);
    // Three on the list (as many as the tries): the card's replays in order from the count, for every try.
    CHECK(list.save(entries, "Replay list") == save::Result::Ok);
    d.card_at = 0;
    seen = timeout();
    for (const std::string& p : seen) std::printf("  played: %s\n", p.c_str());
    CHECK(seen.size() == 3 && seen[0] == files[0].string() && seen[1] == files[1].string() && seen[2] == files[2].string());
    seen = timeout(); // (the count goes on: the first again)
    CHECK(seen.size() == 3 && seen[0] == files[0].string());
    // Two on the list (fewer than the tries): one replay ends the time-out.
    CHECK(list.save({entries[0], entries[1]}, "Replay list") == save::Result::Ok);
    d.card_at = 1;
    seen = timeout();
    CHECK(seen.size() == 1 && seen[0] == files[1].string());
    // A replay gone from the card: nothing plays for it (and with the short list, the time-out is over).
    std::filesystem::remove(files[0]);
    seen = timeout();
    CHECK(seen.empty() && d.card_at == 1);
    // An empty list: the disc's recordings again.
    CHECK(list.save({}, "Replay list") == save::Result::Ok);
    seen = timeout();
    CHECK(seen.size() == 3 && seen[0].rfind("replay/Rdemo", 0) == 0);
    CHECK(f.clean());
    std::filesystem::remove_all(dir);
}
