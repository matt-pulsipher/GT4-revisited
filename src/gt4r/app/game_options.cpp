#include "gt4r/app/game_options.h"

#include "gt4r/app/app.h"
#include "gt4r/save/card_image.h"
#include "gt4r/save/game_data.h"
#include "gt4r/save/save_edit.h"
#include "gt4r/save/save_library.h"

#include "base/log.h"

#include <filesystem>

namespace gt4r {

namespace {

std::string suffix(int player) { return player == 1 ? "_2p" : "_1p"; }

class RunningOptions : public GameOptions {
public:
    explicit RunningOptions(menu::GameState& g) : game_(g) {}
    menu::GameState* state() override { return &game_; }
    void changed() override { game_.need_save = true; } // (as the Options page's OK: setNeedSave)

private:
    menu::GameState& game_;
};

class SavedOptions : public GameOptions {
public:
    SavedOptions(std::optional<save::SaveDocument> doc, std::string why, std::string backups)
        : doc_(std::move(doc)), why_(std::move(why)), backups_(std::move(backups)) {}
    menu::GameState* state() override { return doc_ ? &doc_->state : nullptr; }
    std::string why() const override { return why_; }
    void changed() override { changed_ = true; }
    std::string close() override {
        if (!doc_ || !changed_ || !doc_->changed()) return {};
        changed_ = false;
        const save::SaveDocument::Written w = doc_->write(backups_);
        if (!w.ok) LOG_WARN("Controls: the saved game was not written: %s", w.message.c_str());
        return w.ok ? std::string() : w.message;
    }

private:
    std::optional<save::SaveDocument> doc_;
    std::string why_, backups_;
    bool changed_ = false;
};

} // namespace

input::PortConfig GameOptions::port(int player) {
    menu::GameState* g = state();
    return input::port_from_text(g ? g->option_text("controllers" + suffix(player)) : std::string());
}
void GameOptions::set_port(int player, const input::PortConfig& c) {
    if (menu::GameState* g = state()) g->options["controllers" + suffix(player)] = input::port_text(c), changed();
}
void GameOptions::set_vibration(int player, bool on) {
    input::PortConfig c = port(player);
    input::set_option(c, "VIBRATION", on);
    set_port(player, c);
}
void GameOptions::set_wheel_feedback(int player, bool on) {
    input::PortConfig c = port(player);
    input::set_option(c, "WHEEL FEEDBACK", on);
    set_port(player, c);
}
bool GameOptions::active_steering(int player) {
    menu::GameState* g = state();
    return g && g->option_int("active_steering" + suffix(player), 0) != 0;
}
void GameOptions::set_active_steering(int player, bool on) {
    if (menu::GameState* g = state()) g->options["active_steering" + suffix(player)] = s32(on ? 1 : 0), changed();
}
std::string GameOptions::steering_type(int player) {
    menu::GameState* g = state();
    const std::string t = g ? g->option_text("steering_assist" + suffix(player)) : std::string();
    return t == "PROFESSIONAL" || t == "SIMULATION" ? t : "AMATEUR";
}
void GameOptions::set_steering_type(int player, const std::string& type) {
    if (menu::GameState* g = state()) g->options["steering_assist" + suffix(player)] = type, changed();
}
bool GameOptions::power_assist(int player) { return input::get_feedback(port(player).of(input::Device::Cheetah), input::Device::Cheetah, "POWER_ASSIST") != 0; }
void GameOptions::set_power_assist(int player, bool on) {
    input::PortConfig c = port(player);
    input::set_feedback(c.of(input::Device::Cheetah), input::Device::Cheetah, "POWER_ASSIST", on ? 1 : 0);
    set_port(player, c);
}
int GameOptions::force_strength(int player) { return input::get_feedback(port(player).of(input::Device::Cheetah), input::Device::Cheetah, "POWER"); }
void GameOptions::set_force_strength(int player, int v) {
    input::PortConfig c = port(player);
    input::set_feedback(c.of(input::Device::Cheetah), input::Device::Cheetah, "POWER", v);
    set_port(player, c);
}
void GameOptions::restore_keys(int player) {
    input::PortConfig c = port(player);
    for (size_t d = 0; d < input::kDeviceCount; ++d) {
        const std::vector<gt3r::s8> feedback = c.device[d].feedback;
        c.device[d] = input::default_config(input::Device(d));
        c.device[d].feedback = feedback;
    }
    set_port(player, c);
}
bool GameOptions::keys_changed(int player) {
    const input::PortConfig c = port(player);
    for (size_t d = 0; d < input::kDeviceCount; ++d)
        if (input::keys_changed(c.device[d], input::Device(d))) return true;
    return false;
}

std::unique_ptr<GameOptions> running_game_options(menu::GameState& game) { return std::make_unique<RunningOptions>(game); }

std::unique_ptr<GameOptions> saved_game_options(App& app) {
    const bool prologue = app.disc() && app.disc()->info().game == Game::Prologue;
    if (prologue) return std::make_unique<SavedOptions>(std::nullopt, "Gran Turismo 4 Prologue keeps these in its own Options.", "");
    db::SpecDb* db = app.db();
    if (!db) return std::make_unique<SavedOptions>(std::nullopt, "This disc has no game to hold them.", "");
    std::string dir = app.save_dir();
    while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    const std::string product = app.disc() ? app.disc()->info().product : std::string("BASCUS-97328");
    const save::SaveLibrary library(dir, product);
    const std::string card = library.current();
    if (card.empty() || !library.holds_game(card))
        return std::make_unique<SavedOptions>(std::nullopt, "No game is saved yet: these are the game's own and come with its first save.", "");
    std::string why;
    const std::filesystem::path folder = std::filesystem::path(card) / save::game_folder_name(product);
    std::optional<save::SaveDocument> doc =
        save::SaveDocument::open({save::EditPlace::Kind::Folder, folder.string()}, save::database_codes(*db), product, &why);
    if (!doc) return std::make_unique<SavedOptions>(std::nullopt, "The saved game could not be read: " + why, "");
    return std::make_unique<SavedOptions>(std::move(doc), "", (std::filesystem::path(dir) / "backups").string());
}

} // namespace gt4r
