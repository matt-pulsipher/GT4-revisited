// gt4tool car-scripts: the car model sets that carry a script (docs/formats/model.md "What a car's script reads").
//
//   gt4tool car-scripts <disc> [folder...]
//       every car file of car/lod, car/menu and car/open (or the folders given): each model set with a script, the
//       game values it reads, its named registers, and which registers move when the values do (the script run with
//       the time going on, the brake on and off, the engine speed, the steering, the pedals swept).

#include "gt4r/disc/disc.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/formats/model/model_script.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace gt4r::tool {

int cmd_carscripts(Disc& disc, const std::vector<std::string>& args) {
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
