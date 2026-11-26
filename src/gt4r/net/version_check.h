#pragma once

// The update check's question to the relay server (docs/specs/net.md "Versions and updates"): which versions of this game it
// announces. The relay is shared with another game on the same engine, so the question names ours by its game tag (`kGameId`,
// after the query's own fields: the engine's docs/dev/ONLINE.md "Other games on one relay"). A relay that keeps versions per
// game answers ours with our id after the page; one that does not answers its own game's without it, and that answer is not
// taken (the check then knows nothing, as when nobody answers). One exchange on a background thread: up to three queries a
// second apart, then it gives up; nothing besides the query is sent.

#include "net/rendezvous.h"

#include <memory>
#include <optional>
#include <string>
#include <type_traits>

namespace gt4r::net {

using gt3r::net::GameVersion;
using gt3r::net::RvPlatform;

// The game's id on a relay server, and the size its version query is padded to (its answer, with the id, is smaller).
inline constexpr const char* kGameId = "gt4r";
inline constexpr size_t kGameIdMax = 32;
inline constexpr size_t kTaggedQuerySize = gt3r::net::kVersionQuerySize + 1 + kGameIdMax + 6;

// This build's version (CMakeLists.txt's project VERSION: gt4r_version.h); not the engine's.
const GameVersion& this_version();

// The query (header, cookie 0, version, platform, the game tag; padded) and an answer read: nullopt when it is not a version
// answer for `id` (another game's, a relay's without games, a malformed one).
gt3r::Bytes version_query(const GameVersion& ours, RvPlatform platform, const std::string& id = kGameId);
struct VersionAnswer {
    GameVersion latest, min_online; // 0.0.0: none
    std::string url;                // the release page ("" none or not a web page)
};
std::optional<VersionAnswer> read_version_answer(gt3r::ByteSpan datagram, const std::string& id = kGameId);

// The package a release has for this platform under a stable name ("" none: the release page instead).
std::string release_asset(RvPlatform platform, bool arm64);
std::string this_release_asset();

// The online session names this game to relay servers: its requests carry the game tag (our id and version), so a relay that
// keeps versions per game holds them to ours, and players' direct-IP checks compare this game's versions. Needs the shared
// engine's Session::set_game_tag (its docs/dev/ONLINE.md "Other games on one relay"); with an engine without it, nothing
// changes (the requests carry the engine's version alone, as before). True when the session was named.
namespace detail {
template <class C, class A>
A tag_arg(void (C::*)(A));
template <class S>
auto name_session(S& s, int) -> decltype(tag_arg(&S::set_game_tag), true) {
    using Tag = typename std::decay_t<decltype(tag_arg(&S::set_game_tag))>::value_type;
    Tag tag;
    tag.id = kGameId, tag.version = this_version();
    s.set_game_tag(tag);
    s.set_game_version(this_version());
    return true;
}
template <class S>
bool name_session(S&, long) {
    return false;
}
} // namespace detail
template <class S>
bool name_the_game(S& session) {
    return detail::name_session(session, 0);
}

class VersionCheck {
public:
    struct Result {
        bool answered = false;
        VersionAnswer answer;
        std::string error; // !answered: why
    };
    VersionCheck() = default;
    ~VersionCheck(); // does not wait
    VersionCheck(const VersionCheck&) = delete;
    VersionCheck& operator=(const VersionCheck&) = delete;

    void start(const std::string& server, const GameVersion& ours, RvPlatform platform, int timeout_ms = 3000);
    bool running() const;
    std::optional<Result> take(); // once

private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace gt4r::net
