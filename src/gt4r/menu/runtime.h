#pragma once

// The menus running: a project's widgets as objects its script drives, and the part of the
// program's menu system the scripts are written against (the context, events, focus). An
// early build: what is written here is listed in docs/formats/menu.md ("Running"); everything
// else of the program's is a stand-in that records what was asked of it.

#include "gt4r/menu/project.h"
#include "gt4r/script/vm.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace gt4r::menu {

class Runtime;

// A widget's states: the bits a face's "visible_condition" is tested against.
enum WidgetState : int { kActive = 1, kFocus = 2, kPrelight = 4, kInsensitive = 8 };

// A widget as the scripts see it: a module (its script functions and its children by name)
// with the layout's properties as attributes (`widget.visible = 0`, `label.text = "..."`).
struct LiveWidget : script::Module {
    Widget data; // its type and properties (without the children)
    std::vector<std::shared_ptr<LiveWidget>> children;
    int state = 0;
    std::map<std::string, std::shared_ptr<script::Variable>> objects; // attributes that hold objects (actor)

    // The widgets of a layout, each child a member of its parent by name.
    static std::shared_ptr<LiveWidget> build(const Widget& layout, const std::shared_ptr<script::Module>& parent);
    // A copy with copies of its children and the same script functions; in no parent yet.
    std::shared_ptr<LiveWidget> copy() const;
    void append(const std::shared_ptr<LiveWidget>& child);
    void remove_children();
    // A box that shows one of its children at a time (SelectBox): places them by its "index".
    void arrange();
    Runtime* runtime = nullptr; // the one running it (none for a page only drawn)

    std::shared_ptr<script::Reference> member(script::Vm&, const script::Value& self, const std::string& name) override;
    std::string describe() const override { return "<" + data.type + " " + path() + ">"; }
    void clear() override;

    script::Value get(const std::string& property) const;
    void set(const std::string& property, const script::Value& v);
    void set_property(const std::string& property, const script::Value& v); // without its side effects
    bool visible() const { return !data.has("visible") || data.number("visible") != 0; }
    bool can_focus() const { return data.number("can_focus") != 0; }
    LiveWidget* parent_widget() const { return dynamic_cast<LiveWidget*>(parent.lock().get()); }
    LiveWidget* page(); // the root window it is in
    Rect screen_rect() const;
};

enum class Key { Up, Down, Left, Right, Accept, Cancel };

class Runtime {
public:
    Runtime(FileReader read, std::string region = "us");
    ~Runtime();

    // Loads a project (its layout as live widgets, its script) and calls its onLoad. Throws
    // FormatError / script::ScriptError when the project cannot start.
    void start_project(const std::string& name, const std::string& language);
    // One frame: the events the scripts queued.
    void update();
    void key(Key k);

    Project* project() { return project_.get(); }
    LiveWidget* current_page() { return pages_.empty() ? nullptr : pages_.back().get(); }
    LiveWidget* focused() { return focus_.get(); }
    const std::vector<std::shared_ptr<LiveWidget>>& open_pages() const { return pages_; }
    script::Vm& vm() { return *vm_; }
    // What happened, newest last: pages opened, sounds asked for, script errors.
    const std::deque<std::string>& log() const { return log_; }
    // The project a script asked for next (main::game.next_menu_project after the page closed).
    std::string wanted_project() const;

    void open_page(const std::shared_ptr<LiveWidget>& page, bool replace);
    void close_page();
    void set_focus(const std::shared_ptr<LiveWidget>& w);
    void note(std::string line);

private:
    struct Impl;
    script::Value call_handler(LiveWidget& from, const std::string& name, std::vector<script::Value> args, bool bubble);
    void run_events();
    void focus_first(LiveWidget& page);

    FileReader read_;
    std::string region_;
    std::unique_ptr<script::Vm> vm_;
    std::unique_ptr<Project> project_;
    std::shared_ptr<LiveWidget> root_widget_; // the project's
    std::vector<std::shared_ptr<LiveWidget>> pages_;
    std::shared_ptr<LiveWidget> focus_;
    script::Value context_;
    std::deque<script::Value> events_;
    std::deque<std::string> log_;
};

} // namespace gt4r::menu
