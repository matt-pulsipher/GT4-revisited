#pragma once

// A menu page's files (.gpb, "gpb0" / "gpb1" / "gpb2"): the pictures its widgets name
// ("image/boot/lang_bg_4.png" is a texture set here), and models and layouts the same way;
// docs/formats/menu.md.

#include "gt4r/base/types.h"

#include <string>
#include <string_view>
#include <vector>

namespace gt4r::menu {

class ImagePack {
public:
    struct Entry {
        std::string name; // "image/boot/lang_bg_4.png"
        u32 offset = 0;
        u32 size = 0; // stored bytes
    };

    static bool is_pack(ByteSpan d);
    // Throws FormatError. The pack keeps reading from `file`, which must outlive it.
    static ImagePack parse(ByteSpan file);

    int version() const { return version_; } // 0, 1 or 2
    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(std::string_view name) const;
    // An entry's file, unpacked when it is stored packed.
    Bytes read(const Entry& e) const;

private:
    ByteSpan file_;
    int version_ = 0;
    std::vector<Entry> entries_; // sorted by name, as the file has them
};

} // namespace gt4r::menu
