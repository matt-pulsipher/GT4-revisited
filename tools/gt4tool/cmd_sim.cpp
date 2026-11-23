// gt4tool's simulation commands:
//
//   gt4tool car <disc> [car label]         a car's spec as the race builds it (without a label: every car's figures)
//   gt4tool road <disc> [course]           a course's road as the simulation reads it (without a course: every course)
//   gt4tool drive <disc> <car label> <course> [seconds] [straight | careful]   puts the car on the first grid place and drives it at full
//                                          throttle along the road's middle: time, place, speed, engine speed, gear each second.
//                                          [straight]: no steering; [careful]: braking for bends; a fifth argument
//                                          prints the car's record at the end (driveline, wheels, suspension)

#include "base/log.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/db/car_parts.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/ground.h"
#include "gt4r/sim/line_driver.h"
#include "gt4r/sim/session.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <span>
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

// A course's road by its name ("tsukuba": the file crs/tsukuba).
std::optional<course::Runway> open_road(Disc& disc, const std::string& name) {
    const gt3r::Volume::Entry* e = disc.volume().find("crs/" + name);
    if (!e) return std::nullopt;
    const Bytes data = disc.volume().read(*e);
    if (!course::CourseFile::looks_like_course(data)) return std::nullopt;
    const course::CourseFile file = course::CourseFile::parse(data);
    const ByteSpan section = file.section(data, course::kSlotRunway);
    if (!course::Runway::is_runway(section)) return std::nullopt;
    return course::Runway::parse(section);
}

void road_report(const std::string& name, const course::Runway& r, bool detail) {
    const sim::RoadGround ground(r);
    std::map<int, size_t> materials, flags;
    size_t up = 0, down = 0;
    for (const course::Runway::Triangle& t : r.triangles) {
        ++materials[t.bits & 0x1F], ++flags[t.kind];
        const course::Vec3 &a = r.vertices[t.v[0]].pos, &b = r.vertices[t.v[1]].pos, &c = r.vertices[t.v[2]].pos;
        const float ny = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
        ++(ny > 0 ? up : down);
    }
    size_t grid_ok = 0, mid_ok = 0, v_ok = 0;
    for (const course::Runway::Place& p : r.grid) {
        sim::GroundHit h;
        grid_ok += ground.under(p.pos, h);
    }
    float worst = 0;
    for (const course::Runway::Checkpoint& c : r.checkpoints) {
        sim::GroundHit h;
        mid_ok += ground.under(c.middle, h);
        float v = 0;
        if (ground.distance_along(c.middle, v)) {
            float d = std::fabs(v - c.v);
            if (r.length > 0) d = std::min(d, std::fabs(r.length - d));
            worst = std::max(worst, d), ++v_ok;
        }
    }
    std::printf("%-22s %7.1f m  %5zu triangles (%zu up, %zu down)  grid %zu/%zu on the road  checkpoints %zu/%zu on the road, %zu placed (worst %.2f m)  materials",
                name.c_str(), double(r.length), r.triangles.size(), up, down, grid_ok, r.grid.size(), mid_ok, r.checkpoints.size(), v_ok, double(worst));
    for (const auto& [m, n] : materials) std::printf(" %d:%zu", m, n);
    std::printf("  flags");
    for (const auto& [f, n] : flags) std::printf(" %02x:%zu", unsigned(f), n);
    std::printf("\n");
    if (!detail) return;
    std::printf("  start %.1f goal %.1f; checkpoints:", double(r.start_v), double(r.goal_v));
    for (size_t i = 0; i < r.checkpoints.size(); ++i)
        if (i < 4 || i + 4 >= r.checkpoints.size() || (i > 0 && r.checkpoints[i].v < r.checkpoints[i - 1].v))
            std::printf(" [%zu] v %.1f (%.1f %.1f %.1f)", i, double(r.checkpoints[i].v), double(r.checkpoints[i].middle.x), double(r.checkpoints[i].middle.y),
                        double(r.checkpoints[i].middle.z));
    std::printf("\n");
    for (size_t i = 0; i < r.checkpoints.size(); ++i) {
        const course::Runway::Checkpoint& c = r.checkpoints[i];
        if (std::fabs(c.v - r.checkpoints[0].v) > 400 && c.v > 200 && c.v < r.length - 200) continue;
        std::printf("  checkpoint %zu v %.1f left %.1f %.1f %.1f middle %.1f %.1f %.1f right %.1f %.1f %.1f\n", i, double(c.v), double(c.left.x), double(c.left.y),
                    double(c.left.z), double(c.middle.x), double(c.middle.y), double(c.middle.z), double(c.right.x), double(c.right.y), double(c.right.z));
    }
    for (size_t i = 0; i < r.grid.size(); ++i) {
        sim::GroundHit h;
        const bool ok = ground.under(r.grid[i].pos, h);
        std::printf("  grid %zu: %.2f %.2f %.2f heading %.3f  ground %s %.3f material %d flags %02x normal %.3f %.3f %.3f\n", i, double(r.grid[i].pos.x),
                    double(r.grid[i].pos.y), double(r.grid[i].pos.z), double(r.grid[i].heading), ok ? "at" : "none", double(h.point.y), h.material, h.flags,
                    double(h.normal.x), double(h.normal.y), double(h.normal.z));
    }
}

int cmd_road(Disc& disc, const Args& a) {
    if (!a.empty()) {
        const std::optional<course::Runway> r = open_road(disc, a[0]);
        if (!r) {
            std::printf("no road in crs/%s\n", a[0].c_str());
            return 1;
        }
        road_report(a[0], *r, true);
        return 0;
    }
    for (const std::string& name : disc.volume().list_dir("crs/")) {
        if (name.empty() || name.back() == '/' || name.find('.') != std::string::npos) continue;
        try {
            if (const std::optional<course::Runway> r = open_road(disc, name)) road_report(name, *r, false);
        } catch (const std::exception& e) {
            std::printf("%-22s %s\n", name.c_str(), e.what());
        }
    }
    return 0;
}

int cmd_drive(Disc& disc, const Args& a) {
    db::SpecDb sdb = open_db(disc);
    const sim::Program program = sim::Program::load(disc);
    std::optional<course::Runway> road = open_road(disc, a[1]);
    if (!road) {
        std::printf("no road in crs/%s\n", a[1].c_str());
        return 1;
    }
    sim::Entry entry;
    if (!sim::stock_car(sdb, program, a[0], entry.equipment)) {
        std::printf("no car %s\n", a[0].c_str());
        return 1;
    }
    const double seconds = a.size() > 2 ? std::atof(a[2].c_str()) : 30.0;
    const bool no_steer = a.size() > 3 && a[3] == "straight", careful = a.size() > 3 && a[3] == "careful";
    sim::Session session(sdb, program, std::make_shared<const course::Runway>(std::move(*road)), std::span<const sim::Entry>(&entry, 1));
    for (const std::string& n : session.notes()) std::printf("note: %s\n", n.c_str());
    print_figures(sdb, program, a[0]);
    std::printf("%s course, lap %.1f m\n", session.loose() ? "loose" : "paved", double(session.ground().road().length));
    std::printf("%7s %9s %8s %9s %9s %7s %6s %4s %8s\n", "time", "x", "y", "z", "along", "km/h", "rpm", "gear", "driven m");
    sim::LineDriver driver(session, 0);
    { // the engine revved before the start, as through a start's countdown
        race::CarInput rev;
        rev.throttle = 1;
        for (int i = 0; i < 90; ++i) session.hold_once(std::span<const race::CarInput>(&rev, 1));
    }
    double driven = 0, quarter = -1;
    const int steps = int(seconds * 60.0 + 0.5);
    for (int i = 0; i <= steps; ++i) {
        const race::CarState& car = session.cars()[0];
        if (i % 60 == 0)
            std::printf("%7.2f %9.2f %8.2f %9.2f %9.1f %7.1f %6.0f %4d %8.1f\n", session.time(), double(car.position.x), double(car.position.y),
                        double(car.position.z), double(car.course_v) + double(car.lap) * double(session.ground().road().length), double(car.speed) * 3.6,
                        double(car.engine_rpm), car.gear, driven);
        race::CarInput in;
        in.throttle = 1;
        if (!no_steer) in = driver.drive(1.0f, careful);
        session.step_once(std::span<const race::CarInput>(&in, 1));
        driven += double(std::fabs(session.cars()[0].speed)) * double(sim::Session::kStep);
        if (quarter < 0 && driven >= 402.336) quarter = session.time();
    }
    if (quarter >= 0) std::printf("standing 400 m: %.2f s\n", quarter);
    if (a.size() > 4) { // the record's driveline, for study
        const gt3r::sim::exact::Car& c = session.record(0);
        std::printf("clutch %d lockout %d gears %d up %d rev limit %d idle %d layout %d kind %d hold %d throttle %.2f pedal %.2f\n", c.clutch, c.shift_lockout,
                    c.p.d.gears, c.p.d.up_rpm, c.p.e.rev_limit, c.p.e.idle, c.p.d.layout, c.p.d.kind, c.hold, double(c.throttle.host()),
                    double(c.throttle_pedal.host()));
        for (int g = 0; g <= c.p.d.gears && g < 8; ++g) std::printf(" gear %d speed factor %.3f\n", g, double(c.p.d.gear_speed[size_t(g)].host()));
        for (int w = 0; w < 4; ++w) {
            const gt3r::sim::exact::Wheel& wh = c.w[size_t(w)];
            std::printf(" wheel %d load %.0f peak %.0f slip %.4f gain %.3f optimum %.4f surface %d susp %.4f ground %.3f hub z %.3f rim %.2f hub v %.2f flags %x miss %d\n", w,
                        double(wh.load.host()), double(wh.peak_force.host()), double(wh.slip_ratio.host()), double(wh.slip_gain.host()),
                        double(gt3r::sim::exact::tire_record(c.p, w, wh.surface).slip_drive.host()), wh.surface, double(wh.susp.host()),
                        double(wh.ground.host()), double(c.pos.z.host() + wh.hub.z.host()), double(wh.rim_speed.host()), double(wh.hub_vx.host()), wh.flags, c.g[size_t(w)].miss);
        }
        std::printf("pos z %.3f pitch %.4f roll %.4f tcs %d ascc %d torque %.1f rev limiter %d drive torque %.1f %.1f\n", double(c.pos.z.host()), double(c.pitch.host()),
                    double(c.roll.host()), c.tcs_status, c.ascc_status, double(c.torque_wot.host()), c.rev_limiter, double(c.drive_torque[0].host()),
                    double(c.drive_torque[1].host()));
        for (int ax = 0; ax < 2; ++ax) {
            const gt3r::sim::exact::Suspension& u = c.p.susp[size_t(ax)];
            std::printf(" axle %d: k %.0f k2 %.0f knee %.4f bump limit %.4f droop %.4f static %.4f offset %.4f preload %.0f bump damping %.0f anti-roll %.0f damper b %.3f %.1f %.1f r %.3f %.1f %.1f  pitch lever %.6f roll lever %.6f pitch tire %.6f\n",
                        ax, double(u.k.host()), double(u.k2.host()), double(u.knee.host()), double(u.bump_limit.host()), double(u.droop.host()),
                        double(u.x_static.host()), double(u.offset.host()), double(u.preload.host()), double(u.bump_damping.host()), double(u.anti_roll.host()),
                        double(u.bump_t.host()), double(u.bump_o.host()), double(u.bump_g.host()), double(u.reb_t.host()), double(u.reb_o.host()), double(u.reb_g.host()),
                        double(c.p.pitch_lever[size_t(ax)].host()), double(c.p.roll_lever[size_t(ax)].host()), double(c.p.pitch_tire[size_t(ax)].host()));
        }
        std::printf("mass %.1f weight %.0f radius %.3f %.3f cog %.3f wheelbase %.3f track %.3f\n", double(c.p.mass.host()), double(c.p.weight.host()),
                    double(c.p.tire_radius[0].host()), double(c.p.tire_radius[1].host()), double(c.p.cog_height.host()), double(c.p.wheelbase.host()),
                    double(c.p.track.host()));
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
    {"road", "<disc> [course]", 0, cmd_road},
    {"drive", "<disc> <car label> <course> [seconds] [straight | careful]", 2, cmd_drive},
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
