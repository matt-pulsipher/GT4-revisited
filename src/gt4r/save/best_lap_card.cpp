#include "gt4r/save/best_lap_card.h"

#include "gt4r/save/codec.h"
#include "gt4r/save/photo_file.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>

namespace gt4r::save {

namespace fs = std::filesystem;

namespace {

u32 chance() {
    static std::mt19937 random(u32(std::chrono::steady_clock::now().time_since_epoch().count()) ^ std::random_device{}());
    return random();
}

bool write_file(const fs::path& p, ByteSpan data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return bool(out);
}

} // namespace

bool is_best_lap_folder(const std::string& folder) {
    // (BASCUS-97328B..., BESCES-51719B..., BISCPS-17001B...: two letters, the product's ten, then the kind)
    return folder.size() == 20 && folder[12] == 'B';
}

Result write_best_lap(const std::string& card, const std::string& product, const GameSave::Reader& disc, ByteSpan data, const ReplayParticulars& p,
                      const std::string& icon, const std::string& title, const std::string& over, std::string* folder_out) {
    if (card.empty()) return Result::NoCard;
    if (data.empty()) return Result::InvalidFile;
    std::error_code ec;
    if (!over.empty() && over.find_first_of("/\\") == std::string::npos && is_best_lap_folder(over)) fs::remove_all(fs::path(card) / over, ec);
    std::string folder;
    for (int tries = 0; tries < 64 && (folder.empty() || fs::exists(fs::path(card) / folder, ec)); ++tries)
        folder = product + "B" + folder_letters(u64(chance()) << 10 ^ chance());
    const fs::path dir = fs::path(card) / folder;
    fs::create_directories(dir, ec);
    if (ec) return Result::DeviceError;
    if (!write_file(dir / folder, scramble(data, 0, chance(), FileKind::Replay))) return Result::DeviceError;
    if (!write_file(dir / "thumbnail", encode_particulars(p, chance()))) return Result::DeviceError;
    if (std::optional<Bytes> ico = disc ? disc(icon) : std::nullopt) write_file(dir / "icon0.ico", *ico);
    // icon.sys: the disc's, titled "GRAN TURISMO 4" and the second line, its three icons icon0.ico (as the photos').
    if (std::optional<Bytes> sys = disc ? disc("icon/icon.sys") : std::nullopt; sys && sys->size() >= 0x1C4) {
        Bytes& d = *sys;
        Bytes text = full_width_title("GRAN TURISMO 4");
        const Bytes second = full_width_title(title);
        d[6] = u8(text.size()), d[7] = 0;
        text.insert(text.end(), second.begin(), second.end());
        if (text.size() > 0x43) text.resize(0x42);
        std::fill(d.begin() + 0xC0, d.begin() + 0x104, u8(0));
        std::copy(text.begin(), text.end(), d.begin() + 0xC0);
        for (size_t at : {size_t(0x104), size_t(0x144), size_t(0x184)}) {
            std::fill(d.begin() + std::ptrdiff_t(at), d.begin() + std::ptrdiff_t(at + 0x40), u8(0));
            std::memcpy(d.data() + at, "icon0.ico", 9);
        }
        d.resize(964);
        write_file(dir / "icon.sys", d);
    }
    if (folder_out) *folder_out = folder;
    return Result::Ok;
}

} // namespace gt4r::save
