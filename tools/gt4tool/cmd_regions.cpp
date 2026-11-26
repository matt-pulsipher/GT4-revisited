// gt4tool's regional-release commands (docs/specs/regions.md):
//
//   gt4tool regions <disc> map <reference image> <functions.tsv> <out prefix>
//       every routine of a reference build (its unpacked image as `gt4tool tables <disc> image` writes it, loaded at
//       0x100000, and its function list: start and end in hex, a header line) found in the disc's program by its masked
//       code (program_code.h): <out>.functions.tsv (reference start, words, places found, the disc's start or "-"), and
//       <out>.data.tsv (every data address a found routine builds: the reference's, the disc's, how many instructions
//       build the pair), for analysis outside the repository.

#include "base/file.h"
#include "disc/core_image.h"
#include "gt4r/disc/disc.h"
#include "gt4r/sim/program_code.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace gt4r::tool {

namespace {

using Args = std::vector<std::string>;

// Where a routine may start in an image: after a return's or a jump's delay slot (padding skipped), and at every
// stack frame's opening.
std::vector<u32> starts(ByteSpan img, u32 load, u32 text_end) {
    std::vector<u32> out;
    const u32 n = (text_end - load) / 4;
    auto w = [&](u32 i) { return u32(img[4 * i]) | u32(img[4 * i + 1]) << 8 | u32(img[4 * i + 2]) << 16 | u32(img[4 * i + 3]) << 24; };
    std::vector<bool> mark(n, false);
    for (u32 i = 0; i < n; ++i) {
        const u32 x = w(i);
        if ((x & 0xFFFF8000u) == 0x27BD8000u) mark[i] = true; // addiu $sp, $sp, -k
        if (i + 2 < n && (x == 0x03E00008u || (x >> 26) == 2)) {
            u32 j = i + 2;
            while (j < n && w(j) == 0) ++j;
            if (j < n) mark[j] = true;
        }
    }
    for (u32 i = 0; i < n; ++i)
        if (mark[i]) out.push_back(load + 4 * i);
    return out;
}

int map(Disc& disc, const Args& a) {
    if (a.size() < 4) return 2;
    const std::optional<Bytes> ref = gt3r::read_host_file(a[1]);
    if (!ref) {
        std::fprintf(stderr, "cannot read %s\n", a[1].c_str());
        return 1;
    }
    const gt3r::CoreImage core = gt3r::CoreImage::decode(disc.read_program());
    const sim::ProgramCode rc(*ref, 0x100000), dc(core.program, core.load_address);
    // The reference's routines.
    struct Fn {
        u32 start, words;
    };
    std::vector<Fn> fns;
    std::ifstream in(a[2]);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        std::string st, en;
        if (!(s >> st >> en) || st == "start") continue;
        const u32 b = u32(std::strtoul(st.c_str(), nullptr, 16)), e = u32(std::strtoul(en.c_str(), nullptr, 16));
        if (e > b && rc.in_code(b, (e - b) / 4)) fns.push_back({b, (e - b) / 4});
    }
    const std::vector<u32> cand = starts(core.program, core.load_address, dc.text_end());
    // The disc's candidates hashed once per length.
    std::map<u32, std::unordered_multimap<u64, u32>> by_len;
    for (const Fn& f : fns) by_len.try_emplace(f.words);
    for (auto& [len, m] : by_len)
        for (u32 c : cand)
            if (dc.in_code(c, len)) m.emplace(dc.hash(c, len), c);
    FILE* ff = std::fopen((a[3] + ".functions.tsv").c_str(), "w");
    std::map<std::pair<u32, u32>, u32> pairs;
    size_t found = 0, many = 0, none = 0;
    for (const Fn& f : fns) {
        auto& m = by_len[f.words];
        const auto [lo, hi] = m.equal_range(rc.hash(f.start, f.words));
        std::vector<u32> at;
        for (auto it = lo; it != hi; ++it) at.push_back(it->second);
        if (at.size() == 1) {
            ++found;
            for (u32 k = 0; k < f.words; ++k) {
                const u32 x = rc.address_at(f.start + 4 * k), y = dc.address_at(at[0] + 4 * k);
                if (x && y) ++pairs[{x, y}];
            }
        } else if (at.empty()) ++none;
        else ++many;
        std::fprintf(ff, "%06X\t%u\t%zu\t", f.start, f.words, at.size());
        if (at.size() == 1) std::fprintf(ff, "%06X\n", at[0]);
        else std::fprintf(ff, "-\n");
    }
    std::fclose(ff);
    FILE* fd = std::fopen((a[3] + ".data.tsv").c_str(), "w");
    for (const auto& [p, n] : pairs) std::fprintf(fd, "%06X\t%06X\t%u\n", p.first, p.second, n);
    std::fclose(fd);
    std::printf("%zu routines: %zu found once, %zu in several places, %zu not found; %zu data address pairs\n", fns.size(), found, many, none, pairs.size());
    return 0;
}

} // namespace

int cmd_regions(Disc& disc, const Args& a) {
    if (!a.empty() && a[0] == "map") return map(disc, a);
    std::fprintf(stderr, "usage: gt4tool regions <disc> map <reference image> <functions.tsv> <out prefix>\n");
    return 2;
}

} // namespace gt4r::tool
