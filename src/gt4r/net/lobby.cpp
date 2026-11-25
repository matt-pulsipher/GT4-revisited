#include "gt4r/net/lobby.h"

#include "gt4r/net/identity.h"

#include "base/log.h"

#include <algorithm>
#include <cmath>

namespace gt4r::net {

namespace {

constexpr u8 kRecordVersion = 1;
constexpr size_t kMaxRecord = 1024; // (Session::set_car_info's limit)
const Member kNobody{};

} // namespace

bool Member::operator==(const Member& o) const {
    return encode_member(*this, true) == encode_member(o, true);
}

Bytes encode_member(const Member& m, bool host) {
    gt3r::net::Writer w;
    w.u8_(kRecordVersion);
    for (const std::string* s : {&m.account, &m.style, &m.target, &m.course, &m.car}) w.str(s->substr(0, 0x3F));
    w.u8_(m.paint);
    w.u8_(u8(m.sequence)), w.u8_(u8(m.client_count)), w.u32_(m.time), w.u8_(u8(m.ready_to_race));
    for (u32 v : m.entry) w.u32_(v);
    w.u16_(m.sync), w.u16_(m.ended);
    w.u8_(u8((host ? 1 : 0) | (m.choice ? 2 : 0) | (m.closed ? 4 : 0)));
    if (host) {
        w.u16_(m.round);
        w.u8_(u8(m.rules.laps)), w.u8_(m.rules.tire_wear ? 1 : 0), w.u8_(u8(m.rules.handicap)), w.u8_(u8(m.rules.boost)), w.u8_(u8(m.rules.penalty)),
            w.u8_(u8(m.rules.heavy)), w.u16_(u16(m.rules.ghost_ping_ms));
        w.u16_(m.limits.max_power), w.u8_(u8((m.limits.garage_cars ? 1 : 0) | (m.limits.any_car ? 2 : 0)));
    }
    if (m.choice) write_choice(w, *m.choice);
    return w.data;
}

std::optional<Member> decode_member(ByteSpan data) {
    gt3r::net::Reader r(data);
    if (r.u8_() != kRecordVersion) return std::nullopt;
    Member m;
    for (std::string* s : {&m.account, &m.style, &m.target, &m.course, &m.car}) {
        *s = r.str();
        if (s->size() >= 0x40) return std::nullopt;
    }
    m.paint = r.u8_();
    m.sequence = r.u8_(), m.client_count = r.u8_(), m.time = r.u32_(), m.ready_to_race = r.u8_();
    for (u32& v : m.entry) v = r.u32_();
    m.sync = r.u16_(), m.ended = r.u16_();
    const u8 flags = r.u8_();
    m.closed = flags & 4;
    if (flags & 1) {
        m.round = r.u16_();
        m.rules.laps = std::clamp<int>(r.u8_(), 1, 99), m.rules.tire_wear = r.u8_() != 0;
        m.rules.handicap = gt3r::s8(r.u8_()), m.rules.boost = r.u8_(), m.rules.penalty = r.u8_(), m.rules.heavy = r.u8_(), m.rules.ghost_ping_ms = r.u16_();
        m.limits.max_power = r.u16_();
        const u8 l = r.u8_();
        m.limits.garage_cars = l & 1, m.limits.any_car = l & 2;
    }
    if (flags & 2) {
        CarChoice c;
        if (!read_choice(r, c)) return std::nullopt;
        m.choice = c;
    }
    if (!r.ok()) return std::nullopt;
    return m;
}

Lobby::Lobby() : Lobby(gt3r::net::online()) {}
Lobby::Lobby(gt3r::net::Session& session) : session_(session) {}

Lobby& lobby() {
    static Lobby l;
    return l;
}

void Lobby::begin(const std::string& account) {
    mine_ = Member{};
    mine_.account = account;
    members_.clear(), seen_.clear(), problems_.clear();
    next_round_ = 0;
    wanted_.reset(), race_.reset();
    error_.clear();
    closed_ = alone_ = racing_ = loading_ = false;
    begun_ = true;
    ++version_;
    publish();
}

void Lobby::end() {
    if (session_.active()) session_.close();
    members_.clear(), seen_.clear();
    wanted_.reset(), race_.reset();
    begun_ = closed_ = alone_ = racing_ = loading_ = false;
    mine_ = Member{};
}

void Lobby::publish() {
    if (!active() || !begun_) return;
    const Bytes b = encode_member(mine_, session_.is_host());
    if (b.size() > kMaxRecord) return;
    const u8 id = u8(session_.my_id());
    if (const auto it = seen_.find(id); it != seen_.end() && it->second == b) return;
    seen_[id] = b;
    members_[id] = mine_;
    session_.set_car_info(b);
    ++version_;
}

void Lobby::change(const std::function<void(Member&)>& f) {
    f(mine_);
    publish();
}

void Lobby::poll() {
    if (!begun_ || racing_) return;
    if (session_.active()) session_.poll();
    while (const std::optional<gt3r::net::Event> e = session_.next_event()) on_event(*e);
    if (!session_.active()) {
        if (!closed_) closed_ = true;
        return;
    }
    publish(); // (a guest's record goes out once the host accepted it)
    refresh();
    if (!host()) return;
    // The host: no more players than the pages hold, none while the menus of this round are over.
    while (count() > kMaxLobbyPlayers) {
        const u8 id = session_.players().back().id;
        session_.kick(id, "the session is full");
        if (session_.players().size() && session_.players().back().id == id) break; // (goes with its connection)
    }
    if (count() >= 2) alone_ = false;
    else if (mine_.sequence >= 3 && session_.in_lobby() && mine_.sync >= 1) alone_ = true;
    if (wanted_ && !loading_ && session_.in_lobby() && waiting_for().empty()) {
        gt3r::net::Settings s = session_.settings();
        s.course = *wanted_, s.laps = mine_.rules.laps, s.tire_wear = mine_.rules.tire_wear, s.ghost_ping_ms = u16(mine_.rules.ghost_ping_ms);
        session_.set_settings(s);
        session_.load();
        loading_ = true;
    }
}

void Lobby::refresh() {
    for (const gt3r::net::Player& p : session_.players()) {
        if (int(p.id) == session_.my_id()) continue;
        const Bytes* info = session_.car_info(p.id);
        if (!info || seen_[p.id] == *info) continue;
        seen_[p.id] = *info;
        if (std::optional<Member> m = decode_member(*info)) members_[p.id] = std::move(*m);
        else members_.erase(p.id);
        ++version_;
    }
}

void Lobby::on_event(const gt3r::net::Event& e) {
    using K = gt3r::net::Event::Kind;
    switch (e.kind) {
    case K::Joined:
        ++version_;
        publish();
        break;
    case K::Rejected:
    case K::Disconnected:
        error_ = refusal_text(e.text.empty() ? std::string("The connection was lost.") : e.text, session_.update_required().has_value());
        closed_ = true;
        break;
    case K::Roster: {
        // Players gone: their records too.
        for (auto it = members_.begin(); it != members_.end();) {
            if (session_.player(it->first)) ++it;
            else seen_.erase(it->first), it = members_.erase(it);
        }
        ++version_;
        break;
    }
    case K::Queue: // the relay has no free links: still in the lobby
        error_ = refusal_text(e.text, session_.update_required().has_value());
        loading_ = false;
        break;
    case K::Load: {
        refresh();
        OnlineRace race;
        race.course = e.course;
        race.rules = host_member().rules;
        if (e.laps > 0) race.rules.laps = e.laps;
        for (u8 id : e.grid) {
            const auto it = members_.find(id);
            if (it == members_.end() || !it->second.choice) {
                LOG_WARN("Online: player %u has no car", unsigned(id));
                continue;
            }
            const CarChoice& c = *it->second.choice;
            RaceCar car;
            car.id = id;
            const gt3r::net::Player* p = session_.player(id);
            car.player = !it->second.account.empty() ? it->second.account : p ? p->name : std::string();
            car.label = c.label, car.paint = c.paint;
            if (c.garage) car.condition = c.condition, car.equipment = c.equipment;
            if (build_car) car.equipment = build_car(c, race.course);
            car.automatic = c.automatic, car.asm_on = c.asm_on, car.tcs_on = c.tcs_on, car.wheel_aid = c.wheel_aid, car.driver = c.driver;
            car.digest = c.digest;
            if (int(id) == session_.my_id()) race.ours = int(race.cars.size());
            race.cars.push_back(std::move(car));
        }
        race_ = std::move(race);
        break;
    }
    case K::Lobby: // (the host went back before our race began)
        loading_ = false;
        race_.reset();
        break;
    default: break;
    }
}

std::string Lobby::take_error() { return std::exchange(error_, {}); }

int Lobby::my_index() const {
    const auto& p = session_.players();
    for (size_t i = 0; i < p.size(); ++i)
        if (int(p[i].id) == session_.my_id()) return int(i);
    return 0;
}

u8 Lobby::id_of(int index) const {
    const auto& p = session_.players();
    return index >= 0 && size_t(index) < p.size() ? p[size_t(index)].id : u8(0xFF);
}

const Member& Lobby::member(int index) const {
    const auto it = members_.find(id_of(index));
    return it == members_.end() ? kNobody : it->second;
}

std::string Lobby::name(int index) const {
    const Member& m = member(index);
    if (!m.account.empty()) return m.account;
    const gt3r::net::Player* p = session_.player(id_of(index));
    return p ? p->name : std::string();
}

bool Lobby::changed() { return std::exchange(version_seen_, version_) != version_; }

u32 Lobby::game_time() const { return active() ? u32(std::llround(std::fmod(session_.now() + session_.clock_offset_ms(), 4294967296.0))) : 0; }

bool Lobby::all_connected() const {
    u32 time = 0;
    int wanted = 0;
    for (int i = 0; i < count(); ++i) {
        const u8 id = id_of(i);
        if (!members_.count(id)) return false; // (0x1F74B4: a record not there yet)
        time = std::max(time, member(i).time), wanted = std::max(wanted, member(i).client_count);
    }
    return wanted > 0 && wanted <= count() && s32(game_time() - time) > 0;
}

int Lobby::style_count(const std::string& style) const {
    int n = 0;
    for (int i = 0; i < count(); ++i) n += member(i).style == style;
    return n;
}

bool Lobby::menus_ended() const {
    for (int i = 0; i < count(); ++i)
        if (member(i).ended && member(i).ended == round()) return true;
    return false;
}

void Lobby::enter_sync() {
    wanted_.reset();
    loading_ = false;
    const u16 entered = std::max(round(), next_round_);
    change([entered](Member& m) {
        m.sync = entered;
        m.sequence = std::min(m.sequence, 2);
        m.ready_to_race = 0;
        m.choice.reset();
    });
}

bool Lobby::synced() const {
    if (!active()) return true; // (0x1F75D4: an error answers yes)
    for (int i = 0; i < count(); ++i) {
        if (!members_.count(id_of(i))) return false;
        // (a player who is still on the way in, its menus not begun, holds nobody)
        if (member(i).sync < mine_.sync && member(i).sync > 0) return false;
    }
    return true;
}

bool Lobby::heard(int index) const { return members_.count(id_of(index)) != 0; }

std::string Lobby::car_problem(int index) {
    const Member& m = member(index);
    if (!m.choice || !check_car) return {};
    if (!(problems_for_ == host_member().limits)) problems_.clear(), problems_for_ = host_member().limits;
    gt3r::net::Writer w;
    write_choice(w, *m.choice);
    const auto it = problems_.find(w.data);
    if (it != problems_.end()) return it->second;
    if (problems_.size() > 64) problems_.clear();
    return problems_[w.data] = check_car(*m.choice, host_member().limits);
}

std::string Lobby::waiting_for() {
    if (!active()) return "the connection";
    if (count() < 2) return "another player";
    for (int i = 0; i < count(); ++i) {
        const Member& m = member(i);
        if (m.sequence < 8 || !m.choice || m.sync != round()) return name(i);
        if (const std::string why = car_problem(i); !why.empty()) return name(i) + ": " + why;
    }
    return {};
}

std::optional<OnlineRace> Lobby::take_race() {
    if (!race_) return std::nullopt;
    racing_ = true;
    next_round_ = u16(round() + 1);
    return std::exchange(race_, std::nullopt);
}

void Lobby::end_race() {
    racing_ = false, loading_ = false;
    wanted_.reset(), race_.reset();
    if (!session_.active()) {
        closed_ = true;
        return;
    }
    if (host()) {
        if (!session_.in_lobby()) session_.lobby();
        change([](Member& m) {
            if (++m.round == 0) m.round = 1;
        });
    }
}

} // namespace gt4r::net
