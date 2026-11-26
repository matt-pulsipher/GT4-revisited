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

// The transfer's state, kept by the natives (GameNatives::transfer) from one Start on the Status page to the next.
struct TransferState {
    bool chooser = false; // an app answers the choice
    // What was chosen for each kind in this run of the scripts' check (the manager of slot 1 starts one); decided: asked.
    std::optional<save::EarlierGame> pick[2];
    bool decided[2] = {false, false};
    std::optional<TransferChoice> asking; // for the app to take
    bool waiting = false;                 // the scripts wait for the answer
    Kind waiting_for = Kind::Gt3;
    std::map<std::string, std::string> disc_text; // the questions' texts as the disc has them (before ours)
};

namespace {

TransferState& transfer(GameNatives& n) {
    if (!n.transfer) n.transfer = std::make_shared<TransferState>(), n.transfer->chooser = n.rt.config().choose_earlier_save;
    return *n.transfer;
}

int index_of(Kind k) { return k == Kind::Gt3 ? 0 : 1; }

// What each probe asks for: Gran Turismo 3's credits; the licenses from Gran Turismo 3 (the US program) or Prologue.
Kind license_kind(const std::string& region) { return save::licenses_from_gt3(region) ? Kind::Gt3 : Kind::Prologue; }

// The saves the transfer can take, by the program's folder for the region.
TransferChoice gather(GameNatives& n, Kind kind) {
    TransferChoice c;
    c.kind = kind, c.region = n.rt.region();
    const std::string data = n.rt.data_dir();
    if (kind == Kind::Gt3) {
        c.found = save::find_gt3r_games(n.rt.config().gt3r_dir, c.region, &c.looked);
        if (!data.empty())
            for (save::EarlierGame& g : save::find_card_games(data + "/memorycard", c.region)) c.found.push_back(std::move(g));
    } else {
        c.found = save::find_prologue_games(data, c.region);
    }
    return c;
}

// The disc's questions with their first line (a card's slot) naming the save chosen instead (the English texts; another
// language keeps its own).
void name_the_save(GameNatives& n, const save::EarlierGame& g) {
    Runtime& rt = n.rt;
    const std::string& language = rt.config().language;
    if (!rt.project() || (language != "US" && language != "GB" && language != "UK")) return;
    TransferState& t = transfer(n);
    std::string name;
    for (char c : g.name) name += c == '%' ? std::string("%%") : std::string(1, c);
    for (const char* key : {"exist gt3 data on slot n. inherit license?", "exist gt4p data on slot n. inherit license?"}) {
        const std::string page = rt.project()->module_name() + "::StatusRoot";
        if (!t.disc_text.count(key)) {
            const std::string* disc = rt.project()->text().find(page, key);
            if (!disc) continue;
            t.disc_text[key] = *disc;
        }
        const std::string& disc = t.disc_text[key];
        const size_t line = disc.find('\n');
        if (line == std::string::npos) continue;
        const std::string what = g.kind == Kind::Prologue ? "GT4 Prologue game save: " : "GT3 game save data: ";
        rt.project()->reword(page, key, what + name + disc.substr(line));
    }
}

void choose(GameNatives& n, Kind kind, std::optional<save::EarlierGame> g) {
    TransferState& t = transfer(n);
    if (g) name_the_save(n, *g), n.rt.note("transfer: " + g->name + " (" + g->from + ")");
    else n.rt.note("transfer: nothing chosen");
    t.pick[index_of(kind)] = std::move(g), t.decided[index_of(kind)] = true;
}

const char* probe_result(GameNatives& n, Kind kind) { return transfer(n).pick[index_of(kind)] ? "OK" : "FILE_NOT_FOUND"; }

// A console's card (RuntimeConfig::console_card): the slot's card folder holds the program's folder or not.
std::optional<save::EarlierGame> on_console_card(GameNatives& n, int slot, Kind kind, bool licenses) {
    const std::string card = slot == 0 ? n.card().card() : n.card2().card();
    if (card.empty()) return std::nullopt;
    const std::string region = n.rt.region(), folder = licenses ? save::license_folder(region) : save::gt3_folder(region);
    const std::optional<Bytes> b = gt3r::read_host_file(card + "/" + folder + "/" + folder);
    if (!b) return std::nullopt;
    return kind == Kind::Gt3 ? save::read_gt3_data_file(*b, region) : save::read_prologue_data_file(*b);
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
        t.pick[0].reset(), t.pick[1].reset(), t.decided[0] = t.decided[1] = false;
    }
    auto probe = [self, slot, console](bool licenses) {
        return [self, slot, console, licenses](Vm& vm, const Value&, Args) -> Value {
            GameNatives& n = *self;
            const Kind kind = licenses ? license_kind(n.rt.region()) : Kind::Gt3;
            TransferState& t = transfer(n);
            if (console) {
                std::optional<save::EarlierGame> g = on_console_card(n, slot, kind, licenses);
                const bool found = g.has_value();
                t.pick[index_of(kind)] = std::move(g), t.decided[index_of(kind)] = true;
                return text(found ? "OK" : "FILE_NOT_FOUND");
            }
            // (No card's second slot: the saves are all offered in the first.)
            if (slot != 0) return text("FILE_NOT_FOUND");
            if (t.decided[index_of(kind)]) return text(probe_result(n, kind));
            TransferChoice c = gather(n, kind);
            if (!t.chooser || !vm.can_wait()) {
                choose(n, kind, c.found.empty() ? std::nullopt : std::optional<save::EarlierGame>(c.found.front()));
                return text(probe_result(n, kind));
            }
            t.asking = std::move(c), t.waiting = true, t.waiting_for = kind;
            std::weak_ptr<TransferState> held = n.transfer;
            vm.wait([held] {
                const std::shared_ptr<TransferState> s = held.lock();
                return !s || !s->waiting;
            },
                    [self, kind](Vm& v) {
                        v.then_call(Vm::native("transfer", [self, kind](Vm&, const Value&, Args) { return text(probe_result(*self, kind)); }), {});
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
        const std::optional<save::EarlierGame>& g = transfer(n).pick[index_of(license_kind(region))];
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
        const std::optional<save::EarlierGame>& g = transfer(n).pick[index_of(Kind::Gt3)];
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
    choose(*n, n->transfer->waiting_for, std::move(game));
    n->transfer->waiting = false;
}

} // namespace gt4r::menu
