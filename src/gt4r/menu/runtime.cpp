#include "gt4r/menu/runtime.h"

#include "gt4r/menu/runtime_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace gt4r::menu {

using script::Args;
using script::Kind;
using script::Value;
using script::Vm;

namespace {

constexpr size_t kMaxLog = 400;
constexpr int kMaxStepsPerFrame = 256;       // events and calls the event thread goes through in a frame
constexpr u64 kCallBudget = 40'000'000;      // instructions a script may run before it must wait or end
constexpr const char* kCommonProject = "cursor"; // the cursor and the dialogs every project uses

// The program's key table (US 1.01: 0x6192A0, pairs of pad mask and code). The codes of the
// buttons other than the directions, accept, cancel and start are named for what the scripts
// do with them. ❓ Which pad button gives 0x100, 0x200, 0x800, 0x1000 (taken as L1, R1, L2, R2),
// 0x40000 and 0x80000.
struct KeyCode {
    Key key;
    const char* name;
    int sym;
};
const KeyCode kKeys[] = {
    {Key::Up, "up", 0xFF52},       {Key::Down, "down", 0xFF54},     {Key::Left, "left", 0xFF51},   {Key::Right, "right", 0xFF53},
    {Key::Accept, "accept", 0xFF0D}, {Key::Cancel, "cancel", 0xFF1B}, {Key::Start, "start", 0xFF8D}, {Key::Select, "select", 0xFF63},
    {Key::L1, "l1", 0xFFC8},       {Key::R1, "r1", 0xFFC9},         {Key::L2, "l2", 0xFFD2},       {Key::R2, "r2", 0xFFD3},
    {Key::Square, "square", 0xFFFF}, {Key::Triangle, "triangle", 0xFF13},
};

} // namespace

int keysym(Key k) {
    for (const KeyCode& c : kKeys)
        if (c.key == k) return c.sym;
    return 0;
}

const char* key_name(Key k) {
    for (const KeyCode& c : kKeys)
        if (c.key == k) return c.name;
    return "?";
}

std::optional<Key> key_by_name(const std::string& name) {
    for (const KeyCode& c : kKeys)
        if (name == c.name) return c.key;
    if (name == "cross") return Key::Accept;
    if (name == "triangle") return Key::Cancel;
    return std::nullopt;
}

std::string SoundCall::text() const {
    std::string s = object + "." + method + "(";
    for (size_t i = 0; i < args.size(); ++i) s += (i ? ", " : "") + args[i].to_string();
    return s + ")";
}

// ---------------------------------------------------------------------------------------------
// The world
// ---------------------------------------------------------------------------------------------

Runtime::Runtime(FileReader read, Config config) : read_(std::move(read)), config_(std::move(config)) {
    game_ = std::make_unique<GameState>();
    game_->option_defaults(config_.region);
    load_options();
    vm_ = std::make_unique<Vm>([this](const std::string& name) -> std::optional<Bytes> {
        std::string path = name;
        while (!path.empty() && path[0] == '/') path.erase(0, 1);
        path += ".adc";
        if (std::optional<Bytes> file = read_(path)) return file;
        // The scripts name some files in another case than the disc has them
        // ("us_carlist" for projects/boot/US_carlist.adc).
        if (!config_.list) return std::nullopt;
        const size_t slash = path.rfind('/');
        const std::string folder = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
        auto lower = [](std::string s) {
            for (char& c : s) c = char(std::tolower(u8(c)));
            return s;
        };
        const std::string wanted = lower(path.substr(folder.size()));
        for (const std::string& entry : config_.list(folder))
            if (lower(entry) == wanted) return read_(folder + entry);
        return std::nullopt;
    });
    vm_->stub_natives = true; // what is not written stands in and is counted
    install_natives();
}

Runtime::~Runtime() {
    // The widgets are members of the interpreter's root module: it lets go of them.
    running_.clear();
    events_.clear();
    watchers_.clear();
    pages_.clear();
    render_context_ = {}, update_context_ = {};
    vm_.reset();
}

void Runtime::save_options() {
    if (config_.data_dir.empty()) return;
    const std::string file = config_.data_dir + "/gt4r-options.txt";
    if (std::FILE* f = std::fopen(file.c_str(), "wb")) {
        const std::string t = game_->options_text();
        std::fwrite(t.data(), 1, t.size(), f);
        std::fclose(f);
    } else note("the options could not be kept in " + file);
}

void Runtime::load_options() {
    if (config_.data_dir.empty()) return;
    std::FILE* f = std::fopen((config_.data_dir + "/gt4r-options.txt").c_str(), "rb");
    if (!f) return;
    std::string t;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) t.append(buf, n);
    std::fclose(f);
    game_->options_from_text(t);
}

void Runtime::note(std::string line) {
    log_.push_back(std::move(line));
    while (log_.size() > kMaxLog) log_.pop_front();
}

void Runtime::sound(const std::string& object, const std::string& method, Args args) {
    SoundCall call{object, method, std::vector<Value>(args.begin(), args.end())};
    note(call.text());
    if (on_sound) on_sound(call);
    sounds_.push_back(std::move(call));
    if (sounds_.size() > 4096) sounds_.erase(sounds_.begin(), sounds_.begin() + 2048);
}

void Runtime::install_natives() {
    Vm& vm = *vm_;
    // The program's own modules, as the main script asks for them (`require "menu"`,
    // `require "gt4app"` inside `module menu`).
    vm.add_native_module("menu", [this](Vm&, script::Module& into) {
        Runtime* r = this;
        // The events a script queues: MScriptEvent(context, module, function name, arguments)
        // and MFunctionEvent(function, arguments...).
        into.define("MScriptEvent", Vm::native("MScriptEvent", [](Vm&, const Value&, Args a) {
            auto e = std::make_shared<Object>("MScriptEvent");
            e->set("context", a.empty() ? Value{} : a[0]);
            e->set("module", a.size() > 1 ? a[1] : Value{});
            e->set("function", a.size() > 2 ? a[2] : Value{});
            e->set("args", a.size() > 3 ? a[3] : Value{});
            return object_value(e);
        }));
        into.define("MFunctionEvent", Vm::native("MFunctionEvent", [](Vm&, const Value&, Args a) {
            auto e = std::make_shared<Object>("MFunctionEvent");
            e->set("call", a.empty() ? Value{} : a[0]);
            e->set("args", Value::array(std::vector<Value>(a.begin() + (a.empty() ? 0 : 1), a.end())));
            return object_value(e);
        }));
        // MKeyEvent(keysym): an event a script makes itself.
        into.define("MKeyEvent", Vm::native("MKeyEvent", [](Vm&, const Value&, Args a) {
            auto e = std::make_shared<Object>("MKeyEvent");
            e->set("keysym", a.empty() ? Value::integer(0) : a[0]);
            e->set("state", Value::integer(0)), e->set("port", Value::integer(0)), e->set("widget", Value{});
            return object_value(e);
        }));
        // The manager the widget classes are registered with (script/MenuClassDefine).
        into.define("MManager", Vm::native("MManager", [r](Vm&, const Value&, Args) {
            auto m = std::make_shared<Object>("MManager");
            for (const char* nothing : {"registerClass", "registerPrototype", "registerPrototypes", "finishProject"})
                m->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
            for (const char* none : {"loadPrototype", "loadWidget", "getPrototype"}) m->method(none, [](Vm&, const Value&, Args) { return Value{}; });
            m->method("getUpdateContext", [r](Vm&, const Value&, Args) { return r->update_context(); });
            return object_value(m);
        }));
        // The widget classes themselves, registered by name.
        for (const char* cls : {"MBlurFace", "MButton", "MCarFace", "MColorChipFace", "MColorFace", "MColorWindow", "MComposite", "MEyetoyFace", "MFBox",
                                "MFlashFace", "MFrameImageFace", "MGraphFace", "MHBox", "MImageFace", "MInputNumberFace", "MInputTextFace", "MListBox",
                                "MLoggerFace", "MMBox", "MModelFace", "MOptionMenu", "MPhotoMapWindow", "MPhotoRenderFace", "MPhotoViewFace",
                                "MProgressFace", "MProject", "MRaceCourseMapFace", "MRootWindow", "MScaleBar", "MSceneViewFace", "MScrollArrow",
                                "MScrollBase", "MScrollBox", "MScrollPinch", "MScrollWindow", "MScrollable", "MSelectBar", "MSelectBox",
                                "MSlideShowFace", "MSliderBar", "MTextBoxFace", "MTextFace", "MToolTipFace", "MVBox", "MVirtualFace", "MWidget"}) {
            auto m = std::make_shared<script::Module>();
            m->name = cls, m->is_class = true;
            into.define(cls, Value::of(Kind::Module, m));
        }
        // MMovieFace's own functions: the movie player is not part of the menus here.
        {
            auto movie = std::make_shared<script::Module>();
            movie->name = "MMovieFace", movie->is_class = true;
            for (const char* f : {"initialize", "terminate", "preload", "loadIpic", "expandAlpha", "asyncSound", "setPause"})
                movie->define(f, Vm::native(std::string("MMovieFace::") + f, [](Vm&, const Value&, Args) { return Value{}; }));
            into.define("MMovieFace", Value::of(Kind::Module, movie));
        }
        install_actor_natives(*this, into);
        install_game_natives(*this, into);
        into.stubbed = true; // any other name of the program's is a stand-in
    });
    vm.add_native_module("gt4app", [](Vm&, script::Module&) {});

    // The update context a project's onLoad is given, and the render context every page's
    // event function is given (the program's MUpdateContext at 0x24F7A0, MRenderContext at
    // 0x22D228: the member names are its tables').
    // One object stands for both here: Prologue's scripts, written before the two were told
    // apart, open their pages and set the transition on the context onLoad is given.
    auto render = std::make_shared<Object>("MRenderContext");
    const std::shared_ptr<Object> update = render;
    update_context_ = object_value(update);
    render_context_ = object_value(render);
    update->method("createRenderContext", [](Vm&, const Value&, Args) { return Value{}; });
    update->method("getRenderContext", [this](Vm&, const Value&, Args) { return render_context_; });
    update->method("enterEventLoop", [this](Vm&, const Value&, Args) {
        enter_event_loop();
        return Value{};
    });
    update->method("leaveEventLoop", [this](Vm&, const Value&, Args) {
        leave_event_loop();
        return Value{};
    });
    update->set("cursor_speed_ratio", Value::number(1));
    update->set("repeat_start", Value::integer(20));
    update->set("repeat_speed", Value::integer(4));
    for (const char* zero : {"getPadButtonState", "getLastPadButtonState", "getPadAnalogChannel", "getLastPadAnalogChannel"})
        update->method(zero, [](Vm&, const Value&, Args) { return Value::integer(0); });
    for (const char* nothing : {"loadGpbFromMC", "setStartPage"}) update->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
    update->method("existGpbBinary", [](Vm&, const Value&, Args) { return Value::integer(1); });
    update->method("getStartPage", [](Vm&, const Value&, Args) { return Value{}; });
    auto translate_fn = [this](Vm&, const Value&, Args a) {
        // (the page's module, key) or (a module, a page of the text, key)
        if (a.size() < 2) return Value{};
        return Value::string(a.size() > 2 ? translate(a[0], a[1].to_string(), a[2].to_string()) : translate(a[0], {}, a[1].to_string()));
    };
    update->method("translate", translate_fn);
    render->method("translate", translate_fn);
    render->method("getUpdateContext", [this](Vm&, const Value&, Args) { return update_context_; });

    auto page_arg = [](Args a) { return a.empty() ? nullptr : a[0].as<LiveWidget>(); };
    // startPage(page): the pages shown close and the page opens. While a transition covers the
    // page (panOut, panOutIn) the script waits until it is covered.
    render->method("startPage", [this, page_arg](Vm& v, const Value&, Args a) {
        const std::shared_ptr<LiveWidget> p = page_arg(a);
        if (!p) return Value{};
        const Value transition = v.attribute(render_context_, "transition");
        if (transition_phase(transition) == 0 && v.can_wait()) {
            // Covered first, then the change.
            std::weak_ptr<LiveWidget> weak = p;
            v.wait([transition] { return transition_phase(transition) != 0; }, [this, weak](Vm&) {
                if (auto page = weak.lock()) open_page(page, true);
            });
            return Value{};
        }
        open_page(p, true);
        return Value{};
    });
    render->method("pushPage", [this, page_arg](Vm&, const Value&, Args a) {
        if (auto p = page_arg(a)) open_page(p, false);
        return Value{};
    });
    render->method("closePage", [this, page_arg](Vm&, const Value&, Args a) {
        const std::shared_ptr<LiveWidget> p = page_arg(a);
        if (p) close_page(p);
        else if (!pages_.empty()) close_page(pages_.back());
        return Value{};
    });
    render->method("getCurrentPage", [this](Vm&, const Value&, Args) { return widget_value(pages_.empty() ? nullptr : pages_.back()); });
    render->method("finish", [this](Vm&, const Value&, Args) {
        finish();
        return Value{};
    });
    // sync(seconds): the script waits that long while the page goes on drawing; without an
    // argument, one frame (the program's 0x24F748: seconds x 60 frames).
    render->method("sync", [this](Vm& v, const Value&, Args a) {
        const int frames = a.empty() ? 1 : std::max(int(arg_number(a, 0) * 60.0f), 1);
        const u64 until = frame_ + u64(frames);
        v.wait([this, until] { return frame_ >= until; });
        return Value{};
    });
    render->method("pushEvent", [this](Vm&, const Value&, Args a) {
        if (!a.empty()) push_event(a[0]);
        return Value{};
    });
    render->method("flushKeyEvent", [this](Vm&, const Value&, Args) {
        flush_key_events();
        return Value{};
    });
    render->method("flushEvent", [this](Vm&, const Value&, Args) {
        flush_key_events();
        return Value{};
    });
    render->method("filterEvent", [](Vm&, const Value&, Args) { return Value{}; });
    render->method("getCommonPage", [this](Vm&, const Value&, Args a) { return widget_value(common_page(arg_text(a, 0))); });
    render->method("getCursorProject", [this](Vm&, const Value&, Args) { return widget_value(common_widget_); });
    render->method("getPrelightWidget", [this](Vm&, const Value&, Args) { return widget_value(focused() ? focused()->self() : nullptr); });
    // loadGpb(folder, name): more pictures for the pages shown (a maker's cars); unloadGpb(name).
    auto load_gpb = [this](Vm&, const Value&, Args a) {
        const bool found = project_ && project_->load_pack(arg_text(a, 0), arg_text(a, 1));
        if (!found) note("no pictures " + arg_text(a, 0) + " " + arg_text(a, 1));
        return Value::integer(found);
    };
    auto unload_gpb = [this](Vm&, const Value&, Args a) {
        if (project_) project_->unload_pack(arg_text(a, 0));
        return Value{};
    };
    render->method("loadGpb", load_gpb), render->method("unloadGpb", unload_gpb);
    update->method("loadGpb", load_gpb), update->method("unloadGpb", unload_gpb);
    for (const char* nothing : {"captureScreen", "releaseScreen", "shotScreen", "openOSKeyboard", "closeOSKeyboard", "setMousePositionOnFocus"})
        render->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
    render->method("existGpbBinary", [](Vm&, const Value&, Args) { return Value::integer(1); });
    render->set("enable", Value::integer(1));
    render->set("event_mask", Value::integer(0x7FFFFFFF));
    render->set("clear", Value::integer(1));
    render->set("pause", Value::integer(0));
    render->set("transition", Value{});
    render->set("opacity", Value::number(1));
    render->set("cursor", Value{});
    render->set("cursor_visible", Value::integer(1));
    render->set("cursor_speed_ratio", Value::number(1));
    render->set("follow_mode", Value::integer(0));
    render->set("grab_widget", Value{});
    render->set("popup_widget", Value{});
    render->property("time_after_last_input", [this] { return Value::number(seconds_since_input()); });
}

void Runtime::boot() {
    if (booted_) return;
    booted_ = true;
    vm_->instructions = 0, vm_->budget = kCallBudget * 4;
    if (read_("projects/GT4Application.adc")) {
        try {
            vm_->require("/projects/GT4Application");
        } catch (const script::ScriptError& e) {
            note(std::string("the main script stopped: ") + e.what());
        } catch (const FormatError& e) {
            note(std::string("the main script does not read: ") + e.what());
        }
    }
    // Without a main script (or one that stopped early), the objects it makes are made here.
    if (!vm_->lookup("game").as<Object>() || !vm_->lookup("sound").as<Object>()) install_fallback_world(*this);
    // The common project: its pages are there for every project (getCommonPage).
    if (read_(std::string("projects/") + kCommonProject + "/" + kCommonProject + ".mproject")) try {
        common_ = load_project_files(kCommonProject, config_.language);
        common_widget_ = LiveWidget::build(common_->layout(), vm_->root(), this);
        vm_->instructions = 0, vm_->budget = kCallBudget;
        vm_->require(std::string("/projects/") + kCommonProject + "/" + kCommonProject);
        std::function<void(LiveWidget&)> load = [&](LiveWidget& w) {
            for (auto& c : w.children) load(*c);
            call_now(w, "onLoad", {update_context_});
        };
        load(*common_widget_);
    } catch (const std::exception& e) {
        note(std::string("no common project: ") + e.what());
        common_.reset(), common_widget_.reset();
    }
    // The program's start ends with the configuration's last_strap (0x108BF8 -> 0x10B268): the
    // zone, the credits a new game has (10,000), the music lists.
    // Credits a caller set before the start (a stand-in for a saved game's) are kept.
    const s64 credits = game_->money;
    const Value strap = vm_->lookup("last_strap");
    if (strap.p && strap.p->callable()) try {
            vm_->instructions = 0, vm_->budget = kCallBudget;
            vm_->call(strap, {});
        } catch (const script::ScriptError& e) {
            note(std::string("last_strap stopped: ") + e.what());
        }
    if (credits) game_->money = credits;
}

std::string Runtime::first_project() {
    boot();
    const Value v = vm_->lookup("StartProject");
    return v.kind == Kind::String && !v.as_string().empty() ? v.as_string() : std::string("boot");
}

std::shared_ptr<Project> Runtime::load_project_files(const std::string& name, const std::string& language) { return std::make_shared<Project>(read_, name, language); }

Project* Runtime::project_of(const LiveWidget& page) {
    for (const LiveWidget* w = &page; w; w = w->parent_widget())
        if (w == common_widget_.get()) return common_.get();
    return project_.get();
}

std::shared_ptr<LiveWidget> Runtime::common_page(const std::string& name) {
    if (common_widget_)
        if (auto p = common_widget_->child(name)) return p;
    return root_widget_ ? root_widget_->child(name) : nullptr;
}

bool Runtime::cursor_visible() const { return vm_->attribute(render_context_, "cursor_visible").truthy(); }

float Runtime::fade_cover() const { return transition_cover(vm_->attribute(render_context_, "transition")); }

std::string Runtime::translate(const Value& module, const std::string& page, const std::string& key) {
    const auto m = module.as<script::Module>();
    const auto live = module.as<LiveWidget>();
    Project* project = live && live->page() ? project_of(*live->page()) : live && live.get() == common_widget_.get() ? common_.get() : project_.get();
    for (Project* p : {project, project_.get(), common_.get()}) {
        if (!p) continue;
        if (!page.empty()) {
            if (const std::string* t = p->text().find(page, key)) return *t;
            if (m)
                if (const std::string* t = p->text().find(m->path() + "::" + page, key)) return *t;
        } else if (m) {
            if (const std::string* t = p->text().find(m->path(), key)) return *t;
        }
    }
    return key;
}

// ---------------------------------------------------------------------------------------------
// Projects
// ---------------------------------------------------------------------------------------------

void Runtime::unload_project() {
    running_.clear();
    events_.clear();
    pages_.clear();
    watchers_.clear();
    due_watchers_.clear();
    if (root_widget_) vm_->root()->members.erase(root_widget_->name), root_widget_->clear();
    root_widget_.reset();
    project_.reset();
    finished_ = false;
}

void Runtime::start_project(const std::string& name, const std::string& language) {
    boot();
    unload_project();
    if (!language.empty()) config_.language = language;
    project_ = load_project_files(name, config_.language);
    project_name_ = name;
    root_widget_ = LiveWidget::build(project_->layout(), vm_->root(), this);
    note("project " + name + " (" + root_widget_->name + ")");
    vm_->instructions = 0, vm_->budget = kCallBudget;
    vm_->require("/projects/" + name + "/" + name);
    // A new transition and cursor for each project: the scripts set theirs in onLoad.
    if (auto render = render_context_.as<Object>()) render->set("transition", Value{}), render->set("cursor_visible", Value::integer(1));
    // onLoad on every widget that has one, the children before their parent, the project
    // last (the program's 0x207010), each given the update context.
    std::function<void(const std::shared_ptr<LiveWidget>&)> load = [&](const std::shared_ptr<LiveWidget>& w) {
        for (auto& c : w->children) load(c);
        const auto it = w->members.find("onLoad");
        if (it == w->members.end() || it->second->value.kind != Kind::Function) return;
        Event e;
        e.kind = Event::Kind::Call, e.callee = it->second->value, e.args = {update_context_}, e.what = w->path() + "::onLoad";
        events_.push_back(std::move(e));
    };
    load(root_widget_);
    // A project whose onLoad opens no page starts on its first one (Prologue's projects).
    // ❓ Which page the program takes (its getStartPage) is not read.
    Event first;
    first.kind = Event::Kind::Call, first.what = "the first page";
    first.callee = Vm::native("first page", [this](Vm&, const Value&, Args) {
        if (pages_.empty() && !finished_ && root_widget_)
            for (const auto& page : root_widget_->children)
                if (page->is("RootWindow")) {
                    open_page(page, true);
                    break;
                }
        return Value{};
    });
    events_.push_back(std::move(first));
    pump();
}

std::shared_ptr<LiveWidget> Runtime::start_layout(const Widget& layout) {
    boot();
    unload_project();
    project_name_ = layout.name();
    root_widget_ = LiveWidget::build(layout, vm_->root(), this);
    note("project " + root_widget_->name);
    if (auto render = render_context_.as<Object>()) render->set("transition", Value{}), render->set("cursor_visible", Value::integer(1));
    return root_widget_;
}

std::string Runtime::wanted_project() const { return game_->next_menu_project; }

std::optional<RaceRequest> Runtime::race_request() const {
    const std::string& next = game_->next_sequence;
    if (!finished_ || next.empty() || next == "menu" || next == "boot") return std::nullopt;
    RaceRequest r = game_->race;
    r.sequence = next;
    return r;
}

bool Runtime::advance() {
    if (!finished_) return false;
    if (race_request()) return false;
    const std::string next = game_->next_menu_project;
    if (next.empty()) return false;
    // What the scripts read of where they came from.
    game_->last_menu_project = project_name_;
    game_->last_menu_arg = game_->next_menu_arg;
    if (!game_->next_sequence.empty()) game_->last_sequence = sequence_, sequence_ = game_->next_sequence;
    else if (sequence_ == "boot" && next != "boot") game_->last_sequence = sequence_, sequence_ = "menu";
    game_->next_sequence.clear();
    game_->next_menu_project.clear();
    const std::string arg = game_->next_menu_arg;
    start_project(next);
    game_->next_menu_arg.clear();
    (void)arg;
    return true;
}

void Runtime::finish() {
    if (finished_) return;
    // The pages close; the project's widgets hear onUnload, the children before their parent.
    while (!pages_.empty()) close_page(pages_.back());
    if (root_widget_) {
        std::function<void(LiveWidget&)> unload = [&](LiveWidget& w) {
            for (auto& c : w.children) unload(*c);
            call_now(w, "onUnload", {update_context_});
        };
        unload(*root_widget_);
    }
    watchers_.clear();
    events_.clear();
    finished_ = true;
    save_options();
    const std::optional<RaceRequest> race = race_request();
    note(race ? "the project finished; the scripts ask for " + race->sequence + ": " + race->car + " at " + race->course
              : "the project finished; next: " + (wanted_project().empty() ? std::string("(none)") : wanted_project()));
}

// ---------------------------------------------------------------------------------------------
// The event thread
// ---------------------------------------------------------------------------------------------

void Runtime::run(const Value& callee, std::vector<Value> args, std::string what, std::function<void(const Value&)> done) {
    vm_->instructions = 0, vm_->budget = kCallBudget;
    if (trace_calls) note("call " + what);
    Running r;
    r.what = std::move(what), r.done = std::move(done);
    running_.push_back(std::move(r));
    const size_t at = running_.size() - 1;
    std::shared_ptr<script::Task> task = vm_->start(callee, args);
    if (at < running_.size()) running_[at].task = std::move(task);
}

void Runtime::run_chain(std::vector<std::shared_ptr<LiveWidget>> chain, size_t at, std::string name, std::vector<Value> args, std::function<void(bool)> done) {
    for (; at < chain.size(); ++at) {
        const auto it = chain[at]->members.find(name);
        if (it == chain[at]->members.end() || it->second->value.kind != Kind::Function) continue;
        const Value callee = it->second->value;
        const std::string what = chain[at]->path() + "::" + name;
        run(callee, args, what, [this, chain, at, name, args, done](const Value& result) mutable {
            // A number other than 0 ends it (the program's STOP 1 and FILTER 2).
            if (result.is_number() && result.as_float() != 0) {
                if (done) done(true);
            } else run_chain(std::move(chain), at + 1, std::move(name), std::move(args), std::move(done));
        });
        return;
    }
    if (done) done(false);
}

void Runtime::enter_event_loop() {
    if (running_.empty() || !vm_->can_wait()) return;
    Running* top = &running_.back();
    top->looping = true;
    const size_t index = running_.size() - 1;
    vm_->wait([this, index] { return index >= running_.size() || !running_[index].looping; });
}

void Runtime::leave_event_loop() {
    for (auto it = running_.rbegin(); it != running_.rend(); ++it)
        if (it->looping) {
            it->looping = false;
            return;
        }
}

void Runtime::push_event(const Value& event) {
    Event e;
    e.kind = Event::Kind::Script, e.value = event;
    events_.push_back(std::move(e));
}

void Runtime::flush_key_events() {
    events_.erase(std::remove_if(events_.begin(), events_.end(), [](const Event& e) { return e.kind == Event::Kind::Key; }), events_.end());
}

void Runtime::add_watcher(const std::shared_ptr<Object>& w) {
    if (std::find(watchers_.begin(), watchers_.end(), w) == watchers_.end()) watchers_.push_back(w);
}

void Runtime::remove_watcher(const Object* w) {
    watchers_.erase(std::remove_if(watchers_.begin(), watchers_.end(), [w](const auto& p) { return p.get() == w; }), watchers_.end());
}

void Runtime::track_actor(const std::shared_ptr<script::Heap>& a) {
    for (const auto& w : actors_)
        if (w.lock() == a) return;
    actors_.push_back(a);
}

void Runtime::dispatch(Event& e) {
    if (trace_calls && e.kind == Event::Kind::Key) note(std::string("key ") + key_name(e.key));
    if (e.kind == Event::Kind::Key) return dispatch_key(e.key);
    if (e.kind == Event::Kind::Call) return run(e.callee, std::move(e.args), std::move(e.what));
    const auto object = e.value.as<Object>();
    if (!object) return;
    std::vector<Value> args;
    const Value given = object->get("args");
    Value callee = object->get("call");
    std::string what = "an event's function";
    if (callee.is_nil()) { // MScriptEvent: module.function(context, arguments...)
        const std::string name = object->get("function").to_string();
        callee = vm_->attribute(object->get("module"), name);
        args.push_back(render_context_);
        what = name;
    }
    if (const std::vector<Value>* items = given.items()) args.insert(args.end(), items->begin(), items->end());
    if (callee.p && callee.p->callable()) run(callee, std::move(args), std::move(what));
}

void Runtime::pump() {
    if (pumping_) return;
    struct Guard {
        int& n;
        explicit Guard(int& v) : n(v) { ++n; }
        ~Guard() { --n; }
    } guard(pumping_);
    for (int steps = 0; steps < kMaxStepsPerFrame; ++steps) {
        if (!running_.empty()) {
            Running& top = running_.back();
            if (!top.task) { // started inside its own call and left there
                running_.pop_back();
                continue;
            }
            script::Task& task = *top.task;
            if (task.state == script::Task::State::Waiting) {
                if (!task.until || task.until()) {
                    vm_->instructions = 0, vm_->budget = kCallBudget;
                    vm_->resume(task);
                    continue;
                }
                if (!top.looping) break; // the script waits: nothing else happens on its thread
                // In its own event loop: the events go on below.
            } else {
                Running ended = std::move(running_.back());
                running_.pop_back();
                if (ended.task->state == script::Task::State::Failed) note("script error in " + ended.what + ": " + ended.task->error);
                else if (ended.done) ended.done(ended.task->result);
                continue;
            }
        }
        if (finished_) break;
        if (events_.empty()) {
            if (due_watchers_.empty()) break;
            const std::shared_ptr<Object> w = due_watchers_.front();
            due_watchers_.pop_front();
            Value function, context;
            if (std::find(watchers_.begin(), watchers_.end(), w) == watchers_.end()) continue; // removed meanwhile
            if (!watcher_call(*w, function, context) || !function.p || !function.p->callable()) continue;
            watcher_called(*w);
            run(function, {context}, "a watcher's function");
            continue;
        }
        Event e = std::move(events_.front());
        events_.pop_front();
        dispatch(e);
    }
}

bool Runtime::idle() const {
    if (!events_.empty() || !due_watchers_.empty()) return false;
    for (const Running& r : running_)
        if (!r.looping) return false;
    return true;
}

void Runtime::step_frame_objects() {
    for (size_t i = 0; i < actors_.size();) {
        const std::shared_ptr<script::Heap> held = actors_[i].lock();
        if (!held) {
            actors_.erase(actors_.begin() + std::ptrdiff_t(i));
            continue;
        }
        step_actor(*held);
        ++i;
    }
    step_transition(vm_->attribute(render_context_, "transition"));
    for (const auto& page : pages_) {
        step_widgets(*this, *page);
        pack_page(*page);
    }
    // The watchers whose time has come: their functions are called by the event thread when it
    // has nothing else to do (after the events queued, never inside a call that waits).
    for (const auto& w : watchers_)
        if (step_watcher(*w)) due_watchers_.push_back(w);
}

void Runtime::update() {
    ++frame_;
    step_frame_objects();
    pump();
}

bool Runtime::settle(int max_frames) {
    int calm = 0;
    for (int i = 0; i < max_frames; ++i) {
        update();
        if (finished_) return true;
        const int phase = transition_phase(vm_->attribute(render_context_, "transition"));
        calm = idle() && phase < 0 ? calm + 1 : 0;
        if (calm >= 2) return true; // an event queued by the last frame's has had its turn
    }
    return idle();
}

// ---------------------------------------------------------------------------------------------
// Pages and the focus
// ---------------------------------------------------------------------------------------------

Value Runtime::call_now(LiveWidget& w, const std::string& name, std::vector<Value> args) {
    const auto it = w.members.find(name);
    if (it == w.members.end() || it->second->value.kind != Kind::Function) return {};
    try {
        return vm_->call(it->second->value, args);
    } catch (const script::ScriptError& e) {
        note("script error in " + w.path() + "::" + name + ": " + e.what());
        return {};
    }
}

Value Runtime::bubble_now(LiveWidget& from, const std::string& name, std::vector<Value> args) {
    for (LiveWidget* w = &from; w; w = w->parent_widget()) {
        const Value r = call_now(*w, name, args);
        if (r.is_number() && r.as_float() != 0) return r;
    }
    return {};
}

LiveWidget* Runtime::focused() {
    LiveWidget* page = current_page();
    return page ? page->focus.lock().get() : nullptr;
}

void Runtime::open_page(const std::shared_ptr<LiveWidget>& page, bool replace) {
    if (replace)
        while (!pages_.empty()) close_page(pages_.back());
    if (std::find(pages_.begin(), pages_.end(), page) != pages_.end()) return;
    pages_.push_back(page);
    note("page " + page->name);
    // A page that names its music: the same as the scripts' keepMusic of it.
    if (page->data.has("bgm")) {
        const Value group = Value::string(page->data.text("bgm"));
        sound("sound", "keepMusic", Args(&group, 1));
    }
    // onInitialize(context), then the focus goes to the page's first widget when the script
    // gave it none. The function is called in the script's own call, so it can wait.
    std::weak_ptr<LiveWidget> weak = page;
    auto after = [this, weak](Vm&, const Value&) {
        if (auto p = weak.lock())
            if (std::find(pages_.begin(), pages_.end(), p) != pages_.end()) focus_first(*p);
        return Value{};
    };
    const auto it = page->members.find("onInitialize");
    if (it != page->members.end() && it->second->value.kind == Kind::Function) {
        try {
            vm_->then_call(it->second->value, {render_context_}, after);
        } catch (const script::ScriptError& e) {
            note("script error in " + page->path() + "::onInitialize: " + e.what());
        }
    } else after(*vm_, {});
}

void Runtime::close_page(const std::shared_ptr<LiveWidget>& page) {
    const auto it = std::find(pages_.begin(), pages_.end(), page);
    if (it == pages_.end()) return;
    call_now(*page, "onFinalize", {render_context_});
    const auto again = std::find(pages_.begin(), pages_.end(), page);
    if (again != pages_.end()) pages_.erase(again);
    note("page " + page->name + " closed");
}

void Runtime::focus_first(LiveWidget& page) {
    if (page.focus.lock()) return;
    if (const std::shared_ptr<LiveWidget> w = first_focus(page)) set_focus(page, w);
}

void Runtime::set_focus(LiveWidget& page, const std::shared_ptr<LiveWidget>& w) {
    const std::shared_ptr<LiveWidget> before = page.focus.lock();
    if (before == w) return;
    if (before) {
        before->state &= ~(kFocus | kPrelight);
        auto event = std::make_shared<Object>("MFocusLeaveEvent");
        event->set("widget", widget_value(before)), event->set("context", render_context_);
        bubble_now(*before, "onFocusLeave", {render_context_, object_value(event)});
    }
    page.focus = w;
    if (!w) return;
    w->state |= kFocus | kPrelight; // the faces of a focused widget test either bit
    scroll_into_view(*w);
    auto event = std::make_shared<Object>("MFocusEnterEvent");
    event->set("widget", widget_value(w)), event->set("context", render_context_);
    bubble_now(*w, "onFocusEnter", {render_context_, object_value(event)});
}

void Runtime::key(Key k) {
    Event e;
    e.kind = Event::Kind::Key, e.key = k;
    events_.push_back(std::move(e));
    last_input_frame_ = frame_;
}

// A key press goes to the focused widget and then to each parent up to the page until one
// deals with it (the program's 0x235A20). A widget's class sees it first (a select box moves
// its index, 0x2DAD18), then its script (onKeyPress(context, event)). The page, last, turns
// accept and cancel into their own events and the directions into a move of the focus
// (0x235680).
void Runtime::dispatch_key(Key k) {
    const std::shared_ptr<LiveWidget> page = pages_.empty() ? nullptr : pages_.back();
    if (!page) return;
    const std::shared_ptr<LiveWidget> focus = page->focus.lock();
    std::vector<std::shared_ptr<LiveWidget>> chain;
    for (LiveWidget* w = focus ? focus.get() : page.get(); w; w = w->parent_widget()) {
        chain.push_back(w->self());
        if (w == page.get()) break;
    }
    auto event = std::make_shared<Object>("MKeyPressEvent");
    event->set("keysym", Value::integer(keysym(k)));
    event->set("state", Value::integer(0)), event->set("port", Value::integer(0));
    event->set("widget", widget_value(page)), event->set("context", render_context_);
    const Value ev = object_value(event);

    // One widget after the other: the class, then the script.
    auto step = std::make_shared<std::function<void(size_t)>>();
    std::weak_ptr<std::function<void(size_t)>> weak_step = step;
    *step = [this, chain, k, ev, page, focus, weak_step](size_t at) {
        const auto self = weak_step.lock();
        if (at >= chain.size()) {
            // The page's own handling.
            if (k == Key::Accept || k == Key::Cancel) {
                const bool accept = k == Key::Accept;
                const std::shared_ptr<LiveWidget> target = page->focus.lock() ? page->focus.lock() : (accept ? nullptr : page);
                if (!target) return;
                auto e = std::make_shared<Object>(accept ? "MActivateEvent" : "MCancelEvent");
                e->set("widget", widget_value(target)), e->set("context", render_context_);
                std::vector<std::shared_ptr<LiveWidget>> up;
                for (LiveWidget* w = target.get(); w; w = w->parent_widget()) up.push_back(w->self());
                run_chain(std::move(up), 0, accept ? "onActivate" : "onCancel", {render_context_, object_value(e)}, {});
            } else if (k == Key::Up || k == Key::Down || k == Key::Left || k == Key::Right) navigate(k);
            return;
        }
        LiveWidget& w = *chain[at];
        if (&w != page.get() && widget_key(*this, w, k)) return;
        const auto it = w.members.find("onKeyPress");
        if (it == w.members.end() || it->second->value.kind != Kind::Function) {
            if (self) (*self)(at + 1);
            return;
        }
        run(it->second->value, {render_context_, ev}, w.path() + "::onKeyPress", [self, at](const Value& result) {
            if (result.is_number() && result.as_float() != 0) return;
            if (self) (*self)(at + 1);
        });
    };
    (*step)(0); // the calls it starts hold it until they end
}

// The focus moves as the program's search orders the page's candidates (0x270118; the
// collector 0x26F9E0, the measure 0x26F0A8, the order 0x26F2B8), with the page's
// navigate_clip_angle (0), navigate_start_front (off) and navigate_wraparound (on):
//   - every rectangle is turned so the key's direction is +x ("along"; "across" the other);
//   - the focused entry is the topmost widget above the focus that can take it; F is its
//     navigate source, S and T a candidate's source and target;
//   - a candidate must lie ahead: the middle of S further along than the middle of F;
//   - from F's back edge (its front edge with navigate_start_front), a = T.x0 - edge + 1 and
//     b = T.x1 - edge + 1; in line (T and F overlap across): the smaller of the two that are
//     not negative, angle 0; to the side: the one of smaller size, the angle of the gap across
//     to it, valid only below the clip angle (so never with the default 0);
//   - order: the larger group (inside the focus's scroll space first), the smaller angle, the
//     nearer, the closer across (S against F), the earlier made;
//   - with nothing valid and wraparound on, everything is measured again 10000 further along.
// The key is the page's whether or not the focus moved; the sound plays on a move.
void Runtime::navigate(Key k) {
    LiveWidget* page = current_page();
    if (!page) return;
    const std::vector<FocusCandidate> all = focus_candidates(*page, true);
    if (all.empty()) return;
    std::shared_ptr<LiveWidget> focus = page->focus.lock();
    if (!focus) {
        set_focus(*page, first_focus(*page));
        return;
    }
    // The topmost focusable widget above the focus (a candidate hides what is inside it).
    for (LiveWidget* w = focus->parent_widget(); w && w != page; w = w->parent_widget())
        if (w->can_focus() && condition_holds(*w) && !w->insensitive() && !w->is("ListBox")) focus = w->self();
    struct Box {
        float a0, c0, a1, c1;
    };
    auto turn = [k](const Rect& r, float shift) {
        switch (k) {
        case Key::Right: return Box{r.x + shift, r.y, r.x + r.w + shift, r.y + r.h};
        case Key::Left: return Box{-(r.x + r.w) + shift, -(r.y + r.h), -r.x + shift, -r.y};
        case Key::Down: return Box{r.y + shift, -(r.x + r.w), r.y + r.h + shift, -r.x};
        default: return Box{-(r.y + r.h) + shift, r.x, -r.y + shift, r.x + r.w};
        }
    };
    const float clip_angle = page->data.number("navigate_clip_angle");
    const bool start_front = page->data.number("navigate_start_front") != 0;
    const bool wraparound = !page->data.has("navigate_wraparound") || page->data.number("navigate_wraparound") != 0;
    Box f{};
    bool found = false;
    for (const FocusCandidate& c : all)
        if (c.widget == focus) f = turn(c.source, 0), found = true;
    if (!found) {
        const Rect r = focus->screen_rect();
        f = turn(r, 0);
    }
    struct Measure {
        bool valid = false;
        float angle = 0, distance = 10000, across = 0;
        int group = 0;
        size_t order = 0;
    };
    auto measure = [&](const FocusCandidate& c, float shift) {
        Measure m;
        m.group = c.group;
        const Box s = turn(c.source, shift), t = turn(c.target, shift);
        m.across = std::fabs((s.c0 + s.c1) * 0.5f - (f.c0 + f.c1) * 0.5f);
        if (!((f.a0 + f.a1) * 0.5f < (s.a0 + s.a1) * 0.5f)) return m;
        const float edge = start_front ? f.a1 : f.a0;
        const float a = t.a0 - edge + 1, b = t.a1 - edge + 1;
        float gap = -1;
        if (t.c1 <= f.c0) gap = f.c0 - t.c1;
        else if (f.c1 <= t.c0) gap = t.c0 - f.c1;
        if (gap >= 0) { // to the side
            const float along = std::fabs(a) < std::fabs(b) ? a : b;
            m.angle = std::atan2(gap, along) / 3.14159274f * 180.0f;
            m.distance = std::sqrt(along * along + gap * gap);
            m.valid = m.angle < clip_angle;
            return m;
        }
        if (a >= 0 && b >= 0) m.distance = std::min(a, b), m.valid = true;
        else if (a >= 0) m.distance = a, m.valid = true;
        else if (b >= 0) m.distance = b, m.valid = true;
        return m;
    };
    auto before = [](const Measure& x, const Measure& y) {
        if (x.group != y.group) return x.group > y.group;
        if (x.angle != y.angle) return x.angle < y.angle;
        if (x.distance != y.distance) return x.distance < y.distance;
        if (x.across != y.across) return x.across < y.across;
        return x.order < y.order;
    };
    std::shared_ptr<LiveWidget> best;
    for (int pass = 0; pass < 2 && !best; ++pass) {
        if (pass == 1 && !wraparound) break;
        Measure best_m;
        for (size_t i = 0; i < all.size(); ++i) {
            if (all[i].widget == focus) continue;
            Measure m = measure(all[i], pass ? 10000.0f : 0.0f);
            m.order = i;
            if (!m.valid) continue;
            if (!best || before(m, best_m)) best = all[i].widget, best_m = m;
        }
    }
    if (!best) return;
    set_focus(*page, best);
    navigate_sound(*page);
}

// The page's sound for a move of the focus: its `navigate_sound`. ❓ "cursor" when the layout
// gives none (the name the page's constructor refers to, 0x232E00).
void Runtime::navigate_sound(LiveWidget& page) {
    const std::string name = page.data.has("navigate_sound") ? page.data.text("navigate_sound") : std::string("cursor");
    if (name.empty()) return;
    const Value arg = Value::string(name);
    sound("sound", "play", Args(&arg, 1));
}

} // namespace gt4r::menu
