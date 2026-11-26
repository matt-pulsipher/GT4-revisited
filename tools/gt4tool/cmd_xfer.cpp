// gt4tool xfer: Gran Turismo 3's saves for Gran Turismo mode's Status page (docs/formats/save.md "Earlier games' saves").
//
//   gt4tool xfer <disc> texts <project> [page]     a project's texts (every page, or the pages whose name holds `page`)
//   gt4tool xfer <disc> read <file> [region]       what the transfer takes from a file the player chooses (the US program's rules)
//   gt4tool xfer <disc> find <GT3R folder> [card]  GT3 Revisited's saves found in its data folder, and those in a card folder

#include "gt4r/disc/disc.h"
#include "gt4r/menu/project.h"
#include "gt4r/save/earlier_game.h"

#include "base/file.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace gt4r;

namespace gt4tool {

menu::FileReader disc_reader(Disc& disc); // cmd_menu.cpp

int cmd_xfer(Disc& disc, const std::vector<std::string>& a) {
    if (a[0] == "texts" && a.size() >= 2) {
        const menu::Project project(disc_reader(disc), a[1], a.size() > 3 ? a[3] : "US");
        for (const std::string& page : project.text().pages()) {
            if (a.size() > 2 && page.find(a[2]) == std::string::npos) continue;
            for (const auto& [key, text] : *project.text().page(page)) {
                std::string one = text;
                for (size_t at = 0; (at = one.find('\n', at)) != std::string::npos;) one.replace(at, 1, "\\n");
                std::printf("%s | %s | %s\n", page.c_str(), key.c_str(), one.c_str());
            }
        }
        return 0;
    }
    // What the transfer would take from a file, or finds in GT3 Revisited's folder and a card folder.
    auto show = [](const save::EarlierGame& g) {
        const std::string region = "us";
        const std::optional<s64> credits = save::transferred_credits(g, region);
        const int licenses = save::transferred_licenses(g, region);
        std::string medals;
        for (u8 m : g.medals) medals += m >= 0x80 ? '-' : char('0' + std::min<int>(m, 9));
        std::printf("%-28s %-24s Cr %-12lld medals %s -> %s, %s%s\n  %s\n", g.name.c_str(), g.from.c_str(), (long long)g.credits, medals.c_str(),
                    credits ? ("Cr " + std::to_string(*credits)).c_str() : "no credits", licenses == 2 ? "B and A" : licenses == 1 ? "B" : "no licenses",
                    g.current ? "  (being played)" : "", g.where.c_str());
    };
    if (a[0] == "read" && a.size() >= 2) {
        const std::optional<Bytes> b = gt3r::read_host_file(a[1]);
        std::string why;
        const std::optional<save::EarlierGame> g = b ? save::read_earlier_file(*b, a[1], a.size() > 2 ? a[2] : "us", &why) : std::nullopt;
        if (!g) std::printf("not taken: %s\n", b ? why.c_str() : "cannot be read");
        else show(*g);
        return g ? 0 : 1;
    }
    if (a[0] == "find" && a.size() >= 2) {
        std::string looked;
        for (const save::EarlierGame& g : save::find_gt3r_games(a[1], "us", &looked)) show(g);
        std::printf("(GT3 Revisited's saves looked for in %s)\n", looked.c_str());
        if (a.size() > 2)
            for (const save::EarlierGame& g : save::find_card_games(a[2], "us")) show(g);
        return 0;
    }
    std::fprintf(stderr, "usage: gt4tool xfer <disc> texts <project> [page] | read <file> [region] | find <GT3 Revisited folder> [card folder]\n");
    return 1;
}

} // namespace gt4tool
