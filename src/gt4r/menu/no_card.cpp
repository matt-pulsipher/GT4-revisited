#include "gt4r/menu/no_card.h"

#include "gt4r/menu/game_internal.h"
#include "gt4r/save/codec.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace gt4r::menu {

using script::Value;

namespace {

enum class Do { Answer, NoGame, NoOtherGame, SaveFailed, LoadFailed };
struct Rule {
    const char* page;
    const char* key;
    int answer;
    Do what = Do::Answer;
    const char* project = nullptr; // only there
};

// The texts by the disc's pages and keys. The answers are the ones that lead on: at the first boot (no game yet) "yes" to making
// the game's data, so the game's own name entry comes before the title as with a blank card (Arcade's options, favorites and
// records then go into a game from the start), elsewhere "yes" to what only a card would ask.
const Rule kRules[] = {
    {"BootProject::CheckRoot", "no_gt4_data_create_data_now?", 1},
    {"BootProject::CheckRoot", "no_card_need_n_byte_continue?", 1},
    {"BootProject::CheckRoot", "no_free_space_need_n_byte_continue?", 1},
    {"BootProject::CheckRoot", "please_insert_n_byte_free_card", 1},
    {"BootProject::CheckRoot", "cant_play_gtmode_ok?", 1},
    {"BootProject::CheckRoot", "invalid_file_create?", 0},
    {"BootProject::EntryRoot", "then create save data", 1},
    {"BootProject::ExplainRoot", "gt4 use autosave", 1},
    {"BootProject::ExplainRoot", "kounaru", 1},
    {"BootProject::ExplainRoot", "dont unplug", 1},
    {"BootProject::ExplainRoot", "GT savedate is available", 0}, // (Gran Turismo 3's and Prologue's data on a card)
    // The Status page's transfer (earlier_saves.h): the saves are all offered at once, the player chose none (or there was none).
    {"GTmodeProject::StatusRoot", "gt3 data not found in slot n", 0},
    {"GTmodeProject::StatusRoot", "gt4p data not found in slot n", 0},
    {"ArcadeProject::TopRoot", "load ok?", 1},
    {"ArcadeProject::TopRoot", "broken create new?", 1},
    {"ArcadeProject::TopRoot", "create new?", 0},
    {"ArcadeProject::TopRoot", "create new?2", 1},
    {"ArcadeProject::TopRoot", "no card", 1},
    {"ArcadeProject::TopRoot", "no gt4 game data create?", 1},
    {"MemoryCard", "create_game_data_need_n_byte_confirm", 1},
    {"MemoryCard", "create_best_replay_need_n_byte_confirm", 1},
    {"MemoryCard", "create_film_need_n_byte_confirm", 1},
    {"MemoryCard", "create_full_replay_need_n_byte_confirm", 1},
    {"MemoryCard", "create_photo_need_n_byte_confirm", 1},
    {"MemoryCard", "create_replaylist_need_n_byte_confirm", 1},
    {"MemoryCard", "create_slideshow_need_n_byte_confirm", 1},
    {"MemoryCard", "format_slot_n_cofirm", 1},
    {"MemoryCard", "slot1 card changed try save?", 1},
    {"MemoryCard", "no_gt4_data_retry?", 0, Do::NoGame},
    {"MemoryCard", "create? delete garage", 0},
    {"MemoryCard", "akiramete create? delete garage", 0},
    {"MemoryCard", "hontoni? delete garage dayo?", 0},
    {"MemoryCard", "Do you load trade data?", 1},
    {"MemoryCard", "save_done", 1, Do::Answer, "boot"}, // (a new game's)
    {"MemoryCard", "no_memory_card_on_slot_2", 0, Do::NoOtherGame},
    {"MemoryCard", "no_memorycard_slot_n", 0, Do::NoOtherGame},
    {"MemoryCard", "save_error", 0, Do::SaveFailed},
    {"MemoryCard", "save_error_retry?", 0, Do::SaveFailed},
    {"MemoryCard", "save_error_retry_confirm", 0, Do::SaveFailed},
    {"MemoryCard", "different_card", 0, Do::SaveFailed},
    {"MemoryCard", "different_card_retry?", 0, Do::SaveFailed},
    {"MemoryCard", "different_card_retry?_2", 0, Do::SaveFailed},
    {"MemoryCard", "different_card_retry_confirm", 0, Do::SaveFailed},
    {"MemoryCard", "no_gt4_data", 0, Do::NoGame},
    {"MemoryCard", "no_gt4_data_retry_confirm", 0, Do::NoGame},
    {"MemoryCard", "no_memory_card", 0, Do::NoGame},
    {"MemoryCard", "no_memory_card_retry?", 0, Do::NoGame},
    {"MemoryCard", "no_memory_card_retry_confirm", 0, Do::NoGame},
    {"MemoryCard", "access_error", 0, Do::LoadFailed},
    {"MemoryCard", "load_error", 0, Do::LoadFailed},
    {"MemoryCard", "load_error_retry?", 0, Do::LoadFailed},
    {"MemoryCard", "load_error_retry_confirm", 0, Do::LoadFailed},
    {"MemoryCard", "broken_file", 0, Do::LoadFailed},
    {"MemoryCard", "broken_file_retry?", 0, Do::LoadFailed},
    {"MemoryCard", "broken_file_retry_confirm", 0, Do::LoadFailed},
};

// A folder's path in lines the dialog holds.
std::string folded(const std::string& path) {
    constexpr size_t kLine = 44;
    std::string out;
    std::string line;
    for (size_t at = 0; at < path.size();) {
        // A folder's name at a time.
        const size_t slash = path.find_first_of("/\\", at + 1);
        std::string part = path.substr(at, slash == std::string::npos ? slash : slash - at);
        at = slash == std::string::npos ? path.size() : slash;
        if (!line.empty() && line.size() + part.size() > kLine) out += line + "\n", line.clear();
        line += part;
    }
    return out + line;
}

// The state's bytes that a save would write: what tells whether the game has changed.
u32 print_of(GameNatives& n) {
    GameState& s = n.state();
    n.collect();
    // (Where the menus stand is no change of the game.)
    const std::string project = std::exchange(s.major_menu_project, {}), page = std::exchange(s.major_menu_page, {}), widget = std::exchange(s.major_menu_widget, {});
    const int depth = std::exchange(s.saved_menu_depth, -1);
    const Bytes stack = std::exchange(s.saved_menu_stack, {});
    Bytes all = n.card().payload(s);
    s.major_menu_project = project, s.major_menu_page = page, s.major_menu_widget = widget, s.saved_menu_depth = depth, s.saved_menu_stack = stack;
    for (const GarageEntry& e : s.garage)
        if (e.record) all.insert(all.end(), e.record->bytes.begin(), e.record->bytes.end());
    const s64 money = s.money;
    const u8* m = reinterpret_cast<const u8*>(&money);
    all.insert(all.end(), m, m + sizeof money);
    return save::crc32(all) | 1u;
}

// A notice in the game's own dialog, once the scripts are idle.
void tell(Runtime& rt, const std::string& text) {
    const Value open = rt.vm().attribute(rt.project_value(), "openConfirmDialog");
    if (open.kind != script::Kind::Function) return;
    rt.queue_function(open, {rt.context(), Value::integer(2), Value::string(text)}, "a save that failed");
}

} // namespace

bool matches_template(const std::string& text, const std::string& pattern) {
    size_t at = 0, from = 0;
    for (;;) {
        const size_t mark = std::min(pattern.find("%d", from), pattern.find("%s", from));
        const std::string piece = pattern.substr(from, mark == std::string::npos ? mark : mark - from);
        if (from == 0) {
            if (text.compare(0, piece.size(), piece) != 0) return false;
            at = piece.size();
        } else if (mark == std::string::npos) {
            return text.size() >= at + piece.size() && text.compare(text.size() - piece.size(), piece.size(), piece) == 0;
        } else {
            const size_t found = text.find(piece, at);
            if (found == std::string::npos) return false;
            at = found + piece.size();
        }
        if (mark == std::string::npos) return at == text.size();
        from = mark + 2;
    }
}

std::optional<CardAnswer> card_answer(Runtime& rt, const std::string& message) {
    if (rt.config().console_card) return std::nullopt;
    const std::shared_ptr<GameNatives> n = natives_of(rt);
    for (const Rule& r : kRules) {
        if (r.project && (!rt.project() || rt.project()->name() != r.project)) continue;
        const std::string* text = rt.project() ? rt.project()->text().find(r.page, r.key) : nullptr;
        if (!text || text->empty() || !matches_template(message, *text)) continue;
        CardAnswer a{r.answer, {}};
        const std::string folder = n ? folded(n->card().folder()) : std::string();
        switch (r.what) {
        case Do::Answer: break;
        case Do::NoGame: a.shown = "There is no saved game yet."; break;
        case Do::NoOtherGame: a.shown = "There is no other saved game."; break;
        case Do::SaveFailed:
            if (n && !n->save_failure_told) n->save_failure_told = true, a.shown = "The game could not be saved in\n" + folder + "\nCheck that the folder can be written in.";
            break;
        case Do::LoadFailed:
            if (n && !n->load_failure_told) n->load_failure_told = true, a.shown = "The saved game could not be read:\n" + folder;
            break;
        }
        rt.note("answered: " + std::string(r.key));
        return a;
    }
    return std::nullopt;
}

void reword_card_texts(Project& project, const std::string& language) {
    if (language != "US" && language != "GB" && language != "UK") return;
    static const char* const kTexts[][3] = {
        {"MemoryCard", "now_saving", "Saving data..."},
        {"MemoryCard", "You can turn off the power", "The game has been saved."},
        {"GTmodeProject::HomeRoot", "You can turn off the power", "The game has been saved."},
        {"GTmodeProject::TopRoot", "now saving", "Saving data..."},
        {"ArcadeProject::MakerRoot", "TOOLTIP|Slot1", "Load the garage of the game being played."},
        {"ArcadeProject::MakerRoot", "TOOLTIP|Slot2", "Load the garage of another saved game."},
        {"OptionProject::ReplayRoot", "LabelMcSlot", "Save slot"},
        {"OptionProject::ReplayRoot", "TOOLTIP|McSlot", "Select the slot for saving replay data"},
        {"OptionProject::AlbumRoot", "LabelMcSlot", "Save slot"},
        {"OptionProject::AlbumRoot", "LabelQuality", "Saved images"},
        {"OptionProject::AlbumRoot", "TOOLTIP|McSlot", "Set the slot from which you would like to load Film data, Photo data and Slideshow data"},
        {"OptionProject::AlbumRoot", "TOOLTIP|Quality", "Set image quality when saving files. Higher quality means larger file size."},
        {"LaboModeProject::OptionAlbumDialog", "LabelMcSlot", "Save slot"},
        {"LaboModeProject::OptionAlbumDialog", "LabelQuality_image", "Image Quality: saved files"},
        {"LaboModeProject::OptionAlbumDialog", "TOOLTIP|Quality_image", "Set image quality when saving. Higher quality means larger file size."},
        {"LaboModeProject::OptionReplayDialog", "LabelMcSlot", "Save slot"},
        {"LaboModeProject::OptionReplayDialog", "TOOLTIP|McSlot", "Select the slot for saving replay data"},
        {"LaboModeProject::ReplaySlotDialog", "LabelMcSlot", "Save slot"},
        {"LaboModeProject::ReplaySlotDialog", "TOOLTIP|McSlot", "Select the slot for saving replay data"},
        {"LaboModeProject::Template", "n_kbfree_slot_n", " "},
        {"LaboModeProject::Template", "unformat_slot_n", " "},
        {"LaboModeProject::Template", "no_card", " "},
        {"LaboModeProject::Template", "no_card_slot_n", " "},
        {"PhotoSaveProject::DestinationDialog", "Slot1", "Slot 1"},
        {"PhotoSaveProject::DestinationDialog", "Slot2", "Slot 2"},
        {"PhotoSaveProject::SaveDialog", "Slot1", "Slot 1"},
        {"PhotoSaveProject::SaveDialog", "Slot2", "Slot 2"},
        {"PhotoShootProject::SaveDialog", "Slot1", "Slot 1"},
        {"PhotoShootProject::SaveDialog", "Slot2", "Slot 2"},
        {"PhotoShootProject::ModeDialog", "TOOLTIP|Quality", "Set image quality when saving. Higher quality means larger file size."},
    };
    for (const auto& t : kTexts) project.reword(t[0], t[1], t[2]);
}

bool load_saved_game(Runtime& rt) {
    const std::shared_ptr<GameNatives> n = natives_of(rt);
    if (!n || rt.data_dir().empty()) return false;
    n->checked = true;
    if (n->card().exists() != 0) return false;
    if (n->load_game() == save::Result::Ok) return true;
    n->load_failure_told = true;
    tell(rt, "The saved game could not be read:\n" + folded(n->card().folder()));
    return false;
}

std::string save_game_now(Runtime& rt) {
    const std::shared_ptr<GameNatives> n = natives_of(rt);
    if (!n || rt.config().console_card || rt.data_dir().empty()) return "There is no folder to save in.";
    if (rt.is_prologue() ? !n->checked : !n->state().valid_game_data) return "No game has been started yet: Gran Turismo mode starts one.";
    const save::Result r = n->write_game("Game Data");
    if (r != save::Result::Ok) return "The game could not be saved in " + n->card().folder() + ". Check that the folder can be written in.";
    n->saved_print = print_of(*n);
    return {};
}

void autosave(Runtime& rt) {
    const std::shared_ptr<GameNatives> n = natives_of(rt);
    if (!n || rt.config().console_card || rt.data_dir().empty()) return;
    GameState& s = n->state();
    const bool prologue = rt.is_prologue();
    // (Gran Turismo 4: a game loaded or made, its options' Autosave on; Prologue has no new game's page: its first progress makes
    // the game.)
    if (prologue) {
        if (!n->checked) return;
    } else {
        const auto option = s.options.find("auto_save");
        if (!s.valid_game_data || n->card().exists() != 0 || (option != s.options.end() && std::holds_alternative<int>(option->second) && std::get<int>(option->second) == 0)) return;
    }
    // Not while a championship runs (the original saves on the map only, which a running championship reaches only by leaving
    // it: then it is kept, 0x119BD8): such a save would hold the series' page with no series and no menu stack.
    if (!prologue && !s.series.label.empty() && !s.suspended) return;
    const u32 now = print_of(*n);
    if (!n->saved_print) { // the game as it was loaded (or a disc just started)
        n->saved_print = now;
        return;
    }
    if (now == n->saved_print) return;
    n->saved_print = now; // (told once however it goes)
    const auto before = std::chrono::steady_clock::now();
    const save::Result r = n->write_game("Game Data");
    rt.note("autosave: " + std::string(save::result_name(r)) + ", " +
            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count()) + " ms");
    if (r == save::Result::Ok) n->save_failure_told = false;
    else if (!n->save_failure_told) {
        n->save_failure_told = true;
        tell(rt, "The game could not be saved in\n" + folded(n->card().folder()) + "\nCheck that the folder can be written in.");
    }
}

} // namespace gt4r::menu
