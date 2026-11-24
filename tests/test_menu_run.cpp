// The menus running: the event thread, the widgets' own behavior, the actors and the game's
// state on layouts made up here; then the game's own projects from the discs of GT4R_DISC:
// boot to the title, arcade to its race request, the options and back, the GT mode entry.

#include "test_framework.h"
#include "menu_run_flow.h"

#include "gt4r/menu/program_tables.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/menu/runtime_internal.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>

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
    CHECK(rt.focused() == top->child("Right").get() && seen.size() == 1); // the focus enter event comes the picture after
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
    // MBlinkActor sets a face's own alpha: a composite has none and stays as it is (its blink on
    // a face: test_menu_draw.cpp).
    const Value blink = call(vm, menu_module, "MBlinkActor", {menu::widget_value(right)});
    assign(vm, menu::widget_value(right), "actor", blink);
    assign(vm, blink, "period", Value::number(0.1f));
    for (int i = 0; i < 5; ++i) rt.update();
    CHECK(right->opacity() == 1 && !right->data.has("alpha"));
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
    CHECK(h.option_text("game_zone") == "US"); // the zone by its name (0 UNDEF, 1 JP, 2 US, ...)

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
    // Credits (the configuration gives a new game 10,000) as the money text, and the garage.
    CHECK(vm.attribute(game, "money").as_string() == "--"); // nothing yet
    assign(vm, game, "money", Value::integer(10000));
    CHECK(vm.attribute(game, "money").as_string() == "10,000" && call(vm, game, "isEnoughMoney", {Value::integer(10001)}).i == 0);
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
        // The entry table the scripts read and wrote: entry 0 is the player's car, never "nil".
        CHECK(std::find(race->entries.begin(), race->entries.end(), "nil") == race->entries.end());
        CHECK(race->entries.empty() || race->entries[0] == race->car);
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
    // The map is larger than the screen: it lies so the focused place is in the middle of the
    // view, and follows the focus.
    {
        const std::shared_ptr<menu::LiveWidget> window = f.rt->current_page()->child("ScrollWindow");
        const std::shared_ptr<menu::LiveWidget> clip = window ? window->child("clip") : nullptr;
        const std::shared_ptr<menu::LiveWidget> space = clip ? clip->child("space") : nullptr;
        const std::shared_ptr<menu::LiveWidget> home = space ? space->child("Home") : nullptr;
        CHECK(home != nullptr);
        if (home) {
            const menu::Rect view = *clip->data.rect("geometry"), whole = *space->data.rect("geometry"), at = *home->data.rect("geometry");
            const float x = std::clamp((view.w - at.w) * 0.5f - at.x, view.w - whole.w, 0.0f);
            const float y = std::clamp((view.h - at.h) * 0.5f - at.y, view.h - whole.h, 0.0f);
            CHECK(std::fabs(whole.x - x) < 0.5f && std::fabs(whole.y - y) < 0.5f && whole.w > view.w);
            f.press({menu::Key::Up});
            CHECK(f.focus() != "Home");
            for (int i = 0; i < 120; ++i) f.rt->update();
            const menu::Rect moved = *space->data.rect("geometry");
            CHECK(moved.x != whole.x || moved.y != whole.y);
            f.rt->set_focus(*f.rt->current_page(), home);
        }
    }
    // The map's header: the player's credits.
    const std::shared_ptr<menu::LiveWidget> top = f.rt->open_pages().back();
    const std::shared_ptr<menu::LiveWidget> money = top->child("Common") ? top->child("Common")->child("Header")->child("Money")->child("label") : nullptr;
    if (money) std::printf("  credits: %s\n", money->data.text("text").c_str());
    CHECK(money && money->data.text("text") == "10,000");
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

// Gran Turismo mode with a game and credits: the map, a country's dealers, a maker's showroom,
// a car and its paint, "buy", and the car is the garage's and the one driven.
TEST(disc_menu_buy_a_car) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    menu::GameState& game = f.rt->game();
    game.card.has_game_data = true; // a stand-in for a saved game
    game.username = "Player", game.money = 500000;
    f.rt->boot();
    f.rt->start_project(f.rt->first_project());
    f.rt->settle();
    CHECK(f.page() == "MovieRoot"); // the game loads and the opening plays (opening_after_autoload)
    f.press({menu::Key::Start});
    CHECK(f.rt->finished() && f.rt->advance()); // then the title
    f.rt->settle();
    CHECK(f.page() == "TopRoot");
    f.press({menu::Key::Down, menu::Key::Accept});
    CHECK(f.rt->finished() && f.rt->wanted_project() == "gtmode" && f.rt->advance());
    f.rt->settle();
    CHECK(f.page() == "TopRoot" && f.focus() == "Home");
    f.press({menu::Key::Up});
    CHECK(f.focus() == "DealerItaly");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "NationRoot" && f.focus() == "alfaromeo");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "DealerRoot" && f.focus() == "New");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "BuyCarRoot" && f.focus() == "CarItem");
    f.press({menu::Key::Accept}); // the car
    CHECK(f.focus() == "ColItem");
    f.press({menu::Key::Accept}); // its paint
    CHECK(f.focus() == "Buy");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "BuyCarDialog" && f.focus() == "Cancel");
    f.press({menu::Key::Right});
    CHECK(f.focus() == "Buy");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "GetCarRoot" && f.focus() == "Yes"); // "ride it now?"
    CHECK(game.garage.size() == 1);
    f.press({menu::Key::Accept});
    CHECK(f.page() == "GetCarRoot" && f.focus() == "OK" && f.clean());
    if (game.garage.size() == 1) {
        // The price is the car database's; the car is the one driven.
        const script::Value label[1] = {script::Value::string(game.garage[0].car)};
        const script::Value price = f.rt->vm().call(f.rt->vm().attribute(f.rt->vm().attribute(f.rt->vm().lookup("menu"), "MCarData"), "GetPrice"), label);
        std::printf("  bought %s for %d credits\n", game.garage[0].car.c_str(), price.i);
        CHECK(price.i > 0 && game.money == 500000 - price.i && game.riding_car == 0);
        CHECK(game.card.has_game_data && !game.need_save);
    }
    f.press({menu::Key::Accept});
    CHECK(f.page() == "BuyCarRoot");
    f.press({menu::Key::Cancel});
    CHECK(f.page() == "DealerRoot");
    f.press({menu::Key::Cancel});
    CHECK(f.page() == "NationRoot");
    f.press({menu::Key::Cancel});
    CHECK(f.page() == "TopRoot" && f.focus() == "DealerItaly");
    // Home, and the garage lists the car.
    const std::shared_ptr<menu::LiveWidget> space = f.rt->current_page()->child("ScrollWindow")->child("clip")->child("space");
    f.rt->set_focus(*f.rt->current_page(), space->child("Home"));
    f.press({menu::Key::Accept});
    CHECK(f.page() == "HomeRoot");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "GarageRoot" && f.clean());
    const std::shared_ptr<menu::LiveWidget> cars = f.rt->current_page()->child("Info") ? f.rt->current_page()->child("Info")->child("cars") : nullptr;
    CHECK(cars && cars->data.text("text").find("1") != std::string::npos);
    if (cars) std::printf("  the garage: %s\n", cars->data.text("text").c_str());
}

// A new game is saved on the memory card's folder under the data folder as the game's own
// files; the next start finds it, loads it and goes to the title with it.
TEST(disc_menu_game_saved_and_loaded) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const std::string dir = (std::filesystem::temp_directory_path() / "gt4r-menu-test-save").string();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string name;
    {
        Flow f(*disc, "US", dir);
        f.rt->boot();
        f.rt->start_project(f.rt->first_project());
        f.rt->settle();
        CHECK(f.page() == "ConfirmDialog"); // no game on the card
        f.press({menu::Key::Left, menu::Key::Accept});
        f.press({menu::Key::Right, menu::Key::Accept});
        f.press({menu::Key::Start});
        CHECK(f.rt->advance());
        f.rt->settle();
        name = f.new_game();
        CHECK(!name.empty() && f.rt->project()->name() == "gtmode" && f.clean());
        const std::string folder = dir + "/memorycard/BASCUS-97328GAMEDATA/";
        CHECK(std::filesystem::exists(folder + "BASCUS-97328GAMEDATA") && std::filesystem::file_size(folder + "BASCUS-97328GAMEDATA") == 0x3A0C0);
        CHECK(std::filesystem::exists(folder + "garage") && std::filesystem::exists(folder + "icon.sys") && std::filesystem::exists(folder + "icon0.ico"));
    }
    {
        Flow f(*disc, "US", dir);
        f.rt->boot();
        f.rt->start_project(f.rt->first_project());
        f.rt->settle();
        // The boot project loads the game and goes on to the title.
        for (int i = 0; i < 6 && !f.rt->finished(); ++i) f.press({f.page() == "MovieRoot" ? menu::Key::Start : menu::Key::Accept});
        CHECK(f.rt->finished() && f.rt->game().username == name && f.rt->game().money == 10000 && f.rt->game().card.has_game_data);
        std::printf("  loaded the game of %s\n", f.rt->game().username.c_str());
        CHECK(f.clean());
    }
    if (!std::getenv("GT4R_KEEP_TEST_FILES")) std::filesystem::remove_all(dir);
}

// Gran Turismo mode's places with a car: the map's directions, a used car lot, an event hall to
// an event's page and the race asked for, a license test and a mission with their cars, GT
// Auto, the garage, the status and the diary.
TEST(disc_menu_gt_mode_places) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    if (f.page() != "TopRoot") return;
    const menu::GameState& g = f.rt->game();
    const std::string car = g.garage[0].car;
    // The map moves by the program's search: from Home right, left, up, down.
    for (const auto& [key, to] : {std::pair<menu::Key, const char*>{menu::Key::Right, "ExtremeHall"}, {menu::Key::Left, "GTAuto"}, {menu::Key::Up, "DealerItaly"},
                                  {menu::Key::Down, "License"}}) {
        f.map_focus("Home");
        f.press({key});
        CHECK(f.focus() == to);
    }
    // The used car lot of the week: cars with their used prices.
    f.map_focus("UsedCarAsia");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "UsedCarRoot" && f.focus() == "Item");
    const std::string price = f.text_of("Price");
    CHECK(!f.text_of("Name").empty() && price.rfind("Cr.", 0) == 0 && price.size() > 4);
    std::printf("  used car: %s, %s\n", f.text_of("Name").c_str(), price.c_str());
    // Bought: its used price paid, driven from now on, with its odometer; a day of the diary.
    {
        const s64 money = g.money;
        const u32 day = g.today;
        f.press({menu::Key::Accept, menu::Key::Accept, menu::Key::Right, menu::Key::Accept, menu::Key::Accept, menu::Key::Accept});
        CHECK(f.page() == "UsedCarRoot" && g.garage_count() == 2 && g.riding_car == 1 && g.today == day + 1);
        std::string digits;
        for (char c : price)
            if (c >= '0' && c <= '9') digits += c;
        CHECK(g.money == money - std::atoll(digits.c_str()) && g.garage[1].record && g.garage[1].record->odometer() > 0);
        CHECK(g.diary.back().type == 1 && g.diary.back().b2 == 2); // got a car, used
    }
    f.press({menu::Key::Cancel});
    CHECK(f.page() == "TopRoot");
    f.map_focus("DealerItaly"); // the car bought new is driven again below
    f.rt->game().riding_car = 0;
    // The beginners' hall: the Sunday Cup's page has its prizes; OK, the race, the mode, and the
    // race is asked for with the car driven at the event's course.
    f.map_focus("BeginnerHall");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "EventHallRoot");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "EventInfoDialog" && f.focus() == "OK" && f.text_of("prize") == "600" && f.text_of("label") == "Sunday Cup");
    f.press({menu::Key::Accept, menu::Key::Accept, menu::Key::Accept});
    std::optional<menu::RaceRequest> race = f.rt->race_request();
    CHECK(race && race->sequence == "solo_event" && race->race == "am_sunday_0001" && race->car == car && race->garage_slot == 0 && !race->course.empty());
    CHECK(f.clean());
    // A license test: its own car and course.
    {
        Flow l(*disc);
        CHECK(l.gt_mode_with_a_car());
        l.map_focus("License");
        l.press({menu::Key::Accept});
        CHECK(l.page() == "LicenseRoot" && l.focus() == "l0b");
        l.press({menu::Key::Accept, menu::Key::Accept});
        race = l.rt->race_request();
        CHECK(race && race->sequence == "license" && race->race == "l0b0001" && race->car == "demio_glx_99" && race->course == "testcourse_100m");
        CHECK(l.clean());
    }
    // A mission.
    {
        Flow m(*disc);
        CHECK(m.gt_mode_with_a_car());
        m.map_focus("Mission");
        m.press({menu::Key::Accept});
        CHECK(m.page() == "MissionRoot");
        m.press({menu::Key::Accept});
        race = m.rt->race_request();
        CHECK(race && race->sequence == "mission" && !race->car.empty() && race->car != car && !race->course.empty());
        CHECK(m.clean());
    }
    // GT Auto: the car wash washes the car driven.
    {
        Flow a(*disc);
        CHECK(a.gt_mode_with_a_car());
        a.rt->game().garage[0].record->set_condition(3, 0.5f);
        a.map_focus("GTAuto");
        a.press({menu::Key::Accept});
        CHECK(a.page() == "GTAutoRoot" && a.focus() == "Wash");
        a.press({menu::Key::Right});
        CHECK(a.focus() == "Oil");
        a.press({menu::Key::Left, menu::Key::Accept});
        CHECK(a.page() == "CarWashRoot");
        a.press({menu::Key::Accept});
        for (int i = 0; i < 600; ++i) a.rt->update();
        a.rt->settle();
        CHECK(a.rt->game().garage[0].record->condition(3) == 0 && a.rt->game().money < 500000 - 30590);
        // Wheels for road tires from the first maker: the car's chosen wheel, a day of the diary.
        for (int i = 0; i < 4 && a.page() != "GTAutoRoot"; ++i) a.press({menu::Key::Cancel});
        a.focus_on("Wheel");
        const s64 money = a.rt->game().money;
        a.press({menu::Key::Accept, menu::Key::Accept, menu::Key::Accept});
        CHECK(a.page() == "WheelShopRoot");
        a.press({menu::Key::Accept, menu::Key::Accept, menu::Key::Accept});
        CHECK(a.rt->game().money < money && a.rt->game().garage[0].record->wheel(0) != -1 && a.rt->game().diary.back().type == 6);
        CHECK(a.clean());
    }
    // A tune shop: the car's parts, a sports exhaust's page (power before and after, its
    // price), bought and fitted to the car driven.
    {
        Flow t(*disc);
        CHECK(t.gt_mode_with_a_car());
        t.map_focus("DealerItaly");
        t.press({menu::Key::Accept, menu::Key::Accept});
        CHECK(t.page() == "DealerRoot" && t.focus_on("Tuner"));
        t.press({menu::Key::Accept});
        CHECK(t.page() == "TuningRoot" && t.focus() == "Muffler");
        t.press({menu::Key::Accept, menu::Key::Accept});
        CHECK(t.page() == "TuningDialog" && t.text_of("price") == "Cr. 1,500");
        const int before = std::atoi(t.text_of("before").c_str()), after = std::atoi(t.text_of("after").c_str());
        CHECK(before > 0 && after > before);
        std::printf("  sports exhaust: %d -> %d %s\n", before, after, t.text_of("ps0").c_str());
        const s64 money = t.rt->game().money;
        t.press({menu::Key::Right, menu::Key::Accept, menu::Key::Accept, menu::Key::Accept});
        CHECK(t.rt->game().money == money - 1500);
        script::Vm& vm = t.rt->vm();
        const Value riding = vm.attribute(vm.attribute(vm.lookup("game"), "garage"), "riding_car");
        const Value muffler[1] = {Value::integer(20)};
        CHECK(vm.call(vm.attribute(riding, "getEquipedParts"), muffler).i == 1);
        const Value owned[2] = {Value::integer(20), Value::integer(1)};
        CHECK(vm.call(vm.attribute(riding, "isHavingParts"), owned).i == 1 && t.clean());
    }
    // Home: the status, the diary of the day the car was bought, the garage and the car's page.
    Flow h(*disc);
    CHECK(h.gt_mode_with_a_car());
    h.map_focus("Home");
    h.press({menu::Key::Accept});
    CHECK(h.page() == "HomeRoot");
    h.press({menu::Key::Right, menu::Key::Accept});
    CHECK(h.page() == "StatusRoot");
    h.press({menu::Key::Cancel});
    CHECK(h.page() == "HomeRoot");
    h.focus_on("Diary");
    h.press({menu::Key::Accept});
    CHECK(h.page() == "LogRoot" && h.text_of("record").find("purchased") != std::string::npos);
    h.press({menu::Key::Cancel});
    h.focus_on("Garage");
    h.press({menu::Key::Accept});
    CHECK(h.page() == "GarageRoot" && h.text_of("cars") == "1/1 cars");
    h.focus_on("Line0");
    h.press({menu::Key::Accept});
    CHECK(h.page() == "CarViewRoot" && h.focus() == "Change");
    // Sold: a quarter of its used price, the place left free, no car driven.
    const s64 money = h.rt->game().money;
    h.press({menu::Key::Left, menu::Key::Accept});
    CHECK(h.page() == "BuyCarDialog");
    h.press({menu::Key::Right, menu::Key::Accept}); // "sell"
    CHECK(h.page() == "ConfirmDialog");
    h.press({menu::Key::Accept}); // yes
    for (int i = 0; i < 4 && h.page() == "ConfirmDialog"; ++i) h.press({menu::Key::Accept});
    CHECK(h.rt->game().garage_count() == 0 && h.rt->game().riding_car == -1 && h.rt->game().money > money);
    std::printf("  sold for %lld credits\n", static_cast<long long>(h.rt->game().money - money));
    CHECK(h.clean());
}

// What a finished race gives back (made-up results): an event's prize and place, the race's
// record, a day of the diary; a license test's result and ranking; then GT mode again.
TEST(disc_menu_race_results) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    if (f.page() != "TopRoot") return;
    menu::GameState& g = f.rt->game();
    script::Vm& vm = f.rt->vm();
    const u32 today = g.today;
    const s64 prizes = g.total_prize_money;
    menu::RaceResult won;
    won.race = "am_sunday_0001", won.place = 0, won.time_ms = 185'432, won.distance_m = 8'000, won.garage_slot = g.riding_car, won.score = 30;
    const float engine = g.garage[0].record->condition(1), body = g.garage[0].record->condition(4);
    CHECK(f.rt->race_finished(won) == 600);
    // The distance on the engine and the body as the race's step adds it.
    CHECK(g.garage[0].record->condition(1) == engine + 8'000.0f && g.garage[0].record->condition(4) == body + 8'000.0f);
    CHECK(g.total_prize_money == prizes + 600 && g.rank_counts[1] == 1 && g.today == today + 1 && g.diary.back().type == 3 && g.diary.back().word == 185'432);
    CHECK(g.garage[0].record->odometer() == 8'000);
    const Value record = vm.attribute(vm.lookup("game"), "race_record");
    const Value label[1] = {Value::string("am_sunday_0001")};
    const Value unit = vm.call(vm.attribute(record, "getUnit"), label);
    CHECK(vm.attribute(unit, "result").as_string() == "1" && vm.attribute(unit, "type").as_string() == "event" && vm.attribute(unit, "best_score").i == 30);
    // Second place in the same sequence pays but leaves the record (it follows the sequence's
    // best place); in a later sequence it is the last result, and the win stays the best.
    menu::RaceResult second = won;
    second.place = 1;
    CHECK(f.rt->race_finished(second) == 500 && vm.attribute(unit, "present_result").as_string() == "1");
    g.race_session = {};
    CHECK(f.rt->race_finished(second) == 500 && vm.attribute(unit, "result").as_string() == "1" && vm.attribute(unit, "present_result").as_string() == "2");
    // Not finished: a race counted as such, no prize.
    menu::RaceResult retired = won;
    retired.finished = false;
    CHECK(f.rt->race_finished(retired) == 0 && g.rank_counts[0] == 1 && g.diary.back().b1 == 15);
    // A license test passed with gold: its unit, its ranking, a day.
    menu::RaceResult test;
    test.kind = menu::RaceResult::Kind::License, test.race = "l0b0001", test.result = "gold", test.time_ms = 10'123;
    f.rt->race_finished(test);
    const Value tlabel[1] = {Value::string("l0b0001")};
    CHECK(vm.attribute(vm.call(vm.attribute(record, "getUnit"), tlabel), "result").as_string() == "gold");
    const Value ranking = vm.call(vm.attribute(vm.attribute(vm.lookup("game"), "license_record"), "getUnit"), tlabel);
    CHECK(vm.call(vm.attribute(ranking, "getRankNum"), {}).i == 1);
    const Value first[1] = {Value::integer(0)};
    CHECK(vm.call(vm.attribute(ranking, "getBestTime"), first).as_string() == "0'10.123" && g.diary.back().type == 4);
    // The diary's page of those days says what was run.
    f.map_focus("Home");
    f.press({menu::Key::Accept});
    f.focus_on("Diary");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "LogRoot" && f.clean());
}

// A car's settings: Start at home opens the setting project on the car driven; the sheets
// (Set A, B, C) are the record's three equipment records; a setting written goes to the sheet
// chosen, and is what the race's equipment reads.
TEST(disc_menu_car_settings) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    if (f.page() != "TopRoot") return;
    f.map_focus("Home");
    f.press({menu::Key::Accept, menu::Key::Start});
    CHECK(f.rt->finished() && f.rt->wanted_project() == "setting" && f.rt->advance());
    f.rt->settle();
    CHECK(f.page() == "SettingRoot" && f.focus() == "Set0");
    menu::CarRecord& rec = *f.rt->game().garage[0].record;
    CHECK(rec.active_sheet() == 0);
    f.press({menu::Key::Right, menu::Key::Accept});
    CHECK(f.focus() == "Set1" && rec.active_sheet() == 1);
    // The suspension's ranges and settings, through the car object the pages use.
    script::Vm& vm = f.rt->vm();
    const Value car = vm.attribute(vm.attribute(vm.lookup("game"), "garage"), "riding_car");
    const Value range = vm.call(vm.attribute(car, "getSuspensionRange"), {});
    CHECK(range.items() && range.items()->size() == 42);
    const s32 spring_min = (*range.items())[12].i, spring_max = (*range.items())[13].i;
    const Value at[1] = {Value::integer(14)};
    const s32 spring = vm.call(vm.attribute(car, "getSuspensionSetting"), at).i;
    std::printf("  front spring %d in [%d, %d]\n", spring, spring_min, spring_max);
    CHECK(spring_min <= spring && spring <= spring_max && spring_max > 0);
    const Value put[2] = {Value::integer(14), Value::integer(spring_max)};
    vm.call(vm.attribute(car, "setSuspensionSetting"), put);
    CHECK(rec.sheet(1)[0x142] == spring_max && rec.sheet(0)[0x142] == spring);
    // The race takes the sheet in use: the car's equipment record as the simulation has it.
    CHECK(std::memcmp(rec.active(), rec.sheet(1), menu::CarRecord::kSheet) == 0 && f.clean());
}

// The boxes' packing reproduces the rectangles the layouts were saved with: the program packs
// every picture, so a page packed at once is as its file has it (lines, free boxes, grids).
TEST(disc_menu_packing_keeps_the_layouts) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    for (const char* project : {"gtmode", "setting", "cursor", "quick-event"}) {
        const gt3r::Volume::Entry* e = disc->volume().find(std::string("projects/") + project + "/" + project + ".mproject");
        CHECK(e != nullptr);
        if (!e) continue;
        const menu::Widget layout = menu::parse_layout(disc->volume().read(*e));
        int widgets = 0, moved = 0;
        for (const menu::Widget& page : layout.children()) {
            const std::shared_ptr<menu::LiveWidget> live = menu::LiveWidget::build(page, nullptr);
            menu::pack_page(*live);
            std::function<void(const menu::LiveWidget&, const menu::Widget&)> compare = [&](const menu::LiveWidget& w, const menu::Widget& file) {
                ++widgets;
                const std::optional<menu::Rect> a = w.data.rect("geometry"), b = file.rect("geometry");
                if (a && b && (std::fabs(a->x - b->x) > 0.02f || std::fabs(a->y - b->y) > 0.02f || std::fabs(a->w - b->w) > 0.02f || std::fabs(a->h - b->h) > 0.02f)) {
                    if (++moved <= 3) std::printf("  %s %s: %.2f %.2f %.2f %.2f, the file %.2f %.2f %.2f %.2f\n", project, w.path().c_str(), a->x, a->y, a->w, a->h, b->x, b->y, b->w, b->h);
                }
                for (size_t i = 0; i < w.children.size() && i < file.children().size(); ++i) compare(*w.children[i], file.children()[i]);
            };
            compare(*live, page);
        }
        std::printf("  %s: %d widgets, %d moved\n", project, widgets, moved);
        CHECK(widgets > 50 && moved == 0);
    }
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

// The bars as the program's: a slider with a popup changes its index only while the popup is
// open, accept keeps, cancel puts the old index back and tells the script; a scale bar's value
// is a number moved by its step, its events queued; both draw the pinch after the value.
TEST(menu_bars_follow_the_program) {
    const char* layout = R"(Project {
	name string {"BarProject"}
	children [1] {
		RootWindow {
			name string {"Top"}
			children [3] {
				SliderBar {
					name string {"Slider"} has_script digit {1} popup digit {1} geometry rectangle {100 100 150 24}
					children [1] { Composite { name string {"popup"} geometry rectangle {0 0 128 24} children [1] { Composite { name string {"pinch"} geometry rectangle {0 -14 32 54} } } } }
				}
				ScaleBar {
					name string {"Scale"} has_script digit {1} geometry rectangle {100 200 150 24}
					children [1] { Composite { name string {"pinch"} geometry rectangle {0 0 30 24} } }
				}
				Composite { name string {"Next"} can_focus digit {1} geometry rectangle {300 100 50 24} }
			}
		}
	}
})";
    menu::Runtime rt(no_files());
    const std::shared_ptr<menu::LiveWidget> top = rt.start_layout(menu::parse_layout(std::string_view(layout)))->child("Top");
    script::Vm& vm = rt.vm();
    const std::shared_ptr<menu::LiveWidget> slider = top->child("Slider"), scale = top->child("Scale");
    std::vector<int> told;
    slider->define("onValueChanged", native([&](script::Vm&, script::Args a) {
        told.push_back(a.size() > 1 ? a[1].i : -99);
        return Value{};
    }));
    int scale_told = 0, steps_told = 0;
    scale->define("onValueChanged", native([&](script::Vm&, script::Args) { return Value::integer(++scale_told); }));
    scale->define("onStepChanged", native([&](script::Vm&, script::Args) { return Value::integer(++steps_told); }));
    rt.open_page(top, true);
    rt.set_focus(*top, slider);
    assign(vm, menu::widget_value(slider), "range", Value::array({Value::integer(1), Value::integer(5)}));
    assign(vm, menu::widget_value(slider), "index", Value::integer(3));
    CHECK(vm.attribute(menu::widget_value(slider), "max").i == 5 && slider->index() == 3 && told.empty());
    // Closed: right goes on to the page (the focus moves), the index stays.
    rt.key(menu::Key::Right);
    rt.settle();
    CHECK(rt.focused() == top->child("Next").get() && slider->index() == 3);
    rt.set_focus(*top, slider);
    // Open: right moves it and tells at once; cancel puts it back and tells again.
    rt.key(menu::Key::Accept);
    rt.settle();
    CHECK(vm.attribute(menu::widget_value(slider), "is_popuped").i == 1 && !rt.cursor_visible());
    rt.key(menu::Key::Right);
    rt.key(menu::Key::Right);
    rt.key(menu::Key::Right);
    rt.settle();
    CHECK(slider->index() == 5 && told.size() == 2 && told[1] == 5 && rt.focused() == slider.get()); // stopped at the end; the key swallowed
    rt.key(menu::Key::Cancel);
    rt.settle();
    CHECK(slider->index() == 3 && told.size() == 3 && told[2] == 3 && vm.attribute(menu::widget_value(slider), "is_popuped").i == 0 && rt.cursor_visible());
    // The pinch slides toward its place, in the bar while the popup is closed: (150 - 32) / 4 x (3 - 1).
    for (int i = 0; i < 60; ++i) rt.update();
    CHECK(std::fabs(slider->child("popup")->child("pinch")->data.rect("geometry")->x - 59) < 0.5f);
    // A scale bar without a popup: left and right move the value by the step.
    rt.set_focus(*top, scale);
    rt.settle();
    CHECK(steps_told == 1); // focus enter
    assign(vm, menu::widget_value(scale), "min", Value::number(-10));
    assign(vm, menu::widget_value(scale), "max", Value::number(10));
    assign(vm, menu::widget_value(scale), "step", Value::number(2.5f));
    assign(vm, menu::widget_value(scale), "value", Value::number(20));
    CHECK(vm.attribute(menu::widget_value(scale), "value").as_float() == 10); // clamped
    rt.key(menu::Key::Left);
    rt.settle();
    CHECK(vm.attribute(menu::widget_value(scale), "value").as_float() == 7.5f && vm.attribute(menu::widget_value(scale), "index").i == 8 && scale_told == 1 && steps_told == 3);
    for (int i = 0; i < 120; ++i) rt.update();
    CHECK(std::fabs(scale->child("pinch")->data.rect("geometry")->x - 105) < 0.5f);
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
