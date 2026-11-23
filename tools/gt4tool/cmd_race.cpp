// gt4tool race: the race view's data, for checks from the command line.
//
//   gt4tool race <disc> sections <course>          the course file's sections
//   gt4tool race <disc> rail <course> [seconds]    runs the stand-in simulation on the course's road

#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/race/rail_simulation.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace gt4r;

namespace {

using Args = std::vector<std::string>;

Bytes read_course(Disc& disc, const std::string& name) {
    const std::string path = name.rfind("crs/", 0) == 0 ? name : "crs/" + name;
    const gt3r::Volume::Entry* e = disc.volume().find(path);
    if (!e) throw FormatError("no file " + path);
    Bytes data = disc.volume().read(*e);
    if (!course::CourseFile::looks_like_course(data)) throw FormatError(path + " is not a course");
    return data;
}

int race_sections(Disc& disc, const Args& a) {
    const Bytes data = read_course(disc, a[1]);
    const course::CourseFile file = course::CourseFile::parse(data);
    for (const course::Section& s : file.sections()) std::printf("%2d  %-4s  0x%08x  %8u bytes\n", s.slot, s.tag.c_str(), s.offset, s.size);
    return 0;
}

int race_rail(Disc& disc, const Args& a) {
    const Bytes data = read_course(disc, a[1]);
    const course::CourseFile file = course::CourseFile::parse(data);
    const course::Runway road = course::Runway::parse(file.section(data, course::kSlotRunway));
    const float seconds = a.size() > 2 ? std::strtof(a[2].c_str(), nullptr) : 120.0f;
    std::vector<race::RailCar> specs(6);
    race::RailSimulation sim(road, specs);
    const race::RailLine& line = sim.line();
    std::printf("lap %.1f m, %zu line points, %zu grid places\n", road.length, line.points.size(), road.grid.size());
    if (!line.valid()) return 1;
    std::vector<race::CarInput> inputs(specs.size());
    float max_travel = 0, top = 0;
    std::vector<float> lap_start(specs.size(), -1);
    for (int i = 0; i < int(seconds * 60); ++i) {
        for (size_t c = 0; c < specs.size(); ++c) inputs[c] = race::follow_line(line, sim.cars()[c], c & 1 ? 2.0f : -2.0f, 1.0f - 0.04f * float(c));
        const int lap0 = sim.cars()[0].lap;
        sim.step(inputs, 1.0f / 60);
        const race::CarState& s = sim.cars()[0];
        if (s.lap != lap0) {
            if (lap_start[0] >= 0) std::printf("  lap %d: %.2f s\n", s.lap, float(i) / 60 - lap_start[0]);
            lap_start[0] = float(i) / 60;
        }
        top = std::max(top, s.speed);
        for (const race::CarState& c : sim.cars())
            for (const race::WheelState& w : c.wheels)
                if (std::fabs(w.travel) > max_travel) {
                    max_travel = std::fabs(w.travel);
                    if (max_travel > 0.25f) std::printf("  travel %.2f m at %.0f m along (t = %.1f s)\n", w.travel, c.course_v, float(i) / 60);
                }
        if (i % 600 == 0)
            std::printf("t %5.1f  v %7.1f m  lap %d  %5.1f km/h  gear %d  %4.0f rpm  at (%.1f, %.1f, %.1f)\n", float(i) / 60, s.course_v, s.lap,
                        s.speed * 3.6f, s.gear, s.engine_rpm, s.position.x, s.position.y, s.position.z);
    }
    std::printf("top speed %.1f km/h, largest suspension travel %.3f m\n", top * 3.6f, max_travel);
    return 0;
}

} // namespace

int cmd_race(Disc& disc, const Args& a) {
    if (a[0] == "sections") return race_sections(disc, a);
    if (a[0] == "rail") return race_rail(disc, a);
    std::fprintf(stderr, "race: unknown request %s\n", a[0].c_str());
    return 2;
}
