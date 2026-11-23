// gt4tool's computer drivers' commands:
//
//   gt4tool ai <disc> <course> [cars] [laps]   a field of computer drivers run headless: each car's lap times and
//                                              incidents (wall and car contacts, spins, off the road). [cars]: a number
//                                              (of a default field) or labels joined by commas; [laps] 3.
//   gt4tool ai <disc> lines <course> [line]    the course's driver file: its lines as the race prepares them (with
//                                              [line] every point), its driver sets
//   gt4tool ai <disc> survey                   every course's driver file: its layout, lines and sets

#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/session.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace gt4r::tool {

namespace {

using Args = std::vector<std::string>;

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t a = 0;
    while (a <= s.size()) {
        const size_t b = s.find(',', a);
        const std::string part = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (!part.empty()) out.push_back(part);
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

// A field of small cars of different classes (the grid of a Tsukuba race of the original).
const std::vector<std::string> kDefaultField = {"_1100_standard_type11_49", "z_act_70", "n360_67", "midget2_d_type_98", "s500_63", "ka_01",
                                                "civic_r_98", "integra_r_98", "nsx_r_02", "lancer_evo8_gsr_03"};

std::string clock(double s) {
    char b[32];
    const int m = int(s / 60);
    std::snprintf(b, sizeof b, "%d'%06.3f", m, s - 60.0 * m);
    return b;
}

int lines(Disc& disc, const Args& a) {
    const std::optional<course::AiData> data = sim::ai::Field::read(disc, a[1]);
    if (!data) {
        std::printf("no driver file crs/%s.ad\n", a[1].c_str());
        return 1;
    }
    std::printf("%s: %s points\n", a[1].c_str(), data->format == course::AiData::Format::Floats ? "ADLN (0x40-byte)" : "fixed point (0x28-byte)");
    for (int c = 0; c < course::AiData::kClasses; ++c) {
        std::printf("set %d:", c);
        if (!data->sets[size_t(c)]) std::printf(" none");
        else
            for (int i = 0; i < 0x30; ++i) std::printf(" %d", (*data->sets[size_t(c)])[size_t(i)]);
        std::printf("\n");
    }
    const std::vector<std::string> one = {"ka_01"};
    std::unique_ptr<sim::Session> session = sim::Session::open(disc, a[1], one);
    const sim::Program program = sim::Program::load(disc);
    const sim::ai::Tables tables = sim::ai::Tables::load(program);
    const sim::ai::CourseLines cl(*data, session->ground(), tables);
    std::printf("lap %.1f m\n", double(cl.lap()));
    const int only = a.size() > 2 ? std::atoi(a[2].c_str()) : -1;
    for (int k = 0; k < 8; ++k) {
        const auto& pts = cl.line(k);
        std::printf("line %d: %zu points (file %zu)", k, pts.size(), data->lines[size_t(k)].size());
        if (!pts.empty()) {
            // How well each stretch's end, evaluated, meets the next point.
            float worst = 0;
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                float x, y, h;
                if (cl.evaluate(k, int(i + 1), pts[i + 1].dist - 0.01f, x, y, h)) worst = std::max(worst, std::hypot(x - pts[i + 1].x, y - pts[i + 1].y));
            }
            std::printf(", distances %.1f .. %.1f, stretch ends within %.2f m", double(pts.front().dist), double(pts.back().dist), double(worst));
        }
        std::printf("\n");
        if (k == only)
            for (size_t i = 0; i < pts.size(); ++i) {
                const auto& p = pts[i];
                std::printf("  %3zu kind %d b %4d v %4d  x %8.2f y %8.2f h %6.2f  surf %2d%s dist %7.1f  r %8.2f  head %6.3f  slope %6.3f bank %6.3f  len %6.1f%s\n",
                            i, p.kind, p.brake_adjust, p.speed_adjust, double(p.x), double(p.y), double(p.height), p.surface, p.pit ? " pit" : "    ",
                            double(p.dist), double(p.radius), double(p.heading), double(p.slope), double(p.bank), double(p.length), p.crest ? " crest" : "");
            }
    }
    return 0;
}

int survey(Disc& disc) {
    int files = 0, fixed = 0;
    std::array<int, 8> present{};
    for (const std::string& name : disc.volume().list_dir("crs/")) {
        if (name.size() < 4 || name.substr(name.size() - 3) != ".ad") continue;
        const std::string course = name.substr(0, name.size() - 3);
        try {
            const std::optional<course::AiData> d = sim::ai::Field::read(disc, course);
            if (!d) {
                std::printf("%-24s not a driver file\n", course.c_str());
                continue;
            }
            ++files;
            if (d->format == course::AiData::Format::Fixed) ++fixed;
            std::printf("%-24s %s lines", course.c_str(), d->format == course::AiData::Format::Fixed ? "fixed " : "ADLN  ");
            for (int k = 0; k < 8; ++k) {
                std::printf(" %3zu", d->lines[size_t(k)].size());
                if (!d->lines[size_t(k)].empty()) ++present[size_t(k)];
            }
            int sets = 0;
            for (const auto& s : d->sets) sets += s ? 1 : 0;
            std::printf("  sets %d  class 0: brake %d corner %d look %d..%d\n", sets, d->sets[0] ? (*d->sets[0])[0] : 0, d->sets[0] ? (*d->sets[0])[1] : 0,
                        d->sets[0] ? (*d->sets[0])[7] : 0, d->sets[0] ? (*d->sets[0])[8] : 0);
        } catch (const std::exception& e) {
            std::printf("%-24s %s\n", course.c_str(), e.what());
        }
    }
    std::printf("%d files (%d fixed point); courses with line k:", files, fixed);
    for (int k = 0; k < 8; ++k) std::printf(" %d", present[size_t(k)]);
    std::printf("\n");
    return 0;
}

} // namespace

int cmd_ai(Disc& disc, const Args& a) {
    if (a.empty()) return 2;
    if (a[0] == "survey") return survey(disc);
    if (a[0] == "lines" && a.size() > 1) return lines(disc, a);
    std::vector<std::string> cars;
    if (a.size() > 1) {
        const std::string& c = a[1];
        if (!c.empty() && c.find_first_not_of("0123456789") == std::string::npos) {
            const size_t n = std::min<size_t>(std::stoul(c), kDefaultField.size());
            cars.assign(kDefaultField.begin(), kDefaultField.begin() + long(n));
        } else cars = split(c);
    } else cars.assign(kDefaultField.begin(), kDefaultField.begin() + 6);
    const int laps = a.size() > 2 ? std::max(1, std::atoi(a[2].c_str())) : 3;
    // [trace=<car>]: that car's plan every half second.
    int trace = -1;
    for (const std::string& x : a)
        if (x.rfind("trace=", 0) == 0) trace = std::atoi(x.c_str() + 6);
    auto on_step = [&](const sim::Session& s, const sim::ai::Field& f) {
        if (trace < 0 || size_t(trace) >= s.cars().size() || std::lround(s.time() * 60) % 30 != 0) return;
        const race::CarState& st = s.cars()[size_t(trace)];
        const sim::ai::Field::Snapshot o = f.snapshot(size_t(trace));
        std::printf("%7.2f s %7.1f lap %d  %5.1f km/h  line %d next %2d mode %d status %d state %d  cur %5.1f next %5.1f  thr %.2f brk %.2f steer %+.3f off %+5.2f contact %.2f follow %d lock %.3f cmd %+.3f\n",
                    s.time(), double(st.course_v), st.lap, double(st.speed) * 3.6, o.line, o.next, o.mode, o.status, o.state, double(o.v_cur) * 3.6,
                    double(o.v_next) * 3.6, double(o.throttle), double(o.brake), double(o.steer), double(o.line_offset), double(o.contact), o.follow, double(o.lock), double(o.steer_cmd));
    };
    const sim::ai::FieldRun run = sim::ai::run_field(disc, a[0], cars, laps, {}, 0, on_step);
    for (const std::string& n : run.notes) std::printf("note: %s\n", n.c_str());
    std::printf("%s, lap %.1f m, %d laps, %.1f s run\n", run.course.c_str(), double(run.lap_length), laps, run.seconds);
    std::printf("%-28s %-10s %-10s %s  %5s %4s %4s %5s %5s %6s %s\n", "car", "total", "best", "laps", "top", "wall", "car", "spins", "off", "stuck", "lanes");
    for (const sim::ai::CarRun& r : run.cars) {
        double best = 0;
        for (double l : r.laps)
            if (best == 0 || l < best) best = l;
        std::printf("%-28s %-10s %-10s", r.car.c_str(), r.finished ? clock(r.total).c_str() : "-", best > 0 ? clock(best).c_str() : "-");
        for (double l : r.laps) std::printf(" %s", clock(l).c_str());
        const sim::ai::Incidents& i = r.incidents;
        std::printf("  %5.0f %4d %4d %5d %5d %6.1f %d\n", double(r.top_speed) * 3.6, i.wall_hits, i.car_hits, i.spins, i.off_road, double(i.stuck_seconds), i.lane_changes);
    }
    return 0;
}

} // namespace gt4r::tool
