#include "taps.h"

#include "gt4r/sim/rules.h"
#include "sim/exact/car_fields.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace gt4r::diff {

using sim::layout::Block;

const std::vector<Slot>& slots() {
    static const std::vector<Slot> table = [] {
        std::vector<Slot> v;
        static const sim::Record probe{};
        const u8* base = reinterpret_cast<const u8*>(&probe);
        auto add = [&](const char* n, int i, sim::layout::Place place, const auto& m) {
            using T = std::remove_cvref_t<decltype(m)>;
            const bool real = std::is_same_v<T, gt3r::ee::F>;
            if (!place) return;
            Slot s;
            s.name = i < 0 ? std::string(n) : std::string(n) + "[" + std::to_string(i) + "]";
            s.size = real ? 4 : u32(sizeof(T)), s.place = place;
            s.member = size_t(reinterpret_cast<const u8*>(&m) - base), s.real = real;
            v.push_back(std::move(s));
        };
        ex::fields(probe.car, [&](const char* n, int i, u32 off, const auto& m) { add(n, i, sim::layout::place(off), m); });
        sim::extra_fields(probe.x, [&](const char* n, int i, Block b, u32 off, const auto& m) { add(n, i, {b, off}, m); });
        return v;
    }();
    return table;
}

Image read_image(Ee& ee, u32 car) {
    Image im;
    im.car.resize(sim::layout::kCarSize), im.params.resize(sim::layout::kParamsSize);
    ee.read_bytes(car, im.car.data(), u32(im.car.size()));
    ee.read_bytes(ee.r32(car + 0x10), im.params.data(), u32(im.params.size()));
    return im;
}

namespace {
const u8* at(const Image& im, const Slot& s) { return (s.place.block == Block::Car ? im.car.data() : im.params.data()) + s.place.offset; }
} // namespace

void load(sim::Record& c, const Image& image) {
    for (const Slot& s : slots()) std::memcpy(reinterpret_cast<u8*>(&c) + s.member, at(image, s), s.size);
}

void Taps::add(Tap tap) {
    const size_t i = taps_.size();
    stats_.emplace_back();
    stats_.back().name = tap.name;
    ee_.add_probe(tap.address, [this, i](Ee&) { enter(i); });
    taps_.push_back(std::move(tap));
}

void Taps::enter(size_t tap) {
    const Tap& t = taps_[tap];
    const u32 car = t.car_of(ee_);
    int index = -1;
    for (int i = 0; i < race_.cars; ++i)
        if (race_.car(i) == car) index = i;
    if (index < 0) return;
    Pending p{tap, ee_.g32(29), car, index, read_image(ee_, car), sim::Record{}, {}};
    p.mine.car.behaviour = sim::rules::behaviour();
    load(p.mine, p.before);
    {
        u8 row[sim::rules::kWheelLayoutSize];
        ee_.read_bytes(sim::rules::kWheelLayouts + sim::rules::kWheelLayoutSize * p.before.params[0x30], row, sizeof row);
        p.mine.layout = sim::rules::wheel_layout(row);
    }
    TapCall call{ee_, race_, index, p.mine, p.mine.car, inputs_, {}};
    t.ours(call);
    p.outs = std::move(call.outs);
    const u32 ra = ee_.g32(31);
    const bool fresh = pending_.find(ra) == pending_.end();
    pending_[ra].push_back(std::move(p));
    if (fresh) ee_.add_probe(ra, [this, ra](Ee&) { leave(ra); });
}

void Taps::leave(u32 ra) {
    auto it = pending_.find(ra);
    if (it == pending_.end() || it->second.empty() || it->second.back().sp != ee_.g32(29)) return;
    Pending p = std::move(it->second.back());
    it->second.pop_back();
    TapStat& st = stats_[p.tap];
    ++st.calls;
    const Image after = read_image(ee_, p.car);
    // What the program wrote, and whether a field of ours has its place.
    std::vector<u8> placed_car(after.car.size(), 0), placed_params(after.params.size(), 0);
    bool clean = true;
    const std::vector<std::string>& skip = taps_[p.tap].skip;
    auto note = [&](const std::string& name, bool real, double a, double b) {
        clean = false;
        FieldStat& f = st.fields[name];
        ++f.off;
        const double d = std::fabs(a - b), rel = real ? d / std::max(std::fabs(a), 1e-6) : d;
        if (rel >= f.worst) {
            f.worst = rel;
            char text[160];
            std::snprintf(text, sizeof text, "the program's %.9g, ours %.9g (car %d, call %llu)", a, b, p.index, static_cast<unsigned long long>(st.calls));
            f.example = text;
        }
    };
    for (const Out& o : p.outs) {
        u32 theirs = 0;
        if (o.where == Out::F0) theirs = ee_.fpr[0];
        else if (o.where == Out::V0) theirs = ee_.g32(2);
        else ee_.read_bytes(o.address, reinterpret_cast<u8*>(&theirs), o.size);
        if (o.size < 4 && o.where != Out::Mem) theirs &= (1u << (8 * o.size)) - 1;
        if (theirs == o.bits) continue;
        gt3r::ee::F x, y;
        x.bits = theirs, y.bits = o.bits;
        note(o.name, o.real, o.real ? double(x.host()) : double(theirs), o.real ? double(y.host()) : double(o.bits));
    }
    for (const Slot& s : slots()) {
        std::vector<u8>& placed = s.place.block == Block::Car ? placed_car : placed_params;
        for (u32 i = 0; i < s.size; ++i) placed[s.place.offset + i] = 1;
        const u8* theirs = at(after, s);
        const u8* was = at(p.before, s);
        const u8* mine = reinterpret_cast<const u8*>(&p.mine) + s.member;
        if (std::memcmp(theirs, was, s.size) != 0) ++st.written[s.name];
        if (std::memcmp(theirs, mine, s.size) == 0) continue;
        bool skipped = false;
        for (const std::string& k : skip) skipped = skipped || s.name.compare(0, k.size(), k) == 0;
        if (skipped) continue;
        double a = 0, b = 0;
        if (s.real) {
            gt3r::ee::F x, y;
            std::memcpy(&x, theirs, 4), std::memcpy(&y, mine, 4);
            a = double(x.host()), b = double(y.host());
        } else {
            u32 x = 0, y = 0;
            std::memcpy(&x, theirs, s.size), std::memcpy(&y, mine, s.size);
            a = double(x), b = double(y);
        }
        note(s.name, s.real, a, b);
    }
    for (u32 o = 0; o < after.car.size(); ++o)
        if (after.car[o] != p.before.car[o] && !placed_car[o]) ++st.unplaced_car[o & ~3u];
    for (u32 o = 0; o < after.params.size(); ++o)
        if (after.params[o] != p.before.params[o] && !placed_params[o]) ++st.unplaced_params[o & ~3u];
    st.clean += clean;
}

void Taps::print() const {
    for (const TapStat& st : stats_) {
        std::printf("  %-28s %llu calls, %llu with every placed field equal\n", st.name.c_str(), static_cast<unsigned long long>(st.calls),
                    static_cast<unsigned long long>(st.clean));
        for (const auto& [name, f] : st.fields)
            std::printf("      %-22s differs in %llu, most by %.3g: %s\n", name.c_str(), static_cast<unsigned long long>(f.off), f.worst, f.example.c_str());
        if (!st.written.empty()) {
            std::printf("      writes:");
            size_t n = 0;
            for (const auto& [name, k] : st.written)
                if (n++ < 40) std::printf(" %s", name.c_str());
            std::printf("%s\n", st.written.size() > 40 ? " ..." : "");
        }
        auto list = [](const char* what, const std::map<u32, u64>& m) {
            if (m.empty()) return;
            std::printf("      writes without a place, %s:", what);
            size_t n = 0;
            for (const auto& [o, k] : m)
                if (n++ < 48) std::printf(" +%03X", o);
            std::printf("%s\n", m.size() > 48 ? " ..." : "");
        };
        list("car", st.unplaced_car), list("parameters", st.unplaced_params);
    }
}

} // namespace gt4r::diff
