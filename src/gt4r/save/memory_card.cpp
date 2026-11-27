#include "gt4r/save/memory_card.h"

#include "gt4r/save/codec.h"
#include "gt4r/save/prologue_data.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>

namespace gt4r::save {

namespace fs = std::filesystem;
using menu::CarRecord;
using menu::GameState;

namespace {

constexpr size_t kHeader = 0x40, kGarageFile = kHeader + GameState::kGarageSize * kSlotSize;

u32 get32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
void put32(u8* p, u32 v) { p[0] = u8(v), p[1] = u8(v >> 8), p[2] = u8(v >> 16), p[3] = u8(v >> 24); }

// The program's clock words (any value serves: nothing compares them with a clock).
u32 clock_word() {
    static std::mt19937 random(u32(std::chrono::steady_clock::now().time_since_epoch().count()));
    return u32(std::chrono::system_clock::now().time_since_epoch().count()) ^ random();
}

int kb(size_t bytes) { return int((bytes + 1023) / 1024); }

// ASCII as the full-width Shift-JIS letters the save's title is written in (0x1D0438).
Bytes full_width(const std::string& s) {
    Bytes out;
    for (unsigned char c : s) {
        u16 code = 0x8140; // a full-width space
        if (c >= '0' && c <= '9') code = u16(0x824F + (c - '0'));
        else if (c >= 'A' && c <= 'Z') code = u16(0x8260 + (c - 'A'));
        else if (c >= 'a' && c <= 'z') code = u16(0x8281 + (c - 'a'));
        else if (c == '-') code = 0x817C;
        else if (c == '.') code = 0x8144;
        else if (c == ':') code = 0x8146;
        else if (c == '\'') code = 0x8166;
        out.push_back(u8(code >> 8)), out.push_back(u8(code));
    }
    return out;
}

} // namespace

const char* result_name(Result r) {
    switch (r) {
    case Result::Ok: return "OK";
    case Result::DeviceError: return "DEVICE_ERROR";
    case Result::InvalidFile: return "INVALID_FILE";
    case Result::FileNotFound: return "FILE_NOT_FOUND";
    case Result::DifferentMemoryCard: return "DIFFERENT_MEMORYCARD";
    case Result::NoCard: return "NO_CARD";
    case Result::NoOperation: return "NO_OPERATION";
    }
    return "UNKOWN RESULT";
}

bool is_prologue_product(const std::string& product) {
    const bool region = product.size() > 2 && product[0] == 'B' && product[1] >= 'A' && product[1] <= 'Z' && product[2] == 'S';
    const std::string p = region ? product.substr(2) : product;
    return p == "SCPS-15055" || p == "SCES-52438" || p == "SCKA-20022";
}

GameSave::GameSave(std::string card, Codes codes, Reader disc, std::string product)
    : card_(std::move(card)), codes_(std::move(codes)), disc_(std::move(disc)) {
    // The folder: "BA" + the product + "GAMEDATA" (0x1CC958, 0x1CDA48); its data file has the
    // folder's name.
    // (A product with its region's letters already, "BISCPS-15055" (Prologue's 0x19BCB0), keeps them.)
    const bool region = product.size() > 2 && product[0] == 'B' && (product[1] == 'A' || product[1] == 'I' || product[1] == 'E');
    name_ = (region ? product : "BA" + product) + "GAMEDATA";
    prologue_ = is_prologue_product(product);
    layout_ = layout_of_product(product);
    if (!card_.empty()) folder_ = (fs::path(card_) / name_).string() + "/";
}

std::optional<Bytes> GameSave::read(const std::string& file) const {
    if (folder_.empty()) return std::nullopt;
    std::ifstream in(folder_ + file, std::ios::binary);
    if (!in) return std::nullopt;
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

bool GameSave::write(const std::string& file, ByteSpan data) const {
    if (folder_.empty()) return false;
    std::error_code ec;
    fs::create_directories(folder_, ec);
    const std::string path = folder_ + file, tmp = path + ".new";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!out) return false;
    }
    fs::rename(tmp, path, ec);
    return !ec;
}

std::string GameSave::icon_path() const { return prologue_ ? "icon/gamedata.ico" : "icon/GT4_Gamedata.ico"; }

// Prologue's save: its game data file as the payload is, the icons (FileGT4GameData, 0x19C0F0: the icon
// "icon%d.ico" and "/%s/icon.sys" from the disc's icon/icon.sys).
Result GameSave::save_prologue(GameState& s, const std::string* title) {
    const Bytes payload = write_prologue(s);
    if (!write(name_, payload)) return Result::DeviceError;
    s.saved_image = payload;
    if (title && disc_) {
        if (const std::optional<Bytes> icon = disc_(icon_path())) write("icon0.ico", *icon);
        if (std::optional<Bytes> sys = disc_("icon/icon.sys"); sys && sys->size() >= 0x1C4) {
            // ❓ The title's text: the program builds it at +0xC0 (0x19C488 with "[%d]"); here the title given.
            Bytes text = full_width(*title);
            text.resize(std::min<size_t>(text.size(), 0x43));
            std::memset(sys->data() + 0xC0, 0, 0x44);
            std::memcpy(sys->data() + 0xC0, text.data(), text.size());
            (*sys)[6] = 0, (*sys)[7] = 0;
            for (size_t at : {0x104, 0x144, 0x184}) {
                std::memset(sys->data() + at, 0, 0x40);
                std::memcpy(sys->data() + at, "icon0.ico", 9);
            }
            write("icon.sys", *sys);
        }
    }
    return Result::Ok;
}

int GameSave::exists() const {
    if (!connected()) return -1;
    std::error_code ec;
    return fs::exists(folder_ + name_, ec) ? 0 : 1;
}

int GameSave::used_kb() const {
    if (exists() != 0) return -1;
    // The folder's files in KB, plus one KB a two entries and one (0x1CC3C0).
    int total = 0, entries = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder_, ec)) total += kb(size_t(e.file_size(ec))), ++entries;
    return total + (entries + 1) / 2 + 1;
}

int GameSave::needed_kb() const {
    const std::optional<Bytes> icon = disc_ ? disc_(icon_path()) : std::nullopt;
    if (prologue_) return kb(kProloguePayloadSize) + 1 + (icon ? kb(icon->size()) : 0) + (3 + 1) / 2 + 1;
    return kb(scrambled_size(kPayloadSize)) + kb(kGarageFile) + 1 + (icon ? kb(icon->size()) : 0) + (4 + 1) / 2 + 1;
}

// The garage file's header brought up to date (0x1CE868): checked against the game's key and
// serial, the money put in, the serial slot not holding the game's serial given `serial`, a
// new stamp.
Result GameSave::update_header(GameState& s, u32 serial, bool money) {
    std::optional<Bytes> file = read("garage");
    if (!file || file->size() < kHeader) return Result::InvalidFile;
    u8* h = file->data();
    decode_header(h, s.garage_key);
    if (get32(h + 0x38) != s.garage_key || (get32(h + 0x2C) != s.garage_serial && get32(h + 0x30) != s.garage_serial) ||
        (stamp_ && get32(h + 0x34) != stamp_ && get32(h + 0x3C) != stamp_))
        return Result::DifferentMemoryCard;
    if (money) put32(h, u32(u64(s.money) >> 32)), put32(h + 4, u32(s.money));
    if (get32(h + 0x2C) == s.garage_serial) put32(h + 0x30, serial);
    else put32(h + 0x2C, serial);
    const u32 previous = get32(h + 0x3C);
    u32 stamp = previous + clock_word();
    if (!stamp) stamp = 1;
    put32(h + 0x34, previous), put32(h + 0x3C, stamp);
    encode_header(h, s.garage_key);
    // The header and the cars' records, each in its slot (the program writes a slot when the
    // car driven changes; here all at once).
    for (size_t i = 0; i < s.garage.size() && i < GameState::kGarageSize; ++i)
        if (s.garage[i].record) {
            const Bytes slot = encode_slot(ByteSpan(s.garage[i].record->bytes.data(), CarRecord::kSize), s.garage_key, u32(i), clock_word());
            std::memcpy(file->data() + kHeader + i * kSlotSize, slot.data(), kSlotSize);
        }
    if (!write("garage", *file)) return Result::DeviceError;
    stamp_ = stamp;
    return Result::Ok;
}

Result GameSave::save_new(GameState& s, const std::string& title) {
    if (!connected()) return Result::NoCard;
    if (prologue_) return save_prologue(s, &title);
    // A new key; serial 0; the garage file's slots noise from the key; its header.
    s.garage_key = crc32(ByteSpan(reinterpret_cast<const u8*>(&title[0]), title.size())) ^ clock_word();
    s.garage_serial = 0, stamp_ = 0;
    Bytes garage(kGarageFile, 0);
    fill_new_garage(garage.data() + kHeader, kGarageFile - kHeader, s.garage_key);
    u8* h = garage.data();
    put32(h + 0x38, s.garage_key);
    u32 stamp = clock_word();
    if (!stamp) stamp = 1;
    put32(h + 0x3C, stamp), stamp_ = stamp;
    put32(h + 0x2C, 0), put32(h + 0x30, 1);
    put32(h, u32(u64(s.money) >> 32)), put32(h + 4, u32(s.money));
    encode_header(h, s.garage_key);
    if (!write("garage", garage)) return Result::DeviceError;
    s.garage_serial = 1;
    const Result r = save(s);
    if (r != Result::Ok) return r;
    // The icons: the disc's icon copied, its icon.sys with the title.
    if (disc_) {
        if (const std::optional<Bytes> icon = disc_("icon/GT4_Gamedata.ico")) write("icon0.ico", *icon);
        if (std::optional<Bytes> sys = disc_("icon/icon.sys"); sys && sys->size() >= 0x1C4) {
            // Line one, the program's text at 0x694DD8 (14 full-width letters), then the title.
            Bytes text = full_width("GRAN TURISMO 4");
            const Bytes second = full_width(title);
            text.insert(text.end(), second.begin(), second.end());
            text.resize(std::min<size_t>(text.size(), 0x43));
            std::memset(sys->data() + 0xC0, 0, 0x44);
            std::memcpy(sys->data() + 0xC0, text.data(), text.size());
            (*sys)[6] = 28, (*sys)[7] = 0; // where the second line starts, in bytes (0x1CBED0)
            for (size_t at : {0x104, 0x144, 0x184}) {
                std::memset(sys->data() + at, 0, 0x40);
                std::memcpy(sys->data() + at, "icon0.ico", 9);
            }
            write("icon.sys", *sys);
        }
    }
    return Result::Ok;
}

Result GameSave::save(GameState& s) {
    if (!connected()) return Result::NoCard;
    if (prologue_) return save_prologue(s, nullptr);
    u32 serial = s.garage_serial + 1;
    if (!serial) serial = 1;
    if (const Result r = update_header(s, serial, true); r != Result::Ok) return r;
    const Bytes payload = write_game(s, codes_);
    if (!write(name_, scramble(to_layout(payload, layout_), 0, clock_word()))) return Result::DeviceError;
    s.garage_serial = serial;
    s.saved_image = payload;
    return Result::Ok;
}

Result GameSave::load_instance(GameState& into) const {
    if (!connected()) return Result::NoCard;
    const std::optional<Bytes> file = read(name_);
    if (!file) return Result::FileNotFound;
    if (prologue_) return read_prologue(*file, into) ? Result::Ok : Result::InvalidFile;
    const std::optional<Bytes> file_payload = unscramble(*file, 0);
    const std::optional<Bytes> payload = file_payload ? from_layout(*file_payload, layout_) : std::nullopt;
    if (!payload || !read_game(*payload, into, codes_)) return Result::InvalidFile;
    return Result::Ok;
}

Result GameSave::load(GameState& s) {
    if (!connected()) return Result::NoCard;
    const std::optional<Bytes> file = read(name_);
    if (!file) return Result::DeviceError;
    if (prologue_) {
        GameState loaded = s;
        if (!read_prologue(*file, loaded)) return Result::InvalidFile;
        s.saved_image = std::move(loaded.saved_image), s.username = loaded.username, s.prologue_license = std::move(loaded.prologue_license);
        return Result::Ok;
    }
    const std::optional<Bytes> file_payload = unscramble(*file, 0);
    const std::optional<Bytes> payload = file_payload ? from_layout(*file_payload, layout_) : std::nullopt;
    if (!payload || !valid_game(*payload)) return Result::InvalidFile;
    // The garage file first, so a game whose garage does not match leaves the state as it was.
    std::optional<Bytes> garage = read("garage");
    if (!garage || garage->size() < kHeader) return Result::InvalidFile;
    GameState loaded = s;
    std::vector<int> wanted;
    if (!read_game(*payload, loaded, codes_, &wanted)) return Result::InvalidFile;
    u8* h = garage->data();
    decode_header(h, loaded.garage_key);
    if (get32(h + 0x38) != loaded.garage_key || (get32(h + 0x2C) != loaded.garage_serial && get32(h + 0x30) != loaded.garage_serial))
        return Result::DifferentMemoryCard;
    loaded.money = s64(u64(get32(h)) << 32 | get32(h + 4));
    stamp_ = get32(h + 0x3C);
    for (int i : wanted) {
        menu::GarageEntry& e = loaded.garage[size_t(i)];
        const size_t at = kHeader + size_t(i) * kSlotSize;
        if (garage->size() >= at + kSlotSize)
            if (const std::optional<Bytes> rec = decode_slot(ByteSpan(garage->data() + at, kSlotSize), loaded.garage_key, u32(i))) {
                e.record.emplace();
                std::memcpy(e.record->bytes.data(), rec->data(), CarRecord::kSize);
                continue;
            }
        // ❓ A slot that does not decode: the car as its entry describes it.
        e.kept_flags &= ~u64(2);
    }
    // Options and what the menus keep that a saved game does not hold stay as they were.
    for (const auto& [name, value] : s.options)
        if (!loaded.options.count(name)) loaded.options[name] = value;
    loaded.card = s.card, loaded.valid_game_data = s.valid_game_data, loaded.race = s.race, loaded.menu_stack = s.menu_stack;
    loaded.last_sequence = s.last_sequence, loaded.next_sequence = s.next_sequence, loaded.last_menu_project = s.last_menu_project;
    loaded.next_menu_project = s.next_menu_project, loaded.last_menu_arg = s.last_menu_arg, loaded.next_menu_arg = s.next_menu_arg;
    loaded.menu_result = s.menu_result;
    s = std::move(loaded);
    return Result::Ok;
}

Bytes GameSave::payload(const GameState& s) const { return prologue_ ? write_prologue(s) : to_layout(write_game(s, codes_), layout_); }

Result GameSave::remove() {
    if (!connected()) return Result::NoCard;
    std::error_code ec;
    fs::remove_all(folder_, ec);
    return ec ? Result::DeviceError : Result::Ok;
}

} // namespace gt4r::save
