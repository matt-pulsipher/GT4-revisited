// How a start's arguments and environment make the run played or scripted (src/gt4r/settings/run_mode.h).

#include "test_framework.h"

#include "gt4r/settings/run_mode.h"

#include <map>

using gt4r::settings::RunMode;

namespace {

RunMode mode(const std::vector<std::string>& args, const std::map<std::string, std::string>& env = {}) {
    return RunMode::of(args, [&env](const char* name) -> const char* {
        const auto it = env.find(name);
        return it == env.end() ? nullptr : it->second.c_str();
    });
}

} // namespace

TEST(run_mode_played) {
    for (const RunMode& m : {mode({}), mode({"disc.iso"}), mode({"disc.iso", "--screen", "race"}), mode({"disc.iso"}, {{"GT4R_DATA_DIR", "/d"}})}) {
        CHECK(!m.windowless && !m.scripted && !m.pictures);
        CHECK(m.pace == 0);
    }
    CHECK(mode({"disc.iso", "--headless"}).windowless);
    CHECK(mode({"disc.iso"}, {{"GT4R_HEADLESS", "1"}}).windowless);
    CHECK(!mode({"disc.iso"}, {{"GT4R_HEADLESS", "0"}}).windowless);
}

TEST(run_mode_scripted) {
    for (const char* option : {"--shot", "--sweep"}) {
        const RunMode m = mode({"disc.iso", option, "x"});
        CHECK(m.windowless && m.scripted && m.pictures);
        CHECK(m.pace == 0); // (held by its own rule)
        const RunMode seen = mode({"disc.iso", option, "x", "--visible"});
        CHECK(!seen.windowless && seen.scripted && seen.pictures); // (no focus, no real input, in its window)
    }
    for (const char* option : {"--keys", "--stats"}) {
        const RunMode m = mode({"disc.iso", option, "x"});
        CHECK(m.windowless && m.scripted && !m.pictures);
        CHECK(m.pace == 60);
        const RunMode seen = mode({option, "x"}, {{"GT4R_VISIBLE", "1"}});
        CHECK(!seen.windowless && !seen.scripted && seen.pace == 0); // (a played run with keys pressed for the player)
    }
    for (const char* name : {"GT4R_RACE", "GT4R_MENU", "GT4R_VIEWER"}) {
        CHECK(mode({"disc.iso"}, {{name, "course=tsukuba"}}).windowless);
        CHECK(!mode({"disc.iso", "--visible"}, {{name, "course=tsukuba"}}).scripted);
        CHECK(!mode({"disc.iso"}, {{name, ""}}).windowless);
    }
    // The command line before the environment; an option's value is not an option.
    CHECK(mode({"--headless"}, {{"GT4R_VISIBLE", "1"}}).windowless);
    CHECK(!mode({"--shot", "x", "--visible"}, {{"GT4R_HEADLESS", "1"}}).windowless);
    CHECK(mode({"--shot", "--visible"}).windowless);
    CHECK(!mode({"--shot"}).scripted); // (no file: nothing is asked for)
}

TEST(run_mode_pace) {
    CHECK(mode({"--keys", "x", "--pace", "120"}).pace == 120);
    CHECK(mode({"--pace", "display", "--keys", "x"}).pace == RunMode::kPaceDisplay);
    CHECK(mode({"--keys", "x", "--pace", "off"}).pace == 0);
    CHECK(mode({"--pace", "30"}).pace == 0); // (a window's run: the display's)
    CHECK(mode({"--pace", "30", "--headless"}).pace == 30);
}
