#pragma once

// The update check's question to the relay server (docs/specs/net.md "Versions and updates"): which versions of this game it
// announces. The relay is shared with another game on the same engine, so the question names ours by its game tag (`kGameId`,
// after the query's own fields: the engine's docs/dev/ONLINE.md "Other games on one relay"). A relay that keeps versions per
// game answers ours with our id after the page; one that does not answers its own game's without it, and that answer is not
// taken (the check then knows nothing, as when nobody answers). The engine's check (net/update_check.h): one exchange on a
// background thread, up to three queries a second apart, then it gives up; nothing besides the query is sent.

#include "net/rendezvous.h"
#include "net/session.h"
#include "net/update_check.h"

#include <optional>
#include <string>

namespace gt4r::net {

using gt3r::net::GameVersion;
using gt3r::net::RvPlatform;

// The game's id on a relay server (its game tag's).
inline constexpr const char* kGameId = "gt4r";

// This build's version (CMakeLists.txt's project VERSION: gt4r_version.h); not the engine's.
const GameVersion& this_version();
// The game tag our requests carry: our id and version.
gt3r::net::GameTag game_tag();

struct VersionAnswer {
    GameVersion latest, min_online; // 0.0.0: none
    std::string url;                // the release page ("" none or not a web page)
};

// The package a release has for this platform under a stable name ("" none: the release page instead); vr: the VR builds'
// own (GT3R_XR: the PC VR package, the Meta Quest APK).
std::string release_asset(RvPlatform platform, bool arm64, bool vr = false);
std::string this_release_asset();

// The online session names this game to relay servers: its requests carry the game tag (our id and version), so a relay that
// keeps versions per game holds them to ours, and players' direct-IP checks compare this game's versions (the engine's
// Session::set_game_tag, its docs/dev/ONLINE.md "Other games on one relay").
void name_the_game(gt3r::net::Session& session);

// The engine's version check asked with our game tag.
class VersionCheck {
public:
    struct Result {
        bool answered = false;
        VersionAnswer answer;
        std::string error; // !answered: why
    };
    void start(const std::string& server, const GameVersion& ours, RvPlatform platform, int timeout_ms = 3000);
    bool running() const { return check_.running(); }
    std::optional<Result> take(); // once

private:
    gt3r::net::VersionCheck check_; // (its destructor does not wait)
};

} // namespace gt4r::net
