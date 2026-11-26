#include "gt4r/net/version_check.h"

#include "gt4r_version.h"
#include "net/session.h"
#include "net/socket.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace gt4r::net {

namespace rv = gt3r::net;

const GameVersion& this_version() {
    static const GameVersion v = rv::parse_game_version(GT4R_VERSION).value_or(GameVersion{});
    return v;
}

gt3r::Bytes version_query(const GameVersion& ours, RvPlatform platform, const std::string& id) {
    rv::Writer q = rv::rv_request(rv::Rv::VersionQuery, 0, ours);
    q.u8_(gt3r::u8(platform));
    q.str(id); // the game tag: id, version
    rv::put_version(q, ours);
    rv::rv_pad(q, kTaggedQuerySize);
    return q.data;
}

std::optional<VersionAnswer> read_version_answer(gt3r::ByteSpan d, const std::string& id) {
    rv::Rv type;
    rv::Reader r(d);
    if (rv::rv_version(d) != rv::kRendezvousVersion || !rv::rv_parse(d, type, r) || type != rv::Rv::VersionInfo) return std::nullopt;
    VersionAnswer a;
    a.latest = rv::get_version(r);
    a.min_online = rv::get_version(r);
    a.url = r.str();
    if (!r.ok() || r.remaining() == 0) return std::nullopt; // (no id: the relay's own game's versions)
    if (r.str() != id || !r.ok()) return std::nullopt;
    if (a.url.size() > rv::kMaxReleaseUrl || (a.url.rfind("https://", 0) != 0 && a.url.rfind("http://", 0) != 0)) a.url.clear(); // only web pages are opened
    return a;
}

std::string release_asset(RvPlatform platform, bool arm64) {
    switch (platform) {
    case RvPlatform::Windows: return arm64 ? "" : "gt4r-windows-x64.zip";
    case RvPlatform::MacOS: return arm64 ? "gt4r-macos-arm64.zip" : "";
    case RvPlatform::Linux: return arm64 ? "" : "gt4r-linux-x86_64.tar.gz";
    default: return "";
    }
}

std::string this_release_asset() {
#if defined(__aarch64__) || defined(_M_ARM64)
    constexpr bool arm64 = true;
#elif defined(__x86_64__) || defined(_M_X64)
    constexpr bool arm64 = false;
#else
    return "";
#endif
    return release_asset(rv::this_platform(), arm64);
}

struct VersionCheck::State {
    std::mutex mu;
    bool done = false, taken = false;
    Result result;
    std::atomic<bool> cancel{false};
};

namespace {

VersionCheck::Result run(const std::string& server, const GameVersion& ours, RvPlatform platform, int timeout_ms, const std::atomic<bool>& cancel) {
    using Clock = std::chrono::steady_clock;
    VersionCheck::Result res;
    rv::Address to;
    std::string err;
    if (!rv::resolve(server, rv::kDefaultRelayPort, to, &err)) {
        res.error = err.empty() ? "cannot find " + server : err;
        return res;
    }
    rv::UdpSocket sock;
    if (!sock.open_for(to, &err)) {
        res.error = err;
        return res;
    }
    const gt3r::Bytes q = version_query(ours, platform);
    const auto start = Clock::now(), end = start + std::chrono::milliseconds(timeout_ms);
    const auto every = std::chrono::milliseconds(std::max(1, timeout_ms / 3));
    auto next = start;
    while (!cancel) {
        const auto now = Clock::now();
        if (now >= end) break;
        if (now >= next) sock.send_to(to, q), next = now + every;
        const int wait = int(std::chrono::duration_cast<std::chrono::milliseconds>(std::min(next, end) - now).count());
        if (!sock.wait_readable(std::clamp(wait, 1, 100))) continue;
        gt3r::u8 buf[512];
        rv::Address from;
        while (const size_t n = sock.recv_from(from, buf, sizeof buf)) {
            if (from != to) continue;
            const gt3r::ByteSpan d(buf, n);
            rv::Rv type;
            rv::Reader r(d);
            if (rv::rv_version(d) != rv::kRendezvousVersion || !rv::rv_parse(d, type, r) || type != rv::Rv::VersionInfo) continue;
            if (std::optional<VersionAnswer> a = read_version_answer(d)) {
                res.answered = true, res.answer = *a;
                return res;
            }
            res.error = server + " announces no versions of this game"; // (a relay's own game's: not ours)
            return res;
        }
    }
    res.error = "no answer from " + server;
    return res;
}

} // namespace

VersionCheck::~VersionCheck() {
    if (state_) state_->cancel = true;
}

void VersionCheck::start(const std::string& server, const GameVersion& ours, RvPlatform platform, int timeout_ms) {
    if (state_) state_->cancel = true;
    auto st = std::make_shared<State>();
    state_ = st;
    // Detached: a slow name lookup never holds up the game or its exit; the thread touches only its own state.
    std::thread([st, server, ours, platform, timeout_ms] {
        Result r = run(server, ours, platform, timeout_ms, st->cancel);
        std::lock_guard<std::mutex> lock(st->mu);
        st->result = std::move(r);
        st->done = true;
    }).detach();
}

bool VersionCheck::running() const {
    if (!state_) return false;
    std::lock_guard<std::mutex> lock(state_->mu);
    return !state_->done;
}

std::optional<VersionCheck::Result> VersionCheck::take() {
    if (!state_) return std::nullopt;
    std::lock_guard<std::mutex> lock(state_->mu);
    if (!state_->done || state_->taken) return std::nullopt;
    state_->taken = true;
    return state_->result;
}

} // namespace gt4r::net
