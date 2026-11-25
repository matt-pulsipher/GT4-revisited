#include "gt4r/settings/mod_files.h"

#include "base/config.h"

#include <algorithm>

namespace gt4r::settings {

std::string with_priority(std::string_view ini, int priority) {
    const std::string line = "priority = " + std::to_string(priority);
    const std::string eol = ini.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    std::vector<std::string> lines;
    for (std::string_view l : gt3r::split(ini, '\n')) {
        if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
        lines.emplace_back(l);
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back(); // (the last line's own end)
    bool in_mod = false;
    long header = -1, last = -1, found = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string_view t = gt3r::trim(lines[i]);
        // (a byte order mark before the first section)
        if (i == 0 && t.substr(0, 3) == "\xEF\xBB\xBF") t = gt3r::trim(t.substr(3));
        if (!t.empty() && t.front() == '[') {
            const size_t end = t.find(']');
            in_mod = end != std::string_view::npos && gt3r::to_lower(gt3r::trim(t.substr(1, end - 1))) == "mod";
            if (in_mod && header < 0) header = long(i), last = long(i);
            continue;
        }
        if (!in_mod || t.empty() || t.front() == '#' || t.front() == ';') continue;
        last = long(i);
        const size_t eq = t.find('=');
        if (eq != std::string_view::npos && gt3r::trim(t.substr(0, eq)) == "priority") {
            found = long(i);
            break; // (the first is the one that is read)
        }
    }
    if (found >= 0) lines[size_t(found)] = line;
    else if (header >= 0) lines.insert(lines.begin() + last + 1, line);
    else lines.insert(lines.begin(), {"[mod]", line});
    std::string out;
    for (const std::string& l : lines) out += l + eol;
    return out;
}

void sort_places(std::vector<ModPlace>& mods) {
    std::sort(mods.begin(), mods.end(), [](const ModPlace& a, const ModPlace& b) { return a.priority != b.priority ? a.priority > b.priority : a.id > b.id; });
}

std::vector<ModPlace> move_mod(std::vector<ModPlace> mods, size_t at, int direction) {
    sort_places(mods);
    if (at >= mods.size() || (direction != -1 && direction != 1)) return {};
    const long to = long(at) + direction;
    if (to < 0 || to >= long(mods.size())) return {};
    const std::vector<ModPlace> was = mods;
    std::swap(mods[at], mods[size_t(to)]);
    // From the bottom of the list up: each mod above the one below it, by its own number where that does (equal numbers: by id).
    for (size_t i = mods.size() - 1; i-- > 0;) {
        const ModPlace& below = mods[i + 1];
        ModPlace& m = mods[i];
        if (m.priority > below.priority || (m.priority == below.priority && m.id > below.id)) continue;
        m.priority = m.id > below.id ? below.priority : below.priority + 1;
    }
    std::vector<ModPlace> out;
    for (const ModPlace& m : mods)
        for (const ModPlace& w : was)
            if (w.id == m.id && w.priority != m.priority) out.push_back(m);
    return out;
}

} // namespace gt4r::settings
