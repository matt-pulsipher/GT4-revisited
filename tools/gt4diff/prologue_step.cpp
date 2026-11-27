// gt4r_diff's run of Gran Turismo 4 Prologue's own race step (prologue_race.h): a test drive of one car on a course,
// what the program's step makes of it.
//
//   gt4r_diff <prologue disc> prologue [course] [car] [steps]

#include "orig.h"
#include "prologue_race.h"
#include "prologue_taps.h"

#include "gt4r/sim/feel_prologue.h"

#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/exact_bridge.h"
#include "gt4r/sim/rules_prologue.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/sim/collide.h"
#include "sim/exact/course_ground.h"
#include "gt4r/sim/session.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/params_build.h"
#include "sim/exact/race_inputs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>

namespace gt4r::diff::prologue {

namespace {

// The pad of a test drive at step i: a standing start at full throttle, a lift, turns each way and the brakes.
Pad drive(int i) {
    Pad p;
    const int t = i - 150;
    if (t < 0) return p;
    p.throttle = 1.0f;
    if (t > 500 && t < 560) p.throttle = 0.0f;
    if (t > 600 && t < 700) p.steer = 0.35f;
    if (t > 760 && t < 860) p.steer = -0.5f;
    if (t > 900) p.throttle = 0.0f, p.brake = 0.8f;
    return p;
}

} // namespace

// GT4R_WHOLESTEP: every step from the program's state: the record (and Prologue's part of it) loaded into our session, a
// step of each, every field of the record compared; the fields listed by the step they first differ in.
int whole_step(Disc& disc, const std::string& course, const std::string& car, int steps, std::vector<std::string>* fields = nullptr) {
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc);
    Race r;
    std::string err;
    if (!setup_race(ee, disc, sdb, course, {car}, 1, r, err, aids_from_env())) {
        std::printf("prologue: %s\n", err.c_str());
        return 1;
    }
    const std::vector<std::string> cars{car};
    std::unique_ptr<sim::Session> session = sim::Session::open(disc, course, cars);
    if (!session->prologue_rules()) {
        std::printf("prologue: the session does not run Prologue's step\n");
        return 1;
    }
    {
        const sim::prologue::CarPart& q = session->prologue_part(0);
        std::printf("prologue: tire drop ours %08X %08X, the program's %08X %08X; front share ours %08X, the program's %08X\n", q.tire_drop[0].bits,
                    q.tire_drop[1].bits, ee.r32(r.params(0) + 0x28C), ee.r32(r.params(0) + 0x290), q.front_share.bits, ee.r32(r.params(0) + 0x24));
    }
    PTapStat st;
    st.name = "whole step";
    if (std::getenv("GT4R_START")) { // (the car's set-up, 0x3C93A0: our session's record before any step against the program's)
        sim::Session::skip_hold = true;
        session = sim::Session::open(disc, course, cars);
        gt3r::sim::exact::Car theirs;
        const std::vector<u8> im = gt3_image(ee, r.car(0));
        theirs.behaviour = sim::rules::behaviour();
        gt3r::sim::exact::load_image(theirs, im.data());
        gt3r::sim::exact::Car mine = session->record(0);
        const sim::prologue::CarPart& q = session->prologue_part(0);
        for (size_t w = 0; w < 4; ++w) mine.t[w].steer_sin = q.offset_rate[w], mine.t[w].steer_cos = q.tires.steer_offset[w];
        compare(theirs, mine, st);
        std::printf("prologue: the set-up on %s: %zu fields differ\n", course.c_str(), st.fields.size());
        for (const auto& [name, f] : st.fields) std::printf("    %-24s most by %.3g; %s\n", name.c_str(), f.worst, f.example.c_str());
        return 0;
    }
    auto load = [&](gt3r::sim::exact::Car& c) {
        const std::vector<u8> im = gt3_image(ee, r.car(0));
        c.behaviour = sim::rules::behaviour();
        gt3r::sim::exact::load_image(c, im.data());
        c.later.ratio = gt3r::ee::F::raw(ee.r32(r.car(0) + 0xD8 + 0x3A4));
    };
    const int from = std::getenv("GT4R_WHOLESTEP_FROM") ? std::atoi(std::getenv("GT4R_WHOLESTEP_FROM")) : 0;
    // (GT4R_WHOLESTEP=front: the session's phases 1 .. 11 on the program's state against the program's record as its phase 19
    // starts; the collision's outputs and what phases 14 and 15 write are not compared)
    const bool front = std::getenv("GT4R_WHOLESTEP") && std::string(std::getenv("GT4R_WHOLESTEP")) == "front";
    const sim::Program program = sim::Program::load(disc);
    const gt3r::sim::exact::ProgramData data = sim::core_program(program);
    const gt3r::sim::exact::RaceInputs inputs = gt3r::sim::exact::race_inputs(data, gt3r::sim::exact::Conductor::SinglePlayer, {}, false, 0);
    const sim::prologue::Tables tables = sim::prologue::tables(program);
    gt3r::sim::exact::Car at19;
    bool seen19 = false;
    gt3r::sim::exact::Car exit19;
    ee.probe(0x3B746C, [&](Ee& e) {
        const std::vector<u8> im = gt3_image(e, r.car(0));
        exit19 = {};
        exit19.behaviour = sim::rules::behaviour();
        gt3r::sim::exact::load_image(exit19, im.data());
    });
    ee.probe(0x3B29D0, [&](Ee& e) {
        if (u32(e.a(0)) != r.car(0) || seen19) return;
        const std::vector<u8> im = gt3_image(e, r.car(0));
        at19 = {};
        at19.behaviour = sim::rules::behaviour();
        gt3r::sim::exact::load_image(at19, im.data());
        seen19 = true;
    });
    if (std::getenv("GT4R_TRACE_YAW")) { // (every write of a collision body's yaw rate, +0x3D0, its value after)
        static const u32 writes[] = {0x41D32C, 0x41D9E8, 0x41E304, 0x41F098, 0x41F500, 0x420290, 0x420324};
        static const int base[] = {3, 5, 7, 18, 18, 16, 17};
        for (size_t k = 0; k < std::size(writes); ++k)
            ee.add_probe(writes[k] + 4, [k](Ee& e) { std::printf("  yaw write %06X: w %08X vx %08X vy %08X\n", writes[k], e.r32(e.g32(base[k]) + 0x3D0), e.r32(e.g32(base[k]) + 0x3C4), e.r32(e.g32(base[k]) + 0x3C8)); });
    }
    if (std::getenv("GT4R_TRACE_YAW")) {
        ee.add_probe(0x41EE74, [](Ee& e) { std::printf("  cos %08X sin %08X\n", e.r32(e.g32(29) + 0x10), e.fpr[0]); });
        ee.add_probe(0x41F1E4, [](Ee& e) { std::printf("  j %08X k %08X vp %08X %08X\n", e.fpr[1], e.fpr[10], e.r32(e.g32(7)), e.r32(e.g32(7) + 4)); });
        ee.add_probe(0x41EF34, [](Ee& e) {
            const u32 sp = e.g32(29);
            std::printf("  J %08X %08X f %08X %08X %08X\n", e.r32(sp), e.r32(sp + 4), e.r32(sp + 0x40), e.r32(sp + 0x44), e.r32(sp + 0x48));
        });
        ee.add_probe(0x41F094, [](Ee& e) {
            const u32 sp = e.g32(29);
            std::printf("  moment %08X jl %08X %08X e %08X w %08X\n", e.fpr[0], e.r32(sp + 0x50), e.r32(sp + 0x54), e.g32(2), e.fpr[2]);
            std::printf("  edge %08X %08X\n", e.r32(e.g32(2)), e.r32(e.g32(2) + 4));
            for (u32 k = 0; k < 8; ++k) std::printf("    its edge %u %08X %08X point %08X %08X\n", k, e.r32(e.g32(18) + 0x228 + 0x2C * k), e.r32(e.g32(18) + 0x22C + 0x2C * k), e.r32(e.g32(18) + 8 + 0x44 * k), e.r32(e.g32(18) + 0xC + 0x44 * k));
        });
    }
    if (std::getenv("GT4R_TRACE_YAW")) // (a point's impulse on a wall as it starts, 0x41EE48, and the hit as it is found)
        ee.add_probe(0x41EE48, [](Ee& e) {
            const u32 b = u32(e.a(0));
            std::printf("  point: im %08X ii %08X wk %d wseg %08X u %08X %08X ts %08X s %08X %08X %08X sh %08X v %08X %08X w %08X d %08X %08X\n", e.r32(b), e.r32(b + 4), int(e.r32(b + 0x41C)),
                        e.r32(b + 0x428), e.r32(b + 0x420), e.r32(b + 0x424), e.r32(b + 0x38C), e.r32(b + 0x390), e.r32(b + 0x394), e.r32(b + 0x398), e.r32(b + 0x39C), e.r32(b + 0x3C4),
                        e.r32(b + 0x3C8), e.r32(b + 0x3D0), e.r32(b + 0x3B4), e.r32(b + 0x3B8));
        });
    if (std::getenv("GT4R_TRACE_NUDGE")) { // (the collision's stuck nudge, 0x41D8A8: the body's yaw rate before and after)
        static u32 ra = 0, body = 0;
        ee.add_probe(0x41D8A8, [&](Ee& e) {
            body = u32(e.a(0)), ra = e.g32(31);
            std::printf("  nudge: body %08X w %08X v %08X %08X last %08X steer %d dt %08X touch flags %d\n", body, e.r32(body + 0x3D0), e.r32(body + 0x3C4),
                        e.r32(body + 0x3C8), e.r32(body + 0x3C4 + 0), int(gt3r::s8(e.r8(e.r32(body + 0x450) + 0x5FF))), e.r32(body + 0x4DC), int(e.r8(body + 0x43C)));
            if (ra) e.add_probe(ra, [](Ee& x) {
                    if (body) std::printf("  nudge after: w %08X\n", x.r32(body + 0x3D0));
                    body = 0;
                });
        });
    }
    for (int i = 0; i < steps; ++i) {
        const Pad p = drive(i);
        r.pad[0] = p;
        if (i >= from) {
            gt3r::sim::exact::Car before;
            load(before);
            sim::prologue::CarPart part = session->prologue_part(0);
            for (size_t w = 0; w < 4; ++w) part.offset_rate[w] = before.t[w].steer_sin, part.tires.steer_offset[w] = before.t[w].steer_cos;
            if (before.p.d.kind == 3) {
                const u32 h = r.car(0) + 0x5DC, f = r.car(0) + 0xD8;
                using gt3r::ee::F;
                part.hybrid.engine_target = F::raw(ee.r32(h)), part.hybrid.store = F::raw(ee.r32(h + 4)), part.hybrid_fuel = F::raw(ee.r32(h + 8));
                part.mileage = u16(ee.r16(h + 0xC)), part.hybrid.flags = ee.r8(h + 0xE), part.hybrid.mode = ee.r8(h + 0xF);
                part.mileage_steps = ee.r8(f + 0x4A0), part.hybrid_fuel_mark = F::raw(ee.r32(f + 0x3B0)), part.hybrid_odo_mark = F::raw(ee.r32(f + 0x3B4));
            }
            session->load_state(0, before, session->gt4_record(0));
            session->load_prologue_part(0, part);
            session->set_vu0_random(ee.vr); // (the flaps' random stream)
        }
        if (front && i >= from) {
            namespace ex = gt3r::sim::exact;
            ex::Car mine;
            load(mine);
            sim::prologue::CarPart part = session->prologue_part(0);
            for (size_t w = 0; w < 4; ++w) part.offset_rate[w] = mine.t[w].steer_sin, part.tires.steer_offset[w] = mine.t[w].steer_cos;
            ex::PadFrame f;
            f.flags = 1 | 2 | 4, f.kind = 3;
            auto axis = [](float v) { return s32(std::lround(std::clamp(v, -1.0f, 1.0f) * 4096.0f)); };
            f.steer = gt3r::s16(-axis(p.steer)), f.throttle = u16(axis(p.throttle)), f.brake = u16(axis(p.brake));
            ex::save_previous(mine);
            ex::prep(mine, inputs.prep);
            sim::prologue::driver_input_pad(mine, inputs.pad, f, 0, part.layout, tables);
            sim::prologue::wheel_steer(mine, part, tables);
            sim::prologue::slip_ratios(mine, part.tires, part.layout, tables);
            ex::aids(mine, inputs.aids);
            sim::prologue::dynamics(mine, part, inputs.statics, inputs.gears, inputs.class_grip, tables);
            ex::brake_heat(mine, inputs.brake_heat);
            ex::clear_feedback(mine);
            seen19 = false;
            step(ee, r);
            for (size_t w = 0; w < 4; ++w) mine.t[w].steer_sin = part.offset_rate[w], mine.t[w].steer_cos = part.tires.steer_offset[w];
            // (the collision's outputs)
            mine.pos = at19.pos, mine.vel = at19.vel, mine.heading = at19.heading, mine.yaw_rate = at19.yaw_rate;
            compare(at19, mine, st);
            continue;
        }
        seen19 = false;
        step(ee, r);
        race::CarInput in;
        in.steer = p.steer, in.throttle = p.throttle, in.brake = p.brake;
        session->step_once({&in, 1});
        if (i < from) continue;
        if (std::getenv("GT4R_SHOW_COLLIDE") && i < from + 5 && session->prologue_collision()) {
            const auto& b = session->prologue_collision()->body(0);
            std::printf("    step %d collision: pos %.9g %.9g %.9g / %.9g %.9g %.9g  vel %.9g %.9g %.9g / %.9g %.9g %.9g  heading %.9g / %.9g  yaw %.9g / %.9g  hint %d wall %d\n", i + 1,
                        double(at19.pos.x.host()), double(at19.pos.y.host()), double(at19.pos.z.host()), double(b.cx.host()), double(b.cy.host()),
                        double(b.cz.host()), double(at19.vel.x.host()), double(at19.vel.y.host()), double(at19.vel.z.host()), double(b.cvx.host()),
                        double(b.cvy.host()), double(b.cvz.host()), double(at19.heading.host()), double(b.ch.host()), double(at19.yaw_rate.host()),
                        double(b.cw.host()), b.hint, int(b.wall_hit));
            std::printf("    ours: im %08X ii %08X wk %d wseg %d u %08X %08X ts %08X s %08X %08X %08X sh %08X v %08X %08X w %08X\n", b.im.bits, b.ii.bits, b.wk, b.wseg, b.ux.bits, b.uy.bits,
                        b.ts.bits, b.sx.bits, b.sy.bits, b.sz.bits, b.sh.bits, b.vx.bits, b.vy.bits, b.w.bits);
            const u32 body = ee.r32(r.car(0) + 0xD0);
            (void)body;
            for (int k = 0; k < 8; ++k) std::printf("      edge %d %08X %08X point %08X %08X\n", k, b.e[size_t(k)].sx.bits, b.e[size_t(k)].sy.bits, b.p[size_t(k)].lx.bits, b.p[size_t(k)].ly.bits);
        } else if (std::getenv("GT4R_SHOW_COLLIDE") && i < from + 5) {
            const sim::collide::Body& b = session->body(0);
            std::printf("    step %d collision: pos %.9g %.9g %.9g / %.9g %.9g %.9g  vel %.9g %.9g %.9g / %.9g %.9g %.9g  heading %.9g / %.9g  yaw %.9g / %.9g\n", i + 1,
                        double(at19.pos.x.host()), double(at19.pos.y.host()), double(at19.pos.z.host()), double(b.cx.host()), double(b.cy.host()),
                        double(b.cz.host()), double(at19.vel.x.host()), double(at19.vel.y.host()), double(at19.vel.z.host()), double(b.cvx.host()),
                        double(b.cvy.host()), double(b.cvz.host()), double(at19.heading.host()), double(b.heading.host()), double(at19.yaw_rate.host()),
                        double(b.yaw_rate.host()));
        }
        gt3r::sim::exact::Car after;
        load(after);
        gt3r::sim::exact::Car mine = session->record(0);
        // (Prologue keeps the steering offset and its rate where the engine's record has the steer angle's cosine and sine)
        const sim::prologue::CarPart& q = session->prologue_part(0);
        for (size_t w = 0; w < 4; ++w) mine.t[w].steer_sin = q.offset_rate[w], mine.t[w].steer_cos = q.tires.steer_offset[w];
        if (std::getenv("GT4R_AFTER19")) { // (what the program changes in the record after its phase 19)
            compare(after, exit19, st);
            continue;
        }
        if (std::getenv("GT4R_VS19")) { // (the session's record against phase 19 as the tap runs it on the program's state at its start)
            namespace ex = gt3r::sim::exact;
            static std::unique_ptr<ex::CourseGround> ground;
            if (!ground) {
                const gt3r::Volume::Entry* e = disc.volume().find("crs/" + course);
                const Bytes d = disc.volume().read(*e);
                const course::CourseFile parsed = course::CourseFile::parse(d);
                ground = std::make_unique<ex::CourseGround>(*course::course_road(parsed, d)->gtrw);
            }
            ex::Car k = at19;
            const sim::prologue::CarPart& x = session->prologue_part(0);
            ex::hub_offsets_xy(k);
            ex::speed_ramp(k);
            sim::prologue::ground_contact(k, x, *ground, tables.bumps, true);
            sim::prologue::terrain(k, x);
            ex::suspension(k, nullptr);
            sim::prologue::attitude(k, x.layout, tables);
            sim::prologue::bump_nudge(k, x);
            ex::hub_offsets(k);
            sim::rules::body_axes(k);
            sim::prologue::ground_frame(k);
            sim::prologue::FeelPart feel;
            feel.speedo = gt3r::ee::F::raw(ee.r32(r.car(0) + 0xC60));
            sim::prologue::speeds(k, feel, x.layout, sim::prologue::speed_tables(program, ee.r8(ee.r32(r.car(0) + 0x10) + 0x33)), inputs.prep.condition);
            ex::wheel_spin(k);
            for (size_t w = 0; w < 4; ++w) k.t[w].steer_sin = x.offset_rate[w], k.t[w].steer_cos = x.tires.steer_offset[w];
            compare(k, mine, st);
            continue;
        }
        compare(after, mine, st);
        if (mine.p.d.kind == 3 && i >= from) { // (the hybrid's box)
            const u32 h = r.car(0) + 0x5DC;
            const u32 theirs[] = {ee.r32(h), ee.r32(h + 4), ee.r32(h + 8), u32(u16(ee.r16(h + 0xC))), ee.r8(h + 0xE), ee.r8(h + 0xF)};
            const u32 ours[] = {q.hybrid.engine_target.bits, q.hybrid.store.bits, q.hybrid_fuel.bits, q.mileage, q.hybrid.flags, q.hybrid.mode};
            for (size_t k = 0; k < std::size(ours); ++k)
                if (ours[k] != theirs[k]) {
                    PFieldStat& f = st.fields["hybrid[" + std::to_string(k) + "]"];
                    if (!f.off++) f.example = "first at step " + std::to_string(i + 1);
                }
        }
    }
    if (fields) {
        for (const auto& [name, f] : st.fields) fields->push_back(name);
        return st.calls == u64(steps) ? 0 : 1;
    }
    std::printf("prologue: the whole step from the program's state, %llu steps, %llu with every field equal\n", (unsigned long long)st.calls,
                (unsigned long long)st.clean);
    std::vector<std::pair<std::string, PFieldStat>> v(st.fields.begin(), st.fields.end());
    auto first = [](const PFieldStat& f) { return std::atol(f.example.c_str() + std::strlen("first at step ")); };
    std::sort(v.begin(), v.end(), [&](const auto& a, const auto& b) { return first(a.second) < first(b.second); });
    for (const auto& [name, f] : v)
        std::printf("    %-24s differs in %llu, most by %.3g; %s\n", name.c_str(), (unsigned long long)f.off, f.worst, f.example.c_str());
    return 0;
}

bool whole_step_fields(Disc& disc, const std::string& course, const std::string& car, int steps, std::vector<std::string>& fields) {
    return whole_step(disc, course, car, steps, &fields) == 0;
}

int run(Disc& disc, const std::string& course, const std::string& car, int steps) {
    if (!is_prologue(disc)) {
        std::printf("prologue: the disc's program is not Prologue's\n");
        return 1;
    }
    if (std::getenv("GT4R_WHOLESTEP")) return whole_step(disc, course, car, steps);
    if (std::getenv("GT4R_CONVERT")) return convert_check(disc, course, car);
    set_disc(disc);
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc);
    if (std::getenv("GT4R_MEDAL_NAMES"))
        for (u32 k = 0; k < 4; ++k) std::printf("prologue: medal name %u: %s\n", k, ee.read_cstr(ee.r32(0x505008 + 4 * k)).c_str());
    std::printf("prologue: the image's mean slip switch %u, steering mode %u\n", ee.r8(0x506612), ee.r32(0x714880));
    Race r;
    std::string err;
    // The spec the program's builder (0x3B3998) made, as the car set-up (0x3C93A0) is handed it.
    std::vector<u8> their_spec;
    ee.probe(0x3C93A0, [&](Ee& e) {
        if (their_spec.empty()) {
            their_spec.resize(gt3r::sim::exact::kSpecSize);
            e.read_bytes(u32(e.a(1)), their_spec.data(), u32(their_spec.size()));
        }
    });
    if (!setup_race(ee, disc, sdb, course, {car}, 1, r, err, aids_from_env())) {
        std::printf("prologue: %s\n", err.c_str());
        return 1;
    }
    if (std::getenv("GT4R_PAD_CONSTS")) {
        std::printf("prologue: pad bytes 0x504950..: ");
        for (u32 k = 0; k < 0x20; ++k) std::printf("%02X ", ee.r8(0x504950 + k));
        std::printf("\nprologue: 0x504AA4..: ");
        for (u32 k = 0; k < 7; ++k) std::printf("%g ", double(ee.rf(0x504AA4 + 4 * k)));
        std::printf("\n");
    }
    std::printf("prologue: the race is set up (%s, %s); the mean slip switch %u, steering mode %u\n", course.c_str(), car.c_str(), ee.r8(0x506612), ee.r32(0x714880));
    // The parameter block the program's converter made, against the engine's converter (Gran Turismo 3's, 0x1DBFD8) on the
    // program's own spec: word by word, the program's block read at the engine's offset + 0, 24 or 44.
    if (!their_spec.empty()) {
        std::vector<u8> theirs(kParamsSize);
        ee.read_bytes(r.params(0), theirs.data(), kParamsSize);
        const sim::Program program = sim::Program::load(disc);
        const gt3r::sim::exact::ProgramData data = sim::core_program(program);
        gt3r::sim::exact::ConvertInputs in;
        in.data = &data, in.base = r.params(0), in.control = 0;
        in.physics_globals();
        gt3r::sim::exact::Spec spec;
        std::copy(their_spec.begin(), their_spec.begin() + spec.size(), spec.begin());
        gt3r::sim::exact::ParamImage mine{};
        gt3r::sim::exact::convert_params(spec, in, mine);
        int equal[3] = {}, words = 0;
        const u32 shifts[3] = {0, 24, 44};
        std::string runs;
        int last = -1, run_start = 0;
        auto flush = [&](int k, u32 at) {
            if (last >= 0) {
                char b[64];
                std::snprintf(b, sizeof b, "  %03X..%03X:%s", run_start, at, last == 3 ? "differ" : last == 0 ? "+0" : last == 1 ? "+24" : "+44");
                runs += b;
            }
            last = k, run_start = int(at);
        };
        for (u32 o = 0; o + 4 <= mine.size(); o += 4) {
            u32 a = 0;
            std::memcpy(&a, &mine[o], 4);
            if (a == 0) continue;
            ++words;
            int k = 3;
            for (int s = 0; s < 3 && k == 3; ++s) {
                if (o + shifts[s] + 4 > theirs.size()) continue;
                u32 b = 0;
                std::memcpy(&b, &theirs[o + shifts[s]], 4);
                // (equal, or within 8 units in the last place: the program's own rounding)
                const s64 ulps = s64(s32(a)) - s64(s32(b));
                if (a == b || (((a ^ b) & 0x80000000u) == 0 && (a & 0x7F800000u) != 0 && ulps >= -8 && ulps <= 8)) k = s, ++equal[s];
            }
            if (k != last) flush(k, o);
            if (std::getenv("GT4R_PARAMS") && k == 3) {
                u32 b = 0;
                std::memcpy(&b, &theirs[o], 4);
                float fa, fb;
                std::memcpy(&fa, &a, 4), std::memcpy(&fb, &b, 4);
                std::printf("    P+%03X  ours %08X (%g)  the program's %08X (%g)\n", o, a, double(fa), b, double(fb));
            }
        }
        flush(-1, u32(mine.size()));
        // With the tire records 0x1B8 bytes apart (4 bytes after each of the engine's 0x1B4), what follows them 24 on,
        // and from GT4R_P44 (hex) 44 on.
        const u32 x44 = std::getenv("GT4R_P44") ? u32(std::strtoul(std::getenv("GT4R_P44"), nullptr, 16)) : 0xE6C;
        int same = 0, close = 0, off = 0;
        for (u32 o = 0; o + 4 <= mine.size(); o += 4) {
            u32 q = o;
            if (o >= 0x398 && o < 0xDD0) q = 0x398 + 0x1B8 * ((o - 0x398) / 0x1B4) + (o - 0x398) % 0x1B4;
            else if (o >= 0xDD0) q = o + (o >= x44 ? 44 : 24);
            u32 a = 0, b = 0;
            std::memcpy(&a, &mine[o], 4), std::memcpy(&b, &theirs[q], 4);
            const s64 ulps = s64(s32(a)) - s64(s32(b));
            if (a == b) ++same;
            else if (((a ^ b) & 0x80000000u) == 0 && (a & 0x7F800000u) != 0 && ulps >= -8 && ulps <= 8) ++close;
            else {
                ++off;
                float fa, fb;
                std::memcpy(&fa, &a, 4), std::memcpy(&fb, &b, 4);
                if (std::getenv("GT4R_PARAMS2")) std::printf("    P+%03X (theirs +%03X)  ours %08X (%g)  the program's %08X (%g)\n", o, q, a, double(fa), b, double(fb));
            }
        }
        for (u32 g = 0; g < 8; ++g) {
            float gs;
            std::memcpy(&gs, &theirs[0x1F8 + 0x5C + 4 * g], 4);
            std::printf("  gear %u: %g rad/s per m/s = %.1f rpm per km/h\n", g, double(gs), double(gs) * 60 / 6.2831853 / 3.6);
        }
        for (u32 t = 0; t < 6; ++t) std::printf("  tire %u: +0x1B4 %02X %02X %02X %02X\n", t, theirs[0x398 + 0x1B8 * t + 0x1B4], theirs[0x398 + 0x1B8 * t + 0x1B5], theirs[0x398 + 0x1B8 * t + 0x1B6], theirs[0x398 + 0x1B8 * t + 0x1B7]);
        for (u32 o = 0xE84; o < 0xE98; o += 4) {
            float f;
            u32 w;
            std::memcpy(&f, &theirs[o], 4), std::memcpy(&w, &theirs[o], 4);
            std::printf("  P+%03X %08X %g\n", o, w, double(f));
        }
        std::printf("  P+20A..20B %02X %02X\n", theirs[0x20A], theirs[0x20B]);
        std::printf("  parameters mapped: %d words equal, %d within 8 units in the last place, %d differ\n", same, close, off);
        std::printf("  parameters: %d nonzero words of the engine's; equal at +0: %d, +24: %d, +44: %d\n %s\n", words, equal[0], equal[1], equal[2], runs.c_str());
    }
    const int dump_at = std::getenv("GT4R_DUMP") ? std::atoi(std::getenv("GT4R_DUMP")) : -1;
    auto dump = [&] {
        const u32 at = r.car(0);
        for (u32 off = 0; off < kCarSize; off += 4) {
            const u32 v = ee.r32(at + off);
            if (!v) continue;
            const float f = ee.rf(at + off);
            if (std::fabs(f) > 1e-6f && std::fabs(f) < 1e7f) std::printf("  +%04X %12.6f\n", off, double(f));
            else std::printf("  +%04X %08X\n", off, v);
        }
    };
    if (dump_at == 0) dump();
    // GT4R_TAPS: the engine's routines compared with Prologue's at every call (prologue_taps.h).
    const sim::Program taps_program = sim::Program::load(disc);
    const gt3r::sim::exact::ProgramData taps_data = sim::core_program(taps_program);
    gt3r::sim::exact::RaceInputs inputs =
        gt3r::sim::exact::race_inputs(taps_data, gt3r::sim::exact::Conductor::SinglePlayer, gt3r::sim::exact::RaceConfig{}, false, 0);
    sim::prologue::race_inputs(inputs, sim::prologue::tables(taps_program));
    std::unique_ptr<PTaps> taps;
    if (std::getenv("GT4R_TAPS")) {
        taps = std::make_unique<PTaps>(ee, r, inputs);
        if (std::string(std::getenv("GT4R_TAPS")) == "prologue") add_prologue_taps(*taps, disc, course);
        else if (std::string(std::getenv("GT4R_TAPS")) == "input") add_input_taps(*taps, r);
        else add_engine_taps(*taps);
    }
    struct Sample {
        float x, y, z, v, rpm;
        int gear;
    };
    std::vector<Sample> theirs, ours;
    // (GT4R_GROUND_CHECK: each wheel's ground record as the program's query leaves it, for our road's answer at its point)
    struct GroundSample {
        float at[3], height, normal[3];
        u8 miss, material, flags;
    };
    std::vector<GroundSample> grounds;
    const bool ground_check = std::getenv("GT4R_GROUND_CHECK") != nullptr;
    try {
        for (int i = 0; i < steps; ++i) {
            r.pad[0] = drive(i);
            step(ee, r);
            if (ground_check)
                for (u32 w = 0; w < 4; ++w) {
                    const u32 g = r.car(0) + 0x614 + 0x34 * w;
                    grounds.push_back({{ee.rf(g), ee.rf(g + 4), ee.rf(g + 8)}, ee.rf(g + 0x10), {ee.rf(g + 0x1C), ee.rf(g + 0x20), ee.rf(g + 0x24)},
                                       ee.r8(g + 0x30), ee.r8(g + 0x31), ee.r8(g + 0x33)});
                }
            if (i + 1 == dump_at) dump();
            const u32 F = r.car(0) + 0xD8;
            theirs.push_back({ee.rf(F), ee.rf(F + 4), ee.rf(F + 8), ee.rf(F + 0x450), float(ee.r16(F + 0x494)), int(ee.r32(r.car(0) + 0x510))});
        }
    } catch (const std::exception& e) {
        std::printf("prologue: the step stopped: %s (pc %08X ra %08X)\n", e.what(), ee.cur_pc, ee.g32(31));
        return 1;
    }
    if (taps) taps->print();
    // Ours on the same course and car with the same pad, from the same first step (no hold).
    const std::vector<std::string> cars{car};
    std::unique_ptr<sim::Session> session = sim::Session::open(disc, course, cars);
    for (const std::string& n : session->notes()) std::printf("  ours: %s\n", n.c_str());
    if (ground_check) {
        int same = 0, close = 0, off = 0, miss = 0;
        double worst = 0;
        for (size_t k = 0; k < grounds.size(); ++k) {
            const GroundSample& g = grounds[k];
            if (g.miss) {
                ++miss;
                continue;
            }
            // (the program's query: from 5 m above the point, 0x395760; the record's h may be rewritten by it)
            sim::GroundHit h;
            const gt3r::ee::F at[3] = {gt3r::ee::F(g.at[0]), gt3r::ee::F(g.height + 5.0f), gt3r::ee::F(g.at[2])};
            if (!session->ground().query(at, h)) {
                ++off;
                std::printf("    ground step %zu wheel %zu: ours misses (program's height %g material %u)\n", k / 4 + 1, k % 4, double(g.height), g.material);
                continue;
            }
            const double d = std::fabs(double(h.point.y) - double(g.height));
            if (h.point.y == g.height && h.material == g.material) ++same;
            else if (d < 1e-4 && h.material == g.material) ++close;
            else {
                ++off;
                if (off < 12)
                    std::printf("    ground step %zu wheel %zu: the program's %.6f mat %u flags %u, ours %.6f mat %u flags %u\n", k / 4 + 1, k % 4, double(g.height),
                                g.material, g.flags, double(h.point.y), h.material, h.flags);
            }
            if (d > worst) worst = d;
        }
        std::printf("  ground: %d queries equal, %d within 0.1 mm, %d differ, %d off the road (worst %.6f m)\n", same, close, off, miss, worst);
    }
    if (!their_spec.empty()) {
        // The parameter block as the session builds it (Prologue's spec and converter, rules_prologue.h build_car) against the
        // program's, word by word in Prologue's layout (GT4R_CONVERT compares the converter on every car).
        std::vector<u8> theirs(kParamsSize);
        ee.read_bytes(r.params(0), theirs.data(), kParamsSize);
        const sim::Program program = sim::Program::load(disc);
        const sim::prologue::Tables pt = sim::prologue::tables(program);
        sim::Equipment eq;
        int same = 0, off = 0;
        if (sim::stock_car(sdb, program, car, eq)) {
            gt3r::sim::exact::Car c;
            sim::prologue::Block mine{};
            sim::prologue::build_car(sdb, session->spec(0), eq, 0, true, true, pt, r.params(0), c, &mine);
            for (u32 o = 0; o + 4 <= kParamsSize; o += 4) {
                if (o >= 0xEA8 && o < 0xEDC) continue; // (the aids: the harness's entry switches them off)
                u32 a = 0, b = 0;
                std::memcpy(&a, &mine[o], 4), std::memcpy(&b, &theirs[o], 4);
                if (a == b) ++same;
                else ++off, std::printf("    P+%03X  ours %08X  the program's %08X\n", o, a, b);
            }
        }
        std::printf("  parameters as the session builds them: %d words equal, %d differ\n", same, off);
    }
    {
        gt3r::sim::exact::Spec mine;
        sim::core_spec(session->spec(0), mine);
        int differ = 0;
        for (size_t k = 0; k < mine.size() && k < their_spec.size(); ++k)
            if (mine[k] != their_spec[k]) ++differ;
        std::printf("  spec (the core's layout): %d of %zu bytes differ\n", differ, mine.size());
        for (int k = 0; k < 6; ++k) {
            const u32 b = sim::Spec::tire_block(k);
            std::printf("  our spec's tire block %d kinds: %u %u %u\n", k, session->spec(0).b(b + 3), session->spec(0).b(b + 4), session->spec(0).b(b + 5));
        }
        if (std::getenv("GT4R_SPEC"))
            for (size_t k = 0; k < mine.size() && k < their_spec.size(); ++k)
                if (mine[k] != their_spec[k]) std::printf("    +%03zX  the program's %3u  ours %3u\n", k, their_spec[k], mine[k]);
    }
    for (int i = 0; i < steps; ++i) {
        const Pad p = drive(i);
        race::CarInput in;
        in.steer = p.steer, in.throttle = p.throttle, in.brake = p.brake;
        session->step_once({&in, 1});
        const race::CarState& c = session->cars()[0];
        ours.push_back({c.position.x, c.position.y, c.position.z, c.speed, c.engine_rpm, c.gear});
    }
    std::printf("prologue: %d steps of the program's own (and ours, GT4 rules %s): step, speed km/h, rpm, gear\n", steps, session->gt4_rules() ? "on" : "off");
    const int every = std::getenv("GT4R_EVERY") ? std::atoi(std::getenv("GT4R_EVERY")) : 30;
    for (int i = every - 1; i < steps; i += every) {
        const Sample& a = theirs[size_t(i)];
        const Sample& b = ours[size_t(i)];
        auto moved = [&](const std::vector<Sample>& v) {
            const Sample& p = v[size_t(i - every + 1 > 0 ? i - every : 0)];
            const Sample& q = v[size_t(i)];
            return std::sqrt(double((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y) + (q.z - p.z) * (q.z - p.z))) * 60.0 / every * 3.6;
        };
        std::printf("  %5d  %8.3f %8.3f  (moved %8.3f %8.3f)  %7.0f %7.0f   %08X %d\n", i + 1, double(a.v) * 3.6, double(b.v) * 3.6, moved(theirs), moved(ours),
                    double(a.rpm), double(b.rpm), a.gear, b.gear);
    }
    return 0;
}

} // namespace gt4r::diff::prologue
