// The Replay Theater's list of saved replays and the Save Replay page: the card manager in MODE_LOAD_REPLAY,
// MODE_SAVE_FULL_REPLAY and MODE_SAVE_BESTLAP_REPLAY as labomode's ReplayTheaterRoot and SaveReplayRoot read it (count,
// getNewFile, getFile(i) and each file's title, car, course, time, size, load, saveNew, writeInfo, remove), over the replays the race
// keeps in the data folder's replays/ (race/replay_file.h, docs/formats/race_flow.md "Saved replays") and Gran Turismo 4's own
// (GT4MC::FileGT4ReplayFull: a folder of replays/ or of the memory card, or the data file alone; docs/formats/save.md "The
// game's other saves").

#include "gt4r/menu/game_internal.h"

#include "gt4r/formats/replay/demonstration.h"
#include "gt4r/race/replay_file.h"
#include "gt4r/save/card_icon.h"
#include "gt4r/save/codec.h"
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
    bool bspec = false, valid = true, license = false;
    std::string mode = "NOTHING";
    int submode = -1;
    int kb = 0;
    std::string icon; // its picture's name in GameState::chunk_pictures
};

std::shared_ptr<Object> replay_object(GameNatives& n, const ReplayRow& r) {
    auto f = std::make_shared<Object>("MMemoryCardFile");
    Database& d = n.db();
    std::string car;
    if (!r.car.empty() && d.car(r.car)) car = n.rt.vm().attribute(make_car_object(n.rt, r.car, 0), "short_name").to_string();
    f->set("objectName", text(r.license ? "FileReplayLicense" : "FileReplayFull"));
    f->set("title", text(r.title));
    // (The course's label: the page's script translates it, CourseNameCaption.)
    f->set("carName", text(car)), f->set("courseName", text(r.course)), f->set("time", text(r.time_ms ? time_text(r.time_ms) : std::string()));
    f->set("date", text(r.date)), f->set("size", number(r.kb)), f->set("spectatorMode", number(r.bspec)), f->set("dispstep", number(0));
    f->set("portrait", number(0)), f->set("racemode", text(r.mode)), f->set("racesubmode", number(r.submode));
    f->method("isInfoValid", [valid = r.valid](Vm&, const Value&, Args) { return number(valid); });
    f->method("isIconValid", [icon = r.icon](Vm&, const Value&, Args) { return number(!icon.empty()); });
    f->method("getIconTexture", [icon = r.icon](Vm&, const Value&, Args) { return text(icon); });
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

void install_replay_list(GameNatives& n, Object& manager) {
    auto files = std::make_shared<std::vector<std::string>>(saved_replays(n.rt.data_dir()));
    GameNatives* np = &n;
    GameState* s = &n.state();
    manager.property("count", [files] { return number(s32(files->size())); });
    manager.method("isNoFile", [files](Vm&, const Value&, Args) { return number(files->empty()); });
    manager.method("isConnect", [](Vm&, const Value&, Args) { return number(1); });
    auto playlist = std::make_shared<Object>("MReplayPlayList");
    playlist->set("list", Value::array({}));
    playlist->method("save", [](Vm&, const Value&, Args) { return text("OK"); });
    manager.set("autoloadPlayList", object_value(playlist));
    // The run to be saved (Save Replay): the list's "Create new file", titled by the page after its mode and number.
    Value fresh;
    if (s->pending_replay) {
        const GameState::PendingReplay& q = *s->pending_replay;
        ReplayRow row;
        row.course = q.course, row.car = q.car, row.date = day_text(std::time(nullptr)), row.time_ms = q.time_ms, row.bspec = q.bspec, row.kb = q.kb;
        row.icon = keep_picture(*s, "replay:new", q.picture);
        row.mode = q.mode, row.submode = q.submode, row.license = license_mode(q.mode);
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
        auto save = [s, files, over, weak, np](Vm&, const Value&, Args) {
            const std::shared_ptr<Object> o = weak.lock();
            // (Over one of Gran Turismo 4's own: that one goes, a new one is written.)
            if (!over->empty() && std::filesystem::path(*over).extension() != ".gt4rrep") remove_replay(*over), over->clear();
            const bool ok = o && s->pending_replay && s->pending_replay->save && s->pending_replay->save(o->get("title").to_string(), *over);
            over->clear();
            *files = saved_replays(np->rt.data_dir());
            return text(ok ? "OK" : "DEVICE_ERROR");
        };
        f->method("saveNew", save), f->method("save", save);
    }
    manager.method("getFile", [np, s, files, over](Vm&, const Value&, Args a) -> Value {
        const s32 i = arg_int(a, 0, -1);
        if (i < 0 || size_t(i) >= files->size()) return Value{};
        const std::string path = (*files)[size_t(i)];
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
        std::weak_ptr<Object> weak = f;
        // Renamed (the theater's pen): the title written back.
        f->method("writeInfo", [weak, path, rebuilt](Vm&, const Value&, Args) {
            const std::shared_ptr<Object> o = weak.lock();
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
            *over = path;
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
            files->erase(std::remove(files->begin(), files->end(), path), files->end());
            return text(gone ? "OK" : "DEVICE_ERROR");
        });
        return object_value(f);
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
    auto playlist = std::make_shared<Object>("MReplayPlayList");
    playlist->set("list", Value::array({}));
    playlist->method("save", [](Vm&, const Value&, Args) { return text("OK"); });
    manager.set("autoloadPlayList", object_value(playlist));
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
        f->method("isIconValid", [icon = d.icon](Vm&, const Value&, Args) { return number(!icon.empty()); });
        f->method("getIconTexture", [icon = d.icon](Vm&, const Value&, Args) { return text(icon); });
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

} // namespace gt4r::menu
