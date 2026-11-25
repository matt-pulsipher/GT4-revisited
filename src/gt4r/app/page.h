#pragma once

// What the app's own pages (the home screen, Settings, Mods, Controls, the lists) have in common: rows with a value that a pad, the
// keyboard and the mouse change, lists the mouse scrolls and chooses in, and the system's file windows.

#include "app/ui.h"

#include <SDL3/SDL_dialog.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gt4r {

class App;

// The place of a list's first shown row: kept while it can be, moved only so far that the selected row shows.
struct ListScroll {
    int first = 0;
    int keep(int selected, int count, int shown);      // after a key moved the selection
    void turn(float wheel, int& selected, int count, int shown); // the mouse wheel: the list moves, the selection stays in it
};

// The rows of a list page for the mouse: render() says where they are, update() what the mouse did with them.
class RowPointer {
public:
    struct Hit {
        int list = 0, row = -1;
        bool click = false; // else the mouse moved onto it
    };
    void begin() { areas_.clear(); }
    // Rows first .. first + count - 1 of `list`, each `h` tall, from (x, y).
    void rows(int list, int first, int count, float x, float y, float w, float h);
    // A single place (a button): row `row` of `list`.
    void place(int list, int row, const gt3r::UiRect& r);
    // What the mouse did this frame, in order.
    std::vector<Hit> read(App& app) const;
    // The wheel's turns over a list (0 when the mouse is not on it).
    float wheel(App& app, int list) const;
    // For a list that keeps its selection in the middle: a click selects a row (true: the row that was selected already, to open
    // it), the wheel moves the selection among `count`. The row the mouse is on, -1: none.
    bool pick(App& app, int list, int& selected, int count) const;
    int over(App& app, int list) const;

private:
    struct Area {
        gt3r::UiRect r;
        int list, first, count; // (count rows of r.h / count)
    };
    std::vector<Area> areas_;
};

struct PageRow {
    enum class Kind { Action, Choice, Slider, Info, Heading };
    Kind kind = Kind::Info;
    std::string label, value;
    bool enabled = true, bad = false; // bad: the value in red
    std::string tag;                  // a word beside the value ("after a restart")
    float fraction = 0;               // a slider's
    std::function<void(int)> change;  // a choice's or slider's step, -1 or 1
    std::function<void(float)> slide; // a slider's place, 0 .. 1
    std::function<void()> press;      // Cross
    std::function<void()> other;      // Square
    std::string press_hint, other_hint; // what they do, for the footer ("{Cross}: " and "{Square}: " come before)
    bool selectable() const { return kind != Kind::Info && kind != Kind::Heading && enabled; }
};

// The rows of one page. Built anew each frame (clear(), then what is there now); the selection and the scrolling stay.
class PageRows {
public:
    std::vector<PageRow> rows;

    PageRow& heading(std::string label);
    PageRow& info(std::string label, std::string value);
    PageRow& action(std::string label, std::string value, std::string hint, std::function<void()> press);
    PageRow& choice(std::string label, const std::vector<std::string>& names, size_t now, std::function<void(size_t)> set);
    // The same over (value, name) pairs.
    PageRow& pick(std::string label, const std::vector<std::pair<std::string, std::string>>& options, const std::string& now, std::function<void(const std::string&)> set);
    PageRow& slider(std::string label, float now, float lo, float hi, float step, const std::function<std::string(float)>& text, std::function<void(float)> set);

    // The pad, the keyboard and the mouse on the rows. `above`: Up on the first row (a tab bar takes the focus).
    struct Done {
        bool changed = false, above = false;
    };
    Done update(App& app);
    void render(gt3r::Ui& ui, const gt3r::UiRect& area, bool focused = true);
    std::string hint() const; // the selected row's keys

    int selected = -1;
    ListScroll scroll;

private:
    void move(int d);
    static constexpr float kRow = 17;
    struct Place { // a row as drawn
        gt3r::UiRect r, back, forward, track;
        int row = 0;
    };
    std::vector<Place> places_;
    gt3r::UiRect area_{};
    int shown_ = 1, dragging_ = -1;
};

// A question over a page: two answers, the mouse, the pad and the keyboard.
class PageQuestion {
public:
    void ask(std::string title, std::vector<std::string> lines, std::string yes, std::string no, std::function<void()> on_yes);
    bool open() const { return open_; }
    void update(App& app);
    void render(gt3r::Ui& ui);

private:
    bool open_ = false, on_yes_button_ = false;
    std::string title_, yes_, no_;
    std::vector<std::string> lines_;
    std::function<void()> yes_action_;
    gt3r::UiRect yes_r_{}, no_r_{};
};

// The system's window that chooses a file or a folder. Its answer waits here until the page takes it (the window may outlive it). A
// scripted run answers by the environment: GT4R_DIALOG=<path>.
class FileWindow {
public:
    void open_file(App& app, const SDL_DialogFileFilter* filters, int count); // (the filters must live on)
    void open_folder(App& app);
    bool waiting() const { return slot_ != nullptr; }
    // True once, when the player has answered: `path` empty when they chose none (`error`: why it could not open).
    bool take(std::string& path, std::string& error);

private:
    struct Slot;
    bool scripted(App& app);
    std::shared_ptr<Slot> slot_;
};

} // namespace gt4r
