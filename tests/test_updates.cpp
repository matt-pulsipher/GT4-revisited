// The update check's question to the relay server and the Discord status's text (docs/specs/net.md "Versions and updates",
// "Discord status"). Only servers on this machine are spoken to.

#include "test_framework.h"

#include "gt4r/net/identity.h"
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

namespace {

// A relay's versions for this game.
void list_game(rv::RelayServer::Versions& v, const char* latest, const char* min_online) {
    rv::RelayServer::GameVersions& g = v.games[net::kGameId];
    g.latest = ver(latest), g.min_online = ver(min_online), g.release_url = "https://example.org/ours";
}

} // namespace

TEST(updates_version_is_ours) {
    // The version asked about is this game's own (CMakeLists.txt's project), not the shared engine's.
    CHECK(net::this_version().known() && net::this_version().to_string() == GT4R_VERSION);
}

TEST(updates_query_names_the_game) {
    // The query: the engine's VersionQuery with the platform, then the game tag (our id and version), padded so the answer
    // with our id is never larger than it.
    const std::string id = net::kGameId;
    gt3r::Bytes q;
    std::optional<net::VersionCheck::Result> r = check_against([&](const gt3r::Bytes&) { return std::optional(answer("0.2.0", "0.1.0", "https://example.org/r/download/{asset}", &id)); }, 3000, &q);
    if (!r) SKIP("no UDP sockets here");
    CHECK(q.size() == rv::kTaggedVersionQuerySize && q.size() > rv::kVersionQuerySize);
    rv::Rv type;
    rv::Reader rd(q);
    CHECK(rv::rv_parse(q, type, rd) && type == rv::Rv::VersionQuery);
    rv::get_u64(rd);
    CHECK(rv::get_version(rd).number() == net::this_version().number() && rd.u8_() == u8(rv::RvPlatform::MacOS));
    CHECK(rd.str() == net::kGameId && rv::get_version(rd).number() == net::this_version().number() && rd.ok());
    const std::string longest_url = "https://" + std::string(rv::kMaxReleaseUrl - 8, 'a');
    CHECK(answer("0.2.0", "0.1.0", longest_url, &id).size() <= q.size());
    // Answers: ours (our id after the page) are read; a relay's own game's (no id) or another game's are not.
    CHECK(r->answered && r->answer.latest.number() == "0.2.0" && r->answer.min_online.number() == "0.1.0" && r->answer.url == "https://example.org/r/download/{asset}");
    r = check_against([](const gt3r::Bytes&) { return std::optional(answer("0.9.0", "0.9.0", "https://example.org/other", nullptr)); });
    CHECK(r && !r->answered);
    const std::string other = "another";
    r = check_against([&](const gt3r::Bytes&) { return std::optional(answer("0.9.0", "0.9.0", "https://example.org/other", &other)); });
    CHECK(r && !r->answered);
    r = check_against([&](const gt3r::Bytes&) { return std::optional(answer("0.2.0", "0.0.0", "ftp://example.org", &id)); }); // only web pages are kept
    CHECK(r && r->answered && r->answer.url.empty());
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
    CHECK(asked.size() == rv::kTaggedVersionQuerySize);
    // A relay without games answers its own game's versions: not taken (no notice from another game's releases).
    r = check_against([](const gt3r::Bytes&) { return std::optional(answer("9.9.0", "9.9.0", "https://example.org/other", nullptr)); });
    CHECK(r && !r->answered && !r->answer.latest.known() && r->error.find("no versions for this game") != std::string::npos);
    // Nobody answers: it gives up in its time with a reason.
    r = check_against([](const gt3r::Bytes&) { return std::optional<gt3r::Bytes>(); }, 300);
    CHECK(r && !r->answered && r->error.find("no answer") != std::string::npos);
}

TEST(updates_check_against_the_engines_relay) {
    // The engine's own relay server keeps versions per game: unlisted, ours has none (never its main game's); listed, ours.
    rv::RelayServer srv;
    if (!test::on_free_port([&](u16 port) { return srv.open(port); })) SKIP("no UDP sockets here");
    rv::RelayServer::Versions v;
    v.latest = v.min_online = ver("9.9.0");
    v.release_url = "https://example.org/main";
    const auto ask = [&] {
        srv.set_versions(v);
        net::VersionCheck check;
        check.start("127.0.0.1:" + std::to_string(srv.port()), net::this_version(), rv::RvPlatform::MacOS, 1500);
        std::optional<net::VersionCheck::Result> r;
        for (int i = 0; i < 1000 && !r; ++i) srv.step(5), r = check.take();
        return r;
    };
    std::optional<net::VersionCheck::Result> r = ask();
    CHECK(r && r->answered && !r->answer.latest.known() && !r->answer.min_online.known() && r->answer.url.empty());
    list_game(v, "0.2.0", "0.1.0");
    r = ask();
    CHECK(r && r->answered && r->answer.latest.number() == "0.2.0" && r->answer.min_online.number() == "0.1.0" && r->answer.url == "https://example.org/ours");
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

TEST(updates_relay_holds_us_to_our_versions) {
    // The engine's relay keeps versions per game and its session takes the game tag: the relay's main game asks for a version no
    // copy of ours has, yet our session is held to our own versions only.
    rv::RelayServer server;
    rv::RelayServer::Versions v;
    v.latest = v.min_online = ver("99.0.0");
    v.release_url = "https://example.org/main";
    list_game(v, net::this_version().number().c_str(), net::this_version().number().c_str());
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
        net::name_the_game(s);
        CHECK(s.host_via(at, "One", "", 0));
        run(s);
        CHECK(!s.update_required() && !s.code().empty());
    }
    // Our oldest version raised above this one: refused with our release.
    list_game(v, "99.0.0", "99.0.0");
    server.set_versions(v);
    rv::Session s;
    net::name_the_game(s);
    CHECK(s.host_via(at, "One", "", 0));
    run(s);
    CHECK(s.update_required() && s.update_required()->url == "https://example.org/ours");
}

TEST(updates_relay_joins_our_own_line) {
    // The engine's relay lets a player join only a host of the same x.y of this game (its game tag's version), and the
    // lobby words the refusal as ours (refusal_text): an older player of a host on our latest line is told to update, a
    // newer one that the host must, an older one of a host on a newer line both versions.
    const rv::GameVersion ours = net::this_version();
    const auto at_version = [](u16 major, u16 minor, u16 patch) {
        rv::GameVersion v;
        v.major = major, v.minor = minor, v.patch = patch;
        return v;
    };
    const rv::GameVersion patch = at_version(ours.major, ours.minor, u16(ours.patch + 3));
    const rv::GameVersion next = at_version(ours.major, u16(ours.minor + 1), 0);
    rv::RelayServer server;
    rv::RelayServer::Versions v;
    v.latest = v.min_online = ver("99.0.0");
    v.release_url = "https://example.org/main";
    list_game(v, ours.number().c_str(), "0.0.0"); // no oldest version: only the lines decide
    if (!test::on_free_port([&](u16 port) { return server.open(port); })) SKIP("no UDP sockets here");
    server.set_versions(v);
    const std::string at = "127.0.0.1:" + std::to_string(server.port());
    const auto as = [](rv::Session& s, const rv::GameVersion& ver) {
        rv::GameTag t = net::game_tag();
        t.version = ver;
        s.set_game_tag(t);
        s.set_game_version(ver);
    };
    // Host at `host`, a player at `player`: "" when the player joined, else the lobby's text for the refusal.
    const auto meet = [&](const rv::GameVersion& host, const rv::GameVersion& player) -> std::optional<std::string> {
        rv::Session h, j;
        as(h, host), as(j, player);
        if (!h.host_via(at, "One", "", 0)) return std::nullopt;
        const auto pump = [&](auto done) {
            for (int t = 0; t < 5000; t += 2) {
                server.step(0), h.poll(), j.poll();
                if (done()) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return false;
        };
        if (!pump([&] { return !h.code().empty(); })) return std::nullopt;
        if (!j.join_code(at, h.code(), "Two", "", 0)) return std::nullopt;
        std::optional<std::string> result;
        pump([&] {
            while (std::optional<rv::Event> e = j.next_event()) {
                if (e->kind == rv::Event::Kind::Joined) result = "";
                else if (e->kind == rv::Event::Kind::Disconnected || e->kind == rv::Event::Kind::Rejected)
                    result = net::refusal_text(e->text, j.update_required().has_value());
            }
            while (h.next_event()) {
            }
            return result.has_value();
        });
        return result;
    };
    CHECK(meet(ours, ours) == std::string());
    CHECK(meet(ours, patch) == std::string());
    CHECK(meet(patch, ours) == std::string());
    const std::optional<std::string> newer = meet(ours, next);
    CHECK(newer && *newer == "The host has version " + ours.number() + " (you have " + next.number() + "): the host needs to update.");
    const std::optional<std::string> older = meet(next, ours);
    CHECK(older && *older == "The host has version " + next.number() + " (you have " + ours.number() + "): update to join.");
    if (ours.minor > 0) {
        const std::optional<std::string> old_line = meet(ours, at_version(ours.major, u16(ours.minor - 1), 1)); // (0.0.0 is "unknown")
        CHECK(old_line && *old_line == "This copy of the game is out of date: it must be updated to play online.");
    }
}
