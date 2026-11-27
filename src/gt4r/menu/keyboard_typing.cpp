#include "gt4r/menu/keyboard_typing.h"

#include <cctype>

namespace gt4r::menu {

namespace {

LiveWidget* find_named(LiveWidget& w, const std::string& name, bool shown_only) {
    for (const std::shared_ptr<LiveWidget>& c : w.children) {
        if (!c) continue;
        if (c->name == name && (!shown_only || (c->shown() && !c->insensitive()))) return c.get();
        if (LiveWidget* f = find_named(*c, name, shown_only)) return f;
    }
    return nullptr;
}

} // namespace

LiveWidget* KeyboardTyping::keyboard_of(LiveWidget* page) {
    if (!page) return nullptr;
    // (The box of the InputTextFace whose keys include key_enter.)
    LiveWidget* enter = find_named(*page, "key_enter", false);
    for (LiveWidget* w = enter ? enter->parent_widget() : nullptr; w && w != page; w = w->parent_widget())
        for (const std::shared_ptr<LiveWidget>& c : w->children)
            if (c && c->data.type == "InputTextFace") return w;
    return nullptr;
}

void KeyboardTyping::type(const std::string& text) {
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalpha(c)) queue_.push_back(char(std::tolower(c)));
        else if (ch == '\b' || ch == '\n') queue_.push_back(ch);
    }
}

bool KeyboardTyping::step(Runtime& rt) {
    if (queue_.empty()) return false;
    LiveWidget* page = rt.current_page();
    LiveWidget* keyboard = keyboard_of(page);
    if (!keyboard || !rt.idle()) return keyboard != nullptr; // (the page's script still busy with the last key: wait)
    const char ch = queue_.front();
    queue_.pop_front();
    const std::string name = ch == '\b' ? "key_backspace" : ch == '\n' ? "key_enter" : std::string("key_") + ch;
    LiveWidget* key = find_named(*keyboard, name, true);
    if (!key) return true;
    rt.set_focus(*page, key->self());
    rt.key(Key::Accept);
    return true;
}

} // namespace gt4r::menu
