#pragma once

// The menus' text (.rt2, "RT03"; Prologue's "RT02"): pages of key and text pairs, a page per
// menu page ("BootProject::LanguageRoot") or subject ("CourseName"); docs/formats/menu.md.

#include "gt4r/base/types.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace gt4r::menu {

class TextTable {
public:
    static bool is_table(ByteSpan d) { return d.size() >= 8 && (gt3r::has_magic(d, "RT03") || gt3r::has_magic(d, "RT02")); }
    // Throws FormatError.
    static TextTable parse(ByteSpan file);

    // A page's text for a key (UTF-8); null when the page or the key is not there.
    const std::string* find(std::string_view page, std::string_view key) const;
    std::vector<std::string> pages() const;
    // A page's pairs in key order; null when it is not there.
    const std::map<std::string, std::string, std::less<>>* page(std::string_view name) const;
    size_t size() const; // pairs in all pages
    // Another text for a key of a page there is.
    void reword(std::string_view page, std::string_view key, const std::string& text) {
        if (const auto p = pages_.find(page); p != pages_.end()) p->second[std::string(key)] = text;
    }
    // Another table's pages added to this one's (a project's own text over the common one).
    void merge(const TextTable& other);

private:
    std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>> pages_;
};

} // namespace gt4r::menu
