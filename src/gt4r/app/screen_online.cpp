// The connection's page and the players' panel (online.h).
//
// config.ini [online]: `server` the relay server approved (none until the player approves one; a user's own server is
// written there or typed on the page), `port` the local network's, `max_power`, `garage_cars`, `any_car`, `ghost_ms` and
// `players` the host's rules. GT4R_NET_LOOPBACK=1: nothing but this machine is asked or told (tests).
//
// GT4R_ONLINE="host,players=2,course=tsukuba,laps=2,car=<label>" / "join=127.0.0.1,car=<label>,name=Two" with
// --screen online: a race between copies without the menus (`server=127.0.0.1:47751,codefile=/tmp/code.txt`: through a
// relay server; `paint`, `tire`, `mt=1`, `races=2` more; `port`). The race itself reads GT4R_RACE as Drive does.

#include "gt4r/app/online.h"
#include "gt4r/net/presence_text.h"
#include "gt4r/net/version_check.h"
#include "gt4r/app/page.h"

#include "app/text_keyboard.h"
#include "base/file.h"
#include "base/log.h"
#include "gt4r/menu/game.h"
#include "gt4r/menu/game_network.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/net/identity.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/session.h"
#include "net/relay_server.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace gt4r {

namespace {

using gt3r::net::Session;
namespace rv = gt3r::net;

bool loopback_only() {
    const char* e = std::getenv("GT4R_NET_LOOPBACK");
    return e && *e && *e != '0';
}

// The disc's side of the lobby.
struct OnlineDisc {
    std::string key;
    sim::Program program;
    std::unique_ptr<net::CarBuilder> builder;
};
OnlineDisc& online_side() {
    static OnlineDisc d;
    return d;
}

// The player's unit of power (of PS): the game's option.
struct PowerUnit {
    float factor = 1;
    std::string name = "PS";
};
PowerUnit& power_unit() {
    static PowerUnit u;
    return u;
}
void take_power_unit(const menu::GameState& g) {
    const menu::GameState::Unit u = g.power_unit();
    power_unit() = {u.factor, u.name ? u.name : "PS"};
}
std::string power_text(int ps) { return std::to_string(int(float(ps) * power_unit().factor + 0.5f)) + " " + power_unit().name; }

std::string rules_text(const net::RaceRules& r, const net::LobbyLimits& l) {
    static const char* const kBoost[] = {"no boost", "weak boost", "strong boost"};
    std::string t = std::to_string(r.laps) + (r.laps == 1 ? " lap" : " laps");
    t += r.tire_wear ? ", tires wear" : ", no tire wear";
    t += std::string(", ") + kBoost[std::clamp(r.boost, 0, 2)];
    if (l.max_power) t += ", up to " + power_text(l.max_power);
    if (!l.garage_cars) t += ", cars as sold";
    if (!l.any_car) t += ", favorites only";
    return t;
}

std::string car_name(App& app, const std::string& label) {
    static std::map<std::string, std::string> names;
    if (names.empty())
        if (db::SpecDb* sdb = app.db())
            for (const db::CarEntry& c : sdb->cars()) names[c.label] = c.name.empty() ? c.label : c.name;
    const auto it = names.find(label);
    return it == names.end() ? label : it->second;
}

// `behind`: still in the race our own menus are back from.
const char* what_he_does(const net::Member& m, bool heard, bool behind) {
    if (!heard) return "joining";
    if (behind) return "in the race";
    if (m.sequence <= 2) return "waiting";
    if (m.sequence <= 4) return "choosing the course";
    if (m.sequence <= 6) return "choosing a car";
    return "ready";
}

// Opens the host's session on the local network: the port asked for, or one of the next few.
bool host_lan(Session& s, int port, const std::string& name, std::string* error) {
    for (int i = 0; i < 4; ++i)
        if (s.host(u16(port + i), name, "", 0, error)) return true;
    return false;
}

} // namespace

bool online_setup(App& app) {
    if (!app.disc() || !app.db() || !app.vfs()) return false;
    const std::optional<rv::OnlineDisc> disc = net::online_disc(app.disc()->info());
    if (!disc) return false;
    OnlineDisc& d = online_side();
    net::Lobby& l = net::lobby();
    if (d.key != app.disc()->info().digest) {
        d.builder.reset();
        d.key = app.disc()->info().digest;
        try {
            d.program = sim::Program::load(*app.disc());
        } catch (const std::exception& e) {
            LOG_WARN("Online: the program: %s", e.what());
        }
        if (d.program.known()) {
            gt3r::Vfs* vfs = app.vfs();
            d.builder = std::make_unique<net::CarBuilder>(*app.db(), d.program, [vfs](const std::string& path) { return vfs->read(path); });
        }
    }
    if (!d.builder) return false;
    if (loopback_only()) Session::set_advertise_lan(false);
    l.session().set_disc(*disc);
    net::name_the_game(l.session()); // (the game tag for the relay server: net/version_check.h)
    net::CarBuilder* b = d.builder.get();
    l.digest_car = [b](const net::CarChoice& c) { return b->digest(c); };
    l.check_car = [b](const net::CarChoice& c, const net::LobbyLimits& limits) { return net::limits_problem(*b, c, limits, power_unit().factor, power_unit().name); };
    db::SpecDb* sdb = app.db();
    l.build_car = [b, sdb](const net::CarChoice& c, const std::string& course) {
        const std::optional<sim::CourseRow> row = sim::course_row(*sdb, course);
        return b->raced(c, row ? row->kind : 0);
    };
    return true;
}

// ---- The connection's page.

namespace {

class ConnectScreen : public Screen {
public:
    // The Discord status (app/presence.h): finding a lobby.
    bool presence(App&, net::PresenceText& t) override {
        t.details = "Multi-LAN Race", t.state = "Finding a lobby";
        return true;
    }
    ConnectScreen(App& app, menu::Runtime& rt) : rt_(rt) {
        online_setup(app);
        take_power_unit(rt.game());
        server_ = app.config().get("online", "server").value_or("");
        name_ = net::online_name(app.config().get("online", "name"));
        port_ = std::atoi(app.config().get("online", "port").value_or(std::to_string(net::kLanPort)).c_str());
        if (port_ <= 0 || port_ > 65000) port_ = net::kLanPort;
        auto number = [&](const char* key, int otherwise) { return std::atoi(app.config().get("online", key).value_or(std::to_string(otherwise)).c_str()); };
        limits_.max_power = u16(std::clamp(number("max_power", 0), 0, 1500));
        limits_.garage_cars = number("garage_cars", 1) != 0, limits_.any_car = number("any_car", 1) != 0;
        ghost_ms_ = std::clamp(number("ghost_ms", 150), 0, 1000);
        players_ = std::clamp(number("players", 2), 2, net::kMaxLobbyPlayers);
        search_.search(loopback_only());
    }

    void update(App& app, double) override {
        net::Lobby& l = net::lobby();
        search_.poll();
        build(app);
        if (page_ == Page::Connecting) return connecting(app, l);
        if (editing_) return edit(app);
        const MenuInput& m = app.menu();
        if (rows_page_ != page_) rows_page_ = page_, page_rows_.selected = -1, page_rows_.scroll.first = 0;
        page_rows_.rows.clear();
        for (const Row& r : rows_) {
            PageRow row;
            row.kind = r.act ? PageRow::Kind::Action : r.change ? PageRow::Kind::Choice : PageRow::Kind::Heading;
            row.label = r.label, row.value = r.value, row.change = r.change, row.press_hint = "choose";
            if (r.act) row.press = [this, act = r.act] { message_.clear(), act(); };
            page_rows_.rows.push_back(std::move(row));
        }
        if (page_rows_.update(app).changed) {
            return;
        }
        if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) {
            message_.clear();
            if (page_ == Page::Start) return leave(app);
            page_ = Page::Start;
        }
    }

    void on_event(App&, const SDL_Event& e) override {
        if (!editing_) return;
        if (e.type == SDL_EVENT_TEXT_INPUT) keys_.close(); // (a real keyboard types: ours is not needed)
        typed_ = gt3r::text_field_event(e, text_, !keys_.is_open());
        if (typed_ != gt3r::TextKeyboard::Result::None) pending_ = typed_;
    }

    void render(App& app, Ui& ui) override {
        ui.header(page_ == Page::Host ? (through_server_ ? "OPEN A LOBBY WITH A CODE" : "OPEN A LOBBY ON THE LOCAL NETWORK") : "MULTI-LAN RACE", name_);
        if (page_ == Page::Connecting) {
            ui.text_center(150, connecting_text_, colors::kText);
            ui.footer("{Triangle}: stop");
            return;
        }
        page_rows_.render(ui, {16, 30, 608, 250});
        float ly = 288;
        const int sel = page_rows_.selected;
        if (!message_.empty())
            for (const std::string& line : Ui::wrap(message_, 600)) ui.text(24, ly, line, colors::kBad), ly += 12;
        else if (sel >= 0 && sel < int(rows_.size()))
            for (const std::string& line : Ui::wrap(rows_[size_t(sel)].note, 600)) ui.text(24, ly, line, colors::kDim), ly += 12;
        if (editing_) {
            if (keys_.is_open()) return keys_.draw(ui, text_);
            ui.fill_round(80, 140, 480, 64, 5, colors::kPanel);
            ui.text(96, 152, edit_title_, colors::kAccent);
            ui.text(96, 172, text_ + "_", colors::kText);
            ui.footer("Enter: done   Esc: cancel");
            return;
        }
        ui.footer("{Up/Down}: select  " + page_rows_.hint() + "  {Triangle}: back");
    }

    ~ConnectScreen() override { stop_typing(); }

private:
    enum class Page { Start, Host, Approve, Connecting };
    struct Row {
        std::string label, value, note;
        std::function<void()> act;
        std::function<void(int)> change;
    };

    void build(App& app) {
        rows_.clear();
        App* a = &app;
        if (page_ == Page::Start) {
            rows_.push_back({"On the local network", "", "", {}, {}});
            const std::string disc = net::online_disc(app.disc()->info()).value_or(rv::OnlineDisc{}).id;
            int shown = 0;
            for (const net::FoundLobby& f : search_.found()) {
                if (f.disc != disc || shown >= 5) continue;
                ++shown;
                const std::string address = f.address;
                rows_.push_back({f.name + "'s lobby", std::to_string(f.players) + " of " + std::to_string(f.limit) + (f.open ? "" : ", racing"), "Join it (" + address + ").",
                                 [this, address] { join(address); }, {}});
            }
            rows_.push_back({"Open a lobby", "", "This machine hosts: the others on the local network find it here, or join its address.", [this] { page_ = Page::Host, through_server_ = false; }, {}});
            rows_.push_back({"Join by address", last_address_, "A lobby's address and port, for example 192.168.1.5:" + std::to_string(net::kLanPort) + ".",
                             [this] { type("The lobby's address", last_address_, [this](const std::string& t) { last_address_ = t, join(t); }); }, {}});
            rows_.push_back({"Over the internet", "", "", {}, {}});
            rows_.push_back({"Open a lobby with a code", "", "The server gives the lobby a code; the others join with it. No port needs to be opened.", [this] { through_server([this] { page_ = Page::Host, through_server_ = true; }); }, {}});
            rows_.push_back({"Join with a code", last_code_, "The code of the lobby's host.", [this] {
                                 through_server([this] { type("The lobby's code", last_code_, [this](const std::string& t) { last_code_ = t, join_code(t); }); });
                             }, {}});
            rows_.push_back({"This copy", "", "", {}, {}});
            rows_.push_back({"Your name", name_, "What the other players see. Your own, apart from the Gran Turismo mode player's name.",
                             [this, a] { type("Your name", name_, [this, a](const std::string& t) { set_name(*a, t); }); }, {}});
            rows_.push_back({"Internet server", server_.empty() ? std::string("none chosen") : server_, "The server that gives the codes and carries what cannot go straight. Your own can be typed here.",
                             [this, a] { type("The server", server_.empty() ? std::string(kOfferedServer) : server_, [this, a](const std::string& t) { set_server(*a, t); }); }, {}});
        } else if (page_ == Page::Approve) {
            rows_.push_back({"Internet races meet through a server", "", "", {}, {}});
            rows_.push_back({std::string("Use ") + kOfferedServer, "", "The server sees this machine's internet address while a lobby is open, and passes it to the players who join it. Nothing is sent to it until this is chosen.",
                             [this, a] {
                                 set_server(*a, kOfferedServer);
                                 page_ = Page::Start;
                                 if (approved_) std::exchange(approved_, {})();
                             }, {}});
            rows_.push_back({"Another server", "", "Its name, or its address and port.", [this, a] {
                                 type("The server", "", [this, a](const std::string& t) {
                                     set_server(*a, t);
                                     page_ = Page::Start;
                                     if (!server_.empty() && approved_) std::exchange(approved_, {})();
                                 });
                             }, {}});
            rows_.push_back({"No server", "", "The local network and addresses stay.", [this] { page_ = Page::Start; }, {}});
        } else if (page_ == Page::Host) {
            menu::GameState& g = rt_.game();
            auto option = [this, &g](const char* label, const char* key, std::vector<std::string> names, const char* note) {
                const int now = std::clamp(g.option_int(key), 0, int(names.size()) - 1);
                rows_.push_back({label, names[size_t(now)], note, {}, [this, &g, key, n = int(names.size()), now](int d) { g.options[key] = s32(std::clamp(now + d, 0, n - 1)), changed_ = true; }});
            };
            rows_.push_back({"The race (the 2 Player Battle's options)", "", "", {}, {}});
            rows_.push_back({"Players", std::to_string(players_), "The race's menus begin once so many are there; or with those who are, by the host's Start.", {}, [this](int d) { players_ = std::clamp(players_ + d, 2, net::kMaxLobbyPlayers), changed_ = true; }});
            rows_.push_back({"Laps", std::to_string(std::clamp(g.option_int("split_race_laps", 2), 1, 99)), "", {}, [this, &g](int d) { g.options["split_race_laps"] = s32(std::clamp(g.option_int("split_race_laps", 2) + d, 1, 99)), changed_ = true; }});
            rows_.push_back({"Tires", g.option_int("split_tire_damage") ? "wear" : "do not wear", "", {}, [this, &g](int) { g.options["split_tire_damage"] = s32(g.option_int("split_tire_damage") ? 0 : 1), changed_ = true; }});
            option("Boost for the cars behind", "split_boost_level", {"none", "weak", "strong"}, "");
            rows_.push_back({"What may race", "", "", {}, {}});
            rows_.push_back({"Power", limits_.max_power ? "up to " + power_text(limits_.max_power) : std::string("no limit"), "The car's power as it races, its parts and settings counted.", {},
                             [this](int d) { limits_.max_power = u16(std::clamp(int(limits_.max_power) + 25 * d, 0, 1500)), changed_ = true; }});
            rows_.push_back({"Garage cars", limits_.garage_cars ? "with their parts and settings" : "not allowed", "Gran Turismo mode's cars off the memory cards, as in the 2 Player Battle.", {}, [this](int) { limits_.garage_cars = !limits_.garage_cars, changed_ = true; }});
            rows_.push_back({"Car choice", limits_.any_car ? "every car" : "each player's favorites", "The favorites alone are the original's LAN Battle.", {}, [this](int) { limits_.any_car = !limits_.any_car, changed_ = true; }});
            rows_.push_back({"Contact", ghost_ms_ ? "up to a round trip of " + std::to_string(ghost_ms_) + " ms" : std::string("always"), "Cars whose machines are further apart drive through each other.", {}, [this](int d) { ghost_ms_ = std::clamp(ghost_ms_ + 25 * d, 0, 500), changed_ = true; }});
            rows_.push_back({through_server_ ? "Open it and get a code" : "Open it", "", "", [this, a] { host(*a); }, {}});
        }
    }

    // The online name is the app's setting, not the saved game's: the game's LAN machine name only mirrors it for the pages.
    void set_name(App& app, const std::string& t) {
        name_ = net::online_name(t);
        app.config().set("online", "name", name_);
        app.save_config();
    }
    void set_server(App& app, const std::string& t) {
        server_ = std::string(gt3r::trim(t));
        app.config().set("online", "server", server_);
        app.save_config();
    }
    // Something that needs the server: at once, or after the player has approved one.
    void through_server(std::function<void()> then) {
        if (!server_.empty()) return then();
        approved_ = std::move(then);
        page_ = Page::Approve;
    }

    void begin(bool ok, const std::string& error, bool host, const std::string& text) {
        net::Lobby& l = net::lobby();
        if (!ok) {
            message_ = error.empty() ? std::string("The connection could not be opened.") : net::refusal_text(error, false);
            return;
        }
        menu::GameState& g = rt_.game();
        g.options["LAN_host"] = s32(host), g.options["LAN_style"] = std::string("play");
        g.options["LAN_machine_name"] = name_;
        g.options["LAN_player_num"] = s32(players_), g.options["LAN_monitor_num"] = s32(0);
        l.begin(name_);
        if (host)
            l.change([&](net::Member& m) { m.rules = menu::rules_from_options(g), m.rules.ghost_ping_ms = ghost_ms_, m.limits = limits_; });
        page_ = Page::Connecting, connecting_text_ = text, waiting_ = 0;
    }
    void host(App& app) {
        for (const auto& [key, value] : {std::pair<const char*, int>{"max_power", limits_.max_power}, {"garage_cars", limits_.garage_cars}, {"any_car", limits_.any_car}, {"ghost_ms", ghost_ms_}, {"players", players_}})
            app.config().set("online", key, std::to_string(value));
        if (changed_) app.save_config(), rt_.save_options();
        std::string error;
        Session& s = net::lobby().session();
        const bool ok = through_server_ ? s.host_via(server_, name_, "", 0, &error) : host_lan(s, port_, name_, &error);
        begin(ok, error, true, through_server_ ? "Asking " + server_ + " for a code..." : std::string("Opening the lobby..."));
    }
    void join(const std::string& address) {
        if (address.empty()) return;
        std::string error, at = address;
        if (at.find(':') == std::string::npos) at += ":" + std::to_string(port_);
        begin(net::lobby().session().join(at, name_, "", 0, &error), error, false, "Joining " + at + "...");
    }
    void join_code(const std::string& code) {
        if (!rv::valid_code(rv::normalize_code(code))) {
            message_ = "A code has eight letters and digits.";
            return;
        }
        std::string error;
        begin(net::lobby().session().join_code(server_, code, name_, "", 0, &error), error, false, "Joining " + rv::format_code(rv::normalize_code(code)) + "...");
    }

    void connecting(App& app, net::Lobby& l) {
        l.poll();
        const bool code_needed = l.session().via_server() && l.session().is_host();
        if (l.active() && (!code_needed || !l.session().code().empty()) && (l.host() || l.heard(0))) return app.pop(); // (the menus go on)
        if (const std::string why = l.take_error(); !why.empty() || l.closed()) {
            l.end();
            message_ = why.empty() ? std::string("The connection was lost.") : why;
            page_ = Page::Start;
            return;
        }
        if (app.menu().pressed(PadButton::Triangle) || app.menu().pressed(PadButton::Circle)) {
            l.end();
            page_ = Page::Start;
        }
    }
    void leave(App& app) {
        net::lobby().end();
        app.pop();
    }

    // Text typed: a real keyboard's, or on ours with the pad.
    void type(std::string title, std::string text, std::function<void(const std::string&)> done) {
        edit_title_ = std::move(title), text_ = std::move(text), edited_ = std::move(done);
        editing_ = true, pending_ = gt3r::TextKeyboard::Result::None;
        if (!Ui::prompts_keyboard()) keys_.open(edit_title_, "Done", false);
        want_typing_ = true;
    }
    void edit(App& app) {
        if (want_typing_) {
            want_typing_ = false;
            window_ = SDL_GetRenderWindow(app.renderer());
            if (window_) SDL_StartTextInput(window_);
            return; // (the press that opened it is not its first key)
        }
        auto r = std::exchange(pending_, gt3r::TextKeyboard::Result::None);
        if (keys_.is_open()) {
            const auto pressed = keys_.update(app.menu(), {}, text_);
            if (pressed != gt3r::TextKeyboard::Result::None) r = pressed;
        }
        if (r == gt3r::TextKeyboard::Result::None) return;
        editing_ = false;
        keys_.close();
        stop_typing();
        const std::string t(gt3r::trim(text_));
        if (r == gt3r::TextKeyboard::Result::Done && !t.empty() && edited_) std::exchange(edited_, {})(t);
    }
    void stop_typing() {
        if (window_) SDL_StopTextInput(window_), window_ = nullptr;
    }

    menu::Runtime& rt_;
    net::Discovery search_;
    Page page_ = Page::Start;
    std::vector<Row> rows_;
    PageRows page_rows_;
    Page rows_page_ = Page::Start;
    int port_ = net::kLanPort, players_ = 2, ghost_ms_ = 150, waiting_ = 0;
    net::LobbyLimits limits_;
    bool through_server_ = false, changed_ = false;
    std::string server_, name_, message_, connecting_text_, last_address_, last_code_;
    std::function<void()> approved_;
    // The text being typed.
    gt3r::TextKeyboard keys_;
    gt3r::TextKeyboard::Result typed_ = gt3r::TextKeyboard::Result::None, pending_ = gt3r::TextKeyboard::Result::None;
    bool editing_ = false, want_typing_ = false;
    std::string edit_title_, text_;
    std::function<void(const std::string&)> edited_;
    SDL_Window* window_ = nullptr;
};

} // namespace

std::unique_ptr<Screen> make_connect_screen(App& app, menu::Runtime& rt) { return std::make_unique<ConnectScreen>(app, rt); }

// ---- The lobby while the menus run.

OnlineMenus::OnlineMenus(App& app, menu::Runtime& rt) : rt_(rt) {
    if (online_setup(app)) rt_.network = &net::lobby();
}

OnlineMenus::~OnlineMenus() {
    if (rt_.network) net::lobby().end();
    rt_.network = nullptr;
}

void OnlineMenus::leave() {
    open_ = false, confirm_ = -1;
    menu::leave_lan_pages(rt_);
}

bool OnlineMenus::frame(App& app, const MenuInput& m) {
    if (!rt_.network) return false;
    net::Lobby& l = net::lobby();
    if (l.active()) take_power_unit(rt_.game());
    if (racing_) { // the race screen is gone again
        racing_ = false;
        l.end_race();
        menu::GameState& g = rt_.game();
        g.next_sequence.clear();
        g.last_sequence = "lan_race";
        g.next_menu_project = g.major_menu_project.empty() ? std::string("arcade") : g.major_menu_project;
    }
    l.poll();
    // The local network's lobbies are found while the host's menus run.
    const bool answers = l.host() && !l.session().via_server();
    if (answers && !discovery_.open()) discovery_.host(loopback_only());
    if (!answers && discovery_.open()) discovery_.close();
    if (answers) {
        net::Discovery::Answer a;
        a.port = l.session().port(), a.name = l.name(0);
        a.disc = net::online_disc(app.disc()->info()).value_or(rv::OnlineDisc{}).id;
        a.players = l.count(), a.limit = net::kMaxLobbyPlayers, a.open = l.session().in_lobby();
        discovery_.poll(a);
    }
    // (Scripted runs, videos: GT4R_LOBBY_CODEFILE=<file> gets the host's code, for a second copy's `typefile:` key.)
    if (static const char* code_file = std::getenv("GT4R_LOBBY_CODEFILE"); code_file && *code_file && !code_written_ && l.host() && !l.session().code().empty()) {
        const std::string code = l.session().code();
        code_written_ = gt3r::write_host_file(code_file, ByteSpan(reinterpret_cast<const u8*>(code.data()), code.size()));
    }
    // The lobby ended by itself: why, then the arcade's pages.
    if (l.closed() && notice_.empty()) {
        notice_ = l.take_error();
        if (notice_.empty()) notice_ = "The lobby was closed.";
    }
    if (!notice_.empty()) {
        if (m.pressed(PadButton::Cross) || m.pressed(PadButton::Triangle) || m.pressed(PadButton::Start)) {
            notice_.clear();
            leave();
        }
        return true;
    }
    if (!l.active()) return false;
    const std::string page = rt_.current_page() ? rt_.current_page()->name : std::string();
    const bool matching = page == "MatchingRoot";
    if (matching && l.host() && l.count() >= 2 && m.pressed(PadButton::Start)) {
        // The host begins with the players who are there.
        rt_.game().options["LAN_player_num"] = s32(l.count()), rt_.game().options["LAN_monitor_num"] = s32(0);
        return true;
    }
    if (m.pressed(PadButton::Select)) {
        open_ = !open_, confirm_ = -1, sel_ = 0;
        return true;
    }
    if (!open_) return false;
    sel_ = std::clamp(sel_ + m.vertical(), 0, std::max(0, l.count() - 1));
    if (confirm_ >= 0) {
        if (m.pressed(PadButton::Cross)) {
            if (confirm_ == 100) leave();
            else if (l.host() && confirm_ > 0 && confirm_ < l.count()) l.session().kick(l.id_of(confirm_));
            confirm_ = -1;
        } else if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) confirm_ = -1;
        return true;
    }
    if (m.pressed(PadButton::Square) && l.host() && sel_ > 0) confirm_ = sel_;
    if (m.pressed(PadButton::Start)) confirm_ = 100;
    if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) open_ = false;
    return true;
}

bool OnlineMenus::finished(App& app) {
    if (!rt_.network || !rt_.finished()) return false;
    net::Lobby& l = net::lobby();
    menu::GameState& g = rt_.game();
    const std::optional<menu::RaceRequest> race = rt_.race_request();
    if (race && race->sequence == "lan_race") {
        if (!l.active()) { // (the lobby went while the menus ended)
            if (notice_.empty()) leave();
            return false;
        }
        if (l.host()) l.want_race(race->course);
        if (std::optional<net::OnlineRace> online = l.take_race()) {
            RaceStart start;
            start.course = online->course;
            const net::RaceCar& mine = online->cars[size_t(online->ours)];
            start.car = mine.label, start.paint = int(mine.paint), start.cars = int(online->cars.size()), start.laps = online->rules.laps;
            start.online = std::make_shared<const net::OnlineRace>(std::move(*online));
            start.options = g.options;
            racing_ = true, open_ = false;
            if (audio::AppSound* sound = app.sound()) sound->menu().stop_music(), sound->menu().stop_stream();
            try {
                app.push(make_race_screen(app, start));
            } catch (const std::exception& e) {
                notice_ = std::string("The race could not be started: ") + e.what();
                LOG_WARN("Online: %s", notice_.c_str());
            }
        }
        return true;
    }
    if (race) return false;
    if (rt_.wanted_project() == "network" && !rt_.is_prologue()) {
        if (!connecting_) {
            connecting_ = true;
            app.push(make_connect_screen(app, rt_));
            return true;
        }
        // Back from it: the arcade's pages, the LAN Battle's when the lobby is there.
        connecting_ = false;
        g.next_menu_project = g.major_menu_project.empty() ? std::string("arcade") : g.major_menu_project;
        g.next_menu_arg.clear();
        return false;
    }
    return false;
}

void OnlineMenus::panel(App& app, Ui& ui, float x, float y, float w, float h, bool compact) {
    net::Lobby& l = net::lobby();
    Session& s = l.session();
    ui.fill_round(x, y, w, h, 6, Color{12, 14, 20, 235});
    ui.frame_round(x, y, w, h, 6, 1, colors::kOutline);
    ui.text(x + 12, y + 10, "PLAYERS", colors::kAccent);
    if (s.via_server()) {
        if (!s.code().empty() || !l.host()) {
            const std::string code = rv::format_code(s.code());
            ui.text_right(x + w - 12, y + 10, l.host() ? "The code to join:" : "", colors::kDim);
            if (l.host()) ui.text_big(x + w - 12 - Ui::text_width(code, 2) - 30, y + 24, code, 2, colors::kText);
        }
    } else if (l.host()) {
        std::string at;
        if (!loopback_only())
            for (const std::string& a : rv::local_addresses()) at += (at.empty() ? "" : ", ") + a + ":" + std::to_string(s.port());
        else at = "127.0.0.1:" + std::to_string(s.port());
        ui.text_right(x + w - 12, y + 10, Ui::elide("Address: " + at, w - 120), colors::kDim);
    }
    const float top = y + (s.via_server() && l.host() ? 50 : 30);
    for (int i = 0; i < l.count(); ++i) {
        const float ry = top + float(i) * 18;
        const net::Member& m = l.member(i);
        const bool on = open_ && i == sel_;
        ui.fill_round(x + 8, ry - 2, w - 16, 16, 3, on ? colors::kHighlight : colors::kPanel);
        ui.text(x + 14, ry, Ui::elide(l.name(i) + (i == 0 ? " (host)" : "") + (i == l.my_index() ? " (you)" : ""), 150), colors::kText);
        const std::string problem = l.car_problem(i);
        ui.text(x + 170, ry, Ui::elide(!problem.empty() ? problem : m.choice ? car_name(app, m.choice->label) : std::string(what_he_does(m, l.heard(i), m.sync > 0 && m.sync < l.mine().sync)), w - 340),
                !problem.empty() ? colors::kBad : m.choice ? colors::kText : colors::kDim);
        if (m.choice && problem.empty() && m.sequence >= 7) ui.text_right(x + w - 120, ry, "ready", colors::kGood);
        if (i != l.my_index()) {
            const rv::Link link = s.link(l.id_of(i));
            char b[64] = "";
            if (link.known) std::snprintf(b, sizeof b, "%d ms, %s", int(link.rtt_ms + 0.5), link.relay ? "relay" : link.direct ? "direct" : "via the host");
            ui.text_right(x + w - 14, ry, b, colors::kDim);
        }
    }
    float ny = top + float(l.count()) * 18 + 6;
    const std::string rules = rules_text(l.host_member().rules, l.host_member().limits);
    for (const std::string& line : Ui::wrap(rules, w - 24)) ui.text(x + 12, ny, line, colors::kDim), ny += 12;
    std::string hint;
    if (confirm_ == 100) hint = "Leave the lobby? {Cross}: leave  {Triangle}: stay";
    else if (confirm_ >= 0) hint = "Remove " + l.name(confirm_) + "? {Cross}: remove  {Triangle}: no";
    else if (compact && l.host()) hint = l.count() >= 2 ? "{Start}: begin with these players   {Triangle}: close the lobby" : "Waiting for the others...   {Triangle}: close the lobby";
    else if (compact) hint = "The host begins when everyone is here.   {Triangle}: leave";
    else hint = std::string("{Select}: back to the page   ") + (l.host() && sel_ > 0 ? "{Square}: remove this player   " : "") + "{Start}: leave the lobby";
    ui.text(x + 12, y + h - 18, hint, confirm_ >= 0 ? colors::kText : colors::kDim);
}

void OnlineMenus::render(App& app, Ui& ui, float x, float scale) {
    if (!rt_.network) return;
    net::Lobby& l = net::lobby();
    const float cx = x + 320 * scale;
    if (!notice_.empty()) {
        ui.fill_round(cx - 200, 130, 400, 80, 6, Color{12, 14, 20, 240});
        ui.frame_round(cx - 200, 130, 400, 80, 6, 1, colors::kOutline);
        float y = 144;
        for (const std::string& line : Ui::wrap(notice_, 376)) ui.text_center_in(cx - 200, 400, y, line, colors::kText), y += 13;
        ui.text_center_in(cx - 200, 400, 192, "{Cross}: back to the menus", colors::kDim);
        return;
    }
    if (!l.active() || racing_) return;
    const std::string page = rt_.current_page() ? rt_.current_page()->name : std::string();
    const std::optional<menu::RaceRequest> race = rt_.finished() ? rt_.race_request() : std::nullopt;
    if (race && race->sequence == "lan_race") {
        std::string waits = l.host() ? l.waiting_for() : std::string("the host");
        const rv::RelayWait& queue = l.session().relay_wait();
        if (queue.waiting) waits = rv::relay_wait_text(queue);
        panel(app, ui, cx - 230, 60, 460, 220, false);
        ui.text_center_in(cx - 230, 460, 290, queue.waiting ? waits : "Waiting for " + (waits.empty() ? std::string("the race") : waits) + "...", colors::kText);
        return;
    }
    if (open_ || page == "MatchingRoot") panel(app, ui, cx - 230, page == "MatchingRoot" && !open_ ? 90 : 60, 460, 200, page == "MatchingRoot" && !open_);
    else if (!page.empty() && rt_.project() && rt_.project()->name() == "arcade" && page != "TopRoot" && page != "GameRoot") {
        if (page == "CourseRoot" && !l.synced()) {
            ui.fill_round(cx - 130, 150, 260, 24, 5, Color{0, 0, 0, 190});
            ui.text_center_in(cx - 130, 260, 156, "Waiting for the others' race to end...", colors::kText);
        }
        // Who is ready, small, in the corner the pages leave empty.
        int ready = 0;
        for (int i = 0; i < l.count(); ++i) ready += l.member(i).sequence >= 7;
        ui.text_right(x + 640 * scale - 6, 2, std::to_string(l.count()) + " players" + (ready ? ", " + std::to_string(ready) + " ready" : "") + "   {Select}", Color{230, 230, 230, 200});
    }
}

// ---- A race between copies without the menus.

namespace {

class OnlineScript : public Screen {
public:
    explicit OnlineScript(App& app) {
        std::string spec = std::getenv("GT4R_ONLINE") ? std::getenv("GT4R_ONLINE") : "host";
        net::CarChoice c;
        c.label = "nsx_r_02", c.tire = -1;
        std::string join, server, name = "One";
        int port = net::kLanPort;
        for (std::string_view kv : gt3r::split(spec, ',')) {
            const size_t eq = kv.find('=');
            const std::string k(gt3r::trim(kv.substr(0, eq))), v = eq == std::string_view::npos ? std::string() : std::string(gt3r::trim(kv.substr(eq + 1)));
            if (k == "host") host_ = true;
            else if (k == "join") join = v;
            else if (k == "name") name = v;
            else if (k == "players") players_ = std::clamp(std::atoi(v.c_str()), 1, net::kMaxLobbyPlayers);
            else if (k == "course") course_ = v;
            else if (k == "laps") rules_.laps = std::clamp(std::atoi(v.c_str()), 1, 99);
            else if (k == "wear") rules_.tire_wear = v != "0";
            else if (k == "boost") rules_.boost = std::atoi(v.c_str());
            else if (k == "car") c.label = v;
            else if (k == "paint") c.paint = u8(std::atoi(v.c_str()));
            else if (k == "tire") c.tire = std::atoi(v.c_str());
            else if (k == "mt") c.automatic = v == "0";
            else if (k == "asm") c.asm_on = v != "0";
            else if (k == "tcs") c.tcs_on = v != "0";
            else if (k == "server") server = v;
            else if (k == "codefile") code_file_ = v;
            else if (k == "port") port = std::atoi(v.c_str());
            else if (k == "races") races_ = std::atoi(v.c_str());
        }
        if (!online_setup(app)) {
            status_ = "no online races on this disc";
            return;
        }
        net::Lobby& l = net::lobby();
        c.digest = l.digest_car(c);
        if (!c.digest) {
            status_ = "this disc has no car " + c.label;
            LOG_WARN("Online script: %s", status_.c_str());
            return;
        }
        choice_ = c;
        name_ = name, server_ = server;
        std::string error;
        Session& s = l.session();
        bool ok = false;
        if (host_) ok = server.empty() ? host_lan(s, port, name, &error) : s.host_via(server, name, "", 0, &error);
        else if (!server.empty()) {
            waiting_code_ = true;
            return;
        } else ok = s.join(join.find(':') == std::string::npos ? join + ":" + std::to_string(port) : join, name, "", 0, &error);
        status_ = ok ? (host_ ? "hosting" : "joining") : error;
        if (ok) begin();
    }
    ~OnlineScript() override { net::lobby().end(); }

    void update(App& app, double) override {
        net::Lobby& l = net::lobby();
        if (waiting_code_) {
            const std::optional<Bytes> code = gt3r::read_host_file(code_file_);
            if (!code || code->size() < 8) return;
            std::string error;
            waiting_code_ = false;
            if (l.session().join_code(server_, std::string(gt3r::trim(std::string(code->begin(), code->end()))), name_, "", 0, &error)) begin();
            else status_ = error;
            return;
        }
        if (racing_) {
            racing_ = false;
            l.end_race();
            if (--races_ <= 0) {
                app.quit();
                return;
            }
            l.enter_sync();
            entered_ = true, told_ = false;
        }
        l.poll();
        if (const std::string why = l.take_error(); !why.empty()) status_ = why, LOG_WARN("Online script: %s", why.c_str());
        if (!l.active()) return;
        if (host_ && !code_file_.empty() && !code_written_ && !l.session().code().empty()) {
            const std::string& code = l.session().code();
            code_written_ = gt3r::write_host_file(code_file_, ByteSpan(reinterpret_cast<const u8*>(code.data()), code.size()));
        }
        if (!entered_) l.enter_sync(), entered_ = true;
        if (l.synced() && !told_) {
            told_ = true;
            l.change([&](net::Member& m) { m.choice = choice_, m.sequence = 8, m.rules = rules_; });
        }
        if (host_ && l.count() >= players_) l.want_race(course_);
        status_ = std::to_string(l.count()) + " players" + (l.host() ? ", waiting for " + l.waiting_for() : "");
        if (std::optional<net::OnlineRace> online = l.take_race()) {
            RaceStart start;
            start.course = online->course;
            const net::RaceCar& mine = online->cars[size_t(online->ours)];
            start.car = mine.label, start.paint = int(mine.paint), start.cars = int(online->cars.size()), start.laps = online->rules.laps;
            start.online = std::make_shared<const net::OnlineRace>(std::move(*online));
            racing_ = true;
            app.push(make_race_screen(app, start));
        }
    }
    void render(App&, Ui& ui) override {
        ui.header("ONLINE", host_ ? "host" : "guest");
        ui.text(24, 60, status_, colors::kText);
    }

private:
    void begin() {
        net::lobby().begin(name_);
        net::lobby().change([&](net::Member& m) { m.rules = rules_; });
    }
    bool host_ = false, waiting_code_ = false, racing_ = false, entered_ = false, told_ = false, code_written_ = false;
    int players_ = 2, races_ = 1;
    std::string course_ = "tsukuba", status_, server_, code_file_, name_;
    net::RaceRules rules_;
    net::CarChoice choice_;
};

} // namespace

std::unique_ptr<Screen> make_online_script_screen(App& app) { return std::make_unique<OnlineScript>(app); }

} // namespace gt4r
