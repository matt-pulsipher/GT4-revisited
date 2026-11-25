// gt4tool textures: the game's textures as a pack names them (src/gt4r/settings/texture_files.h).
//
//   gt4tool textures <disc> <outdir> [prefix]    saved as PNG pictures, the files under `prefix` only ("car/menu/ka")
//   gt4tool textures <disc> - [prefix]           only counted

#include "gt4r/disc/disc.h"
#include "gt4r/settings/texture_files.h"

#include "formats/texture_pack.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace gt4r::tool {

int cmd_textures(Disc& disc, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::printf("gt4tool textures <disc> <outdir | -> [prefix]\n");
        return 1;
    }
    const std::string out = args[0], prefix = args.size() > 1 ? args[1] : std::string();
    const settings::FileReader read = [&disc](const std::string& path) -> std::optional<Bytes> {
        if (const auto* e = disc.volume().find(path)) return disc.volume().read(*e);
        return std::nullopt;
    };
    gt3r::texture_pack::Dump dump(out);
    std::map<std::string, std::pair<size_t, size_t>> by_folder; // folder -> files, textures
    std::set<u64> keys;
    u64 texels = 0;
    for (const ImageFile& f : disc.files()) {
        if (f.path.rfind(prefix, 0) != 0 || !settings::has_pack_textures(f.path)) continue;
        const std::optional<Bytes> data = read(f.path);
        if (!data) continue;
        size_t n = 0;
        if (out == "-") settings::for_each_texture(f.path, *data, read, [&](const std::string&, const gt3r::texture_pack::FileTexture& t) {
            if (keys.insert(t.key).second) ++n, texels += u64(t.texture->width) * t.texture->height;
        });
        else n = settings::dump_file(dump, f.path, *data, read);
        auto& [files, textures] = by_folder[f.path.substr(0, f.path.find('/', f.path.find('/') + 1))];
        files += n > 0, textures += n;
    }
    for (const auto& [folder, n] : by_folder) std::printf("%-24s %6zu files %7zu textures\n", folder.c_str(), n.first, n.second);
    if (out == "-") std::printf("%zu textures, %.1f million pixels\n", keys.size(), double(texels) / 1e6);
    else std::printf("%zu textures saved in %s\n", dump.saved(), out.c_str()), dump.finish();
    return 0;
}

} // namespace gt4r::tool
