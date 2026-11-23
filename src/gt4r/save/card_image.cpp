#include "gt4r/save/card_image.h"

#include "formats/ps2/memcard.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace gt4r::save {

namespace fs = std::filesystem;

namespace {

// The order the game writes its files in (save.md "The folder"); others after them by name.
int file_order(const std::string& name, const std::string& folder) {
    if (name == "garage") return 0;
    if (name == folder) return 1;
    if (name == "icon0.ico") return 2;
    if (name == "icon.sys") return 3;
    return 4;
}

} // namespace

std::string game_folder_name(const std::string& product) { return product + "GAMEDATA"; }

bool is_card_image(ByteSpan image) { return gt3r::ps2::is_card_image(image); }

CardImageResult import_card(ByteSpan image, const std::string& card, const std::string& product) {
    CardImageResult r;
    if (!gt3r::ps2::is_card_image(image)) {
        r.message = "not a memory card image (a formatted 8 MB card, with or without its spare bytes)";
        return r;
    }
    std::vector<gt3r::ps2::McDir> dirs;
    try {
        dirs = gt3r::ps2::read_card(image);
    } catch (const std::exception& e) {
        r.message = e.what();
        return r;
    }
    const std::string name = game_folder_name(product);
    const gt3r::ps2::McDir* game = nullptr;
    for (const gt3r::ps2::McDir& d : dirs) {
        r.folders.push_back(d.name);
        if (d.name == name) game = &d;
    }
    if (!game) {
        r.message = "the card holds no " + name + " folder";
        return r;
    }
    std::error_code ec;
    const fs::path to = fs::path(card) / name;
    fs::remove_all(to, ec);
    fs::create_directories(to, ec);
    for (const gt3r::ps2::McFile& f : game->files) {
        std::ofstream out(to / f.name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(f.data.data()), std::streamsize(f.data.size()));
        if (!out) {
            r.message = "cannot write " + (to / f.name).string();
            return r;
        }
    }
    r.ok = true;
    r.message = std::to_string(game->files.size()) + " files of " + name + " copied to " + to.string();
    return r;
}

Bytes export_card(const std::string& card, std::optional<ByteSpan> base, std::string* why, const std::string& product) {
    auto fail = [why](std::string m) {
        if (why) *why = std::move(m);
        return Bytes{};
    };
    const std::string name = game_folder_name(product);
    const fs::path from = fs::path(card) / name;
    std::error_code ec;
    if (!fs::exists(from / name, ec)) return fail("no saved game in " + from.string());
    gt3r::ps2::McDir dir;
    dir.name = name;
    for (const fs::directory_entry& e : fs::directory_iterator(from, ec)) {
        if (!e.is_regular_file()) continue;
        std::ifstream in(e.path(), std::ios::binary);
        gt3r::ps2::McFile f;
        f.name = e.path().filename().string();
        f.data.assign(std::istreambuf_iterator<char>(in), {});
        dir.files.push_back(std::move(f));
    }
    std::stable_sort(dir.files.begin(), dir.files.end(), [&name](const gt3r::ps2::McFile& a, const gt3r::ps2::McFile& b) {
        const int x = file_order(a.name, name), y = file_order(b.name, name);
        return x != y ? x < y : a.name < b.name;
    });
    // The folder's time: its game data file's.
    if (const auto t = fs::last_write_time(from / name, ec); !ec) {
        const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        dir.time = std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
    }
    std::vector<gt3r::ps2::McDir> dirs;
    try {
        if (base) {
            dirs = gt3r::ps2::read_card(*base);
            dirs.erase(std::remove_if(dirs.begin(), dirs.end(), [&name](const gt3r::ps2::McDir& d) { return d.name == name; }), dirs.end());
        }
        dirs.push_back(std::move(dir));
        return gt3r::ps2::make_card(dirs);
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}

CardImageResult import_dropped_card(const std::string& card, const std::string& product) {
    CardImageResult r;
    const std::string name = game_folder_name(product);
    std::error_code ec;
    if (card.empty() || fs::exists(fs::path(card) / name / name, ec)) return r;
    std::vector<fs::path> images;
    for (const fs::directory_entry& e : fs::directory_iterator(card, ec)) {
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (e.is_regular_file() && (ext == ".ps2" || ext == ".mcd")) images.push_back(e.path());
    }
    std::sort(images.begin(), images.end());
    for (const fs::path& p : images) {
        std::ifstream in(p, std::ios::binary);
        const Bytes image((std::istreambuf_iterator<char>(in)), {});
        r = import_card(image, card, product);
        if (r.ok) {
            r.message += " (from " + p.filename().string() + ")";
            return r;
        }
    }
    return CardImageResult{};
}

} // namespace gt4r::save
