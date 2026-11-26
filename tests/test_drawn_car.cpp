// The drawn car on the session's race: the body where the game draws it and each wheel from the body's frame by its
// suspension's travel (sim::Session::publish; docs/specs/physics.md "What runs where").

#include "test_framework.h"

#include "gt4r/formats/course/runway.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/race/car_pose.h"
#include "gt4r/race/car_spec.h"
#include "gt4r/race/rail_simulation.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace gt4r;

TEST(drawn_car_rides_its_suspension) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d.get();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    gt3r::Volume* vol = &disc->volume();
    db::SpecDb sdb([vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    }, [vol](const std::string& dir) { return vol->list_dir(dir); });
    // The Impreza WRC on Grand Canyon's dirt (bumps of 14 .. 20 mm: its tires leave and meet the road every step or two) and
    // on Tsukuba's tarmac, 15 s each after 10 s.
    for (const char* course : {"grandcanyon", "tsukuba"}) {
        const std::string car = "impreza_wrc_03";
        const auto s = sim::Session::open(*disc, course, std::span<const std::string>(&car, 1));
        const race::CarSpec spec = race::read_car_spec(sdb, car);
        const race::RailLine line(s->ground().road());
        double n = 0, step2 = 0, worst_raise = 0, worst_across = 0, above = 0, loaded = 0;
        float last[4]{};
        for (int i = 0; i < 60 * 25; ++i) {
            race::CarInput in = race::follow_line(line, s->cars()[0], 0, 0.8f);
            s->step_once(std::span<const race::CarInput>(&in, 1));
            if (i < 600) continue;
            const race::CarState& c = s->cars()[0];
            CHECK(c.body_placed);
            const gt3r::render::Vec3 up{c.up.x, c.up.y, c.up.z};
            const gt3r::render::Mat4 m = race::body_matrix(c, spec);
            worst_raise = std::max(worst_raise, double(std::fabs(gt3r::render::dot({m.m[12] - c.position.x, m.m[13] - c.position.y, m.m[14] - c.position.z}, up))));
            const auto& r = s->record(0);
            for (int w = 0; w < 4; ++w) {
                const race::Vec3& o = c.wheels[w].center;
                const float h = gt3r::render::dot({o.x - c.position.x, o.y - c.position.y, o.z - c.position.z}, up);
                if (n > 0) step2 += double(h - last[w]) * (h - last[w]);
                last[w] = h;
                const auto& wh = r.w[size_t(w)];
                const float hx = r.pos.x.host() + wh.hub.x.host(), hz = -(r.pos.y.host() + wh.hub.y.host());
                worst_across = std::max(worst_across, std::hypot(double(o.x - hx), double(o.z - hz)));
                if (c.wheels[w].on_ground) above += double(o.y - wh.ground.host() - wh.bump.host() - r.p.tire_radius[size_t(w >> 1)].host()), ++loaded;
            }
            ++n;
        }
        const double rms = std::sqrt(step2 / (4 * n)), sits = above / loaded;
        std::printf("  %s: the body's raise at most %.4f m; the wheels' height on the body from step to step %.4f m rms; a loaded wheel above its road "
                    "by %.4f m; from its hub across the road at most %.3f m\n",
                    course, worst_raise, rms, sits, worst_across);
        // The body is drawn where the simulation has it (no seat on the wheels); the wheels follow their suspension's travel
        // whether their tires are loaded or not (two rules, one for each, made them jump 2 .. 5 cm a step on dirt: 17 mm rms);
        // a loaded wheel stands on its road, its own bump in it; a wheel stays at its hub.
        CHECK(worst_raise == 0);
        CHECK(rms < (course[0] == 'g' ? 0.012 : 0.003));
        CHECK(std::fabs(sits) < 0.005);
        CHECK(worst_across < 0.1);
    }
}
