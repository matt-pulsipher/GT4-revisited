#pragma once

// The fields the save editor opens over its pages: a list to choose from, a number, a short text, a question. Each takes the
// pad (update), typed keys (key) and clicks on what it drew (click, with the area's id and argument of its HitList).

#include "gt4r/app/app.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace gt4r {

// The areas' ids (a page's own are below them).
enum PopupHit { kPopupItem = 100, kPopupKey, kPopupAnswer, kPopupPanel };

// A dark veil and a panel in the middle; the panel's rectangle.
inline gt3r::UiRect popup_panel(Ui& ui, gt3r::HitList& hits, float w, float h, const std::string& title) {
    hits.clear();
    ui.fill(0, 0, Ui::kWidth, Ui::kHeight, Color{0, 0, 0, 150});
    const gt3r::UiRect r{(Ui::kWidth - w) / 2, (Ui::kHeight - h) / 2 - 6, w, h};
    ui.fill_round(r.x, r.y, r.w, r.h, 5, colors::kBackground);
    ui.frame_round(r.x, r.y, r.w, r.h, 5, 1, colors::kOutline);
    ui.text(r.x + 12, r.y + 9, Ui::elide(title, w - 24), colors::kAccent);
    hits.add(r, kPopupPanel);
    return r;
}

struct ChoicePopup {
    struct Item {
        std::string label{}, note{};
        bool swatch = false, mark = false; // `mark`: the one now
        u8 rgb[3] = {};
    };
    static constexpr int kRows = 14;
    bool active = false;
    std::string title;
    std::vector<Item> items;
    int sel = 0, top = 0;
    std::function<void(int)> done;
    std::string typed;

    void open(std::string t, std::vector<Item> list, int now, std::function<void(int)> f) {
        title = std::move(t), items = std::move(list), sel = std::clamp(now, 0, std::max(0, int(items.size()) - 1)), done = std::move(f), active = !items.empty();
        top = sel - kRows / 2, typed.clear();
        clamp_top();
    }
    void clamp_top() { top = std::clamp(top, 0, std::max(0, int(items.size()) - kRows)); }
    void show() {
        if (sel < top) top = sel;
        if (sel >= top + kRows) top = sel - kRows + 1;
        clamp_top();
    }
    void choose(int i) {
        active = false;
        if (const auto f = std::move(done); f && i >= 0 && i < int(items.size())) f(i);
    }
    void move(int d) { sel = std::clamp(sel + d, 0, int(items.size()) - 1), show(); }
    void update(const MenuInput& m) {
        if (const int v = m.vertical()) move(v), typed.clear();
        if (m.pressed(PadButton::L1) || (m.horizontal() < 0)) move(-kRows);
        if (m.pressed(PadButton::R1) || (m.horizontal() > 0)) move(kRows);
        if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) active = false;
        else if (m.pressed(PadButton::Cross)) choose(sel);
    }
    // Letters typed go to the first item that has them.
    void key(const SDL_KeyboardEvent& k) {
        if (k.scancode == SDL_SCANCODE_ESCAPE) active = false;
        else if (k.scancode == SDL_SCANCODE_RETURN || k.scancode == SDL_SCANCODE_KP_ENTER) choose(sel);
        else if (k.scancode == SDL_SCANCODE_UP) move(-1);
        else if (k.scancode == SDL_SCANCODE_DOWN) move(1);
        else if (k.scancode == SDL_SCANCODE_PAGEUP) move(-kRows);
        else if (k.scancode == SDL_SCANCODE_PAGEDOWN) move(kRows);
        else if (k.scancode == SDL_SCANCODE_BACKSPACE) typed.clear();
        else if (k.key >= 32 && k.key < 127) {
            typed += char(std::tolower(int(k.key)));
            for (size_t i = 0; i < items.size(); ++i) {
                std::string s = items[i].label;
                for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
                if (s.find(typed) != std::string::npos) {
                    sel = int(i), show();
                    return;
                }
            }
            typed = typed.substr(0, typed.size() - 1);
        }
    }
    void wheel(int steps) { top -= steps, clamp_top(); }
    void render(Ui& ui, gt3r::HitList& hits) {
        const int n = std::min(kRows, int(items.size()));
        const gt3r::UiRect r = popup_panel(ui, hits, 420, 44 + float(n) * 17, title);
        if (!typed.empty()) ui.text_right(r.x + r.w - 12, r.y + 9, typed, colors::kText);
        for (int i = top; i < std::min(int(items.size()), top + kRows); ++i) {
            const Item& it = items[size_t(i)];
            const float y = r.y + 28 + float(i - top) * 17;
            const bool on = i == sel;
            ui.fill_round(r.x + 8, y - 3, r.w - 20, 15, 3, on ? colors::kHighlight : colors::kPanel);
            float x = r.x + 16;
            if (it.swatch) {
                ui.fill_round(x, y - 1, 18, 10, 2, Color{it.rgb[0], it.rgb[1], it.rgb[2]});
                ui.frame_round(x, y - 1, 18, 10, 2, 1, colors::kOutline);
                x += 24;
            }
            const float room = r.x + r.w - 20 - x - (it.note.empty() ? 0 : Ui::text_width(it.note) + 10);
            ui.text(x, y, Ui::elide(it.label, std::max(80.0f, room)), it.mark ? colors::kAccent : on ? colors::kText : colors::kDim);
            ui.text_right(r.x + r.w - 20, y, it.note, colors::kDim);
            hits.add({r.x + 8, y - 3, r.w - 20, 17}, kPopupItem, i);
        }
        if (int(items.size()) > kRows) ui.scrollbar({r.x, r.y + 25, r.w - 2, kRows * 17.0f}, float(items.size()) * 17, float(top) * 17);
        ui.footer("{Up/Down}: select  {Left/Right}: a page  {Cross}: choose  {Triangle}: cancel");
    }
};

// A number, or a time of minutes, seconds and thousandths.
struct NumberPopup {
    bool active = false, time = false;
    std::string title, digits;
    s64 lo = 0, hi = 0;
    int focus = 0; // 0 .. 11: 7 8 9, 4 5 6, 1 2 3, Del 0 OK
    std::function<void(s64)> done;

    static constexpr const char* kKeys[12] = {"7", "8", "9", "4", "5", "6", "1", "2", "3", "Delete", "0", "OK"};

    void open(std::string t, s64 now, s64 l, s64 h, bool is_time, std::function<void(s64)> f) {
        title = std::move(t), lo = l, hi = h, time = is_time, done = std::move(f), active = true, focus = 11;
        digits.clear(); // (empty: the value as it is)
        now_ = now;
    }
    s64 value() const {
        if (digits.empty()) return now_;
        const s64 v = std::atoll(digits.c_str());
        if (!time) return v;
        return v / 100'000 * 60'000 + std::min<s64>(v / 1'000 % 100, 59) * 1'000 + v % 1'000;
    }
    std::string text(s64 v) const {
        if (!time) return std::to_string(v);
        char b[32];
        std::snprintf(b, sizeof b, "%d'%02d.%03d", int(v / 60'000), int(v / 1'000 % 60), int(v % 1'000));
        return b;
    }
    bool fits() const { return value() >= lo && value() <= hi; }
    void add(char c) {
        if (digits.size() < (time ? 7u : 10u) && !(digits.empty() && c == '0' && time)) digits += c;
    }
    void erase() {
        if (!digits.empty()) digits.pop_back();
    }
    void finish() {
        const s64 v = std::clamp(value(), lo, hi);
        active = false;
        if (const auto f = std::move(done)) f(v);
    }
    void press(int k) {
        if (k == 9) erase();
        else if (k == 11) finish();
        else add(kKeys[k][0]);
    }
    void update(const MenuInput& m) {
        int r = focus / 3, c = focus % 3;
        r = std::clamp(r + m.vertical(), 0, 3), c = std::clamp(c + m.horizontal(), 0, 2);
        focus = r * 3 + c;
        if (m.pressed(PadButton::Cross)) press(focus);
        else if (m.pressed(PadButton::Square)) erase();
        else if (m.pressed(PadButton::Start)) finish();
        else if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) active = false;
    }
    void key(const SDL_KeyboardEvent& k) {
        if (k.scancode == SDL_SCANCODE_ESCAPE) active = false;
        else if (k.scancode == SDL_SCANCODE_RETURN || k.scancode == SDL_SCANCODE_KP_ENTER) finish();
        else if (k.scancode == SDL_SCANCODE_BACKSPACE || k.scancode == SDL_SCANCODE_DELETE) erase();
        else if (k.key >= SDLK_0 && k.key <= SDLK_9) add(char(k.key));
        else if (k.scancode >= SDL_SCANCODE_KP_1 && k.scancode <= SDL_SCANCODE_KP_9) add(char('1' + (k.scancode - SDL_SCANCODE_KP_1)));
        else if (k.scancode == SDL_SCANCODE_KP_0) add('0');
    }
    void render(Ui& ui, gt3r::HitList& hits) {
        const gt3r::UiRect r = popup_panel(ui, hits, 220, 176, title);
        ui.fill_round(r.x + 12, r.y + 26, r.w - 24, 20, 3, colors::kPanel);
        ui.text_right(r.x + r.w - 20, r.y + 32, text(value()), fits() ? digits.empty() ? colors::kDim : colors::kText : colors::kBad);
        ui.text(r.x + 12, r.y + 52, text(lo) + " to " + text(hi), colors::kDim);
        for (int i = 0; i < 12; ++i) {
            const gt3r::UiRect b{r.x + 12 + float(i % 3) * 66, r.y + 68 + float(i / 3) * 25, 62, 21};
            ui.button(b, kKeys[i], i == focus, false, i == 11);
            hits.add(b, kPopupKey, i);
        }
        ui.footer("{Cross}: press  {Square}: delete  {Start}: OK  {Triangle}: cancel");
    }

private:
    s64 now_ = 0;
};

// Letters, as a name page's.
struct TextPopup {
    bool active = false, upper = true;
    std::string title, value;
    size_t most = 10;
    int focus = 0; // 26 letters, then Aa, Delete, OK
    std::function<void(const std::string&)> done;

    static constexpr int kColumns = 9;
    static std::string key_name(int i, bool up) { return i < 26 ? std::string(1, char((up ? 'A' : 'a') + i)) : i == 26 ? std::string(up ? "a-z" : "A-Z") : i == 27 ? "Del" : "OK"; }
    static constexpr int kCount = 29;

    void open(std::string t, std::string now, size_t n, std::function<void(const std::string&)> f) {
        title = std::move(t), value = std::move(now), most = n, done = std::move(f), active = true, focus = 0, upper = value.empty();
    }
    void add(char c) {
        if (value.size() < most) value += c;
        upper = value.empty();
    }
    void erase() {
        if (!value.empty()) value.pop_back();
        upper = value.empty();
    }
    void finish() {
        if (value.empty()) return; // (a name has a letter)
        active = false;
        if (const auto f = std::move(done)) f(value);
    }
    void press(int k) {
        if (k < 26) add(char((upper ? 'A' : 'a') + k));
        else if (k == 26) upper = !upper;
        else if (k == 27) erase();
        else finish();
    }
    void update(const MenuInput& m) {
        int r = focus / kColumns, c = focus % kColumns;
        r = std::clamp(r + m.vertical(), 0, (kCount - 1) / kColumns), c = std::clamp(c + m.horizontal(), 0, kColumns - 1);
        focus = std::min(r * kColumns + c, kCount - 1);
        if (m.pressed(PadButton::Cross)) press(focus);
        else if (m.pressed(PadButton::Square)) erase();
        else if (m.pressed(PadButton::Start)) finish();
        else if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) active = false;
    }
    void key(const SDL_KeyboardEvent& k) {
        if (k.scancode == SDL_SCANCODE_ESCAPE) active = false;
        else if (k.scancode == SDL_SCANCODE_RETURN || k.scancode == SDL_SCANCODE_KP_ENTER) finish();
        else if (k.scancode == SDL_SCANCODE_BACKSPACE || k.scancode == SDL_SCANCODE_DELETE) erase();
        else if (k.key >= SDLK_0 && k.key <= SDLK_9 && !(k.mod & SDL_KMOD_SHIFT)) add(char(k.key));
        else if (k.key >= SDLK_A && k.key <= SDLK_Z) add(char((k.mod & SDL_KMOD_SHIFT ? 'A' : 'a') + (k.key - SDLK_A)));
    }
    void render(Ui& ui, gt3r::HitList& hits) {
        const gt3r::UiRect r = popup_panel(ui, hits, 372, 166, title);
        ui.fill_round(r.x + 12, r.y + 26, r.w - 24, 20, 3, colors::kPanel);
        ui.text(r.x + 20, r.y + 32, value + "_", value.empty() ? colors::kBad : colors::kText);
        for (int i = 0; i < kCount; ++i) {
            const gt3r::UiRect b{r.x + 12 + float(i % kColumns) * 39, r.y + 56 + float(i / kColumns) * 25, 35, 21};
            ui.button(b, key_name(i, upper), i == focus, false, i == kCount - 1);
            hits.add(b, kPopupKey, i);
        }
        ui.footer("{Cross}: press  {Square}: delete  {Start}: OK  {Triangle}: cancel");
    }
};

struct QuestionPopup {
    bool active = false;
    std::string text;
    std::vector<std::pair<std::string, std::function<void()>>> answers;
    int focus = 0;

    void open(std::string t, std::vector<std::pair<std::string, std::function<void()>>> a) { text = std::move(t), answers = std::move(a), focus = 0, active = true; }
    void answer(int i) {
        active = false;
        if (i >= 0 && i < int(answers.size()))
            if (const auto f = answers[size_t(i)].second) f();
    }
    void update(const MenuInput& m) {
        focus = std::clamp(focus + m.vertical() + m.horizontal(), 0, int(answers.size()) - 1);
        if (m.pressed(PadButton::Cross)) answer(focus);
        else if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) active = false;
    }
    void render(Ui& ui, gt3r::HitList& hits) {
        const std::vector<std::string> lines = Ui::wrap(text, 300);
        const gt3r::UiRect r = popup_panel(ui, hits, 324, 24 + float(lines.size()) * 12 + float(answers.size()) * 25, "");
        float y = r.y + 12;
        for (const std::string& l : lines) ui.text(r.x + 12, y, l, colors::kText), y += 12;
        y += 4;
        for (int i = 0; i < int(answers.size()); ++i, y += 25) {
            const gt3r::UiRect b{r.x + 12, y, r.w - 24, 21};
            ui.button(b, answers[size_t(i)].first, i == focus, false, i == 0);
            hits.add(b, kPopupAnswer, i);
        }
        ui.footer("{Up/Down}: select  {Cross}: choose  {Triangle}: cancel");
    }
};

} // namespace gt4r
