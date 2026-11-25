#include "gt4r/net/identity.h"

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
    return text;
}

} // namespace gt4r::net
