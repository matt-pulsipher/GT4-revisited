#include "gt4r/save/save_edit.h"

#include "gt4r/save/card_image.h"
#include "gt4r/save/codec.h"
#include "gt4r/save/prologue_data.h"

#include "formats/ps2/memcard.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>

namespace gt4r::save {

namespace fs = std::filesystem;
using menu::CarRecord;
using menu::GameState;

namespace {

constexpr size_t kHeader = 0x40;

u32 get32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
void put32(u8* p, u32 v) { p[0] = u8(v), p[1] = u8(v >> 8), p[2] = u8(v >> 16), p[3] = u8(v >> 24); }

u32 clock_word() {
    static std::mt19937 random(std::random_device{}());
    return random();
}

// "BASCUS-97328GAMEDATA" of "BASCUS-97328"; "BISCPS-15055GAMEDATA" of Prologue's.
std::string folder_of(const std::string& product) { return game_folder_name(product); }
bool is_prologue(const std::string& product) { return product.rfind("BI", 0) == 0; }

std::optional<Bytes> read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    Bytes b((std::istreambuf_iterator<char>(in)), {});
    if (in.bad()) return std::nullopt;
    return b;
}

// Beside the file, then put in its place: a file is the old one or the new one, never half of one.
bool write_file(const fs::path& p, ByteSpan data, std::string* why) {
    const fs::path tmp = p.string() + ".new";
    std::error_code ec;
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (out) out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (out) out.flush();
        if (!out) {
            if (why) *why = "cannot write " + p.filename().string() + " (is the disk full, or the folder read-only?)";
            out.close();
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        if (why) *why = "cannot replace " + p.filename().string() + ": " + ec.message();
        std::error_code ignored;
        fs::remove(tmp, ignored);
        return false;
    }
    return true;
}

bool set_why(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

std::string time_stamp() {
    const std::time_t t = std::time(nullptr);
    const std::tm tm = *std::localtime(&t);
    char b[40];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d.%02d.%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

// ---- A card image's pages (docs: formats/ps2/memcard.h of the shared engine).
constexpr size_t kPage = 512, kSpare = 16, kCluster = 1024, kEntry = 512;

// The spare area's error-correcting bytes: three for each 128 bytes of a page (the parities of the bits' columns, then those of
// the odd bytes' places and of their complements).
void page_ecc(const u8* page, u8* spare) {
    auto odd = [](u32 v) { return std::popcount(v) & 1; };
    for (int k = 0; k < 4; ++k) {
        u8 column = 0x77, line0 = 0x7F, line1 = 0x7F;
        for (int i = 0; i < 128; ++i) {
            const u32 b = page[k * 128 + i];
            static constexpr u32 kMasks[7] = {0x55, 0x33, 0x0F, 0x00, 0xAA, 0xCC, 0xF0};
            for (int m = 0; m < 7; ++m) column ^= u8(odd(b & kMasks[m]) << m);
            if (odd(b)) line0 ^= u8(~i), line1 ^= u8(i);
        }
        spare[k * 3] = column, spare[k * 3 + 1] = u8(line0 & 0x7F), spare[k * 3 + 2] = line1;
    }
}

} // namespace

std::optional<Bytes> replace_in_card(ByteSpan image, const std::string& folder, const std::string& file, ByteSpan data, std::string* why) {
    if (!gt3r::ps2::is_card_image(image)) return set_why(why, "not a memory card image"), std::nullopt;
    Bytes d(image.begin(), image.end());
    const size_t stride = d.size() == gt3r::ps2::kCardSizeEcc ? kPage + kSpare : kPage;
    const u8* s = d.data();
    if ((s[0x28] | s[0x29] << 8) != int(kPage) || (s[0x2A] | s[0x2B] << 8) != 2) return set_why(why, "a card of another layout"), std::nullopt;
    const u32 alloc = get32(s + 0x34), root = get32(s + 0x3C);
    const size_t clusters = d.size() / stride / 2;
    auto at = [&](size_t cluster, size_t page) { return (cluster * 2 + page) * stride; };
    auto word = [&](size_t cluster, size_t offset) { return get32(d.data() + at(cluster, offset / kPage) + offset % kPage); };
    auto next = [&](u32 c) -> u32 {
        if (c / 256 / 256 >= 32) return 0xFFFFFFFFu;
        const u32 ind = get32(d.data() + 0x50 + 4 * size_t(c / 256 / 256));
        if (ind >= clusters) return 0xFFFFFFFFu;
        const u32 fat = word(ind, 4 * size_t(c / 256 % 256));
        return fat < clusters ? word(fat, 4 * size_t(c % 256)) : 0xFFFFFFFFu;
    };
    // The clusters of a chain, `bytes` long.
    auto chain = [&](u32 first, size_t bytes) {
        std::vector<u32> out;
        u32 c = first;
        while (out.size() * kCluster < bytes && out.size() <= clusters && size_t(c) + alloc < clusters) {
            out.push_back(c + alloc);
            const u32 n = next(c);
            if (out.size() * kCluster >= bytes) break;
            if (!(n & 0x80000000u) || n == 0xFFFFFFFFu) break;
            c = n & 0x7FFFFFFFu;
        }
        return out;
    };
    // An entry of a directory by its name: its first cluster and length.
    struct Found {
        u32 cluster, length;
    };
    auto find = [&](u32 first, u32 entries, const std::string& name, u16 kind) -> std::optional<Found> {
        const std::vector<u32> cs = chain(first, size_t(entries) * kEntry);
        for (u32 i = 0; i < entries && size_t(i) / 2 < cs.size(); ++i) {
            const u8* e = d.data() + at(cs[i / 2], i % 2);
            const u16 mode = u16(e[0] | e[1] << 8);
            if ((mode & 0x8000) == 0 || (mode & kind) == 0) continue;
            char n[33] = {};
            std::memcpy(n, e + 0x40, 32);
            if (name == n) return Found{get32(e + 0x10), get32(e + 4)};
        }
        return std::nullopt;
    };
    const std::vector<u32> first = chain(root, kEntry);
    if (first.empty()) return set_why(why, "the card's directory does not read"), std::nullopt;
    const u32 count = get32(d.data() + at(first[0], 0) + 4);
    const auto dir = count <= 4096 ? find(root, count, folder, 0x20) : std::nullopt;
    if (!dir || dir->length > 4096) return set_why(why, "the card holds no " + folder), std::nullopt;
    const auto f = find(dir->cluster, dir->length, file, 0x10);
    if (!f) return set_why(why, folder + " holds no " + file), std::nullopt;
    if (f->length != data.size()) return set_why(why, file + " has another length on the card"), std::nullopt;
    const std::vector<u32> cs = chain(f->cluster, data.size());
    if (cs.size() * kCluster < data.size()) return set_why(why, "the card's list of " + file + " ends early"), std::nullopt;
    for (size_t o = 0; o < data.size(); o += kPage) {
        u8* page = d.data() + at(cs[o / kCluster], o % kCluster / kPage);
        const size_t n = std::min(kPage, data.size() - o);
        if (std::memcmp(page, data.data() + o, n) == 0) continue;
        std::memcpy(page, data.data() + o, n);
        if (stride != kPage) page_ecc(page, page + kPage);
    }
    return d;
}

std::optional<EditPlace> edit_place(const std::string& path, const std::string& product, std::string* why) {
    std::error_code ec;
    const fs::path p(path);
    const std::string name = folder_of(product);
    if (fs::is_directory(p, ec)) {
        if (fs::is_regular_file(p / name, ec)) return EditPlace{EditPlace::Kind::Folder, p.string()};
        if (fs::is_regular_file(p / name / name, ec)) return EditPlace{EditPlace::Kind::Folder, (p / name).string()};
        set_why(why, "That folder holds no saved game of this disc (" + name + ").");
        return std::nullopt;
    }
    if (!fs::is_regular_file(p, ec)) {
        set_why(why, "There is no such file.");
        return std::nullopt;
    }
    if (p.filename() == name) return EditPlace{EditPlace::Kind::Folder, p.parent_path().string()};
    if (const std::optional<Bytes> b = read_file(p); b && is_card_image(*b)) {
        try {
            for (const gt3r::ps2::McDir& d : gt3r::ps2::read_card(*b))
                if (d.name == name) return EditPlace{EditPlace::Kind::Image, p.string()};
            set_why(why, "That memory card image holds no saved game of this disc (" + name + ").");
        } catch (const std::exception& e) {
            set_why(why, std::string("That memory card image does not read: ") + e.what());
        }
        return std::nullopt;
    }
    set_why(why, "That is neither a memory card image nor a saved game of this disc (" + name + ").");
    return std::nullopt;
}

std::optional<EditFiles> read_files(const EditPlace& place, const std::string& product, std::string* why) {
    const std::string name = folder_of(product);
    const bool prologue = is_prologue(product);
    EditFiles out;
    if (place.kind == EditPlace::Kind::Folder) {
        const std::optional<Bytes> game = read_file(fs::path(place.path) / name);
        const std::optional<Bytes> garage = prologue ? Bytes{} : read_file(fs::path(place.path) / "garage");
        if (!game) return set_why(why, "The save's file " + name + " cannot be read."), std::nullopt;
        if (!garage) return set_why(why, "The save's garage file cannot be read."), std::nullopt;
        out.game = *game, out.garage = *garage;
        return out;
    }
    const std::optional<Bytes> image = read_file(place.path);
    if (!image) return set_why(why, "The memory card image cannot be read."), std::nullopt;
    try {
        for (const gt3r::ps2::McDir& d : gt3r::ps2::read_card(*image)) {
            if (d.name != name) continue;
            bool game = false, garage = prologue;
            for (const gt3r::ps2::McFile& f : d.files) {
                if (f.name == name) out.game = f.data, game = true;
                if (f.name == "garage" && !prologue) out.garage = f.data, garage = true;
            }
            if (game && garage) return out;
            return set_why(why, "The saved game on the card lacks a file."), std::nullopt;
        }
    } catch (const std::exception& e) {
        return set_why(why, std::string("The memory card image does not read: ") + e.what()), std::nullopt;
    }
    return set_why(why, "The card holds no " + name + "."), std::nullopt;
}

bool write_files(const EditPlace& place, const std::string& product, const EditFiles& files, std::string* why) {
    const std::string name = folder_of(product);
    const bool prologue = is_prologue(product);
    if (place.kind == EditPlace::Kind::Folder) {
        // As the program saves: the garage first, then the game data.
        if (!prologue && read_file(fs::path(place.path) / "garage") != files.garage && !write_file(fs::path(place.path) / "garage", files.garage, why)) return false;
        return write_file(fs::path(place.path) / name, files.game, why);
    }
    std::optional<Bytes> image = read_file(place.path);
    if (!image) return set_why(why, "the memory card image cannot be read");
    if (!prologue) {
        image = replace_in_card(*image, name, "garage", files.garage, why);
        if (!image) return false;
    }
    image = replace_in_card(*image, name, name, files.game, why);
    return image && write_file(place.path, *image, why);
}

bool SaveDocument::decode(std::string* why) {
    prologue_ = is_prologue(product_);
    loaded_ = GameState();
    if (prologue_) {
        if (!valid_prologue(files_.game) || !read_prologue(files_.game, loaded_)) return set_why(why, "This is not a whole saved game of Gran Turismo 4 Prologue.");
        payload_ = files_.game;
        return true;
    }
    const std::optional<Bytes> payload = unscramble(files_.game, 0);
    if (!payload) return set_why(why, "The save's checksum is wrong: the file is damaged.");
    if (!valid_game(*payload)) return set_why(why, "This is not a whole saved game of this disc's release.");
    std::vector<int> wanted;
    if (!read_game(*payload, loaded_, codes_, &wanted)) return set_why(why, "The saved game does not read.");
    payload_.assign(payload->begin(), payload->begin() + kPayloadSize);
    if (files_.garage.size() < kHeader) return set_why(why, "The save's garage file is too short.");
    u8 h[kHeader];
    std::memcpy(h, files_.garage.data(), kHeader);
    decode_header(h, loaded_.garage_key);
    if (get32(h + 0x38) != loaded_.garage_key || (get32(h + 0x2C) != loaded_.garage_serial && get32(h + 0x30) != loaded_.garage_serial))
        return set_why(why, "The garage file belongs to another saved game.");
    loaded_.money = s64(u64(get32(h)) << 32 | get32(h + 4));
    for (int i : wanted) {
        menu::GarageEntry& e = loaded_.garage[size_t(i)];
        const size_t at = kHeader + size_t(i) * kSlotSize;
        if (files_.garage.size() < at + kSlotSize) continue;
        if (const std::optional<Bytes> rec = decode_slot(ByteSpan(files_.garage.data() + at, kSlotSize), loaded_.garage_key, u32(i))) {
            e.record.emplace();
            std::memcpy(e.record->bytes.data(), rec->data(), CarRecord::kSize);
        }
    }
    return true;
}

std::optional<SaveDocument> SaveDocument::from_files(EditFiles files, const Codes& codes, const std::string& product, std::string* why) {
    SaveDocument d;
    d.product_ = product, d.codes_ = codes, d.files_ = std::move(files);
    if (!d.decode(why)) return std::nullopt;
    d.state = d.loaded_;
    return d;
}

std::optional<SaveDocument> SaveDocument::open(const EditPlace& place, const Codes& codes, const std::string& product, std::string* why) {
    std::optional<EditFiles> files = read_files(place, product, why);
    if (!files) return std::nullopt;
    std::optional<SaveDocument> d = from_files(std::move(*files), codes, product, why);
    if (d) d->place_ = place;
    return d;
}

bool SaveDocument::reload(std::string* why) {
    const std::optional<EditFiles> files = read_files(place_, product_, why);
    if (!files) return false;
    SaveDocument d = *this;
    d.files_ = *files;
    if (!d.decode(why)) return false;
    d.state = d.loaded_;
    *this = std::move(d);
    return true;
}

// The payload read, with the bytes the changed state writes differently from the state read.
Bytes SaveDocument::changed_payload() const {
    const Bytes before = prologue_ ? write_prologue(loaded_) : write_game(loaded_, codes_);
    const Bytes after = prologue_ ? write_prologue(state) : write_game(state, codes_);
    Bytes out = payload_;
    const size_t n = std::min({before.size(), after.size(), out.size()});
    bool any = false;
    for (size_t i = 0x10; i < n; ++i)
        if (before[i] != after[i]) out[i] = after[i], any = true;
    if (any) put32(out.data() + 8, crc32(ByteSpan(out.data() + 0x10, (prologue_ ? kProloguePayloadSize : kPayloadSize) - 0x10)));
    return out;
}

bool SaveDocument::changed() const {
    if (changed_payload() != payload_) return true;
    if (prologue_) return false;
    if (state.money != loaded_.money) return true;
    for (size_t i = 0; i < state.garage.size(); ++i) {
        if (!state.garage[i].record || int(i) == state.riding_car) continue;
        if (i >= loaded_.garage.size() || !loaded_.garage[i].record || loaded_.garage[i].record->bytes != state.garage[i].record->bytes) return true;
    }
    return false;
}

std::optional<EditFiles> SaveDocument::files(std::string* why) const {
    EditFiles out = files_;
    const Bytes payload = changed_payload();
    if (prologue_) {
        out.game = payload;
        if (!valid_prologue(out.game)) return set_why(why, "the changed save did not check"), std::nullopt;
        return out;
    }
    if (payload != payload_) out.game = scramble(payload, 0, clock_word());
    u8 h[kHeader];
    std::memcpy(h, out.garage.data(), kHeader);
    decode_header(h, loaded_.garage_key);
    if (state.money != loaded_.money) {
        put32(h, u32(u64(state.money) >> 32)), put32(h + 4, u32(state.money));
        encode_header(h, loaded_.garage_key);
        std::memcpy(out.garage.data(), h, kHeader);
    }
    // A slot holds its car's record: the program writes it when the car stops being the one driven, whose record is in the
    // game data.
    for (size_t i = 0; i < state.garage.size() && i < GameState::kGarageSize; ++i) {
        if (!state.garage[i].record || int(i) == state.riding_car) continue;
        const ByteSpan record(state.garage[i].record->bytes.data(), CarRecord::kSize);
        const size_t at = kHeader + i * kSlotSize;
        if (out.garage.size() < at + kSlotSize) return set_why(why, "the garage file has no room for that car"), std::nullopt;
        // (Not changed: its slot as it is.)
        if (int(i) != loaded_.riding_car && i < loaded_.garage.size() && loaded_.garage[i].record && loaded_.garage[i].record->bytes == state.garage[i].record->bytes) continue;
        if (const std::optional<Bytes> have = decode_slot(ByteSpan(out.garage.data() + at, kSlotSize), loaded_.garage_key, u32(i));
            have && std::equal(record.begin(), record.end(), have->begin()))
            continue;
        const Bytes slot = encode_slot(record, loaded_.garage_key, u32(i), clock_word());
        std::memcpy(out.garage.data() + at, slot.data(), kSlotSize);
    }
    // Read back as the program reads them: the same state.
    std::string inner;
    const std::optional<SaveDocument> back = from_files(out, codes_, product_, &inner);
    if (!back) return set_why(why, "the changed save did not read back: " + inner), std::nullopt;
    if (back->payload_ != payload || back->state.money != state.money) return set_why(why, "the changed save did not read back the same"), std::nullopt;
    for (size_t i = 0; i < state.garage.size(); ++i) {
        const menu::GarageEntry& e = state.garage[i];
        if (!e.record) continue;
        if (i >= back->state.garage.size() || !back->state.garage[i].record || back->state.garage[i].record->bytes != e.record->bytes)
            return set_why(why, "a car's settings did not read back the same"), std::nullopt;
    }
    return out;
}

SaveDocument::Written SaveDocument::write(const std::string& backups) {
    Written r;
    r.backup = backup_;
    if (place_.path.empty()) {
        r.message = "this save has no file";
        return r;
    }
    std::string why;
    const std::optional<EditFiles> now = read_files(place_, product_, &why);
    if (!now) {
        r.message = why;
        return r;
    }
    if (now->game != files_.game || now->garage != files_.garage) {
        r.message = "The save has changed since it was opened here (the game wrote it). Nothing was written.";
        return r;
    }
    const std::optional<EditFiles> out = files(&why);
    if (!out) {
        r.message = "Nothing was written: " + why + ".";
        return r;
    }
    // The backup: the files as they are, before the first change.
    if (!backups.empty() && backup_.empty()) {
        std::error_code ec;
        const fs::path from(place_.path);
        const std::string what = place_.kind == EditPlace::Kind::Image ? from.filename().string() : from.parent_path().filename().string();
        fs::path to = fs::path(backups) / (time_stamp() + " " + (what.empty() ? "save" : what));
        for (int i = 2; fs::exists(to, ec); ++i) to = fs::path(backups) / (time_stamp() + " (" + std::to_string(i) + ") " + what);
        fs::create_directories(place_.kind == EditPlace::Kind::Image ? to.parent_path() : to, ec);
        if (place_.kind == EditPlace::Kind::Image) fs::copy_file(from, to, ec);
        else fs::copy(from, to / from.filename(), fs::copy_options::recursive, ec);
        if (ec) {
            r.message = "Nothing was written: the backup could not be made (" + ec.message() + ").";
            return r;
        }
        backup_ = to.string();
    }
    r.backup = backup_;
    if (!write_files(place_, product_, *out, &why)) {
        // (Each file is the old one or the new one: put back what was.)
        std::string ignored;
        write_files(place_, product_, files_, &ignored);
        r.message = "The save was not written: " + why + ".";
        return r;
    }
    const std::optional<EditFiles> back = read_files(place_, product_, &why);
    if (!back || back->game != out->game || back->garage != out->garage) {
        std::string ignored;
        write_files(place_, product_, files_, &ignored);
        r.message = "The save did not read back as written. The files were put back as they were.";
        return r;
    }
    SaveDocument d = *this;
    d.files_ = *out;
    if (d.decode(&why)) {
        d.state = d.loaded_;
        *this = std::move(d);
    }
    r.ok = true;
    return r;
}

} // namespace gt4r::save
