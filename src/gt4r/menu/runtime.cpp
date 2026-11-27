#include "gt4r/menu/runtime.h"

#include "gt4r/menu/game_network.h"
#include "gt4r/menu/race_natives.h"

#include "gt4r/menu/game_settings.h"
#include "gt4r/menu/photo_natives.h"
#include "gt4r/menu/logger.h"

#include "gt4r/menu/no_card.h"
#include "gt4r/menu/runtime_internal.h"

#include "base/log.h"

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
// do with them. 0x100 L1, 0x200 L2, 0x800 R1, 0x1000 R2 (the photo page's help names what they do, the
// photo preview's "L1 R1 Zoom" reads 0x100 and 0x800); ❓ which buttons give 0x40000 and 0x80000.
struct KeyCode {
    Key key;
    const char* name;
    int sym;
};
const KeyCode kKeys[] = {
    {Key::Up, "up", 0xFF52},       {Key::Down, "down", 0xFF54},     {Key::Left, "left", 0xFF51},   {Key::Right, "right", 0xFF53},
    {Key::Accept, "accept", 0xFF0D}, {Key::Cancel, "cancel", 0xFF1B}, {Key::Start, "start", 0xFF8D}, {Key::Select, "select", 0xFF63},
    {Key::L1, "l1", 0xFFC8},       {Key::R1, "r1", 0xFFD2},         {Key::L2, "l2", 0xFFC9},       {Key::R2, "r2", 0xFFD3},
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
        // A project's script names files of the disc's top beside its own (gtmode's
        // "usedcar/US_used0000" is usedcar/US_used0000.adc). ❓ The program's search is not read.
        if (path.rfind("projects/", 0) == 0) {
            const size_t second = path.find('/', 9);
            if (second != std::string::npos && path.find('/', second + 1) != std::string::npos)
                if (std::optional<Bytes> file = read_(path.substr(second + 1))) return file;
        }
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
    static const bool logged = std::getenv("GT4R_MENU_LOG") != nullptr; // (for checks)
    if (logged) LOG_INFO("Menu: %s", line.c_str());
    log_.push_back(std::move(line));
    while (log_.size() > kMaxLog) log_.pop_front();
}

void Runtime::sound(const std::string& object, const std::string& method, Args args) {
    // (The boot's check of the card plays its sound with the question, which is not asked.)
    if (!config_.console_card && object == "sound" && method == "play" && project_name_ == "boot" && current_page() && current_page()->name == "CheckRoot") return;
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
        // MKeyPressEvent / MKeyReleaseEvent / MActivateEvent(context, widget[, keysym]): pushed by a script to a widget.
        for (const char* cls : {"MKeyPressEvent", "MKeyReleaseEvent", "MActivateEvent"})
            into.define(cls, Vm::native(cls, [cls](Vm&, const Value&, Args a) {
                auto e = std::make_shared<Object>(cls);
                e->set("context", a.empty() ? Value{} : a[0]), e->set("widget", a.size() > 1 ? a[1] : Value{});
                e->set("keysym", a.size() > 2 ? a[2] : Value::integer(0));
                e->set("state", Value::integer(0)), e->set("port", Value::integer(0));
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
            for (const char* f : {"initialize", "terminate", "preload", "loadIpic", "asyncSound", "setPause"})
                movie->define(f, Vm::native(std::string("MMovieFace::") + f, [](Vm&, const Value&, Args) { return Value{}; }));
            movie->define("expandAlpha", Vm::native("MMovieFace::expandAlpha", [r](Vm&, const Value&, Args a) {
                              r->movie_expand_alpha = !a.empty() && a[0].truthy();
                              return Value{};
                          }));
            into.define("MMovieFace", Value::of(Kind::Module, movie));
        }
        install_actor_natives(*this, into);
        install_game_natives(*this, into);
        install_race_natives(*this, into);
        install_logger_natives(*this, into);
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
    // The pad held (the program's mask) and the sticks (channels 0 and 1 the right one's, 2 and 3 the left one's): Runtime::set_pad.
    update->method("getPadButtonState", [this](Vm&, const Value&, Args a) { return Value::integer(arg_int(a, 0) == 0 ? s32(pad_.buttons) : 0); });
    update->method("getLastPadButtonState", [this](Vm&, const Value&, Args a) { return Value::integer(arg_int(a, 0) == 0 ? s32(pad_last_.buttons) : 0); });
    update->method("getPadAnalogChannel", [this](Vm&, const Value&, Args a) {
        const int c = arg_int(a, 1);
        return Value::number(arg_int(a, 0) == 0 && c >= 0 && c < 4 ? pad_.sticks[c] : 0.0f);
    });
    update->method("getLastPadAnalogChannel", [this](Vm&, const Value&, Args a) {
        const int c = arg_int(a, 1);
        return Value::number(arg_int(a, 0) == 0 && c >= 0 && c < 4 ? pad_last_.sticks[c] : 0.0f);
    });
    for (const char* nothing : {"loadGpbFromMC", "setStartPage"}) update->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
    // existGpbBinary(path): the packs loaded have it (the Endurance hall's tickets: its large picture, else the small one).
    auto exists_fn = [this](Vm&, const Value&, Args a) {
        if (a.empty() || !project_) return Value::integer(0);
        const std::string path = a[0].to_string();
        bool found = project_->has_entry(current_page() ? current_page()->name : std::string(), path);
        for (const auto& page : open_pages()) found = found || project_->has_entry(page->name, path);
        return Value::integer(found);
    };
    update->method("existGpbBinary", exists_fn);
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
    render->method("getCommonPage", [this](Vm&, const Value&, Args a) {
        const Value page = widget_value(common_page(arg_text(a, 0)));
        if (arg_text(a, 0) != "ConfirmDialog" || config_.console_card || page.is_nil()) return page;
        // The dialog's open(context, type, message, title, root, yes, no): what only a card would ask is answered here (no_card.h).
        auto dialog = std::make_shared<Object>("ConfirmDialog");
        dialog->method("open", [this, page](Vm& vm, const Value&, Args a) {
            std::vector<Value> args(a.begin(), a.end());
            if (args.size() >= 3)
                if (const std::optional<CardAnswer> answer = card_answer(*this, args[2].to_string())) {
                    if (!answer->shown.empty()) {
                        args.resize(3);
                        args[1] = Value::integer(2), args[2] = Value::string(answer->shown);
                        vm.then_call(vm.attribute(page, "open"), args, [what = answer->answer](Vm&, const Value&) { return Value::integer(what); });
                        return Value{};
                    }
                    return Value::integer(answer->answer);
                }
            vm.then_call(vm.attribute(page, "open"), args, {});
            return Value{};
        });
        dialog->method("forceCancel", [page](Vm& vm, const Value&, Args a) {
            vm.then_call(vm.attribute(page, "forceCancel"), std::vector<Value>(a.begin(), a.end()), {});
            return Value{};
        });
        return object_value(dialog);
    });
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
    render->method("existGpbBinary", exists_fn);
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
    options_changed();
}

void Runtime::options_changed() {
    if (on_options) on_options(settings_of(*game_));
}

std::string Runtime::first_project() {
    boot();
    const Value v = vm_->lookup("StartProject");
    return v.kind == Kind::String && !v.as_string().empty() ? v.as_string() : std::string("boot");
}

void Runtime::set_language(const std::string& language) {
    if (language.empty() || language == config_.language) return;
    config_.language = language;
    if (common_) try {
        common_ = load_project_files(kCommonProject, language);
    } catch (const std::exception& e) {
        note(std::string("the common project in ") + language + ": " + e.what());
    }
}

std::shared_ptr<Project> Runtime::load_project_files(const std::string& name, const std::string& language) {
    auto project = std::make_shared<Project>(read_, name, language);
    project->set_face_alpha(is_prologue());
    if (!config_.console_card) reword_card_texts(*project, language);
    return project;
}

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

void Runtime::start_project(const std::string& asked, const std::string& language) {
    boot();
    std::string name = asked;
    // Prologue's card project (its boot's check and load, its save and load pages) without a card: the game there is loaded, the
    // pages are the saved games' (no_card.h), and the title comes.
    if (name == "memcard" && !config_.console_card && is_prologue()) {
        if (game_->next_menu_arg.empty()) load_saved_game(*this);
        else if (game_->next_menu_arg == "save_game" || game_->next_menu_arg == "load_game") saves_asked_ = true;
        const Value top = vm_->lookup("TopProject");
        name = top.kind == Kind::String && !top.as_string().empty() ? top.as_string() : std::string("prologue");
        game_->next_menu_arg.clear(), game_->last_menu_project = asked;
        if (sequence_ == "boot") game_->last_sequence = "boot", sequence_ = "menu";
    }
    unload_project();
    if (!language.empty()) config_.language = language;
    project_ = load_project_files(name, config_.language);
    project_name_ = name;
    root_widget_ = LiveWidget::build(project_->layout(), vm_->root(), this);
    note("project " + name + " (" + root_widget_->name + ")");
    vm_->instructions = 0, vm_->budget = kCallBudget;
    vm_->require("/projects/" + name + "/" + name);
    answer_photo_destination(*root_widget_);
    if (name == "arcade" && network) adapt_lan_pages(*this);
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
    // A project whose onLoad opens no page: Prologue's projects name their first page by their
    // getStartPage(arg), given the project's argument (main::game.next_menu_arg; "" none); else the
    // first page.
    Event first;
    first.kind = Event::Kind::Call, first.what = "the first page";
    const std::string start_arg = game_->next_menu_arg;
    first.callee = Vm::native("first page", [this, start_arg](Vm&, const Value&, Args) {
        if (pages_.empty() && !finished_ && root_widget_) {
            const Value page = call_now(*root_widget_, "getStartPage", {Value::string(start_arg)});
            if (const auto live = page.as<LiveWidget>(); live && live->is("RootWindow")) {
                open_page(live, true);
                return Value{};
            }
        }
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
    // load_replay is a sequence of menus (the Replay Theater); a replay chosen there is "replay".
    if (!finished_ || next.empty() || next == "menu" || next == "boot" || next == "load_replay" || next == "photo_labo") return std::nullopt;
    RaceRequest r = game_->race;
    r.sequence = next;
    complete_race_request(const_cast<Runtime&>(*this), r);
    return r;
}

bool Runtime::advance() {
    if (!finished_) return false;
    if (race_request()) return false;
    // A race sequence that was under way is over.
    if (!game_->race_session.sequence.empty()) game_->end_race_sequence();
    // The program's load_replay sequence (0x10CA50): the next menu argument becomes the page
    // ReplayTheaterRoot (0x42FC98) and the project labomode runs (0x10B290); its card manager in
    // MODE_LOAD_REPLAY lists the replays.
    if (game_->next_sequence == "load_replay") {
        game_->next_menu_project = "labomode", game_->next_menu_arg = "ReplayTheaterRoot";
        game_->card_action = 0, game_->replay_file.clear();
    }
    // The photo_labo sequence (0x1C7A88): the project labomode, no page named.
    if (game_->next_sequence == "photo_labo") {
        game_->next_menu_project = "labomode", game_->next_menu_arg.clear();
        game_->menu_result.clear();
    }
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
    // The Replay Theater ended its project without asking for anything: a replay loaded and the
    // action set (labomode's Play: load, setAction(1), the page closed) is shown, then the theater
    // again (the sequence's loop, 0x10CA50).
    if (sequence_ == "load_replay" && game_->next_sequence.empty() && game_->next_menu_project.empty()) {
        // (a demo replay is a path on the disc: replay/<name>/<name>)
        if (game_->card_action == 1 && !game_->replay_file.empty()) game_->next_sequence = "replay";
        // (its PlayList folder's Play: menu_result "list", the card's play list one replay after another)
        else if (game_->card_action == 1 && game_->menu_result == "list") play_list_next(*this, true);
        game_->next_menu_project = "labomode", game_->next_menu_arg = "ReplayTheaterRoot";
        game_->card_action = 0;
    }
    // The Photo Lab's project ended: the project print when its result is "print", else the lab again, until its result
    // is "exit" (0x1C7A88; the scripts then ask for a sequence). ❓ Developing a film ("develop") is not built.
    if (sequence_ == "photo_labo" && game_->next_sequence.empty() && game_->next_menu_project.empty() && game_->menu_result != "exit") {
        const bool print = game_->menu_result == "print";
        game_->next_menu_project = print ? "print" : "labomode", game_->next_menu_arg.clear();
    }
    const std::optional<RaceRequest> race = race_request();
    // The scripts asked for a race: its sequence starts (the program makes the race's menus'
    // work object anew, 0x127E60, and the result routines' session).
    if (race && game_->sequence_asked) {
        game_->end_race_sequence(); // one not ended by the app
        game_->race_session = {};
        game_->race_session.sequence = race->sequence;
        game_->race_session.license_open = race->sequence == "license";
        start_quick_work(*this, *race);
        // What the race's side reads of the request that the scripts do not set themselves (game().race: RaceScreen, the
        // result routines): the garage car's place, the B-spec driver's book and the course's marks.
        game_->race.garage_slot = race->garage_slot;
        game_->race.bspec_book = race->bspec_book, game_->race.bspec_course_marks = race->bspec_course_marks;
        game_->race.bspec_course_known = race->bspec_course_known;
    }
    game_->sequence_asked = false;
    note(race ? "the project finished; the scripts ask for " + race->sequence + ": " + race->car + " at " + race->course
              : "the project finished; next: " + (wanted_project().empty() ? std::string("(none)") : wanted_project()));
}

// ---------------------------------------------------------------------------------------------
// The event thread
// ---------------------------------------------------------------------------------------------

void Runtime::run(const Value& callee, std::vector<Value> args, std::string what, std::function<void(const Value&)> done) {
    vm_->instructions = 0, vm_->budget = kCallBudget;
    static const bool traced = std::getenv("GT4R_MENU_LOG") && std::string_view(std::getenv("GT4R_MENU_LOG")) == "calls";
    if (trace_calls || traced) note("call " + what);
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
        // The title's Save Game is the saved games' page (no_card.h).
        if (!config_.console_card && name == "onActivate" && project_name_ == "arcade" && chain[at]->name == "Save" && current_page() && current_page()->name == "TopRoot") {
            saves_asked_ = true;
            if (done) done(true);
            return;
        }
        // An activate or cancel function also gets the widget it belongs to (the scripts'
        // `object.getFocusedWidget()` on a page). ❓ From the scripts' use.
        std::vector<Value> call_args = args;
        if (name == "onActivate" || name == "onCancel") call_args.push_back(widget_value(chain[at]));
        run(callee, call_args, what, [this, chain, at, name, args, done](const Value& result) mutable {
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

void Runtime::queue_call(LiveWidget& w, const std::string& name, std::vector<Value> args) {
    const auto it = w.members.find(name);
    if (it == w.members.end() || it->second->value.kind != Kind::Function) return;
    Event e;
    e.kind = Event::Kind::Call, e.callee = it->second->value, e.args = std::move(args), e.what = w.path() + "::" + name;
    events_.push_back(std::move(e));
}

void Runtime::queue_function(const Value& callee, std::vector<Value> args, std::string what) {
    Event e;
    e.kind = Event::Kind::Call, e.callee = callee, e.args = std::move(args), e.what = std::move(what);
    events_.push_back(std::move(e));
}

bool Runtime::is_prologue() {
    if (prologue_ < 0) prologue_ = read_("config/config-prologue.adc") && !read_("config/config-main.adc") ? 1 : 0;
    return prologue_ == 1;
}

LiveWidget* Runtime::cursor_widget() {
    LiveWidget* focus = focused();
    if (focus && focus->is("ListBox")) return list_cursor_row(*focus);
    return focus;
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
    if (e.kind == Event::Kind::Key) {
        key_repeated_ = e.repeat;
        return e.release ? dispatch_release(e.key) : dispatch_key(e.key);
    }
    if (e.kind == Event::Kind::Call) return run(e.callee, std::move(e.args), std::move(e.what));
    const auto object = e.value.as<Object>();
    if (!object) return;
    // A key or an activation a script pushed to a widget of its own choice.
    if (object->label == "MKeyPressEvent" || object->label == "MKeyReleaseEvent" || object->label == "MActivateEvent") {
        const std::shared_ptr<LiveWidget> target = object->get("widget").as<LiveWidget>();
        if (!target) return;
        if (object->label == "MActivateEvent") {
            std::vector<std::shared_ptr<LiveWidget>> up;
            for (LiveWidget* w = target.get(); w; w = w->parent_widget()) up.push_back(w->self());
            return run_chain(std::move(up), 0, "onActivate", {render_context_, e.value}, {});
        }
        for (const KeyCode& c : kKeys)
            if (s32(object->number("keysym")) == c.sym) return object->label == "MKeyPressEvent" ? dispatch_key(c.key, target) : dispatch_release(c.key, target);
        return;
    }
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
            // The loop's turn (0x251CE8): every queued event, then the watchers, once a
            // picture; a watcher's counter steps only on a turn, so not while a call waits
            // in sync (0x250E08). What a watcher queues waits for the next turn.
            if (watchers_turn_ != frame_) {
                watchers_turn_ = frame_;
                for (const auto& w : watchers_)
                    if (step_watcher(*w)) due_watchers_.push_back(w);
            }
            if (due_watchers_.empty()) break;
            const std::shared_ptr<Object> w = due_watchers_.front();
            due_watchers_.pop_front();
            Value function, context;
            if (std::find(watchers_.begin(), watchers_.end(), w) == watchers_.end()) continue; // removed meanwhile
            if (!watcher_call(*w, function, context) || !function.p || !function.p->callable()) continue;
            watcher_called(*w);
            const size_t queued = events_.size();
            run(function, {context}, "a watcher's function");
            if (events_.size() > queued && due_watchers_.empty()) break;
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
}

void Runtime::update() {
    ++frame_;
    step_frame_objects();
    queue_focus_events();
    pump();
    if (autosave_due_ && running_.empty() && events_.empty()) {
        autosave_due_ = false;
        if (!config_.console_card) autosave(*this);
    }
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
    autosave_due_ = true;
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
            if (std::find(pages_.begin(), pages_.end(), p) != pages_.end()) {
                focus_first(*p);
                transition_page_opened(vm_->attribute(render_context_, "transition"));
            }
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
    autosave_due_ = true;
    note("page " + page->name + " closed");
}

void Runtime::focus_first(LiveWidget& page) {
    if (page.focus.lock()) return;
    if (const std::shared_ptr<LiveWidget> w = first_focus(page)) set_focus(page, w);
}

void Runtime::set_focus(LiveWidget& page, const std::shared_ptr<LiveWidget>& w) {
    const std::shared_ptr<LiveWidget> before = page.focus.lock();
    if (before == w) return;
    // Only the reference changes here: the picture after, the focus leave and enter events are
    // queued (0x22FBE0), so a page's onInitialize can set the focus before its widgets' actors
    // are made.
    page.focus = w;
    mark_focus(page);
    if (w) scroll_into_view(*w);
}

// The focus bits: the focused widget's, and every parent's of a widget that has them, each picture (0x263A68, 0x207208).
// Only the top page's focus has them (vf92: the focus bit is "this is the focus of the context's
// top page", 0x2319C0 / 0x235C00): a page under a dialog shows its focused widget at rest.
// (❓ Prelight is the widget the cursor points at: here the focused one.)
void Runtime::mark_focus(LiveWidget& page) {
    std::vector<std::weak_ptr<LiveWidget>> now;
    const bool top = !pages_.empty() && pages_.back().get() == &page;
    for (std::shared_ptr<LiveWidget> w = top ? page.focus.lock() : nullptr; w;) {
        now.push_back(w);
        LiveWidget* up = w.get() == &page ? nullptr : w->parent_widget();
        w = up ? up->self() : nullptr;
    }
    auto same = [](const std::weak_ptr<LiveWidget>& a, const std::weak_ptr<LiveWidget>& b) { return !a.owner_before(b) && !b.owner_before(a); };
    if (now.size() == page.focus_chain.size() && std::equal(now.begin(), now.end(), page.focus_chain.begin(), same)) return;
    for (const auto& w : page.focus_chain)
        if (const auto held = w.lock()) held->state &= ~(kFocus | kPrelight);
    for (const auto& w : now) w.lock()->state |= kFocus | kPrelight;
    page.focus_chain = std::move(now);
}

// Each picture (0x22FBE0): a page whose focus is not the one its last picture had queues an
// MFocusLeaveEvent for the old and an MFocusEnterEvent for the new.
void Runtime::queue_focus_events() {
    for (const auto& page : pages_) {
        mark_focus(*page);
        const std::shared_ptr<LiveWidget> now = page->focus.lock(), seen = page->focus_seen.lock();
        if (now == seen) continue;
        page->focus_seen = now;
        auto queue = [this](const std::shared_ptr<LiveWidget>& w, bool enter) {
            std::weak_ptr<LiveWidget> weak = w;
            Event e;
            e.kind = Event::Kind::Call, e.what = w->path() + (enter ? "::onFocusEnter" : "::onFocusLeave");
            e.callee = Vm::native(e.what, [this, weak, enter](Vm&, const Value&, Args) {
                const std::shared_ptr<LiveWidget> target = weak.lock();
                if (!target) return Value{};
                if (enter) widget_focus_entered(*this, *target);
                auto event = std::make_shared<Object>(enter ? "MFocusEnterEvent" : "MFocusLeaveEvent");
                event->set("widget", widget_value(target)), event->set("context", render_context_);
                bubble_now(*target, enter ? "onFocusEnter" : "onFocusLeave", {render_context_, object_value(event)});
                return Value{};
            });
            events_.push_back(std::move(e));
        };
        if (seen) queue(seen, false);
        if (now) queue(now, true);
    }
    // The cursor's crossings (0x22FFD0): when the widget it aims at (the shown page's focus) is
    // not the one it last went into, an mLeaveEvent for that one and an mEnterEvent for the new
    // (the widgets' onLeave / onEnter: the map's places spin and show their notices).
    const std::shared_ptr<LiveWidget> aim = pages_.empty() ? nullptr : pages_.back()->focus.lock(), was = entered_.lock();
    if (aim != was) {
        entered_ = aim;
        auto cross = [this](const std::shared_ptr<LiveWidget>& w, bool enter) {
            std::weak_ptr<LiveWidget> weak = w;
            Event e;
            e.kind = Event::Kind::Call, e.what = w->path() + (enter ? "::onEnter" : "::onLeave");
            e.callee = Vm::native(e.what, [this, weak, enter](Vm&, const Value&, Args) {
                const std::shared_ptr<LiveWidget> target = weak.lock();
                if (!target) return Value{};
                auto event = std::make_shared<Object>(enter ? "MEnterEvent" : "MLeaveEvent");
                event->set("widget", widget_value(target)), event->set("context", render_context_);
                bubble_now(*target, enter ? "onEnter" : "onLeave", {render_context_, object_value(event)});
                return Value{};
            });
            events_.push_back(std::move(e));
        };
        if (was) cross(was, false);
        if (aim) cross(aim, true);
    }
}

void Runtime::set_pad(const Pad& pad) {
    static const std::pair<u32, Key> buttons[] = {
        {Pad::kUp, Key::Up}, {Pad::kDown, Key::Down}, {Pad::kLeft, Key::Left},   {Pad::kRight, Key::Right}, {Pad::kL1, Key::L1},     {Pad::kL2, Key::L2},
        {Pad::kR1, Key::R1}, {Pad::kR2, Key::R2},     {Pad::kCross, Key::Accept}, {Pad::kTriangle, Key::Cancel}, {Pad::kSquare, Key::Cancel}, {Pad::kStart, Key::Start},
    };
    const u32 released = pad_.buttons & ~pad.buttons;
    pad_last_ = pad_, pad_ = pad;
    if (!keys_taken()) return;
    for (const auto& [bit, key] : buttons)
        if (released & bit) {
            Event e;
            e.kind = Event::Kind::Key, e.key = key, e.release = true;
            events_.push_back(std::move(e));
        }
}

// The context's event mask lets no event through at 0 (the photo map's drags).
bool Runtime::keys_taken() {
    const Value mask = vm_->attribute(render_context_, "event_mask");
    return !mask.is_number() || s32(mask.as_float()) != 0 || mask.as_float() != 0;
}

void Runtime::key(Key k, bool repeat) {
    if (!keys_taken()) return;
    Event e;
    e.kind = Event::Kind::Key, e.key = k, e.repeat = repeat;
    events_.push_back(std::move(e));
    last_input_frame_ = frame_;
}

// A key press goes to the focused widget and then to each parent up to the page until one
// deals with it (the program's 0x235A20). A widget's class sees it first (a select box moves
// its index, 0x2DAD18), then its script (onKeyPress(context, event)). The page, last, turns
// accept and cancel into their own events and the directions into a move of the focus
// (0x235680).
void Runtime::dispatch_key(Key pressed, const std::shared_ptr<LiveWidget>& to) {
    const std::shared_ptr<LiveWidget> page = pages_.empty() ? nullptr : pages_.back();
    if (!page) return;
    const std::shared_ptr<LiveWidget> focus = to ? to : page->focus.lock();
    std::vector<std::shared_ptr<LiveWidget>> chain;
    for (LiveWidget* w = focus ? focus.get() : page.get(); w; w = w->parent_widget()) {
        chain.push_back(w->self());
        if (w == page.get()) break;
    }
    const Key k = pressed;
    auto event = std::make_shared<Object>("MKeyPressEvent");
    event->set("keysym", Value::integer(keysym(k)));
    event->set("state", Value::integer(0)), event->set("port", Value::integer(0));
    event->set("widget", widget_value(page)), event->set("context", render_context_);
    const Value ev = object_value(event);

    // One widget after the other: the class, then the script.
    auto step = std::make_shared<std::function<void(size_t)>>();
    std::weak_ptr<std::function<void(size_t)>> weak_step = step;
    *step = [this, chain, k = pressed, event, ev, page, focus, weak_step](size_t at) mutable {
        const auto self = weak_step.lock();
        // (A script may give the event another key: the photo map's directions.)
        for (const KeyCode& c : kKeys)
            if (s32(event->number("keysym")) == c.sym) k = c.key;
        if (at >= chain.size()) {
            // The page's own handling.
            // Prologue's page (mRootWindow slot 64, 0x230548) takes Start as Accept, and makes its activate event on
            // the page itself, so a page without a focus hears onActivate (its opening movie's skip).
            const bool prologue = is_prologue();
            if (k == Key::Accept || k == Key::Cancel || (prologue && k == Key::Start)) {
                const bool accept = k != Key::Cancel;
                const std::shared_ptr<LiveWidget> target = page->focus.lock() ? page->focus.lock() : (accept && !prologue ? nullptr : page);
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
        // A ScaleBar moves its value (0x238D10) and a ListBox its focus (0x2B3918) after the
        // widget's script, when that did not take the key.
        const bool scale = &w != page.get() && (w.is("ScaleBar") || w.is("ListBox"));
        std::shared_ptr<LiveWidget> held = chain[at];
        const auto it = w.members.find("onKeyPress");
        if (it == w.members.end() || it->second->value.kind != Kind::Function) {
            if (scale && class_key_after(*this, w, k)) return;
            if (self) (*self)(at + 1);
            return;
        }
        run(it->second->value, {render_context_, ev}, w.path() + "::onKeyPress", [this, self, at, scale, held, k](const Value& result) {
            if (result.is_number() && result.as_float() != 0) return;
            if (scale && class_key_after(*this, *held, k)) return;
            if (self) (*self)(at + 1);
        });
    };
    (*step)(0); // the calls it starts hold it until they end
}

// A key let go: the focused widget's script and then each parent's, until one deals with it.
void Runtime::dispatch_release(Key k, const std::shared_ptr<LiveWidget>& to) {
    const std::shared_ptr<LiveWidget> page = pages_.empty() ? nullptr : pages_.back();
    if (!page) return;
    const std::shared_ptr<LiveWidget> focus = to ? to : page->focus.lock();
    std::vector<std::shared_ptr<LiveWidget>> chain;
    for (LiveWidget* w = focus ? focus.get() : page.get(); w; w = w->parent_widget()) {
        chain.push_back(w->self());
        if (w == page.get()) break;
    }
    auto event = std::make_shared<Object>("MKeyReleaseEvent");
    event->set("keysym", Value::integer(keysym(k)));
    event->set("state", Value::integer(0)), event->set("port", Value::integer(0));
    event->set("widget", widget_value(page)), event->set("context", render_context_);
    run_chain(std::move(chain), 0, "onKeyRelease", {render_context_, object_value(event)}, {});
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
