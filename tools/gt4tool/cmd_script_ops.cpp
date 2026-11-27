// gt4tool script-ops: the interpreter's operators on the disc's scripts (docs/formats/script.md "Values of two kinds").
//
//   gt4tool script-ops <disc>                       each operator's uses, and the texts that look like a format with a
//                                                   float's conversion (%f, %e, %g), with the files they are in
//   gt4tool script-ops <disc> sweep [cars=N]        the script functions the Car database's pages call (the dealers' spec
//                                                   sheet, the tune shops' parts, every event's regulations) on every car:
//                                                   the script errors met, and the places where an operator or a test met
//                                                   kinds a native of ours giving another kind than the program's would
//                                                   explain (script::mixed_notes)

#include "gt4r/disc/disc.h"
#include "gt4r/menu/car_database.h"
#include "gt4r/script/adc.h"
#include "gt4r/script/vm.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace gt4tool {
gt4r::menu::FileReader disc_reader(gt4r::Disc& disc);  // cmd_menu.cpp
gt4r::menu::FolderLister disc_lister(gt4r::Disc& disc); // cmd_menu.cpp
}

namespace gt4r::tool {

namespace {

// A printf conversion of a float in `s`: %[flags][width][.precision](f|e|g|E|G).
bool float_format(const std::string& s) {
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        if (s[i] != '%') continue;
        size_t j = i + 1;
        while (j < s.size() && std::string("-+ #0123456789.").find(s[j]) != std::string::npos) ++j;
        if (j < s.size() && std::string("feEgG").find(s[j]) != std::string::npos) return true;
        if (j < s.size() && s[j] == '%') i = j;
    }
    return false;
}

int sweep(Disc& disc, size_t max_cars) {
    menu::RuntimeConfig config;
    config.list = gt4tool::disc_lister(disc);
    config.program_image = [&disc] { return disc.program_image(); };
    config.region = disc.info().region, config.product = disc.info().product;
    const std::vector<std::string> languages = menu::project_languages(config.list, disc.info().game == Game::Prologue ? "prologue" : "boot");
    if (!languages.empty()) {
        config.language = languages[0];
        if (std::find(languages.begin(), languages.end(), disc.info().language) != languages.end()) config.language = disc.info().language;
    }
    std::map<std::string, size_t> notes;
    script::mixed_notes = &notes;
    menu::CarDatabase cdb(gt4tool::disc_reader(disc), config);
    if (!cdb.error().empty()) std::printf("%s\n", cdb.error().c_str());
    std::map<std::string, std::pair<size_t, std::string>> errors; // message -> count, the first car
    size_t cars = 0, calls = 0, events = 0, allowed = 0;
    const auto attempt = [&](const char* what, const std::string& label, const std::function<void()>& run) {
        ++calls;
        try {
            run();
        } catch (const std::exception& e) {
            auto& [n, first] = errors[std::string(what) + ": " + e.what()];
            if (!n++) first = label;
        }
    };
    for (const menu::CarDatabase::Car& c : cdb.cars()) {
        if (cars >= max_cars) break;
        ++cars;
        attempt("specs", c.label, [&] { cdb.specs(c.label); });
        attempt("parts", c.label, [&] { cdb.upgrades(c.label); });
        attempt("events", c.label, [&] {
            for (const menu::CarDatabase::Event& e : cdb.events(c.label)) ++events, allowed += e.allowed;
        });
    }
    script::mixed_notes = nullptr;
    std::printf("%zu cars, %zu calls, %zu event checks (%zu allowed); %zu kinds of script error\n", cars, calls, events,
                allowed, errors.size());
    for (const auto& [what, n] : errors) std::printf("  ERROR %zux (first %s) %s\n", n.first, n.second.c_str(), what.c_str());
    std::vector<std::pair<size_t, std::string>> by_count;
    for (const auto& [where, n] : notes) by_count.push_back({n, where});
    std::sort(by_count.rbegin(), by_count.rend());
    std::printf("%zu places with mixed kinds\n", by_count.size());
    for (const auto& [n, where] : by_count) std::printf("  %8zu  %s\n", n, where.c_str());
    return errors.empty() ? 0 : 1;
}

} // namespace

int cmd_script_ops(Disc& disc, const std::vector<std::string>& args) {
    if (!args.empty() && args[0] == "sweep") {
        size_t max_cars = SIZE_MAX;
        for (const std::string& a : args)
            if (a.rfind("cars=", 0) == 0) max_cars = size_t(std::atoll(a.c_str() + 5));
        return sweep(disc, max_cars);
    }
    std::map<std::string, size_t> ops;
    std::map<std::string, std::set<std::string>> formats; // text -> files
    std::string file;
    std::function<void(const script::Code&)> walk = [&](const script::Code& c) {
        for (const script::Instruction& in : c.instructions) {
            if (in.op == script::Op::BinaryOperator || in.op == script::Op::UnaryOperator || in.op == script::Op::BinaryAssignOperator ||
                in.op == script::Op::UnaryAssignOperator)
                ++ops[std::string(script::op_name(in.op)) + " " + in.text];
            if (in.op == script::Op::StringConst && float_format(in.text)) formats[in.text].insert(file);
            if (in.body) walk(*in.body);
        }
    };
    for (const gt3r::Volume::Entry& e : disc.volume().entries()) {
        if (e.path.size() < 4 || e.path.compare(e.path.size() - 4, 4, ".adc") != 0) continue;
        const Bytes data = disc.volume().read(e);
        if (script::Script::version_of(data) != 5) continue;
        file = e.path;
        try {
            walk(script::Script::parse(data).code);
        } catch (const std::exception& ex) {
            std::printf("%s: %s\n", e.path.c_str(), ex.what());
        }
    }
    for (const auto& [name, n] : ops) std::printf("  %-34s %zu\n", name.c_str(), n);
    std::printf("texts with a float's conversion: %zu\n", formats.size());
    for (const auto& [text, files] : formats) {
        std::string esc;
        for (char c : text) esc += c == '\n' ? std::string("\\n") : std::string(1, c);
        std::printf("  \"%s\" in %zu: %s%s\n", esc.c_str(), files.size(), files.begin()->c_str(), files.size() > 1 ? ", ..." : "");
    }
    return 0;
}

} // namespace gt4r::tool
