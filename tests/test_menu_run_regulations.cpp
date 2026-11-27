// An event's regulations on GT mode's event pages, as the game's own scripts check them (share/regulation.ad): the
// Light-weight K-Car Cup's length limit compares a number with the car's getLength text.

#include "menu_run_flow.h"

#include <cstdio>

namespace {

// The Beginner hall's Light-weight K-Car Cup (its sixth event) entered with the garage's car `slot`: the race asked
// for, or none (the page says the car may not enter).
std::optional<menu::RaceRequest> enter_kcar_cup(Flow& f, int slot, std::string* said) {
    f.rt->game().riding_car = slot;
    f.map_focus("BeginnerHall");
    f.press({menu::Key::Accept});
    if (f.page() != "EventHallRoot") return std::nullopt;
    for (int i = 0; i < 5; ++i) f.press({menu::Key::Down});
    f.press({menu::Key::Accept});
    std::printf("  %s [%s] label=%s\n", f.page().c_str(), f.focus().c_str(), f.text_of("label").c_str());
    if (f.page() != "EventInfoDialog") return std::nullopt; // (the US disc's "Light-weight K-Car Cup", Europe's "Lightweight ..")
    for (int n = 0; n < 4 && !f.rt->race_request(); ++n) {
        f.press({menu::Key::Accept});
        std::printf("  -> %s [%s]\n", f.page().c_str(), f.focus().c_str());
        if (f.page() == "ConfirmDialog") {
            if (said) *said = f.text_of("message");
            break;
        }
    }
    return f.rt->race_request();
}

} // namespace

// A car over 3400 mm (the Alfa Romeo bought at the start) is refused; a Honda BEAT (3295 mm) enters.
TEST(disc_gt_mode_kcar_cup_length_limit) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    {
        Flow f(*disc);
        CHECK(f.gt_mode_with_a_car());
        if (f.page() != "TopRoot") return;
        std::string said;
        const std::optional<menu::RaceRequest> race = enter_kcar_cup(f, 0, &said);
        std::printf("  refused: %s\n", said.c_str());
        CHECK(!race && f.page() == "ConfirmDialog");
        CHECK(f.clean());
    }
    {
        Flow f(*disc);
        CHECK(f.gt_mode_with_a_car());
        if (f.page() != "TopRoot") return;
        menu::GarageEntry beat;
        beat.used = true, beat.car = "beat_91";
        const int slot = f.rt->game().add_car(beat);
        CHECK(slot == 1);
        const std::optional<menu::RaceRequest> race = enter_kcar_cup(f, slot, nullptr);
        CHECK(race && race->race == "am_kcar_0001" && race->car == "beat_91" && race->garage_slot == slot);
        CHECK(f.clean());
    }
}
