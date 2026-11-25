#include "gt4r/menu/save_editor_prologue.h"

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/prologue_license.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace gt4r::menu {

using script::Kind;
using script::Value;

namespace {

constexpr size_t kEntries = 10, kEntryBytes = 0x48, kFirstEntry = 0x10;
u32 get32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
void put32(u8* p, u32 v) { p[0] = u8(v), p[1] = u8(v >> 8), p[2] = u8(v >> 16), p[3] = u8(v >> 24); }

// An entry with nothing in it (0x3FDF80).
void clear_entry(u8* e) {
    std::memset(e, 0, kEntryBytes);
    put32(e + 0x10, kNoTime);
    std::memset(e + 0x24, '-', 8);
}

} // namespace

PrologueSaveEditor::PrologueSaveEditor(FileReader read, RuntimeConfig config) {
    config.data_dir.clear();
    rt_ = std::make_unique<Runtime>(std::move(read), std::move(config));
}
PrologueSaveEditor::~PrologueSaveEditor() = default;

u8* PrologueSaveEditor::unit(int i) { return state_.prologue_license.data() + size_t(i) * kPrologueLicenseUnitBytes; }

void PrologueSaveEditor::start(const GameState& loaded) {
    if (!started_) {
        rt_->boot();
        rt_->start_project("prologue");
        rt_->settle();
        started_ = true;
    }
    state_ = loaded;
    read_tests();
}

void PrologueSaveEditor::set_name(const std::string& v) {
    std::string out;
    for (char c : v)
        if (std::isalpha(static_cast<unsigned char>(c)) && out.size() < kNameLetters) out += char(std::toupper(static_cast<unsigned char>(c)));
    if (!out.empty()) state_.username = out;
}

void PrologueSaveEditor::read_tests() {
    tests_.clear();
    const std::shared_ptr<GameNatives> n = natives_of(*rt_);
    if (!n || state_.prologue_license.size() != kPrologueLicenseUnits * kPrologueLicenseUnitBytes) return;
    Database& d = n->db();
    const Value find = rt_->vm().lookup("prologue::getLicenseID");
    for (int k = 1; k <= int(kPrologueLicenseUnits); ++k) {
        char label[16];
        std::snprintf(label, sizeof label, "l0p%04d", k);
        Test t;
        t.label = label, t.name = "Test " + std::to_string(k);
        if (find.kind == Kind::Function) {
            const Value a[1] = {Value::string(label)};
            const Value id = rt_->vm().call(find, a);
            if (id.kind == Kind::Int && id.i >= 0 && size_t(id.i) < kPrologueLicenseUnits) t.unit = id.i;
        }
        const ByteSpan row = d.row("RACE", label);
        if (t.unit < 0 || row.size() < 0x7C) continue;
        for (size_t m = 0; m < 3; ++m) t.limit[3 - m] = u32(row[0x76 + 2 * m]) * 60'000 + u32(row[0x77 + 2 * m]) * 1'000 + u32(row[0x1A + 2 * m] | row[0x1B + 2 * m] << 8);
        const u8* u = unit(t.unit);
        t.medal = int(std::min<u32>(get32(u + 4), 3)), t.best = get32(u + kFirstEntry + 0x10);
        tests_.push_back(std::move(t));
    }
}

void PrologueSaveEditor::set_medal(size_t test, int medal) {
    if (test >= tests_.size() || medal < 0 || medal > 3) return;
    const Test& t = tests_[test];
    u8* u = unit(t.unit);
    // The times that earn more are taken out, the ones after them move up.
    for (int k = int(kEntries) - 1; k >= 0; --k) {
        u8* e = u + kFirstEntry + size_t(k) * kEntryBytes;
        const u32 time = get32(e + 0x10);
        if (time == kNoTime) continue;
        bool more = medal == 0;
        for (int m = medal + 1; m <= 3 && !more; ++m) more = t.limit[m] && time <= t.limit[m];
        if (!more) continue;
        std::memmove(e, e + kEntryBytes, (kEntries - 1 - size_t(k)) * kEntryBytes);
        clear_entry(u + kFirstEntry + (kEntries - 1) * kEntryBytes);
    }
    put32(u + 4, u32(medal));
    // The most points: of the entries left.
    u32 points = 0;
    for (size_t k = 0; k < kEntries; ++k) points = std::max(points, get32(u + kFirstEntry + k * kEntryBytes + 0x20));
    put32(u + 8, medal ? points : 0);
    const u32 best = get32(u + kFirstEntry + 0x10);
    if (medal && t.limit[medal] && (best == kNoTime || best > t.limit[medal])) {
        // The medal's own time, kept as a run's result is.
        const std::shared_ptr<GameNatives> n = natives_of(*rt_);
        if (n && n->game && prologue_license_load(*n->game, ByteSpan(state_.prologue_license.data(), state_.prologue_license.size()))) {
            static const char* const kNames[4] = {"", "bronze", "silver", "gold"};
            const u32 time = t.limit[medal];
            const Value args[7] = {Value::string(t.label), Value::string(kNames[medal]), Value::integer(s32(time)),
                                   Value::integer(prologue_license_points(n->db().row("RACE", t.label), kNames[medal], time)),
                                   Value::string(state_.username), Value::integer(-1), Value::integer(-1)};
            const Value license = rt_->vm().attribute(rt_->vm().lookup("game"), "license");
            if (license.p) rt_->vm().call(rt_->vm().attribute(license, "recordResult"), args);
            state_.prologue_license = prologue_license_units(*n->game);
        }
    }
    read_tests();
}

} // namespace gt4r::menu
