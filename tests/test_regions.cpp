// The releases of Gran Turismo 4 (docs/formats/menu.md "The release's region and language"): each disc's region, card
// product and language, and its menus booted as its own program boots them.

#include "test_framework.h"

#include "gt4r/menu/runtime.h"
#include "gt4r/sim/program.h"

#include <algorithm>

using namespace gt4r;

namespace {

Disc* release_disc(const char* serial) {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().serial == serial) return d.get();
    return nullptr;
}

std::unique_ptr<menu::Runtime> runtime_of(Disc& d) {
    menu::Runtime::Config config;
    config.region = d.info().region, config.language = d.info().language, config.product = d.info().product;
    config.console_card = true;
    config.list = [&d](const std::string& folder) {
        std::vector<std::string> out;
        for (const gt3r::Volume::Entry& e : d.volume().entries()) {
            if (e.path.compare(0, folder.size(), folder) != 0) continue;
            const size_t slash = e.path.find('/', folder.size());
            std::string name = e.path.substr(folder.size(), slash == std::string::npos ? slash : slash + 1 - folder.size());
            if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
        }
        return out;
    };
    config.program = [&d] { return d.read_program(); };
    return std::make_unique<menu::Runtime>(
        [&d](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = d.volume().find(path);
            if (!e) return std::nullopt;
            return d.volume().read(*e);
        },
        config);
}

std::string page_of(menu::Runtime& rt) { return rt.current_page() ? rt.current_page()->name : std::string(); }

} // namespace

// Each release known by its serial: verified, its region as its program names it, its card folder's product and its
// menus' first language; the program's release by its code.
TEST(disc_regions_identified) {
    struct Want {
        const char* serial;
        const char* region;
        const char* product;
        const char* language;
        sim::Release release;
    };
    const Want wants[] = {
        {"SCUS_973.28", "us", "BASCUS-97328", "US", sim::Release::Us},
        {"SCES_517.19", "uk", "BESCES-51719", "GB", sim::Release::Europe},
        {"SCPS_170.01", "jp", "BISCPS-17001", "JP", sim::Release::Japan},
    };
    int ran = 0;
    for (const Want& w : wants) {
        Disc* d = release_disc(w.serial);
        if (!d) continue;
        ++ran;
        const DiscInfo& i = d->info();
        CHECK(i.game == Game::GT4 && i.verified && !i.modified);
        CHECK(i.region == w.region && i.product == w.product && i.language == w.language);
        const sim::Program p = sim::Program::load(*d);
        CHECK(p.known() && p.release_known() && p.release() == w.release);
    }
    if (!ran) SKIP("needs a Gran Turismo 4 disc (GT4R_DISC)");
}

// Europe's boot asks for the language before anything else; French chosen, the menus are French with French units (km/h,
// ch, kgf.m) and the car database's names are the French table's.
TEST(disc_regions_europe_language_page) {
    Disc* d = release_disc("SCES_517.19");
    if (!d) SKIP("needs Gran Turismo 4 (Europe), SCES-51719 (GT4R_DISC)");
    std::unique_ptr<menu::Runtime> rt = runtime_of(*d);
    rt->boot();
    rt->start_project(rt->first_project());
    rt->settle();
    CHECK(page_of(*rt) == "LanguageRoot");
    rt->key(menu::Key::Down), rt->settle();
    rt->key(menu::Key::Accept), rt->settle();
    CHECK(rt->language() == "FR");
    CHECK(rt->game().option_text("language") == "FR");
    CHECK(rt->game().option_text("unit_velocity") == "KMPH" && rt->game().option_text("unit_power") == "CH" && rt->game().option_text("unit_torque") == "KGFM");
    // (the page asks for the boot project again: it starts in French, at the memory card's question)
    if (rt->finished()) rt->advance(), rt->settle();
    CHECK(rt->project() && rt->project()->language() == "FR" && page_of(*rt) == "ConfirmDialog");
}

// Japan's and the US disc's boots go straight to the memory card's question, in their own language.
TEST(disc_regions_boot_without_language_page) {
    int ran = 0;
    for (const char* serial : {"SCUS_973.28", "SCPS_170.01"}) {
        Disc* d = release_disc(serial);
        if (!d) continue;
        ++ran;
        std::unique_ptr<menu::Runtime> rt = runtime_of(*d);
        rt->boot();
        rt->start_project(rt->first_project());
        rt->settle();
        CHECK(page_of(*rt) == "ConfirmDialog");
        CHECK(rt->project() && rt->project()->language() == d->info().language);
    }
    if (!ran) SKIP("needs a Gran Turismo 4 disc (GT4R_DISC)");
}
