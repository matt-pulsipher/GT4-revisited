// The option menu as the program runs it (mOptionMenu, docs/formats/menu.md "OptionMenu"): its
// list's place when it opens, the fade, the keys over the rows, accept and cancel, a list taller
// than the page's band scrolling with the chosen row. Layouts made up here.

#include "test_framework.h"

#include "gt4r/menu/runtime.h"
#include "gt4r/menu/runtime_internal.h"

#include <cmath>
#include <functional>
#include <string>

using namespace gt4r;
using script::Value;

namespace {

menu::FileReader no_files() {
    return [](const std::string&) -> std::optional<Bytes> { return std::nullopt; };
}

std::string rows(int n) {
    std::string out;
    for (int i = 0; i < n; ++i)
        out += "Composite { name string {\"r" + std::to_string(i) + "\"} geometry rectangle {0 0 100 24} }\n";
    return out;
}

// A menu `name` at (x, y) with `n` rows of 24.
std::string option_menu(const char* name, int y, int n, bool repeat = false) {
    return std::string("OptionMenu { name string {\"") + name + "\"} has_script digit {1} can_focus digit {1}" + (repeat ? " repeat digit {1}" : "") +
           " geometry rectangle {200 " + std::to_string(y) +
           " 150 24}\n children [1] { Composite { name string {\"popup\"} geometry rectangle {0 0 150 24}\n"
           " children [2] { ColorFace { name string {\"shadow\"} geometry rectangle {2 2 150 24} }\n"
           " VBox { name string {\"vbox\"} geometry rectangle {0 0 150 24} children [" +
           std::to_string(n + 1) + "] { ColorFace { name string {\"bg\"} }\n" + rows(n) + "} } } } } }\n";
}

std::string layout() {
    return "Project { name string {\"TestProject\"} children [1] { RootWindow { name string {\"Top\"} children [3] {\n" + option_menu("Mid", 200, 5) +
           option_menu("High", 30, 5, true) + option_menu("Tall", 200, 25) + "} } } }";
}

float screen_y(const std::shared_ptr<menu::LiveWidget>& w) { return w->screen_rect().y; }

} // namespace

TEST(menu_option_menu_places_its_list) {
    menu::Runtime rt(no_files());
    const std::shared_ptr<menu::LiveWidget> top = rt.start_layout(menu::parse_layout(layout()))->child("Top");
    rt.open_page(top, true);
    const std::shared_ptr<menu::LiveWidget> mid = top->child("Mid");
    const std::shared_ptr<menu::LiveWidget> list = mid->child("popup")->child("vbox"), shadow = mid->child("popup")->child("shadow");
    int changed = -1, closed = 0;
    mid->define("onValueChanged", script::Vm::native("test", [&](script::Vm&, const Value&, script::Args a) {
                    changed = a.size() > 1 ? int(a[1].as_float()) : -2;
                    return Value{};
                }));
    mid->define("onPopupClosed", script::Vm::native("test", [&](script::Vm&, const Value&, script::Args) {
                    ++closed;
                    return Value{};
                }));
    menu::widget_set_index(rt, *mid, 2, false);
    rt.set_focus(*top, mid);
    rt.key(menu::Key::Accept);
    rt.settle();
    // The chosen row lies on the menu: the list 2 rows up; its shadow 4 right and 4 down.
    CHECK(mid->data.number("is_popuped") != 0 && rt.focused() == list->children[3].get());
    CHECK(screen_y(list) == 200 - 48 && list->data.rect("geometry")->h == 5 * 24);
    CHECK(shadow->data.rect("geometry")->y == list->data.rect("geometry")->y + 4 && shadow->data.rect("geometry")->x == 4 &&
          shadow->data.rect("geometry")->h == 120);
    // The chosen row is active (its dot); the popup fades in by a tenth a picture.
    CHECK((list->children[3]->state & menu::kActive) != 0);
    const float a0 = mid->child("popup")->opacity();
    rt.update();
    CHECK(mid->child("popup")->opacity() > a0 && mid->child("popup")->opacity() <= 1.0f);
    // Down skips an insensitive row; at the end it stays (no repeat).
    list->children[4]->state |= menu::kInsensitive;
    rt.key(menu::Key::Down);
    rt.settle();
    CHECK(mid->index() == 4 && rt.focused() == list->children[5].get());
    rt.key(menu::Key::Down);
    rt.settle();
    CHECK(mid->index() == 4);
    // Cancel puts the index back, and the script hears no change.
    rt.key(menu::Key::Cancel);
    rt.settle();
    CHECK(mid->index() == 2 && mid->data.number("is_popuped") == 0 && rt.focused() == mid.get() && changed == -1 && closed == 1);
    CHECK((list->children[3]->state & menu::kActive) == 0);
    // Accept tells the script even when the index is the same.
    rt.key(menu::Key::Accept);
    rt.key(menu::Key::Accept);
    rt.settle();
    CHECK(changed == 2 && closed == 2);

    // Near the page's top the list is held a twentieth of the page (24) below it; with
    // `repeat` the index wraps.
    const std::shared_ptr<menu::LiveWidget> high = top->child("High");
    menu::widget_set_index(rt, *high, 4, false);
    rt.set_focus(*top, high);
    rt.key(menu::Key::Accept);
    rt.settle();
    CHECK(screen_y(high->child("popup")->child("vbox")) == 24);
    rt.key(menu::Key::Down);
    rt.settle();
    CHECK(high->index() == 0);
    rt.key(menu::Key::Cancel);
    rt.settle();

    // A list taller than the band (25 rows of 24 > 432) stays and scrolls with the chosen row,
    // 0.4 of the way each picture.
    const std::shared_ptr<menu::LiveWidget> tall = top->child("Tall");
    const std::shared_ptr<menu::LiveWidget> tlist = tall->child("popup")->child("vbox");
    menu::widget_set_index(rt, *tall, 9, false);
    rt.set_focus(*top, tall);
    rt.key(menu::Key::Accept);
    rt.settle();
    CHECK(screen_y(tlist) == 200 - 9 * 24);
    for (int i = 0; i < 12; ++i) rt.key(menu::Key::Down);
    rt.settle();
    CHECK(tall->index() == 21);
    // Row 21 ends at 200 + 13 x 24 = 512 before the list moves: it scrolls so the row ends at 456.
    for (int i = 0; i < 120; ++i) rt.update();
    const float end = screen_y(tlist) + 22 * 24;
    CHECK(std::fabs(end - 456) < 0.5f);
}

// Prologue's page takes Start as Accept and makes its activate event on itself (0x230548): a page without a focus hears
// onActivate from either (its opening movie is skipped so); Gran Turismo 4's page takes neither without a focus.
TEST(menu_prologue_start_activates_the_page) {
    for (const bool prologue : {true, false}) {
        menu::Runtime rt([prologue](const std::string& path) -> std::optional<Bytes> {
            if (prologue && path == "config/config-prologue.adc") return Bytes{0};
            return std::nullopt;
        });
        const std::shared_ptr<menu::LiveWidget> top = rt.start_layout(menu::parse_layout(std::string_view(
                                                                          "Project { name string {\"P\"} children [1] { RootWindow { name string {\"MovieRoot\"} } } }")))
                                                          ->child("MovieRoot");
        int activated = 0;
        top->define("onActivate", script::Vm::native("test", [&](script::Vm&, const Value&, script::Args) {
                        ++activated;
                        return Value::integer(1);
                    }));
        rt.open_page(top, true);
        rt.key(menu::Key::Start);
        rt.settle();
        CHECK(activated == (prologue ? 1 : 0));
        rt.key(menu::Key::Accept);
        rt.settle();
        CHECK(activated == (prologue ? 2 : 0));
    }
}
