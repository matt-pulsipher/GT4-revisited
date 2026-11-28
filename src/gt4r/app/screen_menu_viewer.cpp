// The menu pages as the game's files describe them: a project's layout drawn with its own
// pictures, text and fonts; and the menus running (menu::Runtime): the game's scripts on those
// pages, from the boot project on. How the properties are drawn is in docs/formats/menu.md.

#include "gt4r/app/app.h"
#include "gt4r/app/game_options.h"
#include "gt4r/app/page.h"
#include "gt4r/app/menu_cars.h"
#include "gt4r/app/menu_advertise.h"
#include "gt4r/app/menu_cursor.h"
#include "gt4r/app/menu_loading.h"
#include "gt4r/app/menu_models.h"
#include "gt4r/app/menu_movies.h"
#include "gt4r/app/screen_saves.h"
#include "gt4r/menu/app_button.h"
#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/keyboard_typing.h"
#include "app/text_keyboard.h"
#include "gt4r/menu/game_network.h"
#include "gt4r/menu/no_card.h"
#include "gt4r/app/menu_page_drawer.h"
#include "gt4r/app/logger_face.h"
#include "gt4r/app/menu_painter.h"
#include "gt4r/app/menu_pointer.h"
#include "gt4r/app/menu_slides.h"
#include "gt4r/app/online.h"
#include "gt4r/app/photo_faces.h"
#include "gt4r/app/presence.h"
#include "gt4r/net/lobby.h"

#include "base/log.h"
#include "formats/text/charset.h"
#include "formats/text/text_layout.h"
#include "platform/render_scale.h"
#include "gt4r/audio/app_sound.h"
#include "gt4r/menu/font.h"
#include "gt4r/menu/game_settings.h"
#include "gt4r/menu/project.h"
#include "gt4r/menu/race_natives.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/photo/photo_camera.h"
#include "gt4r/menu/native.h"
#include "gt4r/menu/runtime_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <map>
#include <set>

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
    // `player`: the game itself (make_game_screen): from power on, no lists, no log beside the
    // pages; leaving the menus leaves the screen.
    int title_frames_ = 0; // frames the game's title has been up (the update dialog waits for it: update_prompts)
    bool autodemo_ = false; // the title's time-out is playing its demonstrations (menu::autodemo_step)
    bool attract_replay_ = false; // the race asked for is one of them (RaceStart::attract)

    explicit MenuViewer(App& app, bool player = false, std::string new_card = {}, bool online = false) : player_(player), new_card_(std::move(new_card)) {
        start_boot_ = player;
        online_start_ = online;
        advertise_on_ = player && !online; // (scripted runs: GT4R_MENU advertise=1)
        const auto list = [&app](const std::string& folder) { return app.vfs()->list_dir(folder); };
        for (const std::string& name : menu::list_projects(list)) {
            const std::vector<std::string> languages = menu::project_languages(list, name);
            // (the disc's own language where the project has it: Europe's projects also hold DB, the text's ids)
            std::string language = languages.empty() ? std::string("US") : languages[0];
            if (Disc* disc = app.disc(); disc && std::find(languages.begin(), languages.end(), disc->info().language) != languages.end())
                language = disc->info().language;
            projects_.push_back({name, language});
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
                else if (key == "photo") start_photo_ = value; // a photo shoot on this course, of `car`
                else if (key == "car") start_car_ = value;
                else if (key == "advertise") advertise_on_ = value != "0";
            }
        }
    }

    bool follows_aspect() const override { return runtime_ || player_; }
    void on_event(App&, const SDL_Event& e) override {
        if (!typing_on_) return;
        if (e.type == SDL_EVENT_TEXT_INPUT && e.text.text) typing_.type(e.text.text);
        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
            const SDL_Keycode k = e.key.key;
            const bool enter = k == SDLK_RETURN || k == SDLK_KP_ENTER, back = k == SDLK_BACKSPACE;
            if (!enter && !back && !(k >= 0x20 && k < 0x7F)) return; // (arrows, Escape and the rest keep their menu meaning)
            if (e.type == SDL_EVENT_KEY_UP) return void(typing_held_.erase(int(e.key.scancode)));
            typing_held_.insert(int(e.key.scancode));
            if (enter && !e.key.repeat) typing_.type("\n");
            if (back) typing_.type("\b");
        }
    }
    gt3r::TouchLayout touch_layout() const override { return follows_aspect() ? gt3r::TouchLayout::Menu : gt3r::TouchLayout::None; } // (the game's menus: the pad)

    // The game's title (Arcade's top page): the update dialog may open over it; the Discord status says which menus run.
    std::pair<std::string, std::string> where() const {
        if (!runtime_ || !runtime_->project() || runtime_->open_pages().empty()) return {};
        return {runtime_->project()->name(), runtime_->open_pages().front()->data.name()};
    }
    bool update_prompts() const override { return player_ && title_frames_ >= 180; } // (the title shown a moment: faded in)
    bool presence(App&, net::PresenceText& t) override {
        if (!player_) return false;
        const auto [project, page] = where();
        t = net::menu_presence(project, page);
        if (const net::Lobby& l = net::lobby(); l.active()) { // the Multi-LAN Race's lobby, whatever page is up
            t.details = "Multi-LAN Race";
            t.state = l.host() ? "Hosting a lobby" : "In a lobby";
            t.party_size = l.count(), t.party_max = 6; // (six players at most: the pregrid's slots)
        }
        return !t.details.empty();
    }

    void update(App& app, double dt) override {
        step_time_ += dt;
        title_frames_ = where() == std::pair<std::string, std::string>{"arcade", "TopRoot"} ? title_frames_ + 1 : 0;
        const MenuInput& m = app.menu();
        if (start_boot_) {
            start_boot_ = false, start_project_.clear();
            run(app, true);
            if (runtime_ && !start_photo_.empty() && app.gpu()) {
                menu::RaceRequest q;
                q.sequence = "photo_shoot", q.course = start_photo_, q.car = start_car_;
                race_ = q;
                start_photo_.clear();
                return rt_photo(app);
            }
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
        if (player_) { // the menus left (or failed to start: the error shown until a key)
            if (error_.empty() || m.pressed(PadButton::Cross) || m.pressed(PadButton::Triangle)) app.pop();
            return;
        }
        if (m.pressed(PadButton::Triangle)) {
            if (full_) full_ = false;
            else if (project_) close();
            else app.pop();
            return;
        }
        if (m.pressed(PadButton::Square)) state_ = state_ == 0 ? kFocusBits : state_ == kFocusBits ? kActive : state_ == kActive ? kInsensitive : 0;
        if (!project_) {
            if (projects_.empty()) return;
            const bool clicked = mouse_.pick(app, 0, project_sel_, int(projects_.size()));
            const int page = m.pressed(PadButton::L1) ? -kRows : m.pressed(PadButton::R1) ? kRows : 0;
            project_sel_ = std::clamp(project_sel_ + m.vertical() + page, 0, int(projects_.size()) - 1);
            if (m.pressed(PadButton::Cross) || clicked) return open(app);
            if (m.pressed(PadButton::Start)) run(app, true);
            return;
        }
        const int pages = int(pages_.size());
        if (pages && !full_) mouse_.pick(app, 0, page_sel_, pages);
        if (pages) page_sel_ = std::clamp(page_sel_ + m.vertical() + (m.pressed(PadButton::L1) ? -kRows : 0), 0, pages - 1);
        if (m.pressed(PadButton::Cross)) full_ = !full_;
        if (m.pressed(PadButton::R1)) run(app, false);
    }

    void render(App& app, Ui& ui) override {
        if (runtime_) return render_running(ui);
        if (player_) {
            ui.fill(0, 0, ui.width(), Ui::kHeight, {0, 0, 0});
            float y = Ui::kHeight / 2 - 20;
            for (const std::string& line : Ui::wrap("The game could not start: " + error_, ui.width() - 80)) ui.text_center(y, line, colors::kBad), y += 12;
            ui.footer("{Cross}: back");
            return;
        }
        const char* states = state_ == kFocusBits ? "focused" : state_ == kActive ? "active" : state_ == kInsensitive ? "unavailable" : "at rest";
        if (project_ && full_ && page_sel_ < int(pages_.size())) {
            // The page over the whole height (4:3).
            const float scale = float(Ui::kHeight) / 480.0f;
            ui.fill(0, 0, ui.width(), Ui::kHeight, {0, 0, 0});
            drawer_->draw(*project_, *pages_[size_t(page_sel_)], (ui.width() - 640 * scale) / 2, 0, scale, state_);
            return;
        }
        ui.header("MENU PAGES", project_ ? project_->name() + "  (" + project_->module_name() + ", " + project_->language() + ")" : std::string());
        const int n = project_ ? int(pages_.size()) : int(projects_.size());
        const int sel = project_ ? page_sel_ : project_sel_;
        const int first = ui.list_window(n, sel, kRows), over = mouse_.over(app, 0);
        mouse_.begin(), mouse_.rows(0, first, std::min(kRows, n - first), 4, 24, 200, 12);
        for (int i = 0; i < kRows && first + i < n; ++i) {
            const int idx = first + i;
            const float y = 26 + float(i) * 12;
            if (idx == sel || idx == over) ui.fill(4, y - 2, 200, 12, idx == sel ? colors::kHighlight : colors::kPanel);
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
            if (disc) config.program_image = [disc] { return disc->program_image(); };
            if (disc) config.region = disc->info().region, config.product = disc->info().product; // (Prologue: "jp", its units, PS and kgfm)
            config.data_dir = app.save_dir();
            config.console_card = !card_.empty(); // (GT4R_MENU card=: the original's card pages)
            config.new_card = new_card_;
            config.gt3r_dir = gt3r_data_dir(), config.choose_earlier_save = true; // (the Status page's transfer: screen_earlier_saves.cpp)
            while (!config.data_dir.empty() && (config.data_dir.back() == '/' || config.data_dir.back() == '\\')) config.data_dir.pop_back();
            if (!projects_.empty()) config.language = projects_[size_t(std::clamp(project_sel_, 0, int(projects_.size()) - 1))].language;
            runtime_ = std::make_unique<menu::Runtime>([vfs](const std::string& path) { return vfs->read(path); }, config);
            if (card_ == "none") runtime_->game().card.connected = false;
            else if (card_ == "saved") runtime_->game().card.has_game_data = true, runtime_->game().username = "Player"; // a stand-in for a saved game
            if (credits_ >= 0) runtime_->game().money = credits_;
            app_ = &app;
            online_ = std::make_unique<OnlineMenus>(app, *runtime_);
            runtime_->stream_playing = [&app] { return app.sound() && app.sound()->menu().is_playing_stream(); };
            runtime_->on_sound = [&app](const menu::SoundCall& call) {
                audio::AppSound* sound = app.sound();
                // (The photo mode's camera: its shutter's effect bank.)
                if (sound && call.object == "sound" && call.method == "loadShutter") sound->library_ptr()->set_shutter(call.args.empty() ? std::string() : call.args[0].to_string());
                else if (sound) play_sound_call(sound->menu(), call);
            };
            // The options' menu volumes (game_settings.h).
            // Prologue's options have the music on or off only (its option object's menu_bgm, race_bgm, replay_bgm: no volume
            // property), and its music plays at full level (its saved states' music master 16383).
            const bool prologue_disc = disc && disc->info().game == Game::Prologue;
            if (prologue_disc)
                if (audio::AppSound* sound = app.sound()) sound->menu().set_music_option(1.0f);
            runtime_->on_options = [&app, prologue_disc](const menu::GameSettings& s) {
                if (audio::AppSound* sound = app.sound())
                    sound->menu().set_music_option(prologue_disc ? 1.0f : s.menu.music), sound->menu().set_effect_option(s.menu.effects);
            };
            live_drawer_ = std::make_unique<PageDrawer>(app);
            PageDrawer* drawer = live_drawer_.get();
            // The movie faces: played in their place; their events go to the scripts' event thread.
            movies_ = std::make_unique<MenuMovies>(app.renderer(), *vfs, app.audio().output_device_id(), app.audio().music_volume());
            menu::Runtime* runtime = runtime_.get();
            movies_->on_event = [runtime](LiveWidget& face, const std::string& function) {
                auto e = std::make_shared<menu::Object>("MScriptEvent");
                e->set("module", menu::widget_value(face.self()));
                e->set("function", script::Value::string(function));
                runtime->push_event(menu::object_value(e));
            };
            MenuMovies* movies = movies_.get();
            movies_->expand_alpha = [runtime] { return runtime->movie_expand_alpha; };
            cars_ = std::make_unique<MenuCars>(app);
            cars_->movies = movies_.get(), cars_->on_event = movies_->on_event;
            MenuCars* cars = cars_.get();
            models_ = std::make_unique<MenuModels>(app);
            loading_ = std::make_unique<MenuLoading>(app);
            if (from_boot && advertise_on_) advertise_ = std::make_unique<MenuAdvertise>(app);
            MenuModels* models = models_.get();
            slides_ = std::make_unique<MenuSlides>(app);
            MenuSlides* slides = slides_.get();
            if (!photos_) photos_ = std::make_unique<PhotoFaces>(app);
            PhotoFaces* photos = photos_.get();
            photos->picture = [this]() { return runtime_ ? runtime_->photo_picture.get() : nullptr; };
            slides->card_list = [this]() { return runtime_ ? menu::slide_folders(*runtime_) : std::vector<std::string>{}; };
            slides->card_photo = [this](const std::string& folder) { return runtime_ ? menu::slide_photo(*runtime_, folder) : std::nullopt; };
            drawer->draw_live = [movies, cars, models, slides, photos](const LiveWidget& face, menu::Project* project, const std::string& page, const Rect& at, const SDL_FRect& dst, float opacity) {
                if (face.is("MovieFace")) movies->draw(face, dst, opacity);
                else if (face.is("CarFace")) cars->draw(face, project, page, at, dst, opacity);
                else if (face.is("ModelFace")) models->draw(face, project, page, at, dst, opacity);
                else if (face.is("SlideShowFace")) slides->draw(face, at, dst, opacity);
                else if (face.is("PhotoRenderFace")) photos->draw(at, dst, opacity);
            };
            menu::text_measure().measure = [drawer](const LiveWidget& face, float& w, float& h) { drawer->measure(face, w, h); };
            cursor_ = {};
            runtime_->boot();
            if (const script::Value root = runtime_->vm().lookup("MpegRoot"); root.kind == script::Kind::String) movies_->set_root(root.as_string());
            if (const script::Value n = runtime_->vm().lookup("DemonstrationSlideNum"); n.is_number()) slides_->set_count(int(n.as_float()));
            race_.reset();
            // A new game chosen on the saved games' page: Gran Turismo 4's boot at its name entry (Prologue has no such page).
            if (from_boot && !new_card_.empty() && !runtime_->is_prologue()) runtime_->game().next_menu_arg = "EntryRoot";
            if (from_boot && online_start_ && online_->available()) {
                // The home screen's Online battle: Arcade's Multi-LAN Race asked for at once (the connection's page next), the saved
                // game loaded; leaving the connection or the lobby is leaving for the home screen.
                online_->from_home = true;
                menu::ask_for_lan_battle(*runtime_);
            } else {
                online_start_ = false;
                runtime_->start_project(from_boot || projects_.empty() ? runtime_->first_project() : projects_[size_t(project_sel_)].name);
            }
            new_card_.clear();
        } catch (const std::exception& ex) {
            stop();
            error_ = ex.what();
            LOG_WARN("Menu viewer: %s", ex.what());
        }
    }
    // The game from its start again with another game (`load`: its folder) or a new one (`new_card`), the one played saved before.
    void restart(const std::string& load, const std::string& new_card) {
        if (!runtime_ || !app_) return;
        App& app = *app_;
        menu::autosave(*runtime_);
        if (!load.empty())
            if (const auto n = menu::natives_of(*runtime_)) n->library().set_current(load);
        stop();
        new_card_ = new_card;
        advertise_on_ = false, skip_opening_ = true;
        run(app, true);
    }
    void stop() {
        typing_stop();
        if (held_) SDL_DestroyTexture(held_), held_ = nullptr;
        if (trail_) SDL_DestroyTexture(trail_), trail_ = nullptr;
        if (page_picture_) SDL_DestroyTexture(page_picture_), page_picture_ = nullptr;
        menu::text_measure().measure = nullptr;
        movies_.reset(), cars_.reset(), models_.reset(), slides_.reset(), loading_.reset(), advertise_.reset(), live_drawer_.reset(), online_.reset(), runtime_.reset(), race_.reset();
        if (app_)
            if (audio::AppSound* sound = app_->sound()) sound->stop();
    }

    void update_running(App& app, const MenuInput& m) {
        // Mods installed or switched since: their new cars into the menus' database and makers' lists (an online download).
        if (app.mods_generation() != mods_seen_) {
            mods_seen_ = app.mods_generation();
            if (runtime_)
                if (const std::vector<std::string> added = menu::refresh_mod_cars(*runtime_); !added.empty())
                    LOG_INFO("Menus: %zu mod cars added (%s ...)", added.size(), added[0].c_str());
        }
        // The developer's viewer: Select and Start together stop the running menus.
        if (!player_ && m.held(PadButton::Select) && m.pressed(PadButton::Start)) return stop();
        // The game: the GT4R menu button beside the title or the map (a click or a tap) leaves it for GT4 Revisited's own menu underneath; the
        // Controller Settings button in place of the Options' button configurations opens ours over them.
        if (menu_button_.w > 0)
            for (const gt3r::PointerInput::Action& a : app.pointer())
                if (a.kind == gt3r::PointerInput::Action::Kind::Click && menu_button_.contains(a.x, a.y)) return use_button(app);
        try {
            static const std::pair<PadButton, menu::Key> keys[] = {
                {PadButton::Cross, menu::Key::Accept}, {PadButton::Triangle, menu::Key::Cancel}, {PadButton::Start, menu::Key::Start},
                {PadButton::Select, menu::Key::Select}, {PadButton::L1, menu::Key::L1},         {PadButton::R1, menu::Key::R1},
                {PadButton::L2, menu::Key::L2},         {PadButton::R2, menu::Key::R2},         {PadButton::Square, menu::Key::Cancel},
            };
            // The pictures before the menus: the menus start after them.
            if (advertise_ && advertise_->active()) {
                const int n = std::min(int(step_time_ * 60.0), 4);
                step_time_ = n == 4 ? 0.0 : step_time_ - n / 60.0;
                advertise_->step(float(n) / 60.0f);
                return;
            }
            // (The loading page takes no keys; nor do the pages while the players' panel has them.)
            const bool panel = online_ && online_->frame(app, m);
            const bool keys_on = !panel && (!loading_ || !loading_->active());
            // Select does the same as the button (where the page's scripts do nothing with it).
            if (player_ && keys_on && m.pressed(PadButton::Select) && select_is_ours()) return use_button(app);
            // A button configuration: its table is hidden (hide_key_table); Cross is the Controller Settings button, Triangle
            // (and Square) the page's Cancel, back to the Options; nothing else reaches the page.
            const bool key_page = player_ && way() == Way::Keys;
            if (key_page) {
                hide_key_table();
                if (keys_on && m.pressed(PadButton::Cross)) return use_button(app);
            }
            // A page with a keyboard of its own (the name entry): a real keyboard types on it (menu/keyboard_typing.h); a typing key
            // held is no menu key meanwhile.
            typing_frame(app);
            const bool typing_keys = typing_on_ && (!typing_held_.empty() || typing_.pending());
            send_menu_directions(*runtime_, m, keys_on && !key_page);
            for (const auto& [button, key] : keys)
                if (keys_on && !typing_keys && m.pressed(button) && (!key_page || key == menu::Key::Cancel)) runtime_->key(key);
            if (keys_on && typing_on_) typing_.step(*runtime_);
            // The mouse and fingers on the shown page (app/menu_pointer.h), where it was drawn last.
            if (keys_on && !key_page && live_drawer_) menu_pointer_frame(app, *runtime_, boxes_, drag_, page_x_, page_scale_);
            // The menus run at the program's 60 pictures a second, whatever the display's rate.
            const int steps = std::min(int(step_time_ * 60.0), 4);
            step_time_ = steps == 4 ? 0.0 : step_time_ - steps / 60.0;
            const Uint64 u0 = SDL_GetTicksNS();
            const menu::Runtime::Pad pad = menu_runtime_pad(app.menu_pad());
            for (int i = 0; i < steps; ++i) runtime_->set_pad(pad), runtime_->update(), cursor_.step(*runtime_);
            if (loading_) loading_->step(float(steps) / 60.0f);
            if (timing_ && double(SDL_GetTicksNS() - u0) / 1e6 > 20) LOG_INFO("Menu timing: the scripts took %.1f ms", double(SDL_GetTicksNS() - u0) / 1e6);
            // The connection before the LAN Battle's pages, and its race (online.h).
            if (online_ && online_->finished(app)) return;
            // Opened by the home screen's Online battle: back from the connection, or the lobby left or ended, the home screen.
            if (online_start_ && !net::lobby().active() && runtime_->finished() && !runtime_->race_request() && runtime_->wanted_project() == "arcade")
                return menu::autosave(*runtime_), stop();
            // Started again from the saved games' page: without the opening movie.
            if (skip_opening_ && !runtime_->finished() && runtime_->current_page() && runtime_->current_page()->name == "MovieRoot") skip_opening_ = false, runtime_->key(menu::Key::Start);
            // The saved games' page (the title's Save Game, Prologue's save and load items).
            if (runtime_->take_saves_asked()) {
                SavesHooks hooks;
                hooks.save_now = [this]() { return menu::save_game_now(*runtime_); };
                hooks.load = [this](const std::string& card) { restart(card, {}); };
                hooks.new_game = [this](const std::string& card) { restart({}, card); };
                app.push(make_saves_screen(app, hooks));
                return;
            }
            // The Status page's transfer asks which earlier game's save to take.
            if (std::optional<menu::TransferChoice> choice = menu::take_transfer_choice(*runtime_)) {
                std::weak_ptr<int> life = life_;
                app.push(make_earlier_saves_screen(std::move(*choice), [this, life](std::optional<save::EarlierGame> g) {
                    if (life.lock() && runtime_) menu::answer_transfer_choice(*runtime_, std::move(g));
                }));
                return;
            }
            // A script that ended its project names the next one, or asks for a race.
            if (runtime_->finished() && (!race_ || (loading_ && loading_->active()))) {
                if (!race_) race_ = runtime_->race_request();
                // The title left alone (its `timeout_count_to_autodemo`, 20 s) asks for the
                // "autodemo" sequence (0x10BE20): its demonstrations one after another
                // (menu::autodemo_step: the opening movie, demo_movie after the Polyphony logo,
                // when the movie's count has run out; else the disc's recorded races of its
                // demonstration list, played as the Replay Theater plays them, a key ending the
                // time-out), then the project the title is in (Gran Turismo 4's arcade, Prologue's prologue). Prologue's
                // sequence (0x10A1F0) plays its own list: its replays and its opening movie (menu::autodemo_step).
                menu::GameState& g = runtime_->game();
                auto run_demo = [&](const menu::DemoStep& step) {
                    g.menu_result.clear();
                    if (!step.replay.empty()) {
                        menu::RaceRequest r;
                        r.sequence = "replay", r.replay_file = step.replay;
                        g.replay_file = step.replay;
                        race_ = r, attract_replay_ = true;
                        return;
                    }
                    g.next_menu_arg = step.arg;
                    runtime_->start_project(step.project);
                    g.next_menu_project.clear(), g.next_menu_arg.clear();
                };
                if (race_ && race_->sequence == "autodemo") {
                    race_.reset();
                    // Then the project the game was on (the sequence keeps next_menu_project as it found it: the project that
                    // ran, which ours clears once it starts; 0x10BE20 / Prologue's 0x10A1F0 with 0x3FC640): the title's.
                    const std::string back = !g.next_menu_project.empty() ? g.next_menu_project : runtime_->project() ? runtime_->project()->name() : std::string("arcade");
                    g.last_menu_project = runtime_->project() ? runtime_->project()->name() : std::string();
                    g.last_sequence = g.next_sequence, g.next_sequence.clear();
                    g.major_menu_project = back;
                    if (const auto step = menu::autodemo_step(*runtime_, true)) autodemo_ = true, run_demo(*step);
                    else g.next_menu_project = back; // (nothing played: the title again, 0x42FC50)
                } else if (!race_ && autodemo_) {
                    // A demonstration's project ended: the sequence's next, or the project it named.
                    const bool key = g.menu_result == "terminate"; // (left with a key: the title's wait starts from it)
                    if (const auto step = menu::autodemo_step(*runtime_, false)) run_demo(*step);
                    else autodemo_ = false, key ? runtime_->note_input() : void();
                }
                if (!race_ && !runtime_->finished()) {
                } else if (!race_) {
                    // Between two projects, the loading page: the next project starts once its
                    // black covers the last page, and the page fades out over it.
                    if (loading_ && !loading_->active()) loading_->set_quick(app.video().quick_loading), loading_->begin(unsigned(runtime_->frame() / 7));
                    if (!loading_ || loading_->covered()) {
                        if (!runtime_->advance()) runtime_->note("the menus ended and asked for nothing");
                        if (loading_) loading_->end();
                    }
                }
                // A photo shoot runs its own project on these menus (screen_photo.cpp).
                if (race_ && race_->sequence == "photo_shoot" && app.gpu()) {
                    rt_photo(app);
                    return;
                }
                // A race with its own menus: the race screen runs them over the race on these menus
                // and leaves them where the race sequence ends (race_natives.h).
                if (race_ && (!menu::quick_project(race_->sequence).empty() || race_->sequence == "replay" || race_->sequence == "race_view" || race_->sequence == "split_battle" || race_->sequence == "photo_develop") && app.gpu()) {
                    // The loading page before it, when the scripts asked for the last page held.
                    if (loading_ && runtime_->game().loading_mode == "capture") {
                        if (!loading_->active()) loading_->set_quick(app.video().quick_loading), loading_->begin_capture();
                        if (!loading_->covered()) return;
                        loading_->stop();
                        runtime_->game().loading_mode = "default"; // (the race's menus', quick.pp)
                    }
                    RaceStart start;
                    start.course = race_->course, start.car = race_->car, start.paint = race_->paint;
                    start.cars = std::max(1, race_->max_entry);
                    if (race_->laps > 0) start.laps = race_->laps;
                    start.menus = runtime_.get();
                    start.sequence = race_->sequence;
                    start.attract = std::exchange(attract_replay_, false);
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

    void rt_photo(App& app) {
        const menu::RaceRequest request = *race_;
        race_.reset();
        if (audio::AppSound* sound = app.sound()) sound->menu().stop_music(), sound->menu().stop_stream();
        app.push(make_photo_screen(app, *runtime_, request));
    }

    void render_running(Ui& ui) {
        const float scale = float(Ui::kHeight) / 480.0f, x = (ui.width() - 640 * scale) / 2;
        ui.fill(0, 0, ui.width(), Ui::kHeight, {0, 0, 0});
        if (player_ && way() == Way::Keys) hide_key_table(); // (from the page's first picture)
        const Uint64 t0 = SDL_GetTicksNS();
        if (advertise_ && advertise_->active()) return advertise_->draw(x, scale);
        if (live_drawer_) {
            if (movies_) movies_->begin_frame();
            if (cars_) cars_->begin_frame(runtime_->frame());
            if (models_) models_->begin_frame(runtime_->frame());
            if (slides_) {
                auto option = [this](const char* name, int otherwise) {
                    const auto& o = runtime_->game().options;
                    const auto it = o.find(name);
                    return it != o.end() && std::holds_alternative<s32>(it->second) ? int(std::get<s32>(it->second)) : otherwise;
                };
                slides_->set_options(option("album_slide_play_time", 8), option("album_slide_transition_time", 4), option("album_slide_effect", 1) != 0);
                slides_->begin_frame(runtime_->frame());
            }
            const menu::TransitionPicture tp = menu::transition_picture(runtime_->vm().attribute(runtime_->context(), "transition"));
            // A cross transition coming in: the pages grown about the screen's middle.
            Affine grown;
            grown.a = grown.d = tp.page_scale, grown.e = 320 * (1 - tp.page_scale), grown.f = 240 * (1 - tp.page_scale);
            live_drawer_->menu_frame = runtime_->frame();
            // A transition that shows an earlier picture (blocks, a trail) and the capture loading page need the pages as a
            // picture: then they are drawn into one of their own, in the screen's pixels.
            const bool hold = loading_ && !loading_->active() && runtime_->game().loading_mode == "capture" && !runtime_->finished();
            SDL_Renderer* r = app_->renderer();
            SDL_FRect shown{};
            const float pixels = SDL_GetRenderLogicalPresentationRect(r, &shown) && shown.w > 0 ? shown.w / float(Ui::kWidth) : 1.0f;
            const int tw = std::max(1, int(640 * scale * pixels + 0.5f)), th = std::max(1, int(480 * scale * pixels + 0.5f));
            const bool own = (hold || tp.trail || tp.blocks) && page_target(page_picture_, tw, th);
            SDL_Texture* before = SDL_GetRenderTarget(r);
            const float ox = own ? 0 : x, ps = own ? float(tw) / 640 : scale;
            if (own) {
                SDL_SetRenderTarget(r, page_picture_);
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
                SDL_RenderClear(r);
            }
            boxes_.clear(), live_drawer_->boxes = &boxes_, page_x_ = x, page_scale_ = scale; // (what the pointer is over)
            for (const auto& page : runtime_->open_pages())
                if (menu::Project* project = runtime_->project_of(*page)) live_drawer_->draw(*project, *page, ox, 0, ps, 0, grown, tp.page_opacity);
            live_drawer_->boxes = nullptr;
            if (movies_) movies_->end_frame();
            if (cars_) cars_->end_frame();
            if (models_) models_->end_frame();
            if (slides_) slides_->end_frame();
            if (own) {
                draw_trail(tp, ps);
                SDL_SetRenderTarget(r, before);
                const SDL_FRect dst{x, 0, 640 * scale, 480 * scale};
                SDL_SetTextureBlendMode(page_picture_, SDL_BLENDMODE_NONE);
                SDL_SetTextureAlphaMod(page_picture_, 255);
                SDL_RenderTexture(r, page_picture_, nullptr, &dst);
                if (hold) loading_->keep(page_picture_);
            }
            if (!(player_ && way() == Way::Keys)) draw_cursor(x, scale); // (the game's arrow points at the hidden table)
            draw_blocks(tp, x, scale);
            if (tp.trail && own) std::swap(page_picture_, trail_);
            else if (trail_) SDL_DestroyTexture(trail_), trail_ = nullptr;
            if (!own && page_picture_) SDL_DestroyTexture(page_picture_), page_picture_ = nullptr;
            // The transition's colour over the pages.
            if (timing_) {
                // The pages' drawing (movies and 3D included) and the whole frame, in ms.
                const Uint64 t1 = SDL_GetTicksNS();
                draw_ms_ += double(t1 - t0) / 1e6;
                if (last_frame_) frame_ms_ += double(t1 - last_frame_) / 1e6, frame_max_ = std::max(frame_max_, double(t1 - last_frame_) / 1e6);
                if (last_frame_ && double(t1 - last_frame_) / 1e6 > 33.4)
                    LOG_INFO("Menu timing: a frame of %.1f ms on %s (pages drawn in %.1f ms)", double(t1 - last_frame_) / 1e6,
                             runtime_->current_page() ? runtime_->current_page()->name.c_str() : "-", double(t1 - t0) / 1e6);
                last_frame_ = t1;
                if (++timed_ == 300) {
                    LOG_INFO("Menu timing on %s: frame %.2f ms (max %.2f), pages drawn in %.2f ms", runtime_->current_page() ? runtime_->current_page()->name.c_str() : "-",
                             frame_ms_ / 299, frame_max_, draw_ms_ / 300);
                    timed_ = 0, draw_ms_ = frame_ms_ = frame_max_ = 0, last_frame_ = 0;
                }
            }
            if (const float cover = runtime_->fade_cover(); cover > 0) ui.fill(x, 0, 640 * scale, 480 * scale, Color{0, 0, 0, u8(std::clamp(cover, 0.0f, 1.0f) * 255.0f)});
            if (loading_) loading_->draw(x, scale);
            if (online_) online_->render(*app_, ui, x, scale);
        }
        // What the scripts did, beside the page (the game itself: only what stopped them).
        const std::deque<std::string>& log = runtime_->log();
        if (player_) {
            float y = 2;
            for (size_t i = log.size() > 6 ? log.size() - 6 : 0; i < log.size(); ++i)
                if (log[i].find("stopped:") != std::string::npos || log[i].find("script error") != std::string::npos)
                    for (const std::string& line : Ui::wrap(log[i], x - 4)) ui.text(2, y, line, colors::kBad), y += 12;
        }
        if (player_) draw_menu_button(ui, x, scale);
        const size_t lines = player_ ? 0 : std::min<size_t>(log.size(), 26);
        for (size_t i = 0; i < lines; ++i) ui.text(2, 14 + float(i) * 12, Ui::clip(log[log.size() - lines + i], x - 4), colors::kDim);
        if (!player_) {
            ui.text(2, 2, runtime_->project() ? runtime_->project()->name() : std::string("-"), colors::kAccent);
            ui.text(x + 640 * scale + 2, 2, "Select+Start:", colors::kDim);
            ui.text(x + 640 * scale + 2, 14, "stop", colors::kDim);
        }
        if (race_ && !(loading_ && loading_->active())) {
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

    // The page's project and name (empty: none).
    std::pair<std::string, std::string> page_id() const {
        const LiveWidget* page = runtime_ ? runtime_->current_page() : nullptr;
        if (!page || !runtime_->project()) return {};
        return {runtime_->project()->name(), page->name};
    }
    // Prologue's board with the focus in its ring of signs (its Blocks).
    bool in_ring() const {
        const LiveWidget* page = runtime_ ? runtime_->current_page() : nullptr;
        if (!page) return false;
        const std::shared_ptr<LiveWidget> focus = page->focus.lock();
        for (const LiveWidget* w = focus.get(); w && w != page; w = w->parent_widget())
            if (w->name == "Blocks") return true;
        return false;
    }

    // The pages with a button of ours (menu/app_button.h): the title and Gran Turismo mode's map (GT4R menu beside it: our menu
    // underneath the game) and the Options' controller pages, the button configurations of every device and both players
    // (KeyconfigRoot), Vibration (ControllerRoot) and Steering (SteeringRoot): Controller Settings in place of their tables, our
    // Controls over the game, which shows and sets what those pages set (app/game_options.h). Their model is narrower than our
    // controls (keyboard, touch, triggers, wheels' own buttons), so the page shows only the way to ours, as GT3 Revisited's Key
    // Configuration page does.
    enum class Way { None, Menu, Keys };
    Way way() const {
        const auto [project, page] = page_id();
        const menu::AppButton b = menu::app_button(project, page);
        return b == menu::AppButton::Menu ? Way::Menu : b == menu::AppButton::Keys ? Way::Keys : Way::None;
    }
    // Select does what the button does where the page's scripts do nothing with it.
    bool select_is_ours() const {
        const auto [project, page] = page_id();
        return menu::select_is_app_button(project, page, in_ring());
    }
    void use_button(App& app) {
        // Leaving for our menu: the game saved first as the game saves itself (menu::autosave: when it changed since it was last
        // saved or loaded, the options' Autosave on), so nothing done since the last save is lost.
        if (way() == Way::Menu) return menu::autosave(*runtime_), stop();
        // A button configuration's port (0 the first player's, 1 the second's) and device ("DUALSHOCK 2", or a wheel's: "CHEETAH"
        // the Driving Force Pro, "JAGUAR" the Driving Force, "COUGAR" the GT FORCE): that player's controls, at the wheel's settings
        // for a wheel. Vibration and Steering (both players' on one page): the first player's, at the rumble or the steering.
        const std::string page = runtime_->current_page()->name;
        int player = 0;
        ControlsSection section = page == "ControllerRoot" ? ControlsSection::Rumble : page == "SteeringRoot" ? ControlsSection::Steering : ControlsSection::Top;
        if (page == "KeyconfigRoot") {
            const script::Value port = runtime_->vm().lookup("OptionProject::KeyconfigRoot::port");
            const script::Value device = runtime_->vm().lookup("OptionProject::KeyconfigRoot::peripheral");
            player = port.is_number() && port.as_float() == 1 ? 1 : 0;
            if (device.kind == script::Kind::String && device.as_string() != "DUALSHOCK 2") section = ControlsSection::Wheel;
        }
        app.push(make_controls_screen(app, player, true, section, running_game_options(runtime_->game())));
    }
    // The page's table, its buttons (Cancel, Default, OK) and its tool tip hidden: the page's frame and title stay.
    void hide_key_table() {
        LiveWidget* page = runtime_->current_page();
        if (const std::shared_ptr<LiveWidget> pane = page->child("Pane"))
            for (const std::shared_ptr<LiveWidget>& c : pane->children)
                if (c->name != "bg" && c->name != "Title" && c->visible()) c->set_property("visible", script::Value::integer(0));
        if (const std::shared_ptr<LiveWidget> tip = page->child("ToolTip"); tip && tip->visible()) tip->set_property("visible", script::Value::integer(0));
    }

    // The button beside the page (UI units; Select does the same): at the bottom of the bar left of the page when
    // there is room (the game area's own, else the window's beside a game area that has none), else in the window's bar under
    // the page, else over the page's bottom left corner (menu::app_button_top_over_page); moved up clear of the on-screen
    // controls.
    // `whole`: outside the game area, drawn across the whole window.
    gt3r::UiRect menu_button_rect(App& app, const Ui& ui, float x, bool& whole) const {
        constexpr float kMinBar = 60, kH = Ui::kHeight;
        float wx0 = 0, wy1 = kH; // the window's left edge and bottom, in UI units
        float safe_x0 = -1e9f;     // phones and tablets: the left edge clear of an iPhone's camera housing and the rounded corners
        int ww = 0, wh = 0;
        if (SDL_GetWindowSize(SDL_GetRenderWindow(app.renderer()), &ww, &wh) && ww > 0 && wh > 0) {
            float y0 = 0, x1 = 0;
            if (!SDL_RenderCoordinatesFromWindow(app.renderer(), 0, 0, &wx0, &y0) || !SDL_RenderCoordinatesFromWindow(app.renderer(), float(ww), float(wh), &x1, &wy1))
                wx0 = 0, wy1 = kH;
            if (gt3r::platform::is_mobile()) {
                const SDL_FRect sa = app.safe_area_window();
                float sx = 0, sy = 0;
                if (SDL_RenderCoordinatesFromWindow(app.renderer(), sa.x, sa.y, &sx, &sy)) safe_x0 = sx;
            }
        }
        wx0 = std::min(wx0, 0.0f), wy1 = std::max(wy1, kH);
        const float left = std::max(x >= kMinBar ? 0.0f : wx0, safe_x0); // the bar's left edge
        gt3r::UiRect b;
        if (x - left >= kMinBar) b = {left + 6, kH - 46, std::min(x - left - 12, 90.0f), 22};
        else if (wy1 - kH >= 34) b = {x + 6, kH + 6, 90, 22};
        else b = {x + 6, std::round(menu::app_button_top_over_page(page_id().first, page_id().second) * kH / 480), 74, 22};
        const gt3r::TouchRect r = app.touch_overlay().clear_above({b.x, b.y, b.w, b.h});
        b = {r.x, r.y, r.w, r.h};
        whole = b.x < 0 || b.y < 0 || b.right() > ui.width() || b.bottom() > kH;
        return b;
    }

    // Controller Settings in the middle of the page's frame (its Pane), focused: Cross, Select, a click or a tap opens our controls.
    void draw_key_button(Ui& ui, float x, float scale) {
        menu::Rect pane{130, 20, 380, 406}; // (the button configuration's)
        if (const std::shared_ptr<LiveWidget> p = runtime_->current_page()->child("Pane"))
            if (const std::optional<menu::Rect> g = p->data.rect("geometry")) pane = *g;
        const float cx = pane.x + pane.w / 2, cy = pane.y + std::max(pane.h, 120.0f) / 2;
        const gt3r::UiRect b = menu_button_ = {x + (cx - 110) * scale, (cy - 22) * scale, 220 * scale, 44 * scale};
        float mx = 0, my = 0;
        const bool hover = app_->mouse_at(mx, my) && b.contains(mx, my);
        ui.button(b, "Controller Settings", true, hover, true);
    }

    void draw_menu_button(Ui& ui, float x, float scale) {
        menu_button_ = {};
        const Way w = way();
        if (w == Way::None || (loading_ && loading_->active())) return;
        if (w == Way::Keys) return draw_key_button(ui, x, scale);
        bool whole = false;
        const gt3r::UiRect b = menu_button_ = menu_button_rect(*app_, ui, x, whole);
        float mx = 0, my = 0;
        const bool hover = app_->mouse_at(mx, my) && b.contains(mx, my);
        const bool prompt = !app_->touch_ui() && select_is_ours(); // (a touch screen's Select is one of the on-screen buttons)
        auto draw = [&](float dx, float dy) {
            const gt3r::UiRect d{b.x + dx, b.y + dy, b.w, b.h};
            ui.button(d, "GT4R menu", hover, false);
            if (!prompt) return;
            // Under it in the bar beside the page; beside it over the page (clear of the copyright line) and in the bar under it.
            if (b.right() <= x && b.bottom() + 14 <= Ui::kHeight) ui.text_center_in(d.x, d.w, d.bottom() + 3, "{Select}", colors::kDim);
            else ui.text(d.right() + 5, d.y + 5, "{Select}", colors::kDim);
        };
        if (whole) app_->touch_overlay().draw_whole_window(ui, draw);
        else draw(0, 0);
    }

    // A block transition: the screen held when it started, in blocks that fade by their timers
    // (32 x 24 of 20 x 20 page pixels).
    void draw_blocks(const menu::TransitionPicture& tp, float x, float scale) {
        if (!tp.blocks) {
            if (held_) SDL_DestroyTexture(held_), held_ = nullptr;
            return;
        }
        SDL_Renderer* r = app_->renderer();
        if ((tp.capture || !held_) && page_picture_ && page_target(held_, page_picture_->w, page_picture_->h)) {
            SDL_Texture* before = SDL_GetRenderTarget(r);
            SDL_SetRenderTarget(r, held_);
            SDL_SetTextureBlendMode(page_picture_, SDL_BLENDMODE_NONE);
            SDL_RenderTexture(r, page_picture_, nullptr, nullptr);
            SDL_SetRenderTarget(r, before);
        }
        if (!held_) return;
        float tw = 1, th = 1;
        SDL_GetTextureSize(held_, &tw, &th);
        SDL_SetTextureBlendMode(held_, SDL_BLENDMODE_BLEND);
        for (int row = 0; row < 24; ++row)
            for (int col = 0; col < 32; ++col) {
                const float timer = (*tp.blocks)[size_t(row * 32 + col)];
                if (timer <= 0) continue;
                SDL_SetTextureAlphaMod(held_, u8(std::min(timer, 1.0f) * 255));
                const SDL_FRect src{tw * float(col) / 32, th * float(row) / 24, tw / 32, th / 24};
                const SDL_FRect dst{x + float(col) * 20 * scale, float(row) * 20 * scale, 20 * scale, 20 * scale};
                SDL_RenderTexture(r, held_, &src, &dst);
            }
    }

    // A cross transition's trail (0x2182E0): while the pages come in, the picture shown last is
    // drawn over the one just drawn at the transition's weight, from its rectangle cut by
    // `magnify_diff` page pixels a side (so magnified), then black at 0.1 (`with_black`); the
    // result is the next picture's last. (The first picture of the trail has none before it.)
    void draw_trail(const menu::TransitionPicture& tp, float scale) {
        if (!tp.trail || !trail_) return;
        SDL_Renderer* r = app_->renderer();
        const float tw = float(trail_->w), th = float(trail_->h);
        const float cut = std::clamp(tp.trail_diff * scale, 0.0f, std::min(tw, th) / 2 - 1);
        const SDL_FRect src{cut, cut * th / tw, tw - 2 * cut, th - 2 * cut * th / tw};
        SDL_SetTextureBlendMode(trail_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(trail_, u8(tp.trail_ratio * 255));
        SDL_RenderTexture(r, trail_, &src, nullptr);
        if (tp.trail_black) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 0, 0, 0, u8(0.1f * 255));
            SDL_RenderFillRect(r, nullptr);
        }
    }
    // A texture the pages are drawn into, of this size (made when it has another).
    bool page_target(SDL_Texture*& t, int w, int h) {
        if (t && (t->w != w || t->h != h)) SDL_DestroyTexture(t), t = nullptr;
        if (!t && (t = SDL_CreateTexture(app_->renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h))) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        return t != nullptr;
    }

    // The menus' pointer (menu_cursor.h).
    void draw_cursor(float x, float scale) { cursor_.draw(*runtime_, *live_drawer_, x, scale); }

    std::vector<Entry> projects_;
    std::unique_ptr<menu::Project> project_;
    std::unique_ptr<PageDrawer> drawer_;
    std::vector<std::shared_ptr<LiveWidget>> pages_;
    RowPointer mouse_; // the lists'
    std::unique_ptr<menu::Runtime> runtime_;
    u32 mods_seen_ = 0; // App::mods_generation when the menus last looked
    // Typing on the page's own keyboard: SDL's text input on while the page has one (the engine's start_text_input with the
    // game's keyboard: the system's screen keyboard stays closed), the letters and the typing keys held.
    menu::KeyboardTyping typing_;
    bool typing_on_ = false;
    SDL_Window* typing_window_ = nullptr;
    std::set<int> typing_held_;
    void typing_frame(App& app) {
        const bool want = runtime_ && menu::KeyboardTyping::keyboard_of(runtime_->current_page()) != nullptr;
        if (want == typing_on_) return;
        if (!want) return typing_stop();
        typing_window_ = SDL_GetRenderWindow(app.renderer());
#if defined(SDL_PLATFORM_IOS)
        // (iOS opens its screen keyboard with any text input, whatever the hint: only with a keyboard connected, whose keys
        // then type; without one the game's keyboard is tapped and the system's stays closed, as on Android)
        if (SDL_HasKeyboard())
#endif
            gt3r::start_text_input(typing_window_, true);
        typing_on_ = true;
    }
    void typing_stop() {
        if (typing_on_) gt3r::stop_text_input(typing_window_);
        typing_on_ = false, typing_window_ = nullptr;
        typing_held_.clear(), typing_.clear();
    }
    std::shared_ptr<int> life_ = std::make_shared<int>(0); // (what outlives this page, such as the transfer's choice, asks it whether it is still there)
    std::unique_ptr<OnlineMenus> online_; // the online races' part of the menus
    std::unique_ptr<PageDrawer> live_drawer_;
    std::unique_ptr<MenuMovies> movies_; // the running menus' movie faces
    std::string new_card_;               // a new game to make there (started so)
    bool skip_opening_ = false;
    bool online_start_ = false; // the home screen's Online battle: the connection first, the home screen after
    std::unique_ptr<MenuCars> cars_;     // and car faces
    std::unique_ptr<MenuModels> models_; // and model faces
    std::unique_ptr<MenuSlides> slides_; // and slide shows
    std::unique_ptr<MenuLoading> loading_; // the page between projects
    std::unique_ptr<MenuAdvertise> advertise_; // the pictures before the first menus
    bool advertise_on_ = false;
    MenuCursor cursor_;
    std::vector<menu::DrawnBox> boxes_; // the pages' widgets as last drawn, and where (app/menu_pointer.h)
    menu::PointerDrag drag_;
    float page_x_ = 0, page_scale_ = 0;
    SDL_Texture* page_picture_ = nullptr; // the pages as a picture, while something needs it
    SDL_Texture* held_ = nullptr; // a block transition's screen
    SDL_Texture* trail_ = nullptr; // a cross transition's last picture
    int project_sel_ = 0, page_sel_ = 0, state_ = 0;
    bool player_ = false; // the game itself
    bool full_ = false, start_run_ = false, start_boot_ = false;
    std::string start_photo_, start_car_;
    std::unique_ptr<PhotoFaces> photos_; // a photo's preview (Photo Lab)
    std::string error_, start_project_, start_page_, card_;
    double step_time_ = 0; // display time not yet stepped by the menus
    bool timing_ = false;
    int timed_ = 0;
    double draw_ms_ = 0, frame_ms_ = 0, frame_max_ = 0;
    Uint64 last_frame_ = 0;
    long long credits_ = -1; // a scripted run's credits; -1: the game's own
    std::optional<menu::RaceRequest> race_; // what the running menus asked for at their end
    App* app_ = nullptr;                    // set when the menus start running
    gt3r::UiRect menu_button_{};            // the button beside the page as last drawn (UI units), empty when not shown (way())
};

} // namespace

std::unique_ptr<Screen> make_menu_viewer(App& app) { return std::make_unique<MenuViewer>(app); }
std::unique_ptr<Screen> make_game_screen(App& app, const std::string& new_card) { return std::make_unique<MenuViewer>(app, true, new_card); }
std::unique_ptr<Screen> make_online_game_screen(App& app) { return std::make_unique<MenuViewer>(app, true, std::string(), true); }

} // namespace gt4r

// ---- The running menus drawn over another screen (menu_painter.h).

#include "gt4r/app/menu_painter.h"

namespace gt4r {

namespace {

class RuntimePainter : public MenuPainter {
public:
    explicit RuntimePainter(App& app) : drawer_(app), cars_(app), models_(app), photos_(app) {
        drawer_.draw_live = [this](const LiveWidget& face, menu::Project* project, const std::string& page, const Rect& at, const SDL_FRect& dst, float opacity) {
            if (face.is("CarFace")) cars_.draw(face, project, page, at, dst, opacity);
            else if (face.is("ModelFace")) models_.draw(face, project, page, at, dst, opacity);
            else if (face.is("PhotoRenderFace")) photos_.draw(at, dst, opacity); // (the photo pause's photo_save page)
            else if (face.is("LoggerFace")) draw_logger_face(drawer_, face, at, opacity);
            else if (face.is("RaceCourseMapFace") && course_map) course_map(face, dst, at.w > 0 ? dst.w / at.w : drawer_.screen_scale(), opacity);
        };
        photos_.picture = [this]() { return rt_ ? rt_->photo_picture.get() : nullptr; };
        // The texts' sizes for the pages' packing (a dialog grows to its message) by this drawer's fonts when nothing else
        // measures them (a scripted race with menus of its own; under the menu viewer its drawer does).
        if (!menu::text_measure().measure) {
            menu::text_measure().measure = [this](const LiveWidget& face, float& w, float& h) { drawer_.measure(face, w, h); };
            measures_ = true;
        }
    }
    ~RuntimePainter() override {
        if (measures_) menu::text_measure().measure = nullptr;
    }
    void draw(menu::Runtime& rt, float x, float scale) override {
        rt_ = &rt;
        const menu::TransitionPicture tp = menu::transition_picture(rt.vm().attribute(rt.context(), "transition"));
        Affine grown;
        grown.a = grown.d = tp.page_scale, grown.e = 320 * (1 - tp.page_scale), grown.f = 240 * (1 - tp.page_scale);
        drawer_.menu_frame = rt.frame();
        drawer_.beside = scale > 0 ? x / scale : 0;
        cars_.begin_frame(rt.frame()), models_.begin_frame(rt.frame());
        boxes_.clear(), drawer_.boxes = &boxes_, x_ = x, scale_ = scale; // (what the pointer is over)
        for (const auto& page : rt.open_pages())
            if (menu::Project* project = rt.project_of(*page)) drawer_.draw(*project, *page, x, 0, scale, 0, grown, tp.page_opacity);
        drawer_.boxes = nullptr;
        cars_.end_frame(), models_.end_frame();
        // The pointer (menu_cursor.h), a step a picture of the menus.
        if (stepped_ > rt.frame()) stepped_ = 0;
        for (int n = 0; stepped_ < rt.frame() && n < 10; ++n, ++stepped_) cursor_.step(rt);
        stepped_ = rt.frame();
        cursor_.draw(rt, drawer_, x, scale);
    }
    void pointer(App& app, menu::Runtime& rt) override {
        if (rt_ == &rt) menu_pointer_frame(app, rt, boxes_, drag_, x_, scale_); // (nothing before the pages were first drawn)
    }

private:
    PageDrawer drawer_;
    std::vector<menu::DrawnBox> boxes_; // the pages' widgets as last drawn, and where
    menu::PointerDrag drag_;
    float x_ = 0, scale_ = 0;
    MenuCars cars_;     // the prize pages' models
    MenuModels models_;
    PhotoFaces photos_;
    menu::Runtime* rt_ = nullptr; // while drawn
    MenuCursor cursor_;
    u64 stepped_ = 0; // the menus' picture the pointer was last stepped to
    bool measures_ = false; // the texts are measured by drawer_
};

} // namespace

std::unique_ptr<MenuPainter> make_menu_painter(App& app) { return std::make_unique<RuntimePainter>(app); }

menu::Runtime::Pad menu_runtime_pad(const gt3r::PadState& p) {
    using Pad = menu::Runtime::Pad;
    static const std::pair<PadButton, u32> bits[] = {
        {PadButton::Up, Pad::kUp},         {PadButton::Down, Pad::kDown},         {PadButton::Left, Pad::kLeft},     {PadButton::Right, Pad::kRight},
        {PadButton::L1, Pad::kL1},         {PadButton::L2, Pad::kL2},             {PadButton::R1, Pad::kR1},         {PadButton::R2, Pad::kR2},
        {PadButton::Cross, Pad::kCross},   {PadButton::Circle, Pad::kCircle},     {PadButton::Triangle, Pad::kTriangle}, {PadButton::Square, Pad::kSquare},
        {PadButton::Start, Pad::kStart},
    };
    Pad out;
    for (const auto& [b, bit] : bits)
        if (p.down(b)) out.buttons |= bit;
    const gt3r::PadAxis axes[4] = {gt3r::PadAxis::RX, gt3r::PadAxis::RY, gt3r::PadAxis::LX, gt3r::PadAxis::LY};
    for (int i = 0; i < 4; ++i) out.sticks[i] = photo::stick(p.axis(axes[i]));
    return out;
}

void send_menu_directions(menu::Runtime& rt, const MenuInput& m, bool on) {
    static const std::pair<PadButton, menu::Key> dirs[] = {{PadButton::Up, menu::Key::Up}, {PadButton::Down, menu::Key::Down}, {PadButton::Left, menu::Key::Left}, {PadButton::Right, menu::Key::Right}};
    unsigned held = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (m.held(dirs[i].first)) held |= 1u << i;
        if (on && m.pressed(dirs[i].first) && !m.pressed(dirs[i ^ 1].first)) rt.key(dirs[i].second, (rt.directions_held >> i & 1) != 0);
    }
    rt.directions_held = held;
}

void send_menu_keys(menu::Runtime& rt, const MenuInput& m) {
    static const std::pair<PadButton, menu::Key> keys[] = {
        {PadButton::Cross, menu::Key::Accept}, {PadButton::Triangle, menu::Key::Cancel}, {PadButton::Start, menu::Key::Start},
        {PadButton::Select, menu::Key::Select}, {PadButton::L1, menu::Key::L1},         {PadButton::R1, menu::Key::R1},
        {PadButton::L2, menu::Key::L2},         {PadButton::R2, menu::Key::R2},         {PadButton::Square, menu::Key::Cancel},
    };
    send_menu_directions(rt, m, true);
    for (const auto& [button, key] : keys)
        if (m.pressed(button)) rt.key(key);
}

} // namespace gt4r
