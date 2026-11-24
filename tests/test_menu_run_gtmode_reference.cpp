// Gran Turismo mode against the reference's captures (~/gt4re-out/reference 18-round4-gt, 21-gtmode-deep):
// Used Car Showroom II's stock, prices, mileage and figures on three days.

#include "menu_run_flow.h"

#include "gt4r/menu/game_internal.h"
#include "gt4r/sim/program.h"

#include <cstdio>
#include <functional>
#include <vector>

namespace {

// Every text of the focused widget, "name=text " joined.
std::string focused_texts(Flow& f) {
    std::string out;
    std::function<void(const menu::LiveWidget&)> walk = [&](const menu::LiveWidget& w) {
        if (w.is("TextFace") && !w.data.text("text").empty()) out += w.name + "=" + w.data.text("text") + " ";
        for (const auto& c : w.children) walk(*c);
    };
    if (f.rt->focused()) walk(*f.rt->focused());
    return out;
}

} // namespace

// The lot of cars from the second half of the 90s (the map's UsedCarEU) on 9 APR, 17 JUL and 23 JUL: the week's
// stock (usedcar/US_used<week>), each car made used by makeUsed (0x440FB8) and priced by 0x441128, as the
// original's pages show them.
TEST(disc_gt_mode_used_cars_by_day) {
    Disc* disc = gt4_disc();
    if (!disc || !sim::Program::load(*disc).known()) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    if (f.page() != "TopRoot") return;
    struct Day {
        int m, d;
        std::vector<std::string> rows; // the first rows as the reference shows them: name, price
        std::string first;             // the first row's figures
    };
    const std::vector<Day> days = {
        {4, 9, {"Mitsubishi 3000GT    SL (J) `96|Cr.14,990", "Mitsubishi Lancer Evolution IV    GSR `96|Cr.14,990", "Subaru LEGACY Touring Wagon GT-B `96|Cr.14,665",
                "Honda PRELUDE Type S `96|Cr.13,265"},
         "Spec0=Type:4WD Spec1=Power:221HP/6000rpm Spec2=Torque:202.52ft.lb/4500rpm Spec3=Mileage:32355.3miles Price=Cr.14,990 "},
        {7, 17, {"Mitsubishi 3000GT    SL (J) `96|Cr.10,492", "Mitsubishi 3000GT    VR-4 Turbo (J) `96|Cr.15,130", "Subaru IMPREZA Sedan WRX STi Ver.III`96|Cr.10,097",
                 "Subaru LEGACY Touring Wagon GT-B `96|Cr.14,665"},
         "Spec3=Mileage:41286.4miles"},
        {7, 23, {"Nissan 240SX `96|Cr.8,742", "Mitsubishi 3000GT    SL (J) `96|Cr.10,492", "Subaru IMPREZA Sedan WRX STi Ver.III`96|Cr.14,425", "Honda PRELUDE Type S `96|Cr.13,265"},
         "Spec1=Power:202HP/6000rpm Spec2=Torque:202.52ft.lb/4000rpm Spec3=Mileage:42259.2miles"},
    };
    for (const Day& day : days) {
        f.rt->game().today = menu::date_to_day(2005, day.m, day.d);
        for (int k = 0; k < 4 && f.page() != "TopRoot"; ++k) f.press({menu::Key::Accept}); // (a new course's notice)
        CHECK(f.page() == "TopRoot");
        if (f.page() != "TopRoot") return;
        f.map_focus("UsedCarEU");
        f.press({menu::Key::Accept});
        if (f.page() == "ConfirmDialog") f.press({menu::Key::Accept}); // "Used car line-up has been updated"
        CHECK(f.page() == "UsedCarRoot");
        for (int k = 0; k < 8; ++k) f.press({menu::Key::Up}); // (the list keeps the place of the last visit)
        const std::string first = focused_texts(f);
        CHECK(first.find(day.first) != std::string::npos);
        if (first.find(day.first) == std::string::npos) std::printf("  %d/%d: %s\n", day.m, day.d, first.c_str());
        for (const std::string& want : day.rows) {
            const std::string got = focused_texts(f);
            const size_t bar = want.find('|');
            const bool ok = got.find("Name=" + want.substr(0, bar) + " ") != std::string::npos && got.find("Price=" + want.substr(bar + 1) + " ") != std::string::npos;
            CHECK(ok);
            if (!ok) std::printf("  %d/%d: want %s, got %s\n", day.m, day.d, want.c_str(), got.c_str());
            f.press({menu::Key::Down});
        }
        for (int k = 0; k < 4 && f.page() != "TopRoot"; ++k) f.press({menu::Key::Cancel});
    }
    CHECK(f.clean());
}
