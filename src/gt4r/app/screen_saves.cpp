// The saved games: every game in the data folder (save/save_library.h) with its name and date; load one, start a new one, save the
// one being played, export one to a PlayStation 2 memory card image and import one, take one away. From the home screen, from the
// title's Save Game, and from Prologue's save and load items.

#include "gt4r/app/app.h"
#include "gt4r/app/screen_saves.h"

#include "base/log.h"
#include "gt4r/save/card_image.h"
#include "gt4r/save/save_library.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>

namespace gt4r {

namespace {

// A file dialog's answer, kept until the screen takes it (the dialog may outlive the screen).
struct Chosen {
    std::mutex lock;
    std::string path;
    bool done = false;
};
void SDLCALL on_chosen(void* user, const char* const* files, int) {
    std::unique_ptr<std::shared_ptr<Chosen>> held(static_cast<std::shared_ptr<Chosen>*>(user));
    const std::lock_guard<std::mutex> lock((*held)->lock);
    (*held)->path = files && files[0] ? files[0] : "";
    (*held)->done = true;
}
constexpr SDL_DialogFileFilter kCards[] = {{"PlayStation 2 memory card", "ps2;mcd;bin"}, {"All files", "*"}};

std::string date_text(std::time_t t) {
    char out[32] = "";
    if (const std::tm* tm = t ? std::localtime(&t) : nullptr) std::strftime(out, sizeof out, "%Y-%m-%d %H:%M", tm);
    return out;
}
std::string grouped(s64 v) {
    std::string s = std::to_string(v);
    for (int at = int(s.size()) - 3; at > 0; at -= 3) s.insert(size_t(at), ",");
    return s;
}

class SavesScreen : public Screen {
public:
    SavesScreen(App& app, SavesHooks hooks) : hooks_(std::move(hooks)) {
        prologue_ = app.disc() && app.disc()->info().game == Game::Prologue;
        std::string dir = app.data_dir();
        while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
        library_ = std::make_unique<save::SaveLibrary>(dir, prologue_ ? "BISCPS-15055" : "BASCUS-97328");
        list(app);
    }

    void update(App& app, double) override {
        const MenuInput& m = app.menu();
        if (waiting_) {
            std::string path;
            {
                const std::lock_guard<std::mutex> lock(waiting_->lock);
                if (!waiting_->done) return;
                path = waiting_->path;
            }
            waiting_.reset();
            if (!path.empty()) take_file(app, path);
            return;
        }
        std::vector<Row> rows = now();
        sel_ = std::clamp(sel_ + m.vertical(), 0, std::max(0, int(rows.size()) - 1));
        if (m.vertical()) sure_ = false;
        if (m.pressed(PadButton::Cross) && sel_ < int(rows.size())) {
            note_.clear();
            rows[size_t(sel_)].act(app);
            if (!closing_) list(app);
        }
        if (m.pressed(PadButton::Triangle)) {
            sure_ = false;
            if (chosen_ >= 0) sel_ = first_game() + chosen_, chosen_ = -1;
            else app.pop();
        }
        if (closing_) app.pop();
    }

    void render(App&, Ui& ui) override {
        ui.header("SAVED GAMES", prologue_ ? "Gran Turismo 4 Prologue" : "Gran Turismo 4");
        const std::vector<Row> rows = now();
        constexpr int kShown = 14;
        const int count = int(rows.size()), top = std::clamp(sel_ - kShown / 2, 0, std::max(0, count - kShown));
        if (chosen_ >= 0 && chosen_ < int(games_.size())) {
            const save::SavedGame& g = games_[size_t(chosen_)];
            ui.text(24, 32, (g.name.empty() ? std::string("(no name)") : g.name) + "   " + date_text(g.when), colors::kAccent);
        }
        for (int i = top; i < std::min(count, top + kShown); ++i) {
            const float y = 52 + float(i - top) * 19;
            const bool on = i == sel_;
            ui.fill_round(20, y - 4, Ui::kWidth - 40, 17, 3, on ? colors::kHighlight : colors::kPanel);
            ui.text(30, y, rows[size_t(i)].label, on ? colors::kText : colors::kDim);
            ui.text_right(Ui::kWidth - 30, y, rows[size_t(i)].value, on ? colors::kText : colors::kDim);
        }
        if (games_.empty() && chosen_ < 0)
            ui.text(24, 52 + float(count) * 19 + 8, prologue_ ? "No game is saved yet: the first progress in the School makes one." : "No game is saved yet: Gran Turismo mode starts one.", colors::kDim);
        if (waiting_) ui.text(24, Ui::kHeight - 60, "Choose a file in the window that opened.", colors::kText);
        else if (!note_.empty()) ui.text(24, Ui::kHeight - 60, Ui::clip(note_, Ui::kWidth - 48), bad_ ? colors::kBad : colors::kText);
        ui.footer(chosen_ >= 0 ? "{Cross}: choose  {Triangle}: back to the list" : "{Up/Down}: select  {Cross}: open  {Triangle}: back");
    }

private:
    struct Row {
        std::string label, value;
        std::function<void(App&)> act;
    };

    void list(App& app) {
        games_.clear();
        try {
            if (db::SpecDb* db = app.db()) games_ = library_->games(save::database_codes(*db));
        } catch (const std::exception& e) {
            say(e.what(), true);
        }
        if (chosen_ >= int(games_.size())) chosen_ = -1;
    }
    void say(std::string text, bool bad = false) { note_ = std::move(text), bad_ = bad; }
    int first_game() const { return (hooks_.save_now ? 1 : 0) + 2; }

    std::vector<Row> now() {
        std::vector<Row> rows;
        if (chosen_ >= 0 && chosen_ < int(games_.size())) {
            const save::SavedGame g = games_[size_t(chosen_)];
            if (g.readable)
                rows.push_back({hooks_.load ? "Load" : "Play this game", g.current ? "the game being played" : "", [this, g](App&) {
                                    if (hooks_.load) hooks_.load(g.card), closing_ = true;
                                    else if (library_->set_current(g.card)) chosen_ = -1, say("Play goes on with this game.");
                                    else say("The choice could not be kept in the data folder.", true);
                                }});
            rows.push_back({"Export to PS2", "a memory card image", [this, g](App& app) {
                                from_ = g.card;
                                ask(app, true, (g.name.empty() ? std::string("GT4") : g.name) + ".ps2");
                            }});
            rows.push_back({sure_ ? "Delete: press again to take this game away" : "Delete", "", [this, g](App&) {
                                if (!sure_) return void(sure_ = true);
                                sure_ = false;
                                const bool gone = !g.current || !hooks_.load ? library_->remove(g.card) : false;
                                if (g.current && hooks_.load) say("The game being played cannot be deleted: load another first.", true);
                                else if (gone) say("Deleted."), chosen_ = -1, sel_ = 0;
                                else say("The game could not be deleted: " + g.card, true);
                            }});
            rows.push_back({"Back", "", [this](App&) { sel_ = first_game() + chosen_, chosen_ = -1; }});
            return rows;
        }
        if (hooks_.save_now)
            rows.push_back({"Save now", "", [this](App&) {
                                const std::string why = hooks_.save_now();
                                if (why.empty()) say("The game has been saved.");
                                else say(why, true);
                            }});
        rows.push_back({"New game", "the games below stay", [this](App& app) {
                            if (hooks_.new_game) hooks_.new_game(library_->new_card()), closing_ = true;
                            else app.push(make_game_screen(app, library_->new_card()));
                        }});
        rows.push_back({"Import from a PS2 memory card image", "", [this](App& app) { ask(app, false, {}); }});
        for (size_t i = 0; i < games_.size(); ++i) {
            const save::SavedGame& g = games_[i];
            std::string label = g.readable ? (g.name.empty() ? std::string("(no name)") : g.name) : std::string("(cannot be read)");
            if (g.readable && !prologue_) label += "   Cr. " + grouped(g.money) + "   day " + std::to_string(g.day) + "   " + std::to_string(g.cars) + (g.cars == 1 ? " car" : " cars");
            rows.push_back({label, (g.current ? "being played   " : "") + date_text(g.when), [this, i](App&) { chosen_ = int(i), sel_ = 0; }});
        }
        return rows;
    }

    // A file to write or read, chosen in the system's dialog (a scripted run names it: GT4R_SAVES_FILE).
    void ask(App& app, bool write, const std::string& name) {
        writing_ = write;
        if (const char* path = std::getenv("GT4R_SAVES_FILE")) return take_file(app, path);
        waiting_ = std::make_shared<Chosen>();
        auto* held = new std::shared_ptr<Chosen>(waiting_);
        SDL_Window* window = SDL_GetRenderWindow(app.renderer());
        if (write) SDL_ShowSaveFileDialog(on_chosen, held, window, kCards, SDL_arraysize(kCards), name.c_str());
        else SDL_ShowOpenFileDialog(on_chosen, held, window, kCards, SDL_arraysize(kCards), nullptr, false);
    }

    void take_file(App& app, const std::string& path) {
        if (writing_) {
            std::string why;
            const Bytes image = save::export_card(from_, std::nullopt, &why, library_->product());
            if (image.empty()) return say("Not exported: " + why, true);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(image.data()), std::streamsize(image.size()));
            if (out) say("Exported as " + path);
            else say("The file could not be written: " + path, true);
            return;
        }
        std::ifstream in(path, std::ios::binary);
        const Bytes image((std::istreambuf_iterator<char>(in)), {});
        const std::string card = library_->new_card();
        const save::CardImageResult r = save::import_card(image, card, library_->product());
        if (r.ok) say("Imported: it is in the list.");
        else say("Not imported: " + (r.message.empty() ? std::string("the file is no memory card image.") : r.message), true);
        list(app);
    }

    SavesHooks hooks_;
    bool prologue_ = false, closing_ = false, sure_ = false, writing_ = false, bad_ = false;
    std::unique_ptr<save::SaveLibrary> library_;
    std::vector<save::SavedGame> games_;
    std::shared_ptr<Chosen> waiting_;
    std::string note_, from_;
    int sel_ = 0, chosen_ = -1;
};

} // namespace

std::unique_ptr<Screen> make_saves_screen(App& app, SavesHooks hooks) { return std::make_unique<SavesScreen>(app, std::move(hooks)); }

} // namespace gt4r
