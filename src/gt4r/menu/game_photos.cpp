// The photos on the memory card as the scripts see them: the card manager's photo modes (the
// shoot's saving, Photo Lab's lists: GT4MC's device and files) and the play lists made of them.
// The files themselves are save/photo_file.*; docs/formats/photo.md.

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/photo_natives.h"
#include "gt4r/save/card_icon.h"
#include "gt4r/save/photo_file.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace gt4r::menu {

using script::Args;
using script::Value;
using script::Vm;

namespace {

// A photo of the card as a file object: what it stands for, and the picture its load read.
struct PhotoFileObject : ListedFile {
    PhotoFileObject() : ListedFile("MMemoryCardFile") {}
    save::PhotoEntry photo;
    int slot = 0;
    Bytes jpeg; // after load
};

// The photos' folder: the game's save files, one place whatever slot a page names.
std::string card_dir(Runtime& rt, int) { return rt.data_dir().empty() ? std::string() : rt.data_dir() + "/memorycard"; }

// (the disc's product: "BESCES-51719P..." on Europe's card, "BISCPS-17001P..." on Japan's)
save::PhotoCard photo_card(Runtime& rt, int slot) {
    const std::string& product = rt.config().product;
    return product.empty() ? save::PhotoCard(card_dir(rt, slot), rt.reader()) : save::PhotoCard(card_dir(rt, slot), rt.reader(), product);
}

constexpr s32 kFreeKb = 7999; // (always room: the save files are not a card's; 8,000 is the scripts' unformatted card)

std::vector<u16> small_picture(const PhotoPicture& p) {
    std::vector<u8> rgb(size_t(p.width) * size_t(p.height) * 3);
    for (size_t i = 0; i < p.rgba.size() && i * 3 + 2 < rgb.size(); ++i)
        for (int k = 0; k < 3; ++k) rgb[i * 3 + size_t(k)] = u8(p.rgba[i] >> (8 * k));
    return photo::icon_pixels(rgb, p.width, p.height);
}

Value file_value(GameNatives& n, const save::PhotoEntry& e, int slot);

std::shared_ptr<PhotoFileObject> make_file(GameNatives& n, const save::PhotoEntry& e, int slot) {
    auto f = std::make_shared<PhotoFileObject>();
    f->photo = e, f->slot = slot;
    // (its entry in a play list: the folder's number, its letter on top)
    if (const std::optional<u64> id = save::folder_id(e.folder.substr(e.folder.size() - 7))) f->entry = *id | u64(u8(e.kind)) << 56;
    Runtime* rt = &n.rt;
    std::weak_ptr<PhotoFileObject> weak = f;
    f->set("objectName", text("FilePhoto"));
    f->property("title", [weak] {
        const auto p = weak.lock();
        return text(p && p->photo.info ? p->photo.info->name : std::string());
    }, [weak](const Value& v) {
        const auto p = weak.lock();
        if (p && p->photo.info) p->photo.info->name = v.to_string().substr(0, 63);
    });
    f->set("carName", text(e.info ? e.info->car_name : std::string()));
    f->set("courseName", text(e.info ? e.info->course_file : std::string()));
    f->set("time", text(""));
    char date[32] = "--/--/--";
    if (e.date[0]) std::snprintf(date, sizeof date, "%02d/%02d/%02d", e.date[0] - 2000, e.date[1], e.date[2]);
    f->set("date", text(date));
    f->set("size", number(e.kb)), f->set("portrait", number(e.portrait)), f->set("dispstep", number(0));
    f->set("spectatorMode", number(0)), f->set("racemode", number(0)), f->set("racesubmode", number(0));
    f->method("isInfoValid", [ok = e.info.has_value()](Vm&, const Value&, Args) { return number(ok); });
    const std::string name = "photo:" + std::to_string(slot) + ":" + e.folder;
    const bool icon = photo_card(*rt, slot).icon(e.folder).has_value();
    f->method("isIconValid", [icon](Vm&, const Value&, Args) { return number(icon); });
    f->method("getIconTexture", [rt, name, folder = e.folder, slot](Vm&, const Value&, Args) {
        if (!rt->game().chunk_pictures.count(name)) {
            std::optional<std::vector<u32>> px = photo_card(*rt, slot).icon(folder);
            if (!px) return text("");
            rt->game().chunk_pictures[name] = {save::kIconPicture, save::kIconPicture, std::move(*px)};
        }
        return text(name);
    });
    f->method("writeInfo", [rt, weak](Vm&, const Value&, Args) {
        const auto p = weak.lock();
        if (!p || !p->photo.info) return text("INVALID_FILE");
        return text(save::result_name(photo_card(*rt, p->slot).write_info(p->photo.folder, *p->photo.info)));
    });
    f->method("load", [rt, weak](Vm&, const Value&, Args) {
        const auto p = weak.lock();
        if (!p) return text("INVALID_FILE");
        if (p->photo.kind == 'F') { // (a film: its data for the development, 0x1C7BF8)
            std::optional<Bytes> film = photo_card(*rt, p->slot).film(p->photo.folder);
            if (!film) return text("INVALID_FILE");
            rt->game().develop_film = std::move(*film);
            return text("OK");
        }
        std::optional<Bytes> jpeg = photo_card(*rt, p->slot).picture(p->photo.folder);
        if (!jpeg) return text("INVALID_FILE");
        p->jpeg = std::move(*jpeg);
        return text("OK");
    });
    f->method("remove", [rt, weak](Vm&, const Value&, Args) {
        const auto p = weak.lock();
        return number(p && photo_card(*rt, p->slot).remove(p->photo.folder) == save::Result::Ok);
    });
    f->method("isExist", [](Vm&, const Value&, Args) { return number(0); });
    f->method("readSize", [kb = e.kb](Vm&, const Value&, Args) { return number(kb); });
    f->method("dump", [](Vm&, const Value&, Args) { return Value{}; });
    return f;
}

Value file_value(GameNatives& n, const save::PhotoEntry& e, int slot) { return object_value(make_file(n, e, slot)); }

} // namespace

int PlayListObject::find(u64 e) const {
    for (size_t i = 0; i < files.size(); ++i)
        if (files[i]->entry == e) return int(i);
    return -1;
}

std::shared_ptr<PlayListObject> make_play_list() {
    auto l = std::make_shared<PlayListObject>();
    std::weak_ptr<PlayListObject> weak = l;
    auto with = [weak](auto f) {
        return [weak, f](Vm&, const Value&, Args a) -> Value {
            const auto p = weak.lock();
            return p ? f(*p, a) : Value{};
        };
    };
    l->property("size", [weak] {
        const auto p = weak.lock();
        return number(p ? s32(p->files.size()) : 0);
    });
    // append(file): at the end while there are fewer than 400 (0x17D640 -> 0x1D1280; the scripts ask search first).
    l->method("append", with([](PlayListObject& p, Args a) -> Value {
                  if (!a.empty())
                      if (const auto f = a[0].as<ListedFile>(); f && p.files.size() < 400) p.files.push_back(f), p.changed = true;
                  return Value{};
              }));
    l->method("remove", with([](PlayListObject& p, Args a) -> Value {
                  const s32 i = arg_int(a, 0, -1);
                  if (i >= 0 && size_t(i) < p.files.size()) p.files.erase(p.files.begin() + i), p.changed = true;
                  return Value{};
              }));
    l->method("search", with([](PlayListObject& p, Args a) -> Value {
                  const auto f = a.empty() ? nullptr : a[0].as<ListedFile>();
                  return number(f ? p.find(f->entry) : -1);
              }));
    l->method("isChanged", with([](PlayListObject& p, Args) -> Value {
                  const bool c = p.changed;
                  p.changed = false;
                  return number(c);
              }));
    l->method("clear", with([](PlayListObject& p, Args) -> Value {
                  p.files.clear(), p.changed = true;
                  return Value{};
              }));
    // order(table): the files in that order (the list's rows dragged).
    l->method("order", with([](PlayListObject& p, Args a) -> Value {
                  const std::vector<Value>* t = a.empty() ? nullptr : a[0].items();
                  if (!t || t->size() != p.files.size()) return Value{};
                  std::vector<std::shared_ptr<ListedFile>> out;
                  for (const Value& v : *t) {
                      const int i = v.is_number() ? int(v.as_float()) : -1;
                      if (i < 0 || size_t(i) >= p.files.size()) return Value{};
                      out.push_back(p.files[size_t(i)]);
                  }
                  p.files = std::move(out), p.changed = true;
                  return Value{};
              }));
    l->method("rotate", with([](PlayListObject& p, Args a) -> Value {
                  const int n = int(p.files.size());
                  if (n) std::rotate(p.files.begin(), p.files.begin() + ((arg_int(a, 0) % n) + n) % n, p.files.end());
                  return Value{};
              }));
    l->method("getFile", with([](PlayListObject& p, Args a) -> Value {
                  const s32 i = arg_int(a, 0, -1);
                  return i >= 0 && size_t(i) < p.files.size() ? object_value(p.files[size_t(i)]) : Value{};
              }));
    return l;
}

int photos_kb(Runtime& rt) {
    int kb = 0;
    for (char kind : {'P', 'F'})
        for (const save::PhotoEntry& e : photo_card(rt, 0).list(kind)) kb += e.kb;
    return kb;
}

std::vector<std::string> slide_folders(Runtime& rt) {
    std::vector<std::string> out;
    const save::PhotoCard card = photo_card(rt, 0);
    std::vector<save::PhotoCard::Listed> ids;
    if (card.load_slides(ids) != save::Result::Ok) return out;
    for (const save::PhotoCard::Listed& id : ids) out.push_back(card.folder(id));
    return out;
}

std::optional<Bytes> slide_photo(Runtime& rt, const std::string& folder) { return photo_card(rt, 0).picture(folder); }

void photo_day(Runtime& rt, const std::string& course) {
    DayEvent day;
    day.type = 8, day.b3 = 5, day.b1 = 15, day.b2 = 14, day.word = kNoTime;
    if (const auto n = natives_of(rt)) day.code = n->db().code(kCourseTable, course);
    rt.game().put_day(day);
}

std::vector<float> photo_apertures(Runtime& rt) {
    std::vector<float> out;
    const auto n = natives_of(rt);
    if (!n) return out;
    n->db();
    const sim::Program& program = n->database->program;
    if (!program.known()) return out;
    constexpr u32 kApertures = 0x620E70;
    for (u32 i = 0; i < 16; ++i) {
        const u32 w = program.w(kApertures + i * 4);
        float f;
        std::memcpy(&f, &w, 4);
        out.push_back(f);
    }
    return out;
}

void photo_file_picture(Runtime& rt, Object& file) {
    auto* f = dynamic_cast<PhotoFileObject*>(&file);
    if (!rt.photo_picture) rt.photo_picture = std::make_shared<PhotoPicture>();
    PhotoPicture& p = *rt.photo_picture;
    const u64 version = p.version + 1;
    p = PhotoPicture{};
    p.version = version;
    if (!f || f->jpeg.empty()) return;
    try {
        JpegPicture j = decode_jpeg(f->jpeg);
        p.width = j.width, p.height = j.height, p.rgba = std::move(j.rgba), p.focus = j.focus;
        p.jpeg = f->jpeg;
        // (The file's padding past the picture's end is not part of it.)
        while (p.jpeg.size() > 2 && !p.jpeg.back()) p.jpeg.pop_back();
    } catch (const std::exception& e) {
        rt.note(std::string("photo: ") + e.what());
    }
}

void install_photo_lists(GameNatives& n, script::Module& menu) {
    auto cls = make_class(menu, "MMemoryCardPlayList");
    cls->construct = [](Vm&, Args) { return object_value(make_play_list()); };
    n.photo_print_list = object_value(make_play_list());
}

void answer_photo_destination(LiveWidget& project) {
    // Where to save (a memory card's slot, a USB memory): the photo to the save files. The photo pause's page asks as well whether
    // as a film (SaveDialog: Film, Film and Photo, Photo): that page is shown, as the program's.
    for (const char* name : {"DestinationDialog"})
        if (const auto dialog = project.child(name); dialog && dialog->members.count("kind"))
            if (const auto it = dialog->members.find("open"); it != dialog->members.end() && it->second->value.kind == script::Kind::Function) {
                std::weak_ptr<LiveWidget> weak = dialog;
                it->second->value = Vm::native("open", [weak](Vm&, const Value&, Args) {
                    const auto d = weak.lock();
                    if (!d) return number(0);
                    for (const auto& [member, value] : {std::pair<const char*, s32>{"slot", 0}, {"kind", 2}})
                        if (const auto m = d->members.find(member); m != d->members.end()) m->second->value = number(value);
                    return number(1);
                });
            }
}

bool install_photo_manager(GameNatives& n, Object& manager, const std::string& mode, int slot) {
    const bool save_photo = mode == "MODE_SAVE_PHOTO_PICTURE", save_film = mode == "MODE_SAVE_PHOTO_FILM";
    const bool load_photo = mode == "MODE_LOAD_PHOTO_PICTURE" || mode == "MODE_LOAD_PHOTO_PICTURE_AND_TRANSFER_USB_MEMORY";
    const bool load_film = mode == "MODE_LOAD_PHOTO_FILM";
    const bool play_list = mode == "MODE_PHOTO_PLAYLIST", print_list = mode == "MODE_PHOTO_PRINTLIST";
    if (!save_photo && !save_film && !load_photo && !load_film && !play_list && !print_list) return false;
    GameNatives* np = &n;
    Runtime* rt = &n.rt;
    auto progress = std::make_shared<Object>("MProgress");
    progress->set("value", Value::number(0));
    manager.set("progress", object_value(progress));
    for (const char* q : {"isConnect", "isFormat"}) manager.method(q, [](Vm&, const Value&, Args) { return number(1); });
    manager.method("isChanged", [](Vm&, const Value&, Args) { return number(0); });
    manager.method("isUseNewFile", [](Vm&, const Value&, Args) { return number(1); });
    manager.method("isAutoSaveDisabled", [](Vm&, const Value&, Args) { return number(0); });
    for (const char* nothing : {"clearAutoSaveDisabled", "requestPause", "waitPause", "start", "restart"})
        manager.method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
    manager.set("free", number(kFreeKb)), manager.set("realFree", number(kFreeKb));
    manager.set("cursorPos", number(0));

    // The slide show's list: the card's LSTSLIDE file, read into the list the scripts give it.
    auto shown = std::make_shared<std::shared_ptr<PlayListObject>>();
    auto loaded = std::make_shared<save::Result>(save::Result::Ok);
    manager.property("autoloadPlayListResult", [loaded] { return text(save::result_name(*loaded)); });
    auto auto_list = std::make_shared<Object>("MMemoryCardFile");
    auto_list->property("list", [shown] { return *shown ? object_value(*shown) : Value{}; }, [np, rt, slot, shown, loaded](const Value& v) {
        const auto l = v.as<PlayListObject>();
        if (!l) return;
        *shown = l;
        const save::PhotoCard card = photo_card(*rt, slot);
        std::vector<save::PhotoCard::Listed> ids;
        // (No list yet is an empty one: nothing to ask.)
        *loaded = card.has_slides() ? card.load_slides(ids) : save::Result::Ok;
        if (*loaded != save::Result::Ok || !card.has_slides()) return;
        const std::vector<save::PhotoEntry> photos = card.list('P');
        l->files.clear();
        for (const save::PhotoCard::Listed& id : ids) {
            const std::string folder = card.folder(id);
            for (const save::PhotoEntry& e : photos)
                if (e.folder == folder) l->files.push_back(make_file(*np, e, slot));
        }
    });
    auto_list->set("size", number(save::PhotoCard::kListKb));
    auto_list->method("isExist", [rt, slot](Vm&, const Value&, Args) { return number(photo_card(*rt, slot).has_slides() ? 0 : 1); });
    auto_list->method("readSize", [](Vm&, const Value&, Args) { return number(save::PhotoCard::kListKb); });
    auto_list->method("format", [](Vm&, const Value&, Args) { return number(1); });
    auto_list->method("remove", [rt, slot](Vm&, const Value&, Args) { return number(photo_card(*rt, slot).remove_slides() == save::Result::Ok); });
    for (const char* nothing : {"dump", "setNewFile"}) auto_list->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
    // save(manager) / saveNew(manager, the icon's second line)
    for (const char* write : {"save", "saveNew"}) auto_list->method(write, [rt, slot, shown, loaded](Vm&, const Value&, Args a) {
        std::vector<save::PhotoCard::Listed> ids;
        if (*shown)
            for (const auto& f : (*shown)->files) ids.push_back({f->entry & 0x00FFFFFFFFFFFFFFull, char(f->entry >> 56)});
        const save::Result r = photo_card(*rt, slot).save_slides(ids, a.size() > 1 ? a[1].to_string() : std::string("Slideshow"));
        if (r == save::Result::Ok) *loaded = r;
        return text(save::result_name(r));
    });
    manager.set("autoloadPlayList", object_value(auto_list));

    // The list: the card's photos or films, or the play list's files.
    auto files = std::make_shared<std::vector<save::PhotoEntry>>();
    auto list = std::make_shared<std::shared_ptr<PlayListObject>>();
    auto cache = std::make_shared<std::map<std::string, std::shared_ptr<PhotoFileObject>>>();
    const char kind = load_film || save_film ? 'F' : 'P';
    auto read = [rt, slot, files, kind] { *files = photo_card(*rt, slot).list(kind); };
    if (load_photo || load_film) read();
    manager.property("list", [list] { return *list ? object_value(*list) : Value{}; }, [list](const Value& v) {
        if (const auto l = v.as<PlayListObject>()) *list = l;
    });
    if (print_list)
        if (const auto l = n.photo_print_list.as<PlayListObject>()) *list = l;
    auto count = [files, list, play_list, print_list]() -> s32 {
        if (play_list || print_list) return *list ? s32((*list)->files.size()) : 0;
        return s32(files->size());
    };
    manager.property("count", [count] { return number(count()); });
    manager.method("isNoFile", [count](Vm&, const Value&, Args) { return number(count() == 0); });
    manager.method("clear", [read, cache, load_photo, load_film](Vm&, const Value&, Args) {
        if (load_photo || load_film) read(), cache->clear();
        return Value{};
    });
    manager.method("getFile", [np, slot, files, list, cache, play_list, print_list](Vm&, const Value&, Args a) -> Value {
        const s32 i = arg_int(a, 0, -1);
        if (i < 0) return Value{};
        if (play_list || print_list) return *list && size_t(i) < (*list)->files.size() ? object_value((*list)->files[size_t(i)]) : Value{};
        if (size_t(i) >= files->size()) return Value{};
        const save::PhotoEntry& e = (*files)[size_t(i)];
        auto& f = (*cache)[e.folder];
        if (!f) f = make_file(*np, e, slot);
        return object_value(f);
    });

    // A new photo: the picture the shoot has taken.
    auto file = std::make_shared<Object>("MMemoryCardFile");
    file->set("objectName", text("FilePhoto"));
    file->method("isExist", [](Vm&, const Value&, Args) { return number(1); }); // (a new one)
    file->method("readSize", [](Vm&, const Value&, Args) { return number(0); });
    file->method("format", [](Vm&, const Value&, Args) { return number(1); });
    file->method("remove", [](Vm&, const Value&, Args) { return number(1); });
    file->method("isInfoValid", [](Vm&, const Value&, Args) { return number(0); });
    for (const char* nothing : {"dump", "setNewFile"}) file->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
    file->property("size", [rt, slot, save_film] {
        if (!rt->photo_picture || rt->photo_picture->jpeg.empty()) return number(0);
        const PhotoPicture& p = *rt->photo_picture;
        if (save_film) return number(photo_card(*rt, slot).needed_film_kb(p.film, small_picture(p), p.height > p.width));
        return number(photo_card(*rt, slot).needed_kb(p.jpeg, small_picture(p), p.height > p.width));
    });
    file->method("saveNew", [np, rt, slot, save_film](Vm&, const Value&, Args a) {
        if (!rt->photo_picture || rt->photo_picture->jpeg.empty() || (!rt->photo && rt->photo_picture->course.empty())) return text("INVALID_FILE");
        const PhotoPicture& p = *rt->photo_picture;
        if (save_film && p.film.empty()) return text("INVALID_FILE"); // (a film is the photo pause's only)
        // (the shoot's place and car, or the photo pause's: the picture's own)
        const std::string course = rt->photo ? rt->photo->course : p.course, car_label = rt->photo ? rt->photo->car_label : p.car_label;
        Database& d = np->db();
        save::PhotoInfo info;
        info.course_file = course;
        info.course = d.code(kCourseTable, info.course_file);
        info.car = d.code(kCarTable, car_label);
        if (const db::CarEntry* c = d.car(car_label)) info.car_name = c->name;
        const Value car = make_car_object(*rt, car_label, 0);
        if (car.p) info.car_name = rt->vm().attribute(car, "short_name").to_string();
        info.portrait = p.height > p.width;
        info.mode = p.mode;
        save::PhotoCard card = photo_card(*rt, slot);
        if (save_film) return text(save::result_name(card.save_film(info, p.film, small_picture(p), a.size() > 1 ? a[1].to_string() : std::string("Film data"))));
        return text(save::result_name(card.save(info, p.jpeg, small_picture(p), a.size() > 1 ? a[1].to_string() : std::string("Photo data"))));
    });
    const Value new_file = object_value(file);
    manager.method("getNewFile", [new_file](Vm&, const Value&, Args) { return new_file; });
    return true;
}

} // namespace gt4r::menu
