// The menus running: the event thread, the widgets' own behavior, the actors and the game's
// state on layouts made up here; then the game's own projects from the discs of GT4R_DISC:
// boot to the title, arcade to its race request, the options and back, the GT mode entry.

#include "test_framework.h"

#include "gt4r/menu/program_tables.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/menu/runtime_internal.h"

#include <cmath>
#include <cstring>
#include <filesystem>

using namespace gt4r;
using script::Value;

namespace {

menu::FileReader no_files() {
    return [](const std::string&) -> std::optional<Bytes> { return std::nullopt; };
}

Value call(script::Vm& vm, const Value& of, const char* method, std::vector<Value> args = {}) { return vm.call(vm.attribute(of, method), args); }
void assign(script::Vm& vm, const Value& of, const char* member, const Value& v) { vm.member(of, member)->set(vm, v); }
Value native(std::function<Value(script::Vm&, script::Args)> fn) {
    return script::Vm::native("test", [fn](script::Vm& vm, const Value&, script::Args a) { return fn(vm, a); });
}

// A page with two buttons side by side and a box of three rows that shows one at a time.
const char* kLayout = R"(Project {
	name string {"TestProject"}
	children [2] {
		RootWindow {
			name string {"Top"}
			children [3] {
				Composite { name string {"Left"} can_focus digit {1} geometry rectangle {100 100 80 30} }
				Composite { name string {"Right"} can_focus digit {1} geometry rectangle {300 100 80 30} }
				ScrollBox {
					name string {"Scroll"}
					geometry rectangle {100 300 320 68}
					children [3] {
						SelectBox {
							name string {"Box"}
							geometry rectangle {-64 16 460 36}
							direction digit {1}
							border_width digit {64}
							children [4] {
								Composite { name string {"a"} geometry rectangle {0 0 320 24} }
								Composite { name string {"b"} geometry rectangle {0 0 320 24} }
								Composite { name string {"c"} geometry rectangle {0 0 320 24} }
								Composite { name string {"d"} geometry rectangle {0 0 320 24} }
							}
						}
						Composite { name string {"dec_arrow"} geometry rectangle {145 2 30 15} }
						Composite { name string {"inc_arrow"} geometry rectangle {145 51 30 15} }
					}
				}
			}
		}
		RootWindow {
			name string {"Dialog"}
			children [1] {
				Composite { name string {"OK"} can_focus digit {1} geometry rectangle {280 220 80 30} }
			}
		}
	}
})";

} // namespace

// A select box places its rows as the program does: the chosen row in the middle of the box,
// the others a row apart, the chosen one active; the scroll box around it lights its arrows.
TEST(menu_select_box_places_rows) {
    menu::Runtime rt(no_files());
    const std::shared_ptr<menu::LiveWidget> project = rt.start_layout(menu::parse_layout(std::string_view(kLayout)));
    script::Vm& vm = rt.vm();
    const std::shared_ptr<menu::LiveWidget> top = project->child("Top");
    const std::shared_ptr<menu::LiveWidget> box = top->child("Scroll")->child("Box");
    CHECK(box->item_h == 24 && box->item_w == 320);
    auto y = [&](size_t i) { return box->children[i]->data.rect("geometry")->y; };
    // Four rows of 24 in a box of 36: the first is centered at 6, the next a row below.
    CHECK(y(0) == 6 && y(1) == 30 && y(3) == 78 && box->children[0]->data.rect("geometry")->x == 64);
    CHECK((box->children[0]->state & menu::kActive) && !(box->children[1]->state & menu::kActive));
    // A script's own setting of the index puts the rows in place at once.
    assign(vm, menu::widget_value(box), "index", Value::integer(3));
    CHECK(y(3) == 6 && y(0) == -66 && (box->children[3]->state & menu::kActive));
    // With the focus in the box, the keys along it move the index, and the rows slide there.
    rt.open_page(top, true);
    rt.set_focus(*top, box->children[3]);
    rt.key(menu::Key::Up);
    rt.update();
    CHECK(box->index() == 2 && rt.focused() == box->children[2].get());
    CHECK(y(2) > -18 && y(2) < 6); // on its way from -18 to 6
    rt.settle();
    for (int i = 0; i < 60; ++i) rt.update();
    CHECK(y(2) == 6 && y(3) == 30);
    // The arrows: rows before and after the box's ends.
    CHECK((top->child("Scroll")->child("dec_arrow")->state & menu::kActive) && (top->child("Scroll")->child("inc_arrow")->state & menu::kActive));
    // At the end the key is not the box's: nothing moves.
    assign(vm, menu::widget_value(box), "index", Value::integer(0));
    rt.set_focus(*top, box->children[0]);
    rt.key(menu::Key::Up);
    rt.settle();
    CHECK(box->index() == 0);
    for (int i = 0; i < 4; ++i) rt.update();
    CHECK(!(top->child("Scroll")->child("dec_arrow")->state & menu::kActive));
    // Copies appended to an empty box take the first one's size.
    call(vm, menu::widget_value(box), "clearChildren");
    CHECK(box->children.empty() && call(vm, menu::widget_value(box), "Count").i == 0);
    const Value row = call(vm, menu::widget_value(top->child("Left")), "doCopy");
    call(vm, menu::widget_value(box), "appendChild", {rt.context(), row});
    CHECK(box->children.size() == 1 && box->item_w == 80 && box->item_h == 30 && y(0) == 3); // one row of 30 centered in 36
}

// The event thread: a key goes to the focused widget and up its parents; a handler that waits
// holds the thread; a dialog's own event loop lets events through until it is left.
TEST(menu_event_thread_waits_and_nests) {
    menu::Runtime rt(no_files());
    const std::shared_ptr<menu::LiveWidget> project = rt.start_layout(menu::parse_layout(std::string_view(kLayout)));
    script::Vm& vm = rt.vm();
    const std::shared_ptr<menu::LiveWidget> top = project->child("Top"), dialog = project->child("Dialog");
    const Value context = rt.context();
    std::vector<std::string> seen;
    int result = 0;
    // Left: activate waits half a second, then opens the dialog and waits in its event loop.
    top->child("Left")->define("onActivate", native([&](script::Vm& v, script::Args) {
        seen.push_back("left activated");
        v.then_call(v.attribute(context, "sync"), {Value::number(0.5f)}, [&](script::Vm& v2, const Value&) {
            seen.push_back("waited");
            call(v2, context, "pushPage", {menu::widget_value(dialog)});
            v2.then_call(v2.attribute(call(v2, context, "getUpdateContext"), "enterEventLoop"), {}, [&](script::Vm&, const Value&) {
                seen.push_back("dialog answered " + std::to_string(result));
                return Value::integer(2);
            });
            return Value{};
        });
        return Value{};
    }));
    dialog->child("OK")->define("onActivate", native([&](script::Vm& v, script::Args a) {
        result = 7;
        call(v, a[0], "closePage", {menu::widget_value(dialog)});
        call(v, call(v, a[0], "getUpdateContext"), "leaveEventLoop");
        return Value::integer(2);
    }));
    top->define("onKeyPress", native([&](script::Vm&, script::Args a) {
        seen.push_back("page saw key " + std::to_string(a.size() > 1 ? rt.vm().attribute(a[1], "keysym").i : 0));
        return Value::integer(0);
    }));
    top->child("Right")->define("onFocusEnter", native([&](script::Vm&, script::Args) {
        seen.push_back("right focused");
        return Value::integer(1);
    }));
    rt.open_page(top, true);
    CHECK(rt.focused() == top->child("Left").get() && (top->child("Left")->state & menu::kFocus));
    // Right moves the focus (the page's own handling of the key, after its script passed it on).
    rt.key(menu::Key::Right);
    rt.update();
    CHECK(rt.focused() == top->child("Right").get() && seen.size() == 2 && seen[0] == "page saw key 65363" && seen[1] == "right focused");
    CHECK(!rt.sound_calls().empty() && rt.sound_calls().back().text() == "sound.play(cursor)");
    rt.key(menu::Key::Left);
    rt.update();
    CHECK(rt.focused() == top->child("Left").get());
    seen.clear();
    // Accept: the handler waits 30 frames; a key pressed meanwhile is handled after it.
    rt.key(menu::Key::Accept);
    rt.update();
    CHECK(seen.size() == 2 && seen[1] == "left activated" && !rt.idle());
    rt.key(menu::Key::Right);
    for (int i = 0; i < 10; ++i) rt.update();
    CHECK(seen.size() == 2 && rt.focused() == top->child("Left").get()); // still waiting: nothing moved
    for (int i = 0; i < 25; ++i) rt.update();
    // The dialog is open, its own loop running: the key queued went to the dialog's page.
    CHECK(rt.current_page() == dialog.get() && rt.focused() == dialog->child("OK").get() && rt.idle());
    CHECK(seen.size() >= 3 && seen[2] == "waited");
    rt.key(menu::Key::Accept);
    rt.settle();
    CHECK(result == 7 && rt.current_page() == top.get() && seen.back() == "dialog answered 7" && rt.idle());
    // flushKeyEvent drops the presses not yet handled.
    rt.key(menu::Key::Right);
    call(vm, context, "flushKeyEvent");
    rt.settle();
    CHECK(rt.focused() == top->child("Left").get());
    for (const std::string& line : rt.log()) {
        if (line.find("script error") != std::string::npos) std::printf("  %s\n", line.c_str());
        CHECK(line.find("script error") == std::string::npos);
    }
}

// The actors follow the program's routines: a fade's value moves by its velocity a frame, a
// move goes a ratio of what is left, a blink is a wave of its period.
TEST(menu_actors_move_as_the_program_does) {
    menu::Runtime rt(no_files());
    const std::shared_ptr<menu::LiveWidget> project = rt.start_layout(menu::parse_layout(std::string_view(kLayout)));
    script::Vm& vm = rt.vm();
    const std::shared_ptr<menu::LiveWidget> left = project->child("Top")->child("Left"), right = project->child("Top")->child("Right");
    const Value menu_module = vm.lookup("menu");
    // MFadeActor(widget, 1): as made it stands at its destination (0) and comes in at 1/30 a frame.
    const Value fade = call(vm, menu_module, "MFadeActor", {menu::widget_value(left), Value::integer(1)});
    assign(vm, menu::widget_value(left), "actor", fade);
    CHECK(left->opacity() == 0 && vm.attribute(menu::widget_value(left), "actor").p == fade.p);
    rt.update();
    CHECK(std::fabs(left->opacity() - 1.0f / 30.0f) < 1e-5f);
    // period: the whole way in that many seconds.
    assign(vm, fade, "period", Value::number(0.25f));
    assign(vm, fade, "out", Value::integer(0));
    call(vm, fade, "warp");
    CHECK(left->opacity() == 1);
    assign(vm, fade, "out", Value::integer(1));
    for (int i = 0; i < 7; ++i) rt.update();
    CHECK(std::fabs(left->opacity() - (1.0f - 7.0f / 15.0f)) < 1e-4f);
    for (int i = 0; i < 10; ++i) rt.update();
    CHECK(left->opacity() == 0);
    // repeat: back and forth.
    assign(vm, fade, "repeat", Value::integer(1));
    assign(vm, fade, "out", Value::integer(0));
    for (int i = 0; i < 20; ++i) rt.update();
    CHECK(left->opacity() > 0 && left->opacity() < 1);
    // MMoveActor(widget, x, y): from where the widget is to there, a ratio of what is left a frame.
    const Value move = call(vm, menu_module, "MMoveActor", {menu::widget_value(right), Value::integer(300), Value::integer(300)});
    assign(vm, menu::widget_value(right), "actor", move);
    assign(vm, move, "ratio", Value::number(0.5f));
    assign(vm, move, "out", Value::integer(1));
    rt.update();
    CHECK(right->data.rect("geometry")->y == 200 && right->data.rect("geometry")->x == 300);
    rt.update();
    CHECK(right->data.rect("geometry")->y == 250);
    call(vm, move, "warp");
    CHECK(right->data.rect("geometry")->y == 300);
    assign(vm, move, "out", Value::integer(0));
    call(vm, move, "warp");
    CHECK(right->data.rect("geometry")->y == 100);
    // An actor taken off its widget no longer moves it.
    assign(vm, move, "out", Value::integer(1));
    assign(vm, menu::widget_value(right), "actor", Value{});
    rt.update();
    CHECK(right->data.rect("geometry")->y == 100);
    // MBlinkActor: period 0.1 s is 6 frames from 1 down to 0, again and again.
    const Value blink = call(vm, menu_module, "MBlinkActor", {menu::widget_value(right)});
    assign(vm, menu::widget_value(right), "actor", blink);
    assign(vm, blink, "period", Value::number(0.1f));
    rt.update();
    CHECK(std::fabs(right->opacity() - 5.0f / 6.0f) < 1e-4f);
    for (int i = 0; i < 4; ++i) rt.update();
    CHECK(std::fabs(right->opacity() - 1.0f / 6.0f) < 1e-4f);
    // The transition: a colour over the page in a quarter of a second, and back.
    const Value transition = call(vm, menu_module, "MColorTransition");
    assign(vm, rt.context(), "transition", transition);
    call(vm, transition, "panOutIn", {rt.context()});
    for (int i = 0; i < 8; ++i) rt.update();
    CHECK(std::fabs(rt.fade_cover() - 8.0f / 15.0f) < 1e-4f);
    for (int i = 0; i < 9; ++i) rt.update();
    CHECK(rt.fade_cover() > 0.9f);
    for (int i = 0; i < 20; ++i) rt.update();
    CHECK(rt.fade_cover() == 0);
    // interpolateY: a ratio of the way; within a unit it arrives and says so.
    const Value w = menu::widget_value(left);
    CHECK(call(vm, w, "interpolateY", {Value::integer(200), Value::number(0.5f)}).i == 0 && left->data.rect("geometry")->y == 150);
    for (int i = 0; i < 6; ++i) call(vm, w, "interpolateY", {Value::integer(200), Value::number(0.5f)});
    CHECK(call(vm, w, "interpolateY", {Value::integer(200), Value::number(0.5f)}).i == 1 && left->data.rect("geometry")->y == 200);
}

// The game's state: the options as text and back, the sequencing the scripts set, the race
// request read once a project finished.
TEST(menu_game_state) {
    menu::GameState g;
    g.option_defaults("us");
    CHECK(g.option_int("race_laps") == 2 && g.option_text("special_arcade_tuner").empty() && g.option_text("unit_velocity") == "MPH" && g.power_unit().name == std::string("HP"));
    g.options["race_laps"] = 5;
    g.options["netconf"] = std::string("a \"b\"");
    g.options["some_ratio"] = 0.25f;
    menu::GameState h;
    h.option_defaults("jp");
    h.options_from_text(g.options_text());
    CHECK(h.option_int("race_laps") == 5 && h.option_text("netconf") == "a \"b\"" && std::get<float>(h.options["some_ratio"]) == 0.25f);
    CHECK(h.option_text("game_zone") == "us");

    menu::Runtime rt(no_files());
    rt.boot();
    script::Vm& vm = rt.vm();
    const Value game = vm.lookup("game");
    CHECK(game.kind == script::Kind::Object && vm.lookup("sound").kind == script::Kind::Object);
    assign(vm, game, "course_code", Value::string("tsukuba"));
    assign(vm, game, "car_code", Value::string("ka_01"));
    assign(vm, game, "car_color", Value::integer(3));
    assign(vm, game, "next_sequence", Value::string("single_race"));
    assign(vm, game, "difficulty", Value::string("HARD"));
    CHECK(rt.game().race.course == "tsukuba" && rt.game().race.paint == 3 && vm.attribute(game, "course_code").as_string() == "tsukuba");
    CHECK(!rt.race_request()); // nothing finished yet
    call(vm, rt.context(), "finish");
    const std::optional<menu::RaceRequest> race = rt.race_request();
    CHECK(race && race->sequence == "single_race" && race->car == "ka_01" && race->difficulty == "HARD" && race->max_entry == 6);
    CHECK(!rt.advance());
    // Credits and the garage.
    CHECK(vm.attribute(game, "money").i == 10000 && call(vm, game, "isEnoughMoney", {Value::integer(10001)}).i == 0);
    call(vm, game, "incMoney", {Value::integer(-4000)});
    CHECK(rt.game().money == 6000);
    const Value garage = vm.attribute(game, "garage");
    CHECK(call(vm, garage, "hasRidingCar").i == 0 && vm.attribute(garage, "count").i == 0);
    CHECK(call(vm, garage, "addNewCar", {Value::string("ka_01"), Value::integer(2)}).i == 0);
    call(vm, garage, "setRidingCar", {Value::integer(0)});
    CHECK(call(vm, garage, "hasRidingCar").i == 1 && vm.attribute(vm.attribute(garage, "riding_car"), "car_code").as_string() == "ka_01");
    // The menu stack and what arcade has unlocked.
    call(vm, game, "menuPush", {Value::string("SingleRace")});
    CHECK(call(vm, game, "menuRef").as_string() == "SingleRace" && call(vm, game, "menuPop").as_string() == "SingleRace" && call(vm, game, "menuRef").is_nil());
    CHECK(call(vm, game, "isCarAvailable", {Value::string("ka_01")}).i == 0);
    call(vm, game, "setCarAvailable", {Value::string("ka_01")});
    CHECK(call(vm, game, "isCarAvailable", {Value::string("ka_01")}).i == 1);
    // Every sound call arrives in one place.
    call(vm, vm.lookup("sound"), "play", {Value::string("ok")});
    CHECK(!rt.sound_calls().empty() && rt.sound_calls().back().object == "sound" && rt.sound_calls().back().method == "play" &&
          rt.sound_calls().back().args[0].as_string() == "ok");
    // A memory card with no saved game: the file is not there; no card: no answer.
    const Value manager = call(vm, vm.lookup("menu"), "MMemoryCardManager", {Value::string("MODE_LOAD_GAME")});
    CHECK(call(vm, manager, "isConnect").i == 1 && call(vm, call(vm, manager, "getNewFile"), "isExist").i == 1);
    rt.game().card.connected = false;
    CHECK(call(vm, manager, "isConnect").i == 0 && call(vm, manager, "isFormat").i == 0);
}

// The program's name lists are found by what they are.
TEST(menu_program_tables_found) {
    // A made-up image: names, then a list of {number, name address} entries among other data.
    const u32 base = 0x100000;
    Bytes image(0x400, 0);
    auto put_name = [&](size_t at, const char* s) { std::memcpy(image.data() + at, s, std::strlen(s)); };
    auto put32 = [&](size_t at, u32 v) { std::memcpy(image.data() + at, &v, 4); };
    put_name(0x10, "renault"), put_name(0x20, "hks"), put_name(0x28, "polyphony"), put_name(0x38, "isuzu");
    put_name(0x40, "JP"), put_name(0x44, "US"), put_name(0x48, "DE"), put_name(0x4C, "FR");
    put_name(0x50, "NORMAL"), put_name(0x58, "RACING"), put_name(0x60, "TUNING"), put_name(0x68, "CONCEPT");
    const std::pair<u32, u32> makers[] = {{32, 0x10}, {31, 0x28}, {52, 0x38}, {1, 0x10}};
    const std::pair<u32, u32> tuners[] = {{34, 0x10}, {76, 0x20}, {33, 0x28}, {56, 0x38}};
    const std::pair<u32, u32> countries[] = {{2, 0x40}, {3, 0x44}, {5, 0x48}, {6, 0x4C}};
    const std::pair<u32, u32> categories[] = {{0, 0x50}, {1, 0x58}, {2, 0x60}, {3, 0x68}};
    size_t at = 0x100;
    for (const auto* list : {makers, tuners, countries, categories}) {
        for (int i = 0; i < 4; ++i) put32(at, list[i].first), put32(at + 4, base + list[i].second), at += 8;
        put32(at, 0xFFFFFFFFu), at += 0x20; // what follows a list is no entry
    }
    const menu::ProgramTables t = menu::ProgramTables::find(image, base);
    CHECK(t.makers.size() == 4 && t.makers.at(52) == "isuzu" && t.tuners.size() == 4 && t.tuners.at(76) == "hks");
    CHECK(t.countries.size() == 4 && t.countries.at(2) == "JP" && t.categories.at(3) == "CONCEPT");
}

// ---------------------------------------------------------------------------------------------
// The game's own projects
// ---------------------------------------------------------------------------------------------

namespace {

struct Flow {
    Disc& disc;
    std::unique_ptr<menu::Runtime> rt;

    explicit Flow(Disc& d, const std::string& language = "US", const std::string& data_dir = {}) : disc(d) {
        menu::Runtime::Config config;
        config.language = language;
        config.data_dir = data_dir;
        config.list = [&d](const std::string& folder) {
            std::vector<std::string> out;
            for (const gt3r::Volume::Entry& e : d.volume().entries()) {
                if (e.path.compare(0, folder.size(), folder) != 0) continue;
                const size_t slash = e.path.find('/', folder.size());
                std::string name = e.path.substr(folder.size(), slash == std::string::npos ? slash : slash + 1 - folder.size());
                if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
            }
            return out;
        };
        config.program = [&d] { return d.read_program(); };
        rt = std::make_unique<menu::Runtime>(
            [&d](const std::string& path) -> std::optional<Bytes> {
                const gt3r::Volume::Entry* e = d.volume().find(path);
                if (!e) return std::nullopt;
                return d.volume().read(*e);
            },
            config);
    }
    void press(std::initializer_list<menu::Key> keys) {
        for (menu::Key k : keys) {
            rt->key(k);
            rt->settle();
        }
    }
    std::string page() const { return rt->current_page() ? rt->current_page()->name : std::string(); }
    std::string focus() const { return rt->focused() ? rt->focused()->name : std::string(); }
    bool clean() const {
        for (const std::string& line : rt->log())
            if (line.find("script error") != std::string::npos) {
                std::printf("  %s\n", line.c_str());
                return false;
            }
        return true;
    }
    // From power on to the title page, with a memory card that has no saved game: the game
    // offers to make one (no), says what cannot be saved (continue), plays its opening movie
    // (start skips it) and ends the boot project asking for arcade.
    bool boot_to_title() {
        rt->boot();
        rt->start_project(rt->first_project());
        rt->settle();
        if (page() != "ConfirmDialog") return false;
        press({menu::Key::Left, menu::Key::Accept}); // "create game data?": no
        press({menu::Key::Right, menu::Key::Accept}); // "you cannot play GT mode": continue
        if (page() != "MovieRoot") return false;
        press({menu::Key::Start});
        if (!rt->finished() || rt->wanted_project() != "arcade") return false;
        if (!rt->advance()) return false;
        rt->settle();
        return page() == "TopRoot";
    }
};

Disc* gt4_disc() {
    for (const auto& disc : test::discs())
        if (disc->info().game == Game::GT4) return disc.get();
    return nullptr;
}

} // namespace

// Power on to the title page.
TEST(disc_menu_boot_to_title) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    f.rt->boot();
    CHECK(f.rt->first_project() == "boot" && f.rt->common_project());
    // The main script and the configuration ran: the world the projects expect.
    script::Vm& vm = f.rt->vm();
    CHECK(vm.lookup("game").kind == script::Kind::Object && vm.lookup("manager").kind == script::Kind::Object);
    CHECK(vm.lookup("UsingLanguages").items() && vm.lookup("UsingLanguages").items()->size() == 1 && vm.lookup("DemoList").items());
    f.rt->start_project("boot");
    f.rt->settle();
    // The load page looked for a saved game, found none and went on to the check page, which
    // asks whether to make one.
    CHECK(f.page() == "ConfirmDialog" && f.rt->open_pages().size() == 2 && f.rt->open_pages()[0]->name == "CheckRoot");
    CHECK(f.focus() == "Yes" && f.rt->idle());
    const std::shared_ptr<menu::LiveWidget> message = f.rt->current_page()->child("Dialog")->child("Message")->child("message");
    CHECK(message && message->data.text("text").find("create game data") != std::string::npos);
    // What boot made available to arcade: its starting cars and courses.
    CHECK(f.rt->game().available_cars.size() > 100 && f.rt->game().available_courses.count("tsukuba") && f.rt->game().available_courses.count("nurburgring"));
    CHECK(!f.rt->game().available_courses.count("sarthe")); // not from the start
    f.press({menu::Key::Left});
    CHECK(f.focus() == "No");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "ConfirmDialog" && f.focus() == "No"); // "you cannot play GT mode": retry is offered first
    f.press({menu::Key::Right, menu::Key::Accept});
    CHECK(f.page() == "MovieRoot" && f.rt->open_pages().size() == 1 && !f.rt->cursor_visible());
    f.press({menu::Key::Start});
    CHECK(f.rt->finished() && f.rt->wanted_project() == "arcade" && !f.rt->race_request());
    CHECK(f.rt->advance());
    f.rt->settle();
    CHECK(f.page() == "TopRoot" && f.rt->project()->name() == "arcade" && f.rt->game().last_menu_project == "boot");
    CHECK(f.focus() == "Arcade" && f.clean());
    // The title's menu: the script built it (no ending to watch, no arcade machine).
    const menu::LiveWidget* box = f.rt->focused()->parent_widget();
    CHECK(box && box->is("SelectBox") && box->children.size() == 6 && box->children[1]->name == "GTmode" && box->children[5]->name == "Credit");
}

// With no memory card at all the game says so and goes on.
TEST(disc_menu_boot_without_card) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    f.rt->game().card.connected = false;
    f.rt->boot();
    f.rt->start_project("boot");
    f.rt->settle();
    CHECK(f.page() == "ConfirmDialog");
    const std::shared_ptr<menu::LiveWidget> message = f.rt->current_page()->child("Dialog")->child("Message")->child("message");
    CHECK(message && message->data.text("text").find("memory card") != std::string::npos);
    f.press({menu::Key::Accept});                  // continue without a card
    f.press({menu::Key::Right, menu::Key::Accept}); // "you cannot play GT mode": continue
    CHECK(f.page() == "MovieRoot");
    // Accept goes to the focused widget and the movie page has none; cancel goes to the page.
    f.press({menu::Key::Accept});
    CHECK(f.page() == "MovieRoot" && !f.rt->finished());
    f.press({menu::Key::Cancel});
    CHECK(f.rt->finished() && f.rt->wanted_project() == "arcade" && f.clean());
}

// Arcade: single race, a course, a car, its paint, the driving options, and the race is asked for.
TEST(disc_menu_arcade_to_race) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.boot_to_title());
    if (f.page() != "TopRoot") return;
    f.press({menu::Key::Accept});
    CHECK(f.page() == "GameRoot" && f.focus() == "SingleRace" && f.rt->open_pages().size() == 2);
    f.press({menu::Key::Accept});
    CHECK(f.page() == "CategoryRoot" && f.focus() == "Real");
    // The categories count the courses arcade has: "19/19" would be all of them.
    const std::string count = f.rt->focused()->child("info") ? f.rt->focused()->child("info")->data.text("text") : std::string();
    CHECK(count.find('/') != std::string::npos && count[0] != '0');
    f.press({menu::Key::Accept});
    CHECK(f.page() == "CourseRoot" && f.rt->focused() && f.rt->focused()->parent_widget()->is("SelectBox"));
    const int courses = int(f.rt->focused()->parent_widget()->children.size());
    CHECK(courses >= 5);
    // The second course of the list.
    f.press({menu::Key::Right});
    const int course_index = f.rt->focused()->parent_widget()->index();
    f.press({menu::Key::Accept});
    if (f.page() == "CourseRoot") f.press({menu::Key::Accept}); // the direction (forward) when the course has both
    CHECK(f.page() == "MakerRoot");
    // Down from the tabs into the makers, and the first maker's cars.
    f.press({menu::Key::Accept});
    CHECK(f.page() == "MakerRoot" && f.rt->focused() && f.rt->focused()->name != "Maker");
    const std::string maker = f.focus();
    f.press({menu::Key::Accept});
    CHECK(f.page() == "CarRoot" && f.rt->focused() && f.rt->focused()->is("ListBox"));
    const std::shared_ptr<menu::LiveWidget> car_page = f.rt->open_pages().back();
    const std::shared_ptr<menu::LiveWidget> name = car_page->child("CarScroll")->child("Name");
    const std::string first_car = name->data.text("text");
    CHECK(!first_car.empty());
    f.press({menu::Key::Right});
    CHECK(name->data.text("text") != first_car); // the next car of the maker
    const std::string car_code = car_page->child("CarFace")->data.text("car_code");
    CHECK(!car_code.empty() && f.rt->game().available_cars.count(car_code));
    // Its info line: the drivetrain, the power and the torque from the car database.
    const std::string info = car_page->child("ToolTip")->child("tip")->data.text("value");
    CHECK(info.find("Power:") != std::string::npos && info.find("rpm") != std::string::npos && info.find("--") == std::string::npos);
    f.press({menu::Key::Accept});
    // The paints: a chip each, the paint's name under them.
    CHECK(f.rt->focused() && f.rt->focused()->parent_widget()->is("SelectBox") && f.focus() == "ColItem");
    const size_t paints = f.rt->focused()->parent_widget()->children.size();
    CHECK(paints >= 1 && !car_page->child("ColScroll")->child("Name")->data.text("text").empty());
    if (paints > 1) f.press({menu::Key::Right});
    f.press({menu::Key::Accept});
    CHECK(f.page() == "SettingPopup" && f.focus() == "OK");
    f.press({menu::Key::Accept});
    CHECK(f.rt->finished());
    const std::optional<menu::RaceRequest> race = f.rt->race_request();
    CHECK(race.has_value());
    if (race) {
        CHECK(race->sequence == "single_race" && race->car == car_code && race->paint == (paints > 1 ? 1 : 0));
        CHECK(!race->course.empty() && f.rt->game().available_courses.count(race->course) && race->max_entry >= 2);
        CHECK(race->difficulty == "NORMAL" && race->manual == 0 && race->assist_tcs == 1);
        std::printf("  race asked for: %s, %s (%s, paint %d) at %s (course %d of %d), %d cars, race \"%s\"\n", race->sequence.c_str(), race->car.c_str(),
                    maker.c_str(), race->paint, race->course.c_str(), course_index, courses, race->max_entry, race->race.c_str());
    }
    CHECK(!f.rt->advance() && f.clean());
}

// The options: from the title into the option project, a setting changed in its list and kept
// (under the data folder), and back to the title.
TEST(disc_menu_options_and_back) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const std::string dir = (std::filesystem::temp_directory_path() / "gt4r-menu-test-options").string();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    {
        Flow f(*disc, "US", dir);
        CHECK(f.boot_to_title());
        if (f.page() != "TopRoot") return;
        for (int i = 0; i < 6 && f.focus() != "Option"; ++i) f.press({menu::Key::Down});
        CHECK(f.focus() == "Option");
        f.press({menu::Key::Accept});
        CHECK(f.rt->finished() && f.rt->wanted_project() == "option" && f.rt->advance());
        f.rt->settle();
        CHECK(f.page() == "OptionRoot" && f.rt->project()->name() == "option" && f.rt->focused());
        // The first item: the race settings.
        f.press({menu::Key::Accept});
        CHECK(f.page() == "RaceRoot" && f.rt->focused() && f.rt->focused()->is("OptionMenu"));
        const menu::LiveWidget* transmission = f.rt->focused();
        CHECK(f.rt->game().option_int("automatic_gear") == 1 && transmission->index() == 0);
        // Open the list, take the second row (manual gears), and confirm with OK.
        f.press({menu::Key::Accept});
        CHECK(transmission->data.number("is_popuped") != 0 && f.rt->focused()->parent_widget()->name == "vbox");
        f.press({menu::Key::Down, menu::Key::Accept});
        CHECK(transmission->data.number("is_popuped") == 0 && transmission->index() == 1 && f.rt->focused() == transmission);
        for (int i = 0; i < 8 && f.focus() != "OK"; ++i) f.press({menu::Key::Down});
        if (f.focus() != "OK") f.press({menu::Key::Right});
        CHECK(f.focus() == "OK");
        f.press({menu::Key::Accept});
        CHECK(f.page() == "OptionRoot" && f.rt->game().option_int("automatic_gear") == 0);
        // Leaving the options: "Exit?", yes, and the title is back.
        f.press({menu::Key::Cancel});
        CHECK(f.page() == "ConfirmDialog" && f.focus() == "Yes");
        f.press({menu::Key::Accept});
        CHECK(f.rt->finished() && f.rt->wanted_project() == "arcade" && f.rt->advance());
        f.rt->settle();
        CHECK(f.page() == "TopRoot" && f.focus() == "Option" && f.clean()); // the title remembers where the player was
    }
    // A new run finds the setting as it was left.
    {
        Flow f(*disc, "US", dir);
        CHECK(f.rt->game().option_int("automatic_gear", 1) == 0);
        f.rt->boot();
        CHECK(f.rt->vm().attribute(f.rt->vm().attribute(f.rt->vm().lookup("game"), "option"), "automatic_gear").i == 0);
    }
    std::filesystem::remove_all(dir);
}

// A new game: at the title, GT mode offers to make the game's data; the player's name is
// typed on the name page; the game explains its saving; then GT mode opens on its map.
TEST(disc_menu_new_game_to_gt_mode) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.boot_to_title());
    if (f.page() != "TopRoot") return;
    f.press({menu::Key::Down});
    CHECK(f.focus() == "GTmode");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "ConfirmDialog"); // no game data: make it now?
    f.press({menu::Key::Accept});
    CHECK(f.rt->finished() && f.rt->wanted_project() == "boot" && f.rt->game().next_menu_arg == "EntryRoot" && f.rt->advance());
    f.rt->settle();
    CHECK(f.page() == "EntryRoot" && f.focus() == "key_enter");
    // Two letters, then enter.
    f.press({menu::Key::Left, menu::Key::Accept, menu::Key::Left, menu::Key::Accept});
    const std::string typed = f.rt->current_page()->child("Keyboard")->child("input")->data.text("value");
    CHECK(typed.size() == 2 && typed[0] >= 'A' && typed[0] <= 'Z' && typed[1] >= 'a' && typed[1] <= 'z'); // the keys turn to small letters after the first
    f.press({menu::Key::Right, menu::Key::Right});
    for (int i = 0; i < 6 && f.focus() != "key_enter"; ++i) f.press({menu::Key::Right});
    CHECK(f.focus() == "key_enter");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "ConfirmDialog"); // "is this your name?"
    // Yes to every question, OK to every notice, until the boot project ends.
    for (int i = 0; i < 24 && !f.rt->finished(); ++i) f.press({menu::Key::Accept});
    CHECK(f.rt->finished() && f.rt->game().username == typed && f.rt->game().valid_game_data && f.rt->game().card.has_game_data);
    CHECK(f.rt->advance());
    f.rt->settle();
    CHECK(f.page() == "TopRoot" && f.rt->project()->name() == "arcade");
    // Now GT mode opens.
    for (int i = 0; i < 6 && f.focus() != "GTmode"; ++i) f.press({menu::Key::Down});
    f.press({menu::Key::Accept});
    CHECK(f.rt->finished() && f.rt->wanted_project() == "gtmode" && f.rt->advance());
    f.rt->settle();
    CHECK(f.page() == "TopRoot" && f.rt->project()->name() == "gtmode" && f.focus() == "Home");
    // The map's header: the player's credits.
    const std::shared_ptr<menu::LiveWidget> top = f.rt->open_pages().back();
    const std::shared_ptr<menu::LiveWidget> money = top->child("Common") ? top->child("Common")->child("Header")->child("Money")->child("label") : nullptr;
    CHECK(money && money->data.text("text") == "10000");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "HomeRoot" && f.focus() == "Garage");
    // The home page's numbers: nothing completed, written with one decimal.
    const std::shared_ptr<menu::LiveWidget> home = f.rt->current_page()->self();
    CHECK(home->child("Status")->child("Achievement")->child("value")->data.text("text") == "0.0%%");
    // The garage of a new game: no car, and the page says so.
    f.press({menu::Key::Accept});
    CHECK(f.page() == "GarageRoot" && f.clean());
    f.press({menu::Key::Cancel});
    CHECK(f.page() == "HomeRoot");
    f.press({menu::Key::Cancel});
    CHECK(f.page() == "TopRoot");
}

// Prologue's main script, configuration and projects load, and a project shows a page.
TEST(disc_menu_prologue_loads) {
    Disc* prologue = nullptr;
    for (const auto& disc : test::discs())
        if (disc->info().game != Game::GT4) prologue = disc.get();
    if (!prologue) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    Flow f(*prologue, "JP");
    f.rt->boot();
    CHECK(f.rt->first_project() == "memcard" && f.rt->vm().lookup("TopProject").as_string() == "prologue_opening");
    CHECK(f.rt->vm().lookup("prologue").kind == script::Kind::Module);
    for (const char* project : {"prologue_opening", "prologue", "memcard", "option"}) {
        f.rt->start_project(project);
        f.rt->settle();
        CHECK(f.rt->current_page() != nullptr);
    }
}

// The focus search: in line first, the nearest; around the page's end with wraparound.
TEST(menu_focus_moves_as_the_search_orders) {
    const char* layout = R"(Project {
	name string {"NavProject"}
	children [1] {
		RootWindow {
			name string {"Top"}
			children [5] {
				Composite { name string {"A"} can_focus digit {1} geometry rectangle {100 100 80 30} }
				Composite { name string {"B"} can_focus digit {1} geometry rectangle {300 100 80 30} }
				Composite { name string {"C"} can_focus digit {1} geometry rectangle {500 110 80 30} }
				Composite { name string {"D"} can_focus digit {1} geometry rectangle {120 300 80 30} }
				Composite { name string {"E"} can_focus digit {1} geometry rectangle {400 300 80 30} }
			}
		}
	}
})";
    menu::Runtime rt(no_files());
    const std::shared_ptr<menu::LiveWidget> top = rt.start_layout(menu::parse_layout(std::string_view(layout)))->child("Top");
    rt.open_page(top, true);
    auto go = [&](menu::Key k) {
        rt.key(k);
        rt.settle();
        return rt.focused() ? rt.focused()->name : std::string();
    };
    CHECK(rt.focused()->name == "A");
    CHECK(go(menu::Key::Right) == "B" && go(menu::Key::Right) == "C"); // C overlaps B's height: in line
    CHECK(go(menu::Key::Right) == "A");                                // around the end
    CHECK(go(menu::Key::Left) == "C" && go(menu::Key::Left) == "B");
    CHECK(go(menu::Key::Down) == "B"); // E is below but to the side: not in line, and the page's clip angle is 0
    CHECK(go(menu::Key::Left) == "A" && go(menu::Key::Down) == "D" && go(menu::Key::Right) == "E" && go(menu::Key::Up) == "E");
    // With a clip angle the page lets the focus go to the side.
    top->set_property("navigate_clip_angle", Value::integer(80));
    CHECK(go(menu::Key::Up) == "B" || go(menu::Key::Up) == "C");
    // Without wraparound the end is the end.
    top->set_property("navigate_wraparound", Value::integer(0));
    rt.set_focus(*top, top->child("C"));
    CHECK(go(menu::Key::Right) == "C");
}
