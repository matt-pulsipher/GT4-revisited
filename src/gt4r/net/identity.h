#pragma once

// The disc a session is played with, as the other machines see it (docs/specs/net.md "The session's identity").

#include "gt4r/disc/disc.h"
#include "net/protocol.h"

#include <optional>
#include <string>

namespace gt4r::net {

// "gt4" for the release as it shipped, "gt4:<the files' digest>" for any other Gran Turismo 4 disc (a modified copy, an
// unknown release: only the same files meet). Nothing for Prologue: it has no online races here. The name is what the
// refusal messages of both sides call it.
std::optional<gt3r::net::OnlineDisc> online_disc(const DiscInfo& info);

// What the player reads of a session's refusal: the engine's text, but for a copy the relay server calls too old (which
// names no download) and texts that name another game.
std::string refusal_text(const std::string& text, bool update_required);

// The name the others see: the player's own, `config.ini` [online] name (its spaces trimmed, at most 16 bytes), never the
// Gran Turismo mode player's. None chosen: a new game's LAN machine name, "Player1".
inline constexpr const char* kDefaultOnlineName = "Player1";
inline constexpr size_t kOnlineNameBytes = 16;
std::string online_name(const std::optional<std::string>& configured);

} // namespace gt4r::net
