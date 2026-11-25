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

// The catalog's figures in the player's units on that list (0x445F70, 0x445EE8: the engine row's integer times the
// unit's factor, truncated), as the original's pages show them on 9 APR (playtest's B38).
TEST(disc_gt_mode_used_cars_figures) {
    Disc* disc = gt4_disc();
    if (!disc || !sim::Program::load(*disc).known()) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    if (f.page() != "TopRoot") return;
    f.rt->game().today = menu::date_to_day(2005, 4, 9);
    f.map_focus("UsedCarEU");
    f.press({menu::Key::Accept});
    if (f.page() == "ConfirmDialog") f.press({menu::Key::Accept});
    CHECK(f.page() == "UsedCarRoot");
    std::string all;
    for (int k = 0; k < 60; ++k) {
        all += focused_texts(f) + "\n";
        f.press({menu::Key::Down});
    }
    for (const char* torque : {"202.52", "260.38", "249.53", "162.74", "271.23", "216.98", "117.89", "73.77", "224.21", "278.46", "115.72", "332.71", "110.66", "78.11",
                               "137.42", "253.15", "151.89"}) {
        const bool found = all.find(std::string("Torque:") + torque + "ft.lb") != std::string::npos;
        CHECK(found);
        if (!found) std::printf("  no row with %s ft.lb\n", torque);
    }
    const size_t miata = all.find("Name=Mazda MX-5 Miata 1.8 RS");
    CHECK(miata != std::string::npos && all.substr(miata, all.find('\n', miata) - miata).find("Power:142HP") != std::string::npos);
}

// The Professional hall's events in order with their modes, and the Tuning Car Grand Prix's regulations, as the
// reference's pages (21-gtmode-deep: professional_events_hall_*.png, tuning_car_gp_regulations.png).
TEST(disc_gt_mode_professional_hall) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car(5000000));
    if (f.page() != "TopRoot") return;
    std::string texts;
    std::function<void(const menu::LiveWidget&)> walk = [&](const menu::LiveWidget& w) {
        if (w.is("TextFace") && !w.data.text("text").empty() && w.name != "info") texts += w.data.text("text") + "|";
        for (const auto& c : w.children) walk(*c);
    };
    f.map_focus("ProfessionalHall");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "EventHallRoot");
    if (f.rt->current_page()) walk(*f.rt->current_page());
    const char* hall = "Clubman Cup|Single Race|Tuning Car Grand Prix|Championship Race|Race of NA Sport|Single Race|Race of Turbo Sport|Single Race|"
                       "Boxer Spirit|Single Race|World Classics|Championship Race|World Compact|Championship Race|Supercar Festival|Single Race|"
                       "Gran Turismo World Championship|Championship Race|";
    CHECK(texts.find(hall) != std::string::npos);
    if (texts.find(hall) == std::string::npos) std::printf("  hall: %s\n", texts.c_str());
    f.press({menu::Key::Down, menu::Key::Accept});
    CHECK(f.page() == "EventInfoDialog");
    texts.clear();
    if (f.rt->current_page()) walk(*f.rt->current_page());
    const char* regulations = "Tuning Car Grand Prix|Game Mode|Championship Race\r\nWinning Prize: Cr. 20,000|Tires|No restrictions|Cars\r\nPermitted:|"
                              "All cars permitted|Required\r\nLicense|A-Spec: National A License";
    const char* prizes = "1|10,000|2|6,000|3|4,000|4|3,000|5|1,000|6|500|";
    CHECK(texts.find(regulations) != std::string::npos && texts.find(prizes) != std::string::npos);
    if (texts.find(regulations) == std::string::npos || texts.find(prizes) == std::string::npos) std::printf("  regulations: %s\n", texts.c_str());
    CHECK(f.clean());
}

// Driving Missions: the panels as the reference's page (21-gtmode-deep/driving_missions_page.png).
TEST(disc_gt_mode_missions_page) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car(5000000));
    if (f.page() != "TopRoot") return;
    std::string texts;
    std::function<void(const menu::LiveWidget&)> walk = [&](const menu::LiveWidget& w) {
        if (w.is("TextFace") && !w.data.text("text").empty()) texts += w.name + "=" + w.data.text("text") + "|";
        for (const auto& c : w.children) walk(*c);
    };
    f.map_focus("Mission");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "MissionRoot");
    if (f.rt->current_page()) walk(*f.rt->current_page());
    for (const char* want : {"title=The Pass: Deep Forest|doc=This is a mission for improving basic overtaking techniques. Venue is the Deep Forest track "
                             "with one simple hairpin bend.|crs=Deep Forest: 1st corner",
                             "title=The Pass: Grand Valley|doc=This is an introduction to race techniques where you practice driving the new BMW 3 "
                             "series. Brake late and pass smoothly through the inside of the rival car.|crs=Grand Valley: 1st corner|"})
        CHECK(texts.find(want) != std::string::npos);
    CHECK(f.clean());
}
