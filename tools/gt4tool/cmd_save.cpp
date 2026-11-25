// gt4tool save: Gran Turismo 4's saved game on a memory card (docs/formats/save.md).
//
//   gt4tool save <disc> show <card folder>          the game in a card's folder (BASCUS-97328GAMEDATA/ inside it)
//   gt4tool save <disc> payload <card folder> <out>  the game data file's payload, unscrambled
//   gt4tool save <disc> new <out>                    a new game's payload as the program's constructors make it
//   gt4tool save <disc> card <card folder> [name]    a new game saved in a card's folder
//   gt4tool save <disc> import <card image> <card folder>          the game's folder of a .ps2 / .mcd card image into a card folder
//   gt4tool save <disc> export <card folder> <out image> [base image]  the card folder's game into a card image (another card's other saves kept)
//   gt4tool save <disc> progress <data folder> [name] [credits]  a new game played forward through the menus (cars bought, every
//                                                    license passed, the presents given) and saved in <data folder>/memorycard

#include "gt4r/disc/disc.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/menu/game_progress.h"
#include "gt4r/menu/program_tables.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/save/card_image.h"
#include "gt4r/save/codec.h"
#include "gt4r/save/memory_card.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <fstream>

using namespace gt4r;

namespace gt4tool {

menu::FileReader disc_reader(Disc& disc);   // cmd_menu.cpp
menu::FolderLister disc_lister(Disc& disc); // cmd_menu.cpp

namespace {

db::SpecDb open_db(Disc& disc) {
    auto read = [&disc](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = disc.volume().find(path);
        if (!e) return std::nullopt;
        return disc.volume().read(*e);
    };
    auto list = [&disc](const std::string& dir) {
        std::vector<std::string> out;
        const std::string folder = !dir.empty() && dir.back() == '/' ? dir : dir + "/";
        for (const gt3r::Volume::Entry& e : disc.volume().entries()) {
            if (e.path.compare(0, folder.size(), folder) != 0) continue;
            const size_t slash = e.path.find('/', folder.size());
            std::string name = e.path.substr(folder.size(), slash == std::string::npos ? slash : slash + 1 - folder.size());
            if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
        }
        return out;
    };
    return db::SpecDb(read, list, "american");
}

bool write_file(const std::string& path, ByteSpan b) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    return bool(out);
}

} // namespace

int cmd_save(Disc& disc, const std::vector<std::string>& a) {
    const std::string what = a[0];
    if (what == "new" && a.size() >= 2) {
        db::SpecDb db = open_db(disc);
        const menu::GameState fresh;
        const Bytes payload = save::write_game(fresh, save::database_codes(db));
        return write_file(a[1], payload) ? 0 : 1;
    }
    if (what == "card" && a.size() >= 2) {
        db::SpecDb db = open_db(disc);
        auto read = [&disc](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = disc.volume().find(path);
            if (!e) return std::nullopt;
            return disc.volume().read(*e);
        };
        save::GameSave card(a[1], save::database_codes(db), read);
        menu::GameState s;
        s.username = a.size() >= 3 ? a[2] : "Player", s.money = 10000;
        const save::Result r = card.save_new(s, "Game Data");
        std::printf("%s: %s\n", card.folder().c_str(), save::result_name(r));
        return r == save::Result::Ok ? 0 : 1;
    }
    if (what == "progress" && a.size() >= 2) {
        menu::Runtime::Config config;
        config.list = disc_lister(disc);
        config.program = [&disc] { return disc.read_program(); };
        const std::vector<std::string> languages = menu::project_languages(config.list, "boot");
        if (!languages.empty()) config.language = languages[0];
        config.data_dir = a[1];
        config.console_card = true;
        menu::Runtime rt(disc_reader(disc), config);
        menu::ProgressPlan plan = menu::default_progress_plan(rt);
        if (a.size() >= 3) plan.name = a[2];
        if (a.size() >= 4) plan.credits = std::atoll(a[3].c_str());
        const menu::ProgressReport r = menu::play_progress(rt, plan);
        for (const std::string& line : r.steps) std::printf("%s\n", line.c_str());
        if (!r.ok) std::fprintf(stderr, "error: %s\n", r.error.c_str());
        return r.ok ? 0 : 1;
    }
    if (what == "import" && a.size() >= 3) {
        std::ifstream in(a[1], std::ios::binary);
        const Bytes image((std::istreambuf_iterator<char>(in)), {});
        const save::CardImageResult r = save::import_card(image, a[2]);
        for (const std::string& f : r.folders) std::printf("on the card: %s\n", f.c_str());
        std::printf("%s\n", r.message.c_str());
        return r.ok ? 0 : 1;
    }
    if (what == "export" && a.size() >= 3) {
        std::optional<Bytes> base;
        if (a.size() >= 4) {
            std::ifstream in(a[3], std::ios::binary);
            base = Bytes((std::istreambuf_iterator<char>(in)), {});
        }
        std::string why;
        const Bytes image = base ? save::export_card(a[1], ByteSpan(*base), &why) : save::export_card(a[1], std::nullopt, &why);
        if (image.empty()) {
            std::fprintf(stderr, "error: %s\n", why.c_str());
            return 1;
        }
        std::printf("%s: %zu bytes\n", a[2].c_str(), image.size());
        return write_file(a[2], image) ? 0 : 1;
    }
    if ((what == "show" || what == "payload") && a.size() >= 2) {
        db::SpecDb db = open_db(disc);
        save::GameSave card(a[1], save::database_codes(db));
        if (what == "payload") {
            std::ifstream in(card.folder() + "BASCUS-97328GAMEDATA", std::ios::binary);
            const Bytes file((std::istreambuf_iterator<char>(in)), {});
            const std::optional<Bytes> p = save::unscramble(file, 0);
            if (!p) {
                std::fprintf(stderr, "error: the game data file does not decode\n");
                return 1;
            }
            return a.size() >= 3 && write_file(a[2], *p) ? 0 : 1;
        }
        menu::GameState s;
        const save::Result r = card.load(s);
        std::printf("load: %s\n", save::result_name(r));
        if (r != save::Result::Ok) return 1;
        std::printf("player %s, credits %lld, day %u (game day %u), %zu diary days\n", s.username.c_str(), static_cast<long long>(s.money), s.today,
                    s.today - menu::GameState::kFirstDay, s.diary.size());
        std::printf("garage: %d cars, driving place %d, key %08X, serial %u\n", s.garage_count(), s.riding_car, s.garage_key, s.garage_serial);
        for (size_t i = 0; i < s.garage.size(); ++i) {
            const menu::GarageEntry& e = s.garage[i];
            if (!e.used) continue;
            std::printf("  %4zu %-32s paint %2d  %6.1f PS %5u kg  %8.1f km  rank %d%s\n", i, e.car.c_str(), e.paint, e.power / 100.0, e.weight,
                        (e.record ? e.record->odometer() : e.odometer) / 1000.0, e.ride_rank, e.record ? "  (record)" : "");
        }
        // The game written again from the state: the same payload when every block is understood or kept.
        {
            std::ifstream in(card.folder() + "BASCUS-97328GAMEDATA", std::ios::binary);
            const Bytes file((std::istreambuf_iterator<char>(in)), {});
            const std::optional<Bytes> p = save::unscramble(file, 0);
            menu::GameState t = s;
            t.garage_serial = p && p->size() >= 0x135FC ? u32((*p)[0x135F8] | (*p)[0x135F9] << 8 | (*p)[0x135FA] << 16 | (*p)[0x135FB] << 24) : 0;
            const Bytes again = save::write_game(t, save::database_codes(db));
            std::printf("written again: %s\n", p && std::equal(again.begin(), again.end(), p->begin()) ? "the same bytes" : "different");
        }
        std::printf("available: %zu cars, %zu courses%s; favorites %zu cars, %zu courses\n", s.available_cars.size(), s.available_courses.size(),
                    s.all_available ? " (all)" : "", s.favorite_cars.size(), s.favorite_courses.size());
        return 0;
    }
    std::fprintf(stderr, "usage: gt4tool save <disc> show <card folder> | payload <card folder> <out> | new <out> | card <card folder> [name] | import <card image> <card folder> | export <card folder> <out image> [base image] | progress <data folder> [name] [credits]\n");
    return 2;
}

} // namespace gt4tool
