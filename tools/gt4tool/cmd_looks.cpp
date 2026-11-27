// gt4tool looks <disc> materials [prefix]: the lit shapes' materials of every model set under a prefix, counted by the VU
// program the renderer picks for them (docs/formats/model.md "Lighting"): the lit kind 1 by its specular exponent (shininess
// < 6, < 12, else), the sheen of kind 4 likewise (GT4's own programs: 8 below 12, 16 above).

#include "gt4r/disc/disc.h"
#include "gt4r/formats/model/mdls.h"

#include <array>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace gt4r::tool {

int cmd_looks(Disc& disc, const std::vector<std::string>& args) {
    if (args.empty() || args[0] != "materials") {
        std::fprintf(stderr, "usage: gt4tool looks <disc> materials [prefix]\n");
        return 2;
    }
    const std::string prefix = args.size() > 1 ? args[1] : "";
    // [kind 1 lit with specular | kind 1 without | kind 4] x [< 6, < 12, else]
    std::map<std::string, std::array<size_t, 3>> counts;
    std::map<std::string, std::string> first;
    size_t sets = 0;
    for (const gt3r::Volume::Entry& e : disc.volume().entries()) {
        if (e.path.compare(0, prefix.size(), prefix) != 0 || e.size > (64u << 20)) continue;
        const Bytes data = disc.volume().read(e);
        for (size_t off : mdls::find_model_sets(data)) try {
                const gt3r::scene::Model m = mdls::build_model(ByteSpan(data).subspan(off), {});
                ++sets;
                for (const gt3r::scene::Mesh& mesh : m.meshes)
                    for (const gt3r::scene::Primitive& p : mesh.primitives) {
                        const gt3r::scene::Material& mt = p.material;
                        if (!mt.valid) continue;
                        const bool spec = mt.specular[0] > 0 || mt.specular[1] > 0 || mt.specular[2] > 0;
                        const std::string row = mt.specular_local ? (spec ? "kind 4 (sheen)" : "kind 4, Ms 0") : spec ? "kind 1, specular" : "kind 1, no specular";
                        const int b = mt.shininess < 6 ? 0 : mt.shininess < 12 ? 1 : 2;
                        ++counts[row][size_t(b)];
                        if (!first.count(row + std::to_string(b))) first[row + std::to_string(b)] = e.path;
                    }
            } catch (const std::exception&) {
            }
    }
    std::printf("%zu model sets under \"%s\"; primitives by shininess < 6 | < 12 | >= 12\n", sets, prefix.c_str());
    for (const auto& [row, c] : counts) {
        std::printf("  %-20s %8zu %8zu %8zu", row.c_str(), c[0], c[1], c[2]);
        for (int b = 0; b < 3; ++b)
            if (c[size_t(b)]) std::printf("  [%d] %s", b, first[row + std::to_string(b)].c_str());
        std::printf("\n");
    }
    return 0;
}

} // namespace gt4r::tool
