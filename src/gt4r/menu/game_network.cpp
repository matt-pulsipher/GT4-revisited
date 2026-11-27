// main::network: docs/specs/net.md "The game's own pages". The natives' rules are the program's (0x1E4B20 .. 0x1E7210 and
// the routines behind them, 0x1F6828 .. 0x1F7628), their records the lobby's.

#include "gt4r/menu/game_network.h"

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/game_settings.h"
#include "gt4r/menu/native.h"
#include "gt4r/menu/runtime.h"

#include <algorithm>
#include <map>

namespace gt4r::menu {

using script::Args;
using script::Kind;
using script::Value;
using script::Vm;

namespace {

constexpr const char* kPenalties[] = {"NONE", "SPEED_LIMITATION", "PASSING_PITLANE"};

// The active lobby; null without one.
net::Lobby* live(Runtime& rt) { return rt.network && rt.network->active() ? rt.network : nullptr; }

// A script object's attribute by its path under the running project ("CarRoot::current_car").
Value page_value(Runtime& rt, const std::string& path) { return rt.vm().lookup("ArcadeProject::" + path); }

std::shared_ptr<script::Module> page_module(Runtime& rt, const std::string& path) { return page_value(rt, path).as<script::Module>(); }

// A script function replaced by `fn`, which is given the original.
void replace(Runtime& rt, const std::string& module, const std::string& name, std::function<Value(Vm&, const Value& original, const Value& self, Args)> fn) {
    const std::shared_ptr<script::Module> m = page_module(rt, module);
    if (!m) return;
    const auto it = m->members.find(name);
    if (it == m->members.end() || it->second->value.kind != Kind::Function) return;
    const Value original = it->second->value;
    it->second->value = Vm::native(name, [original, fn = std::move(fn)](Vm& vm, const Value& self, Args a) { return fn(vm, original, self, a); });
}

// A call with the arcade's game read as the 2-Player Battle's (whose pages have the cars and garages).
Value as_split_battle(Runtime& rt, Vm& vm, const Value& callee, Args a) {
    const std::shared_ptr<script::Module> game = page_module(rt, "GameRoot");
    if (!game) return vm.call(callee, a);
    const std::shared_ptr<script::Variable> current = game->define("current_game", page_value(rt, "GameRoot::current_game"));
    const Value before = current->value;
    current->value = Value::string("SplitBattle");
    Value out;
    try {
        out = vm.call(callee, a);
    } catch (...) {
        current->value = before;
        throw;
    }
    current->value = before;
    return out;
}

std::map<const Runtime*, std::string>& lan_picks() {
    static std::map<const Runtime*, std::string> picks;
    return picks;
}

bool on_lan(Runtime& rt) { return live(rt) && page_value(rt, "GameRoot::current_game").to_string() == "LANBattle"; }
bool any_car(Runtime& rt) { return on_lan(rt) && rt.network->host_member().limits.any_car; }

} // namespace

net::RaceRules rules_from_options(const GameState& g) {
    net::RaceRules r;
    r.laps = std::clamp(g.option_int("split_race_laps", 2), 1, 99);
    r.tire_wear = g.option_int("split_tire_damage") != 0;
    r.handicap = g.option_int("split_handicap");
    r.boost = g.option_int("split_boost_level");
    r.heavy = g.option_int("LAN_heavy");
    const std::string penalty = g.option_text("penalty_type");
    for (int i = 0; i < 3; ++i)
        if (penalty == kPenalties[i]) r.penalty = i;
    return r;
}

std::optional<net::CarChoice> choice_of_pages(Runtime& rt) {
    const std::shared_ptr<GameNatives> n = natives_of(rt);
    if (!n) return std::nullopt;
    const GameState& g = rt.game();
    const RaceRequest& q = g.race;
    net::CarChoice c;
    c.automatic = q.manual == 0;
    if (CarHandle* garage = n->game ? car_handle(n->game->get("car_garage")) : nullptr; garage && !garage->label.empty()) {
        const CarRecord& rec = garage->record();
        c.label = garage->label, c.garage = true;
        std::copy_n(rec.active(), c.equipment.bytes.size(), c.equipment.bytes.begin());
        std::copy_n(rec.bytes.begin() + 0x498, c.condition.size(), c.condition.begin());
        c.paint = u8(std::max<int>(0, gt3r::s8(rec.active()[0x18])));
    } else {
        const Value car = page_value(rt, "CarRoot::current_car"), color = page_value(rt, "CarRoot::current_col");
        c.label = car.kind == Kind::String && !car.as_string().empty() ? car.as_string() : q.car;
        c.paint = u8(std::clamp(color.kind == Kind::Int ? color.i : q.paint, 0, 127));
        c.tire = std::clamp(q.tire, 0, 12);
        c.asm_on = q.assist_asm != 0, c.tcs_on = q.assist_asm != 0 || q.assist_tcs != 0;
    }
    if (c.label.empty()) return std::nullopt;
    const GameSettings s = settings_of(g);
    c.driver = s.steering_assist_kind(), c.wheel_aid = s.active_steering;
    return c;
}

void set_lan_pick(Runtime& rt, const std::string& car) {
    if (car.empty()) lan_picks().erase(&rt);
    else lan_picks()[&rt] = car;
}

bool pick_lan_car(Runtime& rt, const std::string& car, const std::string& maker) {
    if (!rt.current_page() || rt.current_page()->name != "MakerRoot") return false;
    if (const std::shared_ptr<script::Module> m = page_module(rt, "MakerRoot")) m->define("selected_name", text(maker));
    set_lan_pick(rt, car);
    rt.queue_function(page_value(rt, "start_page"), {rt.update_context(), page_value(rt, "CarRoot")}, "the app's car picker");
    return true;
}

void leave_lan_pages(Runtime& rt) {
    GameState& g = rt.game();
    if (rt.network) rt.network->end();
    if (!g.menu_stack.empty() && g.menu_stack.back() == "LANBattle") g.menu_stack.pop_back();
    g.major_menu_page.clear();
    g.next_sequence.clear();
    g.next_menu_project = "arcade";
    rt.finish();
}

Value make_network(Runtime& rt) {
    Runtime* r = &rt;
    auto net = std::make_shared<Object>("MNetwork");
    auto answer = [&net, r](const char* name, std::function<s32(net::Lobby&, Args)> fn, s32 otherwise = 0) {
        net->method(name, [r, fn = std::move(fn), otherwise](Vm&, const Value&, Args a) {
            net::Lobby* l = live(*r);
            return number(l ? fn(*l, a) : otherwise);
        });
    };
    auto tell = [&net, r](const char* name, std::function<void(net::Lobby&, Args)> fn) {
        net->method(name, [r, fn = std::move(fn)](Vm&, const Value&, Args a) {
            if (net::Lobby* l = live(*r)) fn(*l, a);
            return Value{};
        });
    };
    auto peek = [&net, r](const char* name, std::function<Value(const net::Member&)> fn) {
        net->method(name, [r, fn = std::move(fn)](Vm&, const Value&, Args a) {
            net::Lobby* l = live(*r);
            static const net::Member nobody;
            return fn(l ? l->member(arg_int(a, 0)) : nobody);
        });
    };
    // The adaptor: there when there is a network to play on.
    net->method("isNetworkAdaptorAvailable", [r](Vm&, const Value&, Args) { return number(r->network != nullptr); });
    net->method("isInterfaceInitialized", [r](Vm&, const Value&, Args) { return number(r->network != nullptr); });
    net->method("isNetworkInitialized", [r](Vm&, const Value&, Args) { return number(r->network != nullptr); });
    // RTIME (the program's session library) is up once the lobby is: the arcade project then opens the LAN Battle's pages.
    answer("isRTIMEInitialized", [](net::Lobby&, Args) { return 1; });
    for (const char* no : {"isLoggedIn", "isMGCLworking", "imHaveMessage", "isEnableAbort", "getInterfaceCount"})
        net->method(no, [](Vm&, const Value&, Args) { return number(0); });
    for (const char* nothing : {"inLobby", "setLobbyMessageEnable", "disconnect", "logout", "leaveGame", "initCommunicator", "exitCommunicator",
                                "beginEnableAbort", "endEnableAbort", "setCallbackModule", "imClose", "raceMenuStart", "raceMasterNotifySvoGameId"})
        net->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
    net->method("getLastErrorCode", [](Vm&, const Value&, Args) { return text(""); });
    for (const char* end : {"terminateRTIME", "terminateNetwork"})
        net->method(end, [r](Vm&, const Value&, Args) {
            if (r->network) r->network->end();
            return Value{};
        });
    // The game: the connection is made before these pages (the lobby is up), so creating and joining answer at once.
    answer("gcreateLAN", [](net::Lobby& l, Args) { return s32(l.host()); });
    answer("gjoinLAN", [](net::Lobby& l, Args) { return s32(!l.host()); });
    net->method("getAvailableGameIdLAN", [](Vm&, const Value&, Args) { return number(0); });
    answer("amIhost", [](net::Lobby& l, Args) { return s32(l.host()); });
    answer("getMemberNumLAN", [](net::Lobby& l, Args) { return l.count(); });
    net->method("gatherMemberInfoLAN", [](Vm&, const Value&, Args) { return number(1); }); // 0x1F6A40
    net->method("getMemberNameLAN", [r](Vm&, const Value&, Args a) {
        net::Lobby* l = live(*r);
        return text(l ? l->name(arg_int(a, 0)) : std::string());
    });
    answer("raceMenuMyIndex", [](net::Lobby& l, Args) { return l.my_index(); });
    answer("raceMenuGamePlayers", [](net::Lobby& l, Args) { return l.count(); });
    answer("getStyleNumLAN", [](net::Lobby& l, Args a) { return l.style_count(arg_text(a, 0)); });
    net->method("gameTime", [r](Vm&, const Value&, Args) {
        net::Lobby* l = live(*r);
        return number(l ? s32(l->game_time()) : 0);
    });
    tell("setGameClosed", [](net::Lobby& l, Args) { l.change([](net::Member& m) { m.closed = true; }); });

    // The members' records.
    tell("raceMenuNotifyAccountName", [](net::Lobby& l, Args a) { l.change([&](net::Member& m) { m.account = arg_text(a, 0); }); });
    net->method("raceMenuPeekAccountName", [r](Vm&, const Value&, Args a) {
        net::Lobby* l = live(*r);
        return text(l ? l->name(arg_int(a, 0)) : std::string());
    });
    tell("raceMenuNotifyStyle", [](net::Lobby& l, Args a) { l.change([&](net::Member& m) { m.style = arg_text(a, 0); }); });
    peek("raceMenuPeekStyle", [](const net::Member& m) { return text(m.style); });
    tell("raceMenuNotifyTarget", [](net::Lobby& l, Args a) { l.change([&](net::Member& m) { m.target = arg_text(a, 0); }); });
    peek("raceMenuPeekTarget", [](const net::Member& m) { return text(m.target); });
    tell("raceMenuNotifySequence", [](net::Lobby& l, Args a) { l.change([&](net::Member& m) { m.sequence = std::clamp(arg_int(a, 0), 0, 255); }); });
    peek("raceMenuPeekSequence", [](const net::Member& m) { return number(m.sequence); });
    tell("raceMenuNotifyCourse", [](net::Lobby& l, Args a) { l.change([&](net::Member& m) { m.course = arg_text(a, 0); }); });
    peek("raceMenuPeekCourse", [](const net::Member& m) { return text(m.course); });
    tell("raceMenuNotifyClientCount", [](net::Lobby& l, Args a) { l.change([&](net::Member& m) { m.client_count = std::clamp(arg_int(a, 0), 0, 127); }); });
    peek("raceMenuPeekClientCount", [](const net::Member& m) { return number(m.client_count); });
    tell("raceMenuNotifyTime", [](net::Lobby& l, Args a) { l.change([&](net::Member& m) { m.time = u32(arg_int(a, 0)); }); });
    peek("raceMenuPeekTime", [](const net::Member& m) { return number(s32(m.time)); });
    tell("raceMenuNotifyReadyToRace", [](net::Lobby& l, Args a) { l.change([&](net::Member& m) { m.ready_to_race = arg_int(a, 0); }); });
    peek("raceMenuPeekReadyToRace", [](const net::Member& m) { return number(m.ready_to_race); });
    tell("raceMenuNotifyCar", [r](net::Lobby& l, Args a) {
        std::optional<net::CarChoice> c = choice_of_pages(*r);
        if (c && !c->garage && !arg_text(a, 0).empty()) c->label = arg_text(a, 0), c->paint = u8(std::clamp(arg_int(a, 1), 0, 127));
        if (c && l.digest_car) c->digest = l.digest_car(*c);
        l.change([&](net::Member& m) { m.car = arg_text(a, 0), m.paint = u8(arg_int(a, 1)), m.choice = c; });
    });
    peek("raceMenuPeekCar", [](const net::Member& m) { return text(m.car); });
    tell("raceMenuNotifyEntrySetting", [](net::Lobby& l, Args a) {
        const std::vector<Value>* words = a.empty() ? nullptr : a[0].items();
        l.change([&](net::Member& m) {
            for (size_t i = 0; words && i < m.entry.size() && i < words->size(); ++i) m.entry[i] = u32((*words)[i].i);
        });
    });
    tell("raceMasterNotifyRaceSetting", [r](net::Lobby& l, Args) { // (the host's alone: 0x1F7330)
        if (l.host()) l.change([&](net::Member& m) {
            const int ghost = m.rules.ghost_ping_ms;
            m.rules = rules_from_options(r->game()), m.rules.ghost_ping_ms = ghost;
        });
    });
    tell("raceMasterNotifyCourse", [](net::Lobby& l, Args a) {
        if (l.host()) l.change([&](net::Member& m) { m.course = arg_text(a, 0); });
    });
    net->method("raceMasterPeekCourse", [r](Vm&, const Value&, Args) {
        net::Lobby* l = live(*r);
        return text(l ? l->host_member().course : std::string());
    });
    // The conditions.
    answer("raceMenuAllConnected", [](net::Lobby& l, Args) { return s32(l.all_connected()); });
    answer("raceMenuChanged", [](net::Lobby& l, Args) { return s32(l.changed()); });
    tell("raceMenuEnd", [](net::Lobby& l, Args) { l.change([&](net::Member& m) { m.ended = l.round(); }); });
    answer("raceMenuForceEnd", [](net::Lobby& l, Args) { return s32(l.menus_ended()); });
    tell("enterNetSync", [](net::Lobby& l, Args) { l.enter_sync(); });
    answer("netSync", [](net::Lobby& l, Args) { return s32(l.synced()); }, 1);
    net->method("leaveNetSync", [](Vm&, const Value&, Args) { return Value{}; });
    // ❓ The pages take either for the end of the session (a sound five times, then the menus again); the lobby's own page
    // says why instead, so neither is ever answered yes here.
    for (const char* no : {"isClientDisconnected", "isDisconnected"}) net->method(no, [](Vm&, const Value&, Args) { return number(0); });

    net->set("network_enable", number(0)), net->set("error_exit", number(0)), net->set("my_id", number(0)), net->set("my_name", text(""));
    net->set("language", text("")), net->set("pipe", Value{}), net->set("im_pipe", Value{});
    return object_value(net);
}

void adapt_lan_pages(Runtime& rt) {
    if (!rt.network) return;
    Runtime* r = &rt;
    // The course taken, the car: the favorites alone (the original's), or the makers' page first.
    replace(rt, "GameRoot", "isSkipMethod", [r](Vm& vm, const Value& original, const Value&, Args a) {
        if (any_car(*r)) return number(0);
        vm.then_call(original, {a.begin(), a.end()});
        return Value{};
    });
    for (const auto& [module, name] : {std::pair<const char*, const char*>{"MethodRoot", "getCurrentCategory"}, {"MethodRoot", "getCurrentCarList"}})
        replace(rt, module, name, [r, list = std::string(name) == "getCurrentCarList"](Vm& vm, const Value& original, const Value&, Args a) {
            if (list && any_car(*r))
                if (const auto it = lan_picks().find(r); it != lan_picks().end() && !it->second.empty()) return Value::array({text(it->second)});
            return any_car(*r) ? as_split_battle(*r, vm, original, a) : vm.call(original, a);
        });
    replace(rt, "MakerRoot", "onInitialize", [r](Vm& vm, const Value& original, const Value&, Args a) {
        if (!any_car(*r)) {
            vm.then_call(original, {a.begin(), a.end()});
            return Value{};
        }
        Value out = as_split_battle(*r, vm, original, a);
        const bool garage = r->network->host_member().limits.garage_cars;
        for (const char* slot : {"MakerRoot::Tab2p::Slot1", "MakerRoot::Tab2p::Slot2"})
            if (const auto w = page_value(*r, slot).as<LiveWidget>()) w->set("visible", number(garage));
        return out;
    });
    // No way back to the course once it is taken, as from the car page.
    replace(rt, "MakerRoot", "onCancel", [r](Vm& vm, const Value& original, const Value&, Args a) {
        if (!on_lan(*r)) {
            vm.then_call(original, {a.begin(), a.end()});
            return Value{};
        }
        const Value s[1] = {text("disable")};
        r->sound("sound", "play", s);
        return number(2);
    });
    // From a maker's cars back to the makers.
    replace(rt, "CarRoot::CarScroll::SelectBox", "onCancel", [r](Vm& vm, const Value& original, const Value&, Args a) {
        if (!any_car(*r) || a.empty()) {
            vm.then_call(original, {a.begin(), a.end()});
            return Value{};
        }
        const Value s[1] = {text("cancel")};
        r->sound("sound", "play", s);
        vm.then_call(page_value(*r, "start_page"), {a[0], page_value(*r, "MakerRoot")}, [](Vm&, const Value&) { return number(2); });
        return Value{};
    });
    // The driving options' OK: a car the host does not let race is told here, before the player waits with it.
    replace(rt, "SettingPopup::Pane::Button::OK", "onActivate", [r](Vm& vm, const Value& original, const Value&, Args a) {
        if (on_lan(*r) && !a.empty()) {
            vm.call(page_value(*r, "SettingPopup::apply_setting"), {});
            std::optional<net::CarChoice> c = choice_of_pages(*r);
            if (c && r->network->digest_car) c->digest = r->network->digest_car(*c);
            const std::string why = c && r->network->check_car ? r->network->check_car(*c, r->network->host_member().limits) : std::string();
            if (!why.empty()) {
                const Value s[1] = {text("disable")};
                r->sound("sound", "play", s);
                std::string said = why;
                said[0] = char(std::toupper(static_cast<unsigned char>(said[0])));
                vm.then_call(page_value(*r, "openConfirmDialog"), {a[0], number(2), text(said + ".")}, [](Vm&, const Value&) { return number(2); });
                return Value{};
            }
        }
        vm.then_call(original, {a.begin(), a.end()});
        return Value{};
    });
}

} // namespace gt4r::menu
