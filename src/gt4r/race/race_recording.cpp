#include "gt4r/race/race_recording.h"

#include "gt4r/save/codec.h"

#include <algorithm>
#include <cstring>

namespace gt4r::race {

replay::Demonstration make_recording(const RecordingSetup& s, const std::vector<sim::Entry>& entries, const std::vector<replay::PadRecord>& records) {
    replay::Demonstration d;
    d.version = 0x40000;
    d.race_mode = s.bspec ? 1 : 0;
    d.race_class = s.race_class;
    auto put = [&](size_t at, u32 v) { std::memcpy(d.information.data() + at, &v, 4); };
    // (+0xC and +0x1C: 2 and 1 in every recording of the disc's.)
    put(0, u32(s.laps)), put(4, u32(s.penalty)), put(8, u32(s.tire_damage)), put(0xC, 2), put(0x1C, 1);
    std::copy(s.driver.begin(), s.driver.end(), d.information.begin() + 0x20);
    d.code_b0 = s.event, d.code_b8 = s.course;
    d.time_ms = s32(s.time_ms), d.time2_ms = s32(s.best_ms);
    d.header.assign(0x1AA4, 0);
    std::copy_n(s.playlist.begin(), std::min<size_t>(s.playlist.size(), 0x810), d.header.begin() + 0x148);
    for (size_t i = 0; i < entries.size(); ++i) {
        const sim::Entry& e = entries[i];
        replay::DemoEntry out;
        out.equipment = e.equipment;
        if (e.condition) out.condition = *e.condition;
        else out.condition[0x14] = 1;
        out.x198 = i == 0 ? 0x100 : 0; // (the player's and the computers' in every recording)
        out.grid_place = e.grid_place >= 0 ? e.grid_place : s32(i);
        out.pedal_style = e.automatic ? 0 : 1, out.driver_kind = e.driver;
        out.aids = {u8(e.asm_on), u8(e.tcs_on), u8(e.wheel_aid), u8(i == 0 && s.race_class != "RaceArcadeSingle" ? 0 : 2)}; // (the last: a license's player's 0)
        d.entries.push_back(out);
    }
    replay::ControlStream stream;
    stream.frames = records, stream.frame_count = u32(records.size() + 1);
    d.streams.push_back(std::move(stream));
    return d;
}

Bytes recording_file(const replay::Demonstration& d, const sim::Program& program, u32 random) {
    const Bytes file = d.encode(program);
    return file.empty() ? Bytes{} : save::scramble(file, 0, random, save::FileKind::Replay);
}

replay::PadRecord mail_record(const RecordMail& m) {
    replay::PadRecord r;
    r.bytes[3] = 6; // (the count of the bytes from +0x10)
    r.bytes[0x10] = m.pit, r.bytes[0x11] = m.front, r.bytes[0x12] = m.rear, r.bytes[0x13] = m.fuel, r.bytes[0x14] = m.command, r.bytes[0x15] = m.argument;
    return r;
}

namespace {
constexpr size_t kDetail = 17; // a car's: four wheels' brake heat (f32), a byte (1 the clutch is locked, 2 held in its rolling start)
}

Bytes replay_details(const std::vector<std::vector<CarState>>& steps) {
    Bytes b;
    for (const std::vector<CarState>& s : steps)
        for (const CarState& c : s) {
            const size_t at = b.size();
            b.resize(at + kDetail);
            for (size_t w = 0; w < 4; ++w) std::memcpy(&b[at + 4 * w], &c.wheels[w].brake_heat, 4);
            b[at + 16] = u8((c.clutch_closed ? 1 : 0) | (c.rolling ? 2 : 0));
        }
    return b;
}

void apply_details(std::vector<std::vector<CarState>>& steps, ByteSpan details) {
    size_t cars = 0;
    for (const std::vector<CarState>& s : steps) cars += s.size();
    if (details.size() != cars * kDetail) return;
    size_t at = 0;
    for (std::vector<CarState>& s : steps)
        for (CarState& c : s) {
            for (size_t w = 0; w < 4; ++w) std::memcpy(&c.wheels[w].brake_heat, &details[at + 4 * w], 4);
            c.clutch_closed = details[at + 16] & 1, c.rolling = details[at + 16] & 2;
            at += kDetail;
        }
}

} // namespace gt4r::race
