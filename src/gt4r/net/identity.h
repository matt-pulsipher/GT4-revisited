#pragma once

// The disc a session is played with, as the other machines see it (docs/specs/net.md "The session's identity").

#include "gt4r/disc/disc.h"
#include "net/protocol.h"

#include <optional>

namespace gt4r::net {

// "gt4" for the release as it shipped, "gt4:<the files' digest>" for any other Gran Turismo 4 disc (a modified copy, an
// unknown release: only the same files meet). Nothing for Prologue: it has no online races here. The name is what the
// refusal messages of both sides call it.
std::optional<gt3r::net::OnlineDisc> online_disc(const DiscInfo& info);

// What the player reads of a session's refusal: the engine's text, but for a copy the relay server calls too old (which
// names no download) and texts that name another game.
std::string refusal_text(const std::string& text, bool update_required);

} // namespace gt4r::net
