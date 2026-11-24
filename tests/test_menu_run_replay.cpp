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
}
