#include "gt4r/audio/race_tables.h"

#include "disc/core_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>

namespace gt4r::audio {

namespace {

float f32_at(ByteSpan d, size_t o) {
    float f;
    std::memcpy(&f, d.data() + o, 4);
    return f;
}
s16 s16_at(ByteSpan d, size_t o) { return s16(rd16(d, o)); }

// The views' gains: five rows of 24 words in hundredths. The last seven columns have no sound
// and hold 100 in every row, which is what the table is known by.
constexpr size_t kGainRow = 2 * RaceSoundTables::kChannels, kSpare = 7;

bool find_gains(ByteSpan d, RaceSoundTables& t, size_t* where = nullptr) {
    u8 tail[2 * kSpare];
    for (size_t i = 0; i < kSpare; ++i) tail[2 * i] = 100, tail[2 * i + 1] = 0;
    const size_t size = kGainRow * RaceSoundTables::kViews, lead = kGainRow - sizeof tail;
    const u8* const begin = d.data();
    const u8* const end = begin + d.size();
    for (const u8* p = begin;;) {
        p = std::search(p, end, tail, tail + sizeof tail);
        if (p == end) return false;
        const size_t at = size_t(p - begin);
        bool ok = at >= lead && (at - lead) % 2 == 0 && at - lead + size <= d.size();
        for (int v = 1; ok && v < RaceSoundTables::kViews; ++v) ok = std::memcmp(p + kGainRow * size_t(v), tail, sizeof tail) == 0;
        for (size_t k = 0; ok && k < size; k += 2) ok = s16_at(d, at - lead + k) >= 0;
        if (ok) {
            for (int v = 0; v < RaceSoundTables::kViews; ++v)
                for (int c = 0; c < RaceSoundTables::kChannels; ++c)
                    t.gain[size_t(v)][size_t(c)] = float(s16_at(d, at - lead + kGainRow * size_t(v) + 2 * size_t(c))) * 0.01f;
            if (where) *where = at - lead;
            return true;
        }
        ++p;
    }
}

// Prologue's views' gains (its 0x3B3978 reads them): five rows of sixteen floats, one per
// channel, with 1.0 in the last column of every row, the in-car rush (column 7) only from the
// car's own in-car view, and the turbo's two (columns 2, 3) silent from the other cars' views.
bool find_float_gains(ByteSpan d, RaceSoundTables& t) {
    constexpr size_t kRow = 4 * 16, kSize = kRow * RaceSoundTables::kViews;
    for (size_t at = 0; at + kSize <= d.size(); at += 4) {
        if (f32_at(d, at + 15 * 4) != 1.0f) continue;
        bool ok = true;
        for (size_t k = 0; ok && k < 16 * size_t(RaceSoundTables::kViews); ++k) {
            const float f = f32_at(d, at + 4 * k);
            ok = std::isfinite(f) && f >= 0.0f && f <= 100.0f && (k % 16 != 15 || f == 1.0f);
        }
        for (int v = 1; ok && v < RaceSoundTables::kViews; ++v) ok = f32_at(d, at + kRow * size_t(v) + 7 * 4) == 0.0f;
        for (int v : {1, 3}) ok = ok && f32_at(d, at + kRow * size_t(v) + 2 * 4) == 0.0f && f32_at(d, at + kRow * size_t(v) + 3 * 4) == 0.0f;
        ok = ok && f32_at(d, at + 7 * 4) > 0.0f && f32_at(d, at) > 0.0f;
        if (!ok) continue;
        for (int v = 0; v < RaceSoundTables::kViews; ++v)
            for (int c = 0; c < 16; ++c) t.gain[size_t(v)][size_t(c)] = f32_at(d, at + kRow * size_t(v) + 4 * size_t(c));
        return true;
    }
    return false;
}

// The microphones: for each output mode three pairs of floats (slope, reach). The engine's pair
// is the same in every mode and every reach is the same distance.
// The afterfire's table: right before the views' gains, eight pairs of powers (x 10 PS), each
// pair rising.
bool find_afterfire(ByteSpan d, size_t gains, RaceSoundTables& t) {
    if (gains < t.afterfire.size()) return false;
    const u8* p = d.data() + gains - t.afterfire.size();
    for (size_t k = 0; k < t.afterfire.size(); k += 2)
        if (p[k] == 0 || p[k] >= p[k + 1]) return false;
    std::memcpy(t.afterfire.data(), p, t.afterfire.size());
    return true;
}

bool find_mics(ByteSpan d, RaceSoundTables& t) {
    constexpr size_t kPairs = RaceSoundTables::kMics * RaceSoundTables::kModes, kRow = 8 * RaceSoundTables::kMics;
    const auto slope_ok = [](float f) { return f > 0.001f && f <= 4.0f; };
    const auto reach_ok = [](float f) { return f >= 0.5f && f <= 200.0f; };
    for (size_t at = 0; at + 8 * kPairs <= d.size(); at += 4) {
        if (std::memcmp(d.data() + at, d.data() + at + kRow, 8) != 0 || std::memcmp(d.data() + at, d.data() + at + 2 * kRow, 8) != 0) continue;
        const float slope = f32_at(d, at), reach = f32_at(d, at + 4);
        if (!slope_ok(slope) || !reach_ok(reach)) continue;
        bool ok = true, differ = false;
        for (size_t k = 0; ok && k < kPairs; ++k) {
            const float s = f32_at(d, at + 8 * k);
            ok = slope_ok(s) && std::memcmp(d.data() + at + 8 * k + 4, d.data() + at + 4, 4) == 0;
            differ |= s != slope;
        }
        if (!ok || !differ) continue;
        for (size_t k = 0; k < kPairs; ++k)
            t.mics[k / RaceSoundTables::kMics][k % RaceSoundTables::kMics] = {f32_at(d, at + 8 * k), f32_at(d, at + 8 * k + 4)};
        return true;
    }
    return false;
}

// The block of numbers: 42 bytes after the tire sounds' curve records (0x28 bytes each, each
// starting with an input's number and three counts of four points).
constexpr size_t kRecord = 0x28, kBlock = 0x2A;

bool curve_record(ByteSpan d, size_t at) { return at + kRecord <= d.size() && d[at] <= 1 && d[at + 1] == 4 && d[at + 2] == 4 && d[at + 3] == 4; }

bool find_params(ByteSpan d, RaceSoundTables& t) {
    for (size_t at = 0; at + kRecord <= d.size(); at += 4) {
        if (!curve_record(d, at) || (at >= kRecord && curve_record(d, at - kRecord))) continue;
        size_t n = 0;
        while (curve_record(d, at + kRecord * n)) ++n;
        if (n < 9 || n > 16) continue;
        const size_t b = at + kRecord * n;
        if (b + kBlock > d.size()) continue;
        const u8* p = d.data() + b;
        // (the wind's and the cabin's speeds and revs rise; the whine's torque is not 0)
        if (p[0x02] == 0 || p[0x10] >= p[0x11] || p[0x12] > p[0x13] || p[0x17] >= p[0x18] || p[0x09] >= p[0x0A]) continue;
        RaceSoundTables::Params& o = t.params;
        const float kmh10 = 10.0f / 3.6f; // a byte counts 10 km/h
        const float drag = float(p[0x0E]) / 10.0f;
        const auto square = [](float v) { return v * v; };
        o.wind_gain = drag * square(float(p[0x0F]) / 10.0f / 100.0f);
        o.wind_speed_lo = float(p[0x10]) * kmh10, o.wind_speed_hi = float(p[0x11]) * kmh10;
        o.wind_rpm_lo = float(p[0x12]) * 100.0f, o.wind_rpm_hi = float(p[0x13]) * 100.0f;
        o.wind_max = float(p[0x14]) / 10.0f;
        o.incar_gain = drag * square(float(p[0x16]) / 10.0f / 100.0f);
        o.incar_speed_lo = float(p[0x17]) * kmh10, o.incar_speed_hi = float(p[0x18]) * kmh10;
        o.incar_pitch_lo = float(p[0x19]) / 100.0f, o.incar_pitch_hi = float(p[0x1A]) / 100.0f;
        o.incar_max = float(p[0x1B]) / 10.0f;
        o.roar_narrow = float(p[0x06]) / 100.0f, o.roar_wide = float(p[0x07]) / 100.0f;
        o.roar_gain = float(p[0x08]) / 100.0f;
        o.roar_speed_lo = float(p[0x09]) * kmh10, o.roar_speed_hi = float(p[0x0A]) * kmh10;
        o.roar_pitch_lo = float(p[0x0B]) / 100.0f, o.roar_pitch_hi = float(p[0x0C]) / 100.0f;
        o.roar_max = float(p[0x0D]) / 10.0f;
        o.whine_gain_lo = float(p[0x00]) / 100.0f, o.whine_gain_hi = float(p[0x01]) / 100.0f;
        o.whine_torque = float(p[0x02]) * 9.8f; // kgf m
        o.whine_pitch_lo = float(p[0x04]) / 10.0f;
        o.whine_pitch_rate = (float(p[0x03]) / 10.0f - o.whine_pitch_lo) / 1047.1976f; // (over 10,000 turns a minute)
        o.engine_level = float(p[0x1C]) / 100.0f, o.exhaust_level = float(p[0x1D]) / 100.0f;
        o.turbine_level = float(p[0x1E]) / 100.0f, o.blowoff_scale = float(p[0x1F]) / 10.0f;
        o.clash_scale = float(p[0x26]) / 100.0f, o.clash_mid = float(p[0x28]) / 100.0f, o.clash_hard = float(p[0x29]) / 100.0f;
        return true;
    }
    return false;
}

} // namespace

float RaceSoundTables::afterfire_chance(int kind, bool boosted, float ps) const {
    if (!found_afterfire) return 0;
    const size_t row = 2 * (2 * size_t(std::clamp(kind, 0, 3)) + (boosted ? 1 : 0));
    const float lo = float(afterfire[row]) * 10.0f, hi = float(afterfire[row + 1]) * 10.0f;
    if (ps < lo) return 0;
    if (ps >= hi) return 0.75f;
    return (ps - lo) / (hi - lo) * 0.5f + 0.25f;
}

RaceSoundTables RaceSoundTables::flat() {
    RaceSoundTables t;
    for (auto& row : t.gain) row.fill(1.0f);
    // The two banks at their recorded level with the throttle open (a level of 64).
    t.params.engine_level = t.params.exhaust_level = 64.0f / 127.0f;
    return t;
}

RaceSoundTables RaceSoundTables::find(ByteSpan image) {
    RaceSoundTables t = flat();
    size_t gains = 0;
    t.found_gains = find_gains(image, t, &gains);
    t.found_afterfire = t.found_gains && find_afterfire(image, gains, t);
    if (!t.found_gains && find_float_gains(image, t)) t.found_gains = true, t.option_level = false, t.layer_adsr1 = LayerPlayer::kPrologueAdsr1;
    t.found_mics = find_mics(image, t);
    t.found_params = find_params(image, t);
    return t;
}

RaceSoundTables RaceSoundTables::from_core(ByteSpan core_file) {
    try {
        const gt3r::CoreImage core = gt3r::CoreImage::decode(core_file);
        return find(core.program);
    } catch (const std::exception&) {
        return flat();
    }
}

} // namespace gt4r::audio
