// gt4tool menu-run: a menu project run without a window. The pages it opens, the focus, the
// sounds and the script errors are printed after each step.
//
//   gt4tool menu-run <disc> <project|boot> [steps...]
//     up down left right accept cancel start select l1 r1 l2 r2 square triangle   a press
//     waitN        N frames                 settle   frames until nothing waits
//     next         the project the scripts asked for (after a project finished)
//     stubs        what the scripts asked of stand-ins so far (the to-do list)
//     tree         the pages shown: widgets, their places and texts
//     card=none | card=empty | card=saved    the memory card's state (before the project starts)
//     all          every car and course available (before the project starts)

#include "gt4r/disc/disc.h"
#include "gt4r/menu/runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace gt4r;

namespace gt4tool {

menu::FileReader disc_reader(Disc& disc) {
    return [&disc](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = disc.volume().find(path);
        if (!e) return std::nullopt;
        return disc.volume().read(*e);
    };
}

menu::FolderLister disc_lister(Disc& disc) {
    return [&disc](const std::string& folder) {
        std::vector<std::string> out;
        for (const gt3r::Volume::Entry& e : disc.volume().entries()) {
            if (e.path.compare(0, folder.size(), folder) != 0) continue;
            const size_t slash = e.path.find('/', folder.size());
            std::string name = e.path.substr(folder.size(), slash == std::string::npos ? slash : slash + 1 - folder.size());
            if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
        }
        return out;
    };
}

namespace {

void print_tree(const menu::LiveWidget& w, int depth) {
    if (!w.visible()) return;
    const menu::Rect r = w.screen_rect();
    std::string line(size_t(depth) * 2, ' ');
    line += w.data.type + " " + w.name;
    char b[96];
    std::snprintf(b, sizeof b, "  [%g %g %g %g]", double(r.x), double(r.y), double(r.w), double(r.h));
    line += b;
    if (w.state) line += " state " + std::to_string(w.state);
    if (w.data.has("opacity") && w.opacity() != 1.0f) {
        std::snprintf(b, sizeof b, " opacity %.2f", double(w.opacity()));
        line += b;
    }
    for (const char* p : {"text", "key", "image_path"})
        if (!w.data.text(p).empty()) line += std::string(" ") + p + "=\"" + w.data.text(p) + "\"";
    std::printf("   %s\n", line.c_str());
    for (const auto& c : w.children) print_tree(*c, depth + 1);
}

} // namespace

int cmd_menu_run(Disc& disc, const std::vector<std::string>& a) {
    menu::Runtime::Config config;
    config.list = disc_lister(disc);
    const std::vector<std::string> languages = menu::project_languages(config.list, a[0] == "boot" ? "boot" : a[0]);
    if (!languages.empty()) config.language = languages[0];
    if (const char* dir = std::getenv("GT4R_DATA_DIR")) config.data_dir = dir;
    menu::Runtime rt(disc_reader(disc), config);
    rt.trace_calls = std::getenv("GT4R_MENU_TRACE") != nullptr;
    size_t shown = 0;
    auto report = [&](const std::string& after) {
        std::printf("-- %s\n", after.c_str());
        for (; shown < rt.log().size(); ++shown) std::printf("   %s\n", rt.log()[shown].c_str());
        std::string pages;
        for (const auto& p : rt.open_pages()) pages += (pages.empty() ? "" : " > ") + p->name;
        const menu::LiveWidget* focus = rt.focused();
        std::printf("   pages: %s; focus: %s%s\n", pages.empty() ? "(none)" : pages.c_str(), focus ? focus->path().c_str() : "(none)",
                    rt.finished() ? "; finished" : rt.idle() ? "" : "; a script waits");
    };
    // What is set before the project starts.
    size_t first = 1;
    for (; first < a.size(); ++first) {
        const std::string& k = a[first];
        if (k == "card=none") rt.game().card.connected = false;
        else if (k == "card=empty") rt.game().card.connected = true, rt.game().card.has_game_data = false;
        else if (k == "card=saved") rt.game().card.connected = true, rt.game().card.has_game_data = true;
        else if (k == "all") rt.game().all_available = true;
        else break;
    }
    rt.boot();
    rt.start_project(a[0]);
    rt.settle();
    report("start " + a[0]);
    for (size_t i = first; i < a.size(); ++i) {
        const std::string& k = a[i];
        if (k == "stubs") {
            for (const auto& [what, n] : rt.vm().stub_uses) std::printf("   %5u  %s\n", n, what.c_str());
            continue;
        }
        if (k == "tree") {
            for (const auto& p : rt.open_pages()) print_tree(*p, 0);
            continue;
        }
        if (k == "settle") rt.settle();
        else if (k.compare(0, 4, "wait") == 0) {
            for (int n = std::max(std::atoi(k.c_str() + 4), 1); n > 0; --n) rt.update();
        } else if (k == "next") {
            if (const std::optional<menu::RaceRequest> race = rt.race_request()) {
                std::printf("   race asked for: %s  course %s  car %s  paint %d  laps %d  difficulty %d  entries %d  race \"%s\"\n", race->sequence.c_str(),
                            race->course.c_str(), race->car.c_str(), race->paint, race->laps, race->difficulty, race->max_entry, race->race.c_str());
                continue;
            }
            if (!rt.advance()) {
                std::printf("   nothing asked for next\n");
                continue;
            }
            rt.settle();
        } else if (const std::optional<menu::Key> key = menu::key_by_name(k)) {
            rt.key(*key);
            rt.settle();
        } else {
            std::fprintf(stderr, "menu-run: no step named %s\n", k.c_str());
            return 2;
        }
        report(k);
    }
    return 0;
}

} // namespace gt4tool
