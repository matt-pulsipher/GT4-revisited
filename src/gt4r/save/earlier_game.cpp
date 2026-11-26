#include "gt4r/save/earlier_game.h"

#include "gt4r/save/save_library.h"

#include "base/config.h"
#include "base/file.h"
#include "formats/ps2/memcard.h"
#include "game/gt_save.h"
#include "game/save_library.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace gt4r::save {

using gt3r::s8;

namespace {

constexpr size_t kGt3Header = 0x40;      // the data file's header (its first word)
constexpr u16 kGt3Major = 7;             // its version's major (+0x0A); the minor (+0x08) the release's
constexpr size_t kGt3Credits = 0x50;     // the credits (u64, inverted) as the program reads them
constexpr size_t kGt3Cars = 0x70, kGt3Wheels = 0x74; // the garage's cars and wheels (u32, inverted)
constexpr size_t kGt3Licenses = 0x170;   // the license records after the cars (0x204) and the wheels (0x44)
constexpr size_t kGt3Car = 0x204, kGt3Wheel = 0x44, kGt3License = 0x154;
constexpr size_t kGt3Tests = 16;         // B's 8, then A's 8
constexpr size_t kPrologueUnits = 25, kPrologueUnit = 0x2E0, kPrologueMedal = 0x34; // (the Japanese layout; "uk" 0x54)
constexpr size_t kPrologueName = 0x20;

bool ends_ci(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(u8(s[s.size() - n + i])) != suffix[i]) return false;
    return true;
}

u64 le(ByteSpan d, size_t at, size_t n) {
    u64 v = 0;
    for (size_t i = 0; i < n; ++i) v |= u64(d[at + i]) << (8 * i);
    return v;
}

std::string text_at(ByteSpan d, size_t at, size_t n) {
    std::string s;
    for (size_t i = 0; i < n && at + i < d.size() && d[at + i]; ++i) s.push_back(char(d[at + i]));
    return s;
}

std::string file_name(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string with_sep(std::string d) {
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    return d;
}

// The release a Gran Turismo 3 data file's minor names, as a region string.
const char* gt3_release(u16 minor) { return minor == 0 ? "Japanese" : minor == 2 ? "European" : "US"; }
u16 gt3_minor(const std::string& region) { return region == "uk" ? 2 : region == "us" ? 1 : 0; }

// The region's Gran Turismo 3 folder in a card's folders.
std::optional<EarlierGame> from_card(const std::vector<gt3r::ps2::McDir>& dirs, const std::string& region, std::string* why) {
    const std::string folder = gt3_folder(region);
    for (const gt3r::ps2::McDir& d : dirs) {
        if (d.name != folder) continue;
        for (const gt3r::ps2::McFile& f : d.files)
            if (f.name == folder) {
                std::optional<EarlierGame> g = read_gt3_data_file(f.data, region, why);
                if (g && d.time) g->when = std::time_t(d.time);
                return g;
            }
        if (why) *why = "its " + folder + " folder has no game data";
        return std::nullopt;
    }
    if (why) {
        std::string others;
        for (const gt3r::ps2::McDir& d : dirs) {
            if (d.name.find("97102") != std::string::npos || d.name.find("50294") != std::string::npos || d.name.find("15009") != std::string::npos)
                others = d.name;
        }
        *why = others.empty() ? "it holds no Gran Turismo 3 game (" + folder + ")"
                              : "it holds another release's Gran Turismo 3 game (" + others + "); this disc reads " + folder;
    }
    return std::nullopt;
}

} // namespace

std::string gt3_folder(const std::string& region) {
    if (region == "uk") return "BESCES-50294GAMEDATA";
    if (region == "us") return "BASCUS-97102GAMEDATA";
    return "BISCPS-15009GAMEDATA";
}

std::string license_folder(const std::string& region) {
    if (region == "uk") return "BESCES-52438GAMEDATA";
    if (region == "us") return "BASCUS-97102GAMEDATA";
    if (region == "kr") return "BKSCKA-20022GAMEDATA";
    return "BISCPS-15055GAMEDATA";
}

std::optional<s64> transferred_credits(const EarlierGame& g, const std::string& region) {
    if (g.kind != EarlierGame::Kind::Gt3) return std::nullopt;
    u64 c = u64(g.credits);
    if (c == 0) return std::nullopt;
    if (region == "jp") c /= 100; // (Japan's Gran Turismo 3 counts yen)
    return s64(s32(std::min<u64>(c, 100'000)));
}

int transferred_licenses(const EarlierGame& g, const std::string& region) {
    bool b = true, a = true;
    if (licenses_from_gt3(region)) {
        // (Gran Turismo 3's medal words are kept inverted: a test with a medal has its stored byte's top bit set.)
        if (g.kind != EarlierGame::Kind::Gt3 || g.medals.size() < kGt3Tests) return 0;
        for (size_t k = 0; k < kGt3Tests; ++k)
            if (g.medals[k] >= 0x80) (k < 8 ? b : a) = false;
    } else {
        if (g.kind != EarlierGame::Kind::Prologue || g.medals.size() < kPrologueUnits) return 0;
        for (size_t k = 0; k < kPrologueUnits; ++k) {
            const bool passed = s8(g.medals[k]) > 0;
            if (!passed && k < 6) b = false;
            if (!passed) a = false;
        }
    }
    // (The program opens B when either holds, and A after it when A's does.)
    return a ? 2 : b ? 1 : 0;
}

std::optional<EarlierGame> read_gt3_data_file(ByteSpan d, const std::string& region, std::string* why) {
    auto fail = [why](const std::string& w) -> std::optional<EarlierGame> {
        if (why) *why = w;
        return std::nullopt;
    };
    if (d.size() < kGt3Licenses || le(d, 0, 4) != kGt3Header || le(d, 0x0A, 2) != kGt3Major) return fail("it is no Gran Turismo 3 game data");
    const u16 minor = u16(le(d, 8, 2));
    if (minor != gt3_minor(region))
        return fail(std::string("it is the ") + gt3_release(minor) + " Gran Turismo 3's game; this disc reads the " + gt3_release(gt3_minor(region)) + " one's");
    EarlierGame g;
    g.kind = EarlierGame::Kind::Gt3;
    g.from = "PS2 save";
    g.credits = s64(~le(d, kGt3Credits, 8));
    const u64 cars = u32(~le(d, kGt3Cars, 4)), wheels = u32(~le(d, kGt3Wheels, 4));
    const u64 at = kGt3Licenses + cars * kGt3Car + wheels * kGt3Wheel;
    if (cars > 100'000 || wheels > 100'000 || at + kGt3Tests * kGt3License > d.size()) return fail("its game data is cut short");
    for (size_t k = 0; k < kGt3Tests; ++k) g.medals.push_back(u8(~d[at + k * kGt3License]));
    // (The payload's last name entered, at its +0x70, inverted like the rest.)
    for (size_t i = 0; i < 16 && u8(~d[kGt3Header + 0x70 + i]); ++i) g.name.push_back(char(u8(~d[kGt3Header + 0x70 + i])));
    return g;
}

std::optional<EarlierGame> read_prologue_data_file(ByteSpan d, std::string* why) {
    if (d.size() < kPrologueMedal + kPrologueUnits * kPrologueUnit || le(d, 0, 4) != 0xFF60BAD0u) {
        if (why) *why = "it is no Gran Turismo 4 Prologue game data";
        return std::nullopt;
    }
    EarlierGame g;
    g.kind = EarlierGame::Kind::Prologue;
    g.from = "Gran Turismo 4 Prologue";
    g.name = text_at(d, kPrologueName, 0x10);
    for (size_t k = 0; k < kPrologueUnits; ++k) g.medals.push_back(d[kPrologueMedal + k * kPrologueUnit]);
    return g;
}

std::optional<EarlierGame> read_gt3r_save(ByteSpan file, std::string* why) {
    try {
        const gt3r::game::GameState s = gt3r::game::load_from_text(std::string(file.begin(), file.end()));
        EarlierGame g;
        g.kind = EarlierGame::Kind::Gt3;
        g.from = "GT3 Revisited";
        g.name = s.file_comment; // (as GT3 Revisited names it: else its file's name)
        g.credits = s.credits;
        for (size_t k = 0; k < kGt3Tests; ++k) g.medals.push_back(u8(s8(int(s.license_tests[k].medal))));
        return g;
    } catch (const std::exception& e) {
        if (why) *why = std::string("its GT3 Revisited save cannot be read (") + e.what() + ")";
        return std::nullopt;
    }
}

std::optional<EarlierGame> read_earlier_file(ByteSpan b, const std::string& name, const std::string& region, std::string* why) {
    std::optional<EarlierGame> g;
    static const char kGt3r[] = "GT3R-GTMODE";
    if (b.size() >= sizeof kGt3r - 1 && std::memcmp(b.data(), kGt3r, sizeof kGt3r - 1) == 0) {
        g = read_gt3r_save(b, why);
    } else if (gt3r::ps2::is_card_image(b)) {
        try {
            g = from_card(gt3r::ps2::read_card(b), region, why);
            if (g) g->from = "PS2 memory card image";
        } catch (const std::exception& e) {
            if (why) *why = std::string("its memory card cannot be read (") + e.what() + ")";
        }
    } else if (b.size() >= 4 && le(b, 0, 4) == kGt3Header) {
        g = read_gt3_data_file(b, region, why);
    } else if (b.size() >= 0x400 && (ends_ci(name, ".psu") || std::memcmp(b.data() + 0x40, gt3_folder(region).c_str(), gt3_folder(region).size() + 1) == 0)) {
        try {
            g = from_card({gt3r::ps2::read_psu(b)}, region, why);
            if (g) g->from = "PS2 save (.psu)";
        } catch (const std::exception& e) {
            if (why) *why = std::string("its save cannot be read (") + e.what() + ")";
        }
    } else if (why) {
        *why = "it is no GT3 Revisited save, memory card image or Gran Turismo 3 save";
    }
    if (g) {
        g->where = name;
        if (g->name.empty()) g->name = file_name(name);
    }
    return g;
}

std::vector<EarlierGame> find_gt3r_games(const std::string& gt3r_dir, const std::string& region, std::string* looked) {
    std::vector<EarlierGame> out;
    if (gt3r_dir.empty() || region == "kr") return out;
    const std::string data = with_sep(gt3r_dir);
    // The save folder its Settings chose (Android's is a folder picked through the system, not a path: not for us).
    std::string folder = data;
    if (const std::optional<gt3r::IniDocument> cfg = gt3r::IniDocument::load(data + "config.ini"))
        if (const std::optional<std::string> chosen = cfg->get("saves", "folder"); chosen && !chosen->empty() && chosen->rfind("content:", 0) != 0)
            folder = with_sep(*chosen);
    const std::string key = region == "us" ? "" : region == "uk" ? "eu" : "jp";
    const std::string library = folder + "saves/" + (key.empty() ? "" : key + "/");
    if (looked) *looked = library;
    auto add = [&](const std::string& path, s64 mtime, bool current) {
        const std::optional<Bytes> b = gt3r::read_host_file(path);
        if (!b) return;
        std::optional<EarlierGame> g = read_gt3r_save(*b);
        if (!g) return;
        g->where = path, g->when = std::time_t(mtime), g->current = current;
        if (g->name.empty()) {
            g->name = file_name(path);
            if (ends_ci(g->name, ".sav")) g->name.resize(g->name.size() - 4);
        }
        out.push_back(std::move(*g));
    };
    if (gt3r::host_is_directory(library)) {
        const gt3r::game::SaveLibrary lib(library);
        const std::string current = lib.current();
        for (const gt3r::game::SaveInfo& s : lib.list()) // (newest first)
            add(s.path, s.mtime, gt3r::game::same_save_file(s.path, current));
        std::stable_partition(out.begin(), out.end(), [](const EarlierGame& g) { return g.current; });
    } else if (key.empty() && gt3r::host_path_exists(data + "gtmode.sav")) {
        if (looked) *looked = data;
        add(data + "gtmode.sav", gt3r::host_file_mtime(data + "gtmode.sav").value_or(0), true);
    }
    return out;
}

std::vector<EarlierGame> find_card_games(const std::string& card, const std::string& region) {
    std::vector<EarlierGame> out;
    if (card.empty() || region == "kr") return out;
    const std::string dir = with_sep(card), folder = gt3_folder(region);
    if (const std::optional<Bytes> b = gt3r::read_host_file(dir + folder + "/" + folder))
        if (std::optional<EarlierGame> g = read_gt3_data_file(*b, region)) {
            g->from = "PS2 save folder", g->where = dir + folder;
            g->when = std::time_t(gt3r::host_file_mtime(dir + folder + "/" + folder).value_or(0));
            if (g->name.empty()) g->name = folder;
            out.push_back(std::move(*g));
        }
    for (const gt3r::HostDirEntry& e : gt3r::host_dir_entries(dir)) {
        if (e.dir || !(ends_ci(e.name, ".ps2") || ends_ci(e.name, ".mcd"))) continue;
        const std::optional<Bytes> b = gt3r::read_host_file(dir + e.name);
        if (!b || !gt3r::ps2::is_card_image(*b)) continue;
        if (std::optional<EarlierGame> g = read_earlier_file(*b, dir + e.name, region)) {
            g->when = g->when ? g->when : std::time_t(e.mtime);
            out.push_back(std::move(*g));
        }
    }
    return out;
}

std::vector<EarlierGame> find_prologue_games(const std::string& data_dir, const std::string& region) {
    std::vector<EarlierGame> out;
    if (data_dir.empty() || licenses_from_gt3(region) || license_folder(region) != "BISCPS-15055GAMEDATA") return out;
    const SaveLibrary lib(data_dir, "BISCPS-15055");
    const std::string current = lib.current(), folder = "BISCPS-15055GAMEDATA";
    for (const std::string& card : lib.cards()) {
        const std::string path = with_sep(card) + folder + "/" + folder;
        const std::optional<Bytes> b = gt3r::read_host_file(path);
        if (!b) continue;
        if (std::optional<EarlierGame> g = read_prologue_data_file(*b)) {
            g->where = card, g->current = card == current, g->when = std::time_t(gt3r::host_file_mtime(path).value_or(0));
            if (g->name.empty()) g->name = "(no name)";
            out.push_back(std::move(*g));
        }
    }
    return out;
}

} // namespace gt4r::save
