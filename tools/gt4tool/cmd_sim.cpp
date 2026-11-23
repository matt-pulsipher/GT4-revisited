// gt4tool's simulation commands:
//
//   gt4tool car <disc> [car label]         a car's spec as the race builds it (without a label: every car's figures)
//   gt4tool road <disc> [course]           a course's road as the simulation reads it (without a course: every course)
//   gt4tool drive <disc> <car label> <course> [seconds]   puts the car on the first grid place and drives it at full
//                                          throttle along the course's middle line: time, place, speed, engine speed, gear

#include "base/log.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/db/car_parts.h"
#include "gt4r/sim/car_spec.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace gt4r::tool {

namespace {

using Args = std::vector<std::string>;

db::SpecDb open_db(Disc& disc) {
    gt3r::Volume* vol = &disc.volume();
    return db::SpecDb(
        [vol](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
}

void print_figures(db::SpecDb& sdb, const sim::Program& program, const std::string& label) {
    sim::Equipment eq;
    if (!sim::stock_car(sdb, program, label, eq)) {
        std::printf("%-34s no parts\n", label.c_str());
        return;
    }
    sim::Spec s;
    sim::build_spec(sdb, program, eq, 0, s);
    const sim::SpecFigures f = sim::spec_figures(s);
    static const char* const kDrive[] = {"FR", "FF", "4WD", "MR", "RR"};
    std::printf("%-34s %4d kg  %5.0f PS @ %5d  %5.1f kgfm @ %5d  %-3s %d gears", label.c_str(), s.weight_kg(), double(f.ps), f.power_rpm, double(f.kgfm),
                f.torque_rpm, s.drive() < 5 ? kDrive[s.drive()] : "?", s.gears());
    for (int i = 1; i <= s.gears() && i < 12; ++i) std::printf(" %.3f", double(s.h(0x138 + 2 * u32(i))) * 0.001);
    std::printf(" x %.3f  tires %.3f / %.3f m\n", double(s.h(0x150)) * 0.001, double(f.tire_radius[0]), double(f.tire_radius[1]));
}

int cmd_car(Disc& disc, const Args& a) {
    db::SpecDb sdb = open_db(disc);
    const sim::Program program = sim::Program::load(disc);
    if (!program.known()) std::printf("program: %s\n", program.issue().c_str());
    if (a.empty()) {
        for (const db::CarEntry& c : sdb.cars()) print_figures(sdb, program, c.label);
        return 0;
    }
    print_figures(sdb, program, a[0]);
    sim::Equipment eq;
    if (!sim::stock_car(sdb, program, a[0], eq)) return 1;
    sim::Spec s;
    sim::build_spec(sdb, program, eq, 0, s);
    std::printf("settings (equipment +0x110 ..):\n ");
    for (u32 o = 0x110; o < sim::Equipment::kSize; ++o) std::printf(" %02x", eq.b(o));
    std::printf("\nspec:\n");
    for (u32 o = 0; o < 0x210; o += 16) {
        std::printf("  %03x ", o);
        for (u32 i = 0; i < 16; ++i) std::printf(" %02x", s.b(o + i));
        std::printf("\n");
    }
    return 0;
}

struct Command {
    const char* name;
    const char* usage;
    size_t min_args;
    int (*run)(Disc&, const Args&);
};
const Command kCommands[] = {
    {"car", "<disc> [car label]", 0, cmd_car},
};

} // namespace

void sim_usage() {
    for (const Command& c : kCommands) std::fprintf(stderr, "  %-13s %s\n", c.name, c.usage);
}

// Runs a simulation command; -1 when argv[1] is not one of them.
int sim_main(int argc, char** argv) {
    if (argc < 3) return -1;
    for (const Command& c : kCommands) {
        if (std::strcmp(argv[1], c.name) != 0) continue;
        const Args args(argv + 3, argv + argc);
        if (args.size() < c.min_args) return -1;
        try {
            gt3r::log_set_level(gt3r::LogLevel::Warn);
            const std::unique_ptr<Disc> disc = Disc::open(argv[2]);
            return c.run(*disc, args);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
    }
    return -1;
}

} // namespace gt4r::tool
