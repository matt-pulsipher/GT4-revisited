// The Replay Theater's list of saved replays: the card manager in MODE_LOAD_REPLAY as labomode's
// ReplayTheaterRoot reads it (count, getFile(i) and each file's title, car, course, size, load,
// remove), over the replays the race keeps in the data folder's replays/ (race/replay_file.h,
// docs/formats/race_flow.md "Saved replays"). The program lists the card's replay files
// (GT4MC::FileGT4ReplayFull, ...); docs/formats/save.md "The game's other saves".

#include "gt4r/menu/game_internal.h"

#include "gt4r/formats/replay/demonstration.h"
#include "gt4r/race/replay_file.h"
#include "gt4r/save/codec.h"

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

} // namespace

std::vector<std::string> saved_replays(const std::string& data_dir) {
    std::vector<std::string> out;
    if (data_dir.empty()) return out;
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::path(data_dir) / "replays";
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_regular_file() && e.path().extension() == ".gt4rrep") out.push_back(e.path().string());
    // The newest first (the names hold the date and time they were saved).
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        std::error_code e1, e2;
        const auto ta = std::filesystem::last_write_time(a, e1), tb = std::filesystem::last_write_time(b, e2);
        return ta != tb ? ta > tb : a > b;
    });
    return out;
}

void install_replay_list(GameNatives& n, Object& manager) {
    auto files = std::make_shared<std::vector<std::string>>(saved_replays(n.rt.data_dir()));
    GameNatives* np = &n;
    GameState* s = &n.state();
    manager.property("count", [files] { return number(s32(files->size())); });
    manager.method("isNoFile", [files](Vm&, const Value&, Args) { return number(files->empty()); });
    manager.method("isConnect", [](Vm&, const Value&, Args) { return number(1); });
    manager.method("getNewFile", [](Vm&, const Value&, Args) { return Value{}; });
    auto playlist = std::make_shared<Object>("MReplayPlayList");
    playlist->set("list", Value::array({}));
    playlist->method("save", [](Vm&, const Value&, Args) { return text("OK"); });
    manager.set("autoloadPlayList", object_value(playlist));
    manager.method("getFile", [np, s, files](Vm&, const Value&, Args a) -> Value {
        const s32 i = arg_int(a, 0, -1);
        if (i < 0 || size_t(i) >= files->size()) return Value{};
        const std::string path = (*files)[size_t(i)];
        auto f = std::make_shared<Object>("MMemoryCardFile");
        const Bytes bytes = read_file(path);
        std::optional<race::ReplayFile> info;
        try {
            if (race::ReplayFile::is_replay(bytes)) info = race::ReplayFile::decode(bytes);
        } catch (const std::exception&) {
        }
        Database& d = np->db();
        std::string car, course = info ? info->course : std::string();
        if (info && !info->cars.empty())
            if (const db::CarEntry* c = d.car(info->cars[0].label)) car = c->name;
        // (The course's label: the page's script translates it, CourseNameCaption.)
        // ❓ The program's title and date texts are the card file's own; here the file's name and
        // the day it was written.
        char date[32] = "";
        std::error_code ec;
        const auto when = std::filesystem::last_write_time(path, ec);
        if (!ec) {
            const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(when - std::filesystem::file_time_type::clock::now() +
                                                                                                std::chrono::system_clock::now());
            const std::time_t t = std::chrono::system_clock::to_time_t(sys);
            if (const std::tm* tm = std::localtime(&t)) std::strftime(date, sizeof date, "%Y/%m/%d", tm);
        }
        f->set("objectName", text("FileReplayFull"));
        f->set("title", text(std::filesystem::path(path).stem().string()));
        f->set("carName", text(car)), f->set("courseName", text(course)), f->set("time", text("")), f->set("date", text(date));
        f->set("size", number(s32((bytes.size() + 1023) / 1024))), f->set("spectatorMode", number(0)), f->set("dispstep", number(0));
        f->set("portrait", number(0)), f->set("racemode", number(0)), f->set("racesubmode", number(0));
        const bool valid = info.has_value();
        f->method("isInfoValid", [valid](Vm&, const Value&, Args) { return number(valid); });
        f->method("isIconValid", [](Vm&, const Value&, Args) { return number(0); });
        f->method("getIconTexture", [](Vm&, const Value&, Args) { return text(""); });
        f->method("writeInfo", [](Vm&, const Value&, Args) { return text("OK"); });
        // load(manager): the replay chosen for the sequence to show (GameState::replay_file).
        f->method("load", [s, path, valid](Vm&, const Value&, Args) {
            if (!valid) return text("INVALID_FILE");
            s->replay_file = path;
            return text("OK");
        });
        f->method("remove", [files, path](Vm&, const Value&, Args) {
            std::error_code e;
            std::filesystem::remove(path, e);
            files->erase(std::remove(files->begin(), files->end(), path), files->end());
            return text(e ? "DEVICE_ERROR" : "OK");
        });
        return object_value(f);
    });
}

// The Replay Theater's Demo tab (MODE_DEMONSTRATION: the program's DeviceDemo, 0x1D1950): the disc's replays the project's
// configuration names for the region (DemonstrationFiles, main::), each a folder replay/<name>/ of a replay file (scrambled as
// the card's replays, then a ReplayData, docs/formats/replay.md), icon.sys, icon0.ico and a thumbnail. Titles "DEMO_000" on,
// the recorded time, course and first car from the replay, the A / B tag its mode (+0x0C, 1 B-spec), the size the folder's
// KB as on a card. ❓ Playing one is not done (the original refuses them in the emulator, "Data is corrupted.", what a console
// does is not known): load answers INVALID_FILE.
void install_demo_list(GameNatives& n, Object& manager, bool sub) {
    GameNatives* np = &n;
    struct Demo {
        std::string name;
        std::optional<replay::Demonstration> run;
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
    manager.method("getFile", [np, demos](Vm&, const Value&, Args a) -> Value {
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
        f->method("isIconValid", [](Vm&, const Value&, Args) { return number(0); });
        f->method("getIconTexture", [](Vm&, const Value&, Args) { return text(""); });
        f->method("writeInfo", [](Vm&, const Value&, Args) { return text("OK"); });
        f->method("load", [](Vm&, const Value&, Args) { return text("INVALID_FILE"); });
        f->method("remove", [](Vm&, const Value&, Args) { return text("DEVICE_ERROR"); });
        return object_value(f);
    });
}

} // namespace gt4r::menu
