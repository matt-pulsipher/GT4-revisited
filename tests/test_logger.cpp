// The Analyzer (menu/logger.h): its samples of a lap run again (0x11E450), the analysis's end (0x122FA8), the face's sticks
// (0x1206A0), the texts; and the project logger's pages on the disc's scripts.

#include "menu_run_flow.h"

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/logger.h"
#include "gt4r/menu/race_natives.h"

#include <cmath>
#include <cstdio>

namespace {

// A lap of `frames` pictures at 1/60 s: the speed `v(i)` m/s, the throttle 0.5, the brake 0 then 1 past the half.
std::shared_ptr<menu::LoggerSource> fake_source(size_t frames, float length, std::function<float(size_t)> v, bool b = false) {
    auto src = std::make_shared<menu::LoggerSource>();
    src->course_length = length;
    for (int t = 0; t < (b ? 2 : 1); ++t) src->info[size_t(t)] = {true, "ka_01", 60, 83'457};
    src->lap = [frames, v](int) {
        auto at = std::make_shared<size_t>(0);
        auto d = std::make_shared<float>(100.0f);
        menu::LoggerLap lap;
        lap.frames = frames;
        lap.next = [frames, v, at, d](menu::LoggerPicture& p) {
            if (*at >= frames) return false;
            const size_t i = (*at)++;
            p.speed = v(i), *d += p.speed / 60.0f, p.distance = *d;
            p.throttle = 0.5f, p.brake = i >= frames / 2 ? 1.0f : 0.0f, p.gear = 3;
            return true;
        };
        return std::optional<menu::LoggerLap>(std::move(lap));
    };
    src->course_label = "tsukuba", src->race_mode_label = "ARCADE_TIME_TRIAL";
    return src;
}

} // namespace

TEST(logger_samples_a_lap) {
    menu::Logger L;
    std::array<u8, 5> display{1, 1, 1, 1, 1};
    L.display = &display;
    // 600 pictures at 30 m/s: 300 m; the course 16,383 m long: a sample every metre.
    L.source = fake_source(600, 16383.0f, [](size_t) { return 30.0f; });
    L.analyze_start();
    CHECK(L.running && L.spacing == 1.0f);
    while (!L.analyze_step(1.0)) {
    }
    CHECK(L.analyzed && !L.running);
    // (the first picture starts the track; each metre after it a sample: 299.5 m -> 299)
    std::printf("  %zu samples, length %.1f, the speed's top %.0f, scale %.0f, metres' scale %.3f\n", L.track[0].size(), double(L.length),
                double(L.speed_top), double(L.scale_v), double(L.scale_m));
    CHECK(L.track[0].size() == 299 && L.track[1].size() == 299); // (an empty Track B samples A's car again)
    CHECK(std::fabs(L.track[0][10].speed() - 30.0f) < 0.03f);
    CHECK((L.track[0][10].speed_gear & 7) == 3);
    CHECK(L.track[0][10].throttle == 128 && L.track[0][10].brake == 0 && L.track[0][290].brake == 255);
    // The speed graph's top: 108 km/h + 100; its scale (int)(108 / 100); the length (count - 1) x spacing; the metres' scale
    // the face's 640 over the length, at most 10.
    CHECK(L.speed_top == 208.0f && L.scale_v == 1.0f && L.length == 298.0f && L.middle == 149.0f);
    CHECK(std::fabs(L.scale_m - 640.0f / 298.0f) < 1e-5f);
    CHECK(std::fabs(L.speed_at(0, 50.0f) - 30.0f) < 0.03f);
}

TEST(logger_middle_stops_then_goes_round) {
    menu::Logger L;
    std::array<u8, 5> display{1, 1, 1, 1, 1};
    L.display = &display;
    L.length = 300, L.middle = 1, L.scale_m = 1;
    const float left[4] = {-1, 0, 0, 0}, none[4] = {0, 0, 0, 0}, right[4] = {1, 0, 0, 0};
    L.input(left);
    CHECK(L.middle == 0 && L.edge[0] == 1); // stopped at the start
    L.input(left);
    CHECK(L.middle == 0 && L.edge[0] == 1);
    L.input(none);
    CHECK(L.edge[0] == 2);
    L.input(left);
    CHECK(L.middle == 300 && L.edge[0] == 0); // round to the end
    L.input(right);
    CHECK(L.middle == 300 && L.edge[1] == 1);
    L.input(none), L.input(right);
    CHECK(L.middle == 0);
    // The left stick zooms: x the metres (0.4 .. 10), y the speed (0.5 .. 4).
    const float in[4] = {0, 0, 1, 1};
    for (int i = 0; i < 200; ++i) L.input(in);
    CHECK(L.scale_m == 10.0f && L.scale_v == 4.0f);
}

TEST(logger_texts) {
    CHECK(menu::logger_tenths(123) == "12.3" && menu::logger_tenths(5) == "0.5");
    CHECK(menu::logger_time_text(83'457) == "1'23.457");
}

// The project logger on the disc's scripts: with CanLogger the page analyzes and shows the graph with the logger focused;
// without, its menu at once with Return to Logger and Load Track B greyed.
TEST(disc_menu_logger_pages) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    for (int can = 1; can >= 0; --can) {
        Flow f(*disc);
        CHECK(f.boot_to_title());
        menu::quick_work(f.rt->game()).set("CanLogger", can);
        f.rt->game().logger = fake_source(600, 32767.0f, [](size_t i) { return 10.0f + float(i) * 0.05f; }, true);
        f.rt->start_project("logger");
        f.rt->settle(600);
        std::string open;
        for (const auto& p : f.rt->open_pages()) open += p->name + " ";
        std::printf("  CanLogger %d: %s[%s]\n", can, open.c_str(), f.focus().c_str());
        CHECK(f.clean());
        if (can) {
            CHECK(f.page() == "LoggerRoot" && f.focus() == "logger");
            // Triangle: the menu; Exit leaves the project with no track to load.
            f.press({menu::Key::Cancel});
            CHECK(f.page() == "LoggerMenu");
        } else {
            CHECK(f.page() == "LoggerMenu");
            menu::LiveWidget* page = f.rt->current_page();
            const std::shared_ptr<menu::LiveWidget> pane = page ? page->child("Pane") : nullptr, logger = pane ? pane->child("Logger") : nullptr;
            CHECK(logger && logger->insensitive());
        }
        CHECK(f.rt->game().logger->load_track == -1);
    }
}
