// The Replay Theater's list of saved replays and the Save Replay page: the card manager in MODE_LOAD_REPLAY,
// MODE_SAVE_FULL_REPLAY and MODE_SAVE_BESTLAP_REPLAY as labomode's ReplayTheaterRoot and SaveReplayRoot read it (count,
// getNewFile, getFile(i) and each file's title, car, course, time, size, load, saveNew, writeInfo, remove), over the replays the race
// keeps in the data folder's replays/ (race/replay_file.h, docs/formats/race_flow.md "Saved replays") and Gran Turismo 4's own
// (GT4MC::FileGT4ReplayFull: a folder of replays/ or of the memory card, or the data file alone; docs/formats/save.md "The
// game's other saves").

#include "gt4r/menu/game_internal.h"

#include "base/log.h"

#include "gt4r/formats/replay/demonstration.h"
#include "gt4r/race/replay_file.h"
#include "gt4r/save/best_lap_card.h"
#include "gt4r/save/card_icon.h"
#include "gt4r/save/codec.h"
#include "gt4r/save/replay_list.h"
#include "gt4r/save/replay_particulars.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace gt4r::menu {

using script::Args;
using script::Value;
using script::Vm;

namespace {

Bytes read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), {});
}

bool write_file(const std::filesystem::path& p, const Bytes& b) {
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    return bool(out);
}

// Whether a file is a replay of Gran Turismo 4's own (scrambled with the replays' places).
bool is_own_replay(const std::filesystem::path& p) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(p, ec);
    return !ec && size >= 0x1000 && size < 0x400000 && size % 64 == 0 && save::unscramble(read_file(p), 0, save::FileKind::Replay).has_value();
}

} // namespace

std::vector<std::string> saved_replays(const std::string& data_dir) {
    std::vector<std::string> out;
    if (data_dir.empty()) return out;
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::path(data_dir) / "replays";
    // Gran Turismo 4's own: a save's folder holding the data file named like it, its particulars and its icon.
    auto own_folder = [&](const std::filesystem::path& folder) {
        const std::filesystem::path data = folder / folder.filename();
        std::error_code e1;
        if (std::filesystem::is_regular_file(data, e1) && std::filesystem::is_regular_file(folder / "thumbnail", e1) && is_own_replay(data)) out.push_back(data.string());
    };
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.is_directory()) own_folder(e.path());
        else if (e.is_regular_file() && (e.path().extension() == ".gt4rrep" || is_own_replay(e.path()))) out.push_back(e.path().string());
    }
    for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(data_dir) / "memorycard", ec))
        if (e.is_directory()) own_folder(e.path());
    // The newest first (the names hold the date and time they were saved).
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        std::error_code e1, e2;
        const auto ta = std::filesystem::last_write_time(a, e1), tb = std::filesystem::last_write_time(b, e2);
        return ta != tb ? ta > tb : a > b;
    });
    return out;
}

namespace {

// A replay's file object as labomode's rows read it.
struct ReplayRow {
    std::string title, course, car, date; // the course and the car by their labels
    u32 time_ms = 0;
    bool bspec = false, valid = true, license = false, best_lap = false;
    std::string mode = "NOTHING";
    int submode = -1;
    int kb = 0;
    std::string icon; // its picture's name in GameState::chunk_pictures
};

std::shared_ptr<ListedFile> replay_object(GameNatives& n, const ReplayRow& r) {
    auto f = std::make_shared<ListedFile>("MMemoryCardFile");
    Database& d = n.db();
    std::string car;
    if (!r.car.empty() && d.car(r.car)) car = n.rt.vm().attribute(make_car_object(n.rt, r.car, 0), "short_name").to_string();
    // (The list's icon by the file's class: a best lap's, labomode's bg_best, is neither of the two named.)
    f->set("objectName", text(r.best_lap ? "FileReplayBest" : r.license ? "FileReplayLicense" : "FileReplayFull"));
    f->set("title", text(r.title));
    // (The course's label: the page's script translates it, CourseNameCaption.)
    f->set("carName", text(car)), f->set("courseName", text(r.course)), f->set("time", text(r.time_ms ? time_text(r.time_ms) : std::string()));
    f->set("date", text(r.date)), f->set("size", number(r.kb)), f->set("spectatorMode", number(r.bspec)), f->set("dispstep", number(0));
    f->set("portrait", number(0)), f->set("racemode", text(r.mode)), f->set("racesubmode", number(r.submode));
    f->method("isInfoValid", [valid = r.valid](Vm&, const Value&, Args) { return number(valid); });
    // A file with no icon has none to wait for: its row is done (the list's update_widget reaches its last step and
    // asks no more updates); its texture is nil.
    f->method("isIconValid", [](Vm&, const Value&, Args) { return number(1); });
    f->method("getIconTexture", [icon = r.icon](Vm&, const Value&, Args) { return icon.empty() ? Value{} : text(icon); });
    return f;
}

// (0x1CC1C8: the year of the century, the month, the day.)
std::string day_text(std::time_t t) {
    char date[32] = "";
    if (const std::tm* tm = std::localtime(&t)) std::strftime(date, sizeof date, "%y/%m/%d", tm);
    return date;
}

std::string keep_picture(GameState& s, const std::string& name, const std::vector<u32>& picture) {
    if (picture.size() != size_t(save::kIconPicture) * save::kIconPicture) return {};
    s.chunk_pictures[name] = {save::kIconPicture, save::kIconPicture, picture};
    return name;
}

bool remove_replay(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) return false;
    // (A save's folder of Gran Turismo 4's own goes with its files.)
    const std::filesystem::path folder = path.parent_path();
    if (path.filename() == folder.filename()) {
        for (const char* other : {"icon.sys", "icon0.ico", "thumbnail"}) std::filesystem::remove(folder / other, ec);
        std::filesystem::remove(folder, ec);
    }
    return true;
}

bool license_mode(const std::string& mode) { return mode.rfind("GTMODE_LICENSE", 0) == 0; }

// A replay of the race's own.
ReplayRow rebuilt_row(GameNatives& n, const std::string& path, const Bytes& bytes) {
    ReplayRow row;
    row.kb = int((bytes.size() + 1023) / 1024);
    try {
        const race::ReplayFile f = race::ReplayFile::decode(bytes);
        row.title = f.title, row.course = f.course, row.time_ms = f.time_ms, row.bspec = f.bspec;
        if (!f.cars.empty()) row.car = f.cars[0].label;
        if (!f.mode.empty()) row.mode = f.mode, row.submode = f.submode, row.license = license_mode(f.mode);
        row.icon = keep_picture(n.state(), "replay:" + path, f.picture);
    } catch (const std::exception&) {
        row.valid = false;
    }
    if (row.title.empty()) row.title = std::filesystem::path(path).stem().string();
    return row;
}

// A replay of Gran Turismo 4's own: its particulars, else what the recording itself says.
ReplayRow own_row(GameNatives& n, const std::string& path, const Bytes& bytes) {
    Database& db = n.db();
    ReplayRow row;
    const std::filesystem::path folder = std::filesystem::path(path).parent_path();
    const bool alone = std::filesystem::path(path).filename() != folder.filename();
    row.best_lap = !alone && save::is_best_lap_folder(folder.filename().string());
    int kb = int((bytes.size() + 1023) / 1024), files = 1;
    if (!alone) {
        for (const char* other : {"icon.sys", "icon0.ico", "thumbnail"}) {
            std::error_code ec;
            const auto size = std::filesystem::file_size(folder / other, ec);
            if (!ec) kb += int((size + 1023) / 1024), ++files;
        }
        if (const std::optional<std::vector<u32>> picture = save::icon_picture(read_file(folder / "icon0.ico"))) row.icon = keep_picture(n.state(), "replay:" + path, *picture);
    }
    row.kb = kb + (files + 1) / 2 + 1; // (as the demos' sizes count: the folder's entries, 0x1CC9A8)
    if (const std::optional<save::ReplayParticulars> p = alone ? std::nullopt : save::decode_particulars(read_file(folder / "thumbnail"))) {
        row.title = p->title, row.course = p->course_label, row.time_ms = p->time_ms, row.bspec = p->spectator, row.license = p->license;
        row.mode = save::kReplayModes[p->mode], row.submode = p->submode;
        row.car = db.label(p->car);
        return row;
    }
    row.title = std::filesystem::path(path).filename().string();
    row.valid = false;
    if (const std::optional<Bytes> body = save::unscramble(bytes, 0, save::FileKind::Replay); body && db.program.known()) {
        try {
            if (const std::optional<replay::Demonstration> run = replay::Demonstration::parse(*body, db.program)) {
                row.valid = true, row.time_ms = u32(run->time_ms), row.bspec = run->race_mode == 1, row.course = db.label(run->code_b8);
                if (!run->entries.empty()) row.car = db.label(rd32(run->entries[0].equipment.bytes, 0));
                row.license = run->race_class == "RaceLicense";
            }
        } catch (const std::exception&) {
        }
    }
    return row;
}

} // namespace

// The card manager's files by its mode: the best lap modes list the best lap replays alone (a card's folder <product>B...:
// FileGT4ReplayBest), Load Best Lap Replay those of the race's course and car (the sequence gives the manager the car's and
// the course's codes, 0x11CF30; ❓ the manager's own filter is not read); the others every replay.
std::vector<std::string> replays_for(GameNatives& n, const std::string& mode) {
    std::vector<std::string> all = saved_replays(n.rt.data_dir());
    if (mode != "MODE_SAVE_BESTLAP_REPLAY" && mode != "MODE_LOAD_BESTLAP_REPLAY") return all;
    std::vector<std::string> out;
    const GameState& g = n.state();
    const std::string& course = g.ghost_course.empty() ? g.race.course : g.ghost_course;
    const std::string& car = g.ghost_car.empty() ? g.race.car : g.ghost_car;
    for (const std::string& path : all) {
        const std::filesystem::path folder = std::filesystem::path(path).parent_path();
        if (std::filesystem::path(path).filename() != folder.filename() || !save::is_best_lap_folder(folder.filename().string())) continue;
        if (mode == "MODE_LOAD_BESTLAP_REPLAY") {
            const std::optional<save::ReplayParticulars> p = save::decode_particulars(read_file(folder / "thumbnail"));
            if (!p || p->course_label != course || n.db().label(p->car) != car) continue;
        }
        out.push_back(path);
    }
    return out;
}

namespace {

std::string card_product(Runtime& rt) { return rt.config().product.empty() ? std::string("BASCUS-97328") : rt.config().product; }
save::ReplayList replay_list_of(Runtime& rt) {
    return save::ReplayList(rt.data_dir().empty() ? std::string() : rt.data_dir() + "/memorycard", rt.reader(), card_product(rt));
}

// A replay's entry in the play list: Gran Turismo 4's own in its card folder by the folder's name (save/replay_list.h); ours,
// with no folder (the race's own replay files, a data file alone), the letter 0 and its file's name hashed into the 42 bits
// of a folder's letters (FNV-1a).
u64 replay_entry(Runtime& rt, const std::string& path) {
    const std::filesystem::path p(path);
    const std::string name = p.filename().string();
    if (name == p.parent_path().filename().string())
        if (const std::optional<u64> e = save::folder_entry(name, card_product(rt))) return *e;
    u64 h = 0xCBF29CE484222325ull;
    for (unsigned char c : name) h = (h ^ c) * 0x100000001B3ull;
    return h & ((u64(1) << 42) - 1);
}

// The replay of an entry ("": none on the card).
std::string replay_of(Runtime& rt, u64 entry) {
    for (const std::string& path : saved_replays(rt.data_dir()))
        if (replay_entry(rt, path) == entry) return path;
    return {};
}

// A replay's file object for the list's row `path` (`files`: the manager's list, which remove takes it from; `over`: the
// replay a save writes over).
std::shared_ptr<ListedFile> replay_file_object(GameNatives* np, const std::string& path, std::shared_ptr<std::vector<std::string>> files,
                                               std::shared_ptr<std::string> over) {
    GameState* s = &np->state();
    const Bytes bytes = read_file(path);
    const bool rebuilt = race::ReplayFile::is_replay(bytes);
    ReplayRow row = rebuilt ? rebuilt_row(*np, path, bytes) : own_row(*np, path, bytes);
    std::error_code ec;
    const auto when = std::filesystem::last_write_time(path, ec);
    if (!ec) {
        const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(when - std::filesystem::file_time_type::clock::now() +
                                                                                            std::chrono::system_clock::now());
        row.date = day_text(std::chrono::system_clock::to_time_t(sys));
    }
    auto f = replay_object(*np, row);
    f->entry = replay_entry(np->rt, path);
    std::weak_ptr<ListedFile> weak = f;
    // Renamed (the theater's pen): the title written back.
    f->method("writeInfo", [weak, path, rebuilt](Vm&, const Value&, Args) {
        const std::shared_ptr<ListedFile> o = weak.lock();
        if (!o) return text("INVALID_FILE");
        const std::string title = o->get("title").to_string();
        try {
            if (rebuilt) {
                race::ReplayFile r = race::ReplayFile::decode(read_file(path));
                r.title = title;
                return text(write_file(path, r.encode()) ? "OK" : "DEVICE_ERROR");
            }
            const std::filesystem::path thumbnail = std::filesystem::path(path).parent_path() / "thumbnail";
            const std::optional<Bytes> b = save::rename_particulars(read_file(thumbnail), title);
            if (!b) return text("INVALID_FILE");
            return text(write_file(thumbnail, *b) ? "OK" : "DEVICE_ERROR");
        } catch (const std::exception&) {
            return text("INVALID_FILE");
        }
    });
    f->method("isExist", [over, path](Vm&, const Value&, Args) {
        if (over) *over = path;
        return number(0);
    });
    f->method("readSize", [kb = row.kb](Vm&, const Value&, Args) { return number(kb); });
    // load(manager): the replay chosen for the sequence to show (GameState::replay_file).
    f->method("load", [s, path, valid = row.valid](Vm&, const Value&, Args) {
        if (!valid) return text("INVALID_FILE");
        s->replay_file = path;
        return text("OK");
    });
    f->method("remove", [files, path](Vm&, const Value&, Args) {
        const bool gone = remove_replay(path);
        if (files) files->erase(std::remove(files->begin(), files->end(), path), files->end());
        return text(gone ? "OK" : "DEVICE_ERROR");
    });
    return f;
}

// The list's entries made files: a replay on the card its row, one not there its entry alone.
void fill_play_list(GameNatives* np, PlayListObject& list, const std::vector<u64>& entries) {
    list.files.clear();
    for (const u64 e : entries) {
        const std::string path = replay_of(np->rt, e);
        std::shared_ptr<ListedFile> f = path.empty() ? std::make_shared<ListedFile>("MMemoryCardFile") : replay_file_object(np, path, nullptr, nullptr);
        f->entry = e, f->missing = path.empty();
        list.files.push_back(std::move(f));
    }
}

// Every replay mode's `autoloadPlayList` (the manager's FileGT4ReplayList, 0x17C348): setting its `list` reads the card's
// play list into that list (`autoloadPlayListResult`: OK, FILE_NOT_FOUND when there is none yet, INVALID_FILE); `save` /
// `saveNew` write the list's entries; isExist, readSize, size, format, remove as a card file's (saveVariousDataImpl).
void install_play_list_file(GameNatives& n, Object& manager) {
    GameNatives* np = &n;
    auto shown = std::make_shared<std::shared_ptr<PlayListObject>>();
    auto loaded = std::make_shared<save::Result>(save::Result::FileNotFound);
    manager.property("autoloadPlayListResult", [loaded] { return text(save::result_name(*loaded)); });
    auto file = std::make_shared<Object>("MMemoryCardFile");
    file->property("list", [shown] { return *shown ? object_value(*shown) : Value{}; }, [np, shown, loaded](const Value& v) {
        const auto l = v.as<PlayListObject>();
        if (!l) return;
        *shown = l;
        std::vector<u64> entries;
        *loaded = replay_list_of(np->rt).load(entries);
        if (*loaded == save::Result::Ok) fill_play_list(np, *l, entries);
    });
    file->set("size", number(save::kPlayListKb));
    file->set("objectName", text("FileReplayList"));
    file->method("isExist", [np](Vm&, const Value&, Args) { return number(replay_list_of(np->rt).exists() ? 0 : 1); });
    file->method("readSize", [](Vm&, const Value&, Args) { return number(save::kPlayListKb); });
    file->method("format", [](Vm&, const Value&, Args) { return number(1); });
    file->method("remove", [np](Vm&, const Value&, Args) { return number(replay_list_of(np->rt).remove() == save::Result::Ok); });
    for (const char* nothing : {"dump", "setNewFile"}) file->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
    // save(manager) / saveNew(manager, icon.sys's second line)
    for (const char* write : {"save", "saveNew"}) file->method(write, [np, shown, loaded](Vm&, const Value&, Args a) {
        std::vector<u64> entries;
        if (*shown)
            for (const auto& f : (*shown)->files) entries.push_back(f->entry);
        const save::Result r = replay_list_of(np->rt).save(entries, a.size() > 1 ? a[1].to_string() : std::string("Replay list"));
        if (r == save::Result::Ok) *loaded = r;
        return text(save::result_name(r));
    });
    manager.set("autoloadPlayList", object_value(file));
}

} // namespace

void install_replay_list(GameNatives& n, Object& manager, const std::string& mode) {
    const bool play_list = mode == "MODE_REPLAY_PLAYLIST";
    auto files = std::make_shared<std::vector<std::string>>(play_list ? std::vector<std::string>{} : replays_for(n, mode));
    GameNatives* np = &n;
    GameState* s = &n.state();
    install_play_list_file(n, manager);
    // The PlayList folder (MODE_REPLAY_PLAYLIST): its rows are the list the scripts give the manager (`list`), the entries of
    // replays no longer on the card taken out of it as it is listed (0x1D2EA8 -> 0x1D12D8).
    auto list = std::make_shared<std::shared_ptr<PlayListObject>>();
    auto prune = [list] {
        if (!*list) return;
        auto& f = (*list)->files;
        f.erase(std::remove_if(f.begin(), f.end(), [](const std::shared_ptr<ListedFile>& x) { return x->missing; }), f.end());
    };
    if (play_list) {
        manager.property("list", [list] { return *list ? object_value(*list) : Value{}; }, [list, prune](const Value& v) {
            if (const auto l = v.as<PlayListObject>()) *list = l, prune();
        });
        for (const char* again : {"start", "restart", "clear"}) manager.method(again, [prune](Vm&, const Value&, Args) {
            prune();
            return Value{};
        });
    }
    auto count = [files, list, play_list]() -> s32 { return play_list ? (*list ? s32((*list)->files.size()) : 0) : s32(files->size()); };
    manager.property("count", [count] { return number(count()); });
    manager.method("isNoFile", [count](Vm&, const Value&, Args) { return number(count() == 0); });
    manager.method("isConnect", [](Vm&, const Value&, Args) { return number(1); });
    // The run to be saved (Save Replay): the list's "Create new file", titled by the page after its mode and number.
    Value fresh;
    if (s->pending_replay && !play_list) {
        const GameState::PendingReplay& q = *s->pending_replay;
        ReplayRow row;
        row.course = q.course, row.car = q.car, row.date = day_text(std::time(nullptr)), row.time_ms = q.time_ms, row.bspec = q.bspec, row.kb = q.kb;
        row.icon = keep_picture(*s, "replay:new", q.picture);
        row.mode = q.mode, row.submode = q.submode, row.license = license_mode(q.mode), row.best_lap = q.best_lap;
        auto f = replay_object(n, row);
        f->method("isExist", [](Vm&, const Value&, Args) { return number(1); }); // 1: not there
        f->method("readSize", [](Vm&, const Value&, Args) { return number(0); });
        fresh = object_value(f);
    }
    manager.method("getNewFile", [fresh](Vm&, const Value&, Args) { return fresh; });
    // saveNew(manager, name) of the new file writes it: over the row chosen when the scripts asked that row first (isExist).
    auto over = std::make_shared<std::string>();
    if (fresh.p) {
        const std::shared_ptr<Object> f = fresh.as<Object>();
        std::weak_ptr<Object> weak = f;
        auto save = [s, files, over, weak, np, mode](Vm&, const Value&, Args) {
            const std::shared_ptr<Object> o = weak.lock();
            // (Over one of Gran Turismo 4's own: that one goes, a new one is written.)
            if (!over->empty() && std::filesystem::path(*over).extension() != ".gt4rrep") remove_replay(*over), over->clear();
            const bool ok = o && s->pending_replay && s->pending_replay->save && s->pending_replay->save(o->get("title").to_string(), *over);
            over->clear();
            *files = replays_for(*np, mode);
            return text(ok ? "OK" : "DEVICE_ERROR");
        };
        f->method("saveNew", save), f->method("save", save);
    }
    manager.method("getFile", [np, files, over, list, play_list](Vm&, const Value&, Args a) -> Value {
        const s32 i = arg_int(a, 0, -1);
        if (play_list) return *list && i >= 0 && size_t(i) < (*list)->files.size() ? object_value((*list)->files[size_t(i)]) : Value{};
        if (i < 0 || size_t(i) >= files->size()) return Value{};
        return object_value(replay_file_object(np, (*files)[size_t(i)], files, over));
    });
}

// The Replay Theater's Demo tab (MODE_DEMONSTRATION: the program's DeviceDemo, 0x1D1950): the disc's replays the project's
// configuration names for the region (DemonstrationFiles, main::), each a folder replay/<name>/ of a replay file (scrambled as
// the card's replays, then a ReplayData, docs/formats/replay.md), icon.sys, icon0.ico (the list's picture is its, save/card_icon.h) and a thumbnail. Titles "DEMO_000" on,
// the recorded time, course and first car from the replay, the A / B tag its mode (+0x0C, 1 B-spec), the size the folder's
// KB as on a card. load gives the sequence the file's path on the disc (the original refuses them in the emulator, "Data is
// corrupted."; a console's answer is not known).
void install_demo_list(GameNatives& n, Object& manager, bool sub) {
    GameNatives* np = &n;
    GameState* s = &n.state();
    struct Demo {
        std::string name;
        std::optional<replay::Demonstration> run;
        std::string icon; // the name of its picture (GameState::chunk_pictures), "" none
        int kb = 0;
    };
    auto demos = std::make_shared<std::vector<Demo>>();
    const Value names = sub ? Value{} : n.rt.vm().lookup("DemonstrationFiles");
    const std::vector<Value>* list = names.items();
    Database& db = n.db();
    for (size_t i = 0; list && i < list->size(); ++i) {
        Demo d;
        d.name = (*list)[i].to_string();
        const std::string folder = "replay/" + d.name + "/";
        int kb = 0, files = 0;
        for (const std::string& f : {d.name, std::string("icon.sys"), std::string("icon0.ico"), std::string("thumbnail")})
            if (const std::optional<Bytes> data = n.rt.reader()(folder + f)) kb += int((data->size() + 1023) / 1024), ++files;
        d.kb = files ? kb + (files + 1) / 2 + 1 : -1;
        if (const std::optional<Bytes> data = n.rt.reader()(folder + d.name); data && db.program.known())
            if (const std::optional<Bytes> body = save::unscramble(*data, 0, save::FileKind::Replay)) {
                try {
                    d.run = replay::Demonstration::parse(*body, db.program);
                } catch (const std::exception&) {
                }
            }
        if (const std::optional<Bytes> data = n.rt.reader()(folder + "icon0.ico"))
            if (std::optional<std::vector<u32>> picture = save::icon_picture(*data)) {
                d.icon = folder + "icon0.ico";
                n.state().chunk_pictures[d.icon] = {save::kIconPicture, save::kIconPicture, std::move(*picture)};
            }
        demos->push_back(std::move(d));
    }
    manager.property("count", [demos] { return number(s32(demos->size())); });
    manager.method("isNoFile", [demos](Vm&, const Value&, Args) { return number(demos->empty()); });
    manager.method("isConnect", [](Vm&, const Value&, Args) { return number(1); });
    manager.method("getNewFile", [](Vm&, const Value&, Args) { return Value{}; });
    install_play_list_file(n, manager);
    manager.method("getFile", [np, s, demos](Vm&, const Value&, Args a) -> Value {
        const s32 i = arg_int(a, 0, -1);
        if (i < 0 || size_t(i) >= demos->size()) return Value{};
        const Demo& d = (*demos)[size_t(i)];
        Database& db = np->db();
        std::string car;
        if (d.run && !d.run->entries.empty()) {
            const std::string label = db.label(u64(d.run->entries[0].equipment.bytes[0]) | u64(d.run->entries[0].equipment.bytes[1]) << 8 |
                                               u64(d.run->entries[0].equipment.bytes[2]) << 16 | u64(d.run->entries[0].equipment.bytes[3]) << 24);
            if (!label.empty()) car = np->rt.vm().attribute(make_car_object(np->rt, label, 0), "short_name").to_string();
        }
        // The course's label: the page's script translates it (CourseNameCaption).
        const std::string course = d.run ? db.label(d.run->code_b8) : std::string();
        char title[16];
        std::snprintf(title, sizeof title, "DEMO_%03d", i);
        auto f = std::make_shared<Object>("MMemoryCardFile");
        f->set("objectName", text("FileReplayFull"));
        f->set("title", text(title));
        f->set("carName", text(car)), f->set("courseName", text(course));
        f->set("time", text(d.run ? time_text(u32(d.run->time_ms)) : std::string()));
        f->set("date", text("--/--/--")), f->set("size", number(d.kb));
        f->set("spectatorMode", number(d.run && d.run->race_mode == 1)), f->set("dispstep", number(0));
        f->set("portrait", number(0)), f->set("racemode", number(0)), f->set("racesubmode", number(0));
        const bool valid = d.run.has_value();
        f->method("isInfoValid", [valid](Vm&, const Value&, Args) { return number(valid); });
        f->method("isIconValid", [](Vm&, const Value&, Args) { return number(1); }); // (as a file's above)
        f->method("getIconTexture", [icon = d.icon](Vm&, const Value&, Args) { return icon.empty() ? Value{} : text(icon); });
        f->method("writeInfo", [](Vm&, const Value&, Args) { return text("OK"); });
        f->method("load", [s, valid, path = "replay/" + d.name + "/" + d.name](Vm&, const Value&, Args) {
            if (!valid) return text("INVALID_FILE");
            s->replay_file = path;
            return text("OK");
        });
        f->method("remove", [](Vm&, const Value&, Args) { return text("DEVICE_ERROR"); });
        return object_value(f);
    });
}

// 0x4349A8: the card's play list's next replay (the replays' slot, option +0x1148), as the play list and the title's
// time-out pick it. None or an empty list: 0. Else the demonstration's count (MDemonstration +0x64) names it, from the first
// again past the end, and goes on by one; with replay_play_mode SHUFFLE (option +0x114C) through an order drawn anew each
// time from its seed (+0x68): 0, 1, .. n - 1, then each place from the second swapped with the one at the draw x (its place
// + 1) (0x6012C0; the program's Mersenne Twister seeded with it, a draw a float over 2^32: 0x579548). The list's length,
// with `path` the replay (0x1D1340; "" when it is not on the card).
s32 card_replay_pick(Runtime& rt, std::string& path) {
    path.clear();
    std::vector<u64> entries;
    if (replay_list_of(rt).load(entries) != save::Result::Ok || entries.empty()) return 0;
    GameState& s = rt.game();
    GameState::Demonstration& d = s.demonstration;
    const s32 count = s32(entries.size());
    if (d.card_at >= count) d.card_at = 0;
    s32 at = d.card_at++;
    if (s.option_text("replay_play_mode") == "SHUFFLE") {
        std::vector<s32> order(static_cast<size_t>(count));
        for (s32 i = 0; i < count; ++i) order[size_t(i)] = i;
        save::Mt19937 mt(d.card_seed);
        for (s32 i = 1; i < count; ++i) std::swap(order[size_t(i)], order[mt.below(u32(i + 1))]);
        at = order[size_t(at)];
    }
    path = replay_of(rt, entries[size_t(at)]);
    return count;
}

// The load_replay sequence's play list (0x10CA50 with 0x4349A8 and 0x1D14E0). The sequence reads the card's list each time
// (card_replay_pick): none or empty, nothing plays. A replay not on the card is passed over and counted; as many as the
// list holds end it (the count is the sequence's, kept through the list's replays). The list goes on after each replay
// until the player ends one (the race's end code 2, 0x10CDD4).
bool play_list_next(Runtime& rt, bool start, bool quit) {
    GameState& s = rt.game();
    if (start) s.replay_list = true, s.replay_list_missed = 0;
    if (!s.replay_list) return false;
    if (quit) return s.replay_list = false;
    for (;;) {
        std::string path;
        const s32 count = card_replay_pick(rt, path);
        if (count == 0) return s.replay_list = false;
        if (!path.empty()) {
            s.replay_file = path, s.next_sequence = "replay";
            return true;
        }
        if (++s.replay_list_missed >= count) return s.replay_list = false;
    }
}

} // namespace gt4r::menu
