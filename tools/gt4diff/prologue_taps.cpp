// Prologue's record in Gran Turismo 3's layout and the taps of its race step (prologue_taps.h).

#include "prologue_taps.h"

#include "gt4r/sim/rules.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/tick.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace gt4r::diff::prologue {

namespace {

// The engine's record: the parameter block P at +0x10 .. +0xEC0; its tire records at P +0x398 + 0x1B4 r, its tire index
// from P +0xDD0, the wear's load terms from P +0xE6C. Prologue's state from the engine's +0x1320 (F +0x39C) is 4 bytes on.
constexpr u32 kP = 0x10, kPEnd = 0xEC0, kInsert = 0x1320;

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
    PTapCall call{ee_, index, p.mine, inputs_};
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
    taps.add({0x3B3810, "brake heat", [](PTapCall& t) { ex::brake_heat(t.car, t.inputs.brake_heat); }});
    taps.add({0x3B5A20, "wheel steer", [](PTapCall& t) { ex::wheel_steer(t.car); }});
    taps.add({0x375F58, "suspension", [](PTapCall& t) { ex::suspension(t.car, nullptr); }});
    taps.add({0x3765F8, "attitude", [](PTapCall& t) { ex::attitude(t.car, gt3r::ee::F()); }});
    taps.add({0x376EC8, "bump nudge", [](PTapCall& t) { ex::bump_nudge(t.car); }});
    taps.add({0x3DBB70, "aero", [](PTapCall& t) { ex::aero(t.car, t.inputs.prep.slip); }});
    taps.add({0x3C4D80, "ground frame", [](PTapCall& t) { ex::ground_frame(t.car); }});
}

} // namespace gt4r::diff::prologue
