#include "gt4r/menu/text_table.h"

#include <algorithm>

namespace gt4r::menu {

namespace {

constexpr u32 kMaxPages = 0x4000, kMaxPairs = 0x40000;

std::string text_at(ByteSpan d, u32 at) {
    if (at >= d.size()) throw FormatError("RT03: text outside the file");
    const auto end = std::find(d.begin() + at, d.end(), u8(0));
    return std::string(d.begin() + at, end);
}

} // namespace

// "RT03", 8 zero bytes, u32 pages ("RT02": the count right after the magic); a page each:
// {u32 name, u32 pairs, u32 pair table, 4 filler bytes}; a pair: {u32 key, u32 text}. Every
// text is NUL-ended UTF-8; pages and keys are in byte order.
TextTable TextTable::parse(ByteSpan d) {
    if (!is_table(d)) throw FormatError("not an RT03 text table");
    const size_t head = d[3] == '3' ? 0x10 : 8;
    if (d.size() < head) throw FormatError("RT03: short file");
    const u32 n = rd32(d, head - 4);
    if (n > kMaxPages || head + size_t(n) * 16 > d.size()) throw FormatError("RT03: bad page count");
    TextTable t;
    for (u32 i = 0; i < n; ++i) {
        const size_t at = head + size_t(i) * 16;
        const u32 pairs = rd32(d, at + 4), table = rd32(d, at + 8);
        if (pairs > kMaxPairs || table > d.size() || size_t(pairs) * 8 > d.size() - table) throw FormatError("RT03: bad page");
        auto& page = t.pages_[text_at(d, rd32(d, at))];
        for (u32 j = 0; j < pairs; ++j) page[text_at(d, rd32(d, table + j * 8))] = text_at(d, rd32(d, table + j * 8 + 4));
    }
    return t;
}

const std::string* TextTable::find(std::string_view page, std::string_view key) const {
    const auto p = pages_.find(page);
    if (p == pages_.end()) return nullptr;
    const auto k = p->second.find(key);
    return k == p->second.end() ? nullptr : &k->second;
}

std::vector<std::string> TextTable::pages() const {
    std::vector<std::string> v;
    for (const auto& [name, page] : pages_) v.push_back(name);
    return v;
}

const std::map<std::string, std::string, std::less<>>* TextTable::page(std::string_view name) const {
    const auto p = pages_.find(name);
    return p == pages_.end() ? nullptr : &p->second;
}

size_t TextTable::size() const {
    size_t n = 0;
    for (const auto& [name, page] : pages_) n += page.size();
    return n;
}

void TextTable::merge(const TextTable& other) {
    for (const auto& [name, page] : other.pages_)
        for (const auto& [key, text] : page) pages_[name][key] = text;
}

} // namespace gt4r::menu
