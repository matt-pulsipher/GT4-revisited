// The update check's question to the relay server and the Discord status's text (docs/specs/net.md "Versions and updates",
// "Discord status"). Only servers on this machine are spoken to.

#include "test_framework.h"

#include "gt4r/net/presence_text.h"
#include "gt4r/net/version_check.h"
#include "gt4r_version.h"
#include "net/relay_server.h"
#include "net/rendezvous.h"
#include "net/session.h"
#include "net/socket.h"
#include "net/update_check.h"
#include "net_ports.h"

#include <chrono>
#include <thread>

using namespace gt4r;
namespace rv = gt3r::net;

namespace {

rv::GameVersion ver(const char* t) { return rv::parse_game_version(t).value(); }

// A relay's answer: the versions, the page, and (a relay that keeps versions per game) the id asked about.
gt3r::Bytes answer(const char* latest, const char* min_online, const std::string& url, const std::string* id) {
    rv::Writer w = rv::rv_begin(rv::Rv::VersionInfo);
    rv::put_version(w, ver(latest)), rv::put_version(w, ver(min_online)), w.str(url);
    if (id) w.str(*id);
    return w.data;
}

// A server on this machine that answers each query with `reply(query)` (nothing when it gives none).
template <class Reply>
std::optional<net::VersionCheck::Result> check_against(Reply reply, int timeout_ms = 3000, gt3r::Bytes* query = nullptr) {
    rv::UdpSocket srv;
    if (!test::on_free_port([&](u16 port) { return srv.open(port); })) return std::nullopt;
    net::VersionCheck check;
    check.start("127.0.0.1:" + std::to_string(srv.local_port()), net::this_version(), rv::RvPlatform::MacOS, timeout_ms);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < end) {
        u8 buf[512];
        rv::Address from;
        if (srv.wait_readable(5))
            while (const size_t n = srv.recv_from(from, buf, sizeof buf)) {
                const gt3r::Bytes q(buf, buf + n);
                if (query) *query = q;
                if (std::optional<gt3r::Bytes> a = reply(q)) srv.send_to(from, *a);
            }
        if (std::optional<net::VersionCheck::Result> r = check.take()) return r;
    }
    return std::nullopt;
}

} // namespace

TEST(updates_version_is_ours) {
    // The version asked about is this game's own (CMakeLists.txt's project), not the shared engine's.
    CHECK(net::this_version().known() && net::this_version().to_string() == GT4R_VERSION);
}

TEST(updates_query_names_the_game) {
    // The query: the engine's VersionQuery with the platform, then the game tag (our id and version), padded so the answer
    // with our id is never larger than it.
    const gt3r::Bytes q = net::version_query(ver("0.1.0"), rv::RvPlatform::Linux);
    CHECK(q.size() == net::kTaggedQuerySize && q.size() > rv::kVersionQuerySize);
    rv::Rv type;
    rv::Reader r(q);
    CHECK(rv::rv_parse(q, type, r) && type == rv::Rv::VersionQuery);
    rv::get_u64(r);
    CHECK(rv::get_version(r).number() == "0.1.0" && r.u8_() == u8(rv::RvPlatform::Linux));
    CHECK(r.str() == net::kGameId && rv::get_version(r).number() == "0.1.0" && r.ok());
    const std::string longest_url = "https://" + std::string(rv::kMaxReleaseUrl - 8, 'a');
    const std::string id = net::kGameId;
    CHECK(answer("0.2.0", "0.1.0", longest_url, &id).size() <= q.size());
    // Answers: ours (our id after the page) are read; a relay's own game's (no id) or another game's are not.
    std::optional<net::VersionAnswer> a = net::read_version_answer(answer("0.2.0", "0.1.0", "https://example.org/r/download/{asset}", &id));
    CHECK(a && a->latest.number() == "0.2.0" && a->min_online.number() == "0.1.0" && a->url == "https://example.org/r/download/{asset}");
    CHECK(!net::read_version_answer(answer("0.9.0", "0.9.0", "https://example.org/other", nullptr)));
    const std::string other = "another";
    CHECK(!net::read_version_answer(answer("0.9.0", "0.9.0", "https://example.org/other", &other)));
    a = net::read_version_answer(answer("0.2.0", "0.0.0", "ftp://example.org", &id)); // only web pages are kept
    CHECK(a && a->url.empty());
}

TEST(updates_release_assets) {
    CHECK(net::release_asset(rv::RvPlatform::MacOS, true) == "gt4r-macos-arm64.zip");
    CHECK(net::release_asset(rv::RvPlatform::Windows, false) == "gt4r-windows-x64.zip");
    CHECK(net::release_asset(rv::RvPlatform::Linux, false) == "gt4r-linux-x86_64.tar.gz");
    CHECK(net::release_asset(rv::RvPlatform::Android, true) == "gt4r-android.apk" && net::release_asset(rv::RvPlatform::Android, false) == "gt4r-android.apk");
    CHECK(net::release_asset(rv::RvPlatform::MacOS, false).empty() && net::release_asset(rv::RvPlatform::Other, true).empty());
    // The page and the download from a relay's URL (the engine's rule).
    const rv::UpdateLinks l = rv::update_links("https://github.com/o/r/releases/latest/download/{asset}", net::release_asset(rv::RvPlatform::MacOS, true));
    CHECK(l.download == "https://github.com/o/r/releases/latest/download/gt4r-macos-arm64.zip" && l.notes == "https://github.com/o/r/releases/latest");
}

TEST(updates_check_round_trip) {
    // A relay that keeps versions per game answers ours.
    const std::string id = net::kGameId;
    gt3r::Bytes asked;
    std::optional<net::VersionCheck::Result> r = check_against([&](const gt3r::Bytes&) { return std::optional(answer("0.2.0", "0.1.0", "https://example.org/r", &id)); }, 3000, &asked);
    if (!r) SKIP("no UDP sockets here");
    CHECK(r->answered && r->answer.latest.number() == "0.2.0" && r->answer.min_online.number() == "0.1.0" && r->answer.url == "https://example.org/r");
    CHECK(asked.size() == net::kTaggedQuerySize);
    // A relay without games answers its own game's versions: not taken (no notice from another game's releases).
    r = check_against([](const gt3r::Bytes&) { return std::optional(answer("9.9.0", "9.9.0", "https://example.org/other", nullptr)); });
    CHECK(r && !r->answered && !r->answer.latest.known() && r->error.find("no versions of this game") != std::string::npos);
    // Nobody answers: it gives up in its time with a reason.
    r = check_against([](const gt3r::Bytes&) { return std::optional<gt3r::Bytes>(); }, 300);
    CHECK(r && !r->answered && r->error.find("no answer") != std::string::npos);
}

TEST(updates_check_against_the_engines_relay) {
    // The engine's own relay server (as the shared engine has it now): whatever it answers, this game's check takes no other
    // game's versions; when it keeps versions per game, it answers ours.
    rv::RelayServer srv;
    if (!test::on_free_port([&](u16 port) { return srv.open(port); })) SKIP("no UDP sockets here");
    rv::RelayServer::Versions v;
    v.latest = v.min_online = ver("9.9.0");
    v.release_url = "https://example.org/main";
    srv.set_versions(v);
    net::VersionCheck check;
    check.start("127.0.0.1:" + std::to_string(srv.port()), net::this_version(), rv::RvPlatform::MacOS, 1500);
    std::optional<net::VersionCheck::Result> r;
    for (int i = 0; i < 1000 && !r; ++i) srv.step(5), r = check.take();
    CHECK(r.has_value());
    if (r && r->answered) CHECK(r->answer.latest.number() != "9.9.0"); // (a relay with games: none listed for ours)
    if (r && !r->answered) CHECK(r->error.find("no versions of this game") != std::string::npos);
}

TEST(updates_presence_text) {
    net::RacePresence r;
    r.sequence = "single_race", r.course = "Tsukuba Circuit", r.car = "Honda CIVIC TYPE R", r.lap = 2, r.laps = 3, r.place = 1, r.cars = 6;
    net::PresenceText t = net::race_presence(r);
    CHECK(t.details == "Arcade race · Tsukuba Circuit" && t.state == "Honda CIVIC TYPE R · Lap 2/3 · 1st of 6");
    r.finished = true, r.place = 2;
    CHECK(net::race_presence(r).state == "Honda CIVIC TYPE R · Finished 2nd");
    r.finished = false, r.sequence = "license", r.cars = 1, r.laps = 1, r.lap = 1;
    t = net::race_presence(r);
    CHECK(t.details == "License test · Tsukuba Circuit" && t.state == "Honda CIVIC TYPE R · Lap 1/1");
    r.replay = true;
    t = net::race_presence(r);
    CHECK(t.details == "Watching a replay · Tsukuba Circuit" && t.state == "Honda CIVIC TYPE R");
    r = {};
    r.online = true, r.players = 3, r.course = "Fuji", r.cars = 3, r.place = 3, r.lap = 1, r.laps = 2, r.car = "A";
    t = net::race_presence(r);
    CHECK(t.details == "Multi-LAN Race · Fuji" && t.state == "A · Lap 1/2 · 3rd of 3" && t.party_size == 3 && t.party_max == 6);
    CHECK(net::race_presence({}).details == "Driving");
    r = {};
    r.car = "Ford Ka `01", r.course = "Tsukuba";
    CHECK(net::race_presence(r).state == "Ford Ka '01");
    // The menus.
    CHECK(net::menu_presence("arcade", "TopRoot").details == "Title menu");
    CHECK(net::menu_presence("arcade", "CourseRoot").details == "Arcade");
    CHECK(net::menu_presence("gtmode", "GTTopRoot").details == "Gran Turismo mode");
    CHECK(net::menu_presence("labomode", "ReplayTheaterRoot").details == "Replay Theater");
    CHECK(net::menu_presence("labomode", "PhotoRoot").details == "Photo Lab");
    CHECK(net::menu_presence("network", "x").details == "Multi-LAN Race");
    CHECK(net::menu_presence("", "").details.empty());
}

namespace {

struct FakeTag {
    std::string id;
    rv::GameVersion version;
};
struct SessionWithTags {
    std::optional<FakeTag> tag;
    rv::GameVersion version;
    void set_game_tag(std::optional<FakeTag> t) { tag = std::move(t); }
    void set_game_version(const rv::GameVersion& v) { version = v; }
};
struct SessionWithout {
    rv::GameVersion version = ver("9.9.9");
    void set_game_version(const rv::GameVersion& v) { version = v; }
};

} // namespace

TEST(updates_session_names_the_game) {
    // With an engine whose session takes a game tag: our id and version, and our version for the direct-IP checks.
    SessionWithTags s;
    CHECK(net::name_the_game(s));
    CHECK(s.tag && s.tag->id == net::kGameId && s.tag->version.to_string() == net::this_version().to_string());
    CHECK(s.version.to_string() == net::this_version().to_string());
    // Without: nothing changes (the requests keep the engine's version, which relays without games check).
    SessionWithout w;
    CHECK(!net::name_the_game(w) && w.version.number() == "9.9.9");
    // The engine's session builds either way.
    rv::Session real;
    (void)net::name_the_game(real);
}

namespace {

// A relay's versions for this game, where the shared engine's relay keeps versions per game (false: it does not).
template <class V>
auto list_game(V& v, const char* latest, const char* min_online, int) -> decltype(v.games, bool()) {
    auto& g = v.games[net::kGameId];
    g.latest = ver(latest), g.min_online = ver(min_online), g.release_url = "https://example.org/ours";
    return true;
}
template <class V>
bool list_game(V&, const char*, const char*, long) {
    return false;
}

} // namespace

TEST(updates_relay_holds_us_to_our_versions) {
    // With an engine whose relay keeps versions per game and whose session takes the game tag: the relay's main game asks for a
    // version no copy of ours has, yet our session is held to our own versions only.
    rv::RelayServer server;
    rv::RelayServer::Versions v;
    v.latest = v.min_online = ver("99.0.0");
    v.release_url = "https://example.org/main";
    if (!list_game(v, net::this_version().number().c_str(), net::this_version().number().c_str(), 0)) SKIP("the shared engine has no game tags yet");
    if (!test::on_free_port([&](u16 port) { return server.open(port); })) SKIP("no UDP sockets here");
    server.set_versions(v);
    const std::string at = "127.0.0.1:" + std::to_string(server.port());
    const auto run = [&](rv::Session& s) {
        for (int t = 0; t < 5000 && s.code().empty() && !s.update_required(); t += 2) server.step(0), s.poll(), std::this_thread::sleep_for(std::chrono::milliseconds(2));
    };
    {
        rv::Session s; // not named: the main game's versions
        CHECK(s.host_via(at, "One", "", 0));
        run(s);
        CHECK(s.update_required().has_value() && s.code().empty());
    }
    {
        rv::Session s; // named: ours
        CHECK(net::name_the_game(s));
        CHECK(s.host_via(at, "One", "", 0));
        run(s);
        CHECK(!s.update_required() && !s.code().empty());
    }
    // Our oldest version raised above this one: refused with our release.
    list_game(v, "99.0.0", "99.0.0", 0);
    server.set_versions(v);
    rv::Session s;
    net::name_the_game(s);
    CHECK(s.host_via(at, "One", "", 0));
    run(s);
    CHECK(s.update_required() && s.update_required()->url == "https://example.org/ours");
}
