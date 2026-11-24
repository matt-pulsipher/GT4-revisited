// A game played forward without its races (game_progress.h): the game's own menus from power on
// to a new game, then what a player's purchases and license tests write, through the scripts'
// natives and the program's result routines (Runtime::race_finished), the presents through the
// present page, and the card manager's save.

#include "gt4r/menu/game_progress.h"

#include "gt4r/menu/game_internal.h"

#include <cstdio>
#include <cstdlib>
#include <functional>

namespace gt4r::menu {

using script::Value;

namespace {

struct Driver {
    Runtime& rt;
    ProgressReport& out;

    std::string page() { return rt.current_page() ? rt.current_page()->name : std::string(); }
    std::string focus() { return rt.focused() ? rt.focused()->name : std::string(); }
    void press(std::initializer_list<Key> keys) {
        for (Key k : keys) {
            rt.key(k);
            rt.settle();
        }
    }
    bool fail(const std::string& why) {
        out.error = why + " (page " + page() + ", focus " + focus() + ")";
        return false;
    }
    std::shared_ptr<LiveWidget> find(const std::string& name) {
        std::function<std::shared_ptr<LiveWidget>(const std::shared_ptr<LiveWidget>&)> f = [&](const std::shared_ptr<LiveWidget>& w) {
            if (w->name == name) return w;
            for (const auto& c : w->children)
                if (auto found = f(c)) return found;
            return std::shared_ptr<LiveWidget>();
        };
        return rt.current_page() ? f(rt.current_page()->self()) : nullptr;
    }
    Value game() { return rt.vm().lookup("game"); }
    Value call(const Value& object, const std::string& method, std::vector<Value> args) {
        return rt.vm().call(rt.vm().attribute(object, method), std::span<const Value>(args.data(), args.size()));
    }

    // Power on with a card that holds no game: no to making one, on past what cannot be saved,
    // the opening movie skipped: the title.
    bool boot_to_title() {
        rt.boot();
        rt.start_project(rt.first_project());
        rt.settle();
        if (page() != "ConfirmDialog") return fail("power on: no question about the game's data (does the card hold a game already?)");
        press({Key::Left, Key::Accept});
        press({Key::Right, Key::Accept});
        if (page() == "MovieRoot") press({Key::Start});
        if (!rt.finished() || rt.wanted_project() != "arcade" || !rt.advance()) return fail("power on: the boot project did not end at the title");
        rt.settle();
        return page() == "TopRoot" || fail("power on: no title page");
    }
    // The title's Gran Turismo mode: the game's data made (a name typed on the page's keyboard,
    // yes to each question), saved on the card, the title again, then the map.
    bool new_game() {
        press({Key::Down});
        if (focus() != "GTmode") return fail("title: Gran Turismo mode not found");
        press({Key::Accept, Key::Accept});
        if (!rt.finished() || !rt.advance()) return fail("new game: no project for making the game's data");
        rt.settle();
        press({Key::Left, Key::Accept, Key::Left, Key::Accept});
        press({Key::Right, Key::Right});
        for (int i = 0; i < 6 && focus() != "key_enter"; ++i) press({Key::Right});
        press({Key::Accept});
        for (int i = 0; i < 24 && !rt.finished(); ++i) press({Key::Accept});
        if (!rt.finished() || !rt.advance()) return fail("new game: the questions did not end");
        rt.settle();
        for (int i = 0; i < 6 && focus() != "GTmode"; ++i) press({Key::Down});
        press({Key::Accept});
        if (!rt.finished() || rt.wanted_project() != "gtmode" || !rt.advance()) return fail("new game: Gran Turismo mode did not start");
        rt.settle();
        return page() == "TopRoot" || fail("new game: no map");
    }
    // A new car bought at its dealer as the purchase page's Buy does it (gtmode's
    // BuyCarRoot):
    // garage.addNewCar, the diary's day, the car open in Arcade. The garage's place, -1 none.
    int buy(const std::string& label, int paint) {
        std::shared_ptr<GameNatives> n = natives_of(rt);
        if (!n || !n->db().car(label)) return -1;
        const Value g = game();
        const Value place = call(rt.vm().attribute(g, "garage"), "addNewCar", {Value::string(label), Value::integer(paint)});
        if (place.is_nil() || place.i < 0) return -1;
        call(rt.vm().attribute(g, "calendar"), "putBuyNewCarEvent", {Value::string(label), Value::integer(paint)});
        call(g, "setCarAvailable", {Value::string(label)});
        return s32(place.i);
    }
    // The present page shown on entering Gran Turismo mode (its project's onLoad) until the map:
    // each present's page waited through and accepted.
    bool presents() {
        rt.start_project("gtmode");
        rt.settle();
        for (int n = 0; n < 200 && page() != "TopRoot"; ++n) {
            if (page() == "PresentRoot") {
                for (int k = 0; k < 900; ++k) rt.update();
                rt.settle();
                press({Key::Accept});
            } else if (page() == "ConfirmDialog") press({Key::Accept});
            else press({Key::Cancel});
        }
        return page() == "TopRoot" || fail("presents: the map did not come back");
    }
};

} // namespace

ProgressPlan default_progress_plan(Runtime& rt) {
    ProgressPlan plan;
    (void)rt;
    // A small car, a four-wheel drive sedan, sports cars front- and mid-engined, a racing car,
    // and a four-wheel drive sports car driven (the US disc's labels; the ones it has are kept).
    const char* const wanted[] = {"panda_super_ie_90", "impreza_wrx_sti_02", "rx8_03", "nsx_r_02", "ford_gt_05", "r390_race_98", "r34_gtr_v2_nur_02"};
    for (const char* label : wanted) plan.cars.emplace_back(label, 0);
    return plan;
}

ProgressReport play_progress(Runtime& rt, const ProgressPlan& plan) {
    ProgressReport out;
    Driver d{rt, out};
    if (rt.data_dir().empty()) {
        out.error = "the runtime has no data folder (no memory card)";
        return out;
    }
    try {
        if (!d.boot_to_title() || !d.new_game()) return out;
        GameState& g = rt.game();
        g.username = plan.name;
        out.steps.push_back("new game for " + plan.name + " on day " + std::to_string(g.today - GameState::kFirstDay));
        // The cars.
        int last = -1;
        for (const auto& [label, paint] : plan.cars) {
            const int place = d.buy(label, paint);
            if (place < 0) {
                out.steps.push_back("not bought: " + label);
                continue;
            }
            last = place;
            out.steps.push_back("bought " + label + " (paint " + std::to_string(paint) + ") into place " + std::to_string(place));
        }
        if (last >= 0) {
            const Value r = d.call(rt.vm().attribute(d.game(), "garage"), "changeRidingCar", {Value::integer(last)});
            out.steps.push_back("driving place " + std::to_string(last) + ": " + (r.is_nil() ? std::string() : r.to_string()));
        }
        // The license tests, each passed in its medal time (the program's result routine for a
        // license test, 0x1105A8: its unit, its ranking, the diary's day).
        std::shared_ptr<GameNatives> n = natives_of(rt);
        for (const std::string& cat : plan.licenses) {
            int passed = 0;
            for (int k = 1; k <= 16; ++k) {
                char label[16];
                std::snprintf(label, sizeof label, "%s%04d", cat.c_str(), k);
                if (n && n->db().race_unit(label) < 0) continue;
                // The medal's time: the RACE row's (+0x76 minutes and seconds, +0x1A ms; gold,
                // then silver and bronze two bytes further on).
                const int medal = plan.license_result == "silver" ? 1 : plan.license_result == "bronze" ? 2 : 0;
                const ByteSpan row = n ? n->db().row("RACE", label) : ByteSpan{};
                u32 time = 0;
                const size_t m = 0x76 + 2 * size_t(medal), ms = 0x1A + 2 * size_t(medal);
                if (row.size() > m + 1 && row.size() > ms + 1) time = u32(row[m]) * 60'000 + u32(row[m + 1]) * 1'000 + u32(row[ms] | row[ms + 1] << 8);
                RaceResult r;
                r.kind = RaceResult::Kind::License, r.race = label, r.result = plan.license_result, r.time_ms = time > 100 ? time - 100 : 30'000;
                rt.race_finished(r);
                ++passed;
            }
            const Value passed_now = d.call(rt.vm().attribute(d.game(), "race_record"), "isLicensePassed", {Value::string(cat)});
            out.steps.push_back(cat + ": " + std::to_string(passed) + " tests " + plan.license_result + (passed_now.i ? ", license held" : ", NOT held"));
        }
        // The presents the records entitle the player to (license papers, their cars, courses by
        // weeks gone by), through the present page.
        const int cars_before = g.garage_count();
        if (!d.presents()) return out;
        out.steps.push_back("presents: " + std::to_string(g.garage_count() - cars_before) + " cars got; " + std::to_string(g.available_courses.size()) +
                            " courses open in Arcade");
        g.money = plan.credits;
        g.username = plan.name;
        // Saved by the card manager, as the map's auto save does.
        const save::Result saved = n ? n->card().save(g) : save::Result::NoCard;
        if (saved != save::Result::Ok) {
            out.error = std::string("save: ") + save::result_name(saved);
            return out;
        }
        out.steps.push_back("saved: " + std::to_string(g.garage_count()) + " cars, " + std::to_string(g.money) + " credits, day " +
                            std::to_string(g.today - GameState::kFirstDay));
        out.ok = true;
    } catch (const std::exception& e) {
        out.error = e.what();
    }
    return out;
}

} // namespace gt4r::menu
