// The title's demonstrations: MDemonstration (Status + 0x3B018; its natives 0x15DB40 .. 0x15E4C0) and the program's
// `autodemo` sequence's choice of what plays (0x10BE20 with 0x434638, 0x434770; docs/formats/menu.md "The title's
// demonstrations").

#include "gt4r/menu/game_internal.h"

#include <chrono>

namespace gt4r::menu {

using script::Args;
using script::Value;
using script::Vm;

namespace {

// The kinds by name (0x622D90; the setter 0x434690 compares the text, any other value is kind 0).
constexpr const char* kDemoKinds[5] = {"none", "autodemo", "menu", "replay", "skip_replay"};

// 0x579358: the console's timer (0x5B8450: T0's count with its wraps) plus a draw of the program's small generator; here the
// app's clock in microseconds.
u32 timer_draw(GameNatives& n) {
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return u32(us) + n.random.step();
}

} // namespace

void install_demonstration(GameNatives& n, Object& game) {
    GameState* s = &n.state();
    GameNatives* np = &n;
    auto demo = std::make_shared<Object>("MDemonstration");
    GameState::Demonstration* d = &s->demonstration;
    // initialize(count, replay_time, repeat_time) (0x15DB40 -> 0x4345D8): the list's length, the recorded runs' time, the
    // tries a time-out makes; the list from its start, the card's replays reset (0x434950), the movie's count from the
    // option `demo_movie_interval` less 1.
    demo->method("initialize", [s, d, np](Vm&, const Value&, Args a) {
        d->count = arg_int(a, 0), d->replay_time = arg_int(a, 1), d->repeat = d->left = arg_int(a, 2);
        d->skip_time = 3, d->index = 0, d->skip_at = 0, d->card_at = 0, d->card_seed = timer_draw(*np);
        d->movie_left = s->option_int("demo_movie_interval", 3) - 1;
        return Value{};
    });
    // resetPlayListCounter (0x15E488 -> 0x434950): the card's replays from the first of the list, a new shuffled order.
    demo->method("resetPlayListCounter", [d, np](Vm&, const Value&, Args) {
        d->card_at = 0, d->card_seed = timer_draw(*np);
        return Value{};
    });
    demo->method("resetMovieCount", [d](Vm&, const Value&, Args) { // (0x15E4C0 -> 0x434768: the movie at the next time-out)
        d->movie_left = 0;
        return Value{};
    });
    demo->property("type", [d] { return text(kDemoKinds[std::clamp(d->type, 0, 4)]); }, [d](const Value& v) {
        d->type = 0;
        if (v.kind == script::Kind::String)
            for (int k = 0; k < 5; ++k)
                if (v.as_string() == kDemoKinds[k]) d->type = k;
    });
    demo->property("name", [d] { return text(d->name); }, [d](const Value& v) { d->name = v.kind == script::Kind::String ? v.as_string() : std::string(); });
    demo->property("arg", [d] { return text(d->arg); }, [d](const Value& v) { d->arg = v.kind == script::Kind::String ? v.as_string() : std::string(); });
    auto word = [demo](const char* name, s32* at) {
        demo->property(name, [at] { return number(*at); }, [at](const Value& v) { *at = v.is_number() ? s32(v.as_float()) : 0; });
    };
    word("repeat_time", &d->repeat), word("skip_time", &d->skip_time), word("restart_from_beginning", &d->restart);
    game.set("demonstration", object_value(demo));
}

// 0x434770: the next demonstration. When the movie's count has run out (and the option asks for the movie at all), the
// movie (kind menu: project demo_movie, "opening"); else the configuration's setNextDemo(index) sets the kind, name and
// argument of the list's entry (the list from its start past its end). Then the movie's count down (from the option
// less 1 again below 0) and the time-out's tries: false when they are spent (or the script set nothing).
bool next_demonstration(Runtime& rt) {
    GameState& s = rt.game();
    GameState::Demonstration& d = s.demonstration;
    bool more = true;
    const s32 interval = s.option_int("demo_movie_interval", 3);
    if (d.movie_left <= 0 && interval > 0) {
        d.type = 2, d.name = "demo_movie", d.arg = "opening";
    } else {
        Vm& vm = rt.vm();
        const Value f = vm.lookup("setNextDemo");
        Value got;
        if (f.p && f.p->callable()) try {
                const Value at[1] = {Value::integer(d.index)};
                got = vm.call(f, at);
            } catch (const script::ScriptError& e) {
                rt.note(std::string("setNextDemo stopped: ") + e.what());
            }
        if (++d.index >= d.count) d.index = 0;
        more = got.truthy();
    }
    if (--d.movie_left < 0) d.movie_left = interval - 1;
    if (--d.left <= 0) d.left = d.repeat, more = false;
    return more;
}

// 0x10BE20: a time-out's demonstrations, one at a time. `first`: the sequence starts (0x434638: the tries anew; with
// restart_from_beginning, the list and the movie's count from their start). The menu project to run and its argument
// (the movie), the disc's recorded race (kind 3, replay: the jump table 0x68D000 sends it to 0x10BEF0, the card's replays
// first by 0x4349A8, else the recording by the entry's name through 0x10BBB0), or none when the sequence is over (the menus
// go back to the last project, 0x42FC50). Every disc's list is kind replay (setNextDemo: Demo(2, "RdemoN").getType() names 2
// "replay"); kinds 1 (an Arcade demo race, 0x10BA28) and 4 are never set. ❓ The card's replays before the disc's are not
// played here.
std::optional<DemoStep> autodemo_step(Runtime& rt, bool first) {
    GameState& s = rt.game();
    GameState::Demonstration& d = s.demonstration;
    if (first) {
        d.left = d.repeat;
        if (d.restart) {
            d.index = 0, d.card_at = 0, d.movie_left = s.option_int("demo_movie_interval", 3) - 1;
            if (const auto n = natives_of(rt)) d.card_seed = timer_draw(*n); // (0x434950)
        }
        d.more = true;
    } else if (s.menu_result == "terminate") {
        d.more = false; // (the movie skipped: 0x10C008; a recorded race left with a key: 0x10BBB0's end code 2)
    }
    while (d.more) {
        d.more = next_demonstration(rt);
        if (d.type == 2) return DemoStep{d.name, d.arg, {}};
        if (d.type == 3 && !d.name.empty()) {
            const std::string file = "replay/" + d.name + "/" + d.name;
            if (rt.reader()(file)) return DemoStep{{}, {}, file};
            rt.note("the demonstration's recording " + file + " is not on the disc");
            continue;
        }
        if (d.type != 0) rt.note("the demonstration's kind " + std::to_string(d.type) + " (" + d.name + ") plays nothing here");
    }
    return std::nullopt;
}

} // namespace gt4r::menu
