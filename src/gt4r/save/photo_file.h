#pragma once

// The photos on a memory card, as the game writes them (GT4MC::FileGT4PictureData): a folder each,
// BASCUS-97328P and seven letters, with the picture (a JPEG file, scrambled), a record of what it
// shows (`thumbnail`), icon.sys and icon0.ico with the photo's own small picture.
// docs/formats/photo.md "On the card".

#include "gt4r/save/memory_card.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::save {

// The record beside a photo or a replay (0x389130, 0xE8 bytes).
struct PhotoInfo {
    u64 car = 0, course = 0; // database codes
    std::string name;        // "P0000"
    std::string course_file; // "ss_times"
    std::string car_name;    // "Ford GT `05": the short name
    bool portrait = false;
    Bytes encode() const;                           // 0xE8 bytes
    static std::optional<PhotoInfo> decode(ByteSpan);
};
inline constexpr size_t kInfoSize = 0xE8, kInfoFile = 0x100;

// The seven letters of a photo's folder: 42 bits, six a letter from the lowest (0x1CBE60): the
// photo's number from 32 on, chance below with bit 0 for a portrait.
std::string folder_letters(u64 id);
std::optional<u64> folder_id(const std::string& letters);

// icon0.ico made from the disc's icon with a 64 x 64 picture (16-bit pixels) in its texture's top
// left (0x1D3E20); empty when the template does not read.
Bytes icon_with_picture(ByteSpan icon, const std::vector<u16>& pixels);

struct PhotoEntry {
    std::string folder; // "BASCUS-97328Pv-clNzY"
    char kind = 'P';    // 'P' a photo, 'F' a film
    int number = 0;
    bool portrait = false;
    int kb = 0;
    std::optional<PhotoInfo> info; // none: the record does not read
    std::array<int, 6> date{};     // when it was written: year, month, day, hour, minute, second (local)
};

class PhotoCard {
public:
    using Reader = GameSave::Reader;
    // `card`: the card's folder ("": none). `disc`: the disc's files (the icons).
    PhotoCard(std::string card, Reader disc, std::string product = "BASCUS-97328");
    bool connected() const { return !card_.empty(); }

    // The photos ('P') or films ('F') on the card, by number.
    std::vector<PhotoEntry> list(char kind) const;
    // A photo's JPEG file.
    std::optional<Bytes> picture(const std::string& folder) const;
    // Its icon's small picture, 64 x 64.
    std::optional<std::vector<u32>> icon(const std::string& folder) const;
    // The KB a photo of this JPEG needs on the card (0x1CC9A8).
    int needed_kb(ByteSpan jpeg, const std::vector<u16>& small, bool portrait) const;
    // A new photo; `title`: icon.sys's second line ("Photo data").
    Result save(const PhotoInfo& info, ByteSpan jpeg, const std::vector<u16>& small, const std::string& title);
    Result remove(const std::string& folder);

private:
    std::string card_, product_;
    Reader disc_;
    Bytes make_icon(const std::vector<u16>& small, bool portrait) const;
};

// ASCII as the full-width Shift-JIS letters of a save's title; [ ] and digits too (0x1CBD18).
Bytes full_width_title(const std::string& s);

} // namespace gt4r::save
