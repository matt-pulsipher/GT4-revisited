// gt4tool's program-tables commands (docs/specs/physics.md "Program tables"):
//
//   gt4tool tables <disc>                       Gran Turismo 4's tables the simulation reads (ours, program_tables.h) against
//                                               the disc's program: per table where it was found, how, and whether it is equal
//   gt4tool tables <disc> image <out>           the unpacked program as loaded, for analysis outside the repository
//   gt4tool tables <disc> survey <sites.tsv>    for each candidate site (name, address, size, routine, words, upper, lower,
//                                               the address it builds): the masked routine's hash, how many places have it and
//                                               whether the site decodes to the table (a site list for program_tables.cpp)

#include "disc/core_image.h"
#include "gt4r/disc/disc.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/program_code.h"
#include "gt4r/sim/program_tables.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace gt4r::tool {

namespace {

using Args = std::vector<std::string>;

// The unpacked program as loaded (for scratch analysis outside the repository).
int image(Disc& disc, const std::string& path) {
    const gt3r::CoreImage core = gt3r::CoreImage::decode(disc.read_program());
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return 1;
    std::fwrite(core.program.data(), 1, core.program.size(), f);
    std::fclose(f);
    std::printf("load 0x%08X, %zu bytes\n", core.load_address, core.program.size());
    return 0;
}

int report(Disc& disc) {
    const sim::Program p = sim::Program::load(disc);
    std::printf("program: %s\n", p.known() ? "Gran Turismo 4's tables served (ours)" : p.issue().c_str());
    if (p.ours())
        for (const std::string& line : p.check().lines(true)) std::printf("  %s\n", line.c_str());
    // The same found by the routines that address the tables, as on another build.
    const std::shared_ptr<const gt3r::CoreImage> core = disc.program_image();
    std::printf("by their routines:\n");
    for (const std::string& line : sim::check_tables(core->program, core->load_address, true).lines(true)) std::printf("  %s\n", line.c_str());
    return 0;
}

int survey(Disc& disc, const std::string& path) {
    const gt3r::CoreImage core = gt3r::CoreImage::decode(disc.read_program());
    const sim::ProgramCode code(core.program, core.load_address);
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        std::string name, ref, size, at, words, hi, lo, built;
        if (!(s >> name >> ref >> size >> at)) continue;
        if (at == "NONE") {
            std::printf("%s\t%s\tNONE\n", name.c_str(), ref.c_str());
            continue;
        }
        s >> words >> hi >> lo >> built;
        const u32 r = u32(std::strtoul(ref.c_str(), nullptr, 16)), a = u32(std::strtoul(at.c_str(), nullptr, 16));
        const u32 n = u32(std::strtoul(words.c_str(), nullptr, 10)), l = u32(std::strtoul(lo.c_str(), nullptr, 10));
        const u32 b = u32(std::strtoul(built.c_str(), nullptr, 16));
        const u64 h = code.hash(a, n);
        const size_t places = code.find(h, n).size();
        const u32 decoded = code.address_in(a, l, b - r);
        std::printf("%s\t%06X\t%06X\t%u\t%u\t0x%X\t0x%016llxull\t%zu\t%s\n", name.c_str(), r, a, n, l, b - r, (unsigned long long)h, places,
                    decoded == r ? "decodes" : "DOES NOT DECODE");
    }
    return 0;
}

} // namespace

int cmd_tables(Disc& disc, const Args& a) {
    if (a.empty()) return report(disc);
    if (a[0] == "survey" && a.size() > 1) return survey(disc, a[1]);
    if (a[0] == "image" && a.size() > 1) return image(disc, a[1]);
    std::fprintf(stderr, "usage: gt4tool tables <disc> [survey <sites.tsv>]\n");
    return 2;
}

} // namespace gt4r::tool
