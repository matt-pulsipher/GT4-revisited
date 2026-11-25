// gt4tool sound <disc> session-wav: a car driven by the simulation on a course, its race sound
// rendered as the Drive screen mixes it (the program's tables, a new game's levels).
//
//   gt4tool sound <disc> session-wav <label> <out.wav> <script> [incar|chase|other] [course] [only=cars|course]
//     script: phases separated by commas, each `what:seconds`:
//       hold   the brake on (the car stands)        idle  no pedal
//       full   full throttle                        half  half throttle
//       brake  the brake on while moving
//     e.g. "hold:1,idle:4,full:8": stand, idle four seconds, then launch through the gears (the
//     automatic gearbox, the aids on), as the reference recordings of the cars at Tsukuba do.
//
// `only=` keeps one part heard: `cars` (no course ambience), `course` (the ambience alone).
//
// Prints the revs, gear, speed and the engine's two levels every tenth of a second. The WAV file
// is for measuring; never commit it.

#include "base/file.h"
#include "gt4r/audio/car_sound.h"
#include "gt4r/audio/race_sound.h"
#include "gt4r/audio/race_tables.h"
#include "gt4r/audio/render.h"
#include "gt4r/audio/sim_sound_state.h"
#include "gt4r/audio/sound_library.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/sim/session.h"
#include "vfs/vfs.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace gt4r {

bool write_sound_wav(const std::string& path, const std::vector<s16>& pcm);

namespace {

struct Phase {
    std::string what;
    double seconds = 0;
};

std::vector<Phase> parse_script(const std::string& s) {
    std::vector<Phase> out;
    std::stringstream in(s);
    for (std::string item; std::getline(in, item, ',');) {
        const size_t colon = item.find(':');
        if (colon == std::string::npos) continue;
        out.push_back({item.substr(0, colon), std::atof(item.c_str() + colon + 1)});
    }
    return out;
}

} // namespace

int sound_session_wav(Disc& disc, const std::vector<std::string>& a) {
    const std::string label = a[1], out = a[2];
    const std::vector<Phase> script = parse_script(a[3]);
    const std::string view = a.size() > 4 ? a[4] : "chase";
    const std::string course_name = a.size() > 5 ? a[5] : "tsukuba";
    const std::string only = a.size() > 6 && a[6].rfind("only=", 0) == 0 ? a[6].substr(5) : std::string();
    if (script.empty()) {
        std::fprintf(stderr, "error: no phases in the script\n");
        return 2;
    }
    const gt3r::Volume::Entry* e = disc.volume().find("crs/" + course_name);
    if (!e) {
        std::fprintf(stderr, "error: no course crs/%s\n", course_name.c_str());
        return 1;
    }
    const Bytes data = disc.volume().read(*e);
    const course::CourseFile file = course::CourseFile::parse(data);
    std::optional<course::Runway> parsed = course::course_road(file, data); // (RNW4, or Prologue's GTRW)
    if (!parsed) {
        std::fprintf(stderr, "error: crs/%s has no road\n", course_name.c_str());
        return 1;
    }
    auto road = std::make_shared<const course::Runway>(std::move(*parsed));
    std::unique_ptr<sim::Session> session;
    try {
        const std::string labels[] = {label};
        session = sim::Session::open(disc, course_name, labels);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }

    gt3r::Vfs vfs(&disc.volume());
    audio::OfflineSound s;
    auto library = std::make_shared<audio::SoundLibrary>(vfs);
    const audio::RaceSoundTables tables = audio::RaceSoundTables::from_core(disc.read_program());
    audio::RaceSound sound(vfs, s.mixer(), s.driver(), library, tables);
    if (only != "course" && !sound.add_car(label)) {
        std::fprintf(stderr, "error: the car's engine sounds do not load\n");
        return 1;
    }
    if (only != "cars") sound.set_course(file.section(data, course::kSlotSound), road);
    for (int ch = 0; ch < audio::CourseSound::kChannels; ++ch)
        for (size_t k = 0; k < sound.course().emitters(ch); ++k) {
            const auto g = sound.course().emitter_gates(ch, k);
            const auto& e = sound.course().emitter(ch, k);
            std::printf("  %s %zu: gates %.1f .. %.1f m, %zu points", audio::CourseSound::effect_name(ch), k, double(g.first), double(g.second),
                        e.points.size());
            for (const auto& pt : e.points) std::printf(" (%.0f %.0f %.0f w %.2f)", double(pt.x), double(pt.y), double(pt.z), double(pt.weight));
            std::printf("\n");
        }
    const audio::SoundListener::Kind kind = view == "incar" ? audio::SoundListener::Kind::InCar
                                            : view == "other" ? audio::SoundListener::Kind::Other
                                                              : audio::SoundListener::Kind::Chase;
    race::CarInput in;
    int step = 0;
    std::printf("   time    rpm gear  km/h  thr  engine exhaust  levels  crowds   at (m)\n");
    for (const Phase& p : script) {
        const int steps = int(p.seconds * 60.0 + 0.5);
        in = race::CarInput{};
        if (p.what == "hold" || p.what == "brake") in.brake = 1;
        else if (p.what == "full") in.throttle = 1;
        else if (p.what == "half") in.throttle = 0.5f;
        else if (p.what != "idle") {
            std::fprintf(stderr, "error: unknown phase %s\n", p.what.c_str());
            return 2;
        }
        s.advance(steps, [&](int) {
            const race::CarInput inputs[] = {in};
            session->step_once(inputs);
            const race::CarState& car = session->cars()[0];
            sound.update(*session, audio::listener_at(car, kind), 0);
            if (step % 6 == 0) {
                const auto& c = session->record(0);
                const audio::CarSoundState st = audio::sound_state(*session, 0, &tables);
                const audio::CarSound::Report none;
                const auto& r = sound.cars() ? sound.car(0).report() : none;
                std::printf("  %5.2f %6.0f %4d %5.1f %4.2f  %6.3f %6.3f  %3d %3d  %6.3f %7.1f  %s  torques %.2f %.2f clutch %d\n", double(step) / 60.0, double(st.rpm),
                            int(c.gear), double(st.speed * 3.6f), double(st.throttle), double(r.engine_gain), double(r.exhaust_gain),
                            r.engine_level, r.exhaust_level, double(sound.report().ambience[2]), double(st.course_distance), p.what.c_str(),
                            double(st.torque_full), double(st.torque_friction), st.clutch);
            }
            ++step;
        });
    }
    sound.stop();
    s.advance_seconds(0.3);
    return write_sound_wav(out, s.pcm) ? 0 : 1;
}

} // namespace gt4r
