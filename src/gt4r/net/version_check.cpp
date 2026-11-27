#include "gt4r/net/version_check.h"

#include "gt4r_version.h"

#include <utility>

namespace gt4r::net {

namespace rv = gt3r::net;

const GameVersion& this_version() {
    static const GameVersion v = rv::parse_game_version(GT4R_VERSION).value_or(GameVersion{});
    return v;
}

rv::GameTag game_tag() {
    rv::GameTag t;
    t.id = kGameId, t.version = this_version();
    return t;
}

void name_the_game(rv::Session& session) {
    session.set_game_tag(game_tag());
    session.set_game_version(this_version());
}

std::string release_asset(RvPlatform platform, bool arm64, bool vr) {
    switch (platform) {
    case RvPlatform::Windows:
        if (arm64) return "";
        return vr ? "gt4r-windows-x64-vr.zip" : "gt4r-windows-x64.zip";
    case RvPlatform::MacOS: return arm64 && !vr ? "gt4r-macos-arm64.zip" : "";
    case RvPlatform::Linux: return arm64 || vr ? "" : "gt4r-linux-x86_64.tar.gz";
    case RvPlatform::Android: // one phone APK for arm64-v8a and x86_64; the Quest APK is arm64 only
        if (vr) return arm64 ? "gt4r-quest.apk" : "";
        return "gt4r-android.apk";
    default: return "";
    }
}

std::string this_release_asset() {
#if GT3R_XR
    constexpr bool vr = true; // the VR builds' own packages: PC VR, Meta Quest
#else
    constexpr bool vr = false;
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    constexpr bool arm64 = true;
#elif defined(__x86_64__) || defined(_M_X64)
    constexpr bool arm64 = false;
#else
    return "";
#endif
    return release_asset(rv::this_platform(), arm64, vr);
}

void VersionCheck::start(const std::string& server, const GameVersion& ours, RvPlatform platform, int timeout_ms) {
    rv::GameTag tag = game_tag();
    tag.version = ours;
    check_.start(server, ours, platform, timeout_ms, std::move(tag));
}

std::optional<VersionCheck::Result> VersionCheck::take() {
    std::optional<rv::VersionCheck::Result> r = check_.take();
    if (!r) return std::nullopt;
    Result out;
    out.answered = r->answered, out.error = std::move(r->error);
    out.answer.latest = r->latest, out.answer.min_online = r->min_online, out.answer.url = std::move(r->url);
    return out;
}

} // namespace gt4r::net
