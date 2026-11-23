// The menu pages as the game's files describe them: a project's layout drawn with its own
// pictures, text and fonts; and the menus running (menu::Runtime): the game's scripts on those
// pages, from the boot project on. How the properties are drawn is in docs/formats/menu.md.

#include "gt4r/app/app.h"
#include "gt4r/app/menu_cars.h"
#include "gt4r/app/menu_models.h"
#include "gt4r/app/menu_movies.h"
#include "gt4r/app/menu_page_drawer.h"

#include "base/log.h"
#include "formats/text/charset.h"
#include "formats/text/text_layout.h"
#include "gt4r/audio/app_sound.h"
#include "gt4r/menu/font.h"
#include "gt4r/menu/project.h"
#include "gt4r/menu/race_natives.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/menu/native.h"
#include "gt4r/menu/runtime_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <map>

namespace gt4r {

namespace {

// A call of the scripts' sound object, played (audio/menu_sound.h has what each does). The
// calls of its `midi` and `stream` objects are not played yet.
void play_sound_call(audio::MenuSound& s, const menu::SoundCall& c) {
    if (c.object != "sound") return;
    auto text = [&c](size_t i) { return i < c.args.size() ? c.args[i].to_string() : std::string(); };
    auto number = [&c](size_t i, float otherwise) { return i < c.args.size() && c.args[i].is_number() ? c.args[i].as_float() : otherwise; };
    const std::string& m = c.method;
    if (m == "play") s.play(text(0));
    else if (m == "load") c.args.empty() ? s.load() : s.load(text(0));
    else if (m == "unload") s.unload();
    else if (m == "keepMusic") s.keep_music(text(0));
    else if (m == "startMusic") s.start_music(text(0));
    else if (m == "stopMusic") s.stop_music();
    else if (m == "fadeOutMusic") s.fade_out_music(number(0, 1.0f));
    else if (m == "fadeInMusic") s.fade_in_music(text(0), number(1, 1.0f));
    else if (m == "startStream") s.start_stream(text(0), text(1));
    else if (m == "startStreamEx") s.start_stream(text(0), text(1), int(number(2, -1)), number(3, 0) != 0, number(4, 1.0f));
    else if (m == "stopStream") s.stop_stream();
    else if (m == "pauseStream") s.pause_stream(number(0, 1) != 0);
}


class MenuViewer : public Screen {
public:
    explicit MenuViewer(App& app) {
        const auto list = [&app](const std::string& folder) { return app.vfs()->list_dir(folder); };
        for (const std::string& name : menu::list_projects(list)) {
            const std::vector<std::string> languages = menu::project_languages(list, name);
            projects_.push_back({name, languages.empty() ? std::string("US") : languages[0]});
        }
        // GT4R_MENU="project=boot,page=LanguageRoot,state=2,full=1,run=1": what a scripted run opens.
        // "boot=1": the menus run from the game's first project, as from power on; "card=none"
        // (no memory card), "card=saved" (one with a saved game), "credits=N" for that run;
        // "timing=1" logs the frame times and the pages' drawing every 300 frames.
        if (const char* env = std::getenv("GT4R_MENU")) {
            std::string spec = env;
            size_t at = 0;
            while (at < spec.size()) {
                const size_t end = std::min(spec.find(',', at), spec.size());
                const std::string item = spec.substr(at, end - at);
                at = end + 1;
                const size_t eq = item.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = item.substr(0, eq), value = item.substr(eq + 1);
                if (key == "project") start_project_ = value;
                else if (key == "page") start_page_ = value;
                else if (key == "state") state_ = std::atoi(value.c_str());
                else if (key == "full") full_ = value != "0";
                else if (key == "run") start_run_ = value != "0";
                else if (key == "boot") start_boot_ = value != "0";
                else if (key == "card") card_ = value;
                else if (key == "credits") credits_ = std::atoll(value.c_str());
                else if (key == "timing") timing_ = value != "0";
            }
        }
    }

    void update(App& app, double dt) override {
        step_time_ += dt;
        const MenuInput& m = app.menu();
        if (start_boot_) {
            start_boot_ = false, start_project_.clear();
            run(app, true);
        }
        if (!start_project_.empty()) {
            for (size_t i = 0; i < projects_.size(); ++i)
                if (projects_[i].name == start_project_) project_sel_ = int(i);
            start_project_.clear();
            open(app);
            for (size_t i = 0; i < pages_.size(); ++i)
                if (pages_[i]->name == start_page_) page_sel_ = int(i);
            if (start_run_ && project_) run(app, false);
        }
        if (runtime_) return update_running(app, m);
        if (m.pressed(PadButton::Triangle)) {
            if (full_) full_ = false;
            else if (project_) close();
            else app.pop();
            return;
        }
        if (m.pressed(PadButton::Square)) state_ = state_ == 0 ? kFocusBits : state_ == kFocusBits ? kActive : state_ == kActive ? kInsensitive : 0;
        if (!project_) {
            if (projects_.empty()) return;
            const int page = m.pressed(PadButton::L1) ? -kRows : m.pressed(PadButton::R1) ? kRows : 0;
            project_sel_ = std::clamp(project_sel_ + m.vertical() + page, 0, int(projects_.size()) - 1);
            if (m.pressed(PadButton::Cross)) open(app);
            if (m.pressed(PadButton::Start)) run(app, true);
            return;
        }
        const int pages = int(pages_.size());
        if (pages) page_sel_ = std::clamp(page_sel_ + m.vertical() + (m.pressed(PadButton::L1) ? -kRows : 0), 0, pages - 1);
        if (m.pressed(PadButton::Cross)) full_ = !full_;
        if (m.pressed(PadButton::R1)) run(app, false);
    }

    void render(App&, Ui& ui) override {
        if (runtime_) return render_running(ui);
        const char* states = state_ == kFocusBits ? "focused" : state_ == kActive ? "active" : state_ == kInsensitive ? "unavailable" : "at rest";
        if (project_ && full_ && page_sel_ < int(pages_.size())) {
            // The page over the whole height (4:3).
            const float scale = float(Ui::kHeight) / 480.0f;
            ui.fill(0, 0, Ui::kWidth, Ui::kHeight, {0, 0, 0});
            drawer_->draw(*project_, *pages_[size_t(page_sel_)], (Ui::kWidth - 640 * scale) / 2, 0, scale, state_);
            return;
        }
        ui.header("MENU PAGES", project_ ? project_->name() + "  (" + project_->module_name() + ", " + project_->language() + ")" : std::string());
        const int n = project_ ? int(pages_.size()) : int(projects_.size());
        const int sel = project_ ? page_sel_ : project_sel_;
        const int first = ui.list_window(n, sel, kRows);
        for (int i = 0; i < kRows && first + i < n; ++i) {
            const int idx = first + i;
            const float y = 26 + float(i) * 12;
            if (idx == sel) ui.fill(4, y - 2, 200, 12, colors::kHighlight);
            ui.text(8, y, Ui::clip(project_ ? pages_[size_t(idx)]->name : projects_[size_t(idx)].name, 190), colors::kText);
        }
        if (n > kRows) ui.text_right(200, Ui::kHeight - 30, std::to_string(sel + 1) + "/" + std::to_string(n), colors::kDim);
        const float px = 212, py = 26, pw = Ui::kWidth - px - 6, ph = Ui::kHeight - 50 - py;
        ui.fill(px, py, pw, ph, colors::kPanel);
        if (!error_.empty()) ui.text(px + 6, py + 6, Ui::clip(error_, pw - 12), colors::kBad);
        if (project_ && page_sel_ < n) {
            const LiveWidget& page = *pages_[size_t(page_sel_)];
            const float scale = std::min(pw / 640.0f, (ph - 14) / 480.0f);
            const float x = px + (pw - 640 * scale) / 2, y = py;
            ui.fill(x, y, 640 * scale, 480 * scale, {0, 0, 0});
            drawer_->draw(*project_, page, x, y, scale, state_);
            ui.text(px + 6, py + ph - 12, std::to_string(drawer_->widgets_drawn) + " widgets drawn " + states, colors::kDim);
        } else if (!project_) {
            ui.text(px + 6, py + 6, "The game's menus, a project each: its pages drawn from the", colors::kDim);
            ui.text(px + 6, py + 18, "disc's layout, pictures, text and fonts.", colors::kDim);
        }
        ui.footer(project_ ? "{Up/Down}: page  {Cross}: whole screen  {Square}: state  {R1}: run its script  {Triangle}: back"
                           : "{Up/Down}: select  {Cross}: open  {Start}: run the menus from boot  {Triangle}: back");
    }

private:
    static constexpr int kRows = 25;
    struct Entry {
        std::string name, language;
    };

    void open(App& app) {
        if (projects_.empty()) return;
        const Entry& e = projects_[size_t(project_sel_)];
        try {
            project_ = std::make_unique<menu::Project>([&app](const std::string& path) { return app.vfs()->read(path); }, e.name, e.language);
            drawer_ = std::make_unique<PageDrawer>(app);
            pages_.clear();
            for (const Widget& page : project_->pages()) pages_.push_back(LiveWidget::build(page, nullptr));
            page_sel_ = 0;
            error_.clear();
        } catch (const std::exception& ex) {
            close();
            error_ = ex.what();
            LOG_WARN("Menu viewer: %s", ex.what());
        }
    }
    void close() {
        for (auto& p : pages_) p->clear();
        pages_.clear(), drawer_.reset(), project_.reset(), error_.clear();
    }

    // The menus running: the game's scripts on its pages. `from_boot`: from the project the
    // game starts with; else the project selected, in the world the main script sets up.
    void run(App& app, bool from_boot) {
        try {
            menu::Runtime::Config config;
            gt3r::Vfs* vfs = app.vfs();
            config.list = [vfs](const std::string& folder) { return vfs->list_dir(folder); };
            Disc* disc = app.disc();
            if (disc) config.program = [disc] { return disc->read_program(); };
            config.data_dir = app.data_dir();
            while (!config.data_dir.empty() && (config.data_dir.back() == '/' || config.data_dir.back() == '\\')) config.data_dir.pop_back();
            if (!projects_.empty()) config.language = projects_[size_t(std::clamp(project_sel_, 0, int(projects_.size()) - 1))].language;
            runtime_ = std::make_unique<menu::Runtime>([vfs](const std::string& path) { return vfs->read(path); }, config);
            if (card_ == "none") runtime_->game().card.connected = false;
            else if (card_ == "saved") runtime_->game().card.has_game_data = true, runtime_->game().username = "Player"; // a stand-in for a saved game
            if (credits_ >= 0) runtime_->game().money = credits_;
            app_ = &app;
            runtime_->on_sound = [&app](const menu::SoundCall& call) {
                if (audio::AppSound* sound = app.sound()) play_sound_call(sound->menu(), call);
            };
            live_drawer_ = std::make_unique<PageDrawer>(app);
            PageDrawer* drawer = live_drawer_.get();
            // The movie faces: played in their place; their events go to the scripts' event thread.
            movies_ = std::make_unique<MenuMovies>(app.renderer(), *vfs, app.audio().output_device_id());
            menu::Runtime* runtime = runtime_.get();
            movies_->on_event = [runtime](LiveWidget& face, const std::string& function) {
                auto e = std::make_shared<menu::Object>("MScriptEvent");
                e->set("module", menu::widget_value(face.self()));
                e->set("function", script::Value::string(function));
                runtime->push_event(menu::object_value(e));
            };
            MenuMovies* movies = movies_.get();
            cars_ = std::make_unique<MenuCars>(app);
            MenuCars* cars = cars_.get();
            models_ = std::make_unique<MenuModels>(app);
            MenuModels* models = models_.get();
            drawer->draw_live = [movies, cars, models](const LiveWidget& face, menu::Project* project, const std::string& page, const Rect& at, const SDL_FRect& dst, float opacity) {
                if (face.is("MovieFace")) movies->draw(face, dst, opacity);
                else if (face.is("CarFace")) cars->draw(face, project, page, at, dst, opacity);
                else if (face.is("ModelFace")) models->draw(face, project, page, at, dst, opacity);
            };
            menu::text_measure().measure = [drawer](const LiveWidget& face, float& w, float& h) { drawer->measure(face, w, h); };
            runtime_->boot();
            if (const script::Value root = runtime_->vm().lookup("MpegRoot"); root.kind == script::Kind::String) movies_->set_root(root.as_string());
            race_.reset();
            runtime_->start_project(from_boot || projects_.empty() ? runtime_->first_project() : projects_[size_t(project_sel_)].name);
        } catch (const std::exception& ex) {
            stop();
            error_ = ex.what();
            LOG_WARN("Menu viewer: %s", ex.what());
        }
    }
    void stop() {
        menu::text_measure().measure = nullptr;
        movies_.reset(), cars_.reset(), models_.reset(), live_drawer_.reset(), runtime_.reset(), race_.reset();
        if (app_)
            if (audio::AppSound* sound = app_->sound()) sound->stop();
    }

    void update_running(App& app, const MenuInput& m) {
        // Select and Start together leave the running menus.
        if (m.held(PadButton::Select) && m.pressed(PadButton::Start)) return stop();
        try {
            static const std::pair<PadButton, menu::Key> keys[] = {
                {PadButton::Cross, menu::Key::Accept}, {PadButton::Triangle, menu::Key::Cancel}, {PadButton::Start, menu::Key::Start},
                {PadButton::Select, menu::Key::Select}, {PadButton::L1, menu::Key::L1},         {PadButton::R1, menu::Key::R1},
                {PadButton::L2, menu::Key::L2},         {PadButton::R2, menu::Key::R2},         {PadButton::Square, menu::Key::Square},
            };
            if (m.vertical()) runtime_->key(m.vertical() < 0 ? menu::Key::Up : menu::Key::Down);
            if (m.horizontal()) runtime_->key(m.horizontal() < 0 ? menu::Key::Left : menu::Key::Right);
            for (const auto& [button, key] : keys)
                if (m.pressed(button)) runtime_->key(key);
            // The menus run at the program's 60 pictures a second, whatever the display's rate.
            const int steps = std::min(int(step_time_ * 60.0), 4);
            step_time_ = steps == 4 ? 0.0 : step_time_ - steps / 60.0;
            for (int i = 0; i < steps; ++i) runtime_->update();
            // A script that ended its project names the next one, or asks for a race.
            if (runtime_->finished() && !race_) {
                race_ = runtime_->race_request();
                if (!race_ && !runtime_->advance()) runtime_->note("the menus ended and asked for nothing");
                // A race with its own menus: the race screen runs them over the race on these menus
                // and leaves them where the race sequence ends (race_natives.h).
                if (race_ && !menu::quick_project(race_->sequence).empty() && app.gpu()) {
                    RaceStart start;
                    start.course = race_->course, start.car = race_->car, start.paint = race_->paint;
                    start.cars = std::max(1, race_->max_entry);
                    if (race_->laps > 0) start.laps = race_->laps;
                    start.menus = runtime_.get();
                    start.sequence = race_->sequence;
                    race_.reset();
                    if (audio::AppSound* sound = app.sound()) sound->menu().stop_music(), sound->menu().stop_stream();
                    app.push(make_race_screen(app, start));
                }
            } else if (race_ && m.pressed(PadButton::Cross)) {
                // The race the menus asked for, on the course as it is driven forward (a reversed
                // course's code is the course's with "r_" before it).
                RaceStart start;
                start.course = race_->course;
                if (!app.vfs()->exists("crs/" + start.course) && start.course.rfind("r_", 0) == 0) start.course = start.course.substr(2);
                start.car = race_->car, start.paint = race_->paint;
                start.cars = std::max(1, race_->max_entry);
                if (race_->laps > 0) start.laps = race_->laps;
                if (audio::AppSound* sound = app.sound()) sound->menu().stop_music(), sound->menu().stop_stream();
                app.push(make_race_screen(app, start));
            }
        } catch (const std::exception& ex) {
            runtime_->note(std::string("stopped: ") + ex.what());
        }
    }

    void render_running(Ui& ui) {
        const float scale = float(Ui::kHeight) / 480.0f, x = (Ui::kWidth - 640 * scale) / 2;
        ui.fill(0, 0, Ui::kWidth, Ui::kHeight, {0, 0, 0});
        const Uint64 t0 = SDL_GetTicksNS();
        if (live_drawer_) {
            if (movies_) movies_->begin_frame();
            if (cars_) cars_->begin_frame(runtime_->frame());
            if (models_) models_->begin_frame(runtime_->frame());
            for (const auto& page : runtime_->open_pages())
                if (menu::Project* project = runtime_->project_of(*page)) live_drawer_->draw(*project, *page, x, 0, scale, 0);
            if (movies_) movies_->end_frame();
            if (cars_) cars_->end_frame();
            if (models_) models_->end_frame();
            draw_cursor(x, scale);
            // The transition's colour over the pages.
            if (timing_) {
                // The pages' drawing (movies and 3D included) and the whole frame, in ms.
                const Uint64 t1 = SDL_GetTicksNS();
                draw_ms_ += double(t1 - t0) / 1e6;
                if (last_frame_) frame_ms_ += double(t1 - last_frame_) / 1e6, frame_max_ = std::max(frame_max_, double(t1 - last_frame_) / 1e6);
                last_frame_ = t1;
                if (++timed_ == 300) {
                    LOG_INFO("Menu timing on %s: frame %.2f ms (max %.2f), pages drawn in %.2f ms", runtime_->current_page() ? runtime_->current_page()->name.c_str() : "-",
                             frame_ms_ / 299, frame_max_, draw_ms_ / 300);
                    timed_ = 0, draw_ms_ = frame_ms_ = frame_max_ = 0, last_frame_ = 0;
                }
            }
            if (const float cover = runtime_->fade_cover(); cover > 0) ui.fill(x, 0, 640 * scale, 480 * scale, Color{0, 0, 0, u8(std::clamp(cover, 0.0f, 1.0f) * 255.0f)});
        }
        // What the scripts did, beside the page.
        const std::deque<std::string>& log = runtime_->log();
        const size_t lines = std::min<size_t>(log.size(), 26);
        for (size_t i = 0; i < lines; ++i) ui.text(2, 14 + float(i) * 12, Ui::clip(log[log.size() - lines + i], x - 4), colors::kDim);
        ui.text(2, 2, runtime_->project() ? runtime_->project()->name() : std::string("-"), colors::kAccent);
        ui.text(x + 640 * scale + 2, 2, "Select+Start:", colors::kDim);
        ui.text(x + 640 * scale + 2, 14, "stop", colors::kDim);
        if (race_) {
            // The menus' work is done: this is what a race would be started from.
            const float px = x + 60 * scale, py = 150 * scale, pw = 520 * scale;
            ui.fill(px, py, pw, 84, colors::kPanel);
            ui.text(px + 8, py + 6, "The menus ask for a race: " + race_->sequence, colors::kAccent);
            ui.text(px + 8, py + 22, "Course " + race_->course + (race_->race.empty() ? std::string() : "   event " + race_->race), colors::kText);
            ui.text(px + 8, py + 36, "Car " + race_->car + "   paint " + std::to_string(race_->paint) + "   " + std::to_string(race_->max_entry) + " cars", colors::kText);
            ui.text(px + 8, py + 50, std::string(race_->manual ? "Manual" : "Automatic") + " gears   difficulty " + race_->difficulty, colors::kText);
            ui.text(px + 8, py + 66, "Cross: drive it", colors::kDim);
        }
    }

    // The menus' pointer: the common project's cursor picture, its tip on the focused widget.
    // ❓ Where on the widget the program puts it (the layouts' cursor_align) is by eye: the
    // widget's right half, at mid height.
    void draw_cursor(float x, float scale) {
        if (!runtime_->cursor_visible() || !runtime_->common_project()) return;
        LiveWidget* focus = runtime_->focused();
        const std::shared_ptr<LiveWidget> root = runtime_->common_page("CursorRoot");
        const std::shared_ptr<LiveWidget> cursor = root ? root->child("Cursor") : nullptr;
        if (!focus || !cursor || !focus->shown()) return;
        const Rect at = focus->screen_rect();
        const std::optional<Rect> size = cursor->data.rect("geometry");
        const float w = size ? size->w : 32, h = size ? size->h : 32;
        LiveWidget pointer;
        pointer.data = cursor->data;
        pointer.set_property("x", script::Value::number(at.x + at.w * 0.75f));
        pointer.set_property("y", script::Value::number(at.y + at.h * 0.5f));
        pointer.set_property("w", script::Value::number(w));
        pointer.set_property("h", script::Value::number(h));
        pointer.name = root->name;
        LiveWidget page;
        page.name = root->name, page.data.type = "RootWindow";
        live_drawer_->draw_one(*runtime_->common_project(), root->name, pointer, x, 0, scale);
    }

    std::vector<Entry> projects_;
    std::unique_ptr<menu::Project> project_;
    std::unique_ptr<PageDrawer> drawer_;
    std::vector<std::shared_ptr<LiveWidget>> pages_;
    std::unique_ptr<menu::Runtime> runtime_;
    std::unique_ptr<PageDrawer> live_drawer_;
    std::unique_ptr<MenuMovies> movies_; // the running menus' movie faces
    std::unique_ptr<MenuCars> cars_;     // and car faces
    std::unique_ptr<MenuModels> models_; // and model faces
    int project_sel_ = 0, page_sel_ = 0, state_ = 0;
    bool full_ = false, start_run_ = false, start_boot_ = false;
    std::string error_, start_project_, start_page_, card_;
    double step_time_ = 0; // display time not yet stepped by the menus
    bool timing_ = false;
    int timed_ = 0;
    double draw_ms_ = 0, frame_ms_ = 0, frame_max_ = 0;
    Uint64 last_frame_ = 0;
    long long credits_ = -1; // a scripted run's credits; -1: the game's own
    std::optional<menu::RaceRequest> race_; // what the running menus asked for at their end
    App* app_ = nullptr;                    // set when the menus start running
};

} // namespace

std::unique_ptr<Screen> make_menu_viewer(App& app) { return std::make_unique<MenuViewer>(app); }

} // namespace gt4r

// ---- The running menus drawn over another screen (menu_painter.h).

#include "gt4r/app/menu_painter.h"

namespace gt4r {

namespace {

class RuntimePainter : public MenuPainter {
public:
    explicit RuntimePainter(App& app) : drawer_(app) {}
    void draw(menu::Runtime& rt, float x, float scale) override {
        for (const auto& page : rt.open_pages())
            if (menu::Project* project = rt.project_of(*page)) drawer_.draw(*project, *page, x, 0, scale, 0);
        // The pointer: the common project's cursor picture at the focused widget (as the viewer).
        if (rt.cursor_visible() && rt.common_project())
            if (LiveWidget* focus = rt.focused(); focus && focus->shown()) {
                const std::shared_ptr<LiveWidget> root = rt.common_page("CursorRoot");
                const std::shared_ptr<LiveWidget> cursor = root ? root->child("Cursor") : nullptr;
                if (cursor) {
                    const Rect at = focus->screen_rect();
                    const std::optional<Rect> size = cursor->data.rect("geometry");
                    LiveWidget pointer;
                    pointer.data = cursor->data;
                    pointer.set_property("x", script::Value::number(at.x + at.w * 0.75f));
                    pointer.set_property("y", script::Value::number(at.y + at.h * 0.5f));
                    pointer.set_property("w", script::Value::number(size ? size->w : 32));
                    pointer.set_property("h", script::Value::number(size ? size->h : 32));
                    pointer.name = root->name;
                    drawer_.draw_one(*rt.common_project(), root->name, pointer, x, 0, scale);
                }
            }
    }

private:
    PageDrawer drawer_;
};

} // namespace

std::unique_ptr<MenuPainter> make_menu_painter(App& app) { return std::make_unique<RuntimePainter>(app); }

void send_menu_keys(menu::Runtime& rt, const MenuInput& m) {
    static const std::pair<PadButton, menu::Key> keys[] = {
        {PadButton::Cross, menu::Key::Accept}, {PadButton::Triangle, menu::Key::Cancel}, {PadButton::Start, menu::Key::Start},
        {PadButton::Select, menu::Key::Select}, {PadButton::L1, menu::Key::L1},         {PadButton::R1, menu::Key::R1},
        {PadButton::L2, menu::Key::L2},         {PadButton::R2, menu::Key::R2},         {PadButton::Square, menu::Key::Square},
    };
    if (m.vertical()) rt.key(m.vertical() < 0 ? menu::Key::Up : menu::Key::Down);
    if (m.horizontal()) rt.key(m.horizontal() < 0 ? menu::Key::Left : menu::Key::Right);
    for (const auto& [button, key] : keys)
        if (m.pressed(button)) rt.key(key);
}

} // namespace gt4r
