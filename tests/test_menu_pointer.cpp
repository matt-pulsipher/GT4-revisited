// The mouse and fingers on the game's pages (menu/pointer.h): a tap focuses and a second chooses, a hover focuses, a
// list's rows by tap, wheel and drag, an open option list, the right button as Cancel. Layouts made up here; the boxes
// are the widgets' own rectangles in drawing order (the app's come from its drawer).

#include "test_framework.h"

#include "gt4r/menu/native.h"
#include "gt4r/menu/pointer.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/menu/runtime_internal.h"

#include <string>

using namespace gt4r;
using script::Value;

namespace {

menu::FileReader no_files() {
    return [](const std::string&) -> std::optional<Bytes> { return std::nullopt; };
}

void add_boxes(const std::shared_ptr<menu::LiveWidget>& w, std::vector<menu::DrawnBox>& out) {
    if (!w->visible()) return;
    out.push_back({w.get(), w->screen_rect(), w->weak_from_this()});
    for (const auto& c : w->children) add_boxes(c, out);
}
std::vector<menu::DrawnBox> boxes_of(const std::shared_ptr<menu::LiveWidget>& page) {
    std::vector<menu::DrawnBox> out;
    add_boxes(page, out);
    return out;
}

// A counter of a widget's event function.
void count(const std::shared_ptr<menu::LiveWidget>& w, const char* event, int& n) {
    w->define(event, script::Vm::native("test", [&n](script::Vm&, const Value&, script::Args) {
                  ++n;
                  return Value::integer(1);
              }));
}

const char* kButtons = R"(Project {
	name string {"TestProject"}
	children [1] {
		RootWindow {
			name string {"Top"}
			children [3] {
				ColorFace { name string {"bg"} geometry rectangle {0 0 640 480} }
				Button { name string {"A"} geometry rectangle {100 100 100 30} children [1] { TextFace { name string {"label"} geometry rectangle {4 4 60 20} } } }
				Button { name string {"B"} geometry rectangle {100 200 100 30} }
			}
		}
	}
})";

} // namespace

TEST(menu_pointer_tap_focuses_then_chooses) {
    menu::Runtime rt(no_files());
    const std::shared_ptr<menu::LiveWidget> top = rt.start_layout(menu::parse_layout(std::string_view(kButtons)))->child("Top");
    rt.open_page(top, true);
    rt.settle();
    const std::shared_ptr<menu::LiveWidget> a = top->child("A"), b = top->child("B");
    int a_on = 0, b_on = 0, cancel = 0;
    count(a, "onActivate", a_on), count(b, "onActivate", b_on), count(top, "onCancel", cancel);
    CHECK(rt.focused() == a.get());
    // A tap on B: the focus goes there and nothing is chosen; a second tap chooses it.
    std::vector<menu::DrawnBox> boxes = boxes_of(top);
    CHECK(menu::pointer_hit(rt, boxes, 150, 215).widget == b);
    CHECK(menu::pointer_tap(rt, boxes, 150, 215));
    rt.settle();
    CHECK(rt.focused() == b.get() && b_on == 0);
    CHECK(menu::pointer_tap(rt, boxes, 150, 215));
    rt.settle();
    CHECK(b_on == 1 && a_on == 0);
    // A face inside a button is the button's; a hover focuses it; the background is nothing.
    CHECK(menu::pointer_hit(rt, boxes, 110, 110).widget == a);
    CHECK(menu::pointer_hover(rt, boxes, 110, 110));
    rt.settle();
    CHECK(rt.focused() == a.get() && a_on == 0);
    CHECK(!menu::pointer_tap(rt, boxes, 400, 400));
    rt.settle();
    CHECK(rt.focused() == a.get() && a_on == 0 && b_on == 1);
    // The right button: Cancel.
    menu::pointer_back(rt);
    rt.settle();
    CHECK(cancel == 1);
    // A widget no longer there is no target; an insensitive one is none either.
    b->set_state(menu::kInsensitive, true);
    CHECK(menu::pointer_hit(rt, boxes, 150, 215).kind == menu::PointerHit::Kind::None);
    b->set_state(menu::kInsensitive, false);
    // The scripts take no keys (the context's event mask 0): nothing.
    if (auto o = rt.context().as<menu::Object>()) o->set("event_mask", Value::integer(0));
    CHECK(!menu::pointer_tap(rt, boxes, 150, 215));
    rt.settle();
    CHECK(rt.focused() == a.get());
}

namespace {

const char* kList = R"(Project {
	name string {"TestProject"}
	children [1] {
		RootWindow {
			name string {"Top"}
			children [2] {
				ListBox { name string {"List"} can_focus digit {1} geometry rectangle {100 100 200 100} direction digit {1} }
				Composite { name string {"Row"} geometry rectangle {0 0 200 20} visible digit {0} }
			}
		}
	}
})";

Value call(script::Vm& vm, const Value& of, const char* method, std::vector<Value> args = {}) { return vm.call(vm.attribute(of, method), args); }
int focus_index(script::Vm& vm, const std::shared_ptr<menu::LiveWidget>& list) { return int(vm.attribute(menu::widget_value(list), "focus_index").as_float()); }

} // namespace

TEST(menu_pointer_list_rows_tap_wheel_drag) {
    menu::Runtime rt(no_files());
    const std::shared_ptr<menu::LiveWidget> top = rt.start_layout(menu::parse_layout(std::string_view(kList)))->child("Top");
    script::Vm& vm = rt.vm();
    rt.open_page(top, true);
    const std::shared_ptr<menu::LiveWidget> list = top->child("List");
    const Value lv = menu::widget_value(list);
    call(vm, lv, "setItemTemplate", {rt.context(), menu::widget_value(top->child("Row"))});
    call(vm, lv, "setItemCount", {Value::integer(20)});
    rt.set_focus(*top, list);
    rt.settle();
    rt.update();
    int chosen = 0;
    count(list, "onActivate", chosen);
    // Rows 0..4 shown, 20 high from y 100: a tap on the third row puts the focus there; a second chooses.
    std::vector<menu::DrawnBox> boxes = boxes_of(top);
    const menu::PointerHit h = menu::pointer_hit(rt, boxes, 150, 145);
    CHECK(h.kind == menu::PointerHit::Kind::ListRow && h.box == list && h.row == 2);
    CHECK(menu::pointer_tap(rt, boxes, 150, 145));
    rt.settle();
    CHECK(focus_index(vm, list) == 2 && chosen == 0);
    CHECK(menu::pointer_tap(rt, boxes, 150, 145));
    rt.settle();
    CHECK(chosen == 1);
    // An insensitive row is no target.
    call(vm, lv, "setItemInsensitive", {Value::integer(3), Value::integer(1)});
    CHECK(menu::pointer_hit(rt, boxes, 150, 165).kind == menu::PointerHit::Kind::None);
    call(vm, lv, "setItemInsensitive", {Value::integer(3), Value::integer(0)});
    // The wheel: a notch down is a row on, two up two back (as the list's keys).
    CHECK(menu::pointer_wheel(rt, boxes, 150, 145, -1));
    rt.settle();
    CHECK(focus_index(vm, list) == 3);
    CHECK(menu::pointer_wheel(rt, boxes, 150, 145, 2));
    rt.settle();
    CHECK(focus_index(vm, list) == 1);
    // A drag up by two rows and a half: two rows on; let go, the next drag starts afresh.
    menu::PointerDrag drag;
    CHECK(menu::pointer_drag(rt, boxes, drag, 150, 180, 150, 160, false));
    CHECK(menu::pointer_drag(rt, boxes, drag, 150, 180, 150, 130, true));
    rt.settle();
    CHECK(focus_index(vm, list) == 3 && !drag.started);
    // A drag that starts off the list moves nothing.
    CHECK(!menu::pointer_drag(rt, boxes, drag, 500, 400, 500, 300, true));
}

namespace {

std::string option_rows(int n) {
    std::string out;
    for (int i = 0; i < n; ++i) out += "Composite { name string {\"r" + std::to_string(i) + "\"} geometry rectangle {0 0 150 24} }\n";
    return out;
}

std::string option_layout() {
    return "Project { name string {\"TestProject\"} children [1] { RootWindow { name string {\"Top\"} children [2] {\n"
           "Button { name string {\"Other\"} geometry rectangle {400 100 100 30} }\n"
           "OptionMenu { name string {\"Menu\"} has_script digit {1} can_focus digit {1} geometry rectangle {200 200 150 24}\n"
           " children [1] { Composite { name string {\"popup\"} geometry rectangle {0 0 150 24}\n"
           " children [2] { ColorFace { name string {\"shadow\"} geometry rectangle {2 2 150 24} }\n"
           " VBox { name string {\"vbox\"} geometry rectangle {0 0 150 24} children [5] { ColorFace { name string {\"bg\"} }\n" +
           option_rows(4) + "} } } } } }\n} } } }";
}

} // namespace

TEST(menu_pointer_open_option_list) {
    menu::Runtime rt(no_files());
    const std::shared_ptr<menu::LiveWidget> top = rt.start_layout(menu::parse_layout(option_layout()))->child("Top");
    rt.open_page(top, true);
    rt.settle();
    const std::shared_ptr<menu::LiveWidget> m = top->child("Menu"), other = top->child("Other");
    int changed = -1;
    m->define("onValueChanged", script::Vm::native("test", [&](script::Vm&, const Value&, script::Args a) {
                  changed = a.size() > 1 ? int(a[1].as_float()) : -2;
                  return Value{};
              }));
    // Tap the menu (focus), tap again (Accept opens its list over the menu: row 0 on it).
    CHECK(rt.focused() == other.get());
    CHECK(menu::pointer_tap(rt, boxes_of(top), 250, 210));
    rt.settle();
    CHECK(rt.focused() == m.get());
    CHECK(menu::pointer_tap(rt, boxes_of(top), 250, 210));
    rt.settle();
    CHECK(m->data.number("is_popuped") != 0);
    // Open, only its rows: the third row (y 248..272) takes the choice and the focus; a tap on it keeps it.
    std::vector<menu::DrawnBox> boxes = boxes_of(top);
    const menu::PointerHit h = menu::pointer_hit(rt, boxes, 250, 260);
    CHECK(h.kind == menu::PointerHit::Kind::OptionRow && h.row == 2);
    CHECK(menu::pointer_hit(rt, boxes, 450, 110).kind == menu::PointerHit::Kind::None); // (the button under it: no)
    CHECK(menu::pointer_tap(rt, boxes, 250, 260));
    rt.settle();
    CHECK(m->index() == 2 && m->data.number("is_popuped") != 0);
    CHECK(menu::pointer_tap(rt, boxes_of(top), 250, 260));
    rt.settle();
    CHECK(m->data.number("is_popuped") == 0 && changed == 2 && rt.focused() == m.get());
    // Open again, a tap off the list closes it as Cancel: the index as it was.
    CHECK(menu::pointer_tap(rt, boxes_of(top), 250, 210));
    rt.settle();
    CHECK(m->data.number("is_popuped") != 0);
    boxes = boxes_of(top);
    menu::pointer_hover(rt, boxes, 250, 180); // (row 2 on the menu: row 1 above it)
    rt.settle();
    CHECK(m->index() == 1);
    CHECK(menu::pointer_tap(rt, boxes, 20, 20));
    rt.settle();
    CHECK(m->data.number("is_popuped") == 0 && m->index() == 2 && changed == 2);
}

namespace {

const char* kScrollBox = R"(Project {
	name string {"TestProject"}
	children [1] {
		RootWindow {
			name string {"Top"}
			children [2] {
				ScrollBox { name string {"Scroll"} geometry rectangle {100 300 300 60} children [3] {
					ListBox { name string {"List"} can_focus digit {1} geometry rectangle {20 0 260 60} direction digit {0} }
					Composite { name string {"dec_arrow"} geometry rectangle {0 20 15 20} }
					Composite { name string {"inc_arrow"} geometry rectangle {285 20 15 20} }
				} }
				Composite { name string {"Row"} geometry rectangle {0 0 50 60} visible digit {0} }
			}
		}
	}
})";

} // namespace

TEST(menu_pointer_scroll_box_arrows) {
    menu::Runtime rt(no_files());
    const std::shared_ptr<menu::LiveWidget> top = rt.start_layout(menu::parse_layout(std::string_view(kScrollBox)))->child("Top");
    script::Vm& vm = rt.vm();
    rt.open_page(top, true);
    const std::shared_ptr<menu::LiveWidget> list = top->child("Scroll")->child("List");
    const Value lv = menu::widget_value(list);
    call(vm, lv, "setItemTemplate", {rt.context(), menu::widget_value(top->child("Row"))});
    call(vm, lv, "setItemCount", {Value::integer(10)});
    rt.settle();
    rt.update();
    // The arrows beside a list across: right is a row on, left a row back (the focus taken by the list first).
    const std::vector<menu::DrawnBox> boxes = boxes_of(top);
    CHECK(menu::pointer_hit(rt, boxes, 392, 330).kind == menu::PointerHit::Kind::Arrow);
    CHECK(menu::pointer_tap(rt, boxes, 392, 330));
    CHECK(menu::pointer_tap(rt, boxes, 392, 330));
    rt.settle();
    CHECK(focus_index(vm, list) == 2 && rt.focused() == list.get());
    CHECK(menu::pointer_tap(rt, boxes, 105, 330));
    rt.settle();
    CHECK(focus_index(vm, list) == 1);
}
