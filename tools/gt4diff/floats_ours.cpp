// Our session beside the program's run of a demonstration (floats.h OursBeside): from the program's set-up (every car's
// record and collision body, the random register, the start's hold), fed the same pad records, the driving line's
// heading the program's active steering read and the computer drivers' outputs of the program's run, compared with the
// program's car 0 after every step.

#include "floats.h"
#include "orig.h"
#include "taps.h"

#include "gt4r/sim/car_layout.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/race/race_start.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/rules_body.h"
#include "gt4r/sim/session.h"

#include <cmath>
#include <cstring>

namespace gt4r::diff {

namespace {

using gt3r::ee::F;
namespace ex = gt3r::sim::exact;

constexpr u32 kRandom = 0x874D50;          // the program's shared random stream
constexpr u32 kBodySize = 0x4F0;           // a collision body (the manager at conductor + 0xDA6C, bodies from +0xC)
constexpr u32 kAfterLineHeading = 0x3491AC; // 0x4649F0's return into the steering 0x348850 ($s1 = F, f0 the heading)

// (as states.cpp reads them)
ex::Aids read_aids(Ee& ee, u32 at) {
    ex::Aids a;
    auto f = [&](u32 o) { return F::raw(ee.r32(at + o)); };
    a.a_gain = f(0x00), a.a_thr = f(0x04), a.d_gain = f(0x08), a.c_gain = f(0x10), a.c_thr = f(0x14), a.e_gain = f(0x18);
    a.tcs_gain = f(0x20), a.tcs_lat_a = f(0x24), a.tcs_lat_b = f(0x28), a.tcs_scale = f(0x2C);
    for (u32 k = 0; k < 2; ++k) {
        a.a_wheels[k] = ee.r8(at + 0x0C + k), a.d_wheels[k] = ee.r8(at + 0x0E + k);
        a.c_wheels[k] = ee.r8(at + 0x1C + k), a.e_wheels[k] = ee.r8(at + 0x1E + k);
    }
    for (u32 k = 0; k < 4; ++k) a.on[k] = ee.r8(at + 0x30 + k);
    return a;
}

void load_body(sim::collide::Body& b, const u8* rec, u32 walls) {
    auto rd = [&](u32 o) {
        u32 x;
        std::memcpy(&x, rec + o, 4);
        return x;
    };
    auto f = [&](u32 o) { return F::raw(rd(o)); };
    auto wall = [&](u32 o) {
        const u32 p = rd(o);
        return p >= walls ? int((p - walls) / 16) : -1;
    };
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

struct OursBeside::Impl {
    Disc& disc;
    OrigRace* o = nullptr;
    std::unique_ptr<sim::Program> program;
    std::unique_ptr<sim::Session> session;
    std::map<u32, F> heading;
    race::PadRecord pad, idle;
    bool rolling0 = false, released = false;
    explicit Impl(Disc& d) : disc(d) {}
};

OursBeside::OursBeside(Disc& disc) : impl_(std::make_unique<Impl>(disc)) {}
OursBeside::~OursBeside() = default;

void OursBeside::load_all(Ee& ee, int first) {
    Impl& m = *impl_;
    OrigRace& o = *m.o;
    // Every car's record and collision body as the program's set-up left them.
    const u32 walls = ee.r32(o.runway + 0x84), manager = o.cond + 0xDA6C;
    for (int i = first; i < o.cars; ++i) {
        sim::Record r;
        r.car = m.session->record(size_t(i));
        r.x = m.session->gt4_record(size_t(i));
        r.layout = m.session->wheel_layout(size_t(i));
        load(r, read_image(ee, o.car(i)));
        m.session->load_state(size_t(i), r.car, r.x);
        std::vector<u8> rec(kBodySize);
        ee.read_bytes(manager + 0xC + kBodySize * u32(i), rec.data(), kBodySize);
        load_body(m.session->body(size_t(i)), rec.data(), walls);
    }
    if (first == 0) m.session->set_random_state(ee.r32(kRandom));
}

bool OursBeside::begin(Ee& ee, OrigRace& o, db::SpecDb& sdb, const std::string& course, int hold) {
    Impl& m = *impl_;
    m.o = &o;
    try {
        m.program = std::make_unique<sim::Program>(sim::Program::load(m.disc));
        const gt3r::Volume::Entry* file = m.disc.volume().find("crs/" + course);
        if (!file) return error = "no course file " + course, false;
        const Bytes data = m.disc.volume().read(*file);
        const course::CourseFile parsed = course::CourseFile::parse(data);
        std::optional<course::Runway> road = course::course_road(parsed, data);
        if (!road) return error = "the course has no road", false;
        std::vector<sim::Entry> entries(size_t(o.cars));
        for (int i = 0; i < o.cars; ++i) {
            const u32 en = o.entries[size_t(i)];
            ee.read_bytes(en + 0x20, entries[size_t(i)].equipment.bytes.data(), sim::Equipment::kSize);
            entries[size_t(i)].grid_place = int(ee.r32(en + 0x3444));
            entries[size_t(i)].automatic = ee.r8(en + 0x3448) == 0;
            entries[size_t(i)].asm_on = ee.r8(en + 0x3450) != 0;
            entries[size_t(i)].tcs_on = ee.r8(en + 0x3454) != 0;
            // (the rolling start's pace, 0x3F7880: the race's speed CFG +0x6E km/h; without one, CFG +0x4E when the
            // conductor's virtual 6 answers (a license test's does), else the place's byte K +0xF866 + i; the session takes
            // it from the entries)
            u32 kmh = ee.r8(o.cond + 0xF818 + 0x6E);
            if (kmh == 0) {
                const u32 vt = ee.r32(o.cond + kCondVtable), slot = vt + 8 * 6;
                const bool v6 = ee.call(ee.r32(slot + 4), {Arg::ptr(o.cond + u32(s32(s16(ee.r16(slot)))))}, 1'000'000).v0.lo != 0;
                kmh = v6 ? ee.r8(o.cond + 0xF818 + 0x4E) : ee.r8(o.cond + 0xF866 + ee.r8(o.car(i) + kF + 0x440));
            }
            entries[size_t(i)].start_speed = float(kmh) / 3.6f;
        }
        // GT4R_FLOATS_OWN=1: from our own set-up (race/race_start.h, the race screen's), not the program's: its fields that differ
        // listed, and the run from there.
        if (own = std::getenv("GT4R_FLOATS_OWN") != nullptr) {
            std::vector<u8> row(151);
            ee.read_bytes(ee.r32(o.info + 0x70), row.data(), u32(row.size()));
            const race::Conductor c = ee.r32(o.cond + kCondVtable) == 0x685F50 ? race::Conductor::Training : race::Conductor::License;
            race::StartCourse starts(*road, parsed.section(data, course::kSlotRunway), race::course_start(sdb, course).way);
            const race::StartRace& race = starts.set_race(c, row, int(gt3r::s8(ee.r8(o.cond + 0xF818 + 0x21))));
            if (const std::optional<course::AiData> ad = sim::ai::Field::read(m.disc, course)) starts.add_lines(*ad, sim::ai::Tables::load(*m.program));
            for (int i = 0; i < o.cars; ++i) {
                sim::Entry& e = entries[size_t(i)];
                const u32 en = o.entries[size_t(i)];
                const race::PlannedStart p = race::plan_start(race, size_t(i), e.grid_place);
                e.line_start = p.line, e.place = p.place;
                e.wheel_aid = ee.r8(en + 0x3458) != 0, e.driver = ee.r8(en + 0x344C), e.control = i > 0 ? 2 : 0;
                e.condition.emplace();
                ee.read_bytes(en + 0x3468, e.condition->data(), 0x20);
            }
            sim::Session::Options so;
            so.hold_steps = 0;
            m.session = std::make_unique<sim::Session>(sdb, *m.program, std::make_shared<const course::Runway>(*road), entries, sim::course_row(sdb, course), &so);
            for (int i = 0; i < o.cars; ++i)
                m.session->set_crossings(size_t(i), race::planned_crossings(race, race::plan_start(race, size_t(i), entries[size_t(i)].grid_place), m.session->cars()[size_t(i)].course_v));
        } else
        m.session = std::make_unique<sim::Session>(sdb, *m.program, std::make_shared<const course::Runway>(std::move(*road)), entries);
    } catch (const std::exception& e) {
        return error = std::string("our session: ") + e.what(), false;
    }
    // The course's kind as the program's conductor has it (K +0xCBDD).
    m.session->set_course_kind(*m.program, ee.r8(o.cond + 0xCBDD));
    if (std::getenv("GT4R_FLOATS_BUMPS")) {
        const sim::rules::BumpTables mine = sim::rules::bump_tables(std::as_const(*m.program), ee.r8(o.cond + 0xCBDD));
        const sim::rules::BumpTables theirs = sim::rules::bump_tables([&](u32 a) { return ee.r32(a); }, [&](u32 a) { return ee.r8(a); });
        for (int s = 0; s < 24; ++s)
            if (mine.amp[size_t(s)].bits != theirs.amp[size_t(s)].bits || mine.freq[size_t(s)].bits != theirs.freq[size_t(s)].bits ||
                mine.scaled[size_t(s)] != theirs.scaled[size_t(s)] || mine.mode[size_t(s)] != theirs.mode[size_t(s)])
                std::printf("  bump row %d: ours %08X %08X %u %u program %08X %08X %u %u\n", s, mine.amp[size_t(s)].bits, mine.freq[size_t(s)].bits,
                            mine.scaled[size_t(s)], mine.mode[size_t(s)], theirs.amp[size_t(s)].bits, theirs.freq[size_t(s)].bits, theirs.scaled[size_t(s)],
                            theirs.mode[size_t(s)]);
    }
    // The race's class grips as the program's conductor has them (CFG +0x18 .. +0x1B).
    std::array<gt3r::s8, 4> grip_bytes;
    for (u32 k = 0; k < 4; ++k) grip_bytes[k] = gt3r::s8(ee.r8(o.cond + 0xF818 + 0x18 + k));
    m.session->set_class_grip(grip_bytes);
    // A computer's car: its driver's aid block and grip by tire class (the driver's parameter block, conductor + 0x10024 + 4 i).
    // (the player's too: while its driver drives it in its rolling start, 0x3F7828 reads the block)
    for (int i = 0; i < o.cars; ++i) {
        const u32 pb = ee.r32(o.cond + 0x10024 + 4 * u32(i));
        if (!pb) continue;
        std::array<F, 3> grip;
        for (u32 k = 0; k < 3; ++k) grip[k] = F::raw(ee.r32(pb + 0xA8 + 4 * k));
        m.session->set_driver_aids(size_t(i), read_aids(ee, pb + 0x24), grip);
    }
    if (!own) load_all(ee);
    else {
        compare(ee, "start");
        m.session->set_random_state(ee.r32(kRandom));
    }
    // (the program counts the race information's +0xBC down before its step and holds while it is not 0; the session
    // holds while its count is not 0 and counts it down after)
    if (hold > 1) m.session->set_countdown(u32(hold - 1));
    Impl* mp = &m;
    // The player's car let go from its rolling start in the program's step (0x345228, at its crossing): ours after its step.
    const u32 car0 = o.car(0);
    ee.add_probe(0x345228, [mp, car0](Ee& e) {
        if (e.g32(4) == car0) mp->released = true;
    });
    ee.add_probe(kAfterLineHeading, [mp](Ee& e) { mp->heading[e.g32(17) - kF] = F::raw(e.fpr[0]); });
    return true;
}

// Every placed field of every car that differs.
int OursBeside::compare(Ee& ee, const char* what) {
    Impl& m = *impl_;
    int n = 0;
    for (int i = 0; i < m.o->cars; ++i) {
        sim::Record theirs, mine;
        theirs.car = mine.car = m.session->record(size_t(i));
        theirs.x = mine.x = m.session->gt4_record(size_t(i));
        theirs.layout = mine.layout = m.session->wheel_layout(size_t(i));
        load(theirs, read_image(ee, m.o->car(i)));
        for (const Slot& sl : slots()) {
            const u8* a = reinterpret_cast<const u8*>(&theirs) + sl.member;
            const u8* b = reinterpret_cast<const u8*>(&mine) + sl.member;
            if (std::memcmp(a, b, sl.size) == 0) continue;
            u32 x = 0, y = 0;
            std::memcpy(&x, a, std::min<u32>(sl.size, 4)), std::memcpy(&y, b, std::min<u32>(sl.size, 4));
            std::printf("  ours %s car %d: %s program %08X ours %08X (%g %g)\n", what, i, sl.name.c_str(), x, y, double(F::raw(x).host()), double(F::raw(y).host()));
            ++n;
        }
    }
    return n;
}

void OursBeside::before_step(gt3r::diff::Ee& ee) {
    impl_->heading.clear();
    impl_->rolling0 = ee.r8(impl_->o->car(0) + kF + 0x467) == 1;
    if (resync) load_all(ee);
    else if (pin_others) load_all(ee, 1);
}

void OursBeside::step(Ee& ee, const u8* record) {
    Impl& m = *impl_;
    OrigRace& o = *m.o;
    std::copy(record, record + 0x20, m.pad.bytes.begin());
    std::vector<race::CarInput> in(size_t(o.cars));
    // A computer driver's outputs of this step (0x464F38 into its driver object), the pedal slewed by the step in its
    // modes 1 and 2 (as states.cpp).
    auto computer = [&](int i, race::CarInput& c) {
        const u32 D = o.car(i) + sim::layout::kDriver;
        c.steer_angle = F::raw(ee.r32(D + 0x30)).host();
        F thr = F::raw(ee.r32(D + 0x34));
        const u8 mode = ee.r8(D + 0x1E);
        if (ee.r8(0x623E50) == 0 && (mode == 1 || mode == 2)) {
            const ex::Car& car = m.session->record(size_t(i));
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
        c.brake = F::raw(ee.r32(D + 0x38)).host();
        c.reverse = ee.r8(D + 0x3E) != 0;
        c.shift_lock = u8(ee.r16(D + 0x3C));
    };
    for (int i = 0; i < o.cars; ++i) {
        race::CarInput& c = in[size_t(i)];
        if (const auto h = m.heading.find(o.car(i)); h != m.heading.end()) m.session->set_line_heading(size_t(i), h->second);
        if (i == 0) {
            c.pad = &m.pad;
            // (held in its rolling start, driver kind 1 before the step: its computer driver's outputs drive it)
            if (m.rolling0) computer(i, c);
            continue;
        }
        if (ee.r8(o.car(i) + kF + 0x466) == 0) {
            c.pad = &m.idle;
            continue;
        }
        c.computer = true;
        computer(i, c);
    }
    m.session->step_once(in);
    if (m.released) m.session->release_start(0), m.released = false;
    // Car 0 against the program's: its place (F +0 .. +8) and velocity.
    const ex::Car& c = m.session->record(0);
    const u32 F0 = o.car(0) + kF;
    const double dx = double(c.pos.x.host()) - ee.rf(F0), dy = double(c.pos.y.host()) - ee.rf(F0 + 4), dz = double(c.pos.z.host()) - ee.rf(F0 + 8);
    const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
    const bool same = c.pos.x.bits == ee.r32(F0) && c.pos.y.bits == ee.r32(F0 + 4) && c.pos.z.bits == ee.r32(F0 + 8);
    // The placed fields of every car that differ (from the first step any does, for `list_steps` steps).
    if (list_steps > 0 && (first_fields_step >= 0 ? steps < first_fields_step + list_steps : true)) {
        for (int i = 0; i < o.cars; ++i) {
            sim::Record theirs, mine;
            theirs.car = mine.car = m.session->record(size_t(i));
            theirs.x = mine.x = m.session->gt4_record(size_t(i));
            theirs.layout = mine.layout = m.session->wheel_layout(size_t(i));
            load(theirs, read_image(ee, o.car(i)));
            for (const Slot& sl : slots()) {
                const u8* a = reinterpret_cast<const u8*>(&theirs) + sl.member;
                const u8* b = reinterpret_cast<const u8*>(&mine) + sl.member;
                if (std::memcmp(a, b, sl.size) == 0) continue;
                if (first_fields_step < 0) first_fields_step = steps;
                u32 x = 0, y = 0;
                std::memcpy(&x, a, std::min<u32>(sl.size, 4)), std::memcpy(&y, b, std::min<u32>(sl.size, 4));
                std::printf("  ours step %d car %d: %s program %08X ours %08X\n", steps, i, sl.name.c_str(), x, y);
            }
        }
    }
    if (!same && first_bits < 0) first_bits = steps;
    last_distance = d;
    worst = std::max(worst, d);
    if (first_apart < 0 && d > 0.001) first_apart = steps;
    ++steps;
}

} // namespace gt4r::diff
