// Prologue's record in Gran Turismo 3's layout and the taps of its race step (prologue_taps.h).

#include "prologue_taps.h"

#include "gt4r/sim/aids_prologue.h"
#include "gt4r/sim/feel_prologue.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/sim/exact_bridge.h"
#include "sim/exact/course_ground.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_chassis.h"
#include "gt4r/sim/rules_prologue.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/tick.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <type_traits>

namespace gt4r::diff::prologue {

Disc* g_disc = nullptr; // (the disc the taps run on: set_disc)
void set_disc(Disc& d) { g_disc = &d; }

namespace {

// The engine's record: the parameter block P at +0x10 .. +0xEC0; its tire records at P +0x398 + 0x1B4 r, its tire index
// from P +0xDD0, the wear's load terms from P +0xE6C. Prologue's state from the engine's +0x1328 (F +0x3A4, the terrain factor) is 4 bytes on.
constexpr u32 kP = 0x10, kPEnd = 0xEC0, kInsert = 0x1328;

u32 params_offset(u32 q) {
    if (q < 0x398) return q;
    if (q < 0xDD0) return 0x398 + 0x1B8 * ((q - 0x398) / 0x1B4) + (q - 0x398) % 0x1B4;
    return q + (q >= 0xE6C ? 44 : 24);
}

// Each field of the mirror: its name, its offset in ex::Car and its size, in the field table's order.
struct Field {
    std::string name;
    u32 engine;
    size_t member, size;
    bool real;
};
const std::vector<Field>& fields() {
    static const std::vector<Field> table = [] {
        std::vector<Field> v;
        static const ex::Car probe{};
        const u8* base = reinterpret_cast<const u8*>(&probe);
        ex::fields(probe, [&](const char* n, int i, u32 off, const auto& m) {
            using T = std::remove_cvref_t<decltype(m)>;
            const bool real = std::is_same_v<T, gt3r::ee::F>;
            v.push_back({i < 0 ? std::string(n) : std::string(n) + "[" + std::to_string(i) + "]", off,
                         size_t(reinterpret_cast<const u8*>(&m) - base), real ? 4 : sizeof(T), real});
        });
        return v;
    }();
    return table;
}

} // namespace

Where place(u32 engine) {
    if (engine < kP) return {false, engine};
    if (engine < kPEnd) return {true, params_offset(engine - kP)};
    return {false, engine - (engine < kInsert ? 0xEACu : 0xEA8u)};
}

std::vector<u8> gt3_image(Ee& ee, u32 car) {
    std::vector<u8> im(ex::kCarSize, 0), c(kCarSize), p(kParamsSize);
    ee.read_bytes(car, c.data(), kCarSize);
    ee.read_bytes(ee.r32(car + 0x10), p.data(), kParamsSize);
    for (u32 o = 0; o < ex::kCarSize; ++o) {
        const Where w = place(o);
        im[o] = w.params ? (w.offset < p.size() ? p[w.offset] : 0) : (w.offset < c.size() ? c[w.offset] : 0);
    }
    return im;
}

void compare(const ex::Car& after, const ex::Car& mine, PTapStat& st) {
    const u8* a = reinterpret_cast<const u8*>(&after);
    const u8* m = reinterpret_cast<const u8*>(&mine);
    ++st.calls;
    bool clean = true;
    for (const Field& f : fields()) {
        if (std::memcmp(a + f.member, m + f.member, f.size) == 0) continue;
        clean = false;
        double x = 0, y = 0;
        if (f.real) {
            gt3r::ee::F u, v;
            std::memcpy(&u, a + f.member, 4), std::memcpy(&v, m + f.member, 4);
            x = double(u.host()), y = double(v.host());
        } else {
            u32 u = 0, v = 0;
            std::memcpy(&u, a + f.member, f.size), std::memcpy(&v, m + f.member, f.size);
            x = double(u), y = double(v);
        }
        PFieldStat& fs = st.fields[f.name];
        if (fs.off == 0) {
            char text[160];
            std::snprintf(text, sizeof text, "first at step %llu: the program's %.9g, ours %.9g", (unsigned long long)st.calls, x, y);
            fs.example = text;
        }
        ++fs.off;
        const double d = std::fabs(x - y), rel = f.real ? d / std::max(std::fabs(x), 1e-6) : d;
        if (rel >= fs.worst) fs.worst = rel;
    }
    if (clean) ++st.clean;
}

void PTaps::add(PTap tap) {
    const size_t i = taps_.size();
    stats_.emplace_back();
    stats_.back().name = tap.name;
    ee_.add_probe(tap.address, [this, i](Ee&) { enter(i); });
    taps_.push_back(std::move(tap));
}

void PTaps::enter(size_t tap) {
    const u32 car = u32(ee_.a(0));
    int index = -1;
    for (int i = 0; i < race_.cars; ++i)
        if (race_.car(i) == car) index = i;
    if (index < 0) return;
    Pending p{tap, ee_.g32(29), car, index, ex::Car{}, ex::Car{}};
    const std::vector<u8> im = gt3_image(ee_, car);
    p.before.behaviour = sim::rules::behaviour();
    ex::load_image(p.before, im.data());
    p.mine = p.before;
    PTapCall call{ee_, car, index, p.mine, inputs_};
    taps_[tap].ours(call);
    const u32 ra = ee_.g32(31);
    const bool fresh = pending_.find(ra) == pending_.end();
    pending_[ra].push_back(std::move(p));
    if (fresh) ee_.add_probe(ra, [this, ra](Ee&) { leave(ra); });
}

void PTaps::leave(u32 ra) {
    auto it = pending_.find(ra);
    if (it == pending_.end() || it->second.empty() || it->second.back().sp != ee_.g32(29)) return;
    Pending p = std::move(it->second.back());
    it->second.pop_back();
    PTapStat& st = stats_[p.tap];
    ++st.calls;
    ex::Car after;
    after.behaviour = p.before.behaviour;
    const std::vector<u8> im = gt3_image(ee_, p.car);
    ex::load_image(after, im.data());
    if (taps_[p.tap].at_exit) { // (ours on the record as the program leaves it: only what ours writes is compared)
        p.mine = after;
        PTapCall call{ee_, p.car, p.index, p.mine, inputs_};
        taps_[p.tap].at_exit(call);
    }
    const u8* a = reinterpret_cast<const u8*>(&after);
    const u8* b = reinterpret_cast<const u8*>(&p.before);
    const u8* m = reinterpret_cast<const u8*>(&p.mine);
    bool clean = true;
    for (const Field& f : fields()) {
        if (std::memcmp(a + f.member, b + f.member, f.size) != 0) ++st.written[f.name];
        if (std::memcmp(a + f.member, m + f.member, f.size) == 0) continue;
        bool skipped = false;
        for (const std::string& k : taps_[p.tap].skip) skipped = skipped || f.name.compare(0, k.size(), k) == 0;
        if (skipped) continue;
        clean = false;
        double x = 0, y = 0;
        if (f.real) {
            gt3r::ee::F u, v;
            std::memcpy(&u, a + f.member, 4), std::memcpy(&v, m + f.member, 4);
            x = double(u.host()), y = double(v.host());
        } else {
            u32 u = 0, v = 0;
            std::memcpy(&u, a + f.member, f.size), std::memcpy(&v, m + f.member, f.size);
            x = double(u), y = double(v);
        }
        PFieldStat& fs = st.fields[f.name];
        ++fs.off;
        const double d = std::fabs(x - y), rel = f.real ? d / std::max(std::fabs(x), 1e-6) : d;
        if (rel >= fs.worst) {
            fs.worst = rel;
            char text[160];
            std::snprintf(text, sizeof text, "the program's %.9g, ours %.9g (car %d, call %llu)", x, y, p.index, static_cast<unsigned long long>(st.calls));
            fs.example = text;
        }
    }
    if (taps_[p.tap].after)
        taps_[p.tap].after(ee_, p.car, p.index, [&](const char* name, u32 program, u32 ours) {
            if (program == ours) return;
            clean = false;
            PFieldStat& fs = st.fields[name];
            ++fs.off;
            const double x = double(gt3r::ee::F::raw(program).host()), y = double(gt3r::ee::F::raw(ours).host());
            const double rel = std::fabs(x - y) / std::max(std::fabs(x), 1e-6);
            if (rel >= fs.worst) {
                fs.worst = rel;
                char text[160];
                std::snprintf(text, sizeof text, "the program's %.9g, ours %.9g (car %d, call %llu)", x, y, p.index, static_cast<unsigned long long>(st.calls));
                fs.example = text;
            }
        });
    st.clean += clean;
}

void PTaps::print() const {
    for (const PTapStat& st : stats_) {
        std::printf("  %-28s %llu calls, %llu with every field equal\n", st.name.c_str(), static_cast<unsigned long long>(st.calls),
                    static_cast<unsigned long long>(st.clean));
        size_t n = 0;
        for (const auto& [name, f] : st.fields)
            if (n++ < 24)
                std::printf("      %-22s differs in %llu, most by %.3g: %s\n", name.c_str(), static_cast<unsigned long long>(f.off), f.worst, f.example.c_str());
        if (st.fields.size() > 24) std::printf("      ... %zu fields differ\n", st.fields.size());
        if (!st.written.empty() && std::getenv("GT4R_WRITES")) {
            std::printf("      writes:");
            for (const auto& [name, k] : st.written) std::printf(" %s", name.c_str());
            std::printf("\n");
        }
    }
}

void add_engine_taps(PTaps& taps) {
    // Each the engine's routine of the role of Prologue's at that address (matched through Gran Turismo 4's, whose
    // counterparts tools/gt4diff/step.cpp taps).
    taps.add({0x3B2480, "save previous", [](PTapCall& t) { ex::save_previous(t.car); }});
    taps.add({0x3BA450, "slip ratios", [](PTapCall& t) { ex::slip_ratios(t.car, t.inputs.statics); }});
    taps.add({0x3BA538, "tire curves", [](PTapCall& t) { ex::tyre_curves(t.car); }});
    taps.add({0x3BA5E8, "tire long force", [](PTapCall& t) { ex::tyre_long_force(t.car, t.inputs.statics); }});
    taps.add({0x3BB138, "tire slip force", [](PTapCall& t) { ex::tyre_slip_force(t.car); }});
    taps.add({0x3BAC98, "forward step", [](PTapCall& t) { ex::forward_step(t.car); }});
    taps.add({0x3BB5D0, "lateral and yaw", [](PTapCall& t) { ex::lateral_yaw(t.car, t.ee.a(1) != 0); }});
    taps.add({0x3BB9F0, "stop hold", [](PTapCall& t) { ex::stop_hold(t.car, t.ee.a(1) != 0); }});
    taps.add({0x3BBF18, "rolling resistance", [](PTapCall& t) { ex::rolling_resistance(t.car, t.inputs.statics); }});
    taps.add({0x3BF598, "slip angles", [](PTapCall& t) { ex::slip_angles(t.car); }});
    taps.add({0x3BF338, "peak force", [](PTapCall& t) { ex::peak_force(t.car, t.inputs.prep.wear); }});
    taps.add({0x3B9E10, "brakes and clutch", [](PTapCall& t) { ex::brakes_and_clutch(t.car); }});
    taps.add({0x3CE4D0, "wheel torques", [](PTapCall& t) { ex::wheel_net_torque(t.car); }});
    taps.add({0x3CEAE0, "axle step", [](PTapCall& t) { ex::axle_step(t.car, t.inputs.statics.awd3, t.inputs.statics.stepless, t.inputs.statics.hybrid); }});
    taps.add({0x3CF740, "wheel speeds", [](PTapCall& t) { ex::wheel_speeds(t.car); }});
    taps.add({0x3BA6C0, "engine speed", [](PTapCall& t) { ex::engine_speed(t.car, t.inputs.statics.hybrid); }});
    taps.add({0x3BAAD8, "gear", [](PTapCall& t) { ex::gear_step(t.car, t.inputs.gears); }});
    taps.add({0x3B3810, "brake heat, Prologue's constants", [](PTapCall& t) {
                  static const sim::prologue::Tables k = sim::prologue::tables(sim::Program::load(*g_disc));
                  ex::RaceInputs in = t.inputs;
                  sim::prologue::race_inputs(in, k);
                  ex::brake_heat(t.car, in.brake_heat);
              }});
    taps.add({0x3B5A20, "wheel steer", [](PTapCall& t) { ex::wheel_steer(t.car); }});
    taps.add({0x375F58, "suspension", [](PTapCall& t) { ex::suspension(t.car, nullptr); }});
    taps.add({0x3765F8, "attitude", [](PTapCall& t) { ex::attitude(t.car, gt3r::ee::F()); }});
    taps.add({0x376EC8, "bump nudge", [](PTapCall& t) { ex::bump_nudge(t.car); }});
    taps.add({0x3DBB70, "aero", [](PTapCall& t) { ex::aero(t.car, t.inputs.prep.slip); }});
    taps.add({0x3C4D80, "ground frame", [](PTapCall& t) { ex::ground_frame(t.car); }});
    taps.add({0x3E0838, "aids", [](PTapCall& t) { ex::aids(t.car, t.inputs.aids); }});
    // The rest of phase 19 (0x3B29D0) and phases 20 and 21.
    taps.add({0x3B44B0, "hub offsets xy", [](PTapCall& t) { ex::hub_offsets_xy(t.car); }});
    taps.add({0x3B4630, "speed ramp", [](PTapCall& t) { ex::speed_ramp(t.car); }});
    taps.add({0x375D00, "terrain factor", [](PTapCall& t) { ex::terrain_factor(t.car); }});
    taps.add({0x3CE1A8, "pad rumble", [](PTapCall& t) { ex::pad_rumble(t.car); }});
    taps.add({0x3BC7F0, "steer force", [](PTapCall& t) { ex::steer_force(t.car, t.inputs.feel); }});
    taps.add({0x3CB1E0, "tire effects", [](PTapCall& t) { ex::tyre_effects(t.car, t.inputs.effects, t.inputs.aids.course_category); }});
    taps.add({0x3B9020, "engine sound", [](PTapCall& t) { ex::engine_sound(t.car, t.inputs.sound_scales); }});
    taps.add({0x3CA7B0, "effects reset", [](PTapCall& t) { ex::effects_reset(t.car, 0, 0); }});
    taps.add({0x3B4238, "view follow", [](PTapCall& t) { ex::view_follow(t.car); }});
    taps.add({0x3B4600, "hub offsets", [](PTapCall& t) { ex::hub_offsets(t.car); }});
    taps.add({0x3C5740, "speed bookkeeping", [](PTapCall& t) { ex::speed_bookkeeping(t.car, t.inputs.prep.condition); }});
    taps.add({0x3B25F0, "wheel spin", [](PTapCall& t) { ex::wheel_spin(t.car); }});
    taps.add({0x3BBBB8, "tire wear", [](PTapCall& t) { ex::tire_wear(t.car, t.inputs.prep.wear); }});
    taps.add({0x3BBD80, "wear flags", [](PTapCall& t) { ex::wear_flags(t.car, t.inputs.prep.wear, {}); }});
}

namespace {

// The tire records' kind bytes as the program's parameter block has them.
sim::prologue::TireKinds kinds_of(Ee& ee, u32 car) {
    sim::prologue::TireKinds k{};
    const u32 P = ee.r32(car + 0x10);
    for (u32 r = 0; r < 6; ++r)
        for (u32 j = 0; j < 3; ++j) k[r][j] = ee.r8(P + 0x398 + 0x1B8 * r + 0x1B4 + j);
    return k;
}

} // namespace

void add_prologue_taps(PTaps& taps, const Disc& disc, const std::string& course) {
    static sim::prologue::Tables t;
    t = sim::prologue::tables(sim::Program::load(const_cast<Disc&>(disc)));
    auto layout = [](PTapCall& c) { return sim::prologue::wheel_layout(t, c.ee.r8(c.ee.r32(c.car_addr + 0x10) + 0x30)); };
    // The record's Gran Turismo 4 part as Prologue keeps it at the call: the tire records with their kinds, each wheel's
    // steering offset (the drive block's +0x1C), its camber force (car +0x4B0 + 4 w), its tire's x1da, the mean slip angle
    // (the engine's F +0x420).
    auto extra = [=](PTapCall& c) {
        sim::rules::TireExtra x = sim::prologue::tire_extra(c.car.p, kinds_of(c.ee, c.car_addr), t);
        const sim::rules::WheelLayout l = layout(c);
        for (size_t w = 0; w < 4; ++w) {
            x.steer_offset[w] = c.car.t[w].steer_cos;
            x.camber_force[w] = gt3r::ee::F::raw(c.ee.r32(c.car_addr + 0x4B0 + 4 * u32(w)));
            x.wheel_xdd[w] = sim::rules::tire_record(x, l, int(w), c.car.w[w].surface).x1da;
        }
        x.mean_slip = c.car.x420;
        return x;
    };
    auto globals = [](PTapCall& c) {
        sim::rules::SlipForceGlobals g = t.globals;
        g.mean_on = c.ee.r8(0x506612);
        return g;
    };
    taps.add({0x3B5A20, "Prologue's wheel steer", [=](PTapCall& c) {
                  sim::prologue::CarPart x;
                  x.tires = extra(c);
                  x.layout = layout(c);
                  x.axle_load = {c.car.p.xe54, c.car.p.xe58};
                  for (size_t w = 0; w < 4; ++w) x.offset_rate[w] = c.car.t[w].steer_sin;
                  sim::prologue::wheel_steer(c.car, x, t);
                  for (size_t w = 0; w < 4; ++w) c.car.t[w].steer_sin = x.offset_rate[w], c.car.t[w].steer_cos = x.tires.steer_offset[w];
              }});
    taps.add({0x3BA450, "Prologue's slip ratios", [=](PTapCall& c) {
                  const sim::rules::TireExtra x = extra(c);
                  sim::prologue::slip_ratios(c.car, x, layout(c), t);
              }});
    taps.add({0x3BB138, "GT4's tire slip force", [=](PTapCall& c) {
                  sim::rules::TireExtra x = extra(c);
                  sim::rules::tire_slip_force(c.car, x, layout(c), globals(c));
                  if (c.ee.r8(0x506612)) c.car.x420 = x.mean_slip;
              }});
    taps.add({0x3BAC98, "GT4's forward step", [=](PTapCall& c) {
                  const sim::rules::TireExtra x = extra(c);
                  sim::rules::ChassisExtra ch;
                  ch.mass = c.car.p.mass;
                  sim::rules::forward_step(c.car, ch, x, layout(c));
              }});
    taps.add({0x3BB5D0, "Prologue's lateral and yaw", [=](PTapCall& c) {
                  const sim::rules::TireExtra x = extra(c);
                  sim::rules::ChassisExtra ch;
                  ch.mass = c.car.p.mass;
                  sim::prologue::CarPart part;
                  part.tires = x, part.layout = layout(c), part.chassis = ch;
                  sim::prologue::lateral_yaw(c.car, part, c.ee.a(1) != 0, t);
              }});
    static sim::prologue::AidTables aid_tables;
    aid_tables = sim::prologue::aid_tables(sim::Program::load(const_cast<Disc&>(disc)));
    taps.add({0x3E0838, "Prologue's aids", [=](PTapCall& c) {
                  const sim::prologue::AidTables& g = aid_tables;
                  sim::prologue::CarPart part;
                  part.tires = extra(c), part.layout = layout(c);
                  // (each wheel's curve A byte, +0x27, as the tire slip force left it)
                  for (u32 w = 0; w < 4; ++w)
                      part.tires.curve_a[w] = gt3r::ee::int_to_f(c.ee.r8(c.car_addr + 0x138 + 0xA4 * w + 0x27)) * gt3r::ee::F::raw(0x3C000000);
                  const u32 rules = c.ee.r32(c.car_addr + 4) + 0x10780;
                  sim::prologue::AidRace r;
                  r.speed_cap = {c.ee.r8(rules + 0x42), c.ee.r8(rules + 0x43)}, r.start_cap = c.ee.r8(rules + 0x4B);
                  sim::prologue::aids(c.car, part, g, r, c.inputs.aids);
              }});
    taps.add({0x3C4D80, "engine's ground frame, GT4's axes", [=](PTapCall& c) {
                  ex::ground_frame(c.car);
                  sim::rules::body_axes(c.car.ground_pitch, c.car.ground_roll, c.car.heading, c.car.ground_fwd, c.car.ground_lat, c.car.ground_up);
              }});
    // The parts of the parameter block the engine's record has no member for, as the program has them.
    auto image = [=](PTapCall& c) {
        sim::prologue::CarPart x;
        x.layout = layout(c);
        std::vector<u8> p(kParamsSize);
        c.ee.read_bytes(c.ee.r32(c.car_addr + 0x10), p.data(), kParamsSize);
        sim::prologue::image_part(x, p.data());
        return x;
    };
    taps.add({0x3754D8, "Prologue's query points and bumps", [](PTapCall&) {}, {"g.h"},
              [=](PTapCall& c) {
                  static bool shown = false;
                  if (!shown && std::getenv("GT4R_SHOW_BUMPS")) {
                      shown = true;
                      for (u32 k = 0; k < 16; ++k)
                          std::printf("    bumps %2u: amp %08X / ours %08X  freq %08X / ours %08X  scaled %d / %d  mode %d / %d\n", k,
                                      c.ee.r32(0x504478 + 4 * k), t.bumps.amp[k].bits, c.ee.r32(0x5044B8 + 4 * k),
                                      t.bumps.freq[k].bits, c.ee.r8(0x5044F8 + k), t.bumps.scaled[k], c.ee.r8(0x504508 + k),
                                      t.bumps.mode[k]);
                  }
                  const sim::prologue::CarPart x = image(c);
                  sim::prologue::query_points(c.car, x);
                  sim::prologue::road_bumps(c.car, x, t.bumps);
              }});
    // Phase 19 whole (0x3B29D0) as the session runs it, the ground query on the course's GTRW road; the effects, feel, sound,
    // view and bookkeeping fields (other routines of Prologue's, not ported) not compared.
    if (!course.empty()) {
        static std::unique_ptr<ex::CourseGround> ground;
        Disc& d = const_cast<Disc&>(disc);
        if (const gt3r::Volume::Entry* e = d.volume().find("crs/" + course)) {
            const Bytes data = d.volume().read(*e);
            const course::CourseFile parsed = course::CourseFile::parse(data);
            if (std::optional<course::Runway> road = course::course_road(parsed, data); road && road->gtrw)
                ground = std::make_unique<ex::CourseGround>(*road->gtrw);
        }
        if (ground)
            taps.add({0x3B29D0, "phase 19 as the session's", [=](PTapCall& c) {
                          ex::Car& k = c.car;
                          const sim::prologue::CarPart x = image(c);
                          ex::hub_offsets_xy(k);
                          ex::speed_ramp(k);
                          sim::prologue::ground_contact(k, x, *ground, t.bumps, true);
                          sim::prologue::terrain(k, x);
                          ex::suspension(k, nullptr);
                          sim::prologue::attitude(k, x.layout, t);
                          sim::prologue::bump_nudge(k, x);
                          ex::hub_offsets(k);
                          sim::rules::body_axes(k);
                          sim::prologue::ground_frame(k);
                          ex::speed_bookkeeping(k, c.inputs.prep.condition);
                          ex::wheel_spin(k);
                      },
                      {"rumble", "steer_force", "ffb", "lag_", "w.skid", "w.slip_intensity", "w.smoke", "x4e", "speedo", "engine_wear", "oil",
                       "odo", "dist_total", "dirt", "max_speed", "speed_abs", "hit_", "feedback", "g.seg", "road_seg", "w.flap"}});
    }
    // Phase 19's bookkeeping (feel_prologue.h): the speedometer at car + 0xC60, outside the mirror.
    {
        static std::map<int, gt3r::ee::F> speedo;
        static const sim::Program program = sim::Program::load(const_cast<Disc&>(disc));
        taps.add({0x3C5740, "Prologue's speeds", [=](PTapCall& c) {
                      sim::prologue::FeelPart x;
                      x.speedo = gt3r::ee::F::raw(c.ee.r32(c.car_addr + 0xC60));
                      const sim::rules::SpeedTables k = sim::prologue::speed_tables(program, c.ee.r8(c.ee.r32(c.car_addr + 0x10) + 0x33));
                      sim::prologue::speeds(c.car, x, layout(c), k, c.inputs.prep.condition);
                      speedo[c.car_index] = x.speedo;
                  },
                  {"speedo"}, {}, [](Ee& ee, u32 car, int index, const std::function<void(const char*, u32, u32)>& field) {
                      field("speedo (car +0xC60)", ee.r32(car + 0xC60), speedo[index].bits);
                  }});
    }
    { // The view's follower (its state in the conductor, cond + 0xDB48 + 0x14 k).
        static std::map<int, sim::rules::ViewState> view;
        static const sim::rules::FeelTables ft = sim::prologue::feel_tables(sim::Program::load(const_cast<Disc&>(disc)));
        auto block = [](Ee& ee, u32 car) { return ee.r32(car + 4) + sim::prologue::kViewBlock + sim::prologue::kViewSize * ee.r8(car + 0xD8 + 0x3EC); };
        taps.add({0x3B4238, "Prologue's view follow", [=](PTapCall& c) {
                      sim::rules::ViewState v;
                      const u32 b = block(c.ee, c.car_addr);
                      v.heading = gt3r::ee::F::raw(c.ee.r32(b)), v.heading_rate = gt3r::ee::F::raw(c.ee.r32(b + 4));
                      v.heading_acc = gt3r::ee::F::raw(c.ee.r32(b + 8)), v.pitch = gt3r::ee::F::raw(c.ee.r32(b + 0xC));
                      v.pitch_rate = gt3r::ee::F::raw(c.ee.r32(b + 0x10));
                      sim::prologue::view_follow(c.car, v, ft);
                      view[c.car_index] = v;
                  },
                  {"lag_heading_acc", "lag_heading_rate", "lag_heading[", "lag_pitch_rate"}, {},
                  [=](Ee& ee, u32 car, int index, const std::function<void(const char*, u32, u32)>& field) {
                      const u32 b = block(ee, car);
                      const sim::rules::ViewState& v = view[index];
                      field("view.heading", ee.r32(b), v.heading.bits), field("view.heading_rate", ee.r32(b + 4), v.heading_rate.bits);
                      field("view.heading_acc", ee.r32(b + 8), v.heading_acc.bits), field("view.pitch", ee.r32(b + 0xC), v.pitch.bits);
                      field("view.pitch_rate", ee.r32(b + 0x10), v.pitch_rate.bits);
                  }});
    }
    taps.add({0x3BBF18, "rolling resistance, Prologue's curve", [=](PTapCall& c) {
                  ex::Statics s = c.inputs.statics;
                  sim::prologue::statics(s, t);
                  ex::rolling_resistance(c.car, s);
              }});
    taps.add({0x375D00, "Prologue's terrain", [=](PTapCall& c) { sim::prologue::terrain(c.car, image(c)); }});
    taps.add({0x376EC8, "Prologue's bump nudge", [=](PTapCall& c) { sim::prologue::bump_nudge(c.car, image(c)); }});
    taps.add({0x3765F8, "Prologue's attitude", [=](PTapCall& c) {
                  sim::prologue::attitude(c.car, layout(c), t, gt3r::ee::F::raw(c.ee.r32(c.car_addr + 0xD8 + 0x3AC)));
              }});
}

namespace {

// The controls' set-up's results (0x3DD068) in the harness against ours: the pedal steps, the steering wheel's settings,
// the follower's table.
void check_controls(Ee& ee, const sim::prologue::Controls& k) {
    int equal = 0, differ = 0;
    auto cmp = [&](const char* what, u32 addr, gt3r::ee::F ours) {
        const u32 theirs = ee.r32(addr);
        if (theirs == ours.bits) ++equal;
        else {
            ++differ;
            std::printf("    controls: %s (%08X) the program's %08X (%g), ours %08X (%g)\n", what, addr, theirs,
                        double(gt3r::ee::F::raw(theirs).host()), ours.bits, double(ours.host()));
        }
    };
    cmp("throttle up", 0x504AA8, k.throttle_up), cmp("throttle down", 0x504AAC, k.throttle_down);
    cmp("brake up", 0x504AB0, k.brake_up), cmp("brake down", 0x504AB4, k.brake_down);
    cmp("third up", 0x504AB8, k.third_up), cmp("third down", 0x504ABC, k.third_down);
    cmp("analog jump", 0x504AA4, k.analog_jump), cmp("wheel slew", 0x504A88, k.wheel_slew);
    for (u32 j = 0; j < 4; ++j) cmp("wheel lock", 0x504A8C + 4 * j, k.wheel_lock[j]);
    for (u32 j = 0; j < 2; ++j) cmp("kind 6 scale", 0x504A9C + 4 * j, k.jog_scale[j]);
    const sim::prologue::SteerFollow& s = k.follow;
    const u32 T = 0x715240;
    for (u32 r = 0; r < 2; ++r) {
        cmp("gain", T + 4 * r, s.gain[r]), cmp("centering", T + 0x08 + 4 * r, s.centering[r]);
        cmp("toward", T + 0x10 + 4 * r, s.toward[r]), cmp("damping", T + 0x18 + 4 * r, s.damping[r]);
        cmp("by speed", T + 0x50 + 4 * r, gt3r::ee::F::raw(s.by_speed[r]));
        cmp("speed lo", T + 0x58 + 4 * r, s.speed_lo[r]), cmp("speed hi", T + 0x60 + 4 * r, s.speed_hi[r]);
    }
    for (u32 i = 0; i < 4; ++i) {
        cmp("rate", T + 0x20 + 4 * i, s.rate[i]), cmp("rate back", T + 0x30 + 4 * i, s.rate_back[i]);
        cmp("rate toward", T + 0x40 + 4 * i, s.rate_toward[i]), cmp("fast", T + 0x68 + 4 * i, s.fast[i]);
        cmp("fast back", T + 0x78 + 4 * i, s.fast_back[i]), cmp("fast toward", T + 0x88 + 4 * i, s.fast_toward[i]);
    }
    std::printf("    controls' set-up: %d words equal to the program's, %d differ; the steering prefilter (mode, gain) %u %u / %u %u / %u %u\n",
                equal, differ, k.prefilter_mode[0], k.prefilter_gain[0], k.prefilter_mode[1], k.prefilter_gain[1], k.prefilter_mode[2],
                k.prefilter_gain[2]);
}

} // namespace

void add_input_taps(PTaps& taps, const Race& race) {
    const Race* r = &race;
    taps.add({0x3DEDE0, "engine's driver input", [r](PTapCall& c) {
                  const Pad& p = r->pad[c.car_index];
                  auto axis = [](float v) { return s32(std::lround(std::clamp(v, -1.0f, 1.0f) * 4096.0f)); };
                  ex::PadFrame f;
                  f.flags = 1 | 2 | 4, f.kind = 3, f.shift = gt3r::s8(p.shift);
                  f.steer = gt3r::s16(-axis(p.steer)), f.throttle = u16(axis(p.throttle)), f.brake = u16(axis(p.brake));
                  ex::driver_input_pad(c.car, c.inputs.pad, f, 0);
              }});
    taps.add({0x3DEDE0, "Prologue's driver input", [r](PTapCall& c) {
                  static sim::prologue::Tables t;
                  const Pad& p = r->pad[c.car_index];
                  auto axis = [](float v) { return s32(std::lround(std::clamp(v, -1.0f, 1.0f) * 4096.0f)); };
                  ex::PadFrame f;
                  f.flags = 1 | 2 | 4, f.kind = 3, f.shift = gt3r::s8(p.shift);
                  f.steer = gt3r::s16(-axis(p.steer)), f.throttle = u16(axis(p.throttle)), f.brake = u16(axis(p.brake));
                  // The controls' set-up on the controller settings' bytes as the harness has them (the program's set-up has
                  // run: its own results are compared once, below).
                  std::array<u8, sim::prologue::kControlBytes> bytes{};
                  for (u32 k = 0; k < bytes.size(); ++k) bytes[k] = c.ee.r8(0x504950 + k);
                  t.controls = sim::prologue::controls(bytes.data());
                  t.pedal_ramps = bytes[0x11] != 0;
                  t.throttle_up = t.controls.throttle_up, t.throttle_down = t.controls.throttle_down;
                  t.brake_up = t.controls.brake_up, t.brake_down = t.controls.brake_down;
                  static bool checked = false;
                  if (!checked) {
                      checked = true;
                      check_controls(c.ee, t.controls);
                  }
                  std::array<u8, sim::rules::kWheelLayoutSize> row{};
                  const u32 layout = 0x503FA8 + sim::rules::kWheelLayoutSize * c.ee.r8(c.ee.r32(c.car_addr + 0x10) + 0x30);
                  for (u32 k = 0; k < row.size(); ++k) row[k] = c.ee.r8(layout + k);
                  sim::prologue::driver_input_pad(c.car, c.inputs.pad, f, c.ee.r8(c.ee.r32(c.car_addr + 4) + 0xDBED) == 0xFF ? 0 : c.ee.r8(c.ee.r32(c.car_addr + 4) + 0xDBED),
                                                  sim::rules::wheel_layout(row.data()), t);
              }, {"ai."}});
    taps.add({0x3BF608, "engine's preparation", [](PTapCall& c) { ex::prep(c.car, c.inputs.prep); }});
}

Pad test_drive(int i) {
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

std::vector<PTapStat> check(Disc& disc, const std::string& course, const std::string& car, int steps, const std::string& which, std::string& err) {
    if (!is_prologue(disc)) return err = "not Prologue's program", std::vector<PTapStat>{};
    g_disc = &disc;
    db::SpecDb sdb(
        [&disc](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = disc.volume().find(path);
            return e ? std::optional<Bytes>(disc.volume().read(*e)) : std::nullopt;
        },
        [&disc](const std::string& dir) { return disc.volume().list_dir(dir); }, "american");
    Ee ee;
    boot(ee, disc);
    Race r;
    if (!setup_race(ee, disc, sdb, course, {car}, 1, r, err, true)) return {};
    const sim::Program program = sim::Program::load(disc);
    const ex::ProgramData data = sim::core_program(program);
    const ex::RaceInputs inputs = ex::race_inputs(data, ex::Conductor::SinglePlayer, ex::RaceConfig{}, false, 0);
    PTaps taps(ee, r, inputs);
    if (which == "prologue") add_prologue_taps(taps, disc, course);
    else if (which == "input") add_input_taps(taps, r);
    else add_engine_taps(taps);
    try {
        for (int i = 0; i < steps; ++i) {
            r.pad[0] = test_drive(i);
            step(ee, r);
        }
    } catch (const std::exception& e) {
        err = e.what();
        return {};
    }
    return taps.stats();
}

} // namespace gt4r::diff::prologue
