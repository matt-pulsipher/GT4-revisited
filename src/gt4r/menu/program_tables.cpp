#include "gt4r/menu/program_tables.h"

#include "disc/core_image.h"

#include <cstring>
#include <vector>

namespace gt4r::menu {

namespace {

// A list in the program: entries of {u32 number, u32 address of a NUL-ended name}.
struct Finder {
    ByteSpan image;
    u32 base;

    u32 word(size_t at) const { return u32(image[at]) | u32(image[at + 1]) << 8 | u32(image[at + 2]) << 16 | u32(image[at + 3]) << 24; }
    // The name an address points to: 1 to 31 printable characters; empty when it is not one.
    std::string name_at(u32 address) const {
        if (address < base || address - base >= image.size()) return {};
        size_t at = address - base;
        std::string s;
        for (; at < image.size() && image[at] != 0; ++at) {
            if (image[at] < 0x20 || image[at] > 0x7E || s.size() >= 31) return {};
            s += char(image[at]);
        }
        return at < image.size() ? s : std::string();
    }
    bool entry(size_t at, int& number, std::string& name) const {
        if (at + 8 > image.size()) return false;
        const u32 n = word(at);
        if (n >= 4096) return false;
        name = name_at(word(at + 4));
        number = int(n);
        return !name.empty();
    }
    // The whole list around one entry that names `anchor`.
    std::map<int, std::string> list_with(const std::string& anchor, const std::string& must_have, const std::string& must_not_have) const {
        std::vector<u8> needle(anchor.begin(), anchor.end());
        needle.push_back(0);
        for (size_t s = 1; s + needle.size() <= image.size(); ++s) {
            if (image[s - 1] != 0 || std::memcmp(image.data() + s, needle.data(), needle.size()) != 0) continue;
            const u32 address = base + u32(s);
            for (size_t p = 4; p + 4 <= image.size(); p += 4) {
                if (word(p) != address) continue;
                size_t first = p - 4;
                int number;
                std::string name;
                if (!entry(first, number, name)) continue;
                while (first >= 8 && entry(first - 8, number, name)) first -= 8;
                std::map<int, std::string> list;
                bool has = must_have.empty(), has_not = false;
                for (size_t at = first; entry(at, number, name); at += 8) {
                    list.emplace(number, name);
                    has = has || name == must_have;
                    has_not = has_not || (!must_not_have.empty() && name == must_not_have);
                }
                if (list.size() >= 4 && has && !has_not) return list;
            }
        }
        return {};
    }
};

} // namespace

ProgramTables ProgramTables::find(ByteSpan image, u32 load_address) {
    const Finder f{image, load_address};
    ProgramTables t;
    // Both the makers' and the tuners' lists name the car makers; only the tuners' has the
    // tuning shops.
    t.tuners = f.list_with("renault", "hks", {});
    t.makers = f.list_with("renault", "polyphony", "hks");
    t.countries = f.list_with("JP", "US", {});
    for (auto it = t.countries.begin(); it != t.countries.end();) it = it->second.size() == 2 ? std::next(it) : t.countries.erase(it);
    t.categories = f.list_with("CONCEPT", "TUNING", {}); // another list has NORMAL and RACING too (with DIRT and SNOW)
    return t;
}

ProgramTables ProgramTables::from_core(ByteSpan core_file) {
    try {
        const gt3r::CoreImage core = gt3r::CoreImage::decode(core_file);
        return find(core.program, core.load_address);
    } catch (const std::exception&) {
        return {};
    }
}

} // namespace gt4r::menu
