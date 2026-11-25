// The list box as the program runs it (mListBox, docs/formats/menu.md "ListBox"): rows from a
// template, the scroll by `alignment`, the rows in range told to visible_callback, the focus to
// focus_callback(new, old), the update countdown, the keys after the script. Layouts made up here.

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

const char* kLayout = R"(Project {
	name string {"TestProject"}
	children [1] {
		RootWindow {
			name string {"Top"}
			children [2] {
				ListBox { name string {"List"} can_focus digit {1} geometry rectangle {100 100 200 100} direction digit {1} }
				Composite { name string {"Row"} geometry rectangle {0 0 200 20} }
			}
		}
	}
})";

Value call(script::Vm& vm, const Value& of, const char* method, std::vector<Value> args = {}) { return vm.call(vm.attribute(of, method), args); }
void assign(script::Vm& vm, const Value& of, const char* member, const Value& v) { vm.member(of, member)->set(vm, v); }
Value native(std::function<Value(script::Vm&, script::Args)> fn) {
    return script::Vm::native("test", [fn](script::Vm& vm, const Value&, script::Args a) { return fn(vm, a); });
}
std::string indexes(const Value& v) {
    std::string out;
    if (const std::vector<Value>* items = v.items())
        for (const Value& i : *items) out += (out.empty() ? "" : ",") + std::to_string(int(i.as_float()));
    return out;
}

} // namespace

TEST(menu_list_box_rows_scroll_and_callbacks) {
    menu::Runtime rt(no_files());
    const std::shared_ptr<menu::LiveWidget> top = rt.start_layout(menu::parse_layout(std::string_view(kLayout)))->child("Top");
    script::Vm& vm = rt.vm();
    rt.open_page(top, true);
    const std::shared_ptr<menu::LiveWidget> list = top->child("List");
    const Value lv = menu::widget_value(list);
    std::vector<std::string> told;
    assign(vm, lv, "visible_callback", native([&](script::Vm&, script::Args a) {
               told.push_back("visible " + indexes(a.size() > 1 ? a[1] : Value{}));
               return Value{};
           }));
    assign(vm, lv, "focus_callback", native([&](script::Vm&, script::Args a) {
               told.push_back("focus " + std::to_string(int(a[1].as_float())) + " " + std::to_string(int(a[2].as_float())));
               return Value{};
           }));
    assign(vm, lv, "update_callback", native([&](script::Vm&, script::Args a) {
               told.push_back("update " + indexes(a.size() > 1 ? a[1] : Value{}));
               return Value{};
           }));
    call(vm, lv, "setItemTemplate", {rt.context(), menu::widget_value(top->child("Row"))});
    CHECK(list->item_h == 20 && list->item_w == 200);
    // A new list's focus is -1; rows made puts it on the first.
    CHECK(vm.attribute(lv, "focus_index").i == -1);
    call(vm, lv, "setItemCount", {Value::integer(20)});
    CHECK(vm.attribute(lv, "focus_index").i == 0 && call(vm, lv, "getItemCount").i == 20);
    rt.set_focus(*top, list);
    rt.update();
    rt.settle();
    // Rows of 20 in a box of 100, alignment 0.5: the focused row's middle wants the box's
    // middle but the list is held at its start; the range is 2 rows each side of the focused
    // one ((100 - 20) x 0.5 = 40), moved to start at row 0: rows 0..4, all told.
    CHECK(list->list_first == 0 && list->list_last == 4);
    CHECK(!told.empty() && told[0] == "visible 0,1,2,3,4");
    CHECK(told.size() >= 2 && told[1] == "focus 0 -1");
    CHECK(list->list_items[0].active && !list->list_items[1].active);
    // The cursor points at the focused row's widget (a copy of the template).
    CHECK(rt.cursor_widget() && rt.cursor_widget()->name == "Row" && rt.cursor_widget()->data.rect("geometry")->y == 0);
    // getItemWidget(i) is nil until made with the context; then it is the row drawn.
    CHECK(call(vm, lv, "getItemWidget", {Value::integer(2)}).kind == script::Kind::Nil);
    const Value made = call(vm, lv, "getItemWidget", {Value::integer(2), rt.context()});
    CHECK(made.as<menu::LiveWidget>() != nullptr);
    // Down 10 rows: the focused row is put in the middle (scroll 50 - (10 x 20 + 10) = -160).
    told.clear();
    for (int i = 0; i < 10; ++i) rt.key(menu::Key::Down);
    rt.settle();
    for (int i = 0; i < 120; ++i) rt.update();
    CHECK(vm.attribute(lv, "focus_index").i == 10 && std::fabs(list->scroll + 160) < 0.01f);
    CHECK(list->list_first == 8 && list->list_last == 12);
    bool focus_told = false;
    for (const std::string& t : told) focus_told = focus_told || t.rfind("focus 10 ", 0) == 0; // keys taken in one picture are told once
    CHECK(focus_told);
    // An insensitive row is passed; the end holds without `repeat` and the key goes on.
    call(vm, lv, "setItemInsensitive", {Value::integer(11), Value::integer(1)});
    rt.key(menu::Key::Down);
    rt.settle();
    CHECK(vm.attribute(lv, "focus_index").i == 12);
    assign(vm, lv, "focus_index", Value::integer(19));
    rt.key(menu::Key::Down);
    rt.settle();
    CHECK(vm.attribute(lv, "focus_index").i == 19);
    // setItemUpdate(i, 2): update_callback hears it two pictures on.
    told.clear();
    for (int i = 0; i < 60; ++i) rt.update();
    call(vm, lv, "setItemUpdate", {Value::integer(18), Value::integer(2)});
    rt.update();
    rt.update();
    rt.settle();
    bool updated = false;
    for (const std::string& t : told) updated = updated || t == "update 18";
    CHECK(updated);
}
