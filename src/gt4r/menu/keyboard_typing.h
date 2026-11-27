#pragma once

// Letters typed on a real keyboard into a page with a keyboard of its own (the boot's name entry, EntryRoot's `Keyboard`:
// an InputTextFace `input` and the key buttons `key_a` .. `key_z`, `key_backspace`, `key_enter`, in a lower- and an
// upper-case set of which the page shows one). A typed letter presses the page's key of that letter in the set shown (the
// focus on it, then Accept), so the page's own script puts it in, with its sound, its case rule and its length limit;
// Backspace presses `key_backspace`, Enter `key_enter`. The app's side (SDL's text input, the keys held) is the menu
// viewer's (screen_menu_viewer.cpp).

#include "gt4r/menu/runtime.h"

#include <deque>
#include <string>

namespace gt4r::menu {

class KeyboardTyping {
public:
    // The page's keyboard (the box holding the InputTextFace and the keys); null when the page has none.
    static LiveWidget* keyboard_of(LiveWidget* page);
    // Typed text as a keyboard gives it ('\b' Backspace, '\n' Enter); what the page has no key for is dropped when pressed.
    void type(const std::string& text);
    bool pending() const { return !queue_.empty(); }
    void clear() { queue_.clear(); }
    // One typed key pressed on the top page's keyboard (one a frame, as the page's script takes a key); false with nothing
    // waiting or no keyboard on the page.
    bool step(Runtime& rt);

private:
    std::deque<char> queue_; // a .. z, '\b', '\n'
};

} // namespace gt4r::menu
