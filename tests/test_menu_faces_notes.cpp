// Face methods the scripts call that change what is drawn: a colored face's setColor, a picture
// face's texcoord edges, a composite's adjustSize.

#include "menu_run_flow.h"

#include "gt4r/menu/runtime_internal.h"

#include <cmath>
#include <cstdlib>

using script::Value;

namespace {

std::shared_ptr<menu::LiveWidget> page(menu::Runtime& rt, const char* children) {
    const std::string text = std::string(R"(Project {
	name string {"TestProject"}
	children [1] {
		RootWindow {
			name string {"Top"}
			children )") + children + "\n\t\t}\n\t}\n}";
    const std::shared_ptr<menu::LiveWidget> top = rt.start_layout(menu::parse_layout(text))->child("Top");
    rt.open_page(top, true);
    return top;
}

Value color(float r, float g, float b, float a) {
    auto c = std::make_shared<menu::Object>("MColorObject");
    c->set("r", Value::number(r)), c->set("g", Value::number(g)), c->set("b", Value::number(b)), c->set("a", Value::number(a));
    return menu::object_value(c);
}

} // namespace

// setColor on a color face: every one of its colors (the corners keep their count), or the one of an index.
TEST(menu_color_face_set_color) {
    menu::Runtime rt([](const std::string&) -> std::optional<Bytes> { return std::nullopt; });
    const auto top = page(rt, R"([2] {
				ColorFace { name string {"plain"} }
				ColorFace { name string {"corners"} color [4] { RGBA {0 0 0 255} RGBA {0 0 0 255} RGBA {0 0 0 255} RGBA {0 0 0 255} } }
			})");
    script::Vm& vm = rt.vm();
    const Value red[1] = {color(1, 0, 0, 1)};
    vm.call(vm.attribute(menu::widget_value(top->child("plain")), "setColor"), red);
    const std::vector<menu::Rgba> plain = top->child("plain")->data.colors("color");
    CHECK(plain.size() == 1 && plain[0].r == 255 && plain[0].g == 0 && plain[0].a == 255);
    const Value corners = menu::widget_value(top->child("corners"));
    vm.call(vm.attribute(corners, "setColor"), red);
    std::vector<menu::Rgba> c = top->child("corners")->data.colors("color");
    CHECK(c.size() == 4 && c[0].r == 255 && c[3].r == 255);
    const Value blue_last[2] = {color(0, 0, 1, 1), Value::integer(9)};
    vm.call(vm.attribute(corners, "setColor"), blue_last);
    c = top->child("corners")->data.colors("color");
    CHECK(c.size() == 4 && c[2].r == 255 && c[3].b == 255 && c[3].r == 0);
}

// texcoord_right = t: the picture's right corners at u = t, the rest the whole picture.
TEST(menu_image_face_texcoord_edges) {
    menu::Runtime rt([](const std::string&) -> std::optional<Bytes> { return std::nullopt; });
    const auto top = page(rt, R"([1] {
				ImageFace { name string {"bar"} geometry rectangle {0 0 200 10} }
			})");
    script::Vm& vm = rt.vm();
    const Value bar = menu::widget_value(top->child("bar"));
    vm.member(bar, "texcoord_right")->set(vm, Value::number(0.25f));
    vm.member(bar, "texcoord_top")->set(vm, Value::number(0.5f));
    const menu::Property* tc = top->child("bar")->data.find("texcoord");
    CHECK(tc && tc->items.size() == 4);
    if (!tc || tc->items.size() < 4) return;
    auto at = [&](size_t corner, size_t axis) { return std::strtof(tc->items[corner].values[axis].c_str(), nullptr); };
    CHECK(at(0, 0) == 0 && at(0, 1) == 0.5f && at(1, 0) == 0.25f && at(1, 1) == 0.5f && at(2, 0) == 0.25f && at(2, 1) == 1 && at(3, 0) == 0 && at(3, 1) == 1);
    CHECK(vm.attribute(bar, "texcoord_right").as_float() == 0.25f);
}

// A composite's adjustSize: its faces to its top left, its size the largest of theirs (a face without a picture its own).
TEST(menu_composite_adjust_size) {
    menu::Runtime rt([](const std::string&) -> std::optional<Bytes> { return std::nullopt; });
    const auto top = page(rt, R"([1] {
				Composite {
					name string {"box"}
					geometry rectangle {10 10 140 50}
					children [3] {
						ColorFace { name string {"a"} geometry rectangle {5 6 30 20} }
						ColorFace { name string {"b"} geometry rectangle {0 0 12 44} }
						Composite { name string {"inner"} geometry rectangle {3 3 300 300} }
					}
				}
			})");
    script::Vm& vm = rt.vm();
    const Value box = menu::widget_value(top->child("box"));
    vm.call(vm.attribute(box, "adjustSize"), {});
    const auto b = top->child("box");
    CHECK(b->get("x").as_float() == 10 && b->get("w").as_float() == 30 && b->get("h").as_float() == 44);
    CHECK(b->child("a")->get("x").as_float() == 0 && b->child("a")->get("y").as_float() == 0);
    CHECK(b->child("inner")->get("x").as_float() == 3); // not a face: neither moved nor counted
}
