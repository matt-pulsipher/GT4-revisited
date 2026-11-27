// gt4tool car-scripts: the car model sets that carry a script (docs/formats/model.md "What a car's script reads").
//
//   gt4tool car-scripts <disc> [folder...]
//       every car file of car/lod, car/menu and car/open (or the folders given): each model set with a script, the
//       game values it reads, its named registers, and which registers move when the values do (the script run with
//       the time going on, the brake on and off, the engine speed, the steering, the pedals swept).

#include "gt4r/disc/disc.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/formats/model/model_script.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace gt4r::tool {

namespace {

float bits_f(u32 v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

void list_program(ByteSpan b, size_t start, const mdls::Script& s) {
    auto name_of = [&](u32 cell) {
        for (const auto& v : s.registers)
            if (cell >= v.cell && cell < v.cell + v.cells) return v.name + (cell > v.cell ? "+" + std::to_string(cell - v.cell) : "");
        return std::string("r") + std::to_string(cell);
    };
    auto f = [&](size_t o) { return bits_f(rd32(b, o)); };
    auto s16_at = [&](size_t o) { return s64(s16(rd16(b, o))); };
    size_t pc = start, last = start;
    int depth = 0;
    while (pc < b.size()) {
        const u8 op = b[pc];
        const size_t a = pc + 1;
        std::printf("  %06zx %*s%02X", pc, 2 * depth, "", op);
        size_t next;
        if (op == 0x00) {
            std::printf(" end\n");
            if (pc >= last) return;
            next = a;
        } else if (op == 0x06 || op == 0x4D) {
            const u32 n = b[a + 2];
            std::printf(" switch %s %u:", op == 0x06 ? ("callback " + std::to_string(rd16(b, a))).c_str() : name_of(rd16(b, a)).c_str(), n);
            for (u32 k = 0; k < n; ++k) {
                const size_t t = size_t(s64(a + 3 + 2 * k) + s16_at(a + 3 + 2 * k));
                std::printf(" %zx", t), last = std::max(last, t);
            }
            std::printf("\n");
            next = a + 3 + 2 * size_t(n);
        } else if (op == 0x07) {
            const u32 n = b[a + 16];
            std::printf(" lod %u:", n);
            for (u32 k = 0; k < n; ++k) {
                const size_t t = size_t(s64(a + 17 + 2 * k) + s16_at(a + 17 + 2 * k));
                std::printf(" %zx", t), last = std::max(last, t);
            }
            std::printf("\n");
            next = a + 17 + 2 * size_t(n);
        } else if (op == 0x08 || op == 0x09) {
            const size_t t = op == 0x08 ? size_t(s64(a) + std::int8_t(b[a])) : size_t(s64(a) + s16_at(a));
            std::printf(" jump %zx\n", t), last = std::max(last, t);
            next = a + (op == 0x08 ? 1 : 2);
        } else if (op == 0x0A) {
            const size_t skip = a + 1 + 12 * size_t(b[a]);
            const size_t t = size_t(s64(skip) + s16_at(skip));
            std::printf(" box cull -> %zx\n", t), last = std::max(last, t);
            next = skip + 2;
        } else {
            const int n = mdls::command_operand_bytes(op);
            if (n < 0) {
                std::printf(" ?\n");
                return;
            }
            if (op == 0x0C) std::printf(" push"), ++depth;
            else if (op == 0x0D) std::printf(" pop"), depth = std::max(0, depth - 1);
            else if (op == 0x04) std::printf(" shape %u", b[a]);
            else if (op == 0x05) std::printf(" shape %u", rd16(b, a));
            else if (op == 0x35) std::printf(" shapes %u+%u", rd16(b, a), rd16(b, a + 2));
            else if (op == 0x11 || op == 0x12) std::printf(" %s %.4f %.4f %.4f", op == 0x11 ? "translate" : "scale", f(a), f(a + 4), f(a + 8));
            else if (op == 0x13) std::printf(" rotate %.4f %.4f %.4f %.4f", f(a), f(a + 4), f(a + 8), f(a + 12));
            else if (op >= 0x14 && op <= 0x16) std::printf(" rotate %c %.4f", 'X' + (op - 0x14), f(a));
            else if (op == 0x0F || op == 0x10) {
                std::printf(" %s", op == 0x0F ? "load" : "multiply");
                for (int k = 0; k < 16; ++k) std::printf(" %.3f", f(a + 4 * size_t(k)));
            } else if (op >= 0x44 && op <= 0x4C) std::printf(" by %s", name_of(rd16(b, a)).c_str());
            else if (op == 0x54) std::printf(" colour scale by %s %s %s %s", name_of(rd16(b, a)).c_str(), name_of(rd16(b, a + 2)).c_str(),
                                             name_of(rd16(b, a + 4)).c_str(), name_of(rd16(b, a + 6)).c_str());
            else if (op == 0x43) std::printf(" script");
            std::printf("\n");
            next = a + size_t(n);
        }
        pc = next;
    }
}

int show_set(Disc& disc, const std::string& path, size_t which) {
    const gt3r::Volume::Entry* e = disc.volume().find(path);
    if (!e) return std::printf("no %s\n", path.c_str()), 1;
    const Bytes file = disc.volume().read(*e);
    const std::vector<size_t> at = mdls::find_model_sets(file);
    if (which >= at.size()) return std::printf("%zu sets\n", at.size()), 1;
    const ByteSpan set = ByteSpan(file).subspan(at[which]);
    const mdls::Header h = mdls::read_header(set);
    const mdls::Script s = mdls::Script::parse(set);
    std::printf("%u models, %u shapes; values:", h.models, h.shapes);
    for (const auto& v : s.host) std::printf(" %s", v.name.c_str());
    std::printf("\nregisters:");
    for (const auto& v : s.registers) std::printf(" %s@%u", v.name.c_str(), v.cell);
    std::printf("\n");
    for (u32 i = 0; i < h.models; ++i) {
        const u32 program = rd32(set, h.model_table + 0x28 * size_t(i) + 0x18);
        if (i >= s.models.size()) { // (a set without a script: model 0's program, the body's)
            std::printf("model %u: program %x\n", i, program);
            if (i == 0 && program && program < h.size) list_program(set.first(h.size), program, s);
            continue;
        }
        std::printf("model %u: setup %d tick %d draw %d, program %x\n", i, s.models[i].setup, s.models[i].tick, s.models[i].draw, program);
        if (program && program < h.size && (s.models[i].draw >= 0 || s.models[i].tick >= 0)) list_program(set.first(h.size), program, s);
    }
    mdls::ScriptState st(s);
    for (const auto& m : s.models)
        if (m.setup >= 0) mdls::run_script(s, m.setup, st);
    for (const float rpm : {600.0f, 3000.0f})
        for (const int pedal : {0, 1, 2}) // none, the left, the right
            for (int step = 0; step <= 12; ++step) {
                for (const auto& v : s.host) {
                    const int c = s.host_cell(v.name);
                    if (v.name == "time") st.set_host(c, u32(step * 16));
                    else if (v.name == "engineRPM") st.set_host_f(c, rpm);
                    else st.set_host_f(c, (v.name == "pedalLeft" && pedal == 1) || (v.name == "pedalRight" && pedal == 2) ? 1.0f : 0.0f);
                }
                for (const auto& m : s.models) {
                    if (m.tick >= 0) mdls::run_script(s, m.tick, st);
                    if (m.draw >= 0) mdls::run_script(s, m.draw, st);
                }
                std::printf("%4.0f rpm, %s, %4d ms:", rpm, pedal == 0 ? "no pedal" : pedal == 1 ? "left pedal" : "right pedal", step * 16);
                for (const auto& v : s.registers) std::printf(" %s %.4g", v.name.c_str(), st.register_f(v.cell));
                std::printf("\n");
            }
    return 0;
}

} // namespace

int cmd_carscripts(Disc& disc, const std::vector<std::string>& args) {
    if (args.size() >= 2 && args[0] == "--show") return show_set(disc, args[1], args.size() > 2 ? std::stoul(args[2]) : 0);
    std::vector<std::string> folders(args.begin(), args.end());
    if (folders.empty()) folders = {"car/lod", "car/menu", "car/open"};
    std::map<std::string, int> host_count, reg_kind_count;
    int files = 0, sets = 0, scripted = 0;
    std::set<std::string> models_with;
    for (const std::string& folder : folders) {
        for (const std::string& name : disc.volume().list_dir(folder)) {
            if (name.find('.') != std::string::npos || name.empty() || name.back() == '/') continue;
            const gt3r::Volume::Entry* e = disc.volume().find(folder + "/" + name);
            if (!e) continue;
            const Bytes f = disc.volume().read(*e);
            ++files;
            std::vector<size_t> at;
            try {
                at = mdls::find_model_sets(f);
            } catch (const std::exception&) {
                continue;
            }
            for (size_t k = 0; k < at.size(); ++k) {
                ++sets;
                mdls::Script s;
                try {
                    s = mdls::Script::parse(ByteSpan(f).subspan(at[k]));
                } catch (const std::exception&) {
                    continue;
                }
                if (s.empty()) continue;
                ++scripted;
                models_with.insert(name);
                std::string hosts, regs;
                for (const auto& v : s.host) hosts += (hosts.empty() ? "" : ",") + v.name, ++host_count[v.name];
                // Which registers move: the script run over 3 s with each value swept.
                mdls::ScriptState st(s);
                for (const auto& m : s.models)
                    if (m.setup >= 0) mdls::run_script(s, m.setup, st);
                std::vector<std::set<u32>> seen(s.register_cells);
                for (int step = 0; step <= 180; ++step) {
                    const float u = float(step) / 180.0f;
                    for (const auto& v : s.host) {
                        const int c = s.host_cell(v.name);
                        if (v.name == "time") st.set_host(c, u32(step * 16));
                        else if (v.name == "timezone") st.set_host(c, u32(step % 3));
                        else if (v.name == "engineRPM") st.set_host_f(c, 1000 + 7000 * u);
                        else if (v.name == "brake" || v.name == "taillamp" || v.name == "pedalLeft") st.set_host_f(c, (step / 10) % 2 ? 1.0f : 0.0f);
                        else if (v.name == "steering" || v.name == "steering_rot") st.set_host_f(c, std::sin(u * 6.283f));
                        else st.set_host_f(c, u);
                    }
                    for (const auto& m : s.models) {
                        if (m.tick >= 0) mdls::run_script(s, m.tick, st);
                        if (m.draw >= 0) mdls::run_script(s, m.draw, st);
                    }
                    for (u32 r = 0; r < s.register_cells && r < st.registers.size(); ++r) seen[r].insert(st.registers[r]);
                }
                std::map<std::string, std::pair<int, int>> kinds; // name stem -> (count, moving)
                for (const auto& v : s.registers) {
                    std::string stem = v.name;
                    while (!stem.empty() && std::isdigit(static_cast<unsigned char>(stem.back()))) stem.pop_back();
                    auto& kc = kinds[stem];
                    ++kc.first;
                    if (v.cell < seen.size() && seen[v.cell].size() > 1) ++kc.second;
                }
                for (const auto& [stem, n] : kinds) {
                    char b[96];
                    std::snprintf(b, sizeof b, "%s%s x%d (%d moving)", regs.empty() ? "" : ", ", stem.c_str(), n.first, n.second);
                    regs += b;
                    ++reg_kind_count[stem];
                }
                std::printf("%-10s %-24s set %zu  reads [%s]  writes %s\n", folder.c_str(), name.c_str(), k, hosts.c_str(), regs.c_str());
            }
        }
    }
    std::printf("\n%d files, %d model sets, %d with a script (%zu car models)\n", files, sets, scripted, models_with.size());
    std::printf("values read:");
    for (const auto& [n, c] : host_count) std::printf(" %s %d,", n.c_str(), c);
    std::printf("\nregisters:");
    for (const auto& [n, c] : reg_kind_count) std::printf(" %s %d,", n.c_str(), c);
    std::printf("\n");
    return 0;
}

} // namespace gt4r::tool
