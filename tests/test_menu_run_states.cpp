// A menu widget's states: what shows by its condition, whose state a face is drawn in, where a face's flag goes, and the focus
// bits of the focused widget's parents.
#include "menu_run_flow.h"

#include "gt4r/menu/layout.h"

using namespace gt4r::menu;

TEST(menu_state_shows) {
    // Without a compare any bit of the condition; a row's plate asks active or focus.
    for (int state = 0; state < 16; ++state) CHECK(state_shows(state, kActive | kFocus, 0) == ((state & (kActive | kFocus)) != 0));
    // With one the compared bits equal the condition's, whatever the others are.
    for (int state = 0; state < 16; ++state) {
        CHECK(state_shows(state, kActive, kActive) == ((state & kActive) != 0));
        CHECK(state_shows(state, 0, kActive) == ((state & kActive) == 0));
        CHECK(state_shows(state, kFocus, kActive | kFocus) == ((state & (kActive | kFocus)) == kFocus));
    }
    // A condition bit outside the compare is not looked at.
    CHECK(state_shows(0, kPrelight, kActive) && !state_shows(kActive, kPrelight, kActive));
    // Neither: always.
    for (int state = 0; state < 16; ++state) CHECK(state_shows(state, 0, 0));
}

namespace {

std::shared_ptr<LiveWidget> row() {
    return LiveWidget::build(parse_layout(R"(Composite {
        name string {"row"}
        children [3] {
            FrameImageFace { name string {"plate"} visible_condition digit {3} }
            Composite {
                name string {"mode"}
                children [2] {
                    ImageFace { name string {"off"} visible_compare digit {1} }
                    ImageFace { name string {"on"} visible_condition digit {1} visible_compare digit {1} }
                }
            }
            ImageFace { name string {"own"} is_face digit {0} }
        }
    })"),
                             nullptr, nullptr);
}

// The names of the faces shown, the states passed down as a page's drawing passes them.
void shown(const LiveWidget& w, int parent_state, std::vector<std::string>& out) {
    const int state = drawn_state(w, parent_state);
    if (!state_shows(state, int(w.data.number("visible_condition")), int(w.data.number("visible_compare")))) return;
    if (w.children.empty()) out.push_back(w.name);
    for (const auto& c : w.children) shown(*c, state, out);
}
std::vector<std::string> shown(const LiveWidget& w) {
    std::vector<std::string> out;
    shown(w, 0, out);
    return out;
}

} // namespace

// A list's chosen row is active: its own faces see that, the faces of a box inside it see the box's state.
TEST(menu_state_row_faces) {
    const auto r = row();
    using Names = std::vector<std::string>;
    CHECK(shown(*r) == (Names{"off", "own"}));
    r->set_state(kActive, true);
    CHECK(shown(*r) == (Names{"plate", "off", "own"}));
    r->child("mode")->set_state(kActive, true);
    CHECK(shown(*r) == (Names{"plate", "on", "own"}));
    r->set_state(kActive, false);
    CHECK(shown(*r) == (Names{"on", "own"}));
    r->set_state(kFocus, true);
    CHECK(shown(*r) == (Names{"plate", "on", "own"}));
}

// A face's flag set is its parent's too, no further; a widget that is no face keeps its own.
TEST(menu_state_face_sets_its_parent) {
    const auto r = row();
    const auto mode = r->child("mode");
    CHECK(is_face(*r->child("plate")) && !is_face(*mode) && !is_face(*r->child("own")));
    mode->child("on")->set("active", script::Value::integer(1));
    CHECK(mode->state == kActive && r->state == 0);
    CHECK(mode->child("off")->get("active").i == 1);
    mode->set("active", script::Value::integer(0));
    CHECK(mode->state == 0 && mode->child("on")->get("active").i == 0);
    r->child("plate")->set("insensitive", script::Value::integer(1));
    CHECK(r->state == kInsensitive && mode->state == 0);
    r->child("own")->set("active", script::Value::integer(1));
    CHECK(r->state == kInsensitive && r->child("own")->state == kActive);
}

// The focus bits are the focused widget's and its parents', up to the page; the last focus's are gone.
TEST(disc_menu_state_focus_chain) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    std::function<void(const LiveWidget&, std::vector<const LiveWidget*>&)> with_bits = [&](const LiveWidget& w, std::vector<const LiveWidget*>& out) {
        if (w.state & kFocus) out.push_back(&w);
        for (const auto& c : w.children) with_bits(*c, out);
    };
    for (int step = 0; step < 4; ++step) {
        LiveWidget* page = f.rt->current_page();
        std::vector<const LiveWidget*> want, got;
        const std::shared_ptr<LiveWidget> focus = page->focus.lock();
        CHECK(focus != nullptr);
        for (const LiveWidget* w = focus.get(); w; w = w == page ? nullptr : w->parent_widget()) want.push_back(w);
        with_bits(*page, got);
        std::sort(want.begin(), want.end()), std::sort(got.begin(), got.end());
        CHECK(want.size() > 1 && want == got);
        f.press({step & 1 ? menu::Key::Left : menu::Key::Right});
        f.rt->settle();
    }
}
