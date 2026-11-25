// The whole race step from the program's state; see whole_step.h.

#include "whole_step.h"

#include "taps.h"

#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/session.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gt4r::diff {

namespace {

constexpr u32 kBodySize = 0x4F0;
using gt3r::ee::F;

void load_body(sim::collide::Body& b, const std::vector<u8>& rec, u32 walls) {
    auto rd = [&](u32 o) {
        u32 x;
        std::memcpy(&x, rec.data() + o, 4);
        return x;
    };
    auto f = [&](u32 o) { return F::raw(rd(o)); };
    auto wall = [&](u32 o) {
        const u32 p = rd(o);
        return p >= walls ? int((p - walls) / 16) : -1;
    };
    // (the members the step carries between steps; the rest it sets itself)
    for (int k = 0; k < 8; ++k) {
        sim::collide::Point& q = b.p[size_t(k)];
        const u32 o = 0x008 + 0x44 * u32(k);
        q.sx = f(o + 0x10), q.sy = f(o + 0x14), q.ex = f(o + 0x18), q.ey = f(o + 0x1C);
        q.flags = rd(o + 0x20), q.hit = rd(o + 0x24), q.t = f(o + 0x28), q.wall = wall(o + 0x2C);
        q.tx = f(o + 0x30), q.ty = f(o + 0x34), q.other = int(rd(o + 0x3C)), q.edge = int(rd(o + 0x40));
    }
    for (int k = 0; k < 8; ++k) {
        sim::collide::Edge& e = b.e[size_t(k)];
        const u32 o = 0x228 + 0x2C * u32(k);
        e.x1c = rd(o + 0x1C), e.hit = rd(o + 0x20), e.t = f(o + 0x24), e.wall = wall(o + 0x28);
    }
    b.have_points = rd(0x3EC), b.enabled = rd(0x430);
    b.rumble_steps = s32(rd(0x4E0)), b.rumble_level = f(0x4E4), b.sound_steps = s32(rd(0x4E8)), b.sound_level = f(0x4EC);
}

} // namespace

WholeStep::WholeStep(Ee& ee, const OrigRace& race, Disc& disc, const std::string& course, const std::vector<std::string>& cars,
                     const sim::ai::Field* field)
    : ee_(ee), race_(race) {
    session_ = sim::Session::open(disc, course, cars);
    if (std::getenv("GT4R_AI_GLOBALS")) { // the program's data words the race's set-up changed (the image holds others)
        const sim::Program program = sim::Program::load(disc);
        for (u32 a = 0x620000; a < 0x624000; a += 4)
            if (program.w(a) != ee.r32(a)) std::printf("global %06X: image %08X, race %08X\n", a, program.w(a), ee.r32(a));
    }
    free_ = std::getenv("GT4R_AI_WHOLEFREE") != nullptr;
    if (free_ && std::atoi(std::getenv("GT4R_AI_WHOLEFREE")) == 2) {
        std::vector<size_t> computer(cars.size());
        for (size_t i = 0; i < cars.size(); ++i) computer[i] = i;
        own_ = sim::ai::Field::open(disc, course, *session_, computer);
        if (field) own_->set_band(field->band());
    }
    if (field)
        for (size_t i = 0; i < cars.size(); ++i) session_->set_driver_aids(i, field->params(i).aids, field->params(i).grip);
    if (own_)
        for (size_t i = 0; i < cars.size(); ++i) session_->set_driver_aids(i, own_->params(i).aids, own_->params(i).grip);
}
WholeStep::~WholeStep() = default;

void WholeStep::before() {
    if (free_ && loaded_) return;
    loaded_ = true;
    const u32 walls = ee_.r32(race_.runway + 0x84);
    const u32 manager = race_.cond + 0xDA6C;
    for (int i = 0; i < race_.cars; ++i) {
        sim::Record r;
        r.car = session_->record(size_t(i));
        r.x = session_->gt4_record(size_t(i));
        r.layout = session_->wheel_layout(size_t(i));
        load(r, read_image(ee_, race_.car(i)));
        session_->load_state(size_t(i), r.car, r.x);
        if (i == 0) session_->set_random_state(ee_.r32(0x874D50)); // (the program's shared random stream: the flaps')
        std::vector<u8> rec(kBodySize);
        ee_.read_bytes(manager + 0xC + kBodySize * u32(i), rec.data(), kBodySize);
        load_body(session_->body(size_t(i)), rec, walls);
    }
}

void WholeStep::after() {
    // The drivers' outputs of this step (0x464F38 writes them into the controls: the driver object at car + 0x14).
    std::vector<race::CarInput> in(size_t(race_.cars));
    for (int i = 0; i < race_.cars; ++i) {
        const u32 D = race_.car(i) + 0x14;
        race::CarInput& c = in[size_t(i)];
        c.computer = true;
        c.steer_angle = F::raw(ee_.r32(D + 0x30)).host();
        // (0x464F38: in the driver's modes 1 and 2 the pedal slews to the wanted throttle by the step, from the state loaded)
        F thr = F::raw(ee_.r32(D + 0x34));
        const u8 mode = ee_.r8(D + 0x1E);
        if (ee_.r8(0x623E50) == 0 && (mode == 1 || mode == 2)) {
            const ex::Car& car = session_->record(size_t(i));
            const F cur = car.throttle_pedal;
            if (cur < thr) {
                const F n = cur + car.dt;
                thr = thr < n ? thr : n;
            } else {
                const F n = cur - car.dt;
                thr = n < thr ? thr : n;
            }
        }
        c.throttle = thr.host();
        c.brake = F::raw(ee_.r32(D + 0x38)).host();
        c.reverse = ee_.r8(D + 0x3E) != 0;
        c.shift_lock = u8(ee_.r16(D + 0x3C));
    }
    if (own_) { // our drivers on our session, their outputs against the program's race's
        std::vector<race::CarInput> mine(in.size());
        own_->drive(mine);
        own_->apply_time_scales(*session_);
        for (int i = 0; i < race_.cars; ++i) {
            const race::CarInput &a = in[size_t(i)], &b = mine[size_t(i)];
            auto note = [&](const char* name, double x, double y) {
                if (x == y) return;
                Stat& st = inputs_[name];
                if (st.off++ == 0) {
                    char t[200];
                    const sim::ai::Field::Probe p = own_->probe_get(size_t(i));
                    const u32 D = race_.car(i) + 0x14;
                    std::snprintf(t, sizeof t, "first at step %llu car %d: the program's %.9g, ours %.9g (mode %u/%d line %u/%d status %d/%d s %.3f)",
                                  static_cast<unsigned long long>(steps_), i, x, y, ee_.r8(D + 0x1E), p.mode, ee_.r8(D + 0x1C), p.line, int(gt3r::s8(ee_.r8(D + 0x43))),
                                  p.status, double(p.s));
                    st.first = t;
                }
            };
            note("input steer", a.steer_angle, b.steer_angle), note("input throttle", a.throttle, b.throttle), note("input brake", a.brake, b.brake);
            note("input reverse", a.reverse, b.reverse), note("input shift lock", a.shift_lock, b.shift_lock);
        }
        in = mine;
    }
    session_->step_once(in);
    ++steps_;
    bool clean = true;
    const char* only = std::getenv("GT4R_AI_WHOLECAR"); // (one car's fields only)
    for (int i = 0; i < race_.cars; ++i) {
        if (only && std::atoi(only) != i) continue;
        sim::Record theirs;
        theirs.car = session_->record(size_t(i));
        theirs.x = session_->gt4_record(size_t(i));
        load(theirs, read_image(ee_, race_.car(i)));
        sim::Record mine;
        mine.car = session_->record(size_t(i));
        mine.x = session_->gt4_record(size_t(i));
        for (const Slot& s : slots()) {
            const u8* a = reinterpret_cast<const u8*>(&theirs) + s.member;
            const u8* b = reinterpret_cast<const u8*>(&mine) + s.member;
            if (std::memcmp(a, b, s.size) == 0) continue;
            clean = false;
            double x = 0, y = 0;
            if (s.real) {
                F fa, fb;
                std::memcpy(&fa, a, 4), std::memcpy(&fb, b, 4);
                x = fa.host(), y = fb.host();
            } else {
                u32 ua = 0, ub = 0;
                std::memcpy(&ua, a, std::min<u32>(s.size, 4)), std::memcpy(&ub, b, std::min<u32>(s.size, 4));
                x = ua, y = ub;
            }
            Stat& st = fields_[s.name];
            if (st.off++ == 0) {
                char t[160];
                std::snprintf(t, sizeof t, "first at step %llu car %d: the program's %.9g, ours %.9g", static_cast<unsigned long long>(steps_), i, x, y);
                st.first = t;
            }
            const double d = std::fabs(x - y), rel = s.real ? d / std::max(std::fabs(x), 1e-6) : d;
            if (rel >= st.worst) {
                st.worst = rel;
                char t[160];
                std::snprintf(t, sizeof t, "the program's %.9g, ours %.9g (car %d, step %llu)", x, y, i, static_cast<unsigned long long>(steps_));
                st.example = t;
            }
        }
    }
    clean_ += clean;
}

void WholeStep::print() const {
    std::printf("  whole step from the program's state  %llu steps, %llu with every placed field equal\n", static_cast<unsigned long long>(steps_),
                static_cast<unsigned long long>(clean_));
    for (const auto& [n, s] : inputs_)
        std::printf("      %-28s differs in %llu: %s\n", n.c_str(), static_cast<unsigned long long>(s.off), s.first.c_str());
    for (const auto& [n, s] : fields_)
        std::printf("      %-28s differs in %llu, most by %.3g: %s; %s\n", n.c_str(), static_cast<unsigned long long>(s.off), s.worst, s.example.c_str(), s.first.c_str());
}

} // namespace gt4r::diff
