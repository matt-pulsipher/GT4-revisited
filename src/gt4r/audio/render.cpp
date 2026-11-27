#include "gt4r/audio/render.h"

#include "audio/spu_reverb.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::audio {

OfflineSound::OfflineSound(bool reverb) : driver_(mixer_) {
    with_output_level(mixer_);
    with_levels_at_once(mixer_);
    with_surround_cue(mixer_);
    if (reverb) mixer_.set_reverb(gt3r::SpuReverb::StudioC, 0x0FFF, 0x0FFF);
}

void OfflineSound::advance(int updates, const std::function<void(int)>& each) {
    const size_t frames = SoundDriver::kSamplesPerUpdate;
    for (int i = 0; i < updates; ++i) {
        if (each) each(i);
        driver_.update();
        const size_t at = pcm.size();
        pcm.resize(at + frames * 2);
        mixer_.mix(pcm.data() + at, frames);
        max_voices = std::max(max_voices, mixer_.active_voices());
    }
}

MusicRender render_music(std::shared_ptr<const sound::InstBank> bank, std::shared_ptr<const sound::SqtBank> sequences, int number, int loops,
                         double max_seconds, double tail_seconds, float volume) {
    OfflineSound s;
    s.driver().set_volume(SoundDriver::Kind::Music, volume);
    s.driver().play_music(std::move(bank), std::move(sequences), number);
    MusicRender out;
    int seen = 0;
    const int limit = int(max_seconds * SoundDriver::kUpdateRate);
    for (int i = 0; i < limit && s.driver().music_playing(); ++i) {
        s.advance(1);
        const int now = s.driver().music_loops();
        for (; seen < now; ++seen) out.loop_frames.push_back(s.pcm.size() / 2);
        if (seen >= loops) break;
    }
    out.end_frame = s.pcm.size() / 2;
    s.driver().stop_music(false);
    s.advance_seconds(tail_seconds);
    out.pcm = std::move(s.pcm);
    out.max_voices = s.max_voices;
    return out;
}

Level measure(const std::vector<s16>& pcm) {
    Level l;
    double sum = 0;
    for (size_t i = 0; i < pcm.size(); ++i) {
        const int a = std::abs(int(pcm[i]));
        l.peak = std::max(l.peak, a);
        l.clipped += a >= 32767;
        sum += double(a) * a;
        if (a > 32) l.sounding_until = double(i / 2) / gt3r::VoiceMixer::kRate;
    }
    if (!pcm.empty()) l.rms = std::sqrt(sum / double(pcm.size())) / 32768.0;
    l.seconds = double(pcm.size() / 2) / gt3r::VoiceMixer::kRate;
    return l;
}

double dominant_frequency(const std::vector<s16>& pcm, size_t first, size_t frames, double lo, double hi) {
    const size_t total = pcm.size() / 2;
    if (first >= total) return 0;
    frames = std::min(frames, total - first);
    const double rate = gt3r::VoiceMixer::kRate;
    const size_t lag_lo = std::max<size_t>(2, size_t(rate / hi)), lag_hi = std::min<size_t>(frames / 2, size_t(rate / lo));
    if (lag_hi <= lag_lo) return 0;
    std::vector<double> x(frames);
    double mean = 0;
    for (size_t i = 0; i < frames; ++i) mean += pcm[(first + i) * 2];
    mean /= double(frames);
    double energy = 0;
    for (size_t i = 0; i < frames; ++i) {
        x[i] = pcm[(first + i) * 2] - mean;
        energy += x[i] * x[i];
    }
    if (energy < 1.0) return 0;
    // The first lag whose correlation is close to the best one: the fundamental, not a multiple.
    std::vector<double> corr(lag_hi + 1, 0.0);
    double best = 0;
    for (size_t lag = lag_lo; lag <= lag_hi; ++lag) {
        double c = 0, e0 = 0, e1 = 0;
        for (size_t i = 0; i + lag < frames; ++i) {
            c += x[i] * x[i + lag];
            e0 += x[i] * x[i];
            e1 += x[i + lag] * x[i + lag];
        }
        corr[lag] = e0 > 0 && e1 > 0 ? c / std::sqrt(e0 * e1) : 0;
        best = std::max(best, corr[lag]);
    }
    if (best <= 0.2) return 0;
    for (size_t lag = lag_lo + 1; lag < lag_hi; ++lag) {
        if (corr[lag] < 0.9 * best || corr[lag] < corr[lag - 1] || corr[lag] < corr[lag + 1]) continue;
        // A parabola through the peak and its neighbors places it between samples.
        const double a = corr[lag - 1], b = corr[lag], c = corr[lag + 1];
        const double denom = a - 2 * b + c;
        const double shift = denom != 0 ? 0.5 * (a - c) / denom : 0;
        return rate / (double(lag) + shift);
    }
    return 0;
}

int tuning_offset_cents(const std::vector<s16>& pcm, size_t first, size_t frames, double* contrast) {
    constexpr size_t kWindow = 16384;
    constexpr int kOffsets = 10, kNotes = 24;
    const size_t total = pcm.size() / 2;
    double energy[kOffsets] = {};
    std::vector<double> window(kWindow), x(kWindow);
    for (size_t i = 0; i < kWindow; ++i) window[i] = 0.5 - 0.5 * std::cos(6.283185307179586 * double(i) / double(kWindow - 1));
    for (size_t at = first; at + kWindow <= std::min(total, first + frames); at += kWindow) {
        for (size_t i = 0; i < kWindow; ++i) x[i] = (pcm[(at + i) * 2] + pcm[(at + i) * 2 + 1]) * 0.5 * window[i];
        for (int o = 0; o < kOffsets; ++o)
            for (int n = 0; n < kNotes; ++n) {
                // Goertzel at the note's frequency.
                const double f = 440.0 * std::pow(2.0, (n + (o - 5) * 0.1) / 12.0);
                const double w = 6.283185307179586 * f / gt3r::VoiceMixer::kRate, c = 2.0 * std::cos(w);
                double s0 = 0, s1 = 0, s2 = 0;
                for (size_t i = 0; i < kWindow; ++i) {
                    s0 = x[i] + c * s1 - s2;
                    s2 = s1;
                    s1 = s0;
                }
                energy[o] += s1 * s1 + s2 * s2 - c * s1 * s2;
            }
    }
    int best = 0;
    double sum = 0;
    for (int o = 0; o < kOffsets; ++o) {
        sum += energy[o];
        if (energy[o] > energy[best]) best = o;
    }
    if (contrast) *contrast = sum > energy[best] ? energy[best] / ((sum - energy[best]) / (kOffsets - 1)) : 0.0;
    return (best - 5) * 10;
}

Bytes wav_file(const std::vector<s16>& pcm, u32 rate) {
    Bytes out(44 + pcm.size() * 2);
    const auto put32 = [&](size_t at, u32 v) { std::memcpy(out.data() + at, &v, 4); };
    const auto put16 = [&](size_t at, u16 v) { std::memcpy(out.data() + at, &v, 2); };
    std::memcpy(out.data(), "RIFF", 4);
    put32(4, u32(out.size() - 8));
    std::memcpy(out.data() + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 2);
    put32(24, rate);
    put32(28, rate * 4);
    put16(32, 4);
    put16(34, 16);
    std::memcpy(out.data() + 36, "data", 4);
    put32(40, u32(pcm.size() * 2));
    if (!pcm.empty()) std::memcpy(out.data() + 44, pcm.data(), pcm.size() * 2);
    return out;
}

} // namespace gt4r::audio
