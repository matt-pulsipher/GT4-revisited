#include "gt4r/menu/earlier_saves.h"

#include "gt4r/menu/game_internal.h"

#include "base/file.h"

#include <cstdio>
#include <map>

namespace gt4r::menu {

using script::Args;
using script::Value;
using script::Vm;
using Kind = save::EarlierGame::Kind;

// The two probes: the credits (Gran Turismo 3's) and the licenses (Gran Turismo 3's on the US program, Prologue's on the
// others; here either).
enum Probe { Credits = 0, Licenses = 1 };

// The transfer's state, kept by the natives (GameNatives::transfer) from one Start on the Status page to the next.
struct TransferState {
    bool chooser = false; // an app answers the choice
    // What was chosen for each probe in this run of the scripts' check (the manager of slot 1 starts one); decided: asked.
    std::optional<save::EarlierGame> pick[2];
    bool decided[2] = {false, false};
    bool cancelled = false;               // the player went back to the game without one
    std::optional<TransferChoice> asking; // for the app to take
    bool waiting = false;                 // the scripts wait for the answer
    Probe waiting_for = Credits;
    std::map<std::string, std::string> disc_text; // the questions' texts as the disc has them (before ours)
};

namespace {

TransferState& transfer(GameNatives& n) {
    if (!n.transfer) n.transfer = std::make_shared<TransferState>(), n.transfer->chooser = n.rt.config().choose_earlier_save;
    return *n.transfer;
}

// The saves a probe can take: the credits Gran Turismo 3's (by the region's folder); the licenses Gran Turismo 3's where the
// program reads it for them (the US program) and Prologue's everywhere (the other regions' path; on the US disc too, the
// user's choice).
TransferChoice gather(GameNatives& n, Probe probe) {
    TransferChoice c;
    c.region = n.rt.region();
    c.licenses = probe == Licenses;
    c.gt3 = !c.licenses || save::licenses_from_gt3(c.region);
    c.prologue = c.licenses;
    const std::string data = n.rt.data_dir();
    if (c.gt3) c.found = save::find_gt3r_games(n.rt.config().gt3r_dir, c.region, &c.looked);
    if (c.prologue)
        for (save::EarlierGame& g : save::find_prologue_games(data)) c.found.push_back(std::move(g));
    if (!data.empty())
        for (save::EarlierGame& g : save::find_card_games(data + "/memorycard", c.region, c.prologue))
            // (Japan's Prologue folder there is our library's first game, listed already.)
            if ((c.gt3 && g.kind == Kind::Gt3) || (g.kind == Kind::Prologue && !g.where.ends_with("BISCPS-15055GAMEDATA")))
                c.found.push_back(std::move(g));
    return c;
}

// The disc's questions with their first line (a card's slot) naming the save chosen instead (the English texts; another
// language keeps its own).
void name_the_save(GameNatives& n, Probe probe, const save::EarlierGame& g) {
    Runtime& rt = n.rt;
    const std::string& language = rt.config().language;
    if (!rt.project() || (language != "US" && language != "GB" && language != "UK")) return;
    TransferState& t = transfer(n);
    std::string name;
    for (char c : g.name) name += c == '%' ? std::string("%%") : std::string(1, c);
    for (const char* key : {probe == Credits ? "exist gt3 data on slot n. inherit license?" : "exist gt4p data on slot n. inherit license?"}) {
        const std::string page = rt.project()->module_name() + "::StatusRoot";
        if (!t.disc_text.count(key)) {
            const std::string* disc = rt.project()->text().find(page, key);
            if (!disc) continue;
            t.disc_text[key] = *disc;
        }
        const std::string& disc = t.disc_text[key];
        const size_t line = disc.find('\n');
        if (line == std::string::npos) continue;
        std::string rest = disc.substr(line);
        // (A Prologue game's licenses: the disc's text says they come from GT3.)
        if (g.kind == Kind::Prologue)
            for (size_t at = 0; (at = rest.find("from GT3", at)) != std::string::npos; at += 16) rest.replace(at, 8, "from GT4 Prologue");
        const std::string what = g.kind == Kind::Prologue ? "GT4 Prologue game save: " : "GT3 game save data: ";
        rt.project()->reword(page, key, what + name + rest);
    }
}

void choose(GameNatives& n, Probe probe, std::optional<save::EarlierGame> g) {
    TransferState& t = transfer(n);
    if (g) name_the_save(n, probe, *g), n.rt.note("transfer: " + g->name + " (" + g->from + ")");
    else n.rt.note("transfer: nothing chosen");
    t.pick[probe] = std::move(g), t.decided[probe] = true;
}

const char* probe_result(GameNatives& n, Probe probe) { return transfer(n).pick[probe] ? "OK" : "FILE_NOT_FOUND"; }

// A console's card (RuntimeConfig::console_card): the slot's card folder holds the program's folder or not.
std::optional<save::EarlierGame> on_console_card(GameNatives& n, int slot, Probe probe) {
    const std::string card = slot == 0 ? n.card().card() : n.card2().card();
    if (card.empty()) return std::nullopt;
    const std::string region = n.rt.region(), folder = probe == Licenses ? save::license_folder(region) : save::gt3_folder(region);
    const std::optional<Bytes> b = gt3r::read_host_file(card + "/" + folder + "/" + folder);
    if (!b) return std::nullopt;
    const bool gt3 = probe == Credits || save::licenses_from_gt3(region);
    return gt3 ? save::read_gt3_data_file(*b, region) : save::read_prologue_data_file(*b, save::prologue_medals_at(folder));
}

// A race record unit passed (0x430C50): a license's best below 13 (a medal or "red"), an event's or a mission's first place.
bool unit_passed(u32 w) {
    const u32 type = w & 15, best = (w >> 4) & 15;
    if (type == 1) return best < 13;
    return (type == 2 || type == 3) && best == 0;
}
// A unit's update (0x430A58): a unit of no kind takes the kind and the result as best and last; one of the same kind
// takes the last, and the best when it is better.
void unit_update(u32& w, u32 type, u32 result) {
    const u32 have = w & 15;
    if (have == 0) {
        w = (w & ~0xFFFu) | type | result << 4 | result << 8;
        return;
    }
    if (have != type || have > 3) return;
    w = (w & ~0xF00u) | result << 8;
    if (result < ((w >> 4) & 15)) w = (w & ~0xF0u) | result << 4;
}

} // namespace

void install_earlier_saves(GameNatives& n, Object& m, const std::string& mode, int slot, bool slot_given) {
    GameNatives* self = &n;
    const bool console = n.rt.config().console_card;
    // The scripts' check makes slot 1's manager first: a new choice.
    if (mode == "MODE_OPERATION" && slot_given && slot == 0) {
        TransferState& t = transfer(n);
        t.pick[0].reset(), t.pick[1].reset(), t.decided[0] = t.decided[1] = false, t.cancelled = false;
    }
    auto probe = [self, slot, console](bool licenses) {
        return [self, slot, console, licenses](Vm& vm, const Value&, Args) -> Value {
            GameNatives& n = *self;
            const Probe probe = licenses ? Licenses : Credits;
            TransferState& t = transfer(n);
            if (console) {
                std::optional<save::EarlierGame> g = on_console_card(n, slot, probe);
                const bool found = g.has_value();
                t.pick[probe] = std::move(g), t.decided[probe] = true;
                return text(found ? "OK" : "FILE_NOT_FOUND");
            }
            // (No card's second slot: the saves are all offered in the first.)
            if (slot != 0) return text("FILE_NOT_FOUND");
            if (t.decided[probe]) return text(probe_result(n, probe));
            // The credits after the licenses: the same Gran Turismo 3 game (the US program reads one folder for both); none when
            // the player went back to the game; asked again after a Prologue game (it has no credits).
            if (probe == Credits && t.decided[Licenses] && (t.cancelled || (t.pick[Licenses] && t.pick[Licenses]->kind == Kind::Gt3))) {
                choose(n, Credits, t.cancelled ? std::nullopt : t.pick[Licenses]);
                return text(probe_result(n, probe));
            }
            TransferChoice c = gather(n, probe);
            if (!t.chooser || !vm.can_wait()) {
                std::optional<save::EarlierGame> first;
                if (!c.found.empty()) first = c.found.front();
                choose(n, probe, std::move(first));
                return text(probe_result(n, probe));
            }
            t.asking = std::move(c), t.waiting = true, t.waiting_for = probe;
            std::weak_ptr<TransferState> held = n.transfer;
            vm.wait([held] {
                const std::shared_ptr<TransferState> s = held.lock();
                return !s || !s->waiting;
            },
                    [self, probe](Vm& v) {
                        v.then_call(Vm::native("transfer", [self, probe](Vm&, const Value&, Args) { return text(probe_result(*self, probe)); }), {});
                    });
            return Value{};
        };
    };
    m.method("isExistGT4pData", probe(true));
    m.method("isExistGT3Data", probe(false));
    // inheritGT4pData (0x1D4698): the licenses' tests opened as passed ("red", 12) where not passed yet.
    m.method("inheritGT4pData", [self](Vm&, const Value&, Args) {
        GameNatives& n = *self;
        const std::string region = n.rt.region();
        const std::optional<save::EarlierGame>& g = transfer(n).pick[Licenses];
        if (!g) return text("FILE_NOT_FOUND");
        const int licenses = save::transferred_licenses(*g, region);
        if (licenses == 0) return text("NO_OPERATION");
        GameState& s = n.state();
        for (int l = 0; l < licenses; ++l)
            for (int test = 1; test <= 16; ++test) {
                char label[16];
                std::snprintf(label, sizeof label, "%s%04d", l == 0 ? "l0b" : "l0a", test);
                const int unit = n.db().race_unit(label);
                if (unit < 0) continue;
                u32& w = s.race_units[size_t(unit)];
                if (!unit_passed(w)) unit_update(w, 1, 12);
            }
        n.rt.note("transfer: licenses " + std::string(licenses == 2 ? "B and A" : "B") + " from " + g->name);
        return text("OK");
    });
    // inheritGT3Data (0x1D49C8): the credits added.
    m.method("inheritGT3Data", [self](Vm&, const Value&, Args) {
        GameNatives& n = *self;
        const std::optional<save::EarlierGame>& g = transfer(n).pick[Credits];
        if (!g) return text("FILE_NOT_FOUND");
        const std::optional<s64> credits = save::transferred_credits(*g, n.rt.region());
        if (!credits) return text("NO_OPERATION");
        n.state().money += *credits;
        n.rt.note("transfer: " + std::to_string(*credits) + " credits from " + g->name);
        return text("OK");
    });
}

void set_transfer_chooser(Runtime& rt, bool on) {
    if (const std::shared_ptr<GameNatives> n = natives_of(rt)) transfer(*n).chooser = on;
}

std::optional<TransferChoice> take_transfer_choice(Runtime& rt) {
    const std::shared_ptr<GameNatives> n = natives_of(rt);
    if (!n || !n->transfer) return std::nullopt;
    return std::exchange(n->transfer->asking, std::nullopt);
}

void answer_transfer_choice(Runtime& rt, std::optional<save::EarlierGame> game) {
    const std::shared_ptr<GameNatives> n = natives_of(rt);
    if (!n || !n->transfer || !n->transfer->waiting) return;
    if (!game) n->transfer->cancelled = true;
    choose(*n, n->transfer->waiting_for, std::move(game));
    n->transfer->waiting = false;
}

} // namespace gt4r::menu
