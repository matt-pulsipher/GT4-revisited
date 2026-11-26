// A game played forward without its races (game_progress.h): the game's own menus from power on
// to a new game, then what a player's purchases and license tests write, through the scripts'
// natives and the program's result routines (Runtime::race_finished), the presents through the
// present page, and the card manager's save.

#include "gt4r/menu/game_progress.h"
#include "gt4r/formats/db/race_labels.h"

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
    // Gran Turismo mode's script's own functions and data (its project loaded).
    Value project(const std::string& path) { return rt.vm().lookup("GTmodeProject::" + path); }
    Value script_call(const std::string& fn, std::vector<Value> args) {
        const Value f = project(fn);
        if (f.kind != script::Kind::Function) throw std::runtime_error("GTmodeProject::" + fn + " not found");
        return rt.vm().call(f, std::span<const Value>(args.data(), args.size()));
    }
    // An event list's race codes (the event data's lists hold the list's name first, then the
    // events; getRaceEventRecordStatistics reads them from index 1).
    std::vector<std::string> event_codes(const std::string& list) {
        std::vector<std::string> out;
        const Value v = project("EventData::" + list);
        const std::vector<Value>* items = v.items();
        if (!items) return out;
        for (size_t j = 1; j < items->size(); ++j) {
            const Value code = rt.vm().attribute((*items)[j], "race_code");
            if (code.kind == script::Kind::String) out.push_back(code.as_string());
        }
        return out;
    }
    bool won(const std::string& code) { return script_call("wonRace", {Value::string(code)}).truthy(); }
    // An event won in first place, each race's result through the program's result routines
    // (0x114498 a race; 0x119CB8 / 0x119F28 a championship's races, the player first in each),
    // as the race sequence starts each race afresh. The races run: their count.
    int win_event(const std::string& code, const std::string& car) {
        const Value subs = script_call("extractSubRaceCode", {Value::string(code)});
        const std::vector<Value>* list = subs.items();
        if (!list || list->empty()) return 0;
        const bool championship = script_call("isChampionship", {Value::string(code)}).truthy();
        GameState& g = rt.game();
        int run = 0;
        for (const Value& sub : *list) {
            g.race_session = {};
            RaceResult r;
            r.kind = championship ? RaceResult::Kind::Championship : RaceResult::Kind::Event;
            r.race = sub.as_string();
            if (championship) r.series = code;
            r.place = 0, r.finished = true, r.time_ms = 0;
            for (int e = 0; e < 6; ++e) {
                RaceResult::Finisher f;
                f.entry = e, f.car = car, f.time_ms = u32(e);
                r.order.push_back(f);
            }
            rt.race_finished(r);
            ++run;
        }
        if (championship) g.suspend_series(false), g.series = {};
        return run;
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

int completion_permille(Runtime& rt) {
    const std::shared_ptr<script::Module> m = rt.vm().lookup("GTmodeProject").as<script::Module>();
    const Value f = rt.vm().lookup("GTmodeProject::getRaceEventRecordStatistics");
    if (!m || f.kind != script::Kind::Function) return -1;
    for (const char* cached : {"cached_achievement", "cached_aspec_score"})
        if (const std::shared_ptr<script::Variable> v = m->find(cached)) v->value = {};
    const Value r = rt.vm().call(f, {});
    const std::vector<Value>* items = r.items();
    return items && !items->empty() ? int((*items)[0].as_float()) : -1;
}

ProgressPlan default_progress_plan(Runtime& rt) {
    ProgressPlan plan;
    (void)rt;
    // A small car, a four-wheel drive sedan, sports cars front- and mid-engined, a racing car,
    // and a four-wheel drive sports car driven (the US disc's labels; the ones it has are kept).
    const char* const wanted[] = {"panda_super_ie_90", "impreza_wrx_sti_02", "rx8_03", "nsx_r_02", "ford_gt_05", "r390_race_98", "r34_gtr_v2_nur_02"};
    for (const char* label : wanted) plan.cars.emplace_back(label, 0);
    // Gran Turismo mode opened up: every Beginner and Professional event but the Gran Turismo World
    // Championship won (the championship then open: canEntryGTWorldChamp), the endurance hall's
    // 25 % (canEntryEndurance) passed with more events (not the Extreme or Endurance halls',
    // which stay to be run), and a car of the Supercar Festival's power driven.
    plan.completion_permille = 260;
    plan.fill_lists_skipped = {"ExtremeHall", "EnduranceHall"};
    plan.driven = "ford_gt_05";
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
                // The RACE table's label has the region in front ("usl0b0001"), as the record's
                // lookup tries it (0x430D20).
                ByteSpan row;
                for (const std::string& tried : db::race_labels(label, n ? n->rt.region() : std::string()))
                    if (row.empty() && n) row = n->db().row("RACE", tried);
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
        // The events won, each race a day of the calendar.
        const std::string won_car = last >= 0 ? g.garage[size_t(last)].car : std::string();
        auto skipped = [&](const std::string& code) {
            return std::find(plan.events_not_won.begin(), plan.events_not_won.end(), code) != plan.events_not_won.end();
        };
        for (const std::string& list : plan.event_lists) {
            const std::vector<std::string> codes = d.event_codes(list);
            int events = 0, races = 0;
            for (const std::string& code : codes)
                if (!skipped(code) && !d.won(code)) races += d.win_event(code, won_car), ++events;
            out.steps.push_back(list + ": " + std::to_string(events) + " of " + std::to_string(codes.size()) + " events won (" + std::to_string(races) +
                                " races)");
        }
        // More events won until the completion asked for: the event data's other lists in turn.
        int done = completion_permille(rt);
        {
            std::string names;
            const std::vector<Value>* lists = d.project("EventData::AllEvents").items();
            for (size_t i = 0; lists && i < lists->size(); ++i)
                if (const std::vector<Value>* e = (*lists)[i].items(); e && !e->empty()) names += " " + (*e)[0].to_string() + "(" + std::to_string(e->size() - 1) + ")";
            out.steps.push_back("event lists:" + names);
        }
        if (plan.completion_permille > 0 && done < plan.completion_permille) {
            const Value all = d.project("EventData::AllEvents");
            const std::vector<Value>* lists = all.items();
            for (size_t i = 0; lists && i < lists->size() && done < plan.completion_permille; ++i) {
                const std::vector<Value>* events = (*lists)[i].items();
                if (!events || events->empty()) continue;
                const std::string name = (*events)[0].to_string();
                if (std::find(plan.fill_lists_skipped.begin(), plan.fill_lists_skipped.end(), name) != plan.fill_lists_skipped.end()) continue;
                int n = 0;
                for (size_t j = 1; j < events->size() && done < plan.completion_permille; ++j) {
                    const Value code = rt.vm().attribute((*events)[j], "race_code");
                    if (code.kind != script::Kind::String || skipped(code.as_string()) || d.won(code.as_string())) continue;
                    d.win_event(code.as_string(), won_car);
                    ++n;
                    done = completion_permille(rt);
                }
                if (n) out.steps.push_back("won " + std::to_string(n) + " events of " + name);
            }
        }
        if (done >= 0) out.steps.push_back("completion " + std::to_string(done / 10) + "." + std::to_string(done % 10) + " %");
        // The presents the records entitle the player to (license papers, their cars, courses by
        // weeks gone by, cars by completion), through the present page.
        const int cars_before = g.garage_count();
        if (!d.presents()) return out;
        out.steps.push_back("presents: " + std::to_string(g.garage_count() - cars_before) + " cars got; " + std::to_string(g.available_courses.size()) +
                            " courses open in Arcade");
        if (!plan.driven.empty())
            for (size_t i = 0; i < g.garage.size(); ++i)
                if (g.garage[i].used && g.garage[i].car == plan.driven) {
                    d.call(rt.vm().attribute(d.game(), "garage"), "changeRidingCar", {Value::integer(s32(i))});
                    out.steps.push_back("driving " + plan.driven + " (place " + std::to_string(i) + ", " + std::to_string(g.garage[i].power / 100) + " PS)");
                    break;
                }
        g.money = plan.credits;
        g.username = plan.name;
        // Saved by the card manager, as the map's auto save does.
        if (n) apply_monitor(*n);
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
