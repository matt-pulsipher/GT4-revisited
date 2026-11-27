// The save editor's files (save/save_edit.*): one field changed changes that field's bytes and the checks alone, a cancelled
// edit nothing, a write keeps a backup and is refused over a save another part wrote; a card image keeps everything else.

#include "test_framework.h"

#include "gt4r/save/card_image.h"
#include "gt4r/save/codec.h"
#include "gt4r/save/memory_card.h"
#include "gt4r/save/prologue_data.h"
#include "gt4r/save/save_edit.h"

#include "formats/ps2/memcard.h"

#include <ctime>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>

using namespace gt4r;
namespace fs = std::filesystem;

namespace {

save::Codes made_up_codes() {
    return {[](u32 table, const std::string& label) -> u64 {
                const size_t digits = label.find_first_of("0123456789");
                if (digits == std::string::npos) return ~u64(0);
                const std::string kind = label.substr(0, digits);
                if ((table == 0 && kind == "car") || (table == 0x22 && kind == "course")) return u64(table) << 32 | u64(std::stoul(label.substr(digits)));
                return ~u64(0);
            },
            [](u64 code) -> std::string {
                if (code == ~u64(0) || (u32(code) & 0x80000000u)) return {};
                return (u32(code >> 32) == 0 ? "car" : "course") + std::to_string(u32(code));
            }};
}

Bytes file_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), {});
}
void put_file(const fs::path& p, const Bytes& b) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

// A game saved in a folder: three cars (the first driven, the second driven before, the third as bought).
fs::path a_saved_game(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    menu::GameState s;
    s.username = "Driver", s.money = 1'234'567, s.total_prize_money = 98'000;
    for (int i = 0; i < 5; ++i) s.put_day(menu::DayEvent{});
    menu::GarageEntry a, b, c;
    a.car = "car12", a.paint = 3, a.ride_rank = 0, a.record.emplace(), a.record->set_odometer(5'000);
    b.car = "car13", b.paint = 1, b.ride_rank = 1, b.record.emplace(), b.record->set_odometer(7'000);
    c.car = "car14", c.paint = 2;
    s.add_car(a), s.add_car(b), s.add_car(c);
    s.riding_car = 0;
    save::GameSave card((dir / "card").string(), made_up_codes());
    if (card.save_new(s, "Game Data") != save::Result::Ok) return {};
    return dir;
}

Bytes payload_of(const Bytes& file) {
    const std::optional<Bytes> p = save::unscramble(file, 0);
    return p ? Bytes(p->begin(), p->begin() + save::kPayloadSize) : Bytes{};
}

const char* const kName = "BASCUS-97328GAMEDATA";

} // namespace

TEST(save_edit_one_field_one_place) {
    const fs::path dir = a_saved_game("gt4r-test-save-edit");
    const fs::path folder = dir / "card" / kName;
    const std::optional<save::EditPlace> place = save::edit_place((dir / "card").string(), "BASCUS-97328");
    CHECK(place && place->path == folder.string());
    CHECK(save::edit_place(folder.string(), "BASCUS-97328") && save::edit_place((folder / kName).string(), "BASCUS-97328"));
    std::string why;
    CHECK(!save::edit_place((folder / "garage").string(), "BASCUS-97328", &why) && !why.empty());
    const std::optional<save::SaveDocument> opened = save::SaveDocument::open(*place, made_up_codes(), "BASCUS-97328", &why);
    CHECK(opened.has_value());
    const Bytes game = file_bytes(folder / kName), garage = file_bytes(folder / "garage"), before = payload_of(game);
    // Nothing changed: the same bytes.
    CHECK(!opened->changed());
    const std::optional<save::EditFiles> same = opened->files();
    CHECK(same && same->game == game && same->garage == garage);

    // Each change: where its bytes may be in the payload (the CRC at 8 .. 12 aside), and whether the garage file changes.
    struct Case {
        const char* what;
        std::function<void(menu::GameState&)> change;
        size_t from, to;
        int slot = -2; // -2: the garage file as it was; -1: its header; else that slot
    };
    const Case cases[] = {
        {"name", [](menu::GameState& s) { s.username = "Other"; }, 0x10, 0x30},
        {"credits", [](menu::GameState& s) { s.money = 999'999'999; }, 0, 0, -1},
        {"total prize credits", [](menu::GameState& s) { s.total_prize_money = 5; }, 0x50, 0x58},
        {"prize cars", [](menu::GameState& s) { s.total_prize_cars = 9; }, 0x58, 0x5C},
        {"finishes", [](menu::GameState& s) { s.rank_counts[1] = 77; }, 0x60, 0x64},
        {"B-spec book", [](menu::GameState& s) { s.bspec_book[10] = 500; }, 0xA4 + 20, 0xA4 + 22},
        {"a day", [](menu::GameState& s) { s.put_day(menu::DayEvent{}); }, 0x230, 0xB8D8},
        {"a race's result", [](menu::GameState& s) { s.race_units[100] = 0x0000F002; }, 0x13AC0 + 400, 0x13AC0 + 404},
        {"a license ranking", [](menu::GameState& s) { s.license_record[0x1F8 * 3] = 1; }, 0x26BA0 + 0x1F8 * 3, 0x26BA0 + 0x1F8 * 4},
        {"a course open", [](menu::GameState& s) { s.available_courses.insert("course5"); }, 0x381C0, 0x385D0},
        {"a present", [](menu::GameState& s) { s.presents[3] = 1; }, 0x387FC, 0x38800},
        {"the ending", [](menu::GameState& s) { s.options["can_watch_ending"] = s32(1); }, 0x38928 + 0x1168, 0x38928 + 0x116C},
        {"a paint", [](menu::GameState& s) { s.garage[2].paint = 5; }, 0xB8E0 + 0x40, 0xB8E0 + 0x60},
        {"a car added", [](menu::GameState& s) { menu::GarageEntry e; e.car = "car99"; s.add_car(e); }, 0xB8E0 + 0x60, 0xB8E0 + 0x80},
        {"the driven car's record", [](menu::GameState& s) { s.garage[0].record->set_odometer(6'000); }, 0x135FC, 0x13AB4},
        {"another car's record", [](menu::GameState& s) { s.garage[1].record->set_odometer(6'000); }, 0, 0, 1},
        {"a record made", [](menu::GameState& s) { s.garage[2].record.emplace(), s.garage[2].record->set_odometer(42); }, 0xB8E0 + 0x40, 0xB8E0 + 0x60, 2},
    };
    for (const Case& c : cases) {
        save::SaveDocument d = *opened;
        c.change(d.state);
        CHECK(d.changed());
        const std::optional<save::EditFiles> out = d.files(&why);
        if (!out) std::printf("  %s: %s\n", c.what, why.c_str());
        CHECK(out.has_value());
        if (!out) continue;
        const Bytes after = payload_of(out->game);
        CHECK(after.size() == before.size() && save::valid_game(after));
        size_t outside = 0, inside = 0;
        for (size_t i = 0; i < after.size(); ++i)
            if (after[i] != before[i] && !(i >= 8 && i < 12)) (i >= c.from && i < c.to ? inside : outside) += 1;
        if (outside || (c.to > c.from) != (inside > 0)) std::printf("  %s: %zu bytes where they belong, %zu elsewhere\n", c.what, inside, outside);
        CHECK(outside == 0 && (c.to > c.from) == (inside > 0));
        if (c.to == c.from) CHECK(out->game == game); // (the game data file itself not written again)
        size_t wrong = 0, right = 0;
        for (size_t i = 0; i < garage.size(); ++i) {
            if (out->garage[i] == garage[i]) continue;
            const int at = i < 0x40 ? -1 : int((i - 0x40) / save::kSlotSize);
            (at == c.slot ? right : wrong) += 1;
        }
        if (wrong || (c.slot != -2) != (right > 0)) std::printf("  %s: the garage file: %zu bytes where they belong, %zu elsewhere\n", c.what, right, wrong);
        CHECK(wrong == 0 && (c.slot != -2) == (right > 0));
    }

    // Another car driven: the one driven before goes to its slot, the new one's record to the game data.
    {
        save::SaveDocument d = *opened;
        d.state.riding_car = 1;
        const std::optional<save::EditFiles> out = d.files(&why);
        CHECK(out.has_value());
        const std::optional<save::SaveDocument> back = save::SaveDocument::from_files(*out, made_up_codes(), "BASCUS-97328");
        CHECK(back && back->state.riding_car == 1 && back->state.garage[0].record && back->state.garage[0].record->odometer() == 5'000 &&
              back->state.garage[1].record && back->state.garage[1].record->odometer() == 7'000);
    }
}

TEST(save_edit_write_backup_refusals) {
    const fs::path dir = a_saved_game("gt4r-test-save-edit2");
    const fs::path folder = dir / "card" / kName, backups = dir / "backups";
    const save::EditPlace place{save::EditPlace::Kind::Folder, folder.string()};
    const Bytes game = file_bytes(folder / kName), garage = file_bytes(folder / "garage");
    std::string why;
    std::optional<save::SaveDocument> d = save::SaveDocument::open(place, made_up_codes(), "BASCUS-97328", &why);
    CHECK(d.has_value());
    // A cancelled edit: nothing on disk changes.
    d->state.money = 5;
    CHECK(d->reload() && d->state.money == 1'234'567 && file_bytes(folder / kName) == game && file_bytes(folder / "garage") == garage && !fs::exists(backups));
    // Written, with a backup of the files as they were; once a document.
    d->state.money = 42, d->state.username = "Other";
    save::SaveDocument::Written w = d->write(backups.string());
    CHECK(w.ok && !w.backup.empty());
    CHECK(file_bytes(fs::path(w.backup) / kName / kName) == game && file_bytes(fs::path(w.backup) / kName / "garage") == garage);
    CHECK(!d->changed() && d->state.money == 42);
    d->state.money = 43;
    w = d->write(backups.string());
    CHECK(w.ok && std::distance(fs::directory_iterator(backups), fs::directory_iterator()) == 1);
    // It loads as the program's load has it, and saves on from there.
    {
        save::GameSave card((dir / "card").string(), made_up_codes());
        menu::GameState s;
        CHECK(card.load(s) == save::Result::Ok && s.money == 43 && s.username == "Other" && s.garage_count() == 3);
        CHECK(card.save(s) == save::Result::Ok);
    }
    // That save was another part's: the document opened before refuses to write over it.
    const Bytes theirs = file_bytes(folder / kName);
    d->state.money = 44;
    w = d->write(backups.string());
    CHECK(!w.ok && !w.message.empty() && file_bytes(folder / kName) == theirs);
    // The backup opens as a save of its own.
    CHECK(save::SaveDocument::open(*save::edit_place(w.backup, "BASCUS-97328"), made_up_codes(), "BASCUS-97328").has_value());
    // No backup can be made: nothing is written.
    put_file(dir / "file", {1});
    std::optional<save::SaveDocument> e = save::SaveDocument::open(place, made_up_codes(), "BASCUS-97328", &why);
    CHECK(e.has_value());
    e->state.money = 45;
    w = e->write((dir / "file" / "backups").string());
    CHECK(!w.ok && file_bytes(folder / kName) == theirs);
    // Damaged files are refused, each with why.
    Bytes broken = theirs;
    broken[1000] ^= 1;
    put_file(folder / kName, broken);
    CHECK(!save::SaveDocument::open(place, made_up_codes(), "BASCUS-97328", &why) && !why.empty());
    put_file(folder / kName, theirs);
    const Bytes good = file_bytes(folder / "garage");
    put_file(folder / "garage", garage.empty() ? Bytes{} : Bytes(0x40, 7));
    CHECK(!save::SaveDocument::open(place, made_up_codes(), "BASCUS-97328", &why) && !why.empty());
    put_file(folder / "garage", good);
    CHECK(save::SaveDocument::open(place, made_up_codes(), "BASCUS-97328", &why).has_value());
}

// The engine's make_card with a time for the card's root entries (GT3R #502), where the engine has it (found through the
// directories' namespace); none without it.
template <class Dirs> auto card_at(const Dirs& dirs, s64 t, int) -> std::optional<decltype(make_card(dirs, t))> { return make_card(dirs, t); }
template <class Dirs> std::optional<Bytes> card_at(const Dirs&, s64, long) { return std::nullopt; }

TEST(save_edit_card_image) {
    const fs::path dir = a_saved_game("gt4r-test-save-edit3");
    // The card's root entries ("." and "..") take the clock's second when a card is made (the engine's make_card) and
    // replace_in_card keeps the image's. With a time for them, the image changed and the new card are made at one time;
    // without, the export's image and the new card are made again until they fall within one second, so the times agree.
    Bytes image, other, fresh;
    std::vector<gt3r::ps2::McDir> dirs;
    std::string why;
    std::optional<Bytes> patched;
    for (int tries = 0; tries < 5; ++tries) {
        const std::time_t before = std::time(nullptr);
        image = save::export_card((dir / "card").string(), std::nullopt);
        if (image.empty()) break;
        // The game data file replaced by other bytes: the image a new card with them would be, page for page, its spare bytes too.
        dirs = gt3r::ps2::read_card(image);
        const std::optional<Bytes> at_once = card_at(dirs, 1792250709, 0);
        for (gt3r::ps2::McFile& f : dirs[0].files)
            if (f.name == kName) {
                for (size_t i = 0; i < f.data.size(); i += 777) f.data[i] ^= 0x5A;
                other = f.data;
            }
        if (at_once) {
            std::printf("  the cards made at one time\n");
            fresh = *card_at(dirs, 1792250709, 0);
            patched = save::replace_in_card(*at_once, kName, kName, other, &why);
            break;
        }
        fresh = gt3r::ps2::make_card(dirs);
        patched = save::replace_in_card(image, kName, kName, other, &why);
        if (std::time(nullptr) == before) break;
    }
    CHECK(!image.empty() && dirs.size() == 1);
    CHECK(patched && patched->size() == fresh.size());
    size_t differ = 0;
    for (size_t i = 0; i < fresh.size(); ++i) differ += (*patched)[i] != fresh[i];
    if (differ) std::printf("  %zu bytes differ from a new card's\n", differ);
    CHECK(differ == 0);
    CHECK(!save::replace_in_card(image, kName, "no such", other) && !save::replace_in_card(image, "no such", kName, other) && !save::replace_in_card(image, kName, kName, Bytes(5)));
    // Edited on the image: what else is on the card stays, and the game reads from it.
    const fs::path card = dir / "Card 1.ps2";
    put_file(card, image);
    const std::optional<save::EditPlace> place = save::edit_place(card.string(), "BASCUS-97328", &why);
    CHECK(place && place->kind == save::EditPlace::Kind::Image);
    std::optional<save::SaveDocument> d = save::SaveDocument::open(*place, made_up_codes(), "BASCUS-97328", &why);
    CHECK(d.has_value());
    d->state.money = 777, d->state.username = "OnCard";
    const save::SaveDocument::Written w = d->write((dir / "backups").string());
    CHECK(w.ok && file_bytes(w.backup) == image);
    const fs::path out = dir / "out";
    CHECK(save::import_card(file_bytes(card), out.string()).ok);
    save::GameSave loaded(out.string(), made_up_codes());
    menu::GameState s;
    CHECK(loaded.load(s) == save::Result::Ok && s.money == 777 && s.username == "OnCard");
    CHECK(file_bytes(out / kName / "garage").size() == 1'280'064);
}

TEST(save_edit_prologue_files) {
    menu::GameState s;
    s.username = "ABC";
    save::EditFiles files;
    files.game = save::write_prologue(s);
    std::optional<save::SaveDocument> d = save::SaveDocument::from_files(files, {}, "BISCPS-15055");
    CHECK(d && d->prologue() && d->state.username == "ABC" && !d->changed());
    d->state.username = "XYZ";
    d->state.prologue_license[0x2E0 * 4 + 4] = 3;
    const std::optional<save::EditFiles> out = d->files();
    CHECK(out && save::valid_prologue(out->game));
    size_t differ = 0;
    for (size_t i = 0; i < out->game.size(); ++i)
        if (out->game[i] != files.game[i] && !(i >= 8 && i < 12)) {
            ++differ;
            CHECK((i >= 0x20 && i < 0x30) || i == 0x30 + 0x2E0 * 4 + 4);
        }
    CHECK(differ == 4);
}
