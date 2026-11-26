// The menu files' readers on made-up files, then on the discs of GT4R_DISC.

#include "test_framework.h"

#include "gt4r/menu/font.h"
#include "gt4r/menu/image_pack.h"
#include "gt4r/menu/layout.h"
#include "gt4r/menu/project.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/menu/text_table.h"

#include <cstring>

using namespace gt4r;

namespace {

void put32(Bytes& b, size_t at, u32 v) {
    if (b.size() < at + 4) b.resize(at + 4);
    std::memcpy(b.data() + at, &v, 4);
}
void put_text(Bytes& b, size_t at, const std::string& s) {
    if (b.size() < at + s.size() + 1) b.resize(at + s.size() + 1);
    std::memcpy(b.data() + at, s.data(), s.size());
}

bool throws(const std::function<void()>& f) {
    try {
        f();
    } catch (const FormatError&) {
        return true;
    }
    return false;
}

} // namespace

TEST(layout_parses) {
    const char* text = R"(Project {
	name string {"DemoProject"}
	children [1] {
		RootWindow {
			name string {"TopRoot"}
			has_script digit {1}
			children [3] {
				ColorFace {
					name string {"bg"}
					color [4] {
						RGBA {1 2 3 255}
						RGBA {4 5 6 255}
						RGBA {7 8 9 0}
						RGBA {10 11 12 0}
					}
				}
				Composite {
					name string {"Title"}
					geometry rectangle {0 8.5 640 56}
					children [1] {
						TextFace {
							name string {"label"}
							text_color RGBA {154 154 161 255}
							font string {"tr_30"}
							align digit {7}
							text string {"Say \"hi\" {now}"}
						}
					}
				}
				ModelFace {
					name string {"car"}
					camera_trans [3] {
						digit {0}
						digit {1.5}
						digit {-4}
					}
					model_rotate vector3 {0 90 0}
				}
			}
		}
	}
}
)";
    const menu::Widget project = menu::parse_layout(std::string_view(text));
    CHECK(project.type == "Project" && project.name() == "DemoProject" && project.children().size() == 1 && project.count() == 6);
    const menu::Widget& root = project.children()[0];
    CHECK(root.type == "RootWindow" && root.number("has_script") == 1 && root.children().size() == 3);
    const std::vector<menu::Rgba> colors = root.children()[0].colors("color");
    CHECK(colors.size() == 4 && colors[1].g == 5 && colors[3].a == 0);
    const menu::Widget* title = root.child("Title");
    CHECK(title && title->rect("geometry") && title->rect("geometry")->y == 8.5f && title->rect("geometry")->h == 56);
    const menu::Widget* label = project.child("TopRoot/Title/label");
    CHECK(label && label->type == "TextFace" && label->text("text") == "Say \"hi\" {now}" && label->number("align") == 7);
    CHECK(label && label->color("text_color") && label->color("text_color")->r == 154 && label->text("font") == "tr_30");
    CHECK(project.child("TopRoot::Title::label") == label && !project.child("TopRoot/None") && !label->has("geometry"));
    const menu::Widget* car = root.child("car");
    CHECK(car && car->find("camera_trans") && car->find("camera_trans")->items.size() == 3 && car->find("camera_trans")->items[1].values[0] == "1.5");
    CHECK(car && car->find("model_rotate")->type == "vector3" && car->find("model_rotate")->values.size() == 3);
    // A list whose count is wrong, a widget that does not end, text after the end.
    CHECK(throws([] { menu::parse_layout(std::string_view("A { children [2] { B { name string {\"x\"} } } }")); }));
    CHECK(throws([] { menu::parse_layout(std::string_view("A { name string {\"x\"}")); }));
    CHECK(throws([] { menu::parse_layout(std::string_view("A { } B { }")); }));
}

TEST(text_table_reads) {
    // Two pages: "Demo::Top" with two pairs, "Empty" with none.
    Bytes f = {'R', 'T', '0', '3'};
    put32(f, 0xC, 2);
    put32(f, 0x10, 0x40), put32(f, 0x14, 2), put32(f, 0x18, 0x50), put32(f, 0x1C, 0x5E5E5E5E);
    put32(f, 0x20, 0x4C), put32(f, 0x24, 0), put32(f, 0x28, 0x60), put32(f, 0x2C, 0x5E5E5E5E);
    put_text(f, 0x40, "Demo::Top"), put_text(f, 0x4C, "Em");
    put32(f, 0x50, 0x60), put32(f, 0x54, 0x64), put32(f, 0x58, 0x70), put32(f, 0x5C, 0x74);
    put_text(f, 0x60, "ok"), put_text(f, 0x64, "C\xC3\xB4te"), put_text(f, 0x70, "no"), put_text(f, 0x74, "Cancel");
    const menu::TextTable t = menu::TextTable::parse(f);
    CHECK(t.pages().size() == 2 && t.size() == 2);
    CHECK(t.find("Demo::Top", "ok") && *t.find("Demo::Top", "ok") == "C\xC3\xB4te" && *t.find("Demo::Top", "no") == "Cancel");
    CHECK(!t.find("Demo::Top", "yes") && !t.find("Other", "ok") && t.page("Em") && t.page("Em")->empty());
    // Prologue's: the page count right after the magic, the pages from +8.
    Bytes old = {'R', 'T', '0', '2'};
    put32(old, 4, 1);
    put32(old, 8, 0x18), put32(old, 0xC, 1), put32(old, 0x10, 0x20), put32(old, 0x14, 0x5E5E5E5E);
    put_text(old, 0x18, "Page");
    put32(old, 0x20, 0x28), put32(old, 0x24, 0x2C);
    put_text(old, 0x28, "key"), put_text(old, 0x2C, "text");
    const menu::TextTable o = menu::TextTable::parse(old);
    CHECK(o.pages().size() == 1 && o.find("Page", "key") && *o.find("Page", "key") == "text");
    Bytes bad = f;
    put32(bad, 0x14, 0x7FFFFFFF);
    CHECK(throws([&] { menu::TextTable::parse(bad); }));
    put32(bad, 0x14, 2), put32(bad, 0x50, 0x1000);
    CHECK(throws([&] { menu::TextTable::parse(bad); }));
}

TEST(image_pack_reads) {
    // gpb2 with two files; then the same as gpb1 (no sizes).
    Bytes f = {'g', 'p', 'b', '2'};
    put32(f, 0xC, 2);
    put32(f, 0x10, 0x30), put32(f, 0x14, 0x40), put32(f, 0x18, 4), put32(f, 0x1C, 0);
    put32(f, 0x20, 0x38), put32(f, 0x24, 0x44), put32(f, 0x28, 8), put32(f, 0x2C, 0);
    put_text(f, 0x30, "a.png"), put_text(f, 0x38, "b.png");
    put_text(f, 0x40, "AAAABBBBBBBB");
    f.resize(0x4C);
    const menu::ImagePack p = menu::ImagePack::parse(f);
    CHECK(p.version() == 2 && p.entries().size() == 2 && p.find("b.png") && !p.find("c.png"));
    CHECK(p.find("a.png") && p.read(*p.find("a.png")) == Bytes({'A', 'A', 'A', 'A'}) && p.read(*p.find("b.png")).size() == 8);
    Bytes g = {'g', 'p', 'b', '1'};
    put32(g, 0xC, 2);
    put32(g, 0x10, 0x20), put32(g, 0x14, 0x30), put32(g, 0x18, 0x28), put32(g, 0x1C, 0x34);
    put_text(g, 0x20, "a.png"), put_text(g, 0x28, "b.png"), put_text(g, 0x30, "AAAABBBBBBBB");
    g.resize(0x3C);
    const menu::ImagePack q = menu::ImagePack::parse(g);
    CHECK(q.version() == 1 && q.read(*q.find("a.png")).size() == 4 && q.read(*q.find("b.png")) == Bytes({'B', 'B', 'B', 'B', 'B', 'B', 'B', 'B'}));
    put32(f, 0x18, 0x1000);
    CHECK(throws([&] { menu::ImagePack::parse(f); }));
    CHECK(!menu::ImagePack::is_pack(Bytes{'g', 'p', 'b', '7', 0, 0, 0, 0}));
}

// Every project of the discs: the layout reads, every picture a widget names is in its page's
// pack and decodes, every text key has its text; the fonts read.
TEST(disc_menus_read) {
    if (test::reference_discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::reference_discs()) {
        const menu::FileReader read = [&](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = disc->volume().find(path);
            if (!e) return std::nullopt;
            return disc->volume().read(*e);
        };
        const menu::FolderLister list = [&](const std::string& folder) {
            std::vector<std::string> out;
            for (const gt3r::Volume::Entry& e : disc->volume().entries()) {
                if (e.path.compare(0, folder.size(), folder) != 0) continue;
                const size_t slash = e.path.find('/', folder.size());
                std::string name = e.path.substr(folder.size(), slash == std::string::npos ? slash : slash + 1 - folder.size());
                if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
            }
            return out;
        };
        size_t projects = 0, pages = 0, widgets = 0, pictures = 0, missing = 0, keys = 0, no_text = 0;
        for (const std::string& name : menu::list_projects(list)) {
            const std::vector<std::string> languages = menu::project_languages(list, name);
            CHECK(!languages.empty());
            if (languages.empty()) continue;
            menu::Project project(read, name, languages[0]);
            ++projects;
            for (const menu::Widget& page : project.pages()) {
                ++pages;
                std::function<void(const menu::Widget&)> walk = [&](const menu::Widget& w) {
                    ++widgets;
                    for (const menu::Property& p : w.properties) {
                        if (p.name.size() >= 10 && p.name.compare(p.name.size() - 10, 10, "image_path") == 0 && !p.values.empty() && !p.values[0].empty()) {
                            ++pictures;
                            const gt3r::Tex1::Image* image = project.image(page.name(), p.values[0]);
                            if (!image || !image->width || image->rgba.size() != size_t(image->width) * image->height) ++missing;
                        }
                        if (p.name == "key" && !p.values.empty() && !p.values[0].empty()) {
                            ++keys;
                            no_text += !project.text().find(project.module_name() + "::" + page.name(), p.values[0]);
                        }
                        for (const menu::Widget& c : p.widgets) walk(c);
                    }
                };
                walk(page);
            }
        }
        std::printf("  %s: %zu projects, %zu pages, %zu widgets, %zu pictures (%zu not found), %zu keys (%zu without text)\n",
                    disc->info().serial.c_str(), projects, pages, widgets, pictures, missing, keys, no_text);
        CHECK(projects > 0 && pages > projects && missing == 0 && no_text == 0);
        if (disc->info().game == Game::GT4) CHECK(projects == 33 && pages == 213 && widgets == 12713 && pictures == 10163 && keys == 1412);

        size_t fonts = 0;
        for (const char* path : {"font/system.fnt", "font/menu.fnt", "font/race.fnt"}) {
            const std::optional<Bytes> file = read(path);
            CHECK(file.has_value());
            if (!file) continue;
            for (const gt3r::text::LatinFont& font : menu::read_fonts(*file).fonts) {
                ++fonts;
                CHECK(!font.name.empty() && font.line_height > 0 && font.atlas[0].width > 0);
                for (const gt3r::text::FontGlyph& g : font.glyphs) // every glyph's picture is inside the font's
                    if (g.width) CHECK(u32(g.atlas_x) + g.width <= font.atlas[0].width && u32(g.atlas_y) + g.height <= font.atlas[0].height);
            }
        }
        CHECK(fonts >= 10);
        if (disc->info().game == Game::GT4) {
            const gt3r::text::FontFile menu_font = menu::read_fonts(*read("font/menu.fnt"));
            CHECK(menu_font.fonts.size() == 1 && menu_font.fonts[0].name == "tr_30" && menu_font.fonts[0].has('A') && menu_font.fonts[0].has(0xE9));
            menu::Project boot(read, "boot", "US");
            CHECK(boot.module_name() == "BootProject" && boot.translate("EntryRoot", "Welcome to GT WORLD") == "Welcome to Gran Turismo 4");
            CHECK(boot.translate("EntryRoot", "no such key") == "no such key");
        }
    }
}

// A widget as the scripts see it: its properties as attributes, its children as members.
TEST(live_widget_properties) {
    const menu::Widget layout = menu::parse_layout(std::string_view(R"(RootWindow {
	name string {"Top"}
	children [2] {
		SelectBox {
			name string {"Box"}
			geometry rectangle {100 200 300 40}
			direction digit {1}
			children [0] {
			}
		}
		Composite {
			name string {"Item"}
			can_focus digit {1}
			geometry rectangle {10 20 30 40}
			children [1] {
				TextFace { name string {"label"} text string {"one"} }
			}
		}
	}
})"));
    script::Vm vm;
    const std::shared_ptr<menu::LiveWidget> top = menu::LiveWidget::build(layout, vm.root());
    using V = script::Value;
    const V item = vm.lookup("Top::Item");
    CHECK(item.kind == script::Kind::Module && vm.attribute(item, "name").as_string() == "Item");
    CHECK(vm.attribute(item, "x").as_float() == 10 && vm.attribute(item, "h").as_float() == 40 && vm.attribute(item, "visible").i == 1);
    CHECK(vm.attribute(vm.lookup("Top::Item::label"), "text").as_string() == "one" && vm.attribute(item, "actor").is_nil());
    vm.member(item, "visible")->set(vm, V::integer(0));
    vm.member(item, "y")->set(vm, V::number(55.5f));
    vm.member(vm.lookup("Top::Item::label"), "text")->set(vm, V::string("two"));
    const menu::LiveWidget& live = *top->children[1];
    CHECK(!live.visible() && live.data.rect("geometry")->y == 55.5f && live.children[0]->data.text("text") == "two");
    // Copies into a select box.
    const V box = vm.lookup("Top::Box");
    for (int i = 0; i < 3; ++i) {
        const V copy = vm.call(vm.attribute(item, "doCopy"), {});
        const V args[1] = {copy};
        vm.call(vm.attribute(box, "appendChild"), args);
    }
    const menu::LiveWidget& b = *top->children[0];
    CHECK(b.children.size() == 3 && b.children[0] != top->children[1] && b.children[2]->children.size() == 1);
    CHECK(vm.attribute(box, "first").p == b.children[0] && vm.attribute(V::of(script::Kind::Module, b.children[0]), "next").p == b.children[1]);
    CHECK(vm.attribute(V::of(script::Kind::Module, b.children[2]), "next").is_nil());
    vm.member(box, "index")->set(vm, V::integer(1));
    CHECK(b.children[0]->data.rect("geometry")->y == -40 && b.children[1]->data.rect("geometry")->y == 0 && b.children[2]->data.rect("geometry")->y == 40);
    // The rows keep the first one's size; the chosen one is in the middle of the box.
    CHECK(b.children[1]->screen_rect().x == 100 && b.children[1]->screen_rect().y == 200 && b.children[1]->screen_rect().w == 30);
    vm.call(vm.attribute(box, "clearChildren"), {});
    CHECK(b.children.empty() && vm.attribute(box, "first").is_nil());
    // A child asked by name that is not there is nil; a value assigned to the name stays. The
    // page a text's key is looked up in goes with the widget.
    CHECK(vm.binary("[]", item, V::string("nothing")).as<script::Reference>()->get(vm).is_nil());
    CHECK(!vm.binary("[]", item, V::string("label")).as<script::Reference>()->get(vm).is_nil());
    vm.binary("[]", item, V::string("notice")).as<script::Reference>()->set(vm, V::string("kept"));
    CHECK(vm.attribute(item, "notice").as_string() == "kept");
}
