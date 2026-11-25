#pragma once

// Two machines of an online race in one process: each its own session with the other's car posed from the other's states,
// which pass through the wire format with a delay, jitter and loss (docs/specs/net.md "Race").

#include "test_framework.h"

#include "gt4r/net/net_field.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/line_driver.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/session.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/sim/ai/run.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gt4r::test::net {

using gt3r::s64;

inline Disc* gt4_disc() {
    for (const std::unique_ptr<Disc>& d : discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

// What a course and cars are built from, read once.
struct World {
    Disc* disc = nullptr;
    db::SpecDb db;
    sim::Program program;
    std::string course;
    std::shared_ptr<const course::Runway> road;
    std::optional<sim::CourseRow> row;

    World(Disc& d, const std::string& name) : disc(&d), db(sim::ai::open_specdb(d)), program(sim::Program::load(d)), course(name) {
        gt3r::Volume* vol = &d.volume();
        if (const gt3r::Volume::Entry* e = vol->find("crs/" + name)) {
            const Bytes data = vol->read(*e);
            if (std::optional<course::Runway> r = course::course_road(course::CourseFile::parse(data), data)) road = std::make_shared<const course::Runway>(std::move(*r));
        }
        row = sim::course_row(db, name);
    }
    std::unique_ptr<sim::Session> session(std::span<const sim::Entry> entries, int hold_steps = 0) {
        sim::Session::Options o;
        o.hold_steps = hold_steps;
        return std::make_unique<sim::Session>(db, program, road, entries, row, &o);
    }
    std::vector<sim::Entry> entries(std::span<const std::string> labels) {
        std::vector<sim::Entry> e(labels.size());
        for (size_t i = 0; i < labels.size(); ++i) {
            if (!sim::stock_car(db, program, labels[i], e[i].equipment)) throw std::runtime_error("no car " + labels[i]);
            e[i].grid_place = int(i);
        }
        return e;
    }
};

// An input's fields as an analog pad's frame (as a session makes one of them).
inline race::PadRecord pad_of(const race::CarInput& in) {
    gt3r::s8 shift = in.shift_up ? 1 : in.shift_down ? -1 : 0;
    sim::rules::PadFrame f;
    f.flags = 7, f.kind = 3, f.shift = shift;
    f.steer = gt3r::s16(std::lround(std::clamp(-in.steer, -1.0f, 1.0f) * 4096.0f));
    f.throttle = u16(std::lround(std::clamp(in.throttle, 0.0f, 1.0f) * 4096.0f));
    f.brake = u16(std::lround(std::clamp(in.brake, 0.0f, 1.0f) * 4096.0f));
    if (in.reverse) f.flags |= 8, f.back = f.throttle, f.throttle = 0;
    race::PadRecord r;
    f.to_bytes(r.bytes.data());
    return r;
}

struct Link {
    double delay_ms = 0, jitter_ms = 0, loss = 0;
};

// One of the race's machines: its own car first in its session, the others after it.
struct Machine {
    std::unique_ptr<sim::Session> session;
    std::unique_ptr<gt4r::net::NetField> field;
    std::vector<size_t> seat;     // session index -> the race's car (0 .. n - 1)
    std::function<race::CarInput(Machine&)> drive;
    race::PadRecord pad;
    s64 clock = 0;
    struct Message {
        double at_ms;
        size_t from;              // the sender's race car
        Bytes state;              // (empty: a hit)
        gt4r::net::Hit hit;
    };
    std::vector<Message> inbox;
    u32 warnings = 0;
    std::string warning;
    s64 contact_steps = 0;        // steps our car touched another's
};

struct Race {
    World& world;
    std::vector<std::unique_ptr<Machine>> machines;
    Link link;
    double now_ms = 0;
    u32 seed = 0x1234567;
    u32 sent = 0, lost = 0;
    float rnd() {
        seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
        return float(seed & 0xFFFFFF) / float(0x1000000);
    }

    // `labels`: the cars, in grid order, one a machine. `hold`: steps of the start's countdown.
    Race(World& w, std::span<const std::string> labels, Link l, int hold = 0, const std::function<void(std::vector<sim::Entry>&)>& change = {}) : world(w), link(l) {
        std::vector<sim::Entry> all = w.entries(labels);
        if (change) change(all);
        for (size_t m = 0; m < all.size(); ++m) {
            auto mc = std::make_unique<Machine>();
            std::vector<sim::Entry> mine{all[m]};
            mc->seat = {m};
            for (size_t k = 0; k < all.size(); ++k)
                if (k != m) mine.push_back(all[k]), mc->seat.push_back(k);
            mc->session = w.session(mine);
            std::vector<gt4r::net::NetField::CarInfo> info;
            for (size_t k = 0; k < mine.size(); ++k)
                info.push_back({u8(mc->seat[k]), "player", mine[k].asm_on, mine[k].tcs_on, mc->session->record(k).p.top_speed.host()});
            mc->field = std::make_unique<gt4r::net::NetField>(*mc->session, std::move(info), 0);
            mc->clock = -s64(hold) * gt4r::net::kClockPerStep;
            machines.push_back(std::move(mc));
        }
    }
    size_t index_in(const Machine& m, size_t race_car) const {
        for (size_t k = 0; k < m.seat.size(); ++k)
            if (m.seat[k] == race_car) return k;
        return 0;
    }
    void send(size_t from, const Bytes& state, const gt4r::net::Hit* hit, int to = -1) {
        for (size_t m = 0; m < machines.size(); ++m) {
            if (m == from || (to >= 0 && size_t(to) != m)) continue;
            if (!hit) {
                ++sent;
                if (rnd() < link.loss) {
                    ++lost;
                    continue;
                }
            }
            const double at = now_ms + link.delay_ms + rnd() * link.jitter_ms;
            machines[m]->inbox.push_back({at, from, hit ? Bytes{} : state, hit ? *hit : gt4r::net::Hit{}});
        }
    }
    // One machine's step.
    void step(size_t m) {
        Machine& mc = *machines[m];
        for (size_t k = 0; k < mc.inbox.size();) {
            Machine::Message& g = mc.inbox[k];
            if (g.at_ms > now_ms + 1e-6) {
                ++k;
                continue;
            }
            const size_t car = index_in(mc, g.from);
            if (g.state.empty()) mc.field->take_hit(car, g.hit, mc.clock);
            else {
                gt3r::net::Reader r(g.state);
                gt3r::net::CarSnapshot s;
                CHECK(gt3r::net::read_snapshot(r, s));
                mc.field->receive(car, s, mc.clock);
            }
            mc.inbox.erase(mc.inbox.begin() + std::ptrdiff_t(k));
        }
        mc.field->before_step(mc.clock);
        std::vector<race::CarInput> in(mc.session->cars().size());
        in[0] = mc.drive ? mc.drive(mc) : race::CarInput{};
        if (!in[0].computer && !in[0].pad) {
            mc.pad = pad_of(in[0]);
            in[0] = race::CarInput{};
            in[0].pad = &mc.pad;
        }
        if (mc.clock < 0) mc.session->hold_once(in);
        else mc.session->step_once(in);
        mc.clock += gt4r::net::kClockPerStep;
        mc.field->after_step(mc.clock);
        if (!mc.field->hits().empty()) ++mc.contact_steps;
        gt3r::net::Writer w;
        gt3r::net::write_snapshot(w, mc.field->state(in[0], mc.clock, 0));
        send(m, w.data, nullptr);
        for (const gt4r::net::NetField::Sent& h : mc.field->hits()) send(m, {}, &h.hit, int(mc.seat[h.car]));
        for (const gt4r::net::NetField::Flag& f : mc.field->take_flags()) {
            ++mc.warnings;
            mc.warning = std::string(gt3r::net::flag_text(f.kind)) + " " + std::to_string(f.value);
        }
    }
    void step_all() {
        for (size_t m = 0; m < machines.size(); ++m) step(m);
        now_ms += 1000.0 / 60;
    }
    // Where machine m has race car `car` (its record's place), and where its owner has it.
    course::Vec3 place(size_t m, size_t car) const {
        const Machine& mc = *machines[m];
        const gt3r::sim::exact::Car& c = mc.session->record(index_in(mc, car));
        return {c.pos.x.host(), c.pos.z.host(), -c.pos.y.host()};
    }
    double error(size_t m, size_t car) const {
        const course::Vec3 a = place(m, car), b = place(car, car);
        return std::sqrt(double(a.x - b.x) * (a.x - b.x) + double(a.y - b.y) * (a.y - b.y) + double(a.z - b.z) * (a.z - b.z));
    }
};

struct Errors {
    double sum2 = 0, worst = 0;
    u64 n = 0;
    void add(double e) { sum2 += e * e, worst = std::max(worst, e), ++n; }
    double rms() const { return n ? std::sqrt(sum2 / double(n)) : 0; }
};

// A pad driver along the road's middle with a steering weave.
inline std::function<race::CarInput(Machine&)> weaving(float throttle = 1.0f, float weave = 0.25f, double phase = 0) {
    auto d = std::make_shared<std::unique_ptr<sim::LineDriver>>();
    return [=](Machine& m) {
        if (!*d) *d = std::make_unique<sim::LineDriver>(*m.session, 0);
        race::CarInput in = (*d)->drive(throttle, true);
        in.steer = std::clamp(in.steer + weave * float(std::sin(double(m.clock) / 3000.0 * 5.0 + phase)), -1.0f, 1.0f);
        return in;
    };
}

// A pad driver at the other car of the race, flat out, where this machine shows it.
inline std::function<race::CarInput(Machine&)> chasing() {
    return [](Machine& m) {
        const race::CarState &me = m.session->cars()[0], &them = m.session->cars()[1];
        const float dx = them.position.x - me.position.x, dz = them.position.z - me.position.z;
        const float along = dx * me.forward.x + dz * me.forward.z, across = dx * me.right.x + dz * me.right.z;
        race::CarInput in;
        in.throttle = 1;
        in.steer = std::clamp(std::atan2(across, std::max(along, 1.0f)) * 2.5f, -1.0f, 1.0f);
        return in;
    };
}

// A driver that changes its mind: along the road's middle, with steering jabs to full lock, the throttle let go and the brake
// stabbed at random moments, a manual gearbox shifted at odd times.
inline std::function<race::CarInput(Machine&)> jerky(u32 seed, bool manual) {
    struct State {
        std::unique_ptr<sim::LineDriver> line;
        u32 seed = 1;
        int jab = 0, lift = 0, stab = 0, hold = 0;
        float side = 0;
    };
    auto st = std::make_shared<State>();
    st->seed = seed | 1;
    return [=](Machine& m) {
        State& s = *st;
        if (!s.line) s.line = std::make_unique<sim::LineDriver>(*m.session, 0);
        auto rnd = [&] { return s.seed ^= s.seed << 13, s.seed ^= s.seed >> 17, s.seed ^= s.seed << 5, s.seed & 0xFFFF; };
        race::CarInput in = s.line->drive(1.0f, true);
        if (s.jab == 0 && rnd() < 1400) s.jab = 4 + int(rnd() % 14), s.side = rnd() & 1 ? 0.8f : -0.8f;
        if (s.lift == 0 && rnd() < 900) s.lift = 6 + int(rnd() % 30);
        if (s.stab == 0 && rnd() < 500) s.stab = 4 + int(rnd() % 20);
        if (s.jab > 0) --s.jab, in.steer = std::clamp(in.steer + s.side, -1.0f, 1.0f);
        if (s.lift > 0) --s.lift, in.throttle = 0;
        if (s.stab > 0) --s.stab, in.brake = 1, in.throttle = 0;
        const race::CarState& me = m.session->cars()[0];
        if (manual && s.hold-- <= 0) {
            const float top = float(m.session->record(0).p.e.rev_limit);
            if (me.engine_rpm > top * 0.92f) in.shift_up = true, s.hold = 20;
            else if (me.engine_rpm < top * 0.45f && me.gear > 1) in.shift_down = true, s.hold = 20;
        }
        return in;
    };
}

} // namespace gt4r::test::net
