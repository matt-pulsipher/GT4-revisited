#include "gt4r/sim/ai/demo_driver.h"

#include "gt4r/save/codec.h"

#include "gt4r/disc/disc.h"

#include <algorithm>

namespace gt4r::sim::ai {

race::CarInput pad_input(const replay::PadRecord& r) {
    race::CarInput in;
    in.steer = std::clamp(-float(r.steer()) / 4096.0f, -1.0f, 1.0f);
    in.throttle = std::clamp(float(r.throttle()) / 4096.0f, 0.0f, 1.0f);
    in.brake = std::clamp(float(r.brake()) / 4096.0f, 0.0f, 1.0f);
    in.shift_up = r.shift() > 0;
    in.shift_down = r.shift() < 0;
    return in;
}

std::optional<DemoRun> DemoRun::load_replay(ByteSpan file, const Program& program, const std::string& name, std::string* error) {
    std::optional<Bytes> plain = save::unscramble(file, 0, save::FileKind::Replay);
    if (!plain) {
        if (error) *error = "not a scrambled replay";
        return std::nullopt;
    }
    std::optional<replay::Demonstration> d = replay::Demonstration::parse(*plain, program, error);
    if (!d) return std::nullopt;
    DemoRun r;
    r.label = name;
    r.demo = std::move(*d);
    if (program.release() == Release::Europe && !name.empty() && name.back() == 'p') r.rate = 50; // (Rdemo<n>p)
    if (!r.demo.streams.empty())
        for (const replay::PadRecord& f : r.demo.streams[0].frames) r.pads.push_back({f.bytes});
    return r;
}

std::array<u8, 6> DemoRun::mail(size_t step) const {
    std::array<u8, 6> m{};
    if (!first_of(step)) return m;
    const replay::PadRecord& f = record(step);
    for (size_t k = 0; k < m.size(); ++k) m[k] = f.bytes[0x10 + k];
    return m;
}

std::vector<Entry> DemoRun::entries() const {
    std::vector<Entry> out;
    for (const replay::DemoEntry& d : demo.entries) {
        Entry e;
        e.equipment = d.equipment;
        e.grid_place = d.grid_place;
        e.automatic = d.pedal_style == 0;
        e.asm_on = d.aids[0] != 0, e.tcs_on = d.aids[1] != 0, e.wheel_aid = d.aids[2] != 0;
        e.condition = d.condition;
        e.driver = d.driver_kind;
        out.push_back(e);
    }
    return out;
}

const replay::PadRecord& DemoRun::record(size_t step) const {
    static const replay::PadRecord kEmpty{};
    step = index(step);
    if (demo.streams.empty() || step >= demo.streams[0].frames.size()) return kEmpty;
    return demo.streams[0].frames[step];
}

race::CarInput DemoRun::input(size_t step) const {
    race::CarInput in = pad_input(record(step));
    // (the record itself: the session's input phase reads it as the program's does, 0x349FD0)
    static const race::PadRecord kEmpty{};
    if (!first_of(step)) in.shift_up = in.shift_down = false;
    in.pad = index(step) < pads.size() ? &pads[index(step)] : &kEmpty;
    return in;
}

std::optional<DemoRun> DemoRun::load(Disc& disc, const Program& program, const std::string& label, std::string* error) {
    const gt3r::Volume::Entry* e = disc.volume().find(replay::demonstration_path(label));
    if (!e) {
        if (error) *error = "no demonstration " + replay::demonstration_path(label);
        return std::nullopt;
    }
    std::optional<replay::Demonstration> d = replay::Demonstration::parse(disc.volume().read(*e), program, error);
    if (!d) return std::nullopt;
    DemoRun r;
    r.label = label;
    r.demo = std::move(*d);
    if (program.release() == Release::Europe && label.rfind("uk", 0) == 0) r.rate = 50;
    if (!r.demo.streams.empty())
        for (const replay::PadRecord& f : r.demo.streams[0].frames) r.pads.push_back({f.bytes});
    return r;
}

} // namespace gt4r::sim::ai
