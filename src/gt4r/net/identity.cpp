#include "gt4r/net/identity.h"

#include <algorithm>
#include <cctype>

namespace gt4r::net {

std::optional<gt3r::net::OnlineDisc> online_disc(const DiscInfo& info) {
    if (info.game != Game::GT4) return std::nullopt;
    gt3r::net::OnlineDisc d;
    if (info.verified && !info.modified) d.id = "gt4", d.name = "another game";
    else d.id = "gt4:" + info.digest, d.name = "a copy of another game";
    return d;
}

std::string refusal_text(const std::string& text, bool update_required) {
    if (update_required) return "This copy of the game is out of date: it must be updated to play online.";
    if (text.find("Please update") != std::string::npos) return "The host has a newer version of the game: this copy must be updated.";
    if (text.find("another version of a") != std::string::npos) return "The host plays another copy of the disc: both must be the same.";
    // The engine's texts as the pages write theirs.
    std::string t = text;
    for (size_t at = 0; (at = t.find("session", at)) != std::string::npos; at += 5) t.replace(at, 7, "lobby");
    if (!t.empty()) t[0] = char(std::toupper(static_cast<unsigned char>(t[0])));
    if (!t.empty() && t.back() != '.') t += '.';
    return t;
}

std::string online_name(const std::optional<std::string>& configured) {
    if (!configured) return kDefaultOnlineName;
    const std::string& t = *configured;
    size_t from = 0, to = t.size();
    while (from < to && std::isspace(static_cast<unsigned char>(t[from]))) ++from;
    while (to > from && std::isspace(static_cast<unsigned char>(t[to - 1]))) --to;
    size_t n = std::min(to - from, kOnlineNameBytes);
    // (Not through a letter of several bytes: a cut before one of its continuation bytes drops the letter.)
    if (n < to - from)
        while (n > 0 && (static_cast<unsigned char>(t[from + n]) & 0xC0) == 0x80) --n;
    const std::string name = t.substr(from, n);
    return name.empty() ? std::string(kDefaultOnlineName) : name;
}

} // namespace gt4r::net
