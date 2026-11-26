// The Status page's transfer from an earlier game (menu/earlier_saves.h): which Gran Turismo 3 game (or Prologue game) Gran
// Turismo 4 takes the licenses and credits from, instead of a memory card's slot. GT3 Revisited's saves where it keeps them on
// this computer, a card's save folder or card image put in the data folder's memorycard/, or a file chosen: a GT3 Revisited
// save, a PS2 memory card image, a .psu or the game's data file. Nothing chosen: the game goes on as with no save.

#include "gt4r/app/app.h"
#include "gt4r/app/page.h"
#include "gt4r/app/screen_saves.h"

#include "base/file.h"

#include <SDL3/SDL.h>

#include <ctime>
#include <cstdlib>
#include <memory>
#include <utility>

namespace gt4r {

namespace {

constexpr SDL_DialogFileFilter kFiles[] = {{"GT3 Revisited save, PS2 memory card or save", "sav;ps2;mcd;bin;psu;raw"}, {"All files", "*"}};

std::string grouped(s64 v) {
    std::string s = std::to_string(v);
    for (int at = int(s.size()) - 3; at > (s[0] == '-' ? 1 : 0); at -= 3) s.insert(size_t(at), ",");
    return s;
}
std::string date_text(std::time_t t) {
    char out[32] = "";
    if (const std::tm* tm = t ? std::localtime(&t) : nullptr) std::strftime(out, sizeof out, "%Y-%m-%d", tm);
    return out;
}

// What a save holds that the transfer looks at, in the player's words.
std::string holds(const save::EarlierGame& g, const std::string& region) {
    if (g.kind == save::EarlierGame::Kind::Prologue) {
        const int l = save::transferred_licenses(g, region);
        return std::string("GT4 Prologue   ") + (l == 2 ? "B and A licenses" : l == 1 ? "B license" : "no B or A license");
    }
    bool b = g.medals.size() >= 16, a = b;
    for (size_t k = 0; k < g.medals.size() && k < 16; ++k)
        if (g.medals[k] >= 0x80) (k < 8 ? b : a) = false;
    return "Cr. " + grouped(g.credits) + "   " + (b && a ? "B and A licenses" : b ? "B license" : a ? "A license" : "no B or A license");
}

class EarlierSavesScreen : public Screen {
public:
    EarlierSavesScreen(menu::TransferChoice choice, std::function<void(std::optional<save::EarlierGame>)> answer)
        : choice_(std::move(choice)), answer_(std::move(answer)) {}
    ~EarlierSavesScreen() override { give({}); } // (gone without a choice: none)

    void update(App& app, double) override {
        if (window_.waiting()) {
            std::string path, error;
            if (!window_.take(path, error)) return;
            if (!path.empty()) take_file(app, path);
            else if (!error.empty()) say(error, true);
            return;
        }
        build(app);
        rows_.update(app);
        if (go_) std::exchange(go_, {})();
        if (app.menu().pressed(PadButton::Triangle)) give({});
        if (given_) app.pop();
    }

    void render(App& app, Ui& ui) override {
        const bool prologue = choice_.prologue, gt3 = choice_.gt3;
        page_header(app, ui, gt3 && prologue ? "EARLIER GAMES" : prologue ? "GT4 PROLOGUE GAMES" : "GRAN TURISMO 3 GAMES", "Transfer to Gran Turismo 4",
                    "< Back to game");
        build(app);
        ui.text(24, 32,
                gt3 && prologue ? "Choose the Gran Turismo 3 or GT4 Prologue game to transfer licenses from."
                : prologue      ? "Choose the GT4 Prologue game to transfer licenses from."
                : choice_.licenses ? "Choose the Gran Turismo 3 game to transfer from."
                                   : "Choose the Gran Turismo 3 game to transfer credits from.",
                colors::kAccent);
        rows_.render(ui, {20, 46, ui.width() - 40, float(Ui::kHeight) - 46 - 66});
        std::string note = note_;
        if (window_.waiting()) note = "Choose a file in the window that opened.";
        else if (note.empty() && gt3) {
            if (choice_.looked.empty()) note = "GT3 Revisited's games: Choose a file, then GT3 Revisited's saves folder.";
            else if (choice_.found.empty()) note = "No GT3 Revisited game in " + choice_.looked;
            else note = "GT3 Revisited's games: " + choice_.looked;
        }
        ui.text(24, Ui::kHeight - 58, Ui::clip(note, ui.width() - 48), bad_ ? colors::kBad : colors::kDim);
        ui.footer(rows_.hint() + "  {Triangle}: back to the game, nothing transferred");
    }

private:
    void say(std::string text, bool bad = false) { note_ = std::move(text), bad_ = bad; }
    void give(std::optional<save::EarlierGame> g) {
        if (given_) return;
        given_ = true;
        if (answer_) answer_(std::move(g));
    }

    void build(App& app) {
        App* ap = &app;
        rows_.rows.clear();
        for (const save::EarlierGame& g : choice_.found) {
            std::string where = g.from + (g.current ? ", being played" : "");
            if (const std::string d = date_text(g.when); !d.empty()) where += "   " + d;
            rows_.action(g.name + "   " + holds(g, choice_.region), where, "transfer from this game", [this, g] { go_ = [this, g] { give(g); }; });
        }
        rows_.action("Choose a file", choice_.prologue && choice_.gt3 ? "a GT3 Revisited save, a GT3 or GT4 Prologue PS2 card image or save"
                                      : choice_.prologue             ? "a GT4 Prologue PS2 card image or save"
                                                                     : "a GT3 Revisited save, a PS2 memory card image or save",
                     "choose the file",
                         [this, ap] { go_ = [this, ap] { window_.open_file(*ap, kFiles, SDL_arraysize(kFiles)); }; });
        rows_.action("Back to the game", "nothing transferred", "back", [this] { go_ = [this] { give({}); }; });
        if (rows_.selected < 0) rows_.selected = 0;
    }

    void take_file(App&, const std::string& path) {
        const std::optional<Bytes> b = gt3r::read_host_file(path);
        if (!b) return say("The file could not be read: " + path, true);
        std::string why;
        std::optional<save::EarlierGame> g = save::read_earlier_file(*b, path, choice_.region, &why, choice_.prologue);
        if (g && !choice_.gt3 && g->kind == save::EarlierGame::Kind::Gt3) g.reset(), why = "a Gran Turismo 3 game gives no licenses on this disc";
        if (!g) return say(std::string(choice_.prologue ? "Not a game to transfer from: " : "Not a Gran Turismo 3 game for this disc: ") + why + ".", true);
        give(std::move(g));
    }

    menu::TransferChoice choice_;
    std::function<void(std::optional<save::EarlierGame>)> answer_;
    PageRows rows_;
    FileWindow window_;
    std::function<void()> go_;
    std::string note_;
    bool bad_ = false, given_ = false;
};

} // namespace

std::unique_ptr<Screen> make_earlier_saves_screen(menu::TransferChoice choice, std::function<void(std::optional<save::EarlierGame>)> answer) {
    return std::make_unique<EarlierSavesScreen>(std::move(choice), std::move(answer));
}

std::string gt3r_data_dir() {
    std::string dir;
    if (const char* d = std::getenv("GT4R_GT3R_DIR")) {
        dir = d;
    } else {
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_EMSCRIPTEN)
        return {}; // (another app's own folder: its saves are chosen as files)
#elif defined(SDL_PLATFORM_WINDOWS)
        if (const char* appdata = std::getenv("APPDATA")) dir = std::string(appdata) + "\\GT3Revisited\\GT3Revisited\\";
#elif defined(SDL_PLATFORM_MACOS)
        if (const char* home = std::getenv("HOME")) dir = std::string(home) + "/Library/Application Support/GT3Revisited/GT3Revisited/";
#else
        if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) dir = std::string(xdg) + "/GT3Revisited/GT3Revisited/";
        else if (const char* home = std::getenv("HOME")) dir = std::string(home) + "/.local/share/GT3Revisited/GT3Revisited/";
#endif
    }
    return !dir.empty() && gt3r::host_is_directory(dir) ? dir : std::string();
}

} // namespace gt4r
