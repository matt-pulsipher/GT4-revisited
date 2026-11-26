// The saved games: every game in the data folder (save/save_library.h) with its name and date; load one, start a new one, save the
// one being played, export one to a PlayStation 2 memory card image and import one, take one away. From the home screen, from the
// title's Save Game, and from Prologue's save and load items.

#include "gt4r/app/app.h"
#include "gt4r/app/page.h"
#include "gt4r/app/screen_saves.h"

#include "base/log.h"
#include "gt4r/save/card_image.h"
#include "gt4r/save/save_library.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <memory>
#include <utility>

namespace gt4r {

namespace {

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
        std::string dir = app.save_dir();
        while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
        library_ = std::make_unique<save::SaveLibrary>(dir, prologue_ ? "BISCPS-15055" : "BASCUS-97328");
        list(app);
    }

    void update(App& app, double) override {
        if (std::exchange(stale_, false)) list(app); // (back from the editor)
        if (window_.waiting()) {
            std::string path, error;
            if (!window_.take(path, error)) return;
            if (!path.empty()) take_file(app, path);
            else if (!error.empty()) say(error, true);
            return;
        }
        build(app);
        const int before = rows_.selected;
        rows_.update(app);
        if (rows_.selected != before) sure_ = false;
        if (go_) {
            const std::function<void()> go = std::exchange(go_, {});
            note_.clear();
            go();
            if (!closing_) list(app);
        }
        if (app.menu().pressed(PadButton::Triangle)) {
            sure_ = false;
            if (chosen_ >= 0) rows_.selected = first_game() + chosen_, chosen_ = -1;
            else app.pop();
        }
        if (closing_) app.pop();
    }

    void render(App& app, Ui& ui) override {
        // (Opened from the game's pages, the title's Save Game: Triangle on the list goes back to them.)
        page_header(app, ui, "SAVED GAMES", prologue_ ? "Gran Turismo 4 Prologue" : "Gran Turismo 4", chosen_ < 0 && hooks_.load ? "< Back to game" : "< Back");
        build(app);
        if (chosen_ >= 0 && chosen_ < int(games_.size())) {
            const save::SavedGame& g = games_[size_t(chosen_)];
            ui.text(24, 32, (g.name.empty() ? std::string("(no name)") : g.name) + "   " + date_text(g.when), colors::kAccent);
        }
        rows_.render(ui, {20, 46, ui.width() - 40, float(Ui::kHeight) - 46 - 66});
        if (window_.waiting()) ui.text(24, Ui::kHeight - 58, "Choose a file in the window that opened.", colors::kText);
        else if (!note_.empty()) ui.text(24, Ui::kHeight - 58, Ui::clip(note_, ui.width() - 48), bad_ ? colors::kBad : colors::kText);
        else if (games_.empty() && chosen_ < 0)
            ui.text(24, Ui::kHeight - 58, prologue_ ? "No game is saved yet: the first progress in the School makes one." : "No game is saved yet: Gran Turismo mode starts one.", colors::kDim);
        ui.footer(rows_.hint() + (chosen_ >= 0 ? "  {Triangle}: back to the list" : "  {Triangle}: back"));
    }

private:
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

    // The page's rows as they are now; what one does is done after the rows' own turn (go_).
    void build(App& app) {
        App* ap = &app;
        rows_.rows.clear();
        const auto row = [this](std::string label, std::string value, std::string hint, std::function<void()> what) {
            rows_.action(std::move(label), std::move(value), std::move(hint), [this, what = std::move(what)] { go_ = what; });
        };
        if (chosen_ >= 0 && chosen_ < int(games_.size())) {
            const save::SavedGame g = games_[size_t(chosen_)];
            if (g.readable)
                row(hooks_.load ? "Load" : "Play this game", g.current ? "the game being played" : "", "play", [this, g, ap] {
                    if (hooks_.load) hooks_.load(g.card), closing_ = true;
                    else if (library_->set_current(g.card)) chosen_ = -1, rows_.selected = 0, stale_ = true, ap->push(make_game_screen(*ap)); // (the game from power on, with this one)
                    else say("The choice could not be kept in the save folder.", true);
                });
            row("Export to PS2", "a memory card image", "choose the file", [this, g, ap] {
                from_ = g.card;
                ask(*ap, true, (g.name.empty() ? std::string("GT4") : g.name) + ".ps2");
            });
            // (Not while a game runs: it saves by itself.)
            if (g.readable && !hooks_.load) row("Edit", "the save editor", "open", [this, g, ap] { ap->push(make_save_editor(*ap, g.card)), stale_ = true; });
            row(sure_ ? "Delete: press again to delete this game" : "Delete", "", "delete", [this, g] {
                if (!sure_) return void(sure_ = true);
                sure_ = false;
                if (g.current && hooks_.load) say("The game being played cannot be deleted: load another first.", true);
                else if (library_->remove(g.card)) say("Deleted."), chosen_ = -1, rows_.selected = 0;
                else say("The game could not be deleted: " + g.card, true);
            });
            row("Back", "", "back to the list", [this] { rows_.selected = first_game() + chosen_, chosen_ = -1; });
            return;
        }
        if (hooks_.save_now)
            row("Save now", "", "save", [this] {
                const std::string why = hooks_.save_now();
                if (why.empty()) say("The game has been saved.");
                else say(why, true);
            });
        row("New game", "the games below stay", "start", [this, ap] {
            if (hooks_.new_game) hooks_.new_game(library_->new_card()), closing_ = true;
            else ap->push(make_game_screen(*ap, library_->new_card()));
        });
        row("Import from a PS2 memory card image", "", "choose the file", [this, ap] { ask(*ap, false, {}); });
        for (size_t i = 0; i < games_.size(); ++i) {
            const save::SavedGame& g = games_[i];
            std::string label = g.readable ? (g.name.empty() ? std::string("(no name)") : g.name) : std::string("(cannot be read)");
            if (g.readable && !prologue_) label += "   Cr. " + grouped(g.money) + "   day " + std::to_string(g.day) + "   " + std::to_string(g.cars) + (g.cars == 1 ? " car" : " cars");
            row(label, (g.current ? "being played   " : "") + date_text(g.when), "open", [this, i] { chosen_ = int(i), rows_.selected = 0; });
        }
    }

    // A file to write or read, chosen in the system's window.
    void ask(App& app, bool write, const std::string& name) {
        writing_ = write;
        if (write) window_.save_file(app, kCards, SDL_arraysize(kCards), name);
        else window_.open_file(app, kCards, SDL_arraysize(kCards));
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
    bool prologue_ = false, closing_ = false, sure_ = false, writing_ = false, stale_ = false, bad_ = false;
    std::unique_ptr<save::SaveLibrary> library_;
    std::vector<save::SavedGame> games_;
    FileWindow window_;
    std::string note_, from_;
    PageRows rows_;
    std::function<void()> go_;
    int chosen_ = -1;
};

} // namespace

std::unique_ptr<Screen> make_saves_screen(App& app, SavesHooks hooks) { return std::make_unique<SavesScreen>(app, std::move(hooks)); }

} // namespace gt4r
