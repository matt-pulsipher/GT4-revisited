#pragma once

// The menus running: the program's menu system as the game's scripts are written against it.
// A project's widgets are objects of the script world; the contexts open and close pages; an
// event thread calls the scripts' event functions, which may wait (`context.sync`, a dialog's
// own event loop) while the pages go on drawing. docs/formats/menu.md ("Running") has what
// follows the program and what is still judged from how the scripts use it.

#include "disc/core_image.h"
#include "gt4r/menu/game.h"
#include "gt4r/menu/native.h"
#include "gt4r/menu/project.h"
#include "gt4r/script/vm.h"

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gt4r::menu {

class Runtime;
struct GameSettings; // game_settings.h
struct PhotoShoot;   // photo_natives.h
struct PhotoPicture;

// A widget's states: the bits a face's "visible_condition" is tested against.
enum WidgetState : int { kActive = 1, kFocus = 2, kPrelight = 4, kInsensitive = 8 };

// A widget as the scripts see it: a module (its script functions and its children by name)
// with the layout's properties as attributes (`widget.visible = 0`, `label.text = "..."`).
struct LiveWidget : script::Module {
    Widget data; // its type and properties (without the children)
    std::vector<std::shared_ptr<LiveWidget>> children;
    int state = 0;
    std::shared_ptr<script::Heap> actor;                              // what moves or fades it (`widget.actor`)
    std::map<std::string, std::shared_ptr<script::Reference>> extras; // attributes the program keeps that are no layout property
    bool by_name_ = false; // inside element(): a name that is not there is nil
    std::weak_ptr<LiveWidget> focus;                                  // a page's: the widget of it that has the focus
    std::vector<std::weak_ptr<LiveWidget>> focus_chain;               // a page's: the widgets that have the focus bits (the focus and its parents)
    std::weak_ptr<LiveWidget> focus_seen;                             // a page's: the focus its last picture had (the focus events)
    Runtime* runtime = nullptr;                                       // the one running it (none for a page only drawn)
    // A list box's rows (mListBox's items): the widget the scripts gave or made for the row (none:
    // the template is drawn there, `drawn` its copy for the row), its place and size in the
    // list, its update countdown and flags.
    struct ListItem {
        std::shared_ptr<LiveWidget> widget, drawn;
        float x = 0, y = 0, w = 0, h = 0;
        int update = 0;
        bool visible = false, active = false, insensitive = false;
    };
    std::vector<ListItem> list_items;
    std::shared_ptr<LiveWidget> list_template;
    int list_first = 0, list_last = -1, list_was_first = 0, list_was_last = -1; // the rows in range, now and the picture before
    int list_told = -1;                                                       // the focus focus_callback last heard
    bool list_redraw = false;                                                 // postRedraw: every row in range enters again
    float scroll = 0, scroll_target = 0; // of a scroll window or list: how far its content is moved
    float item_w = 16, item_h = 16;      // a select box's: the size of its rows (its first row's)

    // The widgets of a layout, each child a member of its parent by name.
    static std::shared_ptr<LiveWidget> build(const Widget& layout, const std::shared_ptr<script::Module>& parent, Runtime* runtime = nullptr);
    // A copy with copies of its children and the same script functions; in no parent yet.
    std::shared_ptr<LiveWidget> copy() const;
    void append(const std::shared_ptr<LiveWidget>& child, const LiveWidget* before = nullptr);
    void remove(const std::shared_ptr<LiveWidget>& child);
    void remove_children();
    void rename(const std::string& to);
    // Places the children of a box as its kind does (VBox, HBox, SelectBox, ...).
    void arrange();

    std::shared_ptr<script::Reference> member(script::Vm&, const script::Value& self, const std::string& name) override;

    std::shared_ptr<script::Reference> element(script::Vm& vm, const script::Value& self, const std::string& name) override;
    std::shared_ptr<script::Reference> dotted(script::Vm& vm, const script::Value& self, const std::string& name) override;
    std::string describe() const override { return "<" + data.type + " " + path() + ">"; }
    void clear() override;

    script::Value get(const std::string& property) const;
    void set(const std::string& property, const script::Value& v);          // with what the program does on a change
    void set_property(const std::string& property, const script::Value& v); // the value alone
    bool is(const char* type) const { return data.type == type; }
    bool visible() const { return !data.has("visible") || data.number("visible") != 0; }
    bool shown() const; // it and every parent visible
    bool insensitive() const { return (state & kInsensitive) != 0; }
    bool can_focus() const;
    bool is_composite() const;
    int index() const { return int(data.number("index")); }
    // A state bit on or off; a face's is its parent's too, up to the first that is no face (active 0x265FF0, focus 0x266098,
    // insensitive 0x266200).
    void set_state(int bit, bool on);
    LiveWidget* parent_widget() const { return dynamic_cast<LiveWidget*>(parent.lock().get()); }
    LiveWidget* page();    // the root window it is in
    LiveWidget* project(); // the project widget above it
    std::shared_ptr<LiveWidget> self() { return std::static_pointer_cast<LiveWidget>(shared_from_this()); }
    std::shared_ptr<LiveWidget> child(const std::string& name) const;
    Rect screen_rect() const;
    float opacity() const { return data.has("opacity") ? data.number("opacity") : 1.0f; }
};

// A face (`is_face`: the ...Face classes, unless the layout says; the bit 13 of the program's flags word).
bool is_face(const LiveWidget& w);
// Whether a widget shows in `state` by its `visible_condition` and `visible_compare` (0x265DC8): the compared bits equal the
// condition's; without a compare any bit of the condition; without either always.
bool state_shows(int state, int condition, int compare);
// The state a widget is drawn in (0x231508): a face its parent's, any other widget its own.
int drawn_state(const LiveWidget& w, int parent_state);

inline script::Value widget_value(const std::shared_ptr<LiveWidget>& w) { return w ? script::Value::of(script::Kind::Module, w) : script::Value{}; }

// The pad as the menus read it. The program turns each button into a key code (the table at
// 0x619370 of the US program: a pad mask and the code); `keysym` gives the code.
enum class Key { Up, Down, Left, Right, Accept, Cancel, Start, Select, L1, R1, L2, R2, Square, Triangle };
int keysym(Key k);
const char* key_name(Key k);
std::optional<Key> key_by_name(const std::string& name);

// One call of `main::sound` (or of its `midi` and `stream` objects) by a script: the place a
// sound player is wired in.
struct SoundCall {
    std::string object; // "sound", "sound.midi", "sound.stream"
    std::string method; // "play", "load", "startStream", ...
    std::vector<script::Value> args;
    std::string text() const; // "sound.play(ok)"
};

// What a runtime is made with.
struct RuntimeConfig {
    std::string region = "us";   // menu::MSystem::GetRegionCode()
    std::string language = "US"; // the projects' language folder
    FolderLister list;           // the disc's folders (the car database's)
    std::function<Bytes()> program; // the disc's program file (CORE.GT4): the name lists the scripts read through MCarData
    std::function<std::shared_ptr<const gt3r::CoreImage>()> program_image; // the same unpacked (Disc::program_image), read before `program`
    // The program's image by the one or the other; null without a program. Throws as they do.
    std::shared_ptr<const gt3r::CoreImage> image() const {
        if (program_image) return program_image();
        return program ? std::make_shared<const gt3r::CoreImage>(gt3r::CoreImage::decode(program())) : nullptr;
    }
    std::string data_dir;        // where the options are kept ("" : not kept)
    bool console_card = false;   // a console's memory card and its dialogs (the original's pages), instead of saves of their own (no_card.h)
    std::string new_card;        // the folder of a new game to make instead of the current one's (save/save_library.h)
};

class Runtime {
public:
    using Config = RuntimeConfig;
    explicit Runtime(FileReader read, Config config = {});
    ~Runtime();

    // Sets the world up as the program does before any project: runs the main script
    // (projects/GT4Application) and the configuration it requires, and loads the common
    // project (the cursor and the dialogs). Without it start_project gives the scripts a bare
    // world. Throws script::ScriptError when the main script cannot run.
    void boot();
    bool booted() const { return booted_; }
    // The project the configuration starts with ("boot").
    std::string first_project();

    // Loads a project (its layout as live widgets, its script) and calls its onLoad. Throws
    // FormatError / script::ScriptError when the project cannot start.
    void start_project(const std::string& name, const std::string& language = {});
    // The same from a layout already read, with no files behind it (no script, pictures or
    // text): its widgets' event functions are whatever the caller defines on them before the
    // next update. The project's first page is not opened by itself.
    std::shared_ptr<LiveWidget> start_layout(const Widget& layout);
    // One frame (1/60 s): the script that waits goes on when its wait is over, the events
    // queued are given to the scripts, the actors move.
    void update();
    // A press of a pad button: queued for the event thread.
    unsigned directions_held = 0; // what the app's directions were last frame (bits: up, down, left, right): a press of one still held is a repeat
    // `repeat`: the key is held since an earlier press of it.
    void key(Key k, bool repeat = false);
    bool key_repeated() const { return key_repeated_; } // of the key being dealt with
    // The pad as it is held now (the program's mask) and its sticks (-1 .. 1: right x, right y, left x, left y): once a
    // picture. What the scripts ask of it (getPadButtonState, getPadAnalogChannel); a button let go queues its key release.
    struct Pad {
        static constexpr u32 kUp = 1, kDown = 2, kLeft = 4, kRight = 8, kTriangle = 0x10, kCircle = 0x20, kSquare = 0x40, kCross = 0x80, kL1 = 0x100,
                             kL2 = 0x200, kR1 = 0x800, kR2 = 0x1000, kStart = 0x4000;
        u32 buttons = 0;
        float sticks[4] = {};
    };
    void set_pad(const Pad& pad);
    const Pad& pad() const { return pad_; }
    const Pad& last_pad() const { return pad_last_; }
    // The photo shoot running (its scripts' camera; photo_natives.h), and the picture the photo faces show.
    std::shared_ptr<struct PhotoShoot> photo;
    std::shared_ptr<struct PhotoPicture> photo_picture;
    // Frames until nothing runs or waits and no event is queued (at most `max_frames`); false
    // when it did not settle.
    bool settle(int max_frames = 600);
    bool idle() const;

    // After the scripts ended the project (`context.finish()`):
    bool finished() const { return finished_; }
    // the project asked for next ("" : none),
    std::string wanted_project() const;
    // or the race they asked for (`main::game.next_sequence` set to a race's).
    std::optional<RaceRequest> race_request() const;
    // A race run: what the program writes after it (records, the diary's day, the counters),
    // as GameState holds them. Answers the prize the scripts pay (the quick menus' `prize`;
    // the money itself is the scripts' incMoney).
    s64 race_finished(const RaceResult& result);
    // Starts the project asked for; false when there is none (a race was asked for, or nothing).
    bool advance();

    Project* project() { return project_.get(); }
    Project* common_project() { return common_.get(); }
    // The project a page belongs to (its pictures and text).
    Project* project_of(const LiveWidget& page);
    LiveWidget* current_page() { return pages_.empty() ? nullptr : pages_.back().get(); }
    LiveWidget* focused();
    // A championship's points for a place (0 first; the program's table 0x6A6EF0), for the race's points pages.
    s32 points_for_place(int place);
    // What the cursor points at: the focused widget, a list box's focused row (0x2B3670).
    LiveWidget* cursor_widget();
    // Gran Turismo 4 Prologue's disc (its configuration script config-prologue).
    bool is_prologue();
    // The pages shown, the lowest first.
    const std::vector<std::shared_ptr<LiveWidget>>& open_pages() const { return pages_; }
    // The page of the common project that draws the cursor, when the scripts show it.
    bool cursor_visible() const;
    script::Vm& vm() { return *vm_; }
    GameState& game() { return *game_; }
    u64 frame() const { return frame_; }
    // What happened, newest last: pages opened, sounds asked for, script errors.
    const std::deque<std::string>& log() const { return log_; }
    // Every sound call, in order (also noted in the log). `on_sound` is called with each.
    const std::vector<SoundCall>& sound_calls() const { return sounds_; }
    std::function<void(const SoundCall&)> on_sound;
    // The options as the rest of the game reads them (game_settings.h), given each time they may
    // have changed: at the end of the start, when a game is loaded, and when the scripts apply,
    // save or reset them. The app sets the menus' sound volumes from it.
    std::function<void(const GameSettings&)> on_options;
    // MMovieFace::expandAlpha(on): the movies' pictures carry their alpha beside their colors.
    bool movie_expand_alpha = false;
    void options_changed();
    // Notes every call the event thread makes and every key it handles in the log.
    bool trace_calls = false;
    // The colour and how much of it covers the page: the transition the scripts started.
    float fade_cover() const;

    // For the program's functions the scripts call (the natives):
    void sound(const std::string& object, const std::string& method, script::Args args);
    void note(std::string line);
    void open_page(const std::shared_ptr<LiveWidget>& page, bool replace);
    void close_page(const std::shared_ptr<LiveWidget>& page);
    void set_focus(LiveWidget& page, const std::shared_ptr<LiveWidget>& w);
    void finish();
    void navigate_sound(LiveWidget& page);
    void push_event(const script::Value& event);
    // A widget's script function queued as an event (run after the event being dispatched);
    // nothing when the widget has no such function.
    void queue_call(LiveWidget& w, const std::string& name, std::vector<script::Value> args);
    // A function value queued the same way (a list box's callbacks).
    void queue_function(const script::Value& callee, std::vector<script::Value> args, std::string what);
    void flush_key_events();
    void enter_event_loop();
    void leave_event_loop();
    void add_watcher(const std::shared_ptr<Object>& w);
    void remove_watcher(const Object* w);
    void track_actor(const std::shared_ptr<script::Heap>& a);
    const script::Value& context() const { return render_context_; }
    const script::Value& update_context() const { return update_context_; }
    const Config& config() const { return config_; }
    const std::string& region() const { return config_.region; }
    const FolderLister& lister() const { return config_.list; }
    const std::string& data_dir() const { return config_.data_dir; }
    const std::string& language() const { return config_.language; }
    // The project's module as a script has it (nil when none runs).
    script::Value project_value() const { return widget_value(root_widget_); }
    // The player asked for the saved games' page (the title's Save Game, Prologue's save and load items): taken once.
    bool take_saves_asked() { return std::exchange(saves_asked_, false); }
    std::string translate(const script::Value& module, const std::string& page, const std::string& key);
    std::shared_ptr<LiveWidget> common_page(const std::string& name);
    // Calls a widget's script function now, to its end (a wait in it is passed over).
    script::Value call_now(LiveWidget& w, const std::string& name, std::vector<script::Value> args);
    // The same, on the widget and then each parent until one deals with it (returns a number
    // other than 0).
    script::Value bubble_now(LiveWidget& from, const std::string& name, std::vector<script::Value> args);
    float seconds_since_input() const { return float(frame_ - last_input_frame_) / 60.0f; }
    FileReader& reader() { return read_; }
    // The options as a text file under the data folder ("gt4r-options.txt"): written when the
    // scripts apply or save them and when a project ends; read when the runtime is made.
    void save_options();
    void load_options();
    // Something the natives own for as long as the runtime lives.
    void keep_alive(std::shared_ptr<void> p) { kept_.push_back(std::move(p)); }

private:
    int prologue_ = -1; // is_prologue(): not yet asked
    struct Running {
        std::shared_ptr<script::Task> task;
        std::function<void(const script::Value&)> done;
        std::string what;
        bool looping = false; // inside enterEventLoop: events go on being handled above it
    };
    struct Event {
        enum class Kind { Key, Script, Call } kind = Kind::Script;
        Key key = Key::Accept;
        bool release = false;                      // a Key let go
        bool repeat = false;                       // a Key still held
        script::Value value;                       // an MScriptEvent / MFunctionEvent
        script::Value callee;                      // a Call's
        std::vector<script::Value> args;
        std::string what;
    };
    void install_natives();
    void mark_focus(LiveWidget& page);
    void queue_focus_events();
    void pump();
    void run(const script::Value& callee, std::vector<script::Value> args, std::string what, std::function<void(const script::Value&)> done = {});
    // Calls `name` on each widget of a chain until one deals with it, then `done(dealt)`.
    void run_chain(std::vector<std::shared_ptr<LiveWidget>> chain, size_t at, std::string name, std::vector<script::Value> args,
                   std::function<void(bool)> done);
    void dispatch(Event& e);
    void dispatch_key(Key pressed, const std::shared_ptr<LiveWidget>& to = nullptr);
    void dispatch_release(Key k, const std::shared_ptr<LiveWidget>& to = nullptr);
    bool keys_taken();
    Pad pad_, pad_last_;
    void navigate(Key k);
    void unload_project();
    void step_frame_objects();
    std::shared_ptr<Project> load_project_files(const std::string& name, const std::string& language);
    void focus_first(LiveWidget& page);

    FileReader read_;
    Config config_;
    bool saves_asked_ = false, autosave_due_ = false;
    std::unique_ptr<GameState> game_;
    std::unique_ptr<script::Vm> vm_;
    bool booted_ = false;
    std::shared_ptr<Project> project_, common_;
    std::shared_ptr<LiveWidget> root_widget_, common_widget_; // the projects'
    std::string project_name_;
    std::vector<std::shared_ptr<LiveWidget>> pages_;
    script::Value render_context_, update_context_;
    std::deque<Event> events_;
    std::vector<Running> running_; // the event thread's calls: the last is the one running
    std::vector<std::weak_ptr<script::Heap>> actors_;
    std::vector<std::shared_ptr<Object>> watchers_;
    std::deque<std::shared_ptr<Object>> due_watchers_;
    u64 watchers_turn_ = ~u64(0); // the frame whose loop turn has stepped the watchers
    std::deque<std::string> log_;
    std::vector<SoundCall> sounds_;
    u64 frame_ = 0, last_input_frame_ = 0;
    bool key_repeated_ = false;
    std::weak_ptr<LiveWidget> entered_; // the widget the cursor last went into (its onEnter)
    bool finished_ = false;
    std::string sequence_ = "boot"; // the part of the game running: "boot", then "menu"
    int pumping_ = 0;
    std::vector<std::shared_ptr<void>> kept_;
};

} // namespace gt4r::menu
