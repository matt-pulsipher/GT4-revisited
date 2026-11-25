// The race step from the original's saved states; see states.h.

#include "states.h"

#include "gt4r/formats/db/race_events.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/ai/bspec_skill.h"
#include "gt4r/sim/ai/entrant.h"
#include "gt4r/sim/ai/run.h"

#include "taps.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/sim/exact_bridge.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/rules_body.h"
#include "gt4r/sim/session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace gt4r::diff {

sim::ai::Field::Probe driver_probe(Ee& ee, const OrigRace& o, int i, float lap); // (ai.cpp)


namespace {

using gt3r::ee::F;

constexpr u32 kBootCounter = 0x0087FA88; // +1 a vsync since boot
constexpr u32 kPadRead = 0x426E80;       // RaceInput's virtual +0x40: the pad device's state into the controller's frame (a1)
constexpr u32 kRaceStep = 0x34DB30;      // (conductor, 0)
constexpr u32 kRandom = 0x874D50;        // the program's shared random stream
constexpr u32 kBodySize = 0x4F0;         // a collision body (the manager at conductor + 0xDA6C, bodies from +0xC)

struct StateFiles {
    Bytes ee, scratch, vu0;
};

Bytes read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// A state's memory: `path` is its eeMemory.bin or the folder holding it.
bool read_state(const std::string& path, StateFiles& s, std::string& err) {
    std::filesystem::path p(path);
    if (std::filesystem::is_directory(p)) p /= "eeMemory.bin";
    s.ee = read_file(p);
    if (s.ee.size() != Ee::kRamSize) return err = "no 32 MB main memory at " + p.string(), false;
    s.scratch = read_file(p.parent_path() / "Scratchpad.bin");
    s.vu0 = read_file(p.parent_path() / "vu0Memory.bin");
    return true;
}

u32 rd32(const Bytes& m, u32 a) {
    u32 v;
    std::memcpy(&v, m.data() + a, 4);
    return v;
}

// The conductor: the word at car 0 + 4 is the conductor (car 0 = conductor + 0x6698) and car 0 + 0x10 its parameter block
// (conductor + 0x98), with 1 .. 6 cars.
u32 find_conductor(const Bytes& m) {
    for (u32 a = 0x100000 + 0x669C; a + 0x10000 < m.size(); a += 4) {
        const u32 c = a - 0x669C;
        if (rd32(m, a) != c || rd32(m, a + 0xC) != c + kParams0) continue;
        const u32 n = rd32(m, c + kCondCars);
        if (n >= 1 && n <= 6) return c;
    }
    return 0;
}

// The race as state 0 has it, in the shape the other checks read (orig.h).
bool race_of(Ee& ee, u32 cond, OrigRace& o, std::string& err) {
    o.cond = cond;
    o.cars = int(ee.r32(cond + kCondCars));
    o.info = ee.r32(cond);
    o.course = ee.r32(cond + kCondCourse);
    o.container = o.course ? ee.r32(o.course + 4) : 0;
    o.runway = o.container ? ee.r32(o.container + 4 * u32(course::kSlotRunway)) : 0;
    if (!o.info || !o.runway) return err = "the state's race has no race information or road", false;
    const u32 list = ee.r32(o.info + 0x60);
    const u32 n = ee.r32(list), arr = ee.r32(list + 8);
    for (u32 k = 0; k < n && k < 6; ++k) o.entries.push_back(ee.r32(arr + 4 * k));
    if (int(o.entries.size()) < o.cars) return err = "the race information lists fewer entries than cars", false;
    return true;
}

// An aid block in the interpreter's memory (the engine's ex::Aids layout).
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

// A car's record image out of a state's memory.
Image image_of(const Bytes& m, u32 car) {
    Image im;
    im.car.assign(m.begin() + car, m.begin() + car + sim::layout::kCarSize);
    const u32 p = rd32(m, car + 0x10);
    im.params.assign(m.begin() + p, m.begin() + p + sim::layout::kParamsSize);
    return im;
}

double value_of(const Slot& s, const u8* p) {
    if (s.real) {
        F f;
        std::memcpy(&f.bits, p, 4);
        return f.host();
    }
    u32 u = 0;
    std::memcpy(&u, p, std::min<u32>(s.size, 4));
    if (s.size == 1) return double(u8(u));
    if (s.size == 2) return double(u16(u));
    return double(u);
}

std::string describe(const Slot& s, const u8* theirs, const u8* mine, const char* a, const char* b) {
    char t[200];
    std::snprintf(t, sizeof t, "%s (%s %.9g, %s %.9g)", s.name.c_str(), a, value_of(s, theirs), b, value_of(s, mine));
    return t;
}

// The placed fields of two images that differ, in the record's order.
std::vector<const Slot*> differing(const sim::Record& a, const sim::Record& b) {
    std::vector<const Slot*> v;
    for (const Slot& s : slots())
        if (std::memcmp(reinterpret_cast<const u8*>(&a) + s.member, reinterpret_cast<const u8*>(&b) + s.member, s.size) != 0) v.push_back(&s);
    return v;
}

size_t bytes_differing(const Image& a, const Image& b) {
    size_t n = 0;
    for (size_t k = 0; k < a.car.size(); ++k) n += a.car[k] != b.car[k];
    for (size_t k = 0; k < a.params.size(); ++k) n += a.params[k] != b.params[k];
    return n;
}

sim::Record record_of(const Image& im) {
    sim::Record r;
    load(r, im);
    return r;
}

std::string label_of_car(db::SpecDb& sdb, u32 id) {
    for (const db::CarEntry& c : sdb.cars())
        if (c.id == id) return c.label;
    return "car " + std::to_string(id);
}

double dist3(const u8* a, const u8* b) {
    double d = 0;
    for (int k = 0; k < 3; ++k) {
        F x, y;
        std::memcpy(&x.bits, a + 4 * k, 4), std::memcpy(&y.bits, b + 4 * k, 4);
        d += (double(x.host()) - y.host()) * (double(x.host()) - y.host());
    }
    return std::sqrt(d);
}

double speed_of(const u8* car) {
    F v[3];
    for (int k = 0; k < 3; ++k) std::memcpy(&v[k].bits, car + kF + 0x544 + 4 * k, 4);
    return std::sqrt(double(v[0].host()) * v[0].host() + double(v[1].host()) * v[1].host() + double(v[2].host()) * v[2].host());
}

// Field name prefixes not compared in ours (GT4R_STATES_SKIP=a,b,...).
std::vector<std::string> skip_list() {
    std::vector<std::string> v;
    if (const char* e = std::getenv("GT4R_STATES_SKIP")) {
        std::string s = e;
        for (size_t a = 0; a <= s.size();) {
            const size_t b = std::min(s.find(',', a), s.size());
            if (b > a) v.push_back(s.substr(a, b - a));
            a = b + 1;
        }
    }
    return v;
}

} // namespace

bool keys_frame(const std::string& keys, std::array<u8, 0x20>& b) {
    b.fill(0);
    bool cross = false, square = false, left = false, right = false;
    for (size_t a = 0; a <= keys.size();) {
        const size_t e = std::min(keys.find('+', a), keys.size());
        const std::string k = keys.substr(a, e - a);
        a = e + 1;
        if (k.empty() || k == "none") continue;
        if (k == "cross") cross = true;
        else if (k == "square") square = true;
        else if (k == "left") left = true;
        else if (k == "right") right = true;
        else return false;
    }
    // The keyboard pad's frame as the original's controller read it (its +0x150 in the saved states): the D-pad as a full
    // analog steering (+ left), Cross and Square as full analog throttle and brake, the kind 3 while steering, 6 button
    // bytes (all 0).
    const s16 steer = left && !right ? 4096 : right && !left ? -4096 : 0;
    b[0] = u8((steer ? 1 : 0) | (cross ? 2 : 0) | (square ? 4 : 0));
    b[1] = steer ? 3 : 0;
    b[3] = 6;
    auto w16 = [&](u32 o, u16 v) { b[o] = u8(v), b[o + 1] = u8(v >> 8); };
    w16(4, u16(steer));
    w16(8, cross ? 4096 : 0);
    w16(0xA, square ? 4096 : 0);
    return true;
}

StatesResult run_states(Disc& disc, const std::string& state0, const std::string& state1, const StatesOptions& options, bool print) {
    StatesResult res;
    StateFiles s0, s1;
    if (!read_state(state0, s0, res.error) || !read_state(state1, s1, res.error)) return res;
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc, sdb);
    // The game's memory as state 0 left it, over everything the boot set up (the program is the same).
    ee.write_bytes(0, s0.ee);
    if (s0.scratch.size() == Ee::kScratchSize) ee.write_bytes(Ee::kScratchBase, s0.scratch);
    if (s0.vu0.size() == Ee::kVu0MemSize) ee.write_bytes(Ee::kVu0MemBase, s0.vu0);
    const u32 cond = find_conductor(s0.ee);
    if (!cond || find_conductor(s1.ee) != cond) return res.error = "no conductor (or not the same one) in the two states", res;
    OrigRace o;
    if (!race_of(ee, cond, o, res.error)) return res;
    u32 course_id = u32(ee.r64(o.info + 0x78));
    for (const db::CourseEntry& c : sdb.courses())
        if (c.id == course_id) res.course = c.label;
    if (res.course.empty()) return res.error = "the race's course is not in the database", res;

    // The player's controller (an entry's controller with +0x1CC set) and the frame its pad gives.
    int player = -1;
    std::array<u8, 0x20> frame{};
    for (int i = 0; i < o.cars; ++i) {
        const u32 ctl = ee.r32(o.entries[size_t(i)] + 0x343C);
        if (ctl && ee.r32(ctl + 0x1CC) && player < 0) {
            player = i;
            ee.read_bytes(ctl + 0x150, frame.data(), 0x20);
        }
    }
    // The keys: "keys[:steps],keys[:steps],...": each held for its steps (the last to the end).
    std::vector<std::pair<int, std::array<u8, 0x20>>> schedule;
    if (!options.keys.empty()) {
        for (size_t a = 0; a <= options.keys.size();) {
            const size_t e = std::min(options.keys.find(',', a), options.keys.size());
            std::string part = options.keys.substr(a, e - a);
            a = e + 1;
            int steps = -1;
            if (const size_t c = part.find(':'); c != std::string::npos) steps = std::atoi(part.c_str() + c + 1), part.resize(c);
            std::array<u8, 0x20> k{};
            if (!keys_frame(part, k)) return res.error = "unknown keys " + part, res;
            schedule.push_back({steps, k});
        }
        res.frame_matches = player < 0 || schedule.front().second == frame;
    } else schedule.push_back({-1, frame});
    ee.hle(
        kPadRead,
        [&frame](Ee& e) {
            const u32 to = u32(e.a(1));
            for (u32 k = 0; k < 0x20; ++k) e.w8(to + k, frame[k]);
        },
        "the keyboard pad held");

    res.frames = options.frames >= 0 ? options.frames : int(rd32(s1.ee, kBootCounter) - rd32(s0.ee, kBootCounter));
    if (res.frames < 0 || res.frames > 100000) return res.error = "the states' boot counters are not in order", res;

    // Each car's label and entry (its car is the equipment's +0x00).
    std::vector<std::string> labels;
    for (int i = 0; i < o.cars; ++i) {
        const u32 entry = o.entries[size_t(i)];
        labels.push_back(label_of_car(sdb, ee.r32(entry + 0x20)));
        StatesCar c;
        c.label = labels.back(), c.player = i == player;
        res.cars.push_back(c);
    }

    // Ours: a session on the race's course with the entries' equipment, every car's record and body from state 0.
    std::unique_ptr<sim::Session> session;
    race::PadRecord pad;
    std::copy(frame.begin(), frame.end(), pad.bytes.begin());
    const std::vector<std::string> skip = skip_list();
    auto skipped = [&](const std::string& n) {
        for (const std::string& p : skip)
            if (n.compare(0, p.size(), p) == 0) return true;
        return false;
    };
    auto load_ours = [&] {
        const u32 walls = ee.r32(o.runway + 0x84), manager = o.cond + 0xDA6C;
        for (int i = 0; i < o.cars; ++i) {
            sim::Record r;
            r.car = session->record(size_t(i));
            r.x = session->gt4_record(size_t(i));
            r.layout = session->wheel_layout(size_t(i));
            load(r, read_image(ee, o.car(i)));
            session->load_state(size_t(i), r.car, r.x);
            std::vector<u8> rec(kBodySize);
            ee.read_bytes(manager + 0xC + kBodySize * u32(i), rec.data(), kBodySize);
            load_body(session->body(size_t(i)), rec.data(), walls);
        }
        session->set_random_state(ee.r32(kRandom));
    };
    if (options.ours) {
        try {
            const sim::Program program = sim::Program::load(disc);
            const gt3r::Volume::Entry* file = disc.volume().find("crs/" + res.course);
            if (!file) return res.error = "no course file for " + res.course, res;
            const Bytes data = disc.volume().read(*file);
            const course::CourseFile parsed = course::CourseFile::parse(data);
            std::optional<course::Runway> road = course::course_road(parsed, data);
            if (!road) return res.error = "the course has no road", res;
            std::vector<sim::Entry> entries(size_t(o.cars));
            for (int i = 0; i < o.cars; ++i) {
                ee.read_bytes(o.entries[size_t(i)] + 0x20, entries[size_t(i)].equipment.bytes.data(), sim::Equipment::kSize);
                entries[size_t(i)].grid_place = int(ee.r32(o.entries[size_t(i)] + 0x3444));
                // (the pedal style +0x3448: 0 automatic; the aids' switches +0x3450 ASM, +0x3454 TCS)
                entries[size_t(i)].automatic = ee.r32(o.entries[size_t(i)] + 0x3448) == 0;
                entries[size_t(i)].asm_on = ee.r32(o.entries[size_t(i)] + 0x3450) != 0;
                entries[size_t(i)].tcs_on = ee.r32(o.entries[size_t(i)] + 0x3454) != 0;
                entries[size_t(i)].wheel_aid = ee.r32(o.entries[size_t(i)] + 0x3458) != 0;
            }
            session = std::make_unique<sim::Session>(sdb, program, std::make_shared<const course::Runway>(std::move(*road)), entries);
        } catch (const std::exception& e) {
            return res.error = std::string("our session: ") + e.what(), res;
        }
        if (std::getenv("GT4R_STATES_GLOBALS")) { // the program's data words the race left changed (the image holds others)
            const sim::Program program = sim::Program::load(disc);
            for (u32 a = 0x620000; a < 0x624000; a += 4)
                if (program.w(a) != ee.r32(a)) std::printf("global %06X: image %08X, race %08X\n", a, program.w(a), ee.r32(a));
            const sim::rules::BodyGlobals mine = sim::rules::body_globals(program);
            const sim::rules::BodyGlobals theirs = sim::rules::body_globals([&](u32 a) { return ee.r32(a); }, [&](u32 a) { return ee.r8(a); });
            std::printf("body globals: ours %d %d %d %u", mine.lateral_geometry, mine.normal_mode, mine.damper_linear, mine.damper_table);
            for (int k = 0; k < 2; ++k) std::printf(" %08X %08X %08X %08X", mine.bump_knee[k].bits, mine.bump_share[k].bits, mine.rebound_knee[k].bits, mine.rebound_share[k].bits);
            for (int k = 0; k < 6; ++k) std::printf(" %08X", mine.in_line[k].bits);
            std::printf("\nbody globals: race %d %d %d %u", theirs.lateral_geometry, theirs.normal_mode, theirs.damper_linear, theirs.damper_table);
            for (int k = 0; k < 2; ++k) std::printf(" %08X %08X %08X %08X", theirs.bump_knee[k].bits, theirs.bump_share[k].bits, theirs.rebound_knee[k].bits, theirs.rebound_share[k].bits);
            for (int k = 0; k < 6; ++k) std::printf(" %08X", theirs.in_line[k].bits);
            std::printf("\n");
        }
        // A computer's car: its driver's aid block and grip by tire class (the driver's parameter block, conductor + 0x10024 + 4 i).
        for (int i = 0; i < o.cars; ++i) {
            const u32 pb = ee.r32(o.cond + 0x10024 + 4 * u32(i));
            if (i == player || !pb) continue;
            std::array<F, 3> grip;
            for (u32 k = 0; k < 3; ++k) grip[k] = F::raw(ee.r32(pb + 0xA8 + 4 * k));
            session->set_driver_aids(size_t(i), read_aids(ee, pb + 0x24), grip);
        }
        // (the program's race counts the race information's +0xBC down before each step and holds while it is not 0; the
        // session holds while its count is not 0 and counts it down after)
        const u32 countdown = ee.r32(o.info + 0xBC);
        if (countdown > 1) session->set_countdown(countdown - 1);
        if (options.own_start < 0) load_ours();
        else {
            // Our own set-up: the countdown's held steps with the keys of state 0 (the computer cars with no input), then every
            // placed field against state 0's.
            std::vector<race::CarInput> in(size_t(o.cars));
            if (player >= 0) in[size_t(player)].pad = &pad;
            for (int i = 0; i < o.cars; ++i)
                if (i != player) in[size_t(i)].computer = true;
            std::copy(schedule.front().second.begin(), schedule.front().second.end(), pad.bytes.begin());
            for (int k = 0; k < options.own_start; ++k) session->hold_once(in);
            // GT4R_STATES_LOAD=<prefix,...>: those placed fields then taken from state 0 (to find which of them matter).
            if (const char* e = std::getenv("GT4R_STATES_LOAD")) {
                std::vector<std::string> want;
                for (std::string rest = e; !rest.empty();) {
                    const size_t comma = rest.find(',');
                    want.push_back(rest.substr(0, comma));
                    rest = comma == std::string::npos ? "" : rest.substr(comma + 1);
                }
                for (int i = 0; i < o.cars; ++i) {
                    sim::Record mine;
                    mine.car = session->record(size_t(i)), mine.x = session->gt4_record(size_t(i)), mine.layout = session->wheel_layout(size_t(i));
                    sim::Record theirs = mine;
                    load(theirs, read_image(ee, o.car(i)));
                    for (const Slot& sl : slots())
                        for (const std::string& w : want)
                            if (sl.name.compare(0, w.size(), w) == 0)
                                std::memcpy(reinterpret_cast<u8*>(&mine) + sl.member, reinterpret_cast<const u8*>(&theirs) + sl.member, sl.size);
                    session->load_state(size_t(i), mine.car, mine.x);
                }
            }
            if (print)
                for (int i = 0; i < o.cars; ++i) {
                    sim::Record theirs;
                    theirs.car = session->record(size_t(i)), theirs.x = session->gt4_record(size_t(i));
                    load(theirs, read_image(ee, o.car(i)));
                    sim::Record mine;
                    mine.car = session->record(size_t(i)), mine.x = session->gt4_record(size_t(i));
                    const std::vector<const Slot*> d = differing(theirs, mine);
                    std::printf("  our set-up after %d held steps against state 0, car %d %s: %zu fields differ\n", options.own_start, i, labels[size_t(i)].c_str(),
                                d.size());
                    for (const Slot* sl : d)
                        if (!skipped(sl->name))
                            std::printf("      %s\n", describe(*sl, reinterpret_cast<const u8*>(&theirs) + sl->member, reinterpret_cast<const u8*>(&mine) + sl->member,
                                                             "state 0", "ours").c_str());
                }
        }
    }

    // GT4R_STATES_DRIVERS=<event label>: ours on from state 0 with our own drivers (their state taken from the program's at
    // state 0), the program's run beside it: GT4R_STATES_FIELD prints both.
    std::unique_ptr<sim::ai::Field> field;
    if (const char* ev = std::getenv("GT4R_STATES_DRIVERS"); ev && session) {
        try {
            const sim::Program program = sim::Program::load(disc);
            u8 rules_block[0x80];
            ee.read_bytes(o.cond + 0xF818, rules_block, sizeof rules_block);
            session->set_wear_rules(rules_block, ee.r8(o.cond + 4 + 0x69) != 0, program);
            std::array<gt3r::s8, 4> grip{};
            for (size_t k = 0; k < 4; ++k) grip[k] = gt3r::s8(rules_block[0x18 + k]);
            session->set_class_grip(grip);
            load_ours();
            db::SpecDb edb = sim::ai::open_specdb(disc);
            const db::RaceEvents events(edb, [&disc](const std::string& path) -> std::optional<Bytes> {
                const gt3r::Volume::Entry* en = disc.volume().find(path);
                return en ? std::optional<Bytes>(disc.volume().read(*en)) : std::nullopt;
            });
            const std::optional<db::RaceEvent> e = events.event(ev);
            sim::ai::Pace pace = e ? sim::ai::pace_for(*e) : sim::ai::Pace{};
            std::vector<size_t> all;
            for (int i = 0; i < o.cars; ++i) all.push_back(size_t(i));
            field = sim::ai::Field::open(disc, res.course, *session, all, pace);
            const float lap = session->ground().road().length;
            for (int i = 0; i < o.cars; ++i) {
                const u8 control = ee.r8(o.car(i) + kF + 0x466);
                if (control == 1) field->set_bspec(size_t(i)), field->command(size_t(i), {int(ee.r8(o.car(i) + 0x81)), int(ee.r8(o.car(i) + 0x82))});
                else if (control == 0) field->set_shadow(size_t(i));
                const sim::ai::Field::Probe p = driver_probe(ee, o, i, lap);
                field->probe_set(size_t(i), p);
                const sim::ai::Params& q = field->params(size_t(i));
                session->set_driver_aids(size_t(i), q.aids, q.grip);
            }
        } catch (const std::exception& e) {
            std::printf("  our drivers: %s\n", e.what());
            field.reset();
        }
    }

    // Per car and field, ours against the program's run: the first step it differs, how often, the worst.
    struct Track {
        int first = -1;
        u64 off = 0;
        double worst = 0;
        std::string first_text;
    };
    std::vector<std::map<std::string, Track>> tracks(size_t(o.cars));

    // GT4R_STATES_TAPS=1: every routine of ours tapped in at each of the program's calls on the state's race (step.cpp's
    // taps), the report at the end.
    std::unique_ptr<Taps> taps;
    std::unique_ptr<sim::Program> tap_program;
    std::unique_ptr<ex::RaceInputs> tap_inputs;
    if (std::getenv("GT4R_STATES_TAPS")) {
        tap_program = std::make_unique<sim::Program>(sim::Program::load(disc));
        set_step_program(tap_program.get());
        tap_inputs = std::make_unique<ex::RaceInputs>(ex::race_inputs(sim::core_program(*tap_program), ex::Conductor::SinglePlayer, ex::RaceConfig{}, false, 0));
        taps = std::make_unique<Taps>(ee, o, *tap_inputs);
        add_step_taps(*taps);
        add_taps_aids(*taps);
        add_taps_steer(*taps);
        add_taps_input(*taps);
        add_taps_start(*taps);
    }

    // The driving line's heading the program's active steering read for each car this step (0x4649F0's answer, at its return
    // into the steering 0x348850), given to ours (Session::set_line_heading) for the same step.
    constexpr u32 kAfterLineHeading = 0x3491AC;
    std::map<u32, F> line_heading;
    ee.add_probe(kAfterLineHeading, [&line_heading](Ee& e) { line_heading[e.g32(17) - kF] = F::raw(e.fpr[0]); });
    race::PadRecord idle_pad; // (a car its pad drives that is not the player's: a ghost; its frame read gives nothing)

    const int last = res.frames + std::max(0, options.window);
    size_t best = SIZE_MAX;
    std::vector<Image> at_frames(size_t(o.cars));
    try {
        for (int step = 1; step <= last; ++step) {
            if (session && options.resync && step <= res.frames) load_ours();
            // The keys held at this step.
            {
                int at = 0;
                frame = schedule.back().second;
                for (const auto& [steps, f] : schedule) {
                    if (steps >= 0 && step > at + steps) {
                        at += steps;
                        continue;
                    }
                    frame = f;
                    break;
                }
                std::copy(frame.begin(), frame.end(), pad.bytes.begin());
            }
            // The race's countdown (the race information's +0xBC): the race's frame counts it down before the step (the
            // states reproduce so, and not with it counted after), and the step holds the cars while it is not 0.
            if (const u32 n = ee.r32(o.info + 0xBC)) ee.w32(o.info + 0xBC, n - 1);
            line_heading.clear();
            ee.call(kRaceStep, {Arg::ptr(o.cond), Arg::integer(0)}, 2'000'000'000);
            // GT4R_STATES_FIELD=<every>: every car's distance, speed, line, mode and target speeds in the program's run.
            if (const char* fe = std::getenv("GT4R_STATES_FIELD"); fe && print && step % std::max(1, std::atoi(fe)) == 0) {
                std::printf("%5d:", step);
                for (int i = 0; i < o.cars; ++i) {
                    const u32 D = o.car(i) + sim::layout::kDriver, F = o.car(i) + kF;
                    std::printf("  %7.1f %4.1f l%u m%u %4.1f/%4.1f t%.2f b%.2f", double(ee.rf(F + 0x4A4)), double(ee.rf(F + 0x53C)), ee.r8(D + 0x1C), ee.r8(D + 0x1E), double(ee.rf(D + 0x24)),
                                double(ee.rf(D + 0x28)), double(ee.rf(D + 0x34)), double(ee.rf(D + 0x38)));
                    if (i == 0) std::printf(" f%d c%u", int(gt3r::s8(ee.r8(D + 0x51))), ee.r8(D + 0x6A));
                }
                std::printf("\n");
            }
            if (field) {
                std::vector<race::CarInput> in(size_t(o.cars));
                for (int i = 0; i < o.cars; ++i) // (the pits' marks as the program's race has them)
                    field->set_marks(size_t(i), (ee.r8(o.car(i) + kF + 0x4B1) & 0x10) != 0, ee.r8(o.car(i) + kF + 0x5C2) != 0);
                field->step(*session, in, -1, ee.r32(o.info + 0xBC)); // (the race's start hold left)
                if (const char* fe = std::getenv("GT4R_STATES_FIELD"); fe && print && step % std::max(1, std::atoi(fe)) == 0) {
                    std::printf("ours %5d:", step);
                    for (int i = 0; i < o.cars; ++i) {
                        const sim::ai::Field::Probe p = field->probe_get(size_t(i));
                        std::printf("  %7.1f %4.1f l%d m%d %4.1f/%4.1f t%.2f b%.2f", double(session->cars()[size_t(i)].course_v), double(session->cars()[size_t(i)].speed), p.line, p.mode,
                                    double(p.v_cur.host()), double(p.v_next.host()), double(p.throttle.host()), double(p.brake.host()));
                    }
                    std::printf("\n");
                }
            } else if (session && step <= res.frames) {
                std::vector<race::CarInput> in(size_t(o.cars));
                for (int i = 0; i < o.cars; ++i) {
                    race::CarInput& c = in[size_t(i)];
                    if (const auto h = line_heading.find(o.car(i)); h != line_heading.end()) session->set_line_heading(size_t(i), h->second);
                    if (i == player) {
                        c.pad = &pad;
                        continue;
                    }
                    if (ee.r8(o.car(i) + kF + 0x466) == 0) { // (control source 0: a pad's)
                        c.pad = &idle_pad;
                        continue;
                    }
                    // A computer driver's outputs of this step (0x464F38 into its driver object at car + 0x14), the pedal
                    // slewed by the step in its modes 1 and 2 (as whole_step.cpp).
                    const u32 D = o.car(i) + sim::layout::kDriver;
                    c.computer = true;
                    c.steer_angle = F::raw(ee.r32(D + 0x30)).host();
                    F thr = F::raw(ee.r32(D + 0x34));
                    const u8 mode = ee.r8(D + 0x1E);
                    if (ee.r8(0x623E50) == 0 && (mode == 1 || mode == 2)) {
                        const ex::Car& car = session->record(size_t(i));
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
                }
                session->step_once(in);
                for (int i = 0; i < o.cars; ++i) {
                    const Image theirs_im = read_image(ee, o.car(i));
                    sim::Record theirs;
                    theirs.car = session->record(size_t(i)), theirs.x = session->gt4_record(size_t(i));
                    load(theirs, theirs_im);
                    sim::Record mine;
                    mine.car = session->record(size_t(i)), mine.x = session->gt4_record(size_t(i));
                    StatesCar& sc = res.cars[size_t(i)];
                    // GT4R_STATES_TRACE=<car>[,<every>]: the car's speed, engine, gear and clutch, the program's / ours.
                    if (const char* tr = std::getenv("GT4R_STATES_TRACE"); tr && print && std::atoi(tr) == i) {
                        const char* comma = std::strchr(tr, ',');
                        const int every = comma ? std::max(1, std::atoi(comma + 1)) : 1;
                        if (step % every == 0 || step == 1) {
                            const ex::Car& p = theirs.car;
                            const ex::Car& q = mine.car;
                            std::printf("    trace %4d  speed %.5f / %.5f  rpm %d / %d  gear %d / %d  clutch %d / %d  clutch torque %.3f / %.3f  throttle %.4f / %.4f  "
                                        "slip0 %.5f / %.5f\n",
                                        step, p.speed.host(), q.speed.host(), int(p.rpm), int(q.rpm), int(p.gear), int(q.gear), int(p.clutch), int(q.clutch),
                                        p.clutch_torque.host(), q.clutch_torque.host(), p.throttle.host(), q.throttle.host(), p.w[0].slip_ratio.host(),
                                        q.w[0].slip_ratio.host());
                        }
                    }
                    std::vector<std::string> now;
                    for (const Slot* s : differing(theirs, mine)) {
                        if (skipped(s->name)) continue;
                        const u8* a = reinterpret_cast<const u8*>(&theirs) + s->member;
                        const u8* b = reinterpret_cast<const u8*>(&mine) + s->member;
                        Track& t = tracks[size_t(i)][s->name];
                        ++t.off;
                        const double x = value_of(*s, a), y = value_of(*s, b), d = std::fabs(x - y);
                        const double rel = s->real ? d / std::max(std::fabs(x), 1e-6) : d;
                        t.worst = std::max(t.worst, rel);
                        // (GT4R_STATES_BIG=<relative>: a field counts from the first step it differs by more than that)
                        static const double big = std::getenv("GT4R_STATES_BIG") ? std::atof(std::getenv("GT4R_STATES_BIG")) : 0;
                        if (rel <= big) {
                            --t.off;
                            continue;
                        }
                        if (t.first < 0) t.first = step, t.first_text = describe(*s, a, b, "program", "ours");
                        if (sc.first_step < 0) now.push_back(t.first_text);
                    }
                    if (sc.first_step < 0 && !now.empty()) sc.first_step = step, sc.first_fields = now;
                    if (step == res.frames) {
                        const ex::Car& mc = session->record(size_t(i));
                        const double dx = double(mc.pos.x.host()) - F::raw(ee.r32(o.car(i) + kF)).host();
                        const double dy = double(mc.pos.y.host()) - F::raw(ee.r32(o.car(i) + kF + 4)).host();
                        const double dz = double(mc.pos.z.host()) - F::raw(ee.r32(o.car(i) + kF + 8)).host();
                        sc.end_position_error = std::sqrt(dx * dx + dy * dy + dz * dz);
                        sc.end_speed_program = speed_of(theirs_im.car.data());
                        sc.end_speed_ours = std::sqrt(double(mc.vel.x.host()) * mc.vel.x.host() + double(mc.vel.y.host()) * mc.vel.y.host() +
                                                      double(mc.vel.z.host()) * mc.vel.z.host());
                    }
                }
            }
            // The program's run against state 1 at the frame count and around it.
            if (step >= res.frames - options.window) {
                size_t off = 0;
                for (int i = 0; i < o.cars; ++i) off += bytes_differing(read_image(ee, o.car(i)), image_of(s1.ee, o.car(i)));
                if (off < best) best = off, res.best_frames = step, res.best_bytes_off = off;
            }
            if (step == res.frames)
                for (int i = 0; i < o.cars; ++i) at_frames[size_t(i)] = read_image(ee, o.car(i));
        }
    } catch (const std::exception& e) {
        char b[64];
        std::snprintf(b, sizeof b, " (pc %08X, ra %08X)", ee.cur_pc, ee.g32(31));
        res.error = std::string("the program's step stopped: ") + e.what() + b;
        return res;
    }
    if (res.frames == 0)
        for (int i = 0; i < o.cars; ++i) at_frames[size_t(i)] = read_image(ee, o.car(i));
    for (int i = 0; i < o.cars; ++i) {
        StatesCar& sc = res.cars[size_t(i)];
        const Image& ran = at_frames[size_t(i)];
        const Image want = image_of(s1.ee, o.car(i));
        const sim::Record a = record_of(ran), b = record_of(want);
        sc.fields = slots().size();
        const std::vector<const Slot*> d = differing(b, a);
        sc.fields_equal = sc.fields - d.size();
        if (!d.empty())
            sc.first_field = describe(*d.front(), reinterpret_cast<const u8*>(&b) + d.front()->member, reinterpret_cast<const u8*>(&a) + d.front()->member,
                                      "state 1", "run");
        sc.bytes_off = bytes_differing(ran, want);
        // GT4R_STATES_LIST=<car>: every placed field of the car that differs from state 1, and the record's other words.
        if (const char* l = std::getenv("GT4R_STATES_LIST"); l && print && std::atoi(l) == i) {
            for (const Slot* s : d) {
                const double x = value_of(*s, reinterpret_cast<const u8*>(&b) + s->member), y = value_of(*s, reinterpret_cast<const u8*>(&a) + s->member);
                std::printf("      %-28s state 1 %.9g, run %.9g (%.3g)\n", s->name.c_str(), x, y, s->real ? std::fabs(x - y) / std::max(std::fabs(x), 1e-9) : std::fabs(x - y));
            }
            std::vector<u8> placed(ran.car.size() + ran.params.size(), 0);
            for (const Slot& s : slots())
                for (u32 k = 0; k < s.size; ++k) placed[(s.place.block == sim::layout::Block::Car ? 0 : ran.car.size()) + s.place.offset + k] = 1;
            for (u32 w = 0; w + 4 <= ran.car.size(); w += 4)
                if (std::memcmp(&ran.car[w], &want.car[w], 4) && !placed[w])
                    std::printf("      car +%04X (unplaced) state 1 %08X, run %08X\n", w, rd32(Bytes(want.car.begin() + w, want.car.begin() + w + 4), 0),
                                rd32(Bytes(ran.car.begin() + w, ran.car.begin() + w + 4), 0));
            for (u32 w = 0; w + 4 <= ran.params.size(); w += 4)
                if (std::memcmp(&ran.params[w], &want.params[w], 4) && !placed[ran.car.size() + w])
                    std::printf("      P +%04X (unplaced) state 1 %08X, run %08X\n", w, rd32(Bytes(want.params.begin() + w, want.params.begin() + w + 4), 0),
                                rd32(Bytes(ran.params.begin() + w, ran.params.begin() + w + 4), 0));
        }
        sc.position_error = dist3(ran.car.data() + kF, want.car.data() + kF);
        sc.speed_error = std::fabs(speed_of(ran.car.data()) - speed_of(want.car.data()));
    }
    res.ran = true;
    if (taps && print) taps->print();

    if (print) {
        std::printf("states: %s, %d cars, %d frames (the boot counters' difference), player car %d, pad frame", res.course.c_str(), o.cars, res.frames, player);
        for (u8 v : schedule.front().second) std::printf(" %02X", v);
        std::printf("%s\n", res.frame_matches ? "" : " (not the frame the state's controller last read)");
        std::printf("  the program's run against state 1: nearest after %d steps (%zu record bytes differ)\n", res.best_frames, res.best_bytes_off);
        for (const StatesCar& c : res.cars)
            std::printf("    car %-28s %s %zu/%zu fields equal, %zu bytes differ, position %.6f m, speed %.6f m/s%s%s\n", c.label.c_str(),
                        c.player ? "(player)" : "        ", c.fields_equal, c.fields, c.bytes_off, c.position_error, c.speed_error,
                        c.first_field.empty() ? "" : "; first: ", c.first_field.c_str());
        if (session) {
            std::printf("  ours from state 0 against the program's run%s:\n", options.resync ? " (each step from the program's state)" : "");
            for (size_t i = 0; i < res.cars.size(); ++i) {
                const StatesCar& c = res.cars[i];
                std::printf("    car %-28s first differs at step %d; at step %d: position %.4f m apart, speed ours %.4f, the program's %.4f m/s\n",
                            c.label.c_str(), c.first_step, res.frames, c.end_position_error, c.end_speed_ours, c.end_speed_program);
                for (const std::string& f : c.first_fields) std::printf("        %s\n", f.c_str());
                // Every field that differed, by the step it first did.
                std::vector<std::pair<std::string, Track>> v;
                for (const auto& kv : tracks[i])
                    if (kv.second.first >= 0) v.push_back(kv);
                std::sort(v.begin(), v.end(), [](const auto& x, const auto& y) { return x.second.first != y.second.first ? x.second.first < y.second.first : x.first < y.first; });
                const size_t show = std::getenv("GT4R_STATES_ALL") ? v.size() : std::min<size_t>(v.size(), 40);
                for (size_t k = 0; k < show; ++k)
                    std::printf("      from step %4d  %4llu steps, worst %.3g: %s\n", v[k].second.first, static_cast<unsigned long long>(v[k].second.off),
                                v[k].second.worst, v[k].second.first_text.c_str());
                if (show < v.size()) std::printf("      (%zu more fields; GT4R_STATES_ALL=1 lists them)\n", v.size() - show);
            }
        }
    }
    return res;
}

} // namespace gt4r::diff
