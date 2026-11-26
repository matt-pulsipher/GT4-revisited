#include "gt4r/save/save_library.h"

#include "gt4r/save/card_image.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace gt4r::save {

namespace fs = std::filesystem;

namespace {

std::time_t file_time(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
    return std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now() +
                                                                                                                   std::chrono::system_clock::now()));
}

} // namespace

SaveLibrary::SaveLibrary(std::string data_dir, std::string product) : dir_(std::move(data_dir)), product_(std::move(product)) {
    const bool region = product_.size() > 2 && product_[0] == 'B' && (product_[1] == 'A' || product_[1] == 'I' || product_[1] == 'E');
    game_ = (region ? product_ : "BA" + product_) + "GAMEDATA";
}

std::string SaveLibrary::marker() const { return (fs::path(dir_) / "games" / ("current-" + product_)).string(); }

bool SaveLibrary::holds_game(const std::string& card) const {
    std::error_code ec;
    return !card.empty() && fs::is_regular_file(fs::path(card) / game_ / game_, ec);
}

std::string SaveLibrary::current() const {
    if (dir_.empty()) return {};
    const std::string first = (fs::path(dir_) / "memorycard").string();
    std::ifstream in(marker());
    std::string name;
    if (in && std::getline(in, name)) {
        const fs::path p = fs::path(name).make_preferred(); // (written with '/': Windows' own separator, as cards() names them)
        // (Inside the data folder: "memorycard" or "games/<number>".)
        if (!name.empty() && p.is_relative() && name.find("..") == std::string::npos) {
            const std::string card = (fs::path(dir_) / p).string();
            if (card == first || holds_game(card) || cards().empty()) return card;
        }
    }
    if (holds_game(first)) return first;
    const std::vector<std::string> all = cards();
    return all.empty() ? first : all[0];
}

bool SaveLibrary::set_current(const std::string& card) {
    if (dir_.empty()) return false;
    std::error_code ec;
    fs::create_directories(fs::path(dir_) / "games", ec);
    std::ofstream out(marker(), std::ios::trunc);
    out << fs::path(card).lexically_relative(dir_).generic_string() << "\n";
    return bool(out);
}

std::vector<std::string> SaveLibrary::cards() const {
    std::vector<std::string> out;
    if (dir_.empty()) return out;
    const std::string first = (fs::path(dir_) / "memorycard").string();
    if (holds_game(first)) out.push_back(first);
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fs::path(dir_) / "games", ec))
        if (e.is_directory() && holds_game(e.path().string())) out.push_back(e.path().string());
    std::sort(out.begin(), out.end(), [this](const std::string& a, const std::string& b) {
        const std::time_t ta = file_time(fs::path(a) / game_ / game_), tb = file_time(fs::path(b) / game_ / game_);
        return ta != tb ? ta > tb : a < b;
    });
    return out;
}

std::string SaveLibrary::new_card() const {
    if (dir_.empty()) return {};
    const std::string first = (fs::path(dir_) / "memorycard").string();
    std::error_code ec;
    if (!fs::exists(fs::path(first) / game_, ec)) return first;
    for (int i = 2;; ++i) {
        char number[16];
        std::snprintf(number, sizeof number, "%04d", i);
        const fs::path p = fs::path(dir_) / "games" / number;
        if (!fs::exists(p / game_, ec)) return p.string();
    }
}

bool SaveLibrary::remove(const std::string& card) {
    if (!holds_game(card)) return false;
    std::error_code ec;
    fs::remove_all(fs::path(card) / game_, ec);
    if (ec) return false;
    std::error_code other;
    if (fs::path(card).parent_path().filename() == "games") fs::remove(card, other); // (when empty)
    return true;
}

u64 SaveLibrary::change_mark(const std::string& card) const {
    if (!holds_game(card)) return 0;
    u64 mark = 0xCBF29CE484222325ull;
    for (const std::string& name : {game_, std::string("garage")}) {
        std::error_code ec;
        const fs::path p = fs::path(card) / game_ / name;
        const auto t = fs::last_write_time(p, ec);
        const u64 size = ec ? 0 : u64(fs::file_size(p, ec));
        for (const u64 v : {ec ? u64(0) : u64(t.time_since_epoch().count()), size}) mark = (mark ^ v) * 0x100000001B3ull;
    }
    return mark ? mark : 1;
}

std::vector<SavedGame> SaveLibrary::games(const Codes& codes) const {
    std::vector<SavedGame> out;
    const std::string now = current();
    for (const std::string& card : cards()) {
        SavedGame g;
        g.card = card, g.current = card == now, g.when = file_time(fs::path(card) / game_ / game_);
        menu::GameState s;
        g.readable = GameSave(card, codes, {}, product_).load(s) == Result::Ok;
        if (g.readable) g.name = s.username, g.money = s.money, g.day = int(s.today) - int(menu::GameState::kFirstDay) + 1, g.cars = s.garage_count();
        out.push_back(std::move(g));
    }
    return out;
}

} // namespace gt4r::save
