// gt4tool gpb: a menu page's file pack (.gpb), listed or written out, for format research.
//
//   gt4tool gpb <disc> <pack path>            its entries: name, stored size, magic
//   gt4tool gpb <disc> <pack path> <outdir>   the same, each entry written (unpacked) under outdir
//
// Extracted files are for your own study; never commit them (docs/REVERSE_ENGINEERING.md).

#include "gt4r/disc/disc.h"
#include "gt4r/menu/image_pack.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace gt4r::tool {

int menudraw_main(int argc, char** argv) {
    if (argc < 4 || std::strcmp(argv[1], "gpb") != 0) return -1;
    try {
        const std::unique_ptr<Disc> disc = Disc::open(argv[2]);
        const gt3r::Volume::Entry* e = disc->volume().find(argv[3]);
        if (!e) {
            std::fprintf(stderr, "error: no %s\n", argv[3]);
            return 1;
        }
        const Bytes file = disc->volume().read(*e);
        const menu::ImagePack pack = menu::ImagePack::parse(file);
        const std::string out = argc > 4 ? argv[4] : "";
        for (const menu::ImagePack::Entry& en : pack.entries()) {
            const Bytes b = pack.read(en);
            char magic[5] = {};
            for (size_t i = 0; i < 4 && i < b.size(); ++i) magic[i] = b[i] >= 32 && b[i] < 127 ? char(b[i]) : '.';
            std::printf("%8u %8zu  %-4s  %s\n", en.size, b.size(), magic, en.name.c_str());
            if (out.empty()) continue;
            const std::filesystem::path p = std::filesystem::path(out) / en.name;
            std::filesystem::create_directories(p.parent_path());
            std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
        }
        return 0;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
}

} // namespace gt4r::tool
