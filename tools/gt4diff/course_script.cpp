// A course's animated models against the original's saved states:
//
//   gt4r_diff <disc> course_script <course> <eeMemory.bin>...
//
// The state holds the scenery set's registers as the models' draw scripts left them, each at the frame its model was last
// drawn. For each script whose registers do not hang on the cars, ours is run at the state's time and at every step
// before it, back to the start: a match is the same value in every register it writes (integers the same, floats within 0.0001).

#include "orig.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/model/model_script.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>

namespace gt4r::diff {

namespace {

// (The states were made on an emulator whose divisions round their own way: the last bits are not judged.)
bool close(u32 a, u32 b) {
    float x, y;
    std::memcpy(&x, &a, 4), std::memcpy(&y, &b, 4);
    return a == b || (std::fabs(x) > 1e-6f && std::fabs(x - y) < 1e-4f);
}

} // namespace

int run_course_script(Disc& disc, const std::string& course, const std::vector<std::string>& paths) {
    const gt3r::Volume::Entry* e = disc.volume().find("crs/" + course);
    if (!e) return std::printf("no course %s\n", course.c_str()), 1;
    const Bytes data = disc.volume().read(*e);
    const course::CourseFile file = course::CourseFile::parse(data);
    const ByteSpan set = file.section(data, course::kSlotWorld);
    const mdls::Script script = mdls::Script::parse(set);
    if (script.empty()) return std::printf("no script\n"), 1;
    const auto far = [](u32, std::span<const u32>) { return 1000.0f; };
    bool asked = false;
    const auto ask = [&](u32, std::span<const u32>) { return asked = true, 1000.0f; };
    int failed = 0;
    for (const std::string& path : paths) {
        std::ifstream in(path, std::ios::binary);
        const std::vector<char> m((std::istreambuf_iterator<char>(in)), {});
        const auto w = [&](size_t a) {
            u32 v = 0;
            if (a + 4 <= m.size()) std::memcpy(&v, m.data() + a, 4);
            return v;
        };
        // The set in memory: the same header as the file's, its pointers filled in.
        size_t at = 0;
        for (size_t a = 0x100000; a + 0x80 < m.size(); a += 16)
            if (!std::memcmp(m.data() + a, "MDLS", 4) && !std::memcmp(m.data() + a + 0x10, set.data() + 0x10, 0x20)) {
                at = a;
                break;
            }
        if (!at) {
            std::printf("%s: the set is not in it\n", path.c_str());
            ++failed;
            continue;
        }
        const size_t instance = w(at + 0x7C);
        const size_t registers = w(instance + 4), host = w(instance + 12);
        const s32 now = s32(w(host + 4 * size_t(std::max(script.host_cell("time"), 0))));
        mdls::ScriptState start(script);
        for (const mdls::Script::Entries& x : script.models)
            if (x.setup >= 0) mdls::run_script(script, x.setup, start, far);
        size_t same = 0, other = 0, skipped = 0, moving = 0;
        std::string which;
        for (size_t i = 0; i < script.models.size(); ++i) {
            const s32 entry = script.models[i].draw;
            if (entry < 0) continue;
            // The cells it writes: those that differ from both fillings at some time.
            std::set<u32> cells;
            asked = false;
            for (s32 t = 0; t < 16000; t += 160)
                for (u32 fill : {0u, 0x7F000001u}) {
                    mdls::ScriptState s = start;
                    for (u32& r : s.registers) r = fill;
                    s.set_host(script.host_cell("time"), u32(t));
                    s.set_host_f(script.host_cell("deltaT"), 1.0f / 60);
                    mdls::run_script(script, entry, s, ask);
                    for (u32 c = 0; c < s.registers.size(); ++c)
                        if (s.registers[c] != fill) cells.insert(c);
                }
            if (asked || cells.empty()) {
                ++skipped;
                continue;
            }
            bool moves = false;
            for (const mdls::Script::Variable& v : script.registers) moves |= cells.count(v.cell) && v.name.rfind("mv", 0) == 0;
            bool drawn = false, found = false;
            for (u32 c : cells) drawn |= w(registers + 4 * c) != 0;
            s32 when = -1, best_t = -1;
            size_t best = 0;
            std::vector<u32> best_cells;
            for (s32 t = now; t >= 0 && !found && drawn; t -= 16) {
                mdls::ScriptState s = start;
                s.set_host(script.host_cell("time"), u32(t));
                s.set_host_f(script.host_cell("deltaT"), 1.0f / 60);
                mdls::run_script(script, entry, s, far);
                size_t equal = 0;
                for (u32 c : cells) equal += close(s.registers[c], w(registers + 4 * c));
                found = equal == cells.size();
                if (equal > best) best = equal, best_t = t, best_cells = s.registers;
                when = t;
            }
            if (!drawn) ++skipped;
            else if (found) {
                ++same, moving += moves;
                if (now - when > 1000 && std::getenv("GT4R_SCRIPT_SHOW")) std::printf("  model %zu: drawn %d ms before\n", i, now - when);
            } else {
                ++other, which += " " + std::to_string(i);
                if (std::getenv("GT4R_SCRIPT_SHOW")) {
                    std::printf("  model %zu: the nearest at %d ms, %zu of %zu:", i, best_t, best, cells.size());
                    for (u32 c : cells) {
                        std::string name = "r" + std::to_string(c);
                        for (const mdls::Script::Variable& v : script.registers)
                            if (v.cell == c) name = v.name;
                        const u32 x = w(registers + 4 * c);
                        float f;
                        std::memcpy(&f, &x, 4);
                        float ours;
                        std::memcpy(&ours, &best_cells[c], 4);
                        std::printf(" %s=%d/%g (ours %d/%g)", name.c_str(), s32(x), double(f), s32(best_cells[c]), double(ours));
                    }
                    std::printf("\n");
                }
            }
        }
        std::printf("%s: time %d ms: %zu models' registers as ours (%zu of them walk), %zu not (%s), %zu not drawn or asking about the cars\n", path.c_str(), now, same, moving, other,
                    which.c_str(), skipped);
        failed += other != 0;
    }
    return failed ? 1 : 0;
}

} // namespace gt4r::diff
