#pragma once

// The mods' order: a mod's place is its mod.ini's `priority` (the shared engine's rule, vfs/vfs.h: the higher one's files win, the
// folder's name between equal ones). Moving a mod on the Mods page writes that line and nothing else of the file.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gt4r::settings {

// The text of a mod.ini with `priority` in its [mod] section: the line replaced or added, every other line as it was.
std::string with_priority(std::string_view ini, int priority);

struct ModPlace {
    std::string id;
    int priority = 0;
};
// The mods in the order the Mods page lists them (the one that wins first).
void sort_places(std::vector<ModPlace>& mods);
// `at` moved one place up (-1) or down (1) in that order: the priorities to write (id and value), none when it is at the end
// already. As few mods as the numbers allow are changed.
std::vector<ModPlace> move_mod(std::vector<ModPlace> mods, size_t at, int direction);

} // namespace gt4r::settings
