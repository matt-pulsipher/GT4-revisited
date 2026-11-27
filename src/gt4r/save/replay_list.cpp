#include "gt4r/save/replay_list.h"

#include "gt4r/save/photo_file.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace gt4r::save {

namespace fs = std::filesystem;

namespace {

Bytes read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), {});
}
bool write_file(const fs::path& p, ByteSpan data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return bool(out);
}
void put32(Bytes& d, size_t o, u32 v) {
    for (int i = 0; i < 4; ++i) d[o + size_t(i)] = u8(v >> (8 * i));
}

} // namespace

Bytes encode_play_list(const std::vector<u64>& entries) {
    Bytes d(kPlayListBytes, 0);
    const size_t n = std::min(entries.size(), size_t(kPlayListSize));
    for (size_t i = 0; i < n; ++i) put32(d, i * 8, u32(entries[i])), put32(d, i * 8 + 4, u32(entries[i] >> 32));
    put32(d, 0xC80, u32(n));
    return d;
}

std::optional<std::vector<u64>> decode_play_list(ByteSpan d) {
    if (d.size() < 0xC84 || rd32(d, 0xC80) > u32(kPlayListSize)) return std::nullopt;
    std::vector<u64> out;
    for (u32 i = 0; i < rd32(d, 0xC80); ++i) out.push_back(u64(rd32(d, i * 8)) | u64(rd32(d, i * 8 + 4)) << 32);
    return out;
}

std::optional<u64> folder_entry(const std::string& folder, const std::string& product) {
    if (folder.size() != product.size() + 8) return std::nullopt;
    const std::optional<u64> id = folder_id(folder.substr(product.size() + 1));
    if (!id) return std::nullopt;
    const u8 letter = folder.compare(0, product.size(), product) == 0 ? u8(folder[product.size()]) : 0;
    return *id | u64(letter) << 56;
}

ReplayList::ReplayList(std::string card, GameSave::Reader disc, std::string product)
    : card_(std::move(card)), product_(std::move(product)), disc_(std::move(disc)) {}

bool ReplayList::exists() const {
    std::error_code ec;
    return !card_.empty() && fs::is_regular_file(fs::path(card_) / folder() / folder(), ec);
}

Result ReplayList::load(std::vector<u64>& out) const {
    out.clear();
    if (card_.empty()) return Result::NoCard;
    if (!exists()) return Result::FileNotFound;
    const std::optional<std::vector<u64>> list = decode_play_list(read_file(fs::path(card_) / folder() / folder()));
    if (!list) return Result::InvalidFile;
    out = *list;
    return Result::Ok;
}

Result ReplayList::save(const std::vector<u64>& entries, const std::string& title) const {
    if (card_.empty()) return Result::NoCard;
    const fs::path dir = fs::path(card_) / folder();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return Result::DeviceError;
    if (!write_file(dir / folder(), encode_play_list(entries))) return Result::DeviceError;
    if (disc_ && !fs::exists(dir / "icon0.ico", ec))
        if (const std::optional<Bytes> icon = disc_("icon/GT4_Playlist_replay.ico")) write_file(dir / "icon0.ico", *icon);
    // icon.sys: the disc's, titled "GRAN TURISMO 4" and the second line, its three icons icon0.ico (as the photos').
    if (std::optional<Bytes> sys = disc_ && !fs::exists(dir / "icon.sys", ec) ? disc_("icon/icon.sys") : std::nullopt; sys && sys->size() >= 0x1C4) {
        Bytes& d = *sys;
        Bytes text = full_width_title("GRAN TURISMO 4");
        const Bytes second = full_width_title(title);
        d[6] = u8(text.size()), d[7] = 0;
        text.insert(text.end(), second.begin(), second.end());
        if (text.size() > 0x43) text.resize(0x42);
        std::fill(d.begin() + 0xC0, d.begin() + 0x104, u8(0));
        std::copy(text.begin(), text.end(), d.begin() + 0xC0);
        for (size_t at : {size_t(0x104), size_t(0x144), size_t(0x184)}) {
            std::fill(d.begin() + ptrdiff_t(at), d.begin() + ptrdiff_t(at + 0x40), u8(0));
            std::memcpy(d.data() + at, "icon0.ico", 9);
        }
        d.resize(964);
        write_file(dir / "icon.sys", d);
    }
    return Result::Ok;
}

Result ReplayList::remove() const {
    if (card_.empty()) return Result::NoCard;
    std::error_code ec;
    fs::remove_all(fs::path(card_) / folder(), ec);
    return ec ? Result::DeviceError : Result::Ok;
}

} // namespace gt4r::save
